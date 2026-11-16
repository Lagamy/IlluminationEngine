#pragma once

#ifdef EDITOR
#include "TriangleSurface.h"
#include "Camera.h"
#include "EditorCamera.h"
#include "Gizmo.h"
#include "imgui.h"
#include <GLFW/glfw3.h>
#include <array>
#include <string>
#include <utility>
#include <vector>
#include <glm/glm.hpp>

namespace Graphics
{
struct BasicMesh;

/*
The editing the HubWorld tab's Geometry and Collision subtabs share (GeometryPanel and CollisionPanel derive from it): picking vertices, edges and faces of one open triangle surface (TriangleSurface: a level mesh, an entity's mesh, a level's collision), box selection, the Move gizmo on the selection, deleting, drawing triangles and extruding edges, merging, undo / redo, the keys and the overlay. What is specific is a panel's: which surface is open and where it is in the world (surface, placement), what to do after a change (commit: upload the mesh, make the live collision again), what lies under a click (openUnderCursor), the tab's header, extra buttons, footer and save.
The user's description of the controls is in docs/ARCHITECTURE.md ("Geometry subtab" and the paragraphs that follow it; the Collision subtab has them all).
Transform / Create / Faces tabs: left click picks (Shift toggles, Ctrl adds), left drag a box picks (Shift or Ctrl adds), right click takes one out, right drag a box takes out what is in it; Alt + right looks around, the middle button turns the camera about the open surface (Editor::processInputs), keys 1-6 align the view (orthographic). The Move gizmo moves the selection (a corner let go near a vertex or an edge merges into it, green; hold Ctrl to keep apart). Create: left click on a vertex starts a triangle, clicks add corners; E extrudes a picked edge (Triangle or Quad), Enter makes the faces; right click steps back / cancels, Esc discards, Q deselects, Delete deletes, I isolates, Shift+Z / Shift+Y undo / redo, Shift+S saves (Editor).
Drawing is ImGui's background draw list (no depth test), like the Gizmo.
*/
struct TriangleEditor
{
	enum Mode
	{
		Vertex,
		Edge,
		Face
	};
	Mode mode = Vertex;
	/* The tabs: Transform picks (left click / left drag box) and moves, Create draws triangles, extrudes and connects, Faces picks faces (Face mode) and has the face operations (delete, flip, select all, invert). */
	enum class Tool
	{
		Transform,
		Create,
		Faces
	};
	Tool tool = Tool::Transform;
	/* P: asks drawTab to open the Create tab (from any other) or the Transform tab (from Create); -1 none, 0 Transform, 1 Create. */
	int tabRequest = -1;
	/* What E makes of a picked edge (Create tab): Triangle = one new vertex joined to both ends (1 face), Quad = two new vertices, one beside each end (2 faces). */
	enum class ExtrudeKind
	{
		Triangle,
		Quad
	};
	ExtrudeKind extrudeKind = ExtrudeKind::Triangle;
	bool showWireframe = true;
	/* Vertex picking and box selection see through the surfaces in front of a vertex (off: a vertex behind a face of the open surface can't be picked). Shared by the panels, saved in editor_settings.json (Editor::saveSettings). */
	static inline bool xray = false;
	/* How close, in window pixels, a vertex must be to another vertex or an edge to merge into it, and to a click to be the one clicked (drawing, the hover ring). Pixels, so the world distance follows the zoom (the camera's distance, or the orthographic view's height). Shared, saved like xray. */
	static inline float mergePixels = 12.0f;
	/* Settings > Mesh editing: the new vertices of a pending extrusion follow the cursor (moving by what the cursor moves, in the plane facing the camera) until a left click places them; Enter and right click / Esc work as always. Saved like xray. */
	static inline bool followCursor = false;
	/* Only the open object is drawn (the Isolate object button, key I). */
	bool isolate = false;
	/* The Texturing subtab is the one shown (Editor::processInputs sets it with mode = Face; GeometryPanel only): picking works as here, without the Move gizmo, and the Geometry-only keys stay off. */
	bool texturing = false;

