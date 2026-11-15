#pragma once

#ifdef EDITOR
#include "EditMesh.h"
#include "Camera.h"
#include "EditorCamera.h"
#include "Gizmo.h"
#include "EntityGeometry.h"
#include "ExternalModel.h"
#include "imgui.h"
#include "PoolId.h"
#include <GLFW/glfw3.h>
#include <array>
#include <map>
#include <string>
#include <utility>
#include <vector>
#include <glm/glm.hpp>

namespace Graphics
{
/*
The HubWorld tab's Geometry subtab (Editor::geometry; EntityListPanel::draw shows drawTab and sets Editor::hubWorldGeometry): edits the geometry of the loaded levels, the first of the HubWorld geometry features that replace the Blender route (docs/TODO.md).
The list at the top of the subtab (drawSources) names what is edited: the level's geometry (first), then the entities. An entity is edited on a copy (Loading::bakeEntity: its meshes in the rest pose as the submeshes of an External model in `editPools`, drawn in place of the entity's model while it is open; one entity at a time), picked and edited like a level's meshes. Once the copy has an edit the exit menu (drawSources) shows: Save to existing prefab (an External prefab) or Create prefab (this level's folder or the HubWorld's Global one) store the edit as the prefab's geometry (EntityGeometry.h), give the prefab the model, assign the entity to it and close the copy; Discard closes it. The list is blocked while an edit is open. An entity that draws an External model can be opened too. One of a prefab with edited geometry is edited again (the copy is the prefab's source baked again with the prefab's diffs applied, bakePrefabSource), and the diffs of submeshes left alone are kept. Any other External model is read from its file (Loading::bakeModelData): the exit menu then has Save to model file, which writes the edit into that file (every user of the model changes), and Create prefab, which writes it into a new model file named like the prefab. A left click on a level's geometry (the mesh under the cursor, Level::rayHitsInstance) opens that mesh as an EditMesh and picks, by the mode: Vertex the corner of the face under the cursor nearest the click, Edge its nearest edge (both ends), Face the face; Shift adds to the selection or takes from it; a right click (not a hold) takes the picked thing under the cursor out of it; a right drag draws a box and picks what the mode finds inside it (Vertex the corners, Edge the edges with both ends, Face the faces with all corners; Shift adds; in the Texturing subtab it always adds), whatever is in front of it. While this subtab is shown the camera looks around with Alt + right (EditorCamera::rightSelects). In the Texturing subtab a left click only adds. A corner is all the vertices at one position (EditMesh::weld).
The Move gizmo (Gizmo::updateLevels, used on one point: the centre of the selected vertices) moves the selection; Delete removes the selected faces (Vertex / Edge mode: the faces that use the selected vertices); Add vertex puts a vertex at the last click on the surface; Connect makes a triangle of the last three vertices picked
(turned to face the camera). E (Extrude) puts a new vertex beside each picked vertex and selects it; with two vertices or an edge picked it puts one new vertex joined to both, and Enter then makes the face (Enter with three vertices picked and nothing pending does Connect). Undo undoes one step of the open mesh. The collision is not edited here (the Collision subtab, CollisionPanel, does).
Save writes the changed meshes' diffs into HubWorld::meshEdits and HubWorld.bin (HubWorld::writeMeshEdits); Ctrl+S (Editor::saveAllLevels) does it too. The meshes are kept while the levels stay loaded (HubWorld::loadCount): a load makes them again from the file, so unsaved edits are gone then.
Edits to a mesh several pieces share show in all of them, but the gizmo and the overlay use the placement of the first. Drawing is ImGui's background draw list (no depth test), like the Gizmo.
*/
struct GeometryPanel
{
	enum Mode
	{
		Vertex,
		Edge,
		Face
	};
	Mode mode = Vertex;
	/* The tabs of the Geometry subtab: Transform picks (left click / left drag box) and moves, Create draws triangles, extrudes and connects, Faces picks faces (Face mode) and has the face operations (delete, flip, select all, invert). */
	enum class Tool
	{
		Transform,
		Create,
		Faces
	};
	Tool tool = Tool::Transform;
	/* What E makes of a picked edge (Create tab): Triangle = one new vertex joined to both ends (1 face), Quad = two new vertices, one beside each end (2 faces). */
	enum class ExtrudeKind
	{
		Triangle,
		Quad
	};
	ExtrudeKind extrudeKind = ExtrudeKind::Triangle;
	bool showWireframe = true;
	/* Vertex picking and box selection see through the surfaces in front of a vertex (off: a vertex behind a face of the open mesh can't be picked). */
	bool xray = false;
	/* Only the open mesh is drawn (the Isolate object button, key I): BasicMesh::draw skips every other mesh (Graphics::isolatedMesh). Picking then takes only the open mesh too. */
	bool isolate = false;
	/* How close, in window pixels, a vertex must be to another vertex or an edge to merge into it, and to a click to be the one clicked (drawing, the hover ring). Pixels, so the world distance follows the zoom (the camera's distance, or the orthographic view's height). The slider in the tab sets it; saved in editor_settings.json (Editor::saveSettings). */
	float mergePixels = 12.0f;
	/* The Texturing subtab is the one shown (Editor::processInputs sets it with mode = Face): picking works as here, without the Move gizmo. */
	bool texturing = false;
	/* Texturing's Edit mode (Editor::processInputs sets it): a pick takes the face with the faces joined to it that have its texture (EditMesh::island). */
	bool island = false;

