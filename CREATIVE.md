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

## What the client's files really hold (the creative data inspector)
- Result of the 5 Nov test (barn prefab, explosive barrel and creature placer devices): every thrown playset reports `streams level not set, level save record null, cooked 0`. The barn lists 39 associated props, the devices list none and are 1 x 1 x 1. `SpawnActorsInPlayset` adds no actors. So the files of this client do not give the server anything to spawn from; the game's own prefab code has nothing to work on, or is not there (or both).
- The SDK dump (`SDK.rar` from OGS-12.41, made with Dumper-7) is the real 12.41 layout of every playset, prop, volume, volume manager and record class. It shows `ActorTemplateRecord` (0x58 bytes) and `ActorInstanceRecord` (0x70 bytes) as blank padding because those structs are written and read by native code, not through reflection. No other public source describes them (Epic only documents the idea: template records hold an actor class and its property settings, instance records hold a placement and point at a template). Their layout has to be read from the running game.
- What the DLL now writes to `xenbono_creative.log` so that no layout is guessed:
  - `Pak files:` the pak files next to the exe (whether any server or creative chunk exists), at startup and in the survey.
  - `Asset registry ...:` what the cooked asset registry lists under `/Game/Playsets` and `/Game/Creative`, and the assets of the playset, prop, grenade and record classes, loaded or not.
  - `Survey ...:` every playset, prop and record the game has loaded, with which of them carry a level, a spawn item, a record or props, plus the level save components of the islands. Written by the button "Write what the creative data holds to the log" (in a creative game) and once automatically at the first thrown prefab.
  - `Playset dump <name>`, `Prop dump <name> #n`: every property of a thrown playset and of its first three props with type, offset and value (arrays, maps and structs included; a struct with no reflected members is shown as raw bytes). A prop's actor record is dumped and described the same way when it exists.

## Our own prefabs
- The files have no data for the real prefabs, so a thrown prefab that has props (or a footprint bigger than 1 x 1) is built from ordinary wood building pieces: walls on the four edges of its footprint (2 high, a door gap on the south side) and a flat roof of floor pieces. Footprint = the playset's SizeX x SizeY, capped at 10 x 10, centred on the volume the grenade made. Written in `BuildOwnPrefab` (FortAthenaCreativePortal.cpp); tick box "Thrown prefabs: build our own version from building pieces".
- Devices (1 x 1 x 1, no props) are not built this way; they need their real actor classes.
- Geometry assumption to check in the game: pieces are placed by a corner (walls run from the corner along their +Y), tile 512 wide, wall 384 high, taken from the pieces players placed in the logs. If the walls come out shifted or rotated, that is the place to adjust.

## Our own devices
- The asset registry (listed through reflection, no loading) shows what the build holds: 724 playsets, 16,879 props and 629 playset grenades under `/Game/Playsets`, and under `/Game/Creative` 531 maps, 2,009 blueprints and one map per device (`/Game/Creative/Maps/Devices/CP_Devices_<Name>`, about 50 devices) with the device's blueprints in `/Game/Creative/Devices/<Name>`. There are no maps for the prop-based prefabs, no `LevelSaveRecord` asset anywhere.
- A thrown `PID_CP_Devices_<Name>` is therefore built by `BuildOwnDevice`: the registry lists `/Game/Creative/Devices/<Name>`, the blueprints are tried best name first (`BP_<Name>`, `<Name>_GBA`), the first one that is a building actor is spawned in the volume the grenade made, with the thrower's team and the build animation. `Own device <Name>:` lines in `xenbono_creative.log` say what was found and tried.

