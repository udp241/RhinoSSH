/*
 * rhinossh_content.c - Content overlay for RhinoSSH config dialog
 *
 * Two views:
 *   0 = Quick Connect (host/port/protocol/save)
 *   1 = Saved Sessions (list with connect/delete)
 *
 * Uses PuTTY's Conf + storage API for session management.
 */

#include <windows.h>
#include <commctrl.h>
#include <string.h>
#include <stdio.h>

/* PuTTY headers for Conf, session storage, strbuf */
#include "putty.h"
#include "storage.h"

#include "rhinossh_content.h"
#include "rhinossh_theme.h"
#include "rhinossh_darkmode.h"

#ifndef EM_SETCUEBANNER
#define EM_SETCUEBANNER 0x1501
#endif

/* ============================================================
 *  GEOMETRY
 * ============================================================ */

#define HEADER_H        70
#define CARD_RADIUS      8
#define CARD_PAD_X      18
#define CARD_PAD_Y      16
#define INPUT_H         30
#define LABEL_H         16
#define PILL_H          28
#define PILL_PAD_X      14
#define PILL_GAP         6
#define CARD_GAP        14
#define FOOTER_H        24
#define SIDEBAR_WIDTH   200

#define MAX_PROTOCOLS    6

typedef struct {
    const char *name;
    int protnum;
    RECT rc;
} ProtoItem;

/* ============================================================
 *  STATE
 * ============================================================ */

static HWND  g_dialog       = NULL;
static HWND  g_content_hwnd = NULL;
static Conf *g_conf         = NULL;
static HFONT g_font         = NULL;
static HFONT g_font_title   = NULL;
static HFONT g_font_small   = NULL;
static HFONT g_font_label   = NULL;
static int   g_current_view = 0;

/* Quick Connect controls */
static HWND  g_host_edit    = NULL;
static HWND  g_port_edit    = NULL;
static HWND  g_sess_edit    = NULL;
static HWND  g_save_btn     = NULL;

/* Saved Sessions controls */
static HWND  g_sess_list    = NULL;
static HWND  g_sconn_btn    = NULL;
static HWND  g_sdel_btn     = NULL;
static HWND  g_sload_btn    = NULL;
static HWND  g_sess_search  = NULL;   /* search/filter box above list */

/* Name of the session currently loaded for editing (empty = new session) */
static char  g_loaded_sessname[256] = "";

static int   g_active_proto = 0;
static int   g_hover_proto  = -1;

/* Active session count — periodic EnumWindows count of terminal windows */
static int   g_active_sessions = 0;
#define IDT_SESSION_REFRESH  7001
#define SESSION_REFRESH_MS   1500

static ProtoItem g_protos[MAX_PROTOCOLS] = {
    { "SSH",    PROT_SSH,    {0} },
    { "Telnet", PROT_TELNET, {0} },
    { "Raw",    PROT_RAW,    {0} },
    { "Rlogin", PROT_RLOGIN, {0} },
    { "Serial", PROT_SERIAL, {0} },
#ifdef PROT_ADB
    { "ADB",    PROT_ADB,    {0} },
#else
    { "ADB",    8,           {0} },
#endif
};

static const char *RHINO_CONTENT_CLASS = "RhinoSshContent";

/* ============================================================
 *  SESSION MANAGEMENT
 * ============================================================ */

static void populate_session_list(void)
{
    if (!g_sess_list) return;
    SendMessageA(g_sess_list, LB_RESETCONTENT, 0, 0);

    /* Pull filter text (lowercased) */
    char filter[128] = "";
    if (g_sess_search) {
        GetWindowTextA(g_sess_search, filter, sizeof(filter));
        for (char *p = filter; *p; p++)
            if (*p >= 'A' && *p <= 'Z') *p += 32;
    }

    settings_e *e = enum_settings_start();
    if (!e) return;

    strbuf *sb = strbuf_new();
    while (enum_settings_next(e, sb)) {
        if (sb->s[0] != '\0') {
            BOOL show = TRUE;
            if (filter[0]) {
                /* Case-insensitive substring match */
                char lc[256];
                snprintf(lc, sizeof(lc), "%s", sb->s);
                for (char *p = lc; *p; p++)
                    if (*p >= 'A' && *p <= 'Z') *p += 32;
                show = (strstr(lc, filter) != NULL);
            }
            if (show)
                SendMessageA(g_sess_list, LB_ADDSTRING, 0, (LPARAM)sb->s);
        }
        strbuf_shrink_to(sb, 0);
    }
    strbuf_free(sb);
    enum_settings_finish(e);
}

static void connect_saved_session(void)
{
    if (!g_sess_list || !g_conf || !g_dialog) return;
    int idx = (int)SendMessageA(g_sess_list, LB_GETCURSEL, 0, 0);
    if (idx == LB_ERR) return;

    char name[256];
    if (SendMessageA(g_sess_list, LB_GETTEXT, idx, (LPARAM)name) == LB_ERR)
        return;

    /* Load session into Conf */
    settings_r *sesskey = open_settings_r(name);
    if (sesskey) {
        load_open_settings(sesskey, g_conf);
        close_settings_r(sesskey);
    }

    /* Tell dialog to connect */
    PostMessage(g_dialog, WM_COMMAND,
                MAKEWPARAM(IDC_RHINO_SESSCONNECT, BN_CLICKED), 0);
}

