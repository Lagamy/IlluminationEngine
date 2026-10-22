#pragma once
#include "BoundingSphere.h"
#include <cstdint>
#include <vector>
#include <glm/glm.hpp>
#include "BasicMesh.h"
#include "PoolId.h"
#include "Texture.h"

namespace Graphics
{
/* One drawn piece of a node: its geometry, uploaded to the GPU by DMC3Mesh::use, and the index of its texture in DMC3Mesh::textures (-1 = untextured). */
struct DMC3Submesh
{
	BasicMesh mesh;
	int32_t textureIndex = -1;
};

/*
One node of the model's hierarchy. parent is an index into DMC3Mesh::nodes, -1 for a root.
Nodes are stored parent before child, so world matrices can be computed in one pass.
localTransform is the node's transform relative to its parent.
*/
struct DMC3Node
{
	int32_t parent;
	glm::mat4 localTransform;
	std::vector<DMC3Submesh> submeshes;
};

/*
The bones of a skinned model (DMC3::importMod), all indexed by bone id, the value vertices carry in Vertex::boneIndices.
parent is a bone id, -1 for the root. localTranslation is the bone's rest offset from its parent (the rest pose has no rotation).
inverseBind takes a rest-pose model-space point into the bone's space. order lists the bone ids parents before children, so world matrices can be computed in one pass.
Empty (no bones) = the model is not skinned.
*/
struct DMC3Skeleton
{
	std::vector<int32_t> parent;
	std::vector<glm::vec3> localTranslation;
	std::vector<glm::mat4> inverseBind;
	std::vector<uint8_t> order;
};

/*
A model of the DMC3 pool (Graphics::dmc3Meshes), made by DMC3::importMod: its nodes, each with its submeshes.
users counts the instances using the mesh; at 0 it is removed (Loading::releaseDMC3Mesh).
*/
struct DMC3Mesh
{
	std::vector<DMC3Node> nodes;
	DMC3Skeleton skeleton;
	/* The model's textures (DMC3::importTextures), uploaded by use() and freed by destroy(); a submesh's textureIndex outside this list is drawn untextured. */
	std::vector<Texture> textures;
	uint32_t users = 0;

	BoundingSphere bounds;

	void use();
	void destroy();

	/* Fills bounds from the vertices in the rest pose, once. */
	void computeBounds();
};
}
