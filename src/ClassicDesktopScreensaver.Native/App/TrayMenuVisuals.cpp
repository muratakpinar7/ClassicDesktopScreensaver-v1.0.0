#include "TrayMenuVisuals.h"

#include <ObjIdl.h>
#include <Gdiplus.h>

#include <algorithm>
#include <cstring>
#include <iterator>

#pragma comment(lib, "Gdiplus.lib")

namespace classic_desktop_screensaver
{
    namespace
    {
        using namespace Gdiplus;

        constexpr std::size_t GlyphIndex(TrayMenuGlyph glyph) noexcept
        {
            return static_cast<std::size_t>(glyph);
        }

        void DrawGlyph(Graphics& graphics, TrayMenuGlyph glyph)
        {
            switch (glyph)
            {
            case TrayMenuGlyph::Settings:
            {
                SolidBrush blue(Color(255, 45, 119, 208));
                SolidBrush hole(Color(255, 255, 255, 255));
                for (int index = 0; index < 8; ++index)
                {
                    const GraphicsState state = graphics.Save();
                    graphics.TranslateTransform(12.0f, 12.0f);
                    graphics.RotateTransform(static_cast<REAL>(index * 45));
                    graphics.FillRectangle(&blue, -2.3f, -10.6f, 4.6f, 6.3f);
                    graphics.Restore(state);
                }
                graphics.FillEllipse(&blue, 5.2f, 5.2f, 13.6f, 13.6f);
                graphics.FillEllipse(&hole, 9.0f, 9.0f, 6.0f, 6.0f);
                break;
            }
            case TrayMenuGlyph::Start:
            {
                Pen frame(Color(255, 60, 99, 157), 2.1f);
                SolidBrush screen(Color(255, 232, 244, 255));
                SolidBrush play(Color(255, 23, 160, 100));
                Pen stem(Color(255, 60, 99, 157), 2.0f);
                const PointF playPoints[] = {
                    { 9.4f, 6.4f }, { 9.4f, 14.5f }, { 16.4f, 10.5f }
                };
                graphics.FillRectangle(&screen, 2.6f, 3.5f, 18.8f, 13.5f);
                graphics.DrawRectangle(&frame, 2.6f, 3.5f, 18.8f, 13.5f);
                graphics.FillPolygon(&play, playPoints, static_cast<INT>(std::size(playPoints)));
                graphics.DrawLine(&stem, 12.0f, 17.5f, 12.0f, 20.6f);
                graphics.DrawLine(&stem, 7.5f, 20.8f, 16.5f, 20.8f);
                break;
            }
            case TrayMenuGlyph::Enabled:
            {
                SolidBrush green(Color(255, 34, 172, 90));
                Pen white(Color(255, 255, 255, 255), 2.8f);
                white.SetStartCap(LineCapRound);
                white.SetEndCap(LineCapRound);
                white.SetLineJoin(LineJoinRound);
                const PointF points[] = {
                    { 6.8f, 12.1f }, { 10.5f, 15.9f }, { 17.8f, 8.0f }
                };
                graphics.FillEllipse(&green, 2.0f, 2.0f, 20.0f, 20.0f);
                graphics.DrawLines(&white, points, static_cast<INT>(std::size(points)));
                break;
            }
            case TrayMenuGlyph::Disabled:
            {
                SolidBrush red(Color(255, 221, 59, 60));
                Pen white(Color(255, 255, 255, 255), 2.7f);
                white.SetStartCap(LineCapRound);
                white.SetEndCap(LineCapRound);
                graphics.FillEllipse(&red, 2.0f, 2.0f, 20.0f, 20.0f);
                graphics.DrawLine(&white, 8.0f, 8.0f, 16.0f, 16.0f);
                graphics.DrawLine(&white, 16.0f, 8.0f, 8.0f, 16.0f);
                break;
            }
            case TrayMenuGlyph::Exit:
            {
                Pen door(Color(255, 78, 93, 118), 2.0f);
                Pen arrow(Color(255, 217, 91, 55), 2.3f);
                arrow.SetStartCap(LineCapRound);
                arrow.SetEndCap(LineCapRound);
                arrow.SetLineJoin(LineJoinRound);
                const PointF doorPoints[] = {
                    { 12.6f, 4.0f }, { 4.5f, 4.0f }, { 4.5f, 20.0f }, { 12.6f, 20.0f }
                };
                const PointF arrowPoints[] = {
                    { 16.5f, 7.5f }, { 21.0f, 12.0f }, { 16.5f, 16.5f }
                };
                graphics.DrawLines(&door, doorPoints, static_cast<INT>(std::size(doorPoints)));
                graphics.DrawLine(&arrow, 8.0f, 12.0f, 21.0f, 12.0f);
                graphics.DrawLines(&arrow, arrowPoints, static_cast<INT>(std::size(arrowPoints)));
                break;
            }
            }
        }

