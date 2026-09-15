using KitsuneNet;
using Shouldly;
using Xunit;

namespace KitsuneNet.Tests;

// See KitsuneEngineTests for why both classes share a single collection.
[Collection("KitsuneSequential")]

/// <summary>
/// Tests for the Sound module (native WAV decode/encode + PCM sample editing).
/// Temp file paths are generated from Lua via <c>FileSystem.GetTempFileName()</c>,
/// matching the convention used throughout ImageTests.cs.
/// </summary>
public sealed class SoundTests
{
    [Fact]
    public async Task Sound_New_IsSilentWithRequestedShape()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(@"
            local snd = Sound.New(44100, 2, 100)
            local s = snd:GetSample(50, 1)
            return snd:GetSampleRate()..','..snd:GetChannels()..','..snd:GetFrameCount()..','..s
        ");
        r.String.ShouldBe("44100,2,100,0.0");
    }

    [Fact]
    public async Task Sound_SetSample_GetSample_RoundTrips()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(@"
            local snd = Sound.New(44100, 1, 10)
            snd:SetSample(3, 0, 0.5)
            return snd:GetSample(3, 0)
        ");
        r.Number.ShouldBe(0.5, 0.0001);
    }

    [Fact]
    public async Task Sound_GetSample_OutOfBounds_Throws()
    {
        using KitsuneEngine engine = new();
        LuaException ex = await Should.ThrowAsync<LuaException>(
            engine.ExecuteStringAsync("local snd = Sound.New(44100,1,10); return snd:GetSample(50, 0)"));
        ex.Message.ShouldContain("out of bounds");
    }

    [Fact]
    public async Task Sound_Save_Open_RoundTrips_WithinQuantizationTolerance()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(@"
            local path = FileSystem.GetTempFileName() .. '_kitsune_sound_roundtrip.wav'
            local snd = Sound.New(22050, 1, 20)
            snd:SetSample(10, 0, 0.75)
            snd:Save(path)
            local snd2 = Sound.Open(path)
            local diff = math.abs(snd2:GetSample(10, 0) - 0.75)
            return snd2:GetSampleRate()..','..snd2:GetChannels()..','..snd2:GetFrameCount()..','..tostring(diff < 0.001)
        ");
        r.String.ShouldBe("22050,1,20,true");
    }

    [Fact]
    public async Task Sound_Open_MissingOrEmptyFile_Throws()
    {
        using KitsuneEngine engine = new();
        LuaException ex = await Should.ThrowAsync<LuaException>(
            engine.ExecuteStringAsync("return Sound.Open(FileSystem.GetTempFileName() .. '_kitsune_missing_xyz.wav')"));
        ex.Message.ShouldContain("Sound.Open");
    }

    [Fact]
    public async Task Sound_ToBytes_FromBytes_RoundTrips()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(@"
            local snd = Sound.New(44100, 1, 5)
            snd:SetSample(2, 0, -0.5)
            local bytes = snd:ToBytes()
            local snd2 = Sound.FromBytes(bytes)
            return tostring(math.abs(snd2:GetSample(2, 0) - (-0.5)) < 0.001)
        ");
        r.String.ShouldBe("true");
    }

    [Fact]
    public async Task Sound_Tone_ProducesNonSilentOscillatingBuffer()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(@"
            local snd = Sound.Tone(44100, 1, 4410, 100, 'sine', 1.0)
            local sawPositive, sawNegative = false, false
            for i = 0, snd:GetFrameCount() - 1 do
                local s = snd:GetSample(i, 0)
                if s > 0.1 then sawPositive = true end
                if s < -0.1 then sawNegative = true end
            end
            return tostring(sawPositive and sawNegative)
        ");
        r.String.ShouldBe("true");
    }

    [Fact]
    public async Task Sound_Tone_UnknownWaveform_Throws()
    {
        using KitsuneEngine engine = new();
        LuaException ex = await Should.ThrowAsync<LuaException>(
            engine.ExecuteStringAsync("return Sound.Tone(44100, 1, 100, 100, 'bogus')"));
        ex.Message.ShouldContain("waveform");
    }

