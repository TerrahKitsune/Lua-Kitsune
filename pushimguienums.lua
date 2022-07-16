
local imguifiles = {
	"C:/Users/Terrah/Documents/GitHub/Lua-Kitsune/Imgui/imgui.h"
};

local function Any(t)

	local cnt = 0;

	if type(t) ~= "table" then
		return cnt;
	end

	for k,v in pairs(t) do 
		cnt = cnt + 1;
	end 
	
	return cnt;
end

local function DoRow(lastnumber, defname, defs, row)

	if not row then 
		return lastnumber;
	end 
	
	while row:match("^([%s,\t])") do
		row = row:match("^[%s,\t]*(.+)");
	end 
	
	if row:match("^//") then 
		return lastnumber;
	end 

	if row:match("//") then 
		row = row:match("(.-)//");
	end

	if not row:match(",") then 
		row = row .. ",";
	end 

	local key, value, f,op,b;

	for seg in row:gmatch("(.-),") do 
	
		while seg:match("^([%s,\t])") do
			seg = seg:match("^[%s,\t]*(.+)");
		end 
		
		while seg:match("([%s,\t])$") do
			seg = seg:match("(.+)[%s,\t]$");
		end 
		
		if seg:sub(1, defname:len()) == defname then 
			
			if seg:match("=") then
				key,value = seg:match("(.-)=(.+)");
				
				key = key:gsub("%s", "");
				value = value:gsub("%s", "");
								
				if value:match("^(.-)(<<)(.-)$") then
				
					f,op,b = value:match("^(.-)(<<)(.-)$");
					f = tonumber(f);
					b = tonumber(b);

					if op == "<<" then 
						value = f << b;
						lastnumber = value + 1;
					else
						print(value);
						print(f,op,b);
						error("Bad op");
					end
				elseif value:match("^[-,0,1,2,3,4,5,6,7,8,9]") then 
					value = tonumber(value);
					lastnumber = value + 1;
				end
			else 
				key = seg;		
				value = lastnumber;
				lastnumber = lastnumber + 1;
			end	

			key = key:sub(defname:len()+1)

			defs[key] = value;

			--print(key.."="..value);
		end
	end 

	return lastnumber;
end

local function DoFile(enums, file)

	local f = assert(io.open(file, "r"));
	local text = f:read("*all");
	local defs;
	local temps;
	f:close();
	for defname, def in text:gmatch("\nenum%s-(.-)%s-{(.-)}") do 
	
		if not defname or defname:len() <= 0 or not def or def:len() <= 0 then 
			error("Missing name");
		end 
	
		defname = defname:gsub("%s", "");
	
		defs = {};
		
		temps = 0;
		
		for row in def:gmatch("(.-)\n") do 
		
			temps = DoRow(temps, defname, defs,row);
		end
		
		print(defname, Any(defs));
		
		if Any(defs) > 0 then
			enums[defname] = defs;
		end
	end
end

local enums = {};

for n=1, #imguifiles do 
	DoFile(enums, imguifiles[n]);
end 

local function Lookup(tag)

	local name, key = tag:match("(.-)_(.+)");
	local value = 0;
	
	name = name .. "_";
	
	if key:match("^(.-[|,-].+)") then 
		local first, op, second = key:match("^(.-)([|,-])(.+)");
		
		if first:sub(1,name:len()) ~= name then 
			first = name..first;
		end
		
		if second:sub(1,name:len()) ~= name then 
			second = name..second;
		end
		
		print(first, second);
		
		if op == "|" then 
			value = Lookup(first) | Lookup(second);
		elseif op == "-" then  	
			value = Lookup(first) - Lookup(second);
		else
			print(key);
			error("Unknown op "..tostring(op));
		end
	else 
	
		value = enums[name][key];
		
		if type(value) == "string" then 
			if key:sub(1,name:len()) ~= name then 
				key = name.."_"..key;
			end
			
			value = Lookup(value);
		end
	end
	
	print(tag.."="..value);
	
	return value;
end

for defname, defs in pairs(enums) do 
	
	local n={};
	
	for k,v in pairs(defs) do 
	
		if type(v) == "string" then
			n[k] = assert(Lookup(v), v);
		end
	end
	
	for k,v in pairs(n) do 
		defs[k] = v;
		print("Set", k, v);
	end	
end

local maincount = 0;
local counts = {};
local f=io.open("test.txt", "w");
for defname, defs in pairs(enums) do 
	
	local cnt =0;
	print(defname);
	f:write(defname.."\n");
	for k,v in pairs(defs) do
	
		if type(v) == "number" then
			print("   "..k.."="..v);
			f:write("   "..k.."="..v.."\n");
		else 
			error(k.." invalid type "..type(v).." "..tostring(v));
		end
		
		cnt = cnt + 1;
	end
	
	counts[defname] = cnt;
	
	print("-----");
	f:write("-----\n");
	f:flush();
	maincount = maincount + 1;
end

print(maincount);

f:write("Code:\n\n");
f:write("int existingEnumTable = LUA_REFNIL;\n\n");
f:write("int lua_pushimguienums(lua_State* L) {\n\n");
f:write("\tif (existingEnumTable != LUA_REFNIL) {\n\n");
f:write("\t\tlua_rawgeti(L, LUA_REGISTRYINDEX, existingEnumTable);\n");
f:write("\t\treturn 1;\n");
f:write("\t}\n\n");
f:write("\tlua_createtable(L, 0, "..maincount..");\n\n");
f:write("\texistingEnumTable = luaL_ref(L, LUA_REGISTRYINDEX);\n");
f:write("\tlua_rawgeti(L, LUA_REGISTRYINDEX, existingEnumTable);\n\n");

for defname, defs in pairs(enums) do 

	local scriptName = defname:sub(1,defname:len()-1);

	f:write('\tlua_pushstring(L, "'..scriptName..'");\n');
	f:write("\tlua_createtable(L, 0, "..counts[defname]..");\n");
	f:write("\t//START ---"..defname.."\n\t{\n");
	
	for k,v in pairs(defs) do 
		f:write('\t\tlua_pushstring(L, "'..k..'");\n');
		f:write('\t\tlua_pushinteger(L, '..v..');\n');
		f:write("\t\tlua_settable(L, -3);\n");
	end
	
	f:write("\t}\n");
	f:write("\n\tlua_settable(L, -3);\n\n");
	
end

f:write("\treturn 1;\n}");
f:close();
