using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;

namespace KitsuneNet
{
    public sealed class KitsuneEngine : IDisposable
    {
        private const string DllName = "KitsuneEngine";

        // GCHandle roots for anonymous Lua closures created via LuaValue.FromCFunction.
        // Like the bags below, these are tied to the shared Lua state.
        private static readonly System.Collections.Concurrent.ConcurrentBag<GCHandle> GlobalCFunctionHandles = new();

        // GCHandle roots for delegates registered via RegisterFunction / RegisterUserdata
        // (dispatch lambdas, gcWrappers, etc.) and for CreateUserdata instance pins.
        // Per-engine handles are transferred here before the engine decrements _refCount,
        // so they remain valid until KitsuneCleanup fires lua_close on the last Dispose.
        private static readonly System.Collections.Concurrent.ConcurrentBag<GCHandle> GlobalFunctionHandles = new();
        private static readonly System.Collections.Concurrent.ConcurrentBag<GCHandle> GlobalUserdataHandles = new();

        // Tracks the number of live KitsuneEngine instances.  KitsuneCleanup is
        // only called when the last instance is disposed; calling it earlier would
        // null g_state and break any concurrently running scripts (e.g. the stress
        // test runs a producer and a consumer as two independent engine instances).
        private static int _refCount;

        // Set to true on the scheduler thread while a LuaFunctionTrampoline call is executing.
        // Used to detect and reject recursive Execute* / Run* calls from within a registered function.
        [ThreadStatic]
        private static bool inLuaCallback;

        private int _disposed;  // 0 = not disposed; 1 = disposed
        private List<GCHandle>? _functionHandles;
        private List<GCHandle>? _userdataHandles;

        public KitsuneEngine()
        {
            // KitsuneInit now returns true only when it creates a new state (we own it)
            // and false when the engine is already initialised by another caller. Call it
            // only on the first instance (_refCount 0 → 1); subsequent instances share the
            // existing state and must not attempt to re-initialise or re-claim ownership.
            if (Interlocked.Increment(ref _refCount) == 1)
            {
                if (!KitsuneInit(IntPtr.Zero))
                {
                    Interlocked.Decrement(ref _refCount);
                    throw new InvalidOperationException("KitsuneInit failed");
                }

                // Drain any orphaned coroutines from a previous engine that was not properly
                // disposed (e.g. abandoned by a test-runner abort).
                if (KitsuneIsRunning())
                {
                    KitsuneInterrupt();
                    KitsuneWait();
                }
            }
        }

        ~KitsuneEngine() => Dispose(false);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate void GetAllCallback(IntPtr key, IntPtr value, IntPtr userdata);

        /// <summary>
        /// Number of native allocations that had not been freed when this engine was disposed.
        /// Non-zero only in USEMEMORYMANAGER builds (Debug/Windows); always 0 in release or Linux.
        /// Check this after <see cref="Dispose"/> to detect native memory leaks.
        /// </summary>
        public ulong LeakedAllocations { get; private set; }

        /// <summary>Returns <c>true</c> if any coroutine is currently running or yielded.</summary>
        public bool IsRunning => KitsuneIsRunning();

        /// <summary>Returns the ID of the first coroutine that is still running, or 0 if none are active.</summary>
        public int RunningCoroutineId => KitsuneGetRunningId();

        /// <summary>Starts a Lua script file as a background coroutine (fire-and-forget).</summary>
        /// <exception cref="LuaException">Thrown if called from within a registered function callback.</exception>
        public void ExecuteFile(string path, params LuaValue[]? args)
        {
            if (inLuaCallback)
            {
                throw new LuaException("cannot be called from within a registered function");
            }

            var (native, ptrs) = BuildNativeArgs(args);
            try
            {
                KitsuneExecuteFileAsync(path, native?.Length ?? 0, native, true);
            }
            finally
            {
                FreeNativeArgs(ptrs);
            }
        }

        /// <summary>Starts a Lua script string as a background coroutine (fire-and-forget).</summary>
        /// <exception cref="LuaException">Thrown if called from within a registered function callback.</exception>
        public void ExecuteString(string script, params LuaValue[]? args)
        {
            if (inLuaCallback)
            {
                throw new LuaException("cannot be called from within a registered function");
            }

            var (native, ptrs) = BuildNativeArgs(args);
            try
            {
                KitsuneExecuteStringAsync(script, native?.Length ?? 0, native, true);
            }
            finally
            {
                FreeNativeArgs(ptrs);
            }
        }

        /// <summary>Starts a Lua script file as a coroutine and asynchronously waits for it to complete.</summary>
        /// <exception cref="LuaException">Thrown if the Lua script raised a runtime or syntax error.</exception>
        public async Task<LuaValue> ExecuteFileAsync(string path, CancellationToken cancellationToken = default, params LuaValue[]? args)
        {
            if (inLuaCallback)
            {
                throw new LuaException("cannot be called from within a registered function");
            }

            var (native, ptrs) = BuildNativeArgs(args);
            int id = -1;
            try
            {
                id = KitsuneExecuteFileAsync(path, native?.Length ?? 0, native, false);
            }
            finally
            {
                FreeNativeArgs(ptrs);
            }

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
            LuaValue result = GetResultVariable(id);
            if (!string.IsNullOrEmpty(error))
            {
                throw new LuaException(error);
            }

            return result;
        }

        /// <summary>Starts a Lua script string as a coroutine and asynchronously waits for it to complete.</summary>
        /// <exception cref="LuaException">Thrown if the Lua script raised a runtime or syntax error.</exception>
        public async Task<LuaValue> ExecuteStringAsync(string script, CancellationToken cancellationToken = default, params LuaValue[]? args)
        {
            if (inLuaCallback)
            {
                throw new LuaException("cannot be called from within a registered function");
            }

            var (native, ptrs) = BuildNativeArgs(args);
            int id = -1;
            try
            {
                id = KitsuneExecuteStringAsync(script, native?.Length ?? 0, native, false);
            }
            finally
            {
                FreeNativeArgs(ptrs);
            }

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
            LuaValue result = GetResultVariable(id);
            if (!string.IsNullOrEmpty(error))
            {
                throw new LuaException(error);
            }

            return result;
        }

        /// <summary>Calls a global Lua function as a background coroutine (fire-and-forget).</summary>
        /// <exception cref="LuaException">Thrown if called from within a registered function callback.</exception>
        public void ExecuteFunction(string functionName, params LuaValue[]? args)
        {
            if (inLuaCallback)
            {
                throw new LuaException("cannot be called from within a registered function");
            }

            var (native, ptrs) = BuildNativeArgs(args);
            try
            {
                KitsuneExecuteFunctionAsync(functionName, native?.Length ?? 0, native, true);
            }
            finally
            {
                FreeNativeArgs(ptrs);
            }
        }

        /// <summary>Calls a global Lua function as a coroutine and asynchronously waits for it to complete.</summary>
        /// <exception cref="LuaException">Thrown if the Lua function raised a runtime error.</exception>
        public async Task<LuaValue> ExecuteFunctionAsync(string functionName, CancellationToken cancellationToken = default, params LuaValue[]? args)
        {
            if (inLuaCallback)
            {
                throw new LuaException("cannot be called from within a registered function");
            }

            var (native, ptrs) = BuildNativeArgs(args);
            int id = -1;
            try
            {
                id = KitsuneExecuteFunctionAsync(functionName, native?.Length ?? 0, native, false);
            }
            finally
            {
                FreeNativeArgs(ptrs);
            }

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
            LuaValue result = GetResultVariable(id);
            if (!string.IsNullOrEmpty(error))
            {
                throw new LuaException(error);
            }

            return result;
        }

