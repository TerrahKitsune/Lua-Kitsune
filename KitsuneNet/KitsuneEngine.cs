using System.Runtime.InteropServices;
using System.Text;

namespace KitsuneNet
{
    public sealed class KitsuneEngine : IDisposable
    {
        private const string DllName = "KitsuneEngine";

        private bool _disposed;

        #region P/Invoke

        // KitsuneVariable x64 layout: int(4) + padding(4) + nuint(8) + union(8) = 24 bytes.
        [StructLayout(LayoutKind.Explicit, Size = 24)]
        private struct KitsuneVariable
        {
            [FieldOffset(0)]  public int    Type;
            [FieldOffset(8)]  public nuint  Length;
            [FieldOffset(16)] public IntPtr Data;
            [FieldOffset(16)] public double Number;
            [FieldOffset(16)] public byte   BoolByte;
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
        private static extern IntPtr KitsuneGetError(int id);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        [return: MarshalAs(UnmanagedType.I1)]
        private static extern bool KitsuneHasResult(int id, out nuint len);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr KitsuneGetResult(int id);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        private static extern void KitsuneCancel(int id);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        private static extern double KitsuneGetRuntime(int id);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        private static extern int KitsuneIsRunning();

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
            {
                native[i].Type = (int)args[i].Type;
                switch (args[i].Type)
                {
                    case LuaType.Number:
                        native[i].Number = args[i].Number;
                        break;
                    case LuaType.Boolean:
                        native[i].BoolByte = args[i].Boolean ? (byte)1 : (byte)0;
                        break;
                    case LuaType.String when args[i].Bytes is not null:
                        byte[] bytes = args[i].Bytes!;
                        IntPtr p = Marshal.AllocHGlobal(bytes.Length + 1);
                        Marshal.Copy(bytes, 0, p, bytes.Length);
                        Marshal.WriteByte(p, bytes.Length, 0);
                        ptrs.Add(p);
                        native[i].Data   = p;
                        native[i].Length = (nuint)bytes.Length;
                        break;
                }
            }
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
            LuaValue result = (LuaType)nv.Type switch
            {
                LuaType.Number  => LuaValue.FromNumber(nv.Number),
                LuaType.Boolean => LuaValue.FromBool(nv.BoolByte != 0),
                LuaType.String when nv.Data != IntPtr.Zero => CopyBytes(nv.Data, (int)nv.Length),
                _ => LuaValue.None,
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
                IntPtr ptr = KitsuneGetError(id);
                return ptr == IntPtr.Zero ? null : Marshal.PtrToStringAnsi(ptr);
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

        /// <summary>
        /// Returns how long the coroutine has been alive in milliseconds, measured from when it was created.
        /// Returns 0 if the ID is not found.
        /// </summary>
        public double GetRuntime(int id) => KitsuneGetRuntime(id);

        // -- Global control -------------------------------------------------------

        /// <summary>
        /// Returns the ID of the first coroutine that is still running, or 0 if none are active.
        /// </summary>
        public int RunningCoroutineId => KitsuneIsRunning();

        /// <summary>Returns <c>true</c> if any coroutine is currently running.</summary>
        public bool IsRunning => KitsuneIsRunning() != 0;

        /// <summary>Signals all running coroutines to stop at the next instruction boundary.</summary>
        public void Interrupt() => KitsuneInterrupt();

        /// <summary>
        /// Returns the IDs of all coroutines that are currently alive — either still running
        /// or finished but not yet released via <see cref="GetResult"/> or <see cref="ReleaseCoroutine"/>.
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

        /// <summary>Sets a Vars global from a typed value. Pass <see cref="LuaValue.None"/> to remove the key.</summary>
        public bool SetVariable(string name, LuaValue value)
        {
            IntPtr strPtr = IntPtr.Zero;
            try
            {
                var nv = new KitsuneVariable { Type = (int)value.Type };
                switch (value.Type)
                {
                    case LuaType.Number:
                        nv.Number = value.Number;
                        break;
                    case LuaType.Boolean:
                        nv.BoolByte = value.Boolean ? (byte)1 : (byte)0;
                        break;
                    case LuaType.String when value.Bytes is not null:
                        byte[] bytes = value.Bytes;
                        strPtr = Marshal.AllocHGlobal(bytes.Length + 1);
                        Marshal.Copy(bytes, 0, strPtr, bytes.Length);
                        Marshal.WriteByte(strPtr, bytes.Length, 0);
                        nv.Data   = strPtr;
                        nv.Length = (nuint)bytes.Length;
                        break;
                }
                return KitsuneSetVariable(name, ref nv);
            }
            finally
            {
                if (strPtr != IntPtr.Zero) Marshal.FreeHGlobal(strPtr);
            }
        }

        /// <summary>Returns the current value of a Vars global, or <see cref="LuaValue.None"/> if not found.</summary>
        public LuaValue GetVariable(string name) => NativePtrToLuaValue(KitsuneGetVariable(name));

        // Convenience shims for common types
        public bool    SetString(string name, string value)  => SetVariable(name, value);
        public bool    SetString(string name, byte[] value)  => SetVariable(name, Encoding.UTF8.GetString(value));
        public bool    SetBool(string name, bool value)      => SetVariable(name, value);
        public bool    SetNumber(string name, double value)  => SetVariable(name, value);
        public string? GetString(string name)      { var v = GetVariable(name); return v.Type == LuaType.String  ? v.String : null; }
        public byte[]? GetStringBytes(string name) { var v = GetVariable(name); return v.Type == LuaType.String ? v.Bytes : null; }
        public double? GetNumber(string name)      { var v = GetVariable(name); return v.Type == LuaType.Number  ? v.Number  : null; }
        public bool?   GetBool(string name)        { var v = GetVariable(name); return v.Type == LuaType.Boolean ? v.Boolean : null; }
        public LuaType GetVariableType(string name) => GetVariable(name).Type;

        public void Dispose()
        {
            if (!_disposed)
            {
                KitsuneCleanup();
                _disposed = true;
            }
        }
    }

