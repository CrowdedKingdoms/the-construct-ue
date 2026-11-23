#include "ConstructStudioDockScheme.h"

#include "ConstructStudioDockLog.h"
#include "CoreGlobals.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "Interfaces/IPluginManager.h"
#include "IWebBrowserSchemeHandler.h"
#include "IWebBrowserSingleton.h"
#include "Misc/App.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "Modules/ModuleManager.h"
#include "WebBrowserModule.h"

namespace
{
	FCriticalSection SchemeOriginsLock;
	TArray<FString> SchemeConnectOrigins;
	bool bSchemeRegistered = false;

	bool IsPlainHttpsOrigin(const FString& Value)
	{
		static const int32 SchemeLength = 8;
		if (!Value.StartsWith(TEXT("https://"), ESearchCase::CaseSensitive) || Value.Len() <= SchemeLength)
		{
			return false;
		}
		for (int32 Index = SchemeLength; Index < Value.Len(); ++Index)
		{
			const TCHAR C = Value[Index];
			const bool bAllowed = (C >= TEXT('a') && C <= TEXT('z')) || (C >= TEXT('0') && C <= TEXT('9')) || C == TEXT('.') || C == TEXT('-') || C == TEXT(':');
			if (!bAllowed)
			{
				return false;
			}
		}
		return true;
	}

	/** Answers one request from a staged file, or with a short plain-text error; runs on the browser's IO thread. */
	class FConstructStudioDockSchemeHandler final : public IWebBrowserSchemeHandler
	{
	public:
		FConstructStudioDockSchemeHandler(const FString& InRoot, TArray<FString> InOrigins)
			: Root(InRoot)
			, Origins(MoveTemp(InOrigins))
		{
		}

		virtual bool ProcessRequest(const FString& Verb, const FString& Url, const FSimpleDelegate& OnHeadersReady) override
		{
			const FString Path = ConstructStudioDockScheme::RequestPath(Url);
			Headers = ConstructStudioDockScheme::HeadersFor(Path, Origins);
			bHead = Verb.Equals(TEXT("HEAD"), ESearchCase::CaseSensitive);
			if (!ConstructStudioDockScheme::IsServedVerb(Verb))
			{
				Answer(405, TEXT("Method not allowed"));
			}
			else if (!OpenFile(ConstructStudioDockScheme::ResolveFile(Root, Path)))
			{
				Answer(404, TEXT("Not found"));
			}
			else
			{
				MimeType = ConstructStudioDockScheme::MimeTypeFor(Path);
			}
			OnHeadersReady.ExecuteIfBound();
			return true;
		}

		virtual void GetResponseHeaders(IHeaders& OutHeaders) override
		{
			OutHeaders.SetStatusCode(Status);
			OutHeaders.SetMimeType(MimeType);
			OutHeaders.SetContentLength(bHead ? 0 : static_cast<int32>(Remaining));
			for (const TPair<FString, FString>& Header : Headers)
			{
				OutHeaders.SetHeader(*Header.Key, *Header.Value);
			}
		}

		virtual bool ReadResponse(uint8* OutBytes, int32 BytesToRead, int32& BytesRead, const FSimpleDelegate& OnMoreDataReady) override
		{
			BytesRead = 0;
			if (bHead || Remaining <= 0 || BytesToRead <= 0)
			{
				return false;
			}
			const int32 Count = static_cast<int32>(FMath::Min<int64>(BytesToRead, Remaining));
			if (File.IsValid())
			{
				if (!File->Read(OutBytes, Count))
				{
					return false;
				}
			}
			else
			{
				FMemory::Memcpy(OutBytes, Body.GetData() + (Body.Num() - Remaining), Count);
			}
			Remaining -= Count;
			BytesRead = Count;
			return true;
		}

		virtual void Cancel() override
		{
			File.Reset();
			Remaining = 0;
		}

	private:
		bool OpenFile(const FString& FullPath)
		{
			if (FullPath.IsEmpty())
			{
				return false;
			}
			File.Reset(IPlatformFile::GetPlatformPhysical().OpenRead(*FullPath));
			const int64 Size = File.IsValid() ? File->Size() : -1;
			if (Size < 0 || Size > MAX_int32)
			{
				File.Reset();
				return false;
			}
			Remaining = Size;
			Status = 200;
			return true;
		}

