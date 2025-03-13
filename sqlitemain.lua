local testTable = {};
table.insert(testTable, {"abc", math.random()});
table.insert(testTable, {"cba", math.random()});
table.insert(testTable, {"xyz", math.random()});

local j=Json.Create();

local testdata = {};
local function reader(context) 

    if context.count then
        context.count = context.count + 1;
    else
        context.count = 1;
    end

    return testdata[context.count];
end

local function update(pk, data)
    
    if not pk then
        data[2] = "INSERT";
        pk = data[1];
        for n=1, #testdata do
            if testdata[n][1] == pk then
                error("Duplicate key "..pk);
            end
        end
        table.insert(testdata, data);
    elseif not data then
        --DELETE
        local idx;
        for n=1, #testdata do
            if testdata[n][1] == pk then
                idx=n;
            end
        end
        if idx then table.remove(testdata, idx); end
    elseif pk ~= data[1] then
        data[2] = "PKUPDATE";
        local idx;
        for n=1, #testdata do
            if testdata[n][1] == pk then
                idx=n;
            elseif testdata[n][1] == data[1] then
                error("Duplicate key "..data[1]);
            end
        end
        if idx then
            table.remove(testdata, idx);
        end
        table.insert(testdata, data);
    else
        data[2] = "UPDATE";
        pk = data[1];
        for n=1, #testdata do
            if testdata[n][1] == pk then
                testdata[n] = data;
                break;
            end
        end
    end
end

local kluffu1, kluffu2 = dofile("kluffu.lua");
RegisterTable("Kluffu1", {"Id", "Value"}, kluffu1);
RegisterTable("Kluffu2", {"Id", "Value"}, kluffu2);
print(j:Encode(query("select * from Kluffu1;")));

RegisterTable("TestTable", {"Id", "Value", "Data"}, testTable);
RegisterVirtualTable("Testx", {"Id", "Value", "Data"}, reader, update);
for n=1, 10 do
    query([[INSERT INTO Testx ("Id", "Value", "Data")VALUES(@id, "abc", @uuid);]], {id=n,uuid=UUID()});
end

print(j:Encode(query("select * from Testx;")));

for k,v in pairs(testTable) do testTable[k]=nil; end

return Wchar.FromAnsi("test");