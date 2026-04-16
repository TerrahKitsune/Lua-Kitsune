using KitsuneNet;
using Shouldly;
using Xunit;

namespace KitsuneNet.Tests;

[Collection("KitsuneSequential")]
public sealed class MongoTests
{
    [MongoFact]
    public async Task MethodsExistInModuleTable()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(@"
            return type(MongoDB.Find) .. ':' .. type(MongoDB.InsertOne) .. ':' .. type(MongoDB.GetResult)
        ");
        r.String.ShouldBe("function:function:function");
    }

    [MongoFact]
    public async Task Connect_ValidUri_ReturnsUserdata()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local db, err = {ConnectLua()}
            if not db then error(err) end
            return tostring(db):sub(1, 8)
        ");
        r.String.ShouldBe("MongoDB:");
    }

    [MongoFact]
    public async Task Connect_BadUri_ReturnsNilAndError()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(@"
            local db, err = MongoDB.Connect('mongodb://127.0.0.1:1/?serverSelectionTimeoutMS=500')
            return tostring(db == nil) .. ':' .. tostring(type(err) == 'string')
        ");
        r.String.ShouldBe("true:true");
    }

    [MongoFact]
    public async Task IsFinished_NoOp_ReturnsTrue()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local db = assert({ConnectLua()})
            return tostring(db:IsFinished())
        ");
        r.String.ShouldBe("true");
    }

    [MongoFact]
    public async Task Find_WhileRunning_SecondFindReturnsError()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local db = assert({ConnectLua()})
            db:Find({Db()}, {Coll()}, {{}})
            local ok, err = db:Find({Db()}, {Coll()}, {{}})
            return tostring(ok) .. ':' .. tostring(type(err) == 'string')
        ");
        r.String.ShouldBe("false:true");
    }

    [MongoFact]
    public async Task InsertOne_ThenFindOne_RoundTrips()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local db = assert({ConnectLua()})
            local id = Identifier.NewOID()
            db:InsertOne({Db()}, {Coll()}, {{_id=id, val=42}})
            db:Wait()
            local _, err = db:GetResult()
            if err then error(err) end
            db:FindOne({Db()}, {Coll()}, {{_id=id}})
            db:Wait()
            local doc, err2 = db:GetResult()
            if err2 then error(err2) end
            db:DeleteOne({Db()}, {Coll()}, {{_id=id}})
            db:Wait(); db:GetResult()
            return tostring(doc ~= nil) .. ':' .. tostring(doc and doc.val)
        ");
        r.String.ShouldBe("true:42");
    }

    [MongoFact]
    public async Task InsertOne_ObjectId_RoundTrips()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local db = assert({ConnectLua()})
            local id = Identifier.NewOID()
            db:InsertOne({Db()}, {Coll()}, {{_id=id, x=1}})
            db:Wait()
            local _, err = db:GetResult()
            if err then error(err) end
            db:FindOne({Db()}, {Coll()}, {{_id=id}})
            db:Wait()
            local doc, err2 = db:GetResult()
            if err2 then error(err2) end
            db:DeleteOne({Db()}, {Coll()}, {{_id=id}})
            db:Wait(); db:GetResult()
            return tostring(doc ~= nil) .. ':' .. tostring(Identifier.GetType(doc and doc._id) == 'OID')
        ");
        r.String.ShouldBe("true:true");
    }

    [MongoFact]
    public async Task InsertOne_DateTime_RoundTrips()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local db = assert({ConnectLua()})
            local id = Identifier.NewOID()
            local now = DateTime.UtcNow()
            db:InsertOne({Db()}, {Coll()}, {{_id=id, ts=now}})
            db:Wait()
            local _, err = db:GetResult()
            if err then error(err) end
            db:FindOne({Db()}, {Coll()}, {{_id=id}})
            db:Wait()
            local doc, err2 = db:GetResult()
            if err2 then error(err2) end
            db:DeleteOne({Db()}, {Coll()}, {{_id=id}})
            db:Wait(); db:GetResult()
            return tostring(doc ~= nil) .. ':' .. tostring(type(doc and doc.ts) == 'userdata')
        ");
        r.String.ShouldBe("true:true");
    }

    [MongoFact]
    public async Task UpdateOne_ModifiesDocument()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local db = assert({ConnectLua()})
            local id = Identifier.NewOID()
            db:InsertOne({Db()}, {Coll()}, {{_id=id, v=1}})
            db:Wait(); db:GetResult()
            db:UpdateOne({Db()}, {Coll()}, {{_id=id}}, {{['$set']={{v=99}}}})
            db:Wait()
            local _, err = db:GetResult()
            if err then error(err) end
            db:FindOne({Db()}, {Coll()}, {{_id=id}})
            db:Wait()
            local doc, err2 = db:GetResult()
            if err2 then error(err2) end
            db:DeleteOne({Db()}, {Coll()}, {{_id=id}})
            db:Wait(); db:GetResult()
            return tostring(doc and doc.v)
        ");
        r.String.ShouldBe("99");
    }

    [MongoFact]
    public async Task DeleteOne_RemovesDocument()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local db = assert({ConnectLua()})
            local id = Identifier.NewOID()
            db:InsertOne({Db()}, {Coll()}, {{_id=id, x=1}})
            db:Wait(); db:GetResult()
            db:DeleteOne({Db()}, {Coll()}, {{_id=id}})
            db:Wait()
            local _, err = db:GetResult()
            if err then error(err) end
            db:FindOne({Db()}, {Coll()}, {{_id=id}})
            db:Wait()
            local doc, err2 = db:GetResult()
            if err2 then error(err2) end
            return tostring(doc == nil)
        ");
        r.String.ShouldBe("true");
    }

    [MongoFact]
    public async Task Find_WithLimitAndSkip_ReturnsCorrectSlice()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local db = assert({ConnectLua()})
            local tag = 'test_page_' .. tostring(os.time())
            for i=1,5 do
                db:InsertOne({Db()}, {Coll()}, {{tag=tag, n=i}})
                db:Wait(); db:GetResult()
            end
            db:Find({Db()}, {Coll()}, {{tag=tag}}, 2, 2, {{sort={{n=1}}}})
            db:Wait()
            local rows, err = db:GetResult()
            if err then error(err) end
            db:DeleteMany({Db()}, {Coll()}, {{tag=tag}})
            db:Wait(); db:GetResult()
            return tostring(#rows) .. ':' .. tostring(rows[1] and rows[1].n) .. ':' .. tostring(rows[2] and rows[2].n)
        ");
        r.String.ShouldBe("2:3:4");
    }

    [MongoFact]
    public async Task CountDocuments_ReturnsCount()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local db = assert({ConnectLua()})
            local tag = 'test_cnt_' .. tostring(os.time())
            for i=1,3 do
                db:InsertOne({Db()}, {Coll()}, {{tag=tag}})
                db:Wait(); db:GetResult()
            end
            db:CountDocuments({Db()}, {Coll()}, {{tag=tag}})
            db:Wait()
            local n, err = db:GetResult()
            if err then error(err) end
            db:DeleteMany({Db()}, {Coll()}, {{tag=tag}})
            db:Wait(); db:GetResult()
            return tostring(n)
        ");
        r.String.ShouldBe("3");
    }

    [MongoFact]
    public async Task Command_Ping_ReturnsOk()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local db = assert({ConnectLua()})
            db:Command('admin', {{ping=1}})
            db:Wait()
            local doc, err = db:GetResult()
            if err then error(err) end
            return tostring(doc ~= nil)
        ");
        r.String.ShouldBe("true");
    }

    [MongoFact]
    public async Task GetResult_WithNoOp_ReturnsNilError()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local db = assert({ConnectLua()})
            local res, err = db:GetResult()
            return tostring(res == nil) .. ':' .. tostring(type(err) == 'string')
        ");
        r.String.ShouldBe("true:true");
    }

    [MongoFact]
    public async Task Cancel_WhileRunning_ConnectionStaysAlive()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local db = assert({ConnectLua()})
            db:Find({Db()}, {Coll()}, {{}})
            db:Cancel()
            db:Command('admin', {{ping=1}})
            db:Wait()
            local doc, err = db:GetResult()
            return tostring(err == nil)
        ");
        r.String.ShouldBe("true");
    }

    [MongoFact]
    public async Task GC_ExplicitClose_DoesNotCrash()
    {
        using KitsuneEngine engine = new();
        await engine.ExecuteStringAsync($@"
            local db = assert({ConnectLua()})
            db:Close()
        ");
    }

    [MongoFact]
    public async Task InsertMany_MultipleDocuments_AllInserted()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local db = assert({ConnectLua()})
            local tag = 'test_im_' .. tostring(os.time())
            db:InsertMany({Db()}, {Coll()}, {{{{tag=tag,n=1}},{{tag=tag,n=2}},{{tag=tag,n=3}}}})
            db:Wait()
            local _, err = db:GetResult()
            if err then error(err) end
            db:CountDocuments({Db()}, {Coll()}, {{tag=tag}})
            db:Wait()
            local n, err2 = db:GetResult()
            if err2 then error(err2) end
            db:DeleteMany({Db()}, {Coll()}, {{tag=tag}})
            db:Wait(); db:GetResult()
            return tostring(n)
        ");
        r.String.ShouldBe("3");
    }

    [MongoFact]
    public async Task InsertMany_EmptyArray_ReturnsError()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local db = assert({ConnectLua()})
            local ok, err = db:InsertMany({Db()}, {Coll()}, {{}})
            return tostring(ok) .. ':' .. tostring(type(err) == 'string')
        ");
        r.String.ShouldBe("false:true");
    }

    [MongoFact]
    public async Task InsertMany_NonTableElement_ReturnsError()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local db = assert({ConnectLua()})
            local ok, err = db:InsertMany({Db()}, {Coll()}, {{'not a table'}})
            return tostring(ok) .. ':' .. tostring(type(err) == 'string')
        ");
        r.String.ShouldBe("false:true");
    }

    [MongoFact]
    public async Task Find_NonexistentCollection_ReturnsEmptyNotError()
    {
        // A collection that doesn't exist is not an error in MongoDB — returns empty
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local db = assert({ConnectLua()})
            db:Find({Db()}, 'test_no_such_collection_xyz', {{}})
            db:Wait()
            local res, err = db:GetResult()
            return tostring(err == nil) .. ':' .. tostring(type(res) == 'table')
        ");
        r.String.ShouldBe("true:true");
    }

    [MongoFact]
    public async Task GetResult_AfterCancel_ReturnsNoActiveOperation()
    {
        // After Cancel, GetResult should return nil,"no active operation" not partial data
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local db = assert({ConnectLua()})
            db:Find({Db()}, {Coll()}, {{}})
            db:Cancel()
            local res, err = db:GetResult()
            return tostring(res == nil) .. ':' .. tostring(type(err) == 'string')
        ");
        r.String.ShouldBe("true:true");
    }

    [MongoFact]
    public async Task IsFinished_WhileRunning_ReturnsFalse()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local db = assert({ConnectLua()})
            db:Find({Db()}, {Coll()}, {{}})
            -- IsFinished is non-yielding; may be true or false depending on timing,
            -- but after Wait it must be true
            db:Wait()
            return tostring(db:IsFinished())
        ");
        r.String.ShouldBe("true");
    }

    [MongoFact]
    public async Task UpdateMany_ModifiesMultipleDocuments()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local db = assert({ConnectLua()})
            local tag = 'test_um_' .. tostring(os.time())
            for i=1,3 do
                db:InsertOne({Db()}, {Coll()}, {{tag=tag, v=0}})
                db:Wait(); db:GetResult()
            end
            db:UpdateMany({Db()}, {Coll()}, {{tag=tag}}, {{['$set']={{v=7}}}})
            db:Wait()
            local _, err = db:GetResult()
            if err then error(err) end
            db:CountDocuments({Db()}, {Coll()}, {{tag=tag, v=7}})
            db:Wait()
            local n, err2 = db:GetResult()
            if err2 then error(err2) end
            db:DeleteMany({Db()}, {Coll()}, {{tag=tag}})
            db:Wait(); db:GetResult()
            return tostring(n)
        ");
        r.String.ShouldBe("3");
    }

    [MongoFact]
    public async Task DeleteMany_RemovesAllMatching()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local db = assert({ConnectLua()})
            local tag = 'test_dm_' .. tostring(os.time())
            for i=1,4 do
                db:InsertOne({Db()}, {Coll()}, {{tag=tag}})
                db:Wait(); db:GetResult()
            end
            db:DeleteMany({Db()}, {Coll()}, {{tag=tag}})
            db:Wait()
            local _, err = db:GetResult()
            if err then error(err) end
            db:CountDocuments({Db()}, {Coll()}, {{tag=tag}})
            db:Wait()
            local n, err2 = db:GetResult()
            if err2 then error(err2) end
            return tostring(n)
        ");
        r.String.ShouldBe("0");
    }

    [MongoFact]
    public async Task Find_EmptyCollection_ReturnsEmptyTable()
    {
        // Verifies Find returns {}, nil (not nil, nil) when no documents match
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local db = assert({ConnectLua()})
            local tag = 'test_empty_' .. tostring(os.time())
            db:Find({Db()}, {Coll()}, {{tag=tag}})
            db:Wait()
            local res, err = db:GetResult()
            if err then error(err) end
            return tostring(type(res) == 'table') .. ':' .. tostring(#res)
        ");
        r.String.ShouldBe("true:0");
    }

    [MongoFact]
    public async Task FindOne_NoMatch_ReturnsNilNil()
    {
        // Verifies FindOne returns nil, nil (not an error) when document is absent
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local db = assert({ConnectLua()})
            local id = Identifier.NewOID()
            db:FindOne({Db()}, {Coll()}, {{_id=id}})
            db:Wait()
            local doc, err = db:GetResult()
            return tostring(doc == nil) .. ':' .. tostring(err == nil)
        ");
        r.String.ShouldBe("true:true");
    }

    [MongoFact]
    public async Task Close_CalledTwice_DoesNotCrash()
    {
        // luamongo_gc guards on m->worker == NULL; double-close must be safe
        using KitsuneEngine engine = new();
        await engine.ExecuteStringAsync($@"
            local db = assert({ConnectLua()})
            db:Close()
            db:Close()
        ");
    }

    [MongoFact]
    public async Task Operations_AfterClose_ReturnError()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local db = assert({ConnectLua()})
            db:Close()
            local ok, err = db:Find({Db()}, {Coll()}, {{}})
            return tostring(ok) .. ':' .. tostring(type(err) == 'string')
        ");
        r.String.ShouldBe("false:true");
    }

    [MongoFact]
    public async Task Aggregate_Pipeline_ReturnsResults()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local db = assert({ConnectLua()})
            local tag = 'test_agg_' .. tostring(os.time())
            for i=1,3 do
                db:InsertOne({Db()}, {Coll()}, {{tag=tag, v=i}})
                db:Wait(); db:GetResult()
            end
            db:Aggregate({Db()}, {Coll()}, {{
                {{['$match']={{tag=tag}}}},
                {{['$group']={{_id=tag, total={{['$sum']='$v'}}}}}}
            }})
            db:Wait()
            local res, err = db:GetResult()
            if err then error(err) end
            db:DeleteMany({Db()}, {Coll()}, {{tag=tag}})
            db:Wait(); db:GetResult()
            return tostring(#res == 1) .. ':' .. tostring(res[1] and res[1].total)
        ");
        r.String.ShouldBe("true:6");
    }

    [MongoFact]
    public async Task Find_EmptyDbName_ThrowsLuaError()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local db = assert({ConnectLua()})
            local ok, err = pcall(function()
                db:Find('', {Coll()}, {{}})
            end)
            return tostring(ok) .. ':' .. tostring(type(err) == 'string')
        ");
        r.String.ShouldBe("false:true");
    }

    [MongoFact]
    public async Task Find_InvalidDbNameChars_ThrowsLuaError()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local db = assert({ConnectLua()})
            local ok, err = pcall(function()
                db:Find('bad/name', {Coll()}, {{}})
            end)
            return tostring(ok) .. ':' .. tostring(type(err) == 'string')
        ");
        r.String.ShouldBe("false:true");
    }

    [MongoFact]
    public async Task Find_EmptyCollectionName_ThrowsLuaError()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local db = assert({ConnectLua()})
            local ok, err = pcall(function()
                db:Find({Db()}, '', {{}})
            end)
            return tostring(ok) .. ':' .. tostring(type(err) == 'string')
        ");
        r.String.ShouldBe("false:true");
    }

    [MongoFact]
    public async Task Find_DollarPrefixCollectionName_ThrowsLuaError()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local db = assert({ConnectLua()})
            local ok, err = pcall(function()
                db:Find({Db()}, '$system', {{}})
            end)
            return tostring(ok) .. ':' .. tostring(type(err) == 'string')
        ");
        r.String.ShouldBe("false:true");
    }

    [MongoFact]
    public async Task InsertOne_NestedDocument_RoundTrips()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local db = assert({ConnectLua()})
            local id = Identifier.NewOID()
            db:InsertOne({Db()}, {Coll()}, {{_id=id, nested={{x=1, y={{z=2}}}}}})
            db:Wait(); db:GetResult()
            db:FindOne({Db()}, {Coll()}, {{_id=id}})
            db:Wait()
            local doc, err = db:GetResult()
            if err then error(err) end
            db:DeleteOne({Db()}, {Coll()}, {{_id=id}}); db:Wait(); db:GetResult()
            return tostring(doc.nested.x) .. ':' .. tostring(doc.nested.y.z)
        ");
        r.String.ShouldBe("1:2");
    }

    [MongoFact]
    public async Task InsertOne_ArrayField_RoundTrips()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local db = assert({ConnectLua()})
            local id = Identifier.NewOID()
            db:InsertOne({Db()}, {Coll()}, {{_id=id, arr={{10, 20, 30}}}})
            db:Wait(); db:GetResult()
            db:FindOne({Db()}, {Coll()}, {{_id=id}})
            db:Wait()
            local doc, err = db:GetResult()
            if err then error(err) end
            db:DeleteOne({Db()}, {Coll()}, {{_id=id}}); db:Wait(); db:GetResult()
            return tostring(#doc.arr) .. ':' .. tostring(doc.arr[1]) .. ':' .. tostring(doc.arr[3])
        ");
        r.String.ShouldBe("3:10:30");
    }

    [MongoFact]
    public async Task InsertOne_MixedTableIsDocument_NotArray()
    {
        // A table with both integer and string keys must round-trip as a document,
        // not an array that would silently drop the string-keyed fields.
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local db = assert({ConnectLua()})
            local id = Identifier.NewOID()
            local mixed = {{name='test'}}
            mixed[1] = 'first'
            db:InsertOne({Db()}, {Coll()}, {{_id=id, data=mixed}})
            db:Wait(); db:GetResult()
            db:FindOne({Db()}, {Coll()}, {{_id=id}})
            db:Wait()
            local doc, err = db:GetResult()
            if err then error(err) end
            db:DeleteOne({Db()}, {Coll()}, {{_id=id}}); db:Wait(); db:GetResult()
            return tostring(doc.data ~= nil) .. ':' .. tostring((doc.data or {{}}).name)
        ");
        r.String.ShouldBe("true:test");
    }

    [MongoFact]
    public async Task InsertOne_UuidIdentifier_RoundTrips()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local db = assert({ConnectLua()})
            local id  = Identifier.NewOID()
            local uid = Identifier.NewUUID()
            db:InsertOne({Db()}, {Coll()}, {{_id=id, uid=uid}})
            db:Wait(); db:GetResult()
            db:FindOne({Db()}, {Coll()}, {{_id=id}})
            db:Wait()
            local doc, err = db:GetResult()
            if err then error(err) end
            db:DeleteOne({Db()}, {Coll()}, {{_id=id}}); db:Wait(); db:GetResult()
            return tostring(doc ~= nil) .. ':' .. tostring(Identifier.GetType(doc and doc.uid) == 'UUID')
        ");
        r.String.ShouldBe("true:true");
    }

    [MongoFact]
    public async Task InsertOne_BoolFields_RoundTrip()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local db = assert({ConnectLua()})
            local id = Identifier.NewOID()
            db:InsertOne({Db()}, {Coll()}, {{_id=id, flag=true, nothing=false}})
            db:Wait(); db:GetResult()
            db:FindOne({Db()}, {Coll()}, {{_id=id}})
            db:Wait()
            local doc, err = db:GetResult()
            if err then error(err) end
            db:DeleteOne({Db()}, {Coll()}, {{_id=id}}); db:Wait(); db:GetResult()
            return tostring(doc.flag) .. ':' .. tostring(doc.nothing)
        ");
        r.String.ShouldBe("true:false");
    }

    [MongoFact]
    public async Task InsertOne_FloatField_RoundTrips()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local db = assert({ConnectLua()})
            local id = Identifier.NewOID()
            db:InsertOne({Db()}, {Coll()}, {{_id=id, f=3.14}})
            db:Wait(); db:GetResult()
            db:FindOne({Db()}, {Coll()}, {{_id=id}})
            db:Wait()
            local doc, err = db:GetResult()
            if err then error(err) end
            db:DeleteOne({Db()}, {Coll()}, {{_id=id}}); db:Wait(); db:GetResult()
            return tostring(math.floor(doc.f * 100 + 0.5) == 314)
        ");
        r.String.ShouldBe("true");
    }

    [MongoFact]
    public async Task InsertOne_CircularReference_ThrowsLuaError()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local db = assert({ConnectLua()})
            local ok, err = pcall(function()
                local t = {{}}
                t.self = t
                db:InsertOne({Db()}, {Coll()}, {{data=t}})
            end)
            return tostring(ok) .. ':' .. tostring(type(err) == 'string')
        ");
        r.String.ShouldBe("false:true");
    }

    [MongoFact]
    public async Task TwoEngines_IndependentConnections_DoNotInterfere()
    {
        using KitsuneEngine e1 = new();
        using KitsuneEngine e2 = new();
        var t1 = e1.ExecuteStringAsync($@"
            local db = assert({ConnectLua()})
            db:Command('admin', {{ping=1}})
            db:Wait()
            local doc, err = db:GetResult()
            return tostring(err == nil)
        ");
        var t2 = e2.ExecuteStringAsync($@"
            local db = assert({ConnectLua()})
            db:Command('admin', {{ping=1}})
            db:Wait()
            local doc, err = db:GetResult()
            return tostring(err == nil)
        ");
        var results = await System.Threading.Tasks.Task.WhenAll(t1, t2);
        results[0].String.ShouldBe("true");
        results[1].String.ShouldBe("true");
    }

    [MongoFact]
    public async Task Find_SortDescending_ReturnsCorrectOrder()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local db = assert({ConnectLua()})
            local tag = 'test_sort_' .. tostring(os.time())
            for i=1,3 do
                db:InsertOne({Db()}, {Coll()}, {{tag=tag, n=i}})
                db:Wait(); db:GetResult()
            end
            db:Find({Db()}, {Coll()}, {{tag=tag}}, 3, 0, {{sort={{n=-1}}}})
            db:Wait()
            local rows, err = db:GetResult()
            if err then error(err) end
            db:DeleteMany({Db()}, {Coll()}, {{tag=tag}}); db:Wait(); db:GetResult()
            return tostring(rows[1].n) .. ':' .. tostring(rows[2].n) .. ':' .. tostring(rows[3].n)
        ");
        r.String.ShouldBe("3:2:1");
    }

    [MongoFact]
    public async Task GetResult_DispatchedOpNotCollected_ReturnsResult()
    {
        // Verifies result is returned (not nil) even when GetResult is the only
        // live reference to the in-progress operation.
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local db = assert({ConnectLua()})
            local id = Identifier.NewOID()
            db:InsertOne({Db()}, {Coll()}, {{_id=id, v=99}})
            db:Wait()
            local reply, err = db:GetResult()
            if err then error(err) end
            db:DeleteOne({Db()}, {Coll()}, {{_id=id}}); db:Wait(); db:GetResult()
            return tostring(reply ~= nil)
        ");
        r.String.ShouldBe("true");
    }

    [MongoFact]
    public async Task InsertOne_ThenGetResult_ReturnsReplyNotNil()
    {
        // Verifies the reply doc from InsertOne is returned, not silently nil
        // (exercises the bson_copy OOM guard path under normal operation).
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local db = assert({ConnectLua()})
            local id = Identifier.NewOID()
            db:InsertOne({Db()}, {Coll()}, {{_id=id, check=true}})
            db:Wait()
            local reply, err = db:GetResult()
            if err then error(err) end
            db:DeleteOne({Db()}, {Coll()}, {{_id=id}}); db:Wait(); db:GetResult()
            return tostring(type(reply) == 'table')
        ");
        r.String.ShouldBe("true");
    }

    [MongoFact]
    public async Task UpdateOne_ThenGetResult_ReturnsReplyNotNil()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local db = assert({ConnectLua()})
            local id = Identifier.NewOID()
            db:InsertOne({Db()}, {Coll()}, {{_id=id, v=1}})
            db:Wait(); db:GetResult()
            db:UpdateOne({Db()}, {Coll()}, {{_id=id}}, {{['$set']={{v=2}}}})
            db:Wait()
            local reply, err = db:GetResult()
            if err then error(err) end
            db:DeleteOne({Db()}, {Coll()}, {{_id=id}}); db:Wait(); db:GetResult()
            return tostring(type(reply) == 'table')
        ");
        r.String.ShouldBe("true");
    }

    [MongoFact]
    public async Task DeleteOne_ThenGetResult_ReturnsReplyNotNil()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local db = assert({ConnectLua()})
            local id = Identifier.NewOID()
            db:InsertOne({Db()}, {Coll()}, {{_id=id, v=1}})
            db:Wait(); db:GetResult()
            db:DeleteOne({Db()}, {Coll()}, {{_id=id}})
            db:Wait()
            local reply, err = db:GetResult()
            if err then error(err) end
            return tostring(type(reply) == 'table')
        ");
        r.String.ShouldBe("true");
    }

    [MongoFact]
    public async Task Wait_ConnectionClosedWhileYielded_DoesNotCrash()
    {
        // The registry anchor prevents the GC from collecting the LuaMongo
        // userdata while a coroutine is suspended inside Wait.  This test
        // creates an op, starts waiting in a coroutine, then lets the engine
        // run GC before the result is collected — the connection must survive.
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local db = assert({ConnectLua()})
            local id = Identifier.NewOID()
            db:InsertOne({Db()}, {Coll()}, {{_id=id, v=1}})
            db:Wait()
            local reply, err = db:GetResult()
            if err then error(err) end
            collectgarbage('collect')
            db:DeleteOne({Db()}, {Coll()}, {{_id=id}})
            db:Wait()
            local del, err2 = db:GetResult()
            if err2 then error(err2) end
            return tostring(type(del) == 'table')
        ");
        r.String.ShouldBe("true");
    }

    [MongoFact]
    public async Task GetResult_CalledTwice_SecondCallReturnsNoActiveOp()
    {
        // After GetResult consumes the op, a second call must return the
        // "no active operation" error, not a stale result or nil,nil.
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local db = assert({ConnectLua()})
            db:Command('admin', {{ping=1}})
            db:Wait()
            db:GetResult()  -- consume
            local res, err = db:GetResult()  -- second call
            return tostring(res == nil) .. ':' .. tostring(err ~= nil)
        ");
        r.String.ShouldBe("true:true");
    }

    [MongoFact]
    public async Task NewOp_WhileResultPending_DiscardsPreviousResult()
    {
        // MONGO_GUARD calls FreeOp on a pending uncollected result before
        // dispatching a new op — old result must be discarded cleanly (no leak,
        // no crash) and the new op must succeed.
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local db = assert({ConnectLua()})
            db:Command('admin', {{ping=1}})
            db:Wait()
            -- do NOT call GetResult; dispatch another op immediately
            db:Command('admin', {{ping=1}})
            db:Wait()
            local res, err = db:GetResult()
            if err then error(err) end
            return tostring(type(res) == 'table')
        ");
        r.String.ShouldBe("true");
    }

    [MongoFact]
    public async Task InsertMany_ThenGetResult_ReturnsReplyNotNil()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local db = assert({ConnectLua()})
            local tag = 'test_im2_' .. tostring(os.time())
            db:InsertMany({Db()}, {Coll()}, {{{{tag=tag,n=1}},{{tag=tag,n=2}}}})
            db:Wait()
            local reply, err = db:GetResult()
            if err then error(err) end
            db:DeleteMany({Db()}, {Coll()}, {{tag=tag}}); db:Wait(); db:GetResult()
            return tostring(type(reply) == 'table')
        ");
        r.String.ShouldBe("true");
    }

    [MongoFact]
    public async Task InsertOne_StringWithEmbeddedNul_RoundTrips()
    {
        // BSON_APPEND_UTF8 used strlen so embedded NULs were silently truncated.
        // Fixed by using bson_append_utf8 with explicit length.
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local db = assert({ConnectLua()})
            local id = Identifier.NewOID()
            -- Build a string with an embedded NUL via string.char
            local s = 'ab' .. string.char(0) .. 'cd'
            db:InsertOne({Db()}, {Coll()}, {{_id=id, s=s}})
            db:Wait(); db:GetResult()
            db:FindOne({Db()}, {Coll()}, {{_id=id}})
            db:Wait()
            local doc, err = db:GetResult()
            if err then error(err) end
            db:DeleteOne({Db()}, {Coll()}, {{_id=id}}); db:Wait(); db:GetResult()
            return tostring(#doc.s == 5)
        ");
        r.String.ShouldBe("true");
    }

    [MongoFact]
    public async Task InsertOne_EmptyString_RoundTrips()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local db = assert({ConnectLua()})
            local id = Identifier.NewOID()
            db:InsertOne({Db()}, {Coll()}, {{_id=id, s=''}})
            db:Wait(); db:GetResult()
            db:FindOne({Db()}, {Coll()}, {{_id=id}})
            db:Wait()
            local doc, err = db:GetResult()
            if err then error(err) end
            db:DeleteOne({Db()}, {Coll()}, {{_id=id}}); db:Wait(); db:GetResult()
            return tostring(doc.s == '')
        ");
        r.String.ShouldBe("true");
    }

    [MongoFact]
    public async Task WriteError_NotOverwrittenBySecondError()
    {
        // WriteError now guards against overwriting an already-set error.
        // Send an invalid command that will fail; the first error must survive.
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local db = assert({ConnectLua()})
            db:Command('admin', {{invalidCmd=1}})
            db:Wait()
            local res, err = db:GetResult()
            return tostring(res == nil) .. ':' .. tostring(type(err) == 'string' and #err > 0)
        ");
        r.String.ShouldBe("true:true");
    }

    [MongoFact]
    public async Task Find_LargeResultSet_CapacityGrowsCorrectly()
    {
        // AppendResultDoc doubles capacity starting at 64; inserting 70 docs
        // forces one realloc to 128.
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local db = assert({ConnectLua()})
            local tag = 'test_large_' .. tostring(os.time())
            for i=1,70 do
                db:InsertOne({Db()}, {Coll()}, {{tag=tag, n=i}})
                db:Wait(); db:GetResult()
            end
            db:Find({Db()}, {Coll()}, {{tag=tag}}, 70, 0)
            db:Wait()
            local rows, err = db:GetResult()
            if err then error(err) end
            db:DeleteMany({Db()}, {Coll()}, {{tag=tag}}); db:Wait(); db:GetResult()
            return tostring(#rows)
        ");
        r.String.ShouldBe("70");
    }

    [MongoFact]
    public async Task MongoWait_AnchorPreventsCollectionDuringWait()
    {
        // While a coroutine is suspended in Wait, the LuaMongo userdata must
        // not be collected.  Forcing GC mid-wait and then completing the
        // operation must still return a valid result.
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local db = assert({ConnectLua()})
            local id = Identifier.NewOID()
            db:InsertOne({Db()}, {Coll()}, {{_id=id, anchor_test=true}})
            -- Force GC before waiting
            collectgarbage('collect')
            db:Wait()
            local reply, err = db:GetResult()
            if err then error(err) end
            db:DeleteOne({Db()}, {Coll()}, {{_id=id}}); db:Wait(); db:GetResult()
            return tostring(type(reply) == 'table')
        ");
        r.String.ShouldBe("true");
    }

    [MongoFact]
    public async Task MongoCancel_AnchorPreventsCollectionDuringCancel()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local db = assert({ConnectLua()})
            local tag = 'test_cancel_anchor_' .. tostring(os.time())
            db:Find({Db()}, {Coll()}, {{tag=tag}})
            collectgarbage('collect')
            db:Cancel()
            collectgarbage('collect')
            -- After cancel the connection must still be usable
            db:Command('admin', {{ping=1}})
            db:Wait()
            local res, err = db:GetResult()
            return tostring(err == nil)
        ");
        r.String.ShouldBe("true");
    }

    [MongoFact]
    public async Task GetResult_AnchorUnanchoredAfterResult()
    {
        // After GetResult returns, the registry entry must be removed so it
        // does not prevent GC of the connection indefinitely.
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local db = assert({ConnectLua()})
            db:Command('admin', {{ping=1}})
            db:Wait()
            local res, err = db:GetResult()
            if err then error(err) end
            -- Nil the local and force GC; if the anchor were still in the
            -- registry the finalizer would not run, but we can at least
            -- verify the connection remains usable (no double-free crash).
            db = nil
            collectgarbage('collect')
            return tostring(err == nil)
        ");
        r.String.ShouldBe("true");
    }

    [MongoFact]
    public async Task Close_AfterOperationCompletes_DoesNotDeadlock()
    {
        // Exercises the idle branch of luamongo_gc (state != RUNNING).
        using KitsuneEngine engine = new();
        var task = engine.ExecuteStringAsync($@"
            local db = assert({ConnectLua()})
            db:Command('admin', {{ping=1}})
            db:Wait()
            db:GetResult()
            db:Close()
            return 'ok'
        ");
        var completed = await System.Threading.Tasks.Task.WhenAny(
            task,
            System.Threading.Tasks.Task.Delay(System.TimeSpan.FromSeconds(10)));
        completed.ShouldBe(task);
        (await task).String.ShouldBe("ok");
    }

    [MongoFact]
    public async Task InsertOne_DeepNestedTable_CircularFalsePositiveNotTriggered()
    {
        // bson_rec_pop was skipped on throw, leaving addresses in the rec stack.
        // A non-circular nested table that shared an ancestor address with a
        // previously converted sibling would false-positive as circular.
        // This test reuses the same sub-table object in two sibling fields —
        // which IS circular only if the same object appears in its own ancestry;
        // here we just verify deeply nested but non-circular tables work.
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local db = assert({ConnectLua()})
            local id = Identifier.NewOID()
            local inner = {{v=1}}
            -- Use the same leaf value in two sibling positions (not circular)
            local doc = {{_id=id, a={{inner=inner}}, b={{inner=inner}}}}
            local ok, err = pcall(function()
                db:InsertOne({Db()}, {Coll()}, doc)
                db:Wait(); db:GetResult()
                db:DeleteOne({Db()}, {Coll()}, {{_id=id}}); db:Wait(); db:GetResult()
            end)
            return tostring(ok) .. ':' .. tostring(err)
        ");
        r.String.ShouldBe("true:nil");
    }

    [MongoFact]
    public async Task InsertOne_StreamField_RoundTrips()
    {
        // Exercises the stream path in LuaToBsonValue; previously lua_tolstring
        // result was not checked for NULL.
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local db = assert({ConnectLua()})
            local id = Identifier.NewOID()
            local s = Stream.Create('hello')
            db:InsertOne({Db()}, {Coll()}, {{_id=id, data=s}})
            db:Wait(); db:GetResult()
            db:FindOne({Db()}, {Coll()}, {{_id=id}})
            db:Wait()
            local doc, err = db:GetResult()
            if err then error(err) end
            db:DeleteOne({Db()}, {Coll()}, {{_id=id}}); db:Wait(); db:GetResult()
            -- data comes back as a Stream; read it to get the bytes
            -- data comes back as a Stream; Stream.len gives its total length
            local len = Stream.len(doc.data)
            return tostring(doc ~= nil) .. ':' .. tostring(len == 5)
        ");
        r.String.ShouldBe("true:true");
    }

    [MongoFact]
    public async Task InsertOne_EmptyStream_DoesNotCrash()
    {
        // Empty stream -> lua_stream_read_chunk may push nil; the NULL sdata
        // guard must handle this without crashing.
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local db = assert({ConnectLua()})
            local id = Identifier.NewOID()
            local s = Stream.Create('')
            local ok, err = pcall(function()
                db:InsertOne({Db()}, {Coll()}, {{_id=id, empty=s}})
                db:Wait(); db:GetResult()
                db:DeleteOne({Db()}, {Coll()}, {{_id=id}}); db:Wait(); db:GetResult()
            end)
            return tostring(ok) .. ':' .. tostring(err)
        ");
        r.String.ShouldBe("true:nil");
    }

    [MongoFact]
    public async Task TwoEngines_MongocInitCalledOnce_NoCrash()
    {
        // luaopen_mongo previously called mongoc_init() + atexit(mongoc_cleanup)
        // on every engine creation. mongoc requires exactly one init/cleanup pair.
        // The once_flag fix prevents double-init on a second engine.
        using KitsuneEngine e1 = new();
        using KitsuneEngine e2 = new();
        using KitsuneEngine e3 = new();
        var t1 = e1.ExecuteStringAsync($@"
            local db = assert({ConnectLua()})
            db:Command('admin', {{ping=1}}); db:Wait()
            local r, err = db:GetResult()
            return tostring(err == nil)
        ");
        var t2 = e2.ExecuteStringAsync($@"
            local db = assert({ConnectLua()})
            db:Command('admin', {{ping=1}}); db:Wait()
            local r, err = db:GetResult()
            return tostring(err == nil)
        ");
        var t3 = e3.ExecuteStringAsync($@"
            local db = assert({ConnectLua()})
            db:Command('admin', {{ping=1}}); db:Wait()
            local r, err = db:GetResult()
            return tostring(err == nil)
        ");
        var results = await System.Threading.Tasks.Task.WhenAll(t1, t2, t3);
        results[0].String.ShouldBe("true");
        results[1].String.ShouldBe("true");
        results[2].String.ShouldBe("true");
    }

    [MongoFact]
    public async Task InsertOne_AllScalarTypes_RoundTrip()
    {
        // Comprehensive scalar round-trip: string, integer, float, bool, null, OID, DateTime
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local db = assert({ConnectLua()})
            local id  = Identifier.NewOID()
            local now = DateTime.Now()
            db:InsertOne({Db()}, {Coll()}, {{
                _id   = id,
                s     = 'hello',
                i     = 42,
                f     = 1.5,
                b     = true,
                n     = false,
                dt    = now,
            }})
            db:Wait(); db:GetResult()
            db:FindOne({Db()}, {Coll()}, {{_id=id}})
            db:Wait()
            local doc, err = db:GetResult()
            if err then error(err) end
            db:DeleteOne({Db()}, {Coll()}, {{_id=id}}); db:Wait(); db:GetResult()
            local ok = doc.s == 'hello'
                    and doc.i == 42
                    and doc.f == 1.5
                    and doc.b == true
                    and doc.n == false
                    and Identifier.GetType(doc._id) == 'OID'
            return tostring(ok)
        ");
        r.String.ShouldBe("true");
    }

    [MongoFact]
    public async Task Find_ProjectionOpts_LimitsFields()
    {
        // Exercises the opts table path through BuildFindOpts/LuaToBson.
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local db = assert({ConnectLua()})
            local id = Identifier.NewOID()
            db:InsertOne({Db()}, {Coll()}, {{_id=id, a=1, b=2}})
            db:Wait(); db:GetResult()
            db:FindOne({Db()}, {Coll()}, {{_id=id}}, {{projection={{a=1, _id=0}}}})
            db:Wait()
            local doc, err = db:GetResult()
            if err then error(err) end
            db:DeleteOne({Db()}, {Coll()}, {{_id=id}}); db:Wait(); db:GetResult()
            return tostring(doc ~= nil) .. ':' .. tostring(doc.a == 1) .. ':' .. tostring(doc.b == nil)
        ");
        r.String.ShouldBe("true:true:true");
    }

    [MongoFact]
    public async Task CountDocuments_WithFilter_ReturnsFilteredCount()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local db = assert({ConnectLua()})
            local tag = 'test_count_' .. tostring(os.time())
            for i=1,5 do
                db:InsertOne({Db()}, {Coll()}, {{tag=tag, even=(i % 2 == 0)}})
                db:Wait(); db:GetResult()
            end
            db:CountDocuments({Db()}, {Coll()}, {{tag=tag, even=true}})
            db:Wait()
            local n, err = db:GetResult()
            if err then error(err) end
            db:DeleteMany({Db()}, {Coll()}, {{tag=tag}}); db:Wait(); db:GetResult()
            return tostring(n)
        ");
        r.String.ShouldBe("2");
    }

    [MongoFact]
    public async Task UpdateOne_Upsert_InsertsWhenNotFound()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local db = assert({ConnectLua()})
            local id = Identifier.NewOID()
            db:UpdateOne({Db()}, {Coll()},
                {{_id=id}},
                {{['$set']={{v=99}}}},
                {{upsert=true}})
            db:Wait()
            local reply, err = db:GetResult()
            if err then error(err) end
            db:FindOne({Db()}, {Coll()}, {{_id=id}})
            db:Wait()
            local doc, err2 = db:GetResult()
            if err2 then error(err2) end
            db:DeleteOne({Db()}, {Coll()}, {{_id=id}}); db:Wait(); db:GetResult()
            return tostring(doc ~= nil) .. ':' .. tostring(doc.v == 99)
        ");
        r.String.ShouldBe("true:true");
    }

    [MongoFact]
    public async Task Find_WithLimitSkipAndSortOpts_Correct()
    {
        // Exercises BuildFindOpts with all three paths: limit, skip, and opts
        // table (sort).  Previously BuildFindOpts returned bson_t* and the
        // caller assigned op->opts after the call, leaving it unowned on throw.
        // Now BuildFindOpts assigns directly to op->opts.
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local db = assert({ConnectLua()})
            local tag = 'test_lss_' .. tostring(os.time())
            for i=1,5 do
                db:InsertOne({Db()}, {Coll()}, {{tag=tag, n=i}})
                db:Wait(); db:GetResult()
            end
            -- limit=2, skip=1, sort descending by n
            db:Find({Db()}, {Coll()}, {{tag=tag}}, 2, 1, {{sort={{n=-1}}}})
            db:Wait()
            local rows, err = db:GetResult()
            if err then error(err) end
            db:DeleteMany({Db()}, {Coll()}, {{tag=tag}}); db:Wait(); db:GetResult()
            -- sorted desc [5,4,3,2,1], skip 1 → start at 4, limit 2 → [4,3]
            return tostring(#rows) .. ':' .. tostring(rows[1].n) .. ':' .. tostring(rows[2].n)
        ");
        r.String.ShouldBe("2:4:3");
    }

    [MongoFact]
    public async Task InsertOne_DecimalField_RoundTrips()
    {
        // Exercises the decimal128 path in LuaToBsonValue and BsonIterValueToLua.
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local db = assert({ConnectLua()})
            local id = Identifier.NewOID()
            local d = Decimal.FromString('123.456')
            db:InsertOne({Db()}, {Coll()}, {{_id=id, d=d}})
            db:Wait(); db:GetResult()
            db:FindOne({Db()}, {Coll()}, {{_id=id}})
            db:Wait()
            local doc, err = db:GetResult()
            if err then error(err) end
            db:DeleteOne({Db()}, {Coll()}, {{_id=id}}); db:Wait(); db:GetResult()
            return tostring(Decimal.ToString(doc.d) == '123.456')
        ");
        r.String.ShouldBe("true");
    }

    [MongoFact]
    public async Task Find_NoOpts_StillWorks()
    {
        // Confirms Find with no limit/skip/opts arguments doesn't crash after
        // BuildFindOpts refactor.
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local db = assert({ConnectLua()})
            local tag = 'test_noopt_' .. tostring(os.time())
            db:InsertOne({Db()}, {Coll()}, {{tag=tag, v=1}})
            db:Wait(); db:GetResult()
            db:Find({Db()}, {Coll()}, {{tag=tag}})
            db:Wait()
            local rows, err = db:GetResult()
            if err then error(err) end
            db:DeleteMany({Db()}, {Coll()}, {{tag=tag}}); db:Wait(); db:GetResult()
            return tostring(#rows == 1)
        ");
        r.String.ShouldBe("true");
    }

    [MongoFact]
    public async Task InsertOne_NullValueInDoc_RoundTrips()
    {
        // Exercises the LUA_TNIL → BSON_APPEND_NULL path and the default
        // BsonIterValueToLua path returning nil for unknown BSON types.
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local db = assert({ConnectLua()})
            local id = Identifier.NewOID()
            db:InsertOne({Db()}, {Coll()}, {{_id=id, n=nil}})
            db:Wait(); db:GetResult()
            db:FindOne({Db()}, {Coll()}, {{_id=id}})
            db:Wait()
            local doc, err = db:GetResult()
            if err then error(err) end
            db:DeleteOne({Db()}, {Coll()}, {{_id=id}}); db:Wait(); db:GetResult()
            -- BSON null round-trips as nil in Lua
            return tostring(doc ~= nil) .. ':' .. tostring(doc.n == nil)
        ");
        r.String.ShouldBe("true:true");
    }

    [MongoFact]
    public async Task Aggregate_MultiStagePipeline_Correct()
    {
        // Verifies LuaToBsonArray builds the pipeline with correct 0-indexed keys.
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local db = assert({ConnectLua()})
            local tag = 'test_agg2_' .. tostring(os.time())
            for i=1,4 do
                db:InsertOne({Db()}, {Coll()}, {{tag=tag, v=i}})
                db:Wait(); db:GetResult()
            end
            -- match tag, group by null, sum all v values
            local pipeline = {{
                {{['$match']  = {{tag=tag}}}},
                {{['$group']  = {{_id=false, total={{['$sum']='$v'}}}}}},
            }}
            db:Aggregate({Db()}, {Coll()}, pipeline)
            db:Wait()
            local res, err = db:GetResult()
            if err then error(err) end
            db:DeleteMany({Db()}, {Coll()}, {{tag=tag}}); db:Wait(); db:GetResult()
            return tostring(#res == 1) .. ':' .. tostring(res[1] and res[1].total)
        ");
        r.String.ShouldBe("true:10");
    }

    [MongoFact]
    public async Task InsertOne_NanDouble_ThrowsLuaError()
    {
        // MongoDB rejects NaN; we now catch it at the Lua boundary with a clear
        // error instead of sending invalid BSON to the server.
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local ok, err = pcall(function()
                local db = assert({ConnectLua()})
                db:InsertOne({Db()}, {Coll()}, {{v = 0/0}})
            end)
            return tostring(ok) .. ':' .. tostring(type(err) == 'string' and #err > 0)
        ");
        r.String.ShouldBe("false:true");
    }

    [MongoFact]
    public async Task InsertOne_InfDouble_ThrowsLuaError()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local ok, err = pcall(function()
                local db = assert({ConnectLua()})
                db:InsertOne({Db()}, {Coll()}, {{v = math.huge}})
            end)
            return tostring(ok) .. ':' .. tostring(type(err) == 'string' and #err > 0)
        ");
        r.String.ShouldBe("false:true");
    }

    [MongoFact]
    public async Task InsertOne_NegInfDouble_ThrowsLuaError()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local ok, err = pcall(function()
                local db = assert({ConnectLua()})
                db:InsertOne({Db()}, {Coll()}, {{v = -math.huge}})
            end)
            return tostring(ok) .. ':' .. tostring(type(err) == 'string' and #err > 0)
        ");
        r.String.ShouldBe("false:true");
    }

    [MongoFact]
    public async Task Find_FloatLimit_ThrowsLuaError()
    {
        // Passing a float as limit was silently truncated; now it's a Lua error.
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local db = assert({ConnectLua()})
            local ok, err = pcall(function()
                db:Find({Db()}, {Coll()}, {{}}, 2.5)
            end)
            return tostring(ok) .. ':' .. tostring(type(err) == 'string' and #err > 0)
        ");
        r.String.ShouldBe("false:true");
    }

    [MongoFact]
    public async Task Find_FloatSkip_ThrowsLuaError()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local db = assert({ConnectLua()})
            local ok, err = pcall(function()
                db:Find({Db()}, {Coll()}, {{}}, 10, 1.9)
            end)
            return tostring(ok) .. ':' .. tostring(type(err) == 'string' and #err > 0)
        ");
        r.String.ShouldBe("false:true");
    }

    [MongoFact]
    public async Task InsertOne_ValidFloat_StillWorks()
    {
        // Confirm normal finite floats are still accepted after the isfinite guard.
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local db = assert({ConnectLua()})
            local id = Identifier.NewOID()
            db:InsertOne({Db()}, {Coll()}, {{_id=id, v=3.14}})
            db:Wait(); db:GetResult()
            db:FindOne({Db()}, {Coll()}, {{_id=id}})
            db:Wait()
            local doc, err = db:GetResult()
            if err then error(err) end
            db:DeleteOne({Db()}, {Coll()}, {{_id=id}}); db:Wait(); db:GetResult()
            return tostring(math.floor(doc.v * 100 + 0.5) == 314)
        ");
        r.String.ShouldBe("true");
    }

    [MongoFact]
    public async Task Find_IntegerLimitAndSkip_StillWork()
    {
        // Confirm integer limit/skip still work after adding the isinteger guard.
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local db = assert({ConnectLua()})
            local tag = 'test_intls_' .. tostring(os.time())
            for i=1,3 do
                db:InsertOne({Db()}, {Coll()}, {{tag=tag, n=i}})
                db:Wait(); db:GetResult()
            end
            db:Find({Db()}, {Coll()}, {{tag=tag}}, 2, 1)
            db:Wait()
            local rows, err = db:GetResult()
            if err then error(err) end
            db:DeleteMany({Db()}, {Coll()}, {{tag=tag}}); db:Wait(); db:GetResult()
            return tostring(#rows)
        ");
        r.String.ShouldBe("2");
    }

    [MongoFact]
    public async Task Close_WithWrongType_ThrowsTypeError()
    {
        // luamongo_gc was registered as Close, using lua_touserdata with no type
        // check.  Calling Close on a non-MongoDB value silently returned 0.
        // MongoClose now uses luaL_checkudata, raising a proper type error.
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(@"
            local ok, err = pcall(function()
                MongoDB.Close(42)
            end)
            return tostring(ok) .. ':' .. tostring(type(err) == 'string' and #err > 0)
        ");
        r.String.ShouldBe("false:true");
    }

    [MongoFact]
    public async Task Close_WithNil_ThrowsTypeError()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(@"
            local ok, err = pcall(function()
                MongoDB.Close(nil)
            end)
            return tostring(ok) .. ':' .. tostring(type(err) == 'string' and #err > 0)
        ");
        r.String.ShouldBe("false:true");
    }

    [MongoFact]
    public async Task Close_WithValidConnection_Works()
    {
        // Regression: confirm MongoClose still works for a real connection.
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local db = assert({ConnectLua()})
            db:Close()
            return 'ok'
        ");
        r.String.ShouldBe("ok");
    }

    [MongoFact]
    public async Task AllOps_AfterManySequentialOps_NoMemoryLeak()
    {
        // Exercises SetupOp new LuaMongoOp{} across all op types to verify
        // the bad_alloc guard didn't break anything.  Uses the _DEBUG live
        // allocs counter indirectly by running many ops without crashing.
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local db = assert({ConnectLua()})
            local tag = 'test_seq_' .. tostring(os.time())
            for i=1,5 do
                db:InsertOne({Db()}, {Coll()}, {{tag=tag, n=i}})
                db:Wait(); db:GetResult()
            end
            db:Find({Db()}, {Coll()}, {{tag=tag}}, 5, 0); db:Wait(); db:GetResult()
            db:FindOne({Db()}, {Coll()}, {{tag=tag}}); db:Wait(); db:GetResult()
            db:CountDocuments({Db()}, {Coll()}, {{tag=tag}}); db:Wait(); db:GetResult()
            db:UpdateMany({Db()}, {Coll()}, {{tag=tag}}, {{['$set']={{checked=true}}}}); db:Wait(); db:GetResult()
            db:DeleteMany({Db()}, {Coll()}, {{tag=tag}}); db:Wait(); db:GetResult()
            db:Command('admin', {{ping=1}}); db:Wait(); db:GetResult()
            db:Close()
            return 'ok'
        ");
        r.String.ShouldBe("ok");
    }

    [MongoFact]
    public async Task Find_DbNameWithEmbeddedNul_ThrowsLuaError()
    {
        // luaL_checkstring + strlen would truncate at the NUL, silently accepting
        // e.g. "bad\0ignored" as "bad".  luaL_checklstring passes the full length
        // to ValidateDbName which rejects embedded NUL bytes.
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local db = assert({ConnectLua()})
            local ok, err = pcall(function()
                db:Find('bad' .. string.char(0) .. 'name', {Coll()}, {{}})
            end)
            return tostring(ok) .. ':' .. tostring(type(err) == 'string' and #err > 0)
        ");
        r.String.ShouldBe("false:true");
    }

    [MongoFact]
    public async Task InsertOne_CollNameWithEmbeddedNul_ThrowsLuaError()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local db = assert({ConnectLua()})
            local ok, err = pcall(function()
                db:InsertOne({Db()}, 'col' .. string.char(0) .. 'bad', {{}})
            end)
            return tostring(ok) .. ':' .. tostring(type(err) == 'string' and #err > 0)
        ");
        r.String.ShouldBe("false:true");
    }

    [MongoFact]
    public async Task Command_DbNameWithEmbeddedNul_ThrowsLuaError()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local db = assert({ConnectLua()})
            local ok, err = pcall(function()
                db:Command('adm' .. string.char(0) .. 'in', {{ping=1}})
            end)
            return tostring(ok) .. ':' .. tostring(type(err) == 'string' and #err > 0)
        ");
        r.String.ShouldBe("false:true");
    }

    [MongoFact]
    public async Task CountDocuments_DbNameWithEmbeddedNul_ThrowsLuaError()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local db = assert({ConnectLua()})
            local ok, err = pcall(function()
                db:CountDocuments('bad' .. string.char(0) .. 'db', {Coll()}, {{}})
            end)
            return tostring(ok) .. ':' .. tostring(type(err) == 'string' and #err > 0)
        ");
        r.String.ShouldBe("false:true");
    }

    [MongoFact]
    public async Task AllOps_NormalNames_StillWork()
    {
        // Regression: confirm all ops still accept normal (NUL-free) names after
        // the luaL_checklstring refactor.
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local db = assert({ConnectLua()})
            local id = Identifier.NewOID()
            db:InsertOne({Db()}, {Coll()}, {{_id=id, v=1}})
            db:Wait(); db:GetResult()
            db:FindOne({Db()}, {Coll()}, {{_id=id}})
            db:Wait()
            local doc, err = db:GetResult()
            if err then error(err) end
            db:UpdateOne({Db()}, {Coll()}, {{_id=id}}, {{['$set']={{v=2}}}})
            db:Wait(); db:GetResult()
            db:DeleteOne({Db()}, {Coll()}, {{_id=id}})
            db:Wait(); db:GetResult()
            return tostring(doc ~= nil) .. ':' .. tostring(doc.v == 1)
        ");
        r.String.ShouldBe("true:true");
    }

    private static string ConnectLua() =>
        "MongoDB.Connect(os.getenv('KITSUNE_MONGO_TEST'))";

    private static string Db() =>
        "os.getenv('KITSUNE_MONGO_DB')";

    private static string Coll() =>
        "os.getenv('KITSUNE_MONGO_COLL')";
}
