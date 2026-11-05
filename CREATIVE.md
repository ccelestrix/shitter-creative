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
- **Prefabs / galleries**: the grenade explodes and spawns a prefab volume with the playset set, but the volume never initialises (no stream component, state 0). The experiment (tick box "Activate thrown prefab volumes") logs what the playset is made of, then tries `SpawnActorsInPlayset` (success = the world's actor count goes up), then `SetCurrentPlayset`, then the volume manager's queue.
- **Memory bar**: logged (`Budget (...)`), the cost directory is reported and loaded; costs are not computed yet.
- **Islands / settings / devices**: partly present; see the log lines for what each call did.
