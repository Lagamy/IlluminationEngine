#include "Mod.h"
#include "GPU_API.h"
#include "GPU_Globals.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cstring>
#include <format>
#include <stdexcept>
#include <vector>

namespace DMC3
{
/* Reads a T at an offset of the entry, throwing if it is not inside it. */
template<typename T>
static T read(std::span<const uint8_t> bytes_, int64_t offset_)
{
	if(offset_ < 0 || static_cast<uint64_t>(offset_) + sizeof(T) > bytes_.size())
	{
		throw std::runtime_error(std::format("Mod: read of {} bytes at {} is outside the {} byte entry.", sizeof(T), offset_, bytes_.size()));
	}
	T value;
	std::memcpy(&value, bytes_.data() + offset_, sizeof(T));
	return value;
}

static glm::vec3 readVec3(std::span<const uint8_t> bytes_, int64_t offset_)
{
	return glm::vec3(read<float>(bytes_, offset_), read<float>(bytes_, offset_ + 4), read<float>(bytes_, offset_ + 8));
}

/*
Triangles of a mesh's strip: vertex p ends a triangle (p-2, p-1, p) unless its weight word has bit 15 set (external, the Blender addon's reading of the weights, see the format doc). The strip's winding is not read from the file:
as in the addon, each triangle is wound so that its face normal points the way the sum of its three vertex normals does (the real rule is unknown), which gives counter-clockwise front faces as the renderer expects.
*/
static void addStripTriangles(Graphics::BasicMesh& rMesh_, const std::vector<bool>& rNoTriangle_)
{
	const Graphics::VertexArray& rVertices = rMesh_.verticies;
	uint32_t p1 = 0;
	uint32_t p2 = 1;
	for(uint32_t p3 = 2; p3 < rVertices.size(); p3++)
	{
		if(!rNoTriangle_[p3])
		{
			glm::vec3 normal = glm::cross(rVertices[p3].position - rVertices[p1].position, rVertices[p2].position - rVertices[p1].position);
			glm::vec3 vertexNormals = rVertices[p1].normal + rVertices[p2].normal + rVertices[p3].normal;
			rMesh_.indicies.push_back(p1);
			if(glm::dot(normal, vertexNormals) > 0.0f)
			{
				rMesh_.indicies.push_back(p3);
				rMesh_.indicies.push_back(p2);
			}
			else
			{
				rMesh_.indicies.push_back(p2);
				rMesh_.indicies.push_back(p3);
			}
		}
		p1 = p2;
		p2 = p3;
	}
}

/*
Mesh record (80 bytes): u16 vertex count, u16 texture index, 12 unknown, i64 offsets of the positions, normals, UVs, bone indices and bone weights (from +0x10), 24 unknown. Positions and normals are 3 floats, the UVs two i16
(divided by 4096). Bone indices are 4 bytes per vertex: byte 0 was 0 in every vertex of pl000 slot 1 and is ignored, bytes 1..3 are bone ids times 4. The weight is a u16: three 5-bit weights (value / 31) for those bytes, and bit 15 for the strip.
The three weights summed to 1 in every vertex of pl000 slot 1 (file inspection, 2026-10-17), so they are used as they are. v is not flipped: the textures are decoded top row first and uploaded as row 0, so v = 0 is the top row, as in the file's DDS (the addon flips it for Blender).
*/
static Graphics::DMC3Submesh readMesh(std::span<const uint8_t> bytes_, int64_t meshOffset_)
{
	Graphics::DMC3Submesh submesh;
	uint16_t vertexCount = read<uint16_t>(bytes_, meshOffset_);
	int64_t positionsOffset = read<int64_t>(bytes_, meshOffset_ + 0x10);
	int64_t normalsOffset = read<int64_t>(bytes_, meshOffset_ + 0x18);
	int64_t uvsOffset = read<int64_t>(bytes_, meshOffset_ + 0x20);
	int64_t boneIndicesOffset = read<int64_t>(bytes_, meshOffset_ + 0x28);
	int64_t weightsOffset = read<int64_t>(bytes_, meshOffset_ + 0x30);
	submesh.textureIndex = read<uint16_t>(bytes_, meshOffset_ + 2);

	std::vector<bool> noTriangle(vertexCount);
	for(uint16_t i = 0; i < vertexCount; i++)
	{
		glm::vec2 uv(read<int16_t>(bytes_, uvsOffset + 4 * i) / 4096.0f, read<int16_t>(bytes_, uvsOffset + 4 * i + 2) / 4096.0f);
		submesh.mesh.verticies.push_back(Graphics::Vertex{readVec3(bytes_, positionsOffset + 12 * i), readVec3(bytes_, normalsOffset + 12 * i), uv, glm::vec4(1.0f)});
		uint16_t weights = read<uint16_t>(bytes_, weightsOffset + 2 * i);
		noTriangle[i] = (weights & 0x8000) != 0;
		Graphics::Vertex& rVertex = submesh.mesh.verticies[i];
		for(int k = 0; k < 3; k++)
		{
			rVertex.boneIndices[k] = read<uint8_t>(bytes_, boneIndicesOffset + 4 * i + 1 + k) / 4;
			rVertex.boneWeights[k] = ((weights >> (5 * k)) & 31) / 31.0f;
		}
	}
	addStripTriangles(submesh.mesh, noTriangle);
	return submesh;
}

/*
Skeleton at the i64 offset of header +0x20 (bone count: u8 at +0x11). It starts with four i32 offsets, relative to its start, to: parent per position (u8, 255 = root), bone id per position (u8, "order"), child object per position (not used),
and the transforms (0x20 bytes per bone id: 3 floats, then 20 bytes not used here; their first float equals the length of the 3 floats in the bones checked).
Layout deduced by file inspection on pl000 slot 1 (2026-10-17): the parent table and the order table are indexed by position, the transforms by bone id; the floats are translations relative to the parent bone (accumulated down the parents, they put the hips at y = 116 and each joint between the vertices
weighted to the bones around it). The order table lists parents first. There is no rotation in the rest pose.
*/
static void readSkeleton(std::span<const uint8_t> bytes_, Graphics::DMC3Skeleton& rSkeleton_)
{
	uint8_t boneCount = read<uint8_t>(bytes_, 0x11);
	int64_t skeletonOffset = read<int64_t>(bytes_, 0x20);
	if(boneCount == 0 || skeletonOffset == 0) return;
	int32_t parentsOffset = read<int32_t>(bytes_, skeletonOffset);
	int32_t orderOffset = read<int32_t>(bytes_, skeletonOffset + 4);
	int32_t transformsOffset = read<int32_t>(bytes_, skeletonOffset + 12);

	rSkeleton_.parent.assign(boneCount, -1);
	rSkeleton_.localTranslation.resize(boneCount);
	rSkeleton_.inverseBind.resize(boneCount);
	for(uint8_t i = 0; i < boneCount; i++)
	{
		uint8_t bone = read<uint8_t>(bytes_, skeletonOffset + orderOffset + i);
		uint8_t parent = read<uint8_t>(bytes_, skeletonOffset + parentsOffset + i);
		if(bone >= boneCount || (parent != 255 && parent >= boneCount))
		{
			throw std::runtime_error(std::format("Mod: skeleton position {} names bone {} with parent {} of {} bones.", i, bone, parent, boneCount));
		}
		rSkeleton_.parent[bone] = parent == 255 ? -1 : parent;
		rSkeleton_.order.push_back(bone);
	}

	/* Rest world positions, parents first; the inverse bind of a bone without rotation is the translation by minus its world position. */
	std::vector<glm::vec3> world(boneCount);
	for(uint8_t bone : rSkeleton_.order)
	{
		int32_t parent = rSkeleton_.parent[bone];
		rSkeleton_.localTranslation[bone] = readVec3(bytes_, skeletonOffset + transformsOffset + 0x20 * static_cast<int64_t>(bone));
		world[bone] = (parent < 0 ? glm::vec3(0.0f) : world[parent]) + rSkeleton_.localTranslation[bone];
		rSkeleton_.inverseBind[bone] = glm::translate(glm::mat4(1.0f), -world[bone]);
	}
}

/*
Entry layout read here (external: the Blender addon, checked by file inspection on pl000 slots 1 and 12; see docs/DMC3_FORMATS.md, "MOD (model)"): `MOD ` at +0, u8 object count at +0x10, objects of 64 bytes from +0x40, each with a u8 mesh count at +0
and an i64 offset of its mesh table at +8 (offsets are from the start of the entry).
*/
PoolId importMod(const char* name_, std::span<const uint8_t> bytes_)
{
	if(bytes_.size() < 0x40 || std::memcmp(bytes_.data(), "MOD ", 4) != 0)
	{
		throw std::runtime_error(std::format("Mod: {} is not a MOD entry.", name_));
	}

	/*
	The node is added to the mesh before its submeshes are made, so that if a later read throws, the pool's removal of the mesh also frees the submeshes already uploaded.
	*/
	PoolId dmc3MeshId = Graphics::dmc3Meshes.add(name_);
	Graphics::DMC3Mesh& rDMC3Mesh = Graphics::getDMC3Mesh(dmc3MeshId);
	Graphics::DMC3Node& node = rDMC3Mesh.nodes.emplace_back();
	node.parent = -1;
	node.localTransform = glm::mat4(1.0f);

	readSkeleton(bytes_, rDMC3Mesh.skeleton);

	uint8_t objectCount = bytes_[0x10];
	for(uint8_t i = 0; i < objectCount; i++)
	{
		int64_t objectOffset = 0x40 + 64 * static_cast<int64_t>(i);
		uint8_t meshCount = read<uint8_t>(bytes_, objectOffset);
		int64_t meshesOffset = read<int64_t>(bytes_, objectOffset + 8);
		for(uint8_t j = 0; j < meshCount; j++)
		{
			Graphics::DMC3Submesh submesh = readMesh(bytes_, meshesOffset + 80 * static_cast<int64_t>(j));
			if(submesh.mesh.indicies.empty()) continue;
			submesh.mesh.create();
			node.submeshes.push_back(std::move(submesh));
		}
	}
	if(node.submeshes.empty())
	{
		throw std::runtime_error(std::format("Mod: {} has no mesh with triangles.", name_));
	}
	return dmc3MeshId;
}
}
