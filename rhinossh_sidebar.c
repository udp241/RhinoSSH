/*
 * rhinossh_sidebar.c - Custom branded sidebar for RhinoSSH config dialog
 *
 * Phase 5a of UI overhaul: kills the PuTTY category treeview look,
 * gives the dialog the RhinoSniff sidebar identity. Panel switching
 * still goes through the (hidden) treeview so PuTTY's controlbox
 * machinery is untouched.
 */

#include <windows.h>
#include <commctrl.h>
#include <string.h>
#include <stdio.h>
#include "rhinossh_sidebar.h"
#include "rhinossh_theme.h"
#include "rhinossh_darkmode.h"
#include "rhinossh_modernui.h"
#include "rhinossh_content.h"
#include "rhinossh_settings.h"

/* Show PuTTY's About box (defined in dialog.c) */
extern void modal_about_box(HWND hwnd);

/* ============================================================
 *  LAYOUT CONSTANTS
 * ============================================================ */

#define BRAND_HEIGHT     56
#define CONNECT_PAD      8        /* padding around CONNECT button */
#define CONNECT_HEIGHT   32       /* CONNECT button height (no padding) */
#define CONNECT_AREA     (CONNECT_PAD * 2 + CONNECT_HEIGHT)
#define ITEM_HEIGHT      30
#define SECTION_HEIGHT   22
#define FOOTER_HEIGHT    28
#define ICON_SIZE        14
#define ICON_LEFT        14
#define LABEL_LEFT       (ICON_LEFT + ICON_SIZE + 8)

#define MAX_ITEMS        24

/* ============================================================
 *  ITEM MODEL
 * ============================================================ */

typedef enum {
    ITEM_PATH,            /* nav → treeview pathname */
    ITEM_ACTION_ABOUT,    /* show About dialog */
    ITEM_ACTION_ADVANCED, /* toggle treeview visibility (Phase 5a stub) */
    ITEM_SECTION,         /* uppercase section label */
    ITEM_SETTINGS         /* nav → rhinossh_settings view */
} ItemType;

typedef enum {
    ICO_NONE,
    ICO_LIGHTNING,  /* Quick Connect */
    ICO_FOLDER,     /* Saved Sessions */
    ICO_KEY,        /* SSH Keys */
    ICO_GEAR,       /* Settings (generic) */
    ICO_LOG,        /* Logging */
    ICO_DOTS,       /* Advanced */
    ICO_INFO,       /* About */
    ICO_TERMINAL,   /* Terminal settings */
    ICO_WINDOW,     /* Window settings */
    ICO_LINK,       /* Connection settings */
    ICO_PALETTE,    /* Colours */
    ICO_PROXY,      /* Proxy */
    ICO_BRUSH,      /* Appearance */
    ICO_SLIDER,     /* Behaviour */
    ICO_LINK_CHAIN, /* URLs */
    ICO_BOLT        /* Automation */
} IconType;

typedef struct {
    ItemType  type;
    IconType  icon;
    char      label[48];
    char      path[64];     /* PuTTY pathname (for ITEM_PATH) */
    int       settings_view;/* RHINO_SETTINGS_xxx (for ITEM_SETTINGS) */
    HTREEITEM tv_item;      /* resolved at create time */
    RECT      bounds;       /* y position computed at layout time */
} SidebarItem;

/* ============================================================
 *  MODULE STATE
 * ============================================================ */

static HWND g_dialog       = NULL;
static HWND g_treeview     = NULL;
static HWND g_tv_label     = NULL;
static HWND g_sidebar_hwnd = NULL;
static HWND g_connect_btn  = NULL;
static HFONT g_font        = NULL;
static HFONT g_font_brand  = NULL;
static HFONT g_font_small  = NULL;

static SidebarItem g_items[MAX_ITEMS];
static int g_item_count = 0;
static int g_active_idx = -1;
static int g_hover_idx  = -1;

/* Collapse state */
static BOOL g_collapsed = FALSE;
#define SIDEBAR_W_EXPANDED  200
#define SIDEBAR_W_COLLAPSED  52
#define TOGGLE_BTN_H         24
static RECT g_toggle_rc = {0,0,0,0};
static BOOL g_toggle_hover = FALSE;

static const char *RHINO_SIDEBAR_CLASS = "RhinoSshSidebar";

/* ============================================================
 *  TREEVIEW HELPERS
 * ============================================================ */

/* Recursive walk to find treeview item with a matching pathname
 * stored in lParam. PuTTY sets lParam to the controlset pathname
 * (see dialog.c treeview_insert). */
