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
    passwordText   = "",
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
    -- Hardware sensor cache (refreshed every 60 frames)
    hw_cpuName     = Hardware.CpuName() or "n/a",
    hw_cpuLoad     = "priming...",
    hw_mem         = Hardware.Memory(),
    hw_cpuTemps    = Hardware.CpuTemp(),
    hw_threads_load = Hardware.CpuThreadsLoad(),
    hw_battery     = Hardware.Battery(),
    hw_gpuMemory   = Hardware.GpuMemory(),
    hw_gpuLoad     = Hardware.GpuLoad(),
    hw_diskIO      = Hardware.DiskIO(),
    hw_networkIO   = Hardware.NetworkIO(),
    hw_lastRefresh = -60,
    -- Audio (nil = not yet attempted, false = unavailable)
    audioSfxId    = nil,
    audioSfxId2   = nil,
    audioMusicId  = nil,
    audioMusicId2 = nil,
    audioSfxVol   = 128,
    audioMusicVol = 128,
}

local function onResourceLoad(type, path)
    if type == 1 then -- RESOURCE_TEXTURE
        if path == "sample.png" then
            local ok, stream = pcall(Stream.Open, "./docs/sample.png", 'rb')
            return ok and stream or nil
        end
    elseif type == 4 then -- RESOURCE_FONT ("FaceName:size:style")
        local face, style = path:match('^([^:]+):[^:]+:(%d+)$')
        style = tonumber(style) or 0
        local bold   = (style & 1) ~= 0
        local italic = (style & 2) ~= 0
        -- libra-sans: all variants share the face name "libra-sans", style bits select the file
        if face == 'libra-sans' then
            local file
            if bold and italic then
                file = './docs/libra-sans.bold-italic.ttf'
            elseif bold then
                file = './docs/libra-sans.bold.ttf'
            elseif italic then
                file = './docs/libra-sans.italic.ttf'
            else
                file = './docs/libra-sans.regular.ttf'
            end
            local ok, stream = pcall(Stream.Open, file, 'rb')
            return ok and stream or nil
        elseif face == '00209 Regular' then
            local ok, stream = pcall(Stream.Open, './docs/00209 Regular.ttf', 'rb')
            return ok and stream or nil
        elseif face == 'Galahad Std Regular14' then
            local ok, stream = pcall(Stream.Open, './docs/Galahad Std Regular14.otf', 'rb')
            return ok and stream or nil
        end
        return nil
    elseif type == 5 then -- RESOURCE_GENERIC (HTML, CSS, misc)
        if not path or path == '' then return nil end
        local ok, stream = pcall(Stream.Open, './' .. path, 'rb')
        if ok and stream then return stream end
        ok, stream = pcall(Stream.Open, './docs/' .. path, 'rb')
        if ok and stream then return stream end
        return nil
    end
    print("Unknown resource requested: type=" .. tostring(type) .. " path=" .. tostring(path))
    return nil
end

local function onError(err)
    ctx.lastError = tostring(err)
    return true
end

