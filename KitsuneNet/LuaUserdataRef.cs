using System;
using System.Threading;

namespace KitsuneNet
{
    /// <summary>
    /// Holds a live reference to a Lua userdata returned by the engine.
    /// The reference keeps the userdata anchored in the Lua registry until disposed,
    /// preventing Lua GC from collecting it while C# still holds a reference.
    /// Obtain via <see cref="LuaValue.UserdataRef"/>; dispose when no longer needed.
    /// Passing the owning <see cref="LuaValue"/> as a script argument or to
    /// <see cref="KitsuneEngine.SetVariable"/> pushes the original Lua userdata object
    /// back into Lua — preserving identity so Lua's <c>==</c> operator works correctly.
    /// </summary>
    public sealed class LuaUserdataRef : IDisposable
    {
        // Heap-allocated KitsuneVariable* returned by KitsuneGetResult / KitsuneGetVariable.
        // Owned exclusively by this instance; KitsuneVariableFree is called on dispose.
        private IntPtr _nativePtr;
        private int _disposed;

        internal LuaUserdataRef(IntPtr nativePtr)
        {
            _nativePtr = nativePtr;
        }

        ~LuaUserdataRef() => Dispose();

        /// <summary>Raw pointer to the native <c>KitsuneVariable</c> struct. Zero when disposed.</summary>
        internal IntPtr NativePtr => _nativePtr;

        /// <summary>
        /// Calls a named metamethod directly from the userdata's metatable:
        /// <c>getmetatable(userdata).__name(userdata, args...)</c>.
        /// Returns <see cref="LuaValue.None"/> when the metamethod is absent or raises an error.
        /// Throws <see cref="ObjectDisposedException"/> when disposed.
        /// </summary>
        public LuaValue CallMetamethod(string metamethod, params LuaValue[]? args)
        {
            ObjectDisposedException.ThrowIf(_disposed != 0, this);
            return KitsuneEngine.CallMetamethod(_nativePtr, metamethod, args);
        }

        /// <summary>
        /// Looks up <paramref name="method"/> on the userdata via <c>__index</c> and calls it
        /// with the userdata as <c>self</c>: <c>userdata:method(args...)</c>.
        /// Both the lookup and the call run inside a single protected call, so any
        /// <c>__index</c> error, missing method, or runtime error returns <see cref="LuaValue.None"/>.
        /// Throws <see cref="ObjectDisposedException"/> when disposed.
        /// </summary>
        public LuaValue CallMethod(string method, params LuaValue[]? args)
        {
            ObjectDisposedException.ThrowIf(_disposed != 0, this);
            return KitsuneEngine.CallMethod(_nativePtr, method, args);
        }

        /// <summary>
        /// Gets <c>userdata[key]</c> in Lua, firing <c>__index</c> if present.
        /// Returns <see cref="LuaValue"/> with <see cref="LuaType.Nil"/> when the key is absent or nil,
        /// or <see cref="LuaType.Error"/> if <c>__index</c> raised.
        /// Throws <see cref="ObjectDisposedException"/> when disposed.
        /// </summary>
        public LuaValue GetIndex(LuaValue key)
        {
            ObjectDisposedException.ThrowIf(_disposed != 0, this);
            return KitsuneEngine.GetIndex(_nativePtr, key);
        }

        /// <summary>
        /// Sets <c>userdata[key] = value</c> in Lua, firing <c>__newindex</c> if present.
        /// Returns <c>false</c> if the object is invalid or a metamethod raised an error.
        /// Throws <see cref="ObjectDisposedException"/> when disposed.
        /// </summary>
        public bool SetIndex(LuaValue key, LuaValue value)
        {
            ObjectDisposedException.ThrowIf(_disposed != 0, this);
            return KitsuneEngine.SetIndex(_nativePtr, key, value);
        }

        /// <summary>
        /// Returns the result of <c>#userdata</c>, firing <c>__len</c> if present.
        /// Returns <see cref="LuaType.Error"/> if <c>__len</c> raised or is absent.
        /// Throws <see cref="ObjectDisposedException"/> when disposed.
        /// </summary>
        public LuaValue GetLength()
        {
            ObjectDisposedException.ThrowIf(_disposed != 0, this);
            return KitsuneEngine.GetLength(_nativePtr);
        }

        /// <summary>
        /// Releases the Lua registry reference. After disposal the userdata may be
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
