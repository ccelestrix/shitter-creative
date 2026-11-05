#include "GameMode.h"

#include "xenbono.h"

void AGameMode::RestartGame()
{
	static auto RestartGameFn = FindObject<UFunction>("/Script/Engine.GameMode.RestartGame");
	this->ProcessEvent(RestartGameFn);
}