        /// <summary>Runs a Lua script file synchronously and returns the typed result.
        /// Returns <see cref="LuaValue.None"/> on start failure or if the script returned nothing.
        /// When called from within a registered function callback the call executes via a re-entrant
        /// tight loop; Sleep() and Yield() inside the nested script are no-ops in that context.</summary>
        /// <exception cref="LuaException">Thrown on any Lua runtime or syntax error, or if the
        /// native engine rejects the call. For concurrent non-blocking execution use <see cref="ExecuteFileAsync"/>.</exception>
        public LuaValue RunFile(string path, params LuaValue[]? args)
        {
            var (native, ptrs) = BuildNativeArgs(args);
            try
            {
                return NativePtrToLuaValue(KitsuneExecuteFile(path, native?.Length ?? 0, native)).GetOrThrow();
            }
            finally
            {
                FreeNativeArgs(ptrs);
            }
        }

        /// <summary>Runs a Lua script string synchronously and returns the typed result.
        /// Returns <see cref="LuaValue.None"/> on start failure or if the script returned nothing.
        /// When called from within a registered function callback the call executes via a re-entrant
        /// tight loop; Sleep() and Yield() inside the nested script are no-ops in that context.</summary>
        /// <exception cref="LuaException">Thrown on any Lua runtime or syntax error, or if the
        /// native engine rejects the call. For concurrent non-blocking execution use <see cref="ExecuteStringAsync"/>.</exception>
        public LuaValue RunString(string script, params LuaValue[]? args)
        {
            var (native, ptrs) = BuildNativeArgs(args);
            try
            {
                return NativePtrToLuaValue(KitsuneExecuteString(script, native?.Length ?? 0, native)).GetOrThrow();
            }
            finally
            {
                FreeNativeArgs(ptrs);
            }
        }

        /// <summary>Calls a global Lua function synchronously and returns the typed result.
        /// Returns <see cref="LuaValue.None"/> on start failure or if the function returned nothing.
        /// When called from within a registered function callback the call executes via a re-entrant
        /// tight loop; Sleep() and Yield() inside the nested function are no-ops in that context.</summary>
        /// <exception cref="LuaException">Thrown on any Lua runtime error, if the function does not exist,
        /// or if the native engine rejects the call. For concurrent non-blocking execution use <see cref="ExecuteFunctionAsync"/>.</exception>
        public LuaValue RunFunction(string functionName, params LuaValue[]? args)
        {
            var (native, ptrs) = BuildNativeArgs(args);
            try
            {
                return NativePtrToLuaValue(KitsuneExecuteFunction(functionName, native?.Length ?? 0, native)).GetOrThrow();
            }
            finally
            {
                FreeNativeArgs(ptrs);
            }
        }

        /// <summary>Starts execution of a <see cref="LuaValue"/> as a background coroutine (fire-and-forget).
        /// <list type="bullet">
        /// <item><see cref="LuaType.Function"/> — calls the Lua function with <paramref name="args"/> as direct parameters.</item>
        /// <item><see cref="LuaType.String"/> — loads the string as a Lua chunk; <paramref name="args"/> are exposed as <c>ARGS[1..n]</c>.</item>
        /// <item>Anything else — no-op (silently ignored).</item>
        /// </list></summary>
        /// <exception cref="LuaException">Thrown if called from within a registered function callback.</exception>
        public void ExecuteVariable(LuaValue variable, params LuaValue[]? args)
        {
            if (inLuaCallback)
            {
                throw new LuaException("cannot be called from within a registered function");
            }

            var (nv, native, ptrs) = BuildVariableAndArgs(variable, args);
            try
            {
                KitsuneExecuteVariableAsync(ref nv, native?.Length ?? 0, native, true);
            }
            finally
            {
                FreeNativeArgs(ptrs);
            }
        }

        /// <summary>Executes a <see cref="LuaValue"/> as a coroutine and asynchronously waits for it to complete.
        /// See <see cref="ExecuteVariable"/> for dispatch rules.</summary>
        /// <exception cref="LuaException">Thrown if the execution raised a runtime or syntax error.</exception>
        public async Task<LuaValue> ExecuteVariableAsync(LuaValue variable, CancellationToken cancellationToken = default, params LuaValue[]? args)
        {
            if (inLuaCallback)
            {
                throw new LuaException("cannot be called from within a registered function");
            }

            var (nv, native, ptrs) = BuildVariableAndArgs(variable, args);
            int id = -1;
            try
            {
                id = KitsuneExecuteVariableAsync(ref nv, native?.Length ?? 0, native, false);
            }
            finally
            {
                FreeNativeArgs(ptrs);
            }

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
            LuaValue result = GetResultVariable(id);
            if (!string.IsNullOrEmpty(error))
            {
                throw new LuaException(error);
            }

            return result;
        }

        /// <summary>Executes a <see cref="LuaValue"/> synchronously and returns the typed result.
        /// Returns <see cref="LuaValue.None"/> on start failure, if execution returned nothing,
        /// or if the Lua code raised an error (silent variant — use
        /// <see cref="ExecuteVariableAsync"/> for error details).
        /// When called from within a registered function callback the call executes via a re-entrant
        /// tight loop; Sleep() and Yield() inside the nested call are no-ops in that context.
        /// See <see cref="ExecuteVariable"/> for dispatch rules.</summary>
        /// <exception cref="LuaException">Thrown if the native engine rejects the call.</exception>
        public LuaValue RunVariable(LuaValue variable, params LuaValue[]? args)
        {
            var (nv, native, ptrs) = BuildVariableAndArgs(variable, args);
            try
            {
                // RunVariable is the "silent" sync variant: Lua runtime errors and type errors
                // return None rather than throwing.  Context rejections (inLuaCallback guard above)
                // still throw before the native call is made.
                LuaValue result = NativePtrToLuaValue(KitsuneExecuteVariable(ref nv, native?.Length ?? 0, native));
                return result.Type == LuaType.Error ? LuaValue.None : result;
            }
            finally
            {
                FreeNativeArgs(ptrs);
            }
        }

        /// <summary>Signals the coroutine to stop and releases its slot.</summary>
        public void Cancel(int id) => KitsuneCancel(id);

        /// <summary>
        /// Returns how long the coroutine has been alive in milliseconds, measured from when it was created.
        /// Returns 0 if the ID is not found.
        /// </summary>
        public double GetRuntime(int id) => KitsuneGetRuntime(id);

        /// <summary>Returns the current status of the coroutine. Thread-safe.</summary>
        public CoroutineStatus GetStatus(int id) => (CoroutineStatus)KitsuneGetStatus(id);

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
        /// Equivalent to <see cref="Wait()"/> but interruptible; checks the same active-coroutine
        /// count used by the native scheduler so sleeping, idle, and running coroutines are all
        /// included (unlike <see cref="IsRunning"/> which can transiently miss sleeping coroutines
        /// between scheduler ticks).
        /// </summary>
        public void Wait(CancellationToken cancellationToken)
        {
            while (KitsuneGetActiveIds(null, 0) > 0)
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
                if (GetStatus(id) == CoroutineStatus.None)
                {
                    return;  // engine disposed or slot compacted; will never produce a result
                }

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
                if (GetStatus(id) == CoroutineStatus.None)
                {
                    return;  // engine disposed or slot compacted; will never produce a result
                }

                await Task.Delay(1, cancellationToken).ConfigureAwait(false);
            }
        }

