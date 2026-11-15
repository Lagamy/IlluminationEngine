#include "CollisionEdit.h"

#ifdef EDITOR
#include <algorithm>
#include <cmath>
#include <functional>
#include <set>
#include <stdexcept>

namespace Graphics
{
namespace
{
constexpr size_t MaxUndoSteps = 64;
}

bool rayHitsCollisionTriangle(const glm::vec3& rOrigin_, const glm::vec3& rDirection_, const glm::vec3& rA_, const glm::vec3& rB_, const glm::vec3& rC_, float& rT_)
{
	glm::vec3 edge1 = rB_ - rA_;
	glm::vec3 edge2 = rC_ - rA_;
	glm::vec3 p = glm::cross(rDirection_, edge2);
	float determinant = glm::dot(edge1, p);
	if(std::abs(determinant) < 1e-12f) return false;
	float inverse = 1.0f / determinant;
	glm::vec3 offset = rOrigin_ - rA_;
	float u = glm::dot(offset, p) * inverse;
	if(u < 0.0f || u > 1.0f) return false;
	glm::vec3 q = glm::cross(offset, edge1);
	float v = glm::dot(rDirection_, q) * inverse;
	if(v < 0.0f || u + v > 1.0f) return false;
	float t = glm::dot(edge2, q) * inverse;
	if(t <= 0.0f) return false;
	rT_ = t;
	return true;
}

CollisionEdit CollisionEdit::open(const std::string& rLevel_, Physics::CollisionMesh original_, const CollisionDiff* pDiff_)
{
	CollisionEdit edit;
	edit.level = rLevel_;
	uint32_t originalVertices = static_cast<uint32_t>(original_.vertices.size());
	uint32_t originalTriangles = static_cast<uint32_t>(original_.indices.size() / 3);
	std::vector<uint32_t> values = collisionTriangleValues(original_);
	edit.vertices = original_.vertices;
	std::vector<bool> removed(originalTriangles, false);

	/* The collision the loader made from the diff lists the imported triangles that were not deleted, in order, then the added ones (applyCollisionDiff). */
	if(pDiff_ != nullptr)
	{
		if(pDiff_->vertexCount != originalVertices || pDiff_->triangleCount != originalTriangles) throw std::runtime_error("the HubWorld.collision.bin edit of this level doesn't fit its collision");
		for(const auto& [index, rPosition] : pDiff_->moved) edit.vertices.at(index) = rPosition;
		edit.vertices.insert(edit.vertices.end(), pDiff_->addedVertices.begin(), pDiff_->addedVertices.end());
		for(uint32_t triangle : pDiff_->deletedTriangles) removed.at(triangle) = true;
		for(const auto& [triangle, value] : pDiff_->valueChanges) values.at(triangle) = value;
	}
	for(uint32_t t = 0; t < originalTriangles; t++)
	{
		if(!removed[t]) edit.triangles.push_back({static_cast<int32_t>(t), {original_.indices[t * 3], original_.indices[t * 3 + 1], original_.indices[t * 3 + 2]}, values[t]});
	}
	if(pDiff_ != nullptr)
	{
		for(const CollisionDiff::AddedTriangle& rAdded : pDiff_->addedTriangles)
		{
			for(uint32_t index : rAdded.v)
			{
				if(index >= edit.vertices.size()) throw std::runtime_error("the HubWorld.collision.bin edit of this level doesn't fit its collision");
			}
			edit.triangles.push_back({-1, rAdded.v, rAdded.value});
		}
	}
	edit.original = std::move(original_);
	return edit;
}

bool CollisionEdit::raycast(const glm::vec3& rOrigin_, const glm::vec3& rDirection_, float& rT_, uint32_t& rTriangle_) const
{
	bool hit = false;
	for(uint32_t i = 0; i < this->triangles.size(); i++)
	{
		const std::array<uint32_t, 3>& rCorners = this->triangles[i].v;
		float t = 0.0f;
		if(!rayHitsCollisionTriangle(rOrigin_, rDirection_, this->vertices[rCorners[0]], this->vertices[rCorners[1]], this->vertices[rCorners[2]], t) || (hit && t >= rT_)) continue;
		rT_ = t;
		rTriangle_ = i;
		hit = true;
	}
	return hit;
}

std::vector<uint32_t> CollisionEdit::weld(uint32_t vertex_) const
{
	std::vector<uint32_t> result;
	const glm::vec3& rPosition = this->vertices[vertex_];
	for(uint32_t i = 0; i < this->vertices.size(); i++)
	{
		if(this->vertices[i] == rPosition) result.push_back(i);
	}
	return result;
}

void CollisionEdit::pushUndo()
{
	this->undoStack.push_back({this->vertices, this->triangles});
	if(this->undoStack.size() > MaxUndoSteps) this->undoStack.erase(this->undoStack.begin());
}

bool CollisionEdit::undo()
{
	if(this->undoStack.empty()) return false;
	this->vertices = std::move(this->undoStack.back().vertices);
	this->triangles = std::move(this->undoStack.back().triangles);
	this->undoStack.pop_back();
	this->changed = true;
	return true;
}

void CollisionEdit::deleteTrianglesUsing(const std::vector<uint32_t>& rVertices_)
{
	std::set<uint32_t> set(rVertices_.begin(), rVertices_.end());
	std::erase_if(this->triangles, [&](const Triangle& rTriangle_) { return set.contains(rTriangle_.v[0]) || set.contains(rTriangle_.v[1]) || set.contains(rTriangle_.v[2]); });
	this->changed = true;
}

void CollisionEdit::deleteTriangles(const std::vector<uint32_t>& rTriangles_)
{
	std::vector<uint32_t> sorted = rTriangles_;
	std::sort(sorted.begin(), sorted.end(), std::greater<uint32_t>());
	sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());
	for(uint32_t triangle : sorted)
	{
		if(triangle < this->triangles.size()) this->triangles.erase(this->triangles.begin() + triangle);
	}
	this->changed = true;
}

