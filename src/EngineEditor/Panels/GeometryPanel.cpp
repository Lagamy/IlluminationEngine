#include "GeometryPanel.h"

#ifdef EDITOR
#include "BoxSelect.h"
#include "Editor.h"
#include "Globals.h"
#include "API.h"
#include "Level.h"
#include "HubWorld.h"
#include "Prefab.h"
#include "ExternalPrefabs.h"
#include "imgui.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <format>
#include <optional>
#include <set>
#include <stdexcept>
#include <glm/ext/matrix_transform.hpp>

namespace Graphics
{
namespace
{
/* The wireframe is not drawn for a mesh with more faces than this (every frame's cost, and a screen of lines). */
constexpr size_t MaxWireframeFaces = 20000;
/* How far (in the mesh's units) an extruded vertex starts from what it comes from. */
constexpr float ExtrudeOffset = 0.25f;
constexpr ImU32 WireColor = IM_COL32(200, 200, 200, 70);
constexpr ImU32 SelectedColor = IM_COL32(255, 200, 40, 255);
constexpr ImU32 SelectedFill = IM_COL32(255, 160, 40, 90);
/* A right press let go within this time and distance of where it went down is a click, not a camera look. */
constexpr double RightClickSeconds = 0.3;
constexpr double RightClickPixels = 4.0;
constexpr ImU32 AddedColor = IM_COL32(60, 220, 255, 255);
constexpr ImU32 DrawColor = IM_COL32(60, 110, 255, 255);
/* The range of GeometryPanel::mergePixels (the slider). */
constexpr float MinMergePixels = 2.0f;
constexpr float MaxMergePixels = 60.0f;
/* Of the vertices under a click the nearest this many are tested for being hidden; the box selection tests the vertices in the box only up to this many (a ray each, against every face), beyond it everything in the box is taken. */
constexpr size_t MaxVisibilityTries = 6;
constexpr size_t MaxVisibilityTests = 600;
/* Merging onto the middle of an edge looks at every edge of the mesh, so it is done only for this many moved positions or fewer. */
constexpr size_t MaxEdgeMergeMovers = 16;
constexpr ImU32 MergeColor = IM_COL32(60, 230, 90, 255);

Level* levelNamed(const std::string& rName_)
{
	for(uint32_t i = 0; i < Globals::Game::levels.objects.size(); i++)
	{
		if(Globals::Game::levels.alive[i] && Globals::Game::levels.names[i] == rName_) return &Globals::Game::levels.objects[i];
	}
	return nullptr;
}

/* The window position of a world point (the whole window is the viewport); false behind the camera. */
bool project(const Camera& rCamera_, const glm::vec3& rWorld_, ImVec2& rOut_)
{
	ImVec2 size = ImGui::GetIO().DisplaySize;
	glm::vec4 clip = rCamera_.projection * rCamera_.view * glm::vec4(rWorld_, 1.0f);
	if(clip.w <= 0.0f) return false;
	rOut_ = ImVec2((clip.x / clip.w * 0.5f + 0.5f) * size.x, (1.0f - (clip.y / clip.w * 0.5f + 0.5f)) * size.y);
	return true;
}

float distanceToSegment(const glm::vec3& rPoint_, const glm::vec3& rA_, const glm::vec3& rB_)
{
	glm::vec3 ab = rB_ - rA_;
	float length = glm::dot(ab, ab);
	float t = length > 0.0f ? std::clamp(glm::dot(rPoint_ - rA_, ab) / length, 0.0f, 1.0f) : 0.0f;
	return glm::distance(rPoint_, rA_ + ab * t);
}
}

void GeometryPanel::checkLoad()
{
	if(Globals::Game::hubWorld.loadCount == this->seenLoadCount) return;
	this->seenLoadCount = Globals::Game::hubWorld.loadCount;
	this->sessions.clear();
	this->hasOpen = false;
	this->hasHit = false;
	this->pendingFaces.clear();
	this->drawCorners.clear();
	this->clearSelection();

	/* The entity is gone with the levels: its copy goes without touching it. */
	this->hasEntity = false;
	this->source = Source::Level;
	this->editPools.clear();
}

Entity* GeometryPanel::liveEntity()
{
	if(!this->hasEntity) return nullptr;
	PoolId id = this->entityCopy.entityId;
	if(id.id >= Globals::Game::entities.objects.size() || !Globals::Game::entities.alive[id.id] || Globals::Game::entities.generation[id.id] != id.generation) return nullptr;
	return &Globals::Game::entities.objects[id.id];
}

glm::mat4 GeometryPanel::entityMatrix(const Entity& rEntity_) const
{
	return glm::translate(glm::mat4(1.0f), rEntity_.renderPosition()) * rEntity_.renderRotation() * glm::scale(glm::mat4(1.0f), rEntity_.scale);
}

bool GeometryPanel::entityChanged() const
{
	return std::any_of(this->sessions.begin(), this->sessions.end(), [](const auto& rEntry_) { return rEntry_.first.first.empty() && rEntry_.second.changed; });
}

bool GeometryPanel::resolve(BasicMesh*& rpMesh_, glm::mat4& rToWorld_)
{
	if(!this->hasOpen) return false;
	if(this->openKey.first.empty())
	{
		Entity* pEntity = this->liveEntity();
		if(pEntity == nullptr || this->openKey.second >= this->entityCopy.pModel->submeshes.size()) return false;
		rpMesh_ = &this->editPools.meshes[this->entityCopy.pModel->submeshes[this->openKey.second].meshId];
		rToWorld_ = this->entityMatrix(*pEntity);
		return true;
	}
	Level* pLevel = levelNamed(this->openKey.first);
	if(pLevel == nullptr) return false;
	std::vector<std::pair<BasicMesh*, glm::mat4>> meshes;
	pLevel->editableMeshes(meshes);
	if(this->openKey.second >= meshes.size()) return false;
	rpMesh_ = meshes[this->openKey.second].first;
	rToWorld_ = pLevel->hubWorldMatrix() * glm::translate(glm::mat4(1.0f), pLevel->position) * meshes[this->openKey.second].second;
	return true;
}

EditMesh* GeometryPanel::current()
{
	if(!this->hasOpen) return nullptr;
	auto found = this->sessions.find(this->openKey);
	return found == this->sessions.end() ? nullptr : &found->second;
}

void GeometryPanel::toggleIsolate()
{
	if(!this->isolate && !this->hasOpen)
	{
		this->message = "Isolate needs an open mesh: click one first.";
		return;
	}
	this->isolate = !this->isolate;
	this->message = this->isolate ? "Only the open mesh is shown (I again shows everything)." : "Everything is shown.";
}

bool GeometryPanel::center(glm::vec3& rWorld_)
{
	EditMesh* pEdit = this->current();
	BasicMesh* pMesh = nullptr;
	glm::mat4 toWorld;
	if(pEdit == nullptr || pEdit->vertices.empty() || !this->resolve(pMesh, toWorld)) return false;
	glm::vec3 low = pEdit->vertices[0].position;
	glm::vec3 high = low;
	for(const auto& rVertex : pEdit->vertices)
	{
		low = glm::min(low, rVertex.position);
		high = glm::max(high, rVertex.position);
	}
	rWorld_ = glm::vec3(toWorld * glm::vec4((low + high) * 0.5f, 1.0f));
	return true;
}

void GeometryPanel::clearSelection()
{
	this->pickedVertices.clear();
	this->pickedEdges.clear();
	this->pickedFaces.clear();
}

void GeometryPanel::open(const std::string& rLevel_, uint32_t ordinal_)
{
	this->openKey = {rLevel_, ordinal_};
	this->hasOpen = true;
	this->hasHit = false;
	this->pendingFaces.clear();
	this->drawCorners.clear();
	this->clearSelection();
	if(this->sessions.contains(this->openKey)) return;

	BasicMesh* pMesh = nullptr;
	glm::mat4 toWorld;
	if(!this->resolve(pMesh, toWorld))
	{
		this->hasOpen = false;
		return;
	}

	/*
	The imported mesh is the one the loader kept when it applied a diff, else the mesh as it is (it has none applied). The diff is only used to label the faces when the loader applied it (the original was kept): one that didn't fit the mesh was skipped and the mesh is as imported.
	*/
	if(rLevel_.empty())
	{
		const MeshDiff* pEntityDiff = nullptr;
		for(const MeshDiff& rDiff : this->entityCopy.diffs)
		{
			if(rDiff.ordinal == ordinal_) pEntityDiff = &rDiff;
		}
		EditMesh edit = EditMesh::open(rLevel_, ordinal_, this->entityCopy.baked.originals[ordinal_], pEntityDiff);
		edit.ownTexture = this->entityCopy.baked.ownTextures[ordinal_];
		this->sessions.emplace(this->openKey, std::move(edit));
		return;
	}
	HubWorld& rHubWorld = Globals::Game::hubWorld;
	auto original = rHubWorld.meshOriginals.find(this->openKey);
	const MeshDiff* pDiff = nullptr;
	if(original != rHubWorld.meshOriginals.end())
	{
		for(const LevelMeshDiffs& rLevel : rHubWorld.meshEdits)
		{
			if(rLevel.level != rLevel_) continue;
			for(const MeshDiff& rDiff : rLevel.meshes)
			{
				if(rDiff.ordinal == ordinal_) pDiff = &rDiff;
			}
		}
	}
	else original = rHubWorld.meshOriginals.emplace(this->openKey, readMeshOriginal(*pMesh)).first;
	this->sessions.emplace(this->openKey, EditMesh::open(rLevel_, ordinal_, original->second, pDiff));
}

std::vector<uint32_t> GeometryPanel::selectedVertices()
{
	std::vector<uint32_t> result;
	EditMesh* pMesh = this->current();
	if(pMesh == nullptr) return result;

	/* One pass over the vertices against the positions picked (welding each picked vertex alone is too slow for a box selection of thousands). */
	std::set<PositionKey> positions;
	for(uint32_t vertex : this->pickedVertices) positions.insert(positionKey(pMesh->vertices[vertex].position));
	for(uint32_t face : this->pickedFaces)
	{
		for(uint32_t vertex : pMesh->faces[face].v) positions.insert(positionKey(pMesh->vertices[vertex].position));
	}
	if(positions.empty()) return result;
	for(uint32_t vertex = 0; vertex < pMesh->vertices.size(); vertex++)
	{
		if(positions.contains(positionKey(pMesh->vertices[vertex].position))) result.push_back(vertex);
	}
	return result;
}

bool GeometryPanel::handle(glm::vec3& rWorld_)
{
	BasicMesh* pMesh = nullptr;
	glm::mat4 toWorld;
	EditMesh* pEdit = this->current();
	if(pEdit == nullptr || !this->resolve(pMesh, toWorld)) return false;
	std::vector<uint32_t> vertices = this->selectedVertices();
	if(vertices.empty()) return false;

	/* The centre of the distinct positions (a welded corner counts once). */
	std::set<PositionKey> seen;
	glm::vec3 centre(0.0f);
	size_t distinct = 0;
	for(uint32_t vertex : vertices)
	{
		const glm::vec3& rPosition = pEdit->vertices[vertex].position;
		if(!seen.insert(positionKey(rPosition)).second) continue;
		centre += rPosition;
		distinct++;
	}
	centre /= static_cast<float>(distinct);
	rWorld_ = glm::vec3(toWorld * glm::vec4(centre, 1.0f));
	return true;
}

void GeometryPanel::pick(const Camera& rCamera_, GLFWwindow* pWindow_, double x_, double y_, PickAction action_)
{
	glm::vec3 origin;
	glm::vec3 direction;
	if(!windowRay(pWindow_, rCamera_, x_, y_, origin, direction)) return;

	Key key;
	if(this->isolate && this->hasOpen) key = this->openKey;
	else if(this->source == Source::Entity)
	{
		/* The entity's copy: the submesh under the cursor, the nearest. The submeshes are baked, so they all sit at the entity's transform. */
		Entity* pEntity = this->liveEntity();
		if(pEntity == nullptr)
		{
			this->message = "The entity is gone.";
			return;
		}
		glm::mat4 inverse = glm::inverse(this->entityMatrix(*pEntity));
		glm::vec3 localOrigin = glm::vec3(inverse * glm::vec4(origin, 1.0f));
		glm::vec3 localDirection = glm::vec3(inverse * glm::vec4(direction, 0.0f));
		int best = -1;
		float bestDistance = 0.0f;
		for(size_t i = 0; i < this->entityCopy.pModel->submeshes.size(); i++)
		{
			float distance = 0.0f;
			bool any = false;
			if(rayHitsMesh(this->editPools.meshes[this->entityCopy.pModel->submeshes[i].meshId], glm::mat4(1.0f), localOrigin, localDirection, distance, any) && (best < 0 || distance < bestDistance))
			{
				best = static_cast<int>(i);
				bestDistance = distance;
			}
		}
		if(best < 0)
		{
			this->message = "No geometry of the entity under the cursor.";
			return;
		}
		key = Key{std::string(), static_cast<uint32_t>(best)};
	}
	else
	{
		/* The mesh under the cursor of all the loaded levels, the nearest. */
		Level* pBestLevel = nullptr;
		const BasicMesh* pBestMesh = nullptr;
		float bestDistance = 0.0f;
		for(uint32_t i = 0; i < Globals::Game::levels.objects.size(); i++)
		{
			if(!Globals::Game::levels.alive[i]) continue;
			float distance = 0.0f;
			const BasicMesh* pMesh = nullptr;
			if(Globals::Game::levels.objects[i].rayHitsInstance(origin, direction, distance, &pMesh) && (pBestLevel == nullptr || distance < bestDistance))
			{
				pBestLevel = &Globals::Game::levels.objects[i];
				pBestMesh = pMesh;
				bestDistance = distance;
			}
		}
		if(pBestLevel == nullptr)
		{
			this->message = "No level geometry under the cursor.";
			return;
		}
		std::vector<std::pair<BasicMesh*, glm::mat4>> meshes;
		pBestLevel->editableMeshes(meshes);
		auto found = std::find_if(meshes.begin(), meshes.end(), [&](const auto& rEntry_) { return rEntry_.first == pBestMesh; });
		if(found == meshes.end()) return;
		key = Key{pBestLevel->name, static_cast<uint32_t>(found - meshes.begin())};
	}
	try
	{
		if(!this->hasOpen || key != this->openKey) this->open(key.first, key.second);
	}
	catch(const std::exception& rError)
	{
		this->message = std::format("Can't edit this mesh: {}", rError.what());
		return;
	}
	BasicMesh* pMesh = nullptr;
	glm::mat4 toWorld;
	EditMesh* pEdit = this->current();
	if(pEdit == nullptr || !this->resolve(pMesh, toWorld)) return;

	glm::mat4 inverse = glm::inverse(toWorld);
	glm::vec3 localOrigin = glm::vec3(inverse * glm::vec4(origin, 1.0f));
	glm::vec3 localDirection = glm::vec3(inverse * glm::vec4(direction, 0.0f));
	float t = 0.0f;
	uint32_t face = 0;
	if(!pEdit->raycast(localOrigin, localDirection, t, face))
	{
		this->message = "No face under the cursor.";
		return;
	}
	this->message.clear();
	this->hasHit = true;
	this->hitFace = face;
	this->hitPoint = localOrigin + localDirection * t;

	if(action_ == PickAction::Replace) this->clearSelection();
	bool takes = action_ == PickAction::Toggle || action_ == PickAction::Remove;
	const std::array<uint32_t, 3>& rCorners = pEdit->faces[face].v;
	auto positionOf = [&](uint32_t vertex_) -> const glm::vec3& { return pEdit->vertices[vertex_].position; };
	auto removeVertexAt = [&](const glm::vec3& rPosition_)
	{
		size_t before = this->pickedVertices.size();
		std::erase_if(this->pickedVertices, [&](uint32_t vertex_) { return positionOf(vertex_) == rPosition_; });
		return this->pickedVertices.size() != before;
	};
	if(this->mode == Vertex)
	{
		uint32_t nearest = *std::min_element(rCorners.begin(), rCorners.end(), [&](uint32_t a_, uint32_t b_) { return glm::distance(positionOf(a_), this->hitPoint) < glm::distance(positionOf(b_), this->hitPoint); });
		bool removed = takes && removeVertexAt(positionOf(nearest));
		if(!removed && action_ != PickAction::Remove) this->pickedVertices.push_back(nearest);
	}
	else if(this->mode == Edge)
	{
		std::pair<uint32_t, uint32_t> best{rCorners[0], rCorners[1]};
		float bestEdge = 1e30f;
		for(int i = 0; i < 3; i++)
		{
			uint32_t a = rCorners[i];
			uint32_t b = rCorners[(i + 1) % 3];
			float distance = distanceToSegment(this->hitPoint, positionOf(a), positionOf(b));
			if(distance < bestEdge)
			{
				bestEdge = distance;
				best = {a, b};
			}
		}
		auto same = [&](const std::pair<uint32_t, uint32_t>& rEdge_)
		{
			return (positionOf(rEdge_.first) == positionOf(best.first) && positionOf(rEdge_.second) == positionOf(best.second)) || (positionOf(rEdge_.first) == positionOf(best.second) && positionOf(rEdge_.second) == positionOf(best.first));
		};
		auto existing = std::find_if(this->pickedEdges.begin(), this->pickedEdges.end(), same);
		if(existing != this->pickedEdges.end())
		{
			if(takes)
			{
				this->pickedEdges.erase(existing);
				removeVertexAt(positionOf(best.first));
				removeVertexAt(positionOf(best.second));
			}
		}
		else if(action_ != PickAction::Remove)
		{
			this->pickedEdges.push_back(best);
			this->pickedVertices.push_back(best.first);
			this->pickedVertices.push_back(best.second);
		}
	}
	else
	{
		std::vector<uint32_t> group = this->island ? pEdit->island(face) : std::vector<uint32_t>{face};
		bool selected = std::find(this->pickedFaces.begin(), this->pickedFaces.end(), face) != this->pickedFaces.end();
		bool want = action_ == PickAction::Remove ? false : action_ == PickAction::Toggle ? !selected : true;
		for(uint32_t member : group)
		{
			std::erase(this->pickedFaces, member);
			if(want) this->pickedFaces.push_back(member);
		}
	}
}

void GeometryPanel::boxSelect(const Camera& rCamera_, const ImVec2& rFrom_, const ImVec2& rTo_, bool additive_, bool remove_)
{
	BasicMesh* pMesh = nullptr;
	glm::mat4 toWorld;
	EditMesh* pEdit = this->current();
	if(pEdit == nullptr || !this->resolve(pMesh, toWorld))
	{
		this->message = "Click a mesh first, then box-select on it.";
		return;
	}
	ScreenBox box(rFrom_, rTo_);
	if(!additive_ && !remove_) this->clearSelection();

	/* The vertices' window positions, once; one behind the camera is never inside. */
	std::vector<ImVec2> points(pEdit->vertices.size());
	std::vector<char> inside(pEdit->vertices.size(), 0);
	for(size_t vertex = 0; vertex < pEdit->vertices.size(); vertex++)
	{
		inside[vertex] = project(rCamera_, glm::vec3(toWorld * glm::vec4(pEdit->vertices[vertex].position, 1.0f)), points[vertex]) && box.contains(points[vertex]);
	}
	if(remove_)
	{
		/* Takes out what is inside the box: the vertices, the edges and faces with all their corners inside. */
		std::erase_if(this->pickedVertices, [&](uint32_t vertex_) { return inside[vertex_] != 0; });
		std::erase_if(this->pickedEdges, [&](const std::pair<uint32_t, uint32_t>& rEdge_) { return inside[rEdge_.first] != 0 && inside[rEdge_.second] != 0; });
		std::erase_if(this->pickedFaces, [&](uint32_t face_) { return inside[pEdit->faces[face_].v[0]] != 0 && inside[pEdit->faces[face_].v[1]] != 0 && inside[pEdit->faces[face_].v[2]] != 0; });
		this->message.clear();
		return;
	}
	/* What is hidden behind a face of the mesh isn't taken (unless X-ray), when there are not too many vertices to test. */
	size_t inCount = std::count(inside.begin(), inside.end(), 1);
	if(!this->xray && inCount <= MaxVisibilityTests)
	{
		for(size_t vertex = 0; vertex < inside.size(); vertex++)
		{
			if(inside[vertex] && !this->vertexVisible(rCamera_, *pEdit, toWorld, static_cast<uint32_t>(vertex))) inside[vertex] = 0;
		}
	}
	std::set<PositionKey> havePositions;
	for(uint32_t vertex : this->pickedVertices) havePositions.insert(positionKey(pEdit->vertices[vertex].position));
	auto pickVertex = [&](uint32_t vertex_)
	{
		if(havePositions.insert(positionKey(pEdit->vertices[vertex_].position)).second) this->pickedVertices.push_back(vertex_);
	};
	if(this->mode == Vertex)
	{
		for(const EditMesh::Face& rFace : pEdit->faces)
		{
			for(uint32_t vertex : rFace.v)
			{
				if(inside[vertex]) pickVertex(vertex);
			}
		}

		/* The vertices the editor added have no face until connected. */
		for(size_t vertex = pEdit->original.vertices.size(); vertex < pEdit->vertices.size(); vertex++)
		{
			if(inside[vertex]) pickVertex(static_cast<uint32_t>(vertex));
		}
	}
	else if(this->mode == Edge)
	{
		std::set<std::pair<PositionKey, PositionKey>> haveEdges;
		auto edgeKey = [&](uint32_t a_, uint32_t b_)
		{
			PositionKey first = positionKey(pEdit->vertices[a_].position);
			PositionKey second = positionKey(pEdit->vertices[b_].position);
			return first < second ? std::make_pair(first, second) : std::make_pair(second, first);
		};
		for(const auto& [a, b] : this->pickedEdges) haveEdges.insert(edgeKey(a, b));
		for(const EditMesh::Face& rFace : pEdit->faces)
		{
			for(int i = 0; i < 3; i++)
			{
				uint32_t a = rFace.v[i];
				uint32_t b = rFace.v[(i + 1) % 3];
				if(!inside[a] || !inside[b] || !haveEdges.insert(edgeKey(a, b)).second) continue;
				this->pickedEdges.push_back({a, b});
				pickVertex(a);
				pickVertex(b);
			}
		}
	}
	else
	{
		std::vector<char> have(pEdit->faces.size(), 0);
		for(uint32_t face : this->pickedFaces) have[face] = 1;
		for(uint32_t face = 0; face < pEdit->faces.size(); face++)
		{
			const std::array<uint32_t, 3>& rCorners = pEdit->faces[face].v;
			if(have[face] || !inside[rCorners[0]] || !inside[rCorners[1]] || !inside[rCorners[2]]) continue;
			this->pickedFaces.push_back(face);
		}
	}
	this->message.clear();
}

void GeometryPanel::flipFaces()
{
	EditMesh* pEdit = this->current();
	BasicMesh* pMesh = nullptr;
	glm::mat4 toWorld;
	if(pEdit == nullptr || !this->resolve(pMesh, toWorld) || this->pickedFaces.empty()) return;
	pEdit->pushUndo();
	for(uint32_t face : this->pickedFaces)
	{
		if(face < pEdit->faces.size()) std::swap(pEdit->faces[face].v[1], pEdit->faces[face].v[2]);
	}
	pEdit->changed = true;
	pEdit->upload(*pMesh);
	this->message = std::format("Flipped {} face(s).", this->pickedFaces.size());
}

void GeometryPanel::selectFaces(bool invert_)
{
	EditMesh* pEdit = this->current();
	if(pEdit == nullptr) return;
	std::vector<char> have(pEdit->faces.size(), 0);
	for(uint32_t face : this->pickedFaces)
	{
		if(face < have.size()) have[face] = 1;
	}
	this->clearSelection();
	for(uint32_t face = 0; face < pEdit->faces.size(); face++)
	{
		if(!invert_ || !have[face]) this->pickedFaces.push_back(face);
	}
}

bool GeometryPanel::cancelExtrusion()
{
	if(!this->drawCorners.empty())
	{
		if(this->drawCorners.size() == 2)
		{
			/* One of the two is new: it stays and the drawing goes on from it (the existing one is let go). Both existing, or both new: the first stays. */
			size_t planned = std::count_if(this->drawCorners.begin(), this->drawCorners.end(), [](const DrawCorner& rCorner_) { return rCorner_.planned; });
			DrawCorner keep = this->drawCorners[0];
			if(planned == 1 && this->drawCorners[1].planned) keep = this->drawCorners[1];
			this->drawCorners.assign(1, keep);
			this->clearSelection();
			if(!keep.planned) this->pickedVertices.push_back(keep.vertex);
			this->message = "Stepped back: click for the next corner.";
			return true;
		}
		this->drawCorners.clear();
		this->clearSelection();
		this->message = "Extrusion cancelled.";
		return true;
	}
	if(!this->pendingFaces.empty())
	{
		this->discard();
		return true;
	}
	return false;
}

void GeometryPanel::discard()
{
	EditMesh* pEdit = this->current();
	BasicMesh* pMesh = nullptr;
	glm::mat4 toWorld;
	bool had = !this->drawCorners.empty();
	this->drawCorners.clear();
	if(!this->pendingFaces.empty() && pEdit != nullptr && pEdit->discardTo(this->pendingBase))
	{
		if(this->resolve(pMesh, toWorld)) pEdit->upload(*pMesh);
		this->clearSelection();
		had = true;
	}
	this->pendingFaces.clear();
	this->message = had ? "Discarded." : "Nothing to discard.";
}

void GeometryPanel::deleteSelection()
{
	EditMesh* pEdit = this->current();
	BasicMesh* pMesh = nullptr;
	glm::mat4 toWorld;
	if(pEdit == nullptr || !this->resolve(pMesh, toWorld)) return;
	if(this->pickedVertices.empty() && this->pickedFaces.empty()) return;
	pEdit->pushUndo();
	if(this->mode == Face) pEdit->deleteFaces(this->pickedFaces);
	else if(this->mode == Vertex) pEdit->deleteFacesUsing(this->selectedVertices());
	else
	{
		/* An edge goes with the faces that share it: those with a corner at each end. */
		std::vector<uint32_t> doomed;
		for(uint32_t face = 0; face < pEdit->faces.size(); face++)
		{
			for(const auto& [a, b] : this->pickedEdges)
			{
				bool hasA = false;
				bool hasB = false;
				for(uint32_t corner : pEdit->faces[face].v)
				{
					hasA = hasA || pEdit->vertices[corner].position == pEdit->vertices[a].position;
					hasB = hasB || pEdit->vertices[corner].position == pEdit->vertices[b].position;
				}
				if(hasA && hasB)
				{
					doomed.push_back(face);
					break;
				}
			}
		}
		pEdit->deleteFaces(doomed);
	}
	this->clearSelection();
	this->hasHit = false;
	pEdit->upload(*pMesh);
}

/* The triangle faces the camera: its normal points against the way the camera looks. */
static void facePointsAtCamera(const EditMesh& rEdit_, const glm::mat4& rToWorld_, const glm::vec3& rCameraFront_, std::array<uint32_t, 3>& rFace_)
{
	glm::vec3 normal = glm::cross(rEdit_.vertices[rFace_[1]].position - rEdit_.vertices[rFace_[0]].position, rEdit_.vertices[rFace_[2]].position - rEdit_.vertices[rFace_[0]].position);
	glm::vec3 front = glm::mat3(glm::inverse(rToWorld_)) * rCameraFront_;
	if(glm::dot(normal, front) > 0.0f) std::swap(rFace_[1], rFace_[2]);
}

bool GeometryPanel::redo()
{
	EditMesh* pEdit = this->current();
	BasicMesh* pMesh = nullptr;
	glm::mat4 toWorld;
	if(pEdit == nullptr || !pEdit->redo()) return false;
	if(this->resolve(pMesh, toWorld)) pEdit->upload(*pMesh);
	this->pendingFaces.clear();
	this->drawCorners.clear();
	this->clearSelection();
	this->hasHit = false;
	this->undone++;
	return true;
}

bool GeometryPanel::undo()
{
	EditMesh* pEdit = this->current();
	BasicMesh* pMesh = nullptr;
	glm::mat4 toWorld;
	if(pEdit == nullptr || !pEdit->undo()) return false;
	if(this->resolve(pMesh, toWorld)) pEdit->upload(*pMesh);
	this->pendingFaces.clear();
	this->drawCorners.clear();
	this->clearSelection();
	this->hasHit = false;
	this->undone++;
	return true;
}

void GeometryPanel::connect(const glm::vec3& rCameraFront_)
{
	EditMesh* pEdit = this->current();
	BasicMesh* pMesh = nullptr;
	glm::mat4 toWorld;
	if(pEdit == nullptr || !this->resolve(pMesh, toWorld)) return;

	/* The last three picked corners at distinct positions. */
	std::vector<uint32_t> corners;
	for(auto it = this->pickedVertices.rbegin(); it != this->pickedVertices.rend() && corners.size() < 3; ++it)
	{
		bool repeated = std::any_of(corners.begin(), corners.end(), [&](uint32_t other_) { return pEdit->vertices[other_].position == pEdit->vertices[*it].position; });
		if(!repeated) corners.push_back(*it);
	}
	if(corners.size() < 3)
	{
		this->message = "Connect needs three vertices picked (an edge counts as two).";
		return;
	}
	std::reverse(corners.begin(), corners.end());

	std::array<uint32_t, 3> face{corners[0], corners[1], corners[2]};
	facePointsAtCamera(*pEdit, toWorld, rCameraFront_, face);
	pEdit->pushUndo();
	pEdit->addFace(face[0], face[1], face[2]);
	pEdit->upload(*pMesh);
	this->message.clear();
}

void GeometryPanel::extrude()
{
	EditMesh* pEdit = this->current();
	BasicMesh* pMesh = nullptr;
	glm::mat4 toWorld;
	if(pEdit == nullptr || !this->resolve(pMesh, toWorld)) return;

	/* The corners (one vertex each) and the pairs of corners the extrusion starts from: an Edge mode edge, or two picked vertices. */
	std::vector<uint32_t> corners;
	for(uint32_t vertex : this->pickedVertices)
	{
		bool repeated = std::any_of(corners.begin(), corners.end(), [&](uint32_t other_) { return pEdit->vertices[other_].position == pEdit->vertices[vertex].position; });
		if(!repeated) corners.push_back(vertex);
	}
	std::vector<std::pair<uint32_t, uint32_t>> sides;
	if(this->mode == Edge) sides = this->pickedEdges;
	else if(this->mode == Vertex && corners.size() == 2) sides.push_back({corners[0], corners[1]});
	if(this->mode == Face || (sides.empty() && corners.empty()))
	{
		this->message = "Extrude (E) needs a vertex or an edge picked.";
		return;
	}

	/* A new vertex starts beside what it comes from, not on it: vertices at one position are one corner, so the selection would move both. */
	glm::vec3 front = glm::normalize(glm::mat3(glm::inverse(toWorld)) * this->cameraFront);
	glm::vec3 right = glm::cross(this->cameraFront, glm::vec3(0.0f, 1.0f, 0.0f));
	if(glm::length(right) < 1e-4f) right = glm::vec3(1.0f, 0.0f, 0.0f);
	glm::vec3 vertexOffset = glm::normalize(glm::mat3(glm::inverse(toWorld)) * right) * ExtrudeOffset;
	auto makeVertex = [&](uint32_t from_, const glm::vec3& rPosition_)
	{
		/* Not the name Vertex: that is the mode here. */
		auto vertex = pEdit->vertices[from_];
		vertex.position = rPosition_;
		pEdit->vertices.push_back(vertex);
		return static_cast<uint32_t>(pEdit->vertices.size() - 1);
	};

	if(this->pendingFaces.empty()) this->pendingBase = pEdit->undoCount();
	pEdit->pushUndo();
	std::vector<uint32_t> made;
	if(!sides.empty())
	{
		/*
		The new vertices go out from the side in the view's plane, away from the face that already uses the side (if one does), so that the new faces continue the surface. Enter makes the faces (commitExtrusion).
		*/
		bool quad = this->extrudeKind == ExtrudeKind::Quad;
		for(const auto& [a, b] : sides)
		{
			glm::vec3 pa = pEdit->vertices[a].position;
			glm::vec3 pb = pEdit->vertices[b].position;
			glm::vec3 away = glm::cross(front, pb - pa);
			if(glm::length(away) < 1e-5f) away = glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), pb - pa);
			if(glm::length(away) < 1e-5f) away = vertexOffset;
			away = glm::normalize(away) * ExtrudeOffset;
			for(const EditMesh::Face& rFace : pEdit->faces)
			{
				int hits = 0;
				glm::vec3 third = pa;
				for(uint32_t corner : rFace.v)
				{
					const glm::vec3& rPosition = pEdit->vertices[corner].position;
					if(rPosition == pa || rPosition == pb) hits++;
					else third = rPosition;
				}
				if(hits < 2) continue;
				if(glm::dot(away, third - (pa + pb) * 0.5f) > 0.0f) away = -away;
				break;
			}
			Extrusion extrusion{a, b, 0, 0, quad};
			if(quad)
			{
				extrusion.c = makeVertex(a, pa + away);
				extrusion.d = makeVertex(b, pb + away);
				made.push_back(extrusion.c);
				made.push_back(extrusion.d);
			}
			else
			{
				extrusion.c = makeVertex(a, (pa + pb) * 0.5f + away);
				extrusion.d = extrusion.c;
				made.push_back(extrusion.c);
			}
			this->pendingFaces.push_back(extrusion);
		}
	}
	else
	{
		for(uint32_t corner : corners) made.push_back(makeVertex(corner, pEdit->vertices[corner].position + vertexOffset));
	}
	pEdit->changed = true;
	pEdit->upload(*pMesh);

	/* The new vertices are the selection, in Vertex mode, so that the Move gizmo takes them. */
	this->mode = Vertex;
	this->clearSelection();
	this->pickedVertices = made;
	this->message = sides.empty() ? std::string() : "Move the new vertices (near another vertex they merge, green; hold Ctrl to keep them apart), then press Enter to make the faces.";
}

