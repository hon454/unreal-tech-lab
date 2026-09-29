#include "DormancyLab.h"
#include "TechLabPlayerController.h"
#include "DormancySample.h"
#include "DormancyExhibit.h"
#include "Engine/World.h"
#include "Engine/NetConnection.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerState.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "Misc/SecureHash.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Misc/EngineVersion.h"
#include "HAL/FileManager.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "ProfilingDebugging/CountersTrace.h"
#include "ProfilingDebugging/MiscTrace.h"

TRACE_DECLARE_INT_COUNTER(LabChanges, TEXT("Dormancy/StateChanges"));
TRACE_DECLARE_INT_COUNTER(LabAwake, TEXT("Dormancy/ConfiguredAwake"));
TRACE_DECLARE_INT_COUNTER(LabFlushes, TEXT("Dormancy/FlushCalls"));
TRACE_DECLARE_INT_COUNTER(LabWakes, TEXT("Dormancy/WakeCalls"));

ADormancyLab::ADormancyLab()
{
	bReplicates = true;
	bAlwaysRelevant = true;
	PrimaryActorTick.bCanEverTick = false;
	SetNetUpdateFrequency(2);
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void ADormancyLab::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ADormancyLab, Settings);
	DOREPLIFETIME(ADormancyLab, Run);
	DOREPLIFETIME(ADormancyLab, Phase);
	DOREPLIFETIME(ADormancyLab, Status);
	DOREPLIFETIME(ADormancyLab, ConnectionStats);
	DOREPLIFETIME(ADormancyLab, SessionResults);
	DOREPLIFETIME(ADormancyLab, Changes);
	DOREPLIFETIME(ADormancyLab, Wakes);
	DOREPLIFETIME(ADormancyLab, Flushes);
	DOREPLIFETIME(ADormancyLab, Awake);
	DOREPLIFETIME(ADormancyLab, Matched);
	DOREPLIFETIME(ADormancyLab, Connections);
	DOREPLIFETIME(ADormancyLab, Convergence);
	DOREPLIFETIME(ADormancyLab, Steps);
}

void ADormancyLab::BeginPlay()
{
	Super::BeginPlay();
	UE_LOG(LogTemp, Display, TEXT("LAB_WORLD_READY Map=%s Authority=%d"), *GetWorld()->GetMapName(), HasAuthority());
	if (HasAuthority())
	{
		FParse::Value(FCommandLine::Get(), TEXT("LabStable="), StableSeconds);
		FParse::Value(FCommandLine::Get(), TEXT("LabChange="), ChangeSeconds);
		FParse::Value(FCommandLine::Get(), TEXT("LabWarm="), WarmSeconds);
		StableSeconds = FMath::Clamp(StableSeconds, 2., 120.);
		ChangeSeconds = FMath::Clamp(ChangeSeconds, 2., 120.);
		WarmSeconds = FMath::Clamp(WarmSeconds, 2., 60.);
		GetWorldTimerManager().SetTimer(Timer, this, &ADormancyLab::Service, .025f, true);
	}
}

void ADormancyLab::EndPlay(const EEndPlayReason::Type Reason)
{
	GetWorldTimerManager().ClearTimer(Timer);
	Super::EndPlay(Reason);
}

ADormancyLab* ADormancyLab::Find(const UWorld* World)
{
	if (!World || !World->GetMapName().EndsWith(TEXT("L_Networking_Dormancy")))
	{
		return nullptr;
	}

	for (TActorIterator<ADormancyLab> It(World); It; ++It)
	{
		return *It;
	}

	return nullptr;
}

bool ADormancyLab::IsRunning() const
{
	return Phase != TEXT("Idle") && Phase != TEXT("Complete") && Phase != TEXT("Failed") && Phase != TEXT("Stopped");
}

bool ADormancyLab::CanControl(const ATechLabPlayerController* PC) const
{
	return PC && PC->PlayerState && PC->GetWorld() == GetWorld();
}

