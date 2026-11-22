/*
 * rhinossh_darkmode.c - Window chrome theming for RhinoSSH
 *
 * Phase 1 of UI overhaul: applies the RhinoSniff visual identity
 * (deep dark surfaces + cyan accent border) to all top-level windows
 * and themes child controls inside dialogs.
 *
 * Frame technique:
 *   - DWMWA_USE_IMMERSIVE_DARK_MODE (Win10 1809+) for dark titlebar
 *   - DWMWA_CAPTION_COLOR (Win11 22000+)            for dark caption fill
 *   - DWMWA_TEXT_COLOR    (Win11 22000+)            for caption text
 *   - DWMWA_BORDER_COLOR  (Win11 22000+)            for cyan window border
 *   - DWMWA_WINDOW_CORNER_PREFERENCE (Win11 22000+) for rounded corners
 *
 * On older Windows these attributes silently fail and the window
 * keeps the dark titlebar without the cyan border / rounding.
 *
 * Themed common controls (group boxes, radios, checkboxes) ignore
 * WM_CTLCOLOR text color, so we subclass and overdraw the text in
 * the Rhino text color after the themed paint runs.
 */

#include <windows.h>
#include <commctrl.h>
#include <uxtheme.h>
#include "rhinossh_darkmode.h"
#include "rhinossh_theme.h"
#include "rhinossh_modernui.h"

/* ============================================================
 *  STATE
 * ============================================================ */

static int g_dark_supported = 0;
static int g_dark_inited    = 0;
static int g_win_build      = 0;

static HBRUSH g_brBg     = NULL;   /* dialog background        */
static HBRUSH g_brSurf   = NULL;   /* surface / panels         */
static HBRUSH g_brEdit   = NULL;   /* edit field background    */
static HBRUSH g_brAccent = NULL;   /* cyan accent (rare uses)  */

/* ============================================================
 *  DWM / UXTHEME ATTRIBUTE CONSTANTS
 * ============================================================ */

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE        20
#endif
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE_OLD
#define DWMWA_USE_IMMERSIVE_DARK_MODE_OLD    19
#endif
#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE       33
#endif
#ifndef DWMWA_BORDER_COLOR
#define DWMWA_BORDER_COLOR                   34
#endif
#ifndef DWMWA_CAPTION_COLOR
#define DWMWA_CAPTION_COLOR                  35
#endif
#ifndef DWMWA_TEXT_COLOR
#define DWMWA_TEXT_COLOR                     36
#endif

/* DWM_WINDOW_CORNER_PREFERENCE values */
#ifndef DWMWCP_DEFAULT
#define DWMWCP_DEFAULT       0
#define DWMWCP_DONOTROUND    1
#define DWMWCP_ROUND         2
#define DWMWCP_ROUNDSMALL    3
#endif

/* DWM color sentinels */
#ifndef DWMWA_COLOR_DEFAULT
#define DWMWA_COLOR_DEFAULT  0xFFFFFFFF
#define DWMWA_COLOR_NONE     0xFFFFFFFE
#endif

/* ============================================================
 *  DYNAMIC BINDINGS
 * ============================================================ */

typedef HRESULT (WINAPI *fnDwmSetWindowAttribute)(HWND, DWORD, LPCVOID, DWORD);
static fnDwmSetWindowAttribute pDwmSetAttr = NULL;

typedef enum { Default = 0, AllowDark = 1, ForceDark = 2, ForceLight = 3 }
        PreferredAppMode;
typedef PreferredAppMode (WINAPI *fnSetPreferredAppMode)(PreferredAppMode);
static fnSetPreferredAppMode pSetAppMode = NULL;

typedef BOOL (WINAPI *fnAllowDarkModeForWindow)(HWND, BOOL);
static fnAllowDarkModeForWindow pAllowDarkWin = NULL;

typedef void (WINAPI *fnFlushMenuThemes)(void);
static fnFlushMenuThemes pFlushMenus = NULL;

