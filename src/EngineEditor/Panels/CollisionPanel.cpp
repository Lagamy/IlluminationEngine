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
constexpr ImU32 SelectedColor = IM_COL32(255, 200, 40, 255);
constexpr ImU32 SelectedFill = IM_COL32(255, 160, 40, 90);
constexpr ImU32 AddedColor = IM_COL32(60, 220, 255, 255);
/* A right press let go within this time and distance of where it went down is a click, not a camera look. */
constexpr double RightClickSeconds = 0.3;
constexpr double RightClickPixels = 4.0;

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

void CollisionPanel::checkLoad()
{
	if(Globals::Game::hubWorld.loadCount == this->seenLoadCount) return;
	this->seenLoadCount = Globals::Game::hubWorld.loadCount;
	this->sessions.clear();
	this->hasOpen = false;
	this->hasHit = false;
	this->applyPending = false;
	this->clearSelection();
}

Level* CollisionPanel::openLevel()
{
	if(!this->hasOpen) return nullptr;
	Level* pLevel = levelNamed(this->openName);
	if(pLevel == nullptr || pLevel->collisionId == UninitializedPoolId) return nullptr;
	return pLevel;
}

glm::mat4 CollisionPanel::toWorld(const Level& rLevel_) const
{
	return rLevel_.hubWorldMatrix() * glm::translate(glm::mat4(1.0f), rLevel_.position);
}

CollisionEdit* CollisionPanel::current()
{
	if(!this->hasOpen) return nullptr;
	auto found = this->sessions.find(this->openName);
	return found == this->sessions.end() ? nullptr : &found->second;
}

void CollisionPanel::clearSelection()
{
	this->pickedVertices.clear();
	this->pickedEdges.clear();
	this->pickedFaces.clear();
}

