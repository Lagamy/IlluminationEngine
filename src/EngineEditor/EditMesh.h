#pragma once

#ifdef EDITOR
#include "MeshDiff.h"
#include <array>
#include <cstdint>
#include <string>
#include <vector>
#include <glm/glm.hpp>

namespace Graphics
{
/*
A level mesh being edited by the Geometry subtab (GeometryPanel): the mesh's vertices and triangles in a form that is changed freely, the imported mesh (MeshOriginal) it is compared with, and an undo stack. Vertices are only ever added, never removed (a deleted vertex is the triangles that used it gone), so a vertex keeps its index and a MeshDiff can name it.
A face knows the imported face it is (original, -1 for one the editor added) so that the faces that are gone can be told. upload writes the state into the BasicMesh (its GPU buffers made again); toDiff works out the MeshDiff of HubWorld.bin against the imported mesh.
The vertices of a mesh are split per UV, color or corner, so what the user sees as one corner is several vertices at one position: weld gives all of them, and the panel moves and picks them together.
*/
struct EditMesh
{
	struct Face
	{
		int32_t original;
		std::array<uint32_t, 3> v;
		/* Number in EditMesh::textures of the External texture the face is drawn with, -1 = the submesh's own. */
		int32_t texture = -1;
	};

	std::string level;
	uint32_t ordinal = 0;
	MeshOriginal original;
	std::vector<Vertex> vertices;
	std::vector<Face> faces;
	/* The External texture names faces refer to (Face::texture). A name stays listed when no face uses it any more; toDiff leaves it out. */
	std::vector<std::string> textures;
	/* The face texture name ("disc:<set>#<name>") the faces with texture -1 are drawn with, for a mesh that is a copy of an entity's (the copy has no submesh texture of its own to fall back on, EntityGeometry.h); upload makes them a group. Not part of the diff. */
	std::string ownTexture;
	/* Whether the state differs from the last save (set by every change, cleared by the panel's save). */
	bool changed = false;

	/* Makes the editing state of the imported mesh with pDiff_ (the diff the mesh has applied; null = the mesh is as imported) on it. */
	static EditMesh open(const std::string& rLevel_, uint32_t ordinal_, MeshOriginal original_, const MeshDiff* pDiff_);

	/* The nearest face the ray (in the mesh's space; direction need not be a unit vector, t_ is in its units) crosses, either side. False if none. */
	bool raycast(const glm::vec3& rOrigin_, const glm::vec3& rDirection_, float& rT_, uint32_t& rFace_) const;

	/* Every vertex at the position of vertex_ (itself included). */
	std::vector<uint32_t> weld(uint32_t vertex_) const;

	/* The face and the faces joined to it through shared edges that have its texture (the Texturing subtab's Edit mode picks these together). */
	std::vector<uint32_t> island(uint32_t face_) const;

	/* Undo steps: pushUndo before a change (a drag counts as one; it also drops the redo steps), undo goes back one, redo goes forward again over an undo. */
	void pushUndo();
	bool undo();
	bool redo();
	/* How many undo steps there are, and going back to the state before step depth_ at once (a discard: the steps after it are dropped, with no redo); false if there are not that many. */
	size_t undoCount() const { return this->undoStack.size(); }
	bool discardTo(size_t depth_);

	/* Removes the faces that use any of the vertices (deleteFaces: the faces of the numbers). */
	void deleteFacesUsing(const std::vector<uint32_t>& rVertices_);
	void deleteFaces(const std::vector<uint32_t>& rFaces_);
	/* Adds a vertex (uv, color and normal taken from the face it is on); returns its index. */
	uint32_t addVertex(const glm::vec3& rPosition_, uint32_t onFace_);
	/* Adds a face of three vertices as given. */
	void addFace(uint32_t a_, uint32_t b_, uint32_t c_);

	/* Gives the faces the External texture of the name ("" = the submesh's own). */
	void setTexture(const std::vector<uint32_t>& rFaces_, const std::string& rName_);
	/*
	Makes the corners of the faces their own vertices: a vertex a face outside the list also uses is copied (the copy is a vertex the editor added, taking the faces' corners), so that changing its uv leaves the other faces alone. Returns the vertices of the faces afterwards, each once.
	*/
	std::vector<uint32_t> isolate(const std::vector<uint32_t>& rFaces_);

	/* Writes the state into the mesh (BasicMesh) and uploads it. */
	void upload(BasicMesh& rMesh_) const;
	MeshDiff toDiff() const;
	/* Back to the imported mesh. */
	void revert();

private:
	struct State
	{
		std::vector<Vertex> vertices;
		std::vector<Face> faces;
		std::vector<std::string> textures;
	};
	std::vector<State> undoStack;
	std::vector<State> redoStack;
};
}
#endif