/* ============================================================
 *  HELPERS
 * ============================================================ */

static int get_build(void)
{
    OSVERSIONINFOW ovi;
    typedef LONG (WINAPI *fnRtlGetVersion)(PRTL_OSVERSIONINFOW);
    HMODULE h = GetModuleHandleW(L"ntdll.dll");
    if (h) {
        fnRtlGetVersion p =
            (fnRtlGetVersion)GetProcAddress(h, "RtlGetVersion");
        if (p) {
            memset(&ovi, 0, sizeof(ovi));
            ovi.dwOSVersionInfoSize = sizeof(ovi);
            p((PRTL_OSVERSIONINFOW)&ovi);
            return (int)ovi.dwBuildNumber;
        }
    }
    return 0;
}

/* TRUE if running on Windows 11 (build 22000 or higher), where the
 * caption/border/corner DWM attributes are honored. */
static int has_win11_dwm(void)
{
    return g_win_build >= 22000;
}

/* ============================================================
 *  THEMED-CONTROL TEXT OVERDRAW + GROUPBOX FRAME
 *
 *  Group boxes get fully owner-drawn so the frame is RHINO_BORDER
 *  instead of the themed white. Radios / checkboxes still use the
 *  themed indicator paint with a text overdraw on top.
 * ============================================================ */

#define DARK_BTN_SUBCLASS_ID 42001

