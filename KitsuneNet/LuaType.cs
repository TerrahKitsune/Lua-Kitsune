using System;
using System.Collections.Generic;
using System.Text;

namespace KitsuneNet
{
    /// <summary>Lua value types. Values match Lua's internal LUA_T* constants.</summary>
    public enum LuaType
    {
        /// <summary>No value / key not set (LUA_TNONE).</summary>
        None = -1,

        /// <summary>Explicit nil (LUA_TNIL).</summary>
        Nil = 0,

        /// <summary>Boolean (LUA_TBOOLEAN).</summary>
        Boolean = 1,

        /// <summary>Light userdata — a raw pointer not managed by Lua (LUA_TLIGHTUSERDATA).</summary>
        LightUserdata = 2,

        /// <summary>Number — integer or float (LUA_TNUMBER).</summary>
        Number = 3,

        /// <summary>String (LUA_TSTRING).</summary>
        String = 4,

        /// <summary>Table (LUA_TTABLE). When returned by the engine the value's <see cref="LuaValue.TableRef"/>
        /// holds a live registry reference; dispose it when done.
        /// When passed to the engine with <see cref="LuaValue.Table"/> set, the snapshot is sent as
        /// <see cref="TableContents"/> automatically by the bridge for backward compatibility.</summary>
        Table = 5,

        /// <summary>Function (LUA_TFUNCTION). Value is not bridgeable; <see cref="LuaValue.Bytes"/> will be null.</summary>
        Function = 6,

        /// <summary>Full userdata (LUA_TUSERDATA). Value is not bridgeable; <see cref="LuaValue.Bytes"/> will be null.</summary>
        Userdata = 7,

        /// <summary>Coroutine thread (LUA_TTHREAD). Value is not bridgeable; <see cref="LuaValue.Bytes"/> will be null.</summary>
        Thread = 8,

        /// <summary>Lua 5.3+ integer subtype. Value is stored in <see cref="LuaValue.Int64"/>; never a float.</summary>
        Integer = -3,

        /// <summary>Kitsune Wchar userdata. UTF-16 LE bytes are stored in <see cref="LuaValue.Bytes"/>;
        /// <see cref="LuaValue.String"/> decodes them. Pushes a Lua Wchar object back into the state.</summary>
        Char16 = -4,

        /// <summary>JSON value bridged via the engine's Json instance. C# holds a
        /// <see cref="System.Text.Json.Nodes.JsonNode"/>; Lua receives/sends a table decoded/encoded
        /// by <c>Json:Decode</c> / <c>Json:Encode</c>. Only meaningful as input (C# → Lua);
        /// Lua → C# tables still arrive as <see cref="Table"/> and can be converted with
        /// <see cref="LuaValue.AsJsonNode"/>.</summary>
        Json = -5,

        /// <summary>Inline C# function value (KITSUNE_TCFUNCTION = -6).
        /// When passed to the engine the function is wrapped as an anonymous Lua closure
        /// without being registered in the global table.
        /// Never returned by the engine; only valid for inbound (C# → Lua) use via
        /// <see cref="LuaValue.FromCFunction"/>.</summary>
        CFunction = -6,

        /// <summary>Stateful iterator (KITSUNE_TITERATOR = -7).
        /// Outbound (C# → Lua) only. Lua receives a callable closure consumable with
        /// <c>for v in iter do</c>. <see cref="IEnumerator{T}.GetEnumerator"/> is called
        /// lazily when Lua invokes the closure for the first time.
        /// Never returned by the engine. Create via <see cref="LuaValue.FromIterator"/>.</summary>
        Iterator = -7,

        /// <summary>Snapshot of a Lua table's key-value pairs (KITSUNE_TTABLECONTENTS = -8).
        /// Produced by <see cref="LuaTableRef.GetContents"/> and consumed by
        /// <see cref="LuaTableRef.SetContents"/>.  <see cref="LuaValue.Table"/> holds the entries.
        /// Passing a <see cref="LuaValue"/> with this type to the engine creates a new Lua table
        /// populated from the snapshot.</summary>
        TableContents = -8,

        /// <summary>Unsigned 64-bit integer (KITSUNE_TUINT = -9). The raw uint64 bit pattern is
        /// stored in <see cref="LuaValue.UInt64"/>. Values that fit in a signed int64 can also
        /// be read via <see cref="LuaValue.Int64"/> (same bit pattern). Pushes a Lua UInt userdata
        /// back into the engine.</summary>
        UInt = -9,

        /// <summary>DateTime userdata (KITSUNE_TDATETIME = -10). Bridged via a heap-allocated
        /// <c>KitsuneDateTime { int64_t ticks; int16_t offset_minutes }</c> struct.
        /// Use <see cref="LuaValue.AsDateTimeOffset"/> to decode. Pushes a Lua DateTime
        /// userdata back into the engine.</summary>
        DateTime = -10,

        /// <summary>TimeSpan userdata (KITSUNE_TTIMESPAN = -11). Bridged via a heap-allocated
        /// <c>KitsuneTimeSpan { int64_t ticks }</c> struct.
        /// Use <see cref="LuaValue.AsTimeSpan"/> to decode. Pushes a Lua TimeSpan userdata
        /// back into the engine.</summary>
        TimeSpan = -11,

        /// <summary>Decimal userdata (KITSUNE_TDECIMAL = -12). Bridged via a heap-allocated
        /// <c>KitsuneDecimal { uint64_t lo; uint64_t hi; int16_t scale; uint8_t negative }</c> struct.
        /// Use <see cref="LuaValue.AsDecimal"/> to decode to a <see cref="decimal"/>.
        /// Pushes a Lua Decimal userdata back into the engine.</summary>
        Decimal = -12,

        /// <summary>Identifier userdata (KITSUNE_TIDENTIFIER = -13). Bridged via a heap-allocated
        /// <c>KitsuneIdentifier { uint8_t type; uint8_t bytes[16] }</c> struct where type 0=UUID, 1=OID.
        /// Use <see cref="LuaValue.AsGuid"/> for UUID or <see cref="LuaValue.AsIdentifierBytes"/>
        /// for raw access. Pushes a Lua Identifier userdata back into the engine.</summary>
        Identifier = -13,

        /// <summary>Error returned by the blocking execute functions (KITSUNE_TERROR = -2) when
        /// the call was rejected — e.g. called from the scheduler thread, from a
        /// <c>kitsune_CFunction</c> callback, or re-entrantly from the same thread.
        /// <see cref="LuaValue.String"/> contains the error message.
        /// The blocking public methods (<see cref="KitsuneEngine.ExecuteFileBlocking"/> etc.)
        /// convert this into a <see cref="LuaException"/> automatically.</summary>
        Error = -2,
    }
}
