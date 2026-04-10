using System;
using System.Threading;

namespace KitsuneNet
{
    /// <summary>
    /// Holds a live reference to a Lua function returned by the engine.
    /// The reference keeps the function anchored in the Lua registry until disposed.
    /// Obtain via <see cref="LuaValue.FunctionRef"/>; dispose when no longer needed.
    /// Passing the owning <see cref="LuaValue"/> to <see cref="KitsuneEngine.SetVariable"/> or
    /// as a coroutine argument pushes the function back into Lua without consuming the ref.
    /// </summary>
    public sealed class LuaFunctionRef : IDisposable
    {
        private readonly WeakReference<KitsuneEngine>? _engine;

        // Heap-allocated KitsuneVariable* returned by KitsuneGetResult / KitsuneGetVariable.
        // Owned exclusively by this instance; KitsuneVariableFree is called on dispose.
        private IntPtr _nativePtr;
        private int _disposed;

        internal LuaFunctionRef(IntPtr nativePtr, KitsuneEngine? engine = null)
        {
            _nativePtr = nativePtr;
            _engine = engine is not null ? new WeakReference<KitsuneEngine>(engine) : null;
        }

        ~LuaFunctionRef() => Dispose();

        /// <summary>Raw pointer to the native <c>KitsuneVariable</c> struct. Zero when disposed.</summary>
        internal IntPtr NativePtr => _nativePtr;

        /// <summary>Calls this Lua function synchronously and returns its result.
        /// Returns <see cref="LuaValue.None"/> when the function raises a Lua runtime error;
        /// use <see cref="InvokeAsync"/> when error details are needed.
        /// Throws <see cref="ObjectDisposedException"/> when the ref or its engine has been disposed.
        /// Throws <see cref="LuaException"/> if the native engine rejects the call
        /// (e.g. re-entrant invocation — same as <see cref="KitsuneEngine.RunVariable"/>).</summary>
        public LuaValue Invoke(params LuaValue[]? args)
        {
            ObjectDisposedException.ThrowIf(_disposed != 0, this);
            if (_engine is null || !_engine.TryGetTarget(out var engine))
            {
                throw new ObjectDisposedException(nameof(KitsuneEngine));
            }

            return engine.RunVariable(new LuaValue { Type = LuaType.Function, FunctionRef = this }, args);
        }

        /// <summary>Calls this Lua function as a coroutine and asynchronously waits for it to complete.
        /// Throws <see cref="ObjectDisposedException"/> when the ref or its engine has been disposed.
        /// Throws <see cref="LuaException"/> when the function raises a Lua error.</summary>
        public Task<LuaValue> InvokeAsync(CancellationToken cancellationToken = default, params LuaValue[]? args)
        {
            ObjectDisposedException.ThrowIf(_disposed != 0, this);
            if (_engine is null || !_engine.TryGetTarget(out var engine))
            {
                throw new ObjectDisposedException(nameof(KitsuneEngine));
            }

            return engine.ExecuteVariableAsync(new LuaValue { Type = LuaType.Function, FunctionRef = this }, cancellationToken, args);
        }

        /// <summary>
        /// Releases the Lua registry reference.  After disposal the function may be
        /// garbage-collected by Lua if no other references exist.
        /// </summary>
        public void Dispose()
        {
            if (Interlocked.Exchange(ref _disposed, 1) == 0)
            {
                IntPtr ptr = Interlocked.Exchange(ref _nativePtr, IntPtr.Zero);
                if (ptr != IntPtr.Zero)
                {
                    KitsuneEngine.ReleaseNativeVariable(ptr);
                }
            }

            GC.SuppressFinalize(this);
        }
    }
}
