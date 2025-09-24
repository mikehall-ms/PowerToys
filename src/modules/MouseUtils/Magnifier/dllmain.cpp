#include "pch.h"
#include <interface/powertoy_module_interface.h>
#include <common/SettingsAPI/settings_objects.h>
#include <common/utils/json.h>
#include <common/logger/logger.h>
#include <common/utils/logger_helper.h>
#include "trace.h"
#include <common/utils/gpo.h>
#include <windows.h>

// Additional Windows API headers for magnifier functionality
#include <shellscalingapi.h>
#include <dwmapi.h>

// Link required libraries for magnifier functionality
#pragma comment(lib, "Shcore.lib")
#pragma comment(lib, "Dwmapi.lib")

extern "C" IMAGE_DOS_HEADER __ImageBase;

HMODULE m_hModule;

// ============================================================================
// MAGNIFIER CONFIGURATION AND CONSTANTS
// ============================================================================
#define MAGNIFIER_WINDOW_CLASS L"PowerToysMagnifierWindow"
#define DEFAULT_MAGNIFICATION 2.0f
#define MIN_MAGNIFICATION 1.0f
#define MAX_MAGNIFICATION 5.0f
#define BASE_CAPTURE_SIZE 150           /* Base capture size at 96 DPI */
#define FRAME_THICKNESS 2
#define MIN_CURSOR_GAP 30               /* Minimum visual gap from cursor */
#define SAFETY_MARGIN 20                /* Additional safety margin to prevent capture overlap */
#define REFRESH_TIMER_ID 1              /* Timer ID for periodic refresh */
#define REFRESH_INTERVAL_MS 50          /* Refresh every 50ms (20 FPS) for smooth updates */
#define ENABLE_DYNAMIC_REFRESH 1        /* Set to 0 to disable timer-based refresh (mouse-only mode) */

// The PowerToy name that will be shown in the settings.
const static wchar_t* MODULE_NAME = L"Magnifier";
// Description used in settings page.
const static wchar_t* MODULE_DESC = L"A screen magnifier that follows your mouse cursor with advanced positioning and DPI awareness";

// Forward declarations for magnifier functions
static LRESULT CALLBACK MagnifierWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
static LRESULT CALLBACK LowLevelMouseProc(int code, WPARAM wParam, LPARAM lParam);

// Static flag to track if window class is registered
static bool g_windowClassRegistered = false;

// Forward declaration of the Magnifier class
class Magnifier;

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID /*lpReserved*/)
{
    m_hModule = hModule;
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
        Logger::info("Magnifier DLL_PROCESS_ATTACH");
        break;
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
        break;
    case DLL_PROCESS_DETACH:
        Logger::info("Magnifier DLL_PROCESS_DETACH");
        // Unregister window class when DLL is unloaded
        if (g_windowClassRegistered)
        {
            UnregisterClass(MAGNIFIER_WINDOW_CLASS, hModule);
            g_windowClassRegistered = false;
            Logger::info("Magnifier window class unregistered");
        }
        break;
    }
    return TRUE;
}

// Implement the PowerToy Module Interface and all the required methods.
class Magnifier : public PowertoyModuleIface
{
public: // public helpers needed by window procedures
    static Magnifier* GetInstance(HWND hwnd)
    {
        if (hwnd)
        {
            return reinterpret_cast<Magnifier*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
        }
        return nullptr;
    }
    void SetInstance(HWND hwnd)
    {
        if (hwnd)
        {
            SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
        }
    }

private:
    bool m_enabled = false;                 /* Module enabled state */
    Hotkey m_activationHotkey{};            /* Activation hotkey */
    float m_magnificationLevel = DEFAULT_MAGNIFICATION; /* Current magnification */

    // ============================================================================
// MAGNIFIER STATE VARIABLES
// ============================================================================
    bool m_magnifierActive = false;
    HWND m_hMagnifierWindow = nullptr;
    HHOOK m_hMouseHook = nullptr;
    HDC m_hScreenDC = nullptr;
    HDC m_hMemDC = nullptr;
    HBITMAP m_hBitmap = nullptr;
    HBITMAP m_hOldBitmap = nullptr;

