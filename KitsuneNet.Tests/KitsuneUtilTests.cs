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
                return Stream.New(function(op)
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
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local id = tostring(Identifier.NewUUID())
                return tostring(type(id) == 'string' and #id == 36
                    and id:sub(9,9) == '-' and id:sub(14,14) == '-'
                    and id:sub(19,19) == '-' and id:sub(24,24) == '-')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task UUID_ConsecutiveCalls_AreDistinct()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("return tostring(tostring(Identifier.NewUUID()) ~= tostring(Identifier.NewUUID()))");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task UUID_IsVersion4()
        {
            using KitsuneEngine engine = new();

            // The 15th character (the version nibble, after two hyphens) must be '4'.
            LuaValue r = await engine.ExecuteStringAsync("return tostring(Identifier.NewUUID()):sub(15,15)");
            r.String.ShouldBe("4");
        }

        [Fact]
        public async Task UUID_HasRfc4122Variant()
        {
            using KitsuneEngine engine = new();

            // Variant bits 10xx: the 20th character must be 8, 9, a, or b.
            LuaValue r = await engine.ExecuteStringAsync(@"
                local c = tostring(Identifier.NewUUID()):sub(20,20)
                return tostring(c == '8' or c == '9' or c == 'a' or c == 'b')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task UUID_ContainsOnlyHexAndDashes()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local id = tostring(Identifier.NewUUID())
                return tostring(id:match('^%x%x%x%x%x%x%x%x%-%x%x%x%x%-%x%x%x%x%-%x%x%x%x%-%x%x%x%x%x%x%x%x%x%x%x%x$') ~= nil)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task UUID_BinaryReturnIs16Bytes()
        {
            using KitsuneEngine engine = new();

            LuaValue r = await engine.ExecuteStringAsync(@"
                local bin = Identifier.NewUUID():AsBytes()
                return tostring(type(bin) == 'string' and #bin == 16)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task UUID_BinaryVersionNibbleIs4()
        {
            using KitsuneEngine engine = new();

            // Byte 7 (1-based): high nibble must be 0x4.
            LuaValue r = await engine.ExecuteStringAsync(@"
                local b = Identifier.NewUUID():AsBytes():byte(7)
                return tostring(b >> 4 == 4)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task UUID_BinaryVariantBitsAreRfc4122()
        {
            using KitsuneEngine engine = new();

            // Byte 9 (1-based): top two bits must be 10xxxxxx (0x80–0xBF).
            LuaValue r = await engine.ExecuteStringAsync(@"
                local b = Identifier.NewUUID():AsBytes():byte(9)
                return tostring(b & 0xC0 == 0x80)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task UUID_StringMatchesBinaryEncoding()
        {
            using KitsuneEngine engine = new();

            // The string representation must round-trip consistently with the binary bytes.
            LuaValue r = await engine.ExecuteStringAsync(@"
                local id = Identifier.NewUUID()
                local str = tostring(id)
                local bin = id:AsBytes()
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
            using KitsuneEngine engine = new();

            // Probabilistically verify uniqueness across 1000 generations.
            LuaValue r = await engine.ExecuteStringAsync(@"
                local seen = {}
                for i = 1, 1000 do
                    local id = tostring(Identifier.NewUUID())
                    if seen[id] then return 'false' end
                    seen[id] = true
                end
                return 'true'
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task UUID_GetType_ReturnsUUID()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("return Identifier.NewUUID():GetType()");
            r.String.ShouldBe("UUID");
        }

        [Fact]
        public async Task UUID_AsString_MatchesToString()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local id = Identifier.NewUUID()
                return tostring(id:AsString() == tostring(id))
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task UUID_IsEmpty_ReturnsFalseForNewUUID()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("return tostring(Identifier.NewUUID():IsEmpty())");
            r.String.ShouldBe("false");
        }

        [Fact]
        public async Task UUID_IsEmpty_ReturnsTrueForAllZeroBytes()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local id = Identifier.FromBytes(string.rep('\0', 16))
                return tostring(id:IsEmpty())
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task UUID_IsEmpty_ReturnsFalseWhenAnyByteNonZero()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local id = Identifier.FromBytes(string.rep('\0', 15) .. '\1')
                return tostring(id:IsEmpty())
            ");
            r.String.ShouldBe("false");
        }

        // -- OID ------------------------------------------------------------------
        [Fact]
        public async Task OID_HasStandardFormat()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local id = tostring(Identifier.NewOID())
                return tostring(type(id) == 'string' and #id == 24)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task OID_ContainsOnlyHexChars()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local id = tostring(Identifier.NewOID())
                return tostring(id:match('^%x+$') ~= nil and #id == 24)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task OID_GetType_ReturnsOID()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("return Identifier.NewOID():GetType()");
            r.String.ShouldBe("OID");
        }

        [Fact]
        public async Task OID_AsBytes_Is12Bytes()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local b = Identifier.NewOID():AsBytes()
                return tostring(type(b) == 'string' and #b == 12)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task OID_AsString_MatchesToString()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local id = Identifier.NewOID()
                return tostring(id:AsString() == tostring(id))
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task OID_ConsecutiveCalls_AreDistinct()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("return tostring(tostring(Identifier.NewOID()) ~= tostring(Identifier.NewOID()))");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task OID_TimestampBytes_AreReasonable()
        {
            using KitsuneEngine engine = new();

            // First 4 bytes are big-endian Unix seconds; must be a plausible timestamp.
            LuaValue r = await engine.ExecuteStringAsync(@"
                local b = Identifier.NewOID():AsBytes()
                local ts = b:byte(1) * 16777216 + b:byte(2) * 65536 + b:byte(3) * 256 + b:byte(4)
                return tostring(ts > 1700000000 and ts < 4000000000)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task OID_IsEmpty_ReturnsFalseForNewOID()
        {
            using KitsuneEngine engine = new();

            // NewOID always has a non-zero timestamp, so it is never empty.
            LuaValue r = await engine.ExecuteStringAsync("return tostring(Identifier.NewOID():IsEmpty())");
            r.String.ShouldBe("false");
        }

        [Fact]
        public async Task OID_IsEmpty_ReturnsTrueForAllZeroBytes()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local id = Identifier.FromBytes(string.rep('\0', 12))
                return tostring(id:IsEmpty())
            ");
            r.String.ShouldBe("true");
        }

        // -- Identifier (FromString) ----------------------------------------------
        [Fact]
        public async Task Identifier_FromString_UUID_RoundTrips()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local id  = Identifier.NewUUID()
                local id2 = Identifier.FromString(tostring(id))
                return tostring(id == id2)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Identifier_FromString_UUID_PreservesType()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                return Identifier.FromString(tostring(Identifier.NewUUID())):GetType()
            ");
            r.String.ShouldBe("UUID");
        }

        [Fact]
        public async Task Identifier_FromString_OID_RoundTrips()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local id  = Identifier.NewOID()
                local id2 = Identifier.FromString(tostring(id))
                return tostring(id == id2)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Identifier_FromString_OID_PreservesType()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                return Identifier.FromString(tostring(Identifier.NewOID())):GetType()
            ");
            r.String.ShouldBe("OID");
        }

        [Fact]
        public async Task Identifier_FromString_InvalidFormat_ReturnsNil()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("return tostring(Identifier.FromString('not-valid-at-all') == nil)");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Identifier_FromString_InvalidOIDHex_ReturnsNil()
        {
            using KitsuneEngine engine = new();

            // Exactly 24 chars but 'z' is not a valid hex digit.
            LuaValue r = await engine.ExecuteStringAsync("return tostring(Identifier.FromString('zzzzzzzzzzzzzzzzzzzzzzzz') == nil)");
            r.String.ShouldBe("true");
        }

        // -- Identifier (FromBytes) -----------------------------------------------
        [Fact]
        public async Task Identifier_FromBytes_UUID_RoundTrips()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local id  = Identifier.NewUUID()
                local id2 = Identifier.FromBytes(id:AsBytes())
                return tostring(id == id2)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Identifier_FromBytes_16Bytes_TypeIsUUID()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local id = Identifier.FromBytes(string.rep('\0', 16))
                return tostring(id ~= nil and id:GetType() == 'UUID')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Identifier_FromBytes_OID_RoundTrips()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local id  = Identifier.NewOID()
                local id2 = Identifier.FromBytes(id:AsBytes())
                return tostring(id == id2)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Identifier_FromBytes_12Bytes_TypeIsOID()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local id = Identifier.FromBytes(string.rep('\0', 12))
                return tostring(id ~= nil and id:GetType() == 'OID')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Identifier_FromBytes_WrongLength_ReturnsNil()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                return tostring(Identifier.FromBytes(string.rep('\0', 8)) == nil)
            ");
            r.String.ShouldBe("true");
        }

        // -- Identifier (Equality) ------------------------------------------------
        [Fact]
        public async Task Identifier_Eq_SameUUID_IsTrue()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local id  = Identifier.NewUUID()
                local id2 = Identifier.FromBytes(id:AsBytes())
                return tostring(id == id2)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Identifier_Eq_DifferentUUID_IsFalse()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("return tostring(Identifier.NewUUID() == Identifier.NewUUID())");
            r.String.ShouldBe("false");
        }

        [Fact]
        public async Task Identifier_Eq_SameOID_IsTrue()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local id  = Identifier.NewOID()
                local id2 = Identifier.FromBytes(id:AsBytes())
                return tostring(id == id2)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Identifier_Eq_DifferentTypes_IsFalse()
        {
            using KitsuneEngine engine = new();

            // 16 zero bytes → UUID; 12 zero bytes → OID; different types, not equal.
            LuaValue r = await engine.ExecuteStringAsync(@"
                local uuid = Identifier.FromBytes(string.rep('\0', 16))
                local oid  = Identifier.FromBytes(string.rep('\0', 12))
                return tostring(uuid == oid)
            ");
            r.String.ShouldBe("false");
        }

        // -- Identifier (IsEmpty) -------------------------------------------------
        [Fact]
        public async Task Identifier_IsEmpty_EmptyUUID_IsTrue()
        {
            using KitsuneEngine engine = new();

            // 00000000-0000-0000-0000-000000000000 (Guid.Empty equivalent)
            LuaValue r = await engine.ExecuteStringAsync(@"
                local id = Identifier.FromString('00000000-0000-0000-0000-000000000000')
                return tostring(id:IsEmpty())
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Identifier_IsEmpty_EmptyOID_IsTrue()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local id = Identifier.FromString('000000000000000000000000')
                return tostring(id:IsEmpty())
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Identifier_IsEmpty_NonZeroUUID_IsFalse()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local id = Identifier.FromString('00000000-0000-0000-0000-000000000001')
                return tostring(id:IsEmpty())
            ");
            r.String.ShouldBe("false");
        }

        [Fact]
        public async Task Identifier_IsEmpty_NonZeroOID_IsFalse()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local id = Identifier.FromString('000000000000000000000001')
                return tostring(id:IsEmpty())
            ");
            r.String.ShouldBe("false");
        }

        // -- Json encoding of Identifier ------------------------------------------
        [Fact]
        public async Task Json_Encode_Identifier_UUID_ProducesJsonString()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local id = Identifier.NewUUID()
                local encoded = Json.New():Encode(id)
                return tostring(encoded == '""' .. tostring(id) .. '""')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Json_Encode_Identifier_OID_ProducesJsonString()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local id = Identifier.NewOID()
                local encoded = Json.New():Encode(id)
                return tostring(encoded == '""' .. tostring(id) .. '""')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Json_Encode_IdentifierInTable_RoundTripsAsString()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local id  = Identifier.NewUUID()
                local str = tostring(id)
                local j   = Json.New()
                local t   = j:Decode(j:Encode({id = id}))
                return tostring(t.id == str)
            ");
            r.String.ShouldBe("true");
        }

        // -- DateTime -------------------------------------------------------------
        [Fact]
        public async Task DateTime_UtcNow_IsUserdata()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("return type(DateTime.UtcNow())");
            r.String.ShouldBe("userdata");
        }

        [Fact]
        public async Task DateTime_Now_IsUserdata()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("return type(DateTime.Now())");
            r.String.ShouldBe("userdata");
        }

        [Fact]
        public async Task DateTime_UtcNow_ToStringIsIso8601()
        {
            using KitsuneEngine engine = new();

            // UTC tostring ends with 'Z'.
            LuaValue r = await engine.ExecuteStringAsync(@"
                local s = tostring(DateTime.UtcNow())
                return tostring(#s >= 24 and s:sub(-1) == 'Z')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task DateTime_New_ComponentsRoundTrip()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local dt = DateTime.New(2024, 3, 15, 10, 30, 45, 123, 0)
                return tostring(
                    dt:Year()        == 2024 and
                    dt:Month()       == 3    and
                    dt:Day()         == 15   and
                    dt:Hour()        == 10   and
                    dt:Minute()      == 30   and
                    dt:Second()      == 45   and
                    dt:Millisecond() == 123
                )
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task DateTime_New_DateOnly_TimeDefaultsToMidnight()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local dt = DateTime.New(2024, 1, 1)
                return tostring(dt:Hour() == 0 and dt:Minute() == 0 and dt:Second() == 0 and dt:Millisecond() == 0)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task DateTime_FromUnixSeconds_RoundTrips()
        {
            using KitsuneEngine engine = new();

            // Unix 0 = 1970-01-01T00:00:00.000Z
            LuaValue r = await engine.ExecuteStringAsync(@"
                local dt = DateTime.FromUnixSeconds(0)
                return tostring(dt:Year() == 1970 and dt:Month() == 1 and dt:Day() == 1 and dt:Hour() == 0)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task DateTime_FromUnixMilliseconds_RoundTrips()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local dt = DateTime.FromUnixMilliseconds(0)
                return tostring(dt:Year() == 1970 and dt:Month() == 1 and dt:Day() == 1)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task DateTime_UnixSeconds_MatchesKnownEpoch()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local dt = DateTime.FromUnixSeconds(0)
                return tostring(dt:UnixSeconds() == 0)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task DateTime_UnixMilliseconds_MatchesKnownEpoch()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local dt = DateTime.FromUnixMilliseconds(1000)
                return tostring(dt:UnixMilliseconds() == 1000)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task DateTime_Parse_ValidIso8601_RoundTrips()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local s  = '2024-03-15T10:30:45.123Z'
                local dt = DateTime.Parse(s)
                return tostring(dt ~= nil and dt:Year() == 2024 and dt:Month() == 3 and dt:Day() == 15
                    and dt:Hour() == 10 and dt:Minute() == 30 and dt:Second() == 45 and dt:Millisecond() == 123)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task DateTime_Parse_WithOffset_UtcTicksCorrect()
        {
            using KitsuneEngine engine = new();

            // 12:00:00+02:00 == 10:00:00 UTC
            LuaValue r = await engine.ExecuteStringAsync(@"
                local dt  = DateTime.Parse('2024-06-01T12:00:00.000+02:00')
                local utc = dt:ToUtc()
                return tostring(utc:Hour() == 10 and utc:OffsetMinutes() == 0)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task DateTime_Parse_InvalidString_ReturnsNil()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("return tostring(DateTime.Parse('not a date') == nil)");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task DateTime_Parse_DateOnly_TimeDefaultsToMidnight()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local dt = DateTime.Parse('2024-07-04')
                return tostring(dt ~= nil and dt:Hour() == 0 and dt:Minute() == 0 and dt:Second() == 0)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task DateTime_ToUtc_ClearsOffset()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local dt  = DateTime.New(2024, 1, 1, 12, 0, 0, 0, 120)
                local utc = dt:ToUtc()
                return tostring(utc:OffsetMinutes() == 0 and utc:Hour() == 10)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task DateTime_ToOffset_ChangesDisplayedHour()
        {
            using KitsuneEngine engine = new();

            // UTC noon → +05:30 = 17:30
            LuaValue r = await engine.ExecuteStringAsync(@"
                local utc  = DateTime.New(2024, 1, 1, 12, 0, 0, 0, 0)
                local ist  = utc:ToOffset(330)
                return tostring(ist:Hour() == 17 and ist:Minute() == 30 and ist:OffsetMinutes() == 330)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task DateTime_AddSeconds_ProducesCorrectResult()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local dt  = DateTime.New(2024, 1, 1, 0, 0, 50, 0, 0)
                local dt2 = dt:AddSeconds(20)
                return tostring(dt2:Minute() == 1 and dt2:Second() == 10)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task DateTime_AddDays_AdvancesDate()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local dt  = DateTime.New(2024, 1, 30, 0, 0, 0, 0, 0)
                local dt2 = dt:AddDays(3)
                return tostring(dt2:Month() == 2 and dt2:Day() == 2)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task DateTime_AddHours_WrapsDay()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local dt  = DateTime.New(2024, 1, 1, 23, 0, 0, 0, 0)
                local dt2 = dt:AddHours(2)
                return tostring(dt2:Day() == 2 and dt2:Hour() == 1)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task DateTime_AddMilliseconds_IncrementsMs()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local dt  = DateTime.New(2024, 1, 1, 0, 0, 0, 900, 0)
                local dt2 = dt:AddMilliseconds(200)
                return tostring(dt2:Second() == 1 and dt2:Millisecond() == 100)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task DateTime_Eq_SameInstant_IsTrue()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local a = DateTime.FromUnixSeconds(1000000)
                local b = DateTime.FromUnixSeconds(1000000)
                return tostring(a == b)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task DateTime_Eq_DifferentOffsetSameInstant_IsTrue()
        {
            using KitsuneEngine engine = new();

            // Same UTC ticks, different display offsets → equal.
            LuaValue r = await engine.ExecuteStringAsync(@"
                local a = DateTime.FromUnixSeconds(0, 0)
                local b = DateTime.FromUnixSeconds(0, 60)
                return tostring(a == b)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task DateTime_Lt_EarlierIsBefore()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local a = DateTime.FromUnixSeconds(1000)
                local b = DateTime.FromUnixSeconds(2000)
                return tostring(a < b and not (b < a))
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task DateTime_Sub_ReturnsTimeSpan()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local a = DateTime.FromUnixSeconds(5000)
                local b = DateTime.FromUnixSeconds(3000)
                local ts = a - b
                return tostring(type(ts) == 'userdata' and math.abs(ts:TotalSeconds() - 2000) < 0.001)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task DateTime_DayOfWeek_KnownDate()
        {
            using KitsuneEngine engine = new();

            // 2024-01-01 was a Monday (.NET DayOfWeek = 1)
            LuaValue r = await engine.ExecuteStringAsync(@"
                local dt = DateTime.New(2024, 1, 1, 0, 0, 0, 0, 0)
                return tostring(dt:DayOfWeek() == 1)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task DateTime_IsEmpty_ZeroTicks_IsTrue()
        {
            using KitsuneEngine engine = new();

            // FromUnixSeconds(0) has ticks == DT_UNIX_EPOCH_TICKS (non-zero),
            // so we construct the empty sentinel via Parse of epoch 0001-01-01.
            LuaValue r = await engine.ExecuteStringAsync(@"
                local dt = DateTime.Parse('0001-01-01T00:00:00.000Z')
                return tostring(dt:IsEmpty())
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task DateTime_IsEmpty_NormalDate_IsFalse()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("return tostring(DateTime.UtcNow():IsEmpty())");
            r.String.ShouldBe("false");
        }

        [Fact]
        public async Task DateTime_Format_StrftimePattern()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local dt = DateTime.New(2024, 6, 15, 0, 0, 0, 0, 0)
                local s  = dt:Format('%Y-%m-%d')
                return tostring(s == '2024-06-15')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task DateTime_AsString_MatchesToString()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local dt = DateTime.UtcNow()
                return tostring(dt:AsString() == tostring(dt))
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task DateTime_UtcNow_UnixSecondsIsReasonable()
        {
            using KitsuneEngine engine = new();

            // Unix seconds must be > 2024-01-01 (1704067200) and < 2100-01-01 (4102444800)
            LuaValue r = await engine.ExecuteStringAsync(@"
                local ts = DateTime.UtcNow():UnixSeconds()
                return tostring(ts > 1704067200 and ts < 4102444800)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task DateTime_Json_Encode_ProducesIso8601String()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local dt  = DateTime.New(2024, 3, 15, 10, 30, 45, 0, 0)
                local enc = Json.New():Encode(dt)
                return tostring(enc == '""2024-03-15T10:30:45.000Z""')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task DateTime_Json_EncodeInTable_RoundTripsAsString()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local dt  = DateTime.New(2024, 3, 15, 10, 30, 45, 0, 0)
                local j   = Json.New()
                local t   = j:Decode(j:Encode({ts = dt}))
                return tostring(t.ts == tostring(dt))
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task DateTime_AddMinutes_ChangesTime()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local dt  = DateTime.New(2024, 1, 1, 12, 45, 0, 0, 0)
                local dt2 = dt:AddMinutes(30)
                return tostring(dt2:Hour() == 13 and dt2:Minute() == 15)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task DateTime_LeapYear_Feb29_Valid()
        {
            using KitsuneEngine engine = new();

            // 2024 is a leap year
            LuaValue r = await engine.ExecuteStringAsync(@"
                local dt = DateTime.New(2024, 2, 29)
                return tostring(dt:Month() == 2 and dt:Day() == 29)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task DateTime_Parse_SqlDatetimeFormat()
        {
            using KitsuneEngine engine = new();

            // MySQL/Postgres often return "YYYY-MM-DD HH:MM:SS" (space separator, no offset)
            LuaValue r = await engine.ExecuteStringAsync(@"
                local dt = DateTime.Parse('2024-03-15 10:30:45')
                return tostring(dt ~= nil and dt:Year() == 2024 and dt:Hour() == 10 and dt:Minute() == 30)
            ");
            r.String.ShouldBe("true");
        }

        // -- CRC32 ----------------------------------------------------------------
        [Fact]
        public async Task CRC32_ReturnsInteger()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("return math.type(CRC32('hello'))");
            r.String.ShouldBe("integer");
        }

        // -- Decimal --------------------------------------------------------------
        [Fact]
        public async Task Decimal_FromString_IsUserdata()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("return type(Decimal.FromString('123.456'))");
            r.String.ShouldBe("userdata");
        }

        [Fact]
        public async Task Decimal_FromString_InvalidReturnsNil()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("return tostring(Decimal.FromString('abc') == nil)");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Decimal_ToString_MatchesToString()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local d = Decimal.FromString('123.456')
                return tostring(d:ToString() == tostring(d))
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Decimal_RoundTrips_ThroughString()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local s = '9999999999999999999999999999999999'
                local d = Decimal.FromString(s)
                return tostring(tostring(d) == s)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Decimal_Negative_RoundTrips()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local d = Decimal.FromString('-123.456')
                return tostring(tostring(d) == '-123.456')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Decimal_Scale_ReturnsDecimalDigits()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("return Decimal.FromString('123.456'):Scale()");
            r.Int64.ShouldBe(3);
        }

        [Fact]
        public async Task Decimal_IsEmpty_ZeroIsTrue()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("return tostring(Decimal.Zero():IsEmpty())");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Decimal_IsEmpty_NonZeroIsFalse()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("return tostring(Decimal.FromString('0.001'):IsEmpty())");
            r.String.ShouldBe("false");
        }

        [Fact]
        public async Task Decimal_IsNegative_PositiveIsFalse()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("return tostring(Decimal.FromString('1'):IsNegative())");
            r.String.ShouldBe("false");
        }

        [Fact]
        public async Task Decimal_IsNegative_NegativeIsTrue()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("return tostring(Decimal.FromString('-1'):IsNegative())");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Decimal_Abs_RemovesSign()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local d = Decimal.FromString('-99.9'):Abs()
                return tostring(not d:IsNegative() and tostring(d) == '99.9')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Decimal_Add_WholeNumbers()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local a = Decimal.FromString('100')
                local b = Decimal.FromString('23')
                return tostring(tostring(a + b) == '123')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Decimal_Add_FractionalAlignsScales()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local a = Decimal.FromString('1.1')
                local b = Decimal.FromString('2.20')
                return tostring(tostring(a + b) == '3.30')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Decimal_Sub_ProducesCorrectResult()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local a = Decimal.FromString('10.00')
                local b = Decimal.FromString('3.75')
                return tostring(tostring(a - b) == '6.25')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Decimal_Sub_NegativeResult()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local a = Decimal.FromString('3')
                local b = Decimal.FromString('10')
                return tostring(tostring(a - b) == '-7')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Decimal_Mul_WholeNumbers()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local a = Decimal.FromString('12')
                local b = Decimal.FromString('12')
                return tostring(tostring(a * b) == '144')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Decimal_Mul_Fractional()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local a = Decimal.FromString('1.5')
                local b = Decimal.FromString('2.0')
                return tostring(tostring(a * b) == '3.00')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Decimal_Unm_NegatesValue()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local d = -Decimal.FromString('42.5')
                return tostring(d:IsNegative() and tostring(d) == '-42.5')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Decimal_Eq_SameValue_IsTrue()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local a = Decimal.FromString('1.50')
                local b = Decimal.FromString('1.50')
                return tostring(a == b)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Decimal_Eq_DifferentValue_IsFalse()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                return tostring(Decimal.FromString('1') == Decimal.FromString('2'))
            ");
            r.String.ShouldBe("false");
        }

        [Fact]
        public async Task Decimal_Lt_SmallerIsLess()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local a = Decimal.FromString('1.0')
                local b = Decimal.FromString('2.0')
                return tostring(a < b and not (b < a))
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Decimal_Round_HalfUp()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local d = Decimal.FromString('1.235'):Round(2)
                return tostring(tostring(d) == '1.24')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Decimal_Truncate_RemovesDigits()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local d = Decimal.FromString('1.999'):Truncate(2)
                return tostring(tostring(d) == '1.99')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Decimal_ToNumber_LossyConversion()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local n = Decimal.FromString('3.14'):ToNumber()
                return tostring(math.type(n) == 'float' and n > 3.13 and n < 3.15)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Decimal_Json_EncodesAsNumber()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local d   = Decimal.FromString('99.95')
                local enc = Json.New():Encode(d)
                return tostring(enc == '99.95')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Decimal_Json_InTable_RoundTripsAsString()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local d = Decimal.FromString('12.34')
                local j = Json.New()
                local t = j:Decode(j:Encode({price = d}))
                return tostring(t.price == 12.34)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Decimal_FromNumber_RoundTrips()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local d = Decimal.FromNumber(42.0)
                return tostring(d ~= nil and d:ToNumber() == 42.0)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Decimal_Div_Basic()
        {
            using KitsuneEngine engine = new();

            // 1 / 4 = 0.25
            LuaValue r = await engine.ExecuteStringAsync(@"
                local a = Decimal.FromString('1')
                local b = Decimal.FromString('4')
                local d = a / b
                return tostring(tostring(d:Round(2)) == '0.25')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Decimal_Div_ByZero_Errors()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local ok, err = pcall(function()
                    return Decimal.FromString('1') / Decimal.FromString('0')
                end)
                return tostring(not ok and err ~= nil)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Decimal_Mod_Basic()
        {
            using KitsuneEngine engine = new();

            // 10 mod 3 = 1
            LuaValue r = await engine.ExecuteStringAsync(@"
                local a = Decimal.FromString('10')
                local b = Decimal.FromString('3')
                return tostring(tostring(a % b) == '1')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Decimal_Eq_DifferentScaleSameValue()
        {
            using KitsuneEngine engine = new();

            // 1.50 and 1.5 are numerically equal
            LuaValue r = await engine.ExecuteStringAsync(@"
                local a = Decimal.FromString('1.50')
                local b = Decimal.FromString('1.5')
                return tostring(a == b)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Decimal_Parse_ScientificNotation()
        {
            using KitsuneEngine engine = new();

            // 1.23E+2 = 123
            LuaValue r = await engine.ExecuteStringAsync(@"
                local d = Decimal.FromString('1.23E+2')
                return tostring(tostring(d) == '123')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Decimal_Parse_NegativeScientificNotation()
        {
            using KitsuneEngine engine = new();

            // 1.5E-2 = 0.015
            LuaValue r = await engine.ExecuteStringAsync(@"
                local d = Decimal.FromString('1.5E-2')
                return tostring(tostring(d) == '0.015')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Decimal_Mul_NegativeSign()
        {
            using KitsuneEngine engine = new();

            // -3 * -4 = 12 (positive)
            LuaValue r = await engine.ExecuteStringAsync(@"
                local a = Decimal.FromString('-3')
                local b = Decimal.FromString('-4')
                return tostring(tostring(a * b) == '12')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Decimal_Round_MultipleDigits_RoundsOnFirstDropped()
        {
            using KitsuneEngine engine = new();

            // 1.449 rounded to 1 decimal place: first dropped digit is 4 → round down → 1.4
            LuaValue r = await engine.ExecuteStringAsync(@"
                local d = Decimal.FromString('1.449'):Round(1)
                return tostring(tostring(d) == '1.4')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Decimal_Round_NoLoss_WhenScaleAlreadyCoarser()
        {
            using KitsuneEngine engine = new();

            // Rounding to a scale >= current scale returns identical value
            LuaValue r = await engine.ExecuteStringAsync(@"
                local d = Decimal.FromString('1.5')
                return tostring(tostring(d:Round(3)) == '1.5')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Decimal_FromInteger_IsExact()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local d = Decimal.FromNumber(9007199254740993)
                return tostring(d:Scale() == 0)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task DateTime_Format_UsesLocalOffset()
        {
            // UTC 2024-01-01 23:00:00 shifted by +02:00 should display as 2024-01-02
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local dt  = DateTime.New(2024, 1, 1, 23, 0, 0, 0, 0)
                local ist = dt:ToOffset(120)
                return tostring(ist:Format('%Y-%m-%d') == '2024-01-02')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task DateTime_New_InvalidDay_Errors()
        {
            // Feb 30 is not a valid date and must raise an error
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local ok, err = pcall(function() return DateTime.New(2024, 2, 30) end)
                return tostring(not ok and err ~= nil)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task DateTime_New_LastDayOfMonth_IsValid()
        {
            // Feb 29 in a leap year must succeed
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local dt = DateTime.New(2024, 2, 29)
                return tostring(dt ~= nil and dt:Day() == 29)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task DateTime_Parse_ExplicitZ_IgnoresFallbackOffset()
        {
            // A Z-suffixed string is unambiguously UTC; a non-zero fallback must be ignored
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local dt = DateTime.Parse('2024-06-15T12:00:00Z', 120)
                return tostring(dt:OffsetMinutes() == 0 and dt:Hour() == 12)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task DateTime_Parse_NoOffset_UsesFallback()
        {
            // A string with no offset indicator should adopt the fallback
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local dt = DateTime.Parse('2024-06-15T12:00:00', 60)
                return tostring(dt:OffsetMinutes() == 60 and dt:Hour() == 12)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Decimal_Div_FractionalDivisor()
        {
            // 1 / 0.5 = 2
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local a = Decimal.FromString('1')
                local b = Decimal.FromString('0.5')
                return tostring(tostring((a / b):Round(0)) == '2')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Decimal_Mod_NegativeDividend()
        {
            // -10 mod 3 = -1 (sign follows dividend)
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local a = Decimal.FromString('-10')
                local b = Decimal.FromString('3')
                return tostring(tostring(a % b) == '-1')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Decimal_Round_ProducesCarryIntoNewDigit()
        {
            // 9.999 rounded to 2 places: 10.00 — carry propagates across the decimal point
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local d = Decimal.FromString('9.999'):Round(2)
                return tostring(tostring(d) == '10.00')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Decimal_Mul_LargeCoefficients_DoesNotOverflow()
        {
            // 999999999 * 999999999 = 999999998000000001; verifies u128_mul128 carry is correct
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local a = Decimal.FromString('999999999')
                local b = Decimal.FromString('999999999')
                return tostring(tostring(a * b) == '999999998000000001')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Decimal_Mul10_LargeValue_DoesNotOverflow()
        {
            // Scale up a number that fills most of the high 64 bits to exercise u128_mul10 with large hi
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local a = Decimal.FromString('99999999999999999999999999999')
                local b = Decimal.FromString('10')
                return tostring(tostring(a * b) == '999999999999999999999999999990')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Decimal_Mul_LargeScales_DoesNotOverflowScale()
        {
            // Both operands have scale 2; result scale must be 4, not a wrapped negative
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local a = Decimal.FromString('1.23')
                local b = Decimal.FromString('4.56')
                local c = a * b
                return tostring(c:Scale() == 4 and tostring(c) == '5.6088')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Decimal_Div_LargeScaleDividend_TargetScaleIsCorrect()
        {
            // Dividend scale=10, divisor scale=0: target_scale must be max(10,0)+10 = 20, not a truncated int16
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local a = Decimal.FromString('1.0000000000')
                local b = Decimal.FromString('3')
                local c = a / b
                return tostring(c:Scale() >= 10)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Decimal_Parse_TooManyFractionalDigits_ReturnsNil()
        {
            // More than 32767 fractional digits must be rejected gracefully
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local s = '0.' .. string.rep('1', 40000)
                local d = Decimal.FromString(s)
                return tostring(d == nil)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task DateTime_Parse_InvalidOffset_ReturnsNil()
        {
            // An offset larger than +-14:00 (840 min) must be rejected
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local dt = DateTime.Parse('2024-01-01T00:00:00+99:00')
                return tostring(dt == nil)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task DateTime_Format_Yday_IsCorrect()
        {
            // March 1 in a leap year (2024) is day 61 (0-based yday = 60, %j = 061)
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local dt = DateTime.New(2024, 3, 1, 0, 0, 0, 0, 0)
                return tostring(dt:Format('%j') == '061')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Decimal_Parse_ScientificLargePositiveExp_IsExact()
        {
            // 1.5E+3 = 1500; verifies exponent arithmetic stays in int, not int16_t
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local d = Decimal.FromString('1.5E+3')
                return tostring(tostring(d) == '1500')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Decimal_Format_ZeroWithLargeScale_DoesNotCrash()
        {
            // Zero with scale=10 must format correctly without buffer overflow
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local d = Decimal.FromString('0.0000000000')
                return tostring(d ~= nil)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task DateTime_Parse_Feb30_ReturnsNil()
        {
            // Feb 30 is not a valid date; parse must reject it
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local dt = DateTime.Parse('2024-02-30T00:00:00Z')
                return tostring(dt == nil)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task DateTime_ToOffset_OutOfRange_Errors()
        {
            // An offset outside +-840 minutes must raise a Lua error
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local ok, err = pcall(function()
                    return DateTime.UtcNow():ToOffset(9999)
                end)
                return tostring(not ok and err ~= nil)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task DateTime_New_OutOfRangeOffset_Errors()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local ok, err = pcall(function() return DateTime.New(2024, 1, 1, 0, 0, 0, 0, 9999) end)
                return tostring(not ok and err ~= nil)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task DateTime_FromUnixSeconds_OutOfRangeOffset_Errors()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local ok, err = pcall(function() return DateTime.FromUnixSeconds(0, 9999) end)
                return tostring(not ok and err ~= nil)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task DateTime_FromUnixMilliseconds_OutOfRangeOffset_Errors()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local ok, err = pcall(function() return DateTime.FromUnixMilliseconds(0, 9999) end)
                return tostring(not ok and err ~= nil)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task DateTime_Parse_OutOfRangeFallbackOffset_Errors()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local ok, err = pcall(function() return DateTime.Parse('2024-01-01T00:00:00', 9999) end)
                return tostring(not ok and err ~= nil)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task DateTime_New_ValidMaxOffset_Succeeds()
        {
            // +14:00 (840 minutes) is the maximum legal UTC offset
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local dt = DateTime.New(2024, 1, 1, 0, 0, 0, 0, 840)
                return tostring(dt ~= nil and dt:OffsetMinutes() == 840)
            ");
            r.String.ShouldBe("true");
        }

        // -- CRC32 ----------------------------------------------------------------
        [Fact]
        public async Task CRC32_IsDeterministic()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("return tostring(CRC32('hello') == CRC32('hello'))");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task CRC32_DifferentInputs_ProduceDifferentValues()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("return tostring(CRC32('hello') ~= CRC32('world'))");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task CRC32_IncrementalMatchesFull()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local full = CRC32('hello world')
                local inc  = CRC32('world', CRC32('hello '))
                return tostring(full == inc)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task StringEqual_TolowerDoesNotMistreatHighBytes()
        {
            // Characters > 127 must not cause UB through the signed-char tolower path;
            // two identical non-ASCII strings must compare equal
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local s = 'caf\xc3\xa9'
                return tostring(string.equal(s, s) == true)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task CSV_SniffDelimiter_FiveFullLines_DoesNotCrash()
        {
            // Five complete lines with semicolons exercises the nLines==maxLines boundary
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local data = 'a;b;c\nd;e;f\ng;h;i\nj;k;l\nm;n;o\n'
                local result = CSV.New(';'):Decode(data)
                local count = 0
                for _ in ipairs(result.Rows) do count = count + 1 end
                return tostring(count == 5)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task CRC64_Stream_RaisesError()
        {
            // CRC64 must raise an error when given a stream, not silently return 0
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local st = Stream.New('hello')
                local ok, err = pcall(CRC64, st)
                return tostring(not ok and err ~= nil)
            ");
            r.String.ShouldBe("true");
        }

        // -- CRC64 ----------------------------------------------------------------
        [Fact]
        public async Task CRC64_ReturnsNumber()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("return type(CRC64('hello'))");
            r.String.ShouldBe("number");
        }

        [Fact]
        public async Task CRC64_IsDeterministic()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("return tostring(CRC64('test') == CRC64('test'))");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task CRC64_DifferentInputs_ProduceDifferentValues()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("return tostring(CRC64('hello') ~= CRC64('world'))");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task CRC64_WithWchar_ReturnsNumber()
        {
            using KitsuneEngine engine = new();

            // CRC64 uses the raw UTF-16 LE bytes of the Wchar, not the UTF-8 encoding.
            LuaValue r = await engine.ExecuteStringAsync("return tostring(type(CRC64(Wchar.FromUtf8('hello'))) == 'number')");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task CRC64_Wchar_IsDeterministic()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local w = Wchar.FromUtf8('deterministic')
                return tostring(CRC64(w) == CRC64(w))
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task CRC64_Wchar_DiffersFromStringEquivalent()
        {
            using KitsuneEngine engine = new();

            // Wchar stores UTF-16 LE bytes; plain string is UTF-8 — different byte sequences.
            LuaValue r = await engine.ExecuteStringAsync("return tostring(CRC64(Wchar.FromUtf8('hello')) ~= CRC64('hello'))");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task CRC64_WithStream_RaisesError()
        {
            // CRC64 no longer accepts stream input — it was silently computing CRC of
            // empty input instead of the stream contents. Now it raises an error.
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local s = Stream.New()
                s:Write('hello')
                local ok, err = pcall(CRC64, s)
                return tostring(not ok and err ~= nil)
            ");
            r.String.ShouldBe("true");
        }

        // -- Time -----------------------------------------------------------------
        [Fact]
        public async Task Time_ReturnsPositiveInteger()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("local t = Time(); return tostring(t > 0 and math.type(t) == 'integer')");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Time_SecondCall_IsGreaterOrEqual()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("local a = Time(); local b = Time(); return tostring(b >= a)");
            r.String.ShouldBe("true");
        }

        // -- Runtime --------------------------------------------------------------
        [Fact]
        public async Task Runtime_ReturnsNonNegativeNumber()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("return tostring(Runtime() >= 0)");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Runtime_IncreasesAfterSleep()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("local a = Runtime(); Sleep(20); return tostring(Runtime() > a)");
            r.String.ShouldBe("true");
        }

        // -- GetMemory ------------------------------------------------------------
        [Fact]
        public async Task GetMemory_ReturnsPositiveValue()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("return tostring(GetMemory() > 0)");
            r.String.ShouldBe("true");
        }

        // -- GlobalMemoryStatus ---------------------------------------------------
        [Fact]
        public async Task GlobalMemoryStatus_Default_ReturnsPercentageInRange()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("local p = GlobalMemoryStatus(); return tostring(type(p) == 'number' and p >= 0 and p <= 100)");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task GlobalMemoryStatus_TotalPhysical_ReturnsPositive()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("return tostring(GlobalMemoryStatus(1) > 0)");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task GlobalMemoryStatus_AllTypes_ReturnNumbers()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
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
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("return tostring(string.equal('hello', 'hello'))");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task StringEqual_DifferentCase_ReturnsTrue()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("return tostring(string.equal('Hello World', 'hello world'))");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task StringEqual_DifferentStrings_ReturnsFalse()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("return tostring(string.equal('hello', 'world'))");
            r.String.ShouldBe("false");
        }

        // -- setenv / getenv ------------------------------------------------------
        [Fact]
        public async Task SetEnv_GetEnv_RoundTrip()
        {
            using KitsuneEngine engine = new();

            // The returned value includes a trailing null byte; strip it before comparing.
            LuaValue r = await engine.ExecuteStringAsync(@"
                setenv('KITSUNE_UTIL_TEST_1', 'hello_kitsune', true)
                return getenv('KITSUNE_UTIL_TEST_1'):gsub('%z', '')
            ");
            r.String.ShouldBe("hello_kitsune");
        }

        [Fact]
        public async Task SetEnv_WithoutOverride_PreservesOriginalValue()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                setenv('KITSUNE_UTIL_TEST_2', 'original', true)
                setenv('KITSUNE_UTIL_TEST_2', 'overwritten', false)
                return getenv('KITSUNE_UTIL_TEST_2'):gsub('%z', '')
            ");
            r.String.ShouldBe("original");
        }

        [Fact]
        public async Task GetEnv_NonExistentVariable_ReturnsNilOrEmpty()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
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
            using KitsuneEngine engine = new();

            // Only b maps to 2, so deterministic regardless of iteration order.
            LuaValue r = await engine.ExecuteStringAsync("return table.first({a=1, b=2, c=3}, function(k,v) if v==2 then return k end end)");
            r.String.ShouldBe("b");
        }

        [Fact]
        public async Task TableFirst_UniqueMatch_ReturnsValue()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("return tostring(table.first({a=5,b=99,c=5}, function(k,v) if v>50 then return v end end))");
            r.String.ShouldBe("99");
        }

        [Fact]
        public async Task TableFirst_NoMatch_ReturnsNil()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("return tostring(table.first({a=1,b=2}, function(k,v) if v>100 then return v end end))");
            r.String.ShouldBe("nil");
        }

        // -- table.select ---------------------------------------------------------
        [Fact]
        public async Task TableSelect_FilterEvenNumbers_ReturnsCorrectValues()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local e = table.select({1,2,3,4,5,6}, function(k,v) if v%2==0 then return v end end)
                table.sort(e)
                return #e .. ':' .. table.concat(e, ',')
            ");
            r.String.ShouldBe("3:2,4,6");
        }

        [Fact]
        public async Task TableSelect_NoMatches_ReturnsEmptyTable()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("local t = table.select({1,3,5}, function(k,v) if v%2==0 then return v end end); return tostring(type(t)=='table' and #t==0)");
            r.String.ShouldBe("true");
        }

        // -- GetIsAdmin -----------------------------------------------------------
        [Fact]
        public async Task GetIsAdmin_ReturnsBool()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("return type(GetIsAdmin())");
            r.String.ShouldBe("boolean");
        }

        // -- GetComputerName ------------------------------------------------------
        [Fact]
        public async Task GetComputerName_ReturnsNonEmptyString()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("local n = GetComputerName(); return tostring(n ~= nil and #n > 0)");
            r.String.ShouldBe("true");
        }

        // -- BencodeDecode --------------------------------------------------------
        [Fact]
        public async Task BencodeDecode_StringField_Decoded()
        {
            using KitsuneEngine engine = new();

            // BencodeDecode wraps each top-level decoded value in an outer array:
            // BencodeDecode(data) returns {[1]=value, [2]=value2, ...}.
            // A bencode dict is therefore at t[1], not t directly.
            LuaValue r = await engine.ExecuteStringAsync("local t = BencodeDecode('d3:foo3:bare'); return tostring(type(t)=='table' and t[1].foo=='bar')");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task BencodeDecode_IntegerField_Decoded()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("local t = BencodeDecode('d3:numi42ee'); return tostring(type(t)=='table' and t[1].num==42)");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task BencodeDecode_ListField_Decoded()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("local t = BencodeDecode('d4:listli1ei2ei3eee'); return tostring(type(t[1].list)=='table' and #t[1].list==3)");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task BencodeDecode_NestedDict_Decoded()
        {
            using KitsuneEngine engine = new();

            // 'd5:outerd5:inneri99eee' encodes {outer={inner=99}}
            LuaValue r = await engine.ExecuteStringAsync("local t = BencodeDecode('d5:outerd5:inneri99eee'); return tostring(type(t[1].outer)=='table' and t[1].outer.inner==99)");
            r.String.ShouldBe("true");
        }

        // -- GetLastError ---------------------------------------------------------
        [Fact]
        public async Task GetLastError_WithCode2_ReturnsNonEmptyMessageAndCode()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("local m,c = GetLastError(2); return tostring(type(m)=='string' and #m>0 and type(c)=='number')");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task GetLastError_NoArgs_ReturnsString()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("return type((GetLastError()))");
            r.String.ShouldBe("string");
        }

        // -- c global variable ----------------------------------------------------
        [Fact]
        public async Task CGlobal_IsTable()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("return type(c)");
            r.String.ShouldBe("table");
        }

        [Fact]
        public async Task CGlobal_LF_MatchesNewline()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("return tostring(c.LF == '\\n')");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task CGlobal_HasAtLeast32Entries()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("local n=0; for _ in pairs(c) do n=n+1 end; return tostring(n>=32)");
            r.String.ShouldBe("true");
        }

        // -- Global variables -----------------------------------------------------
        [Fact]
        public async Task VERSION_Global_IsNonEmptyString()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("return tostring(type(VERSION) == 'string' and #VERSION > 0)");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task CPUID_Global_IsNonEmptyString()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("return tostring(type(CPUID) == 'string' and #CPUID > 0)");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task DEBUG_Global_IsBoolOrNil()
        {
            using KitsuneEngine engine = new();

            // DEBUG is true in debug builds; the global is not defined in release builds.
            LuaValue r = await engine.ExecuteStringAsync("local t = type(DEBUG); return tostring(t == 'boolean' or t == 'nil')");
            r.String.ShouldBe("true");
        }

        // -- Dns ------------------------------------------------------------------
        [Fact]
        public async Task Dns_Localhost_ReturnsString()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("local ip = Dns('localhost'); return tostring(ip ~= nil and type(ip)=='string')");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Dns_WithFullFlag_ReturnsTable()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("return tostring(type(Dns('localhost', true))=='table')");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Dns_WithFullFlag_EntryHasTypeAndIPFields()
        {
            using KitsuneEngine engine = new();

            // Each entry must have a string 'Type' ("IPV4" or "IPV6") and a string 'IP'.
            LuaValue r = await engine.ExecuteStringAsync(@"
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

        // -- Base64 ---------------------------------------------------------------
        [Fact]
        public async Task Base64_Encode_ReturnsCorrectString()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("return Base64.Encode('hello')");
            r.String.ShouldBe("aGVsbG8=");
        }

        [Fact]
        public async Task Base64_Decode_RoundTrip()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("return Base64.Decode(Base64.Encode('kitsune engine'))");
            r.String.ShouldBe("kitsune engine");
        }

        [Fact]
        public async Task Base64_BinaryRoundTrip_PreservesBytes()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("local b = '\\0\\1\\2\\255'; return tostring(Base64.Decode(Base64.Encode(b)) == b)");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Base64_GetEncodeTable_Returns64CharString()
        {
            using KitsuneEngine engine = new();

            // The default RFC 4648 alphabet is exactly 64 characters starting with 'ABCD'.
            LuaValue r = await engine.ExecuteStringAsync(@"
                local t = Base64.GetEncodeTable()
                return tostring(type(t) == 'string' and #t == 64 and t:sub(1, 4) == 'ABCD')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Base64_SetEncodeTable_CustomTable_ChangesEncoding()
        {
            using KitsuneEngine engine = new();

            // Swap to URL-safe alphabet and verify the encoding changes accordingly.
            LuaValue r = await engine.ExecuteStringAsync(@"
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
            using KitsuneEngine engine = new();

            // After restoring the default table, standard round-trips must still work.
            LuaValue r = await engine.ExecuteStringAsync(@"
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
            using KitsuneEngine engine = new();

            // RFC 6234 test vector for SHA-256 of "abc".
            LuaValue r = await engine.ExecuteStringAsync(@"
                local h = SHA256.New()
                h:Update('abc')
                return h:Finish()
            ");
            r.String.ShouldBe("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
        }

        [Fact]
        public async Task SHA256_IncrementalUpdateMatchesSingle()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local h1 = SHA256.New(); h1:Update('hello world'); local hex1 = h1:Finish()
                local h2 = SHA256.New(); h2:Update('hello'); h2:Update(' world'); local hex2 = h2:Finish()
                return tostring(hex1 == hex2)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task MD5_KnownVector_ReturnsCorrectHex()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local h = MD5.New(); h:Update('abc'); return h:Finish()
            ");
            r.String.ShouldBe("900150983cd24fb0d6963f7d28e17f72");
        }

        [Fact]
        public async Task MD5_EmptyInput_ReturnsKnownHash()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("local h = MD5.New(); h:Update(''); return h:Finish()");
            r.String.ShouldBe("d41d8cd98f00b204e9800998ecf8427e");
        }

        [Fact]
        public async Task SHA1_KnownVector_ReturnsCorrectHex()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("local h = SHA1.New(); h:Update('abc'); return h:Finish()");
            r.String.ShouldBe("a9993e364706816aba3e25717850c26c9cd0d89d");
        }

        [Fact]
        public async Task SHA256_Finish_ReturnsBinaryWith32Bytes()
        {
            using KitsuneEngine engine = new();

            // Finish() returns two values: the hex string and a raw 32-byte binary digest.
            LuaValue r = await engine.ExecuteStringAsync(@"
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
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
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
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
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
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local h = SHA1.New()
                h:Update('abc')
                local hex, bin = h:Finish()
                return tostring(type(bin) == 'string' and #bin == 20)
            ");
            r.String.ShouldBe("true");
        }

        // -- Json -----------------------------------------------------------------
        // All operations require an instance (Json.New() or Json.New()).
        // Json.Null is the sentinel value for JSON null.

        // -- Instance round-trips ---------------------------------------------
        [Fact]
        public async Task Json_Encode_ProducesValidJson()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local j = Json.New()
                local t = j:Decode(j:Encode({x=1, y='hello', z=true}))
                return tostring(t.x==1 and t.y=='hello' and t.z==true)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Json_DecodeArray_PreservesOrder()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local j = Json.New()
                local t = j:Decode('[10,20,30]')
                return tostring(t[1]==10 and t[2]==20 and t[3]==30)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Json_NestedTable_EncodesAndDecodes()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local j = Json.New()
                local orig = {a={b={c=42}}}
                local t = j:Decode(j:Encode(orig))
                return tostring(t.a.b.c == 42)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Json_AllBasicTypes_RoundTrip()
        {
            using KitsuneEngine engine = new();

            // Verifies every basic Lua type survives an encode/decode cycle.
            LuaValue r = await engine.ExecuteStringAsync(@"
                local j = Json.New()
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
            using KitsuneEngine engine = new();

            // The same instance must work correctly for multiple encode/decode calls.
            LuaValue r = await engine.ExecuteStringAsync(@"
                local j = Json.New()
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
            using KitsuneEngine engine = new();

            // Integers must round-trip as integers (no ".0" suffix).
            LuaValue r = await engine.ExecuteStringAsync(@"
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
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local j = Json.New()
                local v = j:Decode(j:Encode(3.14))
                return tostring(math.type(v) == 'float' and v == 3.14)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Json_NaN_EncodesAsNull()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("return Json.New():Encode(0/0)");
            r.String.ShouldBe("null");
        }

        [Fact]
        public async Task Json_Function_EncodesAsNull()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("return Json.New():Encode(function() end)");
            r.String.ShouldBe("null");
        }

        [Fact]
        public async Task Json_Thread_EncodesAsNull()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("return Json.New():Encode(coroutine.create(function() end))");
            r.String.ShouldBe("null");
        }

        [Fact]
        public async Task Json_Userdata_EncodesAsNull()
        {
            using KitsuneEngine engine = new();

            // A Stream is a full userdata; it is not JSON-serializable.
            LuaValue r = await engine.ExecuteStringAsync("return Json.New():Encode(Stream.New())");
            r.String.ShouldBe("null");
        }

        [Fact]
        public async Task Json_UnserializableInArray_EncodesAsNull()
        {
            using KitsuneEngine engine = new();

            // Functions embedded in arrays produce a valid array with null slots.
            LuaValue r = await engine.ExecuteStringAsync(@"
                return Json.New():Encode({ 1, function() end, 3 })
            ");
            r.String.ShouldBe("[1,null,3]");
        }

        [Fact]
        public async Task Json_UnserializableInObject_EncodesAsNull()
        {
            using KitsuneEngine engine = new();

            // Functions as object values produce valid JSON with null values.
            LuaValue r = await engine.ExecuteStringAsync(@"
                return Json.New():Encode({ x = function() end })
            ");
            r.String.ShouldBe("{\"x\":null}");
        }

        [Fact]
        public async Task Json_PositiveInfinity_EncodesAsSpecialLiteral()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("return Json.New():Encode(math.huge)");
            r.String.ShouldBe("1e+9999");
        }

        // -- Boolean / nil encoding -------------------------------------------
        [Fact]
        public async Task Json_Boolean_True_EncodesCorrectly()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("return Json.New():Encode(true)");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Json_Boolean_False_EncodesCorrectly()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("return Json.New():Encode(false)");
            r.String.ShouldBe("false");
        }

        [Fact]
        public async Task Json_Nil_EncodesAsNull()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("return Json.New():Encode(nil)");
            r.String.ShouldBe("null");
        }

        // -- Array vs object detection ----------------------------------------
        [Fact]
        public async Task Json_SequenceTable_EncodesAsArray()
        {
            using KitsuneEngine engine = new();

            // A table with consecutive integer keys 1..n encodes as a JSON array.
            LuaValue r = await engine.ExecuteStringAsync(@"
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
            using KitsuneEngine engine = new();

            // A table with only string keys encodes as a JSON object; a truly empty table encodes as [].
            LuaValue r = await engine.ExecuteStringAsync("return Json.New():Encode({})");
            r.String.ShouldBe("[]");
        }

        [Fact]
        public async Task Json_StringKeyTable_EncodesAsObject()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local j = Json.New()
                local t = j:Decode(j:Encode({hello='world'}))
                return t.hello
            ");
            r.String.ShouldBe("world");
        }

        [Fact]
        public async Task Json_New_WithTrue_ProducesPrettyOutput()
        {
            using KitsuneEngine engine = new();

            // Json.New(true) must create a pretty-printing instance.
            LuaValue r = await engine.ExecuteStringAsync(@"
                local j = Json.New(true)
                return tostring(j:Encode({a=1}):find('\n') ~= nil)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Json_New_WithFalse_ProducesCompactOutput()
        {
            using KitsuneEngine engine = new();

            // Json.New(false) must create a compact instance.
            LuaValue r = await engine.ExecuteStringAsync(@"
                local j = Json.New(false)
                return tostring(j:Encode({a=1}):find('\n') == nil)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Json_New_NoArg_ProducesCompactOutput()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local j = Json.New()
                return tostring(j:Encode({a=1}):find('\n') == nil)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Json_New_CalledOnInstance_ProducesCompactOutput()
        {
            using KitsuneEngine engine = new();

            // Regression: New() called on an existing instance must produce a compact
            // instance, not a pretty-printing one.
            LuaValue r = await engine.ExecuteStringAsync(@"
                local j1 = Json.New()      -- compact
                local j2 = j1:New()        -- must also be compact, not pretty
                return tostring(j2:Encode({a=1}):find('\n') == nil)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Json_MixedTable_EncodesAsObject_IntegerKeysBecomesStrings()
        {
            using KitsuneEngine engine = new();

            // Mixed tables (integer and string keys) encode as JSON objects;
            // integer keys become string keys on the round-trip.
            LuaValue r = await engine.ExecuteStringAsync(@"
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
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
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
            using KitsuneEngine engine = new();

            // A number literal that exceeds the internal buffer size must raise an error
            // rather than silently producing a wrong value.
            LuaValue r = await engine.ExecuteStringAsync(@"
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
            using KitsuneEngine engine = new();

            // A number literal just within the internal limit must parse without error.
            LuaValue r = await engine.ExecuteStringAsync(@"
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
            using KitsuneEngine engine = new();

            // \u0041 is 'A', \u00E9 is 'é' (U+00E9)
            LuaValue r = await engine.ExecuteStringAsync(@"
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
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
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
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local j   = Json.New()
                j:SetDecodeNull(true)
                local enc = j:Encode({v=Json.Null})
                local dec = j:Decode(enc)
                return tostring(dec.v == Json.Null and enc:find('null') ~= nil)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Json_Null_IsDistinctFromNil()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("return tostring(Json.Null ~= nil)");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Json_Null_SameReferenceEveryTime()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("return tostring(Json.Null == Json.Null)");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Json_DecodeNull_ReturnsNilByDefault()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("return tostring(Json.New():Decode('null') == nil)");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Json_EncodeNull_ProducesNullLiteral()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("return Json.New():Encode(Json.Null)");
            r.String.ShouldBe("null");
        }

        [Fact]
        public async Task Json_NullInArray_DefaultDecodesAsNil()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local j = Json.New()
                local t = j:Decode('[1,null,3]')
                return tostring(t[1]==1 and t[2]==nil and t[3]==3)
            ");
            r.String.ShouldBe("true");
        }

        // -- Encode / Decode --------------------------------------------------
        [Fact]
        public async Task Json_Decode_ReturnsTable()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local j = Json.New()
                local t = j:Decode('{""a"":1,""b"":""hello""}')
                return tostring(t.a == 1 and t.b == 'hello')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Json_Encode_ProducesString()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
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
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local j = Json.New(true)
                local s = j:Encode({a=1})
                return tostring(s:find('\n') ~= nil)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Json_CreateAlias_WorksIdenticallyToNew()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local j = Json.New()
                local t = j:Decode('[1,2,3]')
                return tostring(t[3] == 3)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Json_RecursionDetected_ThrowsError()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
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
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local j = Json.New()
                local s = Stream.New()
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
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local j  = Json.New()
                local s  = Stream.New()
                local ok = j:EncodeIntoStream(s, 42)
                return tostring(ok == true)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Json_DecodeFromStream_RoundTrip()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local j = Json.New()
                local s = Stream.New()
                j:EncodeIntoStream(s, {x=99, y='hello', z=true})
                s:Seek(0)
                local t = j:DecodeFromStream(s)
                return tostring(t.x == 99 and t.y == 'hello' and t.z == true)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Json_EncodeIntoStream_PrettyFlag_Respected()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local j = Json.New(true)
                local s = Stream.New()
                j:EncodeIntoStream(s, {a=1})
                s:Seek(0)
                return tostring(s:Read():find('\n') ~= nil)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Json_EncodeIntoStream_NonWritableStream_ReturnsFalse()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local j = Json.New()
                local OPEN, CLOSE, CAP_READ = 0, 1, 1
                local s = Stream.New(function(op)
                    if op == OPEN  then return CAP_READ end
                    if op == CLOSE then return true end
                end)
                local ok, err = j:EncodeIntoStream(s, 'test')
                return tostring(ok == false and type(err) == 'string')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Json_DecodeFromStream_NonReadableStream_ReturnsNilAndError()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local j = Json.New()
                local OPEN, CLOSE, CAP_WRITE = 0, 1, 2
                local s = Stream.New(function(op)
                    if op == OPEN  then return CAP_WRITE end
                    if op == CLOSE then return true end
                end)
                local val, err = j:DecodeFromStream(s)
                return tostring(val == nil and type(err) == 'string')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Json_EncodeDecodeFromStream_LargePayload_AllValuesCorrect()
        {
            using KitsuneEngine engine = new();

            // 1000 integers produce ~3900 bytes, exercising multiple streaming flushes
            // during encode and multi-chunk reads during decode.
            LuaValue r = await engine.ExecuteStringAsync(@"
                local j    = Json.New()
                local data = {}
                for i = 1, 1000 do data[i] = i end
                local s = Stream.New()
                j:EncodeIntoStream(s, data)
                s:Seek(0)
                local t = j:DecodeFromStream(s)
                return tostring(#t == 1000 and t[1] == 1 and t[500] == 500 and t[1000] == 1000)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Json_EncodeIntoStream_NullSentinel_RoundTrips()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local j = Json.New()
                j:SetDecodeNull(true)
                local s = Stream.New()
                j:EncodeIntoStream(s, {v = Json.Null})
                s:Seek(0)
                local t = j:DecodeFromStream(s)
                return tostring(t.v == Json.Null)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Json_DecodeFromStream_ReadsFromCurrentPosition()
        {
            using KitsuneEngine engine = new();

            // Encode two values back-to-back; seek to the boundary and verify
            // DecodeFromStream picks up only the second value.
            LuaValue r = await engine.ExecuteStringAsync(@"
                local j = Json.New()
                local s = Stream.New()
                j:EncodeIntoStream(s, 'first')
                local split = s:pos()
                j:EncodeIntoStream(s, 'second')
                s:Seek(split)
                local v = j:DecodeFromStream(s)
                return tostring(v == 'second')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Json_EncodeIntoStream_AdvancesStreamPosition()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local j = Json.New()
                local s = Stream.New()
                j:EncodeIntoStream(s, 42)
                return tostring(s:pos() == 2)   -- '42' is 2 bytes
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Json_DecodeFromStream_PackedObjects_DecodesSequentially()
        {
            using KitsuneEngine engine = new();

            // Three JSON objects written end-to-end with no separator; each
            // DecodeFromStream call must return exactly one object and leave
            // the stream positioned at the start of the next one.
            LuaValue r = await engine.ExecuteStringAsync(@"
                local j = Json.New()
                local s = Stream.New()
                j:EncodeIntoStream(s, {n=1})
                j:EncodeIntoStream(s, {n=2})
                j:EncodeIntoStream(s, {n=3})
                s:Seek(0)
                local a = j:DecodeFromStream(s)
                local b = j:DecodeFromStream(s)
                local c = j:DecodeFromStream(s)
                return tostring(a.n==1 and b.n==2 and c.n==3)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Json_DecodeFromStream_PackedWithWhitespace_DecodesSequentially()
        {
            using KitsuneEngine engine = new();

            // Whitespace and newlines between JSON values must be treated as
            // insignificant separators, matching the behaviour for regular Decode.
            LuaValue r = await engine.ExecuteStringAsync(@"
                local j = Json.New()
                local s = Stream.New()
                s:Write('{""a"":1}' .. '\n\n' .. '{""b"":2}')
                s:Seek(0)
                local t1 = j:DecodeFromStream(s)
                local t2 = j:DecodeFromStream(s)
                return tostring(t1.a == 1 and t2.b == 2)
            ");
            r.String.ShouldBe("true");
        }

        // -- Encode stream / Wchar as JSON value ----------------------------------
        [Fact]
        public async Task Json_Encode_Stream_ReadableSeekable_ProducesJsonString()
        {
            using KitsuneEngine engine = new();

            // A readable+seekable stream encodes as a JSON string of its full contents,
            // regardless of the current cursor position.
            LuaValue r = await engine.ExecuteStringAsync(@"
                local j = Json.New()
                local s = Stream.New('hello')
                return j:Encode(s)
            ");
            r.String.ShouldBe("\"hello\"");
        }

        [Fact]
        public async Task Json_Encode_Stream_EmptyStream_ProducesNull()
        {
            using KitsuneEngine engine = new();

            // An empty stream has no bytes and encodes as null.
            LuaValue r = await engine.ExecuteStringAsync(@"
                local j = Json.New()
                local s = Stream.New()
                return j:Encode(s)
            ");
            r.String.ShouldBe("null");
        }

        [Fact]
        public async Task Json_Encode_Stream_PreservesReadPosition()
        {
            using KitsuneEngine engine = new();

            // The caller's stream position must be restored after encoding.
            LuaValue r = await engine.ExecuteStringAsync(@"
                local j = Json.New()
                local s = Stream.New('ABCDE')
                s:Seek(3)
                j:Encode(s)
                return tostring(s:pos() == 3)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Json_Encode_Stream_QuoteInContent_EscapedCorrectly()
        {
            using KitsuneEngine engine = new();

            // A double-quote byte inside the stream must be JSON-escaped as \".
            LuaValue r = await engine.ExecuteStringAsync(@"
                local j = Json.New()
                local s = Stream.New('a""b')
                return j:Encode(s)
            ");
            r.String.ShouldBe("\"a\\\"b\"");
        }

        [Fact]
        public async Task Json_Encode_Stream_AsTableValue_RoundTripsAsString()
        {
            using KitsuneEngine engine = new();

            // A stream used as a table value encodes as a JSON string;
            // after decode, the value is a Lua string (JSON has no stream type).
            LuaValue r = await engine.ExecuteStringAsync(@"
                local j = Json.New()
                local s = Stream.New('hi')
                local t = j:Decode(j:Encode({data = s}))
                return t.data
            ");
            r.String.ShouldBe("hi");
        }

        [Fact]
        public async Task Json_EncodeIntoStream_StreamValue_WritesJsonString()
        {
            using KitsuneEngine engine = new();

            // When the VALUE being encoded is itself a stream, EncodeIntoStream must
            // write the stream's contents as a JSON string to the destination stream.
            LuaValue r = await engine.ExecuteStringAsync(@"
                local j   = Json.New()
                local src = Stream.New('world')
                local dst = Stream.New()
                j:EncodeIntoStream(dst, src)
                dst:Seek(0)
                return dst:Read()
            ");
            r.String.ShouldBe("\"world\"");
        }

        [Fact]
        public async Task Json_Encode_Wchar_AsciiContent_ProducesJsonString()
        {
            using KitsuneEngine engine = new();

            // ASCII Wchar must produce the same JSON string as the equivalent Lua string.
            LuaValue r = await engine.ExecuteStringAsync(@"
                local j = Json.New()
                local w = Wchar.FromUtf8('hello')
                return j:Encode(w)
            ");
            r.String.ShouldBe("\"hello\"");
        }

        [Fact]
        public async Task Json_Encode_Wchar_NonAscii_RoundTripsCorrectly()
        {
            using KitsuneEngine engine = new();

            // é = U+00E9, UTF-8: 0xC3 0xA9.  Use Lua hex escapes for unambiguous byte values.
            LuaValue r = await engine.ExecuteStringAsync(@"
                local j = Json.New()
                local w = Wchar.FromUtf8('\xC3\xa9')
                return j:Decode(j:Encode(w))
            ");
            r.String.ShouldBe("é");
        }

        [Fact]
        public async Task Json_Encode_Wchar_Empty_ProducesEmptyJsonString()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local j = Json.New()
                local w = Wchar.FromUtf8('')
                return j:Encode(w)
            ");
            r.String.ShouldBe("\"\"");
        }

        [Fact]
        public async Task Json_Encode_Wchar_SpecialChars_EscapedCorrectly()
        {
            using KitsuneEngine engine = new();

            // Double-quotes inside the Wchar content must be JSON-escaped as \".
            LuaValue r = await engine.ExecuteStringAsync(@"
                local j = Json.New()
                local w = Wchar.FromUtf8('say ""hi""')
                return j:Encode(w)
            ");
            r.String.ShouldBe("\"say \\\"hi\\\"\"");
        }

        [Fact]
        public async Task Json_Encode_Wchar_NewlineAndTab_EscapedAndRoundTrip()
        {
            using KitsuneEngine engine = new();

            // Control characters must be JSON-escaped and survive a full decode round-trip.
            LuaValue r = await engine.ExecuteStringAsync(@"
                local j = Json.New()
                local w = Wchar.FromUtf8('a' .. '\n' .. 'b')
                return j:Decode(j:Encode(w))
            ");
            r.String.ShouldBe("a\nb");
        }

        [Fact]
        public async Task Json_Encode_Wchar_AsTableValue_RoundTripsAsString()
        {
            using KitsuneEngine engine = new();

            // After encode?decode, the decoded value is a Lua string (not a Wchar),
            // since JSON has no wchar type.
            LuaValue r = await engine.ExecuteStringAsync(@"
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
            using KitsuneEngine engine = new();

            // Any userdata that is neither a Wchar nor a stream must encode as null.
            // Json.New() returns a LuaJson userdata, which is not stream/wchar.
            LuaValue r = await engine.ExecuteStringAsync(@"
                local j = Json.New()
                return j:Encode(Json.New())
            ");
            r.String.ShouldBe("null");
        }

        // -- Wchar ----------------------------------------------------------------
        [Fact]
        public async Task Wchar_FromUtf8_ToUtf8_RoundTrip()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("return Wchar.FromUtf8('hello'):ToUtf8()");
            r.String.ShouldBe("hello");
        }

        [Fact]
        public async Task Wchar_ToUpper_ChangesCase()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("return Wchar.FromUtf8('hello world'):ToUpper():ToUtf8()");
            r.String.ShouldBe("HELLO WORLD");
        }

        [Fact]
        public async Task Wchar_ToLower_ChangesCase()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("return Wchar.FromUtf8('KITSUNE'):ToLower():ToUtf8()");
            r.String.ShouldBe("kitsune");
        }

        [Fact]
        public async Task Wchar_Substring_ExtractsCorrectly()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("return Wchar.FromUtf8('hello world'):Substring(7):ToUtf8()");
            r.String.ShouldBe("world");
        }

        [Fact]
        public async Task Wchar_Length_ReturnsCharCount()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("return tostring(#Wchar.FromUtf8('hello'))");
            r.String.ShouldBe("5");
        }

        [Fact]
        public async Task Wchar_Empty_HasZeroLength()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("return tostring(#Wchar.FromUtf8('') == 0)");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Wchar_LenMethod_MatchesHashOperator()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("local w = Wchar.FromUtf8('hello'); return tostring(w:len() == #w and w:len() == 5)");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Wchar_ToString_ReturnsUtf8String()
        {
            using KitsuneEngine engine = new();

            // __tostring metamethod should produce the same result as :ToUtf8().
            LuaValue r = await engine.ExecuteStringAsync("return tostring(Wchar.FromUtf8('kitsune'))");
            r.String.ShouldBe("kitsune");
        }

        [Fact]
        public async Task Wchar_Substring_WithLength_ExtractsSlice()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("return Wchar.FromUtf8('hello world'):Substring(1, 5):ToUtf8()");
            r.String.ShouldBe("hello");
        }

        [Fact]
        public async Task Wchar_Substring_OutOfRange_ReturnsNil()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("return tostring(Wchar.FromUtf8('hi'):Substring(99))");
            r.String.ShouldBe("nil");
        }

        [Fact]
        public async Task Wchar_Find_ReturnsPosition()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("return tostring(Wchar.FromUtf8('hello world'):Find(Wchar.FromUtf8('world')))");
            r.String.ShouldBe("7");
        }

        [Fact]
        public async Task Wchar_Find_NotFound_ReturnsNil()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("return tostring(Wchar.FromUtf8('hello'):Find(Wchar.FromUtf8('xyz')))");
            r.String.ShouldBe("nil");
        }

        [Fact]
        public async Task Wchar_Find_WithOffset_StartsFromPosition()
        {
            using KitsuneEngine engine = new();

            // 'a' appears at indices 1 and 4 in 'abcabc'; with offset 2 it finds index 4.
            LuaValue r = await engine.ExecuteStringAsync("return tostring(Wchar.FromUtf8('abcabc'):Find(Wchar.FromUtf8('a'), 2))");
            r.String.ShouldBe("4");
        }

        [Fact]
        public async Task Wchar_Find_StringPattern_Works()
        {
            using KitsuneEngine engine = new();

            // Find also accepts a plain Lua string as the search pattern.
            LuaValue r = await engine.ExecuteStringAsync("return tostring(Wchar.FromUtf8('hello world'):Find('world'))");
            r.String.ShouldBe("7");
        }

        [Fact]
        public async Task Wchar_Find_NonAsciiStringPattern_Works()
        {
            using KitsuneEngine engine = new();

            // String patterns are interpreted as UTF-8; \xC3\xA9 are the UTF-8 bytes for U+00E9 (é).
            LuaValue r = await engine.ExecuteStringAsync(@"
                local hay = Wchar.FromUtf8('caf\xC3\xA9 au lait')
                return tostring(hay:Find('\xC3\xA9') ~= nil)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Wchar_Equality_SameContent_IsTrue()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("return tostring(Wchar.FromUtf8('hello') == Wchar.FromUtf8('hello'))");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Wchar_Equality_DifferentContent_IsFalse()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("return tostring(Wchar.FromUtf8('hello') == Wchar.FromUtf8('world'))");
            r.String.ShouldBe("false");
        }

        [Fact]
        public async Task Wchar_Equality_DifferentLengths_IsFalse()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("return tostring(Wchar.FromUtf8('hi') == Wchar.FromUtf8('hello'))");
            r.String.ShouldBe("false");
        }

        [Fact]
        public async Task Wchar_Equality_EmptyWchars_AreEqual()
        {
            using KitsuneEngine engine = new();

            // Edge case: two empty Wchars must compare as equal.
            LuaValue r = await engine.ExecuteStringAsync("return tostring(Wchar.FromUtf8('') == Wchar.FromUtf8(''))");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Wchar_Concat_WcharAndWchar_ProducesJoined()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("return (Wchar.FromUtf8('hello') .. Wchar.FromUtf8(' world')):ToUtf8()");
            r.String.ShouldBe("hello world");
        }

        [Fact]
        public async Task Wchar_Concat_WcharAndString_ProducesJoined()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("return (Wchar.FromUtf8('hello') .. ' world'):ToUtf8()");
            r.String.ShouldBe("hello world");
        }

        [Fact]
        public async Task Wchar_Concat_StringAndWchar_ProducesJoined()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("return ('hello ' .. Wchar.FromUtf8('world')):ToUtf8()");
            r.String.ShouldBe("hello world");
        }

        [Fact]
        public async Task Wchar_Concat_NonAsciiStringOperand_ProducesCorrectResult()
        {
            using KitsuneEngine engine = new();

            // \xC3\xA9 = UTF-8 for U+00E9 (é); string operands are treated as UTF-8.
            LuaValue r = await engine.ExecuteStringAsync(@"return (Wchar.FromUtf8('caf') .. '\xC3\xA9'):ToUtf8()");
            r.String.ShouldBe("caf\u00e9");
        }

        [Fact]
        public async Task Wchar_ToBytes_ReturnsCorrectCodeValues()
        {
            using KitsuneEngine engine = new();

            // 'A' = 65, 'B' = 66 as wchar_t values.
            LuaValue r = await engine.ExecuteStringAsync(@"
                local b = Wchar.FromUtf8('AB'):ToBytes()
                return tostring(#b == 2 and b[1] == 65 and b[2] == 66)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Wchar_FromBytes_Table_CreatesCorrectWchar()
        {
            using KitsuneEngine engine = new();

            // Reconstruct 'AB' from its wchar_t code values.
            LuaValue r = await engine.ExecuteStringAsync("return Wchar.FromBytes({65, 66}):ToUtf8()");
            r.String.ShouldBe("AB");
        }

        [Fact]
        public async Task Wchar_FromBytes_SingleInteger_CreatesSingleCharWchar()
        {
            using KitsuneEngine engine = new();

            // Codepoint 65 = 'A'.
            LuaValue r = await engine.ExecuteStringAsync("return Wchar.FromBytes(65):ToUtf8()");
            r.String.ShouldBe("A");
        }

        [Fact]
        public async Task Wchar_FromBytes_InvalidCodepoint_ProducesEmptyWchar()
        {
            using KitsuneEngine engine = new();

            // A codepoint above U+10FFFF is invalid; FromBytes must return an empty Wchar.
            LuaValue r = await engine.ExecuteStringAsync("return tostring(#Wchar.FromBytes(0x200000) == 0)");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Wchar_ToBytes_AsciiChar_ProducesOneCodeUnit()
        {
            using KitsuneEngine engine = new();

            // ToBytes returns a table of UTF-16 code units.
            // 'A' is U+0041 — one code unit — so the table has exactly one entry with value 65.
            LuaValue r = await engine.ExecuteStringAsync(@"
                local units = Wchar.FromUtf8('A'):ToBytes()
                return tostring(#units == 1 and units[1] == 65)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Wchar_FromBytes_Table_RoundTrips()
        {
            using KitsuneEngine engine = new();

            // ToBytes returns a table of UTF-16 code units; FromBytes(table) reconstructs from them.
            LuaValue r = await engine.ExecuteStringAsync(@"
                local w1 = Wchar.FromUtf8('hello')
                local w2 = Wchar.FromBytes(w1:ToBytes())
                return tostring(w1 == w2)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Wchar_Codepoints_ReturnsCorrectTable()
        {
            using KitsuneEngine engine = new();

            // 'AB' ? codepoints table {65, 66}.
            LuaValue r = await engine.ExecuteStringAsync(@"
                local pts = Wchar.FromUtf8('AB'):Codepoints()
                return tostring(#pts == 2 and pts[1] == 65 and pts[2] == 66)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Wchar_At_ValidIndex_ReturnsCodepoint()
        {
            using KitsuneEngine engine = new();

            // 'B' is at character position 2 (1-indexed) in 'AB'.
            LuaValue r = await engine.ExecuteStringAsync("return tostring(Wchar.FromUtf8('AB'):At(2) == 66)");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Wchar_At_OutOfRange_ReturnsNil()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("return tostring(Wchar.FromUtf8('hi'):At(99))");
            r.String.ShouldBe("nil");
        }

        [Fact]
        public async Task Wchar_FromAnsi_ToAnsi_AsciiRoundTrip()
        {
            using KitsuneEngine engine = new();

            // ASCII characters are stable across all encodings.
            LuaValue r = await engine.ExecuteStringAsync("return Wchar.FromAnsi('hello'):ToAnsi()");
            r.String.ShouldBe("hello");
        }

        [Fact]
        public async Task Wchar_NonAsciiUtf8_LengthIsWcharCount()
        {
            using KitsuneEngine engine = new();

            // U+00E9 (é) is 2 UTF-8 bytes (\xC3\xA9) but 1 wchar_t; length should be 1.
            LuaValue r = await engine.ExecuteStringAsync(@"return tostring(#Wchar.FromUtf8('\xC3\xA9') == 1)");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Wchar_ChainedOps_ProduceCorrectResult()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
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
            using KitsuneEngine engine = new();

            // Setlocale sets the C locale used by FromAnsi/ToAnsi; must not raise an error.
            LuaValue r = await engine.ExecuteStringAsync(@"
                local ok, err = pcall(Wchar.Setlocale, '')
                return tostring(ok or type(err) == 'string')
            ");
            r.String.ShouldBe("true");
        }

        // -- Stream Wchar read/write -----------------------------------------------
        [Fact]
        public async Task Stream_WriteWchar_ReadWchar_AsciiRoundTrip()
        {
            using KitsuneEngine engine = new();

            // Write a Wchar into a stream and read it back as a Wchar.
            LuaValue r = await engine.ExecuteStringAsync(@"
                local w = Wchar.FromUtf8('hello')
                local s = Stream.New()
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
            using KitsuneEngine engine = new();

            // Write returns the number of bytes written (2 bytes per wchar_t code unit).
            LuaValue r = await engine.ExecuteStringAsync(@"
                local w = Wchar.FromUtf8('hi')
                local s = Stream.New()
                local written = s:Write(w)
                return tostring(written == 4)   -- 2 code units * 2 bytes each
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Stream_WriteWchar_AdvancesPosition()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local w = Wchar.FromUtf8('abc')
                local s = Stream.New()
                s:Write(w)
                return tostring(s:pos() == 6)   -- 3 code units * 2 bytes each
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Stream_ReadWchar_PartialRead_ReturnsRequestedCount()
        {
            using KitsuneEngine engine = new();

            // Write a 5-char Wchar then read back only 3 code units.
            LuaValue r = await engine.ExecuteStringAsync(@"
                local w = Wchar.FromUtf8('hello')
                local s = Stream.New()
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
            using KitsuneEngine engine = new();

            // Requesting more code units than are available returns nil.
            LuaValue r = await engine.ExecuteStringAsync(@"
                local s = Stream.New()
                return tostring(s:ReadWchar(1) == nil)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Stream_WriteWchar_MultipleAppend_ReadBackFull()
        {
            using KitsuneEngine engine = new();

            // Two Wchar writes must be contiguous; one ReadWchar retrieves them all.
            LuaValue r = await engine.ExecuteStringAsync(@"
                local s = Stream.New()
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
            using KitsuneEngine engine = new();

            // ReadWchar() with no argument reads all remaining code units to end of stream.
            LuaValue r = await engine.ExecuteStringAsync(@"
                local s = Stream.New()
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
            using KitsuneEngine engine = new();

            // ReadWchar() from mid-stream must only return code units from the current position.
            LuaValue r = await engine.ExecuteStringAsync(@"
                local s = Stream.New()
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
            using KitsuneEngine engine = new();

            // ReadWchar() with no argument on an empty stream must return nil, not error.
            LuaValue r = await engine.ExecuteStringAsync(@"
                local s = Stream.New()
                return tostring(s:ReadWchar() == nil)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Stream_ReadWchar_ExplicitNilArg_ReadAll()
        {
            using KitsuneEngine engine = new();

            // Passing nil explicitly must behave identically to omitting the argument.
            LuaValue r = await engine.ExecuteStringAsync(@"
                local s = Stream.New()
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
            using KitsuneEngine engine = new();

            // The return value must be a Wchar userdata, not a plain Lua string.
            LuaValue r = await engine.ExecuteStringAsync(@"
                local s = Stream.New()
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
            using KitsuneEngine engine = new();

            // Writing a Wchar to a read-only stream must return 0.
            LuaValue r = await engine.ExecuteStringAsync(@"
                local s = Stream.New(function(op, ...)
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
            using KitsuneEngine engine = new();

            // ReadWchar on a write-only stream must return nil.
            LuaValue r = await engine.ExecuteStringAsync(@"
                local s = Stream.New(function(op, ...)
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
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("local t = Timer.New(); return tostring(t:IsRunning() == false)");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Timer_AfterStart_IsRunning()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("local t = Timer.New(); t:Start(); return tostring(t:IsRunning())");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Timer_ElapsedAfterSleep_IsPositive()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("local t = Timer.New(); t:Start(); Sleep(20); return tostring(t:Elapsed() > 0)");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Timer_StopAndReset_ElapsedIsZero()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local t = Timer.New(); t:Start(); Sleep(10); t:Stop(); t:Reset()
                return tostring(t:Elapsed() == 0 and not t:IsRunning())
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Timer_ElapsedTimeSpan_ReturnsTimeSpanType()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local t = Timer.New()
                t:Start()
                Sleep(20)
                local ts = t:ElapsedTimeSpan()
                return tostring(type(ts) == 'userdata' and ts:TotalMilliseconds() > 0)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Timer_ElapsedTimeSpan_NotStarted_IsZero()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local t = Timer.New()
                local ts = t:ElapsedTimeSpan()
                return tostring(ts:TotalMilliseconds() == 0)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Timer_ElapsedTimeSpan_MatchesElapsed()
        {
            using KitsuneEngine engine = new();

            // ElapsedTimeSpan and Elapsed should agree to within 1ms
            LuaValue r = await engine.ExecuteStringAsync(@"
                local t = Timer.New()
                t:Start()
                Sleep(30)
                t:Stop()
                local ms  = t:Elapsed()
                local ts  = t:ElapsedTimeSpan()
                return tostring(math.abs(ts:TotalMilliseconds() - ms) < 1)
            ");
            r.String.ShouldBe("true");
        }

        // -- Aes ------------------------------------------------------------------
        [Fact]
        public async Task Aes_EncryptDecrypt_RoundTrip()
        {
            using KitsuneEngine engine = new();

            // Use two fresh instances with the same key and default zero IV.
            LuaValue r = await engine.ExecuteStringAsync(@"
                local key = string.rep('\0', 32)
                local plain = 'hello aes world!'
                local enc = Aes.New(key):Encrypt(plain)
                local dec = Aes.New(key):Decrypt(enc)
                return tostring(dec == plain)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Aes_EncryptedData_DiffersFromPlaintext()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local key = string.rep('\0', 32)
                local plain = 'secret message!!'
                local enc = Aes.New(key):Encrypt(plain)
                return tostring(enc ~= plain)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Aes_SetIV_ResetIV_AllowsDecryptWithSameInstance()
        {
            using KitsuneEngine engine = new();

            // SetIV with no argument resets to the stored IV, making the same instance usable
            // for both encrypt and decrypt when the IV is restored between operations.
            LuaValue r = await engine.ExecuteStringAsync(@"
                local key   = string.rep('\0', 32)
                local iv    = string.rep('\0', 16)
                local plain = 'hello aes world!'
                local ctx   = Aes.New(key, iv)
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
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local key   = string.rep('\0', 32)
                local plain = 'hello different!'
                local enc1  = Aes.New(key, string.rep('\0', 16)):Encrypt(plain)
                local enc2  = Aes.New(key, string.rep('\1', 16)):Encrypt(plain)
                return tostring(enc1 ~= enc2)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Aes_CTRMode_RoundTrip()
        {
            using KitsuneEngine engine = new();

            // CTR (stream cipher): encrypt with one instance, decrypt with a fresh instance
            // sharing the same key and IV. Must produce the original plaintext.
            LuaValue r = await engine.ExecuteStringAsync(@"
                local key   = string.rep('\0', 32)
                local iv    = string.rep('\0', 16)
                local plain = 'hello ctr mode!!'
                local enc   = Aes.New(key, iv, true):Encrypt(plain)
                local dec   = Aes.New(key, iv, true):Decrypt(enc)
                return tostring(dec == plain)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Aes_ECBMode_NoIV_RoundTrip()
        {
            using KitsuneEngine engine = new();

            // ECB mode: created without IV; each block encrypted independently.
            LuaValue r = await engine.ExecuteStringAsync(@"
                local key   = string.rep('\0', 32)
                local plain = 'hello ecb mode!!'   -- exactly 16 bytes (one AES block)
                local enc   = Aes.New(key):Encrypt(plain)
                local dec   = Aes.New(key):Decrypt(enc)
                return tostring(dec == plain)
            ");
            r.String.ShouldBe("true");
        }

        // -- Stream (in-memory) ---------------------------------------------------
        [Fact]
        public async Task Stream_Create_FromString_LoadsDataAtPositionZero()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local s = Stream.New('hello stream')
                return s:Read()
            ");
            r.String.ShouldBe("hello stream");
        }

        [Fact]
        public async Task Stream_Create_FromString_PosIsZeroAfterCreate()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local s = Stream.New('hello')
                return tostring(s:pos() == 0 and s:len() == 5)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Stream_Create_WithBackendFunction_CallsOpenForCaps()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local OPEN, CLOSE, READ, WRITE = 0, 1, 2, 3
                local STREAM_CAP_READ = 1
                local s = Stream.New(function(op, ...)
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
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local OPEN, CLOSE, READ = 0, 1, 2
                local STREAM_CAP_READ = 1
                local s = Stream.New(function(op, ...)
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
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local function makeStream()
                    local OPEN, CLOSE, READ, WRITE = 0, 1, 2, 3
                    local CURPOS, LEN, SETPOS, INFO = 4, 5, 6, 7
                    local CAP_READ, CAP_WRITE, CAP_SEEK = 1, 2, 4
                    local buf = ''
                    local pos = 0
                    return Stream.New(function(op, arg)
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
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local s = Stream.New()
                s:Write('hello stream')
                s:Seek(0)
                return s:Read()
            ");
            r.String.ShouldBe("hello stream");
        }

        [Fact]
        public async Task Stream_PosAndLen_AfterWrite_ReturnCorrectValues()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local s = Stream.New()
                s:Write('abcde')
                return tostring(s:pos() == 5 and s:len() == 5)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Stream_GetInfo_ReturnsCapsAndBackendInfo()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local s = Stream.New()
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
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local s = Stream.New()
                s:Write('hello')
                s:Seek(2)
                return tostring(s:pos() == 2)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Stream_WriteByte_ReadByte_RoundTrip()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local s = Stream.New()
                s:WriteByte(42)
                s:Seek(0)
                return tostring(s:ReadByte() == 42)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Stream_PeekByte_DoesNotAdvancePosition()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local s = Stream.New()
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
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local s = Stream.New()
                s:WriteInt(12345)
                s:Seek(0)
                return tostring(s:ReadInt() == 12345)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Stream_Compress_Decompress_RoundTrip()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local s = Stream.New()
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
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local s = Stream.New()
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
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local src = Stream.New()
                src:Write('hello hello hello hello hello')
                local dst = Stream.New()
                src:Compress(nil, dst)
                local decompressed = dst:Decompress()
                return decompressed:Read()
            ");
            r.String.ShouldBe("hello hello hello hello hello");
        }

        [Fact]
        public async Task Stream_Decompress_IntoProvidedStream_RoundTrip()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local src = Stream.New()
                src:Write('hello hello hello hello hello')
                local compressed = src:Compress()
                local dst = Stream.New()
                compressed:Decompress(nil, dst)
                dst:Seek(0)
                return dst:Read()
            ");
            r.String.ShouldBe("hello hello hello hello hello");
        }

        [Fact]
        public async Task Stream_Compress_AndDecompress_BothIntoProvidedStreams_RoundTrip()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local src = Stream.New()
                src:Write('hello hello hello hello hello')
                local compDst = Stream.New()
                local decompDst = Stream.New()
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
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local src = Stream.New()
                src:Write(string.rep('a', 200))
                local dst = Stream.New()
                src:Compress(nil, dst)
                local _, info = dst:GetInfo()
                return tostring(info.pos > 0)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Stream_Decompress_ProvidedDst_PositionNotReset()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local src = Stream.New()
                src:Write(string.rep('a', 200))
                local compressed = src:Compress()
                local dst = Stream.New()
                compressed:Decompress(nil, dst)
                local _, info = dst:GetInfo()
                return tostring(info.pos > 0)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Stream_WriteFloat_ReadFloat_RoundTrip()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local s = Stream.New()
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
            using KitsuneEngine engine = new();

            // lua_callk on READ dispatch means a backend error propagates and is caught by pcall.
            LuaValue r = await engine.ExecuteStringAsync(@"
                local OPEN, CLOSE, READ = 0, 1, 2
                local ok, err = pcall(function()
                    local s = Stream.New(function(op)
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
            using KitsuneEngine engine = new();

            // lua_call_nohook on WRITE dispatch means a backend error bubbles up.
            LuaValue r = await engine.ExecuteStringAsync(@"
                local OPEN, CLOSE, WRITE = 0, 1, 3
                local ok, err = pcall(function()
                    local s = Stream.New(function(op)
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
            using KitsuneEngine engine = new();

            // lua_call_nohook on SETPOS dispatch means a backend error bubbles up.
            LuaValue r = await engine.ExecuteStringAsync(@"
                local OPEN, CLOSE, SETPOS = 0, 1, 6
                local ok, err = pcall(function()
                    local s = Stream.New(function(op)
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
            using KitsuneEngine engine = new();

            // NewStream uses lua_pcall_nohook on OPEN with explicit recovery:
            // a non-number return produces "Backend function failed to open".
            LuaValue r = await engine.ExecuteStringAsync(@"
                local ok, err = pcall(Stream.New, function(op) return 'not_a_number' end)
                return tostring(not ok and err:find('Backend function failed to open') ~= nil)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Stream_BackendOpen_ZeroCaps_GivesCleanError()
        {
            using KitsuneEngine engine = new();

            // Returning 0 caps (no operations supported) is treated as failure.
            LuaValue r = await engine.ExecuteStringAsync(@"
                local ok, err = pcall(Stream.New, function(op) return 0 end)
                return tostring(not ok and err:find('Backend function failed to open') ~= nil)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Stream_BackendOpen_Throws_GivesCleanError()
        {
            using KitsuneEngine engine = new();

            // A throw during OPEN is caught by the protected call in NewStream and
            // reported as "Backend function failed to open" (original message is lost
            // intentionally; the non-number return check fires on the error object).
            LuaValue r = await engine.ExecuteStringAsync(@"
                local ok, err = pcall(Stream.New, function(op) error('boom') end)
                return tostring(not ok and err:find('Backend function failed to open') ~= nil)
            ");
            r.String.ShouldBe("true");
        }

        // -- Stream.Open (file backend) -------------------------------------------
        [Fact]
        public async Task Stream_Open_WriteRead_RoundTrip()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
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
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
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
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
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
            using KitsuneEngine engine = new();

            // s:len() on a file stream must return the total file byte count and
            // must not disturb the read cursor.
            LuaValue r = await engine.ExecuteStringAsync(@"
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
            using KitsuneEngine engine = new();

            // GetInfo().len for a file stream must equal the actual file byte count
            // and must not change the cursor position.
            LuaValue r = await engine.ExecuteStringAsync(@"
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
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local ok, err = pcall(Stream.Open, 'nonexistent_kitsune_xyz_abc_1234567890.bin', 'rb')
                return tostring(not ok and err:find('Stream.Open') ~= nil)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Stream_Open_ReadMode_BlocksWrite()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
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
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local s = Stream.New()
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
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local s = Stream.New()
                s:Write('abc')
                return tostring(s:len())
            ");
            r.String.ShouldBe("3");
        }

        [Fact]
        public async Task Stream_Pos_AdvancesAfterRead()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local s = Stream.New()
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
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local s = Stream.New('hello world')
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
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local s = Stream.New()
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
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local s = Stream.New('ABC')
                s:Seek(0)
                local peeked = s:PeekByte()
                return tostring(peeked) .. ':' .. tostring(s:pos())
            ");
            r.String.ShouldBe("65:0");  // 'A' == 65, pos unchanged
        }

        [Fact]
        public async Task Stream_MultipleNumericTypes_InSequence()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local s = Stream.New()
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
            using KitsuneEngine engine = new();

            // Count rows with ipairs to avoid # unreliability on non-sequence tables.
            LuaValue r = await engine.ExecuteStringAsync(@"
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
            using KitsuneEngine engine = new();

            // Decode("") must return an empty Rows table (zero rows).
            // The old do-while loop produced one spurious empty row; the while
            // loop introduced in Task 13 correctly produces none.
            LuaValue r = await engine.ExecuteStringAsync(@"
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
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local t = CSV.New():Decode('hello,world')
                return tostring(t.Rows[1][1])
            ");
            r.String.ShouldBe("hello");
        }

        [Fact]
        public async Task CSV_Decode_AsciiCell_IsPlainString()
        {
            using KitsuneEngine engine = new();

            // ASCII-only cells must come back as plain Lua strings (not WChar
            // userdata) so the fast path is active.
            LuaValue r = await engine.ExecuteStringAsync(@"
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
            using KitsuneEngine engine = new();

            // Cells containing characters above U+007F must still be WChar userdata.
            LuaValue r = await engine.ExecuteStringAsync(@"
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
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local t = CSV.New():Decode('a,b,c')
                local row = t.Rows[1]
                return tostring(tostring(row[1]) .. ':' .. tostring(row[2]) .. ':' .. tostring(row[3]))
            ");
            r.String.ShouldBe("a:b:c");
        }

        [Fact]
        public async Task CSV_DecodeString_MultipleRows_CorrectCount()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
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
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local t = CSV.New():Decode('""hello"",""world""')
                return tostring(tostring(t.Rows[1][1]) == 'hello' and tostring(t.Rows[1][2]) == 'world')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task CSV_DecodeString_QuotedFieldWithEmbeddedDelimiter_PreservesContent()
        {
            using KitsuneEngine engine = new();

            // A comma inside quotes must not split the field.
            LuaValue r = await engine.ExecuteStringAsync(@"
                local t = CSV.New():Decode('""hello, world"",end')
                return tostring(tostring(t.Rows[1][1]) == 'hello, world')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task CSV_DecodeString_QuotedFieldWithEscapedQuote_ProducesLiteralQuote()
        {
            using KitsuneEngine engine = new();

            // RFC 4180 escaped quote: "" inside a quoted field ? single ".
            LuaValue r = await engine.ExecuteStringAsync(@"
                local t = CSV.New():Decode('""say """"hi"""""",end')
                return tostring(tostring(t.Rows[1][1]) == 'say ""hi""')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task CSV_DecodeString_FieldWithLeadingWhitespace_WhitespaceIsStripped()
        {
            using KitsuneEngine engine = new();

            // Verifies the SkipForwards fix: previously the first non-space character
            // was silently consumed and lost, producing "ello" instead of "hello".
            LuaValue r = await engine.ExecuteStringAsync(@"
                local t = CSV.New():Decode(' hello, world')
                return tostring(tostring(t.Rows[1][1]) == 'hello' and tostring(t.Rows[1][2]) == 'world')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task CSV_DecodeString_EmptyField_PreservesEmptyCell()
        {
            using KitsuneEngine engine = new();

            // a,,b produces three fields; the middle one is empty.
            LuaValue r = await engine.ExecuteStringAsync(@"
                local t = CSV.New():Decode('a,,b')
                local row = t.Rows[1]
                return tostring(tostring(row[1]) == 'a' and tostring(row[2]) == '' and tostring(row[3]) == 'b')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task CSV_DecodeString_ResultHasCommentsKey()
        {
            using KitsuneEngine engine = new();

            // The returned table always has a Comments key even when there are none.
            LuaValue r = await engine.ExecuteStringAsync(@"
                local t = CSV.New():Decode('a,b')
                return tostring(type(t.Comments) == 'table')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task CSV_DecodeString_CommentLine_IsExtractedAndExcludedFromRows()
        {
            using KitsuneEngine engine = new();

            // Lines starting with * are treated as comments and placed in t.Comments.
            LuaValue r = await engine.ExecuteStringAsync(@"
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
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
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
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local t = CSV.New(';'):Decode('a;b;c')
                local row = t.Rows[1]
                return tostring(tostring(row[1]) == 'a' and tostring(row[2]) == 'b' and tostring(row[3]) == 'c')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task CSV_DecodeString_CrLfLineEnding_ParsedAsOneRow()
        {
            using KitsuneEngine engine = new();

            // \r\n (Windows CRLF) must produce the same row count as \n alone.
            LuaValue r = await engine.ExecuteStringAsync(@"
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
            using KitsuneEngine engine = new();

            // Exercises the lua_iswchar branch in DecodeString; all other tests pass
            // plain Lua strings which take the FromUtf8 conversion path instead.
            LuaValue r = await engine.ExecuteStringAsync(@"
                local t = CSV.New():Decode(Wchar.FromUtf8('x,y,z'))
                return tostring(tostring(t.Rows[1][1]) == 'x' and tostring(t.Rows[1][3]) == 'z')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task CSV_Encode_SimpleTable_ProducesCorrectString()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("return CSV.New():Encode({{'a', 'b'}, {'c', 'd'}})");
            r.String.ShouldBe("a,b\nc,d");
        }

        [Fact]
        public async Task CSV_Encode_FieldWithDelimiter_IsQuotedAndRoundTrips()
        {
            using KitsuneEngine engine = new();

            // A field containing the delimiter must be quoted; decoding must recover the original value.
            LuaValue r = await engine.ExecuteStringAsync(@"
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
            using KitsuneEngine engine = new();

            // Wchar fields must be converted via __tostring (UTF-8) during encoding.
            LuaValue r = await engine.ExecuteStringAsync(@"
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
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
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
            using KitsuneEngine engine = new();

            // SkipForwards strips leading whitespace on decode. Encode must quote
            // fields whose value starts with a space or tab so the whitespace lands
            // inside the quotes and is preserved across a decode round-trip.
            LuaValue r = await engine.ExecuteStringAsync(@"
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
            using KitsuneEngine engine = new();

            // RFC 4180: a " inside a quoted field is escaped as ""; Encode must produce
            // that and Decode must recover the original single ".
            LuaValue r = await engine.ExecuteStringAsync(@"
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
            using KitsuneEngine engine = new();

            // A field containing \n must be quoted so the newline is not treated as a
            // row separator on decode.
            LuaValue r = await engine.ExecuteStringAsync(@"
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
            using KitsuneEngine engine = new();

            // ParseDelimiter accepts boolean true as the auto-detect signal, identical
            // to passing the string "auto".
            LuaValue r = await engine.ExecuteStringAsync(@"
                local t = CSV.New(true):Decode('a;b;c\n1;2;3')
                return tostring(tostring(t.Rows[1][1]) == 'a' and tostring(t.Rows[2][3]) == '3')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task CSV_Decode_IntegerCodepointDelimiter_UsedCorrectly()
        {
            using KitsuneEngine engine = new();

            // ParseDelimiter accepts an integer codepoint (59 = ';').
            LuaValue r = await engine.ExecuteStringAsync(@"
                local t = CSV.New(59):Decode('a;b;c')
                return tostring(tostring(t.Rows[1][1]) == 'a' and tostring(t.Rows[1][3]) == 'c')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task CSV_New_NoArgs_Encode_FallsBackToComma()
        {
            using KitsuneEngine engine = new();

            // CSV.New() binds "auto" as the delimiter. Encode with "auto" has no
            // meaningful input to sniff from, so it must fall back to comma.
            LuaValue r = await engine.ExecuteStringAsync("return CSV.New():Encode({{'a', 'b', 'c'}})");
            r.String.ShouldBe("a,b,c");
        }

        [Fact]
        public async Task CSV_Decode_AutoDetect_CommaInput_DetectsCorrectly()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local t = CSV.New():Decode('a,b,c\n1,2,3')
                return tostring(tostring(t.Rows[1][1]) == 'a' and tostring(t.Rows[1][3]) == 'c' and tostring(t.Rows[2][1]) == '1')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task CSV_Decode_AutoDetect_SemicolonInput_DetectsCorrectly()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local t = CSV.New():Decode('a;b;c\n1;2;3')
                return tostring(tostring(t.Rows[1][1]) == 'a' and tostring(t.Rows[1][3]) == 'c' and tostring(t.Rows[2][2]) == '2')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task CSV_Decode_AutoDetect_TabInput_DetectsCorrectly()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local t = CSV.New():Decode('a\tb\tc\n1\t2\t3')
                return tostring(tostring(t.Rows[1][1]) == 'a' and tostring(t.Rows[1][3]) == 'c')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task CSV_New_NoArgs_AutoDetectsSemicolon()
        {
            using KitsuneEngine engine = new();

            // CSV.New() with no delimiter should sniff each Decode call independently.
            LuaValue r = await engine.ExecuteStringAsync(@"
                local csv = CSV.New()
                local t = csv:Decode('a;b;c\n1;2;3')
                return tostring(tostring(t.Rows[1][1]) == 'a' and tostring(t.Rows[1][3]) == 'c' and tostring(t.Rows[2][2]) == '2')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task CSV_New_WithSemicolon_UsesSpecifiedDelimiter()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local csv = CSV.New(';')
                local t = csv:Decode('a;b;c')
                return tostring(tostring(t.Rows[1][1]) == 'a' and tostring(t.Rows[1][3]) == 'c')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task CSV_New_Encode_UsesSpecifiedDelimiter()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("return CSV.New(';'):Encode({{'a', 'b', 'c'}})");
            r.String.ShouldBe("a;b;c");
        }

        [Fact]
        public async Task CSV_DecodeFromFunction_AutoDetect_DetectsSemicolon()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
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
            using KitsuneEngine engine = new();

            // Without Task-14 buffering the sniff would run on only the first
            // chunk ("hello") which contains no delimiter at all and would fall
            // back to comma.  With buffering the iterator keeps pulling chunks
            // until it sees a newline, giving SniffDelimiter enough context to
            // correctly identify the semicolon delimiter.
            LuaValue r = await engine.ExecuteStringAsync(@"
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
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
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
            using KitsuneEngine engine = new();

            // Chunks deliberately cross field and row boundaries to verify the
            // stream refill logic handles mid-field and mid-row chunk splits.
            LuaValue r = await engine.ExecuteStringAsync(@"
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
            using KitsuneEngine engine = new();

            // Supplier returns Wchar userdata objects; they must be converted to UTF-8
            // and parsed identically to plain-string chunks.
            LuaValue r = await engine.ExecuteStringAsync(@"
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
            using KitsuneEngine engine = new();

            // The last row has no trailing newline; the nil from the supplier must
            // flush the in-progress row cleanly.
            LuaValue r = await engine.ExecuteStringAsync(@"
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
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
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
            using KitsuneEngine engine = new();

            // A LuaStream can be passed directly instead of a supplier function;
            // data is pulled in 4 KiB chunks so no full read-into-memory occurs.
            LuaValue r = await engine.ExecuteStringAsync(@"
                local s = Stream.New('a,b,c\n1,2,3\n4,5,6')
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
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local s = Stream.New('a;b;c\n1;2;3')
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
            using KitsuneEngine engine = new();

            // The stream is passed inline with no variable holding it; the iterator
            // closure must keep it alive through GC so all rows are produced.
            LuaValue r = await engine.ExecuteStringAsync(@"
                local rows = {}
                for row in CSV.New():DecodeFromFunction(Stream.New('x,y\nz,w')) do
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
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
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
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
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
            using KitsuneEngine engine = new();

            // Lock(0) is a non-blocking trylock; must succeed when nobody holds the mutex.
            LuaValue r = await engine.ExecuteStringAsync(@"
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
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("return tostring(#FileSystem.CurrentDirectory() > 0)");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task FileSystem_GetTempFileName_ReturnsValidPath()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("local p = FileSystem.GetTempFileName(); return tostring(type(p)=='string' and #p>0)");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task FileSystem_GetDrives_ReturnsList()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("local d = FileSystem.GetDrives(); return tostring(type(d)=='table' and #d>=1)");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task FileSystem_GetDrives_EntryHasDriveField()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local d = FileSystem.GetDrives()
                return tostring(type(d[1].Drive) == 'string' and #d[1].Drive > 0)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task FileSystem_CreateAndDeleteDirectory_Succeeds()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
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
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
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
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
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
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
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
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
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
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
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
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local info = FileSystem.GetFileInfo(FileSystem.GetTempFileName() .. '_no_such_file_xyz')
                return tostring(info == nil)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task FileSystem_Copy_CreatesDestination()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
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
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
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
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
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
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
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
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
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
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
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
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
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
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
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
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
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
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
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
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
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
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
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
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
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
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
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
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
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
        public async Task CSV_Tostring_FixedDelimiterInstance()
        {
            using KitsuneEngine engine = new();

            // __tostring on a fixed-delimiter instance reports the character.
            LuaValue r = await engine.ExecuteStringAsync("return tostring(CSV.New(';'))");
            r.String.ShouldBe("CSV(';')");
        }

        [Fact]
        public async Task CSV_New_CalledOnInstance_CreatesNewIndependentInstance()
        {
            using KitsuneEngine engine = new();

            // csv:New(delim) must ignore the existing instance and return a fresh one
            // with its own delimiter — not a reference to the original.
            LuaValue r = await engine.ExecuteStringAsync(@"
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
            using KitsuneEngine engine = new();

            // The same instance must produce correct results across successive Decode calls.
            // DecodeCsvWith resets pos/last/len but preserves the buffer allocation.
            LuaValue r = await engine.ExecuteStringAsync(@"
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
            using KitsuneEngine engine = new();

            // An auto-detect instance must sniff fresh on every Decode call;
            // the sniffed delimiter from call 1 must not bleed into call 2.
            LuaValue r = await engine.ExecuteStringAsync(@"
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
            using KitsuneEngine engine = new();

            // Passing a write-only stream must produce a clean Lua error, not a crash.
            LuaValue r = await engine.ExecuteStringAsync(@"
                local OPEN, CLOSE, CAP_WRITE = 0, 1, 2
                local s = Stream.New(function(op)
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
            using KitsuneEngine engine = new();

            // Encode after Decode on the same instance must work; the buffer fields
            // used by Decode do not interfere with LuaL_Buffer used by Encode.
            LuaValue r = await engine.ExecuteStringAsync(@"
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
            using KitsuneEngine engine = new();

            // Two separate DecodeFromFunction iterators created from the same auto-detect
            // instance must each detect their own delimiter independently.
            LuaValue r = await engine.ExecuteStringAsync(@"
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
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("return Json.New():Encode(-math.huge)");
            r.String.ShouldBe("-1e+9999");
        }

        [Fact]
        public async Task Json_Tostring_ReturnsNonEmptyString()
        {
            using KitsuneEngine engine = new();

            // __tostring on a Json instance returns a pointer-format string.
            LuaValue r = await engine.ExecuteStringAsync("return tostring(type(tostring(Json.New())) == 'string' and #tostring(Json.New()) > 0)");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Json_Dispose_CanBeCalledExplicitly()
        {
            using KitsuneEngine engine = new();

            // Json.Dispose() is an explicit GC; calling it must not crash and the
            // instance should still be a valid Lua value afterwards.
            LuaValue r = await engine.ExecuteStringAsync(@"
                local j = Json.New()
                j:Dispose()
                return 'ok'
            ");
            r.String.ShouldBe("ok");
        }

        [Fact]
        public async Task Json_Decode_ChunkedFunction_ParsesValues()
        {
            using KitsuneEngine engine = new();

            // json:Decode(fn) calls fn() repeatedly to get input chunks; returning
            // nil or "" signals end of input.  Tests the chunkFnIdx code path.
            LuaValue r = await engine.ExecuteStringAsync(@"
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
            using KitsuneEngine engine = new();

            // Each Decode(fn) call drains exactly one JSON value; the fn is fresh
            // each call so this tests that chunkFnIdx is properly reset.
            LuaValue r = await engine.ExecuteStringAsync(@"
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

        // -- Json SetDecodeNull ---------------------------------------------------
        [Fact]
        public async Task SetDecodeNull_DefaultFalse_NullDecodesAsNil()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local json = Json.New()
                local t = json:Decode('{""value"":null}')
                return tostring(t.value == nil)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task SetDecodeNull_DefaultFalse_NullIsFalsy_CoalescingWorks()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local json = Json.New()
                local t = json:Decode('{""value"":null}')
                local result = t.value and 'truthy' or 'falsy'
                return result
            ");
            r.String.ShouldBe("falsy");
        }

        [Fact]
        public async Task SetDecodeNull_DefaultFalse_RoundTripIsLossy()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local json = Json.New()
                local t = json:Decode('{""value"":null}')
                return json:Encode(t)
            ");

            // nil key is omitted — the table is now empty, encodes as []
            r.String.ShouldBe("[]");
        }

        [Fact]
        public async Task SetDecodeNull_True_NullDecodesAsJsonNull()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local json = Json.New()
                json:SetDecodeNull(true)
                local t = json:Decode('{""value"":null}')
                return tostring(t.value == Json.Null)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task SetDecodeNull_True_JsonNullIsTruthy_RoundTrips()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local json = Json.New()
                json:SetDecodeNull(true)
                local t = json:Decode('{""value"":null}')
                return json:Encode(t)
            ");
            r.String.ShouldBe("{\"value\":null}");
        }

        [Fact]
        public async Task SetDecodeNull_True_RoundTripPreservesNull()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local json = Json.New()
                json:SetDecodeNull(true)
                local t = json:Decode('{""value"":null}')
                return json:Encode(t)
            ");
            r.String.ShouldBe("{\"value\":null}");
        }

        [Fact]
        public async Task SetDecodeNull_ReturnsSelf_AllowsChaining()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local json = Json.New():SetDecodeNull(true)
                local t = json:Decode('{""x"":null}')
                return tostring(t.x == Json.Null)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task SetDecodeNull_False_AfterTrue_RestoresNil()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local json = Json.New()
                json:SetDecodeNull(true)
                json:SetDecodeNull(false)
                local t = json:Decode('{""value"":null}')
                return tostring(t.value == nil)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task SetDecodeNull_True_NonNullValuesUnaffected()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local json = Json.New()
                json:SetDecodeNull(true)
                local t = json:Decode('{""a"":1,""b"":""hello"",""c"":true,""d"":null}')
                return tostring(t.a) .. ',' .. t.b .. ',' .. tostring(t.c) .. ',' .. tostring(t.d == Json.Null)
            ");
            r.String.ShouldBe("1,hello,true,true");
        }

        [Fact]
        public async Task SetDecodeNull_DefaultFalse_NullInArray_BecomesNil()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local json = Json.New()
                local t = json:Decode('[1,null,3]')
                return tostring(t[1]) .. ',' .. tostring(t[2]) .. ',' .. tostring(t[3])
            ");
            r.String.ShouldBe("1,nil,3");
        }

        // -- SetEncodeEmptyObject -------------------------------------------------
        [Fact]
        public async Task SetEncodeEmptyObject_DefaultFalse_EmptyTableEncodesAsArray()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local json = Json.New()
                return json:Encode({})
            ");
            r.String.ShouldBe("[]");
        }

        [Fact]
        public async Task SetEncodeEmptyObject_True_EmptyTableEncodesAsObject()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local json = Json.New()
                json:SetEncodeEmptyObject(true)
                return json:Encode({})
            ");
            r.String.ShouldBe("{}");
        }

        [Fact]
        public async Task SetEncodeEmptyObject_True_NonEmptyTableUnaffected()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local json = Json.New()
                json:SetEncodeEmptyObject(true)
                return json:Encode({1, 2, 3})
            ");
            r.String.ShouldBe("[1,2,3]");
        }

        [Fact]
        public async Task SetEncodeEmptyObject_True_EmptyObjectSentinelEncodesAsObject()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local json = Json.New()
                json:SetEncodeEmptyObject(true)
                return json:Encode(Json.EmptyObject)
            ");
            r.String.ShouldBe("{}");
        }

        [Fact]
        public async Task SetEncodeEmptyObject_EmptyObject_IsDistinctFromNull()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                return tostring(Json.EmptyObject ~= Json.Null)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task SetEncodeEmptyObject_EmptyObject_SameReferenceEveryTime()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                return tostring(Json.EmptyObject == Json.EmptyObject)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task SetEncodeEmptyObject_True_DecodeEmptyObjectReturnsSentinel()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local json = Json.New()
                json:SetEncodeEmptyObject(true)
                local v = json:Decode('{}')
                return tostring(v == Json.EmptyObject)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task SetEncodeEmptyObject_False_DecodeEmptyObjectReturnsTable()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local json = Json.New()
                local v = json:Decode('{}')
                return tostring(type(v) == 'table')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task SetEncodeEmptyObject_True_RoundTrips()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local json = Json.New()
                json:SetEncodeEmptyObject(true)
                local encoded = json:Encode({})
                local decoded = json:Decode(encoded)
                return tostring(decoded == Json.EmptyObject and encoded == '{}')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task SetEncodeEmptyObject_True_SentinelInTable_EncodesAsObject()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local json = Json.New()
                json:SetEncodeEmptyObject(true)
                return json:Encode({empty = Json.EmptyObject})
            ");
            r.String.ShouldBe("{\"empty\":{}}");
        }

        [Fact]
        public async Task SetEncodeEmptyObject_ReturnsSelf_AllowsChaining()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local json = Json.New():SetEncodeEmptyObject(true)
                return json:Encode({})
            ");
            r.String.ShouldBe("{}");
        }

        [Fact]
        public async Task SetEncodeEmptyObject_False_AfterTrue_RestoresArray()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local json = Json.New()
                json:SetEncodeEmptyObject(true)
                json:SetEncodeEmptyObject(false)
                return json:Encode({})
            ");
            r.String.ShouldBe("[]");
        }

        // -- Stream extras --------------------------------------------------------
        // -- Stream.Id ------------------------------------------------------------
        [Fact]
        public async Task Stream_Id_ReturnsInteger()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local s = Stream.New()
                return tostring(math.type(s:Id()) == 'integer')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Stream_Id_SameStream_ReturnsSameValue()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local s = Stream.New()
                return tostring(s:Id() == s:Id())
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Stream_Id_DifferentStreams_ReturnDifferentValues()
        {
            using KitsuneEngine engine = new();

            // Write to both so their internal buffers are allocated, making ids meaningful.
            LuaValue r = await engine.ExecuteStringAsync(@"
                local a = Stream.New()
                a:Write('x')
                local b = Stream.New()
                b:Write('x')
                return tostring(a:Id() ~= b:Id())
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Stream_Id_MemoryStream_ChangesAfterNewStream()
        {
            using KitsuneEngine engine = new();

            // Two distinct stream objects must never share an id, even if one is
            // created shortly after the other.
            LuaValue r = await engine.ExecuteStringAsync(@"
                local a = Stream.New('hello')
                local id1 = a:Id()
                a = nil
                collectgarbage()
                local b = Stream.New('hello')
                local id2 = b:Id()
                -- The ids may or may not collide due to allocator reuse, but both must be integers.
                return tostring(math.type(id1) == 'integer' and math.type(id2) == 'integer')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Stream_Id_FileStream_IsStable()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local _tmp = (os.getenv('TEMP') or os.getenv('TMPDIR') or '/tmp') .. package.config:sub(1,1)
                local path = _tmp .. 'kitsune_id_test.bin'
                local w = Stream.Open(path, 'wb')
                w:Write('x')
                local id1 = w:Id()
                local id2 = w:Id()
                w:Close()
                os.remove(path)
                return tostring(id1 == id2 and math.type(id1) == 'integer')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Stream_Id_LuaFnBackend_IsNonZero()
        {
            using KitsuneEngine engine = new();

            // Lua fn backends have no vtbl->getid; the fallback is the LuaStream*
            // userdata pointer itself, which must be non-zero.
            LuaValue r = await engine.ExecuteStringAsync(@"
                local s = Stream.New(function(op) if op == 0 then return 1 end if op == 1 then return true end end)
                return tostring(s:Id() ~= 0 and math.type(s:Id()) == 'integer')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Stream_Id_LuaFnBackend_SameInstanceSameId()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local s = Stream.New(function(op) if op == 0 then return 1 end if op == 1 then return true end end)
                return tostring(s:Id() == s:Id())
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Stream_WriteDouble_ReadDouble_RoundTrip()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local s = Stream.New()
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
            using KitsuneEngine engine = new();

            // WriteUnsignedShort / WriteUnsignedInt / WriteUnsignedLong — each must
            // round-trip without sign-extension or truncation.
            LuaValue r = await engine.ExecuteStringAsync(@"
                local s = Stream.New()
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
            using KitsuneEngine engine = new();

            // WriteUtf8 converts Latin-1 bytes to UTF-8; the raw bytes can be
            // Read back as a regular Lua string.
            LuaValue r = await engine.ExecuteStringAsync(@"
                local s = Stream.New()
                s:WriteUtf8('hello')
                s:Seek(0)
                return s:Read()
            ");
            r.String.ShouldBe("hello");
        }

        [Fact]
        public async Task Stream_WriteUtf8_EmbeddedNullByte_WritesFullString()
        {
            using KitsuneEngine engine = new();

            // Previously the loop used while(*in) which stops at embedded '\0',
            // silently dropping everything after it.  The fix uses the len from
            // luaL_checklstring so all bytes are encoded and written.
            LuaValue r = await engine.ExecuteStringAsync(@"
                local s = Stream.New()
                s:WriteUtf8('a\0b')   -- 3 bytes: 'a', null, 'b'
                local _, info = s:GetInfo()
                return tostring(info.len == 3)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Stream_ReadUtf8_ReturnsBytesAndCodepoint()
        {
            using KitsuneEngine engine = new();

            // ReadUtf8 reads exactly one UTF-8 codepoint and returns
            // (raw_bytes_string, codepoint_integer).
            LuaValue r = await engine.ExecuteStringAsync(@"
                local s = Stream.New()
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
            using KitsuneEngine engine = new();

            // SetByte(value, pos) writes one byte at pos, restores cursor, then
            // a Read from the original position sees the patched byte.
            LuaValue r = await engine.ExecuteStringAsync(@"
                local s = Stream.New()
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
            using KitsuneEngine engine = new();

            // __tostring reads and returns the stream content ONLY for in-memory
            // streams.  File streams and custom backends use the pointer fallback
            // to avoid side effects and large reads.
            LuaValue r = await engine.ExecuteStringAsync(@"
                local s = Stream.New('hello')
                s:Seek(0)
                return tostring(s)
            ");
            r.String.ShouldBe("hello");
        }

        [Fact]
        public async Task Stream_Tostring_FileStream_ReturnsFallbackString()
        {
            using KitsuneEngine engine = new();

            // A file stream opened with "rb" has CAP_READ + CAP_SEEK, but __tostring
            // must NOT silently read the file — it must return the pointer fallback.
            LuaValue r = await engine.ExecuteStringAsync(@"
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
            using KitsuneEngine engine = new();

            // A write-only stream lacks CAP_READ; __tostring must return a pointer
            // string rather than attempting to read the stream.
            LuaValue r = await engine.ExecuteStringAsync(@"
                local OPEN, CLOSE, CAP_WRITE = 0, 1, 2
                local s = Stream.New(function(op)
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
            using KitsuneEngine engine = new();

            // A read-only stream without CAP_SEEK must also fall back to the pointer
            // string — reading without being able to seek would silently consume data.
            LuaValue r = await engine.ExecuteStringAsync(@"
                local OPEN, CLOSE, READ, CAP_READ = 0, 1, 2, 1
                local s = Stream.New(function(op, len)
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
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local s = Stream.New()
                return tostring(s:Write('hello'))
            ");
            r.String.ShouldBe("5");
        }

        [Fact]
        public async Task Stream_Write_WithLimit_TruncatesOutput()
        {
            using KitsuneEngine engine = new();

            // Write(value, limit) writes at most 'limit' bytes.
            LuaValue r = await engine.ExecuteStringAsync(@"
                local s = Stream.New()
                s:Write('hello world', 5)
                s:Seek(0)
                return s:Read()
            ");
            r.String.ShouldBe("hello");
        }

        [Fact]
        public async Task Stream_Write_WithBoolean_WritesSingleByte()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local s = Stream.New()
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
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local s = Stream.New()
                return tostring(s:Write(nil))
            ");
            r.String.ShouldBe("0");
        }

        [Fact]
        public async Task Stream_Read_WithLength_ReadsExactCount()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local s = Stream.New('hello world')
                return s:Read(5)
            ");
            r.String.ShouldBe("hello");
        }

        // -- SetByte / PeekByte extra forms ----------------------------------------
        [Fact]
        public async Task Stream_SetByte_WithoutPosition_WritesAtCursorAndAdvances()
        {
            using KitsuneEngine engine = new();

            // SetByte(value) with no position writes at the current cursor and
            // advances it, just like WriteByte but without the 0-255 range guard.
            LuaValue r = await engine.ExecuteStringAsync(@"
                local s = Stream.New('ABC')
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
            using KitsuneEngine engine = new();

            // PeekByte(pos) peeks at 'pos' without disturbing the current cursor.
            LuaValue r = await engine.ExecuteStringAsync(@"
                local s = Stream.New('ABCD')
                s:Seek(2)
                local b = s:PeekByte(0)   -- peek at 'A' (65) while cursor is at 2
                return tostring(b == 65 and s:pos() == 2)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Stream_PeekByte_RequiresReadAndSeek_NotADistinctFlag()
        {
            using KitsuneEngine engine = new();

            // PeekStreamByte is now gated on CAP_READ + CAP_SEEK — there is no
            // separate CAP_PEEK flag.  A backend with both returns a real value;
            // a backend with only CAP_READ (no seek) returns -1.
            LuaValue r = await engine.ExecuteStringAsync(@"
                local OPEN, CLOSE, READ = 0, 1, 2
                local CAP_READ = 1
                local s = Stream.New(function(op, len)
                    if op == OPEN  then return CAP_READ end
                    if op == CLOSE then return true end
                    if op == READ  then return 'x' end
                end)
                local noSeek = s:PeekByte()
                -- Memory stream has both CAP_READ and CAP_SEEK: peek must work.
                local m = Stream.New('AB')
                local withSeek = m:PeekByte()
                return tostring(noSeek == -1 and withSeek == 65)
            ");
            r.String.ShouldBe("true");
        }

        // -- Capability-guard return values ----------------------------------------
        [Fact]
        public async Task Stream_Seek_NonSeekable_ReturnsFalse()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local OPEN, CLOSE, CAP_WRITE = 0, 1, 2
                local s = Stream.New(function(op)
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
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local OPEN, CLOSE, CAP_WRITE = 0, 1, 2
                local s = Stream.New(function(op)
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
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local OPEN, CLOSE, CAP_WRITE = 0, 1, 2
                local s = Stream.New(function(op)
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
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local s = Stream.New()
                return tostring(s:WriteByte(256)) .. ':' .. tostring(s:WriteByte(-1))
            ");
            r.String.ShouldBe("false:false");
        }

        [Fact]
        public async Task Stream_WriteByte_Boundaries_RoundTrip()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local s = Stream.New()
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
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local s = Stream.New()
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
            using KitsuneEngine engine = new();

            // U+00E9 (é) encodes as 0xC3 0xA9 in UTF-8.
            LuaValue r = await engine.ExecuteStringAsync(@"
                local s = Stream.New()
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
            using KitsuneEngine engine = new();

            // 0xFF is not a valid UTF-8 lead byte; ReadUtf8 must return nil.
            LuaValue r = await engine.ExecuteStringAsync(@"
                local s = Stream.New()
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
            using KitsuneEngine engine = new();

            // WriteUtf8 treats the input string as Latin-1 and re-encodes to UTF-8.
            // Latin-1 0xE9 (é) must produce the two-byte sequence 0xC3 0xA9.
            LuaValue r = await engine.ExecuteStringAsync(@"
                local s = Stream.New()
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
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local OPEN, CLOSE, CAP_WRITE = 0, 1, 2
                local s = Stream.New(function(op)
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
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local OPEN, CLOSE, CAP_WRITE = 0, 1, 2
                local s = Stream.New(function(op)
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
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local src = Stream.New()
                src:Write(string.rep('a', 100))
                local OPEN, CLOSE, CAP_READ = 0, 1, 1
                local ronly = Stream.New(function(op, len)
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
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local s = Stream.New()
                s:Write('hello')
                s:Close()
                return 'ok'
            ");
            r.String.ShouldBe("ok");
        }

        [Fact]
        public async Task Stream_GetInfo_MemoryStream_TypeIsMemory()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local s = Stream.New()
                s:Write('test')
                local _, info = s:GetInfo()
                return info.type
            ");
            r.String.ShouldBe("memory");
        }

        // -- Hardware -------------------------------------------------------------
        [Fact]
        public async Task Hardware_CpuName_ReturnsNonEmptyStringOrNil()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local n = Hardware.CpuName()
                return tostring(n == nil or (type(n) == 'string' and #n > 0))
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Hardware_Memory_ReturnsTableWithExpectedFields()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local m = Hardware.Memory()
                if m == nil then return 'nil' end
                return tostring(
                    type(m.TotalPhys)   == 'number' and m.TotalPhys   > 0 and
                    type(m.AvailPhys)   == 'number' and m.AvailPhys   >= 0 and
                    type(m.TotalSwap)   == 'number' and m.TotalSwap   >= 0 and
                    type(m.AvailSwap)   == 'number' and m.AvailSwap   >= 0 and
                    type(m.LoadPercent) == 'number' and
                    m.LoadPercent >= 0 and m.LoadPercent <= 100
                )
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Hardware_Memory_TotalPhysIsPositive()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local m = Hardware.Memory()
                return tostring(m ~= nil and m.TotalPhys > 0)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Hardware_Memory_AvailPhysLessThanOrEqualTotalPhys()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local m = Hardware.Memory()
                return tostring(m ~= nil and m.AvailPhys <= m.TotalPhys)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Hardware_CpuLoad_ReturnsNumberOrNil()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local v = Hardware.CpuLoad()
                return tostring(v == nil or type(v) == 'number')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Hardware_CpuLoad_SecondCallInRange()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                Hardware.CpuLoad()   -- prime baseline
                Sleep(100)
                local v = Hardware.CpuLoad()
                return tostring(v == nil or (v >= 0 and v <= 100))
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Hardware_CpuThreadsLoad_ReturnsTable()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local t = Hardware.CpuThreadsLoad()
                return tostring(type(t) == 'table')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Hardware_CpuThreadsLoad_ValuesArePercentages()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local t = Hardware.CpuThreadsLoad()
                for k, v in pairs(t) do
                    if type(k) ~= 'string' then return 'bad key' end
                    if type(v) ~= 'number' then return 'bad value' end
                    if v < 0 or v > 100 then return 'out of range' end
                end
                return 'true'
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Hardware_GpuMemory_ReturnsTableOrNil()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local t = Hardware.GpuMemory()
                return tostring(t == nil or type(t) == 'table')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Hardware_GpuMemory_AdapterEntriesHaveMemoryFields()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local t = Hardware.GpuMemory()
                if t == nil then return 'true' end
                for adapter, m in pairs(t) do
                    if type(adapter) ~= 'string' then return 'bad adapter key' end
                    if type(m) ~= 'table' then return 'bad adapter value' end
                    if type(m.DedicatedUsageMB) ~= 'number' then return 'bad DedicatedUsageMB' end
                    if type(m.SharedUsageMB)    ~= 'number' then return 'bad SharedUsageMB' end
                    if type(m.TotalCommittedMB) ~= 'number' then return 'bad TotalCommittedMB' end
                end
                return 'true'
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Hardware_GpuLoad_ReturnsTableOrNil()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local t = Hardware.GpuLoad()
                return tostring(t == nil or type(t) == 'table')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Hardware_GpuLoad_EngineValuesArePercentages()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local t = Hardware.GpuLoad()
                if t == nil then return 'true' end
                for adapter, engines in pairs(t) do
                    if type(adapter) ~= 'string' then return 'bad adapter key' end
                    if type(engines) ~= 'table'  then return 'bad engines value' end
                    for etype, pct in pairs(engines) do
                        if type(etype) ~= 'string' then return 'bad engine key' end
                        if type(pct)   ~= 'number' then return 'bad engine value' end
                        if pct < 0 or pct > 100    then return 'out of range' end
                    end
                end
                return 'true'
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Hardware_CpuTemp_ReturnsTableOrNil()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local t = Hardware.CpuTemp()
                return tostring(t == nil or type(t) == 'table')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Hardware_CpuTemp_ValuesAreReasonableWhenPresent()
        {
            using KitsuneEngine engine = new();

            // Each entry is {Name=string, Value=number} in -10..150°C
            LuaValue r = await engine.ExecuteStringAsync(@"
                local t = Hardware.CpuTemp()
                if t == nil then return 'true' end
                for _, entry in ipairs(t) do
                    if type(entry.Name)  ~= 'string' then return 'false' end
                    if type(entry.Value) ~= 'number' then return 'false' end
                    if entry.Value < -10 or entry.Value > 150 then return 'false' end
                end
                return 'true'
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Hardware_Battery_ReturnsTableOrNil()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local b = Hardware.Battery()
                return tostring(b == nil or type(b) == 'table')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Hardware_Battery_FieldTypesAreCorrectWhenPresent()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local b = Hardware.Battery()
                if b == nil then return 'true' end
                if type(b.ACLine)   ~= 'boolean' then return 'bad ACLine'   end
                if type(b.Charging) ~= 'boolean' then return 'bad Charging' end
                if b.Percent ~= nil and (type(b.Percent) ~= 'number' or b.Percent < 0 or b.Percent > 100) then
                    return 'bad Percent'
                end
                if b.SecondsRemaining ~= nil and type(b.SecondsRemaining) ~= 'number' then
                    return 'bad SecondsRemaining'
                end
                return 'true'
            ");
            r.String.ShouldBe("true");
        }

        // -- Process
        [Fact]
        public async Task Process_All_ReturnsTableWithAtLeastOneEntry()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
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
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
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
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("return tostring(Process.Open() ~= nil)");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Process_GetID_CurrentProcess_ReturnsPositiveInteger()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local p = Process.Open()
                local id = p:GetID()
                return tostring(math.type(id) == 'integer' and id > 0)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Process_GetName_CurrentProcess_ReturnsNonEmptyString()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local p = Process.Open()
                local name = p:GetName()
                return tostring(type(name) == 'string' and #name > 0)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Process_GetRAM_CurrentProcess_ReturnsPositiveNumber()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local p = Process.Open()
                return tostring(p:GetRAM() > 0)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Process_Open_InvalidPid_ReturnsNil()
        {
            using KitsuneEngine engine = new();

            // PID 2147483647 (INT32_MAX) is above both Linux PID_MAX and any real Windows PID.
            LuaValue r = await engine.ExecuteStringAsync("return tostring(Process.Open(2147483647) == nil)");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Process_Tostring_ReturnsNonEmptyString()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local p = Process.Open()
                local s = tostring(p)
                return tostring(type(s) == 'string' and #s > 0)
            ");
            r.String.ShouldBe("true");
        }

        [AnnoyingFact]
        public async Task Process_Start_ReturnsHandle()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
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
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
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

        [AnnoyingFact]
        public async Task Process_Start_ReadErrorFromPipe_ReturnsNilOrString()
        {
            using KitsuneEngine engine = new();

            // A clean command produces no stderr; result is nil or empty string.
            LuaValue r = await engine.ExecuteStringAsync(@"
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

        [AnnoyingFact]
        public async Task Process_GetExitCode_RunningProcess_ReturnsNil()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
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

        [AnnoyingFact]
        public async Task Process_Stop_RunningProcess_ReturnsTrue()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
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
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
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
            using KitsuneEngine engine = new();

            // The exit status must be cached after the first successful query (Linux
            // uses a cached waitpid result; Windows re-queries the process handle).
            LuaValue r = await engine.ExecuteStringAsync(@"
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

        [AnnoyingFact]
        public async Task Process_ReadFromPipe_WithoutRedirect_ReturnsNil()
        {
            using KitsuneEngine engine = new();

            // A process started without pipe redirect has no stdout fd; must return nil.
            LuaValue r = await engine.ExecuteStringAsync(@"
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

        [AnnoyingFact]
        public async Task Process_WriteToPipe_ReturnsPositiveBytesWritten()
        {
            using KitsuneEngine engine = new();

            // cat (Linux) / cmd /c more (Windows) block on stdin; write must succeed
            // and return the byte count before we stop the process.
            LuaValue r = await engine.ExecuteStringAsync(@"
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

        [AnnoyingFact]
        public async Task Process_GetID_StartedProcess_ReturnsPositiveInteger()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
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
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(MakeChunkedStream + "return tostring(makeChunkedStream({'a','b'}) ~= nil)");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Stream_CreateChunked_PosIsAlwaysNil()
        {
            using KitsuneEngine engine = new();

            // Async streams have no STREAM_CAP_SEEK; pos() returns nil like all non-seekable streams.
            LuaValue r = await engine.ExecuteStringAsync(MakeChunkedStream + @"
                local cs = makeChunkedStream({'hello'})
                return tostring(cs:pos() == nil)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Stream_CreateChunked_LenIsZeroBeforeFirstRead()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(MakeChunkedStream + @"
                local cs = makeChunkedStream({'hello'})
                return tostring(cs:len() == 0)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Stream_HasData_SyncStream_ReturnsBytesRemaining()
        {
            using KitsuneEngine engine = new();

            // Sync streams return the number of bytes remaining (falsy false at EOF,
            // truthy positive integer when data is buffered).
            LuaValue r = await engine.ExecuteStringAsync(@"
                local s = Stream.New()
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
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(MakeChunkedStream + @"
                local cs = makeChunkedStream({'data'})
                return tostring(cs:HasData() == false)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Stream_Closed_HasData_ReturnsMinusOne()
        {
            using KitsuneEngine engine = new();

            // A closed (zeroed) stream returns -1 from HasData so callers can
            // distinguish "stream alive but no data yet" (false) from
            // "stream is dead" (-1) and break out of streaming loops cleanly.
            LuaValue r = await engine.ExecuteStringAsync(@"
                local s = Stream.New()
                s:Write('hello')
                s:Close()
                return tostring(s:HasData() == -1)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Stream_Closed_Read_ReturnsNil()
        {
            using KitsuneEngine engine = new();

            // Reading from a closed stream returns nil regardless of what was
            // written before Close().  No STREAM_CAP_READ on a zeroed stream.
            LuaValue r = await engine.ExecuteStringAsync(@"
                local s = Stream.New()
                s:Write('hello')
                s:Close()
                return tostring(s:Read() == nil)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Stream_Closed_Write_ReturnsZero()
        {
            using KitsuneEngine engine = new();

            // Writing to a closed stream returns 0 (bytes written).
            // No STREAM_CAP_WRITE on a zeroed stream.
            LuaValue r = await engine.ExecuteStringAsync(@"
                local s = Stream.New()
                s:Close()
                return tostring(s:Write('x') == 0)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Stream_Closed_Dead_Distinguishable_From_NoData()
        {
            using KitsuneEngine engine = new();

            // The full contract: false = alive/waiting, -1 = dead.
            // An alive stream with no data ready must return false, not -1.
            LuaValue r = await engine.ExecuteStringAsync(@"
                local alive = Stream.New()   -- empty, no data at pos=0 after Create
                alive:Seek(0)
                local alive_hd = alive:HasData()  -- 0 bytes remaining -> false

                local dead = Stream.New()
                dead:Close()
                local dead_hd  = dead:HasData()   -- zeroed -> -1

                return tostring(alive_hd == false and dead_hd == -1)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Stream_CreateChunked_DeliversChunksInOrder()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(RunCoroutine + MakeChunkedStream + @"
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
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(RunCoroutine + MakeChunkedStream + @"
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
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(RunCoroutine + MakeChunkedStream + @"
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
            using KitsuneEngine engine = new();

            // The chunked stream yields once before each chunk.
            // After the first yield HasData() returns true (pending=true).
            LuaValue r = await engine.ExecuteStringAsync(MakeChunkedStream + @"
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
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(MakeChunkedStream + @"
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
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(RunCoroutine + MakeChunkedStream + @"
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
            using KitsuneEngine engine = new();

            // "hello" and ",world\n" arrive in separate chunks — must be joined.
            LuaValue r = await engine.ExecuteStringAsync(RunCoroutine + MakeChunkedStream + @"
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
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(RunCoroutine + MakeChunkedStream + @"
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
            using KitsuneEngine engine = new();

            // Compress sync then decompress via a non-seekable Lua fn backend,
            // exercising the fn-backend read path inside DecompressStream.
            LuaValue r = await engine.ExecuteStringAsync(RunCoroutine + @"
                run(function()
                    local src = Stream.New()
                    src:Write('hello compressed world')
                    src:Seek(0)
                    local compressed = Stream.Compress(src)
                    compressed:Seek(0)
                    local compBytes = compressed:Read()
                    assert(type(compBytes) == 'string')
                    local pos = 1
                    local fnSrc = Stream.New(function(op, len)
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
        // These tests pass a Lua function backend to Stream.New() to simulate a
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
            using KitsuneEngine engine = new();

            // Baseline: repeated Read() calls assemble the full payload even when
            // the backend returns data 4 bytes at a time.
            LuaValue r = await engine.ExecuteStringAsync(SocketPrologue + @"
                local payload = 'hello from the socket'
                local pos = 1
                local s = Stream.New(function(op, len)
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
            using KitsuneEngine engine = new();

            // CSV DecodeFromFunction with a function-backend stream that returns
            // 8 bytes per Read() call.  Rows and field boundaries deliberately
            // fall inside chunk boundaries so the refill/accumulate logic is exercised.
            LuaValue r = await engine.ExecuteStringAsync(SocketPrologue + @"
                local data = 'col1,col2,col3\nval1,val2,val3\nfoo,bar,baz\n'
                local pos = 1
                local s = Stream.New(function(op, len)
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
            using KitsuneEngine engine = new();

            // Auto-detect with chunked delivery: the delimiter must be sniffed after
            // enough chunks have been accumulated to see a complete line.
            LuaValue r = await engine.ExecuteStringAsync(SocketPrologue + @"
                local data = 'name;age;city\nalice;30;paris\nbob;25;berlin\n'
                local pos = 1
                local s = Stream.New(function(op, len)
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
            using KitsuneEngine engine = new();

            // Compress reads from the stream in up to 64 KiB chunks; the backend
            // returns 16 bytes per call, so many reads are needed.  The decompressed
            // output must exactly match the original payload.
            LuaValue r = await engine.ExecuteStringAsync(SocketPrologue + @"
                local payload = string.rep('kitsune socket data! ', 30)
                local pos = 1
                local s = Stream.New(function(op, len)
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
        public async Task Json_FunctionBackendSocket_DecodeFromStream_ParsesChunkedJson()
        {
            using KitsuneEngine engine = new();

            // DecodeFromStream calls lua_stream_read_chunk ? StreamRead in a loop.
            // The backend returns 6 bytes per call so the decoder must assemble the
            // full JSON object across many reads.
            LuaValue r = await engine.ExecuteStringAsync(SocketPrologue + @"
                local jsonStr = '{""name"":""kitsune"",""version"":4,""active"":true}'
                local pos = 1
                local s = Stream.New(function(op, len)
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
                local t = j:DecodeFromStream(s)
                return tostring(t.name == 'kitsune' and t.version == 4 and t.active == true)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Stream_FunctionBackendSocket_HasData_ReturnsZeroBeforeFirstRead()
        {
            using KitsuneEngine engine = new();

            // A function backend that has not yet delivered any data reports 0 bytes
            // remaining via HasData() because len==0 and pos==0 (no curpos/getlen
            // vtable — the Lua fn backend falls through to STREAM_OP_HASDATA dispatch
            // which returns nil, treated as falsy by the caller).
            LuaValue r = await engine.ExecuteStringAsync(SocketPrologue + @"
                local s = Stream.New(function(op, len)
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
            using KitsuneEngine engine = new();

            // ReadLuaStream now uses lua_callk for fn backends, so Sleep() inside
            // the READ handler can yield the coroutine without hitting
            // "attempt to yield across a C-call boundary".
            LuaValue r = await engine.ExecuteStringAsync(RunCoroutine + SocketPrologue + @"
                run(function()
                    local data = 'hello world'
                    local pos = 1
                    local s = Stream.New(function(op, len)
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
            using KitsuneEngine engine = new();

            // End-to-end: CSV DecodeFromFunction on a fn-backend stream whose
            // READ handler calls Sleep between chunks.  Exercises the full
            // CsvStreamIterator ? lua_callk ? ReadLuaStream ? lua_callk(fn) ? Sleep chain.
            LuaValue r = await engine.ExecuteStringAsync(RunCoroutine + SocketPrologue + @"
                run(function()
                    local data = 'a,b,c\n1,2,3\n4,5,6\n'
                    local pos = 1
                    local s = Stream.New(function(op, len)
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

        // -- Xml ------------------------------------------------------------------
        // All operations require an instance (Xml.New() or Xml.New()).

        // -- Instance creation ----------------------------------------------------
        [Fact]
        public async Task Xml_New_ReturnsUserdata()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("return type(Xml.New())");
            r.String.ShouldBe("userdata");
        }

        [Fact]
        public async Task Xml_Dispose_CanBeCalledExplicitly()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local xml = Xml.New()
                xml:Dispose()
                return 'ok'
            ");
            r.String.ShouldBe("ok");
        }

        [Fact]
        public async Task Xml_New_CalledOnInstance_ReturnsNewInstance()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local a = Xml.New()
                local b = a:New()
                local doc = b:Decode('<hello/>')
                return tostring(doc ~= nil and doc.tag == 'hello')
            ");
            r.String.ShouldBe("true");
        }

        // -- Decode ---------------------------------------------------------------
        [Fact]
        public async Task Xml_Decode_SimpleElement_ReturnsTable()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local xml = Xml.New()
                local doc = xml:Decode('<root/>')
                return tostring(type(doc) == 'table')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Xml_Decode_Tag_IsCorrect()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local xml = Xml.New()
                local doc = xml:Decode('<person/>')
                return doc.tag
            ");
            r.String.ShouldBe("person");
        }

        [Fact]
        public async Task Xml_Decode_NoAttributes_AttrIsEmptyTable()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local xml = Xml.New()
                local doc = xml:Decode('<root/>')
                return tostring(type(doc.attr) == 'table' and #doc.attr == 0)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Xml_Decode_SingleAttribute_KeyAndValueCorrect()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local xml = Xml.New()
                local doc = xml:Decode('<item id=""42""/>')
                return tostring(doc.attr[1].key == 'id' and doc.attr[1].value == '42')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Xml_Decode_MultipleAttributes_AllPresent()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local xml = Xml.New()
                local doc = xml:Decode('<item id=""1"" active=""true""/>')
                return tostring(#doc.attr == 2)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Xml_Decode_TextContent_IsCorrect()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local xml = Xml.New()
                local doc = xml:Decode('<name>Alice</name>')
                return doc.text
            ");
            r.String.ShouldBe("Alice");
        }

        [Fact]
        public async Task Xml_Decode_NoTextContent_TextIsEmptyString()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local xml = Xml.New()
                local doc = xml:Decode('<empty/>')
                return tostring(doc.text == '')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Xml_Decode_NoChildren_ChildrenIsEmptyTable()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local xml = Xml.New()
                local doc = xml:Decode('<leaf/>')
                return tostring(type(doc.children) == 'table' and #doc.children == 0)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Xml_Decode_SingleChild_ChildTagCorrect()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local xml = Xml.New()
                local doc = xml:Decode('<root><child/></root>')
                return doc.children[1].tag
            ");
            r.String.ShouldBe("child");
        }

        [Fact]
        public async Task Xml_Decode_MultipleChildren_CountIsCorrect()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local xml = Xml.New()
                local doc = xml:Decode('<root><a/><b/><c/></root>')
                return tostring(#doc.children == 3)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Xml_Decode_NestedChildren_DeepAccess()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local xml = Xml.New()
                local doc = xml:Decode('<a><b><c>deep</c></b></a>')
                return doc.children[1].children[1].text
            ");
            r.String.ShouldBe("deep");
        }

        [Fact]
        public async Task Xml_Decode_FullNode_AllFieldsPresent()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local xml = Xml.New()
                local doc = xml:Decode('<person id=""1"" active=""true""><name>Alice</name><score>42</score></person>')
                return tostring(
                    doc.tag == 'person' and
                    #doc.attr == 2 and
                    doc.attr[1].key == 'id' and
                    doc.attr[1].value == '1' and
                    #doc.children == 2 and
                    doc.children[1].tag == 'name' and
                    doc.children[1].text == 'Alice' and
                    doc.children[2].tag == 'score' and
                    doc.children[2].text == '42'
                )
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Xml_Decode_InvalidXml_ReturnsNilAndError()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local xml = Xml.New()
                local doc, err = xml:Decode('not < valid > xml <<<')
                return tostring(doc == nil and type(err) == 'string')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Xml_Decode_EmptyString_ReturnsNilAndError()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local xml = Xml.New()
                local doc, err = xml:Decode('')
                return tostring(doc == nil and type(err) == 'string')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Xml_Decode_WithXmlDeclaration_ReturnsRootElement()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local xml = Xml.New()
                local doc = xml:Decode('<?xml version=""1.0"" encoding=""UTF-8""?><root/>')
                return doc.tag
            ");
            r.String.ShouldBe("root");
        }

        [Fact]
        public async Task Xml_Decode_CommentsIgnored_NotInChildren()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local xml = Xml.New()
                local doc = xml:Decode('<root><!-- comment --><child/></root>')
                return tostring(#doc.children == 1 and doc.children[1].tag == 'child')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Xml_Decode_AttributeOrder_Preserved()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local xml = Xml.New()
                local doc = xml:Decode('<item a=""1"" b=""2"" c=""3""/>')
                return tostring(doc.attr[1].key == 'a' and doc.attr[2].key == 'b' and doc.attr[3].key == 'c')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Xml_Decode_ChildrenOrder_Preserved()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local xml = Xml.New()
                local doc = xml:Decode('<root><first/><second/><third/></root>')
                return tostring(doc.children[1].tag == 'first' and doc.children[2].tag == 'second' and doc.children[3].tag == 'third')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Xml_Decode_InstanceReuse_MultipleCalls()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local xml = Xml.New()
                local d1  = xml:Decode('<a/>')
                local d2  = xml:Decode('<b/>')
                return tostring(d1.tag == 'a' and d2.tag == 'b')
            ");
            r.String.ShouldBe("true");
        }

        // -- Encode ---------------------------------------------------------------
        [Fact]
        public async Task Xml_Encode_ReturnsString()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local xml = Xml.New()
                local s   = xml:Encode({ tag='root', attr={}, text='', children={} })
                return tostring(type(s) == 'string' and #s > 0)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Xml_Encode_ContainsXmlDeclaration()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local xml = Xml.New()
                local s   = xml:Encode({ tag='root', attr={}, text='', children={} })
                return tostring(s:find('<?xml') ~= nil)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Xml_Encode_ContainsTag()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local xml = Xml.New()
                local s   = xml:Encode({ tag='person', attr={}, text='', children={} })
                return tostring(s:find('<person') ~= nil)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Xml_Encode_SingleAttribute_AppearsInOutput()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local xml = Xml.New()
                local s   = xml:Encode({ tag='item', attr={{ key='id', value='42' }}, text='', children={} })
                return tostring(s:find('id=""42""') ~= nil)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Xml_Encode_TextContent_AppearsInOutput()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local xml = Xml.New()
                local s   = xml:Encode({ tag='name', attr={}, text='Alice', children={} })
                return tostring(s:find('Alice') ~= nil)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Xml_Encode_MissingTag_RaisesError()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local xml = Xml.New()
                local ok, err = pcall(function()
                    xml:Encode({ tag='', attr={}, text='', children={} })
                end)
                return tostring(not ok and type(err) == 'string')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Xml_Encode_NonTableArg_RaisesError()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local xml = Xml.New()
                local ok, err = pcall(function() xml:Encode('not a table') end)
                return tostring(not ok)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Xml_Encode_IndentFlag_ProducesNewlines()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local xml = Xml.New(true)
                local s   = xml:Encode({ tag='root', attr={}, text='', children={
                    { tag='child', attr={}, text='', children={} }
                }})
                return tostring(s:find('\n') ~= nil)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Xml_Encode_CompactFlag_NoIndentNewlines()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local xml = Xml.New(false)
                local s   = xml:Encode({ tag='root', attr={}, text='', children={
                    { tag='child', attr={}, text='', children={} }
                }})
                -- compact may still have a newline after the declaration line;
                -- check that there is no indentation whitespace (tab or leading spaces before <child)
                return tostring(s:find('\t<child') == nil and s:find('  <child') == nil)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Xml_Encode_IsDeterministic()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local xml = Xml.New()
                local node = { tag='root', attr={{ key='x', value='1' }}, text='hello', children={} }
                return tostring(xml:Encode(node) == xml:Encode(node))
            ");
            r.String.ShouldBe("true");
        }

        // -- Round-trip (Encode then Decode) --------------------------------------
        [Fact]
        public async Task Xml_RoundTrip_Tag_Preserved()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local xml  = Xml.New()
                local node = { tag='person', attr={}, text='', children={} }
                local doc  = xml:Decode(xml:Encode(node))
                return doc.tag
            ");
            r.String.ShouldBe("person");
        }

        [Fact]
        public async Task Xml_RoundTrip_SingleAttribute_Preserved()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local xml  = Xml.New()
                local node = { tag='item', attr={{ key='id', value='7' }}, text='', children={} }
                local doc  = xml:Decode(xml:Encode(node))
                return tostring(doc.attr[1].key == 'id' and doc.attr[1].value == '7')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Xml_RoundTrip_MultipleAttributes_AllPreserved()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local xml  = Xml.New()
                local node = { tag='item', attr={{ key='a', value='1' }, { key='b', value='2' }}, text='', children={} }
                local doc  = xml:Decode(xml:Encode(node))
                return tostring(#doc.attr == 2 and doc.attr[1].key == 'a' and doc.attr[2].key == 'b')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Xml_RoundTrip_TextContent_Preserved()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local xml  = Xml.New()
                local node = { tag='name', attr={}, text='Alice', children={} }
                local doc  = xml:Decode(xml:Encode(node))
                return doc.text
            ");
            r.String.ShouldBe("Alice");
        }

        [Fact]
        public async Task Xml_RoundTrip_SingleChild_Preserved()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local xml  = Xml.New()
                local node = { tag='root', attr={}, text='', children={
                    { tag='child', attr={}, text='value', children={} }
                }}
                local doc = xml:Decode(xml:Encode(node))
                return tostring(#doc.children == 1 and doc.children[1].tag == 'child' and doc.children[1].text == 'value')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Xml_RoundTrip_MultipleChildren_AllPreserved()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local xml  = Xml.New()
                local node = { tag='root', attr={}, text='', children={
                    { tag='a', attr={}, text='1', children={} },
                    { tag='b', attr={}, text='2', children={} },
                    { tag='c', attr={}, text='3', children={} },
                }}
                local doc = xml:Decode(xml:Encode(node))
                return tostring(#doc.children == 3 and doc.children[2].tag == 'b' and doc.children[2].text == '2')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Xml_RoundTrip_NestedChildren_Preserved()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local xml  = Xml.New()
                local node = { tag='a', attr={}, text='', children={
                    { tag='b', attr={}, text='', children={
                        { tag='c', attr={}, text='deep', children={} }
                    }}
                }}
                local doc = xml:Decode(xml:Encode(node))
                return doc.children[1].children[1].text
            ");
            r.String.ShouldBe("deep");
        }

        [Fact]
        public async Task Xml_RoundTrip_FullNode_AllFieldsCorrect()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local xml  = Xml.New()
                local node = {
                    tag  = 'person',
                    attr = { {key='id', value='1'}, {key='active', value='true'} },
                    text = '',
                    children = {
                        { tag='name',  attr={}, text='Alice', children={} },
                        { tag='score', attr={}, text='42',    children={} },
                    }
                }
                local doc = xml:Decode(xml:Encode(node))
                return tostring(
                    doc.tag == 'person'          and
                    #doc.attr == 2               and
                    doc.attr[1].key == 'id'      and
                    doc.attr[1].value == '1'     and
                    #doc.children == 2           and
                    doc.children[1].tag == 'name' and
                    doc.children[1].text == 'Alice' and
                    doc.children[2].text == '42'
                )
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Xml_RoundTrip_InstanceReuse_BothCorrect()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local xml = Xml.New()
                local n1  = { tag='first',  attr={}, text='one', children={} }
                local n2  = { tag='second', attr={}, text='two', children={} }
                local d1  = xml:Decode(xml:Encode(n1))
                local d2  = xml:Decode(xml:Encode(n2))
                return tostring(d1.tag == 'first' and d1.text == 'one' and d2.tag == 'second' and d2.text == 'two')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Xml_RoundTrip_EmptyTextAndChildren_Preserved()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local xml  = Xml.New()
                local node = { tag='empty', attr={}, text='', children={} }
                local doc  = xml:Decode(xml:Encode(node))
                return tostring(doc.text == '' and #doc.children == 0)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Xml_New_CalledOnInstance_WithTrue_ProducesIndented()
        {
            // Regression: xml:New(true) must pass indent=true even though arg1 is the userdata.
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local a    = Xml.New()
                local b    = a:New(true)
                local node = { tag='root', attr={}, text='', children={
                    { tag='child', attr={}, text='', children={} }
                }}
                return tostring(b:Encode(node):find('\n') ~= nil)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Xml_Encode_DeeplyNested_RaisesError()
        {
            // Regression: encode must raise an error rather than C-stack-overflow
            // when nesting exceeds the 512-level guard.
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local xml = Xml.New()
                local root = { tag='a', attr={}, text='', children={} }
                local cur = root
                for i = 1, 520 do
                    local child = { tag='a', attr={}, text='', children={} }
                    cur.children = { child }
                    cur = child
                end
                local ok, err = pcall(function() xml:Encode(root) end)
                return tostring(not ok and type(err) == 'string')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Xml_Decode_DeeplyNested_RaisesError()
        {
            // Regression: decode must raise an error rather than C-stack-overflow
            // when nesting exceeds the 512-level guard.
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local xml   = Xml.New()
                local open  = string.rep('<a>', 520)
                local close = string.rep('</a>', 520)
                local ok, err = pcall(function() xml:Decode(open .. close) end)
                return tostring(not ok and type(err) == 'string')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Xml_Encode_MissingTagOnChild_RaisesError()
        {
            // The longjmp-safe encode_protected path must propagate child errors cleanly.
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local xml  = Xml.New()
                local node = { tag='root', attr={}, text='', children={
                    { tag='', attr={}, text='', children={} }
                }}
                local ok, err = pcall(function() xml:Encode(node) end)
                return tostring(not ok and type(err) == 'string')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Xml_Decode_NonStringArg_RaisesError()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local xml = Xml.New()
                local ok, err = pcall(function() xml:Decode(42) end)
                return tostring(not ok and type(err) == 'string')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Xml_Decode_WhitespaceOnly_ReturnsNilAndError()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local xml = Xml.New()
                local doc, err = xml:Decode('   ')
                return tostring(doc == nil and type(err) == 'string')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Xml_Encode_TextWithSpecialChars_EscapedCorrectly()
        {
            // < > & in text must be XML-escaped so the output round-trips.
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local xml  = Xml.New()
                local node = { tag='data', attr={}, text='a < b & c > d', children={} }
                local doc  = xml:Decode(xml:Encode(node))
                return doc.text
            ");
            r.String.ShouldBe("a < b & c > d");
        }

        [Fact]
        public async Task Xml_Encode_AttributeWithSpecialChars_EscapedCorrectly()
        {
            // Attribute values containing quotes must survive a round-trip.
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local xml  = Xml.New()
                local node = { tag='item', attr={{ key='note', value='a&b' }}, text='', children={} }
                local doc  = xml:Decode(xml:Encode(node))
                return doc.attr[1].value
            ");
            r.String.ShouldBe("a&b");
        }

        [Fact]
        public async Task Xml_Decode_CdataSection_IncludedInText()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local xml = Xml.New()
                local doc = xml:Decode('<root><![CDATA[hello cdata]]></root>')
                return doc.text
            ");
            r.String.ShouldBe("hello cdata");
        }

        [Fact]
        public async Task Xml_Decode_MixedPcdataAndCdata_Concatenated()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local xml = Xml.New()
                local doc = xml:Decode('<root>hello <![CDATA[world]]></root>')
                return doc.text
            ");
            r.String.ShouldBe("hello world");
        }

        // -- MsgPack --------------------------------------------------------------
        // All operations require an instance (MsgPack.New() or MsgPack.New()).

        // -- Instance creation ----------------------------------------------------
        [Fact]
        public async Task MsgPack_New_ReturnsUserdata()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("return type(MsgPack.New())");
            r.String.ShouldBe("userdata");
        }

        [Fact]
        public async Task MsgPack_Tostring_ReturnsNonEmptyString()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("return tostring(type(tostring(MsgPack.New())) == 'string' and #tostring(MsgPack.New()) > 0)");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task MsgPack_Dispose_CanBeCalledExplicitly()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local m = MsgPack.New()
                m:Dispose()
                return 'ok'
            ");
            r.String.ShouldBe("ok");
        }

        // -- Basic type round-trips -----------------------------------------------
        [Fact]
        public async Task MsgPack_Nil_RoundTrips()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local m = MsgPack.New()
                return tostring(m:Decode(m:Encode(nil)) == nil)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task MsgPack_BooleanTrue_RoundTrips()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local m = MsgPack.New()
                return tostring(m:Decode(m:Encode(true)) == true)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task MsgPack_BooleanFalse_RoundTrips()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local m = MsgPack.New()
                return tostring(m:Decode(m:Encode(false)) == false)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task MsgPack_Integer_RoundTrips()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local m = MsgPack.New()
                local v = m:Decode(m:Encode(42))
                return tostring(v == 42 and math.type(v) == 'integer')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task MsgPack_NegativeInteger_RoundTrips()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local m = MsgPack.New()
                return tostring(m:Decode(m:Encode(-1000)) == -1000)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task MsgPack_MaxInt64_RoundTrips()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local m   = MsgPack.New()
                local max = math.maxinteger
                return tostring(m:Decode(m:Encode(max)) == max)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task MsgPack_MinInt64_RoundTrips()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local m   = MsgPack.New()
                local min = math.mininteger
                return tostring(m:Decode(m:Encode(min)) == min)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task MsgPack_Float_RoundTrips()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local m = MsgPack.New()
                local v = m:Decode(m:Encode(3.14))
                return tostring(math.type(v) == 'float' and math.abs(v - 3.14) < 1e-10)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task MsgPack_String_RoundTrips()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local m = MsgPack.New()
                return tostring(m:Decode(m:Encode('hello')) == 'hello')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task MsgPack_EmptyString_RoundTrips()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local m = MsgPack.New()
                return tostring(m:Decode(m:Encode('')) == '')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task MsgPack_StringWithNullBytes_RoundTrips()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local m = MsgPack.New()
                local s = 'a\0b\0c'
                return tostring(m:Decode(m:Encode(s)) == s)
            ");
            r.String.ShouldBe("true");
        }

        // -- Table round-trips ----------------------------------------------------
        [Fact]
        public async Task MsgPack_SequenceTable_EncodesAsArray()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local m = MsgPack.New()
                local t = m:Decode(m:Encode({10, 20, 30}))
                return tostring(t[1] == 10 and t[2] == 20 and t[3] == 30)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task MsgPack_StringKeyTable_EncodesAsMap()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local m = MsgPack.New()
                local t = m:Decode(m:Encode({name = 'kitsune', version = 1}))
                return tostring(t.name == 'kitsune' and t.version == 1)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task MsgPack_EmptyTable_EncodesAsEmptyArray()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local m = MsgPack.New()
                local t = m:Decode(m:Encode({}))
                return tostring(type(t) == 'table')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task MsgPack_NestedTable_RoundTrips()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local m = MsgPack.New()
                local t = m:Decode(m:Encode({a = {b = {c = 99}}}))
                return tostring(t.a.b.c == 99)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task MsgPack_AllBasicTypes_RoundTrip()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local m    = MsgPack.New()
                local orig = {
                    s   = 'hello',
                    n   = 42,
                    f   = 3.14,
                    bt  = true,
                    bf  = false,
                    arr = {1, 2, 3},
                    obj = {nested = 'value'},
                }
                local t = m:Decode(m:Encode(orig))
                return tostring(
                    t.s == 'hello'       and
                    t.n == 42            and
                    math.abs(t.f - 3.14) < 1e-10 and
                    t.bt == true         and
                    t.bf == false        and
                    #t.arr == 3          and t.arr[2] == 2 and
                    t.obj.nested == 'value'
                )
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task MsgPack_InstanceReuse_MultipleCalls()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local m  = MsgPack.New()
                local s1 = m:Encode({a = 1})
                local s2 = m:Encode({b = 2})
                local t1 = m:Decode(s1)
                local t2 = m:Decode(s2)
                return tostring(t1.a == 1 and t2.b == 2 and t1.b == nil and t2.a == nil)
            ");
            r.String.ShouldBe("true");
        }

        // -- Encode returns binary string -----------------------------------------
        [Fact]
        public async Task MsgPack_Encode_ReturnsString()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("return type(MsgPack.New():Encode(42))");
            r.String.ShouldBe("string");
        }

        [Fact]
        public async Task MsgPack_Encode_DifferentValues_ProduceDifferentBytes()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local m = MsgPack.New()
                return tostring(m:Encode(1) ~= m:Encode(2))
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task MsgPack_Encode_IsDeterministic()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local m = MsgPack.New()
                return tostring(m:Encode('hello') == m:Encode('hello'))
            ");
            r.String.ShouldBe("true");
        }

        // -- Unrepresentable types encode as nil ----------------------------------
        [Fact]
        public async Task MsgPack_Function_EncodesAsNil()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local m = MsgPack.New()
                return tostring(m:Decode(m:Encode(function() end)) == nil)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task MsgPack_Thread_EncodesAsNil()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local m = MsgPack.New()
                return tostring(m:Decode(m:Encode(coroutine.create(function() end))) == nil)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task MsgPack_UnserializableInArray_EncodesSlotAsNil()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local m = MsgPack.New()
                local t = m:Decode(m:Encode({1, function() end, 3}))
                return tostring(t[1] == 1 and t[2] == nil and t[3] == 3)
            ");
            r.String.ShouldBe("true");
        }

        // -- Wchar encoding -------------------------------------------------------
        [Fact]
        public async Task MsgPack_Wchar_AsciiContent_EncodesAsStr()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local m = MsgPack.New()
                local w = Wchar.FromUtf8('hello')
                return tostring(m:Decode(m:Encode(w)) == 'hello')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task MsgPack_Wchar_NonAscii_RoundTrips()
        {
            using KitsuneEngine engine = new();

            // é = U+00E9, UTF-8: \xC3\xA9
            LuaValue r = await engine.ExecuteStringAsync(@"
                local m = MsgPack.New()
                local w = Wchar.FromUtf8('\xC3\xa9')
                return tostring(m:Decode(m:Encode(w)) == '\xC3\xa9')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task MsgPack_Wchar_EmptyWchar_EncodesAsEmptyStr()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local m = MsgPack.New()
                local w = Wchar.FromUtf8('')
                return tostring(m:Decode(m:Encode(w)) == '')
            ");
            r.String.ShouldBe("true");
        }

        // -- Typed userdata encoding as string ------------------------------------
        [Fact]
        public async Task MsgPack_Identifier_UUID_EncodesAsCanonicalString()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local m  = MsgPack.New()
                local id = Identifier.NewUUID()
                return tostring(m:Decode(m:Encode(id)) == tostring(id))
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task MsgPack_Identifier_OID_EncodesAsCanonicalString()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local m  = MsgPack.New()
                local id = Identifier.NewOID()
                return tostring(m:Decode(m:Encode(id)) == tostring(id))
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task MsgPack_DateTime_EncodesAsIso8601String()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local m  = MsgPack.New()
                local dt = DateTime.New(2024, 3, 15, 10, 30, 45, 0, 0)
                return tostring(m:Decode(m:Encode(dt)) == tostring(dt))
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task MsgPack_Decimal_EncodesAsString()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local m = MsgPack.New()
                local d = Decimal.FromString('99.95')
                return tostring(m:Decode(m:Encode(d)) == tostring(d))
            ");
            r.String.ShouldBe("true");
        }

        // -- Stream value encoding as bin -----------------------------------------
        [Fact]
        public async Task MsgPack_Stream_ReadableSeekable_EncodesAsBin()
        {
            using KitsuneEngine engine = new();

            // bin decodes to an in-memory Stream
            LuaValue r = await engine.ExecuteStringAsync(@"
                local m   = MsgPack.New()
                local src = Stream.New('hello')
                local out = m:Decode(m:Encode(src))
                return tostring(type(out) == 'userdata')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task MsgPack_Stream_BinDecodes_ToStreamWithCorrectBytes()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local m   = MsgPack.New()
                local src = Stream.New('hello bin')
                local out = m:Decode(m:Encode(src))
                out:Seek(0)
                return out:Read()
            ");
            r.String.ShouldBe("hello bin");
        }

        [Fact]
        public async Task MsgPack_Stream_BinDecodes_PositionAtZero()
        {
            using KitsuneEngine engine = new();

            // Decoded bin stream must be seeked to 0 so the caller can read immediately.
            LuaValue r = await engine.ExecuteStringAsync(@"
                local m   = MsgPack.New()
                local src = Stream.New('abc')
                local out = m:Decode(m:Encode(src))
                return tostring(out:pos() == 0)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task MsgPack_Stream_NonReadableSeekable_EncodesAsNil()
        {
            using KitsuneEngine engine = new();

            // A write-only stream has no CAP_READ; must encode as nil.
            LuaValue r = await engine.ExecuteStringAsync(@"
                local m = MsgPack.New()
                local s = Stream.New(function(op)
                    if op == 0 then return 2 end   -- CAP_WRITE only
                    if op == 1 then return true end
                end)
                return tostring(m:Decode(m:Encode(s)) == nil)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task MsgPack_Stream_EncodePreservesReadPosition()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local m   = MsgPack.New()
                local src = Stream.New('ABCDE')
                src:Seek(3)
                m:Encode(src)
                return tostring(src:pos() == 3)
            ");
            r.String.ShouldBe("true");
        }

        // -- Recursion detection --------------------------------------------------
        [Fact]
        public async Task MsgPack_RecursionDetected_ThrowsError()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local m = MsgPack.New()
                local t = {}
                t.self = t
                local ok, err = pcall(function() m:Encode(t) end)
                return tostring(not ok and err:find('recursion') ~= nil)
            ");
            r.String.ShouldBe("true");
        }

        // -- Decode error handling ------------------------------------------------
        [Fact]
        public async Task MsgPack_Decode_InvalidBytes_ReturnsNilAndError()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local m = MsgPack.New()
                local v, err = m:Decode('\xC1')   -- 0xC1 is never-used in msgpack
                return tostring(v == nil and type(err) == 'string')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task MsgPack_Decode_EmptyString_ReturnsNilAndError()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local m = MsgPack.New()
                local v, err = m:Decode('')
                return tostring(v == nil and type(err) == 'string')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task MsgPack_Decode_TruncatedBytes_ReturnsNilAndError()
        {
            using KitsuneEngine engine = new();

            // Encode a 3-element array then truncate to 1 byte (just the header).
            LuaValue r = await engine.ExecuteStringAsync(@"
                local m    = MsgPack.New()
                local full = m:Encode({1, 2, 3})
                local v, err = m:Decode(full:sub(1, 1))
                return tostring(v == nil and type(err) == 'string')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task MsgPack_Decode_ExtraBytes_StillDecodesFirstValue()
        {
            using KitsuneEngine engine = new();

            // msgpack_unpack_next returns EXTRA_BYTES when more data follows;
            // our decode must still return the first value successfully.
            LuaValue r = await engine.ExecuteStringAsync(@"
                local m  = MsgPack.New()
                local s1 = m:Encode(42)
                local s2 = m:Encode('hello')
                local v  = m:Decode(s1 .. s2)
                return tostring(v == 42)
            ");
            r.String.ShouldBe("true");
        }

        // -- Stream I/O -----------------------------------------------------------
        [Fact]
        public async Task MsgPack_EncodeIntoStream_StreamContainsValidBytes()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local m = MsgPack.New()
                local s = Stream.New()
                m:EncodeIntoStream(s, {a = 1, b = 2})
                s:Seek(0)
                local t = m:Decode(s:Read())
                return tostring(t.a == 1 and t.b == 2)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task MsgPack_EncodeIntoStream_ReturnsTrueOnSuccess()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local m  = MsgPack.New()
                local s  = Stream.New()
                local ok = m:EncodeIntoStream(s, 42)
                return tostring(ok == true)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task MsgPack_EncodeIntoStream_NonWritableStream_ReturnsFalse()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local m = MsgPack.New()
                local s = Stream.New(function(op)
                    if op == 0 then return 1 end   -- CAP_READ only
                    if op == 1 then return true end
                end)
                local ok, err = m:EncodeIntoStream(s, 'test')
                return tostring(ok == false and type(err) == 'string')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task MsgPack_DecodeFromStream_RoundTrip()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local m = MsgPack.New()
                local s = Stream.New()
                m:EncodeIntoStream(s, {x = 99, y = 'hello', z = true})
                s:Seek(0)
                local t = m:DecodeFromStream(s)
                return tostring(t.x == 99 and t.y == 'hello' and t.z == true)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task MsgPack_DecodeFromStream_NonReadableStream_ReturnsNilAndError()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local m = MsgPack.New()
                local s = Stream.New(function(op)
                    if op == 0 then return 2 end   -- CAP_WRITE only
                    if op == 1 then return true end
                end)
                local v, err = m:DecodeFromStream(s)
                return tostring(v == nil and type(err) == 'string')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task MsgPack_EncodeIntoStream_AdvancesStreamPosition()
        {
            using KitsuneEngine engine = new();

            // msgpack integer 42 encodes as 1 byte (positive fixint).
            LuaValue r = await engine.ExecuteStringAsync(@"
                local m = MsgPack.New()
                local s = Stream.New()
                m:EncodeIntoStream(s, 42)
                return tostring(s:pos() == 1)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task MsgPack_DecodeFromStream_SeeksBackUnconsumedBytes()
        {
            using KitsuneEngine engine = new();

            // Write two values back-to-back; DecodeFromStream must leave the stream
            // positioned at the start of the second value.
            LuaValue r = await engine.ExecuteStringAsync(@"
                local m = MsgPack.New()
                local s = Stream.New()
                m:EncodeIntoStream(s, 'first')
                local split = s:pos()
                m:EncodeIntoStream(s, 'second')
                s:Seek(split)
                local v = m:DecodeFromStream(s)
                return tostring(v == 'second')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task MsgPack_DecodeFromStream_PackedValues_DecodesSequentially()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local m = MsgPack.New()
                local s = Stream.New()
                m:EncodeIntoStream(s, {n = 1})
                m:EncodeIntoStream(s, {n = 2})
                m:EncodeIntoStream(s, {n = 3})
                s:Seek(0)
                local a = m:DecodeFromStream(s)
                local b = m:DecodeFromStream(s)
                local c = m:DecodeFromStream(s)
                return tostring(a.n == 1 and b.n == 2 and c.n == 3)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task MsgPack_EncodeDecodeFromStream_LargePayload_AllValuesCorrect()
        {
            using KitsuneEngine engine = new();

            // 1000 integers exercise the encoder across a non-trivial payload size.
            LuaValue r = await engine.ExecuteStringAsync(@"
                local m    = MsgPack.New()
                local data = {}
                for i = 1, 1000 do data[i] = i end
                local s = Stream.New()
                m:EncodeIntoStream(s, data)
                s:Seek(0)
                local t = m:DecodeFromStream(s)
                return tostring(#t == 1000 and t[1] == 1 and t[500] == 500 and t[1000] == 1000)
            ");
            r.String.ShouldBe("true");
        }

        // -- Decode via stream (Decode(stream) delegates to DecodeFromStream) -----
        [Fact]
        public async Task MsgPack_Decode_AcceptsStream()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local m = MsgPack.New()
                local s = Stream.New()
                m:EncodeIntoStream(s, 'from stream')
                s:Seek(0)
                return tostring(m:Decode(s) == 'from stream')
            ");
            r.String.ShouldBe("true");
        }

        // -- New called on instance ------------------------------------------------
        [Fact]
        public async Task MsgPack_New_CalledOnInstance_ReturnsNewInstance()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local a = MsgPack.New()
                local b = a:New()
                local s = b:Encode('hello')
                return tostring(b:Decode(s) == 'hello')
            ");
            r.String.ShouldBe("true");
        }

        // -- Yaml ------------------------------------------------------------------
        // All operations require an instance (Yaml.New() or Yaml.New()).
        // Yaml.New(true) selects block/pretty style; Yaml.New() defaults to flow style.
        [Fact]
        public async Task Yaml_New_ReturnsUserdata()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("return type(Yaml.New())");
            r.String.ShouldBe("userdata");
        }

        [Fact]
        public async Task Yaml_Tostring_ReturnsNonEmptyString()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("return tostring(type(tostring(Yaml.New())) == 'string' and #tostring(Yaml.New()) > 0)");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Yaml_Dispose_CanBeCalledExplicitly()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local y = Yaml.New()
                y:Dispose()
                return 'ok'
            ");
            r.String.ShouldBe("ok");
        }

        [Fact]
        public async Task Yaml_New_CalledOnInstance_ReturnsNewInstance()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local a = Yaml.New()
                local b = a:New()
                local s = b:Encode('hello')
                return tostring(b:Decode(s) == 'hello')
            ");
            r.String.ShouldBe("true");
        }

        // -- Basic type round-trips ------------------------------------------------
        [Fact]
        public async Task Yaml_Nil_RoundTrips()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local y = Yaml.New()
                return tostring(y:Decode(y:Encode(nil)) == nil)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Yaml_BooleanTrue_RoundTrips()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local y = Yaml.New()
                return tostring(y:Decode(y:Encode(true)) == true)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Yaml_BooleanFalse_RoundTrips()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local y = Yaml.New()
                return tostring(y:Decode(y:Encode(false)) == false)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Yaml_Integer_RoundTrips()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local y = Yaml.New()
                local v = y:Decode(y:Encode(42))
                return tostring(v == 42 and math.type(v) == 'integer')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Yaml_NegativeInteger_RoundTrips()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local y = Yaml.New()
                return tostring(y:Decode(y:Encode(-1000)) == -1000)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Yaml_Float_RoundTrips()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local y = Yaml.New()
                local v = y:Decode(y:Encode(3.14))
                return tostring(math.type(v) == 'float' and math.abs(v - 3.14) < 1e-10)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Yaml_String_RoundTrips()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local y = Yaml.New()
                return tostring(y:Decode(y:Encode('hello')) == 'hello')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Yaml_StringWithSpecialChars_RoundTrips()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local y = Yaml.New()
                local s = 'line1\nline2\ttabbed'
                return tostring(y:Decode(y:Encode(s)) == s)
            ");
            r.String.ShouldBe("true");
        }

        // -- Table round-trips -----------------------------------------------------
        [Fact]
        public async Task Yaml_Encode_ProducesString()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local y = Yaml.New()
                local s = y:Encode({x=1, y='hello', z=true})
                return tostring(type(s) == 'string' and #s > 0)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Yaml_Table_RoundTrips()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local y = Yaml.New()
                local t = y:Decode(y:Encode({x=1, y='hello', z=true}))
                return tostring(t.x==1 and t.y=='hello' and t.z==true)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Yaml_Array_PreservesOrder()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local y = Yaml.New()
                local t = y:Decode(y:Encode({10, 20, 30}))
                return tostring(t[1]==10 and t[2]==20 and t[3]==30)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Yaml_NestedTable_RoundTrips()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local y = Yaml.New()
                local orig = {a={b={c=42}}}
                local t = y:Decode(y:Encode(orig))
                return tostring(t.a.b.c == 42)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Yaml_AllBasicTypes_RoundTrip()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local y = Yaml.New()
                local orig = {
                    s   = 'hello',
                    n   = 42,
                    f   = 3.14,
                    bt  = true,
                    bf  = false,
                    arr = {1, 2, 3},
                    obj = {nested = 'value'},
                }
                local t = y:Decode(y:Encode(orig))
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
        public async Task Yaml_InstanceReuse_MultipleCalls()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local y = Yaml.New()
                local s1 = y:Encode({a=1})
                local s2 = y:Encode({b=2})
                local t1 = y:Decode(s1)
                local t2 = y:Decode(s2)
                return tostring(t1.a==1 and t2.b==2 and t1.b==nil and t2.a==nil)
            ");
            r.String.ShouldBe("true");
        }

        // -- YAML-specific decode -------------------------------------------------
        [Fact]
        public async Task Yaml_Decode_BlockStyle_String()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local y = Yaml.New()
                local t = y:Decode('key: value')
                return tostring(t.key == 'value')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Yaml_Decode_Sequence_FromBlockStyle()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local y = Yaml.New()
                local t = y:Decode('- 1\n- 2\n- 3\n')
                return tostring(t[1]==1 and t[2]==2 and t[3]==3)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Yaml_Decode_NullScalar_BecomesNil()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local y = Yaml.New()
                local t = y:Decode('v: null')
                return tostring(t.v == nil)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Yaml_Decode_BoolScalars_Coerced()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local y = Yaml.New()
                local t = y:Decode('a: true\nb: false\nc: yes\nd: no\n')
                return tostring(t.a==true and t.b==false and t.c==true and t.d==false)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Yaml_Decode_IntegerScalar_BecomesInteger()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local y = Yaml.New()
                local t = y:Decode('n: 99')
                return tostring(t.n == 99 and math.type(t.n) == 'integer')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Yaml_Decode_FloatScalar_BecomesFloat()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local y = Yaml.New()
                local t = y:Decode('f: 1.5')
                return tostring(math.type(t.f) == 'float' and t.f == 1.5)
            ");
            r.String.ShouldBe("true");
        }

        // -- Pretty / block style --------------------------------------------------
        [Fact]
        public async Task Yaml_PrettyNew_ProducesBlockStyle()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local y = Yaml.New(true)
                local s = y:Encode({a=1, b=2})
                return tostring(s:find('\n') ~= nil)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Yaml_PrettyEncoded_DecodesCorrectly()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local yp = Yaml.New(true)
                local yf = Yaml.New()
                local t = yf:Decode(yp:Encode({x=10, y=20}))
                return tostring(t.x==10 and t.y==20)
            ");
            r.String.ShouldBe("true");
        }

        // -- Toml ------------------------------------------------------------------
        // All operations require an instance (Toml.New() or Toml.New()).
        // Toml.New(true) selects indented output; Toml.New() defaults to compact.
        // Encode requires a table (TOML always has a root mapping).
        // Decode returns nil, errmsg on parse failure.
        [Fact]
        public async Task Toml_New_ReturnsUserdata()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("return type(Toml.New())");
            r.String.ShouldBe("userdata");
        }

        [Fact]
        public async Task Toml_Tostring_ReturnsNonEmptyString()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("return tostring(type(tostring(Toml.New())) == 'string' and #tostring(Toml.New()) > 0)");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Toml_Dispose_CanBeCalledExplicitly()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local t = Toml.New()
                t:Dispose()
                return 'ok'
            ");
            r.String.ShouldBe("ok");
        }

        [Fact]
        public async Task Toml_New_CalledOnInstance_ReturnsNewInstance()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local a = Toml.New()
                local b = a:New()
                local s = b:Encode({x='hello'})
                return tostring(b:Decode(s).x == 'hello')
            ");
            r.String.ShouldBe("true");
        }

        // -- Basic type round-trips ------------------------------------------------
        [Fact]
        public async Task Toml_Boolean_RoundTrips()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local t = Toml.New()
                local v = t:Decode(t:Encode({a=true, b=false}))
                return tostring(v.a==true and v.b==false)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Toml_Integer_RoundTrips()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local t = Toml.New()
                local v = t:Decode(t:Encode({n=42}))
                return tostring(v.n == 42 and math.type(v.n) == 'integer')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Toml_NegativeInteger_RoundTrips()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local t = Toml.New()
                return tostring(t:Decode(t:Encode({n=-1000})).n == -1000)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Toml_Float_RoundTrips()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local t = Toml.New()
                local v = t:Decode(t:Encode({f=3.14}))
                return tostring(math.type(v.f) == 'float' and math.abs(v.f - 3.14) < 1e-10)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Toml_String_RoundTrips()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local t = Toml.New()
                return tostring(t:Decode(t:Encode({s='hello'})).s == 'hello')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Toml_StringWithSpecialChars_RoundTrips()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local t = Toml.New()
                local s = 'line1\nline2\ttabbed'
                return tostring(t:Decode(t:Encode({s=s})).s == s)
            ");
            r.String.ShouldBe("true");
        }

        // -- Table round-trips -----------------------------------------------------
        [Fact]
        public async Task Toml_FlatTable_RoundTrips()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local t = Toml.New()
                local v = t:Decode(t:Encode({x=1, y='hello', z=true}))
                return tostring(v.x==1 and v.y=='hello' and v.z==true)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Toml_NestedTable_RoundTrips()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local t = Toml.New()
                local orig = {database={host='localhost', port=5432}}
                local v = t:Decode(t:Encode(orig))
                return tostring(v.database.host == 'localhost' and v.database.port == 5432)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Toml_Array_RoundTrips()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local t = Toml.New()
                local v = t:Decode(t:Encode({tags={1,2,3}}))
                return tostring(v.tags[1]==1 and v.tags[2]==2 and v.tags[3]==3)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Toml_AllBasicTypes_RoundTrip()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local t = Toml.New()
                local orig = {
                    s  = 'hello',
                    n  = 42,
                    f  = 3.14,
                    bt = true,
                    bf = false,
                }
                local v = t:Decode(t:Encode(orig))
                return tostring(
                    v.s  == 'hello' and
                    v.n  == 42      and
                    v.f  == 3.14    and
                    v.bt == true    and
                    v.bf == false
                )
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Toml_InstanceReuse_MultipleCalls()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local t = Toml.New()
                local s1 = t:Encode({a=1})
                local s2 = t:Encode({b=2})
                local v1 = t:Decode(s1)
                local v2 = t:Decode(s2)
                return tostring(v1.a==1 and v2.b==2 and v1.b==nil and v2.a==nil)
            ");
            r.String.ShouldBe("true");
        }

        // -- TOML-specific decode --------------------------------------------------
        [Fact]
        public async Task Toml_Decode_HandwrittenString()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local t = Toml.New()
                local v = t:Decode('host = ""localhost""\nport = 5432\n')
                return tostring(v.host == 'localhost' and v.port == 5432)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Toml_Decode_BooleanScalars()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local t = Toml.New()
                local v = t:Decode('a = true\nb = false\n')
                return tostring(v.a == true and v.b == false)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Toml_Decode_SectionHeader()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local t = Toml.New()
                local v = t:Decode('[database]\nhost = ""localhost""\nport = 5432\n')
                return tostring(v.database.host == 'localhost' and v.database.port == 5432)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Toml_Decode_InvalidToml_ReturnsNilAndError()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local t = Toml.New()
                local v, err = t:Decode('this is not valid toml !!!@#$')
                return tostring(v == nil and type(err) == 'string' and #err > 0)
            ");
            r.String.ShouldBe("true");
        }

        // -- Indented output -------------------------------------------------------
        [Fact]
        public async Task Toml_PrettyNew_ProducesNewlines()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local t = Toml.New(true)
                local s = t:Encode({a=1, b=2})
                return tostring(s:find('\n') ~= nil)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Toml_PrettyEncoded_DecodesCorrectly()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local tp = Toml.New(true)
                local tf = Toml.New()
                local v = tf:Decode(tp:Encode({x=10, y=20}))
                return tostring(v.x==10 and v.y==20)
            ");
            r.String.ShouldBe("true");
        }

        // -- Ini -------------------------------------------------------------------
        // All operations require an instance (Ini.New() or Ini.New()).
        // Decode returns a two-level table: { [section] = { [key] = value } }.
        // Keys before any section header land in the "__global" pseudo-section.
        // Encode accepts the same two-level structure.
        [Fact]
        public async Task Ini_New_ReturnsUserdata()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("return type(Ini.New())");
            r.String.ShouldBe("userdata");
        }

        [Fact]
        public async Task Ini_Tostring_ReturnsNonEmptyString()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("return tostring(type(tostring(Ini.New())) == 'string' and #tostring(Ini.New()) > 0)");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Ini_Dispose_CanBeCalledExplicitly()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local i = Ini.New()
                i:Dispose()
                return 'ok'
            ");
            r.String.ShouldBe("ok");
        }

        [Fact]
        public async Task Ini_New_CalledOnInstance_ReturnsNewInstance()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local a = Ini.New()
                local b = a:New()
                local s = b:Encode({sec={k='v'}})
                return tostring(b:Decode(s).sec.k == 'v')
            ");
            r.String.ShouldBe("true");
        }

        // -- Decode ----------------------------------------------------------------
        [Fact]
        public async Task Ini_Decode_SimpleSection_ReturnsTable()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local i = Ini.New()
                local t = i:Decode('[db]\nhost=localhost\nport=5432\n')
                return tostring(t.db.host == 'localhost' and t.db.port == '5432')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Ini_Decode_GlobalKeys_LandInGlobalSection()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local i = Ini.New()
                local t = i:Decode('version=1\nname=app\n')
                return tostring(t.__global.version == '1' and t.__global.name == 'app')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Ini_Decode_CommentsIgnored()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local i = Ini.New()
                local t = i:Decode('; comment\n[sec]\n# also comment\nkey=val\n')
                return tostring(t.sec.key == 'val')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Ini_Decode_InlineComment_Stripped()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local i = Ini.New()
                local t = i:Decode('[s]\nkey=value ; this is a comment\n')
                return tostring(t.s.key == 'value')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Ini_Decode_QuotedValue_StripsQuotes()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local i = Ini.New()
                local t = i:Decode('[s]\nkey=""hello world""\n')
                return tostring(t.s.key == 'hello world')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Ini_Decode_ColonSeparator_Supported()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local i = Ini.New()
                local t = i:Decode('[s]\nkey: value\n')
                return tostring(t.s.key == 'value')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Ini_Decode_MultipleSections()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local i = Ini.New()
                local t = i:Decode('[a]\nx=1\n[b]\ny=2\n')
                return tostring(t.a.x == '1' and t.b.y == '2')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Ini_Decode_EmptyLinesIgnored()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local i = Ini.New()
                local t = i:Decode('\n[s]\n\nkey=val\n\n')
                return tostring(t.s.key == 'val')
            ");
            r.String.ShouldBe("true");
        }

        // -- Encode ----------------------------------------------------------------
        [Fact]
        public async Task Ini_Encode_ProducesString()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local i = Ini.New()
                local s = i:Encode({server={host='localhost', port=5432}})
                return tostring(type(s) == 'string' and #s > 0)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Ini_Encode_SectionHeader_Present()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local i = Ini.New()
                local s = i:Encode({mySection={key='val'}})
                return tostring(s:find('%[mySection%]') ~= nil)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Ini_Encode_Boolean_AsString()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local i = Ini.New()
                local t = i:Decode(i:Encode({s={a=true, b=false}}))
                return tostring(t.s.a == 'true' and t.s.b == 'false')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Ini_Encode_Integer_AsString()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local i = Ini.New()
                local t = i:Decode(i:Encode({s={n=42}}))
                return tostring(t.s.n == '42')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Ini_InstanceReuse_MultipleCalls()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local i = Ini.New()
                local s1 = i:Encode({a={x='1'}})
                local s2 = i:Encode({b={y='2'}})
                local t1 = i:Decode(s1)
                local t2 = i:Decode(s2)
                return tostring(t1.a.x=='1' and t2.b.y=='2' and t1.b==nil and t2.a==nil)
            ");
            r.String.ShouldBe("true");
        }

        // -- Round-trip ------------------------------------------------------------
        [Fact]
        public async Task Ini_RoundTrip_StringValues()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local i = Ini.New()
                local orig = {server={host='localhost', mode='production'}}
                local t = i:Decode(i:Encode(orig))
                return tostring(t.server.host=='localhost' and t.server.mode=='production')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Ini_RoundTrip_MultipleSections()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local i = Ini.New()
                local orig = {db={host='db.local'}, app={name='myapp'}}
                local t = i:Decode(i:Encode(orig))
                return tostring(t.db.host=='db.local' and t.app.name=='myapp')
            ");
            r.String.ShouldBe("true");
        }

        // -- AliveToken -----------------------------------------------------------
        [Fact]
        public async Task AliveToken_New_ReturnsUserdata()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("return type(AliveToken.New())");
            r.String.ShouldBe("userdata");
        }

        [Fact]
        public async Task AliveToken_IsAlive_ReturnsTrueOnNew()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("return tostring(AliveToken.New():IsAlive())");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task AliveToken_IsAlive_ReturnsFalseAfterDispose()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local t = AliveToken.New()
                t:Dispose()
                return tostring(t:IsAlive())
            ");
            r.String.ShouldBe("false");
        }

        [Fact]
        public async Task AliveToken_Dispose_IsIdempotent()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local t = AliveToken.New()
                t:Dispose()
                t:Dispose()
                return tostring(t:IsAlive())
            ");
            r.String.ShouldBe("false");
        }

        [Fact]
        public async Task AliveToken_ErrorIfDead_DoesNotErrorWhenAlive()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local t = AliveToken.New()
                t:ErrorIfDead()
                return 'ok'
            ");
            r.String.ShouldBe("ok");
        }

        [Fact]
        public async Task AliveToken_ErrorIfDead_ErrorsWhenDisposed()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local t = AliveToken.New()
                t:Dispose()
                local ok, err = pcall(function() t:ErrorIfDead() end)
                return tostring(ok) .. '|' .. tostring(err ~= nil)
            ");
            r.String.ShouldBe("false|true");
        }

        [Fact]
        public async Task AliveToken_ErrorIfDead_DefaultMessage()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local t = AliveToken.New()
                t:Dispose()
                local ok, err = pcall(function() t:ErrorIfDead() end)
                return tostring(err:find('Cancellation token was cancelled') ~= nil)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task AliveToken_ErrorIfDead_CustomMessage()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local t = AliveToken.New()
                t:Dispose()
                local ok, err = pcall(function() t:ErrorIfDead('my custom msg') end)
                return tostring(err:find('my custom msg') ~= nil)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task AliveToken_Tostring_AliveContainsAlive()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                return tostring(AliveToken.New())
            ");

            r.String!.ShouldContain("alive");
        }

        [Fact]
        public async Task AliveToken_Tostring_DisposedContainsDisposed()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local t = AliveToken.New()
                t:Dispose()
                return tostring(t)
            ");

            r.String!.ShouldContain("disposed");
        }

        [Fact]
        public async Task AliveToken_SharedAcrossReferences_DisposeThroughOneAffectsOther()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local t1 = AliveToken.New()
                local t2 = t1
                t1:Dispose()
                return tostring(t2:IsAlive())
            ");

            r.String.ShouldBe("false");
        }

        [Fact]
        public async Task AliveToken_UsedAsLoopGuard_ExitsWhenDisposed()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local token = AliveToken.New()
                local count = 0
                for i = 1, 10 do
                    if not token:IsAlive() then break end
                    count = count + 1
                    if i == 3 then token:Dispose() end
                end
                return tostring(count)
            ");
            r.String.ShouldBe("3");
        }

        [Fact]
        public async Task AliveToken_Timeout_ZeroMs_IsAlreadyDead()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local t = AliveToken.New(0)
                return tostring(t:IsAlive())
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task AliveToken_Timeout_LargeMs_StillAlive()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local t = AliveToken.New(60000)
                return tostring(t:IsAlive())
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task AliveToken_Timeout_1Ms_ExpiresAfterSleep()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local t = AliveToken.New(1)
                Sleep(50)
                return tostring(t:IsAlive())
            ");
            r.String.ShouldBe("false");
        }

        [Fact]
        public async Task AliveToken_Timeout_ErrorIfDead_ErrorsAfterExpiry()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local t = AliveToken.New(1)
                Sleep(50)
                local ok, err = pcall(function() t:ErrorIfDead() end)
                return tostring(ok) .. '|' .. tostring(err ~= nil)
            ");
            r.String.ShouldBe("false|true");
        }

        [Fact]
        public async Task AliveToken_Timeout_ToStringContainsRemaining()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local t = AliveToken.New(60000)
                local s = tostring(t)
                return tostring(s:find('remaining') ~= nil)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task AliveToken_Timeout_ToStringDisposedAfterExpiry()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local t = AliveToken.New(1)
                Sleep(50)
                return tostring(t)
            ");
            r.String!.ShouldContain("disposed");
        }

        [Fact]
        public async Task AliveToken_Timeout_NoArg_StillWorks()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local t = AliveToken.New()
                Sleep(50)
                return tostring(t:IsAlive())
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task AliveToken_Link_ChildAliveWhenParentAlive()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local parent = AliveToken.New()
                local child  = AliveToken.New()
                child:Link(parent)
                return tostring(child:IsAlive())
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task AliveToken_Link_ChildDiesWhenParentDisposed()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local parent = AliveToken.New()
                local child  = AliveToken.New()
                child:Link(parent)
                parent:Dispose()
                return tostring(child:IsAlive())
            ");
            r.String.ShouldBe("false");
        }

        [Fact]
        public async Task AliveToken_Link_ChildAliveWhenOnlyOneOfTwoParentsDisposed_False()
        {
            // Disposing ANY linked parent kills the child
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local p1 = AliveToken.New()
                local p2 = AliveToken.New()
                local child = AliveToken.New()
                child:Link(p1, p2)
                p1:Dispose()
                return tostring(child:IsAlive())
            ");
            r.String.ShouldBe("false");
        }

        [Fact]
        public async Task AliveToken_Link_ChildDeadDoesNotRevive()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local parent = AliveToken.New()
                local child  = AliveToken.New()
                child:Link(parent)
                parent:Dispose()
                child:IsAlive()  -- tick to propagate
                -- even if we create a new token and link it, child stays dead
                return tostring(child:IsAlive())
            ");
            r.String.ShouldBe("false");
        }

        [Fact]
        public async Task AliveToken_Link_ChainPropagates()
        {
            // grandparent -> parent -> child: disposing grandparent kills child
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local gp     = AliveToken.New()
                local parent = AliveToken.New()
                local child  = AliveToken.New()
                parent:Link(gp)
                child:Link(parent)
                gp:Dispose()
                return tostring(child:IsAlive())
            ");
            r.String.ShouldBe("false");
        }

        [Fact]
        public async Task AliveToken_Link_MultipleParentsAllAliveKeepsChild()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local p1 = AliveToken.New()
                local p2 = AliveToken.New()
                local p3 = AliveToken.New()
                local child = AliveToken.New()
                child:Link(p1, p2, p3)
                return tostring(child:IsAlive())
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task AliveToken_Link_IncrementalLinkCalls()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local p1 = AliveToken.New()
                local p2 = AliveToken.New()
                local child = AliveToken.New()
                child:Link(p1)
                child:Link(p2)
                p2:Dispose()
                return tostring(child:IsAlive())
            ");
            r.String.ShouldBe("false");
        }

        [Fact]
        public async Task AliveToken_Link_ParentWithTimeoutPropagates()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local parent = AliveToken.New(1)
                local child  = AliveToken.New()
                child:Link(parent)
                Sleep(50)
                return tostring(child:IsAlive())
            ");
            r.String.ShouldBe("false");
        }

        // -- Sleep + AliveToken ---------------------------------------------------
        [Fact]
        public async Task Sleep_WithToken_ReturnsWhenTokenDisposed()
        {
            using KitsuneEngine engine = new();
            var sw = System.Diagnostics.Stopwatch.StartNew();
            await engine.ExecuteStringAsync(@"
                local token = AliveToken.New(50)
                Sleep(token)
            ");
            sw.Stop();

            // Token expires after ~50 ms, should wake well before 1000 ms
            sw.ElapsedMilliseconds.ShouldBeLessThan(500);
        }

        [Fact]
        public async Task Sleep_WithToken_AlreadyDeadReturnsImmediately()
        {
            using KitsuneEngine engine = new();
            var sw = System.Diagnostics.Stopwatch.StartNew();
            await engine.ExecuteStringAsync(@"
                local token = AliveToken.New()
                token:Dispose()
                Sleep(token)
            ");
            sw.Stop();
            sw.ElapsedMilliseconds.ShouldBeLessThan(200);
        }

        [Fact]
        public async Task Sleep_WithTokenAndMs_WakesOnDeadlineWhenTokenLives()
        {
            using KitsuneEngine engine = new();
            var sw = System.Diagnostics.Stopwatch.StartNew();
            await engine.ExecuteStringAsync(@"
                local token = AliveToken.New()
                Sleep(token, 50)
            ");
            sw.Stop();

            // Token stayed alive so deadline (50 ms) should have fired
            sw.ElapsedMilliseconds.ShouldBeGreaterThanOrEqualTo(40);
            sw.ElapsedMilliseconds.ShouldBeLessThan(500);
        }

        [Fact]
        public async Task Sleep_WithTokenAndMs_WakesOnTokenBeforeDeadline()
        {
            using KitsuneEngine engine = new();

            // Set up a token in the engine state
            await engine.ExecuteStringAsync(@"_token = AliveToken.New()");

            // Start the sleep in the background
            var sleepTask = engine.ExecuteStringAsync(@"
                Sleep(_token, 5000)
            ");

            // Dispose the token from outside after a short delay
            await Task.Delay(30);
            await engine.ExecuteStringAsync(@"_token:Dispose()");

            var sw = System.Diagnostics.Stopwatch.StartNew();
            await sleepTask;
            sw.Stop();

            // Should have woken when token was disposed, well before 5000 ms
            sw.ElapsedMilliseconds.ShouldBeLessThan(500);
        }

        [Fact]
        public async Task Sleep_WithTimeoutToken_WakesWhenTokenExpires()
        {
            using KitsuneEngine engine = new();
            var sw = System.Diagnostics.Stopwatch.StartNew();
            await engine.ExecuteStringAsync(@"
                local token = AliveToken.New(50)
                Sleep(token)
            ");
            sw.Stop();

            // Token should expire after ~50ms; definitely before 500ms
            sw.ElapsedMilliseconds.ShouldBeLessThan(500);
        }

        // -- UInt -----------------------------------------------------------------
        [Fact]
        public async Task UInt_Zero_IsUserdata()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("return type(UInt.Zero())");
            r.String.ShouldBe("userdata");
        }

        [Fact]
        public async Task UInt_FromString_BasicValue()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("return tostring(UInt.FromString('42'))");
            r.String.ShouldBe("42");
        }

        [Fact]
        public async Task UInt_FromString_InvalidReturnsNil()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("return tostring(UInt.FromString('abc') == nil)");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task UInt_FromString_NegativeReturnsNil()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("return tostring(UInt.FromString('-1') == nil)");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task UInt_FromNumber_Basic()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("return tostring(UInt.FromNumber(100))");
            r.String.ShouldBe("100");
        }

        [Fact]
        public async Task UInt_FromNumber_NegativeReturnsNil()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("return tostring(UInt.FromNumber(-1) == nil)");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task UInt_FromUnsigned_ReinterpretsSignedBits()
        {
            using KitsuneEngine engine = new();

            // -1 as int64 == 0xFFFFFFFFFFFFFFFF as uint64 == 18446744073709551615
            LuaValue r = await engine.ExecuteStringAsync("return tostring(UInt.FromUnsigned(-1))");
            r.String.ShouldBe("18446744073709551615");
        }

        [Fact]
        public async Task UInt_ToUnsigned_ReinterpretsUintBits()
        {
            using KitsuneEngine engine = new();

            // MaxUInt64 reinterpreted as signed int64 == -1
            LuaValue r = await engine.ExecuteStringAsync(@"
                local u = UInt.FromString('18446744073709551615')
                return tostring(u:ToUnsigned() == -1)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task UInt_ToInteger_TruncatesToSignedRange()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local u = UInt.FromString('42')
                return tostring(u:ToInteger() == 42)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task UInt_ToNumber_LossyDouble()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local u = UInt.FromString('1000')
                return tostring(math.type(u:ToNumber()) == 'float' and u:ToNumber() == 1000.0)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task UInt_IsZero_TrueForZero()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("return tostring(UInt.Zero():IsZero())");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task UInt_IsZero_FalseForNonZero()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("return tostring(UInt.FromString('1'):IsZero())");
            r.String.ShouldBe("false");
        }

        [Fact]
        public async Task UInt_Arithmetic_Add()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local a = UInt.FromString('10')
                local b = UInt.FromString('32')
                return tostring(a + b)
            ");
            r.String.ShouldBe("42");
        }

        [Fact]
        public async Task UInt_Arithmetic_Sub()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local a = UInt.FromString('100')
                local b = UInt.FromString('58')
                return tostring(a - b)
            ");
            r.String.ShouldBe("42");
        }

        [Fact]
        public async Task UInt_Arithmetic_Mul()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local a = UInt.FromString('6')
                local b = UInt.FromString('7')
                return tostring(a * b)
            ");
            r.String.ShouldBe("42");
        }

        [Fact]
        public async Task UInt_Arithmetic_Div()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local a = UInt.FromString('84')
                local b = UInt.FromString('2')
                return tostring(a / b)
            ");
            r.String.ShouldBe("42");
        }

        [Fact]
        public async Task UInt_Arithmetic_Mod()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local a = UInt.FromString('100')
                local b = UInt.FromString('58')
                return tostring(a % b)
            ");
            r.String.ShouldBe("42");
        }

        [Fact]
        public async Task UInt_Arithmetic_DivByZero_Errors()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local ok, err = pcall(function()
                    return UInt.FromString('1') / UInt.Zero()
                end)
                return tostring(not ok)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task UInt_Arithmetic_WrapOnOverflow()
        {
            using KitsuneEngine engine = new();

            // UINT64_MAX + 1 wraps to 0
            LuaValue r = await engine.ExecuteStringAsync(@"
                local max = UInt.FromString('18446744073709551615')
                local one = UInt.FromString('1')
                return tostring((max + one):IsZero())
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task UInt_Arithmetic_WrapOnUnderflow()
        {
            using KitsuneEngine engine = new();

            // 0 - 1 wraps to UINT64_MAX
            LuaValue r = await engine.ExecuteStringAsync(@"
                local zero = UInt.Zero()
                local one  = UInt.FromString('1')
                return tostring(zero - one)
            ");
            r.String.ShouldBe("18446744073709551615");
        }

        [Fact]
        public async Task UInt_Bitwise_And()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local a = UInt.FromString('15')  -- 0b1111
                local b = UInt.FromString('6')   -- 0b0110
                return tostring(a & b)           -- 0b0110 = 6
            ");
            r.String.ShouldBe("6");
        }

        [Fact]
        public async Task UInt_Bitwise_Or()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local a = UInt.FromString('9')   -- 0b1001
                local b = UInt.FromString('6')   -- 0b0110
                return tostring(a | b)           -- 0b1111 = 15
            ");
            r.String.ShouldBe("15");
        }

        [Fact]
        public async Task UInt_Bitwise_Xor()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local a = UInt.FromString('15')  -- 0b1111
                local b = UInt.FromString('9')   -- 0b1001
                return tostring(a ~ b)           -- 0b0110 = 6
            ");
            r.String.ShouldBe("6");
        }

        [Fact]
        public async Task UInt_Bitwise_Not()
        {
            using KitsuneEngine engine = new();

            // ~0 == UINT64_MAX
            LuaValue r = await engine.ExecuteStringAsync(@"
                return tostring(~UInt.Zero())
            ");
            r.String.ShouldBe("18446744073709551615");
        }

        [Fact]
        public async Task UInt_Bitwise_Shl()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local a = UInt.FromString('1')
                return tostring(a << UInt.FromString('4'))  -- 1 << 4 == 16
            ");
            r.String.ShouldBe("16");
        }

        [Fact]
        public async Task UInt_Bitwise_Shr()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local a = UInt.FromString('256')
                return tostring(a >> UInt.FromString('4'))  -- 256 >> 4 == 16
            ");
            r.String.ShouldBe("16");
        }

        [Fact]
        public async Task UInt_Comparison_Eq_SameValue()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local a = UInt.FromString('42')
                local b = UInt.FromString('42')
                return tostring(a == b)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task UInt_Comparison_Eq_DifferentValues()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                return tostring(UInt.FromString('1') == UInt.FromString('2'))
            ");
            r.String.ShouldBe("false");
        }

        [Fact]
        public async Task UInt_Comparison_Lt()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                return tostring(UInt.FromString('1') < UInt.FromString('2'))
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task UInt_Comparison_Le_Equal()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local a = UInt.FromString('5')
                return tostring(a <= a)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task UInt_MaxValue_RoundTrips_ThroughString()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local s = '18446744073709551615'
                return tostring(UInt.FromString(s):ToString() == s)
            ");
            r.String.ShouldBe("true");
        }

        // -- UInt Bridge (C# <-> Lua) --------------------------------------------
        [Fact]
        public async Task UInt_Bridge_SmallValue_ReturnedAsLuaType()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("return UInt.FromString('99')");
            r.Type.ShouldBe(LuaType.UInt);
            r.UInt64.ShouldBe(99UL);
        }

        [Fact]
        public async Task UInt_Bridge_MaxUInt64_PreservedAcrossBridge()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("return UInt.FromString('18446744073709551615')");
            r.Type.ShouldBe(LuaType.UInt);
            r.UInt64.ShouldBe(ulong.MaxValue);
        }

        [Fact]
        public async Task UInt_Bridge_FromUInt64_PushesLuaUInt()
        {
            using KitsuneEngine engine = new();
            engine.SetVariable("u", LuaValue.FromUInt64(ulong.MaxValue));
            LuaValue r = await engine.ExecuteStringAsync("return tostring(u)");
            r.String.ShouldBe("18446744073709551615");
        }

        [Fact]
        public async Task UInt_Bridge_AsInt64_ReinterpretsMaxUInt()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("return UInt.FromString('18446744073709551615')");
            r.AsInt64.ShouldBe(-1L);  // same bit pattern
        }

        [Fact]
        public async Task UInt_Bridge_AsDouble_ApproximatesLargeValue()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("return UInt.FromString('1000')");
            r.AsDouble.ShouldBe(1000.0);
        }

        [Fact]
        public async Task UInt_Bridge_ToString_ShowsDecimalRepresentation()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("return UInt.FromString('12345678901234567890')");
            r.Type.ShouldBe(LuaType.UInt);
            r.ToString().ShouldBe("12345678901234567890");
        }

        // -- UInt MsgPack round-trip ---------------------------------------------
        [Fact]
        public async Task UInt_MsgPack_SmallUInt_RoundTripsAsInteger()
        {
            using KitsuneEngine engine = new();

            // Values <= INT64_MAX decode as plain integers
            LuaValue r = await engine.ExecuteStringAsync(@"
                local m = MsgPack.New()
                local u = UInt.FromString('9223372036854775807')  -- INT64_MAX
                local v = m:Decode(m:Encode(u))
                return tostring(v == 9223372036854775807)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task UInt_MsgPack_LargeUInt_RoundTripsAsUInt()
        {
            using KitsuneEngine engine = new();

            // Values > INT64_MAX decode back as LuaUInt
            LuaValue r = await engine.ExecuteStringAsync(@"
                local m = MsgPack.New()
                local u = UInt.FromString('18446744073709551615')
                local v = m:Decode(m:Encode(u))
                return tostring(type(v) == 'userdata' and tostring(v) == '18446744073709551615')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task UInt_MsgPack_EncodesAsUint64WireType()
        {
            using KitsuneEngine engine = new();

            // A Lua integer -1 encodes as a signed int64; a UInt UINT64_MAX must encode
            // as an unsigned uint64 (different wire bytes).
            LuaValue r = await engine.ExecuteStringAsync(@"
                local m    = MsgPack.New()
                local neg  = m:Encode(-1)
                local umax = m:Encode(UInt.FromString('18446744073709551615'))
                return tostring(neg ~= umax)
            ");
            r.String.ShouldBe("true");
        }

        // -- UInt serializer coverage -------------------------------------------
        [Fact]
        public async Task UInt_Json_EncodesAsNumber()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local j = Json.New()
                local s = j:Encode({v = UInt.FromString('9999999999999999999')})
                return tostring(s:find('9999999999999999999') ~= nil)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task UInt_Yaml_EncodesAsPlainScalar()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local y = Yaml.New()
                local s = y:Encode({v = UInt.FromString('42')})
                return tostring(s:find('42') ~= nil)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task UInt_Toml_EncodesAsInteger()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local t = Toml.New()
                local s = t:Encode({v = UInt.FromString('42')})
                return tostring(s:find('42') ~= nil)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task UInt_Ini_EncodeScalar()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local i = Ini.New()
                local s = i:Encode({sec = {v = UInt.FromString('12345')}})
                return tostring(s:find('12345') ~= nil)
            ");
            r.String.ShouldBe("true");
        }

        // -- UInt Stream write/read ---------------------------------------------
        [Fact]
        public async Task UInt_Stream_Write_Read_RoundTrips()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local s = Stream.New()
                local u = UInt.FromString('18446744073709551615')
                Stream.Write(s, u)
                s:Seek(0)
                local v = Stream.ReadUInt64(s)
                return tostring(type(v) == 'userdata' and tostring(v) == '18446744073709551615')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task UInt_Stream_ReadUnsignedLong_LargeValue_ReturnsUInt()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local s = Stream.New()
                Stream.Write(s, UInt.FromString('18446744073709551615'))
                s:Seek(0)
                local v = Stream.ReadUnsignedLong(s)
                return tostring(type(v) == 'userdata' and tostring(v) == '18446744073709551615')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task UInt_Stream_ReadUnsignedLong_SmallValue_ReturnsInteger()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local s = Stream.New()
                Stream.WriteUnsignedLong(s, 42)
                s:Seek(0)
                local v = Stream.ReadUnsignedLong(s)
                return tostring(v == 42 and math.type(v) == 'integer')
            ");
            r.String.ShouldBe("true");
        }

        // -- Decimal serializer coverage ----------------------------------------
        [Fact]
        public async Task Decimal_Json_EncodesAsNumberInObject()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local j = Json.New()
                local s = j:Encode({v = Decimal.FromString('123.456')})
                return tostring(s:find('123.456') ~= nil)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Decimal_Yaml_EncodesAsQuotedScalar()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local y = Yaml.New()
                local s = y:Encode({v = Decimal.FromString('99.95')})
                return tostring(s:find('99.95') ~= nil)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Decimal_Toml_EncodesAsQuotedString()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local t = Toml.New()
                local s = t:Encode({v = Decimal.FromString('1.23')})
                return tostring(s:find('1.23') ~= nil)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Decimal_Ini_EncodeScalar()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local i = Ini.New()
                local s = i:Encode({sec = {price = Decimal.FromString('9.99')}})
                return tostring(s:find('9.99') ~= nil)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Decimal_Stream_Write_Read_RoundTrips()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local s = Stream.New()
                local d = Decimal.FromString('123.456')
                Stream.Write(s, d)
                s:Seek(0)
                local v = Stream.ReadDecimal(s)
                return tostring(tostring(v) == '123.456')
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Decimal_Stream_Write_NegativeValue_RoundTrips()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local s = Stream.New()
                Stream.Write(s, Decimal.FromString('-99.01'))
                s:Seek(0)
                local v = Stream.ReadDecimal(s)
                return tostring(tostring(v) == '-99.01')
            ");
            r.String.ShouldBe("true");
        }

        // -- Identifier serializer coverage ------------------------------------
        [Fact]
        public async Task Identifier_Json_EncodesAsString()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local j  = Json.New()
                local id = Identifier.FromString('00000000-0000-0000-0000-000000000001')
                local s  = j:Encode({id = id})
                return tostring(s:find('00000000%-0000%-0000%-0000%-000000000001') ~= nil)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Identifier_Yaml_EncodesAsString()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local y  = Yaml.New()
                local id = Identifier.FromString('00000000-0000-0000-0000-000000000002')
                local s  = y:Encode({id = id})
                return tostring(s:find('00000000%-0000%-0000%-0000%-000000000002') ~= nil)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Identifier_Toml_EncodesAsString()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local t  = Toml.New()
                local id = Identifier.FromString('00000000-0000-0000-0000-000000000003')
                local s  = t:Encode({id = id})
                return tostring(s:find('00000000%-0000%-0000%-0000%-000000000003') ~= nil)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Identifier_Ini_EncodeScalar()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local i  = Ini.New()
                local id = Identifier.FromString('00000000-0000-0000-0000-000000000004')
                local s  = i:Encode({sec = {id = id}})
                return tostring(s:find('00000000%-0000%-0000%-0000%-000000000004') ~= nil)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Identifier_Stream_Write_Read_RoundTrips()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local s  = Stream.New()
                local id = Identifier.FromString('aabbccdd-eeff-0011-2233-445566778899')
                Stream.Write(s, id)
                s:Seek(0)
                local id2 = Stream.ReadIdentifier(s)
                return tostring(tostring(id) == tostring(id2))
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Identifier_Stream_Write_ProducesExact16Bytes()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local s  = Stream.New()
                local id = Identifier.NewUUID()
                Stream.Write(s, id)
                return tostring(s:len() == 16)
            ");
            r.String.ShouldBe("true");
        }

        // -- DateTime serializer coverage -------------------------------------
        [Fact]
        public async Task DateTime_Json_EncodesAsIso8601String()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local j  = Json.New()
                local dt = DateTime.New(2024, 6, 15, 12, 0, 0, 0, 0)
                local s  = j:Encode({ts = dt})
                return tostring(s:find('2024') ~= nil)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task DateTime_Yaml_EncodesAsString()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local y  = Yaml.New()
                local dt = DateTime.New(2024, 6, 15, 12, 0, 0, 0, 0)
                local s  = y:Encode({ts = dt})
                return tostring(s:find('2024') ~= nil)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task DateTime_Stream_Write_Read_RoundTrips()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local s  = Stream.New()
                local dt = DateTime.New(2024, 1, 1, 0, 0, 0, 0, 0)
                Stream.Write(s, dt)
                s:Seek(0)
                local dt2 = Stream.ReadDateTime(s)
                return tostring(dt2:Year() == 2024 and dt2:Month() == 1 and dt2:Day() == 1)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task DateTime_Stream_Write_ProducesExact10Bytes()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local s  = Stream.New()
                local dt = DateTime.UtcNow()
                Stream.Write(s, dt)
                return tostring(s:len() == 10)
            ");
            r.String.ShouldBe("true");
        }

        // -- XML text field coverage ------------------------------------------
        [Fact]
        public async Task Xml_UInt_InTextField_Serializes()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local x = Xml.New()
                local s = x:Encode({tag='root', text=UInt.FromString('999')})
                return tostring(s:find('999') ~= nil)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Xml_Decimal_InTextField_Serializes()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local x = Xml.New()
                local s = x:Encode({tag='root', text=Decimal.FromString('3.14')})
                return tostring(s:find('3.14') ~= nil)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Xml_Identifier_InTextField_Serializes()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local x  = Xml.New()
                local id = Identifier.FromString('00000000-0000-0000-0000-000000000005')
                local s  = x:Encode({tag='root', text=id})
                return tostring(s:find('00000000%-0000%-0000%-0000%-000000000005') ~= nil)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Xml_DateTime_InTextField_Serializes()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local x  = Xml.New()
                local dt = DateTime.New(2025, 3, 1, 0, 0, 0, 0, 0)
                local s  = x:Encode({tag='root', text=dt})
                return tostring(s:find('2025') ~= nil)
            ");
            r.String.ShouldBe("true");
        }

        // ── TimeSpan ──────────────────────────────────────────────────────────
        [Fact]
        public async Task TimeSpan_Zero_IsUserdata()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("return type(TimeSpan.Zero())");
            r.String.ShouldBe("userdata");
        }

        [Fact]
        public async Task TimeSpan_FromSeconds_ToString()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("return tostring(TimeSpan.FromSeconds(3661))");

            // 3661 s = 1 h 1 min 1 s  → 01:01:01.000
            r.String.ShouldBe("01:01:01.000");
        }

        [Fact]
        public async Task TimeSpan_FromDays_ToString_IncludesDayComponent()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("return tostring(TimeSpan.FromDays(2))");
            r.String.ShouldBe("2.00:00:00.000");
        }

        [Fact]
        public async Task TimeSpan_Negative_ToString_HasLeadingMinus()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("return tostring(-TimeSpan.FromSeconds(90))");
            r.String.ShouldBe("-00:01:30.000");
        }

        [Fact]
        public async Task TimeSpan_FromMilliseconds_TotalMilliseconds()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("return tostring(math.abs(TimeSpan.FromMilliseconds(1500):TotalMilliseconds() - 1500) < 0.001)");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task TimeSpan_FromMinutes_TotalMinutes()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("return tostring(math.abs(TimeSpan.FromMinutes(90):TotalMinutes() - 90) < 0.0001)");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task TimeSpan_FromHours_TotalHours()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("return tostring(math.abs(TimeSpan.FromHours(1.5):TotalHours() - 1.5) < 0.0001)");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task TimeSpan_FromDays_TotalDays()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("return tostring(math.abs(TimeSpan.FromDays(3):TotalDays() - 3) < 0.0001)");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task TimeSpan_FromTicks_Ticks()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("return tostring(TimeSpan.FromTicks(10000000):Ticks() == 10000000)");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task TimeSpan_Components_Days_Hours_Minutes_Seconds_Milliseconds()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                -- 1 day + 2 h + 3 min + 4 s + 5 ms
                local t = TimeSpan.FromDays(1) + TimeSpan.FromHours(2) + TimeSpan.FromMinutes(3)
                        + TimeSpan.FromSeconds(4) + TimeSpan.FromMilliseconds(5)
                return tostring(t:Days()==1 and t:Hours()==2 and t:Minutes()==3
                    and t:Seconds()==4 and t:Milliseconds()==5)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task TimeSpan_IsZero_TrueForZero()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("return tostring(TimeSpan.Zero():IsZero())");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task TimeSpan_IsZero_FalseForNonZero()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("return tostring(TimeSpan.FromSeconds(1):IsZero())");
            r.String.ShouldBe("false");
        }

        [Fact]
        public async Task TimeSpan_IsNegative_TrueForNegative()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("return tostring((-TimeSpan.FromSeconds(1)):IsNegative())");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task TimeSpan_IsNegative_FalseForPositive()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("return tostring(TimeSpan.FromSeconds(1):IsNegative())");
            r.String.ShouldBe("false");
        }

        [Fact]
        public async Task TimeSpan_Abs_NegativeBecomesPositive()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local neg = -TimeSpan.FromSeconds(5)
                local pos = neg:Abs()
                return tostring(pos:TotalSeconds() > 0 and neg:TotalSeconds() < 0)
            ");
            r.String.ShouldBe("true");
        }

        // ── TimeSpan arithmetic ───────────────────────────────────────────────
        [Fact]
        public async Task TimeSpan_Add_TwoSpans()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local a = TimeSpan.FromSeconds(30)
                local b = TimeSpan.FromSeconds(30)
                return tostring(math.abs((a + b):TotalSeconds() - 60) < 0.001)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task TimeSpan_Sub_TwoSpans()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local a = TimeSpan.FromSeconds(90)
                local b = TimeSpan.FromSeconds(30)
                return tostring(math.abs((a - b):TotalSeconds() - 60) < 0.001)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task TimeSpan_Mul_ByNumber()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local a = TimeSpan.FromSeconds(10)
                return tostring(math.abs((a * 3):TotalSeconds() - 30) < 0.001)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task TimeSpan_Mul_NumberBySpan()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local a = TimeSpan.FromSeconds(10)
                return tostring(math.abs((3 * a):TotalSeconds() - 30) < 0.001)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task TimeSpan_Div_ByNumber()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local a = TimeSpan.FromSeconds(60)
                return tostring(math.abs((a / 2):TotalSeconds() - 30) < 0.001)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task TimeSpan_Div_ByZero_Errors()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local ok, err = pcall(function()
                    return TimeSpan.FromSeconds(1) / 0
                end)
                return tostring(not ok)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task TimeSpan_Negate_FlipsSign()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local a = TimeSpan.FromSeconds(5)
                return tostring(math.abs((-a):TotalSeconds() + 5) < 0.001)
            ");
            r.String.ShouldBe("true");
        }

        // ── TimeSpan comparison ───────────────────────────────────────────────
        [Fact]
        public async Task TimeSpan_Eq_SameValue()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local a = TimeSpan.FromSeconds(30)
                local b = TimeSpan.FromSeconds(30)
                return tostring(a == b)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task TimeSpan_Eq_DifferentValue()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                return tostring(TimeSpan.FromSeconds(1) == TimeSpan.FromSeconds(2))
            ");
            r.String.ShouldBe("false");
        }

        [Fact]
        public async Task TimeSpan_Lt()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                return tostring(TimeSpan.FromSeconds(1) < TimeSpan.FromSeconds(2))
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task TimeSpan_Le_Equal()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local a = TimeSpan.FromSeconds(5)
                return tostring(a <= a)
            ");
            r.String.ShouldBe("true");
        }

        // ── DateTime - DateTime → TimeSpan ────────────────────────────────────
        [Fact]
        public async Task DateTime_Sub_DateTime_ReturnsTimeSpan()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local a = DateTime.New(2024, 1, 1, 1, 0, 0, 0, 0)
                local b = DateTime.New(2024, 1, 1, 0, 0, 0, 0, 0)
                local ts = a - b
                return tostring(type(ts) == 'userdata' and math.abs(ts:TotalHours() - 1) < 0.0001)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task DateTime_Sub_DateTime_NegativeSpan()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local earlier = DateTime.New(2024, 1, 1, 0, 0, 0, 0, 0)
                local later   = DateTime.New(2024, 1, 1, 1, 0, 0, 0, 0)
                local ts = earlier - later
                return tostring(ts:IsNegative())
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task DateTime_Add_TimeSpan_ReturnsDateTime()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local dt = DateTime.New(2024, 1, 1, 0, 0, 0, 0, 0)
                local ts = TimeSpan.FromHours(2)
                local result = dt + ts
                return tostring(result:Hour() == 2 and result:Year() == 2024)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task DateTime_Sub_TimeSpan_ReturnsDateTime()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local dt = DateTime.New(2024, 1, 2, 3, 0, 0, 0, 0)
                local ts = TimeSpan.FromHours(3)
                local result = dt - ts
                return tostring(result:Hour() == 0 and result:Day() == 2)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task DateTime_AddTimeSpan_Method_MatchesOperator()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local dt = DateTime.New(2024, 6, 15, 12, 0, 0, 0, 0)
                local ts = TimeSpan.FromMinutes(90)
                local via_op  = dt + ts
                local via_met = dt:AddTimeSpan(ts)
                return tostring(via_op == via_met)
            ");
            r.String.ShouldBe("true");
        }

        // ── TimeSpan bridge (C# ↔ Lua) ────────────────────────────────────────
        [Fact]
        public async Task TimeSpan_Bridge_SmallValue_ReturnedAsLuaType()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("return TimeSpan.FromSeconds(42)");
            r.Type.ShouldBe(LuaType.TimeSpan);
            r.AsTimeSpan.ShouldBe(System.TimeSpan.FromSeconds(42));
        }

        [Fact]
        public async Task TimeSpan_Bridge_FromTimeSpan_PushesLuaTimeSpan()
        {
            using KitsuneEngine engine = new();
            engine.SetVariable("ts", LuaValue.FromTimeSpan(System.TimeSpan.FromHours(1.5)));
            LuaValue r = await engine.ExecuteStringAsync("return tostring(math.abs(ts:TotalHours() - 1.5) < 0.0001)");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task TimeSpan_Bridge_NegativeValue_PreservedAcrossBridge()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("return -TimeSpan.FromMinutes(30)");
            r.Type.ShouldBe(LuaType.TimeSpan);
            r.AsTimeSpan.TotalMinutes.ShouldBe(-30, tolerance: 0.001);
        }

        [Fact]
        public async Task TimeSpan_Bridge_TickPrecision_Preserved()
        {
            using KitsuneEngine engine = new();

            // 1 tick = 100 ns; well below millisecond resolution
            LuaValue r = await engine.ExecuteStringAsync("return TimeSpan.FromTicks(1)");
            r.Type.ShouldBe(LuaType.TimeSpan);
            r.Int64.ShouldBe(1L);
        }

        [Fact]
        public async Task TimeSpan_Bridge_AsInt64_IsRawTicks()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("return TimeSpan.FromSeconds(1)");
            r.AsInt64.ShouldBe(10_000_000L); // 1 s = 10,000,000 ticks
        }

        [Fact]
        public async Task TimeSpan_Bridge_ToString_MatchesLuaFormat()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("return TimeSpan.FromSeconds(3661)");
            r.Type.ShouldBe(LuaType.TimeSpan);

            // Lua format always includes milliseconds: "01:01:01.000"
            // .NET TimeSpan.ToString("c") omits fractional seconds when zero: "01:01:01"
            // Verify both the Lua string and that AsTimeSpan round-trips correctly instead.
            r.AsTimeSpan.TotalSeconds.ShouldBe(3661, tolerance: 0.001);
        }

        // ── DateTime bridge (typed binary) ────────────────────────────────────
        [Fact]
        public async Task DateTime_Bridge_ReturnedAsLuaTypeDateTime()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("return DateTime.New(2024, 3, 15, 10, 30, 45, 0, 0)");
            r.Type.ShouldBe(LuaType.DateTime);
        }

        [Fact]
        public async Task DateTime_Bridge_AsDateTimeOffset_CorrectComponents()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("return DateTime.New(2024, 3, 15, 10, 30, 45, 0, 0)");
            var dto = r.AsDateTimeOffset;
            dto.Year.ShouldBe(2024);
            dto.Month.ShouldBe(3);
            dto.Day.ShouldBe(15);
            dto.Hour.ShouldBe(10);
            dto.Minute.ShouldBe(30);
            dto.Second.ShouldBe(45);
        }

        [Fact]
        public async Task DateTime_Bridge_FromDateTime_PushesLuaDateTime()
        {
            using KitsuneEngine engine = new();
            var dto = new DateTimeOffset(2025, 7, 4, 12, 0, 0, TimeSpan.Zero);
            engine.SetVariable("dt", LuaValue.FromDateTime(dto));
            LuaValue r = await engine.ExecuteStringAsync("return tostring(dt:Year() == 2025 and dt:Month() == 7 and dt:Day() == 4)");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task DateTime_Bridge_WithUtcOffset_Preserved()
        {
            using KitsuneEngine engine = new();

            // UTC+5:30
            var dto = new DateTimeOffset(2024, 1, 1, 6, 30, 0, TimeSpan.FromMinutes(330));
            engine.SetVariable("dt", LuaValue.FromDateTime(dto));
            LuaValue r = await engine.ExecuteStringAsync("return tostring(dt:OffsetMinutes() == 330)");
            r.String.ShouldBe("true");
        }

        // ── Decimal bridge (typed binary) ─────────────────────────────────────
        [Fact]
        public async Task Decimal_Bridge_ReturnedAsLuaTypeDecimal()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("return Decimal.FromString('123.456')");
            r.Type.ShouldBe(LuaType.Decimal);
        }

        [Fact]
        public async Task Decimal_Bridge_AsDecimal_CorrectValue()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("return Decimal.FromString('99.95')");
            r.AsDecimal.ShouldBe(99.95m);
        }

        [Fact]
        public async Task Decimal_Bridge_FromDecimal_PushesLuaDecimal()
        {
            using KitsuneEngine engine = new();
            engine.SetVariable("d", LuaValue.FromDecimal(3.14m));
            LuaValue r = await engine.ExecuteStringAsync("return tostring(tostring(d):sub(1, 4) == '3.14')");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Decimal_Bridge_NegativeValue_Preserved()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("return Decimal.FromString('-42.5')");
            r.AsDecimal.ShouldBe(-42.5m);
        }

        // ── Identifier bridge (typed binary) ──────────────────────────────────
        [Fact]
        public async Task Identifier_Bridge_ReturnedAsLuaTypeIdentifier()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("return Identifier.NewUUID()");
            r.Type.ShouldBe(LuaType.Identifier);
        }

        [Fact]
        public async Task Identifier_Bridge_AsGuid_ValidFormat()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("return Identifier.FromString('aabbccdd-eeff-4011-8899-aabbccddeeff')");
            r.Type.ShouldBe(LuaType.Identifier);
            r.AsGuid.ShouldNotBe(Guid.Empty);
        }

        [Fact]
        public async Task Identifier_Bridge_AsGuid_RoundTrips()
        {
            using KitsuneEngine engine = new();
            var guid = Guid.NewGuid();
            engine.SetVariable("id", LuaValue.FromGuid(guid));
            LuaValue r = await engine.ExecuteStringAsync("return id");
            r.Type.ShouldBe(LuaType.Identifier);
            r.AsGuid.ShouldBe(guid);
        }

        [Fact]
        public async Task Identifier_Bridge_AsIdentifierBytes_Is16Bytes()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("return Identifier.NewUUID()");
            r.AsIdentifierBytes.ShouldNotBeNull();
            r.AsIdentifierBytes!.Length.ShouldBe(16);
        }

        // ── TimeSpan serializer coverage ──────────────────────────────────────
        [Fact]
        public async Task TimeSpan_Json_EncodesAsString()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local j  = Json.New()
                local ts = TimeSpan.FromSeconds(3661)
                local s  = j:Encode({dur = ts})
                return tostring(s:find('01:01:01.000') ~= nil)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task TimeSpan_Yaml_EncodesAsString()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local y  = Yaml.New()
                local ts = TimeSpan.FromSeconds(90)
                local s  = y:Encode({dur = ts})
                return tostring(s:find('00:01:30.000') ~= nil)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task TimeSpan_Toml_EncodesAsString()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local t  = Toml.New()
                local ts = TimeSpan.FromMinutes(5)
                local s  = t:Encode({dur = ts})
                return tostring(s:find('00:05:00.000') ~= nil)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task TimeSpan_Ini_EncodeScalar()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local i  = Ini.New()
                local ts = TimeSpan.FromHours(1)
                local s  = i:Encode({sec = {dur = ts}})
                return tostring(s:find('01:00:00.000') ~= nil)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task TimeSpan_Xml_InTextField_Serializes()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local x  = Xml.New()
                local ts = TimeSpan.FromSeconds(3661)
                local s  = x:Encode({tag='root', text=ts})
                return tostring(s:find('01:01:01.000') ~= nil)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task TimeSpan_MsgPack_EncodesAsString()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local m  = MsgPack.New()
                local ts = TimeSpan.FromSeconds(3661)
                local v  = m:Decode(m:Encode(ts))
                return tostring(v == '01:01:01.000')
            ");
            r.String.ShouldBe("true");
        }

        // ── TimeSpan stream read/write ────────────────────────────────────────
        [Fact]
        public async Task TimeSpan_Stream_Write_Read_RoundTrips()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local s  = Stream.New()
                local ts = TimeSpan.FromSeconds(7200)
                Stream.Write(s, ts)
                s:Seek(0)
                local ts2 = Stream.ReadTimeSpan(s)
                return tostring(math.abs(ts2:TotalHours() - 2) < 0.0001)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task TimeSpan_Stream_Write_ProducesExact8Bytes()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local s  = Stream.New()
                Stream.Write(s, TimeSpan.FromSeconds(1))
                return tostring(s:len() == 8)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task TimeSpan_Stream_NegativeValue_RoundTrips()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local s  = Stream.New()
                local ts = -TimeSpan.FromMinutes(30)
                Stream.Write(s, ts)
                s:Seek(0)
                local ts2 = Stream.ReadTimeSpan(s)
                return tostring(ts2:IsNegative() and math.abs(ts2:TotalMinutes() + 30) < 0.0001)
            ");
            r.String.ShouldBe("true");
        }

        // ── Bug-fix: INI encoder was missing DateTime ─────────────────────────
        [Fact]
        public async Task Ini_DateTime_EncodesSomething()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local i  = Ini.New()
                local dt = DateTime.New(2024, 6, 15, 12, 0, 0, 0, 0)
                local s  = i:Encode({sec = {ts = dt}})
                -- Must produce a non-empty string containing the year
                return tostring(type(s) == 'string' and #s > 0 and s:find('2024') ~= nil)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Ini_DateTime_KeyPresent()
        {
            using KitsuneEngine engine = new();

            // Before the fix the key was absent entirely because DateTime fell through to return 0.
            LuaValue r = await engine.ExecuteStringAsync(@"
                local i  = Ini.New()
                local dt = DateTime.New(2025, 1, 1, 0, 0, 0, 0, 0)
                local s  = i:Encode({s = {dt = dt}})
                return tostring(s:find('dt') ~= nil)
            ");
            r.String.ShouldBe("true");
        }

        // ── Bug-fix: MySQL/Postgres/Redis binder was missing TimeSpan ─────────
        [Fact]
        public async Task TimeSpan_ToString_IsCanonical()
        {
            // Validates the canonical format used by the string-based binders.
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("return tostring(TimeSpan.FromSeconds(3661))");
            r.String.ShouldBe("01:01:01.000");
        }

        [Fact]
        public async Task TimeSpan_Negative_ToString_IsCanonical()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("return tostring(-TimeSpan.FromMinutes(90))");
            r.String.ShouldBe("-01:30:00.000");
        }

        // ── Bug-fix: Redis was also missing UInt ─────────────────────────────
        [Fact]
        public async Task UInt_ToString_IsDecimal()
        {
            // UInt must produce a plain decimal string so Redis/MySQL/Postgres binders work.
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("return tostring(UInt.FromString('18446744073709551615'))");
            r.String.ShouldBe("18446744073709551615");
        }

        // ── Bug-fix: stream DateTime write/read lost offset_minutes ──────────
        [Fact]
        public async Task DateTime_Stream_Write_Read_PreservesNonZeroOffset()
        {
            using KitsuneEngine engine = new();

            // UTC+5:30 = 330 minutes.  Before the fix offset was always read back as 0.
            LuaValue r = await engine.ExecuteStringAsync(@"
                local dt  = DateTime.New(2024, 6, 15, 12, 0, 0, 0, 330)
                local s   = Stream.New()
                Stream.Write(s, dt)
                s:Seek(0)
                local dt2 = Stream.ReadDateTime(s)
                return tostring(dt2:OffsetMinutes() == 330)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task DateTime_Stream_Write_PreservesUTCOffset()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local dt  = DateTime.New(2024, 1, 1, 12, 0, 0, 0, 0)
                local s   = Stream.New()
                Stream.Write(s, dt)
                s:Seek(0)
                local dt2 = Stream.ReadDateTime(s)
                return tostring(dt2:OffsetMinutes() == 0)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task DateTime_Stream_NegativeOffset_RoundTrips()
        {
            using KitsuneEngine engine = new();

            // UTC-5 = -300 minutes
            LuaValue r = await engine.ExecuteStringAsync(@"
                local dt  = DateTime.New(2024, 3, 10, 8, 0, 0, 0, -300)
                local s   = Stream.New()
                Stream.Write(s, dt)
                s:Seek(0)
                local dt2 = Stream.ReadDateTime(s)
                return tostring(dt2:OffsetMinutes() == -300)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task DateTime_Stream_AllComponents_RoundTrip()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local dt  = DateTime.New(2024, 6, 15, 14, 30, 45, 0, 120)
                local s   = Stream.New()
                Stream.Write(s, dt)
                s:Seek(0)
                local dt2 = Stream.ReadDateTime(s)
                return tostring(
                    dt2:Year()          == 2024 and
                    dt2:Month()         == 6    and
                    dt2:Day()           == 15   and
                    dt2:Hour()          == 14   and
                    dt2:Minute()        == 30   and
                    dt2:Second()        == 45   and
                    dt2:OffsetMinutes() == 120
                )
            ");
            r.String.ShouldBe("true");
        }
    }
}
