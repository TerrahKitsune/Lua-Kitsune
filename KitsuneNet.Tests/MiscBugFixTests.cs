using KitsuneNet;
using Shouldly;
using Xunit;

namespace KitsuneNet.Tests;

/// <summary>
/// Regression tests for small fixes in Archive, Base64, Aes, the hash modules,
/// GetMemory, Dns and Process.
/// </summary>
[Collection("KitsuneSequential")]
public sealed class MiscBugFixTests
{
    // Archive
    [Theory]
    [MemberData(nameof(ArchiveTests.ArchiveFiles), MemberType = typeof(ArchiveTests))]
    public async Task Archive_SetEntry_AfterFileDeleted_ReturnsErrorAndGcIsSafe(string path)
    {
        using KitsuneEngine engine = new();

        // SetEntry reopens the file; when that fails the handle must not be
        // freed twice (once in SetEntry, again in __gc).
        LuaValue r = await engine.ExecuteStringAsync("""
            local src = (...)
            local ext = src:match('%.%w+$')
            local copy = FileSystem.GetTempFileName() .. '_kitsune_setentry_missing' .. ext
            assert(FileSystem.Copy(src, copy, true), 'copy failed')
            local arc, err = Archive.OpenRead(copy)
            assert(arc, err or 'failed to open archive')
            FileSystem.Delete(copy)
            local name, msg = Archive.SetEntry(arc, 1)
            local name2, msg2 = Archive.SetEntry(arc, 1)
            local readOk = pcall(Archive.ReadAll, arc)
            arc = nil
            collectgarbage('collect')
            collectgarbage('collect')
            return type(name) .. '|' .. type(msg) .. '|' .. type(name2) .. '|' .. type(msg2)
                .. '|' .. tostring(readOk)
            """,
            args: [LuaValue.FromString(path)]);
        r.String.ShouldBe("nil|string|nil|string|false");
    }

    // Base64
    [Fact]
    public async Task Base64_Decode_EmptyInvalidAndRoundTrip_ReturnExpectedValues()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(@"
            local d = Base64.Decode('')
            return '[' .. tostring(d) .. ']|' .. tostring(Base64.Decode('abc')) .. '|'
                .. tostring(Base64.Decode(Base64.Encode('hi')))
        ");
        r.String.ShouldBe("[]|nil|hi");
    }

    // Aes
    [Fact]
    public async Task Aes_SetIV_ShortIV_IsZeroPaddedLikeNew()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(@"
            local key   = string.rep('k', 32)
            local iv    = 'abc'
            local plain = 'short iv round trip test'
            local enc1  = Aes.New(key, iv):Encrypt(plain)
            local enc0  = Aes.New(key, iv .. string.rep('\0', 13)):Encrypt(plain)
            local ctx   = Aes.New(key, string.rep('z', 16))
            ctx:SetIV(iv)
            local enc2  = ctx:Encrypt(plain)
            ctx:SetIV(iv)
            local dec   = ctx:Decrypt(enc2)
            local tooLong = pcall(ctx.SetIV, ctx, string.rep('x', 17))
            return tostring(enc1 == enc0) .. '|' .. tostring(enc1 == enc2) .. '|' .. tostring(dec)
                .. '|' .. tostring(tooLong)
        ");
        r.String.ShouldBe("true|true|short iv round trip test|false");
    }

    // Hashing
    [Fact]
    public async Task MD5_FinishTwice_ReturnsSameDigest()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(@"
            local h = MD5.New(); h:Update('abc')
            local a, ra = h:Finish()
            local b, rb = h:Finish()
            local okUpdate, err = pcall(h.Update, h, 'more')
            return tostring(a) .. '|' .. tostring(b) .. '|' .. tostring(ra == rb) .. '|' .. #ra
                .. '|' .. tostring(okUpdate) .. '|' .. tostring(tostring(err):match('already finished'))
        ");
        r.String.ShouldBe("900150983cd24fb0d6963f7d28e17f72|900150983cd24fb0d6963f7d28e17f72|true|16|false|already finished");
    }

