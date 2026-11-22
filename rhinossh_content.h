/*
 * rhinossh_content.h - Content overlay for RhinoSSH config dialog
 */

#ifndef RHINOSSH_CONTENT_H
#define RHINOSSH_CONTENT_H

#include <windows.h>

/* Control IDs */
#define IDC_RHINO_HOST        6100
#define IDC_RHINO_PORT        6101
#define IDC_RHINO_SESSNAME    6102
#define IDC_RHINO_SAVE        6103
#define IDC_RHINO_SESSLIST    6110
#define IDC_RHINO_SESSCONNECT 6111
#define IDC_RHINO_SESSDELETE  6112
#define IDC_RHINO_SESSLOAD    6113
#define IDC_RHINO_SESSSEARCH  6114

/* Views */
#define RHINO_VIEW_QUICKCONNECT  0
#define RHINO_VIEW_SESSIONS      0  /* new unified view (same as above) */
#define RHINO_VIEW_SAVED         0  /* old SAVED view folded in */

void rhinossh_content_create(HWND dialog, HWND sidebar, HFONT font,
                             void *conf);
void rhinossh_content_switch_view(int view);

/* Call at dialog close. If a session was loaded for editing,
 * any in-memory Conf changes get written back to disk. */
void rhinossh_content_autosave_on_close(void);
void rhinossh_content_apply_to_putty(HWND dialog);
int  rhinossh_content_get_host(char *buf, int buflen);
int  rhinossh_content_get_port(void);
int  rhinossh_content_get_protocol(void);
void rhinossh_content_destroy(void);

/* Returns the content panel HWND (NULL if not created). */
HWND rhinossh_content_get_hwnd(void);

#endif
