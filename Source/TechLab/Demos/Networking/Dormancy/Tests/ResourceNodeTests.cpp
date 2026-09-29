#include "Misc/AutomationTest.h"
#include "Engine/World.h"
#include "Demos/Networking/Dormancy/DormancySample.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FResourceDamageTest, "TechLab.Dormancy.ResourceDamage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FResourceDamageTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	for (EResourcePolicy Policy : {EResourcePolicy::Awake, EResourcePolicy::MissingUpdate, EResourcePolicy::Flush, EResourcePolicy::Burst, EResourcePolicy::Initial})
	{
		ADormancySample* Node = World->SpawnActor<ADormancySample>();
		Node->InitializeState(FGuid::NewGuid(), 0, 100, Policy);
		Node->SetNetDormancy(Policy == EResourcePolicy::Awake ? DORM_Awake : Policy == EResourcePolicy::Initial ? DORM_Initial : DORM_DormantAll);
		TestFalse(TEXT("Non-positive damage rejected"), Node->ApplyResourceDamage(0));
		for (int32 Hit = 0; Hit < 5; ++Hit)
		{
			TestTrue(TEXT("Valid hit changes state"), Node->ApplyResourceDamage(20));
		}

		TestEqual(TEXT("Five hits deplete"), Node->GetState().Health, 0);
		TestEqual(TEXT("Version increments for mutations"), Node->GetState().Version, 5);
		TestFalse(TEXT("Depleted hit is not a mutation"), Node->ApplyResourceDamage(20));
		TestEqual(TEXT("Expected explicit flush count"), Node->GetFlushCount(), Policy == EResourcePolicy::Flush || Policy == EResourcePolicy::Initial ? 5 : 0);
		TestEqual(TEXT("Burst wakes only once"), Node->GetWakeCount(), Policy == EResourcePolicy::Burst ? 1 : 0);
		Node->EndResourceBurst();
		if (Policy == EResourcePolicy::Burst || Policy == EResourcePolicy::Initial)
		{
			TestEqual(TEXT("Returns to All, never Initial"), Node->NetDormancy.GetValue(), DORM_DormantAll);
		}

		Node->RestoreResource();
		TestEqual(TEXT("Explicit restoration"), Node->GetState().Health, 100);
		TestEqual(TEXT("Restoration is versioned"), Node->GetState().Version, 6);
		Node->Destroy();
	}

	World->DestroyWorld(false);
	return true;
}

#endif
