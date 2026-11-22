/*
 * rhinossh_clipring.h - Clipboard history ring for RhinoSSH
 *
 * Keeps a ring buffer of recent clipboard entries.
 * Ctrl+Shift+H or context menu "Clipboard History" opens a picker.
 */

#ifndef RHINOSSH_CLIPRING_H
#define RHINOSSH_CLIPRING_H

#include <windows.h>

#define CLIPRING_MAX_ENTRIES  20
#define CLIPRING_MAX_TEXTLEN  4096   /* max chars stored per entry */
#define CLIPRING_PREVIEW_LEN  80     /* chars shown in the popup list */

void clipring_init(void);
void clipring_add(const wchar_t *text, int len);
int  clipring_count(void);

/* Opens the picker dialog.  Returns index chosen (0 = newest), or -1. */
int  clipring_show_picker(HWND parent);

/* Retrieves entry by index (0 = newest).  Returns pointer valid until
 * next clipring_add().  NULL if out of range.  *out_len receives length. */
const wchar_t *clipring_get(int index, int *out_len);

#endif
