using KitsuneNet;
using Shouldly;
using Xunit;

namespace KitsuneNet.Tests;

// See KitsuneEngineTests for why all classes share a single collection.
[Collection("KitsuneSequential")]

/// <summary>
/// Calls every Lua-callable function the engine registers with bad arguments and wrong <c>self</c> values, calls
/// every method of closable objects after they are closed, and collects objects while their operations are
/// pending. Each call must raise a Lua error or return; afterwards the engine must still run Lua. A crash aborts
/// the test host (see <see cref="ProgressLog"/>), and the leak check in <c>KitsuneEngine.Dispose</c> and the
/// sanitizer run cover leaks. Functions are discovered at run time, so new ones are swept automatically.
/// </summary>
public sealed class MisuseTests
{
    private const string ArgSets = @"
        local argsets = {
            {n = 0},
            {n = 1, nil},
            {n = 3, nil, nil, nil},
            {n = 1, 0},
            {n = 1, -1},
            {n = 1, math.mininteger},
            {n = 1, 1e308},
            {n = 1, 0/0},
            {n = 1, ''},
            {n = 1, true},
            {n = 1, {}},
            {n = 1, print},
            {n = 2, '', 0},
            {n = 2, {}, {}},
        }
    ";

    // Lua 5.4 base functions and libraries are not ours to sweep.
    private const string Discovery = @"
        local stdTables = {}
        for _, n in ipairs({'string', 'table', 'math', 'io', 'os', 'coroutine', 'utf8', 'debug', 'package', '_G'}) do
            stdTables[n] = true
        end
        local base = {}
        for _, n in ipairs({'assert', 'collectgarbage', 'dofile', 'error', 'getmetatable', 'ipairs', 'load', 'loadfile',
                            'next', 'pairs', 'pcall', 'print', 'rawequal', 'rawget', 'rawlen', 'rawset', 'require',
                            'select', 'setmetatable', 'tonumber', 'tostring', 'type', 'warn', 'xpcall'}) do
            base[n] = true
        end
    ";

    // Catches calls that block forever, not slow ones: a row makes up to 98 calls, and some functions are
    // slow by nature (Hardware.GpuLoad reads a performance counter per GPU engine, about 1.3 s per call;
    // a Debug-build DuckDB.Open takes up to 1 s).
    private static readonly TimeSpan CallTimeout = TimeSpan.FromSeconds(120);

    // Functions whose effect reaches outside the test process even with junk arguments.
    private static readonly HashSet<string> Excluded = new(StringComparer.Ordinal)
    {
        "ShellExecute",                     // launches programs
        "Break",                            // debugger break
        "Process.Start",                    // spawns processes
        "LuaProcess:Start",                 // the same function reached through a process object
        "Hardware.NvidiaSetPowerLimit",     // changes GPU settings
        "MCP.Create",                       // the server object's Start takes over stdin/stdout
        "Sleep",                            // blocks by design (Sleep(1e308) never returns); covered by its own tests
        "Pause",                            // blocks until resumed by design; covered by its own tests
        "REDIS:Open",                       // Redis.Open reached through the instance; ~100 connection attempts exceed the timeout
    };

    // A crash takes the test host down before xUnit reports the row, and --blame records only the method name.
    // The last line of this file names the row that was running.
    private static readonly string ProgressLog = Path.Combine(AppContext.BaseDirectory, "misuse-progress.log");

