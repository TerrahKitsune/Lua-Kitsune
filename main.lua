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

for n=1, 10 do 
	table.insert(test, tostring(math.random()));
end 

local cnt = 0;

local window = Imgui.Create("Test", "bg", 1280, 800, function(ui)

	if ui:Begin("Demo") then
		
		ui:Text("Test");
		ui:SameLine();
		if ui:Button("Open") then 
			ui:SetValue("property", 1, true);
		end

		ui:Checkbox("Show Demo", "demo");
		ui:SliderFloat("Float", "float", 0, 1);
		ui:ColorEdit3("Background Color", "bg");

		ui:Separator();

		if ui:Button("Counter") then 
			cnt = cnt + 1;
			ui:SetValue("float", 2, 0.5);
		end

		ui:SameLine();
		ui:Text("Count: "..tostring(cnt * ui:GetValue("float", 2)));

		local framerate = ui:Info().Framerate;

		ui:Text(string.format("Application average %.3f ms/frame (%.1f FPS)", 1000.0 / framerate, framerate));

		ui:Separator();
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

window:SetValue("bg", 3, {x=0.45,y=0.55,z=0.6, w=1.0});
window:SetValue("property", 1, true);
window:SetValue("demo", 1, true);
window:SetValue("float", 2, 0.5);

while window:Tick() do end