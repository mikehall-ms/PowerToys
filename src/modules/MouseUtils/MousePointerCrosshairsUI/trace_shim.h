#pragma once
#include <common/Telemetry/TraceBase.h>

struct Trace
{
    static void EnableMousePointerCrosshairs(const bool /*enabled*/) noexcept
    {
        // No-op in UI process; module DLL logs this. Kept to satisfy linker.
    }

    static void StartDrawingCrosshairs() noexcept
    {
        // No-op in UI process; module DLL logs this. Kept to satisfy linker.
    }
};
