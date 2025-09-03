#pragma once
#include "pch.h"
#include <vector>
#include <string>

class GlideMenu
{
public:
    enum class Action : UINT
    {
        LeftClick = 1,
        RightClick = 2,
        DoubleClick = 3,
        Cancel = 4,
    };

    // Create and show a lightweight Win32 popup menu near screenPos. Posts WM_GLIDE_MENU_ACTION to notifyHwnd on commit.
    bool Create(HWND ownerHwnd, HINSTANCE hinst, POINT screenPos, HWND notifyHwnd) noexcept;
    void Destroy() noexcept;

    void Show() noexcept;
    void Hide() noexcept;

    // Cycle selection to next item.
    void Next() noexcept;

    // Commit current selection: posts message and hides the menu
    void CommitSelection() noexcept;

    // Returns true if menu is currently visible
    bool IsOpen() const noexcept { return m_hwnd != nullptr && IsWindowVisible(m_hwnd); }

    // Captured screen position of activation
    POINT Anchor() const noexcept { return m_anchor; }

    // Window plumbing
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) noexcept;
    static constexpr LPCWSTR kClassName = L"GlideMenuHostWindow";

private:
    void UpdateSelectionVisuals() noexcept; // triggers repaint
    void OnPaint() noexcept;

private:
    HWND m_owner{ nullptr };
    HWND m_hwnd{ nullptr };
    HWND m_notify{ nullptr };
    HINSTANCE m_hinst{ nullptr };
    POINT m_anchor{ 0, 0 };

    struct Row
    {
        std::wstring text;
        GlideMenu::Action action{ Action::LeftClick };
    };
    std::vector<Row> m_rows;
    int m_selected{ 0 };

    UINT m_cycleMs{ 700 };

    // Metrics
    int m_width{ 220 };
    int m_rowHeight{ 32 };
    int m_paddingX{ 12 };
    int m_paddingY{ 10 };
    COLORREF m_bg{ RGB(30,30,30) };
    COLORREF m_fg{ RGB(240,240,240) };
    COLORREF m_hiBg{ RGB(62,126,197) };
    COLORREF m_hiFg{ RGB(255,255,255) };

private:
    static constexpr UINT kTimerId = 9001;
};
