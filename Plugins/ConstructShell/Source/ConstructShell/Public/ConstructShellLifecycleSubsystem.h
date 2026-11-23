#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "ConstructShellLifecycleSubsystem.generated.h"

/** Observes the SDK lifecycle for shell diagnostics; no parallel session or authentication state. */
UCLASS()
class CONSTRUCTSHELL_API UConstructShellLifecycleSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

private:
	UFUNCTION()
	void LegalRequired();
	UFUNCTION()
	void LegalEnded();
	UFUNCTION()
	void Login(bool bSuccess, FString Message);
	UFUNCTION()
	void Register(bool bSuccess, FString Message);
	UFUNCTION()
	void Connected();
	UFUNCTION()
	void ConnectionLost();
	UFUNCTION()
	void Address(bool bSuccess, bool GateKeep);
	UFUNCTION()
	void Refreshed();
	UFUNCTION()
	void AccessEnded(const FString& Reason, const FString& BlockedUntil);
	UFUNCTION()
	void Paused(bool bPaused, const FString& Reason);
	UFUNCTION()
	void Logout(bool bSuccess);
};
