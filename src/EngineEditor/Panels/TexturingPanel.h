#pragma once

#ifdef EDITOR
#include "GeometryPanel.h"
#include <string>
#include <vector>
#include <glm/glm.hpp>

namespace Graphics
{
/*
The HubWorld tab's Texturing subtab (Editor::texturing; EntityListPanel::draw shows drawTab and sets Editor::hubWorldTexturing): gives faces of the level geometry an External texture and edits their UVs, the second of the HubWorld features that replace the Blender route (docs/TODO.md).
It picks faces with the GeometryPanel (Editor::processInputs sets GeometryPanel::texturing, Face mode: a left click adds the face under the cursor, a right click takes it out) and works on the mesh it has open (EditMesh); Save is GeometryPanel::save (HubWorld.bin).
The screen is split: the viewport takes the top ViewportFraction of the window (Camera::screenFraction), and drawPanel's window the part below it down to the Resources panel. The window shows the texture of the picked faces (the first one's; a face with none shows the unit square empty) with the faces' UV triangles over it,
filled purple at 30% opacity with an opaque outline, in the box of their UVs with a handle on each corner. Dragging inside the box moves the UVs, dragging a corner scales them from the centre of the box (modelled on MikesFreddyNightmare's AnimatronicPanel::drawPlacement, move and scale only). A UV that is shared with a face that isn't picked is
copied first (EditMesh::isolate), so only the picked faces change. The window has two modes at its top, Set and Edit (editMode): in Edit a pick takes the face and the faces joined to it with its texture (GeometryPanel::island). A Done button at its top right ends the work on a selection: the dropped texture leaves the window and no face stays picked. A tile of the Assets tab ("TEXTURE" payload) dropped on the window, or the Texture... popup (External only), gives the picked faces that texture; BasicMesh::groups draws them with it. A disc tile is a whole set (a .GVM, a .TXD), so dropping one opens a picker of the textures in it (MeshDiff.h, discTextureTiles); the face then holds "disc:<set>#<texture name>". A single texture of a set opened in the Assets tab (double click the tile; "TEXTURETILE" payload, AssetPanel::drawTextureSet) is given at once, with no picker.
*/
struct TexturingPanel
{
	/* The mode chosen at the top of the window: Set (false) gives the picked faces a texture, Edit (true) picks a face with the faces joined to it that have its texture (EditMesh::island) to move and scale their UVs. */
	bool editMode = false;

	/* The share of the window's height the viewport takes, from the top. */
	static constexpr float ViewportFraction = 0.5f;

	/* The subtab's contents. */
	void drawTab(GeometryPanel& rGeometry_);
	/* The window with the UVs (DebugUI::draw, while the subtab is shown). */
	void drawPanel(GeometryPanel& rGeometry_);

private:
	/* Gives the picked faces the External texture ("" = their submesh's own), the mesh made again. */
	void assign(GeometryPanel& rGeometry_, const std::string& rTexture_);
	/* The bounds of the UVs of the picked faces. */
	bool uvBounds(GeometryPanel& rGeometry_, glm::vec2& rMin_, glm::vec2& rMax_);

	/* The drag: 0 none, 1 move, 2 scale. dragVertices are the vertices of the picked faces (after EditMesh::isolate) and dragStart their UVs when it began. */
	int drag = 0;
	uint32_t seenUndone = 0;
	std::vector<uint32_t> dragVertices;
	std::vector<glm::vec2> dragStart;
	glm::vec2 dragPivot = glm::vec2(0.0f);
	glm::vec2 dragMouse = glm::vec2(0.0f);
	float dragDistance = 1.0f;
	/* The part of UV space the window shows, kept while dragging so the view doesn't move under the mouse. */
	glm::vec2 viewMin = glm::vec2(0.0f);
	glm::vec2 viewMax = glm::vec2(1.0f);
	/* The texture shown in the window after one was dropped on it ("" = the first picked face's own); Done clears it. */
	std::string slotTexture;
	/* The disc set (a SADX .GVM's path, a Shadow .TXD's entry name) whose textures the picker popup lists after a disc tile was dropped. */
	std::string discSet;
	std::string message;
};
}
#endif
