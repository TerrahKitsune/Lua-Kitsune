using System.Collections.Generic;
using System.Runtime.CompilerServices;
using System.Threading;

namespace KitsuneNet
{
    /// <summary>
    /// Cancellation and convenience handle for a <see cref="LuaValue"/> iterator
    /// created via <see cref="LuaValue.FromIterator(IEnumerable{LuaValue}, out LuaIteratorRef)"/>
    /// or one of its overloads.
    /// <para>
    /// Unlike <see cref="LuaFunctionRef"/> and <see cref="LuaThreadRef"/>, this class holds
    /// <b>no Lua registry reference</b>. The iterator closure's lifetime is managed entirely by
    /// Lua GC; <c>finalized</c> fires automatically when the closure is collected.
    /// </para>
    /// <para>
    /// <see cref="Dispose"/> (or <see cref="Cancel"/>) signals the iterator to stop: the next
    /// Lua call returns <c>nil</c>, breaking the <c>for</c> loop, after which Lua GC collects
    /// the closure and invokes <c>finalized</c> to release the underlying enumerator.
    /// </para>
    /// </summary>
    public sealed class LuaIteratorRef : IDisposable
    {
        private readonly IEnumerable<LuaValue>? _source;
        private readonly IAsyncEnumerable<LuaValue>? _asyncSource;
        private volatile int _cancelled;

        internal LuaIteratorRef(IEnumerable<LuaValue> source) => _source = source;

        internal LuaIteratorRef(IAsyncEnumerable<LuaValue> asyncSource) => _asyncSource = asyncSource;

        /// <summary>
        /// Returns <c>true</c> once <see cref="Cancel"/> or <see cref="Dispose"/> has been called.
        /// </summary>
        public bool IsCancelled => _cancelled != 0;

        /// <summary>
        /// Signals the iterator to stop. The next Lua call returns <c>nil</c>, causing the
        /// <c>for</c> loop to break cleanly. Lua GC will then collect the closure and fire
        /// <c>finalized</c> to release any underlying resources (e.g. a DB cursor).
        /// </summary>
        public void Cancel() => Interlocked.CompareExchange(ref _cancelled, 1, 0);

        /// <summary>
        /// Enumerates the underlying source on the C# side, independently of any Lua iteration.
        /// <para>
        /// For re-enumerable sources (arrays, <see cref="List{T}"/>, LINQ queries) this creates a
        /// fresh independent cursor each call. For single-use sources (DB cursors, network streams)
        /// only one consumer — either this method or the Lua closure — should iterate; calling both
        /// will produce undefined results.
        /// </para>
        /// <para>
        /// Unlike <see cref="LuaThreadRef.Iterate"/>, this method does <b>not</b> drive any Lua
        /// execution; it enumerates the underlying C# source directly.
        /// </para>
        /// </summary>
        public IEnumerable<LuaValue> Iterator(CancellationToken cancellationToken = default)
        {
            ObjectDisposedException.ThrowIf(_cancelled != 0, this);
            IEnumerable<LuaValue> src = _source
                ?? (_asyncSource?.ToBlockingEnumerable(cancellationToken)
                    ?? throw new InvalidOperationException("No source available."));
            foreach (LuaValue v in src)
            {
                cancellationToken.ThrowIfCancellationRequested();
                yield return v;
            }
        }

        /// <summary>
        /// Asynchronously enumerates the underlying source on the C# side, independently of any
        /// Lua iteration. Uses the <see cref="IAsyncEnumerable{T}"/> source natively when available;
        /// otherwise wraps the synchronous source with per-item cancellation checks.
        /// </summary>
        public async IAsyncEnumerable<LuaValue> IteratorAsync(
            [EnumeratorCancellation] CancellationToken cancellationToken = default)
        {
            ObjectDisposedException.ThrowIf(_cancelled != 0, this);
            if (_asyncSource is not null)
            {
                await foreach (LuaValue v in _asyncSource.WithCancellation(cancellationToken).ConfigureAwait(false))
                {
                    yield return v;
                }
            }
            else if (_source is not null)
            {
                foreach (LuaValue v in _source)
                {
                    cancellationToken.ThrowIfCancellationRequested();
                    yield return v;
                }
            }
        }

        /// <summary>
        /// Signals cancellation. Does not release any Lua registry reference (none is held).
        /// </summary>
        public void Dispose() => Cancel();

        /// <summary>
        /// Returns an <see cref="IEnumerator{T}"/> over the underlying source for use by the
        /// native marshalling layer. For async sources, blocks per-item via
        /// <c>ToBlockingEnumerable()</c> — only suitable for fast async sources.
        /// </summary>
        internal IEnumerator<LuaValue>? GetSyncEnumerator()
        {
            if (_source is not null)
            {
                return _source.GetEnumerator();
            }

            if (_asyncSource is not null)
            {
                return _asyncSource.ToBlockingEnumerable().GetEnumerator();
            }

            return null;
        }
    }
}
