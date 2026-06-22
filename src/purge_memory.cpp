#include "purge_memory.h"
#include <cassert>
#include <Psapi.h>

namespace wperf
{
void PurgeMemory(HANDLE hProcess)
{
    assert(nullptr != hProcess);
    EmptyWorkingSet(hProcess);
}

void BeginMemory(PurgeMemoryProcesses& processes)
{
    processes.current_ = 0;
    processes.numProcesses_ = 0;
    DWORD needed = 0;
    if(!EnumProcesses(processes.processes_, sizeof(DWORD)*PurgeMemoryProcesses::MaxProcesses, &needed)){
        return;
    }
    processes.numProcesses_ = needed / sizeof(DWORD);
}

void PurgeMemory(PurgeMemoryProcesses& processes)
{
    DWORD count = 0;
    for(; processes.current_<processes.numProcesses_ && count<PurgeMemoryProcesses::ProcessPerOneTime; ++processes.current_){
        HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_SET_QUOTA, FALSE, processes.processes_[processes.current_]);
        if(nullptr != hProcess){
            ++count;
            EmptyWorkingSet(hProcess);
            CloseHandle(hProcess);
        }
    }
}
}
