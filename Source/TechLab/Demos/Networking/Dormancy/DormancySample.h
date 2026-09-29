#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DormancySample.generated.h"

class UStaticMeshComponent;

UENUM()
enum class EResourcePolicy : uint8
{
	Awake,
	MissingUpdate,
	Flush,
	Burst,
	Initial
};

USTRUCT()
struct FDormancyState
{
	GENERATED_BODY()

	UPROPERTY()
	FGuid Run;

	UPROPERTY()
	int32 Id = -1;

	UPROPERTY()
	int32 Version = 0;

	UPROPERTY()
	int32 Health = 100;

	UPROPERTY()
	int32 MaxHealth = 100;
};

/** Resource state shared by the exhibit and benchmark. */
UCLASS()
class TECHLAB_API ADormancySample : public AActor
{
	GENERATED_BODY()

public:
	ADormancySample();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;

	const FDormancyState& GetState() const;
	void InitializeState(FGuid Run, int32 Id, int32 MaxHealth, EResourcePolicy InPolicy);
	bool ApplyResourceDamage(int32 Damage);
	void RestoreResource();
	void EndResourceBurst();
	int32 GetFlushCount() const;
	int32 GetWakeCount() const;

	UPROPERTY(EditAnywhere, Category = "Resource")
	int32 ExhibitPair = -1;

	UPROPERTY(EditAnywhere, Category = "Resource")
	bool bReference = false;

	UPROPERTY(EditAnywhere, Replicated, Category = "Resource")
	EResourcePolicy Policy = EResourcePolicy::Awake;

protected:
	virtual void BeginPlay() override;

private:
	void PrepareResourceChange();
	void UpdateResourceAppearance();

	UFUNCTION()
	void OnRep_State();

	UPROPERTY(ReplicatedUsing = OnRep_State)
	FDormancyState State;

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Mesh;

	int32 FlushCount = 0;
	int32 WakeCount = 0;
};
