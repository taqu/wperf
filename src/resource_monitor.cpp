#include "resource_monitor.h"
#include <stdio.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <netioapi.h>
#include <pdh.h>
#include <dxgi.h>

// ---------------------------------------------------------------------------
// Slab layout — one 4096-byte VirtualAlloc page holds all three small arrays.
// Offsets are compile-time constants so no runtime bookkeeping is needed.
// ---------------------------------------------------------------------------
static constexpr DWORD kMaxIf      = 32;
static constexpr DWORD kMaxGpu     = 8;
static constexpr DWORD kPageSize   = 4096;

static constexpr DWORD kSlabIfOff           = 0;
static constexpr DWORD kSlabHndOff          = kSlabIfOff           + kMaxIf  * sizeof(DWORD);
static constexpr DWORD kSlabMetOff          = kSlabHndOff          + kMaxGpu * sizeof(HANDLE);
static constexpr DWORD kSlabHndVramUsedOff  = kSlabMetOff          + kMaxGpu * sizeof(GpuMetrics);
static constexpr DWORD kSlabHndVramBudgetOff = kSlabHndVramUsedOff + kMaxGpu * sizeof(HANDLE);
static constexpr DWORD kSlabUsed            = kSlabHndVramBudgetOff + kMaxGpu * sizeof(HANDLE);
static_assert(kSlabUsed <= ResourceMonitor::kBufferSize, "slab overflows one page");

// ---------------------------------------------------------------------------

static ULONGLONG FileTimeToQuadWord(const FILETIME* ft)
{
    ULARGE_INTEGER uli;
    uli.LowPart  = ft->dwLowDateTime;
    uli.HighPart = ft->dwHighDateTime;
    return uli.QuadPart;
}

ResourceMonitor::ResourceMonitor()
{
}

ResourceMonitor::~ResourceMonitor()
{
}

void ResourceMonitor::Initialize()
{
    // Allocate and zero the slab; partition into typed pointers
    m_slab       = VirtualAlloc(nullptr, kPageSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    m_ifIndices             = reinterpret_cast<DWORD*>    (static_cast<BYTE*>(m_slab) + kSlabIfOff);
    m_gpuCounters           = reinterpret_cast<HANDLE*>   (static_cast<BYTE*>(m_slab) + kSlabHndOff);
    m_gpuData               = reinterpret_cast<GpuMetrics*>(static_cast<BYTE*>(m_slab) + kSlabMetOff);
    m_gpuVramUsedCounters   = reinterpret_cast<HANDLE*>   (static_cast<BYTE*>(m_slab) + kSlabHndVramUsedOff);
    m_textBuffer = reinterpret_cast<wchar_t*>(static_cast<BYTE*>(m_slab) + ResourceMonitor::kBufferSize);

    QueryPerformanceFrequency(&m_perfFreq);
    QueryPerformanceCounter(&m_prevNetTime);

    FILETIME idleTime, kernelTime, userTime;
    if(GetSystemTimes(&idleTime, &kernelTime, &userTime)) {
        m_prevIdleTime   = idleTime;
        m_prevKernelTime = kernelTime;
        m_prevUserTime   = userTime;
    }

    InitNetwork();
    InitDisk();
    InitGpu();
    Update();
}

void ResourceMonitor::Terminate()
{
    if(m_hDiskQuery) {
        PdhCloseQuery((PDH_HQUERY)m_hDiskQuery);
        m_hDiskQuery = nullptr;
    }
    if(m_hGpuQuery) {
        PdhCloseQuery((PDH_HQUERY)m_hGpuQuery);
        m_hGpuQuery = nullptr;
    }
    if(m_gpuBuf) {
        VirtualFree(m_gpuBuf, 0, MEM_RELEASE);
        m_gpuBuf = nullptr;
    }
    if(m_slab) {
        VirtualFree(m_slab, 0, MEM_RELEASE);
        m_textBuffer = nullptr;
        m_slab = nullptr;
    }
}

void ResourceMonitor::Update()
{
    UpdateCpuUsage();
    UpdateMemoryUsage();
    UpdateNetworkSpeed();
    UpdateDiskMetrics();
    UpdateGpuMetrics();
}

void ResourceMonitor::UpdateCpuUsage()
{
    FILETIME idleTime, kernelTime, userTime;
    if(!GetSystemTimes(&idleTime, &kernelTime, &userTime))
        return;

    ULONGLONG idle   = FileTimeToQuadWord(&idleTime);
    ULONGLONG kernel = FileTimeToQuadWord(&kernelTime);
    ULONGLONG user   = FileTimeToQuadWord(&userTime);

    ULONGLONG idleDiff   = idle   - FileTimeToQuadWord(&m_prevIdleTime);
    ULONGLONG kernelDiff = kernel - FileTimeToQuadWord(&m_prevKernelTime);
    ULONGLONG userDiff   = user   - FileTimeToQuadWord(&m_prevUserTime);

    // kernelDiff includes idle time on Windows
    ULONGLONG totalDiff = kernelDiff + userDiff;
    if(totalDiff > 0)
        m_cpuUsage = idleDiff <= totalDiff
            ? 100.0 * (double)(totalDiff - idleDiff) / (double)totalDiff
            : 0.0;
    else
        m_cpuUsage = 0.0;

    m_prevIdleTime   = idleTime;
    m_prevKernelTime = kernelTime;
    m_prevUserTime   = userTime;
}

void ResourceMonitor::UpdateMemoryUsage()
{
    MEMORYSTATUSEX ms;
    ms.dwLength = sizeof(ms);
    if(GlobalMemoryStatusEx(&ms)){
        m_memUsagePercent = ms.dwMemoryLoad;
        m_memUsage = ms.ullTotalPhys - ms.ullAvailPhys;
        m_memAvail = ms.ullAvailPhys;
    }
}

void ResourceMonitor::InitNetwork()
{
    PMIB_IF_TABLE2 pTable = nullptr;
    if(GetIfTable2(&pTable) != NO_ERROR)
        return;
    for(ULONG i = 0; i < pTable->NumEntries && m_ifCount < kMaxIf; ++i) {
        const auto& row = pTable->Table[i];
        if(row.Type == IF_TYPE_SOFTWARE_LOOPBACK) continue;
        if(row.OperStatus != IfOperStatusUp) continue;
        if(!row.InterfaceAndOperStatusFlags.ConnectorPresent) continue;
        m_ifIndices[m_ifCount++] = (DWORD)row.InterfaceIndex;
    }
    FreeMibTable(pTable);
}

void ResourceMonitor::UpdateNetworkSpeed()
{
    ULONGLONG currentIn  = 0;
    ULONGLONG currentOut = 0;

    MIB_IF_ROW2 row{};
    for(DWORD i = 0; i < m_ifCount; ++i) {
        row                = {};
        row.InterfaceIndex = m_ifIndices[i];
        if(GetIfEntry2(&row) == NO_ERROR) {
            currentIn  += row.InOctets;
            currentOut += row.OutOctets;
        }
    }

    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);

    if(m_prevInOctets > 0 || m_prevOutOctets > 0) {
        double elapsed = (double)(now.QuadPart - m_prevNetTime.QuadPart)
                       / (double)m_perfFreq.QuadPart;
        if(elapsed > 0.001) {
            double inSpeed  = (double)(currentIn  - m_prevInOctets)  / elapsed;
            double outSpeed = (double)(currentOut - m_prevOutOctets) / elapsed;
            m_networkMetrics.downloadSpeedBps = inSpeed  > 0.0 ? inSpeed  : 0.0;
            m_networkMetrics.uploadSpeedBps   = outSpeed > 0.0 ? outSpeed : 0.0;
        }
    }

    m_prevInOctets  = currentIn;
    m_prevOutOctets = currentOut;
    m_prevNetTime   = now;
}