	virtual ~TriangleEditor() = default;

	/* The subtab's contents (the header, the shared controls, the panel's extra buttons, the footer). */
	void drawTab();
	/* Picks and moves (Editor::processInputs, while the subtab is shown). */
	void update(GLFWwindow* pWindow_, EditorCamera& rCamera_, Gizmo& rGizmo_, bool uiWantsMouse_, bool cameraDragging_);
	/* The open surface, the selection and the gizmo handles (DebugUI::draw). */
	void drawOverlay(const Camera& rCamera_, Gizmo& rGizmo_);

	const std::vector<uint32_t>& selectedFaces() const { return this->pickedFaces; }
	void clearSelection();
	/* The Isolate object button and key I: turns isolate on or off (needs an open surface). isolatedMesh is what Renderer::run lets draw (null when not isolating or nothing is open); releaseIsolation clears it (another subtab is shown). */
	void toggleIsolate();
	const BasicMesh* isolatedMesh() const { return this->isolatePtr; }
	void releaseIsolation() { this->isolatePtr = nullptr; }
	/* The middle of the open surface's bounds in the world (the middle mouse button orbits about it); false with nothing open. */
	bool center(glm::vec3& rWorld_);
	/* The centre of the selected vertices in the world; false if nothing is selected. */
	bool handle(glm::vec3& rWorld_);
	/* Shift + Z (Editor::processInputs) and the Undo button: goes back one step of the open surface (a move, an added vertex or face, a delete); false when there is none. redo goes forward again (Shift + Y). */
	bool undo();
	bool redo();
	/* Counts the undos and redos, so that the Texturing window drops a UV drag that was undone. */
	uint32_t undone = 0;

protected:
	/* What the panel gives the shared logic. */
	/* The open surface, null if none is open. */
	virtual TriangleSurface* surface() = 0;
	/* Where the open surface is in the world; false if it is gone. */
	virtual bool placement(glm::mat4& rToWorld_) = 0;
	/* After a change: the panel shows it (a mesh is uploaded, the live collision made again). dragging_: a Move gizmo drag is running (the panel may wait for moveEnded). */
	virtual void commit(bool dragging_) = 0;
	/* The Move gizmo was let go. */
	virtual void moveEnded() {}
	/* Drops everything when the levels were loaded again since (every update and drawTab). */
	virtual void checkLoad() = 0;
	/* Opens what the ray (in the world) hits first, or, while isolating, keeps the open one; false (message set) when nothing is hit. */
	virtual bool openUnderCursor(const glm::vec3& rOrigin_, const glm::vec3& rDirection_) = 0;
	/* The faces a pick of face stands for (the face; Texturing's Edit mode: it with the joined faces of its texture). */
	virtual std::vector<uint32_t> faceGroup(uint32_t face_) { return {face_}; }
	/* What Renderer::run lets draw while isolating (BasicMesh::draw skips every other mesh); null = nothing to isolate. */
	virtual const BasicMesh* isolationTarget() = 0;
	/* The wireframe of the open surface (the selection's own drawing is the shared part). */
	virtual void drawWireframe(const Camera& rCamera_, const glm::mat4& rToWorld_);
	/* The tab: at the top, what the open surface is, extra buttons (after the tab's own), at the bottom (Save and the messages), and the line when nothing is open. */
	virtual void drawHeader() {}
	virtual void drawSurfaceInfo() = 0;
	virtual void drawExtraButtons(bool inCreate_, bool inFaces_) {}
	virtual void drawFooter() = 0;
	virtual const char* noSurfaceText() const { return "Nothing open."; }
	virtual bool hasWireframeOption() const { return true; }

	/* What a pick does with the selection: Replace clears it first, Toggle flips what was picked, Add only adds, Remove only takes away. */
	enum class PickAction
	{
		Replace,
		Toggle,
		Add,
		Remove
	};