	/* The subtab's contents. */
	void drawTab();
	/* The list of geometry that can be edited and, with an entity open, the exit menu: at the top of the Geometry and Texturing subtabs. */
	void drawSources();
	/* Picks and moves (Editor::processInputs, while the subtab is shown). */
	void update(GLFWwindow* pWindow_, EditorCamera& rCamera_, Gizmo& rGizmo_, bool uiWantsMouse_, bool cameraDragging_);
	/* The open mesh, the selection and the gizmo handles (DebugUI::draw). */
	void drawOverlay(const Camera& rCamera_, Gizmo& rGizmo_);

	/* For TexturingPanel: the open mesh (null if none), where it is in the world, and the picked faces. */
	EditMesh* current();
	bool resolve(BasicMesh*& rpMesh_, glm::mat4& rToWorld_);
	const std::vector<uint32_t>& selectedFaces() const { return this->pickedFaces; }
	void clearSelection();
	/* The Isolate object button and key I: turns isolate on or off (needs an open mesh). isolatedMesh is the mesh Renderer::run lets draw (null when not isolating or no mesh is open); releaseIsolation clears it (another subtab is shown). */
	void toggleIsolate();
	const BasicMesh* isolatedMesh() const { return this->isolatePtr; }
	void releaseIsolation() { this->isolatePtr = nullptr; }
	/* The middle of the open mesh's bounds in the world (the middle mouse button orbits about it); false with no mesh open. */
	bool center(glm::vec3& rWorld_);

	/* Whether a mesh has an edit that Save hasn't written. */
	bool unsaved() const;
	/* Writes the changed meshes' diffs to HubWorld.bin. Throws std::runtime_error if it can't. */
	void save();

private:
	using Key = std::pair<std::string, uint32_t>;
	/* What a pick does with the selection: Replace clears it first, Toggle flips what was picked, Add only adds, Remove only takes away. */
	enum class PickAction
	{
		Replace,
		Toggle,
		Add,
		Remove
	};