		void Answer(int32 Code, const TCHAR* Text)
		{
			File.Reset();
			Status = Code;
			MimeType = TEXT("text/plain");
			const FTCHARToUTF8 Utf8(Text);
			Body = TArray<uint8>(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
			Remaining = Body.Num();
		}

		FString Root;
		TArray<FString> Origins;
		TArray<TPair<FString, FString>> Headers;
		TArray<uint8> Body;
		TUniquePtr<IFileHandle> File;
		int64 Remaining = 0;
		const TCHAR* MimeType = TEXT("text/plain");
		int32 Status = 500;
		bool bHead = false;
	};

	class FConstructStudioDockSchemeFactory final : public IWebBrowserSchemeHandlerFactory
	{
	public:
		virtual TUniquePtr<IWebBrowserSchemeHandler> Create(FString Verb, FString Url) override
		{
			TArray<FString> Origins;
			{
				FScopeLock Lock(&SchemeOriginsLock);
				Origins = SchemeConnectOrigins;
			}
			return MakeUnique<FConstructStudioDockSchemeHandler>(Root, MoveTemp(Origins));
		}

		/** Set once before the factory is registered, then only read. */
		FString Root;
	};

	FConstructStudioDockSchemeFactory& SchemeFactory()
	{
		// The browser keeps a raw pointer to a factory even after unregistering it, so this one lives until the process exits.
		static FConstructStudioDockSchemeFactory Instance;
		return Instance;
	}
}

bool ConstructStudioDockScheme::Register()
{
	if (bSchemeRegistered)
	{
		return true;
	}
	if (IsRunningCommandlet() || !FApp::CanEverRender())
	{
		return false;
	}
	IWebBrowserModule& Module = IWebBrowserModule::Get();
	IWebBrowserSingleton* Browser = Module.IsWebModuleAvailable() ? Module.GetSingleton() : nullptr;
	if (!Browser)
	{
		UE_LOG(LogConstructStudioDock, Log, TEXT("%s is not served: this process has no web browser."), Origin);
		return false;
	}
	SchemeFactory().Root = ServedRoot();
	bSchemeRegistered = Browser->RegisterSchemeHandlerFactory(TEXT("https"), Domain, &SchemeFactory());
	UE_LOG(LogConstructStudioDock, Log, TEXT("%s %s from %s"), bSchemeRegistered ? TEXT("Serving") : TEXT("Could not serve"), Origin, *SchemeFactory().Root);
	return bSchemeRegistered;
}

void ConstructStudioDockScheme::Unregister()
{
	if (!bSchemeRegistered)
	{
		return;
	}
	bSchemeRegistered = false;
	IWebBrowserModule* Module = FModuleManager::GetModulePtr<IWebBrowserModule>(TEXT("WebBrowser"));
	IWebBrowserSingleton* Browser = Module ? Module->GetSingleton() : nullptr;
	if (Browser)
	{
		Browser->UnregisterSchemeHandlerFactory(&SchemeFactory());
	}
}

bool ConstructStudioDockScheme::IsRegistered()
{
	return bSchemeRegistered;
}

FString ConstructStudioDockScheme::ServedRoot()
{
	// Build.cs stages this folder as loose files, so a packaged build finds it under the plugin's own staged directory.
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("ConstructStudioDockCore"));
	if (!Plugin.IsValid())
	{
		return FString();
	}
	FString Root = FPaths::ConvertRelativePathToFull(Plugin->GetBaseDir() / TEXT("Resources/StudioDock"));
	FPaths::NormalizeDirectoryName(Root);
	return Root;
}

bool ConstructStudioDockScheme::IsStaged()
{
	const FString Root = ServedRoot();
	return !Root.IsEmpty() && FPaths::FileExists(Root / TEXT("index.html")) && FPaths::FileExists(Root / TEXT("dock.js"))
		&& FPaths::FileExists(Root / TEXT("dsh/index.html")) && FPaths::FileExists(Root / TEXT("dsh/preview/vfs-image.tar.gz"));
}

