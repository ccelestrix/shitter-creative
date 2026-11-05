#include "FortGameModeZone.h"

#include "KismetStringLibrary.h"

#include "xenbono.h"

UClass* AFortGameModeZone::StaticClass()
{
	static auto Class = FindObject<UClass>(L"/Script/FortniteGame.FortGameModeZone");
	return Class;
}