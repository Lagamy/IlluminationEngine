#include "STH2005Graphics.h"

namespace Graphics
{
	ONEInstance& getONEInstance(PoolId id_)
	{
		return oneInstances[id_];
	}

	ONELevelInstance& getONELevelInstance(PoolId id_)
	{
		return oneLevelInstances[id_];
	}
}