        /// <summary>Sets a Lua global from a typed value using a dot-separated path. Pass <see cref="LuaValue.None"/> to remove the key.</summary>
        public bool SetVariable(string name, LuaValue value)
        {
            if (inLuaCallback)
            {
                throw new LuaException("SetVariable cannot be called from within a registered function");
            }

            List<IntPtr>? ptrs = null;
            try
            {
                var nv = default(KitsuneVariable);
                FillNativeVariable(ref nv, value, ref ptrs);
                return KitsuneSetVariable(name, ref nv);
            }
            finally
            {
                FreeNativeArgs(ptrs);
            }
        }

        /// <summary>Returns the Lua global at the given dot-separated path, or <see cref="LuaValue.None"/> if not found.</summary>
        public LuaValue GetVariable(string name)
        {
            if (inLuaCallback)
            {
                throw new LuaException("GetVariable cannot be called from within a registered function");
            }

            return NativePtrToLuaValue(KitsuneGetVariable(name));
        }

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
        public LuaStream CreateStream(int size)
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
            if (inLuaCallback)
            {
                throw new LuaException("GetAll cannot be called from within a registered function");
            }

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

        /// <summary>
        /// Pins a <see cref="LuaValue"/> in the Lua registry and returns an integer reference.
        /// The pinned value is kept alive until <see cref="Unregister"/> is called with the returned ref.
        /// Returns <c>-2</c> (<c>LUA_NOREF</c>) on failure.
        /// </summary>
        public int Register(LuaValue value)
        {
            if (inLuaCallback)
            {
                throw new LuaException("Register cannot be called from within a registered function");
            }

            List<IntPtr>? ptrs = null;
            try
            {
                var nv = default(KitsuneVariable);
                FillNativeVariable(ref nv, value, ref ptrs);
                return KitsuneRegister(ref nv);
            }
            finally
            {
                FreeNativeArgs(ptrs);
            }
        }

        /// <summary>
        /// Returns a copy of the value pinned at <paramref name="ref"/> without releasing the pin.
        /// For <see cref="LuaType.Function"/> and <see cref="LuaType.Thread"/> values the returned
        /// <see cref="LuaValue"/> holds an independent registry reference; dispose it when done.
        /// Returns <see cref="LuaValue.None"/> if the ref is not found.
        /// </summary>
        public LuaValue GetByReference(int @ref)
        {
            if (inLuaCallback)
            {
                throw new LuaException("GetByReference cannot be called from within a registered function");
            }

            return NativePtrToLuaValue(KitsuneGetByReference(@ref));
        }

        /// <summary>
        /// Releases the registry pin at <paramref name="ref"/> and returns the previously pinned value.
        /// For <see cref="LuaType.Function"/> and <see cref="LuaType.Thread"/> values the returned
        /// <see cref="LuaValue"/> holds an independent registry reference; dispose it when done.
        /// Returns <see cref="LuaValue.None"/> if the ref is not found.
        /// </summary>
        public LuaValue Unregister(int @ref)
        {
            if (inLuaCallback)
            {
                throw new LuaException("Unregister cannot be called from within a registered function");
            }

            return NativePtrToLuaValue(KitsuneUnregister(@ref));
        }

        /// <summary>
        /// Registers a C# function as a Lua global callable by <paramref name="name"/>.
        /// <paramref name="name"/> may be a dot-separated path (e.g. <c>"Ns.Foo"</c>);
        /// intermediate tables are created automatically.
        /// The function receives the Lua call arguments and returns a single <see cref="LuaValue"/>,
        /// or <see cref="LuaValue.None"/> to return nothing. Throw a <see cref="LuaException"/> to
        /// raise a Lua error with a specific message; any other exception raises the exception message.
        /// </summary>
        public void RegisterFunction(string name, LuaFunction func)
        {
            if (inLuaCallback)
            {
                throw new LuaException("RegisterFunction cannot be called from within a registered function");
            }

            _functionHandles ??= new();
            var handle = GCHandle.Alloc(func);
            _functionHandles.Add(handle);
            KitsuneRegisterFunction(name, GetTrampolinePtr(), (nint)GCHandle.ToIntPtr(handle));
        }

        /// <summary>
        /// Registers a type to be used as userdata.
        /// </summary>
        /// <typeparam name="T">The type to register as userdata.</typeparam>
        /// <returns><c>true</c> if the type was successfully registered; <c>false</c> if the type name was already registered.</returns>
        /// <exception cref="LuaException">Thrown if called from within a registered function.</exception>
        public bool RegisterUserdata<T>()
            where T : class
        {
            if (inLuaCallback)
            {
                throw new LuaException("RegisterUserdata cannot be called from within a registered function");
            }

            var methods = new Dictionary<string, LuaFunction>();
            var metaMethods = new Dictionary<string, LuaFunction>();

            foreach (var method in typeof(T).GetMethods(BindingFlags.Public | BindingFlags.Instance))
            {
                var ma = method.GetCustomAttribute<LuaMethodAttribute>();
                if (ma is not null)
                {
                    string key = ma.Name ?? method.Name;
                    if (methods.ContainsKey(key))
                    {
                        throw new InvalidOperationException(
                            $"[LuaMethod] name '{key}' is used by more than one method on {typeof(T).Name}.");
                    }

                    var captured = method;
                    methods[key] = args =>
                    {
                        var inst = args.Count > 0 ? args[0].GetUserdata<T>() : null;
                        if (inst is null)
                        {
                            return LuaValue.FromError($"expected {typeof(T).Name} as self (arg 0)");
                        }

                        try
                        {
                            object? ret = captured.Invoke(inst, [args]);
                            return ret is LuaValue v ? v : LuaValue.None;
                        }
                        catch (TargetInvocationException tie) when (tie.InnerException is not null)
                        {
                            return LuaValue.FromError(tie.InnerException.Message);
                        }
                        catch (Exception ex)
                        {
                            return LuaValue.FromError(ex.Message);
                        }
                    };
                }

                var mm = method.GetCustomAttribute<LuaMetaMethodAttribute>();
                if (mm is not null)
                {
                    string key = mm.Name;
                    if (metaMethods.ContainsKey(key))
                    {
                        throw new InvalidOperationException(
                            $"[LuaMetaMethod] name '{key}' is used by more than one method on {typeof(T).Name}.");
                    }

                    var captured = method;
                    metaMethods[key] = args =>
                    {
                        var inst = args.Count > 0 ? args[0].GetUserdata<T>() : null;
                        if (inst is null)
                        {
                            return LuaValue.FromError($"expected {typeof(T).Name} as self (arg 0)");
                        }

                        try
                        {
                            object? ret = captured.Invoke(inst, [args]);
                            return ret is LuaValue v ? v : LuaValue.None;
                        }
                        catch (TargetInvocationException tie) when (tie.InnerException is not null)
                        {
                            return LuaValue.FromError(tie.InnerException.Message);
                        }
                        catch (Exception ex)
                        {
                            return LuaValue.FromError(ex.Message);
                        }
                    };
                }
            }

            // Inject a default __tostring if the user did not provide one.
            // Every C# object inherits Object.ToString(), so calling it is always safe.
            if (!metaMethods.ContainsKey("__tostring"))
            {
                metaMethods["__tostring"] = args =>
                {
                    var inst = args.Count > 0 ? args[0].GetUserdata<T>() : null;
                    if (inst is null)
                    {
                        return LuaValue.FromError($"expected {typeof(T).Name} as self (arg 0)");
                    }

                    return LuaValue.FromString(inst.ToString());
                };
            }

            return RegisterUserdataCore(typeof(T).Name, methods, metaMethods);
        }