	/* The vertices of the selection (welded). */
	std::vector<uint32_t> selectedVertices();
	/* The triangles the mode's selection stands for. */
	std::vector<uint32_t> selectedTriangles();
	void pick(const Camera& rCamera_, GLFWwindow* pWindow_, double x_, double y_, PickAction action_);
	/* The box selection: picks everything of the open surface by the mode that lies inside the window rectangle (Vertex: the corners, Edge: the edges with both ends, Face: the faces with all three corners), adding to the selection if additive_, else replacing it; remove_ takes what is inside out of it. */
	void boxSelect(const Camera& rCamera_, const ImVec2& rFrom_, const ImVec2& rTo_, bool additive_, bool remove_ = false);
	void deleteSelection();
	/* The Faces tab: reverses the winding of the picked faces (their normals turn round); picks every face, or the ones not picked. */
	void flipFaces();
	void selectFaces(bool invert_);
	/* Esc: throws away what is half done: the triangle being drawn and the pending extrusions (the surface goes back to before the extrusion's new vertices, the moves of them included). */
	void discard();
	/* Right click while extruding: a pending extrusion is discarded and deselected; a started triangle steps back (two corners: both existing -> only the first, one of them new -> only the new one, which goes on from there; one corner: all of it is dropped and deselected). False when nothing is being extruded (the click then takes a pick out). */
	bool cancelExtrusion();
	void connect(const glm::vec3& rCameraFront_);
	/* E: from each picked edge (Edge mode, or two picked vertices) the new vertices of extrudeKind, waiting for Enter (pendingFaces); with only vertices picked, a new vertex beside each. The new vertices become the selection. */
	void extrude();
	/* Merging: a vertex that is moved or made (the movers) within mergePixels of another vertex or of an edge (not one of the movers, not a side an extrusion starts from) is merged into it: it goes to its position (vertices at one position are one corner); onto an edge the faces of the edge are split there. findMerges lists these (empty while mergeOff); applyMerges does them; edgeAt tells whether the surface has an edge between two positions (the sides that merge show green). */
	struct Merge
	{
		static constexpr uint32_t None = 0xFFFFFFFFu;
		uint32_t mover;
		/* The vertex (or other mover) merged into, None for a point on an edge. */
		uint32_t target;
		/* Where the mover goes. */
		glm::vec3 position;
		/* For a point on an edge: the edge's ends (None otherwise); applyMerges splits the triangles of the edge at the mover. */
		uint32_t edgeA;
		uint32_t edgeB;
	};
	/* The merges of the movers; rGroups_ (parallel to the movers, empty = one group) tells which extrusion a mover belongs to: movers of different groups can merge into each other, the same group not. */
	std::vector<Merge> findMerges(const std::vector<uint32_t>& rMovers_, const std::vector<int>& rGroups_ = {});
	void applyMerges(const std::vector<Merge>& rMerges_, const std::vector<uint32_t>& rMovers_);
	bool edgeAt(const glm::vec3& rA_, const glm::vec3& rB_);
	/* \: switches Triangle / Quad. With extrusions pending, they are made again from the same sides in the new kind (the moves of their new vertices are lost). */
	void cycleExtrudeKind();

	/* The vertices the pending extrusions made (rGroups_: which extrusion each belongs to). */
	std::vector<uint32_t> pendingMade(std::vector<int>* pGroups_ = nullptr) const;
	/* After a move of the selection (the gizmo let go): merges what is near another vertex, as part of the move's undo step. */
	void mergeSelection();

	/* Vertex mode, a left click: starts or goes on with drawing a triangle (drawCorners). Returns false when the click is not one (no vertex under it and nothing started), so that it picks as before. Shift on the third corner starts the next triangle from it. */
	bool drawClick(const Camera& rCamera_, GLFWwindow* pWindow_, double x_, double y_, bool shift_);
	/* Whether a vertex is not hidden behind a triangle of the open surface (always true with xray). */
	bool vertexVisible(const Camera& rCamera_, TriangleSurface& rEdit_, const glm::mat4& rToWorld_, uint32_t vertex_) const;
	/* The vertex nearest to a window position within mergePixels that is not hidden. */
	bool nearestVertex(const Camera& rCamera_, double x_, double y_, uint32_t& rVertex_);
	/* Adds the planned vertices, the triangle of the three drawCorners, and ends the drawing (or, keepGoing_, starts the next triangle from the last corner). */
	void finishTriangle(bool keepGoing_);
	/* Enter: makes the faces of the pending extrusions; with none, the triangle of three picked vertices (connect). */
	void commitExtrusion();
	void addVertexAtHit();

