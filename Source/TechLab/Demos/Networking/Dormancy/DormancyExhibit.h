#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DormancyExhibit.generated.h"

class ADormancySample;
class ATechLabPlayerController;

/** Pair coordination only. Health never travels through this actor. */
UCLASS()
class TECHLAB_API ADormancyExhibit : public AActor
{
	GENERATED_BODY()

public:
	ADormancyExhibit();

	bool TryAttack(ATechLabPlayerController* Controller, FVector& Start, FVector& End, FVector& HitPoint);
	bool RestorePair(ATechLabPlayerController* Controller, int32 Pair);
	void RemoveExhibitResources();
	static ADormancyExhibit* Find(const UWorld* World);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

private:
	bool PairReady(int32 Pair) const;
	void ServiceBursts();

	UPROPERTY()
	TArray<TObjectPtr<ADormancySample>> Nodes;

	FTimerHandle Timer;
	TMap<int32, double> BurstDeadlines;
	TMap<TWeakObjectPtr<ATechLabPlayerController>, double> LastAttacks;
	bool bRemoved = false;
};
