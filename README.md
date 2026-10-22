# IluminationEngine

The shared engine core of SonicFantasy (SonicSunset) and DevilMayCry Rebirth: renderer, entity/level/prefab system, physics and collision, Lua scripting, glTF import, the editor (ImGui) and the generic loaders. A game links the static library `IluminationEngine` and reaches the folder through a symlink (below).

The engine does not build alone. It reads the file formats of the games it supports (the **importers**, see below) but knows nothing about a game's own entities, levels and effects; instead it includes headers and calls functions that the game supplies **by fixed name**, found through the include path and the linker (no registration interfaces).

## Using it from a game

The engine sits next to the game's folder (`../IluminationEngine`) and the game links it with a relative symlink named `IluminationEngine` in its root (`ln -s ../IluminationEngine IluminationEngine`; on Windows this needs symlink support, `git config core.symlinks true`). The game's CMake adds it with an explicit binary directory, since the link points outside the source tree:

```cmake
set(EDITOR ON CACHE BOOL "Build the editor / debug tools" FORCE)   # before add_subdirectory
set(ILLUMINATION_FORMAT_SADX ON CACHE BOOL "" FORCE)               # the format families the game reads
add_subdirectory(IluminationEngine ${CMAKE_BINARY_DIR}/IluminationEngine)
add_executable(Game src/main.cpp ...the game's sources...)
target_include_directories(IluminationEngine PUBLIC src src/Game ...the game's folders...)
target_link_libraries(Game PRIVATE IluminationEngine)
illumination_copy_engine_assets(Game)
illumination_copy_directory(CopyScripts Game ${CMAKE_SOURCE_DIR}/Scripts Scripts)
```

- The game's folders go on `IluminationEngine`'s include path (PUBLIC), so the engine's files find the game's headers and the game's files find the engine's.
- `EDITOR`, `ENGINE_DEBUG` and the platform defines are public: game and engine are compiled with the same struct layouts. With `EDITOR`, `SF_SOURCE_DIR` is the top-level source folder (the game's), where the editor saves levels.
- `illumination_copy_directory`, `illumination_copy_file` and `illumination_copy_engine_assets` (CMakeLists.txt) make the copy-to-`Assets/` targets.
- `External/Collision/Remaps.json` (the game's, copied to `Assets/`) holds the remaps from a collision's raw values to behaviours: every object member is a section (`{"fallback": behaviour, "values": {"0x...": behaviour}}`), read by name (`BABehaviors::remap`). The game decides which section a level or an object's shapes use (`levelRemapSection` in `GameEntity.h`, its loaders); the engine's glTF levels use `levelRemapSection(GLTF_FILE)`.
- `Shaders/` holds the engine's shaders (`Default`, `GLTF`, and `SA`, `STH2005`, `DMC3` for the enabled format families); the game's own go next to them in `Assets/Shaders`.

## Format families (the importers)

Each family is a CMake option (default off; the game sets it before `add_subdirectory`). It adds the family's importers (bytes in, `std::runtime_error` out), the meshes and instances they feed, the resource key types (`src/Keys/`), the graphics pools and getters (`src/Graphics/Formats/`) and the shaders.

| Option | Format | Importers |
|---|---|---|
| `ILLUMINATION_FORMAT_SADX` | Sonic Adventure (DX) | `Importers/Ninja` (models, motions, GVM textures, land tables, SET files), `Importers/GameCube` (disc image, rel modules, texture decoders), `SAMesh`, `TextureSet`, `SAInstance`, `SALevelInstance`, `Keys/SAKeys.h` |
| `ILLUMINATION_FORMAT_STH2005` | Shadow the Hedgehog (2005) | `Importers/STH2005` (ONE archives, PRS, models, motions, collision, placement), `Importers/Renderware` (DFF, TXD, world), `Importers/GameCube`, `RenderWareMesh`, `ONEInstance`, `ONELevelInstance`, `Keys/STH2005Keys.h` |
| `ILLUMINATION_FORMAT_DMC3` | Devil May Cry HD Collection | `Importers/DMC3` (.nbz, .pac, MOD, MOT, TEX; uses the vendored miniz), `DMC3Mesh`, `DMC3Instance`, `Keys/DMC3Keys.h` |

`Importers/External` (the engine's own model, texture, glTF and collision files) is always built. A game includes the family's `Graphics/Formats/<Family>Graphics.h` from its `GameGraphicsGlobals.h` and `GameGraphicsAPI.h`. The instances release their resources through `Loading::release*` functions that the game defines (`GameLoaders.h`).

## What a game supplies

Headers (found by name; every game has its own):

| Header | Holds |
|---|---|
| `GameKeys.h` | the resource key types the engine's structs mention |
| `GameEntity.h` | `SourceEngineEnum` (must contain `GLTF_FILE`), `DefaultSourceEngine`, the game list (`GameSources`, `GameSourceCount`, `GameScriptFolders`), `gameShaderName`, `usesNinjaShapes`, `prefabGameName`, `prefabGameFolder`, `gameDiscMeshName`, `levelRemapSection`, `AnimationIntent` |
| `GamePrefab.h` | `PrefabKind` (must contain `External`), `DefaultPrefabKind`, the declarations of `buildGamePrefab`, `prepareThumbnailParams`, `restoreGameSpawnParams` |
| `GamePlacement.h`, `GameShapes.h` | `PrefabPlacement`, the shape helpers |
| `GameAPI.h`, `GameGraphicsAPI.h`, `GameGraphicsGlobals.h`, `GameLevelLoader.h` | the game's declarations the engine's API, GPU and loader headers pull in |
| `AssetThumbnails.h` | the editor's Assets-tab thumbnails |
| `GameLoaders.h` | the `Loading::` functions that make and release the game's instances, meshes and collision from its keys (the format families' instances call the `release*` ones) |
| `LibraryExport.h` | `Loading::exportLibrary`, the editor's Library.gltf export (EDITOR only) |
| `GameEntityMembers.inl`, `GameLevelMembers.inl`, `GameEntityRecipe.inl`, `GameEntityBaseline.inl`, `GamePrefabFields.inl`, `GameSpawnParams.inl`, `GameLevelRecipe.inl` | fields and members `#include`d inside `Entity`, `Level`, `EntityRecipe`, `EntityBaseline`, `Prefab`, `SpawnParams` and `Loading::Recipe` |

Functions (declared in the headers above or in the engine's, defined by the game): the `Entity` and `Level` members that draw, update and destroy a game's instances; `registerPrefabs`, `buildGamePrefab`; `Loading::readGameRecipe`, `Loading::buildGameLevel`, `Loading::writeGameLevelFile`, `Loading::loadPrefabData`; `Graphics::initGameEffects`, `tickGameEffects`, `drawGameEffects`, `destroyGameGraphics`; `Scripting::registerGameAPI`; `PrefabThumbnails::renderModel`. The list is the linker's: build the game and every missing symbol is one to define.

Games that use it: [SonicFantasy](https://github.com/Lagamy/SonicFantasy) (`src/Game/` is the model of all of this) and DevilMayCry Rebirth. The GitHub repository is spelled `IlluminationEngine` (two l's); the folder and the CMake target keep one, and the games clone it into the folder name their symlink expects (`git clone git@github.com:Lagamy/IlluminationEngine.git IluminationEngine`).
