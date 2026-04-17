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
}

local function onError(err)
    ctx.lastError = tostring(err)
    return true
end

local noResize   = Imgui.Enum.ImGuiWindowFlags.NoResize
local condAlways = Imgui.Enum.ImGuiCond.Always
local tabFlags = Imgui.Enum.ImGuiTabBarFlags.None

local function tabInfo(renderer, ctx)
    renderer:Text("Frame:       " .. ctx.frameCount)
    renderer:Text("Schedule:    " .. ctx.scheduleResult)
    renderer:Text("Last error:  " .. ctx.lastError)
    renderer:Separator()
    renderer:Text("Window size: " .. Imgui.GetWindowWidth() .. " x " .. Imgui.GetWindowHeight())
    renderer:Text("Window pos:  " .. Imgui.GetWindowX() .. ", " .. Imgui.GetWindowY())
    renderer:Text("Focused:     " .. tostring(Imgui.IsFocused()))
    renderer:Text("Minimized:   " .. tostring(Imgui.IsMinimized()))
    local idx, name, mx, my, mw, mh, hz = Imgui.GetMonitor()
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
        local c, v = renderer:RadioButton("Option " .. i, ctx.radioSel == i)
        if c and v then ctx.radioSel = i end
        if i < 2 then renderer:SameLine() end
    end
    renderer:Text("Selected: " .. ctx.radioSel)
    renderer:Separator()
    renderer:Text("--- Sliders ---")
    local fc, fv = renderer:SliderFloat("Float", ctx.sliderFloat, 0.0, 1.0)
    if fc then ctx.sliderFloat = fv end
    local ic, iv = renderer:SliderInt("Int", ctx.sliderInt, 0, 20)
    if ic then ctx.sliderInt = iv end
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

local function render(renderer, ctx)
    ctx.frameCount = ctx.frameCount + 1

    if ctx.frameCount > 100 then
        return false;
    end

    if not ctx.scheduled then
        ctx.scheduled = true
        Imgui.Schedule(function()
            ctx.scheduleResult = "fired on frame 1"
        end)
    end

    renderer:SetNextWindowPos(10, 10, condAlways)
    renderer:SetNextWindowSize(780, 560, condAlways)

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
        renderer:EndTabBar()
    end

    local shouldClose = renderer:Button("Close")
    renderer:End()
    return ctx.windowOpen and not shouldClose
end

Imgui.Start("Kitsune ImGui Test", 800, 600, render, ctx, onError)
