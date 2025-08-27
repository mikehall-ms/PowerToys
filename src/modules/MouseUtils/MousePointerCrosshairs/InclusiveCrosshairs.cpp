// InclusiveCrosshairs.cpp : Defines the entry point for the application.
//

#include "pch.h"
#include "InclusiveCrosshairs.h"
#include <common/interop/shared_constants.h>
#include <common/logger/logger.h>

// Thin stub: forward requests to UI process via shared events

void InclusiveCrosshairsApplySettings(InclusiveCrosshairsSettings& /*settings*/)
{
    // Settings are read by UI exe from file; no-op in stub
}

void InclusiveCrosshairsSwitch()
{
    Logger::trace("Stub: signaling SHOW_MOUSE_CROSSHAIRS_SHARED_EVENT (Switch)");
    HANDLE h = OpenEventW(EVENT_MODIFY_STATE, FALSE, CommonSharedConstants::SHOW_MOUSE_CROSSHAIRS_SHARED_EVENT);
    if (h)
    {
        SetEvent(h);
        CloseHandle(h);
    }
}

void InclusiveCrosshairsDisable()
{
    Logger::trace("Stub: signaling TERMINATE_MOUSE_CROSSHAIRS_SHARED_EVENT");
    HANDLE h = OpenEventW(EVENT_MODIFY_STATE, FALSE, CommonSharedConstants::TERMINATE_MOUSE_CROSSHAIRS_SHARED_EVENT);
    if (h)
    {
        SetEvent(h);
        CloseHandle(h);
    }
}

bool InclusiveCrosshairsIsEnabled()
{
    // UI exe owns state; return true if UI mutex exists
    HANDLE mutex = OpenMutexW(SYNCHRONIZE, FALSE, L"Local\\PowerToys_MousePointerCrosshairs_InstanceMutex");
    if (mutex)
    {
        CloseHandle(mutex);
        return true;
    }
    return false;
}

void InclusiveCrosshairsRequestUpdatePosition()
{
    Logger::trace("Stub: signaling REQUEST_UPDATE via SHOW_MOUSE_CROSSHAIRS_SHARED_EVENT (no-op)");
    HANDLE h = OpenEventW(EVENT_MODIFY_STATE, FALSE, CommonSharedConstants::SHOW_MOUSE_CROSSHAIRS_SHARED_EVENT);
    if (h)
    {
        SetEvent(h);
        CloseHandle(h);
    }
}

void InclusiveCrosshairsEnsureOn()
{
    Logger::trace("Stub: signaling GLIDING_CURSOR_HOTKEY_SHARED_EVENT (EnsureOn)");
    HANDLE h = OpenEventW(EVENT_MODIFY_STATE, FALSE, CommonSharedConstants::GLIDING_CURSOR_HOTKEY_SHARED_EVENT);
    if (h)
    {
        SetEvent(h);
        CloseHandle(h);
    }
}

void InclusiveCrosshairsEnsureOff()
{
    Logger::trace("Stub: signaling SHOW_MOUSE_CROSSHAIRS_SHARED_EVENT (EnsureOff via switch)");
    HANDLE h = OpenEventW(EVENT_MODIFY_STATE, FALSE, CommonSharedConstants::SHOW_MOUSE_CROSSHAIRS_SHARED_EVENT);
    if (h)
    {
        SetEvent(h);
        CloseHandle(h);
    }
}

void InclusiveCrosshairsSetExternalControl(bool /*enabled*/)
{
    // No-op in stub
}

int InclusiveCrosshairsMain(HINSTANCE /*hInstance*/, InclusiveCrosshairsSettings& /*settings*/)
{
    // No window/message loop in stub
    Logger::trace("Stub: InclusiveCrosshairsMain no-op");
    return 0;
}
