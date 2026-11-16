#include "TriangleEditor.h"

#ifdef EDITOR
#include "BoxSelect.h"
#include "imgui.h"
#include <algorithm>
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
/* The wireframe is not drawn for a surface with more triangles than this (every frame's cost, and a screen of lines). */
constexpr size_t MaxWireframeFaces = 20000;
/* How far (in the surface's units) an extruded vertex starts from what it comes from. */
constexpr float ExtrudeOffset = 0.25f;
/* The share of a face's area that has to be in a box for the box to pick it. */
constexpr float BoxFaceShare = 0.4f;

/* The mesh's edges are opaque white and its vertices red, drawn over the scene. */
constexpr ImU32 WireColor = IM_COL32(255, 255, 255, 255);
constexpr ImU32 VertexDotColor = IM_COL32(255, 140, 130, 255);
constexpr ImU32 SelectedColor = IM_COL32(255, 200, 40, 255);
constexpr ImU32 SelectedFill = IM_COL32(255, 160, 40, 90);
/* A right press let go within this time and distance of where it went down is a click, not a camera look. */
constexpr double RightClickSeconds = 0.3;
constexpr double RightClickPixels = 4.0;
constexpr ImU32 AddedColor = IM_COL32(60, 220, 255, 255);
/* The sides and faces of an extrusion that is not made yet (Enter makes them white like the others). */
constexpr ImU32 PendingColor = IM_COL32(150, 150, 150, 255);
constexpr ImU32 PendingFill = IM_COL32(150, 150, 150, 70);
constexpr ImU32 DrawColor = IM_COL32(60, 110, 255, 255);
/* The range of TriangleEditor::mergePixels (the slider). */
constexpr float MinMergePixels = 2.0f;
constexpr float MaxMergePixels = 60.0f;
/* Of the vertices under a click the nearest this many are tested for being hidden; the box selection tests the vertices in the box only up to this many (a ray each, against every triangle), beyond it everything in the box is taken. */
constexpr size_t MaxVisibilityTries = 6;
constexpr size_t MaxVisibilityTests = 600;
/* Merging onto the middle of an edge looks at every edge of the surface, so it is done only for this many moved positions or fewer. */
constexpr size_t MaxEdgeMergeMovers = 16;
constexpr ImU32 MergeColor = IM_COL32(60, 230, 90, 255);

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

/* The triangle faces the camera: its normal points against the way the camera looks. */
static void facePointsAtCamera(TriangleSurface& rEdit_, const glm::mat4& rToWorld_, const glm::vec3& rCameraFront_, std::array<uint32_t, 3>& rFace_)
{
	glm::vec3 normal = glm::cross(rEdit_.position(rFace_[1]) - rEdit_.position(rFace_[0]), rEdit_.position(rFace_[2]) - rEdit_.position(rFace_[0]));
	glm::vec3 front = glm::mat3(glm::inverse(rToWorld_)) * rCameraFront_;
	if(glm::dot(normal, front) > 0.0f) std::swap(rFace_[1], rFace_[2]);
}

/* The face that uses most of the given vertices' positions (at least one), None when none does: what a new triangle next to existing ones continues (its UVs, its texture). */
static uint32_t neighbourFace(TriangleSurface& rSurface_, const std::vector<uint32_t>& rVertices_)
{
	uint32_t best = TriangleSurface::None;
	int bestScore = 0;
	for(uint32_t face = 0; face < rSurface_.triangleCount(); face++)
	{
		int score = 0;
		for(uint32_t vertex : rVertices_)
		{
			for(uint32_t corner : rSurface_.corners(face))
			{
				if(rSurface_.position(corner) == rSurface_.position(vertex))
				{
					score++;
					break;
				}
			}
		}
		if(score > 0 && score >= bestScore)
		{
			best = face;
			bestScore = score;
		}
	}
	return best;
}

void TriangleEditor::clearSelection()
{
	this->pickedVertices.clear();
	this->pickedEdges.clear();
	this->pickedFaces.clear();
}

void TriangleEditor::toggleIsolate()
{
	if(!this->isolate && this->surface() == nullptr)
	{
		this->message = "Isolate needs something open: click it first.";
		return;
	}
	this->isolate = !this->isolate;
	this->message = this->isolate ? "Only the open object is shown (I again shows everything)." : "Everything is shown.";
}

std::vector<uint32_t> TriangleEditor::selectedVertices()
{
	std::vector<uint32_t> result;
	TriangleSurface* pMesh = this->surface();
	if(pMesh == nullptr) return result;

	/* One pass over the vertices against the positions picked (welding each picked vertex alone is too slow for a box selection of thousands). */
	std::set<PositionKey> positions;
	for(uint32_t vertex : this->pickedVertices) positions.insert(positionKey(pMesh->position(vertex)));
	for(uint32_t face : this->pickedFaces)
	{
		for(uint32_t vertex : pMesh->corners(face)) positions.insert(positionKey(pMesh->position(vertex)));
	}
	if(positions.empty()) return result;
	for(uint32_t vertex = 0; vertex < pMesh->vertexCount(); vertex++)
	{
		if(positions.contains(positionKey(pMesh->position(vertex)))) result.push_back(vertex);
	}
	return result;
}

bool TriangleEditor::handle(glm::vec3& rWorld_)
{
	glm::mat4 toWorld;
	TriangleSurface* pEdit = this->surface();
	if(pEdit == nullptr || !this->placement(toWorld)) return false;
	std::vector<uint32_t> vertices = this->selectedVertices();
	if(vertices.empty()) return false;

	/* The centre of the distinct positions (a welded corner counts once). */
	std::set<PositionKey> seen;
	glm::vec3 centre(0.0f);
	size_t distinct = 0;
	for(uint32_t vertex : vertices)
	{
		const glm::vec3& rPosition = pEdit->position(vertex);
		if(!seen.insert(positionKey(rPosition)).second) continue;
		centre += rPosition;
		distinct++;
	}
	centre /= static_cast<float>(distinct);
	rWorld_ = glm::vec3(toWorld * glm::vec4(centre, 1.0f));
	return true;
}

void TriangleEditor::pick(const Camera& rCamera_, GLFWwindow* pWindow_, double x_, double y_, PickAction action_)
{
	glm::vec3 origin;
	glm::vec3 direction;
	if(!windowRay(pWindow_, rCamera_, x_, y_, origin, direction)) return;

	if(!this->openUnderCursor(origin, direction)) return;
	glm::mat4 toWorld;
	TriangleSurface* pEdit = this->surface();
	if(pEdit == nullptr || !this->placement(toWorld)) return;

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
	const std::array<uint32_t, 3>& rCorners = pEdit->corners(face);
	auto positionOf = [&](uint32_t vertex_) -> const glm::vec3& { return pEdit->position(vertex_); };
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
		std::vector<uint32_t> group = this->faceGroup(face);
		bool selected = std::find(this->pickedFaces.begin(), this->pickedFaces.end(), face) != this->pickedFaces.end();
		bool want = action_ == PickAction::Remove ? false : action_ == PickAction::Toggle ? !selected : true;
		for(uint32_t member : group)
		{
			std::erase(this->pickedFaces, member);
			if(want) this->pickedFaces.push_back(member);
		}
	}
}