std::vector<uint32_t> GeometryPanel::pendingMade(std::vector<int>* pGroups_) const
{
	std::vector<uint32_t> made;
	int group = 0;
	for(const Extrusion& rExtrusion : this->pendingFaces)
	{
		made.push_back(rExtrusion.c);
		if(pGroups_ != nullptr) pGroups_->push_back(group);
		if(rExtrusion.quad)
		{
			made.push_back(rExtrusion.d);
			if(pGroups_ != nullptr) pGroups_->push_back(group);
		}
		group++;
	}
	return made;
}

std::vector<GeometryPanel::Merge> GeometryPanel::findMerges(const std::vector<uint32_t>& rMovers_, const std::vector<int>& rGroups_)
{
	std::vector<Merge> merges;
	EditMesh* pEdit = this->current();
	BasicMesh* pMesh = nullptr;
	glm::mat4 toWorld;
	if(this->mergeOff || rMovers_.empty() || pEdit == nullptr || !this->resolve(pMesh, toWorld)) return merges;

	/* Every vertex's window position and depth once (clip w), then each mover looks for what is near it at about its depth. */
	ImVec2 size = ImGui::GetIO().DisplaySize;
	glm::mat4 toClip = this->viewProjection * toWorld;
	glm::mat4 toView = this->viewMatrix * toWorld;
	struct Screen
	{
		glm::vec2 point;
		float depth;
		bool valid;
	};
	auto screenOf = [&](const glm::vec3& rPosition_)
	{
		glm::vec4 clip = toClip * glm::vec4(rPosition_, 1.0f);
		if(clip.w <= 1e-4f) return Screen{glm::vec2(0.0f), 0.0f, false};
		return Screen{glm::vec2((clip.x / clip.w * 0.5f + 0.5f) * size.x, (1.0f - (clip.y / clip.w * 0.5f + 0.5f)) * size.y), -(toView * glm::vec4(rPosition_, 1.0f)).z, true};
	};
	std::vector<Screen> screens;
	screens.reserve(pEdit->vertices.size());
	for(const auto& rVertex : pEdit->vertices) screens.push_back(screenOf(rVertex.position));
	/* The same depth: within 10% of it from a perspective camera, within a tenth of the view's height from an orthographic one. */
	auto sameDepth = [&](float a_, float b_) { return std::abs(a_ - b_) <= (this->orthographicView ? 0.1f * this->orthographicHeight : 0.1f * std::max(a_, b_)); };

	/* The sides pending extrusions start from stay out: a new vertex merged into one of their ends would make no face. */
	std::set<uint32_t> moving(rMovers_.begin(), rMovers_.end());
	std::vector<glm::vec3> keepOut;
	for(const Extrusion& rExtrusion : this->pendingFaces)
	{
		keepOut.push_back(pEdit->vertices[rExtrusion.a].position);
		keepOut.push_back(pEdit->vertices[rExtrusion.b].position);
	}
	auto kept = [&](const glm::vec3& rPosition_) { return std::find(keepOut.begin(), keepOut.end(), rPosition_) != keepOut.end(); };

	/* One mover per position (the others at it go along). */
	std::vector<size_t> order;
	std::vector<glm::vec3> done;
	for(size_t i = 0; i < rMovers_.size(); i++)
	{
		if(rMovers_[i] >= pEdit->vertices.size()) continue;
		const glm::vec3& rPosition = pEdit->vertices[rMovers_[i]].position;
		if(std::find(done.begin(), done.end(), rPosition) != done.end()) continue;
		done.push_back(rPosition);
		order.push_back(i);
	}
	bool edges = order.size() <= MaxEdgeMergeMovers;
	auto groupOf = [&](size_t i_) { return i_ < rGroups_.size() ? rGroups_[i_] : 0; };

	/*
	Last to first, so that a mover that merges into a later mover (of another extrusion) can take the place that one itself ends up at. In the order of preference: a vertex that is not a mover, a later mover, a point on an edge (one that no mover is an end of, and not a side an extrusion starts from; the edge is split there when the merge is applied).
	*/
	std::vector<std::optional<Merge>> result(order.size());
	for(size_t n = order.size(); n-- > 0;)
	{
		uint32_t mover = rMovers_[order[n]];
		const glm::vec3& rPosition = pEdit->vertices[mover].position;
		const Screen& rFrom = screens[mover];
		if(!rFrom.valid) continue;
		float best = this->mergePixels;
		std::optional<Merge> found;
		for(size_t vertex = 0; vertex < screens.size(); vertex++)
		{
			const Screen& rTo = screens[vertex];
			if(!rTo.valid || moving.contains(static_cast<uint32_t>(vertex)) || !sameDepth(rTo.depth, rFrom.depth)) continue;
			float distance = glm::distance(rFrom.point, rTo.point);
			if(distance >= best) continue;
			const glm::vec3& rTarget = pEdit->vertices[vertex].position;
			if(rTarget == rPosition || kept(rTarget)) continue;
			best = distance;
			found = Merge{mover, static_cast<uint32_t>(vertex), rTarget, Merge::None, Merge::None};
		}
		for(size_t m = n + 1; m < order.size(); m++)
		{
			uint32_t other = rMovers_[order[m]];
			const Screen& rTo = screens[other];
			if(groupOf(order[m]) == groupOf(order[n]) || !rTo.valid || !sameDepth(rTo.depth, rFrom.depth)) continue;
			float distance = glm::distance(rFrom.point, rTo.point);
			if(distance >= best) continue;
			glm::vec3 place = result[m] ? result[m]->position : pEdit->vertices[other].position;
			if(place == rPosition || kept(place)) continue;
			best = distance;
			found = Merge{mover, other, place, Merge::None, Merge::None};
		}
		if(!found && edges)
		{
			float bestEdge = this->mergePixels;
			for(const EditMesh::Face& rFace : pEdit->faces)
			{
				for(int i = 0; i < 3; i++)
				{
					uint32_t a = rFace.v[i];
					uint32_t b = rFace.v[(i + 1) % 3];
					const Screen& rA = screens[a];
					const Screen& rB = screens[b];
					if(!rA.valid || !rB.valid || moving.contains(a) || moving.contains(b)) continue;
					const glm::vec3& rPa = pEdit->vertices[a].position;
					const glm::vec3& rPb = pEdit->vertices[b].position;
					if(rPa == rPosition || rPb == rPosition || (kept(rPa) && kept(rPb))) continue;
					/* The nearest point of the side in the window; its place on the edge itself is taken back through the perspective. */
					glm::vec2 ab = rB.point - rA.point;
					float length = glm::dot(ab, ab);
					if(length < 1e-6f) continue;
					float s = glm::dot(rFrom.point - rA.point, ab) / length;
					if(s < 0.02f || s > 0.98f) continue;
					float distance = glm::distance(rFrom.point, rA.point + ab * s);
					if(distance >= bestEdge || !sameDepth(rA.depth * (1.0f - s) + rB.depth * s, rFrom.depth)) continue;
					float t = this->orthographicView ? s : (s / rB.depth) / ((1.0f - s) / rA.depth + s / rB.depth);
					bestEdge = distance;
					found = Merge{mover, Merge::None, rPa + (rPb - rPa) * t, a, b};
				}
			}
		}
		result[n] = found;
	}
	for(const std::optional<Merge>& rMerge : result)
	{
		if(rMerge) merges.push_back(*rMerge);
	}
	return merges;
}

