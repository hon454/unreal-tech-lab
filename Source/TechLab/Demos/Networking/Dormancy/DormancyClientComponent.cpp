#include "DormancyClientComponent.h"
#include "DormancyLab.h"
#include "DormancySample.h"
#include "DormancyExhibit.h"
#include "GameFramework/Pawn.h"
#include "DrawDebugHelpers.h"
#include "Camera/CameraActor.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "SDormancyHUD.h"
#include "TechLabPlayerController.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerState.h"
#include "Misc/CommandLine.h"
#include "TimerManager.h"

UDormancyClientComponent::UDormancyClientComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

ATechLabPlayerController* UDormancyClientComponent::GetController() const
{
	return CastChecked<ATechLabPlayerController>(GetOwner());
}

FDormancySettings& UDormancyClientComponent::GetDraftSettings()
{
	return DraftSettings;
}

const FString& UDormancyClientComponent::GetLiveText() const
{
	return LabLiveText;
}

void UDormancyClientComponent::BeginPlay()
{
	Super::BeginPlay();
	OnWorldLoaded();
}

void UDormancyClientComponent::OnWorldLoaded()
{
	if (!GetController()->IsLocalController())
	{
		return;
	}

	LastVerificationKey.Empty();
	LastVerification = -1;
	GetWorld()->GetTimerManager().SetTimer(LabTimer, this, &UDormancyClientComponent::RefreshLab, .5f, true);
	GetWorld()->GetTimerManager().SetTimer(SelectionTimer, this, &UDormancyClientComponent::RefreshSelection, .1f, true);
	EnsureHUD();
	if (HUD.IsValid())
	{
		HUD->ResetForWorld();
	}
}

void UDormancyClientComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	GetWorld()->GetTimerManager().ClearTimer(LabTimer);
	GetWorld()->GetTimerManager().ClearTimer(SelectionTimer);
	if (HUD.IsValid() && GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(HUD.ToSharedRef());
	}

	HUD.Reset();
	Super::EndPlay(Reason);
}

void UDormancyClientComponent::EnsureHUD()
{
	if (!HUD.IsValid() && GetController()->IsLocalController() && GEngine && GEngine->GameViewport && ADormancyLab::Find(GetWorld()))
	{
		HUD = SNew(SDormancyHUD).Client(this);
		GEngine->GameViewport->AddViewportWidgetContent(HUD.ToSharedRef(), 20);
	}
}

void UDormancyClientComponent::TogglePanel()
{
	EnsureHUD();
	if (HUD.IsValid())
	{
		HUD->TogglePanel();
	}
}

void UDormancyClientComponent::ClosePanel()
{
	if (HUD.IsValid())
	{
		HUD->ClosePanel();
	}
}

void UDormancyClientComponent::ServerLabAction_Implementation(int32 Action, FDormancySettings NewSettings)
{
	ADormancyLab* Lab = ADormancyLab::Find(GetWorld());
	const bool Accepted = Lab && Lab->Request(GetController(), Action, NewSettings);
	UE_LOG(LogTemp, Display, TEXT("LAB_REQUEST Player=%d Action=%d Accepted=%d"),
		   GetController()->PlayerState ? GetController()->PlayerState->GetPlayerId() : -1, Action, Accepted);
	ClientLabResponse(Accepted, Accepted ? TEXT("Server accepted") : TEXT("Server rejected: controller, phase, settings or request rate"));
}

void UDormancyClientComponent::ClientLabResponse_Implementation(bool bAccepted, const FString& Message)
{
	LabMessage = Message;
	UE_LOG(LogTemp, Display, TEXT("LAB_RESPONSE Accepted=%d %s"), bAccepted, *Message);
}

void UDormancyClientComponent::ServerLabReport_Implementation(FGuid ReportRun, const FString& ReportPhase, int32 Count,
															  const FString& Digest)
{
	if (ADormancyLab* Lab = ADormancyLab::Find(GetWorld()))
	{
		Lab->Report(GetController(), ReportRun, ReportPhase, Count, Digest);
	}
}

void UDormancyClientComponent::RequestLab(int32 Action)
{
	if (const ADormancyLab* Lab = ADormancyLab::Find(GetWorld()); Lab && (Action == 1 || Action == 7))
	{
		DraftSettings = Lab->GetSettings();
	}

	ServerLabAction(Action, DraftSettings);
}

