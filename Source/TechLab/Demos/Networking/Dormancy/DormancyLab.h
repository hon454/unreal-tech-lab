#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DormancySettings.h"
#include "DormancyLab.generated.h"

class ATechLabPlayerController;
class ADormancySample;

UCLASS()
class TECHLAB_API ADormancyLab : public AActor
{
	GENERATED_BODY()

public:
	ADormancyLab();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;

	// Read-only experiment state.
	const FDormancySettings& GetSettings() const;
	FGuid GetRun() const;
	const FString& GetPhase() const;
	const FString& GetStatus() const;

	// Published counters.
	int32 GetChanges() const;
	int32 GetFlushes() const;
	int32 GetWakes() const;
	int32 GetAwake() const;
	int32 GetMatched() const;
	int32 GetConnections() const;
	float GetConvergence() const;
	const FString& GetConnectionStats() const;
	const FString& GetSessionResults() const;
	int32 GetSteps() const;

	// Server request validation and client reports.
	bool IsRunning() const;
	bool CanControl(const ATechLabPlayerController* PC) const;
	bool Request(ATechLabPlayerController* PC, int32 Action, const FDormancySettings& NewSettings);
	void Report(ATechLabPlayerController* PC, FGuid ReportRun, const FString& ReportPhase, int32 Count, const FString& Digest);

	// Lookup and state comparison.
	static ADormancyLab* Find(const UWorld* World);
	static FString Digest(const TArray<ADormancySample*>& Actors, FGuid ForRun, int32& Count);

	UPROPERTY(EditAnywhere)
	bool bExhibit = true;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

private:
	void Service();
	void SetPhase(const FString& Name);
	void Start();
	// Clears an experiment run; separate from AActor::Reset and its level-reset event.
	void ResetExperiment();
	void ChangeBatch();

	void PrepareVerification();
	void Finish(bool bSuccess);

	void Record(const FString& Event);
	void RecordConnections();

	UPROPERTY(Replicated)
	FDormancySettings Settings;

	UPROPERTY(Replicated)
	FGuid Run;

	UPROPERTY(Replicated)
	FString Phase = TEXT("Idle");

	UPROPERTY(Replicated)
	FString Status;

	UPROPERTY(Replicated)
	FString ConnectionStats;

	UPROPERTY(Replicated)
	FString SessionResults;

	UPROPERTY(Replicated)
	int32 Changes = 0;

	UPROPERTY(Replicated)
	int32 Flushes = 0;

	UPROPERTY(Replicated)
	int32 Wakes = 0;

	UPROPERTY(Replicated)
	int32 Awake = 0;

	UPROPERTY(Replicated)
	int32 Matched = 0;

	UPROPERTY(Replicated)
	int32 Connections = 0;

	UPROPERTY(Replicated)
	float Convergence = -1.f;

	UPROPERTY(Replicated)
	int32 Steps = 0;

	UPROPERTY()
	TArray<TObjectPtr<ADormancySample>> Samples;

	TMap<TWeakObjectPtr<ATechLabPlayerController>, double> Matches;
	TArray<TWeakObjectPtr<ATechLabPlayerController>> Participants;

	// Experiment scheduling.
	FTimerHandle Timer;
	FRandomStream Random;
	TArray<int32> DamageTargets;
	TMap<int32, TPair<int64, int64>> PreviousNetwork;
	double PreviousNetworkTime = 0;
	int32 InitialHealth = 100;
	int32 CatchupSteps = 0;
	bool bComparing = false;
	double PhaseStart = 0, NextChange = 0, LastPublish = 0;
	double StableSeconds = 15, ChangeSeconds = 15, WarmSeconds = 5;
	double LastRequest = -1;

	// Verification and output.
	FString ExpectedDigest, OutputDir;
	int32 ExpectedCount = 0;
	bool bAborted = false, bConnectionChanged = false;
};
