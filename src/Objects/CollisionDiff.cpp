#include "CollisionDiff.h"
#include "BinaryRead.h"
#include <algorithm>
#include <cstring>
#include <format>
#include <stdexcept>

namespace
{
constexpr char Magic[4] = {'S', 'F', 'H', 'C'};
constexpr uint32_t Version = 1;
constexpr uint32_t NoPiece = 0xFFFFFFFF;

template<typename T>
void put(std::vector<uint8_t>& rBytes_, const T& rValue_)
{
	size_t old = rBytes_.size();
	rBytes_.resize(old + sizeof(T));
	std::memcpy(rBytes_.data() + old, &rValue_, sizeof(T));
}

void putVector(std::vector<uint8_t>& rBytes_, const glm::vec3& rValue_) { put(rBytes_, rValue_.x); put(rBytes_, rValue_.y); put(rBytes_, rValue_.z); }

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
	glm::vec3 vec3() { float x = this->get<float>(); float y = this->get<float>(); float z = this->get<float>(); return glm::vec3(x, y, z); }
};
}

std::vector<uint32_t> collisionTriangleValues(const Physics::CollisionMesh& rMesh_)
{
	size_t triangleCount = rMesh_.indices.size() / 3;
	if(!rMesh_.triangleAttributes.empty()) return rMesh_.triangleAttributes;
	std::vector<uint32_t> values(triangleCount, 0);
	for(const Physics::CollisionPiece& rPiece : rMesh_.pieces)
	{
		for(size_t t = rPiece.firstTriangle; t < rPiece.firstTriangle + rPiece.triangleCount && t < triangleCount; t++) values[t] = rPiece.flags;
	}
	return values;
}

bool applyCollisionDiff(Physics::CollisionMesh& rMesh_, const CollisionDiff& rDiff_)
{
	uint32_t vertexCount = static_cast<uint32_t>(rMesh_.vertices.size());
	uint32_t triangleCount = static_cast<uint32_t>(rMesh_.indices.size() / 3);
	if(rDiff_.vertexCount != vertexCount || rDiff_.triangleCount != triangleCount) return false;
	uint32_t finalVertices = vertexCount + static_cast<uint32_t>(rDiff_.addedVertices.size());
	for(const auto& [index, rPosition] : rDiff_.moved)
	{
		if(index >= vertexCount) return false;
	}
	for(uint32_t triangle : rDiff_.deletedTriangles)
	{
		if(triangle >= triangleCount) return false;
	}
	for(const CollisionDiff::AddedTriangle& rAdded : rDiff_.addedTriangles)
	{
		if(rAdded.v[0] >= finalVertices || rAdded.v[1] >= finalVertices || rAdded.v[2] >= finalVertices) return false;
	}
	for(const auto& [triangle, value] : rDiff_.valueChanges)
	{
		if(triangle >= triangleCount) return false;
	}

	bool perTriangle = !rMesh_.triangleAttributes.empty();
	bool hasMaterials = rMesh_.triangleMaterials.size() == triangleCount;
	std::vector<uint32_t> oldValues = collisionTriangleValues(rMesh_);
	std::vector<uint32_t> pieceOf(triangleCount, NoPiece);
	for(size_t p = 0; p < rMesh_.pieces.size(); p++)
	{
		const Physics::CollisionPiece& rPiece = rMesh_.pieces[p];
		for(size_t t = rPiece.firstTriangle; t < rPiece.firstTriangle + rPiece.triangleCount && t < triangleCount; t++) pieceOf[t] = static_cast<uint32_t>(p);
	}
	std::vector<bool> deleted(triangleCount, false);
	for(uint32_t triangle : rDiff_.deletedTriangles) deleted[triangle] = true;
	for(const auto& [triangle, value] : rDiff_.valueChanges) oldValues[triangle] = value;

	for(const auto& [index, rPosition] : rDiff_.moved) rMesh_.vertices[index] = rPosition;
	rMesh_.vertices.insert(rMesh_.vertices.end(), rDiff_.addedVertices.begin(), rDiff_.addedVertices.end());

	std::vector<uint32_t> indices;
	std::vector<uint32_t> values;
	std::vector<uint32_t> pieces;
	std::vector<uint16_t> materials;
	for(uint32_t t = 0; t < triangleCount; t++)
	{
		if(deleted[t]) continue;
		indices.insert(indices.end(), rMesh_.indices.begin() + t * 3, rMesh_.indices.begin() + t * 3 + 3);
		values.push_back(oldValues[t]);
		pieces.push_back(pieceOf[t]);
		if(hasMaterials) materials.push_back(rMesh_.triangleMaterials[t]);
	}
	for(const CollisionDiff::AddedTriangle& rAdded : rDiff_.addedTriangles)
	{
		indices.insert(indices.end(), rAdded.v.begin(), rAdded.v.end());
		values.push_back(rAdded.value);
		pieces.push_back(NoPiece);
		if(hasMaterials) materials.push_back(0);
	}

	/* A piece runs while the old piece (and, for pieces that carry the value, the value) stays the same; its sphere is the box centre of its vertices and the farthest of them. */
	std::vector<Physics::CollisionPiece> newPieces;
	for(size_t t = 0; t < values.size(); t++)
	{
		bool starts = t == 0 || pieces[t] != pieces[t - 1] || (!perTriangle && values[t] != values[t - 1]);
		if(starts)
		{
			Physics::CollisionPiece piece{};
			piece.flags = perTriangle ? (pieces[t] != NoPiece ? rMesh_.pieces[pieces[t]].flags : 0) : values[t];
			piece.firstTriangle = static_cast<uint32_t>(t);
			newPieces.push_back(piece);
		}
		newPieces.back().triangleCount++;
	}
	for(Physics::CollisionPiece& rPiece : newPieces)
	{
		glm::vec3 low(1e30f);
		glm::vec3 high(-1e30f);
		for(uint32_t i = rPiece.firstTriangle * 3; i < (rPiece.firstTriangle + rPiece.triangleCount) * 3; i++)
		{
			low = glm::min(low, rMesh_.vertices[indices[i]]);
			high = glm::max(high, rMesh_.vertices[indices[i]]);
		}
		rPiece.center = (low + high) * 0.5f;
		rPiece.radius = 0.0f;
		for(uint32_t i = rPiece.firstTriangle * 3; i < (rPiece.firstTriangle + rPiece.triangleCount) * 3; i++) rPiece.radius = std::max(rPiece.radius, glm::distance(rPiece.center, rMesh_.vertices[indices[i]]));
	}

	rMesh_.indices = std::move(indices);
	rMesh_.pieces = std::move(newPieces);
	if(perTriangle) rMesh_.triangleAttributes = std::move(values);
	if(hasMaterials) rMesh_.triangleMaterials = std::move(materials);
	return true;
}

