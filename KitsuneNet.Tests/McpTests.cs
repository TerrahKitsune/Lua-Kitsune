using KitsuneNet;
using Shouldly;
using System.Diagnostics;
using System.Text.Json.Nodes;
using Xunit;

namespace KitsuneNet.Tests;

/// <summary>
/// Tests for the MCP server's elicitation support. The server speaks JSON-RPC over
/// the process's own stdin/stdout, so the protocol tests run the script in a
/// Kitsune.exe child process and play the client over its pipes.
/// </summary>
[Collection("KitsuneSequential")]
public sealed class McpTests
{
    // Prologue for the question setter tests, which need no client.
    private const string QuestionPrologue = @"
        local mcp = MCP.Create({ Name = 'test', Version = '1' }, {})
        local form = mcp:CreateElicitation('Fill in', function() end)
        local function err(f, ...)
            local ok, e = pcall(f, ...)
            return ok and 'ok' or tostring(e)
        end
    ";

    // Every tool returns a string; "ERR:" marks an Elicit that returned false.
    private const string ServerScript = @"
        local mcp = MCP.Create({ Name = 'test', Version = '1' }, {})
        local function default(context, request, reason) return 'default:' .. reason end

        local form = mcp:CreateElicitation('Fill in', default)
        form:AddQuestion('note', 'string', 'Note', false)
        form:AddQuestion('n', 'integer', 'N', true)

        local optionalOnly = mcp:CreateElicitation('Optional', default)
        optionalOnly:AddQuestion('note', 'string', 'Note', false)

        local _, loadTimeErr = pcall(form.Elicit, form, {}, {})

        local function ask(e, context, request)
            local ok, results = e:Elicit(context, request)
            if not ok then return 'ERR:' .. tostring(results) end
            return results
        end

        local schema = mcp:CreateElicitation('Schema', default)
        local email = schema:AddQuestion('email', 'string', 'Email', true)
        email:SetFormat('email')
        email:SetLength(3, 40)
        email:SetDescription('Where to write')
        email:SetDefault('a@b.co')
        local age = schema:AddQuestion('age', 'integer', 'Age', false)
        age:SetRange(18, 99)
        age:SetDefault(30)
        local ratio = schema:AddQuestion('ratio', 'number', 'Ratio', false)
        ratio:SetRange(0.5, nil)
        ratio:SetDefault(1.5)
        local color = schema:AddQuestion('color', 'string', 'Color', false)
        color:AddAnswer('red', 'Red')
        color:AddAnswer('green', 'Green')
        color:SetDefault('green')
        local tags = schema:AddQuestion('tags', 'string', 'Tags', false)
        tags:SetMultiple(1, 2)
        tags:AddAnswer('a', 'A')
        tags:AddAnswer('b', 'B')
        tags:AddAnswer('c', 'C')
        tags:SetDefault({ 'a' })
        local agree = schema:AddQuestion('agree', 'boolean', 'Agree', false)
        agree:SetDefault(true)

        local callbacks = mcp:CreateElicitation('Callbacks', default)
        local nameQ = callbacks:AddQuestion('name', 'string', 'Name', true)
        nameQ:SetCallback(function(context, request, value, answers)
            return value:upper() .. '/' .. tostring(answers.size)
        end)
        local sizeQ = callbacks:AddQuestion('size', 'string', 'Size', true)
        sizeQ:AddAnswer('s', 'Small', function(context, request, value) return 'small:' .. value end)
        sizeQ:AddAnswer('l', 'Large')
        local toppingsQ = callbacks:AddQuestion('toppings', 'string', 'Toppings', false)
        toppingsQ:SetMultiple()
        toppingsQ:AddAnswer('x', 'X', function(context, request, value) return 'own:' .. value end)
        toppingsQ:AddAnswer('y', 'Y')
        toppingsQ:AddAnswer('z', 'Z')
        toppingsQ:SetCallback(function(context, request, value) return 'q:' .. value end)
        local plainQ = callbacks:AddQuestion('plain', 'string', 'Plain', false)
        plainQ:AddAnswer('p', 'P')
        plainQ:AddAnswer('q', 'Q')

        local validated = mcp:CreateElicitation('Validated', default)
        validated:AddQuestion('age', 'integer', 'Age', false):SetRange(18, 99)
        validated:AddQuestion('code', 'string', 'Code', false):SetLength(2, 4)
        local picks = validated:AddQuestion('picks', 'string', 'Picks', false)
        picks:SetMultiple(1, 2)
        picks:AddAnswer('a', 'A')
        picks:AddAnswer('b', 'B')
        picks:AddAnswer('c', 'C')

        mcp:AddTool('schema', '', {}, function(context, request)
            return tostring(select(2, schema:Elicit(context, request)))
        end)

        mcp:AddTool('callbacks', '', {}, function(context, request)
            local r = ask(callbacks, context, request)
            if type(r) == 'string' then return r end
            return r.name .. '|' .. r.size.s .. '|' .. tostring(r.size.l) .. '|' .. r.toppings.x
                .. '|' .. tostring(r.toppings.y) .. '|' .. r.toppings.z .. '|' .. tostring(r.plain.q)
        end)