        /// <summary>
        /// Creates a Lua userdata value wrapping the given C# object instance. The userdata is pinned in memory
        /// until it is collected by Lua's garbage collector or the engine is disposed.
        /// </summary>
        /// <typeparam name="T">The type of the C# object to wrap as userdata.</typeparam>
        /// <param name="instance">The C# object instance to wrap as userdata.</param>
        /// <returns>A LuaValue representing the userdata.</returns>
        /// <exception cref="LuaException">Thrown if called from within a registered function.</exception>
        public LuaValue CreateUserdata<T>(T instance)
            where T : class
        {
            if (inLuaCallback)
            {
                throw new LuaException("CreateUserdata cannot be called from within a registered function");
            }

            ArgumentNullException.ThrowIfNull(instance);

            var handle = GCHandle.Alloc(instance);
            _userdataHandles ??= new List<GCHandle>();
            _userdataHandles.Add(handle);
            return new LuaValue
            {
                Type = LuaType.Userdata,
                Bytes = Encoding.UTF8.GetBytes(typeof(T).Name),
                UserdataGCHandlePtr = GCHandle.ToIntPtr(handle),
            };
        }

        public void Dispose()
        {
            Dispose(true);
            GC.SuppressFinalize(this);
        }

        /// <summary>
        /// Performs a Lua garbage collection operation and returns current heap usage in bytes.
        /// <list type="bullet">
        /// <item><paramref name="mode"/> 0 — query only; no collection performed.</item>
        /// <item><paramref name="mode"/> 1 (default) — full collection cycle (<c>LUA_GCCOLLECT</c>).</item>
        /// <item><paramref name="mode"/> 2 — single incremental step (<c>LUA_GCSTEP</c>).</item>
        /// <item><paramref name="mode"/> 3 — pause the GC (<c>LUA_GCSTOP</c>). Memory grows without
        /// bound until mode 1, 2, or 4 is called.</item>
        /// <item><paramref name="mode"/> 4 — restart a paused GC (<c>LUA_GCRESTART</c>).</item>
        /// </list>
        /// <para>Drains any pending deferred frees from disposed <see cref="LuaFunctionRef"/> /
        /// <see cref="LuaThreadRef"/> instances before the cycle. Holds the Lua scheduler lock
        /// for the full duration.</para>
        /// </summary>
        /// <returns>Current Lua heap usage in bytes, or -1 if the engine is not initialised.</returns>
        public long CollectGarbage(int mode = 1) => KitsuneGC(mode);

        /// <summary>Releases a heap-allocated <c>KitsuneVariable*</c> via the native free function.
        /// Called by <see cref="LuaFunctionRef.Dispose"/> to release a Lua registry reference.
        /// Must not be called while the pointer is still in use.</summary>
        internal static void ReleaseNativeVariable(IntPtr ptr) => KitsuneVariableFree(ptr);

        internal static LuaValue InvokeFunction(LuaFunctionRef fref, LuaValue[]? args)
        {
            if (inLuaCallback)
            {
                throw new LuaException("cannot be called from within a registered function");
            }

            var (nv, native, ptrs) = BuildVariableAndArgs(new LuaValue { Type = LuaType.Function, FunctionRef = fref }, args);
            try
            {
                LuaValue result = NativePtrToLuaValue(KitsuneExecuteVariable(ref nv, native?.Length ?? 0, native));
                return result.Type == LuaType.Error ? LuaValue.None : result;
            }
            finally
            {
                FreeNativeArgs(ptrs);
            }
        }

        internal static async Task<LuaValue> InvokeFunctionAsync(LuaFunctionRef fref, LuaValue[]? args, CancellationToken cancellationToken)
        {
            if (inLuaCallback)
            {
                throw new LuaException("cannot be called from within a registered function");
            }

            var (nv, native, ptrs) = BuildVariableAndArgs(new LuaValue { Type = LuaType.Function, FunctionRef = fref }, args);
            int id = -1;
            try
            {
                id = KitsuneExecuteVariableAsync(ref nv, native?.Length ?? 0, native, false);
            }
            finally
            {
                FreeNativeArgs(ptrs);
            }

            if (id < 0)
            {
                throw new InvalidOperationException("Failed to start Lua coroutine.");
            }

            try
            {
                await WaitForIdAsync(id, cancellationToken).ConfigureAwait(false);
            }
            catch (OperationCanceledException)
            {
                KitsuneCancel(id);
                throw;
            }

            string? error = GetError(id);
            LuaValue result = GetResultVariable(id);
            if (!string.IsNullOrEmpty(error))
            {
                throw new LuaException(error);
            }

            return result;
        }

        /// <summary>
        /// Synchronously steps the coroutine thread once, optionally passing <paramref name="args"/>
        /// as the result of the current <c>coroutine.yield()</c> (or as initial parameters on the
        /// first call). Returns the first value yielded or returned by this step.
        /// Returns <see cref="LuaValue.None"/> when the thread is dead.
        /// Returns a nil <see cref="LuaValue"/> when the thread yielded without a value.
        /// Throws <see cref="LuaException"/> if the thread raises a Lua error.
        /// </summary>
        internal static LuaValue StepThread(LuaThreadRef tref, LuaValue[]? args)
        {
            if (inLuaCallback)
            {
                throw new LuaException("cannot be called from within a registered function");
            }

            var nv = default(KitsuneVariable);
            var tnv = Marshal.PtrToStructure<KitsuneVariable>(tref.NativePtr);
            nv.Type = (int)LuaType.Thread;
            nv.Integer = tnv.Integer;

            List<IntPtr>? ptrs = null;
            KitsuneVariable[]? native = null;
            if (args is { Length: > 0 })
            {
                native = new KitsuneVariable[args.Length];
                for (int i = 0; i < args.Length; i++)
                {
                    FillNativeVariable(ref native[i], args[i], ref ptrs);
                }
            }

            try
            {
                IntPtr resultPtr = KitsuneExecuteVariable(ref nv, native?.Length ?? 0, native);
                LuaValue result = NativePtrToLuaValue(resultPtr);
                if (result.Type == LuaType.Error)
                {
                    throw new LuaException(result.String ?? "thread error");
                }

                return result;
            }
            finally
            {
                FreeNativeArgs(ptrs);
            }
        }

        /// <summary>
        /// Asynchronously steps the coroutine thread once, optionally passing <paramref name="args"/>
        /// as the result of the current <c>coroutine.yield()</c> (or as initial parameters on the
        /// first call). Returns the first value yielded or returned by this step.
        /// Returns <see cref="LuaValue.None"/> when the thread is dead.
        /// Returns a nil <see cref="LuaValue"/> when the thread yielded without a value.
        /// Throws <see cref="LuaException"/> if the thread raises a Lua error.
        /// </summary>
        internal static async Task<LuaValue> StepThreadAsync(LuaThreadRef tref, LuaValue[]? args, CancellationToken cancellationToken)
        {
            if (inLuaCallback)
            {
                throw new LuaException("cannot be called from within a registered function");
            }

            var nv = default(KitsuneVariable);
            var tnv = Marshal.PtrToStructure<KitsuneVariable>(tref.NativePtr);
            nv.Type = (int)LuaType.Thread;
            nv.Integer = tnv.Integer;

            List<IntPtr>? ptrs = null;
            KitsuneVariable[]? native = null;
            if (args is { Length: > 0 })
            {
                native = new KitsuneVariable[args.Length];
                for (int i = 0; i < args.Length; i++)
                {
                    FillNativeVariable(ref native[i], args[i], ref ptrs);
                }
            }

            int id = -1;
            try
            {
                id = KitsuneExecuteVariableAsync(ref nv, native?.Length ?? 0, native, false);
            }
            finally
            {
                FreeNativeArgs(ptrs);
            }

            if (id < 0)
            {
                throw new InvalidOperationException("Failed to start thread step.");
            }

            try
            {
                await WaitForIdAsync(id, cancellationToken).ConfigureAwait(false);
            }
            catch (OperationCanceledException)
            {
                KitsuneCancel(id);
                throw;
            }

            string? error = GetError(id);
            LuaValue result = GetResultVariable(id);
            if (!string.IsNullOrEmpty(error))
            {
                throw new LuaException(error);
            }

            return result;
        }

