/*
 * rhinossh_msgbox.c - Branded message box for RhinoSSH
 *
 * Replaces stock Win32 MessageBox with a dark-themed dialog that
 * matches the RhinoSSH visual identity. Uses an in-memory dialog
 * template (no .rc needed) and DialogBoxIndirect for modality.
 */

#include <windows.h>
#include <commctrl.h>
#include <string.h>

/* We need the REAL MessageBoxIndirect, not our macro. Temporarily
 * bypass the header's macro hijack. */
#undef MessageBox
#undef MessageBoxA

#include "rhinossh_theme.h"
#include "rhinossh_darkmode.h"
#include "rhinossh_modernui.h"

/* ============================================================
 *  CONSTANTS
 * ============================================================ */

#define IDC_MSG_ICON   3001
#define IDC_MSG_TEXT   3002
#define IDC_MSG_BTN1   3010
#define IDC_MSG_BTN2   3011
#define IDC_MSG_BTN3   3012

#define DLG_W          300
#define DLG_H          120
#define ICON_SZ         32
#define MARGIN          18
#define BTN_W           60
#define BTN_H           18
#define ICON_GAP        16   /* horizontal gap between icon and text */

/* ============================================================
 *  DIALOG STATE (passed via GWL_USERDATA)
 * ============================================================ */

typedef struct {
    UINT   mb_type;
    int    result;
    int    btn_count;
    struct { int id; int retval; const char *label; } btns[3];
    int    icon_type;   /* 0=none, 1=info, 2=warning, 3=error, 4=question */
} MsgBoxState;

static void resolve_buttons(MsgBoxState *s, UINT type)
{
    UINT btns = type & MB_TYPEMASK;
    s->btn_count = 0;

    switch (btns) {
      case MB_OK:
        s->btns[0] = (typeof(s->btns[0])){ IDC_MSG_BTN1, IDOK, "OK" };
        s->btn_count = 1;
        break;
      case MB_OKCANCEL:
        s->btns[0] = (typeof(s->btns[0])){ IDC_MSG_BTN1, IDOK, "OK" };
        s->btns[1] = (typeof(s->btns[1])){ IDC_MSG_BTN2, IDCANCEL, "Cancel" };
        s->btn_count = 2;
        break;
      case MB_YESNO:
        s->btns[0] = (typeof(s->btns[0])){ IDC_MSG_BTN1, IDYES, "Yes" };
        s->btns[1] = (typeof(s->btns[1])){ IDC_MSG_BTN2, IDNO, "No" };
        s->btn_count = 2;
        break;
      case MB_YESNOCANCEL:
        s->btns[0] = (typeof(s->btns[0])){ IDC_MSG_BTN1, IDYES, "Yes" };
        s->btns[1] = (typeof(s->btns[1])){ IDC_MSG_BTN2, IDNO, "No" };
        s->btns[2] = (typeof(s->btns[2])){ IDC_MSG_BTN3, IDCANCEL, "Cancel" };
        s->btn_count = 3;
        break;
      case MB_RETRYCANCEL:
        s->btns[0] = (typeof(s->btns[0])){ IDC_MSG_BTN1, IDRETRY, "Retry" };
        s->btns[1] = (typeof(s->btns[1])){ IDC_MSG_BTN2, IDCANCEL, "Cancel" };
        s->btn_count = 2;
        break;
      case MB_ABORTRETRYIGNORE:
        s->btns[0] = (typeof(s->btns[0])){ IDC_MSG_BTN1, IDABORT, "Abort" };
        s->btns[1] = (typeof(s->btns[1])){ IDC_MSG_BTN2, IDRETRY, "Retry" };
        s->btns[2] = (typeof(s->btns[2])){ IDC_MSG_BTN3, IDIGNORE, "Ignore" };
        s->btn_count = 3;
        break;
      default:
        s->btns[0] = (typeof(s->btns[0])){ IDC_MSG_BTN1, IDOK, "OK" };
        s->btn_count = 1;
        break;
    }
}

static void resolve_icon(MsgBoxState *s, UINT type)
{
    UINT icon = type & MB_ICONMASK;
    if (icon == MB_ICONINFORMATION || icon == MB_ICONASTERISK)
        s->icon_type = 1;
    else if (icon == MB_ICONWARNING || icon == MB_ICONEXCLAMATION)
        s->icon_type = 2;
    else if (icon == MB_ICONERROR || icon == MB_ICONHAND || icon == MB_ICONSTOP)
        s->icon_type = 3;
    else if (icon == MB_ICONQUESTION)
        s->icon_type = 4;
    else
        s->icon_type = 0;
}

