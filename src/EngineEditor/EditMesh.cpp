#include "EditMesh.h"

#ifdef EDITOR
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <stdexcept>

namespace Graphics
{
namespace
{
constexpr size_t MaxUndoSteps = 64;
}

EditMesh EditMesh::open(const std::string& rLevel_, uint32_t ordinal_, MeshOriginal original_, const MeshDiff* pDiff_)
{
	EditMesh mesh;
	mesh.level = rLevel_;
	mesh.ordinal = ordinal_;

	/* The mesh the loader made from the diff lists the imported faces that were not deleted, in order, then the added ones (buildMeshState). */
	MeshState state;
	MeshDiff none;
	if(!buildMeshState(original_, pDiff_ != nullptr ? *pDiff_ : none, state)) throw std::runtime_error("the HubWorld.bin edit of this mesh doesn't fit it");
	std::vector<bool> removed(original_.faces.size(), false);
	if(pDiff_ != nullptr)
	{
		for(uint32_t face : pDiff_->deletedFaces) removed[face] = true;
		for(const FaceTextureGroup& rGroup : pDiff_->faceTextures) mesh.textures.push_back(rGroup.texture);
	}
	std::vector<int32_t> survivors;
	for(size_t i = 0; i < removed.size(); i++)
	{
		if(!removed[i]) survivors.push_back(static_cast<int32_t>(i));
	}
	mesh.vertices = std::move(state.vertices);
	for(size_t i = 0; i < state.faces.size(); i++) mesh.faces.push_back({i < survivors.size() ? survivors[i] : -1, state.faces[i], state.faceGroup[i]});
	mesh.original = std::move(original_);
	return mesh;
}

bool EditMesh::raycast(const glm::vec3& rOrigin_, const glm::vec3& rDirection_, float& rT_, uint32_t& rFace_) const
{
	bool hit = false;
	for(uint32_t i = 0; i < this->faces.size(); i++)
	{
		const std::array<uint32_t, 3>& rFace = this->faces[i].v;
		const glm::vec3& rA = this->vertices[rFace[0]].position;
		glm::vec3 edge1 = this->vertices[rFace[1]].position - rA;
		glm::vec3 edge2 = this->vertices[rFace[2]].position - rA;

		/* Moller-Trumbore, both sides. */
		glm::vec3 p = glm::cross(rDirection_, edge2);
		float determinant = glm::dot(edge1, p);
		if(std::abs(determinant) < 1e-12f) continue;
		float inverse = 1.0f / determinant;
		glm::vec3 offset = rOrigin_ - rA;
		float u = glm::dot(offset, p) * inverse;
		if(u < 0.0f || u > 1.0f) continue;
		glm::vec3 q = glm::cross(offset, edge1);
		float v = glm::dot(rDirection_, q) * inverse;
		if(v < 0.0f || u + v > 1.0f) continue;
		float t = glm::dot(edge2, q) * inverse;
		if(t <= 0.0f || (hit && t >= rT_)) continue;
		rT_ = t;
		rFace_ = i;
		hit = true;
	}
	return hit;
}

std::vector<uint32_t> EditMesh::weld(uint32_t vertex_) const
{
	std::vector<uint32_t> result;
	const glm::vec3& rPosition = this->vertices[vertex_].position;
	for(uint32_t i = 0; i < this->vertices.size(); i++)
	{
		if(this->vertices[i].position == rPosition) result.push_back(i);
	}
	return result;
}

std::vector<uint32_t> EditMesh::island(uint32_t face_) const
{
	/*
	Faces are neighbours when they share an edge, by position (the vertices of a corner are split per uv, so the indices don't tell). Edges are keyed by their two positions in sorted order; the group grows from the face over neighbours with the same texture.
	*/
	using Key = std::pair<std::array<float, 3>, std::array<float, 3>>;
	auto keyOf = [&](uint32_t a_, uint32_t b_)
	{
		const glm::vec3& rA = this->vertices[a_].position;
		const glm::vec3& rB = this->vertices[b_].position;
		std::array<float, 3> first{rA.x, rA.y, rA.z};
		std::array<float, 3> second{rB.x, rB.y, rB.z};
		return first < second ? Key{first, second} : Key{second, first};
	};
	std::map<Key, std::vector<uint32_t>> byEdge;
	for(uint32_t face = 0; face < this->faces.size(); face++)
	{
		for(int i = 0; i < 3; i++) byEdge[keyOf(this->faces[face].v[i], this->faces[face].v[(i + 1) % 3])].push_back(face);
	}

	std::vector<uint32_t> result{face_};
	std::set<uint32_t> seen{face_};
	for(size_t next = 0; next < result.size(); next++)
	{
		const Face& rFace = this->faces[result[next]];
		for(int i = 0; i < 3; i++)
		{
			for(uint32_t neighbour : byEdge[keyOf(rFace.v[i], rFace.v[(i + 1) % 3])])
			{
				if(this->faces[neighbour].texture == this->faces[face_].texture && seen.insert(neighbour).second) result.push_back(neighbour);
			}
		}
	}
	return result;
}

void EditMesh::pushUndo()
{
	this->undoStack.push_back({this->vertices, this->faces, this->textures});
	this->redoStack.clear();
	if(this->undoStack.size() > MaxUndoSteps) this->undoStack.erase(this->undoStack.begin());
}

bool EditMesh::undo()
{
	if(this->undoStack.empty()) return false;
	this->redoStack.push_back({this->vertices, this->faces, this->textures});
	this->vertices = std::move(this->undoStack.back().vertices);
	this->faces = std::move(this->undoStack.back().faces);
	this->textures = std::move(this->undoStack.back().textures);
	this->undoStack.pop_back();
	this->changed = true;
	return true;
}

bool EditMesh::discardTo(size_t depth_)
{
	if(depth_ >= this->undoStack.size()) return false;
	this->vertices = std::move(this->undoStack[depth_].vertices);
	this->faces = std::move(this->undoStack[depth_].faces);
	this->textures = std::move(this->undoStack[depth_].textures);
	this->undoStack.resize(depth_);
	this->redoStack.clear();
	this->changed = true;
	return true;
}

bool EditMesh::redo()
{
	if(this->redoStack.empty()) return false;
	this->undoStack.push_back({this->vertices, this->faces, this->textures});
	this->vertices = std::move(this->redoStack.back().vertices);
	this->faces = std::move(this->redoStack.back().faces);
	this->textures = std::move(this->redoStack.back().textures);
	this->redoStack.pop_back();
	this->changed = true;
	return true;
}

void EditMesh::deleteFacesUsing(const std::vector<uint32_t>& rVertices_)
{
	std::set<uint32_t> set(rVertices_.begin(), rVertices_.end());
	std::erase_if(this->faces, [&](const Face& rFace_) { return set.contains(rFace_.v[0]) || set.contains(rFace_.v[1]) || set.contains(rFace_.v[2]); });
	this->changed = true;
}

void EditMesh::deleteFaces(const std::vector<uint32_t>& rFaces_)
{
	std::vector<bool> removed(this->faces.size(), false);
	for(uint32_t face : rFaces_)
	{
		if(face < removed.size()) removed[face] = true;
	}
	size_t index = 0;
	std::erase_if(this->faces, [&](const Face&) { return removed[index++]; });
	this->changed = true;
}

uint32_t EditMesh::addVertex(const glm::vec3& rPosition_, uint32_t onFace_)
{
	Vertex vertex = this->vertices[this->faces[onFace_].v[0]];
	vertex.position = rPosition_;
	this->vertices.push_back(vertex);
	this->changed = true;
	return static_cast<uint32_t>(this->vertices.size() - 1);
}

void EditMesh::addFace(uint32_t a_, uint32_t b_, uint32_t c_)
{
	this->faces.push_back({-1, {a_, b_, c_}, -1});
	this->changed = true;
}

void EditMesh::setTexture(const std::vector<uint32_t>& rFaces_, const std::string& rName_)
{
	int32_t index = -1;
	if(!rName_.empty())
	{
		auto found = std::find(this->textures.begin(), this->textures.end(), rName_);
		if(found == this->textures.end())
		{
			this->textures.push_back(rName_);
			found = this->textures.end() - 1;
		}
		index = static_cast<int32_t>(found - this->textures.begin());
	}
	for(uint32_t face : rFaces_)
	{
		if(face < this->faces.size()) this->faces[face].texture = index;
	}
	this->changed = true;
}

std::vector<uint32_t> EditMesh::isolate(const std::vector<uint32_t>& rFaces_)
{
	std::set<uint32_t> selected(rFaces_.begin(), rFaces_.end());
	std::set<uint32_t> shared;
	for(uint32_t face = 0; face < this->faces.size(); face++)
	{
		if(selected.contains(face)) continue;
		for(uint32_t corner : this->faces[face].v) shared.insert(corner);
	}
	std::map<uint32_t, uint32_t> copies;
	std::set<uint32_t> result;
	for(uint32_t face : selected)
	{
		if(face >= this->faces.size()) continue;
		for(uint32_t& rCorner : this->faces[face].v)
		{
			if(shared.contains(rCorner))
			{
				auto found = copies.find(rCorner);
				if(found == copies.end())
				{
					this->vertices.push_back(this->vertices[rCorner]);
					found = copies.emplace(rCorner, static_cast<uint32_t>(this->vertices.size() - 1)).first;
				}
				rCorner = found->second;
			}
			result.insert(rCorner);
		}
	}
	this->changed = true;
	return std::vector<uint32_t>(result.begin(), result.end());
}

void EditMesh::upload(BasicMesh& rMesh_) const
{
	std::vector<std::array<uint32_t, 3>> triangles;
	std::vector<int32_t> groups;
	std::vector<std::string> groupTextures = this->textures;
	if(!this->ownTexture.empty()) groupTextures.push_back(this->ownTexture);
	for(const Face& rFace : this->faces)
	{
		triangles.push_back(rFace.v);
		groups.push_back(rFace.texture < 0 && !this->ownTexture.empty() ? static_cast<int32_t>(this->textures.size()) : rFace.texture);
	}
	std::vector<uint32_t> touched;
	for(uint32_t i = 0; i < this->vertices.size(); i++)
	{
		if(i >= this->original.vertices.size() || this->vertices[i].position != this->original.vertices[i].position) touched.push_back(i);
	}
	setMeshTriangles(rMesh_, this->vertices, triangles, touched, &groups, &groupTextures);
}

MeshDiff EditMesh::toDiff() const
{
	MeshDiff diff;
	diff.ordinal = this->ordinal;
	diff.sourceMesh = this->original.sourceMesh;
	diff.vertexCount = static_cast<uint32_t>(this->original.vertices.size());
	diff.indexCount = static_cast<uint32_t>(this->original.faces.size() * 3);
	for(uint32_t i = 0; i < this->original.vertices.size(); i++)
	{
		if(this->vertices[i].position != this->original.vertices[i].position) diff.moved.push_back({i, this->vertices[i].position});
	}
	std::vector<bool> kept(this->original.faces.size(), false);
	for(const Face& rFace : this->faces)
	{
		if(rFace.original >= 0) kept[rFace.original] = true;
		else diff.addedFaces.push_back(rFace.v);
	}
	for(uint32_t i = 0; i < kept.size(); i++)
	{
		if(!kept[i]) diff.deletedFaces.push_back(i);
	}
	diff.addedVertices.assign(this->vertices.begin() + static_cast<std::ptrdiff_t>(this->original.vertices.size()), this->vertices.end());
	for(uint32_t i = 0; i < this->original.vertices.size(); i++)
	{
		if(this->vertices[i].uv != this->original.vertices[i].uv) diff.uvMoved.push_back({i, this->vertices[i].uv});
	}
	for(size_t texture = 0; texture < this->textures.size(); texture++)
	{
		FaceTextureGroup group{this->textures[texture], {}};
		for(uint32_t face = 0; face < this->faces.size(); face++)
		{
			if(this->faces[face].texture == static_cast<int32_t>(texture)) group.faces.push_back(face);
		}
		if(!group.faces.empty()) diff.faceTextures.push_back(std::move(group));
	}
	return diff;
}

void EditMesh::revert()
{
	this->pushUndo();
	this->vertices = this->original.vertices;
	this->faces.clear();
	this->textures.clear();
	for(uint32_t i = 0; i < this->original.faces.size(); i++) this->faces.push_back({static_cast<int32_t>(i), this->original.faces[i], -1});
	this->changed = true;
}
}
#endif