    POINT m_mousePos = {0, 0};
    RECT m_currentMonitorRect = {0};
    HMONITOR m_currentMonitor = nullptr;
    int m_captureSize = BASE_CAPTURE_SIZE;
    int m_displayWidth = 0;
    int m_displayHeight = 0;
    int m_currentQuadrant = 0;            /* 0=BR,1=BL,2=TR,3=TL */
    int m_currentDPI = 96;
    int m_dynamicCursorGap = MIN_CURSOR_GAP;
    POINT m_cursorOffsetInCapture = {0, 0};
    DWORD m_lastMouseMoveTime = 0;
    BOOL m_refreshTimerActive = FALSE;

public:
    Magnifier()
    {
        LoggerHelpers::init_logger(MODULE_NAME, L"ModuleInterface", "magnifier");
        Logger::info("Magnifier module constructor called");
        init_settings();
        Logger::info("Magnifier module initialized successfully");
    };

    virtual void destroy() override
    {
        Logger::info("Magnifier::destroy() called");
        if (m_magnifierActive)
        {
            DeactivateMagnifier();
        }
        delete this;
    }

    virtual const wchar_t* get_name() override 
    { 
        Logger::info("Magnifier get_name() called, returning: Magnifier");
        return MODULE_NAME; 
    }
    
    virtual const wchar_t* get_key() override 
    { 
        Logger::info("Magnifier get_key() called, returning: Magnifier");
        return MODULE_NAME; 
    }

    virtual powertoys_gpo::gpo_rule_configured_t gpo_policy_enabled_configuration() override
    {
        auto gpo_result = powertoys_gpo::getConfiguredMagnifierEnabledValue();
        Logger::info("Magnifier gpo_policy_enabled_configuration() called, result: {}", static_cast<int>(gpo_result));
        return gpo_result;
    }

    virtual bool get_config(wchar_t* buffer, int* buffer_size) override
    {
        Logger::info("Magnifier get_config() called");
        HINSTANCE hinstance = reinterpret_cast<HINSTANCE>(&__ImageBase);
        PowerToysSettings::Settings settings(hinstance, get_name());
        settings.set_description(MODULE_DESC);
        settings.set_overview_link(L"https://aka.ms/PowerToysOverview_Magnifier");
        // Magnification level is managed by XAML slider (MouseUtils page). Do not duplicate here.
        bool result = settings.serialize_to_buffer(buffer, buffer_size);
        Logger::info("Magnifier get_config() completed, result: {}", result);
        return result;
    }

    virtual void call_custom_action(const wchar_t* /*action*/) override {}

    virtual void set_config(const wchar_t* config) override
    {
        try
        {
            PowerToysSettings::PowerToyValues values =
                PowerToysSettings::PowerToyValues::from_json_string(config, get_key());
            parse_settings(values);
            if (m_magnifierActive)
            {
                RecomputeDisplaySize();
                UpdateDynamicOffset();
            }
        }
        catch (std::exception&)
        {
            Logger::error("Invalid json when trying to parse Magnifier settings json.");
        }
    }

    virtual void enable()
    {
        m_enabled = true;
        Trace::EnableMagnifier(true);
        Logger::info("Magnifier enabled");
    }

    virtual void disable()
    {
        if (m_magnifierActive)
        {
            DeactivateMagnifier();
        }
        m_enabled = false;
        Trace::EnableMagnifier(false);
        Logger::info("Magnifier disabled");
    }

    virtual bool is_enabled() override { return m_enabled; }
    virtual bool is_enabled_by_default() const override { return false; }

    virtual size_t get_hotkeys(Hotkey* buffer, size_t buffer_size) override
    {
        if (buffer && buffer_size >= 1)
        {
            buffer[0] = m_activationHotkey;
        }
        return 1;
    }

