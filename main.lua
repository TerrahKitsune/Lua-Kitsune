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

print(FileSystem.GetSpecialFolder(0x0010));

TablePrint(Redis);

local redis = Redis.Open("10.9.23.123");

TablePrint(redis:Command("AUTH hej123"));
TablePrint(redis:Command("LOLWUT"));

TablePrint(redis:Command("GET teststring"));
TablePrint(redis:Command("GET testbool"));
TablePrint(redis:Command("GET testdouble"));
TablePrint(redis:Command("GET testnumber"));
TablePrint(redis:Command("GET testnil"));

local len = redis:Command("LLEN testlist");
TablePrint(len);

local list = redis:Command("LRANGE testlist 0 "..len.Value);
TablePrint(list);

local keys={};
local cursor = 0;
local data;
repeat
	data = redis:Command("scan "..cursor);

	if not data then 
		break;
	end

	cursor = tonumber(data.Value[1].Value);

	for n=1, #data.Value[2].Value do
		table.insert(keys, data.Value[2].Value[n].Value);
	end

until cursor == nil or cursor == 0;

for n=1, #keys do 
	print(n, keys[n]);
end

local tim = Timer.New();
tim:Start();
for n=1, 1000000 do 
	data = UUID();
	data = redis:Command("SET test:"..data.." "..n).Value;
	if data ~= "OK" then 
		error(data);
	end
end
tim:Stop();
print(tim:Elapsed());