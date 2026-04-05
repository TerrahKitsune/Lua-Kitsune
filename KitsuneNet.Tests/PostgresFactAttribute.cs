using Xunit;

namespace KitsuneNet.Tests;

public sealed class PostgresFactAttribute : FactAttribute
{
	public PostgresFactAttribute()
	{
		if (string.IsNullOrEmpty(
				Environment.GetEnvironmentVariable("KITSUNE_POSTGRES_TEST")))
			Skip = "Set KITSUNE_POSTGRES_TEST=<conninfo> to run Postgres tests";
	}
}
