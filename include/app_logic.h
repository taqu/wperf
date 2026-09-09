#ifndef INC_APP_LOGIC_H
#define INC_APP_LOGIC_H
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cwchar>

namespace wperf
{
inline void FormatBytes(size_t size, wchar_t* buffer, uint64_t bytes)
{
    constexpr float Giga = 1024.0f * 1024.0f * 1024.0f;
    swprintf_s(buffer, size, L"%.1f GB", (float)bytes / Giga);
}

inline void FormatNetworkSpeed(size_t size, wchar_t* buffer, double bps)
{
    constexpr float Giga = 1024.0f * 1024.0f * 1024.0f;
    constexpr float Mega = 1024.0f * 1024.0f;
    constexpr float Kilo = 1024.0f;
    if(bps >= Giga)
        swprintf_s(buffer, size, L"%.2f GB/s", bps / Giga);
    else if(bps >= Mega)
        swprintf_s(buffer, size, L"%.2f MB/s", bps / Mega);
    else if(bps >= Kilo)
        swprintf_s(buffer, size, L"%.1f KB/s", bps / Kilo);
    else
        swprintf_s(buffer, size, L"%.0f B/s", bps);
}

struct AppSettings
{
    int32_t updateIntervalMs = 1000;
    bool alwaysOnTop = false;
};

// Normalize integers returned by the INI reader; file access stays in main.cpp.
inline AppSettings SettingsFromStoredValues(int32_t interval, bool alwaysOnTop)
{
    return {std::clamp(interval, 250, 60000), alwaysOnTop};
}

// The dialog retains the previous interval when the entered value is invalid.
inline void ApplyIntervalText(AppSettings& settings, const wchar_t* text)
{
    int32_t interval = _wtoi(text);
    if(interval >= 250 && interval <= 60000)
        settings.updateIntervalMs = interval;
}

inline double CpuUsageFromDeltas(uint64_t idle, uint64_t kernel, uint64_t user)
{
    // Windows kernel time includes idle time.
    uint64_t total = kernel + user;
    if(total > 0)
        return idle <= total ? 100.0 * (double)(total - idle) / (double)total : 0.0;
    return 0.0;
}
} // namespace wperf
#endif // INC_APP_LOGIC_H
