#include "LuaImgui.h"
#include "LuaImguiMain.h"

static const struct luaL_Reg imguifunctions[] = {

	{ "SetKeyboardFocusHere", LuaImguiSetKeyboardFocusHere },
	{ "ShowStyleEditor", LuaImguiShowStyleEditor },
	{ "SetStyle", LuaImguiTableSetStyle },
	{ "GetStyle", LuaImguiTableGetStyle },
	{ "SetScrollHereY", LuaImguiSetScrollHereY },
	{ "GetScrollMaxY", LuaImguiGetScrollMaxY },
	{ "GetScrollY", LuaImguiGetScrollY },
	{ "TableSetupColumn", LuaImguiTableSetupColumn },
	{ "PushTextWrapPos", LuaImguiPushTextWrapPos },
	{ "PopTextWrapPos", LuaImguiPopTextWrapPos },
	{ "GetFontSize", LuaImguiGetFontSize },
	{ "OpenPopup", LuaImguiOpenPopup },
	{ "CloseCurrentPopup", LuaImguiCloseCurrentPopup },
	{ "BeginPopup", LuaImguiBeginPopup },
	{ "EndPopup", LuaImguiEndPopup },
	{ "SetNextWindowPos", LuaImguiSetNextWindowPos },
	{ "SetNextItemWidth", LuaImguiSetNextItemWidth },
	{ "PlotLines", LuaImguiPlotLines },
	{ "TableSetColumnIndex", LuaImguiTableSetColumnIndex },
	{ "TableNextRow", LuaImguiTableNextRow },
	{ "TreePop", LuaImguiTreePop },
	{ "TreeNode", LuaImguiTreeNode },
	{ "SetNextItemOpen", LuaImguiSetNextItemOpen },
	{ "PopStyleVar", LuaImguiPopStyleVar },
	{ "PushStyleVar", LuaImguiPushStyleVar },
	{ "GetTextLineHeightWithSpacing", LuaImguiGetTextLineHeightWithSpacing },
	{ "GetCursorStartPos", LuaImguiGetCursorStartPos },
	{ "GetCursorPos", LuaImguiGetCursorPos },
	{ "CalcTextSize", LuaImguiCalcTextSize },
	{ "BeginDisabled", LuaImguiBeginDisabled },
	{ "EndDisabled", LuaImguiEndDisabled },
	{ "Indent", LuaImguiIndent },
	{ "Unindent", LuaImguiUnindent },
	{ "BeginMainMenuBar", LuaImguiBeginMainMenuBar },
	{ "EndMainMenuBar", LuaImguiEndMainMenuBar },
	{ "EndTable", LuaImguiEndTable },
	{ "BeginTable", LuaImguiBeginTable },
	{ "TableNextColumn", LuaImguiTableNextColumn },
	{ "CollapsingHeader", LuaImguiCollapsingHeader },
	{ "ProgressBar", LuaImguiProgressBar },
	{ "GetTextLineHeight", LuaImguiGetTextLineHeight },
	{ "NextWindowContentSize", LuaImguiNextWindowContentSize },
	{ "GetWindowSize", LuaImguiGetWindowSize },
	{ "InputTextMultiline", LuaImguiInputTextMultiline },
	{ "ListBox", LuaImguiListBox },
	{ "SliderInt", LuaImguiSliderInt },
	{ "InputDouble", LuaImguiInputDouble },
	{ "InputFloat", LuaImguiInputFloat },
	{ "InputInt", LuaImguiInputInt },
	{ "InputText", LuaImguiInputText },
	{ "HelpMarker", LuaImguiHelpMarker },
	{ "Combo", LuaImguiCombo },
	{ "LabelText", LuaImguiLabelText },
	{ "BeginTooltip", LuaImguiBeginTooltip },
	{ "EndTooltip", LuaImguiEndTooltip },
	{ "IsItemHovered", LuaImguiIsItemHovered },
	{ "PushButtonRepeat", LuaImguiPushButtonRepeat },
	{ "PopButtonRepeat", LuaImguiPopButtonRepeat },
	{ "ArrowButton", LuaImguiArrowButton },
	{ "AlignTextToFramePadding", LuaImguiAlignTextToFramePadding },
	{ "PushStyleColor", LuaImguiPushStyleColor },
	{ "PopStyleColor", LuaImguiPopStyleColor },
	{ "PushId", LuaImguiPushId },
	{ "PopId", LuaImguiPopId },
	{ "RadioButton", LuaImguiRadioButton },
	{ "TextColored", LuaImguiTextColored },
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

	{ "GetEnums", lua_pushimguienums },
	{ "GetAllValues", GetAllValues },
	{ "Clear", ClearMemory },
	{ "Vec4ToRGB", Vec4ToRGB },
	{ "RGBToVec4", RGBToVec4 },
	{ "GetValue", GetValueFromTag },
	{ "SetValue", SetValueFromTag },
	{ "Info", GetImguiInfo },
	{ "Tick", MainloopImguiWindow },
	{ "Create", CreateImguiWindow },
	{ "Close", imgui_gc },
	{ "Quit", MainLoopQuit },
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