void CollisionPanel::open(Level& rLevel_)
{
	this->openName = rLevel_.name;
	this->hasOpen = true;
	this->hasHit = false;
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

std::vector<uint32_t> CollisionPanel::selectedVertices()
{
	std::vector<uint32_t> result;
	CollisionEdit* pEdit = this->current();
	if(pEdit == nullptr) return result;

	/* One pass over the vertices against the positions picked (welding each picked vertex alone is too slow for a box selection of thousands). */
	std::set<PositionKey> positions;
	for(uint32_t vertex : this->pickedVertices) positions.insert(positionKey(pEdit->vertices[vertex]));
	for(uint32_t face : this->pickedFaces)
	{
		for(uint32_t vertex : pEdit->triangles[face].v) positions.insert(positionKey(pEdit->vertices[vertex]));
	}
	if(positions.empty()) return result;
	for(uint32_t vertex = 0; vertex < pEdit->vertices.size(); vertex++)
	{
		if(positions.contains(positionKey(pEdit->vertices[vertex]))) result.push_back(vertex);
	}
	return result;
}

std::vector<uint32_t> CollisionPanel::selectedTriangles()
{
	std::vector<uint32_t> result;
	CollisionEdit* pEdit = this->current();
	if(pEdit == nullptr) return result;
	if(this->mode == Face) return this->pickedFaces;
	if(this->mode == Vertex)
	{
		std::vector<uint32_t> vertices = this->selectedVertices();
		std::set<uint32_t> set(vertices.begin(), vertices.end());
		for(uint32_t triangle = 0; triangle < pEdit->triangles.size(); triangle++)
		{
			const std::array<uint32_t, 3>& rCorners = pEdit->triangles[triangle].v;
			if(set.contains(rCorners[0]) || set.contains(rCorners[1]) || set.contains(rCorners[2])) result.push_back(triangle);
		}
		return result;
	}

	/* An edge stands for the triangles that share it: those that have it as a side (by position, so welded corners match). */
	auto edgeKey = [&](uint32_t a_, uint32_t b_)
	{
		PositionKey first = positionKey(pEdit->vertices[a_]);
		PositionKey second = positionKey(pEdit->vertices[b_]);
		return first < second ? std::make_pair(first, second) : std::make_pair(second, first);
	};
	std::set<std::pair<PositionKey, PositionKey>> edges;
	for(const auto& [a, b] : this->pickedEdges) edges.insert(edgeKey(a, b));
	for(uint32_t triangle = 0; triangle < pEdit->triangles.size(); triangle++)
	{
		const std::array<uint32_t, 3>& rCorners = pEdit->triangles[triangle].v;
		for(int i = 0; i < 3; i++)
		{
			if(edges.contains(edgeKey(rCorners[i], rCorners[(i + 1) % 3])))
			{
				result.push_back(triangle);
				break;
			}
		}
	}
	return result;
}

bool CollisionPanel::handle(glm::vec3& rWorld_)
{
	CollisionEdit* pEdit = this->current();
	Level* pLevel = this->openLevel();
	if(pEdit == nullptr || pLevel == nullptr) return false;
	std::vector<uint32_t> vertices = this->selectedVertices();
	if(vertices.empty()) return false;

	/* The centre of the distinct positions (a welded corner counts once). */
	std::set<PositionKey> seen;
	glm::vec3 centre(0.0f);
	size_t distinct = 0;
	for(uint32_t vertex : vertices)
	{
		const glm::vec3& rPosition = pEdit->vertices[vertex];
		if(!seen.insert(positionKey(rPosition)).second) continue;
		centre += rPosition;
		distinct++;
	}
	centre /= static_cast<float>(distinct);
	rWorld_ = glm::vec3(this->toWorld(*pLevel) * glm::vec4(centre, 1.0f));
	return true;
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

void CollisionPanel::pick(const Camera& rCamera_, GLFWwindow* pWindow_, double x_, double y_, PickAction action_)
{
	glm::vec3 origin;
	glm::vec3 direction;
	if(!windowRay(pWindow_, rCamera_, x_, y_, origin, direction)) return;

	/* The level whose collision is under the cursor, the nearest triangle of the live collisions (their vertices are turned by the HubWorld place; the position is added). */
	Level* pBestLevel = nullptr;
	float bestDistance = 0.0f;
	for(uint32_t i = 0; i < Globals::Game::levels.objects.size(); i++)
	{
		if(!Globals::Game::levels.alive[i] || Globals::Game::levels.objects[i].collisionId == UninitializedPoolId) continue;
		const Physics::Collision& rCollision = Globals::Physics::collisions[Globals::Game::levels.objects[i].collisionId];
		const Physics::CollisionMesh& rMesh = rCollision.mesh;
		glm::vec3 local = origin - rCollision.position;
		for(size_t t = 0; t + 2 < rMesh.indices.size(); t += 3)
		{
			float distance = 0.0f;
			if(rayHitsCollisionTriangle(local, direction, rMesh.vertices[rMesh.indices[t]], rMesh.vertices[rMesh.indices[t + 1]], rMesh.vertices[rMesh.indices[t + 2]], distance) && (pBestLevel == nullptr || distance < bestDistance))
			{
				pBestLevel = &Globals::Game::levels.objects[i];
				bestDistance = distance;
			}
		}
	}
	if(pBestLevel == nullptr)
	{
		this->message = "No level collision under the cursor.";
		return;
	}
	try
	{
		if(!this->hasOpen || this->openName != pBestLevel->name) this->open(*pBestLevel);
	}
	catch(const std::exception& rError)
	{
		this->message = std::format("Can't edit this collision: {}", rError.what());
		this->hasOpen = false;
		return;
	}
	CollisionEdit* pEdit = this->current();
	if(pEdit == nullptr) return;

	glm::mat4 inverse = glm::inverse(this->toWorld(*pBestLevel));
	glm::vec3 localOrigin = glm::vec3(inverse * glm::vec4(origin, 1.0f));
	glm::vec3 localDirection = glm::vec3(inverse * glm::vec4(direction, 0.0f));
	float t = 0.0f;
	uint32_t triangle = 0;
	if(!pEdit->raycast(localOrigin, localDirection, t, triangle))
	{
		this->message = "No triangle under the cursor.";
		return;
	}
	this->message.clear();
	this->hasHit = true;
	this->hitTriangle = triangle;
	this->hitPoint = localOrigin + localDirection * t;

	if(action_ == PickAction::Replace) this->clearSelection();
	bool takes = action_ == PickAction::Toggle || action_ == PickAction::Remove;
	const std::array<uint32_t, 3>& rCorners = pEdit->triangles[triangle].v;
	auto positionOf = [&](uint32_t vertex_) -> const glm::vec3& { return pEdit->vertices[vertex_]; };
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
		bool selected = std::find(this->pickedFaces.begin(), this->pickedFaces.end(), triangle) != this->pickedFaces.end();
		bool want = action_ == PickAction::Remove ? false : action_ == PickAction::Toggle ? !selected : true;
		std::erase(this->pickedFaces, triangle);
		if(want) this->pickedFaces.push_back(triangle);
	}
}

void CollisionPanel::boxSelect(const Camera& rCamera_, const ImVec2& rFrom_, const ImVec2& rTo_, bool additive_)
{
	CollisionEdit* pEdit = this->current();
	Level* pLevel = this->openLevel();
	if(pEdit == nullptr || pLevel == nullptr)
	{
		this->message = "Click a level's collision first, then box-select on it.";
		return;
	}
	glm::mat4 toWorld = this->toWorld(*pLevel);
	ScreenBox box(rFrom_, rTo_);
	if(!additive_) this->clearSelection();

	/* The vertices' window positions, once; one behind the camera is never inside. */
	std::vector<ImVec2> points(pEdit->vertices.size());
	std::vector<char> inside(pEdit->vertices.size(), 0);
	for(size_t vertex = 0; vertex < pEdit->vertices.size(); vertex++)
	{
		inside[vertex] = project(rCamera_, glm::vec3(toWorld * glm::vec4(pEdit->vertices[vertex], 1.0f)), points[vertex]) && box.contains(points[vertex]);
	}
	std::set<PositionKey> havePositions;
	for(uint32_t vertex : this->pickedVertices) havePositions.insert(positionKey(pEdit->vertices[vertex]));
	auto pickVertex = [&](uint32_t vertex_)
	{
		if(havePositions.insert(positionKey(pEdit->vertices[vertex_])).second) this->pickedVertices.push_back(vertex_);
	};
	if(this->mode == Vertex)
	{
		for(const CollisionEdit::Triangle& rTriangle : pEdit->triangles)
		{
			for(uint32_t vertex : rTriangle.v)
			{
				if(inside[vertex]) pickVertex(vertex);
			}
		}

		/* The vertices the editor added have no triangle until connected. */
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
			PositionKey first = positionKey(pEdit->vertices[a_]);
			PositionKey second = positionKey(pEdit->vertices[b_]);
			return first < second ? std::make_pair(first, second) : std::make_pair(second, first);
		};
		for(const auto& [a, b] : this->pickedEdges) haveEdges.insert(edgeKey(a, b));
		for(const CollisionEdit::Triangle& rTriangle : pEdit->triangles)
		{
			for(int i = 0; i < 3; i++)
			{
				uint32_t a = rTriangle.v[i];
				uint32_t b = rTriangle.v[(i + 1) % 3];
				if(!inside[a] || !inside[b] || !haveEdges.insert(edgeKey(a, b)).second) continue;
				this->pickedEdges.push_back({a, b});
				pickVertex(a);
				pickVertex(b);
			}
		}
	}
	else
	{
		std::vector<char> have(pEdit->triangles.size(), 0);
		for(uint32_t face : this->pickedFaces) have[face] = 1;
		for(uint32_t face = 0; face < pEdit->triangles.size(); face++)
		{
			const std::array<uint32_t, 3>& rCorners = pEdit->triangles[face].v;
			if(have[face] || !inside[rCorners[0]] || !inside[rCorners[1]] || !inside[rCorners[2]]) continue;
			this->pickedFaces.push_back(face);
		}
	}
	this->message.clear();
}