void UDormancyClientComponent::RefreshLab()
{
	ADormancyLab* Lab = ADormancyLab::Find(GetWorld());
	if (!Lab)
	{
		return;
	}

	EnsureHUD();
	UpdateBenchmarkView();

	const bool AutomationDriver = FParse::Param(FCommandLine::Get(), TEXT("LabDrive"));

	LabLiveText =
		FString::Printf(TEXT("%s | %s | clients %d\nRun %s\nActors %d | configured Awake %d / Dormant %d\nChanges %d | flush %d | wake %d "
							 "| steps %d\nMatches %d | convergence %.3f s\n%s\n%s"),
						TEXT("ALL CLIENTS CAN CONTROL"), *Lab->GetPhase(), Lab->GetConnections(), *Lab->GetRun().ToString(),
						Lab->GetRun().IsValid() ? Lab->GetSettings().Count : 0, Lab->GetAwake(),
						Lab->GetRun().IsValid() ? Lab->GetSettings().Count - Lab->GetAwake() : 0, Lab->GetChanges(), Lab->GetFlushes(),
						Lab->GetWakes(), Lab->GetSteps(), Lab->GetMatched(), Lab->GetConvergence(), *Lab->GetStatus(), *LabMessage);
	LabLiveText =
		FString::Printf(TEXT("%s | %s\nF: attack | /: panel | Resources are replicated independently\n"),
						GetOwner()->HasAuthority() ? TEXT("SERVER VIEW (Listen)") : TEXT("CLIENT VIEW"),
						GetWorld()->GetNetMode() == NM_Standalone ? TEXT("Standalone: not network evidence") : TEXT("Network session")) +
		LabLiveText;
	LabLiveText += TEXT("\n") + Lab->GetConnectionStats();
	if (Lab->GetRun().IsValid() &&
		(Lab->GetPhase() == TEXT("Initial") || Lab->GetPhase() == TEXT("Converge") || Lab->GetPhase() == TEXT("Complete")))
	{
		const double Now = GetWorld()->GetTimeSeconds();
		if (Now - LastVerification >= 1.)
		{
			LastVerification = Now;
			TArray<ADormancySample*> Samples;
			for (TActorIterator<ADormancySample> It(GetWorld()); It; ++It)
			{
				if (It->ExhibitPair >= 0)
				{
					// Do not acknowledge benchmark readiness while exhibit replicas remain.
					return;
				}

				Samples.Add(*It);
			}

			int32 Count = 0;
			FString Digest = ADormancyLab::Digest(Samples, Lab->GetRun(), Count);
			const FString Key = Lab->GetRun().ToString() + Lab->GetPhase() + Digest + FString::FromInt(Count);
			if (Key != LastVerificationKey)
			{
				ServerLabReport(Lab->GetRun(), Lab->GetPhase(), Count, Digest);
				LastVerificationKey = Key;
			}
		}
	}

	// Same owned RPC as the HUD. No keyboard/mouse simulation and no UI-test claim.
	if (FParse::Param(FCommandLine::Get(), TEXT("LabAuto")))
	{
		int32 RequiredClients = 2;
		FParse::Value(FCommandLine::Get(), TEXT("LabClients="), RequiredClients);
		const bool ControlTest = FParse::Param(FCommandLine::Get(), TEXT("LabControlTest"));
		if (AutomationDriver && ControlTest && Lab->GetConnections() >= RequiredClients)
		{
			if (ControlTestStage == 0)
			{
				FDormancySettings Invalid = DraftSettings;
				Invalid.Count = 9000;
				ServerLabAction(0, Invalid);
				++ControlTestStage;
			}
			else if (ControlTestStage == 1)
			{
				DraftSettings.Count = 256;
				DraftSettings.Scenario = 1;
				RequestLab(5);
				++ControlTestStage;
			}
			else if (ControlTestStage == 2)
			{
				RequestLab(0);
				++ControlTestStage;
			}
			else if (ControlTestStage == 3 && Lab->GetPhase() == TEXT("Change"))
			{
				RequestLab(5);
				++ControlTestStage;
			}
			else if (ControlTestStage == 4)
			{
				RequestLab(1);
				++ControlTestStage;
			}
			else if (ControlTestStage == 5 && Lab->GetPhase() == TEXT("Stopped"))
			{
				// Editing after a stopped run must clear the field before publishing new settings.
				RequestLab(5);
				++ControlTestStage;
			}
			else if (ControlTestStage == 6 && Lab->GetPhase() == TEXT("Idle"))
			{
				RequestLab(2);
				++ControlTestStage;
			}
			else if (ControlTestStage == 7 && Lab->GetPhase() == TEXT("Idle"))
			{
				RequestLab(0);
				++ControlTestStage;
			}
		}

		if (AutomationDriver && !ControlTest && !bAutomationSent && Lab->GetPhase() == TEXT("Idle") &&
			Lab->GetConnections() >= RequiredClients)
		{
			FParse::Value(FCommandLine::Get(), TEXT("LabCount="), DraftSettings.Count);
			FParse::Value(FCommandLine::Get(), TEXT("LabScenario="), DraftSettings.Scenario);
			FParse::Value(FCommandLine::Get(), TEXT("LabPercent="), DraftSettings.Percent);
			FParse::Value(FCommandLine::Get(), TEXT("LabInterval="), DraftSettings.Interval);
			DraftSettings.bDormancy = !FParse::Param(FCommandLine::Get(), TEXT("LabAwake"));
			DraftSettings.bContinuous = FParse::Param(FCommandLine::Get(), TEXT("LabContinuous"));
			bAutomationSent = true;
			RequestLab(FParse::Param(FCommandLine::Get(), TEXT("LabCompare")) ? 6 : 0);
		}

		if (Lab->GetPhase() == TEXT("Complete") || Lab->GetPhase() == TEXT("Failed"))
		{
			if (AutomationDoneAt < 0)
			{
				AutomationDoneAt = GetWorld()->GetTimeSeconds();
				UE_LOG(LogTemp, Display, TEXT("LAB_AUTOMATION_FINISHED %s %s"), *Lab->GetPhase(), *Lab->GetStatus());
			}

			float ExitDelay = 3;
			FParse::Value(FCommandLine::Get(), TEXT("LabExitDelay="), ExitDelay);
			if (GetWorld()->GetTimeSeconds() - AutomationDoneAt > ExitDelay)
			{
				FPlatformMisc::RequestExit(false);
			}
		}
	}
}

