# RhinoSSH UI Overhaul

Cross-session plan tracker for RhinoSSH visual identity overhaul.
Mirrors the RhinoSniff approach — phased work, each phase landable
and visually testable on its own.

**Goal:** Match the RhinoSniff aesthetic — deep dark surfaces with
cyan accent (#29b6f6), rounded corners, modern typography, branded
chrome instead of vanilla PuTTY/KiTTY UI.

---

## Locked palette

Source of truth: `rhinossh_theme.h`

| Token              | Hex      | RGB             | Use                              |
| ------------------ | -------- | --------------- | -------------------------------- |
| `RHINO_BG_DEEP`    | `#0d1117`| (13, 17, 23)    | Window / dialog background       |
| `RHINO_BG_SURFACE` | `#161b22`| (22, 27, 34)    | Panels, cards                    |
| `RHINO_BG_EDIT`    | `#1c2128`| (28, 33, 40)    | Edit fields                      |
| `RHINO_BG_HEADER`  | `#10141b`| (16, 20, 27)    | Caption / header strip           |
| `RHINO_BORDER`     | `#30363d`| (48, 54, 61)    | Default control border           |
| `RHINO_ACCENT`     | `#29b6f6`| (41, 182, 246)  | **Primary cyan**, window border  |
| `RHINO_ACCENT_HOVER`| `#4fc3f7`| (79, 195, 247) | Cyan hover                       |
| `RHINO_TEXT`       | `#f0f6fc`| (240, 246, 252) | Primary text                     |
| `RHINO_TEXT_MUTED` | `#8b949e`| (139, 148, 158) | Secondary text                   |
| `RHINO_SUCCESS`    | `#4caf50`| (76, 175, 80)   | Green status                     |
| `RHINO_ERROR`      | `#f44336`| (244, 67, 54)   | Red status                       |
| `RHINO_WARNING`    | `#ff9800`| (255, 152, 0)   | Amber status                     |

---

## Architectural decisions

- **Cyan window border via DWM attributes**, not custom non-client area.
  Uses `DWMWA_BORDER_COLOR` + `DWMWA_CAPTION_COLOR` + `DWMWA_TEXT_COLOR`
  + `DWMWA_WINDOW_CORNER_PREFERENCE` (Win11 22000+). Falls through
  silently on Win10 — window keeps dark titlebar without cyan border.
  No DPI / snap / drag-to-maximize headaches.
- **Per-control owner-draw** for buttons, status pills, etc. Existing
  `modernui` flat button infrastructure extends cleanly.
- **In-memory dialog templates** (the pattern from `rhinossh_clipring.c`)
  for all replaced message boxes — no .rc churn.
- **Single source of palette truth** — `rhinossh_theme.h` only. Every
  module includes it. No duplicated `RGB()` literals anywhere else.

---

## Phases

### Phase 1 — Foundation ✅ COMPLETE (April 15, 2026)

- [x] Create `rhinossh_theme.h` (central palette + geometry + typography)
- [x] Rewrite `rhinossh_darkmode.h/c` to use new palette
- [x] Add Win11 DWM cyan border + dark caption + rounded corners in
      `rhinossh_apply_dark_titlebar()` — propagates to every existing
      call site (terminal window + all dialogs) for free
- [x] Refresh `rhinossh_modernui.c` color helpers to source from theme
      so flat buttons match the new look (cyan default, dark surface)

**Files touched (Phase 1):**
- NEW: `rhinossh_theme.h`
- REWRITTEN: `rhinossh_darkmode.h`
- REWRITTEN: `rhinossh_darkmode.c`
- EDITED: `rhinossh_modernui.c` (color helpers only — lines 28–60)
- NEW: `RHINOSSH_UI_OVERHAUL.md` (this file)

**No changes to** `window.c`, `dialog.c`, `controls.c`, `console.c`,
`pageant.c`, `puttygen.c` — existing `rhinossh_apply_dark_titlebar()`
and `rhinossh_apply_dark_dialog()` call sites pick up the new look
automatically.

**Build:** `cd 0.76b_My_PuTTY/windows && mingw32-make`

**What you should see after rebuild:**
- Cyan border around every window (terminal + config + about + etc.)
- Dark caption bar matching window background
- Rounded window corners
- All dialogs in #0d1117 background
- Buttons in #29b6f6 cyan when default/primary

---

### Phase 2 — Buttons (next)

Goal: full button vocabulary matching RhinoSniff (Primary cyan,
Secondary outlined, Ghost/tertiary, status badges).

- [ ] Add button "variant" API to `rhinossh_modernui.h`:
      `MODERNUI_BTN_PRIMARY` / `_SECONDARY` / `_GHOST` / `_DANGER`
- [ ] Per-variant subclass tagging via `SetProp(hwnd, "rhinoVariant", ...)`
- [ ] Larger corner radius (6px), tighter borders (1px), more padding
- [ ] Subtle outer glow on focused/hovered primary buttons
- [ ] Owner-drawn checkbox + radio button (replace themed defaults so
      indicator color is cyan, not Win11 blue)
- [ ] Status pill helper (`modernui_draw_pill(hdc, rc, text, color)`)
      for `● STOPPED` / `● Running` indicators

### Phase 3 — Custom MessageBox

Goal: every `MessageBox()` call replaced with branded dialog.

- [ ] New `rhinossh_msgbox.c/h` — drop-in replacement matching
      `MessageBox()` signature, returning same `IDOK/IDCANCEL/IDYES/
      IDNO/IDRETRY/IDABORT/IDIGNORE` codes
- [ ] In-memory dialog template (clipring pattern), Segoe UI Variable,
      RHINO_BG_DEEP background, cyan accent button row
- [ ] Custom 32×32 icons drawn in code: cyan info, red error, amber
      warning, green success
- [ ] Sweep ~50 call sites across `window.c`, `dialog.c`, `controls.c`,
      `pageant.c`, `puttygen.c`, `storage.c` and replace
- [ ] Hostkey prompt (`console.c`) gets the special treatment —
      multi-line message, fingerprint in mono font

### Phase 4 — About / Event Log / License dialogs

- [ ] About box: branded header strip with Rhino logo, version,
      build date, GitHub link
- [ ] Event Log: dark monospace listbox, filter chips at top,
      Copy/Clear/Close as Primary/Ghost buttons
- [ ] License dialog: dark scroll view, branded header

### Phase 5 — Config dialog (the big one)

- [ ] Restyle the category tree (left pane): cyan active item, indent,
      subtle hover, expanded/collapsed chevrons
- [ ] Add primary "▶ CONNECT" cyan button at top of sidebar
      (above the tree), replacing the bottom "Open" button position
- [ ] Status footer at bottom: connection-state pill + version
- [ ] Make group boxes look like RhinoSniff cards (rounded, surface
      background, no borders just elevation)
- [ ] Tighter spacing throughout, larger inputs, modern combo arrows

### Phase 6 — Terminal window accents

- [ ] Cyan 1px accent line at top of terminal (just below caption)
- [ ] Status pill in caption: `● Connected` green / `● Disconnected` red
      (replaces / augments the title text)
- [ ] Custom-themed scrollbar (already partial — finish it)

### Phase 7 — Small dialogs

- [ ] Resize tooltip (`sizetip.c`) — rounded dark pill instead of
      yellow tooltip
- [ ] Find / Replace dialog
- [ ] Specials menu, Saved Sessions submenu styling

### Phase 8 — Companion apps (optional)

- [ ] PuTTYgen — apply theme + branded chrome
- [ ] Pageant tray — themed tray menu, branded About

---

## Testing notes

After each phase Rhino will rebuild and visually check the affected
surfaces. No automated test harness — visual confirmation per phase
before moving on.

**Build command (every phase):**
```
cd C:\path\to\RhinoSSH-source\0.76b_My_PuTTY\windows
mingw32-make
```

Output: `rhinossh.exe` in `0.76b_My_PuTTY\windows\`.

---

## Rules in force (never relax)

1. **NEVER GUESS** — research before any change
2. **ALWAYS RESEARCH** before making claims
3. **DO NOT change unrequested code** without asking first
