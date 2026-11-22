/*
 * rhinossh_clipring.c - Clipboard history ring for RhinoSSH
 *
 * Ring buffer of recent clipboard entries with a Win32 listbox picker.
 * Integrated into RhinoSSH's dark mode when active.
 */

#include <windows.h>
#include <commctrl.h>
#include <wchar.h>
#include "rhinossh_clipring.h"
#include "rhinossh_darkmode.h"

/* ---- Ring buffer ---- */

typedef struct {
    wchar_t text[CLIPRING_MAX_TEXTLEN];
    int     len;
} ClipEntry;

static ClipEntry g_ring[CLIPRING_MAX_ENTRIES];
static int g_head  = 0;   /* next write position */
static int g_count = 0;

void clipring_init(void)
{
    g_head  = 0;
    g_count = 0;
    memset(g_ring, 0, sizeof(g_ring));
}

void clipring_add(const wchar_t *text, int len)
{
    if (!text || len <= 0)
        return;

    /* Skip pure whitespace */
    int all_ws = 1;
    for (int i = 0; i < len; i++) {
        if (text[i] != L' ' && text[i] != L'\t' &&
            text[i] != L'\r' && text[i] != L'\n') {
            all_ws = 0;
            break;
        }
    }
    if (all_ws)
        return;

    /* Clamp length */
    if (len > CLIPRING_MAX_TEXTLEN - 1)
        len = CLIPRING_MAX_TEXTLEN - 1;

    /* Skip duplicates of the most recent entry */
    if (g_count > 0) {
        int prev = (g_head - 1 + CLIPRING_MAX_ENTRIES) % CLIPRING_MAX_ENTRIES;
        if (g_ring[prev].len == len &&
            memcmp(g_ring[prev].text, text, len * sizeof(wchar_t)) == 0)
            return;
    }

    memcpy(g_ring[g_head].text, text, len * sizeof(wchar_t));
    g_ring[g_head].text[len] = L'\0';
    g_ring[g_head].len = len;

    g_head = (g_head + 1) % CLIPRING_MAX_ENTRIES;
    if (g_count < CLIPRING_MAX_ENTRIES)
        g_count++;
}

int clipring_count(void)
{
    return g_count;
}

const wchar_t *clipring_get(int index, int *out_len)
{
    if (index < 0 || index >= g_count)
        return NULL;
    int pos = (g_head - 1 - index + CLIPRING_MAX_ENTRIES * 2) % CLIPRING_MAX_ENTRIES;
    if (out_len)
        *out_len = g_ring[pos].len;
    return g_ring[pos].text;
}

/* ---- Build a one-line preview for the listbox ---- */

static void make_preview(const wchar_t *src, int srclen, wchar_t *dst, int dstmax)
{
    int j = 0;
    for (int i = 0; i < srclen && j < dstmax - 1; i++) {
        wchar_t c = src[i];
        if (c == L'\r')
            continue;
        if (c == L'\n' || c == L'\t') {
            if (j > 0 && dst[j-1] != L' ')
                dst[j++] = L' ';
            continue;
        }
        dst[j++] = c;
    }
    /* Trim trailing space */
    while (j > 0 && dst[j-1] == L' ')
        j--;
    dst[j] = L'\0';
}

/* ---- Picker dialog (runtime-created, no .rc needed) ---- */

#define IDC_CLIPLIST  1001
#define IDC_CLIPOK    IDOK
#define IDC_CLIPCAN   IDCANCEL

static int g_picker_result = -1;
static HFONT g_picker_font = NULL;

