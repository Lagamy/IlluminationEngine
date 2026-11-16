#pragma once
#include <cmath>

#ifdef EDITOR
#include "EditMesh.h"
#include "CollisionEdit.h"
#include <array>
#include <cstdint>
#include <vector>
#include <glm/glm.hpp>

namespace Graphics
{
/*
What TriangleEditor (the editing logic the Geometry and Collision subtabs share: picking, boxes, moving, deleting, drawing and extruding triangles, merging, undo) needs of the thing it edits: vertices with a position, triangles of three vertex numbers, adding, deleting and undo. EditMesh and CollisionEdit stay as they are; MeshSurface and CollisionSurface are thin views of them that the panels hold and hand out (GeometryPanel::surface, CollisionPanel::surface).
Vertices are only ever added, never removed (as in both edits): a deleted vertex is the triangles that used it gone. The others a triangle or vertex carries (uv, colour, normal, face texture, collision value) are not the editor's business: a new vertex or triangle takes them from a vertex or triangle given as "like" (None = the defaults), by the surface's own rules.
*/
struct TriangleSurface
{
	static constexpr uint32_t None = 0xFFFFFFFFu;

	virtual ~TriangleSurface() = default;
	virtual uint32_t vertexCount() const = 0;
	virtual glm::vec3& position(uint32_t vertex_) = 0;
	virtual uint32_t triangleCount() const = 0;
	virtual std::array<uint32_t, 3>& corners(uint32_t triangle_) = 0;
	/* How many vertices the imported mesh had (the ones from there on are the editor's: they are marked). */
	virtual uint32_t originalVertexCount() const = 0;
	/* A new vertex at the position with what vertex like_ has apart from it; returns its number. */
	virtual uint32_t addVertex(const glm::vec3& rPosition_, uint32_t like_) = 0;
	/* Gives vertex_ the texture coordinate its position would have if the face (a_, b_, reference_) went on to it (the face's mapping from positions to coordinates, extended); nothing for a surface with no coordinates. */
	virtual void extendUv(uint32_t vertex_, uint32_t a_, uint32_t b_, uint32_t reference_) {}
	/* A new triangle; what is not the corners is taken from triangle like_, or, with None, the surface's default (a face with the submesh's own texture, a collision triangle with the panel's Value). */
	virtual void addTriangle(uint32_t a_, uint32_t b_, uint32_t c_, uint32_t like_) = 0;
	/* Gives vertex_ what lies between vertices a_ and b_ (t_ from a_ to b_) apart from the position: the uv of a vertex made on an edge. */
	virtual void blendVertex(uint32_t vertex_, uint32_t a_, uint32_t b_, float t_) {}
	virtual void deleteTriangles(const std::vector<uint32_t>& rTriangles_) = 0;
	virtual void deleteTrianglesUsing(const std::vector<uint32_t>& rVertices_) = 0;
	/* The nearest triangle the ray (in the surface's space) crosses, either side; t_ in the units of the direction. */
	virtual bool raycast(const glm::vec3& rOrigin_, const glm::vec3& rDirection_, float& rT_, uint32_t& rTriangle_) const = 0;

	/* Undo steps (a drag counts as one): pushUndo before a change, undo goes back one, redo forward again, discardTo goes back to before step depth_ at once without a redo. */
	virtual void pushUndo() = 0;
	virtual bool undo() = 0;
	virtual bool redo() = 0;
	virtual size_t undoCount() const = 0;
	virtual bool discardTo(size_t depth_) = 0;
	/* Drops the undo steps after step depth_ (that one stays: it is the state before the work they were part of), so one undo takes all of it back. */
	virtual void collapseUndo(size_t depth_) = 0;
	/* Removes the listed vertices the editor added that no triangle uses (the others get new numbers); returns how many went. */
	virtual size_t dropUnusedAdded(const std::vector<uint32_t>& rVertices_) = 0;
	/* Whether a new triangle next to an existing one takes that one's texture (a mesh does; a collision triangle takes the Value field). */
	virtual bool continuesTexture() const { return false; }
	virtual void markChanged() = 0;
	/* Back to the imported state (an undo step). */
	virtual void revert() = 0;
};

/* An EditMesh seen as a TriangleSurface (a face's own texture is taken from the face given as like). */
struct MeshSurface : TriangleSurface
{
	EditMesh* pEdit = nullptr;