void UDormancyClientComponent::RequestAttack()
{
	ADormancyLab* Lab = ADormancyLab::Find(GetWorld());
	const double Now = GetWorld()->GetTimeSeconds();
	if (!Lab || !Lab->CanControl(GetController()) || Lab->IsRunning() || (HUD.IsValid() && HUD->IsExpanded()) || Now - LastLocalAttack < .5)
	{
		return;
	}

	LastLocalAttack = Now;
	ServerAttack();
}

void UDormancyClientComponent::ServerAttack_Implementation()
{
	ADormancyLab* Lab = ADormancyLab::Find(GetWorld());
	if (!Lab || Lab->IsRunning() || !Lab->CanControl(GetController()))
	{
		return;
	}

	if (ADormancyExhibit* Exhibit = ADormancyExhibit::Find(GetWorld()))
	{
		FVector Start = FVector::ZeroVector;
		FVector End = FVector::ZeroVector;
		FVector Hit = FVector::ZeroVector;
		const bool Applied = Exhibit->TryAttack(GetController(), Start, End, Hit);
		if (!Start.Equals(End))
		{
			for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
			{
				if (UDormancyClientComponent* Client = It->Get()->FindComponentByClass<UDormancyClientComponent>())
				{
					Client->ClientAttackResult(Start, End, Hit, Applied);
				}
			}
		}
	}
}

void UDormancyClientComponent::ClientAttackResult_Implementation(FVector Start, FVector End, FVector Hit, bool bApplied)
{
	DrawDebugSphere(GetWorld(), End, 150.f, 24, bApplied ? FColor::Green : FColor::Red, false, .5f);
	DrawDebugSphere(GetWorld(), Hit, 15.f, 12, FColor::Yellow, false, .5f);
}

