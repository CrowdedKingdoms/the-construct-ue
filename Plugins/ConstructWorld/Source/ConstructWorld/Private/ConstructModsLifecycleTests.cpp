#if WITH_DEV_AUTOMATION_TESTS
#include "ConstructModsSubsystem.h"
#include "ConstructModMenu.h"
#include "ConstructModPanel.h"
#include "ConstructClaimPad.h"
#include "ConstructUIInputSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Misc/AutomationTest.h"
#include "Slate/SceneViewport.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/SViewport.h"

struct FConstructModsLifecycleTestAccess
{
    static void Retain(UConstructModsSubsystem& Owner, ULocalPlayer* Local, UConstructModMenu* Menu)
    {
        Owner.AddPlayer(Local);
        Owner.Players.FindChecked(Local).Menu = Menu;
    }
    static void Arm(UConstructModMenu& Menu, UConstructModPanel& Panel, UWorld* World)
    {
        Menu.bOpen = true;
        Menu.BoundWorld = World;
        Menu.UserId = 7;
        Menu.InputOwner = Menu.GetOwningLocalPlayer();
        ++Menu.Generation;
        Menu.Panel = &Panel;
        Menu.OpenGrid = 123;
        Panel.BoundWorld = World;
        Panel.bBound = true;
        Panel.GridId = 123;
        Panel.UserId = 7;
    }
    static bool IsOpen(const UConstructModMenu& Menu) { return Menu.bOpen; }
    static bool IsBound(const UConstructModPanel& Panel) { return Panel.bBound; }
    static bool HasPanel(const UConstructModMenu& Menu) { return Menu.Panel != nullptr; }
    static bool HasPlayer(const UConstructModsSubsystem& Owner, ULocalPlayer* Local) { return Owner.Players.Contains(Local); }
    static bool HasRetainedMenu(const UConstructModsSubsystem& Owner, ULocalPlayer* Local)
    {
        const auto* Entry = Owner.Players.Find(Local);
        return Entry && Entry->Menu != nullptr;
    }
    static void SignOut(UConstructModsSubsystem& Owner, bool bSuccess) { Owner.SignedOut(bSuccess); }
    static void AccessEnd(UConstructModsSubsystem& Owner) { Owner.AccessEnded(TEXT("expired"), TEXT("Access expired")); }
    static void WorldEnd(UConstructModsSubsystem& Owner) { Owner.Deinitialize(); }
    static void ControllerEnd(UConstructModsSubsystem& Owner, ULocalPlayer* Local) { Owner.ControllerChanged(nullptr, Local); }
    static void Remove(UConstructModsSubsystem& Owner, ULocalPlayer* Local) { Owner.RemovePlayer(Local); }
    static void BindPad(UConstructModMenu& Menu, AConstructClaimPad& Pad)
    {
        Menu.BoundPad = &Pad;
        Pad.OnOwnerShown.AddUniqueDynamic(&Menu, &UConstructModMenu::PadOwnerChanged);
        Pad.OnEndPlay.AddUniqueDynamic(&Menu, &UConstructModMenu::PadEnded);
    }
    static void Ownership(AConstructClaimPad& Pad, bool bMine)
    {
        FCrowdyChunkClaimInfo Reply;
        Reply.GridId = 123;
        Reply.OwnerUserId = bMine ? 7 : 8;
        Reply.bOwnedByMe = bMine;
        Pad.ShowSearch(&Reply);
    }
};

namespace
{
    struct FModsLifecycleFixture
    {
        UWorld* World = nullptr;
        TStrongObjectPtr<UGameViewportClient> Viewport;
        TSharedPtr<FSceneViewport> SceneViewport;
        TSharedPtr<SViewport> ViewportWidget;
        TSharedPtr<SWidget> MenuSlate;
        TStrongObjectPtr<ULocalPlayer> Local;
        TStrongObjectPtr<UConstructModsSubsystem> Owner;
        TStrongObjectPtr<UConstructModMenu> Menu;
        TStrongObjectPtr<UConstructModPanel> Panel;
        APlayerController* Controller = nullptr;
        UConstructUIInputSubsystem* Gate = nullptr;

