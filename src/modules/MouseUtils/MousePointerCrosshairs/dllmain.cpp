#include "pch.h"
#include <shellapi.h>
#include <interface/powertoy_module_interface.h>
#include <common/SettingsAPI/settings_objects.h>
#include <common/interop/shared_constants.h>
#include <common/utils/winapi_error.h>
#include "trace.h"
#include "InclusiveCrosshairs.h"
#include "common/utils/color.h"
#include <atomic>
#include <thread>
#include <chrono>
#include <memory>

extern void InclusiveCrosshairsRequestUpdatePosition();
extern void InclusiveCrosshairsEnsureOn();
extern void InclusiveCrosshairsEnsureOff();
extern void InclusiveCrosshairsSetExternalControl(bool enabled);

// Non-Localizable strings (JSON keys)
namespace
{
    const wchar_t JSON_KEY_PROPERTIES[] = L"properties";
    const wchar_t JSON_KEY_VALUE[] = L"value";
    const wchar_t JSON_KEY_ACTIVATION_SHORTCUT[] = L"activation_shortcut";
    const wchar_t JSON_KEY_GLIDING_ACTIVATION_SHORTCUT[] = L"gliding_cursor_activation_shortcut";
    const wchar_t JSON_KEY_CROSSHAIRS_COLOR[] = L"crosshairs_color";
    const wchar_t JSON_KEY_CROSSHAIRS_OPACITY[] = L"crosshairs_opacity";
    const wchar_t JSON_KEY_CROSSHAIRS_RADIUS[] = L"crosshairs_radius";
    const wchar_t JSON_KEY_CROSSHAIRS_THICKNESS[] = L"crosshairs_thickness";
    const wchar_t JSON_KEY_CROSSHAIRS_BORDER_COLOR[] = L"crosshairs_border_color";
    const wchar_t JSON_KEY_CROSSHAIRS_BORDER_SIZE[] = L"crosshairs_border_size";
    const wchar_t JSON_KEY_CROSSHAIRS_AUTO_HIDE[] = L"crosshairs_auto_hide";
    const wchar_t JSON_KEY_CROSSHAIRS_IS_FIXED_LENGTH_ENABLED[] = L"crosshairs_is_fixed_length_enabled";
    const wchar_t JSON_KEY_CROSSHAIRS_FIXED_LENGTH[] = L"crosshairs_fixed_length";
    const wchar_t JSON_KEY_AUTO_ACTIVATE[] = L"auto_activate";
    const wchar_t JSON_KEY_GLIDE_TRAVEL_SPEED[] = L"gliding_travel_speed";
    const wchar_t JSON_KEY_GLIDE_DELAY_SPEED[] = L"gliding_delay_speed";
}

extern "C" IMAGE_DOS_HEADER __ImageBase;

