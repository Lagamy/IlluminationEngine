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
Level* levelNamed(const std::string& rName_)
{
	for(uint32_t i = 0; i < Globals::Game::levels.objects.size(); i++)
	{
		if(Globals::Game::levels.alive[i] && Globals::Game::levels.names[i] == rName_) return &Globals::Game::levels.objects[i];
	}
	return nullptr;
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

TriangleSurface* GeometryPanel::surface()
{
	EditMesh* pEdit = this->current();
	if(pEdit == nullptr) return nullptr;
	this->meshSurface.pEdit = pEdit;
	return &this->meshSurface;
}

bool GeometryPanel::placement(glm::mat4& rToWorld_)
{
	BasicMesh* pMesh = nullptr;
	return this->resolve(pMesh, rToWorld_);
}

void GeometryPanel::commit(bool dragging_)
{
	BasicMesh* pMesh = nullptr;
	glm::mat4 toWorld;
	EditMesh* pEdit = this->current();
	if(pEdit != nullptr && this->resolve(pMesh, toWorld))
	{
		pEdit->upload(*pMesh);
		meshRevision++;
	}
}

const BasicMesh* GeometryPanel::isolationTarget()
{
	BasicMesh* pMesh = nullptr;
	glm::mat4 toWorld;
	return this->resolve(pMesh, toWorld) ? pMesh : nullptr;
}

std::vector<uint32_t> GeometryPanel::faceGroup(uint32_t face_)
{
	EditMesh* pEdit = this->current();
	return this->island && pEdit != nullptr ? pEdit->island(face_) : std::vector<uint32_t>{face_};
}

bool GeometryPanel::openUnderCursor(const glm::vec3& rOrigin_, const glm::vec3& rDirection_)
{
	const glm::vec3 origin = rOrigin_;
	const glm::vec3 direction = rDirection_;
	Key key;
	if(this->isolate && this->hasOpen) key = this->openKey;
	else if(this->source == Source::Entity)
	{
		/* The entity's copy: the submesh under the cursor, the nearest. The submeshes are baked, so they all sit at the entity's transform. */
		Entity* pEntity = this->liveEntity();
		if(pEntity == nullptr)
		{
			this->message = "The entity is gone.";
			return false;
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
			return false;
		}
		key = Key{std::string(), static_cast<uint32_t>(best)};
	}
	else
	{
		/* The open mesh is tried by the ray first, as it is now (edits, which can be outside what the level's pieces were built to cover, such as a face extruded away from a wall): when the ray hits it and nothing of the levels is nearer it stays the open mesh. */
		float openDistance = 0.0f;
		bool openHit = false;
		if(this->hasOpen && this->current() != nullptr)
		{
			BasicMesh* pOpenMesh = nullptr;
			glm::mat4 openToWorld;
			if(this->resolve(pOpenMesh, openToWorld))
			{
				glm::mat4 inverse = glm::inverse(openToWorld);
				uint32_t face = 0;
				TriangleSurface* pOpenSurface = this->surface();
				openHit = pOpenSurface != nullptr && pOpenSurface->raycast(glm::vec3(inverse * glm::vec4(origin, 1.0f)), glm::vec3(inverse * glm::vec4(direction, 0.0f)), openDistance, face);
			}
		}

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
		if(openHit && (pBestLevel == nullptr || openDistance <= bestDistance + 1e-3f)) return true;
		if(pBestLevel == nullptr)
		{
			this->message = "No level geometry under the cursor.";
			return false;
		}
		std::vector<std::pair<BasicMesh*, glm::mat4>> meshes;
		pBestLevel->editableMeshes(meshes);
		auto found = std::find_if(meshes.begin(), meshes.end(), [&](const auto& rEntry_) { return rEntry_.first == pBestMesh; });
		if(found == meshes.end()) return false;
		key = Key{pBestLevel->name, static_cast<uint32_t>(found - meshes.begin())};
	}
	try
	{
		if(!this->hasOpen || key != this->openKey) this->open(key.first, key.second);
	}
	catch(const std::exception& rError)
	{
		this->message = std::format("Can't edit this mesh: {}", rError.what());
		return false;
	}
	return true;
}

void GeometryPanel::drawSurfaceInfo()
{
	EditMesh* pEdit = this->current();
	if(pEdit == nullptr) return;
	ImGui::Text("%s, mesh %u: %zu vertices, %zu faces%s", pEdit->level.empty() ? "entity" : pEdit->level.c_str(), pEdit->ordinal, pEdit->vertices.size(), pEdit->faces.size(), pEdit->changed ? " (unsaved)" : "");
}

void GeometryPanel::drawFooter()
{
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