    virtual bool on_hotkey(size_t hotkeyId) override
    {
        Logger::trace("Magnifier hotkey is invoked from Centralized keyboard hook");
        if (!m_enabled)
        {
            Logger::warn("Magnifier hotkey triggered but module is disabled");
            return false;
        }

        if (hotkeyId == 0)
        {
            Trace::ActivateMagnifier();
            if (m_magnifierActive)
            {
                Logger::info("Magnifier is active, deactivating");
                DeactivateMagnifier();
            }
            else
            {
                Logger::info("Magnifier is inactive, activating");
                ActivateMagnifier();
            }
            return true;
        }
        return false;
    }

private:
    void ActivateMagnifier()
    {
        if (m_magnifierActive)
        {
            Logger::warn("ActivateMagnifier called but magnifier is already active");
            return;
        }
        
        Logger::info("ActivateMagnifier starting initialization");
        
        try
        {
            SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
            GetCursorPos(&m_mousePos);
            UpdateMonitorInfo(m_mousePos);
            RecomputeDisplaySize();
            
            Logger::info("Magnifier: Initializing application");
            if (!InitializeApplication())
            {
                Logger::error("Failed to initialize magnifier application");
                Cleanup();
                return;
            }
            
            Logger::info("Magnifier: Initializing GDI");
            if (!InitializeGDI())
            {
                Logger::error("Failed to initialize magnifier GDI");
                Cleanup();
                return;
            }
            
            Logger::info("Magnifier: Creating magnifier window");
            if (!CreateMagnifierWindow())
            {
                Logger::error("Failed to create magnifier window");
                Cleanup();
                return;
            }
            
            Logger::info("Magnifier: Setting up mouse hook");
            m_hMouseHook = SetWindowsHookEx(WH_MOUSE_LL, LowLevelMouseProc, m_hModule, 0);
            if (!m_hMouseHook)
            {
                Logger::error("Failed to set up mouse hook for magnifier, error: {}", GetLastError());
                Cleanup();
                return;
            }
            
            UpdateMagnifierDisplay();
            ShowWindow(m_hMagnifierWindow, SW_SHOW);
            UpdateWindow(m_hMagnifierWindow);
            StartRefreshTimer();
            m_magnifierActive = true;
            Logger::info("Magnifier successfully activated");
        }
        catch (...)
        {
            Logger::error("Exception occurred while activating magnifier");
            Cleanup();
        }
    }

    void DeactivateMagnifier()
    {
        if (!m_magnifierActive)
        {
            Logger::warn("DeactivateMagnifier called but magnifier is not active");
            return;
        }
        
        Logger::info("DeactivateMagnifier starting cleanup");
        StopRefreshTimer();
        Cleanup();
        m_magnifierActive = false;
        Logger::info("Magnifier deactivated and cleaned up");
    }

    // SETTINGS PARSING
    void init_settings()
    {
        try
        {
            PowerToysSettings::PowerToyValues settings =
                PowerToysSettings::PowerToyValues::load_from_settings_file(Magnifier::get_key());
            parse_settings(settings);
        }
        catch (std::exception&)
        {
            Logger::info("Magnifier settings file not found or invalid, using defaults");
            m_activationHotkey.win = true;
            m_activationHotkey.alt = true;
            m_activationHotkey.key = VK_OEM_COMMA;
        }
    }

    void parse_settings(PowerToysSettings::PowerToyValues& settings)
    {
        try
        {
            auto settingsObject = settings.get_raw_json();
            if (settingsObject.GetView().Size())
            {
                // Hotkey
                try
                {
                    auto jsonHotkey = settingsObject.GetNamedObject(L"properties").GetNamedObject(L"activation_shortcut");
                    auto hotkey = PowerToysSettings::HotkeyObject::from_json(jsonHotkey);
                    m_activationHotkey.win = hotkey.win_pressed();
                    m_activationHotkey.ctrl = hotkey.ctrl_pressed();
                    m_activationHotkey.shift = hotkey.shift_pressed();
                    m_activationHotkey.alt = hotkey.alt_pressed();
                    m_activationHotkey.key = static_cast<unsigned char>(hotkey.get_code());
                }
                catch (...)
                {
                    Logger::warn("Failed to initialize Magnifier activation shortcut. Using default Win+Alt+Comma");
                    m_activationHotkey.win = true;
                    m_activationHotkey.alt = true;
                    m_activationHotkey.key = VK_OEM_COMMA;
                }
                // Magnification (value comes from XAML slider, still persisted in JSON)
                try
                {
                    auto jsonMag = settingsObject.GetNamedObject(L"properties").GetNamedObject(L"magnification_level");
                    float value = static_cast<float>(jsonMag.GetNamedNumber(L"value"));
                    if (value >= MIN_MAGNIFICATION && value <= MAX_MAGNIFICATION)
                    {
                        m_magnificationLevel = value;
                    }
                    else
                    {
                        Logger::warn("Magnification level out of range, using default");
                        m_magnificationLevel = DEFAULT_MAGNIFICATION;
                    }
                }
                catch (...)
                {
                    Logger::warn("Failed to parse magnification level, using default");
                    m_magnificationLevel = DEFAULT_MAGNIFICATION;
                }
            }
            else
            {
                Logger::info("Magnifier settings are empty, using defaults");
                m_activationHotkey.win = true;
                m_activationHotkey.alt = true;
                m_activationHotkey.key = VK_OEM_COMMA;
                m_magnificationLevel = DEFAULT_MAGNIFICATION;
            }
        }
        catch (...)
        {
            Logger::error("Failed to parse Magnifier settings");
            m_activationHotkey.win = true;
            m_activationHotkey.alt = true;
            m_activationHotkey.key = VK_OEM_COMMA;
            m_magnificationLevel = DEFAULT_MAGNIFICATION;
        }
    }