/* ============================================================
 *  ICON DRAWING
 * ============================================================ */

static void draw_msg_icon(HDC hdc, RECT *rc, int icon_type)
{
    int cx = (rc->left + rc->right) / 2;
    int cy = (rc->top + rc->bottom) / 2;
    int r  = 14;

    COLORREF color;
    switch (icon_type) {
      case 1: color = RHINO_ACCENT;  break;  /* info */
      case 2: color = RHINO_WARNING; break;  /* warning */
      case 3: color = RHINO_ERROR;   break;  /* error */
      case 4: color = RHINO_ACCENT;  break;  /* question */
      default: return;
    }

    if (icon_type == 2) {
        /* Warning: filled amber triangle with "!" */
        HBRUSH br  = CreateSolidBrush(color);
        HBRUSH ob  = (HBRUSH)SelectObject(hdc, br);
        HPEN   pen = CreatePen(PS_SOLID, 1, color);
        HPEN   op  = (HPEN)SelectObject(hdc, pen);
        POINT tri[3] = {
            { cx, cy - r }, { cx - r, cy + r - 2 }, { cx + r, cy + r - 2 }
        };
        Polygon(hdc, tri, 3);
        SelectObject(hdc, ob); SelectObject(hdc, op);
        DeleteObject(br); DeleteObject(pen);
        /* "!" in dark */
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RHINO_BG_DEEP);
        HFONT f = CreateFontW(-18, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                               DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                               CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                               DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        HFONT of = (HFONT)SelectObject(hdc, f);
        DrawTextA(hdc, "!", 1, rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(hdc, of);
        DeleteObject(f);
    } else {
        /* Circle with symbol inside */
        HBRUSH br  = CreateSolidBrush(color);
        HBRUSH ob  = (HBRUSH)SelectObject(hdc, br);
        HPEN   pen = CreatePen(PS_SOLID, 1, color);
        HPEN   op  = (HPEN)SelectObject(hdc, pen);
        Ellipse(hdc, cx - r, cy - r, cx + r, cy + r);
        SelectObject(hdc, ob); SelectObject(hdc, op);
        DeleteObject(br); DeleteObject(pen);

        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RHINO_BG_DEEP);
        HFONT f = CreateFontW(-18, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                               DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                               CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                               DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        HFONT of = (HFONT)SelectObject(hdc, f);

        const char *sym;
        if (icon_type == 3)      sym = "X";
        else if (icon_type == 4) sym = "?";
        else                     sym = "i";

        DrawTextA(hdc, sym, 1, rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(hdc, of);
        DeleteObject(f);
    }
}

/* ============================================================
 *  DIALOG PROC
 * ============================================================ */

static INT_PTR CALLBACK MsgBoxProc(HWND hwnd, UINT msg,
                                   WPARAM wParam, LPARAM lParam)
{
    MsgBoxState *s = (MsgBoxState *)GetWindowLongPtr(hwnd, GWLP_USERDATA);

    switch (msg) {
      case WM_INITDIALOG: {
        s = (MsgBoxState *)lParam;
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)s);

        rhinossh_apply_dark_dialog(hwnd);

        /* Center on parent (or screen if no parent) */
        HWND parent = GetParent(hwnd);
        RECT rp, rd;
        if (parent && GetWindowRect(parent, &rp)) {
            GetWindowRect(hwnd, &rd);
            int x = rp.left + ((rp.right - rp.left) - (rd.right - rd.left)) / 2;
            int y = rp.top  + ((rp.bottom - rp.top) - (rd.bottom - rd.top)) / 2;
            SetWindowPos(hwnd, NULL, x, y, 0, 0,
                         SWP_NOSIZE | SWP_NOZORDER);
        }

        SetFocus(GetDlgItem(hwnd, IDC_MSG_BTN1));
        return FALSE;
      }

      case WM_DRAWITEM: {
        DRAWITEMSTRUCT *dis = (DRAWITEMSTRUCT *)lParam;
        if (dis->CtlID == IDC_MSG_ICON && s) {
            RECT rc = dis->rcItem;
            HBRUSH bg = CreateSolidBrush(RHINO_BG_DEEP);
            FillRect(dis->hDC, &rc, bg);
            DeleteObject(bg);
            draw_msg_icon(dis->hDC, &rc, s->icon_type);
            return TRUE;
        }
        break;
      }

      case WM_COMMAND: {
        if (!s) break;
        WORD id = LOWORD(wParam);
        for (int i = 0; i < s->btn_count; i++) {
            if (id == s->btns[i].id) {
                s->result = s->btns[i].retval;
                EndDialog(hwnd, s->result);
                return TRUE;
            }
        }
        break;
      }

      case WM_CTLCOLORDLG:
      case WM_CTLCOLORSTATIC:
      case WM_CTLCOLORBTN:
      case WM_CTLCOLOREDIT:
      case WM_CTLCOLORLISTBOX: {
        HBRUSH br = rhinossh_dark_ctlcolor((HDC)wParam, (HWND)lParam, msg);
        if (br) return (INT_PTR)br;
        break;
      }

      case WM_CLOSE:
        if (s) {
            /* Return IDCANCEL on close, matching stock MessageBox */
            s->result = IDCANCEL;
            EndDialog(hwnd, IDCANCEL);
        }
        return TRUE;
    }
    return FALSE;
}

