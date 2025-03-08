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

oprint=print;
local function CreateGCPrint()
	SetGCFunction({last=Time()}, function(obj) local t=Time();oprint("COLLECTING GARBAGE Lua mem: "..math.floor(collectgarbage("count")) .. " KB Time: "..(t-obj.last)); CreateGCPrint(); end);
end
CreateGCPrint();
collectgarbage();

local function HexToString(hexString)
    local str = ""
    for i = 1, #hexString, 2 do
        local byte = tonumber(hexString:sub(i, i+1), 16)
        str = str .. string.char(byte)
    end
    return str
end

FileSystem.SetCurrentDirectory("C:\\Users\\Terrah\\Desktop");

local db = SQLite.Open(":memory:");

db:Query([[CREATE TABLE "test" (
	"Id"	INTEGER NOT NULL,
	"Data"	TEXT NOT NULL,
	PRIMARY KEY("Id" AUTOINCREMENT)
);]]);

assert(db:Query([[select Lua("local uid = UUID(); return uid;");]]));
assert(db:Fetch(), "Emtpy result");
print(db:GetRow(1));

local test = {};

local function AggregateFunction(isFinish, id, data)

	if isFinish then
		local r = Json.Create():Encode(test);
		test = {};
		return r;
	else
		table.insert(test, {Id = id, Data=data});
	end
end

db:RegisterAggregateFunction(AggregateFunction, "LuaAgg", 2);
db:RegisterFunction(UUID, "UUID", 1);

for n=1, 10 do
	assert(db:Query([[insert into "test" ("Data")VALUES(UUID('a'))]]));
end

assert(db:Query([[select * from "test";]]));
while db:Fetch() do
	print(db:GetRow(1), db:GetRow(2));
end

assert(db:Query([[select "Id" % 2 as "T", JSON(LuaAgg("Id", "Data")) from "test" group by "Id" % 2;]]));

while db:Fetch() do
	print(db:GetRow(1), db:GetRow(2));
end