#include "LuaImgui.h"
#include "LuaImguiMain.h"

static const struct luaL_Reg imguifunctions[] = {

	{ "TextWrapped", LuaImguiTextWrapped },
	{ "BeginTabItem", LuaImguiBeginTabItem },
	{ "EndTabItem", LuaImguiEndTabBarItem },
	{ "EndTabBar", LuaImguiEndTabBar },
	{ "BeginTabBar", LuaImguiBeginTabBar },
	{ "GetFrameHeightWithSpacing", LuaImguiGetFrameHeightWithSpacing },
	{ "BeginGroup", LuaImguiBeginGroup },
	{ "EndGroup", LuaImguiEndGroup },
	{ "EndChild", LuaImguiEndChild },
	{ "BeginChild", LuaImguiBeginChild },
	{ "MenuItem", LuaImguiMenuItem },
	{ "BeginMenu", LuaImguiBeginMenu },
	{ "EndMenu", LuaImguiEndMenu },
	{ "EndMenuBar", LuaImguiEndMenuBar },
	{ "BeginMenuBar", LuaImguiBeginMenuBar },
	{ "SameLine", LuaImguiSameLine },
	{ "Separator", LuaImguiSeparator },
	{ "Selectable", LuaImguiSelectable },
	{ "Button", LuaImguiButton },
	{ "ColorEdit3", LuaImguiColorEdit3 },
	{ "SliderFloat", LuaImguiSliderFloat },
	{ "Text", LuaImguiText },
	{ "Checkbox", LuaImguiCheckbox },
	{ "ShowDemoWindow", LuaImguiShowDemoWindow },
	{ "End", LuaImguiEnd },
	{ "Begin", LuaImguiBegin },
	{ "SetNextWindowSize", LuaImguiSetNextWindowSize },
	
	{ "GetValue", GetValueFromTag },
	{ "SetValue", SetValueFromTag },
	{ "Info", GetImguiInfo },
	{ "Tick", MainloopImguiWindow },
	{ "Create", CreateImguiWindow },
	{ "Close", imgui_gc },
	{ NULL, NULL }
};

static const luaL_Reg imguimeta[] = {
	{ "__gc",  imgui_gc },
	{ "__tostring",  imgui_tostring },
{ NULL, NULL }
};

int luaopen_imgui(lua_State* L) {

	luaL_newlibtable(L, imguifunctions);
	luaL_setfuncs(L, imguifunctions, 0);

	luaL_newmetatable(L, IMGUI);
	luaL_setfuncs(L, imguimeta, 0);

	lua_pushliteral(L, "__index");
	lua_pushvalue(L, -3);
	lua_rawset(L, -3);
	lua_pushliteral(L, "__metatable");
	lua_pushvalue(L, -3);
	lua_rawset(L, -3);

	lua_pop(L, 1);
	return 1;
}