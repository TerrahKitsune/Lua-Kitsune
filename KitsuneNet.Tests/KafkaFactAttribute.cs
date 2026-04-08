using Xunit;

namespace KitsuneNet.Tests;

public sealed class KafkaFactAttribute : FactAttribute
{
    public KafkaFactAttribute()
    {
        if (string.IsNullOrEmpty(
            Environment.GetEnvironmentVariable("KITSUNE_KAFKA_TEST")))
        {
            Skip = "Set KITSUNE_KAFKA_TEST=host:port:topic:partition to run Kafka tests";
        }
    }
}
