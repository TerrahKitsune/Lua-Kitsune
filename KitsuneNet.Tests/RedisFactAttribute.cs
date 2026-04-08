using Xunit;

namespace KitsuneNet.Tests;

public sealed class RedisFactAttribute : FactAttribute
{
    public RedisFactAttribute()
    {
        if (string.IsNullOrEmpty(
            Environment.GetEnvironmentVariable("KITSUNE_REDIS_TEST")))
        {
            Skip = "Set KITSUNE_REDIS_TEST=host:port (or host:port:password) to run Redis tests";
        }
    }
}