        mcp:AddTool('validated', '', {}, function(context, request)
            local r = ask(validated, context, request)
            if type(r) == 'string' then return r end
            return 'ok'
        end)

        mcp:AddTool('form', '', {}, function(context, request)
            local r = ask(form, context, request)
            if type(r) == 'string' then return r end
            return tostring(r.note) .. '|' .. tostring(r.n)
        end)

        mcp:AddTool('optional_only', '', {}, function(context, request)
            local r = ask(optionalOnly, context, request)
            if type(r) == 'string' then return r end
            return next(r) == nil and 'empty' or 'not empty'
        end)

        mcp:AddTool('can_elicit', '', {}, function(context, request)
            return tostring(request.CanElicit)
        end)

        mcp:AddTool('load_time', '', {}, function() return tostring(loadTimeErr) end)

        mcp:AddTool('in_task', '', {}, function(context, request)
            local err
            Tasks.New(function()
                local ok, e = pcall(form.Elicit, form, context, request)
                err = ok and 'no error' or tostring(e)
            end)
            for _ = 1, 500 do
                if err then return err end
                Sleep(10)
            end
            return 'task never finished'
        end)

        mcp:AddTool('no_yield', '', {}, function(context, request)
            local ok, err = pcall(table.sort, { 2, 1 }, function(a, b)
                form:Elicit(context, request)
                return a < b
            end)
            return ok and 'no error' or tostring(err)
        end)

        assert(mcp:Start())
        while mcp:IsRunning() do Sleep(5) end
    ";

    private const string ElicitingCapabilities = @"{""elicitation"":{}}";

    private static readonly string KitsuneExe = KitsuneExeFactAttribute.ExePath;

    // Where Elicit may be called
    [Fact]
    public async Task Elicit_AtLoadTime_Throws()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(@"
            local mcp = MCP.Create({ Name = 'test', Version = '1' }, {})
            local e = mcp:CreateElicitation('Fill in', function() return 'default' end)
            e:AddQuestion('n', 'integer', 'N', true)
            local ok, err = pcall(e.Elicit, e, {}, {})
            return tostring(ok) .. '|' .. tostring(err)
        ");
        r.String.ShouldBe("false|Elicit: must be called from inside a tool call");
    }

    [KitsuneExeFact]
    public async Task Elicit_AtLoadTime_ThrowsAndSendsNothing()
    {
        await using var client = await McpClient.StartAsync(ElicitingCapabilities);
        (await client.CallToolAsync("load_time")).ShouldBe("Elicit: must be called from inside a tool call");
    }

    [KitsuneExeFact]
    public async Task Elicit_FromTaskStartedByTool_ThrowsAndSendsNothing()
    {
        await using var client = await McpClient.StartAsync(ElicitingCapabilities);
        (await client.CallToolAsync("in_task")).ShouldContain("Elicit: must be called from inside a tool call");
    }

    [KitsuneExeFact]
    public async Task Elicit_FromNonYieldableCallback_ThrowsAndSendsNothing()
    {
        await using var client = await McpClient.StartAsync(ElicitingCapabilities);
        (await client.CallToolAsync("no_yield")).ShouldContain("Elicit: can't wait here");
    }

    // JSON null in the client's messages
    [KitsuneExeFact]
    public async Task Elicit_OptionalAnswerNull_IsAbsent()
    {
        await using var client = await McpClient.StartAsync(ElicitingCapabilities);
        string text = await client.CallToolAnsweringAsync("form",
            @"{""action"":""accept"",""content"":{""note"":null,""n"":5}}");
        text.ShouldBe("nil|5");
    }

    [KitsuneExeFact]
    public async Task Elicit_RequiredAnswerNull_IsMissing()
    {
        await using var client = await McpClient.StartAsync(ElicitingCapabilities);
        string text = await client.CallToolAnsweringAsync("form",
            @"{""action"":""accept"",""content"":{""note"":""hi"",""n"":null}}");
        text.ShouldBe("ERR:invalid response from client: missing required answer 'n'");
    }

    [KitsuneExeFact]
    public async Task Elicit_AcceptWithNullContent_GivesEmptyResults()
    {
        await using var client = await McpClient.StartAsync(ElicitingCapabilities);
        string text = await client.CallToolAnsweringAsync("optional_only",
            @"{""action"":""accept"",""content"":null}");
        text.ShouldBe("empty");
    }

    [KitsuneExeFact]
    public async Task Initialize_ElicitationCapabilityNull_CannotElicit()
    {
        await using var client = await McpClient.StartAsync(@"{""elicitation"":null}");
        (await client.CallToolAsync("can_elicit")).ShouldBe("false");
    }

    [KitsuneExeFact]
    public async Task Initialize_ElicitationCapabilityEmptyObject_CanElicit()
    {
        await using var client = await McpClient.StartAsync(ElicitingCapabilities);
        (await client.CallToolAsync("can_elicit")).ShouldBe("true");
    }

    // Question setters (definition side, no client needed)
    [Fact]
    public async Task QuestionAddAnswer_StringQuestion_ReturnsNothing()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(QuestionPrologue + @"
            local s = form:AddQuestion('s', 'string', 'S', false)
            return select('#', s:AddAnswer('a', 'A')) .. '|' .. select('#', s:AddAnswer('b', 'B', function() end))
        ");
        r.String.ShouldBe("0|0");
    }