static LRESULT CALLBACK DarkBtnSubProc(
    HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
    UINT_PTR uId, DWORD_PTR ref)
{
    (void)ref;

    if (msg == WM_PAINT) {
        LONG style = GetWindowLong(hwnd, GWL_STYLE);
        LONG btype = style & BS_TYPEMASK;

        int isGroupBox   = (btype == BS_GROUPBOX);
        int isRadioCheck = (btype == BS_RADIOBUTTON  || btype == BS_AUTORADIOBUTTON ||
                            btype == BS_CHECKBOX     || btype == BS_AUTOCHECKBOX    ||
                            btype == BS_3STATE       || btype == BS_AUTO3STATE);

        if (!isGroupBox && !isRadioCheck)
            return DefSubclassProc(hwnd, msg, wParam, lParam);

        /* ---- Group box: overdraw themed frame in RHINO_BORDER ----
         * We DO NOT fill the interior — siblings (radios / edits / labels)
         * live visually inside the groupbox area but are z-order siblings,
         * so any FillRect here would wipe them out until they re-render
         * on hover. Same GetDC/ReleaseDC pattern as radio/checkbox below
         * so we don't mess with WM_PAINT validation. */
        if (isGroupBox) {
            LRESULT lr = DefSubclassProc(hwnd, msg, wParam, lParam);

            HDC hdc = GetDC(hwnd);
            if (!hdc) return lr;

            HFONT f  = (HFONT)SendMessage(hwnd, WM_GETFONT, 0, 0);
            HFONT of = f ? (HFONT)SelectObject(hdc, f) : NULL;

            char text[512];
            int len = GetWindowTextA(hwnd, text, sizeof(text));
            SIZE sz = { 0, 0 };
            if (len > 0)
                GetTextExtentPoint32A(hdc, text, len, &sz);

            RECT rc;
            GetClientRect(hwnd, &rc);

            /* Overdraw the themed (white) frame in RHINO_BORDER.
             * Top edge offset by half label height so the label still
             * appears to break the frame line. Two passes (1px outer +
             * 1px inner) to fully cover the themed 2px line. */
            RECT fr = rc;
            fr.top += sz.cy / 2;

            HBRUSH br = CreateSolidBrush(RHINO_BORDER);
            FrameRect(hdc, &fr, br);
            InflateRect(&fr, -1, -1);
            FrameRect(hdc, &fr, br);
            DeleteObject(br);

            /* Draw label with small bg fill behind it (label area only —
             * NOT the whole interior) to break the frame line. */
            if (len > 0) {
                RECT tr;
                tr.left   = rc.left + 9;
                tr.top    = rc.top;
                tr.right  = tr.left + sz.cx + 6;
                tr.bottom = tr.top  + sz.cy;

                FillRect(hdc, &tr, g_brBg);

                tr.left += 3;
                BOOL enabled = IsWindowEnabled(hwnd);
                SetTextColor(hdc, enabled ? RHINO_TEXT : RHINO_TEXT_DISABLED);
                SetBkMode(hdc, TRANSPARENT);
                DrawTextA(hdc, text, len, &tr,
                          DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOCLIP);
            }

            if (of) SelectObject(hdc, of);
            ReleaseDC(hwnd, hdc);
            return lr;
        }

        /* ---- Radio / checkbox: themed paint + text overdraw ---- */
        LRESULT lr = DefSubclassProc(hwnd, msg, wParam, lParam);

        char text[512];
        int len = GetWindowTextA(hwnd, text, sizeof(text));
        if (len <= 0)
            return lr;

        HDC hdc  = GetDC(hwnd);
        HFONT f  = (HFONT)SendMessage(hwnd, WM_GETFONT, 0, 0);
        HFONT of = f ? (HFONT)SelectObject(hdc, f) : NULL;

        BOOL enabled = IsWindowEnabled(hwnd);
        SetTextColor(hdc, enabled ? RHINO_TEXT : RHINO_TEXT_DISABLED);
        SetBkMode(hdc, TRANSPARENT);

        RECT rc;
        GetClientRect(hwnd, &rc);

        SIZE sz;
        GetTextExtentPoint32A(hdc, text, len, &sz);

        int indW = GetSystemMetrics(SM_CXMENUCHECK)
                 + GetSystemMetrics(SM_CXEDGE);
        RECT tr;
        tr.left   = rc.left + indW;
        tr.top    = rc.top;
        tr.right  = rc.right;
        tr.bottom = rc.bottom;

        RECT bg;
        bg.left   = tr.left;
        bg.top    = tr.top;
        bg.right  = tr.left + sz.cx + 4;
        bg.bottom = tr.top  + sz.cy + 4;
        FillRect(hdc, &bg, g_brBg);

        DrawTextA(hdc, text, len, &tr,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        if (of) SelectObject(hdc, of);
        ReleaseDC(hwnd, hdc);
        return lr;
    }

    if (msg == WM_NCDESTROY)
        RemoveWindowSubclass(hwnd, DarkBtnSubProc, uId);

    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

/* ============================================================
 *  NON-CLIENT BORDER OVERDRAW
 *
 *  Edit / Listbox / Combobox / Treeview / Listview controls have a
 *  white sunken WS_EX_CLIENTEDGE or themed WS_BORDER. We subclass
 *  WM_NCPAINT and paint a 1-2px RHINO_BORDER frame on top after the
 *  default NC paint runs, covering the bright edge.
 * ============================================================ */

#define DARK_BORDER_SUBCLASS_ID 42002

static LRESULT CALLBACK DarkBorderSubProc(
    HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
    UINT_PTR uId, DWORD_PTR ref)
{
    (void)ref;

    switch (msg) {
      case WM_NCPAINT:
      {
        /* Let default NC paint run first so scrollbars / focus rect
         * stay correct, then overdraw the outer edge. */
        LRESULT lr = DefSubclassProc(hwnd, msg, wParam, lParam);

        HDC hdc = GetWindowDC(hwnd);
        if (hdc) {
            RECT wrc;
            GetWindowRect(hwnd, &wrc);
            int w = wrc.right  - wrc.left;
            int h = wrc.bottom - wrc.top;
            RECT outer = { 0, 0, w, h };

            HBRUSH br = CreateSolidBrush(RHINO_BORDER);
            FrameRect(hdc, &outer, br);

            /* WS_EX_CLIENTEDGE renders as a 2px sunken edge — cover
             * the second pixel ring too so no white shows through. */
            LONG ex = GetWindowLong(hwnd, GWL_EXSTYLE);
            if (ex & WS_EX_CLIENTEDGE) {
                RECT inner = { 1, 1, w - 1, h - 1 };
                FrameRect(hdc, &inner, br);
            }
            DeleteObject(br);

            ReleaseDC(hwnd, hdc);
        }
        return lr;
      }

      case WM_NCDESTROY:
        RemoveWindowSubclass(hwnd, DarkBorderSubProc, uId);
        break;
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

/* Helper: tag a control for NC border overdraw — only if it actually
 * has a visible border style. The single RedrawWindow call at the end
 * of rhinossh_apply_dark_dialog kicks all NC areas to repaint at once. */
static void apply_dark_border(HWND hwnd)
{
    LONG style = GetWindowLong(hwnd, GWL_STYLE);
    LONG ex    = GetWindowLong(hwnd, GWL_EXSTYLE);
    if (!(style & WS_BORDER) &&
        !(ex & (WS_EX_CLIENTEDGE | WS_EX_STATICEDGE)))
        return;
    SetWindowSubclass(hwnd, DarkBorderSubProc, DARK_BORDER_SUBCLASS_ID, 0);
}

/* ============================================================
 *  CHILD-WINDOW THEMING
 * ============================================================ */

static BOOL CALLBACK dark_child_cb(HWND hwnd, LPARAM lParam)
{
    char cls[64];
    (void)lParam;
    GetClassNameA(hwnd, cls, sizeof(cls));

    if (pAllowDarkWin)
        pAllowDarkWin(hwnd, TRUE);

    if (!_stricmp(cls, "SysTreeView32") || !_stricmp(cls, "SysListView32")) {
        SetWindowTheme(hwnd, L"DarkMode_Explorer", NULL);
        apply_dark_border(hwnd);

    } else if (!_stricmp(cls, "SysHeader32") ||
               !_stricmp(cls, "ScrollBar")) {
        /* Theme only — no NC border overdraw (no meaningful border) */
        SetWindowTheme(hwnd, L"DarkMode_Explorer", NULL);

    } else if (!_stricmp(cls, "Button")) {
        SetWindowTheme(hwnd, L"DarkMode_Explorer", NULL);
        SetWindowSubclass(hwnd, DarkBtnSubProc, DARK_BTN_SUBCLASS_ID, 0);

    } else if (!_stricmp(cls, "Edit") || !_stricmp(cls, "ComboBox") ||
               !_stricmp(cls, "ComboLBox")) {
        SetWindowTheme(hwnd, L"DarkMode_CFD", NULL);
        apply_dark_border(hwnd);

    } else if (!_stricmp(cls, "ListBox")) {
        SetWindowTheme(hwnd, L"DarkMode_Explorer", NULL);
        apply_dark_border(hwnd);

    } else if (!_stricmp(cls, "Static")) {
        /* Strip theme so WM_CTLCOLORSTATIC text color sticks */
        SetWindowTheme(hwnd, L"", L"");

    } else if (!_stricmp(cls, "msctls_trackbar32") ||
               !_stricmp(cls, "msctls_updown32")   ||
               !_stricmp(cls, "msctls_progress32") ||
               !_stricmp(cls, "SysTabControl32")) {
        SetWindowTheme(hwnd, L"DarkMode_Explorer", NULL);
    }

    InvalidateRect(hwnd, NULL, TRUE);
    return TRUE;
}

/* ============================================================
 *  PUBLIC API
 * ============================================================ */

void rhinossh_darkmode_init(void)
{
    HMODULE hUx, hDwm;
    if (g_dark_inited) return;
    g_dark_inited = 1;

    g_win_build = get_build();
    if (g_win_build < 17763) return;   /* dark mode landed in Win10 1809 */

    hUx = LoadLibraryW(L"uxtheme.dll");
    if (hUx) {
        pSetAppMode   = (fnSetPreferredAppMode)
                         GetProcAddress(hUx, MAKEINTRESOURCEA(135));
        pAllowDarkWin = (fnAllowDarkModeForWindow)
                         GetProcAddress(hUx, MAKEINTRESOURCEA(133));
        pFlushMenus   = (fnFlushMenuThemes)
                         GetProcAddress(hUx, MAKEINTRESOURCEA(136));
    }

    hDwm = LoadLibraryW(L"dwmapi.dll");
    if (hDwm)
        pDwmSetAttr = (fnDwmSetWindowAttribute)
                       GetProcAddress(hDwm, "DwmSetWindowAttribute");

    if (pSetAppMode) pSetAppMode(ForceDark);
    if (pFlushMenus) pFlushMenus();

    g_brBg     = CreateSolidBrush(RHINO_BG_DEEP);
    g_brSurf   = CreateSolidBrush(RHINO_BG_SURFACE);
    g_brEdit   = CreateSolidBrush(RHINO_BG_EDIT);
    g_brAccent = CreateSolidBrush(RHINO_ACCENT);

    g_dark_supported = 1;
}

int rhinossh_is_dark_mode(void) { return g_dark_supported; }

void rhinossh_apply_dark_titlebar(HWND hwnd)
{
    BOOL dark = TRUE;

    if (!g_dark_supported || !hwnd) return;

    if (pAllowDarkWin)
        pAllowDarkWin(hwnd, TRUE);

    if (!pDwmSetAttr)
        return;

    /* ---- 1. Dark immersive titlebar (Win10 1809+) ---- */
    if (FAILED(pDwmSetAttr(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE,
                            &dark, sizeof(dark))))
        pDwmSetAttr(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE_OLD,
                     &dark, sizeof(dark));

    /* ---- 2. Win11-only attributes ---- */
    if (has_win11_dwm()) {
        COLORREF caption = RHINO_BG_HEADER;     /* dark caption fill */
        COLORREF text    = RHINO_TEXT;          /* white-ish caption text */
        COLORREF border  = RHINO_ACCENT;        /* cyan window border */
        DWORD    corner  = DWMWCP_ROUND;        /* rounded corners */

        pDwmSetAttr(hwnd, DWMWA_CAPTION_COLOR, &caption, sizeof(caption));
        pDwmSetAttr(hwnd, DWMWA_TEXT_COLOR,    &text,    sizeof(text));
        pDwmSetAttr(hwnd, DWMWA_BORDER_COLOR,  &border,  sizeof(border));
        pDwmSetAttr(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE,
                          &corner, sizeof(corner));

        /* Start breathing glow on the border for this window */
        rhinossh_register_breathing_border(hwnd);
    }
}

void rhinossh_apply_dark_dialog(HWND hwnd)
{
    HWND child;
    if (!g_dark_supported || !hwnd) return;

    rhinossh_apply_dark_titlebar(hwnd);
    if (pAllowDarkWin) pAllowDarkWin(hwnd, TRUE);

    EnumChildWindows(hwnd, dark_child_cb, 0);

    /* TreeView explicit colors */
    child = NULL;
    while ((child = FindWindowEx(hwnd, child, WC_TREEVIEW, NULL)) != NULL) {
        TreeView_SetBkColor   (child, RHINO_BG_DEEP);
        TreeView_SetTextColor (child, RHINO_TEXT);
        TreeView_SetLineColor (child, RHINO_BORDER);
        SetWindowTheme        (child, L"DarkMode_Explorer", NULL);
    }

    /* ListView explicit colors */
    child = NULL;
    while ((child = FindWindowEx(hwnd, child, WC_LISTVIEW, NULL)) != NULL) {
        ListView_SetBkColor     (child, RHINO_BG_DEEP);
        ListView_SetTextBkColor (child, RHINO_BG_DEEP);
        ListView_SetTextColor   (child, RHINO_TEXT);
        SetWindowTheme          (child, L"DarkMode_Explorer", NULL);
    }

    if (pFlushMenus) pFlushMenus();

    /* Phase 2: every dialog gets the modernui flat-button treatment
     * (cyan radios + checkboxes + push buttons), not just the main
     * config dialog. SubclassButtons is idempotent — safe to call even
     * if dialog.c also explicitly calls modernui_apply_flat_buttons. */
    modernui_apply_flat_buttons(hwnd);

    /* One pass that repaints both client AND non-client (frame) for
     * the dialog and every child. Triggers DarkBorderSubProc on each
     * subclassed control's NC area in a single, coalesced redraw —
     * no cascading SetWindowPos calls during enumeration. */
    RedrawWindow(hwnd, NULL, NULL,
                 RDW_FRAME | RDW_INVALIDATE | RDW_ALLCHILDREN |
                 RDW_UPDATENOW | RDW_ERASE);
}

HBRUSH rhinossh_dark_ctlcolor(HDC hdc, HWND hwndCtl, UINT msg)
{
    (void)hwndCtl;
    if (!g_dark_supported) return NULL;

    switch (msg) {
    case WM_CTLCOLORDLG:
        SetTextColor(hdc, RHINO_TEXT);
        SetBkColor  (hdc, RHINO_BG_DEEP);
        return g_brBg;

    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
        SetTextColor(hdc, RHINO_TEXT);
        SetBkMode   (hdc, TRANSPARENT);
        return g_brBg;

    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
        SetTextColor(hdc, RHINO_TEXT);
        SetBkColor  (hdc, RHINO_BG_EDIT);
        return g_brEdit;

    case WM_CTLCOLORSCROLLBAR:
        return g_brSurf;
    }
    return NULL;
}

/* ============================================================
 *  BREATHING GLOW — animated window border
 *
 *  A single 50ms timer cycles a blended cyan color on the DWM
 *  border of every registered HWND. Unfocused windows get the
 *  static dim color; focused windows pulse between dim and bright
 *  on a ~3 second sine loop.
 *
 *  Only works on Win11 (DWMWA_BORDER_COLOR). No-op on older.
 * ============================================================ */

#include <math.h>

#define GLOW_MAX_WINDOWS   32
#define GLOW_TICK_MS       50
#define GLOW_CYCLE_MS      1800   /* full breathe period */
#define GLOW_TIMER_ID      0x5257 /* 'RW' */

/* Dim and bright endpoints — wider swing for a more visible breathe. */
#define GLOW_DIM_R   20
#define GLOW_DIM_G   140
#define GLOW_DIM_B   210
#define GLOW_BRT_R   180
#define GLOW_BRT_G   240
#define GLOW_BRT_B   255

static HWND   g_glow_hwnds[GLOW_MAX_WINDOWS] = { 0 };
static int    g_glow_count    = 0;
static HWND   g_glow_timer_hwnd = NULL;   /* message-only host for timer */
static double g_glow_phase    = 0.0;      /* 0..2pi */
static COLORREF g_glow_last[GLOW_MAX_WINDOWS] = { 0 };  /* last color set per hwnd */

static COLORREF glow_color_at(double phase01)
{
    /* phase01 in [0,1]. Map to sine in [0,1]. */
    double s = (sin(phase01 * 2.0 * 3.14159265358979323846) + 1.0) * 0.5;
    int r = (int)(GLOW_DIM_R + (GLOW_BRT_R - GLOW_DIM_R) * s);
    int g = (int)(GLOW_DIM_G + (GLOW_BRT_G - GLOW_DIM_G) * s);
    int b = (int)(GLOW_DIM_B + (GLOW_BRT_B - GLOW_DIM_B) * s);
    return RGB(r, g, b);
}

static void glow_apply(HWND hwnd, COLORREF c)
{
    if (!pDwmSetAttr || !hwnd) return;
    pDwmSetAttr(hwnd, DWMWA_BORDER_COLOR, &c, sizeof(c));
}

static void CALLBACK glow_timer_proc(HWND hwnd, UINT msg, UINT_PTR id, DWORD t)
{
    (void)hwnd; (void)msg; (void)id; (void)t;

    /* Advance phase by (tick / cycle) of a full turn */
    g_glow_phase += (double)GLOW_TICK_MS / (double)GLOW_CYCLE_MS;
    if (g_glow_phase >= 1.0) g_glow_phase -= 1.0;

    COLORREF lit = glow_color_at(g_glow_phase);
    COLORREF dim = RGB(GLOW_DIM_R, GLOW_DIM_G, GLOW_DIM_B);
    HWND fg = GetForegroundWindow();

    /* Walk the list; compact over dead HWNDs in the same pass */
    int w = 0;
    for (int r = 0; r < g_glow_count; r++) {
        HWND h = g_glow_hwnds[r];
        if (!h || !IsWindow(h)) continue;

        COLORREF desired = (h == fg) ? lit : dim;
        /* Skip calling DWM if the color hasn't changed (unfocused
         * windows just stay static after one call) */
        if (g_glow_last[w] != desired) {
            glow_apply(h, desired);
            g_glow_last[w] = desired;
        } else if (w != r) {
            g_glow_last[w] = g_glow_last[r];
        }
        g_glow_hwnds[w] = h;
        w++;
    }
    /* zero out the trailing slots */
    for (int i = w; i < g_glow_count; i++) {
        g_glow_hwnds[i] = NULL;
        g_glow_last[i]  = 0;
    }
    g_glow_count = w;

    /* If everyone's gone, kill the timer */
    if (g_glow_count == 0 && g_glow_timer_hwnd) {
        KillTimer(g_glow_timer_hwnd, GLOW_TIMER_ID);
        DestroyWindow(g_glow_timer_hwnd);
        g_glow_timer_hwnd = NULL;
    }
}

static void glow_start_timer_if_needed(void)
{
    if (g_glow_timer_hwnd) return;
    /* Message-only window to own the timer */
    g_glow_timer_hwnd = CreateWindowExA(
        0, "STATIC", "", 0, 0, 0, 0, 0,
        HWND_MESSAGE, NULL, GetModuleHandle(NULL), NULL);
    if (g_glow_timer_hwnd)
        SetTimer(g_glow_timer_hwnd, GLOW_TIMER_ID, GLOW_TICK_MS, glow_timer_proc);
}

void rhinossh_register_breathing_border(HWND hwnd)
{
    if (!hwnd || !has_win11_dwm() || !pDwmSetAttr) return;
    /* Skip if already registered */
    for (int i = 0; i < g_glow_count; i++)
        if (g_glow_hwnds[i] == hwnd) return;
    if (g_glow_count >= GLOW_MAX_WINDOWS) return;
    g_glow_hwnds[g_glow_count] = hwnd;
    g_glow_last[g_glow_count]  = 0;   /* force first-tick update */
    g_glow_count++;
    glow_start_timer_if_needed();
}

void rhinossh_unregister_breathing_border(HWND hwnd)
{
    if (!hwnd) return;
    for (int i = 0; i < g_glow_count; i++) {
        if (g_glow_hwnds[i] == hwnd) {
            /* Shift tail down */
            for (int j = i; j < g_glow_count - 1; j++) {
                g_glow_hwnds[j] = g_glow_hwnds[j + 1];
                g_glow_last[j]  = g_glow_last[j + 1];
            }
            g_glow_count--;
            g_glow_hwnds[g_glow_count] = NULL;
            g_glow_last[g_glow_count]  = 0;
            return;
        }
    }
}