        /// <summary>
        /// Asynchronously iterates over a Lua coroutine thread, yielding each value it produces.
        /// Each <c>coroutine.yield(v)</c> or final <c>return v</c> in the thread produces one
        /// element. Iteration ends when the thread is dead or yields/returns nothing
        /// (<see cref="LuaType.None"/>). Raises <see cref="LuaException"/> if the thread errors.
        /// <para>
        /// The <paramref name="thread"/> value must be a <see cref="LuaType.Thread"/> with a
        /// live <see cref="LuaValue.ThreadRef"/>; obtain it from a script that returns
        /// <c>coroutine.create(...)</c>.
        /// </para>
        /// </summary>
        /// <exception cref="LuaException">Thrown if called from within a registered function callback,
        /// or if the coroutine raises a Lua runtime error.</exception>
        internal static async IAsyncEnumerable<LuaValue> IterateThreadAsync(
            LuaThreadRef tref,
            [System.Runtime.CompilerServices.EnumeratorCancellation] CancellationToken cancellationToken = default)
        {
            if (inLuaCallback)
            {
                throw new LuaException("cannot be called from within a registered function");
            }

            var nv = default(KitsuneVariable);
            var tnv = Marshal.PtrToStructure<KitsuneVariable>(tref.NativePtr);
            nv.Type = (int)LuaType.Thread;
            nv.Integer = tnv.Integer;

            while (true)
            {
                cancellationToken.ThrowIfCancellationRequested();

                int id = KitsuneExecuteVariableAsync(ref nv, 0, null, false);
                if (id < 0)
                {
                    throw new InvalidOperationException("Failed to start thread step coroutine.");
                }

                try
                {
                    await WaitForIdAsync(id, cancellationToken).ConfigureAwait(false);
                }
                catch (OperationCanceledException)
                {
                    KitsuneCancel(id);
                    throw;
                }

                string? error = GetError(id);
                LuaValue result = GetResultVariable(id);

                if (!string.IsNullOrEmpty(error))
                {
                    throw new LuaException(error);
                }

                if (result.Type == LuaType.None)
                {
                    yield break;
                }

                yield return result;
            }
        }

        /// <summary>
        /// Synchronously iterates over a Lua coroutine thread, yielding each value it produces.
        /// Each <c>coroutine.yield(v)</c> or final <c>return v</c> in the thread produces one
        /// element. Iteration ends when the thread is dead or yields/returns nothing
        /// (<see cref="LuaType.None"/>). Raises <see cref="LuaException"/> if the thread errors.
        /// <para>
        /// The <paramref name="thread"/> value must be a <see cref="LuaType.Thread"/> with a
        /// live <see cref="LuaValue.ThreadRef"/>; obtain it from a script that returns
        /// <c>coroutine.create(...)</c>.
        /// </para>
        /// </summary>
        /// <exception cref="LuaException">Thrown if called from within a registered function callback,
        /// or if the coroutine raises a Lua runtime error.</exception>
        internal static IEnumerable<LuaValue> IterateThread(
            LuaThreadRef tref,
            CancellationToken cancellationToken = default)
        {
            if (inLuaCallback)
            {
                throw new LuaException("cannot be called from within a registered function");
            }

            var nv = default(KitsuneVariable);
            var tnv2 = Marshal.PtrToStructure<KitsuneVariable>(tref.NativePtr);
            nv.Type = (int)LuaType.Thread;
            nv.Integer = tnv2.Integer;

            while (true)
            {
                cancellationToken.ThrowIfCancellationRequested();

                int id = KitsuneExecuteVariableAsync(ref nv, 0, null, false);
                if (id < 0)
                {
                    throw new InvalidOperationException("Failed to start thread step coroutine.");
                }

                try
                {
                    while (!HasResult(id))
                    {
                        cancellationToken.ThrowIfCancellationRequested();
                        if ((CoroutineStatus)KitsuneGetStatus(id) == CoroutineStatus.None)
                        {
                            break;
                        }

                        Thread.Sleep(1);
                    }
                }
                catch (OperationCanceledException)
                {
                    KitsuneCancel(id);
                    throw;
                }

                string? error = GetError(id);
                LuaValue result = GetResultVariable(id);

                if (!string.IsNullOrEmpty(error))
                {
                    throw new LuaException(error);
                }

                if (result.Type == LuaType.None)
                {
                    yield break;
                }

                yield return result;
            }
        }

        #region P/Invoke

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        [return: MarshalAs(UnmanagedType.I1)]
        private static extern bool KitsuneInit(IntPtr memoryAllocator);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        private static extern void KitsuneVariableFree(IntPtr var);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        private static extern int KitsuneExecuteFileAsync([MarshalAs(UnmanagedType.LPUTF8Str)] string path, int argc, KitsuneVariable[]? argv, [MarshalAs(UnmanagedType.I1)] bool fireAndForget);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        private static extern int KitsuneExecuteStringAsync([MarshalAs(UnmanagedType.LPUTF8Str)] string script, int argc, KitsuneVariable[]? argv, [MarshalAs(UnmanagedType.I1)] bool fireAndForget);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        private static extern int KitsuneExecuteFunctionAsync([MarshalAs(UnmanagedType.LPUTF8Str)] string functionName, int argc, KitsuneVariable[]? argv, [MarshalAs(UnmanagedType.I1)] bool fireAndForget);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr KitsuneExecuteFile([MarshalAs(UnmanagedType.LPUTF8Str)] string path, int argc, KitsuneVariable[]? argv);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr KitsuneExecuteString([MarshalAs(UnmanagedType.LPUTF8Str)] string script, int argc, KitsuneVariable[]? argv);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr KitsuneExecuteFunction([MarshalAs(UnmanagedType.LPUTF8Str)] string functionName, int argc, KitsuneVariable[]? argv);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        private static extern int KitsuneExecuteVariableAsync(ref KitsuneVariable var, int argc, KitsuneVariable[]? argv, [MarshalAs(UnmanagedType.I1)] bool fireAndForget);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr KitsuneExecuteVariable(ref KitsuneVariable var, int argc, KitsuneVariable[]? argv);

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

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        [return: MarshalAs(UnmanagedType.I1)]
        private static extern bool KitsuneSetVariable([MarshalAs(UnmanagedType.LPUTF8Str)] string name, ref KitsuneVariable var);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr KitsuneGetVariable([MarshalAs(UnmanagedType.LPUTF8Str)] string name);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        private static extern int KitsuneGetActiveIds(int[]? buffer, int bufferSize);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        private static extern nuint KitsuneCleanup();

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        private static extern int KitsuneGC(int mode);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        private static extern void KitsuneGetAll([MarshalAs(UnmanagedType.LPUTF8Str)] string? path, GetAllCallback callback, IntPtr userdata);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        private static extern int KitsuneRegister(ref KitsuneVariable var);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr KitsuneGetByReference(int @ref);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr KitsuneUnregister(int @ref);

        // func is a delegate* unmanaged[Cdecl] cast to nint; userdata is a GCHandle address.
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        private static extern void KitsuneRegisterFunction([MarshalAs(UnmanagedType.LPUTF8Str)] string name, nint func, nint userdata);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr KitsuneCreateMemoryBlock(nuint size);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        [return: MarshalAs(UnmanagedType.I1)]
        private static extern bool KitsuneRegisterUserdata([MarshalAs(UnmanagedType.LPUTF8Str)] string name, IntPtr registration);