static HTREEITEM find_tv_item(HWND tv, HTREEITEM start, const char *path)
{
    HTREEITEM item = start ? start : TreeView_GetRoot(tv);
    while (item) {
        TVITEMA ti;
        memset(&ti, 0, sizeof(ti));
        ti.hItem = item;
        ti.mask  = TVIF_PARAM;
        TreeView_GetItem(tv, &ti);
        if (ti.lParam) {
            const char *p = (const char *)ti.lParam;
            if (p && strcmp(p, path) == 0)
                return item;
        }
        HTREEITEM child = TreeView_GetChild(tv, item);
        if (child) {
            HTREEITEM found = find_tv_item(tv, child, path);
            if (found) return found;
        }
        item = TreeView_GetNextSibling(tv, item);
    }
    return NULL;
}

/* ============================================================
 *  ITEM LIST CONSTRUCTION
 * ============================================================ */

static int add_item(ItemType type, IconType icon,
                    const char *label, const char *path)
{
    if (g_item_count >= MAX_ITEMS) return -1;
    SidebarItem *it = &g_items[g_item_count];
    memset(it, 0, sizeof(*it));
    it->type = type;
    it->icon = icon;
    if (label) {
        strncpy(it->label, label, sizeof(it->label) - 1);
        it->label[sizeof(it->label) - 1] = 0;
    }
    if (path) {
        strncpy(it->path, path, sizeof(it->path) - 1);
        it->path[sizeof(it->path) - 1] = 0;
    }
    return g_item_count++;
}

static void layout_items(int sidebar_w)
{
    int y = BRAND_HEIGHT + CONNECT_AREA;
    for (int i = 0; i < g_item_count; i++) {
        SidebarItem *it = &g_items[i];
        int h = (it->type == ITEM_SECTION) ? SECTION_HEIGHT : ITEM_HEIGHT;
        it->bounds.left   = 0;
        it->bounds.top    = y;
        it->bounds.right  = sidebar_w;
        it->bounds.bottom = y + h;
        y += h;
    }
}

static int item_at(int y)
{
    for (int i = 0; i < g_item_count; i++) {
        SidebarItem *it = &g_items[i];
        if (it->type == ITEM_SECTION) continue;
        if (y >= it->bounds.top && y < it->bounds.bottom)
            return i;
    }
    return -1;
}

/* ============================================================
 *  ICON DRAWING (simple GDI vectors at 14x14)
 * ============================================================ */

