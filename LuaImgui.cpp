#include "LuaImgui.h"

int LuaImguiSetNextWindowPos(lua_State* L) {

	LuaImgui* imgui = lua_toimgui(L, 1);

	if (!imgui->isInRender) {
		luaL_error(L, "Draw functions can only be called inside renderer");
		return 0;
	}

	ImVec2 pos = lua_toimvec2(L, 2);

	ImGui::SetNextWindowPos(pos);

	return 0;
}

int LuaImguiSetNextItemWidth(lua_State* L) {

	LuaImgui* imgui = lua_toimgui(L, 1);

	if (!imgui->isInRender) {
		luaL_error(L, "Draw functions can only be called inside renderer");
		return 0;
	}

	ImGui::SetNextItemWidth(luaL_checknumber(L, 2));

	return 0;
}

float FloatGetter(void* data, int idx) {

	lua_State* L = (lua_State*)data;

	lua_pushinteger(L, idx + 1);
	lua_gettable(L, -2);
	float numb = lua_tonumber(L, -1);
	lua_pop(L, 1);

	return numb;
}

int LuaImguiPlotLines(lua_State* L) {

	LuaImgui* imgui = lua_toimgui(L, 1);
	const char* label = luaL_checkstring(L, 2);
	const char* tag = lua_tostring(L, 4);

	if (!imgui->isInRender) {
		luaL_error(L, "Draw functions can only be called inside renderer");
		return 0;
	}

	int len = 0;

	if (lua_type(L, 3) == LUA_TTABLE) {

		lua_len(L, 3);
		len = lua_tointeger(L, -1);
		lua_pop(L, 1);
	}

	const char* overlay = NULL;

	if (tag) {
		ImguiElement* element = GetElement(imgui, tag, IMGUI_TYPE_STRING);
		if (!element) {
			element = AddElement(imgui, tag, IMGUI_TYPE_STRING);
			if (!element) {
				luaL_error(L, "Imgui Out of memory");
				return 0;
			}
		}

		overlay = (const char*)element->Data;
	}

	lua_pushvalue(L, 3);
	ImGui::PlotLines(label, &FloatGetter, L, len, 0, overlay);

	return 0;
}

int LuaImguiTableSetColumnIndex(lua_State* L) {

	LuaImgui* imgui = lua_toimgui(L, 1);

	if (!imgui->isInRender) {
		luaL_error(L, "Draw functions can only be called inside renderer");
		return 0;
	}

	lua_pushboolean(L, ImGui::TableSetColumnIndex(luaL_checkinteger(L, 2)) == true);

	return 1;
}

int LuaImguiTableNextRow(lua_State* L) {

	LuaImgui* imgui = lua_toimgui(L, 1);

	if (!imgui->isInRender) {
		luaL_error(L, "Draw functions can only be called inside renderer");
		return 0;
	}

	ImGui::TableNextRow(luaL_optinteger(L, 2, 0), luaL_optnumber(L, 3, 0.0));

	return 0;
}

int LuaImguiTreePop(lua_State* L) {

	LuaImgui* imgui = lua_toimgui(L, 1);

	if (!imgui->isInRender) {
		luaL_error(L, "Draw functions can only be called inside renderer");
		return 0;
	}

	ImGui::TreePop();

	return 0;
}

int LuaImguiTreeNode(lua_State* L) {

	LuaImgui* imgui = lua_toimgui(L, 1);

	if (!imgui->isInRender) {
		luaL_error(L, "Draw functions can only be called inside renderer");
		return 0;
	}

	lua_pushboolean(L, ImGui::TreeNode(luaL_checkstring(L, 2)) == true);
	return 1;
}

int LuaImguiSetNextItemOpen(lua_State* L) {

	LuaImgui* imgui = lua_toimgui(L, 1);

	if (!imgui->isInRender) {
		luaL_error(L, "Draw functions can only be called inside renderer");
		return 0;
	}

	ImGui::SetNextItemOpen(lua_toboolean(L, 2) != 0, luaL_optinteger(L, 3, 0));

	return 0;
}

int LuaImguiPopStyleVar(lua_State* L) {

	LuaImgui* imgui = lua_toimgui(L, 1);

	if (!imgui->isInRender) {
		luaL_error(L, "Draw functions can only be called inside renderer");
		return 0;
	}

	ImGui::PopStyleVar(luaL_optinteger(L, 2, 1));

	return 0;
}

int LuaImguiPushStyleVar(lua_State* L) {

	LuaImgui* imgui = lua_toimgui(L, 1);

	if (!imgui->isInRender) {
		luaL_error(L, "Draw functions can only be called inside renderer");
		return 0;
	}

	if (lua_type(L, 3) == LUA_TTABLE) {
		ImGui::PushStyleVar(luaL_checkinteger(L, 2), lua_toimvec2(L, 3));
	}
	else {
		ImGui::PushStyleVar(luaL_checkinteger(L, 2), luaL_checknumber(L, 3));
	}

	return 0;
}

int LuaImguiGetTextLineHeightWithSpacing(lua_State* L) {

	LuaImgui* imgui = lua_toimgui(L, 1);

	if (!imgui->isInRender) {
		luaL_error(L, "Draw functions can only be called inside renderer");
		return 0;
	}

	lua_pushnumber(L, ImGui::GetTextLineHeightWithSpacing());

	return 1;
}

int LuaImguiGetCursorStartPos(lua_State* L) {

	LuaImgui* imgui = lua_toimgui(L, 1);

	if (!imgui->isInRender) {
		luaL_error(L, "Draw functions can only be called inside renderer");
		return 0;
	}

	ImVec2 size = ImGui::GetCursorStartPos();
	lua_pushimvec2(L, size);
	return 1;
}

int LuaImguiGetCursorPos(lua_State* L) {

	LuaImgui* imgui = lua_toimgui(L, 1);

	if (!imgui->isInRender) {
		luaL_error(L, "Draw functions can only be called inside renderer");
		return 0;
	}

	ImVec2 size = ImGui::GetCursorPos();
	lua_pushimvec2(L, size);
	return 1;
}

int LuaImguiCalcTextSize(lua_State* L) {

	LuaImgui* imgui = lua_toimgui(L, 1);
	size_t len;
	const char* str = luaL_checklstring(L, 2, &len);

	ImVec2 size = ImGui::CalcTextSize(str, str + len, lua_toboolean(L, 3) != 0, luaL_optnumber(L, 4, -1.0));
	lua_pushimvec2(L, size);
	return 1;
}

int LuaImguiEndDisabled(lua_State* L) {

	LuaImgui* imgui = lua_toimgui(L, 1);

	if (!imgui->isInRender) {
		luaL_error(L, "Draw functions can only be called inside renderer");
		return 0;
	}

	ImGui::EndDisabled();

	return 0;
}

int LuaImguiBeginDisabled(lua_State* L) {

	LuaImgui* imgui = lua_toimgui(L, 1);

	if (!imgui->isInRender) {
		luaL_error(L, "Draw functions can only be called inside renderer");
		return 0;
	}

	ImGui::BeginDisabled();

	return 0;
}

