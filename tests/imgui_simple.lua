math.randomseed(Time())

local BG_TAG = "demo.bg"
local WINDOW_OPEN_TAG = "demo.window.open"
local ENABLE_TAG = "demo.enable"
local VOLUME_TAG = "demo.volume"

local ui
ui = Imgui.Create("Lua-Kitsune Imgui Demo", BG_TAG, 900, 600, function(draw)
    if not ui then return end -- Guard against nil ui during initialization
    
    draw:SetNextWindowSize({ x = 420, y = 260 }, 4) -- ImGuiCond_FirstUseEver

    if draw:Begin("Simple UI", WINDOW_OPEN_TAG, 0) then
        draw:Text("Hello from Lua + ImGui")
        draw:Separator()

        draw:Checkbox("Enable option", ENABLE_TAG)
        draw:SliderFloat("Volume", VOLUME_TAG, 0.0, 100.0, "%.1f")

        if draw:Button("Random background") then
            local r = math.random(25, 80)
            local g = math.random(25, 80)
            local b = math.random(25, 80)
            ui:SetValue(BG_TAG, 3, Imgui.RGBToVec4(r, g, b))
        end

        draw:SameLine()
        if draw:Button("Quit") then
            ui:Close()
        end

        draw:Separator()

        local enabled = ui:GetValue(ENABLE_TAG, 1)
        local volume = ui:GetValue(VOLUME_TAG, 2) or 0.0

        draw:Text(string.format("Enabled: %s", tostring(enabled == true)))
        draw:Text(string.format("Volume: %.1f", volume))

        draw:End()
    end
end)

if not ui then
    error("Failed to create Imgui window")
end

ui:SetValue(BG_TAG, 3, Imgui.RGBToVec4(35, 35, 40))
ui:SetValue(WINDOW_OPEN_TAG, 1, true)
ui:SetValue(ENABLE_TAG, 1, true)
ui:SetValue(VOLUME_TAG, 2, 35.0)

while ui:Tick() do
    Sleep(1)
end
