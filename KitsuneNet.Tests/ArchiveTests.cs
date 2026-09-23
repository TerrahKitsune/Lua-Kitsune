using KitsuneNet;
using Shouldly;
using Xunit;

namespace KitsuneNet.Tests;

[Collection("KitsuneSequential")]
public sealed class ArchiveTests
{
    public static TheoryData<string> ArchiveFiles =>
    [
        Path.Combine(AppContext.BaseDirectory, "test.7z" ).Replace('\\', '/'),
        Path.Combine(AppContext.BaseDirectory, "test.zip").Replace('\\', '/'),
    ];

    // -- Entries ---------------------------------------------------------------
    [ArchiveTheory]
    [MemberData(nameof(ArchiveFiles))]
    public async Task Entries_Returns_TwoFiles(string path)
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync("""
            local arc, err = Archive.OpenRead((...))
            assert(arc, err or "failed to open archive")
            local entries = Archive.Entries(arc)
            return tostring(#entries)
            """,
            args: [LuaValue.FromString(path)]);
        r.String.ShouldBe("2");
    }

    [ArchiveTheory]
    [MemberData(nameof(ArchiveFiles))]
    public async Task Entries_ContainsExpectedFileNames(string path)
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync("""
            local arc, err = Archive.OpenRead((...))
            assert(arc, err or "failed to open archive")
            local entries = Archive.Entries(arc)
            local names = {}
            for i = 1, #entries do
                names[i] = entries[i].Name
            end
            table.sort(names)
            return names[1] .. '|' .. names[2]
            """,
            args: [LuaValue.FromString(path)]);
        r.String.ShouldBe("this is a test file.txt|this is also a test file.txt");
    }

    [ArchiveTheory]
    [MemberData(nameof(ArchiveFiles))]
    public async Task Entries_ReportsCorrectFileSizes(string path)
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync("""
            local arc, err = Archive.OpenRead((...))
            assert(arc, err or "failed to open archive")
            local entries = Archive.Entries(arc)
            for i = 1, #entries do
                assert(entries[i].Size >= 0, "entry " .. i .. " has negative size")
            end
            return "ok"
            """,
            args: [LuaValue.FromString(path)]);
        r.String.ShouldBe("ok");
    }

    // -- ReadAll ---------------------------------------------------------------
    [ArchiveTheory]
    [MemberData(nameof(ArchiveFiles))]
    public async Task ReadAll_FirstEntry_ReturnsNonEmptyData(string path)
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync("""
            local arc, err = Archive.OpenRead((...))
            assert(arc, err or "failed to open archive")
            local name, size = Archive.SetEntry(arc, 1)
            assert(name, "SetEntry returned nil for entry 1")
            local data = Archive.ReadAll(arc)
            assert(data ~= nil, "ReadAll returned nil")
            return tostring(#data > 0)
            """,
            args: [LuaValue.FromString(path)]);
        r.String.ShouldBe("true");
    }

    [ArchiveTheory]
    [MemberData(nameof(ArchiveFiles))]
    public async Task ReadAll_SecondEntry_ReturnsNonEmptyData(string path)
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync("""
            local arc, err = Archive.OpenRead((...))
            assert(arc, err or "failed to open archive")
            local name, size = Archive.SetEntry(arc, 2)
            assert(name, "SetEntry returned nil for entry 2")
            local data = Archive.ReadAll(arc)
            assert(data ~= nil, "ReadAll returned nil")
            return tostring(#data > 0)
            """,
            args: [LuaValue.FromString(path)]);
        r.String.ShouldBe("true");
    }

    [ArchiveTheory]
    [MemberData(nameof(ArchiveFiles))]
    public async Task ReadAll_BothEntries_DataMatchesDeclaredSize(string path)
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync("""
            local arc, err = Archive.OpenRead((...))
            assert(arc, err or "failed to open archive")
            local entries = Archive.Entries(arc)
            for i = 1, #entries do
                local name, size = Archive.SetEntry(arc, i)
                assert(name, "SetEntry returned nil for entry " .. i)
                local data = Archive.ReadAll(arc)
                assert(data, "ReadAll returned nil for entry " .. i)
                assert(#data == size,
                    "entry " .. i .. ": ReadAll size " .. #data .. " != declared " .. size)
            end
            return "ok"
            """,
            args: [LuaValue.FromString(path)]);
        r.String.ShouldBe("ok");
    }

    // -- Read (chunked) --------------------------------------------------------
    [ArchiveTheory]
    [MemberData(nameof(ArchiveFiles))]
    public async Task Read_ChunkedRead_ReassemblesFullEntry(string path)
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync("""
            local arc, err = Archive.OpenRead((...))
            assert(arc, err or "failed to open archive")
            local name, size = Archive.SetEntry(arc, 1)
            assert(name, "SetEntry returned nil")
            local parts = {}
            repeat
                local chunk = Archive.Read(arc, 4)
                if chunk then
                    parts[#parts + 1] = chunk
                end
            until chunk == nil
            local assembled = table.concat(parts)
            return tostring(#assembled == size)
            """,
            args: [LuaValue.FromString(path)]);
        r.String.ShouldBe("true");
    }

    // -- OpenRead error handling -----------------------------------------------
    [ArchiveFact]
    public async Task OpenRead_NonExistentFile_ReturnsNilAndError()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync("""
            local arc, err = Archive.OpenRead((...))
            return tostring(arc == nil) .. '|' .. tostring(type(err) == 'string')
            """,
            args: [LuaValue.FromString("nonexistent_file_that_does_not_exist.7z")]);
        r.String.ShouldBe("true|true");
    }

    // -- Non-ASCII archive path ------------------------------------------------
    [ArchiveTheory]
    [MemberData(nameof(ArchiveFiles))]
    public async Task OpenRead_NonAsciiPath_ReadsEntries(string path)
    {
        using KitsuneEngine engine = new();

        // A copy of the archive under a name outside the ANSI code page must open,
        // list and read exactly like the original.
        LuaValue r = await engine.ExecuteStringAsync("""
            local src = (...)
            local ext = src:match('%.%w+$')
            local copy = FileSystem.GetTempFileName() .. '_\xc5\x81\xe6\xb5\x8b_\xf0\x9f\x98\x80' .. ext
            assert(FileSystem.Copy(src, copy, true), 'copy failed')
            local arc, err = Archive.OpenRead(copy)
            local count, data = 0, nil
            if arc then
                count = #Archive.Entries(arc)
                Archive.SetEntry(arc, 1)
                data = Archive.ReadAll(arc)
            end
            FileSystem.Delete(copy)
            return tostring(arc ~= nil and count == 2 and type(data) == 'string') .. '|' .. tostring(err)
            """,
            args: [LuaValue.FromString(path)]);
        r.String.ShouldBe("true|nil");
    }
}
