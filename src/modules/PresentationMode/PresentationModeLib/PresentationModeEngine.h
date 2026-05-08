#pragma once

#include <atomic>
#include <memory>
#include <unordered_set>

#include "RoutingDecider.h"
#include "WindowEventListener.h"

namespace PresentationMode
{
    // Orchestrates the engine: listens for new windows, decides whether to route them,
    // and moves them onto the chosen monitor.
    class PresentationModeEngine
    {
    public:
        PresentationModeEngine();
        ~PresentationModeEngine();

        PresentationModeEngine(const PresentationModeEngine&) = delete;
        PresentationModeEngine& operator=(const PresentationModeEngine&) = delete;

        void UpdateConfig(const RoutingConfig& config);

        void Start();
        void Stop();

        bool IsRunning() const noexcept { return m_running.load(); }

        // Toggle helper used by hotkey / custom action: swaps the routing direction.
        // Returns the new routing mode.
        RoutingMode ToggleRoutingMode();

        // Re-evaluates every visible top-level window and routes those that match.
        void RouteExistingWindows();

    private:
        void OnWindowEvent(HWND hwnd);

        std::atomic<bool> m_running{ false };
        RoutingDecider m_decider;
        std::unique_ptr<WindowEventListener> m_listener;
        RoutingMode m_currentMode{ RoutingMode::AwayFromPresentation };
        std::wstring m_currentDeviceName;
        std::wstring m_currentExcluded;
    };
}
