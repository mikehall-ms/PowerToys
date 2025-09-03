#include "pch.h"
#include "GlideMenu.h"
#include <shlobj.h>
#include <knownfolders.h>

#ifndef WM_GLIDE_MENU_ACTION
#define WM_GLIDE_MENU_ACTION (WM_APP + 100)
#endif

static inline int GM_GetX(LPARAM lp) { return static_cast<short>(LOWORD(lp)); }
static inline int GM_GetY(LPARAM lp) { return static_cast<short>(HIWORD(lp)); }

// --- Debug logging helpers ---
static std::wstring GM_LogPath()
{
    static std::wstring path;
    if (!path.empty()) return path;
    PWSTR docs = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &docs)) && docs)
    {
        path.assign(docs);
        CoTaskMemFree(docs);
    }
    if (!path.empty() && path.back() != L'\\') path.push_back(L'\\');
    path.append(L"mousepointer_glidemenu.log");
    return path;
}
static void GM_Log(const wchar_t* fmt, ...)
{
    wchar_t buffer[1024];
    va_list args; va_start(args, fmt);
    _vsnwprintf_s(buffer, _countof(buffer), _TRUNCATE, fmt, args);
    va_end(args);

    OutputDebugStringW(L"[GlideMenu] ");
    OutputDebugStringW(buffer);
    OutputDebugStringW(L"\r\n");

    const auto path = GM_LogPath();
    HANDLE h = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h != INVALID_HANDLE_VALUE)
    {
        DWORD written = 0; std::wstring line(buffer); line.append(L"\r\n");
        WriteFile(h, line.c_str(), static_cast<DWORD>(line.size() * sizeof(wchar_t)), &written, nullptr);
        CloseHandle(h);
    }
}

static GlideMenu* s_menuInst = nullptr;

