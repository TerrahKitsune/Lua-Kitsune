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
            [FieldOffset(0)]
            public int Type;

            [FieldOffset(8)]
            public nuint Length;

            [FieldOffset(16)]
            public IntPtr Data;

            [FieldOffset(16)]
            public double Number;

            [FieldOffset(16)]
            public long Integer;

            [FieldOffset(16)]
            public byte BoolByte;
        }

        // Mirrors KeyValuePairKitsuneVariableNode: Key(24) + Value(24) + Next ptr(8) = 56 bytes.
        [StructLayout(LayoutKind.Sequential)]
        private struct NativeKVNode
        {
            public KitsuneVariable Key;
            public KitsuneVariable Value;
            public IntPtr Next;
        }

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        [return: MarshalAs(UnmanagedType.I1)]
        private static extern bool KitsuneInit(IntPtr initFunc);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        private static extern void KitsuneVariableFree(IntPtr var);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi, BestFitMapping = false, ThrowOnUnmappableChar = true)]
        private static extern int KitsuneExecuteFileAsync([MarshalAs(UnmanagedType.LPStr)] string path, int argc, KitsuneVariable[]? argv, [MarshalAs(UnmanagedType.I1)] bool fireAndForget);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi, BestFitMapping = false, ThrowOnUnmappableChar = true)]
        private static extern int KitsuneExecuteStringAsync([MarshalAs(UnmanagedType.LPStr)] string script, int argc, KitsuneVariable[]? argv, [MarshalAs(UnmanagedType.I1)] bool fireAndForget);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi, BestFitMapping = false, ThrowOnUnmappableChar = true)]
        private static extern int KitsuneExecuteFunctionAsync([MarshalAs(UnmanagedType.LPStr)] string functionName, int argc, KitsuneVariable[]? argv, [MarshalAs(UnmanagedType.I1)] bool fireAndForget);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi, BestFitMapping = false, ThrowOnUnmappableChar = true)]
        private static extern IntPtr KitsuneExecuteFile([MarshalAs(UnmanagedType.LPStr)] string path, int argc, KitsuneVariable[]? argv);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi, BestFitMapping = false, ThrowOnUnmappableChar = true)]
        private static extern IntPtr KitsuneExecuteString([MarshalAs(UnmanagedType.LPStr)] string script, int argc, KitsuneVariable[]? argv);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi, BestFitMapping = false, ThrowOnUnmappableChar = true)]
        private static extern IntPtr KitsuneExecuteFunction([MarshalAs(UnmanagedType.LPStr)] string functionName, int argc, KitsuneVariable[]? argv);

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

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi, BestFitMapping = false, ThrowOnUnmappableChar = true)]
        [return: MarshalAs(UnmanagedType.I1)]
        private static extern bool KitsuneSetVariable([MarshalAs(UnmanagedType.LPStr)] string name, ref KitsuneVariable var);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi, BestFitMapping = false, ThrowOnUnmappableChar = true)]
        private static extern IntPtr KitsuneGetVariable([MarshalAs(UnmanagedType.LPStr)] string name);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        private static extern int KitsuneGetActiveIds(int[]? buffer, int bufferSize);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        private static extern nuint KitsuneCleanup();

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        private static extern void KitsuneRegisterSession();

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate void GetAllCallback(IntPtr key, IntPtr value, IntPtr userdata);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi, BestFitMapping = false, ThrowOnUnmappableChar = true)]
        private static extern void KitsuneGetAll([MarshalAs(UnmanagedType.LPStr)] string? path, GetAllCallback callback, IntPtr userdata);

        // func is a delegate* unmanaged[Cdecl] cast to nint; userdata is a GCHandle address.
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi, BestFitMapping = false, ThrowOnUnmappableChar = true)]
        private static extern void KitsuneRegisterFunction([MarshalAs(UnmanagedType.LPStr)] string name, nint func, nint userdata);

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
            [FieldOffset(0)]
            public byte Flags;

            [FieldOffset(8)]
            public IntPtr UserData;

            [FieldOffset(16)]
            public IntPtr Next; // intrusive list pointer — not used by C#

            [FieldOffset(24)]
            public nuint Size;
        }

        #endregion

        // Tracks the number of live KitsuneEngine instances.  KitsuneCleanup is
        // only called when the last instance is disposed; calling it earlier would
        // null g_state and break any concurrently running scripts (e.g. the stress
        // test runs a producer and a consumer as two independent engine instances).
        private static int _refCount;

        // Set to true on the scheduler thread while a LuaFunctionTrampoline call is executing.
        // Used to detect and reject recursive Execute* / Run* calls from within a registered function.
        [ThreadStatic]
        private static bool inLuaCallback;

        /// <summary>
        /// Number of native allocations that had not been freed when this engine was disposed.
        /// Non-zero only in USEMEMORYMANAGER builds (Debug/Windows); always 0 in release or Linux.
        /// Check this after <see cref="Dispose"/> to detect native memory leaks.
        /// </summary>
        public ulong LeakedAllocations { get; private set; }

        public KitsuneEngine()
        {
            if (!KitsuneInit(IntPtr.Zero))
            {
                throw new InvalidOperationException("KitsuneInit failed");
            }
            Interlocked.Increment(ref _refCount);
        }

        // Converts a LuaValue[] to a KitsuneVariable[] suitable for P/Invoke.
        // String data is heap-allocated; caller MUST call FreeNativeArgs when done.
        private static (KitsuneVariable[]? Native, IntPtr[] Ptrs) BuildNativeArgs(LuaValue[]? args)
        {
            if (args is null || args.Length == 0)
            {
                return (null, []);
            }

            var native = new KitsuneVariable[args.Length];
            var ptrs = new List<IntPtr>(args.Length);
            for (int i = 0; i < args.Length; i++)
            {
                FillNativeVariable(ref native[i], args[i], ptrs);
            }

            return (native, [.. ptrs]);
        }

        private static void FreeNativeArgs(IntPtr[] ptrs)
        {
            foreach (var p in ptrs)
            {
                Marshal.FreeHGlobal(p);
            }
        }

        private static LuaValue NativeCopyBytes(IntPtr src, nuint length)
        {
            if (length > (nuint)Array.MaxLength)
            {
                throw new InvalidOperationException($"Native data length {length} exceeds the managed array limit.");
            }
            int len = (int)length;
            byte[] bytes = new byte[len];
            if (len > 0)
            {
                Marshal.Copy(src, bytes, 0, len);
            }
            return LuaValue.FromBytes(bytes);
        }

        // wcharCount is the char16_t count; each char16_t is 2 bytes (UTF-16 LE on Windows).
        private static LuaValue NativeCopyChar16(IntPtr src, nuint wcharCount)
        {
            if (wcharCount > (nuint)(Array.MaxLength / 2))
            {
                throw new InvalidOperationException($"Native wchar count {wcharCount} exceeds the managed array limit.");
            }
            int byteCount = (int)wcharCount * 2;
            byte[] bytes = new byte[byteCount];
            if (byteCount > 0)
            {
                Marshal.Copy(src, bytes, 0, byteCount);
            }
            return new LuaValue { Type = LuaType.Char16, Bytes = bytes };
        }

        private static LuaValue NativeParseJson(IntPtr src, nuint length)
        {
            if (length > (nuint)Array.MaxLength)
            {
                throw new InvalidOperationException($"Native data length {length} exceeds the managed array limit.");
            }
            int len = (int)length;
            byte[] bytes = new byte[len];
            Marshal.Copy(src, bytes, 0, len);
            try
            {
                return new LuaValue { Type = LuaType.Json, JsonNode = JsonNode.Parse(bytes) };
            }
            catch
            {
                return LuaValue.FromBytes(bytes);
            }
        }

        // Reads a heap-allocated KitsuneVariable*, converts it to LuaValue, and frees it.
        private static LuaValue NativePtrToLuaValue(IntPtr ptr)
        {
            if (ptr == IntPtr.Zero)
            {
                return LuaValue.None;
            }
            var nv = Marshal.PtrToStructure<KitsuneVariable>(ptr);
            LuaType t = (LuaType)nv.Type;
            LuaValue result = t switch
            {
                LuaType.Number => LuaValue.FromNumber(nv.Number),
                LuaType.Integer => LuaValue.FromInt64(nv.Integer),
                LuaType.Boolean => LuaValue.FromBool(nv.BoolByte != 0),
                LuaType.String when nv.Data != IntPtr.Zero => NativeCopyBytes(nv.Data, nv.Length),
                LuaType.Char16 when nv.Data != IntPtr.Zero => NativeCopyChar16(nv.Data, nv.Length),
                LuaType.Userdata when nv.Data != IntPtr.Zero && nv.Length > 0 => NativeCopyBytes(nv.Data, nv.Length) with { Type = LuaType.Userdata },
                LuaType.Json when nv.Data != IntPtr.Zero && nv.Length > 0 => NativeParseJson(nv.Data, nv.Length),
                LuaType.Stream when nv.Data != IntPtr.Zero => NativeWrapSharedMemory(nv.Data),
                LuaType.Table => ReadNativeTable(nv.Data),
                LuaType.Error when nv.Data != IntPtr.Zero => NativeCopyBytes(nv.Data, nv.Length) with { Type = LuaType.Error },
                LuaType.None => LuaValue.None,
                _ => new LuaValue { Type = t },  // Nil/Function/Userdata/Thread/LightUserdata
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
                LuaType.Number => LuaValue.FromNumber(nv.Number),
                LuaType.Integer => LuaValue.FromInt64(nv.Integer),
                LuaType.Boolean => LuaValue.FromBool(nv.BoolByte != 0),
                LuaType.String when nv.Data != IntPtr.Zero => NativeCopyBytes(nv.Data, nv.Length),
                LuaType.Char16 when nv.Data != IntPtr.Zero => NativeCopyChar16(nv.Data, nv.Length),
                LuaType.Userdata when nv.Data != IntPtr.Zero && nv.Length > 0 => NativeCopyBytes(nv.Data, nv.Length) with { Type = LuaType.Userdata },
                LuaType.Json when nv.Data != IntPtr.Zero && nv.Length > 0 => NativeParseJson(nv.Data, nv.Length),
                LuaType.Stream when nv.Data != IntPtr.Zero => NativeWrapSharedMemory(nv.Data),
                LuaType.Table => ReadNativeTable(nv.Data),
                LuaType.None => LuaValue.None,
                _ => new LuaValue { Type = t },
            };
        }

        // Walks a native KeyValuePairKitsuneVariableNode linked list and converts it to a LuaValue table.
        // NativeVariableToLuaValue is called recursively for each entry, so nested tables are handled.
        private static LuaValue ReadNativeTable(IntPtr headPtr)
        {
            if (headPtr == IntPtr.Zero)
            {
                return new LuaValue { Type = LuaType.Table };
            }
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
            if (entries.Count == 0)
            {
                return IntPtr.Zero;
            }
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
                FillNativeVariable(ref n.Key, entries[i].Key, ptrs);
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
                case LuaType.Number:
                    nv.Number = v.Number;
                    break;
                case LuaType.Integer:
                    nv.Integer = v.Int64;
                    break;
                case LuaType.Boolean:
                    nv.BoolByte = v.Boolean ? (byte)1 : (byte)0;
                    break;
                case LuaType.String:
                    if (v.Bytes is not null)
                    {
                        byte[] bytes = v.Bytes;
                        IntPtr p = Marshal.AllocHGlobal(bytes.Length + 1);
                        if (bytes.Length > 0)
                        {
                            Marshal.Copy(bytes, 0, p, bytes.Length);
                        }
                        Marshal.WriteByte(p, bytes.Length, 0);
                        ptrs.Add(p);
                        nv.Data = p;
                        nv.Length = (nuint)bytes.Length;
                    }
                    break;
                case LuaType.Char16:
                    if (v.Bytes is not null)
                    {
                        // Bytes stores UTF-16 LE; Length = number of char16_t code units (2 bytes each).
                        byte[] wbytes = v.Bytes;
                        IntPtr p = Marshal.AllocHGlobal(wbytes.Length + 2);  // +2 for null char16_t
                        if (wbytes.Length > 0)
                        {
                            Marshal.Copy(wbytes, 0, p, wbytes.Length);
                        }
                        Marshal.WriteInt16(p, wbytes.Length, 0);
                        ptrs.Add(p);
                        nv.Data = p;
                        nv.Length = (nuint)(wbytes.Length / 2);
                    }
                    break;
                case LuaType.Table when v.Table is not null:
                    nv.Data = BuildNativeTable(v.Table, ptrs);
                    nv.Length = (nuint)v.Table.Count;
                    break;
                case LuaType.Json when v.JsonNode is not null:
                {
                    byte[] json = JsonSerializer.SerializeToUtf8Bytes(v.JsonNode);
                    IntPtr p = Marshal.AllocHGlobal(json.Length + 1);
                    Marshal.Copy(json, 0, p, json.Length);
                    Marshal.WriteByte(p, json.Length, 0);
                    ptrs.Add(p);
                    nv.Data = p;
                    nv.Length = (nuint)json.Length;
                    break;
                }
                case LuaType.Stream when v.StreamValue is not null:
                {
                    // Fast path: CreateStream block — pass the existing block directly (zero copy).
                    // MarkPassedToLua flips _isManaged=false to prevent a second fast-pass of the
                    // same block. The C++ lua_push_sharedmemory_stream call sets FlagLuaReferenced
                    // on the block so Dispose knows Lua's GC will eventually set OWNER_DISPOSED.
                    if (v.StreamValue is LuaStream managedLs)
                    {
                        IntPtr sharedPtr = managedLs.GetSharedBlockPtr();
                        if (sharedPtr != IntPtr.Zero)
                        {
                            managedLs.MarkPassedToLua(); // disable fast path for future calls
                            nv.Data = sharedPtr;
                            break;
                        }
                    }

                    // Copy path: allocate a new block and fill it with the stream's bytes.
                    byte[] data = v.StreamValue switch
                    {
                        LuaStream ls => ls.ToArray(),
                        System.IO.MemoryStream ms => ms.ToArray(),
                        _ => ReadStreamToBytes(v.StreamValue),
                    };

                    // Returns NULL on allocation failure; stream arg is silently skipped.
                    IntPtr block = KitsuneCreateMemoryBlock((nuint)data.Length);
                    if (block == IntPtr.Zero)
                    {
                        break;
                    }

                    if (data.Length > 0)
                    {
                        Marshal.Copy(data, 0, IntPtr.Add(block, 32), data.Length);
                    }

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
            if (blockPtr == IntPtr.Zero)
            {
                return LuaValue.None;
            }
            var header = Marshal.PtrToStructure<SharedMemoryBlockHeader>(blockPtr);
            if ((ulong)header.Size > (ulong)long.MaxValue)
            {
                throw new InvalidOperationException($"Stream block size {header.Size} exceeds the addressable range.");
            }
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
        /// <exception cref="LuaException">Thrown if called from within a registered function callback.</exception>
        public int ExecuteFile(string path, bool fireAndForget = false, params LuaValue[]? args)
        {
            if (inLuaCallback)
            {
                throw new LuaException("cannot be called from within a registered function");
            }

            var (native, ptrs) = BuildNativeArgs(args);
            try
            {
                return KitsuneExecuteFileAsync(path, native?.Length ?? 0, native, fireAndForget);
            }
            finally
            {
                FreeNativeArgs(ptrs);
            }
        }

        /// <summary>Starts a Lua script string as a coroutine and returns its ID, or -1 on failure.</summary>
        /// <exception cref="LuaException">Thrown if called from within a registered function callback.</exception>
        public int ExecuteString(string script, bool fireAndForget = false, params LuaValue[]? args)
        {
            if (inLuaCallback)
            {
                throw new LuaException("cannot be called from within a registered function");
            }

            var (native, ptrs) = BuildNativeArgs(args);
            try
            {
                return KitsuneExecuteStringAsync(script, native?.Length ?? 0, native, fireAndForget);
            }
            finally
            {
                FreeNativeArgs(ptrs);
            }
        }

        /// <summary>Starts a Lua script file as a coroutine and asynchronously waits for it to complete.</summary>
        /// <exception cref="LuaException">Thrown if the Lua script raised a runtime or syntax error.</exception>
        public async Task<string?> ExecuteFileAsync(string path, CancellationToken cancellationToken = default, params LuaValue[]? args)
        {
            int id = ExecuteFile(path, false, args);
            if (id < 0)
            {
                throw new InvalidOperationException($"Failed to start Lua coroutine for file '{path}'.");
            }

            try
            {
                await WaitAsync(id, cancellationToken).ConfigureAwait(false);
            }
            catch (OperationCanceledException)
            {
                Cancel(id);
                throw;
            }

            string? error = GetError(id);
            if (!string.IsNullOrEmpty(error))
            {
                Cancel(id);
                throw new LuaException(error);
            }

            return GetResultString(id);
        }

        /// <summary>Starts a Lua script string as a coroutine and asynchronously waits for it to complete.</summary>
        /// <exception cref="LuaException">Thrown if the Lua script raised a runtime or syntax error.</exception>
        public async Task<string?> ExecuteStringAsync(string script, CancellationToken cancellationToken = default, params LuaValue[]? args)
        {
            int id = ExecuteString(script, false, args);
            if (id < 0)
            {
                throw new InvalidOperationException("Failed to start Lua coroutine.");
            }

            try
            {
                await WaitAsync(id, cancellationToken).ConfigureAwait(false);
            }
            catch (OperationCanceledException)
            {
                Cancel(id);
                throw;
            }

            string? error = GetError(id);
            if (!string.IsNullOrEmpty(error))
            {
                Cancel(id);
                throw new LuaException(error);
            }

            return GetResultString(id);
        }

        /// <summary>Calls a global Lua function as a coroutine and returns its ID, or -1 on failure.</summary>
        /// <exception cref="LuaException">Thrown if called from within a registered function callback.</exception>
        public int ExecuteFunction(string functionName, bool fireAndForget = false, params LuaValue[]? args)
        {
            if (inLuaCallback)
            {
                throw new LuaException("cannot be called from within a registered function");
            }

            var (native, ptrs) = BuildNativeArgs(args);
            try
            {
                return KitsuneExecuteFunctionAsync(functionName, native?.Length ?? 0, native, fireAndForget);
            }
            finally
            {
                FreeNativeArgs(ptrs);
            }
        }

        /// <summary>Calls a global Lua function as a coroutine and asynchronously waits for it to complete.</summary>
        /// <exception cref="LuaException">Thrown if the Lua function raised a runtime error.</exception>
        public async Task<string?> ExecuteFunctionAsync(string functionName, CancellationToken cancellationToken = default, params LuaValue[]? args)
        {
            int id = ExecuteFunction(functionName, false, args);
            if (id < 0)
            {
                throw new InvalidOperationException($"Failed to start Lua coroutine for function '{functionName}'.");
            }

            try
            {
                await WaitAsync(id, cancellationToken).ConfigureAwait(false);
            }
            catch (OperationCanceledException)
            {
                Cancel(id);
                throw;
            }

            string? error = GetError(id);
            if (!string.IsNullOrEmpty(error))
            {
                Cancel(id);
                throw new LuaException(error);
            }

            return GetResultString(id);
        }

        /// <summary>Runs a Lua script file synchronously and returns the typed result.
        /// Returns <see cref="LuaValue.None"/> on start failure or if the script raised an error.
        /// For error details use <see cref="ExecuteFileAsync"/> instead.</summary>
        /// <exception cref="LuaException">Thrown if called from within a registered function callback.</exception>
        public LuaValue RunFile(string path, params LuaValue[]? args)
        {
            if (inLuaCallback)
            {
                throw new LuaException("cannot be called from within a registered function");
            }

            var (native, ptrs) = BuildNativeArgs(args);
            try
            {
                return NativePtrToLuaValue(KitsuneExecuteFile(path, native?.Length ?? 0, native));
            }
            finally
            {
                FreeNativeArgs(ptrs);
            }
        }

        /// <summary>Runs a Lua script string synchronously and returns the typed result.
        /// Returns <see cref="LuaValue.None"/> on start failure or if the script raised an error.
        /// For error details use <see cref="ExecuteStringAsync"/> instead.</summary>
        /// <exception cref="LuaException">Thrown if called from within a registered function callback.</exception>
        public LuaValue RunString(string script, params LuaValue[]? args)
        {
            if (inLuaCallback)
            {
                throw new LuaException("cannot be called from within a registered function");
            }

            var (native, ptrs) = BuildNativeArgs(args);
            try
            {
                return NativePtrToLuaValue(KitsuneExecuteString(script, native?.Length ?? 0, native));
            }
            finally
            {
                FreeNativeArgs(ptrs);
            }
        }

        /// <summary>Calls a global Lua function synchronously and returns the typed result.
        /// Returns <see cref="LuaValue.None"/> on start failure or if the function raised an error.
        /// For error details use <see cref="ExecuteFunctionAsync"/> instead.</summary>
        /// <exception cref="LuaException">Thrown if called from within a registered function callback.</exception>
        public LuaValue RunFunction(string functionName, params LuaValue[]? args)
        {
            if (inLuaCallback)
            {
                throw new LuaException("cannot be called from within a registered function");
            }

            var (native, ptrs) = BuildNativeArgs(args);
            try
            {
                return NativePtrToLuaValue(KitsuneExecuteFunction(functionName, native?.Length ?? 0, native));
            }
            finally
            {
                FreeNativeArgs(ptrs);
            }
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
            if (len == 0)
            {
                return null;
            }

            if (len > (nuint)Array.MaxLength)
            {
                throw new InvalidOperationException($"Error message length {len} exceeds the managed array limit.");
            }
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
            return (v.Type == LuaType.String || v.Type == LuaType.Char16) ? v.String : null;
        }

        /// <summary>Returns the result as raw bytes, or <c>null</c> if nil/none. Releases the slot.</summary>
        public byte[]? GetResult(int id)
        {
            IntPtr ptr = KitsuneGetResult(id);
            if (ptr == IntPtr.Zero)
            {
                return null;
            }
            var nv = Marshal.PtrToStructure<KitsuneVariable>(ptr);
            byte[]? result = null;
            if (nv.Type == (int)LuaType.String && nv.Data != IntPtr.Zero && nv.Length > 0)
            {
                if (nv.Length > (nuint)Array.MaxLength)
                {
                    throw new InvalidOperationException($"Result length {nv.Length} exceeds the managed array limit.");
                }
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
            if (count == 0)
            {
                return [];
            }
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
                await Task.Delay(1, cancellationToken).ConfigureAwait(false);
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
                await Task.Delay(1, cancellationToken).ConfigureAwait(false);
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
            if (block == IntPtr.Zero)
            {
                throw new OutOfMemoryException("KitsuneCreateMemoryBlock failed.");
            }
            var header = Marshal.PtrToStructure<SharedMemoryBlockHeader>(block);
            return new LuaStream(block, (long)header.Size, managed: true);
        }

        // Convenience shims for common types (path is dot-separated, e.g. "foo" or "foo.bar")
        public bool SetString(string name, string value) => SetVariable(name, value);

        public bool SetString(string name, byte[] value) => SetVariable(name, LuaValue.FromBytes(value));

        public bool SetBool(string name, bool value) => SetVariable(name, value);

        public bool SetNumber(string name, double value) => SetVariable(name, value);

        public bool SetInt64(string name, long value) => SetVariable(name, LuaValue.FromInt64(value));

        public string? GetString(string name)
        {
            var v = GetVariable(name);
            return v.Type == LuaType.String ? v.String : null;
        }

        public byte[]? GetStringBytes(string name)
        {
            var v = GetVariable(name);
            return v.Type == LuaType.String ? v.Bytes : null;
        }

        public double? GetNumber(string name)
        {
            var v = GetVariable(name);
            return v.Type == LuaType.Number ? v.Number : v.Type == LuaType.Integer ? (double)v.Int64 : null;
        }

        public long? GetInt64(string name)
        {
            var v = GetVariable(name);
            return v.Type == LuaType.Integer ? v.Int64 : v.Type == LuaType.Number ? (long)v.Number : null;
        }

        public bool? GetBool(string name)
        {
            var v = GetVariable(name);
            return v.Type == LuaType.Boolean ? v.Boolean : null;
        }

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
                if (key == IntPtr.Zero || value == IntPtr.Zero)
                {
                    return;
                }
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
            bool prev = inLuaCallback;
            inLuaCallback = true;
            try
            {
                var handle = GCHandle.FromIntPtr((nint)userdata);
                var func = (LuaFunction)handle.Target!;

                var args = new LuaValue[argc];
                for (int i = 0; i < argc; i++)
                {
                    args[i] = NativeVariableToLuaValue(argv[i]);
                }

                LuaValue result = func(Array.AsReadOnly(args));
                if (result.Type != LuaType.None)
                {
                    InvokeResultSetter(resultSetterPtr, result);
                }
                return 1;
            }
            catch (Exception ex)
            {
                try
                {
                    InvokeResultSetterError(resultSetterPtr, ex.Message);
                }
                catch
                {
                    // OOM during error marshal: fall through, engine raises generic error
                }

                return 0;
            }
            finally
            {
                inLuaCallback = prev;
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
            Dispose(true);
            GC.SuppressFinalize(this);
        }

        private void Dispose(bool disposing)
        {
            if (Interlocked.Exchange(ref _disposed, 1) == 0)
            {
                if (Interlocked.Decrement(ref _refCount) == 0)
                {
                    LeakedAllocations = (ulong)KitsuneCleanup();
                }

                if (disposing && _functionHandles is not null)
                {
                    foreach (var h in _functionHandles)
                    {
                        if (h.IsAllocated)
                        {
                            h.Free();
                        }
                    }

                    _functionHandles.Clear();
                }
            }
        }

        ~KitsuneEngine() => Dispose(false);
    }
}
