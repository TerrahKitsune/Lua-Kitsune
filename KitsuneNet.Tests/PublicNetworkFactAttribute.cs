using Xunit;

namespace KitsuneNet.Tests;

/// <summary>
/// Marks a test that needs the public internet (for example real TLS certificate checks).
/// Skipped unless the environment variable <c>KITSUNE_PUBLIC_NET_TEST</c> is <c>1</c>.
/// </summary>
public sealed class PublicNetworkFactAttribute : FactAttribute
{
    public PublicNetworkFactAttribute()
    {
        if (Environment.GetEnvironmentVariable("KITSUNE_PUBLIC_NET_TEST") != "1")
        {
            Skip = "Needs the public internet: set KITSUNE_PUBLIC_NET_TEST=1 to run";
        }
    }
}
