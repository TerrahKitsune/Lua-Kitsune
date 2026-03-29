using System.Runtime.InteropServices;
using System.Text;

namespace KitsuneNet
{
    public sealed class KitsuneEngine : IDisposable
    {
        private const string DllName = "KitsuneEngine";

        private bool _disposed;
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
        private static extern bool KitsuneInit();

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

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate void GetAllCallback(IntPtr key, IntPtr value, IntPtr userdata);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        private static extern void KitsuneGetAll([In] string? path, GetAllCallback callback, IntPtr userdata);

        // func is a delegate* unmanaged[Cdecl] cast to nint; userdata is a GCHandle address.
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        private static extern void KitsuneRegisterFunction(string name, nint func, nint userdata);

        #endregion

        /// <summary>Initialises the engine. Throws if <c>KitsuneInit</c> returns false.</summary>
        public KitsuneEngine()
        {
            if (!KitsuneInit())
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
                LuaType.String when nv.Data != IntPtr.Zero => CopyBytes(nv.Data, (int)nv.Length),
                LuaType.Table  => ReadNativeTable(nv.Data),
                LuaType.None    => LuaValue.None,
                _               => new LuaValue { Type = t },  // Nil/Function/Userdata/Thread/LightUserdata
            };
            KitsuneVariableFree(ptr);
            return result;

            static LuaValue CopyBytes(IntPtr src, int length)
            {
                byte[] bytes = new byte[length];
                if (length > 0) Marshal.Copy(src, bytes, 0, length);
                return LuaValue.FromBytes(bytes);
            }
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
                LuaType.String when nv.Data != IntPtr.Zero => CopyBytes(nv.Data, (int)nv.Length),
                LuaType.Table  => ReadNativeTable(nv.Data),
                LuaType.None    => LuaValue.None,
                _               => new LuaValue { Type = t },
            };

