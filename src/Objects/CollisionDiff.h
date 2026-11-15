#pragma once
#include "CollisionMesh.h"
#include <array>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>
#include <glm/glm.hpp>

/*
The HubWorld's collision edits (Assets/HubWorld.collision.bin; the editor's Collision subtab makes them): a level's collision as its importer builds it, plus one diff. The imported collision is never stored; Loading::loadLevel applies the diff right after the level's collision is built, only while the levels load as a HubWorld.
The diff is checked by the imported collision's vertex and triangle counts: one that doesn't match is on stderr and skipped, so a changed importer can't scramble a level. Vertices and triangles are named by their numbers in the imported CollisionMesh, in the level's space (before the HubWorld place turns it).
Vertices are never removed (a deleted vertex is its triangles gone), so the numbers stay valid. A triangle's value is what decides what it is (CollisionMesh::triangleValue).

HubWorld.collision.bin, little-endian: "SFHC", u32 version (1), u32 level count, then per level u32 name length, the name, u32 vertexCount, u32 triangleCount (of the imported collision); u32 moved count + {u32 vertex, f32 x y z}; u32 deleted count + u32 triangles; u32 added vertex count + {f32 x y z}; u32 added triangle count + {u32 a b c (into imported + added vertices), u32 value}; u32 value change count + {u32 triangle (imported number), u32 value}.
*/
struct CollisionDiff
{
	struct AddedTriangle
	{
		std::array<uint32_t, 3> v;
		uint32_t value;
	};

	uint32_t vertexCount = 0;
	uint32_t triangleCount = 0;
	std::vector<std::pair<uint32_t, glm::vec3>> moved;
	std::vector<uint32_t> deletedTriangles;
	std::vector<glm::vec3> addedVertices;
	std::vector<AddedTriangle> addedTriangles;
	std::vector<std::pair<uint32_t, uint32_t>> valueChanges;

	bool empty() const { return this->moved.empty() && this->deletedTriangles.empty() && this->addedVertices.empty() && this->addedTriangles.empty() && this->valueChanges.empty(); }
};

struct LevelCollisionDiff
{
	std::string level;
	CollisionDiff diff;
};

/* The value of every triangle (CollisionMesh::triangleValue for all of them at once). */
std::vector<uint32_t> collisionTriangleValues(const Physics::CollisionMesh& rMesh_);

/*
The collision with the diff applied; false (the mesh untouched) when the mesh isn't the one the diff was made for or an index of the diff is out of range.
The kept triangles stay in order and the added ones follow. A mesh with a value per triangle (Shadow) keeps them in triangleAttributes (and triangleMaterials, 0 for an added triangle); one with the values in its pieces (SADX) gets its pieces made again: a new piece starts where the piece or the value changes, its flags the value, its sphere made from its triangles. An added triangle is in no old piece.
*/
bool applyCollisionDiff(Physics::CollisionMesh& rMesh_, const CollisionDiff& rDiff_);

std::vector<uint8_t> writeCollisionDiffs(const std::vector<LevelCollisionDiff>& rLevels_);
/* Throws std::runtime_error on a file that isn't HubWorld.collision.bin or is cut short. */
std::vector<LevelCollisionDiff> readCollisionDiffs(const std::vector<uint8_t>& rBytes_);
