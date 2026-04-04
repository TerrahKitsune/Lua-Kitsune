using System.Runtime.InteropServices;
using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;

namespace KitsuneNet
{
    public sealed class KitsuneEngine : IDisposable
    {
        private const string DllName = "KitsuneEngine";

        private int _disposed;  // 0 = not disposed; 1 = disposed
        private List<GCHandle>? _functionHandles;

        #region P/Invoke

        // KitsuneVariable x64 layout: int(4) + padding(4) + nuint(8) + union(8) = 24 bytes.
        [StructLayout(LayoutKind.Explicit, Size = 24)]
        private struct KitsuneVariable
        {
            [FieldOffset(0)]  public int    Type;
            [FieldOffset(8)]  public nuint  Length;
            [FieldOffset(16)] public IntPtr Data;
            [FieldOffset(16)] public double Number;
            [FieldOffset(16)] public long   Integer;
            [FieldOffset(16)] public byte   BoolByte;
        }

        // Mirrors KeyValuePairKitsuneVariableNode: Key(24) + Value(24) + Next ptr(8) = 56 bytes.
        [StructLayout(LayoutKind.Sequential)]
        private struct NativeKVNode
        {
            public KitsuneVariable Key;
            public KitsuneVariable Value;
            public IntPtr          Next;
        }

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        [return: MarshalAs(UnmanagedType.I1)]
        private static extern bool KitsuneInit(IntPtr initFunc);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        private static extern void KitsuneVariableFree(IntPtr var);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        private static extern int KitsuneExecuteFile(string path, int argc, KitsuneVariable[]? argv,
            [MarshalAs(UnmanagedType.I1)] bool fireAndForget);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        private static extern int KitsuneExecuteString(string script, int argc, KitsuneVariable[]? argv,
            [MarshalAs(UnmanagedType.I1)] bool fireAndForget);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        private static extern int KitsuneExecuteFunction(string functionName, int argc, KitsuneVariable[]? argv,
            [MarshalAs(UnmanagedType.I1)] bool fireAndForget);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        private static extern nuint KitsuneGetError(int id, byte[]? buf, nuint bufSize);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        [return: MarshalAs(UnmanagedType.I1)]
        private static extern bool KitsuneHasResult(int id, out nuint len);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr KitsuneGetResult(int id);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        private static extern void KitsuneCancel(int id);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        private static extern void KitsuneReleaseResult(int id);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        private static extern double KitsuneGetRuntime(int id);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        private static extern int KitsuneGetStatus(int id);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        [return: MarshalAs(UnmanagedType.I1)]
        private static extern bool KitsuneIsRunning();

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        private static extern int KitsuneGetRunningId();

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        private static extern void KitsuneInterrupt();

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        private static extern void KitsuneWait();

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        [return: MarshalAs(UnmanagedType.I1)]
        private static extern bool KitsuneSetVariable(string name, ref KitsuneVariable var);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        private static extern IntPtr KitsuneGetVariable(string name);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        private static extern int KitsuneGetActiveIds(int[]? buffer, int bufferSize);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        private static extern void KitsuneCleanup();

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        private static extern void KitsuneRegisterSession();

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate void GetAllCallback(IntPtr key, IntPtr value, IntPtr userdata);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        private static extern void KitsuneGetAll([In] string? path, GetAllCallback callback, IntPtr userdata);

        // func is a delegate* unmanaged[Cdecl] cast to nint; userdata is a GCHandle address.
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        private static extern void KitsuneRegisterFunction(string name, nint func, nint userdata);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr KitsuneCreateMemoryBlock(nuint size);

        // Mirrors the x64 layout of SharedMemoryBlock (see KitsuneEngine.h):
        //   offset  0: BYTE              flags    (1 byte + 7 padding)
        //   offset  8: void*             userdata (8 bytes, reserved)
        //   offset 16: SharedMemoryBlock* next    (8 bytes, intrusive list link — do NOT read/write from C#)
        //   offset 24: size_t            size     (8 bytes)
        //   offset 32: BYTE              data[]   (variable — NOT part of this header struct)
        [StructLayout(LayoutKind.Explicit, Size = 32)]
        private struct SharedMemoryBlockHeader
        {
            [FieldOffset(0)]  public byte   Flags;
            [FieldOffset(8)]  public IntPtr UserData;
            [FieldOffset(16)] public IntPtr Next;    // intrusive list pointer — not used by C#
            [FieldOffset(24)] public nuint  Size;
        }

        #endregion

        /// <summary>Initialises the engine. Throws if <c>KitsuneInit</c> returns false.</summary>
        public KitsuneEngine()
        {
            if (!KitsuneInit(IntPtr.Zero))
                throw new InvalidOperationException("KitsuneInit failed");
        }

        // -- Execution ------------------------------------------------------------

        // -- Helpers -------------------------------------------------------------

        // Converts a LuaValue[] to a KitsuneVariable[] suitable for P/Invoke.
        // String data is heap-allocated; caller MUST call FreeNativeArgs when done.
        private static (KitsuneVariable[]? native, IntPtr[] ptrs) BuildNativeArgs(LuaValue[]? args)
        {
            if (args is null || args.Length == 0) return (null, []);
            var native = new KitsuneVariable[args.Length];
            var ptrs   = new List<IntPtr>(args.Length);
            for (int i = 0; i < args.Length; i++)
                FillNativeVariable(ref native[i], args[i], ptrs);
            return (native, [.. ptrs]);
        }

        private static void FreeNativeArgs(IntPtr[] ptrs)
        {
            foreach (var p in ptrs) Marshal.FreeHGlobal(p);
        }

        private static LuaValue NativeCopyBytes(IntPtr src, nuint length)
        {
            if (length > (nuint)Array.MaxLength)
                throw new InvalidOperationException($"Native data length {length} exceeds the managed array limit.");
            int len = (int)length;
            byte[] bytes = new byte[len];
            if (len > 0) Marshal.Copy(src, bytes, 0, len);
            return LuaValue.FromBytes(bytes);
        }

        // wcharCount is the char16_t count; each char16_t is 2 bytes (UTF-16 LE on Windows).
        private static LuaValue NativeCopyWchar(IntPtr src, nuint wcharCount)
        {
            if (wcharCount > (nuint)(Array.MaxLength / 2))
                throw new InvalidOperationException($"Native wchar count {wcharCount} exceeds the managed array limit.");
            int byteCount = (int)wcharCount * 2;
            byte[] bytes = new byte[byteCount];
            if (byteCount > 0) Marshal.Copy(src, bytes, 0, byteCount);
            return new LuaValue { Type = LuaType.Wchar, Bytes = bytes };
        }

        private static LuaValue NativeParseJson(IntPtr src, nuint length)
        {
            if (length > (nuint)Array.MaxLength)
                throw new InvalidOperationException($"Native data length {length} exceeds the managed array limit.");
            int len = (int)length;
            byte[] bytes = new byte[len];
            Marshal.Copy(src, bytes, 0, len);
            try   { return new LuaValue { Type = LuaType.Json, JsonNode = JsonNode.Parse(bytes) }; }
            catch { return LuaValue.FromBytes(bytes); }  // malformed JSON falls back to raw string
        }

