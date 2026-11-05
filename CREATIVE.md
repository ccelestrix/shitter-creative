# Creative on 12.41 (Chapter 2 Season 2)

12.41 runs Unreal Engine 4.24. Upstream Reboot only showed the **Creative** tick box for engine 4.22-4.23, so on 12.41 Creative could not be switched on at all.

## Changed so far
- The Creative tick box now shows for engine 4.22 to 4.24.
- `ShowPlayset` logs an error and returns instead of crashing when the playset loader address was not found for the version.
- `AFortAthenaCreativePortal::GetCreatorName` no longer dereferences a null pointer.
- A missing `CreativePortalManager` on the game state is logged (`LogCreative`) instead of crashing.

## Not tested yet
None of this has been run against a 12.41 client. To find what breaks next, start a 12.41 server with Creative ticked and read the `LogCreative` lines.

## Likely next problems (from reading the code)
- `Addresses::LoadPlayset` is found by a string search. If it comes back 0 on 12.41, islands will not load their playset.
- The code assumes the 4.23 field names on the portal (`LinkedVolume`, `bPortalOpen`, `OwningPlayer`) and on the controller (`OwnedPortal`, `CreativePlotLinkedVolume`). A rename in 12.41 shows up as an offset error in the log.
- Saving and loading island contents is not implemented.

## Logs
- `xenbono.log`: every line from every logger, written straight to disk in the game's working directory (falls back to the exe folder, the DLL folder, then temp if that folder is not writable). The first line says which folder was used.
- `xenbono_creative.log`: only the Creative lines (`LogCreative`), written next to the game executable. It starts with a report of which Creative functions and addresses were found, then logs island setup, the hub teleport, portal teleports and creative item requests.

## Island creation (ported from the Xenon server, checked against the 12.41 SDK)
- A new island starts as the `Temperate_Medium` plot (the first one on the Select Type screen) and its playset.
- The level save component is told the plot is the player's, loaded from the plot, and public. Without that the client says creating and saving islands does not work.
- The portal, island volume and controller now call their `OnRep_` functions so clients see the change (the loading screen and the island appearing come from these).
- Entering the island gives the creative phone, links the volume to the player and drops them in from the sky (Z 10000, then `BeginSkydiving`).
- Every creative `Server*` call the client makes (claim/release portal, load/save checkpoint, load plot, set playset, ...) is logged to `xenbono_creative.log`. Picking a layout (`ServerSetPlayset`) loads that playset into the island.
- Still missing: saving and loading real island contents.

## What is missing from the executable (and what XENBONO does about it)
The Fortnite executable has no dedicated-server code, so some of Creative's server side is simply not there (found by testing, confirmed by people who have done Creative before). XENBONO reimplements the parts it needs. Everything it does is logged to `xenbono_creative.log`.

- **Start Game / End Game** (`CreativeMinigame` in `FortAthenaCreativePortal.cpp`): after Start the game moves the island's minigame to Transitioning and stops. A small driver (tick box "Drive Start Game / End Game") moves it on, following what the Xenon server does in `FortMinigame.cpp`: teams (3 and up) and `OnPlayerPawnPossessedDuringTransition` for each player, `AdvanceState` then `HandleMinigameStarted`, `OnClientFinishTeleportingForMinigame` and a second's wait in WaitingForCameras, then Warmup, InProgress, the time limit, End Game and the reset back to editing. Each step calls the game's own function first (`AdvanceState`, `EndGame`, ...), checks the result, and sets the state itself only if nothing happened.
- **Prefabs / galleries / devices** (devices are thrown as `CP_PlaysetGrenade_...` items, so they are prefabs too): the grenade explodes and spawns a prefab volume with the playset set, but the volume never initialises (no stream component, state 0) and the game's record-spawn step finishes with nothing. A prefab is not a streamed level: it is a list of props, each a saved record (`PlaysetPropLevelSaveRecordCollection`: soft record, name, transform). What a thrown prefab does, in order (tick boxes in the Game tab, "Activate thrown prefab volumes" is on by default):
  1. `SpawnActorsInPlayset` (with an actor probe, the spawn may be asynchronous);
  2. a check of the volume manager (is it "loading islands", is the volume registered; it lowers a stuck loading counter and registers the volume);
  3. a read-only description of the prefab's data and raw bytes of the first records, so the layout of the opaque record structs can be worked out from the log;
  4. tick box "apply the props' saved records": the volume manager's own `ApplyLevelSaveToWorld` for the first 3 records, with probes that log where new actors appear;
  5. `SetCurrentPlayset`, streaming only if a stream component appears;
  6. tick box "also mark their volume Ready" (tested on its own, an earlier version of it was followed by a crash);
  7. the volume manager's queue (`TryActivateQueuedVolumes`).
  Actor probes write `Actor probe '...' after N ms: M new actors: ...` with the class and position of the new actors.
- **Memory bar**: logged (`Budget (...)`), the cost directory is reported and loaded; costs are not computed yet.
- **Islands / settings / devices**: partly present; see the log lines for what each call did.

## If the server never becomes joinable (stuck on the loading screen)
- The server only starts listening once the creative hub's `FortPlayerStartCreative` actors exist. They come from the hub level (`CreativeHub_S11_01_Creative_Spawns`), one of the playlist's three levels. The playlist levels are not streamed by the game state: `StreamLevel` spawns a `BuildingFoundation3x3` actor for each level and that actor streams it, so a lost request is never repeated by the game.
- While the player starts are missing, `xenbono_creative.log` says every 4 seconds (every 20 s after the first 80 s): which world this is, how many player starts exist, which of the playlist's levels have a streaming level (`MISSING` if not), what each streaming level is doing (loaded, visible, pending, should-be-loaded) and what each streaming foundation holds.
- After 8 s a creative streaming level that exists but is not loaded is told to load and become visible. After 10 s, and again after 30 s, a playlist level that still has no streaming level at all is requested again (only that level, so nothing is loaded twice). Each of these is logged as a warning.
- If it still never listens, the useful extra file is the engine log of the server process (`FortniteGame\Saved\Logs\FortniteGame.log` next to the exe), which holds the real load error.
