#pragma once
#include "BasicMesh.h"
#include "Texture.h"
#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include <glm/glm.hpp>

/*
The HubWorld's geometry edits (Assets/HubWorld.bin; the editor's Geometry subtab makes them): a level's meshes as the disc / glTF import gives them, plus a diff per edited mesh. The disc meshes are never stored; the diff is applied to the freshly imported mesh by Loading::loadLevel while the levels load as a HubWorld.
A mesh is named by its ordinal in Level::editableMeshes (the order the importer makes the pieces in, shared meshes once) and checked by sourceMesh, vertexCount and indexCount of the imported mesh: a diff whose mesh doesn't match is on stderr and skipped, so a changed importer can't scramble a level.
Vertices are never removed (a deleted vertex is its faces gone), so indices stay valid: moved is {vertex index, new position}; deletedFaces are the imported mesh's face numbers (index / 3); addedVertices follow the imported vertices and addedFaces index into imported + added. Normals of the moved and added vertices are made again from the faces that use them (refreshNormals).
HubWorld.bin, little-endian: "SFHB", u32 version (2; 1 is read too, it has no uvMoved or faceTextures), u32 level count, then per level u32 name length, the name, u32 mesh count, then per mesh u32 ordinal, sourceMesh, vertexCount, indexCount; u32 moved count + {u32 index, f32 x y z}; u32 deleted count + u32 faces; u32 added vertex count + {f32 position[3], normal[3], uv[2], color[4]}; u32 added face count + {u32 a b c}; (version 2:) u32 uvMoved count + {u32 index, f32 u v}; u32 texture group count + per group {u32 name length, the External texture's name, u32 face count, u32 faces}.
uvMoved is {vertex index, new uv} of the imported vertices whose uv the Texturing subtab changed (a vertex shared with a face that wasn't edited is copied first, the copy is an added vertex). faceTextures name the faces (numbers in the final face list: the imported faces that are kept in order, then addedFaces) that are drawn with an External texture
in place of their submesh's (the texture is an External texture's name, or "disc:<set>#<texture name>", see findFaceTexture): setMeshTriangles puts the faces of a texture together and gives the mesh an IndexGroup for each (a texture that isn't found leaves its faces with the submesh's own).
*/
struct FaceTextureGroup
{
	std::string texture;
	std::vector<uint32_t> faces;
};

/* One texture of a disc set (a SADX .GVM, a Shadow .TXD): its name inside the set and the pool texture it was decoded into. */
struct DiscTextureTile
{
	std::string name;
	Graphics::Texture* pTexture;
};

/*
Reads the textures of a disc set, set by the game (Loading::registerDiscFaceTextures), which owns the discs; empty = a game without discs. The set is named as the Assets tab drags it: a SADX .GVM's path, or a Shadow .TXD's entry name (the first loaded archive that has the entry); the first call loads
and decodes it, and the set then stays loaded for the run (a face group may be drawn at any time). An unknown set or a file that doesn't read gives no tiles.
*/
inline std::function<std::vector<DiscTextureTile>(const std::string& set_)> discTextureTiles;

/* The texture of a face group's name: an External texture of that name (Loading::findExternalTexture), or "disc:<set>#<name>" the tile of that name in the disc set (discTextureTiles); nullptr if there is none. */
Graphics::Texture* findFaceTexture(const std::string& rName_);

struct MeshDiff
{
	uint32_t ordinal = 0;
	uint32_t sourceMesh = 0;
	uint32_t vertexCount = 0;
	uint32_t indexCount = 0;
	std::vector<std::pair<uint32_t, glm::vec3>> moved;
	std::vector<uint32_t> deletedFaces;
	std::vector<Graphics::Vertex> addedVertices;
	std::vector<std::array<uint32_t, 3>> addedFaces;
	std::vector<std::pair<uint32_t, glm::vec2>> uvMoved;
	std::vector<FaceTextureGroup> faceTextures;

	bool empty() const { return this->moved.empty() && this->deletedFaces.empty() && this->addedVertices.empty() && this->addedFaces.empty() && this->uvMoved.empty() && this->faceTextures.empty(); }
};

struct LevelMeshDiffs
{
	std::string level;
	std::vector<MeshDiff> meshes;
};

/* A mesh as imported (before any diff): kept by the editor for the meshes it edits, to work a diff out against. */
struct MeshOriginal
{
	std::vector<Graphics::Vertex> vertices;
	std::vector<std::array<uint32_t, 3>> faces;
	uint32_t sourceMesh = 0;
};

/* The normal of each touched vertex is made again from the faces that use it (a touched vertex no face uses keeps its normal). */
void refreshNormals(std::vector<Graphics::Vertex>& rVertices_, const std::vector<std::array<uint32_t, 3>>& rFaces_, const std::vector<uint32_t>& rTouched_);

/* A mesh with a diff applied, not yet in the BasicMesh: the vertices, the faces in the final order and for each the number of its faceTextures group (-1 = none). touched are the vertices whose normal is made again. */
struct MeshState
{
	std::vector<Graphics::Vertex> vertices;
	std::vector<std::array<uint32_t, 3>> faces;
	std::vector<int32_t> faceGroup;
	std::vector<uint32_t> touched;
};

/* The imported mesh with the diff applied; false when an index of the diff is out of range. */
bool buildMeshState(const MeshOriginal& rOriginal_, const MeshDiff& rDiff_, MeshState& rState_);

/* The triangles of a standard-layout mesh (the indices in threes; a mesh with no indices is read in order). Throws if the mesh is not in the standard layout. */
MeshOriginal readMeshOriginal(const Graphics::BasicMesh& rMesh_);

/*
Makes the mesh the vertices and triangles (normals of the touched_ vertices are made again from them), its GPU buffers made again. A mesh with no triangle left keeps one degenerate triangle (a mesh with no indices would be drawn as its vertices).
pFaceGroup_ (a group number per face, -1 = none) and pGroupTextures_ (External texture name per group) give faces a texture: the indices are ordered ungrouped first, then each group's, and BasicMesh::groups lists the runs (see IndexGroup).
*/
void setMeshTriangles(Graphics::BasicMesh& rMesh_, std::vector<Graphics::Vertex> vertices_, const std::vector<std::array<uint32_t, 3>>& rFaces_, const std::vector<uint32_t>& rTouched_, const std::vector<int32_t>* pFaceGroup_ = nullptr, const std::vector<std::string>* pGroupTextures_ = nullptr);

/* The mesh with the diff applied; false (the mesh untouched) when the mesh isn't the one the diff was made for. rOriginal_ gets the imported mesh. */
bool applyMeshDiff(Graphics::BasicMesh& rMesh_, const MeshDiff& rDiff_, MeshOriginal& rOriginal_);

std::vector<uint8_t> writeMeshDiffs(const std::vector<LevelMeshDiffs>& rLevels_);
/* Throws std::runtime_error on a file that isn't HubWorld.bin or is cut short. */
std::vector<LevelMeshDiffs> readMeshDiffs(const std::vector<uint8_t>& rBytes_);
