#pragma once

#include "xenbono.h"

class AFortGameSessionDedicatedAthena : public AActor
{
public:
	static uint8 GetSquadIdForCurrentPlayerHook(AFortGameSessionDedicatedAthena* GameSessionDedicated, void* UniqueId);
};