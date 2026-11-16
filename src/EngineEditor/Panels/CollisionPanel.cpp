#include "CollisionPanel.h"

#ifdef EDITOR
#include "BoxSelect.h"
#include "Editor.h"
#include "Globals.h"
#include "GameEntity.h"
#include "Level.h"
#include "HubWorld.h"
#include "imgui.h"
#include <algorithm>
#include <cmath>
#include <format>
#include <set>
#include <stdexcept>
#include <glm/ext/matrix_transform.hpp>

namespace Graphics
{
namespace
{
constexpr ImU32 WireColor = IM_COL32(200, 200, 200, 120);

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

/* Stands for "every mesh": BasicMesh::draw skips all meshes but the one Graphics::isolatedMesh names, and it is only compared. */
const char IsolateAllMarker = 0;
}

void CollisionPanel::checkLoad()
{
	if(Globals::Game::hubWorld.loadCount == this->seenLoadCount) return;
	this->seenLoadCount = Globals::Game::hubWorld.loadCount;
	this->sessions.clear();
	this->hasOpen = false;
	this->hasHit = false;
	this->applyPending = false;
	this->pendingFaces.clear();
	this->drawCorners.clear();
	this->clearSelection();
}

Level* CollisionPanel::openLevel()
{
	if(!this->hasOpen) return nullptr;
	Level* pLevel = levelNamed(this->openName);
	if(pLevel == nullptr || pLevel->collisionId == UninitializedPoolId) return nullptr;
	return pLevel;
}

glm::mat4 CollisionPanel::levelToWorld(const Level& rLevel_) const
{
	return rLevel_.hubWorldMatrix() * glm::translate(glm::mat4(1.0f), rLevel_.position);
}

CollisionEdit* CollisionPanel::current()
{
	if(!this->hasOpen) return nullptr;
	auto found = this->sessions.find(this->openName);
	return found == this->sessions.end() ? nullptr : &found->second;
}

void CollisionPanel::open(Level& rLevel_)
{
	this->openName = rLevel_.name;
	this->hasOpen = true;
	this->hasHit = false;
	this->pendingFaces.clear();
	this->drawCorners.clear();
	this->clearSelection();
	if(this->sessions.contains(rLevel_.name)) return;

	/*
	The imported collision is the one the loader kept when it applied a diff, else the collision as it is (it has none applied; its vertices are the loaded ones, which the HubWorld place may have turned: setRotation keeps them in baseVertices). The diff is only used to label the triangles when the loader applied it (the original was kept): one that didn't fit was skipped and the collision is as imported.
	*/
	HubWorld& rHubWorld = Globals::Game::hubWorld;
	auto original = rHubWorld.collisionOriginals.find(rLevel_.name);
	const CollisionDiff* pDiff = nullptr;
	if(original != rHubWorld.collisionOriginals.end())
	{
		for(const LevelCollisionDiff& rLevel : rHubWorld.collisionEdits)
		{
			if(rLevel.level == rLevel_.name) pDiff = &rLevel.diff;
		}
	}
	else
	{
		const Physics::Collision& rCollision = Globals::Physics::collisions[rLevel_.collisionId];
		Physics::CollisionMesh mesh = rCollision.mesh;
		if(!rCollision.baseVertices.empty()) mesh.vertices = rCollision.baseVertices;
		original = rHubWorld.collisionOriginals.emplace(rLevel_.name, std::move(mesh)).first;
	}
	this->sessions.emplace(rLevel_.name, CollisionEdit::open(rLevel_.name, original->second, pDiff));
}

void CollisionPanel::apply()
{
	CollisionEdit* pEdit = this->current();
	Level* pLevel = this->openLevel();
	if(pEdit == nullptr || pLevel == nullptr) return;
	Physics::Collision& rCollision = Globals::Physics::collisions[pLevel->collisionId];

	/* The turn is taken off first (it keeps a copy of the loaded vertices that would not match the new ones), the HubWorld place put on after. */
	rCollision.setRotation(glm::mat3(1.0f));
	rCollision.mesh = pEdit->build();
	rCollision.computeBounds();
	pLevel->placeCollision();
	Globals::Game::baBehaviors.resolve(rCollision, Globals::Game::baBehaviors.remap(levelRemapSection(pLevel->sourceEngine)));
	Globals::Physics::collisionSystem.rebuildQuadTree();
	editor.collisionView.levelLinesStale();
	this->applyPending = false;
}

void CollisionPanel::setValueOfSelection()
{
	CollisionEdit* pEdit = this->current();
	if(pEdit == nullptr) return;
	std::vector<uint32_t> targets = this->selectedTriangles();
	if(targets.empty())
	{
		this->message = "No triangle is selected.";
		return;
	}
	pEdit->pushUndo();
	pEdit->setValue(targets, this->value);
	this->apply();
}

bool CollisionPanel::unsaved() const
{
	return std::any_of(this->sessions.begin(), this->sessions.end(), [](const auto& rEntry_) { return rEntry_.second.changed; });
}

void CollisionPanel::save()
{
	HubWorld& rHubWorld = Globals::Game::hubWorld;
	for(auto& [name, rEdit] : this->sessions)
	{
		if(!rEdit.changed) continue;
		std::erase_if(rHubWorld.collisionEdits, [&](const LevelCollisionDiff& rEntry_) { return rEntry_.level == name; });
		CollisionDiff diff = rEdit.toDiff();
		if(!diff.empty()) rHubWorld.collisionEdits.push_back({name, std::move(diff)});
	}
	rHubWorld.writeCollisionEdits();
	for(auto& [name, rEdit] : this->sessions) rEdit.changed = false;
}

TriangleSurface* CollisionPanel::surface()
{
	CollisionEdit* pEdit = this->current();
	if(pEdit == nullptr) return nullptr;
	this->collisionSurface.pEdit = pEdit;
	this->collisionSurface.pValue = &this->value;
	return &this->collisionSurface;
}

bool CollisionPanel::placement(glm::mat4& rToWorld_)
{
	Level* pLevel = this->openLevel();
	if(pLevel == nullptr) return false;
	rToWorld_ = this->levelToWorld(*pLevel);
	return true;
}

void CollisionPanel::commit(bool dragging_)
{
	/* During a gizmo drag the live collision waits for the release (moveEnded). */
	if(dragging_)
	{
		this->applyPending = true;
		return;
	}
	this->apply();
}

void CollisionPanel::moveEnded()
{
	if(this->applyPending) this->apply();
}

const BasicMesh* CollisionPanel::isolationTarget()
{
	return this->openLevel() != nullptr ? reinterpret_cast<const BasicMesh*>(&IsolateAllMarker) : nullptr;
}

PoolId CollisionPanel::isolatedCollision()
{
	if(!this->isolate) return UninitializedPoolId;
	Level* pLevel = this->openLevel();
	return pLevel != nullptr ? pLevel->collisionId : UninitializedPoolId;
}

bool CollisionPanel::openUnderCursor(const glm::vec3& rOrigin_, const glm::vec3& rDirection_)
{
	/* While isolating only the open level's collision is there to pick. */
	if(this->isolate && this->hasOpen && this->openLevel() != nullptr) return true;

	/* The level whose collision is under the cursor, the nearest triangle of the live collisions (their vertices are turned by the HubWorld place; the position is added). */
	Level* pBestLevel = nullptr;
	float bestDistance = 0.0f;
	for(uint32_t i = 0; i < Globals::Game::levels.objects.size(); i++)
	{
		if(!Globals::Game::levels.alive[i] || Globals::Game::levels.objects[i].collisionId == UninitializedPoolId) continue;
		const Physics::Collision& rCollision = Globals::Physics::collisions[Globals::Game::levels.objects[i].collisionId];
		const Physics::CollisionMesh& rMesh = rCollision.mesh;
		glm::vec3 local = rOrigin_ - rCollision.position;
		for(size_t t = 0; t + 2 < rMesh.indices.size(); t += 3)
		{
			float distance = 0.0f;
			if(rayHitsCollisionTriangle(local, rDirection_, rMesh.vertices[rMesh.indices[t]], rMesh.vertices[rMesh.indices[t + 1]], rMesh.vertices[rMesh.indices[t + 2]], distance) && (pBestLevel == nullptr || distance < bestDistance))
			{
				pBestLevel = &Globals::Game::levels.objects[i];
				bestDistance = distance;
			}
		}
	}
	if(pBestLevel == nullptr)
	{
		this->message = "No level collision under the cursor.";
		return false;
	}
	try
	{
		if(!this->hasOpen || this->openName != pBestLevel->name) this->open(*pBestLevel);
	}
	catch(const std::exception& rError)
	{
		this->message = std::format("Can't edit this collision: {}", rError.what());
		this->hasOpen = false;
		return false;
	}
	return true;
}

void CollisionPanel::drawWireframe(const Camera& rCamera_, const glm::mat4& rToWorld_)
{
	/* The collision view's lines are behind the edit while a handle is dragged: the triangles of the selected vertices are drawn here, where they are now. */
	CollisionEdit* pEdit = this->current();
	if(pEdit == nullptr) return;
	std::vector<uint32_t> selected = this->selectedVertices();
	if(selected.empty()) return;
	ImDrawList* pDraw = ImGui::GetBackgroundDrawList();
	auto screen = [&](uint32_t vertex_, ImVec2& rOut_) { return project(rCamera_, glm::vec3(rToWorld_ * glm::vec4(pEdit->vertices[vertex_], 1.0f)), rOut_); };
	std::vector<bool> used(pEdit->vertices.size(), false);
	for(uint32_t vertex : selected) used[vertex] = true;
	for(const CollisionEdit::Triangle& rTriangle : pEdit->triangles)
	{
		if(!used[rTriangle.v[0]] && !used[rTriangle.v[1]] && !used[rTriangle.v[2]]) continue;
		ImVec2 points[3];
		if(screen(rTriangle.v[0], points[0]) && screen(rTriangle.v[1], points[1]) && screen(rTriangle.v[2], points[2])) pDraw->AddTriangle(points[0], points[1], points[2], WireColor);
	}
}

void CollisionPanel::drawHeader()
{
	ImGui::SeparatorText("Collision");
	ImGui::TextWrapped("The Geometry subtab's controls, on a level's collision: left click picks, right click takes out, drag boxes, the Move gizmo, drawing and extruding triangles (Create tab). New triangles get the Value below. Entities' collision shapes are not edited here.");
}

void CollisionPanel::drawSurfaceInfo()
{
	CollisionEdit* pEdit = this->current();
	if(pEdit == nullptr) return;
	ImGui::Text("%s: %zu vertices, %zu triangles%s", pEdit->level.c_str(), pEdit->vertices.size(), pEdit->triangles.size(), pEdit->changed ? " (unsaved)" : "");
}

void CollisionPanel::drawExtraButtons(bool inCreate_, bool inFaces_)
{
	CollisionEdit* pEdit = this->current();
	if(pEdit == nullptr) return;
	std::vector<uint32_t> triangles = this->selectedTriangles();

	/* The values the selected triangles have (a few, then "..."). */
	std::set<uint32_t> values;
	for(uint32_t triangle : triangles)
	{
		if(triangle < pEdit->triangles.size()) values.insert(pEdit->triangles[triangle].value);
	}
	if(!values.empty())
	{
		std::string text = "Values:";
		size_t shown = 0;
		for(uint32_t value : values)
		{
			if(shown++ == 8)
			{
				text += " ...";
				break;
			}
			text += std::format(" {}", value);
		}
		ImGui::TextUnformatted(text.c_str());
	}
	ImGui::InputScalar("Value", ImGuiDataType_U32, &this->value);
	ImGui::SameLine();
	if(ImGui::Button("Take")) this->value = values.empty() ? this->value : *values.begin();
	if(ImGui::IsItemHovered()) ImGui::SetTooltip("Takes the lowest value of the selected triangles.");
	if(ImGui::Button("Set value")) this->setValueOfSelection();
	if(ImGui::IsItemHovered()) ImGui::SetTooltip("Gives the Value to the selected triangles. Connect, drawing and extruding give it to the new triangles.");
}

void CollisionPanel::drawFooter()
{
	if(ImGui::Button("Save HubWorld.collision.bin"))
	{
		try
		{
			this->save();
			this->message = "Wrote HubWorld.collision.bin.";
		}
		catch(const std::exception& rError)
		{
			this->message = std::format("HubWorld.collision.bin not written: {}", rError.what());
		}
	}
	if(!this->message.empty()) ImGui::TextWrapped("%s", this->message.c_str());
}
}
#endif
