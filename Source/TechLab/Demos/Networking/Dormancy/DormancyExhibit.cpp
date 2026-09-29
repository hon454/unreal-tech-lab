#include "DormancyExhibit.h"
#include "DormancySample.h"
#include "DormancyLab.h"
#include "TechLabPlayerController.h"
#include "EngineUtils.h"
#include "Engine/NetDriver.h"
#include "Engine/NetworkObjectList.h"
#include "Engine/OverlapResult.h"
#include "GameFramework/Pawn.h"
#include "TimerManager.h"

ADormancyExhibit::ADormancyExhibit()
{
	PrimaryActorTick.bCanEverTick = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void ADormancyExhibit::BeginPlay()
{
	Super::BeginPlay();
	if (HasAuthority())
	{
		for (TActorIterator<ADormancySample> It(GetWorld()); It; ++It)
		{
			if (It->ExhibitPair >= 0)
			{
				Nodes.Add(*It);
			}
		}

		GetWorldTimerManager().SetTimer(Timer, this, &ADormancyExhibit::ServiceBursts, .1f, true);
	}
}

void ADormancyExhibit::EndPlay(const EEndPlayReason::Type Reason)
{
	GetWorldTimerManager().ClearTimer(Timer);
	Super::EndPlay(Reason);
}

ADormancyExhibit* ADormancyExhibit::Find(const UWorld* World)
{
	for (TActorIterator<ADormancyExhibit> It(World); It; ++It)
	{
		return *It;
	}

	return nullptr;
}

bool ADormancyExhibit::PairReady(int32 Pair) const
{
	const UNetDriver* Driver = GetNetDriver();
	for (ADormancySample* Node : Nodes)
	{
		if (IsValid(Node) && Node->ExhibitPair == Pair && Node->Policy == EResourcePolicy::MissingUpdate && Driver)
		{
			const TSharedPtr<FNetworkObjectInfo> Info = Driver->GetNetworkObjectList().Find(Node);
			if (!Info.IsValid() || Info->DormantConnections.Num() < Driver->ClientConnections.Num())
			{
				return false;
			}
		}
	}

	return true;
}

bool ADormancyExhibit::TryAttack(ATechLabPlayerController* Controller, FVector& Start, FVector& End, FVector& HitPoint)
{
	ADormancyLab* Lab = ADormancyLab::Find(GetWorld());
	APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	if (!HasAuthority() || bRemoved || !Lab || Lab->IsRunning() || !Lab->CanControl(Controller) || !Pawn)
	{
		return false;
	}

	const double Now = GetWorld()->GetTimeSeconds();
	const double* Last = LastAttacks.Find(Controller);
	if (Last && Now - *Last < .5)
	{
		return false;
	}

	LastAttacks.Add(Controller, Now);
	Start = Pawn->GetActorLocation();
	End = Start + Pawn->GetActorForwardVector() * 160.f - FVector(0, 0, 50.f);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(ResourceAttack), false, Pawn);
	TArray<FOverlapResult> Overlaps;
	GetWorld()->OverlapMultiByChannel(Overlaps, End, FQuat::Identity, ECC_Visibility, FCollisionShape::MakeSphere(150.f), Params);
	ADormancySample* Target = nullptr;
	float Nearest = TNumericLimits<float>::Max();
	HitPoint = End;
	for (const FOverlapResult& Overlap : Overlaps)
	{
		ADormancySample* Candidate = Cast<ADormancySample>(Overlap.GetActor());
		if (!Candidate || Candidate->ExhibitPair < 0)
		{
			continue;
		}

		FVector Center;
		FVector Extent;
		Candidate->GetActorBounds(false, Center, Extent);
		const float Distance = FVector::DistSquared(Start, Center);
		FHitResult Obstacle;
		GetWorld()->LineTraceSingleByChannel(Obstacle, Start, Center, ECC_Visibility, Params);
		if (Distance < Nearest && (!Obstacle.bBlockingHit || Obstacle.GetActor() == Candidate))
		{
			Nearest = Distance;
			Target = Candidate;
			HitPoint = Center;
		}
	}

	if (!Target || Target->ExhibitPair < 0 || !PairReady(Target->ExhibitPair))
	{
		return false;
	}

	bool Applied = false;
	for (ADormancySample* Node : Nodes)
	{
		if (IsValid(Node) && Node->ExhibitPair == Target->ExhibitPair)
		{
			Applied |= Node->ApplyResourceDamage(20);
		}
	}

	if (Applied)
	{
		BurstDeadlines.Add(Target->ExhibitPair, Now + 2.);
		UE_LOG(LogTemp, Display, TEXT("RESOURCE_ATTACK Pair=%d Damage=20"), Target->ExhibitPair);
	}

	return Applied;
}

bool ADormancyExhibit::RestorePair(ATechLabPlayerController* Controller, int32 Pair)
{
	ADormancyLab* Lab = ADormancyLab::Find(GetWorld());
	if (!HasAuthority() || bRemoved || Pair < 0 || Pair > 3 || !Lab || Lab->IsRunning() || !Lab->CanControl(Controller))
	{
		return false;
	}

	for (ADormancySample* Node : Nodes)
	{
		if (IsValid(Node) && Node->ExhibitPair == Pair)
		{
			Node->RestoreResource();
			Node->EndResourceBurst();
		}
	}

	return true;
}

void ADormancyExhibit::ServiceBursts()
{
	for (TMap<int32, double>::TIterator It = BurstDeadlines.CreateIterator(); It; ++It)
	{
		if (GetWorld()->GetTimeSeconds() >= It.Value())
		{
			for (ADormancySample* Node : Nodes)
			{
				if (IsValid(Node) && Node->ExhibitPair == It.Key())
				{
					Node->EndResourceBurst();
				}
			}

			It.RemoveCurrent();
		}
	}
}

void ADormancyExhibit::RemoveExhibitResources()
{
	check(HasAuthority());
	bRemoved = true;
	GetWorldTimerManager().ClearTimer(Timer);
	for (ADormancySample* Node : Nodes)
	{
		if (IsValid(Node))
		{
			Node->Destroy();
		}
	}

	Nodes.Empty();
	BurstDeadlines.Empty();
}