LRESULT CALLBACK GlideMenu::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) noexcept
{
    switch (msg)
    {
    case WM_NCCREATE:
        GM_Log(L"WndProc WM_NCCREATE hwnd=%p", hwnd);
        return TRUE;
    case WM_CREATE:
        GM_Log(L"WndProc WM_CREATE hwnd=%p", hwnd);
        return 0;
    case WM_ACTIVATE:
        GM_Log(L"WndProc WM_ACTIVATE state=%u hwnd=%p", (unsigned)LOWORD(wParam), hwnd);
        return 0;
    case WM_SETFOCUS:
        GM_Log(L"WndProc WM_SETFOCUS hwnd=%p", hwnd);
        return 0;
    case WM_MOUSEACTIVATE:
        GM_Log(L"WndProc WM_MOUSEACTIVATE hwnd=%p", hwnd);
        return MA_ACTIVATE;
    case WM_SHOWWINDOW:
        GM_Log(L"WndProc WM_SHOWWINDOW shown=%d hwnd=%p", (int)wParam, hwnd);
        return 0;
    case WM_CAPTURECHANGED:
        GM_Log(L"WndProc WM_CAPTURECHANGED hwnd=%p newCapture=%p", hwnd, (HWND)lParam);
        return 0;
    case WM_TIMER:
        if (wParam == kTimerId && s_menuInst)
        {
            GM_Log(L"WndProc WM_TIMER cycle selection current=%d", s_menuInst->m_selected);
            s_menuInst->Next();
        }
        return 0;
    case WM_LBUTTONDOWN:
        GM_Log(L"WndProc WM_LBUTTONDOWN");
        SetCapture(hwnd);
        return 0;
    case WM_LBUTTONUP:
        GM_Log(L"WndProc WM_LBUTTONUP");
        if (s_menuInst)
        {
            int y = GM_GetY(lParam);
            int index = (y - s_menuInst->m_paddingY) / s_menuInst->m_rowHeight;
            if (index >= 0 && index < (int)s_menuInst->m_rows.size())
            {
                s_menuInst->m_selected = index;
                GM_Log(L"Mouse click commit index=%d", index);
                s_menuInst->CommitSelection();
            }
        }
        ReleaseCapture();
        return 0;
    case WM_RBUTTONDOWN:
        GM_Log(L"WndProc WM_RBUTTONDOWN");
        SetCapture(hwnd);
        return 0;
    case WM_RBUTTONUP:
        GM_Log(L"WndProc WM_RBUTTONUP -> Cancel");
        if (s_menuInst)
        {
            s_menuInst->m_selected = (int)s_menuInst->m_rows.size() - 1; // Cancel row
            s_menuInst->CommitSelection();
        }
        ReleaseCapture();
        return 0;
    case WM_MOUSEMOVE:
        if (s_menuInst)
        {
            int y = GM_GetY(lParam);
            int index = (y - s_menuInst->m_paddingY) / s_menuInst->m_rowHeight;
            if (index >= 0 && index < (int)s_menuInst->m_rows.size() && index != s_menuInst->m_selected)
            {
                s_menuInst->m_selected = index;
                GM_Log(L"WndProc WM_MOUSEMOVE highlight index=%d", index);
                s_menuInst->UpdateSelectionVisuals();
            }
        }
        return 0;
    case WM_KEYDOWN:
        GM_Log(L"WndProc WM_KEYDOWN vk=0x%02X", (unsigned)wParam);
        if (s_menuInst)
        {
            if (wParam == VK_RETURN)
            {
                s_menuInst->CommitSelection();
                return 0;
            }
            if (wParam == VK_ESCAPE)
            {
                s_menuInst->m_selected = (int)s_menuInst->m_rows.size() - 1; // Cancel
                s_menuInst->CommitSelection();
                return 0;
            }
            if (wParam == VK_UP)
            {
                s_menuInst->m_selected = (s_menuInst->m_selected - 1 + (int)s_menuInst->m_rows.size()) % (int)s_menuInst->m_rows.size();
                s_menuInst->UpdateSelectionVisuals();
                return 0;
            }
            if (wParam == VK_DOWN)
            {
                s_menuInst->m_selected = (s_menuInst->m_selected + 1) % (int)s_menuInst->m_rows.size();
                s_menuInst->UpdateSelectionVisuals();
                return 0;
            }
        }
        break;
    case WM_PAINT:
        GM_Log(L"WndProc WM_PAINT hwnd=%p", hwnd);
        if (s_menuInst)
            s_menuInst->OnPaint();
        return 0;
    case WM_ERASEBKGND:
        return 1; // handled in WM_PAINT
    case WM_DESTROY:
        GM_Log(L"WndProc WM_DESTROY hwnd=%p", hwnd);
        return 0;
    default:
        break;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

static void EnsureWindowClass(HINSTANCE hinst)
{
    static bool s_reg = false;
    if (s_reg) return;
    WNDCLASSW wc{};
    wc.lpfnWndProc = GlideMenu::WndProc;
    wc.hInstance = hinst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW+1);
    wc.lpszClassName = GlideMenu::kClassName;
    SetLastError(0);
    ATOM atom = RegisterClassW(&wc);
    GM_Log(L"EnsureWindowClass RegisterClassW('%s') atom=%hu gle=%lu", GlideMenu::kClassName, atom, GetLastError());
    s_reg = true;
}

bool GlideMenu::Create(HWND ownerHwnd, HINSTANCE hinst, POINT screenPos, HWND notifyHwnd) noexcept
{
    GM_Log(L"Create owner=%p hinst=%p notify=%p at=[%ld,%ld]", ownerHwnd, hinst, notifyHwnd, screenPos.x, screenPos.y);

    m_owner = ownerHwnd;
    m_notify = notifyHwnd;
    m_hinst = hinst;
    m_anchor = screenPos;
    s_menuInst = this;

    // Build items before sizing
    m_rows.clear();
    m_rows.push_back({ L"< Left Click", Action::LeftClick });
    m_rows.push_back({ L"< Right Click", Action::RightClick });
    m_rows.push_back({ L"< Double Click", Action::DoubleClick });
    m_rows.push_back({ L"X Cancel", Action::Cancel });

    EnsureWindowClass(hinst);

    const int width = m_width;
    const int height = m_paddingY * 2 + static_cast<int>(m_rows.size()) * m_rowHeight;

    SetLastError(0);
    m_hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
                             kClassName, L"GlideMenu", WS_POPUP, screenPos.x, screenPos.y,
                             width, height,
                             nullptr, nullptr, hinst, nullptr);
    GM_Log(L"CreateWindowExW hwnd=%p gle=%lu", m_hwnd, GetLastError());
    if (!m_hwnd)
        return false;

    // Show and bring to foreground, activate, set focus
    BOOL sp = SetWindowPos(m_hwnd, HWND_TOPMOST, screenPos.x, screenPos.y, width, height, SWP_SHOWWINDOW);
    GM_Log(L"SetWindowPos TOPMOST show ret=%d gle=%lu", sp, GetLastError());
    BOOL bw = BringWindowToTop(m_hwnd);
    GM_Log(L"BringWindowToTop ret=%d gle=%lu", bw, GetLastError());
    BOOL sf = SetForegroundWindow(m_hwnd);
    GM_Log(L"SetForegroundWindow ret=%d gle=%lu", sf, GetLastError());
    HWND prev = SetActiveWindow(m_hwnd);
    GM_Log(L"SetActiveWindow prev=%p gle=%lu", prev, GetLastError());
    HWND fprev = SetFocus(m_hwnd);
    GM_Log(L"SetFocus prev=%p gle=%lu", fprev, GetLastError());

    RECT rc{}; GetWindowRect(m_hwnd, &rc);
    GM_Log(L"Created window rect=[%ld,%ld %ldx%ld]", rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top);

    // Start selection timer
    UINT_PTR tid = SetTimer(m_hwnd, kTimerId, m_cycleMs, nullptr);
    GM_Log(L"SetTimer id=%u interval=%u ret=%llu gle=%lu", kTimerId, m_cycleMs, static_cast<unsigned long long>(tid), GetLastError());
    m_selected = 0;
    UpdateSelectionVisuals();
    return true;
}

