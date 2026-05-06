using System.IO;
using Xunit;

namespace KitsuneNet.Tests;

public sealed class LlamaFactAttribute : FactAttribute
{
    public static readonly string ModelPath = Path.GetFullPath(
        Path.Combine(
            AppContext.BaseDirectory,
            "..",
            "..",
            "..",
            "..",
            "tests",
            "models",
            "qwen3-0.6b-q8_0.gguf"));

    public LlamaFactAttribute()
    {
        if (!File.Exists(ModelPath))
        {
            Skip = $"Run tests/fetch-test-model.ps1 to download the test model ({ModelPath})";
        }
    }
}
