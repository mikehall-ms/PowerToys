#include "pch.h"
#include "Overlay.h"
#include <common/logger/logger.h>
#include <string>
#include <cstdarg>
#include <mutex>
#include <process.h> // _beginthreadex

// Enable/disable diagnostics
// #define LOG_UI_DIAG 1
#define LOG_SETTINGS_DIAG 1

#if LOG_UI_DIAG || LOG_SETTINGS_DIAG
// Log file in user's Documents folder
#include <shlobj.h>      // SHGetKnownFolderPath / SHGetFolderPathW
#include <knownfolders.h>

static std::once_flag g_uiLogOnce;
static std::mutex g_uiLogMutex;
static std::wstring g_uiLogPath; // resolved at runtime to %USERPROFILE%\Documents\mousepointerui.log

static std::wstring GetUiLogPath()
{
    // Prefer KnownFolders (Vista+), fallback to legacy CSIDL
    PWSTR docs = nullptr;
    std::wstring path;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &docs)) && docs)
    {
        path.assign(docs);
        CoTaskMemFree(docs);
    }
    else
    {
        wchar_t buf[MAX_PATH]{};
        if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_PERSONAL, nullptr, SHGFP_TYPE_CURRENT, buf)))
        {
            path.assign(buf);
        }
        else
        {
            path.assign(L"C:\\"); // last-resort fallback
        }
    }
    if (!path.empty() && path.back() != L'\\')
    {
        path.push_back(L'\\');
    }
    path.append(L"mousepointerui.log");
    return path;
}

static void UiLogInit()
{
    std::call_once(g_uiLogOnce, []() {
        g_uiLogPath = GetUiLogPath();
        // Best-effort: delete previous file
        DeleteFileW(g_uiLogPath.c_str());
    });
}

static void UiLog(const wchar_t* fmt, ...)
{
    UiLogInit();
    wchar_t buffer[1024];
    va_list args;
    va_start(args, fmt);
    _vsnwprintf_s(buffer, _countof(buffer), _TRUNCATE, fmt, args);
    va_end(args);

    std::lock_guard<std::mutex> lock(g_uiLogMutex);
    HANDLE h = CreateFileW(g_uiLogPath.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
    {
        // Fallback to debugger output if file cannot be opened
        OutputDebugStringW(L"[MousePointerCrosshairsUI] ");
        OutputDebugStringW(buffer);
        OutputDebugStringW(L"\r\n");
        return;
    }
    DWORD written = 0;
    std::wstring line(buffer);
    line.append(L"\r\n");
    WriteFile(h, line.c_str(), static_cast<DWORD>(line.size() * sizeof(wchar_t)), &written, nullptr);
    CloseHandle(h);
}
#else
#define UiLog(...) ((void)0)
#endif

// --- New DPI/metrics debug helpers ---
#if LOG_UI_DIAG
static const wchar_t* DpiContextName(DPI_AWARENESS_CONTEXT ctx)
{
    if (AreDpiAwarenessContextsEqual(ctx, DPI_AWARENESS_CONTEXT_UNAWARE)) return L"UNAWARE";
    if (AreDpiAwarenessContextsEqual(ctx, DPI_AWARENESS_CONTEXT_UNAWARE_GDISCALED)) return L"UNAWARE_GDISCALED";
    if (AreDpiAwarenessContextsEqual(ctx, DPI_AWARENESS_CONTEXT_SYSTEM_AWARE)) return L"SYSTEM_AWARE";
    if (AreDpiAwarenessContextsEqual(ctx, DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE)) return L"PER_MONITOR_AWARE";
    if (AreDpiAwarenessContextsEqual(ctx, DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) return L"PER_MONITOR_AWARE_V2";
    return L"UNKNOWN";
}

static void LogVirtualScreenMetrics(const wchar_t* tag)
{
    const int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    const int vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    UiLog(L"%s: SM_VIRTUAL: [%d,%d %dx%d]", tag, vx, vy, vw, vh);
}

static void LogDpiState(const wchar_t* tag, HWND hwnd)
{
    const auto tctx = GetThreadDpiAwarenessContext();
    const auto wctx = hwnd ? GetWindowDpiAwarenessContext(hwnd) : (DPI_AWARENESS_CONTEXT)nullptr;
    const UINT wDpi = hwnd ? GetDpiForWindow(hwnd) : 0;
    const UINT sysDpi = GetDpiForSystem();
    UiLog(L"%s: ThreadDPI=%s WindowDPI=%s GetDpiForWindow=%u GetDpiForSystem=%u", tag,
          DpiContextName(tctx), wctx ? DpiContextName(wctx) : L"(null)", wDpi, sysDpi);
    LogVirtualScreenMetrics(L"    ");
}
#else
static inline const wchar_t* DpiContextName(DPI_AWARENESS_CONTEXT) { return L""; }
static inline void LogVirtualScreenMetrics(const wchar_t*, ...) {}
static inline void LogDpiState(const wchar_t*, HWND) {}
#endif

// Settings watching and JSON parsing includes
#include <winrt/Windows.Data.Json.h>
#include <fstream>

// Helper: read UTF-8 file to std::wstring
static bool ReadFileUtf8(const std::wstring& path, std::wstring& out)
{
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
    {
        return false;
    }
    LARGE_INTEGER li{};
    if (!GetFileSizeEx(h, &li) || li.QuadPart <= 0)
    {
        CloseHandle(h);
        return false;
    }
    const DWORD size = static_cast<DWORD>(li.QuadPart);
    std::string bytes;
    bytes.resize(size);
    DWORD read = 0;
    BOOL ok = ReadFile(h, bytes.data(), size, &read, nullptr);
    CloseHandle(h);
    if (!ok || read == 0)
    {
        return false;
    }
    int wlen = MultiByteToWideChar(CP_UTF8, 0, bytes.data(), static_cast<int>(read), nullptr, 0);
    if (wlen <= 0)
        return false;
    out.resize(wlen);
    MultiByteToWideChar(CP_UTF8, 0, bytes.data(), static_cast<int>(read), out.data(), wlen);
    return true;
}

// Helper: get %LocalAppData%\Microsoft\PowerToys\MousePointerCrosshairs\settings.json
static std::wstring GetCrosshairsSettingsPath()
{
    PWSTR lad = nullptr;
    std::wstring path;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &lad)) && lad)
    {
        path.assign(lad);
        CoTaskMemFree(lad);
    }
    if (!path.empty() && path.back() != L'\\')
    {
        path.push_back(L'\\');
    }
    path.append(L"Microsoft\\PowerToys\\MousePointerCrosshairs\\settings.json");
    return path;
}

// Helper: directory of the settings file
static std::wstring GetDirectoryFromPath(const std::wstring& file)
{
    auto pos = file.find_last_of(L"\\/");
    if (pos == std::wstring::npos)
        return L"";
    return file.substr(0, pos);
}

