#include "MeshDiff.h"
#include "BinaryRead.h"
#include "ExternalAssets.h"
#include "Texture.h"
#include <cstdio>
#include <cstring>
#include <format>
#include <stdexcept>
#include <unordered_map>

namespace
{
constexpr char Magic[4] = {'S', 'F', 'H', 'B'};
constexpr uint32_t Version = 2;

template<typename T>
void put(std::vector<uint8_t>& rBytes_, const T& rValue_)
{
	size_t old = rBytes_.size();
	rBytes_.resize(old + sizeof(T));
	std::memcpy(rBytes_.data() + old, &rValue_, sizeof(T));
}

void putVector(std::vector<uint8_t>& rBytes_, const glm::vec2& rValue_) { put(rBytes_, rValue_.x); put(rBytes_, rValue_.y); }
void putVector(std::vector<uint8_t>& rBytes_, const glm::vec3& rValue_) { put(rBytes_, rValue_.x); put(rBytes_, rValue_.y); put(rBytes_, rValue_.z); }
void putVector(std::vector<uint8_t>& rBytes_, const glm::vec4& rValue_) { put(rBytes_, rValue_.x); put(rBytes_, rValue_.y); put(rBytes_, rValue_.z); put(rBytes_, rValue_.w); }

/* A reader over the file's bytes: each get checks the bounds (readLE throws past the end). */
struct Reader
{
	const std::vector<uint8_t>& bytes;
	uint64_t offset = 0;

	template<typename T>
	T get()
	{
		T value = readLE<T>(this->bytes, this->offset);
		this->offset += sizeof(T);
		return value;
	}
	glm::vec2 vec2() { float x = this->get<float>(); float y = this->get<float>(); return glm::vec2(x, y); }
	glm::vec3 vec3() { float x = this->get<float>(); float y = this->get<float>(); float z = this->get<float>(); return glm::vec3(x, y, z); }
	glm::vec4 vec4() { float x = this->get<float>(); float y = this->get<float>(); float z = this->get<float>(); float w = this->get<float>(); return glm::vec4(x, y, z, w); }
};

}

/* The normal of each touched vertex is the sum of the (area weighted) normals of the faces that use it. A touched vertex no face uses keeps its normal. */
void refreshNormals(std::vector<Graphics::Vertex>& rVertices_, const std::vector<std::array<uint32_t, 3>>& rFaces_, const std::vector<uint32_t>& rTouched_)
{
	if(rTouched_.empty()) return;
	std::unordered_map<uint32_t, glm::vec3> sums;
	for(uint32_t index : rTouched_) sums[index] = glm::vec3(0.0f);
	for(const std::array<uint32_t, 3>& rFace : rFaces_)
	{
		glm::vec3 normal = glm::cross(rVertices_[rFace[1]].position - rVertices_[rFace[0]].position, rVertices_[rFace[2]].position - rVertices_[rFace[0]].position);
		for(uint32_t index : rFace)
		{
			auto found = sums.find(index);
			if(found != sums.end()) found->second += normal;
		}
	}
	for(const auto& [index, rSum] : sums)
	{
		if(glm::dot(rSum, rSum) > 1e-20f) rVertices_[index].normal = glm::normalize(rSum);
	}
}

MeshOriginal readMeshOriginal(const Graphics::BasicMesh& rMesh_)
{
	MeshOriginal original;
	original.sourceMesh = rMesh_.sourceMesh;
	original.vertices.assign(rMesh_.verticies.begin(), rMesh_.verticies.end());
	if(rMesh_.indicies.empty())
	{
		for(uint32_t i = 0; i + 2 < original.vertices.size(); i += 3) original.faces.push_back({i, i + 1, i + 2});
	}
	else
	{
		for(size_t i = 0; i + 2 < rMesh_.indicies.size(); i += 3) original.faces.push_back({rMesh_.indicies[i], rMesh_.indicies[i + 1], rMesh_.indicies[i + 2]});
	}
	return original;
}

Graphics::Texture* findFaceTexture(const std::string& rName_)
{
	if(!rName_.starts_with("disc:")) return Loading::findExternalTexture(rName_);
	size_t hash = rName_.rfind('#');
	if(hash == std::string::npos || !discTextureTiles) return nullptr;
	std::string name = rName_.substr(hash + 1);
	for(const DiscTextureTile& rTile : discTextureTiles(rName_.substr(5, hash - 5)))
	{
		if(rTile.name == name) return rTile.pTexture;
	}
	return nullptr;
}

