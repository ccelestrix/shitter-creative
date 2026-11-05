#pragma once

#include "xenbono.h"

#include "Actor.h"
#include "Stack.h"

class AFortMinigame : public AActor // AInfo
{
public:
	static inline void (*ClearPlayerInventoryOriginal)(UObject* Context, FFrame& Stack, void* Ret);

	static void ClearPlayerInventoryHook(UObject* Context, FFrame& Stack, void* Ret);
};