    [Fact]
    public async Task QuestionAddAnswer_DuplicateValue_Throws()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(QuestionPrologue + @"
            local s = form:AddQuestion('s', 'string', 'S', false)
            s:AddAnswer('a', 'A')
            return err(s.AddAnswer, s, 'a', 'Again')
        ");
        r.String.ShouldBe("AddAnswer: question 's' already has an answer 'a'");
    }

    [Fact]
    public async Task QuestionAddAnswer_NonStringQuestion_Throws()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(QuestionPrologue + @"
            local n = form:AddQuestion('n', 'integer', 'N', false)
            local b = form:AddQuestion('b', 'boolean', 'B', false)
            return err(n.AddAnswer, n, 'a', 'A') .. '|' .. err(b.AddAnswer, b, 'a', 'A')
        ");
        r.String.ShouldBe("AddAnswer: question 'n' is integer; answers are only allowed on string questions"
            + "|AddAnswer: question 'b' is boolean; answers are only allowed on string questions");
    }

    [Fact]
    public async Task QuestionAddAnswer_FreeTextQuestion_Throws()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(QuestionPrologue + @"
            local sized = form:AddQuestion('sized', 'string', 'Sized', false)
            sized:SetLength(1, 2)
            local formatted = form:AddQuestion('formatted', 'string', 'Formatted', false)
            formatted:SetFormat('uri')
            return err(sized.AddAnswer, sized, 'a', 'A') .. '|' .. err(formatted.AddAnswer, formatted, 'a', 'A')
        ");
        r.String.ShouldBe("AddAnswer: question 'sized' is free text (SetLength/SetFormat); it can't also have answers"
            + "|AddAnswer: question 'formatted' is free text (SetLength/SetFormat); it can't also have answers");
    }

    [Fact]
    public async Task QuestionAddAnswer_NonFunctionCallback_Throws()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(QuestionPrologue + @"
            local s = form:AddQuestion('s', 'string', 'S', false)
            local e = err(s.AddAnswer, s, 'a', 'A', 'not a function')
            return tostring(e:match('function expected, got string'))
        ");
        r.String.ShouldBe("function expected, got string");
    }

    [Fact]
    public async Task QuestionAddAnswer_MissingLabel_Throws()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(QuestionPrologue + @"
            local s = form:AddQuestion('s', 'string', 'S', false)
            local e = err(s.AddAnswer, s, 'a')
            return tostring(e:match('string expected, got no value'))
        ");
        r.String.ShouldBe("string expected, got no value");
    }

    [Fact]
    public async Task QuestionSetDescription_ReturnsNothingAndRejectsASecondCall()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(QuestionPrologue + @"
            local s = form:AddQuestion('s', 'string', 'S', false)
            return select('#', s:SetDescription('first')) .. '|' .. err(s.SetDescription, s, 'second')
        ");
        r.String.ShouldBe("0|SetDescription: already set on question 's'");
    }

    [Fact]
    public async Task QuestionSetDescription_NonString_Throws()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(QuestionPrologue + @"
            local s = form:AddQuestion('s', 'string', 'S', false)
            return tostring(err(s.SetDescription, s, {}):match('string expected, got table'))
        ");
        r.String.ShouldBe("string expected, got table");
    }

    [Fact]
    public async Task QuestionSetDefault_IntegerQuestion_AcceptsIntegralFloatAndRejectsOthers()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(QuestionPrologue + @"
            local a = form:AddQuestion('a', 'integer', 'A', false)
            local b = form:AddQuestion('b', 'integer', 'B', false)
            local c = form:AddQuestion('c', 'integer', 'C', false)
            return err(a.SetDefault, a, 3.0) .. '|' .. err(b.SetDefault, b, 3.5) .. '|' .. err(c.SetDefault, c, 'x')
        ");
        r.String.ShouldBe("ok|SetDefault: question 'b' is integer; got number|SetDefault: question 'c' is integer; got string");
    }

    [Fact]
    public async Task QuestionSetDefault_NumberAndBooleanQuestions_CheckTheType()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(QuestionPrologue + @"
            local x = form:AddQuestion('x', 'number', 'X', false)
            local x2 = form:AddQuestion('x2', 'number', 'X2', false)
            local b = form:AddQuestion('b', 'boolean', 'B', false)
            local b2 = form:AddQuestion('b2', 'boolean', 'B2', false)
            return err(x.SetDefault, x, 2) .. '|' .. err(x2.SetDefault, x2, 'x')
                .. '|' .. err(b.SetDefault, b, true) .. '|' .. err(b2.SetDefault, b2, 1)
        ");
        r.String.ShouldBe("ok|SetDefault: question 'x2' is number; got string|ok|SetDefault: question 'b2' is boolean; got number");
    }

    [Fact]
    public async Task QuestionSetDefault_StringQuestion_ExpectsStringOrListWhenMultiple()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(QuestionPrologue + @"
            local s = form:AddQuestion('s', 'string', 'S', false)
            local m = form:AddQuestion('m', 'string', 'M', false)
            m:SetMultiple()
            return err(s.SetDefault, s, 5) .. '|' .. err(m.SetDefault, m, 'a')
        ");
        r.String.ShouldBe("SetDefault: question 's' expects a string; got number"
            + "|SetDefault: question 'm' expects a list of answer values; got string");
    }

