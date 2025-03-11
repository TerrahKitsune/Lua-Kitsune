
function TestFunction()

	local test = {};

	for n=1, 10 do
		local id=UUID;
		table.insert(test, id);
	end

	if true then return test end

	local iterator = 0;

	return function(row) 
	
		iterator = iterator + 1;
		local val = test[iterator];

		if not val then return nil; end

		--if iterator == 2 then error("error"); end

		return iterator, val;
	end;
end

return Wchar.FromAnsi("test");