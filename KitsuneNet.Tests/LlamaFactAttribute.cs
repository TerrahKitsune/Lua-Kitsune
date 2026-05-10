using Xunit;

namespace KitsuneNet.Tests;

public sealed class LlamaFactAttribute : FactAttribute
{
    public LlamaFactAttribute()
    {
        if (!OperatingSystem.IsWindows())
        {
            Skip = "Windows-only test (Llama is not available on this platform)";
            return;
        }

        if (string.IsNullOrEmpty(
            Environment.GetEnvironmentVariable("KITSUNE_LLAMA_MODEL")))
        {
            Skip = "Set KITSUNE_LLAMA_MODEL=path/to/model.gguf to run Llama generate tests";
        }
    }
}
