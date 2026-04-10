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
        // Unlike RegisterFunction handles (per-engine, freed on individual dispose), these
        // are tied to the shared Lua state and freed when the last engine is disposed.
        private static readonly System.Collections.Concurrent.ConcurrentBag<GCHandle> GlobalCFunctionHandles = new();

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

        public KitsuneEngine()
        {
            if (!KitsuneInit(IntPtr.Zero))
            {
                throw new InvalidOperationException("KitsuneInit failed");
            }

            // When this becomes the sole live engine and the scheduler is already running,
            // there are orphaned coroutines from a previous engine that was not properly
            // disposed (e.g. abandoned by a test-runner abort).  Interrupt and drain them
            // now so they cannot block Wait() calls issued by this engine.
            if (Interlocked.Increment(ref _refCount) == 1 && KitsuneIsRunning())
            {
                KitsuneInterrupt();
                KitsuneWait();
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
        /// For Lua runtime error details use <see cref="ExecuteFileAsync"/> instead.</summary>
        /// <exception cref="LuaException">Thrown if called from within a registered function callback,
        /// or if the native engine rejects the call (e.g. re-entrant invocation).</exception>
        public LuaValue RunFile(string path, params LuaValue[]? args)
        {
            if (inLuaCallback)
            {
                throw new LuaException("cannot be called from within a registered function");
            }

            var (native, ptrs) = BuildNativeArgs(args);
            try
            {
                return NativePtrToLuaValue(KitsuneExecuteFile(path, native?.Length ?? 0, native), this).GetOrThrow();
            }
            finally
            {
                FreeNativeArgs(ptrs);
            }
        }

        /// <summary>Runs a Lua script string synchronously and returns the typed result.
        /// Returns <see cref="LuaValue.None"/> on start failure or if the script returned nothing.
        /// For Lua runtime error details use <see cref="ExecuteStringAsync"/> instead.</summary>
        /// <exception cref="LuaException">Thrown if called from within a registered function callback,
        /// or if the native engine rejects the call (e.g. re-entrant invocation).</exception>
        public LuaValue RunString(string script, params LuaValue[]? args)
        {
            if (inLuaCallback)
            {
                throw new LuaException("cannot be called from within a registered function");
            }

            var (native, ptrs) = BuildNativeArgs(args);
            try
            {
                return NativePtrToLuaValue(KitsuneExecuteString(script, native?.Length ?? 0, native), this).GetOrThrow();
            }
            finally
            {
                FreeNativeArgs(ptrs);
            }
        }

        /// <summary>Calls a global Lua function synchronously and returns the typed result.
        /// Returns <see cref="LuaValue.None"/> on start failure or if the function returned nothing.
        /// For Lua runtime error details use <see cref="ExecuteFunctionAsync"/> instead.</summary>
        /// <exception cref="LuaException">Thrown if called from within a registered function callback,
        /// or if the native engine rejects the call (e.g. re-entrant invocation).</exception>
        public LuaValue RunFunction(string functionName, params LuaValue[]? args)
        {
            if (inLuaCallback)
            {
                throw new LuaException("cannot be called from within a registered function");
            }

            var (native, ptrs) = BuildNativeArgs(args);
            try
            {
                return NativePtrToLuaValue(KitsuneExecuteFunction(functionName, native?.Length ?? 0, native), this).GetOrThrow();
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
        /// Returns <see cref="LuaValue.None"/> on start failure or if execution returned nothing.
        /// See <see cref="ExecuteVariable"/> for dispatch rules.</summary>
        /// <exception cref="LuaException">Thrown if called from within a registered function callback,
        /// or if the native engine rejects the call (e.g. re-entrant invocation).</exception>
        public LuaValue RunVariable(LuaValue variable, params LuaValue[]? args)
        {
            if (inLuaCallback)
            {
                throw new LuaException("cannot be called from within a registered function");
            }

            var (nv, native, ptrs) = BuildVariableAndArgs(variable, args);
            try
            {
                return NativePtrToLuaValue(KitsuneExecuteVariable(ref nv, native?.Length ?? 0, native), this).GetOrThrow();
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
        public LuaValue GetVariable(string name)
        {
            if (inLuaCallback)
            {
                throw new LuaException("GetVariable cannot be called from within a registered function");
            }

            return NativePtrToLuaValue(KitsuneGetVariable(name), this);
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

        public void Dispose()
        {
            Dispose(true);
            GC.SuppressFinalize(this);
        }

        /// <summary>Releases a heap-allocated <c>KitsuneVariable*</c> via the native free function.
        /// Called by <see cref="LuaFunctionRef.Dispose"/> to release a Lua registry reference.
        /// Must not be called while the pointer is still in use.</summary>
        internal static void ReleaseNativeVariable(IntPtr ptr) => KitsuneVariableFree(ptr);

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
        internal async IAsyncEnumerable<LuaValue> IterateThreadAsync(
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
        internal IEnumerable<LuaValue> IterateThread(
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
                    Wait(id, cancellationToken);
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

                if (result.Type == LuaType.None)
                {
                    yield break;
                }

                yield return result;
            }
        }

        private static unsafe nint GetTrampolinePtr() =>
           (nint)(delegate* unmanaged[Cdecl]<int, KitsuneVariable*, nint, void*, int>)&LuaFunctionTrampoline;

        #region P/Invoke

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        [return: MarshalAs(UnmanagedType.I1)]
        private static extern bool KitsuneInit(IntPtr initFunc);

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
        private static extern void KitsuneGetAll([MarshalAs(UnmanagedType.LPUTF8Str)] string? path, GetAllCallback callback, IntPtr userdata);

        // func is a delegate* unmanaged[Cdecl] cast to nint; userdata is a GCHandle address.
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        private static extern void KitsuneRegisterFunction([MarshalAs(UnmanagedType.LPUTF8Str)] string name, nint func, nint userdata);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr KitsuneCreateMemoryBlock(nuint size);

        #endregion

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
        private static LuaValue NativePtrToLuaValue(IntPtr ptr, KitsuneEngine? engine = null)
        {
            if (ptr == IntPtr.Zero)
            {
                return LuaValue.None;
            }
            var nv = Marshal.PtrToStructure<KitsuneVariable>(ptr);
            LuaType t = (LuaType)nv.Type;

            // Function / Thread: transfer the native pointer to a LuaFunctionRef / LuaThreadRef;
            // the ref keeps the Lua registry entry alive until Dispose() calls KitsuneVariableFree.
            // KitsuneVariableFree must NOT be called here — the ref's Dispose() does it.
            if (t == LuaType.Function)
            {
                return new LuaValue
                {
                    Type = LuaType.Function,
                    FunctionRef = new LuaFunctionRef(ptr, engine)
                };
            }

            if (t == LuaType.Thread)
            {
                return new LuaValue
                {
                    Type = LuaType.Thread,
                    ThreadRef = new LuaThreadRef(ptr, engine)
                };
            }
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
        // All heap allocations are added to ptrs; caller must pass the array to FreeNativeArgs.
        private static (KitsuneVariable Var, KitsuneVariable[]? Args, IntPtr[] Ptrs) BuildVariableAndArgs(
            LuaValue variable, LuaValue[]? args)
        {
            var ptrs = new List<IntPtr>();
            var nv = default(KitsuneVariable);
            FillNativeVariable(ref nv, variable, ptrs);

            KitsuneVariable[]? native = null;
            if (args is { Length: > 0 })
            {
                native = new KitsuneVariable[args.Length];
                for (int i = 0; i < args.Length; i++)
                {
                    FillNativeVariable(ref native[i], args[i], ptrs);
                }
            }

            return (nv, native, [.. ptrs]);
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

        /// <summary>Returns <c>true</c> once the coroutine has finished (success or error).</summary>
        private bool HasResult(int id) => KitsuneHasResult(id, out _);

        /// <summary>Returns the error string for a finished coroutine, or <c>null</c> if none.</summary>
        private string? GetError(int id)
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
        private LuaValue GetResultVariable(int id) => NativePtrToLuaValue(KitsuneGetResult(id), this);

        private void Dispose(bool disposing)
        {
            if (Interlocked.Exchange(ref _disposed, 1) == 0)
            {
                if (Interlocked.Decrement(ref _refCount) == 0)
                {
                    LeakedAllocations = (ulong)KitsuneCleanup();
                    while (GlobalCFunctionHandles.TryTake(out var h))
                    {
                        h.Free();
                    }
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
