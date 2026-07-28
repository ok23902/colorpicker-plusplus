#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <windows.h>
#include <string>
#include <algorithm>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

#include "resource.h"

// ------------------------------------------------------------
// Configuration
// ------------------------------------------------------------

static constexpr int HOTKEY_ID = 1;
static constexpr UINT HOTKEY_MODIFIERS = MOD_CONTROL | MOD_SHIFT;
static constexpr UINT HOTKEY_VK = 'C';

static constexpr int PREVIEW_WIDTH = 360;
static constexpr int PREVIEW_HEIGHT = 210;

static constexpr int MAG_SOURCE_RADIUS = 6;   // 13 x 13 source pixels
static constexpr int MAG_ZOOM = 12;           // 13 x 13 -> 156 x 156

// ------------------------------------------------------------
// Global state
// ------------------------------------------------------------

static HWND g_preview = nullptr;
static HHOOK g_mouseHook = nullptr;
static HHOOK g_keyboardHook = nullptr;
static POINT g_mousePos{};
static COLORREF g_currentColor = RGB(0, 0, 0);
static bool g_picking = false;
static bool g_previewClassRegistered = false;
static HANDLE g_mutex = nullptr;

// ------------------------------------------------------------
// Helpers
// ------------------------------------------------------------

static RECT GetVirtualScreenRect()
{
    RECT r{};
    r.left = GetSystemMetrics(SM_XVIRTUALSCREEN);
    r.top = GetSystemMetrics(SM_YVIRTUALSCREEN);
    r.right = r.left + GetSystemMetrics(SM_CXVIRTUALSCREEN);
    r.bottom = r.top + GetSystemMetrics(SM_CYVIRTUALSCREEN);
    return r;
}

static COLORREF GetScreenPixel(POINT pt)
{
    HDC hdc = GetDC(nullptr);
    if (!hdc)
        return CLR_INVALID;

    COLORREF color = GetPixel(hdc, pt.x, pt.y);
    ReleaseDC(nullptr, hdc);
    return color;
}

static std::wstring ColorToHex(COLORREF color)
{
    wchar_t buffer[16];
    swprintf_s(
        buffer,
        L"#%02X%02X%02X",
        GetRValue(color),
        GetGValue(color),
        GetBValue(color)
    );
    return buffer;
}

static std::wstring ColorToRgbString(COLORREF color)
{
    wchar_t buffer[32];
    swprintf_s(
        buffer,
        L"RGB(%u, %u, %u)",
        static_cast<unsigned>(GetRValue(color)),
        static_cast<unsigned>(GetGValue(color)),
        static_cast<unsigned>(GetBValue(color))
    );
    return buffer;
}

static bool CopyToClipboard(const std::wstring& text)
{
    if (!OpenClipboard(nullptr))
        return false;

    EmptyClipboard();

    const SIZE_T bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!hMem)
    {
        CloseClipboard();
        return false;
    }

    void* ptr = GlobalLock(hMem);
    if (!ptr)
    {
        GlobalFree(hMem);
        CloseClipboard();
        return false;
    }

    memcpy(ptr, text.c_str(), bytes);
    GlobalUnlock(hMem);

    if (!SetClipboardData(CF_UNICODETEXT, hMem))
    {
        GlobalFree(hMem);
        CloseClipboard();
        return false;
    }

    CloseClipboard();
    return true;
}

static void EndPicker()
{
    g_picking = false;
    PostQuitMessage(0);
}

static void ClampPreviewPosition(int& x, int& y, int w, int h)
{
    RECT vr = GetVirtualScreenRect();

    if (x + w > vr.right)
        x = g_mousePos.x - w - 24;

    if (y + h > vr.bottom)
        y = g_mousePos.y - h - 24;

    if (x < vr.left)
        x = vr.left;

    if (y < vr.top)
        y = vr.top;
}

static void UpdatePreview();

// ------------------------------------------------------------
// Preview window
// ------------------------------------------------------------

