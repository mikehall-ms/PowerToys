#pragma once
#include "pch.h"

#ifdef COMPOSITION
namespace winrt
{
    using namespace winrt::Windows::System;
    using namespace winrt::Windows::UI::Composition;
}

namespace ABI
{
    using namespace ABI::Windows::System;
    using namespace ABI::Windows::UI::Composition::Desktop;
}
#endif

#include "GlideMenu.h"

// Settings payload used internally when applying settings
#pragma pack(push, 1)
struct CrosshairsSettingsPayload
{
    // Colors are ARGB
    uint8_t colorA, colorR, colorG, colorB;
    uint8_t borderA, borderR, borderG, borderB;
    int32_t radius;
    int32_t thickness;
    int32_t opacity;      // 0-100
    int32_t borderSize;
    int32_t fixedLength;
    uint8_t autoHide;             // bool
    uint8_t isFixedLengthEnabled; // bool
    uint8_t reserved[2]{};
    // Gliding speeds (pixels per base tick)
    int32_t glideTravelSpeed{ 25 }; // fast
    int32_t glideDelaySpeed{ 5 };   // slow
};
#pragma pack(pop)

class Overlay
{
public:
    explicit Overlay(HINSTANCE hInstance) noexcept;
    ~Overlay();

    // Lifecycle
    bool Initialize();
    void Terminate();

    // Controls (thread-safe via PostMessage)
    void Switch();
    void EnsureOn();
    void EnsureOff();
    void RequestUpdatePosition();
    void SetExternalControl(bool enabled);

    // New control: toggle gliding cursor state machine
    void ToggleGlidingCursor();

    // Message pump helper
    static LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) noexcept;

private:
    bool CreateComposition();
    void UpdateCrosshairsPosition();
    void StartDrawing();
    void StopDrawing();
    void SetAutoHideTimer() noexcept;

    // Settings helpers
    void ApplySettingsPayload(const CrosshairsSettingsPayload& p, bool applyToRuntime = true) noexcept;
    void LoadSettingsFromFile(bool applyToRuntime) noexcept;
    void StartSettingsWatcher();
    void StopSettingsWatcher();

    // Gliding helpers
    void StartGlideIfNeeded();
    void AdvanceGlideStage();
    void StopGlide();

private:
    // Settings
    winrt::Windows::UI::Color m_crosshairs_border_color{ 0xFF, 0x00, 0x00, 0x00 };
    winrt::Windows::UI::Color m_crosshairs_color{ 0xFF, 0xFF, 0x00, 0x00 };
    int m_crosshairs_radius{ 20 };
    int m_crosshairs_thickness{ 3 };
    int m_crosshairs_border_size{ 1 };
    bool m_crosshairs_is_fixed_length_enabled{ false };
    int m_crosshairs_fixed_length{ 100 };
    float m_crosshairs_opacity{ 1.0f };
    bool m_crosshairs_auto_hide{ false };

    // Settings file paths and watcher
    std::wstring m_settingsPath{};
    std::wstring m_settingsDir{};
    HANDLE m_settingsWatcherStopEvent{ nullptr };
    HANDLE m_settingsWatcherThread{ nullptr };

    // Gliding cursor state
    enum class GlideStage
    {
        None = 0,
        VerticalFast = 1,
        VerticalSlow = 2,
        HorizontalFast = 3,
        HorizontalSlow = 4,
        Menu = 5,
    };

    // Base window for speed (pixels per 200ms like original) and timer tick driving movement
    static constexpr UINT kBaseSpeedTickMs = 200;
    static constexpr UINT kTimerTickMs = 10;

    bool m_glidingActive{ false };
    GlideStage m_glideStage{ GlideStage::None };
    int m_glideFastSpeed{ 25 }; // pixels per base tick
    int m_glideSlowSpeed{ 5 };  // pixels per base tick
    int m_glideX{ 0 };          // screen coords (rounded)
    int m_glideY{ 0 };          // screen coords (rounded)

    // Fractional remainder accumulators for distributing base-window pixels across 10ms ticks
    int m_glideFracX{ 0 };
    int m_glideFracY{ 0 };

    // Legacy high-precision fields (kept for potential future use)
    float m_glidePosX{ 0.f };
    float m_glidePosY{ 0.f };
    DWORD m_lastGlideTickMs{ 0 };

    // Menu integration
    std::unique_ptr<GlideMenu> m_menu;
    POINT m_menuAnchor{ 0, 0 };
    bool m_overlayHiddenForMenu{ false };

private:
    static constexpr auto m_className = L"MousePointerCrosshairsUIOverlay";
    static constexpr auto m_windowTitle = L"PowerToys Mouse Pointer Crosshairs UI";
    static constexpr DWORD AUTO_HIDE_TIMER_ID = 101;
    static constexpr DWORD WM_SWITCH_ACTIVATION_MODE = WM_APP + 1;
    static constexpr DWORD WM_ENSURE_ON = WM_APP + 2;
    static constexpr DWORD WM_ENSURE_OFF = WM_APP + 3;
    static constexpr DWORD WM_REQUEST_UPDATE = WM_APP + 4;
    static constexpr DWORD WM_RELOAD_SETTINGS = WM_APP + 5;
    static constexpr DWORD WM_TOGGLE_GLIDE = WM_APP + 6;
    static constexpr DWORD GLIDE_TIMER_ID = 201;
    static constexpr DWORD WM_GLIDE_MENU_ACTION = WM_APP + 100;

    static Overlay* s_instance;

    HWND m_hwndOwner{ nullptr };
    HWND m_hwnd{ nullptr };
    HINSTANCE m_hinstance{ nullptr };

#ifdef COMPOSITION
    winrt::DispatcherQueueController m_dispatcherQueueController{ nullptr };
    winrt::Compositor m_compositor{ nullptr };
    winrt::Desktop::DesktopWindowTarget m_target{ nullptr };
    winrt::ContainerVisual m_root{ nullptr };
    winrt::LayerVisual m_crosshairs_border_layer{ nullptr };
    winrt::LayerVisual m_crosshairs_layer{ nullptr };
    winrt::SpriteVisual m_left_crosshairs_border{ nullptr };
    winrt::SpriteVisual m_left_crosshairs{ nullptr };
    winrt::SpriteVisual m_right_crosshairs_border{ nullptr };
    winrt::SpriteVisual m_right_crosshairs{ nullptr };
    winrt::SpriteVisual m_top_crosshairs_border{ nullptr };
    winrt::SpriteVisual m_top_crosshairs{ nullptr };
    winrt::SpriteVisual m_bottom_crosshairs_border{ nullptr };
    winrt::SpriteVisual m_bottom_crosshairs{ nullptr };
#endif

    HHOOK m_mouseHook{ nullptr };
    bool m_drawing{ false };
    bool m_destroyed{ false };
    bool m_hiddenCursor{ false };
    bool m_externalControl{ false };
};
