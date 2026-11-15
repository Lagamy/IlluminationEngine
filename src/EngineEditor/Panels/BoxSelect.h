#pragma once

#ifdef EDITOR
#include "imgui.h"
#include <algorithm>
#include <array>
#include <glm/glm.hpp>

namespace Graphics
{
/* What the box selection (a right drag) of GeometryPanel and CollisionPanel shares. */

/* A position as a map / set key: vertices at exactly one position are one corner (EditMesh::weld, CollisionEdit::weld). */
using PositionKey = std::array<float, 3>;

inline PositionKey positionKey(const glm::vec3& rPosition_)
{
	return {rPosition_.x, rPosition_.y, rPosition_.z};
}

/* The rectangle two window points span, whichever corner each is. */
struct ScreenBox
{
	ImVec2 low;
	ImVec2 high;

	ScreenBox(const ImVec2& rA_, const ImVec2& rB_) : low(std::min(rA_.x, rB_.x), std::min(rA_.y, rB_.y)), high(std::max(rA_.x, rB_.x), std::max(rA_.y, rB_.y)) {}

	bool contains(const ImVec2& rPoint_) const
	{
		return rPoint_.x >= this->low.x && rPoint_.x <= this->high.x && rPoint_.y >= this->low.y && rPoint_.y <= this->high.y;
	}
};

constexpr ImU32 BoxFill = IM_COL32(255, 200, 40, 40);
constexpr ImU32 BoxOutline = IM_COL32(255, 200, 40, 220);
}
#endif