int LuaImguiUnindent(lua_State* L) {

	LuaImgui* imgui = lua_toimgui(L, 1);

	if (!imgui->isInRender) {
		luaL_error(L, "Draw functions can only be called inside renderer");
		return 0;
	}

	ImGui::Unindent(luaL_optnumber(L, 2, 0.0));

	return 0;
}

int LuaImguiIndent(lua_State* L) {

	LuaImgui* imgui = lua_toimgui(L, 1);

	if (!imgui->isInRender) {
		luaL_error(L, "Draw functions can only be called inside renderer");
		return 0;
	}

	ImGui::Indent(luaL_optnumber(L, 2, 0.0));

	return 0;
}

int LuaImguiEndMainMenuBar(lua_State* L) {

	LuaImgui* imgui = lua_toimgui(L, 1);

	if (!imgui->isInRender) {
		luaL_error(L, "Draw functions can only be called inside renderer");
		return 0;
	}

	ImGui::EndMainMenuBar();

	return 0;
}

int LuaImguiBeginMainMenuBar(lua_State* L) {

	LuaImgui* imgui = lua_toimgui(L, 1);

	if (!imgui->isInRender) {
		luaL_error(L, "Draw functions can only be called inside renderer");
		return 0;
	}

	lua_pushboolean(L, ImGui::BeginMainMenuBar() == true);

	return 1;
}

int LuaImguiTableNextColumn(lua_State* L) {

	LuaImgui* imgui = lua_toimgui(L, 1);

	if (!imgui->isInRender) {
		luaL_error(L, "Draw functions can only be called inside renderer");
		return 0;
	}

	lua_pushboolean(L, ImGui::TableNextColumn() == true);

	return 1;
}

int LuaImguiEndTable(lua_State* L) {

	LuaImgui* imgui = lua_toimgui(L, 1);

	if (!imgui->isInRender) {
		luaL_error(L, "Draw functions can only be called inside renderer");
		return 0;
	}

	ImGui::EndTable();

	return 0;
}

int LuaImguiBeginTable(lua_State* L) {

	LuaImgui* imgui = lua_toimgui(L, 1);
	const char* label = luaL_checkstring(L, 2);
	int columns = luaL_checkinteger(L, 3);

	if (!imgui->isInRender) {
		luaL_error(L, "Draw functions can only be called inside renderer");
		return 0;
	}

	bool result = ImGui::BeginTable(label, columns, luaL_optinteger(L, 4, 0), lua_toimvec2(L, 5), luaL_optnumber(L, 6, 0.0));

	lua_pushboolean(L, result == true);
	return 1;
}

int LuaImguiCollapsingHeader(lua_State* L) {

	LuaImgui* imgui = lua_toimgui(L, 1);
	const char* label = luaL_checkstring(L, 2);
	const char* tag = lua_tostring(L, 3);

	if (!imgui->isInRender) {
		luaL_error(L, "Draw functions can only be called inside renderer");
		return 0;
	}

	bool* open = NULL;

	if (tag) {
		ImguiElement* element = GetElement(imgui, tag, IMGUI_TYPE_BOOL);
		if (!element) {
			element = AddElement(imgui, tag, IMGUI_TYPE_BOOL);
			if (!element) {
				luaL_error(L, "Imgui Out of memory");
				return 0;
			}
		}

		open = (bool*)element->Data;
	}

	lua_pushboolean(L, ImGui::CollapsingHeader(label, open, luaL_optinteger(L, 4, 0)) == true);

	return 1;
}

int LuaImguiProgressBar(lua_State* L) {

	LuaImgui* imgui = lua_toimgui(L, 1);
	float fraction = lua_tonumber(L, 2);
	const char* tag = lua_tostring(L, 4);

	if (!imgui->isInRender) {
		luaL_error(L, "Draw functions can only be called inside renderer");
		return 0;
	}

	ImVec2 size = ImVec2(-FLT_MIN, 0.0f);

	if (lua_type(L, 3) == LUA_TTABLE) {
		size = lua_toimvec2(L, 3);
	}

	const char* overlay = NULL;

	if (tag) {
		ImguiElement* element = GetElement(imgui, tag, IMGUI_TYPE_STRING);
		if (!element) {
			element = AddElement(imgui, tag, IMGUI_TYPE_STRING);
			if (!element) {
				luaL_error(L, "Imgui Out of memory");
				return 0;
			}
		}

		overlay = (const char*)element->Data;
	}

	ImGui::ProgressBar(fraction, size, overlay);

	return 0;
}

int LuaImguiGetTextLineHeight(lua_State* L) {

	LuaImgui* imgui = lua_toimgui(L, 1);

	if (!imgui->isInRender) {
		luaL_error(L, "Draw functions can only be called inside renderer");
		return 0;
	}

	lua_pushnumber(L, ImGui::GetTextLineHeight());

	return 1;
}

int LuaImguiNextWindowContentSize(lua_State* L) {

	LuaImgui* imgui = lua_toimgui(L, 1);

	if (!imgui->isInRender) {
		luaL_error(L, "Draw functions can only be called inside renderer");
		return 0;
	}

	ImGui::SetNextWindowContentSize(lua_toimvec2(L, 2));

	return 0;
}

int LuaImguiGetWindowSize(lua_State* L) {

	LuaImgui* imgui = lua_toimgui(L, 1);

	if (!imgui->isInRender) {
		luaL_error(L, "Draw functions can only be called inside renderer");
		return 0;
	}

	lua_pushimvec2(L, ImGui::GetWindowSize());

	return 1;
}

int LuaImguiInputTextMultiline(lua_State* L) {

	LuaImgui* imgui = lua_toimgui(L, 1);
	const char* label = luaL_checkstring(L, 2);
	const char* tag = luaL_checkstring(L, 3);

	if (!imgui->isInRender) {
		luaL_error(L, "Draw functions can only be called inside renderer");
		return 0;
	}

	ImguiElement* element = GetElement(imgui, tag, IMGUI_TYPE_STRING);
	if (!element) {
		element = AddElement(imgui, tag, IMGUI_TYPE_STRING);
		if (!element) {
			luaL_error(L, "Imgui Out of memory");
			return 0;
		}
	}

	ImVec2 size = lua_toimvec2(L, 4);
	int flags = luaL_optinteger(L, 5, 0);

	flags |= ImGuiInputTextFlags_CallbackResize;

	bool result = ImGui::InputTextMultiline(label, (char*)element->Data, element->Size, size, flags, &LuaImGuiInputTextCallback, element);

	lua_pushboolean(L, result == true);

	return 1;
}

int LuaImguiListBox(lua_State* L) {

	LuaImgui* imgui = lua_toimgui(L, 1);
	const char* label = luaL_checkstring(L, 2);
	const char* tag = luaL_checkstring(L, 3);

	if (!imgui->isInRender) {
		luaL_error(L, "Draw functions can only be called inside renderer");
		return 0;
	}

	ImguiElement* element = GetElement(imgui, tag, IMGUI_TYPE_INT);
	if (!element) {
		element = AddElement(imgui, tag, IMGUI_TYPE_INT);
		if (!element) {
			luaL_error(L, "Imgui Out of memory");
			return 0;
		}
	}

	int* f = (int*)element->Data;

	LoadTableIntoStringArray(L, imgui, 4);

	lua_pushboolean(L, ImGui::ListBox(label, f, imgui->stringArray, imgui->stringArrayLen, luaL_optinteger(L, 5, -1)));

	return 1;
}