void ResourceMonitor::InitDisk()
{
    PDH_HQUERY hQuery = nullptr;
    if(PdhOpenQueryW(nullptr, 0, &hQuery) != ERROR_SUCCESS)
        return;
    PDH_HCOUNTER hRead = nullptr, hWrite = nullptr;
    PdhAddEnglishCounterW(hQuery, L"\\PhysicalDisk(_Total)\\Disk Read Bytes/sec",  0, &hRead);
    PdhAddEnglishCounterW(hQuery, L"\\PhysicalDisk(_Total)\\Disk Write Bytes/sec", 0, &hWrite);
    PdhCollectQueryData(hQuery);
    m_hDiskQuery        = (HANDLE)hQuery;
    m_hDiskReadCounter  = (HANDLE)hRead;
    m_hDiskWriteCounter = (HANDLE)hWrite;
}

void ResourceMonitor::UpdateDiskMetrics()
{
    if(!m_hDiskQuery) return;
    if(PdhCollectQueryData((PDH_HQUERY)m_hDiskQuery) != ERROR_SUCCESS) return;

    PDH_FMT_COUNTERVALUE val{};
    if(m_hDiskReadCounter &&
       PdhGetFormattedCounterValue((PDH_HCOUNTER)m_hDiskReadCounter,
           PDH_FMT_DOUBLE, nullptr, &val) == ERROR_SUCCESS)
        m_diskMetrics.readBytesPerSec = val.doubleValue > 0.0 ? val.doubleValue : 0.0;

    if(m_hDiskWriteCounter &&
       PdhGetFormattedCounterValue((PDH_HCOUNTER)m_hDiskWriteCounter,
           PDH_FMT_DOUBLE, nullptr, &val) == ERROR_SUCCESS)
        m_diskMetrics.writeBytesPerSec = val.doubleValue > 0.0 ? val.doubleValue : 0.0;
}

