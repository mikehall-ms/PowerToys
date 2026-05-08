#include "pch.h"
#include "RoutingDecider.h"

#include <algorithm>
#include <cwctype>

#include <common/logger/logger.h>

namespace PresentationMode
{
    namespace
    {
        std::wstring ToLower(std::wstring value)
        {
            std::transform(value.begin(), value.end(), value.begin(), [](wchar_t c) {
                return static_cast<wchar_t>(std::towlower(c));
            });
            return value;
        }

        std::wstring TrimCopy(const std::wstring& value)
        {
            const auto first = std::find_if_not(value.begin(), value.end(), [](wchar_t c) { return std::iswspace(c); });
            const auto last = std::find_if_not(value.rbegin(), value.rend(), [](wchar_t c) { return std::iswspace(c); }).base();
            if (first >= last)
            {
                return {};
            }
            return std::wstring(first, last);
        }

        bool IsShellWindow(HWND hwnd)
        {
            if (hwnd == GetShellWindow() || hwnd == GetDesktopWindow())
            {
                return true;
            }

            wchar_t className[256] = {};
            if (GetClassNameW(hwnd, className, ARRAYSIZE(className)) > 0)
            {
                static constexpr const wchar_t* kShellClasses[] = {
                    L"Progman", L"WorkerW", L"Shell_TrayWnd", L"Shell_SecondaryTrayWnd", L"DV2ControlHost"
                };
                for (const wchar_t* shell : kShellClasses)
                {
                    if (wcscmp(className, shell) == 0)
                    {
                        return true;
                    }
                }
            }
            return false;
        }
    }

    void RoutingDecider::UpdateConfig(const RoutingConfig& config)
    {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        m_presentationDeviceName = config.presentationMonitorDeviceName;
        m_mode = config.mode;
        m_excludedApps = ParseExcludedApps(config.excludedAppsRaw);
    }

    HMONITOR RoutingDecider::GetPresentationMonitor() const
    {
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        if (m_presentationDeviceName.empty())
        {
            return nullptr;
        }
        return FindMonitorByDeviceName(m_presentationDeviceName);
    }

    RoutingDecider::Decision RoutingDecider::Decide(HWND hwnd) const
    {
        Decision result;

        if (!hwnd || !IsRoutableWindow(hwnd))
        {
            return result;
        }

        std::wstring processName = GetWindowProcessName(hwnd);

        std::shared_lock<std::shared_mutex> lock(m_mutex);

        if (m_presentationDeviceName.empty())
        {
            return result;
        }

        if (!processName.empty() && m_excludedApps.contains(ToLower(processName)))
        {
            return result;
        }

        HMONITOR presentationMonitor = FindMonitorByDeviceName(m_presentationDeviceName);
        if (!presentationMonitor)
        {
            return result;
        }

        HMONITOR currentMonitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);

        if (m_mode == RoutingMode::ToPresentation)
        {
            if (currentMonitor != presentationMonitor)
            {
                result.shouldRoute = true;
                result.targetMonitor = presentationMonitor;
            }
        }
        else // AwayFromPresentation
        {
            if (currentMonitor == presentationMonitor)
            {
                HMONITOR alternate = PickAlternateMonitor(presentationMonitor);
                if (alternate)
                {
                    result.shouldRoute = true;
                    result.targetMonitor = alternate;
                }
            }
        }

        return result;
    }

    bool RoutingDecider::IsRoutableWindow(HWND hwnd)
    {
        if (!IsWindow(hwnd) || !IsWindowVisible(hwnd))
        {
            return false;
        }

        // Only consider top-level windows (no owner).
        if (GetWindow(hwnd, GW_OWNER) != nullptr)
        {
            return false;
        }

        const LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
        if (exStyle & WS_EX_TOOLWINDOW)
        {
            return false;
        }

        const LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
        if (style & WS_CHILD)
        {
            return false;
        }

        // Skip cloaked / hidden-by-shell windows. Best-effort: DwmGetWindowAttribute
        // would be ideal but isn't required here because we react to UNCLOAKED events too.

        if (IsShellWindow(hwnd))
        {
            return false;
        }

        // Filter out windows with no title - usually transient surfaces.
        if (GetWindowTextLengthW(hwnd) == 0)
        {
            return false;
        }

        return true;
    }

    std::wstring RoutingDecider::GetWindowProcessName(HWND hwnd)
    {
        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        if (pid == 0)
        {
            return {};
        }

        HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (!hProcess)
        {
            return {};
        }

        wchar_t buffer[MAX_PATH] = {};
        DWORD size = ARRAYSIZE(buffer);
        std::wstring result;
        if (QueryFullProcessImageNameW(hProcess, 0, buffer, &size))
        {
            std::wstring path(buffer, size);
            const auto pos = path.find_last_of(L"\\/");
            result = (pos == std::wstring::npos) ? path : path.substr(pos + 1);
        }
        CloseHandle(hProcess);
        return result;
    }

    std::unordered_set<std::wstring> RoutingDecider::ParseExcludedApps(const std::wstring& raw)
    {
        std::unordered_set<std::wstring> result;
        std::wstring current;
        current.reserve(64);

        auto flush = [&]() {
            std::wstring trimmed = TrimCopy(current);
            if (!trimmed.empty())
            {
                result.insert(ToLower(std::move(trimmed)));
            }
            current.clear();
        };

        for (wchar_t c : raw)
        {
            if (c == L'\n' || c == L'\r' || c == L';' || c == L',')
            {
                flush();
            }
            else
            {
                current.push_back(c);
            }
        }
        flush();

        return result;
    }

    HMONITOR RoutingDecider::FindMonitorByDeviceName(const std::wstring& deviceName)
    {
        if (deviceName.empty())
        {
            return nullptr;
        }

        struct Context
        {
            const std::wstring* name;
            HMONITOR found;
        } ctx{ &deviceName, nullptr };

        EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR monitor, HDC, LPRECT, LPARAM lparam) -> BOOL {
            auto* ctx = reinterpret_cast<Context*>(lparam);
            MONITORINFOEXW info{};
            info.cbSize = sizeof(info);
            if (GetMonitorInfoW(monitor, &info))
            {
                if (_wcsicmp(info.szDevice, ctx->name->c_str()) == 0)
                {
                    ctx->found = monitor;
                    return FALSE;
                }
            }
            return TRUE;
        }, reinterpret_cast<LPARAM>(&ctx));

        return ctx.found;
    }

    HMONITOR RoutingDecider::PickAlternateMonitor(HMONITOR avoid)
    {
        struct Context
        {
            HMONITOR avoid;
            HMONITOR primary;
            HMONITOR firstOther;
        } ctx{ avoid, nullptr, nullptr };

        EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR monitor, HDC, LPRECT, LPARAM lparam) -> BOOL {
            auto* ctx = reinterpret_cast<Context*>(lparam);
            if (monitor == ctx->avoid)
            {
                return TRUE;
            }

            MONITORINFO info{};
            info.cbSize = sizeof(info);
            if (GetMonitorInfoW(monitor, &info))
            {
                if (info.dwFlags & MONITORINFOF_PRIMARY)
                {
                    ctx->primary = monitor;
                }
            }

            if (!ctx->firstOther)
            {
                ctx->firstOther = monitor;
            }
            return TRUE;
        }, reinterpret_cast<LPARAM>(&ctx));

        return ctx.primary ? ctx.primary : ctx.firstOther;
    }
}
