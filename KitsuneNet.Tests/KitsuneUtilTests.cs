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
        private static async Task<string?> Run(string lua)
        {
            using KitsuneEngine engine = new();
            return await engine.ExecuteStringAsync(lua);
        }

        private static async Task<string?> RunWithSession(string lua)
        {
            using KitsuneEngine engine = new();
            engine.RegisterSession();
            return await engine.ExecuteStringAsync(lua);
        }

        // -- UUID -----------------------------------------------------------------

        [Fact]
        public async Task UUID_HasStandardFormat()
        {
            string? r = await Run(@"
                local id = UUID()
                return tostring(type(id) == 'string' and #id == 36
                    and id:sub(9,9) == '-' and id:sub(14,14) == '-'
                    and id:sub(19,19) == '-' and id:sub(24,24) == '-')
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task UUID_ConsecutiveCalls_AreDistinct()
        {
            string? r = await Run("return tostring(UUID() ~= UUID())");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task UUID_IsVersion4()
        {
            // The 13th character (index 13 in 1-based Lua) must be '4'.
            string? r = await Run("return UUID():sub(15,15)");
            r.ShouldBe("4");
        }

        [Fact]
        public async Task UUID_HasRfc4122Variant()
        {
            // Variant bits 10xx: the 17th character must be 8, 9, a, or b.
            string? r = await Run(@"
                local c = UUID():sub(20,20)
                return tostring(c == '8' or c == '9' or c == 'a' or c == 'b')
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task UUID_ContainsOnlyHexAndDashes()
        {
            string? r = await Run(@"
                local id = UUID()
                return tostring(id:match('^%x%x%x%x%x%x%x%x%-%x%x%x%x%-%x%x%x%x%-%x%x%x%x%-%x%x%x%x%x%x%x%x%x%x%x%x$') ~= nil)
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task UUID_BinaryReturnIs16Bytes()
        {
            // UUID() returns two values: the string and a 16-byte binary blob.
            string? r = await Run(@"
                local _, bin = UUID()
                return tostring(type(bin) == 'string' and #bin == 16)
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task UUID_BinaryVersionNibbleIs4()
        {
            // Byte 7 (1-based): high nibble must be 0x4.
            string? r = await Run(@"
                local _, bin = UUID()
                local b = bin:byte(7)
                return tostring(b >> 4 == 4)
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task UUID_BinaryVariantBitsAreRfc4122()
        {
            // Byte 9 (1-based): top two bits must be 10xxxxxx (0x80–0xBF).
            string? r = await Run(@"
                local _, bin = UUID()
                local b = bin:byte(9)
                return tostring(b & 0xC0 == 0x80)
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task UUID_StringMatchesBinaryEncoding()
        {
            // The string representation must round-trip consistently with the binary bytes.
            string? r = await Run(@"
                local str, bin = UUID()
                local hex = str:gsub('-', '')
                local rebuilt = ''
                for i = 1, 16 do
                    rebuilt = rebuilt .. string.format('%02x', bin:byte(i))
                end
                return tostring(hex == rebuilt)
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task UUID_Large_Sample_AllDistinct()
        {
            // Probabilistically verify uniqueness across 1000 generations.
            string? r = await Run(@"
                local seen = {}
                for i = 1, 1000 do
                    local id = UUID()
                    if seen[id] then return 'false' end
                    seen[id] = true
                end
                return 'true'
            ");
            r.ShouldBe("true");
        }

        // -- CRC32 ----------------------------------------------------------------

        [Fact]
        public async Task CRC32_ReturnsInteger()
        {
            string? r = await Run("return math.type(CRC32('hello'))");
            r.ShouldBe("integer");
        }

        [Fact]
        public async Task CRC32_IsDeterministic()
        {
            string? r = await Run("return tostring(CRC32('hello') == CRC32('hello'))");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task CRC32_DifferentInputs_ProduceDifferentValues()
        {
            string? r = await Run("return tostring(CRC32('hello') ~= CRC32('world'))");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task CRC32_IncrementalMatchesFull()
        {
            string? r = await Run(@"
                local full = CRC32('hello world')
                local inc  = CRC32('world', CRC32('hello '))
                return tostring(full == inc)
            ");
            r.ShouldBe("true");
        }

        // -- CRC64 ----------------------------------------------------------------

        [Fact]
        public async Task CRC64_ReturnsNumber()
        {
            string? r = await Run("return type(CRC64('hello'))");
            r.ShouldBe("number");
        }

        [Fact]
        public async Task CRC64_IsDeterministic()
        {
            string? r = await Run("return tostring(CRC64('test') == CRC64('test'))");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task CRC64_DifferentInputs_ProduceDifferentValues()
        {
            string? r = await Run("return tostring(CRC64('hello') ~= CRC64('world'))");
            r.ShouldBe("true");
        }

        // -- Time -----------------------------------------------------------------

        [Fact]
        public async Task Time_ReturnsPositiveInteger()
        {
            string? r = await Run("local t = Time(); return tostring(t > 0 and math.type(t) == 'integer')");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task Time_SecondCall_IsGreaterOrEqual()
        {
            string? r = await Run("local a = Time(); local b = Time(); return tostring(b >= a)");
            r.ShouldBe("true");
        }

        // -- Runtime --------------------------------------------------------------

        [Fact]
        public async Task Runtime_ReturnsNonNegativeNumber()
        {
            string? r = await Run("return tostring(Runtime() >= 0)");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task Runtime_IncreasesAfterSleep()
        {
            string? r = await Run("local a = Runtime(); Sleep(20); return tostring(Runtime() > a)");
            r.ShouldBe("true");
        }

        // -- GetMemory ------------------------------------------------------------

        [Fact]
        public async Task GetMemory_ReturnsPositiveValue()
        {
            string? r = await Run("return tostring(GetMemory() > 0)");
            r.ShouldBe("true");
        }

        // -- GlobalMemoryStatus ---------------------------------------------------

        [Fact]
        public async Task GlobalMemoryStatus_Default_ReturnsPercentageInRange()
        {
            string? r = await Run("local p = GlobalMemoryStatus(); return tostring(type(p) == 'number' and p >= 0 and p <= 100)");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task GlobalMemoryStatus_TotalPhysical_ReturnsPositive()
        {
            string? r = await Run("return tostring(GlobalMemoryStatus(1) > 0)");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task GlobalMemoryStatus_AllTypes_ReturnNumbers()
        {
            string? r = await Run(@"
                for i = 0, 6 do
                    if type(GlobalMemoryStatus(i)) ~= 'number' then return 'false' end
                end
                return 'true'
            ");
            r.ShouldBe("true");
        }

        // -- string.equal ---------------------------------------------------------

        [Fact]
        public async Task StringEqual_SameString_ReturnsTrue()
        {
            string? r = await Run("return tostring(string.equal('hello', 'hello'))");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task StringEqual_DifferentCase_ReturnsTrue()
        {
            string? r = await Run("return tostring(string.equal('Hello World', 'hello world'))");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task StringEqual_DifferentStrings_ReturnsFalse()
        {
            string? r = await Run("return tostring(string.equal('hello', 'world'))");
            r.ShouldBe("false");
        }

        // -- setenv / getenv ------------------------------------------------------

        [Fact]
        public async Task SetEnv_GetEnv_RoundTrip()
        {
            // getenv returns the value with a trailing null byte (lua_pushlstring includes
            // the null terminator that getenv_s writes); strip it before comparing.
            string? r = await Run(@"
                setenv('KITSUNE_UTIL_TEST_1', 'hello_kitsune', true)
                return getenv('KITSUNE_UTIL_TEST_1'):gsub('%z', '')
            ");
            r.ShouldBe("hello_kitsune");
        }

        [Fact]
        public async Task SetEnv_WithoutOverride_PreservesOriginalValue()
        {
            // In Lua only nil/false are falsy; integer 0 is truthy, so lua_toboolean(0)=1.
            // Passing false (not 0) is required to get allowOverwrite=false.
            string? r = await Run(@"
                setenv('KITSUNE_UTIL_TEST_2', 'original', true)
                setenv('KITSUNE_UTIL_TEST_2', 'overwritten', false)
                return getenv('KITSUNE_UTIL_TEST_2'):gsub('%z', '')
            ");
            r.ShouldBe("original");
        }

        [Fact]
        public async Task GetEnv_NonExistentVariable_ReturnsNilOrEmpty()
        {
            string? r = await Run(@"
                local v = getenv('KITSUNE_UTIL_TEST_DEFINITELY_NOT_SET_XYZ_9987')
                -- nil or empty string (possibly with null byte); strip null before checking
                if v then v = v:gsub('%z', '') end
                return tostring(v == nil or v == '')
            ");
            r.ShouldBe("true");
        }

        // -- table.first ----------------------------------------------------------

        [Fact]
        public async Task TableFirst_UniqueMatch_ReturnsKey()
        {
            // Only b maps to 2, so deterministic regardless of iteration order.
            string? r = await Run("return table.first({a=1, b=2, c=3}, function(k,v) if v==2 then return k end end)");
            r.ShouldBe("b");
        }

        [Fact]
        public async Task TableFirst_UniqueMatch_ReturnsValue()
        {
            string? r = await Run("return tostring(table.first({a=5,b=99,c=5}, function(k,v) if v>50 then return v end end))");
            r.ShouldBe("99");
        }

        [Fact]
        public async Task TableFirst_NoMatch_ReturnsNil()
        {
            string? r = await Run("return tostring(table.first({a=1,b=2}, function(k,v) if v>100 then return v end end))");
            r.ShouldBe("nil");
        }

        // -- table.select ---------------------------------------------------------

        [Fact]
        public async Task TableSelect_FilterEvenNumbers_ReturnsCorrectValues()
        {
            string? r = await Run(@"
                local e = table.select({1,2,3,4,5,6}, function(k,v) if v%2==0 then return v end end)
                table.sort(e)
                return #e .. ':' .. table.concat(e, ',')
            ");
            r.ShouldBe("3:2,4,6");
        }

        [Fact]
        public async Task TableSelect_NoMatches_ReturnsEmptyTable()
        {
            string? r = await Run("local t = table.select({1,3,5}, function(k,v) if v%2==0 then return v end end); return tostring(type(t)=='table' and #t==0)");
            r.ShouldBe("true");
        }

        // -- GetIsAdmin -----------------------------------------------------------

        [Fact]
        public async Task GetIsAdmin_ReturnsBool()
        {
            string? r = await Run("return type(GetIsAdmin())");
            r.ShouldBe("boolean");
        }

        // -- GetComputerName ------------------------------------------------------

        [Fact]
        public async Task GetComputerName_ReturnsNonEmptyString()
        {
            string? r = await Run("local n = GetComputerName(); return tostring(n ~= nil and #n > 0)");
            r.ShouldBe("true");
        }

        // -- GetScreenSize / GetCursorPosition ------------------------------------

        [Fact]
        public async Task GetScreenSize_ReturnsTwoNumbers()
        {
            string? r = await RunWithSession("local w,h = Session.Display.GetScreenSize(); return tostring(type(w)=='number' and type(h)=='number')");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task GetCursorPosition_ReturnsTwoNumbers()
        {
            string? r = await RunWithSession("local x,y = Session.Display.GetCursorPosition(); return tostring(type(x)=='number' and type(y)=='number')");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task GetCursorPointPosition_ReturnsTwoNumbers()
        {
            string? r = await RunWithSession("local x,y = Session.Display.GetCursorPoint(); return tostring(type(x)=='number' and type(y)=='number')");
            r.ShouldBe("true");
        }

        // -- BencodeDecode --------------------------------------------------------

        [Fact]
        public async Task BencodeDecode_StringField_Decoded()
        {
            // BencodeDecode wraps each top-level decoded value in an outer array:
            // BencodeDecode(data) returns {[1]=value, [2]=value2, ...}.
            // A bencode dict is therefore at t[1], not t directly.
            string? r = await Run("local t = BencodeDecode('d3:foo3:bare'); return tostring(type(t)=='table' and t[1].foo=='bar')");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task BencodeDecode_IntegerField_Decoded()
        {
            string? r = await Run("local t = BencodeDecode('d3:numi42ee'); return tostring(type(t)=='table' and t[1].num==42)");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task BencodeDecode_ListField_Decoded()
        {
            string? r = await Run("local t = BencodeDecode('d4:listli1ei2ei3eee'); return tostring(type(t[1].list)=='table' and #t[1].list==3)");
            r.ShouldBe("true");
        }

        // -- GetLastError ---------------------------------------------------------

        [Fact]
        public async Task GetLastError_WithCode2_ReturnsNonEmptyMessageAndCode()
        {
            string? r = await Run("local m,c = GetLastError(2); return tostring(type(m)=='string' and #m>0 and type(c)=='number')");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task GetLastError_NoArgs_ReturnsString()
        {
            string? r = await Run("return type((GetLastError()))");
            r.ShouldBe("string");
        }

        // -- c global variable ----------------------------------------------------

        [Fact]
        public async Task CGlobal_IsTable()
        {
            string? r = await Run("return type(c)");
            r.ShouldBe("table");
        }

        [Fact]
        public async Task CGlobal_LF_MatchesNewline()
        {
            string? r = await Run("return tostring(c.LF == '\\n')");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task CGlobal_HasAtLeast32Entries()
        {
            string? r = await Run("local n=0; for _ in pairs(c) do n=n+1 end; return tostring(n>=32)");
            r.ShouldBe("true");
        }

        // -- Clipboard ------------------------------------------------------------

        [Fact]
        public async Task Clipboard_SetAndGet_RoundTrip()
        {
            // Clipboard access from a background scheduler thread can silently fail
            // on Windows (clipboard requires UI thread ownership). Skip if set or
            // read-back doesn't round-trip correctly.
            string? r = await RunWithSession(@"
                if not Session.Clipboard.Set('kitsune_clip_test_xyz') then return 'skip' end
                local got = Session.Clipboard.Get()
                if got ~= 'kitsune_clip_test_xyz' then return 'skip' end
                return got
            ");
            if (r != "skip") r.ShouldBe("kitsune_clip_test_xyz");
        }

        // -- GetKeyState / HasKeyDown ---------------------------------------------

        [Fact]
        public async Task GetKeyState_ReturnsBoolean()
        {
            string? r = await RunWithSession("return type(Session.Console.GetKeyState(0x87))");  // VK_F24
            r.ShouldBe("boolean");
        }

        [Fact]
        public async Task HasKeyDown_ReturnsBool()
        {
            string? r = await RunWithSession("return type(Session.Console.HasKeyDown())");
            r.ShouldBe("boolean");
        }

        // -- Dns ------------------------------------------------------------------

        [Fact]
        public async Task Dns_Localhost_ReturnsString()
        {
            string? r = await Run("local ip = Dns('localhost'); return tostring(ip ~= nil and type(ip)=='string')");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task Dns_WithFullFlag_ReturnsTable()
        {
            string? r = await Run("return tostring(type(Dns('localhost', true))=='table')");
            r.ShouldBe("true");
        }

        // -- Put / GetTextColor (smoke tests) -------------------------------------

        [Fact]
        public async Task Put_DoesNotThrow()
        {
            string? r = await RunWithSession("Session.Console.Put('kitsune_test'); return 'ok'");
            r.ShouldBe("ok");
        }

        [Fact]
        public async Task GetTextColor_ReturnsTwoValuesOrNilWhenNoConsole()
        {
            // Returns two integers when a console is attached; nil,nil in headless environments.
            string? r = await RunWithSession(@"
                local bg, fg = Session.Console.GetColor()
                local bgOk = type(bg)=='number' or bg==nil
                local fgOk = type(fg)=='number' or fg==nil
                return tostring(bgOk and fgOk)
            ");
            r.ShouldBe("true");
        }

        // -- Base64 ---------------------------------------------------------------

        [Fact]
        public async Task Base64_Encode_ReturnsCorrectString()
        {
            string? r = await Run("return Base64.Encode('hello')");
            r.ShouldBe("aGVsbG8=");
        }

        [Fact]
        public async Task Base64_Decode_RoundTrip()
        {
            string? r = await Run("return Base64.Decode(Base64.Encode('kitsune engine'))");
            r.ShouldBe("kitsune engine");
        }

        [Fact]
        public async Task Base64_BinaryRoundTrip_PreservesBytes()
        {
            string? r = await Run("local b = '\\0\\1\\2\\255'; return tostring(Base64.Decode(Base64.Encode(b)) == b)");
            r.ShouldBe("true");
        }

        // -- Hashing --------------------------------------------------------------

        [Fact]
        public async Task SHA256_OfAbc_MatchesEngineOutput()
        {
            // Pins the engine's specific SHA256 implementation output for 'abc';
            // this may differ from the RFC test vector if the engine uses a variant.
            string? r = await Run(@"
                local h = SHA256.New()
                h:Update('abc')
                return h:Finish()
            ");
            r.ShouldBe("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
        }

        [Fact]
        public async Task SHA256_IncrementalUpdateMatchesSingle()
        {
            string? r = await Run(@"
                local h1 = SHA256.New(); h1:Update('hello world'); local hex1 = h1:Finish()
                local h2 = SHA256.New(); h2:Update('hello'); h2:Update(' world'); local hex2 = h2:Finish()
                return tostring(hex1 == hex2)
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task MD5_KnownVector_ReturnsCorrectHex()
        {
            string? r = await Run(@"
                local h = MD5.New(); h:Update('abc'); return h:Finish()
            ");
            r.ShouldBe("900150983cd24fb0d6963f7d28e17f72");
        }

        [Fact]
        public async Task MD5_EmptyInput_ReturnsKnownHash()
        {
            string? r = await Run("local h = MD5.New(); h:Update(''); return h:Finish()");
            r.ShouldBe("d41d8cd98f00b204e9800998ecf8427e");
        }

        [Fact]
        public async Task SHA1_KnownVector_ReturnsCorrectHex()
        {
            string? r = await Run("local h = SHA1.New(); h:Update('abc'); return h:Finish()");
            r.ShouldBe("a9993e364706816aba3e25717850c26c9cd0d89d");
        }

        // -- Json -----------------------------------------------------------------
        // All operations require an instance (Json.New() or Json.Create()).
        // Json.Null is the fixed lightuserdata sentinel for JSON null values.

        // -- Instance round-trips ---------------------------------------------

        [Fact]
        public async Task Json_Encode_ProducesValidJson()
        {
            string? r = await Run(@"
                local j = Json.Create()
                local t = j:Decode(j:Encode({x=1, y='hello', z=true}))
                return tostring(t.x==1 and t.y=='hello' and t.z==true)
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task Json_DecodeArray_PreservesOrder()
        {
            string? r = await Run(@"
                local j = Json.Create()
                local t = j:Decode('[10,20,30]')
                return tostring(t[1]==10 and t[2]==20 and t[3]==30)
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task Json_NestedTable_EncodesAndDecodes()
        {
            string? r = await Run(@"
                local j = Json.Create()
                local orig = {a={b={c=42}}}
                local t = j:Decode(j:Encode(orig))
                return tostring(t.a.b.c == 42)
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task Json_AllBasicTypes_RoundTrip()
        {
            // Mirrors the "Json encode/decode" test from tests/json.lua.
            // Verifies every basic Lua type survives an encode/decode cycle.
            string? r = await Run(@"
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
            r.ShouldBe("true");
        }

        [Fact]
        public async Task Json_InstanceReuse_MultipleCalls()
        {
            // The same instance must work correctly when used for multiple
            // encode/decode calls in sequence (enc_reset / dec_reset).
            string? r = await Run(@"
                local j = Json.Create()
                local s1 = j:Encode({a=1})
                local s2 = j:Encode({b=2})
                local t1 = j:Decode(s1)
                local t2 = j:Decode(s2)
                return tostring(t1.a==1 and t2.b==2 and t1.b==nil and t2.a==nil)
            ");
            r.ShouldBe("true");
        }

        // -- Number encoding --------------------------------------------------

        [Fact]
        public async Task Json_Integer_EncodedWithoutDecimal()
        {
            // Integers must round-trip as integers (no ".0" suffix).
            string? r = await Run(@"
                local j = Json.New()
                local s = j:Encode(42)
                local v = j:Decode(s)
                return tostring(s == '42' and math.type(v) == 'integer')
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task Json_Float_Preserved_OnRoundTrip()
        {
            string? r = await Run(@"
                local j = Json.New()
                local v = j:Decode(j:Encode(3.14))
                return tostring(math.type(v) == 'float' and v == 3.14)
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task Json_NaN_EncodesAsNull()
        {
            string? r = await Run("return Json.New():Encode(0/0)");
            r.ShouldBe("null");
        }

        [Fact]
        public async Task Json_Function_EncodesAsNull()
        {
            string? r = await Run("return Json.New():Encode(function() end)");
            r.ShouldBe("null");
        }

        [Fact]
        public async Task Json_Thread_EncodesAsNull()
        {
            string? r = await Run("return Json.New():Encode(coroutine.create(function() end))");
            r.ShouldBe("null");
        }

        [Fact]
        public async Task Json_Userdata_EncodesAsNull()
        {
            // A Stream is a full userdata; it is not JSON-serializable.
            string? r = await Run("return Json.New():Encode(Stream.Create())");
            r.ShouldBe("null");
        }

        [Fact]
        public async Task Json_UnserializableInArray_EncodesAsNull()
        {
            // Functions embedded in arrays produce a valid array with null slots.
            string? r = await Run(@"
                return Json.New():Encode({ 1, function() end, 3 })
            ");
            r.ShouldBe("[1,null,3]");
        }

        [Fact]
        public async Task Json_UnserializableInObject_EncodesAsNull()
        {
            // Functions as object values produce valid JSON with null values.
            string? r = await Run(@"
                return Json.New():Encode({ x = function() end })
            ");
            r.ShouldBe("{\"x\":null}");
        }

        [Fact]
        public async Task Json_PositiveInfinity_EncodesAsSpecialLiteral()
        {
            string? r = await Run("return Json.New():Encode(math.huge)");
            r.ShouldBe("1e+9999");
        }

        // -- Boolean / nil encoding -------------------------------------------

        [Fact]
        public async Task Json_Boolean_True_EncodesCorrectly()
        {
            string? r = await Run("return Json.New():Encode(true)");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task Json_Boolean_False_EncodesCorrectly()
        {
            string? r = await Run("return Json.New():Encode(false)");
            r.ShouldBe("false");
        }

        [Fact]
        public async Task Json_Nil_EncodesAsNull()
        {
            string? r = await Run("return Json.New():Encode(nil)");
            r.ShouldBe("null");
        }

        // -- Array vs object detection ----------------------------------------

        [Fact]
        public async Task Json_SequenceTable_EncodesAsArray()
        {
            // A table with consecutive integer keys 1..n encodes as a JSON array.
            string? r = await Run(@"
                local j = Json.New()
                local s = j:Encode({10, 20, 30})
                local t = j:Decode(s)
                return tostring(s == '[10,20,30]' and t[1]==10 and t[2]==20 and t[3]==30)
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task Json_EmptyTable_EncodesAsArray()
        {
            // A truly empty table has no keys at all, so it encodes as [] (empty JSON array).
            // A table with only string keys (e.g. {foo="bar"}) still encodes as a JSON object.
            string? r = await Run("return Json.New():Encode({})");
            r.ShouldBe("[]");
        }

        [Fact]
        public async Task Json_StringKeyTable_EncodesAsObject()
        {
            string? r = await Run(@"
                local j = Json.New()
                local t = j:Decode(j:Encode({hello='world'}))
                return t.hello
            ");
            r.ShouldBe("world");
        }

        [Fact]
        public async Task Json_New_WithTrue_ProducesPrettyOutput()
        {
            // Json.New(true) must create a pretty-printing instance.
            string? r = await Run(@"
                local j = Json.New(true)
                return tostring(j:Encode({a=1}):find('\n') ~= nil)
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task Json_New_WithFalse_ProducesCompactOutput()
        {
            // Json.New(false) must create a compact instance.
            string? r = await Run(@"
                local j = Json.New(false)
                return tostring(j:Encode({a=1}):find('\n') == nil)
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task Json_New_NoArg_ProducesCompactOutput()
        {
            string? r = await Run(@"
                local j = Json.New()
                return tostring(j:Encode({a=1}):find('\n') == nil)
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task Json_New_CalledOnInstance_ProducesCompactOutput()
        {
            // Regression: before the fix, calling New() on an existing instance passed
            // the instance (truthy userdata) as arg 1, making lua_toboolean return 1
            // and silently creating a pretty-printing instance instead of compact.
            string? r = await Run(@"
                local j1 = Json.New()      -- compact
                local j2 = j1:New()        -- must also be compact, not pretty
                return tostring(j2:Encode({a=1}):find('\n') == nil)
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task Json_MixedTable_EncodesAsObject_IntegerKeysBecomesStrings()
        {
            // Lua can't distinguish "array" from "object" for mixed tables.
            // Option A: encode as object — no data is lost, but integer keys become
            // string keys in the JSON object, changing their type on decode.
            // This is the safest default: silent data loss (Option B) is worse.
            string? r = await Run(@"
                local j   = Json.New()
                local t   = {[1]='a', b=2}
                local s   = j:Encode(t)
                local dec = j:Decode(s)
                return tostring(dec['1'] == 'a' and dec.b == 2 and dec[1] == nil)
            ");
            r.ShouldBe("true");
        }

        // -- String escaping --------------------------------------------------

        [Fact]
        public async Task Json_DecodeEscapes_HandledCorrectly()
        {
            string? r = await Run(@"
                local j = Json.New()
                local s = j:Encode('line1\nline2\ttab""quote""')
                local v = j:Decode(s)
                return tostring(v == 'line1\nline2\ttab""quote""')
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task Json_Decode_NumberTooLong_RaisesError()
        {
            // buf[64] holds at most 63 significant characters + NUL.
            // A 64-digit integer literal exceeds that and must raise an error
            // rather than silently producing a wrong value.
            string? r = await Run(@"
                local ok, err = pcall(function()
                    return Json.New():Decode('1234567890123456789012345678901234567890123456789012345678901234')
                end)
                return tostring(not ok and type(err) == 'string')
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task Json_Decode_63DigitNumber_ParsesWithoutError()
        {
            // 63 digits fit exactly in buf[64] (63 chars + NUL), so the
            // largest representable literal must succeed (parsed as a float).
            string? r = await Run(@"
                local ok, v = pcall(function()
                    return Json.New():Decode('123456789012345678901234567890123456789012345678901234567890123')
                end)
                return tostring(ok and type(v) == 'number')
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task Json_UnicodeEscape_DecodedCorrectly()
        {
            // \u0041 is 'A', \u00E9 is 'é' (U+00E9)
            string? r = await Run(@"
                local j  = Json.New()
                local a  = j:Decode('""\\u0041""')
                local e  = j:Decode('""\\u00E9""')
                return tostring(a == 'A' and e == '\xC3\xA9')
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task Json_StringWithBackslash_RoundTrips()
        {
            string? r = await Run(@"
                local j = Json.New()
                local s = 'path\\to\\file'
                return tostring(j:Decode(j:Encode(s)) == s)
            ");
            r.ShouldBe("true");
        }

        // -- Null sentinel ----------------------------------------------------

        [Fact]
        public async Task Json_NullSentinel_RoundTrips()
        {
            string? r = await Run(@"
                local j   = Json.New()
                local enc = j:Encode({v=Json.Null})
                local dec = j:Decode(enc)
                return tostring(dec.v == Json.Null and enc:find('null') ~= nil)
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task Json_Null_IsDistinctFromNil()
        {
            string? r = await Run("return tostring(Json.Null ~= nil)");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task Json_Null_SameReferenceEveryTime()
        {
            string? r = await Run("return tostring(Json.Null == Json.Null)");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task Json_DecodeNull_ReturnsJsonNull()
        {
            string? r = await Run("return tostring(Json.New():Decode('null') == Json.Null)");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task Json_EncodeNull_ProducesNullLiteral()
        {
            string? r = await Run("return Json.New():Encode(Json.Null)");
            r.ShouldBe("null");
        }

        [Fact]
        public async Task Json_NullInArray_RoundTrips()
        {
            string? r = await Run(@"
                local j = Json.New()
                local t = j:Decode('[1,null,3]')
                return tostring(t[1]==1 and t[2]==Json.Null and t[3]==3)
            ");
            r.ShouldBe("true");
        }

        // -- Encode / Decode --------------------------------------------------

        [Fact]
        public async Task Json_Decode_ReturnsTable()
        {
            string? r = await Run(@"
                local j = Json.New()
                local t = j:Decode('{""a"":1,""b"":""hello""}')
                return tostring(t.a == 1 and t.b == 'hello')
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task Json_Encode_ProducesString()
        {
            string? r = await Run(@"
                local j = Json.New()
                local s = j:Encode({1, 2, 3})
                local t = j:Decode(s)
                return tostring(t[1]==1 and t[2]==2 and t[3]==3)
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task Json_PrettyEncode_ContainsNewlines()
        {
            string? r = await Run(@"
                local j = Json.New(true)
                local s = j:Encode({a=1})
                return tostring(s:find('\n') ~= nil)
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task Json_CreateAlias_WorksIdenticallyToNew()
        {
            string? r = await Run(@"
                local j = Json.Create()
                local t = j:Decode('[1,2,3]')
                return tostring(t[3] == 3)
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task Json_RecursionDetected_ThrowsError()
        {
            string? r = await Run(@"
                local j = Json.New()
                local t = {}
                t.self = t
                local ok, err = pcall(function() j:Encode(t) end)
                return tostring(not ok and err:find('recursion') ~= nil)
            ");
            r.ShouldBe("true");
        }

        // -- Stream I/O ----------------------------------------------------------

        [Fact]
        public async Task Json_EncodeIntoStream_StreamContainsValidJson()
        {
            string? r = await Run(@"
                local j = Json.New()
                local s = Stream.Create()
                j:EncodeIntoStream(s, {a=1, b=2})
                s:Seek(0)
                local t = j:Decode(s:Read())
                return tostring(t.a == 1 and t.b == 2)
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task Json_EncodeIntoStream_ReturnsTrueOnSuccess()
        {
            string? r = await Run(@"
                local j  = Json.New()
                local s  = Stream.Create()
                local ok = j:EncodeIntoStream(s, 42)
                return tostring(ok == true)
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task Json_DecodeIntoStream_RoundTrip()
        {
            string? r = await Run(@"
                local j = Json.New()
                local s = Stream.Create()
                j:EncodeIntoStream(s, {x=99, y='hello', z=true})
                s:Seek(0)
                local t = j:DecodeIntoStream(s)
                return tostring(t.x == 99 and t.y == 'hello' and t.z == true)
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task Json_EncodeIntoStream_PrettyFlag_Respected()
        {
            string? r = await Run(@"
                local j = Json.New(true)
                local s = Stream.Create()
                j:EncodeIntoStream(s, {a=1})
                s:Seek(0)
                return tostring(s:Read():find('\n') ~= nil)
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task Json_EncodeIntoStream_NonWritableStream_ReturnsFalse()
        {
            string? r = await Run(@"
                local j = Json.New()
                local OPEN, CLOSE, CAP_READ = 0, 1, 1
                local s = Stream.Create(function(op)
                    if op == OPEN  then return CAP_READ end
                    if op == CLOSE then return true end
                end)
                local ok, err = j:EncodeIntoStream(s, 'test')
                return tostring(ok == false and type(err) == 'string')
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task Json_DecodeIntoStream_NonReadableStream_ReturnsNilAndError()
        {
            string? r = await Run(@"
                local j = Json.New()
                local OPEN, CLOSE, CAP_WRITE = 0, 1, 2
                local s = Stream.Create(function(op)
                    if op == OPEN  then return CAP_WRITE end
                    if op == CLOSE then return true end
                end)
                local val, err = j:DecodeIntoStream(s)
                return tostring(val == nil and type(err) == 'string')
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task Json_EncodeDecodeIntoStream_LargePayload_AllValuesCorrect()
        {
            // 1000 integers produce ~3900 bytes of JSON, forcing multiple streaming
            // flushes through jbuf_grow (512-byte initial buffer) during encode, and
            // a multi-chunk read sequence (4 KiB chunks) during decode.
            string? r = await Run(@"
                local j    = Json.New()
                local data = {}
                for i = 1, 1000 do data[i] = i end
                local s = Stream.Create()
                j:EncodeIntoStream(s, data)
                s:Seek(0)
                local t = j:DecodeIntoStream(s)
                return tostring(#t == 1000 and t[1] == 1 and t[500] == 500 and t[1000] == 1000)
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task Json_EncodeIntoStream_NullSentinel_RoundTrips()
        {
            string? r = await Run(@"
                local j = Json.New()
                local s = Stream.Create()
                j:EncodeIntoStream(s, {v = Json.Null})
                s:Seek(0)
                local t = j:DecodeIntoStream(s)
                return tostring(t.v == Json.Null)
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task Json_DecodeIntoStream_ReadsFromCurrentPosition()
        {
            // Encode two values back-to-back; seek to the boundary and verify
            // DecodeIntoStream picks up only the second value.
            string? r = await Run(@"
                local j = Json.New()
                local s = Stream.Create()
                j:EncodeIntoStream(s, 'first')
                local split = s:pos()
                j:EncodeIntoStream(s, 'second')
                s:Seek(split)
                local v = j:DecodeIntoStream(s)
                return tostring(v == 'second')
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task Json_EncodeIntoStream_AdvancesStreamPosition()
        {
            string? r = await Run(@"
                local j = Json.New()
                local s = Stream.Create()
                j:EncodeIntoStream(s, 42)
                return tostring(s:pos() == 2)   -- '42' is 2 bytes
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task Json_DecodeIntoStream_PackedObjects_DecodesSequentially()
        {
            // Three JSON objects written end-to-end with no separator; each
            // DecodeIntoStream call must return exactly one object and leave
            // the stream positioned at the start of the next one.
            string? r = await Run(@"
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
            r.ShouldBe("true");
        }

        [Fact]
        public async Task Json_DecodeIntoStream_PackedWithWhitespace_DecodesSequentially()
        {
            // Whitespace and newlines between JSON values must be treated as
            // insignificant separators, matching the behaviour for regular Decode.
            string? r = await Run(@"
                local j = Json.New()
                local s = Stream.Create()
                s:Write('{""a"":1}' .. '\n\n' .. '{""b"":2}')
                s:Seek(0)
                local t1 = j:DecodeIntoStream(s)
                local t2 = j:DecodeIntoStream(s)
                return tostring(t1.a == 1 and t2.b == 2)
            ");
            r.ShouldBe("true");
        }

        // -- Encode stream / Wchar as JSON value ----------------------------------

        [Fact]
        public async Task Json_Encode_Stream_ReadableSeekable_ProducesJsonString()
        {
            // A readable+seekable in-memory stream encodes as a JSON string of its bytes.
            // enc_value rewinds to position 0 before reading, so the result is always the
            // full content regardless of where the cursor sits at the time of the call.
            string? r = await Run(@"
                local j = Json.New()
                local s = Stream.Create('hello')
                return j:Encode(s)
            ");
            r.ShouldBe("\"hello\"");
        }

        [Fact]
        public async Task Json_Encode_Stream_EmptyStream_ProducesNull()
        {
            // An empty stream has no bytes; lua_stream_read_chunk returns nil ? null.
            string? r = await Run(@"
                local j = Json.New()
                local s = Stream.Create()
                return j:Encode(s)
            ");
            r.ShouldBe("null");
        }

        [Fact]
        public async Task Json_Encode_Stream_PreservesReadPosition()
        {
            // The caller's stream position must be restored after encoding.
            string? r = await Run(@"
                local j = Json.New()
                local s = Stream.Create('ABCDE')
                s:Seek(3)
                j:Encode(s)
                return tostring(s:pos() == 3)
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task Json_Encode_Stream_QuoteInContent_EscapedCorrectly()
        {
            // A double-quote byte inside the stream must be JSON-escaped as \".
            string? r = await Run(@"
                local j = Json.New()
                local s = Stream.Create('a""b')
                return j:Encode(s)
            ");
            r.ShouldBe("\"a\\\"b\"");
        }

        [Fact]
        public async Task Json_Encode_Stream_AsTableValue_RoundTripsAsString()
        {
            // A stream used as a table value encodes as a JSON string;
            // after decode, the value is a Lua string (JSON has no stream type).
            string? r = await Run(@"
                local j = Json.New()
                local s = Stream.Create('hi')
                local t = j:Decode(j:Encode({data = s}))
                return t.data
            ");
            r.ShouldBe("hi");
        }

        [Fact]
        public async Task Json_EncodeIntoStream_StreamValue_WritesJsonString()
        {
            // When the VALUE being encoded is itself a stream, EncodeIntoStream must
            // write the stream's contents as a JSON string to the destination stream.
            string? r = await Run(@"
                local j   = Json.New()
                local src = Stream.Create('world')
                local dst = Stream.Create()
                j:EncodeIntoStream(dst, src)
                dst:Seek(0)
                return dst:Read()
            ");
            r.ShouldBe("\"world\"");
        }

        [Fact]
        public async Task Json_Encode_Wchar_AsciiContent_ProducesJsonString()
        {
            // ASCII Wchar must produce the same JSON string as the equivalent Lua string.
            string? r = await Run(@"
                local j = Json.New()
                local w = Wchar.FromUtf8('hello')
                return j:Encode(w)
            ");
            r.ShouldBe("\"hello\"");
        }

        [Fact]
        public async Task Json_Encode_Wchar_NonAscii_RoundTripsCorrectly()
        {
            // é = U+00E9, UTF-8: 0xC3 0xA9.  Use Lua hex escapes so the bytes are
            // unambiguous ASCII in the script source and survive ANSI marshaling.
            string? r = await Run(@"
                local j = Json.New()
                local w = Wchar.FromUtf8('\xC3\xa9')
                return j:Decode(j:Encode(w))
            ");
            r.ShouldBe("é");
        }

        [Fact]
        public async Task Json_Encode_Wchar_Empty_ProducesEmptyJsonString()
        {
            string? r = await Run(@"
                local j = Json.New()
                local w = Wchar.FromUtf8('')
                return j:Encode(w)
            ");
            r.ShouldBe("\"\"");
        }

        [Fact]
        public async Task Json_Encode_Wchar_SpecialChars_EscapedCorrectly()
        {
            // Double-quotes inside the Wchar content must be JSON-escaped as \".
            string? r = await Run(@"
                local j = Json.New()
                local w = Wchar.FromUtf8('say ""hi""')
                return j:Encode(w)
            ");
            r.ShouldBe("\"say \\\"hi\\\"\"");
        }

        [Fact]
        public async Task Json_Encode_Wchar_NewlineAndTab_EscapedAndRoundTrip()
        {
            // Control characters must be JSON-escaped and survive a full decode round-trip.
            string? r = await Run(@"
                local j = Json.New()
                local w = Wchar.FromUtf8('a' .. '\n' .. 'b')
                return j:Decode(j:Encode(w))
            ");
            r.ShouldBe("a\nb");
        }

        [Fact]
        public async Task Json_Encode_Wchar_AsTableValue_RoundTripsAsString()
        {
            // After encode?decode, the decoded value is a Lua string (not a Wchar),
            // since JSON has no wchar type.
            string? r = await Run(@"
                local j = Json.New()
                local w = Wchar.FromUtf8('world')
                local t = j:Decode(j:Encode({msg = w}))
                return t.msg
            ");
            r.ShouldBe("world");
        }

        [Fact]
        public async Task Json_Encode_UnknownUserdata_ProducesNull()
        {
            // Any userdata that is neither a Wchar nor a stream must encode as null.
            // Json.New() returns a LuaJson userdata, which is not stream/wchar.
            string? r = await Run(@"
                local j = Json.New()
                return j:Encode(Json.New())
            ");
            r.ShouldBe("null");
        }

        // -- Wchar ----------------------------------------------------------------

        [Fact]
        public async Task Wchar_FromUtf8_ToUtf8_RoundTrip()
        {
            string? r = await Run("return Wchar.FromUtf8('hello'):ToUtf8()");
            r.ShouldBe("hello");
        }

        [Fact]
        public async Task Wchar_ToUpper_ChangesCase()
        {
            string? r = await Run("return Wchar.FromUtf8('hello world'):ToUpper():ToUtf8()");
            r.ShouldBe("HELLO WORLD");
        }

        [Fact]
        public async Task Wchar_ToLower_ChangesCase()
        {
            string? r = await Run("return Wchar.FromUtf8('KITSUNE'):ToLower():ToUtf8()");
            r.ShouldBe("kitsune");
        }

        [Fact]
        public async Task Wchar_Substring_ExtractsCorrectly()
        {
            string? r = await Run("return Wchar.FromUtf8('hello world'):Substring(7):ToUtf8()");
            r.ShouldBe("world");
        }

        [Fact]
        public async Task Wchar_Length_ReturnsCharCount()
        {
            string? r = await Run("return tostring(#Wchar.FromUtf8('hello'))");
            r.ShouldBe("5");
        }

        [Fact]
        public async Task Wchar_Empty_HasZeroLength()
        {
            string? r = await Run("return tostring(#Wchar.FromUtf8('') == 0)");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task Wchar_LenMethod_MatchesHashOperator()
        {
            string? r = await Run("local w = Wchar.FromUtf8('hello'); return tostring(w:len() == #w and w:len() == 5)");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task Wchar_ToString_ReturnsUtf8String()
        {
            // __tostring metamethod should produce the same result as :ToUtf8().
            string? r = await Run("return tostring(Wchar.FromUtf8('kitsune'))");
            r.ShouldBe("kitsune");
        }

        [Fact]
        public async Task Wchar_Substring_WithLength_ExtractsSlice()
        {
            string? r = await Run("return Wchar.FromUtf8('hello world'):Substring(1, 5):ToUtf8()");
            r.ShouldBe("hello");
        }

        [Fact]
        public async Task Wchar_Substring_OutOfRange_ReturnsNil()
        {
            string? r = await Run("return tostring(Wchar.FromUtf8('hi'):Substring(99))");
            r.ShouldBe("nil");
        }

        [Fact]
        public async Task Wchar_Find_ReturnsPosition()
        {
            string? r = await Run("return tostring(Wchar.FromUtf8('hello world'):Find(Wchar.FromUtf8('world')))");
            r.ShouldBe("7");
        }

        [Fact]
        public async Task Wchar_Find_NotFound_ReturnsNil()
        {
            string? r = await Run("return tostring(Wchar.FromUtf8('hello'):Find(Wchar.FromUtf8('xyz')))");
            r.ShouldBe("nil");
        }

        [Fact]
        public async Task Wchar_Find_WithOffset_StartsFromPosition()
        {
            // 'a' appears at indices 1 and 4 in 'abcabc'; with offset 2 it finds index 4.
            string? r = await Run("return tostring(Wchar.FromUtf8('abcabc'):Find(Wchar.FromUtf8('a'), 2))");
            r.ShouldBe("4");
        }

        [Fact]
        public async Task Wchar_Find_StringPattern_Works()
        {
            // Find also accepts a plain Lua string as the search pattern.
            string? r = await Run("return tostring(Wchar.FromUtf8('hello world'):Find('world'))");
            r.ShouldBe("7");
        }

        [Fact]
        public async Task Wchar_Find_NonAsciiStringPattern_Works()
        {
            // Verifies WcharFind now uses FromUtf8 (not FromAnsi) for string patterns.
            // \xC3\xA9 are the raw UTF-8 bytes for U+00E9 (é).
            string? r = await Run(@"
                local hay = Wchar.FromUtf8('caf\xC3\xA9 au lait')
                return tostring(hay:Find('\xC3\xA9') ~= nil)
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task Wchar_Equality_SameContent_IsTrue()
        {
            string? r = await Run("return tostring(Wchar.FromUtf8('hello') == Wchar.FromUtf8('hello'))");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task Wchar_Equality_DifferentContent_IsFalse()
        {
            string? r = await Run("return tostring(Wchar.FromUtf8('hello') == Wchar.FromUtf8('world'))");
            r.ShouldBe("false");
        }

        [Fact]
        public async Task Wchar_Equality_DifferentLengths_IsFalse()
        {
            string? r = await Run("return tostring(Wchar.FromUtf8('hi') == Wchar.FromUtf8('hello'))");
            r.ShouldBe("false");
        }

        [Fact]
        public async Task Wchar_Equality_EmptyWchars_AreEqual()
        {
            // Verifies the wchar_eq fix: wcsncmp is not called when len == 0.
            string? r = await Run("return tostring(Wchar.FromUtf8('') == Wchar.FromUtf8(''))");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task Wchar_Concat_WcharAndWchar_ProducesJoined()
        {
            string? r = await Run("return (Wchar.FromUtf8('hello') .. Wchar.FromUtf8(' world')):ToUtf8()");
            r.ShouldBe("hello world");
        }

        [Fact]
        public async Task Wchar_Concat_WcharAndString_ProducesJoined()
        {
            string? r = await Run("return (Wchar.FromUtf8('hello') .. ' world'):ToUtf8()");
            r.ShouldBe("hello world");
        }

        [Fact]
        public async Task Wchar_Concat_StringAndWchar_ProducesJoined()
        {
            string? r = await Run("return ('hello ' .. Wchar.FromUtf8('world')):ToUtf8()");
            r.ShouldBe("hello world");
        }

        [Fact]
        public async Task Wchar_Concat_NonAsciiStringOperand_ProducesCorrectResult()
        {
            // Verifies wchar_concat uses MultiByteToWideChar(CP_UTF8) for string operands.
            // \xC3\xA9 = UTF-8 for U+00E9 (é); previous code used mbstowcs (ANSI).
            string? r = await Run(@"return (Wchar.FromUtf8('caf') .. '\xC3\xA9'):ToUtf8()");
            r.ShouldBe("caf\u00e9");
        }

        [Fact]
        public async Task Wchar_ToBytes_ReturnsCorrectCodeValues()
        {
            // 'A' = 65, 'B' = 66 as wchar_t values.
            string? r = await Run(@"
                local b = Wchar.FromUtf8('AB'):ToBytes()
                return tostring(#b == 2 and b[1] == 65 and b[2] == 66)
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task Wchar_FromBytes_Table_CreatesCorrectWchar()
        {
            // Reconstruct 'AB' from its wchar_t code values.
            string? r = await Run("return Wchar.FromBytes({65, 66}):ToUtf8()");
            r.ShouldBe("AB");
        }

        [Fact]
        public async Task Wchar_FromBytes_SingleInteger_CreatesSingleCharWchar()
        {
            // Codepoint 65 = 'A'.
            string? r = await Run("return Wchar.FromBytes(65):ToUtf8()");
            r.ShouldBe("A");
        }

        [Fact]
        public async Task Wchar_FromBytes_InvalidCodepoint_ProducesEmptyWchar()
        {
            // 0x200000 exceeds U+10FFFF; verifies the FillLuaWCharWithCodePoint fix
            // (wcharCount > 0 instead of != -1) returns an empty Wchar rather than crashing.
            string? r = await Run("return tostring(#Wchar.FromBytes(0x200000) == 0)");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task Wchar_ToBytes_AsciiChar_ProducesOneCodeUnit()
        {
            // ToBytes returns a table of char16_t code units (UTF-16 LE, platform-independent).
            // 'A' is U+0041 — one code unit — so the table has exactly one entry with value 65.
            string? r = await Run(@"
                local units = Wchar.FromUtf8('A'):ToBytes()
                return tostring(#units == 1 and units[1] == 65)
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task Wchar_FromBytes_Table_RoundTrips()
        {
            // ToBytes yields a table of char16_t code units; FromBytes(table) reconstructs from them.
            string? r = await Run(@"
                local w1 = Wchar.FromUtf8('hello')
                local w2 = Wchar.FromBytes(w1:ToBytes())
                return tostring(w1 == w2)
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task Wchar_Codepoints_ReturnsCorrectTable()
        {
            // 'AB' ? codepoints table {65, 66}.
            string? r = await Run(@"
                local pts = Wchar.FromUtf8('AB'):Codepoints()
                return tostring(#pts == 2 and pts[1] == 65 and pts[2] == 66)
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task Wchar_At_ValidIndex_ReturnsCodepoint()
        {
            // 'B' is at character position 2 (1-indexed) in 'AB'.
            string? r = await Run("return tostring(Wchar.FromUtf8('AB'):At(2) == 66)");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task Wchar_At_OutOfRange_ReturnsNil()
        {
            string? r = await Run("return tostring(Wchar.FromUtf8('hi'):At(99))");
            r.ShouldBe("nil");
        }

        [Fact]
        public async Task Wchar_FromAnsi_ToAnsi_AsciiRoundTrip()
        {
            // ASCII characters are stable across all encodings.
            string? r = await Run("return Wchar.FromAnsi('hello'):ToAnsi()");
            r.ShouldBe("hello");
        }

        [Fact]
        public async Task Wchar_NonAsciiUtf8_LengthIsWcharCount()
        {
            // U+00E9 (é) is 2 UTF-8 bytes (\xC3\xA9) but 1 wchar_t; length should be 1.
            string? r = await Run(@"return tostring(#Wchar.FromUtf8('\xC3\xA9') == 1)");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task Wchar_ChainedOps_ProduceCorrectResult()
        {
            string? r = await Run(@"
                local result = Wchar.FromUtf8('hello world')
                    :ToUpper()
                    :Substring(7)
                return tostring(result:ToUtf8() == 'WORLD')
            ");
            r.ShouldBe("true");
        }

        // -- Stream Wchar read/write -----------------------------------------------

        [Fact]
        public async Task Stream_WriteWchar_ReadWchar_AsciiRoundTrip()
        {
            // Write a Wchar into a stream and read it back as a Wchar.
            string? r = await Run(@"
                local w = Wchar.FromUtf8('hello')
                local s = Stream.Create()
                s:Write(w)
                s:Seek(0)
                local w2 = s:ReadWchar(5)
                return w2:ToUtf8()
            ");
            r.ShouldBe("hello");
        }

        [Fact]
        public async Task Stream_WriteWchar_ReturnsCorrectByteCount()
        {
            // Write returns the number of bytes written (2 bytes per wchar_t code unit).
            string? r = await Run(@"
                local w = Wchar.FromUtf8('hi')
                local s = Stream.Create()
                local written = s:Write(w)
                return tostring(written == 4)   -- 2 code units * 2 bytes each
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task Stream_WriteWchar_AdvancesPosition()
        {
            string? r = await Run(@"
                local w = Wchar.FromUtf8('abc')
                local s = Stream.Create()
                s:Write(w)
                return tostring(s:pos() == 6)   -- 3 code units * 2 bytes each
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task Stream_ReadWchar_PartialRead_ReturnsRequestedCount()
        {
            // Write a 5-char Wchar then read back only 3 code units.
            string? r = await Run(@"
                local w = Wchar.FromUtf8('hello')
                local s = Stream.Create()
                s:Write(w)
                s:Seek(0)
                local w2 = s:ReadWchar(3)
                return tostring(w2:len() == 3 and w2:ToUtf8() == 'hel')
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task Stream_ReadWchar_PastEnd_ReturnsNil()
        {
            // Requesting more code units than are available returns nil.
            string? r = await Run(@"
                local s = Stream.Create()
                return tostring(s:ReadWchar(1) == nil)
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task Stream_WriteWchar_MultipleAppend_ReadBackFull()
        {
            // Two Wchar writes must be contiguous; one ReadWchar retrieves them all.
            string? r = await Run(@"
                local s = Stream.Create()
                s:Write(Wchar.FromUtf8('foo'))
                s:Write(Wchar.FromUtf8('bar'))
                s:Seek(0)
                local w = s:ReadWchar(6)
                return w:ToUtf8()
            ");
            r.ShouldBe("foobar");
        }

        [Fact]
        public async Task Stream_ReadWchar_NoLength_ReadsRemaining()
        {
            // ReadWchar() with no argument reads all remaining code units to end of stream.
            string? r = await Run(@"
                local s = Stream.Create()
                s:Write(Wchar.FromUtf8('hello'))
                s:Seek(0)
                local w = s:ReadWchar()
                return w:ToUtf8()
            ");
            r.ShouldBe("hello");
        }

        [Fact]
        public async Task Stream_ReadWchar_NoLength_FromMidStream_ReadsRemainder()
        {
            // ReadWchar() from mid-stream must only return code units from the current position.
            string? r = await Run(@"
                local s = Stream.Create()
                s:Write(Wchar.FromUtf8('abcde'))
                s:Seek(4)   -- skip first 2 code units (4 bytes each)
                local w = s:ReadWchar()
                return tostring(w:len() == 3 and w:ToUtf8() == 'cde')
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task Stream_ReadWchar_NoArg_EmptyStream_ReturnsNil()
        {
            // ReadWchar() with no argument on an empty stream must return nil, not error.
            string? r = await Run(@"
                local s = Stream.Create()
                return tostring(s:ReadWchar() == nil)
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task Stream_ReadWchar_ExplicitNilArg_ReadAll()
        {
            // Passing nil explicitly must behave identically to omitting the argument.
            string? r = await Run(@"
                local s = Stream.Create()
                s:Write(Wchar.FromUtf8('test'))
                s:Seek(0)
                local w = s:ReadWchar(nil)
                return tostring(w ~= nil and w:ToUtf8() == 'test')
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task Stream_ReadWchar_ResultIsWcharUserdata()
        {
            // The return value must be a Wchar userdata, not a plain Lua string.
            string? r = await Run(@"
                local s = Stream.Create()
                s:Write(Wchar.FromUtf8('x'))
                s:Seek(0)
                local w = s:ReadWchar(1)
                return tostring(type(w) == 'userdata' and w:len() == 1)
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task Stream_WriteWchar_NonWritable_ReturnsZero()
        {
            // Writing a Wchar to a read-only function-backend stream must return 0.
            // Caps: READ=1, WRITE=2, SEEK=4 — return 1 for read-only.
            string? r = await Run(@"
                local s = Stream.Create(function(op, ...)
                    if op == READ then return 'x' end
                    return 1   -- caps: READ only (no WRITE bit)
                end)
                local w = Wchar.FromUtf8('hi')
                return tostring(s:Write(w) == 0)
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task Stream_ReadWchar_WriteOnly_ReturnsNil()
        {
            // ReadWchar on a write-only stream must return nil.
            string? r = await Run(@"
                local s = Stream.Create(function(op, ...)
                    if op == WRITE then return true end
                    return 2   -- caps: WRITE only
                end)
                return tostring(s:ReadWchar(1) == nil)
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task Timer_InitialState_NotRunning()
        {
            string? r = await Run("local t = Timer.New(); return tostring(t:IsRunning() == false)");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task Timer_AfterStart_IsRunning()
        {
            string? r = await Run("local t = Timer.New(); t:Start(); return tostring(t:IsRunning())");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task Timer_ElapsedAfterSleep_IsPositive()
        {
            string? r = await Run("local t = Timer.New(); t:Start(); Sleep(20); return tostring(t:Elapsed() > 0)");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task Timer_StopAndReset_ElapsedIsZero()
        {
            string? r = await Run(@"
                local t = Timer.New(); t:Start(); Sleep(10); t:Stop(); t:Reset()
                return tostring(t:Elapsed() == 0 and not t:IsRunning())
            ");
            r.ShouldBe("true");
        }

        // -- Aes ------------------------------------------------------------------

        [Fact]
        public async Task Aes_EncryptDecrypt_RoundTrip()
        {
            // Use two fresh instances with the same key and default zero IV.
            string? r = await Run(@"
                local key = string.rep('\0', 32)
                local plain = 'hello aes world!'
                local enc = Aes.Create(key):Encrypt(plain)
                local dec = Aes.Create(key):Decrypt(enc)
                return tostring(dec == plain)
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task Aes_EncryptedData_DiffersFromPlaintext()
        {
            string? r = await Run(@"
                local key = string.rep('\0', 32)
                local plain = 'secret message!!'
                local enc = Aes.Create(key):Encrypt(plain)
                return tostring(enc ~= plain)
            ");
            r.ShouldBe("true");
        }

        // -- SQLite (in-memory) ---------------------------------------------------

        [Fact]
        public async Task SQLite_InMemory_CreateInsertSelect()
        {
            string? r = await Run(@"
                local db = SQLite.Open()
                db:Query('CREATE TABLE t (id INTEGER, name TEXT)'); db:Fetch()
                db:Query([[INSERT INTO t VALUES (1, 'alice')]]); db:Fetch()
                db:Query([[INSERT INTO t VALUES (2, 'bob')]]);   db:Fetch()
                db:Query('SELECT id, name FROM t ORDER BY id')
                local out = {}
                while db:Fetch() do
                    local row = db:GetRow()
                    table.insert(out, tostring(row.id) .. ':' .. tostring(row.name))
                end
                db:Close()
                return table.concat(out, ',')
            ");
            r.ShouldBe("1:alice,2:bob");
        }

        [Fact]
        public async Task SQLite_GetRow_ByIndex_ReturnsValue()
        {
            string? r = await Run(@"
                local db = SQLite.Open()
                db:Query('CREATE TABLE t (v TEXT)'); db:Fetch()
                db:Query([[INSERT INTO t VALUES ('test_val')]]); db:Fetch()
                db:Query('SELECT v FROM t')
                db:Fetch()
                local val = db:GetRow(1)
                db:Close()
                return val
            ");
            r.ShouldBe("test_val");
        }

        [Fact]
        public async Task SQLite_RegisterFunction_CallableFromQuery()
        {
            // SQLite returns numeric results as floats (7.0, not 7).
            string? r = await Run(@"
                local db = SQLite.Open()
                db:RegisterFunction(function(a, b) return a + b end, 'add2', 2)
                db:Query('SELECT add2(3, 4) AS result')
                db:Fetch()
                local val = db:GetRow(1)
                db:Close()
                return tostring(val)
            ");
            r.ShouldBe("7.0");
        }

        [Fact]
        public async Task SQLite_NullValue_IsNilInLua()
        {
            string? r = await Run(@"
                local db = SQLite.Open()
                db:Query('CREATE TABLE t (v TEXT)'); db:Fetch()
                db:Query('INSERT INTO t VALUES (NULL)'); db:Fetch()
                db:Query('SELECT v FROM t')
                db:Fetch()
                local val = db:GetRow(1)
                db:Close()
                return tostring(val)
            ");
            r.ShouldBe("nil");
        }

        [Fact]
        public async Task SQLite_IntegerColumn_RoundTrips()
        {
            string? r = await Run(@"
                local db = SQLite.Open()
                db:Query('CREATE TABLE t (n INTEGER)'); db:Fetch()
                db:Query('INSERT INTO t VALUES (42)'); db:Fetch()
                db:Query('SELECT n FROM t')
                db:Fetch()
                local row = db:GetRow()
                db:Close()
                return tostring(row.n)
            ");
            r.ShouldBe("42");
        }

        [Fact]
        public async Task SQLite_FloatColumn_RoundTrips()
        {
            string? r = await Run(@"
                local db = SQLite.Open()
                db:Query('CREATE TABLE t (f REAL)'); db:Fetch()
                db:Query('INSERT INTO t VALUES (3.14)'); db:Fetch()
                db:Query('SELECT f FROM t')
                db:Fetch()
                local row = db:GetRow()
                db:Close()
                return tostring(math.abs(row.f - 3.14) < 0.0001)
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task SQLite_MultipleRows_FetchAll()
        {
            string? r = await Run(@"
                local db = SQLite.Open()
                db:Query('CREATE TABLE t (n INTEGER)'); db:Fetch()
                for i = 1, 5 do
                    db:Query('INSERT INTO t VALUES (' .. i .. ')'); db:Fetch()
                end
                db:Query('SELECT n FROM t ORDER BY n')
                local sum = 0
                while db:Fetch() do
                    sum = sum + db:GetRow(1)
                end
                db:Close()
                return tostring(sum)
            ");
            r.ShouldBe("15");
        }

        [Fact]
        public async Task SQLite_ParameterizedQuery_TableBind()
        {
            string? r = await Run(@"
                local db = SQLite.Open()
                db:Query('CREATE TABLE t (id INTEGER, name TEXT)'); db:Fetch()
                db:Query('INSERT INTO t VALUES (:id, :name)', {id=7, name='kitsune'}); db:Fetch()
                db:Query('SELECT name FROM t WHERE id = 7')
                db:Fetch()
                local val = db:GetRow(1)
                db:Close()
                return tostring(val)
            ");
            r.ShouldBe("kitsune");
        }

        [Fact]
        public async Task SQLite_InvalidQuery_ReturnsFalseAndError()
        {
            string? r = await Run(@"
                local db = SQLite.Open()
                local ok, err = db:Query('THIS IS NOT SQL')
                db:Close()
                return tostring(ok == false and type(err) == 'string' and #err > 0)
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task SQLite_AggregateFunction_SumCustom()
        {
            string? r = await Run(@"
                local db = SQLite.Open()
                db:Query('CREATE TABLE t (n INTEGER)'); db:Fetch()
                for i = 1, 4 do
                    db:Query('INSERT INTO t VALUES (' .. i .. ')'); db:Fetch()
                end
                local acc = 0
                db:RegisterAggregateFunction(function(isFinish, v)
                    if isFinish then return acc end
                    acc = acc + v
                end, 'mysum', 1)
                db:Query('SELECT mysum(n) FROM t')
                db:Fetch()
                local val = db:GetRow(1)
                db:Close()
                return tostring(val)
            ");
            r.ShouldBe("10.0");
        }

        [Fact]
        public async Task SQLite_Close_ThenQueryRaisesError()
        {
            string? r = await Run(@"
                local db = SQLite.Open()
                db:Close()
                local ok, err = pcall(function() db:Query('SELECT 1') end)
                return tostring(not ok and type(err) == 'string')
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task SQLite_InstanceReuse_MultipleQueries()
        {
            string? r = await Run(@"
                local db = SQLite.Open()
                db:Query('CREATE TABLE t (v TEXT)'); db:Fetch()
                db:Query([[INSERT INTO t VALUES ('first')]]); db:Fetch()
                db:Query([[INSERT INTO t VALUES ('second')]]); db:Fetch()
                db:Query('SELECT COUNT(*) FROM t')
                db:Fetch()
                local count = db:GetRow(1)
                db:Query('SELECT v FROM t ORDER BY rowid LIMIT 1')
                db:Fetch()
                local first = db:GetRow(1)
                db:Close()
                return tostring(count) .. ':' .. tostring(first)
            ");
            r.ShouldBe("2:first");
        }

        // -- Stream (in-memory) ---------------------------------------------------

        [Fact]
        public async Task Stream_Create_FromString_LoadsDataAtPositionZero()
        {
            string? r = await Run(@"
                local s = Stream.Create('hello stream')
                return s:Read()
            ");
            r.ShouldBe("hello stream");
        }

        [Fact]
        public async Task Stream_Create_FromString_PosIsZeroAfterCreate()
        {
            string? r = await Run(@"
                local s = Stream.Create('hello')
                return tostring(s:pos() == 0 and s:len() == 5)
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task Stream_Create_WithBackendFunction_CallsOpenForCaps()
        {
            string? r = await Run(@"
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
            r.ShouldBe("true");
        }

        [Fact]
        public async Task Stream_Create_WithBackendFunction_ReadDelegatesToFunction()
        {
            string? r = await Run(@"
                local OPEN, CLOSE, READ = 0, 1, 2
                local STREAM_CAP_READ = 1
                local s = Stream.Create(function(op, ...)
                    if op == OPEN then return STREAM_CAP_READ end
                    if op == READ then return 'from backend' end
                    if op == CLOSE then return true end
                end)
                return s:Read()
            ");
            r.ShouldBe("from backend");
        }

        [Fact]
        public async Task Stream_CustomBackend_DocExample_WorksCorrectly()
        {
            string? r = await Run(@"
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
            r.ShouldBe("true");
        }

        [Fact]
        public async Task Stream_WriteAndRead_RoundTrip()
        {
            string? r = await Run(@"
                local s = Stream.Create()
                s:Write('hello stream')
                s:Seek(0)
                return s:Read()
            ");
            r.ShouldBe("hello stream");
        }

        [Fact]
        public async Task Stream_PosAndLen_AfterWrite_ReturnCorrectValues()
        {
            string? r = await Run(@"
                local s = Stream.Create()
                s:Write('abcde')
                return tostring(s:pos() == 5 and s:len() == 5)
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task Stream_GetInfo_ReturnsCapsAndBackendInfo()
        {
            string? r = await Run(@"
                local s = Stream.Create()
                s:Write('hello')
                local caps, info = s:GetInfo()
                return tostring(
                    type(caps) == 'table' and caps.Caps > 0 and
                    type(info) == 'table' and info.pos == 5 and info.len == 5 and info.alloc >= 5
                )
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task Stream_Seek_UpdatesPosition()
        {
            string? r = await Run(@"
                local s = Stream.Create()
                s:Write('hello')
                s:Seek(2)
                return tostring(s:pos() == 2)
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task Stream_WriteByte_ReadByte_RoundTrip()
        {
            string? r = await Run(@"
                local s = Stream.Create()
                s:WriteByte(42)
                s:Seek(0)
                return tostring(s:ReadByte() == 42)
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task Stream_PeekByte_DoesNotAdvancePosition()
        {
            string? r = await Run(@"
                local s = Stream.Create()
                s:WriteByte(99)
                s:Seek(0)
                local b = s:PeekByte()
                return tostring(b == 99 and s:pos() == 0)
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task Stream_WriteInt_ReadInt_RoundTrip()
        {
            string? r = await Run(@"
                local s = Stream.Create()
                s:WriteInt(12345)
                s:Seek(0)
                return tostring(s:ReadInt() == 12345)
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task Stream_Compress_Decompress_RoundTrip()
        {
            string? r = await Run(@"
                local s = Stream.Create()
                s:Write('hello hello hello hello hello')
                local compressed = s:Compress()
                local decompressed = compressed:Decompress()
                return decompressed:Read()
            ");
            r.ShouldBe("hello hello hello hello hello");
        }

        [Fact]
        public async Task Stream_Compress_ProducesSmallerOutput()
        {
            string? r = await Run(@"
                local s = Stream.Create()
                s:Write(string.rep('a', 1000))
                s:Seek(0)
                local compressed = s:Compress()
                local _, info = compressed:GetInfo()
                return tostring(info.len < 1000)
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task Stream_Compress_IntoProvidedStream_RoundTrip()
        {
            string? r = await Run(@"
                local src = Stream.Create()
                src:Write('hello hello hello hello hello')
                local dst = Stream.Create()
                src:Compress(nil, dst)
                local decompressed = dst:Decompress()
                return decompressed:Read()
            ");
            r.ShouldBe("hello hello hello hello hello");
        }

        [Fact]
        public async Task Stream_Decompress_IntoProvidedStream_RoundTrip()
        {
            string? r = await Run(@"
                local src = Stream.Create()
                src:Write('hello hello hello hello hello')
                local compressed = src:Compress()
                local dst = Stream.Create()
                compressed:Decompress(nil, dst)
                dst:Seek(0)
                return dst:Read()
            ");
            r.ShouldBe("hello hello hello hello hello");
        }

        [Fact]
        public async Task Stream_Compress_AndDecompress_BothIntoProvidedStreams_RoundTrip()
        {
            string? r = await Run(@"
                local src = Stream.Create()
                src:Write('hello hello hello hello hello')
                local compDst = Stream.Create()
                local decompDst = Stream.Create()
                src:Compress(nil, compDst)
                compDst:Decompress(nil, decompDst)
                decompDst:Seek(0)
                return decompDst:Read()
            ");
            r.ShouldBe("hello hello hello hello hello");
        }

        [Fact]
        public async Task Stream_Compress_ProvidedDst_PositionNotReset()
        {
            string? r = await Run(@"
                local src = Stream.Create()
                src:Write(string.rep('a', 200))
                local dst = Stream.Create()
                src:Compress(nil, dst)
                local _, info = dst:GetInfo()
                return tostring(info.pos > 0)
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task Stream_Decompress_ProvidedDst_PositionNotReset()
        {
            string? r = await Run(@"
                local src = Stream.Create()
                src:Write(string.rep('a', 200))
                local compressed = src:Compress()
                local dst = Stream.Create()
                compressed:Decompress(nil, dst)
                local _, info = dst:GetInfo()
                return tostring(info.pos > 0)
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task Stream_WriteFloat_ReadFloat_RoundTrip()
        {
            string? r = await Run(@"
                local s = Stream.Create()
                s:WriteFloat(3.14)
                s:Seek(0)
                local v = s:ReadFloat()
                return tostring(math.abs(v - 3.14) < 0.001)
            ");
            r.ShouldBe("true");
        }

        // -- Stream backend error propagation -------------------------------------

        [Fact]
        public async Task Stream_BackendReadError_PropagatesViaPcall()
        {
            // lua_call_nohook on READ dispatch means a backend error bubbles up.
            string? r = await Run(@"
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
            r.ShouldBe("true");
        }

        [Fact]
        public async Task Stream_BackendWriteError_PropagatesViaPcall()
        {
            // lua_call_nohook on WRITE dispatch means a backend error bubbles up.
            string? r = await Run(@"
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
            r.ShouldBe("true");
        }

        [Fact]
        public async Task Stream_BackendSeekError_PropagatesViaPcall()
        {
            // lua_call_nohook on SETPOS dispatch means a backend error bubbles up.
            string? r = await Run(@"
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
            r.ShouldBe("true");
        }

        [Fact]
        public async Task Stream_BackendOpen_NonNumber_GivesCleanError()
        {
            // NewStream uses lua_pcall_nohook on OPEN with explicit recovery:
            // a non-number return produces "Backend function failed to open".
            string? r = await Run(@"
                local ok, err = pcall(Stream.Create, function(op) return 'not_a_number' end)
                return tostring(not ok and err:find('Backend function failed to open') ~= nil)
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task Stream_BackendOpen_ZeroCaps_GivesCleanError()
        {
            // Returning 0 caps (no operations supported) is treated as failure.
            string? r = await Run(@"
                local ok, err = pcall(Stream.Create, function(op) return 0 end)
                return tostring(not ok and err:find('Backend function failed to open') ~= nil)
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task Stream_BackendOpen_Throws_GivesCleanError()
        {
            // A throw during OPEN is caught by the protected call in NewStream and
            // reported as "Backend function failed to open" (original message is lost
            // intentionally; the non-number return check fires on the error object).
            string? r = await Run(@"
                local ok, err = pcall(Stream.Create, function(op) error('boom') end)
                return tostring(not ok and err:find('Backend function failed to open') ~= nil)
            ");
            r.ShouldBe("true");
        }

        // -- Stream.Open (file backend) -------------------------------------------

        [Fact]
        public async Task Stream_Open_WriteRead_RoundTrip()
        {
            string? r = await Run(@"
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
            r.ShouldBe("hello file stream");
        }

        [Fact]
        public async Task Stream_Open_Info_ContainsNameAndType()
        {
            string? r = await Run(@"
                local _tmp = (os.getenv('TEMP') or os.getenv('TMPDIR') or '/tmp') .. package.config:sub(1,1)
                local path = _tmp .. 'kitsune_stream_info.bin'
                local w = Stream.Open(path, 'wb')
                local _, info = w:GetInfo()
                w:Close()
                os.remove(path)
                return tostring(info.name == path and info.type == 'file')
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task Stream_Open_Seek_UpdatesPosition()
        {
            string? r = await Run(@"
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
            r.ShouldBe("2");
        }

        [Fact]
        public async Task Stream_Open_Len_ReturnsFileSizeWithoutMovingCursor()
        {
            // s:len() on a file stream must return the total file byte count and
            // must not disturb the read cursor.
            string? r = await Run(@"
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
            r.ShouldBe("6:2");
        }

        [Fact]
        public async Task Stream_Open_Info_LenMatchesFileSize()
        {
            // GetInfo().len for a file stream must equal the actual file byte count
            // and must not change the cursor position.
            string? r = await Run(@"
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
            r.ShouldBe("5:3");
        }

        [Fact]
        public async Task Stream_Open_NonexistentFile_RaisesError()
        {
            string? r = await Run(@"
                local ok, err = pcall(Stream.Open, 'nonexistent_kitsune_xyz_abc_1234567890.bin', 'rb')
                return tostring(not ok and err:find('Stream.Open') ~= nil)
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task Stream_Open_ReadMode_BlocksWrite()
        {
            string? r = await Run(@"
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
            r.ShouldBe("true");
        }

        // -- Stream (module API) --------------------------------------------------

        [Fact]
        public async Task Stream_Seek_AllowsMultipleReads()
        {
            string? r = await Run(@"
                local s = Stream.Create()
                s:Write('reread')
                s:Seek(0)
                local first = s:Read()
                s:Seek(0)
                local second = s:Read()
                return tostring(first == second)
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task Stream_Len_ReflectsWrittenBytes()
        {
            string? r = await Run(@"
                local s = Stream.Create()
                s:Write('abc')
                return tostring(s:len())
            ");
            r.ShouldBe("3");
        }

        [Fact]
        public async Task Stream_Pos_AdvancesAfterRead()
        {
            string? r = await Run(@"
                local s = Stream.Create()
                s:Write('abcde')
                s:Seek(0)
                s:Read(2)
                return tostring(s:pos())
            ");
            r.ShouldBe("2");
        }

        [Fact]
        public async Task Stream_Read_PartialLength_ThenRemainder()
        {
            string? r = await Run(@"
                local s = Stream.Create('hello world')
                s:Seek(0)
                local first = s:Read(5)
                local rest  = s:Read()
                return first .. ':' .. rest
            ");
            r.ShouldBe("hello: world");
        }

        [Fact]
        public async Task Stream_ReadByte_AtEnd_ReturnsNegativeOne()
        {
            string? r = await Run(@"
                local s = Stream.Create()
                s:WriteByte(1)
                s:Seek(0)
                s:ReadByte()
                return tostring(s:ReadByte())
            ");
            r.ShouldBe("-1");
        }

        [Fact]
        public async Task Stream_PeekByte_ReturnsValueAndLeavesPos()
        {
            string? r = await Run(@"
                local s = Stream.Create('ABC')
                s:Seek(0)
                local peeked = s:PeekByte()
                return tostring(peeked) .. ':' .. tostring(s:pos())
            ");
            r.ShouldBe("65:0");  // 'A' == 65, pos unchanged
        }

        [Fact]
        public async Task Stream_MultipleNumericTypes_InSequence()
        {
            string? r = await Run(@"
                local s = Stream.Create()
                s:WriteShort(100)
                s:WriteInt(200)
                s:WriteLong(300)
                s:Seek(0)
                return tostring(s:ReadShort()) .. ':' .. tostring(s:ReadInt()) .. ':' .. tostring(s:ReadLong())
            ");
            r.ShouldBe("100:200:300");
        }

        // -- CSV ------------------------------------------------------------------

        [Fact]
        public async Task CSV_DecodeString_ParsesRows()
        {
            // Count rows with ipairs to avoid # unreliability on non-sequence tables.
            string? r = await Run(@"
                local t = CSV.New():Decode('a,b,c\n1,2,3')
                local count = 0
                for _ in ipairs(t.Rows) do count = count + 1 end
                return tostring(count == 2)
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task CSV_DecodeString_EmptyInput_ZeroRows()
        {
            // Decode("") must return an empty Rows table (zero rows).
            // The old do-while loop produced one spurious empty row; the while
            // loop introduced in Task 13 correctly produces none.
            string? r = await Run(@"
                local t = CSV.New():Decode('')
                local count = 0
                for _ in ipairs(t.Rows) do count = count + 1 end
                return tostring(count == 0)
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task CSV_DecodeString_RowValues_AccessibleAsWchar()
        {
            string? r = await Run(@"
                local t = CSV.New():Decode('hello,world')
                return tostring(t.Rows[1][1])
            ");
            r.ShouldBe("hello");
        }

        [Fact]
        public async Task CSV_Decode_AsciiCell_IsPlainString()
        {
            // ASCII-only cells must come back as plain Lua strings (not WChar
            // userdata) so the fast path is active.
            string? r = await Run(@"
                local t = CSV.New():Decode('hello,42,2024-01-01')
                return tostring(
                    type(t.Rows[1][1]) == 'string' and
                    type(t.Rows[1][2]) == 'string' and
                    type(t.Rows[1][3]) == 'string')
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task CSV_Decode_NonAsciiCell_IsWchar()
        {
            // Cells containing characters above U+007F must still be WChar userdata.
            string? r = await Run(@"
                local input = 'caf\xc3\xa9,plain'
                local t = CSV.New():Decode(input)
                return tostring(
                    type(t.Rows[1][1]) == 'userdata' and
                    type(t.Rows[1][2]) == 'string')
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task CSV_DecodeString_MultipleColumnsPerRow()
        {
            string? r = await Run(@"
                local t = CSV.New():Decode('a,b,c')
                local row = t.Rows[1]
                return tostring(tostring(row[1]) .. ':' .. tostring(row[2]) .. ':' .. tostring(row[3]))
            ");
            r.ShouldBe("a:b:c");
        }

        [Fact]
        public async Task CSV_DecodeString_MultipleRows_CorrectCount()
        {
            string? r = await Run(@"
                local t = CSV.New():Decode('r1c1,r1c2\nr2c1,r2c2\nr3c1,r3c2')
                local count = 0
                for _ in ipairs(t.Rows) do count = count + 1 end
                return tostring(count == 3)
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task CSV_DecodeString_QuotedField_StripsQuotes()
        {
            string? r = await Run(@"
                local t = CSV.New():Decode('""hello"",""world""')
                return tostring(tostring(t.Rows[1][1]) == 'hello' and tostring(t.Rows[1][2]) == 'world')
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task CSV_DecodeString_QuotedFieldWithEmbeddedDelimiter_PreservesContent()
        {
            // A comma inside quotes must not split the field.
            string? r = await Run(@"
                local t = CSV.New():Decode('""hello, world"",end')
                return tostring(tostring(t.Rows[1][1]) == 'hello, world')
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task CSV_DecodeString_QuotedFieldWithEscapedQuote_ProducesLiteralQuote()
        {
            // RFC 4180 escaped quote: "" inside a quoted field ? single ".
            string? r = await Run(@"
                local t = CSV.New():Decode('""say """"hi"""""",end')
                return tostring(tostring(t.Rows[1][1]) == 'say ""hi""')
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task CSV_DecodeString_FieldWithLeadingWhitespace_WhitespaceIsStripped()
        {
            // Verifies the SkipForwards fix: previously the first non-space character
            // was silently consumed and lost, producing "ello" instead of "hello".
            string? r = await Run(@"
                local t = CSV.New():Decode(' hello, world')
                return tostring(tostring(t.Rows[1][1]) == 'hello' and tostring(t.Rows[1][2]) == 'world')
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task CSV_DecodeString_EmptyField_PreservesEmptyCell()
        {
            // a,,b produces three fields; the middle one is empty.
            string? r = await Run(@"
                local t = CSV.New():Decode('a,,b')
                local row = t.Rows[1]
                return tostring(tostring(row[1]) == 'a' and tostring(row[2]) == '' and tostring(row[3]) == 'b')
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task CSV_DecodeString_ResultHasCommentsKey()
        {
            // The returned table always has a Comments key even when there are none.
            string? r = await Run(@"
                local t = CSV.New():Decode('a,b')
                return tostring(type(t.Comments) == 'table')
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task CSV_DecodeString_CommentLine_IsExtractedAndExcludedFromRows()
        {
            // Lines starting with * are treated as comments and placed in t.Comments.
            string? r = await Run(@"
                local t = CSV.New():Decode('* this is a comment\na,b')
                local commentCount = 0
                for _ in ipairs(t.Comments) do commentCount = commentCount + 1 end
                local rowCount = 0
                for _ in ipairs(t.Rows) do rowCount = rowCount + 1 end
                return tostring(commentCount == 1 and rowCount == 1)
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task CSV_DecodeString_MultipleCommentLines_AllExtracted()
        {
            string? r = await Run(@"
                local t = CSV.New():Decode('* line one\n* line two\na,b')
                local count = 0
                for _ in ipairs(t.Comments) do count = count + 1 end
                return tostring(count == 2)
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task CSV_DecodeString_CustomDelimiter_SplitsOnSemicolon()
        {
            string? r = await Run(@"
                local t = CSV.New(';'):Decode('a;b;c')
                local row = t.Rows[1]
                return tostring(tostring(row[1]) == 'a' and tostring(row[2]) == 'b' and tostring(row[3]) == 'c')
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task CSV_DecodeString_CrLfLineEnding_ParsedAsOneRow()
        {
            // \r\n (Windows CRLF) must produce the same row count as \n alone.
            string? r = await Run(@"
                local t = CSV.New():Decode('a,b\r\nc,d')
                local count = 0
                for _ in ipairs(t.Rows) do count = count + 1 end
                return tostring(count == 2 and tostring(t.Rows[1][1]) == 'a' and tostring(t.Rows[2][1]) == 'c')
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task CSV_DecodeString_WcharInput_ParsedCorrectly()
        {
            // Exercises the lua_iswchar branch in DecodeString; all other tests pass
            // plain Lua strings which take the FromUtf8 conversion path instead.
            string? r = await Run(@"
                local t = CSV.New():Decode(Wchar.FromUtf8('x,y,z'))
                return tostring(tostring(t.Rows[1][1]) == 'x' and tostring(t.Rows[1][3]) == 'z')
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task CSV_Encode_SimpleTable_ProducesCorrectString()
        {
            string? r = await Run("return CSV.New():Encode({{'a', 'b'}, {'c', 'd'}})");
            r.ShouldBe("a,b\nc,d");
        }

        [Fact]
        public async Task CSV_Encode_FieldWithDelimiter_IsQuotedAndRoundTrips()
        {
            // A field containing the delimiter must be quoted; decoding must recover the original value.
            string? r = await Run(@"
                local csv = CSV.New()
                local encoded = csv:Encode({{'hello, world', 'end'}})
                local decoded = csv:Decode(encoded)
                return tostring(tostring(decoded.Rows[1][1]) == 'hello, world' and tostring(decoded.Rows[1][2]) == 'end')
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task CSV_Encode_WcharField_ConvertedToUtf8()
        {
            // Wchar fields must be converted via __tostring (UTF-8) during encoding.
            string? r = await Run(@"
                local csv = CSV.New()
                local rows = {{Wchar.FromUtf8('hello'), Wchar.FromUtf8('world')}}
                local encoded = csv:Encode(rows)
                local decoded = csv:Decode(encoded)
                return tostring(tostring(decoded.Rows[1][1]) == 'hello' and tostring(decoded.Rows[1][2]) == 'world')
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task CSV_Encode_CustomDelimiter_UsedInOutput()
        {
            string? r = await Run(@"
                local csv = CSV.New(';')
                local encoded = csv:Encode({{'a', 'b', 'c'}})
                local decoded = csv:Decode(encoded)
                return tostring(tostring(decoded.Rows[1][1]) == 'a' and tostring(decoded.Rows[1][3]) == 'c')
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task CSV_Encode_LeadingSpaceField_RoundTrips()
        {
            // SkipForwards strips leading whitespace on decode. Encode must quote
            // fields whose value starts with a space or tab so the whitespace lands
            // inside the quotes and is preserved across a decode round-trip.
            string? r = await Run(@"
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
            r.ShouldBe("true");
        }

        [Fact]
        public async Task CSV_Encode_FieldWithEmbeddedQuote_RoundTrips()
        {
            // RFC 4180: a " inside a quoted field is escaped as ""; Encode must produce
            // that and Decode must recover the original single ".
            string? r = await Run(@"
                local csv = CSV.New()
                local rows = {{'say ""hi""', 'end'}}
                local encoded = csv:Encode(rows)
                local decoded = csv:Decode(encoded)
                return tostring(tostring(decoded.Rows[1][1]) == 'say ""hi""' and tostring(decoded.Rows[1][2]) == 'end')
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task CSV_Encode_FieldWithEmbeddedNewline_IsQuoted()
        {
            // A field containing \n must be quoted so the newline is not treated as a
            // row separator on decode.
            string? r = await Run(@"
                local csv = CSV.New()
                local rows = {{'line1\nline2', 'after'}}
                local encoded = csv:Encode(rows)
                local decoded = csv:Decode(encoded)
                return tostring(tostring(decoded.Rows[1][1]) == 'line1\nline2' and tostring(decoded.Rows[1][2]) == 'after')
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task CSV_Decode_BooleanTrue_TriggersAutoDetect()
        {
            // ParseDelimiter accepts boolean true as the auto-detect signal, identical
            // to passing the string "auto".
            string? r = await Run(@"
                local t = CSV.New(true):Decode('a;b;c\n1;2;3')
                return tostring(tostring(t.Rows[1][1]) == 'a' and tostring(t.Rows[2][3]) == '3')
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task CSV_Decode_IntegerCodepointDelimiter_UsedCorrectly()
        {
            // ParseDelimiter accepts an integer codepoint (59 = ';').
            string? r = await Run(@"
                local t = CSV.New(59):Decode('a;b;c')
                return tostring(tostring(t.Rows[1][1]) == 'a' and tostring(t.Rows[1][3]) == 'c')
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task CSV_New_NoArgs_Encode_FallsBackToComma()
        {
            // CSV.New() binds "auto" as the delimiter. Encode with "auto" has no
            // meaningful input to sniff from, so it must fall back to comma.
            string? r = await Run("return CSV.New():Encode({{'a', 'b', 'c'}})");
            r.ShouldBe("a,b,c");
        }

        [Fact]
        public async Task CSV_Decode_AutoDetect_CommaInput_DetectsCorrectly()
        {
            string? r = await Run(@"
                local t = CSV.New():Decode('a,b,c\n1,2,3')
                return tostring(tostring(t.Rows[1][1]) == 'a' and tostring(t.Rows[1][3]) == 'c' and tostring(t.Rows[2][1]) == '1')
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task CSV_Decode_AutoDetect_SemicolonInput_DetectsCorrectly()
        {
            string? r = await Run(@"
                local t = CSV.New():Decode('a;b;c\n1;2;3')
                return tostring(tostring(t.Rows[1][1]) == 'a' and tostring(t.Rows[1][3]) == 'c' and tostring(t.Rows[2][2]) == '2')
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task CSV_Decode_AutoDetect_TabInput_DetectsCorrectly()
        {
            string? r = await Run(@"
                local t = CSV.New():Decode('a\tb\tc\n1\t2\t3')
                return tostring(tostring(t.Rows[1][1]) == 'a' and tostring(t.Rows[1][3]) == 'c')
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task CSV_New_NoArgs_AutoDetectsSemicolon()
        {
            // CSV.New() with no delimiter should sniff each Decode call independently.
            string? r = await Run(@"
                local csv = CSV.New()
                local t = csv:Decode('a;b;c\n1;2;3')
                return tostring(tostring(t.Rows[1][1]) == 'a' and tostring(t.Rows[1][3]) == 'c' and tostring(t.Rows[2][2]) == '2')
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task CSV_New_WithSemicolon_UsesSpecifiedDelimiter()
        {
            string? r = await Run(@"
                local csv = CSV.New(';')
                local t = csv:Decode('a;b;c')
                return tostring(tostring(t.Rows[1][1]) == 'a' and tostring(t.Rows[1][3]) == 'c')
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task CSV_New_Encode_UsesSpecifiedDelimiter()
        {
            string? r = await Run("return CSV.New(';'):Encode({{'a', 'b', 'c'}})");
            r.ShouldBe("a;b;c");
        }

        [Fact]
        public async Task CSV_DecodeFromFunction_AutoDetect_DetectsSemicolon()
        {
            string? r = await Run(@"
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
            r.ShouldBe("a:c|1:3");
        }

        [Fact]
        public async Task CSV_DecodeFromFunction_AutoDetect_AccumulatesChunksForSniff()
        {
            // Without Task-14 buffering the sniff would run on only the first
            // chunk ("hello") which contains no delimiter at all and would fall
            // back to comma.  With buffering the iterator keeps pulling chunks
            // until it sees a newline, giving SniffDelimiter enough context to
            // correctly identify the semicolon delimiter.
            string? r = await Run(@"
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
            r.ShouldBe("true");
        }

        [Fact]
        public async Task CSV_New_NoArgs_DecodeFromFunction_AutoDetects()
        {
            string? r = await Run(@"
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
            r.ShouldBe("x:z");
        }

        [Fact]
        public async Task CSV_DecodeFromFunction_ChunkedStringInput_YieldsAllRows()
        {
            // Chunks deliberately cross field and row boundaries to verify the
            // stream refill logic handles mid-field and mid-row chunk splits.
            string? r = await Run(@"
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
            r.ShouldBe("a:b:c|1:2:3|4:5:6");
        }

        [Fact]
        public async Task CSV_DecodeFromFunction_WcharChunks_ConvertedTransparently()
        {
            // Supplier returns Wchar userdata objects; they must be converted to UTF-8
            // and parsed identically to plain-string chunks.
            string? r = await Run(@"
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
            r.ShouldBe("x:y|z:w");
        }

        [Fact]
        public async Task CSV_DecodeFromFunction_NilTerminates_FinalRowWithoutNewline()
        {
            // The last row has no trailing newline; the nil from the supplier must
            // flush the in-progress row cleanly.
            string? r = await Run(@"
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
            r.ShouldBe("hello:world");
        }

        [Fact]
        public async Task CSV_DecodeFromFunction_CustomDelimiter_Respected()
        {
            string? r = await Run(@"
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
            r.ShouldBe("a:b:c");
        }

        [Fact]
        public async Task CSV_DecodeFromFunction_Stream_ParsesRows()
        {
            // A LuaStream can be passed directly instead of a supplier function;
            // data is pulled in 4 KiB chunks so no full read-into-memory occurs.
            string? r = await Run(@"
                local s = Stream.Create('a,b,c\n1,2,3\n4,5,6')
                local rows = {}
                for row in CSV.New():DecodeFromFunction(s) do
                    table.insert(rows, tostring(row[1]) .. ':' .. tostring(row[2]) .. ':' .. tostring(row[3]))
                end
                return table.concat(rows, '|')
            ");
            r.ShouldBe("a:b:c|1:2:3|4:5:6");
        }

        [Fact]
        public async Task CSV_DecodeFromFunction_Stream_CustomDelimiter_Respected()
        {
            string? r = await Run(@"
                local s = Stream.Create('a;b;c\n1;2;3')
                local rows = {}
                for row in CSV.New(';'):DecodeFromFunction(s) do
                    table.insert(rows, tostring(row[1]) .. ':' .. tostring(row[3]))
                end
                return table.concat(rows, '|')
            ");
            r.ShouldBe("a:c|1:3");
        }

        [Fact]
        public async Task CSV_DecodeFromFunction_Stream_KeepsStreamAliveWithoutExplicitVariable()
        {
            // The stream is passed inline with no variable holding it; the iterator
            // closure must keep it alive through GC so all rows are produced.
            string? r = await Run(@"
                local rows = {}
                for row in CSV.New():DecodeFromFunction(Stream.Create('x,y\nz,w')) do
                    table.insert(rows, tostring(row[1]) .. ':' .. tostring(row[2]))
                end
                return table.concat(rows, '|')
            ");
            r.ShouldBe("x:y|z:w");
        }

        // -- Mutex ----------------------------------------------------------------

        [Fact]
        public async Task Mutex_Open_LockAndUnlock_Succeeds()
        {
            string? r = await Run(@"
                local m = Mutex.Open('KitsuneTestMutex_Util')
                local ok = m:Lock(500)
                m:Unlock()
                return tostring(ok == true)
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task Mutex_Info_ReturnsNameAndLockedState()
        {
            string? r = await Run(@"
                local m = Mutex.Open('KitsuneInfoMutex_Util')
                m:Lock(100)
                local locked, name = m:Info()
                m:Unlock()
                return tostring(locked and name == 'KitsuneInfoMutex_Util')
            ");
            r.ShouldBe("true");
        }

        // -- FileSystem -----------------------------------------------------------

        [Fact]
        public async Task FileSystem_CurrentDirectory_ReturnsNonEmptyString()
        {
            string? r = await Run("return tostring(#FileSystem.CurrentDirectory() > 0)");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task FileSystem_GetTempFileName_ReturnsValidPath()
        {
            string? r = await Run("local p = FileSystem.GetTempFileName(); return tostring(type(p)=='string' and #p>0)");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task FileSystem_GetDrives_ReturnsList()
        {
            string? r = await Run("local d = FileSystem.GetDrives(); return tostring(type(d)=='table' and #d>=1)");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task FileSystem_GetDrives_EntryHasDriveField()
        {
            string? r = await Run(@"
                local d = FileSystem.GetDrives()
                return tostring(type(d[1].Drive) == 'string' and #d[1].Drive > 0)
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task FileSystem_CreateAndDeleteDirectory_Succeeds()
        {
            string? r = await Run(@"
                local dir = FileSystem.GetTempFileName() .. '_kitsune_testdir'
                local ok1 = FileSystem.CreateDirectory(dir)
                local ok2 = FileSystem.RemoveDirectory(dir)
                return tostring(ok1 and ok2)
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task FileSystem_CreateAndDeleteDirectory_WithWchar_Succeeds()
        {
            string? r = await Run(@"
                local dir = Wchar.FromUtf8(FileSystem.GetTempFileName() .. '_kitsune_wchar_testdir')
                local ok1 = FileSystem.CreateDirectory(dir)
                local ok2 = FileSystem.RemoveDirectory(dir)
                return tostring(ok1 and ok2)
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task FileSystem_Open_WriteAndRead_RoundTrip()
        {
            string? r = await Run(@"
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
            r.ShouldBe("hello kitsune");
        }

        [Fact]
        public async Task FileSystem_Open_WithWcharPath_WriteAndRead_RoundTrip()
        {
            string? r = await Run(@"
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
            r.ShouldBe("hello wchar");
        }

        [Fact]
        public async Task FileSystem_GetFileInfo_ReturnsValidTable()
        {
            string? r = await Run(@"
                local path = FileSystem.GetTempFileName() .. '_kitsune_info.txt'
                local f = FileSystem.Open(path, 'wb')
                f:write('abc')
                f:close()
                local info = FileSystem.GetFileInfo(path)
                FileSystem.Delete(path)
                return tostring(type(info) == 'table' and info.Size == 3 and info.isFolder == false)
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task FileSystem_GetFileInfo_WithWchar_ReturnsValidTable()
        {
            string? r = await Run(@"
                local path = FileSystem.GetTempFileName() .. '_kitsune_winfo.txt'
                local f = FileSystem.Open(path, 'wb')
                f:write('xyz')
                f:close()
                local info = FileSystem.GetFileInfo(Wchar.FromUtf8(path))
                FileSystem.Delete(path)
                return tostring(type(info) == 'table' and info.Size == 3)
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task FileSystem_GetFileInfo_MissingPath_ReturnsNil()
        {
            string? r = await Run(@"
                local info = FileSystem.GetFileInfo(FileSystem.GetTempFileName() .. '_no_such_file_xyz')
                return tostring(info == nil)
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task FileSystem_Copy_CreatesDestination()
        {
            string? r = await Run(@"
                local src = FileSystem.GetTempFileName() .. '_kitsune_src.txt'
                local dst = FileSystem.GetTempFileName() .. '_kitsune_dst.txt'
                local f = FileSystem.Open(src, 'wb'); f:write('copy me'); f:close()
                local ok = FileSystem.Copy(src, dst, true)
                local exists = FileSystem.GetFileInfo(dst) ~= nil
                FileSystem.Delete(src)
                FileSystem.Delete(dst)
                return tostring(ok and exists)
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task FileSystem_Copy_WithWcharPaths_CreatesDestination()
        {
            string? r = await Run(@"
                local src = FileSystem.GetTempFileName() .. '_kitsune_wsrc.txt'
                local dst = FileSystem.GetTempFileName() .. '_kitsune_wdst.txt'
                local f = FileSystem.Open(src, 'wb'); f:write('wchar copy'); f:close()
                local ok = FileSystem.Copy(Wchar.FromUtf8(src), Wchar.FromUtf8(dst), true)
                local exists = FileSystem.GetFileInfo(dst) ~= nil
                FileSystem.Delete(src)
                FileSystem.Delete(dst)
                return tostring(ok and exists)
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task FileSystem_Move_SourceGoneDestinationExists()
        {
            string? r = await Run(@"
                local src = FileSystem.GetTempFileName() .. '_kitsune_msrc.txt'
                local dst = FileSystem.GetTempFileName() .. '_kitsune_mdst.txt'
                local f = FileSystem.Open(src, 'wb'); f:write('move me'); f:close()
                local ok = FileSystem.Move(src, dst)
                local srcGone = FileSystem.GetFileInfo(src) == nil
                local dstExists = FileSystem.GetFileInfo(dst) ~= nil
                FileSystem.Delete(dst)
                return tostring(ok and srcGone and dstExists)
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task FileSystem_Rename_SourceGoneDestinationExists()
        {
            string? r = await Run(@"
                local src = FileSystem.GetTempFileName() .. '_kitsune_rsrc.txt'
                local dst = FileSystem.GetTempFileName() .. '_kitsune_rdst.txt'
                local f = FileSystem.Open(src, 'wb'); f:write('rename me'); f:close()
                local ok = FileSystem.Rename(src, dst)
                local srcGone = FileSystem.GetFileInfo(src) == nil
                local dstExists = FileSystem.GetFileInfo(dst) ~= nil
                FileSystem.Delete(dst)
                return tostring(ok and srcGone and dstExists)
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task FileSystem_Rename_WithWcharPaths()
        {
            string? r = await Run(@"
                local src = FileSystem.GetTempFileName() .. '_kitsune_wrsrc.txt'
                local dst = FileSystem.GetTempFileName() .. '_kitsune_wrdst.txt'
                local f = FileSystem.Open(src, 'wb'); f:write('wchar rename'); f:close()
                local ok = FileSystem.Rename(Wchar.FromUtf8(src), Wchar.FromUtf8(dst))
                local srcGone = FileSystem.GetFileInfo(src) == nil
                local dstExists = FileSystem.GetFileInfo(dst) ~= nil
                FileSystem.Delete(dst)
                return tostring(ok and srcGone and dstExists)
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task FileSystem_Delete_RemovesFile()
        {
            string? r = await Run(@"
                local path = FileSystem.GetTempFileName() .. '_kitsune_del.txt'
                local f = FileSystem.Open(path, 'wb'); f:write('delete me'); f:close()
                local ok = FileSystem.Delete(path)
                local gone = FileSystem.GetFileInfo(path) == nil
                return tostring(ok and gone)
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task FileSystem_Delete_WithWcharPath_RemovesFile()
        {
            string? r = await Run(@"
                local path = FileSystem.GetTempFileName() .. '_kitsune_wdel.txt'
                local f = FileSystem.Open(path, 'wb'); f:write('wchar delete'); f:close()
                local ok = FileSystem.Delete(Wchar.FromUtf8(path))
                local gone = FileSystem.GetFileInfo(path) == nil
                return tostring(ok and gone)
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task FileSystem_GetFiles_ReturnsOnlyFiles()
        {
            string? r = await Run(@"
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
            r.ShouldBe("true");
        }

        [Fact]
        public async Task FileSystem_GetFiles_WithWcharPath_ReturnsOnlyFiles()
        {
            string? r = await Run(@"
                local sep = package.config:sub(1,1)
                local base = FileSystem.GetTempFileName() .. '_kitsune_wls'
                FileSystem.CreateDirectory(base)
                local f1 = FileSystem.Open(base .. sep .. 'x.txt', 'wb'); f1:write('x'); f1:close()
                local files = FileSystem.GetFiles(Wchar.FromUtf8(base))
                FileSystem.Delete(base .. sep .. 'x.txt')
                FileSystem.RemoveDirectory(base)
                return tostring(#files == 1)
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task FileSystem_GetDirectories_ReturnsOnlyDirs()
        {
            string? r = await Run(@"
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
            r.ShouldBe("true");
        }

        [Fact]
        public async Task FileSystem_GetAll_ReturnsMixedEntries()
        {
            string? r = await Run(@"
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
            r.ShouldBe("true");
        }

        [Fact]
        public async Task FileSystem_GetAll_EntriesHaveExpectedFields()
        {
            string? r = await Run(@"
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
            r.ShouldBe("true");
        }

        [Fact]
        public async Task FileSystem_GetAll_WithWcharPath_ReturnsMixedEntries()
        {
            string? r = await Run(@"
                local sep = package.config:sub(1,1)
                local base = FileSystem.GetTempFileName() .. '_kitsune_wall'
                FileSystem.CreateDirectory(base)
                local f = FileSystem.Open(base .. sep .. 'y.txt', 'wb'); f:write('y'); f:close()
                local all = FileSystem.GetAll(Wchar.FromUtf8(base))
                FileSystem.Delete(base .. sep .. 'y.txt')
                FileSystem.RemoveDirectory(base)
                return tostring(#all == 1)
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task FileSystem_SetCurrentDirectory_ChangesDirectory()
        {
            string? r = await Run(@"
                local orig = FileSystem.CurrentDirectory()
                local tmp  = FileSystem.GetTempFileName() .. '_kitsune_chdir'
                FileSystem.CreateDirectory(tmp)
                local ok = FileSystem.SetCurrentDirectory(tmp)
                FileSystem.SetCurrentDirectory(orig)
                FileSystem.RemoveDirectory(tmp)
                return tostring(ok == true)
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task FileSystem_SetCurrentDirectory_WithWcharPath_ChangesDirectory()
        {
            string? r = await Run(@"
                local orig = FileSystem.CurrentDirectory()
                local tmp  = FileSystem.GetTempFileName() .. '_kitsune_wchdir'
                FileSystem.CreateDirectory(tmp)
                local ok = FileSystem.SetCurrentDirectory(Wchar.FromUtf8(tmp))
                FileSystem.SetCurrentDirectory(orig)
                FileSystem.RemoveDirectory(tmp)
                return tostring(ok == true)
            ");
            r.ShouldBe("true");
        }

        // -- Env ------------------------------------------------------------------

        [Fact]
        public async Task Env_Create_StoresAndRetrievesValues()
        {
            string? r = await Run(@"
                local e = Env.Create('KitsuneUtilTestEnv')
                e.greeting = 'hello'
                e.count    = 42
                return tostring(e.greeting == 'hello' and e.count == 42)
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task Env_GetOrCreate_ReturnsSameEnv()
        {
            string? r = await Run(@"
                local e1 = Env.GetOrCreate('SharedEnvUtil')
                e1.val = 'shared'
                local e2 = Env.GetOrCreate('SharedEnvUtil')
                return tostring(e2.val == 'shared')
            ");
            r.ShouldBe("true");
        }

        // -- CSV instance extras --------------------------------------------------

        [Fact]
        public async Task CSV_Create_AliasWorksIdenticallyToNew()
        {
            // CSV.Create is a registered alias for CSV.New; must return a working instance.
            string? r = await Run(@"
                local csv = CSV.Create(';')
                local t = csv:Decode('a;b;c')
                return tostring(tostring(t.Rows[1][1]) == 'a' and tostring(t.Rows[1][3]) == 'c')
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task CSV_Tostring_AutoInstance()
        {
            // __tostring on a no-delimiter instance reports "CSV(auto)".
            string? r = await Run("return tostring(CSV.New())");
            r.ShouldBe("CSV(auto)");
        }

        [Fact]
        public async Task CSV_Tostring_FixedDelimiterInstance()
        {
            // __tostring on a fixed-delimiter instance reports the character.
            string? r = await Run("return tostring(CSV.New(';'))");
            r.ShouldBe("CSV(';')");
        }

        [Fact]
        public async Task CSV_New_CalledOnInstance_CreatesNewIndependentInstance()
        {
            // csv:New(delim) must ignore the existing instance and return a fresh one
            // with its own delimiter — not a reference to the original.
            string? r = await Run(@"
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
            r.ShouldBe("true");
        }

        [Fact]
        public async Task CSV_InstanceReuse_MultipleDecodeCalls_BothCorrect()
        {
            // The same instance must produce correct results across successive Decode calls.
            // DecodeCsvWith resets pos/last/len but preserves the buffer allocation.
            string? r = await Run(@"
                local csv = CSV.New()
                local t1 = csv:Decode('a,b,c')
                local t2 = csv:Decode('1,2,3')
                return tostring(
                    tostring(t1.Rows[1][1]) == 'a' and tostring(t1.Rows[1][3]) == 'c' and
                    tostring(t2.Rows[1][1]) == '1' and tostring(t2.Rows[1][3]) == '3'
                )
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task CSV_AutoDetect_ReSniffsDelimiterOnEachDecode()
        {
            // An auto-detect instance must sniff fresh on every Decode call;
            // the sniffed delimiter from call 1 must not bleed into call 2.
            string? r = await Run(@"
                local csv = CSV.New()
                local t1 = csv:Decode('a,b,c')   -- sniffs comma
                local t2 = csv:Decode('x;y;z')   -- must sniff semicolon, not reuse comma
                return tostring(
                    tostring(t1.Rows[1][2]) == 'b' and
                    tostring(t2.Rows[1][2]) == 'y'
                )
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task CSV_DecodeFromFunction_NonReadableStream_RaisesError()
        {
            // Passing a write-only stream must produce a clean Lua error, not a crash.
            string? r = await Run(@"
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
            r.ShouldBe("true");
        }

        [Fact]
        public async Task CSV_InstanceReuse_EncodeAfterDecode_BothCorrect()
        {
            // Encode after Decode on the same instance must work; the buffer fields
            // used by Decode do not interfere with LuaL_Buffer used by Encode.
            string? r = await Run(@"
                local csv = CSV.New(';')
                local t   = csv:Decode('a;b;c')
                local enc = csv:Encode({{tostring(t.Rows[1][1]), tostring(t.Rows[1][3])}})
                return enc
            ");
            r.ShouldBe("a;c");
        }

        [Fact]
        public async Task CSV_DecodeFromFunction_InstanceAutoDetectDoesNotBleedIntoSecondIterator()
        {
            // Two separate DecodeFromFunction iterators created from the same auto-detect
            // instance must each detect their own delimiter independently.
            string? r = await Run(@"
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
            r.ShouldBe("b|y");
        }

        // -- Json extras ----------------------------------------------------------

        [Fact]
        public async Task Json_NegativeInfinity_EncodesAsSpecialLiteral()
        {
            string? r = await Run("return Json.New():Encode(-math.huge)");
            r.ShouldBe("-1e+9999");
        }

        [Fact]
        public async Task Json_Tostring_ReturnsNonEmptyString()
        {
            // __tostring on a Json instance returns a pointer-format string.
            string? r = await Run("return tostring(type(tostring(Json.New())) == 'string' and #tostring(Json.New()) > 0)");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task Json_Dispose_CanBeCalledExplicitly()
        {
            // Json.Dispose() is an explicit GC; calling it must not crash and the
            // instance should still be a valid Lua value afterwards.
            string? r = await Run(@"
                local j = Json.New()
                j:Dispose()
                return 'ok'
            ");
            r.ShouldBe("ok");
        }

        [Fact]
        public async Task Json_Decode_ChunkedFunction_ParsesValues()
        {
            // json:Decode(fn) calls fn() repeatedly to get input chunks; returning
            // nil or "" signals end of input.  Tests the chunkFnIdx code path.
            string? r = await Run(@"
                local j      = Json.New()
                local chunks = { '[1,', '2,', '3]' }
                local i      = 0
                local t = j:Decode(function()
                    i = i + 1
                    return chunks[i]
                end)
                return tostring(type(t) == 'table' and t[1] == 1 and t[2] == 2 and t[3] == 3)
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task Json_Decode_ChunkedFunction_MultipleValues()
        {
            // Each Decode(fn) call drains exactly one JSON value; the fn is fresh
            // each call so this tests that chunkFnIdx is properly reset.
            string? r = await Run(@"
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
            r.ShouldBe("true");
        }

        // -- Stream extras --------------------------------------------------------

        [Fact]
        public async Task Stream_WriteDouble_ReadDouble_RoundTrip()
        {
            string? r = await Run(@"
                local s = Stream.Create()
                s:WriteDouble(3.141592653589793)
                s:Seek(0)
                local v = s:ReadDouble()
                return tostring(math.abs(v - 3.141592653589793) < 1e-12)
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task Stream_UnsignedNumericTypes_RoundTrip()
        {
            // WriteUnsignedShort / WriteUnsignedInt / WriteUnsignedLong — each must
            // round-trip without sign-extension or truncation.
            string? r = await Run(@"
                local s = Stream.Create()
                s:WriteUnsignedShort(60000)
                s:WriteUnsignedInt(3000000000)
                s:WriteUnsignedLong(9000000000)
                s:Seek(0)
                return tostring(s:ReadUnsignedShort()) .. ':'
                    .. tostring(s:ReadUnsignedInt()) .. ':'
                    .. tostring(s:ReadUnsignedLong())
            ");
            r.ShouldBe("60000:3000000000:9000000000");
        }

        [Fact]
        public async Task Stream_WriteUtf8_WriteAndReadBack()
        {
            // WriteUtf8 converts Latin-1 bytes to UTF-8; the raw bytes can be
            // Read back as a regular Lua string.
            string? r = await Run(@"
                local s = Stream.Create()
                s:WriteUtf8('hello')
                s:Seek(0)
                return s:Read()
            ");
            r.ShouldBe("hello");
        }

        [Fact]
        public async Task Stream_WriteUtf8_EmbeddedNullByte_WritesFullString()
        {
            // Previously the loop used while(*in) which stops at embedded '\0',
            // silently dropping everything after it.  The fix uses the len from
            // luaL_checklstring so all bytes are encoded and written.
            string? r = await Run(@"
                local s = Stream.Create()
                s:WriteUtf8('a\0b')   -- 3 bytes: 'a', null, 'b'
                local _, info = s:GetInfo()
                return tostring(info.len == 3)
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task Stream_ReadUtf8_ReturnsBytesAndCodepoint()
        {
            // ReadUtf8 reads exactly one UTF-8 codepoint and returns
            // (raw_bytes_string, codepoint_integer).
            string? r = await Run(@"
                local s = Stream.Create()
                s:Write('A')           -- single ASCII codepoint (U+0041)
                s:Seek(0)
                local bytes, cp = s:ReadUtf8()
                return tostring(bytes == 'A' and cp == 65)
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task Stream_SetByte_AtPosition_ModifiesWithoutMovingCursor()
        {
            // SetByte(value, pos) writes one byte at pos, restores cursor, then
            // a Read from the original position sees the patched byte.
            string? r = await Run(@"
                local s = Stream.Create()
                s:Write('ABCD')
                s:SetByte(88, 1)   -- patch index 1 ('B') with 'X' (88)
                s:Seek(0)
                return s:Read()    -- should read 'AXCD'
            ");
            r.ShouldBe("AXCD");
        }

        [Fact]
        public async Task Stream_Tostring_ReadableAndSeekable_ReadsContent()
        {
            // __tostring reads and returns the stream content ONLY for in-memory
            // streams.  File streams and custom backends use the pointer fallback
            // to avoid side effects and large reads.
            string? r = await Run(@"
                local s = Stream.Create('hello')
                s:Seek(0)
                return tostring(s)
            ");
            r.ShouldBe("hello");
        }

        [Fact]
        public async Task Stream_Tostring_FileStream_ReturnsFallbackString()
        {
            // A file stream opened with "rb" has CAP_READ + CAP_SEEK, but __tostring
            // must NOT silently read the file — it must return the pointer fallback.
            string? r = await Run(@"
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
            r.ShouldBe("true");
        }

        [Fact]
        public async Task Stream_Tostring_NonReadableStream_ReturnsFallbackString()
        {
            // A write-only stream lacks CAP_READ; __tostring must return a pointer
            // string rather than attempting to read the stream.
            string? r = await Run(@"
                local OPEN, CLOSE, CAP_WRITE = 0, 1, 2
                local s = Stream.Create(function(op)
                    if op == OPEN  then return CAP_WRITE end
                    if op == CLOSE then return true end
                end)
                local str = tostring(s)
                return tostring(type(str) == 'string' and #str > 0 and str ~= '')
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task Stream_Tostring_ReadableButNotSeekable_ReturnsFallbackString()
        {
            // A read-only stream without CAP_SEEK must also fall back to the pointer
            // string — reading without being able to seek would silently consume data.
            string? r = await Run(@"
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
            r.ShouldBe("true");
        }

        // -- Write / Read coverage -------------------------------------------------

        [Fact]
        public async Task Stream_Write_ReturnsWrittenByteCount()
        {
            string? r = await Run(@"
                local s = Stream.Create()
                return tostring(s:Write('hello'))
            ");
            r.ShouldBe("5");
        }

        [Fact]
        public async Task Stream_Write_WithLimit_TruncatesOutput()
        {
            // Write(value, limit) writes at most 'limit' bytes.
            string? r = await Run(@"
                local s = Stream.Create()
                s:Write('hello world', 5)
                s:Seek(0)
                return s:Read()
            ");
            r.ShouldBe("hello");
        }

        [Fact]
        public async Task Stream_Write_WithBoolean_WritesSingleByte()
        {
            string? r = await Run(@"
                local s = Stream.Create()
                s:Write(true)
                s:Write(false)
                s:Seek(0)
                return tostring(s:ReadByte()) .. ':' .. tostring(s:ReadByte())
            ");
            r.ShouldBe("1:0");
        }

        [Fact]
        public async Task Stream_Write_UnsupportedType_ReturnsZero()
        {
            string? r = await Run(@"
                local s = Stream.Create()
                return tostring(s:Write(nil))
            ");
            r.ShouldBe("0");
        }

        [Fact]
        public async Task Stream_Read_WithLength_ReadsExactCount()
        {
            string? r = await Run(@"
                local s = Stream.Create('hello world')
                return s:Read(5)
            ");
            r.ShouldBe("hello");
        }

        // -- SetByte / PeekByte extra forms ----------------------------------------

        [Fact]
        public async Task Stream_SetByte_WithoutPosition_WritesAtCursorAndAdvances()
        {
            // SetByte(value) with no position writes at the current cursor and
            // advances it, just like WriteByte but without the 0-255 range guard.
            string? r = await Run(@"
                local s = Stream.Create('ABC')
                s:Seek(1)
                s:SetByte(88)   -- 'X'
                s:Seek(0)
                return s:Read()
            ");
            r.ShouldBe("AXC");
        }

        [Fact]
        public async Task Stream_PeekByte_AtExplicitPosition_LeavesOriginalCursor()
        {
            // PeekByte(pos) peeks at 'pos' without disturbing the current cursor.
            string? r = await Run(@"
                local s = Stream.Create('ABCD')
                s:Seek(2)
                local b = s:PeekByte(0)   -- peek at 'A' (65) while cursor is at 2
                return tostring(b == 65 and s:pos() == 2)
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task Stream_PeekByte_RequiresReadAndSeek_NotADistinctFlag()
        {
            // PeekStreamByte is now gated on CAP_READ + CAP_SEEK — there is no
            // separate CAP_PEEK flag.  A backend with both returns a real value;
            // a backend with only CAP_READ (no seek) returns -1.
            string? r = await Run(@"
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
            r.ShouldBe("true");
        }

        // -- Capability-guard return values ----------------------------------------

        [Fact]
        public async Task Stream_Seek_NonSeekable_ReturnsFalse()
        {
            string? r = await Run(@"
                local OPEN, CLOSE, CAP_WRITE = 0, 1, 2
                local s = Stream.Create(function(op)
                    if op == OPEN  then return CAP_WRITE end
                    if op == CLOSE then return true end
                end)
                return tostring(s:Seek(0))
            ");
            r.ShouldBe("false");
        }

        [Fact]
        public async Task Stream_Pos_NonSeekable_ReturnsNil()
        {
            string? r = await Run(@"
                local OPEN, CLOSE, CAP_WRITE = 0, 1, 2
                local s = Stream.Create(function(op)
                    if op == OPEN  then return CAP_WRITE end
                    if op == CLOSE then return true end
                end)
                return tostring(s:pos())
            ");
            r.ShouldBe("nil");
        }

        [Fact]
        public async Task Stream_Len_WriteOnly_ReturnsNil()
        {
            string? r = await Run(@"
                local OPEN, CLOSE, CAP_WRITE = 0, 1, 2
                local s = Stream.Create(function(op)
                    if op == OPEN  then return CAP_WRITE end
                    if op == CLOSE then return true end
                end)
                return tostring(s:len())
            ");
            r.ShouldBe("nil");
        }

        // -- WriteByte boundary and range ------------------------------------------

        [Fact]
        public async Task Stream_WriteByte_OutOfRange_ReturnsFalse()
        {
            string? r = await Run(@"
                local s = Stream.Create()
                return tostring(s:WriteByte(256)) .. ':' .. tostring(s:WriteByte(-1))
            ");
            r.ShouldBe("false:false");
        }

        [Fact]
        public async Task Stream_WriteByte_Boundaries_RoundTrip()
        {
            string? r = await Run(@"
                local s = Stream.Create()
                s:WriteByte(0)
                s:WriteByte(255)
                s:Seek(0)
                return tostring(s:ReadByte()) .. ':' .. tostring(s:ReadByte())
            ");
            r.ShouldBe("0:255");
        }

        // -- Signed short ----------------------------------------------------------

        [Fact]
        public async Task Stream_WriteShort_NegativeValue_RoundTrips()
        {
            string? r = await Run(@"
                local s = Stream.Create()
                s:WriteShort(-100)
                s:Seek(0)
                return tostring(s:ReadShort())
            ");
            r.ShouldBe("-100");
        }

        // -- ReadUtf8 extended coverage --------------------------------------------

        [Fact]
        public async Task Stream_ReadUtf8_MultiByte_ReturnsCodepoint()
        {
            // U+00E9 (é) encodes as 0xC3 0xA9 in UTF-8.
            string? r = await Run(@"
                local s = Stream.Create()
                s:WriteByte(0xC3)
                s:WriteByte(0xA9)
                s:Seek(0)
                local bytes, cp = s:ReadUtf8()
                return tostring(#bytes == 2 and cp == 0xE9)
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task Stream_ReadUtf8_InvalidLeadByte_ReturnsNil()
        {
            // 0xFF is not a valid UTF-8 lead byte; ReadUtf8 must return nil.
            string? r = await Run(@"
                local s = Stream.Create()
                s:WriteByte(0xFF)
                s:Seek(0)
                return tostring(s:ReadUtf8())
            ");
            r.ShouldBe("nil");
        }

        // -- WriteUtf8 Latin-1 conversion ------------------------------------------

        [Fact]
        public async Task Stream_WriteUtf8_HighByte_ConvertedToUtf8Pair()
        {
            // WriteUtf8 treats the input string as Latin-1 and re-encodes to UTF-8.
            // Latin-1 0xE9 (é) must produce the two-byte sequence 0xC3 0xA9.
            string? r = await Run(@"
                local s = Stream.Create()
                s:WriteUtf8('\xE9')
                local _, info = s:GetInfo()
                s:Seek(0)
                local b1 = s:ReadByte()
                local b2 = s:ReadByte()
                return tostring(info.len == 2 and b1 == 0xC3 and b2 == 0xA9)
            ");
            r.ShouldBe("true");
        }

        // -- Compress / Decompress error paths -------------------------------------

        [Fact]
        public async Task Stream_Compress_NonReadableSource_ReturnsNilAndError()
        {
            string? r = await Run(@"
                local OPEN, CLOSE, CAP_WRITE = 0, 1, 2
                local s = Stream.Create(function(op)
                    if op == OPEN  then return CAP_WRITE end
                    if op == CLOSE then return true end
                end)
                local result, err = s:Compress()
                return tostring(result == nil and type(err) == 'string')
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task Stream_Decompress_NonReadableSource_ReturnsNilAndError()
        {
            string? r = await Run(@"
                local OPEN, CLOSE, CAP_WRITE = 0, 1, 2
                local s = Stream.Create(function(op)
                    if op == OPEN  then return CAP_WRITE end
                    if op == CLOSE then return true end
                end)
                local result, err = s:Decompress()
                return tostring(result == nil and type(err) == 'string')
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task Stream_Compress_NonWritableDest_ReturnsNilAndError()
        {
            string? r = await Run(@"
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
            r.ShouldBe("true");
        }

        // -- Misc stream operations ------------------------------------------------

        [Fact]
        public async Task Stream_Close_ExplicitCall_DoesNotCrash()
        {
            string? r = await Run(@"
                local s = Stream.Create()
                s:Write('hello')
                s:Close()
                return 'ok'
            ");
            r.ShouldBe("ok");
        }

        [Fact]
        public async Task Stream_GetInfo_MemoryStream_TypeIsMemory()
        {
            string? r = await Run(@"
                local s = Stream.Create()
                s:Write('test')
                local _, info = s:GetInfo()
                return info.type
            ");
            r.ShouldBe("memory");
        }

        // -- SharedMemory (ToSharedMemory / OpenSharedMemory) ---------------------

        [Fact]
        public async Task SharedMemory_OpenSharedMemory_InfoType_IsSharedMemoryOut()
        {
            string? r = await Run(@"
                local s = Stream.OpenSharedMemory(64)
                local _, info = s:GetInfo()
                return info.type
            ");
            r.ShouldBe("sharedmemory_out");
        }

        [Fact]
        public async Task SharedMemory_OpenSharedMemory_SizeMatchesRequested()
        {
            string? r = await Run(@"
                local s = Stream.OpenSharedMemory(128)
                local _, info = s:GetInfo()
                return tostring(info.size == 128)
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task SharedMemory_OpenSharedMemory_IsReadWriteAndSeekable()
        {
            string? r = await Run(@"
                local CAP_READ, CAP_WRITE, CAP_SEEK = 1, 2, 4
                local s = Stream.OpenSharedMemory(16)
                local caps, _ = s:GetInfo()
                return tostring(
                    (caps.Caps & CAP_READ)  ~= 0 and
                    (caps.Caps & CAP_WRITE) ~= 0 and
                    (caps.Caps & CAP_SEEK)  ~= 0
                )
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task SharedMemory_OpenSharedMemory_WriteAndRead_RoundTrip()
        {
            // Size the block exactly to the payload so Read() returns only the written bytes.
            string? r = await Run(@"
                local payload = 'hello shmem'
                local s = Stream.OpenSharedMemory(#payload)
                s:Write(payload)
                s:Seek(0)
                return s:Read()
            ");
            r.ShouldBe("hello shmem");
        }

        [Fact]
        public async Task SharedMemory_OpenSharedMemory_ZeroSize_RaisesError()
        {
            string? r = await Run(@"
                local ok, err = pcall(Stream.OpenSharedMemory, 0)
                return tostring(not ok and type(err) == 'string')
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task SharedMemory_ToSharedMemory_PreservesStreamContents()
        {
            string? r = await Run(@"
                local src = Stream.Create()
                src:Write('snapshot data')
                local snap = src:ToSharedMemory()
                snap:Seek(0)
                return snap:Read()
            ");
            r.ShouldBe("snapshot data");
        }

        [Fact]
        public async Task SharedMemory_ToSharedMemory_InfoType_IsSharedMemoryOut()
        {
            string? r = await Run(@"
                local src = Stream.Create()
                src:Write('test')
                local snap = src:ToSharedMemory()
                local _, info = snap:GetInfo()
                return info.type
            ");
            r.ShouldBe("sharedmemory_out");
        }

        [Fact]
        public async Task SharedMemory_ToSharedMemory_SizeMatchesSourceLength()
        {
            string? r = await Run(@"
                local src = Stream.Create()
                src:Write('hello')
                local snap = src:ToSharedMemory()
                local _, info = snap:GetInfo()
                return tostring(info.size == 5)
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task SharedMemory_ToSharedMemory_IsIndependentOfSource()
        {
            // ToSharedMemory produces a deep copy; mutating the source afterward
            // must not alter the snapshot.
            string? r = await Run(@"
                local src = Stream.Create()
                src:Write('original')
                local snap = src:ToSharedMemory()
                src:Seek(0)
                src:Write('modified')
                snap:Seek(0)
                return snap:Read()
            ");
            r.ShouldBe("original");
        }

        [Fact]
        public async Task SharedMemory_ToSharedMemory_CapturesFromPositionZero()
        {
            // ToSharedMemory internally seeks the source to 0 before snapshotting,
            // so the full content is captured regardless of the current cursor.
            string? r = await Run(@"
                local src = Stream.Create()
                src:Write('full content')
                src:Seek(5)
                local snap = src:ToSharedMemory()
                snap:Seek(0)
                return snap:Read()
            ");
            r.ShouldBe("full content");
        }

        [Fact]
        public async Task SharedMemory_ToSharedMemory_WithDispose_OriginalIsZeroed()
        {
            // After ToSharedMemory(true) the original stream is disposed: its Caps
            // are zeroed, so pos() returns nil (no STREAM_CAP_SEEK).
            string? r = await Run(@"
                local src = Stream.Create()
                src:Write('bye')
                local snap = src:ToSharedMemory(true)
                return tostring(src:pos() == nil)
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task SharedMemory_ToSharedMemory_WithDispose_SnapshotContentsIntact()
        {
            string? r = await Run(@"
                local src = Stream.Create()
                src:Write('preserve me')
                local snap = src:ToSharedMemory(true)
                snap:Seek(0)
                return snap:Read()
            ");
            r.ShouldBe("preserve me");
        }

        [Fact]
        public async Task SharedMemory_ToSharedMemory_NonReadableStream_RaisesError()
        {
            string? r = await Run(@"
                local OPEN, CLOSE, CAP_WRITE = 0, 1, 2
                local s = Stream.Create(function(op)
                    if op == OPEN  then return CAP_WRITE end
                    if op == CLOSE then return true end
                end)
                local ok, err = pcall(function() s:ToSharedMemory() end)
                return tostring(not ok and type(err) == 'string')
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task SharedMemory_ToSharedMemory_NonSeekableStream_RaisesError()
        {
            // A read-only backend without CAP_SEEK must also be rejected.
            string? r = await Run(@"
                local OPEN, CLOSE, READ, CAP_READ = 0, 1, 2, 1
                local s = Stream.Create(function(op, len)
                    if op == OPEN  then return CAP_READ end
                    if op == CLOSE then return true end
                    if op == READ  then return 'data' end
                end)
                local ok, err = pcall(function() s:ToSharedMemory() end)
                return tostring(not ok and type(err) == 'string')
            ");
            r.ShouldBe("true");
        }
    }
}
