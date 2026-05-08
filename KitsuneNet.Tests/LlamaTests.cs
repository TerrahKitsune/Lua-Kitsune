using KitsuneNet;
using Shouldly;
using Xunit;

namespace KitsuneNet.Tests
{
    [Collection("KitsuneSequential")]
    public sealed class LlamaTests
    {
        [Fact]
        public void Llama_GlobalExists()
        {
            using KitsuneEngine engine = new();
            engine.RunString("return type(Llama)").String.ShouldBe("table");
        }

        [Fact]
        public void Llama_CreateContext_ReturnsUserdata()
        {
            using KitsuneEngine engine = new();
            engine.RunString("local ctx = Llama.CreateContext(); local t = type(ctx); ctx:Dispose(); return t").String.ShouldBe("userdata");
        }

        [Fact]
        public void Llama_GetLogs_ReturnsTable()
        {
            using KitsuneEngine engine = new();
            engine.RunString("return type(Llama.GetLogs())").String.ShouldBe("table");
        }

        [Fact]
        public void Llama_Context_Dispose_IsIdempotent()
        {
            using KitsuneEngine engine = new();
            engine.RunString(@"
                local ctx = Llama.CreateContext()
                ctx:Dispose()
                ctx:Dispose()
                return true
            ").Boolean.ShouldBeTrue();
        }

        [Fact]
        public void Llama_Context_Info_ReturnsTable()
        {
            using KitsuneEngine engine = new();
            engine.RunString(@"
                local ctx = Llama.CreateContext()
                local info = ctx:Info()
                ctx:Dispose()
                return type(info)
            ").String.ShouldBe("table");
        }

        [Fact]
        public void Llama_Context_StatusIdle_WhenNew()
        {
            using KitsuneEngine engine = new();
            engine.RunString(@"
                local ctx = Llama.CreateContext()
                local info = ctx:Info()
                ctx:Dispose()
                return info.context.status
            ").String.ShouldBe("idle");
        }
    }
}
