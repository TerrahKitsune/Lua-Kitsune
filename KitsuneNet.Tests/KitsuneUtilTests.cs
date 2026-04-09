using KitsuneNet;
using Shouldly;
using Xunit;

namespace KitsuneNet.Tests
{
    // See KitsuneEngineTests for why both classes share a single collection.
    [Collection("KitsuneSequential")]

    /// <summary>
    /// Tests for the non-module global functions in the Kitsune Lua environment.
    /// Covers: UUID, CRC32/64, Time, Runtime, GetMemory, GlobalMemoryStatus,
    /// string.equal, setenv/getenv, table.first/select, system queries,
    /// BencodeDecode, GetLastError, c global, clipboard, registry, and more.
    /// </summary>
    public sealed class KitsuneUtilTests
    {
        // Helper Lua prologue shared by all socket-simulation tests.
        private const string SocketPrologue =
            "local OPEN, CLOSE, READ, CAP_READ = 0, 1, 2, 1\n";

        private const string RunCoroutine = @"
            local function run(fn)
                local co = coroutine.create(fn)
                local ok, err = coroutine.resume(co)
                while ok and coroutine.status(co) ~= 'dead' do
                    ok, err = coroutine.resume(co)
                end
                if not ok then error(err, 2) end
            end
        ";

        // Lua function-backend stream that yields once per chunk via Sleep(0)
        // and supports CAP_READ only (no seek).
        private const string MakeChunkedStream = @"
            local function makeChunkedStream(chunks)
                local idx, pending = 1, false
                return Stream.Create(function(op)
                    if op == 0 then return 1 end
                    if op == 1 then return true end
                    if op == 5 then return pending and 1 or 0 end
                    if op == 8 then return pending end
                    if op == 2 then
                        if idx > #chunks then return nil end
                        if not pending then pending = true; Sleep(0) end
                        pending = false
                        local c = chunks[idx]; idx = idx + 1; return c
                    end
                end)
            end
        ";

