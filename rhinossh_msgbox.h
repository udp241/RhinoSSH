/*
 * rhinossh_msgbox.h - Branded message box replacement for RhinoSSH
 *
 * Drop-in for MessageBox/MessageBoxA — same signature, same return
 * values (IDOK/IDCANCEL/IDYES/IDNO/IDRETRY/IDABORT/IDIGNORE).
 * Renders with RhinoSSH dark theme + cyan accent + flat buttons.
 *
 * Include this AFTER <windows.h> — it redefines MessageBox/MessageBoxA
 * as macros so all existing call sites route through the branded dialog
 * without source changes.
 */

#ifndef RHINOSSH_MSGBOX_H
#define RHINOSSH_MSGBOX_H

#include <windows.h>

int rhinossh_msgbox(HWND hWnd, LPCSTR lpText, LPCSTR lpCaption, UINT uType);

/* Macro hijack: every MessageBox/MessageBoxA call becomes rhinossh_msgbox.
 * Undefine the Windows macros first so ours takes effect. */
#ifdef MessageBox
#undef MessageBox
#endif
#ifdef MessageBoxA
#undef MessageBoxA
#endif

#define MessageBox  rhinossh_msgbox
#define MessageBoxA rhinossh_msgbox

#endif /* RHINOSSH_MSGBOX_H */
