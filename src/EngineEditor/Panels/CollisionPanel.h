#pragma once

#ifdef EDITOR
#include "TriangleEditor.h"
#include "CollisionEdit.h"
#include "PoolId.h"
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
The picking, box selection, moving, deleting, drawing and extruding triangles, merging, undo and keys are TriangleEditor's (the Geometry subtab shares them; new triangles and vertices made by drawing, extruding and Connect take the Value field's value). The Move gizmo (Gizmo::updateLevels used on one point: the centre of the selected vertices) moves the selection; Delete removes the selected triangles (Vertex mode: the triangles that use a selected vertex; Edge: the ones with both ends of a picked edge); Add vertex puts a vertex at the last click on the collision; Connect makes a triangle of the last three vertices picked (turned to face the camera) with the value of the Value field; Set value gives the Value to the selected triangles. Undo undoes one step of the open collision.
After a change the level's live Collision is made again from the edit (CollisionEdit::build) and the quad tree rebuilt; during a drag of the gizmo that waits for the release, and the collision view's lines are not changed until then (the overlay draws the triangles of the selected vertices).
Save writes the changed levels' diffs into HubWorld::collisionEdits and HubWorld.collision.bin (HubWorld::writeCollisionEdits); Ctrl+S (Editor::saveAllLevels) does it too. The edits are kept while the levels stay loaded (HubWorld::loadCount): a load makes them again from the file, so unsaved edits are gone then. Drawing is ImGui's background draw list (no depth test), like the GeometryPanel.
*/
struct CollisionPanel : TriangleEditor
{
	/* Whether a level's collision has an edit that Save hasn't written. */
	bool unsaved() const;
	/* Writes the changed levels' diffs to HubWorld.collision.bin. Throws std::runtime_error if it can't. */
	void save();
	/* While isolating: the live collision of the open level, the only one the collision view draws (UninitializedPoolId when not isolating or nothing is open). */
	PoolId isolatedCollision();

protected:
	TriangleSurface* surface() override;
	bool placement(glm::mat4& rToWorld_) override;
	void commit(bool dragging_) override;
	void moveEnded() override;
	void checkLoad() override;
	bool openUnderCursor(const glm::vec3& rOrigin_, const glm::vec3& rDirection_) override;
	const BasicMesh* isolationTarget() override;
	void drawWireframe(const Camera& rCamera_, const glm::mat4& rToWorld_) override;
	void drawHeader() override;
	void drawSurfaceInfo() override;
	void drawExtraButtons(bool inCreate_, bool inFaces_) override;
	void drawFooter() override;
	const char* noSurfaceText() const override { return "No level collision open."; }
	bool hasWireframeOption() const override { return false; }

private:
	/* Opens the level's collision for editing (throws std::runtime_error when it can't). */
	void open(Level& rLevel_);
	CollisionEdit* current();
	/* The open level, null if it is gone. */
	Level* openLevel();
	/* From the collision's (level) space to the world. */
	glm::mat4 levelToWorld(const Level& rLevel_) const;
	void setValueOfSelection();
	/* Makes the open level's live collision from the edit (see above). */
	void apply();

	std::map<std::string, CollisionEdit> sessions;
	bool hasOpen = false;
	std::string openName;
	uint32_t seenLoadCount = 0;
	CollisionSurface collisionSurface;

	/* The Value field: given to a triangle by Connect, drawing and extruding, and by Set value. */
	uint32_t value = 0;
	/* A gizmo drag changed the edit: the live collision is made again when the handle is let go. */
	bool applyPending = false;
};
}
#endif