// Helper: parse #RRGGBB string
static bool ParseRgbHex(const std::wstring& hex, uint8_t& r, uint8_t& g, uint8_t& b)
{
    std::wstring s = hex;
    if (!s.empty() && s[0] == L'#')
        s = s.substr(1);
    if (s.size() != 6)
        return false;
    unsigned int ri = 0, gi = 0, bi = 0;
    if (swscanf_s(s.substr(0, 2).c_str(), L"%02x", &ri) != 1) return false;
    if (swscanf_s(s.substr(2, 2).c_str(), L"%02x", &gi) != 1) return false;
    if (swscanf_s(s.substr(4, 2).c_str(), L"%02x", &bi) != 1) return false;
    r = static_cast<uint8_t>(ri);
    g = static_cast<uint8_t>(gi);
    b = static_cast<uint8_t>(bi);
    return true;
}

// Helper: compute desired overlay rects (physical and logical)
struct OverlayDesiredRects { RECT physical{}; RECT logical{}; UINT dpi{ 96 }; float scale{ 1.0f }; };

static BOOL CALLBACK EnumMonProc(HMONITOR /*hMon*/, HDC /*hdc*/, LPRECT lprc, LPARAM lParam)
{
    if (!lprc)
        return TRUE;
    RECT* pUnion = reinterpret_cast<RECT*>(lParam);
    if (IsRectEmpty(pUnion))
    {
        *pUnion = *lprc;
    }
    else
    {
        RECT tmp = *pUnion;
        tmp.left = min(tmp.left, lprc->left);
        tmp.top = min(tmp.top, lprc->top);
        tmp.right = max(tmp.right, lprc->right);
        tmp.bottom = max(tmp.bottom, lprc->bottom);
        *pUnion = tmp;
    }
    return TRUE;
}

static OverlayDesiredRects ComputeDesiredOverlayRects(HWND hwnd)
{
    LogDpiState(L"ComputeDesiredOverlayRects: DPI state", hwnd);

    OverlayDesiredRects out{};
    RECT unionPhysical{}; // starts empty
    EnumDisplayMonitors(nullptr, nullptr, EnumMonProc, reinterpret_cast<LPARAM>(&unionPhysical));
    // IMPORTANT: Do NOT inset by 1px; it causes client/screen coordinate offset and misalignment
    out.physical = unionPhysical;

    // Convert to logical for this window using its current DPI
    UINT dpi = hwnd ? GetDpiForWindow(hwnd) : GetDpiForSystem();
    if (dpi == 0)
    {
        dpi = 96;
    }
    out.dpi = dpi;
    out.scale = static_cast<float>(dpi) / 96.0f;
    auto toLogical = [dpi](int px) { return MulDiv(px, 96, static_cast<int>(dpi)); };

    RECT logical{};
    logical.left = toLogical(unionPhysical.left);
    logical.top = toLogical(unionPhysical.top);
    logical.right = toLogical(unionPhysical.right);
    logical.bottom = toLogical(unionPhysical.bottom);
    out.logical = logical;

#ifdef LOG_UI_DIAG
    UiLog(L"ComputeDesiredOverlayRects: phys=[%ld,%ld %ldx%ld] logical=[%ld,%ld %ldx%ld] dpi=%u scale=%.3f",
          out.physical.left,
          out.physical.top,
          out.physical.right - out.physical.left,
          out.physical.bottom - out.physical.top,
          out.logical.left,
          out.logical.top,
          out.logical.right - out.logical.left,
          out.logical.bottom - out.logical.top,
          out.dpi,
          out.scale);
#endif

    return out;
}

// Resize overlay window to virtual desktop only when needed
static void ResizeOverlayToVirtualDesktopInset(HWND hwnd)
{
    if (!hwnd)
        return;

    LogDpiState(L"ResizeOverlayToVirtualDesktopInset: DPI state", hwnd);

    const auto desired = ComputeDesiredOverlayRects(hwnd);

    // Apply 1px inset hack here so we don't thrash size on every mouse move
    const int desX = desired.physical.left + 1;
    const int desY = desired.physical.top + 1;
    const int desW = (desired.physical.right - desired.physical.left) - 2;
    const int desH = (desired.physical.bottom - desired.physical.top) - 2;

    RECT wr{};
    if (GetWindowRect(hwnd, &wr))
    {
        const int curW = wr.right - wr.left;
        const int curH = wr.bottom - wr.top;
#ifdef LOG_UI_DIAG
        UiLog(L"ResizeOverlayToVirtualDesktopInset cur(phys)=[%ld,%ld %ldx%ld] desiredInset(phys)=[%d,%d %dx%d] dpi=%u",
              wr.left, wr.top, curW, curH, desX, desY, desW, desH, desired.dpi);
#endif

        if (wr.left != desX || wr.top != desY || curW != desW || curH != desH)
        {
#ifdef LOG_UI_DIAG
            UiLog(L"ResizeOverlayToVirtualDesktopInset SetWindowPos -> [%d,%d %dx%d]", desX, desY, desW, desH);
#endif
            SetWindowPos(hwnd, HWND_TOPMOST, desX, desY, desW, desH, SWP_NOACTIVATE);
        }
        else
        {
            // Make sure we stay on top without changing size/position
            SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        }
    }
}

Overlay* Overlay::s_instance = nullptr;

Overlay::Overlay(HINSTANCE hInstance) noexcept : m_hinstance(hInstance) {}
Overlay::~Overlay() {}

bool Overlay::Initialize()
{
    s_instance = this;
    UiLog(L"MousePointerCrosshairsUI - Overlay::Initialize");

    // Cache settings file path/dir
    m_settingsPath = GetCrosshairsSettingsPath();
    m_settingsDir = GetDirectoryFromPath(m_settingsPath);

    WNDCLASS wc{};
#ifdef LOG_UI_DIAG
    UiLog(L"Initialize: hInstance=0x%p", this->m_hinstance);
#endif
    m_hinstance = this->m_hinstance;

#ifdef LOG_UI_DIAG
    UiLog(L"hInstance - 0x%lx", this->m_hinstance);
#endif

    SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if (!GetClassInfoW(this->m_hinstance, m_className, &wc))
    {
        wc.lpfnWndProc = Overlay::WndProc;
        wc.hInstance = this->m_hinstance;
        wc.hIcon = LoadIcon(this->m_hinstance, IDI_APPLICATION);
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(NULL_BRUSH));
        wc.lpszClassName = m_className;

        if (!RegisterClassW(&wc))
        {
#ifdef LOG_UI_DIAG
            UiLog(L"RegisterClassW failed gle=%lu", GetLastError());
#endif
        }
    }
    // Register window class if needed (use WNDCLASSEXW pattern)
    WNDCLASSEXW info{}; info.cbSize = sizeof(info);
    BOOL classExists = GetClassInfoExW(this->m_hinstance, m_className, &info);

#ifdef LOG_UI_DIAG
    UiLog(L"GetClassInfoExW('%s') exists=%d gle=%lu", m_className, classExists, GetLastError());