    [Fact]
    public async Task QuestionSetDefault_SecondCall_Throws()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(QuestionPrologue + @"
            local n = form:AddQuestion('n', 'integer', 'N', false)
            return select('#', n:SetDefault(4)) .. '|' .. err(n.SetDefault, n, 5)
        ");
        r.String.ShouldBe("0|SetDefault: already set on question 'n'");
    }

    [Fact]
    public async Task QuestionSetDefault_WithoutValue_Throws()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(QuestionPrologue + @"
            local n = form:AddQuestion('n', 'integer', 'N', false)
            return tostring(err(n.SetDefault, n):match('value expected'))
        ");
        r.String.ShouldBe("value expected");
    }

    [Fact]
    public async Task QuestionSetDefault_NotOneOfTheAnswers_ThrowsWhenElicitValidates()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(QuestionPrologue + @"
            local c = form:AddQuestion('c', 'string', 'C', false)
            c:AddAnswer('a', 'A')
            c:SetDefault('zzz')
            return err(form.Elicit, form, {}, {})
        ");
        r.String.ShouldBe("Elicit: default 'zzz' of question 'c' isn't one of its answers");
    }

    [Fact]
    public async Task QuestionSetDefault_MultipleWithUnknownAnswer_ThrowsWhenElicitValidates()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(QuestionPrologue + @"
            local m = form:AddQuestion('m', 'string', 'M', false)
            m:SetMultiple()
            m:AddAnswer('a', 'A')
            m:SetDefault({ 'a', 'nope' })
            return err(form.Elicit, form, {}, {})
        ");
        r.String.ShouldBe("Elicit: default of question 'm' contains something that isn't one of its answers");
    }

    [Fact]
    public async Task QuestionSetDefault_ValidDefaults_PassElicitValidation()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(QuestionPrologue + @"
            local c = form:AddQuestion('c', 'string', 'C', false)
            c:AddAnswer('a', 'A')
            c:SetDefault('a')
            local m = form:AddQuestion('m', 'string', 'M', false)
            m:SetMultiple()
            m:AddAnswer('x', 'X')
            m:AddAnswer('y', 'Y')
            m:SetDefault({ 'x', 'y' })
            return err(form.Elicit, form, {}, {})
        ");
        r.String.ShouldBe("Elicit: must be called from inside a tool call");
    }

    [Fact]
    public async Task QuestionSetRange_StringQuestion_Throws()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(QuestionPrologue + @"
            local s = form:AddQuestion('s', 'string', 'S', false)
            local b = form:AddQuestion('b', 'boolean', 'B', false)
            return err(s.SetRange, s, 1, 2) .. '|' .. err(b.SetRange, b, 1, 2)
        ");
        r.String.ShouldBe("SetRange: question 's' is string; SetRange needs integer or number"
            + "|SetRange: question 'b' is boolean; SetRange needs integer or number");
    }

    [Fact]
    public async Task QuestionSetRange_NoBounds_Throws()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(QuestionPrologue + @"
            local n = form:AddQuestion('n', 'integer', 'N', false)
            return err(n.SetRange, n) .. '|' .. err(n.SetRange, n, nil, nil)
        ");
        r.String.ShouldBe("SetRange: give a minimum, a maximum, or both|SetRange: give a minimum, a maximum, or both");
    }

    [Fact]
    public async Task QuestionSetRange_IntegerQuestion_AcceptsIntegralFloatsAndRejectsFractions()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(QuestionPrologue + @"
            local a = form:AddQuestion('a', 'integer', 'A', false)
            local b = form:AddQuestion('b', 'integer', 'B', false)
            local c = form:AddQuestion('c', 'integer', 'C', false)
            return err(a.SetRange, a, 1.0, 3) .. '|' .. err(b.SetRange, b, 1.5, 3) .. '|' .. err(c.SetRange, c, 'a', 1)
        ");
        r.String.ShouldBe("ok|SetRange: question 'b' is integer; bounds must be integers or nil"
            + "|SetRange: question 'c' is integer; bounds must be integers or nil");
    }

    [Fact]
    public async Task QuestionSetRange_MinimumAboveMaximum_Throws()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(QuestionPrologue + @"
            local n = form:AddQuestion('n', 'integer', 'N', false)
            local x = form:AddQuestion('x', 'number', 'X', false)
            return err(n.SetRange, n, 5, 1) .. '|' .. err(x.SetRange, x, 5.5, 1.5)
        ");
        r.String.ShouldBe("SetRange: minimum is greater than maximum|SetRange: minimum is greater than maximum");
    }

    [Fact]
    public async Task QuestionSetRange_OneSidedBoundsAndSecondCall()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(QuestionPrologue + @"
            local x = form:AddQuestion('x', 'number', 'X', false)
            local y = form:AddQuestion('y', 'number', 'Y', false)
            return select('#', x:SetRange(nil, 2.5)) .. '|' .. err(x.SetRange, x, 0, 1)
                .. '|' .. err(y.SetRange, y, 0.5, 2.5)
        ");
        r.String.ShouldBe("0|SetRange: already set on question 'x'|ok");
    }