void UDormancyClientComponent::RestoreSelectedPair()
{
	if (SelectedNodes[0].IsValid())
	{
		ServerRestorePair(SelectedNodes[0]->ExhibitPair);
	}
}

void UDormancyClientComponent::ServerRestorePair_Implementation(int32 Pair)
{
	const double Now = GetWorld()->GetTimeSeconds();
	if (Now - LastRestore < .5)
	{
		return;
	}

	LastRestore = Now;
	if (ADormancyExhibit* Exhibit = ADormancyExhibit::Find(GetWorld()))
	{
		Exhibit->RestorePair(GetController(), Pair);
	}
}

void UDormancyClientComponent::RefreshSelection()
{
	SelectedNodes[0].Reset();
	SelectedNodes[1].Reset();
	ADormancyLab* Lab = ADormancyLab::Find(GetWorld());
	APawn* Pawn = GetController()->GetPawn();
	if (!GEngine || !GEngine->GameViewport || !Lab || Lab->IsRunning() || !Pawn)
	{
		return;
	}

	float Best = 500.f * 500.f;
	int32 Pair = -1;
	for (TActorIterator<ADormancySample> It(GetWorld()); It; ++It)
	{
		const FVector Delta = It->GetActorLocation() - Pawn->GetActorLocation();
		if (It->ExhibitPair >= 0 && Delta.SizeSquared() < Best &&
			FVector::DotProduct(Pawn->GetActorForwardVector(), Delta.GetSafeNormal()) > -.1f)
		{
			Best = Delta.SizeSquared();
			Pair = It->ExhibitPair;
		}
	}

	if (Pair >= 0)
	{
		for (TActorIterator<ADormancySample> It(GetWorld()); It; ++It)
		{
			if (It->ExhibitPair == Pair)
			{
				SelectedNodes[It->bReference ? 0 : 1] = *It;
			}
		}
	}
}

FString UDormancyClientComponent::GetResourceText(int32 Index) const
{
	const ADormancySample* Node = SelectedNodes[Index].Get();
	if (!Node)
	{
		return FString();
	}

	return FString::Printf(TEXT("%s | HP %d/%d | v%d"), Index == 0 ? TEXT("Awake reference") : TEXT("Experiment"), Node->GetState().Health,
						   Node->GetState().MaxHealth, Node->GetState().Version);
}

float UDormancyClientComponent::GetResourceFraction(int32 Index) const
{
	const ADormancySample* Node = SelectedNodes[Index].Get();
	return Node ? float(Node->GetState().Health) / FMath::Max(1, Node->GetState().MaxHealth) : 0.f;
}

FVector2D UDormancyClientComponent::GetResourcePosition(int32 Index) const
{
	const ADormancySample* Node = SelectedNodes[Index].Get();
	FVector2D Screen(-10000.f, -10000.f);
	if (Node && GetController()->ProjectWorldLocationToScreen(Node->GetActorLocation() + FVector(0, 0, 140), Screen, true))
	{
		Screen /= FMath::Max(.1f, UWidgetLayoutLibrary::GetViewportScale(GetController()));
		return Screen - FVector2D(120, 0);
	}

	return FVector2D(-10000.f, -10000.f);
}

void UDormancyClientComponent::UpdateBenchmarkView()
{
	ADormancyLab* Lab = ADormancyLab::Find(GetWorld());
	const bool Wanted = Lab && Lab->IsRunning();
	if (Wanted == bBenchmarkView)
	{
		return;
	}

	if (Wanted)
	{
		for (TActorIterator<ACameraActor> It(GetWorld()); It; ++It)
		{
			if (It->ActorHasTag(TEXT("ResourceBenchmarkCamera")))
			{
				GetController()->SetViewTarget(*It);
				GetController()->SetIgnoreMoveInput(true);
				GetController()->SetIgnoreLookInput(true);
				bBenchmarkView = true;
				return;
			}
		}
	}
	else
	{
		GetController()->SetViewTarget(GetController()->GetPawn());
		GetController()->ResetIgnoreMoveInput();
		GetController()->ResetIgnoreLookInput();
		if (HUD.IsValid() && HUD->IsExpanded())
		{
			GetController()->SetIgnoreMoveInput(true);
			GetController()->SetIgnoreLookInput(true);
		}

		bBenchmarkView = false;
	}
}