#endif

    if (!classExists)
    {
        WNDCLASSEXW reg{};
        reg.cbSize = sizeof(WNDCLASSEXW);
        reg.style = CS_HREDRAW | CS_VREDRAW;
        reg.lpfnWndProc = Overlay::WndProc;
        reg.hInstance = this->m_hinstance;
        reg.hIcon = LoadIconW(this->m_hinstance, IDI_APPLICATION);
        reg.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        reg.hbrBackground = static_cast<HBRUSH>(GetStockObject(NULL_BRUSH));
        reg.lpszClassName = m_className;
        reg.hIconSm = LoadIconW(this->m_hinstance, IDI_APPLICATION);
        SetLastError(0);
        ATOM atom = RegisterClassExW(&reg);

#ifdef LOG_UI_DIAG
        UiLog(L"RegisterClassExW('%s') atom=%hu gle=%lu", m_className, atom, GetLastError());
#endif
        if (!atom)
        {
            return false;
        }
    }

    m_hwndOwner = CreateWindow(L"static", nullptr, WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, this->m_hinstance, nullptr);
    // Extended style
    DWORD exStyle = WS_EX_TRANSPARENT | WS_EX_LAYERED | WS_EX_NOREDIRECTIONBITMAP | WS_EX_TOOLWINDOW;

    // Create the overlay at the final virtual-desktop size to avoid CW_USEDEFAULT/0 ambiguity
    const int vx = GetSystemMetrics(SM_XVIRTUALSCREEN) + 1;
    const int vy = GetSystemMetrics(SM_YVIRTUALSCREEN) + 1;
    const int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN) - 2;
    const int vh = GetSystemMetrics(SM_CYVIRTUALSCREEN) - 2;

    // Before creating our window, check if a window with the same title already exists
    HWND existing = FindWindowW(nullptr, m_windowTitle);
    if (existing)
    {
#ifdef LOG_UI_DIAG
        UiLog(L"Existing window with same title found: hwnd=%p", existing);
#endif
    }
    else
    {
#ifdef LOG_UI_DIAG
        UiLog(L"No existing window with title '%s' found", m_windowTitle);
#endif
    }

    // Ensure GetLastError reflects CreateWindowExW outcome
    SetLastError(0);
    m_hwnd = CreateWindowExW(
        exStyle,
        m_className,
        m_windowTitle,
        WS_POPUP,
        vx,
        vy,
        vw,
        vh,
        nullptr,
        nullptr,
        this->m_hinstance,
        nullptr);

    if (!m_hwnd)
    {
#ifdef LOG_UI_DIAG
        UiLog(L"CreateWindowExW failed gle=%lu", GetLastError());
#endif
        // Extra check: ensure class still exists and log
        WNDCLASSEXW check{}; check.cbSize = sizeof(check);
        BOOL existsNow = GetClassInfoExW(this->m_hinstance, m_className, &check);
#ifdef LOG_UI_DIAG
        UiLog(L"Post-fail GetClassInfoExW exists=%d gle=%lu", existsNow, GetLastError());
#endif
        return false;
    }
#ifdef LOG_UI_DIAG
    UiLog(L"WM_CREATE hwnd=%p", m_hwnd);
    UiLog(L"Overlay hwnd=%p", m_hwnd);
#endif
    // Size overlay to virtual desktop before any draw (should already match from CreateWindowExW)
    SetWindowPos(m_hwnd, HWND_TOPMOST, vx, vy, vw, vh, 0);
    // Size overlay to virtual desktop before any draw
    ResizeOverlayToVirtualDesktopInset(m_hwnd);
    RECT wr{};
    GetWindowRect(m_hwnd, &wr);

#ifdef LOG_UI_DIAG
    UiLog(L"After SetWindowPos: win=[%ld,%ld %ldx%ld]", wr.left, wr.top, (wr.right - wr.left), (wr.bottom - wr.top));
    UiLog(L"After initial resize: win=[%ld,%ld %ldx%ld]", wr.left, wr.top, (wr.right - wr.left), (wr.bottom - wr.top));
#endif
    // Do NOT show the window here. Keep hidden until StartDrawing() is called.
    // ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);
    // UpdateWindow(m_hwnd);
#if LOG_SETTINGS_DIAG
    UiLog(L"[Settings] UI initialized. Loading settings from file and starting watcher...");
#endif

#ifdef COMPOSITION
    if (!CreateComposition())
    {
#ifdef LOG_UI_DIAG
        UiLog(L"CreateComposition failed");
#endif
        return false;
    }
#ifdef LOG_UI_DIAG
    UiLog(L"DesktopWindowTarget created and composition tree built");
#endif
#endif

    // Load settings immediately and apply
    LoadSettingsFromFile(true);
    // Start settings file watcher
    StartSettingsWatcher();

    return true;
}

