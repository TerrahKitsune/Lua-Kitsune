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
        // Heap-allocated KitsuneVariable* returned by KitsuneGetResult / KitsuneGetVariable.
        // Owned exclusively by this instance; KitsuneVariableFree is called on dispose.
        private IntPtr _nativePtr;
        private int _disposed;

        internal LuaFunctionRef(IntPtr nativePtr)
        {
            _nativePtr = nativePtr;
        }

        /// <summary>Raw pointer to the native <c>KitsuneVariable</c> struct. Zero when disposed.</summary>
        internal IntPtr NativePtr => _nativePtr;

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

        ~LuaFunctionRef() => Dispose();
    }
}