/* The share (0 to 1) of a triangle's window area that is inside the box: the triangle cut by the box's four sides in turn (Sutherland-Hodgman), its area over the triangle's. */
static float boxCoverage(const ScreenBox& rBox_, const ImVec2 (&rPoints_)[3])
{
	auto area = [](const std::vector<ImVec2>& rPolygon_)
	{
		float twice = 0.0f;
		for(size_t i = 0; i < rPolygon_.size(); i++)
		{
			const ImVec2& rA = rPolygon_[i];
			const ImVec2& rB = rPolygon_[(i + 1) % rPolygon_.size()];
			twice += rA.x * rB.y - rB.x * rA.y;
		}
		return std::abs(twice) * 0.5f;
	};
	std::vector<ImVec2> polygon{rPoints_[0], rPoints_[1], rPoints_[2]};
	float whole = area(polygon);
	if(whole < 1e-4f) return 0.0f;
	for(int side = 0; side < 4 && !polygon.empty(); side++)
	{
		auto inside = [&](const ImVec2& rPoint_)
		{
			if(side == 0) return rPoint_.x >= rBox_.low.x;
			if(side == 1) return rPoint_.x <= rBox_.high.x;
			if(side == 2) return rPoint_.y >= rBox_.low.y;
			return rPoint_.y <= rBox_.high.y;
		};
		auto cut = [&](const ImVec2& rA_, const ImVec2& rB_)
		{
			float limit = side == 0 ? rBox_.low.x : side == 1 ? rBox_.high.x : side == 2 ? rBox_.low.y : rBox_.high.y;
			float t = side < 2 ? (limit - rA_.x) / (rB_.x - rA_.x) : (limit - rA_.y) / (rB_.y - rA_.y);
			return ImVec2(rA_.x + (rB_.x - rA_.x) * t, rA_.y + (rB_.y - rA_.y) * t);
		};
		std::vector<ImVec2> next;
		for(size_t i = 0; i < polygon.size(); i++)
		{
			const ImVec2& rCurrent = polygon[i];
			const ImVec2& rPrevious = polygon[(i + polygon.size() - 1) % polygon.size()];
			if(inside(rCurrent))
			{
				if(!inside(rPrevious)) next.push_back(cut(rPrevious, rCurrent));
				next.push_back(rCurrent);
			}
			else if(inside(rPrevious)) next.push_back(cut(rPrevious, rCurrent));
		}
		polygon = std::move(next);
	}
	return polygon.size() < 3 ? 0.0f : area(polygon) / whole;
}

