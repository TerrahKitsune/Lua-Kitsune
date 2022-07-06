local _exit=Exit;Exit=function(ret) GetKey(); return ret; end

function TablePrint(tbl, depth)

	if(not tbl and depth) then
		assert(tbl, depth);
	end

	depth = depth or 0;

	local padding="";

	for n=1, depth do 
		padding = padding.." ";
	end

	print(padding..tostring(tbl));

	if type(tbl)~="table" then 	
		return;
	end

	for k,v in pairs(tbl) do 
		print(padding..tostring(k)..": "..tostring(v));

		if type(v)=="table" then 
			TablePrint(v, depth+1);
		end
	end

end

function ArrayPrint(arr)

	print(tostring(arr).." "..tostring(#arr));

	if type(arr)~="table" then 
		return;
	end

	for n=1,#arr do 
		print(arr[n]);
	end 
end

for n=1, #ARGS do 
	print(n, ARGS[n]);
end

function PrintPixel(px)
	io.write("{"..px.r.." | ");
	io.write(px.g .. " | ");
	print(px.b .. "} ");
end

function DumpToFile(file, tbl)
	local f = io.open(file, "w");
	f:write(JSON:encode_pretty(tbl));
	f:flush();
	f:close();
end

local statusTimer = Timer.New();
statusTimer:Start();
function WriteStatusString(str, prevlen, sincelast)

	if statusTimer:Elapsed() <= sincelast then 
		return prevlen;
	else 
		statusTimer:Stop();
		statusTimer:Reset();
		statusTimer:Start();
	end

	prevlen = prevlen or 0;

	if prevlen > 0 then 

		for n=1, prevlen do 
			io.write("\b");
		end 

		for n=1, prevlen do 
			io.write(" ");
		end 

		for n=1, prevlen do 
			io.write("\b");
		end 
	end

	io.write(str);

	return str:len();
end

function string.fromhex(str)
    return (str:gsub('..', function (cc)
        return string.char(tonumber(cc, 16))
    end))
end

function string.tohex(str)
    return (str:gsub('.', function (c)
        return string.format('%02X', string.byte(c))
    end))
end

math.randomseed(Time());
math.random(); math.random(); math.random();

print("Percent                    ", GlobalMemoryStatus());
print("total KB of physical memory", GlobalMemoryStatus(1));
print("free  KB of physical memory", GlobalMemoryStatus(2));
print("total KB of paging file    ", GlobalMemoryStatus(3));
print("free  KB of paging file    ", GlobalMemoryStatus(4));
print("total KB of virtual memory ", GlobalMemoryStatus(5));
print("free  KB of virtual memory ", GlobalMemoryStatus(6));

local function SetGCFunction(tbl, func)
	return setmetatable(tbl, {__gc = func})
end

local gigabuttonoffset = 275;

local function CreateGCPrint()
	SetGCFunction({last=Time()}, function(obj) local t=Time();print("COLLECTING GARBAGE Lua mem: "..math.floor(collectgarbage("count")) .. " KB Time: "..(t-obj.last)); CreateGCPrint(); end);
end
CreateGCPrint();
collectgarbage();

TablePrint(Imgui);

local test = {};
local testSelected = 0;

local elements = {"Wind", "Fire", "Water", "Earth"}

for n=1, 10 do 
	table.insert(test, tostring(math.random()));
end 

local cnt = 0;
local maxfpses = 100;
local fpses = {};
local fpstimer = Timer.New();
fpstimer:Start();

local function AddFps(ui, fps)

	if fps <= 0 then 
		return;
	end 

	local fpsavg = fps;
	local offset = 0; 

	if #fpses < maxfpses then 
		offset = offset + 1;
	end

	local temp;
	for n=#fpses + offset, 2, -1 do 
		fpses[n] = fpses[n - 1];
		fpsavg = fpsavg + fpses[n];
	end

	fpses[1] = fps;

	if #fpses > 0 then
		fpsavg = fpsavg / #fpses
	end

	ui:SetValue("fpsgraphoverlay", 5, tostring(math.floor(fpsavg)).." avg");
end

local window = Imgui.Create("Test", "bg", 1280, 800, function(ui)

	local framerate;

	local info = ui:Info();
	local vec0 = {x=0,y=0};
	ui:PushStyleVar(3, 0.0);
	ui:PushStyleVar(5, vec0);
	ui:SetNextWindowSize({x=info.DisplaySizeX,y=25});
	ui:SetNextWindowPos(vec0);

	if ui:Begin("##mainmenubar", nil, 1295) and ui:BeginMenuBar() then 
		
		ui:PopStyleVar(2);

		if fpstimer:Elapsed() >= 100 then
			
		framerate = framerate or ui:Info().Framerate;

			AddFps(ui, framerate);

			fpstimer:Stop();
			fpstimer:Reset();
			fpstimer:Start();
		end

		if ui:BeginMenu("File") then 

			ui:Separator();
			if ui:MenuItem("Exit") then 
				Exit=_exit;
				ui:Quit();
			end

			ui:EndMenu();
		end

		framerate = ui:Info().Framerate;

		local fps = math.floor(framerate).." fps";
		local size = ui:GetWindowSize();
		local textSize = ui:CalcTextSize(fps);
		local cursor = ui:GetCursorPos();

		ui:SameLine(size.x - cursor.x - textSize.x);

		ui:Text(fps);
		if ui:IsItemHovered() then
			ui:BeginTooltip()  			
			ui:PlotLines("##fpsgraph", fpses, "fpsgraphoverlay");		
			ui:EndTooltip();
		end

		ui:EndMenuBar();
	else 
		ui:PopStyleVar(2);
	end

	ui:End();
	

	if ui:Begin("Demo", nil, 64) then
		
		if ui:CollapsingHeader("Stuff") then

			local r,g,b = ui.Vec4ToRGB(ui:GetValue("bg", 3));

			ui:TextColored(ui.RGBToVec4(r,g,b), "Test");
			ui:SameLine();

			ui:Text(tostring(r).."|"..tostring(g).."|"..tostring(b));
			ui:SameLine();
			if ui:Button("Open") then 
				ui:SetValue("property", 1, true);
			end

			if ui:Checkbox("Show Demo", "demo") then 
				print("Click show demo checkbox");
				ui:SetValue("tabletabopen", 1, true);
			end 

			if ui:SliderFloat("Float", "float", 0, 1, "Nice = %.3f") then 
				print("Slider changed");
			end 

			if ui:ColorEdit3("Background Color", "bg") then 
				print("Changed color");
			end

			ui:Separator();

			if ui:Button("Counter") then 
				cnt = cnt + 1;
				ui:SetValue("float", 2, 0.5);
			end

			ui:SameLine();
			ui:PushButtonRepeat(true);	
			if ui:ArrowButton("left", 0) then 
				cnt = cnt - 1;
			end		
			ui:SameLine();
			if ui:ArrowButton("right", 1) then 
				cnt = cnt + 1;
			end
			ui:PopButtonRepeat();

			ui:SameLine();
			ui:Text("Count: "..tostring(cnt * ui:GetValue("float", 2)));

			if ui:IsItemHovered() then 
				ui:BeginTooltip();
				ui:Text("Real: "..tostring(cnt));
				ui:EndTooltip();
			end
		end 

		if ui:CollapsingHeader("FPS") then
			
			framerate = framerate or ui:Info().Framerate;

			ui:Text(string.format("Application average %.3f ms/frame (%.1f FPS)", 1000.0 / framerate, framerate));
		end

		if ui:CollapsingHeader("Input") then

			if ui:RadioButton("A", "radiobutton", string.byte("A")) then 
				print("Click radio button a");
			end 
			ui:SameLine();
			ui:RadioButton("B", "radiobutton", string.byte("B"));
			ui:SameLine();
			ui:RadioButton("C", "radiobutton", string.byte("C"));
			ui:SameLine();
			ui:Text(string.char(ui:GetValue("radiobutton", 4)));

			for n=1, 3 do 

				if n > 1 then 
					ui:SameLine();
				end

				ui:PushId(n);
				ui:PushStyleColor(21, ui.RGBToVec4(n*50, n*50,n*50));
				ui:PushStyleColor(23, ui.RGBToVec4(math.random(0,255), math.random(0,255), math.random(0,255)));
				ui:Button("Button "..tostring(n));
				ui:PopStyleColor();
				ui:PopStyleColor();
				ui:PopId(n);
			end

			ui:Separator();
			ui:LabelText("label", "Value");
			local letters = {"a","b","c","d","e"};
			if ui:Combo("Combo", "comboselected", letters, 2) then 
				print("Combo changed");
			end

			ui:SameLine();
			ui:HelpMarker("Selected "..tostring(letters[ui:GetValue("comboselected", 4) + 1]));

			if ui:InputText("Text!", "textinput", "Hint text") then 
				print("Text was changed");
			end 
			ui:SameLine();
			ui:HelpMarker("Text: "..ui:GetValue("textinput", 5));

			if ui:InputInt("Ints", "intinput") then 
				print("Int was changed");
			end
			ui:SameLine();
			ui:HelpMarker("Int: "..tostring(ui:GetValue("intinput", 4)));

			if ui:InputFloat("Floats", "floatinput", 1.0) then 
				print("Float was changed");
			end
			ui:SameLine();
			ui:HelpMarker("Float: "..tostring(ui:GetValue("floatinput", 2)));

			if ui:InputDouble("Doubles", "doubleinput", 1.0) then 
				print("Double was changed");
			end
			ui:SameLine();
			ui:HelpMarker("Double: "..tostring(ui:GetValue("doubleinput", 6)));

			if ui:SliderInt("Int slider", "intslider", 0, 100) then 
				print("Int slider was changed");
			end
			ui:SameLine();
			ui:HelpMarker("Int: "..tostring(ui:GetValue("intslider", 4)));


			if ui:SliderInt("Enum", "enumslider", 0, #elements - 1, elements[ui:GetValue("enumslider", 4) + 1]) then 
				print("enum slider was changed");
			end
			ui:SameLine();
			ui:HelpMarker("Element: "..elements[ui:GetValue("enumslider", 4) + 1]);

			ui:Separator();
			ui:LabelText("label", "Value");
			local testlistbox = {"asd","dsa","abc","haha"};
			if ui:ListBox("Listbox", "listboxselected", testlistbox) then 
				print("Listbox changed");
			end
			ui:SameLine();
			ui:HelpMarker("Listbox: "..testlistbox[ui:GetValue("listboxselected", 4) + 1]);
		
			local size = ui:GetWindowSize();

			size.y = ui:GetTextLineHeight() * 10;
			size.x = 0;

			ui:Separator();
			if ui:InputTextMultiline("Big box", "bigtext", size) then 
				print("Big box changed");
			end
			ui:SameLine();
			ui:HelpMarker("Bigtext: "..ui:GetValue("bigtext", 5));

			ui:ProgressBar(ui:GetValue("float", 2), nil, "textinput");
		end

		ui:SetNextItemOpen(ui:GetValue("tabletabopen", 1));
		
		local windowSize = ui:GetWindowSize();

		if ui:CollapsingHeader("Table", "demo") and ui:BeginTable("tablestuff",  3, 1920 | 64) then 

			if ui:TableNextColumn() then 
				ui:Checkbox("Test 1", "demo");
			end

			if ui:TableNextColumn() then 
				ui:Checkbox("Test 2", "demo");
			end

			if ui:TableNextColumn() then 
				ui:Checkbox("Test 3", "demo");
			end

			if ui:TableNextColumn() then 
				ui:Checkbox("Test 4", "demo");
			end

			if ui:TableNextColumn() then 
				ui:Checkbox("Test 5", "demo");
			end

			if ui:TableNextColumn() then 
				ui:Button("Test 6");
			end

			ui:EndTable();
			ui:SetValue("tabletabopen", 1, true);
		else 
			ui:SetValue("tabletabopen", 1, false);
		end
	end

	ui:End();

	if ui:GetValue("demo", 1) then
		ui:ShowDemoWindow("demo");
	end

	if ui:GetValue("property", 1) then

		ui:SetNextWindowSize(500,440,4);

		if ui:Begin("Example: Simple layout", "property", 1024) then

			if ui:BeginMenuBar() then

				if ui:BeginMenu("File") then 
					
					if ui:MenuItem("Close") then
						ui:SetValue("property", 1, false);
					end

					ui:EndMenu();
				end

				ui:EndMenuBar();
			end

			if ui:BeginChild("left pane", 150, 0, false) then 

				for n=1, #test do 		
					if ui:Selectable(test[n], testSelected == n) then 
						testSelected = n;
					end
				end
			end

			ui:EndChild();
			ui:SameLine();

			ui:BeginGroup();

			local frameHeight = 0 - ui:GetFrameHeightWithSpacing();

			if ui:BeginChild("item view", 0, frameHeight, false) then 

				ui:Text(string.format("MyObject: %s", tostring(test[testSelected] or "None" )));
				ui:Separator();

				if ui:BeginTabBar("##Tabs") then 

					if ui:BeginTabItem("Description") then
						ui:TextWrapped("Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod tempor incididunt ut labore et dolore magna aliqua.");
						ui:EndTabItem();
					end

					if ui:BeginTabItem("Details") then
						ui:TextWrapped("ID: 0123456789");
						ui:EndTabItem();
					end

					ui:EndTabBar();
				end

			end
			ui:EndChild();

			ui:Button("Revert");
			ui:SameLine();
			ui:Button("Save");

			ui:EndGroup();

		end

		ui:End();
	end
end);

window.GetEnums();

window:SetValue("bg", 3, {x=0.45,y=0.55,z=0.6, w=1.0});
window:SetValue("property", 1, false);
window:SetValue("demo", 1, true);
window:SetValue("float", 2, 0.5);
window:SetValue("radiobutton", 4, string.byte("B"));
window:SetValue("comboselected", 4, 2);
window:SetValue("enumslider", 4, 0);
window:SetValue("tabletabopen", 1, true);

local values = window:GetAllValues();

for n=1, #values do 
	print(values[n].Name, values[n].Type);
end 

print("Enums:-----------");

for def,vals in pairs(window.GetEnums()) do

	print(def);

	for k,v in pairs(vals) do 
		print("   "..k.."="..v);
	end
end

while window:Tick() do end