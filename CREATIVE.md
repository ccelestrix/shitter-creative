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