void GeometryPanel::applyMerges(const std::vector<Merge>& rMerges_, const std::vector<uint32_t>& rMovers_)
{
	EditMesh* pEdit = this->current();
	if(pEdit == nullptr) return;
	/* Every vertex at the mover's position goes with it; the new positions are worked out before any is set. */
	std::vector<std::pair<uint32_t, glm::vec3>> moves;
	for(const Merge& rMerge : rMerges_)
	{
		glm::vec3 old = pEdit->vertices[rMerge.mover].position;
		for(uint32_t vertex : rMovers_)
		{
			if(pEdit->vertices[vertex].position == old) moves.push_back({vertex, rMerge.position});
		}
	}
	for(const auto& [vertex, position] : moves) pEdit->vertices[vertex].position = position;
	if(!moves.empty()) pEdit->changed = true;

	/*
	A merge into an edge splits the faces that use the edge at the vertex (each into two, as new faces: the original face is gone, with its texture kept), so that no gap is left; the vertex's uv is the edge's two uv blended where it lies.
	*/
	for(const Merge& rMerge : rMerges_)
	{
		if(rMerge.edgeA == Merge::None) continue;
		glm::vec3 pa = pEdit->vertices[rMerge.edgeA].position;
		glm::vec3 pb = pEdit->vertices[rMerge.edgeB].position;
		float length = glm::dot(pb - pa, pb - pa);
		if(length > 0.0f)
		{
			float t = std::clamp(glm::dot(rMerge.position - pa, pb - pa) / length, 0.0f, 1.0f);
			pEdit->vertices[rMerge.mover].uv = glm::mix(pEdit->vertices[rMerge.edgeA].uv, pEdit->vertices[rMerge.edgeB].uv, t);
		}
		std::vector<uint32_t> removed;
		std::vector<EditMesh::Face> added;
		for(uint32_t face = 0; face < pEdit->faces.size(); face++)
		{
			const EditMesh::Face& rFace = pEdit->faces[face];
			for(int i = 0; i < 3; i++)
			{
				const glm::vec3& rFrom = pEdit->vertices[rFace.v[i]].position;
				const glm::vec3& rTo = pEdit->vertices[rFace.v[(i + 1) % 3]].position;
				if(!((rFrom == pa && rTo == pb) || (rFrom == pb && rTo == pa))) continue;
				uint32_t x = rFace.v[i];
				uint32_t y = rFace.v[(i + 1) % 3];
				uint32_t z = rFace.v[(i + 2) % 3];
				EditMesh::Face first = rFace;
				EditMesh::Face second = rFace;
				first.original = -1;
				second.original = -1;
				first.v = {x, rMerge.mover, z};
				second.v = {rMerge.mover, y, z};
				added.push_back(first);
				added.push_back(second);
				removed.push_back(face);
				break;
			}
		}
		pEdit->deleteFaces(removed);
		pEdit->faces.insert(pEdit->faces.end(), added.begin(), added.end());
		pEdit->changed = true;
		/* The faces' numbers changed. */
		this->pickedFaces.clear();
	}
}

