// Copyright (c) Microsoft Corporation
// The Microsoft Corporation licenses this file to you under the MIT license.
// See the LICENSE file in the project root for more information.

#include "pch.h"
#include "../../../interface/powertoy_module_interface.h"
#include "../../../common/SettingsAPI/settings_objects.h"
#include "trace.h"
#include "../../../common/logger/logger.h"
#include "../../../common/utils/logger_helper.h"
#include "../../../common/utils/game_mode.h"
#include "../../../common/interop/shared_constants.h"
#include <atomic>
#include <thread>
#include <windows.h>
#include <wtsapi32.h>
#include "resource.h"
#include "RightClickLockCore.h"

#pragma comment(lib, "wtsapi32.lib")

extern "C" IMAGE_DOS_HEADER __ImageBase;

BOOL APIENTRY DllMain(HMODULE /*hModule*/, DWORD ul_reason_for_call, LPVOID /*lpReserved*/)
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

// Non-Localizable strings
namespace
{
	const wchar_t JSON_KEY_PROPERTIES[] = L"properties";
	const wchar_t JSON_KEY_VALUE[] = L"value";
	const wchar_t JSON_KEY_ACTIVATION_SHORTCUT[] = L"activation_shortcut";
	const wchar_t JSON_KEY_PANIC_SHORTCUT[] = L"panic_shortcut";
	const wchar_t JSON_KEY_AUTO_ACTIVATE_IN_GAME_MODE[] = L"auto_activate_in_game_mode";
	const wchar_t JSON_KEY_HOLD_DELAY_MS[] = L"hold_delay_ms";
	const wchar_t JSON_KEY_MOVE_CANCEL_PIXELS[] = L"move_cancel_pixels";
}

// The PowerToy name that will be shown in the settings.
const static wchar_t* MODULE_NAME = L"RightClickLock";
// Add a description that will be shown in the module settings page.
const static wchar_t* MODULE_DESC = L"Hold the right mouse button without keeping it pressed";

constexpr int RIGHT_CLICK_LOCK_DEFAULT_HOLD_DELAY_MS = 300;
constexpr int RIGHT_CLICK_LOCK_MIN_HOLD_DELAY_MS = 100;
constexpr int RIGHT_CLICK_LOCK_MAX_HOLD_DELAY_MS = 2000;
constexpr int RIGHT_CLICK_LOCK_DEFAULT_MOVE_CANCEL_PIXELS = 10;
constexpr int RIGHT_CLICK_LOCK_MIN_MOVE_CANCEL_PIXELS = 0;
constexpr int RIGHT_CLICK_LOCK_MAX_MOVE_CANCEL_PIXELS = 200;

// Hotkey ids returned by get_hotkeys, in declaration order.
enum RightClickLockHotkeyId : size_t
{
	HotkeyIdToggle = 0,
	HotkeyIdPanic = 1,
	HotkeyCount = 2,
};

// Forward declaration
class RightClickLock;

// Global instance pointer for the mouse hook (WH_MOUSE_LL callback is a static function).
static RightClickLock* g_rightClickLockInstance = nullptr;

// Implement the PowerToy Module Interface and all the required methods.
class RightClickLock : public PowertoyModuleIface
{
private:
	// The PowerToy state.
	bool m_enabled = false;
	bool m_autoActivateInGameMode = false;
	int m_holdDelayMs = RIGHT_CLICK_LOCK_DEFAULT_HOLD_DELAY_MS;
	int m_moveCancelPixels = RIGHT_CLICK_LOCK_DEFAULT_MOVE_CANCEL_PIXELS;

	// Whether the user has toggled the lock feature on via the activation hotkey.
	std::atomic<bool> m_lockActive{ false };

	// Mouse hook
	HHOOK m_mouseHook = nullptr;
	std::atomic<bool> m_hookActive{ false };

	// Core state machine
	RightClickLockCore m_core;

	// Hotkeys
	Hotkey m_activationHotkey{};
	Hotkey m_panicHotkey{};

	// Message-only window living on the hook thread. Hosts the arming timer and receives
	// WM_WTSSESSION_CHANGE so we can force-release on lock/logoff.
	HWND m_hookWindow = nullptr;

