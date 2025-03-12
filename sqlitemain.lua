local testTable = {};
table.insert(testTable, {"abc", math.random()});
table.insert(testTable, {"cba", math.random()});
table.insert(testTable, {"xyz", math.random()});

Testy = {Test={1}};

Testy.Test.P = print;

function TestFunction()

	return coroutine.create(function ()
                 
        print("start");

        for n=1, 10 do
            local id=UUID();
            coroutine.yield(id, math.random());
        end

        print("stop");
    end);
end

local j=Json.Create();
print(j:Encode(query("select 'abc' as B;")));

RegisterTable("TestTable", {"Id", "Value", "Data"}, testTable);

print(j:Encode(query("select * from TestTable where Id >= @id;", {id=2})));

for k,v in pairs(testTable) do testTable[k]=nil; end

return Wchar.FromAnsi("test");