    // DYNAMIC SIZING AND POSITIONING
    void RecomputeDisplaySize()
    {
        m_displayWidth = static_cast<int>(m_captureSize * m_magnificationLevel);
        m_displayHeight = static_cast<int>(m_captureSize * m_magnificationLevel);
        UpdateDynamicOffset();
    }

    int CalculateDynamicCaptureSize(int dpi)
    {
        int scaledSize = MulDiv(BASE_CAPTURE_SIZE, dpi, 96);
        if (scaledSize < 50) scaledSize = 50;
        if (scaledSize > 500) scaledSize = 500;
        return scaledSize;
    }

    int CalculateDynamicCursorGap()
    {
        int captureRadius = m_captureSize / 2;
        int minimumGapForCapture = captureRadius + SAFETY_MARGIN;
        int scaledGap = static_cast<int>(MIN_CURSOR_GAP * m_magnificationLevel);
        int dpiAdjustedGap = MulDiv(scaledGap, m_currentDPI, 96);
        int dynamicGap = max(minimumGapForCapture, dpiAdjustedGap);
        if (dynamicGap < MIN_CURSOR_GAP) dynamicGap = MIN_CURSOR_GAP;
        int maxGap = (m_currentMonitorRect.right - m_currentMonitorRect.left) / 6;
        if (dynamicGap > maxGap) dynamicGap = maxGap;
        return dynamicGap;
    }

    void UpdateDynamicOffset()
    {
        int newGap = CalculateDynamicCursorGap();
        if (newGap != m_dynamicCursorGap)
        {
            m_dynamicCursorGap = newGap;
        }
    }

    // MONITOR AWARENESS AND DPI HANDLING
    void UpdateMonitorInfo(POINT mousePos)
    {
        HMONITOR newMonitor = MonitorFromPoint(mousePos, MONITOR_DEFAULTTONEAREST);
        if (newMonitor != m_currentMonitor)
        {
            m_currentMonitor = newMonitor;
            MONITORINFO mi = {0};
            mi.cbSize = sizeof(MONITORINFO);
            if (GetMonitorInfo(m_currentMonitor, &mi))
            {
                m_currentMonitorRect = mi.rcMonitor;
                UINT dpiX = 96, dpiY = 96;
                if (SUCCEEDED(GetDpiForMonitor(m_currentMonitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY)))
                {
                    m_currentDPI = static_cast<int>(dpiX);
                }
                else
                {
                    HDC hdc = GetDC(nullptr);
                    if (hdc)
                    {
                        m_currentDPI = GetDeviceCaps(hdc, LOGPIXELSX);
                        ReleaseDC(nullptr, hdc);
                    }
                    else
                    {
                        m_currentDPI = 96;
                    }
                }
                int newCaptureSize = CalculateDynamicCaptureSize(m_currentDPI);
                if (newCaptureSize != m_captureSize)
                {
                    m_captureSize = newCaptureSize;
                    RecomputeDisplaySize();
                    UpdateDynamicOffset();
                }
                RecreateGDIResources();
            }
        }
    }

