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

        /// <summary>Table (LUA_TTABLE). Value is not bridgeable; <see cref="LuaValue.Bytes"/> will be null.</summary>
        Table = 5,

        /// <summary>Function (LUA_TFUNCTION). Value is not bridgeable; <see cref="LuaValue.Bytes"/> will be null.</summary>
        Function = 6,

        /// <summary>Full userdata (LUA_TUSERDATA). Value is not bridgeable; <see cref="LuaValue.Bytes"/> will be null.</summary>
        Userdata = 7,

        /// <summary>Coroutine thread (LUA_TTHREAD). Value is not bridgeable; <see cref="LuaValue.Bytes"/> will be null.</summary>
        Thread = 8,

        /// <summary>Lua 5.3+ integer subtype. Value is stored in <see cref="LuaValue.Int64"/>; never a float.</summary>
        Integer = -3,

        /// <summary>Kitsune Wchar userdata. UTF-8 bytes are stored in <see cref="LuaValue.Bytes"/>;
        /// <see cref="LuaValue.String"/> decodes them. Pushes a Lua Wchar object back into the state.</summary>
        Wchar = -4,

        /// <summary>JSON value bridged via the engine's Json instance. C# holds a
        /// <see cref="System.Text.Json.Nodes.JsonNode"/>; Lua receives/sends a table decoded/encoded
        /// by <c>Json:Decode</c> / <c>Json:Encode</c>. Only meaningful as input (C# → Lua);
        /// Lua → C# tables still arrive as <see cref="Table"/> and can be converted with
        /// <see cref="LuaValue.AsJsonNode"/>.</summary>
        Json = -5,

        /// <summary>Shared-memory stream block (KITSUNE_TSTREAM = -6).
        /// When received from the engine the value's <see cref="LuaValue.StreamValue"/> is a
        /// <see cref="LuaStream"/> that directly addresses the native block with zero copy;
        /// <see cref="LuaStream.Dispose"/> calls the block's close callback to release the
        /// Lua registry anchor. Use <see cref="LuaValue.FromStream(byte[])"/> or
        /// <see cref="LuaValue.FromStream(System.IO.Stream)"/> to send a stream to Lua.</summary>
        Stream = -6,

        /// <summary>Error returned by the blocking execute functions (KITSUNE_TERROR = -2) when
        /// the call was rejected — e.g. called from the scheduler thread, from a
        /// <c>kitsune_CFunction</c> callback, or re-entrantly from the same thread.
        /// <see cref="LuaValue.String"/> contains the error message.
        /// The blocking public methods (<see cref="KitsuneEngine.ExecuteFileBlocking"/> etc.)
        /// convert this into a <see cref="LuaException"/> automatically.</summary>
        Error = -2,
    }
}
