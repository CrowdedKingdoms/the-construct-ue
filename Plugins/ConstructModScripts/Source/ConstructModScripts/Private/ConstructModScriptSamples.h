#pragma once

#include "ConstructModScriptPresentation.h"

namespace ConstructModScriptSamples
{
    inline TSharedRef<FConstructModScriptPresentation> Make()
    {
        TSharedRef<FConstructModScriptPresentation> View = MakeShared<FConstructModScriptPresentation>();
        View->ShowHud({TEXT("sample-greeter"), TEXT("Platform greeter"), 41, 1}, TEXT("Welcome to the Holodeck.\nLook around and make yourself at home."));
        View->ShowHud({TEXT("sample-painter"), TEXT("Colour trail"), 41, 1}, TEXT("Selected: Gold\nClick the floor to leave a trail."));
        View->Started({TEXT("sample-stopped"), TEXT("Loop example"), 42, 1});
        View->Stopped(TEXT("sample-stopped"), 1, FText::FromString(TEXT("Stopped: the script exceeded its execution budget.")));
        return View;
    }

    inline FConstructScriptConsent Consent()
    {
        FConstructScriptConsent Prompt;
        Prompt.RequestId = 1;
        Prompt.Author = FText::FromString(TEXT("Example land owner"));
        Prompt.Scripts = FText::FromString(TEXT("Platform greeter and Colour trail"));
        Prompt.Groups = {FText::FromString(TEXT("Presentation, pointer input and world access"))};
        Prompt.Calls = {
            FText::FromString(TEXT("Show text and local shapes on your screen")),
            FText::FromString(TEXT("Read game clicks, nearby players and blocks on this land")),
            FText::FromString(TEXT("Request block edits using your own permissions"))
        };
        Prompt.NotOffered = {FText::FromString(TEXT("Move your character, spawn actors or send chat messages"))};
        return Prompt;
    }
}
