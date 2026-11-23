#include "ConstructModScriptPresentation.h"

const FConstructScriptCard* FConstructModScriptPresentation::Find(const FString& Source) const
{
    return Cards.FindByPredicate([&Source](const FConstructScriptCard& Card) { return Card.Label.Source == Source; });
}

FConstructScriptCard* FConstructModScriptPresentation::FindMutable(const FString& Source)
{
    return Cards.FindByPredicate([&Source](const FConstructScriptCard& Card) { return Card.Label.Source == Source; });
}

bool FConstructModScriptPresentation::Admit(const FConstructScriptLabel& Label)
{
    if (Label.Source.IsEmpty() || Label.Source.Len() > 128 || Label.Generation < 0)
    {
        return false;
    }
    if (const FConstructScriptCard* Existing = Find(Label.Source))
    {
        if (Label.Generation < Existing->Label.Generation || (Label.Generation == Existing->Label.Generation && !Existing->bRunning))
        {
            return false;
        }
        if (Existing->bRunning)
        {
            return true;
        }
    }
    int32 Running = 0;
    for (const FConstructScriptCard& Card : Cards)
    {
        Running += Card.bRunning ? 1 : 0;
    }
    return Running < MaxRunning;
}

bool FConstructModScriptPresentation::Started(const FConstructScriptLabel& Label)
{
    if (!Admit(Label))
    {
        return false;
    }
    FConstructScriptCard* Card = FindMutable(Label.Source);
    if (!Card)
    {
        if (Cards.Num() == MaxRows)
        {
            const int32 Oldest = Cards.IndexOfByPredicate([](const FConstructScriptCard& Entry) { return !Entry.bRunning; });
            if (Oldest == INDEX_NONE)
            {
                return false;
            }
            Cards.RemoveAt(Oldest);
        }
        Card = &Cards.AddDefaulted_GetRef();
    }
    if (Card->Label.Generation != Label.Generation || Card->Label.Source != Label.Source)
    {
        *Card = FConstructScriptCard();
    }
    Card->Label = Label;
    Card->Label.Name = Label.Name.Left(64);
    Card->bRunning = true;
    Card->StopReason = FText::GetEmpty();
    OnChanged.Broadcast();
    return true;
}

void FConstructModScriptPresentation::Stopped(const FString& Source, int64 Generation, const FText& Reason)
{
    if (FConstructScriptCard* Card = FindMutable(Source); Card && Card->Label.Generation == Generation)
    {
        Card->bRunning = false;
        Card->bPending = false;
        Card->Text.Reset();
        Card->StopReason = FText::FromString(Reason.ToString().Left(500));
        OnChanged.Broadcast();
    }
}

FString FConstructModScriptPresentation::BoundHudText(const FString& Text)
{
    FString Result = Text.Replace(TEXT("\r\n"), TEXT("\n")).Replace(TEXT("\r"), TEXT("\n")).Left(500);
    int32 Lines = 1;
    for (int32 Index = 0; Index < Result.Len(); ++Index)
    {
        if (Result[Index] == '\n' && ++Lines > 8)
        {
            Result.LeftInline(Index);
            break;
        }
    }
    return Result;
}

void FConstructModScriptPresentation::ShowHud(const FConstructScriptLabel& Label, const FString& Text)
{
    const FConstructScriptCard* Current = Find(Label.Source);
    const bool bSameRun = Current && Current->bRunning && Current->Label.Generation == Label.Generation && Current->Label.Name == Label.Name.Left(64) && Current->Label.AuthorId == Label.AuthorId;
    if (!bSameRun && !Started(Label))
    {
        return;
    }
    FindMutable(Label.Source)->Text = BoundHudText(Text);
    OnHudChanged.Broadcast();
}

void FConstructModScriptPresentation::ClearHud(const FString& Source, int64 Generation)
{
    if (FConstructScriptCard* Card = FindMutable(Source); Card && Card->Label.Generation == Generation)
    {
        Card->Text.Reset();
        OnHudChanged.Broadcast();
    }
}

