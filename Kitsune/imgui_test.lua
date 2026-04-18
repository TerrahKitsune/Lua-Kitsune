local ctx = {
    frameCount     = 0,
    scheduleResult = "pending",
    lastError      = "none",
    scheduled      = false,
    windowOpen     = true,
    checkboxVal    = false,
    sliderFloat    = 0.5,
    sliderInt      = 5,
    inputText      = "hello",
    radioSel       = 0,
    -- Drag inputs
    dragFloat      = 0.5,
    dragInt        = 5,
    dragFloatLo    = 0.2,
    dragFloatHi    = 0.8,
    dragIntLo      = 2,
    dragIntHi      = 8,
    -- Extra inputs
    inputFloat     = 3.14,
    inputInt       = 42,
    multilineText  = "line one\nline two\nline three",
    sliderAngle    = 0.0,
    vsliderF       = 0.5,
    vsliderI       = 5,
    comboSel       = 0,
    listboxSel     = 0,
    -- Tree / Selectable
    selectableSel  = -1,
    -- Style / Layout
    progressVal    = 0.3,
    -- Scroll demo
    scrollHereMid  = false,
    -- Popups / Menus
    menuLastClick  = "none",
    popupMsg       = "idle",
    -- System
    showDemo       = false,
    showMetrics    = false,
    showMenuDemo   = false,
    consoleVisible = true,
    ownsConsole    = Win32.OwnsConsole(),
    mdDoc          = Stream.Open('./docs/markdown-test.md', 'rb'),
}

local function onResourceLoad(path)

    if path == "sample.png" then
        return Stream.Open("./docs/sample.png", 'rb');
    else
        print("Unknown resource requested: " .. path)
        return nil;
    end
end

local function onError(err)
    ctx.lastError = tostring(err)
    return true
end

local noResize   = Imgui.Enum.ImGuiWindowFlags.NoResize
                 + Imgui.Enum.ImGuiWindowFlags.NoMove
                 + Imgui.Enum.ImGuiWindowFlags.NoCollapse
                 + Imgui.Enum.ImGuiWindowFlags.NoBringToFrontOnFocus
local condAlways = Imgui.Enum.ImGuiCond.Always
local tabFlags   = Imgui.Enum.ImGuiTabBarFlags.None

local tableFlags   = Imgui.Enum.ImGuiTableFlags.Borders + Imgui.Enum.ImGuiTableFlags.RowBg
local colButton    = Imgui.Enum.ImGuiCol.Button
local keyEnter     = Imgui.Enum.ImGuiKey._Enter
local keySpace     = Imgui.Enum.ImGuiKey._Space
local treeLeaf     = Imgui.Enum.ImGuiTreeNodeFlags.Leaf
local menuBarFlag  = Imgui.Enum.ImGuiWindowFlags.MenuBar + Imgui.Enum.ImGuiWindowFlags.AlwaysAutoResize + Imgui.Enum.ImGuiWindowFlags.NoSavedSettings

local comboItems   = { "Apple", "Banana", "Cherry", "Date", "Elderberry" }
local listboxItems = { "Red", "Green", "Blue", "Yellow", "Magenta" }

local function tabInfo(renderer, ctx)
    renderer:Text("Frame:       " .. ctx.frameCount)
    renderer:Text("Schedule:    " .. ctx.scheduleResult)
    renderer:Text("Last error:  " .. ctx.lastError)
    renderer:Separator()
    renderer:Text("Window size: " .. SDL.GetWindowWidth() .. " x " .. SDL.GetWindowHeight())
    renderer:Text("Window pos:  " .. SDL.GetWindowX() .. ", " .. SDL.GetWindowY())
    renderer:Text("Focused:     " .. tostring(SDL.IsFocused()))
    renderer:Text("Minimized:   " .. tostring(SDL.IsMinimized()))
    local idx, name, mx, my, mw, mh, hz = SDL.GetMonitor()
    if idx then
        renderer:Text("Monitor " .. idx .. ": " .. name .. " (" .. mw .. "x" .. mh .. " @" .. hz .. "Hz)")
    end
end