int LuaImguiSliderInt(lua_State* L) {

	LuaImgui* imgui = lua_toimgui(L, 1);
	const char* label = luaL_checkstring(L, 2);
	const char* tag = luaL_checkstring(L, 3);

	if (!imgui->isInRender) {
		luaL_error(L, "Draw functions can only be called inside renderer");
		return 0;
	}

	ImguiElement* element = GetElement(imgui, tag, IMGUI_TYPE_INT);
	if (!element) {
		element = AddElement(imgui, tag, IMGUI_TYPE_INT);
		if (!element) {
			luaL_error(L, "Imgui Out of memory");
			return 0;
		}
	}

	lua_pushboolean(L, ImGui::SliderInt(label, (int*)element->Data, luaL_checkinteger(L, 4), luaL_checkinteger(L, 5), luaL_optstring(L, 6, "%d"), luaL_optinteger(L, 7, 0)) == true);

	return 1;
}

int LuaImguiInputDouble(lua_State* L) {

	LuaImgui* imgui = lua_toimgui(L, 1);
	const char* label = luaL_checkstring(L, 2);
	const char* tag = luaL_checkstring(L, 3);

	if (!imgui->isInRender) {
		luaL_error(L, "Draw functions can only be called inside renderer");
		return 0;
	}

	ImguiElement* element = GetElement(imgui, tag, IMGUI_TYPE_DOUBLE);
	if (!element) {
		element = AddElement(imgui, tag, IMGUI_TYPE_DOUBLE);
		if (!element) {
			luaL_error(L, "Imgui Out of memory");
			return 0;
		}
	}

	lua_pushboolean(L, ImGui::InputDouble(label, (double*)element->Data, luaL_optnumber(L, 4, 0), luaL_optnumber(L, 5, 0), luaL_optstring(L, 6, "%.3f"), luaL_optinteger(L, 7, 0)) == true);

	return 1;
}

int LuaImguiInputFloat(lua_State* L) {

	LuaImgui* imgui = lua_toimgui(L, 1);
	const char* label = luaL_checkstring(L, 2);
	const char* tag = luaL_checkstring(L, 3);

	if (!imgui->isInRender) {
		luaL_error(L, "Draw functions can only be called inside renderer");
		return 0;
	}

	ImguiElement* element = GetElement(imgui, tag, IMGUI_TYPE_FLOAT);
	if (!element) {
		element = AddElement(imgui, tag, IMGUI_TYPE_FLOAT);
		if (!element) {
			luaL_error(L, "Imgui Out of memory");
			return 0;
		}
	}

	lua_pushboolean(L, ImGui::InputFloat(label, (float*)element->Data, luaL_optnumber(L, 4, 0), luaL_optnumber(L, 5, 0), luaL_optstring(L, 6, "%.3f"), luaL_optinteger(L, 7, 0)) == true);

	return 1;
}

int LuaImguiInputInt(lua_State* L) {

	LuaImgui* imgui = lua_toimgui(L, 1);
	const char* label = luaL_checkstring(L, 2);
	const char* tag = luaL_checkstring(L, 3);

	if (!imgui->isInRender) {
		luaL_error(L, "Draw functions can only be called inside renderer");
		return 0;
	}

	ImguiElement* element = GetElement(imgui, tag, IMGUI_TYPE_INT);
	if (!element) {
		element = AddElement(imgui, tag, IMGUI_TYPE_INT);
		if (!element) {
			luaL_error(L, "Imgui Out of memory");
			return 0;
		}
	}

	lua_pushboolean(L, ImGui::InputInt(label, (int*)element->Data, luaL_optinteger(L, 4, 1), luaL_optinteger(L, 5, 100), luaL_optinteger(L, 6, 0)) == true);

	return 1;
}

int LuaImguiInputText(lua_State* L) {

	LuaImgui* imgui = lua_toimgui(L, 1);
	const char* label = luaL_checkstring(L, 2);
	const char* tag = luaL_checkstring(L, 3);
	const char* hint = lua_tostring(L, 4);

	if (!imgui->isInRender) {
		luaL_error(L, "Draw functions can only be called inside renderer");
		return 0;
	}

	ImguiElement* element = GetElement(imgui, tag, IMGUI_TYPE_STRING);
	if (!element) {
		element = AddElement(imgui, tag, IMGUI_TYPE_STRING);
		if (!element) {
			luaL_error(L, "Imgui Out of memory");
			return 0;
		}
	}

	int flags = luaL_optinteger(L, 5, 0);

	flags |= ImGuiInputTextFlags_CallbackResize;

	if (hint) {
		lua_pushboolean(L, ImGui::InputTextWithHint(label, hint, (char*)element->Data, element->Size, flags, &LuaImGuiInputTextCallback, element) == true);
	}
	else {
		lua_pushboolean(L, ImGui::InputText(label, (char*)element->Data, element->Size, flags, &LuaImGuiInputTextCallback, element) == true);
	}

	return 1;
}

int LuaImguiHelpMarker(lua_State* L) {

	LuaImgui* imgui = lua_toimgui(L, 1);
	const char* text = luaL_checkstring(L, 2);

	if (!imgui->isInRender) {
		luaL_error(L, "Draw functions can only be called inside renderer");
		return 0;
	}

	ImGui::TextDisabled("(?)");
	if (ImGui::IsItemHovered())
	{
		ImGui::BeginTooltip();
		ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
		ImGui::TextUnformatted(text);
		ImGui::PopTextWrapPos();
		ImGui::EndTooltip();
	}

	return 0;
}

int LuaImguiCombo(lua_State* L) {

	LuaImgui* imgui = lua_toimgui(L, 1);
	const char* label = luaL_checkstring(L, 2);
	const char* tag = luaL_checkstring(L, 3);

	if (!imgui->isInRender) {
		luaL_error(L, "Draw functions can only be called inside renderer");
		return 0;
	}

	ImguiElement* element = GetElement(imgui, tag, IMGUI_TYPE_INT);
	if (!element) {
		element = AddElement(imgui, tag, IMGUI_TYPE_INT);
		if (!element) {
			luaL_error(L, "Imgui Out of memory");
			return 0;
		}
	}

	int* f = (int*)element->Data;

	LoadTableIntoStringArray(L, imgui, 4);

	lua_pushboolean(L, ImGui::Combo(label, f, imgui->stringArray, imgui->stringArrayLen, luaL_optinteger(L, 5, -1)));

	return 1;
}

int LuaImguiLabelText(lua_State* L) {

	LuaImgui* imgui = lua_toimgui(L, 1);

	if (!imgui->isInRender) {
		luaL_error(L, "Draw functions can only be called inside renderer");
		return 0;
	}

	ImGui::LabelText(luaL_checkstring(L, 2), "%s", luaL_checkstring(L, 3));

	return 0;
}

int LuaImguiBeginTooltip(lua_State* L) {

	LuaImgui* imgui = lua_toimgui(L, 1);

	if (!imgui->isInRender) {
		luaL_error(L, "Draw functions can only be called inside renderer");
		return 0;
	}

	ImGui::BeginTooltip();

	return 0;
}

