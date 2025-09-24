/*
 * Win32 Mouse Magnifier Application - Clean Rewrite
 * Constant-offset quadrant positioning (BR -> BL -> TL -> TR)
 * Multi-monitor aware version with dynamic capture sizing
 */
#include <windows.h>
#include <stdio.h>
#include <shellscalingapi.h>
#include <dwmapi.h>

// Link required libraries
#pragma comment(lib, "Shcore.lib")
#pragma comment(lib, "Dwmapi.lib")

// ============================================================================
// CONFIGURATION
// ============================================================================
#define MAGNIFIER_WINDOW_CLASS L"MagnifierWindow"
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


// ============================================================================
// GLOBAL STATE
// ============================================================================
static HINSTANCE g_hInstance = NULL;
static HWND      g_hMagnifierWindow = NULL;
static HHOOK     g_hMouseHook = NULL;
static HDC       g_hScreenDC = NULL;
static HDC       g_hMemDC = NULL;
static HBITMAP   g_hBitmap = NULL;
static HBITMAP   g_hOldBitmap = NULL;

static POINT g_mousePos = {0,0};
static RECT  g_currentMonitorRect = {0};      /* Current monitor bounds */
static HMONITOR g_currentMonitor = NULL;      /* Current monitor handle */
static float g_magnification = DEFAULT_MAGNIFICATION;
static int   g_captureSize = BASE_CAPTURE_SIZE;   /* dynamic capture size */
static int   g_displayWidth = 0;             /* scaled (without frame) */
static int   g_displayHeight = 0;            /* scaled (without frame) */
static int   g_currentQuadrant = 0;          /* 0=BR,1=BL,2=TR,3=TL */
static int   g_currentDPI = 96;              /* Current monitor DPI */
static int   g_dynamicCursorGap = MIN_CURSOR_GAP;  /* Dynamic offset distance */

/* Crosshair positioning for accurate cursor representation */
static POINT g_actualCaptureTopLeft = {0,0}; /* Actual capture top-left after clamping */
static POINT g_cursorOffsetInCapture = {0,0}; /* Cursor position within the captured area */

/* Refresh management for capturing dynamic content */
static BOOL g_refreshTimerActive = FALSE;    /* Whether refresh timer is currently active */
static DWORD g_lastMouseMoveTime = 0;        /* Timestamp of last mouse movement */

/* Diagnostic logging */
#ifdef _DEBUG
static void DebugLog(const char* tag, POINT mouse, POINT win, int quad, POINT facingCorner)
{
    static const char* qn[4] = {"BR","BL","TR","TL"};
    char buf[320];
    const char* name = (quad>=0 && quad<4)? qn[quad]:"?";
    /* facingCorner is the window corner nearest the cursor */
    int dxFacing = facingCorner.x - mouse.x;
    int dyFacing = facingCorner.y - mouse.y;
    int dxTopLeft = win.x - mouse.x;      /* requested relative top-left */
    int dyTopLeft = win.y - mouse.y;
    sprintf_s(buf,sizeof(buf),
              "%s | mouse=(%ld,%ld) winTopLeft=(%ld,%ld) relTopLeft=(%d,%d) quad=%s facingCorner=(%ld,%ld) facingDelta=(%d,%d)\r\n",
              tag, mouse.x, mouse.y, win.x, win.y, dxTopLeft, dyTopLeft, name,
              facingCorner.x, facingCorner.y, dxFacing, dyFacing);
    OutputDebugStringA(buf);
}
#else
#define DebugLog(tag,mouse,win,quad,facingCorner) ((void)0)
#endif

