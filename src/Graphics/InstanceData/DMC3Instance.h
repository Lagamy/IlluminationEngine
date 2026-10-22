#pragma once
#include <memory>
#include <optional>
#include <vector>
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include "PoolId.h"
#include "Texture.h"
#include "Mot.h"

namespace Graphics
{
/*
Per-entity state of a shared DMC3Mesh, and everything needed to draw it: the shader. The instance holds one user of the mesh (released by destroy).
boneRotation is the pose of a skinned mesh (DMC3Mesh::skeleton): a rotation per bone id, applied to the bone in its parent's space on top of the rest offset; empty = the rest pose. It is sized by setBoneRotation, and may be shorter than the skeleton.
boneTranslation replaces a bone's rest offset when it has a value (a motion's translation channels are absolute, not added to the rest offset); empty or nullopt = the rest offset. It may be shorter than the skeleton.
pOverrideTexture is a texture drawn on every textured submesh in place of its own (an external texture, Entity::setOverrideTexture); nullptr = the submesh's own. It points into the external pools, which outlive the entities that use it.
*/
struct DMC3Instance
{
	PoolId dmc3MeshId;
	PoolId shaderId;
	Texture* pOverrideTexture = nullptr;
	std::vector<glm::quat> boneRotation;
	std::vector<std::optional<glm::vec3>> boneTranslation;
	/* The playing motion (shared by the instances that play it, Loading::findOrImportMotion; nullptr = none) and the frame it is at. */
	std::shared_ptr<const DMC3::Motion> motion;
	float motionFrame = 0.0f;
	bool motionLoop = true;

	void draw(const glm::mat4& rBase_);
	void destroy();
};
}
