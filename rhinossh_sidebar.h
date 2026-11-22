/*
 * rhinossh_sidebar.h - Custom branded sidebar for RhinoSSH config dialog
 *
 * Replaces PuTTY's category treeview with a RhinoSniff-style sidebar:
 * brand header, primary CONNECT button, "Workspace" + "Configure"
 * sections with icon nav, status footer.
 *
 * The treeview is hidden (not destroyed) so panel switching still
 * works via TreeView_SelectItem — sidebar nav clicks fire
 * TVN_SELCHANGED on the hidden treeview, which PuTTY's existing
 * dialog handler picks up to swap panels.
 */

#ifndef RHINOSSH_SIDEBAR_H
#define RHINOSSH_SIDEBAR_H

#include <windows.h>

/* ID range for sidebar controls (6000+, doesn't collide with PuTTY
 * controlbox IDs or modernui's 5000-range). */
#define IDC_RHINO_SIDEBAR_BASE  6000
#define IDC_RHINO_CONNECT       6001

/* Build the sidebar inside the config dialog. Hides the treeview and
 * "Cate&gory:" label, creates the custom sidebar over the same area.
 * Must be called from WM_INITDIALOG after PuTTY's treeview is built. */
void rhinossh_sidebar_create(HWND dialog, HWND treeview, HWND tv_label,
                             HFONT font);

/* Tear down sidebar resources. Called from WM_DESTROY. */
void rhinossh_sidebar_destroy(void);

/* Returns the sidebar's HWND (for callers that need to size around it).
 * NULL if sidebar isn't created. */
HWND rhinossh_sidebar_get_hwnd(void);

/* Toggle the sidebar between expanded (with labels) and collapsed
 * (icons only). Resizes the content/settings panels too. */
void rhinossh_sidebar_toggle_collapse(void);

#endif /* RHINOSSH_SIDEBAR_H */