    // Types with a close-like method: Lua that creates one instance, that method, and the environment variable
    // the instance needs (null when it needs nothing). Left out: LuaProcess (Stop kills the process), Timer
    // (Stop only pauses it), LUAMCP (Stop of a server that never started), and HTTPRESPONSE/WEBSOCKET (they
    // need a live request and are covered by the HttpServer tests).
    private static readonly Dictionary<string, (Func<string> Create, string Close, string? Env)> Closable = new()
    {
        ["AliveToken"] = (() => "AliveToken.New()", "Dispose", null),
        ["LUAINI"] = (() => "Ini.New()", "Dispose", null),
        ["LUAYAML"] = (() => "Yaml.New()", "Dispose", null),
        ["LUAXML"] = (() => "Xml.New()", "Dispose", null),
        ["LUATOML"] = (() => "Toml.New()", "Dispose", null),
        ["LUAMSGPACK"] = (() => "MsgPack.New()", "Dispose", null),
        ["LUAJSON"] = (() => "Json.New()", "Dispose", null),
        ["STREAM"] = (() => "Stream.New()", "Close", null),
        ["MD5"] = (() => "MD5.New()", "Finish", null),
        ["LUASHA1"] = (() => "SHA1.New()", "Finish", null),
        ["LUASHA256"] = (() => "SHA256.New()", "Finish", null),
        ["SQLite"] = (() => "SQLite.Open()", "Close", null),
        ["DuckDB"] = (() => "DuckDB.Open()", "Close", null),
        ["LuaTask"] = (() => "Tasks.New(function() end)", "Dispose", null),
        ["HTTPSERVER"] = (() => "HttpServer.Listen('127.0.0.1:19900')", "Close", null),
        ["TCPLISTENER"] = (() => "TCP.StartListener(19901)", "Dispose", null),
        ["TCPCLIENT"] = (() => "(function() _listener = TCP.StartListener(19902); return TCP.Connect('127.0.0.1', 19902) end)()", "Dispose", null),
        ["LuaMongo"] = (MongoTests.ConnectLua, "Close", "KITSUNE_MONGO_TEST"),
        ["LuaMySQL"] = (MySqlTests.ConnectLua, "Close", "KITSUNE_MYSQL_TEST"),
        ["LuaPostgres"] = (PostgresTests.ConnectLua, "Close", "KITSUNE_POSTGRES_TEST"),
        ["REDIS"] = (RedisTests.Open, "Dispose", "KITSUNE_REDIS_TEST"),
        ["KAFKAPRODUCER"] = (() => $"Kafka.NewProducer({{['bootstrap.servers'] = '{KafkaTests.Bootstrap()}'}})", "Close", "KITSUNE_KAFKA_TEST"),
        ["KAFKACONSUMER"] = (() => $"Kafka.NewConsumer({{['bootstrap.servers'] = '{KafkaTests.Bootstrap()}', ['group.id'] = 'kitsune_{TestRun.Id}_misuse'}})", "Close", "KITSUNE_KAFKA_TEST"),
    };

