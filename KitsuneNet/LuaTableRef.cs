using System;
using System.Collections.Generic;
using System.Threading;

namespace KitsuneNet
{
    /// <summary>
    /// Holds a live reference to a Lua table returned by the engine.
    /// The reference keeps the table anchored in the Lua registry until disposed.
    /// Obtain via <see cref="LuaValue.TableRef"/>; dispose when no longer needed.
    /// Passing the owning <see cref="LuaValue"/> to <see cref="KitsuneEngine.SetVariable"/> or
    /// as a coroutine argument pushes the same Lua table back into Lua without copying it.
    /// </summary>
    public sealed class LuaTableRef : IDisposable
    {
        // Heap-allocated KitsuneVariable* returned by KitsuneGetResult / KitsuneGetVariable.
        // Owned exclusively by this instance; KitsuneVariableFree is called on dispose.
        private IntPtr _nativePtr;
        private int _disposed;

        internal LuaTableRef(IntPtr nativePtr)
        {
            _nativePtr = nativePtr;
        }

        ~LuaTableRef() => Dispose();

        /// <summary>Raw pointer to the native <c>KitsuneVariable</c> struct. Zero when disposed.</summary>
        internal IntPtr NativePtr => _nativePtr;

        /// <summary>Snapshots the current contents of the Lua table into a managed list.
        /// The returned <see cref="LuaValue"/> has type <see cref="LuaType.TableContents"/>.
        /// Throws <see cref="ObjectDisposedException"/> when disposed.</summary>
        public IReadOnlyList<KeyValuePair<LuaValue, LuaValue>> GetContents()
        {
            ObjectDisposedException.ThrowIf(_disposed != 0, this);
            return KitsuneEngine.TableRefGetContents(_nativePtr);
        }

        /// <summary>Replaces all entries in the live Lua table with the provided snapshot.
        /// All existing keys are removed first; then each key-value pair in <paramref name="contents"/>
        /// is written in. Integer keys restore the array part automatically.
        /// Returns <c>false</c> if the native call fails.
        /// Throws <see cref="ObjectDisposedException"/> when disposed.</summary>
        public bool SetContents(IReadOnlyList<KeyValuePair<LuaValue, LuaValue>> contents)
        {
            ObjectDisposedException.ThrowIf(_disposed != 0, this);
            return KitsuneEngine.TableRefSetContents(_nativePtr, contents);
        }

        /// <summary>
        /// Releases the Lua registry reference.  After disposal the table may be
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
