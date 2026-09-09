#include "lock_gui.h"
#include "lock_gui_presenter.h"
#include "lock_inspector.h"
#include "process_controller.h"
#include "resource.h"
#include <atomic>
#include <commctrl.h>
#include <filesystem>
#include <memory>
#include <process.h>
#include <shobjidl.h>
#include <string>

namespace wperf
{
namespace
{
    constexpr wchar_t ClassName[] = L"wperfLockInspector";
    constexpr int PathEdit = 4101;
    constexpr int BrowseFile = 4102;
    constexpr int BrowseFolder = 4103;
    constexpr int Inspect = IDOK;
    constexpr int DeepScan = 4105;
    constexpr int ResultList = 4106;
    constexpr int StatusLabel = 4107;
    constexpr int Refresh = 4108;
    constexpr int CloseNormallyButton = 4109;
    constexpr int ForceTerminateButton = 4110;
    constexpr int Close = IDCANCEL;
    constexpr int MinimumWidth = 680;
    constexpr int MinimumHeight = 430;

    struct ScanCompletion;
    struct ActionCompletion;

    struct WindowState
    {
        std::atomic<HWND> window = nullptr;
        HANDLE worker = nullptr;
        HANDLE completionEvent = nullptr;
        std::atomic<ScanCompletion*> completion = nullptr;
        HANDLE actionWorker = nullptr;
        HANDLE actionEvent = nullptr;
        std::atomic<ActionCompletion*> actionCompletion = nullptr;
        HFONT font = nullptr;
        HBRUSH background = nullptr;
        HBRUSH editBackground = nullptr;
        bool scanning = false;
        bool lastDeep = false;
        bool actionActive = false;
        int selectedRow = -1;
        std::vector<ProcessIdentity> identities;
        std::wstring selectedName;
        std::wstring actionFeedback;
    };

    struct ScanRequest
    {
        std::shared_ptr<WindowState> state;
        std::wstring path;
        bool deep = false;
    };

    struct ScanCompletion
    {
        LockInspectionResult result;
    };

    struct ActionRequest
    {
        std::shared_ptr<WindowState> state;
        ProcessIdentity identity;
        bool force = false;
    };

    struct ActionCompletion
    {
        ProcessControlResult result;
        bool force = false;
    };

    int Scale(HWND window, int value)
    {
        HDC dc = GetDC(window);
        const int dpi = dc ? GetDeviceCaps(dc, LOGPIXELSX) : 96;
        if(dc)
            ReleaseDC(window, dc);
        return MulDiv(value, dpi, 96);
    }

