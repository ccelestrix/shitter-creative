# PrefabPreview

Looks at what our own prefab layout (`XENBONO/PrefabArchitect.h`) makes of a prefab, without starting the game.

The game writes, for every prefab it builds (and for every prefab when the "Write every prefab's pieces to the log" button is used), lines like

```
Architect input: prefab PID_019_PG_Farmstead_M_Barn_a 5 7 5
Architect input: type SM_Farm_Wall_1_C wall 15 512 79 402 box 0 0 0
```

into `xenbono_creative.log`: the size of the prefab in tiles, and for each kind of piece its kind, how many there are, its box (X, Y, Z) and the shape of its top
(`box`, `shed` with the axis and side that is high, `tent` with the axis of its ridge).

```
python extract.py xenbono_creative.log descriptions         # one .txt per prefab
cl /std:c++20 /EHsc /O1 preview.cpp /Fe:preview.exe          # once (MSVC)
preview.exe descriptions\PID_019_PG_Farmstead_M_Barn_a.txt out.json
python render.py out.json out.png                            # needs numpy and opencv-python
```

`render.py` draws every piece as its box (or wedge / tent) from four sides; the green line is the front.
`samples/` has a few hand-written descriptions to start from.
