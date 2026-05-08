#pragma once

namespace PresentationMode
{
    // Moves a window onto a target monitor, preserving its relative size and position.
    class WindowPlacer
    {
    public:
        // Returns true on success.
        static bool MoveWindowToMonitor(HWND hwnd, HMONITOR targetMonitor);
    };
}