    void SetControlFont(HWND parent, int identifier, HFONT font)
    {
        SendDlgItemMessageW(parent, identifier, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    }

    void EnableScanControls(HWND window, bool enabled)
    {
        EnableWindow(GetDlgItem(window, Inspect), enabled);
        EnableWindow(GetDlgItem(window, DeepScan), enabled);
        EnableWindow(GetDlgItem(window, Refresh), enabled);
        EnableWindow(GetDlgItem(window, BrowseFile), enabled);
        EnableWindow(GetDlgItem(window, BrowseFolder), enabled);
        if(!enabled) {
            EnableWindow(GetDlgItem(window, CloseNormallyButton), FALSE);
            EnableWindow(GetDlgItem(window, ForceTerminateButton), FALSE);
        }
    }

    void Layout(HWND window)
    {
        RECT area{};
        GetClientRect(window, &area);
        const int margin = Scale(window, 14);
        const int gap = Scale(window, 8);
        const int controlHeight = Scale(window, 27);
        const int browseWidth = Scale(window, 92);
        const int buttonWidth = Scale(window, 92);
        const int actionWidth = Scale(window, 128);
        const int labelHeight = Scale(window, 20);
        int y = margin;
        MoveWindow(GetDlgItem(window, 4199), margin, y + Scale(window, 4), Scale(window, 38), labelHeight, TRUE);
        const int editX = margin + Scale(window, 42);
        const int folderX = area.right - margin - browseWidth;
        const int fileX = folderX - gap - browseWidth;
        MoveWindow(GetDlgItem(window, PathEdit), editX, y, fileX - gap - editX, controlHeight, TRUE);
        MoveWindow(GetDlgItem(window, BrowseFile), fileX, y, browseWidth, controlHeight, TRUE);
        MoveWindow(GetDlgItem(window, BrowseFolder), folderX, y, browseWidth, controlHeight, TRUE);
        y += controlHeight + gap;
        MoveWindow(GetDlgItem(window, Inspect), margin, y, buttonWidth, controlHeight, TRUE);
        MoveWindow(GetDlgItem(window, DeepScan), margin + buttonWidth + gap, y, buttonWidth, controlHeight, TRUE);
        MoveWindow(GetDlgItem(window, 4198), margin + 2 * (buttonWidth + gap), y + Scale(window, 4),
                   area.right - margin - (margin + 2 * (buttonWidth + gap)), labelHeight, TRUE);
        y += controlHeight + Scale(window, 14);
        MoveWindow(GetDlgItem(window, 4197), margin, y, area.right - 2 * margin, labelHeight, TRUE);
        y += labelHeight + Scale(window, 4);
        const int bottomY = area.bottom - margin - controlHeight;
        const int statusY = bottomY - gap - labelHeight;
        MoveWindow(GetDlgItem(window, ResultList), margin, y, area.right - 2 * margin, statusY - gap - y, TRUE);
        HWND list = GetDlgItem(window, ResultList);
        const int processWidth = Scale(window, 180);
        const int pidWidth = Scale(window, 80);
        const int minimumResourceWidth = Scale(window, 160);
        const int availableResourceWidth = static_cast<int>(area.right) - 2 * margin - processWidth - pidWidth - Scale(window, 6);
        const int resourceWidth = availableResourceWidth > minimumResourceWidth
                                      ? availableResourceWidth
                                      : minimumResourceWidth;
        ListView_SetColumnWidth(list, 0, processWidth);
        ListView_SetColumnWidth(list, 1, pidWidth);
        ListView_SetColumnWidth(list, 2, resourceWidth);
        MoveWindow(GetDlgItem(window, StatusLabel), margin, statusY, area.right - 2 * margin, labelHeight, TRUE);
        MoveWindow(GetDlgItem(window, CloseNormallyButton), margin + buttonWidth + gap, bottomY, actionWidth, controlHeight, TRUE);
        MoveWindow(GetDlgItem(window, ForceTerminateButton), margin + buttonWidth + gap + actionWidth + gap, bottomY,
                   actionWidth, controlHeight, TRUE);
        MoveWindow(GetDlgItem(window, Refresh), margin, bottomY, buttonWidth, controlHeight, TRUE);
        MoveWindow(GetDlgItem(window, Close), area.right - margin - buttonWidth, bottomY, buttonWidth, controlHeight, TRUE);
    }

    void UpdateActionControls(HWND window, WindowState& state)
    {
        const bool enabled = !state.scanning && !state.actionActive && state.selectedRow >= 0
                             && static_cast<size_t>(state.selectedRow) < state.identities.size()
                             && state.identities[static_cast<size_t>(state.selectedRow)].valid
                             && state.identities[static_cast<size_t>(state.selectedRow)].pid != GetCurrentProcessId();
        EnableWindow(GetDlgItem(window, CloseNormallyButton), enabled);
        EnableWindow(GetDlgItem(window, ForceTerminateButton), enabled);
    }

    std::wstring PickPath(HWND owner, bool folder)
    {
        const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        const bool uninitialize = SUCCEEDED(initialized);
        IFileOpenDialog* dialog = nullptr;
        std::wstring path;
        if(SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                      IID_PPV_ARGS(&dialog)))) {
            FILEOPENDIALOGOPTIONS options{};
            if(SUCCEEDED(dialog->GetOptions(&options))) {
                options = static_cast<FILEOPENDIALOGOPTIONS>(
                    options | FOS_FORCEFILESYSTEM | (folder ? FOS_PICKFOLDERS : FOS_FILEMUSTEXIST));
                dialog->SetOptions(options);
            }
            dialog->SetTitle(folder ? L"Select a folder to inspect" : L"Select a file to inspect");
            if(SUCCEEDED(dialog->Show(owner))) {
                IShellItem* item = nullptr;
                if(SUCCEEDED(dialog->GetResult(&item))) {
                    PWSTR selected = nullptr;
                    if(SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &selected))) {
                        path = selected;
                        CoTaskMemFree(selected);
                    }
                    item->Release();
                }
            }
            dialog->Release();
        }
        if(uninitialize)
            CoUninitialize();
        return path;
    }

    unsigned __stdcall ScanWorker(void* parameter)
    {
        std::unique_ptr<ScanRequest> request(static_cast<ScanRequest*>(parameter));
        auto completion = std::make_unique<ScanCompletion>();
        try {
            completion->result = InspectLocks(std::filesystem::path(request->path), LockInspectionOptions{request->deep});
        } catch(...) {
            completion->result.status = LockInspectionStatus::NativeScanFailure;
            completion->result.nativeError = ERROR_NOT_ENOUGH_MEMORY;
        }
        request->state->completion.store(completion.release());
        SetEvent(request->state->completionEvent);
        return 0;
    }

    unsigned __stdcall ActionWorker(void* parameter)
    {
        std::unique_ptr<ActionRequest> request(static_cast<ActionRequest*>(parameter));
        auto completion = std::make_unique<ActionCompletion>();
        completion->force = request->force;
        try {
            completion->result = request->force ? ForceTerminate(request->identity)
                                                : RequestGracefulClose(request->identity);
        } catch(...) {
            completion->result = {ProcessControlStatus::Failed, ERROR_NOT_ENOUGH_MEMORY, 0};
        }
        request->state->actionCompletion.store(completion.release());
        SetEvent(request->state->actionEvent);
        return 0;
    }

    std::wstring ActionStatus(const ProcessControlResult& result, bool force)
    {
        std::wstring status;
        switch(result.status) {
        case ProcessControlStatus::ProcessExited:
            status = force ? L"Process force terminated successfully." : L"Process exited successfully.";
            break;
        case ProcessControlStatus::CloseRequestSent:
            status = L"Close request sent.";
            break;
        case ProcessControlStatus::NoClosableWindow:
            status = L"No closable top-level window was found for this process.";
            break;
        case ProcessControlStatus::StillRunning:
            status = force ? L"Process is still running after force termination." : L"Process is still running after the close request.";
            break;
        case ProcessControlStatus::AlreadyExited:
            status = L"The process is no longer running.";
            break;
        case ProcessControlStatus::IdentityMismatch:
            status = L"The selected process changed since the scan. Refresh and try again.";
            break;
        case ProcessControlStatus::AccessDenied:
            status = L"The process could not be controlled because access was denied.";
            break;
        case ProcessControlStatus::SelfTarget:
            status = L"wperf cannot control itself.";
            break;
        case ProcessControlStatus::InvalidIdentity:
            status = L"The selected process identity is no longer valid. Refresh and try again.";
            break;
        case ProcessControlStatus::Failed:
            status = force ? L"Force termination failed." : L"The close request failed.";
            break;
        }
        if(result.nativeError != 0)
            status += L" Windows error: " + std::to_wstring(result.nativeError) + L'.';
        return status;
    }

    void BeginAction(HWND window, WindowState& state, bool force)
    {
        if(state.scanning || state.actionActive || state.selectedRow < 0
           || static_cast<size_t>(state.selectedRow) >= state.identities.size())
            return;
        const auto identity = state.identities[static_cast<size_t>(state.selectedRow)];
        if(!identity.valid || identity.pid == GetCurrentProcessId())
            return;
        if(force) {
            std::wstring prompt = L"Force terminate " + state.selectedName + L" (PID "
                                  + std::to_wstring(identity.pid) + L")?\n\nUnsaved data may be lost.";
            if(MessageBoxW(window, prompt.c_str(), L"Confirm force termination",
                           MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2)
               != IDYES)
                return;
        }

        state.actionActive = true;
        EnableScanControls(window, false);
        SetDlgItemTextW(window, StatusLabel, force ? L"Force termination in progress..." : L"Close request in progress...");
        auto request = std::make_unique<ActionRequest>();
        request->state = *reinterpret_cast<std::shared_ptr<WindowState>*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        request->identity = identity;
        request->force = force;
        state.actionEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if(!state.actionEvent) {
            state.actionActive = false;
            EnableScanControls(window, true);
            UpdateActionControls(window, state);
            SetDlgItemTextW(window, StatusLabel, L"Unable to create the process-control completion event.");
            return;
        }
        const uintptr_t thread = _beginthreadex(nullptr, 0, ActionWorker, request.get(), 0, nullptr);
        if(thread == 0) {
            CloseHandle(state.actionEvent);
            state.actionEvent = nullptr;
            state.actionActive = false;
            EnableScanControls(window, true);
            UpdateActionControls(window, state);
            SetDlgItemTextW(window, StatusLabel, L"Unable to start the process-control worker.");
            return;
        }
        request.release();
        state.actionWorker = reinterpret_cast<HANDLE>(thread);
    }

    void BeginScan(HWND window, WindowState& state, bool deep)
    {
        if(state.scanning)
            return;
        const int length = GetWindowTextLengthW(GetDlgItem(window, PathEdit));
        std::wstring path(static_cast<size_t>(length) + 1, L'\0');
        GetDlgItemTextW(window, PathEdit, path.data(), length + 1);
        path.resize(static_cast<size_t>(length));
        if(path.empty()) {
            SetDlgItemTextW(window, StatusLabel, L"Enter or select a file or directory to inspect.");
            SetFocus(GetDlgItem(window, PathEdit));
            return;
        }

        state.scanning = true;
        state.lastDeep = deep;
        EnableScanControls(window, false);
        SetDlgItemTextW(window, StatusLabel, deep ? L"Deep scan in progress..." : L"Inspection in progress...");
        ListView_DeleteAllItems(GetDlgItem(window, ResultList));
        auto request = std::make_unique<ScanRequest>();
        request->state = *reinterpret_cast<std::shared_ptr<WindowState>*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        request->path = std::move(path);
        request->deep = deep;
        state.completionEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if(!state.completionEvent) {
            state.scanning = false;
            EnableScanControls(window, true);
            SetDlgItemTextW(window, StatusLabel, L"Unable to create the inspection completion event.");
            return;
        }
        uintptr_t thread = _beginthreadex(nullptr, 0, ScanWorker, request.get(), 0, nullptr);
        if(thread == 0) {
            CloseHandle(state.completionEvent);
            state.completionEvent = nullptr;
            state.scanning = false;
            EnableScanControls(window, true);
            SetDlgItemTextW(window, StatusLabel, L"Unable to start the inspection worker.");
            return;
        }
        request.release();
        state.worker = reinterpret_cast<HANDLE>(thread);
    }

    void ShowResult(HWND window, WindowState& state, const LockGuiPresentation& view)
    {
        HWND list = GetDlgItem(window, ResultList);
        ListView_DeleteAllItems(list);
        state.identities.clear();
        state.selectedRow = -1;
        state.selectedName.clear();
        for(size_t index = 0; index < view.rows.size(); ++index) {
            const auto& row = view.rows[index];
            LVITEMW item{};
            item.mask = LVIF_TEXT;
            item.iItem = static_cast<int>(index);
            item.pszText = const_cast<wchar_t*>(row.process.c_str());
            const int actual = ListView_InsertItem(list, &item);
            const std::wstring pid = std::to_wstring(row.pid);
            ListView_SetItemText(list, actual, 1, const_cast<wchar_t*>(pid.c_str()));
            ListView_SetItemText(list, actual, 2, const_cast<wchar_t*>(row.resource.c_str()));
            ProcessIdentity identity{row.pid, row.startTime, row.startTime != 0};
            const ProcessIdentity captured = CaptureProcessIdentity(row.pid);
            if(captured.valid && (!identity.valid || identity.creationTime == captured.creationTime))
                identity = captured;
            else if(identity.valid && (!captured.valid || identity.creationTime != captured.creationTime))
                identity.valid = false;
            state.identities.push_back(identity);
        }
        std::wstring status = view.status;
        if(!state.actionFeedback.empty()) {
            if(!status.empty())
                status += L"  ";
            status += state.actionFeedback;
            state.actionFeedback.clear();
        }
        SetDlgItemTextW(window, StatusLabel, status.c_str());
        UpdateActionControls(window, state);
    }

    LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
    {
        auto* holder = reinterpret_cast<std::shared_ptr<WindowState>*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        WindowState* state = holder ? holder->get() : nullptr;
        switch(message) {
        case WM_NCCREATE: {
            const auto create = reinterpret_cast<CREATESTRUCTW*>(lParam);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
            return TRUE;
        }
        case WM_CREATE: {
            holder = reinterpret_cast<std::shared_ptr<WindowState>*>(GetWindowLongPtrW(window, GWLP_USERDATA));
            state = holder->get();
            state->window = window;
            state->background = CreateSolidBrush(RGB(18, 18, 20));
            state->editBackground = CreateSolidBrush(RGB(35, 35, 42));
            state->font = CreateFontW(-Scale(window, 14), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                      DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                      CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            const HINSTANCE instance = GetModuleHandleW(nullptr);
            CreateWindowExW(0, L"STATIC", L"Path:", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window,
                            reinterpret_cast<HMENU>(static_cast<INT_PTR>(4199)), instance, nullptr);
            CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                            0, 0, 0, 0, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(PathEdit)), instance, nullptr);
            CreateWindowExW(0, L"BUTTON", L"Browse File", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                            0, 0, 0, 0, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(BrowseFile)), instance, nullptr);
            CreateWindowExW(0, L"BUTTON", L"Browse Folder", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                            0, 0, 0, 0, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(BrowseFolder)), instance, nullptr);
            CreateWindowExW(0, L"BUTTON", L"Inspect", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                            0, 0, 0, 0, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(Inspect)), instance, nullptr);
            CreateWindowExW(0, L"BUTTON", L"Deep Scan", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                            0, 0, 0, 0, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(DeepScan)), instance, nullptr);
            CreateWindowExW(0, L"STATIC", L"Deep Scan checks system file handles and may take longer.",
                            WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window,
                            reinterpret_cast<HMENU>(static_cast<INT_PTR>(4198)), instance, nullptr);
            CreateWindowExW(0, L"STATIC", L"Processes using this resource:", WS_CHILD | WS_VISIBLE,
                            0, 0, 0, 0, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(4197)), instance, nullptr);
            HWND list = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                                        WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SHOWSELALWAYS,
                                        0, 0, 0, 0, window,
                                        reinterpret_cast<HMENU>(static_cast<INT_PTR>(ResultList)), instance, nullptr);
            ListView_SetExtendedListViewStyle(list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
            LVCOLUMNW column{LVCF_TEXT | LVCF_WIDTH};
            column.pszText = const_cast<wchar_t*>(L"Process");
            column.cx = Scale(window, 180);
            ListView_InsertColumn(list, 0, &column);
            column.pszText = const_cast<wchar_t*>(L"PID");
            column.cx = Scale(window, 80);
            ListView_InsertColumn(list, 1, &column);
            column.pszText = const_cast<wchar_t*>(L"Resource");
            column.cx = Scale(window, 360);
            ListView_InsertColumn(list, 2, &column);
            ListView_SetBkColor(list, RGB(24, 24, 29));
            ListView_SetTextBkColor(list, RGB(24, 24, 29));
            ListView_SetTextColor(list, RGB(230, 230, 235));
            CreateWindowExW(0, L"STATIC", L"Ready.", WS_CHILD | WS_VISIBLE,
                            0, 0, 0, 0, window,
                            reinterpret_cast<HMENU>(static_cast<INT_PTR>(StatusLabel)), instance, nullptr);
            CreateWindowExW(0, L"BUTTON", L"Refresh", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                            0, 0, 0, 0, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(Refresh)), instance, nullptr);
            CreateWindowExW(0, L"BUTTON", L"Close Normally", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                            0, 0, 0, 0, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(CloseNormallyButton)), instance, nullptr);
            CreateWindowExW(0, L"BUTTON", L"Force Terminate", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                            0, 0, 0, 0, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ForceTerminateButton)), instance, nullptr);
            CreateWindowExW(0, L"BUTTON", L"Close", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                            0, 0, 0, 0, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(Close)), instance, nullptr);
            const int controls[] = {4199, PathEdit, BrowseFile, BrowseFolder, Inspect, DeepScan, 4198,
                                    4197, ResultList, StatusLabel, Refresh, CloseNormallyButton, ForceTerminateButton, Close};
            for(int identifier: controls) SetControlFont(window, identifier, state->font);
            Layout(window);
            UpdateActionControls(window, *state);
            return 0;
        }
        case WM_SIZE:
            Layout(window);
            return 0;
        case WM_GETMINMAXINFO: {
            auto info = reinterpret_cast<MINMAXINFO*>(lParam);
            info->ptMinTrackSize = {Scale(window, MinimumWidth), Scale(window, MinimumHeight)};
            return 0;
        }
        case WM_COMMAND:
            if(!state)
                return 0;
            switch(LOWORD(wParam)) {
            case BrowseFile:
            case BrowseFolder: {
                const auto path = PickPath(window, LOWORD(wParam) == BrowseFolder);
                if(!path.empty())
                    SetDlgItemTextW(window, PathEdit, path.c_str());
                return 0;
            }
            case Inspect:
                BeginScan(window, *state, false);
                return 0;
            case DeepScan:
                BeginScan(window, *state, true);
                return 0;
            case Refresh:
                BeginScan(window, *state, state->lastDeep);
                return 0;
            case CloseNormallyButton:
                BeginAction(window, *state, false);
                return 0;
            case ForceTerminateButton:
                BeginAction(window, *state, true);
                return 0;
            case Close:
                DestroyWindow(window);
                return 0;
            }
            break;
        case WM_NOTIFY:
            if(state && reinterpret_cast<NMHDR*>(lParam)->idFrom == ResultList
               && reinterpret_cast<NMHDR*>(lParam)->code == LVN_ITEMCHANGED) {
                state->selectedRow = ListView_GetNextItem(GetDlgItem(window, ResultList), -1, LVNI_SELECTED);
                state->selectedName.clear();
                if(state->selectedRow >= 0) {
                    wchar_t name[512]{};
                    ListView_GetItemText(GetDlgItem(window, ResultList), state->selectedRow, 0, name, 512);
                    state->selectedName = name;
                }
                UpdateActionControls(window, *state);
                return 0;
            }
            break;
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORBTN: {
            HDC dc = reinterpret_cast<HDC>(wParam);
            SetTextColor(dc, RGB(210, 210, 220));
            SetBkMode(dc, TRANSPARENT);
            return reinterpret_cast<LRESULT>(state ? state->background : nullptr);
        }
        case WM_CTLCOLOREDIT: {
            HDC dc = reinterpret_cast<HDC>(wParam);
            SetTextColor(dc, RGB(255, 255, 255));
            SetBkColor(dc, RGB(35, 35, 42));
            return reinterpret_cast<LRESULT>(state ? state->editBackground : nullptr);
        }
        case WM_PAINT: {
            PAINTSTRUCT paint{};
            HDC dc = BeginPaint(window, &paint);
            RECT area{};
            GetClientRect(window, &area);
            FillRect(dc, &area, state->background);
            EndPaint(window, &paint);
            return 0;
        }
        case WM_ERASEBKGND: {
            RECT area{};
            GetClientRect(window, &area);
            FillRect(reinterpret_cast<HDC>(wParam), &area, state->background);
            return 1;
        }
        case WM_CLOSE:
            DestroyWindow(window);
            return 0;
        case WM_NCDESTROY:
            if(state) {
                state->window = nullptr;
                if(state->font)
                    DeleteObject(state->font);
                if(state->background)
                    DeleteObject(state->background);
                if(state->editBackground)
                    DeleteObject(state->editBackground);
                state->font = nullptr;
                state->background = nullptr;
                state->editBackground = nullptr;
            }
            SetWindowLongPtrW(window, GWLP_USERDATA, 0);
            return DefWindowProcW(window, message, wParam, lParam);
        }
        return DefWindowProcW(window, message, wParam, lParam);
    }
} // namespace

