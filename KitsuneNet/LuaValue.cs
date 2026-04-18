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

        /// <summary>Entries for <see cref="LuaType.Table"/> snapshot values (including backward-compat
        /// tables created via <see cref="FromTable"/>). Null for live table refs and non-table types.</summary>
        public IReadOnlyList<KeyValuePair<LuaValue, LuaValue>>? Table { get; init; }

        /// <summary>Live reference to a Lua table for <see cref="LuaType.Table"/> values returned
        /// by the engine.  Holds a Lua registry anchor; must be disposed when no longer needed.
        /// Use <see cref="LuaTableRef.GetContents"/> to snapshot the table's contents, and
        /// <see cref="LuaTableRef.SetContents"/> to replace them. Null for all other types.</summary>
        public LuaTableRef? TableRef { get; init; }

        /// <summary>Parsed JSON node for <see cref="LuaType.Json"/> values. Null for all other types.</summary>
        public JsonNode? JsonNode { get; init; }

        /// <summary>Live reference to a Lua function for <see cref="LuaType.Function"/> values returned
        /// by the engine.  Holds a Lua registry anchor; must be disposed when no longer needed.
        /// Null for all other types and for function values embedded inside tables.</summary>
        public LuaFunctionRef? FunctionRef { get; init; }

        /// <summary>Live reference to a Lua coroutine thread for <see cref="LuaType.Thread"/> values
        /// returned by the engine.  Holds a Lua registry anchor; must be disposed when no longer needed.
        /// Null for all other types and for thread values embedded inside tables.</summary>
        public LuaThreadRef? ThreadRef { get; init; }

        /// <summary>Live reference to a Lua userdata for <see cref="LuaType.Userdata"/> values
        /// returned by the engine.  Holds a Lua registry anchor; must be disposed when no longer needed.
        /// Passing the owning <see cref="LuaValue"/> back to the engine pushes the original Lua object,
        /// preserving identity. Null when type is not Userdata.</summary>
        public LuaUserdataRef? UserdataRef { get; init; }

        /// <summary>Stream for <see cref="LuaType.Stream"/> values.
        /// <summary>Delegate for <see cref="LuaType.CFunction"/> values.
        /// When passed to the engine the delegate is wrapped as an anonymous Lua closure
        /// without being registered in the global table. Null for all other types.</summary>
        public LuaFunction? CFunctionValue { get; init; }

        /// <summary>Iterator source for <see cref="LuaType.Iterator"/> values.
        /// Holds the <see cref="LuaIteratorRef"/> wrapping the underlying sequence.
        /// Null for all other types.</summary>
        public LuaIteratorRef? IteratorValue { get; init; }

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

        /// <summary>GCHandle address for <see cref="LuaType.Userdata"/> instances created via
        /// <see cref="KitsuneEngine.CreateUserdata{T}"/>. Zero for unregistered userdatas
        /// and all other types. Used internally to round-trip the managed instance through Lua.</summary>
        internal nint UserdataGCHandlePtr { get; init; }

        public static implicit operator LuaValue(double v) => FromNumber(v);

        public static implicit operator LuaValue(bool v) => FromBool(v);

        public static implicit operator LuaValue(string? v) => FromString(v);

        public static implicit operator LuaValue(byte[]? v) => FromBytes(v);

        public static implicit operator LuaValue(JsonNode? v) => FromJson(v);

        /// <summary>Returns the C# instance for a Kitsune-registered <see cref="LuaType.Userdata"/>
        /// value, or <c>null</c> for unregistered userdatas and all other types.</summary>
        public object? GetUserdata() =>
            UserdataGCHandlePtr != 0 ? System.Runtime.InteropServices.GCHandle.FromIntPtr(UserdataGCHandlePtr).Target : null;

        /// <summary>Typed convenience wrapper over <see cref="GetUserdata"/>.
        /// Returns <c>null</c> when the instance is not a <typeparamref name="T"/>.</summary>
        /// <typeparam name="T">The expected userdata type. Must match the type argument used to register the instance.</typeparam>
        public T? GetUserdata<T>()
            where T : class => GetUserdata() as T;

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
            LuaType.CFunction => "cfunction",
            LuaType.Iterator => "iterator",
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
        /// <summary>Returns this value unchanged, or throws <see cref="LuaException"/> when
        /// <see cref="Type"/> is <see cref="LuaType.Error"/>. Called by the synchronous
        /// <c>Run*</c> methods to surface native rejections as exceptions rather than
        /// silent <see cref="None"/> returns.</summary>
        /// <exception cref="LuaException">Thrown when <see cref="Type"/> is <see cref="LuaType.Error"/>.</exception>
        public LuaValue GetOrThrow()
        {
            if (Type == LuaType.Error)
            {
                throw new LuaException(String ?? string.Empty);
            }

            return this;
        }

        public JsonNode? AsJsonNode()
        {
            if (Type == LuaType.Json)
            {
                return JsonNode;
            }

            if (Type == LuaType.Table)
            {
                if (TableRef is { } tr)
                {
                    // Live ref — snapshot contents on demand.
                    return TableContentsToJsonNode(tr.GetContents());
                }

                // Backward-compat snapshot (e.g. from callback arg or LuaValue.FromTable).
                return Table is not null ? TableContentsToJsonNode(Table) : new JsonObject();
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

        /// <summary>Creates an error value carrying <paramref name="message"/>.
        /// When returned from a <see cref="LuaFunction"/> the engine converts it to a Lua error
        /// via the result-setter, raising it in the calling coroutine. On the C# side
        /// <see cref="GetOrThrow"/> surfaces it as a <see cref="LuaException"/>.</summary>
        public static LuaValue FromError(string message) =>
            new() { Type = LuaType.Error, Bytes = Encoding.UTF8.GetBytes(message) };

        /// <summary>Creates a table value from a list of key-value entries.</summary>
        public static LuaValue FromTable(IReadOnlyList<KeyValuePair<LuaValue, LuaValue>> entries) =>
            new() { Type = LuaType.Table, Table = entries };

        /// <summary>Creates a JSON value that is decoded to a Lua table when pushed into the engine.
        /// On the C# side the node is stored directly; no serialisation occurs until the value is
        /// marshalled across the bridge.</summary>
        public static LuaValue FromJson(JsonNode? node) =>
            node is null ? None : new() { Type = LuaType.Json, JsonNode = node };

        /// <summary>Creates an anonymous Lua function value from a C# delegate.
        /// When passed to the engine the delegate is wrapped as an anonymous Lua closure
        /// without being registered in the global table. The engine keeps the delegate
        /// alive until it is shut down via <see cref="KitsuneEngine.Dispose"/>.</summary>
        public static LuaValue FromCFunction(LuaFunction func) =>
            new() { Type = LuaType.CFunction, CFunctionValue = func };

        /// <summary>Creates an iterator value from a synchronous sequence.
        /// Lua receives a stateful closure iterable with <c>for v in iter do</c>.
        /// <see cref="IEnumerable{T}.GetEnumerator"/> is called lazily when Lua invokes
        /// the closure for the first time.</summary>
        public static LuaValue FromIterator(IEnumerable<LuaValue> source)
        {
            var iterRef = new LuaIteratorRef(source);
            return new() { Type = LuaType.Iterator, IteratorValue = iterRef };
        }

        /// <summary>Creates an iterator value and returns a control handle for cancellation
        /// or independent C#-side enumeration via <see cref="LuaIteratorRef.Iterator"/>.</summary>
        public static LuaValue FromIterator(IEnumerable<LuaValue> source, out LuaIteratorRef handle)
        {
            handle = new LuaIteratorRef(source);
            return new() { Type = LuaType.Iterator, IteratorValue = handle };
        }

        /// <summary>Creates an iterator value from an async sequence. When passed to Lua the
        /// async source is consumed via <c>ToBlockingEnumerable</c>, which blocks the Lua
        /// scheduler thread per step. Suitable only for fast async sources.</summary>
        public static LuaValue FromIterator(IAsyncEnumerable<LuaValue> source)
        {
            var iterRef = new LuaIteratorRef(source);
            return new() { Type = LuaType.Iterator, IteratorValue = iterRef };
        }

        /// <inheritdoc cref="FromIterator(IAsyncEnumerable{LuaValue})"/>
        public static LuaValue FromIterator(IAsyncEnumerable<LuaValue> source, out LuaIteratorRef handle)
        {
            handle = new LuaIteratorRef(source);
            return new() { Type = LuaType.Iterator, IteratorValue = handle };
        }

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
            ReferenceEquals(ThreadRef, other.ThreadRef) &&
            ReferenceEquals(CFunctionValue, other.CFunctionValue) &&
            ReferenceEquals(IteratorValue, other.IteratorValue) &&
            UserdataGCHandlePtr == other.UserdataGCHandlePtr;

        public override int GetHashCode()
        {
            var hash = default(System.HashCode);
            hash.Add(Type);
            hash.Add(Number);
            hash.Add(Int64);
            hash.Add(Boolean);
            if (Bytes is not null)
            {
                foreach (byte b in Bytes)
                {
                    hash.Add(b);
                }
            }
            hash.Add(Table);
            hash.Add(JsonNode);
            hash.Add(FunctionRef);
            hash.Add(ThreadRef);
            hash.Add(CFunctionValue);
            hash.Add(IteratorValue);
            hash.Add(UserdataGCHandlePtr);
            return hash.ToHashCode();
        }

        private static JsonNode? TableToJsonNode(LuaValue v) =>
            TableContentsToJsonNode(v.Table ?? Array.Empty<KeyValuePair<LuaValue, LuaValue>>());

        private static JsonNode? TableContentsToJsonNode(IReadOnlyList<KeyValuePair<LuaValue, LuaValue>> table)
        {
            if (table.Count == 0)
            {
                return new JsonObject();
            }

            // Detect Lua array: all keys are integers and form a sequence 1..n
            bool isArray = table.All(kvp => kvp.Key.Type == LuaType.Integer);
            if (isArray)
            {
                var sorted = table.OrderBy(kvp => kvp.Key.AsInt64).ToList();
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
            foreach (var kvp in table)
            {
                obj[kvp.Key.String ?? kvp.Key.ToString()] = kvp.Value.AsJsonNode();
            }
            return obj;
        }
    }
}
