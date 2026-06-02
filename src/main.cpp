#include "resource_monitor.h"
#include <cassert>
#include <commctrl.h>
#include <cstdint>
#include <dxgi.h>
#include <algorithm>
#include <windows.h>

// Main window proc and helpers
LRESULT CALLBACK MainWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
LRESULT OnPaintMain(HWND hwnd, ResourceMonitor& monitor);
LRESULT CALLBACK SettingsWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
void ShowSettingsDialog(HWND hwndParent);

namespace
{
ResourceMonitor g_monitor;
static const wchar_t* AppName = L"wperf";
static const wchar_t* IniFileName = L"wperf.ini";
static const wchar_t* SettingsName = L"Settings";
static constexpr float Giga = 1024.0f * 1024.0f * 1024.0f;
static constexpr float Mega = 1024.0f * 1024.0f;
static constexpr float Kilo = 1024.0f;

DWORD GetModulePath(DWORD size, wchar_t* buffer)
{
    buffer[0] = L'\0';
    DWORD length = GetModuleFileNameW(nullptr, buffer, size);
    if(length < ResourceMonitor::kBufferSize) {
        return length;
    }
    return 0;
}

DWORD GetIniFilePath(DWORD size, wchar_t* buffer)
{
    DWORD length = GetModulePath(size, buffer);
    const wchar_t* last_slash = nullptr;
    if (0<length) {
        for (size_t i = length; 0<i; --i) {
            wchar_t c = buffer[i - 1];
            if (c == L'\\' || c == L'/') {
                last_slash = &buffer[i - 1];
                break;
            }
        }
    }

    if (nullptr != last_slash) {
        size_t dir_len = (size_t)(last_slash - buffer) + 1;
        if (dir_len < size) {
            buffer[dir_len] = L'\0';
            wcsncat(buffer, IniFileName, wcslen(IniFileName));
            return length+dir_len;
        }
    } else {
        length = wcslen(IniFileName);
        wcsncpy(buffer, IniFileName, length);
        buffer[length] = L'\0';
        return length;
    }
}

void FormatBytes(DWORD size, wchar_t* buffer, ULONGLONG bytes)
{
    swprintf_s(buffer, size, L"%.1f GB", (float)bytes / Giga);
}

void FormatNetworkSpeed(DWORD size, wchar_t* buffer, float bps)
{
    if(bps >= Giga)
        swprintf_s(buffer, size, L"%.2f GB/s", bps / Giga);
    else if(bps >= Mega)
        swprintf_s(buffer, size, L"%.2f MB/s", bps / Mega);
    else if(bps >= Kilo)
        swprintf_s(buffer, size, L"%.1f KB/s", bps / Kilo);
    else
        swprintf_s(buffer, size, L"%.0f B/s", bps);
}

void DrawCard(HDC hdc, const RECT& rect, COLORREF accentColor)
{
    // 1. Draw card background (slightly lighter dark gray)
    HBRUSH hbrCard = CreateSolidBrush(RGB(24, 24, 29));
    FillRect(hdc, &rect, hbrCard);
    DeleteObject(hbrCard);

    // 2. Draw 1px card border
    HPEN hPenBorder = CreatePen(PS_SOLID, 1, RGB(45, 45, 55));
    HPEN hPenOld = (HPEN)SelectObject(hdc, hPenBorder);
    MoveToEx(hdc, rect.left, rect.top, nullptr);
    LineTo(hdc, rect.right - 1, rect.top);
    LineTo(hdc, rect.right - 1, rect.bottom - 1);
    LineTo(hdc, rect.left, rect.bottom - 1);
    LineTo(hdc, rect.left, rect.top);
    SelectObject(hdc, hPenOld);
    DeleteObject(hPenBorder);

    // 3. Draw 4px neon vertical accent bar on the left edge
    RECT accentRect = {rect.left + 1, rect.top + 1, rect.left + 5, rect.bottom - 1};
    HBRUSH hbrAccent = CreateSolidBrush(accentColor);
    FillRect(hdc, &accentRect, hbrAccent);
    DeleteObject(hbrAccent);
}

void DrawProgressBar(HDC hdc, const RECT& rect, float percentage, COLORREF color)
{
    // 1. Draw progress bar background slot
    HBRUSH hbrSlot = CreateSolidBrush(RGB(40, 40, 46));
    FillRect(hdc, &rect, hbrSlot);
    DeleteObject(hbrSlot);

    // 2. Calculate and draw filled bar bounds
    int32_t totalWidth = rect.right - rect.left;
    int32_t fillWidth = (int32_t)(totalWidth * (percentage / 100.0f));
    fillWidth = std::clamp(fillWidth, 0, totalWidth);

    if(fillWidth > 0) {
        RECT fillRect = {rect.left, rect.top, rect.left + fillWidth, rect.bottom};
        HBRUSH hbrFill = CreateSolidBrush(color);
        FillRect(hdc, &fillRect, hbrFill);
        DeleteObject(hbrFill);
    }

    // 3. Draw simple dark outline for depth
    HPEN hPenOutline = CreatePen(PS_SOLID, 1, RGB(30, 30, 35));
    HPEN hPenOld = (HPEN)SelectObject(hdc, hPenOutline);
    MoveToEx(hdc, rect.left, rect.top, nullptr);
    LineTo(hdc, rect.right - 1, rect.top);
    LineTo(hdc, rect.right - 1, rect.bottom - 1);
    LineTo(hdc, rect.left, rect.bottom - 1);
    LineTo(hdc, rect.left, rect.top);
    SelectObject(hdc, hPenOld);
    DeleteObject(hPenOutline);
}
struct AppSettings {
    int32_t updateIntervalMs = 1000;
    bool alwaysOnTop = false;
};
AppSettings g_settings;
HWND g_hwndSettings = nullptr;

static constexpr int kDlgIntervalEdit   = 2001;
static constexpr int kDlgAlwaysOnTopChk = 2002;
static constexpr int kDlgOK             = 2003;
static constexpr int kDlgCancel         = 2004;
static constexpr wchar_t SettingsClassName[] = L"wperfSettings";

void LoadSettings(size_t length, const wchar_t* iniPath)
{
    if(length<=0){
        return;
    }
    g_settings.updateIntervalMs = std::clamp((int32_t)GetPrivateProfileIntW(SettingsName, L"UpdateIntervalMs", 1000, iniPath), 250, 60000);
    g_settings.alwaysOnTop = GetPrivateProfileIntW(SettingsName, L"AlwaysOnTop", 0, iniPath) != 0;
}

void SaveSettings(size_t length, const wchar_t* iniPath)
{
    wchar_t intervalStr[16];
    swprintf_s(intervalStr, L"%d", g_settings.updateIntervalMs);
    WritePrivateProfileStringW(SettingsName, L"UpdateIntervalMs", intervalStr, iniPath);
    WritePrivateProfileStringW(SettingsName, L"AlwaysOnTop", g_settings.alwaysOnTop ? L"1" : L"0", iniPath);
}

int32_t CountGpuAdapters()
{
    IDXGIFactory1* pFactory = nullptr;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&pFactory)))
        return 1;
    int32_t count = 0;
    UINT idx = 0;
    IDXGIAdapter1* pAdapter = nullptr;
    while (pFactory->EnumAdapters1(idx++, &pAdapter) != DXGI_ERROR_NOT_FOUND) {
        DXGI_ADAPTER_DESC1 desc{};
        pAdapter->GetDesc1(&desc);
        if (!(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE))
            ++count;
        pAdapter->Release();
    }
    pFactory->Release();
    return count > 0 ? count : 1;
}
} // namespace