local function tabWidgets(renderer, ctx)
    renderer:Text("--- Buttons ---")
    if renderer:Button("Click me") then
        ctx.lastError = "Button clicked on frame " .. ctx.frameCount
    end
    renderer:SameLine()
    if renderer:Button("Trigger error") then
        error("manual error trigger")
    end
    renderer:Separator()
    renderer:Text("--- Checkbox ---")
    local changed, val = renderer:Checkbox("Toggle me", ctx.checkboxVal)
    if changed then ctx.checkboxVal = val end
    renderer:Text("Value: " .. tostring(ctx.checkboxVal))
    renderer:Separator()
    renderer:Text("--- Radio buttons ---")
    for i = 0, 2 do
        if renderer:RadioButton("Option " .. i, ctx.radioSel == i) then
            ctx.radioSel = i
        end
        if i < 2 then renderer:SameLine() end
    end
    renderer:Text("Selected: " .. ctx.radioSel)
    renderer:Separator()
    renderer:Text("--- Sliders ---")
    local fc, fv = renderer:SliderFloat("Float", ctx.sliderFloat, 0.0, 1.0)
    if fc then ctx.sliderFloat = fv end
    renderer:Text(string.format("Float value: %.3f", ctx.sliderFloat))
    local ic, iv = renderer:SliderInt("Int", ctx.sliderInt, 0, 20)
    if ic then ctx.sliderInt = iv end
    renderer:Text("Int value: " .. ctx.sliderInt)
end

local function tabText(renderer, ctx)
    renderer:Text("Plain Text")
    renderer:TextDisabled("Disabled Text")
    renderer:TextWrapped("Wrapped: " .. string.rep("word ", 30))
    renderer:Separator()
    renderer:TextColored(1, 0, 0, 1, "Red")
    renderer:TextColored(0, 1, 0, 1, "Green")
    renderer:TextColored(0, 0.5, 1, 1, "Blue")
    renderer:Separator()
    renderer:BulletText("Bullet item one")
    renderer:BulletText("Bullet item two")
    renderer:BulletText("Bullet item three")
end

local function tabInput(renderer, ctx)
    renderer:Text("--- InputText ---")
    local changed, val = renderer:InputText("Text input", ctx.inputText)
    if changed then ctx.inputText = val end
    renderer:Text("Value: " .. ctx.inputText)
end

local function tabLayout(renderer, ctx)
    renderer:Text("--- SameLine ---")
    renderer:Button("A") renderer:SameLine()
    renderer:Button("B") renderer:SameLine()
    renderer:Button("C")
    renderer:Separator()
    renderer:Text("--- Indent ---")
    renderer:Text("No indent")
    renderer:Indent()
    renderer:Text("Indented once")
    renderer:Indent()
    renderer:Text("Indented twice")
    renderer:Unindent()
    renderer:Unindent()
    renderer:Separator()
    renderer:Text("--- Child window ---")
    if renderer:BeginChild("child1", 300, 60, true) then
        renderer:Text("Inside child window")
        renderer:Text("Line 2")
    end
    renderer:EndChild()
    renderer:Separator()
    renderer:Text("--- BeginGroup / EndGroup ---")
    renderer:BeginGroup()
        renderer:Text("Grouped item A")
        renderer:Button("Grouped button")
        renderer:Text("Grouped item B")
    renderer:EndGroup()
    renderer:SameLine()
    renderer:Text("<-- items above are grouped")
    renderer:Separator()
    renderer:Text("--- PushID / PopID (same label, different IDs) ---")
    for i = 1, 3 do
        renderer:PushID(i)
        if renderer:Button("Click") then ctx.lastPushId = i end
        renderer:SameLine()
        renderer:Text("ID " .. i)
        renderer:PopID()
    end
    renderer:Text("Last clicked: " .. tostring(ctx.lastPushId or "none"))
end

local function tabSchedule(renderer, ctx)
    renderer:Text("Result: " .. ctx.scheduleResult)
    renderer:Separator()
    if renderer:Button("Re-schedule") then
        Imgui.Schedule(function()
            ctx.scheduleResult = "re-scheduled on frame " .. ctx.frameCount
        end)
    end
