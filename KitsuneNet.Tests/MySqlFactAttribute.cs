using Xunit;

namespace KitsuneNet.Tests;

public sealed class MySqlFactAttribute : FactAttribute
{
    public MySqlFactAttribute()
    {
        if (string.IsNullOrEmpty(
            Environment.GetEnvironmentVariable("KITSUNE_MYSQL_TEST")))
        {
            Skip = "Set KITSUNE_MYSQL_TEST=host:port:user:pass:db to run MySQL tests";
        }
    }
}
