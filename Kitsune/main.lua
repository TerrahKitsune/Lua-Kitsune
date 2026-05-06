if not REAL then 
	REAL=true;
else
	print("Recursive");
	return;
end

GetKey = Session.Console.GetKey;
SetTitle = Session.Console.SetTitle;

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

function PrintPixel(px)
	io.write("{"..px.r.." | ");
	io.write(px.g .. " | ");
	print(px.b .. "} ");
end

function DumpToFile(file, tbl)
	local json = Json.Create()
	local f = io.open(file, "w")
	f:write(json:EncodePretty(tbl))
	f:flush()
	f:close()
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

oprint=print;
local function CreateGCPrint()
	SetGCFunction({last=Time()}, function(obj) local t=Time();oprint("COLLECTING GARBAGE Lua mem: "..math.floor(collectgarbage("count")) .. " KB Time: "..(t-obj.last)); CreateGCPrint(); end);
end
CreateGCPrint();
collectgarbage();
GetKey = Session.Console.GetKey;

local ctx = Llama.CreateContext();
assert(ctx:SetModel("C:/Users/Terrah/Documents/GitHub/Lua-Kitsune/tests/models/qwen3-0.6b-q8_0.gguf"));
print(ctx:IsReady());
assert(ctx:Generate({
    { role = 'system', content = 'You are a helpful assistant.' },
    { role = 'user',   content = 'Hello!' },
}));

local ok, data = ctx:Poll()
while ok do
    if data then
        if data.type == 'error' then error(data.text) end
        if data.type == 'token'     then io.write(data.text) end
        if data.type == 'reasoning' then io.write(data.text) end
    end
    Sleep(10)
    ok, data = ctx:Poll()
end

GetKey();

data = Llama.GetLogs();

for n=1, #data do
	print(data[n]);
end
data=nil

GetKey();

print("");
print(Json.New(true):Encode(ctx:Info()));

GetKey();

if true then return; end

SetTitle("Kitsune: ".._VERSION);
if Imgui then
	print("Imgui is detected");
end
--GetKey();
if Imgui then
	Session.Console.Clear();
	dofile("imgui_test.lua");
end
