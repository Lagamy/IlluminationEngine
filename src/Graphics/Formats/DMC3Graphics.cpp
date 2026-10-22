#include "DMC3Graphics.h"

namespace Graphics
{
	DMC3Instance& getDMC3Instance(PoolId id_)
	{
		return dmc3Instances[id_];
	}

	DMC3Mesh& getDMC3Mesh(PoolId id_)
	{
		return dmc3Meshes[id_];
	}

	PoolId getDMC3MeshId(const char* name_)
	{
		return dmc3Meshes.getIdByName(name_);
	}

	DMC3Mesh& getDMC3MeshByName(const char* name_)
	{
		return dmc3Meshes[dmc3Meshes.getIdByName(name_)];
	}
}
