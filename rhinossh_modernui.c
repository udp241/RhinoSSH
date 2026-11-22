/*
 * rhinossh_modernui.c - Modern UI enhancements for RhinoSSH
 *
 * 1. Flat button styling with hover/press effects
 * 2. Quick connect bar (hostname + port + Connect)
 * 3. Collapsible sidebar toggle
 */

#include <windows.h>
#include <commctrl.h>
#include <windowsx.h>
#include <string.h>
#include "rhinossh_modernui.h"
#include "rhinossh_darkmode.h"
#include "rhinossh_theme.h"

#ifndef EM_SETCUEBANNER
#define EM_SETCUEBANNER 0x1501
#endif

#ifndef BS_TYPEMASK
#define BS_TYPEMASK 0x0000000FL
#endif

/* ================================================================
 *  1. FLAT BUTTON STYLING
 * ================================================================ */

/* Color palette - sourced from rhinossh_theme.h (matches RhinoSniff brand).
 * Light-mode fallback values left for when dark mode isn't available. */
static COLORREF btn_bg(void) {
    return rhinossh_is_dark_mode() ? RHINO_BG_SURFACE : RGB(225, 225, 225);
}
static COLORREF btn_bg_hover(void) {
    return rhinossh_is_dark_mode() ? RHINO_BG_HOVER   : RGB(210, 210, 215);
}
static COLORREF btn_bg_press(void) {
    return rhinossh_is_dark_mode() ? RHINO_BG_PRESS   : RGB(190, 190, 195);
}
static COLORREF btn_border(void) {
    return rhinossh_is_dark_mode() ? RHINO_BORDER     : RGB(180, 180, 185);
}
static COLORREF btn_border_hover(void) {
    return rhinossh_is_dark_mode() ? RHINO_BORDER_FOCUS : RGB(60, 120, 200);
}
static COLORREF btn_text(void) {
    return rhinossh_is_dark_mode() ? RHINO_TEXT      : RGB(30, 30, 30);
}
static COLORREF btn_text_disabled(void) {
    return rhinossh_is_dark_mode() ? RHINO_TEXT_DISABLED : RGB(160, 160, 160);
}
/* Accent color for primary/default buttons - cyan, matching RhinoSniff */
static COLORREF btn_accent(void) {
    return rhinossh_is_dark_mode() ? RHINO_ACCENT       : RGB(50, 120, 210);
}
static COLORREF btn_accent_hover(void) {
    return rhinossh_is_dark_mode() ? RHINO_ACCENT_HOVER : RGB(40, 100, 190);
}
static COLORREF btn_accent_press(void) {
    return rhinossh_is_dark_mode() ? RHINO_ACCENT_PRESS : RGB(30, 85, 170);
}

/* Per-button state tracking */
#define FLATBTN_SUBCLASS_ID 42100

typedef struct {
    int  hover;
    int  pressed;
    int  is_default;   /* BS_DEFPUSHBUTTON */
} FlatBtnState;

/* ---- Indicator size helpers (radio + checkbox) ----
 * Use system metric so layout matches what dialog templates expect. */
static int rc_indicator_size(void) {
    int sz = GetSystemMetrics(SM_CXMENUCHECK);
    return sz > 0 ? sz : 13;
}
static int rc_text_offset(void) {
    return rc_indicator_size() + GetSystemMetrics(SM_CXEDGE);
}

static COLORREF dialog_bg(void) {
    return rhinossh_is_dark_mode() ? RHINO_BG_DEEP : GetSysColor(COLOR_BTNFACE);
}