bool GeometryPanel::edgeAt(const glm::vec3& rA_, const glm::vec3& rB_) const
{
	auto found = this->sessions.find(this->openKey);
	if(!this->hasOpen || found == this->sessions.end() || rA_ == rB_) return false;
	const EditMesh& rEdit = found->second;
	for(const EditMesh::Face& rFace : rEdit.faces)
	{
		for(int i = 0; i < 3; i++)
		{
			const glm::vec3& rFrom = rEdit.vertices[rFace.v[i]].position;
			const glm::vec3& rTo = rEdit.vertices[rFace.v[(i + 1) % 3]].position;
			if((rFrom == rA_ && rTo == rB_) || (rFrom == rB_ && rTo == rA_)) return true;
		}
	}
	return false;
}

void GeometryPanel::mergeSelection()
{
	EditMesh* pEdit = this->current();
	BasicMesh* pMesh = nullptr;
	glm::mat4 toWorld;
	if(pEdit == nullptr || !this->resolve(pMesh, toWorld)) return;
	std::vector<uint32_t> movers = this->selectedVertices();
	std::vector<Merge> merges = this->findMerges(movers);
	if(merges.empty()) return;
	this->applyMerges(merges, movers);
	pEdit->upload(*pMesh);
	this->message = std::format("Merged {} corner(s) into vertices they were moved onto (hold Ctrl when letting go to keep them apart; a corner on an edge splits it).", merges.size());
}

void GeometryPanel::commitExtrusion()
{
	EditMesh* pEdit = this->current();
	BasicMesh* pMesh = nullptr;
	glm::mat4 toWorld;
	if(pEdit == nullptr || !this->resolve(pMesh, toWorld)) return;
	if(this->pendingFaces.empty())
	{
		/* Three picked vertices joined: the triangle between them. */
		if(this->pickedVertices.size() >= 3) this->connect(this->cameraFront);
		return;
	}
	pEdit->pushUndo();

	/* The new vertices near another vertex are merged into it first (not with Ctrl held); a face whose corners end up on one position (or a line) is left out. */
	std::vector<int> groups;
	std::vector<uint32_t> made = this->pendingMade(&groups);
	std::vector<Merge> merges = this->findMerges(made, groups);
	this->applyMerges(merges, made);

	size_t count = 0;
	auto addFace = [&](std::array<uint32_t, 3> face_)
	{
		const glm::vec3& rA = pEdit->vertices[face_[0]].position;
		const glm::vec3& rB = pEdit->vertices[face_[1]].position;
		const glm::vec3& rC = pEdit->vertices[face_[2]].position;
		if(rA == rB || rB == rC || rA == rC) return;
		facePointsAtCamera(*pEdit, toWorld, this->cameraFront, face_);
		pEdit->addFace(face_[0], face_[1], face_[2]);
		count++;
	};
	for(const Extrusion& rExtrusion : this->pendingFaces)
	{
		if(rExtrusion.quad)
		{
			addFace({rExtrusion.a, rExtrusion.b, rExtrusion.d});
			addFace({rExtrusion.a, rExtrusion.d, rExtrusion.c});
		}
		else addFace({rExtrusion.a, rExtrusion.b, rExtrusion.c});
	}
	this->message = std::format("Made {} face(s){}; pick them in Face mode.", count, merges.empty() ? "" : std::format(", merged {} corner(s)", merges.size()));
	this->pendingFaces.clear();
	pEdit->upload(*pMesh);
}