    [Fact]
    public async Task Sound_Slice_ReturnsCorrectFrameCountAndContent()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(@"
            local snd = Sound.New(44100, 1, 10)
            snd:SetSample(5, 0, 0.25)
            local sliced = snd:Slice(4, 3)
            return sliced:GetFrameCount()..','..sliced:GetSample(1, 0)
        ");
        r.String.ShouldBe("3,0.25");
    }

    [Fact]
    public async Task Sound_Concat_SumsFrameCounts()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(@"
            local a = Sound.New(44100, 1, 4)
            local b = Sound.New(44100, 1, 6)
            local c = a:Concat(b)
            return c:GetFrameCount()
        ");
        r.Int64.ShouldBe(10);
    }

    [Fact]
    public async Task Sound_Concat_ChannelMismatch_Throws()
    {
        using KitsuneEngine engine = new();
        LuaException ex = await Should.ThrowAsync<LuaException>(
            engine.ExecuteStringAsync("local a = Sound.New(44100,1,4); local b = Sound.New(44100,2,4); return a:Concat(b)"));
        ex.Message.ShouldContain("channel");
    }

    [Fact]
    public async Task Sound_Mix_OfSoundWithItself_DoublesAmplitude_AndClamps()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(@"
            local a = Sound.New(44100, 1, 4)
            a:SetSample(0, 0, 0.3)
            a:SetSample(1, 0, 0.9)
            local b = a:Clone()
            a:Mix(b, 0)
            return string.format('%.2f,%.2f', a:GetSample(0, 0), a:GetSample(1, 0))
        ");
        r.String.ShouldBe("0.60,1.00");
    }

    [Fact]
    public async Task Sound_ApplyGain_ScalesSamples()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(@"
            local snd = Sound.New(44100, 1, 4)
            snd:SetSample(0, 0, 0.5)
            snd:ApplyGain(0.5)
            return snd:GetSample(0, 0)
        ");
        r.Number.ShouldBe(0.25, 0.0001);
    }

    [Fact]
    public async Task Sound_Fade_AppliesLinearEnvelope()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(@"
            local snd = Sound.New(44100, 1, 3)
            snd:SetSample(0, 0, 1.0)
            snd:SetSample(1, 0, 1.0)
            snd:SetSample(2, 0, 1.0)
            snd:Fade(0, 3, 0.0, 1.0)
            return snd:GetSample(0, 0)..','..snd:GetSample(2, 0)
        ");
        r.String.ShouldBe("0.0,1.0");
    }

    [Fact]
    public async Task Sound_Normalize_ScalesPeakToTarget()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(@"
            local snd = Sound.New(44100, 1, 2)
            snd:SetSample(0, 0, 0.25)
            snd:SetSample(1, 0, -0.5)
            snd:Normalize(1.0)
            return snd:GetSample(0, 0)..','..snd:GetSample(1, 0)
        ");
        r.String.ShouldBe("0.5,-1.0");
    }

    [Fact]
    public async Task Sound_Reverse_ReversesFrameOrder()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(@"
            local snd = Sound.New(44100, 1, 3)
            snd:SetSample(0, 0, 0.1)
            snd:SetSample(2, 0, 0.9)
            snd:Reverse()
            return string.format('%.2f,%.2f', snd:GetSample(0, 0), snd:GetSample(2, 0))
        ");
        r.String.ShouldBe("0.90,0.10");
    }

    [Fact]
    public async Task Sound_GetPeak_And_GetRMS_ReportCorrectly()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(@"
            local snd = Sound.New(44100, 1, 2)
            snd:SetSample(0, 0, -0.5)
            snd:SetSample(1, 0, 0.5)
            local mn, mx = snd:GetPeak()
            local rms = snd:GetRMS()
            return mn..','..mx..','..rms
        ");
        r.String.ShouldBe("-0.5,0.5,0.5");
    }

    [Fact]
    public async Task Sound_Resample_ScalesFrameCountProportionally()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(@"
            local snd = Sound.New(44100, 1, 100)
            local resampled = snd:Resample(22050)
            return resampled:GetSampleRate()..','..resampled:GetFrameCount()
        ");
        r.String.ShouldBe("22050,50");
    }
}
