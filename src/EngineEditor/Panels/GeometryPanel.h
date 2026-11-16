#pragma once

#ifdef EDITOR
#include "TriangleEditor.h"
#include "EditMesh.h"
#include "EntityGeometry.h"
#include "ExternalModel.h"
#include "PoolId.h"
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
The picking, box selection, moving, deleting, drawing and extruding, merging, undo and keys are TriangleEditor's (the Collision subtab shares them). The Move gizmo (Gizmo::updateLevels, used on one point: the centre of the selected vertices) moves the selection; Delete removes the selected faces (Vertex / Edge mode: the faces that use the selected vertices); Add vertex puts a vertex at the last click on the surface; Connect makes a triangle of the last three vertices picked
(turned to face the camera). E (Extrude) puts a new vertex beside each picked vertex and selects it; with two vertices or an edge picked it puts one new vertex joined to both, and Enter then makes the face (Enter with three vertices picked and nothing pending does Connect). Undo undoes one step of the open mesh. The collision is not edited here (the Collision subtab, CollisionPanel, does).
Save writes the changed meshes' diffs into HubWorld::meshEdits and HubWorld.bin (HubWorld::writeMeshEdits); Ctrl+S (Editor::saveAllLevels) does it too. The meshes are kept while the levels stay loaded (HubWorld::loadCount): a load makes them again from the file, so unsaved edits are gone then.
Edits to a mesh several pieces share show in all of them, but the gizmo and the overlay use the placement of the first. Drawing is ImGui's background draw list (no depth test), like the Gizmo.
*/
struct GeometryPanel : TriangleEditor
{
	/* Texturing's Edit mode (Editor::processInputs sets it): a pick takes the face with the faces joined to it that have its texture (EditMesh::island). */
	bool island = false;

	/* The list of geometry that can be edited and, with an entity open, the exit menu: at the top of the Geometry and Texturing subtabs. */
	void drawSources();

	/* For TexturingPanel: the open mesh (null if none) and where it is in the world. */
	EditMesh* current();
	bool resolve(BasicMesh*& rpMesh_, glm::mat4& rToWorld_);

	/* Whether a mesh has an edit that Save hasn't written. */
	bool unsaved() const;
	/* Writes the changed meshes' diffs to HubWorld.bin. Throws std::runtime_error if it can't. */
	void save();
	/* The level's own entity and the prefab that commitEntity asks Editor::replaceWithPrefab to replace it with; false when there is none (taken once). */
	bool takeReplacement(PoolId& rEntityId_, PoolId& rPrefabId_);

protected:
	TriangleSurface* surface() override;
	bool placement(glm::mat4& rToWorld_) override;
	void commit(bool dragging_) override;
	void checkLoad() override;
	bool openUnderCursor(const glm::vec3& rOrigin_, const glm::vec3& rDirection_) override;
	std::vector<uint32_t> faceGroup(uint32_t face_) override;
	const BasicMesh* isolationTarget() override;
	void drawHeader() override { this->drawSources(); }
	void drawSurfaceInfo() override;
	void drawFooter() override;
	const char* noSurfaceText() const override { return "No mesh open."; }

private:
	using Key = std::pair<std::string, uint32_t>;

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
	void open(const std::string& rLevel_, uint32_t ordinal_);

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
	Key openKey;
	uint32_t seenLoadCount = 0;
	MeshSurface meshSurface;
};
}
#endif