void TriangleEditor::boxSelect(const Camera& rCamera_, const ImVec2& rFrom_, const ImVec2& rTo_, bool additive_, bool remove_)
{
	glm::mat4 toWorld;
	TriangleSurface* pEdit = this->surface();
	if(pEdit == nullptr || !this->placement(toWorld))
	{
		this->message = "Click something first, then box-select on it.";
		return;
	}
	ScreenBox box(rFrom_, rTo_);
	if(!additive_ && !remove_) this->clearSelection();

	/* The vertices' window positions, once; one behind the camera is never inside. */
	std::vector<ImVec2> points(pEdit->vertexCount());
	std::vector<char> inside(pEdit->vertexCount(), 0);
	for(size_t vertex = 0; vertex < pEdit->vertexCount(); vertex++)
	{
		inside[vertex] = project(rCamera_, glm::vec3(toWorld * glm::vec4(pEdit->position(vertex), 1.0f)), points[vertex]) && box.contains(points[vertex]);
	}
	/* A face is in the box when this share of its area is (BoxFaceShare), not only when all its corners are. */
	auto faceCovered = [&](uint32_t face_)
	{
		const std::array<uint32_t, 3>& rCorners = pEdit->corners(face_);
		ImVec2 triangle[3];
		for(int i = 0; i < 3; i++)
		{
			if(!project(rCamera_, glm::vec3(toWorld * glm::vec4(pEdit->position(rCorners[i]), 1.0f)), triangle[i])) return false;
		}
		return boxCoverage(box, triangle) >= BoxFaceShare;
	};
	std::vector<char> rawInside = inside;
	if(remove_)
	{
		/* Takes out what is inside the box: the vertices, the edges with both ends inside and the faces the box covers. */
		std::erase_if(this->pickedVertices, [&](uint32_t vertex_) { return inside[vertex_] != 0; });
		std::erase_if(this->pickedEdges, [&](const std::pair<uint32_t, uint32_t>& rEdge_) { return inside[rEdge_.first] != 0 && inside[rEdge_.second] != 0; });
		std::erase_if(this->pickedFaces, [&](uint32_t face_) { return faceCovered(face_); });
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
	for(uint32_t vertex : this->pickedVertices) havePositions.insert(positionKey(pEdit->position(vertex)));
	auto pickVertex = [&](uint32_t vertex_)
	{
		if(havePositions.insert(positionKey(pEdit->position(vertex_))).second) this->pickedVertices.push_back(vertex_);
	};
	if(this->mode == Vertex)
	{
		for(uint32_t faceNumber = 0; faceNumber < pEdit->triangleCount(); faceNumber++)
		{
			const std::array<uint32_t, 3>& rFace = pEdit->corners(faceNumber);
			for(uint32_t vertex : rFace)
			{
				if(inside[vertex]) pickVertex(vertex);
			}
		}

		/* The vertices the editor added have no face until connected. */
		for(size_t vertex = pEdit->originalVertexCount(); vertex < pEdit->vertexCount(); vertex++)
		{
			if(inside[vertex]) pickVertex(static_cast<uint32_t>(vertex));
		}
	}
	else if(this->mode == Edge)
	{
		std::set<std::pair<PositionKey, PositionKey>> haveEdges;
		auto edgeKey = [&](uint32_t a_, uint32_t b_)
		{
			PositionKey first = positionKey(pEdit->position(a_));
			PositionKey second = positionKey(pEdit->position(b_));
			return first < second ? std::make_pair(first, second) : std::make_pair(second, first);
		};
		for(const auto& [a, b] : this->pickedEdges) haveEdges.insert(edgeKey(a, b));
		for(uint32_t faceNumber = 0; faceNumber < pEdit->triangleCount(); faceNumber++)
		{
			const std::array<uint32_t, 3>& rFace = pEdit->corners(faceNumber);
			for(int i = 0; i < 3; i++)
			{
				uint32_t a = rFace[i];
				uint32_t b = rFace[(i + 1) % 3];
				if(!inside[a] || !inside[b] || !haveEdges.insert(edgeKey(a, b)).second) continue;
				this->pickedEdges.push_back({a, b});
				pickVertex(a);
				pickVertex(b);
			}
		}
	}
	else
	{
		std::vector<char> have(pEdit->triangleCount(), 0);
		for(uint32_t face : this->pickedFaces) have[face] = 1;
		for(uint32_t face = 0; face < pEdit->triangleCount(); face++)
		{
			const std::array<uint32_t, 3>& rCorners = pEdit->corners(face);
			if(have[face] || !faceCovered(face)) continue;

			/* Behind the mesh's own faces it is not taken (unless X-ray): when a corner is in the box, one of them has to be seen. */
			bool anyRaw = rawInside[rCorners[0]] || rawInside[rCorners[1]] || rawInside[rCorners[2]];
			bool anySeen = inside[rCorners[0]] || inside[rCorners[1]] || inside[rCorners[2]];
			if(anyRaw && !anySeen) continue;
			this->pickedFaces.push_back(face);
		}
	}
	this->message.clear();
}

void TriangleEditor::flipFaces()
{
	TriangleSurface* pEdit = this->surface();
	glm::mat4 toWorld;
	if(pEdit == nullptr || !this->placement(toWorld) || this->pickedFaces.empty()) return;
	pEdit->pushUndo();
	for(uint32_t face : this->pickedFaces)
	{
		if(face < pEdit->triangleCount()) std::swap(pEdit->corners(face)[1], pEdit->corners(face)[2]);
	}
	pEdit->markChanged();
	this->commit(false);
	this->message = std::format("Flipped {} face(s).", this->pickedFaces.size());
}

void TriangleEditor::selectFaces(bool invert_)
{
	TriangleSurface* pEdit = this->surface();
	if(pEdit == nullptr) return;
	std::vector<char> have(pEdit->triangleCount(), 0);
	for(uint32_t face : this->pickedFaces)
	{
		if(face < have.size()) have[face] = 1;
	}
	this->clearSelection();
	for(uint32_t face = 0; face < pEdit->triangleCount(); face++)
	{
		if(!invert_ || !have[face]) this->pickedFaces.push_back(face);
	}
}

bool TriangleEditor::cancelExtrusion()
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

void TriangleEditor::discard()
{
	TriangleSurface* pEdit = this->surface();
	glm::mat4 toWorld;
	bool had = !this->drawCorners.empty();
	this->drawCorners.clear();
	if(!this->pendingFaces.empty() && pEdit != nullptr && pEdit->discardTo(this->pendingBase))
	{
		if(this->placement(toWorld)) this->commit(false);
		this->clearSelection();
		had = true;
	}
	this->pendingFaces.clear();
	this->message = had ? "Discarded." : "Nothing to discard.";
}

std::vector<uint32_t> TriangleEditor::selectedTriangles()
{
	std::vector<uint32_t> result;
	TriangleSurface* pEdit = this->surface();
	if(pEdit == nullptr) return result;
	if(this->mode == Face) return this->pickedFaces;
	if(this->mode == Vertex)
	{
		std::vector<uint32_t> vertices = this->selectedVertices();
		std::set<uint32_t> set(vertices.begin(), vertices.end());
		for(uint32_t triangle = 0; triangle < pEdit->triangleCount(); triangle++)
		{
			const std::array<uint32_t, 3>& rCorners = pEdit->corners(triangle);
			if(set.contains(rCorners[0]) || set.contains(rCorners[1]) || set.contains(rCorners[2])) result.push_back(triangle);
		}
		return result;
	}

	/* An edge stands for the triangles that share it: those that have it as a side (by position, so welded corners match). */
	auto edgeKey = [&](uint32_t a_, uint32_t b_)
	{
		PositionKey first = positionKey(pEdit->position(a_));
		PositionKey second = positionKey(pEdit->position(b_));
		return first < second ? std::make_pair(first, second) : std::make_pair(second, first);
	};
	std::set<std::pair<PositionKey, PositionKey>> edges;
	for(const auto& [a, b] : this->pickedEdges) edges.insert(edgeKey(a, b));
	for(uint32_t triangle = 0; triangle < pEdit->triangleCount(); triangle++)
	{
		const std::array<uint32_t, 3>& rCorners = pEdit->corners(triangle);
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

void TriangleEditor::deleteSelection()
{
	TriangleSurface* pEdit = this->surface();
	glm::mat4 toWorld;
	if(pEdit == nullptr || !this->placement(toWorld)) return;

	/* The triangles of the selection go, and the vertices the editor added that no triangle uses then (the selected ones: a vertex with no face is deleted with Delete too). The imported vertices stay, they are only gone with their faces. Not while an extrusion or a triangle is half made: their vertices are numbered. */
	std::vector<uint32_t> doomed = this->selectedTriangles();
	std::vector<uint32_t> vertices = this->selectedVertices();
	bool idle = this->pendingFaces.empty() && this->drawCorners.empty();

	/* Whether an added vertex of the selection would have no face left. */
	bool droppable = false;
	if(idle)
	{
		std::set<uint32_t> gone(doomed.begin(), doomed.end());
		std::vector<char> used(pEdit->vertexCount(), 0);
		for(uint32_t face = 0; face < pEdit->triangleCount(); face++)
		{
			if(gone.contains(face)) continue;
			for(uint32_t corner : pEdit->corners(face)) used[corner] = 1;
		}
		for(uint32_t vertex : vertices)
		{
			if(vertex >= pEdit->originalVertexCount() && !used[vertex]) droppable = true;
		}
	}
	if(doomed.empty() && !droppable)
	{
		this->message = "Nothing to delete: an imported vertex goes with its faces.";
		return;
	}
	pEdit->pushUndo();
	if(!doomed.empty()) pEdit->deleteTriangles(doomed);
	if(droppable) pEdit->dropUnusedAdded(vertices);
	this->clearSelection();
	this->hasHit = false;
	this->commit(false);
}

bool TriangleEditor::redo()
{
	TriangleSurface* pEdit = this->surface();
	glm::mat4 toWorld;
	if(pEdit == nullptr || !pEdit->redo()) return false;
	if(this->placement(toWorld)) this->commit(false);
	this->pendingFaces.clear();
	this->drawCorners.clear();

	/* In Texturing the picked faces stay (the UV box then shows what the undo gave back); the ones the step took away are dropped. Elsewhere the selection goes, since a vertex or an edge may no longer exist. */
	if(this->texturing)
	{
		uint32_t count = pEdit->triangleCount();
		std::erase_if(this->pickedFaces, [&](uint32_t face_) { return face_ >= count; });
	}
	else this->clearSelection();
	this->hasHit = false;
	this->undone++;
	return true;
}

bool TriangleEditor::undo()
{
	TriangleSurface* pEdit = this->surface();
	glm::mat4 toWorld;
	if(pEdit == nullptr || !pEdit->undo()) return false;
	if(this->placement(toWorld)) this->commit(false);
	this->pendingFaces.clear();
	this->drawCorners.clear();

	/* In Texturing the picked faces stay (the UV box then shows what the undo gave back); the ones the step took away are dropped. Elsewhere the selection goes, since a vertex or an edge may no longer exist. */
	if(this->texturing)
	{
		uint32_t count = pEdit->triangleCount();
		std::erase_if(this->pickedFaces, [&](uint32_t face_) { return face_ >= count; });
	}
	else this->clearSelection();
	this->hasHit = false;
	this->undone++;
	return true;
}

void TriangleEditor::connect(const glm::vec3& rCameraFront_)
{
	TriangleSurface* pEdit = this->surface();
	glm::mat4 toWorld;
	if(pEdit == nullptr || !this->placement(toWorld)) return;

	/* The last three picked corners at distinct positions. */
	std::vector<uint32_t> corners;
	for(auto it = this->pickedVertices.rbegin(); it != this->pickedVertices.rend() && corners.size() < 3; ++it)
	{
		bool repeated = std::any_of(corners.begin(), corners.end(), [&](uint32_t other_) { return pEdit->position(other_) == pEdit->position(*it); });
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
	pEdit->addTriangle(face[0], face[1], face[2], pEdit->continuesTexture() ? neighbourFace(*pEdit, corners) : TriangleSurface::None);
	this->commit(false);
	this->message.clear();
}

void TriangleEditor::cycleExtrudeKind()
{
	bool wasQuad = this->extrudeKind == ExtrudeKind::Quad;
	this->extrudeKind = wasQuad ? ExtrudeKind::Triangle : ExtrudeKind::Quad;
	TriangleSurface* pEdit = this->surface();
	if(this->pendingFaces.empty() || pEdit == nullptr) return;

	/* Where the new vertices were, as offsets from their sides: a triangle's one vertex from the side's middle, a quad's two from their own ends. The other kind starts from the same offset (a quad's average becomes the triangle's; a triangle's gives both quad vertices), so what was moved stays moved. */
	struct Placed
	{
		uint32_t a;
		uint32_t b;
		glm::vec3 offsetA;
		glm::vec3 offsetB;
	};
	std::vector<Placed> placed;
	for(const Extrusion& rExtrusion : this->pendingFaces)
	{
		glm::vec3 pa = pEdit->position(rExtrusion.a);
		glm::vec3 pb = pEdit->position(rExtrusion.b);
		if(wasQuad)
		{
			glm::vec3 average = ((pEdit->position(rExtrusion.c) - pa) + (pEdit->position(rExtrusion.d) - pb)) * 0.5f;
			placed.push_back({rExtrusion.a, rExtrusion.b, average, average});
		}
		else
		{
			glm::vec3 offset = pEdit->position(rExtrusion.c) - (pa + pb) * 0.5f;
			placed.push_back({rExtrusion.a, rExtrusion.b, offset, offset});
		}
	}
	bool wasFollowing = this->following;
	std::vector<std::pair<uint32_t, uint32_t>> sides;
	for(const Placed& rPlaced : placed) sides.push_back({rPlaced.a, rPlaced.b});
	this->discard();
	this->mode = Edge;
	this->clearSelection();
	this->pickedEdges = sides;
	this->extrude();
	if(this->pendingFaces.size() != placed.size()) return;
	for(size_t i = 0; i < placed.size(); i++)
	{
		const Extrusion& rExtrusion = this->pendingFaces[i];
		glm::vec3 pa = pEdit->position(rExtrusion.a);
		glm::vec3 pb = pEdit->position(rExtrusion.b);
		if(rExtrusion.quad)
		{
			pEdit->position(rExtrusion.c) = pa + placed[i].offsetA;
			pEdit->position(rExtrusion.d) = pb + placed[i].offsetB;
		}
		else pEdit->position(rExtrusion.c) = (pa + pb) * 0.5f + placed[i].offsetA;
	}
	pEdit->markChanged();
	this->commit(false);
	this->following = wasFollowing && followCursor;
	this->beginFollow();
}

void TriangleEditor::beginFollow()
{
	TriangleSurface* pEdit = this->surface();
	this->followStart.clear();
	this->followReady = false;
	if(pEdit == nullptr) return;
	for(uint32_t vertex : this->pendingMade()) this->followStart.push_back({vertex, pEdit->position(vertex)});
}

void TriangleEditor::extrude()
{
	TriangleSurface* pEdit = this->surface();
	glm::mat4 toWorld;
	if(pEdit == nullptr || !this->placement(toWorld)) return;

	/* The corners (one vertex each) and the pairs of corners the extrusion starts from: an Edge mode edge, or two picked vertices. */
	std::vector<uint32_t> corners;
	for(uint32_t vertex : this->pickedVertices)
	{
		bool repeated = std::any_of(corners.begin(), corners.end(), [&](uint32_t other_) { return pEdit->position(other_) == pEdit->position(vertex); });
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
	auto makeVertex = [&](uint32_t from_, const glm::vec3& rPosition_) { return pEdit->addVertex(rPosition_, from_); };

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
			glm::vec3 pa = pEdit->position(a);
			glm::vec3 pb = pEdit->position(b);
			glm::vec3 away = glm::cross(front, pb - pa);
			if(glm::length(away) < 1e-5f) away = glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), pb - pa);
			if(glm::length(away) < 1e-5f) away = vertexOffset;
			away = glm::normalize(away) * ExtrudeOffset;
			for(uint32_t faceNumber = 0; faceNumber < pEdit->triangleCount(); faceNumber++)
			{
				const std::array<uint32_t, 3>& rFace = pEdit->corners(faceNumber);
				int hits = 0;
				glm::vec3 third = pa;
				for(uint32_t corner : rFace)
				{
					const glm::vec3& rPosition = pEdit->position(corner);
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
		for(uint32_t corner : corners) made.push_back(makeVertex(corner, pEdit->position(corner) + vertexOffset));
	}
	pEdit->markChanged();
	this->commit(false);

	/* The new vertices are the selection, in Vertex mode, so that the Move gizmo takes them. */
	this->mode = Vertex;
	this->clearSelection();
	this->pickedVertices = made;
	this->following = !sides.empty() && followCursor;
	this->beginFollow();
	this->message = sides.empty() ? std::string() : "Move the new vertices (near another vertex they merge, green; hold Ctrl to keep them apart), then press Enter to make the faces.";
}

std::vector<uint32_t> TriangleEditor::pendingMade(std::vector<int>* pGroups_) const
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

std::vector<TriangleEditor::Merge> TriangleEditor::findMerges(const std::vector<uint32_t>& rMovers_, const std::vector<int>& rGroups_)
{
	std::vector<Merge> merges;
	TriangleSurface* pEdit = this->surface();
	glm::mat4 toWorld;
	if(this->mergeOff || rMovers_.empty() || pEdit == nullptr || !this->placement(toWorld)) return merges;

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
	screens.reserve(pEdit->vertexCount());
	for(uint32_t vertex = 0; vertex < pEdit->vertexCount(); vertex++) screens.push_back(screenOf(pEdit->position(vertex)));
	/* The same depth: within 10% of it from a perspective camera, within a tenth of the view's height from an orthographic one. */
	auto sameDepth = [&](float a_, float b_) { return std::abs(a_ - b_) <= (this->orthographicView ? 0.1f * this->orthographicHeight : 0.1f * std::max(a_, b_)); };

	/* The sides pending extrusions start from stay out: a new vertex merged into one of their ends would make no face. */
	std::set<uint32_t> moving(rMovers_.begin(), rMovers_.end());
	std::vector<glm::vec3> keepOut;
	for(const Extrusion& rExtrusion : this->pendingFaces)
	{
		keepOut.push_back(pEdit->position(rExtrusion.a));
		keepOut.push_back(pEdit->position(rExtrusion.b));
	}
	auto kept = [&](const glm::vec3& rPosition_) { return std::find(keepOut.begin(), keepOut.end(), rPosition_) != keepOut.end(); };

	/* One mover per position (the others at it go along). */
	std::vector<size_t> order;
	std::vector<glm::vec3> done;
	for(size_t i = 0; i < rMovers_.size(); i++)
	{
		if(rMovers_[i] >= pEdit->vertexCount()) continue;
		const glm::vec3& rPosition = pEdit->position(rMovers_[i]);
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
		const glm::vec3& rPosition = pEdit->position(mover);
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
			const glm::vec3& rTarget = pEdit->position(vertex);
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
			glm::vec3 place = result[m] ? result[m]->position : pEdit->position(other);
			if(place == rPosition || kept(place)) continue;
			best = distance;
			found = Merge{mover, other, place, Merge::None, Merge::None};
		}
		if(!found && edges)
		{
			float bestEdge = this->mergePixels;
			for(uint32_t faceNumber = 0; faceNumber < pEdit->triangleCount(); faceNumber++)
			{
				const std::array<uint32_t, 3>& rFace = pEdit->corners(faceNumber);
				for(int i = 0; i < 3; i++)
				{
					uint32_t a = rFace[i];
					uint32_t b = rFace[(i + 1) % 3];
					const Screen& rA = screens[a];
					const Screen& rB = screens[b];
					if(!rA.valid || !rB.valid || moving.contains(a) || moving.contains(b)) continue;
					const glm::vec3& rPa = pEdit->position(a);
					const glm::vec3& rPb = pEdit->position(b);
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

void TriangleEditor::applyMerges(const std::vector<Merge>& rMerges_, const std::vector<uint32_t>& rMovers_)
{
	TriangleSurface* pEdit = this->surface();
	if(pEdit == nullptr) return;
	/* Every vertex at the mover's position goes with it; the new positions are worked out before any is set. */
	std::vector<std::pair<uint32_t, glm::vec3>> moves;
	for(const Merge& rMerge : rMerges_)
	{
		glm::vec3 old = pEdit->position(rMerge.mover);
		for(uint32_t vertex : rMovers_)
		{
			if(pEdit->position(vertex) == old) moves.push_back({vertex, rMerge.position});
		}
	}
	for(const auto& [vertex, position] : moves) pEdit->position(vertex) = position;
	if(!moves.empty()) pEdit->markChanged();

	/*
	A merge into an edge splits the faces that use the edge at the vertex (each into two, as new faces: the original face is gone, with its texture kept), so that no gap is left; the vertex's uv is the edge's two uv blended where it lies.
	*/
	for(const Merge& rMerge : rMerges_)
	{
		if(rMerge.edgeA == Merge::None) continue;
		glm::vec3 pa = pEdit->position(rMerge.edgeA);
		glm::vec3 pb = pEdit->position(rMerge.edgeB);
		float length = glm::dot(pb - pa, pb - pa);
		if(length > 0.0f)
		{
			float t = std::clamp(glm::dot(rMerge.position - pa, pb - pa) / length, 0.0f, 1.0f);
			pEdit->blendVertex(rMerge.mover, rMerge.edgeA, rMerge.edgeB, t);
		}
		std::vector<uint32_t> removed;
		std::vector<std::array<uint32_t, 4>> added;
		for(uint32_t face = 0; face < pEdit->triangleCount(); face++)
		{
			const std::array<uint32_t, 3> corners = pEdit->corners(face);
			for(int i = 0; i < 3; i++)
			{
				const glm::vec3& rFrom = pEdit->position(corners[i]);
				const glm::vec3& rTo = pEdit->position(corners[(i + 1) % 3]);
				if(!((rFrom == pa && rTo == pb) || (rFrom == pb && rTo == pa))) continue;
				uint32_t x = corners[i];
				uint32_t y = corners[(i + 1) % 3];
				uint32_t z = corners[(i + 2) % 3];
				added.push_back({x, rMerge.mover, z, face});
				added.push_back({rMerge.mover, y, z, face});
				removed.push_back(face);
				break;
			}
		}
		/* The new triangles are made while the old ones still stand (they take what the old one has: texture, value), then the old ones go. */
		for(const std::array<uint32_t, 4>& rNew : added) pEdit->addTriangle(rNew[0], rNew[1], rNew[2], rNew[3]);
		pEdit->deleteTriangles(removed);
		pEdit->markChanged();
		/* The faces' numbers changed. */
		this->pickedFaces.clear();
	}
}

bool TriangleEditor::edgeAt(const glm::vec3& rA_, const glm::vec3& rB_)
{
	TriangleSurface* pEdit = this->surface();
	if(pEdit == nullptr || rA_ == rB_) return false;
	for(uint32_t face = 0; face < pEdit->triangleCount(); face++)
	{
		const std::array<uint32_t, 3>& rCorners = pEdit->corners(face);
		for(int i = 0; i < 3; i++)
		{
			const glm::vec3& rFrom = pEdit->position(rCorners[i]);
			const glm::vec3& rTo = pEdit->position(rCorners[(i + 1) % 3]);
			if((rFrom == rA_ && rTo == rB_) || (rFrom == rB_ && rTo == rA_)) return true;
		}
	}
	return false;
}

void TriangleEditor::mergeSelection()
{
	TriangleSurface* pEdit = this->surface();
	glm::mat4 toWorld;
	if(pEdit == nullptr || !this->placement(toWorld)) return;
	std::vector<uint32_t> movers = this->selectedVertices();
	std::vector<Merge> merges = this->findMerges(movers);
	if(merges.empty()) return;
	this->applyMerges(merges, movers);
	this->commit(false);
	this->message = std::format("Merged {} corner(s) into vertices they were moved onto (hold Ctrl when letting go to keep them apart; a corner on an edge splits it).", merges.size());
}

void TriangleEditor::commitExtrusion()
{
	TriangleSurface* pEdit = this->surface();
	glm::mat4 toWorld;
	if(pEdit == nullptr || !this->placement(toWorld)) return;
	if(this->pendingFaces.empty())
	{
		/* Three picked vertices joined: the triangle between them. */
		if(this->pickedVertices.size() >= 3) this->connect(this->cameraFront);
		return;
	}

	/* The extrusion, its moves and the faces are one undo step: back to before E (the steps since, E's and the moves', go; a step left would give back vertices with no face). */
	pEdit->collapseUndo(this->pendingBase);

	/* The new vertices near another vertex are merged into it first (not with Ctrl held); a face whose corners end up on one position (or a line) is left out. */
	std::vector<int> groups;
	std::vector<uint32_t> made = this->pendingMade(&groups);
	std::vector<Merge> merges = this->findMerges(made, groups);
	this->applyMerges(merges, made);

	/* The new faces continue the face each side belongs to (the newest one that has it, so an earlier extrusion's face counts, whatever texture it was given): the new vertices get the uv its mapping gives them (a copied uv makes a flat face when extruded along a wall) and the faces take its texture. */
	std::vector<uint32_t> likes;
	for(const Extrusion& rExtrusion : this->pendingFaces)
	{
		uint32_t like = neighbourFace(*pEdit, {rExtrusion.a, rExtrusion.b});
		likes.push_back(like);
		if(like == TriangleSurface::None) continue;
		std::array<uint32_t, 3> mapping = pEdit->corners(like);
		pEdit->extendUv(rExtrusion.c, mapping[0], mapping[1], mapping[2]);
		if(rExtrusion.quad) pEdit->extendUv(rExtrusion.d, mapping[0], mapping[1], mapping[2]);
	}

	size_t count = 0;
	auto addFace = [&](std::array<uint32_t, 3> face_, uint32_t like_)
	{
		const glm::vec3& rA = pEdit->position(face_[0]);
		const glm::vec3& rB = pEdit->position(face_[1]);
		const glm::vec3& rC = pEdit->position(face_[2]);
		if(rA == rB || rB == rC || rA == rC) return;
		facePointsAtCamera(*pEdit, toWorld, this->cameraFront, face_);
		pEdit->addTriangle(face_[0], face_[1], face_[2], like_);
		count++;
	};
	for(size_t i = 0; i < this->pendingFaces.size(); i++)
	{
		const Extrusion& rExtrusion = this->pendingFaces[i];
		uint32_t like = pEdit->continuesTexture() ? likes[i] : TriangleSurface::None;
		if(rExtrusion.quad)
		{
			addFace({rExtrusion.a, rExtrusion.b, rExtrusion.d}, like);
			addFace({rExtrusion.a, rExtrusion.d, rExtrusion.c}, like);
		}
		else addFace({rExtrusion.a, rExtrusion.b, rExtrusion.c}, like);
	}
	this->message = std::format("Made {} face(s){}; pick them in Face mode.", count, merges.empty() ? "" : std::format(", merged {} corner(s)", merges.size()));
	this->pendingFaces.clear();
	this->commit(false);
}

void TriangleEditor::addVertexAtHit()
{
	TriangleSurface* pEdit = this->surface();
	glm::mat4 toWorld;
	if(pEdit == nullptr || !this->placement(toWorld) || !this->hasHit || this->hitFace >= pEdit->triangleCount()) return;
	pEdit->pushUndo();
	uint32_t vertex = pEdit->addVertex(this->hitPoint, pEdit->corners(this->hitFace)[0]);
	this->commit(false);
	this->clearSelection();
	this->pickedVertices.push_back(vertex);
}

bool TriangleEditor::nearestVertex(const Camera& rCamera_, double x_, double y_, uint32_t& rVertex_)
{
	TriangleSurface* pEdit = this->surface();
	glm::mat4 toWorld;
	if(pEdit == nullptr || !this->placement(toWorld)) return false;
	/* The vertices within the click radius, nearest first; the first one that is not hidden behind a face (a few are tried). */
	std::vector<std::pair<float, uint32_t>> candidates;
	for(size_t vertex = 0; vertex < pEdit->vertexCount(); vertex++)
	{
		ImVec2 point;
		if(!project(rCamera_, glm::vec3(toWorld * glm::vec4(pEdit->position(vertex), 1.0f)), point)) continue;
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

bool TriangleEditor::vertexVisible(const Camera& rCamera_, TriangleSurface& rEdit_, const glm::mat4& rToWorld_, uint32_t vertex_) const
{
	if(this->xray) return true;
	/*
	A ray from the vertex towards the camera (parallel to the view direction for an orthographic one), started a little way along it so that the faces the vertex belongs to are not hit: a face in between hides it. Only the open mesh hides; other meshes and entities don't. A vertex the editor added has no face yet and is seen like the others.
	*/
	glm::vec3 world = glm::vec3(rToWorld_ * glm::vec4(rEdit_.position(vertex_), 1.0f));
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

bool TriangleEditor::drawClick(const Camera& rCamera_, GLFWwindow* pWindow_, double x_, double y_, bool shift_)
{
	TriangleSurface* pEdit = this->surface();
	glm::mat4 toWorld;
	if(pEdit == nullptr || !this->placement(toWorld)) return false;
	auto positionOf = [&](const DrawCorner& rCorner_) { return rCorner_.planned ? rCorner_.position : pEdit->position(rCorner_.vertex); };

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
		bool repeated = std::any_of(this->drawCorners.begin(), this->drawCorners.end(), [&](const DrawCorner& rCorner_) { return positionOf(rCorner_) == pEdit->position(vertex); });
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

void TriangleEditor::finishTriangle(bool keepGoing_)
{
	TriangleSurface* pEdit = this->surface();
	glm::mat4 toWorld;
	if(pEdit == nullptr || !this->placement(toWorld) || this->drawCorners.size() != 3) return;

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
		face[i] = pEdit->addVertex(rCorner.position, source);
	}

	/* The new triangle continues a face next to it: its new vertices get the uv that face's mapping gives them (a copied uv would make a flat triangle in uv space), and it takes that face's texture. */
	std::vector<uint32_t> existing;
	for(size_t i = 0; i < 3; i++)
	{
		if(!this->drawCorners[i].planned) existing.push_back(face[i]);
	}
	uint32_t neighbour = neighbourFace(*pEdit, existing);
	if(neighbour != TriangleSurface::None)
	{
		std::array<uint32_t, 3> mapping = pEdit->corners(neighbour);
		for(size_t i = 0; i < 3; i++)
		{
			if(this->drawCorners[i].planned) pEdit->extendUv(face[i], mapping[0], mapping[1], mapping[2]);
		}
	}
	std::array<uint32_t, 3> corners = face;
	facePointsAtCamera(*pEdit, toWorld, this->cameraFront, face);
	pEdit->addTriangle(face[0], face[1], face[2], pEdit->continuesTexture() ? neighbour : TriangleSurface::None);
	pEdit->markChanged();
	this->commit(false);

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

void TriangleEditor::update(GLFWwindow* pWindow_, EditorCamera& rCamera_, Gizmo& rGizmo_, bool uiWantsMouse_, bool cameraDragging_)
{
	this->checkLoad();
	this->cameraFront = rCamera_.front;
	/* Holding Ctrl keeps merging off (the green merges go while it is held). */
	this->mergeOff = !this->texturing && !ImGui::GetIO().WantTextInput && (ImGui::IsKeyDown(ImGuiKey_LeftCtrl) || ImGui::IsKeyDown(ImGuiKey_RightCtrl));
	this->viewProjection = rCamera_.projection * rCamera_.view;
	this->viewMatrix = rCamera_.view;
	this->orthographicView = rCamera_.orthographic;
	this->orthographicHeight = rCamera_.orthoHeight;
	/* What stays drawn while isolating. */
	this->isolatePtr = this->isolate ? this->isolationTarget() : nullptr;
	if(!ImGui::GetIO().WantTextInput && !ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_I, false)) this->toggleIsolate();
	/* The drawing stops with the mode, and when an undo took out a vertex it used. */
	TriangleSurface* pCurrent = this->surface();
	if(this->mode != Vertex || this->texturing || this->tool != Tool::Create || pCurrent == nullptr || std::any_of(this->drawCorners.begin(), this->drawCorners.end(), [&](const DrawCorner& rCorner_) { return !rCorner_.planned && rCorner_.vertex >= pCurrent->vertexCount(); })) this->drawCorners.clear();

	/*
	With the Settings option on, the new vertices of a pending extrusion move with the cursor: by how far its ray's point on the plane through the vertices (facing the camera) moved since the extrusion started or the kind was switched. A left click places them (the click is no pick), after which the Move gizmo moves them as usual. Nothing follows while the cursor is over the UI or the camera is held.
	*/
	if(this->pendingFaces.empty() || this->texturing) this->following = false;
	if(this->following && !uiWantsMouse_ && !cameraDragging_ && !this->followStart.empty() && pCurrent != nullptr)
	{
		glm::mat4 toWorldNow;
		glm::vec3 rayOrigin;
		glm::vec3 rayDirection;
		double cx = 0.0;
		double cy = 0.0;
		glfwGetCursorPos(pWindow_, &cx, &cy);
		if(this->placement(toWorldNow) && windowRay(pWindow_, rCamera_, cx, cy, rayOrigin, rayDirection))
		{
			glm::vec3 center(0.0f);
			for(const auto& [vertex, rStart] : this->followStart) center += rStart;
			center /= static_cast<float>(this->followStart.size());
			glm::vec3 centerWorld = glm::vec3(toWorldNow * glm::vec4(center, 1.0f));
			float facing = glm::dot(rayDirection, this->cameraFront);
			if(std::abs(facing) > 1e-4f)
			{
				glm::vec3 hit = rayOrigin + rayDirection * (glm::dot(centerWorld - rayOrigin, this->cameraFront) / facing);
				if(!this->followReady)
				{
					this->followCursor0 = hit;
					this->followReady = true;
				}
				glm::mat4 inverse = glm::inverse(toWorldNow);
				glm::vec3 delta = glm::vec3(inverse * glm::vec4(hit, 1.0f)) - glm::vec3(inverse * glm::vec4(this->followCursor0, 1.0f));
				for(const auto& [vertex, rStart] : this->followStart) pCurrent->position(vertex) = rStart + delta;
				pCurrent->markChanged();
				this->commit(true);
			}
		}
	}
	bool followingNow = this->following;

	/* The Move gizmo works on one point, the centre of the selected vertices: the selection moves by what the point moved (taken into the mesh's space). */
	glm::vec3 handleWorld;
	glm::mat4 toWorld;
	if(!this->texturing && !followingNow && this->handle(handleWorld) && this->placement(toWorld))
	{
		std::vector<glm::vec3> positions{handleWorld};
		std::vector<glm::vec3> rotations{glm::vec3(0.0f)};
		std::vector<glm::vec3> pivots{glm::vec3(0.0f)};
		if(rGizmo_.updateLevels(pWindow_, rCamera_, positions, rotations, pivots, uiWantsMouse_, cameraDragging_))
		{
			TriangleSurface* pEdit = this->surface();
			if(!this->dragUndoPushed)
			{
				pEdit->pushUndo();
				this->dragUndoPushed = true;
			}
			glm::mat4 inverse = glm::inverse(toWorld);
			glm::vec3 delta = glm::vec3(inverse * glm::vec4(positions[0], 1.0f)) - glm::vec3(inverse * glm::vec4(handleWorld, 1.0f));
			for(uint32_t vertex : this->selectedVertices()) pEdit->position(vertex) += delta;
			pEdit->markChanged();
			this->commit(true);
		}
	}
	else
	{
		/* No handles: the gizmo is told, so that it neither keeps a hover nor a drag from before (hovering() would block picks). */
		std::vector<glm::vec3> none;
		std::vector<glm::vec3> noRotations;
		rGizmo_.updateLevels(pWindow_, rCamera_, none, noRotations, none, uiWantsMouse_, cameraDragging_);
	}
	/* A move ends: the moved corners that are near another vertex merge into it (the move's undo step takes it too). */
	if(!rGizmo_.wantsMouse())
	{
		if(this->dragUndoPushed && !this->texturing)
		{
			this->mergeSelection();
			this->moveEnded();
		}
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

	/* P switches between the Create and Transform tabs (the vertex / edge create and edit modes); the tab bar does it in the next drawTab. */
	if(!this->texturing && !ImGui::GetIO().WantTextInput && !ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_P, false)) this->tabRequest = this->tool == Tool::Create ? 0 : 1;

	/* E extrudes the picked edges (or vertices), Enter makes the faces of the extrusions (or the triangle of three picked vertices). */
	if(!this->texturing && this->tool == Tool::Create && !ImGui::GetIO().WantTextInput)
	{
		if(ImGui::IsKeyPressed(ImGuiKey_E, false))
		{
			this->drawCorners.clear();
			this->extrude();
		}
		if(ImGui::IsKeyPressed(ImGuiKey_Backslash, false)) this->cycleExtrudeKind();
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

	if(!leftDown && this->leftWasDown && this->clickPending && this->following)
	{
		/* The click that places a following extrusion (it is no pick). */
		this->clickPending = false;
		this->following = false;
	}
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

void TriangleEditor::drawOverlay(const Camera& rCamera_, Gizmo& rGizmo_)
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
	TriangleSurface* pEdit = this->surface();
	glm::mat4 toWorld;
	if(pEdit == nullptr || !this->placement(toWorld)) return;
	ImDrawList* pDraw = ImGui::GetBackgroundDrawList();
	auto screen = [&](uint32_t vertex_, ImVec2& rOut_) { return project(rCamera_, glm::vec3(toWorld * glm::vec4(pEdit->position(vertex_), 1.0f)), rOut_); };

	this->drawWireframe(rCamera_, toWorld);
	for(uint32_t face : this->pickedFaces)
	{
		if(face >= pEdit->triangleCount()) continue;
		ImVec2 points[3];
		if(screen(pEdit->corners(face)[0], points[0]) && screen(pEdit->corners(face)[1], points[1]) && screen(pEdit->corners(face)[2], points[2]))
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
			if(pEdit->position(rMerge.mover) == pEdit->position(vertex_)) return rMerge.position;
		}
		return pEdit->position(vertex_);
	};
	auto line = [&](uint32_t from_, uint32_t to_)
	{
		glm::vec3 a = mergedPosition(from_);
		glm::vec3 b = mergedPosition(to_);
		ImVec2 pointA;
		ImVec2 pointB;
		if(project(rCamera_, glm::vec3(toWorld * glm::vec4(a, 1.0f)), pointA) && project(rCamera_, glm::vec3(toWorld * glm::vec4(b, 1.0f)), pointB)) pDraw->AddLine(pointA, pointB, this->edgeAt(a, b) ? MergeColor : PendingColor, 2.0f);
	};
	for(const Extrusion& rExtrusion : this->pendingFaces)
	{
		ImVec2 from;
		ImVec2 to;
		if(screen(rExtrusion.a, from) && screen(rExtrusion.b, to)) pDraw->AddLine(from, to, DrawColor, 3.0f);
		auto fill = [&](uint32_t p_, uint32_t q_, uint32_t r_)
		{
			ImVec2 pa;
			ImVec2 pb;
			ImVec2 pc;
			auto at = [&](uint32_t vertex_, ImVec2& rPoint_) { return project(rCamera_, glm::vec3(toWorld * glm::vec4(mergedPosition(vertex_), 1.0f)), rPoint_); };
			if(at(p_, pa) && at(q_, pb) && at(r_, pc)) pDraw->AddTriangleFilled(pa, pb, pc, PendingFill);
		};
		if(rExtrusion.quad)
		{
			fill(rExtrusion.a, rExtrusion.b, rExtrusion.d);
			fill(rExtrusion.a, rExtrusion.d, rExtrusion.c);
		}
		else fill(rExtrusion.a, rExtrusion.b, rExtrusion.c);
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
			glm::vec3 position = rCorner.planned ? rCorner.position : pEdit->position(rCorner.vertex);
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
				if(!rLast.planned && points.size() == this->drawCorners.size() && this->edgeAt(pEdit->position(rLast.vertex), pEdit->position(hover))) pDraw->AddLine(points.back(), point, MergeColor, 3.0f);
			}
		}
	}

	/* The vertices the editor added are always marked (they have no face until connected); the picked ones are larger. */
	for(size_t vertex = pEdit->originalVertexCount(); vertex < pEdit->vertexCount(); vertex++)
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

bool TriangleEditor::center(glm::vec3& rWorld_)
{
	TriangleSurface* pEdit = this->surface();
	glm::mat4 toWorld;
	if(pEdit == nullptr || pEdit->vertexCount() == 0 || !this->placement(toWorld)) return false;
	glm::vec3 low = pEdit->position(0);
	glm::vec3 high = low;
	for(uint32_t vertex = 0; vertex < pEdit->vertexCount(); vertex++)
	{
		low = glm::min(low, pEdit->position(vertex));
		high = glm::max(high, pEdit->position(vertex));
	}
	rWorld_ = glm::vec3(toWorld * glm::vec4((low + high) * 0.5f, 1.0f));
	return true;
}

void TriangleEditor::drawWireframe(const Camera& rCamera_, const glm::mat4& rToWorld_)
{
	TriangleSurface* pEdit = this->surface();
	if(pEdit == nullptr || !this->showWireframe || pEdit->triangleCount() > MaxWireframeFaces) return;
	ImDrawList* pDraw = ImGui::GetBackgroundDrawList();
	auto screen = [&](uint32_t vertex_, ImVec2& rOut_) { return project(rCamera_, glm::vec3(rToWorld_ * glm::vec4(pEdit->position(vertex_), 1.0f)), rOut_); };
	for(uint32_t faceNumber = 0; faceNumber < pEdit->triangleCount(); faceNumber++)
	{
		const std::array<uint32_t, 3>& rFace = pEdit->corners(faceNumber);
		ImVec2 points[3];
		if(screen(rFace[0], points[0]) && screen(rFace[1], points[1]) && screen(rFace[2], points[2])) pDraw->AddTriangle(points[0], points[1], points[2], WireColor, 1.5f);
	}
	/* Only the vertices a face uses: the mesh never loses a vertex, so those of deleted faces would show as strays. */
	std::vector<char> used(pEdit->vertexCount(), 0);
	for(uint32_t faceNumber = 0; faceNumber < pEdit->triangleCount(); faceNumber++)
	{
		for(uint32_t corner : pEdit->corners(faceNumber)) used[corner] = 1;
	}
	for(uint32_t vertex = 0; vertex < pEdit->vertexCount(); vertex++)
	{
		ImVec2 point;
		if(used[vertex] && screen(vertex, point)) pDraw->AddCircleFilled(point, 3.0f, VertexDotColor);
	}
}

void TriangleEditor::drawTab()
{
	this->checkLoad();
	this->drawHeader();
	/* The tabs set tool; Create only takes effect while its tab is the open one. */
	bool inCreate = false;
	bool inFaces = false;
	if(ImGui::BeginTabBar("GeometryTool"))
	{
		if(ImGui::BeginTabItem("Transform", nullptr, this->tabRequest == 0 ? ImGuiTabItemFlags_SetSelected : 0))
		{
			this->tool = Tool::Transform;
			ImGui::TextWrapped("Left click picks (Shift toggles, Ctrl adds), left drag a box picks what is in it (Shift or Ctrl adds). Right click takes one out, right drag a box takes out what is in it. Alt + right drag looks around, the middle button turns the view about the object, Alt flies down. The Move gizmo moves the selection; a corner let go near another vertex or edge merges into it (green while moving). Hold Ctrl when letting go to keep them apart. Q deselects, Delete deletes, P switches to Create.");
			ImGui::EndTabItem();
		}
		if(ImGui::BeginTabItem("Create", nullptr, this->tabRequest == 1 ? ImGuiTabItemFlags_SetSelected : 0))
		{
			this->tool = Tool::Create;
			inCreate = true;
			ImGui::TextWrapped("Vertex mode: left click a vertex to start a triangle, then click empty space or other vertices for the corners (Shift on the third goes on from it; right click steps back; Esc stops). E extrudes the picked edge (Edge mode, or two picked vertices) as a triangle or a quad, \\ switches Triangle / Quad, Enter makes the faces; new vertices near another vertex merge into it (green), hold Ctrl to keep them apart. Keys 1-6 align the view (orthographic). P switches back to Transform.");
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
		this->tabRequest = -1;
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
	if(this->hasWireframeOption())
	{
		ImGui::Checkbox("Show wireframe", &this->showWireframe);
		ImGui::SameLine();
	}
	ImGui::Checkbox("X-ray picking", &xray);
	if(ImGui::Button(this->isolate ? "Show all (I)" : "Isolate object (I)")) this->toggleIsolate();
	/* A pixel radius, so it follows the zoom by itself: zoomed out a radius covers more of the world, close up less. */
	ImGui::SliderFloat("Merge radius (px)", &mergePixels, MinMergePixels, MaxMergePixels, "%.0f");
	if(ImGui::IsItemHovered()) ImGui::SetTooltip("Within this many screen pixels a vertex is clicked on and a moved or made vertex merges into another vertex or edge. It is in pixels, so it grows in the world as you zoom out.");

	TriangleSurface* pEdit = this->surface();
	if(pEdit == nullptr)
	{
		ImGui::TextDisabled("%s", this->noSurfaceText());
	}
	else
	{
		this->drawSurfaceInfo();
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
			if(kind != static_cast<int>(this->extrudeKind)) this->cycleExtrudeKind();
			if(!this->pendingFaces.empty())
			{
				if(ImGui::Button("Make faces (Enter)")) this->commitExtrusion();
			}
		}
		this->drawExtraButtons(inCreate, inFaces);
		if(ImGui::Button("Undo (Shift+Z)")) this->undo();
		ImGui::SameLine();
		if(ImGui::Button("Redo (Shift+Y)")) this->redo();
		ImGui::SameLine();
		if(ImGui::Button("Revert to imported"))
		{
			pEdit->revert();
			this->pendingFaces.clear();
			this->drawCorners.clear();
			this->commit(false);
			this->clearSelection();
			this->hasHit = false;
		}
	}
	ImGui::Separator();
	this->drawFooter();
}
}
#endif
