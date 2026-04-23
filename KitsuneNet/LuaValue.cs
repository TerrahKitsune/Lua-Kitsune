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

        /// <summary>Raw uint64 bit pattern for <see cref="LuaType.UInt"/> values. Zero for all other types.
        /// Stored in the same union field as <see cref="Int64"/>; cast accordingly.</summary>
        public ulong UInt64 => (ulong)Int64;

        /// <summary>Decodes the 10-byte blob for <see cref="LuaType.DateTime"/> values into a
        /// <see cref="DateTimeOffset"/>. The blob is { int64 ticks LE, int16 offset_minutes LE }.
        /// Returns <see cref="DateTimeOffset.MinValue"/> for non-DateTime types or malformed blobs.</summary>
        public DateTimeOffset AsDateTimeOffset
        {
            get
            {
                if (Type != LuaType.DateTime || Bytes is null || Bytes.Length < 10)
                {
                    return DateTimeOffset.MinValue;
                }

                long ticks = System.Runtime.InteropServices.MemoryMarshal.Read<long>(Bytes.AsSpan(0, 8));
                short offsetMinutes = System.Runtime.InteropServices.MemoryMarshal.Read<short>(Bytes.AsSpan(8, 2));

                // Ticks are stored relative to 0001-01-01 (same as .NET DateTime).
                var dto = new DateTimeOffset(ticks, TimeSpan.FromMinutes(offsetMinutes));
                return dto;
            }
        }

        /// <summary>Decodes the inline int64 tick count for <see cref="LuaType.TimeSpan"/> values
        /// into a <see cref="System.TimeSpan"/>. Returns <see cref="System.TimeSpan.Zero"/> for
        /// non-TimeSpan types.</summary>
        public System.TimeSpan AsTimeSpan => Type == LuaType.TimeSpan
            ? System.TimeSpan.FromTicks(Int64)
            : System.TimeSpan.Zero;

        /// <summary>Decodes the 16-byte raw LuaDecimal blob for <see cref="LuaType.Decimal"/> values
        /// into a .NET <see cref="decimal"/>. Layout: uint64 lo, uint64 hi, int16 scale, uint8 negative.
        /// Returns <c>0m</c> for non-Decimal types or malformed blobs.</summary>
        public decimal AsDecimal
        {
            get
            {
                if (Type != LuaType.Decimal || Bytes is null || Bytes.Length < 19)
                {
                    return 0m;
                }

                ulong lo64 = System.Runtime.InteropServices.MemoryMarshal.Read<ulong>(Bytes.AsSpan(0, 8));
                ulong hi64 = System.Runtime.InteropServices.MemoryMarshal.Read<ulong>(Bytes.AsSpan(8, 8));
                short scale = System.Runtime.InteropServices.MemoryMarshal.Read<short>(Bytes.AsSpan(16, 2));
                byte neg = Bytes[18];
                int lo32 = (int)(lo64 & 0xFFFFFFFF);
                int mid32 = (int)(lo64 >> 32);
                int hi32 = (int)(hi64 & 0xFFFFFFFF);
                byte clampedScale = scale < 0 ? (byte)0 : scale > 28 ? (byte)28 : (byte)scale;
                return new decimal(lo32, mid32, hi32, neg != 0, clampedScale);
            }
        }

        /// <summary>Decodes the 17-byte blob for <see cref="LuaType.Identifier"/> values (type=UUID)
        /// into a <see cref="Guid"/>. Returns <see cref="Guid.Empty"/> for OID types, non-Identifier
        /// types, or malformed blobs.</summary>
        public Guid AsGuid
        {
            get
            {
                if (Type != LuaType.Identifier || Bytes is null || Bytes.Length < 17 || Bytes[0] != 0)
                {
                    return Guid.Empty;
                }

                return new Guid(Bytes.AsSpan(1, 16));
            }
        }

        /// <summary>Returns the raw identifier bytes (16 bytes) for <see cref="LuaType.Identifier"/>
        /// values, or <c>null</c> for other types. Byte 0 of <see cref="Bytes"/> is the type
        /// discriminator (0=UUID, 1=OID); bytes 1–16 are the raw identifier data.</summary>
        public byte[]? AsIdentifierBytes => Type == LuaType.Identifier && Bytes is { Length: >= 17 }
            ? Bytes[1..17]
            : null;

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
        public double AsDouble => Type == LuaType.Integer ? (double)Int64 : Type == LuaType.UInt ? (double)UInt64 : Type == LuaType.TimeSpan ? (double)Int64 : Number;

        /// <summary>Returns the numeric value as <c>long</c>, bridging both
        /// <see cref="LuaType.Integer"/> and <see cref="LuaType.Number"/> (float) subtypes.
        /// For <see cref="LuaType.TimeSpan"/> returns the raw tick count.
        /// Zero for all other types.</summary>
        public long AsInt64 => Type == LuaType.Integer ? Int64 : Type == LuaType.UInt ? Int64 : Type == LuaType.TimeSpan ? Int64 : (long)Number;

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
            LuaType.UInt => UInt64.ToString(),
            LuaType.TimeSpan => AsTimeSpan.ToString("c", System.Globalization.CultureInfo.InvariantCulture),
            LuaType.DateTime => AsDateTimeOffset.ToString("O", System.Globalization.CultureInfo.InvariantCulture),
            LuaType.Decimal => AsDecimal.ToString(System.Globalization.CultureInfo.InvariantCulture),
            LuaType.Identifier => AsGuid.ToString("D"),
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

        /// <summary>Creates an unsigned 64-bit integer value. Pushes a Lua UInt userdata into the engine.
        /// Values that fit in a signed int64 can also be created via <see cref="FromInt64"/>.</summary>
        public static LuaValue FromUInt64(ulong v) => new() { Type = LuaType.UInt, Int64 = (long)v };

        /// <summary>Creates a DateTime value from a <see cref="DateTimeOffset"/>.
        /// Pushes a Lua DateTime userdata into the engine with the full tick precision and UTC offset.</summary>
        public static LuaValue FromDateTime(DateTimeOffset v)
        {
            var blob = new byte[10];
            System.Runtime.InteropServices.MemoryMarshal.Write(blob.AsSpan(0, 8), v.Ticks);
            short offsetMinutes = (short)v.Offset.TotalMinutes;
            System.Runtime.InteropServices.MemoryMarshal.Write(blob.AsSpan(8, 2), offsetMinutes);
            return new() { Type = LuaType.DateTime, Bytes = blob };
        }

        /// <summary>Creates a TimeSpan value from a <see cref="System.TimeSpan"/>.
        /// Pushes a Lua TimeSpan userdata into the engine. Stored inline (no heap allocation).</summary>
        public static LuaValue FromTimeSpan(System.TimeSpan v) =>
            new() { Type = LuaType.TimeSpan, Int64 = v.Ticks };

        /// <summary>Creates a Decimal value from a <see cref="decimal"/>.
        /// Pushes a Lua Decimal userdata into the engine. The .NET decimal is encoded as the
        /// 16-byte LuaDecimal binary format.</summary>
        public static LuaValue FromDecimal(decimal v)
        {
            var ints = decimal.GetBits(v);

            // sizeof(LuaDecimal) in MSVC x64 = 24 (uint64 lo+hi at offsets 0,8; int16 scale at 16; uint8 neg at 18; 5 pad bytes to align to 8)
            var blob = new byte[24];
            ulong lo64 = (uint)ints[0] | ((ulong)(uint)ints[1] << 32);
            ulong hi64 = (uint)ints[2]; // hi32 only; top 32 bits unused by .NET decimal
            byte neg = (ints[3] & unchecked((int)0x80000000)) != 0 ? (byte)1 : (byte)0;
            short scale = (short)((ints[3] >> 16) & 0xFF);
            System.Runtime.InteropServices.MemoryMarshal.Write(blob.AsSpan(0, 8), lo64);
            System.Runtime.InteropServices.MemoryMarshal.Write(blob.AsSpan(8, 8), hi64);
            System.Runtime.InteropServices.MemoryMarshal.Write(blob.AsSpan(16, 2), scale);
            blob[18] = neg;

            // bytes 19-23 = padding, already 0
            return new() { Type = LuaType.Decimal, Bytes = blob };
        }

        /// <summary>Creates an Identifier value from a <see cref="Guid"/> (UUID type).
        /// Pushes a Lua Identifier userdata into the engine.</summary>
        public static LuaValue FromGuid(Guid v)
        {
            var blob = new byte[17];
            blob[0] = 0; // IDENTIFIER_UUID
            v.TryWriteBytes(blob.AsSpan(1, 16));
            return new() { Type = LuaType.Identifier, Bytes = blob };
        }

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
