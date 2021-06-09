#include "gui.hpp"
#include "imgui.h"

void tooltiipText(std::string txt, float padding_x /*= 4.0f*/, float padding_y /*= 2.0f*/)
{
	ImVec2 sz = ImGui::CalcTextSize(txt.c_str());
	ImVec2 cursor = ImGui::GetCursorPos();
	ImGui::InvisibleButton("##padded-text", ImVec2(sz.x + padding_x * 2, sz.y + padding_y * 2));    // ImVec2 operators require imgui_internal.h include and -DIMGUI_DEFINE_MATH_OPERATORS=1
	ImVec2 final_cursor_pos = ImGui::GetCursorPos();
	ImGui::SetCursorPos(ImVec2(cursor.x + padding_x, cursor.y + padding_y));
	ImGui::Text(txt.c_str());
	ImGui::SetCursorPos(final_cursor_pos);
}
