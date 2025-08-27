#include "pch.h"
#include <common/utils/ProcessWaiter.h>
#include <common/utils/EventWaiter.h>
#include <common/logger/logger.h>
#include <common/utils/logger_helper.h>
#include <common/interop/shared_constants.h>
#include "Overlay.h"

int WINAPI wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE, _In_ PWSTR lpCmdLine, _In_ int)
{
    LoggerHelpers::init_logger(L"MousePointerCrosshairsUI", L"", LogSettings::mousePointerCrosshairsLoggerName);

    // Single-instance mutex like ColorPicker
    HANDLE mutex = CreateMutex(nullptr, TRUE, L"Local\\PowerToys_MousePointerCrosshairs_InstanceMutex");
    if (!mutex || GetLastError() == ERROR_ALREADY_EXISTS)
    {
        if (mutex)
        {
            CloseHandle(mutex);
        }
        return 0;
    }

    // Exit when Runner exits if pid provided
    std::wstring pid = std::wstring(lpCmdLine);
    if (!pid.empty())
    {
        auto mainThreadId = GetCurrentThreadId();
        ProcessWaiter::OnProcessTerminate(pid, [mainThreadId](int /*err*/) {
            PostThreadMessage(mainThreadId, WM_QUIT, 0, 0);
        });
    }

    // Initialize overlay window/composition
    Overlay overlay(hInstance);
    if (!overlay.Initialize())
    {
        return 0;
    }

    // Event waiters: map shared events into overlay control
    EventWaiter showWaiter(CommonSharedConstants::SHOW_MOUSE_CROSSHAIRS_SHARED_EVENT, [&overlay](DWORD) {
        overlay.Switch();
        overlay.RequestUpdatePosition();
    });

    EventWaiter glidingWaiter(CommonSharedConstants::GLIDING_CURSOR_HOTKEY_SHARED_EVENT, [&overlay](DWORD) {
        overlay.ToggleGlidingCursor();
    });

    EventWaiter telemetryWaiter(CommonSharedConstants::MOUSE_CROSSHAIRS_SEND_SETTINGS_TELEMETRY_EVENT, [](DWORD) {
        Logger::trace(L"MousePointerCrosshairsUI telemetry request received");
    });

    auto mainThreadId = GetCurrentThreadId();
    EventWaiter terminateWaiter(CommonSharedConstants::TERMINATE_MOUSE_CROSSHAIRS_SHARED_EVENT, [mainThreadId](DWORD) {
        PostThreadMessage(mainThreadId, WM_QUIT, 0, 0);
    });

    // Message loop
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    overlay.Terminate();
    CloseHandle(mutex);
    return 0;
}