        // Reads a heap-allocated KitsuneVariable*, converts it to LuaValue, and frees it.
        private static LuaValue NativePtrToLuaValue(IntPtr ptr)
        {
            if (ptr == IntPtr.Zero) return LuaValue.None;
            var nv = Marshal.PtrToStructure<KitsuneVariable>(ptr);
            LuaType t = (LuaType)nv.Type;
            LuaValue result = t switch
            {
                LuaType.Number  => LuaValue.FromNumber(nv.Number),
                LuaType.Integer => LuaValue.FromInt64(nv.Integer),
                LuaType.Boolean => LuaValue.FromBool(nv.BoolByte != 0),
                LuaType.String when nv.Data != IntPtr.Zero => NativeCopyBytes(nv.Data, nv.Length),
                LuaType.Wchar  when nv.Data != IntPtr.Zero => NativeCopyWchar(nv.Data, nv.Length),
                LuaType.Userdata when nv.Data != IntPtr.Zero && nv.Length > 0 => NativeCopyBytes(nv.Data, nv.Length) with { Type = LuaType.Userdata },
                LuaType.Json   when nv.Data != IntPtr.Zero && nv.Length > 0 => NativeParseJson(nv.Data, nv.Length),
                LuaType.Stream when nv.Data != IntPtr.Zero => NativeWrapSharedMemory(nv.Data),
                LuaType.Table  => ReadNativeTable(nv.Data),
                LuaType.None    => LuaValue.None,
                _               => new LuaValue { Type = t },  // Nil/Function/Userdata/Thread/LightUserdata
            };
            KitsuneVariableFree(ptr);
            return result;
        }

        // Converts a by-value KitsuneVariable (already marshaled into managed memory) to a LuaValue.
        // Does NOT free any native memory — use this for embedded struct members, not heap pointers.
        private static LuaValue NativeVariableToLuaValue(KitsuneVariable nv)
        {
            LuaType t = (LuaType)nv.Type;
            return t switch
            {
                LuaType.Number  => LuaValue.FromNumber(nv.Number),
                LuaType.Integer => LuaValue.FromInt64(nv.Integer),
                LuaType.Boolean => LuaValue.FromBool(nv.BoolByte != 0),
                LuaType.String when nv.Data != IntPtr.Zero => NativeCopyBytes(nv.Data, nv.Length),
                LuaType.Wchar  when nv.Data != IntPtr.Zero => NativeCopyWchar(nv.Data, nv.Length),
                LuaType.Userdata when nv.Data != IntPtr.Zero && nv.Length > 0 => NativeCopyBytes(nv.Data, nv.Length) with { Type = LuaType.Userdata },
                LuaType.Json   when nv.Data != IntPtr.Zero && nv.Length > 0 => NativeParseJson(nv.Data, nv.Length),
                LuaType.Stream when nv.Data != IntPtr.Zero => NativeWrapSharedMemory(nv.Data),
                LuaType.Table  => ReadNativeTable(nv.Data),
                LuaType.None    => LuaValue.None,
                _               => new LuaValue { Type = t },
            };
        }

        // Walks a native KeyValuePairKitsuneVariableNode linked list and converts it to a LuaValue table.
        // NativeVariableToLuaValue is called recursively for each entry, so nested tables are handled.
        private static LuaValue ReadNativeTable(IntPtr headPtr)
        {
            if (headPtr == IntPtr.Zero) return new LuaValue { Type = LuaType.Table };
            var entries = new List<KeyValuePair<LuaValue, LuaValue>>();
            IntPtr node = headPtr;
            while (node != IntPtr.Zero)
            {
                var n = Marshal.PtrToStructure<NativeKVNode>(node);
                entries.Add(new KeyValuePair<LuaValue, LuaValue>(
                    NativeVariableToLuaValue(n.Key),
                    NativeVariableToLuaValue(n.Value)));
                node = n.Next;
            }
            return LuaValue.FromTable(entries.AsReadOnly());
        }

        // Builds a native linked list from a managed table. Every allocation is added to ptrs for cleanup.
        private static IntPtr BuildNativeTable(
            IReadOnlyList<KeyValuePair<LuaValue, LuaValue>> entries, List<IntPtr> ptrs)
        {
            if (entries.Count == 0) return IntPtr.Zero;
            int nodeSize = Marshal.SizeOf<NativeKVNode>();
            var nodes = new IntPtr[entries.Count];
            for (int i = 0; i < entries.Count; i++)
            {
                nodes[i] = Marshal.AllocHGlobal(nodeSize);
                ptrs.Add(nodes[i]);
            }
            for (int i = 0; i < entries.Count; i++)
            {
                var n = default(NativeKVNode);
                FillNativeVariable(ref n.Key,   entries[i].Key,   ptrs);
                FillNativeVariable(ref n.Value, entries[i].Value, ptrs);
                n.Next = i + 1 < entries.Count ? nodes[i + 1] : IntPtr.Zero;
                Marshal.StructureToPtr(n, nodes[i], false);
            }
            return nodes[0];
        }

        // Fills a single KitsuneVariable struct for native pass-through; string and table data are
        // heap-allocated and added to ptrs so FreeNativeArgs cleans them up after the call returns.
        private static void FillNativeVariable(ref KitsuneVariable nv, LuaValue v, List<IntPtr> ptrs)
        {
            nv.Type = (int)v.Type;
            switch (v.Type)
            {
                case LuaType.Number:  nv.Number   = v.Number;  break;
                case LuaType.Integer: nv.Integer  = v.Int64;   break;
                case LuaType.Boolean: nv.BoolByte = v.Boolean ? (byte)1 : (byte)0; break;
                case LuaType.String:
                    if (v.Bytes is not null)
                    {
                        byte[] bytes = v.Bytes;
                        IntPtr p = Marshal.AllocHGlobal(bytes.Length + 1);
                        if (bytes.Length > 0) Marshal.Copy(bytes, 0, p, bytes.Length);
                        Marshal.WriteByte(p, bytes.Length, 0);
                        ptrs.Add(p);
                        nv.Data   = p;
                        nv.Length = (nuint)bytes.Length;
                    }
                    break;
                case LuaType.Wchar:
                    if (v.Bytes is not null)
                    {
                        // Bytes stores UTF-16 LE; Length = number of char16_t code units (2 bytes each).
                        byte[] wbytes = v.Bytes;
                        IntPtr p = Marshal.AllocHGlobal(wbytes.Length + 2);  // +2 for null char16_t
                        if (wbytes.Length > 0) Marshal.Copy(wbytes, 0, p, wbytes.Length);
                        Marshal.WriteInt16(p, wbytes.Length, 0);
                        ptrs.Add(p);
                        nv.Data   = p;
                        nv.Length = (nuint)(wbytes.Length / 2);
                    }
                    break;
                case LuaType.Table when v.Table is not null:
                    nv.Data   = BuildNativeTable(v.Table, ptrs);
                    nv.Length = (nuint)v.Table.Count;
                    break;
                case LuaType.Json when v.JsonNode is not null: {
                    byte[] json = JsonSerializer.SerializeToUtf8Bytes(v.JsonNode);
                    IntPtr p = Marshal.AllocHGlobal(json.Length + 1);
                    Marshal.Copy(json, 0, p, json.Length);
                    Marshal.WriteByte(p, json.Length, 0);
                    ptrs.Add(p);
                    nv.Data   = p;
                    nv.Length = (nuint)json.Length;
                    break;
                }
                case LuaType.Stream when v.StreamValue is not null: {
                    // Fast path: CreateStream block — pass the existing block directly (zero copy).
                    // MarkPassedToLua flips _isManaged=false to prevent a second fast-pass of the
                    // same block. The C++ lua_push_sharedmemory_stream call sets FlagLuaReferenced
                    // on the block so Dispose knows Lua's GC will eventually set OWNER_DISPOSED.
                    if (v.StreamValue is LuaStream managedLs) {
                        IntPtr sharedPtr = managedLs.GetSharedBlockPtr();
                        if (sharedPtr != IntPtr.Zero) {
                            managedLs.MarkPassedToLua();  // disable fast path for future calls
                            nv.Data = sharedPtr;
                            break;
                        }
                    }
                    // Copy path: allocate a new block and fill it with the stream's bytes.
                    byte[] data = v.StreamValue switch
                    {
                        LuaStream ls              => ls.ToArray(),
                        System.IO.MemoryStream ms => ms.ToArray(),
                        _                         => ReadStreamToBytes(v.StreamValue),
                    };
                    // Returns NULL on allocation failure; stream arg is silently skipped.
                    IntPtr block = KitsuneCreateMemoryBlock((nuint)data.Length);
                    if (block == IntPtr.Zero) break;
                    if (data.Length > 0)
                        Marshal.Copy(data, 0, IntPtr.Add(block, 32), data.Length);
                    nv.Data = block;
                    // NOT added to ptrs — the block is owned by the global list; freed by ticker.
                    break;
                }
            }
        }

