#pragma once

#include "CoreMinimal.h"
#include "Engine/GameViewportClient.h"

/** Owns only the horizontal change applied to the viewport's existing native player layout. */
struct FConstructStudioViewportInset
{
    TWeakObjectPtr<UGameViewportClient> Viewport;
    TArray<FSplitscreenData> Original;
    TArray<FSplitscreenData> Applied;
    float Fraction = 1.0f;

    void Apply(UGameViewportClient* Client, float GameFraction)
    {
        if (!Client)
        {
            Restore();
            return;
        }
        const bool bSameViewport = Viewport.Get() == Client;
        if (bSameViewport && Fraction == GameFraction && !Applied.IsEmpty())
        {
            return;
        }
        // Remove our previous scale before taking the current layout as the new baseline.
        Restore(!bSameViewport);
        Viewport = Client;
        Original = Client->SplitscreenInfo;
        Applied = Original;
        for (FSplitscreenData& Mode : Applied)
        {
            for (FPerPlayerSplitscreenData& Player : Mode.PlayerData)
            {
                Player.OriginX *= GameFraction;
                Player.SizeX *= GameFraction;
            }
        }
        Client->SplitscreenInfo = Applied;
        Client->LayoutPlayers();
        Fraction = GameFraction;
    }

    void Restore(bool bLayoutPlayers = true)
    {
        if (UGameViewportClient* Client = Viewport.Get())
        {
            // Preserve an unrelated layout change made after ours rather than overwriting it.
            for (int32 ModeIndex = 0; ModeIndex < Applied.Num() && ModeIndex < Client->SplitscreenInfo.Num(); ++ModeIndex)
            {
                FSplitscreenData& Current = Client->SplitscreenInfo[ModeIndex];
                const FSplitscreenData& Last = Applied[ModeIndex];
                const FSplitscreenData& Before = Original[ModeIndex];
                for (int32 PlayerIndex = 0; PlayerIndex < Last.PlayerData.Num() && PlayerIndex < Current.PlayerData.Num(); ++PlayerIndex)
                {
                    FPerPlayerSplitscreenData& Entry = Current.PlayerData[PlayerIndex];
                    const FPerPlayerSplitscreenData& Previous = Last.PlayerData[PlayerIndex];
                    if (Entry.OriginX == Previous.OriginX)
                    {
                        Entry.OriginX = Before.PlayerData[PlayerIndex].OriginX;
                    }
                    if (Entry.SizeX == Previous.SizeX)
                    {
                        Entry.SizeX = Before.PlayerData[PlayerIndex].SizeX;
                    }
                }
            }
            if (bLayoutPlayers)
            {
                Client->LayoutPlayers();
            }
        }
        Viewport.Reset();
        Original.Reset();
        Applied.Reset();
        Fraction = 1.0f;
    }

    ~FConstructStudioViewportInset()
    {
        Restore();
    }
};

