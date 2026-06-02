#include "graph_control.h"

// Link with msimg32.lib for AlphaBlend
#pragma comment(lib, "msimg32.lib")

bool GraphControl::RegisterClass(HINSTANCE hInstance) {
    WNDCLASSEXW wcx = { 0 };
    wcx.cbSize = sizeof(wcx);
    wcx.style = CS_HREDRAW | CS_VREDRAW;
    wcx.lpfnWndProc = WindowProc;
    wcx.hInstance = hInstance;
    wcx.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcx.hbrBackground = nullptr; // Handled by double buffering
    wcx.lpszClassName = L"GraphControlClass";
    return RegisterClassExW(&wcx) != 0;
}

GraphControl::GraphControl() {
}

GraphControl::~GraphControl() {
    if (m_hbmBuffer) {
        DeleteObject(m_hbmBuffer);
    }
}

void GraphControl::SetColors(COLORREF lineColor, COLORREF fillColor, COLORREF gridColor) {
    m_lineColor = lineColor;
    m_fillColor = fillColor;
    m_gridColor = gridColor;
    if (m_hwnd) {
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
}

void GraphControl::AddSample(double value) {
    m_samples.push_back(value);
    if (m_samples.size() > m_maxSamples) {
        m_samples.erase(m_samples.begin());
    }
    if (m_hwnd) {
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
}

void GraphControl::Clear() {
    m_samples.clear();
    if (m_hwnd) {
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
}

HWND GraphControl::Create(HWND hwndParent, int32_t x, int32_t y, int32_t width, int32_t height, UINT_PTR id) {
    m_hwnd = CreateWindowExW(
        0,
        L"GraphControlClass",
        L"",
        WS_CHILD | WS_VISIBLE,
        x, y, width, height,
        hwndParent,
        (HMENU)id,
        GetModuleHandle(nullptr),
        this
    );
    return m_hwnd;
}

LRESULT CALLBACK GraphControl::WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    GraphControl* pThis = nullptr;
    if (uMsg == WM_NCCREATE) {
        LPCREATESTRUCTW lpcs = (LPCREATESTRUCTW)lParam;
        pThis = (GraphControl*)lpcs->lpCreateParams;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)pThis);
    } else {
        pThis = (GraphControl*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    }

    if (pThis) {
        pThis->m_hwnd = hwnd;
        switch (uMsg) {
            case WM_PAINT:
                return pThis->OnPaint(hwnd);
            case WM_ERASEBKGND:
                return 1; // Prevent background erasing to avoid flicker
            case WM_SIZE:
                // Size changes are handled dynamically in OnPaint via GetClientRect
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            case WM_DESTROY:
                if (pThis->m_hbmBuffer) {
                    DeleteObject(pThis->m_hbmBuffer);
                    pThis->m_hbmBuffer = nullptr;
                }
                break;
        }
    }
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

LRESULT GraphControl::OnPaint(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    
    RECT rect;
    GetClientRect(hwnd, &rect);
    int32_t w = rect.right - rect.left;
    int32_t h = rect.bottom - rect.top;

    if (w <= 0 || h <= 0) {
        EndPaint(hwnd, &ps);
        return 0;
    }

    // Recreate buffer bitmap if dimensions changed
    if (!m_hbmBuffer || m_bufferWidth != w || m_bufferHeight != h) {
        if (m_hbmBuffer) DeleteObject(m_hbmBuffer);
        HDC hdcScreen = GetDC(nullptr);
        m_hbmBuffer = CreateCompatibleBitmap(hdcScreen, w, h);
        ReleaseDC(nullptr, hdcScreen);
        m_bufferWidth = w;
        m_bufferHeight = h;
    }

    HDC hdcMem = CreateCompatibleDC(hdc);
    HBITMAP hbmOld = (HBITMAP)SelectObject(hdcMem, m_hbmBuffer);

    // 1. Fill background with sleek dark color
    HBRUSH hbrBg = CreateSolidBrush(m_bgColor);
    FillRect(hdcMem, &rect, hbrBg);
    DeleteObject(hbrBg);

    // 2. Draw border
    HPEN hPenBorder = CreatePen(PS_SOLID, 1, m_gridColor);
    HPEN hPenOld = (HPEN)SelectObject(hdcMem, hPenBorder);
    MoveToEx(hdcMem, 0, 0, nullptr);
    LineTo(hdcMem, w - 1, 0);
    LineTo(hdcMem, w - 1, h - 1);
    LineTo(hdcMem, 0, h - 1);
    LineTo(hdcMem, 0, 0);
    SelectObject(hdcMem, hPenOld);
    DeleteObject(hPenBorder);

    // 3. Draw horizontal and vertical grid lines
    HPEN hPenGrid = CreatePen(PS_DOT, 1, m_gridColor);
    hPenOld = (HPEN)SelectObject(hdcMem, hPenGrid);
    
    // Draw 4 horizontal grid segments
    int32_t gridLines = 4;
    for (int32_t i = 1; i < gridLines; ++i) {
        int32_t y = (h * i) / gridLines;
        MoveToEx(hdcMem, 1, y, nullptr);
        LineTo(hdcMem, w - 2, y);
    }
    
    // Draw vertical grid lines every 10 samples
    double sampleStep = (double)(w - 2) / (double)(m_maxSamples - 1);
    for (size_t i = 10; i < m_maxSamples; i += 10) {
        int32_t x = (int32_t)(i * sampleStep) + 1;
        MoveToEx(hdcMem, x, 1, nullptr);
        LineTo(hdcMem, x, h - 2);
    }
    
    SelectObject(hdcMem, hPenOld);
    DeleteObject(hPenGrid);

    // 4. Plot historical data
    if (!m_samples.empty()) {
        double maxVal = m_maxValue;
        if (!m_isStaticScale) {
            // Dynamic scale: Find max seen sample, pad to 1.1x to prevent line bumping top
            double currentMax = 1024.0 * 1024.0; // Start with 1 MB/s minimum dynamic scale limit
            for (double val : m_samples) {
                if (val > currentMax) currentMax = val;
            }
            maxVal = currentMax * 1.1;
        }

        std::vector<POINT> pts;
        pts.reserve(m_samples.size());
        
        // Map samples into pixel coordinates
        // Using X bounds [1, w-2] and Y bounds [2, h-4]
        for (size_t i = 0; i < m_samples.size(); ++i) {
            double val = m_samples[i];
            double pct = val / maxVal;
            if (pct > 1.0) pct = 1.0;
            if (pct < 0.0) pct = 0.0;
            
            int32_t x = (int32_t)(i * sampleStep) + 1;
            int32_t y = h - 2 - (int32_t)(pct * (double)(h - 4));
            pts.push_back({ x, y });
        }

        // Draw filled polygon underneath the line with low opacity (AlphaBlend)
        if (pts.size() > 1) {
            HDC hdcAlpha = CreateCompatibleDC(hdcMem);
            HBITMAP hbmAlpha = CreateCompatibleBitmap(hdcMem, w, h);
            HBITMAP hbmAlphaOld = (HBITMAP)SelectObject(hdcAlpha, hbmAlpha);

            // GDI AlphaBlend blending source needs solid black background first
            HBRUSH hbrBlack = (HBRUSH)GetStockObject(BLACK_BRUSH);
            RECT rTemp = { 0, 0, w, h };
            FillRect(hdcAlpha, &rTemp, hbrBlack);

            std::vector<POINT> polyPts = pts;
            // Close the shape by adding bottom-right and bottom-left bounds
            polyPts.push_back({ pts.back().x, h - 2 });
            polyPts.push_back({ pts.front().x, h - 2 });

            HBRUSH hbrFill = CreateSolidBrush(m_fillColor);
            HBRUSH hbrAlphaOld = (HBRUSH)SelectObject(hdcAlpha, hbrFill);
            HPEN hPenNull = CreatePen(PS_NULL, 0, 0);
            HPEN hPenAlphaOld = (HPEN)SelectObject(hdcAlpha, hPenNull);

            Polygon(hdcAlpha, polyPts.data(), (int)polyPts.size());

            SelectObject(hdcAlpha, hPenAlphaOld);
            SelectObject(hdcAlpha, hbrAlphaOld);
            DeleteObject(hPenNull);
            DeleteObject(hbrFill);

            // Perform hardware-accelerated blend
            BLENDFUNCTION bf = { 0 };
            bf.BlendOp = AC_SRC_OVER;
            bf.SourceConstantAlpha = 45; // ~17% transparency
            bf.AlphaFormat = 0;

            AlphaBlend(hdcMem, 0, 0, w, h, hdcAlpha, 0, 0, w, h, bf);

            SelectObject(hdcAlpha, hbmAlphaOld);
            DeleteObject(hbmAlpha);
            DeleteDC(hdcAlpha);
        }

        // Draw the foreground line (neon glowing visual look)
        HPEN hPenLine = CreatePen(PS_SOLID, 2, m_lineColor);
        hPenOld = (HPEN)SelectObject(hdcMem, hPenLine);
        Polyline(hdcMem, pts.data(), (int)pts.size());
        SelectObject(hdcMem, hPenOld);
        DeleteObject(hPenLine);
    }

    // 5. Draw labels and text overlay
    HFONT hFont = CreateFontW(14, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 
                              OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, 
                              DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    HFONT hFontOld = (HFONT)SelectObject(hdcMem, hFont);

    SetBkMode(hdcMem, TRANSPARENT);
    SetTextColor(hdcMem, m_textColor);

    // Draw Title
    RECT rTitle = { 10, 8, w - 10, 30 };
    DrawTextW(hdcMem, m_title.c_str(), -1, &rTitle, DT_LEFT | DT_TOP | DT_SINGLELINE);

    // Draw Current Value
    if (!m_samples.empty()) {
        wchar_t valStr[64];
        if (m_isStaticScale) {
            swprintf_s(valStr, L"%.1f%%", m_samples.back());
        } else {
            // Dynamic scale bandwidth speed formatting
            double bps = m_samples.back();
            if (bps >= 1024.0 * 1024.0 * 1024.0) {
                swprintf_s(valStr, L"%.2f GB/s", bps / (1024.0 * 1024.0 * 1024.0));
            } else if (bps >= 1024.0 * 1024.0) {
                swprintf_s(valStr, L"%.2f MB/s", bps / (1024.0 * 1024.0));
            } else if (bps >= 1024.0) {
                swprintf_s(valStr, L"%.1f KB/s", bps / 1024.0);
            } else {
                swprintf_s(valStr, L"%.0f B/s", bps);
            }
        }
        RECT rVal = { 10, 8, w - 10, 30 };
        DrawTextW(hdcMem, valStr, -1, &rVal, DT_RIGHT | DT_TOP | DT_SINGLELINE);
    }

    SelectObject(hdcMem, hFontOld);
    DeleteObject(hFont);

    // Blit fully assembled frame to the screen
    BitBlt(hdc, 0, 0, w, h, hdcMem, 0, 0, SRCCOPY);

    SelectObject(hdcMem, hbmOld);
    DeleteDC(hdcMem);
    EndPaint(hwnd, &ps);
    return 0;
}