static void draw_icon(HDC hdc, int x, int y, IconType icon, COLORREF color)
{
    HPEN   pen = CreatePen(PS_SOLID, 2, color);
    HPEN   op  = (HPEN)SelectObject(hdc, pen);
    HBRUSH ob  = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));

    switch (icon) {
      case ICO_LIGHTNING: {
        POINT pts[4] = {
            {x + 9, y + 1}, {x + 2, y + 8}, {x + 7, y + 8}, {x + 5, y + 13}
        };
        Polyline(hdc, pts, 4);
        break;
      }
      case ICO_FOLDER: {
        POINT pts[7] = {
            {x + 1, y + 11}, {x + 1, y + 4},  {x + 5, y + 4},
            {x + 6, y + 5},  {x + 13, y + 5}, {x + 13, y + 11},
            {x + 1, y + 11}
        };
        Polyline(hdc, pts, 7);
        break;
      }
      case ICO_KEY: {
        Ellipse(hdc, x, y + 5, x + 8, y + 13);
        MoveToEx(hdc, x + 8, y + 9, NULL); LineTo(hdc, x + 13, y + 4);
        MoveToEx(hdc, x + 10, y + 6, NULL); LineTo(hdc, x + 12, y + 8);
        break;
      }
      case ICO_GEAR: {
        Ellipse(hdc, x + 3, y + 3, x + 11, y + 11);
        MoveToEx(hdc, x + 7, y,      NULL); LineTo(hdc, x + 7, y + 3);
        MoveToEx(hdc, x + 7, y + 11, NULL); LineTo(hdc, x + 7, y + 14);
        MoveToEx(hdc, x,     y + 7,  NULL); LineTo(hdc, x + 3, y + 7);
        MoveToEx(hdc, x + 11, y + 7, NULL); LineTo(hdc, x + 14, y + 7);
        break;
      }
      case ICO_LOG: {
        MoveToEx(hdc, x + 1, y + 3,  NULL); LineTo(hdc, x + 13, y + 3);
        MoveToEx(hdc, x + 1, y + 7,  NULL); LineTo(hdc, x + 11, y + 7);
        MoveToEx(hdc, x + 1, y + 11, NULL); LineTo(hdc, x + 13, y + 11);
        break;
      }
      case ICO_DOTS: {
        SelectObject(hdc, op);
        DeleteObject(pen);
        HBRUSH br = CreateSolidBrush(color);
        for (int i = 0; i < 3; i++) {
            RECT r = { x + 1 + i * 5, y + 6, x + 4 + i * 5, y + 9 };
            FillRect(hdc, &r, br);
        }
        DeleteObject(br);
        SelectObject(hdc, ob);
        return;
      }
      case ICO_INFO: {
        Ellipse(hdc, x + 1, y + 1, x + 13, y + 13);
        SelectObject(hdc, op);
        DeleteObject(pen);
        HBRUSH br = CreateSolidBrush(color);
        RECT a = { x + 6, y + 3, x + 8, y + 5 };
        FillRect(hdc, &a, br);
        RECT b = { x + 6, y + 6, x + 8, y + 11 };
        FillRect(hdc, &b, br);
        DeleteObject(br);
        SelectObject(hdc, ob);
        return;
      }
      case ICO_TERMINAL: {
        /* Terminal/monitor outline */
        RoundRect(hdc, x, y + 1, x + 14, y + 10, 2, 2);
        MoveToEx(hdc, x + 5, y + 11, NULL); LineTo(hdc, x + 9, y + 11);
        MoveToEx(hdc, x + 7, y + 10, NULL); LineTo(hdc, x + 7, y + 12);
        /* Cursor line inside */
        MoveToEx(hdc, x + 3, y + 5, NULL); LineTo(hdc, x + 7, y + 5);
        break;
      }
      case ICO_WINDOW: {
        /* Window frame with titlebar */
        RoundRect(hdc, x, y + 1, x + 14, y + 13, 2, 2);
        MoveToEx(hdc, x, y + 4, NULL); LineTo(hdc, x + 14, y + 4);
        /* Close dot in titlebar */
        SelectObject(hdc, op);
        DeleteObject(pen);
        HBRUSH br = CreateSolidBrush(color);
        RECT dot = { x + 10, y + 2, x + 12, y + 4 };
        FillRect(hdc, &dot, br);
        DeleteObject(br);
        SelectObject(hdc, ob);
        return;
      }
      case ICO_LINK: {
        /* Chain link / two interlocking ovals */
        Arc(hdc, x, y + 2, x + 9, y + 11, x + 4, y + 2, x + 4, y + 11);
        Arc(hdc, x + 5, y + 2, x + 14, y + 11, x + 10, y + 11, x + 10, y + 2);
        MoveToEx(hdc, x + 4, y + 6, NULL); LineTo(hdc, x + 10, y + 6);
        break;
      }
      case ICO_PALETTE: {
        /* Artist palette — rounded blob with three colour dots */
        RoundRect(hdc, x, y + 2, x + 14, y + 13, 5, 5);
        SelectObject(hdc, op);
        DeleteObject(pen);
        HBRUSH br = CreateSolidBrush(color);
        /* Three colour dots */
        RECT d1 = { x + 3, y + 4, x + 5, y + 6 };
        RECT d2 = { x + 9, y + 4, x + 11, y + 6 };
        RECT d3 = { x + 6, y + 8, x + 8, y + 10 };
        FillRect(hdc, &d1, br);
        FillRect(hdc, &d2, br);
        FillRect(hdc, &d3, br);
        DeleteObject(br);
        SelectObject(hdc, ob);
        return;
      }
      case ICO_PROXY: {
        /* Globe / network — circle with meridian and equator */
        Ellipse(hdc, x, y + 1, x + 14, y + 14);
        MoveToEx(hdc, x, y + 7, NULL);       LineTo(hdc, x + 14, y + 7);
        MoveToEx(hdc, x + 7, y + 1, NULL);   LineTo(hdc, x + 7, y + 14);
        /* Vertical arc to look like a globe */
        Arc(hdc, x + 3, y + 1, x + 11, y + 14,
            x + 7, y + 1, x + 7, y + 14);
        break;
      }
      case ICO_BRUSH: {
        /* Paint brush — handle diagonal, brush tip at lower-left */
        MoveToEx(hdc, x + 12, y + 1, NULL);  LineTo(hdc, x + 7, y + 6);
        MoveToEx(hdc, x + 13, y + 2, NULL);  LineTo(hdc, x + 8, y + 7);
        /* Brush head (filled triangle-ish) */
        SelectObject(hdc, op);
        DeleteObject(pen);
        HBRUSH br = CreateSolidBrush(color);
        POINT tri[3] = {
            {x + 6, y + 7}, {x + 9, y + 10}, {x + 1, y + 13}
        };
        HBRUSH ob2 = (HBRUSH)SelectObject(hdc, br);
        Polygon(hdc, tri, 3);
        SelectObject(hdc, ob2);
        DeleteObject(br);
        SelectObject(hdc, ob);
        return;
      }
      case ICO_SLIDER: {
        /* Two horizontal sliders with knobs */
        MoveToEx(hdc, x + 1, y + 4, NULL);   LineTo(hdc, x + 13, y + 4);
        MoveToEx(hdc, x + 1, y + 10, NULL);  LineTo(hdc, x + 13, y + 10);
        /* Knobs */
        SelectObject(hdc, op);
        DeleteObject(pen);
        HBRUSH br = CreateSolidBrush(color);
        RECT k1 = { x + 8, y + 2, x + 11, y + 7 };
        RECT k2 = { x + 3, y + 8, x + 6, y + 13 };
        FillRect(hdc, &k1, br);
        FillRect(hdc, &k2, br);
        DeleteObject(br);
        SelectObject(hdc, ob);
        return;
      }
      case ICO_LINK_CHAIN: {
        /* Hyperlink — underlined "W" shape (world-web feel) */
        Ellipse(hdc, x + 1, y + 2, x + 7, y + 8);
        Ellipse(hdc, x + 7, y + 6, x + 13, y + 12);
        MoveToEx(hdc, x + 1, y + 13, NULL); LineTo(hdc, x + 13, y + 13);
        break;
      }
      case ICO_BOLT: {
        /* Lightning bolt */
        SelectObject(hdc, op);
        DeleteObject(pen);
        HBRUSH br = CreateSolidBrush(color);
        POINT bolt[7] = {
            {x + 8, y + 1}, {x + 3, y + 7}, {x + 7, y + 7},
            {x + 5, y + 13}, {x + 11, y + 6}, {x + 7, y + 6},
            {x + 8, y + 1}
        };
        HBRUSH ob2 = (HBRUSH)SelectObject(hdc, br);
        Polygon(hdc, bolt, 7);
        SelectObject(hdc, ob2);
        DeleteObject(br);
        SelectObject(hdc, ob);
        return;
      }
      default: break;
    }

    SelectObject(hdc, ob);
    SelectObject(hdc, op);
    DeleteObject(pen);
}

