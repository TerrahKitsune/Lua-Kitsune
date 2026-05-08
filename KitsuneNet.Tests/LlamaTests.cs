using KitsuneNet;
using Shouldly;
using Xunit;

namespace KitsuneNet.Tests
{
    [Collection("KitsuneSequential")]
    public sealed class LlamaTests
    {
        private static string ModelPath =>
            Environment.GetEnvironmentVariable("KITSUNE_LLAMA_MODEL")!;

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

        [LlamaFact]
        public void Llama_Generate_ProducesNonEmptyResponse()
        {
            using KitsuneEngine engine = new();
            var result = engine.RunString($@"
                local ctx = Llama.CreateContext()
                ctx:SetModel([[{ModelPath}]])
                ctx:LoadModel()
                local deadline = os.clock() + 30
                while ctx:Info().context.status == 'loading' and os.clock() < deadline do end
                ctx:Generate({{ {{ role = 'user', content = 'Say hello.' }} }})
                local result = ''
                local deadline2 = os.clock() + 60
                local ok, data = ctx:Poll()
                while ok do
                    if data then
                        if data.type == 'error' then error(data.text) end
                        if data.type == 'token' then result = result .. data.text end
                    end
                    Sleep(10)
                    ok, data = ctx:Poll()
                end
                ctx:Dispose()
                return result
            ");

            result.String.ShouldNotBeNullOrEmpty();
        }

        [LlamaFact]
        public void Llama_Generate_StatusBecomesIdle_AfterGeneration()
        {
            using KitsuneEngine engine = new();
            var result = engine.RunString($@"
                local ctx = Llama.CreateContext()
                ctx:SetModel([[{ModelPath}]])
                ctx:LoadModel()
                local deadline = os.clock() + 30
                while ctx:Info().context.status == 'loading' and os.clock() < deadline do end
                ctx:Generate({{ {{ role = 'user', content = 'Hi.' }} }})
                local deadline2 = os.clock() + 60
                local ok, data = ctx:Poll()
                while ok do
                    if data and data.type == 'error' then error(data.text) end
                    Sleep(10)
                    ok, data = ctx:Poll()
                end
                local status = ctx:Info().context.status
                ctx:Dispose()
                return status
            ");

            result.String.ShouldBe("idle");
        }

        [LlamaFact]
        public void Llama_Generate_Stop_Cancels()
        {
            using KitsuneEngine engine = new();
            var result = engine.RunString($@"
                local ctx = Llama.CreateContext()
                ctx:SetModel([[{ModelPath}]])
                ctx:LoadModel()
                local deadline = os.clock() + 30
                while ctx:Info().context.status == 'loading' and os.clock() < deadline do end
                ctx:Generate({{ {{ role = 'user', content = 'Count from 1 to 1000.' }} }})
                local count = 0
                local ok, data = ctx:Poll()
                while ok do
                    if data then
                        if data.type == 'error' then error(data.text) end
                        if data.type == 'token' then
                            count = count + 1
                            if count >= 5 then
                                ctx:Stop()
                                break
                            end
                        end
                    end
                    Sleep(10)
                    ok, data = ctx:Poll()
                end
                local deadline3 = os.clock() + 10
                while ctx:Info().context.status ~= 'idle' and os.clock() < deadline3 do end
                local status = ctx:Info().context.status
                ctx:Dispose()
                return status
            ");

            result.String.ShouldBe("idle");
        }
    }
}