void ConstructStudioDockScheme::SetConnectOrigins(const TArray<FString>& Origins)
{
	TArray<FString> Accepted;
	for (const FString& Candidate : Origins)
	{
		const FString Lowered = Candidate.ToLower();
		if (IsPlainHttpsOrigin(Lowered))
		{
			Accepted.AddUnique(Lowered);
		}
	}
	FScopeLock Lock(&SchemeOriginsLock);
	SchemeConnectOrigins = MoveTemp(Accepted);
}

bool ConstructStudioDockScheme::IsOnOrigin(const FString& Url)
{
	static const FString Prefix = FString(Origin) + TEXT("/");
	return Url.StartsWith(Prefix, ESearchCase::CaseSensitive);
}

FString ConstructStudioDockScheme::OriginOf(const FString& Url)
{
	static const int32 SchemeLength = 8;
	if (!Url.StartsWith(TEXT("https://"), ESearchCase::IgnoreCase))
	{
		return FString();
	}
	int32 End = Url.Len();
	for (int32 Index = SchemeLength; Index < Url.Len(); ++Index)
	{
		const TCHAR C = Url[Index];
		if (C == TEXT('/') || C == TEXT('?') || C == TEXT('#'))
		{
			End = Index;
			break;
		}
	}
	return End > SchemeLength ? Url.Left(End).ToLower() : FString();
}

FString ConstructStudioDockScheme::NormalizeOrigin(const FString& Value)
{
	FString Lowered = Value.ToLower();
	Lowered.RemoveFromEnd(TEXT("/"));
	return IsPlainHttpsOrigin(Lowered) ? Lowered : FString();
}

FString ConstructStudioDockScheme::RequestPath(const FString& Url)
{
	if (!IsOnOrigin(Url))
	{
		return FString();
	}
	FString Path = Url.RightChop(FCString::Strlen(Origin) + 1);
	for (int32 Index = 0; Index < Path.Len(); ++Index)
	{
		if (Path[Index] == TEXT('?') || Path[Index] == TEXT('#'))
		{
			Path.LeftInline(Index);
			break;
		}
	}
	return Path;
}

FString ConstructStudioDockScheme::ResolveFile(const FString& Root, const FString& Path)
{
	if (Root.IsEmpty())
	{
		return FString();
	}
	FString Relative = Path;
	if (Relative.IsEmpty() || Relative.EndsWith(TEXT("/")))
	{
		Relative += TEXT("index.html");
	}
	TArray<FString> Segments;
	Relative.ParseIntoArray(Segments, TEXT("/"), false);
	for (const FString& Segment : Segments)
	{
		// A leading dot also refuses "." and "..", so no path can climb out of Root.
		if (Segment.IsEmpty() || Segment.StartsWith(TEXT(".")) || Segment.Contains(TEXT("\\")) || Segment.Contains(TEXT(":")) || Segment.Contains(TEXT("%")))
		{
			return FString();
		}
	}
	FString Full = FPaths::Combine(Root, Relative);
	FPaths::NormalizeFilename(Full);
	return FPaths::IsUnderDirectory(Full, Root) ? Full : FString();
}

bool ConstructStudioDockScheme::IsServedVerb(const FString& Verb)
{
	return Verb.Equals(TEXT("GET"), ESearchCase::CaseSensitive) || Verb.Equals(TEXT("HEAD"), ESearchCase::CaseSensitive);
}