/* Load a saved session into Conf for editing — does NOT connect.
 * After this, every settings view reads from the loaded session,
 * and the Save button in Quick Connect will write back to this name. */
static void load_saved_session(void)
{
    if (!g_sess_list || !g_conf || !g_dialog) return;
    int idx = (int)SendMessageA(g_sess_list, LB_GETCURSEL, 0, 0);
    if (idx == LB_ERR) return;

    char name[256];
    if (SendMessageA(g_sess_list, LB_GETTEXT, idx, (LPARAM)name) == LB_ERR)
        return;

    settings_r *sesskey = open_settings_r(name);
    if (sesskey) {
        load_open_settings(sesskey, g_conf);
        close_settings_r(sesskey);
    } else {
        extern int rhinossh_msgbox(HWND, LPCSTR, LPCSTR, UINT);
        char msg[512];
        snprintf(msg, sizeof(msg), "Could not open session \"%s\".", name);
        rhinossh_msgbox(g_dialog, msg, "RhinoSSH",
                        MB_OK | MB_ICONWARNING);
        return;
    }

    /* Remember which session is being edited */
    snprintf(g_loaded_sessname, sizeof(g_loaded_sessname), "%s", name);

    /* Populate Quick Connect fields too, so the user can save back */
    if (g_host_edit)
        SetWindowTextA(g_host_edit, conf_get_str(g_conf, CONF_host));
    if (g_port_edit) {
        char portstr[16];
        snprintf(portstr, sizeof(portstr), "%d",
                 conf_get_int(g_conf, CONF_port));
        SetWindowTextA(g_port_edit, portstr);
    }
    if (g_sess_edit)
        SetWindowTextA(g_sess_edit, name);
}

static void delete_saved_session(void)
{
    if (!g_sess_list) return;
    int idx = (int)SendMessageA(g_sess_list, LB_GETCURSEL, 0, 0);
    if (idx == LB_ERR) return;

    char name[256];
    if (SendMessageA(g_sess_list, LB_GETTEXT, idx, (LPARAM)name) == LB_ERR)
        return;

    /* Confirm */
    char msg[512];
    snprintf(msg, sizeof(msg), "Delete session \"%s\"?", name);
    extern int rhinossh_msgbox(HWND, LPCSTR, LPCSTR, UINT);
    if (rhinossh_msgbox(g_dialog, msg, "RhinoSSH",
                        MB_YESNO | MB_ICONQUESTION) != IDYES)
        return;

    del_settings(name);
    /* If we're currently editing the deleted session, clear that state */
    if (strcmp(g_loaded_sessname, name) == 0) {
        g_loaded_sessname[0] = '\0';
        if (g_content_hwnd) InvalidateRect(g_content_hwnd, NULL, FALSE);
    }
    populate_session_list();
}

static void save_current_session(void)
{
    if (!g_sess_edit || !g_conf || !g_dialog) return;

    char name[256];
    GetWindowTextA(g_sess_edit, name, sizeof(name));
    if (name[0] == '\0') {
        extern int rhinossh_msgbox(HWND, LPCSTR, LPCSTR, UINT);
        rhinossh_msgbox(g_dialog, "Enter a session name.",
                        "RhinoSSH", MB_OK | MB_ICONINFORMATION);
        return;
    }

    /* Write current Quick Connect values into Conf first */
    char host[512];
    GetWindowTextA(g_host_edit, host, sizeof(host));
    char portstr[16];
    GetWindowTextA(g_port_edit, portstr, sizeof(portstr));
    int port = atoi(portstr);
    if (port <= 0 || port > 65535) port = 22;
    int proto = (g_active_proto >= 0 && g_active_proto < MAX_PROTOCOLS)
                ? g_protos[g_active_proto].protnum : PROT_SSH;

    conf_set_str(g_conf, CONF_host, host);
    conf_set_int(g_conf, CONF_port, port);
    conf_set_int(g_conf, CONF_protocol, proto);

    /* Save to registry */
    char *errmsg = NULL;
    settings_w *sesskey = open_settings_w(name, &errmsg);
    if (sesskey) {
        save_open_settings(sesskey, g_conf);
        close_settings_w(sesskey);
    }
    if (errmsg) sfree(errmsg);

    /* Refresh session list + clear name field */
    populate_session_list();
    SetWindowTextA(g_sess_edit, "");
    /* We're no longer editing the loaded session */
    g_loaded_sessname[0] = '\0';
    if (g_content_hwnd) InvalidateRect(g_content_hwnd, NULL, FALSE);

    extern int rhinossh_msgbox(HWND, LPCSTR, LPCSTR, UINT);
    rhinossh_msgbox(g_dialog, "Session saved.", "RhinoSSH",
                    MB_OK | MB_ICONINFORMATION);
}

/* ============================================================
 *  HIDE PUTTY CONTROLS (called once at create time)
 * ============================================================ */

typedef struct { HWND k1; HWND k2; } HideCtx;

