/*
 * rhinossh_modernui.h - Modern UI enhancements for RhinoSSH
 *
 * Flat button styling, quick connect bar, collapsible sidebar.
 */

#ifndef RHINOSSH_MODERNUI_H
#define RHINOSSH_MODERNUI_H

#include <windows.h>

/* ---- Flat Button Styling ---- */

/* Call after dialog creation to subclass all buttons in hwnd */
void modernui_apply_flat_buttons(HWND hwnd);

/* Call in WM_DRAWITEM for owner-drawn buttons.  Returns TRUE if handled. */
BOOL modernui_draw_button(DRAWITEMSTRUCT *dis);

/* ---- Quick Connect Bar ---- */

#define IDC_QCHOST     5001
#define IDC_QCPORT     5002
#define IDC_QCCONNECT  5003
#define IDC_QCLABEL    5004

/* Creates the quick connect bar at the top of the config dialog.
 * Returns the height in pixels consumed (other controls shift down). */
int modernui_create_quickconnect(HWND hwnd, HINSTANCE hinst, HFONT font);

/* Call when IDC_QCCONNECT is clicked.
 * Fills host (max hostlen) and port.  Returns TRUE if valid. */
BOOL modernui_get_quickconnect(HWND hwnd, char *host, int hostlen, int *port);

/* ---- Collapsible Sidebar ---- */

#define IDC_SIDEBAR_TOGGLE  5010

/* Creates the sidebar toggle button. Call after treeview is created. */
void modernui_create_sidebar_toggle(HWND hwnd, HINSTANCE hinst, HFONT font);

/* Toggles the sidebar between expanded and collapsed states.
 * treeview_hwnd is the treeview, panel_x is the panel start offset.
 * Returns new panel x offset. */
int modernui_toggle_sidebar(HWND hwnd, HWND treeview_hwnd);

/* Returns TRUE if sidebar is currently collapsed */
BOOL modernui_sidebar_collapsed(void);

#endif
