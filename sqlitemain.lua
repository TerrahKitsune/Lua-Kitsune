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

local testTable = {};
RegisterFunction("CRC64", function(data) if not data then return nil; end return string.format('%016x', CRC64(data)); end);
print(query("select CRC64('abc');"));
print(scalar("select CRC64('abc');"));
RegisterTable("TestTable", {"Id", "Value"}, testTable);
RegisterAggregate("TestAgg", function(isfinished, context) 

    context.cnt = context.cnt or 0;   
    if isfinished then return context.cnt; end
    context.cnt = context.cnt + 1;
end);

RegisterVirtualTable("Testx", {"Id", "Value"}, reader, update);
for n=1, 10 do
    query([[INSERT INTO TestTable ("Id", "Value")VALUES(@id, @uuid);]], {id=n,uuid=UUID()});
end

query([[Update TestTable set "Value"=@uuid WHERE "Id" IN (@id, 15, 2);]], {id=10,uuid="bla"});
query([[Update TestTable set "Id"=@uuid WHERE "Id"=@id;]], {id=10,uuid=15});
query([[Delete from TestTable WHERE "Id"=@id;]], {id=1});
print(j:Encode(query("select TestAgg(Id) as cnt from TestTable;")));


return Wchar.FromAnsi("test");