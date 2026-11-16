#pragma once

#include "CoreMinimal.h"
#include "Widgets/Layout/SExpandableArea.h"

/** A native expander whose indicator stays alongside the title even when status details wrap below it. */
class CONSTRUCTSHELL_API SConstructExpandableArea : public SExpandableArea
{
public:
    using FArguments = SExpandableArea::FArguments;
    static constexpr float HeaderRowHeight = 28.0f;
    void Construct(const FArguments& InArgs);

protected:
    virtual TSharedRef<SWidget> ConstructHeaderWidget(const FArguments& InArgs, TSharedRef<SWidget> HeaderContent) override;
};