            static LuaValue CopyBytes(IntPtr src, int length)
            {
                byte[] bytes = new byte[length];
                if (length > 0) Marshal.Copy(src, bytes, 0, length);
                return LuaValue.FromBytes(bytes);
            }
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
                case LuaType.String when v.Bytes is { Length: > 0 } bytes:
                    IntPtr p = Marshal.AllocHGlobal(bytes.Length + 1);
                    Marshal.Copy(bytes, 0, p, bytes.Length);
                    Marshal.WriteByte(p, bytes.Length, 0);
                    ptrs.Add(p);
                    nv.Data   = p;
                    nv.Length = (nuint)bytes.Length;
                    break;
                case LuaType.Table when v.Table is not null:
                    nv.Data   = BuildNativeTable(v.Table, ptrs);
                    nv.Length = (nuint)v.Table.Count;
                    break;
            }
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
                byte[] buf = new byte[(int)len + 1];
                KitsuneGetError(id, buf, (nuint)buf.Length);
                return Encoding.UTF8.GetString(buf, 0, (int)len);
            }

            /// <summary>Returns the typed result and releases the slot.</summary>
            public LuaValue GetResultVariable(int id) => NativePtrToLuaValue(KitsuneGetResult(id));

            /// <summary>Returns the result as a UTF-8 string, or <c>null</c> if nil/none. Releases the slot.</summary>
            public string? GetResultString(int id)
            {
                LuaValue v = GetResultVariable(id);
                return v.Type == LuaType.String ? v.String : null;
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
                    result = new byte[(int)nv.Length];
                    Marshal.Copy(nv.Data, result, 0, (int)nv.Length);
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

        /// <summary>
        /// Returns the ID of the first coroutine that is still running, or 0 if none are active.
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
                var nv = new KitsuneVariable { Type = (int)value.Type };
                switch (value.Type)
                {
                    case LuaType.Number:
                        nv.Number = value.Number;
                        break;
                    case LuaType.Integer:
                        nv.Integer = value.Int64;
                        break;
                    case LuaType.Boolean:
                        nv.BoolByte = value.Boolean ? (byte)1 : (byte)0;
                        break;
                    case LuaType.String when value.Bytes is not null:
                        byte[] bytes = value.Bytes;
                        IntPtr strPtr = Marshal.AllocHGlobal(bytes.Length + 1);
                        Marshal.Copy(bytes, 0, strPtr, bytes.Length);
                        Marshal.WriteByte(strPtr, bytes.Length, 0);
                        ptrs.Add(strPtr);
                        nv.Data   = strPtr;
                        nv.Length = (nuint)bytes.Length;
                        break;
                    case LuaType.Table when value.Table is not null:
                        nv.Data   = BuildNativeTable(value.Table, ptrs);
                        nv.Length = (nuint)value.Table.Count;
                        break;
                }
                return KitsuneSetVariable(name, ref nv);
            }
            finally
            {
                FreeNativeArgs([.. ptrs]);
            }
        }

        /// <summary>Returns the Lua global at the given dot-separated path, or <see cref="LuaValue.None"/> if not found.</summary>
        public LuaValue GetVariable(string name) => NativePtrToLuaValue(KitsuneGetVariable(name));

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
            IntPtr strPtr = IntPtr.Zero;
            try
            {
                KitsuneVariable nv = new() { Type = (int)result.Type };
                if (result.Type == LuaType.String && result.Bytes is { Length: > 0 } bytes)
                {
                    strPtr = Marshal.AllocHGlobal(bytes.Length + 1);
                    Marshal.Copy(bytes, 0, strPtr, bytes.Length);
                    Marshal.WriteByte(strPtr, bytes.Length, 0);
                    nv.Length = (nuint)bytes.Length;
                    nv.Data   = strPtr;
                }
                else if (result.Type == LuaType.Number)   nv.Number   = result.Number;
                else if (result.Type == LuaType.Integer)  nv.Integer  = result.Int64;
                else if (result.Type == LuaType.Boolean)  nv.BoolByte = result.Boolean ? (byte)1 : (byte)0;
                setter(&nv);
            }
            finally
            {
                if (strPtr != IntPtr.Zero) Marshal.FreeHGlobal(strPtr);
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
            if (!_disposed)
            {
                KitsuneCleanup();
                if (_functionHandles is not null)
                {
                    foreach (var h in _functionHandles)
                        if (h.IsAllocated) h.Free();
                    _functionHandles.Clear();
                }
                _disposed = true;
            }
        }
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
        /// <summary>Stopped by an explicit <see cref="KitsuneEngine.Cancel"/> call.</summary>
        Cancelled = 6,
        /// <summary><see cref="KitsuneEngine.Cancel"/> was called; awaiting the next scheduler cycle to process the interruption.</summary>
        Cancelling = 7,
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

        /// <summary>Decodes <see cref="Bytes"/> as UTF-8. Returns <c>null</c> when <see cref="Bytes"/> is null.</summary>
        public string? String => Bytes is null ? null : Encoding.UTF8.GetString(Bytes);

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
            LuaType.Number        => Number.ToString(System.Globalization.CultureInfo.InvariantCulture),
            LuaType.Integer       => Int64.ToString(),
            LuaType.Boolean       => Boolean.ToString(),
            LuaType.Nil           => "nil",
            LuaType.Table         => Table is not null ? $"table({Table.Count})" : "table",
            LuaType.Function      => "function",
            LuaType.Userdata      => "userdata",
            LuaType.Thread        => "thread",
            LuaType.LightUserdata => "lightuserdata",
            _                     => string.Empty,
        };

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
        /// <summary>Creates a table value from a list of key-value entries.</summary>
        public static LuaValue FromTable(IReadOnlyList<KeyValuePair<LuaValue, LuaValue>> entries) =>
            new() { Type = LuaType.Table, Table = entries };

        public static implicit operator LuaValue(double v)  => FromNumber(v);
        public static implicit operator LuaValue(bool v)    => FromBool(v);
        public static implicit operator LuaValue(string? v) => FromString(v);
        public static implicit operator LuaValue(byte[]? v) => FromBytes(v);
    }
}