void Overlay::Terminate()
{
#ifdef LOG_UI_DIAG
    UiLog(L"Terminate: DestroyWindow owner=%p hwnd=%p", m_hwndOwner, m_hwnd);
    UiLog(L"Terminate: hwnd=%p", m_hwnd);
#endif

    StopSettingsWatcher();

    if (m_mouseHook)
    {
        UnhookWindowsHookEx(m_mouseHook);
        m_mouseHook = nullptr;
    }

    if (m_hwnd)
    {
        KillTimer(m_hwnd, AUTO_HIDE_TIMER_ID);
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
    if (m_hwndOwner)
    {
        DestroyWindow(m_hwndOwner);
        m_hwndOwner = nullptr;
        m_hwnd = nullptr;
    }
}

void Overlay::Switch()
{
    PostMessage(m_hwnd, WM_SWITCH_ACTIVATION_MODE, 0, 0);
}

void Overlay::EnsureOn()
{
    PostMessage(m_hwnd, WM_ENSURE_ON, 0, 0);
}

void Overlay::EnsureOff()
{
    PostMessage(m_hwnd, WM_ENSURE_OFF, 0, 0);
}

void Overlay::RequestUpdatePosition()
{
    PostMessage(m_hwnd, WM_REQUEST_UPDATE, 0, 0);
}

void Overlay::SetExternalControl(bool enabled)
{
#ifdef LOG_UI_DIAG
    UiLog(L"SetExternalControl enabled=%d", static_cast<int>(enabled));
#endif
    m_externalControl = enabled;
    if (enabled && m_mouseHook)
    {
        UnhookWindowsHookEx(m_mouseHook);
        m_mouseHook = nullptr;
    }
    else if (!enabled && m_drawing && !m_mouseHook)
    {
        m_mouseHook = SetWindowsHookEx(WH_MOUSE_LL, [](int nCode, WPARAM wParam, LPARAM lParam)->LRESULT {
            if (nCode >= 0 && wParam == WM_MOUSEMOVE && Overlay::s_instance && !Overlay::s_instance->m_externalControl)
            {
                PostMessage(Overlay::s_instance->m_hwnd, WM_REQUEST_UPDATE, 0, 0);
            }
            return CallNextHookEx(0, nCode, wParam, lParam);
        }, m_hinstance, 0);
#ifdef LOG_UI_DIAG
        UiLog(L"SetWindowsHookEx LL result=%p gle=%lu", m_mouseHook, GetLastError());
#endif
    }
}

#ifdef COMPOSITION
bool Overlay::CreateComposition()
{
    DispatcherQueueOptions options = { sizeof(options), DQTYPE_THREAD_CURRENT, DQTAT_COM_ASTA };
    ABI::IDispatcherQueueController* controller;
    winrt::check_hresult(CreateDispatcherQueueController(options, &controller));
    *winrt::put_abi(m_dispatcherQueueController) = controller;

    m_compositor = winrt::Compositor();
    ABI::IDesktopWindowTarget* target;
    winrt::check_hresult(m_compositor.as<ABI::ICompositorDesktopInterop>()->CreateDesktopWindowTarget(m_hwnd, false, &target));
    *winrt::put_abi(m_target) = target;

    m_root = m_compositor.CreateContainerVisual();
    m_root.RelativeSizeAdjustment({ 1.0f, 1.0f });
    m_target.Root(m_root);
    m_root.Opacity(m_crosshairs_opacity);

    m_crosshairs_border_layer = m_compositor.CreateLayerVisual();
    m_crosshairs_border_layer.RelativeSizeAdjustment({ 1.0f, 1.0f });
    m_root.Children().InsertAtTop(m_crosshairs_border_layer);
    m_crosshairs_border_layer.Opacity(1.0f);

    m_crosshairs_layer = m_compositor.CreateLayerVisual();
    m_crosshairs_layer.RelativeSizeAdjustment({ 1.0f, 1.0f });

    m_left_crosshairs_border = m_compositor.CreateSpriteVisual();
    m_left_crosshairs_border.AnchorPoint({ 1.0f, 0.5f });
    m_left_crosshairs_border.Brush(m_compositor.CreateColorBrush(m_crosshairs_border_color));
    m_crosshairs_border_layer.Children().InsertAtTop(m_left_crosshairs_border);
    m_left_crosshairs = m_compositor.CreateSpriteVisual();
    m_left_crosshairs.AnchorPoint({ 1.0f, 0.5f });
    m_left_crosshairs.Brush(m_compositor.CreateColorBrush(m_crosshairs_color));
    m_crosshairs_layer.Children().InsertAtTop(m_left_crosshairs);

    m_right_crosshairs_border = m_compositor.CreateSpriteVisual();
    m_right_crosshairs_border.AnchorPoint({ 0.0f, 0.5f });
    m_right_crosshairs_border.Brush(m_compositor.CreateColorBrush(m_crosshairs_border_color));
    m_crosshairs_border_layer.Children().InsertAtTop(m_right_crosshairs_border);
    m_right_crosshairs = m_compositor.CreateSpriteVisual();
    m_right_crosshairs.AnchorPoint({ 0.0f, 0.5f });
    m_right_crosshairs.Brush(m_compositor.CreateColorBrush(m_crosshairs_color));
    m_crosshairs_layer.Children().InsertAtTop(m_right_crosshairs);

    m_top_crosshairs_border = m_compositor.CreateSpriteVisual();
    m_top_crosshairs_border.AnchorPoint({ 0.5f, 1.0f });
    m_top_crosshairs_border.Brush(m_compositor.CreateColorBrush(m_crosshairs_border_color));
    m_crosshairs_border_layer.Children().InsertAtTop(m_top_crosshairs_border);
    m_top_crosshairs = m_compositor.CreateSpriteVisual();
    m_top_crosshairs.AnchorPoint({ 0.5f, 1.0f });
    m_top_crosshairs.Brush(m_compositor.CreateColorBrush(m_crosshairs_color));
    m_crosshairs_layer.Children().InsertAtTop(m_top_crosshairs);

    m_bottom_crosshairs_border = m_compositor.CreateSpriteVisual();
    m_bottom_crosshairs_border.AnchorPoint({ 0.5f, 0.0f });
    m_bottom_crosshairs_border.Brush(m_compositor.CreateColorBrush(m_crosshairs_border_color));
    m_crosshairs_border_layer.Children().InsertAtTop(m_bottom_crosshairs_border);
    m_bottom_crosshairs = m_compositor.CreateSpriteVisual();
    m_bottom_crosshairs.AnchorPoint({ 0.5f, 0.0f });
    m_bottom_crosshairs.Brush(m_compositor.CreateColorBrush(m_crosshairs_color));
    m_crosshairs_layer.Children().InsertAtTop(m_bottom_crosshairs);

    m_crosshairs_border_layer.Children().InsertAtTop(m_crosshairs_layer);
    m_crosshairs_layer.Opacity(1.0f);

    return true;
}
#endif

void Overlay::UpdateCrosshairsPosition()
{
    LogDpiState(L"UpdateCrosshairsPosition: DPI state (entry)", m_hwnd);

    static RECT s_lastDesiredPhysical{};

    // Ensure window is correctly sized only if needed
    ResizeOverlayToVirtualDesktopInset(m_hwnd);

    POINT ptCursorScreen{};
    POINT ptCursor{};

    // Use gliding position when gliding is active, otherwise read the real cursor
    if (m_glidingActive)
    {
        ptCursorScreen.x = m_glideX;
        ptCursorScreen.y = m_glideY;
    }
    else
    {
        GetCursorPos(&ptCursorScreen);
    }
    ptCursor = ptCursorScreen;

    const auto desired = ComputeDesiredOverlayRects(m_hwnd);
    if (memcmp(&s_lastDesiredPhysical, &desired.physical, sizeof(RECT)) != 0)
    {
#ifdef LOG_UI_DIAG
        UiLog(L"Desired physical changed since last: prev=[%ld,%ld %ldx%ld] -> curr=[%ld,%ld %ldx%ld]",
              s_lastDesiredPhysical.left, s_lastDesiredPhysical.top,
              s_lastDesiredPhysical.right - s_lastDesiredPhysical.left,
              s_lastDesiredPhysical.bottom - s_lastDesiredPhysical.top,
              desired.physical.left, desired.physical.top,
              desired.physical.right - desired.physical.left,
              desired.physical.bottom - desired.physical.top);
#endif
        s_lastDesiredPhysical = desired.physical;
    }

#ifdef LOG_UI_DIAG
    UiLog(L"UpdateCrosshairsPosition: desired rect (phys) [%ld,%ld %ldx%ld]",
          desired.physical.left, desired.physical.top,
          desired.physical.right - desired.physical.left,
          desired.physical.bottom - desired.physical.top);
#endif

    RECT wr{}; GetWindowRect(m_hwnd, &wr);

#ifdef LOG_UI_DIAG
    UiLog(L"UpdateCrosshairsPosition: current win(phys)=[%ld,%ld %ldx%ld]",
          wr.left, wr.top, wr.right - wr.left, wr.bottom - wr.top);
#endif

    HMONITOR cursorMonitor = MonitorFromPoint(ptCursorScreen, MONITOR_DEFAULTTONEAREST);

    if (cursorMonitor == NULL)
    {
#ifdef LOG_UI_DIAG
        UiLog(L"UpdateCrosshairsPosition: MonitorFromPoint returned NULL");
#endif
        return;
    }

    MONITORINFO monitorInfo; monitorInfo.cbSize = sizeof(monitorInfo);

    if (!GetMonitorInfo(cursorMonitor, &monitorInfo))
    {
#ifdef LOG_UI_DIAG
        UiLog(L"UpdateCrosshairsPosition: GetMonitorInfo failed gle=%lu", GetLastError());
#endif
        return;
    }

#ifdef LOG_UI_DIAG
    UiLog(L"Monitor rc=[%ld,%ld %ldx%ld]", monitorInfo.rcMonitor.left, monitorInfo.rcMonitor.top,
          monitorInfo.rcMonitor.right - monitorInfo.rcMonitor.left, monitorInfo.rcMonitor.bottom - monitorInfo.rcMonitor.top);
#endif

    POINT ptMonitorUpperLeft{ monitorInfo.rcMonitor.left, monitorInfo.rcMonitor.top };
    POINT ptMonitorBottomRight{ monitorInfo.rcMonitor.right, monitorInfo.rcMonitor.bottom };

    // Convert everything to client coordinates.
    ScreenToClient(m_hwnd, &ptCursor);
    ScreenToClient(m_hwnd, &ptMonitorUpperLeft);
    ScreenToClient(m_hwnd, &ptMonitorBottomRight);

#ifdef LOG_UI_DIAG
    UiLog(L"Client cursor=[%ld,%ld] monUL=[%ld,%ld] monBR=[%ld,%ld]", ptCursor.x, ptCursor.y, ptMonitorUpperLeft.x, ptMonitorUpperLeft.y, ptMonitorBottomRight.x, ptMonitorBottomRight.y);
#endif

    // Crosshair position should receive a minor adjustment for odd values to prevent anti-aliasing due to half pixels, while still looking like it's centered around the mouse pointer.
    float halfPixelAdjustment = m_crosshairs_thickness % 2 == 1 ? 0.5f : 0.0f;
    float borderSizePadding = m_crosshairs_border_size * 2.f;

#ifdef COMPOSITION
    {
        float leftCrosshairsFullScreenLength = ptCursor.x - ptMonitorUpperLeft.x - m_crosshairs_radius + halfPixelAdjustment * 2.f;
        float leftCrosshairsLength = m_crosshairs_is_fixed_length_enabled ? m_crosshairs_fixed_length : leftCrosshairsFullScreenLength;
        float leftCrosshairsBorderLength = m_crosshairs_is_fixed_length_enabled ? m_crosshairs_fixed_length + borderSizePadding : leftCrosshairsFullScreenLength + m_crosshairs_border_size;
        m_left_crosshairs_border.Offset({ ptCursor.x - m_crosshairs_radius + m_crosshairs_border_size + halfPixelAdjustment * 2.f, ptCursor.y + halfPixelAdjustment, .0f });
        m_left_crosshairs_border.Size({ leftCrosshairsBorderLength, m_crosshairs_thickness + borderSizePadding });
        m_left_crosshairs.Offset({ ptCursor.x - m_crosshairs_radius + halfPixelAdjustment * 2.f, ptCursor.y + halfPixelAdjustment, .0f });
        m_left_crosshairs.Size({ leftCrosshairsLength, static_cast<float>(m_crosshairs_thickness) });
    }

    {
        float rightCrosshairsFullScreenLength = static_cast<float>(ptMonitorBottomRight.x) - ptCursor.x - m_crosshairs_radius;
        float rightCrosshairsLength = m_crosshairs_is_fixed_length_enabled ? m_crosshairs_fixed_length : rightCrosshairsFullScreenLength;
        float rightCrosshairsBorderLength = m_crosshairs_is_fixed_length_enabled ? m_crosshairs_fixed_length + borderSizePadding : rightCrosshairsFullScreenLength + m_crosshairs_border_size;
        m_right_crosshairs_border.Offset({ static_cast<float>(ptCursor.x) + m_crosshairs_radius - m_crosshairs_border_size, ptCursor.y + halfPixelAdjustment, .0f });
        m_right_crosshairs_border.Size({ rightCrosshairsBorderLength, m_crosshairs_thickness + borderSizePadding });
        m_right_crosshairs.Offset({ static_cast<float>(ptCursor.x) + m_crosshairs_radius, ptCursor.y + halfPixelAdjustment, .0f });
        m_right_crosshairs.Size({ rightCrosshairsLength, static_cast<float>(m_crosshairs_thickness) });
    }

    {
        float topCrosshairsFullScreenLength = ptCursor.y - ptMonitorUpperLeft.y - m_crosshairs_radius + halfPixelAdjustment * 2.f;
        float topCrosshairsLength = m_crosshairs_is_fixed_length_enabled ? m_crosshairs_fixed_length : topCrosshairsFullScreenLength;
        float topCrosshairsBorderLength = m_crosshairs_is_fixed_length_enabled ? m_crosshairs_fixed_length + borderSizePadding : topCrosshairsFullScreenLength + m_crosshairs_border_size;
        m_top_crosshairs_border.Offset({ ptCursor.x + halfPixelAdjustment, ptCursor.y - m_crosshairs_radius + m_crosshairs_border_size + halfPixelAdjustment * 2.f, .0f });
        m_top_crosshairs_border.Size({ m_crosshairs_thickness + borderSizePadding, topCrosshairsBorderLength });
        m_top_crosshairs.Offset({ ptCursor.x + halfPixelAdjustment, ptCursor.y - m_crosshairs_radius + halfPixelAdjustment * 2.f, .0f });
        m_top_crosshairs.Size({ static_cast<float>(m_crosshairs_thickness), topCrosshairsLength });
    }

    {
        float bottomCrosshairsFullScreenLength = static_cast<float>(ptMonitorBottomRight.y) - ptCursor.y - m_crosshairs_radius;
        float bottomCrosshairsLength = m_crosshairs_is_fixed_length_enabled ? m_crosshairs_fixed_length : bottomCrosshairsFullScreenLength;
        float bottomCrosshairsBorderLength = m_crosshairs_is_fixed_length_enabled ? m_crosshairs_fixed_length + borderSizePadding : bottomCrosshairsFullScreenLength + m_crosshairs_border_size;
        m_bottom_crosshairs_border.Offset({ ptCursor.x + halfPixelAdjustment, static_cast<float>(ptCursor.y) + m_crosshairs_radius - m_crosshairs_border_size, .0f });
        m_bottom_crosshairs_border.Size({ m_crosshairs_thickness + borderSizePadding, bottomCrosshairsBorderLength });
        m_bottom_crosshairs.Offset({ ptCursor.x + halfPixelAdjustment, static_cast<float>(ptCursor.y) + m_crosshairs_radius, .0f });
        m_bottom_crosshairs.Size({ static_cast<float>(m_crosshairs_thickness), bottomCrosshairsLength });
    }
#endif // COMPOSITION
}

void Overlay::StartDrawing()
{
#ifdef LOG_UI_DIAG
    UiLog(L"StartDrawing hwnd=%p", m_hwnd);
#endif
    LogDpiState(L"StartDrawing: DPI state", m_hwnd);
    // Ensure overlay rect is correct and z-order is topmost without changing size if already correct
    ResizeOverlayToVirtualDesktopInset(m_hwnd);
    UpdateCrosshairsPosition();
    m_hiddenCursor = false;

    if (m_crosshairs_auto_hide)
    {
        CURSORINFO cursorInfo{};
        cursorInfo.cbSize = sizeof(cursorInfo);
        if (GetCursorInfo(&cursorInfo))
        {
            m_hiddenCursor = (cursorInfo.flags & CURSOR_SHOWING) == 0;
        }
        SetAutoHideTimer();
    }

    if (!m_hiddenCursor)
    {
        ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);
    }

    m_drawing = true;
    m_mouseHook = SetWindowsHookEx(WH_MOUSE_LL, [](int nCode, WPARAM wParam, LPARAM lParam)->LRESULT {
        if (nCode >= 0 && wParam == WM_MOUSEMOVE && Overlay::s_instance && !Overlay::s_instance->m_externalControl)
        {
            // Avoid heavy work in hook; marshal to UI thread only
            PostMessage(Overlay::s_instance->m_hwnd, WM_REQUEST_UPDATE, 0, 0);
        }
        return CallNextHookEx(0, nCode, wParam, lParam);
    }, m_hinstance, 0);

#ifdef LOG_UI_DIAG
    UiLog(L"SetWindowsHookEx LL result=%p gle=%lu", m_mouseHook, GetLastError());
#endif
}