const TCHAR* ConstructStudioDockScheme::MimeTypeFor(const FString& Path)
{
	static const TPair<const TCHAR*, const TCHAR*> Types[] =
	{
		{TEXT("html"), TEXT("text/html")},
		{TEXT("js"), TEXT("text/javascript")},
		{TEXT("mjs"), TEXT("text/javascript")},
		{TEXT("css"), TEXT("text/css")},
		{TEXT("json"), TEXT("application/json")},
		{TEXT("map"), TEXT("application/json")},
		{TEXT("webmanifest"), TEXT("application/manifest+json")},
		{TEXT("svg"), TEXT("image/svg+xml")},
		{TEXT("png"), TEXT("image/png")},
		{TEXT("jpg"), TEXT("image/jpeg")},
		{TEXT("jpeg"), TEXT("image/jpeg")},
		{TEXT("webp"), TEXT("image/webp")},
		{TEXT("ico"), TEXT("image/x-icon")},
		{TEXT("woff"), TEXT("font/woff")},
		{TEXT("woff2"), TEXT("font/woff2")},
		{TEXT("ttf"), TEXT("font/ttf")},
		{TEXT("otf"), TEXT("font/otf")},
		{TEXT("wasm"), TEXT("application/wasm")},
		{TEXT("gz"), TEXT("application/gzip")},
		{TEXT("tar"), TEXT("application/x-tar")},
		{TEXT("txt"), TEXT("text/plain")},
		{TEXT("md"), TEXT("text/plain")}
	};
	const FString Extension = FPaths::GetExtension(Path);
	for (const TPair<const TCHAR*, const TCHAR*>& Type : Types)
	{
		if (Extension.Equals(Type.Key, ESearchCase::IgnoreCase))
		{
			return Type.Value;
		}
	}
	return TEXT("application/octet-stream");
}

TArray<TPair<FString, FString>> ConstructStudioDockScheme::HeadersFor(const FString& Path, const TArray<FString>& ConnectOrigins)
{
	const FString Extra = ConnectOrigins.IsEmpty() ? FString() : TEXT(" ") + FString::Join(ConnectOrigins, TEXT(" "));
	const bool bHarness = Path.StartsWith(TEXT("dsh/"), ESearchCase::CaseSensitive);
	// The harness compiles module bodies with new Function, runs its worker from blob URLs and calls the API; the page does none of it.
	// The harness renders the agent's markdown, so images and every other load stay on this origin: only connect-src reaches the API.
	const FString Policy = bHarness
		? FString::Printf(TEXT("default-src 'self'; base-uri 'none'; object-src 'none'; frame-ancestors 'self'; script-src 'self' 'unsafe-eval' 'unsafe-inline' blob:; style-src 'self' 'unsafe-inline'; img-src 'self' data: blob:; font-src 'self' data:; connect-src 'self' blob:%s; worker-src 'self' blob:; frame-src 'self'; media-src 'self' blob:; manifest-src 'self'"), *Extra)
		: FString(TEXT("default-src 'self'; script-src 'self'; style-src 'self' 'unsafe-inline'; connect-src 'self'; frame-src 'self'; frame-ancestors 'none'; base-uri 'none'; object-src 'none'; form-action 'none'"));
	return {
		{TEXT("Cross-Origin-Opener-Policy"), TEXT("same-origin")},
		{TEXT("Cross-Origin-Embedder-Policy"), TEXT("require-corp")},
		{TEXT("Cross-Origin-Resource-Policy"), TEXT("same-origin")},
		{TEXT("Content-Security-Policy"), Policy},
		{TEXT("X-Content-Type-Options"), TEXT("nosniff")},
		{TEXT("Cache-Control"), TEXT("no-store")}
	};
}

ConstructStudioDockScheme::ENavigation ConstructStudioDockScheme::Navigation(const FString& Url, bool bMainFrame, const FString& StudioOrigin)
{
	if (IsOnOrigin(Url))
	{
		return ENavigation::Allow;
	}
	const bool bWeb = Url.StartsWith(TEXT("https:"), ESearchCase::IgnoreCase) || Url.StartsWith(TEXT("http:"), ESearchCase::IgnoreCase);
	// A frame's own documents (about:blank, blob:) stay under the page's CSP; only the main frame may never leave the origin.
	if (!bMainFrame && !bWeb)
	{
		return ENavigation::Allow;
	}
	return Popup(Url, StudioOrigin);
}

ConstructStudioDockScheme::ENavigation ConstructStudioDockScheme::Popup(const FString& Url, const FString& StudioOrigin)
{
	const FString Target = OriginOf(Url);
	if (Target.IsEmpty() || StudioOrigin.IsEmpty() || !Target.Equals(StudioOrigin, ESearchCase::CaseSensitive))
	{
		return ENavigation::Refuse;
	}
	return ENavigation::OpenOutside;
}