local function hardwareQuery(ctx)

    Hardware.CpuLoad()  -- prime / advance baseline
    Sleep(100)
    ctx.hw_mem      = Hardware.Memory()
    Sleep(100)
    ctx.hw_cpuTemps = Hardware.CpuTemp()
    Sleep(100)
    ctx.hw_threads_load = Hardware.CpuThreadsLoad()
    Sleep(100)
    ctx.hw_battery  = Hardware.Battery()
    Sleep(100)
    ctx.hw_gpuMemory = Hardware.GpuMemory()
    Sleep(100)
    ctx.hw_gpuLoad   = Hardware.GpuLoad()
    Sleep(100)
    ctx.hw_diskIO    = Hardware.DiskIO()
    Sleep(100)
    ctx.hw_networkIO = Hardware.NetworkIO()
    Sleep(100)
    local load = Hardware.CpuLoad()
    ctx.hw_cpuLoad = load and string.format("%.1f%%", load) or "n/a"
    Sleep(100)
    ctx.hw_block = false;    
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

    -- Refresh hardware sensors
    if not ctx.hw_block then
        ctx.hw_block = true; -- Block until our scheduled query is done
        Imgui.Schedule(hardwareQuery, ctx)
    end

    renderer:Text("Frame:       " .. ctx.frameCount)
    renderer:Text("Schedule:    " .. ctx.scheduleResult)
    if renderer:SmallButton("Last error:  " .. ctx.lastError) then
        Session.Clipboard.Set(ctx.lastError)
    end
    renderer:Separator()

    -- Window / display
    renderer:Text("Window size: " .. SDL.Window.GetWindowWidth() .. " x " .. SDL.Window.GetWindowHeight())
    renderer:Text("Window pos:  " .. SDL.Window.GetWindowX() .. ", " .. SDL.Window.GetWindowY())
    renderer:Text("Focused:     " .. tostring(SDL.Window.IsFocused()))
    renderer:Text("Minimized:   " .. tostring(SDL.Window.IsMinimized()))
    local idx, name, mx, my, mw, mh, hz = SDL.Window.GetMonitor()
    if idx then
        renderer:Text("Monitor " .. idx .. ": " .. name .. " (" .. mw .. "x" .. mh .. " @" .. hz .. "Hz)")
    end
    renderer:Separator()

    -- Timing
    local ticks    = SDL.Time.GetTicks()
    local freq     = SDL.Time.GetPerformanceFrequency()
    local dt, fps  = SDL.Time.GetFrameTime()
    renderer:Text("SDL.Time.GetTicks():               " .. ticks .. " ms")
    renderer:Text("SDL.Time.GetPerformanceFrequency(): " .. freq)
    renderer:Text("SDL.Time.GetPerformanceCounter():   " .. SDL.Time.GetPerformanceCounter())
    renderer:Text(string.format("SDL.Time.GetFrameTime() dt:         %.4f s", dt))
    renderer:Text(string.format("SDL.Time.GetFrameTime() fps:        %.1f fps", fps))
    renderer:Separator()

    -- Input state
    local mx2, my2, mb = SDL.Input.GetMouseState()
    renderer:Text("Mouse pos:     " .. mx2 .. ", " .. my2)
    renderer:Text("Mouse buttons: " .. mb .. "  (bit0=L bit1=M bit2=R)")
    local mods = SDL.Input.GetModState()
    renderer:Text("Mod state:     shift=" .. tostring(mods.shift) ..
        "  ctrl=" .. tostring(mods.ctrl) ..
        "  alt=" .. tostring(mods.alt))
    renderer:Text("W key held:    " .. tostring(SDL.Input.GetKeyState(26)))   -- SDL_SCANCODE_W = 26
    renderer:Text("Space held:    " .. tostring(SDL.Input.GetKeyState(44)))   -- SDL_SCANCODE_SPACE = 44
    renderer:Separator()

    -- Gamepad
    local npads = SDL.Input.GetNumJoysticks()
    renderer:Text("Joysticks: " .. npads)
    if npads > 0 then
        renderer:Text("  Axis 0 (LX): " .. string.format("%.3f", SDL.Input.GetGamepadAxis(0, 0)))
        renderer:Text("  Axis 1 (LY): " .. string.format("%.3f", SDL.Input.GetGamepadAxis(0, 1)))
        renderer:Text("  Button A:    " .. tostring(SDL.Input.GetGamepadButton(0, 0)))
    end
    renderer:Separator()

    -- Texture cache
    local count, bytes = OpenGL.GetTextureCount()
    renderer:Text("OpenGL textures: " .. count .. "  (~" .. math.floor(bytes / 1024) .. " KB GPU)")
    local ids = OpenGL.GetAllLoadedTextures()
    for i = 1, #ids do
        local id   = ids[i]
        local data = OpenGL.GetData(id)
        if data then
            renderer:Text(string.format("  [%d] id=%-3d  %4dx%-4d  loaded=%-5s  source=%s",
                i, id, data.width, data.height,
                tostring(data.isLoaded),
                data.source or "(none)"))
        end
    end
    renderer:Separator()

    -- Hardware (summary only)
    renderer:Text("--- Hardware ---")
    renderer:Text("CPU:         " .. (ctx.hw_cpuName or "n/a"))
    renderer:Text("CPU load:    " .. (ctx.hw_cpuLoad or "n/a"))
    local mem = ctx.hw_mem
    if mem then
        renderer:Text(string.format("RAM:         %d MB used / %d MB total (%d%%)",
            mem.TotalPhys - mem.AvailPhys, mem.TotalPhys, mem.LoadPercent))
        renderer:Text(string.format("Swap:        %d MB free / %d MB total",
            mem.AvailSwap, mem.TotalSwap))
    else
        renderer:Text("RAM:         n/a")
    end
    local cpuTemps = ctx.hw_cpuTemps
    if cpuTemps and #cpuTemps > 0 then
        for _, t in ipairs(cpuTemps) do
            renderer:Text(string.format("  %-28s %.1f°C", t.Name, t.Value))
        end
    else
        renderer:Text("CPU temp:    n/a")
    end
    local bat = ctx.hw_battery
    if bat then
        local pct    = bat.Percent and (bat.Percent .. "%") or "?"
        local status = bat.Charging and "Charging" or (bat.ACLine and "Plugged in" or "Discharging")
        local remain = ""
        if bat.SecondsRemaining then
            local h = math.floor(bat.SecondsRemaining / 3600)
            local m = math.floor((bat.SecondsRemaining % 3600) / 60)
            remain = string.format("  (%dh %02dm remaining)", h, m)
        end
        renderer:Text(string.format("Battery:     %s  %s%s", pct, status, remain))
    else
        renderer:Text("Battery:     n/a")
    end
