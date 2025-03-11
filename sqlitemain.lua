local testTable = {};

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

RegisterTable("TestTable", {"Id", "Value", "Data"}, testTable);

return Wchar.FromAnsi("test");