int LuaImguiEndTooltip(lua_State* L) {

	LuaImgui* imgui = lua_toimgui(L, 1);

	if (!imgui->isInRender) {
		luaL_error(L, "Draw functions can only be called inside renderer");
		return 0;
	}

	ImGui::EndTooltip();

	return 0;
}

int LuaImguiIsItemHovered(lua_State* L) {

	LuaImgui* imgui = lua_toimgui(L, 1);

	if (!imgui->isInRender) {
		luaL_error(L, "Draw functions can only be called inside renderer");
		return 0;
	}

	lua_pushboolean(L, ImGui::IsItemHovered(lua_tointeger(L, 2)) == true);

	return 1;
}

int LuaImguiArrowButton(lua_State* L) {

	LuaImgui* imgui = lua_toimgui(L, 1);

	if (!imgui->isInRender) {
		luaL_error(L, "Draw functions can only be called inside renderer");
		return 0;
	}

	lua_pushboolean(L, ImGui::ArrowButton(luaL_checkstring(L, 2), luaL_checkinteger(L, 3)));

	return 1;
}

int LuaImguiPopButtonRepeat(lua_State* L) {

	LuaImgui* imgui = lua_toimgui(L, 1);

	if (!imgui->isInRender) {
		luaL_error(L, "Draw functions can only be called inside renderer");
		return 0;
	}

	ImGui::PopButtonRepeat();

	return 0;
}

int LuaImguiPushButtonRepeat(lua_State* L) {

	LuaImgui* imgui = lua_toimgui(L, 1);

	if (!imgui->isInRender) {
		luaL_error(L, "Draw functions can only be called inside renderer");
		return 0;
	}

	ImGui::PushButtonRepeat(lua_toboolean(L, 2) != 0);

	return 0;
}

int LuaImguiAlignTextToFramePadding(lua_State* L) {

	LuaImgui* imgui = lua_toimgui(L, 1);

	if (!imgui->isInRender) {
		luaL_error(L, "Draw functions can only be called inside renderer");
		return 0;
	}

	ImGui::AlignTextToFramePadding();

	return 0;
}

int LuaImguiPopStyleColor(lua_State* L) {

	LuaImgui* imgui = lua_toimgui(L, 1);

	if (!imgui->isInRender) {
		luaL_error(L, "Draw functions can only be called inside renderer");
		return 0;
	}

	ImGui::PopStyleColor();

	return 0;
}

int LuaImguiPushStyleColor(lua_State* L) {

	LuaImgui* imgui = lua_toimgui(L, 1);

	if (!imgui->isInRender) {
		luaL_error(L, "Draw functions can only be called inside renderer");
		return 0;
	}

	luaL_checktype(L, 3, LUA_TTABLE);
	ImGui::PushStyleColor(luaL_checkinteger(L, 2), lua_toimvec4(L, 3));

	return 0;
}

int LuaImguiPopId(lua_State* L) {

	LuaImgui* imgui = lua_toimgui(L, 1);

	if (!imgui->isInRender) {
		luaL_error(L, "Draw functions can only be called inside renderer");
		return 0;
	}

	ImGui::PopID();

	return 0;
}

int LuaImguiPushId(lua_State* L) {

	LuaImgui* imgui = lua_toimgui(L, 1);

	if (!imgui->isInRender) {
		luaL_error(L, "Draw functions can only be called inside renderer");
		return 0;
	}

	if (lua_type(L, 2) == LUA_TNUMBER) {
		ImGui::PushID(lua_tointeger(L, 2));
	}
	else if (lua_type(L, 2) == LUA_TSTRING) {
		ImGui::PushID(lua_tostring(L, 2));
	}
	else {
		luaL_error(L, "Id must be an int or a string");
	}

	return 0;
}

int LuaImguiRadioButton(lua_State* L) {

	LuaImgui* imgui = lua_toimgui(L, 1);
	const char* title = luaL_checkstring(L, 2);
	const char* tag = luaL_checkstring(L, 3);
	int id = lua_tointeger(L, 4);

	if (!imgui->isInRender) {
		luaL_error(L, "Draw functions can only be called inside renderer");
		return 0;
	}

	ImguiElement* element = GetElement(imgui, tag, IMGUI_TYPE_INT);
	if (!element) {
		element = AddElement(imgui, tag, IMGUI_TYPE_INT);
		if (!element) {
			luaL_error(L, "Imgui Out of memory");
			return 0;
		}
	}

	int* f = (int*)element->Data;

	lua_pushboolean(L, ImGui::RadioButton(title, f, id) == true);

	return 1;
}

int LuaImguiTextColored(lua_State* L) {

	LuaImgui* imgui = lua_toimgui(L, 1);
	luaL_checktype(L, 2, LUA_TTABLE);
	const char* text = luaL_checkstring(L, 3);

	if (!imgui->isInRender) {
		luaL_error(L, "Draw functions can only be called inside renderer");
		return 0;
	}

	ImGui::TextColored(lua_toimvec4(L, 2), "%s", text);

	return 0;
}

int LuaImguiTextWrapped(lua_State* L) {

	LuaImgui* imgui = lua_toimgui(L, 1);

	if (!imgui->isInRender) {
		luaL_error(L, "Draw functions can only be called inside renderer");
		return 0;
	}

	ImGui::TextWrapped("%s", luaL_checkstring(L, 2));

	return 0;
}

int LuaImguiEndTabBarItem(lua_State* L) {

	LuaImgui* imgui = lua_toimgui(L, 1);

	if (!imgui->isInRender) {
		luaL_error(L, "Draw functions can only be called inside renderer");
		return 0;
	}

	ImGui::EndTabItem();

	return 0;
}

int LuaImguiBeginTabItem(lua_State* L) {

	LuaImgui* imgui = lua_toimgui(L, 1);
	const char* label = luaL_checkstring(L, 2);
	const char* tag = lua_tostring(L, 3);

	if (!imgui->isInRender) {
		luaL_error(L, "Draw functions can only be called inside renderer");
		return 0;
	}

	bool* check = NULL;

	if (tag) {
		ImguiElement* element = GetElement(imgui, tag, IMGUI_TYPE_BOOL);
		if (!element) {
			element = AddElement(imgui, tag, IMGUI_TYPE_BOOL);
			if (!element) {
				luaL_error(L, "Imgui Out of memory");
				return 0;
			}

			check = (bool*)element->Data;
		}
	}

	lua_pushboolean(L, ImGui::BeginTabItem(label, check, lua_tointeger(L, 4)));

	return 1;
}

int LuaImguiEndTabBar(lua_State* L) {

	LuaImgui* imgui = lua_toimgui(L, 1);

	if (!imgui->isInRender) {
		luaL_error(L, "Draw functions can only be called inside renderer");
		return 0;
	}

	ImGui::EndTabBar();

	return 0;
}

int LuaImguiBeginTabBar(lua_State* L) {

	LuaImgui* imgui = lua_toimgui(L, 1);

	if (!imgui->isInRender) {
		luaL_error(L, "Draw functions can only be called inside renderer");
		return 0;
	}

	lua_pushboolean(L, ImGui::BeginTabBar(luaL_checkstring(L, 2), lua_tointeger(L, 3)));

	return 1;
}