	uint32_t vertexCount() const override { return static_cast<uint32_t>(this->pEdit->vertices.size()); }
	glm::vec3& position(uint32_t vertex_) override { return this->pEdit->vertices[vertex_].position; }
	uint32_t triangleCount() const override { return static_cast<uint32_t>(this->pEdit->faces.size()); }
	std::array<uint32_t, 3>& corners(uint32_t triangle_) override { return this->pEdit->faces[triangle_].v; }
	uint32_t originalVertexCount() const override { return static_cast<uint32_t>(this->pEdit->original.vertices.size()); }
	uint32_t addVertex(const glm::vec3& rPosition_, uint32_t like_) override
	{
		/* Not the name Vertex: that is the panels' mode. */
		auto made = this->pEdit->vertices[like_];
		made.position = rPosition_;
		this->pEdit->vertices.push_back(made);
		this->pEdit->changed = true;
		return static_cast<uint32_t>(this->pEdit->vertices.size() - 1);
	}
	void addTriangle(uint32_t a_, uint32_t b_, uint32_t c_, uint32_t like_) override
	{
		int32_t texture = like_ < this->pEdit->faces.size() ? this->pEdit->faces[like_].texture : -1;
		this->pEdit->addFace(a_, b_, c_);
		this->pEdit->faces.back().texture = texture;
	}
	void blendVertex(uint32_t vertex_, uint32_t a_, uint32_t b_, float t_) override
	{
		this->pEdit->vertices[vertex_].uv = glm::mix(this->pEdit->vertices[a_].uv, this->pEdit->vertices[b_].uv, t_);
	}
	void extendUv(uint32_t vertex_, uint32_t a_, uint32_t b_, uint32_t reference_) override
	{
		const auto& rVertices = this->pEdit->vertices;
		glm::vec3 first = rVertices[b_].position - rVertices[a_].position;
		glm::vec3 second = rVertices[reference_].position - rVertices[a_].position;
		glm::vec3 offset = rVertices[vertex_].position - rVertices[a_].position;

		/* The offset in the face's two edge directions (least squares, since the new position is not in the face's plane). */
		float aa = glm::dot(first, first);
		float ab = glm::dot(first, second);
		float bb = glm::dot(second, second);
		float determinant = aa * bb - ab * ab;
		if(std::abs(determinant) < 1e-12f) return;
		float x = (glm::dot(offset, first) * bb - glm::dot(offset, second) * ab) / determinant;
		float y = (glm::dot(offset, second) * aa - glm::dot(offset, first) * ab) / determinant;
		glm::vec2 base = rVertices[a_].uv;
		this->pEdit->vertices[vertex_].uv = base + x * (rVertices[b_].uv - base) + y * (rVertices[reference_].uv - base);
		this->pEdit->changed = true;
	}
	void deleteTriangles(const std::vector<uint32_t>& rTriangles_) override { this->pEdit->deleteFaces(rTriangles_); }
	void deleteTrianglesUsing(const std::vector<uint32_t>& rVertices_) override { this->pEdit->deleteFacesUsing(rVertices_); }
	bool raycast(const glm::vec3& rOrigin_, const glm::vec3& rDirection_, float& rT_, uint32_t& rTriangle_) const override { return this->pEdit->raycast(rOrigin_, rDirection_, rT_, rTriangle_); }
	void pushUndo() override { this->pEdit->pushUndo(); }
	bool undo() override { return this->pEdit->undo(); }
	bool redo() override { return this->pEdit->redo(); }
	size_t undoCount() const override { return this->pEdit->undoCount(); }
	bool discardTo(size_t depth_) override { return this->pEdit->discardTo(depth_); }
	void collapseUndo(size_t depth_) override { this->pEdit->collapseUndo(depth_); }
	size_t dropUnusedAdded(const std::vector<uint32_t>& rVertices_) override { return this->pEdit->dropUnusedAdded(rVertices_); }
	bool continuesTexture() const override { return true; }
	void markChanged() override { this->pEdit->changed = true; }
	void revert() override { this->pEdit->revert(); }
};

/* A CollisionEdit seen as a TriangleSurface (a triangle's value is the Value field's, or the value of the triangle given as like). */
struct CollisionSurface : TriangleSurface
{
	CollisionEdit* pEdit = nullptr;
	/* The Value field of the panel (CollisionPanel::value). */
	const uint32_t* pValue = nullptr;

	uint32_t vertexCount() const override { return static_cast<uint32_t>(this->pEdit->vertices.size()); }
	glm::vec3& position(uint32_t vertex_) override { return this->pEdit->vertices[vertex_]; }
	uint32_t triangleCount() const override { return static_cast<uint32_t>(this->pEdit->triangles.size()); }
	std::array<uint32_t, 3>& corners(uint32_t triangle_) override { return this->pEdit->triangles[triangle_].v; }
	uint32_t originalVertexCount() const override { return static_cast<uint32_t>(this->pEdit->original.vertices.size()); }
	uint32_t addVertex(const glm::vec3& rPosition_, uint32_t like_) override { return this->pEdit->addVertex(rPosition_); }
	void addTriangle(uint32_t a_, uint32_t b_, uint32_t c_, uint32_t like_) override
	{
		uint32_t value = like_ < this->pEdit->triangles.size() ? this->pEdit->triangles[like_].value : *this->pValue;
		this->pEdit->addTriangle(a_, b_, c_, value);
	}
	void deleteTriangles(const std::vector<uint32_t>& rTriangles_) override { this->pEdit->deleteTriangles(rTriangles_); }
	void deleteTrianglesUsing(const std::vector<uint32_t>& rVertices_) override { this->pEdit->deleteTrianglesUsing(rVertices_); }
	bool raycast(const glm::vec3& rOrigin_, const glm::vec3& rDirection_, float& rT_, uint32_t& rTriangle_) const override { return this->pEdit->raycast(rOrigin_, rDirection_, rT_, rTriangle_); }
	void pushUndo() override { this->pEdit->pushUndo(); }
	bool undo() override { return this->pEdit->undo(); }
	bool redo() override { return this->pEdit->redo(); }
	size_t undoCount() const override { return this->pEdit->undoCount(); }
	bool discardTo(size_t depth_) override { return this->pEdit->discardTo(depth_); }
	void collapseUndo(size_t depth_) override { this->pEdit->collapseUndo(depth_); }
	size_t dropUnusedAdded(const std::vector<uint32_t>& rVertices_) override { return this->pEdit->dropUnusedAdded(rVertices_); }
	void markChanged() override { this->pEdit->changed = true; }
	void revert() override { this->pEdit->revert(); }
};
}
#endif