void Overlay::StopDrawing()
{
#ifdef LOG_UI_DIAG
    UiLog(L"StopDrawing hwnd=%p", m_hwnd);
#endif
    m_drawing = false;
    ShowWindow(m_hwnd, SW_HIDE);
    if (m_mouseHook)
    {
        UnhookWindowsHookEx(m_mouseHook);
        m_mouseHook = nullptr;
    }
    KillTimer(m_hwnd, AUTO_HIDE_TIMER_ID);
}

void Overlay::SetAutoHideTimer() noexcept
{
    SetTimer(m_hwnd, AUTO_HIDE_TIMER_ID, 1000, NULL);
}

void Overlay::ApplySettingsPayload(const CrosshairsSettingsPayload& p, bool applyToRuntime) noexcept
{
#if LOG_SETTINGS_DIAG
    static bool s_first = true;
    UiLog(L"[Settings] %s CrosshairsSettingsPayload applyToRuntime=%d", s_first ? L"First apply:" : L"Update:", applyToRuntime ? 1 : 0);
    s_first = false;
    UiLog(L"[Settings] color ARGB=(%u,%u,%u,%u) border ARGB=(%u,%u,%u,%u) radius=%d thick=%d opacity=%d borderSize=%d fixedLen=%d fixedEnabled=%d autoHide=%d travelSpeed=%d delaySpeed=%d",
          p.colorA, p.colorR, p.colorG, p.colorB, p.borderA, p.borderR, p.borderG, p.borderB, p.radius, p.thickness, p.opacity, p.borderSize, p.fixedLength, (int)p.isFixedLengthEnabled, (int)p.autoHide, p.glideTravelSpeed, p.glideDelaySpeed);
#endif

    m_crosshairs_color = { p.colorA, p.colorR, p.colorG, p.colorB };
    m_crosshairs_border_color = { p.borderA, p.borderR, p.borderG, p.borderB };
    m_crosshairs_radius = p.radius;
    m_crosshairs_thickness = p.thickness;
    m_crosshairs_opacity = (std::max)(0.f, (std::min)(1.f, static_cast<float>(p.opacity) / 100.0f));
    m_crosshairs_border_size = p.borderSize;
    m_crosshairs_is_fixed_length_enabled = p.isFixedLengthEnabled != 0;
    m_crosshairs_fixed_length = p.fixedLength;
    m_crosshairs_auto_hide = p.autoHide != 0;

    // Apply gliding speeds from settings
    if (p.glideTravelSpeed > 0) m_glideFastSpeed = p.glideTravelSpeed;
    if (p.glideDelaySpeed > 0) m_glideSlowSpeed = p.glideDelaySpeed;

#ifdef COMPOSITION
    if (applyToRuntime)
    {
        if (m_left_crosshairs && m_right_crosshairs && m_top_crosshairs && m_bottom_crosshairs)
        {
            m_left_crosshairs.Brush().as<winrt::CompositionColorBrush>().Color(m_crosshairs_color);
            m_right_crosshairs.Brush().as<winrt::CompositionColorBrush>().Color(m_crosshairs_color);
            m_top_crosshairs.Brush().as<winrt::CompositionColorBrush>().Color(m_crosshairs_color);
            m_bottom_crosshairs.Brush().as<winrt::CompositionColorBrush>().Color(m_crosshairs_color);

            m_left_crosshairs_border.Brush().as<winrt::CompositionColorBrush>().Color(m_crosshairs_border_color);
            m_right_crosshairs_border.Brush().as<winrt::CompositionColorBrush>().Color(m_crosshairs_border_color);
            m_top_crosshairs_border.Brush().as<winrt::CompositionColorBrush>().Color(m_crosshairs_border_color);
            m_bottom_crosshairs_border.Brush().as<winrt::CompositionColorBrush>().Color(m_crosshairs_border_color);

            m_root.Opacity(m_crosshairs_opacity);
        }
        UpdateCrosshairsPosition();
    }
#else
    (void)applyToRuntime;
#endif // COMPOSITION
}