static LRESULT CALLBACK PickerProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
      case WM_INITDIALOG:
      {
        /* Apply dark mode if active */
        if (rhinossh_is_dark_mode())
            rhinossh_apply_dark_dialog(hwnd);

        HWND hList = GetDlgItem(hwnd, IDC_CLIPLIST);

        /* Set font to something readable */
        g_picker_font = CreateFontW(
            -14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");
        if (g_picker_font)
            SendMessage(hList, WM_SETFONT, (WPARAM)g_picker_font, TRUE);

        int n = clipring_count();
        for (int i = 0; i < n; i++) {
            int elen;
            const wchar_t *entry = clipring_get(i, &elen);
            if (!entry) continue;

            wchar_t preview[CLIPRING_PREVIEW_LEN + 16];
            wchar_t body[CLIPRING_PREVIEW_LEN + 1];
            make_preview(entry, elen, body, CLIPRING_PREVIEW_LEN + 1);

            wsprintfW(preview, L"%2d. %s", i + 1, body);
            SendMessageW(hList, LB_ADDSTRING, 0, (LPARAM)preview);
        }

        if (n > 0)
            SendMessage(hList, LB_SETCURSEL, 0, 0);

        /* Center dialog on parent */
        HWND hParent = GetParent(hwnd);
        if (hParent) {
            RECT rp, rd;
            GetWindowRect(hParent, &rp);
            GetWindowRect(hwnd, &rd);
            int x = rp.left + ((rp.right - rp.left) - (rd.right - rd.left)) / 2;
            int y = rp.top  + ((rp.bottom - rp.top) - (rd.bottom - rd.top)) / 2;
            SetWindowPos(hwnd, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
        }

        SetFocus(hList);
        return FALSE;   /* we set focus ourselves */
      }

      case WM_COMMAND:
        switch (LOWORD(wParam)) {
          case IDC_CLIPOK:
          {
            HWND hList = GetDlgItem(hwnd, IDC_CLIPLIST);
            g_picker_result = (int)SendMessage(hList, LB_GETCURSEL, 0, 0);
            if (g_picker_result == LB_ERR)
                g_picker_result = -1;
            EndDialog(hwnd, 1);
            return TRUE;
          }
          case IDC_CLIPCAN:
            g_picker_result = -1;
            EndDialog(hwnd, 0);
            return TRUE;
          case IDC_CLIPLIST:
            if (HIWORD(wParam) == LBN_DBLCLK) {
                /* Double-click = select and close */
                SendMessage(hwnd, WM_COMMAND, IDC_CLIPOK, 0);
                return TRUE;
            }
            break;
        }
        break;

      case WM_CTLCOLORLISTBOX:
      case WM_CTLCOLORDLG:
      case WM_CTLCOLORSTATIC:
      case WM_CTLCOLORBTN:
      case WM_CTLCOLOREDIT:
        if (rhinossh_is_dark_mode()) {
            HBRUSH br = rhinossh_dark_ctlcolor((HDC)wParam, (HWND)lParam, msg);
            if (br)
                return (LRESULT)br;
        }
        break;

      case WM_CLOSE:
        g_picker_result = -1;
        EndDialog(hwnd, 0);
        return TRUE;

      case WM_DESTROY:
        if (g_picker_font) { DeleteObject(g_picker_font); g_picker_font = NULL; }
        return FALSE;
    }
    return FALSE;
}

/*
 * Build the dialog template in memory so we don't need a .rc resource.
 * Layout: a listbox filling most of the window, OK and Cancel at bottom.
 */