// Forward declarations
static BOOL InitializeApplication(HINSTANCE);
static BOOL InitializeGDI(void);
static BOOL CreateMagnifierWindow(void);
static void Cleanup(void);
static void UpdateMagnifierDisplay(void);
static void CaptureArea(void);
static POINT CalculatePosition(POINT mouse); /* returns window top-left */
static void RecomputeDisplaySize(void);
static void ApplyMagnification(float newMag);
static POINT GetFacingCornerFromTopLeft(POINT topLeft, int quadrant);
static void UpdateMonitorInfo(POINT mousePos);
static void RecreateGDIResources(void);
static int CalculateDynamicCaptureSize(int dpi);
static int CalculateDynamicCursorGap(void);
static void UpdateDynamicOffset(void);
static COLORREF GetSystemAccentColor(void);
static void StartRefreshTimer(void);
static void StopRefreshTimer(void);
static void RefreshMagnifierContent(void);

// ============================================================================
// DYNAMIC OFFSET CALCULATION
// ============================================================================
/*
 * Calculate the minimum safe distance the magnifier window should be from the cursor
 * to prevent the window from appearing in its own captured area.
 * 
 * The logic is:
 * 1. The capture area is g_captureSize x g_captureSize centered on the cursor
 * 2. The magnifier window size is (g_displayWidth + 2*FRAME_THICKNESS) x (g_displayHeight + 2*FRAME_THICKNESS)
 * 3. We need to ensure the closest corner of the magnifier window is outside the capture area
 * 4. Add safety margin and minimum visual gap for usability
 */
static int CalculateDynamicCursorGap(void)
{
    /* Half of capture area gives us the radius from cursor to capture edge */
    int captureRadius = g_captureSize / 2;
    
    /* Window dimensions including frame */
    int windowWidth = g_displayWidth + (FRAME_THICKNESS * 2);
    int windowHeight = g_displayHeight + (FRAME_THICKNESS * 2);
    
    /* For safety, we want the nearest corner of the window to be outside the capture area.
     * In the worst case (diagonal positioning), we need to account for the window's 
     * diagonal extent from its nearest corner.
     * However, since we're positioning by quadrants, we can be more precise:
     * - For BR quadrant: nearest corner is top-left, so we need captureRadius + safety margin
     * - For other quadrants: similar logic applies
     */
    
    /* Calculate the minimum gap needed based on capture area */
    int minimumGapForCapture = captureRadius + SAFETY_MARGIN;
    
    /* Scale gap based on magnification to maintain proportional spacing */
    int scaledGap = (int)(MIN_CURSOR_GAP * g_magnification);
    
    /* Also consider DPI scaling for consistent visual appearance */
    int dpiAdjustedGap = MulDiv(scaledGap, g_currentDPI, 96);
    
    /* Use the maximum of all calculated values to ensure safety */
    int dynamicGap = minimumGapForCapture;
    if (dpiAdjustedGap > dynamicGap) dynamicGap = dpiAdjustedGap;
    
    /* Ensure we don't go below minimum for usability */
    if (dynamicGap < MIN_CURSOR_GAP) dynamicGap = MIN_CURSOR_GAP;
    
    /* Cap at reasonable maximum to prevent window from going off-screen */
    int maxGap = (g_currentMonitorRect.right - g_currentMonitorRect.left) / 6; /* 1/6 of monitor width */
    if (dynamicGap > maxGap) dynamicGap = maxGap;
    
    return dynamicGap;
}

static void UpdateDynamicOffset(void)
{
    int newGap = CalculateDynamicCursorGap();
    if (newGap != g_dynamicCursorGap)
    {
        g_dynamicCursorGap = newGap;
    }
}

// ============================================================================
// DYNAMIC SIZING BASED ON DPI
// ============================================================================
static int CalculateDynamicCaptureSize(int dpi)
{
    /* Scale capture size based on DPI to maintain consistent visual size
     * At 96 DPI (100% scale): BASE_CAPTURE_SIZE
     * At 120 DPI (125% scale): BASE_CAPTURE_SIZE * 1.25
     * At 144 DPI (150% scale): BASE_CAPTURE_SIZE * 1.5
     * At 192 DPI (200% scale): BASE_CAPTURE_SIZE * 2.0
     * etc.
     */
    int scaledSize = MulDiv(BASE_CAPTURE_SIZE, dpi, 96);
    
    /* Clamp to reasonable bounds */
    if (scaledSize < 50) scaledSize = 50;
    if (scaledSize > 500) scaledSize = 500;
        
    return scaledSize;
}

