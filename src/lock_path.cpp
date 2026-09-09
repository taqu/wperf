#include "native_handle_backend.h"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <limits>

namespace wperf::detail
{
int CompareLockPaths(std::wstring_view left, std::wstring_view right)
{
    if(left.empty() || right.empty()) return left.empty() ? (right.empty() ? 0 : -1) : 1;
    if(left.size() > INT_MAX || right.size() > INT_MAX) return left.compare(right);
    return CompareStringOrdinal(left.data(), static_cast<int>(left.size()), right.data(),
                                static_cast<int>(right.size()), TRUE) - CSTR_EQUAL;
}

namespace
{
bool Prefix(std::wstring_view value, std::wstring_view prefix)
{
    return value.size() >= prefix.size() && CompareLockPaths(value.substr(0, prefix.size()), prefix) == 0;
}
}

std::wstring NormalizeLockPath(std::wstring_view path, std::span<const DeviceMapping> mappings)
{
    std::wstring result(path);
    std::replace(result.begin(), result.end(), L'/', L'\\');
    if(Prefix(result, L"\\\\?\\UNC\\") || Prefix(result, L"\\??\\UNC\\")) result = L"\\\\" + result.substr(8);
    else if(Prefix(result, L"\\\\?\\") || Prefix(result, L"\\??\\")) result.erase(0, 4);
    if(Prefix(result, L"\\Device\\Mup\\")) result = L"\\\\" + result.substr(12);
    const DeviceMapping* best = nullptr;
    for(const auto& mapping : mappings) {
        if(!mapping.device.empty() && Prefix(result, mapping.device)
            && (result.size() == mapping.device.size() || result[mapping.device.size()] == L'\\')
            && (!best || mapping.device.size() > best->device.size()
                || (mapping.device.size() == best->device.size() && mapping.drive < best->drive))) best = &mapping;
    }
    if(best) result = best->drive + result.substr(best->device.size());
    while(result.size() > 1 && result.back() == L'\\' && !(result.size() == 3 && result[1] == L':'))
        result.pop_back();
    return result;
}

bool MatchesLockPath(std::wstring_view target, std::wstring_view resource, bool directory)
{
    if(target.empty()) return false;
    if(CompareLockPaths(target, resource) == 0) return true;
    return directory && resource.size() > target.size() && Prefix(resource, target)
        && (target.back() == L'\\' || resource[target.size()] == L'\\');
}

size_t NextSnapshotSize(size_t current, size_t requested)
{
    constexpr size_t maximum = 64 * 1024 * 1024;
    if(current >= maximum || requested > maximum) return 0;
    const size_t doubled = current > maximum / 2 ? maximum : current * 2;
    return (std::max)(doubled, requested);
}
}