void GeometryPanel::addVertexAtHit()
{
	EditMesh* pEdit = this->current();
	BasicMesh* pMesh = nullptr;
	glm::mat4 toWorld;
	if(pEdit == nullptr || !this->resolve(pMesh, toWorld) || !this->hasHit || this->hitFace >= pEdit->faces.size()) return;
	pEdit->pushUndo();
	uint32_t vertex = pEdit->addVertex(this->hitPoint, this->hitFace);
	pEdit->upload(*pMesh);
	this->clearSelection();
	this->pickedVertices.push_back(vertex);
}

bool GeometryPanel::nearestVertex(const Camera& rCamera_, double x_, double y_, uint32_t& rVertex_)
{
	EditMesh* pEdit = this->current();
	BasicMesh* pMesh = nullptr;
	glm::mat4 toWorld;
	if(pEdit == nullptr || !this->resolve(pMesh, toWorld)) return false;
	/* The vertices within the click radius, nearest first; the first one that is not hidden behind a face (a few are tried). */
	std::vector<std::pair<float, uint32_t>> candidates;
	for(size_t vertex = 0; vertex < pEdit->vertices.size(); vertex++)
	{
		ImVec2 point;
		if(!project(rCamera_, glm::vec3(toWorld * glm::vec4(pEdit->vertices[vertex].position, 1.0f)), point)) continue;
		float distance = std::hypot(point.x - static_cast<float>(x_), point.y - static_cast<float>(y_));
		if(distance < this->mergePixels) candidates.push_back({distance, static_cast<uint32_t>(vertex)});
	}
	std::sort(candidates.begin(), candidates.end());
	for(size_t i = 0; i < candidates.size() && i < MaxVisibilityTries; i++)
	{
		if(!this->vertexVisible(rCamera_, *pEdit, toWorld, candidates[i].second)) continue;
		rVertex_ = candidates[i].second;
		return true;
	}
	return false;
}

bool GeometryPanel::vertexVisible(const Camera& rCamera_, const EditMesh& rEdit_, const glm::mat4& rToWorld_, uint32_t vertex_) const
{
	if(this->xray) return true;
	/*
	A ray from the vertex towards the camera (parallel to the view direction for an orthographic one), started a little way along it so that the faces the vertex belongs to are not hit: a face in between hides it. Only the open mesh hides; other meshes and entities don't. A vertex the editor added has no face yet and is seen like the others.
	*/
	glm::vec3 world = glm::vec3(rToWorld_ * glm::vec4(rEdit_.vertices[vertex_].position, 1.0f));
	glm::vec3 toCamera = rCamera_.orthographic ? -rCamera_.front : rCamera_.position - world;
	float length = glm::length(toCamera);
	if(length < 1e-6f) return true;
	glm::vec3 direction = toCamera / length;
	float offset = rCamera_.orthographic ? rCamera_.orthoHeight * 1e-3f : length * 1e-3f;
	glm::mat4 inverse = glm::inverse(rToWorld_);
	glm::vec3 localOrigin = glm::vec3(inverse * glm::vec4(world + direction * offset, 1.0f));
	glm::vec3 localDirection = glm::vec3(inverse * glm::vec4(direction, 0.0f));
	float t = 0.0f;
	uint32_t face = 0;
	if(!rEdit_.raycast(localOrigin, localDirection, t, face)) return true;
	return !rCamera_.orthographic && t > length - offset;
}

bool GeometryPanel::drawClick(const Camera& rCamera_, GLFWwindow* pWindow_, double x_, double y_, bool shift_)
{
	EditMesh* pEdit = this->current();
	BasicMesh* pMesh = nullptr;
	glm::mat4 toWorld;
	if(pEdit == nullptr || !this->resolve(pMesh, toWorld)) return false;
	auto positionOf = [&](const DrawCorner& rCorner_) { return rCorner_.planned ? rCorner_.position : pEdit->vertices[rCorner_.vertex].position; };

	uint32_t vertex = 0;
	bool onVertex = this->nearestVertex(rCamera_, x_, y_, vertex);
	/* With Ctrl held, a click on a vertex of a started triangle places a new vertex there instead of merging into it. */
	if(!this->drawCorners.empty() && this->mergeOff) onVertex = false;
	if(this->drawCorners.empty())
	{
		if(!onVertex) return false;
		/* The vertex is selected as well, so that Escape leaves it selected for the Move gizmo. */
		this->clearSelection();
		this->pickedVertices.push_back(vertex);
		this->drawSource = vertex;
		this->drawCorners.push_back({false, vertex, glm::vec3(0.0f)});
		this->message = "Click to place the next corner, or click a vertex. Right click steps back, Esc stops.";
		return true;
	}

	/* The gizmo of the starting vertex goes while the triangle is drawn: it would take the clicks near it. */
	this->clearSelection();
	if(onVertex)
	{
		bool repeated = std::any_of(this->drawCorners.begin(), this->drawCorners.end(), [&](const DrawCorner& rCorner_) { return positionOf(rCorner_) == pEdit->vertices[vertex].position; });
		if(repeated) return true;
		size_t existing = std::count_if(this->drawCorners.begin(), this->drawCorners.end(), [](const DrawCorner& rCorner_) { return !rCorner_.planned; });
		if(this->drawCorners.size() == 2 && existing == 2)
		{
			this->message = "A triangle needs at least one new vertex: click empty space for the third corner.";
			return true;
		}
		this->drawCorners.push_back({false, vertex, glm::vec3(0.0f)});
	}
	else
	{
		/* The new vertex goes where the cursor ray crosses the plane through the last corner, facing the camera: in an aligned view it is a position in 2D. */
		glm::vec3 origin;
		glm::vec3 direction;
		if(!windowRay(pWindow_, rCamera_, x_, y_, origin, direction)) return true;
		glm::vec3 tail = glm::vec3(toWorld * glm::vec4(positionOf(this->drawCorners.back()), 1.0f));
		float facing = glm::dot(direction, rCamera_.front);
		if(std::abs(facing) < 1e-4f) return true;
		glm::vec3 world = origin + direction * (glm::dot(tail - origin, rCamera_.front) / facing);
		this->drawCorners.push_back({true, 0, glm::vec3(glm::inverse(toWorld) * glm::vec4(world, 1.0f))});
	}
	if(this->drawCorners.size() == 3) this->finishTriangle(shift_);
	return true;
}

void GeometryPanel::finishTriangle(bool keepGoing_)
{
	EditMesh* pEdit = this->current();
	BasicMesh* pMesh = nullptr;
	glm::mat4 toWorld;
	if(pEdit == nullptr || !this->resolve(pMesh, toWorld) || this->drawCorners.size() != 3) return;

	/* A planned vertex copies the attributes (normal, uv, ...) of the first existing corner; the chain starts on one, so there is always one. */
	uint32_t source = this->drawSource;
	pEdit->pushUndo();
	std::array<uint32_t, 3> face;
	for(size_t i = 0; i < 3; i++)
	{
		const DrawCorner& rCorner = this->drawCorners[i];
		if(!rCorner.planned)
		{
			face[i] = rCorner.vertex;
			continue;
		}
		/* Not the name Vertex: that is the mode here. */
		auto vertex = pEdit->vertices[source];
		vertex.position = rCorner.position;
		pEdit->vertices.push_back(vertex);
		face[i] = static_cast<uint32_t>(pEdit->vertices.size() - 1);
	}
	std::array<uint32_t, 3> corners = face;
	facePointsAtCamera(*pEdit, toWorld, this->cameraFront, face);
	pEdit->addFace(face[0], face[1], face[2]);
	pEdit->changed = true;
	pEdit->upload(*pMesh);

	this->drawCorners.clear();
	this->clearSelection();
	if(keepGoing_)
	{
		this->drawSource = corners[2];
		this->drawCorners.push_back({false, corners[2], glm::vec3(0.0f)});
		this->pickedVertices.push_back(corners[2]);
		this->message = "Triangle made; drawing on from its last corner.";
		return;
	}
	this->pickedVertices.assign(corners.begin(), corners.end());
	this->message = "Triangle made.";
}

