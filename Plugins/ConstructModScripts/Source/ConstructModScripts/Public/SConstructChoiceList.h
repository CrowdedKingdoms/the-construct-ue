#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

struct CONSTRUCTMODSCRIPTS_API FConstructChoiceOption
{
    FName Key;
    FText Title;
    FText Description;
    bool bEnabled = true;
};

DECLARE_DELEGATE_OneParam(FConstructChoiceConfirmed, FName);

/** Stacked option cards. Selection is local UI state; only explicit confirmation emits a request. */
class CONSTRUCTMODSCRIPTS_API SConstructChoiceList : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SConstructChoiceList) : _InitiallySelected(NAME_None), _CanConfirm(true) {}
        SLATE_ARGUMENT(TArray<FConstructChoiceOption>, Options)
        SLATE_ARGUMENT(FName, InitiallySelected)
        SLATE_ATTRIBUTE(bool, CanConfirm)
        SLATE_EVENT(FConstructChoiceConfirmed, OnConfirmed)
    SLATE_END_ARGS()

    void Construct(const FArguments& Args);
    bool Select(FName Key);
    bool Confirm();
    FName GetSelection() const { return Selection; }

private:
    TArray<FConstructChoiceOption> Options;
    FName Selection;
    TAttribute<bool> CanConfirm;
    FConstructChoiceConfirmed OnConfirmed;
};