    [Fact]
    public async Task SHA256_FinishTwice_ReturnsSameDigest()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(@"
            local h = SHA256.New(); h:Update('abc')
            local a, ra = h:Finish()
            local b, rb = h:Finish()
            local okUpdate, err = pcall(h.Update, h, 'more')
            return tostring(a) .. '|' .. tostring(b) .. '|' .. tostring(ra == rb) .. '|' .. #ra
                .. '|' .. tostring(okUpdate) .. '|' .. tostring(tostring(err):match('already finished'))
        ");
        r.String.ShouldBe(
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad|"
            + "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad|true|32|false|already finished");
    }

    [Fact]
    public async Task SHA1_FinishTwice_ReturnsSameDigest()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(@"
            local h = SHA1.New(); h:Update('abc')
            local a, ra = h:Finish()
            local b, rb = h:Finish()
            return tostring(a) .. '|' .. tostring(b) .. '|' .. tostring(ra == rb) .. '|' .. #ra
        ");
        r.String.ShouldBe(
            "a9993e364706816aba3e25717850c26c9cd0d89d|a9993e364706816aba3e25717850c26c9cd0d89d|true|20");
    }

    // GetMemory
    [Fact]
    public async Task GetMemory_ReturnsPositiveInteger()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(
            "local m = GetMemory(); return tostring(math.type(m)) .. '|' .. tostring(m > 0)");
        r.String.ShouldBe("integer|true");
    }

    // Dns
    [Fact]
    public async Task Dns_IPv6Only_ReturnsNil()
    {
        using KitsuneEngine engine = new();

        // '::1' resolves to an IPv6 address only, so there is no IPv4 result.
        LuaValue r = await engine.ExecuteStringAsync(@"
            local ip = Dns('::1')
            return type(ip)
        ");
        r.String.ShouldBe("nil");
    }

    // Process
    [WindowsOnlyFact]
    public async Task Process_Priority_NoArgument_ReturnsPriorityClass()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(@"
            local p = Process.Open()
            local prio = p:Priority()
            return tostring(math.type(prio)) .. '|' .. tostring(prio > 1)
        ");
        r.String.ShouldBe("integer|true");
    }

    [WindowsOnlyFact]
    public async Task Process_Affinity_ReturnsIntegerMasks()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(@"
            local p = Process.Open()
            local procMask, sysMask = p:Affinity()
            return tostring(math.type(procMask)) .. '|' .. tostring(math.type(sysMask))
                .. '|' .. tostring(procMask ~= 0) .. '|' .. tostring(sysMask ~= 0)
        ");
        r.String.ShouldBe("integer|integer|true|true");
    }

    // DateTime.Parse: whole-hour offsets (Postgres TIMESTAMPTZ output)
    [Fact]
    public async Task DateTime_Parse_HourOnlyOffset()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(@"
            local a = DateTime.Parse('2024-01-01 12:00:00+00')
            local b = DateTime.Parse('2024-01-01 12:00:00.123456-05')
            local c = DateTime.Parse('2024-01-01 12:00:00+0530')
            local d = DateTime.Parse('2024-01-01 12:00:00+05:30')
            local e = DateTime.Parse('2024-01-01 12:00:00+05:')
            local f = DateTime.Parse('2024-01-01 12:00:00+5')
            return table.concat({
                a:OffsetMinutes(), a:UnixSeconds(),
                b:OffsetMinutes(), b:Hour(), b:Millisecond(), b:ToUtc():Hour(),
                c:OffsetMinutes(), d:OffsetMinutes(),
                tostring(e), tostring(f)}, ',')
        ");
        r.String.ShouldBe("0,1704110400.0,-300,12,123,17,330,330,nil,nil");
    }

    // Identifier == other userdata
    [Fact]
    public async Task Identifier_Eq_OtherUserdata_IsFalse()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(@"
            local id = Identifier.NewOID()
            local dt = DateTime.Now()
            local copy = Identifier.FromString(tostring(id))
            return tostring(id == dt) .. ':' .. tostring(dt == id) .. ':' .. tostring(id == copy)
        ");
        r.String.ShouldBe("false:false:true");
    }

    // Yaml anchors, aliases and merge keys
    [Fact]
    public async Task Yaml_Decode_ScalarAlias_Resolves()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(@"
            local t = Yaml.New():Decode('a: &x 5\nb: *x\nc: &s hello\nd: *s\ne: &n ~\nf: *n\n')
            return table.concat({tostring(t.a), tostring(t.b), t.c, t.d,
                tostring(t.e), tostring(t.f)}, ',')
        ");
        r.String.ShouldBe("5,5,hello,hello,nil,nil");
    }

    [Fact]
    public async Task Yaml_Decode_CollectionAlias_SharesTable()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(@"
            local t = Yaml.New():Decode([[
base: &b
  host: db
  port: 5432
list: &l [1, 2, 3]
copy: *b
items: *l
]])
            return tostring(t.copy == t.base) .. ':' .. t.copy.host .. ':' .. t.copy.port
                .. ':' .. tostring(t.items == t.list) .. ':' .. #t.items
        ");
        r.String.ShouldBe("true:db:5432:true:3");
    }

    [Fact]
    public async Task Yaml_Decode_MergeKey_ExplicitKeysWin()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(@"
            local t = Yaml.New():Decode([[
a: &a {x: 1, y: 2, z: 3}
b: &b {y: 20, w: 40}
one:
  x: 10
  <<: *a
many:
  <<: [*b, *a]
  z: 30
]])
            local o, m = t.one, t.many
            return table.concat({o.x, o.y, o.z, tostring(o['<<']),
                m.x, m.y, m.z, m.w, tostring(m['<<'])}, ',')
        ");
        r.String.ShouldBe("10,2,3,nil,1,20,30,40,nil");
    }

    [Fact]
    public async Task Yaml_Decode_SelfReferenceAndUndefinedAlias()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(@"
            local y = Yaml.New()
            local t = y:Decode('&r {name: root, self: *r}')
            local ok, err = pcall(y.Decode, y, 'a: *missing')
            return tostring(t.self == t) .. ':' .. t.self.name .. ':' .. tostring(ok) .. ':'
                .. tostring(err:find('undefined alias', 1, true) ~= nil)
        ");
        r.String.ShouldBe("true:root:false:true");
    }

    [Fact]
    public async Task Yaml_Decode_NullAndComplexKeys_DoNotBreakParsing()
    {
        // A null key can't be stored in a Lua table and must not raise; a
        // non-scalar key must still have its events consumed so the parse is not derailed.
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(@"
            local t = Yaml.New():Decode('~: 1\n? [a, b]\n: 2\nc: 3\n')
            local tableKeys = 0
            for k, v in pairs(t) do if type(k) == 'table' and v == 2 then tableKeys = tableKeys + 1 end end
            return tostring(t.c) .. ':' .. tableKeys
        ");
        r.String.ShouldBe("3:1");
    }

    [Fact]
    public async Task Yaml_Decode_ParseErrorAndDeepNesting_RaiseCleanly()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(@"
            local y = Yaml.New()
            local ok1, e1 = pcall(y.Decode, y, 'a: [1, 2')
            -- Valid but too deep: rejected either by libyaml's own depth limit or ours.
            local ok2, e2 = pcall(y.Decode, y, string.rep('[', 5000) .. string.rep(']', 5000))
            local ok3, v = pcall(y.Decode, y, 'x: 1')
            return tostring(ok1) .. ':' .. tostring(e1:find('parse error', 1, true) ~= nil) .. ':'
                .. tostring(ok2) .. ':' .. tostring(e2:find('Yaml: ', 1, true) ~= nil) .. ':'
                .. tostring(ok3 and v.x)
        ");
        r.String.ShouldBe("false:true:false:true:1");
    }
}