static void draw_flat_button(HWND hwnd, HDC hdc, RECT *rc, FlatBtnState *st)
{
    BOOL enabled = IsWindowEnabled(hwnd);
    BOOL is_def  = st->is_default;

    /* Pick colors */
    COLORREF bg, border, text;
    if (!enabled) {
        bg     = btn_bg();
        border = btn_border();
        text   = btn_text_disabled();
    } else if (st->pressed) {
        bg     = is_def ? btn_accent_press() : btn_bg_press();
        border = btn_border_hover();
        text   = is_def ? RHINO_TEXT_ON_ACCENT : btn_text();
    } else if (st->hover) {
        bg     = is_def ? btn_accent_hover() : btn_bg_hover();
        border = btn_border_hover();
        text   = is_def ? RHINO_TEXT_ON_ACCENT : btn_text();
    } else {
        bg     = is_def ? btn_accent() : btn_bg();
        border = is_def ? btn_accent() : btn_border();
        text   = is_def ? RHINO_TEXT_ON_ACCENT : btn_text();
    }

    /* Fill background */
    HBRUSH hBr = CreateSolidBrush(bg);
    FillRect(hdc, rc, hBr);
    DeleteObject(hBr);

    /* Draw 1px border with rounded corners */
    HPEN hPen = CreatePen(PS_SOLID, 1, border);
    HPEN hOld = (HPEN)SelectObject(hdc, hPen);
    HBRUSH hNull = (HBRUSH)GetStockObject(NULL_BRUSH);
    HBRUSH hOldBr = (HBRUSH)SelectObject(hdc, hNull);
    RoundRect(hdc, rc->left, rc->top, rc->right, rc->bottom,
              RHINO_RADIUS, RHINO_RADIUS);
    SelectObject(hdc, hOldBr);
    SelectObject(hdc, hOld);
    DeleteObject(hPen);

    /* Draw text */
    char btntext[256];
    GetWindowTextA(hwnd, btntext, sizeof(btntext));

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, text);

    HFONT hFont = (HFONT)SendMessage(hwnd, WM_GETFONT, 0, 0);
    HFONT hOldFont = hFont ? (HFONT)SelectObject(hdc, hFont) : NULL;

    /* Draw focus rect (subtle dotted inner border) */
    if (GetFocus() == hwnd) {
        RECT frc = *rc;
        InflateRect(&frc, -3, -3);
        DrawFocusRect(hdc, &frc);
    }

    DrawTextA(hdc, btntext, -1, rc,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    if (hOldFont)
        SelectObject(hdc, hOldFont);
}