uint32_t CollisionEdit::addVertex(const glm::vec3& rPosition_)
{
	this->vertices.push_back(rPosition_);
	this->changed = true;
	return static_cast<uint32_t>(this->vertices.size() - 1);
}

void CollisionEdit::addTriangle(uint32_t a_, uint32_t b_, uint32_t c_, uint32_t value_)
{
	this->triangles.push_back({-1, {a_, b_, c_}, value_});
	this->changed = true;
}

void CollisionEdit::setValue(const std::vector<uint32_t>& rTriangles_, uint32_t value_)
{
	for(uint32_t triangle : rTriangles_)
	{
		if(triangle < this->triangles.size()) this->triangles[triangle].value = value_;
	}
	this->changed = true;
}

CollisionDiff CollisionEdit::toDiff() const
{
	CollisionDiff diff;
	diff.vertexCount = static_cast<uint32_t>(this->original.vertices.size());
	diff.triangleCount = static_cast<uint32_t>(this->original.indices.size() / 3);
	for(uint32_t i = 0; i < diff.vertexCount; i++)
	{
		if(this->vertices[i] != this->original.vertices[i]) diff.moved.push_back({i, this->vertices[i]});
	}
	diff.addedVertices.assign(this->vertices.begin() + diff.vertexCount, this->vertices.end());

	std::vector<uint32_t> originalValues = collisionTriangleValues(this->original);
	std::vector<bool> kept(diff.triangleCount, false);
	for(const Triangle& rTriangle : this->triangles)
	{
		if(rTriangle.original < 0)
		{
			diff.addedTriangles.push_back({rTriangle.v, rTriangle.value});
			continue;
		}
		kept[rTriangle.original] = true;
		if(rTriangle.value != originalValues[rTriangle.original]) diff.valueChanges.push_back({static_cast<uint32_t>(rTriangle.original), rTriangle.value});
	}
	for(uint32_t t = 0; t < diff.triangleCount; t++)
	{
		if(!kept[t]) diff.deletedTriangles.push_back(t);
	}
	return diff;
}

Physics::CollisionMesh CollisionEdit::build() const
{
	Physics::CollisionMesh mesh = this->original;
	applyCollisionDiff(mesh, this->toDiff());
	return mesh;
}

void CollisionEdit::revert()
{
	this->pushUndo();
	std::vector<uint32_t> values = collisionTriangleValues(this->original);
	this->vertices = this->original.vertices;
	this->triangles.clear();
	for(uint32_t t = 0; t < values.size(); t++) this->triangles.push_back({static_cast<int32_t>(t), {this->original.indices[t * 3], this->original.indices[t * 3 + 1], this->original.indices[t * 3 + 2]}, values[t]});
	this->changed = true;
}
}
#endif
