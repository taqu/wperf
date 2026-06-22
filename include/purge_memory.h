#ifndef INC_PURGE_MEMORY_H
#define INC_PURGE_MEMORY_H
#ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#endif
#ifndef WINVER
#    define WINVER 0x0601
#endif
#ifndef _WIN32_WINNT
#    define _WIN32_WINNT 0x0601
#endif
#include <windows.h>

namespace wperf
{
struct PurgeMemoryProcesses
{
    inline static constexpr DWORD MaxProcesses = 1024;
    inline static constexpr DWORD ProcessPerOneTime = 16;
    inline bool isEnd() const
    {
        return numProcesses_<=current_;
    }
    DWORD current_;
    DWORD numProcesses_;
    DWORD processes_[MaxProcesses];
};

void PurgeMemory(HANDLE hProcess);
void BeginMemory(PurgeMemoryProcesses& processes);
void PurgeMemory(PurgeMemoryProcesses& processes);
}
#endif //INC_PURGE_MEMORY_H
