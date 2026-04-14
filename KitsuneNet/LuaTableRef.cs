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
        /// Calls a named metamethod directly from the table's metatable:
        /// <c>getmetatable(table).__name(table, args...)</c>.
        /// Returns <see cref="LuaValue.None"/> when the metamethod is absent or raises an error.
        /// Throws <see cref="ObjectDisposedException"/> when disposed.
        /// </summary>
        public LuaValue CallMetamethod(string metamethod, params LuaValue[]? args)
        {
            ObjectDisposedException.ThrowIf(_disposed != 0, this);
            return KitsuneEngine.CallMetamethod(_nativePtr, metamethod, args);
        }

        /// <summary>
        /// Looks up <paramref name="method"/> on the table via <c>__index</c> and calls it
        /// with the table as <c>self</c>: <c>table:method(args...)</c>.
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
        /// Gets <c>table[key]</c> in Lua, firing <c>__index</c> if present.
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
        /// Sets <c>table[key] = value</c> in Lua, firing <c>__newindex</c> if present.
        /// Returns <c>false</c> if the object is invalid or a metamethod raised an error.
        /// Throws <see cref="ObjectDisposedException"/> when disposed.
        /// </summary>
        public bool SetIndex(LuaValue key, LuaValue value)
        {
            ObjectDisposedException.ThrowIf(_disposed != 0, this);
            return KitsuneEngine.SetIndex(_nativePtr, key, value);
        }

        /// <summary>
        /// Returns the result of <c>#table</c>, firing <c>__len</c> if present.
        /// For plain tables without <c>__len</c>, returns the raw sequence length.
        /// Returns <see cref="LuaType.Error"/> if <c>__len</c> raised.
        /// Throws <see cref="ObjectDisposedException"/> when disposed.
        /// </summary>
        public LuaValue GetLength()
        {
            ObjectDisposedException.ThrowIf(_disposed != 0, this);
            return KitsuneEngine.GetLength(_nativePtr);
        }

        /// <summary>
        /// Lazily iterates the table's key-value pairs one entry at a time using raw table
        /// traversal (<c>lua_next</c>). Unlike <see cref="GetContents"/> which loads everything
        /// at once, each step fetches a single entry — suitable for large tables or external-pull
        /// use cases such as SQLite virtual tables.
        /// Does not trigger <c>__pairs</c> metamethods; iteration order matches Lua's raw <c>next</c>.
        /// If the table is modified during iteration, behavior mirrors Lua's own <c>next(t, k)</c>.
        /// Disposing the enumerator early (e.g. <c>break</c>) correctly releases the cursor.
        /// Throws <see cref="ObjectDisposedException"/> when disposed.
        /// </summary>
        public IEnumerable<KeyValuePair<LuaValue, LuaValue>> Pairs()
        {
            ObjectDisposedException.ThrowIf(_disposed != 0, this);
            IntPtr cursor = IntPtr.Zero;
            try
            {
                while (true)
                {
                    IntPtr step = KitsuneEngine.TableNext(_nativePtr, cursor);
                    cursor = IntPtr.Zero;  // ownership transferred regardless of outcome

                    if (step == IntPtr.Zero)
                    {
                        yield break;  // OOM or invalid table
                    }

                    var pair = KitsuneEngine.ReadNextPair(step);
                    if (pair is null)
                    {
                        KitsuneEngine.ReleaseNativeVariable(step);  // TNONE (exhausted) or TERROR
                        yield break;
                    }

                    cursor = step;  // hold for next iteration; consumed by next TableNext call
                    yield return new KeyValuePair<LuaValue, LuaValue>(pair.Value.Key, pair.Value.Value);
                }
            }
            finally
            {
                if (cursor != IntPtr.Zero)
                {
                    KitsuneEngine.ReleaseNativeVariable(cursor);  // early break: free dangling cursor
                }
            }
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