bool FConstructModScriptPresentation::ShowPrompt(FConstructScriptConsent Prompt)
{
    const int32 Existing = Prompts.IndexOfByPredicate([&Prompt](const FConstructScriptConsent& Entry) { return Entry.RequestId == Prompt.RequestId; });
    if (Existing != INDEX_NONE)
    {
        return false;
    }
    if (Prompts.Num() >= MaxPrompts)
    {
        return false;
    }
    Prompt.Author = FText::FromString(Prompt.Author.ToString().Left(128));
    Prompt.Scripts = FText::FromString(Prompt.Scripts.ToString().Left(512));
    const auto Bound = [](TArray<FText>& Lines)
    {
        Lines.SetNum(FMath::Min(Lines.Num(), 38));
        for (FText& Line : Lines)
        {
            Line = FText::FromString(Line.ToString().Left(160));
        }
    };
    Bound(Prompt.Groups);
    Bound(Prompt.Calls);
    Bound(Prompt.NotOffered);
    Prompt.bPending = false;
    Prompts.Add(MoveTemp(Prompt));
    OnChanged.Broadcast();
    return true;
}

void FConstructModScriptPresentation::WithdrawPrompt(int32 RequestId)
{
    if (Prompts.RemoveAll([RequestId](const FConstructScriptConsent& Prompt) { return Prompt.RequestId == RequestId; }) > 0)
    {
        OnChanged.Broadcast();
    }
}

bool FConstructModScriptPresentation::Answer(int32 RequestId, bool bRun)
{
    if (Prompts.IsEmpty() || Prompts[0].RequestId != RequestId || Prompts[0].bPending || !OnAnswerRequested.IsBound())
    {
        return false;
    }
    Prompts[0].bPending = true;
    OnChanged.Broadcast();
    OnAnswerRequested.Broadcast(RequestId, bRun);
    return true;
}

bool FConstructModScriptPresentation::Revoke(const FString& Source)
{
    FConstructScriptCard* Card = FindMutable(Source);
    if (!Card || !Card->bRunning || Card->bPending || !OnRevokeRequested.IsBound())
    {
        return false;
    }
    Card->bPending = true;
    OnChanged.Broadcast();
    OnRevokeRequested.Broadcast(Source);
    return true;
}

bool FConstructModScriptPresentation::ForgetAuthor(int64 AuthorId)
{
    if (AuthorId <= 0 || !OnForgetRequested.IsBound())
    {
        return false;
    }
    bool bFound = false;
    for (FConstructScriptCard& Card : Cards)
    {
        if (Card.bRunning && Card.Label.AuthorId == AuthorId)
        {
            if (Card.bPending)
            {
                return false;
            }
            bFound = true;
        }
    }
    if (!bFound)
    {
        return false;
    }
    for (FConstructScriptCard& Card : Cards)
    {
        if (Card.bRunning && Card.Label.AuthorId == AuthorId)
        {
            Card.bPending = true;
        }
    }
    OnChanged.Broadcast();
    OnForgetRequested.Broadcast(AuthorId);
    return true;
}

void FConstructModScriptPresentation::Error(const FString& Source, const FText& Message)
{
    if (FConstructScriptCard* Card = FindMutable(Source))
    {
        Card->bPending = false;
    }
    Notice = FText::FromString(Message.ToString().Left(500));
    OnChanged.Broadcast();
}

void FConstructModScriptPresentation::Reset()
{
    Cards.Reset();
    Prompts.Reset();
    Notice = FText::GetEmpty();
    OnChanged.Broadcast();
}

void FConstructModScriptPresentation::SetNotice(const FText& Message)
{
    Notice = FText::FromString(Message.ToString().Left(500));
    OnHudChanged.Broadcast();
}

void FConstructModScriptPresentation::SetContext(const FText& Message)
{
    Context = FText::FromString(Message.ToString().Left(160));
    OnHudChanged.Broadcast();
}
