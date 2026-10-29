#include "ConstructShellStyle.h"

#include "Brushes/SlateColorBrush.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Styling/CoreStyle.h"

namespace
{
FLinearColor Hex(uint32 RGB)
{
	return FLinearColor::FromSRGBColor(FColor((RGB >> 16) & 255, (RGB >> 8) & 255, RGB & 255));
}
TUniquePtr<FSlateStyleSet> MakeStyle()
{
	auto Style = MakeUnique<FSlateStyleSet>(TEXT("ConstructShell"));
	Style->Set("Construct.Panel", new FSlateRoundedBoxBrush(Hex(0x101111), 8.0f, Hex(0x2B2B2D), 1.0f));
	Style->Set("Construct.Background", new FSlateColorBrush(Hex(0x09090A)));
	Style->Set("Construct.Line", new FSlateColorBrush(Hex(0x2B2B2D)));
	FButtonStyle Primary;
	Primary.SetNormal(FSlateRoundedBoxBrush(FConstructShellStyle::Gold(), 6.0f))
	    .SetHovered(FSlateRoundedBoxBrush(Hex(0xE8BE40), 6.0f))
	    .SetPressed(FSlateRoundedBoxBrush(Hex(0xB8901F), 6.0f))
	    .SetDisabled(FSlateRoundedBoxBrush(Hex(0x3B3421), 6.0f))
	    .SetNormalForeground(Hex(0x09090A))
	    .SetHoveredForeground(Hex(0x09090A))
	    .SetPressedForeground(Hex(0x09090A))
	    .SetDisabledForeground(FConstructShellStyle::Muted());
	Style->Set("Construct.Button.Primary", Primary);
	FButtonStyle Secondary;
	Secondary.SetNormal(FSlateRoundedBoxBrush(Hex(0x19191B), 6.0f, Hex(0x444448), 1.0f))
	    .SetHovered(FSlateRoundedBoxBrush(Hex(0x28282A), 6.0f, Hex(0x66666A), 1.0f))
	    .SetPressed(FSlateRoundedBoxBrush(Hex(0x101011), 6.0f))
	    .SetDisabled(FSlateRoundedBoxBrush(Hex(0x141415), 6.0f))
	    .SetNormalForeground(FConstructShellStyle::Ink())
	    .SetHoveredForeground(FConstructShellStyle::Ink())
	    .SetPressedForeground(FConstructShellStyle::Ink())
	    .SetDisabledForeground(FConstructShellStyle::Muted());
	Style->Set("Construct.Button.Secondary", Secondary);
	FButtonStyle Ghost = FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("NoBorder");
	Ghost.SetNormalForeground(FConstructShellStyle::Ink()).SetHoveredForeground(FConstructShellStyle::Gold());
	Style->Set("Construct.Button.Ghost", Ghost);
	FEditableTextBoxStyle Input = FCoreStyle::Get().GetWidgetStyle<FEditableTextBoxStyle>("NormalEditableTextBox");
	Input.SetBackgroundImageNormal(FSlateRoundedBoxBrush(Hex(0x101011), 6.0f, Hex(0x444448), 1.0f))
	    .SetBackgroundImageHovered(FSlateRoundedBoxBrush(Hex(0x101011), 6.0f, Hex(0x66666A), 1.0f))
	    .SetBackgroundImageFocused(FSlateRoundedBoxBrush(Hex(0x101011), 6.0f, FConstructShellStyle::Gold(), 1.0f))
	    .SetBackgroundImageReadOnly(FSlateRoundedBoxBrush(Hex(0x101011), 6.0f, Hex(0x444448), 1.0f))
	    .SetReadOnlyForegroundColor(FConstructShellStyle::Muted())
	    .SetForegroundColor(FConstructShellStyle::Ink())
	    .SetPadding(FMargin(12, 10));
	Style->Set("Construct.Input", Input);
	const auto Text = [&](const TCHAR* Key, int32 Size, bool bBold, FLinearColor Color)
	{
		Style->Set(Key,
		           FTextBlockStyle()
		               .SetFont(FCoreStyle::GetDefaultFontStyle(bBold ? "Bold" : "Regular", Size))
		               .SetColorAndOpacity(Color));
	};
	Text(TEXT("Construct.Text.Title"), 24, true, FConstructShellStyle::Ink());
	Text(TEXT("Construct.Text.Heading"), 16, true, FConstructShellStyle::Ink());
	Text(TEXT("Construct.Text.Body"), 12, false, FConstructShellStyle::Ink());
	Text(TEXT("Construct.Text.Muted"), 12, false, FConstructShellStyle::Muted());
	Text(TEXT("Construct.Text.Small"), 10, false, FConstructShellStyle::Muted());
	Text(TEXT("Construct.Text.Error"), 12, false, Hex(0xE05260));
	return Style;
}
}

const ISlateStyle& FConstructShellStyle::Get()
{
	static TUniquePtr<FSlateStyleSet> Style = MakeStyle();
	return *Style;
}

FLinearColor FConstructShellStyle::Gold()
{
	return Hex(0xE0AE2A);
}
FLinearColor FConstructShellStyle::Ink()
{
	return Hex(0xFAFAFA);
}
FLinearColor FConstructShellStyle::Muted()
{
	return Hex(0xA1A1A4);
}
