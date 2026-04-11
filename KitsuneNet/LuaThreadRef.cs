using System;
using System.Collections.Generic;
using System.Threading;

namespace KitsuneNet
{
    /// <summary>
    /// Holds a live reference to a Lua coroutine thread returned by the engine.
    /// The reference keeps the thread anchored in the Lua registry until disposed.
    /// Obtain via <see cref="LuaValue.ThreadRef"/>; dispose when no longer needed.
    /// Passing the owning <see cref="LuaValue"/> to <see cref="KitsuneEngine.SetVariable"/> or
    /// as a coroutine argument pushes the thread back into Lua without consuming the ref.
    /// </summary>
    public sealed class LuaThreadRef : IDisposable
    {
        // Heap-allocated KitsuneVariable* returned by KitsuneGetResult / KitsuneGetVariable.
        // Owned exclusively by this instance; KitsuneVariableFree is called on dispose.
        private IntPtr _nativePtr;
        private int _disposed;

        internal LuaThreadRef(IntPtr nativePtr)
        {
            _nativePtr = nativePtr;
        }

        ~LuaThreadRef() => Dispose();

        /// <summary>Raw pointer to the native <c>KitsuneVariable</c> struct. Zero when disposed.</summary>
        internal IntPtr NativePtr => _nativePtr;

        /// <summary>
        /// Synchronously steps the coroutine once, optionally passing <paramref name="args"/>
        /// as the result of the current <c>coroutine.yield()</c> (or as initial parameters on
        /// the first call). Returns the first value yielded or returned by this step.
        /// Returns <see cref="LuaValue.None"/> when the thread is dead.
        /// Returns a nil <see cref="LuaValue"/> when the thread yielded with no value.
        /// Throws <see cref="LuaException"/> if the coroutine raises a runtime error.
        /// </summary>
        public LuaValue Step(params LuaValue[]? args)
        {
            ObjectDisposedException.ThrowIf(_disposed != 0, this);
            return KitsuneEngine.StepThread(this, args);
        }

        /// <summary>
        /// Asynchronously steps the coroutine once, optionally passing <paramref name="args"/>
        /// as the result of the current <c>coroutine.yield()</c> (or as initial parameters on
        /// the first call). Returns the first value yielded or returned by this step.
        /// Returns <see cref="LuaValue.None"/> when the thread is dead.
        /// Returns a nil <see cref="LuaValue"/> when the thread yielded with no value.
        /// Throws <see cref="LuaException"/> if the coroutine raises a runtime error.
        /// </summary>
        public Task<LuaValue> StepAsync(CancellationToken cancellationToken = default, params LuaValue[]? args)
        {
            ObjectDisposedException.ThrowIf(_disposed != 0, this);
            return KitsuneEngine.StepThreadAsync(this, args, cancellationToken);
        }

        /// <summary>
        /// Synchronously iterates the coroutine, yielding each value it produces.
        /// Matches the pattern of <see cref="LuaFunctionRef.Invoke"/>.
        /// Throws <see cref="ObjectDisposedException"/> when the ref or its engine has been disposed.
        /// Throws <see cref="LuaException"/> when called from within a registered function callback,
        /// or if the coroutine raises a runtime error.
        /// </summary>
        public IEnumerable<LuaValue> Iterate(CancellationToken cancellationToken = default)
        {
            ObjectDisposedException.ThrowIf(_disposed != 0, this);
            return KitsuneEngine.IterateThread(this, cancellationToken);
        }

        /// <summary>
        /// Asynchronously iterates the coroutine, yielding each value it produces.
        /// Matches the pattern of <see cref="LuaFunctionRef.InvokeAsync"/>.
        /// Throws <see cref="ObjectDisposedException"/> when the ref or its engine has been disposed.
        /// Throws <see cref="LuaException"/> when called from within a registered function callback,
        /// or if the coroutine raises a runtime error.
        /// </summary>
        public IAsyncEnumerable<LuaValue> IterateAsync(CancellationToken cancellationToken = default)
        {
            ObjectDisposedException.ThrowIf(_disposed != 0, this);
            return KitsuneEngine.IterateThreadAsync(this, cancellationToken);
        }

        /// <summary>
        /// Releases the Lua registry reference. After disposal the thread may be
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