void setMeshTriangles(Graphics::BasicMesh& rMesh_, std::vector<Graphics::Vertex> vertices_, const std::vector<std::array<uint32_t, 3>>& rFaces_, const std::vector<uint32_t>& rTouched_, const std::vector<int32_t>* pFaceGroup_, const std::vector<std::string>* pGroupTextures_)
{
	refreshNormals(vertices_, rFaces_, rTouched_);

	/* A group whose texture isn't found (the External file is gone) leaves its faces with the submesh's own. */
	size_t groupCount = pGroupTextures_ == nullptr ? 0 : pGroupTextures_->size();
	std::vector<GLuint> textures(groupCount, 0);
	for(size_t g = 0; g < groupCount; g++)
	{
		if(Graphics::Texture* pTexture = findFaceTexture((*pGroupTextures_)[g])) textures[g] = pTexture->textureId;
		else fprintf(stderr, "HubWorld.bin: face texture '%s' not found, its faces keep the mesh's own\n", (*pGroupTextures_)[g].c_str());
	}
	auto groupOf = [&](size_t face_) -> int32_t
	{
		int32_t group = pFaceGroup_ != nullptr && face_ < pFaceGroup_->size() ? (*pFaceGroup_)[face_] : -1;
		return group >= 0 && static_cast<size_t>(group) < groupCount && textures[group] != 0 ? group : -1;
	};
	std::vector<uint32_t> indices;
	indices.reserve(rFaces_.size() * 3);
	std::vector<Graphics::IndexGroup> groups;
	for(int32_t group = -1; group < static_cast<int32_t>(groupCount); group++)
	{
		uint32_t first = static_cast<uint32_t>(indices.size());
		for(size_t f = 0; f < rFaces_.size(); f++)
		{
			if(groupOf(f) == group) indices.insert(indices.end(), rFaces_[f].begin(), rFaces_[f].end());
		}
		if(group >= 0 && indices.size() > first) groups.push_back({first, static_cast<uint32_t>(indices.size()) - first, textures[group]});
	}
	if(indices.empty())
	{
		indices = {0, 0, 0};
		groups.clear();
	}
	if(!rMesh_.sourceVertex.empty()) rMesh_.sourceVertex.resize(vertices_.size(), UINT32_MAX);
	rMesh_.destroy();
	rMesh_.verticies = vertices_;
	rMesh_.indicies = indices;
	rMesh_.groups = std::move(groups);
	rMesh_.create();
}

bool buildMeshState(const MeshOriginal& rOriginal_, const MeshDiff& rDiff_, MeshState& rState_)
{
	rState_ = MeshState();
	rState_.vertices = rOriginal_.vertices;
	for(const auto& [index, rPosition] : rDiff_.moved)
	{
		if(index >= rState_.vertices.size()) return false;
		rState_.vertices[index].position = rPosition;
		rState_.touched.push_back(index);
	}
	for(const auto& [index, rUv] : rDiff_.uvMoved)
	{
		if(index >= rState_.vertices.size()) return false;
		rState_.vertices[index].uv = rUv;
	}
	for(const Graphics::Vertex& rVertex : rDiff_.addedVertices)
	{
		rState_.touched.push_back(static_cast<uint32_t>(rState_.vertices.size()));
		rState_.vertices.push_back(rVertex);
	}
	std::vector<bool> removed(rOriginal_.faces.size(), false);
	for(uint32_t face : rDiff_.deletedFaces)
	{
		if(face >= removed.size()) return false;
		removed[face] = true;
	}
	for(size_t i = 0; i < rOriginal_.faces.size(); i++)
	{
		if(!removed[i]) rState_.faces.push_back(rOriginal_.faces[i]);
	}
	for(const std::array<uint32_t, 3>& rFace : rDiff_.addedFaces)
	{
		if(rFace[0] >= rState_.vertices.size() || rFace[1] >= rState_.vertices.size() || rFace[2] >= rState_.vertices.size()) return false;
		rState_.faces.push_back(rFace);
	}
	rState_.faceGroup.assign(rState_.faces.size(), -1);
	for(size_t g = 0; g < rDiff_.faceTextures.size(); g++)
	{
		for(uint32_t face : rDiff_.faceTextures[g].faces)
		{
			if(face >= rState_.faces.size()) return false;
			rState_.faceGroup[face] = static_cast<int32_t>(g);
		}
	}
	return true;
}

bool applyMeshDiff(Graphics::BasicMesh& rMesh_, const MeshDiff& rDiff_, MeshOriginal& rOriginal_)
{
	MeshOriginal original = readMeshOriginal(rMesh_);
	if(original.sourceMesh != rDiff_.sourceMesh || original.vertices.size() != rDiff_.vertexCount || original.faces.size() * 3 != rDiff_.indexCount) return false;

	MeshState state;
	if(!buildMeshState(original, rDiff_, state)) return false;
	std::vector<std::string> textures;
	for(const FaceTextureGroup& rGroup : rDiff_.faceTextures) textures.push_back(rGroup.texture);
	setMeshTriangles(rMesh_, std::move(state.vertices), state.faces, state.touched, &state.faceGroup, &textures);
	rOriginal_ = std::move(original);
	return true;
}

