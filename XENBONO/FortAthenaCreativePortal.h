#pragma once

#include "BuildingActor.h"
#include "FortVolume.h"
#include "Stack.h"

struct FCreativeLoadedLinkData
{
	
};

class AFortAthenaCreativePortal : public ABuildingActor // ABuildingGameplayActor
{
public:
	static inline void (*TeleportPlayerToLinkedVolumeOriginal)(UObject* Context, FFrame& Stack, void* Ret);
	static inline void (*TeleportPlayerOriginal)(UObject* Context, FFrame& Stack, void* Ret);

	FCreativeLoadedLinkData* GetIslandInfo()
	{
		static auto CreativeLoadedLinkDataStruct = FindObject<UStruct>("/Script/FortniteGame.CreativeLoadedLinkData");

		if (!CreativeLoadedLinkDataStruct)
			return nullptr;

		static auto IslandInfoOffset = GetOffset("IslandInfo");
		return GetPtr<FCreativeLoadedLinkData>(IslandInfoOffset);
	}

	void* GetOwningPlayer()
	{
		static auto OwningPlayerOffset = GetOffset("OwningPlayer", false);

		if (OwningPlayerOffset == -1)
			return nullptr;

		return GetPtr<void>(OwningPlayerOffset);
	}

	bool& GetPortalOpen()
	{
		static auto bPortalOpenOffset = GetOffset("bPortalOpen");
		return Get<bool>(bPortalOpenOffset);
	}

	bool& GetUserInitiatedLoad()
	{
		static auto bUserInitiatedLoadOffset = GetOffset("bUserInitiatedLoad");
		return Get<bool>(bUserInitiatedLoadOffset);
	}

	bool& GetInErrorState()
	{
		static auto bInErrorStateOffset = GetOffset("bInErrorState");
		return Get<bool>(bInErrorStateOffset);
	}

	AFortVolume*& GetLinkedVolume()
	{
		static auto LinkedVolumeOffset = GetOffset("LinkedVolume");
		return Get<AFortVolume*>(LinkedVolumeOffset);
	}

	FString& GetCreatorName()
	{
		auto IslandInfo = GetIslandInfo();

		if (!IslandInfo)
		{
			static FString NoCreator; // never dereference null; callers just see an empty name
			return NoCreator;
		}

		static auto CreatorNameOffset = FindOffsetStruct("/Script/FortniteGame.CreativeLoadedLinkData", "CreatorName");
		return *(FString*)(__int64(IslandInfo) + CreatorNameOffset);
	}

	static void TeleportPlayerToLinkedVolumeHook(UObject* Context, FFrame& Stack, void* Ret);
	static void TeleportPlayerHook(UObject* Context, FFrame& Stack, void* Ret);
	// hook TeleportPlayer ?? but do what with it
};

// What making an island needs on 12.41, taken from how the Xenon game server does it (checked against the 12.41 SDK).
class AFortPlayerControllerAthena;
class UFortPlaysetItemDefinition;

namespace CreativeIsland
{
	/** Calls a function with no parameters (an OnRep_ normally) so clients see the change; logs when it does not exist on this version. */
	void CallOnRep(UObject* Object, const std::string& FunctionName);
	/** The plot a new island starts as ("Temperate Island", the first one in the Select Type screen). */
	UObject* FindPlot();
	/** The playset (island layout) a plot starts from. */
	UFortPlaysetItemDefinition* GetPlotPlayset(UObject* Plot);
	/** Gives the player the creative phone if they do not have it yet. */
	void GiveCreativePhone(AFortPlayerControllerAthena* Controller);
	/** Spawns the island's settings machine (what My Island > Settings / UI and Start Game read), if the island has none yet. */
	void SetupSettingsMachine(class AFortVolume* Volume);
	/** Logs every creative call the client makes to the server. */
	void HookControllerFunctions();
}