	// Event-driven trigger support (for CmdPal/automation)
	HANDLE m_triggerEventHandle = nullptr;
	HANDLE m_terminateEventHandle = nullptr;
	std::thread m_eventThread;
	std::atomic_bool m_listening{ false };

public:
	RightClickLock()
	{
		LoggerHelpers::init_logger(MODULE_NAME, L"ModuleInterface", LogSettings::rightClickLockLoggerName);
		init_settings();
		g_rightClickLockInstance = this;
	}

	virtual void destroy() override
	{
		// Ensure the synthetic hold is released and hooks/threads are torn down before deletion.
		disable();
		m_core.EmergencyRelease();
		g_rightClickLockInstance = nullptr;
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
		return powertoys_gpo::getConfiguredRightClickLockEnabledValue();
	}

	virtual bool get_config(wchar_t* buffer, int* buffer_size) override
	{
		HINSTANCE hinstance = reinterpret_cast<HINSTANCE>(&__ImageBase);

		PowerToysSettings::Settings settings(hinstance, get_name());
		settings.set_description(IDS_RIGHTCLICKLOCK_NAME);
		settings.set_icon_key(L"pt-right-click-lock");

		auto activation_object = PowerToysSettings::HotkeyObject::from_settings(
			m_activationHotkey.win,
			m_activationHotkey.ctrl,
			m_activationHotkey.alt,
			m_activationHotkey.shift,
			m_activationHotkey.key);
		settings.add_hotkey(JSON_KEY_ACTIVATION_SHORTCUT, IDS_RIGHTCLICKLOCK_NAME, activation_object);

		auto panic_object = PowerToysSettings::HotkeyObject::from_settings(
			m_panicHotkey.win,
			m_panicHotkey.ctrl,
			m_panicHotkey.alt,
			m_panicHotkey.shift,
			m_panicHotkey.key);
		settings.add_hotkey(JSON_KEY_PANIC_SHORTCUT, IDS_RIGHTCLICKLOCK_NAME, panic_object);

		settings.add_bool_toggle(JSON_KEY_AUTO_ACTIVATE_IN_GAME_MODE, IDS_RIGHTCLICKLOCK_NAME, m_autoActivateInGameMode);
		settings.add_int_spinner(JSON_KEY_HOLD_DELAY_MS, IDS_RIGHTCLICKLOCK_NAME, m_holdDelayMs, RIGHT_CLICK_LOCK_MIN_HOLD_DELAY_MS, RIGHT_CLICK_LOCK_MAX_HOLD_DELAY_MS, 50);
		settings.add_int_spinner(JSON_KEY_MOVE_CANCEL_PIXELS, IDS_RIGHTCLICKLOCK_NAME, m_moveCancelPixels, RIGHT_CLICK_LOCK_MIN_MOVE_CANCEL_PIXELS, RIGHT_CLICK_LOCK_MAX_MOVE_CANCEL_PIXELS, 1);

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
		}
		catch (std::exception&)
		{
			Logger::error("Invalid json when trying to parse RightClickLock settings json.");
		}
	}

	virtual void enable() override
	{
		m_enabled = true;
		Trace::EnableRightClickLock(true);

		m_triggerEventHandle = CreateEventW(nullptr, false, false, CommonSharedConstants::RIGHT_CLICK_LOCK_TRIGGER_EVENT);
		m_terminateEventHandle = CreateEventW(nullptr, false, false, nullptr);
		if (m_triggerEventHandle)
		{
			ResetEvent(m_triggerEventHandle);
		}
		if (m_triggerEventHandle && m_terminateEventHandle)
		{
			m_listening = true;
			m_eventThread = std::thread([this]() { EventThreadProc(); });
		}
	}

	virtual void disable() override
	{
		m_enabled = false;
		Trace::EnableRightClickLock(false);

		m_listening = false;
		if (m_terminateEventHandle)
		{
			SetEvent(m_terminateEventHandle);
		}
		if (m_eventThread.joinable())
		{
			m_eventThread.join();
		}
		if (m_triggerEventHandle)
		{
			CloseHandle(m_triggerEventHandle);
			m_triggerEventHandle = nullptr;
		}
		if (m_terminateEventHandle)
		{
			CloseHandle(m_terminateEventHandle);
			m_terminateEventHandle = nullptr;
		}

		// Safety: never leave a synthetic hold active after the module is disabled.
		m_core.EmergencyRelease();
		m_lockActive = false;
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
		if (buffer && buffer_size >= HotkeyCount)
		{
			buffer[HotkeyIdToggle] = m_activationHotkey;
			buffer[HotkeyIdPanic] = m_panicHotkey;
		}
		return HotkeyCount;
	}

	virtual bool on_hotkey(size_t hotkeyId) override
	{
		if (!m_enabled)
		{
			return false;
		}

		if (hotkeyId == HotkeyIdToggle)
		{
			// Toggle the lock feature on/off on the hook thread.
			if (m_triggerEventHandle)
			{
				return SetEvent(m_triggerEventHandle);
			}
			return false;
		}

		if (hotkeyId == HotkeyIdPanic)
		{
			// Panic release: force-release the synthetic hold immediately, regardless of state.
			m_core.EmergencyRelease();
			Logger::info("RightClickLock panic hotkey - emergency release");
			return true;
		}

		return false;
	}

