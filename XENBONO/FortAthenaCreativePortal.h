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
	/** Writes everything about the islands, portals, volumes, players and permissions to the log. Each reason is written at most a few times. */
	void DumpState(const std::string& Reason, int MaxTimes = 3);
	/** A thrown prefab/gallery grenade has loaded its playset: stream it into the volume the grenade spawned. */
	void LoadThrownPrefab(UObject* Projectile, UObject* LoadedAsset);
	/** Writes the state of every minigame whose state changed since the last call (PreGame, Setup, ... InProgress). */
	void LogMinigameStates(const char* Why);
	/** Writes the island's creative budget (the "memory used" bar at the top): total and used per category. */
	void LogBudget(const char* Why, int MaxTimes = 40);
	/** Remembers every actor that exists now; a little later the log says which new actors appeared and where. */
	void StartActorProbe(const std::string& Label, class AActor* Anchor);
	/** Writes the results of the actor probes that are due. Called from the minigame driver's tick. */
	void TickProbes();
	/** Lists the pak files next to the executable (a client has only client chunks; the creative server data may be in none of them). */
	void LogPakFiles();
	/** Writes which playsets, props and records the game has loaded and what data they carry. Must run on the game thread. */
	void DumpCreativeData(const char* Why);
	/** A piece a player just built: makes sure it replicates, and a moment later writes whether a client has a channel for it. */
	void WatchPiece(class AActor* Piece);
	/** Every property of an object with its value (see PropDump), to the creative log. */
	void DumpObjectToLog(const std::string& Label, UObject* Object, int Depth, int MaxLines);
}

// Start Game / End Game. The game's server-side minigame flow is missing from this executable: after Start the island's minigame stops in
// Transitioning. This drives it forward step by step: the game's own function first, checked, and forced only if nothing happened.
namespace CreativeMinigame
{
	/** Remembers the game thread (the driver only runs there). Called when an island is set up. */
	void NoteGameThread();
	/** Called often (from ProcessEvent); does something only a few times a second. */
	void Tick();
	/** The client pressed End Game (ServerEndMinigame). */
	void NoteEndRequested(UObject* Controller);
}