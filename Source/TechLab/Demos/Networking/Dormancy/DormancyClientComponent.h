#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DormancySettings.h"
#include "DormancyClientComponent.generated.h"

class ATechLabPlayerController;
class SDormancyHUD;
class ADormancySample;

/** Owned RPC endpoint and client-side validation for the Dormancy demo. */
UCLASS()
class TECHLAB_API UDormancyClientComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UDormancyClientComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	void TogglePanel();
	void ClosePanel();
	void RequestAttack();
	void RestoreSelectedPair();
	FString GetResourceText(int32 Index) const;
	float GetResourceFraction(int32 Index) const;
	FVector2D GetResourcePosition(int32 Index) const;

	void OnWorldLoaded();

	void RequestLab(int32 Action);

	ATechLabPlayerController* GetController() const;
	FDormancySettings& GetDraftSettings();
	const FString& GetLiveText() const;

private:
	void RefreshLab();
	void EnsureHUD();
	void RefreshSelection();
	void UpdateBenchmarkView();

	// Owned network requests and responses.
	UFUNCTION(Server, Reliable)
	void ServerAttack();

	UFUNCTION(Server, Reliable)
	void ServerRestorePair(int32 Pair);

	UFUNCTION(Client, Unreliable)
	void ClientAttackResult(FVector Start, FVector End, FVector Hit, bool bApplied);

	UFUNCTION(Server, Reliable)
	void ServerLabAction(int32 Action, FDormancySettings NewSettings);

	UFUNCTION(Server, Reliable)
	void ServerLabReport(FGuid ReportRun, const FString& ReportPhase, int32 Count, const FString& Digest);

	UFUNCTION(Client, Reliable)
	void ClientLabResponse(bool bAccepted, const FString& Message);

	// Local draft and HUD state.
	FDormancySettings DraftSettings;
	FString LabLiveText;
	FString LabMessage;
	TSharedPtr<SDormancyHUD> HUD;
	FTimerHandle LabTimer;
	FTimerHandle SelectionTimer;
	TWeakObjectPtr<ADormancySample> SelectedNodes[2];
	double LastLocalAttack = -1;
	double LastRestore = -1;
	bool bBenchmarkView = false;

	// Final-state reporting.
	double LastVerification = -1;
	FString LastVerificationKey;

	// Command-line validation driver.
	double AutomationDoneAt = -1;
	bool bAutomationSent = false;
	int32 ControlTestStage = 0;
};