bool ADormancyLab::Request(ATechLabPlayerController* PC, int32 Action, const FDormancySettings& NewSettings)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(Dormancy_ServerRequest);
	if (!HasAuthority() || !CanControl(PC) || Action < 0 || Action > 7 || Action == 3 || Action == 4 || !NewSettings.IsValid())
	{
		return false;
	}

	const double Now = GetWorld()->GetTimeSeconds();
	if (Now - LastRequest < .15)
	{
		return false;
	}

	LastRequest = Now;
	if (Action == 1 && IsRunning() && Phase != TEXT("Converge"))
	{
		bAborted = true;
		bComparing = false;
		PrepareVerification();
		SetPhase(TEXT("Converge"));
		return true;
	}

	if (IsRunning())
	{
		return false;
	}

	if (Action == 7)
	{
		if (GetWorld()->IsInSeamlessTravel())
		{
			return false;
		}

		const FString Map = TEXT("/Game/Maps/Networking/L_Networking_Dormancy");
		return GetWorld()->ServerTravel(Map);
	}

	if (!bExhibit)
	{
		return false;
	}

	if (Action == 2)
	{
		bComparing = false;
		ResetExperiment();
		return true;
	}

	if (Action == 5)
	{
		// A completed field belongs to its recorded settings. Do not relabel it with a new configuration.
		if (Run.IsValid())
		{
			ResetExperiment();
		}

		Settings = NewSettings;
		ForceNetUpdate();
		return true;
	}

	if (Action == 0 || Action == 6)
	{
		bComparing = Action == 6;
		SessionResults.Empty();
		Settings = NewSettings;
		if (bComparing)
		{
			Settings.Scenario = 0;
		}

		Start();
		return true;
	}

	return false;
}

void ADormancyLab::ResetExperiment()
{
	for (ADormancySample* A : Samples)
	{
		if (IsValid(A))
		{
			A->Destroy();
		}
	}

	Samples.Empty();
	Matches.Empty();
	Participants.Empty();
	ExpectedDigest.Empty();
	DamageTargets.Empty();
	PreviousNetwork.Empty();
	PreviousNetworkTime = 0;
	CatchupSteps = 0;
	Changes = Wakes = Flushes = Awake = Steps = Matched = 0;
	Convergence = -1;
	Run.Invalidate();
	Phase = TEXT("Idle");
	Status = TEXT("Ready. Measurements require separate network clients.");
	ForceNetUpdate();
}

