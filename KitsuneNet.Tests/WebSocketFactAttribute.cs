using Xunit;

namespace KitsuneNet.Tests;

/// <summary>
/// A fact that is skipped automatically on platforms where WebSocket server
/// support is not compiled in (libevent &lt; 2.2, e.g. Ubuntu 24.04).
/// On Windows, WebSocket is always included. On Linux it requires libevent &gt;= 2.2.
/// </summary>
public sealed class WebSocketFactAttribute : FactAttribute
{
    public WebSocketFactAttribute()
    {
        if (!OperatingSystem.IsWindows())
            Skip = "WebSocket server support requires libevent >= 2.2 (not available on this platform)";
    }
}
