using System.Runtime.InteropServices;
using System.Text;

namespace KitsuneNet
{
    public sealed class KitsuneEngine : IDisposable
    {
        private const string DllName = "KitsuneEngine";

        private bool _disposed;

        #region P/Invoke

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        [return: MarshalAs(UnmanagedType.I1)]
        private static extern bool KitsuneInit();

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        private static extern int KitsuneExecuteFile(string path, int argc, string[]? argv,
            [MarshalAs(UnmanagedType.I1)] bool fireAndForget);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        private static extern int KitsuneExecuteString(string script, int argc, string[]? argv,
            [MarshalAs(UnmanagedType.I1)] bool fireAndForget);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        private static extern int KitsuneExecuteFunction(string functionName, int argc, string[]? argv,
            [MarshalAs(UnmanagedType.I1)] bool fireAndForget);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr KitsuneGetError(int id);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        [return: MarshalAs(UnmanagedType.I1)]
        private static extern bool KitsuneHasResult(int id, out nuint len);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        private static extern nuint KitsuneGetResult(int id, byte[]? buffer, nuint bufferSize);

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
        private static extern bool KitsuneSetString(string name, byte[] value, nuint length);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        [return: MarshalAs(UnmanagedType.I1)]
        private static extern bool KitsuneSetBool(string name, [MarshalAs(UnmanagedType.I1)] bool value);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        [return: MarshalAs(UnmanagedType.I1)]
        private static extern bool KitsuneSetNumber(string name, double value);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        private static extern nuint KitsuneGetString(string name, byte[] buffer, nuint bufferSize);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        [return: MarshalAs(UnmanagedType.I1)]
        private static extern bool KitsuneGetNumber(string name, out double value);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        [return: MarshalAs(UnmanagedType.I1)]
        private static extern bool KitsuneGetBool(string name, [MarshalAs(UnmanagedType.I1)] out bool value);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        private static extern int KitsuneGetVariableType(string name);

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

        /// <summary>
        /// Starts a Lua script file as a coroutine and returns its ID, or -1 on failure.
        /// When <paramref name="fireAndForget"/> is <c>true</c> the slot is freed automatically on
        /// completion; use <see cref="ExecuteFileAsync"/> when a result is needed.
        /// </summary>
        public int ExecuteFile(string path, bool fireAndForget = false, params string[]? args)
        {
            string[] argv = args ?? [];
            return KitsuneExecuteFile(path, argv.Length, argv, fireAndForget);
        }

        /// <summary>
        /// Starts a Lua script string as a coroutine and returns its ID, or -1 on failure.
        /// When <paramref name="fireAndForget"/> is <c>true</c> the slot is freed automatically on
        /// completion; use <see cref="ExecuteStringAsync"/> when a result is needed.
        /// </summary>
        public int ExecuteString(string script, bool fireAndForget = false, params string[]? args)
        {
            string[] argv = args ?? [];
            return KitsuneExecuteString(script, argv.Length, argv, fireAndForget);
        }