void CollisionPanel::deleteSelection()
{
	CollisionEdit* pEdit = this->current();
	if(pEdit == nullptr) return;
	std::vector<uint32_t> doomed = this->selectedTriangles();
	if(doomed.empty()) return;
	pEdit->pushUndo();
	pEdit->deleteTriangles(doomed);
	this->clearSelection();
	this->hasHit = false;
	this->apply();
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

void CollisionPanel::connect(const glm::vec3& rCameraFront_)
{
	CollisionEdit* pEdit = this->current();
	Level* pLevel = this->openLevel();
	if(pEdit == nullptr || pLevel == nullptr) return;

	/* The last three picked corners at distinct positions. */
	std::vector<uint32_t> corners;
	for(auto it = this->pickedVertices.rbegin(); it != this->pickedVertices.rend() && corners.size() < 3; ++it)
	{
		bool repeated = std::any_of(corners.begin(), corners.end(), [&](uint32_t other_) { return pEdit->vertices[other_] == pEdit->vertices[*it]; });
		if(!repeated) corners.push_back(*it);
	}
	if(corners.size() < 3)
	{
		this->message = "Connect needs three vertices picked (an edge counts as two).";
		return;
	}
	std::reverse(corners.begin(), corners.end());

	/* The triangle faces the camera: its normal points against the way the camera looks. */
	glm::vec3 normal = glm::cross(pEdit->vertices[corners[1]] - pEdit->vertices[corners[0]], pEdit->vertices[corners[2]] - pEdit->vertices[corners[0]]);
	glm::vec3 front = glm::mat3(glm::inverse(this->toWorld(*pLevel))) * rCameraFront_;
	if(glm::dot(normal, front) > 0.0f) std::swap(corners[1], corners[2]);
	pEdit->pushUndo();
	pEdit->addTriangle(corners[0], corners[1], corners[2], this->value);
	this->message.clear();
	this->apply();
}

void CollisionPanel::addVertexAtHit()
{
	CollisionEdit* pEdit = this->current();
	if(pEdit == nullptr || !this->hasHit) return;
	pEdit->pushUndo();
	uint32_t vertex = pEdit->addVertex(this->hitPoint);
	this->clearSelection();
	this->pickedVertices.push_back(vertex);
}

void CollisionPanel::update(GLFWwindow* pWindow_, const Camera& rCamera_, Gizmo& rGizmo_, bool uiWantsMouse_, bool cameraDragging_)
{
	this->checkLoad();
	this->cameraFront = rCamera_.front;

	/* The Move gizmo works on one point, the centre of the selected vertices: the selection moves by what the point moved (taken into the collision's space). The live collision is made again when the handle is let go. */
	glm::vec3 handleWorld;
	Level* pLevel = this->openLevel();
	if(pLevel != nullptr && this->handle(handleWorld))
	{
		std::vector<glm::vec3> positions{handleWorld};
		std::vector<glm::vec3> rotations{glm::vec3(0.0f)};
		if(rGizmo_.updateLevels(pWindow_, rCamera_, positions, rotations, uiWantsMouse_, cameraDragging_))
		{
			CollisionEdit* pEdit = this->current();
			if(!this->dragUndoPushed)
			{
				pEdit->pushUndo();
				this->dragUndoPushed = true;
			}
			glm::mat4 inverse = glm::inverse(this->toWorld(*pLevel));
			glm::vec3 delta = glm::vec3(inverse * glm::vec4(positions[0], 1.0f)) - glm::vec3(inverse * glm::vec4(handleWorld, 1.0f));
			for(uint32_t vertex : this->selectedVertices()) pEdit->vertices[vertex] += delta;
			pEdit->changed = true;
			this->applyPending = true;
		}
	}
	if(!rGizmo_.wantsMouse())
	{
		this->dragUndoPushed = false;
		if(this->applyPending) this->apply();
	}

	/* A left press the UI, the gizmo and the camera didn't take picks when it is let go (as Editor's click to select). */
	bool leftDown = glfwGetMouseButton(pWindow_, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
	if(leftDown && !this->leftWasDown && !cameraDragging_ && !uiWantsMouse_ && !rGizmo_.wantsMouse())
	{
		this->clickPending = true;
		glfwGetCursorPos(pWindow_, &this->clickX, &this->clickY);
	}
	if(cameraDragging_) this->clickPending = false;
	if(!leftDown && this->leftWasDown && this->clickPending)
	{
		this->clickPending = false;
		bool additive = glfwGetKey(pWindow_, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS || glfwGetKey(pWindow_, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;
		this->pick(rCamera_, pWindow_, this->clickX, this->clickY, additive ? PickAction::Toggle : PickAction::Replace);
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
	if(rightDown && !this->rightWasDown && !uiWantsMouse_ && !cameraDragging_)
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
			bool additive = glfwGetKey(pWindow_, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS || glfwGetKey(pWindow_, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;
			this->boxSelect(rCamera_, ImVec2(static_cast<float>(this->rightX), static_cast<float>(this->rightY)), ImVec2(static_cast<float>(x), static_cast<float>(y)), additive);
		}
		else if(glfwGetTime() - this->rightTime <= RightClickSeconds) this->pick(rCamera_, pWindow_, this->rightX, this->rightY, PickAction::Remove);
	}
	if(!rightDown) this->boxing = false;
	this->rightWasDown = rightDown;
}

void CollisionPanel::drawOverlay(const Camera& rCamera_, Gizmo& rGizmo_)
{
	if(this->boxing)
	{
		ScreenBox box(ImVec2(static_cast<float>(this->rightX), static_cast<float>(this->rightY)), ImGui::GetMousePos());
		ImGui::GetBackgroundDrawList()->AddRectFilled(box.low, box.high, BoxFill);
		ImGui::GetBackgroundDrawList()->AddRect(box.low, box.high, BoxOutline);
	}
	CollisionEdit* pEdit = this->current();
	Level* pLevel = this->openLevel();
	if(pEdit == nullptr || pLevel == nullptr) return;
	glm::mat4 toWorld = this->toWorld(*pLevel);
	ImDrawList* pDraw = ImGui::GetBackgroundDrawList();
	auto screen = [&](uint32_t vertex_, ImVec2& rOut_) { return project(rCamera_, glm::vec3(toWorld * glm::vec4(pEdit->vertices[vertex_], 1.0f)), rOut_); };

	/* The collision view's lines are behind the edit while a handle is dragged: the triangles of the selected vertices are drawn here, where they are now. */
	std::vector<uint32_t> selected = this->selectedVertices();
	if(!selected.empty())
	{
		std::vector<bool> used(pEdit->vertices.size(), false);
		for(uint32_t vertex : selected) used[vertex] = true;
		for(const CollisionEdit::Triangle& rTriangle : pEdit->triangles)
		{
			if(!used[rTriangle.v[0]] && !used[rTriangle.v[1]] && !used[rTriangle.v[2]]) continue;
			ImVec2 points[3];
			if(screen(rTriangle.v[0], points[0]) && screen(rTriangle.v[1], points[1]) && screen(rTriangle.v[2], points[2])) pDraw->AddTriangle(points[0], points[1], points[2], WireColor);
		}
	}
	for(uint32_t face : this->pickedFaces)
	{
		if(face >= pEdit->triangles.size()) continue;
		ImVec2 points[3];
		if(screen(pEdit->triangles[face].v[0], points[0]) && screen(pEdit->triangles[face].v[1], points[1]) && screen(pEdit->triangles[face].v[2], points[2]))
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

	/* The vertices the editor added are always marked (they have no triangle until connected); the picked ones are larger. */
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
	if(this->handle(handleWorld)) rGizmo_.drawLevels(rCamera_, {handleWorld});
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

void CollisionPanel::drawTab()
{
	this->checkLoad();
	ImGui::SeparatorText("Collision");
	ImGui::TextWrapped("Left click a level's collision to pick it (Shift adds), right click to take it out, right drag a box to pick everything in it (Shift adds; Alt + right drag looks around). The Move gizmo (key 1) moves the selected vertices. Entities' collision shapes are not edited here.");
	int mode = this->mode;
	ImGui::RadioButton("Vertex", &mode, Vertex);
	ImGui::SameLine();
	ImGui::RadioButton("Edge", &mode, Edge);
	ImGui::SameLine();
	ImGui::RadioButton("Face", &mode, Face);
	if(mode != this->mode)
	{
		this->mode = static_cast<Mode>(mode);
		this->clearSelection();
	}

	CollisionEdit* pEdit = this->current();
	if(pEdit == nullptr)
	{
		ImGui::TextDisabled("No level collision open.");
	}
	else
	{
		ImGui::Text("%s: %zu vertices, %zu triangles%s", pEdit->level.c_str(), pEdit->vertices.size(), pEdit->triangles.size(), pEdit->changed ? " (unsaved)" : "");
		std::vector<uint32_t> triangles = this->selectedTriangles();
		ImGui::Text("Picked: %zu vertices, %zu edges, %zu triangles", this->pickedVertices.size(), this->pickedEdges.size(), triangles.size());

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
		if(ImGui::Button("Delete")) this->deleteSelection();
		ImGui::SameLine();
		if(ImGui::Button("Add vertex")) this->addVertexAtHit();
		ImGui::SameLine();
		if(ImGui::Button("Connect")) this->connect(this->cameraFront);
		ImGui::InputScalar("Value", ImGuiDataType_U32, &this->value);
		ImGui::SameLine();
		if(ImGui::Button("Take")) this->value = values.empty() ? this->value : *values.begin();
		if(ImGui::IsItemHovered()) ImGui::SetTooltip("Takes the lowest value of the selected triangles.");
		if(ImGui::Button("Set value")) this->setValueOfSelection();
		if(ImGui::IsItemHovered()) ImGui::SetTooltip("Gives the Value to the selected triangles. Connect gives it to the new triangle.");
		if(ImGui::Button("Undo"))
		{
			if(pEdit->undo()) this->apply();
			this->clearSelection();
			this->hasHit = false;
		}
		ImGui::SameLine();
		if(ImGui::Button("Revert collision"))
		{
			pEdit->revert();
			this->apply();
			this->clearSelection();
			this->hasHit = false;
		}
	}
	ImGui::Separator();
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