std::vector<uint8_t> writeMeshDiffs(const std::vector<LevelMeshDiffs>& rLevels_)
{
	std::vector<uint8_t> bytes(Magic, Magic + 4);
	put(bytes, Version);
	put(bytes, static_cast<uint32_t>(rLevels_.size()));
	for(const LevelMeshDiffs& rLevel : rLevels_)
	{
		put(bytes, static_cast<uint32_t>(rLevel.level.size()));
		bytes.insert(bytes.end(), rLevel.level.begin(), rLevel.level.end());
		put(bytes, static_cast<uint32_t>(rLevel.meshes.size()));
		for(const MeshDiff& rDiff : rLevel.meshes)
		{
			put(bytes, rDiff.ordinal);
			put(bytes, rDiff.sourceMesh);
			put(bytes, rDiff.vertexCount);
			put(bytes, rDiff.indexCount);
			put(bytes, static_cast<uint32_t>(rDiff.moved.size()));
			for(const auto& [index, rPosition] : rDiff.moved)
			{
				put(bytes, index);
				putVector(bytes, rPosition);
			}
			put(bytes, static_cast<uint32_t>(rDiff.deletedFaces.size()));
			for(uint32_t face : rDiff.deletedFaces) put(bytes, face);
			put(bytes, static_cast<uint32_t>(rDiff.addedVertices.size()));
			for(const Graphics::Vertex& rVertex : rDiff.addedVertices)
			{
				putVector(bytes, rVertex.position);
				putVector(bytes, rVertex.normal);
				putVector(bytes, rVertex.uv);
				putVector(bytes, rVertex.color);
			}
			put(bytes, static_cast<uint32_t>(rDiff.addedFaces.size()));
			for(const std::array<uint32_t, 3>& rFace : rDiff.addedFaces)
			{
				for(uint32_t index : rFace) put(bytes, index);
			}
		}
	}
	return bytes;
}

std::vector<LevelMeshDiffs> readMeshDiffs(const std::vector<uint8_t>& rBytes_)
{
	if(rBytes_.size() < 4 || std::memcmp(rBytes_.data(), Magic, 4) != 0) throw std::runtime_error("not a HubWorld.bin (wrong magic).");
	Reader reader{rBytes_, 4};
	uint32_t version = reader.get<uint32_t>();
	if(version < 1 || version > Version) throw std::runtime_error(std::format("HubWorld.bin version {} is not one this build reads ({}).", version, Version));

	/* A count in a damaged file could ask for gigabytes: a count can't be more than the bytes left, since each entry takes at least a byte. */
	auto count = [&]()
	{
		uint32_t value = reader.get<uint32_t>();
		if(value > rBytes_.size()) throw std::runtime_error("HubWorld.bin: a count is larger than the file.");
		return value;
	};
	std::vector<LevelMeshDiffs> levels(count());
	for(LevelMeshDiffs& rLevel : levels)
	{
		uint32_t nameLength = count();
		if(reader.offset + nameLength > rBytes_.size()) throw std::runtime_error("HubWorld.bin: cut short.");
		rLevel.level.assign(reinterpret_cast<const char*>(rBytes_.data() + reader.offset), nameLength);
		reader.offset += nameLength;
		rLevel.meshes.resize(count());
		for(MeshDiff& rDiff : rLevel.meshes)
		{
			rDiff.ordinal = reader.get<uint32_t>();
			rDiff.sourceMesh = reader.get<uint32_t>();
			rDiff.vertexCount = reader.get<uint32_t>();
			rDiff.indexCount = reader.get<uint32_t>();
			rDiff.moved.resize(count());
			for(auto& rMove : rDiff.moved)
			{
				rMove.first = reader.get<uint32_t>();
				rMove.second = reader.vec3();
			}
			rDiff.deletedFaces.resize(count());
			for(uint32_t& rFace : rDiff.deletedFaces) rFace = reader.get<uint32_t>();
			rDiff.addedVertices.resize(count());
			for(Graphics::Vertex& rVertex : rDiff.addedVertices)
			{
				rVertex.position = reader.vec3();
				rVertex.normal = reader.vec3();
				rVertex.uv = reader.vec2();
				rVertex.color = reader.vec4();
			}
			rDiff.addedFaces.resize(count());
			for(std::array<uint32_t, 3>& rFace : rDiff.addedFaces)
			{
				for(uint32_t& rIndex : rFace) rIndex = reader.get<uint32_t>();
			}
		}
	}
	return levels;
}
