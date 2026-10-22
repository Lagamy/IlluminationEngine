#pragma once
#include "SAInstance.h"
#include "SALevelInstance.h"
#include "SAMesh.h"
#include "SAMotion.h"
#include "SAAnimation.h"
#include "SAMdl.h"
#include "Rel.h"
#include "TextureSet.h"
#include "TextureBinding.h"
#include "Pool.h"
#include "PoolMap.h"
#include "PoolId.h"
#include <string>
#include <unordered_map>

/*
The graphics pools and API of the SADX (Ninja) format family: meshes, motions, texture sets, instances and the opened rel modules. A game that reads SADX discs (CMake option ILLUMINATION_FORMAT_SADX) includes this file from its GameGraphicsGlobals.h and GameGraphicsAPI.h; defined in Formats/SADXGraphics.cpp.
*/
namespace Graphics
{
	inline PoolMap<TextureSet> textureSets = ("TextureSets");
	inline Pool<TextureBinding> textureBindings = ("TextureBindings");
	inline PoolMap<SAMesh> saMeshes = ("SAMeshes");
	inline std::unordered_map<std::string, RelModule> relModules;
	inline PoolMap<SAMotion> saMotions = ("SAMotions");
	inline Pool<SAInstance> saInstances = ("SAInstances");
	inline Pool<SALevelInstance> saLevelInstances = ("SALevelInstances");

	PoolId addSAMesh(const char* name_);
	PoolId addTextureSet(const char* name_);

	TextureSet& getTextureSet(PoolId id_);
	SAMesh& getSAMesh(PoolId id_);
	SAInstance& getSAInstance(PoolId id_);
	SALevelInstance& getSALevelInstance(PoolId id_);

	PoolId getTextureSetId(const char* name_);
	PoolId getSAMeshId(const char* name_);

	SAMesh& getSAMeshByName(const char* name_);
}