// ============================================================================
// MONITOR AWARENESS
// ============================================================================
static void UpdateMonitorInfo(POINT mousePos)
{
    HMONITOR newMonitor = MonitorFromPoint(mousePos, MONITOR_DEFAULTTONEAREST);
    
    if (newMonitor != g_currentMonitor)
    {
        g_currentMonitor = newMonitor;
        
        MONITORINFO mi = {0};
        mi.cbSize = sizeof(MONITORINFO);
        
        if (GetMonitorInfo(g_currentMonitor, &mi))
        {
            g_currentMonitorRect = mi.rcMonitor;
            
            /* Get DPI for this monitor */
            UINT dpiX = 96, dpiY = 96;
            HRESULT hr = GetDpiForMonitor(g_currentMonitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY);
            if (SUCCEEDED(hr))
            {
                g_currentDPI = (int)dpiX;
            }
            else
            {
                /* Fallback for older Windows versions or on failure */
                HDC hdc = GetDC(NULL);
                if (hdc)
                {
                    g_currentDPI = GetDeviceCaps(hdc, LOGPIXELSX);
                    ReleaseDC(NULL, hdc);
                }
                else
                {
                    g_currentDPI = 96;
                }
            }
            
            /* Recalculate capture size based on new monitor DPI */
            int newCaptureSize = CalculateDynamicCaptureSize(g_currentDPI);
            if (newCaptureSize != g_captureSize)
            {
                g_captureSize = newCaptureSize;
                RecomputeDisplaySize();
                UpdateDynamicOffset();  /* Update offset when capture size changes */
            }
                        
            /* Recreate GDI resources for the new monitor */
            RecreateGDIResources();
        }
    }
}

