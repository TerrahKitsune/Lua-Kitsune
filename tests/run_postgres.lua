local _put = Put
print = function(...)
    local parts = {}
    for i = 1, select("#", ...) do
        parts[i] = tostring(select(i, ...))
    end
    _put(table.concat(parts, "\t") .. "\n")
end

require("tests.postgres")