static BOOL CALLBACK hide_putty_cb(HWND hwnd, LPARAM lParam)
{
    HideCtx *ctx = (HideCtx *)lParam;
    if (hwnd == ctx->k1 || hwnd == ctx->k2)
        return TRUE;
    HWND parent = GetParent(hwnd);
    if (parent == g_dialog)
        ShowWindow(hwnd, SW_HIDE);
    return TRUE;
}

static void hide_all_putty_controls(HWND dialog, HWND sidebar, HWND content)
{
    HideCtx ctx = { sidebar, content };
    EnumChildWindows(dialog, hide_putty_cb, (LPARAM)&ctx);
}

/* ============================================================
 *  PAINT HELPERS
 * ============================================================ */

static void fill_round(HDC hdc, RECT *rc, COLORREF color, int radius)
{
    HBRUSH br = CreateSolidBrush(color);
    HPEN pen = CreatePen(PS_SOLID, 1, color);
    HBRUSH ob = (HBRUSH)SelectObject(hdc, br);
    HPEN op = (HPEN)SelectObject(hdc, pen);
    RoundRect(hdc, rc->left, rc->top, rc->right, rc->bottom, radius, radius);
    SelectObject(hdc, ob); SelectObject(hdc, op);
    DeleteObject(br); DeleteObject(pen);
}

static void stroke_round(HDC hdc, RECT *rc, COLORREF color, int radius)
{
    HPEN pen = CreatePen(PS_SOLID, 1, color);
    HPEN op = (HPEN)SelectObject(hdc, pen);
    HBRUSH ob = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
    RoundRect(hdc, rc->left, rc->top, rc->right, rc->bottom, radius, radius);
    SelectObject(hdc, ob); SelectObject(hdc, op);
    DeleteObject(pen);
}

static void paint_label(HDC hdc, RECT *rc, const char *text)
{
    HFONT of = g_font_label ? (HFONT)SelectObject(hdc, g_font_label) : NULL;
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RHINO_TEXT_MUTED);
    DrawTextA(hdc, text, -1, rc, DT_LEFT | DT_TOP | DT_SINGLELINE);
    if (of) SelectObject(hdc, of);
}

static void paint_status_pill(HDC hdc, RECT *rc)
{
    int n = g_active_sessions;
    COLORREF bg, fg, dot_c;
    char label[32];
    if (n <= 0) {
        bg = RGB(40, 44, 56);          /* muted slate */
        fg = RHINO_TEXT_MUTED;
        dot_c = RHINO_TEXT_MUTED;
        snprintf(label, sizeof(label), "NO SESSIONS");
    } else {
        bg = RGB(20, 50, 30);          /* dim green */
        fg = RGB(130, 230, 150);       /* bright green */
        dot_c = RGB(130, 230, 150);
        if (n == 1) snprintf(label, sizeof(label), "1 ACTIVE");
        else        snprintf(label, sizeof(label), "%d ACTIVE", n);
    }
    fill_round(hdc, rc, bg, 10);
    int cy = (rc->top + rc->bottom) / 2;
    HBRUSH dot = CreateSolidBrush(dot_c);
    HBRUSH od = (HBRUSH)SelectObject(hdc, dot);
    HPEN np = (HPEN)SelectObject(hdc, GetStockObject(NULL_PEN));
    Ellipse(hdc, rc->left + 10, cy - 3, rc->left + 16, cy + 3);
    SelectObject(hdc, np); SelectObject(hdc, od);
    DeleteObject(dot);
    HFONT of = g_font_small ? (HFONT)SelectObject(hdc, g_font_small) : NULL;
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, fg);
    RECT tr = *rc; tr.left += 22;
    DrawTextA(hdc, label, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    if (of) SelectObject(hdc, of);
}

/* ============================================================
 *  ACTIVE SESSION COUNTING
 * ============================================================ */

/* EnumWindows callback: count top-level visible windows whose class
 * name matches appname ("PuTTY" by default, or custom via -classname).
 * The config dialog itself uses class "PuTTYConfigBox", so it's naturally
 * excluded from the match. */
static BOOL CALLBACK count_sessions_proc(HWND hwnd, LPARAM lparam)
{
    (void)lparam;
    if (!IsWindowVisible(hwnd)) return TRUE;
    char cls[64];
    int n = GetClassNameA(hwnd, cls, sizeof(cls));
    if (n <= 0) return TRUE;
    /* Match exactly appname; reject "PuTTYConfigBox" etc. */
    if (appname && strcmp(cls, appname) == 0)
        g_active_sessions++;
    return TRUE;
}

static void refresh_session_count(HWND hwnd)
{
    int prev = g_active_sessions;
    g_active_sessions = 0;
    EnumWindows(count_sessions_proc, 0);
    if (prev != g_active_sessions && hwnd) {
        /* Repaint whole window — the pill is in the header and the
         * text is in the footer, both depend on this count. */
        InvalidateRect(hwnd, NULL, FALSE);
    }
}

