#include "EntityGeometry.h"

#ifdef EDITOR
#include "EditMesh.h"
#include "Globals.h"
#include "GPU_Globals.h"
#include "GPU_API.h"
#include "API.h"
#include "Prefab.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <format>
#include <map>
#include <stdexcept>
#include <glm/gtc/matrix_inverse.hpp>

namespace Loading
{
namespace
{
std::map<std::string, PrefabGeometry> records;

/* The vertices of one baked mesh as the bytes of the standard layout. */
std::vector<unsigned char> vertexBytes(const std::vector<Graphics::Vertex>& rVertices_)
{
	std::vector<unsigned char> bytes(rVertices_.size() * sizeof(Graphics::Vertex));
	if(!rVertices_.empty()) std::memcpy(bytes.data(), rVertices_.data(), bytes.size());
	return bytes;
}
}

BakedEntity bakeEntity(const Entity& rEntity_)
{
	std::vector<std::pair<const Graphics::BasicMesh*, glm::mat4>> meshes;
	rEntity_.instanceMeshes(meshes, true);
	if(meshes.empty()) throw std::runtime_error("the entity has no model to edit.");

	/* The instance turns its model (instanceTurn) before the entity's transform; an override model is drawn without that, so the turn is baked in. */
	glm::mat4 turn = rEntity_.instanceTurn();
	BakedEntity baked;
	baked.data.layout = Graphics::VertexLayout::standard();
	glm::vec3 low(1e30f);
	glm::vec3 high(-1e30f);
	for(const auto& [pMesh, rPlacement] : meshes)
	{
		MeshOriginal original = readMeshOriginal(*pMesh);
		glm::mat4 matrix = turn * rPlacement;
		glm::mat3 normalMatrix = glm::inverseTranspose(glm::mat3(matrix));
		for(Graphics::Vertex& rVertex : original.vertices)
		{
			rVertex.position = glm::vec3(matrix * glm::vec4(rVertex.position, 1.0f));
			rVertex.normal = glm::normalize(normalMatrix * rVertex.normal);
			low = glm::min(low, rVertex.position);
			high = glm::max(high, rVertex.position);
		}
		External::ModelSubmesh submesh;
		submesh.vertexCount = static_cast<uint32_t>(original.vertices.size());
		submesh.vertices = vertexBytes(original.vertices);
		for(const std::array<uint32_t, 3>& rFace : original.faces) submesh.indices.insert(submesh.indices.end(), rFace.begin(), rFace.end());
		baked.data.submeshes.push_back(std::move(submesh));
		baked.ownTextures.push_back(rEntity_.instanceMeshTexture(pMesh));
		baked.originals.push_back(std::move(original));
	}
	baked.data.bounds.center = (low + high) * 0.5f;
	baked.data.bounds.radius = glm::distance(low, high) * 0.5f;
	baked.data.bounds.valid = true;
	return baked;
}

BakedEntity bakeModelData(const External::ModelData& rModel_)
{
	if(rModel_.layout.key() != Graphics::VertexLayout::standard().key()) throw std::runtime_error("the model has a custom vertex layout.");
	BakedEntity baked;
	baked.data = rModel_;
	for(uint32_t i = 0; i < rModel_.submeshes.size(); i++)
	{
		const External::ModelSubmesh& rSubmesh = rModel_.submeshes[i];
		if(rSubmesh.vertices.size() != rSubmesh.vertexCount * sizeof(Graphics::Vertex)) throw std::runtime_error("the model's vertices don't have the standard size.");
		MeshOriginal original;
		original.sourceMesh = i;
		original.vertices.resize(rSubmesh.vertexCount);
		if(!rSubmesh.vertices.empty()) std::memcpy(original.vertices.data(), rSubmesh.vertices.data(), rSubmesh.vertices.size());
		for(size_t k = 0; k + 2 < rSubmesh.indices.size(); k += 3) original.faces.push_back({rSubmesh.indices[k], rSubmesh.indices[k + 1], rSubmesh.indices[k + 2]});
		baked.ownTextures.push_back(rSubmesh.texture);
		baked.originals.push_back(std::move(original));
	}
	return baked;
}

External::ModelData modelDataWithDiffs(const BakedEntity& rBaked_, const std::vector<MeshDiff>& rDiffs_, const std::string& name_)
{
	External::ModelData data;
	data.name = name_;
	data.layout = Graphics::VertexLayout::standard();
	glm::vec3 low(1e30f);
	glm::vec3 high(-1e30f);
	for(uint32_t i = 0; i < rBaked_.originals.size(); i++)
	{
		const MeshOriginal& rOriginal = rBaked_.originals[i];
		MeshDiff none;
		const MeshDiff* pDiff = &none;
		for(const MeshDiff& rDiff : rDiffs_)
		{
			if(rDiff.ordinal == i) pDiff = &rDiff;
		}
		MeshState state;
		if(!buildMeshState(rOriginal, *pDiff, state)) throw std::runtime_error(std::format("the edit of mesh {} doesn't fit the model.", i));
		refreshNormals(state.vertices, state.faces, state.touched);

		/* The faces by texture, in the order the textures first appear; a face with none keeps the submesh's own. */
		std::vector<std::string> textures;
		std::vector<std::vector<uint32_t>> facesOf;
		for(uint32_t face = 0; face < state.faces.size(); face++)
		{
			const std::string& rTexture = state.faceGroup[face] < 0 ? rBaked_.ownTextures[i] : pDiff->faceTextures[state.faceGroup[face]].texture;
			if(rTexture.starts_with("disc:")) throw std::runtime_error("a model file can't keep a disc texture, use an External one.");
			size_t group = std::find(textures.begin(), textures.end(), rTexture) - textures.begin();
			if(group == textures.size())
			{
				textures.push_back(rTexture);
				facesOf.emplace_back();
			}
			facesOf[group].push_back(face);
		}
		for(size_t group = 0; group < textures.size(); group++)
		{
			External::ModelSubmesh submesh;
			submesh.texture = textures[group];
			std::map<uint32_t, uint32_t> remap;
			std::vector<Graphics::Vertex> vertices;
			for(uint32_t face : facesOf[group])
			{
				for(uint32_t corner : state.faces[face])
				{
					auto [found, added] = remap.try_emplace(corner, static_cast<uint32_t>(vertices.size()));
					if(added)
					{
						vertices.push_back(state.vertices[corner]);
						low = glm::min(low, state.vertices[corner].position);
						high = glm::max(high, state.vertices[corner].position);
					}
					submesh.indices.push_back(found->second);
				}
			}
			submesh.vertexCount = static_cast<uint32_t>(vertices.size());
			submesh.vertices = vertexBytes(vertices);
			data.submeshes.push_back(std::move(submesh));
		}
	}
	if(data.submeshes.empty()) throw std::runtime_error("every face of the model is deleted.");
	data.bounds.center = (low + high) * 0.5f;
	data.bounds.radius = glm::distance(low, high) * 0.5f;
	data.bounds.valid = true;
	return data;
}

const Graphics::ExternalModel& saveFileModel(ExternalScope scope_, External::ModelData data_)
{
	Graphics::ExternalPools& rPools = externalPoolsOf(scope_);
	std::string name = data_.name;

	/* The old model is freed by the replacement: the entities that draw it are given the new one. */
	const Graphics::ExternalModel* pOld = nullptr;
	auto found = rPools.models.nameToId.find(name);
	if(found != rPools.models.nameToId.end()) pOld = &rPools.models[found->second];
	std::vector<PoolId> users;
	for(uint32_t i = 0; pOld != nullptr && i < Globals::Game::entities.objects.size(); i++)
	{
		if(Globals::Game::entities.alive[i] && Globals::Game::entities.objects[i].pOverrideModel == pOld) users.push_back(PoolId(i, Globals::Game::entities.generation[i]));
	}
	importExternalModel(std::move(data_), scope_);
	const Graphics::ExternalModel& rModel = rPools.models[rPools.models.nameToId.at(name)];
	for(PoolId id : users) Globals::Game::entities[id].setOverrideModel(&rModel);
	return rModel;
}

std::string externalPrefabPoolName(ExternalScope scope_, const std::string& levelName_, const std::string& name_)
{
	return scope_ == ExternalScope::Project ? "Global/" + name_ : "Level/" + levelName_ + "/" + name_;
}

std::string editedModelName(const std::string& poolName_)
{
	return poolName_ + " geometry";
}

const Graphics::ExternalModel& buildEditedModel(Graphics::ExternalPools& rPools_, bool inProject_, const std::string& name_, const BakedEntity& rBaked_, const std::vector<MeshDiff>& rDiffs_)
{
	External::ModelData data = rBaked_.data;
	data.name = name_;
	const Graphics::ExternalModel& rModel = addExternalModel(rPools_, inProject_, std::move(data));
	for(uint32_t i = 0; i < rBaked_.originals.size(); i++)
	{
		const MeshOriginal& rOriginal = rBaked_.originals[i];
		const MeshDiff* pDiff = nullptr;
		for(const MeshDiff& rDiff : rDiffs_)
		{
			if(rDiff.ordinal == i) pDiff = &rDiff;
		}
		if(pDiff != nullptr && (pDiff->sourceMesh != rOriginal.sourceMesh || pDiff->vertexCount != rOriginal.vertices.size() || pDiff->indexCount != rOriginal.faces.size() * 3))
		{
			std::fprintf(stderr, "Prefab geometry %s: the edit of mesh %u doesn't fit the source model, skipped.\n", name_.c_str(), i);
			pDiff = nullptr;
		}
		Graphics::EditMesh edit;
		try
		{
			edit = Graphics::EditMesh::open(std::string(), i, rOriginal, pDiff);
		}
		catch(const std::exception& rError)
		{
			std::fprintf(stderr, "Prefab geometry %s: mesh %u: %s\n", name_.c_str(), i, rError.what());
			edit = Graphics::EditMesh::open(std::string(), i, rOriginal, nullptr);
		}
		edit.ownTexture = rBaked_.ownTextures[i];
		if(pDiff != nullptr || !edit.ownTexture.empty()) edit.upload(rPools_.meshes[rModel.submeshes[i].meshId]);
	}
	return rModel;
}

void setPrefabGeometry(const std::string& poolName_, PrefabGeometry geometry_)
{
	records[poolName_] = std::move(geometry_);
}

const PrefabGeometry* prefabGeometry(const std::string& poolName_)
{
	auto found = records.find(poolName_);
	return found == records.end() ? nullptr : &found->second;
}

void forgetLevelGeometry(const std::string& levelName_)
{
	std::string prefix = "Level/" + levelName_ + "/";
	std::erase_if(records, [&](const auto& rEntry_) { return rEntry_.first.starts_with(prefix); });
}

const Graphics::ExternalModel& replaceEditedModel(ExternalScope scope_, const std::string& poolName_, const BakedEntity& rBaked_, const std::vector<MeshDiff>& rDiffs_)
{
	Graphics::ExternalPools& rPools = externalPoolsOf(scope_);
	std::string name = editedModelName(poolName_);

	/* The old model is freed by the replacement: the entities that draw it are given the new one. */
	const Graphics::ExternalModel* pOld = nullptr;
	auto found = rPools.models.nameToId.find(name);
	if(found != rPools.models.nameToId.end()) pOld = &rPools.models[found->second];
	std::vector<PoolId> users;
	for(uint32_t i = 0; pOld != nullptr && i < Globals::Game::entities.objects.size(); i++)
	{
		if(Globals::Game::entities.alive[i] && Globals::Game::entities.objects[i].pOverrideModel == pOld) users.push_back(PoolId(i, Globals::Game::entities.generation[i]));
	}
	const Graphics::ExternalModel& rModel = buildEditedModel(rPools, scope_ == ExternalScope::Project, name, rBaked_, rDiffs_);
	for(PoolId id : users) Globals::Game::entities[id].setOverrideModel(&rModel);
	return rModel;
}

BakedEntity bakePrefabSource(const PrefabGeometry& rGeometry_)
{
	static PoolMap<Entity> pool("EntityGeometrySources");
	auto source = Globals::Game::prefabs.nameToId.find(rGeometry_.source);
	if(source == Globals::Game::prefabs.nameToId.end()) throw std::runtime_error(std::format("the source prefab '{}' doesn't exist.", rGeometry_.source));
	const Prefab& rPrefab = Globals::Game::prefabs[source->second];
	SpawnParams params;
	params.what = "(prefab geometry)";
	prepareThumbnailParams(rPrefab, params);
	PoolId entityId = spawnPrefab(source->second, Graphics::getShaderId(gameShaderName(rPrefab.game)), params, &pool);
	if(entityId == UninitializedPoolId) throw std::runtime_error("the source entity can't be made.");
	try
	{
		Entity& rEntity = pool[entityId];
		if(!rGeometry_.discMesh.empty() && rEntity.discMesh() != rGeometry_.discMesh) rEntity.setDiscMesh(rGeometry_.discMesh);
		if(!rGeometry_.discTexture.empty() && rEntity.discTexture() != rGeometry_.discTexture) rEntity.setDiscTexture(rGeometry_.discTexture);
		BakedEntity baked = bakeEntity(rEntity);
		pool.remove(entityId);
		return baked;
	}
	catch(...)
	{
		pool.remove(entityId);
		throw;
	}
}

void rebuildEditedModels(ExternalScope scope_, const std::string& levelName_)
{
	std::string prefix = scope_ == ExternalScope::Project ? "Global/" : "Level/" + levelName_ + "/";
	for(const auto& [rPoolName, rGeometry] : records)
	{
		if(!rPoolName.starts_with(prefix)) continue;
		try
		{
			BakedEntity baked = bakePrefabSource(rGeometry);
			replaceEditedModel(scope_, rPoolName, baked, rGeometry.meshes);
		}
		catch(const std::exception& rError)
		{
			std::fprintf(stderr, "Prefab geometry %s not built: %s\n", rPoolName.c_str(), rError.what());
		}
	}
}
}
#endif
