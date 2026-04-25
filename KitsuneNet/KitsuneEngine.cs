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

        // Tracks the number of live KitsuneEngine instances.  KitsuneCleanup is
        // only called when the last instance is disposed; calling it earlier would
        // null g_state and break any concurrently running scripts (e.g. the stress
        // test runs a producer and a consumer as two independent engine instances).
        private static long _refCount;

        // Set to true on the scheduler thread while a LuaFunctionTrampoline call is executing.
        // Used to detect and reject recursive Execute* / Run* calls from within a registered function.
        [ThreadStatic]
        private static bool inLuaCallback;

        private int _disposed;  // 0 = not disposed; 1 = disposed

        public KitsuneEngine()
        {
            // KitsuneInit now returns true only when it creates a new state (we own it)
            // and false when the engine is already initialised by another caller. Call it
            // only on the first instance (_refCount 0 ? 1); subsequent instances share the
            // existing state and must not attempt to re-initialise or re-claim ownership.
            if (Interlocked.Increment(ref _refCount) == 1)
            {
                if (!KitsuneInit(IntPtr.Zero))
                {
                    Interlocked.Decrement(ref _refCount);
                    throw new InvalidOperationException("KitsuneInit failed");
                }
            }
        }

        ~KitsuneEngine() => Dispose(false);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate void GetAllCallback(IntPtr key, IntPtr value, IntPtr userdata);

        /// <summary>Returns <c>true</c> if any coroutine is currently running or yielded.</summary>
        public bool IsRunning => KitsuneIsRunning();

        /// <summary>Returns the ID of the first coroutine that is still running, or 0 if none are active.</summary>
        public int RunningCoroutineId => KitsuneGetRunningId();

        public static long GetReferences() => Interlocked.Read(ref _refCount);

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
        /// Un-pauses a coroutine that was suspended by <c>Pause()</c>.
        /// Returns <c>true</c> if the coroutine was found in a paused state and successfully resumed.
        /// Returns <c>false</c> if the id was not found, not paused, or already running. Thread-safe.
        /// </summary>
        public bool Resume(int id) => KitsuneResume(id);

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

        public LuaType GetVariableType(string name)
        {
            var v = GetVariable(name);
            v.FunctionRef?.Dispose();
            v.ThreadRef?.Dispose();
            v.TableRef?.Dispose();
            v.UserdataRef?.Dispose();
            return v.Type;
        }

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
                    NativeVariableToLuaValue(k, allowTableSnapshot: false),
                    NativeVariableToLuaValue(v, allowTableSnapshot: false)));
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

            var handle = GCHandle.Alloc(func);
            KitsuneRegisterFunction(name, GetTrampolinePtr(), GCHandle.ToIntPtr(handle), GetFinalizerPtr());
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

            // Do NOT add to GlobalHandles — userdata instance pins are owned exclusively
            // by the Lua __gc metamethod injected in RegisterUserdataCore.  Adding them
            // here would cause premature double-free when a concurrent engine's Dispose
            // drains GlobalHandles while another engine's Lua state still holds the pin.
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

        internal static IReadOnlyList<KeyValuePair<LuaValue, LuaValue>> TableRefGetContents(IntPtr tableVarPtr)
        {
            IntPtr resultPtr = KitsuneGetTableContents(tableVarPtr);
            if (resultPtr == IntPtr.Zero)
            {
                return Array.Empty<KeyValuePair<LuaValue, LuaValue>>();
            }

            KitsuneVariable nv;
            unsafe
            {
                nv = Unsafe.ReadUnaligned<KitsuneVariable>((void*)resultPtr);
            }
            var snapshotValue = ReadNativeTable(nv.Data);
            KitsuneVariableFree(resultPtr);
            return snapshotValue.Table ?? Array.Empty<KeyValuePair<LuaValue, LuaValue>>();
        }

        internal static JsonNode? TableRefGetContentsAsJson(IntPtr tableVarPtr)
        {
            IntPtr resultPtr = KitsuneGetTableContentsAsJson(tableVarPtr);
            if (resultPtr == IntPtr.Zero)
            {
                return null;
            }

            return NativePtrToLuaValue(resultPtr).AsJsonNode();
        }

        internal static bool TableRefSetContents(IntPtr tableVarPtr, IReadOnlyList<KeyValuePair<LuaValue, LuaValue>> contents)
        {
            List<IntPtr>? ptrs = null;
            IntPtr cvPtr = IntPtr.Zero;
            try
            {
                var cv = new KitsuneVariable { Type = (int)LuaType.TableContents };
                if (contents.Count > 0)
                {
                    cv.Data = BuildNativeTable(contents, ref ptrs);
                    cv.Length = (nuint)contents.Count;
                }
                cvPtr = Marshal.AllocHGlobal(Marshal.SizeOf<KitsuneVariable>());
                Marshal.StructureToPtr(cv, cvPtr, false);
                return KitsuneSetTableContents(tableVarPtr, cvPtr);
            }
            finally
            {
                if (cvPtr != IntPtr.Zero)
                {
                    Marshal.FreeHGlobal(cvPtr);
                }

                FreeNativeArgs(ptrs);
            }
        }

        // Calls a named metamethod from obj's metatable: getmetatable(obj).__name(obj, args...).
        // Returns LuaValue.None when the metamethod is absent or raises.
        internal static LuaValue CallMetamethod(IntPtr objVarPtr, string metamethod, LuaValue[]? args)
        {
            var (native, ptrs) = BuildNativeArgs(args);
            try
            {
                IntPtr resultPtr = KitsuneCallMetamethod(objVarPtr, metamethod, native?.Length ?? 0, native);
                return resultPtr == IntPtr.Zero ? LuaValue.None : NativePtrToLuaValue(resultPtr);
            }
            finally
            {
                FreeNativeArgs(ptrs);
            }
        }

        // Looks up method on obj via __index and calls it with obj as self: obj:method(args...).
        // Returns LuaValue.None when the method is absent, not callable, or raises.
        internal static LuaValue CallMethod(IntPtr objVarPtr, string method, LuaValue[]? args)
        {
            var (native, ptrs) = BuildNativeArgs(args);
            try
            {
                IntPtr resultPtr = KitsuneCallMethod(objVarPtr, method, native?.Length ?? 0, native);
                return resultPtr == IntPtr.Zero ? LuaValue.None : NativePtrToLuaValue(resultPtr);
            }
            finally
            {
                FreeNativeArgs(ptrs);
            }
        }

        // Gets obj[key] via __index. Returns LuaValue.None on OOM/invalid obj; LuaType.Nil for absent/nil key.
        internal static LuaValue GetIndex(IntPtr objVarPtr, LuaValue key)
        {
            var (native, ptrs) = BuildNativeArgs([key]);
            try
            {
                IntPtr resultPtr = KitsuneGetIndex(objVarPtr, ref native![0]);
                return resultPtr == IntPtr.Zero ? LuaValue.None : NativePtrToLuaValue(resultPtr);
            }
            finally
            {
                FreeNativeArgs(ptrs);
            }
        }

        // Sets obj[key] = value via __newindex. Returns false on error.
        internal static bool SetIndex(IntPtr objVarPtr, LuaValue key, LuaValue value)
        {
            var (native, ptrs) = BuildNativeArgs([key, value]);
            try
            {
                return KitsuneSetIndex(objVarPtr, ref native![0], ref native[1]);
            }
            finally
            {
                FreeNativeArgs(ptrs);
            }
        }

        // Returns #obj via __len. Returns LuaValue.None on OOM/invalid obj.
        internal static LuaValue GetLength(IntPtr objVarPtr)
        {
            IntPtr resultPtr = KitsuneGetLength(objVarPtr);
            return resultPtr == IntPtr.Zero ? LuaValue.None : NativePtrToLuaValue(resultPtr);
        }

        // Advances a raw table iterator one step. key is consumed (freed) when it is a
        // KITSUNE_TTABLECONTENTS result from a prior TableNext call; pass IntPtr.Zero to start.
        // Returns the new state pointer (TTABLECONTENTS, TNONE when exhausted, or TERROR).
        // The caller owns the returned pointer and must pass it back or free it.
        internal static IntPtr TableNext(IntPtr tableVarPtr, IntPtr key) =>
            KitsuneNext(tableVarPtr, key);

        // Reads the single key-value pair from a KITSUNE_TTABLECONTENTS step pointer without
        // consuming it. All values are fully materialized (strings copied, tables snapshotted)
        // before returning, so the pair is safe to use after the cursor is advanced.
        // Returns null if the pointer is not a valid TTABLECONTENTS entry.
        internal static (LuaValue Key, LuaValue Value)? ReadNextPair(IntPtr stepPtr)
        {
            if (stepPtr == IntPtr.Zero)
            {
                return null;
            }

            KitsuneVariable nv;
            unsafe
            {
                nv = Unsafe.ReadUnaligned<KitsuneVariable>((void*)stepPtr);
            }

            if ((LuaType)nv.Type != LuaType.TableContents || nv.Data == IntPtr.Zero)
            {
                return null;
            }

            var node = Marshal.PtrToStructure<NativeKVNode>(nv.Data);
            return (NativeVariableToLuaValue(node.Key), NativeVariableToLuaValue(node.Value));
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
        private static extern bool KitsuneInit(IntPtr kitsuneMemoryAllocator);

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
        [return: MarshalAs(UnmanagedType.I1)]
        private static extern bool KitsuneResume(int id);

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

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr KitsuneGetTableContents(IntPtr tableVarPtr);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        [return: MarshalAs(UnmanagedType.I1)]
        private static extern bool KitsuneSetTableContents(IntPtr tableVarPtr, IntPtr contentsVarPtr);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr KitsuneGetIndex(IntPtr objVarPtr, ref KitsuneVariable key);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        [return: MarshalAs(UnmanagedType.I1)]
        private static extern bool KitsuneSetIndex(IntPtr objVarPtr, ref KitsuneVariable key, ref KitsuneVariable value);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr KitsuneGetLength(IntPtr objVarPtr);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr KitsuneGetTableContentsAsJson(IntPtr tableVarPtr);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr KitsuneNext(IntPtr tableVarPtr, IntPtr key);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr KitsuneCallMetamethod(IntPtr objVarPtr, [MarshalAs(UnmanagedType.LPUTF8Str)] string metamethod, int argc, KitsuneVariable[]? argv);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr KitsuneCallMethod(IntPtr objVarPtr, [MarshalAs(UnmanagedType.LPUTF8Str)] string method, int argc, KitsuneVariable[]? argv);

        // func is a delegate* unmanaged[Cdecl] cast to nint; userdata is a GCHandle address.
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        private static extern void KitsuneRegisterFunction([MarshalAs(UnmanagedType.LPUTF8Str)] string name, nint func, nint userdata, nint finalizer);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        [return: MarshalAs(UnmanagedType.I1)]
        private static extern bool KitsuneRegisterUserdata([MarshalAs(UnmanagedType.LPUTF8Str)] string name, IntPtr registration);

        #endregion

        private static unsafe nint GetTrampolinePtr() =>
            (nint)(delegate* unmanaged[Cdecl]<int, KitsuneVariable*, nint, void*, int>)&LuaFunctionTrampoline;

        private static unsafe nint GetFinalizerPtr() =>
            (nint)(delegate* unmanaged[Cdecl]<void*, void>)&LuaDelegateFinalizer;

        // Called by the native KitsuneGCHookUD __gc when Lua collects a closure that wraps
        // a C# delegate.  Frees the GCHandle so the delegate object can be collected.
        [UnmanagedCallersOnly(CallConvs = new[] { typeof(System.Runtime.CompilerServices.CallConvCdecl) })]
        private static unsafe void LuaDelegateFinalizer(void* userdata)
        {
            var handle = GCHandle.FromIntPtr((nint)userdata);
            if (handle.IsAllocated)
            {
                handle.Free();
            }
        }

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

        private static LuaValue NativeMarshalDateTime(IntPtr ptr)
        {
            var s = Marshal.PtrToStructure<NativeDateTime>(ptr);
            return LuaValue.FromDateTime(new DateTimeOffset(s.Ticks, TimeSpan.FromMinutes(s.OffsetMinutes)));
        }

        private static LuaValue NativeMarshalDecimal(IntPtr ptr)
        {
            var s = Marshal.PtrToStructure<NativeDecimal>(ptr);
            int lo32 = (int)(s.Lo & 0xFFFFFFFF);
            int mid32 = (int)(s.Lo >> 32);
            int hi32 = (int)(s.Hi & 0xFFFFFFFF);
            byte scale = s.Scale < 0 ? (byte)0 : s.Scale > 28 ? (byte)28 : (byte)s.Scale;
            return LuaValue.FromDecimal(new decimal(lo32, mid32, hi32, s.Negative != 0, scale));
        }

        private static unsafe LuaValue NativeMarshalIdentifier(IntPtr ptr)
        {
            var s = (NativeIdentifier*)ptr;
            var blob = new byte[17];
            blob[0] = s->Type;
            for (int i = 0; i < 16; i++)
            {
                blob[i + 1] = s->Bytes[i];
            }

            return new LuaValue { Type = LuaType.Identifier, Bytes = blob };
        }

        private static NativeDecimal DecimalToNative(decimal v)
        {
            int[] bits = decimal.GetBits(v);
            return new NativeDecimal
            {
                Lo = (uint)bits[0] | ((ulong)(uint)bits[1] << 32),
                Hi = (uint)bits[2],
                Scale = (short)((bits[3] >> 16) & 0xFF),
                Negative = (bits[3] & unchecked((int)0x80000000)) != 0 ? (byte)1 : (byte)0,
            };
        }

        private static unsafe void WriteNativeIdentifier(IntPtr ptr, LuaValue v)
        {
            var s = (NativeIdentifier*)ptr;
            s->Type = 0;
            for (int i = 0; i < 16; i++)
            {
                s->Bytes[i] = 0;
            }

            if (v.Bytes is { Length: >= 17 })
            {
                s->Type = v.Bytes[0];
                for (int i = 0; i < 16; i++)
                {
                    s->Bytes[i] = v.Bytes[i + 1];
                }
            }
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

            // Function / Thread / Table / Userdata: transfer the native pointer to a ref object;
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

            if (t == LuaType.Table)
            {
                return new LuaValue { Type = LuaType.Table, TableRef = new LuaTableRef(ptr) };
            }

            if (t == LuaType.Userdata)
            {
                // Read name/GCHandle now (while the KitsuneUserData* is still alive),
                // then transfer ownership to LuaUserdataRef so the registry ref persists
                // until the caller disposes the value.
                LuaValue ud = nv.Data != IntPtr.Zero
                    ? NativeUnmarshalUserdata(nv.Data, nv.Length)
                    : new LuaValue { Type = LuaType.Userdata };
                return ud with { UserdataRef = new LuaUserdataRef(ptr) };
            }

            LuaValue result = t switch
            {
                LuaType.Number => LuaValue.FromNumber(nv.Number),
                LuaType.Integer => LuaValue.FromInt64(nv.Integer),
                LuaType.UInt => LuaValue.FromUInt64((ulong)nv.Integer),
                LuaType.TimeSpan when nv.Data != IntPtr.Zero => LuaValue.FromTimeSpan(System.TimeSpan.FromTicks(Marshal.PtrToStructure<NativeTimeSpan>(nv.Data).Ticks)),
                LuaType.DateTime when nv.Data != IntPtr.Zero => NativeMarshalDateTime(nv.Data),
                LuaType.Decimal when nv.Data != IntPtr.Zero => NativeMarshalDecimal(nv.Data),
                LuaType.Identifier when nv.Data != IntPtr.Zero => NativeMarshalIdentifier(nv.Data),
                LuaType.Boolean => LuaValue.FromBool(nv.BoolByte != 0),
                LuaType.String when nv.Data != IntPtr.Zero => NativeCopyBytes(nv.Data, nv.Length),
                LuaType.Char16 when nv.Data != IntPtr.Zero => NativeCopyChar16(nv.Data, nv.Length),
                LuaType.Json when nv.Data != IntPtr.Zero && nv.Length > 0 => NativeParseJson(nv.Data, nv.Length),
                LuaType.TableContents => ReadNativeTable(nv.Data),  // snapshot from KitsuneGetTableContents
                LuaType.Error when nv.Data != IntPtr.Zero => NativeCopyBytes(nv.Data, nv.Length) with { Type = LuaType.Error },
                LuaType.None => LuaValue.None,
                _ => new LuaValue { Type = t },  // Nil/LightUserdata/etc.
            };
            KitsuneVariableFree(ptr);
            return result;
        }

        // Converts a by-value KitsuneVariable (already marshaled into managed memory) to a LuaValue.
        // Does NOT free any native memory — use this for embedded struct members, not heap pointers.
        // Function/Thread values are returned as opaque (Type only, no ref) since the variable
        // is embedded inside a larger allocation (table node or callback args array).
        // When allowTableSnapshot is true (default), table values with a live ref are snapshotted inline
        // via KitsuneGetTableContents.  Pass false from GetAll callbacks, which already hold
        // AcquireLuaAccess — calling KitsuneGetTableContents there would deadlock.
        private static LuaValue NativeVariableToLuaValue(KitsuneVariable nv, bool allowTableSnapshot = true)
        {
            LuaType t = (LuaType)nv.Type;
            if (allowTableSnapshot && t == LuaType.Table && nv.Ref > 0)
            {
                // The embedded variable holds a live luaL_ref (valid for the duration of the
                // enclosing native call). Snapshot it immediately so the returned LuaValue has
                // .Table populated — restoring the behaviour callers expect for callback args.
                unsafe
                {
                    KitsuneVariable local = nv;
                    IntPtr contentsPtr = KitsuneGetTableContents((IntPtr)(&local));
                    if (contentsPtr == IntPtr.Zero)
                    {
                        return new LuaValue { Type = LuaType.Table };
                    }

                    KitsuneVariable cnv = Unsafe.ReadUnaligned<KitsuneVariable>((void*)contentsPtr);
                    LuaValue snapshot = ReadNativeTable(cnv.Data);
                    KitsuneVariableFree(contentsPtr);
                    return snapshot;  // Type = LuaType.Table, Table = entries
                }
            }
            return t switch
            {
                LuaType.Number => LuaValue.FromNumber(nv.Number),
                LuaType.Integer => LuaValue.FromInt64(nv.Integer),
                LuaType.UInt => LuaValue.FromUInt64((ulong)nv.Integer),
                LuaType.TimeSpan when nv.Data != IntPtr.Zero => LuaValue.FromTimeSpan(System.TimeSpan.FromTicks(Marshal.PtrToStructure<NativeTimeSpan>(nv.Data).Ticks)),
                LuaType.DateTime when nv.Data != IntPtr.Zero => NativeMarshalDateTime(nv.Data),
                LuaType.Decimal when nv.Data != IntPtr.Zero => NativeMarshalDecimal(nv.Data),
                LuaType.Identifier when nv.Data != IntPtr.Zero => NativeMarshalIdentifier(nv.Data),
                LuaType.Boolean => LuaValue.FromBool(nv.BoolByte != 0),
                LuaType.String when nv.Data != IntPtr.Zero => NativeCopyBytes(nv.Data, nv.Length),
                LuaType.Char16 when nv.Data != IntPtr.Zero => NativeCopyChar16(nv.Data, nv.Length),
                LuaType.Userdata when nv.Data != IntPtr.Zero => NativeUnmarshalUserdata(nv.Data, nv.Length),
                LuaType.Json when nv.Data != IntPtr.Zero && nv.Length > 0 => NativeParseJson(nv.Data, nv.Length),
                LuaType.TableContents => ReadNativeTable(nv.Data),  // snapshot linked list inside a node
                LuaType.None => LuaValue.None,
                _ => new LuaValue { Type = t },  // Nil/Table(no-ref or no-snapshot)/Function/Thread/Userdata/LightUserdata
            };
        }

        // Walks a native KitsuneKeyValuePairVariableNode linked list and converts it to a LuaValue table.
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
                case LuaType.UInt:
                    nv.Integer = v.Int64;  // same bit pattern; native reads as uint64
                    break;
                case LuaType.TimeSpan:
                    {
                        var s = new NativeTimeSpan { Ticks = v.AsTimeSpan.Ticks };
                        IntPtr p = Marshal.AllocHGlobal(Marshal.SizeOf<NativeTimeSpan>());
                        Marshal.StructureToPtr(s, p, false);
                        ptrs ??= new List<IntPtr>();
                        ptrs.Add(p);
                        nv.Data = p;
                        break;
                    }

                case LuaType.DateTime:
                    {
                        var dto = v.AsDateTimeOffset;
                        var s = new NativeDateTime { Ticks = dto.Ticks, OffsetMinutes = (short)dto.Offset.TotalMinutes };
                        IntPtr p = Marshal.AllocHGlobal(Marshal.SizeOf<NativeDateTime>());
                        Marshal.StructureToPtr(s, p, false);
                        ptrs ??= new List<IntPtr>();
                        ptrs.Add(p);
                        nv.Data = p;
                        break;
                    }

                case LuaType.Decimal:
                    {
                        var s = DecimalToNative(v.AsDecimal);
                        IntPtr p = Marshal.AllocHGlobal(Marshal.SizeOf<NativeDecimal>());
                        Marshal.StructureToPtr(s, p, false);
                        ptrs ??= new List<IntPtr>();
                        ptrs.Add(p);
                        nv.Data = p;
                        break;
                    }

                case LuaType.Identifier:
                    {
                        IntPtr p = Marshal.AllocHGlobal(Marshal.SizeOf<NativeIdentifier>());
                        WriteNativeIdentifier(p, v);
                        ptrs ??= new List<IntPtr>();
                        ptrs.Add(p);
                        nv.Data = p;
                        break;
                    }
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
                case LuaType.Table when v.TableRef is { NativePtr: 0 }:
                    throw new ObjectDisposedException(nameof(LuaTableRef),
                        "Cannot marshal a disposed LuaTableRef across the native bridge.");
                case LuaType.Table when v.TableRef is { } tr && tr.NativePtr != IntPtr.Zero:
                    {
                        // Copy the registry ref integer from the native KitsuneVariable.
                        // PushKitsuneVariable uses lua_rawgeti with this ref to push the live table.
                        var tnv = Marshal.PtrToStructure<KitsuneVariable>(tr.NativePtr);
                        nv.Ref = tnv.Ref;
                        break;
                    }
                case LuaType.Table when v.Table is not null:
                    // Backward-compat: snapshot table (created via LuaValue.FromTable).
                    // Send as KITSUNE_TTABLECONTENTS so PushKitsuneVariable creates a new table.
                    nv.Type = (int)LuaType.TableContents;
                    nv.Data = BuildNativeTable(v.Table, ref ptrs);
                    nv.Length = (nuint)v.Table.Count;
                    break;
                case LuaType.TableContents when v.Table is not null:
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
                case LuaType.CFunction when v.CFunctionValue is LuaFunction luaFunc:
                    {
                        // Allocate a GCHandle to keep the delegate alive while Lua may call the closure.
                        // The handle is freed by the KitsuneGCHook __gc when Lua collects the closure.
                        var handle = GCHandle.Alloc(luaFunc);

                        // Allocate a kitsune_CFunctionData { func, userdata, finalizer } on the unmanaged heap.
                        // PushKitsuneVariable copies the three pointer values into Lua upvalue slots, so
                        // this struct only needs to survive until the native call returns.
                        IntPtr structPtr = Marshal.AllocHGlobal(IntPtr.Size * 3);
                        Marshal.WriteIntPtr(structPtr, 0, GetTrampolinePtr());
                        Marshal.WriteIntPtr(structPtr, IntPtr.Size, GCHandle.ToIntPtr(handle));
                        Marshal.WriteIntPtr(structPtr, IntPtr.Size * 2, GetFinalizerPtr());
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

                        // kitsune_CFunctionData { func, userdata, finalizer } for step.
                        // first and next share the same struct because IEnumerator.MoveNext() is stateful.
                        // finalizer is NULL — the iterator manages its own lifetime via finalizeFunc/__gc.
                        IntPtr stepCFD = Marshal.AllocHGlobal(IntPtr.Size * 3);
                        Marshal.WriteIntPtr(stepCFD, 0, GetTrampolinePtr());
                        Marshal.WriteIntPtr(stepCFD, IntPtr.Size, GCHandle.ToIntPtr(iterState.StepHandle));
                        Marshal.WriteIntPtr(stepCFD, IntPtr.Size * 2, IntPtr.Zero);
                        ptrs ??= new List<IntPtr>();
                        ptrs.Add(stepCFD);

                        // kitsune_CFunctionData { func, userdata, finalizer } for finalized.
                        // finalizer is NULL — the finalizeFunc itself does cleanup, no extra hook needed.
                        IntPtr finCFD = Marshal.AllocHGlobal(IntPtr.Size * 3);
                        Marshal.WriteIntPtr(finCFD, 0, GetTrampolinePtr());
                        Marshal.WriteIntPtr(finCFD, IntPtr.Size, GCHandle.ToIntPtr(iterState.FinalizeHandle));
                        Marshal.WriteIntPtr(finCFD, IntPtr.Size * 2, IntPtr.Zero);
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
                case LuaType.Userdata when v.UserdataRef is { } ur && ur.NativePtr != IntPtr.Zero:
                    {
                        // Copy the KitsuneUserData* pointer directly from the held KitsuneVariable.
                        // PushKitsuneVariable reads ud->ref and calls lua_rawgeti to push the
                        // original Lua userdata — preserving identity.
                        // The pointer is owned by LuaUserdataRef; do NOT add it to ptrs.
                        var tnv = Marshal.PtrToStructure<KitsuneVariable>(ur.NativePtr);
                        nv.Data = tnv.Data;
                        nv.Length = tnv.Length;
                        break;
                    }
                case LuaType.Userdata when v.Bytes is not null && v.UserdataGCHandlePtr != 0:
                    {
                        // Allocate a KitsuneUserData { char* name, int ref, (pad), void* userdata } on the unmanaged heap.
                        // PushKitsuneVariable reads it; ref = LUA_NOREF signals the fallback new-wrapper path.
                        byte[] nameBytes = v.Bytes;
                        IntPtr namePtr = Marshal.AllocHGlobal(nameBytes.Length + 1);
                        Marshal.Copy(nameBytes, 0, namePtr, nameBytes.Length);
                        Marshal.WriteByte(namePtr, nameBytes.Length, 0);

                        var kud = new KitsuneUserDataNative { Name = namePtr, Ref = -2, Userdata = v.UserdataGCHandlePtr };
                        IntPtr structPtr = Marshal.AllocHGlobal(Marshal.SizeOf<KitsuneUserDataNative>());
                        Marshal.StructureToPtr(kud, structPtr, false);

                        ptrs ??= new List<IntPtr>();
                        ptrs.Add(namePtr);
                        ptrs.Add(structPtr);
                        nv.Data = structPtr;
                        nv.Length = (nuint)nameBytes.Length;
                        break;
                    }
            }
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
                IntPtr next = Marshal.ReadIntPtr(head, IntPtr.Size * 4);
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
                // GCHandles for LuaFunction delegates are freed by KitsuneGCHook __gc callbacks.
                FreeNamedFunctionList(funcHead);
                FreeNamedFunctionList(metaHead);
                if (reg != IntPtr.Zero)
                {
                    Marshal.FreeHGlobal(reg);
                }
            }
        }

        // Allocates a KitsuneNamedFunction { char* name, func, void* userdata, kitsune_Finalizer finalizer, Next* } node.
        // The LuaFunction delegate GCHandle is freed by the KitsuneGCHook __gc when Lua collects the closure.
        // The name string and node itself are freed by FreeNamedFunctionList once KitsuneRegisterUserdata has consumed them.
        private IntPtr AllocNamedFunction(string name, LuaFunction func, IntPtr next)
        {
            var handle = GCHandle.Alloc(func);

            byte[] nameBytes = Encoding.UTF8.GetBytes(name);
            IntPtr namePtr = Marshal.AllocHGlobal(nameBytes.Length + 1);
            Marshal.Copy(nameBytes, 0, namePtr, nameBytes.Length);
            Marshal.WriteByte(namePtr, nameBytes.Length, 0);

            // KitsuneNamedFunction { char* name, kitsune_CFunction func, void* userdata, kitsune_Finalizer finalizer, KitsuneNamedFunction* Next }
            // Each field is pointer-sized on x64 (40 bytes total).
            IntPtr node = Marshal.AllocHGlobal(IntPtr.Size * 5);
            Marshal.WriteIntPtr(node, 0, namePtr);
            Marshal.WriteIntPtr(node, IntPtr.Size, GetTrampolinePtr());
            Marshal.WriteIntPtr(node, IntPtr.Size * 2, GCHandle.ToIntPtr(handle));
            Marshal.WriteIntPtr(node, IntPtr.Size * 3, GetFinalizerPtr());
            Marshal.WriteIntPtr(node, IntPtr.Size * 4, next);
            return node;
        }

        private void Dispose(bool disposing)
        {
            if (Interlocked.Exchange(ref _disposed, 1) == 0)
            {
                if (Interlocked.Decrement(ref _refCount) == 0)
                {
                    // All LuaFunctionRef / LuaThreadRef instances must be disposed by the caller
                    // before this point.  Disposing them enqueues their KitsuneVariable* to
                    // g_pendingVariableChainHead; DrainPendingVariableChain inside KitsuneCleanup
                    // processes those frees while the Lua state is still live.
                    ulong sessionLeaks = (ulong)KitsuneCleanup();

                    if (disposing && sessionLeaks != 0)
                    {
                        throw new ApplicationException($"Native memory leak: {sessionLeaks} unfreed allocation(s)");
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
            public int Ref;

            [FieldOffset(16)]
            public IntPtr Data;

            [FieldOffset(16)]
            public double Number;

            [FieldOffset(16)]
            public long Integer;

            [FieldOffset(16)]
            public byte BoolByte;
        }

        // Mirrors KitsuneKeyValuePairVariableNode: Key(24) + Value(24) + Next ptr(8) = 56 bytes.
        [StructLayout(LayoutKind.Sequential)]
        private struct NativeKVNode
        {
            public KitsuneVariable Key;
            public KitsuneVariable Value;
            public IntPtr Next;
        }

        // Mirrors KitsuneUserData: { char* name, int ref, (4-byte pad), void* userdata } on x64.
        // nv.Data points to one of these for every LUA_TUSERDATA variable produced by the engine.
        [StructLayout(LayoutKind.Sequential)]
        private struct KitsuneUserDataNative
        {
            public IntPtr Name;     // heap-allocated UTF-8 name string
            public int Ref;         // luaL_ref registry ref; managed by the engine (LUA_NOREF = -2 when absent)

            // 4 bytes padding (implicit, to align Userdata to pointer boundary)
            public IntPtr Userdata; // GCHandle address for Kitsune-registered userdatas; IntPtr.Zero otherwise
        }

        // Mirrors KitsuneDateTime { int64_t ticks; int16_t offset_minutes; }
        [StructLayout(LayoutKind.Sequential)]
        private struct NativeDateTime
        {
            public long Ticks;
            public short OffsetMinutes;
        }

        // Mirrors KitsuneTimeSpan { int64_t ticks; }
        [StructLayout(LayoutKind.Sequential)]
        private struct NativeTimeSpan
        {
            public long Ticks;
        }

        // Mirrors KitsuneDecimal { uint64_t lo; uint64_t hi; int16_t scale; uint8_t negative; }
        [StructLayout(LayoutKind.Sequential)]
        private struct NativeDecimal
        {
            public ulong Lo;
            public ulong Hi;
            public short Scale;
            public byte Negative;
        }

        // Mirrors KitsuneIdentifier { uint8_t type; uint8_t bytes[16]; }
        [StructLayout(LayoutKind.Sequential)]
        private unsafe struct NativeIdentifier
        {
            public byte Type;
            public fixed byte Bytes[16];
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
