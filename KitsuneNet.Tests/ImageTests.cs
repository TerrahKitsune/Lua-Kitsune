using KitsuneNet;
using Shouldly;
using Xunit;

namespace KitsuneNet.Tests;

// See KitsuneEngineTests for why both classes share a single collection.
[Collection("KitsuneSequential")]

/// <summary>
/// Tests for the Image module (native PNG decode/encode + pixel editing).
/// Temp file paths are generated from Lua via <c>FileSystem.GetTempFileName()</c>,
/// matching the convention used throughout KitsuneUtilTests.cs.
/// </summary>
public sealed class ImageTests
{
    [Fact]
    public async Task Image_New_DefaultsToFullyTransparent()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(@"
            local img = Image.New(2, 2)
            local r,g,b,a = img:GetPixel(0, 0)
            return r..','..g..','..b..','..a
        ");
        r.String.ShouldBe("0,0,0,0");
    }

    [Fact]
    public async Task Image_SetPixel_GetPixel_RoundTrips()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(@"
            local img = Image.New(4, 4)
            img:SetPixel(1, 2, 10, 20, 30, 255)
            local r,g,b,a = img:GetPixel(1, 2)
            return r..','..g..','..b..','..a
        ");
        r.String.ShouldBe("10,20,30,255");
    }

    [Fact]
    public async Task Image_GetPixel_OutOfBounds_Throws()
    {
        using KitsuneEngine engine = new();
        LuaException ex = await Should.ThrowAsync<LuaException>(
            engine.ExecuteStringAsync("local img = Image.New(2,2); return img:GetPixel(5, 5)"));
        ex.Message.ShouldContain("out of bounds");
    }

    [Fact]
    public async Task Image_Save_Open_RoundTrips_Pixels_And_Dimensions()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(@"
            local path = FileSystem.GetTempFileName() .. '_kitsune_image_roundtrip.png'
            local img = Image.New(3, 3)
            img:SetPixel(1, 1, 200, 100, 50, 255)
            img:Save(path)
            local img2 = Image.Open(path)
            local r,g,b,a = img2:GetPixel(1, 1)
            return img2:GetWidth()..'x'..img2:GetHeight()..':'..r..','..g..','..b..','..a
        ");
        r.String.ShouldBe("3x3:200,100,50,255");
    }

    [Fact]
    public async Task Image_Open_MissingOrEmptyFile_Throws()
    {
        using KitsuneEngine engine = new();
        LuaException ex = await Should.ThrowAsync<LuaException>(
            engine.ExecuteStringAsync("return Image.Open(FileSystem.GetTempFileName() .. '_kitsune_missing_xyz.png')"));
        ex.Message.ShouldContain("Image.Open");
    }

    [Fact]
    public async Task Image_SetMetadata_RoundTrips_ThroughSaveAndOpen()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(@"
            local path = FileSystem.GetTempFileName() .. '_kitsune_image_meta.png'
            local img = Image.New(2, 2)
            img:SetMetadata('location', 'sight-glass-1')
            img:Save(path)
            local img2 = Image.Open(path)
            local meta = img2:GetMetadata()
            return meta.width..'x'..meta.height..':'..tostring(meta.tags.location)
        ");
        r.String.ShouldBe("2x2:sight-glass-1");
    }

    [Fact]
    public async Task Image_ToBytes_FromBytes_RoundTrips()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(@"
            local img = Image.New(2, 2)
            img:SetPixel(0, 0, 1, 2, 3, 4)
            local bytes = img:ToBytes()
            local img2 = Image.FromBytes(bytes)
            local r,g,b,a = img2:GetPixel(0, 0)
            return r..','..g..','..b..','..a
        ");
        r.String.ShouldBe("1,2,3,4");
    }

    [Fact]
    public async Task Image_Crop_ReturnsCorrectDimensionsAndContent()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(@"
            local img = Image.New(5, 5)
            img:SetPixel(2, 3, 9, 8, 7, 255)
            local cropped = img:Crop(1, 2, 3, 3)
            local r,g,b,a = cropped:GetPixel(1, 1)
            return cropped:GetWidth()..'x'..cropped:GetHeight()..':'..r..','..g..','..b..','..a
        ");
        r.String.ShouldBe("3x3:9,8,7,255");
    }

