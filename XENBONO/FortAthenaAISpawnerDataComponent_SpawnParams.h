#pragma once

#include "xenbono.h"

class UFortAthenaAISpawnerDataComponent_SpawnParams : public UObject
{
public:
	UClass*& GetPawnClass()
	{
		static auto PawnClassOffset = GetOffset("PawnClass");
		return Get<UClass*>(PawnClassOffset);
	}
};