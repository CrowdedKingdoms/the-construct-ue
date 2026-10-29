#pragma once

#include "CoreMinimal.h"
#include "Misc/DateTime.h"

namespace ConstructShellDiagnostics
{
/** Backend messages remain visible in the UI; durable logs contain only fixed issue categories. */
inline FString Issue(const FString& Message)
{
	const FString Lower = Message.Left(1024).ToLower();
	if (Lower.Contains(TEXT("no saved session")))
	{
		return TEXT("No saved session found");
	}
	if (Lower.Contains(TEXT("credential")) || Lower.Contains(TEXT("invalid password")))
	{
		return TEXT("Invalid credentials");
	}
	if (Lower.Contains(TEXT("expired")))
	{
		return TEXT("Session or access expired");
	}
	if (Lower.Contains(TEXT("revoked")))
	{
		return TEXT("Access revoked");
	}
	if (Lower.Contains(TEXT("suspend")) || Lower.Contains(TEXT("blocked")))
	{
		return TEXT("Access suspended or blocked");
	}
	if (Lower.Contains(TEXT("denied")) || Lower.Contains(TEXT("not granted")))
	{
		return TEXT("Access denied");
	}
	if (Lower.Contains(TEXT("maintenance")))
	{
		return TEXT("Maintenance");
	}
	if (Lower.Contains(TEXT("paused")))
	{
		return TEXT("Application paused");
	}
	if (Lower.Contains(TEXT("timeout")) || Lower.Contains(TEXT("timed out")))
	{
		return TEXT("Service request timed out");
	}
	if (Lower.Contains(TEXT("unavailable")))
	{
		return TEXT("Service unavailable");
	}
	if (Lower.Contains(TEXT("cancel")))
	{
		return TEXT("Request canceled");
	}
	return Message.IsEmpty() ? TEXT("No reason supplied") : TEXT("Details shown in runtime UI");
}

inline FString Deadline(const FString& Value)
{
	FDateTime Parsed;
	return Value.Len() <= 40 && FDateTime::ParseIso8601(*Value, Parsed) ? Parsed.ToIso8601() : TEXT("unspecified");
}
}