    [Fact]
    public async Task QuestionSetRange_NonNumericBoundOnNumberQuestion_Throws()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(QuestionPrologue + @"
            local x = form:AddQuestion('x', 'number', 'X', false)
            return tostring(err(x.SetRange, x, 'a', 1):match('number expected, got string'))
        ");
        r.String.ShouldBe("number expected, got string");
    }

    [Fact]
    public async Task QuestionSetLength_NotFreeText_Throws()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(QuestionPrologue + @"
            local n = form:AddQuestion('n', 'integer', 'N', false)
            local withAnswers = form:AddQuestion('withAnswers', 'string', 'W', false)
            withAnswers:AddAnswer('a', 'A')
            local multi = form:AddQuestion('multi', 'string', 'M', false)
            multi:SetMultiple()
            return err(n.SetLength, n, 1, 2) .. '|' .. err(withAnswers.SetLength, withAnswers, 1, 2)
                .. '|' .. err(multi.SetLength, multi, 1, 2)
        ");
        const string Suffix = "isn't free text; SetLength needs a string question without answers";
        r.String.ShouldBe($"SetLength: question 'n' {Suffix}|SetLength: question 'withAnswers' {Suffix}|SetLength: question 'multi' {Suffix}");
    }

    [Fact]
    public async Task QuestionSetLength_InvalidBounds_Throw()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(QuestionPrologue + @"
            local l = form:AddQuestion('l', 'string', 'L', false)
            return err(l.SetLength, l) .. '|' .. err(l.SetLength, l, 5, 2)
                .. '|' .. err(l.SetLength, l, -1, 2) .. '|' .. err(l.SetLength, l, 1.5, 2)
        ");
        r.String.ShouldBe("SetLength: give a minimum, a maximum, or both"
            + "|SetLength: minimum is greater than maximum"
            + "|SetLength: bounds must be non-negative integers or nil"
            + "|SetLength: bounds must be non-negative integers or nil");
    }

    [Fact]
    public async Task QuestionSetLength_OneSidedBoundAndSecondCall()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(QuestionPrologue + @"
            local l = form:AddQuestion('l', 'string', 'L', false)
            return select('#', l:SetLength(nil, 10)) .. '|' .. err(l.SetLength, l, 1, 2)
        ");
        r.String.ShouldBe("0|SetLength: already set on question 'l'");
    }