/* ---- Cyan-accent radio button ---- */
static void draw_flat_radio(HWND hwnd, HDC hdc, RECT *rc, FlatBtnState *st)
{
    BOOL checked = (SendMessage(hwnd, BM_GETCHECK, 0, 0) == BST_CHECKED);
    BOOL enabled = IsWindowEnabled(hwnd);

    /* Erase background to match parent dialog */
    HBRUSH bgBr = CreateSolidBrush(dialog_bg());
    FillRect(hdc, rc, bgBr);
    DeleteObject(bgBr);

    /* Indicator: circle, vertically centered against client rect */
    int sz  = rc_indicator_size();
    int top = rc->top + ((rc->bottom - rc->top - sz) / 2);
    RECT ind = { rc->left, top, rc->left + sz, top + sz };

    COLORREF ringColor;
    if (!enabled)               ringColor = btn_text_disabled();
    else if (checked || st->hover) ringColor = btn_accent();
    else                        ringColor = btn_border();

    /* Outer ring */
    HPEN pen   = CreatePen(PS_SOLID, 1, ringColor);
    HPEN op    = (HPEN)SelectObject(hdc, pen);
    HBRUSH ob  = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Ellipse(hdc, ind.left, ind.top, ind.right, ind.bottom);
    SelectObject(hdc, ob);
    SelectObject(hdc, op);
    DeleteObject(pen);

    /* Inner filled dot when checked */
    if (checked) {
        HBRUSH dot = CreateSolidBrush(enabled ? btn_accent() : btn_text_disabled());
        HBRUSH od  = (HBRUSH)SelectObject(hdc, dot);
        HPEN   np  = (HPEN)SelectObject(hdc, GetStockObject(NULL_PEN));
        int inset = 4;
        Ellipse(hdc, ind.left + inset, ind.top + inset,
                     ind.right - inset, ind.bottom - inset);
        SelectObject(hdc, np);
        SelectObject(hdc, od);
        DeleteObject(dot);
    }

    /* Label */
    char text[256];
    int len = GetWindowTextA(hwnd, text, sizeof(text));
    HFONT f  = (HFONT)SendMessage(hwnd, WM_GETFONT, 0, 0);
    HFONT of = f ? (HFONT)SelectObject(hdc, f) : NULL;

    if (len > 0) {
        RECT tr = { rc->left + rc_text_offset(), rc->top, rc->right, rc->bottom };
        SetTextColor(hdc, enabled ? btn_text() : btn_text_disabled());
        SetBkMode(hdc, TRANSPARENT);
        DrawTextA(hdc, text, len, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        /* Focus rect around label */
        if (GetFocus() == hwnd) {
            SIZE ts;
            GetTextExtentPoint32A(hdc, text, len, &ts);
            RECT frc;
            frc.left   = rc->left + rc_text_offset() - 2;
            frc.top    = rc->top  + (rc->bottom - rc->top - ts.cy) / 2 - 1;
            frc.right  = frc.left + ts.cx + 4;
            frc.bottom = frc.top  + ts.cy + 2;
            DrawFocusRect(hdc, &frc);
        }
    }

    if (of) SelectObject(hdc, of);
}

/* ---- Cyan-accent checkbox (also handles BST_INDETERMINATE) ---- */
static void draw_flat_check(HWND hwnd, HDC hdc, RECT *rc, FlatBtnState *st)
{
    int  state    = (int)SendMessage(hwnd, BM_GETCHECK, 0, 0);
    BOOL checked  = (state == BST_CHECKED);
    BOOL indeterm = (state == BST_INDETERMINATE);
    BOOL enabled  = IsWindowEnabled(hwnd);

    /* Erase background */
    HBRUSH bgBr = CreateSolidBrush(dialog_bg());
    FillRect(hdc, rc, bgBr);
    DeleteObject(bgBr);

    /* Indicator: square, vertically centered */
    int sz  = rc_indicator_size();
    int top = rc->top + ((rc->bottom - rc->top - sz) / 2);
    RECT ind = { rc->left, top, rc->left + sz, top + sz };

    /* Box fill */
    COLORREF fillColor;
    if (!enabled)               fillColor = rhinossh_is_dark_mode() ? RHINO_BG_EDIT : RGB(240, 240, 240);
    else if (checked||indeterm) fillColor = btn_accent();
    else                        fillColor = rhinossh_is_dark_mode() ? RHINO_BG_EDIT : RGB(255, 255, 255);
    HBRUSH boxBr = CreateSolidBrush(fillColor);
    FillRect(hdc, &ind, boxBr);
    DeleteObject(boxBr);

    /* Box border */
    COLORREF borderColor;
    if (!enabled)                  borderColor = btn_text_disabled();
    else if (checked || st->hover) borderColor = btn_accent();
    else                           borderColor = btn_border();
    HBRUSH borderBr = CreateSolidBrush(borderColor);
    FrameRect(hdc, &ind, borderBr);
    DeleteObject(borderBr);

    /* Checkmark (or dash for indeterminate) */
    if (checked || indeterm) {
        COLORREF mark = enabled ? RHINO_TEXT_ON_ACCENT : btn_text_disabled();
        HPEN pen = CreatePen(PS_SOLID, 2, mark);
        HPEN op  = (HPEN)SelectObject(hdc, pen);
        if (indeterm) {
            MoveToEx(hdc, ind.left + 3, ind.top + sz/2, NULL);
            LineTo  (hdc, ind.right - 3, ind.top + sz/2);
        } else {
            POINT pts[3];
            pts[0].x = ind.left  + 3;          pts[0].y = ind.top + sz/2 + 1;
            pts[1].x = ind.left  + sz/2;       pts[1].y = ind.bottom - 4;
            pts[2].x = ind.right - 3;          pts[2].y = ind.top + 3;
            Polyline(hdc, pts, 3);
        }
        SelectObject(hdc, op);
        DeleteObject(pen);
    }

    /* Label */
    char text[256];
    int len = GetWindowTextA(hwnd, text, sizeof(text));
    HFONT f  = (HFONT)SendMessage(hwnd, WM_GETFONT, 0, 0);
    HFONT of = f ? (HFONT)SelectObject(hdc, f) : NULL;

    if (len > 0) {
        RECT tr = { rc->left + rc_text_offset(), rc->top, rc->right, rc->bottom };
        SetTextColor(hdc, enabled ? btn_text() : btn_text_disabled());
        SetBkMode(hdc, TRANSPARENT);
        DrawTextA(hdc, text, len, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        if (GetFocus() == hwnd) {
            SIZE ts;
            GetTextExtentPoint32A(hdc, text, len, &ts);
            RECT frc;
            frc.left   = rc->left + rc_text_offset() - 2;
            frc.top    = rc->top  + (rc->bottom - rc->top - ts.cy) / 2 - 1;
            frc.right  = frc.left + ts.cx + 4;
            frc.bottom = frc.top  + ts.cy + 2;
            DrawFocusRect(hdc, &frc);
        }
    }

    if (of) SelectObject(hdc, of);
}

static LRESULT CALLBACK FlatBtnSubProc(
    HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
    UINT_PTR uId, DWORD_PTR ref)
{
    FlatBtnState *st = (FlatBtnState *)ref;

    switch (msg) {
      case WM_PAINT:
      {
        LONG style = GetWindowLong(hwnd, GWL_STYLE);
        LONG btype = style & BS_TYPEMASK;

        BOOL isPush  = (btype == BS_PUSHBUTTON || btype == BS_DEFPUSHBUTTON ||
                        btype == 0 /* default style */);
        BOOL isRadio = (btype == BS_RADIOBUTTON || btype == BS_AUTORADIOBUTTON);
        BOOL isCheck = (btype == BS_CHECKBOX    || btype == BS_AUTOCHECKBOX  ||
                        btype == BS_3STATE      || btype == BS_AUTO3STATE);

        if (!isPush && !isRadio && !isCheck)
            return DefSubclassProc(hwnd, msg, wParam, lParam);

        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);

        if (isPush) {
            st->is_default = (btype == BS_DEFPUSHBUTTON);
            draw_flat_button(hwnd, hdc, &rc, st);
        } else if (isRadio) {
            draw_flat_radio(hwnd, hdc, &rc, st);
        } else {
            draw_flat_check(hwnd, hdc, &rc, st);
        }

        EndPaint(hwnd, &ps);
        return 0;
      }

      case WM_MOUSEMOVE:
        if (!st->hover) {
            st->hover = 1;
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
            TrackMouseEvent(&tme);
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return DefSubclassProc(hwnd, msg, wParam, lParam);

      case WM_MOUSELEAVE:
        st->hover = 0;
        st->pressed = 0;
        InvalidateRect(hwnd, NULL, FALSE);
        return DefSubclassProc(hwnd, msg, wParam, lParam);

      case WM_LBUTTONDOWN:
        st->pressed = 1;
        InvalidateRect(hwnd, NULL, FALSE);
        return DefSubclassProc(hwnd, msg, wParam, lParam);

      case WM_LBUTTONUP:
        st->pressed = 0;
        InvalidateRect(hwnd, NULL, FALSE);
        return DefSubclassProc(hwnd, msg, wParam, lParam);

      case WM_NCDESTROY:
        RemoveProp(hwnd, "rhinoFlatBtn");
        free(st);
        RemoveWindowSubclass(hwnd, FlatBtnSubProc, uId);
        return DefSubclassProc(hwnd, msg, wParam, lParam);

      case WM_ERASEBKGND:
        return 1;   /* suppress default erase — we paint everything */
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

/* Callback for EnumChildWindows */
static BOOL CALLBACK SubclassButtons(HWND hwnd, LPARAM lParam)
{
    char cls[32];
    (void)lParam;
    GetClassNameA(hwnd, cls, sizeof(cls));

    if (stricmp(cls, "Button") != 0)
        return TRUE;

    LONG style = GetWindowLong(hwnd, GWL_STYLE);
    LONG btype = style & BS_TYPEMASK;

    /* Subclass push buttons, radios, checkboxes — skip groupboxes
     * (groupboxes are handled by darkmode's DarkBtnSubProc). */
    BOOL isPush  = (btype == BS_PUSHBUTTON || btype == BS_DEFPUSHBUTTON ||
                    btype == 0);
    BOOL isRadio = (btype == BS_RADIOBUTTON || btype == BS_AUTORADIOBUTTON);
    BOOL isCheck = (btype == BS_CHECKBOX    || btype == BS_AUTOCHECKBOX  ||
                    btype == BS_3STATE      || btype == BS_AUTO3STATE);
    if (!isPush && !isRadio && !isCheck)
        return TRUE;

    /* Idempotency: bail if we've already subclassed this control.
     * Without this guard a second modernui_apply_flat_buttons call
     * leaks the FlatBtnState struct allocated below. */
    if (GetProp(hwnd, "rhinoFlatBtn"))
        return TRUE;

    FlatBtnState *st = (FlatBtnState *)calloc(1, sizeof(FlatBtnState));
    if (!st) return TRUE;

    SetWindowSubclass(hwnd, FlatBtnSubProc, FLATBTN_SUBCLASS_ID,
                      (DWORD_PTR)st);
    SetProp(hwnd, "rhinoFlatBtn", (HANDLE)1);

    return TRUE;
}

void modernui_apply_flat_buttons(HWND hwnd)
{
    EnumChildWindows(hwnd, SubclassButtons, 0);
}

/* ================================================================
 *  2. QUICK CONNECT BAR
 * ================================================================ */

#define QC_BAR_HEIGHT  30   /* pixels */
#define QC_MARGIN       6

int modernui_create_quickconnect(HWND hwnd, HINSTANCE hinst, HFONT font)
{
    RECT rcClient;
    GetClientRect(hwnd, &rcClient);

    int barW = rcClient.right;
    int y = 2;

    /* "Quick Connect:" label */
    HWND hLabel = CreateWindowExA(0, "STATIC", "Quick Connect:",
        WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE,
        QC_MARGIN, y, 90, QC_BAR_HEIGHT - 4,
        hwnd, (HMENU)(ULONG_PTR)IDC_QCLABEL, hinst, NULL);
    SendMessage(hLabel, WM_SETFONT, (WPARAM)font, TRUE);

    /* Host edit */
    HWND hHost = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        QC_MARGIN + 92, y + 2, barW - 260, QC_BAR_HEIGHT - 8,
        hwnd, (HMENU)(ULONG_PTR)IDC_QCHOST, hinst, NULL);
    SendMessage(hHost, WM_SETFONT, (WPARAM)font, TRUE);
    SendMessage(hHost, EM_SETCUEBANNER, TRUE, (LPARAM)L"hostname or IP");

    /* Port edit */
    HWND hPort = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "22",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_NUMBER,
        barW - 160, y + 2, 45, QC_BAR_HEIGHT - 8,
        hwnd, (HMENU)(ULONG_PTR)IDC_QCPORT, hinst, NULL);
    SendMessage(hPort, WM_SETFONT, (WPARAM)font, TRUE);

    /* Connect button */
    HWND hBtn = CreateWindowExA(0, "BUTTON", "Connect",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        barW - 108, y + 1, 100, QC_BAR_HEIGHT - 6,
        hwnd, (HMENU)(ULONG_PTR)IDC_QCCONNECT, hinst, NULL);
    SendMessage(hBtn, WM_SETFONT, (WPARAM)font, TRUE);

    /* Subclass the Connect button for flat styling */
    FlatBtnState *st = (FlatBtnState *)calloc(1, sizeof(FlatBtnState));
    if (st)
        SetWindowSubclass(hBtn, FlatBtnSubProc, FLATBTN_SUBCLASS_ID,
                          (DWORD_PTR)st);

    /* Apply dark mode to the new controls */
    if (rhinossh_is_dark_mode())
        rhinossh_apply_dark_dialog(hwnd);

    return QC_BAR_HEIGHT;
}

BOOL modernui_get_quickconnect(HWND hwnd, char *host, int hostlen, int *port)
{
    HWND hHost = GetDlgItem(hwnd, IDC_QCHOST);
    HWND hPort = GetDlgItem(hwnd, IDC_QCPORT);
    if (!hHost || !hPort) return FALSE;

    GetWindowTextA(hHost, host, hostlen);
    if (host[0] == '\0') return FALSE;

    char portstr[16];
    GetWindowTextA(hPort, portstr, sizeof(portstr));
    *port = atoi(portstr);
    if (*port <= 0 || *port > 65535)
        *port = 22;

    return TRUE;
}

/* ================================================================
 *  3. COLLAPSIBLE SIDEBAR
 * ================================================================ */

static BOOL g_sidebar_collapsed = FALSE;
static int  g_sidebar_full_width = 0;    /* saved original width */
static int  g_sidebar_full_x = 0;

#define SIDEBAR_COLLAPSED_W  8    /* pixels: just a thin clickable strip */

void modernui_create_sidebar_toggle(HWND hwnd, HINSTANCE hinst, HFONT font)
{
    /* We don't create a separate button — instead, we track the
     * collapsed state and the treeview itself acts as the toggle
     * target when collapsed (click the thin strip to expand).
     * The GenericMainDlgProc in dialog.c will call
     * modernui_toggle_sidebar() when needed. */
    g_sidebar_collapsed = FALSE;
    g_sidebar_full_width = 0;
}

int modernui_toggle_sidebar(HWND hwnd, HWND treeview_hwnd)
{
    RECT tvrc;
    GetWindowRect(treeview_hwnd, &tvrc);
    MapWindowPoints(HWND_DESKTOP, hwnd, (POINT*)&tvrc, 2);

    if (!g_sidebar_collapsed) {
        /* Collapse: save current width, shrink treeview */
        g_sidebar_full_width = tvrc.right - tvrc.left;
        g_sidebar_full_x = tvrc.left;

        /* Shrink treeview to thin strip */
        SetWindowPos(treeview_hwnd, NULL,
                     tvrc.left, tvrc.top,
                     SIDEBAR_COLLAPSED_W, tvrc.bottom - tvrc.top,
                     SWP_NOZORDER);

        /* Hide the "Category:" label */
        HWND hStatic = FindWindowExA(hwnd, NULL, "STATIC", "Cate&gory:");
        if (hStatic) ShowWindow(hStatic, SW_HIDE);

        /* Shift all panel controls left */
        /* Panels are children with x > treeview right edge */
        int shift = g_sidebar_full_width - SIDEBAR_COLLAPSED_W;
        HWND child = GetWindow(hwnd, GW_CHILD);
        while (child) {
            if (child != treeview_hwnd) {
                RECT crc;
                GetWindowRect(child, &crc);
                MapWindowPoints(HWND_DESKTOP, hwnd, (POINT*)&crc, 2);
                if (crc.left > tvrc.left + SIDEBAR_COLLAPSED_W + 5) {
                    SetWindowPos(child, NULL,
                                 crc.left - shift, crc.top,
                                 (crc.right - crc.left) + shift,
                                 crc.bottom - crc.top,
                                 SWP_NOZORDER);
                }
            }
            child = GetWindow(child, GW_HWNDNEXT);
        }

        g_sidebar_collapsed = TRUE;
    } else {
        /* Expand: restore treeview width */
        SetWindowPos(treeview_hwnd, NULL,
                     g_sidebar_full_x, tvrc.top,
                     g_sidebar_full_width, tvrc.bottom - tvrc.top,
                     SWP_NOZORDER);

        /* Show label */
        HWND hStatic = FindWindowExA(hwnd, NULL, "STATIC", "Cate&gory:");
        if (hStatic) ShowWindow(hStatic, SW_SHOW);

        /* Shift panels back */
        int shift = g_sidebar_full_width - SIDEBAR_COLLAPSED_W;
        HWND child = GetWindow(hwnd, GW_CHILD);
        while (child) {
            if (child != treeview_hwnd) {
                RECT crc;
                GetWindowRect(child, &crc);
                MapWindowPoints(HWND_DESKTOP, hwnd, (POINT*)&crc, 2);
                /* These were shifted left, shift them back */
                if (crc.left >= g_sidebar_full_x) {
                    int newW = (crc.right - crc.left) - shift;
                    if (newW < 50) newW = crc.right - crc.left;
                    SetWindowPos(child, NULL,
                                 crc.left + shift, crc.top,
                                 newW, crc.bottom - crc.top,
                                 SWP_NOZORDER);
                }
            }
            child = GetWindow(child, GW_HWNDNEXT);
        }

        g_sidebar_collapsed = FALSE;
    }

    InvalidateRect(hwnd, NULL, TRUE);
    return g_sidebar_collapsed ? SIDEBAR_COLLAPSED_W : g_sidebar_full_width;
}

BOOL modernui_sidebar_collapsed(void)
{
    return g_sidebar_collapsed;
}
