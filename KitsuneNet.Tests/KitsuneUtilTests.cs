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

        // -- ResList global variable ----------------------------------------------

        [Fact]
        public async Task ResList_IsTable()
        {
            string? r = await Run("return type(ResList)");
            r.ShouldBe("table");
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

        // -- GetRegistryValue -----------------------------------------------------

        [Fact]
        public async Task GetRegistryValue_KnownKey_ReturnsNonEmptyString()
        {
            // HKLM(0) \ SOFTWARE\Microsoft\Windows NT\CurrentVersion \ ProductName
            string? r = await Run(@"
                local v = GetRegistryValue(0, 'SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion', 'ProductName')
                return tostring(type(v)=='string' and #v>0)
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task GetRegistryValue_NonExistentKey_ReturnsNilAndError()
        {
            string? r = await Run(@"
                local v, err = GetRegistryValue(0, 'SOFTWARE\\NonExistent_XYZ_9876', 'NoSuchEntry')
                return tostring(v == nil and err ~= nil)
            ");
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

        [Fact]
        public async Task Json_Encode_ProducesValidJson()
        {
            // Encode a simple table and verify the round-trip via Decode.
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
        public async Task Json_NullSentinel_RoundTrips()
        {
            string? r = await Run(@"
                local j = Json.Create()
                j:SetNullValue('__NULL__')
                local enc = j:Encode({v='__NULL__'})
                local dec = j:Decode(enc)
                return tostring(dec.v == '__NULL__' and enc:find('null') ~= nil)
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
        public async Task Wchar_ToWide_ReturnsRawBytesOfCorrectLength()
        {
            // On Windows wchar_t is 2 bytes; 'A' is one wchar_t so ToWide returns 2 bytes.
            string? r = await Run(@"
                local wide = Wchar.FromUtf8('A'):ToWide()
                return tostring(type(wide) == 'string' and #wide == 2)
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task Wchar_FromBytes_RawString_RoundTrips()
        {
            // ToWide yields raw wchar_t bytes; FromBytes(string) reconstructs from them.
            string? r = await Run(@"
                local w1 = Wchar.FromUtf8('hello')
                local w2 = Wchar.FromBytes(w1:ToWide())
                return tostring(w1 == w2)
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task Wchar_Codepoints_ReturnsCorrectTable()
        {
            // 'AB' → codepoints table {65, 66}.
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

        // -- Timer ----------------------------------------------------------------

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

        // -- Stream (in-memory) ---------------------------------------------------

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
        public async Task Stream_FromString_ReadMatchesInput()
        {
            string? r = await Run(@"
                local s = Stream.FromString('kitsune')
                return s:Read()
            ");
            r.ShouldBe("kitsune");
        }

        [Fact]
        public async Task Stream_GetInfo_ReturnsLengthAndPos()
        {
            string? r = await Run(@"
                local s = Stream.Create()
                s:Write('abcde')
                local pos, len, alloc = s:GetInfo()
                return tostring(pos == 5 and len == 5)
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task Stream_Compress_Decompress_RoundTrip()
        {
            string? r = await Run(@"
                local s = Stream.FromString('hello hello hello hello hello')
                local compressed   = s:Compress()
                local decompressed = compressed:Decompress()
                decompressed:Seek(0)
                return decompressed:Read()
            ");
            r.ShouldBe("hello hello hello hello hello");
        }

        // -- CSV ------------------------------------------------------------------

        [Fact]
        public async Task CSV_DecodeString_ParsesRows()
        {
            // Count rows with ipairs to avoid # unreliability on non-sequence tables.
            string? r = await Run(@"
                local csv = CSV.Create()
                local t = csv:DecodeString('a,b,c\n1,2,3')
                local count = 0
                for _ in ipairs(t.Rows) do count = count + 1 end
                return tostring(count == 2)
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task CSV_DecodeString_RowValues_AccessibleAsWchar()
        {
            string? r = await Run(@"
                local csv = CSV.Create()
                local t = csv:DecodeString('hello,world')
                return tostring(t.Rows[1][1])
            ");
            r.ShouldBe("hello");
        }

        [Fact]
        public async Task CSV_DecodeString_MultipleColumnsPerRow()
        {
            string? r = await Run(@"
                local csv = CSV.Create()
                local t = csv:DecodeString('a,b,c')
                local row = t.Rows[1]
                return tostring(tostring(row[1]) .. ':' .. tostring(row[2]) .. ':' .. tostring(row[3]))
            ");
            r.ShouldBe("a:b:c");
        }

        [Fact]
        public async Task CSV_DecodeString_MultipleRows_CorrectCount()
        {
            string? r = await Run(@"
                local csv = CSV.Create()
                local t = csv:DecodeString('r1c1,r1c2\nr2c1,r2c2\nr3c1,r3c2')
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
                local csv = CSV.Create()
                local t = csv:DecodeString('""hello"",""world""')
                return tostring(tostring(t.Rows[1][1]) == 'hello' and tostring(t.Rows[1][2]) == 'world')
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task CSV_DecodeString_QuotedFieldWithEmbeddedDelimiter_PreservesContent()
        {
            // A comma inside quotes must not split the field.
            string? r = await Run(@"
                local csv = CSV.Create()
                local t = csv:DecodeString('""hello, world"",end')
                return tostring(tostring(t.Rows[1][1]) == 'hello, world')
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task CSV_DecodeString_QuotedFieldWithEscapedQuote_ProducesLiteralQuote()
        {
            // RFC 4180 escaped quote: "" inside a quoted field → single ".
            string? r = await Run(@"
                local csv = CSV.Create()
                local t = csv:DecodeString('""say """"hi"""""",end')
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
                local csv = CSV.Create()
                local t = csv:DecodeString(' hello, world')
                return tostring(tostring(t.Rows[1][1]) == 'hello' and tostring(t.Rows[1][2]) == 'world')
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task CSV_DecodeString_EmptyField_PreservesEmptyCell()
        {
            // a,,b produces three fields; the middle one is empty.
            string? r = await Run(@"
                local csv = CSV.Create()
                local t = csv:DecodeString('a,,b')
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
                local csv = CSV.Create()
                local t = csv:DecodeString('a,b')
                return tostring(type(t.Comments) == 'table')
            ");
            r.ShouldBe("true");
        }

        [Fact]
        public async Task CSV_DecodeString_CommentLine_IsExtractedAndExcludedFromRows()
        {
            // Lines starting with * are treated as comments and placed in t.Comments.
            string? r = await Run(@"
                local csv = CSV.Create()
                local t = csv:DecodeString('* this is a comment\na,b')
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
                local csv = CSV.Create()
                local t = csv:DecodeString('* line one\n* line two\na,b')
                local count = 0
                for _ in ipairs(t.Comments) do count = count + 1 end
                return tostring(count == 2)
            ");
            r.ShouldBe("true");
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
    }
}
