#pragma once

#include <TraceLoggingProvider.h>

class Trace
{
public:
    // Log if the user has Magnifier enabled or disabled
    static void EnableMagnifier(const bool enabled) noexcept;

    // Log that the user activated the module
    static void ActivateMagnifier() noexcept;
};