void ADormancyLab::Start()
{
	ResetExperiment();
	Run = FGuid::NewGuid();
	Random.Initialize(Settings.Seed);
	if (ADormancyExhibit* Exhibit = ADormancyExhibit::Find(GetWorld()))
	{
		Exhibit->RemoveExhibitResources();
	}

	TArray<int32> Order;
	for (int32 Id = 0; Id < Settings.Count; ++Id)
	{
		Order.Add(Id);
	}

	for (int32 Id = Order.Num() - 1; Id > 0; --Id)
	{
		Order.Swap(Id, Random.RandRange(0, Id));
	}

	const int32 TargetCount = FMath::RoundToInt(Settings.Count * Settings.Percent / 100.f);
	for (int32 Id = 0; Id < TargetCount; ++Id)
	{
		DamageTargets.Add(Order[Id]);
	}

	InitialHealth = FMath::Max(100, 20 * (FMath::CeilToInt(ChangeSeconds / Settings.Interval) + 1));
	bAborted = bConnectionChanged = false;
	OutputDir = FPaths::ProjectSavedDir() / TEXT("Dormancy") / Run.ToString(EGuidFormats::Digits);
	IFileManager::Get().MakeDirectory(*OutputDir, true);
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		if (ATechLabPlayerController* PC = Cast<ATechLabPlayerController>(It->Get()); PC && PC->GetNetConnection())
		{
			Participants.Add(PC);
		}
	}

	for (int32 Id = 0; Id < Settings.Count; ++Id)
	{
		const int32 Columns = FMath::CeilToInt(FMath::Sqrt(float(Settings.Count)));
		// The 60 cm grid fits even 8192 nodes within the authored 70 x 63 m benchmark room.
		const FVector Location = GetActorLocation() + FVector(600 + (Id / Columns) * 60, -(Columns - 1) * 30 + (Id % Columns) * 60, 65);
		const FTransform Transform(FQuat::Identity, Location, FVector(.35f));
		ADormancySample* A = GetWorld()->SpawnActorDeferred<ADormancySample>(ADormancySample::StaticClass(), Transform);
		A->InitializeState(Run, Id, InitialHealth,
						   Settings.Scenario == 0	? EResourcePolicy::Awake
						   : Settings.Scenario == 1 ? EResourcePolicy::Flush
													: EResourcePolicy::Burst);
		A->FinishSpawning(Transform);
		Samples.Add(A);
	}

	Awake = Samples.Num();
	PrepareVerification();
	SetPhase(TEXT("Initial"));
	const FString Manifest = FString::Printf(
		TEXT("engine=%s\nrun=%s\nscenario=%d\ndormancy=%d\ncontinuous=%d\nactors=%d\npercent=%d\ninterval=%.3f\nseed=%d\nconnections=%"
			 "d\nnetmode=%d\nwarm=%.3f\nstable=%.3f\nchange=%.3f\nrelevancy=AlwaysRelevant\nnetupdate=30\n"),
		*FEngineVersion::Current().ToString(), *Run.ToString(), Settings.Scenario, Settings.UsesDormancy(), Settings.Scenario == 2,
		Settings.Count, Settings.Percent, Settings.Interval, Settings.Seed, Participants.Num(), int32(GetNetMode()), WarmSeconds,
		StableSeconds, ChangeSeconds);
	FString Schedule = FString::Printf(TEXT("schema=resource-v1\nmax_health=%d\ndamage=20\nsteps=%d\ninterval=%.6f\ncohort="),
									   InitialHealth, FMath::CeilToInt(ChangeSeconds / Settings.Interval), Settings.Interval);
	for (int32 Id : DamageTargets)
	{
		Schedule += FString::FromInt(Id) + TEXT(",");
	}

	FFileHelper::SaveStringToFile(Schedule, *(OutputDir / TEXT("schedule.txt")));
	FFileHelper::SaveStringToFile(Manifest, *(OutputDir / TEXT("manifest.txt")));
}

void ADormancyLab::SetPhase(const FString& Name)
{
	if (Run.IsValid())
	{
		RecordConnections();
	}

	if (Phase != TEXT("Idle"))
	{
		TRACE_END_REGION(*Phase);
	}

	Phase = Name;
	PhaseStart = GetWorld()->GetTimeSeconds();
	TRACE_BEGIN_REGION(*Phase);
	TRACE_BOOKMARK(TEXT("Dormancy Run=%s Phase=%s Steps=%d"), *Run.ToString(), *Name, Steps);
	Record(Name);
	ForceNetUpdate();
}

void ADormancyLab::RecordConnections()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(Dormancy_ConnectionSampling);
	FString Rows;
	ConnectionStats.Empty();
	const double Now = GetWorld()->GetTimeSeconds();
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		if (PC && PC->PlayerState && PC->GetNetConnection())
		{
			UNetConnection* C = PC->GetNetConnection();
			const int32 Id = PC->PlayerState->GetPlayerId();
			const TPair<int64, int64>* Previous = PreviousNetwork.Find(Id);
			if (Previous && Now > PreviousNetworkTime)
			{
				ConnectionStats += FString::Printf(TEXT("Connection %d: %.1f KiB/s | %.1f packets/s\n"), Id,
												   (C->OutTotalBytes - Previous->Key) / (Now - PreviousNetworkTime) / 1024.,
												   (C->OutTotalPackets - Previous->Value) / (Now - PreviousNetworkTime));
			}

			PreviousNetwork.Add(Id, TPair<int64, int64>(C->OutTotalBytes, C->OutTotalPackets));
			Rows += FString::Printf(TEXT("%.6f,%s,%d,%d,%d\n"), double(GetWorld()->GetTimeSeconds()), *Phase,
									PC->PlayerState->GetPlayerId(), C->OutTotalBytes, C->OutTotalPackets);
		}
	}

	PreviousNetworkTime = Now;
	if (!Rows.IsEmpty())
	{
		FFileHelper::SaveStringToFile(Rows, *(OutputDir / TEXT("connections.csv")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,
									  &IFileManager::Get(), FILEWRITE_Append);
	}
}