        /// <summary>
        /// Starts a Lua script file as a coroutine and asynchronously waits for it to complete.
        /// </summary>
        /// <returns>The UTF-8 result string, or <c>null</c> if the script returned nil or nothing.</returns>
        /// <exception cref="InvalidOperationException">Thrown if the coroutine could not be started.</exception>
        /// <exception cref="LuaException">Thrown if the Lua script raised a runtime or syntax error.</exception>
        public async Task<string?> ExecuteFileAsync(string path, CancellationToken cancellationToken = default, params string[]? args)
        {
            string[] argv = args ?? [];
            int id = KitsuneExecuteFile(path, argv.Length, argv, false);
            if (id < 0)
            {
                throw new InvalidOperationException($"Failed to start Lua coroutine for file '{path}'.");
            }

            try
            {
                await WaitAsync(id, cancellationToken);
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

        /// <summary>
        /// Starts a Lua script string as a coroutine and asynchronously waits for it to complete.
        /// </summary>
        /// <returns>The UTF-8 result string, or <c>null</c> if the script returned nil or nothing.</returns>
        /// <exception cref="InvalidOperationException">Thrown if the coroutine could not be started.</exception>
        /// <exception cref="LuaException">Thrown if the Lua script raised a runtime or syntax error.</exception>
        public async Task<string?> ExecuteStringAsync(string script, CancellationToken cancellationToken = default, params string[]? args)
        {
            string[] argv = args ?? [];
            int id = KitsuneExecuteString(script, argv.Length, argv, false);
            if (id < 0)
            {
                throw new InvalidOperationException("Failed to start Lua coroutine.");
            }

            try
            {
                await WaitAsync(id, cancellationToken);
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

        /// <summary>
        /// Calls a global Lua function as a coroutine and returns its ID, or -1 on failure.
        /// The ARGS global is not set; arguments are passed directly to the function.
        /// When <paramref name="fireAndForget"/> is <c>true</c> the slot is freed automatically on
        /// completion; use <see cref="ExecuteFunctionAsync"/> when a result is needed.
        /// </summary>
        public int ExecuteFunction(string functionName, bool fireAndForget = false, params string[]? args)
        {
            string[] argv = args ?? [];
            return KitsuneExecuteFunction(functionName, argv.Length, argv, fireAndForget);
        }

        /// <summary>
        /// Calls a global Lua function as a coroutine and asynchronously waits for it to complete.
        /// </summary>
        /// <returns>The UTF-8 result string, or <c>null</c> if the function returned nil or nothing.</returns>
        /// <exception cref="InvalidOperationException">Thrown if the coroutine could not be started.</exception>
        /// <exception cref="LuaException">Thrown if the Lua function raised a runtime error.</exception>
        public async Task<string?> ExecuteFunctionAsync(string functionName, CancellationToken cancellationToken = default, params string[]? args)
        {
            string[] argv = args ?? [];
            int id = KitsuneExecuteFunction(functionName, argv.Length, argv, false);

            if (id < 0)
            {
                throw new InvalidOperationException($"Failed to start Lua coroutine for function '{functionName}'.");
            }

            try
            {
                await WaitAsync(id, cancellationToken);
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

        // -- Per-coroutine queries

        /// <summary>
        /// Returns <c>true</c> once the coroutine has finished (success or error).
        /// <paramref name="len"/> is set to the byte length of the pending result (0 = no result).
        /// </summary>
        public bool HasResult(int id, out nuint len) => KitsuneHasResult(id, out len);

        /// <summary>Returns <c>true</c> once the coroutine has finished (success or error).</summary>
        public bool HasResult(int id) => KitsuneHasResult(id, out _);

        /// <summary>
        /// Returns the error string for a finished coroutine, or <c>null</c> if none.
        /// Valid until <see cref="GetResult"/> or <see cref="ReleaseCoroutine"/> releases the slot.
        /// </summary>
        public string? GetError(int id)
        {
            IntPtr ptr = KitsuneGetError(id);
            return ptr == IntPtr.Zero ? null : Marshal.PtrToStringAnsi(ptr);
        }

        /// <summary>
        /// Returns the result of a finished coroutine as raw bytes, or <c>null</c> if there is no result.
        /// Consumes the result and releases the slot. Always call this (or <see cref="ReleaseCoroutine"/>)
        /// after <see cref="HasResult"/> returns <c>true</c> to free the slot.
        /// </summary>
        public byte[]? GetResult(int id)
        {
            if (!KitsuneHasResult(id, out nuint len))
                return null;
            if (len == 0)
            {
                KitsuneGetResult(id, null, 0);
                return null;
            }
            byte[] buffer = new byte[(int)len + 1];
            nuint actual = KitsuneGetResult(id, buffer, (nuint)buffer.Length);
            return actual == 0 ? null : buffer[..(int)actual];
        }

        /// <summary>
        /// Returns the result of a finished coroutine as a UTF-8 string, or <c>null</c> if there is no result.
        /// Consumes the result and releases the slot.
        /// </summary>
        public string? GetResultString(int id)
        {
            byte[]? result = GetResult(id);
            return result is null ? null : Encoding.UTF8.GetString(result);
        }

        /// <summary>
        /// Signals the coroutine to stop at the next instruction boundary and releases its slot automatically.
        /// If the coroutine is already finished but not yet released, releases it immediately.
        /// After calling <see cref="Cancel"/> do not call <see cref="GetResult"/> or <see cref="ReleaseCoroutine"/> for that ID.
        /// </summary>
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

        /// <summary>Sets a string variable in the Vars table from a UTF-8 string.</summary>
        public bool SetString(string name, string value)
        {
            byte[] bytes = Encoding.UTF8.GetBytes(value);
            return KitsuneSetString(name, bytes, (nuint)bytes.Length);
        }

        /// <summary>Sets a string variable in the Vars table from raw bytes.</summary>
        public bool SetString(string name, byte[] value) =>
            KitsuneSetString(name, value, (nuint)value.Length);

        /// <summary>Sets a boolean variable in the Vars table.</summary>
        public bool SetBool(string name, bool value) => KitsuneSetBool(name, value);

        /// <summary>Sets a numeric variable in the Vars table.</summary>
        public bool SetNumber(string name, double value) => KitsuneSetNumber(name, value);

        /// <summary>Returns the string value of a Vars global as a UTF-8 string, or <c>null</c> if not found or not a string.</summary>
        public string? GetString(string name)
        {
            byte[] initial = new byte[256];
            nuint actual = KitsuneGetString(name, initial, (nuint)initial.Length);
            if (actual == 0)
                return null;
            if (actual < (nuint)initial.Length)
                return Encoding.UTF8.GetString(initial, 0, (int)actual);
            byte[] buffer = new byte[(int)actual + 1];
            KitsuneGetString(name, buffer, (nuint)buffer.Length);
            return Encoding.UTF8.GetString(buffer, 0, (int)actual);
        }

        /// <summary>Returns the string value of a Vars global as raw bytes, or <c>null</c> if not found or not a string.</summary>
        public byte[]? GetStringBytes(string name)
        {
            byte[] initial = new byte[256];
            nuint actual = KitsuneGetString(name, initial, (nuint)initial.Length);
            if (actual == 0)
                return null;
            if (actual < (nuint)initial.Length)
                return initial[..(int)actual];
            byte[] buffer = new byte[(int)actual + 1];
            KitsuneGetString(name, buffer, (nuint)buffer.Length);
            return buffer[..(int)actual];
        }

        /// <summary>Returns the number value of a Vars global, or <c>null</c> if not found or not a number.</summary>
        public double? GetNumber(string name)
        {
            if (!KitsuneGetNumber(name, out double value))
                return null;
            return value;
        }

        /// <summary>Returns the boolean value of a Vars global, or <c>null</c> if not found or not a boolean.</summary>
        public bool? GetBool(string name)
        {
            if (!KitsuneGetBool(name, out bool value))
                return null;
            return value;
        }

        /// <summary>Returns the <see cref="LuaType"/> of a Vars global, or <see cref="LuaType.Nil"/> if nil or not set.</summary>
        public LuaType GetVariableType(string name) => (LuaType)KitsuneGetVariableType(name);

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

    /// <summary>
    /// Lua value types as returned by <see cref="KitsuneEngine.GetVariableType"/>.
    /// Values match Lua's internal type constants.
    /// </summary>
    public enum LuaType
    {
        /// <summary>The variable is nil or not set.</summary>
        Nil     = -1,
        /// <summary>The variable holds a boolean.</summary>
        Boolean =  1,
        /// <summary>The variable holds a number (integer or float).</summary>
        Number  =  3,
        /// <summary>The variable holds a string.</summary>
        String  =  4,
    }
}