static LRESULT CALLBACK PreviewWndProc(
    HWND hwnd,
    UINT msg,
    WPARAM wParam,
    LPARAM lParam)
{
    switch (msg)
    {
    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
    {
        PAINTSTRUCT ps{};
        HDC hdc = BeginPaint(hwnd, &ps);

        RECT rc{};
        GetClientRect(hwnd, &rc);
        const int width = rc.right - rc.left;
        const int height = rc.bottom - rc.top;

        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP backBmp = CreateCompatibleBitmap(hdc, width, height);
        HGDIOBJ oldBackBmp = SelectObject(memDC, backBmp);

        HBRUSH bg = CreateSolidBrush(RGB(28, 28, 28));
        FillRect(memDC, &rc, bg);
        DeleteObject(bg);

        SetBkMode(memDC, TRANSPARENT);

        // Layout
        const int magX = 12;
        const int magY = 12;
        const int sourceSize = MAG_SOURCE_RADIUS * 2 + 1;
        const int magSize = sourceSize * MAG_ZOOM;

        // Capture and magnify the area under the cursor.
        HDC screenDC = GetDC(nullptr);
        HDC srcDC = CreateCompatibleDC(screenDC);
        HBITMAP srcBmp = CreateCompatibleBitmap(screenDC, sourceSize, sourceSize);
        HGDIOBJ oldSrcBmp = SelectObject(srcDC, srcBmp);

        RECT vr = GetVirtualScreenRect();
        int srcX = g_mousePos.x - MAG_SOURCE_RADIUS;
        int srcY = g_mousePos.y - MAG_SOURCE_RADIUS;

        const int minX = static_cast<int>(vr.left);
        const int minY = static_cast<int>(vr.top);
        const int maxX = static_cast<int>(vr.right) - sourceSize;
        const int maxY = static_cast<int>(vr.bottom) - sourceSize;

        srcX = std::max(minX, std::min(srcX, maxX));
        srcY = std::max(minY, std::min(srcY, maxY));

        BitBlt(
            srcDC,
            0,
            0,
            sourceSize,
            sourceSize,
            screenDC,
            srcX,
            srcY,
            SRCCOPY | CAPTUREBLT
        );

        SetStretchBltMode(memDC, COLORONCOLOR);
        StretchBlt(
            memDC,
            magX,
            magY,
            magSize,
            magSize,
            srcDC,
            0,
            0,
            sourceSize,
            sourceSize,
            SRCCOPY
        );

        // Border around magnifier
        HBRUSH borderBrush = CreateSolidBrush(RGB(90, 90, 90));
        RECT borderRect{
            magX - 1,
            magY - 1,
            magX + magSize + 1,
            magY + magSize + 1
        };
        FrameRect(memDC, &borderRect, borderBrush);
        DeleteObject(borderBrush);

        // Center pixel marker
        const int centerCellX = magX + MAG_SOURCE_RADIUS * MAG_ZOOM;
        const int centerCellY = magY + MAG_SOURCE_RADIUS * MAG_ZOOM;

        HPEN centerPen = CreatePen(PS_SOLID, 1, RGB(255, 255, 255));
        HGDIOBJ oldPen = SelectObject(memDC, centerPen);

        MoveToEx(memDC, centerCellX, magY, nullptr);
        LineTo(memDC, centerCellX, magY + magSize);

        MoveToEx(memDC, magX, centerCellY, nullptr);
        LineTo(memDC, magX + magSize, centerCellY);

        SelectObject(memDC, oldPen);
        DeleteObject(centerPen);

        // Small box to show the exact sampled pixel
        RECT pixelRect{
            centerCellX,
            centerCellY,
            centerCellX + MAG_ZOOM,
            centerCellY + MAG_ZOOM
        };
        HBRUSH pixelOutline = CreateSolidBrush(RGB(255, 255, 255));
        FrameRect(memDC, &pixelRect, pixelOutline);
        DeleteObject(pixelOutline);

        // Sample color box
        RECT sampleRect{
            190,
            12,
            238,
            60
        };
        HBRUSH sampleBrush = CreateSolidBrush(g_currentColor);
        FillRect(memDC, &sampleRect, sampleBrush);
        DeleteObject(sampleBrush);

        HBRUSH sampleBorder = CreateSolidBrush(RGB(120, 120, 120));
        FrameRect(memDC, &sampleRect, sampleBorder);
        DeleteObject(sampleBorder);

        // Text
        HFONT font = CreateFontW(
            18,
            0,
            0,
            0,
            FW_NORMAL,
            FALSE,
            FALSE,
            FALSE,
            DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE,
            L"Segoe UI"
        );

        HFONT oldFont = (HFONT)SelectObject(memDC, font);
        SetTextColor(memDC, RGB(255, 255, 255));

        std::wstring hexText = ColorToHex(g_currentColor);
        std::wstring rgbText = ColorToRgbString(g_currentColor);

        RECT hexRect{ 190, 70, rc.right - 10, 100 };
        DrawTextW(
            memDC,
            hexText.c_str(),
            -1,
            &hexRect,
            DT_SINGLELINE | DT_LEFT | DT_VCENTER
        );

        RECT rgbRect{ 190, 100, rc.right - 10, 130 };
        DrawTextW(
            memDC,
            rgbText.c_str(),
            -1,
            &rgbRect,
            DT_SINGLELINE | DT_LEFT | DT_VCENTER
        );

        const wchar_t* helpText = L"Left click: copy   Right click / Esc: cancel";
        RECT helpRect{ 12, 170, rc.right - 10, rc.bottom - 10 };
        DrawTextW(
            memDC,
            helpText,
            -1,
            &helpRect,
            DT_LEFT | DT_WORDBREAK
        );

        SelectObject(memDC, oldFont);
        DeleteObject(font);

        BitBlt(hdc, 0, 0, width, height, memDC, 0, 0, SRCCOPY);

        SelectObject(srcDC, oldSrcBmp);
        DeleteObject(srcBmp);
        DeleteDC(srcDC);
        ReleaseDC(nullptr, screenDC);

        SelectObject(memDC, oldBackBmp);
        DeleteObject(backBmp);
        DeleteDC(memDC);

        EndPaint(hwnd, &ps);
        return 0;
    }
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static bool EnsurePreviewClassRegistered()
{
    if (g_previewClassRegistered)
        return true;

    const wchar_t CLASS_NAME[] =
        L"MinimalColorPickerPreview";

    WNDCLASSEXW wc{};

    wc.cbSize =
        sizeof(WNDCLASSEXW);

    wc.lpfnWndProc =
        PreviewWndProc;

    wc.hInstance =
        GetModuleHandleW(nullptr);

    wc.hIcon =
        LoadIconW(
            GetModuleHandleW(nullptr),
            MAKEINTRESOURCEW(IDI_APP_ICON)
        );

    wc.hIconSm =
        LoadIconW(
            GetModuleHandleW(nullptr),
            MAKEINTRESOURCEW(IDI_APP_ICON)
        );

    wc.hCursor =
        LoadCursorW(
            nullptr,
            IDC_ARROW
        );

    wc.lpszClassName =
        CLASS_NAME;

    if (!RegisterClassExW(&wc))
    {
        if (GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
            return false;
    }

    g_previewClassRegistered = true;

    return true;
}

static void UpdatePreview()
{
    if (!g_preview)
        return;

    g_currentColor = GetScreenPixel(g_mousePos);
    if (g_currentColor == CLR_INVALID)
        return;

    int x = g_mousePos.x + 24;
    int y = g_mousePos.y + 24;

    ClampPreviewPosition(x, y, PREVIEW_WIDTH, PREVIEW_HEIGHT);

    SetWindowPos(
        g_preview,
        HWND_TOPMOST,
        x,
        y,
        PREVIEW_WIDTH,
        PREVIEW_HEIGHT,
        SWP_NOACTIVATE | SWP_SHOWWINDOW
    );

    InvalidateRect(g_preview, nullptr, TRUE);
}

// ------------------------------------------------------------
// Hooks
// ------------------------------------------------------------

static LRESULT CALLBACK MouseHookProc(
    int nCode,
    WPARAM wParam,
    LPARAM lParam)
{
    if (nCode == HC_ACTION && g_picking)
    {
        const auto* info = reinterpret_cast<MSLLHOOKSTRUCT*>(lParam);

        switch (wParam)
        {
        case WM_MOUSEMOVE:
            g_mousePos = info->pt;
            SetCursor(LoadCursorW(nullptr, IDC_CROSS));
            UpdatePreview();
            break;

        case WM_LBUTTONDOWN:
        {
            COLORREF color = GetScreenPixel(info->pt);
            if (color != CLR_INVALID)
                CopyToClipboard(ColorToHex(color));

            EndPicker();
            return 1;
        }

        case WM_RBUTTONDOWN:
            EndPicker();
            return 1;
        }
    }

    return CallNextHookEx(g_mouseHook, nCode, wParam, lParam);
}

static LRESULT CALLBACK KeyboardHookProc(
    int nCode,
    WPARAM wParam,
    LPARAM lParam)
{
    if (nCode == HC_ACTION && g_picking)
    {
        const auto* info = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);

        if ((wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN) &&
            info->vkCode == VK_ESCAPE)
        {
            EndPicker();
            return 1;
        }
    }

    return CallNextHookEx(g_keyboardHook, nCode, wParam, lParam);
}

// ------------------------------------------------------------
// Start / stop picker
// ------------------------------------------------------------

static bool StartPicker()
{
    if (g_picking)
        return true;

    if (!EnsurePreviewClassRegistered())
        return false;

    g_picking = true;
    GetCursorPos(&g_mousePos);

    g_preview = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT,
        L"MinimalColorPickerPreview",
        L"",
        WS_POPUP,
        0,
        0,
        PREVIEW_WIDTH,
        PREVIEW_HEIGHT,
        nullptr,
        nullptr,
        GetModuleHandleW(nullptr),
        nullptr
    );

    if (!g_preview)
    {
        g_picking = false;
        return false;
    }

    ShowWindow(g_preview, SW_SHOWNOACTIVATE);
    UpdatePreview();

    g_mouseHook = SetWindowsHookExW(
        WH_MOUSE_LL,
        MouseHookProc,
        nullptr,
        0
    );

    if (!g_mouseHook)
    {
        DestroyWindow(g_preview);
        g_preview = nullptr;
        g_picking = false;
        return false;
    }

    g_keyboardHook = SetWindowsHookExW(
        WH_KEYBOARD_LL,
        KeyboardHookProc,
        nullptr,
        0
    );

    if (!g_keyboardHook)
    {
        UnhookWindowsHookEx(g_mouseHook);
        g_mouseHook = nullptr;
        DestroyWindow(g_preview);
        g_preview = nullptr;
        g_picking = false;
        return false;
    }

    return true;
}

static void StopPicker()
{
    g_picking = false;

    if (g_keyboardHook)
    {
        UnhookWindowsHookEx(g_keyboardHook);
        g_keyboardHook = nullptr;
    }

    if (g_mouseHook)
    {
        UnhookWindowsHookEx(g_mouseHook);
        g_mouseHook = nullptr;
    }

    if (g_preview)
    {
        DestroyWindow(g_preview);
        g_preview = nullptr;
    }
}

// ------------------------------------------------------------
// Entry point
// ------------------------------------------------------------

int WINAPI wWinMain(
    HINSTANCE,
    HINSTANCE,
    PWSTR,
    int)
{
    g_mutex = CreateMutexW(
        nullptr,
        TRUE,
        L"ColorPickerPlusPlus_Instance"
    );

    if (!g_mutex)
    {
        MessageBoxW(
            nullptr,
            L"Failed to create application mutex.",
            L"Color Picker",
            MB_OK | MB_ICONERROR
        );

        return 1;
    }

    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        MessageBoxW(
            nullptr,
            L"ColorPicker++ is already running.",
            L"Color Picker",
            MB_OK | MB_ICONINFORMATION
        );

        CloseHandle(g_mutex);
        g_mutex = nullptr;

        return 0;
    }

    SetProcessDpiAwarenessContext(
        DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
    );

    if (!RegisterHotKey(
        nullptr,
        HOTKEY_ID,
        HOTKEY_MODIFIERS,
        HOTKEY_VK))
    {
        MessageBoxW(
            nullptr,
            L"Failed to register Ctrl + Shift + C.",
            L"Color Picker",
            MB_OK | MB_ICONERROR
        );
        return 1;
    }

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0)
    {
        if (msg.message == WM_HOTKEY && msg.wParam == HOTKEY_ID)
        {
            if (!StartPicker())
            {
                MessageBoxW(
                    nullptr,
                    L"Failed to start picker.",
                    L"Color Picker",
                    MB_OK | MB_ICONERROR
                );
                break;
            }

            continue;
        }

        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    StopPicker();
    UnregisterHotKey(nullptr, HOTKEY_ID);
    if (g_mutex)
    {
        CloseHandle(g_mutex);
        g_mutex = nullptr;
    }
    return 0;
}
