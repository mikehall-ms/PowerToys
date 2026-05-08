#include "pch.h"
#include <interface/powertoy_module_interface.h>
#include "trace.h"
#include <common/logger/logger.h>
#include <common/SettingsAPI/settings_objects.h>
#include <common/SettingsAPI/settings_helpers.h>
#include <common/utils/logger_helper.h>

#include <atomic>
#include <memory>
#include <optional>

#include <PresentationModeEngine.h>

extern "C" IMAGE_DOS_HEADER __ImageBase;

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
        Trace::RegisterProvider();
        break;
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
        break;
    case DLL_PROCESS_DETACH:
        Trace::UnregisterProvider();
        break;
    }
    return TRUE;
}

const static wchar_t* MODULE_NAME = L"PresentationMode";
const static wchar_t* MODULE_DESC = L"Route newly-opened windows to or away from a designated presentation monitor.";

namespace
{
    constexpr int kHotkeyId = 0;

    constexpr PowertoyModuleIface::HotkeyEx GetDefaultHotkey() noexcept
    {
        return { static_cast<WORD>(MOD_WIN | MOD_SHIFT), static_cast<WORD>(0x50), kHotkeyId };
    }
}

class PresentationModeModuleInterface : public PowertoyModuleIface
{
private:
    std::atomic<bool> m_enabled{ false };

    int m_routingMode = 0;
    std::wstring m_presentationMonitor;
    std::wstring m_excludedApps;
    PowertoyModuleIface::HotkeyEx m_hotkey{ GetDefaultHotkey() };

    std::unique_ptr<PresentationMode::PresentationModeEngine> m_engine;

    PresentationMode::RoutingConfig BuildConfig() const
    {
        PresentationMode::RoutingConfig cfg;
        cfg.presentationMonitorDeviceName = m_presentationMonitor;
        cfg.mode = (m_routingMode == 1)
            ? PresentationMode::RoutingMode::ToPresentation
            : PresentationMode::RoutingMode::AwayFromPresentation;
        cfg.excludedAppsRaw = m_excludedApps;
        return cfg;
    }

    void LoadSettingsFromDisk()
    {
        try
        {
            auto values = PowerToysSettings::PowerToyValues::load_from_settings_file(get_key());

            auto routingMode = values.get_int_value(L"routing_mode");
            if (routingMode.has_value())
            {
                m_routingMode = routingMode.value();
            }

            auto presentationMonitor = values.get_string_value(L"presentation_monitor");
            if (presentationMonitor.has_value())
            {
                m_presentationMonitor = presentationMonitor.value();
            }

            auto excludedApps = values.get_string_value(L"excluded_apps");
            if (excludedApps.has_value())
            {
                m_excludedApps = excludedApps.value();
            }
        }
        catch (const std::exception&)
        {
            Logger::warn(L"PresentationMode: failed to load settings file; using defaults.");
        }
    }

public:
    PresentationModeModuleInterface()
    {
        LoggerHelpers::init_logger(L"PresentationMode", L"ModuleInterface", LogSettings::presentationModeLoggerName);
    }

    virtual const wchar_t* get_key() override
    {
        return L"PresentationMode";
    }

    virtual void destroy() override
    {
        disable();
        delete this;
    }

    virtual const wchar_t* get_name() override
    {
        return MODULE_NAME;
    }

    virtual powertoys_gpo::gpo_rule_configured_t gpo_policy_enabled_configuration() override
    {
        return powertoys_gpo::getConfiguredPresentationModeEnabledValue();
    }

    virtual bool get_config(wchar_t* buffer, int* buffer_size) override
    {
        HINSTANCE hinstance = reinterpret_cast<HINSTANCE>(&__ImageBase);

        PowerToysSettings::Settings settings(hinstance, get_name());
        settings.set_description(MODULE_DESC);

        return settings.serialize_to_buffer(buffer, buffer_size);
    }

    virtual void set_config(const wchar_t* config) override
    {
        try
        {
            auto values = PowerToysSettings::PowerToyValues::from_json_string(config, get_key());

            auto routingMode = values.get_int_value(L"routing_mode");
            if (routingMode.has_value())
            {
                m_routingMode = routingMode.value();
            }

            auto presentationMonitor = values.get_string_value(L"presentation_monitor");
            if (presentationMonitor.has_value())
            {
                m_presentationMonitor = presentationMonitor.value();
            }

            auto excludedApps = values.get_string_value(L"excluded_apps");
            if (excludedApps.has_value())
            {
                m_excludedApps = excludedApps.value();
            }

            values.save_to_settings_file();

            if (m_engine)
            {
                m_engine->UpdateConfig(BuildConfig());
            }
        }
        catch (const std::exception&)
        {
            Logger::error("[PresentationMode] set_config: Failed to parse or apply config.");
        }
    }

    virtual void enable() override
    {
        Logger::info(L"Enabling PresentationMode module...");

        LoadSettingsFromDisk();

        if (!m_engine)
        {
            m_engine = std::make_unique<PresentationMode::PresentationModeEngine>();
        }

        m_engine->UpdateConfig(BuildConfig());
        m_engine->Start();

        m_enabled.store(true);
        Trace::Enable(true);
    }

    virtual void disable() override
    {
        Logger::info("PresentationMode disabling");
        m_enabled.store(false);

        if (m_engine)
        {
            m_engine->Stop();
        }

        Trace::Enable(false);
    }

    virtual bool is_enabled() override
    {
        return m_enabled.load();
    }

    virtual bool is_enabled_by_default() const override
    {
        return false;
    }

    virtual std::optional<HotkeyEx> GetHotkeyEx() override
    {
        return m_hotkey;
    }

    virtual void OnHotkeyEx() override
    {
        Logger::trace(L"PresentationMode: activation hotkey pressed");

        if (!m_enabled.load() || !m_engine)
        {
            return;
        }

        const auto newMode = m_engine->ToggleRoutingMode();
        m_routingMode = (newMode == PresentationMode::RoutingMode::ToPresentation) ? 1 : 0;
    }

    virtual void call_custom_action(const wchar_t* action) override
    {
        if (!action)
        {
            return;
        }

        std::wstring name(action);
        if (name == L"Toggle" || name == L"toggle")
        {
            OnHotkeyEx();
        }
    }
};

extern "C" __declspec(dllexport) PowertoyModuleIface* __cdecl powertoy_create()
{
    return new PresentationModeModuleInterface();
}