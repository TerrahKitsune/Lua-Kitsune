namespace KitsuneNet
{
    /// <summary>
    /// Owns a heap-allocated native <c>KitsuneVariable*</c> holding a captured Lua function.
    /// Obtain via <see cref="KitsuneEngine.GetResultFunction"/> or <see cref="KitsuneEngine.GetFunction"/>.
    /// Pass to <see cref="KitsuneEngine.ExecuteVariableBlocking"/> or
    /// <see cref="KitsuneEngine.ExecuteVariableAsync"/> to invoke the function.
    /// <para>
    /// The same handle may be called any number of times; the Lua registry reference is only
    /// released when <see cref="Dispose"/> is called (or the finalizer runs).
    /// </para>
    /// </summary>
    public sealed class LuaVariableHandle : IDisposable
    {
        private readonly Action<IntPtr> _free;
        private IntPtr _ptr;

        internal LuaVariableHandle(IntPtr ptr, Action<IntPtr> free)
        {
            _ptr = ptr;
            _free = free;
        }

        ~LuaVariableHandle() => Dispose();

        /// <summary><c>true</c> while the handle is alive; <c>false</c> after <see cref="Dispose"/>.</summary>
        public bool IsValid => _ptr != IntPtr.Zero;

        internal IntPtr NativePtr => _ptr;

        /// <summary>Releases the Lua registry reference and frees the native variable struct.</summary>
        public void Dispose()
        {
            IntPtr ptr = Interlocked.Exchange(ref _ptr, IntPtr.Zero);
            if (ptr != IntPtr.Zero)
            {
                _free(ptr);
            }

            GC.SuppressFinalize(this);
        }
    }
}
