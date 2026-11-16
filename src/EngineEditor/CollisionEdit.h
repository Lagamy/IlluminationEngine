#pragma once

#ifdef EDITOR
#include "CollisionDiff.h"
#include <array>
#include <cstdint>
#include <string>
#include <vector>
#include <glm/glm.hpp>

namespace Graphics
{
/*
A level's collision being edited by the Collision subtab (CollisionPanel): its vertices and triangles in a form that is changed freely, the imported collision it is compared with, and an undo stack. The positions are in the level's space (the collision before the HubWorld place turns it).
As in EditMesh, vertices are only ever added, never removed (a deleted vertex is the triangles that used it gone), so a vertex keeps its number and a CollisionDiff can name it. A triangle knows the imported triangle it is (original, -1 for one the editor added) so that the triangles that are gone can be told. toDiff works out the CollisionDiff of HubWorld.collision.bin; the collision the game uses is made from it (build), so the edit looks the same as after a load.
*/
struct CollisionEdit
{
	struct Triangle
	{
		int32_t original;
		std::array<uint32_t, 3> v;
		uint32_t value;
	};

	std::string level;
	Physics::CollisionMesh original;
	std::vector<glm::vec3> vertices;
	std::vector<Triangle> triangles;
	/* Whether the state differs from the last save (set by every change, cleared by the panel's save). */
	bool changed = false;

	/* Makes the editing state of the imported collision with pDiff_ (the diff the collision has applied; null = as imported) on it. */
	static CollisionEdit open(const std::string& rLevel_, Physics::CollisionMesh original_, const CollisionDiff* pDiff_);

	/* The nearest triangle the ray (in the level's space; direction need not be a unit vector, t_ is in its units) crosses, either side. False if none. */
	bool raycast(const glm::vec3& rOrigin_, const glm::vec3& rDirection_, float& rT_, uint32_t& rTriangle_) const;

	/* Every vertex at the position of vertex_ (itself included). */
	std::vector<uint32_t> weld(uint32_t vertex_) const;

	/* Undo steps: pushUndo before a change (a drag counts as one; it also drops the redo steps), undo goes back one, redo forward again over an undo, discardTo goes back to the state before step depth_ at once (the steps after it are dropped, no redo). */
	void pushUndo();
	bool undo();
	bool redo();
	size_t undoCount() const { return this->undoStack.size(); }
	bool discardTo(size_t depth_);
	/* As EditMesh::dropUnusedAdded. */
	size_t dropUnusedAdded(const std::vector<uint32_t>& rVertices_);
	/* Drops the undo steps after step depth_ (that one stays). */
	void collapseUndo(size_t depth_) { if(this->undoStack.size() > depth_ + 1) this->undoStack.resize(depth_ + 1); this->redoStack.clear(); }

	/* Removes the triangles that use any of the vertices (deleteTriangles: the triangles of the numbers). */
	void deleteTrianglesUsing(const std::vector<uint32_t>& rVertices_);
	void deleteTriangles(const std::vector<uint32_t>& rTriangles_);
	/* Adds a vertex; returns its number. */
	uint32_t addVertex(const glm::vec3& rPosition_);
	/* Adds a triangle of three vertices as given with a value. */
	void addTriangle(uint32_t a_, uint32_t b_, uint32_t c_, uint32_t value_);
	void setValue(const std::vector<uint32_t>& rTriangles_, uint32_t value_);

	/* The imported collision with the edit applied: what the game's Collision gets. */
	Physics::CollisionMesh build() const;
	CollisionDiff toDiff() const;
	/* Back to the imported collision. */
	void revert();

private:
	struct State
	{
		std::vector<glm::vec3> vertices;
		std::vector<Triangle> triangles;
	};
	std::vector<State> undoStack;
	std::vector<State> redoStack;
};

/* Moller-Trumbore against one triangle, both sides; t_ in the units of the direction. */
bool rayHitsCollisionTriangle(const glm::vec3& rOrigin_, const glm::vec3& rDirection_, const glm::vec3& rA_, const glm::vec3& rB_, const glm::vec3& rC_, float& rT_);
}
#endif