LRESULT CALLBACK SettingsWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    static HBRUSH s_hbrDark = nullptr;
    static HBRUSH s_hbrEdit = nullptr;

    switch(uMsg) {
    case WM_CREATE: {
        s_hbrDark = CreateSolidBrush(RGB(18, 18, 20));
        s_hbrEdit = CreateSolidBrush(RGB(35, 35, 42));

        HINSTANCE hInst = GetModuleHandleW(nullptr);

        CreateWindowExW(0, L"STATIC", L"Update interval (ms):",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            12, 16, 160, 20, hwnd, nullptr, hInst, nullptr);

        wchar_t buf[16];
        swprintf_s(buf, L"%d", g_settings.updateIntervalMs);
        CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", buf,
            WS_CHILD | WS_VISIBLE | ES_NUMBER | WS_TABSTOP,
            180, 13, 72, 22, hwnd, (HMENU)(UINT_PTR)kDlgIntervalEdit, hInst, nullptr);

        CreateWindowExW(0, L"BUTTON", L"Always on top",
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | WS_TABSTOP,
            12, 48, 160, 22, hwnd, (HMENU)(UINT_PTR)kDlgAlwaysOnTopChk, hInst, nullptr);
        SendDlgItemMessageW(hwnd, kDlgAlwaysOnTopChk, BM_SETCHECK,
            g_settings.alwaysOnTop ? BST_CHECKED : BST_UNCHECKED, 0);

        CreateWindowExW(0, L"BUTTON", L"OK",
            WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON | WS_TABSTOP,
            58, 82, 72, 26, hwnd, (HMENU)(UINT_PTR)kDlgOK, hInst, nullptr);
        CreateWindowExW(0, L"BUTTON", L"Cancel",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
            140, 82, 72, 26, hwnd, (HMENU)(UINT_PTR)kDlgCancel, hInst, nullptr);
        return 0;
    }

    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)wParam;
        SetTextColor(hdc, RGB(200, 200, 210));
        SetBkMode(hdc, TRANSPARENT);
        return (LRESULT)s_hbrDark;
    }

    case WM_CTLCOLOREDIT: {
        HDC hdc = (HDC)wParam;
        SetTextColor(hdc, RGB(255, 255, 255));
        SetBkColor(hdc, RGB(35, 35, 42));
        return (LRESULT)s_hbrEdit;
    }

    case WM_CTLCOLORBTN: {
        HDC hdc = (HDC)wParam;
        SetTextColor(hdc, RGB(200, 200, 210));
        SetBkMode(hdc, TRANSPARENT);
        return (LRESULT)s_hbrDark;
    }

    case WM_ERASEBKGND: {
        RECT rc;
        GetClientRect(hwnd, &rc);
        FillRect((HDC)wParam, &rc, s_hbrDark);
        return 1;
    }

    case WM_COMMAND: {
        int id = LOWORD(wParam);
        if(id == kDlgOK) {
            wchar_t intervalBuf[16] = {};
            GetDlgItemTextW(hwnd, kDlgIntervalEdit, intervalBuf, 16);
            int32_t newInterval = _wtoi(intervalBuf);
            if(newInterval >= 250 && newInterval <= 60000)
                g_settings.updateIntervalMs = newInterval;
            g_settings.alwaysOnTop = SendDlgItemMessageW(hwnd, kDlgAlwaysOnTopChk, BM_GETCHECK, 0, 0) == BST_CHECKED;

            DWORD length = GetIniFilePath(ResourceMonitor::kBufferWChars, g_monitor.GetTextBuffer());
            SaveSettings(length, g_monitor.GetTextBuffer());

            HWND hwndMain = GetParent(hwnd);
            if(hwndMain) {
                KillTimer(hwndMain, 1);
                SetTimer(hwndMain, 1, g_settings.updateIntervalMs, nullptr);
                SetWindowPos(hwndMain,
                    g_settings.alwaysOnTop ? HWND_TOPMOST : HWND_BOTTOM,
                    0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            }
            DestroyWindow(hwnd);
        } else if(id == kDlgCancel) {
            DestroyWindow(hwnd);
        }
        return 0;
    }

    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;

    case WM_NCDESTROY: {
        if(s_hbrDark) { DeleteObject(s_hbrDark); s_hbrDark = nullptr; }
        if(s_hbrEdit) { DeleteObject(s_hbrEdit); s_hbrEdit = nullptr; }
        g_hwndSettings = nullptr;
        return 0;
    }
    }
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