// New: load settings from settings.json and apply
void Overlay::LoadSettingsFromFile(bool applyToRuntime) noexcept
{
    const std::wstring path = m_settingsPath;
    std::wstring jsonText;
    if (!ReadFileUtf8(path, jsonText))
    {
#ifdef LOG_SETTINGS_DIAG
        UiLog(L"[Settings] Failed to read settings file: %s", path.c_str());
#endif
        return;
    }

    try
    {
        auto root = winrt::Windows::Data::Json::JsonObject::Parse(winrt::hstring(jsonText));
        if (!root.HasKey(L"properties"))
            return;
        auto props = root.GetNamedObject(L"properties");

        auto getInt = [&](const wchar_t* name, int& out, bool* found = nullptr) {
            if (props.HasKey(name))
            {
                auto obj = props.GetNamedObject(name);
                if (obj.HasKey(L"value"))
                {
                    out = static_cast<int>(obj.GetNamedNumber(L"value"));
                    if (found) *found = true;
                }
            }
        };
        auto getBool = [&](const wchar_t* name, bool& out, bool* found = nullptr) {
            if (props.HasKey(name))
            {
                auto obj = props.GetNamedObject(name);
                if (obj.HasKey(L"value"))
                {
                    out = obj.GetNamedBoolean(L"value");
                    if (found) *found = true;
                }
            }
        };
        auto getRgb = [&](const wchar_t* name, uint8_t& r, uint8_t& g, uint8_t& b, bool* found = nullptr) {
            if (props.HasKey(name))
            {
                auto obj = props.GetNamedObject(name);
                if (obj.HasKey(L"value"))
                {
                    std::wstring s = obj.GetNamedString(L"value").c_str();
                    if (ParseRgbHex(s, r, g, b))
                    {
                        if (found) *found = true;
                    }
                }
            }
        };

        // Build payload
        CrosshairsSettingsPayload p{};
        p.colorA = 0xFF; p.borderA = 0xFF;
        uint8_t r = 255, g = 0, b = 0;
        uint8_t br = 255, bg = 255, bb = 255;
        getRgb(L"crosshairs_color", r, g, b, nullptr);
        getRgb(L"crosshairs_border_color", br, bg, bb, nullptr);
        p.colorR = r; p.colorG = g; p.colorB = b;
        p.borderR = br; p.borderG = bg; p.borderB = bb;
        getInt(L"crosshairs_radius", p.radius);
        getInt(L"crosshairs_thickness", p.thickness);
        getInt(L"crosshairs_opacity", p.opacity);
        getInt(L"crosshairs_border_size", p.borderSize);
        getInt(L"crosshairs_fixed_length", p.fixedLength);
        bool bval = false;
        getBool(L"crosshairs_is_fixed_length_enabled", bval);
        p.isFixedLengthEnabled = bval ? 1 : 0;
        bval = false;
        getBool(L"crosshairs_auto_hide", bval);
        p.autoHide = bval ? 1 : 0;
        // New: speeds
        getInt(L"gliding_travel_speed", p.glideTravelSpeed);
        getInt(L"gliding_delay_speed", p.glideDelaySpeed);

#ifdef LOG_SETTINGS_DIAG
        UiLog(L"[Settings] Loaded settings from file. Applying...");
#endif
        ApplySettingsPayload(p, applyToRuntime);
    }
    catch (...)
    {
#ifdef LOG_SETTINGS_DIAG
        UiLog(L"[Settings] Failed to parse settings JSON");
#endif
    }
}