        #endregion

        private static unsafe nint GetTrampolinePtr() =>
            (nint)(delegate* unmanaged[Cdecl]<int, KitsuneVariable*, nint, void*, int>)&LuaFunctionTrampoline;

        // Converts a LuaValue[] to a KitsuneVariable[] suitable for P/Invoke.
        // String data is heap-allocated; caller MUST call FreeNativeArgs when done.
        private static (KitsuneVariable[]? Native, List<IntPtr>? Ptrs) BuildNativeArgs(LuaValue[]? args)
        {
            if (args is null || args.Length == 0)
            {
                return (null, null);
            }

            var native = new KitsuneVariable[args.Length];
            List<IntPtr>? ptrs = null;
            for (int i = 0; i < args.Length; i++)
            {
                FillNativeVariable(ref native[i], args[i], ref ptrs);
            }

            return (native, ptrs);
        }

        private static void FreeNativeArgs(List<IntPtr>? ptrs)
        {
            if (ptrs is null)
            {
                return;
            }

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

        // wcharCount is the char16_t count; each char16_t is 2 bytes (UTF-16 LE).
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

        // Unmarshals a KitsuneUserDataNative { char* name, void* userdata } pointed to by ptr.
        // nv.Length carries the name byte count so the name string can be copied without strlen.
        private static LuaValue NativeUnmarshalUserdata(IntPtr ptr, nuint nameLen)
        {
            if (ptr == IntPtr.Zero)
            {
                return new LuaValue { Type = LuaType.Userdata };
            }
            var kud = Marshal.PtrToStructure<KitsuneUserDataNative>(ptr);
            byte[]? nameBytes = null;
            if (kud.Name != IntPtr.Zero && nameLen > 0)
            {
                int len = (int)nameLen;
                nameBytes = new byte[len];
                Marshal.Copy(kud.Name, nameBytes, 0, len);
            }
            return new LuaValue { Type = LuaType.Userdata, Bytes = nameBytes, UserdataGCHandlePtr = kud.Userdata };
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
        // For LuaType.Function the pointer is NOT freed here; ownership transfers to LuaFunctionRef.
        private static LuaValue NativePtrToLuaValue(IntPtr ptr)
        {
            if (ptr == IntPtr.Zero)
            {
                return LuaValue.None;
            }

            // ReadUnaligned is a direct memory load on the blittable struct,
            // avoiding the marshalling-layer overhead of Marshal.PtrToStructure.
            KitsuneVariable nv;
            unsafe
            {
                nv = Unsafe.ReadUnaligned<KitsuneVariable>((void*)ptr);
            }

            LuaType t = (LuaType)nv.Type;

            // Function / Thread: transfer the native pointer to a LuaFunctionRef / LuaThreadRef;
            // the ref keeps the Lua registry entry alive until Dispose() calls KitsuneVariableFree.
            // KitsuneVariableFree must NOT be called here — the ref's Dispose() does it.
            if (t == LuaType.Function)
            {
                return new LuaValue { Type = LuaType.Function, FunctionRef = new LuaFunctionRef(ptr) };
            }

            if (t == LuaType.Thread)
            {
                return new LuaValue { Type = LuaType.Thread, ThreadRef = new LuaThreadRef(ptr) };
            }
            LuaValue result = t switch
            {
                LuaType.Number => LuaValue.FromNumber(nv.Number),
                LuaType.Integer => LuaValue.FromInt64(nv.Integer),
                LuaType.Boolean => LuaValue.FromBool(nv.BoolByte != 0),
                LuaType.String when nv.Data != IntPtr.Zero => NativeCopyBytes(nv.Data, nv.Length),
                LuaType.Char16 when nv.Data != IntPtr.Zero => NativeCopyChar16(nv.Data, nv.Length),
                LuaType.Userdata when nv.Data != IntPtr.Zero => NativeUnmarshalUserdata(nv.Data, nv.Length),
                LuaType.Json when nv.Data != IntPtr.Zero && nv.Length > 0 => NativeParseJson(nv.Data, nv.Length),
                LuaType.Stream when nv.Data != IntPtr.Zero => NativeWrapSharedMemory(nv.Data),
                LuaType.Table => ReadNativeTable(nv.Data),
                LuaType.Error when nv.Data != IntPtr.Zero => NativeCopyBytes(nv.Data, nv.Length) with { Type = LuaType.Error },
                LuaType.None => LuaValue.None,
                _ => new LuaValue { Type = t },  // Nil/Userdata/LightUserdata
            };
            KitsuneVariableFree(ptr);
            return result;
        }

        // Converts a by-value KitsuneVariable (already marshaled into managed memory) to a LuaValue.
        // Does NOT free any native memory — use this for embedded struct members, not heap pointers.
        // Function values are returned as opaque (Type=Function, no FunctionRef) since the variable
        // is embedded inside a larger allocation (table node or callback args array).
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
                LuaType.Userdata when nv.Data != IntPtr.Zero => NativeUnmarshalUserdata(nv.Data, nv.Length),
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

        // Builds a native linked list from a managed table. Every allocation is appended to ptrs (lazily created) for cleanup.
        private static IntPtr BuildNativeTable(
            IReadOnlyList<KeyValuePair<LuaValue, LuaValue>> entries, ref List<IntPtr>? ptrs)
        {
            if (entries.Count == 0)
            {
                return IntPtr.Zero;
            }
            ptrs ??= new List<IntPtr>();
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
                FillNativeVariable(ref n.Key, entries[i].Key, ref ptrs);
                FillNativeVariable(ref n.Value, entries[i].Value, ref ptrs);
                n.Next = i + 1 < entries.Count ? nodes[i + 1] : IntPtr.Zero;
                Marshal.StructureToPtr(n, nodes[i], false);
            }
            return nodes[0];
        }

        // Fills a single KitsuneVariable struct for native pass-through; string and table data are
        // heap-allocated and appended to ptrs (lazily created if null) for cleanup by FreeNativeArgs.
        private static void FillNativeVariable(ref KitsuneVariable nv, LuaValue v, ref List<IntPtr>? ptrs)
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
                        ptrs ??= new List<IntPtr>();
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
                        ptrs ??= new List<IntPtr>();
                        ptrs.Add(p);
                        nv.Data = p;
                        nv.Length = (nuint)(wbytes.Length / 2);
                    }
                    break;
                case LuaType.Function when v.FunctionRef is { NativePtr: 0 }:
                    throw new ObjectDisposedException(nameof(LuaFunctionRef),
                        "Cannot marshal a disposed LuaFunctionRef across the native bridge.");
                case LuaType.Function when v.FunctionRef is { } fr && fr.NativePtr != IntPtr.Zero:
                    {
                        // Copy the registry ref integer from the native KitsuneVariable.
                        // PushKitsuneVariable uses lua_rawgeti with this ref to push the function.
                        var fnv = Marshal.PtrToStructure<KitsuneVariable>(fr.NativePtr);
                        nv.Integer = fnv.Integer;
                        break;
                    }
                case LuaType.Thread when v.ThreadRef is { NativePtr: 0 }:
                    throw new ObjectDisposedException(nameof(LuaThreadRef),
                        "Cannot marshal a disposed thread ref across the native bridge.");
                case LuaType.Thread when v.ThreadRef is { } tfr && tfr.NativePtr != IntPtr.Zero:
                    {
                        // Copy the registry ref integer from the native KitsuneVariable.
                        // PushKitsuneVariable uses lua_rawgeti with this ref to push the thread.
                        var tnv = Marshal.PtrToStructure<KitsuneVariable>(tfr.NativePtr);
                        nv.Integer = tnv.Integer;
                        break;
                    }
                case LuaType.Table when v.Table is not null:
                    nv.Data = BuildNativeTable(v.Table, ref ptrs);
                    nv.Length = (nuint)v.Table.Count;
                    break;
                case LuaType.Json when v.JsonNode is not null:
                    {
                        byte[] json = JsonSerializer.SerializeToUtf8Bytes(v.JsonNode);
                        IntPtr p = Marshal.AllocHGlobal(json.Length + 1);
                        Marshal.Copy(json, 0, p, json.Length);
                        Marshal.WriteByte(p, json.Length, 0);
                        ptrs ??= new List<IntPtr>();
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
                case LuaType.CFunction when v.CFunctionValue is LuaFunction luaFunc:
                    {
                        // Allocate a GCHandle to keep the delegate alive while Lua may call the closure.
                        // The handle is added to s_globalCFunctionHandles and freed when the last engine
                        // is disposed (same lifetime as the Lua state that owns the closure).
                        var handle = GCHandle.Alloc(luaFunc);
                        GlobalCFunctionHandles.Add(handle);

                        // Allocate a kitsune_CFunctionData { func, userdata } on the unmanaged heap.
                        // PushKitsuneVariable copies the two pointer values into Lua upvalue slots, so
                        // this struct only needs to survive until the native call returns.
                        IntPtr structPtr = Marshal.AllocHGlobal(IntPtr.Size * 2);
                        Marshal.WriteIntPtr(structPtr, 0, GetTrampolinePtr());
                        Marshal.WriteIntPtr(structPtr, IntPtr.Size, GCHandle.ToIntPtr(handle));
                        ptrs ??= new List<IntPtr>();
                        ptrs.Add(structPtr);
                        nv.Data = structPtr;
                        break;
                    }
                case LuaType.Iterator when v.IteratorValue is LuaIteratorRef iterRef:
                    {
                        // IteratorState holds the source ref and, lazily, the enumerator created on first
                        // Lua call.  Both stepFunc and finalizeFunc close over the same instance.
                        // GCHandles are self-cleaning: freed inside finalizeFunc when Lua GCs the closure,
                        // NOT added to GlobalCFunctionHandles.
                        var iterState = new IteratorState();

                        LuaFunction stepFunc = _ =>
                        {
                            if (iterRef.IsCancelled)
                            {
                                return LuaValue.None;
                            }

                            iterState.Enumerator ??= iterRef.GetSyncEnumerator();
                            if (iterState.Enumerator is null || !iterState.Enumerator.MoveNext())
                            {
                                return LuaValue.None;
                            }

                            return iterState.Enumerator.Current;
                        };

                        LuaFunction finalizeFunc = _ =>
                        {
                            iterState.Enumerator?.Dispose();
                            iterState.Enumerator = null;
                            if (iterState.StepHandle.IsAllocated)
                            {
                                iterState.StepHandle.Free();
                            }

                            if (iterState.FinalizeHandle.IsAllocated)
                            {
                                iterState.FinalizeHandle.Free();
                            }

                            return LuaValue.None;
                        };

                        iterState.StepHandle = GCHandle.Alloc(stepFunc);
                        iterState.FinalizeHandle = GCHandle.Alloc(finalizeFunc);

                        // kitsune_CFunctionData for step — first and next share the same struct
                        // because IEnumerator.MoveNext() is already stateful.
                        IntPtr stepCFD = Marshal.AllocHGlobal(IntPtr.Size * 2);
                        Marshal.WriteIntPtr(stepCFD, 0, GetTrampolinePtr());
                        Marshal.WriteIntPtr(stepCFD, IntPtr.Size, GCHandle.ToIntPtr(iterState.StepHandle));
                        ptrs ??= new List<IntPtr>();
                        ptrs.Add(stepCFD);

                        // kitsune_CFunctionData for finalized
                        IntPtr finCFD = Marshal.AllocHGlobal(IntPtr.Size * 2);
                        Marshal.WriteIntPtr(finCFD, 0, GetTrampolinePtr());
                        Marshal.WriteIntPtr(finCFD, IntPtr.Size, GCHandle.ToIntPtr(iterState.FinalizeHandle));
                        ptrs.Add(finCFD);

                        // KitsuneIterator { first*, next*, finalized*, userdata }
                        IntPtr iterStruct = Marshal.AllocHGlobal(IntPtr.Size * 4);
                        Marshal.WriteIntPtr(iterStruct, 0, stepCFD);
                        Marshal.WriteIntPtr(iterStruct, IntPtr.Size, stepCFD);
                        Marshal.WriteIntPtr(iterStruct, IntPtr.Size * 2, finCFD);
                        Marshal.WriteIntPtr(iterStruct, IntPtr.Size * 3, IntPtr.Zero);
                        ptrs.Add(iterStruct);

                        nv.Data = iterStruct;

                        // GCHandles intentionally NOT in ptrs — freed by finalizeFunc.
                        break;
                    }
                case LuaType.Userdata when v.Bytes is not null && v.UserdataGCHandlePtr != 0:
                    {
                        // Allocate a KitsuneUserData { char* name, void* userdata } on the unmanaged heap.
                        // PushKitsuneVariable reads it to fill LuaKitsuneUserdata and apply the metatable.
                        byte[] nameBytes = v.Bytes;
                        IntPtr namePtr = Marshal.AllocHGlobal(nameBytes.Length + 1);
                        Marshal.Copy(nameBytes, 0, namePtr, nameBytes.Length);
                        Marshal.WriteByte(namePtr, nameBytes.Length, 0);

                        IntPtr structPtr = Marshal.AllocHGlobal(IntPtr.Size * 2);
                        Marshal.WriteIntPtr(structPtr, 0, namePtr);
                        Marshal.WriteIntPtr(structPtr, IntPtr.Size, v.UserdataGCHandlePtr);

                        ptrs ??= new List<IntPtr>();
                        ptrs.Add(namePtr);
                        ptrs.Add(structPtr);
                        nv.Data = structPtr;
                        nv.Length = (nuint)nameBytes.Length;
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

        // Fills a single KitsuneVariable for the variable-to-execute and its argument list.
        // Heap allocations for strings, tables etc. are collected lazily in ptrs; pass to FreeNativeArgs.
        private static (KitsuneVariable Var, KitsuneVariable[]? Args, List<IntPtr>? Ptrs) BuildVariableAndArgs(
            LuaValue variable, LuaValue[]? args)
        {
            List<IntPtr>? ptrs = null;
            var nv = default(KitsuneVariable);
            FillNativeVariable(ref nv, variable, ref ptrs);

            KitsuneVariable[]? native = null;
            if (args is { Length: > 0 })
            {
                native = new KitsuneVariable[args.Length];
                for (int i = 0; i < args.Length; i++)
                {
                    FillNativeVariable(ref native[i], args[i], ref ptrs);
                }
            }

            return (nv, native, ptrs);
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
                if (result.Type == LuaType.Error)
                {
                    InvokeResultSetterError(resultSetterPtr, result.String ?? string.Empty);
                    return 0;
                }

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
            List<IntPtr>? ptrs = null;
            try
            {
                var nv = default(KitsuneVariable);
                FillNativeVariable(ref nv, result, ref ptrs);
                setter(&nv);
            }
            finally
            {
                FreeNativeArgs(ptrs);
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

        private static void FreeNamedFunctionList(IntPtr head)
        {
            while (head != IntPtr.Zero)
            {
                IntPtr namePtr = Marshal.ReadIntPtr(head, 0);
                IntPtr next = Marshal.ReadIntPtr(head, IntPtr.Size * 3);
                Marshal.FreeHGlobal(namePtr);
                Marshal.FreeHGlobal(head);
                head = next;
            }
        }

        /// <summary>Returns <c>true</c> once the coroutine has finished (success or error).</summary>
        private static bool HasResult(int id) => KitsuneHasResult(id, out _);

        /// <summary>Returns the error string for a finished coroutine, or <c>null</c> if none.</summary>
        private static string? GetError(int id)
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

        private static async Task WaitForIdAsync(int id, CancellationToken cancellationToken)
        {
            while (!HasResult(id))
            {
                cancellationToken.ThrowIfCancellationRequested();
                if ((CoroutineStatus)KitsuneGetStatus(id) == CoroutineStatus.None)
                {
                    return;
                }

                await Task.Delay(1, cancellationToken).ConfigureAwait(false);
            }
        }

        /// <summary>Returns the typed result and releases the slot.</summary>
        private static LuaValue GetResultVariable(int id) => NativePtrToLuaValue(KitsuneGetResult(id));

        private bool RegisterUserdataCore(
            string name,
            IReadOnlyDictionary<string, LuaFunction> methods,
            IReadOnlyDictionary<string, LuaFunction> metaMethods)
        {
            // Inject __gc: call any user-supplied __gc first, then always free the GCHandle.
            LuaFunction? userGc = metaMethods.GetValueOrDefault("__gc");
            LuaFunction gcWrapper = args =>
            {
                nint handlePtr = args.Count > 0 ? args[0].UserdataGCHandlePtr : 0;
                try
                {
                    userGc?.Invoke(args);
                }
                finally
                {
                    if (handlePtr != 0)
                    {
                        var h = GCHandle.FromIntPtr(handlePtr);
                        if (h.IsAllocated)
                        {
                            h.Free();
                        }
                    }
                }

                return LuaValue.None;
            };

            IntPtr funcHead = IntPtr.Zero;
            IntPtr metaHead = IntPtr.Zero;
            IntPtr reg = IntPtr.Zero;
            try
            {
                foreach (var kvp in methods)
                {
                    funcHead = AllocNamedFunction(kvp.Key, kvp.Value, funcHead);
                }

                metaHead = AllocNamedFunction("__gc", gcWrapper, IntPtr.Zero);
                foreach (var kvp in metaMethods)
                {
                    if (kvp.Key == "__gc")
                    {
                        continue;
                    }

                    metaHead = AllocNamedFunction(kvp.Key, kvp.Value, metaHead);
                }

                // KitsuneUserDataRegistration { MetaTableFunctions*, Functions* }
                reg = Marshal.AllocHGlobal(IntPtr.Size * 2);
                Marshal.WriteIntPtr(reg, 0, metaHead);
                Marshal.WriteIntPtr(reg, IntPtr.Size, funcHead);

                return KitsuneRegisterUserdata(name, reg);
            }
            finally
            {
                // Free temporary structs — the engine copied method info into Lua metatables.
                // GCHandles for the LuaFunction delegates remain alive in _functionHandles.
                FreeNamedFunctionList(funcHead);
                FreeNamedFunctionList(metaHead);
                if (reg != IntPtr.Zero)
                {
                    Marshal.FreeHGlobal(reg);
                }
            }
        }

        // Allocates a NamedKitsuneFunction { char* name, func, void* userdata, Next* } node.
        // The LuaFunction delegate is rooted in _functionHandles; the name string and node itself
        // are freed by FreeNamedFunctionList once KitsuneRegisterUserdata has consumed them.
        private IntPtr AllocNamedFunction(string name, LuaFunction func, IntPtr next)
        {
            _functionHandles ??= new List<GCHandle>();
            var handle = GCHandle.Alloc(func);
            _functionHandles.Add(handle);

            byte[] nameBytes = Encoding.UTF8.GetBytes(name);
            IntPtr namePtr = Marshal.AllocHGlobal(nameBytes.Length + 1);
            Marshal.Copy(nameBytes, 0, namePtr, nameBytes.Length);
            Marshal.WriteByte(namePtr, nameBytes.Length, 0);

            // NamedKitsuneFunction { char* name, kitsune_CFunction func, void* userdata, NamedKitsuneFunction* Next }
            // Each field is pointer-sized on x64 (32 bytes total).
            IntPtr node = Marshal.AllocHGlobal(IntPtr.Size * 4);
            Marshal.WriteIntPtr(node, 0, namePtr);
            Marshal.WriteIntPtr(node, IntPtr.Size, GetTrampolinePtr());
            Marshal.WriteIntPtr(node, IntPtr.Size * 2, GCHandle.ToIntPtr(handle));
            Marshal.WriteIntPtr(node, IntPtr.Size * 3, next);
            return node;
        }

        private void Dispose(bool disposing)
        {
            if (Interlocked.Exchange(ref _disposed, 1) == 0)
            {
                // Transfer this engine's handles to the shared bags BEFORE decrementing
                // the ref-count.  The Lua state is process-wide: delegates and userdata
                // pins registered by any engine must stay valid until lua_close fires all
                // __gc callbacks inside KitsuneCleanup.  Only the last engine calls
                // KitsuneCleanup, so handles must not be freed while earlier engines dispose.
                //
                // Safety: transfer-before-decrement is the key invariant.  When a decrement
                // reaches zero, every previously-decremented engine has already completed its
                // transfer, so KitsuneCleanup sees all handles in the global bags.
                if (disposing)
                {
                    if (_functionHandles is not null)
                    {
                        foreach (var h in _functionHandles)
                        {
                            GlobalFunctionHandles.Add(h);
                        }

                        _functionHandles = null;
                    }

                    if (_userdataHandles is not null)
                    {
                        foreach (var h in _userdataHandles)
                        {
                            GlobalUserdataHandles.Add(h);
                        }

                        _userdataHandles = null;
                    }
                }

                if (Interlocked.Decrement(ref _refCount) == 0)
                {
                    // All LuaFunctionRef / LuaThreadRef instances must be disposed by the caller
                    // before this point.  Disposing them enqueues their KitsuneVariable* to
                    // g_pendingVariableChainHead; DrainPendingVariableChain inside KitsuneCleanup
                    // processes those frees while the Lua state is still live.
                    LeakedAllocations = (ulong)KitsuneCleanup();

                    // Safe to free all accumulated handles now that the Lua state is gone.
                    while (GlobalCFunctionHandles.TryTake(out var h))
                    {
                        h.Free();
                    }

                    // Dispatch lambdas and gcWrappers: never freed by __gc, always freed here.
                    while (GlobalFunctionHandles.TryTake(out var h))
                    {
                        h.Free();
                    }

                    // Userdata instance pins: __gc may have already freed some during lua_close.
                    while (GlobalUserdataHandles.TryTake(out var h))
                    {
                        if (h.IsAllocated)
                        {
                            h.Free();
                        }
                    }
                }
            }
        }

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

        // Mirrors KitsuneUserData: { char* name, void* userdata }.
        // nv.Data points to one of these for every LUA_TUSERDATA variable produced by the engine.
        [StructLayout(LayoutKind.Sequential)]
        private struct KitsuneUserDataNative
        {
            public IntPtr Name;     // heap-allocated UTF-8 name string
            public IntPtr Userdata; // GCHandle address for Kitsune-registered userdatas; IntPtr.Zero otherwise
        }

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

        // Holds the lazy enumerator and GCHandles for a KITSUNE_TITERATOR marshal.
        // Shared by stepFunc and finalizeFunc closures inside FillNativeVariable.
        private sealed class IteratorState
        {
            public IEnumerator<LuaValue>? Enumerator;
            public GCHandle StepHandle;
            public GCHandle FinalizeHandle;
        }
    }
}