static void paint_footer(HDC hdc, int w, int h)
{
    RECT rc = { 0, h - FOOTER_H, w, h };
    HBRUSH bg = CreateSolidBrush(RHINO_BG_HEADER);
    FillRect(hdc, &rc, bg); DeleteObject(bg);
    RECT bdr = { 0, rc.top, w, rc.top + 1 };
    HBRUSH bd = CreateSolidBrush(RHINO_BORDER_SOFT);
    FillRect(hdc, &bdr, bd); DeleteObject(bd);
    HFONT of = g_font_small ? (HFONT)SelectObject(hdc, g_font_small) : NULL;
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RHINO_TEXT_MUTED);
    RECT lc = rc; lc.left += 16;

    char status[128];
    int n = g_active_sessions;
    if (n <= 0) {
        snprintf(status, sizeof(status), "Ready  -  No active sessions");
    } else if (n == 1) {
        snprintf(status, sizeof(status), "Ready  -  1 active session");
    } else {
        snprintf(status, sizeof(status), "Ready  -  %d active sessions", n);
    }
    DrawTextA(hdc, status, -1, &lc,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    RECT rcr = rc; rcr.right -= 16;
    DrawTextA(hdc, "v0.83  -  Build 26100", -1, &rcr,
              DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    if (of) SelectObject(hdc, of);
}

/* ============================================================
 *  PROTOCOL PILLS
 * ============================================================ */

static int proto_at(int x, int y)
{
    for (int i = 0; i < MAX_PROTOCOLS; i++)
        if (PtInRect(&g_protos[i].rc, (POINT){x, y}))
            return i;
    return -1;
}

static void paint_pills(HDC hdc, int left, int y)
{
    HFONT of = g_font ? (HFONT)SelectObject(hdc, g_font) : NULL;
    int x = left;
    for (int i = 0; i < MAX_PROTOCOLS; i++) {
        SIZE sz;
        GetTextExtentPoint32A(hdc, g_protos[i].name,
                              (int)strlen(g_protos[i].name), &sz);
        int w = sz.cx + PILL_PAD_X * 2;
        g_protos[i].rc = (RECT){ x, y, x + w, y + PILL_H };
        BOOL active = (i == g_active_proto);
        BOOL hover = (i == g_hover_proto);
        if (active)
            fill_round(hdc, &g_protos[i].rc, RHINO_ACCENT, 6);
        else {
            fill_round(hdc, &g_protos[i].rc, RHINO_BG_EDIT, 6);
            stroke_round(hdc, &g_protos[i].rc,
                         hover ? RHINO_ACCENT : RHINO_BORDER, 6);
        }
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, active ? RHINO_TEXT_ON_ACCENT : RHINO_TEXT);
        DrawTextA(hdc, g_protos[i].name, -1, &g_protos[i].rc,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        x += w + PILL_GAP;
    }
    if (of) SelectObject(hdc, of);
}

/* ============================================================
 *  LAYOUT
 * ============================================================ */

typedef struct {
    RECT title, pill, subtitle;
    RECT card1, card2;
    RECT host_label, host_input, port_label, port_input;
    RECT proto_label;
    int proto_y;
    RECT save_label, save_input, save_btn_rc;
    /* Saved Sessions view */
    RECT sess_list_rc, sconn_btn_rc, sdel_btn_rc, sload_btn_rc, sess_search_rc;
    RECT footer;
} Layout;

static void compute_layout(int w, int h, Layout *L)
{
    int x0 = 28, y = 22;
    L->title    = (RECT){ x0, y, x0 + 200, y + 28 };
    L->pill     = (RECT){ x0 + 210, y + 4, x0 + 340, y + 24 };
    y += 36;
    L->subtitle = (RECT){ x0, y, w - 28, y + 18 };
    y += 28;

    /* Card 1: host/port/protocol + save-name row */
    L->card1 = (RECT){ x0, y, w - 28, 0 };
    int cx = x0 + CARD_PAD_X, cw = (w - 28 - x0) - CARD_PAD_X * 2;
    int cy = y + CARD_PAD_Y;
    L->host_label = (RECT){ cx, cy, cx + cw - 110, cy + LABEL_H };
    L->port_label = (RECT){ cx + cw - 90, cy, cx + cw, cy + LABEL_H };
    cy += LABEL_H + 4;
    L->host_input = (RECT){ cx, cy, cx + cw - 110, cy + INPUT_H };
    L->port_input = (RECT){ cx + cw - 90, cy, cx + cw, cy + INPUT_H };
    cy += INPUT_H + CARD_GAP;
    L->proto_label = (RECT){ cx, cy, cx + cw, cy + LABEL_H };
    cy += LABEL_H + 6;
    L->proto_y = cy;
    cy += PILL_H + CARD_GAP;
    /* Save-name + SAVE button on its own row */
    L->save_label = (RECT){ cx, cy, cx + cw, cy + LABEL_H };
    cy += LABEL_H + 4;
    L->save_input  = (RECT){ cx, cy, cx + cw - 80, cy + INPUT_H };
    L->save_btn_rc = (RECT){ cx + cw - 70, cy, cx + cw, cy + INPUT_H };
    cy += INPUT_H + CARD_PAD_Y;
    L->card1.bottom = cy;

    /* Spacer */
    y = cy + CARD_GAP;

    /* Card 2: saved sessions list + [LOAD] [CONNECT] [DELETE] */
    L->card2 = (RECT){ x0, y, w - 28, 0 };
    int sx = x0 + CARD_PAD_X, sw = (w - 28 - x0) - CARD_PAD_X * 2;
    int sy = y + CARD_PAD_Y;
    /* Search box at the top of the card */
    L->sess_search_rc = (RECT){ sx, sy, sx + sw, sy + INPUT_H };
    sy += INPUT_H + 10;
    int listH = h - FOOTER_H - sy - 60 - CARD_PAD_Y;
    if (listH < 100) listH = 100;
    L->sess_list_rc = (RECT){ sx, sy, sx + sw, sy + listH };
    sy += listH + 10;
    int btnW = 80;
    L->sload_btn_rc = (RECT){ sx + sw - btnW * 3 - 16, sy,
                               sx + sw - btnW * 2 - 16, sy + INPUT_H };
    L->sconn_btn_rc = (RECT){ sx + sw - btnW * 2 - 8, sy,
                               sx + sw - btnW - 8, sy + INPUT_H };
    L->sdel_btn_rc  = (RECT){ sx + sw - btnW, sy,
                               sx + sw, sy + INPUT_H };
    sy += INPUT_H + CARD_PAD_Y;
    L->card2.bottom = sy;

    L->footer = (RECT){ 0, h - FOOTER_H, w, h };
}

/* ============================================================
 *  POSITION CHILD CONTROLS
 * ============================================================ */

static void position_children(HWND hwnd)
{
    RECT crc;
    GetClientRect(hwnd, &crc);
    Layout L;
    compute_layout(crc.right, crc.bottom, &L);

    #define SETPOS(h, r) if (h) SetWindowPos(h, NULL, \
        (r).left+1, (r).top+1, (r).right-(r).left-2, (r).bottom-(r).top-2, \
        SWP_NOZORDER | SWP_NOACTIVATE)

    /* Card 1: Quick connect fields + save-name row */
    SETPOS(g_host_edit, L.host_input);
    SETPOS(g_port_edit, L.port_input);
    SETPOS(g_sess_edit, L.save_input);
    if (g_save_btn) SetWindowPos(g_save_btn, NULL,
        L.save_btn_rc.left, L.save_btn_rc.top,
        L.save_btn_rc.right - L.save_btn_rc.left,
        L.save_btn_rc.bottom - L.save_btn_rc.top,
        SWP_NOZORDER | SWP_NOACTIVATE);

    /* Card 2: Saved sessions list + 3 buttons */
    if (g_sess_search) SetWindowPos(g_sess_search, NULL,
        L.sess_search_rc.left, L.sess_search_rc.top,
        L.sess_search_rc.right - L.sess_search_rc.left,
        L.sess_search_rc.bottom - L.sess_search_rc.top,
        SWP_NOZORDER | SWP_NOACTIVATE);
    if (g_sess_list) SetWindowPos(g_sess_list, NULL,
        L.sess_list_rc.left, L.sess_list_rc.top,
        L.sess_list_rc.right - L.sess_list_rc.left,
        L.sess_list_rc.bottom - L.sess_list_rc.top,
        SWP_NOZORDER | SWP_NOACTIVATE);
    if (g_sconn_btn) SetWindowPos(g_sconn_btn, NULL,
        L.sconn_btn_rc.left, L.sconn_btn_rc.top,
        L.sconn_btn_rc.right - L.sconn_btn_rc.left,
        L.sconn_btn_rc.bottom - L.sconn_btn_rc.top,
        SWP_NOZORDER | SWP_NOACTIVATE);
    if (g_sdel_btn) SetWindowPos(g_sdel_btn, NULL,
        L.sdel_btn_rc.left, L.sdel_btn_rc.top,
        L.sdel_btn_rc.right - L.sdel_btn_rc.left,
        L.sdel_btn_rc.bottom - L.sdel_btn_rc.top,
        SWP_NOZORDER | SWP_NOACTIVATE);
    if (g_sload_btn) SetWindowPos(g_sload_btn, NULL,
        L.sload_btn_rc.left, L.sload_btn_rc.top,
        L.sload_btn_rc.right - L.sload_btn_rc.left,
        L.sload_btn_rc.bottom - L.sload_btn_rc.top,
        SWP_NOZORDER | SWP_NOACTIVATE);
    #undef SETPOS
}