/* ============================================================
 *  PAINT
 * ============================================================ */

static void paint_brand_header(HDC hdc, int w)
{
    RECT rc = { 0, 0, w, BRAND_HEIGHT };
    HBRUSH bg = CreateSolidBrush(RHINO_BG_SURFACE);
    FillRect(hdc, &rc, bg);
    DeleteObject(bg);

    /* Logo box (cyan) with "R" — centered when collapsed */
    int logoLeft = g_collapsed ? (w - 28) / 2 : 12;
    RECT logoRc = { logoLeft, 14, logoLeft + 28, 42 };
    HBRUSH logoBg = CreateSolidBrush(RHINO_ACCENT);
    FillRect(hdc, &logoRc, logoBg);
    DeleteObject(logoBg);

    HFONT logoFont = CreateFontW(-18, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                 DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                 CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                 DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    HFONT of = (HFONT)SelectObject(hdc, logoFont);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RHINO_BG_DEEP);
    DrawTextA(hdc, "R", 1, &logoRc,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(hdc, of);
    DeleteObject(logoFont);

    /* Brand text + version pill — expanded mode only */
    if (!g_collapsed) {
        HFONT bf = g_font_brand ? g_font_brand : g_font;
        of = bf ? (HFONT)SelectObject(hdc, bf) : NULL;
        SetTextColor(hdc, RHINO_TEXT);
        RECT brandRc = { 48, 14, w - 8, 30 };
        DrawTextA(hdc, "RhinoSSH", -1, &brandRc,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        if (g_font_small) SelectObject(hdc, g_font_small);
        RECT pillRc = { 48, 32, 48 + 42, 32 + 14 };
        HBRUSH pillBg = CreateSolidBrush(RHINO_ACCENT_DIM);
        FillRect(hdc, &pillRc, pillBg);
        DeleteObject(pillBg);
        SetTextColor(hdc, RHINO_ACCENT);
        DrawTextA(hdc, "v0.83", -1, &pillRc,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        if (of) SelectObject(hdc, of);
    }

    /* Bottom border */
    RECT bdr = { 0, BRAND_HEIGHT - 1, w, BRAND_HEIGHT };
    HBRUSH bdrBr = CreateSolidBrush(RHINO_BORDER_SOFT);
    FillRect(hdc, &bdr, bdrBr);
    DeleteObject(bdrBr);
}

static void paint_status_footer(HDC hdc, int w, int h)
{
    int y = h - FOOTER_HEIGHT;

    RECT rc = { 0, y, w, h };
    HBRUSH bg = CreateSolidBrush(RHINO_BG_SURFACE);
    FillRect(hdc, &rc, bg);
    DeleteObject(bg);

    /* Top border */
    RECT bdr = { 0, y, w, y + 1 };
    HBRUSH bdrBr = CreateSolidBrush(RHINO_BORDER_SOFT);
    FillRect(hdc, &bdr, bdrBr);
    DeleteObject(bdrBr);

    /* Green status dot — centered when collapsed */
    int dotY = y + (FOOTER_HEIGHT - 6) / 2;
    int dotX = g_collapsed ? (w - 7) / 2 : 12;
    HBRUSH dot = CreateSolidBrush(RHINO_SUCCESS);
    HBRUSH od  = (HBRUSH)SelectObject(hdc, dot);
    HPEN   np  = (HPEN)SelectObject(hdc, GetStockObject(NULL_PEN));
    Ellipse(hdc, dotX, dotY, dotX + 7, dotY + 7);
    SelectObject(hdc, np);
    SelectObject(hdc, od);
    DeleteObject(dot);

    /* Status text — expanded mode only */
    if (!g_collapsed) {
        HFONT sf = g_font_small ? g_font_small : g_font;
        HFONT of = sf ? (HFONT)SelectObject(hdc, sf) : NULL;
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RHINO_TEXT_MUTED);
        RECT txt = { 25, y, w - 8, h };
        DrawTextA(hdc, "Ready", -1, &txt,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        if (of) SelectObject(hdc, of);
    }
}

static void paint_items(HDC hdc, int w)
{
    HFONT itemF = g_font ? g_font : (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    HFONT of    = (HFONT)SelectObject(hdc, itemF);
    SetBkMode(hdc, TRANSPARENT);

    for (int i = 0; i < g_item_count; i++) {
        SidebarItem *it = &g_items[i];

        if (it->type == ITEM_SECTION) {
            if (g_collapsed) continue;   /* hide section labels when collapsed */
            HFONT sf = g_font_small ? g_font_small : itemF;
            HFONT prev = (HFONT)SelectObject(hdc, sf);
            SetTextColor(hdc, RHINO_TEXT_MUTED);
            RECT secRc = it->bounds;
            secRc.left = ICON_LEFT;
            secRc.top += 8;
            DrawTextA(hdc, it->label, -1, &secRc,
                      DT_LEFT | DT_TOP | DT_SINGLELINE);
            SelectObject(hdc, prev);
            continue;
        }

        BOOL is_active = (i == g_active_idx);
        BOOL is_hover  = (i == g_hover_idx);

        if (is_active) {
            HBRUSH ab = CreateSolidBrush(RHINO_ACCENT_DIM);
            FillRect(hdc, &it->bounds, ab);
            DeleteObject(ab);
            RECT bar = { it->bounds.left, it->bounds.top,
                         it->bounds.left + 2, it->bounds.bottom };
            HBRUSH abar = CreateSolidBrush(RHINO_ACCENT);
            FillRect(hdc, &bar, abar);
            DeleteObject(abar);
        } else if (is_hover) {
            HBRUSH hb = CreateSolidBrush(RHINO_BG_HOVER);
            FillRect(hdc, &it->bounds, hb);
            DeleteObject(hb);
        }

        COLORREF iconCol = is_active ? RHINO_ACCENT : RHINO_TEXT_MUTED;
        int iconY = it->bounds.top + (ITEM_HEIGHT - ICON_SIZE) / 2;
        /* Center the icon in collapsed mode, keep left-aligned otherwise */
        int iconX = g_collapsed
            ? (w - ICON_SIZE) / 2
            : ICON_LEFT;
        draw_icon(hdc, iconX, iconY, it->icon, iconCol);

        if (!g_collapsed) {
            SetTextColor(hdc, is_active ? RHINO_ACCENT : RHINO_TEXT);
            RECT lbl = it->bounds;
            lbl.left = LABEL_LEFT;
            DrawTextA(hdc, it->label, -1, &lbl,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        }
    }

    SelectObject(hdc, of);
}

/* Paint the collapse/expand toggle button in the top-right corner.
 * Arrow points left when expanded, right when collapsed. */
static void paint_toggle_btn(HDC hdc, int w)
{
    int pad = 6;
    int btnSize = 20;
    g_toggle_rc.right  = w - pad;
    g_toggle_rc.left   = g_toggle_rc.right - btnSize;
    g_toggle_rc.top    = pad;
    g_toggle_rc.bottom = g_toggle_rc.top + btnSize;
    /* In collapsed mode, center the toggle horizontally */
    if (g_collapsed) {
        g_toggle_rc.left = (w - btnSize) / 2;
        g_toggle_rc.right = g_toggle_rc.left + btnSize;
    }

    if (g_toggle_hover) {
        HBRUSH hb = CreateSolidBrush(RHINO_BG_HOVER);
        FillRect(hdc, &g_toggle_rc, hb);
        DeleteObject(hb);
    }

    /* Chevron: < when expanded (click to collapse), > when collapsed */
    HPEN pen = CreatePen(PS_SOLID, 2,
                         g_toggle_hover ? RHINO_ACCENT : RHINO_TEXT_MUTED);
    HPEN op = (HPEN)SelectObject(hdc, pen);
    int cx = (g_toggle_rc.left + g_toggle_rc.right) / 2;
    int cy = (g_toggle_rc.top + g_toggle_rc.bottom) / 2;
    if (g_collapsed) {
        MoveToEx(hdc, cx - 3, cy - 4, NULL); LineTo(hdc, cx + 3, cy);
        MoveToEx(hdc, cx + 3, cy, NULL);     LineTo(hdc, cx - 3, cy + 4);
    } else {
        MoveToEx(hdc, cx + 3, cy - 4, NULL); LineTo(hdc, cx - 3, cy);
        MoveToEx(hdc, cx - 3, cy, NULL);     LineTo(hdc, cx + 3, cy + 4);
    }
    SelectObject(hdc, op);
    DeleteObject(pen);
}

/* ============================================================
 *  WINDOW PROC
 * ============================================================ */

static LRESULT CALLBACK SidebarWndProc(HWND hwnd, UINT msg,
                                       WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
      case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);
        int w = rc.right - rc.left;
        int h = rc.bottom - rc.top;

        /* Surface background + right separator */
        HBRUSH bg = CreateSolidBrush(RHINO_BG_SURFACE);
        FillRect(hdc, &rc, bg);
        DeleteObject(bg);

        HBRUSH sep = CreateSolidBrush(RHINO_BORDER);
        RECT sepRc = { w - 1, 0, w, h };
        FillRect(hdc, &sepRc, sep);
        DeleteObject(sep);

        paint_brand_header(hdc, w);
        paint_toggle_btn(hdc, w);
        paint_items(hdc, w);
        paint_status_footer(hdc, w, h);

        EndPaint(hwnd, &ps);
        return 0;
      }

      case WM_MOUSEMOVE: {
        int x = (short)LOWORD(lParam);
        int y = (short)HIWORD(lParam);
        POINT pt = {x, y};
        BOOL over_toggle = PtInRect(&g_toggle_rc, pt);
        int idx = over_toggle ? -1 : item_at(y);
        if (over_toggle != g_toggle_hover || idx != g_hover_idx) {
            g_toggle_hover = over_toggle;
            g_hover_idx = idx;
            InvalidateRect(hwnd, NULL, FALSE);
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
            TrackMouseEvent(&tme);
        }
        return 0;
      }

      case WM_MOUSELEAVE:
        g_hover_idx = -1;
        g_toggle_hover = FALSE;
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;

      case WM_LBUTTONDOWN: {
        int x = (short)LOWORD(lParam);
        int y = (short)HIWORD(lParam);
        POINT pt = {x, y};
        /* Toggle button click */
        if (PtInRect(&g_toggle_rc, pt)) {
            rhinossh_sidebar_toggle_collapse();
            return 0;
        }
        int idx = item_at(y);
        if (idx < 0) return 0;

        SidebarItem *it = &g_items[idx];

        if (it->type == ITEM_PATH) {
            g_active_idx = idx;
            InvalidateRect(hwnd, NULL, FALSE);

            /* Hide settings if visible, show content */
            rhinossh_settings_hide();
            {
                extern HWND rhinossh_content_get_hwnd(void);
                HWND ch = rhinossh_content_get_hwnd();
                if (ch) ShowWindow(ch, SW_SHOW);
            }

            /* Switch content view based on nav item */
            if (strcmp(it->path, "Session") == 0) {
                rhinossh_content_switch_view(0);  /* Unified Sessions view */
                SetWindowTextA(g_dialog, "RhinoSSH 0.83 - Sessions");
            }

            /* Treeview panel switch (only if treeview exists) */
            if (g_treeview && IsWindow(g_treeview) && it->tv_item)
                TreeView_SelectItem(g_treeview, it->tv_item);

        } else if (it->type == ITEM_SETTINGS) {
            g_active_idx = idx;
            InvalidateRect(hwnd, NULL, FALSE);

            /* Hide content, show settings */
            {
                extern HWND rhinossh_content_get_hwnd(void);
                HWND ch = rhinossh_content_get_hwnd();
                if (ch) ShowWindow(ch, SW_HIDE);
            }
            rhinossh_settings_switch_view(it->settings_view);

        } else if (it->type == ITEM_ACTION_ABOUT) {
            modal_about_box(g_dialog);
        } else if (it->type == ITEM_ACTION_ADVANCED) {
            /* No treeview in nuclear rebuild — placeholder for now. */
            if (g_treeview && IsWindow(g_treeview)) {
                BOOL vis = IsWindowVisible(g_treeview);
                ShowWindow(g_treeview, vis ? SW_HIDE : SW_SHOW);
                if (g_tv_label)
                    ShowWindow(g_tv_label, vis ? SW_HIDE : SW_SHOW);
                if (!vis) BringWindowToTop(g_treeview);
            }
        }
        return 0;
      }

      case WM_COMMAND:
        if (LOWORD(wParam) == IDC_RHINO_CONNECT) {
            /* Dialog.c handles IDC_RHINO_CONNECT: reads content
             * values, sets Conf, ends dialog. */
            PostMessage(g_dialog, WM_COMMAND,
                        MAKEWPARAM(IDC_RHINO_CONNECT, BN_CLICKED), 0);
            return 0;
        }
        break;

      case WM_CTLCOLORBTN:
      case WM_CTLCOLORSTATIC:
        /* Forward to dialog so darkmode handler colors child controls */
        return SendMessage(g_dialog, msg, wParam, lParam);

      case WM_ERASEBKGND:
        return 1;   /* WM_PAINT handles bg, suppress flicker */
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

/* ============================================================
 *  PUBLIC API
 * ============================================================ */

static void register_class_once(HINSTANCE hinst)
{
    static BOOL registered = FALSE;
    if (registered) return;

    WNDCLASSEXA wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = SidebarWndProc;
    wc.hInstance     = hinst;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.lpszClassName = RHINO_SIDEBAR_CLASS;
    RegisterClassExA(&wc);
    registered = TRUE;
}

void rhinossh_sidebar_create(HWND dialog, HWND treeview, HWND tv_label,
                             HFONT font)
{
    if (!dialog) return;

    g_dialog    = dialog;
    g_treeview  = treeview;
    g_tv_label  = tv_label;
    g_font      = font;
    g_item_count = 0;
    g_active_idx = -1;
    g_hover_idx  = -1;

    HINSTANCE hinst = (HINSTANCE)GetWindowLongPtr(dialog, GWLP_HINSTANCE);
    register_class_once(hinst);

    /* Larger fonts for the brand strip */
    g_font_brand = CreateFontW(-14, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                DEFAULT_PITCH | FF_DONTCARE,
                                L"Segoe UI Variable");
    g_font_small = CreateFontW(-10, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                DEFAULT_PITCH | FF_DONTCARE,
                                L"Segoe UI Variable");

    /* Sidebar fills the entire left side of the dialog at a fixed width,
     * ignoring the old treeview's cramped bounds. */
    RECT dlgClient;
    GetClientRect(dialog, &dlgClient);

    int sx = 0;
    int sy = 0;
    int sw = 200;                          /* fixed sidebar width */
    int sh = dlgClient.bottom;             /* full dialog height */

    if (tv_label && IsWindow(tv_label))
        ShowWindow(tv_label, SW_HIDE);

    /* Hide treeview if it exists (may be NULL if nuclear rebuild
     * destroyed it). */
    if (treeview && IsWindow(treeview))
        ShowWindow(treeview, SW_HIDE);

    /* Create sidebar window over the treeview's slot */
    g_sidebar_hwnd = CreateWindowExA(
        WS_EX_COMPOSITED, RHINO_SIDEBAR_CLASS, "",
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
        sx, sy, sw, sh,
        dialog, NULL, hinst, NULL);

    if (!g_sidebar_hwnd) return;

    /* Build the nav model */
    add_item(ITEM_SECTION, ICO_NONE, "WORKSPACE", NULL);
    int qc_idx = add_item(ITEM_PATH, ICO_FOLDER, "Sessions", "Session");

    add_item(ITEM_SECTION, ICO_NONE, "CONFIGURE", NULL);
    {
        /* Settings views — these use rhinossh_settings, not PuTTY panels */
        int idx;
        idx = add_item(ITEM_SETTINGS, ICO_TERMINAL, "Terminal",   NULL);
        if (idx >= 0) g_items[idx].settings_view = 0;
        idx = add_item(ITEM_SETTINGS, ICO_WINDOW,   "Window",     NULL);
        if (idx >= 0) g_items[idx].settings_view = 1;
        idx = add_item(ITEM_SETTINGS, ICO_LINK,     "Connection", NULL);
        if (idx >= 0) g_items[idx].settings_view = 2;
        idx = add_item(ITEM_SETTINGS, ICO_KEY,      "SSH Keys",   NULL);
        if (idx >= 0) g_items[idx].settings_view = 3;
        idx = add_item(ITEM_SETTINGS, ICO_LOG,      "Logging",    NULL);
        if (idx >= 0) g_items[idx].settings_view = 4;
        idx = add_item(ITEM_SETTINGS, ICO_PALETTE,  "Colours",    NULL);
        if (idx >= 0) g_items[idx].settings_view = 5;
        idx = add_item(ITEM_SETTINGS, ICO_PROXY,    "Proxy",      NULL);
        if (idx >= 0) g_items[idx].settings_view = 6;
        idx = add_item(ITEM_SETTINGS, ICO_BRUSH,    "Appearance", NULL);
        if (idx >= 0) g_items[idx].settings_view = 7;
        idx = add_item(ITEM_SETTINGS, ICO_SLIDER,   "Behaviour",  NULL);
        if (idx >= 0) g_items[idx].settings_view = 8;
        idx = add_item(ITEM_SETTINGS, ICO_LINK_CHAIN,"URLs",      NULL);
        if (idx >= 0) g_items[idx].settings_view = 9;
        idx = add_item(ITEM_SETTINGS, ICO_BOLT,     "Automation", NULL);
        if (idx >= 0) g_items[idx].settings_view = 10;
        idx = add_item(ITEM_SETTINGS, ICO_DOTS,     "Advanced",   NULL);
        if (idx >= 0) g_items[idx].settings_view = 11;
    }
                 add_item(ITEM_ACTION_ABOUT,    ICO_INFO, "About",       NULL);

    /* Resolve treeview HTREEITEMs for path-targeted nav items
     * (only if treeview exists — nuclear rebuild destroys it). */
    if (treeview && IsWindow(treeview)) {
        for (int i = 0; i < g_item_count; i++) {
            if (g_items[i].type == ITEM_PATH && g_items[i].path[0])
                g_items[i].tv_item = find_tv_item(treeview, NULL,
                                                  g_items[i].path);
        }
    }
    g_active_idx = qc_idx;

    layout_items(sw);

    /* Big cyan CONNECT button under the brand strip */
    int btnY = BRAND_HEIGHT + CONNECT_PAD;
    g_connect_btn = CreateWindowExA(
        0, "BUTTON", "CONNECT",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON | BS_DEFPUSHBUTTON,
        12, btnY, sw - 24, CONNECT_HEIGHT,
        g_sidebar_hwnd, (HMENU)(ULONG_PTR)IDC_RHINO_CONNECT, hinst, NULL);
    if (g_connect_btn && font)
        SendMessage(g_connect_btn, WM_SETFONT, (WPARAM)font, TRUE);

    /* Apply modernui flat-button styling so CONNECT picks up cyan
     * accent + 6px radius (matches the rest of the UI). */
    if (g_connect_btn)
        modernui_apply_flat_buttons(g_sidebar_hwnd);

    InvalidateRect(g_sidebar_hwnd, NULL, TRUE);
}

void rhinossh_sidebar_destroy(void)
{
    if (g_font_brand) { DeleteObject(g_font_brand); g_font_brand = NULL; }
    if (g_font_small) { DeleteObject(g_font_small); g_font_small = NULL; }
    g_sidebar_hwnd = NULL;
    g_dialog       = NULL;
    g_treeview     = NULL;
    g_tv_label     = NULL;
    g_connect_btn  = NULL;
    g_item_count   = 0;
    g_active_idx   = -1;
    g_hover_idx    = -1;
    g_collapsed    = FALSE;
    g_toggle_hover = FALSE;
    g_toggle_rc.left = g_toggle_rc.right = g_toggle_rc.top = g_toggle_rc.bottom = 0;
}

HWND rhinossh_sidebar_get_hwnd(void)
{
    return g_sidebar_hwnd;
}

/* Toggle between expanded (200px, labels visible) and collapsed
 * (52px, icons only). Resizes the sidebar, content, and settings
 * windows so the reclaimed space is used by content. */
void rhinossh_sidebar_toggle_collapse(void)
{
    if (!g_sidebar_hwnd || !g_dialog) return;

    g_collapsed = !g_collapsed;
    int new_w = g_collapsed ? SIDEBAR_W_COLLAPSED : SIDEBAR_W_EXPANDED;

    /* Resize sidebar window */
    RECT sr;
    GetWindowRect(g_sidebar_hwnd, &sr);
    POINT tl = { sr.left, sr.top };
    ScreenToClient(g_dialog, &tl);
    int h = sr.bottom - sr.top;
    SetWindowPos(g_sidebar_hwnd, NULL, tl.x, tl.y, new_w, h,
                 SWP_NOZORDER | SWP_NOACTIVATE);

    /* Recompute item rects for the new width */
    layout_items(new_w);

    /* Resize CONNECT button: hide it when collapsed (too narrow for text) */
    if (g_connect_btn) {
        if (g_collapsed) {
            ShowWindow(g_connect_btn, SW_HIDE);
        } else {
            ShowWindow(g_connect_btn, SW_SHOW);
            int btnY = BRAND_HEIGHT + CONNECT_PAD;
            SetWindowPos(g_connect_btn, NULL, 12, btnY,
                         new_w - 24, CONNECT_HEIGHT,
                         SWP_NOZORDER | SWP_NOACTIVATE);
        }
    }

    /* Resize content + settings windows to fill the reclaimed space */
    RECT dr;
    GetClientRect(g_dialog, &dr);
    int right_x = new_w;
    int right_w = dr.right - new_w;

    HWND content = rhinossh_content_get_hwnd();
    if (content) {
        SetWindowPos(content, NULL, right_x, 0, right_w, dr.bottom,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        InvalidateRect(content, NULL, TRUE);
    }
    HWND settings = rhinossh_settings_get_hwnd();
    if (settings) {
        SetWindowPos(settings, NULL, right_x, 0, right_w, dr.bottom,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        InvalidateRect(settings, NULL, TRUE);
    }

    InvalidateRect(g_sidebar_hwnd, NULL, TRUE);
}
