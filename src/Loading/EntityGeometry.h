#pragma once

#ifdef EDITOR
#include "ExternalAssets.h"
#include "Entity.h"
#include "MeshDiff.h"
#include <string>
#include <vector>

/*
The geometry of an entity edited in the editor (Geometry / Texturing subtabs, GeometryPanel), and how an External prefab keeps it. EDITOR only: the game does not read or write these.
An entity's meshes are shared disc data, so the editor works on a copy: bakeEntity takes the entity's meshes in their rest pose (Entity::instanceMeshes with rest_, the engine's own turn applied) into the vertices of an External model, one submesh per mesh, in the order instanceMeshes lists them; that order is the ordinal a MeshDiff names a submesh by (as Level::editableMeshes does for a level). The model is static: an animated entity keeps its rest pose, and its materials' flags (clamp, flip, double sided, additive) are not carried.
The copy has no texture of its own to fall back on: each submesh's disc texture is its EditMesh::ownTexture ("disc:<set>#<name>", Entity::instanceMeshTexture), drawn as a face group.
An External prefab keeps its edit as a PrefabGeometry: where the baked model comes from (the source prefab by its pool name, the disc mesh and texture the entity had, so a rebuild bakes the same) and a MeshDiff per edited submesh (MeshDiff.h; the edit format keeps the source's vertex numbers, not a baked model). The prefab's entry in prefabs.json has "geometry": {"source", "mesh", "texture"} and the diffs are in geometry.bin beside it (HubWorld.bin's format: one LevelMeshDiffs per prefab, its `level` the prefab's name).
The model of such a prefab is registered in its scope's pools as "<pool name> geometry" and is the prefab's mesh (Prefab::definition); rebuildEditedModels makes it again from the source whenever the scope's prefabs are read.
*/
namespace Loading
{
struct BakedEntity
{
	External::ModelData data;
	std::vector<MeshOriginal> originals;
	std::vector<std::string> ownTextures;
};

/* The entity's meshes baked (see above). Throws std::runtime_error if the entity has no instance or a mesh has a custom vertex layout. */
BakedEntity bakeEntity(const Entity& rEntity_);

struct PrefabGeometry
{
	std::string source;
	std::string discMesh;
	std::string discTexture;
	std::vector<MeshDiff> meshes;
};

/* The pool name of an External prefab ("Global/<name>", "Level/<level>/<name>") and the name of its geometry model. */
std::string externalPrefabPoolName(ExternalScope scope_, const std::string& levelName_, const std::string& name_);
std::string editedModelName(const std::string& poolName_);

/*
Registers the baked model in rPools_ as name_ with the diffs applied (a diff that doesn't fit its submesh is printed to stderr and left out) and returns it. Needs the GL context. A model of that name there is replaced, so the pointer of the old one dies: replaceEditedModel below keeps the entities that hold it.
*/
const Graphics::ExternalModel& buildEditedModel(Graphics::ExternalPools& rPools_, bool inProject_, const std::string& name_, const BakedEntity& rBaked_, const std::vector<MeshDiff>& rDiffs_);

/*
A model read from a file (ExternalAssets.h) baked the same way, for editing it: one submesh per submesh of the file, the submesh's texture name as the face texture of its faces. Throws std::runtime_error for a custom vertex layout.
modelDataWithDiffs makes the file's model again from it with the diffs applied (the vertices a deleted face leaves unused are dropped, normals of moved vertices made again, the faces of a texture of their own become a submesh each, the bounds worked out): the model's own edit lives in the file, not in a diff. Throws std::runtime_error if a face has a disc texture (a file can only name External ones) or nothing is left.
saveFileModel writes it into the scope's Models folder (importExternalModel, a model of that name there is replaced) and gives the entities that held the model it replaces the new one. Needs the GL context.
*/
BakedEntity bakeModelData(const External::ModelData& rModel_);
External::ModelData modelDataWithDiffs(const BakedEntity& rBaked_, const std::vector<MeshDiff>& rDiffs_, const std::string& name_);
const Graphics::ExternalModel& saveFileModel(ExternalScope scope_, External::ModelData data_);

/* The records of the prefabs (by pool name): set by the editor's exit menu, read from prefabs.json by loadExternalPrefabs. */
void setPrefabGeometry(const std::string& poolName_, PrefabGeometry geometry_);
const PrefabGeometry* prefabGeometry(const std::string& poolName_);
/* Forgets the records of a level's Level folder (the level is gone). */
void forgetLevelGeometry(const std::string& levelName_);

/* The source entity of a record baked as rebuildEditedModels does (without the diffs); the editor re-edits a prefab's geometry from it. Throws std::runtime_error if the source can't be made. Needs the GL context. */
BakedEntity bakePrefabSource(const PrefabGeometry& rGeometry_);

/*
Makes the model of every prefab record of the scope again: it spawns the source prefab in a pool of its own, gives it the recorded disc mesh and texture, bakes it and builds the model in the scope's pools. A prefab whose source can't be made (a Shadow archive that isn't loaded, say) is printed to stderr and has no model until the next read.
levelName_ is the level of the Level scope.
*/
void rebuildEditedModels(ExternalScope scope_, const std::string& levelName_);

/* Builds the model of one record from a baked entity and gives it to the entities that held the model it replaces. */
const Graphics::ExternalModel& replaceEditedModel(ExternalScope scope_, const std::string& poolName_, const BakedEntity& rBaked_, const std::vector<MeshDiff>& rDiffs_);
}
#endif