int LuaImguiGetFrameHeightWithSpacing(lua_State* L) {

	LuaImgui* imgui = lua_toimgui(L, 1);

	if (!imgui->isInRender) {
		luaL_error(L, "Draw functions can only be called inside renderer");
		return 0;
	}

	lua_pushnumber(L, ImGui::GetFrameHeightWithSpacing());
	return 1;
}

int LuaImguiEndGroup(lua_State* L) {

	LuaImgui* imgui = lua_toimgui(L, 1);

	if (!imgui->isInRender) {
		luaL_error(L, "Draw functions can only be called inside renderer");
		return 0;
	}

	ImGui::EndGroup();
	return 0;
}

int LuaImguiBeginGroup(lua_State* L) {

	LuaImgui* imgui = lua_toimgui(L, 1);

	if (!imgui->isInRender) {
		luaL_error(L, "Draw functions can only be called inside renderer");
		return 0;
	}

	ImGui::BeginGroup();
	return 0;
}

int LuaImguiEndChild(lua_State* L) {

	LuaImgui* imgui = lua_toimgui(L, 1);

	if (!imgui->isInRender) {
		luaL_error(L, "Draw functions can only be called inside renderer");
		return 0;
	}

	ImGui::EndChild();

	return 0;
}

int LuaImguiBeginChild(lua_State* L) {

	LuaImgui* imgui = lua_toimgui(L, 1);
	const char* title = luaL_checkstring(L, 2);
	ImVec2 size = ImVec2(lua_tonumber(L, 3), lua_tonumber(L, 4));

	if (!imgui->isInRender) {
		luaL_error(L, "Draw functions can only be called inside renderer");
		return 0;
	}

	lua_pushboolean(L, ImGui::BeginChild(title, size, lua_toboolean(L, 5) == 0) == true);
	return 1;
}

int LuaImguiMenuItem(lua_State* L) {

	LuaImgui* imgui = lua_toimgui(L, 1);

	if (!imgui->isInRender) {
		luaL_error(L, "Draw functions can only be called inside renderer");
		return 0;
	}

	lua_pushboolean(L, ImGui::MenuItem(luaL_checkstring(L, 2)) == true);

	return 1;
}

int LuaImguiEndMenu(lua_State* L) {

	LuaImgui* imgui = lua_toimgui(L, 1);

	if (!imgui->isInRender) {
		luaL_error(L, "Draw functions can only be called inside renderer");
		return 0;
	}

	ImGui::EndMenu();

	return 0;
}

int LuaImguiBeginMenu(lua_State* L) {

	LuaImgui* imgui = lua_toimgui(L, 1);

	if (!imgui->isInRender) {
		luaL_error(L, "Draw functions can only be called inside renderer");
		return 0;
	}

	lua_pushboolean(L, ImGui::BeginMenu(luaL_checkstring(L, 2), lua_toboolean(L, 3) == 0) == true);

	return 1;
}

int LuaImguiEndMenuBar(lua_State* L) {

	LuaImgui* imgui = lua_toimgui(L, 1);

	if (!imgui->isInRender) {
		luaL_error(L, "Draw functions can only be called inside renderer");
		return 0;
	}

	ImGui::EndMenuBar();

	return 0;
}

int LuaImguiBeginMenuBar(lua_State* L) {

	LuaImgui* imgui = lua_toimgui(L, 1);

	if (!imgui->isInRender) {
		luaL_error(L, "Draw functions can only be called inside renderer");
		return 0;
	}

	lua_pushboolean(L, ImGui::BeginMenuBar() == true);

	return 1;
}

int LuaImguiSetNextWindowSize(lua_State* L) {

	LuaImgui* imgui = lua_toimgui(L, 1);

	if (!imgui->isInRender) {
		luaL_error(L, "Draw functions can only be called inside renderer");
		return 0;
	}

	ImGui::SetNextWindowSize(lua_toimvec2(L, 2), luaL_optinteger(L, 3, 0));

	return 0;
}

int LuaImguiSelectable(lua_State* L) {

	LuaImgui* imgui = lua_toimgui(L, 1);
	const char* text = luaL_checkstring(L, 2);
	int select = lua_toboolean(L, 3);

	if (!imgui->isInRender) {
		luaL_error(L, "Draw functions can only be called inside renderer");
		return 0;
	}

	if (ImGui::Selectable(text, select != 0)) {
		lua_pushboolean(L, 1);
	}
	else {
		lua_pushboolean(L, 0);
	}

	return 1;
}

int LuaImguiSeparator(lua_State* L) {

	LuaImgui* imgui = lua_toimgui(L, 1);

	if (!imgui->isInRender) {
		luaL_error(L, "Draw functions can only be called inside renderer");
		return 0;
	}

	ImGui::Separator();

	return 0;
}

int LuaImguiSameLine(lua_State* L) {

	LuaImgui* imgui = lua_toimgui(L, 1);

	if (!imgui->isInRender) {
		luaL_error(L, "Draw functions can only be called inside renderer");
		return 0;
	}

	ImGui::SameLine(luaL_optnumber(L, 2, 0), luaL_optnumber(L, 3, -1.0));

	return 0;
}

int LuaImguiButton(lua_State* L) {

	LuaImgui* imgui = lua_toimgui(L, 1);
	const char* text = luaL_checkstring(L, 2);

	if (!imgui->isInRender) {
		luaL_error(L, "Draw functions can only be called inside renderer");
		return 0;
	}

	lua_pushboolean(L, ImGui::Button(text) == true);

	return 1;
}

int LuaImguiColorEdit3(lua_State* L) {

	LuaImgui* imgui = lua_toimgui(L, 1);
	const char* text = luaL_checkstring(L, 2);
	const char* tag = luaL_checkstring(L, 3);

	if (!imgui->isInRender) {
		luaL_error(L, "Draw functions can only be called inside renderer");
		return 0;
	}

	ImguiElement* element = GetElement(imgui, tag, IMGUI_TYPE_VEC4);
	if (!element) {
		element = AddElement(imgui, tag, IMGUI_TYPE_VEC4);
		if (!element) {
			luaL_error(L, "Imgui Out of memory");
			return 0;
		}
	}

	ImVec4* f = (ImVec4*)element->Data;

	lua_pushboolean(L, ImGui::ColorEdit3(text, (float*)f) == true);

	return 1;
}

int LuaImguiSliderFloat(lua_State* L) {

	LuaImgui* imgui = lua_toimgui(L, 1);
	const char* text = luaL_checkstring(L, 2);
	const char* tag = luaL_checkstring(L, 3);
	float min = luaL_checknumber(L, 4);
	float max = luaL_checknumber(L, 5);

	if (!imgui->isInRender) {
		luaL_error(L, "Draw functions can only be called inside renderer");
		return 0;
	}

	ImguiElement* element = GetElement(imgui, tag, IMGUI_TYPE_FLOAT);
	if (!element) {
		element = AddElement(imgui, tag, IMGUI_TYPE_FLOAT);
		if (!element) {
			luaL_error(L, "Imgui Out of memory");
			return 0;
		}
	}

	float* f = (float*)element->Data;

	lua_pushboolean(L, ImGui::SliderFloat(text, f, min, max, luaL_optstring(L, 6, "%.3f"), (ImGuiSliderFlags)luaL_optinteger(L, 7, 0)) == true);

	return 1;
}

