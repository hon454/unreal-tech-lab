#include "DormancySample.h"
#include "Components/StaticMeshComponent.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

ADormancySample::ADormancySample()
{
	bReplicates = true;
	bAlwaysRelevant = true;
	PrimaryActorTick.bCanEverTick = false;
	NetDormancy = DORM_Awake;
	SetNetUpdateFrequency(30);
	SetMinNetUpdateFrequency(30);
	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Display"));
	RootComponent = Mesh;
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Game/LevelPrototyping/Meshes/SM_ChamferCube"));
	Mesh->SetStaticMesh(Cube.Object);
	Mesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Mesh->SetCollisionResponseToAllChannels(ECR_Ignore);
	Mesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	Mesh->SetGenerateOverlapEvents(false);
	Mesh->SetCastShadow(false);
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Material(TEXT("/Game/Maps/Networking/M_DormancyState"));
	if (Material.Succeeded())
	{
		Mesh->SetMaterial(0, Material.Object);
	}
}

void ADormancySample::BeginPlay()
{
	Super::BeginPlay();
	UpdateResourceAppearance();
}

void ADormancySample::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ADormancySample, State);
	DOREPLIFETIME(ADormancySample, Policy);
}

const FDormancyState& ADormancySample::GetState() const
{
	return State;
}

void ADormancySample::InitializeState(FGuid Run, int32 Id, int32 MaxHealth, EResourcePolicy InPolicy)
{
	check(HasAuthority());
	State.Run = Run;
	State.Id = Id;
	State.MaxHealth = MaxHealth;
	State.Health = MaxHealth;
	Policy = InPolicy;
}

void ADormancySample::PrepareResourceChange()
{
	if (Policy == EResourcePolicy::Flush || Policy == EResourcePolicy::Initial)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(Resource_Flush);
		FlushNetDormancy();
		++FlushCount;
	}
	else if (Policy == EResourcePolicy::Burst && NetDormancy != DORM_Awake)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(Resource_Wake);
		SetNetDormancy(DORM_Awake);
		++WakeCount;
	}

	// MissingUpdate deliberately omits wake/flush: labelled error exhibit only.
}

bool ADormancySample::ApplyResourceDamage(int32 Damage)
{
	check(HasAuthority());
	if (Damage <= 0 || State.Health <= 0)
	{
		return false;
	}

	PrepareResourceChange();
	State.Health = FMath::Max(0, State.Health - Damage);
	++State.Version;
	UpdateResourceAppearance();
	return true;
}

void ADormancySample::RestoreResource()
{
	check(HasAuthority());
	// Explicit recovery repairs the error exhibit too. Never return to DORM_Initial.
	if (NetDormancy > DORM_Awake)
	{
		FlushNetDormancy();
		++FlushCount;
	}

	State.Health = State.MaxHealth;
	++State.Version;
	UpdateResourceAppearance();
}

void ADormancySample::EndResourceBurst()
{
	check(HasAuthority());
	if (Policy == EResourcePolicy::Burst)
	{
		SetNetDormancy(DORM_DormantAll);
	}
}

int32 ADormancySample::GetFlushCount() const
{
	return FlushCount;
}

int32 ADormancySample::GetWakeCount() const
{
	return WakeCount;
}

void ADormancySample::OnRep_State()
{
	UpdateResourceAppearance();
}

void ADormancySample::UpdateResourceAppearance()
{
	if (GetNetMode() != NM_DedicatedServer)
	{
		Mesh->SetCustomPrimitiveDataFloat(0, State.MaxHealth > 0 ? 1.f - float(State.Health) / State.MaxHealth : 1.f);
	}
}

