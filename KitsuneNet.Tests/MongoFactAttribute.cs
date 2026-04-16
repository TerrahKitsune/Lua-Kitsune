using Xunit;

namespace KitsuneNet.Tests;

public sealed class MongoFactAttribute : FactAttribute
{
    public MongoFactAttribute()
    {
        if (string.IsNullOrEmpty(
            Environment.GetEnvironmentVariable("KITSUNE_MONGO_TEST")))
        {
            Skip = "Set KITSUNE_MONGO_TEST=mongodb://... to run MongoDB tests";
        }
    }
}