    // Each script starts an operation, drops the last reference to the object that owns it, forces a full
    // collection, then drives the operation on (or abandons it). The object must stay alive until the operation
    // ends, or the collection must cancel it cleanly. HttpServer, Mongo and the encoders have their own GC tests.
    private static readonly Dictionary<string, (Func<string> Script, string? Env)> PendingOperations = new()
    {
        ["SQLite statement"] = (() => @"
            local db = SQLite.Open()
            db:Query('CREATE TABLE t (n INTEGER)')
            db:Query('INSERT INTO t VALUES (1), (2), (3)')
            db:Query('SELECT n FROM t')
            db:Fetch()
            db = nil
            collectgarbage(); collectgarbage()
        ", null),
        ["DuckDB statement"] = (() => @"
            local db = DuckDB.Open()
            db:Query('SELECT * FROM range(1000)')
            db:Fetch()
            db = nil
            collectgarbage(); collectgarbage()
        ", null),
        ["HttpClient request"] = (() => @"
            local client = HttpClient.New()
            client:SetTimeout(2000)
            local co = client:Request('GET', BASE .. '/delay/gc')
            coroutine.resume(co)
            client = nil
            collectgarbage(); collectgarbage()
            local deadline = Time() + 5000
            while coroutine.status(co) ~= 'dead' and Time() < deadline do coroutine.resume(co); Sleep(10) end
            co = nil
            collectgarbage(); collectgarbage()
        ", null),
        ["MySQL query"] = (() => $@"
            local conn = assert({MySqlTests.ConnectLua()})
            local co = assert(conn:Query('SELECT SLEEP(0.2)'))
            coroutine.resume(co)
            conn = nil
            collectgarbage(); collectgarbage()
            local deadline = Time() + 5000
            while coroutine.status(co) ~= 'dead' and Time() < deadline do coroutine.resume(co); Sleep(10) end
            co = nil
            collectgarbage(); collectgarbage()
        ", "KITSUNE_MYSQL_TEST"),
        ["Postgres query"] = (() => $@"
            local conn = assert({PostgresTests.ConnectLua()})
            local co = assert(conn:Query('SELECT pg_sleep(0.2)'))
            coroutine.resume(co)
            conn = nil
            collectgarbage(); collectgarbage()
            local deadline = Time() + 5000
            while coroutine.status(co) ~= 'dead' and Time() < deadline do coroutine.resume(co); Sleep(10) end
            co = nil
            collectgarbage(); collectgarbage()
        ", "KITSUNE_POSTGRES_TEST"),
        ["Redis subscription"] = (() => $@"
            local redis = assert({RedisTests.Open()})
            local co = redis:Subscribe('kitsune_' .. RUN_ID .. '_misuse_gc')
            coroutine.resume(co, false)
            redis = nil
            collectgarbage(); collectgarbage()
            coroutine.resume(co, false)
            coroutine.resume(co, true)
            co = nil
            collectgarbage(); collectgarbage()
        ", "KITSUNE_REDIS_TEST"),
    };

    public static TheoryData<string> ModuleFunctions()
    {
        using KitsuneEngine engine = new();
        string list = engine.RunString(Discovery + @"
            local out = {}
            for name, value in pairs(_G) do
                if type(value) == 'table' and not stdTables[name] then
                    for key, f in pairs(value) do
                        if type(f) == 'function' and type(key) == 'string' then out[#out + 1] = name .. '.' .. key end
                    end
                elseif type(value) == 'function' and not base[name] then
                    out[#out + 1] = name
                end
            end
            table.sort(out)
            return table.concat(out, '\n')
        ").String!;
        return ToTheoryData(list);
    }

    // Every metatable registered with luaL_newmetatable (it has __name), except Lua's own FILE*.
    // __gc and __close are left out: only the collector calls them, never with a foreign object.
    public static TheoryData<string> Methods()
    {
        using KitsuneEngine engine = new();
        string list = engine.RunString(@"
            local out = {}
            for _, mt in pairs(debug.getregistry()) do
                if type(mt) == 'table' and type(rawget(mt, '__name')) == 'string' and mt.__name ~= 'FILE*' then
                    local seen = {}
                    local function add(t)
                        for key, f in pairs(t) do
                            if type(f) == 'function' and type(key) == 'string' and key ~= '__gc' and key ~= '__close' and not seen[key] then
                                seen[key] = true
                                out[#out + 1] = mt.__name .. ':' .. key
                            end
                        end
                    end
                    add(mt)
                    if type(rawget(mt, '__index')) == 'table' then add(mt.__index) end
                end
            end
            table.sort(out)
            return table.concat(out, '\n')
        ").String!;
        return ToTheoryData(list);
    }

    public static TheoryData<string> ClosableTypes() => Names(Closable.Keys);

    public static TheoryData<string> PendingOperationNames() => Names(PendingOperations.Keys);

    [Theory]
    [MemberData(nameof(ModuleFunctions))]
    public async Task ModuleFunction_BadArguments_RaisesErrorOrReturns(string function)
    {
        File.AppendAllText(ProgressLog, function + Environment.NewLine);
        using KitsuneEngine engine = new();
        string lookup = function.Contains('.')
            ? $"_G['{function.Split('.')[0]}']['{function.Split('.')[1]}']"
            : $"_G['{function}']";
        LuaValue r = await RunWithTimeout(engine, ArgSets + $@"
            local f = {lookup}
            for _, a in ipairs(argsets) do
                pcall(f, table.unpack(a, 1, a.n))
            end
            collectgarbage()
            collectgarbage()
            return 'alive'
        ");
        r.String.ShouldBe("alive");
        engine.RunString("return 1 + 1").AsInt64.ShouldBe(2L);
    }

    [Theory]
    [MemberData(nameof(Methods))]
    public async Task Method_WrongSelf_RaisesErrorOrReturns(string method)
    {
        File.AppendAllText(ProgressLog, method + Environment.NewLine);
        string[] parts = method.Split(':', 2);
        using KitsuneEngine engine = new();
        LuaValue r = await RunWithTimeout(engine, ArgSets + $@"
            local f
            for _, mt in pairs(debug.getregistry()) do
                if type(mt) == 'table' and rawget(mt, '__name') == '{parts[0]}' then
                    f = rawget(mt, '{parts[1]}')
                    if f == nil and type(rawget(mt, '__index')) == 'table' then f = mt.__index['{parts[1]}'] end
                end
            end
            assert(type(f) == 'function', 'method not found')
            local selves = {{nil, 0, '', true, {{}}, print, io.stdout}}
            for i = 1, 7 do
                for _, a in ipairs(argsets) do
                    pcall(f, selves[i], table.unpack(a, 1, a.n))
                end
            end
            collectgarbage()
            collectgarbage()
            return 'alive'
        ");
        r.String.ShouldBe("alive");
        engine.RunString("return 1 + 1").AsInt64.ShouldBe(2L);
    }

    [SkippableTheory]
    [MemberData(nameof(ClosableTypes))]
    public async Task Method_AfterClose_RaisesErrorOrReturns(string type)
    {
        (Func<string> create, string close, string? env) = Closable[type];
        Skip.If(env is not null && string.IsNullOrEmpty(Environment.GetEnvironmentVariable(env)), $"Set {env} to run");
        File.AppendAllText(ProgressLog, type + " (closed)" + Environment.NewLine);

        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await RunWithTimeout(engine, ArgSets + $@"
            local obj = assert({create()})
            local mt = debug.getmetatable(obj)
            assert(rawget(mt, '__name') == '{type}', 'factory made a ' .. tostring(rawget(mt, '__name')))
            local close = obj.{close}
            close(obj)
            local methods = {{}}
            local function add(t)
                for key, f in pairs(t) do
                    if type(f) == 'function' and type(key) == 'string' and key ~= '__gc' and key ~= '__close' then methods[key] = f end
                end
            end
            add(mt)
            if type(rawget(mt, '__index')) == 'table' then add(mt.__index) end
            for _, f in pairs(methods) do
                for _, a in ipairs(argsets) do
                    pcall(f, obj, table.unpack(a, 1, a.n))
                end
            end
            pcall(close, obj)
            obj = nil
            _listener = nil
            collectgarbage()
            collectgarbage()
            return 'alive'
        ");
        r.String.ShouldBe("alive");
        engine.RunString("return 1 + 1").AsInt64.ShouldBe(2L);
    }

    [SkippableTheory]
    [MemberData(nameof(PendingOperationNames))]
    public async Task PendingOperation_OwnerCollected_DoesNotCrash(string operation)
    {
        (Func<string> script, string? env) = PendingOperations[operation];
        Skip.If(env is not null && string.IsNullOrEmpty(Environment.GetEnvironmentVariable(env)), $"Set {env} to run");
        File.AppendAllText(ProgressLog, operation + " (collected)" + Environment.NewLine);

        using var echo = new LocalEchoServer();
        using KitsuneEngine engine = TestRun.NewEngine();
        engine.SetString("BASE", echo.BaseUrl);
        LuaValue r = await RunWithTimeout(engine, script() + "\nreturn 'alive'");
        r.String.ShouldBe("alive");
        engine.RunString("return 1 + 1").AsInt64.ShouldBe(2L);
    }

    [Fact]
    public void Discovery_FindsEveryModuleAndMetatable()
    {
        // Guards the discovery itself: if a module stops being a global table or metatables lose __name,
        // the theories above would silently sweep less.
        int functions = ModuleFunctions().Cast<object>().Count();
        int methods = Methods().Cast<object>().Count();
        functions.ShouldBeGreaterThan(400);
        methods.ShouldBeGreaterThan(300);
    }

    private static TheoryData<string> Names(IEnumerable<string> names)
    {
        var data = new TheoryData<string>();
        foreach (string name in names.Order(StringComparer.Ordinal))
        {
            data.Add(name);
        }

        return data;
    }

    private static TheoryData<string> ToTheoryData(string list)
    {
        var data = new TheoryData<string>();
        foreach (string name in list.Split('\n', StringSplitOptions.RemoveEmptyEntries))
        {
            if (!Excluded.Contains(name) && !Excluded.Contains(name.Split('.')[0]))
            {
                data.Add(name);
            }
        }

        return data;
    }

    // Junk arguments become file names (SQLite.Open(0) creates a database called "0"), so each sweep runs in
    // an empty working directory that is removed afterwards. The test classes share one sequential collection,
    // so changing the process working directory affects no other test.
    private static async Task<LuaValue> RunWithTimeout(KitsuneEngine engine, string script)
    {
        string previous = Environment.CurrentDirectory;
        string scratch = Path.Combine(Path.GetTempPath(), "kitsune-misuse-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(scratch);
        Environment.CurrentDirectory = scratch;
        try
        {
            Task<LuaValue> run = engine.ExecuteStringAsync(script);
            if (await Task.WhenAny(run, Task.Delay(CallTimeout)) != run)
            {
                foreach (int id in engine.GetActiveIds())
                {
                    engine.Cancel(id);
                }

                engine.Interrupt();
                throw new TimeoutException($"Still running after {CallTimeout.TotalSeconds} s: a call blocked instead of failing on bad arguments");
            }

            return await run;
        }
        finally
        {
            Environment.CurrentDirectory = previous;
            try
            {
                Directory.Delete(scratch, recursive: true);
            }
            catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
            {
                // A file still held open by an object awaiting collection; the temp folder is cleaned by the OS.
            }
        }
    }
}
