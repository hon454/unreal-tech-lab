#pragma once

#include "CoreMinimal.h"
#include "DormancySettings.generated.h"

USTRUCT()
struct FDormancySettings
{
	GENERATED_BODY()

	bool IsValid() const;

	bool UsesDormancy() const
	{
		return Scenario != 0;
	}

	UPROPERTY()
	int32 Scenario = 0; // 0 Awake, 1 Flush before damage, 2 Awake during damage interval

	UPROPERTY()
	int32 Count = 1024;

	UPROPERTY()
	int32 Percent = 1;

	UPROPERTY()
	float Interval = 1.f;

	UPROPERTY()
	bool bDormancy = true; // Frequent Changes comparison arm

	UPROPERTY()
	bool bContinuous = false;

	UPROPERTY()
	int32 Seed = 74219;
};