    [Fact]
    public async Task Image_Resize_Nearest_HasNoBlendedEdges()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(@"
            local img = Image.New(2, 2)
            img:SetPixel(0, 0, 255, 0, 0, 255)
            img:SetPixel(1, 0, 0, 255, 0, 255)
            img:SetPixel(0, 1, 0, 0, 255, 255)
            img:SetPixel(1, 1, 255, 255, 0, 255)
            local big = img:Resize(4, 4, 'nearest')
            local r1,g1,b1,a1 = big:GetPixel(0, 0)
            local r2,g2,b2,a2 = big:GetPixel(3, 3)
            return (r1..','..g1..','..b1..','..a1)..'|'..(r2..','..g2..','..b2..','..a2)
        ");
        r.String.ShouldBe("255,0,0,255|255,255,0,255");
    }

    [Fact]
    public async Task Image_Resize_UnknownFilter_Throws()
    {
        using KitsuneEngine engine = new();
        LuaException ex = await Should.ThrowAsync<LuaException>(
            engine.ExecuteStringAsync("local img = Image.New(2,2); return img:Resize(4, 4, 'bogus')"));
        ex.Message.ShouldContain("filter");
    }

    [Fact]
    public async Task Image_Resize_ReturnsCorrectDimensions()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(@"
            local img = Image.New(4, 4)
            local resized = img:Resize(8, 2)
            return resized:GetWidth()..'x'..resized:GetHeight()
        ");
        r.String.ShouldBe("8x2");
    }

    [Fact]
    public async Task Image_Clone_IsIndependentCopy()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(@"
            local img = Image.New(2, 2)
            img:SetPixel(0, 0, 1, 1, 1, 255)
            local copy = img:Clone()
            copy:SetPixel(0, 0, 9, 9, 9, 255)
            local r1,g1,b1,a1 = img:GetPixel(0, 0)
            local r2,g2,b2,a2 = copy:GetPixel(0, 0)
            return (r1..','..g1..','..b1..','..a1)..'|'..(r2..','..g2..','..b2..','..a2)
        ");
        r.String.ShouldBe("1,1,1,255|9,9,9,255");
    }

    [Fact]
    public async Task Image_Composite_BlendsOntoTarget_LeavesRestUntouched()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(@"
            local bg = Image.New(4, 4)
            bg:FillRect(0, 0, 4, 4, 255, 255, 255, 255)
            local fg = Image.New(2, 2)
            fg:FillRect(0, 0, 2, 2, 0, 0, 0, 255)
            bg:Composite(fg, 1, 1)
            local r1,g1,b1,a1 = bg:GetPixel(1, 1)
            local r2,g2,b2,a2 = bg:GetPixel(0, 0)
            return (r1..','..g1..','..b1..','..a1)..'|'..(r2..','..g2..','..b2..','..a2)
        ");
        r.String.ShouldBe("0,0,0,255|255,255,255,255");
    }

    [Fact]
    public async Task Image_Diff_ReportsChangedBoundingBox()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(@"
            local a = Image.New(5, 5)
            local b = a:Clone()
            b:SetPixel(2, 3, 9, 9, 9, 255)
            local x,y,w,h,changed = a:Diff(b)
            return x..','..y..','..w..','..h..','..changed
        ");
        r.String.ShouldBe("2,3,1,1,1");
    }

    [Fact]
    public async Task Image_Diff_NoChanges_ReturnsNilsAndZeroCount()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(@"
            local a = Image.New(3, 3)
            local b = a:Clone()
            local x,y,w,h,changed = a:Diff(b)
            return tostring(x)..','..tostring(y)..','..tostring(w)..','..tostring(h)..','..tostring(changed)
        ");
        r.String.ShouldBe("nil,nil,nil,nil,0");
    }

    [Fact]
    public async Task Image_DrawCircle_Filled_PaintsCenter()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(@"
            local img = Image.New(10, 10)
            img:DrawCircle(5, 5, 3, 255, 0, 0, 255, true)
            local r,g,b,a = img:GetPixel(5, 5)
            return r..','..g..','..b..','..a
        ");
        r.String.ShouldBe("255,0,0,255");
    }

