#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef WINVER
#define WINVER 0x0601
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif

#include <windows.h>

struct NetworkMetrics {
    double downloadSpeedBps = 0.0;
    double uploadSpeedBps   = 0.0;
};

struct DiskMetrics {
    double readBytesPerSec  = 0.0;
    double writeBytesPerSec = 0.0;
};

struct GpuMetrics {
    double loadPercent = 0.0;
};

// Lightweight non-owning view returned by GetGpuMetrics().
// Supports the same .size() / operator[] usage as std::vector.
struct GpuMetricsView {
    const GpuMetrics* data  = nullptr;
    DWORD             count = 0;
    size_t            size()                const { return count; }
    const GpuMetrics& operator[](size_t i)  const { return data[i]; }
};

class ResourceMonitor {
public:
    inline static constexpr DWORD kBufferSize   = 2048;
    inline static constexpr DWORD kBufferWChars = 2048/sizeof(wchar_t);

    ResourceMonitor();
    ~ResourceMonitor();

    void Initialize();
    void Terminate();
    void Update();

    double GetCpuUsage()           const { return m_cpuUsage; }
    double GetMemoryUsagePercent() const { return m_memUsagePercent; }

    const NetworkMetrics& GetNetworkMetrics() const { return m_networkMetrics; }
    const DiskMetrics&    GetDiskMetrics()    const { return m_diskMetrics; }
    GpuMetricsView GetGpuMetrics()     const { return {m_gpuData, m_gpuCount}; }

    wchar_t* GetTextBuffer() { return m_textBuffer; }   
private:
    void UpdateCpuUsage();
    void UpdateMemoryUsage();
    void InitNetwork();
    void UpdateNetworkSpeed();
    void InitDisk();
    void UpdateDiskMetrics();
    void InitGpu();
    void UpdateGpuMetrics();

    // CPU state
    FILETIME m_prevIdleTime   = {};
    FILETIME m_prevKernelTime = {};
    FILETIME m_prevUserTime   = {};
    double   m_cpuUsage       = 0.0;

    // Memory state
    double m_memUsagePercent = 0.0;

    // Network state
    // m_ifIndices points into m_slab; populated once in InitNetwork, read-only thereafter
    DWORD*        m_ifIndices     = nullptr;
    DWORD         m_ifCount       = 0;
    ULONGLONG     m_prevInOctets  = 0;
    ULONGLONG     m_prevOutOctets = 0;
    LARGE_INTEGER m_prevNetTime   = {};
    LARGE_INTEGER m_perfFreq      = {};
    NetworkMetrics m_networkMetrics{};

    // Disk state
    HANDLE      m_hDiskQuery        = nullptr;
    HANDLE      m_hDiskReadCounter  = nullptr;
    HANDLE      m_hDiskWriteCounter = nullptr;
    DiskMetrics m_diskMetrics{};

    // GPU state
    // m_gpuCounters and m_gpuData point into m_slab
    HANDLE*     m_gpuCounters = nullptr;
    GpuMetrics* m_gpuData     = nullptr;
    DWORD       m_gpuCount    = 0;
    HANDLE      m_hGpuQuery   = nullptr;

    // PDH counter result buffer: separate VirtualAlloc, page-rounded,
    // reallocated only when the required size exceeds current capacity
    BYTE*  m_gpuBuf    = nullptr;
    DWORD  m_gpuBufCap = 0;

    // Temporary text buffer for formatting metric strings; shared across all calls, page-rounded
    wchar_t* m_textBuffer = nullptr;

    // Single VirtualAlloc page holding all small fixed arrays:
    //   [kSlabIfOff  .. ) DWORD[kMaxIf]    interface indices
    //   [kSlabHndOff .. ) HANDLE[kMaxGpu]  PDH counter handles
    //   [kSlabMetOff .. ) GpuMetrics[kMaxGpu]
    void* m_slab = nullptr;
};
