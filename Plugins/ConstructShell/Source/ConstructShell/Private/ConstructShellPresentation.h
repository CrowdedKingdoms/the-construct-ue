#pragma once

#include "CoreMinimal.h"
#include "Network/UDP/CrowdyUDPSubsystem.h"

namespace ConstructShellPresentation
{
inline bool ShouldShowSignIn(bool bLocalPreview, bool bSignedIn, bool bAccessEnded)
{
	return !bLocalPreview && (!bSignedIn || bAccessEnded);
}

inline bool IsSessionReady(
    bool bLocalPreview, bool bSignedIn, bool bAccessEnded, bool bConnected, bool bLegalPending, bool bAppPaused)
{
	return bLocalPreview ||
	       (!ShouldShowSignIn(false, bSignedIn, bAccessEnded) && bConnected && !bLegalPending && !bAppPaused);
}

inline FText ConnectionCaption(EUDPConnectionState Connection,
                               bool bAppPaused,
                               const FString& PauseReason,
                               bool bSDKAvailable)
{
	if (bAppPaused)
	{
		return FText::Format(NSLOCTEXT("ConstructShell", "AppPaused", "App paused: {0}"),
		                     FText::FromString(PauseReason));
	}
	if (!bSDKAvailable)
	{
		return NSLOCTEXT("ConstructShell", "NoConnection", "Crowdy unavailable");
	}
	switch (Connection)
	{
	case EUDPConnectionState::Connecting:
		return NSLOCTEXT("ConstructShell", "Connecting", "Connecting...");
	case EUDPConnectionState::Connected:
		return NSLOCTEXT("ConstructShell", "Connected", "Connected");
	case EUDPConnectionState::Reconnecting:
		return NSLOCTEXT("ConstructShell", "Reconnecting", "Reconnecting...");
	case EUDPConnectionState::GateKeep:
		return NSLOCTEXT("ConstructShell", "Full", "App is full");
	default:
		return NSLOCTEXT("ConstructShell", "Disconnected", "Disconnected");
	}
}
}
