using Xunit;

namespace KitsuneNet.Tests;

/// <summary>
/// Every test class joins this collection, so the tests run one at a time and the fixture is disposed once,
/// after the last test.
/// </summary>
[CollectionDefinition("KitsuneSequential")]
public sealed class KitsuneSequentialCollection : ICollectionFixture<LeakSanitizerCheck>
{
}
