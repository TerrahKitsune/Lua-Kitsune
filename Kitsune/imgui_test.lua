local context = {
    frameCount    = 0,
    scheduleResult = "pending",
    lastError     = "none",
    scheduled     = false,
}

local function onError(err)
    context.lastError = err
    return true
end

local noResizeFlag = Imgui.Enum.ImGuiWindowFlags.NoResize

local function render(renderer, ctx)
    ctx.frameCount = ctx.frameCount + 1

    -- Schedule fires once on the first frame when the session is live
    if not ctx.scheduled then
        ctx.scheduled = true
        Imgui.Schedule(function()
            ctx.scheduleResult = "scheduled function ran"
        end)
    end

    renderer:SetNextWindowPos(10, 10)
    renderer:SetNextWindowSize(780, 560)
    local open = renderer:Begin("Kitsune ImGui Test", nil, noResizeFlag)

    if open then
        renderer:Text("Frame: " .. ctx.frameCount)
        renderer:Text("Schedule result: " .. ctx.scheduleResult)
        renderer:Text("Last error: " .. tostring(ctx.lastError))
        renderer:Separator()

        local w = Imgui.GetWindowWidth()
        local h = Imgui.GetWindowHeight()
        local x = Imgui.GetWindowX()
        local y = Imgui.GetWindowY()
        renderer:Text("Window size: " .. w .. " x " .. h)
        renderer:Text("Window position: " .. x .. ", " .. y)
        renderer:Text("Focused: " .. tostring(Imgui.IsFocused()))
        renderer:Text("Minimized: " .. tostring(Imgui.IsMinimized()))

        local monIdx, monName, monX, monY, monW, monH, monHz = Imgui.GetMonitor()
        if monIdx then
            renderer:Text("Monitor " .. monIdx .. ": " .. monName ..
                " (" .. monW .. "x" .. monH .. " @" .. monHz .. "Hz)" ..
                " at " .. monX .. ", " .. monY)
        end

        renderer:Separator()

        local shouldError = ctx.frameCount == 5
        local shouldClose = renderer:Button("Close")

        renderer:End()

        if shouldError then
            error("intentional test error on frame 5")
        end

        if shouldClose then
            return false
        end
        return true
    end
end

Imgui.Start("Kitsune ImGui Test", 800, 600, render, context, onError)