int LuaImguiText(lua_State* L) {

	LuaImgui* imgui = lua_toimgui(L, 1);
	size_t len;
	const char* text = luaL_checklstring(L, 2, &len);

	if (!imgui->isInRender) {
		luaL_error(L, "Draw functions can only be called inside renderer");
		return 0;
	}

	ImGui::TextUnformatted(text, text + len);

	return 0;
}

int LuaImguiCheckbox(lua_State* L) {

	LuaImgui* imgui = lua_toimgui(L, 1);
	const char* text = luaL_checkstring(L, 2);
	const char* tag = luaL_checkstring(L, 3);

	if (!imgui->isInRender) {
		luaL_error(L, "Draw functions can only be called inside renderer");
		return 0;
	}

	ImguiElement* element = GetElement(imgui, tag, IMGUI_TYPE_BOOL);
	if (!element) {
		element = AddElement(imgui, tag, IMGUI_TYPE_BOOL);
		if (!element) {
			luaL_error(L, "Imgui Out of memory");
			return 0;
		}
	}

	bool* check = (bool*)element->Data;

	lua_pushboolean(L, ImGui::Checkbox(text, check) == true);

	return 1;
}

int LuaImguiShowDemoWindow(lua_State* L) {

	LuaImgui* imgui = lua_toimgui(L, 1);
	const char* tag = lua_tostring(L, 2);

	if (!imgui->isInRender) {
		luaL_error(L, "Draw functions can only be called inside renderer");
		return 0;
	}

	bool* show = NULL;

	if (tag) {

		ImguiElement* element = GetElement(imgui, tag, IMGUI_TYPE_BOOL);
		if (!element) {
			element = AddElement(imgui, tag, IMGUI_TYPE_BOOL);
			if (!element) {
				luaL_error(L, "Imgui Out of memory");
				return 0;
			}
		}

		show = (bool*)element->Data;
	}

	ImGui::ShowDemoWindow(show);

	return 0;
}

int LuaImguiEnd(lua_State* L) {

	LuaImgui* imgui = lua_toimgui(L, 1);

	if (!imgui->isInRender) {
		luaL_error(L, "Draw functions can only be called inside renderer");
		return 0;
	}

	ImGui::End();

	return 0;
}

int LuaImguiBegin(lua_State* L) {

	LuaImgui* imgui = lua_toimgui(L, 1);
	const char* title = lua_tostring(L, 2);
	const char* tag = lua_tostring(L, 3);

	if (!imgui->isInRender) {
		luaL_error(L, "Draw functions can only be called inside renderer");
		return 0;
	}

	bool* closeable = NULL;

	if (tag) {

		ImguiElement* element = GetElement(imgui, tag, IMGUI_TYPE_BOOL);
		if (!element) {
			element = AddElement(imgui, tag, IMGUI_TYPE_BOOL);
			if (!element) {
				luaL_error(L, "Imgui Out of memory");
				return 0;
			}
		}

		closeable = (bool*)element->Data;
	}

	lua_pushboolean(L, ImGui::Begin(title, closeable, luaL_optinteger(L, 4, 0)));

	return 1;
}

int GetImguiInfo(lua_State* L) {

	LuaImgui* imgui = lua_toimgui(L, 1);

	lua_createtable(L, 0, 10);

	lua_pushstring(L, "InRender");
	lua_pushboolean(L, imgui->isInRender == true);
	lua_settable(L, -3);

	lua_pushstring(L, "IsValid");
	lua_pushboolean(L, imgui->renderFuncRef != LUA_REFNIL);
	lua_settable(L, -3);

	lua_pushstring(L, "Framerate");
	lua_pushnumber(L, ImGui::GetIO().Framerate);
	lua_settable(L, -3);

	ImGuiIO& io = ImGui::GetIO();

	lua_pushstring(L, "WantCaptureKeyboard");
	lua_pushboolean(L, io.WantCaptureKeyboard == true);
	lua_settable(L, -3);

	lua_pushstring(L, "WantCaptureMouse");
	lua_pushboolean(L, io.WantCaptureMouse == true);
	lua_settable(L, -3);

	lua_pushstring(L, "WantCaptureMouseUnlessPopupClose");
	lua_pushboolean(L, io.WantCaptureMouseUnlessPopupClose == true);
	lua_settable(L, -3);

	lua_pushstring(L, "DisplaySizeX");
	lua_pushnumber(L, io.DisplaySize.x);
	lua_settable(L, -3);

	lua_pushstring(L, "DisplaySizeY");
	lua_pushnumber(L, io.DisplaySize.y);
	lua_settable(L, -3);

	return 1;
}

int GetValueFromTag(lua_State* L) {

	LuaImgui* imgui = lua_toimgui(L, 1);
	const char* tag = luaL_checkstring(L, 2);
	int type = luaL_optinteger(L, 3, IMGUI_TYPE_ANY);

	if (type < IMGUI_TYPE_ANY || type > IMGUI_TYPE_MAX) {

		luaL_error(L, "Invalid type");
		return 0;
	}

	ImguiElement* element = GetElement(imgui, tag, type);

	if (!element) {
		lua_pushnil(L);
	}
	else if (element->Type == IMGUI_TYPE_BOOL) {

		lua_pushboolean(L, (*(bool*)element->Data) == true);
	}
	else if (element->Type == IMGUI_TYPE_FLOAT) {

		lua_pushnumber(L, *(float*)element->Data);
	}
	else if (element->Type == IMGUI_TYPE_VEC4) {

		ImVec4* vec = (ImVec4*)element->Data;

		lua_pushimvec4(L, *vec);
	}
	else if (type == IMGUI_TYPE_INT) {
		lua_pushinteger(L, *(int*)element->Data);
	}
	else if (type == IMGUI_TYPE_STRING) {
		lua_pushlstring(L, (char*)element->Data, element->Len);
	}
	else if (type == IMGUI_TYPE_DOUBLE) {
		lua_pushnumber(L, *(double*)element->Data);
	}

	return 1;
}

int SetValueFromTag(lua_State* L) {

	LuaImgui* imgui = lua_toimgui(L, 1);
	const char* tag = luaL_checkstring(L, 2);
	int type = luaL_checkinteger(L, 3);

	if (type <= IMGUI_TYPE_ANY || type > IMGUI_TYPE_MAX) {

		luaL_error(L, "Invalid type");
		return 0;
	}

	ImguiElement* element = GetElement(imgui, tag, type);

	if (!element) {
		element = AddElement(imgui, tag, type);
		if (!element) {
			luaL_error(L, "Out of memory");
			return 0;
		}
	}

	if (type == IMGUI_TYPE_BOOL) {
		*((bool*)element->Data) = (lua_toboolean(L, 4) != 0);
	}
	else if (type == IMGUI_TYPE_FLOAT) {
		*((float*)element->Data) = lua_tonumber(L, 4);
	}
	else if (type == IMGUI_TYPE_VEC4) {

		luaL_checktype(L, 4, LUA_TTABLE);
		ImVec4 vec = lua_toimvec4(L, 4);

		memcpy(element->Data, &vec, sizeof(ImVec4));
	}
	else if (type == IMGUI_TYPE_INT) {
		*((int*)element->Data) = lua_tointeger(L, 4);
	}
	else if (type == IMGUI_TYPE_STRING) {

		size_t len;
		const char* str = lua_tolstring(L, 4, &len);

		if (len + 1 > element->Size || !element->Data) {

			if (element->Data) {
				gff_free(element->Data);
				element->Size = 0;
				element->Len = 0;
			}

			element->Data = gff_malloc(sizeof(char) * len + 1);

			if (!element->Data) {
				luaL_error(L, "Out of memory");
				return 0;
			}

			element->Size = len + 1;
		}

		char* elementStr = (char*)element->Data;
		memcpy(elementStr, str, sizeof(char) * len);
		elementStr[len] = '\0';
		element->Len = len;
	}
	else if (type == IMGUI_TYPE_DOUBLE) {
		*((double*)element->Data) = lua_tonumber(L, 4);
	}

	return 0;
}

