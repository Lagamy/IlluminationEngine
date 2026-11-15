#include "TexturingPanel.h"

#ifdef EDITOR
#include "Editor.h"
#include "ExternalAssets.h"
#include "GPU_Globals.h"
#include "MeshDiff.h"
#include "imgui.h"
#include <algorithm>
#include <cmath>
#include <format>
#include <stdexcept>

namespace Graphics
{
namespace
{
constexpr ImU32 FaceFill = IM_COL32(160, 60, 255, 77);
constexpr ImU32 FaceOutline = IM_COL32(200, 130, 255, 255);
constexpr ImU32 BoxColor = IM_COL32(255, 220, 60, 255);
constexpr ImU32 Background = IM_COL32(24, 24, 28, 255);
constexpr float HandleRadius = 6.0f;
constexpr float HandleTolerance = 3.0f;
}

bool TexturingPanel::uvBounds(GeometryPanel& rGeometry_, glm::vec2& rMin_, glm::vec2& rMax_)
{
	EditMesh* pEdit = rGeometry_.current();
	if(pEdit == nullptr) return false;
	bool any = false;
	for(uint32_t face : rGeometry_.selectedFaces())
	{
		if(face >= pEdit->faces.size()) continue;
		for(uint32_t corner : pEdit->faces[face].v)
		{
			const glm::vec2& rUv = pEdit->vertices[corner].uv;
			rMin_ = any ? glm::min(rMin_, rUv) : rUv;
			rMax_ = any ? glm::max(rMax_, rUv) : rUv;
			any = true;
		}
	}
	return any;
}

void TexturingPanel::assign(GeometryPanel& rGeometry_, const std::string& rTexture_)
{
	EditMesh* pEdit = rGeometry_.current();
	BasicMesh* pMesh = nullptr;
	glm::mat4 toWorld;
	if(pEdit == nullptr || rGeometry_.selectedFaces().empty() || !rGeometry_.resolve(pMesh, toWorld))
	{
		this->message = "Pick faces first.";
		return;
	}
	if(!rTexture_.empty() && findFaceTexture(rTexture_) == nullptr)
	{
		this->message = std::format("There is no texture {}.", rTexture_);
		return;
	}
	pEdit->pushUndo();
	pEdit->setTexture(rGeometry_.selectedFaces(), rTexture_);
	pEdit->upload(*pMesh);
	this->slotTexture = rTexture_;
	this->message.clear();
}

void TexturingPanel::drawTab(GeometryPanel& rGeometry_)
{
	rGeometry_.drawSources();
	ImGui::TextWrapped("Left click level faces in the viewport to pick them (Shift toggles), left drag a box to pick the faces in it, right click or right drag a box to take them out (Alt + right drag looks around). Shift + Z undoes a texture or UV edit, Shift + Y does it again, Shift + S saves. In Set mode drag a texture from the Assets tab onto the texture window, or use Texture..., to give the picked faces that texture. In Edit mode a click picks the faces joined to it with its texture. Drag the box in the window to move the picked UVs, a corner to scale. Done ends the selection.");
	EditMesh* pEdit = rGeometry_.current();
	if(pEdit == nullptr)
	{
		ImGui::TextDisabled("No mesh open.");
	}
	else
	{
		ImGui::Text("%s, mesh %u: %zu faces%s", pEdit->level.c_str(), pEdit->ordinal, pEdit->faces.size(), pEdit->changed ? " (unsaved)" : "");
		ImGui::Text("Picked: %zu faces", rGeometry_.selectedFaces().size());
		if(ImGui::Button("Texture...")) ImGui::OpenPopup("TexturingPick");
		ImGui::SameLine();
		if(ImGui::Button("Own texture")) this->assign(rGeometry_, "");
		if(ImGui::BeginPopup("TexturingPick"))
		{
			for(int scope = 0; scope < 2; scope++)
			{
				ExternalPools& rPools = scope == 0 ? externalLevel() : externalProject;
				for(size_t i = 0; i < rPools.textures.allocatedSize(); i++)
				{
					if(!rPools.textures.alive[i]) continue;
					const std::string& rName = rPools.textures.names[i];
					if(scope == 1 && externalLevel().textures.nameToId.contains(rName)) continue;
					if(ImGui::Selectable(std::format("{}{}", rName, scope == 0 ? " (level)" : "").c_str())) this->assign(rGeometry_, rName);
				}
			}
			ImGui::EndPopup();
		}
		if(ImGui::Button("Undo (Shift+Z)")) rGeometry_.undo();
		ImGui::SameLine();
		if(ImGui::Button("Redo (Shift+Y)")) rGeometry_.redo();
	}
	ImGui::Separator();
	if(ImGui::Button("Save HubWorld.bin"))
	{
		try
		{
			rGeometry_.save();
			this->message = "Wrote HubWorld.bin.";
		}
		catch(const std::exception& rError)
		{
			this->message = std::format("HubWorld.bin not written: {}", rError.what());
		}
	}
	if(!this->message.empty()) ImGui::TextWrapped("%s", this->message.c_str());
}

void TexturingPanel::drawPanel(GeometryPanel& rGeometry_)
{
	ImVec2 display = ImGui::GetIO().DisplaySize;
	float left = editor.entityList.width;
	float top = display.y * ViewportFraction;
	float width = display.x - left - editor.inspector.width;
	float height = std::max(display.y - editor.prefabPanel.height - top, 120.0f);
	ImGui::SetNextWindowPos(ImVec2(left, top));
	ImGui::SetNextWindowSize(ImVec2(width, height));
	ImGui::Begin("Texture", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings);

	/* The modes at the left, Done at the right; a mode change drops the selection (Set and Edit pick differently). */
	int mode = this->editMode ? 1 : 0;
	ImGui::RadioButton("Set", &mode, 0);
	ImGui::SameLine();
	ImGui::RadioButton("Edit", &mode, 1);
	if((mode == 1) != this->editMode)
	{
		this->editMode = mode == 1;
		rGeometry_.clearSelection();
		this->drag = 0;
	}
	ImGui::SameLine(ImGui::GetWindowWidth() - ImGui::CalcTextSize("Done").x - ImGui::GetStyle().FramePadding.x * 2.0f - ImGui::GetStyle().WindowPadding.x);
	if(ImGui::Button(rGeometry_.isolate ? "Show all (I)" : "Isolate object (I)")) rGeometry_.toggleIsolate();
	ImGui::SameLine();
	if(ImGui::Button("Done"))
	{
		this->slotTexture.clear();
		rGeometry_.clearSelection();
		this->drag = 0;
	}

	EditMesh* pEdit = rGeometry_.current();
	BasicMesh* pMesh = nullptr;
	glm::mat4 toWorld;
	glm::vec2 boundsMin = glm::vec2(0.0f);
	glm::vec2 boundsMax = glm::vec2(1.0f);
	bool hasFaces = pEdit != nullptr && rGeometry_.resolve(pMesh, toWorld) && this->uvBounds(rGeometry_, boundsMin, boundsMax);
	if(!hasFaces) this->drag = 0;

	/* The dropped texture, else the texture of the first picked face. */
	GLuint textureId = 0;
	if(!this->slotTexture.empty())
	{
		if(Texture* pTexture = findFaceTexture(this->slotTexture)) textureId = pTexture->textureId;
	}
	else if(hasFaces)
	{
		uint32_t first = rGeometry_.selectedFaces()[0];
		if(first < pEdit->faces.size() && pEdit->faces[first].texture >= 0)
		{
			if(Texture* pTexture = findFaceTexture(pEdit->textures[pEdit->faces[first].texture])) textureId = pTexture->textureId;
		}
		else if(first < pEdit->faces.size() && !pEdit->ownTexture.empty())
		{
			if(Texture* pTexture = findFaceTexture(pEdit->ownTexture)) textureId = pTexture->textureId;
		}
	}
	static const std::vector<uint32_t> NoFaces;
	const std::vector<uint32_t>& rFaces = hasFaces ? rGeometry_.selectedFaces() : NoFaces;

	/* The view: the unit square and the picked UVs, with a margin; fixed while dragging. */
	if(this->drag == 0)
	{
		this->viewMin = glm::min(glm::vec2(0.0f), boundsMin);
		this->viewMax = glm::max(glm::vec2(1.0f), boundsMax);
		glm::vec2 margin = (this->viewMax - this->viewMin) * 0.05f;
		this->viewMin -= margin;
		this->viewMax += margin;
	}
	ImVec2 avail = ImGui::GetContentRegionAvail();
	ImVec2 origin = ImGui::GetCursorScreenPos();
	ImGui::InvisibleButton("TextureCanvas", ImVec2(std::max(avail.x, 1.0f), std::max(avail.y, 1.0f)));
	if(!this->editMode && ImGui::BeginDragDropTarget())
	{
		if(const ImGuiPayload* pPayload = ImGui::AcceptDragDropPayload("TEXTURE"))
		{
			std::string text(static_cast<const char*>(pPayload->Data));
			size_t tab = text.find('\t');
			if(text.substr(0, tab) == "external") this->assign(rGeometry_, text.substr(tab + 1));
			else
			{
				/* A disc tile is a whole set (a .GVM, a .TXD): the picker below lists its textures. */
				this->discSet = text.substr(tab + 1);
				ImGui::OpenPopup("TexturingDisc");
			}
		}
		/* One texture of a disc set, opened in the Assets tab: given at once. */
		if(const ImGuiPayload* pPayload = ImGui::AcceptDragDropPayload("TEXTURETILE")) this->assign(rGeometry_, std::string("disc:") + static_cast<const char*>(pPayload->Data));
		ImGui::EndDragDropTarget();
	}
	if(ImGui::BeginPopup("TexturingDisc"))
	{
		ImGui::TextUnformatted(this->discSet.c_str());
		std::vector<DiscTextureTile> tiles = discTextureTiles ? discTextureTiles(this->discSet) : std::vector<DiscTextureTile>();
		if(tiles.empty()) ImGui::TextDisabled("No textures could be read from it.");
		int column = 0;
		for(const DiscTextureTile& rTile : tiles)
		{
			if(column % 6 != 0) ImGui::SameLine();
			column++;
			ImGui::PushID(rTile.name.c_str());
			if(ImGui::ImageButton("##tile", static_cast<ImTextureID>(rTile.pTexture->textureId), ImVec2(64.0f, 64.0f)))
			{
				this->assign(rGeometry_, std::format("disc:{}#{}", this->discSet, rTile.name));
				ImGui::CloseCurrentPopup();
			}
			if(ImGui::IsItemHovered()) ImGui::SetTooltip("%s", rTile.name.c_str());
			ImGui::PopID();
		}
		ImGui::EndPopup();
	}
	/* An undo (the button or Shift + Z) ends a drag in progress. */
	if(this->seenUndone != rGeometry_.undone)
	{
		this->seenUndone = rGeometry_.undone;
		this->drag = 0;
	}
	bool pressed = ImGui::IsItemActivated();
	bool held = ImGui::IsItemActive();

	glm::vec2 range = this->viewMax - this->viewMin;
	float scale = std::min(avail.x / range.x, avail.y / range.y);
	glm::vec2 offset = glm::vec2(origin.x, origin.y) + (glm::vec2(avail.x, avail.y) - range * scale) * 0.5f - this->viewMin * scale;
	auto toScreen = [&](const glm::vec2& rUv_) { glm::vec2 p = offset + rUv_ * scale; return ImVec2(p.x, p.y); };

	ImDrawList* pDraw = ImGui::GetWindowDrawList();
	pDraw->AddRectFilled(origin, ImVec2(origin.x + avail.x, origin.y + avail.y), Background);
	if(textureId != 0) pDraw->AddImage(static_cast<ImTextureID>(textureId), toScreen(glm::vec2(0.0f)), toScreen(glm::vec2(1.0f)));
	else pDraw->AddRect(toScreen(glm::vec2(0.0f)), toScreen(glm::vec2(1.0f)), IM_COL32(120, 120, 130, 255));
	for(uint32_t face : rFaces)
	{
		if(face >= pEdit->faces.size()) continue;
		ImVec2 points[3];
		for(int i = 0; i < 3; i++) points[i] = toScreen(pEdit->vertices[pEdit->faces[face].v[i]].uv);

		/* AddConvexPolyFilled and AddPolyline want one winding. */
		float area = (points[1].x - points[0].x) * (points[2].y - points[0].y) - (points[1].y - points[0].y) * (points[2].x - points[0].x);
		if(area < 0.0f) std::swap(points[1], points[2]);
		pDraw->AddConvexPolyFilled(points, 3, FaceFill);
		pDraw->AddPolyline(points, 3, FaceOutline, ImDrawFlags_Closed, 1.5f);
	}

	if(!hasFaces)
	{
		pDraw->AddText(ImVec2(origin.x + 8.0f, origin.y + 8.0f), IM_COL32(150, 150, 160, 255), "Pick faces in the viewport (left click picks, Shift toggles, left drag a box, right click or right drag takes out).");
		ImGui::End();
		return;
	}

	/* The box of the picked UVs and the handle on each corner. */
	ImVec2 corners[4] = {toScreen(boundsMin), toScreen(glm::vec2(boundsMax.x, boundsMin.y)), toScreen(boundsMax), toScreen(glm::vec2(boundsMin.x, boundsMax.y))};
	pDraw->AddPolyline(corners, 4, BoxColor, ImDrawFlags_Closed, 1.0f);
	for(const ImVec2& rCorner : corners) pDraw->AddCircleFilled(rCorner, HandleRadius, BoxColor);

	ImVec2 mouseScreen = ImGui::GetIO().MousePos;
	glm::vec2 mouse(mouseScreen.x, mouseScreen.y);
	if(pressed)
	{
		/* A corner (with a little tolerance) first, then the inside of the box. */
		int hit = 0;
		for(const ImVec2& rCorner : corners)
		{
			if(glm::distance(mouse, glm::vec2(rCorner.x, rCorner.y)) <= HandleRadius + HandleTolerance) hit = 2;
		}
		if(hit == 0 && mouseScreen.x >= corners[0].x && mouseScreen.x <= corners[2].x && mouseScreen.y >= corners[0].y && mouseScreen.y <= corners[2].y) hit = 1;
		if(hit != 0)
		{
			pEdit->pushUndo();
			this->dragVertices = pEdit->isolate(rFaces);
			this->dragStart.clear();
			for(uint32_t vertex : this->dragVertices) this->dragStart.push_back(pEdit->vertices[vertex].uv);
			glm::vec2 min = this->dragStart[0];
			glm::vec2 max = this->dragStart[0];
			for(const glm::vec2& rUv : this->dragStart)
			{
				min = glm::min(min, rUv);
				max = glm::max(max, rUv);
			}
			this->dragPivot = (min + max) * 0.5f;
			this->dragMouse = mouse;
			ImVec2 pivotScreen = toScreen(this->dragPivot);
			this->dragDistance = std::max(glm::distance(mouse, glm::vec2(pivotScreen.x, pivotScreen.y)), 1.0f);
			this->drag = hit;
		}
	}
	if(this->drag != 0 && held)
	{
		if(this->drag == 1)
		{
			glm::vec2 delta = (mouse - this->dragMouse) / scale;
			for(size_t i = 0; i < this->dragVertices.size(); i++) pEdit->vertices[this->dragVertices[i]].uv = this->dragStart[i] + delta;
		}
		else
		{
			ImVec2 pivotScreen = toScreen(this->dragPivot);
			float factor = std::max(glm::distance(mouse, glm::vec2(pivotScreen.x, pivotScreen.y)) / this->dragDistance, 0.01f);
			for(size_t i = 0; i < this->dragVertices.size(); i++) pEdit->vertices[this->dragVertices[i]].uv = this->dragPivot + (this->dragStart[i] - this->dragPivot) * factor;
		}
		pEdit->changed = true;
		pEdit->upload(*pMesh);
	}
	if(!held) this->drag = 0;
	ImGui::End();
}
}
#endif
