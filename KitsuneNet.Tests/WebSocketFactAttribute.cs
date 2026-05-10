using Xunit;

namespace KitsuneNet.Tests;

/// <summary>
/// A fact attribute for WebSocket tests.
/// WebSocket is always compiled in on all platforms (bundled libevent 2.2).
/// </summary>
public sealed class WebSocketFactAttribute : FactAttribute
{
}
