#include "LuaImgui.h"

int LuaImguiSetNextWindowSize(lua_State* L) {

	LuaImgui* imgui = lua_toimgui(L, 1);

	if (!imgui->isInRender) {
		luaL_error(L, "Draw functions can only be called inside renderer");
		return 0;
	}

	ImGui::SetNextWindowSize(ImVec2(luaL_optinteger(L, 2, 0), luaL_optinteger(L, 3, 0)), luaL_optinteger(L, 4, 0));

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

	ImGui::SameLine();

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

	ImGui::ColorEdit3(text, (float*)f);

	return 0;
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

	ImGui::SliderFloat(text, f, min, max);

	return 0;
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

	ImGui::Checkbox(text, check);

	return 0;
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

	lua_createtable(L, 9, 0);

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

		lua_createtable(L, 4, 0);

		lua_pushstring(L, "x");
		lua_pushnumber(L, vec->x);
		lua_settable(L, -3);

		lua_pushstring(L, "y");
		lua_pushnumber(L, vec->y);
		lua_settable(L, -3);

		lua_pushstring(L, "z");
		lua_pushnumber(L, vec->z);
		lua_settable(L, -3);

		lua_pushstring(L, "w");
		lua_pushnumber(L, vec->w);
		lua_settable(L, -3);
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
		ImVec4* vec = (ImVec4*)element->Data;

		lua_pushvalue(L, 4);

		lua_pushstring(L, "x");
		lua_gettable(L, -2);
		vec->x = lua_tonumber(L, -1);
		lua_pop(L, 1);

		lua_pushstring(L, "y");
		lua_gettable(L, -2);
		vec->y = lua_tonumber(L, -1);
		lua_pop(L, 1);

		lua_pushstring(L, "z");
		lua_gettable(L, -2);
		vec->z = lua_tonumber(L, -1);
		lua_pop(L, 1);

		lua_pushstring(L, "w");
		lua_gettable(L, -2);
		vec->w = lua_tonumber(L, -1);
		lua_pop(L, 1);

		lua_pop(L, 1);
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

LuaImgui* lua_toimgui(lua_State* L, int index) {

	LuaImgui* imgui = (LuaImgui*)luaL_checkudata(L, index, IMGUI);

	if (imgui == NULL) {
		luaL_error(L, "parameter is not a %s", IMGUI);
	}

	return imgui;
}

int imgui_gc(lua_State* L) {

	LuaImgui* imgui = lua_toimgui(L, 1);

	if (imgui->hWnd != INVALID_HANDLE_VALUE) {

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

		ImguiElement* c = imgui->Elements;
		ImguiElement* temp;
		imgui->Elements = NULL;
		while (c) {

			temp = c;
			c = c->Next;

			if (temp->Data)
				gff_free(temp->Data);
			if (temp->Name)
				gff_free(temp->Name);
			gff_free(temp);
		}

		if (imgui->backgroundTag) {
			gff_free(imgui->backgroundTag);
		}
	}

	return 0;
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

			ZeroMemory((*addr)->Data, sizeof(ImVec4));
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

			gff_free(temp->Name);
			gff_free(temp);

			return true;
		}

		addr = &(*addr)->Next;
	}

	return false;
}