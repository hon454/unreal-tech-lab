#include "DormancySettings.h"

bool FDormancySettings::IsValid() const
{
	return Scenario >= 0 && Scenario <= 2 && Count >= 1 && Count <= 8192 && Percent >= 0 && Percent <= 100 && FMath::IsFinite(Interval) &&
		   Interval >= .05f && Interval <= 10.f;
}