	/* Drops everything when the levels were loaded again since (the meshes are new). */
	void checkLoad();
	/* Entity geometry: opens the copy of the entity (throws std::runtime_error when it can't), closes it (the entity draws its own model again unless it was given another), the entity if it is still alive, the changed state of the copy's meshes. */
	void openEntity(PoolId entityId_);
	void closeEntity();
	Entity* liveEntity();
	bool entityChanged() const;
	glm::mat4 entityMatrix(const Entity& rEntity_) const;
	/* The exit menu's two actions: stores the copy's edits as the geometry of the External prefab, builds its model, assigns the entity and closes the copy. A level's own entity (made from the game's data) can't change its prefab in the level file, so it is not assigned: it is asked to be replaced by an entity of the prefab (takeReplacement). Throws std::runtime_error. */
	void commitEntity(PoolId prefabId_);
	/* The diffs of the copy's opened submeshes. */
	std::vector<MeshDiff> sessionDiffs() const;
	/* Save to model file: the copy of a model file's edit overwrites the file (Loading::saveFileModel) and the entity draws the new model. Throws std::runtime_error. */
	void commitFileModel();
	void drawExitMenu();
public:
	/* The level's own entity and the prefab that commitEntity asks Editor::replaceWithPrefab to replace it with; false when there is none (taken once). */
	bool takeReplacement(PoolId& rEntityId_, PoolId& rPrefabId_);
private:
	void open(const std::string& rLevel_, uint32_t ordinal_);
	/* The vertices of the selection (welded). */
	std::vector<uint32_t> selectedVertices();
	void pick(const Camera& rCamera_, GLFWwindow* pWindow_, double x_, double y_, PickAction action_);
	/* The box selection: picks everything of the open mesh by the mode that lies inside the window rectangle (Vertex: the corners, Edge: the edges with both ends, Face: the faces with all three corners), adding to the selection if additive_, else replacing it. Nothing is hidden by what is in front. */
	void boxSelect(const Camera& rCamera_, const ImVec2& rFrom_, const ImVec2& rTo_, bool additive_, bool remove_ = false);
	void deleteSelection();
	/* The Faces tab: reverses the winding of the picked faces (their normals turn round); picks every face, or the ones not picked. */
	void flipFaces();
	void selectFaces(bool invert_);
	/* Esc: throws away what is half done: the triangle being drawn and the pending extrusions (the mesh goes back to before the extrusion's new vertices, the moves of them included). */
	void discard();
	/* Right click while extruding: a pending extrusion is discarded and deselected; a started triangle steps back (two corners: both existing -> only the first, one of them new -> only the new one, which goes on from there; one corner: all of it is dropped and deselected). False when nothing is being extruded (the click then takes a pick out). */
	bool cancelExtrusion();
	void connect(const glm::vec3& rCameraFront_);
	/* E: from each picked edge (Edge mode, or two picked vertices) the new vertices of extrudeKind, waiting for Enter (pendingFaces); with only vertices picked, a new vertex beside each. The new vertices become the selection. */
	void extrude();
	/* Merging: a vertex that is moved or made (the movers) within a few pixels of another vertex (not one of the movers, not a side an extrusion starts from) is merged into it: it goes to its position (vertices at one position are one corner). findMerges lists these (empty while mergeOff); applyMerges moves them; edgeAt tells whether the mesh has an edge between two positions (the sides that merge show green). */
	struct Merge
	{
		static constexpr uint32_t None = 0xFFFFFFFFu;
		uint32_t mover;
		/* The vertex (or other mover) merged into, None for a point on an edge. */
		uint32_t target;
		/* Where the mover goes. */
		glm::vec3 position;
		/* For a point on an edge: the edge's ends (None otherwise); applyMerges splits the faces of the edge at the mover. */
		uint32_t edgeA;
		uint32_t edgeB;
	};
	/* The merges of the movers; rGroups_ (parallel to the movers, empty = one group) tells which extrusion a mover belongs to: movers of different groups can merge into each other, the same group not. Besides vertices, a mover can merge into the middle of an edge. */
	std::vector<Merge> findMerges(const std::vector<uint32_t>& rMovers_, const std::vector<int>& rGroups_ = {});
	void applyMerges(const std::vector<Merge>& rMerges_, const std::vector<uint32_t>& rMovers_);
	bool edgeAt(const glm::vec3& rA_, const glm::vec3& rB_) const;
	/* The vertices the pending extrusions made. */
	std::vector<uint32_t> pendingMade(std::vector<int>* pGroups_ = nullptr) const;
	/* After a move of the selection (the gizmo let go): merges what is near another vertex, as part of the move's undo step. */
	void mergeSelection();

	/* Vertex mode, a left click: starts or goes on with drawing a triangle (drawCorners). Returns false when the click is not one (no vertex under it and nothing started), so that it picks as before. Shift on the third corner starts the next triangle from it. */
	bool drawClick(const Camera& rCamera_, GLFWwindow* pWindow_, double x_, double y_, bool shift_);
	/* The vertex nearest to a window position within a few pixels. */
	bool vertexVisible(const Camera& rCamera_, const EditMesh& rEdit_, const glm::mat4& rToWorld_, uint32_t vertex_) const;
	bool nearestVertex(const Camera& rCamera_, double x_, double y_, uint32_t& rVertex_);
	/* Adds the planned vertices, the face of the three drawCorners, and ends the drawing (or, keepGoing_, starts the next triangle from the last corner). */
	void finishTriangle(bool keepGoing_);
	/* Enter: makes the faces of the pending extrusions; with none, the triangle of three picked vertices (connect). */
	void commitExtrusion();
	void addVertexAtHit();
public:
	/* Ctrl + Z (Editor::processInputs) and the Undo button: goes back one step of the open mesh (a move, an added vertex or face, a delete); false when there is none. */
	bool undo();
	/* Shift + Y (Editor::processInputs) and the Redo button: goes forward over an undo; false when there is none. */
	bool redo();
	/* Counts the undos, so that the Texturing window drops a UV drag that was undone. */
	uint32_t undone = 0;
	/* The centre of the selected vertices in the world; false if nothing is selected. */
	bool handle(glm::vec3& rWorld_);
private:

