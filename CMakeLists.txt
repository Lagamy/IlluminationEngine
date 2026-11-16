cmake_minimum_required(VERSION 3.28)
project(IluminationEngine C CXX)

# The engine is a static library a game links; it does not build alone (it includes headers and calls functions the game supplies, see README.md).
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
	message(FATAL_ERROR "IluminationEngine is built as part of a game: add_subdirectory it from the game's CMakeLists.txt (see README.md).")
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# GLAD
set(GLAD_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third-party/glad")
set(GLAD_SRC ${GLAD_DIR}/src/glad.c)

# GLM
set(GLM_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third-party/glm")

# ImGui
set(IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third-party/imgui")

# GLFW (vendored static builds: lib/libglfw3.a for Linux, lib/glfw3.lib for Windows)
set(GLFW_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third-party/GLFW")

# Stb_Image setup
set(STB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third-party/stb")

# cgltf 1.15 (vendored, MIT, https://github.com/jkuhlmann/cgltf): single-header glTF 2.0 parser, implementation in cgltf.cpp
set(CGLTF_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third-party/cgltf")

# Lua 5.4.7 (vendored, MIT, https://www.lua.org/ftp/lua-5.4.7.tar.gz, the interpreter and compiler programs left out), built as a static C library. LUA_USE_POSIX only on non-Windows (Lua picks its Windows setup itself): no dlopen, so scripts can't load native libraries.
set(LUA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third-party/lua")
file(GLOB LUA_SRC ${LUA_DIR}/*.c)
# Compiled as C++ (it supports that) so that a Lua error is a C++ exception: it unwinds our C++ frames, where a longjmp over them would not run their destructors.
set_source_files_properties(${LUA_SRC} PROPERTIES LANGUAGE CXX)
add_library(lua STATIC ${LUA_SRC})
if(WIN32)
	target_compile_options(lua PRIVATE /w)
else()
	target_compile_definitions(lua PRIVATE LUA_USE_POSIX)
	target_compile_options(lua PRIVATE -w)
endif()
target_include_directories(lua PUBLIC ${LUA_DIR})

# The editor: the debug UI (ImGui), the collision view, the entity labels, the editor camera (src/EngineEditor/Editor.h) and the GL debug output.
# The game sets it before add_subdirectory (set(EDITOR ON CACHE BOOL "..." FORCE) makes that line win over the cache, so a change there is picked up by the next CMake run with no -D flag); not set = off.
set(EDITOR OFF CACHE BOOL "Build the editor / debug tools")

# The format families the engine can read, one option each (a game sets the ones it uses before add_subdirectory, like EDITOR; not set = off): the importers, the meshes and instances they feed, the graphics pools and the shaders of that family.
# SADX: Sonic Adventure (Ninja models, GVM textures, rel land tables, SET files). STH2005: Shadow the Hedgehog (2005) (ONE archives, RenderWare, GameCube textures). DMC3: the Devil May Cry HD Collection (.nbz archives, MOD / MOT / TEX).
set(ILLUMINATION_FORMAT_SADX OFF CACHE BOOL "Read the SADX formats")
set(ILLUMINATION_FORMAT_STH2005 OFF CACHE BOOL "Read the Shadow the Hedgehog (2005) formats")
set(ILLUMINATION_FORMAT_DMC3 OFF CACHE BOOL "Read the Devil May Cry HD Collection formats")

add_library(IluminationEngine STATIC
	src/Graphics/Renderer.cpp
	src/Graphics/RenderTarget.cpp
	src/Graphics/GPU_API.cpp
	src/Graphics/Resources/Shader.cpp
	src/Graphics/Resources/Texture.cpp
	src/Graphics/Resources/BasicMesh.cpp
	src/Graphics/Resources/ExternalModel.cpp
	src/Graphics/Resources/VertexLayout.cpp
	src/Graphics/InstanceData/GLTFLevelInstance.cpp
	src/Importers/External/ModelFile.cpp
	src/Importers/External/TextureFile.cpp
	src/Importers/External/GLTFFile.cpp
	src/Importers/External/CollisionFile.cpp
	src/Objects/Camera.cpp
	src/Objects/Entity.cpp
	src/Objects/BABehaviors.cpp
	src/Gameplay/GameplaySystem.cpp
	src/Gameplay/GameEvents.cpp
	src/Scripting/Scripting.cpp
	src/Scripting/ScriptAPI.cpp
	src/Objects/Level.cpp
	src/Objects/HubWorld.cpp
	src/Objects/MeshDiff.cpp
	src/Objects/CollisionDiff.cpp
	src/Objects/Prefab.cpp
	src/Objects/SpatialIndex.cpp
	src/Loading/ResourceLoader.cpp
	src/Loading/LevelLoader.cpp
	src/Loading/ExternalAssets.cpp
	src/Physics/Collision.cpp
	src/Physics/QuadTree.cpp
	src/Physics/PhysicsSystem.cpp
	src/Physics/CollisionSystem.cpp
	src/Misc/API.cpp

	${GLAD_SRC}
	${STB_DIR}/stb_image.cpp
	${STB_DIR}/stb_image_write.cpp
	${CGLTF_DIR}/cgltf.cpp
)

# The GameCube readers (disc image, rel modules, texture decoders) are shared by SADX and Shadow.
if(ILLUMINATION_FORMAT_SADX OR ILLUMINATION_FORMAT_STH2005)
	target_sources(IluminationEngine PRIVATE
		src/Importers/GameCube/GCDisc.cpp
		src/Importers/GameCube/Rel.cpp
		src/Importers/GameCube/GXTexture.cpp
		src/Importers/GameCube/SaComp.cpp
	)
endif()

if(ILLUMINATION_FORMAT_SADX)
	target_sources(IluminationEngine PRIVATE
		src/Importers/Ninja/SAMdl.cpp
		src/Importers/Ninja/Gvm.cpp
		src/Importers/Ninja/SAMotion.cpp
		src/Importers/Ninja/SALandTable.cpp
		src/Importers/Ninja/SAShapes.cpp
		src/Importers/Ninja/SASet.cpp
		src/Graphics/Resources/SAMesh.cpp
		src/Graphics/Resources/TextureSet.cpp
		src/Graphics/InstanceData/SAInstance.cpp
		src/Graphics/InstanceData/SALevelInstance.cpp
		src/Graphics/Formats/SADXGraphics.cpp
	)
endif()

if(ILLUMINATION_FORMAT_STH2005)
	target_sources(IluminationEngine PRIVATE
		src/Importers/STH2005/Prs.cpp
		src/Importers/STH2005/ONE.cpp
		src/Importers/STH2005/Bon.cpp
		src/Importers/STH2005/CCL.cpp
		src/Importers/STH2005/Placement.cpp
		src/Importers/STH2005/RingLine.cpp
		src/Importers/STH2005/Model.cpp
		src/Importers/STH2005/Mtp.cpp
		src/Importers/Renderware/Dff.cpp
		src/Importers/Renderware/RwMaterial.cpp
		src/Importers/Renderware/World.cpp
		src/Importers/Renderware/CollisionWorld.cpp
		src/Importers/Renderware/Txd.cpp
		src/Importers/Renderware/Uva.cpp
		src/Graphics/Resources/RenderWareMesh.cpp
		src/Graphics/InstanceData/ONEInstance.cpp
		src/Graphics/InstanceData/ONELevelInstance.cpp
		src/Graphics/Formats/STH2005Graphics.cpp
	)
endif()

# miniz 3.0.2 (vendored, MIT, https://github.com/richgel999/miniz/releases/tag/3.0.2, amalgamated miniz.c/miniz.h): inflates the ZIP-based .nbz archives (src/Importers/DMC3/Nbz). Included as "miniz/miniz.h" through the third-party include directory.
if(ILLUMINATION_FORMAT_DMC3)
	target_sources(IluminationEngine PRIVATE
		src/Importers/DMC3/Nbz.cpp
		src/Importers/DMC3/Pac.cpp
		src/Importers/DMC3/Mod.cpp
		src/Importers/DMC3/Mot.cpp
		src/Importers/DMC3/Tex.cpp
		src/Graphics/Resources/DMC3Mesh.cpp
		src/Graphics/InstanceData/DMC3Instance.cpp
		src/Graphics/Formats/DMC3Graphics.cpp
		third-party/miniz/miniz.c
	)
endif()

if(EDITOR)
	target_sources(IluminationEngine PRIVATE
		src/EngineEditor/Editor.cpp
		src/EngineEditor/EditorCamera.cpp
		src/EngineEditor/Gizmo.cpp
		src/EngineEditor/Debug/DebugUI.cpp
		src/EngineEditor/Debug/CollisionView.cpp
		src/Loading/GLTFImport.cpp
		src/Loading/ExternalPrefabs.cpp
		src/Loading/EntityGeometry.cpp
		src/EngineEditor/Debug/Text.cpp
		src/EngineEditor/Panels/EntityListPanel.cpp
		src/EngineEditor/Panels/InspectorPanel.cpp
		src/EngineEditor/Panels/AssetPanel.cpp
		src/EngineEditor/Panels/PrefabPanel.cpp
		src/EngineEditor/Panels/PrefabThumbnails.cpp
		src/EngineEditor/Panels/GeometryPanel.cpp
		src/EngineEditor/Panels/TexturingPanel.cpp
		src/EngineEditor/Panels/CollisionPanel.cpp
		src/EngineEditor/Panels/TriangleEditor.cpp
		src/EngineEditor/EditMesh.cpp
		src/EngineEditor/CollisionEdit.cpp
		${IMGUI_DIR}/imgui.cpp
		${IMGUI_DIR}/imgui_draw.cpp
		${IMGUI_DIR}/imgui_widgets.cpp
		${IMGUI_DIR}/imgui_tables.cpp
		${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
		${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
	)
	# EDITOR is public: the game and the engine must see the same struct layouts (the headers have #ifdef EDITOR members).
	target_compile_definitions(IluminationEngine PUBLIC EDITOR)
	# Where the editor saves level folders (Levels/ of the game's source tree, the top-level project of the build; the copy in Assets/Levels is written too).
	target_compile_definitions(IluminationEngine PUBLIC SF_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
endif()

# The engine's own folders. A game adds its own (target_include_directories(IluminationEngine PUBLIC ...), so the engine's files find the game's headers and the game's find the engine's).
target_include_directories(IluminationEngine PUBLIC
	${CMAKE_CURRENT_SOURCE_DIR}/src
	${CMAKE_CURRENT_SOURCE_DIR}/src/Objects
	${CMAKE_CURRENT_SOURCE_DIR}/src/Gameplay
	${CMAKE_CURRENT_SOURCE_DIR}/src/Scripting
	${CMAKE_CURRENT_SOURCE_DIR}/src/Graphics
	${CMAKE_CURRENT_SOURCE_DIR}/src/Graphics/Resources
	${CMAKE_CURRENT_SOURCE_DIR}/src/Graphics/InstanceData
	${CMAKE_CURRENT_SOURCE_DIR}/src/Graphics/Formats
	${CMAKE_CURRENT_SOURCE_DIR}/src/Importers
	${CMAKE_CURRENT_SOURCE_DIR}/src/Importers/External
	${CMAKE_CURRENT_SOURCE_DIR}/src/Importers/GameCube
	${CMAKE_CURRENT_SOURCE_DIR}/src/Importers/Ninja
	${CMAKE_CURRENT_SOURCE_DIR}/src/Importers/STH2005
	${CMAKE_CURRENT_SOURCE_DIR}/src/Importers/Renderware
	${CMAKE_CURRENT_SOURCE_DIR}/src/Importers/DMC3
	${CMAKE_CURRENT_SOURCE_DIR}/src/Loading
	${CMAKE_CURRENT_SOURCE_DIR}/src/Physics
	${CMAKE_CURRENT_SOURCE_DIR}/src/Keys
	${CMAKE_CURRENT_SOURCE_DIR}/src/Misc
	${CMAKE_CURRENT_SOURCE_DIR}/src/EngineEditor
	${CMAKE_CURRENT_SOURCE_DIR}/src/EngineEditor/Debug
	${CMAKE_CURRENT_SOURCE_DIR}/src/EngineEditor/Panels

	${GLAD_DIR}/include
	${GLFW_DIR}/include
	${GLM_DIR}
	${IMGUI_DIR}
	${STB_DIR}
	${CGLTF_DIR}
	${CMAKE_CURRENT_SOURCE_DIR}/third-party
)

# Platform libraries. Linux: the static GLFW lib loads X11 at runtime through dlopen, so it only needs dl, pthread and m.
# Windows: the vendored glfw3.lib is a static MSVC build, so it needs the system libraries GLFW calls into.
# All public: the game's executable links them through the engine.
if(WIN32)
	target_link_libraries(IluminationEngine PUBLIC ${GLFW_DIR}/lib/glfw3.lib opengl32 gdi32 user32 shell32)
	target_compile_definitions(IluminationEngine PUBLIC NOMINMAX _USE_MATH_DEFINES _CRT_SECURE_NO_WARNINGS)
	if(MSVC)
		target_compile_options(IluminationEngine PUBLIC /utf-8)
		target_compile_options(lua PRIVATE /utf-8)
	endif()
else()
	target_link_libraries(IluminationEngine PUBLIC ${GLFW_DIR}/lib/libglfw3.a pthread m GL dl)
endif()

# Lua
target_link_libraries(IluminationEngine PUBLIC lua)

# Enables the Pool / PoolMap validity checks (public: the pools' layout changes with it, the game must agree)
target_compile_definitions(IluminationEngine PUBLIC $<$<CONFIG:Debug>:ENGINE_DEBUG>)

# The game's executable links the engine and copies what it needs to <executable folder>/Assets with these (every one is a target built with ALL, without outputs, so it runs on every build and replaces existing files, even when the executable doesn't need relinking).
# illumination_copy_directory(<name> <executable target> <source folder> <folder in Assets>): copies a folder.
function(illumination_copy_directory name target source destination)
	add_custom_target(${name} ALL
		COMMAND ${CMAKE_COMMAND} -E copy_directory
			${source}
			$<TARGET_FILE_DIR:${target}>/Assets/${destination}
	)
	add_dependencies(${name} ${target})
endfunction()

# illumination_copy_file(<name> <executable target> <source file> <path in Assets>): copies one file when it changed.
function(illumination_copy_file name target source destination)
	get_filename_component(destinationFolder ${destination} DIRECTORY)
	add_custom_target(${name} ALL
		COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:${target}>/Assets/${destinationFolder}
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
			${source}
			$<TARGET_FILE_DIR:${target}>/Assets/${destination}
	)
	add_dependencies(${name} ${target})
endfunction()

# illumination_copy_engine_assets(<executable target>): the engine's own shaders to Assets/Shaders: Default and GLTF, and those of each enabled format family (SA and STH2005 for SADX / Shadow, DMC3). The game copies its shaders into the same folder with illumination_copy_directory; make that target depend on CopyEngineShaders so they don't write at the same time.
function(illumination_copy_engine_assets target)
	set(shaderFolders Default GLTF)
	if(ILLUMINATION_FORMAT_SADX)
		list(APPEND shaderFolders SA)
	endif()
	if(ILLUMINATION_FORMAT_STH2005)
		list(APPEND shaderFolders STH2005)
	endif()
	if(ILLUMINATION_FORMAT_DMC3)
		list(APPEND shaderFolders DMC3)
	endif()
	set(copyCommands)
	foreach(folder IN LISTS shaderFolders)
		list(APPEND copyCommands COMMAND ${CMAKE_COMMAND} -E copy_directory
			${CMAKE_CURRENT_FUNCTION_LIST_DIR}/Shaders/${folder}
			$<TARGET_FILE_DIR:${target}>/Assets/Shaders/${folder})
	endforeach()
	add_custom_target(CopyEngineShaders ALL ${copyCommands})
	add_dependencies(CopyEngineShaders ${target})
endfunction()