/* ============================================================
 *  IN-MEMORY DIALOG TEMPLATE BUILDER
 * ============================================================ */

#define ALIGN4(ptr) (BYTE *)(((ULONG_PTR)(ptr) + 3) & ~3)

static void write_word(BYTE **p, WORD v) { *(WORD *)*p = v; *p += sizeof(WORD); }
static void write_dword(BYTE **p, DWORD v) { *(DWORD *)*p = v; *p += sizeof(DWORD); }

static void write_wstr(BYTE **p, const wchar_t *s)
{
    int len = (int)(wcslen(s) + 1) * sizeof(wchar_t);
    memcpy(*p, s, len);
    *p += len;
}

static LPDLGTEMPLATE build_msgbox_template(const char *caption,
                                           const char *text,
                                           MsgBoxState *s)
{
    /* Over-allocate (templates need room for class/title words) */
    BYTE *buf = (BYTE *)calloc(1, 8192);
    if (!buf) return NULL;

    /* Measure text to size the dialog */
    HDC hdc = GetDC(NULL);
    HFONT f = CreateFontW(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                           CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                           DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    HFONT of = (HFONT)SelectObject(hdc, f);

    RECT trc = { 0, 0, DLG_W - MARGIN * 2 - ICON_SZ - ICON_GAP, 0 };
    DrawTextA(hdc, text, -1, &trc,
              DT_CALCRECT | DT_WORDBREAK | DT_LEFT);
    int textH = trc.bottom - trc.top;
    int rowH  = (textH > ICON_SZ) ? textH : ICON_SZ;

    SelectObject(hdc, of);
    DeleteObject(f);
    ReleaseDC(NULL, hdc);

    int dlgH = MARGIN + rowH + MARGIN + BTN_H + MARGIN;
    if (dlgH < DLG_H) dlgH = DLG_H;

    int nControls = 2 + s->btn_count;   /* icon + text + buttons */

    BYTE *p = buf;

    /* ---- DLGTEMPLATE ---- */
    DLGTEMPLATE *dlg = (DLGTEMPLATE *)p;
    dlg->style = DS_MODALFRAME | DS_CENTER | WS_POPUP | WS_CAPTION |
                 WS_SYSMENU | WS_VISIBLE | DS_SETFONT;
    dlg->dwExtendedStyle = 0;
    dlg->cdit = (WORD)nControls;
    dlg->x  = 0; dlg->y  = 0;
    dlg->cx = (WORD)DLG_W; dlg->cy = (WORD)dlgH;
    p += sizeof(DLGTEMPLATE);

    /* Menu (none) */
    write_word(&p, 0);
    /* Class (default) */
    write_word(&p, 0);
    /* Title */
    wchar_t wcap[256];
    MultiByteToWideChar(CP_ACP, 0, caption ? caption : "RhinoSSH",
                        -1, wcap, 256);
    write_wstr(&p, wcap);
    /* Font (DS_SETFONT) */
    write_word(&p, 9);
    write_wstr(&p, L"Segoe UI");

    /* ---- Icon (owner-drawn static, vertically centered in row) ---- */
    int iconY = MARGIN + (rowH - ICON_SZ) / 2;
    p = ALIGN4(p);
    DLGITEMTEMPLATE *item = (DLGITEMTEMPLATE *)p;
    item->style = WS_CHILD | WS_VISIBLE | SS_OWNERDRAW;
    item->dwExtendedStyle = 0;
    item->x  = MARGIN;
    item->y  = (WORD)iconY;
    item->cx = ICON_SZ;
    item->cy = ICON_SZ;
    item->id = IDC_MSG_ICON;
    p += sizeof(DLGITEMTEMPLATE);
    write_word(&p, 0xFFFF); write_word(&p, 0x0082); /* STATIC */
    write_word(&p, 0);  /* title empty */
    write_word(&p, 0);  /* extra data */

    /* ---- Text (static, vertically centered in row) ---- */
    int textY = MARGIN + (rowH - textH) / 2;
    p = ALIGN4(p);
    item = (DLGITEMTEMPLATE *)p;
    item->style = WS_CHILD | WS_VISIBLE | SS_LEFT;
    item->dwExtendedStyle = 0;
    item->x  = MARGIN + ICON_SZ + ICON_GAP;
    item->y  = (WORD)textY;
    item->cx = (WORD)(DLG_W - MARGIN * 2 - ICON_SZ - ICON_GAP);
    item->cy = (WORD)textH;
    item->id = IDC_MSG_TEXT;
    p += sizeof(DLGITEMTEMPLATE);
    write_word(&p, 0xFFFF); write_word(&p, 0x0082); /* STATIC */
    wchar_t wtext[4096];
    MultiByteToWideChar(CP_ACP, 0, text ? text : "", -1, wtext, 4096);
    write_wstr(&p, wtext);
    write_word(&p, 0);

    /* ---- Buttons (spaced more, bottom-right with margin) ---- */
    int btnGap = 8;
    int btnTotalW = s->btn_count * BTN_W + (s->btn_count - 1) * btnGap;
    int btnX = DLG_W - MARGIN - btnTotalW;
    int btnY = dlgH - BTN_H - MARGIN;

    for (int i = 0; i < s->btn_count; i++) {
        p = ALIGN4(p);
        item = (DLGITEMTEMPLATE *)p;
        item->style = WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                      (i == 0 ? BS_DEFPUSHBUTTON : BS_PUSHBUTTON);
        item->dwExtendedStyle = 0;
        item->x  = (WORD)(btnX + i * (BTN_W + btnGap));
        item->y  = (WORD)btnY;
        item->cx = BTN_W;
        item->cy = BTN_H;
        item->id = (WORD)s->btns[i].id;
        p += sizeof(DLGITEMTEMPLATE);
        write_word(&p, 0xFFFF); write_word(&p, 0x0080); /* BUTTON */
        wchar_t wlbl[64];
        MultiByteToWideChar(CP_ACP, 0, s->btns[i].label, -1, wlbl, 64);
        write_wstr(&p, wlbl);
        write_word(&p, 0);
    }

    return (LPDLGTEMPLATE)buf;
}

/* ============================================================
 *  PUBLIC API
 * ============================================================ */

int rhinossh_msgbox(HWND hWnd, LPCSTR lpText, LPCSTR lpCaption, UINT uType)
{
    MsgBoxState s;
    memset(&s, 0, sizeof(s));
    s.mb_type = uType;
    s.result  = 0;

    resolve_buttons(&s, uType);
    resolve_icon(&s, uType);

    LPDLGTEMPLATE tmpl = build_msgbox_template(
        lpCaption ? lpCaption : "RhinoSSH",
        lpText    ? lpText    : "",
        &s);

    if (!tmpl) {
        /* Fallback to real Windows MessageBox if template fails */
        typedef int (WINAPI *fnRealMB)(HWND, LPCSTR, LPCSTR, UINT);
        fnRealMB real = (fnRealMB)GetProcAddress(
            GetModuleHandleA("user32.dll"), "MessageBoxA");
        if (real) return real(hWnd, lpText, lpCaption, uType);
        return IDOK;
    }

    DialogBoxIndirectParamA(
        GetModuleHandle(NULL), tmpl, hWnd,
        MsgBoxProc, (LPARAM)&s);

    free(tmpl);
    return s.result;
}