void GlideMenu::Destroy() noexcept
{
    GM_Log(L"Destroy hwnd=%p", m_hwnd);
    if (m_hwnd)
    {
        KillTimer(m_hwnd, kTimerId);
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
}

void GlideMenu::Show() noexcept
{
    GM_Log(L"Show hwnd=%p", m_hwnd);
    if (m_hwnd)
    {
        ShowWindow(m_hwnd, SW_SHOW);
        BringWindowToTop(m_hwnd);
        SetForegroundWindow(m_hwnd);
        SetActiveWindow(m_hwnd);
        SetFocus(m_hwnd);
        // Do not capture mouse here; capture only during button down
    }
}

void GlideMenu::Hide() noexcept
{
    GM_Log(L"Hide hwnd=%p", m_hwnd);
    if (m_hwnd)
    {
        if (GetCapture() == m_hwnd)
        {
            ReleaseCapture();
            GM_Log(L"ReleaseCapture on Hide");
        }
        ShowWindow(m_hwnd, SW_HIDE);
    }
}

void GlideMenu::Next() noexcept
{
    if (m_rows.empty()) return;
    m_selected = (m_selected + 1) % static_cast<int>(m_rows.size());
    GM_Log(L"Next selection=%d", m_selected);
    UpdateSelectionVisuals();
}

void GlideMenu::UpdateSelectionVisuals() noexcept
{
    if (m_hwnd)
    {
        InvalidateRect(m_hwnd, nullptr, FALSE);
        UpdateWindow(m_hwnd);
    }
}

void GlideMenu::OnPaint() noexcept
{
    PAINTSTRUCT ps{};
    HDC hdc = BeginPaint(m_hwnd, &ps);
    RECT rc; GetClientRect(m_hwnd, &rc);

    HBRUSH bg = CreateSolidBrush(m_bg);
    FillRect(hdc, &rc, bg);
    DeleteObject(bg);

    SetBkMode(hdc, TRANSPARENT);

    for (int i = 0; i < (int)m_rows.size(); ++i)
    {
        RECT rowRc{ rc.left + m_paddingX, rc.top + m_paddingY + i * m_rowHeight,
                    rc.left + m_width - m_paddingX, rc.top + m_paddingY + (i + 1) * m_rowHeight };

        if (i == m_selected)
        {
            HBRUSH hi = CreateSolidBrush(m_hiBg);
            RECT hiRc{ rc.left + 2, rowRc.top, rc.right - 2, rowRc.bottom };
            FillRect(hdc, &hiRc, hi);
            DeleteObject(hi);
            SetTextColor(hdc, m_hiFg);
        }
        else
        {
            SetTextColor(hdc, m_fg);
        }

        DrawTextW(hdc, m_rows[i].text.c_str(), -1, &rowRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }

    EndPaint(m_hwnd, &ps);
}

void GlideMenu::CommitSelection() noexcept
{
    GM_Log(L"CommitSelection sel=%d hwnd=%p notify=%p", m_selected, m_hwnd, m_notify);
    if (!m_notify || m_rows.empty())
        return;
    auto action = static_cast<UINT>(m_rows[m_selected].action);
    LPARAM pos = MAKELPARAM(static_cast<WORD>(m_anchor.x), static_cast<WORD>(m_anchor.y));
    BOOL pm = PostMessage(m_notify, WM_GLIDE_MENU_ACTION, action, pos);
    GM_Log(L"PostMessage WM_GLIDE_MENU_ACTION action=%u pos=[%ld,%ld] ret=%d gle=%lu", action, m_anchor.x, m_anchor.y, pm, GetLastError());
    Hide();
}
