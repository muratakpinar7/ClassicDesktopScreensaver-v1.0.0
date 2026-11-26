#include "..\..\src\ClassicDesktopScreensaver.Native\Core\PowerCapabilities.h"
#include "..\..\src\ClassicDesktopScreensaver.Native\Core\HotkeyGesture.h"
#include "..\..\src\ClassicDesktopScreensaver.Native\Core\LaunchBehavior.h"
#include "..\..\src\ClassicDesktopScreensaver.Native\Core\PowerService.h"
#include "..\..\src\ClassicDesktopScreensaver.Native\Core\SettingsStore.h"
#include "..\..\src\ClassicDesktopScreensaver.Native\Core\RuntimePolicy.h"
#include "..\..\src\ClassicDesktopScreensaver.Native\Core\TrayCallback.h"
#include "..\..\src\ClassicDesktopScreensaver.Native\Core\UiTestPolicy.h"
#include "..\..\src\ClassicDesktopScreensaver.Native\Core\ScreenSaverActivationLease.h"
#include "..\..\src\ClassicDesktopScreensaver.Native\Core\BrowserVideoGuard.h"
#include "..\..\src\ClassicDesktopScreensaver.Native\App\SettingsWindow.h"
#include "..\..\src\ClassicDesktopScreensaver.Native\App\GuardService.h"
#include "..\..\src\ClassicDesktopScreensaver.Native\App\Localization.h"
#include "..\..\src\ClassicDesktopScreensaver.Native\App\SettingsLayout.h"
#include "..\..\src\ClassicDesktopScreensaver.Native\App\TrayMenuModel.h"
#include "..\..\src\ClassicDesktopScreensaver.Native\App\TrayMenuPlacement.h"
#include "..\..\src\ClassicDesktopScreensaver.Native\App\TrayMenuVisuals.h"
#include "..\..\src\ClassicDesktopScreensaver.Native\App\UiDiagnosticLog.h"
#include "..\..\src\ClassicDesktopScreensaver.Native\App\ScreenSaverService.h"

#include <Windows.h>
#include <CommCtrl.h>
#include <Pdh.h>
#include <PdhMsg.h>

#include <array>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <limits>
#include <numeric>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

using classic_desktop_screensaver::EvaluateHibernateAvailability;
using classic_desktop_screensaver::QueryHibernateAvailability;
using classic_desktop_screensaver::HibernateAvailability;
using classic_desktop_screensaver::HibernateCapabilities;
using classic_desktop_screensaver::AppLanguage;
using classic_desktop_screensaver::AppSettings;
using classic_desktop_screensaver::HotCorner;
using classic_desktop_screensaver::HotkeyGesture;
using classic_desktop_screensaver::IPowerApi;
using classic_desktop_screensaver::PowerCapabilitiesSnapshot;
using classic_desktop_screensaver::PowerRequestResult;
using classic_desktop_screensaver::PowerRequestStatus;
using classic_desktop_screensaver::PowerFailureStage;
using classic_desktop_screensaver::ShortcutAction;
using classic_desktop_screensaver::ShortcutAssignment;
using classic_desktop_screensaver::SettingsStore;
using classic_desktop_screensaver::RequestHibernate;
using classic_desktop_screensaver::RequestSleep;
using classic_desktop_screensaver::TryParseHotkey;
using classic_desktop_screensaver::FormatHotkey;
using classic_desktop_screensaver::HasDuplicateShortcuts;
using classic_desktop_screensaver::ModifierControl;
using classic_desktop_screensaver::ModifierShift;
using classic_desktop_screensaver::ShouldPassBarePhysicalPauseThroughTerminal;
using classic_desktop_screensaver::ShouldShowSettingsAtLaunch;
using classic_desktop_screensaver::TrayNotification;
using classic_desktop_screensaver::TryDecodeTrayCallback;
using classic_desktop_screensaver::IsTrayContextMenuEvent;
using classic_desktop_screensaver::MakeTrayMenuModel;
using classic_desktop_screensaver::MenuSettings;
using classic_desktop_screensaver::MenuStartSaver;
using classic_desktop_screensaver::MenuToggleSaver;
using classic_desktop_screensaver::MenuExit;
using classic_desktop_screensaver::ShouldRetryTrayRegistration;
using classic_desktop_screensaver::TrayMenuGlyph;
using classic_desktop_screensaver::TrayMenuImages;
using classic_desktop_screensaver::TrayRegistrationRetryDelayMs;
using classic_desktop_screensaver::TrayRegistrationRetryInitialIntervalMs;
using classic_desktop_screensaver::TrayRegistrationRetryMaximumIntervalMs;
using classic_desktop_screensaver::TrayMenuPlacement;
using classic_desktop_screensaver::CalculateTrayMenuPlacement;
using classic_desktop_screensaver::LogUiDiagnostic;
using classic_desktop_screensaver::LogPowerRequestDiagnostic;
using classic_desktop_screensaver::SetUiDiagnosticLoggingEnabled;
using classic_desktop_screensaver::UiTestPolicy;
using classic_desktop_screensaver::MakeUiTestPolicy;
using classic_desktop_screensaver::ScreenSaverActivationLease;
using classic_desktop_screensaver::IScreenSaverActivationApi;
using classic_desktop_screensaver::BrowserVideoStatusMessage;
using classic_desktop_screensaver::BrowserVideoProtocolVersion;
using classic_desktop_screensaver::BrowserVideoMonitoringFlag;
using classic_desktop_screensaver::IsValidBrowserVideoStatus;
using classic_desktop_screensaver::IsBrowserVideoReportActive;
using classic_desktop_screensaver::CreateSettingsWindow;
using classic_desktop_screensaver::ShowSettingsWindow;
using classic_desktop_screensaver::CalculateLayoutCellWidths;
using classic_desktop_screensaver::LayoutCellMode;
using classic_desktop_screensaver::LayoutCellWidthRequest;
using classic_desktop_screensaver::SettingsControlId;
using classic_desktop_screensaver::SettingsHost;
using classic_desktop_screensaver::RuntimeGuardSnapshot;
using classic_desktop_screensaver::IsAutoScreenSaverDue;
using classic_desktop_screensaver::IsAutoLockDue;
using classic_desktop_screensaver::IsScreenSaverBlocked;
using classic_desktop_screensaver::IsAutomaticLockBlocked;
using classic_desktop_screensaver::IsPointInHotCorner;
using classic_desktop_screensaver::IsHotCornerDelayDue;
using classic_desktop_screensaver::IsBlockingMediaPlayback;
using classic_desktop_screensaver::IsGuardQueryResultCurrent;
using classic_desktop_screensaver::NextGuardQueryRevision;
using classic_desktop_screensaver::MediaQueryState;
using classic_desktop_screensaver::QueryGuards;
using classic_desktop_screensaver::EffectiveIdleMilliseconds;
using classic_desktop_screensaver::IsIdleTimerGap;
using classic_desktop_screensaver::ScreenSaverService;
using classic_desktop_screensaver::ScreenSaverPathForLaunch;

namespace
{
    unsigned int failures = 0;

    struct SettingsUiFixture
    {
        AppSettings settings{};
        unsigned int updateCalls{};
        unsigned int keyboardSuspendedCalls{};
        unsigned int saverCalls{};
        unsigned int hibernateRefreshCalls{};
        HibernateAvailability hibernateAvailability{ HibernateAvailability::Unsupported };
    };

    struct SettingsMessageLoopFixture
    {
        SettingsUiFixture settings{};
        SettingsHost host{};
        std::atomic<HWND> window{};
        HANDLE readyEvent{};
    };

    AppSettings UiGetSettings(void* context)
    {
        return static_cast<SettingsUiFixture*>(context)->settings;
    }

    bool UiUpdateSettings(void* context, const AppSettings& settings, std::wstring& error)
    {
        auto* fixture = static_cast<SettingsUiFixture*>(context);
        fixture->settings = settings;
        ++fixture->updateCalls;
        error.clear();
        return true;
    }

    HibernateAvailability UiRefreshHibernate(void* context)
    {
        auto* fixture = static_cast<SettingsUiFixture*>(context);
        ++fixture->hibernateRefreshCalls;
        return fixture->hibernateAvailability;
    }

    std::wstring UiWindowsSaverPath(void*)
    {
        return {};
    }

    bool UiStartSaver(void* context, const std::wstring&, std::wstring& error)
    {
        ++static_cast<SettingsUiFixture*>(context)->saverCalls;
        error.clear();
        return true;
    }

    void UiOpenSaverSettings(void*) {}
    void UiKeyboardSuspended(void* context, bool);

