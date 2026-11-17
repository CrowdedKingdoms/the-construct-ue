#pragma once

#include "CoreMinimal.h"

/** The dock's fixed https origin, which never resolves on the internet: the browser serves it only from the plugin's files. */
namespace ConstructStudioDockScheme
{
	inline constexpr const TCHAR* Domain = TEXT("studio-dock.invalid");
	inline constexpr const TCHAR* Origin = TEXT("https://studio-dock.invalid");
	inline constexpr const TCHAR* PageUrl = TEXT("https://studio-dock.invalid/index.html");

	/** What the dock does with a navigation or popup in its browser. */
	enum class ENavigation : uint8
	{
		Allow,
		/** Cancelled in the dock and opened in the player's own browser. */
		OpenOutside,
		Refuse
	};

	/** Registers the scheme handler factory with the browser, once; false where the browser cannot run. Game thread. */
	bool Register();

	/** Takes the factory off the browser's list; the browser itself keeps serving it until it shuts down. */
	void Unregister();

	bool IsRegistered();

	/** The plugin's Resources/StudioDock folder, which holds the page and the harness under dsh/, in the editor and packaged. */
	FString ServedRoot();

	/** Whether the page and the harness are in ServedRoot. */
	bool IsStaged();

	/** Replaces the API origins the harness may connect to; anything but a plain https origin is dropped. */
	void SetConnectOrigins(const TArray<FString>& Origins);

	/** Whether Url is a page or file on the dock's origin. */
	bool IsOnOrigin(const FString& Url);

	/** The https origin of Url, lowercased, or empty when Url is not https. */
	FString OriginOf(const FString& Url);

	/** Value as a plain https origin with no trailing slash, lowercased, or empty when it is not one. */
	FString NormalizeOrigin(const FString& Value);

	/** The path of a request on the dock's origin without its query, such as "dsh/index.html"; empty off the origin. */
	FString RequestPath(const FString& Url);

	/** The file a request path names, or empty when the path is refused or leaves Root. */
	FString ResolveFile(const FString& Root, const FString& Path);

	/** Only GET and HEAD are served. */
	bool IsServedVerb(const FString& Verb);

	/** The MIME type served for a request path. */
	const TCHAR* MimeTypeFor(const FString& Path);

	/** The headers sent with every response for a request path, whatever its status. */
	TArray<TPair<FString, FString>> HeadersFor(const FString& Path, const TArray<FString>& ConnectOrigins);

	/** The dock's origin stays; a link whose origin is exactly StudioOrigin opens outside; anything else leaving it is refused. */
	ENavigation Navigation(const FString& Url, bool bMainFrame, const FString& StudioOrigin);

	/** A popup never opens in the dock: one whose origin is exactly StudioOrigin opens outside, any other is refused. */
	ENavigation Popup(const FString& Url, const FString& StudioOrigin);
}