/* ============================================================
 *  VIEW SWITCHING
 * ============================================================ */

static void show_view_controls(int view)
{
    (void)view;   /* unified view — always show everything */
    if (g_host_edit) ShowWindow(g_host_edit, SW_SHOW);
    if (g_port_edit) ShowWindow(g_port_edit, SW_SHOW);
    if (g_sess_edit) ShowWindow(g_sess_edit, SW_SHOW);
    if (g_save_btn)  ShowWindow(g_save_btn, SW_SHOW);
    if (g_sess_list) ShowWindow(g_sess_list, SW_SHOW);
    if (g_sconn_btn) ShowWindow(g_sconn_btn, SW_SHOW);
    if (g_sdel_btn)  ShowWindow(g_sdel_btn, SW_SHOW);
    if (g_sload_btn) ShowWindow(g_sload_btn, SW_SHOW);
    if (g_sess_search) ShowWindow(g_sess_search, SW_SHOW);
}

void rhinossh_content_switch_view(int view)
{
    if (!g_content_hwnd) return;
    g_current_view = view;
    show_view_controls(view);
    populate_session_list();   /* refresh list every time */
    position_children(g_content_hwnd);
    InvalidateRect(g_content_hwnd, NULL, TRUE);
}

/* ============================================================
 *  WINDOW PROC
 * ============================================================ */