static void RecreateGDIResources(void)
{
    /* Clean up existing resources */
    if (g_hOldBitmap) { SelectObject(g_hMemDC, g_hOldBitmap); g_hOldBitmap = NULL; }
    if (g_hBitmap) { DeleteObject(g_hBitmap); g_hBitmap = NULL; }
    if (g_hMemDC) { DeleteDC(g_hMemDC); g_hMemDC = NULL; }
    if (g_hScreenDC) { ReleaseDC(NULL, g_hScreenDC); g_hScreenDC = NULL; }
    
    /* Recreate for current monitor with new capture size */
    g_hScreenDC = GetDC(NULL);
    if (!g_hScreenDC) return;
    
    g_hMemDC = CreateCompatibleDC(g_hScreenDC);
    if (!g_hMemDC) 
    {
        ReleaseDC(NULL, g_hScreenDC);
        g_hScreenDC = NULL;
        return;
    }
    
    g_hBitmap = CreateCompatibleBitmap(g_hScreenDC, g_captureSize, g_captureSize);
    if (!g_hBitmap) 
    {
        DeleteDC(g_hMemDC);
        ReleaseDC(NULL, g_hScreenDC);
        g_hMemDC = NULL;
        g_hScreenDC = NULL;
        return;
    }
    
    g_hOldBitmap = (HBITMAP)SelectObject(g_hMemDC, g_hBitmap);
    
    /* Resize magnifier window to match new display dimensions */
    if (g_hMagnifierWindow)
    {
        SetWindowPos(g_hMagnifierWindow, NULL, 0, 0,
                     g_displayWidth + FRAME_THICKNESS*2,
                     g_displayHeight + FRAME_THICKNESS*2,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

// ============================================================================
// MAGNIFICATION SIZE
// ============================================================================
static void RecomputeDisplaySize(void)
{
    g_displayWidth  = (int)(g_captureSize * g_magnification);
    g_displayHeight = (int)(g_captureSize * g_magnification);
    
    /* Update dynamic offset when display size changes */
    UpdateDynamicOffset();
}

static void ApplyMagnification(float newMag)
{
    if (newMag < MIN_MAGNIFICATION) newMag = MIN_MAGNIFICATION;
    if (newMag > MAX_MAGNIFICATION) newMag = MAX_MAGNIFICATION;
    g_magnification = newMag;
    RecomputeDisplaySize();
    
    /* Resize window to match new display dimensions */
    if (g_hMagnifierWindow)
    {
        SetWindowPos(g_hMagnifierWindow, NULL, 0, 0,
                     g_displayWidth + FRAME_THICKNESS*2,
                     g_displayHeight + FRAME_THICKNESS*2,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
    
    UpdateMagnifierDisplay();
}

// ============================================================================
// POSITIONING (DYNAMIC VISUAL GAP)
// ============================================================================
/*
 * Dynamic positioning with calculated safe distance to prevent the magnifier
 * window from appearing in its own captured area. The gap is calculated based on:
 * - Capture area size (to ensure window is outside capture radius)
 * - Window size and magnification level (for proportional spacing)
 * - Monitor DPI (for consistent visual appearance)
 * - Safety margins (to prevent edge cases)
 * 
 * Quadrant selection order (first fit): BR -> BL -> TL -> TR.
 * Now uses current monitor bounds and dynamic cursor gap.
 */
static POINT CalculatePosition(POINT mouse)
{
    int winW = g_displayWidth + FRAME_THICKNESS * 2;
    int winH = g_displayHeight + FRAME_THICKNESS * 2;
    POINT pos; /* top-left */

    /* Try Bottom-Right */
    pos.x = mouse.x + g_dynamicCursorGap;
    pos.y = mouse.y + g_dynamicCursorGap;
    if (pos.x + winW <= g_currentMonitorRect.right && pos.y + winH <= g_currentMonitorRect.bottom) { g_currentQuadrant = 0; return pos; }

    /* Try Bottom-Left */
    pos.x = mouse.x - g_dynamicCursorGap - winW;
    pos.y = mouse.y + g_dynamicCursorGap;
    if (pos.x >= g_currentMonitorRect.left && pos.y + winH <= g_currentMonitorRect.bottom) { g_currentQuadrant = 1; return pos; }

    /* Try Top-Left */
    pos.x = mouse.x - g_dynamicCursorGap - winW;
    pos.y = mouse.y - g_dynamicCursorGap - winH;
    if (pos.x >= g_currentMonitorRect.left && pos.y >= g_currentMonitorRect.top) { g_currentQuadrant = 3; return pos; }

    /* Try Top-Right */
    pos.x = mouse.x + g_dynamicCursorGap;
    pos.y = mouse.y - g_dynamicCursorGap - winH;
    if (pos.x + winW <= g_currentMonitorRect.right && pos.y >= g_currentMonitorRect.top) { g_currentQuadrant = 2; return pos; }

    /* Fallback: clamp BR attempt to current monitor */
    pos.x = mouse.x + g_dynamicCursorGap;
    pos.y = mouse.y + g_dynamicCursorGap;
    if (pos.x + winW > g_currentMonitorRect.right) pos.x = g_currentMonitorRect.right - winW;
    if (pos.y + winH > g_currentMonitorRect.bottom) pos.y = g_currentMonitorRect.bottom - winH;
    if (pos.x < g_currentMonitorRect.left) pos.x = g_currentMonitorRect.left;
    if (pos.y < g_currentMonitorRect.top)  pos.y = g_currentMonitorRect.top;
    g_currentQuadrant = 0;
    return pos;
}

static POINT GetFacingCornerFromTopLeft(POINT topLeft, int quadrant)
{
    POINT facing = topLeft;
    int winW = g_displayWidth + (FRAME_THICKNESS * 2);
    int winH = g_displayHeight + (FRAME_THICKNESS * 2);
    switch (quadrant)
    {
        case 0: /* BR */ break; /* facing corner = topLeft */
        case 1: /* BL */ facing.x += winW; break;          /* top-right corner */
        case 2: /* TR */ facing.y += winH; break;          /* bottom-left corner */
        case 3: /* TL */ facing.x += winW; facing.y += winH; break; /* bottom-right */
        default: break;
    }
    return facing;
}

// ============================================================================
// SCREEN CAPTURE
// ============================================================================
static void CaptureArea(void)
{
    int capW = g_captureSize;
    int capH = g_captureSize;
    
    /* Calculate desired capture area centered on cursor */
    int idealX = g_mousePos.x - capW/2;
    int idealY = g_mousePos.y - capH/2;
    
    /* Clamp to current monitor bounds */
    int actualX = idealX;
    int actualY = idealY;
    
    if (actualX < g_currentMonitorRect.left) actualX = g_currentMonitorRect.left;
    if (actualY < g_currentMonitorRect.top)  actualY = g_currentMonitorRect.top;
    if (actualX + capW > g_currentMonitorRect.right)  actualX = g_currentMonitorRect.right  - capW;
    if (actualY + capH > g_currentMonitorRect.bottom) actualY = g_currentMonitorRect.bottom - capH;

    /* Store the actual capture position */
    g_actualCaptureTopLeft.x = actualX;
    g_actualCaptureTopLeft.y = actualY;
    
    /* Calculate where the cursor actually is within the captured area */
    g_cursorOffsetInCapture.x = g_mousePos.x - actualX;
    g_cursorOffsetInCapture.y = g_mousePos.y - actualY;

    BitBlt(g_hMemDC, 0, 0, capW, capH, g_hScreenDC, actualX, actualY, SRCCOPY);
}

// ============================================================================
// REDRAW & UPDATE
// ============================================================================
static void UpdateMagnifierDisplay(void)
{
    if (!g_hMagnifierWindow) return;
    
    /* Check if we've moved to a different monitor */
    UpdateMonitorInfo(g_mousePos);
    
    CaptureArea();
    POINT topLeft = CalculatePosition(g_mousePos);
        
    SetWindowPos(g_hMagnifierWindow, HWND_TOPMOST, topLeft.x, topLeft.y,
                 g_displayWidth + FRAME_THICKNESS*2,
                 g_displayHeight + FRAME_THICKNESS*2,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);

    POINT facing = GetFacingCornerFromTopLeft(topLeft, g_currentQuadrant);

    InvalidateRect(g_hMagnifierWindow, NULL, FALSE);
    
    /* Start refresh timer to capture dynamic content when mouse becomes stationary */
    StartRefreshTimer();
}

// ============================================================================
// DRAWING
// ============================================================================

/*
 * Get the system accent color using DWM API.
 * Falls back to a default blue color if unable to retrieve the system setting.
 */
static COLORREF GetSystemAccentColor(void)
{
    COLORREF accentColor = RGB(0, 120, 212); /* Default Windows blue as fallback */
    
    /* Use DwmGetColorizationColor for getting the system colorization color */
    DWORD dwColorizationColor;
    BOOL fOpaqueBlend;
    
    HRESULT hr = DwmGetColorizationColor(&dwColorizationColor, &fOpaqueBlend);
    if (SUCCEEDED(hr))
    {
        /* DwmGetColorizationColor returns color as 0xAARRGGBB, convert to COLORREF */
        accentColor = RGB((dwColorizationColor & 0xFF0000) >> 16,
                         (dwColorizationColor & 0x00FF00) >> 8,
                         (dwColorizationColor & 0x0000FF));
    }
        
    return accentColor;
}

static void DrawMagnifiedContent(HDC hdc)
{
    RECT rc; 
    HBRUSH frameBrush;
    HPEN borderPen, crossPen, crossOutlinePen;
    HGDIOBJ oldPen, oldBrush;
    
    GetClientRect(g_hMagnifierWindow, &rc);
    
    /* Use system accent color for frame */
    COLORREF accentColor = GetSystemAccentColor();
    frameBrush = CreateSolidBrush(accentColor);
    FillRect(hdc, &rc, frameBrush);

    if (g_hMemDC && g_hBitmap)
    {
        StretchBlt(hdc, FRAME_THICKNESS, FRAME_THICKNESS,
                   g_displayWidth, g_displayHeight,
                   g_hMemDC, 0, 0, g_captureSize, g_captureSize, SRCCOPY);
    }

    /* Create a complementary border color (lighter version of accent) */
    COLORREF borderColor = RGB(
        min(255, GetRValue(accentColor) + 60),
        min(255, GetGValue(accentColor) + 60), 
        min(255, GetBValue(accentColor) + 60)
    );
    
    borderPen = CreatePen(PS_SOLID, 1, borderColor);
    oldPen = SelectObject(hdc, borderPen);
    oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Rectangle(hdc,
              FRAME_THICKNESS-1, FRAME_THICKNESS-1,
              FRAME_THICKNESS + g_displayWidth + 1,
              FRAME_THICKNESS + g_displayHeight + 1);

    /* Enhanced crosshair positioned at actual cursor location within captured area */
    {
        /* Calculate crosshair position based on cursor's actual position in the captured area */
        int cx = FRAME_THICKNESS + (int)((float)g_cursorOffsetInCapture.x * g_magnification);
        int cy = FRAME_THICKNESS + (int)((float)g_cursorOffsetInCapture.y * g_magnification);
        
        /* Ensure crosshair stays within the display area */
        if (cx < FRAME_THICKNESS) cx = FRAME_THICKNESS;
        if (cy < FRAME_THICKNESS) cy = FRAME_THICKNESS;
        if (cx >= FRAME_THICKNESS + g_displayWidth) cx = FRAME_THICKNESS + g_displayWidth - 1;
        if (cy >= FRAME_THICKNESS + g_displayHeight) cy = FRAME_THICKNESS + g_displayHeight - 1;
        
        /* Draw light outline first (1 pixel offset) */
        crossOutlinePen = CreatePen(PS_SOLID, 1, RGB(255, 255, 255)); /* White outline */
        SelectObject(hdc, crossOutlinePen);
        
        /* Horizontal outline */
        MoveToEx(hdc, cx-11, cy-1, NULL); LineTo(hdc, cx+11, cy-1);
        MoveToEx(hdc, cx-11, cy+1, NULL); LineTo(hdc, cx+11, cy+1);
        MoveToEx(hdc, cx-10, cy-1, NULL); LineTo(hdc, cx-10, cy+1);
        MoveToEx(hdc, cx+10, cy-1, NULL); LineTo(hdc, cx+10, cy+1);
        
        /* Vertical outline */
        MoveToEx(hdc, cx-1, cy-11, NULL); LineTo(hdc, cx-1, cy+11);
        MoveToEx(hdc, cx+1, cy-11, NULL); LineTo(hdc, cx+1, cy+11);
        MoveToEx(hdc, cx-1, cy-10, NULL); LineTo(hdc, cx+1, cy-10);
        MoveToEx(hdc, cx-1, cy+10, NULL); LineTo(hdc, cx+1, cy+10);
        
        /* Draw main crosshair */
        crossPen = CreatePen(PS_SOLID, 1, RGB(255, 0, 0)); /* Red crosshair */
        SelectObject(hdc, crossPen);
        MoveToEx(hdc, cx-10, cy, NULL); LineTo(hdc, cx+10, cy);
        MoveToEx(hdc, cx, cy-10, NULL); LineTo(hdc, cx, cy+10);
    }

    /* Cleanup */
    SelectObject(hdc, oldPen);
    SelectObject(hdc, oldBrush);
    DeleteObject(frameBrush);
    DeleteObject(borderPen);
    DeleteObject(crossPen);
    DeleteObject(crossOutlinePen);
}

// ============================================================================
// WINDOW PROC
// ============================================================================
static LRESULT CALLBACK MagnifierWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
        case WM_PAINT:
        {
            PAINTSTRUCT ps; HDC hdc = BeginPaint(hwnd,&ps);
            DrawMagnifiedContent(hdc);
            EndPaint(hwnd,&ps);
            return 0;
        }
        case WM_TIMER:
        {
            if (wParam == REFRESH_TIMER_ID)
            {
                /* Check if mouse has been stationary long enough to warrant timer updates */
                DWORD currentTime = GetTickCount();
                if (currentTime - g_lastMouseMoveTime > 100) /* 100ms threshold */
                {
                    RefreshMagnifierContent();
                }
                return 0;
            }
            break;
        }
        case WM_KEYDOWN:
        {
            if (wParam == VK_ESCAPE) { PostQuitMessage(0); return 0; }
            if (wParam == VK_OEM_PLUS || wParam == VK_ADD || wParam == 187)
            { ApplyMagnification(g_magnification + 0.1f); return 0; }
            if (wParam == VK_OEM_MINUS || wParam == VK_SUBTRACT || wParam == 189)
            { ApplyMagnification(g_magnification - 0.1f); return 0; }
            break;
        }
        case WM_LBUTTONDOWN:
            SendMessage(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, lParam); return 0;
        case WM_DESTROY:
            StopRefreshTimer(); /* Clean up timer on window destruction */
            PostQuitMessage(0); return 0;
    }
    return DefWindowProc(hwnd,msg,wParam,lParam);
}

// ============================================================================
// LOW LEVEL MOUSE HOOK
// ============================================================================
static LRESULT CALLBACK LowLevelMouseProc(int code, WPARAM wParam, LPARAM lParam)
{
    if (code == HC_ACTION && wParam == WM_MOUSEMOVE)
    {
        PMSLLHOOKSTRUCT ms = (PMSLLHOOKSTRUCT)lParam;
        g_mousePos = ms->pt;
        g_lastMouseMoveTime = GetTickCount(); /* Track timing for timer logic */
        UpdateMagnifierDisplay();
    }
    return CallNextHookEx(g_hMouseHook, code, wParam, lParam);
}

// ============================================================================
// INITIALIZATION / CLEANUP
// ============================================================================
static BOOL InitializeApplication(HINSTANCE hInstance)
{
    WNDCLASSEX wc = {0};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = MagnifierWindowProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wc.hIconSm = wc.hIcon;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW+1);
    wc.lpszClassName = MAGNIFIER_WINDOW_CLASS;
    return RegisterClassEx(&wc) != 0;
}

static BOOL InitializeGDI(void)
{
    g_hScreenDC = GetDC(NULL); if (!g_hScreenDC) return FALSE;
    g_hMemDC = CreateCompatibleDC(g_hScreenDC); if (!g_hMemDC) { ReleaseDC(NULL,g_hScreenDC); g_hScreenDC=NULL; return FALSE; }
    g_hBitmap = CreateCompatibleBitmap(g_hScreenDC, g_captureSize, g_captureSize);
    if (!g_hBitmap) { DeleteDC(g_hMemDC); ReleaseDC(NULL,g_hScreenDC); g_hMemDC=NULL; g_hScreenDC=NULL; return FALSE; }
    g_hOldBitmap = (HBITMAP)SelectObject(g_hMemDC, g_hBitmap);
    return TRUE;
}

static BOOL CreateMagnifierWindow(void)
{
    g_hMagnifierWindow = CreateWindowEx(WS_EX_TOPMOST | WS_EX_NOACTIVATE,
                                        MAGNIFIER_WINDOW_CLASS, L"Magnifier", WS_POPUP,
                                        0, 0,
                                        g_displayWidth + FRAME_THICKNESS*2,
                                        g_displayHeight + FRAME_THICKNESS*2,
                                        NULL, NULL, g_hInstance, NULL);
    return g_hMagnifierWindow != NULL;
}

static void Cleanup(void)
{
    StopRefreshTimer(); /* Stop refresh timer before cleanup */
    if (g_hMouseHook) { UnhookWindowsHookEx(g_hMouseHook); g_hMouseHook=NULL; }
    if (g_hOldBitmap) { SelectObject(g_hMemDC, g_hOldBitmap); g_hOldBitmap=NULL; }
    if (g_hBitmap) { DeleteObject(g_hBitmap); g_hBitmap=NULL; }
    if (g_hMemDC) { DeleteDC(g_hMemDC); g_hMemDC=NULL; }
    if (g_hScreenDC) { ReleaseDC(NULL,g_hScreenDC); g_hScreenDC=NULL; }
    if (g_hMagnifierWindow) { DestroyWindow(g_hMagnifierWindow); g_hMagnifierWindow=NULL; }
}

// ============================================================================
// REFRESH TIMER MANAGEMENT
// ============================================================================
/*
 * Start the refresh timer to capture dynamic desktop content when mouse is stationary.
 * This ensures that text being typed, animations, video, and other dynamic content
 * is reflected in the magnifier even when the mouse isn't moving.
 */
static void StartRefreshTimer(void)
{
#if ENABLE_DYNAMIC_REFRESH
    if (!g_refreshTimerActive && g_hMagnifierWindow)
    {
        if (SetTimer(g_hMagnifierWindow, REFRESH_TIMER_ID, REFRESH_INTERVAL_MS, NULL))
        {
            g_refreshTimerActive = TRUE;
        }
    }
#endif /* ENABLE_DYNAMIC_REFRESH */
}

static void StopRefreshTimer(void)
{
    if (g_refreshTimerActive && g_hMagnifierWindow)
    {
        KillTimer(g_hMagnifierWindow, REFRESH_TIMER_ID);
        g_refreshTimerActive = FALSE;
    }
}

/*
 * Refresh magnifier content without changing position.
 * Used by timer to capture dynamic desktop changes when mouse is stationary.
 */
static void RefreshMagnifierContent(void)
{
    if (!g_hMagnifierWindow) return;
    
    /* Only capture and redraw, don't change position */
    CaptureArea();
    InvalidateRect(g_hMagnifierWindow, NULL, FALSE);
}

// ============================================================================
// ENTRY POINT
// ============================================================================
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrev, LPSTR lpCmdLine, int nShow)
{
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    (void)hPrev; (void)lpCmdLine; (void)nShow;
    g_hInstance = hInstance;

    GetCursorPos(&g_mousePos);
    
    /* Initialize monitor info for current mouse position - this will set DPI and capture size */
    UpdateMonitorInfo(g_mousePos);
    
    /* Now compute display size with the correct capture size */
    RecomputeDisplaySize();

    if (!InitializeApplication(hInstance) || !InitializeGDI() || !CreateMagnifierWindow())
    {
        Cleanup();
        MessageBox(NULL,L"Initialization failed",L"Magnifier",MB_OK|MB_ICONERROR);
        return 1;
    }

    g_hMouseHook = SetWindowsHookEx(WH_MOUSE_LL, LowLevelMouseProc, hInstance, 0);
    if (!g_hMouseHook)
    {
        Cleanup();
        MessageBox(NULL,L"Mouse hook failed",L"Magnifier",MB_OK|MB_ICONERROR);
        return 1;
    }

    /* Initial placement */
    UpdateMagnifierDisplay();
    ShowWindow(g_hMagnifierWindow, SW_SHOW);
    UpdateWindow(g_hMagnifierWindow);

    /* Create a timer for periodic refresh */
    SetTimer(g_hMagnifierWindow, REFRESH_TIMER_ID, REFRESH_INTERVAL_MS, NULL);

    MSG msg;
    while (GetMessage(&msg,NULL,0,0)) { TranslateMessage(&msg); DispatchMessage(&msg); }

    /* Clean up timer on exit */
    KillTimer(g_hMagnifierWindow, REFRESH_TIMER_ID);

    Cleanup();
    return (int)msg.wParam;
}