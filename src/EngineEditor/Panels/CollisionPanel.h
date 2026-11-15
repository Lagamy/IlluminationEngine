#pragma once

#ifdef EDITOR
#include "CollisionEdit.h"
#include "Camera.h"
#include "Gizmo.h"
#include "imgui.h"
#include <GLFW/glfw3.h>
#include <map>
#include <string>
#include <vector>
#include <glm/glm.hpp>

struct Level;

namespace Graphics
{
/*
The HubWorld tab's Collision subtab (Editor::collisionPanel; EntityListPanel::draw shows drawTab and sets Editor::hubWorldCollision): edits the collision of the loaded levels (not the entities' shapes), the way the Geometry subtab edits their meshes. While the subtab is shown every level is drawn like in the HubWorld view and the collision view draws the levels' collision lines (CollisionView::draw), coloured by triangle value.
A left click on a level's collision (the nearest triangle of the live collisions under the cursor) opens that level's collision as a CollisionEdit and picks by the mode: Vertex the corner of the triangle under the cursor nearest the click, Edge its nearest edge (both ends), Face the triangle; Shift adds to the selection or takes from it; a right click (not a hold) takes the picked thing under the cursor out of it; a right drag draws a box and picks what the mode finds inside it (Vertex the corners, Edge the edges with both ends, Face the triangles with all corners; Shift adds), whatever is in front of it. While this subtab is shown the camera looks around with Alt + right (EditorCamera::rightSelects). A corner is all the vertices at one position (CollisionEdit::weld).
The Move gizmo (Gizmo::updateLevels used on one point: the centre of the selected vertices) moves the selection; Delete removes the selected triangles (Vertex mode: the triangles that use a selected vertex; Edge: the ones with both ends of a picked edge); Add vertex puts a vertex at the last click on the collision; Connect makes a triangle of the last three vertices picked (turned to face the camera) with the value of the Value field; Set value gives the Value to the selected triangles. Undo undoes one step of the open collision.
After a change the level's live Collision is made again from the edit (CollisionEdit::build) and the quad tree rebuilt; during a drag of the gizmo that waits for the release, and the collision view's lines are not changed until then (the overlay draws the triangles of the selected vertices).
Save writes the changed levels' diffs into HubWorld::collisionEdits and HubWorld.collision.bin (HubWorld::writeCollisionEdits); Ctrl+S (Editor::saveAllLevels) does it too. The edits are kept while the levels stay loaded (HubWorld::loadCount): a load makes them again from the file, so unsaved edits are gone then. Drawing is ImGui's background draw list (no depth test), like the GeometryPanel.
*/
struct CollisionPanel
{
	enum Mode
	{
		Vertex,
		Edge,
		Face
	};
	Mode mode = Vertex;

	/* The subtab's contents. */
	void drawTab();
	/* Picks and moves (Editor::processInputs, while the subtab is shown). */
	void update(GLFWwindow* pWindow_, const Camera& rCamera_, Gizmo& rGizmo_, bool uiWantsMouse_, bool cameraDragging_);
	/* The open collision, the selection and the gizmo handles (DebugUI::draw). */
	void drawOverlay(const Camera& rCamera_, Gizmo& rGizmo_);

	/* Whether a level's collision has an edit that Save hasn't written. */
	bool unsaved() const;
	/* Writes the changed levels' diffs to HubWorld.collision.bin. Throws std::runtime_error if it can't. */
	void save();

private:
	/* What a pick does with the selection: Replace clears it first, Toggle flips what was picked, Remove only takes away. */
	enum class PickAction
	{
		Replace,
		Toggle,
		Remove
	};

	/* Drops everything when the levels were loaded again since (the collisions are new). */
	void checkLoad();
	/* Opens the level's collision for editing (throws std::runtime_error when it can't). */
	void open(Level& rLevel_);
	CollisionEdit* current();
	/* The open level, null if it is gone. */
	Level* openLevel();
	/* From the collision's (level) space to the world. */
	glm::mat4 toWorld(const Level& rLevel_) const;
	void clearSelection();
	/* The vertices of the selection (welded). */
	std::vector<uint32_t> selectedVertices();
	/* The triangles the mode's selection stands for. */
	std::vector<uint32_t> selectedTriangles();
	void pick(const Camera& rCamera_, GLFWwindow* pWindow_, double x_, double y_, PickAction action_);
	/* The box selection: picks everything of the open collision by the mode that lies inside the window rectangle (Vertex: the corners, Edge: the edges with both ends, Face: the triangles with all three corners), adding to the selection if additive_, else replacing it. Nothing is hidden by what is in front. */
	void boxSelect(const Camera& rCamera_, const ImVec2& rFrom_, const ImVec2& rTo_, bool additive_);
	void deleteSelection();
	void connect(const glm::vec3& rCameraFront_);
	void addVertexAtHit();
	void setValueOfSelection();
	/* Makes the open level's live collision from the edit (see above). */
	void apply();
	/* The centre of the selected vertices in the world; false if nothing is selected. */
	bool handle(glm::vec3& rWorld_);

	std::map<std::string, CollisionEdit> sessions;
	bool hasOpen = false;
	std::string openName;
	uint32_t seenLoadCount = 0;

	/* The selection: vertices in the order they were picked (an edge adds both ends), edges (for drawing), triangles. */
	std::vector<uint32_t> pickedVertices;
	std::vector<std::pair<uint32_t, uint32_t>> pickedEdges;
	std::vector<uint32_t> pickedFaces;

	/* The last click on the open collision: the triangle and the point on it (level space). */
	bool hasHit = false;
	uint32_t hitTriangle = 0;
	glm::vec3 hitPoint = glm::vec3(0.0f);

	/* The Value field: given to a triangle by Connect and Set value. */
	uint32_t value = 0;
	/* A gizmo drag changed the edit: the live collision is made again when the handle is let go. */
	bool applyPending = false;
	bool dragUndoPushed = false;

	bool leftWasDown = false;
	bool clickPending = false;
	double clickX = 0.0;
	double clickY = 0.0;
	/* A right press and release in place takes the triangle under the press out of the selection; a right drag (boxing) box-selects (the camera's look hold is Alt + right here, EditorCamera::rightSelects). */
	bool rightWasDown = false;
	bool rightPending = false;
	bool boxing = false;
	double rightX = 0.0;
	double rightY = 0.0;
	double rightTime = 0.0;
	glm::vec3 cameraFront = glm::vec3(0.0f, 0.0f, -1.0f);
	std::string message;
};
}
#endif
