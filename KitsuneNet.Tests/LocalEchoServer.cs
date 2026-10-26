using System.Net;
using System.Net.Sockets;
using System.Net.WebSockets;
using System.Text.Encodings.Web;
using System.Text.Json;

namespace KitsuneNet.Tests;

/// <summary>
/// Local stand-in for httpbin.org and echo.websocket.org, so HttpClient tests need no internet.
/// Plain .NET on purpose: a Kitsune server engine would share the process-wide Debug leak
/// counter with the client engine under test.
/// <list type="bullet">
/// <item>Any method and path: 200 with a JSON echo of method, url, decoded query args, headers and body.</item>
/// <item>Paths under <c>/delay/</c>: never answered, for timeout tests.</item>
/// <item><c>/ws</c>: WebSocket that echoes every message with its original frame type.</item>
/// </list>
/// </summary>
public sealed class LocalEchoServer : IDisposable
{
    private static readonly JsonSerializerOptions EchoJson = new() { Encoder = JavaScriptEncoder.UnsafeRelaxedJsonEscaping };

    private readonly HttpListener _listener = new();
    private readonly CancellationTokenSource _stop = new();
    private readonly Task _acceptLoop;

    public LocalEchoServer()
    {
        int port = GetFreePort();
        BaseUrl = $"http://127.0.0.1:{port}";
        WebSocketUrl = $"ws://127.0.0.1:{port}/ws";
        _listener.Prefixes.Add(BaseUrl + "/");
        _listener.Start();
        _acceptLoop = Task.Run(AcceptLoopAsync);
    }

    public string BaseUrl { get; }

    public string WebSocketUrl { get; }

    public void Dispose()
    {
        _stop.Cancel();
        _listener.Close();
        try
        {
            _acceptLoop.Wait(TimeSpan.FromSeconds(5));
        }
        catch (AggregateException)
        {
        }

        _stop.Dispose();
    }

    private static int GetFreePort()
    {
        var probe = new TcpListener(IPAddress.Loopback, 0);
        probe.Start();
        int port = ((IPEndPoint)probe.LocalEndpoint).Port;
        probe.Stop();
        return port;
    }

    private static async Task EchoRequestAsync(HttpListenerContext context)
    {
        HttpListenerRequest request = context.Request;
        string body;
        using (var reader = new StreamReader(request.InputStream, request.ContentEncoding))
        {
            body = await reader.ReadToEndAsync().ConfigureAwait(false);
        }

        var args = new Dictionary<string, string?>();
        foreach (string? key in request.QueryString.AllKeys)
        {
            if (key != null)
            {
                args[key] = request.QueryString[key];
            }
        }

        var headers = new Dictionary<string, string?>();
        foreach (string? key in request.Headers.AllKeys)
        {
            if (key != null)
            {
                headers[key] = request.Headers[key];
            }
        }

        var echo = new { method = request.HttpMethod, url = request.Url!.ToString(), args, headers, data = body };
        byte[] bytes = JsonSerializer.SerializeToUtf8Bytes(echo, EchoJson);
        context.Response.StatusCode = 200;
        context.Response.ContentType = "application/json";
        context.Response.ContentLength64 = bytes.Length;
        await context.Response.OutputStream.WriteAsync(bytes).ConfigureAwait(false);
        context.Response.Close();
    }

    private async Task AcceptLoopAsync()
    {
        while (!_stop.IsCancellationRequested)
        {
            HttpListenerContext context;
            try
            {
                context = await _listener.GetContextAsync().ConfigureAwait(false);
            }
            catch (Exception e) when (e is HttpListenerException or ObjectDisposedException or InvalidOperationException)
            {
                return;
            }

            _ = Task.Run(() => HandleAsync(context));
        }
    }

    private async Task HandleAsync(HttpListenerContext context)
    {
        try
        {
            string path = context.Request.Url!.AbsolutePath;
            if (context.Request.IsWebSocketRequest && path == "/ws")
            {
                await EchoWebSocketAsync(context).ConfigureAwait(false);
            }
            else if (path.StartsWith("/delay/", StringComparison.Ordinal))
            {
                await Task.Delay(Timeout.Infinite, _stop.Token).ConfigureAwait(false);
            }
            else
            {
                await EchoRequestAsync(context).ConfigureAwait(false);
            }
        }
        catch (Exception e) when (e is HttpListenerException or ObjectDisposedException or OperationCanceledException or WebSocketException or IOException)
        {
            // The client went away or the server is shutting down.
        }
    }

    private async Task EchoWebSocketAsync(HttpListenerContext context)
    {
        HttpListenerWebSocketContext wsContext = await context.AcceptWebSocketAsync(subProtocol: null).ConfigureAwait(false);
        using WebSocket ws = wsContext.WebSocket;
        byte[] buffer = new byte[64 * 1024];
        using var message = new MemoryStream();
        while (ws.State == WebSocketState.Open)
        {
            WebSocketReceiveResult result = await ws.ReceiveAsync(buffer, _stop.Token).ConfigureAwait(false);
            if (result.MessageType == WebSocketMessageType.Close)
            {
                await ws.CloseOutputAsync(WebSocketCloseStatus.NormalClosure, null, CancellationToken.None).ConfigureAwait(false);
                return;
            }

            message.Write(buffer, 0, result.Count);
            if (result.EndOfMessage)
            {
                await ws.SendAsync(message.ToArray(), result.MessageType, true, _stop.Token).ConfigureAwait(false);
                message.SetLength(0);
            }
        }
    }
}
