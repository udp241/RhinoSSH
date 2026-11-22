<div align="center">

<img src="res/rhinossh-256.png" width="116" alt="RhinoSSH" />

# RhinoSSH

**A fast, portable Windows SSH & telnet client.**
A security-patched fork of [KiTTY](https://www.9bis.com/kitty/) (itself a fork of [PuTTY](https://www.chiark.greenend.org.uk/~sgtatham/putty/)), rebranded and tuned.

![Platform](https://img.shields.io/badge/platform-Windows-0a0a12?style=flat-square&logo=windows)
![Language](https://img.shields.io/badge/C-single%20binary-00bfa5?style=flat-square)
![License](https://img.shields.io/badge/license-MIT-00bfa5?style=flat-square)
![Based on](https://img.shields.io/badge/based%20on-KiTTY%20%2F%20PuTTY-512BD4?style=flat-square)

[Discord](https://discord.gg/icmp) · [Telegram](https://t.me/ncahq)

</div>

---

## About

RhinoSSH is a Windows terminal client in the PuTTY family. It keeps everything KiTTY and PuTTY give you — SSH, telnet, serial, raw, rlogin — and ships as a single portable `.exe` with no install required.

This is a **fork**: the heavy lifting is Simon Tatham's PuTTY and Cyril Dupont's KiTTY. RhinoSSH rebrands it, folds in security patches, and lines it up with the rest of the Rhino tool family. Full credit to the upstream projects — see [Credits](#credits) and `LICENCE.TXT`.

## Features

Everything from KiTTY, including:

- **Portable** — runs from a single `.exe`, sessions saved to a local file or the registry
- **Session launcher & filter** — quick-search and launch saved sessions
- **Automatic login** — stored credentials and post-login command scripts
- **Pre-defined command shortcuts** and locally-run scripts on a remote session
- **URL hyperlinks**, transparency, roll-up, always-on-top, send-to-tray
- **Per-session icons**, duplicate-session quick start, enhanced configuration box
- **zmodem** file transfer, **RuTTY** scripting, **far2l** extensions, **adb** support
- Everything PuTTY supports underneath (SSH-2, port forwarding, public-key auth, etc.)

RhinoSSH's own changes are focused on security fixes and branding rather than reworking the terminal core.

## Build

RhinoSSH builds with the KiTTY / PuTTY Windows toolchain (Visual Studio or MinGW). The source layout and build scripts match KiTTY's, so the upstream [build notes](https://github.com/cyd01/KiTTY) apply. In short, open the project under `0.76b_My_PuTTY/` or run the provided makefiles with your Windows C toolchain; the result is a single `RhinoSSH.exe`.

## Credits

RhinoSSH stands entirely on upstream work:

- **PuTTY** — © Simon Tatham and the PuTTY team · [chiark.greenend.org.uk/~sgtatham/putty](https://www.chiark.greenend.org.uk/~sgtatham/putty/)
- **KiTTY** — © 2007–2013 Cyril Dupont · [9bis.com/kitty](https://www.9bis.com/kitty/)

Both are MIT-licensed; RhinoSSH keeps that license. See `LICENCE.TXT` for the full text.

## License

MIT — the PuTTY/KiTTY license is retained in full in `LICENCE.TXT`. RhinoSSH's modifications are released under the same terms.

---

<div align="center">

Part of the **Rhino** tool family

[discord.gg/icmp](https://discord.gg/icmp) · [t.me/ncahq](https://t.me/ncahq) · Discord: **@wljt**

</div>