int Vec4ToRGB(lua_State* L) {

	ImVec4 vec = lua_toimvec4(L, 1);

	int r = MAX(MIN((int)(vec.x * 255.0), 255), 0);
	int g = MAX(MIN((int)(vec.y * 255.0), 255), 0);
	int b = MAX(MIN((int)(vec.z * 255.0), 255), 0);

	lua_pushinteger(L, r);
	lua_pushinteger(L, g);
	lua_pushinteger(L, b);

	return 3;
}

int RGBToVec4(lua_State* L) {

	int r = luaL_checkinteger(L, 1);
	int g = luaL_checkinteger(L, 2);
	int b = luaL_checkinteger(L, 3);
	int a = luaL_optnumber(L, 4, 4);

	r = MAX(MIN(r, 255), 0);
	g = MAX(MIN(g, 255), 0);
	b = MAX(MIN(b, 255), 0);
	a = MAX(MIN(a, 1.0), 0.0);

	lua_pushimvec4(L, ImVec4(r / 255.0, g / 255.0, b / 255.0, a));

	return 1;
}

int ClearMemory(lua_State* L) {

	LuaImgui* imgui = lua_toimgui(L, 1);

	ImguiElement* c = imgui->Elements;
	ImguiElement* temp;
	imgui->Elements = NULL;

	while (c) {

		temp = c;
		c = c->Next;

		temp->freeFunc(temp);
	}

	if (imgui->stringArray) {
		gff_free(imgui->stringArray);
	}

	imgui->stringArray = NULL;
	imgui->stringArrayLen = 0;
	imgui->stringArraySize = 0;

	return 0;
}

int GetAllValues(lua_State* L) {

	LuaImgui* imgui = lua_toimgui(L, 1);

	int cnt = 0;
	ImguiElement* c = imgui->Elements;
	while (c) {
		cnt++;
		c = c->Next;
	}

	lua_createtable(L, cnt, 0);
	cnt = 0;
	c = imgui->Elements;
	while (c) {

		lua_createtable(L, 0, 2);

		lua_pushstring(L, "Name");
		lua_pushstring(L, c->Name);
		lua_settable(L, -3);

		lua_pushstring(L, "Type");
		lua_pushinteger(L, c->Type);
		lua_settable(L, -3);

		lua_rawseti(L, -2, ++cnt);
		c = c->Next;
	}

	return 1;
}

void lua_pushimvec2(lua_State* L, ImVec2 vec) {

	lua_createtable(L, 0, 2);

	lua_pushstring(L, "x");
	lua_pushnumber(L, vec.x);
	lua_settable(L, -3);

	lua_pushstring(L, "y");
	lua_pushnumber(L, vec.y);
	lua_settable(L, -3);
}

ImVec2 lua_toimvec2(lua_State* L, int idx) {

	ImVec2 vec = ImVec2(0, 0);

	if (lua_type(L, idx) != LUA_TTABLE) {
		return vec;
	}

	lua_pushvalue(L, idx);

	lua_pushstring(L, "x");
	lua_gettable(L, -2);
	vec.x = lua_tonumber(L, -1);
	lua_pop(L, 1);

	lua_pushstring(L, "y");
	lua_gettable(L, -2);
	vec.y = lua_tonumber(L, -1);
	lua_pop(L, 1);

	lua_pop(L, 1);

	return vec;
}

void lua_pushimvec4(lua_State* L, ImVec4 vec) {

	lua_createtable(L, 0, 4);

	lua_pushstring(L, "x");
	lua_pushnumber(L, vec.x);
	lua_settable(L, -3);

	lua_pushstring(L, "y");
	lua_pushnumber(L, vec.y);
	lua_settable(L, -3);

	lua_pushstring(L, "z");
	lua_pushnumber(L, vec.z);
	lua_settable(L, -3);

	lua_pushstring(L, "w");
	lua_pushnumber(L, vec.w);
	lua_settable(L, -3);
}

ImVec4 lua_toimvec4(lua_State* L, int idx) {

	ImVec4 vec = ImVec4(0, 0, 0, 0);

	if (lua_type(L, idx) != LUA_TTABLE) {
		return vec;
	}

	lua_pushvalue(L, idx);

	lua_pushstring(L, "x");
	lua_gettable(L, -2);
	vec.x = lua_tonumber(L, -1);
	lua_pop(L, 1);

	lua_pushstring(L, "y");
	lua_gettable(L, -2);
	vec.y = lua_tonumber(L, -1);
	lua_pop(L, 1);

	lua_pushstring(L, "z");
	lua_gettable(L, -2);
	vec.z = lua_tonumber(L, -1);
	lua_pop(L, 1);

	lua_pushstring(L, "w");
	lua_gettable(L, -2);
	vec.w = lua_tonumber(L, -1);
	lua_pop(L, 1);

	lua_pop(L, 1);

	return vec;
}

LuaImgui* lua_toimgui(lua_State* L, int index) {

	LuaImgui* imgui = (LuaImgui*)luaL_checkudata(L, index, IMGUI);

	if (imgui == NULL) {
		luaL_error(L, "parameter is not a %s", IMGUI);
	}

	return imgui;
}

int imgui_tostring(lua_State* L) {
	char tim[100];
	sprintf(tim, "Imgui: 0x%08X", lua_toimgui(L, 1));
	lua_pushfstring(L, tim);
	return 1;
}

ImguiElement* GetElement(LuaImgui* ui, const char* name, int type) {

	ImguiElement* c = ui->Elements;
	while (c) {

		if ((type == 0 || c->Type == type) && strcmp(c->Name, name) == 0) {
			return c;
		}

		c = c->Next;
	}

	return NULL;
}

void GenericElementFree(ImguiElement* element) {

	if (element->Name) {
		gff_free(element->Name);
		element->Name = NULL;
	}

	if (element->Data) {
		gff_free(element->Data);
		element->Data = NULL;
	}

	gff_free(element);
}

