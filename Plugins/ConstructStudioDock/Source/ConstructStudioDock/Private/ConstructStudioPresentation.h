#pragma once

#include "CoreMinimal.h"
#include "ConstructStudioDockTypes.h"

struct FConstructStudioLayout
{
    bool bFullscreen = false;
    float DockPixels = 0.0f;
    float GameFraction = 1.0f;

    static FConstructStudioLayout Calculate(float WindowPixels, float RequestedFraction = 0.52f)
    {
        FConstructStudioLayout Result;
        if (!FMath::IsFinite(WindowPixels) || WindowPixels <= 0.0f)
        {
            return Result;
        }
        Result.bFullscreen = WindowPixels < 1000.0f;
        if (Result.bFullscreen)
        {
            Result.DockPixels = WindowPixels;
            return Result;
        }
        const float Fraction = FMath::IsFinite(RequestedFraction) ? RequestedFraction : 0.52f;
        Result.DockPixels = FMath::Clamp(WindowPixels * Fraction, 480.0f, WindowPixels - 420.0f);
        Result.GameFraction = (WindowPixels - Result.DockPixels) / WindowPixels;
        return Result;
    }
};

/** Display state only. The core owns approval validity, timeouts and execution. */
struct FConstructStudioPresentation
{
    EConstructStudioDockState State = EConstructStudioDockState::Closed;
    TOptional<FConstructStudioDeployPrompt> Prompt;
    FText Notice;
    FText Warning;
    FText UsageText;
    double PromptShownAt = 0.0;
    bool bPromptVisible = false;
    bool bBrowserFocused = false;

    bool IsOpen() const
    {
        return State != EConstructStudioDockState::Closed;
    }

    bool HasNativePrompt() const
    {
        return State == EConstructStudioDockState::NeedsConsent || Prompt.IsSet();
    }

    bool HoldsModal(bool bFullscreen) const
    {
        return IsOpen() && (bBrowserFocused || HasNativePrompt() || bFullscreen);
    }

    bool ShowsWarning() const
    {
        return !Warning.IsEmpty() && !HasNativePrompt();
    }

    bool CanRun(double Now) const
    {
        return Prompt.IsSet() && bPromptVisible && FMath::IsFinite(Now) && Now - PromptShownAt >= 1.0;
    }

    void Present(const FConstructStudioDeployPrompt& Value, double Now, bool bVisible = true)
    {
        Prompt = Value;
        Warning = FText::GetEmpty();
        bPromptVisible = false;
        ShowPrompt(bVisible, Now);
    }

    /** The click guard runs only while the player can see the prompt and restarts each time it reappears. */
    void ShowPrompt(bool bVisible, double Now)
    {
        if (bVisible && !bPromptVisible)
        {
            PromptShownAt = Now;
        }
        bPromptVisible = bVisible;
    }

    void Withdraw(int64 PromptId)
    {
        if (Prompt.IsSet() && Prompt->PromptId == PromptId)
        {
            Prompt.Reset();
        }
    }

    void Reset()
    {
        *this = FConstructStudioPresentation();
    }
};