        bool Initialize()
        {
            if (!GEngine)
            {
                return false;
            }
            UWorld::InitializationValues Init;
            Init.InitializeScenes(true).AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
            World = UWorld::CreateWorld(EWorldType::EditorPreview, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
            if (!World)
            {
                return false;
            }
            FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::EditorPreview);
            Context.SetCurrentWorld(World);
            Viewport.Reset(NewObject<UGameViewportClient>(GEngine));
            ViewportWidget = SNew(SViewport);
            SceneViewport = Viewport->CreateViewport(ViewportWidget.ToSharedRef());
            Context.GameViewport = Viewport.Get();
            World->InitializeActorsForPlay(FURL());
            Controller = World->SpawnActor<APlayerController>();
            if (!Controller)
            {
                return false;
            }
            Local.Reset(NewObject<ULocalPlayer>(GEngine));
            Local->PlayerAdded(Viewport.Get(), int32(0));
            Controller->SetPlayer(Local.Get());
            Gate = Local->GetSubsystem<UConstructUIInputSubsystem>();
            if (!Gate)
            {
                return false;
            }
            Owner.Reset(NewObject<UConstructModsSubsystem>(World));
            Menu.Reset(CreateWidget<UConstructModMenu>(Controller));
            Panel.Reset(CreateWidget<UConstructModPanel>(Controller));
            if (!Menu.IsValid() || !Panel.IsValid())
            {
                return false;
            }
            Menu->SetIsFocusable(true);
            Menu->SetOwningPlayer(Controller);
            Panel->SetOwningPlayer(Controller);
            if (Menu->GetOwningLocalPlayer() != Local.Get())
            {
                return false;
            }
            // Retain the visual root just as AddToPlayerScreen does in the running game.
            MenuSlate = Menu->TakeWidget();
            FConstructModsLifecycleTestAccess::Retain(*Owner, Local.Get(), Menu.Get());
            FConstructModsLifecycleTestAccess::Arm(*Menu, *Panel, World);
            Gate->SetSessionReady(true);
            Gate->SetModal(TEXT("Construct.Mods"), true, Menu.Get());
            return true;
        }
        ~FModsLifecycleFixture()
        {
            if (Owner.IsValid() && Local.IsValid())
            {
                FConstructModsLifecycleTestAccess::Remove(*Owner, Local.Get());
            }
            if (Local.IsValid())
            {
                Local->PlayerRemoved();
            }
            MenuSlate.Reset();
            Panel.Reset();
            Menu.Reset();
            Owner.Reset();
            SceneViewport.Reset();
            if (World)
            {
                GEngine->DestroyWorldContext(World);
                World->DestroyWorld(false);
            }
        }
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructModsOwnerSignalsTest, "Construct.Mods.WorldOwnerLifecycleAndModalRelease", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FConstructModsOwnerSignalsTest::RunTest(const FString&)
{
    for (int32 Signal = 0; Signal < 6; ++Signal)
    {
        FModsLifecycleFixture F;
        if (!TestTrue(TEXT("Real local-player fixture initialized"), F.Initialize()))
        {
            return false;
        }
        TestFalse(TEXT("Mods registration blocks gameplay"), F.Gate->IsGameplayInputAllowed());
        TestTrue(TEXT("Only Mods owns the navigation hold"), F.Gate->CanUseNavigationInput(TEXT("Construct.Mods")));
        FConstructModsLifecycleTestAccess::SignOut(*F.Owner, false);
        TestTrue(TEXT("Failed logout leaves binding alive"), FConstructModsLifecycleTestAccess::IsOpen(*F.Menu));
        F.Gate->SetModal(TEXT("OtherFixtureModal"), true);
        switch (Signal)
        {
        case 0: FConstructModsLifecycleTestAccess::SignOut(*F.Owner, true); break;
        case 1: FConstructModsLifecycleTestAccess::AccessEnd(*F.Owner); break;
        case 2: FConstructModsLifecycleTestAccess::ControllerEnd(*F.Owner, F.Local.Get()); break;
        case 3: FConstructModsLifecycleTestAccess::Remove(*F.Owner, F.Local.Get()); break;
        case 4: FConstructModsLifecycleTestAccess::WorldEnd(*F.Owner); break;
        case 5:
            F.Controller->DispatchBeginPlay();
            TestTrue(TEXT("Controller entered real actor lifecycle"), F.Controller->HasActorBegunPlay());
            TestTrue(TEXT("Controller really destroyed"), F.Controller->Destroy());
            break;
        }
        TestFalse(TEXT("Owner closes menu"), FConstructModsLifecycleTestAccess::IsOpen(*F.Menu));
        TestFalse(TEXT("Owner closes panel binding"), FConstructModsLifecycleTestAccess::IsBound(*F.Panel));
        TestFalse(TEXT("Closed menu releases panel"), FConstructModsLifecycleTestAccess::HasPanel(*F.Menu));
        TestFalse(TEXT("Unrelated modal remains a gameplay hold"), F.Gate->IsGameplayInputAllowed());
        TestTrue(TEXT("Mods registration was removed while other modal remains"), F.Gate->CanUseNavigationInput(TEXT("OtherFixtureModal")));
        F.Gate->SetModal(TEXT("OtherFixtureModal"), false);
        TestTrue(TEXT("Final modal removal restores gameplay"), F.Gate->IsGameplayInputAllowed());
        if (Signal == 2)
        {
            TestFalse(TEXT("Controller change drops retained widget"), FConstructModsLifecycleTestAccess::HasRetainedMenu(*F.Owner, F.Local.Get()));
        }
        if (Signal == 3)
        {
            TestFalse(TEXT("Player removal drops owner entry"), FConstructModsLifecycleTestAccess::HasPlayer(*F.Owner, F.Local.Get()));
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructModsPadLifecycleTest, "Construct.Mods.PadOwnershipAndActualDestroyCloseBinding", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FConstructModsPadLifecycleTest::RunTest(const FString&)
{
    for (bool bDestroy : {false, true})
    {
        FModsLifecycleFixture F;
        if (!TestTrue(TEXT("Real local-player fixture initialized"), F.Initialize()))
        {
            return false;
        }
        AConstructClaimPad* Pad = F.World->SpawnActor<AConstructClaimPad>();
        if (!TestNotNull(TEXT("Real pad exists"), Pad))
        {
            return false;
        }
        Pad->DispatchBeginPlay();
        TestTrue(TEXT("Pad entered genuine Engine lifecycle"), Pad->HasActorBegunPlay());
        FConstructModsLifecycleTestAccess::BindPad(*F.Menu, *Pad);
        FConstructModsLifecycleTestAccess::Ownership(*Pad, true);
        TestTrue(TEXT("Unchanged ownership retains binding"), FConstructModsLifecycleTestAccess::IsBound(*F.Panel));
        if (bDestroy)
        {
            TestTrue(TEXT("Actual Destroy routes EndPlay"), Pad->Destroy());
            TestFalse(TEXT("Pad ended play"), Pad->HasActorBegunPlay());
        }
        else
        {
            FConstructModsLifecycleTestAccess::Ownership(*Pad, false);
        }
        TestFalse(TEXT("Pad lifetime/ownership closes binding"), FConstructModsLifecycleTestAccess::IsBound(*F.Panel));
        TestFalse(TEXT("Pad lifetime/ownership releases panel"), FConstructModsLifecycleTestAccess::HasPanel(*F.Menu));
        TestTrue(TEXT("Land menu stays open"), FConstructModsLifecycleTestAccess::IsOpen(*F.Menu));
        TestFalse(TEXT("Open menu retains its modal"), F.Gate->IsGameplayInputAllowed());
        F.Menu->Close();
        TestTrue(TEXT("Closing remaining menu releases modal"), F.Gate->IsGameplayInputAllowed());
    }
    return true;
}
#endif
