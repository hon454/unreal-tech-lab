#include "TechLabPortal.h"

#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/PackageName.h"
#include "UObject/ConstructorHelpers.h"
#include "TechLab.h"
#include "Demos/Networking/Dormancy/DormancyLab.h"

ATechLabPortal::ATechLabPortal()
{
	PrimaryActorTick.bCanEverTick = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	Plinth = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Plinth"));
	Plinth->SetupAttachment(RootComponent);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	Plinth->SetStaticMesh(Cube.Object);
	Plinth->SetRelativeLocation(FVector(0, 0, 10));
	Plinth->SetRelativeScale3D(FVector(3.5, 4.4, 0.2));
	Plinth->SetCollisionProfileName(TEXT("BlockAll"));

	Entrance = CreateDefaultSubobject<UBoxComponent>(TEXT("Entrance"));
	Entrance->SetupAttachment(RootComponent);
	Entrance->SetRelativeLocation(FVector(0, 0, 120));
	Entrance->SetBoxExtent(FVector(150, 200, 100));
	Entrance->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Entrance->SetCollisionResponseToAllChannels(ECR_Ignore);
	Entrance->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	Entrance->SetGenerateOverlapEvents(true);
	Entrance->OnComponentBeginOverlap.AddDynamic(this, &ATechLabPortal::EnterExhibit);

	auto MakeLabel = [this](const TCHAR* Name, float Height, float Size) {
		UTextRenderComponent* Label = CreateDefaultSubobject<UTextRenderComponent>(Name);
		Label->SetupAttachment(RootComponent);
		Label->SetRelativeLocation(FVector(105, 0, Height));
		Label->SetRelativeRotation(FRotator(0, 180, 0));
		Label->SetHorizontalAlignment(EHTA_Center);
		Label->SetWorldSize(Size);
		Label->SetTextRenderColor(FColor::White);
		return Label;
	};

	Title = MakeLabel(TEXT("Title"), 285, 30);
	Subtitle = MakeLabel(TEXT("Subtitle"), 230, 16);
	Status = MakeLabel(TEXT("Status"), 155, 22);
}

void ATechLabPortal::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	// Text render components can be stripped when loading the map on a dedicated server.
	if (Title)
	{
		Title->SetText(ExhibitTitle);
		Title->SetTextRenderColor(AccentColor);
	}

	if (Subtitle)
	{
		Subtitle->SetText(ExhibitSubtitle);
	}

	if (Status)
	{
		Status->SetText(DestinationMap.IsNull() ? FText::FromString(TEXT("COMING SOON"))
													: FText::FromString(TEXT("/ HUD > ENTER DORMANCY")));
		Status->SetTextRenderColor(DestinationMap.IsNull() ? FColor(160, 170, 185) : AccentColor);
	}
}

void ATechLabPortal::BeginPlay()
{
	Super::BeginPlay();
	if (!DestinationMap.IsNull())
	{
		Entrance->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	}
}

void ATechLabPortal::EnterExhibit(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComponent,
								  int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	const APawn* Pawn = Cast<APawn>(OtherActor);
	if (!HasAuthority() || bTravelRequested || DestinationMap.IsNull() || !Pawn || !Pawn->IsPlayerControlled())
	{
		return;
	}

	const FString MapPackage = DestinationMap.ToSoftObjectPath().GetLongPackageName();
	// This exhibit uses the HUD for server-authorized travel; portals are visual markers.
	if (ADormancyLab::Find(GetWorld()))
	{
		return;
	}

	if (!FPackageName::DoesPackageExist(MapPackage))
	{
		UE_LOG(LogTechLab, Warning, TEXT("Exhibit destination is unavailable: %s"), *MapPackage);
		return;
	}

	bTravelRequested = true;
	if (GetNetMode() == NM_Standalone)
	{
		UGameplayStatics::OpenLevel(this, FName(*MapPackage));
	}
	else
	{
		// The authority moves the session together; clients never open an independent world.
		bTravelRequested = GetWorld()->ServerTravel(MapPackage);
	}
}
