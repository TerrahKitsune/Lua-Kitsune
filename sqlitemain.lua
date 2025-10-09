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

function DumpArgs()
    return ARGS;
end

RegisterVirtualTable("Testx", {"Id", "Value"}, reader, update);
for n=1, 100 do
    query("insert into Testx values (@id, @data);", {id=n, data=UUID()});
end
print(Json.Create(true):Encode(query("select * from Testx;")));
print(Json.Create(true):Encode(query("select * from Testx where Id=50;")));

return Wchar.FromAnsi("test");