void ADormancyLab::Record(const FString& Event)
{
	const FString Line = FString::Printf(TEXT("%s,%.6f,%.6f,%d,%d,%d,%d,%d\n"), *Event, FPlatformTime::Seconds(),
										 double(GetWorld()->GetTimeSeconds()), Steps, Changes, Wakes, Flushes, Matched);
	FFileHelper::SaveStringToFile(Line, *(OutputDir / TEXT("events.csv")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,
								  &IFileManager::Get(), FILEWRITE_Append);
	UE_LOG(LogTemp, Display, TEXT("LAB_EVENT Run=%s %s"), *Run.ToString(), *Line);
}

FString ADormancyLab::Digest(const TArray<ADormancySample*>& Actors, FGuid ForRun, int32& Count)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(Dormancy_ValidationDigest);
	TArray<ADormancySample*> Sorted;
	for (ADormancySample* A : Actors)
	{
		if (IsValid(A) && A->GetState().Run == ForRun)
		{
			Sorted.Add(A);
		}
	}

	Sorted.Sort([](const ADormancySample& A, const ADormancySample& B) {
		return A.GetState().Id < B.GetState().Id;
	});

	Count = Sorted.Num();
	FSHA1 Hash;
	int32 Previous = -1;
	for (ADormancySample* A : Sorted)
	{
		if (A->GetState().Id <= Previous)
		{
			return TEXT("DUPLICATE_ID");
		}

		Previous = A->GetState().Id;
		const int32 Data[] = {A->GetState().Id, A->GetState().Version, A->GetState().Health};
		Hash.Update(reinterpret_cast<const uint8*>(Data), sizeof(Data));
	}

	Hash.Final();
	uint8 Bytes[20];
	Hash.GetHash(Bytes);
	return BytesToHex(Bytes, 20);
}

void ADormancyLab::PrepareVerification()
{
	if (Settings.UsesDormancy())
	{
		for (ADormancySample* A : Samples)
		{
			A->SetNetDormancy(DORM_DormantAll);
		}
	}

	Awake = Settings.UsesDormancy() ? 0 : Samples.Num();
	TArray<ADormancySample*> Raw;
	for (ADormancySample* A : Samples)
	{
		Raw.Add(A);
	}

	ExpectedDigest = Digest(Raw, Run, ExpectedCount);
	Matches.Empty();
	Matched = 0;
}

void ADormancyLab::Report(ATechLabPlayerController* PC, FGuid ReportRun, const FString& ReportPhase, int32 Count, const FString& Value)
{
	if (!HasAuthority() || ReportRun != Run || ReportPhase != Phase || Value.Len() != 40 || !PC || !PC->GetNetConnection())
	{
		return;
	}

	if (Phase != TEXT("Initial") && Phase != TEXT("Converge") && Phase != TEXT("Complete"))
	{
		return;
	}

	if (Value == ExpectedDigest && Count == ExpectedCount && !Matches.Contains(PC))
	{
		Matches.Add(PC, GetWorld()->GetTimeSeconds() - PhaseStart);
		Matched = Matches.Num();
		Record(FString::Printf(TEXT("ClientMatch:%d:%.3f"), PC->PlayerState ? PC->PlayerState->GetPlayerId() : -1, Matches[PC]));
		if (Phase == TEXT("Complete") && !Participants.Contains(PC))
		{
			Record(FString::Printf(TEXT("LateJoinMatch:%d"), PC->PlayerState ? PC->PlayerState->GetPlayerId() : -1));
		}
	}
}

