/*
 * rhinossh_settings.h - Custom settings views for RhinoSSH
 *
 * Seven card-based dark-themed views: Terminal, Window, Connection,
 * SSH Auth, Logging, Colours, Proxy.  Each reads/writes PuTTY Conf
 * directly — no PuTTY panels.
 */

#ifndef RHINOSSH_SETTINGS_H
#define RHINOSSH_SETTINGS_H

#include <windows.h>

/* View IDs (passed to rhinossh_settings_switch_view) */
#define RHINO_SETTINGS_TERMINAL    0
#define RHINO_SETTINGS_WINDOW      1
#define RHINO_SETTINGS_CONNECTION  2
#define RHINO_SETTINGS_SSH_AUTH    3
#define RHINO_SETTINGS_LOGGING     4
#define RHINO_SETTINGS_COLOURS     5
#define RHINO_SETTINGS_PROXY       6
#define RHINO_SETTINGS_APPEARANCE  7
#define RHINO_SETTINGS_BEHAVIOUR   8
#define RHINO_SETTINGS_URLS        9
#define RHINO_SETTINGS_AUTOMATION  10
#define RHINO_SETTINGS_ADVANCED    11
#define RHINO_SETTINGS_COUNT       12

/* Create the settings panel.  conf is a PuTTY Conf*.
 * The panel is created hidden; call switch_view to show it. */
void rhinossh_settings_create(HWND dialog, HFONT font, void *conf,
                              int x, int y, int w, int h);

/* Switch to a settings view (0-4). Makes the settings panel
 * visible and reloads values from Conf. */
void rhinossh_settings_switch_view(int view);

/* Hide the settings panel (when switching back to content). */
void rhinossh_settings_hide(void);

/* Returns TRUE if the settings panel is currently visible. */
BOOL rhinossh_settings_is_visible(void);

/* Flush the current view's edit controls (ST_INT/ST_STR/ST_FILE text
 * fields) to g_conf. ST_TOGGLE/ST_CHOICE/ST_COLOUR already save on
 * every click, so this only matters for typed text that hasn't yet
 * fired EN_KILLFOCUS. Call before closing the dialog. */
void rhinossh_settings_save_current_view(void);

/* Destroy resources. Call from WM_CLOSE / WM_DESTROY. */
void rhinossh_settings_destroy(void);

/* Returns the settings HWND (NULL if not created). */
HWND rhinossh_settings_get_hwnd(void);

#endif /* RHINOSSH_SETTINGS_H */
