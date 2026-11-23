#if WITH_DEV_AUTOMATION_TESTS

#include "ConstructStudioDock.h"
#include "ConstructStudioDockTestAccess.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"

namespace ConstructStudioDockTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
	using FAccess = FConstructStudioDockTestAccess;
	using FRig = FConstructStudioDockTestRig;

	FString StateOf(const FRig& Rig)
	{
		return ConstructStudioDockName(Rig.Dock->GetState());
	}

	FConstructStudioCapture Captured(int32 Width, int32 Height)
	{
		FConstructStudioCapture Capture;
		Capture.bCaptured = true;
		Capture.PngBase64 = TEXT("AAAA");
		Capture.Width = Width;
		Capture.Height = Height;
		return Capture;
	}
}

using namespace ConstructStudioDockTests;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructStudioDockRefusalTest, "Construct.StudioDock.RefusesAtOnceWithStateUnchanged", Flags)
bool FConstructStudioDockRefusalTest::RunTest(const FString& Parameters)
{
	{
		FRig Out(false);
		TestEqual(TEXT("no signed-in player refuses NotSignedIn"), ConstructStudioDockName(Out.Open()), FString(TEXT("NotSignedIn")));
		TestEqual(TEXT("the refused dock stays Closed"), StateOf(Out), FString(TEXT("Closed")));
		TestEqual(TEXT("a refusal fires no event"), Out.Listener->Lines.Num(), 0);
	}
	FRig F;
	TestEqual(TEXT("no grid refuses NoGrid"), ConstructStudioDockName(F.Dock->Open(0, FText::GetEmpty())), FString(TEXT("NoGrid")));
	TestEqual(TEXT("NoGrid leaves it Closed with no event"), F.Lines(), FString());
	TestEqual(TEXT("a signed-in player on a grid opens"), ConstructStudioDockName(F.Open()), FString(TEXT("None")));
	TestEqual(TEXT("the state is Booting at once"), StateOf(F), FString(TEXT("Booting")));
	TestEqual(TEXT("a second Open while booting refuses AlreadyOpen"), ConstructStudioDockName(F.Open()), FString(TEXT("AlreadyOpen")));
	{
		FRig Second;
		TestEqual(TEXT("a second dock in this process refuses NotAvailable"), ConstructStudioDockName(Second.Open()), FString(TEXT("NotAvailable")));
		TestEqual(TEXT("the second dock stays Closed"), StateOf(Second), FString(TEXT("Closed")));
	}
	FAccess::Advance(*F.Dock, 0.016f);
	TestEqual(TEXT("Open while Failed refuses AlreadyOpen"), ConstructStudioDockName(F.Open()), FString(TEXT("AlreadyOpen")));
	TestEqual(TEXT("only the boot changed the state"), F.Lines(), FString(TEXT("State:Booting:None | State:Failed:NotBuiltYet")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructStudioDockInertTest, "Construct.StudioDock.InertBootFailsThenClosesFromFailed", Flags)
bool FConstructStudioDockInertTest::RunTest(const FString& Parameters)
{
	{
		FRig F;
		F.Open();
		const int64 First = FAccess::Generation(*F.Dock);
		FAccess::Advance(*F.Dock, 0.016f);
		TestEqual(TEXT("with no live boot and no claim the boot fails"), StateOf(F), FString(TEXT("Failed")));
		TestEqual(TEXT("with NotBuiltYet"), ConstructStudioDockName(F.Dock->GetReason()), FString(TEXT("NotBuiltYet")));
		TestFalse(TEXT("the reason has text the panel can show"), F.Dock->GetReasonText().IsEmpty());
		FAccess::Advance(*F.Dock, 120.f);
		TestEqual(TEXT("Failed keeps its reason until Close"), ConstructStudioDockName(F.Dock->GetReason()), FString(TEXT("NotBuiltYet")));
		F.Dock->Close();
		TestEqual(TEXT("Close returns Failed to Closed"), StateOf(F), FString(TEXT("Closed")));
		TestEqual(TEXT("a later Open works"), ConstructStudioDockName(F.Open()), FString(TEXT("None")));
		TestTrue(TEXT("and is a new generation"), FAccess::Generation(*F.Dock) > First);
		TestEqual(TEXT("the events in order"), F.Lines(),
			FString(TEXT("State:Booting:None | State:Failed:NotBuiltYet | State:Closed:Closed | State:Booting:None")));
	}
	{
		FRig F;
		F.Open();
		FAccess::ClaimBoot(*F.Dock);
		FAccess::Advance(*F.Dock, 59.f);
		TestEqual(TEXT("a replayed boot is still Booting at 59 s"), StateOf(F), FString(TEXT("Booting")));
		FAccess::Advance(*F.Dock, 1.5f);
		TestEqual(TEXT("a boot that takes 60 s fails BootTimedOut"), ConstructStudioDockName(F.Dock->GetReason()), FString(TEXT("BootTimedOut")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructStudioDockGenerationTest, "Construct.StudioDock.LateAnswerIsDropped", Flags)
bool FConstructStudioDockGenerationTest::RunTest(const FString& Parameters)
{
	FRig F;
	F.Ready();
	const int64 First = FAccess::Generation(*F.Dock);
	FAccess::Screenshot(*F.Dock, First, TEXT("first"));
	TestEqual(TEXT("the host was asked"), F.Listener->Captures.Num(), 1);
	F.Dock->Close();
	TestEqual(TEXT("Close refuses the pending screenshot itself"), F.SentCount(TEXT("studio.screenshot:refused: closed")), 1);
	F.Open();
	FAccess::ClaimBoot(*F.Dock);
	FAccess::Boot(*F.Dock, First, EConstructStudioDockState::Ready);
	TestEqual(TEXT("a boot answer from the closed boot is dropped"), StateOf(F), FString(TEXT("Booting")));
	FAccess::Screenshot(*F.Dock, First, TEXT("stale"));
	TestEqual(TEXT("a request from the closed boot is dropped"), F.Listener->Captures.Num(), 1);
	UConstructStudioDock::FinishCapture(F.Listener->Captures[0], Captured(640, 360));
	TestEqual(TEXT("the closed boot's capture answer is dropped"), F.SentCount(TEXT("studio.screenshot:captured 640x360")), 0);
	FAccess::Boot(*F.Dock, FAccess::Generation(*F.Dock), EConstructStudioDockState::Ready);
	TestEqual(TEXT("the current boot's answer is taken"), StateOf(F), FString(TEXT("Ready")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructStudioDockCloseTest, "Construct.StudioDock.CloseFromAnyStateWithdrawsThePrompt", Flags)
bool FConstructStudioDockCloseTest::RunTest(const FString& Parameters)
{
	{
		FRig F;
		F.Open();
		F.Dock->Close();
		TestEqual(TEXT("Booting closes"), StateOf(F), FString(TEXT("Closed")));
	}
	{
		FRig F;
		F.Open();
		FAccess::ClaimBoot(*F.Dock);
		FAccess::Boot(*F.Dock, FAccess::Generation(*F.Dock), EConstructStudioDockState::NeedsConsent);
		F.Dock->Close();
		TestEqual(TEXT("NeedsConsent closes"), StateOf(F), FString(TEXT("Closed")));
	}
	{
		FRig F;
		F.Ready();
		FAccess::Request(*F.Dock, EConstructStudioDeployKind::DraftTest);
		TestEqual(TEXT("a prompt is up"), F.Listener->Prompts.Num(), 1);
		const int64 PromptId = F.Listener->Prompts.IsEmpty() ? 0 : F.Listener->Prompts[0].PromptId;
		F.Dock->Close();
		TestEqual(TEXT("Ready closes"), StateOf(F), FString(TEXT("Closed")));
		TestTrue(TEXT("Close withdraws the pending prompt"), F.Listener->WithdrawnIds.Contains(PromptId));
		TestEqual(TEXT("and refuses it to the agent"), F.SentCount(TEXT("AnswerDeployPrompt:Refused")), 1);
		TestEqual(TEXT("and says goodbye"), F.SentCount(TEXT("Close:Closed")), 1);
		F.Dock->AnswerDeployPrompt(PromptId, true);
		TestEqual(TEXT("an answer after Close does nothing"), F.SentCount(TEXT("AnswerDeployPrompt:Run")), 0);
	}
	{
		FRig F;
		F.Open();
		FAccess::Advance(*F.Dock, 0.016f);
		F.Dock->Close();
		TestEqual(TEXT("Failed closes"), StateOf(F), FString(TEXT("Closed")));
		F.Dock->Close();
		TestEqual(TEXT("closing a closed dock fires nothing more"), F.Listener->Lines.Num(), 3);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructStudioDockDeclineTest, "Construct.StudioDock.PromptDeclinesAfterSixtySeconds", Flags)
bool FConstructStudioDockDeclineTest::RunTest(const FString& Parameters)
{
	FRig F;
	F.Ready();
	FAccess::Request(*F.Dock, EConstructStudioDeployKind::DraftTest);
	const int64 PromptId = F.Listener->Prompts.IsEmpty() ? 0 : F.Listener->Prompts[0].PromptId;
	FAccess::Advance(*F.Dock, 59.f);
	TestEqual(TEXT("at 59 s the prompt is still up"), F.Listener->WithdrawnIds.Num(), 0);
	FAccess::Advance(*F.Dock, 1.5f);
	TestTrue(TEXT("at 60 s it withdraws itself"), F.Listener->WithdrawnIds == TArray<int64>({ PromptId }));
	TestEqual(TEXT("and declines to the agent"), F.SentCount(TEXT("AnswerDeployPrompt:NotNow")), 1);
	F.Dock->AnswerDeployPrompt(PromptId, true);
	TestEqual(TEXT("a late Run is ignored"), F.SentCount(TEXT("AnswerDeployPrompt:Run")), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructStudioDockOneAtATimeTest, "Construct.StudioDock.OnePromptAndOneRunAtATime", Flags)
bool FConstructStudioDockOneAtATimeTest::RunTest(const FString& Parameters)
{
	FRig F;
	F.Ready();
	FAccess::Request(*F.Dock, EConstructStudioDeployKind::DraftTest);
	FAccess::Request(*F.Dock, EConstructStudioDeployKind::Live);
	TestEqual(TEXT("a second request while a prompt is up is refused"), F.Listener->Prompts.Num(), 1);
	TestEqual(TEXT("as busy"), F.SentCount(TEXT("studio.deployLive:refused: busy")), 1);
	const int64 First = F.Listener->Prompts.IsEmpty() ? 0 : F.Listener->Prompts[0].PromptId;
	F.Dock->AnswerDeployPrompt(First, true);
	TestEqual(TEXT("Run is sent"), F.SentCount(TEXT("AnswerDeployPrompt:Run")), 1);
	FAccess::Request(*F.Dock, EConstructStudioDeployKind::Live);
	TestEqual(TEXT("the slot is held until the run answers"), F.SentCount(TEXT("studio.deployLive:refused: busy")), 2);
	FAccess::RunAnswered(*F.Dock, true);
	TestEqual(TEXT("the run's answer goes to the agent"), F.SentCount(TEXT("RunProject:ok")), 1);
	FAccess::Request(*F.Dock, EConstructStudioDeployKind::Live);
	TestEqual(TEXT("then a new prompt shows"), F.Listener->Prompts.Num(), 2);
	if (F.Listener->Prompts.Num() == 2)
	{
		TestTrue(TEXT("with a new id"), F.Listener->Prompts[1].PromptId > First);
		TestEqual(TEXT("of the asked kind"), ConstructStudioDockName(F.Listener->Prompts[1].Kind), FString(TEXT("Live")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructStudioDockFixedPromptTest, "Construct.StudioDock.RunRefusedWhenGridProjectOrRevisionChanged", Flags)
bool FConstructStudioDockFixedPromptTest::RunTest(const FString& Parameters)
{
	FRig F;
	F.Ready(TEXT("7b1f7e2c"), TEXT("rev-1"));
	FAccess::FileChanged(*F.Dock, TEXT("server/src/logic.rs"), TEXT("rev-2"));
	FAccess::Request(*F.Dock, EConstructStudioDeployKind::DraftTest);
	if (!TestEqual(TEXT("a prompt is up"), F.Listener->Prompts.Num(), 1))
	{
		return false;
	}
	const FConstructStudioDeployPrompt Prompt = F.Listener->Prompts[0];
	TestTrue(TEXT("it carries an id"), Prompt.PromptId > 0);
	TestEqual(TEXT("the grid's id"), Prompt.GridId, int64(100));
	TestEqual(TEXT("the grid's label"), Prompt.GridLabel.ToString(), FString(TEXT("Chunk 1, 2, 3")));
	TestEqual(TEXT("the project's id"), Prompt.ProjectId, FString(TEXT("7b1f7e2c")));
	TestEqual(TEXT("the project's name"), Prompt.ProjectName, FString(TEXT("Project")));
	TestEqual(TEXT("the saved revision when asked"), Prompt.Revision, FString(TEXT("rev-2")));
	TestTrue(TEXT("whether it reaches visitors"), Prompt.bReachesVisitors);
	TestEqual(TEXT("the files changed in this boot"), FString::Join(Prompt.ChangedFiles, TEXT(",")), FString(TEXT("server/src/logic.rs")));

	FAccess::FileChanged(*F.Dock, TEXT("client/src/lib.rs"), TEXT("rev-3"));
	TestTrue(TEXT("a save that changes the revision withdraws it"), F.Listener->WithdrawnIds == TArray<int64>({ Prompt.PromptId }));

	FAccess::Request(*F.Dock, EConstructStudioDeployKind::DraftTest);
	F.Dock->SetGrid(100, FText::FromString(TEXT("Chunk 1, 2, 3")));
	TestEqual(TEXT("the same grid withdraws nothing"), F.Listener->WithdrawnIds.Num(), 1);
	F.Dock->SetGrid(200, FText::FromString(TEXT("Chunk 4, 5, 6")));
	TestEqual(TEXT("moving to another grid withdraws it"), F.Listener->WithdrawnIds.Num(), 2);

	FAccess::Request(*F.Dock, EConstructStudioDeployKind::DraftTest);
	TestEqual(TEXT("the new prompt names the new grid"), F.Listener->Prompts.Last().GridId, int64(200));
	FAccess::Project(*F.Dock, TEXT("7B1F7E2C"), TEXT("Other"), TEXT("rev-3"), false);
	F.Dock->AnswerDeployPrompt(F.Listener->Prompts.Last().PromptId, true);
	TestEqual(TEXT("Run after the project changed, if only in case, is refused"), F.SentCount(TEXT("AnswerDeployPrompt:Run")), 0);
	TestEqual(TEXT("and the prompt is withdrawn"), F.Listener->WithdrawnIds.Num(), 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructStudioDockConsentTest, "Construct.StudioDock.ConsentCarriesTheBootOn", Flags)
bool FConstructStudioDockConsentTest::RunTest(const FString& Parameters)
{
	FRig F;
	F.Open();
	FAccess::ClaimBoot(*F.Dock);
	F.Dock->GiveProviderConsent();
	TestEqual(TEXT("consent outside NeedsConsent is ignored"), F.SentCount(TEXT("GiveProviderConsent:")), 0);
	const int64 Generation = FAccess::Generation(*F.Dock);
	FAccess::Boot(*F.Dock, Generation, EConstructStudioDockState::NeedsConsent, EConstructStudioDockReason::None, FText::FromString(TEXT("Notice text")));
	TestEqual(TEXT("NeedsConsent is a state"), StateOf(F), FString(TEXT("NeedsConsent")));
	TestEqual(TEXT("with its own reason"), ConstructStudioDockName(F.Dock->GetReason()), FString(TEXT("NeedsConsent")));
	TestEqual(TEXT("the notice is the platform's"), F.Dock->GetConsentNotice().ToString(), FString(TEXT("Notice text")));
	FAccess::Boot(*F.Dock, Generation, EConstructStudioDockState::Ready);
	TestEqual(TEXT("Ready cannot skip consent"), StateOf(F), FString(TEXT("NeedsConsent")));
	F.Dock->GiveProviderConsent();
	TestEqual(TEXT("consent carries the boot on"), StateOf(F), FString(TEXT("Booting")));
	TestEqual(TEXT("and is recorded"), F.SentCount(TEXT("GiveProviderConsent:")), 1);
	FAccess::Boot(*F.Dock, Generation, EConstructStudioDockState::Ready);
	TestEqual(TEXT("then the boot reaches Ready"), StateOf(F), FString(TEXT("Ready")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructStudioDockCaptureTest, "Construct.StudioDock.CaptureAnswersOnceThroughTheHost", Flags)
bool FConstructStudioDockCaptureTest::RunTest(const FString& Parameters)
{
	FRig F;
	F.Ready();
	FAccess::Screenshot(*F.Dock, FAccess::Generation(*F.Dock), TEXT("look"));
	TestTrue(TEXT("the host is asked with the label"), F.Listener->Lines.Contains(TEXT("Capture:look")));
	if (!TestEqual(TEXT("one capture is pending"), F.Listener->Captures.Num(), 1))
	{
		return false;
	}
	UConstructStudioDock::FinishCapture(F.Listener->Captures[0], Captured(640, 360));
	UConstructStudioDock::FinishCapture(F.Listener->Captures[0], Captured(640, 360));
	TestEqual(TEXT("the answer reaches the agent once"), F.SentCount(TEXT("studio.screenshot:captured 640x360")), 1);

	F.Dock->ShareScreenshot();
	if (TestEqual(TEXT("the player's share asks the host too"), F.Listener->Captures.Num(), 2))
	{
		FConstructStudioCapture Refused;
		Refused.Refusal = TEXT("no frame drawn");
		UConstructStudioDock::FinishCapture(F.Listener->Captures[1], Refused);
		TestEqual(TEXT("a refusal is passed on"), F.SentCount(TEXT("page.capture:refused: no frame drawn")), 1);
	}

	F.Dock->SetHost(TScriptInterface<IConstructStudioDockHost>());
	FAccess::Screenshot(*F.Dock, FAccess::Generation(*F.Dock), TEXT("gone"));
	TestEqual(TEXT("with no host the request is refused at once"), F.SentCount(TEXT("studio.screenshot:refused: no host")), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructStudioDockFocusTest, "Construct.StudioDock.FocusAndEscape", Flags)
bool FConstructStudioDockFocusTest::RunTest(const FString& Parameters)
{
	FRig F;
	FAccess::Focus(*F.Dock, true);
	TestEqual(TEXT("a closed dock reports no focus"), F.Listener->Lines.Num(), 0);
	F.Ready();
	FAccess::Focus(*F.Dock, true);
	FAccess::Focus(*F.Dock, true);
	FAccess::CloseRequest(*F.Dock);
	TestEqual(TEXT("Escape asks; the game decides"), StateOf(F), FString(TEXT("Ready")));
	F.Dock->Close();
	TestEqual(TEXT("focus once, Escape, then Close drops focus before Closed"), F.Lines(),
		FString(TEXT("State:Booting:None | State:Ready:None | Focus:1 | CloseRequested | Focus:0 | State:Closed:Closed")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructStudioDockSignOutTest, "Construct.StudioDock.ClosesItselfWhenThePlayerChanges", Flags)
bool FConstructStudioDockSignOutTest::RunTest(const FString& Parameters)
{
	{
		FRig F;
		F.Ready();
		FAccess::SignIn(*F.Dock, 0);
		FAccess::Advance(*F.Dock, 0.016f);
		TestEqual(TEXT("sign-out closes it"), ConstructStudioDockName(F.Dock->GetReason()), FString(TEXT("SignedOut")));
		TestEqual(TEXT("to Closed"), StateOf(F), FString(TEXT("Closed")));
	}
	{
		FRig F;
		F.Ready();
		FAccess::SignIn(*F.Dock, 43);
		FAccess::Advance(*F.Dock, 0.016f);
		TestEqual(TEXT("another signed-in user closes it"), ConstructStudioDockName(F.Dock->GetReason()), FString(TEXT("UserChanged")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructStudioDockAccessEndedTest, "Construct.StudioDock.ClosesItselfWhenAccessEnds", Flags)
bool FConstructStudioDockAccessEndedTest::RunTest(const FString& Parameters)
{
	FRig F;
	F.Ready();
	FAccess::EndAccess(*F.Dock);
	FAccess::Advance(*F.Dock, 0.016f);
	TestEqual(TEXT("ended access closes it"), ConstructStudioDockName(F.Dock->GetReason()), FString(TEXT("AccessEnded")));
	TestEqual(TEXT("to Closed"), StateOf(F), FString(TEXT("Closed")));
	TestEqual(TEXT("and says why to the agent"), F.SentCount(TEXT("Close:AccessEnded")), 1);
	TestEqual(TEXT("a later Open is refused"), ConstructStudioDockName(F.Open()), FString(TEXT("NotSignedIn")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructStudioDockWorldCleanupTest, "Construct.StudioDock.ClosesWithItsWorld", Flags)
bool FConstructStudioDockWorldCleanupTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::EditorPreview, false);
	UWorld* Other = UWorld::CreateWorld(EWorldType::EditorPreview, false);
	FRig F(true, World);
	F.Open();
	Other->DestroyWorld(false);
	TestEqual(TEXT("another world's cleanup leaves it open"), StateOf(F), FString(TEXT("Booting")));
	World->DestroyWorld(false);
	TestEqual(TEXT("its own world's cleanup closes it"), StateOf(F), FString(TEXT("Closed")));
	TestEqual(TEXT("as WorldEnded"), ConstructStudioDockName(F.Dock->GetReason()), FString(TEXT("WorldEnded")));
	TestEqual(TEXT("with its event"), F.Lines(), FString(TEXT("State:Booting:None | State:Closed:WorldEnded")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructStudioDockSlateGoneTest, "Construct.StudioDock.ClosesWhenItsSlateWidgetGoes", Flags)
bool FConstructStudioDockSlateGoneTest::RunTest(const FString& Parameters)
{
	{
		FRig F;
		TSharedPtr<SWidget> Slot = F.Dock->TakeWidget();
		F.Open();
		Slot.Reset();
		TestEqual(TEXT("dropping its Slate widget closes the dock"), StateOf(F), FString(TEXT("Closed")));
		TestEqual(TEXT("as Closed"), F.Lines(), FString(TEXT("State:Booting:None | State:Closed:Closed")));
		FRig Next;
		TestEqual(TEXT("so a new dock opens"), ConstructStudioDockName(Next.Open()), FString(TEXT("None")));
	}
	{
		FRig F;
		TSharedPtr<SWidget> Slot = F.Dock->TakeWidget();
		F.Open();
		F.Dock->ReleaseSlateResources(true);
		TestEqual(TEXT("releasing its Slate resources closes it"), StateOf(F), FString(TEXT("Closed")));
		FRig Next;
		TestEqual(TEXT("so a new dock opens"), ConstructStudioDockName(Next.Open()), FString(TEXT("None")));
		Next.Dock->Close();
		F.Open();
		Slot.Reset();
		TestEqual(TEXT("the released widget going later leaves a reopened dock open"), StateOf(F), FString(TEXT("Booting")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructStudioDockReopenFocusTest, "Construct.StudioDock.FocusReportedAgainAfterReopen", Flags)
bool FConstructStudioDockReopenFocusTest::RunTest(const FString& Parameters)
{
	FRig F;
	F.Ready();
	FAccess::SetSlotFocus(*F.Dock, true);
	FAccess::Focus(*F.Dock, true);
	FAccess::CloseRequest(*F.Dock);
	F.Dock->Close();
	TestFalse(TEXT("Close takes keyboard focus off the slot"), FAccess::SlotFocused(*F.Dock));
	FAccess::SetSlotFocus(*F.Dock, true);
	FAccess::Focus(*F.Dock, true);
	TestEqual(TEXT("a click on the closed slot reports nothing"), F.Listener->Lines.Last(), FString(TEXT("State:Closed:Closed")));
	F.Open();
	TestEqual(TEXT("Open reports the focus the slot already holds"), F.Lines(),
		FString(TEXT("State:Booting:None | State:Ready:None | Focus:1 | CloseRequested | Focus:0 | State:Closed:Closed | State:Booting:None | Focus:1")));
	F.Dock->Close();
	F.Open();
	TestEqual(TEXT("a slot without focus reports none"), F.Listener->Lines.Last(), FString(TEXT("State:Booting:None")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructStudioDockUnreadProjectTest, "Construct.StudioDock.DeployRefusedBeforeTheProjectIsRead", Flags)
bool FConstructStudioDockUnreadProjectTest::RunTest(const FString& Parameters)
{
	FRig F;
	TestEqual(TEXT("Open takes a project id"), ConstructStudioDockName(F.Dock->Open(100, FText::FromString(TEXT("Chunk 1, 2, 3")), TEXT("7b1f7e2c"))), FString(TEXT("None")));
	FAccess::ClaimBoot(*F.Dock);
	FAccess::Boot(*F.Dock, FAccess::Generation(*F.Dock), EConstructStudioDockState::Ready);
	FAccess::Request(*F.Dock, EConstructStudioDeployKind::DraftTest);
	TestEqual(TEXT("a draft test before the project is read is refused"), F.SentCount(TEXT("studio.draftTest:refused: no project")), 1);
	FAccess::FileChanged(*F.Dock, TEXT("server/src/logic.rs"), TEXT("rev-2"));
	FAccess::Request(*F.Dock, EConstructStudioDeployKind::Live);
	TestEqual(TEXT("a save is not the project's read"), F.SentCount(TEXT("studio.deployLive:refused: no project")), 1);
	FAccess::Project(*F.Dock, TEXT("7b1f7e2c"), TEXT("Project"), FString(), false);
	FAccess::Request(*F.Dock, EConstructStudioDeployKind::DraftTest);
	TestEqual(TEXT("a read with no revision is refused too"), F.SentCount(TEXT("studio.draftTest:refused: no project")), 2);
	TestEqual(TEXT("and no prompt shows"), F.Listener->Prompts.Num(), 0);
	FAccess::Project(*F.Dock, TEXT("7b1f7e2c"), TEXT("Project"), TEXT("rev-3"), false);
	FAccess::Request(*F.Dock, EConstructStudioDeployKind::DraftTest);
	if (TestEqual(TEXT("once read, the prompt shows"), F.Listener->Prompts.Num(), 1))
	{
		TestEqual(TEXT("at the read revision"), F.Listener->Prompts[0].Revision, FString(TEXT("rev-3")));
	}
	return true;
}

#endif