	/* The selection: vertices in the order they were picked (an edge adds both ends), edges (for drawing), faces. */
	std::vector<uint32_t> pickedVertices;
	std::vector<std::pair<uint32_t, uint32_t>> pickedEdges;
	std::vector<uint32_t> pickedFaces;

	/* An extrusion waiting for Enter: the vertices of the side it starts from (a, b) and the new ones (c beside a; d beside b, only for a quad; a triangle has c alone). Kept while the surface stays open. */
	struct Extrusion
	{
		uint32_t a;
		uint32_t b;
		uint32_t c;
		uint32_t d;
		bool quad;
	};
	std::vector<Extrusion> pendingFaces;

	/* The pending extrusion follows the cursor (followCursor): the new vertices' positions when it started (followStart), where the cursor's ray met the plane then (followCursor0, world, once followReady), and whether it is still following (a left click ends it). beginFollow takes the positions as they are now. */
	bool following = false;
	bool followReady = false;
	std::vector<std::pair<uint32_t, glm::vec3>> followStart;
	glm::vec3 followCursor0 = glm::vec3(0.0f);
	void beginFollow();
	/* undoCount before the first of the pending extrusions, what discard goes back to. */
	size_t pendingBase = 0;
	/* True while Ctrl is held (set each update): the preview shows no merges and nothing merges (a corner click on a vertex while drawing places a new vertex, Enter on an extrusion and the end of a move leave the vertices apart). */
	bool mergeOff = false;
	/* The camera as of the last update / overlay (merging works in the window's pixels, at the view depth). */
	glm::mat4 viewProjection = glm::mat4(1.0f);
	glm::mat4 viewMatrix = glm::mat4(1.0f);
	bool orthographicView = false;
	float orthographicHeight = 20.0f;

	/* A corner of the triangle being drawn: an existing vertex, or one that is only planned (position in the surface's space) and becomes a vertex when the triangle is made, so that Esc leaves no trace. */
	struct DrawCorner
	{
		bool planned = false;
		uint32_t vertex = 0;
		glm::vec3 position = glm::vec3(0.0f);
	};
	/* The corners clicked so far, at most 3, drawn blue. Empty = not drawing. */
	std::vector<DrawCorner> drawCorners;
	/* The existing vertex the drawing started on: the planned corners copy its attributes (the chain can go on from a planned corner after a right click). */
	uint32_t drawSource = 0;
	/* A left drag in an aligned view pans the camera instead of picking. */
	bool leftPanning = false;
	/* A left drag in the Transform and Faces tabs is a box selection, made when it is let go (from clickX / clickY). */
	bool leftBoxing = false;
	double lastX = 0.0;
	double lastY = 0.0;

	/* The last click on the open surface: the triangle and the point on it (surface space). */
	bool hasHit = false;
	uint32_t hitFace = 0;
	glm::vec3 hitPoint = glm::vec3(0.0f);

	/* What stays drawn while isolating (set each update). */
	const BasicMesh* isolatePtr = nullptr;

	bool leftWasDown = false;
	bool clickPending = false;
	double clickX = 0.0;
	double clickY = 0.0;
	/* A right press and release in place takes the face under the press out of the selection; a right drag (boxing) box-selects (the camera's look hold is Alt + right here, EditorCamera::rightSelects). */
	bool rightWasDown = false;
	bool rightPending = false;
	bool boxing = false;
	double rightX = 0.0;
	double rightY = 0.0;
	double rightTime = 0.0;
	bool dragUndoPushed = false;
	glm::vec3 cameraFront = glm::vec3(0.0f, 0.0f, -1.0f);
	std::string message;
};
}
#endif