    [Fact]
    public async Task Image_DrawLine_PaintsBothEndpoints()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(@"
            local img = Image.New(10, 10)
            img:DrawLine(1, 1, 8, 1, 0, 255, 0, 255)
            local r1,g1,b1,a1 = img:GetPixel(1, 1)
            local r2,g2,b2,a2 = img:GetPixel(8, 1)
            return (r1..','..g1..','..b1..','..a1)..'|'..(r2..','..g2..','..b2..','..a2)
        ");
        r.String.ShouldBe("0,255,0,255|0,255,0,255");
    }

    [Fact]
    public async Task Image_DrawLine_Antialiased_ProducesPartialCoverageOnAdjacentRow()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(@"
            local img = Image.New(10, 10)
            img:DrawLine(0, 5, 9, 5, 0, 255, 0, 255, 1, true)
            local r1,g1,b1,a1 = img:GetPixel(5, 5)
            local r2,g2,b2,a2 = img:GetPixel(5, 4)
            local r3,g3,b3,a3 = img:GetPixel(5, 3)
            return (r1..','..g1..','..b1..','..a1)..'|'..(r2..','..g2..','..b2..','..a2)..'|'..(r3..','..g3..','..b3..','..a3)
        ");
        r.String.ShouldBe("0,255,0,128|0,255,0,128|0,0,0,0");
    }

    [Fact]
    public async Task Image_DrawCircle_Antialiased_ProducesPartialCoverageAtBoundary()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(@"
            local img = Image.New(12, 12)
            img:DrawCircle(5, 5, 3, 0, 0, 255, 255, true, true)
            local r1,g1,b1,a1 = img:GetPixel(5, 5)
            local r2,g2,b2,a2 = img:GetPixel(8, 5)
            return (r1..','..g1..','..b1..','..a1)..'|'..(r2..','..g2..','..b2..','..a2)
        ");
        r.String.ShouldBe("0,0,255,255|0,0,255,128");
    }

    [Fact]
    public async Task Image_RGBtoHSV_HSVtoRGB_RoundTrips_PureRed()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(@"
            local h, s, v = Image.RGBtoHSV(255, 0, 0)
            local r2, g2, b2 = Image.HSVtoRGB(h, s, v)
            return string.format('%.2f,%.2f,%.2f:%d,%d,%d', h, s, v, r2, g2, b2)
        ");
        r.String.ShouldBe("0.00,1.00,1.00:255,0,0");
    }

    [Fact]
    public async Task Image_RGBtoHSL_HSLtoRGB_RoundTrips_PureRed()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(@"
            local h, s, l = Image.RGBtoHSL(255, 0, 0)
            local r2, g2, b2 = Image.HSLtoRGB(h, s, l)
            return string.format('%.2f,%.2f,%.2f:%d,%d,%d', h, s, l, r2, g2, b2)
        ");
        r.String.ShouldBe("0.00,1.00,0.50:255,0,0");
    }

    [Fact]
    public async Task Image_Tint_PreservesBrightness_ReplacesHue()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(@"
            local img = Image.New(2, 2)
            img:FillRect(0, 0, 2, 2, 255, 255, 255, 255)
            img:Tint(255, 0, 0, 1.0)
            local r,g,b,a = img:GetPixel(0, 0)
            return r..','..g..','..b..','..a
        ");
        r.String.ShouldBe("255,0,0,255");
    }

    [Fact]
    public async Task Image_RecolorPalette_SwapsExactMatch_LeavesOthersUntouched()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(@"
            local img = Image.New(2, 1)
            img:SetPixel(0, 0, 10, 20, 30, 255)
            img:SetPixel(1, 0, 99, 99, 99, 255)
            img:RecolorPalette({ {10, 20, 30, 200, 50, 60, 0} })
            local r1,g1,b1,a1 = img:GetPixel(0, 0)
            local r2,g2,b2,a2 = img:GetPixel(1, 0)
            return (r1..','..g1..','..b1..','..a1)..'|'..(r2..','..g2..','..b2..','..a2)
        ");
        r.String.ShouldBe("200,50,60,255|99,99,99,255");
    }

