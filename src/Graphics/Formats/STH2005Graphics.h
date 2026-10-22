#pragma once
#include "ONE.h"
#include "ONEInstance.h"
#include "ONELevelInstance.h"
#include "Pool.h"
#include "PoolMap.h"
#include "PoolId.h"

/*
The graphics pools and API of the Shadow the Hedgehog (2005) format family: ONE archives and the instances made from them. A game that reads Shadow discs (CMake option ILLUMINATION_FORMAT_STH2005) includes this file from its GameGraphicsGlobals.h and GameGraphicsAPI.h; defined in Formats/STH2005Graphics.cpp.
*/
namespace Graphics
{
	inline PoolMap<STH2005::ONE> oneArchives = ("ONEArchives");
	inline Pool<ONEInstance> oneInstances = ("ONEInstances");
	inline Pool<ONELevelInstance> oneLevelInstances = ("ONELevelInstances");

	ONEInstance& getONEInstance(PoolId id_);
	ONELevelInstance& getONELevelInstance(PoolId id_);
}