    void RecreateGDIResources()
    {
        if (m_hOldBitmap) { SelectObject(m_hMemDC, m_hOldBitmap); m_hOldBitmap = nullptr; }
        if (m_hBitmap) { DeleteObject(m_hBitmap); m_hBitmap = nullptr; }
        if (m_hMemDC) { DeleteDC(m_hMemDC); m_hMemDC = nullptr; }
        if (m_hScreenDC) { ReleaseDC(nullptr, m_hScreenDC); m_hScreenDC = nullptr; }
        if (m_magnifierActive && m_hMagnifierWindow)
        {
            InitializeGDI();
            SetWindowPos(m_hMagnifierWindow, nullptr, 0, 0,
                         m_displayWidth + (FRAME_THICKNESS * 2),
                         m_displayHeight + (FRAME_THICKNESS * 2),
                         SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        }
    }

    // POSITIONING LOGIC
    POINT CalculatePosition(POINT mouse)
    {
        int winW = m_displayWidth + FRAME_THICKNESS * 2;
        int winH = m_displayHeight + FRAME_THICKNESS * 2;
        POINT pos;
        pos.x = mouse.x + m_dynamicCursorGap;
        pos.y = mouse.y + m_dynamicCursorGap;
        if (pos.x + winW <= m_currentMonitorRect.right && pos.y + winH <= m_currentMonitorRect.bottom) { m_currentQuadrant = 0; return pos; }
        pos.x = mouse.x - m_dynamicCursorGap - winW;
        pos.y = mouse.y + m_dynamicCursorGap;
        if (pos.x >= m_currentMonitorRect.left && pos.y + winH <= m_currentMonitorRect.bottom) { m_currentQuadrant = 1; return pos; }
        pos.x = mouse.x - m_dynamicCursorGap - winW;
        pos.y = mouse.y - m_dynamicCursorGap - winH;
        if (pos.x >= m_currentMonitorRect.left && pos.y >= m_currentMonitorRect.top) { m_currentQuadrant = 3; return pos; }
        pos.x = mouse.x + m_dynamicCursorGap;
        pos.y = mouse.y - m_dynamicCursorGap - winH;
        if (pos.x + winW <= m_currentMonitorRect.right && pos.y >= m_currentMonitorRect.top) { m_currentQuadrant = 2; return pos; }
        pos.x = mouse.x + m_dynamicCursorGap;
        pos.y = mouse.y + m_dynamicCursorGap;
        if (pos.x + winW > m_currentMonitorRect.right) pos.x = m_currentMonitorRect.right - winW;
        if (pos.y + winH > m_currentMonitorRect.bottom) pos.y = m_currentMonitorRect.bottom - winH;
        if (pos.x < m_currentMonitorRect.left) pos.x = m_currentMonitorRect.left;
        if (pos.y < m_currentMonitorRect.top)  pos.y = m_currentMonitorRect.top;
        m_currentQuadrant = 0;
        return pos;
    }

    // SCREEN CAPTURE AND RENDERING
    void CaptureArea()
    {
        if (!m_hMemDC || !m_hBitmap || !m_hScreenDC)
        {
            return;
        }
        int capW = m_captureSize;
        int capH = m_captureSize;
        int idealX = m_mousePos.x - capW/2;
        int idealY = m_mousePos.y - capH/2;
        int actualX = idealX;
        int actualY = idealY;
        if (actualX < m_currentMonitorRect.left) actualX = m_currentMonitorRect.left;
        if (actualY < m_currentMonitorRect.top)  actualY = m_currentMonitorRect.top;
        if (actualX + capW > m_currentMonitorRect.right)  actualX = m_currentMonitorRect.right  - capW;
        if (actualY + capH > m_currentMonitorRect.bottom) actualY = m_currentMonitorRect.bottom - capH;
        m_cursorOffsetInCapture.x = m_mousePos.x - actualX;
        m_cursorOffsetInCapture.y = m_mousePos.y - actualY;
        BitBlt(m_hMemDC, 0, 0, capW, capH, m_hScreenDC, actualX, actualY, SRCCOPY);
    }

    void UpdateMagnifierDisplay()
    {
        if (!m_hMagnifierWindow) return;
        UpdateMonitorInfo(m_mousePos);
        POINT newPos = CalculatePosition(m_mousePos);
        SetWindowPos(m_hMagnifierWindow, HWND_TOPMOST, newPos.x, newPos.y, 0, 0,
                     SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        CaptureArea();
        InvalidateRect(m_hMagnifierWindow, nullptr, FALSE);
    }

    COLORREF GetSystemAccentColor()
    {
        COLORREF accentColor = RGB(0, 120, 215);
        DWORD dwColorizationColor = 0; BOOL fOpaqueBlend = FALSE;
        if (SUCCEEDED(DwmGetColorizationColor(&dwColorizationColor, &fOpaqueBlend)))
        {
            accentColor = RGB((dwColorizationColor & 0xFF0000) >> 16,
                              (dwColorizationColor & 0x00FF00) >> 8,
                              (dwColorizationColor & 0x0000FF));
        }
        return accentColor;
    }

    void DrawMagnifiedContent(HDC hdc)
    {
        RECT rc; GetClientRect(m_hMagnifierWindow, &rc);
        COLORREF accentColor = GetSystemAccentColor();
        HBRUSH frameBrush = CreateSolidBrush(accentColor);
        FillRect(hdc, &rc, frameBrush);
        if (m_hMemDC && m_hBitmap)
        {
            StretchBlt(hdc, FRAME_THICKNESS, FRAME_THICKNESS,
                       m_displayWidth, m_displayHeight,
                       m_hMemDC, 0, 0, m_captureSize, m_captureSize, SRCCOPY);
        }
        COLORREF borderColor = RGB(
            min(255, GetRValue(accentColor) + 60),
            min(255, GetGValue(accentColor) + 60),
            min(255, GetBValue(accentColor) + 60));
        HPEN borderPen = CreatePen(PS_SOLID, 1, borderColor);
        HGDIOBJ oldPen = SelectObject(hdc, borderPen);
        HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
        Rectangle(hdc,
                  FRAME_THICKNESS-1, FRAME_THICKNESS-1,
                  FRAME_THICKNESS + m_displayWidth + 1,
                  FRAME_THICKNESS + m_displayHeight + 1);
        int cx = FRAME_THICKNESS + static_cast<int>(static_cast<float>(m_cursorOffsetInCapture.x) * m_magnificationLevel);
        int cy = FRAME_THICKNESS + static_cast<int>(static_cast<float>(m_cursorOffsetInCapture.y) * m_magnificationLevel);
        if (cx < FRAME_THICKNESS) cx = FRAME_THICKNESS;
        if (cy < FRAME_THICKNESS) cy = FRAME_THICKNESS;
        if (cx >= FRAME_THICKNESS + m_displayWidth) cx = FRAME_THICKNESS + m_displayWidth - 1;
        if (cy >= FRAME_THICKNESS + m_displayHeight) cy = FRAME_THICKNESS + m_displayHeight - 1;
        HPEN crossOutlinePen = CreatePen(PS_SOLID, 1, RGB(255, 255, 255));
        SelectObject(hdc, crossOutlinePen);
        MoveToEx(hdc, cx-11, cy-1, nullptr); LineTo(hdc, cx+11, cy-1);
        MoveToEx(hdc, cx-11, cy+1, nullptr); LineTo(hdc, cx+11, cy+1);
        MoveToEx(hdc, cx-10, cy-1, nullptr); LineTo(hdc, cx-10, cy+1);
        MoveToEx(hdc, cx+10, cy-1, nullptr); LineTo(hdc, cx+10, cy+1);
        MoveToEx(hdc, cx-1, cy-11, nullptr); LineTo(hdc, cx-1, cy+11);
        MoveToEx(hdc, cx+1, cy-11, nullptr); LineTo(hdc, cx+1, cy+11);
        MoveToEx(hdc, cx-1, cy-10, nullptr); LineTo(hdc, cx+1, cy-10);
        MoveToEx(hdc, cx-1, cy+10, nullptr); LineTo(hdc, cx+1, cy+10);
        HPEN crossPen = CreatePen(PS_SOLID, 1, RGB(255, 0, 0));
        SelectObject(hdc, crossPen);
        MoveToEx(hdc, cx-10, cy, nullptr); LineTo(hdc, cx+10, cy);
        MoveToEx(hdc, cx, cy-10, nullptr); LineTo(hdc, cx, cy+10);
        SelectObject(hdc, oldPen);
        SelectObject(hdc, oldBrush);
        DeleteObject(frameBrush);
        DeleteObject(borderPen);
        DeleteObject(crossPen);
        DeleteObject(crossOutlinePen);
    }

    // WINDOW INITIALIZATION AND MANAGEMENT
    BOOL InitializeApplication()
    {
        Logger::info("InitializeApplication called, g_windowClassRegistered = {}", g_windowClassRegistered);
        
        // Only register the window class if it hasn't been registered yet
        if (!g_windowClassRegistered)
        {
            WNDCLASSEX wc = {0};
            wc.cbSize = sizeof(wc);
            wc.style = CS_HREDRAW | CS_VREDRAW;
            wc.lpfnWndProc = MagnifierWindowProc;
            wc.hInstance = m_hModule;
            wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
            wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
            wc.hIconSm = wc.hIcon;
            wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
            wc.lpszClassName = MAGNIFIER_WINDOW_CLASS;
            
            ATOM result = RegisterClassEx(&wc);
            if (result == 0)
            {
                DWORD error = GetLastError();
                if (error == ERROR_CLASS_ALREADY_EXISTS)
                {
                    Logger::info("Window class already exists, proceeding");
                    g_windowClassRegistered = true;
                    return TRUE;
                }
                else
                {
                    Logger::error("Failed to register window class, error: {}", error);
                    return FALSE;
                }
            }
            else
            {
                g_windowClassRegistered = true;
                Logger::info("Window class registered successfully");
            }
        }
        else
        {
            Logger::info("Window class already registered, skipping registration");
        }
        
        return TRUE;
    }

    BOOL InitializeGDI()
    {
        Logger::info("InitializeGDI called");
        
        m_hScreenDC = GetDC(nullptr);
        if (!m_hScreenDC) 
        {
            Logger::error("Failed to get screen DC");
            return FALSE;
        }
        
        m_hMemDC = CreateCompatibleDC(m_hScreenDC);
        if (!m_hMemDC)
        {
            Logger::error("Failed to create compatible DC");
            ReleaseDC(nullptr, m_hScreenDC); 
            m_hScreenDC = nullptr; 
            return FALSE;
        }
        
        m_hBitmap = CreateCompatibleBitmap(m_hScreenDC, m_captureSize, m_captureSize);
        if (!m_hBitmap)
        {
            Logger::error("Failed to create compatible bitmap");
            DeleteDC(m_hMemDC); 
            ReleaseDC(nullptr, m_hScreenDC); 
            m_hMemDC = nullptr; 
            m_hScreenDC = nullptr; 
            return FALSE;
        }
        
        m_hOldBitmap = static_cast<HBITMAP>(SelectObject(m_hMemDC, m_hBitmap));
        Logger::info("GDI initialized successfully");
        return TRUE;
    }

    BOOL CreateMagnifierWindow()
    {
        Logger::info("CreateMagnifierWindow called");
        
        m_hMagnifierWindow = CreateWindowEx(WS_EX_TOPMOST | WS_EX_NOACTIVATE,
                                            MAGNIFIER_WINDOW_CLASS, L"PowerToys Magnifier", WS_POPUP,
                                            0, 0,
                                            m_displayWidth + FRAME_THICKNESS*2,
                                            m_displayHeight + FRAME_THICKNESS*2,
                                            nullptr, nullptr, m_hModule, this);
        
        if (!m_hMagnifierWindow)
        {
            Logger::error("Failed to create magnifier window, error: {}", GetLastError());
            return FALSE;
        }
        
        Logger::info("Magnifier window created successfully");
        return TRUE;
    }

    void Cleanup()
    {
        Logger::info("Cleanup called");
        
        StopRefreshTimer();
        
        if (m_hMouseHook) 
        { 
            Logger::info("Unhooking mouse hook");
            UnhookWindowsHookEx(m_hMouseHook); 
            m_hMouseHook = nullptr; 
        }
        
        if (m_hOldBitmap) 
        { 
            Logger::info("Restoring old bitmap");
            SelectObject(m_hMemDC, m_hOldBitmap); 
            m_hOldBitmap = nullptr; 
        }
        
        if (m_hBitmap) 
        { 
            Logger::info("Deleting bitmap");
            DeleteObject(m_hBitmap); 
            m_hBitmap = nullptr; 
        }
        
        if (m_hMemDC) 
        { 
            Logger::info("Deleting memory DC");
            DeleteDC(m_hMemDC); 
            m_hMemDC = nullptr; 
        }
        
        if (m_hScreenDC) 
        { 
            Logger::info("Releasing screen DC");
            ReleaseDC(nullptr, m_hScreenDC); 
            m_hScreenDC = nullptr; 
        }
        
        if (m_hMagnifierWindow) 
        { 
            Logger::info("Destroying magnifier window");
            DestroyWindow(m_hMagnifierWindow); 
            m_hMagnifierWindow = nullptr; 
        }
        
        Logger::info("Cleanup completed");
    }

    // REFRESH TIMER MANAGEMENT
    void StartRefreshTimer()
    {
#if ENABLE_DYNAMIC_REFRESH
        if (!m_refreshTimerActive && m_hMagnifierWindow)
        {
            if (SetTimer(m_hMagnifierWindow, REFRESH_TIMER_ID, REFRESH_INTERVAL_MS, nullptr))
            {
                m_refreshTimerActive = TRUE;
            }
        }
#endif
    }

    void StopRefreshTimer()
    {
        if (m_refreshTimerActive && m_hMagnifierWindow)
        {
            KillTimer(m_hMagnifierWindow, REFRESH_TIMER_ID);
            m_refreshTimerActive = FALSE;
        }
    }

    void RefreshMagnifierContent()
    {
        if (!m_hMagnifierWindow) return;
        CaptureArea();
        InvalidateRect(m_hMagnifierWindow, nullptr, FALSE);
    }

public:
    void OnMouseMove(POINT mousePos)
    {
        m_mousePos = mousePos;
        m_lastMouseMoveTime = GetTickCount();
        UpdateMagnifierDisplay();
    }

    void OnWindowPaint(HDC hdc)
    {
        DrawMagnifiedContent(hdc);
    }

    void OnTimer(WPARAM timerId)
    {
        if (timerId == REFRESH_TIMER_ID)
        {
            DWORD currentTime = GetTickCount();
            if (currentTime - m_lastMouseMoveTime > 100)
            {
                RefreshMagnifierContent();
            }
        }
    }

    void OnKeyDown(WPARAM vkCode)
    {
        if (vkCode == VK_ESCAPE)
        {
            DeactivateMagnifier();
        }
    }
};

// ============================================================================
// STATIC WINDOW PROCEDURES
// ============================================================================

static LRESULT CALLBACK MagnifierWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    Magnifier* instance = nullptr;
    if (msg == WM_CREATE)
    {
        CREATESTRUCT* cs = reinterpret_cast<CREATESTRUCT*>(lParam);
        instance = reinterpret_cast<Magnifier*>(cs->lpCreateParams);
        if (instance)
        {
            instance->SetInstance(hwnd);
        }
    }
    else
    {
        instance = Magnifier::GetInstance(hwnd);
    }

    switch (msg)
    {
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        if (instance)
        {
            instance->OnWindowPaint(hdc);
        }
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_TIMER:
        if (instance) instance->OnTimer(wParam); return 0;
    case WM_KEYDOWN:
        if (instance) instance->OnKeyDown(wParam); return 0;
    case WM_LBUTTONDOWN:
        SendMessage(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, lParam); return 0;
    case WM_DESTROY:
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

static LRESULT CALLBACK LowLevelMouseProc(int code, WPARAM wParam, LPARAM lParam)
{
    if (code == HC_ACTION && wParam == WM_MOUSEMOVE)
    {
        PMSLLHOOKSTRUCT ms = reinterpret_cast<PMSLLHOOKSTRUCT>(lParam);
        HWND hwnd = FindWindow(MAGNIFIER_WINDOW_CLASS, nullptr);
        if (hwnd)
        {
            Magnifier* instance = Magnifier::GetInstance(hwnd);
            if (instance)
            {
                instance->OnMouseMove(ms->pt);
            }
        }
    }
    return CallNextHookEx(nullptr, code, wParam, lParam);
}

extern "C" __declspec(dllexport) PowertoyModuleIface* __cdecl powertoy_create()
{
    Logger::info("Magnifier powertoy_create() called - creating new Magnifier instance");
#pragma warning(push)
#pragma warning(disable : 26403) // Reset or explicitly delete an owner<T> pointer - ownership transferred to caller
    auto magnifier = new Magnifier();
#pragma warning(pop)
    Logger::info("Magnifier powertoy_create() completed successfully");
    return magnifier;
}