        HBITMAP CreateGlyphBitmap(TrayMenuGlyph glyph, UINT size) noexcept
        {
            Bitmap source(size, size, PixelFormat32bppPARGB);
            if (source.GetLastStatus() != Ok) return nullptr;

            {
                Graphics graphics(&source);
                if (graphics.GetLastStatus() != Ok) return nullptr;
                graphics.SetSmoothingMode(SmoothingModeAntiAlias);
                graphics.SetPixelOffsetMode(PixelOffsetModeHighQuality);
                graphics.SetCompositingMode(CompositingModeSourceCopy);
                graphics.Clear(Color(0, 0, 0, 0));
                graphics.SetCompositingMode(CompositingModeSourceOver);
                graphics.ScaleTransform(static_cast<REAL>(size) / 24.0f,
                    static_cast<REAL>(size) / 24.0f);
                DrawGlyph(graphics, glyph);
            }

            Rect rectangle(0, 0, static_cast<INT>(size), static_cast<INT>(size));
            BitmapData pixels{};
            if (source.LockBits(&rectangle, ImageLockModeRead, PixelFormat32bppPARGB, &pixels) != Ok)
                return nullptr;

            BITMAPINFO bitmapInfo{};
            bitmapInfo.bmiHeader.biSize = sizeof(bitmapInfo.bmiHeader);
            bitmapInfo.bmiHeader.biWidth = static_cast<LONG>(size);
            bitmapInfo.bmiHeader.biHeight = -static_cast<LONG>(size);
            bitmapInfo.bmiHeader.biPlanes = 1;
            bitmapInfo.bmiHeader.biBitCount = 32;
            bitmapInfo.bmiHeader.biCompression = BI_RGB;

            HDC desktop = GetDC(nullptr);
            void* destinationPixels = nullptr;
            HBITMAP bitmap = desktop == nullptr ? nullptr : static_cast<HBITMAP>(CreateDIBSection(
                desktop, &bitmapInfo, DIB_RGB_COLORS, &destinationPixels, nullptr, 0));
            if (desktop != nullptr) ReleaseDC(nullptr, desktop);

            if (bitmap != nullptr && destinationPixels != nullptr)
            {
                const auto* sourcePixels = static_cast<const std::uint8_t*>(pixels.Scan0);
                auto* destination = static_cast<std::uint8_t*>(destinationPixels);
                const std::size_t rowBytes = static_cast<std::size_t>(size) * 4;
                const std::ptrdiff_t stride = pixels.Stride;
                for (UINT row = 0; row < size; ++row)
                {
                    const std::ptrdiff_t sourceRow = stride >= 0
                        ? static_cast<std::ptrdiff_t>(row) * stride
                        : static_cast<std::ptrdiff_t>(size - 1 - row) * -stride;
                    std::memcpy(destination + static_cast<std::size_t>(row) * rowBytes,
                        sourcePixels + sourceRow, rowBytes);
                }
            }
            else if (bitmap != nullptr)
            {
                DeleteObject(bitmap);
                bitmap = nullptr;
            }

            source.UnlockBits(&pixels);
            return bitmap;
        }
    }

    TrayMenuImages::TrayMenuImages(UINT dpi) noexcept
    {
        GdiplusStartupInput startupInput{};
        if (GdiplusStartup(&gdiplusToken_, &startupInput, nullptr) != Ok)
        {
            gdiplusToken_ = 0;
            return;
        }

        const UINT effectiveDpi = dpi == 0 ? 96u : dpi;
        size_ = static_cast<UINT>(std::max(16, MulDiv(24, static_cast<int>(effectiveDpi), 96)));
        for (std::size_t index = 0; index < bitmaps_.size(); ++index)
        {
            bitmaps_[index] = CreateGlyphBitmap(static_cast<TrayMenuGlyph>(index), size_);
        }
    }

    TrayMenuImages::~TrayMenuImages()
    {
        for (HBITMAP bitmap : bitmaps_)
        {
            if (bitmap != nullptr) DeleteObject(bitmap);
        }
        if (gdiplusToken_ != 0) GdiplusShutdown(gdiplusToken_);
    }

    HBITMAP TrayMenuImages::Get(TrayMenuGlyph glyph) const noexcept
    {
        const std::size_t index = GlyphIndex(glyph);
        return index < bitmaps_.size() ? bitmaps_[index] : nullptr;
    }

    bool TrayMenuImages::IsAvailable() const noexcept
    {
        return gdiplusToken_ != 0 &&
            std::all_of(bitmaps_.begin(), bitmaps_.end(), [](HBITMAP bitmap)
            {
                return bitmap != nullptr;
            });
    }
}
