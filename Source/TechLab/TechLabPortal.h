#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TechLabPortal.generated.h"

class UBoxComponent;
class UStaticMeshComponent;
class UTextRenderComponent;

/** A hub exhibit that becomes a travel entrance when a destination is assigned. */
UCLASS()
class TECHLAB_API ATechLabPortal : public AActor
{
	GENERATED_BODY()

public:
	ATechLabPortal();
	virtual void OnConstruction(const FTransform& Transform) override;

	UPROPERTY(EditAnywhere, Category = "TechLab")
	FText ExhibitTitle;

	UPROPERTY(EditAnywhere, Category = "TechLab")
	FText ExhibitSubtitle;

	UPROPERTY(EditAnywhere, Category = "TechLab")
	FColor AccentColor = FColor(70, 210, 230);

	/** Leave empty for an inactive, coming-soon exhibit. Include assigned maps in packaging settings. */
	UPROPERTY(EditAnywhere, Category = "TechLab")
	TSoftObjectPtr<UWorld> DestinationMap;

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY(VisibleAnywhere, Category = "TechLab")
	TObjectPtr<UStaticMeshComponent> Plinth;

	UPROPERTY(VisibleAnywhere, Category = "TechLab")
	TObjectPtr<UBoxComponent> Entrance;

	UPROPERTY(VisibleAnywhere, Category = "TechLab")
	TObjectPtr<UTextRenderComponent> Title;

	UPROPERTY(VisibleAnywhere, Category = "TechLab")
	TObjectPtr<UTextRenderComponent> Subtitle;

	UPROPERTY(VisibleAnywhere, Category = "TechLab")
	TObjectPtr<UTextRenderComponent> Status;

	bool bTravelRequested = false;

	UFUNCTION()
	void EnterExhibit(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComponent, int32 OtherBodyIndex, bool bFromSweep,
		const FHitResult& SweepResult);
};