void Overlay::StartSettingsWatcher()
{
    if (m_settingsWatcherThread)
        return;

    if (m_settingsDir.empty())
        return;

    m_settingsWatcherStopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!m_settingsWatcherStopEvent)
        return;

    // Launch thread
    m_settingsWatcherThread = (HANDLE)_beginthreadex(nullptr, 0, [](void* ctx)->unsigned {
        Overlay* self = reinterpret_cast<Overlay*>(ctx);
        const std::wstring dir = self->m_settingsDir;
        HANDLE hChange = FindFirstChangeNotificationW(dir.c_str(), FALSE, FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE);
        if (hChange == INVALID_HANDLE_VALUE || hChange == nullptr)
        {
            return 0u;
        }
        HANDLE handles[2] = { self->m_settingsWatcherStopEvent, hChange };
        bool running = true;
        while (running)
        {
            DWORD dw = WaitForMultipleObjects(2, handles, FALSE, INFINITE);
            if (dw == WAIT_OBJECT_0)
            {
                // stop
                running = false;
                break;
            }
            else if (dw == WAIT_OBJECT_0 + 1)
            {
                // Directory changed, throttle and notify UI thread
                Sleep(250);
                if (self->m_hwnd)
                {
                    PostMessage(self->m_hwnd, WM_RELOAD_SETTINGS, 0, 0);
                }
                FindNextChangeNotification(hChange);
            }
            else
            {
                // error
                running = false;
                break;
            }
        }
        FindCloseChangeNotification(hChange);
        return 0u;
    }, this, 0, nullptr);
}

void Overlay::StopSettingsWatcher()
{
    if (m_settingsWatcherStopEvent)
    {
        SetEvent(m_settingsWatcherStopEvent);
    }
    if (m_settingsWatcherThread)
    {
        WaitForSingleObject(m_settingsWatcherThread, 2000);
        CloseHandle(m_settingsWatcherThread);
        m_settingsWatcherThread = nullptr;
    }
    if (m_settingsWatcherStopEvent)
    {
        CloseHandle(m_settingsWatcherStopEvent);
        m_settingsWatcherStopEvent = nullptr;
    }
}

