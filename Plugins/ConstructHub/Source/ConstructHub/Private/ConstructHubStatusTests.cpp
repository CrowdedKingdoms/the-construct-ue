#if WITH_DEV_AUTOMATION_TESTS

#include "ConstructHubStatusWidget.h"
#include "ConstructHubSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/AutomationTest.h"
#include "PluginDescriptor.h"
#include "UObject/StrongObjectPtr.h"

struct FConstructHubTestAccess
{
    static UConstructHubStatusWidget* FindWidget(const UConstructHubSubsystem& Hub, ULocalPlayer* Player)
    {
        const FConstructHubLocalPlayer* Entry = Hub.Players.Find(Player);
        return Entry ? Entry->Widget.Get() : nullptr;
    }

    static int32 CountWidgets(const UConstructHubSubsystem& Hub)
    {
        int32 Count = 0;
        for (const auto& Entry : Hub.Players)
        {
            Count += Entry.Value.Widget ? 1 : 0;
        }
        return Count;
    }

    static void NotifyController(UConstructHubSubsystem& Hub, APlayerController* Controller, ULocalPlayer* Player)
    {
        Hub.ControllerChanged(Controller, TWeakObjectPtr<ULocalPlayer>(Player));
    }
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructHubStatusTextTest, "Construct.Hub.HUD.StatusText", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FConstructHubStatusTextTest::RunTest(const FString&)
{
    TStrongObjectPtr<UConstructHubStatusWidget> Widget(NewObject<UConstructHubStatusWidget>());
    Widget->SetHubStatus(12, {});
    TestEqual(TEXT("A missing level keeps the HUD disconnected"), Widget->GetStatusText().ToString(), FString(TEXT("World hub not connected")));
    Widget->SetHubStatus({}, 3);
    TestEqual(TEXT("A missing pulse keeps the HUD disconnected"), Widget->GetStatusText().ToString(), FString(TEXT("World hub not connected")));
    Widget->SetHubStatus(12, 3);
    TestEqual(TEXT("Both values appear on one line"), Widget->GetStatusText().ToString(), FString(TEXT("World pulse 12, level 3")));
    Widget->SetHubStatus(0, 1);
    TestEqual(TEXT("Zero is a ready pulse value"), Widget->GetStatusText().ToString(), FString(TEXT("World pulse 0, level 1")));
    Widget->SetHubStatus({}, {});
    TestEqual(TEXT("Losing readiness clears previously displayed values"), Widget->GetStatusText().ToString(), FString(TEXT("World hub not connected")));
    return true;
}

namespace
{
    struct FHubTestWorld
    {
        UWorld* World = nullptr;
        TStrongObjectPtr<UGameInstance> Game;
        TArray<TStrongObjectPtr<ULocalPlayer>> Players;

        ~FHubTestWorld()
        {
            if (World)
            {
                for (const TStrongObjectPtr<ULocalPlayer>& Player : Players)
                {
                    if (Game->GetLocalPlayers().Contains(Player.Get()))
                    {
                        Game->RemoveLocalPlayer(Player.Get());
                    }
                }
                World->SetGameInstance(nullptr);
                GEngine->DestroyWorldContext(World);
                World->DestroyWorld(false);
            }
        }

        UConstructHubSubsystem* Initialize()
        {
            UWorld::InitializationValues Options;
            Options.InitializeScenes(true).AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
            World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Options);
            if (!World)
            {
                return nullptr;
            }
            GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
            Game.Reset(NewObject<UGameInstance>(GEngine));
            World->SetGameInstance(Game.Get());
            World->InitializeActorsForPlay(FURL());
            UConstructHubSubsystem* Hub = World->GetSubsystem<UConstructHubSubsystem>();
            if (Hub)
            {
                Hub->OnWorldBeginPlay(*World);
            }
            return Hub;
        }

