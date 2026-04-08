using System.Runtime.InteropServices;

namespace KitsuneNet
{
    /// <summary>
    /// A <see cref="System.IO.Stream"/> that directly addresses a native
    /// <c>SharedMemoryBlock</c> without copying.
    /// <para>
    /// <b>Inbound (Lua → C#):</b> created by <see cref="KitsuneEngine"/> when a Lua coroutine
    /// returns a stream result.  <see cref="Dispose"/> sets <see cref="FlagAccessorDisposed"/> on
    /// the block; the engine's ticker frees it once Lua's GC also sets <see cref="FlagOwnerDisposed"/>.
    /// </para>
    /// <para>
    /// <b>Managed (C# → Lua):</b> created via <see cref="KitsuneEngine.CreateStream"/>.
    /// Always read-write.  Pass to Lua via <see cref="KitsuneEngine.SetVariable"/> or as a
    /// coroutine argument.  C# may continue accessing the stream after the handoff for concurrent
    /// shared-memory use; call <see cref="Dispose"/> when done.  If never passed to Lua,
    /// <see cref="Dispose"/> frees the block on the next ticker cycle.
    /// </para>
    /// <para>
    /// All read and write operations set and clear <see cref="FlagLocked"/> on the block's
    /// <c>flags</c> byte around each access (skipped for read-only blocks).
    /// This is a cooperative advisory signal — see <see cref="FlagLocked"/> for the full caveat.
    /// </para>
    /// </summary>
    public sealed class LuaStream : System.IO.UnmanagedMemoryStream
    {
        // ── Flag constants (mirror KitsuneEngine.h KITSUNE_SHARED_MEMORY_FLAG_*) ──

        /// <summary>Cooperative advisory lock bit.  Set by an accessor (including this class)
        /// before each read or write, cleared immediately after.
        /// Other accessors should spin until this bit is clear before accessing the data.
        /// <para>⚠ Not a true mutex: the underlying byte RMW is non-atomic on x86-64
        /// (movzx / or / mov), so two concurrent writers can both believe they hold the lock.
        /// For hard mutual exclusion use a real synchronisation primitive alongside this signal.
        /// </para></summary>
        public const byte FlagLocked = 0x01;  // (1 << 0)

        /// <summary>Marks the block as read-only.  Write operations are rejected and
        /// <see cref="FlagLocked"/> is never set on reads.</summary>
        public const byte FlagReadOnly = 0x04;  // (1 << 2)

        /// <summary>Set by the engine's ticker once all Lua streams referencing this block
        /// have been GC'd.  Once set, the block's data must not be accessed.</summary>
        public const byte FlagOwnerDisposed = 0x10;  // (1 << 4)

        /// <summary>Cleared by the <see cref="LuaStream"/> constructor when C# takes ownership;
        /// set by <see cref="Dispose"/> when C# is done.  The ticker frees the block once both
        /// <see cref="FlagOwnerDisposed"/> and this flag are set.</summary>
        public const byte FlagAccessorDisposed = 0x20; // (1 << 5)

        /// <summary>Set by <c>lua_push_sharedmemory_stream</c> when any Lua stream is created
        /// from this block.  Used by <see cref="Dispose"/> to determine whether Lua's GC will
        /// eventually set <see cref="FlagOwnerDisposed"/>.</summary>
        public const byte FlagLuaReferenced = 0x40;  // (1 << 6)

        private IntPtr _blockPtr;
        private int _disposed;
        private bool _isManaged;  // true for CreateStream blocks until passed to Lua

        // Unified constructor.  Clears ACCESSOR_DISPOSED on the block, taking the accessor role.
        // managed=true for CreateStream blocks (always read-write).
        // managed=false for inbound blocks (access mode derived from READONLY flag).
        internal unsafe LuaStream(IntPtr blockPtr, long size, bool managed = false)
            : base()
        {
            _blockPtr = blockPtr;
            _isManaged = managed;
            byte flags = Marshal.ReadByte(blockPtr, 0);

            // Signal that C# is now the active accessor.
            Marshal.WriteByte(blockPtr, 0, (byte)(flags & ~FlagAccessorDisposed));
            bool readOnly = !managed && (flags & FlagReadOnly) != 0;
            Initialize(
                (byte*)((nint)blockPtr + 32),
                size,
                size,
                readOnly ? System.IO.FileAccess.Read : System.IO.FileAccess.ReadWrite);
        }

        // Finalizer: safety net in case Dispose was never called explicitly.
        // The block is still live (ACCESSOR_DISPOSED=0 prevents the ticker from freeing it),
        // so reading _blockPtr here is safe.
        ~LuaStream() => Dispose(false);

        /// <summary>
        /// Reads the <c>flags</c> byte of the underlying <c>SharedMemoryBlock</c> header.
        /// Check against <see cref="FlagLocked"/>, <see cref="FlagReadOnly"/>, etc.
        /// Returns 0 if the block pointer has been cleared (after <see cref="Dispose"/>).
        /// </summary>
        public byte Flags => _blockPtr != IntPtr.Zero ? Marshal.ReadByte(_blockPtr, 0) : (byte)0;