end

local function fmtBytes(n)
    if n >= 1073741824 then return string.format("%.2f GB", n / 1073741824)
    elseif n >= 1048576 then return string.format("%.2f MB", n / 1048576)
    elseif n >= 1024    then return string.format("%.1f KB", n / 1024)
    else                     return string.format("%d B",    math.floor(n))
    end
end

local function tabHardware(renderer, ctx)

    -- Refresh hardware sensors
    if not ctx.hw_block then
        ctx.hw_block = true; -- Block until our scheduled query is done
        Imgui.Schedule(hardwareQuery, ctx)
    end

    -- Per-thread CPU load
    renderer:Text("--- Thread Load ---")
    local threads = ctx.hw_threads_load
    if threads then
        local tkeys = {}
        for k in pairs(threads) do tkeys[#tkeys+1] = k end
        table.sort(tkeys)
        if #tkeys > 0 then
            renderer:Text(string.format("%d threads", #tkeys))
            for _, k in ipairs(tkeys) do
                renderer:Text(string.format("Cpu thread %-12s %.1f%%", k, threads[k]))
            end
        else
            renderer:Text("  none")
        end
    else
        renderer:Text("  n/a")
    end
    renderer:Separator()

    -- GPU memory per adapter
    renderer:Text("--- GPU Memory ---")
    local gpuMem = ctx.hw_gpuMemory
    if gpuMem then
        local adapters = {}
        for k in pairs(gpuMem) do adapters[#adapters+1] = k end
        table.sort(adapters)
        if #adapters > 0 then
            for _, name in ipairs(adapters) do
                local m = gpuMem[name]
                renderer:Text(string.format("  %s", name))
                renderer:Text(string.format("    Dedicated: %d MB  Shared: %d MB  Total: %d MB",
                    m.DedicatedUsageMB or 0, m.SharedUsageMB or 0, m.TotalCommittedMB or 0))
            end
        else
            renderer:Text("  none")
        end
    else
        renderer:Text("  n/a")
    end
    renderer:Separator()

    -- GPU engine load per adapter
    renderer:Text("--- GPU Load ---")
    local gpuLoad = ctx.hw_gpuLoad
    if gpuLoad then
        local adapters = {}
        for k in pairs(gpuLoad) do adapters[#adapters+1] = k end
        table.sort(adapters)
        if #adapters > 0 then
            for _, name in ipairs(adapters) do
                local eng = gpuLoad[name]
                renderer:Text(string.format("  %s", name))
                local etypes = {}
                for e in pairs(eng) do etypes[#etypes+1] = e end
                table.sort(etypes)
                for _, e in ipairs(etypes) do
                    if eng[e] > 0 then
                        renderer:Text(string.format("    %-20s %.1f%%", e, eng[e]))
                    end
                end
            end
        else
            renderer:Text("  none")
        end
    else
        renderer:Text("  n/a")
    end
    renderer:Separator()

    -- Disk I/O
    renderer:Text("--- Disk I/O ---")
    local diskIO = ctx.hw_diskIO
    if diskIO then
        local disks = {}
        for k in pairs(diskIO) do disks[#disks+1] = k end
        table.sort(disks)
        if #disks > 0 then
            for _, name in ipairs(disks) do
                local d = diskIO[name]
                renderer:Text(string.format("  %s", name))
                renderer:Text(string.format("    Read: %s/s  Write: %s/s%s",
                    fmtBytes(d.ReadBytesPerSec  or 0),
                    fmtBytes(d.WriteBytesPerSec or 0),
                    d.ActivePercent and string.format("  Active: %.1f%%", d.ActivePercent) or ""))
            end
        else
            renderer:Text("  none")
        end
    else
        renderer:Text("  n/a")
    end
    renderer:Separator()

    -- Network I/O
    renderer:Text("--- Network I/O ---")
    local netIO = ctx.hw_networkIO
    if netIO then
        local nics = {}
        for k in pairs(netIO) do nics[#nics+1] = k end
        table.sort(nics)
        if #nics > 0 then
            for _, name in ipairs(nics) do
                local n = netIO[name]
                renderer:Text(string.format("  %s", name))
                renderer:Text(string.format("    Recv: %s/s  Send: %s/s  Total: %s/s",
                    fmtBytes(n.RecvBytesPerSec   or 0),
                    fmtBytes(n.SendBytesPerSec   or 0),
                    fmtBytes(n.TotalBytesPerSec  or 0)))
            end
        else
            renderer:Text("  none")
        end
    else
        renderer:Text("  n/a")
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
    renderer:Separator()
    renderer:Text("--- Password masking ---")
    local pwChanged, pwVal = renderer:InputText("Password", ctx.passwordText, Imgui.Enum.ImGuiInputTextFlags.Password)
    if pwChanged then ctx.passwordText = pwVal end
    renderer:Text("Length: " .. #ctx.passwordText .. " char(s)")
    renderer:Text("Actual: " .. ctx.passwordText)
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

    if renderer:Button("Schedule error") then
        Imgui.Schedule(function()
            error("Oh no there was an error (test)");
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
    renderer:Separator()
    renderer:Text("--- Fonts (libra-sans, size 18) ---")
    if not ctx.fontRegular then
        ctx.fontRegular    = Font.Resolve('libra-sans', 18, false, false)
        ctx.fontBold       = Font.Resolve('libra-sans', 18, true,  false)
        ctx.fontItalic     = Font.Resolve('libra-sans', 18, false, true)
        ctx.fontBoldItalic = Font.Resolve('libra-sans', 18, true,  true)
    end
    local function fontRow(label, id)
        if id then
            renderer:PushFont(id)
            renderer:Text(label)
            renderer:PopFont()
        else
            renderer:TextDisabled(label .. ' (not loaded)')
        end
    end
    fontRow('Regular — The quick brown fox jumps over the lazy dog',     ctx.fontRegular)
    fontRow('Bold — The quick brown fox jumps over the lazy dog',        ctx.fontBold)
    fontRow('Italic — The quick brown fox jumps over the lazy dog',      ctx.fontItalic)
    fontRow('Bold-Italic — The quick brown fox jumps over the lazy dog', ctx.fontBoldItalic)
    renderer:Separator()
    renderer:Text("--- PushBold / PushItalic (auto-resolve from current font) ---")
    if ctx.fontRegular then
        renderer:PushFont(ctx.fontRegular)
        renderer:Text('Using libra-sans as base:')
        renderer:PushBold()
        renderer:Text('  Bold via PushBold — should match the Bold row above')
        renderer:PopBold()
        renderer:PushItalic()
        renderer:Text('  Italic via PushItalic — should match the Italic row above')
        renderer:PopItalic()
        renderer:PopFont()
        renderer:Text('Using default ImGui font as base (no libra-sans push):')
        renderer:PushBold()
        renderer:Text('  Bold via PushBold — gold fallback if no bold variant for default font')
        renderer:PopBold()
        renderer:PushItalic()
        renderer:Text('  Italic via PushItalic — grey fallback if no italic variant for default font')
        renderer:PopItalic()
    else
        renderer:TextDisabled('Fonts not loaded — PushBold/PushItalic demo skipped')
    end
    renderer:Separator()
    renderer:Text("--- Default Font (libra-sans, size 18) ---")
    renderer:Text("Current default: " .. tostring(ctx.defaultFontName or 'ImGui built-in'))
    if renderer:Button("Set libra-sans as default font") then
        if not ctx.fontRegular then
            ctx.fontRegular = Font.Resolve('libra-sans', 18, false, false)
        end
        if ctx.fontRegular then
            Font.SetDefault(ctx.fontRegular)
            ctx.defaultFontName = 'libra-sans.regular 18'
        else
            ctx.defaultFontName = 'libra-sans.regular 18 (pending atlas build)'
        end
    end
    renderer:SameLine()
    if renderer:Button("Restore built-in default") then
        Font.SetDefault(nil)
        ctx.defaultFontName = 'ImGui built-in'
    end
    renderer:Text("Once libra-sans is default, PushBold/PushItalic resolve bold/italic automatically.")
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

local function tabAudio(renderer, ctx)
    -- Lazy-load on first visit
    if ctx.audioSfxId == nil then
        local stream = Stream.Open('./docs/sample.wav', 'rb')
        ctx.audioSfxId = stream and SDL.Audio.Load(stream, 'sample.wav') or false
    end
    if ctx.audioSfxId2 == nil then
        local stream = Stream.Open('./docs/sample2.wav', 'rb')
        ctx.audioSfxId2 = stream and SDL.Audio.Load(stream, 'sample2.wav') or false
    end
    if ctx.audioMusicId == nil then
        local stream = Stream.Open('./docs/sample.ogg', 'rb')
        ctx.audioMusicId = stream and SDL.Audio.LoadMusic(stream, 'sample.ogg') or false
    end
    if ctx.audioMusicId2 == nil then
        local stream = Stream.Open('./docs/sample2.ogg', 'rb')
        ctx.audioMusicId2 = stream and SDL.Audio.LoadMusic(stream, 'sample2.ogg') or false
    end

    -- ---------------------------------------------------------------------------
    -- Sound Effects
    -- ---------------------------------------------------------------------------
    renderer:SeparatorText("Sound Effects")

    local function sfxRow(label, id)
        if not id then
            renderer:TextDisabled(label .. ": not found in docs/")
            return
        end
        local data = SDL.Audio.GetData(id)
        renderer:Text(string.format("%-14s  id=%-3d  loaded=%-5s  playing=%s",
            label, id,
            tostring(data and data.isLoaded),
            tostring(SDL.Audio.IsPlaying(id))))
        renderer:SameLine()
        if renderer:Button("Play##" .. label) then SDL.Audio.Play(id) end
        renderer:SameLine()
        if renderer:Button("x3##" .. label) then SDL.Audio.Play(id, 2) end
        renderer:SameLine()
        if renderer:Button("Stop##" .. label) then SDL.Audio.Stop(id) end
        renderer:SameLine()
        if renderer:Button("FadeIn##" .. label) then SDL.Audio.FadeIn(id, 500) end
        renderer:SameLine()
        if renderer:Button("FadeOut##" .. label) then SDL.Audio.FadeOut(id, 500) end
        renderer:SameLine()
        if renderer:Button("Destroy##" .. label) then
            SDL.Audio.Destroy(id)
            if id == ctx.audioSfxId  then ctx.audioSfxId  = nil end
            if id == ctx.audioSfxId2 then ctx.audioSfxId2 = nil end
        end
    end

    sfxRow("sample.wav",  ctx.audioSfxId)
    sfxRow("sample2.wav", ctx.audioSfxId2)

    if ctx.audioSfxId then
        renderer:Text("Volume (0-128):")
        renderer:SameLine()
        local vc, vv = renderer:SliderInt("##sfxvol", ctx.audioSfxVol, 0, 128)
        if vc then
            ctx.audioSfxVol = vv
            SDL.Audio.SetVolume(ctx.audioSfxId, vv)
        end
    end

    if renderer:Button("Stop All SFX") then SDL.Audio.Stop() end

    -- ---------------------------------------------------------------------------
    -- Music
    -- ---------------------------------------------------------------------------
    renderer:SeparatorText("Music")

    local currentMusic = SDL.Audio.GetCurrentMusic()
    renderer:Text("playing:        " .. tostring(SDL.Audio.IsMusicPlaying()))
    renderer:Text("paused:         " .. tostring(SDL.Audio.IsMusicPaused()))
    renderer:Text("GetCurrentMusic: " .. tostring(currentMusic))
    renderer:Separator()

    local function musicRow(label, id)
        if not id then
            renderer:TextDisabled(label .. ": not found in docs/")
            return
        end
        local data   = SDL.Audio.GetData(id)
        local active = currentMusic == id
        renderer:Text(string.format("%-14s  id=%-3d  loaded=%-5s  %s",
            label, id,
            tostring(data and data.isLoaded),
            active and "[ACTIVE]" or ""))
        renderer:SameLine()
        if renderer:Button("Play loop##" .. label) then SDL.Audio.PlayMusic(id, -1) end
        renderer:SameLine()
        if renderer:Button("Play once##" .. label) then SDL.Audio.PlayMusic(id, 1) end
        renderer:SameLine()
        if renderer:Button("Destroy##" .. label) then
            SDL.Audio.DestroyMusic(id)
            if id == ctx.audioMusicId  then ctx.audioMusicId  = nil end
            if id == ctx.audioMusicId2 then ctx.audioMusicId2 = nil end
        end
    end

    musicRow("sample.ogg",  ctx.audioMusicId)
    musicRow("sample2.ogg", ctx.audioMusicId2)

    renderer:Spacing()
    if renderer:Button("Stop##music")   then SDL.Audio.StopMusic() end
    renderer:SameLine()
    if renderer:Button("Pause##music")  then SDL.Audio.PauseMusic() end
    renderer:SameLine()
    if renderer:Button("Resume##music") then SDL.Audio.ResumeMusic() end

    renderer:Text("Music Volume (0-128):")
    renderer:SameLine()
    local mvc, mvv = renderer:SliderInt("##musicvol", ctx.audioMusicVol, 0, 128)
    if mvc then
        ctx.audioMusicVol = mvv
        SDL.Audio.SetMusicVolume(mvv)
    end

    -- ---------------------------------------------------------------------------
    -- Cache queries
    -- ---------------------------------------------------------------------------
    renderer:SeparatorText("Cache")
    renderer:Text("GetId('sample.wav'):  " .. tostring(SDL.Audio.GetId('sample.wav')))
    renderer:Text("GetId('sample2.wav'): " .. tostring(SDL.Audio.GetId('sample2.wav')))
    renderer:Text("GetId('sample.ogg'):  " .. tostring(SDL.Audio.GetId('sample.ogg')))
    renderer:Text("GetId('sample2.ogg'): " .. tostring(SDL.Audio.GetId('sample2.ogg')))
    renderer:Spacing()
    if renderer:Button("DestroyAll") then
        SDL.Audio.DestroyAll()
        ctx.audioSfxId    = nil
        ctx.audioSfxId2   = nil
        ctx.audioMusicId  = nil
        ctx.audioMusicId2 = nil
        ctx.audioSfxVol   = 128
        ctx.audioMusicVol = 128
    end
end

local function tabHtml(renderer, ctx)
    -- Lazy-load the document once per session
    if ctx.htmlDocId == nil then
        ctx.htmlDocId = Resource.Resolve('docs/sample.html') or false
    end
    if ctx.htmlDocId and ctx.htmlDoc == nil then
        ctx.htmlDoc = Html.Parse(ctx.htmlDocId)
        if ctx.htmlDoc then
            ctx.htmlLastEvent = 'none'
            ctx.htmlLastHandle = 0
            ctx.htmlDoc:SetEventHandler(function(eventtype, doc, handle)
                ctx.htmlLastEvent  = eventtype
                ctx.htmlLastHandle = handle
                local el = doc:QueryByHandle(handle)
                if el and eventtype == 'click' then
                    if el.attrs and el.attrs['data-action'] == 'hello' then
                        ctx.htmlMsg = 'Hello from Lua!'
                    elseif el.attrs and el.attrs['data-action'] == 'world' then
                        ctx.htmlMsg = 'World!'
                    elseif el.tag == 'a' and el.href then
                        ctx.htmlMsg = 'Link: ' .. el.href
                    else
                        ctx.htmlMsg = string.format('click id=%s tag=%s', tostring(el.id), tostring(el.tag))
                    end
                end
            end)
        end
    end

    if not ctx.htmlDoc then
        renderer:TextColored(1, 0.4, 0.4, 1, 'HTML document not loaded (docs/sample.html)')
        renderer:Text('Make sure Resource.SetLoader can open docs/ files.')
        return
    end

    -- Status bar
    renderer:Text(string.format('Last event: %-10s  handle: %d',
        ctx.htmlLastEvent or 'none', ctx.htmlLastHandle or 0))
    if ctx.htmlMsg then
        renderer:SameLine()
        renderer:Text('  =>  ' .. ctx.htmlMsg)
    end

    if renderer:Button('Reload HTML') then
        ctx.htmlDocId = nil
        ctx.htmlDoc   = nil
        ctx.htmlMsg   = nil
        return
    end
    renderer:SameLine()
    if renderer:Button('Invalidate') then
        ctx.htmlDoc:Invalidate()
    end

    renderer:Separator()

    -- Render into a child region so it scrolls independently
    local cw, ch = renderer:GetContentRegionAvail()
    if renderer:BeginChild('##html_view', cw, ch - 4, false) then
        renderer:Html(ctx.htmlDoc)
    end
    renderer:EndChild()
end

local function tabMarkdown(renderer, ctx)
    local refresh = renderer:Button('Reload')
    renderer:SameLine()
    renderer:Text('docs/markdown-test.md')
    renderer:Separator()
    renderer:MarkdownRender(ctx.mdDoc, refresh)
end

local function tabTextures(renderer, ctx)
    if not ctx.texOriginal then
        ctx.texOriginal  = OpenGL.LoadTexture(Stream.Open('./docs/sample.png', 'rb'), 'sample.png')
        ctx.texThumbnail = OpenGL.ResizeTexture(ctx.texOriginal, 100, 100, 'sample.png:thumb')
        ctx.texSheet     = OpenGL.LoadTexture(Stream.Open('./docs/samplespritesheet.png', 'rb'), 'samplespritesheet.png')
        ctx.texGif       = OpenGL.LoadTexture(Stream.Open('./docs/sample.gif', 'rb'), 'sample.gif')
        ctx.sheetFrame   = 1
        ctx.sheetTimer   = 0.0
    end

    local function showTexture(label, id)
        if not id then
            renderer:Text(label .. ': not loaded')
            return
        end
        local data = OpenGL.GetData(id)
        renderer:Text(label)
        renderer:Text('  id         : ' .. tostring(id))
        renderer:Text('  width      : ' .. tostring(data and data.width      or '?'))
        renderer:Text('  height     : ' .. tostring(data and data.height     or '?'))
        renderer:Text('  format     : ' .. tostring(data and data.format     or 'none'))
        renderer:Text('  frameCount : ' .. tostring(data and data.frameCount or '?'))
        renderer:Text('  source     : ' .. tostring(data and data.source     or 'none'))
        if data then
            renderer:Image(id, data.width, data.height)
        end
        renderer:Separator()
    end

    showTexture('Original (sample.png)', ctx.texOriginal)
    showTexture('Thumbnail (100x100)',    ctx.texThumbnail)
    showTexture('Animated GIF (sample.gif)', ctx.texGif)

    -- Sprite sheet: 512x512, 8 cols x 8 rows = 64 frames, each cell 64x64
    if ctx.texSheet then
        local cols    = 8
        local rows    = 8
        local fps     = 12
        local total   = 60  -- sheet is 8x8 but last 4 frames are empty

        ctx.sheetTimer = ctx.sheetTimer + (1.0 / 60.0)
        if ctx.sheetTimer >= 1.0 / fps then
            ctx.sheetTimer = ctx.sheetTimer - (1.0 / fps)
            ctx.sheetFrame = (ctx.sheetFrame % total) + 1
        end

        renderer:Text('Sprite sheet (samplespritesheet.png) — frame ' .. ctx.sheetFrame .. ' / ' .. total)
        renderer:Text('  id : ' .. tostring(ctx.texSheet))
        renderer:ImageFrame(ctx.texSheet, 64, 64, ctx.sheetFrame, cols, rows)
        renderer:SameLine()
        renderer:ImageFrame(ctx.texSheet, 64, 64, ctx.sheetFrame, cols, rows, true, false)
        renderer:Text('  normal  mirrored (flipX)')
        renderer:Separator()

        renderer:Text('Full sheet preview (512x512):')
        renderer:Image(ctx.texSheet, 512, 512)
    end
end

local function render(renderer, ctx)
    ctx.frameCount = ctx.frameCount + 1

    if not ctx.scheduled then
        ctx.scheduled = true
        Imgui.Schedule(function()
            ctx.scheduleResult = "fired on frame 1"
        end)
    end

    local winW = SDL.Window.GetWindowWidth()
    local winH = SDL.Window.GetWindowHeight()
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
        if renderer:BeginTabItem("Hardware") then
            tabHardware(renderer, ctx)
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
        if renderer:BeginTabItem('Html') then
            tabHtml(renderer, ctx)
            renderer:EndTabItem()
        end
        if renderer:BeginTabItem('Textures') then
            tabTextures(renderer, ctx)
            renderer:EndTabItem()
        end
        if renderer:BeginTabItem('Audio') then
            tabAudio(renderer, ctx)
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
Resource.SetLoader(onResourceLoad)