        // Wraps an inbound SharedMemoryBlock* in a LuaStream — zero copy.
        // The LuaStream clears ACCESSOR_DISPOSED on the block, taking ownership of the accessor
        // role. Disposing sets ACCESSOR_DISPOSED; the engine's ticker frees the block once Lua
        // also sets OWNER_DISPOSED via shmem_close.
        private static LuaValue NativeWrapSharedMemory(IntPtr blockPtr)
        {
            if (blockPtr == IntPtr.Zero) return LuaValue.None;
            var header = Marshal.PtrToStructure<SharedMemoryBlockHeader>(blockPtr);
            if ((ulong)header.Size > (ulong)long.MaxValue)
                throw new InvalidOperationException($"Stream block size {header.Size} exceeds the addressable range.");
            return new LuaValue { Type = LuaType.Stream, StreamValue = new LuaStream(blockPtr, (long)header.Size) };
        }

        // Reads a System.IO.Stream into a byte array, seeking from the start when possible.
        private static byte[] ReadStreamToBytes(System.IO.Stream stream)
        {
            if (stream.CanSeek)
            {
                long saved = stream.Position;
                stream.Position = 0;
                byte[] buf = new byte[checked((int)stream.Length)];
                stream.ReadExactly(buf);
                stream.Position = saved;
                return buf;
            }
            using var ms = new System.IO.MemoryStream();
            stream.CopyTo(ms);
            return ms.ToArray();
        }

        // -- Execution ------------------------------------------------------------

        /// <summary>Starts a Lua script file as a coroutine and returns its ID, or -1 on failure.</summary>
        public int ExecuteFile(string path, bool fireAndForget = false, params LuaValue[]? args)
        {
            var (native, ptrs) = BuildNativeArgs(args);
            try   { return KitsuneExecuteFile(path, native?.Length ?? 0, native, fireAndForget); }
            finally { FreeNativeArgs(ptrs); }
        }

        /// <summary>Starts a Lua script string as a coroutine and returns its ID, or -1 on failure.</summary>
        public int ExecuteString(string script, bool fireAndForget = false, params LuaValue[]? args)
        {
            var (native, ptrs) = BuildNativeArgs(args);
            try   { return KitsuneExecuteString(script, native?.Length ?? 0, native, fireAndForget); }
            finally { FreeNativeArgs(ptrs); }
        }

        /// <summary>Starts a Lua script file as a coroutine and asynchronously waits for it to complete.</summary>
        /// <exception cref="LuaException">Thrown if the Lua script raised a runtime or syntax error.</exception>
        public async Task<string?> ExecuteFileAsync(string path, CancellationToken cancellationToken = default, params LuaValue[]? args)
        {
            int id = ExecuteFile(path, false, args);
            if (id < 0) throw new InvalidOperationException($"Failed to start Lua coroutine for file '{path}'.");
            try { await WaitAsync(id, cancellationToken); }
            catch (OperationCanceledException) { Cancel(id); throw; }
            string? error = GetError(id);
            if (!string.IsNullOrEmpty(error)) { Cancel(id); throw new LuaException(error); }
            return GetResultString(id);
        }

        /// <summary>Starts a Lua script string as a coroutine and asynchronously waits for it to complete.</summary>
        /// <exception cref="LuaException">Thrown if the Lua script raised a runtime or syntax error.</exception>
        public async Task<string?> ExecuteStringAsync(string script, CancellationToken cancellationToken = default, params LuaValue[]? args)
        {
            int id = ExecuteString(script, false, args);
            if (id < 0) throw new InvalidOperationException("Failed to start Lua coroutine.");
            try { await WaitAsync(id, cancellationToken); }
            catch (OperationCanceledException) { Cancel(id); throw; }
            string? error = GetError(id);
            if (!string.IsNullOrEmpty(error)) { Cancel(id); throw new LuaException(error); }
            return GetResultString(id);
        }

        /// <summary>Calls a global Lua function as a coroutine and returns its ID, or -1 on failure.</summary>
        public int ExecuteFunction(string functionName, bool fireAndForget = false, params LuaValue[]? args)
        {
            var (native, ptrs) = BuildNativeArgs(args);
            try   { return KitsuneExecuteFunction(functionName, native?.Length ?? 0, native, fireAndForget); }
            finally { FreeNativeArgs(ptrs); }
        }

        /// <summary>Calls a global Lua function as a coroutine and asynchronously waits for it to complete.</summary>
        /// <exception cref="LuaException">Thrown if the Lua function raised a runtime error.</exception>
        public async Task<string?> ExecuteFunctionAsync(string functionName, CancellationToken cancellationToken = default, params LuaValue[]? args)
        {
            int id = ExecuteFunction(functionName, false, args);
            if (id < 0) throw new InvalidOperationException($"Failed to start Lua coroutine for function '{functionName}'.");
            try { await WaitAsync(id, cancellationToken); }
            catch (OperationCanceledException) { Cancel(id); throw; }
            string? error = GetError(id);
            if (!string.IsNullOrEmpty(error)) { Cancel(id); throw new LuaException(error); }
            return GetResultString(id);
        }
        // -- Per-coroutine queries ------------------------------------------------

        /// <summary>Returns <c>true</c> once the coroutine has finished (success or error).</summary>
        public bool HasResult(int id, out nuint len) => KitsuneHasResult(id, out len);

        /// <summary>Returns <c>true</c> once the coroutine has finished (success or error).</summary>
        public bool HasResult(int id) => KitsuneHasResult(id, out _);

        /// <summary>Returns the error string for a finished coroutine, or <c>null</c> if none.</summary>
        public string? GetError(int id)
        {
            nuint len = KitsuneGetError(id, null, 0);
            if (len == 0) return null;
            if (len > (nuint)Array.MaxLength)
                throw new InvalidOperationException($"Error message length {len} exceeds the managed array limit.");
            int intLen = (int)len;
            byte[] buf = new byte[intLen + 1];
            KitsuneGetError(id, buf, (nuint)buf.Length);
            return Encoding.UTF8.GetString(buf, 0, intLen);
        }

