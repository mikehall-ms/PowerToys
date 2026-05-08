#include "pch.h"
#include "WindowPlacer.h"

#include <algorithm>

#include <common/logger/logger.h>

namespace PresentationMode
{
    namespace
    {
        bool GetWorkArea(HMONITOR monitor, RECT& workArea)
        {
            MONITORINFO info{};
            info.cbSize = sizeof(info);
            if (!GetMonitorInfoW(monitor, &info))
            {
                return false;
            }
            workArea = info.rcWork;
            return true;
        }
    }

    bool WindowPlacer::MoveWindowToMonitor(HWND hwnd, HMONITOR targetMonitor)
    {
        if (!hwnd || !IsWindow(hwnd) || !targetMonitor)
        {
            return false;
        }

        HMONITOR sourceMonitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
        if (sourceMonitor == targetMonitor)
        {
            return true;
        }

        RECT sourceWork{}, targetWork{};
        if (!GetWorkArea(sourceMonitor, sourceWork) || !GetWorkArea(targetMonitor, targetWork))
        {
            return false;
        }

        WINDOWPLACEMENT placement{};
        placement.length = sizeof(placement);
        if (!GetWindowPlacement(hwnd, &placement))
        {
            return false;
        }

        const bool wasMaximized = (placement.showCmd == SW_SHOWMAXIMIZED);

        // If maximized, restore first so we can move it, then re-maximize on the target monitor.
        if (wasMaximized)
        {
            ShowWindow(hwnd, SW_RESTORE);
        }

        RECT windowRect{};
        if (!GetWindowRect(hwnd, &windowRect))
        {
            return false;
        }

        const LONG sourceWidth = std::max<LONG>(1, sourceWork.right - sourceWork.left);
        const LONG sourceHeight = std::max<LONG>(1, sourceWork.bottom - sourceWork.top);
        const LONG targetWidth = targetWork.right - targetWork.left;
        const LONG targetHeight = targetWork.bottom - targetWork.top;

        // Translate position relative to the source work area.
        const LONG relX = windowRect.left - sourceWork.left;
        const LONG relY = windowRect.top - sourceWork.top;
        const LONG width = windowRect.right - windowRect.left;
        const LONG height = windowRect.bottom - windowRect.top;

        // Scale by the ratio of work-area sizes to handle differing resolutions/DPIs reasonably.
        const double xRatio = static_cast<double>(targetWidth) / static_cast<double>(sourceWidth);
        const double yRatio = static_cast<double>(targetHeight) / static_cast<double>(sourceHeight);

        LONG newWidth = std::min<LONG>(targetWidth, static_cast<LONG>(width * xRatio));
        LONG newHeight = std::min<LONG>(targetHeight, static_cast<LONG>(height * yRatio));
        LONG newX = targetWork.left + static_cast<LONG>(relX * xRatio);
        LONG newY = targetWork.top + static_cast<LONG>(relY * yRatio);

        // Clamp into the target work area.
        newX = std::clamp<LONG>(newX, targetWork.left, targetWork.right - newWidth);
        newY = std::clamp<LONG>(newY, targetWork.top, targetWork.bottom - newHeight);

        const UINT flags = SWP_NOZORDER | SWP_NOACTIVATE | SWP_ASYNCWINDOWPOS;
        if (!SetWindowPos(hwnd, nullptr, newX, newY, newWidth, newHeight, flags))
        {
            Logger::warn(L"PresentationMode: SetWindowPos failed, last error {}", GetLastError());
            return false;
        }

        if (wasMaximized)
        {
            ShowWindow(hwnd, SW_MAXIMIZE);
        }

        return true;
    }
}
