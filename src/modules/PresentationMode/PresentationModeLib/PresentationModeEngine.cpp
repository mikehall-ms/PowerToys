#include "pch.h"
#include "PresentationModeEngine.h"

#include <common/logger/logger.h>

#include "WindowPlacer.h"

namespace PresentationMode
{
    PresentationModeEngine::PresentationModeEngine()
        : m_listener(std::make_unique<WindowEventListener>())
    {
    }

    PresentationModeEngine::~PresentationModeEngine()
    {
        Stop();
    }

    void PresentationModeEngine::UpdateConfig(const RoutingConfig& config)
    {
        m_currentMode = config.mode;
        m_currentDeviceName = config.presentationMonitorDeviceName;
        m_currentExcluded = config.excludedAppsRaw;
        m_decider.UpdateConfig(config);

        if (m_running.load())
        {
            // Re-route existing windows so a config change takes effect immediately.
            RouteExistingWindows();
        }
    }

    void PresentationModeEngine::Start()
    {
        if (m_running.exchange(true))
        {
            return;
        }

        Logger::info(L"PresentationMode: engine starting");

        m_listener->Start([this](HWND hwnd) { OnWindowEvent(hwnd); });

        // Apply current config to existing windows on startup.
        RouteExistingWindows();
    }

    void PresentationModeEngine::Stop()
    {
        if (!m_running.exchange(false))
        {
            return;
        }

        Logger::info(L"PresentationMode: engine stopping");
        if (m_listener)
        {
            m_listener->Stop();
        }
    }

    RoutingMode PresentationModeEngine::ToggleRoutingMode()
    {
        m_currentMode = (m_currentMode == RoutingMode::AwayFromPresentation)
            ? RoutingMode::ToPresentation
            : RoutingMode::AwayFromPresentation;

        RoutingConfig cfg;
        cfg.presentationMonitorDeviceName = m_currentDeviceName;
        cfg.mode = m_currentMode;
        cfg.excludedAppsRaw = m_currentExcluded;
        m_decider.UpdateConfig(cfg);

        if (m_running.load())
        {
            RouteExistingWindows();
        }

        return m_currentMode;
    }

    void PresentationModeEngine::OnWindowEvent(HWND hwnd)
    {
        if (!m_running.load())
        {
            return;
        }

        const auto decision = m_decider.Decide(hwnd);
        if (decision.shouldRoute && decision.targetMonitor)
        {
            WindowPlacer::MoveWindowToMonitor(hwnd, decision.targetMonitor);
        }
    }

    void PresentationModeEngine::RouteExistingWindows()
    {
        if (!m_running.load())
        {
            return;
        }

        struct Context
        {
            PresentationModeEngine* engine;
        } ctx{ this };

        EnumWindows([](HWND hwnd, LPARAM lparam) -> BOOL {
            auto* ctx = reinterpret_cast<Context*>(lparam);
            ctx->engine->OnWindowEvent(hwnd);
            return TRUE;
        }, reinterpret_cast<LPARAM>(&ctx));
    }
}