static LRESULT CALLBACK ContentWndProc(HWND hwnd, UINT msg,
                                       WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
      case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT crc;
        GetClientRect(hwnd, &crc);
        int w = crc.right, h = crc.bottom;

        HBRUSH bg = CreateSolidBrush(RHINO_BG_DEEP);
        FillRect(hdc, &crc, bg); DeleteObject(bg);

        Layout L;
        compute_layout(w, h, &L);

        /* Title: "Sessions" always */
        HFONT of = g_font_title ? (HFONT)SelectObject(hdc, g_font_title) : NULL;
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RHINO_TEXT);
        DrawTextA(hdc, "Sessions", -1, &L.title,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        if (of) SelectObject(hdc, of);

        /* Status pill — green "N active" */
        paint_status_pill(hdc, &L.pill);

        /* Subtitle */
        of = g_font_small ? (HFONT)SelectObject(hdc, g_font_small) : NULL;
        SetTextColor(hdc, RHINO_TEXT_MUTED);
        char subbuf[512];
        const char *sub;
        if (g_loaded_sessname[0]) {
            snprintf(subbuf, sizeof(subbuf),
                     "Editing saved session \"%s\" — change settings, then click SAVE to update.",
                     g_loaded_sessname);
            sub = subbuf;
        } else {
            sub = "Connect to a host, or pick a saved session below.";
        }
        DrawTextA(hdc, sub, -1, &L.subtitle,
                  DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);
        if (of) SelectObject(hdc, of);

        /* Card 1 — Quick connect + save-name */
        fill_round(hdc, &L.card1, RHINO_BG_SURFACE, CARD_RADIUS);
        stroke_round(hdc, &L.card1, RHINO_BORDER, CARD_RADIUS);
        paint_label(hdc, &L.host_label, "HOST");
        paint_label(hdc, &L.port_label, "PORT");
        stroke_round(hdc, &L.host_input, RHINO_BORDER, 4);
        stroke_round(hdc, &L.port_input, RHINO_BORDER, 4);
        paint_label(hdc, &L.proto_label, "PROTOCOL");
        paint_pills(hdc, L.card1.left + CARD_PAD_X, L.proto_y);
        /* Save-name row */
        paint_label(hdc, &L.save_label, "SAVE AS");
        stroke_round(hdc, &L.save_input, RHINO_BORDER, 4);

        /* Card 2 — Saved sessions list + buttons */
        if (L.card2.bottom > L.card2.top) {
            fill_round(hdc, &L.card2, RHINO_BG_SURFACE, CARD_RADIUS);
            stroke_round(hdc, &L.card2, RHINO_BORDER, CARD_RADIUS);
            /* Section label above the search box */
            RECT slabel = L.sess_search_rc;
            slabel.bottom = slabel.top - 4;
            slabel.top = slabel.bottom - LABEL_H;
            paint_label(hdc, &slabel, "SAVED SESSIONS");
            stroke_round(hdc, &L.sess_search_rc, RHINO_BORDER, 4);
        }

        paint_footer(hdc, w, h);
        EndPaint(hwnd, &ps);
        return 0;
      }

      case WM_COMMAND: {
        WORD id = LOWORD(wParam);
        WORD notif = HIWORD(wParam);
        if (id == IDC_RHINO_SAVE && notif == BN_CLICKED) {
            save_current_session();
            return 0;
        }
        if (id == IDC_RHINO_SESSCONNECT && notif == BN_CLICKED) {
            connect_saved_session();
            return 0;
        }
        if (id == IDC_RHINO_SESSDELETE && notif == BN_CLICKED) {
            delete_saved_session();
            return 0;
        }
        if (id == IDC_RHINO_SESSLOAD && notif == BN_CLICKED) {
            load_saved_session();
            return 0;
        }
        if (id == IDC_RHINO_SESSLIST && notif == LBN_DBLCLK) {
            /* Double-click opens/connects to the session (original behavior) */
            connect_saved_session();
            return 0;
        }
        if (id == IDC_RHINO_SESSSEARCH && notif == EN_CHANGE) {
            populate_session_list();
            return 0;
        }
        break;
      }

      case WM_LBUTTONDOWN: {
        int x = (short)LOWORD(lParam);
        int y = (short)HIWORD(lParam);
        if (g_current_view == RHINO_VIEW_QUICKCONNECT) {
            int p = proto_at(x, y);
            if (p >= 0 && p != g_active_proto) {
                g_active_proto = p;
                InvalidateRect(hwnd, NULL, FALSE);
            }
        }
        return 0;
      }

      case WM_MOUSEMOVE: {
        int x = (short)LOWORD(lParam);
        int y = (short)HIWORD(lParam);
        int p = (g_current_view == RHINO_VIEW_QUICKCONNECT)
                ? proto_at(x, y) : -1;
        if (p != g_hover_proto) {
            g_hover_proto = p;
            InvalidateRect(hwnd, NULL, FALSE);
            if (p >= 0) {
                TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
                TrackMouseEvent(&tme);
            }
        }
        return 0;
      }

      case WM_MOUSELEAVE:
        g_hover_proto = -1;
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;

      case WM_TIMER:
        if (wParam == IDT_SESSION_REFRESH) {
            refresh_session_count(hwnd);
        }
        return 0;

      case WM_SIZE:
        position_children(hwnd);
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;

      case WM_CTLCOLOREDIT:
      case WM_CTLCOLORSTATIC:
      case WM_CTLCOLORBTN:
      case WM_CTLCOLORLISTBOX: {
        HDC hdc = (HDC)wParam;
        SetTextColor(hdc, RHINO_TEXT);
        SetBkMode(hdc, OPAQUE);
        BOOL isEdit = (msg == WM_CTLCOLOREDIT || msg == WM_CTLCOLORLISTBOX);
        SetBkColor(hdc, isEdit ? RHINO_BG_EDIT : RHINO_BG_DEEP);
        static HBRUSH br_edit = NULL, br_dlg = NULL;
        if (!br_edit) br_edit = CreateSolidBrush(RHINO_BG_EDIT);
        if (!br_dlg)  br_dlg  = CreateSolidBrush(RHINO_BG_DEEP);
        return (LRESULT)(isEdit ? br_edit : br_dlg);
      }

      case WM_ERASEBKGND:
        return 1;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