    [Fact]
    public async Task Image_AdjustHSV_HueShift180_TurnsRedToCyan()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(@"
            local img = Image.New(1, 1)
            img:SetPixel(0, 0, 255, 0, 0, 255)
            img:AdjustHSV(180)
            local r,g,b,a = img:GetPixel(0, 0)
            return r..','..g..','..b..','..a
        ");
        r.String.ShouldBe("0,255,255,255");
    }

    [Fact]
    public async Task Image_Dither_OnlyProducesPaletteColors()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(@"
            local img = Image.New(4, 4)
            img:FillRect(0, 0, 4, 4, 128, 128, 128, 255)
            img:Dither({ {0,0,0}, {255,255,255} }, 64)
            local ok = true
            for y = 0, 3 do
                for x = 0, 3 do
                    local r,g,b = img:GetPixel(x, y)
                    if not ((r == 0 or r == 255) and r == g and g == b) then ok = false end
                end
            end
            return tostring(ok)
        ");
        r.String.ShouldBe("true");
    }

    [Fact]
    public async Task Image_Outline_PaintsAdjacentTransparentPixels_LeavesCenterUntouched()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(@"
            local img = Image.New(5, 5)
            img:SetPixel(2, 2, 9, 9, 9, 255)
            img:Outline(255, 0, 0, 255, 1)
            local r1,g1,b1,a1 = img:GetPixel(1, 2)
            local r2,g2,b2,a2 = img:GetPixel(2, 2)
            return (r1..','..g1..','..b1..','..a1)..'|'..(r2..','..g2..','..b2..','..a2)
        ");
        r.String.ShouldBe("255,0,0,255|9,9,9,255");
    }

    [Fact]
    public async Task Image_Stamp_PastesBrushCenteredOnPoint()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(@"
            local img = Image.New(10, 10)
            local brush = Image.New(2, 2)
            brush:FillRect(0, 0, 2, 2, 255, 0, 0, 255)
            img:Stamp(brush, 5, 5, 1.0)
            local r,g,b,a = img:GetPixel(5, 5)
            return r..','..g..','..b..','..a
        ");
        r.String.ShouldBe("255,0,0,255");
    }

    [Fact]
    public async Task Image_StrokePath_PaintsAlongWholePath()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(@"
            local img = Image.New(10, 10)
            local brush = Image.New(1, 1)
            brush:SetPixel(0, 0, 0, 255, 0, 255)
            img:StrokePath(brush, { {1, 1}, {5, 1} }, 1, 1.0)
            local r1,g1,b1,a1 = img:GetPixel(1, 1)
            local r2,g2,b2,a2 = img:GetPixel(3, 1)
            local r3,g3,b3,a3 = img:GetPixel(5, 1)
            return (r1..','..g1..','..b1..','..a1)..'|'..(r2..','..g2..','..b2..','..a2)..'|'..(r3..','..g3..','..b3..','..a3)
        ");
        r.String.ShouldBe("0,255,0,255|0,255,0,255|0,255,0,255");
    }

    [Fact]
    public async Task Image_FlipHorizontal_MirrorsPixels()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(@"
            local img = Image.New(3, 1)
            img:SetPixel(0, 0, 10, 20, 30, 255)
            local flipped = img:FlipHorizontal()
            local r1,g1,b1,a1 = flipped:GetPixel(2, 0)
            local r2,g2,b2,a2 = flipped:GetPixel(0, 0)
            return (r1..','..g1..','..b1..','..a1)..'|'..(r2..','..g2..','..b2..','..a2)
        ");
        r.String.ShouldBe("10,20,30,255|0,0,0,0");
    }

    [Fact]
    public async Task Image_FlipVertical_MirrorsPixels()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(@"
            local img = Image.New(1, 3)
            img:SetPixel(0, 0, 10, 20, 30, 255)
            local flipped = img:FlipVertical()
            local r1,g1,b1,a1 = flipped:GetPixel(0, 2)
            return r1..','..g1..','..b1..','..a1
        ");
        r.String.ShouldBe("10,20,30,255");
    }