void ResourceMonitor::InitGpu()
{
    PDH_HQUERY hQuery = nullptr;
    if(PdhOpenQueryW(nullptr, 0, &hQuery) != ERROR_SUCCESS){
        return;
    }
    m_hGpuQuery = (HANDLE)hQuery;

    IDXGIFactory1* pFactory = nullptr;
    if(FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&pFactory)))
        return;

    UINT idx = 0;
    IDXGIAdapter1* pAdapter = nullptr;
    while(pFactory->EnumAdapters1(idx++, &pAdapter) != DXGI_ERROR_NOT_FOUND) {
        DXGI_ADAPTER_DESC1 desc{};
        pAdapter->GetDesc1(&desc);
        if(!(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) && m_gpuCount < kMaxGpu) {
            wchar_t path[256];
            swprintf_s(path,
                L"\\GPU Engine(*luid_0x%08X_0x%08X*engtype_3D*)\\Utilization Percentage",
                desc.AdapterLuid.HighPart, desc.AdapterLuid.LowPart);
            PDH_HCOUNTER hCounter = nullptr;
            PdhAddEnglishCounterW((PDH_HQUERY)m_hGpuQuery, path, 0, &hCounter);
            m_gpuCounters[m_gpuCount] = (HANDLE)hCounter;

            swprintf_s(path,
                L"\\GPU Adapter Memory(*luid_0x%08X_0x%08X*)\\Dedicated Usage",
                desc.AdapterLuid.HighPart, desc.AdapterLuid.LowPart);
            PDH_HCOUNTER hVramUsed = nullptr;
            PdhAddEnglishCounterW((PDH_HQUERY)m_hGpuQuery, path, 0, &hVramUsed);
            m_gpuVramUsedCounters[m_gpuCount] = (HANDLE)hVramUsed;

            m_gpuData[m_gpuCount] = {};
            ++m_gpuCount;
        }
        pAdapter->Release();
    }
    pFactory->Release();

    if(m_gpuCount > 0)
        PdhCollectQueryData((PDH_HQUERY)m_hGpuQuery);
}

void ResourceMonitor::UpdateGpuMetrics()
{
    if(!m_hGpuQuery || m_gpuCount == 0) return;
    PdhCollectQueryData((PDH_HQUERY)m_hGpuQuery);

    for(DWORD i = 0; i < m_gpuCount; ++i) {
        // 3D load
        {
            HANDLE hCounter = m_gpuCounters[i];
            if(hCounter) {
                DWORD needed = 0, count = 0;
                PdhGetFormattedCounterArrayW((PDH_HCOUNTER)hCounter,
                    PDH_FMT_DOUBLE, &needed, &count, nullptr);
                if(needed > 0) {
                    if(needed > m_gpuBufCap) {
                        if(m_gpuBuf) VirtualFree(m_gpuBuf, 0, MEM_RELEASE);
                        DWORD cap = (needed + kPageSize - 1) & ~(kPageSize - 1);
                        m_gpuBuf    = static_cast<BYTE*>(
                            VirtualAlloc(nullptr, cap, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
                        m_gpuBufCap = m_gpuBuf ? cap : 0;
                    }
                    if(m_gpuBuf) {
                        auto* pItems = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(m_gpuBuf);
                        if(PdhGetFormattedCounterArrayW((PDH_HCOUNTER)hCounter,
                               PDH_FMT_DOUBLE, &needed, &count, pItems) == ERROR_SUCCESS) {
                            double total = 0.0;
                            for(DWORD j = 0; j < count; ++j)
                                if(pItems[j].FmtValue.CStatus == 0)
                                    total += pItems[j].FmtValue.doubleValue;
                            m_gpuData[i].loadPercent = total > 100.0 ? 100.0 : total;
                        }
                    }
                }
            }
        }

        // VRAM used: sum Dedicated Usage across all instances for this GPU
        double vramUsed = 0.0;
        {
            HANDLE hCounter = m_gpuVramUsedCounters[i];
            if(hCounter) {
                DWORD needed = 0, count = 0;
                PdhGetFormattedCounterArrayW((PDH_HCOUNTER)hCounter,
                    PDH_FMT_LARGE, &needed, &count, nullptr);
                if(needed > 0) {
                    if(needed > m_gpuBufCap) {
                        if(m_gpuBuf) VirtualFree(m_gpuBuf, 0, MEM_RELEASE);
                        DWORD cap = (needed + kPageSize - 1) & ~(kPageSize - 1);
                        m_gpuBuf    = static_cast<BYTE*>(
                            VirtualAlloc(nullptr, cap, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
                        m_gpuBufCap = m_gpuBuf ? cap : 0;
                    }
                    if(m_gpuBuf) {
                        auto* pItems = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(m_gpuBuf);
                        if(PdhGetFormattedCounterArrayW((PDH_HCOUNTER)hCounter,
                               PDH_FMT_LARGE, &needed, &count, pItems) == ERROR_SUCCESS) {
                            LONGLONG total = 0;
                            for(DWORD j = 0; j < count; ++j)
                                if(pItems[j].FmtValue.CStatus == 0)
                                    total += pItems[j].FmtValue.largeValue;
                            vramUsed = (double)total;
                        }
                    }
                }
            }
        }
        m_gpuData[i].vramUsedBytes = vramUsed;
    }
}