        /// <summary>Returns the typed result and releases the slot.</summary>
        public LuaValue GetResultVariable(int id) => NativePtrToLuaValue(KitsuneGetResult(id));

        /// <summary>Returns the result as a UTF-8/Unicode string, or <c>null</c> if nil/none. Releases the slot.</summary>
        public string? GetResultString(int id)
        {
            LuaValue v = GetResultVariable(id);
            return (v.Type == LuaType.String || v.Type == LuaType.Wchar) ? v.String : null;
        }

        /// <summary>Returns the result as raw bytes, or <c>null</c> if nil/none. Releases the slot.</summary>
        public byte[]? GetResult(int id)
        {
            IntPtr ptr = KitsuneGetResult(id);
            if (ptr == IntPtr.Zero) return null;
            var nv = Marshal.PtrToStructure<KitsuneVariable>(ptr);
            byte[]? result = null;
            if (nv.Type == (int)LuaType.String && nv.Data != IntPtr.Zero && nv.Length > 0)
            {
                if (nv.Length > (nuint)Array.MaxLength)
                    throw new InvalidOperationException($"Result length {nv.Length} exceeds the managed array limit.");
                int len = (int)nv.Length;
                result = new byte[len];
                Marshal.Copy(nv.Data, result, 0, len);
            }
            KitsuneVariableFree(ptr);
            return result;
        }

        /// <summary>Signals the coroutine to stop and releases its slot.</summary>
        public void Cancel(int id) => KitsuneCancel(id);

        /// <summary>Releases the slot of a finished coroutine without consuming its result.
        /// Use after reading the error with <see cref="GetError"/> when you do not need the result.
        /// No-op for running coroutines — use <see cref="Cancel"/> for those. Thread-safe.
        /// </summary>
        public void ReleaseResult(int id) => KitsuneReleaseResult(id);

        /// <summary>
        /// Returns how long the coroutine has been alive in milliseconds, measured from when it was created.
        /// Returns 0 if the ID is not found.
        /// </summary>
        public double GetRuntime(int id) => KitsuneGetRuntime(id);

        /// <summary>Returns the current status of the coroutine. Thread-safe.</summary>
        public CoroutineStatus GetStatus(int id) => (CoroutineStatus)KitsuneGetStatus(id);

        // -- Global control -------------------------------------------------------

        /// <summary>Returns <c>true</c> if any coroutine is currently running or yielded.</summary>
        public bool IsRunning => KitsuneIsRunning();

        /// <summary>Returns the ID of the first coroutine that is still running, or 0 if none are active.</summary>
        public int RunningCoroutineId => KitsuneGetRunningId();

        /// <summary>Signals all running coroutines to stop at the next instruction boundary.</summary>
        public void Interrupt() => KitsuneInterrupt();

        /// <summary>
        /// Returns the IDs of all coroutines that are currently alive — either still running
        /// or finished but not yet released via <see cref="GetResult"/> or <see cref="ReleaseResult"/>.
        /// </summary>
        public int[] GetActiveIds()
        {
            int count = KitsuneGetActiveIds(null, 0);
            if (count == 0) return [];
            int[] ids = new int[count];
            KitsuneGetActiveIds(ids, ids.Length);
            return ids;
        }

        /// <summary>Blocks until all coroutines have finished.</summary>
        public void Wait() => KitsuneWait();

        /// <summary>
        /// Blocks until all coroutines have finished, or <paramref name="cancellationToken"/> is cancelled.
        /// </summary>
        public void Wait(CancellationToken cancellationToken)
        {
            while (IsRunning)
            {
                cancellationToken.ThrowIfCancellationRequested();
                Thread.Sleep(1);
            }
        }

        /// <summary>
        /// Blocks until the specified coroutine has finished, or <paramref name="cancellationToken"/> is cancelled.
        /// </summary>
        public void Wait(int id, CancellationToken cancellationToken = default)
        {
            while (!HasResult(id))
            {
                cancellationToken.ThrowIfCancellationRequested();
                Thread.Sleep(1);
            }
        }

        /// <summary>
        /// Asynchronously waits until all coroutines have finished,
        /// or <paramref name="cancellationToken"/> is cancelled.
        /// </summary>
        public async Task WaitAsync(CancellationToken cancellationToken = default)
        {
            while (IsRunning)
            {
                cancellationToken.ThrowIfCancellationRequested();
                await Task.Delay(1, cancellationToken);
            }
        }

        /// <summary>
        /// Asynchronously waits until the specified coroutine has finished,
        /// or <paramref name="cancellationToken"/> is cancelled.
        /// </summary>
        public async Task WaitAsync(int id, CancellationToken cancellationToken = default)
        {
            while (!HasResult(id))
            {
                cancellationToken.ThrowIfCancellationRequested();
                await Task.Delay(1, cancellationToken);
            }
        }

        // -- Variable bridge ------------------------------------------------------

        /// <summary>Sets a Lua global from a typed value using a dot-separated path. Pass <see cref="LuaValue.None"/> to remove the key.</summary>
        public bool SetVariable(string name, LuaValue value)
        {
            var ptrs = new List<IntPtr>();
            try
            {
                var nv = default(KitsuneVariable);
                FillNativeVariable(ref nv, value, ptrs);
                return KitsuneSetVariable(name, ref nv);
            }
            finally
            {
                FreeNativeArgs([.. ptrs]);
            }
        }

        /// <summary>Returns the Lua global at the given dot-separated path, or <see cref="LuaValue.None"/> if not found.</summary>
        public LuaValue GetVariable(string name) => NativePtrToLuaValue(KitsuneGetVariable(name));

        /// <summary>
        /// Allocates a shared-memory <see cref="LuaStream"/> of <paramref name="size"/> bytes
        /// backed by <c>KitsuneCreateMemoryBlock</c>.  The block is tracked by the engine's
        /// global registry and freed automatically once both C# and Lua are done with it.
        /// <para>
        /// Write to the stream before passing it to Lua via <see cref="SetVariable"/> or as a
        /// coroutine argument.  After the handoff the stream remains valid for concurrent
        /// read/write access while Lua holds its inbound stream.  Calling
        /// <see cref="LuaStream.Dispose"/> after the handoff is safe and simply signals C#'s
        /// side is done; the block is freed by the engine's ticker when Lua's GC also disposes.
        /// </para>
        /// <para>
        /// If the stream is never passed to Lua, disposing it frees the block immediately
        /// (on the next ticker cycle).
        /// </para>
        /// </summary>
        /// <exception cref="ArgumentOutOfRangeException">Thrown when <paramref name="size"/> is zero or negative.</exception>
        /// <exception cref="OutOfMemoryException">Thrown when the native allocation fails.</exception>
        public unsafe LuaStream CreateStream(int size)
        {
            ArgumentOutOfRangeException.ThrowIfNegativeOrZero(size);
            IntPtr block = KitsuneCreateMemoryBlock((nuint)size);
            if (block == IntPtr.Zero) throw new OutOfMemoryException("KitsuneCreateMemoryBlock failed.");
            var header = Marshal.PtrToStructure<SharedMemoryBlockHeader>(block);
            return new LuaStream(block, (long)header.Size, managed: true);
        }

