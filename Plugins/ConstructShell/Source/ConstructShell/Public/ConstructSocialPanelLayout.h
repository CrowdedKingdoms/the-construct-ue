#pragma once

#include "CoreMinimal.h"

class SWidget;

/** The Construct's independent communication cards, clear of the connection bar and hub status. */
enum class EConstructSocialPanelRegion : uint8
{
    Chat,
    Voice,
    Webcam
};

namespace ConstructSocialPanelLayout
{
    /** Places a card in its own bounded viewport region. Scrollable card content remains reachable in small windows. */
    CONSTRUCTSHELL_API TSharedRef<SWidget> Place(EConstructSocialPanelRegion Region, TSharedRef<SWidget> Content);
}