    /// <summary>Thrown when a Lua script raises a runtime or syntax error.</summary>
    public sealed class LuaException : Exception
    {
        public LuaException(string message) : base(message) { }
    }

    /// <summary>Lua value types. Values match Lua's internal LUA_T* constants.</summary>
    public enum LuaType
    {
        /// <summary>No value / key not set (LUA_TNONE).</summary>
        None    = -1,
        /// <summary>Boolean (LUA_TBOOLEAN).</summary>
        Boolean =  1,
        /// <summary>Number — integer or float (LUA_TNUMBER).</summary>
        Number  =  3,
        /// <summary>String (LUA_TSTRING).</summary>
        String  =  4,
    }

    /// <summary>A typed value exchanged with the Lua engine.</summary>
    public readonly record struct LuaValue
    {
        public LuaType Type    { get; init; }
        public double  Number  { get; init; }
        public bool    Boolean { get; init; }
        /// <summary>Raw bytes for <see cref="LuaType.String"/> values. Not guaranteed to be valid UTF-8.</summary>
        public byte[]? Bytes   { get; init; }

        /// <summary>Decodes <see cref="Bytes"/> as UTF-8. Returns <c>null</c> when <see cref="Bytes"/> is null.</summary>
        public string? String => Bytes is null ? null : Encoding.UTF8.GetString(Bytes);

        /// <summary>Returns the most useful string representation of the value.</summary>
        public override string ToString() => Type switch
        {
            LuaType.String  => String ?? string.Empty,
            LuaType.Number  => Number.ToString(System.Globalization.CultureInfo.InvariantCulture),
            LuaType.Boolean => Boolean.ToString(),
            _               => string.Empty,
        };

        /// <summary>No value / not set.</summary>
        public static LuaValue None => new() { Type = LuaType.None };

        public static LuaValue FromNumber(double v)  => new() { Type = LuaType.Number,  Number  = v };
        public static LuaValue FromBool(bool v)      => new() { Type = LuaType.Boolean, Boolean = v };
        /// <summary>Creates a string value by UTF-8 encoding <paramref name="v"/>.</summary>
        public static LuaValue FromString(string? v) =>
            v is null ? None : new() { Type = LuaType.String, Bytes = Encoding.UTF8.GetBytes(v) };
        /// <summary>Creates a string value from a raw byte array with no encoding applied.</summary>
        public static LuaValue FromBytes(byte[]? v) =>
            v is null ? None : new() { Type = LuaType.String, Bytes = v };

        public static implicit operator LuaValue(double v)  => FromNumber(v);
        public static implicit operator LuaValue(bool v)    => FromBool(v);
        public static implicit operator LuaValue(string? v) => FromString(v);
        public static implicit operator LuaValue(byte[]? v) => FromBytes(v);
    }
}
