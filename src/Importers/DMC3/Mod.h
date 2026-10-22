#pragma once
#include <cstdint>
#include <span>
#include "PoolId.h"

namespace DMC3
{
/*
Imports a `MOD ` model entry (docs/DMC3_FORMATS.md, "MOD (model)"; bytes_ is the entry alone, its offsets are from its start) into a new Graphics::DMC3Mesh named name_ and returns its id. One node with no transform
and one submesh per mesh record, in the order of the objects; the triangles are the meshes' strips. White (vertex color); each submesh gets its UVs and its texture index (the textures themselves come from DMC3::importTextures). Bone indices, weights and the skeleton are not read,
the normals are only used to pick each triangle's winding. Needs the renderer initialized (the submeshes are uploaded). Throws std::runtime_error naming the offset if the entry does not follow the layout;
the partly made DMC3Mesh then stays in the pool for the caller to remove.
*/
PoolId importMod(const char* name_, std::span<const uint8_t> bytes_);
}
