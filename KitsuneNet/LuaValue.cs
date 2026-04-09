using System;
using System.Collections.Generic;
using System.Text;
using System.Text.Json.Nodes;

namespace KitsuneNet
{
    /// <summary>A typed value exchanged with the Lua engine.</summary>
    public readonly record struct LuaValue
    {
        public LuaType Type { get; init; }

        public double Number { get; init; }

        public long Int64 { get; init; }

        public bool Boolean { get; init; }

        /// <summary>Raw bytes for <see cref="LuaType.String"/> values. Not guaranteed to be valid UTF-8.</summary>
        public byte[]? Bytes { get; init; }

        /// <summary>Entries for <see cref="LuaType.Table"/> values. Null for empty tables or non-table types.</summary>
        public IReadOnlyList<KeyValuePair<LuaValue, LuaValue>>? Table { get; init; }

        /// <summary>Parsed JSON node for <see cref="LuaType.Json"/> values. Null for all other types.</summary>
        public JsonNode? JsonNode { get; init; }

        /// <summary>Live reference to a Lua function for <see cref="LuaType.Function"/> values returned
        /// by the engine.  Holds a Lua registry anchor; must be disposed when no longer needed.
        /// Null for all other types and for function values embedded inside tables.</summary>
        public LuaFunctionRef? FunctionRef { get; init; }

        /// <summary>Stream for <see cref="LuaType.Stream"/> values.
        /// Inbound (Lua → C#): a <see cref="LuaStream"/> wrapping native block memory directly.
        /// Read-only when <c>KITSUNE_SHARED_MEMORY_FLAG_READONLY</c> is set on the block;
        /// read-write otherwise.  <see cref="LuaStream.Dispose"/> calls the block's close callback
        /// to release the Lua registry anchor.
        /// Outbound (C# → Lua): any <see cref="System.IO.Stream"/> whose bytes are copied into
        /// a native block; a <see cref="System.IO.MemoryStream"/> or <see cref="LuaStream"/>
        /// avoids a double-copy. Null for all other types.</summary>
        public System.IO.Stream? StreamValue { get; init; }

        /// <summary>Decodes <see cref="Bytes"/> as UTF-8 for strings, or UTF-16 LE for Wchar values.
        /// Returns <c>null</c> when <see cref="Bytes"/> is null.</summary>
        public string? String => Type == LuaType.Char16
            ? (Bytes is null ? null : Encoding.Unicode.GetString(Bytes))
            : (Bytes is null ? null : Encoding.UTF8.GetString(Bytes));

        /// <summary>Returns the numeric value as <c>double</c>, bridging both
        /// <see cref="LuaType.Number"/> (float) and <see cref="LuaType.Integer"/> subtypes.
        /// Zero for all other types.</summary>
        public double AsDouble => Type == LuaType.Integer ? (double)Int64 : Number;

        /// <summary>Returns the numeric value as <c>long</c>, bridging both
        /// <see cref="LuaType.Integer"/> and <see cref="LuaType.Number"/> (float) subtypes.
        /// Zero for all other types.</summary>
        public long AsInt64 => Type == LuaType.Integer ? Int64 : (long)Number;

        /// <summary>No value / not set.</summary>
        public static LuaValue None => new() { Type = LuaType.None };

        public static implicit operator LuaValue(double v) => FromNumber(v);

        public static implicit operator LuaValue(bool v) => FromBool(v);

        public static implicit operator LuaValue(string? v) => FromString(v);

        public static implicit operator LuaValue(byte[]? v) => FromBytes(v);

        public static implicit operator LuaValue(JsonNode? v) => FromJson(v);

        /// <summary>Returns the most useful string representation of the value.</summary>
        public override string ToString() => Type switch
        {
            LuaType.String => String ?? string.Empty,
            LuaType.Char16 => String ?? string.Empty,
            LuaType.Number => Number.ToString(System.Globalization.CultureInfo.InvariantCulture),
            LuaType.Integer => Int64.ToString(),
            LuaType.Boolean => Boolean.ToString(),
            LuaType.Nil => "nil",
            LuaType.Table => Table is not null ? $"table({Table.Count})" : "table",
            LuaType.Json => JsonNode?.ToJsonString() ?? "null",
            LuaType.Stream => StreamValue?.ToString() ?? "stream(null)",
            LuaType.Function => "function",
            LuaType.Userdata => "userdata",
            LuaType.Thread => "thread",
            LuaType.LightUserdata => "lightuserdata",
            _ => string.Empty,
        };

        /// <summary>
        /// Converts this value to a <see cref="JsonNode"/>.
        /// <list type="bullet">
        /// <item><see cref="LuaType.Json"/>: returns the already-parsed node directly.</item>
        /// <item><see cref="LuaType.Table"/>: walks the linked list; sequential integer keys
        ///   (1, 2, … n) produce a <see cref="JsonArray"/>; all other keys produce a
        ///   <see cref="JsonObject"/> keyed by the string representation of each key.</item>
        /// <item>Scalar types: wrapped in the appropriate <see cref="JsonValue"/>.</item>
        /// <item><see cref="LuaType.Nil"/> / <see cref="LuaType.None"/>: returns <c>null</c>.</item>
        /// </list>
        /// </summary>
        public JsonNode? AsJsonNode()
        {
            if (Type == LuaType.Json)
            {
                return JsonNode;
            }

            if (Type == LuaType.Table)
            {
                return TableToJsonNode(this);
            }
            return Type switch
            {
                LuaType.String => JsonValue.Create(String),
                LuaType.Number => JsonValue.Create(Number),
                LuaType.Integer => JsonValue.Create(Int64),
                LuaType.Boolean => JsonValue.Create(Boolean),
                _ => null,
            };
        }

        public static LuaValue FromNumber(double v) => new() { Type = LuaType.Number, Number = v };

        public static LuaValue FromBool(bool v) => new() { Type = LuaType.Boolean, Boolean = v };

        public static LuaValue FromInt64(long v) => new() { Type = LuaType.Integer, Int64 = v };

        /// <summary>Creates a string value by UTF-8 encoding <paramref name="v"/>.</summary>
        public static LuaValue FromString(string? v) =>
            v is null ? None : new() { Type = LuaType.String, Bytes = Encoding.UTF8.GetBytes(v) };

        /// <summary>Creates a string value from a raw byte array with no encoding applied.</summary>
        public static LuaValue FromBytes(byte[]? v) =>
            v is null ? None : new() { Type = LuaType.String, Bytes = v };

        /// <summary>Creates a Wchar value. When set on a Lua global the engine pushes a Lua Wchar object.
        /// The UTF-16 LE encoded text is stored in <see cref="Bytes"/>; <see cref="String"/> decodes it back.</summary>
        public static LuaValue FromWchar(string? v) =>
            v is null ? None : new() { Type = LuaType.Char16, Bytes = Encoding.Unicode.GetBytes(v) };

        /// <summary>Creates a table value from a list of key-value entries.</summary>
        public static LuaValue FromTable(IReadOnlyList<KeyValuePair<LuaValue, LuaValue>> entries) =>
            new() { Type = LuaType.Table, Table = entries };

        /// <summary>Creates a JSON value that is decoded to a Lua table when pushed into the engine.
        /// On the C# side the node is stored directly; no serialisation occurs until the value is
        /// marshalled across the bridge.</summary>
        public static LuaValue FromJson(JsonNode? node) =>
            node is null ? None : new() { Type = LuaType.Json, JsonNode = node };

        /// <summary>Creates a stream value from a raw byte array. The bytes are copied into
        /// a native <c>SharedMemoryBlock</c> when passed across the bridge.</summary>
        public static LuaValue FromStream(byte[] data)
            => new() { Type = LuaType.Stream, StreamValue = new System.IO.MemoryStream(data, writable: false) };

        /// <summary>Creates a stream value from any <see cref="System.IO.Stream"/>.
        /// The stream is read from its current position (or from the beginning when seekable)
        /// and the bytes are copied into a native block when passed across the bridge.</summary>
        public static LuaValue FromStream(System.IO.Stream stream)
            => new() { Type = LuaType.Stream, StreamValue = stream };

        /// <summary>Content-based equality: <see cref="Bytes"/> arrays are compared element-by-element.</summary>
        public bool Equals(LuaValue other) =>
            Type == other.Type &&
            Number == other.Number &&
            Int64 == other.Int64 &&
            Boolean == other.Boolean &&
            (Bytes is null ? other.Bytes is null : other.Bytes is not null && Bytes.AsSpan().SequenceEqual(other.Bytes)) &&
            ReferenceEquals(Table, other.Table) &&
            ReferenceEquals(JsonNode, other.JsonNode) &&
            ReferenceEquals(FunctionRef, other.FunctionRef) &&
            ReferenceEquals(StreamValue, other.StreamValue);

        public override int GetHashCode()
        {
            var hash = new System.HashCode();
            hash.Add(Type);
            hash.Add(Number);
            hash.Add(Int64);
            hash.Add(Boolean);
            if (Bytes is not null)
            {
                foreach (byte b in Bytes)
                    hash.Add(b);
            }
            hash.Add(Table);
            hash.Add(JsonNode);
            hash.Add(FunctionRef);
            hash.Add(StreamValue);
            return hash.ToHashCode();
        }

        private static JsonNode? TableToJsonNode(LuaValue v)
        {
            if (v.Table is null || v.Table.Count == 0)
            {
                return new JsonObject();
            }

            // Detect Lua array: all keys are integers and form a sequence 1..n
            bool isArray = v.Table.All(kvp => kvp.Key.Type == LuaType.Integer);
            if (isArray)
            {
                var sorted = v.Table.OrderBy(kvp => kvp.Key.AsInt64).ToList();
                bool sequential = sorted.Select((kvp, i) => kvp.Key.AsInt64 == i + 1).All(b => b);
                if (sequential)
                {
                    var arr = new JsonArray();
                    foreach (var kvp in sorted)
                    {
                        arr.Add(kvp.Value.AsJsonNode());
                    }
                    return arr;
                }
            }

            var obj = new JsonObject();
            foreach (var kvp in v.Table)
            {
                obj[kvp.Key.String ?? kvp.Key.ToString()] = kvp.Value.AsJsonNode();
            }
            return obj;
        }
    }
}