        // Convenience shims for common types (path is dot-separated, e.g. "foo" or "foo.bar")
        public bool    SetString(string name, string value)  => SetVariable(name, value);
        public bool    SetString(string name, byte[] value)  => SetVariable(name, LuaValue.FromBytes(value));
        public bool    SetBool(string name, bool value)      => SetVariable(name, value);
        public bool    SetNumber(string name, double value)  => SetVariable(name, value);
        public bool    SetInt64(string name, long value)     => SetVariable(name, LuaValue.FromInt64(value));
        public string? GetString(string name)      { var v = GetVariable(name); return v.Type == LuaType.String  ? v.String : null; }
        public byte[]? GetStringBytes(string name) { var v = GetVariable(name); return v.Type == LuaType.String  ? v.Bytes  : null; }
        public double? GetNumber(string name)      { var v = GetVariable(name); return v.Type == LuaType.Number  ? v.Number : v.Type == LuaType.Integer ? (double)v.Int64 : null; }
        public long?   GetInt64(string name)       { var v = GetVariable(name); return v.Type == LuaType.Integer ? v.Int64  : v.Type == LuaType.Number  ? (long)v.Number  : null; }
        public bool?   GetBool(string name)        { var v = GetVariable(name); return v.Type == LuaType.Boolean ? v.Boolean : null; }
        public LuaType GetVariableType(string name) => GetVariable(name).Type;

        /// <summary>
        /// Returns all entries at the given dot-separated path as a list of key-value pairs.
        /// Pass <c>null</c> or <c>""</c> to iterate the Lua global environment (<c>_G</c>) itself.
        /// Returns an empty list when the path does not exist or does not contain a table.
        /// </summary>
        public IReadOnlyList<KeyValuePair<LuaValue, LuaValue>> GetAll(string? path = null)
        {
            var result = new List<KeyValuePair<LuaValue, LuaValue>>();
            GetAllCallback cb = (key, value, _) =>
            {
                if (key == IntPtr.Zero || value == IntPtr.Zero) return;
                var k = Marshal.PtrToStructure<KitsuneVariable>(key);
                var v = Marshal.PtrToStructure<KitsuneVariable>(value);
                result.Add(new KeyValuePair<LuaValue, LuaValue>(
                    NativeVariableToLuaValue(k),
                    NativeVariableToLuaValue(v)));
            };
            KitsuneGetAll(path, cb, IntPtr.Zero);
            GC.KeepAlive(cb);  // prevent GC from collecting the delegate before the call returns
            return result.AsReadOnly();
        }

        // -- RegisterSession / RegisterFunction -----------------------------------

        /// <summary>
        /// Registers the <c>Session</c> table (<c>Session.Console</c>, <c>Session.Clipboard</c>)
        /// into the Lua global environment. Call once from the host after construction to enable
        /// interactive session functions. Safe to call multiple times (re-registers the table).
        /// </summary>
        public void RegisterSession() => KitsuneRegisterSession();

        // -- RegisterFunction ----------------------------------------------------

        /// <summary>
        /// Registers a C# function as a Lua global callable by <paramref name="name"/>.
        /// <paramref name="name"/> may be a dot-separated path (e.g. <c>"Ns.Foo"</c>);
        /// intermediate tables are created automatically.
        /// The function receives the Lua call arguments and returns a single <see cref="LuaValue"/>,
        /// or <see cref="LuaValue.None"/> to return nothing. Throw a <see cref="LuaException"/> to
        /// raise a Lua error with a specific message; any other exception raises the exception message.
        /// </summary>
        public unsafe void RegisterFunction(string name, LuaFunction func)
        {
            _functionHandles ??= new();
            var handle = GCHandle.Alloc(func);
            _functionHandles.Add(handle);
            var fp = (nint)(delegate* unmanaged[Cdecl]<int, KitsuneVariable*, nint, void*, int>)&LuaFunctionTrampoline;
            KitsuneRegisterFunction(name, fp, (nint)GCHandle.ToIntPtr(handle));
        }

        // Called from native code for every function registered via RegisterFunction.
        // One trampoline handles all registrations; the GCHandle in userdata identifies the target.
        [UnmanagedCallersOnly(CallConvs = new[] { typeof(System.Runtime.CompilerServices.CallConvCdecl) })]
        private static unsafe int LuaFunctionTrampoline(
            int argc, KitsuneVariable* argv, nint resultSetterPtr, void* userdata)
        {
            try
            {
                var handle = GCHandle.FromIntPtr((nint)userdata);
                var func = (LuaFunction)handle.Target!;

                var args = new LuaValue[argc];
                for (int i = 0; i < argc; i++)
                    args[i] = NativeVariableToLuaValue(argv[i]);

                LuaValue result = func(Array.AsReadOnly(args));
                if (result.Type != LuaType.None)
                    InvokeResultSetter(resultSetterPtr, result);
                return 1;
            }
            catch (Exception ex)
            {
                try { InvokeResultSetterError(resultSetterPtr, ex.Message); }
                catch { /* OOM during error marshal: fall through, engine raises generic error */ }
                return 0;
            }
        }

        // Calls the native resultSetter with a typed value.
        private static unsafe void InvokeResultSetter(nint resultSetterPtr, LuaValue result)
        {
            var setter = (delegate* unmanaged[Cdecl]<KitsuneVariable*, int>)resultSetterPtr;
            var ptrs = new List<IntPtr>();
            try
            {
                var nv = default(KitsuneVariable);
                FillNativeVariable(ref nv, result, ptrs);
                setter(&nv);
            }
            finally
            {
                FreeNativeArgs([.. ptrs]);
            }
        }

        // Calls the native resultSetter with KITSUNE_TERROR to raise a Lua error.
        private static unsafe void InvokeResultSetterError(nint resultSetterPtr, string message)
        {
            var setter = (delegate* unmanaged[Cdecl]<KitsuneVariable*, int>)resultSetterPtr;
            byte[] msgBytes = Encoding.UTF8.GetBytes(message);
            IntPtr msgPtr = Marshal.AllocHGlobal(msgBytes.Length + 1);
            try
            {
                Marshal.Copy(msgBytes, 0, msgPtr, msgBytes.Length);
                Marshal.WriteByte(msgPtr, msgBytes.Length, 0);
                KitsuneVariable errVar = new() { Type = -2, Length = (nuint)msgBytes.Length };  // KITSUNE_TERROR
                errVar.Data = msgPtr;
                setter(&errVar);
            }
            finally
            {
                Marshal.FreeHGlobal(msgPtr);
            }
        }

        public void Dispose()
        {
            if (Interlocked.Exchange(ref _disposed, 1) == 0)
            {
                KitsuneCleanup();
                if (_functionHandles is not null)
                {
                    foreach (var h in _functionHandles)
                        if (h.IsAllocated) h.Free();
                    _functionHandles.Clear();
                }
            }
        }
    }

