#if WITH_DEV_AUTOMATION_TESTS

#include "ConstructStudioDock.h"
#include "ConstructStudioDockBridge.h"
#include "ConstructStudioDockFrames.h"
#include "ConstructStudioDockScheme.h"
#include "ConstructStudioDockTestAccess.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/UObjectIterator.h"
#include <type_traits>

namespace ConstructStudioDockPageTests
{
	constexpr EAutomationTestFlags PageTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	FString HeaderValue(const TArray<TPair<FString, FString>>& Headers, const TCHAR* Name)
	{
		for (const TPair<FString, FString>& Header : Headers)
		{
			if (Header.Key.Equals(Name, ESearchCase::IgnoreCase))
			{
				return Header.Value;
			}
		}
		return FString();
	}

	const TCHAR* NavigationName(ConstructStudioDockScheme::ENavigation Decision)
	{
		switch (Decision)
		{
		case ConstructStudioDockScheme::ENavigation::Allow: return TEXT("Allow");
		case ConstructStudioDockScheme::ENavigation::OpenOutside: return TEXT("OpenOutside");
		default: return TEXT("Refuse");
		}
	}

	FString WorkerRequest(const FString& Nonce, const FString& Id, const FString& Method)
	{
		return FString::Printf(TEXT("{\"v\":5,\"n\":\"%s\",\"from\":\"worker\",\"t\":\"req\",\"id\":\"%s\",\"method\":\"%s\",\"params\":{}}"), *Nonce, *Id, *Method);
	}

	FString WorkerEvent(const FString& Nonce, const FString& Event)
	{
		return FString::Printf(TEXT("{\"v\":5,\"n\":\"%s\",\"from\":\"worker\",\"t\":\"event\",\"event\":\"%s\",\"payload\":{}}"), *Nonce, *Event);
	}

	FString PageNotice(const FString& Nonce, const FString& Event)
	{
		return FString::Printf(TEXT("{\"v\":5,\"n\":\"%s\",\"from\":\"page\",\"t\":\"dock\",\"event\":\"%s\",\"payload\":{}}"), *Nonce, *Event);
	}

	TSharedPtr<FJsonObject> ParseObject(const FString& Json)
	{
		TSharedPtr<FJsonObject> Object;
		FJsonSerializer::Deserialize(TJsonReaderFactory<TCHAR>::Create(Json), Object);
		return Object;
	}

