#ifndef INC_LOCK_GUI_H
#define INC_LOCK_GUI_H

#include <string_view>
#include <windows.h>

namespace wperf
{
// Runs the dedicated Lock Inspector UI and returns when its window is closed and
// any in-flight bounded scan has finished. The desktop monitor is not initialized.
int RunLockInspectorGui(HINSTANCE instance, int showCommand, std::wstring_view initialPath);
} // namespace wperf

#endif // INC_LOCK_GUI_H