	/* What is edited: the levels' meshes, or the copy of the open entity (sessions with an empty level name are its meshes, by submesh). */
	enum class Source
	{
		Level,
		Entity
	};
	Source source = Source::Level;
	struct EntityCopy
	{
		PoolId entityId = UninitializedPoolId;
		Loading::BakedEntity baked;
		const ExternalModel* pModel = nullptr;
		/* What the prefab geometry records of the entity (Loading::PrefabGeometry). */
		std::string sourcePrefab;
		std::string discMesh;
		std::string discTexture;
		/* Re-editing an edited prefab's entity: the diffs the prefab has (the copy starts with them applied; the ones of submeshes left alone are kept) and the model the entity drew, which it gets back if the copy is discarded. */
		std::vector<MeshDiff> diffs;
		const ExternalModel* pPrevious = nullptr;
		/* Editing an External model's file (not an edited prefab's geometry): the model's name and scope. Saving overwrites the file, or copies it into a model named like a new prefab. */
		std::string fileModel;
		bool fileModelInProject = false;
	};
	bool hasEntity = false;
	PoolId replaceEntity = UninitializedPoolId;
	PoolId replaceWith = UninitializedPoolId;
	EntityCopy entityCopy;
	ExternalPools editPools = ExternalPools("EditMeshes", "EditTextures", "EditModels");
	char entityFilter[64] = "";
	char newPrefabName[64] = "";
	bool newPrefabInLevel = false;
	/* What the list and the exit menu last did (shown under them, in both subtabs). */
	std::string sourceMessage;

	std::map<Key, EditMesh> sessions;
	bool hasOpen = false;
	const BasicMesh* isolatePtr = nullptr;
	Key openKey;
	uint32_t seenLoadCount = 0;

	/* The selection: vertices in the order they were picked (an edge adds both ends), edges (for drawing), faces. */
	std::vector<uint32_t> pickedVertices;
	std::vector<std::pair<uint32_t, uint32_t>> pickedEdges;
	std::vector<uint32_t> pickedFaces;
	/* An extrusion waiting for Enter: the vertices of the side it starts from (a, b) and the new ones (c beside a; d beside b, only for a quad; a triangle has c alone). Kept while the mesh stays open. */
	struct Extrusion
	{
		uint32_t a;
		uint32_t b;
		uint32_t c;
		uint32_t d;
		bool quad;
	};
	std::vector<Extrusion> pendingFaces;
	/* EditMesh::undoCount before the first of the pending extrusions, what discard goes back to. */
	size_t pendingBase = 0;
	/* True while Ctrl is held (set each update): the preview shows no merges and nothing merges (a corner click on a vertex while drawing places a new vertex, Enter on an extrusion and the end of a move leave the vertices apart). */
	bool mergeOff = false;
	/* The camera as of the last update / overlay (merging works in the window's pixels, at the view depth). */
	glm::mat4 viewProjection = glm::mat4(1.0f);
	glm::mat4 viewMatrix = glm::mat4(1.0f);
	bool orthographicView = false;
	float orthographicHeight = 20.0f;

	/* A corner of the triangle being drawn: an existing vertex, or one that is only planned (position in the mesh's space) and becomes a vertex when the triangle is made, so that Escape leaves no trace. */
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
	/* A left drag in the Transform tab is a box selection, made when it is let go (from clickX / clickY). */
	bool leftBoxing = false;
	double lastX = 0.0;
	double lastY = 0.0;

	/* The last click on the open mesh: the face and the point on it (mesh space). */
	bool hasHit = false;
	uint32_t hitFace = 0;
	glm::vec3 hitPoint = glm::vec3(0.0f);

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