int RunLockInspectorGui(HINSTANCE instance, int showCommand, std::wstring_view initialPath)
{
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&controls);
    SetProcessDPIAware();
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = WindowProcedure;
    windowClass.hInstance = instance;
    windowClass.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_ICON_MAIN));
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.lpszClassName = ClassName;
    if(!RegisterClassExW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return 1;

    auto state = std::make_shared<WindowState>();
    auto holder = std::make_unique<std::shared_ptr<WindowState>>(state);
    auto* holderPointer = holder.get();
    HWND window = CreateWindowExW(WS_EX_APPWINDOW, ClassName, L"wperf Lock Inspector",
                                  WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                                  Scale(nullptr, MinimumWidth), Scale(nullptr, MinimumHeight),
                                  nullptr, nullptr, instance, holder.get());
    if(!window)
        return 1;
    SetDlgItemTextW(window, PathEdit, std::wstring(initialPath).c_str());
    holder.release();
    ShowWindow(window, showCommand);
    UpdateWindow(window);

    MSG message{};
    bool quit = false;
    const auto dispatch = [&state](MSG& current) {
        HWND active = state->window.load();
        if(!active || !IsDialogMessageW(active, &current)) {
            TranslateMessage(&current);
            DispatchMessageW(&current);
        }
    };
    while(!quit && (state->window.load() != nullptr || state->scanning || state->actionActive)) {
        if(state->scanning || state->actionActive) {
            HANDLE waitHandles[2]{};
            DWORD handleCount = 0;
            const DWORD scanIndex = state->scanning ? handleCount++ : MAXDWORD;
            if(state->scanning)
                waitHandles[scanIndex] = state->completionEvent;
            const DWORD actionIndex = state->actionActive ? handleCount++ : MAXDWORD;
            if(state->actionActive)
                waitHandles[actionIndex] = state->actionEvent;
            const DWORD wait = MsgWaitForMultipleObjects(handleCount, waitHandles, FALSE, INFINITE, QS_ALLINPUT);
            if(state->scanning && wait == WAIT_OBJECT_0 + scanIndex) {
                std::unique_ptr<ScanCompletion> completion(state->completion.exchange(nullptr));
                CloseHandle(state->completionEvent);
                state->completionEvent = nullptr;
                if(state->worker) {
                    WaitForSingleObject(state->worker, INFINITE);
                    CloseHandle(state->worker);
                    state->worker = nullptr;
                }
                state->scanning = false;
                if(HWND active = state->window.load()) {
                    ShowResult(active, *state, PresentLockInspection(completion->result));
                    EnableScanControls(active, true);
                }
                continue;
            }
            if(state->actionActive && wait == WAIT_OBJECT_0 + actionIndex) {
                std::unique_ptr<ActionCompletion> completion(state->actionCompletion.exchange(nullptr));
                CloseHandle(state->actionEvent);
                state->actionEvent = nullptr;
                if(state->actionWorker) {
                    WaitForSingleObject(state->actionWorker, INFINITE);
                    CloseHandle(state->actionWorker);
                    state->actionWorker = nullptr;
                }
                state->actionActive = false;
                state->actionFeedback = ActionStatus(completion->result, completion->force);
                if(HWND active = state->window.load()) {
                    EnableScanControls(active, true);
                    BeginScan(active, *state, state->lastDeep);
                }
                continue;
            }
            if(wait == WAIT_FAILED)
                break;
            while(PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                if(message.message == WM_QUIT) {
                    quit = true;
                    break;
                }
                dispatch(message);
            }
            continue;
        }
        const BOOL result = GetMessageW(&message, nullptr, 0, 0);
        if(result <= 0)
            break;
        dispatch(message);
    }
    if(state->worker) {
        WaitForSingleObject(state->worker, INFINITE);
        CloseHandle(state->worker);
    }
    if(state->completionEvent)
        CloseHandle(state->completionEvent);
    delete state->completion.exchange(nullptr);
    if(state->actionWorker) {
        WaitForSingleObject(state->actionWorker, INFINITE);
        CloseHandle(state->actionWorker);
    }
    if(state->actionEvent)
        CloseHandle(state->actionEvent);
    delete state->actionCompletion.exchange(nullptr);
    delete holderPointer;
    return 0;
}
} // namespace wperf
