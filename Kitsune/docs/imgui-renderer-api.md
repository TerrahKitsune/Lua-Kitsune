# ImGui Renderer API Reference

> **Auto-generated** by `generate_imgui_bindings.py`. Do not edit manually.

> **Note:** The `renderer` userdata is only valid during the `renderFn` callback passed to `Imgui.Start`. Do not store it across frames.

## Global API

### `Imgui.Start(title, width, height, renderFn, [context], [onError])`

Opens the ImGui window and begins the render loop. Blocks until the window is closed or `renderFn` returns `false`.

- `title` — window title string
- `width`, `height` — initial window size in pixels
- `renderFn(renderer, context)` — called every frame. Return `false` to close, `true`/nothing to continue.
- `context` *(optional)* — any Lua table; passed to `renderFn` every frame. Created empty if omitted.
- `onError(err)` *(optional)* — called when `renderFn` raises an error. Return `true` to keep running, `false`/nothing to stop.

Can only be called once per script run. Removes itself after the first call.

### `Imgui.Schedule(fn, [args...])`

Queues `fn` to start as a fire-and-forget coroutine at the end of the current frame. Safe to call from inside `renderFn`. Do not use the `renderer` inside a scheduled function — it is only valid during the frame callback.

### `Imgui.Console(visible)`

Shows or hides the console window. **Windows only** — no-op on Linux.

### Window functions

All return `nil` if called before `Imgui.Start`.

| Function | Parameters | Returns | Notes |
|---|---|---|---|
| `Imgui.GetWindowWidth()` | — | `integer` | Current window width in pixels |
| `Imgui.GetWindowHeight()` | — | `integer` | Current window height in pixels |
| `Imgui.GetWindowX()` | — | `integer` | Window X position on screen |
| `Imgui.GetWindowY()` | — | `integer` | Window Y position on screen |
| `Imgui.SetWindowSize(w, h)` | `integer, integer` | — | Resize the window |
| `Imgui.SetWindowPosition(x, y)` | `integer, integer` | — | Move the window |
| `Imgui.SetWindowTitle(title)` | `string` | — | Change the window title bar text |
| `Imgui.IsMinimized()` | — | `boolean` | Whether the window is minimised |
| `Imgui.IsFocused()` | — | `boolean` | Whether the window has input focus |
| `Imgui.SetFullscreen(enabled)` | `boolean` | — | Toggle borderless fullscreen |
| `Imgui.GetMonitor()` | — | `index, name, x, y, width, height, refreshRate` | Info about the monitor the window is currently on |

## Supported Methods

| Signature | Returns |
|---|---|
| `renderer:ShowDemoWindow([p_open: boolean])` | `boolean p_open` |
| `renderer:ShowMetricsWindow([p_open: boolean])` | `boolean p_open` |
| `renderer:ShowDebugLogWindow([p_open: boolean])` | `boolean p_open` |
| `renderer:ShowIDStackToolWindow()` | `—` |
| `renderer:ShowAboutWindow([p_open: boolean])` | `boolean p_open` |
| `renderer:ShowStyleSelector(label: string)` | `boolean` |
| `renderer:ShowFontSelector(label: string)` | `—` |
| `renderer:ShowUserGuide()` | `—` |
| `renderer:GetVersion()` | `string` |
| `renderer:Begin(name: string, [p_open: boolean], [flags: integer])` | `boolean, boolean p_open` |
| `renderer:End()` | `—` |
| `renderer:BeginChild(str_id: string, [size: number, number  or  {x, y}], [child_flags: integer], [window_flags: integer])` | `boolean` |
| `renderer:EndChild()` | `—` |
| `renderer:IsWindowAppearing()` | `boolean` |
| `renderer:IsWindowCollapsed()` | `boolean` |
| `renderer:IsWindowFocused([flags: integer])` | `boolean` |
| `renderer:IsWindowHovered([flags: integer])` | `boolean` |
| `renderer:GetWindowPos()` | `number x, number y` |
| `renderer:GetWindowSize()` | `number x, number y` |
| `renderer:GetWindowWidth()` | `number` |
| `renderer:GetWindowHeight()` | `number` |
| `renderer:SetNextWindowPos(pos: number, number  or  {x, y}, [cond: integer])` | `—` |
| `renderer:SetNextWindowSize(size: number, number  or  {x, y}, [cond: integer])` | `—` |
| `renderer:SetNextWindowContentSize(size: number, number  or  {x, y})` | `—` |
| `renderer:SetNextWindowCollapsed(collapsed: boolean, [cond: integer])` | `—` |
| `renderer:SetNextWindowFocus()` | `—` |
| `renderer:SetNextWindowScroll(scroll: number, number  or  {x, y})` | `—` |
| `renderer:SetNextWindowBgAlpha(alpha: number)` | `—` |
| `renderer:SetWindowPos(pos: number, number  or  {x, y}, [cond: integer])` | `—` |
| `renderer:SetWindowSize(size: number, number  or  {x, y}, [cond: integer])` | `—` |
| `renderer:SetWindowCollapsed(collapsed: boolean, [cond: integer])` | `—` |
| `renderer:SetWindowFocus()` | `—` |
| `renderer:SetWindowFontScale(scale: number)` | `—` |
| `renderer:GetScrollX()` | `number` |
| `renderer:GetScrollY()` | `number` |
| `renderer:SetScrollX(scroll_x: number)` | `—` |
| `renderer:SetScrollY(scroll_y: number)` | `—` |
| `renderer:GetScrollMaxX()` | `number` |
| `renderer:GetScrollMaxY()` | `number` |
| `renderer:SetScrollHereX([center_x_ratio: number])` | `—` |
| `renderer:SetScrollHereY([center_y_ratio: number])` | `—` |
| `renderer:SetScrollFromPosX(local_x: number, [center_x_ratio: number])` | `—` |
| `renderer:SetScrollFromPosY(local_y: number, [center_y_ratio: number])` | `—` |
| `renderer:PopFont()` | `—` |
| `renderer:PushStyleColor(idx: integer, col: ImU32)` | `—` |
| `renderer:PopStyleColor()` | `—` |
| `renderer:PopStyleVar()` | `—` |
| `renderer:PushItemFlag(option: integer, enabled: boolean)` | `—` |
| `renderer:PopItemFlag()` | `—` |
| `renderer:PushItemWidth(item_width: number)` | `—` |
| `renderer:PopItemWidth()` | `—` |
| `renderer:SetNextItemWidth(item_width: number)` | `—` |
| `renderer:CalcItemWidth()` | `number` |
| `renderer:PushTextWrapPos([wrap_local_pos_x: number])` | `—` |
| `renderer:PopTextWrapPos()` | `—` |
| `renderer:GetFontSize()` | `number` |
| `renderer:GetFontTexUvWhitePixel()` | `number x, number y` |
| `renderer:GetColorU32(idx: integer)` | `ImU32` |
| `renderer:GetCursorScreenPos()` | `number x, number y` |
| `renderer:SetCursorScreenPos(pos: number, number  or  {x, y})` | `—` |
| `renderer:GetContentRegionAvail()` | `number x, number y` |
| `renderer:GetCursorPos()` | `number x, number y` |
| `renderer:GetCursorPosX()` | `number` |
| `renderer:GetCursorPosY()` | `number` |
| `renderer:SetCursorPos(local_pos: number, number  or  {x, y})` | `—` |
| `renderer:SetCursorPosX(local_x: number)` | `—` |
| `renderer:SetCursorPosY(local_y: number)` | `—` |
| `renderer:GetCursorStartPos()` | `number x, number y` |
| `renderer:Separator()` | `—` |
| `renderer:SameLine()` | `—` |
| `renderer:NewLine()` | `—` |
| `renderer:Spacing()` | `—` |
| `renderer:Dummy(size: number, number  or  {x, y})` | `—` |
| `renderer:Indent()` | `—` |
| `renderer:Unindent()` | `—` |
| `renderer:BeginGroup()` | `—` |
| `renderer:EndGroup()` | `—` |
| `renderer:AlignTextToFramePadding()` | `—` |
| `renderer:GetTextLineHeight()` | `number` |
| `renderer:GetTextLineHeightWithSpacing()` | `number` |
| `renderer:GetFrameHeight()` | `number` |
| `renderer:GetFrameHeightWithSpacing()` | `number` |
| `renderer:PushID(str_id: string)` | `—` |
| `renderer:PopID()` | `—` |
| `renderer:GetID(str_id: string)` | `integer` |
| `renderer:TextUnformatted(text: string)` | `—` |
| `renderer:SeparatorText(label: string)` | `—` |
| `renderer:Button(label: string)` | `boolean` |
| `renderer:SmallButton(label: string)` | `boolean` |
| `renderer:InvisibleButton(str_id: string, size: number, number  or  {x, y}, [flags: integer])` | `boolean` |
| `renderer:ArrowButton(str_id: string, dir: integer)` | `boolean` |
| `renderer:Checkbox(label: string, v: boolean)` | `boolean, boolean v` |
| `renderer:CheckboxFlagsIntPtr(label: string, flags: integer, flags_value: integer)` | `boolean, integer flags` |
| `renderer:RadioButton(label: string, active: boolean)` | `boolean` |
| `renderer:ProgressBar(fraction: number, [size_arg: number, number  or  {x, y}], [overlay: string])` | `—` |
| `renderer:Bullet()` | `—` |
| `renderer:TextLink(label: string)` | `boolean` |
| `renderer:TextLinkOpenURL(label: string)` | `—` |
| `renderer:BeginCombo(label: string, preview_value: string, [flags: integer])` | `boolean` |
| `renderer:EndCombo()` | `—` |
| `renderer:ComboEx(label: string, current_item: integer, items_separated_by_zeros: string, [popup_max_height_in_items: integer])` | `boolean, integer current_item` |
| `renderer:DragFloat(label: string, v: number)` | `boolean, number v` |
| `renderer:DragFloatRange2(label: string, v_current_min: number, v_current_max: number)` | `boolean, number v_current_min, number v_current_max` |
| `renderer:DragInt(label: string, v: integer)` | `boolean, integer v` |
| `renderer:DragIntRange2(label: string, v_current_min: integer, v_current_max: integer)` | `boolean, integer v_current_min, integer v_current_max` |
| `renderer:SliderFloat(label: string, v: number, v_min: number, v_max: number)` | `boolean, number v` |
| `renderer:SliderAngle(label: string, v_rad: number)` | `boolean, number v_rad` |
| `renderer:SliderInt(label: string, v: integer, v_min: integer, v_max: integer)` | `boolean, integer v` |
| `renderer:VSliderFloat(label: string, size: number, number  or  {x, y}, v: number, v_min: number, v_max: number)` | `boolean, number v` |
| `renderer:VSliderInt(label: string, size: number, number  or  {x, y}, v: integer, v_min: integer, v_max: integer)` | `boolean, integer v` |
| `renderer:InputFloat(label: string, v: number)` | `boolean, number v` |
| `renderer:InputInt(label: string, v: integer)` | `boolean, integer v` |
| `renderer:ColorButton(desc_id: string, col: number, number, number, number, [flags: integer])` | `boolean` |
| `renderer:SetColorEditOptions(flags: integer)` | `—` |
| `renderer:TreeNode(label: string)` | `boolean` |
| `renderer:TreeNodeEx(label: string, [flags: integer])` | `boolean` |
| `renderer:TreePush(str_id: string)` | `—` |
| `renderer:TreePop()` | `—` |
| `renderer:GetTreeNodeToLabelSpacing()` | `number` |
| `renderer:CollapsingHeader(label: string, [flags: integer])` | `boolean` |
| `renderer:SetNextItemOpen(is_open: boolean, [cond: integer])` | `—` |
| `renderer:SetNextItemStorageID(storage_id: integer)` | `—` |
| `renderer:Selectable(label: string)` | `boolean` |
| `renderer:IsItemToggledSelection()` | `boolean` |
| `renderer:BeginListBox(label: string, [size: number, number  or  {x, y}])` | `boolean` |
| `renderer:EndListBox()` | `—` |
| `renderer:BeginMenuBar()` | `boolean` |
| `renderer:EndMenuBar()` | `—` |
| `renderer:BeginMainMenuBar()` | `boolean` |
| `renderer:EndMainMenuBar()` | `—` |
| `renderer:BeginMenu(label: string)` | `boolean` |
| `renderer:EndMenu()` | `—` |
| `renderer:MenuItem(label: string)` | `boolean` |
| `renderer:BeginTooltip()` | `boolean` |
| `renderer:EndTooltip()` | `—` |
| `renderer:BeginItemTooltip()` | `boolean` |
| `renderer:BeginPopup(str_id: string, [flags: integer])` | `boolean` |
| `renderer:BeginPopupModal(name: string, [p_open: boolean], [flags: integer])` | `boolean, boolean p_open` |
| `renderer:EndPopup()` | `—` |
| `renderer:OpenPopup(str_id: string, [popup_flags: integer])` | `—` |
| `renderer:OpenPopupOnItemClick([str_id: string], [popup_flags: integer])` | `—` |
| `renderer:CloseCurrentPopup()` | `—` |
| `renderer:BeginPopupContextItem()` | `boolean` |
| `renderer:BeginPopupContextWindow()` | `boolean` |
| `renderer:BeginPopupContextVoid()` | `boolean` |
| `renderer:IsPopupOpen(str_id: string, [flags: integer])` | `boolean` |
| `renderer:BeginTable(str_id: string, columns: integer, [flags: integer])` | `boolean` |
| `renderer:EndTable()` | `—` |
| `renderer:TableNextRow()` | `—` |
| `renderer:TableNextColumn()` | `boolean` |
| `renderer:TableSetColumnIndex(column_n: integer)` | `boolean` |
| `renderer:TableSetupColumn(label: string, [flags: integer])` | `—` |
| `renderer:TableSetupScrollFreeze(cols: integer, rows: integer)` | `—` |
| `renderer:TableHeader(label: string)` | `—` |
| `renderer:TableHeadersRow()` | `—` |
| `renderer:TableAngledHeadersRow()` | `—` |
| `renderer:TableGetColumnCount()` | `integer` |
| `renderer:TableGetColumnIndex()` | `integer` |
| `renderer:TableGetRowIndex()` | `integer` |
| `renderer:TableGetColumnName([column_n: integer])` | `string` |
| `renderer:TableGetColumnFlags([column_n: integer])` | `integer` |
| `renderer:TableSetColumnEnabled(column_n: integer, v: boolean)` | `—` |
| `renderer:TableGetHoveredColumn()` | `integer` |
| `renderer:Columns()` | `—` |
| `renderer:NextColumn()` | `—` |
| `renderer:GetColumnIndex()` | `integer` |
| `renderer:GetColumnWidth([column_index: integer])` | `number` |
| `renderer:SetColumnWidth(column_index: integer, width: number)` | `—` |
| `renderer:GetColumnOffset([column_index: integer])` | `number` |
| `renderer:SetColumnOffset(column_index: integer, offset_x: number)` | `—` |
| `renderer:GetColumnsCount()` | `integer` |
| `renderer:BeginTabBar(str_id: string, [flags: integer])` | `boolean` |
| `renderer:EndTabBar()` | `—` |
| `renderer:BeginTabItem(label: string, [p_open: boolean], [flags: integer])` | `boolean, boolean p_open` |
| `renderer:EndTabItem()` | `—` |
| `renderer:TabItemButton(label: string, [flags: integer])` | `boolean` |
| `renderer:SetTabItemClosed(tab_or_docked_window_label: string)` | `—` |
| `renderer:LogToTTY([auto_open_depth: integer])` | `—` |
| `renderer:LogToFile([auto_open_depth: integer], [filename: string])` | `—` |
| `renderer:LogToClipboard([auto_open_depth: integer])` | `—` |
| `renderer:LogFinish()` | `—` |
| `renderer:LogButtons()` | `—` |
| `renderer:BeginDragDropSource([flags: integer])` | `boolean` |
| `renderer:EndDragDropSource()` | `—` |
| `renderer:BeginDragDropTarget()` | `boolean` |
| `renderer:EndDragDropTarget()` | `—` |
| `renderer:BeginDisabled([disabled: boolean])` | `—` |
| `renderer:EndDisabled()` | `—` |
| `renderer:PushClipRect(clip_rect_min: number, number  or  {x, y}, clip_rect_max: number, number  or  {x, y}, intersect_with_current_clip_rect: boolean)` | `—` |
| `renderer:PopClipRect()` | `—` |
| `renderer:SetItemDefaultFocus()` | `—` |
| `renderer:SetKeyboardFocusHere()` | `—` |
| `renderer:SetNavCursorVisible(visible: boolean)` | `—` |
| `renderer:SetNextItemAllowOverlap()` | `—` |
| `renderer:IsItemHovered([flags: integer])` | `boolean` |
| `renderer:IsItemActive()` | `boolean` |
| `renderer:IsItemFocused()` | `boolean` |
| `renderer:IsItemClicked()` | `boolean` |
| `renderer:IsItemVisible()` | `boolean` |
| `renderer:IsItemEdited()` | `boolean` |
| `renderer:IsItemActivated()` | `boolean` |
| `renderer:IsItemDeactivated()` | `boolean` |
| `renderer:IsItemDeactivatedAfterEdit()` | `boolean` |
| `renderer:IsItemToggledOpen()` | `boolean` |
| `renderer:IsAnyItemHovered()` | `boolean` |
| `renderer:IsAnyItemActive()` | `boolean` |
| `renderer:IsAnyItemFocused()` | `boolean` |
| `renderer:GetItemID()` | `integer` |
| `renderer:GetItemRectMin()` | `number x, number y` |
| `renderer:GetItemRectMax()` | `number x, number y` |
| `renderer:GetItemRectSize()` | `number x, number y` |
| `renderer:IsRectVisibleBySize(size: number, number  or  {x, y})` | `boolean` |
| `renderer:GetTime()` | `number` |
| `renderer:GetFrameCount()` | `integer` |
| `renderer:GetStyleColorName(idx: integer)` | `string` |
| `renderer:CalcTextSize(text: string)` | `number x, number y` |
| `renderer:ColorConvertU32ToFloat4(in: ImU32)` | `number r, number g, number b, number a` |
| `renderer:ColorConvertFloat4ToU32(in: number, number, number, number)` | `ImU32` |
| `renderer:IsKeyDown(key: integer)` | `boolean` |
| `renderer:IsKeyPressed(key: integer)` | `boolean` |
| `renderer:IsKeyReleased(key: integer)` | `boolean` |
| `renderer:IsKeyChordPressed(key_chord: integer)` | `boolean` |
| `renderer:GetKeyPressedAmount(key: integer, repeat_delay: number, rate: number)` | `integer` |
| `renderer:GetKeyName(key: integer)` | `string` |
| `renderer:SetNextFrameWantCaptureKeyboard(want_capture_keyboard: boolean)` | `—` |
| `renderer:Shortcut(key_chord: integer, [flags: integer])` | `boolean` |
| `renderer:SetNextItemShortcut(key_chord: integer, [flags: integer])` | `—` |
| `renderer:SetItemKeyOwner(key: integer)` | `—` |
| `renderer:IsMouseDown(button: integer)` | `boolean` |
| `renderer:IsMouseClicked(button: integer)` | `boolean` |
| `renderer:IsMouseReleased(button: integer)` | `boolean` |
| `renderer:IsMouseDoubleClicked(button: integer)` | `boolean` |
| `renderer:IsMouseReleasedWithDelay(button: integer, delay: number)` | `boolean` |
| `renderer:GetMouseClickedCount(button: integer)` | `integer` |
| `renderer:IsMouseHoveringRect(r_min: number, number  or  {x, y}, r_max: number, number  or  {x, y})` | `boolean` |
| `renderer:IsAnyMouseDown()` | `boolean` |
| `renderer:GetMousePos()` | `number x, number y` |
| `renderer:GetMousePosOnOpeningCurrentPopup()` | `number x, number y` |
| `renderer:IsMouseDragging(button: integer, [lock_threshold: number])` | `boolean` |
| `renderer:GetMouseDragDelta([button: integer], [lock_threshold: number])` | `number x, number y` |
| `renderer:ResetMouseDragDelta()` | `—` |
| `renderer:GetMouseCursor()` | `integer` |
| `renderer:SetMouseCursor(cursor_type: integer)` | `—` |
| `renderer:SetNextFrameWantCaptureMouse(want_capture_mouse: boolean)` | `—` |
| `renderer:GetClipboardText()` | `string` |
| `renderer:SetClipboardText(text: string)` | `—` |
| `renderer:LoadIniSettingsFromDisk(ini_filename: string)` | `—` |
| `renderer:SaveIniSettingsToDisk(ini_filename: string)` | `—` |
| `renderer:DebugTextEncoding(text: string)` | `—` |
| `renderer:DebugFlashStyleColor(idx: integer)` | `—` |
| `renderer:DebugStartItemPicker()` | `—` |
| `renderer:PushButtonRepeat(repeat: boolean)` | `—` |
| `renderer:PopButtonRepeat()` | `—` |
| `renderer:PushTabStop(tab_stop: boolean)` | `—` |
| `renderer:PopTabStop()` | `—` |
| `renderer:GetContentRegionMax()` | `number x, number y` |
| `renderer:GetWindowContentRegionMin()` | `number x, number y` |
| `renderer:GetWindowContentRegionMax()` | `number x, number y` |
| `renderer:BeginChildFrame(id: integer, size: number, number  or  {x, y})` | `boolean` |
| `renderer:EndChildFrame()` | `—` |
| `renderer:ShowStackToolWindow([p_open: boolean])` | `boolean p_open` |
| `renderer:SetItemAllowOverlap()` | `—` |
| `renderer:PushAllowKeyboardFocus(tab_stop: boolean)` | `—` |
| `renderer:PopAllowKeyboardFocus()` | `—` |