    [Fact]
    public async Task Image_Rotate90_SwapsDimensions_AndMapsPixelsClockwise()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(@"
            local img = Image.New(2, 1)
            img:SetPixel(0, 0, 1, 2, 3, 255)
            img:SetPixel(1, 0, 4, 5, 6, 255)
            local rot = img:Rotate90()
            local r1,g1,b1,a1 = rot:GetPixel(0, 0)
            local r2,g2,b2,a2 = rot:GetPixel(0, 1)
            return rot:GetWidth()..'x'..rot:GetHeight()..':'..(r1..','..g1..','..b1..','..a1)..'|'..(r2..','..g2..','..b2..','..a2)
        ");
        r.String.ShouldBe("1x2:1,2,3,255|4,5,6,255");
    }

    [Fact]
    public async Task Image_Blur_IsAlphaAware_PreservesColorRatio()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(@"
            local img = Image.New(5, 5)
            img:SetPixel(2, 2, 200, 100, 50, 255)
            img:Blur(1, 1)
            local r,g,b,a = img:GetPixel(2, 2)
            return r..','..g..','..b..','..a
        ");
        r.String.ShouldBe("200,100,50,28");
    }

    [Fact]
    public async Task Image_DropShadow_ProducesLargerOffsetSilhouette()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(@"
            local img = Image.New(2, 2)
            img:FillRect(0, 0, 2, 2, 1, 1, 1, 255)
            local shadow = img:DropShadow(0, 0, 0, 10, 20, 30, 200)
            local r,g,b,a = shadow:GetPixel(2, 2)
            return shadow:GetWidth()..'x'..shadow:GetHeight()..':'..r..','..g..','..b..','..a
        ");
        r.String.ShouldBe("6x6:10,20,30,200");
    }

    [Fact]
    public async Task Image_DropShadow_LargeOffset_DoesNotClip()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(@"
            local img = Image.New(4, 4)
            img:FillRect(0, 0, 4, 4, 1, 1, 1, 255)
            -- No blur here: with blurRadius=0 the silhouette is an exact copy,
            -- so a clipped placement (the old margin = blurRadius+2 formula,
            -- which ignored offsetX/offsetY) is distinguishable from a correct
            -- one by an exact alpha value, not just 'something nonzero'.
            local shadow = img:DropShadow(44, 22, 0, 0, 0, 0, 255)
            local margin = 0 + 2 + 44
            local r,g,b,a = shadow:GetPixel(margin + 44, margin + 22)
            return shadow:GetWidth()..'x'..shadow:GetHeight()..':'..a
        ");
        r.String.ShouldBe("96x96:255");
    }

    [Fact]
    public async Task Image_Invert_FlipsRGB_LeavesAlpha()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(@"
            local img = Image.New(1, 1)
            img:SetPixel(0, 0, 10, 20, 30, 255)
            img:Invert()
            local r,g,b,a = img:GetPixel(0, 0)
            return r..','..g..','..b..','..a
        ");
        r.String.ShouldBe("245,235,225,255");
    }

    [Fact]
    public async Task Image_Grayscale_ReducesToLuminance()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(@"
            local img = Image.New(1, 1)
            img:SetPixel(0, 0, 255, 0, 0, 255)
            img:Grayscale()
            local r,g,b,a = img:GetPixel(0, 0)
            return r..','..g..','..b..','..a
        ");
        r.String.ShouldBe("76,76,76,255");
    }

    [Fact]
    public async Task Image_AdjustBrightnessContrast_AppliesAdditiveBrightness()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(@"
            local img = Image.New(1, 1)
            img:SetPixel(0, 0, 100, 100, 100, 255)
            img:AdjustBrightnessContrast(50, 0)
            local r,g,b,a = img:GetPixel(0, 0)
            return r..','..g..','..b..','..a
        ");
        r.String.ShouldBe("150,150,150,255");
    }

    [Fact]
    public async Task Image_Threshold_SplitsIntoTwoTones()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(@"
            local img = Image.New(2, 1)
            img:SetPixel(0, 0, 200, 200, 200, 255)
            img:SetPixel(1, 0, 10, 10, 10, 255)
            img:Threshold(150, 255, 0, 0, 255)
            local r1,g1,b1,a1 = img:GetPixel(0, 0)
            local r2,g2,b2,a2 = img:GetPixel(1, 0)
            return (r1..','..g1..','..b1..','..a1)..'|'..(r2..','..g2..','..b2..','..a2)
        ");
        r.String.ShouldBe("255,0,0,255|0,0,0,0");
    }

