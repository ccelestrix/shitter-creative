#pragma once

#include <atomic>

#include "inc.h"

namespace Globals
{
	// Streams thrown prefabs/galleries into the volume the grenade spawns. Experimental: an earlier version of this crashed the server a few seconds after a throw. It is on again with a safer sequence that logs every step; untick it in the Game tab if the server dies.
	extern inline bool bLoadThrownPrefabs = true;
	// Moves a started island game along (Transitioning, Warmup, InProgress, post game). The game's own server code for this is missing here.
	extern inline bool bDriveMinigames = true;
	// Also marks a thrown prefab's volume Ready (state 3) and tells the clients, like the island setup does. Off: forcing the state of that volume in an
	// earlier version was followed by a server crash, so it is tested on its own.
	extern inline bool bPrefabMarkReady = false;
	// Applies the first few saved records of a thrown prefab's props with the volume manager's own ApplyLevelSaveToWorld, and logs where the actors appear.
	extern inline bool bPrefabApplyRecords = false;
	// Thrown prefabs are built from ordinary building pieces (the client has no data for the real ones).
	extern inline bool bOwnPrefabs = true;
	// Set by the GUI button; the game thread writes the creative data survey to the log on its next tick.
	extern inline bool bDumpCreativeDataRequested = false;
	extern inline bool bCreative = false; // the Creative tick box: opens the creative hub map. Off by default, 12.41 runs the creative playlist on the normal map.
	extern inline bool bGoingToPlayEvent = false;
	extern inline bool bEnableAGIDs = true;
	extern inline bool bNoMCP = false;
	extern inline bool bLogProcessEvent = false;
	// extern inline bool bLateGame = false;
	extern inline std::atomic<bool> bLateGame(false);

	extern inline bool bInfiniteMaterials = false;
	extern inline bool bInfiniteAmmo = false;
	extern inline bool bShouldUseReplicationGraph = false;

	extern inline bool bHitReadyToStartMatch = false;
	extern inline bool bInitializedPlaylist = false;
	extern inline bool bStartedListening = false;
	extern inline bool bAutoRestart = false; // doesnt work fyi
	extern inline bool bFillVendingMachines = true;
	extern inline bool bPrivateIPsAreOperator = true;
	extern inline int AmountOfListens = 0; // TODO: Switch to this for LastNum
	extern inline bool bDeveloperMode = false;
}

extern inline int NumToSubtractFromSquadId = 0; // I think 2?

extern inline std::string PlaylistName =
"/Game/Athena/Playlists/Creative/Playlist_PlaygroundV2.Playlist_PlaygroundV2";
// "/Game/Athena/Playlists/Playlist_DefaultSolo.Playlist_DefaultSolo";
// "/Game/Athena/Playlists/gg/Playlist_Gg_Reverse.Playlist_Gg_Reverse";
// "/Game/Athena/Playlists/Playlist_DefaultDuo.Playlist_DefaultDuo";
// "/Game/Athena/Playlists/Playground/Playlist_Playground.Playlist_Playground";
// "/Game/Athena/Playlists/Carmine/Playlist_Carmine.Playlist_Carmine";
// "/Game/Athena/Playlists/Fill/Playlist_Fill_Solo.Playlist_Fill_Solo";
// "/Game/Athena/Playlists/Low/Playlist_Low_Solo.Playlist_Low_Solo";
// "/Game/Athena/Playlists/Bling/Playlist_Bling_Solo.Playlist_Bling_Solo";
// "/Game/Athena/Playlists/Creative/Playlist_PlaygroundV2.Playlist_PlaygroundV2";
// "/Game/Athena/Playlists/Ashton/Playlist_Ashton_Sm.Playlist_Ashton_Sm";
// "/Game/Athena/Playlists/BattleLab/Playlist_BattleLab.Playlist_BattleLab";
// "/MoleGame/Playlists/Playlist_MoleGame.Playlist_MoleGame"; // very experimental dont use

// True for the Creative tick box and also when the creative playlist is played on the normal map (what 12.41 does by default).
static inline bool IsCreativeGame()
{
	return Globals::bCreative || PlaylistName.contains("/Creative/Playlist_Playground");
}