    /// <summary>
    /// Operation codes passed to a Lua function backend registered with <c>Stream.Create(fn)</c>.
    /// The function receives <c>(op, arg)</c> and must return the appropriate value for each op.
    /// </summary>
    /// <remarks>
    /// <para>The integer values are part of the public Lua stream-backend protocol.
    /// Lua scripts that implement custom backends should use these symbolic names
    /// (or their integer equivalents) rather than hardcoding numbers.</para>
    /// <para>Example backend skeleton:</para>
    /// <code>
    /// local CAP_READ, CAP_WRITE, CAP_SEEK = 1, 2, 4
    /// return Stream.Create(function(op, arg)
    ///     if     op == 0 then return CAP_READ + CAP_WRITE + CAP_SEEK  -- Open
    ///     elseif op == 1 then return true                              -- Close
    ///     elseif op == 2 then return readBytes(arg)                   -- Read
    ///     elseif op == 3 then return writeBytes(arg)                  -- Write
    ///     elseif op == 4 then return currentPosition                  -- CurPos
    ///     elseif op == 5 then return totalLength                      -- Len
    ///     elseif op == 6 then pos = arg; return true                  -- SetPos
    ///     elseif op == 7 then return { type = 'custom' }              -- Info
    ///     end
    /// end)
    /// </code>
    /// </remarks>
    public enum StreamBackendOp
    {
        /// <summary>Open the stream. Return an integer capability bitmask (see <see cref="StreamCaps"/>).</summary>
        Open   = 0,
        /// <summary>Close the stream and release resources. Return <c>true</c> on success.</summary>
        Close  = 1,
        /// <summary>Read up to <c>arg</c> bytes (0 = all remaining). Return a string, or an empty string / <c>false</c> on EOF.</summary>
        Read   = 2,
        /// <summary>Write the string <c>arg</c>. Return <c>true</c> on success.</summary>
        Write  = 3,
        /// <summary>Return the current cursor position as an integer.</summary>
        CurPos = 4,
        /// <summary>Return the total length of the stream as an integer.</summary>
        Len    = 5,
        /// <summary>Move the cursor to position <c>arg</c>. Return <c>true</c> on success.</summary>
        SetPos = 6,
        /// <summary>Return a backend-defined info table (e.g. <c>{ type = "custom" }</c>).</summary>
        Info   = 7,
    }

    /// <summary>Capability flags returned by a custom stream backend's <see cref="StreamBackendOp.Open"/> call
    /// and stored in <c>Stream:GetInfo()</c>'s <c>Caps</c> field.</summary>
    [Flags]
    public enum StreamCaps : byte
    {
        /// <summary>Stream supports read operations.</summary>
        Read  = 1,
        /// <summary>Stream supports write operations.</summary>
        Write = 2,
        /// <summary>Stream supports seeking (<c>Stream:Seek</c>, <c>Stream:pos()</c>).</summary>
        Seek  = 4,
    }

    /// <summary>Thrown when a Lua script raises a runtime or syntax error.</summary>
    public sealed class LuaException : Exception
    {
        public LuaException(string message) : base(message) { }
    }

    /// <summary>
    /// A C# function that can be registered and called from Lua as <c>Kitsune.Name(...)</c>.
    /// </summary>
    /// <param name="args">The Lua call arguments. Valid only for the duration of the call.</param>
    /// <returns>
    /// The value to return to Lua, or <see cref="LuaValue.None"/> to return nothing.
    /// Throw <see cref="LuaException"/> (or any exception) to raise a Lua error.
    /// </returns>
    public delegate LuaValue LuaFunction(IReadOnlyList<LuaValue> args);

    /// <summary>Status of a coroutine managed by the engine.</summary>
    public enum CoroutineStatus
    {
        /// <summary>ID not found — never existed, already released, or fully compacted.</summary>
        None      = 0,
        /// <summary>Alive and queued; waiting to be resumed by the scheduler.</summary>
        Idle      = 1,
        /// <summary>Alive but suspended for a <c>Sleep()</c> deadline.</summary>
        Sleeping  = 2,
        /// <summary>Currently executing inside <c>lua_resume</c>.</summary>
        Running   = 3,
        /// <summary>Finished successfully; result not yet consumed.</summary>
        Done      = 4,
        /// <summary>Finished with a runtime or Lua error. Call <see cref="KitsuneEngine.GetError"/> to read the message.</summary>
        Faulted   = 5,
        /// <summary>Stopped by an explicit <see cref="KitsuneEngine.Cancel"/> call, or cancel is pending
        /// but the scheduler has not yet processed it — callers can treat both the same way.</summary>
        Cancelled = 6,
    }

    /// <summary>Lua value types. Values match Lua's internal LUA_T* constants.</summary>
    public enum LuaType
    {
        /// <summary>No value / key not set (LUA_TNONE).</summary>
        None          = -1,
        /// <summary>Explicit nil (LUA_TNIL).</summary>
        Nil           =  0,
        /// <summary>Boolean (LUA_TBOOLEAN).</summary>
        Boolean       =  1,
        /// <summary>Light userdata — a raw pointer not managed by Lua (LUA_TLIGHTUSERDATA).</summary>
        LightUserdata =  2,
        /// <summary>Number — integer or float (LUA_TNUMBER).</summary>
        Number        =  3,
        /// <summary>String (LUA_TSTRING).</summary>
        String        =  4,
        /// <summary>Table (LUA_TTABLE). Value is not bridgeable; <see cref="LuaValue.Bytes"/> will be null.</summary>
        Table         =  5,
        /// <summary>Function (LUA_TFUNCTION). Value is not bridgeable; <see cref="LuaValue.Bytes"/> will be null.</summary>
        Function      =  6,
        /// <summary>Full userdata (LUA_TUSERDATA). Value is not bridgeable; <see cref="LuaValue.Bytes"/> will be null.</summary>
        Userdata      =  7,
        /// <summary>Coroutine thread (LUA_TTHREAD). Value is not bridgeable; <see cref="LuaValue.Bytes"/> will be null.</summary>
        Thread        =  8,
        /// <summary>Lua 5.3+ integer subtype. Value is stored in <see cref="LuaValue.Int64"/>; never a float.</summary>
        Integer       = -3,
        /// <summary>Kitsune Wchar userdata. UTF-8 bytes are stored in <see cref="LuaValue.Bytes"/>;
        /// <see cref="LuaValue.String"/> decodes them. Pushes a Lua Wchar object back into the state.</summary>
        Wchar         = -4,
        /// <summary>JSON value bridged via the engine's Json instance. C# holds a
        /// <see cref="System.Text.Json.Nodes.JsonNode"/>; Lua receives/sends a table decoded/encoded
        /// by <c>Json:Decode</c> / <c>Json:Encode</c>. Only meaningful as input (C# → Lua);
        /// Lua → C# tables still arrive as <see cref="Table"/> and can be converted with
        /// <see cref="LuaValue.AsJsonNode"/>.</summary>
        Json          = -5,
        /// <summary>Shared-memory stream block (KITSUNE_TSTREAM = -6).
        /// When received from the engine the value's <see cref="LuaValue.StreamValue"/> is a
        /// <see cref="LuaStream"/> that directly addresses the native block with zero copy;
        /// <see cref="LuaStream.Dispose"/> calls the block's close callback to release the
        /// Lua registry anchor. Use <see cref="LuaValue.FromStream(byte[])"/> or
        /// <see cref="LuaValue.FromStream(System.IO.Stream)"/> to send a stream to Lua.</summary>
        Stream        = -6,
    }