    [Fact]
    public async Task Image_ApplyMask_ScalesAlphaByMaskAlpha()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(@"
            local img = Image.New(2, 1)
            img:FillRect(0, 0, 2, 1, 100, 150, 200, 255)
            local mask = Image.New(2, 1)
            mask:SetPixel(0, 0, 0, 0, 0, 255)
            mask:SetPixel(1, 0, 0, 0, 0, 0)
            img:ApplyMask(mask)
            local a1 = select(4, img:GetPixel(0, 0))
            local a2 = select(4, img:GetPixel(1, 0))
            return a1..','..a2
        ");
        r.String.ShouldBe("255,0");
    }

    [Fact]
    public async Task Image_Noise_IsDeterministicForSameSeed()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(@"
            local img1 = Image.New(3, 3)
            img1:FillRect(0, 0, 3, 3, 128, 128, 128, 255)
            img1:Noise(30, 42)
            local img2 = Image.New(3, 3)
            img2:FillRect(0, 0, 3, 3, 128, 128, 128, 255)
            img2:Noise(30, 42)
            local r1,g1,b1 = img1:GetPixel(1, 1)
            local r2,g2,b2 = img2:GetPixel(1, 1)
            return tostring(r1 == r2 and g1 == g2 and b1 == b2)
        ");
        r.String.ShouldBe("true");
    }

    [Fact]
    public async Task Image_FillGradientLinear_InterpolatesAlongAxis()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(@"
            local img = Image.New(2, 1)
            img:FillGradientLinear(0, 0, 0, 0, 0, 255, 2, 0, 200, 0, 0, 255)
            local r1 = select(1, img:GetPixel(0, 0))
            local r2 = select(1, img:GetPixel(1, 0))
            return r1..','..r2
        ");
        r.String.ShouldBe("50,150");
    }

    [Fact]
    public async Task Image_FillGradientRadial_MatchesInnerColorAtCenter()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(@"
            local img = Image.New(3, 3)
            img:FillGradientRadial(1.5, 1.5, 2, 255, 0, 0, 255, 0, 0, 255, 255)
            local r,g,b,a = img:GetPixel(1, 1)
            return r..','..g..','..b..','..a
        ");
        r.String.ShouldBe("255,0,0,255");
    }

    [Fact]
    public async Task Image_FillPolygon_FillsInsideLeavesOutsideUntouched()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(@"
            local img = Image.New(10, 10)
            img:FillPolygon({ {1, 1}, {8, 1}, {1, 8} }, 0, 255, 0, 255)
            local r1,g1,b1,a1 = img:GetPixel(2, 2)
            local r2,g2,b2,a2 = img:GetPixel(8, 8)
            return (r1..','..g1..','..b1..','..a1)..'|'..(r2..','..g2..','..b2..','..a2)
        ");
        r.String.ShouldBe("0,255,0,255|0,0,0,0");
    }

    [Fact]
    public async Task Image_DrawText_RendersDigitGlyph()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(@"
            local img = Image.New(4, 5)
            img:DrawText('1', 0, 0, 255, 255, 255, 255)
            local a00 = select(4, img:GetPixel(0, 0))
            local a10 = select(4, img:GetPixel(1, 0))
            local a04 = select(4, img:GetPixel(0, 4))
            local a24 = select(4, img:GetPixel(2, 4))
            return a00..','..a10..','..a04..','..a24
        ");
        r.String.ShouldBe("0,255,255,255");
    }

    [Fact]
    public async Task Image_ExtractPalette_RanksMostFrequentColorFirst()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(@"
            local img = Image.New(2, 3)
            img:SetPixel(0, 0, 255, 0, 0, 255)
            img:SetPixel(1, 0, 255, 0, 0, 255)
            img:SetPixel(0, 1, 255, 0, 0, 255)
            img:SetPixel(1, 1, 255, 0, 0, 255)
            img:SetPixel(0, 2, 0, 0, 255, 255)
            local palette = img:ExtractPalette(2)
            return palette[1].r..','..palette[1].g..','..palette[1].b..','..palette[1].count
        ");
        r.String.ShouldBe("255,0,0,4");
    }
}