void ADormancyLab::ChangeBatch()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(Dormancy_ChangeBatch);
	for (int32 Id : DamageTargets)
	{
		ADormancySample* A = Samples[Id];
		const int32 PreviousFlushes = A->GetFlushCount();
		const int32 PreviousWakes = A->GetWakeCount();
		if (A->ApplyResourceDamage(20))
		{
			++Changes;
		}

		Flushes += A->GetFlushCount() - PreviousFlushes;
		Wakes += A->GetWakeCount() - PreviousWakes;
	}

	Awake = Settings.Scenario == 0 ? Samples.Num() : Settings.Scenario == 2 ? DamageTargets.Num() : 0;
	++Steps;
}

void ADormancyLab::Finish(bool bSuccess)
{
	int32 CurrentParticipants = 0;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		if (ATechLabPlayerController* PC = Cast<ATechLabPlayerController>(It->Get()); PC && PC->GetNetConnection())
		{
			++CurrentParticipants;
			bConnectionChanged |= !Participants.Contains(PC);
		}
	}

	bConnectionChanged |= CurrentParticipants != Participants.Num();
	bSuccess &= !bConnectionChanged;
	for (ADormancySample* A : Samples)
	{
		const FDormancyState& S = A->GetState();
		const int32 Hits = DamageTargets.Contains(S.Id) ? Steps : 0;
		bSuccess &= S.Health == InitialHealth - Hits * 20 && S.Version == Hits;
	}

	Convergence = float(GetWorld()->GetTimeSeconds() - PhaseStart);
	const bool Networked = GetNetMode() != NM_Standalone && Participants.Num() > 0;
	Status = FString::Printf(TEXT("%s | %d/%d clients | convergence upper bound %.3fs | SHA1 over sorted ID/version/health; intermediate "
								  "versions may coalesce.%s"),
							 bSuccess ? TEXT("Final state matched") : TEXT("Verification FAILED"), Matched, Participants.Num(), Convergence,
							 Networked ? TEXT("") : TEXT(" STANDALONE: not network performance evidence."));
	SetPhase(bAborted ? TEXT("Stopped") : bSuccess ? TEXT("Complete") : TEXT("Failed"));
	FFileHelper::SaveStringToFile(
		Status + TEXT("\nDigest=") + ExpectedDigest +
			FString::Printf(TEXT("\nSteps=%d\nChanges=%d\nFlushes=%d\nWakes=%d\nConnectionChanged=%d\nCatchupSteps=%d\n"), Steps, Changes,
							Flushes, Wakes, bConnectionChanged, CatchupSteps),
		*(OutputDir / TEXT("result.txt")));
	SessionResults +=
		FString::Printf(TEXT("Policy %d | Run %s | %s | catch-up steps %d\n"), Settings.Scenario, *Run.ToString(), *Status, CatchupSteps);
	if (bComparing && bSuccess && !bAborted && Settings.Scenario < 2)
	{
		SetPhase(TEXT("Between"));
	}
	else
	{
		bComparing = false;
	}
}