    void PumpUiMessagesFor(std::chrono::milliseconds duration)
    {
        const auto deadline = std::chrono::steady_clock::now() + duration;
        do
        {
            MSG message{};
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
            {
                if (message.message == WM_QUIT)
                {
                    PostQuitMessage(static_cast<int>(message.wParam));
                    return;
                }
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        } while (std::chrono::steady_clock::now() < deadline);
    }

    DWORD WINAPI SettingsMessageLoopThread(void* value)
    {
        auto* fixture = static_cast<SettingsMessageLoopFixture*>(value);
        if (fixture == nullptr) return 1;
        INITCOMMONCONTROLSEX controls{ sizeof(controls) };
        controls.dwICC = ICC_WIN95_CLASSES;
        const bool controlsReady = InitCommonControlsEx(&controls) != FALSE;
        if (controlsReady)
        {
            fixture->host = SettingsHost{
                .context = &fixture->settings,
                .getSettings = UiGetSettings,
                .updateSettings = UiUpdateSettings,
                .refreshHibernate = UiRefreshHibernate,
                .windowsSaverPath = UiWindowsSaverPath,
                .startSaver = UiStartSaver,
                .openSaverSettings = UiOpenSaverSettings,
                .keyboardSuspended = UiKeyboardSuspended,
                .readOnly = true
            };
            fixture->settings.settings.language = AppLanguage::Turkish;
            HWND window = CreateSettingsWindow(GetModuleHandleW(nullptr), nullptr, &fixture->host);
            fixture->window.store(window, std::memory_order_release);
            if (window != nullptr) ShowWindow(window, SW_SHOWNA);
        }
        SetEvent(fixture->readyEvent);
        MSG message{};
        while (GetMessageW(&message, nullptr, 0, 0) > 0)
        {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        HWND window = fixture->window.exchange(nullptr, std::memory_order_acq_rel);
        if (window != nullptr && IsWindow(window)) DestroyWindow(window);
        return controlsReady ? 0 : 1;
    }

    void UiKeyboardSuspended(void* context, bool)
    {
        ++static_cast<SettingsUiFixture*>(context)->keyboardSuspendedCalls;
    }

    struct ChildCaptureContext
    {
        HWND topLevel{};
        HDC destination{};
        RECT topLevelBounds{};
        unsigned int captured{};
    };

    BOOL CALLBACK CaptureVisibleChild(HWND child, LPARAM value)
    {
        auto* context = reinterpret_cast<ChildCaptureContext*>(value);
        if (context == nullptr || !IsWindowVisible(child)) return TRUE;
        RECT bounds{};
        if (GetWindowRect(child, &bounds) == FALSE) return TRUE;
        const int width = bounds.right - bounds.left;
        const int height = bounds.bottom - bounds.top;
        if (width <= 0 || height <= 0) return TRUE;

        HDC childDc = CreateCompatibleDC(context->destination);
        HBITMAP childBitmap = CreateCompatibleBitmap(context->destination, width, height);
        if (childDc == nullptr || childBitmap == nullptr)
        {
            if (childBitmap != nullptr) DeleteObject(childBitmap);
            if (childDc != nullptr) DeleteDC(childDc);
            return TRUE;
        }
        const HGDIOBJ previous = SelectObject(childDc, childBitmap);
        RECT localBounds{ 0, 0, width, height };
        FillRect(childDc, &localBounds, GetSysColorBrush(COLOR_BTNFACE));
        bool rendered = PrintWindow(child, childDc, PW_CLIENTONLY) != FALSE;
        if (!rendered)
        {
            SendMessageW(child, WM_PRINT, reinterpret_cast<WPARAM>(childDc),
                PRF_CLIENT | PRF_ERASEBKGND);
            rendered = true;
        }
        if (rendered && BitBlt(context->destination,
            bounds.left - context->topLevelBounds.left,
            bounds.top - context->topLevelBounds.top, width, height,
            childDc, 0, 0, SRCCOPY) != FALSE)
        {
            ++context->captured;
        }
        SelectObject(childDc, previous);
        DeleteObject(childBitmap);
        DeleteDC(childDc);
        return TRUE;
    }

    void PumpPendingWindowMessages()
    {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE) != FALSE)
        {
            if (message.message == WM_QUIT)
            {
                PostQuitMessage(static_cast<int>(message.wParam));
                break;
            }
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }

    bool SaveWindowBitmap(HWND window, const std::filesystem::path& path,
        bool& usedPrintWindow, unsigned int& capturedChildren, bool compositeChildren = true)
    {
        RECT bounds{};
        if (GetWindowRect(window, &bounds) == FALSE) return false;
        const int width = bounds.right - bounds.left;
        const int height = bounds.bottom - bounds.top;
        if (width <= 0 || height <= 0) return false;

        HDC screen = GetDC(nullptr);
        if (screen == nullptr) return false;
        HDC memory = CreateCompatibleDC(screen);
        BITMAPINFO bitmapInfo{};
        bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bitmapInfo.bmiHeader.biWidth = width;
        bitmapInfo.bmiHeader.biHeight = -height;
        bitmapInfo.bmiHeader.biPlanes = 1;
        bitmapInfo.bmiHeader.biBitCount = 32;
        bitmapInfo.bmiHeader.biCompression = BI_RGB;
        void* pixels = nullptr;
        HBITMAP bitmap = CreateDIBSection(screen, &bitmapInfo, DIB_RGB_COLORS,
            &pixels, nullptr, 0);
        if (memory == nullptr || bitmap == nullptr || pixels == nullptr)
        {
            if (bitmap != nullptr) DeleteObject(bitmap);
            if (memory != nullptr) DeleteDC(memory);
            ReleaseDC(nullptr, screen);
            return false;
        }

        const HGDIOBJ previous = SelectObject(memory, bitmap);
        RECT canvas{ 0, 0, width, height };
        FillRect(memory, &canvas, GetSysColorBrush(COLOR_BTNFACE));
        RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
        bool captured = BitBlt(memory, 0, 0, width, height, screen,
                bounds.left, bounds.top, SRCCOPY | CAPTUREBLT) != FALSE;
        if (!captured)
        {
            usedPrintWindow = PrintWindow(window, memory, PW_RENDERFULLCONTENT) != FALSE;
            captured = usedPrintWindow;
        }
        if (compositeChildren)
        {
            ChildCaptureContext childContext{ window, memory, bounds, 0 };
            HWND content = FindWindowExW(window, nullptr,
                L"ClassicDesktopScreensaver.Native.Settings.Content", nullptr);
            HWND surface = content == nullptr ? nullptr : FindWindowExW(content, nullptr,
                L"ClassicDesktopScreensaver.Native.Settings.Surface", nullptr);
            if (surface != nullptr)
                EnumChildWindows(surface, CaptureVisibleChild, reinterpret_cast<LPARAM>(&childContext));
            capturedChildren = childContext.captured;
        }

        const std::size_t pixelBytes = static_cast<std::size_t>(width) *
            static_cast<std::size_t>(height) * 4u;
        auto* pixelData = static_cast<unsigned char*>(pixels);
        const COLORREF buttonFace = GetSysColor(COLOR_BTNFACE);
        for (std::size_t offset = 0; offset < pixelBytes; offset += 4u)
        {
            if (pixelData[offset + 3u] == 0 && pixelData[offset] == 0 &&
                pixelData[offset + 1u] == 0 && pixelData[offset + 2u] == 0)
            {
                pixelData[offset] = GetBValue(buttonFace);
                pixelData[offset + 1u] = GetGValue(buttonFace);
                pixelData[offset + 2u] = GetRValue(buttonFace);
            }
            pixelData[offset + 3u] = 0xFF;
        }
        BITMAPFILEHEADER fileHeader{};
        fileHeader.bfType = 0x4D42;
        fileHeader.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
        fileHeader.bfSize = static_cast<DWORD>(fileHeader.bfOffBits + pixelBytes);
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        const bool saved = captured && output.good();
        if (saved)
        {
            output.write(reinterpret_cast<const char*>(&fileHeader), sizeof(fileHeader));
            output.write(reinterpret_cast<const char*>(&bitmapInfo.bmiHeader), sizeof(bitmapInfo.bmiHeader));
            output.write(static_cast<const char*>(pixels), static_cast<std::streamsize>(pixelBytes));
        }
        const bool writeSucceeded = saved && output.good();
        output.close();
        SelectObject(memory, previous);
        DeleteObject(bitmap);
        DeleteDC(memory);
        ReleaseDC(nullptr, screen);
        return writeSucceeded;
    }

    struct HwndTreeRecord
    {
        HWND window{};
        int controlId{};
        std::wstring className;
        LONG_PTR style{};
        LONG_PTR extendedStyle{};
        RECT screen{};
        RECT parentClient{};
        RECT viewportClient{};
        RECT viewportIntersection{};
        RECT clipBox{};
        int zOrder{-1};
        int clipType{ NULLREGION };
        bool valid{};
        bool childStyle{};
        bool visibleStyle{};
        bool visible{};
        bool enabled{};
        bool correctParent{};
        bool hasBounds{};
        bool intersectsViewport{};
    };

    struct HwndTreeContext
    {
        HWND viewportWindow{};
        HWND surface{};
        RECT viewport{};
        int surfaceOffsetY{};
        int scrollPosition{};
        std::vector<HwndTreeRecord> records;
    };

    BOOL CALLBACK CollectSettingsHwnd(HWND child, LPARAM value)
    {
        auto* context = reinterpret_cast<HwndTreeContext*>(value);
        if (context == nullptr || GetParent(child) != context->surface) return TRUE;

        HwndTreeRecord record{};
        record.window = child;
        record.controlId = GetDlgCtrlID(child);
        record.valid = IsWindow(child) != FALSE;
        record.correctParent = GetParent(child) == context->surface;
        record.style = GetWindowLongPtrW(child, GWL_STYLE);
        record.extendedStyle = GetWindowLongPtrW(child, GWL_EXSTYLE);
        record.childStyle = (record.style & WS_CHILD) != 0;
        record.visibleStyle = (record.style & WS_VISIBLE) != 0;
        record.visible = IsWindowVisible(child) != FALSE;
        record.enabled = IsWindowEnabled(child) != FALSE;
        wchar_t className[128]{};
        const int classLength = GetClassNameW(child, className, static_cast<int>(std::size(className)));
        if (classLength > 0) record.className.assign(className, static_cast<std::size_t>(classLength));

        if (GetWindowRect(child, &record.screen) != FALSE)
        {
            POINT points[2] = { { record.screen.left, record.screen.top },
                { record.screen.right, record.screen.bottom } };
            MapWindowPoints(nullptr, context->surface, points, 2);
            record.parentClient = { points[0].x, points[0].y, points[1].x, points[1].y };
            points[0] = { record.screen.left, record.screen.top };
            points[1] = { record.screen.right, record.screen.bottom };
            MapWindowPoints(nullptr, context->viewportWindow, points, 2);
            record.viewportClient = { points[0].x, points[0].y, points[1].x, points[1].y };
            record.hasBounds = record.parentClient.right > record.parentClient.left &&
                record.parentClient.bottom > record.parentClient.top;
            record.intersectsViewport = IntersectRect(&record.viewportIntersection,
                &record.viewportClient, &context->viewport) != FALSE;
        }
        HDC dc = GetDC(child);
        if (dc != nullptr)
        {
            record.clipType = GetClipBox(dc, &record.clipBox);
            ReleaseDC(child, dc);
        }

        int zOrder = 0;
        for (HWND sibling = GetWindow(context->surface, GW_CHILD);
            sibling != nullptr; sibling = GetWindow(sibling, GW_HWNDNEXT), ++zOrder)
        {
            if (sibling == child)
            {
                record.zOrder = zOrder;
                break;
            }
        }
        context->records.push_back(std::move(record));
        return TRUE;
    }

    bool WriteSettingsHwndTree(HWND window, const std::filesystem::path& path,
        HwndTreeContext* result = nullptr)
    {
        HWND content = FindWindowExW(window, nullptr,
            L"ClassicDesktopScreensaver.Native.Settings.Content", nullptr);
        HWND surface = content == nullptr ? nullptr : FindWindowExW(content, nullptr,
            L"ClassicDesktopScreensaver.Native.Settings.Surface", nullptr);
        if (content == nullptr || surface == nullptr) return false;
        RECT viewport{};
        if (GetClientRect(content, &viewport) == FALSE) return false;

        HwndTreeContext context{};
        context.viewportWindow = content;
        context.surface = surface;
        context.viewport = viewport;
        RECT surfaceRect{};
        GetWindowRect(surface, &surfaceRect);
        POINT surfaceOrigin{ surfaceRect.left, surfaceRect.top };
        MapWindowPoints(nullptr, content, &surfaceOrigin, 1);
        context.surfaceOffsetY = surfaceOrigin.y;
        SCROLLINFO scroll{ sizeof(scroll) };
        scroll.fMask = SIF_POS | SIF_RANGE | SIF_PAGE;
        if (GetScrollInfo(content, SB_VERT, &scroll)) context.scrollPosition = scroll.nPos;
        EnumChildWindows(surface, CollectSettingsHwnd, reinterpret_cast<LPARAM>(&context));
        if (context.records.empty()) return false;

        std::wofstream output(path, std::ios::binary | std::ios::trunc);
        if (!output) return false;
        output << L"window_valid=" << (IsWindow(window) ? 1 : 0)
            << L" window_visible=" << (IsWindowVisible(window) ? 1 : 0)
            << L" content_valid=" << (IsWindow(content) ? 1 : 0)
            << L" content_visible=" << (IsWindowVisible(content) ? 1 : 0)
            << L" content_style=0x" << std::hex << GetWindowLongPtrW(content, GWL_STYLE)
            << L" content_exstyle=0x" << GetWindowLongPtrW(content, GWL_EXSTYLE)
            << L" surface_valid=" << (IsWindow(surface) ? 1 : 0)
            << L" surface_visible=" << (IsWindowVisible(surface) ? 1 : 0)
            << L" surface_style=0x" << GetWindowLongPtrW(surface, GWL_STYLE)
            << L" surface_exstyle=0x" << GetWindowLongPtrW(surface, GWL_EXSTYLE)
            << std::dec << L" viewport=" << viewport.left << L"," << viewport.top << L"," <<
            viewport.right << L"," << viewport.bottom << L" surface_offset_y=" << context.surfaceOffsetY
            << L" scroll_position=" << context.scrollPosition
            << L" child_count=" << context.records.size() << L"\n";

        unsigned int validCount{};
        unsigned int visibleCount{};
        unsigned int enabledCount{};
        unsigned int clippedInViewCount{};
        unsigned int badParentCount{};
        unsigned int badGeometryCount{};
        for (HwndTreeRecord& record : context.records)
        {
            validCount += record.valid ? 1u : 0u;
            visibleCount += record.visibleStyle && record.visible ? 1u : 0u;
            enabledCount += record.enabled ? 1u : 0u;
            if (!record.correctParent) ++badParentCount;
            if (!record.childStyle || !record.hasBounds) ++badGeometryCount;
            const bool fullyInsideViewport = record.viewportClient.left >= viewport.left &&
                record.viewportClient.top >= viewport.top && record.viewportClient.right <= viewport.right &&
                record.viewportClient.bottom <= viewport.bottom;
            if (fullyInsideViewport && record.clipType == NULLREGION)
                ++clippedInViewCount;
        }
        output << L"valid=" << validCount << L" visible=" << visibleCount
            << L" enabled=" << enabledCount << L" empty_clip_in_view=" << clippedInViewCount
            << L" bad_parent=" << badParentCount << L" bad_geometry=" << badGeometryCount << L"\n";
        for (const HwndTreeRecord& record : context.records)
        {
            output << L"id=" << record.controlId << L" hwnd=0x" << std::hex <<
                reinterpret_cast<UINT_PTR>(record.window) << L" style=0x" << record.style <<
                L" exstyle=0x" << record.extendedStyle << std::dec
                << L" class=" << record.className
                << L" parent=0x" << std::hex << reinterpret_cast<UINT_PTR>(GetParent(record.window)) << std::dec
                << L" z=" << record.zOrder << L" valid=" << record.valid
                << L" child=" << record.childStyle << L" ws_visible=" << record.visibleStyle
                << L" visible=" << record.visible << L" enabled=" << record.enabled
                << L" rect_screen=" << record.screen.left << L"," << record.screen.top << L"," <<
                record.screen.right << L"," << record.screen.bottom
                << L" rect_parent=" << record.parentClient.left << L"," << record.parentClient.top << L"," <<
                record.parentClient.right << L"," << record.parentClient.bottom
                << L" rect_viewport=" << record.viewportClient.left << L"," << record.viewportClient.top << L"," <<
                record.viewportClient.right << L"," << record.viewportClient.bottom
                << L" intersects_view=" << record.intersectsViewport
                << L" intersection=" << record.viewportIntersection.left << L"," <<
                record.viewportIntersection.top << L"," << record.viewportIntersection.right << L"," <<
                record.viewportIntersection.bottom
                << L" clip_type=" << record.clipType << L" clip_box=" << record.clipBox.left << L"," <<
                record.clipBox.top << L"," << record.clipBox.right << L"," << record.clipBox.bottom << L"\n";
        }
        output.close();
        if (result != nullptr) *result = std::move(context);
        return output.good();
    }

    int RunSettingsHwndInspection(const std::filesystem::path& outputPath, DWORD holdMilliseconds)
    {
        INITCOMMONCONTROLSEX controls{ sizeof(controls) };
        controls.dwICC = ICC_WIN95_CLASSES;
        if (InitCommonControlsEx(&controls) == FALSE) return EXIT_FAILURE;
        SettingsUiFixture fixture;
        fixture.settings.language = AppLanguage::Turkish;
        SettingsHost host{
            .context = &fixture,
            .getSettings = UiGetSettings,
            .updateSettings = UiUpdateSettings,
            .refreshHibernate = UiRefreshHibernate,
            .windowsSaverPath = UiWindowsSaverPath,
            .startSaver = UiStartSaver,
            .openSaverSettings = UiOpenSaverSettings,
            .keyboardSuspended = UiKeyboardSuspended,
            .readOnly = true
        };
        HWND window = CreateSettingsWindow(GetModuleHandleW(nullptr), nullptr, &host);
        if (window == nullptr) return EXIT_FAILURE;
        ShowSettingsWindow(window);
        SetWindowPos(window, HWND_TOP, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        PumpPendingWindowMessages();
        RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
        UpdateWindow(window);
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        PumpPendingWindowMessages();

        std::filesystem::path treePath = outputPath;
        treePath.replace_extension(L".hwnd.txt");
        const bool treeSaved = WriteSettingsHwndTree(window, treePath);
        bool usedPrintWindow = false;
        unsigned int capturedChildren{};
        const bool screenshotSaved = SaveWindowBitmap(window, outputPath,
            usedPrintWindow, capturedChildren, false);
        HWND content = FindWindowExW(window, nullptr,
            L"ClassicDesktopScreensaver.Native.Settings.Content", nullptr);
        std::filesystem::path bottomPath = outputPath;
        bottomPath.replace_filename(outputPath.stem().wstring() + L"-bottom" + outputPath.extension().wstring());
        std::filesystem::path bottomTreePath = outputPath;
        bottomTreePath.replace_filename(outputPath.stem().wstring() + L"-bottom.hwnd.txt");
        bool bottomTreeSaved = false;
        bool bottomScreenshotSaved = false;
        if (content != nullptr)
        {
            SendMessageW(content, WM_VSCROLL, MAKEWPARAM(SB_BOTTOM, 0), 0);
            PumpPendingWindowMessages();
            RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
            UpdateWindow(window);
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            PumpPendingWindowMessages();
            bottomTreeSaved = WriteSettingsHwndTree(window, bottomTreePath);
            bool bottomUsedPrintWindow = false;
            unsigned int bottomCapturedChildren{};
            bottomScreenshotSaved = SaveWindowBitmap(window, bottomPath,
                bottomUsedPrintWindow, bottomCapturedChildren, false);
            if (holdMilliseconds != 0)
            {
                const ULONGLONG end = GetTickCount64() + holdMilliseconds;
                while (GetTickCount64() < end)
                {
                    PumpPendingWindowMessages();
                    Sleep(25);
                }
            }
        }
        const bool visible = IsWindowVisible(window) != FALSE;
        DestroyWindow(window);
        if (!treeSaved || !screenshotSaved || !bottomTreeSaved || !bottomScreenshotSaved || !visible)
            return EXIT_FAILURE;
        std::wprintf(L"Live HWND inspection saved: %ls, %ls, %ls and %ls (desktop_capture=%ls)\n",
            outputPath.c_str(), treePath.c_str(), bottomPath.c_str(), bottomTreePath.c_str(),
            usedPrintWindow ? L"PrintWindow" : L"BitBlt");
        return EXIT_SUCCESS;
    }

    int RunSettingsLayoutPreview(const std::filesystem::path& outputPath,
        UINT previewDpi, int previewWidthDips)
    {
        INITCOMMONCONTROLSEX controls{ sizeof(controls) };
        controls.dwICC = ICC_WIN95_CLASSES;
        if (InitCommonControlsEx(&controls) == FALSE) return EXIT_FAILURE;

        SettingsUiFixture fixture;
        fixture.settings.language = AppLanguage::Turkish;
        SettingsHost host{
            .context = &fixture,
            .getSettings = UiGetSettings,
            .updateSettings = UiUpdateSettings,
            .refreshHibernate = UiRefreshHibernate,
            .windowsSaverPath = UiWindowsSaverPath,
            .startSaver = UiStartSaver,
            .openSaverSettings = UiOpenSaverSettings,
            .keyboardSuspended = UiKeyboardSuspended,
            .readOnly = true
        };
        HWND window = CreateSettingsWindow(GetModuleHandleW(nullptr), nullptr, &host);
        if (window == nullptr) return EXIT_FAILURE;
        if (previewDpi != 0)
        {
            const auto scale = [previewDpi](int dips)
                { return MulDiv(dips, static_cast<int>(previewDpi), 96); };
            const int heightDips = previewWidthDips < 600 ? 620 : 860;
            RECT suggested{ 40, 40, 40 + scale(previewWidthDips), 40 + scale(heightDips) };
            SendMessageW(window, WM_DPICHANGED, MAKELONG(previewDpi, previewDpi),
                reinterpret_cast<LPARAM>(&suggested));
        }
        ShowSettingsWindow(window);
        SetWindowPos(window, HWND_TOP, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
        bool usedPrintWindow = false;
        unsigned int capturedChildren{};
        const bool topSaved = SaveWindowBitmap(window, outputPath, usedPrintWindow, capturedChildren);
        HWND content = FindWindowExW(window, nullptr,
            L"ClassicDesktopScreensaver.Native.Settings.Content", nullptr);
        std::filesystem::path bottomPath = outputPath;
        bottomPath.replace_filename(outputPath.stem().wstring() + L"-bottom" + outputPath.extension().wstring());
        bool bottomSaved = false;
        if (topSaved && content != nullptr)
        {
            SendMessageW(content, WM_VSCROLL, MAKEWPARAM(SB_BOTTOM, 0), 0);
            RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            bool bottomUsedPrintWindow = false;
            unsigned int bottomCapturedChildren{};
            bottomSaved = SaveWindowBitmap(window, bottomPath, bottomUsedPrintWindow,
                bottomCapturedChildren);
        }
        DestroyWindow(window);
        if (!topSaved || !bottomSaved) return EXIT_FAILURE;
        std::wprintf(L"Settings preview saved: %ls and %ls (%ls base capture, %u child controls)\n",
            outputPath.c_str(), bottomPath.c_str(),
            usedPrintWindow ? L"PrintWindow" : L"screen", capturedChildren);
        return EXIT_SUCCESS;
    }

    BOOL CALLBACK CountChildWindow(HWND, LPARAM value)
    {
        ++*reinterpret_cast<unsigned int*>(value);
        return TRUE;
    }

    BOOL CALLBACK ValidateWindowUpdateRegion(HWND window, LPARAM)
    {
        ValidateRect(window, nullptr);
        return TRUE;
    }

    struct UpdateRegionCount
    {
        unsigned int windows{};
        unsigned int invalid{};
    };

    BOOL CALLBACK CountInvalidWindowUpdateRegions(HWND window, LPARAM value)
    {
        auto* count = reinterpret_cast<UpdateRegionCount*>(value);
        if (count == nullptr) return FALSE;
        ++count->windows;
        RECT update{};
        if (GetUpdateRect(window, &update, FALSE) != FALSE) ++count->invalid;
        return TRUE;
    }

    struct TestChildRectangle
    {
        HWND window{};
        RECT rectangle{};
        bool group{};
    };

    void Expect(bool passed, const char* name);

    BOOL CALLBACK CollectSettingsRectangles(HWND window, LPARAM value)
    {
        auto* children = reinterpret_cast<std::vector<TestChildRectangle>*>(value);
        if (children == nullptr || GetParent(window) == nullptr) return TRUE;
        LONG_PTR style = GetWindowLongPtrW(window, GWL_STYLE);
        const bool group = (style & BS_TYPEMASK) == BS_GROUPBOX;
        RECT bounds{};
        if (GetWindowRect(window, &bounds) == FALSE) return TRUE;
        POINT points[2] = { { bounds.left, bounds.top }, { bounds.right, bounds.bottom } };
        HWND parent = GetParent(window);
        MapWindowPoints(nullptr, parent, points, 2);
        children->push_back({ window, RECT{ points[0].x, points[0].y, points[1].x, points[1].y }, group });
        return TRUE;
    }

    bool GetWindowRectRelativeTo(HWND window, HWND coordinateWindow, RECT& rectangle)
    {
        if (window == nullptr || coordinateWindow == nullptr ||
            GetWindowRect(window, &rectangle) == FALSE) return false;
        POINT points[2] = { { rectangle.left, rectangle.top },
            { rectangle.right, rectangle.bottom } };
        SetLastError(ERROR_SUCCESS);
        if (MapWindowPoints(nullptr, coordinateWindow, points, 2) == 0 &&
            GetLastError() != ERROR_SUCCESS) return false;
        rectangle = { points[0].x, points[0].y, points[1].x, points[1].y };
        return true;
    }

    bool SettingsGeometryIsValid(HWND settingsWindow)
    {
        HWND content = FindWindowExW(settingsWindow, nullptr,
            L"ClassicDesktopScreensaver.Native.Settings.Content", nullptr);
        HWND surface = content == nullptr ? nullptr : FindWindowExW(content, nullptr,
            L"ClassicDesktopScreensaver.Native.Settings.Surface", nullptr);
        if (content == nullptr || surface == nullptr) return false;
        RECT client{};
        if (GetClientRect(surface, &client) == FALSE) return false;
        std::vector<TestChildRectangle> children;
        EnumChildWindows(surface, CollectSettingsRectangles, reinterpret_cast<LPARAM>(&children));
        if (children.size() < 45) return false;

        std::vector<RECT> groups;
        std::vector<RECT> controls;
        for (const TestChildRectangle& child : children)
        {
            const RECT& rect = child.rectangle;
            if (rect.right <= rect.left || rect.bottom <= rect.top || rect.left < -1 ||
                rect.right > client.right + 1)
            {
                return false;
            }
            if (child.group) groups.push_back(rect);
            else controls.push_back(rect);
        }
        if (groups.size() != 6) return false;
        std::sort(groups.begin(), groups.end(), [](const RECT& left, const RECT& right)
            { return left.top < right.top; });
        for (std::size_t i = 0; i < groups.size(); ++i)
        {
            for (std::size_t j = i + 1; j < groups.size(); ++j)
            {
                if (groups[i].top < groups[j].bottom && groups[j].top < groups[i].bottom)
                    return false;
            }
        }
        for (const RECT& control : controls)
        {
            if (control.top < groups.front().top || control.bottom > groups.back().bottom) continue;
            const bool insideSection = std::any_of(groups.begin(), groups.end(), [&control](const RECT& group)
                {
                    return control.left >= group.left && control.right <= group.right &&
                        control.top >= group.top && control.bottom <= group.bottom;
                });
            if (!insideSection) return false;
        }
        for (std::size_t i = 0; i < controls.size(); ++i)
        {
            for (std::size_t j = i + 1; j < controls.size(); ++j)
            {
                if (controls[i].left < controls[j].right && controls[j].left < controls[i].right &&
                    controls[i].top < controls[j].bottom && controls[j].top < controls[i].bottom)
                    return false;
            }
        }
        return true;
    }

    bool WindowFitsMonitorWorkArea(HWND window)
    {
        RECT windowBounds{};
        if (GetWindowRect(window, &windowBounds) == FALSE) return false;
        const HMONITOR monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
        MONITORINFO info{ sizeof(info) };
        if (GetMonitorInfoW(monitor, &info) == FALSE) return false;
        return windowBounds.left >= info.rcWork.left && windowBounds.top >= info.rcWork.top &&
            windowBounds.right <= info.rcWork.right && windowBounds.bottom <= info.rcWork.bottom;
    }

    bool ResizeSettingsContentToClient(HWND settingsWindow, HWND content)
    {
        RECT client{};
        if (GetClientRect(settingsWindow, &client) == FALSE) return false;
        return SetWindowPos(content, nullptr, 0, 0, client.right, client.bottom,
            SWP_NOACTIVATE | SWP_NOZORDER) != FALSE;
    }

    bool SettingsTabSequenceExists(HWND settingsWindow, std::size_t expectedStops)
    {
        HWND current = GetNextDlgTabItem(settingsWindow, nullptr, FALSE);
        if (current == nullptr) return false;
        std::vector<HWND> visited;
        while (current != nullptr && visited.size() <= expectedStops + 2)
        {
            if ((GetWindowLongPtrW(current, GWL_STYLE) & WS_TABSTOP) == 0) return false;
            if (std::find(visited.begin(), visited.end(), current) != visited.end()) break;
            visited.push_back(current);
            current = GetNextDlgTabItem(settingsWindow, current, FALSE);
        }
        return visited.size() >= expectedStops;
    }

    void TestAdaptiveLayoutWidthCalculation()
    {
        const std::array<LayoutCellWidthRequest, 3> cells = {{
            { 220, 120, LayoutCellMode::Flexible },
            { 36, 36, LayoutCellMode::Fixed },
            { 150, 150, LayoutCellMode::Fill }
        }};
        const auto wide = CalculateLayoutCellWidths(600, 8, cells);
        const int wideTotal = std::accumulate(wide.widths.begin(), wide.widths.end(), 16);
        Expect(!wide.stacked && wide.widths.size() == cells.size() && wideTotal == 600,
            "adaptive layout distributes available width without overflow");

        const auto constrained = CalculateLayoutCellWidths(380, 8, cells);
        const int constrainedTotal = std::accumulate(constrained.widths.begin(), constrained.widths.end(), 16);
        Expect(!constrained.stacked && constrainedTotal <= 380 && constrained.widths[0] >= 120,
            "adaptive layout shrinks flexible cells to their minimum before wrapping");

        const auto narrow = CalculateLayoutCellWidths(300, 8, cells);
        Expect(narrow.stacked && narrow.widths.size() == cells.size(),
            "adaptive layout stacks cells when minimum widths cannot fit");
    }

    class FakePowerApi final : public IPowerApi
    {
    public:
        PowerCapabilitiesSnapshot capabilities{};
        bool displayOffAccepted{ true };
        bool suspendAccepted{ true };
        std::uint32_t displayOffErrorCode{ 5 };
        std::uint32_t suspendErrorCode{ 5 };
        PowerFailureStage suspendFailureStage{ PowerFailureStage::SetSuspendState };
        unsigned int queryCalls{};
        unsigned int displayOffCalls{};
        unsigned int sleepCalls{};
        unsigned int hibernateCalls{};

        PowerCapabilitiesSnapshot QueryCapabilities() noexcept override
        {
            ++queryCalls;
            return capabilities;
        }

        bool RequestDisplayOff(std::uint32_t& errorCode,
            PowerFailureStage& failureStage) noexcept override
        {
            ++displayOffCalls;
            errorCode = displayOffAccepted ? 0 : displayOffErrorCode;
            failureStage = displayOffAccepted
                ? PowerFailureStage::None : PowerFailureStage::DisplayOffRequest;
            return displayOffAccepted;
        }

        bool RequestSuspend(bool hibernate, std::uint32_t& errorCode,
            PowerFailureStage& failureStage) noexcept override
        {
            hibernate ? ++hibernateCalls : ++sleepCalls;
            errorCode = suspendAccepted ? 0 : suspendErrorCode;
            failureStage = suspendAccepted ? PowerFailureStage::None : suspendFailureStage;
            return suspendAccepted;
        }
    };

    void Expect(
        const char* name,
        HibernateAvailability actual,
        HibernateAvailability expected)
    {
        if (actual == expected)
        {
            std::printf("PASS: %s\n", name);
            return;
        }

        std::printf("FAIL: %s\n", name);
        ++failures;
    }

    void Expect(bool passed, const char* name)
    {
        if (passed)
        {
            std::printf("PASS: %s\n", name);
            return;
        }

        std::printf("FAIL: %s\n", name);
        ++failures;
    }

    std::filesystem::path MakeTemporaryIniPath()
    {
        wchar_t directory[MAX_PATH]{};
        wchar_t fileName[MAX_PATH]{};
        const DWORD length = GetTempPathW(MAX_PATH, directory);
        if (length == 0 || length >= MAX_PATH ||
            GetTempFileNameW(directory, L"CDS", 0, fileName) == 0)
        {
            return {};
        }

        DeleteFileW(fileName);
        std::filesystem::path path(fileName);
        path += L".ini";
        return path;
    }

    bool WriteUtf8(const std::filesystem::path& path, const std::wstring& text)
    {
        const int required = WideCharToMultiByte(
            CP_UTF8, WC_ERR_INVALID_CHARS, text.data(),
            static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
        if (required <= 0)
        {
            return false;
        }

        std::string bytes(static_cast<std::size_t>(required), '\0');
        if (WideCharToMultiByte(
            CP_UTF8, WC_ERR_INVALID_CHARS, text.data(),
            static_cast<int>(text.size()), bytes.data(), required,
            nullptr, nullptr) != required)
        {
            return false;
        }

        HANDLE file = CreateFileW(
            path.c_str(), GENERIC_WRITE, 0, nullptr,
            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
        {
            return false;
        }

        DWORD written = 0;
        const bool success = WriteFile(
            file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) &&
            written == bytes.size();
        CloseHandle(file);
        return success;
    }

    void TestSettingsStore()
    {
        const auto path = MakeTemporaryIniPath();
        if (path.empty())
        {
            std::printf("FAIL: create temporary settings path\n");
            ++failures;
            return;
        }

        SettingsStore store(path);
        AppSettings defaults;
        std::wstring error;
        const bool loadedMissing = store.Load(defaults, error);
        Expect(
            loadedMissing && defaults.autoScreenSaverEnabled &&
                defaults.screenSaverIdleMinutes == 2 &&
                !defaults.sleepHotkeyEnabled &&
                defaults.hibernateHotkey == L"Shift+Pause",
            "missing settings file loads defaults");

        const std::wstring legacy =
            L"; legacy-compatible file\r\n"
            L"[GENERAL]\r\n"
            L"Language=Turkish\r\n"
            L"AutoScreenSaverEnabled=false\r\n"
            L"ScreenSaverIdleMinutes=9999\r\n"
            L"ScreenSaverPath=C:\\Ekran\\görüntü.scr\r\n"
            L"UnknownFutureOption=ignored\r\n"
            L"[hotcorner]\r\n"
            L"DelayMs=-5\r\n"
            L"Corner=BottomLeft\r\n"
            L"[HOTKEYS]\r\n"
            L"SleepEnabled=true\r\n"
            L"Sleep=Ctrl+Pause\r\n";

        const bool wroteLegacy = WriteUtf8(path, legacy);
        AppSettings parsed;
        const bool loadedLegacy = wroteLegacy && store.Load(parsed, error);
        Expect(
            loadedLegacy && parsed.language == AppLanguage::Turkish &&
                !parsed.autoScreenSaverEnabled &&
                parsed.screenSaverIdleMinutes == 1440 &&
                parsed.hotCorner == HotCorner::BottomLeft &&
                parsed.hotCornerDelayMs == 0 &&
                parsed.sleepHotkeyEnabled &&
                parsed.sleepHotkey == L"Ctrl+Pause" &&
                parsed.useCustomScreenSaver &&
                parsed.screenSaverPath == L"C:\\Ekran\\görüntü.scr",
            "case-insensitive INI keys and sections parse");

        AppSettings toSave = parsed;
        toSave.showTrayStatusIcon = false;
        toSave.hibernateHotkeyEnabled = true;
        const bool saved = store.Save(toSave, error);
        AppSettings roundTrip;
        const bool loadedRoundTrip = saved && store.Load(roundTrip, error);
        Expect(
            loadedRoundTrip &&
                roundTrip.language == AppLanguage::Turkish &&
                roundTrip.useCustomScreenSaver == toSave.useCustomScreenSaver &&
                roundTrip.screenSaverPath == toSave.screenSaverPath &&
                roundTrip.hotCorner == toSave.hotCorner &&
                roundTrip.showTrayStatusIcon == toSave.showTrayStatusIcon &&
                roundTrip.hibernateHotkeyEnabled &&
                roundTrip.hibernateHotkey == L"Shift+Pause",
            "settings save uses UTF-8 and round-trips values");
        Expect(ScreenSaverPathForLaunch(roundTrip) == roundTrip.screenSaverPath,
            "enabled custom screen saver selects the retained path for launch");

        toSave.useCustomScreenSaver = false;
        const bool savedDisabledCustomSaver = store.Save(toSave, error);
        AppSettings disabledCustomSaver;
        const bool loadedDisabledCustomSaver = savedDisabledCustomSaver &&
            store.Load(disabledCustomSaver, error);
        Expect(loadedDisabledCustomSaver && !disabledCustomSaver.useCustomScreenSaver &&
            disabledCustomSaver.screenSaverPath == toSave.screenSaverPath,
            "disabling custom screen saver preserves its path for later re-enabling");
        Expect(ScreenSaverPathForLaunch(disabledCustomSaver).empty(),
            "disabled custom screen saver selects Windows' configured saver instead");
        toSave.useCustomScreenSaver = true;

        bool allCornerValuesRoundTrip = true;
        for (const HotCorner corner : { HotCorner::TopLeft, HotCorner::TopRight,
            HotCorner::BottomLeft, HotCorner::BottomRight })
        {
            toSave.hotCorner = corner;
            AppSettings loadedCorner;
            allCornerValuesRoundTrip = allCornerValuesRoundTrip &&
                store.Save(toSave, error) && store.Load(loadedCorner, error) &&
                loadedCorner.hotCorner == corner;
        }
        Expect(allCornerValuesRoundTrip,
            "all four Hot Corner choices round-trip through the isolated settings.ini");

        DeleteFileW(path.c_str());
        std::filesystem::path temporaryPath = path;
        temporaryPath += L".tmp";
        DeleteFileW(temporaryPath.c_str());
    }

    void TestHotkeys()
    {
        Expect(ShouldShowSettingsAtLaunch(false, false),
            "normal direct launch opens the Settings window");
        Expect(!ShouldShowSettingsAtLaunch(true, false),
            "--background launches without opening Settings");
        Expect(ShouldShowSettingsAtLaunch(true, true),
            "an explicit Settings request opens Settings in background mode");

        std::wstring error;
        HotkeyGesture pause;
        const bool parsedPause = TryParseHotkey(L"Pause", pause, error);
        Expect(
            parsedPause && pause.virtualKey == VK_PAUSE &&
                FormatHotkey(pause) == L"Pause",
            "Pause shortcut parses and formats");

        HotkeyGesture shiftPause;
        const bool parsedShiftPause = TryParseHotkey(L"Shift+Pause", shiftPause, error);
        Expect(
            parsedShiftPause &&
                shiftPause.modifiers == ModifierShift &&
                shiftPause.virtualKey == VK_PAUSE &&
                FormatHotkey(shiftPause) == L"Shift+Pause",
            "Shift+Pause remains distinct from bare Pause");

        HotkeyGesture controlScroll;
        const bool parsedControlScroll =
            TryParseHotkey(L"Control + Scroll Lock", controlScroll, error);
        Expect(
            parsedControlScroll &&
                controlScroll.modifiers == ModifierControl &&
                controlScroll.virtualKey == VK_SCROLL &&
                FormatHotkey(controlScroll) == L"Ctrl+ScrollLock",
            "Control and Scroll Lock aliases parse");

        HotkeyGesture functionKey;
        const bool parsedFunctionKey =
            TryParseHotkey(L"Shift+F24", functionKey, error);
        Expect(
            parsedFunctionKey && functionKey.virtualKey == VK_F24,
            "function keys map to their Win32 virtual key");

        HotkeyGesture invalid;
        Expect(
            !TryParseHotkey(L"Shift+UnknownKey", invalid, error),
            "unknown shortcut name is rejected");
        Expect(
            !TryParseHotkey(L"LeftShift+Pause", invalid, error),
            "modifier key cannot be assigned as the main key");

        std::vector<ShortcutAssignment> duplicates{
            { ShortcutAction::Sleep, pause, true },
            { ShortcutAction::Hibernate, pause, true }
        };
        Expect(
            HasDuplicateShortcuts(duplicates),
            "duplicate enabled shortcut assignments are rejected");
        duplicates[1].enabled = false;
        Expect(
            !HasDuplicateShortcuts(duplicates),
            "disabled shortcut assignments do not conflict");

        Expect(
            ShouldPassBarePhysicalPauseThroughTerminal(
                ShortcutAction::Sleep, VK_PAUSE, 0, false,
                L"ConsoleWindowClass"),
            "bare physical Pause passes through the classic console used by CMD and Windows PowerShell");
        Expect(
            ShouldPassBarePhysicalPauseThroughTerminal(
                ShortcutAction::Hibernate, VK_PAUSE, 0, false,
                L"CASCADIA_HOSTING_WINDOW_CLASS"),
            "bare physical Pause passes through Windows Terminal");
        Expect(
            !ShouldPassBarePhysicalPauseThroughTerminal(
                ShortcutAction::Hibernate, VK_PAUSE, ModifierShift, false,
                L"ConsoleWindowClass"),
            "Shift+Pause is not covered by the terminal exception");
        Expect(
            !ShouldPassBarePhysicalPauseThroughTerminal(
                ShortcutAction::Sleep, VK_PAUSE, 0, true,
                L"ConsoleWindowClass"),
            "injected Pause is not covered by the terminal exception");
        Expect(
            !ShouldPassBarePhysicalPauseThroughTerminal(
                ShortcutAction::Sleep, 'P', 0, false,
                L"ConsoleWindowClass"),
            "remapped Sleep key is not covered by the terminal exception");
        Expect(
            !ShouldPassBarePhysicalPauseThroughTerminal(
                ShortcutAction::StartScreenSaver, VK_PAUSE, 0, false,
                L"ConsoleWindowClass"),
            "screen saver action is not covered by the power shortcut exception");
    }

    void ExpectPowerStatus(
        const char* name,
        const PowerRequestResult& result,
        PowerRequestStatus expected)
    {
        Expect(result.status == expected, name);
    }

    void TestPowerDispatch()
    {
        FakePowerApi api;
        api.capabilities = { true, true, false, false, true, true, true };
        api.displayOffAccepted = false;
        const PowerRequestResult s0Failed = RequestSleep(api);
        ExpectPowerStatus(
            "S0 display-off API failure is reported",
            s0Failed, PowerRequestStatus::RequestFailed);
        Expect(s0Failed.failureStage == PowerFailureStage::DisplayOffRequest,
            "S0 failure records its actual API stage");
        Expect(
            api.displayOffCalls == 1 && api.sleepCalls == 0 &&
                api.hibernateCalls == 0,
            "S0 failure does not fall back to classic Sleep or Hibernate");

        api = FakePowerApi{};
        api.capabilities = { true, false, false, false, true, true, true };
        api.suspendAccepted = false;
        const PowerRequestResult s3Failed = RequestSleep(api);
        ExpectPowerStatus(
            "classic Sleep API failure is reported",
            s3Failed, PowerRequestStatus::RequestFailed);
        Expect(s3Failed.failureStage == PowerFailureStage::SetSuspendState,
            "classic Sleep failure records SetSuspendState as its stage");
        Expect(
            api.displayOffCalls == 0 && api.sleepCalls == 1 &&
                api.hibernateCalls == 0,
            "classic Sleep failure does not fall back to another power action");

        api = FakePowerApi{};
        api.capabilities = { true, false, false, false, false, true, false };
        ExpectPowerStatus(
            "Sleep unavailable is reported without a power call",
            RequestSleep(api), PowerRequestStatus::SleepUnavailable);
        Expect(
            api.displayOffCalls == 0 && api.sleepCalls == 0 &&
                api.hibernateCalls == 0,
            "Sleep capability absence causes no power API call");

        api = FakePowerApi{};
        api.capabilities = { true, false, false, false, true, true, false };
        ExpectPowerStatus(
            "Hibernate requires both S4 and its hibernation file",
            RequestHibernate(api), PowerRequestStatus::HibernateUnavailable);
        Expect(
            api.displayOffCalls == 0 && api.sleepCalls == 0 &&
                api.hibernateCalls == 0,
            "unavailable Hibernate causes no power API call");

        api = FakePowerApi{};
        api.capabilities = { true, true, false, false, false, true, true };
        ExpectPowerStatus(
            "available Hibernate sends its own request",
            RequestHibernate(api), PowerRequestStatus::RequestAccepted);
        Expect(
            api.displayOffCalls == 0 && api.sleepCalls == 0 &&
                api.hibernateCalls == 1,
            "Hibernate uses one S4 request and no alternate power action");

        api = FakePowerApi{};
        api.capabilities = { true, true, false, false, false, true, true };
        api.suspendAccepted = false;
        api.suspendErrorCode = ERROR_NOACCESS;
        api.suspendFailureStage = PowerFailureStage::SetSuspendState;
        const PowerRequestResult hibernateFailed = RequestHibernate(api);
        Expect(hibernateFailed.status == PowerRequestStatus::RequestFailed &&
            hibernateFailed.errorCode == ERROR_NOACCESS &&
            hibernateFailed.failureStage == PowerFailureStage::SetSuspendState,
            "Hibernate failure preserves the API stage and ERROR_NOACCESS code");
        Expect(api.hibernateCalls == 1 && api.sleepCalls == 0 && api.displayOffCalls == 0,
            "failed Hibernate performs no Sleep, display-off or fallback request");

        api = FakePowerApi{};
        api.capabilities = { true, true, false, false, false, true, true };
        api.suspendAccepted = false;
        api.suspendErrorCode = ERROR_NOACCESS;
        api.suspendFailureStage = PowerFailureStage::AdjustShutdownPrivilege;
        const PowerRequestResult privilegeFailed = RequestHibernate(api);
        Expect(privilegeFailed.status == PowerRequestStatus::RequestFailed &&
            privilegeFailed.errorCode == ERROR_NOACCESS &&
            privilegeFailed.failureStage == PowerFailureStage::AdjustShutdownPrivilege,
            "Hibernate reports ERROR_NOACCESS from AdjustTokenPrivileges as the privilege stage");
        Expect(api.hibernateCalls == 1 && api.sleepCalls == 0 && api.displayOffCalls == 0,
            "privilege-adjustment failure performs no other power action");

        api = FakePowerApi{};
        api.capabilities = {};
        ExpectPowerStatus(
            "power capability query failure fails closed",
            RequestSleep(api), PowerRequestStatus::CapabilitiesUnavailable);
        Expect(
            api.displayOffCalls == 0 && api.sleepCalls == 0 &&
                api.hibernateCalls == 0,
            "capability query failure causes no power API call");
    }

    void TestRuntimePolicy()
    {
        Expect(!IsAutoScreenSaverDue(false, false, 500'000, 2),
            "disabled automatic screen saver never becomes due");
        Expect(!IsAutoScreenSaverDue(false, false, 60'000, 1) &&
            IsAutoLockDue(true, false, 60'000, 1),
            "automatic lock remains due when the app-managed screen saver is disabled");
        Expect(!IsAutoScreenSaverDue(true, true, 500'000, 2),
            "automatic screen saver fires once per idle period");
        Expect(!IsAutoScreenSaverDue(true, false, 119'999, 2),
            "automatic screen saver observes its configured idle threshold");
        Expect(IsAutoScreenSaverDue(true, false, 120'000, 2),
            "automatic screen saver is due at the configured threshold");
        Expect(IsAutoLockDue(true, false, 600'000, 10),
            "automatic lock observes its independent idle threshold");
        Expect(!IsAutoLockDue(true, true, 900'000, 10),
            "automatic lock fires once per idle period");
        Expect(EffectiveIdleMilliseconds(120'000, 60'000) == 60'000 &&
            EffectiveIdleMilliseconds(45'000, 90'000) == 45'000,
            "effective idle uses the earlier of real input and app activity clocks");
        Expect(!IsIdleTimerGap(0, 120'000, 5'000) &&
            !IsIdleTimerGap(10'000, 14'999, 5'000) &&
            IsIdleTimerGap(10'000, 15'000, 5'000) &&
            !IsIdleTimerGap(15'000, 10'000, 5'000),
            "long idle-timer gaps identify resume without treating startup or nonmonotonic values as resume");
        const std::uint64_t postWakeIdle = EffectiveIdleMilliseconds(121'032, 250);
        Expect(postWakeIdle == 250 && !IsAutoScreenSaverDue(true, false, postWakeIdle, 1) &&
            !IsAutoLockDue(true, false, postWakeIdle, 1),
            "a large pre-wake system idle value cannot bypass the fresh post-resume activity baseline");
        bool lockAttempted = false;
        int fakeLockCalls = 0;
        for (int tick = 0; tick < 4; ++tick)
        {
            if (IsAutoLockDue(true, lockAttempted, 60'000, 1))
            {
                lockAttempted = true;
                ++fakeLockCalls; // Simulate failure without calling LockWorkStation.
            }
        }
        Expect(fakeLockCalls == 1 && lockAttempted,
            "a failed automatic lock request is not repeated every timer tick");

        Expect(!IsBlockingMediaPlayback(true, false, false),
            "unknown active media playback is not misclassified as video");
        Expect(IsBlockingMediaPlayback(true, true, true),
            "explicitly identified active video blocks the screen saver");
        Expect(!IsBlockingMediaPlayback(true, true, false),
            "known audio-only playback does not block the screen saver");
        Expect(!IsBlockingMediaPlayback(false, false, false),
            "stopped media with unknown type does not block the screen saver");
        Expect(!IsBlockingMediaPlayback(false, true, true),
            "paused or ended video does not block the screen saver");

        const std::uint32_t initialGuardRevision = 7;
        const std::uint32_t preSuspendQuery = NextGuardQueryRevision(initialGuardRevision);
        Expect(IsGuardQueryResultCurrent(preSuspendQuery, preSuspendQuery, preSuspendQuery),
            "current idle guard query result is accepted");
        const std::uint32_t afterS0Resume = NextGuardQueryRevision(preSuspendQuery);
        Expect(!IsGuardQueryResultCurrent(preSuspendQuery, preSuspendQuery, afterS0Resume),
            "a pre-suspend guard result is rejected after S0 resume invalidates its revision");
        int staleSaverDispatches = 0;
        int staleLockDispatches = 0;
        if (IsGuardQueryResultCurrent(preSuspendQuery, preSuspendQuery, afterS0Resume))
        {
            ++staleSaverDispatches;
            ++staleLockDispatches;
        }
        const std::uint32_t preHibernateQuery = NextGuardQueryRevision(afterS0Resume);
        const std::uint32_t afterS4Resume = NextGuardQueryRevision(preHibernateQuery);
        Expect(!IsGuardQueryResultCurrent(preHibernateQuery, preHibernateQuery, afterS4Resume),
            "a pre-hibernate guard result is rejected after S4 resume invalidates its revision");
        if (IsGuardQueryResultCurrent(preHibernateQuery, preHibernateQuery, afterS4Resume))
        {
            ++staleSaverDispatches;
            ++staleLockDispatches;
        }
        Expect(staleSaverDispatches == 0 && staleLockDispatches == 0,
            "stale S0/S4 guard results dispatch neither screen saver nor lock work");
        Expect(!IsGuardQueryResultCurrent(preHibernateQuery, preHibernateQuery,
            NextGuardQueryRevision(afterS4Resume)),
            "user activity also invalidates a delayed guard result");
        Expect(NextGuardQueryRevision(std::numeric_limits<std::uint32_t>::max()) == 1,
            "guard query revision wrap skips the reserved zero value");
        Expect(EffectiveIdleMilliseconds(24ull * 60ull * 60ull * 1000ull, 0) == 0 &&
            !IsAutoScreenSaverDue(true, false, 0, 1),
            "resume app-activity baseline prevents sleep duration alone triggering saver");

        const RuntimeGuardSnapshot video{ true, false, false };
        const RuntimeGuardSnapshot audioOnly{ false, false, false };
        const RuntimeGuardSnapshot fullscreen{ false, true, false };
        const RuntimeGuardSnapshot presentation{ false, false, true };
        Expect(IsScreenSaverBlocked(true, false, video),
            "media guard blocks explicitly identified active video");
        Expect(!IsScreenSaverBlocked(true, false, audioOnly),
            "audio-only playback does not block the screen saver");
        Expect(!IsScreenSaverBlocked(false, false, video),
            "disabled media guard does not block the screen saver");
        const RuntimeGuardSnapshot pausedOrEndedVideo{ false, false, false };
        Expect(!IsScreenSaverBlocked(true, false, pausedOrEndedVideo),
            "paused or ended video restores normal screen-saver eligibility");
        Expect(IsScreenSaverBlocked(false, true, fullscreen),
            "full-screen guard blocks a full-screen foreground app");
        Expect(IsScreenSaverBlocked(false, true, presentation),
            "full-screen guard blocks presentation mode");
        Expect(!IsAutomaticLockBlocked(false, video),
            "automatic lock guard can be disabled independently");
        Expect(IsAutomaticLockBlocked(true, fullscreen),
            "automatic lock guard observes full-screen and presentation status");

        Expect(IsPointInHotCorner(HotCorner::TopLeft, 0, 0, 0, 0, 1920, 1080),
            "top-left Hot Corner includes its edge");
        Expect(IsPointInHotCorner(HotCorner::TopRight, 1917, 2, 0, 0, 1920, 1080),
            "top-right Hot Corner uses the configured edge tolerance");
        Expect(IsPointInHotCorner(HotCorner::BottomLeft, 2, 1077, 0, 0, 1920, 1080),
            "bottom-left Hot Corner uses the configured edge tolerance");
        Expect(IsPointInHotCorner(HotCorner::BottomRight, 1919, 1079, 0, 0, 1920, 1080),
            "bottom-right Hot Corner includes the screen corner");
        Expect(!IsPointInHotCorner(HotCorner::TopRight, 100, 2, 0, 0, 1920, 1080),
            "Hot Corner ignores points outside the selected edge");
        Expect(!IsHotCornerDelayDue(true, true, 1000, 500),
            "Hot Corner triggers only once per continuous entry");
        Expect(!IsHotCornerDelayDue(true, false, 499, 500),
            "Hot Corner waits for its configured dwell time");
        Expect(IsHotCornerDelayDue(true, false, 500, 500),
            "Hot Corner triggers at its configured dwell time");
    }

    void TestLiveMediaSessionQuery()
    {
        const auto status = QueryGuards(true, false);
        const char* state = "not-requested";
        switch (status.mediaQueryState)
        {
        case MediaQueryState::Succeeded: state = "succeeded"; break;
        case MediaQueryState::TimedOut: state = "timed-out"; break;
        case MediaQueryState::Failed: state = "failed"; break;
        default: break;
        }
        std::printf(
            "Read-only GSMTC query (current test account): state=%s hresult=0x%08lX session=%u video=%u music=%u unknown-type=%u\n",
            state,
            static_cast<unsigned long>(status.mediaQueryHresult),
            status.mediaSessionAvailable ? 1u : 0u,
            status.videoPlaying ? 1u : 0u,
            status.musicPlaying ? 1u : 0u,
            status.unknownTypePlaying ? 1u : 0u);
        Expect(status.mediaQueryState != MediaQueryState::NotRequested,
            "a requested GSMTC check reports success, timeout or failure explicitly");
        Expect(!status.videoPlaying || status.mediaQueryState == MediaQueryState::Succeeded,
            "video guard cannot be asserted when the GSMTC query failed");
    }

    std::wstring ProcessNameForPid(DWORD processId)
    {
        HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
        if (process == nullptr) return {};

        std::wstring imagePath(32768, L'\0');
        DWORD length = static_cast<DWORD>(imagePath.size());
        const BOOL read = QueryFullProcessImageNameW(process, 0, imagePath.data(), &length);
        CloseHandle(process);
        if (read == FALSE || length == 0) return {};
        imagePath.resize(length);
        const std::size_t slash = imagePath.find_last_of(L"\\/");
        return slash == std::wstring::npos ? imagePath : imagePath.substr(slash + 1);
    }

    bool IsVideoDecodeEngine(const wchar_t* instanceName)
    {
        if (instanceName == nullptr) return false;
        std::wstring normalized(instanceName);
        std::transform(normalized.begin(), normalized.end(), normalized.begin(),
            [](wchar_t value) { return static_cast<wchar_t>(std::towlower(value)); });
        return normalized.find(L"engtype_videodecode") != std::wstring::npos;
    }

    int RunMediaGuardDiagnostic()
    {
        const auto media = QueryGuards(true, false);
        const char* state = "not-requested";
        switch (media.mediaQueryState)
        {
        case MediaQueryState::Succeeded: state = "succeeded"; break;
        case MediaQueryState::TimedOut: state = "timed-out"; break;
        case MediaQueryState::Failed: state = "failed"; break;
        default: break;
        }
        std::printf(
            "Read-only GSMTC diagnostic: state=%s hresult=0x%08lX session=%u video=%u music=%u unknown-type=%u\n",
            state,
            static_cast<unsigned long>(media.mediaQueryHresult),
            media.mediaSessionAvailable ? 1u : 0u,
            media.videoPlaying ? 1u : 0u,
            media.musicPlaying ? 1u : 0u,
            media.unknownTypePlaying ? 1u : 0u);

        PDH_HQUERY query{};
        PDH_STATUS status = PdhOpenQueryW(nullptr, 0, &query);
        if (status != ERROR_SUCCESS)
        {
            std::printf("Read-only GPU diagnostic: unavailable pdh_status=0x%08lX\n",
                static_cast<unsigned long>(status));
            return EXIT_SUCCESS;
        }

        PDH_HCOUNTER counter{};
        status = PdhAddEnglishCounterW(query, L"\\GPU Engine(*)\\Utilization Percentage", 0, &counter);
        if (status != ERROR_SUCCESS)
        {
            PdhCloseQuery(query);
            std::printf("Read-only GPU diagnostic: counter-unavailable pdh_status=0x%08lX\n",
                static_cast<unsigned long>(status));
            return EXIT_SUCCESS;
        }

        status = PdhCollectQueryData(query);
        if (status != ERROR_SUCCESS)
        {
            PdhCloseQuery(query);
            std::printf("Read-only GPU diagnostic: sample-failed pdh_status=0x%08lX\n",
                static_cast<unsigned long>(status));
            return EXIT_SUCCESS;
        }

        for (unsigned int sample = 1; sample <= 5; ++sample)
        {
            Sleep(1000);
            status = PdhCollectQueryData(query);
            if (status != ERROR_SUCCESS)
            {
                std::printf("GPU VideoDecode sample %u/5: failed pdh_status=0x%08lX\n",
                    sample, static_cast<unsigned long>(status));
                continue;
            }

            DWORD bufferSize{};
            DWORD itemCount{};
            status = PdhGetFormattedCounterArrayW(
                counter, PDH_FMT_DOUBLE, &bufferSize, &itemCount, nullptr);
            if (status != PDH_MORE_DATA && status != ERROR_SUCCESS)
            {
                std::printf("GPU VideoDecode sample %u/5: read-failed pdh_status=0x%08lX\n",
                    sample, static_cast<unsigned long>(status));
                continue;
            }

            std::vector<BYTE> storage(bufferSize);
            auto* items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(storage.data());
            status = PdhGetFormattedCounterArrayW(
                counter, PDH_FMT_DOUBLE, &bufferSize, &itemCount, items);
            if (status != ERROR_SUCCESS)
            {
                std::printf("GPU VideoDecode sample %u/5: read-failed pdh_status=0x%08lX\n",
                    sample, static_cast<unsigned long>(status));
                continue;
            }

            double firefoxPeak{};
            double allProcessPeak{};
            unsigned int activeEngineCount{};
            for (DWORD index = 0; index < itemCount; ++index)
            {
                if (!IsVideoDecodeEngine(items[index].szName) ||
                    (items[index].FmtValue.CStatus != PDH_CSTATUS_VALID_DATA &&
                        items[index].FmtValue.CStatus != PDH_CSTATUS_NEW_DATA))
                {
                    continue;
                }

                const double utilization = items[index].FmtValue.doubleValue;
                allProcessPeak = (std::max)(allProcessPeak, utilization);
                if (utilization <= 0.1) continue;
                ++activeEngineCount;

                const wchar_t* prefix = items[index].szName;
                if (prefix != nullptr && wcsncmp(prefix, L"pid_", 4) == 0)
                {
                    wchar_t* end{};
                    const unsigned long parsedPid = std::wcstoul(prefix + 4, &end, 10);
                    if (end != prefix + 4 && end != nullptr && *end == L'_')
                    {
                        const std::wstring processName = ProcessNameForPid(static_cast<DWORD>(parsedPid));
                        if (_wcsicmp(processName.c_str(), L"firefox.exe") == 0)
                        {
                            firefoxPeak = (std::max)(firefoxPeak, utilization);
                        }
                    }
                }
            }

            std::printf(
                "GPU VideoDecode sample %u/5: active_engines=%u firefox_peak=%.3f%% all_process_peak=%.3f%%\n",
                sample, activeEngineCount, firefoxPeak, allProcessPeak);
        }

        PdhCloseQuery(query);
        return EXIT_SUCCESS;
    }

    void TestSingleInstancePrimitives()
    {
        const std::wstring suffix = std::to_wstring(GetCurrentProcessId());
        const std::wstring mutexName = L"Local\\CDS.NativeTests.Mutex." + suffix;
        HANDLE firstMutex = CreateMutexW(nullptr, TRUE, mutexName.c_str());
        const DWORD firstError = GetLastError();
        HANDLE secondMutex = CreateMutexW(nullptr, FALSE, mutexName.c_str());
        const DWORD secondError = GetLastError();
        Expect(firstMutex != nullptr && firstError != ERROR_ALREADY_EXISTS,
            "single-instance primitive creates the first named mutex");
        Expect(secondMutex != nullptr && secondError == ERROR_ALREADY_EXISTS,
            "single-instance primitive detects a second named mutex");
        if (firstMutex != nullptr) ReleaseMutex(firstMutex);
        if (secondMutex != nullptr) CloseHandle(secondMutex);
        if (firstMutex != nullptr) CloseHandle(firstMutex);

        const std::wstring eventName = L"Local\\CDS.NativeTests.Event." + suffix;
        HANDLE event = CreateEventW(nullptr, FALSE, FALSE, eventName.c_str());
        const bool eventSet = event != nullptr && SetEvent(event) != FALSE;
        const bool eventObserved = event != nullptr && WaitForSingleObject(event, 0) == WAIT_OBJECT_0;
        Expect(eventSet && eventObserved,
            "single-instance show-settings event can signal the primary instance");
        if (event != nullptr) CloseHandle(event);
    }

    void TestTrayCallbackDecoding()
    {
        TrayNotification notification{};
        const WPARAM anchor = static_cast<WPARAM>(MAKELPARAM(640, 480));
        const LPARAM version4Callback = MAKELPARAM(WM_CONTEXTMENU, 1);
        const bool version4Decoded = TryDecodeTrayCallback(
            anchor, version4Callback, true, 1, notification);
        Expect(version4Decoded && notification.iconId == 1 &&
            notification.eventCode == WM_CONTEXTMENU && notification.hasAnchor &&
            notification.anchor.x == 640 && notification.anchor.y == 480,
            "NOTIFYICON_VERSION_4 decodes event, icon id and anchor coordinates");

        notification = {};
        const bool version4WrongIconRejected = !TryDecodeTrayCallback(
            anchor, MAKELPARAM(WM_CONTEXTMENU, 2), true, 1, notification);
        Expect(version4WrongIconRejected,
            "NOTIFYICON_VERSION_4 rejects callbacks for a different icon id");

        notification = {};
        const bool version4DecodedAfterShellRecreation = TryDecodeTrayCallback(
            anchor, version4Callback, false, 1, notification);
        Expect(version4DecodedAfterShellRecreation && notification.hasAnchor &&
            notification.eventCode == WM_CONTEXTMENU,
            "version 4 callback remains decodable while shell icon registration is being recovered");

        notification = {};
        const bool legacyDecoded = TryDecodeTrayCallback(
            1, WM_RBUTTONUP, false, 1, notification);
        Expect(legacyDecoded && notification.iconId == 1 &&
            notification.eventCode == WM_RBUTTONUP && !notification.hasAnchor,
            "legacy notification format decodes wParam icon id and lParam event");

        notification = {};
        Expect(!TryDecodeTrayCallback(2, WM_RBUTTONUP, false, 1, notification),
            "legacy notification format rejects callbacks for a different icon id");

        notification = {};
        const bool legacyDecodedAfterVersion4NegotiationLoss = TryDecodeTrayCallback(
            1, WM_RBUTTONUP, true, 1, notification);
        Expect(legacyDecodedAfterVersion4NegotiationLoss && !notification.hasAnchor &&
            notification.eventCode == WM_RBUTTONUP,
            "legacy callback remains decodable when version 4 negotiation is unavailable");

        notification = {};
        Expect(!TryDecodeTrayCallback(anchor, MAKELPARAM(0x7FFF, 1), true, 1, notification),
            "unknown callback payloads are rejected in both tray protocols");

        Expect(IsTrayContextMenuEvent(WM_CONTEXTMENU, true) &&
            !IsTrayContextMenuEvent(WM_RBUTTONUP, true),
            "version 4 opens the context menu only for its context-menu callback, preventing duplicate right-click menus");
        Expect(IsTrayContextMenuEvent(WM_RBUTTONUP, false) &&
            !IsTrayContextMenuEvent(WM_CONTEXTMENU, false),
            "legacy tray callback opens the context menu only on right-button release");
        Expect(IsTrayContextMenuEvent(NIN_KEYSELECT, true) &&
            IsTrayContextMenuEvent(NIN_KEYSELECT, false),
            "keyboard selection can open the tray context menu for either callback protocol");
    }

    void TestTrayMenuModelAndPlacement()
    {
        Expect(ShouldRetryTrayRegistration(false, 0),
            "transient taskbar registration failures schedule a retry");
        Expect(ShouldRetryTrayRegistration(false, 1000000),
            "tray registration recovery continues after repeated transient failures");
        Expect(!ShouldRetryTrayRegistration(true, 0),
            "successful tray registration does not schedule a retry");
        Expect(TrayRegistrationRetryDelayMs(0) == TrayRegistrationRetryInitialIntervalMs &&
            TrayRegistrationRetryDelayMs(1) == 2000 &&
            TrayRegistrationRetryDelayMs(2) == 4000 &&
            TrayRegistrationRetryDelayMs(3) == 8000 &&
            TrayRegistrationRetryDelayMs(4) == TrayRegistrationRetryMaximumIntervalMs &&
            TrayRegistrationRetryDelayMs(1000000) == TrayRegistrationRetryMaximumIntervalMs,
            "tray registration retry uses exponential backoff capped at fifteen seconds");

        const auto safe = MakeTrayMenuModel(false, false);
        Expect(safe.size() == 4 &&
            safe[0].command == MenuSettings && safe[0].localizationKey == L"TraySettings" &&
            safe[1].command == MenuStartSaver && safe[1].localizationKey == L"TrayStart" &&
            safe[2].command == MenuToggleSaver && safe[2].localizationKey == L"TrayStatusInactive" &&
            safe[3].command == MenuExit && safe[3].localizationKey == L"Exit",
            "tray menu retains C# item order and localized labels in safe UI mode");
        Expect(safe[0].enabled && !safe[1].enabled && !safe[2].enabled && safe[3].enabled,
            "safe UI menu shows saver actions disabled while Settings and Exit stay enabled");
        Expect(safe[0].glyph == TrayMenuGlyph::Settings &&
            safe[1].glyph == TrayMenuGlyph::Start &&
            safe[2].glyph == TrayMenuGlyph::Disabled &&
            safe[3].glyph == TrayMenuGlyph::Exit,
            "each tray menu action receives its matching icon");

        const auto normalEnabled = MakeTrayMenuModel(true, true);
        Expect(normalEnabled[0].enabled && normalEnabled[1].enabled &&
            normalEnabled[2].enabled && normalEnabled[3].enabled &&
            normalEnabled[2].localizationKey == L"TrayStatusActive" &&
            normalEnabled[2].glyph == TrayMenuGlyph::Enabled,
            "normal tray menu enables saver actions and reflects automatic mode On");
        const auto normalDisabled = MakeTrayMenuModel(true, false);
        Expect(normalDisabled[2].localizationKey == L"TrayStatusInactive" &&
            normalDisabled[2].glyph == TrayMenuGlyph::Disabled,
            "normal tray menu reflects automatic mode Off");

        const RECT monitor{ 0, 0, 1920, 1080 };
        const RECT bottomWork{ 0, 0, 1920, 1040 };
        const RECT bottomRightIcon{ 1640, 1040, 1664, 1080 };
        const TrayMenuPlacement bottom = CalculateTrayMenuPlacement(
            bottomRightIcon, monitor, bottomWork, 180);
        Expect(bottom.anchor.x == bottomRightIcon.right &&
            bottom.anchor.y == bottomRightIcon.top &&
            (bottom.alignmentFlags & TPM_RIGHTALIGN) != 0 &&
            (bottom.alignmentFlags & TPM_BOTTOMALIGN) != 0 &&
            (bottom.alignmentFlags & TPM_WORKAREA) != 0,
            "bottom taskbar tray menu aligns above the icon inside the work area");

        const RECT topWork{ 0, 40, 1920, 1080 };
        const RECT topRightIcon{ 1640, 0, 1664, 40 };
        const TrayMenuPlacement top = CalculateTrayMenuPlacement(
            topRightIcon, monitor, topWork, 180);
        Expect(top.anchor.x == topRightIcon.right && top.anchor.y == topRightIcon.bottom &&
            (top.alignmentFlags & TPM_RIGHTALIGN) != 0 &&
            (top.alignmentFlags & TPM_BOTTOMALIGN) == 0,
            "top taskbar tray menu aligns below the icon");

        const RECT leftWork{ 40, 0, 1920, 1080 };
        const RECT leftIcon{ 0, 500, 40, 540 };
        const TrayMenuPlacement left = CalculateTrayMenuPlacement(leftIcon, monitor, leftWork, 180);
        Expect(left.anchor.x == leftIcon.right &&
            (left.alignmentFlags & TPM_RIGHTALIGN) == 0,
            "left taskbar tray menu opens to the right of the icon");

        const RECT rightWork{ 0, 0, 1880, 1080 };
        const RECT rightIcon{ 1880, 500, 1920, 540 };
        const TrayMenuPlacement right = CalculateTrayMenuPlacement(rightIcon, monitor, rightWork, 180);
        Expect(right.anchor.x == rightIcon.left &&
            (right.alignmentFlags & TPM_RIGHTALIGN) != 0,
            "right taskbar tray menu opens to the left of the icon");
    }

    void TestTrayMenuImages()
    {
        const DWORD handlesBefore = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
        bool allIconsValid = true;
        bool iconsHaveTransparency = true;
        for (unsigned int pass = 0; pass < 3; ++pass)
        {
            TrayMenuImages images(144);
            allIconsValid = allIconsValid && images.IsAvailable() && images.Size() == 36;
            for (unsigned int glyph = 0; glyph < 5; ++glyph)
            {
                HBITMAP bitmap = images.Get(static_cast<TrayMenuGlyph>(glyph));
                DIBSECTION section{};
                const int objectSize = bitmap == nullptr ? 0 :
                    GetObjectW(bitmap, sizeof(section), &section);
                const bool dimensionsValid = objectSize == sizeof(section) &&
                    section.dsBm.bmWidth == 36 && section.dsBm.bmHeight == 36 &&
                    section.dsBm.bmBits != nullptr;
                allIconsValid = allIconsValid && dimensionsValid;
                if (dimensionsValid)
                {
                    const auto* pixels = static_cast<const std::uint32_t*>(section.dsBm.bmBits);
                    bool hasVisiblePixel = false;
                    for (int index = 0; index < 36 * 36; ++index)
                    {
                        hasVisiblePixel = hasVisiblePixel || (pixels[index] >> 24) != 0;
                    }
                    iconsHaveTransparency = iconsHaveTransparency && hasVisiblePixel &&
                        (pixels[0] >> 24) == 0;
                }
            }
        }
        const DWORD handlesAfter = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
        Expect(allIconsValid, "tray menu icons are created at the current DPI");
        Expect(iconsHaveTransparency, "tray menu icon bitmaps preserve transparent margins");
        Expect(handlesBefore == 0 || handlesAfter <= handlesBefore + 1,
            "repeated tray icon creation and destruction does not leak GDI objects");
    }

    void TestDiagnosticLoggingPreservesLastError()
    {
        SetUiDiagnosticLoggingEnabled(true);
        LogUiDiagnostic(L"test_last_error_log_seed");
        constexpr DWORD Sentinel = 0x1234;
        SetLastError(Sentinel);
        LogUiDiagnostic(L"test_last_error_log_preserved");
        const DWORD actual = GetLastError();
        SetUiDiagnosticLoggingEnabled(false);
        Expect(actual == Sentinel,
            "diagnostic logging preserves the caller's last-error value");

        SetLastError(Sentinel);
        LogPowerRequestDiagnostic(nullptr, true, FALSE, ERROR_NOACCESS,
            L"operation=Hibernate failure_stage=SetSuspendState");
        Expect(GetLastError() == Sentinel,
            "normal-mode power failure diagnostics preserve last error while logging API stage");
    }

    void TestUiTestPolicy()
    {
        const UiTestPolicy safe = MakeUiTestPolicy(true);
        Expect(!safe.applyStartupSettings && !safe.installKeyboardHook &&
            !safe.startRuntimeTimers && !safe.persistSettings &&
            !safe.allowSaverActions && !safe.allowPowerActions &&
            !safe.suppressWindowsScreenSaver && safe.readOnlySettings,
            "safe UI test mode disables power, saver, Windows saver suppression, startup, timer and settings writes");

        const UiTestPolicy normal = MakeUiTestPolicy(false);
        Expect(normal.applyStartupSettings && normal.installKeyboardHook &&
            normal.startRuntimeTimers && normal.persistSettings &&
            normal.allowSaverActions && normal.allowPowerActions &&
            normal.suppressWindowsScreenSaver && !normal.readOnlySettings,
            "normal application policy retains existing runtime behavior");
    }

    class FakeScreenSaverActivationApi final : public IScreenSaverActivationApi
    {
    public:
        bool active{};
        bool secure{};
        bool policyManaged{};
        bool querySucceeds{ true };
        bool secureQuerySucceeds{ true };
        bool policyQuerySucceeds{ true };
        bool setSucceeds{ true };
        DWORD queryError{ ERROR_ACCESS_DENIED };
        DWORD secureQueryError{ ERROR_ACCESS_DENIED };
        DWORD policyQueryError{ ERROR_ACCESS_DENIED };
        DWORD setError{ ERROR_ACCESS_DENIED };
        unsigned int queryCalls{};
        unsigned int secureQueryCalls{};
        unsigned int policyQueryCalls{};
        unsigned int setCalls{};

        bool Query(bool& value, DWORD& error) noexcept override
        {
            ++queryCalls;
            error = querySucceeds ? ERROR_SUCCESS : queryError;
            if (!querySucceeds) return false;
            value = active;
            return true;
        }

        bool QuerySecure(bool& value, DWORD& error) noexcept override
        {
            ++secureQueryCalls;
            error = secureQuerySucceeds ? ERROR_SUCCESS : secureQueryError;
            if (!secureQuerySucceeds) return false;
            value = secure;
            return true;
        }

        bool QueryPolicyManaged(bool& value, DWORD& error) noexcept override
        {
            ++policyQueryCalls;
            error = policyQuerySucceeds ? ERROR_SUCCESS : policyQueryError;
            if (!policyQuerySucceeds) return false;
            value = policyManaged;
            return true;
        }

        bool Set(bool value, DWORD& error) noexcept override
        {
            ++setCalls;
            error = setSucceeds ? ERROR_SUCCESS : setError;
            if (!setSucceeds) return false;
            active = value;
            return true;
        }
    };

    void TestWindowsScreenSaverActivationLease()
    {
        FakeScreenSaverActivationApi api;
        api.active = true;
        ScreenSaverActivationLease lease;
        const auto begin = lease.Begin(api);
        Expect(begin.succeeded && begin.changed && !api.active && lease.OriginalActive(),
            "normal app suppression temporarily disables an enabled Windows saver");
        const auto noOpReassert = lease.Reassert(api);
        Expect(noOpReassert.succeeded && !noOpReassert.changed && api.setCalls == 1,
            "reasserting an already suppressed Windows saver performs no redundant setting call");
        api.active = true;
        const auto reassert = lease.Reassert(api);
        Expect(reassert.succeeded && reassert.changed && !api.active,
            "runtime reassertion suppresses an external re-enable without changing the saved original state");
        const auto restore = lease.Restore(api);
        Expect(restore.succeeded && restore.changed && api.active && !lease.Acquired(),
            "normal shutdown restores the initial Windows screen-saver state");

        api = FakeScreenSaverActivationApi{};
        api.active = false;
        ScreenSaverActivationLease initiallyDisabled;
        const auto disabledBegin = initiallyDisabled.Begin(api);
        const auto disabledRestore = initiallyDisabled.Restore(api);
        Expect(disabledBegin.succeeded && !disabledBegin.changed &&
            disabledRestore.succeeded && !disabledRestore.changed && !api.active && api.setCalls == 0,
            "an initially disabled Windows saver stays disabled and needs no setting writes");

        api = FakeScreenSaverActivationApi{};
        api.active = true;
        api.querySucceeds = false;
        ScreenSaverActivationLease unknownState;
        const auto queryFailure = unknownState.Begin(api);
        Expect(!queryFailure.succeeded && !unknownState.Acquired() && api.setCalls == 0,
            "failed state query fails closed without changing the Windows saver");

        api = FakeScreenSaverActivationApi{};
        api.active = true;
        api.policyManaged = true;
        ScreenSaverActivationLease policyManagedSaver;
        const auto policyBegin = policyManagedSaver.Begin(api);
        Expect(policyBegin.succeeded && policyBegin.policyManaged &&
            !policyManagedSaver.Acquired() && api.active && api.setCalls == 0,
            "screen-saver policy settings prevent runtime suppression");

        api = FakeScreenSaverActivationApi{};
        api.active = true;
        api.policyQuerySucceeds = false;
        ScreenSaverActivationLease unknownPolicy;
        const auto policyQueryFailure = unknownPolicy.Begin(api);
        Expect(!policyQueryFailure.succeeded && !unknownPolicy.Acquired() &&
            api.active && api.setCalls == 0,
            "failed policy query fails closed without changing the Windows saver");

        api = FakeScreenSaverActivationApi{};
        api.active = true;
        api.setSucceeds = false;
        ScreenSaverActivationLease failedSet;
        const auto setFailure = failedSet.Begin(api);
        Expect(!setFailure.succeeded && !failedSet.Acquired() && api.active && api.setCalls == 1,
            "failed suppression does not claim ownership or alter later shutdown behavior");

        api = FakeScreenSaverActivationApi{};
        api.active = true;
        ScreenSaverActivationLease retryRestore;
        const auto retryBegin = retryRestore.Begin(api);
        api.setSucceeds = false;
        const auto restoreFailure = retryRestore.Restore(api);
        Expect(retryBegin.succeeded && !restoreFailure.succeeded && retryRestore.Acquired(),
            "failed screen-saver restoration keeps the lease available for a shutdown retry");
        api.setSucceeds = true;
        const auto restoreRetry = retryRestore.Restore(api);
        Expect(restoreRetry.succeeded && api.active && !retryRestore.Acquired(),
            "a subsequent restoration attempt returns the original enabled state");

        api = FakeScreenSaverActivationApi{};
        api.active = true;
        ScreenSaverActivationLease policyAppeared;
        const auto beforePolicy = policyAppeared.Begin(api);
        api.policyManaged = true;
        const auto policyReassert = policyAppeared.Reassert(api);
        Expect(beforePolicy.changed && policyReassert.succeeded &&
            policyReassert.policyManaged && api.active && !policyAppeared.Acquired(),
            "a newly applied saver policy restores the Windows saver during runtime");

        api = FakeScreenSaverActivationApi{};
        api.active = true;
        ScreenSaverActivationLease relinquished;
        const auto relinquishBegin = relinquished.Begin(api);
        api.active = false;
        relinquished.Abandon();
        const auto relinquishedReassert = relinquished.Reassert(api);
        const auto relinquishedRestore = relinquished.Restore(api);
        Expect(relinquishBegin.changed && relinquishedReassert.succeeded &&
            relinquishedRestore.succeeded && !api.active && api.setCalls == 1,
            "an explicit Windows active-state change transfers ownership and is preserved");

        api = FakeScreenSaverActivationApi{};
        api.active = true;
        ScreenSaverActivationLease failedSecurityRecheck;
        const auto beforeSecurityFailure = failedSecurityRecheck.Begin(api);
        api.secureQuerySucceeds = false;
        const auto securityRecheckFailure = failedSecurityRecheck.Reassert(api);
        Expect(beforeSecurityFailure.changed && !securityRecheckFailure.succeeded &&
            api.active && !failedSecurityRecheck.Acquired(),
            "runtime security-query failure attempts to restore the Windows saver");

        api = FakeScreenSaverActivationApi{};
        api.active = true;
        api.secure = true;
        ScreenSaverActivationLease protectedSaver;
        const auto protectedBegin = protectedSaver.Begin(api);
        Expect(protectedBegin.succeeded && protectedBegin.securityProtected &&
            !protectedSaver.Acquired() && api.active && api.setCalls == 0,
            "password-protected Windows screen saver is never suppressed");
        api.secure = false;
        const auto protectionRemoved = protectedSaver.Reassert(api);
        Expect(protectionRemoved.succeeded && protectionRemoved.changed &&
            protectedSaver.Acquired() && !api.active,
            "screen-saver suppression begins after password protection is removed");
        api.secure = true;
        const auto protectionAdded = protectedSaver.Reassert(api);
        Expect(protectionAdded.succeeded && protectionAdded.securityProtected &&
            protectionAdded.changed && api.active && !protectedSaver.Acquired(),
            "enabling password protection during runtime restores Windows screen saver immediately");

        api = FakeScreenSaverActivationApi{};
        api.active = true;
        api.secureQuerySucceeds = false;
        ScreenSaverActivationLease unknownSecurity;
        const auto securityQueryFailure = unknownSecurity.Begin(api);
        Expect(!securityQueryFailure.succeeded && !unknownSecurity.Acquired() &&
            api.active && api.setCalls == 0,
            "failed screen-saver security query fails closed without suppressing it");
    }

    void TestBrowserVideoGuardReport()
    {
        const BrowserVideoStatusMessage active{
            BrowserVideoProtocolVersion, BrowserVideoMonitoringFlag, 1 };
        const BrowserVideoStatusMessage monitoringNoVideo{
            BrowserVideoProtocolVersion, BrowserVideoMonitoringFlag, 0 };
        const BrowserVideoStatusMessage stopped{
            BrowserVideoProtocolVersion, 0, 0 };
        const BrowserVideoStatusMessage unsupportedVersion{ 2, BrowserVideoMonitoringFlag, 1 };
        const BrowserVideoStatusMessage excessiveFrames{
            BrowserVideoProtocolVersion, BrowserVideoMonitoringFlag, 1025 };
        const BrowserVideoStatusMessage inconsistent{
            BrowserVideoProtocolVersion, 0, 1 };

        Expect(IsValidBrowserVideoStatus(active) && IsBrowserVideoReportActive(active, 100, 5100),
            "fresh extension report with visible advancing frames blocks the app saver");
        Expect(!IsBrowserVideoReportActive(active, 100, 5101),
            "stale browser video reports expire and restore normal saver eligibility");
        Expect(!IsBrowserVideoReportActive(active, 100, 99),
            "monotonic clock rollback cannot make an old browser report fresh");
        Expect(IsValidBrowserVideoStatus(monitoringNoVideo) &&
            !IsBrowserVideoReportActive(monitoringNoVideo, 100, 200),
            "browser monitoring without an advancing visible frame does not block the saver");
        Expect(IsValidBrowserVideoStatus(stopped) &&
            !IsBrowserVideoReportActive(stopped, 100, 200),
            "stopped extension monitoring cannot keep the saver blocked");
        Expect(!IsValidBrowserVideoStatus(unsupportedVersion) &&
            !IsValidBrowserVideoStatus(excessiveFrames) &&
            !IsValidBrowserVideoStatus(inconsistent),
            "invalid browser protocol versions, counts and flags are rejected");
    }

    void TestSettingsWindowAndCleanup()
    {
        INITCOMMONCONTROLSEX controls{ sizeof(controls) };
        controls.dwICC = ICC_WIN95_CLASSES;
        const bool controlsReady = InitCommonControlsEx(&controls) != FALSE;
        Expect(controlsReady, "native Settings common controls initialize");
        if (!controlsReady) return;

        SettingsUiFixture fixture;
        fixture.settings.language = AppLanguage::Turkish;
        fixture.settings.autoScreenSaverEnabled = true;
        fixture.settings.hibernateHotkeyEnabled = true;
        SettingsHost host{
            .context = &fixture,
            .getSettings = UiGetSettings,
            .updateSettings = UiUpdateSettings,
            .refreshHibernate = UiRefreshHibernate,
            .windowsSaverPath = UiWindowsSaverPath,
            .startSaver = UiStartSaver,
            .openSaverSettings = UiOpenSaverSettings,
            .keyboardSuspended = UiKeyboardSuspended
        };

        DWORD handlesBefore{};
        GetProcessHandleCount(GetCurrentProcess(), &handlesBefore);
        const DWORD userObjectsBefore = GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS);
        bool allWindowsCreated = true;
        bool hibernateGated = true;
        bool statusExplainsUnavailable = true;
        bool languageSwitchRebuildsText = true;
        bool singleScrollablePage = true;
        bool normalControlsEnabled = true;
        bool normalControlsPersist = true;
        bool allGroupsPresent = true;
        bool verticalScrollingWorks = true;
        bool scrollbarCommandsWork = true;
        bool mouseWheelScrollingWorks = true;
        bool keyboardScrollingWorks = true;
        bool childrenStayInLogicalCoordinates = true;
        bool childCountStableDuringScroll = true;
        bool narrowWindowLayoutFits = true;
        bool responsiveGeometryFits = true;
        bool dialogKeyboardStylesPresent = true;
        bool footerAccessibleAfterScroll = true;
        bool focusScrollsIntoView = true;
        bool tabSequenceValid = true;
        bool localizedGeometryValid = true;
        bool initialWindowFitsWorkArea = true;
        bool dpiWindowFitsWorkArea = true;
        unsigned int childCount = 0;
        DWORD handlesAfterWarmup = 0;
        constexpr unsigned int repetitions = 12;
        for (unsigned int index = 0; index < repetitions; ++index)
        {
            HWND window = CreateSettingsWindow(GetModuleHandleW(nullptr), nullptr, &host);
            if (window == nullptr)
            {
                allWindowsCreated = false;
                break;
            }
            if (index == 0) initialWindowFitsWorkArea = WindowFitsMonitorWorkArea(window);
            unsigned int currentChildren = 0;
            EnumChildWindows(window, CountChildWindow, reinterpret_cast<LPARAM>(&currentChildren));
            childCount = currentChildren;
            HWND content = FindWindowExW(window, nullptr,
                L"ClassicDesktopScreensaver.Native.Settings.Content", nullptr);
            HWND surface = content == nullptr ? nullptr : FindWindowExW(content, nullptr,
                L"ClassicDesktopScreensaver.Native.Settings.Surface", nullptr);
            if (content != nullptr) ResizeSettingsContentToClient(window, content);
            if (index == 0)
                tabSequenceValid = SettingsTabSequenceExists(window, 12);
            HWND hibernateEnabled = surface == nullptr ? nullptr :
                GetDlgItem(surface, static_cast<int>(SettingsControlId::HibernateHotkeyEnabled));
            HWND hibernateEdit = surface == nullptr ? nullptr :
                GetDlgItem(surface, static_cast<int>(SettingsControlId::HibernateHotkey));
            HWND hibernateStatus = surface == nullptr ? nullptr :
                GetDlgItem(surface, static_cast<int>(SettingsControlId::HibernateStatus));
            HWND autoGroup = surface == nullptr ? nullptr :
                GetDlgItem(surface, static_cast<int>(SettingsControlId::AutomaticGroup));
            HWND saverGroup = surface == nullptr ? nullptr :
                GetDlgItem(surface, static_cast<int>(SettingsControlId::ScreenSaverGroup));
            HWND guardsGroup = surface == nullptr ? nullptr :
                GetDlgItem(surface, static_cast<int>(SettingsControlId::GuardsGroup));
            HWND cornerGroup = surface == nullptr ? nullptr :
                GetDlgItem(surface, static_cast<int>(SettingsControlId::HotCornerGroup));
            HWND hotkeysGroup = surface == nullptr ? nullptr :
                GetDlgItem(surface, static_cast<int>(SettingsControlId::HotkeysGroup));
            HWND behaviorGroup = surface == nullptr ? nullptr :
                GetDlgItem(surface, static_cast<int>(SettingsControlId::BehaviorGroup));
            allGroupsPresent = allGroupsPresent && saverGroup != nullptr && autoGroup != nullptr &&
                guardsGroup != nullptr && cornerGroup != nullptr && hotkeysGroup != nullptr && behaviorGroup != nullptr;
            singleScrollablePage = singleScrollablePage && content != nullptr &&
                surface != nullptr &&
                FindWindowExW(content, nullptr, WC_TABCONTROLW, nullptr) == nullptr &&
                (GetWindowLongPtrW(content, GWL_STYLE) & WS_VSCROLL) != 0;
            if (index == 0 && content != nullptr && surface != nullptr && saverGroup != nullptr)
            {
                SCROLLINFO scroll{ sizeof(scroll) };
                scroll.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
                const bool scrollRangeAvailable = GetScrollInfo(content, SB_VERT, &scroll) != FALSE &&
                    scroll.nMax > static_cast<int>(scroll.nPage);
                HWND autoCheck = GetDlgItem(surface, 108);
                RECT logicalBefore{};
                RECT viewportBefore{};
                RECT surfaceBefore{};
                const bool startCoordinatesAvailable = GetWindowRectRelativeTo(autoCheck, surface, logicalBefore) &&
                    GetWindowRectRelativeTo(autoCheck, content, viewportBefore) &&
                    GetWindowRectRelativeTo(surface, content, surfaceBefore);
                RECT groupBefore{};
                GetWindowRect(saverGroup, &groupBefore);
                POINT beforePoints[2] = { { groupBefore.left, groupBefore.top },
                    { groupBefore.right, groupBefore.bottom } };
                MapWindowPoints(nullptr, content, beforePoints, 2);
                SendMessageW(content, WM_VSCROLL, MAKEWPARAM(SB_PAGEDOWN, 0), 0);
                RECT groupAfter{};
                GetWindowRect(saverGroup, &groupAfter);
                POINT afterPoints[2] = { { groupAfter.left, groupAfter.top },
                    { groupAfter.right, groupAfter.bottom } };
                MapWindowPoints(nullptr, content, afterPoints, 2);
                verticalScrollingWorks = verticalScrollingWorks && scrollRangeAvailable &&
                    afterPoints[0].y < beforePoints[0].y;
                RECT logicalAfter{};
                RECT viewportAfter{};
                RECT surfaceAfter{};
                const bool afterCoordinatesAvailable = GetWindowRectRelativeTo(autoCheck, surface, logicalAfter) &&
                    GetWindowRectRelativeTo(autoCheck, content, viewportAfter) &&
                    GetWindowRectRelativeTo(surface, content, surfaceAfter);
                childrenStayInLogicalCoordinates = childrenStayInLogicalCoordinates &&
                    startCoordinatesAvailable && afterCoordinatesAvailable &&
                    EqualRect(&logicalBefore, &logicalAfter) &&
                    viewportAfter.top - viewportBefore.top == surfaceAfter.top - surfaceBefore.top &&
                    surfaceAfter.top < surfaceBefore.top;
                unsigned int childrenAfterScroll{};
                EnumChildWindows(window, CountChildWindow, reinterpret_cast<LPARAM>(&childrenAfterScroll));
                childCountStableDuringScroll = childCountStableDuringScroll && childrenAfterScroll == childCount;

                SCROLLINFO scrolledInfo{ sizeof(scrolledInfo) };
                scrolledInfo.fMask = SIF_POS;
                SendMessageW(content, WM_VSCROLL, MAKEWPARAM(SB_PAGEDOWN, 0), 0);
                scrollbarCommandsWork = scrollbarCommandsWork && GetScrollInfo(content, SB_VERT, &scrolledInfo) &&
                    scrolledInfo.nPos > 0;
                SendMessageW(content, WM_VSCROLL, MAKEWPARAM(SB_TOP, 0), 0);

                SendMessageW(autoCheck, WM_MOUSEWHEEL,
                    MAKEWPARAM(0, static_cast<WORD>(-WHEEL_DELTA)), 0);
                scrolledInfo.nPos = 0;
                mouseWheelScrollingWorks = mouseWheelScrollingWorks &&
                    GetScrollInfo(content, SB_VERT, &scrolledInfo) && scrolledInfo.nPos > 0;
                SendMessageW(content, WM_VSCROLL, MAKEWPARAM(SB_TOP, 0), 0);

                SendMessageW(autoCheck, WM_KEYDOWN, VK_NEXT, 0);
                scrolledInfo.nPos = 0;
                keyboardScrollingWorks = keyboardScrollingWorks &&
                    GetScrollInfo(content, SB_VERT, &scrolledInfo) && scrolledInfo.nPos > 0;
                SendMessageW(autoCheck, WM_KEYDOWN, VK_HOME, 0);
                scrolledInfo.nPos = -1;
                keyboardScrollingWorks = keyboardScrollingWorks &&
                    GetScrollInfo(content, SB_VERT, &scrolledInfo) && scrolledInfo.nPos == 0;

                SendMessageW(content, WM_VSCROLL, MAKEWPARAM(SB_TOP, 0), 0);
                HWND lastTabStop = GetDlgItem(surface, 134);
                if (lastTabStop != nullptr) SendMessageW(lastTabStop, WM_SETFOCUS, 0, 0);
                RECT focusBounds{};
                RECT focusClient{};
                if (lastTabStop != nullptr) GetWindowRect(lastTabStop, &focusBounds);
                POINT focusPoints[2] = { { focusBounds.left, focusBounds.top },
                    { focusBounds.right, focusBounds.bottom } };
                MapWindowPoints(nullptr, content, focusPoints, 2);
                GetClientRect(content, &focusClient);
                focusScrollsIntoView = focusScrollsIntoView && lastTabStop != nullptr &&
                    focusPoints[0].y >= 0 && focusPoints[1].y <= focusClient.bottom;
                SendMessageW(content, WM_VSCROLL, MAKEWPARAM(SB_TOP, 0), 0);
                SendMessageW(content, WM_VSCROLL, MAKEWPARAM(SB_BOTTOM, 0), 0);
                HWND hideButton = GetDlgItem(surface, 134);
                RECT hideBounds{};
                RECT visiblePage{};
                GetWindowRect(hideButton, &hideBounds);
                POINT hidePoints[2] = { { hideBounds.left, hideBounds.top }, { hideBounds.right, hideBounds.bottom } };
                MapWindowPoints(nullptr, content, hidePoints, 2);
                GetClientRect(content, &visiblePage);
                footerAccessibleAfterScroll = footerAccessibleAfterScroll && hideButton != nullptr &&
                    hidePoints[0].y >= 0 && hidePoints[1].y <= visiblePage.bottom;
                SendMessageW(content, WM_VSCROLL, MAKEWPARAM(SB_TOP, 0), 0);

                SetWindowPos(window, nullptr, 0, 0, 650, 680,
                    SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
                ResizeSettingsContentToClient(window, content);
                RECT contentClient{};
                RECT narrowGroup{};
                GetClientRect(content, &contentClient);
                GetWindowRect(saverGroup, &narrowGroup);
                POINT narrowPoints[2] = { { narrowGroup.left, narrowGroup.top },
                    { narrowGroup.right, narrowGroup.bottom } };
                MapWindowPoints(nullptr, content, narrowPoints, 2);
                narrowWindowLayoutFits = narrowPoints[1].x <= contentClient.right;

                dialogKeyboardStylesPresent =
                    (GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_CONTROLPARENT) != 0 &&
                    (GetWindowLongPtrW(content, GWL_EXSTYLE) & WS_EX_CONTROLPARENT) != 0 &&
                    (GetWindowLongPtrW(surface, GWL_EXSTYLE) & WS_EX_CONTROLPARENT) != 0 &&
                    (GetWindowLongPtrW(GetDlgItem(surface, 104), GWL_STYLE) & WS_TABSTOP) != 0 &&
                    (GetWindowLongPtrW(GetDlgItem(surface, 130), GWL_STYLE) & WS_TABSTOP) != 0;

                SendMessageW(content, WM_VSCROLL, MAKEWPARAM(SB_TOP, 0), 0);
                for (const UINT dpi : { 96u, 120u, 144u, 168u })
                {
                    const auto scale = [dpi](int dips) { return MulDiv(dips, static_cast<int>(dpi), 96); };
                    RECT suggested{ 40, 40, 40 + scale(720), 40 + scale(650) };
                    SendMessageW(window, WM_DPICHANGED, MAKELONG(dpi, dpi),
                        reinterpret_cast<LPARAM>(&suggested));
                    ResizeSettingsContentToClient(window, content);
                    dpiWindowFitsWorkArea = dpiWindowFitsWorkArea && WindowFitsMonitorWorkArea(window);
                    for (const int widthDips : { 500, 720 })
                    {
                        SetWindowPos(window, nullptr, 0, 0, scale(widthDips), scale(620),
                            SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
                        ResizeSettingsContentToClient(window, content);
                        SendMessageW(content, WM_VSCROLL, MAKEWPARAM(SB_TOP, 0), 0);
                        responsiveGeometryFits = responsiveGeometryFits && SettingsGeometryIsValid(window);
                        SCROLLINFO dpiScroll{ sizeof(dpiScroll) };
                        dpiScroll.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
                        verticalScrollingWorks = verticalScrollingWorks &&
                            GetScrollInfo(content, SB_VERT, &dpiScroll) != FALSE &&
                            dpiScroll.nMax > static_cast<int>(dpiScroll.nPage);
                    }
                }
            }
            HWND autoCheck = surface == nullptr ? nullptr : GetDlgItem(surface, 108);
            HWND languageControl = surface == nullptr ? nullptr : GetDlgItem(surface,
                static_cast<int>(SettingsControlId::Language));
            HWND restoreDefaults = surface == nullptr ? nullptr : GetDlgItem(surface, 132);
            normalControlsEnabled = normalControlsEnabled &&
                autoCheck != nullptr && IsWindowEnabled(autoCheck) != FALSE &&
                languageControl != nullptr && IsWindowEnabled(languageControl) != FALSE &&
                restoreDefaults != nullptr && IsWindowEnabled(restoreDefaults) != FALSE;
            hibernateGated = hibernateGated && hibernateEnabled != nullptr &&
                hibernateEdit != nullptr && IsWindowEnabled(hibernateEnabled) != FALSE &&
                IsWindowEnabled(hibernateEdit) == FALSE &&
                SendMessageW(hibernateEnabled, BM_GETCHECK, 0, 0) == BST_CHECKED;
            const int statusLength = hibernateStatus == nullptr ? 0 : GetWindowTextLengthW(hibernateStatus);
            statusExplainsUnavailable = statusExplainsUnavailable && statusLength > 0;
            if (index == 0)
            {
                HWND language = languageControl;
                wchar_t groupText[128]{};
                if (language == nullptr || saverGroup == nullptr || autoCheck == nullptr)
                {
                    languageSwitchRebuildsText = false;
                    normalControlsPersist = false;
                }
                else
                {
                    SendMessageW(autoCheck, BM_SETCHECK, BST_UNCHECKED, 0);
                    SendMessageW(content, WM_COMMAND,
                        MAKEWPARAM(108, BN_CLICKED), reinterpret_cast<LPARAM>(autoCheck));
                    normalControlsPersist = !fixture.settings.autoScreenSaverEnabled &&
                        fixture.updateCalls == 1;
                    GetWindowTextW(saverGroup, groupText, static_cast<int>(std::size(groupText)));
                    SendMessageW(language, CB_SETCURSEL, 2, 0);
                    SendMessageW(content, WM_COMMAND,
                        MAKEWPARAM(static_cast<int>(SettingsControlId::Language), CBN_SELCHANGE),
                        reinterpret_cast<LPARAM>(language));
                    content = FindWindowExW(window, nullptr,
                        L"ClassicDesktopScreensaver.Native.Settings.Content", nullptr);
                    saverGroup = surface == nullptr ? nullptr : GetDlgItem(surface,
                        static_cast<int>(SettingsControlId::ScreenSaverGroup));
                    wchar_t translatedGroupText[128]{};
                    if (saverGroup != nullptr)
                        GetWindowTextW(saverGroup, translatedGroupText,
                            static_cast<int>(std::size(translatedGroupText)));
                    languageSwitchRebuildsText = fixture.settings.language == AppLanguage::English &&
                        std::wstring(translatedGroupText) == L"Screen saver" &&
                        std::wstring(groupText) != std::wstring(translatedGroupText);
                    ResizeSettingsContentToClient(window, content);
                    localizedGeometryValid = SettingsGeometryIsValid(window);
                }
            }
            DestroyWindow(window);
            if (index == 0) GetProcessHandleCount(GetCurrentProcess(), &handlesAfterWarmup);
        }

        DWORD handlesAfter{};
        GetProcessHandleCount(GetCurrentProcess(), &handlesAfter);
        const DWORD userObjectsAfter = GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS);
        Expect(allWindowsCreated, "native Settings window creates without showing it");
        Expect(childCount >= 45, "native Settings window builds the complete one-page settings layout");
        Expect(singleScrollablePage && allGroupsPresent,
            "Settings shows all C#-matching groups on one vertically scrollable page with no tabs");
        Expect(verticalScrollingWorks,
            "one-page Settings viewport pages through its content surface");
        Expect(scrollbarCommandsWork,
            "vertical scrollbar page commands move the content surface once");
        Expect(mouseWheelScrollingWorks,
            "mouse-wheel events from child controls reach the viewport scrollbar");
        Expect(keyboardScrollingWorks,
            "Page Down and Home keyboard commands scroll from focused non-text controls");
        Expect(childrenStayInLogicalCoordinates && childCountStableDuringScroll,
            "scrolling moves the single content surface while child HWND coordinates and count stay stable");
        Expect(footerAccessibleAfterScroll,
            "bottom action buttons are reachable at the end of the scroll range");
        Expect(narrowWindowLayoutFits,
            "stretchable settings groups remain inside the minimum-width window");
        Expect(initialWindowFitsWorkArea && dpiWindowFitsWorkArea,
            "Settings opens and applies DPI size recommendations inside the monitor work area");
        Expect(responsiveGeometryFits,
            "settings controls do not overlap or exceed the client width at 100, 125, 150 and 175 percent DPI");
        Expect(dialogKeyboardStylesPresent,
            "Settings exposes a nested tab-navigation hierarchy and tab stops");
        Expect(tabSequenceValid,
            "tab navigation walks the Settings controls without leaving the page");
        Expect(focusScrollsIntoView,
            "keyboard focus scrolls a lower control into the visible client area");
        Expect(normalControlsEnabled,
            "normal mode leaves regular settings and restore-default controls enabled");
        Expect(normalControlsPersist,
            "normal mode routes an enabled setting change through the host for persistence");
        Expect(hibernateGated,
            "unavailable S4 disables the shortcut editor but keeps its enable preference accessible");
        Expect(statusExplainsUnavailable, "Settings shows a Hibernate unavailable explanation");
        Expect(languageSwitchRebuildsText, "Settings language selection rebuilds localized controls");
        Expect(localizedGeometryValid,
            "localized control rebuild retains collision-free Settings geometry");
        Expect(fixture.updateCalls == 2, "normal Settings smoke test persists only its explicit test setting and language changes");
        Expect(handlesAfterWarmup != 0 && handlesAfter <= handlesAfterWarmup + 1,
            "repeated Settings create/destroy does not leak process handles");
        Expect(userObjectsAfter <= userObjectsBefore + 1,
            "repeated Settings create/destroy does not leak USER objects");

        SettingsUiFixture readOnlyFixture;
        readOnlyFixture.settings.language = AppLanguage::Turkish;
        SettingsHost readOnlyHost{
            .context = &readOnlyFixture,
            .getSettings = UiGetSettings,
            .updateSettings = UiUpdateSettings,
            .refreshHibernate = UiRefreshHibernate,
            .windowsSaverPath = UiWindowsSaverPath,
            .startSaver = UiStartSaver,
            .openSaverSettings = UiOpenSaverSettings,
            .keyboardSuspended = UiKeyboardSuspended,
            .readOnly = true
        };
        HWND readOnlyWindow = CreateSettingsWindow(
            GetModuleHandleW(nullptr), nullptr, &readOnlyHost);
        const bool readOnlyWindowCreated = readOnlyWindow != nullptr;
        if (readOnlyWindowCreated)
        {
            SendMessageW(readOnlyWindow, WM_SHOWWINDOW, TRUE, 0);
        }
        HWND readOnlyContent = readOnlyWindowCreated ? FindWindowExW(readOnlyWindow, nullptr,
            L"ClassicDesktopScreensaver.Native.Settings.Content", nullptr) : nullptr;
        HWND readOnlySurface = readOnlyContent == nullptr ? nullptr : FindWindowExW(readOnlyContent, nullptr,
            L"ClassicDesktopScreensaver.Native.Settings.Surface", nullptr);
        const bool readOnlyGeometryValid = readOnlyContent != nullptr &&
            ResizeSettingsContentToClient(readOnlyWindow, readOnlyContent) &&
            SettingsGeometryIsValid(readOnlyWindow);
        wchar_t safeCaption[160]{};
        if (readOnlyWindowCreated)
            GetWindowTextW(readOnlyWindow, safeCaption, static_cast<int>(std::size(safeCaption)));
        const bool safeModeVisibleToUser = readOnlyWindowCreated &&
            std::wstring(safeCaption).find(L"Güvenli UI Testi (Salt Okunur)") != std::wstring::npos;
        const bool pageActionsDisabled = readOnlySurface != nullptr &&
            IsWindowEnabled(GetDlgItem(readOnlySurface, 104)) == FALSE &&
            IsWindowEnabled(GetDlgItem(readOnlySurface, 105)) == FALSE &&
            IsWindowEnabled(GetDlgItem(readOnlySurface, 130)) == FALSE &&
            IsWindowEnabled(GetDlgItem(readOnlySurface, 132)) == FALSE &&
            IsWindowEnabled(GetDlgItem(readOnlySurface, 133)) != FALSE &&
            IsWindowEnabled(GetDlgItem(readOnlySurface, 134)) != FALSE;
        const bool navigationAndCloseEnabled = readOnlySurface != nullptr &&
            IsWindowEnabled(GetDlgItem(readOnlySurface, 133)) != FALSE &&
            IsWindowEnabled(GetDlgItem(readOnlySurface, 134)) != FALSE;
        if (readOnlyWindowCreated)
        {
            SendMessageW(readOnlyContent, WM_COMMAND, MAKEWPARAM(105, BN_CLICKED), 0);
            DestroyWindow(readOnlyWindow);
        }
        Expect(readOnlyWindowCreated, "safe UI test mode can create its read-only Settings window");
        Expect(readOnlyGeometryValid,
            "safe UI test mode preserves the same collision-free layout while controls are disabled");
        Expect(safeModeVisibleToUser,
            "safe UI test mode is identified in the Settings window title");
        Expect(pageActionsDisabled, "safe UI test mode disables Settings actions and editable controls");
        Expect(navigationAndCloseEnabled, "safe UI test mode keeps About and minimize-to-tray controls active");
        Expect(readOnlyFixture.updateCalls == 0 && readOnlyFixture.saverCalls == 0,
            "safe UI test mode cannot persist settings or start a screen saver");
    }

    void TestCustomScreenSaverReenableAndHibernateControl()
    {
        SettingsUiFixture fixture;
        fixture.settings.language = AppLanguage::Turkish;
        fixture.settings.useCustomScreenSaver = true;
        fixture.settings.screenSaverPath = L"C:\\Screens\\custom.scr";
        fixture.settings.hibernateHotkeyEnabled = true;
        SettingsHost host{
            .context = &fixture,
            .getSettings = UiGetSettings,
            .updateSettings = UiUpdateSettings,
            .refreshHibernate = UiRefreshHibernate,
            .windowsSaverPath = UiWindowsSaverPath,
            .startSaver = UiStartSaver,
            .openSaverSettings = UiOpenSaverSettings,
            .keyboardSuspended = UiKeyboardSuspended,
            .readOnly = false
        };
        HWND window = CreateSettingsWindow(GetModuleHandleW(nullptr), nullptr, &host);
        if (window == nullptr)
        {
            Expect(false, "custom screen saver toggle test creates Settings");
            return;
        }
        ShowSettingsWindow(window);
        HWND content = FindWindowExW(window, nullptr,
            L"ClassicDesktopScreensaver.Native.Settings.Content", nullptr);
        HWND surface = content == nullptr ? nullptr : FindWindowExW(content, nullptr,
            L"ClassicDesktopScreensaver.Native.Settings.Surface", nullptr);
        HWND customCheck = surface == nullptr ? nullptr : GetDlgItem(surface, 102);
        HWND customPath = surface == nullptr ? nullptr : GetDlgItem(surface, 103);
        HWND testButton = surface == nullptr ? nullptr : GetDlgItem(surface, 105);
        HWND hibernateCheck = surface == nullptr ? nullptr : GetDlgItem(surface,
            static_cast<int>(SettingsControlId::HibernateHotkeyEnabled));
        HWND hibernateEdit = surface == nullptr ? nullptr : GetDlgItem(surface,
            static_cast<int>(SettingsControlId::HibernateHotkey));

        const bool initialCustomState = customCheck != nullptr && customPath != nullptr &&
            testButton != nullptr && SendMessageW(customCheck, BM_GETCHECK, 0, 0) == BST_CHECKED &&
            IsWindowEnabled(customPath) != FALSE && IsWindowEnabled(testButton) != FALSE;
        if (customCheck != nullptr) SendMessageW(customCheck, BM_CLICK, 0, 0);
        const bool disabledCustomState = fixture.updateCalls == 1 &&
            !fixture.settings.useCustomScreenSaver &&
            fixture.settings.screenSaverPath == L"C:\\Screens\\custom.scr" &&
            SendMessageW(customCheck, BM_GETCHECK, 0, 0) == BST_UNCHECKED &&
            IsWindowEnabled(customCheck) != FALSE && IsWindowEnabled(customPath) == FALSE &&
            IsWindowEnabled(testButton) == FALSE;
        if (customCheck != nullptr) SendMessageW(customCheck, BM_CLICK, 0, 0);
        const bool reenabledCustomState = fixture.updateCalls == 2 &&
            fixture.settings.useCustomScreenSaver &&
            fixture.settings.screenSaverPath == L"C:\\Screens\\custom.scr" &&
            SendMessageW(customCheck, BM_GETCHECK, 0, 0) == BST_CHECKED &&
            IsWindowEnabled(customPath) != FALSE && IsWindowEnabled(testButton) != FALSE;

        const bool hibernateCanBeDisabledUnavailable = hibernateCheck != nullptr &&
            hibernateEdit != nullptr && IsWindowEnabled(hibernateCheck) != FALSE &&
            IsWindowEnabled(hibernateEdit) == FALSE &&
            SendMessageW(hibernateCheck, BM_GETCHECK, 0, 0) == BST_CHECKED;
        if (hibernateCheck != nullptr) SendMessageW(hibernateCheck, BM_CLICK, 0, 0);
        const bool hibernatePreferenceChanged = fixture.updateCalls == 3 &&
            !fixture.settings.hibernateHotkeyEnabled &&
            IsWindowEnabled(hibernateCheck) != FALSE;

        DestroyWindow(window);
        Expect(initialCustomState && disabledCustomState && reenabledCustomState,
            "custom .scr can be turned off and on again without losing its saved path");
        Expect(hibernateCanBeDisabledUnavailable && hibernatePreferenceChanged,
            "the Hibernate shortcut can be disabled while live S4 capability is unavailable");
        Expect(fixture.saverCalls == 0,
            "custom saver and hibernate preference UI checks invoke no saver or power operation");
    }

    void TestSettingsPopulationDoesNotTriggerRepeatedSaves()
    {
        SettingsUiFixture fixture;
        fixture.settings.language = AppLanguage::Turkish;
        SettingsHost host{
            .context = &fixture,
            .getSettings = UiGetSettings,
            .updateSettings = UiUpdateSettings,
            .refreshHibernate = UiRefreshHibernate,
            .windowsSaverPath = UiWindowsSaverPath,
            .startSaver = UiStartSaver,
            .openSaverSettings = UiOpenSaverSettings,
            .keyboardSuspended = UiKeyboardSuspended,
            .readOnly = false
        };
        HWND window = CreateSettingsWindow(GetModuleHandleW(nullptr), nullptr, &host);
        if (window == nullptr)
        {
            Expect(false, "Settings initialization can be observed without starting application actions");
            return;
        }

        ShowSettingsWindow(window);
        PumpUiMessagesFor(std::chrono::milliseconds(1250));
        const bool initialPopulationWasReadOnly = fixture.updateCalls == 0;
        DestroyWindow(window);

        Expect(initialPopulationWasReadOnly,
            "initial Settings population does not schedule periodic saves or reset the idle countdown");
        Expect(fixture.saverCalls == 0,
            "Settings population regression test does not launch a screen saver or power action");
    }

    void TestWindowsScreenSaverReadOnlyQuery()
    {
        ScreenSaverService service;
        const auto snapshot = service.ReadWindowsScreenSaverSnapshot();
        std::printf("Windows screensaver snapshot: active_query=%d active=%d active_error=%lu timeout_query=%d timeout_s=%u timeout_error=%lu running_query=%d running=%d running_error=%lu selected_configured=%d\n",
            snapshot.activeQuerySucceeded ? 1 : 0,
            snapshot.active ? 1 : 0,
            snapshot.activeQueryError,
            snapshot.timeoutQuerySucceeded ? 1 : 0,
            snapshot.timeoutSeconds,
            snapshot.timeoutQueryError,
            snapshot.runningQuerySucceeded ? 1 : 0,
            snapshot.running ? 1 : 0,
            snapshot.runningQueryError,
            snapshot.selectedSaverConfigured ? 1 : 0);
        Expect(snapshot.activeQuerySucceeded && snapshot.timeoutQuerySucceeded &&
            snapshot.runningQuerySucceeded,
            "Windows screen saver active, timeout and running state can be queried read-only");
    }

    void TestDirectScreenSaverLaunchAndProcessTracking()
    {
        wchar_t executablePath[32768]{};
        const DWORD pathLength = GetModuleFileNameW(
            nullptr, executablePath, static_cast<DWORD>(std::size(executablePath)));
        if (pathLength == 0 || pathLength >= std::size(executablePath))
        {
            Expect(false, "direct screen saver launch test reads its executable path");
            return;
        }

        ScreenSaverService service;
        std::wstring error;
        DWORD failureCode = ERROR_SUCCESS;
        classic_desktop_screensaver::ScreenSaverLaunchDetails launch{};
        const bool started = service.Start(
            executablePath, error, AppLanguage::Auto, &failureCode, &launch);
        Expect(started && failureCode == ERROR_SUCCESS && launch.processId != 0 &&
            launch.processHandleAvailable && !launch.reusedExistingProcess,
            "screen saver executable is started directly with an observable child process");
        if (!started) return;

        classic_desktop_screensaver::ScreenSaverProcessObservation observation{};
        for (unsigned int attempt = 0; attempt < 200; ++attempt)
        {
            observation = service.ObserveProcess();
            if (observation.state == classic_desktop_screensaver::ScreenSaverProcessState::Exited ||
                observation.state == classic_desktop_screensaver::ScreenSaverProcessState::Failed)
                break;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        Expect(observation.state == classic_desktop_screensaver::ScreenSaverProcessState::Exited &&
            observation.processId == launch.processId && observation.exitCode == EXIT_SUCCESS,
            "screen saver process exit code is observed without launching a real saver");
    }

    void TestLiveHibernateCapabilityQuery()
    {
        const HibernateAvailability availability = QueryHibernateAvailability();
        const char* label = availability == HibernateAvailability::Available
            ? "available"
            : availability == HibernateAvailability::Unsupported
                ? "unavailable"
                : "query_failed";
        std::printf("Live Hibernate capability (current test account): %s\n", label);
        Expect(availability != HibernateAvailability::QueryFailed,
            "GetPwrCapabilities can determine current S4 and hibernation-file status read-only");
    }

    void TestHibernateStatusRefreshWhileSettingsOpen()
    {
        INITCOMMONCONTROLSEX controls{ sizeof(controls) };
        controls.dwICC = ICC_WIN95_CLASSES;
        if (InitCommonControlsEx(&controls) == FALSE)
        {
            Expect(false, "Hibernate status refresh initializes common controls");
            return;
        }

        SettingsUiFixture fixture;
        fixture.settings.language = AppLanguage::Turkish;
        fixture.settings.hibernateHotkeyEnabled = true;
        fixture.hibernateAvailability = HibernateAvailability::Available;
        SettingsHost host{
            .context = &fixture,
            .getSettings = UiGetSettings,
            .updateSettings = UiUpdateSettings,
            .refreshHibernate = UiRefreshHibernate,
            .windowsSaverPath = UiWindowsSaverPath,
            .startSaver = UiStartSaver,
            .openSaverSettings = UiOpenSaverSettings,
            .keyboardSuspended = UiKeyboardSuspended
        };
        HWND window = CreateSettingsWindow(GetModuleHandleW(nullptr), nullptr, &host);
        if (window == nullptr)
        {
            Expect(false, "Hibernate status refresh creates the real Settings window");
            return;
        }

        ShowSettingsWindow(window);
        HWND content = FindWindowExW(window, nullptr,
            L"ClassicDesktopScreensaver.Native.Settings.Content", nullptr);
        HWND surface = content == nullptr ? nullptr : FindWindowExW(content, nullptr,
            L"ClassicDesktopScreensaver.Native.Settings.Surface", nullptr);
        HWND hibernateCheck = surface == nullptr ? nullptr : GetDlgItem(surface,
            static_cast<int>(SettingsControlId::HibernateHotkeyEnabled));
        HWND hibernateEdit = surface == nullptr ? nullptr : GetDlgItem(surface,
            static_cast<int>(SettingsControlId::HibernateHotkey));
        HWND hibernateStatus = surface == nullptr ? nullptr : GetDlgItem(surface,
            static_cast<int>(SettingsControlId::HibernateStatus));
        const bool initiallyAvailable = hibernateEdit != nullptr &&
            IsWindowEnabled(hibernateEdit) != FALSE && hibernateStatus != nullptr &&
            GetWindowTextLengthW(hibernateStatus) == 0;

        fixture.hibernateAvailability = HibernateAvailability::Unsupported;
        if (initiallyAvailable) SendMessageW(window, WM_SETTINGCHANGE, 0, 0);
        wchar_t unavailableText[256]{};
        if (hibernateStatus != nullptr)
            GetWindowTextW(hibernateStatus, unavailableText,
                static_cast<int>(std::size(unavailableText)));
        const bool broadcastRefreshesStatus = initiallyAvailable &&
            hibernateCheck != nullptr && IsWindowEnabled(hibernateCheck) != FALSE &&
            SendMessageW(hibernateCheck, BM_GETCHECK, 0, 0) == BST_CHECKED &&
            hibernateEdit != nullptr && IsWindowEnabled(hibernateEdit) == FALSE &&
            std::wstring(unavailableText).find(L"Hazırda Beklet") != std::wstring::npos;

        fixture.hibernateAvailability = HibernateAvailability::Available;
        if (broadcastRefreshesStatus) PumpUiMessagesFor(std::chrono::milliseconds(3200));
        wchar_t availableText[256]{};
        if (hibernateStatus != nullptr)
            GetWindowTextW(hibernateStatus, availableText,
                static_cast<int>(std::size(availableText)));
        const bool timerRestoresStatus = broadcastRefreshesStatus &&
            hibernateEdit != nullptr && IsWindowEnabled(hibernateEdit) != FALSE &&
            std::wstring(availableText).empty();

        DestroyWindow(window);
        Expect(initiallyAvailable,
            "Settings initially shows Hibernate controls enabled when S4 is available");
        Expect(broadcastRefreshesStatus,
            "WM_SETTINGCHANGE immediately refreshes Hibernate status without changing its preference");
        Expect(timerRestoresStatus,
            "visible Settings rechecks S4 capability and restores the editor after a change without a broadcast");
        Expect(fixture.hibernateRefreshCalls >= 3,
            "Hibernate capability is queried on open, configuration notification, and visible refresh");
        Expect(fixture.updateCalls == 0 && fixture.saverCalls == 0,
            "Hibernate status refresh neither saves settings nor starts an action");

        SettingsUiFixture readOnlyFixture;
        readOnlyFixture.settings.language = AppLanguage::Turkish;
        readOnlyFixture.settings.hibernateHotkeyEnabled = true;
        SettingsHost readOnlyHost{
            .context = &readOnlyFixture,
            .getSettings = UiGetSettings,
            .updateSettings = UiUpdateSettings,
            .refreshHibernate = UiRefreshHibernate,
            .windowsSaverPath = UiWindowsSaverPath,
            .startSaver = UiStartSaver,
            .openSaverSettings = UiOpenSaverSettings,
            .keyboardSuspended = UiKeyboardSuspended,
            .readOnly = true
        };
        HWND readOnlyWindow = CreateSettingsWindow(
            GetModuleHandleW(nullptr), nullptr, &readOnlyHost);
        if (readOnlyWindow != nullptr) ShowSettingsWindow(readOnlyWindow);
        HWND readOnlyContent = readOnlyWindow == nullptr ? nullptr : FindWindowExW(
            readOnlyWindow, nullptr, L"ClassicDesktopScreensaver.Native.Settings.Content", nullptr);
        HWND readOnlySurface = readOnlyContent == nullptr ? nullptr : FindWindowExW(
            readOnlyContent, nullptr, L"ClassicDesktopScreensaver.Native.Settings.Surface", nullptr);
        HWND readOnlyHibernateEdit = readOnlySurface == nullptr ? nullptr : GetDlgItem(
            readOnlySurface, static_cast<int>(SettingsControlId::HibernateHotkey));
        readOnlyFixture.hibernateAvailability = HibernateAvailability::Available;
        if (readOnlyWindow != nullptr) SendMessageW(readOnlyWindow, WM_SETTINGCHANGE, 0, 0);
        const bool safeModeRemainsReadOnly = readOnlyHibernateEdit != nullptr &&
            IsWindowEnabled(readOnlyHibernateEdit) == FALSE;
        if (readOnlyWindow != nullptr) DestroyWindow(readOnlyWindow);
        Expect(safeModeRemainsReadOnly,
            "capability refresh never re-enables Hibernate controls in safe read-only mode");
        Expect(readOnlyFixture.updateCalls == 0 && readOnlyFixture.saverCalls == 0,
            "safe-mode Hibernate status refresh performs no settings or screen-saver action");
    }

    void TestHotCornerComboPopulationAndPersistence()
    {
        INITCOMMONCONTROLSEX controls{ sizeof(controls) };
        controls.dwICC = ICC_WIN95_CLASSES;
        if (InitCommonControlsEx(&controls) == FALSE)
        {
            Expect(false, "Hot Corner combo common controls initialize");
            return;
        }

        const std::array<std::wstring_view, 4> turkishLabels = {
            L"Sol üst", L"Sağ üst", L"Sol alt", L"Sağ alt"
        };
        const std::array<std::wstring_view, 4> englishLabels = {
            L"Top left", L"Top right", L"Bottom left", L"Bottom right"
        };
        const std::array<HotCorner, 4> values = {
            HotCorner::TopLeft, HotCorner::TopRight,
            HotCorner::BottomLeft, HotCorner::BottomRight
        };
        const auto optionsMatch = [&values](HWND combo,
            const std::array<std::wstring_view, 4>& expected)
        {
            if (combo == nullptr || IsWindow(combo) == FALSE ||
                SendMessageW(combo, CB_GETCOUNT, 0, 0) != 4)
            {
                return false;
            }
            wchar_t text[64]{};
            for (std::size_t index = 0; index < expected.size(); ++index)
            {
                const LRESULT textLength = SendMessageW(combo, CB_GETLBTEXTLEN,
                    static_cast<WPARAM>(index), 0);
                if (textLength <= 0 || textLength >= static_cast<LRESULT>(std::size(text))) return false;
                const LRESULT copied = SendMessageW(combo, CB_GETLBTEXT,
                    static_cast<WPARAM>(index), reinterpret_cast<LPARAM>(text));
                if (copied != textLength || std::wstring_view(text) != expected[index])
                    return false;
                if (SendMessageW(combo, CB_GETITEMDATA,
                    static_cast<WPARAM>(index), 0) != static_cast<LRESULT>(values[index])) return false;
            }
            return true;
        };

        SettingsUiFixture fixture;
        fixture.settings.language = AppLanguage::Turkish;
        SettingsHost host{
            .context = &fixture,
            .getSettings = UiGetSettings,
            .updateSettings = UiUpdateSettings,
            .refreshHibernate = UiRefreshHibernate,
            .windowsSaverPath = UiWindowsSaverPath,
            .startSaver = UiStartSaver,
            .openSaverSettings = UiOpenSaverSettings,
            .keyboardSuspended = UiKeyboardSuspended
        };
        HWND window = CreateSettingsWindow(GetModuleHandleW(nullptr), nullptr, &host);
        if (window != nullptr) SendMessageW(window, WM_SHOWWINDOW, TRUE, 0);
        HWND content = window == nullptr ? nullptr : FindWindowExW(window, nullptr,
            L"ClassicDesktopScreensaver.Native.Settings.Content", nullptr);
        HWND surface = content == nullptr ? nullptr : FindWindowExW(content, nullptr,
            L"ClassicDesktopScreensaver.Native.Settings.Surface", nullptr);
        constexpr int cornerId = static_cast<int>(classic_desktop_screensaver::SettingsControlId::HotCornerCombo);
        HWND corner = surface == nullptr ? nullptr : GetDlgItem(surface, cornerId);
        const bool turkishOptions = optionsMatch(corner, turkishLabels);
        HWND hotCornerGroup = surface == nullptr ? nullptr : GetDlgItem(surface,
            static_cast<int>(SettingsControlId::HotCornerGroup));
        wchar_t hotCornerHeading[128]{};
        if (hotCornerGroup != nullptr)
            GetWindowTextW(hotCornerGroup, hotCornerHeading,
                static_cast<int>(std::size(hotCornerHeading)));
        const bool turkishHeading = std::wstring(hotCornerHeading) == L"Etkin Köşeler";
        const LRESULT selectedIndex = corner == nullptr ? CB_ERR :
            SendMessageW(corner, CB_GETCURSEL, 0, 0);
        const bool topRightRestored = selectedIndex == 1 &&
            SendMessageW(corner, CB_GETITEMDATA, static_cast<WPARAM>(selectedIndex), 0) ==
                static_cast<LRESULT>(HotCorner::TopRight);

        int bottomLeftIndex = CB_ERR;
        if (corner != nullptr)
        {
            for (int index = 0; index < 4; ++index)
            {
                if (SendMessageW(corner, CB_GETITEMDATA, static_cast<WPARAM>(index), 0) ==
                    static_cast<LRESULT>(HotCorner::BottomLeft))
                {
                    bottomLeftIndex = index;
                    break;
                }
            }
        }
        bool selectionPersisted = false;
        if (surface != nullptr && corner != nullptr && bottomLeftIndex != CB_ERR)
        {
            SendMessageW(corner, CB_SETCURSEL, static_cast<WPARAM>(bottomLeftIndex), 0);
            SendMessageW(surface, WM_COMMAND, MAKEWPARAM(cornerId, CBN_SELCHANGE),
                reinterpret_cast<LPARAM>(corner));
            selectionPersisted = fixture.updateCalls == 1 &&
                fixture.settings.hotCorner == HotCorner::BottomLeft;
        }

        bool englishOptions = false;
        bool englishHeading = false;
        if (surface != nullptr)
        {
            HWND language = GetDlgItem(surface,
                static_cast<int>(classic_desktop_screensaver::SettingsControlId::Language));
            if (language != nullptr)
            {
                SendMessageW(language, CB_SETCURSEL, 2, 0);
                SendMessageW(surface, WM_COMMAND,
                    MAKEWPARAM(static_cast<int>(classic_desktop_screensaver::SettingsControlId::Language), CBN_SELCHANGE),
                    reinterpret_cast<LPARAM>(language));
                corner = GetDlgItem(surface, cornerId);
                englishOptions = optionsMatch(corner, englishLabels);
                hotCornerGroup = GetDlgItem(surface,
                    static_cast<int>(SettingsControlId::HotCornerGroup));
                hotCornerHeading[0] = L'\0';
                if (hotCornerGroup != nullptr)
                    GetWindowTextW(hotCornerGroup, hotCornerHeading,
                        static_cast<int>(std::size(hotCornerHeading)));
                englishHeading = std::wstring(hotCornerHeading) == L"Hot Corners";
            }
        }
        if (window != nullptr) DestroyWindow(window);
        Expect(window != nullptr, "Hot Corner settings window creates for real HWND inspection");
        Expect(turkishOptions, "Hot Corner combo exposes four Turkish labels and stable enum item data");
        Expect(turkishHeading, "Hot Corner group heading is localized as Etkin Köşeler in Turkish");
        Expect(topRightRestored, "Hot Corner combo restores the configured TopRight selection by item data");
        Expect(selectionPersisted, "Hot Corner selection updates the isolated settings fixture");
        Expect(englishOptions, "Hot Corner combo rebuild retains all four English options after localization");
        Expect(englishHeading, "Hot Corner group heading changes to Hot Corners in English");
        Expect(fixture.settings.hotCorner == HotCorner::BottomLeft && fixture.updateCalls == 2,
            "language change retains the selected Hot Corner value without changing its mapping");

        SettingsUiFixture readOnlyFixture;
        readOnlyFixture.settings.language = AppLanguage::Turkish;
        SettingsHost readOnlyHost{
            .context = &readOnlyFixture,
            .getSettings = UiGetSettings,
            .updateSettings = UiUpdateSettings,
            .refreshHibernate = UiRefreshHibernate,
            .windowsSaverPath = UiWindowsSaverPath,
            .startSaver = UiStartSaver,
            .openSaverSettings = UiOpenSaverSettings,
            .keyboardSuspended = UiKeyboardSuspended,
            .readOnly = true
        };
        HWND readOnlyWindow = CreateSettingsWindow(
            GetModuleHandleW(nullptr), nullptr, &readOnlyHost);
        if (readOnlyWindow != nullptr) SendMessageW(readOnlyWindow, WM_SHOWWINDOW, TRUE, 0);
        HWND readOnlyContent = readOnlyWindow == nullptr ? nullptr : FindWindowExW(readOnlyWindow, nullptr,
            L"ClassicDesktopScreensaver.Native.Settings.Content", nullptr);
        HWND readOnlySurface = readOnlyContent == nullptr ? nullptr : FindWindowExW(readOnlyContent, nullptr,
            L"ClassicDesktopScreensaver.Native.Settings.Surface", nullptr);
        HWND readOnlyCorner = readOnlySurface == nullptr ? nullptr : GetDlgItem(readOnlySurface, cornerId);
        const bool safeOptionsRemainVisible = optionsMatch(readOnlyCorner, turkishLabels) &&
            IsWindowEnabled(readOnlyCorner) == FALSE;
        if (readOnlyWindow != nullptr) DestroyWindow(readOnlyWindow);
        Expect(safeOptionsRemainVisible,
            "safe read-only mode keeps Hot Corner options populated while the combo is disabled");
        Expect(readOnlyFixture.updateCalls == 0,
            "safe Hot Corner inspection leaves isolated settings unchanged");
    }

    void TestSettingsHwndVisibilityAndDisplayRecovery()
    {
        SettingsUiFixture fixture;
        fixture.settings.language = AppLanguage::Turkish;
        SettingsHost host{
            .context = &fixture,
            .getSettings = UiGetSettings,
            .updateSettings = UiUpdateSettings,
            .refreshHibernate = UiRefreshHibernate,
            .windowsSaverPath = UiWindowsSaverPath,
            .startSaver = UiStartSaver,
            .openSaverSettings = UiOpenSaverSettings,
            .keyboardSuspended = UiKeyboardSuspended,
            .readOnly = true
        };
        HWND window = CreateSettingsWindow(GetModuleHandleW(nullptr), nullptr, &host);
        if (window == nullptr)
        {
            Expect(false, "visible HWND test creates the real Settings window");
            return;
        }
        ShowSettingsWindow(window);
        HWND content = FindWindowExW(window, nullptr,
            L"ClassicDesktopScreensaver.Native.Settings.Content", nullptr);
        HWND surface = content == nullptr ? nullptr : FindWindowExW(content, nullptr,
            L"ClassicDesktopScreensaver.Native.Settings.Surface", nullptr);
        const LONG_PTR surfaceClassStyle = surface == nullptr ? 0 : GetClassLongPtrW(surface, GCL_STYLE);
        const LONG_PTR surfaceWindowStyle = surface == nullptr ? 0 : GetWindowLongPtrW(surface, GWL_STYLE);
        const LONG_PTR surfaceBackgroundBrush = surface == nullptr ? 0 :
            GetClassLongPtrW(surface, GCLP_HBRBACKGROUND);
        const bool surfaceCompositingEnabled = surface != nullptr &&
            (GetWindowLongPtrW(surface, GWL_EXSTYLE) & WS_EX_COMPOSITED) != 0 &&
            (surfaceClassStyle & (CS_OWNDC | CS_CLASSDC | CS_PARENTDC)) == 0;
        const bool surfacePaintsBehindTransparentChildren = surface != nullptr &&
            (surfaceWindowStyle & WS_CLIPCHILDREN) == 0 && surfaceBackgroundBrush != 0;
        wchar_t tempPath[MAX_PATH]{};
        const DWORD tempLength = GetTempPathW(static_cast<DWORD>(std::size(tempPath)), tempPath);
        const auto reportPath = tempLength != 0 && tempLength < std::size(tempPath)
            ? std::filesystem::path(tempPath) / L"ClassicDesktopScreensaver.Native-HwndTree-test.txt"
            : std::filesystem::path{};
        HwndTreeContext tree{};
        const bool treeCaptured = content != nullptr && !reportPath.empty() &&
            WriteSettingsHwndTree(window, reportPath, &tree);

        unsigned int groupCount{};
        unsigned int groupedControls{};
        bool visibleControlsHaveDrawRegion = treeCaptured;
        bool controlsAreAboveTheirGroupBoxes = treeCaptured;
        bool allChildrenValidAndVisible = treeCaptured;
        bool safeControlsRemainDisabled = treeCaptured;
        bool surfaceOffsetMatchesScrollbar = treeCaptured && tree.surfaceOffsetY == -tree.scrollPosition;
        bool scrollCompletesRepaint = surface != nullptr;
        if (scrollCompletesRepaint)
        {
            ValidateRect(content, nullptr);
            ValidateRect(surface, nullptr);
            EnumChildWindows(surface, ValidateWindowUpdateRegion, 0);
            SendMessageW(content, WM_VSCROLL, MAKEWPARAM(SB_PAGEDOWN, 0), 0);
            RECT contentUpdate{};
            RECT surfaceUpdate{};
            const bool contentInvalidated = GetUpdateRect(content, &contentUpdate, FALSE) != FALSE;
            const bool surfaceInvalidated = GetUpdateRect(surface, &surfaceUpdate, FALSE) != FALSE;
            UpdateRegionCount childUpdates{};
            EnumChildWindows(surface, CountInvalidWindowUpdateRegions,
                reinterpret_cast<LPARAM>(&childUpdates));
            scrollCompletesRepaint = !contentInvalidated && !surfaceInvalidated && childUpdates.windows > 0 &&
                childUpdates.invalid == 0;
            SendMessageW(content, WM_VSCROLL, MAKEWPARAM(SB_TOP, 0), 0);
        }
        std::vector<const HwndTreeRecord*> groups;
        for (const HwndTreeRecord& record : tree.records)
        {
            const bool isGroup = (record.style & BS_TYPEMASK) == BS_GROUPBOX;
            if (isGroup)
            {
                groups.push_back(&record);
                ++groupCount;
            }
            allChildrenValidAndVisible = allChildrenValidAndVisible && record.valid &&
                record.correctParent && record.childStyle && record.visibleStyle && record.visible;
            safeControlsRemainDisabled = safeControlsRemainDisabled &&
                (isGroup || record.controlId == 133 || record.controlId == 134 || !record.enabled);
        }
        for (const HwndTreeRecord& record : tree.records)
        {
            const bool fullyInsideViewport = record.viewportClient.left >= tree.viewport.left &&
                record.viewportClient.top >= tree.viewport.top && record.viewportClient.right <= tree.viewport.right &&
                record.viewportClient.bottom <= tree.viewport.bottom;
            if ((record.style & BS_TYPEMASK) == BS_GROUPBOX || !fullyInsideViewport) continue;
            const auto group = std::find_if(groups.begin(), groups.end(), [&record](const HwndTreeRecord* candidate)
                {
                    return record.parentClient.left >= candidate->parentClient.left &&
                        record.parentClient.right <= candidate->parentClient.right &&
                        record.parentClient.top >= candidate->parentClient.top &&
                        record.parentClient.bottom <= candidate->parentClient.bottom;
                });
            if (group == groups.end()) continue;
            ++groupedControls;
            visibleControlsHaveDrawRegion = visibleControlsHaveDrawRegion && record.clipType != NULLREGION;
            controlsAreAboveTheirGroupBoxes = controlsAreAboveTheirGroupBoxes &&
                record.zOrder < (*group)->zOrder;
        }

        bool bottomTreeCaptured = false;
        bool bottomControlsHaveDrawRegion = false;
        bool bottomControlsAreAboveGroupBoxes = false;
        bool bottomFooterHasDrawRegion = false;
        bool coordinateTranslationMatches = false;
        if (content != nullptr && !reportPath.empty())
        {
            SendMessageW(content, WM_VSCROLL, MAKEWPARAM(SB_BOTTOM, 0), 0);
            std::filesystem::path bottomReportPath = reportPath;
            bottomReportPath.replace_filename(reportPath.stem().wstring() + L"-bottom.txt");
            HwndTreeContext bottomTree{};
            bottomTreeCaptured = WriteSettingsHwndTree(window, bottomReportPath, &bottomTree);
            surfaceOffsetMatchesScrollbar = surfaceOffsetMatchesScrollbar && bottomTreeCaptured &&
                bottomTree.surfaceOffsetY == -bottomTree.scrollPosition;
            coordinateTranslationMatches = bottomTreeCaptured &&
                bottomTree.records.size() == tree.records.size() &&
                bottomTree.surfaceOffsetY < tree.surfaceOffsetY;
            if (coordinateTranslationMatches)
            {
                for (const HwndTreeRecord& topRecord : tree.records)
                {
                    const auto bottomRecord = std::find_if(bottomTree.records.begin(), bottomTree.records.end(),
                        [&topRecord](const HwndTreeRecord& candidate)
                        { return candidate.window == topRecord.window; });
                    if (bottomRecord == bottomTree.records.end() ||
                        !EqualRect(&topRecord.parentClient, &bottomRecord->parentClient) ||
                        bottomRecord->viewportClient.top - topRecord.viewportClient.top !=
                            bottomTree.surfaceOffsetY - tree.surfaceOffsetY)
                    {
                        coordinateTranslationMatches = false;
                        break;
                    }
                }
            }
            bottomControlsHaveDrawRegion = bottomTreeCaptured;
            bottomControlsAreAboveGroupBoxes = bottomTreeCaptured;
            std::vector<const HwndTreeRecord*> bottomGroups;
            for (const HwndTreeRecord& record : bottomTree.records)
            {
                if ((record.style & BS_TYPEMASK) == BS_GROUPBOX) bottomGroups.push_back(&record);
            }
            for (const HwndTreeRecord& record : bottomTree.records)
            {
                const bool fullyInsideViewport = record.viewportClient.left >= bottomTree.viewport.left &&
                    record.viewportClient.top >= bottomTree.viewport.top &&
                    record.viewportClient.right <= bottomTree.viewport.right &&
                    record.viewportClient.bottom <= bottomTree.viewport.bottom;
                if (!fullyInsideViewport) continue;
                if (record.controlId == 134)
                    bottomFooterHasDrawRegion = record.visible && record.clipType != NULLREGION;
                if ((record.style & BS_TYPEMASK) == BS_GROUPBOX) continue;
                const auto group = std::find_if(bottomGroups.begin(), bottomGroups.end(), [&record](const HwndTreeRecord* candidate)
                    {
                        return record.parentClient.left >= candidate->parentClient.left &&
                            record.parentClient.right <= candidate->parentClient.right &&
                            record.parentClient.top >= candidate->parentClient.top &&
                            record.parentClient.bottom <= candidate->parentClient.bottom;
                    });
                if (group == bottomGroups.end()) continue;
                bottomControlsHaveDrawRegion = bottomControlsHaveDrawRegion &&
                    record.clipType != NULLREGION;
                bottomControlsAreAboveGroupBoxes = bottomControlsAreAboveGroupBoxes &&
                    record.zOrder < (*group)->zOrder;
            }
            SendMessageW(content, WM_VSCROLL, MAKEWPARAM(SB_TOP, 0), 0);
        }

        RECT current{};
        GetWindowRect(window, &current);
        const int width = current.right - current.left;
        const int height = current.bottom - current.top;
        const HMONITOR monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
        MONITORINFO monitorInfo{ sizeof(monitorInfo) };
        const bool monitorAvailable = GetMonitorInfoW(monitor, &monitorInfo) != FALSE;
        const int offscreenX = monitorAvailable ? monitorInfo.rcWork.left - width - 100 : current.left - width - 100;
        const int offscreenY = monitorAvailable ? monitorInfo.rcWork.top - height - 100 : current.top - height - 100;
        const bool movedOffscreen = SetWindowPos(window, nullptr, offscreenX, offscreenY, width, height,
            SWP_NOZORDER | SWP_NOACTIVATE) != FALSE;
        const bool reallyOffscreen = movedOffscreen && !WindowFitsMonitorWorkArea(window);
        SendMessageW(window, WM_DISPLAYCHANGE, 32, MAKELPARAM(1280, 720));
        const bool displayChangeRecovered = WindowFitsMonitorWorkArea(window);

        SendMessageW(window, WM_CLOSE, 0, 0);
        const bool hiddenOnClose = IsWindowVisible(window) == FALSE;
        const bool movedOffscreenWhileHidden = SetWindowPos(window, nullptr, offscreenX, offscreenY,
            width, height, SWP_NOZORDER | SWP_NOACTIVATE) != FALSE;
        ShowSettingsWindow(window);
        const bool showRestoredVisibility = IsWindowVisible(window) != FALSE &&
            WindowFitsMonitorWorkArea(window);
        DestroyWindow(window);

        Expect(treeCaptured && groupCount == 6 && groupedControls >= 30,
            "real visible Settings HWND tree contains all group boxes and their child controls");
        Expect(surfaceCompositingEnabled,
            "scroll surface uses supported descendant compositing with a compatible window class");
        Expect(surfacePaintsBehindTransparentChildren,
            "scroll surface paints its class background beneath transparent group-box children");
        Expect(allChildrenValidAndVisible,
            "real Settings children have valid HWNDs, the content parent, WS_CHILD, WS_VISIBLE and visible ancestors");
        Expect(safeControlsRemainDisabled,
            "safe UI test mode keeps child controls read-only without hiding their HWNDs");
        Expect(visibleControlsHaveDrawRegion && controlsAreAboveTheirGroupBoxes,
            "visible Settings controls retain a non-empty clipped draw region and Z-order above group boxes");
        Expect(bottomTreeCaptured && bottomControlsHaveDrawRegion && bottomControlsAreAboveGroupBoxes &&
            bottomFooterHasDrawRegion,
            "scrolled real HWND tree keeps lower controls and footer visible and unoccluded");
        Expect(surfaceOffsetMatchesScrollbar,
            "real viewport scroll position matches the single surface HWND offset at the top and bottom");
        Expect(coordinateTranslationMatches,
            "every child HWND keeps identical surface coordinates and shifts once with its parent surface");
        Expect(scrollCompletesRepaint,
            "visible-window scrolling synchronously repaints the viewport, moved surface and child HWNDs without stale update regions");
        Expect(reallyOffscreen && displayChangeRecovered,
            "WM_DISPLAYCHANGE moves an off-screen Settings window back into the monitor work area");
        Expect(hiddenOnClose && movedOffscreenWhileHidden && showRestoredVisibility,
            "reopening a hidden off-screen Settings window restores it without creating another window");
        Expect(fixture.updateCalls == 0 && fixture.saverCalls == 0,
            "real HWND visibility and display recovery test performs no settings or screen saver actions");
    }

    void TestSettingsMessageLoopResponsiveness()
    {
        SettingsMessageLoopFixture fixture{};
        fixture.readyEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        const bool eventCreated = fixture.readyEvent != nullptr;
        HANDLE thread = eventCreated
            ? CreateThread(nullptr, 0, SettingsMessageLoopThread, &fixture, 0, nullptr)
            : nullptr;
        const bool threadCreated = thread != nullptr;
        const DWORD ready = threadCreated
            ? WaitForSingleObject(fixture.readyEvent, 5000) : WAIT_FAILED;
        HWND window = fixture.window.load(std::memory_order_acquire);
        const DWORD threadId = threadCreated ? GetThreadId(thread) : 0;
        DWORD windowThreadId{};
        DWORD windowProcessId{};
        const DWORD actualWindowThreadId = window != nullptr
            ? GetWindowThreadProcessId(window, &windowProcessId) : 0;
        windowThreadId = actualWindowThreadId;
        const bool windowOnMessageLoopThread = window != nullptr &&
            windowThreadId == threadId && windowProcessId == GetCurrentProcessId();

        DWORD_PTR result{};
        const bool displayChangeResponsive = window != nullptr &&
            SendMessageTimeoutW(window, WM_DISPLAYCHANGE, 32, MAKELPARAM(1280, 720),
                SMTO_ABORTIFHUNG | SMTO_BLOCK, 2000, &result) != 0;
        RECT suggested{ 40, 40, 40 + MulDiv(720, 120, 96), 40 + MulDiv(650, 120, 96) };
        const bool dpiChangeResponsive = window != nullptr &&
            SendMessageTimeoutW(window, WM_DPICHANGED, MAKELONG(120, 120),
                reinterpret_cast<LPARAM>(&suggested), SMTO_ABORTIFHUNG | SMTO_BLOCK,
                2000, &result) != 0;
        const bool stillVisibleAndInWorkArea = window != nullptr && IsWindow(window) &&
            IsWindowVisible(window) && WindowFitsMonitorWorkArea(window);

        const bool quitPosted = threadCreated && PostThreadMessageW(threadId, WM_QUIT, 0, 0) != FALSE;
        const DWORD threadExit = threadCreated ? WaitForSingleObject(thread, 5000) : WAIT_FAILED;
        if (thread != nullptr) CloseHandle(thread);
        if (fixture.readyEvent != nullptr) CloseHandle(fixture.readyEvent);

        Expect(eventCreated && threadCreated && ready == WAIT_OBJECT_0 && window != nullptr,
            "Settings can run on a dedicated Win32 UI message-loop thread");
        Expect(windowOnMessageLoopThread && displayChangeResponsive && dpiChangeResponsive,
            "display and DPI notifications are processed promptly on the Settings UI thread");
        Expect(stillVisibleAndInWorkArea,
            "Settings remains visible and inside the work area after queued display and DPI messages");
        Expect(quitPosted && threadExit == WAIT_OBJECT_0,
            "Settings UI message loop exits and destroys its window cleanly");
        Expect(fixture.settings.updateCalls == 0 && fixture.settings.saverCalls == 0,
            "message-loop display test performs no settings writes or screen saver actions");
    }
}

int wmain(int argc, wchar_t** argv)
{
    // ScreenSaverService's safe process-lifecycle test launches this same
    // regression executable with the single /s argument. Exit immediately so
    // the test never launches a real screen saver or a power action.
    if (argc == 2 && std::wstring_view(argv[1]) == L"/s") return EXIT_SUCCESS;
    if (argc > 1 && std::wstring_view(argv[1]) == L"--media-query-diagnostic")
    {
        return RunMediaGuardDiagnostic();
    }
    if (argc > 1 && std::wstring_view(argv[1]) == L"--settings-hwnd-inspection")
    {
        std::filesystem::path outputPath;
        DWORD holdMilliseconds{};
        if (argc > 2) outputPath = argv[2];
        if (argc > 3)
        {
            wchar_t* end{};
            const unsigned long parsed = std::wcstoul(argv[3], &end, 10);
            if (end != argv[3] && *end == L'\0' && parsed <= 30000)
                holdMilliseconds = static_cast<DWORD>(parsed);
        }
        if (outputPath.empty())
        {
            wchar_t tempPath[MAX_PATH]{};
            const DWORD length = GetTempPathW(static_cast<DWORD>(std::size(tempPath)), tempPath);
            if (length == 0 || length >= std::size(tempPath)) return EXIT_FAILURE;
            outputPath = std::filesystem::path(tempPath) /
                L"ClassicDesktopScreensaver.Native-HwndInspection.bmp";
        }
        return RunSettingsHwndInspection(outputPath, holdMilliseconds);
    }
    if (argc > 1 && std::wstring_view(argv[1]) == L"--settings-layout-preview")
    {
        std::filesystem::path outputPath;
        UINT previewDpi = 0;
        int previewWidthDips = 720;
        int nextArgument = 2;
        if (argc > 2)
        {
            wchar_t* end{};
            const unsigned long parsedDpi = std::wcstoul(argv[2], &end, 10);
            if (end != argv[2] && *end == L'\0' &&
                (parsedDpi == 96 || parsedDpi == 120 || parsedDpi == 144 || parsedDpi == 168))
            {
                previewDpi = static_cast<UINT>(parsedDpi);
                ++nextArgument;
            }
            else outputPath = argv[2];
        }
        if (outputPath.empty() && argc > nextArgument)
        {
            wchar_t* end{};
            const unsigned long parsedWidth = std::wcstoul(argv[nextArgument], &end, 10);
            if (end != argv[nextArgument] && *end == L'\0' && parsedWidth >= 480 && parsedWidth <= 720)
            {
                previewWidthDips = static_cast<int>(parsedWidth);
                ++nextArgument;
            }
        }
        if (outputPath.empty() && argc > nextArgument) outputPath = argv[nextArgument];
        if (outputPath.empty())
        {
            wchar_t tempPath[MAX_PATH]{};
            const DWORD length = GetTempPathW(static_cast<DWORD>(std::size(tempPath)), tempPath);
            if (length == 0 || length >= std::size(tempPath)) return EXIT_FAILURE;
            const UINT nameDpi = previewDpi == 0 ? GetDpiForSystem() : previewDpi;
            outputPath = std::filesystem::path(tempPath) /
                (L"ClassicDesktopScreensaver.Native-SettingsLayoutPreview-" +
                    std::to_wstring(nameDpi) + L"-" + std::to_wstring(previewWidthDips) + L".bmp");
        }
        return RunSettingsLayoutPreview(outputPath, previewDpi, previewWidthDips);
    }
    Expect(
        "S4 and hibernation file are both present",
        EvaluateHibernateAvailability({ true, true, true }),
        HibernateAvailability::Available);
    Expect(
        "S4 without hibernation file is unavailable",
        EvaluateHibernateAvailability({ true, true, false }),
        HibernateAvailability::Unsupported);
    Expect(
        "hibernation file without S4 is unavailable",
        EvaluateHibernateAvailability({ true, false, true }),
        HibernateAvailability::Unsupported);
    Expect(
        "missing S4 and hibernation file is unavailable",
        EvaluateHibernateAvailability({ true, false, false }),
        HibernateAvailability::Unsupported);
    Expect(
        "capability query failure fails closed",
        EvaluateHibernateAvailability({ false, true, true }),
        HibernateAvailability::QueryFailed);

    TestSettingsStore();
    TestHotkeys();
    TestPowerDispatch();
    TestRuntimePolicy();
    TestLiveMediaSessionQuery();
    TestSingleInstancePrimitives();
    TestTrayCallbackDecoding();
    TestTrayMenuModelAndPlacement();
    TestTrayMenuImages();
    TestDiagnosticLoggingPreservesLastError();
    TestUiTestPolicy();
    TestWindowsScreenSaverActivationLease();
    TestBrowserVideoGuardReport();
    TestAdaptiveLayoutWidthCalculation();
    TestSettingsWindowAndCleanup();
    TestCustomScreenSaverReenableAndHibernateControl();
    TestSettingsPopulationDoesNotTriggerRepeatedSaves();
    TestWindowsScreenSaverReadOnlyQuery();
    TestDirectScreenSaverLaunchAndProcessTracking();
    TestLiveHibernateCapabilityQuery();
    TestHibernateStatusRefreshWhileSettingsOpen();
    TestHotCornerComboPopulationAndPersistence();
    TestSettingsHwndVisibilityAndDisplayRecovery();
    TestSettingsMessageLoopResponsiveness();

    std::printf("%u test(s) failed.\n", failures);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