end

-- ---------------------------------------------------------------------------
-- New tabs
-- ---------------------------------------------------------------------------

local function tabDrag(renderer, ctx)
    renderer:Text("--- DragFloat ---")
    local c, v = renderer:DragFloat("Drag float", ctx.dragFloat)
    if c then ctx.dragFloat = v end
    renderer:Text(string.format("Value: %.3f", ctx.dragFloat))
    renderer:Separator()
    renderer:Text("--- DragInt ---")
    local ci, vi = renderer:DragInt("Drag int", ctx.dragInt)
    if ci then ctx.dragInt = vi end
    renderer:Text("Value: " .. ctx.dragInt)
    renderer:Separator()
    renderer:Text("--- DragFloatRange2 ---")
    local cr, vlo, vhi = renderer:DragFloatRange2("Float range", ctx.dragFloatLo, ctx.dragFloatHi)
    if cr then ctx.dragFloatLo = vlo; ctx.dragFloatHi = vhi end
    renderer:Text(string.format("Range: %.2f .. %.2f", ctx.dragFloatLo, ctx.dragFloatHi))
    renderer:Separator()
    renderer:Text("--- DragIntRange2 ---")
    local cir, ilo, ihi = renderer:DragIntRange2("Int range", ctx.dragIntLo, ctx.dragIntHi)
    if cir then ctx.dragIntLo = ilo; ctx.dragIntHi = ihi end
    renderer:Text("Range: " .. ctx.dragIntLo .. " .. " .. ctx.dragIntHi)
end

