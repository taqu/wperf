#pragma once
#include <windows.h>
#include <cstdint>
#include <vector>
#include <string>

class GraphControl {
public:
    static bool RegisterClass(HINSTANCE hInstance);

    GraphControl();
    ~GraphControl();

    void SetTitle(const std::wstring& title) { m_title = title; }
    void SetColors(COLORREF lineColor, COLORREF fillColor, COLORREF gridColor);
    void SetScaleStatic(double maxVal) { m_isStaticScale = true; m_maxValue = maxVal; }
    void SetScaleDynamic() { m_isStaticScale = false; m_maxValue = 1.0; }

    void AddSample(double value);
    void Clear();

    HWND Create(HWND hwndParent, int32_t x, int32_t y, int32_t width, int32_t height, UINT_PTR id);
    HWND GetHWND() const { return m_hwnd; }

private:
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    LRESULT OnPaint(HWND hwnd);

    HWND m_hwnd = nullptr;
    std::wstring m_title;
    
    // Styling
    COLORREF m_lineColor = RGB(0, 242, 254);
    COLORREF m_fillColor = RGB(0, 242, 254);
    COLORREF m_gridColor = RGB(45, 45, 55);
    COLORREF m_bgColor = RGB(22, 22, 26);
    COLORREF m_textColor = RGB(220, 220, 220);

    // Data
    std::vector<double> m_samples;
    size_t m_maxSamples = 60; // 60 seconds history
    
    bool m_isStaticScale = true;
    double m_maxValue = 100.0;

    // Double buffering cache
    HBITMAP m_hbmBuffer = nullptr;
    int32_t m_bufferWidth = 0;
    int32_t m_bufferHeight = 0;
};
