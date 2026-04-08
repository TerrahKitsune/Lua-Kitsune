using System;
using System.Collections.Generic;
using System.Text;

namespace KitsuneNet
{
    /// <summary>Capability flags returned by a custom stream backend's <see cref="StreamBackendOp.Open"/> call
    /// and stored in <c>Stream:GetInfo()</c>'s <c>Caps</c> field.</summary>
    [Flags]
    public enum StreamCaps : byte
    {
        /// <summary>Stream supports read operations.</summary>
        Read = 1,

        /// <summary>Stream supports write operations.</summary>
        Write = 2,

        /// <summary>Stream supports seeking (<c>Stream:Seek</c>, <c>Stream:pos()</c>).</summary>
        Seek = 4,
    }
}