ImguiElement* AddElement(LuaImgui* ui, const char* name, int type) {

	ImguiElement** addr = &ui->Elements;

	while (*addr) {
		addr = &(*addr)->Next;
	}

	*addr = (ImguiElement*)gff_malloc(sizeof(ImguiElement));

	if (*addr) {
		ZeroMemory(*addr, sizeof(ImguiElement));

		(*addr)->Name = (char*)gff_malloc(strlen(name) + 1);

		if (!(*addr)->Name) {
			gff_free(*addr);
			*addr = NULL;
			return NULL;
		}

		strcpy((*addr)->Name, name);

		if (type == IMGUI_TYPE_BOOL) {

			(*addr)->Data = gff_malloc(sizeof(bool));

			if (!(*addr)->Data) {
				gff_free((*addr)->Name);
				gff_free(*addr);
				*addr = NULL;
				return NULL;
			}

			(*addr)->Len = sizeof(bool);
			(*addr)->Size = sizeof(bool);
			(*addr)->freeFunc = &GenericElementFree;
			ZeroMemory((*addr)->Data, sizeof(bool));
		}
		else if (type == IMGUI_TYPE_FLOAT) {

			(*addr)->Data = gff_malloc(sizeof(float));

			if (!(*addr)->Data) {
				gff_free((*addr)->Name);
				gff_free(*addr);
				*addr = NULL;
				return NULL;
			}

			(*addr)->Len = sizeof(float);
			(*addr)->Size = sizeof(float);
			(*addr)->freeFunc = &GenericElementFree;
			ZeroMemory((*addr)->Data, sizeof(float));
		}
		else if (type == IMGUI_TYPE_VEC4) {

			(*addr)->Data = gff_malloc(sizeof(ImVec4));

			if (!(*addr)->Data) {
				gff_free((*addr)->Name);
				gff_free(*addr);
				*addr = NULL;
				return NULL;
			}

			(*addr)->Len = sizeof(ImVec4);
			(*addr)->Size = sizeof(ImVec4);
			(*addr)->freeFunc = &GenericElementFree;
			ZeroMemory((*addr)->Data, sizeof(ImVec4));
		}
		else if (type == IMGUI_TYPE_INT) {

			(*addr)->Data = gff_malloc(sizeof(int));

			if (!(*addr)->Data) {
				gff_free((*addr)->Name);
				gff_free(*addr);
				*addr = NULL;
				return NULL;
			}

			(*addr)->Len = sizeof(int);
			(*addr)->Size = sizeof(int);
			(*addr)->freeFunc = &GenericElementFree;
			ZeroMemory((*addr)->Data, sizeof(int));
		}
		else if (type == IMGUI_TYPE_STRING) {

			(*addr)->Data = gff_malloc(sizeof(char) * 20);

			if (!(*addr)->Data) {
				gff_free((*addr)->Name);
				gff_free(*addr);
				*addr = NULL;
				return NULL;
			}

			(*addr)->Len = 0;
			(*addr)->Size = sizeof(char) * 20;
			(*addr)->freeFunc = &GenericElementFree;
			ZeroMemory((*addr)->Data, sizeof(int));
		}
		else if (type == IMGUI_TYPE_DOUBLE) {

			(*addr)->Data = gff_malloc(sizeof(double));

			if (!(*addr)->Data) {
				gff_free((*addr)->Name);
				gff_free(*addr);
				*addr = NULL;
				return NULL;
			}

			(*addr)->Len = sizeof(double);
			(*addr)->Size = sizeof(double);
			(*addr)->freeFunc = &GenericElementFree;
			ZeroMemory((*addr)->Data, sizeof(double));
		}
		else {
			assert(false, "INVALID TYPE");
		}

		(*addr)->Type = type;
	}

	return *addr;
}

bool RemoveElement(LuaImgui* ui, const char* name, int type) {

	ImguiElement** addr = &ui->Elements;
	ImguiElement* temp;

	while (*addr) {

		if ((*addr)->Type == type && strcmp((*addr)->Name, name) == 0) {

			temp = *addr;
			*addr = temp->Next;

			temp->freeFunc(temp);

			return true;
		}

		addr = &(*addr)->Next;
	}

	return false;
}

void LoadTableIntoStringArray(lua_State* L, LuaImgui* ui, int idx) {

	if (lua_type(L, idx) != LUA_TTABLE) {
		return;
	}

	lua_pushvalue(L, idx);
	lua_len(L, -1);
	int len = lua_tointeger(L, -1);
	lua_pop(L, 1);

	if (len <= 0) {

		ui->stringArrayLen = 0;
		return;
	}
	else if (ui->stringArraySize < len) {

		if (ui->stringArray) {
			gff_free(ui->stringArray);
			ui->stringArraySize = 0;
			ui->stringArrayLen = 0;
		}

		ui->stringArray = (const char**)gff_malloc(sizeof(const char*) * len);

		if (!ui->stringArray) {
			luaL_error(L, "Out of memory");
			return;
		}

		ui->stringArraySize = len;
		ui->stringArrayLen = len;
	}
	else {
		ui->stringArrayLen = len;
	}

	for (int n = 1; n <= len; n++) {

		lua_pushinteger(L, n);
		lua_gettable(L, -2);

		ui->stringArray[n - 1] = lua_tostring(L, -1);

		lua_pop(L, 1);
	}

	lua_pop(L, 1);
}

int LuaImGuiInputTextCallback(ImGuiInputTextCallbackData* data) {

	if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {

		ImguiElement* element = (ImguiElement*)data->UserData;

		if (element->Size <= data->BufTextLen) {

			char* temp = (char*)realloc(element->Data, element->Size + 100 + 1);

			if (temp) {
				element->Data = temp;
				element->Size = element->Size + 100;
				data->Buf = temp;
				data->BufSize = element->Size;
			}
			else {
				data->BufSize = element->Size;
				data->BufTextLen = element->Size;
			}
		}

		element->Len = data->BufTextLen;
	}

	return 0;
}

LuaImgui* lua_pushimgui(lua_State* L) {

	LuaImgui* imgui = (LuaImgui*)lua_newuserdata(L, sizeof(LuaImgui));

	if (imgui == NULL) {
		luaL_error(L, "Unable to push imgui");
		return 0;
	}

	luaL_getmetatable(L, IMGUI);
	lua_setmetatable(L, -2);
	memset(imgui, 0, sizeof(LuaImgui));

	imgui->hWnd = (HWND)INVALID_HANDLE_VALUE;
	imgui->renderFuncRef = LUA_REFNIL;

	return imgui;
}

int imgui_gc(lua_State* L) {

	LuaImgui* imgui = lua_toimgui(L, 1);

	if (imgui->hWnd != INVALID_HANDLE_VALUE) {

		ClearMemory(L);

		WaitForLastSubmittedFrame(imgui);

		ImGui_ImplDX12_Shutdown();
		ImGui_ImplWin32_Shutdown();
		ImGui::DestroyContext();

		CleanupDeviceD3D(imgui);
		DestroyWindow(imgui->hWnd);
		UnregisterClass(imgui->wc.lpszClassName, imgui->wc.hInstance);

		imgui->hWnd = (HWND)INVALID_HANDLE_VALUE;

		luaL_unref(L, LUA_REGISTRYINDEX, imgui->renderFuncRef);

		windowExists = false;

		if (imgui->backgroundTag) {
			gff_free(imgui->backgroundTag);
		}

		imgui->isInRender = false;
	}

	return 0;
}