        APlayerController* AddLocalPlayer(int32 Index)
        {
            ULocalPlayer* Player = NewObject<ULocalPlayer>(GEngine);
            Players.Emplace(Player);
            Game->AddLocalPlayer(Player, FPlatformUserId::CreateFromInternalId(Index));
            APlayerController* Controller = World->SpawnActor<APlayerController>();
            if (Controller)
            {
                Controller->SetPlayer(Player);
            }
            return Controller;
        }
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructHubStatusInstallationTest, "Construct.Hub.HUD.LocalPlayers", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FConstructHubStatusInstallationTest::RunTest(const FString&)
{
    if (!TestNotNull(TEXT("Initialized engine"), GEngine))
    {
        return false;
    }
    FHubTestWorld Fixture;
    UConstructHubSubsystem* Hub = Fixture.Initialize();
    if (!TestNotNull(TEXT("World has a Hub subsystem"), Hub))
    {
        return false;
    }
    APlayerController* First = Fixture.AddLocalPlayer(0);
    APlayerController* Second = Fixture.AddLocalPlayer(1);
    if (!TestNotNull(TEXT("First controller"), First) || !TestNotNull(TEXT("Second controller"), Second))
    {
        return false;
    }

    ULocalPlayer* FirstPlayer = Fixture.Players[0].Get();
    ULocalPlayer* SecondPlayer = Fixture.Players[1].Get();
    UConstructHubStatusWidget* FirstWidget = FConstructHubTestAccess::FindWidget(*Hub, FirstPlayer);
    UConstructHubStatusWidget* SecondWidget = FConstructHubTestAccess::FindWidget(*Hub, SecondPlayer);
    if (!TestNotNull(TEXT("First player gets a badge"), FirstWidget) || !TestNotNull(TEXT("Second player gets a badge"), SecondWidget))
    {
        return false;
    }
    TestNotEqual(TEXT("Each player owns a different badge"), FirstWidget, SecondWidget);
    TestEqual(TEXT("The initial badge shows disconnected without an SDK session"), FirstWidget->GetStatusText().ToString(), FString(TEXT("World hub not connected")));
    FConstructHubTestAccess::NotifyController(*Hub, First, FirstPlayer);
    TestEqual(TEXT("A duplicate notification keeps the original badge"), FConstructHubTestAccess::FindWidget(*Hub, FirstPlayer), FirstWidget);
    TestEqual(TEXT("There is one badge per local player"), FConstructHubTestAccess::CountWidgets(*Hub), 2);

    APlayerController* Remote = Fixture.World->SpawnActor<APlayerController>();
    if (!TestNotNull(TEXT("Controller with no local player"), Remote))
    {
        return false;
    }
    FConstructHubTestAccess::NotifyController(*Hub, Remote, FirstPlayer);
    TestNull(TEXT("A controller outside the local player's slot gets no badge"), FConstructHubTestAccess::FindWidget(*Hub, FirstPlayer));
    TestEqual(TEXT("The remote controller does not affect the second player's badge"), FConstructHubTestAccess::FindWidget(*Hub, SecondPlayer), SecondWidget);
    FConstructHubTestAccess::NotifyController(*Hub, First, FirstPlayer);
    TestNotNull(TEXT("The valid local controller restores its badge"), FConstructHubTestAccess::FindWidget(*Hub, FirstPlayer));
    TestEqual(TEXT("Only the two local players have badges"), FConstructHubTestAccess::CountWidgets(*Hub), 2);

    Fixture.Game->RemoveLocalPlayer(FirstPlayer);
    TestNull(TEXT("Removing a local player releases its badge"), FConstructHubTestAccess::FindWidget(*Hub, FirstPlayer));
    TestEqual(TEXT("The other player's badge is preserved"), FConstructHubTestAccess::FindWidget(*Hub, SecondPlayer), SecondWidget);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructHubShellDependencyTest, "Construct.Hub.HUD.ShellDependency", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FConstructHubShellDependencyTest::RunTest(const FString&)
{
    const TSharedPtr<IPlugin> Shell = IPluginManager::Get().FindPlugin(TEXT("ConstructShell"));
    if (!TestTrue(TEXT("Construct Shell plugin exists"), Shell.IsValid()))
    {
        return false;
    }
    TestFalse(TEXT("Construct Shell does not depend on the optional HUD"), Shell->GetDescriptor().Plugins.ContainsByPredicate(
        [](const FPluginReferenceDescriptor& Plugin)
        {
            return Plugin.Name == TEXT("ConstructHub");
        }));
    return true;
}

#endif