void GeometryPanel::update(GLFWwindow* pWindow_, EditorCamera& rCamera_, Gizmo& rGizmo_, bool uiWantsMouse_, bool cameraDragging_)
{
	this->checkLoad();
	this->cameraFront = rCamera_.front;
	/* Holding Ctrl keeps merging off (the green merges go while it is held). */
	this->mergeOff = !this->texturing && !ImGui::GetIO().WantTextInput && (ImGui::IsKeyDown(ImGuiKey_LeftCtrl) || ImGui::IsKeyDown(ImGuiKey_RightCtrl));
	this->viewProjection = rCamera_.projection * rCamera_.view;
	this->viewMatrix = rCamera_.view;
	this->orthographicView = rCamera_.orthographic;
	this->orthographicHeight = rCamera_.orthoHeight;
	/* The mesh that stays drawn while isolating. */
	BasicMesh* pIsolated = nullptr;
	glm::mat4 isolatedWorld;
	this->isolatePtr = this->isolate && this->resolve(pIsolated, isolatedWorld) ? pIsolated : nullptr;
	if(!ImGui::GetIO().WantTextInput && !ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_I, false)) this->toggleIsolate();
	/* The drawing stops with the mode, and when an undo took out a vertex it used. */
	EditMesh* pCurrent = this->current();
	if(this->mode != Vertex || this->texturing || this->tool != Tool::Create || pCurrent == nullptr || std::any_of(this->drawCorners.begin(), this->drawCorners.end(), [&](const DrawCorner& rCorner_) { return !rCorner_.planned && rCorner_.vertex >= pCurrent->vertices.size(); })) this->drawCorners.clear();

	/* The Move gizmo works on one point, the centre of the selected vertices: the selection moves by what the point moved (taken into the mesh's space). */
	glm::vec3 handleWorld;
	BasicMesh* pMesh = nullptr;
	glm::mat4 toWorld;
	if(!this->texturing && this->handle(handleWorld) && this->resolve(pMesh, toWorld))
	{
		std::vector<glm::vec3> positions{handleWorld};
		std::vector<glm::vec3> rotations{glm::vec3(0.0f)};
		if(rGizmo_.updateLevels(pWindow_, rCamera_, positions, rotations, uiWantsMouse_, cameraDragging_))
		{
			EditMesh* pEdit = this->current();
			if(!this->dragUndoPushed)
			{
				pEdit->pushUndo();
				this->dragUndoPushed = true;
			}
			glm::mat4 inverse = glm::inverse(toWorld);
			glm::vec3 delta = glm::vec3(inverse * glm::vec4(positions[0], 1.0f)) - glm::vec3(inverse * glm::vec4(handleWorld, 1.0f));
			for(uint32_t vertex : this->selectedVertices()) pEdit->vertices[vertex].position += delta;
			pEdit->changed = true;
			pEdit->upload(*pMesh);
		}
	}
	else
	{
		/* No handles: the gizmo is told, so that it neither keeps a hover nor a drag from before (hovering() would block picks). */
		std::vector<glm::vec3> none;
		std::vector<glm::vec3> noRotations;
		rGizmo_.updateLevels(pWindow_, rCamera_, none, noRotations, uiWantsMouse_, cameraDragging_);
	}
	/* A move ends: the moved corners that are near another vertex merge into it (the move's undo step takes it too). */
	if(!rGizmo_.wantsMouse())
	{
		if(this->dragUndoPushed && !this->texturing) this->mergeSelection();
		this->dragUndoPushed = false;
	}


	/* Esc discards the triangle being drawn and the pending extrusions, Q takes the selection away, Delete deletes it (the Delete button's work; not in Texturing, which has no deleting). */
	if(!ImGui::GetIO().WantTextInput && !ImGui::GetIO().KeyCtrl)
	{
		if(!this->texturing && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) this->discard();
		if(ImGui::IsKeyPressed(ImGuiKey_Q, false))
		{
			/* The corners of a triangle being drawn are planned vertices that exist only there: they go with it. */
			this->drawCorners.clear();
			this->clearSelection();
		}
		if(!this->texturing && ImGui::IsKeyPressed(ImGuiKey_Delete, false)) this->deleteSelection();
	}

	/* E extrudes the picked edges (or vertices), Enter makes the faces of the extrusions (or the triangle of three picked vertices). */
	if(!this->texturing && this->tool == Tool::Create && !ImGui::GetIO().WantTextInput)
	{
		if(ImGui::IsKeyPressed(ImGuiKey_E, false))
		{
			this->drawCorners.clear();
			this->extrude();
		}
		if(ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false)) this->commitExtrusion();
	}

	/* A left press the UI, the gizmo and the camera didn't take picks when it is let go (as Editor's click to select). */
	bool leftDown = glfwGetMouseButton(pWindow_, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
	/* The Move gizmo's handles come first: a press on one (or on its way to one) is the gizmo's, never a pick or a draw click. */
	if(leftDown && !this->leftWasDown && !cameraDragging_ && !uiWantsMouse_ && !rGizmo_.wantsMouse() && !rGizmo_.hovering())
	{
		this->clickPending = true;
		glfwGetCursorPos(pWindow_, &this->clickX, &this->clickY);
		this->lastX = this->clickX;
		this->lastY = this->clickY;
	}
	if(cameraDragging_)
	{
		this->clickPending = false;
		this->leftPanning = false;
	}

	/* In an aligned view (keys 1-6) a left drag that goes farther than a click pans the camera in the view plane; it is no pick. */
	double cursorX = 0.0;
	double cursorY = 0.0;
	glfwGetCursorPos(pWindow_, &cursorX, &cursorY);
	bool movedFar = leftDown && this->clickPending && std::hypot(cursorX - this->clickX, cursorY - this->clickY) > RightClickPixels;
	if(movedFar && rCamera_.aligned && this->tool == Tool::Create)
	{
		this->clickPending = false;
		this->leftPanning = true;
	}
	if(movedFar && (this->texturing || this->tool != Tool::Create))
	{
		this->clickPending = false;
		this->leftBoxing = true;
	}
	if(cameraDragging_) this->leftBoxing = false;
	if(!leftDown && this->leftWasDown && this->leftBoxing)
	{
		bool additive = glfwGetKey(pWindow_, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS || glfwGetKey(pWindow_, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;
		bool ctrl = glfwGetKey(pWindow_, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS || glfwGetKey(pWindow_, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS;
		this->boxSelect(rCamera_, ImVec2(static_cast<float>(this->clickX), static_cast<float>(this->clickY)), ImVec2(static_cast<float>(cursorX), static_cast<float>(cursorY)), additive || (ctrl && this->drawCorners.empty() && this->pendingFaces.empty()));
	}
	if(!leftDown) this->leftBoxing = false;
	if(leftDown && this->leftPanning) rCamera_.panPixels(static_cast<float>(cursorX - this->lastX), static_cast<float>(cursorY - this->lastY));
	if(!leftDown) this->leftPanning = false;
	this->lastX = cursorX;
	this->lastY = cursorY;

	if(!leftDown && this->leftWasDown && this->clickPending)
	{
		this->clickPending = false;
		bool additive = glfwGetKey(pWindow_, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS || glfwGetKey(pWindow_, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;
		/* Ctrl + click adds to the selection in every mode of the edit tabs, and in Edge and Face mode of the Create tab; not in the Create tab's Vertex mode (a click there starts the extrusion), and not while something is being extruded (a triangle drawn or an extrusion pending): there Ctrl is the merge key and the click is the extrusion's. */
		bool ctrl = glfwGetKey(pWindow_, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS || glfwGetKey(pWindow_, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS;
		bool adds = ctrl && this->drawCorners.empty() && this->pendingFaces.empty() && (this->texturing || this->tool != Tool::Create || this->mode != Vertex);
		/* Vertex mode draws triangles: a click on a vertex starts one, then every click is a corner. Shift + click on a vertex with nothing started selects instead (it toggles). */
		bool drawing = !this->texturing && this->tool == Tool::Create && this->mode == Vertex && (!this->drawCorners.empty() || !(additive || adds));
		if(!drawing || !this->drawClick(rCamera_, pWindow_, this->clickX, this->clickY, additive)) this->pick(rCamera_, pWindow_, this->clickX, this->clickY, adds ? PickAction::Add : additive ? PickAction::Toggle : PickAction::Replace);
	}
	this->leftWasDown = leftDown;

	/*
	A right press let go within RightClickSeconds and RightClickPixels of where it went down is a click (takes out); one that moved farther is a box selection, made when it is let go (the camera's look hold is Alt + right in this subtab: EditorCamera::rightSelects).
	*/
	bool rightDown = glfwGetMouseButton(pWindow_, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
	if(cameraDragging_)
	{
		this->rightPending = false;
		this->boxing = false;
	}
	if(rightDown && !this->rightWasDown && !uiWantsMouse_ && !cameraDragging_ && !rGizmo_.hovering())
	{
		this->rightPending = true;
		glfwGetCursorPos(pWindow_, &this->rightX, &this->rightY);
		this->rightTime = glfwGetTime();
	}
	double x = 0.0;
	double y = 0.0;
	glfwGetCursorPos(pWindow_, &x, &y);
	if(rightDown && this->rightPending && std::hypot(x - this->rightX, y - this->rightY) > RightClickPixels) this->boxing = true;
	if(!rightDown && this->rightWasDown && this->rightPending)
	{
		this->rightPending = false;
		if(this->boxing)
		{
			this->drawCorners.clear();
			this->boxSelect(rCamera_, ImVec2(static_cast<float>(this->rightX), static_cast<float>(this->rightY)), ImVec2(static_cast<float>(x), static_cast<float>(y)), false, true);
		}
		else if(glfwGetTime() - this->rightTime <= RightClickSeconds && !(!this->texturing && this->cancelExtrusion())) this->pick(rCamera_, pWindow_, this->rightX, this->rightY, PickAction::Remove);
	}
	if(!rightDown) this->boxing = false;
	this->rightWasDown = rightDown;
}

void GeometryPanel::drawOverlay(const Camera& rCamera_, Gizmo& rGizmo_)
{
	this->viewProjection = rCamera_.projection * rCamera_.view;
	this->viewMatrix = rCamera_.view;
	this->orthographicView = rCamera_.orthographic;
	this->orthographicHeight = rCamera_.orthoHeight;
	if(this->boxing || this->leftBoxing)
	{
		/* Yellow picks (left drag); red takes out (right drag). */
		bool picks = this->leftBoxing;
		ImVec2 from = this->leftBoxing ? ImVec2(static_cast<float>(this->clickX), static_cast<float>(this->clickY)) : ImVec2(static_cast<float>(this->rightX), static_cast<float>(this->rightY));
		ScreenBox box(from, ImGui::GetMousePos());
		ImGui::GetBackgroundDrawList()->AddRectFilled(box.low, box.high, picks ? BoxFill : IM_COL32(255, 60, 60, 40));
		ImGui::GetBackgroundDrawList()->AddRect(box.low, box.high, picks ? BoxOutline : IM_COL32(255, 60, 60, 220));
	}
	EditMesh* pEdit = this->current();
	BasicMesh* pMesh = nullptr;
	glm::mat4 toWorld;
	if(pEdit == nullptr || !this->resolve(pMesh, toWorld)) return;
	ImDrawList* pDraw = ImGui::GetBackgroundDrawList();
	auto screen = [&](uint32_t vertex_, ImVec2& rOut_) { return project(rCamera_, glm::vec3(toWorld * glm::vec4(pEdit->vertices[vertex_].position, 1.0f)), rOut_); };

	if(this->showWireframe && pEdit->faces.size() <= MaxWireframeFaces)
	{
		for(const EditMesh::Face& rFace : pEdit->faces)
		{
			ImVec2 points[3];
			if(screen(rFace.v[0], points[0]) && screen(rFace.v[1], points[1]) && screen(rFace.v[2], points[2])) pDraw->AddTriangle(points[0], points[1], points[2], WireColor);
		}
	}
	for(uint32_t face : this->pickedFaces)
	{
		if(face >= pEdit->faces.size()) continue;
		ImVec2 points[3];
		if(screen(pEdit->faces[face].v[0], points[0]) && screen(pEdit->faces[face].v[1], points[1]) && screen(pEdit->faces[face].v[2], points[2]))
		{
			pDraw->AddTriangleFilled(points[0], points[1], points[2], SelectedFill);
			pDraw->AddTriangle(points[0], points[1], points[2], SelectedColor, 2.0f);
		}
	}
	for(const auto& [a, b] : this->pickedEdges)
	{
		ImVec2 from;
		ImVec2 to;
		if(screen(a, from) && screen(b, to)) pDraw->AddLine(from, to, SelectedColor, 3.0f);
	}

	/*
	The pending extrusions: the side they start from is blue, the new sides cyan. A new vertex near another one merges into it (see findMerges): it is drawn there, and a new side that is then an edge the mesh already has is green, with a green ring on the vertex it merged into. A picked vertex being moved shows the same.
	*/
	std::vector<Merge> merges;
	std::vector<uint32_t> movers;
	if(!this->pendingFaces.empty())
	{
		std::vector<int> groups;
		movers = this->pendingMade(&groups);
		merges = this->findMerges(movers, groups);
	}
	else if(this->dragUndoPushed && !this->texturing)
	{
		movers = this->selectedVertices();
		if(movers.size() <= 256) merges = this->findMerges(movers);
	}
	auto mergedPosition = [&](uint32_t vertex_)
	{
		for(const Merge& rMerge : merges)
		{
			if(pEdit->vertices[rMerge.mover].position == pEdit->vertices[vertex_].position) return rMerge.position;
		}
		return pEdit->vertices[vertex_].position;
	};
	auto line = [&](uint32_t from_, uint32_t to_)
	{
		glm::vec3 a = mergedPosition(from_);
		glm::vec3 b = mergedPosition(to_);
		ImVec2 pointA;
		ImVec2 pointB;
		if(project(rCamera_, glm::vec3(toWorld * glm::vec4(a, 1.0f)), pointA) && project(rCamera_, glm::vec3(toWorld * glm::vec4(b, 1.0f)), pointB)) pDraw->AddLine(pointA, pointB, this->edgeAt(a, b) ? MergeColor : AddedColor, 2.0f);
	};
	for(const Extrusion& rExtrusion : this->pendingFaces)
	{
		ImVec2 from;
		ImVec2 to;
		if(screen(rExtrusion.a, from) && screen(rExtrusion.b, to)) pDraw->AddLine(from, to, DrawColor, 3.0f);
		line(rExtrusion.a, rExtrusion.c);
		if(rExtrusion.quad)
		{
			line(rExtrusion.b, rExtrusion.d);
			line(rExtrusion.c, rExtrusion.d);
		}
		else line(rExtrusion.b, rExtrusion.c);
	}
	for(const Merge& rMerge : merges)
	{
		ImVec2 point;
		if(project(rCamera_, glm::vec3(toWorld * glm::vec4(rMerge.position, 1.0f)), point)) pDraw->AddCircle(point, 9.0f, MergeColor, 0, 2.0f);
	}

	/* The triangle being drawn: its corners, the lines between them and from the last one (and, with two corners, the first) to the cursor. */
	if(!this->drawCorners.empty())
	{
		std::vector<ImVec2> points;
		for(const DrawCorner& rCorner : this->drawCorners)
		{
			ImVec2 point;
			glm::vec3 position = rCorner.planned ? rCorner.position : pEdit->vertices[rCorner.vertex].position;
			if(project(rCamera_, glm::vec3(toWorld * glm::vec4(position, 1.0f)), point)) points.push_back(point);
		}
		for(size_t i = 0; i + 1 < points.size(); i++) pDraw->AddLine(points[i], points[i + 1], DrawColor, 2.0f);
		if(points.size() == this->drawCorners.size() && !ImGui::GetIO().WantCaptureMouse)
		{
			pDraw->AddLine(points.back(), ImGui::GetMousePos(), DrawColor, 2.0f);
			if(points.size() == 2) pDraw->AddLine(points.front(), ImGui::GetMousePos(), DrawColor, 1.0f);
		}
		for(const ImVec2& rPoint : points) pDraw->AddCircleFilled(rPoint, 6.0f, DrawColor);
		/* A vertex under the cursor is what the next click merges into (green; not after Ctrl); a side to it that is already an edge is green too. */
		uint32_t hover = 0;
		if(!this->mergeOff && !ImGui::GetIO().WantCaptureMouse && this->nearestVertex(rCamera_, ImGui::GetMousePos().x, ImGui::GetMousePos().y, hover))
		{
			ImVec2 point;
			if(screen(hover, point))
			{
				pDraw->AddCircle(point, 9.0f, MergeColor, 0, 2.0f);
				const DrawCorner& rLast = this->drawCorners.back();
				if(!rLast.planned && points.size() == this->drawCorners.size() && this->edgeAt(pEdit->vertices[rLast.vertex].position, pEdit->vertices[hover].position)) pDraw->AddLine(points.back(), point, MergeColor, 3.0f);
			}
		}
	}

	/* The vertices the editor added are always marked (they have no face until connected); the picked ones are larger. */
	for(size_t vertex = pEdit->original.vertices.size(); vertex < pEdit->vertices.size(); vertex++)
	{
		ImVec2 point;
		if(screen(static_cast<uint32_t>(vertex), point)) pDraw->AddCircleFilled(point, 3.0f, AddedColor);
	}
	for(uint32_t vertex : this->pickedVertices)
	{
		ImVec2 point;
		if(screen(vertex, point)) pDraw->AddCircleFilled(point, 5.0f, SelectedColor);
	}
	glm::vec3 handleWorld;
	if(!this->texturing && this->handle(handleWorld)) rGizmo_.drawLevels(rCamera_, {handleWorld});
}

bool GeometryPanel::unsaved() const
{
	return std::any_of(this->sessions.begin(), this->sessions.end(), [](const auto& rEntry_) { return !rEntry_.first.first.empty() && rEntry_.second.changed; });
}

void GeometryPanel::save()
{
	HubWorld& rHubWorld = Globals::Game::hubWorld;
	for(auto& [key, rMesh] : this->sessions)
	{
		if(key.first.empty() || !rMesh.changed) continue;
		auto level = std::find_if(rHubWorld.meshEdits.begin(), rHubWorld.meshEdits.end(), [&](const LevelMeshDiffs& rEntry_) { return rEntry_.level == key.first; });
		if(level == rHubWorld.meshEdits.end())
		{
			rHubWorld.meshEdits.push_back({key.first, {}});
			level = rHubWorld.meshEdits.end() - 1;
		}
		std::erase_if(level->meshes, [&](const MeshDiff& rDiff_) { return rDiff_.ordinal == key.second; });
		MeshDiff diff = rMesh.toDiff();
		if(!diff.empty()) level->meshes.push_back(std::move(diff));
	}
	std::erase_if(rHubWorld.meshEdits, [](const LevelMeshDiffs& rLevel_) { return rLevel_.meshes.empty(); });
	rHubWorld.writeMeshEdits();
	for(auto& [key, rMesh] : this->sessions)
	{
		if(!key.first.empty()) rMesh.changed = false;
	}
}

/* The geometry record of the entity's prefab, nullptr if the prefab is not an External one with an edit. */
static const Loading::PrefabGeometry* editedPrefabRecord(const Entity& rEntity_)
{
	if(rEntity_.prefabId == UninitializedPoolId) return nullptr;
	const Prefab& rPrefab = Globals::Game::prefabs[rEntity_.prefabId];
	if(rPrefab.kind != PrefabKind::External) return nullptr;
	return Loading::prefabGeometry(Loading::externalPrefabPoolName(rPrefab.inLevel ? Loading::ExternalScope::Level : Loading::ExternalScope::Project, rPrefab.level, rPrefab.name));
}

void GeometryPanel::openEntity(PoolId entityId_)
{
	Entity& rEntity = Globals::Game::entities[entityId_];
	EntityCopy copy;
	copy.entityId = entityId_;

	/* An entity of a prefab with a geometry record is edited again from the record: the source baked as it was, the prefab's diffs applied to the copy and kept in the sessions. Any other External model is edited from its file (fileModel). */
	const Loading::PrefabGeometry* pRecord = editedPrefabRecord(rEntity);
	if(pRecord != nullptr)
	{
		copy.baked = Loading::bakePrefabSource(*pRecord);
		copy.sourcePrefab = pRecord->source;
		copy.discMesh = pRecord->discMesh;
		copy.discTexture = pRecord->discTexture;
		copy.diffs = pRecord->meshes;
		copy.pPrevious = rEntity.pOverrideModel;
	}
	else if(rEntity.pOverrideModel != nullptr)
	{
		copy.baked = Loading::bakeModelData(Loading::readExternalModelFile(*rEntity.pOverrideModel));
		copy.fileModel = rEntity.pOverrideModel->name;
		copy.fileModelInProject = rEntity.pOverrideModel->inProject;
		copy.pPrevious = rEntity.pOverrideModel;
	}
	else
	{
		copy.baked = Loading::bakeEntity(rEntity);
		copy.sourcePrefab = rEntity.prefabId == UninitializedPoolId ? std::string() : Globals::Game::prefabs.getName(rEntity.prefabId);
		if(copy.sourcePrefab.empty()) throw std::runtime_error("the entity has no prefab to rebuild its model from.");
		copy.discMesh = rEntity.discMesh();
		copy.discTexture = rEntity.discTexture();
	}

	/* The copy shows the submeshes' own textures as face groups at once (buildEditedModel). */
	this->editPools.clear();
	copy.pModel = &Loading::buildEditedModel(this->editPools, true, "Edit", copy.baked, copy.diffs);
	this->entityCopy = std::move(copy);
	this->hasEntity = true;
	rEntity.setOverrideModel(this->entityCopy.pModel);
	this->hasOpen = false;
	this->hasHit = false;
	this->clearSelection();
	this->sessions.clear();
}

void GeometryPanel::closeEntity()
{
	if(!this->hasEntity) return;
	Entity* pEntity = this->liveEntity();
	if(pEntity != nullptr && pEntity->pOverrideModel == this->entityCopy.pModel) pEntity->setOverrideModel(this->entityCopy.pPrevious);
	std::erase_if(this->sessions, [](const auto& rEntry_) { return rEntry_.first.first.empty(); });
	if(this->hasOpen && this->openKey.first.empty()) this->hasOpen = false;
	this->hasHit = false;
	this->clearSelection();
	this->editPools.clear();
	this->hasEntity = false;
	this->entityCopy = EntityCopy();
}

void GeometryPanel::commitEntity(PoolId prefabId_)
{
	Entity* pEntity = this->liveEntity();
	if(pEntity == nullptr) throw std::runtime_error("the entity is gone.");
	Prefab& rPrefab = Globals::Game::prefabs[prefabId_];
	if(rPrefab.kind != PrefabKind::External) throw std::runtime_error("only an External prefab can keep the geometry.");
	Loading::ExternalScope scope = rPrefab.inLevel ? Loading::ExternalScope::Level : Loading::ExternalScope::Project;
	std::string poolName = Loading::externalPrefabPoolName(scope, rPrefab.level, rPrefab.name);
	std::string modelName;

	/* A model that came from a file is copied: the prefab gets a file of its own, named like it, with the edit made in it. */
	if(!this->entityCopy.fileModel.empty())
	{
		modelName = rPrefab.name;
		Loading::saveFileModel(scope, Loading::modelDataWithDiffs(this->entityCopy.baked, this->sessionDiffs(), modelName));
		this->entityCopy.pPrevious = nullptr;
	}
	else
	{
		Loading::PrefabGeometry geometry{this->entityCopy.sourcePrefab, this->entityCopy.discMesh, this->entityCopy.discTexture, {}};
		for(const auto& [key, rMesh] : this->sessions)
		{
			if(!key.first.empty()) continue;
			MeshDiff diff = rMesh.toDiff();
			if(!diff.empty()) geometry.meshes.push_back(std::move(diff));
		}

		/* The diffs of submeshes that were not opened stay as the prefab has them. */
		for(const MeshDiff& rOld : this->entityCopy.diffs)
		{
			if(!this->sessions.contains({std::string(), rOld.ordinal})) geometry.meshes.push_back(rOld);
		}

		/* The model the entity drew is replaced below (its pointer dies), so it is not given back when the copy closes. */
		this->entityCopy.pPrevious = nullptr;
		Loading::setPrefabGeometry(poolName, geometry);
		Loading::replaceEditedModel(scope, poolName, this->entityCopy.baked, geometry.meshes);

		modelName = Loading::editedModelName(poolName);
	}

	/* The edit lives only in the prefab: the entity joins it and gets the model from it (setPrefabMesh also writes prefabs.json and geometry.bin). The entity, its level and HubWorld.json keep no mesh edit of their own. A level's own entity stays as it is (its prefab is not in the level file): the editor replaces it with an entity of the prefab, which the level file keeps as an added one. */
	PoolId levelId = pEntity->levelId;
	bool ownEntity = !pEntity->hubWorldOwned && levelId != UninitializedPoolId && Globals::Game::levels.alive[levelId.id] && getLevel(levelId).sourceIndexOf(pEntity->selfId) >= 0;
	if(ownEntity)
	{
		this->replaceEntity = pEntity->selfId;
		this->replaceWith = prefabId_;
	}
	else pEntity->prefabId = prefabId_;
	setPrefabMesh(prefabId_, MeshChoice{MeshChoiceKind::External, modelName});
	this->closeEntity();
}

std::vector<MeshDiff> GeometryPanel::sessionDiffs() const
{
	std::vector<MeshDiff> diffs;
	for(const auto& [key, rMesh] : this->sessions)
	{
		if(!key.first.empty()) continue;
		MeshDiff diff = rMesh.toDiff();
		if(!diff.empty()) diffs.push_back(std::move(diff));
	}
	return diffs;
}

void GeometryPanel::commitFileModel()
{
	Entity* pEntity = this->liveEntity();
	if(pEntity == nullptr) throw std::runtime_error("the entity is gone.");
	Loading::ExternalScope scope = this->entityCopy.fileModelInProject ? Loading::ExternalScope::Project : Loading::ExternalScope::Level;
	External::ModelData data = Loading::modelDataWithDiffs(this->entityCopy.baked, this->sessionDiffs(), this->entityCopy.fileModel);

	/* The file's old model dies in the save: the entities that drew it get the new one, this entity (it draws the copy) too. */
	this->entityCopy.pPrevious = nullptr;
	const ExternalModel& rModel = Loading::saveFileModel(scope, std::move(data));
	pEntity->setOverrideModel(&rModel);
	this->closeEntity();
}

bool GeometryPanel::takeReplacement(PoolId& rEntityId_, PoolId& rPrefabId_)
{
	if(this->replaceEntity == UninitializedPoolId) return false;
	rEntityId_ = this->replaceEntity;
	rPrefabId_ = this->replaceWith;
	this->replaceEntity = UninitializedPoolId;
	this->replaceWith = UninitializedPoolId;
	return true;
}

void GeometryPanel::drawExitMenu()
{
	if(!this->hasEntity) return;
	Entity* pEntity = this->liveEntity();
	ImGui::SeparatorText("Entity edit");
	ImGui::Text("%s", pEntity != nullptr ? Globals::Game::entities.getName(this->entityCopy.entityId).c_str() : "(gone)");
	bool changed = this->entityChanged();
	if(!changed) ImGui::TextDisabled("No change yet.");

	ImGui::BeginDisabled(!changed || pEntity == nullptr);
	bool fileModel = !this->entityCopy.fileModel.empty();
	if(fileModel)
	{
		if(ImGui::Button("Save to model file"))
		{
			try
			{
				this->commitFileModel();
				this->sourceMessage = "Saved to the model file.";
			}
			catch(const std::exception& rError)
			{
				this->sourceMessage = std::format("Not saved: {}", rError.what());
			}
		}
		if(ImGui::IsItemHovered()) ImGui::SetTooltip("Overwrites %s.model.*: everything that draws the model changes.", this->entityCopy.fileModel.c_str());
	}
	else if(ImGui::Button("Save to existing prefab")) ImGui::OpenPopup("SaveToPrefab");
	ImGui::SameLine();
	if(ImGui::Button("Create prefab")) ImGui::OpenPopup("CreatePrefab");
	ImGui::EndDisabled();
	ImGui::SameLine();
	if(ImGui::Button("Discard")) this->closeEntity();

	/* Existing: the External prefabs of the HubWorld's Global folder and of the active level's folder. */
	if(ImGui::BeginPopup("SaveToPrefab"))
	{
		std::string level = Loading::levelPrefabScope();
		bool any = false;
		for(uint32_t i = 0; i < Globals::Game::prefabs.objects.size(); i++)
		{
			if(!Globals::Game::prefabs.alive[i]) continue;
			const Prefab& rPrefab = Globals::Game::prefabs.objects[i];
			if(rPrefab.kind != PrefabKind::External || (rPrefab.inLevel && rPrefab.level != level)) continue;
			any = true;
			if(ImGui::Selectable(Globals::Game::prefabs.names[i].c_str()))
			{
				try
				{
					this->commitEntity(PoolId(i, Globals::Game::prefabs.generation[i]));
					this->sourceMessage = "Saved to the prefab.";
				}
				catch(const std::exception& rError)
				{
					this->sourceMessage = std::format("Not saved: {}", rError.what());
				}
			}
		}
		if(!any) ImGui::TextDisabled("No External prefab yet.");
		ImGui::EndPopup();
	}
	if(ImGui::BeginPopup("CreatePrefab"))
	{
		bool hasLevel = !Loading::levelPrefabScope().empty();
		ImGui::InputText("Name", this->newPrefabName, sizeof(this->newPrefabName));
		if(!hasLevel) this->newPrefabInLevel = false;
		ImGui::BeginDisabled(!hasLevel);
		if(ImGui::RadioButton("This level", this->newPrefabInLevel)) this->newPrefabInLevel = true;
		ImGui::EndDisabled();
		ImGui::SameLine();
		if(ImGui::RadioButton("HubWorld (Global)", !this->newPrefabInLevel)) this->newPrefabInLevel = false;
		if(ImGui::Button("Create"))
		{
			try
			{
				Loading::ExternalScope scope = this->newPrefabInLevel ? Loading::ExternalScope::Level : Loading::ExternalScope::Project;
				if(fileModel && Loading::externalPoolsOf(scope).models.nameToId.contains(this->newPrefabName)) throw std::runtime_error(std::format("a model named '{}' exists already.", this->newPrefabName));
				PoolId prefabId = Loading::createExternalPrefab(this->newPrefabInLevel ? Loading::ExternalScope::Level : Loading::ExternalScope::Project, std::string(), this->newPrefabName);
				this->commitEntity(prefabId);
				this->sourceMessage = std::format("Created the prefab '{}'.", this->newPrefabName);
				this->newPrefabName[0] = 0;
				ImGui::CloseCurrentPopup();
			}
			catch(const std::exception& rError)
			{
				this->sourceMessage = std::format("Not created: {}", rError.what());
			}
		}
		ImGui::EndPopup();
	}
}

void GeometryPanel::drawSources()
{
	ImGui::SeparatorText("Geometry");
	if(ImGui::Selectable("Level", this->source == Source::Level) && this->source != Source::Level)
	{
		if(this->entityChanged()) this->sourceMessage = "Save or discard the entity's edit first.";
		else
		{
			this->closeEntity();
			this->source = Source::Level;
		}
	}
	if(ImGui::TreeNodeEx("Entities", ImGuiTreeNodeFlags_DefaultOpen))
	{
		ImGui::InputText("Filter", this->entityFilter, sizeof(this->entityFilter));
		std::string needle = this->entityFilter;
		std::transform(needle.begin(), needle.end(), needle.begin(), [](unsigned char c_) { return static_cast<char>(std::tolower(c_)); });
		std::vector<uint32_t> shown;
		for(uint32_t i = 0; i < Globals::Game::entities.objects.size(); i++)
		{
			if(!Globals::Game::entities.alive[i]) continue;
			const Entity& rEntity = Globals::Game::entities.objects[i];
			if(rEntity.isExternalOnly() ? editedPrefabRecord(rEntity) == nullptr && rEntity.pOverrideModel == nullptr : rEntity.instanceId == UninitializedPoolId) continue;
			if(!needle.empty())
			{
				std::string name = Globals::Game::entities.names[i];
				std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c_) { return static_cast<char>(std::tolower(c_)); });
				if(name.find(needle) == std::string::npos) continue;
			}
			shown.push_back(i);
		}
		ImGui::BeginChild("EntityGeometryList", ImVec2(0.0f, ImGui::GetTextLineHeightWithSpacing() * 8.0f), ImGuiChildFlags_Borders);
		ImGuiListClipper clipper;
		clipper.Begin(static_cast<int>(shown.size()));
		while(clipper.Step())
		{
			for(int row = clipper.DisplayStart; row < clipper.DisplayEnd; row++)
			{
				uint32_t i = shown[row];
				PoolId id(i, Globals::Game::entities.generation[i]);
				bool selected = this->hasEntity && this->entityCopy.entityId == id;
				ImGui::PushID(static_cast<int>(i));
				if(ImGui::Selectable(Globals::Game::entities.names[i].c_str(), selected) && !selected)
				{
					if(this->entityChanged()) this->sourceMessage = "Save or discard the entity's edit first.";
					else
					{
						try
						{
							this->closeEntity();
							this->openEntity(id);
							this->source = Source::Entity;
							this->sourceMessage.clear();
						}
						catch(const std::exception& rError)
						{
							this->closeEntity();
							this->source = Source::Level;
							this->sourceMessage = std::format("Can't edit this entity: {}", rError.what());
						}
					}
				}
				ImGui::PopID();
			}
		}
		ImGui::EndChild();
		ImGui::TreePop();
	}
	this->drawExitMenu();
	if(!this->sourceMessage.empty()) ImGui::TextWrapped("%s", this->sourceMessage.c_str());
}

void GeometryPanel::drawTab()
{
	this->checkLoad();
	this->drawSources();
	/* The tabs set tool; Create only takes effect while its tab is the open one. */
	bool inCreate = false;
	bool inFaces = false;
	if(ImGui::BeginTabBar("GeometryTool"))
	{
		if(ImGui::BeginTabItem("Transform"))
		{
			this->tool = Tool::Transform;
			ImGui::TextWrapped("Left click picks (Shift toggles), left drag a box picks what is in it (Shift adds). Right click takes one out, right drag a box takes out what is in it. Alt + right drag looks around. The Move gizmo moves the selection; a corner let go near another vertex merges into it (green while moving). Hold Ctrl when letting go to keep them apart.");
			ImGui::EndTabItem();
		}
		if(ImGui::BeginTabItem("Create"))
		{
			this->tool = Tool::Create;
			inCreate = true;
			ImGui::TextWrapped("Vertex mode: left click a vertex to start a triangle, then click empty space or other vertices for the corners (Shift on the third goes on from it; Esc stops). E extrudes the picked edge (Edge mode, or two picked vertices) as a triangle or a quad, Enter makes the faces; new vertices near another vertex merge into it (green), hold Ctrl to keep them apart. Keys 1-6 align the view.");
			ImGui::EndTabItem();
		}
		if(ImGui::BeginTabItem("Faces"))
		{
			this->tool = Tool::Faces;
			inFaces = true;
			ImGui::TextWrapped("Face mode, with the Transform tab's picking and Move gizmo. Delete / Delete key removes the picked faces, Flip turns their normals round, Select all and Invert pick faces. Q deselects, Esc discards a half-made triangle or extrusion.");
			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
	}
	/* The Faces tab works on faces only. */
	if(inFaces && this->mode != Face)
	{
		this->mode = Face;
		this->clearSelection();
	}
	int mode = this->mode;
	ImGui::BeginDisabled(inFaces);
	ImGui::RadioButton("Vertex", &mode, Vertex);
	ImGui::SameLine();
	ImGui::RadioButton("Edge", &mode, Edge);
	ImGui::SameLine();
	ImGui::RadioButton("Face", &mode, Face);
	ImGui::EndDisabled();
	if(mode != this->mode)
	{
		this->mode = static_cast<Mode>(mode);
		this->clearSelection();
	}
	ImGui::Checkbox("Show wireframe", &this->showWireframe);
	ImGui::SameLine();
	ImGui::Checkbox("X-ray picking", &this->xray);
	if(ImGui::Button(this->isolate ? "Show all (I)" : "Isolate object (I)")) this->toggleIsolate();
	/* A pixel radius, so it follows the zoom by itself: zoomed out a radius covers more of the world, close up less. */
	ImGui::SliderFloat("Merge radius (px)", &this->mergePixels, MinMergePixels, MaxMergePixels, "%.0f");
	if(ImGui::IsItemHovered()) ImGui::SetTooltip("Within this many screen pixels a vertex is clicked on and a moved or made vertex merges into another vertex or edge. It is in pixels, so it grows in the world as you zoom out.");

	EditMesh* pEdit = this->current();
	if(pEdit == nullptr)
	{
		ImGui::TextDisabled("No mesh open.");
	}
	else
	{
		ImGui::Text("%s, mesh %u: %zu vertices, %zu faces%s", pEdit->level.empty() ? "entity" : pEdit->level.c_str(), pEdit->ordinal, pEdit->vertices.size(), pEdit->faces.size(), pEdit->changed ? " (unsaved)" : "");
		ImGui::Text("Picked: %zu vertices, %zu edges, %zu faces. Merging: %s (hold Ctrl to keep apart)", this->pickedVertices.size(), this->pickedEdges.size(), this->pickedFaces.size(), this->mergeOff ? "off" : "on");
		if(inFaces)
		{
			if(ImGui::Button("Delete (Del)")) this->deleteSelection();
			ImGui::SameLine();
			if(ImGui::Button("Flip normals")) this->flipFaces();
			ImGui::SameLine();
			if(ImGui::Button("Select all")) this->selectFaces(false);
			ImGui::SameLine();
			if(ImGui::Button("Invert")) this->selectFaces(true);
		}
		else if(!inCreate)
		{
			if(ImGui::Button("Delete (Del)")) this->deleteSelection();
		}
		else
		{
			if(ImGui::Button("Add vertex")) this->addVertexAtHit();
			ImGui::SameLine();
			if(ImGui::Button("Connect")) this->connect(this->cameraFront);
			ImGui::SameLine();
			if(ImGui::Button("Extrude (E)")) this->extrude();
			ImGui::SameLine();
			int kind = static_cast<int>(this->extrudeKind);
			ImGui::RadioButton("Triangle (1 vertex)", &kind, 0);
			ImGui::SameLine();
			ImGui::RadioButton("Quad (2 vertices)", &kind, 1);
			this->extrudeKind = static_cast<ExtrudeKind>(kind);
			if(!this->pendingFaces.empty())
			{
				if(ImGui::Button("Make faces (Enter)")) this->commitExtrusion();
			}
		}
		if(ImGui::Button("Undo (Shift+Z)")) this->undo();
		ImGui::SameLine();
		if(ImGui::Button("Redo (Shift+Y)")) this->redo();
		ImGui::SameLine();
		if(ImGui::Button("Revert mesh"))
		{
			BasicMesh* pMesh = nullptr;
			glm::mat4 toWorld;
			pEdit->revert();
			this->pendingFaces.clear();
			this->drawCorners.clear();
			if(this->resolve(pMesh, toWorld)) pEdit->upload(*pMesh);
			this->clearSelection();
			this->hasHit = false;
		}
	}
	ImGui::Separator();
	if(ImGui::Button("Save HubWorld.bin"))
	{
		try
		{
			this->save();
			this->message = "Wrote HubWorld.bin.";
		}
		catch(const std::exception& rError)
		{
			this->message = std::format("HubWorld.bin not written: {}", rError.what());
		}
	}
	if(!this->message.empty()) ImGui::TextWrapped("%s", this->message.c_str());
}
}
#endif