## Hand-written Overrides

These functions are implemented manually in `ImguiRenderer.cpp`:

- `renderer:ColorConvertHSVtoRGB(...)`
- `renderer:ColorConvertRGBtoHSV(...)`
- `renderer:Combo(...)`
- `renderer:EndFrame(...)`
- `renderer:InputText(...)`
- `renderer:InputTextMultiline(...)`
- `renderer:ListBox(...)`
- `renderer:NewFrame(...)`
- `renderer:PlotHistogram(...)`
- `renderer:PlotHistogramEx(...)`
- `renderer:PlotLines(...)`
- `renderer:PlotLinesEx(...)`
- `renderer:Render(...)`
- `renderer:RenderPlatformWindowsDefault(...)`
- `renderer:Text(...)`
- `renderer:TextColored(...)`
- `renderer:TextDisabled(...)`
- `renderer:TextWrapped(...)`

## Skipped Functions

| Function | Reason |
|---|---|
| `CreateContext` | unsupported parameter type: ImFontAtlas* |
| `DestroyContext` | unsupported parameter type: ImGuiContext* |
| `GetCurrentContext` | unsupported return type: ImGuiContext* |
| `SetCurrentContext` | unsupported parameter type: ImGuiContext* |
| `GetIO` | unsupported return type: ImGuiIO* |
| `GetPlatformIO` | unsupported return type: ImGuiPlatformIO* |
| `GetStyle` | unsupported return type: ImGuiStyle* |
| `NewFrame` | hand-written override |
| `EndFrame` | hand-written override |
| `Render` | hand-written override |
| `GetDrawData` | unsupported return type: ImDrawData* |
| `ShowIDStackToolWindowEx` | duplicate variant of ImGui::ShowIDStackToolWindow |
| `ShowStyleEditor` | unsupported parameter type: ImGuiStyle* |
| `StyleColorsDark` | unsupported parameter type: ImGuiStyle* |
| `StyleColorsLight` | unsupported parameter type: ImGuiStyle* |
| `StyleColorsClassic` | unsupported parameter type: ImGuiStyle* |
| `BeginChildID` | duplicate variant of ImGui::BeginChild |
| `GetWindowDrawList` | unsupported return type: ImDrawList* |
| `SetNextWindowPosEx` | duplicate variant of ImGui::SetNextWindowPos |
| `SetNextWindowSizeConstraints` | unsupported parameter type: ImGuiSizeCallback |
| `SetWindowPosStr` | duplicate variant of ImGui::SetWindowPos |
| `SetWindowSizeStr` | duplicate variant of ImGui::SetWindowSize |
| `SetWindowCollapsedStr` | duplicate variant of ImGui::SetWindowCollapsed |
| `SetWindowFocusStr` | duplicate variant of ImGui::SetWindowFocus |
| `PushFont` | unsupported parameter type: ImFont* |
| `PushStyleColorImVec4` | duplicate variant of ImGui::PushStyleColor |
| `PopStyleColorEx` | duplicate variant of ImGui::PopStyleColor |
| `PushStyleVar` | unsupported parameter type: ImGuiStyleVar |
| `PushStyleVarImVec2` | unsupported parameter type: ImGuiStyleVar |
| `PushStyleVarX` | unsupported parameter type: ImGuiStyleVar |
| `PushStyleVarY` | unsupported parameter type: ImGuiStyleVar |
| `PopStyleVarEx` | duplicate variant of ImGui::PopStyleVar |
| `GetFont` | unsupported return type: ImFont* |
| `GetColorU32Ex` | duplicate variant of ImGui::GetColorU32 |
| `GetColorU32ImVec4` | duplicate variant of ImGui::GetColorU32 |
| `GetColorU32ImU32` | duplicate variant of ImGui::GetColorU32 |
| `GetColorU32ImU32Ex` | duplicate variant of ImGui::GetColorU32 |
| `GetStyleColorVec4` | unsupported return type: const ImVec4* |
| `SameLineEx` | duplicate variant of ImGui::SameLine |
| `IndentEx` | duplicate variant of ImGui::Indent |
| `UnindentEx` | duplicate variant of ImGui::Unindent |
| `PushIDStr` | duplicate variant of ImGui::PushID |
| `PushIDPtr` | duplicate variant of ImGui::PushID |
| `PushIDInt` | duplicate variant of ImGui::PushID |
| `GetIDStr` | duplicate variant of ImGui::GetID |
| `GetIDPtr` | duplicate variant of ImGui::GetID |
| `GetIDInt` | duplicate variant of ImGui::GetID |
| `TextUnformattedEx` | duplicate variant of ImGui::TextUnformatted |
| `Text` | hand-written override |
| `TextV` | unsupported parameter type: va_list |
| `TextColored` | hand-written override |
| `TextColoredV` | unsupported parameter type: va_list |
| `TextDisabled` | hand-written override |
| `TextDisabledV` | unsupported parameter type: va_list |
| `TextWrapped` | hand-written override |
| `TextWrappedV` | unsupported parameter type: va_list |
| `LabelText` | unsupported parameter type:  |
| `LabelTextV` | unsupported parameter type: va_list |
| `BulletText` | unsupported parameter type:  |
| `BulletTextV` | unsupported parameter type: va_list |
| `ButtonEx` | duplicate variant of ImGui::Button |
| `CheckboxFlagsUintPtr` | duplicate variant of ImGui::CheckboxFlags |
| `RadioButtonIntPtr` | duplicate variant of ImGui::RadioButton |
| `TextLinkOpenURLEx` | duplicate variant of ImGui::TextLinkOpenURL |
| `Image` | unsupported parameter type: ImTextureID |
| `ImageEx` | unsupported parameter type: ImTextureID |
| `ImageWithBg` | unsupported parameter type: ImTextureID |
| `ImageWithBgEx` | unsupported parameter type: ImTextureID |
| `ImageButton` | unsupported parameter type: ImTextureID |
| `ImageButtonEx` | unsupported parameter type: ImTextureID |
| `ComboChar` | unsupported parameter type: const char*const[] |
| `ComboCharEx` | unsupported parameter type: const char*const[] |
| `Combo` | hand-written override |
| `ComboCallback` | duplicate variant of ImGui::Combo |
| `ComboCallbackEx` | duplicate variant of ImGui::Combo |
| `DragFloatEx` | duplicate variant of ImGui::DragFloat |
| `DragFloat2` | unsupported parameter type: float[2] |
| `DragFloat2Ex` | unsupported parameter type: float[2] |
| `DragFloat3` | unsupported parameter type: float[3] |
| `DragFloat3Ex` | unsupported parameter type: float[3] |
| `DragFloat4` | unsupported parameter type: float[4] |
| `DragFloat4Ex` | unsupported parameter type: float[4] |
| `DragFloatRange2Ex` | duplicate variant of ImGui::DragFloatRange2 |
| `DragIntEx` | duplicate variant of ImGui::DragInt |
| `DragInt2` | unsupported parameter type: int[2] |
| `DragInt2Ex` | unsupported parameter type: int[2] |
| `DragInt3` | unsupported parameter type: int[3] |
| `DragInt3Ex` | unsupported parameter type: int[3] |
| `DragInt4` | unsupported parameter type: int[4] |
| `DragInt4Ex` | unsupported parameter type: int[4] |
| `DragIntRange2Ex` | duplicate variant of ImGui::DragIntRange2 |
| `DragScalar` | unsupported parameter type: void* |
| `DragScalarEx` | unsupported parameter type: void* |
| `DragScalarN` | unsupported parameter type: void* |
| `DragScalarNEx` | unsupported parameter type: void* |
| `SliderFloatEx` | duplicate variant of ImGui::SliderFloat |
| `SliderFloat2` | unsupported parameter type: float[2] |
| `SliderFloat2Ex` | unsupported parameter type: float[2] |
| `SliderFloat3` | unsupported parameter type: float[3] |
| `SliderFloat3Ex` | unsupported parameter type: float[3] |
| `SliderFloat4` | unsupported parameter type: float[4] |
| `SliderFloat4Ex` | unsupported parameter type: float[4] |
| `SliderAngleEx` | duplicate variant of ImGui::SliderAngle |
| `SliderIntEx` | duplicate variant of ImGui::SliderInt |
| `SliderInt2` | unsupported parameter type: int[2] |
| `SliderInt2Ex` | unsupported parameter type: int[2] |
| `SliderInt3` | unsupported parameter type: int[3] |
| `SliderInt3Ex` | unsupported parameter type: int[3] |
| `SliderInt4` | unsupported parameter type: int[4] |
| `SliderInt4Ex` | unsupported parameter type: int[4] |
| `SliderScalar` | unsupported parameter type: void* |
| `SliderScalarEx` | unsupported parameter type: void* |
| `SliderScalarN` | unsupported parameter type: void* |
| `SliderScalarNEx` | unsupported parameter type: void* |
| `VSliderFloatEx` | duplicate variant of ImGui::VSliderFloat |
| `VSliderIntEx` | duplicate variant of ImGui::VSliderInt |
| `VSliderScalar` | unsupported parameter type: void* |
| `VSliderScalarEx` | unsupported parameter type: void* |
| `InputText` | hand-written override |
| `InputTextEx` | unsupported parameter type: size_t |
| `InputTextMultiline` | hand-written override |
| `InputTextMultilineEx` | unsupported parameter type: size_t |
| `InputTextWithHint` | unsupported parameter type: size_t |
| `InputTextWithHintEx` | unsupported parameter type: size_t |
| `InputFloatEx` | duplicate variant of ImGui::InputFloat |
| `InputFloat2` | unsupported parameter type: float[2] |
| `InputFloat2Ex` | unsupported parameter type: float[2] |
| `InputFloat3` | unsupported parameter type: float[3] |
| `InputFloat3Ex` | unsupported parameter type: float[3] |
| `InputFloat4` | unsupported parameter type: float[4] |
| `InputFloat4Ex` | unsupported parameter type: float[4] |
| `InputIntEx` | duplicate variant of ImGui::InputInt |
| `InputInt2` | unsupported parameter type: int[2] |
| `InputInt3` | unsupported parameter type: int[3] |
| `InputInt4` | unsupported parameter type: int[4] |
| `InputDouble` | unsupported parameter type: double* |
| `InputDoubleEx` | unsupported parameter type: double* |
| `InputScalar` | unsupported parameter type: void* |
| `InputScalarEx` | unsupported parameter type: void* |
| `InputScalarN` | unsupported parameter type: void* |
| `InputScalarNEx` | unsupported parameter type: void* |
| `ColorEdit3` | unsupported parameter type: float[3] |
| `ColorEdit4` | unsupported parameter type: float[4] |
| `ColorPicker3` | unsupported parameter type: float[3] |
| `ColorPicker4` | unsupported parameter type: float[4] |
| `ColorButtonEx` | duplicate variant of ImGui::ColorButton |
| `TreeNodeStr` | duplicate variant of ImGui::TreeNode |
| `TreeNodePtr` | duplicate variant of ImGui::TreeNode |
| `TreeNodeV` | unsupported parameter type: va_list |
| `TreeNodeVPtr` | unsupported parameter type: const void* |
| `TreeNodeExStr` | duplicate variant of ImGui::TreeNodeEx |
| `TreeNodeExPtr` | duplicate variant of ImGui::TreeNodeEx |
| `TreeNodeExV` | unsupported parameter type: va_list |
| `TreeNodeExVPtr` | unsupported parameter type: const void* |
| `TreePushPtr` | duplicate variant of ImGui::TreePush |
| `CollapsingHeaderBoolPtr` | duplicate variant of ImGui::CollapsingHeader |
| `SelectableEx` | duplicate variant of ImGui::Selectable |
| `SelectableBoolPtr` | duplicate variant of ImGui::Selectable |
| `SelectableBoolPtrEx` | duplicate variant of ImGui::Selectable |
| `BeginMultiSelect` | unsupported return type: ImGuiMultiSelectIO* |
| `BeginMultiSelectEx` | unsupported return type: ImGuiMultiSelectIO* |
| `EndMultiSelect` | unsupported return type: ImGuiMultiSelectIO* |
| `SetNextItemSelectionUserData` | unsupported parameter type: ImGuiSelectionUserData |
| `ListBox` | hand-written override |
| `ListBoxCallback` | unsupported parameter type: const char* (*getter)(void* user_data, int idx) |
| `ListBoxCallbackEx` | unsupported parameter type: const char* (*getter)(void* user_data, int idx) |
| `PlotLines` | hand-written override |
| `PlotLinesEx` | hand-written override |
| `PlotLinesCallback` | unsupported parameter type: float (*values_getter)(void* data, int idx) |
| `PlotLinesCallbackEx` | unsupported parameter type: float (*values_getter)(void* data, int idx) |
| `PlotHistogram` | hand-written override |
| `PlotHistogramEx` | hand-written override |
| `PlotHistogramCallback` | unsupported parameter type: float (*values_getter)(void* data, int idx) |
| `PlotHistogramCallbackEx` | unsupported parameter type: float (*values_getter)(void* data, int idx) |
| `BeginMenuEx` | duplicate variant of ImGui::BeginMenu |
| `MenuItemEx` | duplicate variant of ImGui::MenuItem |
| `MenuItemBoolPtr` | duplicate variant of ImGui::MenuItem |
| `SetTooltip` | unsupported parameter type:  |
| `SetTooltipV` | unsupported parameter type: va_list |
| `SetItemTooltip` | unsupported parameter type:  |
| `SetItemTooltipV` | unsupported parameter type: va_list |
| `OpenPopupID` | duplicate variant of ImGui::OpenPopup |
| `BeginPopupContextItemEx` | duplicate variant of ImGui::BeginPopupContextItem |
| `BeginPopupContextWindowEx` | duplicate variant of ImGui::BeginPopupContextWindow |
| `BeginPopupContextVoidEx` | duplicate variant of ImGui::BeginPopupContextVoid |
| `BeginTableEx` | duplicate variant of ImGui::BeginTable |
| `TableNextRowEx` | duplicate variant of ImGui::TableNextRow |
| `TableSetupColumnEx` | duplicate variant of ImGui::TableSetupColumn |
| `TableGetSortSpecs` | unsupported return type: ImGuiTableSortSpecs* |
| `TableSetBgColor` | unsupported parameter type: ImGuiTableBgTarget |
| `ColumnsEx` | duplicate variant of ImGui::Columns |
| `LogText` | unsupported parameter type:  |
| `LogTextV` | unsupported parameter type: va_list |
| `SetDragDropPayload` | unsupported parameter type: const void* |
| `AcceptDragDropPayload` | unsupported return type: const ImGuiPayload* |
| `GetDragDropPayload` | unsupported return type: const ImGuiPayload* |
| `SetKeyboardFocusHereEx` | duplicate variant of ImGui::SetKeyboardFocusHere |
| `IsItemClickedEx` | duplicate variant of ImGui::IsItemClicked |
| `GetMainViewport` | unsupported return type: ImGuiViewport* |
| `GetBackgroundDrawList` | unsupported return type: ImDrawList* |
| `GetForegroundDrawList` | unsupported return type: ImDrawList* |
| `IsRectVisible` | duplicate variant of ImGui::IsRectVisible |
| `GetDrawListSharedData` | unsupported return type: ImDrawListSharedData* |
| `SetStateStorage` | unsupported parameter type: ImGuiStorage* |
| `GetStateStorage` | unsupported return type: ImGuiStorage* |
| `CalcTextSizeEx` | duplicate variant of ImGui::CalcTextSize |
| `ColorConvertRGBtoHSV` | hand-written override |
| `ColorConvertHSVtoRGB` | hand-written override |
| `IsKeyPressedEx` | duplicate variant of ImGui::IsKeyPressed |
| `IsMouseClickedEx` | duplicate variant of ImGui::IsMouseClicked |
| `IsMouseHoveringRectEx` | duplicate variant of ImGui::IsMouseHoveringRect |
| `IsMousePosValid` | unsupported parameter type: const ImVec2* |
| `ResetMouseDragDeltaEx` | duplicate variant of ImGui::ResetMouseDragDelta |
| `LoadIniSettingsFromMemory` | unsupported parameter type: size_t |
| `SaveIniSettingsToMemory` | unsupported parameter type: size_t* |
| `DebugCheckVersionAndDataLayout` | unsupported parameter type: size_t |
| `DebugLog` | unsupported parameter type:  |
| `DebugLogV` | unsupported parameter type: va_list |
| `SetAllocatorFunctions` | unsupported parameter type: void* |
| `GetAllocatorFunctions` | unsupported parameter type: void** |
| `MemAlloc` | unsupported parameter type: size_t |
| `MemFree` | unsupported parameter type: void* |
| `ImageImVec4` | unsupported parameter type: ImTextureID |
| `BeginChildFrameEx` | duplicate variant of ImGui::BeginChildFrame |
| `ComboObsolete` | duplicate variant of ImGui::Combo |
| `ComboObsoleteEx` | duplicate variant of ImGui::Combo |
| `ListBoxObsolete` | unsupported parameter type: bool (*old_callback)(void* user_data, int idx, const char** out_text) |
| `ListBoxObsoleteEx` | unsupported parameter type: bool (*old_callback)(void* user_data, int idx, const char** out_text) |