HMODULE m_hModule;

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID /*lpReserved*/)
{
    m_hModule = hModule;
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

// The PowerToy name that will be shown in the settings.
const static wchar_t* MODULE_NAME = L"MousePointerCrosshairs";
// Add a description that will we shown in the module settings page.
const static wchar_t* MODULE_DESC = L"<no description>";

// Implement the PowerToy Module Interface and all the required methods.
class MousePointerCrosshairs : public PowertoyModuleIface
{
private:
    // The PowerToy state.
    bool m_enabled = false;

    // Additional hotkeys (legacy API) to support multiple shortcuts
    Hotkey m_activationHotkey{};    // Crosshairs toggle
    Hotkey m_glidingHotkey{};       // Gliding cursor state machine

    // Mouse Pointer Crosshairs specific settings
    InclusiveCrosshairsSettings m_inclusiveCrosshairsSettings;

    // IPC shared events (mirrors ColorPicker pattern)
    HANDLE m_hShowEvent = nullptr;              // SHOW_MOUSE_CROSSHAIRS_SHARED_EVENT
    HANDLE m_hGlidingEvent = nullptr;           // GLIDING_CURSOR_HOTKEY_SHARED_EVENT
    HANDLE m_hTerminateEvent = nullptr;         // TERMINATE_MOUSE_CROSSHAIRS_SHARED_EVENT
    HANDLE m_hTelemetryEvent = nullptr;         // MOUSE_CROSSHAIRS_SEND_SETTINGS_TELEMETRY_EVENT

    // Process handle for the UI exe
    HANDLE m_hProcess = nullptr;

    static const int MAX_WAIT_MILLISEC = 10000;

    bool is_process_running()
    {
        return m_hProcess && WaitForSingleObject(m_hProcess, 0) == WAIT_TIMEOUT;
    }

    void launch_process()
    {
        Logger::trace(L"Starting MousePointerCrosshairsUI process");
        unsigned long powertoys_pid = GetCurrentProcessId();

        std::wstring cmd = L"PowerToys.MousePointerCrosshairsUI.exe ";
        cmd += std::to_wstring(powertoys_pid);

        STARTUPINFOW si{};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};
        std::wstring mutableCmd = cmd; // CreateProcessW may modify the buffer
        BOOL ok = CreateProcessW(
            nullptr,
            mutableCmd.data(),
            nullptr,
            nullptr,
            FALSE,
            0,
            nullptr,
            nullptr,
            &si,
            &pi);
        if (ok)
        {
            Logger::trace("Successfully started the MousePointerCrosshairsUI process");
            CloseHandle(pi.hThread);
            m_hProcess = pi.hProcess;
        }
        else
        {
            Logger::error(L"MousePointerCrosshairsUI failed to start. {}", get_last_error_or_default(GetLastError()));
            m_hProcess = nullptr;
        }
    }

    // Load the settings file.
    void init_settings()
    {
        try
        {
            PowerToysSettings::PowerToyValues settings =
                PowerToysSettings::PowerToyValues::load_from_settings_file(get_key());

            parse_settings(settings);
        }
        catch (std::exception&)
        {
            Logger::error("Invalid json when trying to load the Mouse Pointer Crosshairs settings json from file.");
        }
    }

public:
    MousePointerCrosshairs()
    {
        LoggerHelpers::init_logger(MODULE_NAME, L"ModuleInterface", LogSettings::mousePointerCrosshairsLoggerName);
        init_settings();

        // Create shared events
        m_hShowEvent = CreateDefaultEvent(CommonSharedConstants::SHOW_MOUSE_CROSSHAIRS_SHARED_EVENT);
        m_hGlidingEvent = CreateDefaultEvent(CommonSharedConstants::GLIDING_CURSOR_HOTKEY_SHARED_EVENT);
        m_hTerminateEvent = CreateDefaultEvent(CommonSharedConstants::TERMINATE_MOUSE_CROSSHAIRS_SHARED_EVENT);
        m_hTelemetryEvent = CreateDefaultEvent(CommonSharedConstants::MOUSE_CROSSHAIRS_SEND_SETTINGS_TELEMETRY_EVENT);
    };

    virtual void destroy() override
    {
        Logger::trace("MousePointerCrosshairs::destroy()");
        delete this;
    }

    virtual const wchar_t* get_name() override
    {
        return MODULE_NAME;
    }

    virtual const wchar_t* get_key() override
    {
        return MODULE_NAME;
    }

    virtual powertoys_gpo::gpo_rule_configured_t gpo_policy_enabled_configuration() override
    {
        return powertoys_gpo::getConfiguredMousePointerCrosshairsEnabledValue();
    }

    virtual bool get_config(wchar_t* buffer, int* buffer_size) override
    {
        HINSTANCE hinstance = reinterpret_cast<HINSTANCE>(&__ImageBase);

        PowerToysSettings::Settings settings(hinstance, get_name());

        return settings.serialize_to_buffer(buffer, buffer_size);
    }

    virtual void call_custom_action(const wchar_t* /*action*/) override {}

    virtual void set_config(const wchar_t* config) override
    {
        try
        {
            PowerToysSettings::PowerToyValues values =
                PowerToysSettings::PowerToyValues::from_json_string(config, get_key());

            parse_settings(values);
            // No direct push to UI; UI watches settings.json and reloads
        }
        catch (std::exception&)
        {
            Logger::error("Invalid json when trying to parse Mouse Pointer Crosshairs settings json.");
        }
    }

    virtual void enable()
    {
        m_enabled = true;
        Trace::EnableMousePointerCrosshairs(true);
        ResetEvent(m_hShowEvent);
        ResetEvent(m_hGlidingEvent);
        ResetEvent(m_hTerminateEvent);
        ResetEvent(m_hTelemetryEvent);
        launch_process();
        // UI will read settings on startup and watch for changes
    }

    virtual void disable()
    {
        Logger::trace("MousePointerCrosshairs::disable()");
        if (m_enabled)
        {
            // Signal terminate and wait a bit
            if (m_hTerminateEvent)
            {
                SetEvent(m_hTerminateEvent);
            }
            if (m_hProcess)
            {
                WaitForSingleObject(m_hProcess, 1500);
                TerminateProcess(m_hProcess, 1);
                m_hProcess = nullptr;
            }
        }
        m_enabled = false;
        Trace::EnableMousePointerCrosshairs(false);
    }

    virtual bool is_enabled() override
    {
        return m_enabled;
    }

    virtual bool is_enabled_by_default() const override
    {
        return false;
    }

    virtual size_t get_hotkeys(Hotkey* buffer, size_t buffer_size) override
    {
        if (buffer && buffer_size >= 2)
        {
            buffer[0] = m_activationHotkey; // Crosshairs toggle
            buffer[1] = m_glidingHotkey;    // Gliding cursor toggle
        }
        return 2;
    }

    virtual bool on_hotkey(size_t hotkeyId) override
    {
        if (!m_enabled)
        {
            return false;
        }

        if (!is_process_running())
        {
            launch_process();
            // UI will initialize and load settings itself
        }

        if (hotkeyId == 0)
        {
            SetEvent(m_hShowEvent);
            return true;
        }
        if (hotkeyId == 1)
        {
            SetEvent(m_hGlidingEvent);
            return true;
        }
        return false;
    }

    virtual void send_settings_telemetry() override
    {
        if (m_hTelemetryEvent)
        {
            SetEvent(m_hTelemetryEvent);
        }
    }

private:
    // Load the settings file.
    void init_settings_silent()
    {
        try
        {
            PowerToysSettings::PowerToyValues settings =
                PowerToysSettings::PowerToyValues::load_from_settings_file(MousePointerCrosshairs::get_key());
            parse_settings(settings);
        }
        catch (std::exception&)
        {
            Logger::error("Invalid json when trying to load the Mouse Pointer Crosshairs settings json from file.");
        }
    }

    void parse_settings(PowerToysSettings::PowerToyValues& settings)
    {
        auto settingsObject = settings.get_raw_json();
        InclusiveCrosshairsSettings inclusiveCrosshairsSettings;
        if (settingsObject.GetView().Size())
        {
            try
            {
                auto jsonPropertiesObject = settingsObject.GetNamedObject(JSON_KEY_PROPERTIES).GetNamedObject(JSON_KEY_ACTIVATION_SHORTCUT);
                auto hotkey = PowerToysSettings::HotkeyObject::from_json(jsonPropertiesObject);

                m_activationHotkey.win = hotkey.win_pressed();
                m_activationHotkey.ctrl = hotkey.ctrl_pressed();
                m_activationHotkey.shift = hotkey.shift_pressed();
                m_activationHotkey.alt = hotkey.alt_pressed();
                m_activationHotkey.key = static_cast<unsigned char>(hotkey.get_code());
            }
            catch (...)
            {
                Logger::warn("Failed to initialize Mouse Pointer Crosshairs activation shortcut");
            }
            try
            {
                auto jsonPropertiesObject = settingsObject.GetNamedObject(JSON_KEY_PROPERTIES).GetNamedObject(JSON_KEY_GLIDING_ACTIVATION_SHORTCUT);
                auto hotkey = PowerToysSettings::HotkeyObject::from_json(jsonPropertiesObject);
                m_glidingHotkey.win = hotkey.win_pressed();
                m_glidingHotkey.ctrl = hotkey.ctrl_pressed();
                m_glidingHotkey.shift = hotkey.shift_pressed();
                m_glidingHotkey.alt = hotkey.alt_pressed();
                m_glidingHotkey.key = static_cast<unsigned char>(hotkey.get_code());
            }
            catch (...)
            {
                Logger::warn("Failed to initialize Gliding Cursor activation shortcut. Using default Win+Alt+.");
                m_glidingHotkey.win = true;
                m_glidingHotkey.alt = true;
                m_glidingHotkey.ctrl = false;
                m_glidingHotkey.shift = false;
                m_glidingHotkey.key = VK_OEM_PERIOD;
            }
            try
            {
                auto jsonPropertiesObject = settingsObject.GetNamedObject(JSON_KEY_PROPERTIES).GetNamedObject(JSON_KEY_CROSSHAIRS_OPACITY);
                int value = static_cast<uint8_t>(jsonPropertiesObject.GetNamedNumber(JSON_KEY_VALUE));
                if (value >= 0)
                {
                    inclusiveCrosshairsSettings.crosshairsOpacity = value;
                }
                else
                {
                    throw std::runtime_error("Invalid Opacity value");
                }
            }
            catch (...)
            {
                Logger::warn("Failed to initialize Opacity from settings. Will use default value");
            }
            try
            {
                auto jsonPropertiesObject = settingsObject.GetNamedObject(JSON_KEY_PROPERTIES).GetNamedObject(JSON_KEY_CROSSHAIRS_COLOR);
                auto crosshairsColor = (std::wstring)jsonPropertiesObject.GetNamedString(JSON_KEY_VALUE);
                uint8_t r, g, b;
                if (!checkValidRGB(crosshairsColor, &r, &g, &b))
                {
                    Logger::error("Crosshairs color RGB value is invalid. Will use default value");
                }
                else
                {
                    inclusiveCrosshairsSettings.crosshairsColor = winrt::Windows::UI::ColorHelper::FromArgb(255, r, g, b);
                }
            }
            catch (...)
            {
                Logger::warn("Failed to initialize crosshairs color from settings. Will use default value");
            }
            try
            {
                auto jsonPropertiesObject = settingsObject.GetNamedObject(JSON_KEY_PROPERTIES).GetNamedObject(JSON_KEY_CROSSHAIRS_RADIUS);
                int value = static_cast<int>(jsonPropertiesObject.GetNamedNumber(JSON_KEY_VALUE));
                if (value >= 0)
                {
                    inclusiveCrosshairsSettings.crosshairsRadius = value;
                }
                else
                {
                    throw std::runtime_error("Invalid Radius value");
                }
            }
            catch (...)
            {
                Logger::warn("Failed to initialize Radius from settings. Will use default value");
            }
            try
            {
                auto jsonPropertiesObject = settingsObject.GetNamedObject(JSON_KEY_PROPERTIES).GetNamedObject(JSON_KEY_CROSSHAIRS_THICKNESS);
                int value = static_cast<int>(jsonPropertiesObject.GetNamedNumber(JSON_KEY_VALUE));
                if (value >= 0)
                {
                    inclusiveCrosshairsSettings.crosshairsThickness = value;
                }
                else
                {
                    throw std::runtime_error("Invalid Thickness value");
                }
            }
            catch (...)
            {
                Logger::warn("Failed to initialize Thickness from settings. Will use default value");
            }
            try
            {
                auto jsonPropertiesObject = settingsObject.GetNamedObject(JSON_KEY_PROPERTIES).GetNamedObject(JSON_KEY_CROSSHAIRS_BORDER_COLOR);
                auto crosshairsBorderColor = (std::wstring)jsonPropertiesObject.GetNamedString(JSON_KEY_VALUE);
                uint8_t r, g, b;
                if (!checkValidRGB(crosshairsBorderColor, &r, &g, &b))
                {
                    Logger::error("Crosshairs border color RGB value is invalid. Will use default value");
                }
                else
                {
                    inclusiveCrosshairsSettings.crosshairsBorderColor = winrt::Windows::UI::ColorHelper::FromArgb(255, r, g, b);
                }
            }
            catch (...)
            {
                Logger::warn("Failed to initialize crosshairs border color from settings. Will use default value");
            }
            try
            {
                auto jsonPropertiesObject = settingsObject.GetNamedObject(JSON_KEY_PROPERTIES).GetNamedObject(JSON_KEY_CROSSHAIRS_BORDER_SIZE);
                int value = static_cast<int>(jsonPropertiesObject.GetNamedNumber(JSON_KEY_VALUE));
                if (value >= 0)
                {
                    inclusiveCrosshairsSettings.crosshairsBorderSize = value;
                }
                else
                {
                    throw std::runtime_error("Invalid Border Color value");
                }
            }
            catch (...)
            {
                Logger::warn("Failed to initialize border color from settings. Will use default value");
            }
            try
            {
                auto jsonPropertiesObject = settingsObject.GetNamedObject(JSON_KEY_PROPERTIES).GetNamedObject(JSON_KEY_CROSSHAIRS_AUTO_HIDE);
                inclusiveCrosshairsSettings.crosshairsAutoHide = jsonPropertiesObject.GetNamedBoolean(JSON_KEY_VALUE);
            }
            catch (...)
            {
                Logger::warn("Failed to initialize auto hide from settings. Will use default value");
            }
            try
            {
                auto jsonPropertiesObject = settingsObject.GetNamedObject(JSON_KEY_PROPERTIES).GetNamedObject(JSON_KEY_CROSSHAIRS_IS_FIXED_LENGTH_ENABLED);
                bool value = jsonPropertiesObject.GetNamedBoolean(JSON_KEY_VALUE);
                inclusiveCrosshairsSettings.crosshairsIsFixedLengthEnabled = value;
            }
            catch (...)
            {
                Logger::warn("Failed to initialize fixed length enabled from settings. Will use default value");
            }
            try
            {
                auto jsonPropertiesObject = settingsObject.GetNamedObject(JSON_KEY_PROPERTIES).GetNamedObject(JSON_KEY_CROSSHAIRS_FIXED_LENGTH);
                int value = static_cast<int>(jsonPropertiesObject.GetNamedNumber(JSON_KEY_VALUE));
                if (value >= 0)
                {
                    inclusiveCrosshairsSettings.crosshairsFixedLength = value;
                }
                else
                {
                    throw std::runtime_error("Invalid Fixed Length value");
                }
            }
            catch (...)
            {
                Logger::warn("Failed to initialize fixed length from settings. Will use default value");
            }
            try
            {
                auto jsonPropertiesObject = settingsObject.GetNamedObject(JSON_KEY_PROPERTIES).GetNamedObject(JSON_KEY_AUTO_ACTIVATE);
                inclusiveCrosshairsSettings.autoActivate = jsonPropertiesObject.GetNamedBoolean(JSON_KEY_VALUE);
            }
            catch (...)
            {
                Logger::warn("Failed to initialize auto activate from settings. Will use default value");
            }
        }
        else
        {
            Logger::info("Mouse Pointer Crosshairs settings are empty");
        }
        
        if (m_activationHotkey.key == 0)
        {
            m_activationHotkey.win = true;
            m_activationHotkey.alt = true;
            m_activationHotkey.ctrl = false;
            m_activationHotkey.shift = false;
            m_activationHotkey.key = 'P';
        }
        if (m_glidingHotkey.key == 0)
        {
            m_glidingHotkey.win = true;
            m_glidingHotkey.alt = true;
            m_glidingHotkey.ctrl = false;
            m_glidingHotkey.shift = false;
            m_glidingHotkey.key = VK_OEM_PERIOD;
        }
        m_inclusiveCrosshairsSettings = inclusiveCrosshairsSettings;
    }
};

extern "C" __declspec(dllexport) PowertoyModuleIface* __cdecl powertoy_create()
{
    return new MousePointerCrosshairs();
}