void ShowSettingsDialog(HWND hwndParent)
{
    if(g_hwndSettings) {
        SetForegroundWindow(g_hwndSettings);
        return;
    }

    RECT rc = {0, 0, 268, 120};
    AdjustWindowRectEx(&rc, WS_POPUP | WS_CAPTION | WS_SYSMENU, FALSE,
        WS_EX_DLGMODALFRAME | WS_EX_TOOLWINDOW);

    RECT parentRect;
    GetWindowRect(hwndParent, &parentRect);
    int dlgW = rc.right - rc.left;
    int dlgH = rc.bottom - rc.top;
    int x = parentRect.left + (parentRect.right - parentRect.left - dlgW) / 2;
    int y = parentRect.top + (parentRect.bottom - parentRect.top - dlgH) / 2;

    g_hwndSettings = CreateWindowExW(
        WS_EX_DLGMODALFRAME | WS_EX_TOOLWINDOW,
        SettingsClassName, L"Settings",
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        x, y, dlgW, dlgH,
        hwndParent, nullptr, GetModuleHandleW(nullptr), nullptr);

    if(!g_hwndSettings) return;

    EnableWindow(hwndParent, FALSE);
    ShowWindow(g_hwndSettings, SW_SHOW);
    UpdateWindow(g_hwndSettings);

    MSG msg;
    while(g_hwndSettings) {
        BOOL ret = GetMessageW(&msg, nullptr, 0, 0);
        if(ret == 0) { PostQuitMessage((int)msg.wParam); break; }
        if(ret == -1) break;
        if(!IsDialogMessageW(g_hwndSettings, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    if(IsWindow(hwndParent)) {
        EnableWindow(hwndParent, TRUE);
        SetForegroundWindow(hwndParent);
    }
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    // Enable modern visual styling
    InitCommonControls();

    // Enable high DPI awareness to make layouts and fonts razor-sharp
    SetProcessDPIAware();

    // Register Main Window Class
    WNDCLASSEXW wcx = {0};
    wcx.cbSize = sizeof(wcx);
    wcx.style = CS_HREDRAW | CS_VREDRAW;
    wcx.lpfnWndProc = MainWndProc;
    wcx.hInstance = hInstance;
    wcx.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wcx.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcx.hbrBackground = nullptr; // Managed entirely by double buffer in OnPaint
    wcx.lpszClassName = AppName;

    if(!RegisterClassExW(&wcx)) {
        MessageBoxW(nullptr, L"Failed to register main window class!", L"Error", MB_ICONERROR);
        return 1;
    }
    g_monitor.Initialize();

    WNDCLASSEXW wcxSettings = {0};
    wcxSettings.cbSize = sizeof(wcxSettings);
    wcxSettings.style = CS_HREDRAW | CS_VREDRAW;
    wcxSettings.lpfnWndProc = SettingsWndProc;
    wcxSettings.hInstance = hInstance;
    wcxSettings.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcxSettings.hbrBackground = nullptr;
    wcxSettings.lpszClassName = SettingsClassName;
    RegisterClassExW(&wcxSettings);

    // Load last coordinates from wperf.ini
    DWORD length = GetIniFilePath(ResourceMonitor::kBufferWChars, g_monitor.GetTextBuffer());
    LoadSettings(length, g_monitor.GetTextBuffer());

    int32_t width = 260;
    int32_t height = 150 + CountGpuAdapters() * 30;

    int32_t x = (int32_t)GetPrivateProfileIntW(L"Window", L"x", CW_USEDEFAULT, g_monitor.GetTextBuffer());
    int32_t y = (int32_t)GetPrivateProfileIntW(L"Window", L"y", CW_USEDEFAULT, g_monitor.GetTextBuffer());
    // If no config found, default to bottom-right corner of screen (with margins)
    if(x == (int32_t)CW_USEDEFAULT || y == (int32_t)CW_USEDEFAULT) {
        RECT workArea;
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0);
        x = workArea.right - width - 20;
        y = workArea.bottom - height - 20;
    }

    // Create Main Window as a borderless popup window
    HWND hwnd = CreateWindowExW(
        WS_EX_TOOLWINDOW,
        AppName,
        AppName,                                // Window title "wperf"
        WS_POPUP | WS_SYSMENU | WS_MINIMIZEBOX, // Completely borderless window
        x, y, width, height,
        nullptr, nullptr, hInstance, nullptr);

    if(!hwnd) {
        MessageBoxW(nullptr, L"Failed to create main window!", L"Error", MB_ICONERROR);
        return 1;
    }

    SetWindowPos(hwnd, g_settings.alwaysOnTop ? HWND_TOPMOST : HWND_BOTTOM,
        0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    // High-efficiency message loop
    MSG msg;
    while(GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    g_monitor.Terminate();
    return (int)msg.wParam;
}

LRESULT CALLBACK MainWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch(uMsg) {
    case WM_CREATE: {
        // High precision standard 1-second system timer
        SetTimer(hwnd, 1, g_settings.updateIntervalMs, nullptr);
        return 0;
    }

    case WM_TIMER: {
        if(wParam == 1) {
            // CRITICAL RESOURCE SAVING OPTIMIZATION:
            // When minimized, suspend CPU polling, network updates, and painting entirely!
            if(IsIconic(hwnd)) {
                return 0;
            }

            g_monitor.Update();
            // Force redraw of the entire window (very cheap since it's just basic texts and bars)
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }

    case WM_WINDOWPOSCHANGING: {
        WINDOWPOS* pWndPos = (WINDOWPOS*)lParam;
        pWndPos->hwndInsertAfter = g_settings.alwaysOnTop ? HWND_TOPMOST : HWND_BOTTOM;
        return 0;
    }

    case WM_NCHITTEST: {
        // Allows the user to click and drag the borderless window from anywhere
        LRESULT hit = DefWindowProcW(hwnd, uMsg, wParam, lParam);
        if(hit == HTCLIENT) {
            return HTCAPTION;
        }
        return hit;
    }

    case WM_RBUTTONUP:
    case WM_NCRBUTTONUP: {
        HMENU hMenu = CreatePopupMenu();
        AppendMenuW(hMenu, MF_STRING, 1001, L"Settings");
        AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(hMenu, MF_STRING, 1002, L"Exit");

        POINT pt;
        GetCursorPos(&pt);

        SetForegroundWindow(hwnd);
        int32_t selection = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, nullptr);
        DestroyMenu(hMenu);

        if(selection == 1001) {
            ShowSettingsDialog(hwnd);
        } else if(selection == 1002) {
            DestroyWindow(hwnd);
        }
        return 0;
    }

    case WM_PAINT: {
        return OnPaintMain(hwnd, g_monitor);
    }

    case WM_ERASEBKGND:
        return 1; // Prevent background erasing to avoid layout flicker

    case WM_DESTROY: {
        KillTimer(hwnd, 1);

        // Save window coordinates into wperf.ini
        WINDOWPLACEMENT wp = {0};
        wp.length = sizeof(wp);
        if(GetWindowPlacement(hwnd, &wp)) {
            // Only save coordinate states if normal (not minimized)
            if(wp.showCmd == SW_SHOWNORMAL || wp.showCmd == SW_SHOW) {
                DWORD length = GetIniFilePath(ResourceMonitor::kBufferWChars, g_monitor.GetTextBuffer());
                wchar_t xStr[16], yStr[16];
                swprintf_s(xStr, L"%ld", wp.rcNormalPosition.left);
                swprintf_s(yStr, L"%ld", wp.rcNormalPosition.top);
                WritePrivateProfileStringW(L"Window", L"x", xStr, g_monitor.GetTextBuffer());
                WritePrivateProfileStringW(L"Window", L"y", yStr, g_monitor.GetTextBuffer());
            }
        }

        PostQuitMessage(0);
        return 0;
    }
    }
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

LRESULT OnPaintMain(HWND hwnd, ResourceMonitor& monitor)
{
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);

    RECT rect;
    GetClientRect(hwnd, &rect);
    int32_t w = rect.right - rect.left;
    int32_t h = rect.bottom - rect.top;

    if(w <= 0 || h <= 0) {
        EndPaint(hwnd, &ps);
        return 0;
    }

    // High performance double buffering
    static HBITMAP s_hbmBuffer = nullptr;
    static int32_t s_bufferWidth = 0;
    static int32_t s_bufferHeight = 0;

    if(!s_hbmBuffer || s_bufferWidth != w || s_bufferHeight != h) {
        if(s_hbmBuffer)
            DeleteObject(s_hbmBuffer);
        HDC hdcScreen = GetDC(nullptr);
        s_hbmBuffer = CreateCompatibleBitmap(hdcScreen, w, h);
        ReleaseDC(nullptr, hdcScreen);
        s_bufferWidth = w;
        s_bufferHeight = h;
    }

    HDC hdcMem = CreateCompatibleDC(hdc);
    HBITMAP hbmOld = (HBITMAP)SelectObject(hdcMem, s_hbmBuffer);

    // 1. Draw dashboard base background (Sleek Dark Theme)
    HBRUSH hbrBg = CreateSolidBrush(RGB(18, 18, 20));
    FillRect(hdcMem, &rect, hbrBg);
    DeleteObject(hbrBg);

    // 2. Draw subtle 1px border around the entire window to stand out
    HPEN hPenWinBorder = CreatePen(PS_SOLID, 1, RGB(50, 50, 60));
    HPEN hPenOld = (HPEN)SelectObject(hdcMem, hPenWinBorder);
    MoveToEx(hdcMem, 0, 0, nullptr);
    LineTo(hdcMem, w - 1, 0);
    LineTo(hdcMem, w - 1, h - 1);
    LineTo(hdcMem, 0, h - 1);
    LineTo(hdcMem, 0, 0);
    SelectObject(hdcMem, hPenOld);
    DeleteObject(hPenWinBorder);

    // Create custom typography using native ClearType renderer
    HFONT hFontTitle = CreateFontW(14, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                   OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                   DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    HFONT hFontSection = CreateFontW(11, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                     OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                     DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    HFONT hFontDetail = CreateFontW(11, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                    OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                    DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

    SetBkMode(hdcMem, TRANSPARENT);

    // ---- Header: app title + live clock ----
    SYSTEMTIME st;
    GetLocalTime(&st);
    static const wchar_t* const s_months[] = {
        L"Jan",L"Feb",L"Mar",L"Apr",L"May",L"Jun",
        L"Jul",L"Aug",L"Sep",L"Oct",L"Nov",L"Dec"
    };
    static const wchar_t* const s_days[] = {
        L"Sun",L"Mon",L"Tue",L"Wed",L"Thu",L"Fri",L"Sat"
    };
    wchar_t timeStr[48];
    swprintf_s(timeStr, L"%s, %d %s %d %02d:%02d:%02d", s_days[st.wDayOfWeek], st.wDay, s_months[st.wMonth - 1], st.wYear, st.wHour, st.wMinute, st.wSecond);

    RECT rHeaderTop = {12, 8, w - 12, 26};
    SelectObject(hdcMem, hFontTitle);
    SetTextColor(hdcMem, RGB(255, 255, 255));
    DrawTextW(hdcMem, AppName, -1, &rHeaderTop, DT_LEFT | DT_TOP | DT_SINGLELINE);
    DrawTextW(hdcMem, timeStr, -1, &rHeaderTop, DT_RIGHT | DT_TOP | DT_SINGLELINE);

    // ---- Card layout with running cursor ----
    const int32_t cardWidth = w - 24;
    const int32_t cardH = 26;
    const int32_t cardStep = cardH + 4;
    int32_t cy = 30;

    // Fetch all metrics up front
    const double memUsage    = monitor.GetMemoryUsagePercent();
    const double cpuUsage    = monitor.GetCpuUsage();
    const auto   diskMetrics = monitor.GetDiskMetrics();
    const auto   netMetrics  = monitor.GetNetworkMetrics();
    const auto&  gpuList     = monitor.GetGpuMetrics();

    static const COLORREF kColorRam  = RGB(155, 93,  229);
    static const COLORREF kColorCpu  = RGB(0,   242, 254);
    static const COLORREF kColorGpu  = RGB(57,  255, 20);
    static const COLORREF kColorDisk = RGB(255, 82,  82);
    static const COLORREF kColorNet  = RGB(255, 159, 10);

    // ---- 1. Memory (RAM) ----
    {
        RECT rCard = {12, cy, 12 + cardWidth, cy + cardH};
        DrawCard(hdcMem, rCard, kColorRam);
        SelectObject(hdcMem, hFontSection);
        SetTextColor(hdcMem, kColorRam);
        RECT rL = {24, cy + 6, 60, cy + 20};
        DrawTextW(hdcMem, L"RAM", -1, &rL, DT_LEFT | DT_TOP | DT_SINGLELINE);
        SelectObject(hdcMem, hFontDetail);
        SetTextColor(hdcMem, RGB(255, 255, 255));
        wchar_t s[32];
        swprintf_s(s, L"%.1f%%", memUsage);
        RECT rV = {60, cy + 6, 110, cy + 20};
        DrawTextW(hdcMem, s, -1, &rV, DT_LEFT | DT_TOP | DT_SINGLELINE);
        RECT rP = {115, cy + 9, w - 20, cy + 17};
        DrawProgressBar(hdcMem, rP, memUsage, kColorRam);
        cy += cardStep;
    }

    // ---- 2. CPU ----
    {
        RECT rCard = {12, cy, 12 + cardWidth, cy + cardH};
        DrawCard(hdcMem, rCard, kColorCpu);
        SelectObject(hdcMem, hFontSection);
        SetTextColor(hdcMem, kColorCpu);
        RECT rL = {24, cy + 6, 60, cy + 20};
        DrawTextW(hdcMem, L"CPU", -1, &rL, DT_LEFT | DT_TOP | DT_SINGLELINE);
        SelectObject(hdcMem, hFontDetail);
        SetTextColor(hdcMem, RGB(255, 255, 255));
        wchar_t s[32];
        swprintf_s(s, L"%.1f%%", cpuUsage);
        RECT rV = {60, cy + 6, 110, cy + 20};
        DrawTextW(hdcMem, s, -1, &rV, DT_LEFT | DT_TOP | DT_SINGLELINE);
        RECT rP = {115, cy + 9, w - 20, cy + 17};
        DrawProgressBar(hdcMem, rP, cpuUsage, kColorCpu);
        cy += cardStep;
    }

    // ---- 3. GPU (one card per adapter) ----
    for (size_t gi = 0; gi < gpuList.size(); ++gi) {
        const auto& gm = gpuList[gi];
        RECT rCard = {12, cy, 12 + cardWidth, cy + cardH};
        DrawCard(hdcMem, rCard, kColorGpu);
        SelectObject(hdcMem, hFontSection);
        SetTextColor(hdcMem, kColorGpu);
        wchar_t gpuLabel[16];
        swprintf_s(gpuLabel, L"GPU%zu", gi + 1);
        RECT rL = {24, cy + 6, 60, cy + 20};
        DrawTextW(hdcMem, gpuLabel, -1, &rL, DT_LEFT | DT_TOP | DT_SINGLELINE);
        SelectObject(hdcMem, hFontDetail);
        SetTextColor(hdcMem, RGB(255, 255, 255));
        wchar_t s[32];
        swprintf_s(s, L"%.1f%%", gm.loadPercent);
        RECT rV = {60, cy + 6, 110, cy + 20};
        DrawTextW(hdcMem, s, -1, &rV, DT_LEFT | DT_TOP | DT_SINGLELINE);
        RECT rP = {115, cy + 9, w - 20, cy + 17};
        DrawProgressBar(hdcMem, rP, gm.loadPercent, kColorGpu);
        cy += cardStep;
    }

    // ---- 4. Disk I/O ----
    {
        RECT rCard = {12, cy, 12 + cardWidth, cy + cardH};
        DrawCard(hdcMem, rCard, kColorDisk);
        SelectObject(hdcMem, hFontSection);
        SetTextColor(hdcMem, kColorDisk);
        RECT rL = {24, cy + 6, 60, cy + 20};
        DrawTextW(hdcMem, L"DISK", -1, &rL, DT_LEFT | DT_TOP | DT_SINGLELINE);
        SelectObject(hdcMem, hFontDetail);
        SetTextColor(hdcMem, RGB(255, 255, 255));
        wchar_t readStr[16];
        FormatNetworkSpeed(16, readStr, diskMetrics.readBytesPerSec);
        wchar_t writeStr[16];
        FormatNetworkSpeed(16, writeStr, diskMetrics.writeBytesPerSec);
        swprintf_s(g_monitor.GetTextBuffer(), ResourceMonitor::kBufferWChars, L"R: %s  W: %s", readStr, writeStr);
        RECT rV = {60, cy + 6, w - 20, cy + 20};
        DrawTextW(hdcMem, g_monitor.GetTextBuffer(), -1, &rV, DT_LEFT | DT_TOP | DT_SINGLELINE);
        cy += cardStep;
    }

    // ---- 5. Network ----
    {
        RECT rCard = {12, cy, 12 + cardWidth, cy + cardH};
        DrawCard(hdcMem, rCard, kColorNet);
        SelectObject(hdcMem, hFontSection);
        SetTextColor(hdcMem, kColorNet);
        RECT rL = {24, cy + 6, 60, cy + 20};
        DrawTextW(hdcMem, L"NET", -1, &rL, DT_LEFT | DT_TOP | DT_SINGLELINE);
        SelectObject(hdcMem, hFontDetail);
        SetTextColor(hdcMem, RGB(255, 255, 255));
        wchar_t downloadStr[16];
        FormatNetworkSpeed(16, downloadStr, netMetrics.downloadSpeedBps);
        wchar_t uploadStr[16];
        FormatNetworkSpeed(16, uploadStr, netMetrics.uploadSpeedBps);
        swprintf_s(g_monitor.GetTextBuffer(), ResourceMonitor::kBufferWChars, L"D: %s  U: %s", downloadStr, uploadStr);
        RECT rV = {60, cy + 6, w - 20, cy + 20};
        DrawTextW(hdcMem, g_monitor.GetTextBuffer(), -1, &rV, DT_LEFT | DT_TOP | DT_SINGLELINE);
        cy += cardStep;
    }

    // Clean up typography handles
    DeleteObject(hFontTitle);
    DeleteObject(hFontSection);
    DeleteObject(hFontDetail);

    // BitBlt fully assembled buffer onto standard GDI clipping viewport
    BitBlt(hdc, 0, 0, w, h, hdcMem, 0, 0, SRCCOPY);

    SelectObject(hdcMem, hbmOld);
    DeleteDC(hdcMem);
    EndPaint(hwnd, &ps);
    return 0;
}