    /// <summary>A typed value exchanged with the Lua engine.</summary>
    public readonly record struct LuaValue
    {
        public LuaType Type    { get; init; }
        public double  Number  { get; init; }
        public long    Int64   { get; init; }
        public bool    Boolean { get; init; }
        /// <summary>Raw bytes for <see cref="LuaType.String"/> values. Not guaranteed to be valid UTF-8.</summary>
        public byte[]? Bytes   { get; init; }
        /// <summary>Entries for <see cref="LuaType.Table"/> values. Null for empty tables or non-table types.</summary>
        public IReadOnlyList<KeyValuePair<LuaValue, LuaValue>>? Table { get; init; }
        /// <summary>Parsed JSON node for <see cref="LuaType.Json"/> values. Null for all other types.</summary>
        public JsonNode? JsonNode { get; init; }
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
        public string? String => Type == LuaType.Wchar
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

        /// <summary>Returns the most useful string representation of the value.</summary>
        public override string ToString() => Type switch
        {
            LuaType.String        => String ?? string.Empty,
            LuaType.Wchar         => String ?? string.Empty,
            LuaType.Number        => Number.ToString(System.Globalization.CultureInfo.InvariantCulture),
            LuaType.Integer       => Int64.ToString(),
            LuaType.Boolean       => Boolean.ToString(),
            LuaType.Nil           => "nil",
            LuaType.Table         => Table is not null ? $"table({Table.Count})" : "table",
            LuaType.Json          => JsonNode?.ToJsonString() ?? "null",
            LuaType.Stream        => StreamValue?.ToString() ?? "stream(null)",
            LuaType.Function      => "function",
            LuaType.Userdata      => "userdata",
            LuaType.Thread        => "thread",
            LuaType.LightUserdata => "lightuserdata",
            _                     => string.Empty,
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
                return JsonNode;
            if (Type == LuaType.Table)
                return TableToJsonNode(this);
            return Type switch
            {
                LuaType.String  => JsonValue.Create(String),
                LuaType.Number  => JsonValue.Create(Number),
                LuaType.Integer => JsonValue.Create(Int64),
                LuaType.Boolean => JsonValue.Create(Boolean),
                _               => null,
            };
        }

        private static JsonNode? TableToJsonNode(LuaValue v)
        {
            if (v.Table is null || v.Table.Count == 0)
                return new JsonObject();

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
                        arr.Add(kvp.Value.AsJsonNode());
                    return arr;
                }
            }

