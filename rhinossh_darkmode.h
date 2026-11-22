/*
 * rhinossh_darkmode.h - Window chrome theming for RhinoSSH
 *
 * Applies the Rhino dark frame (dark caption + cyan border + rounded
 * corners on Win11) to top-level windows, and themes child controls
 * inside dialogs. Color palette lives in rhinossh_theme.h.
 */

#ifndef RHINOSSH_DARKMODE_H
#define RHINOSSH_DARKMODE_H

#include <windows.h>
#include "rhinossh_theme.h"
#include "rhinossh_msgbox.h"

/* One-time initialization. Detects Win build, loads uxtheme/dwmapi,
 * sets process-wide dark mode preference. Idempotent. */
void   rhinossh_darkmode_init(void);

/* Apply the full Rhino frame (dark titlebar + cyan border + dark
 * caption + rounded corners on Win11) to a top-level window.
 * Safe to call on dialogs, message boxes, and the terminal window. */
void   rhinossh_apply_dark_titlebar(HWND hwnd);

/* Theme every child control inside a dialog (treeview, listview,
 * buttons, edits, combos, statics, scrollbars). Calls
 * rhinossh_apply_dark_titlebar() internally. */
void   rhinossh_apply_dark_dialog(HWND hwnd);

/* Drop-in WM_CTLCOLOR* handler. Returns brush to use, or NULL to
 * fall through to default. msg is one of WM_CTLCOLOR{DLG,STATIC,
 * BTN,EDIT,LISTBOX,SCROLLBAR}. */
HBRUSH rhinossh_dark_ctlcolor(HDC hdc, HWND hwndCtl, UINT msg);

/* Returns 1 if dark mode is active (Win10 1809+), else 0. */
int    rhinossh_is_dark_mode(void);

/* Breathing-glow border animation.
 *
 * register: adds HWND to the animated set — DWM border color pulses
 * between dim cyan and a brighter cyan on a ~3s sine loop. Unfocused
 * windows hold the static dim color. Auto-started by
 * rhinossh_apply_dark_titlebar() on Win11; safe to call on HWNDs
 * that are already registered (idempotent). No-op on < Win11.
 *
 * unregister: removes HWND from the set. Dead HWNDs are also pruned
 * automatically on each tick, so calling this isn't strictly required. */
void   rhinossh_register_breathing_border(HWND hwnd);
void   rhinossh_unregister_breathing_border(HWND hwnd);

/* ---- Backwards-compat aliases ----
 * Old code referenced these directly; map them to the new palette. */
#define RHINO_BG_COLOR      RHINO_BG_DEEP
#define RHINO_BG_LIGHTER    RHINO_BG_SURFACE
#define RHINO_TEXT_COLOR    RHINO_TEXT
/* RHINO_BG_EDIT already defined in rhinossh_theme.h */

#endif /* RHINOSSH_DARKMODE_H */