/* ============================================================
 *  CREATE / DESTROY
 * ============================================================ */

static void register_class_once(HINSTANCE hinst)
{
    static BOOL registered = FALSE;
    if (registered) return;
    WNDCLASSEXA wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = ContentWndProc;
    wc.hInstance = hinst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.lpszClassName = RHINO_CONTENT_CLASS;
    RegisterClassExA(&wc);
    registered = TRUE;
}

void rhinossh_content_create(HWND dialog, HWND sidebar, HFONT font,
                             void *conf)
{
    if (!dialog) return;

    g_dialog = dialog;
    g_font = font;
    g_conf = (Conf *)conf;
    g_active_proto = 0;
    g_hover_proto = -1;
    g_current_view = RHINO_VIEW_QUICKCONNECT;

    HINSTANCE hinst = (HINSTANCE)GetWindowLongPtr(dialog, GWLP_HINSTANCE);
    register_class_once(hinst);

    g_font_title = CreateFontW(-18, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                DEFAULT_PITCH | FF_DONTCARE,
                                L"Segoe UI Variable");
    g_font_small = CreateFontW(-11, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                DEFAULT_PITCH | FF_DONTCARE,
                                L"Segoe UI Variable");
    g_font_label = CreateFontW(-10, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                DEFAULT_PITCH | FF_DONTCARE,
                                L"Segoe UI Variable");

    RECT dr;
    GetClientRect(dialog, &dr);
    int cx = SIDEBAR_WIDTH, cy = 0;
    int cw = dr.right - SIDEBAR_WIDTH, ch = dr.bottom;

    g_content_hwnd = CreateWindowExA(
        WS_EX_COMPOSITED, RHINO_CONTENT_CLASS, "",
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
        cx, cy, cw, ch, dialog, NULL, hinst, NULL);

    if (!g_content_hwnd) return;

    /* Prime the session count immediately, then refresh every 1.5s */
    refresh_session_count(g_content_hwnd);
    SetTimer(g_content_hwnd, IDT_SESSION_REFRESH, SESSION_REFRESH_MS, NULL);

    /* ---- Quick Connect controls ---- */
    g_host_edit = CreateWindowExA(0, "EDIT", "",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        0, 0, 100, INPUT_H,
        g_content_hwnd, (HMENU)(ULONG_PTR)IDC_RHINO_HOST, hinst, NULL);
    g_port_edit = CreateWindowExA(0, "EDIT", "22",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | ES_NUMBER,
        0, 0, 80, INPUT_H,
        g_content_hwnd, (HMENU)(ULONG_PTR)IDC_RHINO_PORT, hinst, NULL);
    g_sess_edit = CreateWindowExA(0, "EDIT", "",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        0, 0, 200, INPUT_H,
        g_content_hwnd, (HMENU)(ULONG_PTR)IDC_RHINO_SESSNAME, hinst, NULL);
    g_save_btn = CreateWindowExA(0, "BUTTON", "SAVE",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        0, 0, 60, INPUT_H,
        g_content_hwnd, (HMENU)(ULONG_PTR)IDC_RHINO_SAVE, hinst, NULL);

    /* ---- Saved Sessions controls (initially hidden) ---- */
    g_sess_list = CreateWindowExA(WS_EX_CLIENTEDGE, "LISTBOX", "",
        WS_CHILD | WS_TABSTOP | WS_VSCROLL |
        LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
        0, 0, 200, 200,
        g_content_hwnd, (HMENU)(ULONG_PTR)IDC_RHINO_SESSLIST, hinst, NULL);
    g_sconn_btn = CreateWindowExA(0, "BUTTON", "Connect",
        WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON | BS_DEFPUSHBUTTON,
        0, 0, 80, INPUT_H,
        g_content_hwnd, (HMENU)(ULONG_PTR)IDC_RHINO_SESSCONNECT, hinst, NULL);
    g_sdel_btn = CreateWindowExA(0, "BUTTON", "Delete",
        WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON,
        0, 0, 80, INPUT_H,
        g_content_hwnd, (HMENU)(ULONG_PTR)IDC_RHINO_SESSDELETE, hinst, NULL);
    g_sload_btn = CreateWindowExA(0, "BUTTON", "Load",
        WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON,
        0, 0, 80, INPUT_H,
        g_content_hwnd, (HMENU)(ULONG_PTR)IDC_RHINO_SESSLOAD, hinst, NULL);
    g_sess_search = CreateWindowExA(0, "EDIT", "",
        WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL,
        0, 0, 200, INPUT_H,
        g_content_hwnd, (HMENU)(ULONG_PTR)IDC_RHINO_SESSSEARCH, hinst, NULL);

    /* Set fonts */
    HWND ctrls[] = { g_host_edit, g_port_edit, g_sess_edit, g_save_btn,
                     g_sess_list, g_sconn_btn, g_sdel_btn, g_sload_btn,
                     g_sess_search };
    for (int i = 0; i < 9; i++)
        if (ctrls[i] && font)
            SendMessage(ctrls[i], WM_SETFONT, (WPARAM)font, TRUE);

    SendMessageW(g_host_edit, EM_SETCUEBANNER, TRUE,
                 (LPARAM)L"hostname or IP");
    SendMessageW(g_sess_edit, EM_SETCUEBANNER, TRUE,
                 (LPARAM)L"Session name (optional)");
    SendMessageW(g_sess_search, EM_SETCUEBANNER, TRUE,
                 (LPARAM)L"Search saved sessions...");

    /* Show all controls (unified view) */
    show_view_controls(RHINO_VIEW_QUICKCONNECT);

    /* Populate the saved sessions list right away — unified view shows
     * it on load, not just when the user navigates to it. */
    populate_session_list();

    /* Hide PuTTY controls underneath */
    hide_all_putty_controls(dialog, sidebar, g_content_hwnd);

    position_children(g_content_hwnd);
    InvalidateRect(g_content_hwnd, NULL, TRUE);
}