private:
	void EventThreadProc()
	{
		HANDLE handles[2] = { m_triggerEventHandle, m_terminateEventHandle };

		// WH_MOUSE_LL callbacks and timers are delivered to the thread that installed the
		// hook. Ensure this thread has a message queue and a window to host the arm timer
		// and receive session-change notifications.
		MSG msg;
		PeekMessage(&msg, nullptr, WM_USER, WM_USER, PM_NOREMOVE);

		CreateHookWindow();

		// Engage automatically if the feature should auto-activate in Game Mode.
		UpdateLockState();

		while (m_listening)
		{
			auto res = MsgWaitForMultipleObjects(2, handles, false, INFINITE, QS_ALLINPUT);
			if (!m_listening)
			{
				break;
			}

			if (res == WAIT_OBJECT_0)
			{
				ToggleLock();
			}
			else if (res == WAIT_OBJECT_0 + 1)
			{
				break;
			}
			else
			{
				while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
				{
					TranslateMessage(&msg);
					DispatchMessage(&msg);
				}
			}
		}

		StopMouseHook();
		DestroyHookWindow();
		Logger::info("RightClickLock event listener stopped");
	}

	void ToggleLock()
	{
		bool active = !m_lockActive.load();
		m_lockActive = active;
		UpdateLockState();
		Logger::info(active ? "RightClickLock toggled ON" : "RightClickLock toggled OFF");
	}

	// Start/stop the mouse hook so it is active when the feature is toggled on, or when
	// auto-activate-in-game-mode is enabled and a full-screen game is running.
	void UpdateLockState()
	{
		bool shouldHook = m_lockActive.load();
		if (!shouldHook && m_autoActivateInGameMode && detect_game_mode())
		{
			shouldHook = true;
		}

		if (shouldHook)
		{
			m_core.SetHoldDelayMs(m_holdDelayMs);
			m_core.SetMoveCancelPixels(m_moveCancelPixels);
			StartMouseHook();
		}
		else
		{
			StopMouseHook();
			m_core.EmergencyRelease();
		}
	}

	void init_settings()
	{
		try
		{
			PowerToysSettings::PowerToyValues settings =
				PowerToysSettings::PowerToyValues::load_from_settings_file(RightClickLock::get_key());
			parse_settings(settings);
		}
		catch (std::exception&)
		{
			Logger::error("Invalid json when trying to load the RightClickLock settings json from file.");
		}
	}

	void parse_settings(PowerToysSettings::PowerToyValues& settings)
	{
		auto settingsObject = settings.get_raw_json();
		if (settingsObject.GetView().Size())
		{
			try
			{
				auto jsonObject = settingsObject.GetNamedObject(JSON_KEY_PROPERTIES).GetNamedObject(JSON_KEY_ACTIVATION_SHORTCUT);
				auto hotkey = PowerToysSettings::HotkeyObject::from_json(jsonObject);
				m_activationHotkey.win = hotkey.win_pressed();
				m_activationHotkey.ctrl = hotkey.ctrl_pressed();
				m_activationHotkey.shift = hotkey.shift_pressed();
				m_activationHotkey.alt = hotkey.alt_pressed();
				m_activationHotkey.key = static_cast<unsigned char>(hotkey.get_code());
			}
			catch (...)
			{
				Logger::warn("Failed to initialize RightClickLock activation shortcut");
			}

			try
			{
				auto jsonObject = settingsObject.GetNamedObject(JSON_KEY_PROPERTIES).GetNamedObject(JSON_KEY_PANIC_SHORTCUT);
				auto hotkey = PowerToysSettings::HotkeyObject::from_json(jsonObject);
				m_panicHotkey.win = hotkey.win_pressed();
				m_panicHotkey.ctrl = hotkey.ctrl_pressed();
				m_panicHotkey.shift = hotkey.shift_pressed();
				m_panicHotkey.alt = hotkey.alt_pressed();
				m_panicHotkey.key = static_cast<unsigned char>(hotkey.get_code());
			}
			catch (...)
			{
				Logger::warn("Failed to initialize RightClickLock panic shortcut");
			}

			try
			{
				auto propertiesObject = settingsObject.GetNamedObject(JSON_KEY_PROPERTIES);
				if (propertiesObject.HasKey(JSON_KEY_AUTO_ACTIVATE_IN_GAME_MODE))
				{
					auto obj = propertiesObject.GetNamedObject(JSON_KEY_AUTO_ACTIVATE_IN_GAME_MODE);
					m_autoActivateInGameMode = obj.GetNamedBoolean(JSON_KEY_VALUE);
				}
			}
			catch (...)
			{
				Logger::warn("Failed to initialize RightClickLock auto activate in game mode. Will use default value (false)");
			}

			try
			{
				auto propertiesObject = settingsObject.GetNamedObject(JSON_KEY_PROPERTIES);
				if (propertiesObject.HasKey(JSON_KEY_HOLD_DELAY_MS))
				{
					auto obj = propertiesObject.GetNamedObject(JSON_KEY_HOLD_DELAY_MS);
					m_holdDelayMs = static_cast<int>(obj.GetNamedNumber(JSON_KEY_VALUE));
				}
			}
			catch (...)
			{
				Logger::warn("Failed to initialize RightClickLock hold delay. Will use default value");
			}

			try
			{
				auto propertiesObject = settingsObject.GetNamedObject(JSON_KEY_PROPERTIES);
				if (propertiesObject.HasKey(JSON_KEY_MOVE_CANCEL_PIXELS))
				{
					auto obj = propertiesObject.GetNamedObject(JSON_KEY_MOVE_CANCEL_PIXELS);
					m_moveCancelPixels = static_cast<int>(obj.GetNamedNumber(JSON_KEY_VALUE));
				}
			}
			catch (...)
			{
				Logger::warn("Failed to initialize RightClickLock move cancel pixels. Will use default value");
			}
		}
		else
		{
			Logger::info("RightClickLock settings are empty");
		}

		// Clamp to sane ranges so hand-edited values cannot break behavior.
		m_holdDelayMs = max(RIGHT_CLICK_LOCK_MIN_HOLD_DELAY_MS, min(RIGHT_CLICK_LOCK_MAX_HOLD_DELAY_MS, m_holdDelayMs));
		m_moveCancelPixels = max(RIGHT_CLICK_LOCK_MIN_MOVE_CANCEL_PIXELS, min(RIGHT_CLICK_LOCK_MAX_MOVE_CANCEL_PIXELS, m_moveCancelPixels));

		// Default activation hotkey: Win+Alt+R
		if (m_activationHotkey.key == 0)
		{
			m_activationHotkey.win = true;
			m_activationHotkey.alt = true;
			m_activationHotkey.ctrl = false;
			m_activationHotkey.shift = false;
			m_activationHotkey.key = 'R';
		}

		// Default panic hotkey: Ctrl+Alt+R
		if (m_panicHotkey.key == 0)
		{
			m_panicHotkey.win = false;
			m_panicHotkey.alt = true;
			m_panicHotkey.ctrl = true;
			m_panicHotkey.shift = false;
			m_panicHotkey.key = 'R';
		}

		m_core.SetHoldDelayMs(m_holdDelayMs);
		m_core.SetMoveCancelPixels(m_moveCancelPixels);
	}

	void StartMouseHook()
	{
		if (m_mouseHook || m_hookActive)
		{
			return;
		}

		m_mouseHook = SetWindowsHookEx(WH_MOUSE_LL, MouseHookProc, GetModuleHandle(nullptr), 0);
		if (m_mouseHook)
		{
			m_hookActive = true;
			Logger::info("RightClickLock mouse hook started successfully");
		}
		else
		{
			DWORD error = GetLastError();
			Logger::error(L"Failed to install RightClickLock mouse hook, error: {}", error);
		}
	}

	void StopMouseHook()
	{
		if (m_mouseHook)
		{
			UnhookWindowsHookEx(m_mouseHook);
			m_mouseHook = nullptr;
			m_hookActive = false;
			// Safety: releasing the hook while locked would strand the user with a held button.
			m_core.EmergencyRelease();
			m_core.Reset();
			Logger::info("RightClickLock mouse hook stopped");
		}
	}

	void CreateHookWindow()
	{
		if (m_hookWindow)
		{
			return;
		}

		WNDCLASSEXW wc = { sizeof(WNDCLASSEXW) };
		wc.lpfnWndProc = HookWindowProc;
		wc.hInstance = GetModuleHandle(nullptr);
		wc.lpszClassName = L"RightClickLockHookWindow";
		RegisterClassExW(&wc);

		m_hookWindow = CreateWindowExW(
			0,
			L"RightClickLockHookWindow",
			nullptr,
			0,
			0, 0, 0, 0,
			HWND_MESSAGE,
			nullptr,
			GetModuleHandle(nullptr),
			nullptr);

		if (m_hookWindow)
		{
			// Receive lock/logoff notifications so we can force-release a synthetic hold.
			WTSRegisterSessionNotification(m_hookWindow, NOTIFY_FOR_THIS_SESSION);
		}
		else
		{
			Logger::error(L"Failed to create RightClickLock hook window, error: {}", GetLastError());
		}
	}

	void DestroyHookWindow()
	{
		if (m_hookWindow)
		{
			WTSUnRegisterSessionNotification(m_hookWindow);
			DestroyWindow(m_hookWindow);
			m_hookWindow = nullptr;
			UnregisterClassW(L"RightClickLockHookWindow", GetModuleHandle(nullptr));
		}
	}

	static LRESULT CALLBACK HookWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
	{
		if (g_rightClickLockInstance)
		{
			switch (msg)
			{
			case WM_TIMER:
				if (wParam == RightClickLockCore::ArmTimerId)
				{
					g_rightClickLockInstance->m_core.OnArmTimer();
					return 0;
				}
				break;

			case WM_WTSSESSION_CHANGE:
				if (wParam == WTS_SESSION_LOCK || wParam == WTS_SESSION_LOGOFF)
				{
					// Don't return from the lock screen with a synthetically-held button.
					g_rightClickLockInstance->m_core.EmergencyRelease();
				}
				break;
			}
		}

		return DefWindowProcW(hwnd, msg, wParam, lParam);
	}

	static LRESULT CALLBACK MouseHookProc(int nCode, WPARAM wParam, LPARAM lParam)
	{
		if (nCode >= 0 && g_rightClickLockInstance && g_rightClickLockInstance->m_hookActive)
		{
			auto* info = reinterpret_cast<MSLLHOOKSTRUCT*>(lParam);
			bool suppress = false;

			// Isolate exceptions: an unhandled exception here would silently unhook us and
			// could strand the user with a held button.
			try
			{
				auto& core = g_rightClickLockInstance->m_core;
				switch (wParam)
				{
				case WM_RBUTTONDOWN:
					suppress = core.HandleRightButtonDown(*info, g_rightClickLockInstance->m_hookWindow);
					break;
				case WM_RBUTTONUP:
					suppress = core.HandleRightButtonUp(*info);
					break;
				case WM_MOUSEMOVE:
					suppress = core.HandleMouseMove(*info);
					break;
				default:
					break;
				}
			}
			catch (...)
			{
				suppress = false;
			}

			if (suppress)
			{
				return 1;
			}
		}

		return CallNextHookEx(nullptr, nCode, wParam, lParam);
	}
};

extern "C" __declspec(dllexport) PowertoyModuleIface* __cdecl powertoy_create()
{
	return new RightClickLock();
}