static LPDLGTEMPLATE build_picker_template(void)
{
    /*
     * We allocate a buffer and fill DLGTEMPLATE + DLGITEMTEMPLATE
     * structures manually.  Sizes in dialog units.
     */
    #define DLG_W  340
    #define DLG_H  260
    #define BTN_W   60
    #define BTN_H   14
    #define MARGIN   6

    /* Over-allocate — templates need room for class/title words */
    BYTE *buf = (BYTE *)calloc(1, 4096);
    if (!buf) return NULL;

    BYTE *p = buf;

    /* ---- DLGTEMPLATE ---- */
    DLGTEMPLATE *dlg = (DLGTEMPLATE *)p;
    dlg->style = DS_MODALFRAME | DS_CENTER | WS_POPUP | WS_CAPTION |
                 WS_SYSMENU | WS_VISIBLE | DS_SETFONT;
    dlg->dwExtendedStyle = 0;
    dlg->cdit = 3;   /* listbox + 2 buttons */
    dlg->x  = 0;
    dlg->y  = 0;
    dlg->cx = DLG_W;
    dlg->cy = DLG_H;
    p += sizeof(DLGTEMPLATE);

    /* Menu (none) */
    *(WORD *)p = 0; p += sizeof(WORD);
    /* Class (default) */
    *(WORD *)p = 0; p += sizeof(WORD);
    /* Title */
    const wchar_t *title = L"RhinoSSH - Clipboard History";
    int tlen = (int)(wcslen(title) + 1) * sizeof(wchar_t);
    memcpy(p, title, tlen);
    p += tlen;
    /* Font (DS_SETFONT) */
    *(WORD *)p = 9; p += sizeof(WORD);  /* point size */
    const wchar_t *fname = L"Segoe UI";
    int flen = (int)(wcslen(fname) + 1) * sizeof(wchar_t);
    memcpy(p, fname, flen);
    p += flen;

    /* ---- Align to DWORD ---- */
    #define ALIGN4(ptr) (BYTE *)(((ULONG_PTR)(ptr) + 3) & ~3)

    /* ---- Listbox ---- */
    p = ALIGN4(p);
    DLGITEMTEMPLATE *item = (DLGITEMTEMPLATE *)p;
    item->style = WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL |
                  WS_TABSTOP | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT;
    item->dwExtendedStyle = 0;
    item->x  = MARGIN;
    item->y  = MARGIN;
    item->cx = DLG_W - MARGIN * 2;
    item->cy = DLG_H - BTN_H - MARGIN * 3 - 2;
    item->id = IDC_CLIPLIST;
    p += sizeof(DLGITEMTEMPLATE);
    /* Class: 0x0083 = listbox */
    *(WORD *)p = 0xFFFF; p += sizeof(WORD);
    *(WORD *)p = 0x0083; p += sizeof(WORD);
    /* Title (empty) */
    *(WORD *)p = 0; p += sizeof(WORD);
    /* Extra data */
    *(WORD *)p = 0; p += sizeof(WORD);

    /* ---- OK button ---- */
    p = ALIGN4(p);
    item = (DLGITEMTEMPLATE *)p;
    item->style = WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON | WS_TABSTOP;
    item->dwExtendedStyle = 0;
    item->x  = DLG_W - MARGIN - BTN_W * 2 - 6;
    item->y  = DLG_H - BTN_H - MARGIN;
    item->cx = BTN_W;
    item->cy = BTN_H;
    item->id = IDC_CLIPOK;
    p += sizeof(DLGITEMTEMPLATE);
    *(WORD *)p = 0xFFFF; p += sizeof(WORD);
    *(WORD *)p = 0x0080; p += sizeof(WORD);  /* button */
    const wchar_t *ok = L"Paste";
    int olen = (int)(wcslen(ok) + 1) * sizeof(wchar_t);
    memcpy(p, ok, olen);
    p += olen;
    *(WORD *)p = 0; p += sizeof(WORD);

    /* ---- Cancel button ---- */
    p = ALIGN4(p);
    item = (DLGITEMTEMPLATE *)p;
    item->style = WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP;
    item->dwExtendedStyle = 0;
    item->x  = DLG_W - MARGIN - BTN_W;
    item->y  = DLG_H - BTN_H - MARGIN;
    item->cx = BTN_W;
    item->cy = BTN_H;
    item->id = IDC_CLIPCAN;
    p += sizeof(DLGITEMTEMPLATE);
    *(WORD *)p = 0xFFFF; p += sizeof(WORD);
    *(WORD *)p = 0x0080; p += sizeof(WORD);
    const wchar_t *can = L"Cancel";
    int clen = (int)(wcslen(can) + 1) * sizeof(wchar_t);
    memcpy(p, can, clen);
    p += clen;
    *(WORD *)p = 0; p += sizeof(WORD);

    return (LPDLGTEMPLATE)buf;
}

int clipring_show_picker(HWND parent)
{
    if (clipring_count() == 0) {
        MessageBoxW(parent, L"Clipboard history is empty.",
                    L"RhinoSSH", MB_OK | MB_ICONINFORMATION);
        return -1;
    }

    g_picker_result = -1;

    LPDLGTEMPLATE tmpl = build_picker_template();
    if (!tmpl) return -1;

    DialogBoxIndirectW(GetModuleHandle(NULL), tmpl, parent, PickerProc);

    free(tmpl);
    return g_picker_result;
}