std::vector<uint8_t> writeCollisionDiffs(const std::vector<LevelCollisionDiff>& rLevels_)
{
	std::vector<uint8_t> bytes(Magic, Magic + 4);
	put(bytes, Version);
	put(bytes, static_cast<uint32_t>(rLevels_.size()));
	for(const LevelCollisionDiff& rLevel : rLevels_)
	{
		const CollisionDiff& rDiff = rLevel.diff;
		put(bytes, static_cast<uint32_t>(rLevel.level.size()));
		bytes.insert(bytes.end(), rLevel.level.begin(), rLevel.level.end());
		put(bytes, rDiff.vertexCount);
		put(bytes, rDiff.triangleCount);
		put(bytes, static_cast<uint32_t>(rDiff.moved.size()));
		for(const auto& [index, rPosition] : rDiff.moved)
		{
			put(bytes, index);
			putVector(bytes, rPosition);
		}
		put(bytes, static_cast<uint32_t>(rDiff.deletedTriangles.size()));
		for(uint32_t triangle : rDiff.deletedTriangles) put(bytes, triangle);
		put(bytes, static_cast<uint32_t>(rDiff.addedVertices.size()));
		for(const glm::vec3& rPosition : rDiff.addedVertices) putVector(bytes, rPosition);
		put(bytes, static_cast<uint32_t>(rDiff.addedTriangles.size()));
		for(const CollisionDiff::AddedTriangle& rAdded : rDiff.addedTriangles)
		{
			for(uint32_t index : rAdded.v) put(bytes, index);
			put(bytes, rAdded.value);
		}
		put(bytes, static_cast<uint32_t>(rDiff.valueChanges.size()));
		for(const auto& [triangle, value] : rDiff.valueChanges)
		{
			put(bytes, triangle);
			put(bytes, value);
		}
	}
	return bytes;
}

std::vector<LevelCollisionDiff> readCollisionDiffs(const std::vector<uint8_t>& rBytes_)
{
	if(rBytes_.size() < 4 || std::memcmp(rBytes_.data(), Magic, 4) != 0) throw std::runtime_error("not a HubWorld.collision.bin (wrong magic).");
	Reader reader{rBytes_, 4};
	uint32_t version = reader.get<uint32_t>();
	if(version != Version) throw std::runtime_error(std::format("HubWorld.collision.bin version {} is not one this build reads ({}).", version, Version));

	/* A count in a damaged file could ask for gigabytes: a count can't be more than the bytes left, since each entry takes at least a byte. */
	auto count = [&]()
	{
		uint32_t value = reader.get<uint32_t>();
		if(value > rBytes_.size()) throw std::runtime_error("HubWorld.collision.bin: a count is larger than the file.");
		return value;
	};
	std::vector<LevelCollisionDiff> levels(count());
	for(LevelCollisionDiff& rLevel : levels)
	{
		uint32_t nameLength = count();
		if(reader.offset + nameLength > rBytes_.size()) throw std::runtime_error("HubWorld.collision.bin: cut short.");
		rLevel.level.assign(reinterpret_cast<const char*>(rBytes_.data() + reader.offset), nameLength);
		reader.offset += nameLength;
		CollisionDiff& rDiff = rLevel.diff;
		rDiff.vertexCount = reader.get<uint32_t>();
		rDiff.triangleCount = reader.get<uint32_t>();
		rDiff.moved.resize(count());
		for(auto& rMove : rDiff.moved)
		{
			rMove.first = reader.get<uint32_t>();
			rMove.second = reader.vec3();
		}
		rDiff.deletedTriangles.resize(count());
		for(uint32_t& rTriangle : rDiff.deletedTriangles) rTriangle = reader.get<uint32_t>();
		rDiff.addedVertices.resize(count());
		for(glm::vec3& rPosition : rDiff.addedVertices) rPosition = reader.vec3();
		rDiff.addedTriangles.resize(count());
		for(CollisionDiff::AddedTriangle& rAdded : rDiff.addedTriangles)
		{
			for(uint32_t& rIndex : rAdded.v) rIndex = reader.get<uint32_t>();
			rAdded.value = reader.get<uint32_t>();
		}
		rDiff.valueChanges.resize(count());
		for(auto& rChange : rDiff.valueChanges)
		{
			rChange.first = reader.get<uint32_t>();
			rChange.second = reader.get<uint32_t>();
		}
	}
	return levels;
}
