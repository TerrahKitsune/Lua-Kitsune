using System;
using System.Collections.Generic;
using System.Text;

namespace KitsuneNet
{
    /// <summary>
    /// Operation codes passed to a Lua function backend registered with <c>Stream.Create(fn)</c>.
    /// The function receives <c>(op, arg)</c> and must return the appropriate value for each op.
    /// </summary>
    /// <remarks>
    /// <para>The integer values are part of the public Lua stream-backend protocol.
    /// Lua scripts that implement custom backends should use these symbolic names
    /// (or their integer equivalents) rather than hardcoding numbers.</para>
    /// <para>Example backend skeleton:</para>
    /// <code>
    /// local CAP_READ, CAP_WRITE, CAP_SEEK = 1, 2, 4
    /// return Stream.Create(function(op, arg)
    ///     if     op == 0 then return CAP_READ + CAP_WRITE + CAP_SEEK  -- Open
    ///     elseif op == 1 then return true                              -- Close
    ///     elseif op == 2 then return readBytes(arg)                   -- Read
    ///     elseif op == 3 then return writeBytes(arg)                  -- Write
    ///     elseif op == 4 then return currentPosition                  -- CurPos
    ///     elseif op == 5 then return totalLength                      -- Len
    ///     elseif op == 6 then pos = arg; return true                  -- SetPos
    ///     elseif op == 7 then return { type = 'custom' }              -- Info
    ///     end
    /// end)
    /// </code>
    /// </remarks>
    public enum StreamBackendOp
    {
        /// <summary>Open the stream. Return an integer capability bitmask (see <see cref="StreamCaps"/>).</summary>
        Open = 0,

        /// <summary>Close the stream and release resources. Return <c>true</c> on success.</summary>
        Close = 1,

        /// <summary>Read up to <c>arg</c> bytes (0 = all remaining). Return a string, or an empty string / <c>false</c> on EOF.</summary>
        Read = 2,

        /// <summary>Write the string <c>arg</c>. Return <c>true</c> on success.</summary>
        Write = 3,

        /// <summary>Return the current cursor position as an integer.</summary>
        CurPos = 4,

        /// <summary>Return the total length of the stream as an integer.</summary>
        Len = 5,

        /// <summary>Move the cursor to position <c>arg</c>. Return <c>true</c> on success.</summary>
        SetPos = 6,

        /// <summary>Return a backend-defined info table (e.g. <c>{ type = "custom" }</c>).</summary>
        Info = 7,
    }
}
