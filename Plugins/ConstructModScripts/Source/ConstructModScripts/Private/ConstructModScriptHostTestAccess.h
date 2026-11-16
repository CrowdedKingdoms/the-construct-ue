#pragma once

#if WITH_DEV_AUTOMATION_TESTS

#include "ConstructModScriptHost.h"
#include "ConstructClaimsSubsystem.h"
#include "GameFramework/PlayerController.h"
#include "Subsystem/CrowdyVoxelWorld.h"
#include "Subsystem/CrowdyModScripts.h"

struct FConstructModScriptHostTestAccess
{
    static void BuildChunk(TFunctionRef<FCrowdyVoxel(FIntVector)> Read, FCrowdyModScriptChunk& Out)
    {
        UConstructModScriptHost::BuildChunk(Read, Out);
    }

    static void SetVoxelWorld(UConstructModScriptHost& Host, UCrowdyVoxelWorld* World)
    {
        Host.VoxelWorld = World;
    }

    static void RetainEdit(UConstructModScriptHost& Host, int64 EditId, const FCrowdyModScript& Script)
    {
        Record(Host, EditId, UConstructModScriptHost::Label(Script));
    }

    static const FConstructScriptLabel* SubmittedEdit(const UConstructModScriptHost& Host, int64 EditId)
    {
        return Host.SubmittedEdits.Find(EditId);
    }

    static void Bind(UConstructModScriptHost& Host, UCrowdyModScripts* Source)
    {
        Host.BindSource(Source);
    }

    static void Unbind(UConstructModScriptHost& Host)
    {
        Host.UnbindSource();
    }

    static void Record(UConstructModScriptHost& Host, int64 EditId, const FConstructScriptLabel& Label)
    {
        Host.SubmittedEdits.Add(EditId, Label);
    }

    static void Complete(UConstructModScriptHost& Host, int64 EditId, const FCrowdyVoxelWrite& Write, ECrowdyVoxelResult Result, const FString& Message)
    {
        Host.EditDone(EditId, Write, Result, Message);
    }

    static void Controller(UConstructModScriptHost& Host, APlayerController* PlayerController)
    {
        Host.SetController(PlayerController);
    }

    static void SetVisitDependencies(UConstructModScriptHost& Host, UConstructClaimsSubsystem* Claims, APlayerController* PlayerController)
    {
        Host.Claims = Claims;
        Host.PlayerController = PlayerController;
    }

    static void UpdateVisit(UConstructModScriptHost& Host)
    {
        Host.UpdateVisit();
    }

    static int32 SourceBindingCount(UConstructModScriptHost& Host, UCrowdyModScripts& Source)
    {
        int32 Count = 0;
        Count += Source.OnModScriptPrompt.IsAlreadyBound(&Host, &UConstructModScriptHost::Prompted) ? 1 : 0;
        Count += Source.OnModScriptPromptWithdrawn.IsAlreadyBound(&Host, &UConstructModScriptHost::PromptWithdrawn) ? 1 : 0;
        Count += Source.OnModScriptStarted.IsAlreadyBound(&Host, &UConstructModScriptHost::Started) ? 1 : 0;
        Count += Source.OnModScriptStopped.IsAlreadyBound(&Host, &UConstructModScriptHost::Stopped) ? 1 : 0;
        Count += Source.OnModScriptError.IsAlreadyBound(&Host, &UConstructModScriptHost::SourceError) ? 1 : 0;
        return Count;
    }

    static bool ShouldCancelVisit(UConstructModScriptHost& Host, const FCrowdyModScriptGrid& Grid, const FInt64Vector& CurrentChunk, bool bCurrentLookupSucceeded)
    {
        return Host.ShouldCancelVisit(Grid, CurrentChunk, bCurrentLookupSucceeded);
    }

    static void PrepareVisit(UConstructModScriptHost& Host, const FInt64Vector& Chunk, uint64 Request)
    {
        Host.VisitChunk = Chunk;
        Host.bHasChunk = true;
        Host.AppliedLookup = Request;
    }
    static uint64 AppliedLookup(const UConstructModScriptHost& Host)
    {
        return Host.AppliedLookup;
    }

    static FInt64Vector VisitChunk(const UConstructModScriptHost& Host)
    {
        return Host.VisitChunk;
    }

    static bool HasChunk(const UConstructModScriptHost& Host)
    {
        return Host.bHasChunk;
    }

    static bool HasController(const UConstructModScriptHost& Host)
    {
        return Host.PlayerController.IsValid();
    }

    static void SignedOut(UConstructModScriptHost& Host, bool bSuccess)
    {
        Host.SignedOut(bSuccess);
    }

    static void AccessEnded(UConstructModScriptHost& Host)
    {
        Host.AccessEnded(TEXT("Test access ended"), FString());
    }
};

#endif