        // ── Read overrides ────────────────────────────────────────────────────────────────────

        /// <inheritdoc/>
        public override int Read(byte[] buffer, int offset, int count)
        {
            AcquireLock();
            try
            {
                return base.Read(buffer, offset, count);
            }
            finally
            {
                ReleaseLock();
            }
        }

        /// <inheritdoc/>
        public override int Read(Span<byte> destination)
        {
            AcquireLock();
            try
            {
                return base.Read(destination);
            }
            finally
            {
                ReleaseLock();
            }
        }

        /// <inheritdoc/>
        public override int ReadByte()
        {
            AcquireLock();
            try
            {
                return base.ReadByte();
            }
            finally
            {
                ReleaseLock();
            }
        }

        // ── Write overrides ───────────────────────────────────────────────────────────────────

        /// <inheritdoc/>
        public override void Write(byte[] buffer, int offset, int count)
        {
            AcquireLock();
            try
            {
                base.Write(buffer, offset, count);
            }
            finally
            {
                ReleaseLock();
            }
        }

        /// <inheritdoc/>
        public override void Write(ReadOnlySpan<byte> source)
        {
            AcquireLock();
            try
            {
                base.Write(source);
            }
            finally
            {
                ReleaseLock();
            }
        }

        /// <inheritdoc/>
        public override void WriteByte(byte value)
        {
            AcquireLock();
            try
            {
                base.WriteByte(value);
            }
            finally
            {
                ReleaseLock();
            }
        }

        /// <summary>
        /// Returns a managed copy of the stream's full contents without changing
        /// the current read position.
        /// </summary>
        public byte[] ToArray()
        {
            long saved = Position;
            try
            {
                Position = 0;
                byte[] data = new byte[Length];
                _ = Read(data, 0, data.Length);
                return data;
            }
            finally
            {
                Position = saved;
            }
        }

        /// <inheritdoc/>
        public override string ToString() => $"LuaStream({Length} bytes)";

        /// <summary>
        /// Sets <see cref="FlagAccessorDisposed"/> on the underlying block to signal C# is done,
        /// then closes the managed stream view.  The engine's ticker frees the block once Lua's
        /// GC also sets <see cref="FlagOwnerDisposed"/>.  If the stream was created via
        /// <see cref="KitsuneEngine.CreateStream"/> but never passed to Lua, both flags are set
        /// so the ticker can free the block on its next cycle.
        /// </summary>
        // Called by FillNativeVariable when a CreateStream block is about to cross into Lua.
        // Flips _isManaged to false so subsequent SetVariable calls go through the copy path
        // (prevents a second fast-pass of the same block to Lua).
        internal void MarkPassedToLua() => _isManaged = false;

        // Returns the block pointer only for CreateStream blocks that have not yet been passed to
        // Lua, enabling the zero-copy fast path in FillNativeVariable.  Inbound blocks always
        // return IntPtr.Zero (copy path) to prevent multiple Lua streams pointing at the same block.
        internal IntPtr GetSharedBlockPtr() => _isManaged ? _blockPtr : IntPtr.Zero;

        /// <summary>
        /// Sets <see cref="FlagAccessorDisposed"/> on the underlying block to signal C# is done,
        /// then closes the managed stream view.  The engine's ticker frees the block once Lua's
        /// GC also sets <see cref="FlagOwnerDisposed"/>.  If the stream was created via
        /// <see cref="KitsuneEngine.CreateStream"/> but never passed to Lua, both flags are set
        /// so the ticker can free the block on its next cycle.
        /// </summary>
        protected override void Dispose(bool disposing)
        {
            if (Interlocked.Exchange(ref _disposed, 1) == 0)
            {
                base.Dispose(disposing);  // closes managed stream view; does NOT touch native memory
                if (_blockPtr != IntPtr.Zero)
                {
                    byte flags = Marshal.ReadByte(_blockPtr, 0);
                    byte toSet = FlagAccessorDisposed;

                    // If Lua never created a stream from this block, nobody will set OWNER_DISPOSED;
                    // set it here so the ticker can free the block on its next cycle.
                    if ((flags & FlagLuaReferenced) == 0)
                    {
                        toSet |= FlagOwnerDisposed;
                    }

                    Marshal.WriteByte(_blockPtr, 0, (byte)(flags | toSet));
                    _blockPtr = IntPtr.Zero;
                }
            }
        }

        // ── LOCKED-flag helpers ───────────────────────────────────────────────────────────────
        // Sets FlagLocked before an access and clears it after (skipped on read-only blocks).
        // Non-atomic: see FlagLocked caveat above.
        private void AcquireLock()
        {
            if (CanWrite && _blockPtr != IntPtr.Zero)
            {
                Marshal.WriteByte(_blockPtr, 0, (byte)(Marshal.ReadByte(_blockPtr, 0) | FlagLocked));
            }
        }

        private void ReleaseLock()
        {
            if (CanWrite && _blockPtr != IntPtr.Zero)
            {
                Marshal.WriteByte(_blockPtr, 0, (byte)(Marshal.ReadByte(_blockPtr, 0) & ~FlagLocked));
            }
        }
    }
}
