#pragma once

#include "FortWeaponItemDefinition.h"
#include "xenbono.h"

class UFortWeaponMeleeItemDefinition : public UFortWeaponItemDefinition
{
public:
	static UClass* StaticClass()
	{
		static auto Class = FindObject<UClass>("/Script/FortniteGame.FortWeaponMeleeItemDefinition");
		return Class;
	}
};