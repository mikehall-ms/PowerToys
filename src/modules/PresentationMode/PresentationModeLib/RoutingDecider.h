#pragma once

#include <optional>
#include <shared_mutex>
#include <string>
#include <unordered_set>

namespace PresentationMode
{
    enum class RoutingMode : int
    {
        AwayFromPresentation = 0,
        ToPresentation = 1,
    };

    struct RoutingConfig
    {
        std::wstring presentationMonitorDeviceName;
        RoutingMode mode = RoutingMode::AwayFromPresentation;
        std::wstring excludedAppsRaw;
    };

    // Decides whether a newly-shown window should be moved, and to which monitor.
    class RoutingDecider
    {
    public:
        struct Decision
        {
            bool shouldRoute = false;
            HMONITOR targetMonitor = nullptr;
        };

        void UpdateConfig(const RoutingConfig& config);

        // Returns the decision for the given top-level window.
        Decision Decide(HWND hwnd) const;

        // Returns the configured presentation monitor handle, or null if none/unresolved.
        HMONITOR GetPresentationMonitor() const;

    private:
        static bool IsRoutableWindow(HWND hwnd);
        static std::wstring GetWindowProcessName(HWND hwnd);
        static std::unordered_set<std::wstring> ParseExcludedApps(const std::wstring& raw);
        static HMONITOR FindMonitorByDeviceName(const std::wstring& deviceName);
        static HMONITOR PickAlternateMonitor(HMONITOR avoid);

        mutable std::shared_mutex m_mutex;
        std::wstring m_presentationDeviceName;
        RoutingMode m_mode = RoutingMode::AwayFromPresentation;
        std::unordered_set<std::wstring> m_excludedApps;
    };
}
