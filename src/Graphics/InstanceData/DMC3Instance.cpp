#include "DMC3Instance.h"
#include "GPU_Globals.h"
#include "RenderStats.h"
#include "GPU_API.h"
#include "ResourceLoader.h"
#include <algorithm>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace Graphics
{
/*
Binds the instance's shader, uploads the mesh if needed and draws every submesh of every node.
Each node's world matrix is its parent's world matrix times its local matrix, rBase_ being the parent of roots.
Relies on DMC3Mesh storing parents before children.
A skinned mesh (non-empty skeleton) also gets the bone matrices: bone world = parent world * translate(rest offset) * rotation of the pose, matrix = bone world * inverse bind, which is the identity in the rest pose. They are uploaded once per draw to
the uniform array bones, hasSkin is set for every skinned mesh and cleared otherwise. The matrices are in the model's space, so the node matrix still applies after them.
A submesh with a texture of the mesh binds it (pOverrideTexture, when set, is bound in its place); one without clears hasTexture.
*/
void DMC3Instance::draw(const glm::mat4& rBase_)
{
	Shader& rShader = shaders[this->shaderId];
	renderStats.program(rShader.id);
	glUseProgram(rShader.id);
	DMC3Mesh& rDMC3Mesh = dmc3Meshes[this->dmc3MeshId];
	rDMC3Mesh.use();
	/* The world matrices live in a vector kept between calls: a heap allocation per entity per frame cost more than the math. */
	static std::vector<glm::mat4> world;
	world.resize(rDMC3Mesh.nodes.size());

	const DMC3Skeleton& rSkeleton = rDMC3Mesh.skeleton;
	bool skinned = !rSkeleton.order.empty();
	glUniform1i(HasSkinLocation, skinned);
	if(skinned)
	{
		static std::vector<glm::mat4> bones;
		bones.resize(rSkeleton.parent.size());
		for(uint8_t bone : rSkeleton.order)
		{
			glm::mat4 local = glm::translate(glm::mat4(1.0f), bone < this->boneTranslation.size() && this->boneTranslation[bone] ? *this->boneTranslation[bone] : rSkeleton.localTranslation[bone]);
			if(bone < this->boneRotation.size()) local *= glm::mat4_cast(this->boneRotation[bone]);
			int32_t parent = rSkeleton.parent[bone];
			bones[bone] = parent < 0 ? local : bones[parent] * local;
		}
		for(size_t bone = 0; bone < bones.size() && bone < MaxBones; bone++)
		{
			bones[bone] = bones[bone] * rSkeleton.inverseBind[bone];
		}
		renderStats.boneUploads++;
		glUniformMatrix4fv(BonesLocation, static_cast<GLsizei>(std::min<size_t>(bones.size(), MaxBones)), GL_FALSE, glm::value_ptr(bones[0]));
	}

	for(size_t i = 0; i < rDMC3Mesh.nodes.size(); i++)
	{
		DMC3Node& rNode = rDMC3Mesh.nodes[i];
		world[i] = (rNode.parent < 0 ? rBase_ : world[rNode.parent]) * rNode.localTransform;

		if(rNode.submeshes.empty()) continue;
		renderStats.uniformUploads++;
		glUniformMatrix4fv(rShader.modelLocation, 1, GL_FALSE, glm::value_ptr(world[i]));
		for(DMC3Submesh& rSubmesh : rNode.submeshes)
		{
			bool hasTexture = rSubmesh.textureIndex >= 0 && rSubmesh.textureIndex < static_cast<int32_t>(rDMC3Mesh.textures.size());
			if(hasTexture)
			{
				Texture& rTexture = this->pOverrideTexture ? *this->pOverrideTexture : rDMC3Mesh.textures[rSubmesh.textureIndex];
				renderStats.texture(rTexture.textureId);
				glBindTextureUnit(0, rTexture.textureId);
			}
			glUniform1i(HasTextureLocation, hasTexture);
			rSubmesh.mesh.draw();
		}
	}
}

/* Gives back the instance's user of its mesh; the mesh is removed when no instance uses it. */
void DMC3Instance::destroy()
{
	Loading::releaseDMC3Mesh(this->dmc3MeshId);
}
}