	FCrowdyStudioAgentBoot AgentBoot()
	{
		FCrowdyStudioAgentBoot Boot;
		Boot.Result = ECrowdyStudioResult::Success;
		Boot.AppId = 9;
		Boot.GraphQLUrl = TEXT("https://api.example.test/graphql");
		Boot.ModelBaseUrl = TEXT("https://api.example.test");
		Boot.ConnectOrigins = {TEXT("https://api.example.test")};
		Boot.StudioOrigin = TEXT("https://studio.example.test");
		FCrowdyStudioModel Model;
		Model.ModelId = TEXT("model-1");
		Model.Name = TEXT("Model One");
		Model.InputModalities = {TEXT("text"), TEXT("image"), TEXT("audio")};
		Boot.Models.Add(Model);
		return Boot;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructStudioDockSchemeHeadersTest, "Construct.StudioDock.Page.SchemeHeadersPerPath", ConstructStudioDockPageTests::PageTestFlags)
bool FConstructStudioDockSchemeHeadersTest::RunTest(const FString& Parameters)
{
	using ConstructStudioDockPageTests::HeaderValue;
	const TArray<FString> Origins = {TEXT("https://api.example.test")};
	for (const TCHAR* Path : {TEXT("index.html"), TEXT("dock.js"), TEXT("dsh/index.html"), TEXT("dsh/preview/vfs-image.tar.gz"), TEXT("missing.txt")})
	{
		const TArray<TPair<FString, FString>> Headers = ConstructStudioDockScheme::HeadersFor(Path, Origins);
		TestEqual(FString::Printf(TEXT("%s: COOP"), Path), HeaderValue(Headers, TEXT("Cross-Origin-Opener-Policy")), FString(TEXT("same-origin")));
		TestEqual(FString::Printf(TEXT("%s: COEP"), Path), HeaderValue(Headers, TEXT("Cross-Origin-Embedder-Policy")), FString(TEXT("require-corp")));
		TestEqual(FString::Printf(TEXT("%s: CORP"), Path), HeaderValue(Headers, TEXT("Cross-Origin-Resource-Policy")), FString(TEXT("same-origin")));
		TestEqual(FString::Printf(TEXT("%s: nosniff"), Path), HeaderValue(Headers, TEXT("X-Content-Type-Options")), FString(TEXT("nosniff")));
		TestTrue(FString::Printf(TEXT("%s: no Content-Encoding"), Path), HeaderValue(Headers, TEXT("Content-Encoding")).IsEmpty());
		TestFalse(FString::Printf(TEXT("%s: a CSP"), Path), HeaderValue(Headers, TEXT("Content-Security-Policy")).IsEmpty());
	}

	const FString Page = HeaderValue(ConstructStudioDockScheme::HeadersFor(TEXT("index.html"), Origins), TEXT("Content-Security-Policy"));
	TestTrue(TEXT("the page connects only to its own origin"), Page.Contains(TEXT("connect-src 'self';")));
	TestFalse(TEXT("the page lists no API origin"), Page.Contains(TEXT("api.example.test")));
	TestTrue(TEXT("the page frames only its own origin"), Page.Contains(TEXT("frame-src 'self'")));
	TestTrue(TEXT("nothing frames the page"), Page.Contains(TEXT("frame-ancestors 'none'")));
	TestFalse(TEXT("the page runs no eval"), Page.Contains(TEXT("unsafe-eval")));

	const FString Harness = HeaderValue(ConstructStudioDockScheme::HeadersFor(TEXT("dsh/index.html"), Origins), TEXT("Content-Security-Policy"));
	TestTrue(TEXT("the harness's script-src"), Harness.Contains(TEXT("script-src 'self' 'unsafe-eval' 'unsafe-inline' blob:")));
	TestTrue(TEXT("the harness's worker-src"), Harness.Contains(TEXT("worker-src 'self' blob:")));
	TestTrue(TEXT("the harness connects to the boot's origins only"), Harness.Contains(TEXT("connect-src 'self' blob: https://api.example.test;")));
	TestTrue(TEXT("only the page frames the harness"), Harness.Contains(TEXT("frame-ancestors 'self'")));
	TestTrue(TEXT("every other load stays on the dock's origin"), Harness.StartsWith(TEXT("default-src 'self';")));
	TestTrue(TEXT("the agent's markdown images cannot reach another host"), Harness.Contains(TEXT("img-src 'self' data: blob:;")));
	TestTrue(TEXT("no plugin or base rewrite"), Harness.Contains(TEXT("object-src 'none'")) && Harness.Contains(TEXT("base-uri 'none'")));
	const FString Bare = HeaderValue(ConstructStudioDockScheme::HeadersFor(TEXT("dsh/index.html"), TArray<FString>()), TEXT("Content-Security-Policy"));
	TestTrue(TEXT("with no boot the harness connects nowhere else"), Bare.Contains(TEXT("connect-src 'self' blob:;")));

	TestEqual(TEXT("the image is served as gzip bytes"), FString(ConstructStudioDockScheme::MimeTypeFor(TEXT("dsh/preview/vfs-image.tar.gz"))), FString(TEXT("application/gzip")));
	TestEqual(TEXT("the page is html"), FString(ConstructStudioDockScheme::MimeTypeFor(TEXT("index.html"))), FString(TEXT("text/html")));
	TestEqual(TEXT("a Studio origin is normalized"), ConstructStudioDockScheme::NormalizeOrigin(TEXT("https://Studio.Example.Test/")), FString(TEXT("https://studio.example.test")));
	TestTrue(TEXT("an http origin is no origin"), ConstructStudioDockScheme::NormalizeOrigin(TEXT("http://studio.example.test")).IsEmpty());
	TestTrue(TEXT("a URL with a path is no origin"), ConstructStudioDockScheme::NormalizeOrigin(TEXT("https://studio.example.test/x")).IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructStudioDockSchemeTraversalTest, "Construct.StudioDock.Page.SchemeRefusesTraversalAndOtherVerbs", ConstructStudioDockPageTests::PageTestFlags)
bool FConstructStudioDockSchemeTraversalTest::RunTest(const FString& Parameters)
{
	FString Root = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("StudioDockTraversal"));
	FPaths::NormalizeDirectoryName(Root);
	for (const TCHAR* Path : {TEXT("../secret.txt"), TEXT("dsh/../../secret.txt"), TEXT("dsh/.."), TEXT("dsh/%2e%2e/secret.txt"),
		TEXT("dsh\\..\\secret.txt"), TEXT("C:/Windows/win.ini"), TEXT("/secret.txt"), TEXT("dsh//index.html"), TEXT(".hidden"), TEXT("dsh/.git/config")})
	{
		TestTrue(FString::Printf(TEXT("%s is refused"), Path), ConstructStudioDockScheme::ResolveFile(Root, Path).IsEmpty());
	}
	TestTrue(TEXT("no root serves nothing"), ConstructStudioDockScheme::ResolveFile(FString(), TEXT("index.html")).IsEmpty());
	TestEqual(TEXT("the origin's root is the page"), ConstructStudioDockScheme::ResolveFile(Root, FString()), Root / TEXT("index.html"));
	TestEqual(TEXT("a folder serves its index"), ConstructStudioDockScheme::ResolveFile(Root, TEXT("dsh/")), Root / TEXT("dsh/index.html"));
	TestEqual(TEXT("a harness file resolves under the root"), ConstructStudioDockScheme::ResolveFile(Root, TEXT("dsh/preview/vfs-image.tar.gz")), Root / TEXT("dsh/preview/vfs-image.tar.gz"));

	TestEqual(TEXT("a request path drops query and fragment"), ConstructStudioDockScheme::RequestPath(TEXT("https://studio-dock.invalid/dsh/index.html?x=1#y")), FString(TEXT("dsh/index.html")));
	TestTrue(TEXT("another origin has no request path"), ConstructStudioDockScheme::RequestPath(TEXT("https://studio-dock.invalid.example/index.html")).IsEmpty());

	TestTrue(TEXT("GET is served"), ConstructStudioDockScheme::IsServedVerb(TEXT("GET")));
	TestTrue(TEXT("HEAD is served"), ConstructStudioDockScheme::IsServedVerb(TEXT("HEAD")));
	for (const TCHAR* Verb : {TEXT("POST"), TEXT("PUT"), TEXT("DELETE"), TEXT("OPTIONS"), TEXT("get"), TEXT("")})
	{
		TestFalse(FString::Printf(TEXT("'%s' is refused"), Verb), ConstructStudioDockScheme::IsServedVerb(Verb));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructStudioDockBridgeClassTest, "Construct.StudioDock.Page.BridgeHasExactlyBootAndSend", ConstructStudioDockPageTests::PageTestFlags)
bool FConstructStudioDockBridgeClassTest::RunTest(const FString& Parameters)
{
	static_assert(std::is_final_v<UConstructStudioDockBridge>, "The bridge class is final.");
	const UClass* Class = UConstructStudioDockBridge::StaticClass();
	TArray<FString> Names;
	for (TFieldIterator<UFunction> It(Class); It; ++It)
	{
		Names.Add(It->GetName().ToLower());
		if (It->GetOwnerClass() != Class)
		{
			continue;
		}
		TestFalse(FString::Printf(TEXT("%s is not a Blueprint function"), *It->GetName()), It->HasAnyFunctionFlags(FUNC_BlueprintCallable | FUNC_BlueprintPure | FUNC_BlueprintEvent));
	}
	Names.Sort();
	TestEqual(TEXT("its functions, inherited ones included"), FString::Join(Names, TEXT(",")), FString(TEXT("boot,executeubergraph,send")));
	TestFalse(TEXT("it is not the widget"), Class->IsChildOf(UConstructStudioDock::StaticClass()));
#if WITH_METADATA
	TestTrue(TEXT("it says whether Blueprints may subclass it"), Class->HasMetaData(TEXT("IsBlueprintBase")));
	TestFalse(TEXT("and they may not"), Class->GetBoolMetaData(TEXT("IsBlueprintBase")));
#endif
	int32 Subclasses = 0;
	for (TObjectIterator<UClass> It; It; ++It)
	{
		Subclasses += (*It != Class && It->IsChildOf(Class)) ? 1 : 0;
	}
	TestEqual(TEXT("nothing subclasses it"), Subclasses, 0);

	const TStrongObjectPtr<UConstructStudioDockBridge> Unbound(NewObject<UConstructStudioDockBridge>(GetTransientPackage()));
	TestTrue(TEXT("an unbound boot() answers an error"), Unbound->Boot().Contains(TEXT("\"error\"")));
	TestTrue(TEXT("an unbound send() answers an error"), Unbound->Send(TEXT("{}")).Contains(TEXT("\"error\"")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructStudioDockFrameChecksTest, "Construct.StudioDock.Page.FrameChecksDropStrayFrames", ConstructStudioDockPageTests::PageTestFlags)
bool FConstructStudioDockFrameChecksTest::RunTest(const FString& Parameters)
{
	using namespace ConstructStudioDockPageTests;
	FConstructStudioDockChannel Channel([](const FString&) {});
	TestFalse(TEXT("before a boot every frame is dropped"), Channel.Parse(WorkerRequest(TEXT("nonce-a"), TEXT("1"), TEXT("studio.projectList"))).IsSet());
	Channel.Begin(3, TEXT("nonce-a"));

	const TOptional<FConstructStudioDockFrame> Request = Channel.Parse(WorkerRequest(TEXT("nonce-a"), TEXT("1"), TEXT("studio.projectList")));
	TestTrue(TEXT("a worker request with the boot's nonce is read"), Request.IsSet());
	TestTrue(TEXT("as a request with its method and id"), Request.IsSet() && Request->Kind == FConstructStudioDockFrame::EKind::Request
		&& Request->Name == TEXT("studio.projectList") && Request->Id == TEXT("1") && Request->Body.IsValid());
	const TOptional<FConstructStudioDockFrame> Event = Channel.Parse(WorkerEvent(TEXT("nonce-a"), TEXT("worker.ready")));
	TestTrue(TEXT("a worker event is read"), Event.IsSet() && Event->Kind == FConstructStudioDockFrame::EKind::WorkerEvent);
	const TOptional<FConstructStudioDockFrame> Notice = Channel.Parse(PageNotice(TEXT("nonce-a"), TEXT("joined")));
	TestTrue(TEXT("the page's own notice is read"), Notice.IsSet() && Notice->Kind == FConstructStudioDockFrame::EKind::PageNotice);

	const TCHAR* Dropped[][2] =
	{
		{TEXT("wrong nonce"), TEXT("{\"v\":5,\"n\":\"nonce-b\",\"from\":\"worker\",\"t\":\"req\",\"id\":\"1\",\"method\":\"m\",\"params\":{}}")},
		{TEXT("no nonce"), TEXT("{\"v\":5,\"from\":\"worker\",\"t\":\"req\",\"id\":\"1\",\"method\":\"m\",\"params\":{}}")},
		{TEXT("a nonce that is not a string"), TEXT("{\"v\":5,\"n\":1,\"from\":\"worker\",\"t\":\"req\",\"id\":\"1\",\"method\":\"m\",\"params\":{}}")},
		{TEXT("an older version"), TEXT("{\"v\":4,\"n\":\"nonce-a\",\"from\":\"worker\",\"t\":\"req\",\"id\":\"1\",\"method\":\"m\",\"params\":{}}")},
		{TEXT("a version as text"), TEXT("{\"v\":\"5\",\"n\":\"nonce-a\",\"from\":\"worker\",\"t\":\"req\",\"id\":\"1\",\"method\":\"m\",\"params\":{}}")},
		{TEXT("a request claiming the page"), TEXT("{\"v\":5,\"n\":\"nonce-a\",\"from\":\"page\",\"t\":\"req\",\"id\":\"1\",\"method\":\"m\",\"params\":{}}")},
		{TEXT("an unknown sender"), TEXT("{\"v\":5,\"n\":\"nonce-a\",\"from\":\"game\",\"t\":\"event\",\"event\":\"worker.ready\",\"payload\":{}}")},
		{TEXT("a response from the worker"), TEXT("{\"v\":5,\"n\":\"nonce-a\",\"from\":\"worker\",\"t\":\"res\",\"id\":\"1\",\"result\":{}}")},
		{TEXT("a notice claiming the worker"), TEXT("{\"v\":5,\"n\":\"nonce-a\",\"from\":\"worker\",\"t\":\"dock\",\"event\":\"booted\",\"payload\":{}}")},
		{TEXT("a request with no id"), TEXT("{\"v\":5,\"n\":\"nonce-a\",\"from\":\"worker\",\"t\":\"req\",\"method\":\"m\",\"params\":{}}")},
		{TEXT("a request with no method"), TEXT("{\"v\":5,\"n\":\"nonce-a\",\"from\":\"worker\",\"t\":\"req\",\"id\":\"1\",\"params\":{}}")},
		{TEXT("not JSON"), TEXT("v5 nonce-a worker req")},
		{TEXT("a JSON array"), TEXT("[5,\"nonce-a\"]")}
	};
	for (const TCHAR* const* Case : Dropped)
	{
		TestFalse(FString::Printf(TEXT("%s is dropped"), Case[0]), Channel.Parse(Case[1]).IsSet());
	}

	const FString Deep = FString::Printf(TEXT("{\"v\":5,\"n\":\"nonce-a\",\"from\":\"worker\",\"t\":\"req\",\"id\":\"1\",\"method\":\"m\",\"params\":%s1%s}"),
		*FString::ChrN(40, TEXT('[')), *FString::ChrN(40, TEXT(']')));
	TestFalse(TEXT("a frame nested too deep is dropped"), Channel.Parse(Deep).IsSet());
	const FString Quoted = FString::Printf(TEXT("{\"v\":5,\"n\":\"nonce-a\",\"from\":\"worker\",\"t\":\"req\",\"id\":\"1\",\"method\":\"m\",\"params\":{\"text\":\"%s\\\"\"}}"),
		*FString::ChrN(40, TEXT('[')));
	TestTrue(TEXT("brackets inside a string are not nesting"), Channel.Parse(Quoted).IsSet());

	Channel.End();
	TestFalse(TEXT("after the boot ends its frames are dropped"), Channel.Parse(WorkerRequest(TEXT("nonce-a"), TEXT("1"), TEXT("m"))).IsSet());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructStudioDockGreetReadyTest, "Construct.StudioDock.Page.ReadyAndHelloEndAfterOneExchange", ConstructStudioDockPageTests::PageTestFlags)
bool FConstructStudioDockGreetReadyTest::RunTest(const FString& Parameters)
{
	FConstructStudioDockChannel Channel([](const FString&) {});
	Channel.Begin(1, TEXT("nonce-1"));
	TestFalse(TEXT("a ready before the page joins is not greeted"), Channel.GreetReady(1));
	Channel.SetPageJoined(1);
	TestFalse(TEXT("a ready from an older boot is not greeted"), Channel.GreetReady(2));
	TestTrue(TEXT("the worker's first ready is greeted"), Channel.GreetReady(1));
	TestFalse(TEXT("the ready that answers that hello is not"), Channel.GreetReady(1));
	TestTrue(TEXT("a restarted worker's ready is greeted again"), Channel.GreetReady(1));
	Channel.Begin(2, TEXT("nonce-2"));
	Channel.SetPageJoined(2);
	TestTrue(TEXT("a new boot greets its worker's first ready"), Channel.GreetReady(2));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructStudioDockFrameGenerationTest, "Construct.StudioDock.Page.LateAnswerFromAnOlderBootIsDropped", ConstructStudioDockPageTests::PageTestFlags)
bool FConstructStudioDockFrameGenerationTest::RunTest(const FString& Parameters)
{
	TArray<FString> Delivered;
	FConstructStudioDockChannel Channel([&Delivered](const FString& Json) { Delivered.Add(Json); });
	const TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();

	Channel.Begin(1, TEXT("nonce-1"));
	TestFalse(TEXT("nothing goes out before the page joins"), Channel.Reply(1, TEXT("a"), Result));
	Channel.SetPageJoined(1);
	TestTrue(TEXT("the current boot's answer goes out"), Channel.Reply(1, TEXT("a"), Result));

	Channel.Begin(2, TEXT("nonce-2"));
	Channel.SetPageJoined(1);
	TestFalse(TEXT("an older boot cannot mark the page joined"), Channel.HasPageJoined());
	Channel.SetPageJoined(2);
	TestFalse(TEXT("a late answer from the older boot is dropped"), Channel.Reply(1, TEXT("a"), Result));
	TestFalse(TEXT("so is a late refusal"), Channel.Refuse(1, TEXT("a"), EConstructStudioDockRefusal::Failed));
	TestTrue(TEXT("the new boot's answer goes out"), Channel.Reply(2, TEXT("b"), Result));
	TestEqual(TEXT("two frames went out"), Delivered.Num(), 2);
	TestTrue(TEXT("the new boot's answer carries the new nonce"), Delivered.Num() == 2 && Delivered[1].Contains(TEXT("\"n\":\"nonce-2\"")) && Delivered[1].Contains(TEXT("\"id\":\"b\"")));

	const TSharedPtr<FJsonObject> Frame = ConstructStudioDockPageTests::ParseObject(Delivered.Last());
	TestTrue(TEXT("a reply is a res frame from the page at version 5"), Frame.IsValid() && Frame->GetNumberField(TEXT("v")) == 5.0
		&& Frame->GetStringField(TEXT("from")) == TEXT("page") && Frame->GetStringField(TEXT("t")) == TEXT("res"));

	Channel.End();
	TestFalse(TEXT("nothing goes out after the boot ends"), Channel.Reply(2, TEXT("c"), Result));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructStudioDockTokenFramesTest, "Construct.StudioDock.Page.TokenOnlyInHelloAndToken", ConstructStudioDockPageTests::PageTestFlags)
bool FConstructStudioDockTokenFramesTest::RunTest(const FString& Parameters)
{
	TArray<FString> Delivered;
	FConstructStudioDockChannel Channel([&Delivered](const FString& Json) { Delivered.Add(Json); });
	Channel.Begin(4, TEXT("nonce-4"));
	Channel.SetPageJoined(4);
	const TSharedRef<FJsonObject> Forged = MakeShared<FJsonObject>();
	Forged->SetStringField(TEXT("appToken"), TEXT("forged"));
	TestFalse(TEXT("Tell refuses page.hello"), Channel.Tell(4, TEXT("page.hello"), Forged));
	TestFalse(TEXT("Tell refuses page.token"), Channel.Tell(4, TEXT("page.token"), Forged));
	TestFalse(TEXT("Tell refuses page.token in any case"), Channel.Tell(4, TEXT("Page.Token"), Forged));
	TestFalse(TEXT("an empty token sends nothing"), Channel.Token(4, FString()));
	TestEqual(TEXT("none of those went out"), Delivered.Num(), 0);

	TestTrue(TEXT("Hello goes out"), Channel.Hello(4, TEXT("9"), FString(), TEXT("secret-token")));
	TestTrue(TEXT("Token goes out"), Channel.Token(4, TEXT("secret-token-2")));
	TestTrue(TEXT("page.bye goes out"), Channel.Tell(4, TEXT("page.bye"), MakeShared<FJsonObject>()));
	TestEqual(TEXT("three frames went out"), Delivered.Num(), 3);
	TestTrue(TEXT("hello carries the token"), Delivered.Num() == 3 && Delivered[0].Contains(TEXT("\"event\":\"page.hello\"")) && Delivered[0].Contains(TEXT("secret-token")));
	TestTrue(TEXT("hello has a null project when none is open"), Delivered.Num() == 3 && Delivered[0].Contains(TEXT("\"projectId\":null")));
	TestTrue(TEXT("token carries the new token"), Delivered.Num() == 3 && Delivered[1].Contains(TEXT("\"event\":\"page.token\"")) && Delivered[1].Contains(TEXT("secret-token-2")));
	TestFalse(TEXT("bye carries none"), Delivered.Num() == 3 && Delivered[2].Contains(TEXT("secret-token")));

	Delivered.Reset();
	TestTrue(TEXT("Hello without a token still goes out"), Channel.Hello(4, TEXT("9"), TEXT("p-1"), FString()));
	TestFalse(TEXT("with no appToken field"), Delivered.Num() == 1 && Delivered[0].Contains(TEXT("appToken")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructStudioDockFailReleasesPageTest, "Construct.StudioDock.Page.FailedDockKeepsNoPage", ConstructStudioDockPageTests::PageTestFlags)
bool FConstructStudioDockFailReleasesPageTest::RunTest(const FString& Parameters)
{
	using namespace ConstructStudioDockPageTests;
	using FAccess = FConstructStudioDockTestAccess;
	FConstructStudioDockTestRig Rig;
	Rig.Open();
	FAccess::BootLive(*Rig.Dock, AgentBoot());
	const TStrongObjectPtr<UConstructStudioDockBridge> Bridge(FAccess::Bridge(*Rig.Dock));
	if (!TestTrue(TEXT("the boot material makes a bridge"), Bridge.IsValid()))
	{
		return false;
	}
	// On the dock's origin a bridge still bound would answer the boot material, not an error.
	FAccess::SetMainFrame(*Rig.Dock, ConstructStudioDockScheme::PageUrl);
	FAccess::Advance(*Rig.Dock, 61.f);
	TestTrue(TEXT("a harness that never boots fails the dock"), Rig.Dock->GetState() == EConstructStudioDockState::Failed);
	TestNull(TEXT("and the failed dock keeps no page"), FAccess::Bridge(*Rig.Dock));
	TestEqual(TEXT("so the old page's bridge answers closed"), Bridge->Boot(), ConstructStudioDockFrames::ErrorReply(TEXT("closed")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructStudioDockBridgeOriginTest, "Construct.StudioDock.Page.BridgeAnswersOnlyOnTheDockOrigin", ConstructStudioDockPageTests::PageTestFlags)
bool FConstructStudioDockBridgeOriginTest::RunTest(const FString& Parameters)
{
	using namespace ConstructStudioDockPageTests;
	using FAccess = FConstructStudioDockTestAccess;
	const FString Token = TEXT("tok-secret-1");
	TArray<FString> Scripts;
	FConstructStudioDockTestRig Rig;
	Rig.Open();
	FAccess::SetToken(*Rig.Dock, Token);
	FAccess::CaptureScripts(*Rig.Dock, [&Scripts](const FString& Script) { Scripts.Add(Script); });
	FAccess::BootLive(*Rig.Dock, AgentBoot());
	const TStrongObjectPtr<UConstructStudioDockBridge> Bridge(FAccess::Bridge(*Rig.Dock));
	if (!TestTrue(TEXT("the boot material makes a bridge"), Bridge.IsValid()))
	{
		return false;
	}

	FAccess::SetMainFrame(*Rig.Dock, TEXT("https://evil.example.test/index.html"));
	TestTrue(TEXT("boot() off the dock's origin answers an error"), Bridge->Boot().Contains(TEXT("\"error\"")));
	FAccess::SetMainFrame(*Rig.Dock, ConstructStudioDockScheme::PageUrl);
	const FString Reply = Bridge->Boot();
	const TSharedPtr<FJsonObject> Boot = ParseObject(Reply);
	if (!TestTrue(TEXT("boot() on the dock's origin answers the material"), Boot.IsValid() && !Boot->HasField(TEXT("error"))))
	{
		return false;
	}
	const FString Nonce = Boot->GetStringField(TEXT("nonce"));
	TestFalse(TEXT("the boot material holds no token"), Reply.Contains(Token));
	TestEqual(TEXT("the persist scope is the app and a hash of the user"), Boot->GetStringField(TEXT("persistScope")), ConstructStudioDockFrames::PersistScope(9, 42));
	TestTrue(TEXT("the persist scope does not show the user id"), Boot->GetStringField(TEXT("persistScope")).StartsWith(TEXT("9/")) && !Boot->GetStringField(TEXT("persistScope")).EndsWith(TEXT("/42")));
	const TSharedPtr<FJsonObject>* Files = nullptr;
	TestTrue(TEXT("it carries both seed files"), Boot->TryGetObjectField(TEXT("files"), Files) && (*Files)->HasField(TEXT("crowdy.json")) && (*Files)->HasField(TEXT("settings.yaml")));
	const TSharedPtr<FJsonObject> Crowdy = Files ? ParseObject((*Files)->GetStringField(TEXT("crowdy.json"))) : TSharedPtr<FJsonObject>();
	TestTrue(TEXT("crowdy.json carries the nonce, channel and mod mode"), Crowdy.IsValid() && Crowdy->GetStringField(TEXT("bridgeNonce")) == Nonce
		&& Crowdy->GetStringField(TEXT("bridgeChannel")) == Boot->GetStringField(TEXT("channel")) && Crowdy->GetStringField(TEXT("studioMode")) == TEXT("mod")
		&& Crowdy->GetStringField(TEXT("appId")) == TEXT("9"));
	const FString Yaml = Files ? (*Files)->GetStringField(TEXT("settings.yaml")) : FString();
	TestTrue(TEXT("settings.yaml points at the model endpoint"), Yaml.Contains(TEXT("baseURL: \"https://api.example.test/v1/model\"")));
	TestTrue(TEXT("settings.yaml keeps only text and image inputs"), Yaml.Contains(TEXT("\"inputModalities\":[\"text\",\"image\"]")) && !Yaml.Contains(TEXT("audio")));

	Bridge->Send(WorkerRequest(Nonce, TEXT("0"), TEXT("studio.projectList")));
	TestEqual(TEXT("nothing reaches the page before it joins"), Scripts.Num(), 0);
	Bridge->Send(PageNotice(Nonce, TEXT("joined")));
	TestEqual(TEXT("joining sends page.hello"), Scripts.Num(), 1);
	Bridge->Send(WorkerRequest(Nonce, TEXT("1"), TEXT("studio.projectList")));
	Bridge->Send(WorkerRequest(Nonce, TEXT("2"), TEXT("grid.programRun")));
	Bridge->Send(WorkerRequest(TEXT("not-the-nonce"), TEXT("3"), TEXT("studio.projectList")));
	TestEqual(TEXT("two requests answered, the stray one dropped"), Scripts.Num(), 3);
	TestTrue(TEXT("a request the dock answers in Unreal gets its answer"), Scripts.Num() == 3 && Scripts[1].Contains(TEXT("\"t\":\"res\"")) && Scripts[1].Contains(TEXT("\"id\":\"1\""))
		&& Scripts[1].Contains(TEXT("\"projects\":[]")));
	TestTrue(TEXT("a grid program request is refused as not offered"), Scripts.Num() == 3 && Scripts[2].Contains(TEXT("\"code\":\"BRIDGE_REFUSED\"")) && Scripts[2].Contains(TEXT("\"id\":\"2\"")));
	Bridge->Send(WorkerEvent(Nonce, TEXT("worker.ready")));
	TestEqual(TEXT("worker.ready sends page.hello again, then the grid's context"), Scripts.Num(), 5);
	Bridge->Send(WorkerEvent(Nonce, TEXT("worker.ready")));
	TestEqual(TEXT("the worker.ready that answers that hello sends nothing"), Scripts.Num(), 5);
	FAccess::TokenChanged(*Rig.Dock, 42, TEXT("tok-secret-2"));
	FAccess::TokenChanged(*Rig.Dock, 7, TEXT("tok-secret-other"));
	TestEqual(TEXT("a new token for the booted user goes out, another user's does not"), Scripts.Num(), 6);
	TestFalse(TEXT("a late answer from an older boot is dropped"), FAccess::Reply(*Rig.Dock, FAccess::Generation(*Rig.Dock) - 1, TEXT("1"), MakeShared<FJsonObject>()));

	FAccess::SetMainFrame(*Rig.Dock, TEXT("https://evil.example.test/index.html"));
	TestTrue(TEXT("send() off the dock's origin answers an error"), Bridge->Send(WorkerEvent(Nonce, TEXT("worker.ready"))).Contains(TEXT("\"error\"")));
	FAccess::Reply(*Rig.Dock, FAccess::Generation(*Rig.Dock), TEXT("4"), MakeShared<FJsonObject>());
	TestEqual(TEXT("no script runs in a page off the dock's origin"), Scripts.Num(), 6);
	FAccess::SetMainFrame(*Rig.Dock, ConstructStudioDockScheme::PageUrl);

	Rig.Dock->Close();
	TestEqual(TEXT("Close says page.bye"), Scripts.Num(), 7);
	TestTrue(TEXT("as the last frame"), Scripts.Num() == 7 && Scripts[6].Contains(TEXT("\"event\":\"page.bye\"")));
	TestTrue(TEXT("boot() after Close answers an error"), Bridge->Boot().Contains(TEXT("\"error\"")));
	TestTrue(TEXT("send() after Close answers an error"), Bridge->Send(WorkerEvent(Nonce, TEXT("worker.ready"))).Contains(TEXT("\"error\"")));
	TestEqual(TEXT("and reaches nothing"), Scripts.Num(), 7);

	for (const FString& Script : Scripts)
	{
		const bool bCarriesToken = Script.Contains(TEXT("tok-secret"));
		const bool bTokenFrame = Script.Contains(TEXT("\"event\":\"page.hello\"")) || Script.Contains(TEXT("\"event\":\"page.token\""));
		TestTrue(TEXT("only page.hello and page.token carry the token"), !bCarriesToken || bTokenFrame);
	}
	TestTrue(TEXT("page.hello carried it"), Scripts.Num() > 0 && Scripts[0].Contains(Token));
	TestTrue(TEXT("page.token carried the new one"), Scripts.Num() > 5 && Scripts[5].Contains(TEXT("tok-secret-2")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructStudioDockNavigationTest, "Construct.StudioDock.Page.NavigationPolicy", ConstructStudioDockPageTests::PageTestFlags)
bool FConstructStudioDockNavigationTest::RunTest(const FString& Parameters)
{
	using ConstructStudioDockPageTests::NavigationName;
	const FString Studio = TEXT("https://studio.example.test");
	const FString Allow = TEXT("Allow");
	const FString Outside = TEXT("OpenOutside");
	const FString Refuse = TEXT("Refuse");
	struct FCase
	{
		const TCHAR* Url;
		bool bMainFrame;
		const FString& Expected;
	};
	const FCase Cases[] =
	{
		{TEXT("https://studio-dock.invalid/index.html"), true, Allow},
		{TEXT("https://studio-dock.invalid/dsh/index.html"), false, Allow},
		{TEXT("https://studio.example.test/projects/1"), true, Outside},
		{TEXT("https://STUDIO.example.test/x"), true, Outside},
		{TEXT("https://studio.example.test/x"), false, Outside},
		{TEXT("https://studio.example.test.evil.test/"), true, Refuse},
		{TEXT("https://studio.example.test:8443/"), true, Refuse},
		{TEXT("https://user@studio.example.test/"), true, Refuse},
		{TEXT("http://studio.example.test/"), true, Refuse},
		{TEXT("https://evil.test/"), true, Refuse},
		{TEXT("https://evil.test/"), false, Refuse},
		{TEXT("https://studio-dock.invalid.evil.test/"), true, Refuse},
		{TEXT("javascript:alert(1)"), true, Refuse},
		{TEXT("about:blank"), true, Refuse},
		{TEXT("about:blank"), false, Allow}
	};
	for (const FCase& Case : Cases)
	{
		TestEqual(FString::Printf(TEXT("%s %s"), Case.bMainFrame ? TEXT("main frame") : TEXT("subframe"), Case.Url),
			FString(NavigationName(ConstructStudioDockScheme::Navigation(Case.Url, Case.bMainFrame, Studio))), Case.Expected);
	}
	TestEqual(TEXT("with no Studio origin a link is refused"), FString(NavigationName(ConstructStudioDockScheme::Navigation(TEXT("https://studio.example.test/"), true, FString()))), Refuse);
	TestEqual(TEXT("a popup to the Studio origin opens outside"), FString(NavigationName(ConstructStudioDockScheme::Popup(TEXT("https://studio.example.test/wallet"), Studio))), Outside);
	TestEqual(TEXT("a popup on the dock's origin is refused"), FString(NavigationName(ConstructStudioDockScheme::Popup(TEXT("https://studio-dock.invalid/index.html"), Studio))), Refuse);
	TestEqual(TEXT("a popup elsewhere is refused"), FString(NavigationName(ConstructStudioDockScheme::Popup(TEXT("https://evil.test/"), Studio))), Refuse);
	TestEqual(TEXT("a DevTools popup is refused"), FString(NavigationName(ConstructStudioDockScheme::Popup(FString(), Studio))), Refuse);
	return true;
}

#endif