void rhinossh_content_destroy(void)
{
    if (g_content_hwnd) KillTimer(g_content_hwnd, IDT_SESSION_REFRESH);
    if (g_font_title) { DeleteObject(g_font_title); g_font_title = NULL; }
    if (g_font_small) { DeleteObject(g_font_small); g_font_small = NULL; }
    if (g_font_label) { DeleteObject(g_font_label); g_font_label = NULL; }
    g_dialog = NULL; g_content_hwnd = NULL; g_conf = NULL;
    g_host_edit = g_port_edit = g_sess_edit = g_save_btn = NULL;
    g_sess_list = g_sconn_btn = g_sdel_btn = g_sload_btn = NULL;
    g_sess_search = NULL;
    g_current_view = 0;
}

/* Getters for Quick Connect values */
void rhinossh_content_apply_to_putty(HWND dialog) { (void)dialog; }

int rhinossh_content_get_host(char *buf, int buflen)
{
    if (!g_host_edit || !buf || buflen <= 0) return 0;
    return GetWindowTextA(g_host_edit, buf, buflen);
}

int rhinossh_content_get_port(void)
{
    if (!g_port_edit) return 22;
    char buf[16];
    GetWindowTextA(g_port_edit, buf, sizeof(buf));
    int p = atoi(buf);
    return (p > 0 && p <= 65535) ? p : 22;
}

int rhinossh_content_get_protocol(void)
{
    if (g_active_proto >= 0 && g_active_proto < MAX_PROTOCOLS)
        return g_protos[g_active_proto].protnum;
    return PROT_SSH;
}

HWND rhinossh_content_get_hwnd(void)
{
    return g_content_hwnd;
}

/* If a session was loaded for editing, persist any unsaved changes
 * from g_conf back to its session on disk. Called at dialog-close.
 * No-op when nothing was loaded (new Quick Connect sessions shouldn't
 * be silently created). */
void rhinossh_content_autosave_on_close(void)
{
    if (!g_conf || g_loaded_sessname[0] == '\0') return;

    /* Pull current Quick Connect field values into Conf before persisting */
    if (g_host_edit && IsWindow(g_host_edit)) {
        char host[512];
        GetWindowTextA(g_host_edit, host, sizeof(host));
        conf_set_str(g_conf, CONF_host, host);
    }
    if (g_port_edit && IsWindow(g_port_edit)) {
        char portstr[16];
        GetWindowTextA(g_port_edit, portstr, sizeof(portstr));
        int port = atoi(portstr);
        if (port > 0 && port <= 65535)
            conf_set_int(g_conf, CONF_port, port);
    }
    if (g_active_proto >= 0 && g_active_proto < MAX_PROTOCOLS)
        conf_set_int(g_conf, CONF_protocol, g_protos[g_active_proto].protnum);

    char *errmsg = NULL;
    settings_w *sesskey = open_settings_w(g_loaded_sessname, &errmsg);
    if (sesskey) {
        save_open_settings(sesskey, g_conf);
        close_settings_w(sesskey);
    }
    if (errmsg) sfree(errmsg);
}