    [Fact]
    public async Task QuestionSetFormat_KnownFormats_Accepted()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(QuestionPrologue + @"
            local results = {}
            for _, fmt in ipairs({ 'email', 'uri', 'date', 'date-time' }) do
                local q = form:AddQuestion('q_' .. fmt, 'string', fmt, false)
                results[#results + 1] = select('#', q:SetFormat(fmt)) .. ':' .. err(q.SetFormat, q, fmt)
            end
            return table.concat(results, '|')
        ");
        r.String.ShouldBe("0:SetFormat: already set on question 'q_email'"
            + "|0:SetFormat: already set on question 'q_uri'"
            + "|0:SetFormat: already set on question 'q_date'"
            + "|0:SetFormat: already set on question 'q_date-time'");
    }

    [Fact]
    public async Task QuestionSetFormat_UnknownFormat_Throws()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(QuestionPrologue + @"
            local s = form:AddQuestion('s', 'string', 'S', false)
            return err(s.SetFormat, s, 'bogus')
        ");
        r.String.ShouldBe("SetFormat: unknown format 'bogus' (expected \"email\", \"uri\", \"date\" or \"date-time\")");
    }

    [Fact]
    public async Task QuestionSetFormat_NotFreeText_Throws()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(QuestionPrologue + @"
            local n = form:AddQuestion('n', 'integer', 'N', false)
            local withAnswers = form:AddQuestion('withAnswers', 'string', 'W', false)
            withAnswers:AddAnswer('a', 'A')
            return err(n.SetFormat, n, 'email') .. '|' .. err(withAnswers.SetFormat, withAnswers, 'email')
        ");
        const string Suffix = "isn't free text; SetFormat needs a string question without answers";
        r.String.ShouldBe($"SetFormat: question 'n' {Suffix}|SetFormat: question 'withAnswers' {Suffix}");
    }

    [Fact]
    public async Task QuestionSetFormat_MissingFormat_Throws()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(QuestionPrologue + @"
            local s = form:AddQuestion('s', 'string', 'S', false)
            return tostring(err(s.SetFormat, s):match('string expected, got no value'))
        ");
        r.String.ShouldBe("string expected, got no value");
    }

    [Fact]
    public async Task QuestionSetMultiple_StringQuestion_ReturnsNothingAndRejectsASecondCall()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(QuestionPrologue + @"
            local m = form:AddQuestion('m', 'string', 'M', false)
            return select('#', m:SetMultiple(1, 2)) .. '|' .. err(m.SetMultiple, m)
        ");
        r.String.ShouldBe("0|SetMultiple: already set on question 'm'");
    }

    [Fact]
    public async Task QuestionSetMultiple_NonStringOrFreeText_Throws()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(QuestionPrologue + @"
            local n = form:AddQuestion('n', 'integer', 'N', false)
            local sized = form:AddQuestion('sized', 'string', 'Sized', false)
            sized:SetLength(1, 2)
            local formatted = form:AddQuestion('formatted', 'string', 'Formatted', false)
            formatted:SetFormat('date')
            return err(n.SetMultiple, n) .. '|' .. err(sized.SetMultiple, sized) .. '|' .. err(formatted.SetMultiple, formatted)
        ");
        const string Suffix = "can't be multi-select; SetMultiple needs a string question with answers";
        r.String.ShouldBe($"SetMultiple: question 'n' {Suffix}|SetMultiple: question 'sized' {Suffix}|SetMultiple: question 'formatted' {Suffix}");
    }

    [Fact]
    public async Task QuestionSetMultiple_InvalidBounds_Throw()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(QuestionPrologue + @"
            local m = form:AddQuestion('m', 'string', 'M', false)
            return err(m.SetMultiple, m, 3, 1) .. '|' .. err(m.SetMultiple, m, -1)
        ");
        r.String.ShouldBe("SetMultiple: minimum is greater than maximum|SetMultiple: bounds must be non-negative integers or nil");
    }

    [Fact]
    public async Task QuestionSetMultiple_AfterSetDefault_Throws()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(QuestionPrologue + @"
            local s = form:AddQuestion('s', 'string', 'S', false)
            s:SetDefault('q')
            return err(s.SetMultiple, s)
        ");
        r.String.ShouldBe("SetMultiple: call SetMultiple before SetDefault on question 's' (the default becomes a list)");
    }

    [Fact]
    public async Task QuestionSetMultiple_WithoutAnswers_ThrowsWhenElicitValidates()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(QuestionPrologue + @"
            local m = form:AddQuestion('m', 'string', 'M', false)
            m:SetMultiple()
            return err(form.Elicit, form, {}, {})
        ");
        r.String.ShouldBe("Elicit: question 'm' is multi-select but has no answers");
    }

    [Fact]
    public async Task QuestionSetCallback_Function_ReturnsNothingAndRejectsASecondCall()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(QuestionPrologue + @"
            local n = form:AddQuestion('n', 'integer', 'N', false)
            return select('#', n:SetCallback(function() end)) .. '|' .. err(n.SetCallback, n, function() end)
        ");
        r.String.ShouldBe("0|SetCallback: already set on question 'n'");
    }

    [Fact]
    public async Task QuestionSetCallback_NonFunction_Throws()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(QuestionPrologue + @"
            local n = form:AddQuestion('n', 'integer', 'N', false)
            return tostring(err(n.SetCallback, n, 'x'):match('function expected, got string'))
                .. '|' .. tostring(err(n.SetCallback, n):match('function expected, got no value'))
        ");
        r.String.ShouldBe("function expected, got string|function expected, got no value");
    }

    // Setters as the client sees them
    [KitsuneExeFact]
    public async Task Elicit_ConfiguredQuestions_AppearInTheRequestedSchema()
    {
        await using var client = await McpClient.StartAsync(ElicitingCapabilities);
        JsonNode request = await client.CallToolCapturingElicitationAsync("schema");

        request["params"]!["message"]!.GetValue<string>().ShouldBe("Schema");
        JsonNode schema = request["params"]!["requestedSchema"]!;
        schema["type"]!.GetValue<string>().ShouldBe("object");
        schema["required"]!.AsArray().Select(n => n!.GetValue<string>()).ShouldBe(new[] { "email" });

        JsonNode properties = schema["properties"]!;
        JsonNode email = properties["email"]!;
        email["type"]!.GetValue<string>().ShouldBe("string");
        email["title"]!.GetValue<string>().ShouldBe("Email");
        email["format"]!.GetValue<string>().ShouldBe("email");
        email["minLength"]!.GetValue<int>().ShouldBe(3);
        email["maxLength"]!.GetValue<int>().ShouldBe(40);
        email["description"]!.GetValue<string>().ShouldBe("Where to write");
        email["default"]!.GetValue<string>().ShouldBe("a@b.co");

        JsonNode age = properties["age"]!;
        age["type"]!.GetValue<string>().ShouldBe("integer");
        age["minimum"]!.GetValue<int>().ShouldBe(18);
        age["maximum"]!.GetValue<int>().ShouldBe(99);
        age["default"]!.GetValue<int>().ShouldBe(30);

        JsonNode ratio = properties["ratio"]!;
        ratio["type"]!.GetValue<string>().ShouldBe("number");
        ratio["minimum"]!.GetValue<double>().ShouldBe(0.5);
        ratio["maximum"].ShouldBeNull();
        ratio["default"]!.GetValue<double>().ShouldBe(1.5);

        JsonNode agree = properties["agree"]!;
        agree["type"]!.GetValue<string>().ShouldBe("boolean");
        agree["default"]!.GetValue<bool>().ShouldBeTrue();
    }

    [KitsuneExeFact]
    public async Task Elicit_Answers_AppearAsTitledChoicesInTheRequestedSchema()
    {
        await using var client = await McpClient.StartAsync(ElicitingCapabilities);
        JsonNode request = await client.CallToolCapturingElicitationAsync("schema");
        JsonNode properties = request["params"]!["requestedSchema"]!["properties"]!;

        JsonNode color = properties["color"]!;
        color["type"]!.GetValue<string>().ShouldBe("string");
        color["default"]!.GetValue<string>().ShouldBe("green");
        JsonArray choices = color["oneOf"]!.AsArray();
        choices.Select(c => c!["const"]!.GetValue<string>()).ShouldBe(new[] { "red", "green" });
        choices.Select(c => c!["title"]!.GetValue<string>()).ShouldBe(new[] { "Red", "Green" });

        JsonNode tags = properties["tags"]!;
        tags["type"]!.GetValue<string>().ShouldBe("array");
        tags["minItems"]!.GetValue<int>().ShouldBe(1);
        tags["maxItems"]!.GetValue<int>().ShouldBe(2);
        tags["default"]!.AsArray().Select(n => n!.GetValue<string>()).ShouldBe(new[] { "a" });
        JsonArray options = tags["items"]!["anyOf"]!.AsArray();
        options.Select(o => o!["const"]!.GetValue<string>()).ShouldBe(new[] { "a", "b", "c" });
        options.Select(o => o!["title"]!.GetValue<string>()).ShouldBe(new[] { "A", "B", "C" });
    }

    [KitsuneExeFact]
    public async Task Elicit_SetCallbackAndAnswerCallbacks_ShapeTheResults()
    {
        await using var client = await McpClient.StartAsync(ElicitingCapabilities);
        string text = await client.CallToolAnsweringAsync("callbacks",
            @"{""action"":""accept"",""content"":{""name"":""ann"",""size"":""s"",""toppings"":[""x"",""z""],""plain"":""q""}}");

        // name: question callback sees the value and all answers; size.s: its own callback;
        // toppings.x: its own callback, toppings.z: the question's; plain.q: no callback records true.
        text.ShouldBe("ANN/s|small:s|nil|own:x|nil|q:z|true");
    }

    [KitsuneExeFact]
    public async Task Elicit_SetRange_RejectsIntegerOutsideTheRange()
    {
        await using var client = await McpClient.StartAsync(ElicitingCapabilities);
        (await client.CallToolAnsweringAsync("validated", @"{""action"":""accept"",""content"":{""age"":17}}"))
            .ShouldBe("ERR:invalid response from client: 'age' is outside the allowed range");
        (await client.CallToolAnsweringAsync("validated", @"{""action"":""accept"",""content"":{""age"":100}}"))
            .ShouldBe("ERR:invalid response from client: 'age' is outside the allowed range");
        (await client.CallToolAnsweringAsync("validated", @"{""action"":""accept"",""content"":{""age"":18}}"))
            .ShouldBe("ok");
        (await client.CallToolAnsweringAsync("validated", @"{""action"":""accept"",""content"":{""age"":99}}"))
            .ShouldBe("ok");
    }

    [KitsuneExeFact]
    public async Task Elicit_SetLength_RejectsTextOutsideTheLengthRange()
    {
        await using var client = await McpClient.StartAsync(ElicitingCapabilities);
        (await client.CallToolAnsweringAsync("validated", @"{""action"":""accept"",""content"":{""code"":""a""}}"))
            .ShouldBe("ERR:invalid response from client: 'code' has a length outside the allowed range");
        (await client.CallToolAnsweringAsync("validated", @"{""action"":""accept"",""content"":{""code"":""abcde""}}"))
            .ShouldBe("ERR:invalid response from client: 'code' has a length outside the allowed range");
        (await client.CallToolAnsweringAsync("validated", @"{""action"":""accept"",""content"":{""code"":""ab""}}"))
            .ShouldBe("ok");
        (await client.CallToolAnsweringAsync("validated", @"{""action"":""accept"",""content"":{""code"":""abcd""}}"))
            .ShouldBe("ok");
    }

    [KitsuneExeFact]
    public async Task Elicit_SetMultiple_RejectsPickCountsOutsideTheRange()
    {
        await using var client = await McpClient.StartAsync(ElicitingCapabilities);
        (await client.CallToolAnsweringAsync("validated", @"{""action"":""accept"",""content"":{""picks"":[]}}"))
            .ShouldBe("ERR:invalid response from client: 'picks' has 0 picks, outside the allowed range");
        (await client.CallToolAnsweringAsync("validated", @"{""action"":""accept"",""content"":{""picks"":[""a"",""b"",""c""]}}"))
            .ShouldBe("ERR:invalid response from client: 'picks' has 3 picks, outside the allowed range");
        (await client.CallToolAnsweringAsync("validated", @"{""action"":""accept"",""content"":{""picks"":[""a"",""b""]}}"))
            .ShouldBe("ok");
        (await client.CallToolAnsweringAsync("validated", @"{""action"":""accept"",""content"":{""picks"":[""a"",""zzz""]}}"))
            .ShouldBe("ERR:invalid response from client: 'picks' contains something that isn't one of its answers");
    }

    // Client side
    private sealed class McpClient : IAsyncDisposable
    {
        private static readonly TimeSpan ReadTimeout = TimeSpan.FromSeconds(10);

        private readonly Process process;
        private readonly string scriptPath;
        private int nextId = 1;

        private McpClient(Process process, string scriptPath)
        {
            this.process = process;
            this.scriptPath = scriptPath;
        }

        public static async Task<McpClient> StartAsync(string capabilitiesJson)
        {
            File.Exists(KitsuneExe).ShouldBeTrue($"Kitsune.exe not built: {KitsuneExe}");
            string script = Path.Combine(Path.GetTempPath(), "_kitsune_mcp_" + Guid.NewGuid().ToString("N") + ".lua");
            await File.WriteAllTextAsync(script, ServerScript);

            var psi = new ProcessStartInfo(KitsuneExe, $"\"{script}\"")
            {
                UseShellExecute = false,
                CreateNoWindow = true,
                RedirectStandardInput = true,
                RedirectStandardOutput = true,
                RedirectStandardError = true,
                StandardInputEncoding = new System.Text.UTF8Encoding(false), // no BOM before the first message
                WorkingDirectory = Path.GetDirectoryName(KitsuneExe)!,
            };
            var client = new McpClient(Process.Start(psi)!, script);

            JsonNode init = await client.RequestAsync("initialize",
                $@"{{""protocolVersion"":""2025-11-25"",""capabilities"":{capabilitiesJson},""clientInfo"":{{""name"":""tests"",""version"":""1""}}}}");
            init["result"]!["protocolVersion"]!.GetValue<string>().ShouldBe("2025-11-25");
            await client.SendAsync(@"{""jsonrpc"":""2.0"",""method"":""notifications/initialized""}");
            return client;
        }

        // Calls a tool that doesn't elicit: the very next message must be its result.
        public async Task<string> CallToolAsync(string name)
        {
            JsonNode response = await RequestAsync("tools/call", $@"{{""name"":""{name}"",""arguments"":{{}}}}");
            return ToolText(response);
        }

        // Calls a tool that elicits once, answering the elicitation/create with `result`.
        public async Task<string> CallToolAnsweringAsync(string name, string resultJson)
        {
            int id = nextId++;
            await SendAsync($@"{{""jsonrpc"":""2.0"",""id"":{id},""method"":""tools/call"",""params"":{{""name"":""{name}"",""arguments"":{{}}}}}}");

            JsonNode request = await ReadAsync();
            request["method"]?.GetValue<string>().ShouldBe("elicitation/create");
            string elicitId = request["id"]!.GetValue<string>();
            await SendAsync($@"{{""jsonrpc"":""2.0"",""id"":""{elicitId}"",""result"":{resultJson}}}");

            JsonNode response = await ReadAsync();
            response["id"]!.GetValue<int>().ShouldBe(id);
            return ToolText(response);
        }

        // Calls a tool that elicits once and declines the form; returns the elicitation/create request the server sent.
        public async Task<JsonNode> CallToolCapturingElicitationAsync(string name)
        {
            int id = nextId++;
            await SendAsync($@"{{""jsonrpc"":""2.0"",""id"":{id},""method"":""tools/call"",""params"":{{""name"":""{name}"",""arguments"":{{}}}}}}");

            JsonNode request = await ReadAsync();
            request["method"]?.GetValue<string>().ShouldBe("elicitation/create");
            string elicitId = request["id"]!.GetValue<string>();
            await SendAsync($@"{{""jsonrpc"":""2.0"",""id"":""{elicitId}"",""result"":{{""action"":""decline""}}}}");

            JsonNode response = await ReadAsync();
            response["id"]!.GetValue<int>().ShouldBe(id);
            ToolText(response).ShouldBe("default:decline");
            return request;
        }

        public async ValueTask DisposeAsync()
        {
            try
            {
                process.StandardInput.Close(); // EOF ends the server's poll loop and the script
                using var cts = new CancellationTokenSource(TimeSpan.FromSeconds(10));
                await process.WaitForExitAsync(cts.Token);
            }
            catch (OperationCanceledException)
            {
                process.Kill(true);
            }
            finally
            {
                process.Dispose();
                File.Delete(scriptPath);
            }
        }

        private static string ToolText(JsonNode response)
        {
            JsonNode result = response["result"].ShouldNotBeNull();
            return result["content"]![0]!["text"]!.GetValue<string>();
        }

        private async Task<JsonNode> RequestAsync(string method, string paramsJson)
        {
            int id = nextId++;
            await SendAsync($@"{{""jsonrpc"":""2.0"",""id"":{id},""method"":""{method}"",""params"":{paramsJson}}}");
            JsonNode response = await ReadAsync();
            response["method"].ShouldBeNull($"expected the response to {method}, got a request: {response.ToJsonString()}");
            response["id"]!.GetValue<int>().ShouldBe(id);
            return response;
        }

        private async Task SendAsync(string json)
        {
            await process.StandardInput.WriteAsync(json + "\n"); // MCP stdio frames are '\n'-terminated
            await process.StandardInput.FlushAsync();
        }

        private async Task<JsonNode> ReadAsync()
        {
            using var cts = new CancellationTokenSource(ReadTimeout);
            string? line;
            try
            {
                line = await process.StandardOutput.ReadLineAsync(cts.Token);
            }
            catch (OperationCanceledException)
            {
                throw new TimeoutException("no message from the MCP server within " + ReadTimeout);
            }
            if (line == null)
            {
                throw new InvalidOperationException("MCP server exited: " + await process.StandardError.ReadToEndAsync());
            }

            return JsonNode.Parse(line)!;
        }
    }
}