## Prefabs from the real pieces
- Every playset carries `ActorClassCount`: the classes it is made of and how many of each (the IceCastle walls, the farm roofs, the bank's balconies ...). It says what and how many, not where. `BuildPrefabFromClasses` builds a prefab out of those real pieces instead of player pieces: floors on the ground, walls round the edge of the footprint (door front centre, windows and arches spread out in proportion), a roof, stairs inside and the remaining pieces as decoration. The footprint is the playset's SizeX x SizeY (max 12), the number of wall levels follows from how many wall pieces there are (1 to 3).
- Placement is a guess (pieces are assumed to sit by a corner on a 512 grid with 384 high walls, like the player pieces); the log line `Own prefab (real pieces): built N pieces ...` and the `piece <class> xN as <kind>` lines say what was used. A playset that lists no loadable pieces falls back to the wood house.
- Layout v2: every piece class is measured first (spawned once high above the prefab, its bounding box read with `GetActorBounds`, then removed) and placed by the middle of its box, so the unknown pivots no longer matter. The wall panel with most pieces sets the pitch along the edge and the height of a level; panels are turned so their long side runs along the edge; pillars (and columns) go to the corners and edge middles instead of taking wall slots; caps, battlements and rails run along the top edge; floors and roofs are gridded by their own size; decoration is laid out in rows inside, spaced by its own size; wall lights and wall art are left out. The log lists each class with its measured box.

## Devices from their map (replaces the folder search)
- The 5 Nov test showed the folder search does not work for most devices (`/Game/Creative/Devices/Speaker` holds nothing). Every device does have a map `CP_Devices_<Name>` under `/Game/Creative/Maps/Devices` (found through the asset registry). `ReadDeviceMap` loads that map as an asset only (it is not streamed in), reads `PersistentLevel.Actors` (ULevel::Actors is at 0x98 in this build) and for each actor of one of the game's own blueprints takes its class, relative location, rotation and scale from its root component. `BuildOwnDevice` spawns the same actors in the volume the grenade made, lifted by the playset's own location offset (75 for devices), and tells building actors who built them.
- `Device map <Name>:` and `map actor N:` lines say what was found; the folder search stays as the fallback when no map is listed.

## What the 6 Nov logs showed about building and devices
- Player pieces replicate: `Piece watch:` lines report a channel on the client connection, bReplicates 1, RemoteRole 1, not hidden. So an invisible piece is not a server replication problem.
- The registry listing helper (`RegistryAssets`) came back empty in the device code although the same call worked in the survey; device maps are now loaded by their name (`/Game/Creative/Maps/Devices/CP_Devices_<Name>`), the registry list is only a fallback.
- Prefab pieces are now placed, measured with `GetActorBounds`, and corrected: the real box middle is moved to the wanted point and a panel whose long side does not run along its edge is turned a quarter. The log says how many pieces had to be moved and turned (`of N pieces whose box was checked ...`).

## The server ending itself about a second after start
- Twice (10:09 and 11:43 on 6 Nov, builds with no relation to each other) the process was ended by `TerminateProcess(3)` (exit code 3 is what `abort()` gives) from the game's own code, about 1.0 to 1.1 seconds after "Finished initialization!" and before any player joined. The stack shows only the thread's start routine and the game function that ends the process, so it is a thread of the game that is not the game thread, not our code. Most starts do not die.
- The exit log now lists, after the stack, the thread that ended the process (when it was created, where it runs, which functions created it) and the last twelve threads created, plus the modules loaded since startup (`Modules (at startup)` lists them once). `NtCreateThreadEx` is hooked right after MinHook starts to keep that record.

## The hub that does not load (6 Nov, 11:53 run)
- In a run that could not be joined the server asked for the three hub levels at 11:53:49 and the world still had 0 streaming levels a minute later; the three streaming foundations existed (enabled state 1, level to stream set) and the requests at 10 s and 30 s changed nothing. The same code loaded the hub in 1 s in other runs, so the game's own way of streaming them fails now and then.
- After 15 s of waiting the end of the engine's own log (`Saved/Logs/FortniteGame.log`) is copied into `xenbono.log` (lines about levels, streaming, errors, failures; prefix `LogEngine`), and again when the process ends. After 20 s a level that still has no streaming level is loaded directly with `ULevelStreamingDynamic::LoadLevelInstance`, named like the level itself.
- 6 Nov, 12:03 run: the hub was loaded directly after 20 s, the server listened, the island was set up and its level streamed on the server (`OnFinishedLoading`, floor tiles registered), but the player spawned in the air with no island: the client never got the levels either (its hub and island come from the same streaming that failed on the server). Changes: the direct load now comes after 8 s, the extra foundations at 10 s and 30 s are no longer made, the presetup waits (at most 30 s) while the asset registry is still loading assets (a request made during that can be lost; the state is logged at the presetup and in every hub report), the playlist-levels-have-streamed-in events of the game are called after a direct load, and the island setup now writes every streaming level with its state and the island stream component's contents.
- 6 Nov, 12:13 run (every run since the 11:40 build needed the direct load): the server had all levels and the island streamed fine, the client still had no map. A streaming foundation has a replicated flag `bServerStreamedInLevel` (set by the foundation's own streaming when the server has streamed its level in) that the clients wait for before they load the level. When the levels are loaded by hand the foundations never set it, so after a direct load the flag is now set on every streaming foundation (`OnRep_ServerStreamedInLevel`, net update). The foundation report in the log also shows the foundation's own streaming level (`LevelStreamInfo`) and that flag. The world's `StreamingLevels` list is not where a foundation keeps its level, so "0 streaming levels" in the hub report does not by itself mean the foundation has none.
- 6 Nov, 12:29 run: the exit log traced the process end to a thread that was created before the thread hook was installed (so by the game's startup, not by this DLL): it runs a start routine in the exe (0x56E4D7E) that calls the exe's exit function (0x67269E) directly, which calls `TerminateProcess(3)`. The threads created shortly before are the NVIDIA driver's (the GS window's DirectX device). It is the same call as in the 10:09 and 11:43 crashes. What the thread is for is not known. Workaround: `TerminateProcessHook` parks a thread of the exe that calls `TerminateProcess` with exit code 3 (it sleeps forever inside the hook) instead of letting it end the process; the tick box "Keep the server running if the game tries to end itself (exit code 3)" in the GS window (`Globals::bKeepServerAlive`, on by default) turns it off. Every exit is still written to the log.
- 6 Nov, 12:39 run: parking the thread that calls `TerminateProcess(3)` froze the game (no log line after it, no "Waiting for the hub" reports): the thread is the MAIN thread. Its start routine (exe + 0x56E4D7E) is the exe's entry point and the function it calls (exe + 0x67269E) is the exit of the C runtime, so WinMain returned 3: the engine left its main loop because something asked it to exit with status 3. The parking was removed again. The engine log is not in the game folder but in `%LOCALAPPDATA%\FortniteGame\Saved\Logs\FortniteGame.log`; the exit log now reads that file (the newer of the two candidates) and writes its last 40 lines, which should hold the reason for the exit request.
