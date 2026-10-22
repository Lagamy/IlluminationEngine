#pragma once
#include "DMC3Instance.h"
#include "DMC3Mesh.h"
#include "Pool.h"
#include "PoolMap.h"
#include "PoolId.h"

/*
The graphics pools and API of the Devil May Cry HD Collection (DMC3) format family: meshes and instances. A game that reads its data (CMake option ILLUMINATION_FORMAT_DMC3) includes this file from its GameGraphicsGlobals.h and GameGraphicsAPI.h; defined in Formats/DMC3Graphics.cpp.
*/
namespace Graphics
{
	inline PoolMap<DMC3Mesh> dmc3Meshes = ("DMC3Meshes");
	inline Pool<DMC3Instance> dmc3Instances = ("DMC3Instances");

	DMC3Mesh& getDMC3Mesh(PoolId id_);
	DMC3Instance& getDMC3Instance(PoolId id_);
	PoolId getDMC3MeshId(const char* name_);
	DMC3Mesh& getDMC3MeshByName(const char* name_);
}