void ADormancyLab::Service()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(Dormancy_Controller);
	const double Now = GetWorld()->GetTimeSeconds();
	if (Now - LastPublish >= .5)
	{
		LastPublish = Now;
		Connections = 0;
		if (Run.IsValid() && IsRunning())
		{
			RecordConnections();
		}

		for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
		{
			if (ATechLabPlayerController* PC = Cast<ATechLabPlayerController>(It->Get()))
			{
				if (PC->GetNetConnection())
				{
					++Connections;
				}

				if (IsRunning() && PC->GetNetConnection() && !Participants.Contains(PC))
				{
					bConnectionChanged = true;
				}
			}
		}

		if (IsRunning() && Connections != Participants.Num())
		{
			bConnectionChanged = true;
		}

		TRACE_COUNTER_SET(LabChanges, Changes);
		TRACE_COUNTER_SET(LabAwake, Awake);
		TRACE_COUNTER_SET(LabFlushes, Flushes);
		TRACE_COUNTER_SET(LabWakes, Wakes);
	}

	if (!IsRunning())
	{
		float ExitDelay = 7;
		FParse::Value(FCommandLine::Get(), TEXT("LabExitDelay="), ExitDelay);
		if ((Phase == TEXT("Complete") || Phase == TEXT("Failed")) && Now - PhaseStart > ExitDelay &&
			FParse::Param(FCommandLine::Get(), TEXT("LabAutoExit")))
		{
			FPlatformMisc::RequestExit(false);
		}

		return;
	}

	const double Elapsed = Now - PhaseStart;
	if (Phase == TEXT("Between"))
	{
		if (Elapsed >= 2.)
		{
			++Settings.Scenario;
			Start();
		}
	}
	else if (Phase == TEXT("Initial"))
	{
		if (Matched == Participants.Num() && Matched > 0)
		{
			SetPhase(TEXT("Warmup"));
		}
		else if (Elapsed > 60)
		{
			Finish(false);
		}
	}
	else if (Phase == TEXT("Warmup") && Elapsed >= WarmSeconds)
	{
		SetPhase(TEXT("Stable"));
	}
	else if (Phase == TEXT("Stable") && Elapsed >= StableSeconds)
	{
		SetPhase(TEXT("Change"));
		NextChange = Now;
	}
	else if (Phase == TEXT("Change"))
	{
		const int32 TargetSteps = FMath::CeilToInt(ChangeSeconds / Settings.Interval);
		int32 Catchup = 0;
		while (Steps < TargetSteps && Now + .00001 >= NextChange && Catchup++ < 8)
		{
			if (Catchup > 1)
			{
				++CatchupSteps;
			}

			ChangeBatch();
			NextChange += Settings.Interval;
		}

		if (Steps >= TargetSteps && Elapsed >= ChangeSeconds)
		{
			PrepareVerification();
			SetPhase(TEXT("Converge"));
		}
	}
	else if (Phase == TEXT("Converge"))
	{
		bool All = !Participants.IsEmpty();
		for (TWeakObjectPtr<ATechLabPlayerController> PC : Participants)
		{
			All &= PC.IsValid() && Matches.Contains(PC);
		}

		if (All)
		{
			Finish(!bConnectionChanged);
		}
		else if (Elapsed > 30)
		{
			Finish(false);
		}
	}
}

const FDormancySettings& ADormancyLab::GetSettings() const
{
	return Settings;
}

FGuid ADormancyLab::GetRun() const
{
	return Run;
}

const FString& ADormancyLab::GetPhase() const
{
	return Phase;
}

const FString& ADormancyLab::GetStatus() const
{
	return Status;
}

int32 ADormancyLab::GetChanges() const
{
	return Changes;
}

int32 ADormancyLab::GetFlushes() const
{
	return Flushes;
}

int32 ADormancyLab::GetWakes() const
{
	return Wakes;
}

int32 ADormancyLab::GetAwake() const
{
	return Awake;
}

int32 ADormancyLab::GetMatched() const
{
	return Matched;
}

int32 ADormancyLab::GetConnections() const
{
	return Connections;
}

float ADormancyLab::GetConvergence() const
{
	return Convergence;
}

int32 ADormancyLab::GetSteps() const
{
	return Steps;
}

const FString& ADormancyLab::GetConnectionStats() const
{
	return ConnectionStats;
}

const FString& ADormancyLab::GetSessionResults() const
{
	return SessionResults;
}