LRESULT CALLBACK Overlay::WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) noexcept
{
    switch (message)
    {
    case WM_NCCREATE:
        // Must return TRUE to continue creation; returning FALSE aborts CreateWindowExW
        return TRUE;
    case WM_CREATE:
#ifdef LOG_UI_DIAG
        UiLog(L"WM_CREATE hwnd=%p", hWnd);
#endif
        break;
    case WM_DISPLAYCHANGE:
        if (s_instance)
        {
#ifdef LOG_UI_DIAG
            UiLog(L"WM_DISPLAYCHANGE");
#endif
            LogDpiState(L"WM_DISPLAYCHANGE: DPI state", s_instance->m_hwnd);
            ResizeOverlayToVirtualDesktopInset(s_instance->m_hwnd);
        }
        break;
    case WM_DPICHANGED:
        if (s_instance)
        {
            RECT* suggested = reinterpret_cast<RECT*>(lParam);
#ifdef LOG_UI_DIAG
            UiLog(L"WM_DPICHANGED newSuggested=[%ld,%ld %ldx%ld] dpiForWindow=%u",
                  suggested ? suggested->left : 0L,
                  suggested ? suggested->top : 0L,
                  suggested ? (suggested->right - suggested->left) : 0L,
                  suggested ? (suggested->bottom - suggested->top) : 0L,
                  GetDpiForWindow(s_instance->m_hwnd));
#endif
            LogDpiState(L"WM_DPICHANGED: DPI state", s_instance->m_hwnd);
            ResizeOverlayToVirtualDesktopInset(s_instance->m_hwnd);
        }
        break;
    case WM_NCHITTEST:
        return HTTRANSPARENT;
    case WM_SWITCH_ACTIVATION_MODE:
#ifdef LOG_UI_DIAG
        UiLog(L"WM_SWITCH_ACTIVATION_MODE drawing=%d", s_instance ? static_cast<int>(s_instance->m_drawing) : 0);
#endif
        if (s_instance)
        {
            if (s_instance->m_drawing)
            {
                s_instance->StopDrawing();
            }
            else
            {
                s_instance->StartDrawing();
            }
        }
        break;
    case WM_ENSURE_ON:
#ifdef LOG_UI_DIAG
        UiLog(L"WM_ENSURE_ON");
#endif
        if (s_instance && !s_instance->m_drawing)
        {
            s_instance->StartDrawing();
        }
        break;
    case WM_ENSURE_OFF:
#ifdef LOG_UI_DIAG
        UiLog(L"WM_ENSURE_OFF");
#endif
        if (s_instance && s_instance->m_drawing)
        {
            s_instance->StopDrawing();
        }
        break;
    case WM_REQUEST_UPDATE:
        if (s_instance)
        {
            // Avoid spamming logs per-move
            s_instance->UpdateCrosshairsPosition();
        }
        break;
    case WM_TIMER:
        if (!s_instance)
            break;

        if (wParam == GLIDE_TIMER_ID && s_instance->m_glidingActive)
        {
            // Compute virtual desktop bounds
            int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
            int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
            int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
            int vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
            int right = vx + vw;
            int bottom = vy + vh;

            // Determine base speed (pixels per 200ms window like original)
            int baseSpeed = 0;
            switch (s_instance->m_glideStage)
            {
            case GlideStage::VerticalFast: baseSpeed = s_instance->m_glideFastSpeed; break;
            case GlideStage::VerticalSlow: baseSpeed = s_instance->m_glideSlowSpeed; break;
            case GlideStage::HorizontalFast: baseSpeed = s_instance->m_glideFastSpeed; break;
            case GlideStage::HorizontalSlow: baseSpeed = s_instance->m_glideSlowSpeed; break;
            default: baseSpeed = 0; break;
            }

            // Distribute movement over 10ms ticks to match pixels-per-base-window speeds
            // perTick = floor(baseSpeed * kTimerTickMs / kBaseSpeedTickMs) with fractional remainder accumulator
            const int num = baseSpeed * static_cast<int>(Overlay::kTimerTickMs);
            const int den = static_cast<int>(Overlay::kBaseSpeedTickMs);

            if (s_instance->m_glideStage == GlideStage::VerticalFast || s_instance->m_glideStage == GlideStage::VerticalSlow)
            {
                int move = num / den;
                s_instance->m_glideFracX += (num % den);
                if (s_instance->m_glideFracX >= den)
                {
                    move += 1;
                    s_instance->m_glideFracX -= den;
                }

                s_instance->m_glideX += move;
                if (s_instance->m_glideX >= right)
                {
                    s_instance->m_glideX = vx; // wrap to left edge
                }
            }
            else if (s_instance->m_glideStage == GlideStage::HorizontalFast || s_instance->m_glideStage == GlideStage::HorizontalSlow)
            {
                int move = num / den;
                s_instance->m_glideFracY += (num % den);
                if (s_instance->m_glideFracY >= den)
                {
                    move += 1;
                    s_instance->m_glideFracY -= den;
                }

                s_instance->m_glideY += move;
                if (s_instance->m_glideY >= bottom)
                {
                    s_instance->m_glideY = vy; // wrap to top edge
                }
            }

            // Update the crosshairs at the new glide position
            s_instance->UpdateCrosshairsPosition();
        }
        else if (wParam == AUTO_HIDE_TIMER_ID && s_instance->m_drawing)
        {
            CURSORINFO cursorInfo{};
            cursorInfo.cbSize = sizeof(cursorInfo);
            if (GetCursorInfo(&cursorInfo))
            {
                bool showing = (cursorInfo.flags & CURSOR_SHOWING) != 0;
#ifdef LOG_UI_DIAG
                UiLog(L"WM_TIMER auto-hide showing=%d hiddenCursor=%d", static_cast<int>(showing), static_cast<int>(s_instance->m_hiddenCursor));
#endif
                if (showing)
                {
                    if (s_instance->m_hiddenCursor)
                    {
                        s_instance->m_hiddenCursor = false;
                        ShowWindow(s_instance->m_hwnd, SW_SHOWNOACTIVATE);
                    }
                }
                else
                {
                    if (!s_instance->m_hiddenCursor)
                    {
                        s_instance->m_hiddenCursor = true;
                        ShowWindow(s_instance->m_hwnd, SW_HIDE);
                    }
                }
            }
        }
        break;
    case WM_RELOAD_SETTINGS:
        if (s_instance)
        {
#if LOG_SETTINGS_DIAG
            UiLog(L"[Settings] Reload requested via watcher");
#endif
            s_instance->LoadSettingsFromFile(true);
        }
        break;
    case WM_DESTROY:
#ifdef LOG_UI_DIAG
        UiLog(L"WM_DESTROY hwnd=%p", hWnd);
#endif
        break;
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

void Overlay::ToggleGlidingCursor()
{
    // Ensure window exists and is shown
    if (!m_drawing)
    {
        StartDrawing();
    }

    if (!m_glidingActive)
    {
        // Begin gliding state machine
        m_glidingActive = true;
        m_glideStage = GlideStage::VerticalFast;

        // Initialize start positions: X at left edge (0), Y at screen midpoint
        RECT vr{ GetSystemMetrics(SM_XVIRTUALSCREEN), GetSystemMetrics(SM_YVIRTUALSCREEN),
                 GetSystemMetrics(SM_XVIRTUALSCREEN) + GetSystemMetrics(SM_CXVIRTUALSCREEN),
                 GetSystemMetrics(SM_YVIRTUALSCREEN) + GetSystemMetrics(SM_CYVIRTUALSCREEN) };
        m_glideX = vr.left;
        m_glideY = (vr.top + vr.bottom) / 2;
        m_glidePosX = static_cast<float>(m_glideX);
        m_glidePosY = static_cast<float>(m_glideY);
        m_glideFracX = 0;
        m_glideFracY = 0;
        m_lastGlideTickMs = 0;

        // Disable mouse movement by ignoring move updates via external control flag
        SetExternalControl(true);

        // Start timer at 10ms tick
        SetTimer(m_hwnd, GLIDE_TIMER_ID, kTimerTickMs, nullptr);
        return;
    }

    // If already gliding, advance to next stage
    AdvanceGlideStage();
}

void Overlay::AdvanceGlideStage()
{
    switch (m_glideStage)
    {
    case GlideStage::VerticalFast:
        m_glideStage = GlideStage::VerticalSlow;
        // reset fraction so next phase starts cleanly
        m_glideFracX = 0; m_glideFracY = 0;
        break;
    case GlideStage::VerticalSlow:
    {
        // Move to horizontal fast: reset Y to top, X stays where chosen, start moving down
        RECT vr{ GetSystemMetrics(SM_XVIRTUALSCREEN), GetSystemMetrics(SM_YVIRTUALSCREEN),
                 GetSystemMetrics(SM_XVIRTUALSCREEN) + GetSystemMetrics(SM_CXVIRTUALSCREEN),
                 GetSystemMetrics(SM_YVIRTUALSCREEN) + GetSystemMetrics(SM_CYVIRTUALSCREEN) };
        m_glideY = vr.top; // top
        m_glideStage = GlideStage::HorizontalFast;
        m_glideFracX = 0; m_glideFracY = 0;
        break;
    }
    case GlideStage::HorizontalFast:
        m_glideStage = GlideStage::HorizontalSlow;
        m_glideFracX = 0; m_glideFracY = 0;
        break;
    case GlideStage::HorizontalSlow:
        m_glideStage = GlideStage::Menu;
        // Stop timer and show placeholder menu
        KillTimer(m_hwnd, GLIDE_TIMER_ID);
        MessageBoxW(m_hwnd, L"Gliding cursor selection menu (placeholder)", L"Mouse Pointer Crosshairs", MB_OK | MB_TOPMOST);
        // End gliding mode and turn off overlay
        StopGlide();
        StopDrawing();
        break;
    default:
        break;
    }
}

void Overlay::StartGlideIfNeeded()
{
    if (!m_glidingActive)
    {
        ToggleGlidingCursor();
    }
}

void Overlay::StopGlide()
{
    m_glidingActive = false;
    m_glideStage = GlideStage::None;
    m_glideFracX = 0;
    m_glideFracY = 0;
    KillTimer(m_hwnd, GLIDE_TIMER_ID);
    // Re-enable mouse tracking
    SetExternalControl(false);
}
