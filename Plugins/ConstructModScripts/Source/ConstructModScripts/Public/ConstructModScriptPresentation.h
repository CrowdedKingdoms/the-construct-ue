#pragma once

#include "CoreMinimal.h"

/** Display identity retained after a script stops, so delayed feedback can still name its source. */
struct CONSTRUCTMODSCRIPTS_API FConstructScriptLabel
{
    FString Source;
    FString Name;
    int64 AuthorId = 0;
    int64 Generation = 0;
};

struct CONSTRUCTMODSCRIPTS_API FConstructScriptCard
{
    FConstructScriptLabel Label;
    FString Text;
    FText StopReason;
    bool bRunning = true;
    bool bPending = false;
};

/** Already-formatted consent content. Platform interpretation belongs to the source, not the widget. */
struct CONSTRUCTMODSCRIPTS_API FConstructScriptConsent
{
    int32 RequestId = 0;
    FText Author;
    FText Scripts;
    TArray<FText> Groups;
    TArray<FText> Calls;
    TArray<FText> NotOffered;
    bool bPending = false;
};

DECLARE_MULTICAST_DELEGATE_TwoParams(FConstructScriptAnswerRequested, int32, bool);
DECLARE_MULTICAST_DELEGATE_OneParam(FConstructScriptRevokeRequested, const FString&);
DECLARE_MULTICAST_DELEGATE_OneParam(FConstructScriptForgetRequested, int64);

/** Bounded player-facing state. Requests never manufacture consent or a running/stopped result. */
class CONSTRUCTMODSCRIPTS_API FConstructModScriptPresentation
{
public:
    static constexpr int32 MaxRunning = 8;
    static constexpr int32 MaxRows = 32;
    static constexpr int32 MaxPrompts = 8;

    bool Started(const FConstructScriptLabel& Label);
    void Stopped(const FString& Source, int64 Generation, const FText& Reason);
    void ShowHud(const FConstructScriptLabel& Label, const FString& Text);
    void ClearHud(const FString& Source, int64 Generation);
    bool ShowPrompt(FConstructScriptConsent Prompt);
    void WithdrawPrompt(int32 RequestId);
    bool Answer(int32 RequestId, bool bRun);
    bool Revoke(const FString& Source);
    bool ForgetAuthor(int64 AuthorId);
    void Error(const FString& Source, const FText& Message);
    void SetNotice(const FText& Message);
    void SetContext(const FText& Message);
    void Reset();

    const TArray<FConstructScriptCard>& GetCards() const { return Cards; }
    const FConstructScriptConsent* GetPrompt() const { return Prompts.IsEmpty() ? nullptr : &Prompts[0]; }
    const FConstructScriptCard* Find(const FString& Source) const;
    FText GetNotice() const { return Notice; }
    FText GetContext() const { return Context; }
    static FString BoundHudText(const FString& Text);

    FSimpleMulticastDelegate OnChanged;
    FSimpleMulticastDelegate OnHudChanged;
    FConstructScriptAnswerRequested OnAnswerRequested;
    FConstructScriptRevokeRequested OnRevokeRequested;
    FConstructScriptForgetRequested OnForgetRequested;

private:
    FConstructScriptCard* FindMutable(const FString& Source);
    bool Admit(const FConstructScriptLabel& Label);
    TArray<FConstructScriptCard> Cards;
    TArray<FConstructScriptConsent> Prompts;
    FText Notice;
    FText Context;
};