            var obj = new JsonObject();
            foreach (var kvp in v.Table)
                obj[kvp.Key.String ?? kvp.Key.ToString()] = kvp.Value.AsJsonNode();
            return obj;
        }

        /// <summary>No value / not set.</summary>
        public static LuaValue None => new() { Type = LuaType.None };

        public static LuaValue FromNumber(double v)  => new() { Type = LuaType.Number,  Number  = v };
        public static LuaValue FromBool(bool v)      => new() { Type = LuaType.Boolean, Boolean = v };
        public static LuaValue FromInt64(long v)     => new() { Type = LuaType.Integer, Int64   = v };
        /// <summary>Creates a string value by UTF-8 encoding <paramref name="v"/>.</summary>
        public static LuaValue FromString(string? v) =>
            v is null ? None : new() { Type = LuaType.String, Bytes = Encoding.UTF8.GetBytes(v) };
        /// <summary>Creates a string value from a raw byte array with no encoding applied.</summary>
        public static LuaValue FromBytes(byte[]? v) =>
            v is null ? None : new() { Type = LuaType.String, Bytes = v };
        /// <summary>Creates a Wchar value. When set on a Lua global the engine pushes a Lua Wchar object.
        /// The UTF-16 LE encoded text is stored in <see cref="Bytes"/>; <see cref="String"/> decodes it back.</summary>
        public static LuaValue FromWchar(string? v) =>
            v is null ? None : new() { Type = LuaType.Wchar, Bytes = Encoding.Unicode.GetBytes(v) };
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

        public static implicit operator LuaValue(double v)    => FromNumber(v);
        public static implicit operator LuaValue(bool v)      => FromBool(v);
        public static implicit operator LuaValue(string? v)   => FromString(v);
        public static implicit operator LuaValue(byte[]? v)   => FromBytes(v);
        public static implicit operator LuaValue(JsonNode? v) => FromJson(v);
    }

    /// <summary>
    /// A <see cref="System.IO.Stream"/> that directly addresses a native
    /// <c>SharedMemoryBlock</c> without copying.
    /// <para>
    /// <b>Inbound (Lua → C#):</b> created by <see cref="KitsuneEngine"/> when a Lua coroutine
    /// returns a stream result.  Whether the stream is read-only or read-write is determined
    /// by <c>KITSUNE_SHARED_MEMORY_FLAG_READONLY</c> in the block's <c>flags</c> field.
    /// <see cref="Dispose"/> calls the block's close callback to release the Lua registry anchor
    /// and free the native memory.
    /// </para>
    /// <para>
    /// <b>Managed (C# → Lua):</b> created via <see cref="KitsuneEngine.CreateStream"/>.
    /// Always read-write.  Once passed to Lua via <see cref="LuaValue.FromStream(System.IO.Stream)"/>
    /// and <see cref="KitsuneEngine.SetVariable"/> (or as a coroutine argument), Lua takes
    /// ownership of the underlying block.  Do <em>not</em> call <see cref="Dispose"/> after the
    /// handoff — the close callback is cleared automatically and the engine's finalisation
    /// frees the block when Lua's GC collects the stream.
    /// </para>
    /// <para>
    /// All read and write operations set and clear <see cref="FlagLocked"/> on the block's
    /// <c>flags</c> byte around each access (skipped for read-only blocks).
    /// This is a cooperative advisory signal — see <see cref="FlagLocked"/> for the full caveat.
    /// </para>
    /// </summary>
    /// <summary>
    /// A <see cref="System.IO.Stream"/> that directly addresses a native
    /// <c>SharedMemoryBlock</c> without copying.
    /// <para>
    /// <b>Inbound (Lua → C#):</b> created by <see cref="KitsuneEngine"/> when a Lua coroutine
    /// returns a stream result.  <see cref="Dispose"/> sets <see cref="FlagAccessorDisposed"/> on
    /// the block; the engine's ticker frees it once Lua's GC also sets <see cref="FlagOwnerDisposed"/>.
    /// </para>
    /// <para>
    /// <b>Managed (C# → Lua):</b> created via <see cref="KitsuneEngine.CreateStream"/>.
    /// Always read-write.  Pass to Lua via <see cref="KitsuneEngine.SetVariable"/> or as a
    /// coroutine argument.  C# may continue accessing the stream after the handoff for concurrent
    /// shared-memory use; call <see cref="Dispose"/> when done.  If never passed to Lua,
    /// <see cref="Dispose"/> frees the block on the next ticker cycle.
    /// </para>
    /// <para>
    /// All read and write operations set and clear <see cref="FlagLocked"/> on the block's
    /// <c>flags</c> byte around each access (skipped for read-only blocks).
    /// This is a cooperative advisory signal — see <see cref="FlagLocked"/> for the full caveat.
    /// </para>
    /// </summary>
    public sealed class LuaStream : System.IO.UnmanagedMemoryStream
    {
        // ── Flag constants (mirror KitsuneEngine.h KITSUNE_SHARED_MEMORY_FLAG_*) ──
        /// <summary>Cooperative advisory lock bit.  Set by an accessor (including this class)
        /// before each read or write, cleared immediately after.
        /// Other accessors should spin until this bit is clear before accessing the data.
        /// <para>⚠ Not a true mutex: the underlying byte RMW is non-atomic on x86-64
        /// (movzx / or / mov), so two concurrent writers can both believe they hold the lock.
        /// For hard mutual exclusion use a real synchronisation primitive alongside this signal.
        /// </para></summary>
        public const byte FlagLocked          = 0x01;  // (1 << 0)
        /// <summary>Marks the block as read-only.  Write operations are rejected and
        /// <see cref="FlagLocked"/> is never set on reads.</summary>
        public const byte FlagReadOnly        = 0x04;  // (1 << 2)
        /// <summary>Set by the engine's ticker once all Lua streams referencing this block
        /// have been GC'd.  Once set, the block's data must not be accessed.</summary>
        public const byte FlagOwnerDisposed   = 0x10;  // (1 << 4)
        /// <summary>Cleared by the <see cref="LuaStream"/> constructor when C# takes ownership;
        /// set by <see cref="Dispose"/> when C# is done.  The ticker frees the block once both
        /// <see cref="FlagOwnerDisposed"/> and this flag are set.</summary>
        public const byte FlagAccessorDisposed = 0x20; // (1 << 5)
        /// <summary>Set by <c>lua_push_sharedmemory_stream</c> when any Lua stream is created
        /// from this block.  Used by <see cref="Dispose"/> to determine whether Lua's GC will
        /// eventually set <see cref="FlagOwnerDisposed"/>.</summary>
        public const byte FlagLuaReferenced   = 0x40;  // (1 << 6)

        private IntPtr    _blockPtr;
        private int       _disposed;
        private bool      _isManaged;  // true for CreateStream blocks until passed to Lua

        // Unified constructor.  Clears ACCESSOR_DISPOSED on the block, taking the accessor role.
        // managed=true for CreateStream blocks (always read-write).
        // managed=false for inbound blocks (access mode derived from READONLY flag).
        internal unsafe LuaStream(IntPtr blockPtr, long size, bool managed = false) : base()
        {
            _blockPtr  = blockPtr;
            _isManaged = managed;
            byte flags = Marshal.ReadByte(blockPtr, 0);
            // Signal that C# is now the active accessor.
            Marshal.WriteByte(blockPtr, 0, (byte)(flags & ~FlagAccessorDisposed));
            bool readOnly = !managed && (flags & FlagReadOnly) != 0;
            Initialize((byte*)((nint)blockPtr + 32), size, size,
                readOnly ? System.IO.FileAccess.Read : System.IO.FileAccess.ReadWrite);
        }

        /// <summary>
        /// Reads the <c>flags</c> byte of the underlying <c>SharedMemoryBlock</c> header.
        /// Check against <see cref="FlagLocked"/>, <see cref="FlagReadOnly"/>, etc.
        /// Returns 0 if the block pointer has been cleared (after <see cref="Dispose"/>).
        /// </summary>
        public byte Flags => _blockPtr != IntPtr.Zero ? Marshal.ReadByte(_blockPtr, 0) : (byte)0;

        // Called by FillNativeVariable when a CreateStream block is about to cross into Lua.
        // Flips _isManaged to false so subsequent SetVariable calls go through the copy path
        // (prevents a second fast-pass of the same block to Lua).
        internal void MarkPassedToLua() => _isManaged = false;

        // Returns the block pointer only for CreateStream blocks that have not yet been passed to
        // Lua, enabling the zero-copy fast path in FillNativeVariable.  Inbound blocks always
        // return IntPtr.Zero (copy path) to prevent multiple Lua streams pointing at the same block.
        internal IntPtr GetSharedBlockPtr() => _isManaged ? _blockPtr : IntPtr.Zero;

        // ── LOCKED-flag helpers ───────────────────────────────────────────────────
        // Sets FlagLocked before an access and clears it after (skipped on read-only blocks).
        // Non-atomic: see FlagLocked caveat above.
        private void AcquireLock()
        {
            if (CanWrite && _blockPtr != IntPtr.Zero)
                Marshal.WriteByte(_blockPtr, 0, (byte)(Marshal.ReadByte(_blockPtr, 0) | FlagLocked));
        }

        private void ReleaseLock()
        {
            if (CanWrite && _blockPtr != IntPtr.Zero)
                Marshal.WriteByte(_blockPtr, 0, (byte)(Marshal.ReadByte(_blockPtr, 0) & ~FlagLocked));
        }

        // ── Read overrides ────────────────────────────────────────────────────────
        /// <inheritdoc/>
        public override int Read(byte[] buffer, int offset, int count)
        {
            AcquireLock();
            try   { return base.Read(buffer, offset, count); }
            finally { ReleaseLock(); }
        }

        /// <inheritdoc/>
        public override int Read(Span<byte> destination)
        {
            AcquireLock();
            try   { return base.Read(destination); }
            finally { ReleaseLock(); }
        }

        /// <inheritdoc/>
        public override int ReadByte()
        {
            AcquireLock();
            try   { return base.ReadByte(); }
            finally { ReleaseLock(); }
        }

        // ── Write overrides ───────────────────────────────────────────────────────
        /// <inheritdoc/>
        public override void Write(byte[] buffer, int offset, int count)
        {
            AcquireLock();
            try   { base.Write(buffer, offset, count); }
            finally { ReleaseLock(); }
        }

        /// <inheritdoc/>
        public override void Write(ReadOnlySpan<byte> source)
        {
            AcquireLock();
            try   { base.Write(source); }
            finally { ReleaseLock(); }
        }

        /// <inheritdoc/>
        public override void WriteByte(byte value)
        {
            AcquireLock();
            try   { base.WriteByte(value); }
            finally { ReleaseLock(); }
        }

        /// <summary>
        /// Returns a managed copy of the stream's full contents without changing
        /// the current read position.
        /// </summary>
        public byte[] ToArray()
        {
            long saved = Position;
            try
            {
                Position = 0;
                byte[] data = new byte[Length];
                _ = Read(data, 0, data.Length);
                return data;
            }
            finally
            {
                Position = saved;
            }
        }

        /// <inheritdoc/>
        public override string ToString() => $"LuaStream({Length} bytes)";

        /// <summary>
        /// Sets <see cref="FlagAccessorDisposed"/> on the underlying block to signal C# is done,
        /// then closes the managed stream view.  The engine's ticker frees the block once Lua's
        /// GC also sets <see cref="FlagOwnerDisposed"/>.  If the stream was created via
        /// <see cref="KitsuneEngine.CreateStream"/> but never passed to Lua, both flags are set
        /// so the ticker can free the block on its next cycle.
        /// </summary>
        protected override void Dispose(bool disposing)
        {
            if (Interlocked.Exchange(ref _disposed, 1) == 0)
            {
                base.Dispose(disposing);  // closes managed stream view; does NOT touch native memory
                if (_blockPtr != IntPtr.Zero)
                {
                    byte flags = Marshal.ReadByte(_blockPtr, 0);
                    byte toSet = FlagAccessorDisposed;
                    // If Lua never created a stream from this block, nobody will set OWNER_DISPOSED;
                    // set it here so the ticker can free the block on its next cycle.
                    if ((flags & FlagLuaReferenced) == 0)
                        toSet |= FlagOwnerDisposed;
                    Marshal.WriteByte(_blockPtr, 0, (byte)(flags | toSet));
                    _blockPtr = IntPtr.Zero;
                }
            }
        }

        // Finalizer: safety net in case Dispose was never called explicitly.
        // The block is still live (ACCESSOR_DISPOSED=0 prevents the ticker from freeing it),
        // so reading _blockPtr here is safe.
        ~LuaStream() => Dispose(false);
    }
}