local function tabExtraInput(renderer, ctx)
    renderer:Text("--- InputFloat ---")
    local cf, vf = renderer:InputFloat("Input float", ctx.inputFloat)
    if cf then ctx.inputFloat = vf end
    renderer:Text(string.format("Value: %.4f", ctx.inputFloat))
    renderer:Separator()
    renderer:Text("--- InputInt ---")
    local cii, vii = renderer:InputInt("Input int", ctx.inputInt)
    if cii then ctx.inputInt = vii end
    renderer:Text("Value: " .. ctx.inputInt)
    renderer:Separator()
    renderer:Text("--- InputTextMultiline ---")
    local cm, vm = renderer:InputTextMultiline("##ml", ctx.multilineText, 0, 80)
    if cm then ctx.multilineText = vm end
    renderer:Text("Chars: " .. #ctx.multilineText)
    renderer:Separator()
    renderer:Text("--- SliderAngle ---")
    local ca, va = renderer:SliderAngle("Angle", ctx.sliderAngle)
    if ca then ctx.sliderAngle = va end
    renderer:Text(string.format("Radians: %.3f", ctx.sliderAngle))
    renderer:Separator()
    renderer:Text("--- VSliderFloat / VSliderInt ---")
    local cvf, vvf = renderer:VSliderFloat("##vsf", 30, 100, ctx.vsliderF, 0.0, 1.0)
    if cvf then ctx.vsliderF = vvf end
    renderer:SameLine()
    local cvi, vvi = renderer:VSliderInt("##vsi", 30, 100, ctx.vsliderI, 0, 20)
    if cvi then ctx.vsliderI = vvi end
    renderer:SameLine()
    renderer:Text(string.format("%.2f / %d", ctx.vsliderF, ctx.vsliderI))
    renderer:Separator()
    renderer:Text("--- Combo ---")
    local cc, ci = renderer:Combo("Pick fruit", ctx.comboSel, comboItems)
    if cc then ctx.comboSel = ci end
    renderer:Text("Selected: " .. (comboItems[ctx.comboSel + 1] or "?"))
    renderer:Separator()
    renderer:Text("--- ListBox ---")
    local cl, li = renderer:ListBox("Pick colour", ctx.listboxSel, listboxItems)
    if cl then ctx.listboxSel = li end
    renderer:Text("Selected: " .. (listboxItems[ctx.listboxSel + 1] or "?"))
    renderer:Separator()
    renderer:Text("--- PlotLines ---")
    local plotData = {}
    for i = 1, 32 do plotData[i] = math.sin(i * 0.4 + ctx.frameCount * 0.05) end
    renderer:PlotLines("sin wave", plotData)
end

local function tabTree(renderer, ctx)
    renderer:Text("--- TreeNode ---")
    if renderer:TreeNode("Parent node") then
        renderer:Text("Child item A")
        renderer:Text("Child item B")
        if renderer:TreeNode("Nested node") then
            renderer:Text("Deeply nested item")
            renderer:TreePop()
        end
        renderer:TreePop()
    end
    renderer:Separator()
    renderer:Text("--- TreeNodeEx (leaf) ---")
    if renderer:TreeNodeEx("Leaf (no children)", treeLeaf) then
        renderer:TreePop()
    end
    renderer:Separator()
    renderer:Text("--- CollapsingHeader ---")
    if renderer:CollapsingHeader("Section one") then
        renderer:Text("Content of section one")
        renderer:BulletText("Bullet point alpha")
        renderer:BulletText("Bullet point beta")
    end
    if renderer:CollapsingHeader("Section two") then
        renderer:Text("Content of section two")
    end
    renderer:Separator()
    renderer:Text("--- Selectable (selected: " .. ctx.selectableSel .. ") ---")
    local selectItems = { "Item Alpha", "Item Beta", "Item Gamma", "Item Delta" }
    for i, name in ipairs(selectItems) do
        if renderer:Selectable(name, ctx.selectableSel == i) then
            ctx.selectableSel = i
        end
    end
end

local function tabStyle(renderer, ctx)
    renderer:Text("--- PushStyleColor / PopStyleColor ---")
    -- IM_COL32(R,G,B,A) = (A<<24)|(B<<16)|(G<<8)|R
    renderer:PushStyleColor(colButton, 0xFF22AA44)  -- green button
    renderer:Button("Styled button (green)")
    renderer:PopStyleColor()
    renderer:Separator()
    renderer:Text("--- BeginDisabled / EndDisabled ---")
    renderer:BeginDisabled()
    renderer:Button("Disabled button")
    renderer:SliderFloat("Disabled slider", 0.5, 0.0, 1.0)
    renderer:InputText("Disabled input", "readonly")
    renderer:EndDisabled()
    renderer:Separator()
    renderer:Text("--- ProgressBar ---")
    ctx.progressVal = (ctx.progressVal + 0.002) % 1.0
    renderer:ProgressBar(ctx.progressVal, -1, 0)
    renderer:Text(string.format("%.0f%%", ctx.progressVal * 100))
    renderer:Separator()
    renderer:Text("--- SeparatorText ---")
    renderer:SeparatorText("Named Separator")
    renderer:Text("Content after named separator")
    renderer:Separator()
    renderer:Text("--- TextLink / TextLinkOpenURL ---")
    renderer:TextLink("https://github.com")
    renderer:SameLine()
    renderer:TextLinkOpenURL("Open GitHub", "https://github.com/TerrahKitsune/Lua-Kitsune")
    renderer:Separator()
    renderer:Text("--- Bullet / SmallButton / InvisibleButton ---")
    renderer:Bullet()
    renderer:SameLine()
    renderer:Text("Bullet point")
    renderer:SmallButton("Small")
    renderer:SameLine()
    renderer:InvisibleButton("##inv", 60, 20)
    if renderer:IsItemHovered() then
        renderer:Text("Invisible button hovered!")
    else
        renderer:Text("Hover the gap (60x20 after Small)")
    end
end

local function tabPopups(renderer, ctx)
    renderer:Text("--- Tooltip (hover next button) ---")
    renderer:Button("Hover me##tip")
    if renderer:IsItemHovered() then
        renderer:BeginTooltip()
        renderer:Text("I am a tooltip")
        renderer:Text("Frame: " .. ctx.frameCount)
        renderer:EndTooltip()
    end
    renderer:Separator()
    renderer:Text("Last popup action: " .. ctx.popupMsg)
    renderer:Text("--- BeginPopup ---")
    if renderer:Button("Open context popup") then
        renderer:OpenPopup("ctx_popup")
    end
    if renderer:BeginPopup("ctx_popup") then
        renderer:Text("Context Popup")
        renderer:Separator()
        if renderer:MenuItem("Action A") then ctx.popupMsg = "Action A" end
        if renderer:MenuItem("Action B") then ctx.popupMsg = "Action B" end
        renderer:Separator()
        if renderer:MenuItem("Close") then renderer:CloseCurrentPopup() end
        renderer:EndPopup()
    end
    renderer:Text("IsPopupOpen: " .. tostring(renderer:IsPopupOpen("ctx_popup")))
    renderer:Separator()
    renderer:Text("--- BeginPopupModal ---")
    if renderer:Button("Open modal") then
        renderer:OpenPopup("test_modal")
    end
    if renderer:BeginPopupModal("test_modal") then
        renderer:Text("This is a modal dialog.")
        renderer:Text("It blocks interaction with other windows.")
        renderer:Separator()
        if renderer:Button("OK##modal") then
            renderer:CloseCurrentPopup()
        end
        renderer:SameLine()
        if renderer:Button("Cancel##modal") then
            renderer:CloseCurrentPopup()
        end
        renderer:EndPopup()
    end
end

local function tabMenus(renderer, ctx)
    renderer:Text("Last menu click: " .. ctx.menuLastClick)
    renderer:Separator()
    renderer:Text("--- BeginMainMenuBar (top of screen) ---")
    if renderer:BeginMainMenuBar() then
        if renderer:BeginMenu("File") then
            if renderer:MenuItem("New")  then ctx.menuLastClick = "File > New"  end
            if renderer:MenuItem("Open") then ctx.menuLastClick = "File > Open" end
            renderer:Separator()
            if renderer:MenuItem("Quit") then ctx.menuLastClick = "File > Quit" end
            renderer:EndMenu()
        end
        if renderer:BeginMenu("Edit") then
            if renderer:MenuItem("Copy")  then ctx.menuLastClick = "Edit > Copy"  end
            if renderer:MenuItem("Paste") then ctx.menuLastClick = "Edit > Paste" end
            renderer:EndMenu()
        end
        if renderer:BeginMenu("Disabled", false) then
            renderer:EndMenu()
        end
        renderer:EndMainMenuBar()
    end
    renderer:Separator()
    renderer:Text("--- BeginMenuBar (child window with MenuBar flag) ---")
    renderer:Text("(rendered after main window so it stays on top)")
    ctx.showMenuDemo = true
end

local function tabTables(renderer, ctx)
    renderer:Text("--- BeginTable with headers ---")
    if renderer:BeginTable("t1", 3, tableFlags) then
        renderer:TableSetupColumn("Name")
        renderer:TableSetupColumn("Value")
        renderer:TableSetupColumn("Notes")
        renderer:TableHeadersRow()
        local rows = {
            { "Alpha", "1", "First row"  },
            { "Beta",  "2", "Second row" },
            { "Gamma", "3", "Third row"  },
        }
        for _, row in ipairs(rows) do
            renderer:TableNextRow()
            renderer:TableSetColumnIndex(0); renderer:Text(row[1])
            renderer:TableSetColumnIndex(1); renderer:Text(row[2])
            renderer:TableSetColumnIndex(2); renderer:Text(row[3])
        end
        renderer:EndTable()
    end
    renderer:Separator()
    renderer:Text("--- TableNextColumn ---")
    if renderer:BeginTable("t2", 2, tableFlags) then
        renderer:TableSetupColumn("Left")
        renderer:TableSetupColumn("Right")
        renderer:TableHeadersRow()
        for i = 1, 4 do
            renderer:TableNextRow()
            renderer:TableNextColumn(); renderer:Text("Row " .. i .. " left")
            renderer:TableNextColumn(); renderer:Text("Row " .. i .. " right")
        end
        renderer:EndTable()
    end
    renderer:Separator()
    renderer:Text("--- Table queries ---")
    if renderer:BeginTable("t3", 3) then
        renderer:TableSetupColumn("A")
        renderer:TableSetupColumn("B")
        renderer:TableSetupColumn("C")
        renderer:TableHeadersRow()
        renderer:TableNextRow()
        renderer:TableNextColumn(); renderer:Text("Count: "  .. renderer:TableGetColumnCount())
        renderer:TableNextColumn(); renderer:Text("ColIdx: " .. renderer:TableGetColumnIndex())
        renderer:TableNextColumn(); renderer:Text("RowIdx: " .. renderer:TableGetRowIndex())
        renderer:EndTable()
    end
end

local function tabCursor(renderer, ctx)
    renderer:Text("--- GetCursorPos / GetCursorScreenPos ---")
    local cx, cy = renderer:GetCursorPos()
    renderer:Text(string.format("CursorPos: %.0f, %.0f", cx, cy))
    local sx, sy = renderer:GetCursorScreenPos()
    renderer:Text(string.format("ScreenPos: %.0f, %.0f", sx, sy))
    renderer:Separator()
    renderer:Text("--- Manual cursor positioning ---")
    local bx, by = renderer:GetCursorPos()
    renderer:SetCursorPos(bx + 40, by + 10)
    renderer:Button("Offset button")
    renderer:SetCursorPos(bx, by + 50)
    renderer:Text("(back to baseline)")
    renderer:Separator()
    renderer:Text("--- GetContentRegionAvail ---")
    local avX, avY = renderer:GetContentRegionAvail()
    renderer:Text(string.format("Available: %.0f x %.0f", avX, avY))
    renderer:Separator()
    renderer:Text("--- Item geometry (hover button) ---")
    renderer:Button("Check my rect")
    local rMinX, rMinY = renderer:GetItemRectMin()
    local rMaxX, rMaxY = renderer:GetItemRectMax()
    local rSzX,  rSzY  = renderer:GetItemRectSize()
    renderer:Text("Hovered:   " .. tostring(renderer:IsItemHovered()))
    renderer:Text(string.format("Rect min:  %.0f, %.0f", rMinX, rMinY))
    renderer:Text(string.format("Rect max:  %.0f, %.0f", rMaxX, rMaxY))
    renderer:Text(string.format("Rect size: %.0f x %.0f", rSzX,  rSzY))
    renderer:Separator()
    renderer:Text("--- Window state queries ---")
    renderer:Text("IsWindowHovered:   " .. tostring(renderer:IsWindowHovered()))
    renderer:Text("IsWindowFocused:   " .. tostring(renderer:IsWindowFocused()))
    renderer:Text("IsWindowCollapsed: " .. tostring(renderer:IsWindowCollapsed()))
    renderer:Text("IsWindowAppearing: " .. tostring(renderer:IsWindowAppearing()))
    local wpX, wpY = renderer:GetWindowPos()
    local wsX, wsY = renderer:GetWindowSize()
    renderer:Text(string.format("GetWindowPos:  %.0f, %.0f", wpX, wpY))
    renderer:Text(string.format("GetWindowSize: %.0f x %.0f", wsX, wsY))
    renderer:Separator()
    renderer:Text("--- SetWindowFontScale ---")
    renderer:Text("Normal scale text")
    renderer:SetWindowFontScale(1.5)
    renderer:Text("1.5x scale text")
    renderer:SetWindowFontScale(1.0)
    renderer:Separator()
    renderer:Text("--- Scroll ---")
    if renderer:BeginChild("##scroll_demo", 0, 120, true) then
        renderer:Text(string.format("ScrollY: %.0f / %.0f", renderer:GetScrollY(), renderer:GetScrollMaxY()))
        if renderer:Button("Scroll to top") then
            renderer:SetScrollY(0)
        end
        renderer:SameLine()
        if renderer:Button("Scroll to bottom") then
            renderer:SetScrollY(renderer:GetScrollMaxY())
        end
        renderer:SameLine()
        if renderer:Button("SetScrollHereY to line 10") then
            ctx.scrollHereMid = true
        end
        for i = 1, 20 do
            renderer:Text("Scroll line " .. i)
            if i == 10 and ctx.scrollHereMid then
                -- Cursor is now just past line 10; 0.5 centres it in the view.
                renderer:SetScrollHereY(0.5)
                ctx.scrollHereMid = false
            end
        end
    end
    renderer:EndChild()
end

local function tabSystem(renderer, ctx)
    renderer:Text("--- Mouse ---")
    local mx, my = renderer:GetMousePos()
    renderer:Text(string.format("Mouse pos: %.0f, %.0f", mx, my))
    renderer:Text("LMB down:       " .. tostring(renderer:IsMouseDown(0)))
    renderer:Text("RMB down:       " .. tostring(renderer:IsMouseDown(1)))
    renderer:Text("LMB clicked:    " .. tostring(renderer:IsMouseClicked(0)))
    renderer:Text("LMB dbl-click:  " .. tostring(renderer:IsMouseDoubleClicked(0)))
    local dx, dy = renderer:GetMouseDragDelta(0)
    renderer:Text(string.format("Drag delta LMB: %.1f, %.1f", dx, dy))
    renderer:Separator()
    renderer:Text("--- Keyboard ---")
    renderer:Text("Enter down:    " .. tostring(renderer:IsKeyDown(keyEnter)))
    renderer:Text("Space pressed: " .. tostring(renderer:IsKeyPressed(keySpace)))
    renderer:Text("Enter name:    " .. (renderer:GetKeyName(keyEnter) or "?"))
    renderer:Separator()
    renderer:Text("--- Clipboard ---")
    renderer:Text("Content: " .. (renderer:GetClipboardText() or ""))
    if renderer:Button("Copy test string") then
        renderer:SetClipboardText("hello from imgui_test.lua")
    end
    renderer:Separator()
    renderer:Text("--- Metrics ---")
    renderer:Text(string.format("Time:        %.2f", renderer:GetTime()))
    renderer:Text("Frame count: "             .. renderer:GetFrameCount())
    renderer:Text("Version:     "             .. renderer:GetVersion())
    renderer:Text(string.format("Font size:   %.0f", renderer:GetFontSize()))
    renderer:Text(string.format("Line height: %.0f", renderer:GetTextLineHeight()))
    local tw, th = renderer:CalcTextSize("Hello, ImGui!")
    renderer:Text(string.format("CalcTextSize: %.0f x %.0f", tw, th))
    renderer:Separator()
    renderer:Text("--- Debug windows ---")
    if renderer:Button("Toggle Demo")    then ctx.showDemo    = not ctx.showDemo    end
    renderer:SameLine()
    if renderer:Button("Toggle Metrics") then ctx.showMetrics = not ctx.showMetrics end
    renderer:Separator()
    renderer:Text("--- Console ---")
    renderer:Text("Owns console: " .. tostring(ctx.ownsConsole))
    if ctx.ownsConsole then
        if renderer:Button("Toggle (minimize/restore)") then
            ctx.consoleVisible = not ctx.consoleVisible
            local actual = Win32.Console(ctx.consoleVisible)
            if actual ~= nil then ctx.consoleVisible = actual end
        end
        renderer:SameLine()
        renderer:Text(ctx.consoleVisible and "visible" or "minimized")
        if renderer:Button("Destroy Console") then
            Win32.DestroyConsole()
            ctx.consoleVisible = false
        end
    else
        renderer:TextDisabled("Console not owned by this process (launched from shell)")
    end
    if ctx.showDemo    then ctx.showDemo    = renderer:ShowDemoWindow(ctx.showDemo)       == true end
    if ctx.showMetrics then ctx.showMetrics = renderer:ShowMetricsWindow(ctx.showMetrics) == true end
end

local function tabMarkdown(renderer, ctx)
    local refresh = renderer:Button('Reload')
    renderer:SameLine()
    renderer:Text('docs/markdown-test.md')
    renderer:Separator()
    renderer:MarkdownRender(ctx.mdDoc, refresh)
end

local function render(renderer, ctx)
    ctx.frameCount = ctx.frameCount + 1

    if not ctx.scheduled then
        ctx.scheduled = true
        Imgui.Schedule(function()
            ctx.scheduleResult = "fired on frame 1"
        end)
    end

    local winW = SDL.GetWindowWidth()
    local winH = SDL.GetWindowHeight()
    renderer:SetNextWindowPos(0, 0, condAlways)
    renderer:SetNextWindowSize(winW, winH, condAlways)

    local open, stillOpen = renderer:Begin("Kitsune ImGui Test", ctx.windowOpen, noResize)
    ctx.windowOpen = stillOpen
    if not open then
        renderer:End()
        return ctx.windowOpen
    end

    if renderer:BeginTabBar("MainTabs", tabFlags) then
        if renderer:BeginTabItem("Info") then
            tabInfo(renderer, ctx)
            renderer:EndTabItem()
        end
        if renderer:BeginTabItem("Widgets") then
            tabWidgets(renderer, ctx)
            renderer:EndTabItem()
        end
        if renderer:BeginTabItem("Text") then
            tabText(renderer, ctx)
            renderer:EndTabItem()
        end
        if renderer:BeginTabItem("Input") then
            tabInput(renderer, ctx)
            renderer:EndTabItem()
        end
        if renderer:BeginTabItem("Layout") then
            tabLayout(renderer, ctx)
            renderer:EndTabItem()
        end
        if renderer:BeginTabItem("Schedule") then
            tabSchedule(renderer, ctx)
            renderer:EndTabItem()
        end
        if renderer:BeginTabItem("Drag") then
            tabDrag(renderer, ctx)
            renderer:EndTabItem()
        end
        if renderer:BeginTabItem("ExtraInput") then
            tabExtraInput(renderer, ctx)
            renderer:EndTabItem()
        end
        if renderer:BeginTabItem("Tree") then
            tabTree(renderer, ctx)
            renderer:EndTabItem()
        end
        if renderer:BeginTabItem("Style") then
            tabStyle(renderer, ctx)
            renderer:EndTabItem()
        end
        if renderer:BeginTabItem("Popups") then
            tabPopups(renderer, ctx)
            renderer:EndTabItem()
        end
        if renderer:BeginTabItem("Menus") then
            tabMenus(renderer, ctx)
            renderer:EndTabItem()
        end
        if renderer:BeginTabItem("Tables") then
            tabTables(renderer, ctx)
            renderer:EndTabItem()
        end
        if renderer:BeginTabItem("Cursor") then
            tabCursor(renderer, ctx)
            renderer:EndTabItem()
        end
        if renderer:BeginTabItem('System') then
            tabSystem(renderer, ctx)
            renderer:EndTabItem()
        end
        if renderer:BeginTabItem('Markdown') then
            tabMarkdown(renderer, ctx)
            renderer:EndTabItem()
        end
        renderer:EndTabBar()
    end

    local shouldClose = renderer:Button("Close")
    renderer:End()

    -- Render the menu demo sub-window AFTER the main End() so ImGui draws it
    -- on top regardless of which window last had focus.
    local wantMenuDemo = ctx.showMenuDemo
    ctx.showMenuDemo = false  -- reset; tabMenus sets it true each frame it is active
    if wantMenuDemo then
        renderer:SetNextWindowPos(30, 230, condAlways)
        renderer:SetNextWindowSize(300, 80, condAlways)
        local isOpen = renderer:Begin("##menu_demo_win", nil, menuBarFlag)
        if isOpen then
            if renderer:BeginMenuBar() then
                if renderer:BeginMenu("Tools") then
                    if renderer:MenuItem("Metrics") then ctx.menuLastClick = "Tools > Metrics" end
                    if renderer:MenuItem("Demo")    then ctx.menuLastClick = "Tools > Demo"    end
                    renderer:EndMenu()
                end
                renderer:EndMenuBar()
            end
            renderer:Text("Window with embedded menu bar")
        end
        renderer:End()
    end

    return ctx.windowOpen and not shouldClose
end

-- ---------------------------------------------------------------------------
-- Start Point
-- ---------------------------------------------------------------------------

Imgui.Start("Kitsune ImGui Test", 800, 600, render, ctx, onError)
OpenGL.SetResourceLoader(onResourceLoad);