## Enums

Constants are accessible as `Imgui.Enum.<EnumName>.<Value>`, e.g.:

```lua
local flags = Imgui.Enum.ImGuiWindowFlags.NoTitleBar
local col   = Imgui.Enum.ImGuiCol.Button
```

| Lua path | C constant | Value |
|---|---|---|
| `Imgui.Enum.ImGuiWindowFlags.None` | `ImGuiWindowFlags_None` | `0` |
| `Imgui.Enum.ImGuiWindowFlags.NoTitleBar` | `ImGuiWindowFlags_NoTitleBar` | `1` |
| `Imgui.Enum.ImGuiWindowFlags.NoResize` | `ImGuiWindowFlags_NoResize` | `2` |
| `Imgui.Enum.ImGuiWindowFlags.NoMove` | `ImGuiWindowFlags_NoMove` | `4` |
| `Imgui.Enum.ImGuiWindowFlags.NoScrollbar` | `ImGuiWindowFlags_NoScrollbar` | `8` |
| `Imgui.Enum.ImGuiWindowFlags.NoScrollWithMouse` | `ImGuiWindowFlags_NoScrollWithMouse` | `16` |
| `Imgui.Enum.ImGuiWindowFlags.NoCollapse` | `ImGuiWindowFlags_NoCollapse` | `32` |
| `Imgui.Enum.ImGuiWindowFlags.AlwaysAutoResize` | `ImGuiWindowFlags_AlwaysAutoResize` | `64` |
| `Imgui.Enum.ImGuiWindowFlags.NoBackground` | `ImGuiWindowFlags_NoBackground` | `128` |
| `Imgui.Enum.ImGuiWindowFlags.NoSavedSettings` | `ImGuiWindowFlags_NoSavedSettings` | `256` |
| `Imgui.Enum.ImGuiWindowFlags.NoMouseInputs` | `ImGuiWindowFlags_NoMouseInputs` | `512` |
| `Imgui.Enum.ImGuiWindowFlags.MenuBar` | `ImGuiWindowFlags_MenuBar` | `1024` |
| `Imgui.Enum.ImGuiWindowFlags.HorizontalScrollbar` | `ImGuiWindowFlags_HorizontalScrollbar` | `2048` |
| `Imgui.Enum.ImGuiWindowFlags.NoFocusOnAppearing` | `ImGuiWindowFlags_NoFocusOnAppearing` | `4096` |
| `Imgui.Enum.ImGuiWindowFlags.NoBringToFrontOnFocus` | `ImGuiWindowFlags_NoBringToFrontOnFocus` | `8192` |
| `Imgui.Enum.ImGuiWindowFlags.AlwaysVerticalScrollbar` | `ImGuiWindowFlags_AlwaysVerticalScrollbar` | `16384` |
| `Imgui.Enum.ImGuiWindowFlags.AlwaysHorizontalScrollbar` | `ImGuiWindowFlags_AlwaysHorizontalScrollbar` | `32768` |
| `Imgui.Enum.ImGuiWindowFlags.NoNavInputs` | `ImGuiWindowFlags_NoNavInputs` | `65536` |
| `Imgui.Enum.ImGuiWindowFlags.NoNavFocus` | `ImGuiWindowFlags_NoNavFocus` | `131072` |
| `Imgui.Enum.ImGuiWindowFlags.UnsavedDocument` | `ImGuiWindowFlags_UnsavedDocument` | `262144` |
| `Imgui.Enum.ImGuiWindowFlags.NoNav` | `ImGuiWindowFlags_NoNav` | `196608` |
| `Imgui.Enum.ImGuiWindowFlags.NoDecoration` | `ImGuiWindowFlags_NoDecoration` | `43` |
| `Imgui.Enum.ImGuiWindowFlags.NoInputs` | `ImGuiWindowFlags_NoInputs` | `197120` |
| `Imgui.Enum.ImGuiWindowFlags.NavFlattened` | `ImGuiWindowFlags_NavFlattened` | `536870912` |
| `Imgui.Enum.ImGuiWindowFlags.AlwaysUseWindowPadding` | `ImGuiWindowFlags_AlwaysUseWindowPadding` | `1073741824` |
| `Imgui.Enum.ImGuiChildFlags.None` | `ImGuiChildFlags_None` | `0` |
| `Imgui.Enum.ImGuiChildFlags.Borders` | `ImGuiChildFlags_Borders` | `1` |
| `Imgui.Enum.ImGuiChildFlags.AlwaysUseWindowPadding` | `ImGuiChildFlags_AlwaysUseWindowPadding` | `2` |
| `Imgui.Enum.ImGuiChildFlags.ResizeX` | `ImGuiChildFlags_ResizeX` | `4` |
| `Imgui.Enum.ImGuiChildFlags.ResizeY` | `ImGuiChildFlags_ResizeY` | `8` |
| `Imgui.Enum.ImGuiChildFlags.AutoResizeX` | `ImGuiChildFlags_AutoResizeX` | `16` |
| `Imgui.Enum.ImGuiChildFlags.AutoResizeY` | `ImGuiChildFlags_AutoResizeY` | `32` |
| `Imgui.Enum.ImGuiChildFlags.AlwaysAutoResize` | `ImGuiChildFlags_AlwaysAutoResize` | `64` |
| `Imgui.Enum.ImGuiChildFlags.FrameStyle` | `ImGuiChildFlags_FrameStyle` | `128` |
| `Imgui.Enum.ImGuiChildFlags.NavFlattened` | `ImGuiChildFlags_NavFlattened` | `256` |
| `Imgui.Enum.ImGuiChildFlags.Border` | `ImGuiChildFlags_Border` | `1` |
| `Imgui.Enum.ImGuiItemFlags.None` | `ImGuiItemFlags_None` | `0` |
| `Imgui.Enum.ImGuiItemFlags.NoTabStop` | `ImGuiItemFlags_NoTabStop` | `1` |
| `Imgui.Enum.ImGuiItemFlags.NoNav` | `ImGuiItemFlags_NoNav` | `2` |
| `Imgui.Enum.ImGuiItemFlags.NoNavDefaultFocus` | `ImGuiItemFlags_NoNavDefaultFocus` | `4` |
| `Imgui.Enum.ImGuiItemFlags.ButtonRepeat` | `ImGuiItemFlags_ButtonRepeat` | `8` |
| `Imgui.Enum.ImGuiItemFlags.AutoClosePopups` | `ImGuiItemFlags_AutoClosePopups` | `16` |
| `Imgui.Enum.ImGuiItemFlags.AllowDuplicateId` | `ImGuiItemFlags_AllowDuplicateId` | `32` |
| `Imgui.Enum.ImGuiInputTextFlags.None` | `ImGuiInputTextFlags_None` | `0` |
| `Imgui.Enum.ImGuiInputTextFlags.CharsDecimal` | `ImGuiInputTextFlags_CharsDecimal` | `1` |
| `Imgui.Enum.ImGuiInputTextFlags.CharsHexadecimal` | `ImGuiInputTextFlags_CharsHexadecimal` | `2` |
| `Imgui.Enum.ImGuiInputTextFlags.CharsScientific` | `ImGuiInputTextFlags_CharsScientific` | `4` |
| `Imgui.Enum.ImGuiInputTextFlags.CharsUppercase` | `ImGuiInputTextFlags_CharsUppercase` | `8` |
| `Imgui.Enum.ImGuiInputTextFlags.CharsNoBlank` | `ImGuiInputTextFlags_CharsNoBlank` | `16` |
| `Imgui.Enum.ImGuiInputTextFlags.AllowTabInput` | `ImGuiInputTextFlags_AllowTabInput` | `32` |
| `Imgui.Enum.ImGuiInputTextFlags.EnterReturnsTrue` | `ImGuiInputTextFlags_EnterReturnsTrue` | `64` |
| `Imgui.Enum.ImGuiInputTextFlags.EscapeClearsAll` | `ImGuiInputTextFlags_EscapeClearsAll` | `128` |
| `Imgui.Enum.ImGuiInputTextFlags.CtrlEnterForNewLine` | `ImGuiInputTextFlags_CtrlEnterForNewLine` | `256` |
| `Imgui.Enum.ImGuiInputTextFlags.ReadOnly` | `ImGuiInputTextFlags_ReadOnly` | `512` |
| `Imgui.Enum.ImGuiInputTextFlags.Password` | `ImGuiInputTextFlags_Password` | `1024` |
| `Imgui.Enum.ImGuiInputTextFlags.AlwaysOverwrite` | `ImGuiInputTextFlags_AlwaysOverwrite` | `2048` |
| `Imgui.Enum.ImGuiInputTextFlags.AutoSelectAll` | `ImGuiInputTextFlags_AutoSelectAll` | `4096` |
| `Imgui.Enum.ImGuiInputTextFlags.ParseEmptyRefVal` | `ImGuiInputTextFlags_ParseEmptyRefVal` | `8192` |
| `Imgui.Enum.ImGuiInputTextFlags.DisplayEmptyRefVal` | `ImGuiInputTextFlags_DisplayEmptyRefVal` | `16384` |
| `Imgui.Enum.ImGuiInputTextFlags.NoHorizontalScroll` | `ImGuiInputTextFlags_NoHorizontalScroll` | `32768` |
| `Imgui.Enum.ImGuiInputTextFlags.NoUndoRedo` | `ImGuiInputTextFlags_NoUndoRedo` | `65536` |
| `Imgui.Enum.ImGuiInputTextFlags.ElideLeft` | `ImGuiInputTextFlags_ElideLeft` | `131072` |
| `Imgui.Enum.ImGuiInputTextFlags.CallbackCompletion` | `ImGuiInputTextFlags_CallbackCompletion` | `262144` |
| `Imgui.Enum.ImGuiInputTextFlags.CallbackHistory` | `ImGuiInputTextFlags_CallbackHistory` | `524288` |
| `Imgui.Enum.ImGuiInputTextFlags.CallbackAlways` | `ImGuiInputTextFlags_CallbackAlways` | `1048576` |
| `Imgui.Enum.ImGuiInputTextFlags.CallbackCharFilter` | `ImGuiInputTextFlags_CallbackCharFilter` | `2097152` |
| `Imgui.Enum.ImGuiInputTextFlags.CallbackResize` | `ImGuiInputTextFlags_CallbackResize` | `4194304` |
| `Imgui.Enum.ImGuiInputTextFlags.CallbackEdit` | `ImGuiInputTextFlags_CallbackEdit` | `8388608` |
| `Imgui.Enum.ImGuiTreeNodeFlags.None` | `ImGuiTreeNodeFlags_None` | `0` |
| `Imgui.Enum.ImGuiTreeNodeFlags.Selected` | `ImGuiTreeNodeFlags_Selected` | `1` |
| `Imgui.Enum.ImGuiTreeNodeFlags.Framed` | `ImGuiTreeNodeFlags_Framed` | `2` |
| `Imgui.Enum.ImGuiTreeNodeFlags.AllowOverlap` | `ImGuiTreeNodeFlags_AllowOverlap` | `4` |
| `Imgui.Enum.ImGuiTreeNodeFlags.NoTreePushOnOpen` | `ImGuiTreeNodeFlags_NoTreePushOnOpen` | `8` |
| `Imgui.Enum.ImGuiTreeNodeFlags.NoAutoOpenOnLog` | `ImGuiTreeNodeFlags_NoAutoOpenOnLog` | `16` |
| `Imgui.Enum.ImGuiTreeNodeFlags.DefaultOpen` | `ImGuiTreeNodeFlags_DefaultOpen` | `32` |
| `Imgui.Enum.ImGuiTreeNodeFlags.OpenOnDoubleClick` | `ImGuiTreeNodeFlags_OpenOnDoubleClick` | `64` |
| `Imgui.Enum.ImGuiTreeNodeFlags.OpenOnArrow` | `ImGuiTreeNodeFlags_OpenOnArrow` | `128` |
| `Imgui.Enum.ImGuiTreeNodeFlags.Leaf` | `ImGuiTreeNodeFlags_Leaf` | `256` |
| `Imgui.Enum.ImGuiTreeNodeFlags.Bullet` | `ImGuiTreeNodeFlags_Bullet` | `512` |
| `Imgui.Enum.ImGuiTreeNodeFlags.FramePadding` | `ImGuiTreeNodeFlags_FramePadding` | `1024` |
| `Imgui.Enum.ImGuiTreeNodeFlags.SpanAvailWidth` | `ImGuiTreeNodeFlags_SpanAvailWidth` | `2048` |
| `Imgui.Enum.ImGuiTreeNodeFlags.SpanFullWidth` | `ImGuiTreeNodeFlags_SpanFullWidth` | `4096` |
| `Imgui.Enum.ImGuiTreeNodeFlags.SpanLabelWidth` | `ImGuiTreeNodeFlags_SpanLabelWidth` | `8192` |
| `Imgui.Enum.ImGuiTreeNodeFlags.SpanAllColumns` | `ImGuiTreeNodeFlags_SpanAllColumns` | `16384` |
| `Imgui.Enum.ImGuiTreeNodeFlags.LabelSpanAllColumns` | `ImGuiTreeNodeFlags_LabelSpanAllColumns` | `32768` |
| `Imgui.Enum.ImGuiTreeNodeFlags.NavLeftJumpsBackHere` | `ImGuiTreeNodeFlags_NavLeftJumpsBackHere` | `131072` |
| `Imgui.Enum.ImGuiTreeNodeFlags.CollapsingHeader` | `ImGuiTreeNodeFlags_CollapsingHeader` | `26` |
| `Imgui.Enum.ImGuiTreeNodeFlags.AllowItemOverlap` | `ImGuiTreeNodeFlags_AllowItemOverlap` | `4` |
| `Imgui.Enum.ImGuiTreeNodeFlags.SpanTextWidth` | `ImGuiTreeNodeFlags_SpanTextWidth` | `8192` |
| `Imgui.Enum.ImGuiPopupFlags.None` | `ImGuiPopupFlags_None` | `0` |
| `Imgui.Enum.ImGuiPopupFlags.MouseButtonLeft` | `ImGuiPopupFlags_MouseButtonLeft` | `0` |
| `Imgui.Enum.ImGuiPopupFlags.MouseButtonRight` | `ImGuiPopupFlags_MouseButtonRight` | `1` |
| `Imgui.Enum.ImGuiPopupFlags.MouseButtonMiddle` | `ImGuiPopupFlags_MouseButtonMiddle` | `2` |
| `Imgui.Enum.ImGuiPopupFlags.NoReopen` | `ImGuiPopupFlags_NoReopen` | `32` |
| `Imgui.Enum.ImGuiPopupFlags.NoOpenOverExistingPopup` | `ImGuiPopupFlags_NoOpenOverExistingPopup` | `128` |
| `Imgui.Enum.ImGuiPopupFlags.NoOpenOverItems` | `ImGuiPopupFlags_NoOpenOverItems` | `256` |
| `Imgui.Enum.ImGuiPopupFlags.AnyPopupId` | `ImGuiPopupFlags_AnyPopupId` | `1024` |
| `Imgui.Enum.ImGuiPopupFlags.AnyPopupLevel` | `ImGuiPopupFlags_AnyPopupLevel` | `2048` |
| `Imgui.Enum.ImGuiPopupFlags.AnyPopup` | `ImGuiPopupFlags_AnyPopup` | `3072` |
| `Imgui.Enum.ImGuiSelectableFlags.None` | `ImGuiSelectableFlags_None` | `0` |
| `Imgui.Enum.ImGuiSelectableFlags.NoAutoClosePopups` | `ImGuiSelectableFlags_NoAutoClosePopups` | `1` |
| `Imgui.Enum.ImGuiSelectableFlags.SpanAllColumns` | `ImGuiSelectableFlags_SpanAllColumns` | `2` |
| `Imgui.Enum.ImGuiSelectableFlags.AllowDoubleClick` | `ImGuiSelectableFlags_AllowDoubleClick` | `4` |
| `Imgui.Enum.ImGuiSelectableFlags.Disabled` | `ImGuiSelectableFlags_Disabled` | `8` |
| `Imgui.Enum.ImGuiSelectableFlags.AllowOverlap` | `ImGuiSelectableFlags_AllowOverlap` | `16` |
| `Imgui.Enum.ImGuiSelectableFlags.Highlight` | `ImGuiSelectableFlags_Highlight` | `32` |
| `Imgui.Enum.ImGuiSelectableFlags.DontClosePopups` | `ImGuiSelectableFlags_DontClosePopups` | `1` |
| `Imgui.Enum.ImGuiSelectableFlags.AllowItemOverlap` | `ImGuiSelectableFlags_AllowItemOverlap` | `16` |
| `Imgui.Enum.ImGuiComboFlags.None` | `ImGuiComboFlags_None` | `0` |
| `Imgui.Enum.ImGuiComboFlags.PopupAlignLeft` | `ImGuiComboFlags_PopupAlignLeft` | `1` |
| `Imgui.Enum.ImGuiComboFlags.HeightSmall` | `ImGuiComboFlags_HeightSmall` | `2` |
| `Imgui.Enum.ImGuiComboFlags.HeightRegular` | `ImGuiComboFlags_HeightRegular` | `4` |
| `Imgui.Enum.ImGuiComboFlags.HeightLarge` | `ImGuiComboFlags_HeightLarge` | `8` |
| `Imgui.Enum.ImGuiComboFlags.HeightLargest` | `ImGuiComboFlags_HeightLargest` | `16` |
| `Imgui.Enum.ImGuiComboFlags.NoArrowButton` | `ImGuiComboFlags_NoArrowButton` | `32` |
| `Imgui.Enum.ImGuiComboFlags.NoPreview` | `ImGuiComboFlags_NoPreview` | `64` |
| `Imgui.Enum.ImGuiComboFlags.WidthFitPreview` | `ImGuiComboFlags_WidthFitPreview` | `128` |
| `Imgui.Enum.ImGuiTabBarFlags.None` | `ImGuiTabBarFlags_None` | `0` |
| `Imgui.Enum.ImGuiTabBarFlags.Reorderable` | `ImGuiTabBarFlags_Reorderable` | `1` |
| `Imgui.Enum.ImGuiTabBarFlags.AutoSelectNewTabs` | `ImGuiTabBarFlags_AutoSelectNewTabs` | `2` |
| `Imgui.Enum.ImGuiTabBarFlags.TabListPopupButton` | `ImGuiTabBarFlags_TabListPopupButton` | `4` |
| `Imgui.Enum.ImGuiTabBarFlags.NoCloseWithMiddleMouseButton` | `ImGuiTabBarFlags_NoCloseWithMiddleMouseButton` | `8` |
| `Imgui.Enum.ImGuiTabBarFlags.NoTabListScrollingButtons` | `ImGuiTabBarFlags_NoTabListScrollingButtons` | `16` |
| `Imgui.Enum.ImGuiTabBarFlags.NoTooltip` | `ImGuiTabBarFlags_NoTooltip` | `32` |
| `Imgui.Enum.ImGuiTabBarFlags.DrawSelectedOverline` | `ImGuiTabBarFlags_DrawSelectedOverline` | `64` |
| `Imgui.Enum.ImGuiTabBarFlags.FittingPolicyResizeDown` | `ImGuiTabBarFlags_FittingPolicyResizeDown` | `128` |
| `Imgui.Enum.ImGuiTabBarFlags.FittingPolicyScroll` | `ImGuiTabBarFlags_FittingPolicyScroll` | `256` |
| `Imgui.Enum.ImGuiTabItemFlags.None` | `ImGuiTabItemFlags_None` | `0` |
| `Imgui.Enum.ImGuiTabItemFlags.UnsavedDocument` | `ImGuiTabItemFlags_UnsavedDocument` | `1` |
| `Imgui.Enum.ImGuiTabItemFlags.SetSelected` | `ImGuiTabItemFlags_SetSelected` | `2` |
| `Imgui.Enum.ImGuiTabItemFlags.NoCloseWithMiddleMouseButton` | `ImGuiTabItemFlags_NoCloseWithMiddleMouseButton` | `4` |
| `Imgui.Enum.ImGuiTabItemFlags.NoPushId` | `ImGuiTabItemFlags_NoPushId` | `8` |
| `Imgui.Enum.ImGuiTabItemFlags.NoTooltip` | `ImGuiTabItemFlags_NoTooltip` | `16` |
| `Imgui.Enum.ImGuiTabItemFlags.NoReorder` | `ImGuiTabItemFlags_NoReorder` | `32` |
| `Imgui.Enum.ImGuiTabItemFlags.Leading` | `ImGuiTabItemFlags_Leading` | `64` |
| `Imgui.Enum.ImGuiTabItemFlags.Trailing` | `ImGuiTabItemFlags_Trailing` | `128` |
| `Imgui.Enum.ImGuiTabItemFlags.NoAssumedClosure` | `ImGuiTabItemFlags_NoAssumedClosure` | `256` |
| `Imgui.Enum.ImGuiFocusedFlags.None` | `ImGuiFocusedFlags_None` | `0` |
| `Imgui.Enum.ImGuiFocusedFlags.ChildWindows` | `ImGuiFocusedFlags_ChildWindows` | `1` |
| `Imgui.Enum.ImGuiFocusedFlags.RootWindow` | `ImGuiFocusedFlags_RootWindow` | `2` |
| `Imgui.Enum.ImGuiFocusedFlags.AnyWindow` | `ImGuiFocusedFlags_AnyWindow` | `4` |
| `Imgui.Enum.ImGuiFocusedFlags.NoPopupHierarchy` | `ImGuiFocusedFlags_NoPopupHierarchy` | `8` |
| `Imgui.Enum.ImGuiFocusedFlags.RootAndChildWindows` | `ImGuiFocusedFlags_RootAndChildWindows` | `3` |
| `Imgui.Enum.ImGuiHoveredFlags.None` | `ImGuiHoveredFlags_None` | `0` |
| `Imgui.Enum.ImGuiHoveredFlags.ChildWindows` | `ImGuiHoveredFlags_ChildWindows` | `1` |
| `Imgui.Enum.ImGuiHoveredFlags.RootWindow` | `ImGuiHoveredFlags_RootWindow` | `2` |
| `Imgui.Enum.ImGuiHoveredFlags.AnyWindow` | `ImGuiHoveredFlags_AnyWindow` | `4` |
| `Imgui.Enum.ImGuiHoveredFlags.NoPopupHierarchy` | `ImGuiHoveredFlags_NoPopupHierarchy` | `8` |
| `Imgui.Enum.ImGuiHoveredFlags.AllowWhenBlockedByPopup` | `ImGuiHoveredFlags_AllowWhenBlockedByPopup` | `32` |
| `Imgui.Enum.ImGuiHoveredFlags.AllowWhenBlockedByActiveItem` | `ImGuiHoveredFlags_AllowWhenBlockedByActiveItem` | `128` |
| `Imgui.Enum.ImGuiHoveredFlags.AllowWhenOverlappedByItem` | `ImGuiHoveredFlags_AllowWhenOverlappedByItem` | `256` |
| `Imgui.Enum.ImGuiHoveredFlags.AllowWhenOverlappedByWindow` | `ImGuiHoveredFlags_AllowWhenOverlappedByWindow` | `512` |
| `Imgui.Enum.ImGuiHoveredFlags.AllowWhenDisabled` | `ImGuiHoveredFlags_AllowWhenDisabled` | `1024` |
| `Imgui.Enum.ImGuiHoveredFlags.NoNavOverride` | `ImGuiHoveredFlags_NoNavOverride` | `2048` |
| `Imgui.Enum.ImGuiHoveredFlags.AllowWhenOverlapped` | `ImGuiHoveredFlags_AllowWhenOverlapped` | `768` |
| `Imgui.Enum.ImGuiHoveredFlags.RectOnly` | `ImGuiHoveredFlags_RectOnly` | `928` |
| `Imgui.Enum.ImGuiHoveredFlags.RootAndChildWindows` | `ImGuiHoveredFlags_RootAndChildWindows` | `3` |
| `Imgui.Enum.ImGuiHoveredFlags.ForTooltip` | `ImGuiHoveredFlags_ForTooltip` | `4096` |
| `Imgui.Enum.ImGuiHoveredFlags.Stationary` | `ImGuiHoveredFlags_Stationary` | `8192` |
| `Imgui.Enum.ImGuiHoveredFlags.DelayNone` | `ImGuiHoveredFlags_DelayNone` | `16384` |
| `Imgui.Enum.ImGuiHoveredFlags.DelayShort` | `ImGuiHoveredFlags_DelayShort` | `32768` |
| `Imgui.Enum.ImGuiHoveredFlags.DelayNormal` | `ImGuiHoveredFlags_DelayNormal` | `65536` |
| `Imgui.Enum.ImGuiHoveredFlags.NoSharedDelay` | `ImGuiHoveredFlags_NoSharedDelay` | `131072` |
| `Imgui.Enum.ImGuiDragDropFlags.None` | `ImGuiDragDropFlags_None` | `0` |
| `Imgui.Enum.ImGuiDragDropFlags.SourceNoPreviewTooltip` | `ImGuiDragDropFlags_SourceNoPreviewTooltip` | `1` |
| `Imgui.Enum.ImGuiDragDropFlags.SourceNoDisableHover` | `ImGuiDragDropFlags_SourceNoDisableHover` | `2` |
| `Imgui.Enum.ImGuiDragDropFlags.SourceNoHoldToOpenOthers` | `ImGuiDragDropFlags_SourceNoHoldToOpenOthers` | `4` |
| `Imgui.Enum.ImGuiDragDropFlags.SourceAllowNullID` | `ImGuiDragDropFlags_SourceAllowNullID` | `8` |
| `Imgui.Enum.ImGuiDragDropFlags.SourceExtern` | `ImGuiDragDropFlags_SourceExtern` | `16` |
| `Imgui.Enum.ImGuiDragDropFlags.PayloadAutoExpire` | `ImGuiDragDropFlags_PayloadAutoExpire` | `32` |
| `Imgui.Enum.ImGuiDragDropFlags.PayloadNoCrossContext` | `ImGuiDragDropFlags_PayloadNoCrossContext` | `64` |
| `Imgui.Enum.ImGuiDragDropFlags.PayloadNoCrossProcess` | `ImGuiDragDropFlags_PayloadNoCrossProcess` | `128` |
| `Imgui.Enum.ImGuiDragDropFlags.AcceptBeforeDelivery` | `ImGuiDragDropFlags_AcceptBeforeDelivery` | `1024` |
| `Imgui.Enum.ImGuiDragDropFlags.AcceptNoDrawDefaultRect` | `ImGuiDragDropFlags_AcceptNoDrawDefaultRect` | `2048` |
| `Imgui.Enum.ImGuiDragDropFlags.AcceptNoPreviewTooltip` | `ImGuiDragDropFlags_AcceptNoPreviewTooltip` | `4096` |
| `Imgui.Enum.ImGuiDragDropFlags.AcceptPeekOnly` | `ImGuiDragDropFlags_AcceptPeekOnly` | `3072` |
| `Imgui.Enum.ImGuiDragDropFlags.SourceAutoExpirePayload` | `ImGuiDragDropFlags_SourceAutoExpirePayload` | `32` |
| `Imgui.Enum.ImGuiDataType.S8` | `ImGuiDataType_S8` | `0` |
| `Imgui.Enum.ImGuiDataType.U8` | `ImGuiDataType_U8` | `1` |
| `Imgui.Enum.ImGuiDataType.S16` | `ImGuiDataType_S16` | `2` |
| `Imgui.Enum.ImGuiDataType.U16` | `ImGuiDataType_U16` | `3` |
| `Imgui.Enum.ImGuiDataType.S32` | `ImGuiDataType_S32` | `4` |
| `Imgui.Enum.ImGuiDataType.U32` | `ImGuiDataType_U32` | `5` |
| `Imgui.Enum.ImGuiDataType.S64` | `ImGuiDataType_S64` | `6` |
| `Imgui.Enum.ImGuiDataType.U64` | `ImGuiDataType_U64` | `7` |
| `Imgui.Enum.ImGuiDataType.Float` | `ImGuiDataType_Float` | `8` |
| `Imgui.Enum.ImGuiDataType.Double` | `ImGuiDataType_Double` | `9` |
| `Imgui.Enum.ImGuiDataType.Bool` | `ImGuiDataType_Bool` | `10` |
| `Imgui.Enum.ImGuiDataType.String` | `ImGuiDataType_String` | `11` |
| `Imgui.Enum.ImGuiDir._None` | `ImGuiDir_None` | `-1` |
| `Imgui.Enum.ImGuiDir._Left` | `ImGuiDir_Left` | `0` |
| `Imgui.Enum.ImGuiDir._Right` | `ImGuiDir_Right` | `1` |
| `Imgui.Enum.ImGuiDir._Up` | `ImGuiDir_Up` | `2` |
| `Imgui.Enum.ImGuiDir._Down` | `ImGuiDir_Down` | `3` |
| `Imgui.Enum.ImGuiSortDirection._None` | `ImGuiSortDirection_None` | `0` |
| `Imgui.Enum.ImGuiSortDirection._Ascending` | `ImGuiSortDirection_Ascending` | `1` |
| `Imgui.Enum.ImGuiSortDirection._Descending` | `ImGuiSortDirection_Descending` | `2` |
| `Imgui.Enum.ImGuiKey._None` | `ImGuiKey_None` | `0` |
| `Imgui.Enum.ImGuiKey._NamedKey_BEGIN` | `ImGuiKey_NamedKey_BEGIN` | `512` |
| `Imgui.Enum.ImGuiKey._Tab` | `ImGuiKey_Tab` | `512` |
| `Imgui.Enum.ImGuiKey._LeftArrow` | `ImGuiKey_LeftArrow` | `513` |
| `Imgui.Enum.ImGuiKey._RightArrow` | `ImGuiKey_RightArrow` | `514` |
| `Imgui.Enum.ImGuiKey._UpArrow` | `ImGuiKey_UpArrow` | `515` |
| `Imgui.Enum.ImGuiKey._DownArrow` | `ImGuiKey_DownArrow` | `516` |
| `Imgui.Enum.ImGuiKey._PageUp` | `ImGuiKey_PageUp` | `517` |
| `Imgui.Enum.ImGuiKey._PageDown` | `ImGuiKey_PageDown` | `518` |
| `Imgui.Enum.ImGuiKey._Home` | `ImGuiKey_Home` | `519` |
| `Imgui.Enum.ImGuiKey._End` | `ImGuiKey_End` | `520` |
| `Imgui.Enum.ImGuiKey._Insert` | `ImGuiKey_Insert` | `521` |
| `Imgui.Enum.ImGuiKey._Delete` | `ImGuiKey_Delete` | `522` |
| `Imgui.Enum.ImGuiKey._Backspace` | `ImGuiKey_Backspace` | `523` |
| `Imgui.Enum.ImGuiKey._Space` | `ImGuiKey_Space` | `524` |
| `Imgui.Enum.ImGuiKey._Enter` | `ImGuiKey_Enter` | `525` |
| `Imgui.Enum.ImGuiKey._Escape` | `ImGuiKey_Escape` | `526` |
| `Imgui.Enum.ImGuiKey._LeftCtrl` | `ImGuiKey_LeftCtrl` | `527` |
| `Imgui.Enum.ImGuiKey._LeftShift` | `ImGuiKey_LeftShift` | `528` |
| `Imgui.Enum.ImGuiKey._LeftAlt` | `ImGuiKey_LeftAlt` | `529` |
| `Imgui.Enum.ImGuiKey._LeftSuper` | `ImGuiKey_LeftSuper` | `530` |
| `Imgui.Enum.ImGuiKey._RightCtrl` | `ImGuiKey_RightCtrl` | `531` |
| `Imgui.Enum.ImGuiKey._RightShift` | `ImGuiKey_RightShift` | `532` |
| `Imgui.Enum.ImGuiKey._RightAlt` | `ImGuiKey_RightAlt` | `533` |
| `Imgui.Enum.ImGuiKey._RightSuper` | `ImGuiKey_RightSuper` | `534` |
| `Imgui.Enum.ImGuiKey._Menu` | `ImGuiKey_Menu` | `535` |
| `Imgui.Enum.ImGuiKey._0` | `ImGuiKey_0` | `536` |
| `Imgui.Enum.ImGuiKey._1` | `ImGuiKey_1` | `537` |
| `Imgui.Enum.ImGuiKey._2` | `ImGuiKey_2` | `538` |
| `Imgui.Enum.ImGuiKey._3` | `ImGuiKey_3` | `539` |
| `Imgui.Enum.ImGuiKey._4` | `ImGuiKey_4` | `540` |
| `Imgui.Enum.ImGuiKey._5` | `ImGuiKey_5` | `541` |
| `Imgui.Enum.ImGuiKey._6` | `ImGuiKey_6` | `542` |
| `Imgui.Enum.ImGuiKey._7` | `ImGuiKey_7` | `543` |
| `Imgui.Enum.ImGuiKey._8` | `ImGuiKey_8` | `544` |
| `Imgui.Enum.ImGuiKey._9` | `ImGuiKey_9` | `545` |
| `Imgui.Enum.ImGuiKey._A` | `ImGuiKey_A` | `546` |
| `Imgui.Enum.ImGuiKey._B` | `ImGuiKey_B` | `547` |
| `Imgui.Enum.ImGuiKey._C` | `ImGuiKey_C` | `548` |
| `Imgui.Enum.ImGuiKey._D` | `ImGuiKey_D` | `549` |
| `Imgui.Enum.ImGuiKey._E` | `ImGuiKey_E` | `550` |
| `Imgui.Enum.ImGuiKey._F` | `ImGuiKey_F` | `551` |
| `Imgui.Enum.ImGuiKey._G` | `ImGuiKey_G` | `552` |
| `Imgui.Enum.ImGuiKey._H` | `ImGuiKey_H` | `553` |
| `Imgui.Enum.ImGuiKey._I` | `ImGuiKey_I` | `554` |
| `Imgui.Enum.ImGuiKey._J` | `ImGuiKey_J` | `555` |
| `Imgui.Enum.ImGuiKey._K` | `ImGuiKey_K` | `556` |
| `Imgui.Enum.ImGuiKey._L` | `ImGuiKey_L` | `557` |
| `Imgui.Enum.ImGuiKey._M` | `ImGuiKey_M` | `558` |
| `Imgui.Enum.ImGuiKey._N` | `ImGuiKey_N` | `559` |
| `Imgui.Enum.ImGuiKey._O` | `ImGuiKey_O` | `560` |
| `Imgui.Enum.ImGuiKey._P` | `ImGuiKey_P` | `561` |
| `Imgui.Enum.ImGuiKey._Q` | `ImGuiKey_Q` | `562` |
| `Imgui.Enum.ImGuiKey._R` | `ImGuiKey_R` | `563` |
| `Imgui.Enum.ImGuiKey._S` | `ImGuiKey_S` | `564` |
| `Imgui.Enum.ImGuiKey._T` | `ImGuiKey_T` | `565` |
| `Imgui.Enum.ImGuiKey._U` | `ImGuiKey_U` | `566` |
| `Imgui.Enum.ImGuiKey._V` | `ImGuiKey_V` | `567` |
| `Imgui.Enum.ImGuiKey._W` | `ImGuiKey_W` | `568` |
| `Imgui.Enum.ImGuiKey._X` | `ImGuiKey_X` | `569` |
| `Imgui.Enum.ImGuiKey._Y` | `ImGuiKey_Y` | `570` |
| `Imgui.Enum.ImGuiKey._Z` | `ImGuiKey_Z` | `571` |
| `Imgui.Enum.ImGuiKey._F1` | `ImGuiKey_F1` | `572` |
| `Imgui.Enum.ImGuiKey._F2` | `ImGuiKey_F2` | `573` |
| `Imgui.Enum.ImGuiKey._F3` | `ImGuiKey_F3` | `574` |
| `Imgui.Enum.ImGuiKey._F4` | `ImGuiKey_F4` | `575` |
| `Imgui.Enum.ImGuiKey._F5` | `ImGuiKey_F5` | `576` |
| `Imgui.Enum.ImGuiKey._F6` | `ImGuiKey_F6` | `577` |
| `Imgui.Enum.ImGuiKey._F7` | `ImGuiKey_F7` | `578` |
| `Imgui.Enum.ImGuiKey._F8` | `ImGuiKey_F8` | `579` |
| `Imgui.Enum.ImGuiKey._F9` | `ImGuiKey_F9` | `580` |
| `Imgui.Enum.ImGuiKey._F10` | `ImGuiKey_F10` | `581` |
| `Imgui.Enum.ImGuiKey._F11` | `ImGuiKey_F11` | `582` |
| `Imgui.Enum.ImGuiKey._F12` | `ImGuiKey_F12` | `583` |
| `Imgui.Enum.ImGuiKey._F13` | `ImGuiKey_F13` | `584` |
| `Imgui.Enum.ImGuiKey._F14` | `ImGuiKey_F14` | `585` |
| `Imgui.Enum.ImGuiKey._F15` | `ImGuiKey_F15` | `586` |
| `Imgui.Enum.ImGuiKey._F16` | `ImGuiKey_F16` | `587` |
| `Imgui.Enum.ImGuiKey._F17` | `ImGuiKey_F17` | `588` |
| `Imgui.Enum.ImGuiKey._F18` | `ImGuiKey_F18` | `589` |
| `Imgui.Enum.ImGuiKey._F19` | `ImGuiKey_F19` | `590` |
| `Imgui.Enum.ImGuiKey._F20` | `ImGuiKey_F20` | `591` |
| `Imgui.Enum.ImGuiKey._F21` | `ImGuiKey_F21` | `592` |
| `Imgui.Enum.ImGuiKey._F22` | `ImGuiKey_F22` | `593` |
| `Imgui.Enum.ImGuiKey._F23` | `ImGuiKey_F23` | `594` |
| `Imgui.Enum.ImGuiKey._F24` | `ImGuiKey_F24` | `595` |
| `Imgui.Enum.ImGuiKey._Apostrophe` | `ImGuiKey_Apostrophe` | `596` |
| `Imgui.Enum.ImGuiKey._Comma` | `ImGuiKey_Comma` | `597` |
| `Imgui.Enum.ImGuiKey._Minus` | `ImGuiKey_Minus` | `598` |
| `Imgui.Enum.ImGuiKey._Period` | `ImGuiKey_Period` | `599` |
| `Imgui.Enum.ImGuiKey._Slash` | `ImGuiKey_Slash` | `600` |
| `Imgui.Enum.ImGuiKey._Semicolon` | `ImGuiKey_Semicolon` | `601` |
| `Imgui.Enum.ImGuiKey._Equal` | `ImGuiKey_Equal` | `602` |
| `Imgui.Enum.ImGuiKey._LeftBracket` | `ImGuiKey_LeftBracket` | `603` |
| `Imgui.Enum.ImGuiKey._Backslash` | `ImGuiKey_Backslash` | `604` |
| `Imgui.Enum.ImGuiKey._RightBracket` | `ImGuiKey_RightBracket` | `605` |
| `Imgui.Enum.ImGuiKey._GraveAccent` | `ImGuiKey_GraveAccent` | `606` |
| `Imgui.Enum.ImGuiKey._CapsLock` | `ImGuiKey_CapsLock` | `607` |
| `Imgui.Enum.ImGuiKey._ScrollLock` | `ImGuiKey_ScrollLock` | `608` |
| `Imgui.Enum.ImGuiKey._NumLock` | `ImGuiKey_NumLock` | `609` |
| `Imgui.Enum.ImGuiKey._PrintScreen` | `ImGuiKey_PrintScreen` | `610` |
| `Imgui.Enum.ImGuiKey._Pause` | `ImGuiKey_Pause` | `611` |
| `Imgui.Enum.ImGuiKey._Keypad0` | `ImGuiKey_Keypad0` | `612` |
| `Imgui.Enum.ImGuiKey._Keypad1` | `ImGuiKey_Keypad1` | `613` |
| `Imgui.Enum.ImGuiKey._Keypad2` | `ImGuiKey_Keypad2` | `614` |
| `Imgui.Enum.ImGuiKey._Keypad3` | `ImGuiKey_Keypad3` | `615` |
| `Imgui.Enum.ImGuiKey._Keypad4` | `ImGuiKey_Keypad4` | `616` |
| `Imgui.Enum.ImGuiKey._Keypad5` | `ImGuiKey_Keypad5` | `617` |
| `Imgui.Enum.ImGuiKey._Keypad6` | `ImGuiKey_Keypad6` | `618` |
| `Imgui.Enum.ImGuiKey._Keypad7` | `ImGuiKey_Keypad7` | `619` |
| `Imgui.Enum.ImGuiKey._Keypad8` | `ImGuiKey_Keypad8` | `620` |
| `Imgui.Enum.ImGuiKey._Keypad9` | `ImGuiKey_Keypad9` | `621` |
| `Imgui.Enum.ImGuiKey._KeypadDecimal` | `ImGuiKey_KeypadDecimal` | `622` |
| `Imgui.Enum.ImGuiKey._KeypadDivide` | `ImGuiKey_KeypadDivide` | `623` |
| `Imgui.Enum.ImGuiKey._KeypadMultiply` | `ImGuiKey_KeypadMultiply` | `624` |
| `Imgui.Enum.ImGuiKey._KeypadSubtract` | `ImGuiKey_KeypadSubtract` | `625` |
| `Imgui.Enum.ImGuiKey._KeypadAdd` | `ImGuiKey_KeypadAdd` | `626` |
| `Imgui.Enum.ImGuiKey._KeypadEnter` | `ImGuiKey_KeypadEnter` | `627` |
| `Imgui.Enum.ImGuiKey._KeypadEqual` | `ImGuiKey_KeypadEqual` | `628` |
| `Imgui.Enum.ImGuiKey._AppBack` | `ImGuiKey_AppBack` | `629` |
| `Imgui.Enum.ImGuiKey._AppForward` | `ImGuiKey_AppForward` | `630` |
| `Imgui.Enum.ImGuiKey._Oem102` | `ImGuiKey_Oem102` | `631` |
| `Imgui.Enum.ImGuiKey._GamepadStart` | `ImGuiKey_GamepadStart` | `632` |
| `Imgui.Enum.ImGuiKey._GamepadBack` | `ImGuiKey_GamepadBack` | `633` |
| `Imgui.Enum.ImGuiKey._GamepadFaceLeft` | `ImGuiKey_GamepadFaceLeft` | `634` |
| `Imgui.Enum.ImGuiKey._GamepadFaceRight` | `ImGuiKey_GamepadFaceRight` | `635` |
| `Imgui.Enum.ImGuiKey._GamepadFaceUp` | `ImGuiKey_GamepadFaceUp` | `636` |
| `Imgui.Enum.ImGuiKey._GamepadFaceDown` | `ImGuiKey_GamepadFaceDown` | `637` |
| `Imgui.Enum.ImGuiKey._GamepadDpadLeft` | `ImGuiKey_GamepadDpadLeft` | `638` |
| `Imgui.Enum.ImGuiKey._GamepadDpadRight` | `ImGuiKey_GamepadDpadRight` | `639` |
| `Imgui.Enum.ImGuiKey._GamepadDpadUp` | `ImGuiKey_GamepadDpadUp` | `640` |
| `Imgui.Enum.ImGuiKey._GamepadDpadDown` | `ImGuiKey_GamepadDpadDown` | `641` |
| `Imgui.Enum.ImGuiKey._GamepadL1` | `ImGuiKey_GamepadL1` | `642` |
| `Imgui.Enum.ImGuiKey._GamepadR1` | `ImGuiKey_GamepadR1` | `643` |
| `Imgui.Enum.ImGuiKey._GamepadL2` | `ImGuiKey_GamepadL2` | `644` |
| `Imgui.Enum.ImGuiKey._GamepadR2` | `ImGuiKey_GamepadR2` | `645` |
| `Imgui.Enum.ImGuiKey._GamepadL3` | `ImGuiKey_GamepadL3` | `646` |
| `Imgui.Enum.ImGuiKey._GamepadR3` | `ImGuiKey_GamepadR3` | `647` |
| `Imgui.Enum.ImGuiKey._GamepadLStickLeft` | `ImGuiKey_GamepadLStickLeft` | `648` |
| `Imgui.Enum.ImGuiKey._GamepadLStickRight` | `ImGuiKey_GamepadLStickRight` | `649` |
| `Imgui.Enum.ImGuiKey._GamepadLStickUp` | `ImGuiKey_GamepadLStickUp` | `650` |
| `Imgui.Enum.ImGuiKey._GamepadLStickDown` | `ImGuiKey_GamepadLStickDown` | `651` |
| `Imgui.Enum.ImGuiKey._GamepadRStickLeft` | `ImGuiKey_GamepadRStickLeft` | `652` |
| `Imgui.Enum.ImGuiKey._GamepadRStickRight` | `ImGuiKey_GamepadRStickRight` | `653` |
| `Imgui.Enum.ImGuiKey._GamepadRStickUp` | `ImGuiKey_GamepadRStickUp` | `654` |
| `Imgui.Enum.ImGuiKey._GamepadRStickDown` | `ImGuiKey_GamepadRStickDown` | `655` |
| `Imgui.Enum.ImGuiKey._MouseLeft` | `ImGuiKey_MouseLeft` | `656` |
| `Imgui.Enum.ImGuiKey._MouseRight` | `ImGuiKey_MouseRight` | `657` |
| `Imgui.Enum.ImGuiKey._MouseMiddle` | `ImGuiKey_MouseMiddle` | `658` |
| `Imgui.Enum.ImGuiKey._MouseX1` | `ImGuiKey_MouseX1` | `659` |
| `Imgui.Enum.ImGuiKey._MouseX2` | `ImGuiKey_MouseX2` | `660` |
| `Imgui.Enum.ImGuiKey._MouseWheelX` | `ImGuiKey_MouseWheelX` | `661` |
| `Imgui.Enum.ImGuiKey._MouseWheelY` | `ImGuiKey_MouseWheelY` | `662` |
| `Imgui.Enum.ImGuiKey.ImGuiMod_None` | `ImGuiMod_None` | `0` |
| `Imgui.Enum.ImGuiKey.ImGuiMod_Ctrl` | `ImGuiMod_Ctrl` | `4096` |
| `Imgui.Enum.ImGuiKey.ImGuiMod_Shift` | `ImGuiMod_Shift` | `8192` |
| `Imgui.Enum.ImGuiKey.ImGuiMod_Alt` | `ImGuiMod_Alt` | `16384` |
| `Imgui.Enum.ImGuiKey.ImGuiMod_Super` | `ImGuiMod_Super` | `32768` |
| `Imgui.Enum.ImGuiKey.ImGuiMod_Shortcut` | `ImGuiMod_Shortcut` | `4096` |
| `Imgui.Enum.ImGuiKey._ModCtrl` | `ImGuiKey_ModCtrl` | `4096` |
| `Imgui.Enum.ImGuiKey._ModShift` | `ImGuiKey_ModShift` | `8192` |
| `Imgui.Enum.ImGuiKey._ModAlt` | `ImGuiKey_ModAlt` | `16384` |
| `Imgui.Enum.ImGuiKey._ModSuper` | `ImGuiKey_ModSuper` | `32768` |
| `Imgui.Enum.ImGuiInputFlags.None` | `ImGuiInputFlags_None` | `0` |
| `Imgui.Enum.ImGuiInputFlags.Repeat` | `ImGuiInputFlags_Repeat` | `1` |
| `Imgui.Enum.ImGuiInputFlags.RouteActive` | `ImGuiInputFlags_RouteActive` | `1024` |
| `Imgui.Enum.ImGuiInputFlags.RouteFocused` | `ImGuiInputFlags_RouteFocused` | `2048` |
| `Imgui.Enum.ImGuiInputFlags.RouteGlobal` | `ImGuiInputFlags_RouteGlobal` | `4096` |
| `Imgui.Enum.ImGuiInputFlags.RouteAlways` | `ImGuiInputFlags_RouteAlways` | `8192` |
| `Imgui.Enum.ImGuiInputFlags.RouteOverFocused` | `ImGuiInputFlags_RouteOverFocused` | `16384` |
| `Imgui.Enum.ImGuiInputFlags.RouteOverActive` | `ImGuiInputFlags_RouteOverActive` | `32768` |
| `Imgui.Enum.ImGuiInputFlags.RouteUnlessBgFocused` | `ImGuiInputFlags_RouteUnlessBgFocused` | `65536` |
| `Imgui.Enum.ImGuiInputFlags.RouteFromRootWindow` | `ImGuiInputFlags_RouteFromRootWindow` | `131072` |
| `Imgui.Enum.ImGuiInputFlags.Tooltip` | `ImGuiInputFlags_Tooltip` | `262144` |
| `Imgui.Enum.ImGuiConfigFlags.None` | `ImGuiConfigFlags_None` | `0` |
| `Imgui.Enum.ImGuiConfigFlags.NavEnableKeyboard` | `ImGuiConfigFlags_NavEnableKeyboard` | `1` |
| `Imgui.Enum.ImGuiConfigFlags.NavEnableGamepad` | `ImGuiConfigFlags_NavEnableGamepad` | `2` |
| `Imgui.Enum.ImGuiConfigFlags.NoMouse` | `ImGuiConfigFlags_NoMouse` | `16` |
| `Imgui.Enum.ImGuiConfigFlags.NoMouseCursorChange` | `ImGuiConfigFlags_NoMouseCursorChange` | `32` |
| `Imgui.Enum.ImGuiConfigFlags.NoKeyboard` | `ImGuiConfigFlags_NoKeyboard` | `64` |
| `Imgui.Enum.ImGuiConfigFlags.IsSRGB` | `ImGuiConfigFlags_IsSRGB` | `1048576` |
| `Imgui.Enum.ImGuiConfigFlags.IsTouchScreen` | `ImGuiConfigFlags_IsTouchScreen` | `2097152` |
| `Imgui.Enum.ImGuiConfigFlags.NavEnableSetMousePos` | `ImGuiConfigFlags_NavEnableSetMousePos` | `4` |
| `Imgui.Enum.ImGuiConfigFlags.NavNoCaptureKeyboard` | `ImGuiConfigFlags_NavNoCaptureKeyboard` | `8` |
| `Imgui.Enum.ImGuiBackendFlags.None` | `ImGuiBackendFlags_None` | `0` |
| `Imgui.Enum.ImGuiBackendFlags.HasGamepad` | `ImGuiBackendFlags_HasGamepad` | `1` |
| `Imgui.Enum.ImGuiBackendFlags.HasMouseCursors` | `ImGuiBackendFlags_HasMouseCursors` | `2` |
| `Imgui.Enum.ImGuiBackendFlags.HasSetMousePos` | `ImGuiBackendFlags_HasSetMousePos` | `4` |
| `Imgui.Enum.ImGuiBackendFlags.RendererHasVtxOffset` | `ImGuiBackendFlags_RendererHasVtxOffset` | `8` |
| `Imgui.Enum.ImGuiCol.Text` | `ImGuiCol_Text` | `0` |
| `Imgui.Enum.ImGuiCol.TextDisabled` | `ImGuiCol_TextDisabled` | `1` |
| `Imgui.Enum.ImGuiCol.WindowBg` | `ImGuiCol_WindowBg` | `2` |
| `Imgui.Enum.ImGuiCol.ChildBg` | `ImGuiCol_ChildBg` | `3` |
| `Imgui.Enum.ImGuiCol.PopupBg` | `ImGuiCol_PopupBg` | `4` |
| `Imgui.Enum.ImGuiCol.Border` | `ImGuiCol_Border` | `5` |
| `Imgui.Enum.ImGuiCol.BorderShadow` | `ImGuiCol_BorderShadow` | `6` |
| `Imgui.Enum.ImGuiCol.FrameBg` | `ImGuiCol_FrameBg` | `7` |
| `Imgui.Enum.ImGuiCol.FrameBgHovered` | `ImGuiCol_FrameBgHovered` | `8` |
| `Imgui.Enum.ImGuiCol.FrameBgActive` | `ImGuiCol_FrameBgActive` | `9` |
| `Imgui.Enum.ImGuiCol.TitleBg` | `ImGuiCol_TitleBg` | `10` |
| `Imgui.Enum.ImGuiCol.TitleBgActive` | `ImGuiCol_TitleBgActive` | `11` |
| `Imgui.Enum.ImGuiCol.TitleBgCollapsed` | `ImGuiCol_TitleBgCollapsed` | `12` |
| `Imgui.Enum.ImGuiCol.MenuBarBg` | `ImGuiCol_MenuBarBg` | `13` |
| `Imgui.Enum.ImGuiCol.ScrollbarBg` | `ImGuiCol_ScrollbarBg` | `14` |
| `Imgui.Enum.ImGuiCol.ScrollbarGrab` | `ImGuiCol_ScrollbarGrab` | `15` |
| `Imgui.Enum.ImGuiCol.ScrollbarGrabHovered` | `ImGuiCol_ScrollbarGrabHovered` | `16` |
| `Imgui.Enum.ImGuiCol.ScrollbarGrabActive` | `ImGuiCol_ScrollbarGrabActive` | `17` |
| `Imgui.Enum.ImGuiCol.CheckMark` | `ImGuiCol_CheckMark` | `18` |
| `Imgui.Enum.ImGuiCol.SliderGrab` | `ImGuiCol_SliderGrab` | `19` |
| `Imgui.Enum.ImGuiCol.SliderGrabActive` | `ImGuiCol_SliderGrabActive` | `20` |
| `Imgui.Enum.ImGuiCol.Button` | `ImGuiCol_Button` | `21` |
| `Imgui.Enum.ImGuiCol.ButtonHovered` | `ImGuiCol_ButtonHovered` | `22` |
| `Imgui.Enum.ImGuiCol.ButtonActive` | `ImGuiCol_ButtonActive` | `23` |
| `Imgui.Enum.ImGuiCol.Header` | `ImGuiCol_Header` | `24` |
| `Imgui.Enum.ImGuiCol.HeaderHovered` | `ImGuiCol_HeaderHovered` | `25` |
| `Imgui.Enum.ImGuiCol.HeaderActive` | `ImGuiCol_HeaderActive` | `26` |
| `Imgui.Enum.ImGuiCol.Separator` | `ImGuiCol_Separator` | `27` |
| `Imgui.Enum.ImGuiCol.SeparatorHovered` | `ImGuiCol_SeparatorHovered` | `28` |
| `Imgui.Enum.ImGuiCol.SeparatorActive` | `ImGuiCol_SeparatorActive` | `29` |
| `Imgui.Enum.ImGuiCol.ResizeGrip` | `ImGuiCol_ResizeGrip` | `30` |
| `Imgui.Enum.ImGuiCol.ResizeGripHovered` | `ImGuiCol_ResizeGripHovered` | `31` |
| `Imgui.Enum.ImGuiCol.ResizeGripActive` | `ImGuiCol_ResizeGripActive` | `32` |
| `Imgui.Enum.ImGuiCol.TabHovered` | `ImGuiCol_TabHovered` | `33` |
| `Imgui.Enum.ImGuiCol.Tab` | `ImGuiCol_Tab` | `34` |
| `Imgui.Enum.ImGuiCol.TabSelected` | `ImGuiCol_TabSelected` | `35` |
| `Imgui.Enum.ImGuiCol.TabSelectedOverline` | `ImGuiCol_TabSelectedOverline` | `36` |
| `Imgui.Enum.ImGuiCol.TabDimmed` | `ImGuiCol_TabDimmed` | `37` |
| `Imgui.Enum.ImGuiCol.TabDimmedSelected` | `ImGuiCol_TabDimmedSelected` | `38` |
| `Imgui.Enum.ImGuiCol.TabDimmedSelectedOverline` | `ImGuiCol_TabDimmedSelectedOverline` | `39` |
| `Imgui.Enum.ImGuiCol.PlotLines` | `ImGuiCol_PlotLines` | `40` |
| `Imgui.Enum.ImGuiCol.PlotLinesHovered` | `ImGuiCol_PlotLinesHovered` | `41` |
| `Imgui.Enum.ImGuiCol.PlotHistogram` | `ImGuiCol_PlotHistogram` | `42` |
| `Imgui.Enum.ImGuiCol.PlotHistogramHovered` | `ImGuiCol_PlotHistogramHovered` | `43` |
| `Imgui.Enum.ImGuiCol.TableHeaderBg` | `ImGuiCol_TableHeaderBg` | `44` |
| `Imgui.Enum.ImGuiCol.TableBorderStrong` | `ImGuiCol_TableBorderStrong` | `45` |
| `Imgui.Enum.ImGuiCol.TableBorderLight` | `ImGuiCol_TableBorderLight` | `46` |
| `Imgui.Enum.ImGuiCol.TableRowBg` | `ImGuiCol_TableRowBg` | `47` |
| `Imgui.Enum.ImGuiCol.TableRowBgAlt` | `ImGuiCol_TableRowBgAlt` | `48` |
| `Imgui.Enum.ImGuiCol.TextLink` | `ImGuiCol_TextLink` | `49` |
| `Imgui.Enum.ImGuiCol.TextSelectedBg` | `ImGuiCol_TextSelectedBg` | `50` |
| `Imgui.Enum.ImGuiCol.DragDropTarget` | `ImGuiCol_DragDropTarget` | `51` |
| `Imgui.Enum.ImGuiCol.NavCursor` | `ImGuiCol_NavCursor` | `52` |
| `Imgui.Enum.ImGuiCol.NavWindowingHighlight` | `ImGuiCol_NavWindowingHighlight` | `53` |
| `Imgui.Enum.ImGuiCol.NavWindowingDimBg` | `ImGuiCol_NavWindowingDimBg` | `54` |
| `Imgui.Enum.ImGuiCol.ModalWindowDimBg` | `ImGuiCol_ModalWindowDimBg` | `55` |
| `Imgui.Enum.ImGuiCol.TabActive` | `ImGuiCol_TabActive` | `35` |
| `Imgui.Enum.ImGuiCol.TabUnfocused` | `ImGuiCol_TabUnfocused` | `37` |
| `Imgui.Enum.ImGuiCol.TabUnfocusedActive` | `ImGuiCol_TabUnfocusedActive` | `38` |
| `Imgui.Enum.ImGuiCol.NavHighlight` | `ImGuiCol_NavHighlight` | `52` |
| `Imgui.Enum.ImGuiStyleVar.Alpha` | `ImGuiStyleVar_Alpha` | `0` |
| `Imgui.Enum.ImGuiStyleVar.DisabledAlpha` | `ImGuiStyleVar_DisabledAlpha` | `1` |
| `Imgui.Enum.ImGuiStyleVar.WindowPadding` | `ImGuiStyleVar_WindowPadding` | `2` |
| `Imgui.Enum.ImGuiStyleVar.WindowRounding` | `ImGuiStyleVar_WindowRounding` | `3` |
| `Imgui.Enum.ImGuiStyleVar.WindowBorderSize` | `ImGuiStyleVar_WindowBorderSize` | `4` |
| `Imgui.Enum.ImGuiStyleVar.WindowMinSize` | `ImGuiStyleVar_WindowMinSize` | `5` |
| `Imgui.Enum.ImGuiStyleVar.WindowTitleAlign` | `ImGuiStyleVar_WindowTitleAlign` | `6` |
| `Imgui.Enum.ImGuiStyleVar.ChildRounding` | `ImGuiStyleVar_ChildRounding` | `7` |
| `Imgui.Enum.ImGuiStyleVar.ChildBorderSize` | `ImGuiStyleVar_ChildBorderSize` | `8` |
| `Imgui.Enum.ImGuiStyleVar.PopupRounding` | `ImGuiStyleVar_PopupRounding` | `9` |
| `Imgui.Enum.ImGuiStyleVar.PopupBorderSize` | `ImGuiStyleVar_PopupBorderSize` | `10` |
| `Imgui.Enum.ImGuiStyleVar.FramePadding` | `ImGuiStyleVar_FramePadding` | `11` |
| `Imgui.Enum.ImGuiStyleVar.FrameRounding` | `ImGuiStyleVar_FrameRounding` | `12` |
| `Imgui.Enum.ImGuiStyleVar.FrameBorderSize` | `ImGuiStyleVar_FrameBorderSize` | `13` |
| `Imgui.Enum.ImGuiStyleVar.ItemSpacing` | `ImGuiStyleVar_ItemSpacing` | `14` |
| `Imgui.Enum.ImGuiStyleVar.ItemInnerSpacing` | `ImGuiStyleVar_ItemInnerSpacing` | `15` |
| `Imgui.Enum.ImGuiStyleVar.IndentSpacing` | `ImGuiStyleVar_IndentSpacing` | `16` |
| `Imgui.Enum.ImGuiStyleVar.CellPadding` | `ImGuiStyleVar_CellPadding` | `17` |
| `Imgui.Enum.ImGuiStyleVar.ScrollbarSize` | `ImGuiStyleVar_ScrollbarSize` | `18` |
| `Imgui.Enum.ImGuiStyleVar.ScrollbarRounding` | `ImGuiStyleVar_ScrollbarRounding` | `19` |
| `Imgui.Enum.ImGuiStyleVar.GrabMinSize` | `ImGuiStyleVar_GrabMinSize` | `20` |
| `Imgui.Enum.ImGuiStyleVar.GrabRounding` | `ImGuiStyleVar_GrabRounding` | `21` |
| `Imgui.Enum.ImGuiStyleVar.ImageBorderSize` | `ImGuiStyleVar_ImageBorderSize` | `22` |
| `Imgui.Enum.ImGuiStyleVar.TabRounding` | `ImGuiStyleVar_TabRounding` | `23` |
| `Imgui.Enum.ImGuiStyleVar.TabBorderSize` | `ImGuiStyleVar_TabBorderSize` | `24` |
| `Imgui.Enum.ImGuiStyleVar.TabBarBorderSize` | `ImGuiStyleVar_TabBarBorderSize` | `25` |
| `Imgui.Enum.ImGuiStyleVar.TabBarOverlineSize` | `ImGuiStyleVar_TabBarOverlineSize` | `26` |
| `Imgui.Enum.ImGuiStyleVar.TableAngledHeadersAngle` | `ImGuiStyleVar_TableAngledHeadersAngle` | `27` |
| `Imgui.Enum.ImGuiStyleVar.TableAngledHeadersTextAlign` | `ImGuiStyleVar_TableAngledHeadersTextAlign` | `28` |
| `Imgui.Enum.ImGuiStyleVar.ButtonTextAlign` | `ImGuiStyleVar_ButtonTextAlign` | `29` |
| `Imgui.Enum.ImGuiStyleVar.SelectableTextAlign` | `ImGuiStyleVar_SelectableTextAlign` | `30` |
| `Imgui.Enum.ImGuiStyleVar.SeparatorTextBorderSize` | `ImGuiStyleVar_SeparatorTextBorderSize` | `31` |
| `Imgui.Enum.ImGuiStyleVar.SeparatorTextAlign` | `ImGuiStyleVar_SeparatorTextAlign` | `32` |
| `Imgui.Enum.ImGuiStyleVar.SeparatorTextPadding` | `ImGuiStyleVar_SeparatorTextPadding` | `33` |
| `Imgui.Enum.ImGuiButtonFlags.None` | `ImGuiButtonFlags_None` | `0` |
| `Imgui.Enum.ImGuiButtonFlags.MouseButtonLeft` | `ImGuiButtonFlags_MouseButtonLeft` | `1` |
| `Imgui.Enum.ImGuiButtonFlags.MouseButtonRight` | `ImGuiButtonFlags_MouseButtonRight` | `2` |
| `Imgui.Enum.ImGuiButtonFlags.MouseButtonMiddle` | `ImGuiButtonFlags_MouseButtonMiddle` | `4` |
| `Imgui.Enum.ImGuiButtonFlags.EnableNav` | `ImGuiButtonFlags_EnableNav` | `8` |
| `Imgui.Enum.ImGuiColorEditFlags.None` | `ImGuiColorEditFlags_None` | `0` |
| `Imgui.Enum.ImGuiColorEditFlags.NoAlpha` | `ImGuiColorEditFlags_NoAlpha` | `2` |
| `Imgui.Enum.ImGuiColorEditFlags.NoPicker` | `ImGuiColorEditFlags_NoPicker` | `4` |
| `Imgui.Enum.ImGuiColorEditFlags.NoOptions` | `ImGuiColorEditFlags_NoOptions` | `8` |
| `Imgui.Enum.ImGuiColorEditFlags.NoSmallPreview` | `ImGuiColorEditFlags_NoSmallPreview` | `16` |
| `Imgui.Enum.ImGuiColorEditFlags.NoInputs` | `ImGuiColorEditFlags_NoInputs` | `32` |
| `Imgui.Enum.ImGuiColorEditFlags.NoTooltip` | `ImGuiColorEditFlags_NoTooltip` | `64` |
| `Imgui.Enum.ImGuiColorEditFlags.NoLabel` | `ImGuiColorEditFlags_NoLabel` | `128` |
| `Imgui.Enum.ImGuiColorEditFlags.NoSidePreview` | `ImGuiColorEditFlags_NoSidePreview` | `256` |
| `Imgui.Enum.ImGuiColorEditFlags.NoDragDrop` | `ImGuiColorEditFlags_NoDragDrop` | `512` |
| `Imgui.Enum.ImGuiColorEditFlags.NoBorder` | `ImGuiColorEditFlags_NoBorder` | `1024` |
| `Imgui.Enum.ImGuiColorEditFlags.AlphaOpaque` | `ImGuiColorEditFlags_AlphaOpaque` | `2048` |
| `Imgui.Enum.ImGuiColorEditFlags.AlphaNoBg` | `ImGuiColorEditFlags_AlphaNoBg` | `4096` |
| `Imgui.Enum.ImGuiColorEditFlags.AlphaPreviewHalf` | `ImGuiColorEditFlags_AlphaPreviewHalf` | `8192` |
| `Imgui.Enum.ImGuiColorEditFlags.AlphaBar` | `ImGuiColorEditFlags_AlphaBar` | `65536` |
| `Imgui.Enum.ImGuiColorEditFlags.HDR` | `ImGuiColorEditFlags_HDR` | `524288` |
| `Imgui.Enum.ImGuiColorEditFlags.DisplayRGB` | `ImGuiColorEditFlags_DisplayRGB` | `1048576` |
| `Imgui.Enum.ImGuiColorEditFlags.DisplayHSV` | `ImGuiColorEditFlags_DisplayHSV` | `2097152` |
| `Imgui.Enum.ImGuiColorEditFlags.DisplayHex` | `ImGuiColorEditFlags_DisplayHex` | `4194304` |
| `Imgui.Enum.ImGuiColorEditFlags.Uint8` | `ImGuiColorEditFlags_Uint8` | `8388608` |
| `Imgui.Enum.ImGuiColorEditFlags.Float` | `ImGuiColorEditFlags_Float` | `16777216` |
| `Imgui.Enum.ImGuiColorEditFlags.PickerHueBar` | `ImGuiColorEditFlags_PickerHueBar` | `33554432` |
| `Imgui.Enum.ImGuiColorEditFlags.PickerHueWheel` | `ImGuiColorEditFlags_PickerHueWheel` | `67108864` |
| `Imgui.Enum.ImGuiColorEditFlags.InputRGB` | `ImGuiColorEditFlags_InputRGB` | `134217728` |
| `Imgui.Enum.ImGuiColorEditFlags.InputHSV` | `ImGuiColorEditFlags_InputHSV` | `268435456` |
| `Imgui.Enum.ImGuiColorEditFlags.AlphaPreview` | `ImGuiColorEditFlags_AlphaPreview` | `0` |
| `Imgui.Enum.ImGuiSliderFlags.None` | `ImGuiSliderFlags_None` | `0` |
| `Imgui.Enum.ImGuiSliderFlags.Logarithmic` | `ImGuiSliderFlags_Logarithmic` | `32` |
| `Imgui.Enum.ImGuiSliderFlags.NoRoundToFormat` | `ImGuiSliderFlags_NoRoundToFormat` | `64` |
| `Imgui.Enum.ImGuiSliderFlags.NoInput` | `ImGuiSliderFlags_NoInput` | `128` |
| `Imgui.Enum.ImGuiSliderFlags.WrapAround` | `ImGuiSliderFlags_WrapAround` | `256` |
| `Imgui.Enum.ImGuiSliderFlags.ClampOnInput` | `ImGuiSliderFlags_ClampOnInput` | `512` |
| `Imgui.Enum.ImGuiSliderFlags.ClampZeroRange` | `ImGuiSliderFlags_ClampZeroRange` | `1024` |
| `Imgui.Enum.ImGuiSliderFlags.NoSpeedTweaks` | `ImGuiSliderFlags_NoSpeedTweaks` | `2048` |
| `Imgui.Enum.ImGuiSliderFlags.AlwaysClamp` | `ImGuiSliderFlags_AlwaysClamp` | `1536` |
| `Imgui.Enum.ImGuiMouseButton.Left` | `ImGuiMouseButton_Left` | `0` |
| `Imgui.Enum.ImGuiMouseButton.Right` | `ImGuiMouseButton_Right` | `1` |
| `Imgui.Enum.ImGuiMouseButton.Middle` | `ImGuiMouseButton_Middle` | `2` |
| `Imgui.Enum.ImGuiMouseCursor.None` | `ImGuiMouseCursor_None` | `-1` |
| `Imgui.Enum.ImGuiMouseCursor.Arrow` | `ImGuiMouseCursor_Arrow` | `0` |
| `Imgui.Enum.ImGuiMouseCursor.TextInput` | `ImGuiMouseCursor_TextInput` | `1` |
| `Imgui.Enum.ImGuiMouseCursor.ResizeAll` | `ImGuiMouseCursor_ResizeAll` | `2` |
| `Imgui.Enum.ImGuiMouseCursor.ResizeNS` | `ImGuiMouseCursor_ResizeNS` | `3` |
| `Imgui.Enum.ImGuiMouseCursor.ResizeEW` | `ImGuiMouseCursor_ResizeEW` | `4` |
| `Imgui.Enum.ImGuiMouseCursor.ResizeNESW` | `ImGuiMouseCursor_ResizeNESW` | `5` |
| `Imgui.Enum.ImGuiMouseCursor.ResizeNWSE` | `ImGuiMouseCursor_ResizeNWSE` | `6` |
| `Imgui.Enum.ImGuiMouseCursor.Hand` | `ImGuiMouseCursor_Hand` | `7` |
| `Imgui.Enum.ImGuiMouseCursor.Wait` | `ImGuiMouseCursor_Wait` | `8` |
| `Imgui.Enum.ImGuiMouseCursor.Progress` | `ImGuiMouseCursor_Progress` | `9` |
| `Imgui.Enum.ImGuiMouseCursor.NotAllowed` | `ImGuiMouseCursor_NotAllowed` | `10` |
| `Imgui.Enum.ImGuiMouseSource._Mouse` | `ImGuiMouseSource_Mouse` | `0` |
| `Imgui.Enum.ImGuiMouseSource._TouchScreen` | `ImGuiMouseSource_TouchScreen` | `1` |
| `Imgui.Enum.ImGuiMouseSource._Pen` | `ImGuiMouseSource_Pen` | `2` |
| `Imgui.Enum.ImGuiCond.None` | `ImGuiCond_None` | `0` |
| `Imgui.Enum.ImGuiCond.Always` | `ImGuiCond_Always` | `1` |
| `Imgui.Enum.ImGuiCond.Once` | `ImGuiCond_Once` | `2` |
| `Imgui.Enum.ImGuiCond.FirstUseEver` | `ImGuiCond_FirstUseEver` | `4` |
| `Imgui.Enum.ImGuiCond.Appearing` | `ImGuiCond_Appearing` | `8` |
| `Imgui.Enum.ImGuiTableFlags.None` | `ImGuiTableFlags_None` | `0` |
| `Imgui.Enum.ImGuiTableFlags.Resizable` | `ImGuiTableFlags_Resizable` | `1` |
| `Imgui.Enum.ImGuiTableFlags.Reorderable` | `ImGuiTableFlags_Reorderable` | `2` |
| `Imgui.Enum.ImGuiTableFlags.Hideable` | `ImGuiTableFlags_Hideable` | `4` |
| `Imgui.Enum.ImGuiTableFlags.Sortable` | `ImGuiTableFlags_Sortable` | `8` |
| `Imgui.Enum.ImGuiTableFlags.NoSavedSettings` | `ImGuiTableFlags_NoSavedSettings` | `16` |
| `Imgui.Enum.ImGuiTableFlags.ContextMenuInBody` | `ImGuiTableFlags_ContextMenuInBody` | `32` |
| `Imgui.Enum.ImGuiTableFlags.RowBg` | `ImGuiTableFlags_RowBg` | `64` |
| `Imgui.Enum.ImGuiTableFlags.BordersInnerH` | `ImGuiTableFlags_BordersInnerH` | `128` |
| `Imgui.Enum.ImGuiTableFlags.BordersOuterH` | `ImGuiTableFlags_BordersOuterH` | `256` |
| `Imgui.Enum.ImGuiTableFlags.BordersInnerV` | `ImGuiTableFlags_BordersInnerV` | `512` |
| `Imgui.Enum.ImGuiTableFlags.BordersOuterV` | `ImGuiTableFlags_BordersOuterV` | `1024` |
| `Imgui.Enum.ImGuiTableFlags.BordersH` | `ImGuiTableFlags_BordersH` | `384` |
| `Imgui.Enum.ImGuiTableFlags.BordersV` | `ImGuiTableFlags_BordersV` | `1536` |
| `Imgui.Enum.ImGuiTableFlags.BordersInner` | `ImGuiTableFlags_BordersInner` | `640` |
| `Imgui.Enum.ImGuiTableFlags.BordersOuter` | `ImGuiTableFlags_BordersOuter` | `1280` |
| `Imgui.Enum.ImGuiTableFlags.Borders` | `ImGuiTableFlags_Borders` | `1920` |
| `Imgui.Enum.ImGuiTableFlags.NoBordersInBody` | `ImGuiTableFlags_NoBordersInBody` | `2048` |
| `Imgui.Enum.ImGuiTableFlags.NoBordersInBodyUntilResize` | `ImGuiTableFlags_NoBordersInBodyUntilResize` | `4096` |
| `Imgui.Enum.ImGuiTableFlags.SizingFixedFit` | `ImGuiTableFlags_SizingFixedFit` | `8192` |
| `Imgui.Enum.ImGuiTableFlags.SizingFixedSame` | `ImGuiTableFlags_SizingFixedSame` | `16384` |
| `Imgui.Enum.ImGuiTableFlags.SizingStretchProp` | `ImGuiTableFlags_SizingStretchProp` | `24576` |
| `Imgui.Enum.ImGuiTableFlags.SizingStretchSame` | `ImGuiTableFlags_SizingStretchSame` | `32768` |
| `Imgui.Enum.ImGuiTableFlags.NoHostExtendX` | `ImGuiTableFlags_NoHostExtendX` | `65536` |
| `Imgui.Enum.ImGuiTableFlags.NoHostExtendY` | `ImGuiTableFlags_NoHostExtendY` | `131072` |
| `Imgui.Enum.ImGuiTableFlags.NoKeepColumnsVisible` | `ImGuiTableFlags_NoKeepColumnsVisible` | `262144` |
| `Imgui.Enum.ImGuiTableFlags.PreciseWidths` | `ImGuiTableFlags_PreciseWidths` | `524288` |
| `Imgui.Enum.ImGuiTableFlags.NoClip` | `ImGuiTableFlags_NoClip` | `1048576` |
| `Imgui.Enum.ImGuiTableFlags.PadOuterX` | `ImGuiTableFlags_PadOuterX` | `2097152` |
| `Imgui.Enum.ImGuiTableFlags.NoPadOuterX` | `ImGuiTableFlags_NoPadOuterX` | `4194304` |
| `Imgui.Enum.ImGuiTableFlags.NoPadInnerX` | `ImGuiTableFlags_NoPadInnerX` | `8388608` |
| `Imgui.Enum.ImGuiTableFlags.ScrollX` | `ImGuiTableFlags_ScrollX` | `16777216` |
| `Imgui.Enum.ImGuiTableFlags.ScrollY` | `ImGuiTableFlags_ScrollY` | `33554432` |
| `Imgui.Enum.ImGuiTableFlags.SortMulti` | `ImGuiTableFlags_SortMulti` | `67108864` |
| `Imgui.Enum.ImGuiTableFlags.SortTristate` | `ImGuiTableFlags_SortTristate` | `134217728` |
| `Imgui.Enum.ImGuiTableFlags.HighlightHoveredColumn` | `ImGuiTableFlags_HighlightHoveredColumn` | `268435456` |
| `Imgui.Enum.ImGuiTableColumnFlags.None` | `ImGuiTableColumnFlags_None` | `0` |
| `Imgui.Enum.ImGuiTableColumnFlags.Disabled` | `ImGuiTableColumnFlags_Disabled` | `1` |
| `Imgui.Enum.ImGuiTableColumnFlags.DefaultHide` | `ImGuiTableColumnFlags_DefaultHide` | `2` |
| `Imgui.Enum.ImGuiTableColumnFlags.DefaultSort` | `ImGuiTableColumnFlags_DefaultSort` | `4` |
| `Imgui.Enum.ImGuiTableColumnFlags.WidthStretch` | `ImGuiTableColumnFlags_WidthStretch` | `8` |
| `Imgui.Enum.ImGuiTableColumnFlags.WidthFixed` | `ImGuiTableColumnFlags_WidthFixed` | `16` |
| `Imgui.Enum.ImGuiTableColumnFlags.NoResize` | `ImGuiTableColumnFlags_NoResize` | `32` |
| `Imgui.Enum.ImGuiTableColumnFlags.NoReorder` | `ImGuiTableColumnFlags_NoReorder` | `64` |
| `Imgui.Enum.ImGuiTableColumnFlags.NoHide` | `ImGuiTableColumnFlags_NoHide` | `128` |
| `Imgui.Enum.ImGuiTableColumnFlags.NoClip` | `ImGuiTableColumnFlags_NoClip` | `256` |
| `Imgui.Enum.ImGuiTableColumnFlags.NoSort` | `ImGuiTableColumnFlags_NoSort` | `512` |
| `Imgui.Enum.ImGuiTableColumnFlags.NoSortAscending` | `ImGuiTableColumnFlags_NoSortAscending` | `1024` |
| `Imgui.Enum.ImGuiTableColumnFlags.NoSortDescending` | `ImGuiTableColumnFlags_NoSortDescending` | `2048` |
| `Imgui.Enum.ImGuiTableColumnFlags.NoHeaderLabel` | `ImGuiTableColumnFlags_NoHeaderLabel` | `4096` |
| `Imgui.Enum.ImGuiTableColumnFlags.NoHeaderWidth` | `ImGuiTableColumnFlags_NoHeaderWidth` | `8192` |
| `Imgui.Enum.ImGuiTableColumnFlags.PreferSortAscending` | `ImGuiTableColumnFlags_PreferSortAscending` | `16384` |
| `Imgui.Enum.ImGuiTableColumnFlags.PreferSortDescending` | `ImGuiTableColumnFlags_PreferSortDescending` | `32768` |
| `Imgui.Enum.ImGuiTableColumnFlags.IndentEnable` | `ImGuiTableColumnFlags_IndentEnable` | `65536` |
| `Imgui.Enum.ImGuiTableColumnFlags.IndentDisable` | `ImGuiTableColumnFlags_IndentDisable` | `131072` |
| `Imgui.Enum.ImGuiTableColumnFlags.AngledHeader` | `ImGuiTableColumnFlags_AngledHeader` | `262144` |
| `Imgui.Enum.ImGuiTableColumnFlags.IsEnabled` | `ImGuiTableColumnFlags_IsEnabled` | `16777216` |
| `Imgui.Enum.ImGuiTableColumnFlags.IsVisible` | `ImGuiTableColumnFlags_IsVisible` | `33554432` |
| `Imgui.Enum.ImGuiTableColumnFlags.IsSorted` | `ImGuiTableColumnFlags_IsSorted` | `67108864` |
| `Imgui.Enum.ImGuiTableColumnFlags.IsHovered` | `ImGuiTableColumnFlags_IsHovered` | `134217728` |
| `Imgui.Enum.ImGuiTableRowFlags.None` | `ImGuiTableRowFlags_None` | `0` |
| `Imgui.Enum.ImGuiTableRowFlags.Headers` | `ImGuiTableRowFlags_Headers` | `1` |
| `Imgui.Enum.ImGuiTableBgTarget.None` | `ImGuiTableBgTarget_None` | `0` |
| `Imgui.Enum.ImGuiTableBgTarget.RowBg0` | `ImGuiTableBgTarget_RowBg0` | `1` |
| `Imgui.Enum.ImGuiTableBgTarget.RowBg1` | `ImGuiTableBgTarget_RowBg1` | `2` |
| `Imgui.Enum.ImGuiTableBgTarget.CellBg` | `ImGuiTableBgTarget_CellBg` | `3` |
| `Imgui.Enum.ImGuiMultiSelectFlags.None` | `ImGuiMultiSelectFlags_None` | `0` |
| `Imgui.Enum.ImGuiMultiSelectFlags.SingleSelect` | `ImGuiMultiSelectFlags_SingleSelect` | `1` |
| `Imgui.Enum.ImGuiMultiSelectFlags.NoSelectAll` | `ImGuiMultiSelectFlags_NoSelectAll` | `2` |
| `Imgui.Enum.ImGuiMultiSelectFlags.NoRangeSelect` | `ImGuiMultiSelectFlags_NoRangeSelect` | `4` |
| `Imgui.Enum.ImGuiMultiSelectFlags.NoAutoSelect` | `ImGuiMultiSelectFlags_NoAutoSelect` | `8` |
| `Imgui.Enum.ImGuiMultiSelectFlags.NoAutoClear` | `ImGuiMultiSelectFlags_NoAutoClear` | `16` |
| `Imgui.Enum.ImGuiMultiSelectFlags.NoAutoClearOnReselect` | `ImGuiMultiSelectFlags_NoAutoClearOnReselect` | `32` |
| `Imgui.Enum.ImGuiMultiSelectFlags.BoxSelect1d` | `ImGuiMultiSelectFlags_BoxSelect1d` | `64` |
| `Imgui.Enum.ImGuiMultiSelectFlags.BoxSelect2d` | `ImGuiMultiSelectFlags_BoxSelect2d` | `128` |
| `Imgui.Enum.ImGuiMultiSelectFlags.BoxSelectNoScroll` | `ImGuiMultiSelectFlags_BoxSelectNoScroll` | `256` |
| `Imgui.Enum.ImGuiMultiSelectFlags.ClearOnEscape` | `ImGuiMultiSelectFlags_ClearOnEscape` | `512` |
| `Imgui.Enum.ImGuiMultiSelectFlags.ClearOnClickVoid` | `ImGuiMultiSelectFlags_ClearOnClickVoid` | `1024` |
| `Imgui.Enum.ImGuiMultiSelectFlags.ScopeWindow` | `ImGuiMultiSelectFlags_ScopeWindow` | `2048` |
| `Imgui.Enum.ImGuiMultiSelectFlags.ScopeRect` | `ImGuiMultiSelectFlags_ScopeRect` | `4096` |
| `Imgui.Enum.ImGuiMultiSelectFlags.SelectOnClick` | `ImGuiMultiSelectFlags_SelectOnClick` | `8192` |
| `Imgui.Enum.ImGuiMultiSelectFlags.SelectOnClickRelease` | `ImGuiMultiSelectFlags_SelectOnClickRelease` | `16384` |
| `Imgui.Enum.ImGuiMultiSelectFlags.NavWrapX` | `ImGuiMultiSelectFlags_NavWrapX` | `65536` |
| `Imgui.Enum.ImGuiSelectionRequestType._None` | `ImGuiSelectionRequestType_None` | `0` |
| `Imgui.Enum.ImGuiSelectionRequestType._SetAll` | `ImGuiSelectionRequestType_SetAll` | `1` |
| `Imgui.Enum.ImGuiSelectionRequestType._SetRange` | `ImGuiSelectionRequestType_SetRange` | `2` |
| `Imgui.Enum.ImDrawFlags.None` | `ImDrawFlags_None` | `0` |
| `Imgui.Enum.ImDrawFlags.Closed` | `ImDrawFlags_Closed` | `1` |
| `Imgui.Enum.ImDrawFlags.RoundCornersTopLeft` | `ImDrawFlags_RoundCornersTopLeft` | `16` |
| `Imgui.Enum.ImDrawFlags.RoundCornersTopRight` | `ImDrawFlags_RoundCornersTopRight` | `32` |
| `Imgui.Enum.ImDrawFlags.RoundCornersBottomLeft` | `ImDrawFlags_RoundCornersBottomLeft` | `64` |
| `Imgui.Enum.ImDrawFlags.RoundCornersBottomRight` | `ImDrawFlags_RoundCornersBottomRight` | `128` |
| `Imgui.Enum.ImDrawFlags.RoundCornersNone` | `ImDrawFlags_RoundCornersNone` | `256` |
| `Imgui.Enum.ImDrawFlags.RoundCornersTop` | `ImDrawFlags_RoundCornersTop` | `48` |
| `Imgui.Enum.ImDrawFlags.RoundCornersBottom` | `ImDrawFlags_RoundCornersBottom` | `192` |
| `Imgui.Enum.ImDrawFlags.RoundCornersLeft` | `ImDrawFlags_RoundCornersLeft` | `80` |
| `Imgui.Enum.ImDrawFlags.RoundCornersRight` | `ImDrawFlags_RoundCornersRight` | `160` |
| `Imgui.Enum.ImDrawFlags.RoundCornersAll` | `ImDrawFlags_RoundCornersAll` | `240` |
| `Imgui.Enum.ImDrawListFlags.None` | `ImDrawListFlags_None` | `0` |
| `Imgui.Enum.ImDrawListFlags.AntiAliasedLines` | `ImDrawListFlags_AntiAliasedLines` | `1` |
| `Imgui.Enum.ImDrawListFlags.AntiAliasedLinesUseTex` | `ImDrawListFlags_AntiAliasedLinesUseTex` | `2` |
| `Imgui.Enum.ImDrawListFlags.AntiAliasedFill` | `ImDrawListFlags_AntiAliasedFill` | `4` |
| `Imgui.Enum.ImDrawListFlags.AllowVtxOffset` | `ImDrawListFlags_AllowVtxOffset` | `8` |
| `Imgui.Enum.ImFontAtlasFlags.None` | `ImFontAtlasFlags_None` | `0` |
| `Imgui.Enum.ImFontAtlasFlags.NoPowerOfTwoHeight` | `ImFontAtlasFlags_NoPowerOfTwoHeight` | `1` |
| `Imgui.Enum.ImFontAtlasFlags.NoMouseCursors` | `ImFontAtlasFlags_NoMouseCursors` | `2` |
| `Imgui.Enum.ImFontAtlasFlags.NoBakedLines` | `ImFontAtlasFlags_NoBakedLines` | `4` |
| `Imgui.Enum.ImGuiViewportFlags.None` | `ImGuiViewportFlags_None` | `0` |
| `Imgui.Enum.ImGuiViewportFlags.IsPlatformWindow` | `ImGuiViewportFlags_IsPlatformWindow` | `1` |
| `Imgui.Enum.ImGuiViewportFlags.IsPlatformMonitor` | `ImGuiViewportFlags_IsPlatformMonitor` | `2` |
| `Imgui.Enum.ImGuiViewportFlags.OwnedByApp` | `ImGuiViewportFlags_OwnedByApp` | `4` |