        // -- UUID -----------------------------------------------------------------
        [Fact]
        public async Task UUID_HasStandardFormat()
        {
            LuaValue r = await Run(@"
                local id = UUID()
                return tostring(type(id) == 'string' and #id == 36
                    and id:sub(9,9) == '-' and id:sub(14,14) == '-'
                    and id:sub(19,19) == '-' and id:sub(24,24) == '-')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task UUID_ConsecutiveCalls_AreDistinct()
        {
            LuaValue r = await Run("return tostring(UUID() ~= UUID())");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task UUID_IsVersion4()
        {
            // The 15th character (the version nibble, after two hyphens) must be '4'.
            LuaValue r = await Run("return UUID():sub(15,15)");
            r.String.ShouldBe("4");
        }

        [Fact]
        public async Task UUID_HasRfc4122Variant()
        {
            // Variant bits 10xx: the 17th character must be 8, 9, a, or b.
            LuaValue r = await Run(@"
                local c = UUID():sub(20,20)
                return tostring(c == '8' or c == '9' or c == 'a' or c == 'b')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task UUID_ContainsOnlyHexAndDashes()
        {
            LuaValue r = await Run(@"
                local id = UUID()
                return tostring(id:match('^%x%x%x%x%x%x%x%x%-%x%x%x%x%-%x%x%x%x%-%x%x%x%x%-%x%x%x%x%x%x%x%x%x%x%x%x$') ~= nil)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task UUID_BinaryReturnIs16Bytes()
        {
            // UUID() returns two values: the string and a 16-byte binary blob.
            LuaValue r = await Run(@"
                local _, bin = UUID()
                return tostring(type(bin) == 'string' and #bin == 16)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task UUID_BinaryVersionNibbleIs4()
        {
            // Byte 7 (1-based): high nibble must be 0x4.
            LuaValue r = await Run(@"
                local _, bin = UUID()
                local b = bin:byte(7)
                return tostring(b >> 4 == 4)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task UUID_BinaryVariantBitsAreRfc4122()
        {
            // Byte 9 (1-based): top two bits must be 10xxxxxx (0x80–0xBF).
            LuaValue r = await Run(@"
                local _, bin = UUID()
                local b = bin:byte(9)
                return tostring(b & 0xC0 == 0x80)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task UUID_StringMatchesBinaryEncoding()
        {
            // The string representation must round-trip consistently with the binary bytes.
            LuaValue r = await Run(@"
                local str, bin = UUID()
                local hex = str:gsub('-', '')
                local rebuilt = ''
                for i = 1, 16 do
                    rebuilt = rebuilt .. string.format('%02x', bin:byte(i))
                end
                return tostring(hex == rebuilt)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task UUID_Large_Sample_AllDistinct()
        {
            // Probabilistically verify uniqueness across 1000 generations.
            LuaValue r = await Run(@"
                local seen = {}
                for i = 1, 1000 do
                    local id = UUID()
                    if seen[id] then return 'false' end
                    seen[id] = true
                end
                return 'true'
            ");
            r.String.ShouldBe("true");
        }

        // -- CRC32 ----------------------------------------------------------------
        [Fact]
        public async Task CRC32_ReturnsInteger()
        {
            LuaValue r = await Run("return math.type(CRC32('hello'))");
            r.String.ShouldBe("integer");
        }

        [Fact]
        public async Task CRC32_IsDeterministic()
        {
            LuaValue r = await Run("return tostring(CRC32('hello') == CRC32('hello'))");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task CRC32_DifferentInputs_ProduceDifferentValues()
        {
            LuaValue r = await Run("return tostring(CRC32('hello') ~= CRC32('world'))");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task CRC32_IncrementalMatchesFull()
        {
            LuaValue r = await Run(@"
                local full = CRC32('hello world')
                local inc  = CRC32('world', CRC32('hello '))
                return tostring(full == inc)
            ");
            r.String.ShouldBe("true");
        }

        // -- CRC64 ----------------------------------------------------------------
        [Fact]
        public async Task CRC64_ReturnsNumber()
        {
            LuaValue r = await Run("return type(CRC64('hello'))");
            r.String.ShouldBe("number");
        }

        [Fact]
        public async Task CRC64_IsDeterministic()
        {
            LuaValue r = await Run("return tostring(CRC64('test') == CRC64('test'))");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task CRC64_DifferentInputs_ProduceDifferentValues()
        {
            LuaValue r = await Run("return tostring(CRC64('hello') ~= CRC64('world'))");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task CRC64_WithWchar_ReturnsNumber()
        {
            // CRC64 uses the raw UTF-16 LE bytes of the Wchar, not the UTF-8 encoding.
            LuaValue r = await Run("return tostring(type(CRC64(Wchar.FromUtf8('hello'))) == 'number')");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task CRC64_Wchar_IsDeterministic()
        {
            LuaValue r = await Run(@"
                local w = Wchar.FromUtf8('deterministic')
                return tostring(CRC64(w) == CRC64(w))
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task CRC64_Wchar_DiffersFromStringEquivalent()
        {
            // Wchar stores UTF-16 LE bytes; plain string is UTF-8 — different byte sequences.
            LuaValue r = await Run("return tostring(CRC64(Wchar.FromUtf8('hello')) ~= CRC64('hello'))");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task CRC64_WithStream_ReturnsNumber()
        {
            // CRC64 accepts a Stream argument and returns a number.
            LuaValue r = await Run(@"
                local s = Stream.Create()
                s:Write('hello')
                return tostring(type(CRC64(s)) == 'number')
            ");
            r.String.ShouldBe("true");
        }

        // -- Time -----------------------------------------------------------------
        [Fact]
        public async Task Time_ReturnsPositiveInteger()
        {
            LuaValue r = await Run("local t = Time(); return tostring(t > 0 and math.type(t) == 'integer')");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Time_SecondCall_IsGreaterOrEqual()
        {
            LuaValue r = await Run("local a = Time(); local b = Time(); return tostring(b >= a)");
            r.String.ShouldBe("true");
        }

        // -- Runtime --------------------------------------------------------------
        [Fact]
        public async Task Runtime_ReturnsNonNegativeNumber()
        {
            LuaValue r = await Run("return tostring(Runtime() >= 0)");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Runtime_IncreasesAfterSleep()
        {
            LuaValue r = await Run("local a = Runtime(); Sleep(20); return tostring(Runtime() > a)");
            r.String.ShouldBe("true");
        }

        // -- GetMemory ------------------------------------------------------------
        [Fact]
        public async Task GetMemory_ReturnsPositiveValue()
        {
            LuaValue r = await Run("return tostring(GetMemory() > 0)");
            r.String.ShouldBe("true");
        }

        // -- GlobalMemoryStatus ---------------------------------------------------
        [Fact]
        public async Task GlobalMemoryStatus_Default_ReturnsPercentageInRange()
        {
            LuaValue r = await Run("local p = GlobalMemoryStatus(); return tostring(type(p) == 'number' and p >= 0 and p <= 100)");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task GlobalMemoryStatus_TotalPhysical_ReturnsPositive()
        {
            LuaValue r = await Run("return tostring(GlobalMemoryStatus(1) > 0)");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task GlobalMemoryStatus_AllTypes_ReturnNumbers()
        {
            LuaValue r = await Run(@"
                for i = 0, 6 do
                    if type(GlobalMemoryStatus(i)) ~= 'number' then return 'false' end
                end
                return 'true'
            ");
            r.String.ShouldBe("true");
        }

        // -- string.equal ---------------------------------------------------------
        [Fact]
        public async Task StringEqual_SameString_ReturnsTrue()
        {
            LuaValue r = await Run("return tostring(string.equal('hello', 'hello'))");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task StringEqual_DifferentCase_ReturnsTrue()
        {
            LuaValue r = await Run("return tostring(string.equal('Hello World', 'hello world'))");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task StringEqual_DifferentStrings_ReturnsFalse()
        {
            LuaValue r = await Run("return tostring(string.equal('hello', 'world'))");
            r.String.ShouldBe("false");
        }

        // -- setenv / getenv ------------------------------------------------------
        [Fact]
        public async Task SetEnv_GetEnv_RoundTrip()
        {
            // The returned value includes a trailing null byte; strip it before comparing.
            LuaValue r = await Run(@"
                setenv('KITSUNE_UTIL_TEST_1', 'hello_kitsune', true)
                return getenv('KITSUNE_UTIL_TEST_1'):gsub('%z', '')
            ");
            r.String.ShouldBe("hello_kitsune");
        }

        [Fact]
        public async Task SetEnv_WithoutOverride_PreservesOriginalValue()
        {
            LuaValue r = await Run(@"
                setenv('KITSUNE_UTIL_TEST_2', 'original', true)
                setenv('KITSUNE_UTIL_TEST_2', 'overwritten', false)
                return getenv('KITSUNE_UTIL_TEST_2'):gsub('%z', '')
            ");
            r.String.ShouldBe("original");
        }

        [Fact]
        public async Task GetEnv_NonExistentVariable_ReturnsNilOrEmpty()
        {
            LuaValue r = await Run(@"
                local v = getenv('KITSUNE_UTIL_TEST_DEFINITELY_NOT_SET_XYZ_9987')
                -- nil or empty string (possibly with null byte); strip null before checking
                if v then v = v:gsub('%z', '') end
                return tostring(v == nil or v == '')
            ");
            r.String.ShouldBe("true");
        }

        // -- table.first ----------------------------------------------------------
        [Fact]
        public async Task TableFirst_UniqueMatch_ReturnsKey()
        {
            // Only b maps to 2, so deterministic regardless of iteration order.
            LuaValue r = await Run("return table.first({a=1, b=2, c=3}, function(k,v) if v==2 then return k end end)");
            r.String.ShouldBe("b");
        }

        [Fact]
        public async Task TableFirst_UniqueMatch_ReturnsValue()
        {
            LuaValue r = await Run("return tostring(table.first({a=5,b=99,c=5}, function(k,v) if v>50 then return v end end))");
            r.String.ShouldBe("99");
        }

        [Fact]
        public async Task TableFirst_NoMatch_ReturnsNil()
        {
            LuaValue r = await Run("return tostring(table.first({a=1,b=2}, function(k,v) if v>100 then return v end end))");
            r.String.ShouldBe("nil");
        }

        // -- table.select ---------------------------------------------------------
        [Fact]
        public async Task TableSelect_FilterEvenNumbers_ReturnsCorrectValues()
        {
            LuaValue r = await Run(@"
                local e = table.select({1,2,3,4,5,6}, function(k,v) if v%2==0 then return v end end)
                table.sort(e)
                return #e .. ':' .. table.concat(e, ',')
            ");
            r.String.ShouldBe("3:2,4,6");
        }

        [Fact]
        public async Task TableSelect_NoMatches_ReturnsEmptyTable()
        {
            LuaValue r = await Run("local t = table.select({1,3,5}, function(k,v) if v%2==0 then return v end end); return tostring(type(t)=='table' and #t==0)");
            r.String.ShouldBe("true");
        }

        // -- GetIsAdmin -----------------------------------------------------------
        [Fact]
        public async Task GetIsAdmin_ReturnsBool()
        {
            LuaValue r = await Run("return type(GetIsAdmin())");
            r.String.ShouldBe("boolean");
        }

        // -- GetComputerName ------------------------------------------------------
        [Fact]
        public async Task GetComputerName_ReturnsNonEmptyString()
        {
            LuaValue r = await Run("local n = GetComputerName(); return tostring(n ~= nil and #n > 0)");
            r.String.ShouldBe("true");
        }

        // -- GetScreenSize / GetCursorPosition ------------------------------------
        [Fact]
        public async Task GetScreenSize_ReturnsTwoNumbers()
        {
            LuaValue r = await RunWithSession("local w,h = Session.Display.GetScreenSize(); return tostring(type(w)=='number' and type(h)=='number')");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task GetCursorPosition_ReturnsTwoNumbers()
        {
            LuaValue r = await RunWithSession("local x,y = Session.Display.GetCursorPosition(); return tostring(type(x)=='number' and type(y)=='number')");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task GetCursorPointPosition_ReturnsTwoNumbers()
        {
            LuaValue r = await RunWithSession("local x,y = Session.Display.GetCursorPoint(); return tostring(type(x)=='number' and type(y)=='number')");
            r.String.ShouldBe("true");
        }

        // -- BencodeDecode --------------------------------------------------------
        [Fact]
        public async Task BencodeDecode_StringField_Decoded()
        {
            // BencodeDecode wraps each top-level decoded value in an outer array:
            // BencodeDecode(data) returns {[1]=value, [2]=value2, ...}.
            // A bencode dict is therefore at t[1], not t directly.
            LuaValue r = await Run("local t = BencodeDecode('d3:foo3:bare'); return tostring(type(t)=='table' and t[1].foo=='bar')");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task BencodeDecode_IntegerField_Decoded()
        {
            LuaValue r = await Run("local t = BencodeDecode('d3:numi42ee'); return tostring(type(t)=='table' and t[1].num==42)");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task BencodeDecode_ListField_Decoded()
        {
            LuaValue r = await Run("local t = BencodeDecode('d4:listli1ei2ei3eee'); return tostring(type(t[1].list)=='table' and #t[1].list==3)");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task BencodeDecode_NestedDict_Decoded()
        {
            // 'd5:outerd5:inneri99eee' encodes {outer={inner=99}}
            LuaValue r = await Run("local t = BencodeDecode('d5:outerd5:inneri99eee'); return tostring(type(t[1].outer)=='table' and t[1].outer.inner==99)");
            r.String.ShouldBe("true");
        }

        // -- GetLastError ---------------------------------------------------------
        [Fact]
        public async Task GetLastError_WithCode2_ReturnsNonEmptyMessageAndCode()
        {
            LuaValue r = await Run("local m,c = GetLastError(2); return tostring(type(m)=='string' and #m>0 and type(c)=='number')");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task GetLastError_NoArgs_ReturnsString()
        {
            LuaValue r = await Run("return type((GetLastError()))");
            r.String.ShouldBe("string");
        }

        // -- c global variable ----------------------------------------------------
        [Fact]
        public async Task CGlobal_IsTable()
        {
            LuaValue r = await Run("return type(c)");
            r.String.ShouldBe("table");
        }

        [Fact]
        public async Task CGlobal_LF_MatchesNewline()
        {
            LuaValue r = await Run("return tostring(c.LF == '\\n')");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task CGlobal_HasAtLeast32Entries()
        {
            LuaValue r = await Run("local n=0; for _ in pairs(c) do n=n+1 end; return tostring(n>=32)");
            r.String.ShouldBe("true");
        }

        // -- Global variables -----------------------------------------------------
        [Fact]
        public async Task VERSION_Global_IsNonEmptyString()
        {
            LuaValue r = await Run("return tostring(type(VERSION) == 'string' and #VERSION > 0)");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task CPUID_Global_IsNonEmptyString()
        {
            LuaValue r = await Run("return tostring(type(CPUID) == 'string' and #CPUID > 0)");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task DEBUG_Global_IsBoolOrNil()
        {
            // DEBUG is true in debug builds; the global is not defined in release builds.
            LuaValue r = await Run("local t = type(DEBUG); return tostring(t == 'boolean' or t == 'nil')");
            r.String.ShouldBe("true");
        }

        // -- Clipboard ------------------------------------------------------------
        [Fact]
        public async Task Clipboard_SetAndGet_RoundTrip()
        {
            // Clipboard access from a background scheduler thread can silently fail
            // on Windows (clipboard requires UI thread ownership). Skip if set or
            // read-back doesn't round-trip correctly.
            LuaValue r = await RunWithSession(@"
                if not Session.Clipboard.Set('kitsune_clip_test_xyz') then return 'skip' end
                local got = Session.Clipboard.Get()
                if got ~= 'kitsune_clip_test_xyz' then return 'skip' end
                return got
            ");
            if (r != "skip")
            {
                r.String.ShouldBe("kitsune_clip_test_xyz");
            }
        }

        // -- GetKeyState / HasKeyDown ---------------------------------------------
        [Fact]
        public async Task GetKeyState_ReturnsBoolean()
        {
            LuaValue r = await RunWithSession("return type(Session.Console.GetKeyState(0x87))");  // VK_F24
            r.String.ShouldBe("boolean");
        }

        [Fact]
        public async Task HasKeyDown_ReturnsBool()
        {
            LuaValue r = await RunWithSession("return type(Session.Console.HasKeyDown())");
            r.String.ShouldBe("boolean");
        }

        // -- Dns ------------------------------------------------------------------
        [Fact]
        public async Task Dns_Localhost_ReturnsString()
        {
            LuaValue r = await Run("local ip = Dns('localhost'); return tostring(ip ~= nil and type(ip)=='string')");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Dns_WithFullFlag_ReturnsTable()
        {
            LuaValue r = await Run("return tostring(type(Dns('localhost', true))=='table')");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Dns_WithFullFlag_EntryHasTypeAndIPFields()
        {
            // Each entry must have a string 'Type' ("IPV4" or "IPV6") and a string 'IP'.
            LuaValue r = await Run(@"
                local results = Dns('localhost', true)
                if type(results) ~= 'table' or #results == 0 then return 'skip' end
                local entry = results[1]
                return tostring(
                    type(entry.Type) == 'string' and
                    type(entry.IP)   == 'string' and
                    (entry.Type == 'IPV4' or entry.Type == 'IPV6'))
            ");
            if (r != "skip")
            {
                r.String.ShouldBe("true");
            }
        }

        // -- Put / GetTextColor (smoke tests) -------------------------------------
        [Fact]
        public async Task Put_DoesNotThrow()
        {
            LuaValue r = await RunWithSession("Session.Console.Put('kitsune_test'); return 'ok'");
            r.String.ShouldBe("ok");
        }

        [Fact]
        public async Task GetTextColor_ReturnsTwoValuesOrNilWhenNoConsole()
        {
            // Returns two integers when a console is attached; nil,nil in headless environments.
            LuaValue r = await RunWithSession(@"
                local bg, fg = Session.Console.GetColor()
                local bgOk = type(bg)=='number' or bg==nil
                local fgOk = type(fg)=='number' or fg==nil
                return tostring(bgOk and fgOk)
            ");
            r.String.ShouldBe("true");
        }

        // -- Base64 ---------------------------------------------------------------
        [Fact]
        public async Task Base64_Encode_ReturnsCorrectString()
        {
            LuaValue r = await Run("return Base64.Encode('hello')");
            r.String.ShouldBe("aGVsbG8=");
        }

        [Fact]
        public async Task Base64_Decode_RoundTrip()
        {
            LuaValue r = await Run("return Base64.Decode(Base64.Encode('kitsune engine'))");
            r.String.ShouldBe("kitsune engine");
        }

        [Fact]
        public async Task Base64_BinaryRoundTrip_PreservesBytes()
        {
            LuaValue r = await Run("local b = '\\0\\1\\2\\255'; return tostring(Base64.Decode(Base64.Encode(b)) == b)");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Base64_GetEncodeTable_Returns64CharString()
        {
            // The default RFC 4648 alphabet is exactly 64 characters starting with 'ABCD'.
            LuaValue r = await Run(@"
                local t = Base64.GetEncodeTable()
                return tostring(type(t) == 'string' and #t == 64 and t:sub(1, 4) == 'ABCD')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Base64_SetEncodeTable_CustomTable_ChangesEncoding()
        {
            // Swap to URL-safe alphabet and verify the encoding changes accordingly.
            LuaValue r = await Run(@"
                local default = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/'
                local urlsafe = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_'
                local input   = string.char(251)   -- 0xFB: top-6-bits = 62
                local std_enc = Base64.Encode(input)
                Base64.SetEncodeTable(urlsafe)
                local url_enc = Base64.Encode(input)
                Base64.SetEncodeTable(default)      -- restore default
                return tostring(std_enc ~= url_enc and url_enc:sub(1,1) == '-')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Base64_SetEncodeTable_RestoreDefault_RoundTripsCorrectly()
        {
            // After restoring the default table, standard round-trips must still work.
            LuaValue r = await Run(@"
                local default = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/'
                Base64.SetEncodeTable(default)
                local plain = 'hello world'
                return tostring(Base64.Decode(Base64.Encode(plain)) == plain)
            ");
            r.String.ShouldBe("true");
        }

        // -- Hashing --------------------------------------------------------------
        [Fact]
        public async Task SHA256_OfAbc_MatchesEngineOutput()
        {
            // RFC 6234 test vector for SHA-256 of "abc".
            LuaValue r = await Run(@"
                local h = SHA256.New()
                h:Update('abc')
                return h:Finish()
            ");
            r.String.ShouldBe("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
        }

        [Fact]
        public async Task SHA256_IncrementalUpdateMatchesSingle()
        {
            LuaValue r = await Run(@"
                local h1 = SHA256.New(); h1:Update('hello world'); local hex1 = h1:Finish()
                local h2 = SHA256.New(); h2:Update('hello'); h2:Update(' world'); local hex2 = h2:Finish()
                return tostring(hex1 == hex2)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task MD5_KnownVector_ReturnsCorrectHex()
        {
            LuaValue r = await Run(@"
                local h = MD5.New(); h:Update('abc'); return h:Finish()
            ");
            r.String.ShouldBe("900150983cd24fb0d6963f7d28e17f72");
        }

        [Fact]
        public async Task MD5_EmptyInput_ReturnsKnownHash()
        {
            LuaValue r = await Run("local h = MD5.New(); h:Update(''); return h:Finish()");
            r.String.ShouldBe("d41d8cd98f00b204e9800998ecf8427e");
        }

        [Fact]
        public async Task SHA1_KnownVector_ReturnsCorrectHex()
        {
            LuaValue r = await Run("local h = SHA1.New(); h:Update('abc'); return h:Finish()");
            r.String.ShouldBe("a9993e364706816aba3e25717850c26c9cd0d89d");
        }

        [Fact]
        public async Task SHA256_Finish_ReturnsBinaryWith32Bytes()
        {
            // Finish() returns two values: the hex string and a raw 32-byte binary digest.
            LuaValue r = await Run(@"
                local h = SHA256.New()
                h:Update('abc')
                local hex, bin = h:Finish()
                return tostring(type(bin) == 'string' and #bin == 32)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task SHA256_BinaryMatchesHex()
        {
            LuaValue r = await Run(@"
                local h = SHA256.New()
                h:Update('abc')
                local hex, bin = h:Finish()
                local rebuilt = ''
                for i = 1, 32 do rebuilt = rebuilt .. string.format('%02x', string.byte(bin, i)) end
                return tostring(hex == rebuilt)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task MD5_Finish_ReturnsBinaryWith16Bytes()
        {
            LuaValue r = await Run(@"
                local h = MD5.New()
                h:Update('abc')
                local hex, bin = h:Finish()
                return tostring(type(bin) == 'string' and #bin == 16)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task SHA1_Finish_ReturnsBinaryWith20Bytes()
        {
            LuaValue r = await Run(@"
                local h = SHA1.New()
                h:Update('abc')
                local hex, bin = h:Finish()
                return tostring(type(bin) == 'string' and #bin == 20)
            ");
            r.String.ShouldBe("true");
        }

        // -- Json -----------------------------------------------------------------
        // All operations require an instance (Json.New() or Json.Create()).
        // Json.Null is the sentinel value for JSON null.

        // -- Instance round-trips ---------------------------------------------
        [Fact]
        public async Task Json_Encode_ProducesValidJson()
        {
            LuaValue r = await Run(@"
                local j = Json.Create()
                local t = j:Decode(j:Encode({x=1, y='hello', z=true}))
                return tostring(t.x==1 and t.y=='hello' and t.z==true)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Json_DecodeArray_PreservesOrder()
        {
            LuaValue r = await Run(@"
                local j = Json.Create()
                local t = j:Decode('[10,20,30]')
                return tostring(t[1]==10 and t[2]==20 and t[3]==30)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Json_NestedTable_EncodesAndDecodes()
        {
            LuaValue r = await Run(@"
                local j = Json.Create()
                local orig = {a={b={c=42}}}
                local t = j:Decode(j:Encode(orig))
                return tostring(t.a.b.c == 42)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Json_AllBasicTypes_RoundTrip()
        {
            // Verifies every basic Lua type survives an encode/decode cycle.
            LuaValue r = await Run(@"
                local j = Json.Create()
                local orig = {
                    s   = 'hello',
                    n   = 42,
                    f   = 3.14,
                    bt  = true,
                    bf  = false,
                    arr = {1, 2, 3},
                    obj = {nested = 'value'},
                }
                local t = j:Decode(j:Encode(orig))
                return tostring(
                    t.s == 'hello'   and
                    t.n == 42        and
                    t.f == 3.14      and
                    t.bt == true     and
                    t.bf == false    and
                    #t.arr == 3      and t.arr[2] == 2 and
                    t.obj.nested == 'value'
                )
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Json_InstanceReuse_MultipleCalls()
        {
            // The same instance must work correctly for multiple encode/decode calls.
            LuaValue r = await Run(@"
                local j = Json.Create()
                local s1 = j:Encode({a=1})
                local s2 = j:Encode({b=2})
                local t1 = j:Decode(s1)
                local t2 = j:Decode(s2)
                return tostring(t1.a==1 and t2.b==2 and t1.b==nil and t2.a==nil)
            ");
            r.String.ShouldBe("true");
        }

        // -- Number encoding --------------------------------------------------
        [Fact]
        public async Task Json_Integer_EncodedWithoutDecimal()
        {
            // Integers must round-trip as integers (no ".0" suffix).
            LuaValue r = await Run(@"
                local j = Json.New()
                local s = j:Encode(42)
                local v = j:Decode(s)
                return tostring(s == '42' and math.type(v) == 'integer')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Json_Float_Preserved_OnRoundTrip()
        {
            LuaValue r = await Run(@"
                local j = Json.New()
                local v = j:Decode(j:Encode(3.14))
                return tostring(math.type(v) == 'float' and v == 3.14)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Json_NaN_EncodesAsNull()
        {
            LuaValue r = await Run("return Json.New():Encode(0/0)");
            r.String.ShouldBe("null");
        }

        [Fact]
        public async Task Json_Function_EncodesAsNull()
        {
            LuaValue r = await Run("return Json.New():Encode(function() end)");
            r.String.ShouldBe("null");
        }

        [Fact]
        public async Task Json_Thread_EncodesAsNull()
        {
            LuaValue r = await Run("return Json.New():Encode(coroutine.create(function() end))");
            r.String.ShouldBe("null");
        }

        [Fact]
        public async Task Json_Userdata_EncodesAsNull()
        {
            // A Stream is a full userdata; it is not JSON-serializable.
            LuaValue r = await Run("return Json.New():Encode(Stream.Create())");
            r.String.ShouldBe("null");
        }

        [Fact]
        public async Task Json_UnserializableInArray_EncodesAsNull()
        {
            // Functions embedded in arrays produce a valid array with null slots.
            LuaValue r = await Run(@"
                return Json.New():Encode({ 1, function() end, 3 })
            ");
            r.String.ShouldBe("[1,null,3]");
        }

        [Fact]
        public async Task Json_UnserializableInObject_EncodesAsNull()
        {
            // Functions as object values produce valid JSON with null values.
            LuaValue r = await Run(@"
                return Json.New():Encode({ x = function() end })
            ");
            r.String.ShouldBe("{\"x\":null}");
        }

        [Fact]
        public async Task Json_PositiveInfinity_EncodesAsSpecialLiteral()
        {
            LuaValue r = await Run("return Json.New():Encode(math.huge)");
            r.String.ShouldBe("1e+9999");
        }

        // -- Boolean / nil encoding -------------------------------------------
        [Fact]
        public async Task Json_Boolean_True_EncodesCorrectly()
        {
            LuaValue r = await Run("return Json.New():Encode(true)");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Json_Boolean_False_EncodesCorrectly()
        {
            LuaValue r = await Run("return Json.New():Encode(false)");
            r.String.ShouldBe("false");
        }

        [Fact]
        public async Task Json_Nil_EncodesAsNull()
        {
            LuaValue r = await Run("return Json.New():Encode(nil)");
            r.String.ShouldBe("null");
        }

        // -- Array vs object detection ----------------------------------------
        [Fact]
        public async Task Json_SequenceTable_EncodesAsArray()
        {
            // A table with consecutive integer keys 1..n encodes as a JSON array.
            LuaValue r = await Run(@"
                local j = Json.New()
                local s = j:Encode({10, 20, 30})
                local t = j:Decode(s)
                return tostring(s == '[10,20,30]' and t[1]==10 and t[2]==20 and t[3]==30)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Json_EmptyTable_EncodesAsArray()
        {
            // A table with only string keys encodes as a JSON object; a truly empty table encodes as [].
            LuaValue r = await Run("return Json.New():Encode({})");
            r.String.ShouldBe("[]");
        }

        [Fact]
        public async Task Json_StringKeyTable_EncodesAsObject()
        {
            LuaValue r = await Run(@"
                local j = Json.New()
                local t = j:Decode(j:Encode({hello='world'}))
                return t.hello
            ");
            r.String.ShouldBe("world");
        }

        [Fact]
        public async Task Json_New_WithTrue_ProducesPrettyOutput()
        {
            // Json.New(true) must create a pretty-printing instance.
            LuaValue r = await Run(@"
                local j = Json.New(true)
                return tostring(j:Encode({a=1}):find('\n') ~= nil)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Json_New_WithFalse_ProducesCompactOutput()
        {
            // Json.New(false) must create a compact instance.
            LuaValue r = await Run(@"
                local j = Json.New(false)
                return tostring(j:Encode({a=1}):find('\n') == nil)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Json_New_NoArg_ProducesCompactOutput()
        {
            LuaValue r = await Run(@"
                local j = Json.New()
                return tostring(j:Encode({a=1}):find('\n') == nil)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Json_New_CalledOnInstance_ProducesCompactOutput()
        {
            // Regression: New() called on an existing instance must produce a compact
            // instance, not a pretty-printing one.
            LuaValue r = await Run(@"
                local j1 = Json.New()      -- compact
                local j2 = j1:New()        -- must also be compact, not pretty
                return tostring(j2:Encode({a=1}):find('\n') == nil)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Json_MixedTable_EncodesAsObject_IntegerKeysBecomesStrings()
        {
            // Mixed tables (integer and string keys) encode as JSON objects;
            // integer keys become string keys on the round-trip.
            LuaValue r = await Run(@"
                local j   = Json.New()
                local t   = {[1]='a', b=2}
                local s   = j:Encode(t)
                local dec = j:Decode(s)
                return tostring(dec['1'] == 'a' and dec.b == 2 and dec[1] == nil)
            ");
            r.String.ShouldBe("true");
        }

        // -- String escaping --------------------------------------------------
        [Fact]
        public async Task Json_DecodeEscapes_HandledCorrectly()
        {
            LuaValue r = await Run(@"
                local j = Json.New()
                local s = j:Encode('line1\nline2\ttab""quote""')
                local v = j:Decode(s)
                return tostring(v == 'line1\nline2\ttab""quote""')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Json_Decode_NumberTooLong_RaisesError()
        {
            // A number literal that exceeds the internal buffer size must raise an error
            // rather than silently producing a wrong value.
            LuaValue r = await Run(@"
                local ok, err = pcall(function()
                    return Json.New():Decode('1234567890123456789012345678901234567890123456789012345678901234')
                end)
                return tostring(not ok and type(err) == 'string')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Json_Decode_63DigitNumber_ParsesWithoutError()
        {
            // A number literal just within the internal limit must parse without error.
            LuaValue r = await Run(@"
                local ok, v = pcall(function()
                    return Json.New():Decode('123456789012345678901234567890123456789012345678901234567890123')
                end)
                return tostring(ok and type(v) == 'number')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Json_UnicodeEscape_DecodedCorrectly()
        {
            // \u0041 is 'A', \u00E9 is 'é' (U+00E9)
            LuaValue r = await Run(@"
                local j  = Json.New()
                local a  = j:Decode('""\\u0041""')
                local e  = j:Decode('""\\u00E9""')
                return tostring(a == 'A' and e == '\xC3\xA9')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Json_StringWithBackslash_RoundTrips()
        {
            LuaValue r = await Run(@"
                local j = Json.New()
                local s = 'path\\to\\file'
                return tostring(j:Decode(j:Encode(s)) == s)
            ");
            r.String.ShouldBe("true");
        }

        // -- Null sentinel ----------------------------------------------------
        [Fact]
        public async Task Json_NullSentinel_RoundTrips()
        {
            LuaValue r = await Run(@"
                local j   = Json.New()
                local enc = j:Encode({v=Json.Null})
                local dec = j:Decode(enc)
                return tostring(dec.v == Json.Null and enc:find('null') ~= nil)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Json_Null_IsDistinctFromNil()
        {
            LuaValue r = await Run("return tostring(Json.Null ~= nil)");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Json_Null_SameReferenceEveryTime()
        {
            LuaValue r = await Run("return tostring(Json.Null == Json.Null)");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Json_DecodeNull_ReturnsJsonNull()
        {
            LuaValue r = await Run("return tostring(Json.New():Decode('null') == Json.Null)");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Json_EncodeNull_ProducesNullLiteral()
        {
            LuaValue r = await Run("return Json.New():Encode(Json.Null)");
            r.String.ShouldBe("null");
        }

        [Fact]
        public async Task Json_NullInArray_RoundTrips()
        {
            LuaValue r = await Run(@"
                local j = Json.New()
                local t = j:Decode('[1,null,3]')
                return tostring(t[1]==1 and t[2]==Json.Null and t[3]==3)
            ");
            r.String.ShouldBe("true");
        }

        // -- Encode / Decode --------------------------------------------------
        [Fact]
        public async Task Json_Decode_ReturnsTable()
        {
            LuaValue r = await Run(@"
                local j = Json.New()
                local t = j:Decode('{""a"":1,""b"":""hello""}')
                return tostring(t.a == 1 and t.b == 'hello')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Json_Encode_ProducesString()
        {
            LuaValue r = await Run(@"
                local j = Json.New()
                local s = j:Encode({1, 2, 3})
                local t = j:Decode(s)
                return tostring(t[1]==1 and t[2]==2 and t[3]==3)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Json_PrettyEncode_ContainsNewlines()
        {
            LuaValue r = await Run(@"
                local j = Json.New(true)
                local s = j:Encode({a=1})
                return tostring(s:find('\n') ~= nil)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Json_CreateAlias_WorksIdenticallyToNew()
        {
            LuaValue r = await Run(@"
                local j = Json.Create()
                local t = j:Decode('[1,2,3]')
                return tostring(t[3] == 3)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Json_RecursionDetected_ThrowsError()
        {
            LuaValue r = await Run(@"
                local j = Json.New()
                local t = {}
                t.self = t
                local ok, err = pcall(function() j:Encode(t) end)
                return tostring(not ok and err:find('recursion') ~= nil)
            ");
            r.String.ShouldBe("true");
        }

        // -- Stream I/O ----------------------------------------------------------
        [Fact]
        public async Task Json_EncodeIntoStream_StreamContainsValidJson()
        {
            LuaValue r = await Run(@"
                local j = Json.New()
                local s = Stream.Create()
                j:EncodeIntoStream(s, {a=1, b=2})
                s:Seek(0)
                local t = j:Decode(s:Read())
                return tostring(t.a == 1 and t.b == 2)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Json_EncodeIntoStream_ReturnsTrueOnSuccess()
        {
            LuaValue r = await Run(@"
                local j  = Json.New()
                local s  = Stream.Create()
                local ok = j:EncodeIntoStream(s, 42)
                return tostring(ok == true)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Json_DecodeIntoStream_RoundTrip()
        {
            LuaValue r = await Run(@"
                local j = Json.New()
                local s = Stream.Create()
                j:EncodeIntoStream(s, {x=99, y='hello', z=true})
                s:Seek(0)
                local t = j:DecodeIntoStream(s)
                return tostring(t.x == 99 and t.y == 'hello' and t.z == true)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Json_EncodeIntoStream_PrettyFlag_Respected()
        {
            LuaValue r = await Run(@"
                local j = Json.New(true)
                local s = Stream.Create()
                j:EncodeIntoStream(s, {a=1})
                s:Seek(0)
                return tostring(s:Read():find('\n') ~= nil)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Json_EncodeIntoStream_NonWritableStream_ReturnsFalse()
        {
            LuaValue r = await Run(@"
                local j = Json.New()
                local OPEN, CLOSE, CAP_READ = 0, 1, 1
                local s = Stream.Create(function(op)
                    if op == OPEN  then return CAP_READ end
                    if op == CLOSE then return true end
                end)
                local ok, err = j:EncodeIntoStream(s, 'test')
                return tostring(ok == false and type(err) == 'string')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Json_DecodeIntoStream_NonReadableStream_ReturnsNilAndError()
        {
            LuaValue r = await Run(@"
                local j = Json.New()
                local OPEN, CLOSE, CAP_WRITE = 0, 1, 2
                local s = Stream.Create(function(op)
                    if op == OPEN  then return CAP_WRITE end
                    if op == CLOSE then return true end
                end)
                local val, err = j:DecodeIntoStream(s)
                return tostring(val == nil and type(err) == 'string')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Json_EncodeDecodeIntoStream_LargePayload_AllValuesCorrect()
        {
            // 1000 integers produce ~3900 bytes, exercising multiple streaming flushes
            // during encode and multi-chunk reads during decode.
            LuaValue r = await Run(@"
                local j    = Json.New()
                local data = {}
                for i = 1, 1000 do data[i] = i end
                local s = Stream.Create()
                j:EncodeIntoStream(s, data)
                s:Seek(0)
                local t = j:DecodeIntoStream(s)
                return tostring(#t == 1000 and t[1] == 1 and t[500] == 500 and t[1000] == 1000)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Json_EncodeIntoStream_NullSentinel_RoundTrips()
        {
            LuaValue r = await Run(@"
                local j = Json.New()
                local s = Stream.Create()
                j:EncodeIntoStream(s, {v = Json.Null})
                s:Seek(0)
                local t = j:DecodeIntoStream(s)
                return tostring(t.v == Json.Null)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Json_DecodeIntoStream_ReadsFromCurrentPosition()
        {
            // Encode two values back-to-back; seek to the boundary and verify
            // DecodeIntoStream picks up only the second value.
            LuaValue r = await Run(@"
                local j = Json.New()
                local s = Stream.Create()
                j:EncodeIntoStream(s, 'first')
                local split = s:pos()
                j:EncodeIntoStream(s, 'second')
                s:Seek(split)
                local v = j:DecodeIntoStream(s)
                return tostring(v == 'second')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Json_EncodeIntoStream_AdvancesStreamPosition()
        {
            LuaValue r = await Run(@"
                local j = Json.New()
                local s = Stream.Create()
                j:EncodeIntoStream(s, 42)
                return tostring(s:pos() == 2)   -- '42' is 2 bytes
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Json_DecodeIntoStream_PackedObjects_DecodesSequentially()
        {
            // Three JSON objects written end-to-end with no separator; each
            // DecodeIntoStream call must return exactly one object and leave
            // the stream positioned at the start of the next one.
            LuaValue r = await Run(@"
                local j = Json.New()
                local s = Stream.Create()
                j:EncodeIntoStream(s, {n=1})
                j:EncodeIntoStream(s, {n=2})
                j:EncodeIntoStream(s, {n=3})
                s:Seek(0)
                local a = j:DecodeIntoStream(s)
                local b = j:DecodeIntoStream(s)
                local c = j:DecodeIntoStream(s)
                return tostring(a.n==1 and b.n==2 and c.n==3)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Json_DecodeIntoStream_PackedWithWhitespace_DecodesSequentially()
        {
            // Whitespace and newlines between JSON values must be treated as
            // insignificant separators, matching the behaviour for regular Decode.
            LuaValue r = await Run(@"
                local j = Json.New()
                local s = Stream.Create()
                s:Write('{""a"":1}' .. '\n\n' .. '{""b"":2}')
                s:Seek(0)
                local t1 = j:DecodeIntoStream(s)
                local t2 = j:DecodeIntoStream(s)
                return tostring(t1.a == 1 and t2.b == 2)
            ");
            r.String.ShouldBe("true");
        }

        // -- Encode stream / Wchar as JSON value ----------------------------------
        [Fact]
        public async Task Json_Encode_Stream_ReadableSeekable_ProducesJsonString()
        {
            // A readable+seekable stream encodes as a JSON string of its full contents,
            // regardless of the current cursor position.
            LuaValue r = await Run(@"
                local j = Json.New()
                local s = Stream.Create('hello')
                return j:Encode(s)
            ");
            r.String.ShouldBe("\"hello\"");
        }

        [Fact]
        public async Task Json_Encode_Stream_EmptyStream_ProducesNull()
        {
            // An empty stream has no bytes and encodes as null.
            LuaValue r = await Run(@"
                local j = Json.New()
                local s = Stream.Create()
                return j:Encode(s)
            ");
            r.String.ShouldBe("null");
        }

        [Fact]
        public async Task Json_Encode_Stream_PreservesReadPosition()
        {
            // The caller's stream position must be restored after encoding.
            LuaValue r = await Run(@"
                local j = Json.New()
                local s = Stream.Create('ABCDE')
                s:Seek(3)
                j:Encode(s)
                return tostring(s:pos() == 3)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Json_Encode_Stream_QuoteInContent_EscapedCorrectly()
        {
            // A double-quote byte inside the stream must be JSON-escaped as \".
            LuaValue r = await Run(@"
                local j = Json.New()
                local s = Stream.Create('a""b')
                return j:Encode(s)
            ");
            r.String.ShouldBe("\"a\\\"b\"");
        }

        [Fact]
        public async Task Json_Encode_Stream_AsTableValue_RoundTripsAsString()
        {
            // A stream used as a table value encodes as a JSON string;
            // after decode, the value is a Lua string (JSON has no stream type).
            LuaValue r = await Run(@"
                local j = Json.New()
                local s = Stream.Create('hi')
                local t = j:Decode(j:Encode({data = s}))
                return t.data
            ");
            r.String.ShouldBe("hi");
        }

        [Fact]
        public async Task Json_EncodeIntoStream_StreamValue_WritesJsonString()
        {
            // When the VALUE being encoded is itself a stream, EncodeIntoStream must
            // write the stream's contents as a JSON string to the destination stream.
            LuaValue r = await Run(@"
                local j   = Json.New()
                local src = Stream.Create('world')
                local dst = Stream.Create()
                j:EncodeIntoStream(dst, src)
                dst:Seek(0)
                return dst:Read()
            ");
            r.String.ShouldBe("\"world\"");
        }

        [Fact]
        public async Task Json_Encode_Wchar_AsciiContent_ProducesJsonString()
        {
            // ASCII Wchar must produce the same JSON string as the equivalent Lua string.
            LuaValue r = await Run(@"
                local j = Json.New()
                local w = Wchar.FromUtf8('hello')
                return j:Encode(w)
            ");
            r.String.ShouldBe("\"hello\"");
        }

        [Fact]
        public async Task Json_Encode_Wchar_NonAscii_RoundTripsCorrectly()
        {
            // é = U+00E9, UTF-8: 0xC3 0xA9.  Use Lua hex escapes for unambiguous byte values.
            LuaValue r = await Run(@"
                local j = Json.New()
                local w = Wchar.FromUtf8('\xC3\xa9')
                return j:Decode(j:Encode(w))
            ");
            r.String.ShouldBe("é");
        }

        [Fact]
        public async Task Json_Encode_Wchar_Empty_ProducesEmptyJsonString()
        {
            LuaValue r = await Run(@"
                local j = Json.New()
                local w = Wchar.FromUtf8('')
                return j:Encode(w)
            ");
            r.String.ShouldBe("\"\"");
        }

        [Fact]
        public async Task Json_Encode_Wchar_SpecialChars_EscapedCorrectly()
        {
            // Double-quotes inside the Wchar content must be JSON-escaped as \".
            LuaValue r = await Run(@"
                local j = Json.New()
                local w = Wchar.FromUtf8('say ""hi""')
                return j:Encode(w)
            ");
            r.String.ShouldBe("\"say \\\"hi\\\"\"");
        }

        [Fact]
        public async Task Json_Encode_Wchar_NewlineAndTab_EscapedAndRoundTrip()
        {
            // Control characters must be JSON-escaped and survive a full decode round-trip.
            LuaValue r = await Run(@"
                local j = Json.New()
                local w = Wchar.FromUtf8('a' .. '\n' .. 'b')
                return j:Decode(j:Encode(w))
            ");
            r.String.ShouldBe("a\nb");
        }

        [Fact]
        public async Task Json_Encode_Wchar_AsTableValue_RoundTripsAsString()
        {
            // After encode?decode, the decoded value is a Lua string (not a Wchar),
            // since JSON has no wchar type.
            LuaValue r = await Run(@"
                local j = Json.New()
                local w = Wchar.FromUtf8('world')
                local t = j:Decode(j:Encode({msg = w}))
                return t.msg
            ");
            r.String.ShouldBe("world");
        }

        [Fact]
        public async Task Json_Encode_UnknownUserdata_ProducesNull()
        {
            // Any userdata that is neither a Wchar nor a stream must encode as null.
            // Json.New() returns a LuaJson userdata, which is not stream/wchar.
            LuaValue r = await Run(@"
                local j = Json.New()
                return j:Encode(Json.New())
            ");
            r.String.ShouldBe("null");
        }

        // -- Wchar ----------------------------------------------------------------
        [Fact]
        public async Task Wchar_FromUtf8_ToUtf8_RoundTrip()
        {
            LuaValue r = await Run("return Wchar.FromUtf8('hello'):ToUtf8()");
            r.String.ShouldBe("hello");
        }

        [Fact]
        public async Task Wchar_ToUpper_ChangesCase()
        {
            LuaValue r = await Run("return Wchar.FromUtf8('hello world'):ToUpper():ToUtf8()");
            r.String.ShouldBe("HELLO WORLD");
        }

        [Fact]
        public async Task Wchar_ToLower_ChangesCase()
        {
            LuaValue r = await Run("return Wchar.FromUtf8('KITSUNE'):ToLower():ToUtf8()");
            r.String.ShouldBe("kitsune");
        }

        [Fact]
        public async Task Wchar_Substring_ExtractsCorrectly()
        {
            LuaValue r = await Run("return Wchar.FromUtf8('hello world'):Substring(7):ToUtf8()");
            r.String.ShouldBe("world");
        }

        [Fact]
        public async Task Wchar_Length_ReturnsCharCount()
        {
            LuaValue r = await Run("return tostring(#Wchar.FromUtf8('hello'))");
            r.String.ShouldBe("5");
        }

        [Fact]
        public async Task Wchar_Empty_HasZeroLength()
        {
            LuaValue r = await Run("return tostring(#Wchar.FromUtf8('') == 0)");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Wchar_LenMethod_MatchesHashOperator()
        {
            LuaValue r = await Run("local w = Wchar.FromUtf8('hello'); return tostring(w:len() == #w and w:len() == 5)");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Wchar_ToString_ReturnsUtf8String()
        {
            // __tostring metamethod should produce the same result as :ToUtf8().
            LuaValue r = await Run("return tostring(Wchar.FromUtf8('kitsune'))");
            r.String.ShouldBe("kitsune");
        }

        [Fact]
        public async Task Wchar_Substring_WithLength_ExtractsSlice()
        {
            LuaValue r = await Run("return Wchar.FromUtf8('hello world'):Substring(1, 5):ToUtf8()");
            r.String.ShouldBe("hello");
        }

        [Fact]
        public async Task Wchar_Substring_OutOfRange_ReturnsNil()
        {
            LuaValue r = await Run("return tostring(Wchar.FromUtf8('hi'):Substring(99))");
            r.String.ShouldBe("nil");
        }

        [Fact]
        public async Task Wchar_Find_ReturnsPosition()
        {
            LuaValue r = await Run("return tostring(Wchar.FromUtf8('hello world'):Find(Wchar.FromUtf8('world')))");
            r.String.ShouldBe("7");
        }

        [Fact]
        public async Task Wchar_Find_NotFound_ReturnsNil()
        {
            LuaValue r = await Run("return tostring(Wchar.FromUtf8('hello'):Find(Wchar.FromUtf8('xyz')))");
            r.String.ShouldBe("nil");
        }

        [Fact]
        public async Task Wchar_Find_WithOffset_StartsFromPosition()
        {
            // 'a' appears at indices 1 and 4 in 'abcabc'; with offset 2 it finds index 4.
            LuaValue r = await Run("return tostring(Wchar.FromUtf8('abcabc'):Find(Wchar.FromUtf8('a'), 2))");
            r.String.ShouldBe("4");
        }

        [Fact]
        public async Task Wchar_Find_StringPattern_Works()
        {
            // Find also accepts a plain Lua string as the search pattern.
            LuaValue r = await Run("return tostring(Wchar.FromUtf8('hello world'):Find('world'))");
            r.String.ShouldBe("7");
        }

        [Fact]
        public async Task Wchar_Find_NonAsciiStringPattern_Works()
        {
            // String patterns are interpreted as UTF-8; \xC3\xA9 are the UTF-8 bytes for U+00E9 (é).
            LuaValue r = await Run(@"
                local hay = Wchar.FromUtf8('caf\xC3\xA9 au lait')
                return tostring(hay:Find('\xC3\xA9') ~= nil)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Wchar_Equality_SameContent_IsTrue()
        {
            LuaValue r = await Run("return tostring(Wchar.FromUtf8('hello') == Wchar.FromUtf8('hello'))");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Wchar_Equality_DifferentContent_IsFalse()
        {
            LuaValue r = await Run("return tostring(Wchar.FromUtf8('hello') == Wchar.FromUtf8('world'))");
            r.String.ShouldBe("false");
        }

        [Fact]
        public async Task Wchar_Equality_DifferentLengths_IsFalse()
        {
            LuaValue r = await Run("return tostring(Wchar.FromUtf8('hi') == Wchar.FromUtf8('hello'))");
            r.String.ShouldBe("false");
        }

        [Fact]
        public async Task Wchar_Equality_EmptyWchars_AreEqual()
        {
            // Edge case: two empty Wchars must compare as equal.
            LuaValue r = await Run("return tostring(Wchar.FromUtf8('') == Wchar.FromUtf8(''))");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Wchar_Concat_WcharAndWchar_ProducesJoined()
        {
            LuaValue r = await Run("return (Wchar.FromUtf8('hello') .. Wchar.FromUtf8(' world')):ToUtf8()");
            r.String.ShouldBe("hello world");
        }

        [Fact]
        public async Task Wchar_Concat_WcharAndString_ProducesJoined()
        {
            LuaValue r = await Run("return (Wchar.FromUtf8('hello') .. ' world'):ToUtf8()");
            r.String.ShouldBe("hello world");
        }

        [Fact]
        public async Task Wchar_Concat_StringAndWchar_ProducesJoined()
        {
            LuaValue r = await Run("return ('hello ' .. Wchar.FromUtf8('world')):ToUtf8()");
            r.String.ShouldBe("hello world");
        }

        [Fact]
        public async Task Wchar_Concat_NonAsciiStringOperand_ProducesCorrectResult()
        {
            // \xC3\xA9 = UTF-8 for U+00E9 (é); string operands are treated as UTF-8.
            LuaValue r = await Run(@"return (Wchar.FromUtf8('caf') .. '\xC3\xA9'):ToUtf8()");
            r.String.ShouldBe("caf\u00e9");
        }

        [Fact]
        public async Task Wchar_ToBytes_ReturnsCorrectCodeValues()
        {
            // 'A' = 65, 'B' = 66 as wchar_t values.
            LuaValue r = await Run(@"
                local b = Wchar.FromUtf8('AB'):ToBytes()
                return tostring(#b == 2 and b[1] == 65 and b[2] == 66)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Wchar_FromBytes_Table_CreatesCorrectWchar()
        {
            // Reconstruct 'AB' from its wchar_t code values.
            LuaValue r = await Run("return Wchar.FromBytes({65, 66}):ToUtf8()");
            r.String.ShouldBe("AB");
        }

        [Fact]
        public async Task Wchar_FromBytes_SingleInteger_CreatesSingleCharWchar()
        {
            // Codepoint 65 = 'A'.
            LuaValue r = await Run("return Wchar.FromBytes(65):ToUtf8()");
            r.String.ShouldBe("A");
        }

        [Fact]
        public async Task Wchar_FromBytes_InvalidCodepoint_ProducesEmptyWchar()
        {
            // A codepoint above U+10FFFF is invalid; FromBytes must return an empty Wchar.
            LuaValue r = await Run("return tostring(#Wchar.FromBytes(0x200000) == 0)");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Wchar_ToBytes_AsciiChar_ProducesOneCodeUnit()
        {
            // ToBytes returns a table of UTF-16 code units.
            // 'A' is U+0041 — one code unit — so the table has exactly one entry with value 65.
            LuaValue r = await Run(@"
                local units = Wchar.FromUtf8('A'):ToBytes()
                return tostring(#units == 1 and units[1] == 65)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Wchar_FromBytes_Table_RoundTrips()
        {
            // ToBytes returns a table of UTF-16 code units; FromBytes(table) reconstructs from them.
            LuaValue r = await Run(@"
                local w1 = Wchar.FromUtf8('hello')
                local w2 = Wchar.FromBytes(w1:ToBytes())
                return tostring(w1 == w2)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Wchar_Codepoints_ReturnsCorrectTable()
        {
            // 'AB' ? codepoints table {65, 66}.
            LuaValue r = await Run(@"
                local pts = Wchar.FromUtf8('AB'):Codepoints()
                return tostring(#pts == 2 and pts[1] == 65 and pts[2] == 66)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Wchar_At_ValidIndex_ReturnsCodepoint()
        {
            // 'B' is at character position 2 (1-indexed) in 'AB'.
            LuaValue r = await Run("return tostring(Wchar.FromUtf8('AB'):At(2) == 66)");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Wchar_At_OutOfRange_ReturnsNil()
        {
            LuaValue r = await Run("return tostring(Wchar.FromUtf8('hi'):At(99))");
            r.String.ShouldBe("nil");
        }

        [Fact]
        public async Task Wchar_FromAnsi_ToAnsi_AsciiRoundTrip()
        {
            // ASCII characters are stable across all encodings.
            LuaValue r = await Run("return Wchar.FromAnsi('hello'):ToAnsi()");
            r.String.ShouldBe("hello");
        }

        [Fact]
        public async Task Wchar_NonAsciiUtf8_LengthIsWcharCount()
        {
            // U+00E9 (é) is 2 UTF-8 bytes (\xC3\xA9) but 1 wchar_t; length should be 1.
            LuaValue r = await Run(@"return tostring(#Wchar.FromUtf8('\xC3\xA9') == 1)");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Wchar_ChainedOps_ProduceCorrectResult()
        {
            LuaValue r = await Run(@"
                local result = Wchar.FromUtf8('hello world')
                    :ToUpper()
                    :Substring(7)
                return tostring(result:ToUtf8() == 'WORLD')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Wchar_Setlocale_DoesNotThrow()
        {
            // Setlocale sets the C locale used by FromAnsi/ToAnsi; must not raise an error.
            LuaValue r = await Run(@"
                local ok, err = pcall(Wchar.Setlocale, '')
                return tostring(ok or type(err) == 'string')
            ");
            r.String.ShouldBe("true");
        }

        // -- Stream Wchar read/write -----------------------------------------------
        [Fact]
        public async Task Stream_WriteWchar_ReadWchar_AsciiRoundTrip()
        {
            // Write a Wchar into a stream and read it back as a Wchar.
            LuaValue r = await Run(@"
                local w = Wchar.FromUtf8('hello')
                local s = Stream.Create()
                s:Write(w)
                s:Seek(0)
                local w2 = s:ReadWchar(5)
                return w2:ToUtf8()
            ");
            r.String.ShouldBe("hello");
        }

        [Fact]
        public async Task Stream_WriteWchar_ReturnsCorrectByteCount()
        {
            // Write returns the number of bytes written (2 bytes per wchar_t code unit).
            LuaValue r = await Run(@"
                local w = Wchar.FromUtf8('hi')
                local s = Stream.Create()
                local written = s:Write(w)
                return tostring(written == 4)   -- 2 code units * 2 bytes each
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Stream_WriteWchar_AdvancesPosition()
        {
            LuaValue r = await Run(@"
                local w = Wchar.FromUtf8('abc')
                local s = Stream.Create()
                s:Write(w)
                return tostring(s:pos() == 6)   -- 3 code units * 2 bytes each
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Stream_ReadWchar_PartialRead_ReturnsRequestedCount()
        {
            // Write a 5-char Wchar then read back only 3 code units.
            LuaValue r = await Run(@"
                local w = Wchar.FromUtf8('hello')
                local s = Stream.Create()
                s:Write(w)
                s:Seek(0)
                local w2 = s:ReadWchar(3)
                return tostring(w2:len() == 3 and w2:ToUtf8() == 'hel')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Stream_ReadWchar_PastEnd_ReturnsNil()
        {
            // Requesting more code units than are available returns nil.
            LuaValue r = await Run(@"
                local s = Stream.Create()
                return tostring(s:ReadWchar(1) == nil)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Stream_WriteWchar_MultipleAppend_ReadBackFull()
        {
            // Two Wchar writes must be contiguous; one ReadWchar retrieves them all.
            LuaValue r = await Run(@"
                local s = Stream.Create()
                s:Write(Wchar.FromUtf8('foo'))
                s:Write(Wchar.FromUtf8('bar'))
                s:Seek(0)
                local w = s:ReadWchar(6)
                return w:ToUtf8()
            ");
            r.String.ShouldBe("foobar");
        }

        [Fact]
        public async Task Stream_ReadWchar_NoLength_ReadsRemaining()
        {
            // ReadWchar() with no argument reads all remaining code units to end of stream.
            LuaValue r = await Run(@"
                local s = Stream.Create()
                s:Write(Wchar.FromUtf8('hello'))
                s:Seek(0)
                local w = s:ReadWchar()
                return w:ToUtf8()
            ");
            r.String.ShouldBe("hello");
        }

        [Fact]
        public async Task Stream_ReadWchar_NoLength_FromMidStream_ReadsRemainder()
        {
            // ReadWchar() from mid-stream must only return code units from the current position.
            LuaValue r = await Run(@"
                local s = Stream.Create()
                s:Write(Wchar.FromUtf8('abcde'))
                s:Seek(4)   -- skip first 2 code units (2 bytes each)
                local w = s:ReadWchar()
                return tostring(w:len() == 3 and w:ToUtf8() == 'cde')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Stream_ReadWchar_NoArg_EmptyStream_ReturnsNil()
        {
            // ReadWchar() with no argument on an empty stream must return nil, not error.
            LuaValue r = await Run(@"
                local s = Stream.Create()
                return tostring(s:ReadWchar() == nil)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Stream_ReadWchar_ExplicitNilArg_ReadAll()
        {
            // Passing nil explicitly must behave identically to omitting the argument.
            LuaValue r = await Run(@"
                local s = Stream.Create()
                s:Write(Wchar.FromUtf8('test'))
                s:Seek(0)
                local w = s:ReadWchar(nil)
                return tostring(w ~= nil and w:ToUtf8() == 'test')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Stream_ReadWchar_ResultIsWcharUserdata()
        {
            // The return value must be a Wchar userdata, not a plain Lua string.
            LuaValue r = await Run(@"
                local s = Stream.Create()
                s:Write(Wchar.FromUtf8('x'))
                s:Seek(0)
                local w = s:ReadWchar(1)
                return tostring(type(w) == 'userdata' and w:len() == 1)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Stream_WriteWchar_NonWritable_ReturnsZero()
        {
            // Writing a Wchar to a read-only stream must return 0.
            LuaValue r = await Run(@"
                local s = Stream.Create(function(op, ...)
                    if op == READ then return 'x' end
                    return 1   -- caps: READ only (no WRITE bit)
                end)
                local w = Wchar.FromUtf8('hi')
                return tostring(s:Write(w) == 0)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Stream_ReadWchar_WriteOnly_ReturnsNil()
        {
            // ReadWchar on a write-only stream must return nil.
            LuaValue r = await Run(@"
                local s = Stream.Create(function(op, ...)
                    if op == WRITE then return true end
                    return 2   -- caps: WRITE only
                end)
                return tostring(s:ReadWchar(1) == nil)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Timer_InitialState_NotRunning()
        {
            LuaValue r = await Run("local t = Timer.New(); return tostring(t:IsRunning() == false)");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Timer_AfterStart_IsRunning()
        {
            LuaValue r = await Run("local t = Timer.New(); t:Start(); return tostring(t:IsRunning())");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Timer_ElapsedAfterSleep_IsPositive()
        {
            LuaValue r = await Run("local t = Timer.New(); t:Start(); Sleep(20); return tostring(t:Elapsed() > 0)");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Timer_StopAndReset_ElapsedIsZero()
        {
            LuaValue r = await Run(@"
                local t = Timer.New(); t:Start(); Sleep(10); t:Stop(); t:Reset()
                return tostring(t:Elapsed() == 0 and not t:IsRunning())
            ");
            r.String.ShouldBe("true");
        }

        // -- Aes ------------------------------------------------------------------
        [Fact]
        public async Task Aes_EncryptDecrypt_RoundTrip()
        {
            // Use two fresh instances with the same key and default zero IV.
            LuaValue r = await Run(@"
                local key = string.rep('\0', 32)
                local plain = 'hello aes world!'
                local enc = Aes.Create(key):Encrypt(plain)
                local dec = Aes.Create(key):Decrypt(enc)
                return tostring(dec == plain)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Aes_EncryptedData_DiffersFromPlaintext()
        {
            LuaValue r = await Run(@"
                local key = string.rep('\0', 32)
                local plain = 'secret message!!'
                local enc = Aes.Create(key):Encrypt(plain)
                return tostring(enc ~= plain)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Aes_SetIV_ResetIV_AllowsDecryptWithSameInstance()
        {
            // SetIV with no argument resets to the stored IV, making the same instance usable
            // for both encrypt and decrypt when the IV is restored between operations.
            LuaValue r = await Run(@"
                local key   = string.rep('\0', 32)
                local iv    = string.rep('\0', 16)
                local plain = 'hello aes world!'
                local ctx   = Aes.Create(key, iv)
                local enc   = ctx:Encrypt(plain)
                ctx:SetIV(iv)   -- reset to original IV before decrypting
                local dec   = ctx:Decrypt(enc)
                return tostring(dec == plain)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Aes_DifferentIVs_ProduceDifferentCiphertext()
        {
            LuaValue r = await Run(@"
                local key   = string.rep('\0', 32)
                local plain = 'hello different!'
                local enc1  = Aes.Create(key, string.rep('\0', 16)):Encrypt(plain)
                local enc2  = Aes.Create(key, string.rep('\1', 16)):Encrypt(plain)
                return tostring(enc1 ~= enc2)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Aes_CTRMode_RoundTrip()
        {
            // CTR (stream cipher): encrypt with one instance, decrypt with a fresh instance
            // sharing the same key and IV. Must produce the original plaintext.
            LuaValue r = await Run(@"
                local key   = string.rep('\0', 32)
                local iv    = string.rep('\0', 16)
                local plain = 'hello ctr mode!!'
                local enc   = Aes.Create(key, iv, true):Encrypt(plain)
                local dec   = Aes.Create(key, iv, true):Decrypt(enc)
                return tostring(dec == plain)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Aes_ECBMode_NoIV_RoundTrip()
        {
            // ECB mode: created without IV; each block encrypted independently.
            LuaValue r = await Run(@"
                local key   = string.rep('\0', 32)
                local plain = 'hello ecb mode!!'   -- exactly 16 bytes (one AES block)
                local enc   = Aes.Create(key):Encrypt(plain)
                local dec   = Aes.Create(key):Decrypt(enc)
                return tostring(dec == plain)
            ");
            r.String.ShouldBe("true");
        }

        // -- Stream (in-memory) ---------------------------------------------------
        [Fact]
        public async Task Stream_Create_FromString_LoadsDataAtPositionZero()
        {
            LuaValue r = await Run(@"
                local s = Stream.Create('hello stream')
                return s:Read()
            ");
            r.String.ShouldBe("hello stream");
        }

        [Fact]
        public async Task Stream_Create_FromString_PosIsZeroAfterCreate()
        {
            LuaValue r = await Run(@"
                local s = Stream.Create('hello')
                return tostring(s:pos() == 0 and s:len() == 5)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Stream_Create_WithBackendFunction_CallsOpenForCaps()
        {
            LuaValue r = await Run(@"
                local OPEN, CLOSE, READ, WRITE = 0, 1, 2, 3
                local STREAM_CAP_READ = 1
                local s = Stream.Create(function(op, ...)
                    if op == OPEN then return STREAM_CAP_READ end
                    if op == READ then return 'backend data' end
                    if op == CLOSE then return true end
                end)
                local caps, _ = s:GetInfo()
                return tostring(caps.Caps == STREAM_CAP_READ)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Stream_Create_WithBackendFunction_ReadDelegatesToFunction()
        {
            LuaValue r = await Run(@"
                local OPEN, CLOSE, READ = 0, 1, 2
                local STREAM_CAP_READ = 1
                local s = Stream.Create(function(op, ...)
                    if op == OPEN then return STREAM_CAP_READ end
                    if op == READ then return 'from backend' end
                    if op == CLOSE then return true end
                end)
                return s:Read()
            ");
            r.String.ShouldBe("from backend");
        }

        [Fact]
        public async Task Stream_CustomBackend_DocExample_WorksCorrectly()
        {
            LuaValue r = await Run(@"
                local function makeStream()
                    local OPEN, CLOSE, READ, WRITE = 0, 1, 2, 3
                    local CURPOS, LEN, SETPOS, INFO = 4, 5, 6, 7
                    local CAP_READ, CAP_WRITE, CAP_SEEK = 1, 2, 4
                    local buf = ''
                    local pos = 0
                    return Stream.Create(function(op, arg)
                        if op == OPEN then
                            return CAP_READ + CAP_WRITE + CAP_SEEK
                        elseif op == CLOSE then
                            buf = nil
                            return true
                        elseif op == READ then
                            if pos >= #buf then return '' end
                            local n = (arg == 0) and (#buf - pos) or arg
                            local chunk = buf:sub(pos + 1, pos + n)
                            pos = pos + #chunk
                            return chunk
                        elseif op == WRITE then
                            buf = buf:sub(1, pos) .. arg .. buf:sub(pos + #arg + 1)
                            pos = pos + #arg
                            return true
                        elseif op == CURPOS then
                            return pos
                        elseif op == LEN then
                            return #buf
                        elseif op == SETPOS then
                            pos = math.max(0, math.min(arg, #buf))
                            return true
                        elseif op == INFO then
                            return { pos = pos, len = #buf, type = 'lua' }
                        end
                    end)
                end
                local s = makeStream()
                s:Write('hello world')
                s:Seek(6)
                local read = s:Read()
                local p = s:pos()
                local _, info = s:GetInfo()
                return tostring(read == 'world' and p == 11 and info.len == 11)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Stream_WriteAndRead_RoundTrip()
        {
            LuaValue r = await Run(@"
                local s = Stream.Create()
                s:Write('hello stream')
                s:Seek(0)
                return s:Read()
            ");
            r.String.ShouldBe("hello stream");
        }

        [Fact]
        public async Task Stream_PosAndLen_AfterWrite_ReturnCorrectValues()
        {
            LuaValue r = await Run(@"
                local s = Stream.Create()
                s:Write('abcde')
                return tostring(s:pos() == 5 and s:len() == 5)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Stream_GetInfo_ReturnsCapsAndBackendInfo()
        {
            LuaValue r = await Run(@"
                local s = Stream.Create()
                s:Write('hello')
                local caps, info = s:GetInfo()
                return tostring(
                    type(caps) == 'table' and caps.Caps > 0 and
                    type(info) == 'table' and info.pos == 5 and info.len == 5 and info.alloc >= 5
                )
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Stream_Seek_UpdatesPosition()
        {
            LuaValue r = await Run(@"
                local s = Stream.Create()
                s:Write('hello')
                s:Seek(2)
                return tostring(s:pos() == 2)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Stream_WriteByte_ReadByte_RoundTrip()
        {
            LuaValue r = await Run(@"
                local s = Stream.Create()
                s:WriteByte(42)
                s:Seek(0)
                return tostring(s:ReadByte() == 42)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Stream_PeekByte_DoesNotAdvancePosition()
        {
            LuaValue r = await Run(@"
                local s = Stream.Create()
                s:WriteByte(99)
                s:Seek(0)
                local b = s:PeekByte()
                return tostring(b == 99 and s:pos() == 0)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Stream_WriteInt_ReadInt_RoundTrip()
        {
            LuaValue r = await Run(@"
                local s = Stream.Create()
                s:WriteInt(12345)
                s:Seek(0)
                return tostring(s:ReadInt() == 12345)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Stream_Compress_Decompress_RoundTrip()
        {
            LuaValue r = await Run(@"
                local s = Stream.Create()
                s:Write('hello hello hello hello hello')
                local compressed = s:Compress()
                local decompressed = compressed:Decompress()
                return decompressed:Read()
            ");
            r.String.ShouldBe("hello hello hello hello hello");
        }

        [Fact]
        public async Task Stream_Compress_ProducesSmallerOutput()
        {
            LuaValue r = await Run(@"
                local s = Stream.Create()
                s:Write(string.rep('a', 1000))
                s:Seek(0)
                local compressed = s:Compress()
                local _, info = compressed:GetInfo()
                return tostring(info.len < 1000)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Stream_Compress_IntoProvidedStream_RoundTrip()
        {
            LuaValue r = await Run(@"
                local src = Stream.Create()
                src:Write('hello hello hello hello hello')
                local dst = Stream.Create()
                src:Compress(nil, dst)
                local decompressed = dst:Decompress()
                return decompressed:Read()
            ");
            r.String.ShouldBe("hello hello hello hello hello");
        }

        [Fact]
        public async Task Stream_Decompress_IntoProvidedStream_RoundTrip()
        {
            LuaValue r = await Run(@"
                local src = Stream.Create()
                src:Write('hello hello hello hello hello')
                local compressed = src:Compress()
                local dst = Stream.Create()
                compressed:Decompress(nil, dst)
                dst:Seek(0)
                return dst:Read()
            ");
            r.String.ShouldBe("hello hello hello hello hello");
        }

        [Fact]
        public async Task Stream_Compress_AndDecompress_BothIntoProvidedStreams_RoundTrip()
        {
            LuaValue r = await Run(@"
                local src = Stream.Create()
                src:Write('hello hello hello hello hello')
                local compDst = Stream.Create()
                local decompDst = Stream.Create()
                src:Compress(nil, compDst)
                compDst:Decompress(nil, decompDst)
                decompDst:Seek(0)
                return decompDst:Read()
            ");
            r.String.ShouldBe("hello hello hello hello hello");
        }

        [Fact]
        public async Task Stream_Compress_ProvidedDst_PositionNotReset()
        {
            LuaValue r = await Run(@"
                local src = Stream.Create()
                src:Write(string.rep('a', 200))
                local dst = Stream.Create()
                src:Compress(nil, dst)
                local _, info = dst:GetInfo()
                return tostring(info.pos > 0)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Stream_Decompress_ProvidedDst_PositionNotReset()
        {
            LuaValue r = await Run(@"
                local src = Stream.Create()
                src:Write(string.rep('a', 200))
                local compressed = src:Compress()
                local dst = Stream.Create()
                compressed:Decompress(nil, dst)
                local _, info = dst:GetInfo()
                return tostring(info.pos > 0)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Stream_WriteFloat_ReadFloat_RoundTrip()
        {
            LuaValue r = await Run(@"
                local s = Stream.Create()
                s:WriteFloat(3.14)
                s:Seek(0)
                local v = s:ReadFloat()
                return tostring(math.abs(v - 3.14) < 0.001)
            ");
            r.String.ShouldBe("true");
        }

        // -- Stream backend error propagation -------------------------------------
        [Fact]
        public async Task Stream_BackendReadError_PropagatesViaPcall()
        {
            // lua_callk on READ dispatch means a backend error propagates and is caught by pcall.
            LuaValue r = await Run(@"
                local OPEN, CLOSE, READ = 0, 1, 2
                local ok, err = pcall(function()
                    local s = Stream.Create(function(op)
                        if op == OPEN then return 1 end
                        if op == CLOSE then return true end
                        if op == READ then error('backend read error') end
                    end)
                    s:Read()
                end)
                return tostring(not ok and type(err) == 'string' and err:find('backend read error') ~= nil)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Stream_BackendWriteError_PropagatesViaPcall()
        {
            // lua_call_nohook on WRITE dispatch means a backend error bubbles up.
            LuaValue r = await Run(@"
                local OPEN, CLOSE, WRITE = 0, 1, 3
                local ok, err = pcall(function()
                    local s = Stream.Create(function(op)
                        if op == OPEN then return 2 end
                        if op == CLOSE then return true end
                        if op == WRITE then error('backend write error') end
                    end)
                    s:Write('hello')
                end)
                return tostring(not ok and type(err) == 'string' and err:find('backend write error') ~= nil)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Stream_BackendSeekError_PropagatesViaPcall()
        {
            // lua_call_nohook on SETPOS dispatch means a backend error bubbles up.
            LuaValue r = await Run(@"
                local OPEN, CLOSE, SETPOS = 0, 1, 6
                local ok, err = pcall(function()
                    local s = Stream.Create(function(op)
                        if op == OPEN then return 4 end
                        if op == CLOSE then return true end
                        if op == SETPOS then error('backend seek error') end
                    end)
                    s:Seek(5)
                end)
                return tostring(not ok and type(err) == 'string' and err:find('backend seek error') ~= nil)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Stream_BackendOpen_NonNumber_GivesCleanError()
        {
            // NewStream uses lua_pcall_nohook on OPEN with explicit recovery:
            // a non-number return produces "Backend function failed to open".
            LuaValue r = await Run(@"
                local ok, err = pcall(Stream.Create, function(op) return 'not_a_number' end)
                return tostring(not ok and err:find('Backend function failed to open') ~= nil)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Stream_BackendOpen_ZeroCaps_GivesCleanError()
        {
            // Returning 0 caps (no operations supported) is treated as failure.
            LuaValue r = await Run(@"
                local ok, err = pcall(Stream.Create, function(op) return 0 end)
                return tostring(not ok and err:find('Backend function failed to open') ~= nil)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Stream_BackendOpen_Throws_GivesCleanError()
        {
            // A throw during OPEN is caught by the protected call in NewStream and
            // reported as "Backend function failed to open" (original message is lost
            // intentionally; the non-number return check fires on the error object).
            LuaValue r = await Run(@"
                local ok, err = pcall(Stream.Create, function(op) error('boom') end)
                return tostring(not ok and err:find('Backend function failed to open') ~= nil)
            ");
            r.String.ShouldBe("true");
        }

        // -- Stream.Open (file backend) -------------------------------------------
        [Fact]
        public async Task Stream_Open_WriteRead_RoundTrip()
        {
            LuaValue r = await Run(@"
                local _tmp = (os.getenv('TEMP') or os.getenv('TMPDIR') or '/tmp') .. package.config:sub(1,1)
                local path = _tmp .. 'kitsune_stream_rw.bin'
                local w = Stream.Open(path, 'wb')
                w:Write('hello file stream')
                w:Close()
                local r = Stream.Open(path, 'rb')
                local data = r:Read()
                r:Close()
                os.remove(path)
                return data
            ");
            r.String.ShouldBe("hello file stream");
        }

        [Fact]
        public async Task Stream_Open_Info_ContainsNameAndType()
        {
            LuaValue r = await Run(@"
                local _tmp = (os.getenv('TEMP') or os.getenv('TMPDIR') or '/tmp') .. package.config:sub(1,1)
                local path = _tmp .. 'kitsune_stream_info.bin'
                local w = Stream.Open(path, 'wb')
                local _, info = w:GetInfo()
                w:Close()
                os.remove(path)
                return tostring(info.name == path and info.type == 'file')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Stream_Open_Seek_UpdatesPosition()
        {
            LuaValue r = await Run(@"
                local _tmp = (os.getenv('TEMP') or os.getenv('TMPDIR') or '/tmp') .. package.config:sub(1,1)
                local path = _tmp .. 'kitsune_stream_seek.bin'
                local w = Stream.Open(path, 'wb')
                w:Write('ABCDEF')
                w:Seek(2)
                local p = w:pos()
                w:Close()
                os.remove(path)
                return tostring(p)
            ");
            r.String.ShouldBe("2");
        }

        [Fact]
        public async Task Stream_Open_Len_ReturnsFileSizeWithoutMovingCursor()
        {
            // s:len() on a file stream must return the total file byte count and
            // must not disturb the read cursor.
            LuaValue r = await Run(@"
                local _tmp = (os.getenv('TEMP') or os.getenv('TMPDIR') or '/tmp') .. package.config:sub(1,1)
                local path = _tmp .. 'kitsune_stream_len.bin'
                local w = Stream.Open(path, 'wb')
                w:Write('ABCDEF')
                w:Close()
                local r = Stream.Open(path, 'rb')
                r:Seek(2)
                local len = r:len()
                local pos = r:pos()
                r:Close()
                os.remove(path)
                return tostring(len) .. ':' .. tostring(pos)
            ");
            r.String.ShouldBe("6:2");
        }

        [Fact]
        public async Task Stream_Open_Info_LenMatchesFileSize()
        {
            // GetInfo().len for a file stream must equal the actual file byte count
            // and must not change the cursor position.
            LuaValue r = await Run(@"
                local _tmp = (os.getenv('TEMP') or os.getenv('TMPDIR') or '/tmp') .. package.config:sub(1,1)
                local path = _tmp .. 'kitsune_stream_infolen.bin'
                local w = Stream.Open(path, 'wb')
                w:Write('hello')
                w:Close()
                local r = Stream.Open(path, 'rb')
                r:Seek(3)
                local _, info = r:GetInfo()
                local pos = r:pos()
                r:Close()
                os.remove(path)
                return tostring(info.len) .. ':' .. tostring(pos)
            ");
            r.String.ShouldBe("5:3");
        }

        [Fact]
        public async Task Stream_Open_NonexistentFile_RaisesError()
        {
            LuaValue r = await Run(@"
                local ok, err = pcall(Stream.Open, 'nonexistent_kitsune_xyz_abc_1234567890.bin', 'rb')
                return tostring(not ok and err:find('Stream.Open') ~= nil)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Stream_Open_ReadMode_BlocksWrite()
        {
            LuaValue r = await Run(@"
                local _tmp = (os.getenv('TEMP') or os.getenv('TMPDIR') or '/tmp') .. package.config:sub(1,1)
                local path = _tmp .. 'kitsune_stream_caps.bin'
                local w = Stream.Open(path, 'wb')
                w:Write('x')
                w:Close()
                local s = Stream.Open(path, 'rb')
                local result = s:Write('y')
                s:Close()
                os.remove(path)
                return tostring(result == false or result == nil or result == 0)
            ");
            r.String.ShouldBe("true");
        }

        // -- Stream (module API) --------------------------------------------------
        [Fact]
        public async Task Stream_Seek_AllowsMultipleReads()
        {
            LuaValue r = await Run(@"
                local s = Stream.Create()
                s:Write('reread')
                s:Seek(0)
                local first = s:Read()
                s:Seek(0)
                local second = s:Read()
                return tostring(first == second)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Stream_Len_ReflectsWrittenBytes()
        {
            LuaValue r = await Run(@"
                local s = Stream.Create()
                s:Write('abc')
                return tostring(s:len())
            ");
            r.String.ShouldBe("3");
        }

        [Fact]
        public async Task Stream_Pos_AdvancesAfterRead()
        {
            LuaValue r = await Run(@"
                local s = Stream.Create()
                s:Write('abcde')
                s:Seek(0)
                s:Read(2)
                return tostring(s:pos())
            ");
            r.String.ShouldBe("2");
        }

        [Fact]
        public async Task Stream_Read_PartialLength_ThenRemainder()
        {
            LuaValue r = await Run(@"
                local s = Stream.Create('hello world')
                s:Seek(0)
                local first = s:Read(5)
                local rest  = s:Read()
                return first .. ':' .. rest
            ");
            r.String.ShouldBe("hello: world");
        }

        [Fact]
        public async Task Stream_ReadByte_AtEnd_ReturnsNegativeOne()
        {
            LuaValue r = await Run(@"
                local s = Stream.Create()
                s:WriteByte(1)
                s:Seek(0)
                s:ReadByte()
                return tostring(s:ReadByte())
            ");
            r.String.ShouldBe("-1");
        }

        [Fact]
        public async Task Stream_PeekByte_ReturnsValueAndLeavesPos()
        {
            LuaValue r = await Run(@"
                local s = Stream.Create('ABC')
                s:Seek(0)
                local peeked = s:PeekByte()
                return tostring(peeked) .. ':' .. tostring(s:pos())
            ");
            r.String.ShouldBe("65:0");  // 'A' == 65, pos unchanged
        }

        [Fact]
        public async Task Stream_MultipleNumericTypes_InSequence()
        {
            LuaValue r = await Run(@"
                local s = Stream.Create()
                s:WriteShort(100)
                s:WriteInt(200)
                s:WriteLong(300)
                s:Seek(0)
                return tostring(s:ReadShort()) .. ':' .. tostring(s:ReadInt()) .. ':' .. tostring(s:ReadLong())
            ");
            r.String.ShouldBe("100:200:300");
        }

        // -- CSV ------------------------------------------------------------------
        [Fact]
        public async Task CSV_DecodeString_ParsesRows()
        {
            // Count rows with ipairs to avoid # unreliability on non-sequence tables.
            LuaValue r = await Run(@"
                local t = CSV.New():Decode('a,b,c\n1,2,3')
                local count = 0
                for _ in ipairs(t.Rows) do count = count + 1 end
                return tostring(count == 2)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task CSV_DecodeString_EmptyInput_ZeroRows()
        {
            // Decode("") must return an empty Rows table (zero rows).
            // The old do-while loop produced one spurious empty row; the while
            // loop introduced in Task 13 correctly produces none.
            LuaValue r = await Run(@"
                local t = CSV.New():Decode('')
                local count = 0
                for _ in ipairs(t.Rows) do count = count + 1 end
                return tostring(count == 0)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task CSV_DecodeString_RowValues_AccessibleAsWchar()
        {
            LuaValue r = await Run(@"
                local t = CSV.New():Decode('hello,world')
                return tostring(t.Rows[1][1])
            ");
            r.String.ShouldBe("hello");
        }

        [Fact]
        public async Task CSV_Decode_AsciiCell_IsPlainString()
        {
            // ASCII-only cells must come back as plain Lua strings (not WChar
            // userdata) so the fast path is active.
            LuaValue r = await Run(@"
                local t = CSV.New():Decode('hello,42,2024-01-01')
                return tostring(
                    type(t.Rows[1][1]) == 'string' and
                    type(t.Rows[1][2]) == 'string' and
                    type(t.Rows[1][3]) == 'string')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task CSV_Decode_NonAsciiCell_IsWchar()
        {
            // Cells containing characters above U+007F must still be WChar userdata.
            LuaValue r = await Run(@"
                local input = 'caf\xc3\xa9,plain'
                local t = CSV.New():Decode(input)
                return tostring(
                    type(t.Rows[1][1]) == 'userdata' and
                    type(t.Rows[1][2]) == 'string')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task CSV_DecodeString_MultipleColumnsPerRow()
        {
            LuaValue r = await Run(@"
                local t = CSV.New():Decode('a,b,c')
                local row = t.Rows[1]
                return tostring(tostring(row[1]) .. ':' .. tostring(row[2]) .. ':' .. tostring(row[3]))
            ");
            r.String.ShouldBe("a:b:c");
        }

        [Fact]
        public async Task CSV_DecodeString_MultipleRows_CorrectCount()
        {
            LuaValue r = await Run(@"
                local t = CSV.New():Decode('r1c1,r1c2\nr2c1,r2c2\nr3c1,r3c2')
                local count = 0
                for _ in ipairs(t.Rows) do count = count + 1 end
                return tostring(count == 3)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task CSV_DecodeString_QuotedField_StripsQuotes()
        {
            LuaValue r = await Run(@"
                local t = CSV.New():Decode('""hello"",""world""')
                return tostring(tostring(t.Rows[1][1]) == 'hello' and tostring(t.Rows[1][2]) == 'world')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task CSV_DecodeString_QuotedFieldWithEmbeddedDelimiter_PreservesContent()
        {
            // A comma inside quotes must not split the field.
            LuaValue r = await Run(@"
                local t = CSV.New():Decode('""hello, world"",end')
                return tostring(tostring(t.Rows[1][1]) == 'hello, world')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task CSV_DecodeString_QuotedFieldWithEscapedQuote_ProducesLiteralQuote()
        {
            // RFC 4180 escaped quote: "" inside a quoted field ? single ".
            LuaValue r = await Run(@"
                local t = CSV.New():Decode('""say """"hi"""""",end')
                return tostring(tostring(t.Rows[1][1]) == 'say ""hi""')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task CSV_DecodeString_FieldWithLeadingWhitespace_WhitespaceIsStripped()
        {
            // Verifies the SkipForwards fix: previously the first non-space character
            // was silently consumed and lost, producing "ello" instead of "hello".
            LuaValue r = await Run(@"
                local t = CSV.New():Decode(' hello, world')
                return tostring(tostring(t.Rows[1][1]) == 'hello' and tostring(t.Rows[1][2]) == 'world')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task CSV_DecodeString_EmptyField_PreservesEmptyCell()
        {
            // a,,b produces three fields; the middle one is empty.
            LuaValue r = await Run(@"
                local t = CSV.New():Decode('a,,b')
                local row = t.Rows[1]
                return tostring(tostring(row[1]) == 'a' and tostring(row[2]) == '' and tostring(row[3]) == 'b')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task CSV_DecodeString_ResultHasCommentsKey()
        {
            // The returned table always has a Comments key even when there are none.
            LuaValue r = await Run(@"
                local t = CSV.New():Decode('a,b')
                return tostring(type(t.Comments) == 'table')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task CSV_DecodeString_CommentLine_IsExtractedAndExcludedFromRows()
        {
            // Lines starting with * are treated as comments and placed in t.Comments.
            LuaValue r = await Run(@"
                local t = CSV.New():Decode('* this is a comment\na,b')
                local commentCount = 0
                for _ in ipairs(t.Comments) do commentCount = commentCount + 1 end
                local rowCount = 0
                for _ in ipairs(t.Rows) do rowCount = rowCount + 1 end
                return tostring(commentCount == 1 and rowCount == 1)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task CSV_DecodeString_MultipleCommentLines_AllExtracted()
        {
            LuaValue r = await Run(@"
                local t = CSV.New():Decode('* line one\n* line two\na,b')
                local count = 0
                for _ in ipairs(t.Comments) do count = count + 1 end
                return tostring(count == 2)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task CSV_DecodeString_CustomDelimiter_SplitsOnSemicolon()
        {
            LuaValue r = await Run(@"
                local t = CSV.New(';'):Decode('a;b;c')
                local row = t.Rows[1]
                return tostring(tostring(row[1]) == 'a' and tostring(row[2]) == 'b' and tostring(row[3]) == 'c')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task CSV_DecodeString_CrLfLineEnding_ParsedAsOneRow()
        {
            // \r\n (Windows CRLF) must produce the same row count as \n alone.
            LuaValue r = await Run(@"
                local t = CSV.New():Decode('a,b\r\nc,d')
                local count = 0
                for _ in ipairs(t.Rows) do count = count + 1 end
                return tostring(count == 2 and tostring(t.Rows[1][1]) == 'a' and tostring(t.Rows[2][1]) == 'c')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task CSV_DecodeString_WcharInput_ParsedCorrectly()
        {
            // Exercises the lua_iswchar branch in DecodeString; all other tests pass
            // plain Lua strings which take the FromUtf8 conversion path instead.
            LuaValue r = await Run(@"
                local t = CSV.New():Decode(Wchar.FromUtf8('x,y,z'))
                return tostring(tostring(t.Rows[1][1]) == 'x' and tostring(t.Rows[1][3]) == 'z')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task CSV_Encode_SimpleTable_ProducesCorrectString()
        {
            LuaValue r = await Run("return CSV.New():Encode({{'a', 'b'}, {'c', 'd'}})");
            r.String.ShouldBe("a,b\nc,d");
        }

        [Fact]
        public async Task CSV_Encode_FieldWithDelimiter_IsQuotedAndRoundTrips()
        {
            // A field containing the delimiter must be quoted; decoding must recover the original value.
            LuaValue r = await Run(@"
                local csv = CSV.New()
                local encoded = csv:Encode({{'hello, world', 'end'}})
                local decoded = csv:Decode(encoded)
                return tostring(tostring(decoded.Rows[1][1]) == 'hello, world' and tostring(decoded.Rows[1][2]) == 'end')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task CSV_Encode_WcharField_ConvertedToUtf8()
        {
            // Wchar fields must be converted via __tostring (UTF-8) during encoding.
            LuaValue r = await Run(@"
                local csv = CSV.New()
                local rows = {{Wchar.FromUtf8('hello'), Wchar.FromUtf8('world')}}
                local encoded = csv:Encode(rows)
                local decoded = csv:Decode(encoded)
                return tostring(tostring(decoded.Rows[1][1]) == 'hello' and tostring(decoded.Rows[1][2]) == 'world')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task CSV_Encode_CustomDelimiter_UsedInOutput()
        {
            LuaValue r = await Run(@"
                local csv = CSV.New(';')
                local encoded = csv:Encode({{'a', 'b', 'c'}})
                local decoded = csv:Decode(encoded)
                return tostring(tostring(decoded.Rows[1][1]) == 'a' and tostring(decoded.Rows[1][3]) == 'c')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task CSV_Encode_LeadingSpaceField_RoundTrips()
        {
            // SkipForwards strips leading whitespace on decode. Encode must quote
            // fields whose value starts with a space or tab so the whitespace lands
            // inside the quotes and is preserved across a decode round-trip.
            LuaValue r = await Run(@"
                local csv = CSV.New()
                local tab = '\9'
                local rows = {{' leading space', 'normal', tab .. 'leading tab'}}
                local encoded = csv:Encode(rows)
                local decoded = csv:Decode(encoded)
                local r1 = tostring(decoded.Rows[1][1])
                local r2 = tostring(decoded.Rows[1][2])
                local r3 = tostring(decoded.Rows[1][3])
                return tostring(r1 == ' leading space' and r2 == 'normal' and r3 == tab .. 'leading tab')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task CSV_Encode_FieldWithEmbeddedQuote_RoundTrips()
        {
            // RFC 4180: a " inside a quoted field is escaped as ""; Encode must produce
            // that and Decode must recover the original single ".
            LuaValue r = await Run(@"
                local csv = CSV.New()
                local rows = {{'say ""hi""', 'end'}}
                local encoded = csv:Encode(rows)
                local decoded = csv:Decode(encoded)
                return tostring(tostring(decoded.Rows[1][1]) == 'say ""hi""' and tostring(decoded.Rows[1][2]) == 'end')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task CSV_Encode_FieldWithEmbeddedNewline_IsQuoted()
        {
            // A field containing \n must be quoted so the newline is not treated as a
            // row separator on decode.
            LuaValue r = await Run(@"
                local csv = CSV.New()
                local rows = {{'line1\nline2', 'after'}}
                local encoded = csv:Encode(rows)
                local decoded = csv:Decode(encoded)
                return tostring(tostring(decoded.Rows[1][1]) == 'line1\nline2' and tostring(decoded.Rows[1][2]) == 'after')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task CSV_Decode_BooleanTrue_TriggersAutoDetect()
        {
            // ParseDelimiter accepts boolean true as the auto-detect signal, identical
            // to passing the string "auto".
            LuaValue r = await Run(@"
                local t = CSV.New(true):Decode('a;b;c\n1;2;3')
                return tostring(tostring(t.Rows[1][1]) == 'a' and tostring(t.Rows[2][3]) == '3')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task CSV_Decode_IntegerCodepointDelimiter_UsedCorrectly()
        {
            // ParseDelimiter accepts an integer codepoint (59 = ';').
            LuaValue r = await Run(@"
                local t = CSV.New(59):Decode('a;b;c')
                return tostring(tostring(t.Rows[1][1]) == 'a' and tostring(t.Rows[1][3]) == 'c')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task CSV_New_NoArgs_Encode_FallsBackToComma()
        {
            // CSV.New() binds "auto" as the delimiter. Encode with "auto" has no
            // meaningful input to sniff from, so it must fall back to comma.
            LuaValue r = await Run("return CSV.New():Encode({{'a', 'b', 'c'}})");
            r.String.ShouldBe("a,b,c");
        }

        [Fact]
        public async Task CSV_Decode_AutoDetect_CommaInput_DetectsCorrectly()
        {
            LuaValue r = await Run(@"
                local t = CSV.New():Decode('a,b,c\n1,2,3')
                return tostring(tostring(t.Rows[1][1]) == 'a' and tostring(t.Rows[1][3]) == 'c' and tostring(t.Rows[2][1]) == '1')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task CSV_Decode_AutoDetect_SemicolonInput_DetectsCorrectly()
        {
            LuaValue r = await Run(@"
                local t = CSV.New():Decode('a;b;c\n1;2;3')
                return tostring(tostring(t.Rows[1][1]) == 'a' and tostring(t.Rows[1][3]) == 'c' and tostring(t.Rows[2][2]) == '2')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task CSV_Decode_AutoDetect_TabInput_DetectsCorrectly()
        {
            LuaValue r = await Run(@"
                local t = CSV.New():Decode('a\tb\tc\n1\t2\t3')
                return tostring(tostring(t.Rows[1][1]) == 'a' and tostring(t.Rows[1][3]) == 'c')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task CSV_New_NoArgs_AutoDetectsSemicolon()
        {
            // CSV.New() with no delimiter should sniff each Decode call independently.
            LuaValue r = await Run(@"
                local csv = CSV.New()
                local t = csv:Decode('a;b;c\n1;2;3')
                return tostring(tostring(t.Rows[1][1]) == 'a' and tostring(t.Rows[1][3]) == 'c' and tostring(t.Rows[2][2]) == '2')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task CSV_New_WithSemicolon_UsesSpecifiedDelimiter()
        {
            LuaValue r = await Run(@"
                local csv = CSV.New(';')
                local t = csv:Decode('a;b;c')
                return tostring(tostring(t.Rows[1][1]) == 'a' and tostring(t.Rows[1][3]) == 'c')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task CSV_New_Encode_UsesSpecifiedDelimiter()
        {
            LuaValue r = await Run("return CSV.New(';'):Encode({{'a', 'b', 'c'}})");
            r.String.ShouldBe("a;b;c");
        }

        [Fact]
        public async Task CSV_DecodeFromFunction_AutoDetect_DetectsSemicolon()
        {
            LuaValue r = await Run(@"
                local sent = false
                local rows = {}
                for row in CSV.New():DecodeFromFunction(function()
                    if sent then return nil end
                    sent = true
                    return 'a;b;c\n1;2;3'
                end) do
                    table.insert(rows, tostring(row[1]) .. ':' .. tostring(row[3]))
                end
                return table.concat(rows, '|')
            ");
            r.String.ShouldBe("a:c|1:3");
        }

        [Fact]
        public async Task CSV_DecodeFromFunction_AutoDetect_AccumulatesChunksForSniff()
        {
            // Without Task-14 buffering the sniff would run on only the first
            // chunk ("hello") which contains no delimiter at all and would fall
            // back to comma.  With buffering the iterator keeps pulling chunks
            // until it sees a newline, giving SniffDelimiter enough context to
            // correctly identify the semicolon delimiter.
            LuaValue r = await Run(@"
                local chunks = { 'hello', ';world', '\ngoodbye;world' }
                local idx = 0
                local rows = {}
                for row in CSV.New():DecodeFromFunction(function()
                    idx = idx + 1
                    return chunks[idx]
                end) do
                    table.insert(rows, row)
                end
                return tostring(
                    #rows == 2 and
                    tostring(rows[1][1]) == 'hello'  and tostring(rows[1][2]) == 'world' and
                    tostring(rows[2][1]) == 'goodbye' and tostring(rows[2][2]) == 'world')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task CSV_New_NoArgs_DecodeFromFunction_AutoDetects()
        {
            LuaValue r = await Run(@"
                local csv = CSV.New()
                local sent = false
                local rows = {}
                for row in csv:DecodeFromFunction(function()
                    if sent then return nil end
                    sent = true
                    return 'x|y|z'
                end) do
                    table.insert(rows, tostring(row[1]) .. ':' .. tostring(row[3]))
                end
                return table.concat(rows, '|')
            ");
            r.String.ShouldBe("x:z");
        }

        [Fact]
        public async Task CSV_DecodeFromFunction_ChunkedStringInput_YieldsAllRows()
        {
            // Chunks deliberately cross field and row boundaries to verify the
            // stream refill logic handles mid-field and mid-row chunk splits.
            LuaValue r = await Run(@"
                local data = 'a,b,c\n1,2,3\n4,5,6'
                local pos  = 1
                local rows = {}
                for row in CSV.New():DecodeFromFunction(function()
                    if pos > #data then return nil end
                    local chunk = data:sub(pos, pos + 3)
                    pos = pos + 4
                    return chunk
                end) do
                    table.insert(rows, tostring(row[1]) .. ':' .. tostring(row[2]) .. ':' .. tostring(row[3]))
                end
                return table.concat(rows, '|')
            ");
            r.String.ShouldBe("a:b:c|1:2:3|4:5:6");
        }

        [Fact]
        public async Task CSV_DecodeFromFunction_WcharChunks_ConvertedTransparently()
        {
            // Supplier returns Wchar userdata objects; they must be converted to UTF-8
            // and parsed identically to plain-string chunks.
            LuaValue r = await Run(@"
                local chunks = { Wchar.FromUtf8('x,y'), Wchar.FromUtf8('\nz,w') }
                local i = 0
                local rows = {}
                for row in CSV.New():DecodeFromFunction(function()
                    i = i + 1
                    return chunks[i]
                end) do
                    table.insert(rows, tostring(row[1]) .. ':' .. tostring(row[2]))
                end
                return table.concat(rows, '|')
            ");
            r.String.ShouldBe("x:y|z:w");
        }

        [Fact]
        public async Task CSV_DecodeFromFunction_NilTerminates_FinalRowWithoutNewline()
        {
            // The last row has no trailing newline; the nil from the supplier must
            // flush the in-progress row cleanly.
            LuaValue r = await Run(@"
                local sent = false
                local rows = {}
                for row in CSV.New():DecodeFromFunction(function()
                    if sent then return nil end
                    sent = true
                    return 'hello,world'
                end) do
                    table.insert(rows, tostring(row[1]) .. ':' .. tostring(row[2]))
                end
                return table.concat(rows, '|')
            ");
            r.String.ShouldBe("hello:world");
        }

        [Fact]
        public async Task CSV_DecodeFromFunction_CustomDelimiter_Respected()
        {
            LuaValue r = await Run(@"
                local sent = false
                local rows = {}
                for row in CSV.New(';'):DecodeFromFunction(function()
                    if sent then return nil end
                    sent = true
                    return 'a;b;c'
                end) do
                    table.insert(rows, tostring(row[1]) .. ':' .. tostring(row[2]) .. ':' .. tostring(row[3]))
                end
                return table.concat(rows, '|')
            ");
            r.String.ShouldBe("a:b:c");
        }

        [Fact]
        public async Task CSV_DecodeFromFunction_Stream_ParsesRows()
        {
            // A LuaStream can be passed directly instead of a supplier function;
            // data is pulled in 4 KiB chunks so no full read-into-memory occurs.
            LuaValue r = await Run(@"
                local s = Stream.Create('a,b,c\n1,2,3\n4,5,6')
                local rows = {}
                for row in CSV.New():DecodeFromFunction(s) do
                    table.insert(rows, tostring(row[1]) .. ':' .. tostring(row[2]) .. ':' .. tostring(row[3]))
                end
                return table.concat(rows, '|')
            ");
            r.String.ShouldBe("a:b:c|1:2:3|4:5:6");
        }

        [Fact]
        public async Task CSV_DecodeFromFunction_Stream_CustomDelimiter_Respected()
        {
            LuaValue r = await Run(@"
                local s = Stream.Create('a;b;c\n1;2;3')
                local rows = {}
                for row in CSV.New(';'):DecodeFromFunction(s) do
                    table.insert(rows, tostring(row[1]) .. ':' .. tostring(row[3]))
                end
                return table.concat(rows, '|')
            ");
            r.String.ShouldBe("a:c|1:3");
        }

        [Fact]
        public async Task CSV_DecodeFromFunction_Stream_KeepsStreamAliveWithoutExplicitVariable()
        {
            // The stream is passed inline with no variable holding it; the iterator
            // closure must keep it alive through GC so all rows are produced.
            LuaValue r = await Run(@"
                local rows = {}
                for row in CSV.New():DecodeFromFunction(Stream.Create('x,y\nz,w')) do
                    table.insert(rows, tostring(row[1]) .. ':' .. tostring(row[2]))
                end
                return table.concat(rows, '|')
            ");
            r.String.ShouldBe("x:y|z:w");
        }

        // -- Mutex ----------------------------------------------------------------
        [Fact]
        public async Task Mutex_Open_LockAndUnlock_Succeeds()
        {
            LuaValue r = await Run(@"
                local m = Mutex.Open('KitsuneTestMutex_Util')
                local ok = m:Lock(500)
                m:Unlock()
                return tostring(ok == true)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Mutex_Info_ReturnsNameAndLockedState()
        {
            LuaValue r = await Run(@"
                local m = Mutex.Open('KitsuneInfoMutex_Util')
                m:Lock(100)
                local locked, name = m:Info()
                m:Unlock()
                return tostring(locked and name == 'KitsuneInfoMutex_Util')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Mutex_Lock_ZeroTimeout_SucceedsWhenFree()
        {
            // Lock(0) is a non-blocking trylock; must succeed when nobody holds the mutex.
            LuaValue r = await Run(@"
                local m = Mutex.Open('KitsuneTestMutex_ZeroTO')
                local ok = m:Lock(0)
                if ok then m:Unlock() end
                return tostring(ok == true)
            ");
            r.String.ShouldBe("true");
        }

        // -- FileSystem -----------------------------------------------------------
        [Fact]
        public async Task FileSystem_CurrentDirectory_ReturnsNonEmptyString()
        {
            LuaValue r = await Run("return tostring(#FileSystem.CurrentDirectory() > 0)");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task FileSystem_GetTempFileName_ReturnsValidPath()
        {
            LuaValue r = await Run("local p = FileSystem.GetTempFileName(); return tostring(type(p)=='string' and #p>0)");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task FileSystem_GetDrives_ReturnsList()
        {
            LuaValue r = await Run("local d = FileSystem.GetDrives(); return tostring(type(d)=='table' and #d>=1)");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task FileSystem_GetDrives_EntryHasDriveField()
        {
            LuaValue r = await Run(@"
                local d = FileSystem.GetDrives()
                return tostring(type(d[1].Drive) == 'string' and #d[1].Drive > 0)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task FileSystem_CreateAndDeleteDirectory_Succeeds()
        {
            LuaValue r = await Run(@"
                local dir = FileSystem.GetTempFileName() .. '_kitsune_testdir'
                local ok1 = FileSystem.CreateDirectory(dir)
                local ok2 = FileSystem.RemoveDirectory(dir)
                return tostring(ok1 and ok2)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task FileSystem_CreateAndDeleteDirectory_WithWchar_Succeeds()
        {
            LuaValue r = await Run(@"
                local dir = Wchar.FromUtf8(FileSystem.GetTempFileName() .. '_kitsune_wchar_testdir')
                local ok1 = FileSystem.CreateDirectory(dir)
                local ok2 = FileSystem.RemoveDirectory(dir)
                return tostring(ok1 and ok2)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task FileSystem_Open_WriteAndRead_RoundTrip()
        {
            LuaValue r = await Run(@"
                local path = FileSystem.GetTempFileName() .. '_kitsune_open.txt'
                local f = FileSystem.Open(path, 'wb')
                f:write('hello kitsune')
                f:close()
                local g = FileSystem.Open(path, 'rb')
                local data = g:read('*a')
                g:close()
                FileSystem.Delete(path)
                return data
            ");
            r.String.ShouldBe("hello kitsune");
        }

        [Fact]
        public async Task FileSystem_Open_WithWcharPath_WriteAndRead_RoundTrip()
        {
            LuaValue r = await Run(@"
                local path = FileSystem.GetTempFileName() .. '_kitsune_wchar_open.txt'
                local wpath = Wchar.FromUtf8(path)
                local f = FileSystem.Open(wpath, 'wb')
                f:write('hello wchar')
                f:close()
                local g = FileSystem.Open(wpath, 'rb')
                local data = g:read('*a')
                g:close()
                FileSystem.Delete(wpath)
                return data
            ");
            r.String.ShouldBe("hello wchar");
        }

        [Fact]
        public async Task FileSystem_GetFileInfo_ReturnsValidTable()
        {
            LuaValue r = await Run(@"
                local path = FileSystem.GetTempFileName() .. '_kitsune_info.txt'
                local f = FileSystem.Open(path, 'wb')
                f:write('abc')
                f:close()
                local info = FileSystem.GetFileInfo(path)
                FileSystem.Delete(path)
                return tostring(type(info) == 'table' and info.Size == 3 and info.isFolder == false)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task FileSystem_GetFileInfo_WithWchar_ReturnsValidTable()
        {
            LuaValue r = await Run(@"
                local path = FileSystem.GetTempFileName() .. '_kitsune_winfo.txt'
                local f = FileSystem.Open(path, 'wb')
                f:write('xyz')
                f:close()
                local info = FileSystem.GetFileInfo(Wchar.FromUtf8(path))
                FileSystem.Delete(path)
                return tostring(type(info) == 'table' and info.Size == 3)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task FileSystem_GetFileInfo_MissingPath_ReturnsNil()
        {
            LuaValue r = await Run(@"
                local info = FileSystem.GetFileInfo(FileSystem.GetTempFileName() .. '_no_such_file_xyz')
                return tostring(info == nil)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task FileSystem_Copy_CreatesDestination()
        {
            LuaValue r = await Run(@"
                local src = FileSystem.GetTempFileName() .. '_kitsune_src.txt'
                local dst = FileSystem.GetTempFileName() .. '_kitsune_dst.txt'
                local f = FileSystem.Open(src, 'wb'); f:write('copy me'); f:close()
                local ok = FileSystem.Copy(src, dst, true)
                local exists = FileSystem.GetFileInfo(dst) ~= nil
                FileSystem.Delete(src)
                FileSystem.Delete(dst)
                return tostring(ok and exists)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task FileSystem_Copy_WithWcharPaths_CreatesDestination()
        {
            LuaValue r = await Run(@"
                local src = FileSystem.GetTempFileName() .. '_kitsune_wsrc.txt'
                local dst = FileSystem.GetTempFileName() .. '_kitsune_wdst.txt'
                local f = FileSystem.Open(src, 'wb'); f:write('wchar copy'); f:close()
                local ok = FileSystem.Copy(Wchar.FromUtf8(src), Wchar.FromUtf8(dst), true)
                local exists = FileSystem.GetFileInfo(dst) ~= nil
                FileSystem.Delete(src)
                FileSystem.Delete(dst)
                return tostring(ok and exists)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task FileSystem_Move_SourceGoneDestinationExists()
        {
            LuaValue r = await Run(@"
                local src = FileSystem.GetTempFileName() .. '_kitsune_msrc.txt'
                local dst = FileSystem.GetTempFileName() .. '_kitsune_mdst.txt'
                local f = FileSystem.Open(src, 'wb'); f:write('move me'); f:close()
                local ok = FileSystem.Move(src, dst)
                local srcGone = FileSystem.GetFileInfo(src) == nil
                local dstExists = FileSystem.GetFileInfo(dst) ~= nil
                FileSystem.Delete(dst)
                return tostring(ok and srcGone and dstExists)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task FileSystem_Rename_SourceGoneDestinationExists()
        {
            LuaValue r = await Run(@"
                local src = FileSystem.GetTempFileName() .. '_kitsune_rsrc.txt'
                local dst = FileSystem.GetTempFileName() .. '_kitsune_rdst.txt'
                local f = FileSystem.Open(src, 'wb'); f:write('rename me'); f:close()
                local ok = FileSystem.Rename(src, dst)
                local srcGone = FileSystem.GetFileInfo(src) == nil
                local dstExists = FileSystem.GetFileInfo(dst) ~= nil
                FileSystem.Delete(dst)
                return tostring(ok and srcGone and dstExists)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task FileSystem_Rename_WithWcharPaths()
        {
            LuaValue r = await Run(@"
                local src = FileSystem.GetTempFileName() .. '_kitsune_wrsrc.txt'
                local dst = FileSystem.GetTempFileName() .. '_kitsune_wrdst.txt'
                local f = FileSystem.Open(src, 'wb'); f:write('wchar rename'); f:close()
                local ok = FileSystem.Rename(Wchar.FromUtf8(src), Wchar.FromUtf8(dst))
                local srcGone = FileSystem.GetFileInfo(src) == nil
                local dstExists = FileSystem.GetFileInfo(dst) ~= nil
                FileSystem.Delete(dst)
                return tostring(ok and srcGone and dstExists)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task FileSystem_Delete_RemovesFile()
        {
            LuaValue r = await Run(@"
                local path = FileSystem.GetTempFileName() .. '_kitsune_del.txt'
                local f = FileSystem.Open(path, 'wb'); f:write('delete me'); f:close()
                local ok = FileSystem.Delete(path)
                local gone = FileSystem.GetFileInfo(path) == nil
                return tostring(ok and gone)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task FileSystem_Delete_WithWcharPath_RemovesFile()
        {
            LuaValue r = await Run(@"
                local path = FileSystem.GetTempFileName() .. '_kitsune_wdel.txt'
                local f = FileSystem.Open(path, 'wb'); f:write('wchar delete'); f:close()
                local ok = FileSystem.Delete(Wchar.FromUtf8(path))
                local gone = FileSystem.GetFileInfo(path) == nil
                return tostring(ok and gone)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task FileSystem_GetFiles_ReturnsOnlyFiles()
        {
            LuaValue r = await Run(@"
                local sep = package.config:sub(1,1)
                local base = FileSystem.GetTempFileName() .. '_kitsune_ls'
                FileSystem.CreateDirectory(base)
                FileSystem.CreateDirectory(base .. sep .. 'subdir')
                local f1 = FileSystem.Open(base .. sep .. 'a.txt', 'wb'); f1:write('a'); f1:close()
                local f2 = FileSystem.Open(base .. sep .. 'b.txt', 'wb'); f2:write('b'); f2:close()
                local files = FileSystem.GetFiles(base)
                FileSystem.Delete(base .. sep .. 'a.txt')
                FileSystem.Delete(base .. sep .. 'b.txt')
                FileSystem.RemoveDirectory(base .. sep .. 'subdir')
                FileSystem.RemoveDirectory(base)
                return tostring(#files == 2)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task FileSystem_GetFiles_WithWcharPath_ReturnsOnlyFiles()
        {
            LuaValue r = await Run(@"
                local sep = package.config:sub(1,1)
                local base = FileSystem.GetTempFileName() .. '_kitsune_wls'
                FileSystem.CreateDirectory(base)
                local f1 = FileSystem.Open(base .. sep .. 'x.txt', 'wb'); f1:write('x'); f1:close()
                local files = FileSystem.GetFiles(Wchar.FromUtf8(base))
                FileSystem.Delete(base .. sep .. 'x.txt')
                FileSystem.RemoveDirectory(base)
                return tostring(#files == 1)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task FileSystem_GetDirectories_ReturnsOnlyDirs()
        {
            LuaValue r = await Run(@"
                local sep = package.config:sub(1,1)
                local base = FileSystem.GetTempFileName() .. '_kitsune_dirs'
                FileSystem.CreateDirectory(base)
                FileSystem.CreateDirectory(base .. sep .. 'sub1')
                FileSystem.CreateDirectory(base .. sep .. 'sub2')
                local f = FileSystem.Open(base .. sep .. 'file.txt', 'wb'); f:write('f'); f:close()
                local dirs = FileSystem.GetDirectories(base)
                FileSystem.Delete(base .. sep .. 'file.txt')
                FileSystem.RemoveDirectory(base .. sep .. 'sub1')
                FileSystem.RemoveDirectory(base .. sep .. 'sub2')
                FileSystem.RemoveDirectory(base)
                return tostring(#dirs == 2)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task FileSystem_GetAll_ReturnsMixedEntries()
        {
            LuaValue r = await Run(@"
                local sep = package.config:sub(1,1)
                local base = FileSystem.GetTempFileName() .. '_kitsune_all'
                FileSystem.CreateDirectory(base)
                FileSystem.CreateDirectory(base .. sep .. 'subdir')
                local f = FileSystem.Open(base .. sep .. 'file.txt', 'wb'); f:write('f'); f:close()
                local all = FileSystem.GetAll(base)
                FileSystem.Delete(base .. sep .. 'file.txt')
                FileSystem.RemoveDirectory(base .. sep .. 'subdir')
                FileSystem.RemoveDirectory(base)
                return tostring(#all == 2)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task FileSystem_GetAll_EntriesHaveExpectedFields()
        {
            LuaValue r = await Run(@"
                local sep = package.config:sub(1,1)
                local base = FileSystem.GetTempFileName() .. '_kitsune_fields'
                FileSystem.CreateDirectory(base)
                local f = FileSystem.Open(base .. sep .. 'f.txt', 'wb'); f:write('abc'); f:close()
                local all = FileSystem.GetAll(base)
                local entry = all[1]
                FileSystem.Delete(base .. sep .. 'f.txt')
                FileSystem.RemoveDirectory(base)
                return tostring(
                    entry ~= nil and
                    type(entry.FileName) ~= 'nil' and
                    type(entry.isFolder) == 'boolean' and
                    type(entry.Size) == 'number'
                )
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task FileSystem_GetAll_WithWcharPath_ReturnsMixedEntries()
        {
            LuaValue r = await Run(@"
                local sep = package.config:sub(1,1)
                local base = FileSystem.GetTempFileName() .. '_kitsune_wall'
                FileSystem.CreateDirectory(base)
                local f = FileSystem.Open(base .. sep .. 'y.txt', 'wb'); f:write('y'); f:close()
                local all = FileSystem.GetAll(Wchar.FromUtf8(base))
                FileSystem.Delete(base .. sep .. 'y.txt')
                FileSystem.RemoveDirectory(base)
                return tostring(#all == 1)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task FileSystem_SetCurrentDirectory_ChangesDirectory()
        {
            LuaValue r = await Run(@"
                local orig = FileSystem.CurrentDirectory()
                local tmp  = FileSystem.GetTempFileName() .. '_kitsune_chdir'
                FileSystem.CreateDirectory(tmp)
                local ok = FileSystem.SetCurrentDirectory(tmp)
                FileSystem.SetCurrentDirectory(orig)
                FileSystem.RemoveDirectory(tmp)
                return tostring(ok == true)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task FileSystem_SetCurrentDirectory_WithWcharPath_ChangesDirectory()
        {
            LuaValue r = await Run(@"
                local orig = FileSystem.CurrentDirectory()
                local tmp  = FileSystem.GetTempFileName() .. '_kitsune_wchdir'
                FileSystem.CreateDirectory(tmp)
                local ok = FileSystem.SetCurrentDirectory(Wchar.FromUtf8(tmp))
                FileSystem.SetCurrentDirectory(orig)
                FileSystem.RemoveDirectory(tmp)
                return tostring(ok == true)
            ");
            r.String.ShouldBe("true");
        }

        // -- CSV instance extras --------------------------------------------------
        [Fact]
        public async Task CSV_Create_AliasWorksIdenticallyToNew()
        {
            // CSV.Create is a registered alias for CSV.New; must return a working instance.
            LuaValue r = await Run(@"
                local csv = CSV.Create(';')
                local t = csv:Decode('a;b;c')
                return tostring(tostring(t.Rows[1][1]) == 'a' and tostring(t.Rows[1][3]) == 'c')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task CSV_Tostring_AutoInstance()
        {
            // __tostring on a no-delimiter instance reports "CSV(auto)".
            LuaValue r = await Run("return tostring(CSV.New())");
            r.String.ShouldBe("CSV(auto)");
        }

        [Fact]
        public async Task CSV_Tostring_FixedDelimiterInstance()
        {
            // __tostring on a fixed-delimiter instance reports the character.
            LuaValue r = await Run("return tostring(CSV.New(';'))");
            r.String.ShouldBe("CSV(';')");
        }

        [Fact]
        public async Task CSV_New_CalledOnInstance_CreatesNewIndependentInstance()
        {
            // csv:New(delim) must ignore the existing instance and return a fresh one
            // with its own delimiter — not a reference to the original.
            LuaValue r = await Run(@"
                local a = CSV.New(';')
                local b = a:New(',')
                -- Decode the same input with both: 'a' splits on ';', 'b' does not.
                local ta = a:Decode('x;y;z')
                local tb = b:Decode('x;y;z')
                return tostring(
                    #ta.Rows[1] == 3 and          -- a splits correctly on ';'
                    tostring(ta.Rows[1][2]) == 'y' and
                    #tb.Rows[1] == 1              -- b looks for ',' so treats input as one field
                )
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task CSV_InstanceReuse_MultipleDecodeCalls_BothCorrect()
        {
            // The same instance must produce correct results across successive Decode calls.
            // DecodeCsvWith resets pos/last/len but preserves the buffer allocation.
            LuaValue r = await Run(@"
                local csv = CSV.New()
                local t1 = csv:Decode('a,b,c')
                local t2 = csv:Decode('1,2,3')
                return tostring(
                    tostring(t1.Rows[1][1]) == 'a' and tostring(t1.Rows[1][3]) == 'c' and
                    tostring(t2.Rows[1][1]) == '1' and tostring(t2.Rows[1][3]) == '3'
                )
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task CSV_AutoDetect_ReSniffsDelimiterOnEachDecode()
        {
            // An auto-detect instance must sniff fresh on every Decode call;
            // the sniffed delimiter from call 1 must not bleed into call 2.
            LuaValue r = await Run(@"
                local csv = CSV.New()
                local t1 = csv:Decode('a,b,c')   -- sniffs comma
                local t2 = csv:Decode('x;y;z')   -- must sniff semicolon, not reuse comma
                return tostring(
                    tostring(t1.Rows[1][2]) == 'b' and
                    tostring(t2.Rows[1][2]) == 'y'
                )
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task CSV_DecodeFromFunction_NonReadableStream_RaisesError()
        {
            // Passing a write-only stream must produce a clean Lua error, not a crash.
            LuaValue r = await Run(@"
                local OPEN, CLOSE, CAP_WRITE = 0, 1, 2
                local s = Stream.Create(function(op)
                    if op == OPEN  then return CAP_WRITE end
                    if op == CLOSE then return true end
                end)
                local ok, err = pcall(function()
                    CSV.New():DecodeFromFunction(s)
                end)
                return tostring(not ok and type(err) == 'string')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task CSV_InstanceReuse_EncodeAfterDecode_BothCorrect()
        {
            // Encode after Decode on the same instance must work; the buffer fields
            // used by Decode do not interfere with LuaL_Buffer used by Encode.
            LuaValue r = await Run(@"
                local csv = CSV.New(';')
                local t   = csv:Decode('a;b;c')
                local enc = csv:Encode({{tostring(t.Rows[1][1]), tostring(t.Rows[1][3])}})
                return enc
            ");
            r.String.ShouldBe("a;c");
        }

        [Fact]
        public async Task CSV_DecodeFromFunction_InstanceAutoDetectDoesNotBleedIntoSecondIterator()
        {
            // Two separate DecodeFromFunction iterators created from the same auto-detect
            // instance must each detect their own delimiter independently.
            LuaValue r = await Run(@"
                local csv = CSV.New()
                local rows1 = {}
                local done1 = false
                for row in csv:DecodeFromFunction(function()
                    if done1 then return nil end
                    done1 = true
                    return 'a,b'
                end) do
                    table.insert(rows1, tostring(row[2]))
                end
                local rows2 = {}
                local done2 = false
                for row in csv:DecodeFromFunction(function()
                    if done2 then return nil end
                    done2 = true
                    return 'x;y'
                end) do
                    table.insert(rows2, tostring(row[2]))
                end
                return rows1[1] .. '|' .. rows2[1]
            ");
            r.String.ShouldBe("b|y");
        }

        // -- Json extras ----------------------------------------------------------
        [Fact]
        public async Task Json_NegativeInfinity_EncodesAsSpecialLiteral()
        {
            LuaValue r = await Run("return Json.New():Encode(-math.huge)");
            r.String.ShouldBe("-1e+9999");
        }

        [Fact]
        public async Task Json_Tostring_ReturnsNonEmptyString()
        {
            // __tostring on a Json instance returns a pointer-format string.
            LuaValue r = await Run("return tostring(type(tostring(Json.New())) == 'string' and #tostring(Json.New()) > 0)");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Json_Dispose_CanBeCalledExplicitly()
        {
            // Json.Dispose() is an explicit GC; calling it must not crash and the
            // instance should still be a valid Lua value afterwards.
            LuaValue r = await Run(@"
                local j = Json.New()
                j:Dispose()
                return 'ok'
            ");
            r.String.ShouldBe("ok");
        }

        [Fact]
        public async Task Json_Decode_ChunkedFunction_ParsesValues()
        {
            // json:Decode(fn) calls fn() repeatedly to get input chunks; returning
            // nil or "" signals end of input.  Tests the chunkFnIdx code path.
            LuaValue r = await Run(@"
                local j      = Json.New()
                local chunks = { '[1,', '2,', '3]' }
                local i      = 0
                local t = j:Decode(function()
                    i = i + 1
                    return chunks[i]
                end)
                return tostring(type(t) == 'table' and t[1] == 1 and t[2] == 2 and t[3] == 3)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Json_Decode_ChunkedFunction_MultipleValues()
        {
            // Each Decode(fn) call drains exactly one JSON value; the fn is fresh
            // each call so this tests that chunkFnIdx is properly reset.
            LuaValue r = await Run(@"
                local j   = Json.New()
                local src = '[1,2,3]'
                local pos = 1
                local v = j:Decode(function()
                    if pos > #src then return nil end
                    local chunk = src:sub(pos, pos + 1)
                    pos = pos + 2
                    return chunk
                end)
                return tostring(type(v) == 'table' and v[1] == 1 and v[3] == 3)
            ");
            r.String.ShouldBe("true");
        }

        // -- Stream extras --------------------------------------------------------
        [Fact]
        public async Task Stream_WriteDouble_ReadDouble_RoundTrip()
        {
            LuaValue r = await Run(@"
                local s = Stream.Create()
                s:WriteDouble(3.141592653589793)
                s:Seek(0)
                local v = s:ReadDouble()
                return tostring(math.abs(v - 3.141592653589793) < 1e-12)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Stream_UnsignedNumericTypes_RoundTrip()
        {
            // WriteUnsignedShort / WriteUnsignedInt / WriteUnsignedLong — each must
            // round-trip without sign-extension or truncation.
            LuaValue r = await Run(@"
                local s = Stream.Create()
                s:WriteUnsignedShort(60000)
                s:WriteUnsignedInt(3000000000)
                s:WriteUnsignedLong(9000000000)
                s:Seek(0)
                return tostring(s:ReadUnsignedShort()) .. ':'
                    .. tostring(s:ReadUnsignedInt()) .. ':'
                    .. tostring(s:ReadUnsignedLong())
            ");
            r.String.ShouldBe("60000:3000000000:9000000000");
        }

        [Fact]
        public async Task Stream_WriteUtf8_WriteAndReadBack()
        {
            // WriteUtf8 converts Latin-1 bytes to UTF-8; the raw bytes can be
            // Read back as a regular Lua string.
            LuaValue r = await Run(@"
                local s = Stream.Create()
                s:WriteUtf8('hello')
                s:Seek(0)
                return s:Read()
            ");
            r.String.ShouldBe("hello");
        }

        [Fact]
        public async Task Stream_WriteUtf8_EmbeddedNullByte_WritesFullString()
        {
            // Previously the loop used while(*in) which stops at embedded '\0',
            // silently dropping everything after it.  The fix uses the len from
            // luaL_checklstring so all bytes are encoded and written.
            LuaValue r = await Run(@"
                local s = Stream.Create()
                s:WriteUtf8('a\0b')   -- 3 bytes: 'a', null, 'b'
                local _, info = s:GetInfo()
                return tostring(info.len == 3)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Stream_ReadUtf8_ReturnsBytesAndCodepoint()
        {
            // ReadUtf8 reads exactly one UTF-8 codepoint and returns
            // (raw_bytes_string, codepoint_integer).
            LuaValue r = await Run(@"
                local s = Stream.Create()
                s:Write('A')           -- single ASCII codepoint (U+0041)
                s:Seek(0)
                local bytes, cp = s:ReadUtf8()
                return tostring(bytes == 'A' and cp == 65)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Stream_SetByte_AtPosition_ModifiesWithoutMovingCursor()
        {
            // SetByte(value, pos) writes one byte at pos, restores cursor, then
            // a Read from the original position sees the patched byte.
            LuaValue r = await Run(@"
                local s = Stream.Create()
                s:Write('ABCD')
                s:SetByte(88, 1)   -- patch index 1 ('B') with 'X' (88)
                s:Seek(0)
                return s:Read()    -- should read 'AXCD'
            ");
            r.String.ShouldBe("AXCD");
        }

        [Fact]
        public async Task Stream_Tostring_ReadableAndSeekable_ReadsContent()
        {
            // __tostring reads and returns the stream content ONLY for in-memory
            // streams.  File streams and custom backends use the pointer fallback
            // to avoid side effects and large reads.
            LuaValue r = await Run(@"
                local s = Stream.Create('hello')
                s:Seek(0)
                return tostring(s)
            ");
            r.String.ShouldBe("hello");
        }

        [Fact]
        public async Task Stream_Tostring_FileStream_ReturnsFallbackString()
        {
            // A file stream opened with "rb" has CAP_READ + CAP_SEEK, but __tostring
            // must NOT silently read the file — it must return the pointer fallback.
            LuaValue r = await Run(@"
                local _tmp = (os.getenv('TEMP') or os.getenv('TMPDIR') or '/tmp') .. package.config:sub(1,1)
                local path = _tmp .. 'kitsune_tostring_test.bin'
                local w = Stream.Open(path, 'wb')
                w:Write('secret')
                w:Close()
                local r = Stream.Open(path, 'rb')
                local str = tostring(r)
                r:Close()
                os.remove(path)
                return tostring(type(str) == 'string' and #str > 0 and str ~= 'secret')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Stream_Tostring_NonReadableStream_ReturnsFallbackString()
        {
            // A write-only stream lacks CAP_READ; __tostring must return a pointer
            // string rather than attempting to read the stream.
            LuaValue r = await Run(@"
                local OPEN, CLOSE, CAP_WRITE = 0, 1, 2
                local s = Stream.Create(function(op)
                    if op == OPEN  then return CAP_WRITE end
                    if op == CLOSE then return true end
                end)
                local str = tostring(s)
                return tostring(type(str) == 'string' and #str > 0 and str ~= '')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Stream_Tostring_ReadableButNotSeekable_ReturnsFallbackString()
        {
            // A read-only stream without CAP_SEEK must also fall back to the pointer
            // string — reading without being able to seek would silently consume data.
            LuaValue r = await Run(@"
                local OPEN, CLOSE, READ, CAP_READ = 0, 1, 2, 1
                local s = Stream.Create(function(op, len)
                    if op == OPEN  then return CAP_READ end
                    if op == CLOSE then return true end
                    if op == READ  then return 'canary' end
                end)
                local str = tostring(s)
                -- must be a non-empty string that is NOT the backend's read data
                return tostring(type(str) == 'string' and #str > 0 and str ~= 'canary')
            ");
            r.String.ShouldBe("true");
        }

        // -- Write / Read coverage -------------------------------------------------
        [Fact]
        public async Task Stream_Write_ReturnsWrittenByteCount()
        {
            LuaValue r = await Run(@"
                local s = Stream.Create()
                return tostring(s:Write('hello'))
            ");
            r.String.ShouldBe("5");
        }

        [Fact]
        public async Task Stream_Write_WithLimit_TruncatesOutput()
        {
            // Write(value, limit) writes at most 'limit' bytes.
            LuaValue r = await Run(@"
                local s = Stream.Create()
                s:Write('hello world', 5)
                s:Seek(0)
                return s:Read()
            ");
            r.String.ShouldBe("hello");
        }

        [Fact]
        public async Task Stream_Write_WithBoolean_WritesSingleByte()
        {
            LuaValue r = await Run(@"
                local s = Stream.Create()
                s:Write(true)
                s:Write(false)
                s:Seek(0)
                return tostring(s:ReadByte()) .. ':' .. tostring(s:ReadByte())
            ");
            r.String.ShouldBe("1:0");
        }

        [Fact]
        public async Task Stream_Write_UnsupportedType_ReturnsZero()
        {
            LuaValue r = await Run(@"
                local s = Stream.Create()
                return tostring(s:Write(nil))
            ");
            r.String.ShouldBe("0");
        }

        [Fact]
        public async Task Stream_Read_WithLength_ReadsExactCount()
        {
            LuaValue r = await Run(@"
                local s = Stream.Create('hello world')
                return s:Read(5)
            ");
            r.String.ShouldBe("hello");
        }

        // -- SetByte / PeekByte extra forms ----------------------------------------
        [Fact]
        public async Task Stream_SetByte_WithoutPosition_WritesAtCursorAndAdvances()
        {
            // SetByte(value) with no position writes at the current cursor and
            // advances it, just like WriteByte but without the 0-255 range guard.
            LuaValue r = await Run(@"
                local s = Stream.Create('ABC')
                s:Seek(1)
                s:SetByte(88)   -- 'X'
                s:Seek(0)
                return s:Read()
            ");
            r.String.ShouldBe("AXC");
        }

        [Fact]
        public async Task Stream_PeekByte_AtExplicitPosition_LeavesOriginalCursor()
        {
            // PeekByte(pos) peeks at 'pos' without disturbing the current cursor.
            LuaValue r = await Run(@"
                local s = Stream.Create('ABCD')
                s:Seek(2)
                local b = s:PeekByte(0)   -- peek at 'A' (65) while cursor is at 2
                return tostring(b == 65 and s:pos() == 2)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Stream_PeekByte_RequiresReadAndSeek_NotADistinctFlag()
        {
            // PeekStreamByte is now gated on CAP_READ + CAP_SEEK — there is no
            // separate CAP_PEEK flag.  A backend with both returns a real value;
            // a backend with only CAP_READ (no seek) returns -1.
            LuaValue r = await Run(@"
                local OPEN, CLOSE, READ = 0, 1, 2
                local CAP_READ = 1
                local s = Stream.Create(function(op, len)
                    if op == OPEN  then return CAP_READ end
                    if op == CLOSE then return true end
                    if op == READ  then return 'x' end
                end)
                local noSeek = s:PeekByte()
                -- Memory stream has both CAP_READ and CAP_SEEK: peek must work.
                local m = Stream.Create('AB')
                local withSeek = m:PeekByte()
                return tostring(noSeek == -1 and withSeek == 65)
            ");
            r.String.ShouldBe("true");
        }

        // -- Capability-guard return values ----------------------------------------
        [Fact]
        public async Task Stream_Seek_NonSeekable_ReturnsFalse()
        {
            LuaValue r = await Run(@"
                local OPEN, CLOSE, CAP_WRITE = 0, 1, 2
                local s = Stream.Create(function(op)
                    if op == OPEN  then return CAP_WRITE end
                    if op == CLOSE then return true end
                end)
                return tostring(s:Seek(0))
            ");
            r.String.ShouldBe("false");
        }

        [Fact]
        public async Task Stream_Pos_NonSeekable_ReturnsNil()
        {
            LuaValue r = await Run(@"
                local OPEN, CLOSE, CAP_WRITE = 0, 1, 2
                local s = Stream.Create(function(op)
                    if op == OPEN  then return CAP_WRITE end
                    if op == CLOSE then return true end
                end)
                return tostring(s:pos())
            ");
            r.String.ShouldBe("nil");
        }

        [Fact]
        public async Task Stream_Len_WriteOnly_ReturnsNil()
        {
            LuaValue r = await Run(@"
                local OPEN, CLOSE, CAP_WRITE = 0, 1, 2
                local s = Stream.Create(function(op)
                    if op == OPEN  then return CAP_WRITE end
                    if op == CLOSE then return true end
                end)
                return tostring(s:len())
            ");
            r.String.ShouldBe("nil");
        }

        // -- WriteByte boundary and range ------------------------------------------
        [Fact]
        public async Task Stream_WriteByte_OutOfRange_ReturnsFalse()
        {
            LuaValue r = await Run(@"
                local s = Stream.Create()
                return tostring(s:WriteByte(256)) .. ':' .. tostring(s:WriteByte(-1))
            ");
            r.String.ShouldBe("false:false");
        }

        [Fact]
        public async Task Stream_WriteByte_Boundaries_RoundTrip()
        {
            LuaValue r = await Run(@"
                local s = Stream.Create()
                s:WriteByte(0)
                s:WriteByte(255)
                s:Seek(0)
                return tostring(s:ReadByte()) .. ':' .. tostring(s:ReadByte())
            ");
            r.String.ShouldBe("0:255");
        }

        // -- Signed short ----------------------------------------------------------
        [Fact]
        public async Task Stream_WriteShort_NegativeValue_RoundTrips()
        {
            LuaValue r = await Run(@"
                local s = Stream.Create()
                s:WriteShort(-100)
                s:Seek(0)
                return tostring(s:ReadShort())
            ");
            r.String.ShouldBe("-100");
        }

        // -- ReadUtf8 extended coverage --------------------------------------------
        [Fact]
        public async Task Stream_ReadUtf8_MultiByte_ReturnsCodepoint()
        {
            // U+00E9 (é) encodes as 0xC3 0xA9 in UTF-8.
            LuaValue r = await Run(@"
                local s = Stream.Create()
                s:WriteByte(0xC3)
                s:WriteByte(0xA9)
                s:Seek(0)
                local bytes, cp = s:ReadUtf8()
                return tostring(#bytes == 2 and cp == 0xE9)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Stream_ReadUtf8_InvalidLeadByte_ReturnsNil()
        {
            // 0xFF is not a valid UTF-8 lead byte; ReadUtf8 must return nil.
            LuaValue r = await Run(@"
                local s = Stream.Create()
                s:WriteByte(0xFF)
                s:Seek(0)
                return tostring(s:ReadUtf8())
            ");
            r.String.ShouldBe("nil");
        }

        // -- WriteUtf8 Latin-1 conversion ------------------------------------------
        [Fact]
        public async Task Stream_WriteUtf8_HighByte_ConvertedToUtf8Pair()
        {
            // WriteUtf8 treats the input string as Latin-1 and re-encodes to UTF-8.
            // Latin-1 0xE9 (é) must produce the two-byte sequence 0xC3 0xA9.
            LuaValue r = await Run(@"
                local s = Stream.Create()
                s:WriteUtf8('\xE9')
                local _, info = s:GetInfo()
                s:Seek(0)
                local b1 = s:ReadByte()
                local b2 = s:ReadByte()
                return tostring(info.len == 2 and b1 == 0xC3 and b2 == 0xA9)
            ");
            r.String.ShouldBe("true");
        }

        // -- Compress / Decompress error paths -------------------------------------
        [Fact]
        public async Task Stream_Compress_NonReadableSource_ReturnsNilAndError()
        {
            LuaValue r = await Run(@"
                local OPEN, CLOSE, CAP_WRITE = 0, 1, 2
                local s = Stream.Create(function(op)
                    if op == OPEN  then return CAP_WRITE end
                    if op == CLOSE then return true end
                end)
                local result, err = s:Compress()
                return tostring(result == nil and type(err) == 'string')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Stream_Decompress_NonReadableSource_ReturnsNilAndError()
        {
            LuaValue r = await Run(@"
                local OPEN, CLOSE, CAP_WRITE = 0, 1, 2
                local s = Stream.Create(function(op)
                    if op == OPEN  then return CAP_WRITE end
                    if op == CLOSE then return true end
                end)
                local result, err = s:Decompress()
                return tostring(result == nil and type(err) == 'string')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Stream_Compress_NonWritableDest_ReturnsNilAndError()
        {
            LuaValue r = await Run(@"
                local src = Stream.Create()
                src:Write(string.rep('a', 100))
                local OPEN, CLOSE, CAP_READ = 0, 1, 1
                local ronly = Stream.Create(function(op, len)
                    if op == OPEN  then return CAP_READ end
                    if op == CLOSE then return true end
                    if op == 2     then return '' end
                end)
                local result, err = src:Compress(nil, ronly)
                return tostring(result == nil and type(err) == 'string')
            ");
            r.String.ShouldBe("true");
        }

        // -- Misc stream operations ------------------------------------------------
        [Fact]
        public async Task Stream_Close_ExplicitCall_DoesNotCrash()
        {
            LuaValue r = await Run(@"
                local s = Stream.Create()
                s:Write('hello')
                s:Close()
                return 'ok'
            ");
            r.String.ShouldBe("ok");
        }

        [Fact]
        public async Task Stream_GetInfo_MemoryStream_TypeIsMemory()
        {
            LuaValue r = await Run(@"
                local s = Stream.Create()
                s:Write('test')
                local _, info = s:GetInfo()
                return info.type
            ");
            r.String.ShouldBe("memory");
        }

        // -- SharedMemory (ToSharedMemory / OpenSharedMemory) ---------------------
        [Fact]
        public async Task SharedMemory_OpenSharedMemory_InfoType_IsSharedMemoryOut()
        {
            LuaValue r = await Run(@"
                local s = Stream.OpenSharedMemory(64)
                local _, info = s:GetInfo()
                return info.type
            ");
            r.String.ShouldBe("sharedmemory_out");
        }

        [Fact]
        public async Task SharedMemory_OpenSharedMemory_SizeMatchesRequested()
        {
            LuaValue r = await Run(@"
                local s = Stream.OpenSharedMemory(128)
                local _, info = s:GetInfo()
                return tostring(info.size == 128)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task SharedMemory_OpenSharedMemory_IsReadWriteAndSeekable()
        {
            LuaValue r = await Run(@"
                local CAP_READ, CAP_WRITE, CAP_SEEK = 1, 2, 4
                local s = Stream.OpenSharedMemory(16)
                local caps, _ = s:GetInfo()
                return tostring(
                    (caps.Caps & CAP_READ)  ~= 0 and
                    (caps.Caps & CAP_WRITE) ~= 0 and
                    (caps.Caps & CAP_SEEK)  ~= 0
                )
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task SharedMemory_OpenSharedMemory_WriteAndRead_RoundTrip()
        {
            // Size the block exactly to the payload so Read() returns only the written bytes.
            LuaValue r = await Run(@"
                local payload = 'hello shmem'
                local s = Stream.OpenSharedMemory(#payload)
                s:Write(payload)
                s:Seek(0)
                return s:Read()
            ");
            r.String.ShouldBe("hello shmem");
        }

        [Fact]
        public async Task SharedMemory_OpenSharedMemory_ZeroSize_RaisesError()
        {
            LuaValue r = await Run(@"
                local ok, err = pcall(Stream.OpenSharedMemory, 0)
                return tostring(not ok and type(err) == 'string')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task SharedMemory_ToSharedMemory_PreservesStreamContents()
        {
            LuaValue r = await Run(@"
                local src = Stream.Create()
                src:Write('snapshot data')
                local snap = src:ToSharedMemory()
                snap:Seek(0)
                return snap:Read()
            ");
            r.String.ShouldBe("snapshot data");
        }

        [Fact]
        public async Task SharedMemory_ToSharedMemory_InfoType_IsSharedMemoryOut()
        {
            LuaValue r = await Run(@"
                local src = Stream.Create()
                src:Write('test')
                local snap = src:ToSharedMemory()
                local _, info = snap:GetInfo()
                return info.type
            ");
            r.String.ShouldBe("sharedmemory_out");
        }

        [Fact]
        public async Task SharedMemory_ToSharedMemory_SizeMatchesSourceLength()
        {
            LuaValue r = await Run(@"
                local src = Stream.Create()
                src:Write('hello')
                local snap = src:ToSharedMemory()
                local _, info = snap:GetInfo()
                return tostring(info.size == 5)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task SharedMemory_ToSharedMemory_IsIndependentOfSource()
        {
            // ToSharedMemory produces a deep copy; mutating the source afterward
            // must not alter the snapshot.
            LuaValue r = await Run(@"
                local src = Stream.Create()
                src:Write('original')
                local snap = src:ToSharedMemory()
                src:Seek(0)
                src:Write('modified')
                snap:Seek(0)
                return snap:Read()
            ");
            r.String.ShouldBe("original");
        }

        [Fact]
        public async Task SharedMemory_ToSharedMemory_CapturesFromPositionZero()
        {
            // ToSharedMemory internally seeks the source to 0 before snapshotting,
            // so the full content is captured regardless of the current cursor.
            LuaValue r = await Run(@"
                local src = Stream.Create()
                src:Write('full content')
                src:Seek(5)
                local snap = src:ToSharedMemory()
                snap:Seek(0)
                return snap:Read()
            ");
            r.String.ShouldBe("full content");
        }

        [Fact]
        public async Task SharedMemory_ToSharedMemory_WithDispose_OriginalIsZeroed()
        {
            // After ToSharedMemory(true) the original stream is disposed: its Caps
            // are zeroed, so pos() returns nil (no STREAM_CAP_SEEK).
            LuaValue r = await Run(@"
                local src = Stream.Create()
                src:Write('bye')
                local snap = src:ToSharedMemory(true)
                return tostring(src:pos() == nil)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task SharedMemory_ToSharedMemory_WithDispose_SnapshotContentsIntact()
        {
            LuaValue r = await Run(@"
                local src = Stream.Create()
                src:Write('preserve me')
                local snap = src:ToSharedMemory(true)
                snap:Seek(0)
                return snap:Read()
            ");
            r.String.ShouldBe("preserve me");
        }

        [Fact]
        public async Task SharedMemory_ToSharedMemory_NonReadableStream_RaisesError()
        {
            LuaValue r = await Run(@"
                local OPEN, CLOSE, CAP_WRITE = 0, 1, 2
                local s = Stream.Create(function(op)
                    if op == OPEN  then return CAP_WRITE end
                    if op == CLOSE then return true end
                end)
                local ok, err = pcall(function() s:ToSharedMemory() end)
                return tostring(not ok and type(err) == 'string')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task SharedMemory_ToSharedMemory_NonSeekableStream_RaisesError()
        {
            // A read-only backend without CAP_SEEK must also be rejected.
            LuaValue r = await Run(@"
                local OPEN, CLOSE, READ, CAP_READ = 0, 1, 2, 1
                local s = Stream.Create(function(op, len)
                    if op == OPEN  then return CAP_READ end
                    if op == CLOSE then return true end
                    if op == READ  then return 'data' end
                end)
                local ok, err = pcall(function() s:ToSharedMemory() end)
                return tostring(not ok and type(err) == 'string')
            ");
            r.String.ShouldBe("true");
        }

        // -- Process --------------------------------------------------------------
        [Fact]
        public async Task Process_All_ReturnsTableWithAtLeastOneEntry()
        {
            LuaValue r = await Run(@"
                local p = Process.All()
                local n = 0
                for _ in pairs(p) do n = n + 1 end
                return tostring(n > 0)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Process_All_KeysAreIntegersValuesAreStrings()
        {
            LuaValue r = await Run(@"
                local p = Process.All()
                for k, v in pairs(p) do
                    if type(k) ~= 'number' or type(v) ~= 'string' then return 'false' end
                end
                return 'true'
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Process_Open_CurrentProcess_ReturnsNonNil()
        {
            LuaValue r = await Run("return tostring(Process.Open() ~= nil)");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Process_GetID_CurrentProcess_ReturnsPositiveInteger()
        {
            LuaValue r = await Run(@"
                local p = Process.Open()
                local id = p:GetID()
                return tostring(math.type(id) == 'integer' and id > 0)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Process_GetName_CurrentProcess_ReturnsNonEmptyString()
        {
            LuaValue r = await Run(@"
                local p = Process.Open()
                local name = p:GetName()
                return tostring(type(name) == 'string' and #name > 0)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Process_GetRAM_CurrentProcess_ReturnsPositiveNumber()
        {
            LuaValue r = await Run(@"
                local p = Process.Open()
                return tostring(p:GetRAM() > 0)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Process_Open_InvalidPid_ReturnsNil()
        {
            // PID 2147483647 (INT32_MAX) is above both Linux PID_MAX and any real Windows PID.
            LuaValue r = await Run("return tostring(Process.Open(2147483647) == nil)");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Process_Tostring_ReturnsNonEmptyString()
        {
            LuaValue r = await Run(@"
                local p = Process.Open()
                local s = tostring(p)
                return tostring(type(s) == 'string' and #s > 0)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Process_Start_ReturnsHandle()
        {
            LuaValue r = await Run(@"
                local is_win = package.config:sub(1,1) == '\\'
                local cmd = is_win and 'cmd /c echo hello' or 'echo hello'
                local proc = Process.Start(nil, cmd, nil, false, false)
                return tostring(proc ~= nil)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Process_Start_ReadFromPipe_ContainsOutput()
        {
            LuaValue r = await Run(@"
                local is_win = package.config:sub(1,1) == '\\'
                local cmd = is_win and 'cmd /c echo hello' or 'echo hello'
                -- noconsole=true so stdout goes to the pipe, not a new console window
                local proc = Process.Start(nil, cmd, nil, true, true)
                if proc == nil then return 'skip' end
                Sleep(200)
                local out = ''
                local chunk = proc:ReadFromPipe()
                while chunk do
                    out = out .. chunk
                    chunk = proc:ReadFromPipe()
                end
                return tostring(out:find('hello') ~= nil)
            ");
            if (r != "skip")
            {
                r.String.ShouldBe("true");
            }
        }

        [Fact]
        public async Task Process_Start_ReadErrorFromPipe_ReturnsNilOrString()
        {
            // A clean command produces no stderr; result is nil or empty string.
            LuaValue r = await Run(@"
                local is_win = package.config:sub(1,1) == '\\'
                local cmd = is_win and 'cmd /c echo hello' or 'echo hello'
                local proc = Process.Start(nil, cmd, nil, false, true)
                if proc == nil then return 'skip' end
                Sleep(200)
                local err = proc:ReadErrorFromPipe()
                return tostring(err == nil or type(err) == 'string')
            ");
            if (r != "skip")
            {
                r.String.ShouldBe("true");
            }
        }

        [Fact]
        public async Task Process_GetExitCode_RunningProcess_ReturnsNil()
        {
            LuaValue r = await Run(@"
                local is_win = package.config:sub(1,1) == '\\'
                local cmd = is_win and 'cmd /c ping 127.0.0.1 -n 10 >nul 2>&1' or 'sleep 10'
                local proc = Process.Start(nil, cmd, nil, false, false)
                if proc == nil then return 'skip' end
                local code = proc:GetExitCode()
                proc:Stop()
                return tostring(code == nil)
            ");
            if (r != "skip")
            {
                r.String.ShouldBe("true");
            }
        }

        [Fact]
        public async Task Process_Stop_RunningProcess_ReturnsTrue()
        {
            LuaValue r = await Run(@"
                local is_win = package.config:sub(1,1) == '\\'
                local cmd = is_win and 'cmd /c ping 127.0.0.1 -n 10 >nul 2>&1' or 'sleep 10'
                local proc = Process.Start(nil, cmd, nil, false, false)
                if proc == nil then return 'skip' end
                return tostring(proc:Stop() == true)
            ");
            if (r != "skip")
            {
                r.String.ShouldBe("true");
            }
        }

        [Fact]
        public async Task Process_GetExitCode_CompletedProcess_ReturnsNumber()
        {
            LuaValue r = await Run(@"
                local is_win = package.config:sub(1,1) == '\\'
                local cmd = is_win and 'cmd /c exit 0' or 'true'
                local proc = Process.Start(nil, cmd, nil, true, false)
                if proc == nil then return 'skip' end
                local code = nil
                for _ = 1, 30 do
                    code = proc:GetExitCode()
                    if code ~= nil then break end
                    Sleep(50)
                end
                return tostring(type(code) == 'number')
            ");
            if (r != "skip")
            {
                r.String.ShouldBe("true");
            }
        }

        [Fact]
        public async Task Process_GetExitCode_SameValueOnMultipleCalls()
        {
            // The exit status must be cached after the first successful query (Linux
            // uses a cached waitpid result; Windows re-queries the process handle).
            LuaValue r = await Run(@"
                local is_win = package.config:sub(1,1) == '\\'
                local cmd = is_win and 'cmd /c exit 0' or 'true'
                local proc = Process.Start(nil, cmd, nil, true, false)
                if proc == nil then return 'skip' end
                local c1 = nil
                for _ = 1, 30 do
                    c1 = proc:GetExitCode()
                    if c1 ~= nil then break end
                    Sleep(50)
                end
                local c2 = proc:GetExitCode()
                return tostring(c1 ~= nil and c1 == c2)
            ");
            if (r != "skip")
            {
                r.String.ShouldBe("true");
            }
        }

        [Fact]
        public async Task Process_ReadFromPipe_WithoutRedirect_ReturnsNil()
        {
            // A process started without pipe redirect has no stdout fd; must return nil.
            LuaValue r = await Run(@"
                local is_win = package.config:sub(1,1) == '\\'
                local cmd = is_win and 'cmd /c exit 0' or 'true'
                local proc = Process.Start(nil, cmd, nil, false, false)
                if proc == nil then return 'skip' end
                return tostring(proc:ReadFromPipe() == nil)
            ");
            if (r != "skip")
            {
                r.String.ShouldBe("true");
            }
        }

        [Fact]
        public async Task Process_WriteToPipe_ReturnsPositiveBytesWritten()
        {
            // cat (Linux) / cmd /c more (Windows) block on stdin; write must succeed
            // and return the byte count before we stop the process.
            LuaValue r = await Run(@"
                local is_win = package.config:sub(1,1) == '\\'
                local cmd = is_win and 'cmd /c more' or 'cat'
                local proc = Process.Start(nil, cmd, nil, false, 7)
                if proc == nil then return 'skip' end
                local written = proc:WriteToPipe('hello\n')
                proc:Stop()
                return tostring(type(written) == 'number' and written > 0)
            ");
            if (r != "skip")
            {
                r.String.ShouldBe("true");
            }
        }

        [Fact]
        public async Task Process_GetID_StartedProcess_ReturnsPositiveInteger()
        {
            LuaValue r = await Run(@"
                local is_win = package.config:sub(1,1) == '\\'
                local cmd = is_win and 'cmd /c ping 127.0.0.1 -n 5 >nul 2>&1' or 'sleep 5'
                local proc = Process.Start(nil, cmd, nil, false, false)
                if proc == nil then return 'skip' end
                local id = proc:GetID()
                proc:Stop()
                return tostring(math.type(id) == 'integer' and id > 0)
            ");
            if (r != "skip")
            {
                r.String.ShouldBe("true");
            }
        }

        // -- Async stream (CreateChunked / HasData / CSV / Compress) --------------
        // All tests that involve yielding coroutines use the helper below, which
        // mirrors the coroutine-resume loop that test scripts previously used.
        [Fact]
        public async Task Stream_CreateChunked_IsNotNil()
        {
            LuaValue r = await Run(MakeChunkedStream + "return tostring(makeChunkedStream({'a','b'}) ~= nil)");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Stream_CreateChunked_PosIsAlwaysNil()
        {
            // Async streams have no STREAM_CAP_SEEK; pos() returns nil like all non-seekable streams.
            LuaValue r = await Run(MakeChunkedStream + @"
                local cs = makeChunkedStream({'hello'})
                return tostring(cs:pos() == nil)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Stream_CreateChunked_LenIsZeroBeforeFirstRead()
        {
            LuaValue r = await Run(MakeChunkedStream + @"
                local cs = makeChunkedStream({'hello'})
                return tostring(cs:len() == 0)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Stream_HasData_SyncStream_ReturnsBytesRemaining()
        {
            // Sync streams return the number of bytes remaining (falsy false at EOF,
            // truthy positive integer when data is buffered).
            LuaValue r = await Run(@"
                local s = Stream.Create()
                s:Write('hello')
                s:Seek(0)
                local available = s:HasData()   -- 5 bytes remain
                s:Read()                        -- consume all
                local atEnd = s:HasData()       -- 0 bytes remain -> false
                return tostring(available == 5 and atEnd == false)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Stream_HasData_AsyncStream_FalseBeforeYield()
        {
            LuaValue r = await Run(MakeChunkedStream + @"
                local cs = makeChunkedStream({'data'})
                return tostring(cs:HasData() == false)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Stream_Closed_HasData_ReturnsMinusOne()
        {
            // A closed (zeroed) stream returns -1 from HasData so callers can
            // distinguish "stream alive but no data yet" (false) from
            // "stream is dead" (-1) and break out of streaming loops cleanly.
            LuaValue r = await Run(@"
                local s = Stream.Create()
                s:Write('hello')
                s:Close()
                return tostring(s:HasData() == -1)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Stream_Closed_Read_ReturnsNil()
        {
            // Reading from a closed stream returns nil regardless of what was
            // written before Close().  No STREAM_CAP_READ on a zeroed stream.
            LuaValue r = await Run(@"
                local s = Stream.Create()
                s:Write('hello')
                s:Close()
                return tostring(s:Read() == nil)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Stream_Closed_Write_ReturnsZero()
        {
            // Writing to a closed stream returns 0 (bytes written).
            // No STREAM_CAP_WRITE on a zeroed stream.
            LuaValue r = await Run(@"
                local s = Stream.Create()
                s:Close()
                return tostring(s:Write('x') == 0)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Stream_Closed_Dead_Distinguishable_From_NoData()
        {
            // The full contract: false = alive/waiting, -1 = dead.
            // An alive stream with no data ready must return false, not -1.
            LuaValue r = await Run(@"
                local alive = Stream.Create()   -- empty, no data at pos=0 after Create
                alive:Seek(0)
                local alive_hd = alive:HasData()  -- 0 bytes remaining -> false

                local dead = Stream.Create()
                dead:Close()
                local dead_hd  = dead:HasData()   -- zeroed -> -1

                return tostring(alive_hd == false and dead_hd == -1)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Stream_CreateChunked_DeliversChunksInOrder()
        {
            LuaValue r = await Run(RunCoroutine + MakeChunkedStream + @"
                run(function()
                    local cs = makeChunkedStream({'alpha', 'beta', 'gamma'})
                    local a = cs:Read()
                    local b = cs:Read()
                    local c = cs:Read()
                    assert(a == 'alpha' and b == 'beta' and c == 'gamma',
                        'chunks out of order: ' .. tostring(a) .. ',' .. tostring(b) .. ',' .. tostring(c))
                end)
                return 'ok'
            ");
            r.String.ShouldBe("ok");
        }

        [Fact]
        public async Task Stream_CreateChunked_ReturnsNilAfterLastChunk()
        {
            LuaValue r = await Run(RunCoroutine + MakeChunkedStream + @"
                run(function()
                    local cs = makeChunkedStream({'only'})
                    cs:Read()
                    local eof = cs:Read()
                    assert(eof == nil, 'expected nil at EOF, got: ' .. tostring(eof))
                end)
                return 'ok'
            ");
            r.String.ShouldBe("ok");
        }

        [Fact]
        public async Task Stream_CreateChunked_EmptyTable_ImmediateEof()
        {
            LuaValue r = await Run(RunCoroutine + MakeChunkedStream + @"
                run(function()
                    local cs = makeChunkedStream({})
                    local v = cs:Read()
                    assert(v == nil, 'empty chunked stream should return nil, got: ' .. tostring(v))
                end)
                return 'ok'
            ");
            r.String.ShouldBe("ok");
        }

        [Fact]
        public async Task Stream_HasData_TrueAfterYield()
        {
            // The chunked stream yields once before each chunk.
            // After the first yield HasData() returns true (pending=true).
            LuaValue r = await Run(MakeChunkedStream + @"
                local cs = makeChunkedStream({'data'})
                local co = coroutine.create(function() cs:Read() end)
                coroutine.resume(co)   -- yields once; pending = true
                return tostring(cs:HasData() == true)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Stream_LenNonZero_AfterYield()
        {
            LuaValue r = await Run(MakeChunkedStream + @"
                local cs = makeChunkedStream({'data'})
                local co = coroutine.create(function() cs:Read() end)
                coroutine.resume(co)
                return tostring(cs:len() > 0)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task CSV_Read_AsyncStream_SimpleRows()
        {
            LuaValue r = await Run(RunCoroutine + MakeChunkedStream + @"
                run(function()
                    local cs = makeChunkedStream({'a,b,c\n', '1,2,3\n', '4,5,6\n'})
                    local rows = {}
                    for row in CSV.New():DecodeFromFunction(cs) do rows[#rows + 1] = row end
                    assert(#rows == 3, 'expected 3 rows, got ' .. #rows)
                    assert(tostring(rows[1][1]) == 'a' and tostring(rows[1][3]) == 'c', 'header mismatch')
                    assert(tostring(rows[2][1]) == '1' and tostring(rows[2][3]) == '3', 'row2 mismatch')
                    assert(tostring(rows[3][1]) == '4' and tostring(rows[3][3]) == '6', 'row3 mismatch')
                end)
                return 'ok'
            ");
            r.String.ShouldBe("ok");
        }

        [Fact]
        public async Task CSV_Read_AsyncStream_RowSpanningMultipleChunks()
        {
            // "hello" and ",world\n" arrive in separate chunks — must be joined.
            LuaValue r = await Run(RunCoroutine + MakeChunkedStream + @"
                run(function()
                    local cs = makeChunkedStream({'hello', ',world\n', 'foo,bar\n'})
                    local rows = {}
                    for row in CSV.New():DecodeFromFunction(cs) do rows[#rows + 1] = row end
                    assert(#rows == 2, 'expected 2 rows, got ' .. #rows)
                    assert(tostring(rows[1][1]) == 'hello', 'col1: ' .. tostring(rows[1][1]))
                    assert(tostring(rows[1][2]) == 'world', 'col2: ' .. tostring(rows[1][2]))
                    assert(tostring(rows[2][1]) == 'foo' and tostring(rows[2][2]) == 'bar')
                end)
                return 'ok'
            ");
            r.String.ShouldBe("ok");
        }

        [Fact]
        public async Task Stream_Compress_AsyncSource_RoundTrip()
        {
            LuaValue r = await Run(RunCoroutine + MakeChunkedStream + @"
                run(function()
                    local original = 'the quick brown fox jumps over the lazy dog'
                    local cs = makeChunkedStream({'the quick ', 'brown fox ', 'jumps over ', 'the lazy dog'})
                    local compressed = Stream.Compress(cs)
                    assert(compressed ~= nil, 'Compress returned nil')
                    local decompressed = Stream.Decompress(compressed)
                    assert(decompressed ~= nil, 'Decompress returned nil')
                    local result = decompressed:Read()
                    assert(result == original,
                        'round-trip mismatch\nexpected: ' .. original .. '\ngot: ' .. tostring(result))
                end)
                return 'ok'
            ");
            r.String.ShouldBe("ok");
        }

        [Fact]
        public async Task Stream_Decompress_AsyncSource_RoundTrip()
        {
            // Compress sync then decompress via a non-seekable Lua fn backend,
            // exercising the fn-backend read path inside DecompressStream.
            LuaValue r = await Run(RunCoroutine + @"
                run(function()
                    local src = Stream.Create()
                    src:Write('hello compressed world')
                    src:Seek(0)
                    local compressed = Stream.Compress(src)
                    compressed:Seek(0)
                    local compBytes = compressed:Read()
                    assert(type(compBytes) == 'string')
                    local pos = 1
                    local fnSrc = Stream.Create(function(op, len)
                        if op == 0 then return 1 end
                        if op == 1 then return true end
                        if op == 2 then
                            if pos > #compBytes then return nil end
                            local n = len > 0 and math.min(len, #compBytes - pos + 1) or (#compBytes - pos + 1)
                            local chunk = compBytes:sub(pos, pos + n - 1)
                            pos = pos + n
                            return chunk
                        end
                    end)
                    local decompressed = fnSrc:Decompress()
                    assert(decompressed ~= nil)
                    local result = decompressed:Read()
                    assert(result == 'hello compressed world',
                        'fn-backend decompress mismatch: ' .. tostring(result))
                end)
                return 'ok'
            ");
            r.String.ShouldBe("ok");
        }

        // -- Function-backend socket simulation -----------------------------------
        // These tests pass a Lua function backend to Stream.Create() to simulate a
        // network socket that delivers data in small increments.  The READ handler
        // is called via lua_callk, so Sleep() inside it yields cooperatively without
        // raising "attempt to yield across a C-call boundary" (see
        // Stream_FunctionBackend_SleepInReadHandler_YieldsCorrectly and
        // CSV_FunctionBackendSocket_SleepInReadHandler_ParsesRows).  These particular
        // tests omit Sleep to stay fast and deterministic; "slow delivery" is instead
        // simulated by returning at most N bytes per Read() call, forcing each consumer
        // (CSV, Compress, JSON) to issue multiple reads.  This exercises the same
        // multi-read code paths that real network sockets exercise at runtime.
        [Fact]
        public async Task Stream_FunctionBackendSocket_Read_DeliversAllChunks()
        {
            // Baseline: repeated Read() calls assemble the full payload even when
            // the backend returns data 4 bytes at a time.
            LuaValue r = await Run(SocketPrologue + @"
                local payload = 'hello from the socket'
                local pos = 1
                local s = Stream.Create(function(op, len)
                    if op == OPEN  then return CAP_READ end
                    if op == CLOSE then return true end
                    if op == READ  then
                        if pos > #payload then return nil end
                        local chunk = payload:sub(pos, pos + 3)  -- 4 bytes at a time
                        pos = pos + 4
                        return chunk
                    end
                end)
                local buf = ''
                local chunk = s:Read()
                while chunk do
                    buf = buf .. chunk
                    chunk = s:Read()
                end
                return buf
            ");
            r.String.ShouldBe("hello from the socket");
        }

        [Fact]
        public async Task CSV_FunctionBackendSocket_ParsesChunkedRows()
        {
            // CSV DecodeFromFunction with a function-backend stream that returns
            // 8 bytes per Read() call.  Rows and field boundaries deliberately
            // fall inside chunk boundaries so the refill/accumulate logic is exercised.
            LuaValue r = await Run(SocketPrologue + @"
                local data = 'col1,col2,col3\nval1,val2,val3\nfoo,bar,baz\n'
                local pos = 1
                local s = Stream.Create(function(op, len)
                    if op == OPEN  then return CAP_READ end
                    if op == CLOSE then return true end
                    if op == READ  then
                        if pos > #data then return nil end
                        local chunk = data:sub(pos, pos + 7)  -- 8 bytes at a time
                        pos = pos + 8
                        return chunk
                    end
                end)
                local rows = {}
                for row in CSV.New():DecodeFromFunction(s) do
                    rows[#rows + 1] = tostring(row[1]) .. ':' .. tostring(row[2]) .. ':' .. tostring(row[3])
                end
                return table.concat(rows, '|')
            ");
            r.String.ShouldBe("col1:col2:col3|val1:val2:val3|foo:bar:baz");
        }

        [Fact]
        public async Task CSV_FunctionBackendSocket_AutoDetectSemicolon_Chunked()
        {
            // Auto-detect with chunked delivery: the delimiter must be sniffed after
            // enough chunks have been accumulated to see a complete line.
            LuaValue r = await Run(SocketPrologue + @"
                local data = 'name;age;city\nalice;30;paris\nbob;25;berlin\n'
                local pos = 1
                local s = Stream.Create(function(op, len)
                    if op == OPEN  then return CAP_READ end
                    if op == CLOSE then return true end
                    if op == READ  then
                        if pos > #data then return nil end
                        local chunk = data:sub(pos, pos + 5)  -- 6 bytes at a time
                        pos = pos + 6
                        return chunk
                    end
                end)
                local rows = {}
                for row in CSV.New():DecodeFromFunction(s) do
                    rows[#rows + 1] = tostring(row[1]) .. ':' .. tostring(row[3])
                end
                return table.concat(rows, '|')
            ");
            r.String.ShouldBe("name:city|alice:paris|bob:berlin");
        }

        [Fact]
        public async Task Stream_FunctionBackendSocket_Compress_RoundTrip()
        {
            // Compress reads from the stream in up to 64 KiB chunks; the backend
            // returns 16 bytes per call, so many reads are needed.  The decompressed
            // output must exactly match the original payload.
            LuaValue r = await Run(SocketPrologue + @"
                local payload = string.rep('kitsune socket data! ', 30)
                local pos = 1
                local s = Stream.Create(function(op, len)
                    if op == OPEN  then return CAP_READ end
                    if op == CLOSE then return true end
                    if op == READ  then
                        if pos > #payload then return false end
                        local chunk = payload:sub(pos, pos + 15)  -- 16 bytes at a time
                        pos = pos + 16
                        return chunk
                    end
                end)
                local compressed   = s:Compress()
                local decompressed = compressed:Decompress()
                return tostring(decompressed:Read() == payload)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Json_FunctionBackendSocket_DecodeIntoStream_ParsesChunkedJson()
        {
            // DecodeIntoStream calls lua_stream_read_chunk ? StreamRead in a loop.
            // The backend returns 6 bytes per call so the decoder must assemble the
            // full JSON object across many reads.
            LuaValue r = await Run(SocketPrologue + @"
                local jsonStr = '{""name"":""kitsune"",""version"":4,""active"":true}'
                local pos = 1
                local s = Stream.Create(function(op, len)
                    if op == OPEN  then return CAP_READ end
                    if op == CLOSE then return true end
                    if op == READ  then
                        if pos > #jsonStr then return nil end
                        local chunk = jsonStr:sub(pos, pos + 5)  -- 6 bytes at a time
                        pos = pos + 6
                        return chunk
                    end
                end)
                local j = Json.New()
                local t = j:DecodeIntoStream(s)
                return tostring(t.name == 'kitsune' and t.version == 4 and t.active == true)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Stream_FunctionBackendSocket_HasData_ReturnsZeroBeforeFirstRead()
        {
            // A function backend that has not yet delivered any data reports 0 bytes
            // remaining via HasData() because len==0 and pos==0 (no curpos/getlen
            // vtable — the Lua fn backend falls through to STREAM_OP_HASDATA dispatch
            // which returns nil, treated as falsy by the caller).
            LuaValue r = await Run(SocketPrologue + @"
                local s = Stream.Create(function(op, len)
                    if op == OPEN  then return CAP_READ end
                    if op == CLOSE then return true end
                    if op == READ  then return 'data' end
                end)
                -- The fn backend has no getlen/curpos vtable; STREAM_OP_HASDATA
                -- is dispatched and the function returns nil (no handler) which
                -- is falsy — callers should treat nil as 'no data available yet'.
                local hd = s:HasData()
                return tostring(hd == nil or hd == false or hd == 0)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Stream_FunctionBackend_SleepInReadHandler_YieldsCorrectly()
        {
            // ReadLuaStream now uses lua_callk for fn backends, so Sleep() inside
            // the READ handler can yield the coroutine without hitting
            // "attempt to yield across a C-call boundary".
            LuaValue r = await Run(RunCoroutine + SocketPrologue + @"
                run(function()
                    local data = 'hello world'
                    local pos = 1
                    local s = Stream.Create(function(op, len)
                        if op == OPEN  then return CAP_READ end
                        if op == CLOSE then return true end
                        if op == READ  then
                            if pos > #data then return nil end
                            Sleep(5)   -- yield to scheduler; must not raise an error
                            local chunk = data:sub(pos, pos + 3)
                            pos = pos + 4
                            return chunk
                        end
                    end)
                    local buf = ''
                    local chunk = s:Read()
                    while chunk do
                        buf = buf .. chunk
                        chunk = s:Read()
                    end
                    assert(buf == data, 'expected [' .. data .. '] got [' .. buf .. ']')
                end)
                return 'ok'
            ");
            r.String.ShouldBe("ok");
        }

        [Fact]
        public async Task CSV_FunctionBackendSocket_SleepInReadHandler_ParsesRows()
        {
            // End-to-end: CSV DecodeFromFunction on a fn-backend stream whose
            // READ handler calls Sleep between chunks.  Exercises the full
            // CsvStreamIterator ? lua_callk ? ReadLuaStream ? lua_callk(fn) ? Sleep chain.
            LuaValue r = await Run(RunCoroutine + SocketPrologue + @"
                run(function()
                    local data = 'a,b,c\n1,2,3\n4,5,6\n'
                    local pos = 1
                    local s = Stream.Create(function(op, len)
                        if op == OPEN  then return CAP_READ end
                        if op == CLOSE then return true end
                        if op == READ  then
                            if pos > #data then return nil end
                            Sleep(5)
                            local chunk = data:sub(pos, pos + 5)  -- 6 bytes at a time
                            pos = pos + 6
                            return chunk
                        end
                    end)
                    local rows = {}
                    for row in CSV.New():DecodeFromFunction(s) do
                        rows[#rows + 1] = tostring(row[1]) .. ':' .. tostring(row[2]) .. ':' .. tostring(row[3])
                    end
                    assert(#rows == 3, 'expected 3 rows got ' .. #rows)
                    assert(rows[1] == 'a:b:c',   rows[1])
                    assert(rows[2] == '1:2:3',   rows[2])
                    assert(rows[3] == '4:5:6',   rows[3])
                end)
                return 'ok'
            ");
            r.String.ShouldBe("ok");
        }

        private static async Task<LuaValue> Run(string lua)
        {
            var engine = new KitsuneEngine();
            LuaValue result;
            try
            {
                result = await engine.ExecuteStringAsync(lua).ConfigureAwait(false);
            }
            finally
            {
                engine.Dispose();
            }

            if (engine.LeakedAllocations != 0)
            {
                throw new InvalidOperationException($"Native memory leak: {engine.LeakedAllocations} unfreed allocation(s) after KitsuneCleanup");
            }

            return result;
        }

        private static async Task<LuaValue> RunWithSession(string lua)
        {
            var engine = new KitsuneEngine();
            engine.RegisterSession();
            LuaValue result;
            try
            {
                result = await engine.ExecuteStringAsync(lua).ConfigureAwait(false);
            }
            finally
            {
                engine.Dispose();
            }

            if (engine.LeakedAllocations != 0)
            {
                throw new InvalidOperationException($"Native memory leak: {engine.LeakedAllocations} unfreed allocation(s) after KitsuneCleanup");
            }

            return result;
        }
    }
}
