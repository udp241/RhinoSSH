/*
 * rhinossh_theme.h - Unified visual palette for RhinoSSH
 *
 * Central source of truth for colors, geometry, and fonts across all
 * RhinoSSH UI modules (darkmode, modernui, msgbox, clipring, etc.).
 *
 * Matches the RhinoSniff brand identity: deep dark surfaces with
 * cyan accent (#29b6f6).
 */

#ifndef RHINOSSH_THEME_H
#define RHINOSSH_THEME_H

#include <windows.h>

/* ============================================================
 *  COLOR PALETTE
 * ============================================================ */

/* ---- Surfaces ---- */
#define RHINO_BG_DEEP       RGB( 13,  17,  23)   /* #0d1117  window bg */
#define RHINO_BG_SURFACE    RGB( 22,  27,  34)   /* #161b22  panels    */
#define RHINO_BG_EDIT       RGB( 28,  33,  40)   /* #1c2128  inputs    */
#define RHINO_BG_HOVER      RGB( 33,  38,  45)   /* hover bg           */
#define RHINO_BG_PRESS      RGB( 11,  14,  20)   /* press bg           */
#define RHINO_BG_HEADER     RGB( 16,  20,  27)   /* header strip       */

/* ---- Borders ---- */
#define RHINO_BORDER        RGB( 48,  54,  61)   /* #30363d default    */
#define RHINO_BORDER_SOFT   RGB( 36,  41,  47)   /* subtle separator   */
#define RHINO_BORDER_FOCUS  RGB( 41, 182, 246)   /* cyan focus ring    */

/* ---- Accent (cyan) - matches RhinoSniff brand ---- */
#define RHINO_ACCENT        RGB( 41, 182, 246)   /* #29b6f6 primary    */
#define RHINO_ACCENT_HOVER  RGB( 79, 195, 247)   /* #4fc3f7 hover      */
#define RHINO_ACCENT_PRESS  RGB( 30, 136, 229)   /* #1e88e5 press      */
#define RHINO_ACCENT_DIM    RGB( 20,  90, 125)   /* darker cyan        */

/* ---- Text ---- */
#define RHINO_TEXT          RGB(240, 246, 252)   /* #f0f6fc primary    */
#define RHINO_TEXT_MUTED    RGB(139, 148, 158)   /* #8b949e secondary  */
#define RHINO_TEXT_DISABLED RGB( 72,  79,  88)   /* #484f58 disabled   */
#define RHINO_TEXT_ON_ACCENT RGB(255, 255, 255)  /* white on cyan      */

/* ---- Status colors (pills, icons, indicators) ---- */
#define RHINO_SUCCESS       RGB( 76, 175,  80)   /* #4caf50 green      */
#define RHINO_ERROR         RGB(244,  67,  54)   /* #f44336 red        */
#define RHINO_WARNING       RGB(255, 152,   0)   /* #ff9800 amber      */
#define RHINO_INFO          RGB( 41, 182, 246)   /* cyan info          */

/* ---- Data series colors (used in stats/columns, matches RhinoSniff) ---- */
#define RHINO_SERIES_RED    RGB(244,  67,  54)
#define RHINO_SERIES_AMBER  RGB(255, 152,   0)
#define RHINO_SERIES_PURPLE RGB(186, 104, 200)
#define RHINO_SERIES_BLUE   RGB( 79, 195, 247)
#define RHINO_SERIES_ORANGE RGB(255, 138,  61)
#define RHINO_SERIES_GREEN  RGB( 76, 175,  80)

/* ============================================================
 *  GEOMETRY
 * ============================================================ */

#define RHINO_RADIUS         6     /* button / input corner radius   */
#define RHINO_RADIUS_LARGE  10     /* card / dialog corner radius    */
#define RHINO_PADDING       12     /* default control padding        */
#define RHINO_GAP            8     /* default gap between controls   */

/* Button sizing */
#define RHINO_BTN_HEIGHT    28     /* min button height (px)         */
#define RHINO_BTN_HEIGHT_LG 36     /* large/primary button height    */

/* Border thickness */
#define RHINO_BORDER_THICK   1
#define RHINO_FOCUS_THICK    2

/* ============================================================
 *  TYPOGRAPHY
 * ============================================================ */

/* Wrapped in W() so headers compiled without UNICODE still get LPCWSTR */
#define RHINO_FONT_UI       L"Segoe UI Variable"
#define RHINO_FONT_UI_FB    L"Segoe UI"
#define RHINO_FONT_MONO     L"Cascadia Code"
#define RHINO_FONT_MONO_FB  L"Consolas"

#define RHINO_FONT_PT       9      /* default UI point size          */
#define RHINO_FONT_PT_SM    8
#define RHINO_FONT_PT_LG   10
#define RHINO_FONT_PT_TITLE 12

#endif /* RHINOSSH_THEME_H */
