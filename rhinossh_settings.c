/*
 * rhinossh_settings.c - Custom settings views for RhinoSSH
 *
 * Six card-based dark-themed views: Terminal, Window, Connection,
 * SSH Auth, Logging, Colours.
 * Each uses the same card/pill/dark-theme style as Quick Connect.
 * All controls read/write PuTTY's Conf directly.
 *
 * Control types:
 *   ST_TOGGLE  - boolean on/off pill (reads/writes conf_bool)
 *   ST_CHOICE  - row of exclusive pills (reads/writes conf_int)
 *   ST_INT     - number edit box (reads/writes conf_int)
 *   ST_STR     - text edit box (reads/writes conf_str)
 *   ST_FILE    - text edit + browse button (reads/writes conf_filename)
 *   ST_FONT    - read-only display + "Choose..." button (reads/writes conf_fontspec)
 *   ST_COLOUR  - coloured swatch (reads/writes conf_int_int slot*3+{0,1,2})
 */

#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <uxtheme.h>
#include <string.h>
#include <stdio.h>

#include "putty.h"       /* Conf, conf_get_*, conf_set_* */
#include "rhinossh_settings.h"
#include "rhinossh_theme.h"
#include "rhinossh_darkmode.h"

#ifndef EM_SETCUEBANNER
#define EM_SETCUEBANNER 0x1501
#endif

/* ============================================================
 *  DATA MODEL
 * ============================================================ */

typedef enum { ST_TOGGLE, ST_CHOICE, ST_INT, ST_STR, ST_FILE, ST_FONT, ST_COLOUR, ST_BUTTON } SettingType;

#define MAX_OPTS  6

typedef struct {
    const char  *label;
    SettingType  type;
    int          conf_key;          /* CONF_xxx enum value            */
    /* ST_CHOICE options */
    int          n_opts;
    const char  *opt_labels[MAX_OPTS];
    int          opt_values[MAX_OPTS];
    const char  *desc;              /* one-line help text (optional, NULL = none) */
    /* Runtime */
    HWND         edit_hwnd;         /* for ST_INT / ST_STR / ST_FILE  */
    HWND         browse_hwnd;       /* "..." button for ST_FILE       */
    RECT         label_rc;
    RECT         ctrl_rc;           /* toggle pill or first choice    */
    RECT         browse_rc;         /* browse button rect for ST_FILE */
    RECT         pill_rcs[MAX_OPTS];/* individual choice pill rects   */
} SettingItem;

#define MAX_CARD_ITEMS 8

typedef struct {
    const char  *title;
    int          n_items;
    SettingItem  items[MAX_CARD_ITEMS];
    /* Computed at layout time */
    RECT         card_rc;
} SettingCard;

#define MAX_CARDS 8

typedef struct {
    const char *title;
    const char *subtitle;
    int         n_cards;
    SettingCard cards[MAX_CARDS];
} SettingsView;

/* ============================================================
 *  VIEW DEFINITIONS
 * ============================================================ */

static SettingsView g_views[RHINO_SETTINGS_COUNT] = {

    /* ---- VIEW 0: TERMINAL ---- */
    {
        "Terminal", "Configure terminal emulation, cursor, font, and scrollback.",
        5,
        {
            { "FONT", 2, {
                { "Terminal font", ST_FONT, CONF_font, .desc="The font used in the terminal. Fixed-width only." },
                { "Rendering", ST_CHOICE, CONF_font_quality, 4,
                  {"Default","Antialiased","Non-AA","ClearType"},
                  {0,1,2,3},
                  "How text is smoothed. ClearType looks best on LCD screens." },
            }},
            { "SIZE", 2, {
                { "Columns", ST_INT, CONF_width,  .desc="Width of the terminal in characters (default 80)" },
                { "Rows",    ST_INT, CONF_height, .desc="Height of the terminal in characters (default 24)" },
            }},
            { "SCROLLBACK", 3, {
                { "Lines",             ST_INT,    CONF_savelines,     .desc="How many past lines to keep. More = more RAM used." },
                { "Scroll on keystroke", ST_TOGGLE, CONF_scroll_on_key, .desc="Jump to the bottom when you press a key" },
                { "Scroll on output",  ST_TOGGLE, CONF_scroll_on_disp, .desc="Jump to the bottom when the server prints something" },
            }},
            { "CURSOR", 2, {
                { "Type", ST_CHOICE, CONF_cursor_type, 3,
                  {"Block","Underline","Bar"}, {0,1,2},
                  "Shape of the text cursor" },
                { "Blink", ST_TOGGLE, CONF_blink_cur, .desc="Blink the cursor" },
            }},
            { "FEATURES", 5, {
                { "Disable app cursor keys", ST_TOGGLE, CONF_no_applic_c,       .desc="Stop apps from hijacking the arrow keys" },
                { "Disable app keypad",      ST_TOGGLE, CONF_no_applic_k,       .desc="Stop apps from hijacking the numpad" },
                { "Disable mouse reporting", ST_TOGGLE, CONF_no_mouse_rep,      .desc="Don't let remote apps track your mouse" },
                { "Disable remote resize",   ST_TOGGLE, CONF_no_remote_resize,  .desc="Ignore apps that try to resize the window" },
                { "Disable alt screen",      ST_TOGGLE, CONF_no_alt_screen,     .desc="Don't let apps clear the screen with their own view" },
            }},
        }
    },

    /* ---- VIEW 1: WINDOW ---- */
    {
        "Window", "Appearance, close behavior, resize, and colour support.",
        4,
        {
            { "APPEARANCE", 4, {
                { "Window title",  ST_STR,    CONF_wintitle,      .desc="Text shown in the titlebar" },
                { "Scrollbar",     ST_TOGGLE, CONF_scrollbar,     .desc="Show the scrollbar on the side" },
                { "Always on top", ST_TOGGLE, CONF_alwaysontop,   .desc="Keep the window above other windows" },
                { "Warn on close", ST_TOGGLE, CONF_warn_on_close, .desc="Ask before closing during an active connection" },
            }},
            { "CLOSE ON EXIT", 1, {
                { "Action", ST_CHOICE, CONF_close_on_exit, 3,
                  {"Always","Never","Clean exit only"}, {0,1,2},
                  "What happens when the remote shell exits" },
            }},
            { "RESIZE MODE", 1, {
                { "Mode", ST_CHOICE, CONF_resize_action, 4,
                  {"Change terminal","Change font","Disabled","Forbid"}, {0,1,2,3},
                  "What happens when you drag the window to resize" },
            }},
            { "COLOURS", 4, {
                { "ANSI colours",    ST_TOGGLE, CONF_ansi_colour,      .desc="Let the server display coloured text" },
                { "xterm 256 colour",ST_TOGGLE, CONF_xterm_256_colour, .desc="Let the server use 256 colours" },
                { "True colour",     ST_TOGGLE, CONF_true_colour,      .desc="Let the server use millions of colours (best)" },
                { "Bold style", ST_CHOICE, CONF_bold_style, 3,
                  {"Font","Colour","Both"}, {1,2,3},
                  "How bold text is shown: thicker font, brighter colour, or both" },
            }},
        }
    },

    /* ---- VIEW 2: CONNECTION ---- */
    {
        "Connection", "Login credentials, SSH options, and keepalive.",
        4,
        {
            { "LOGIN", 2, {
                { "Username",       ST_STR, CONF_username,   .desc="Who to log in as (blank = ask every time)" },
                { "Remote command", ST_STR, CONF_remote_cmd, .desc="Run this command right after connecting" },
            }},
            { "SSH OPTIONS", 3, {
                { "Compression",     ST_TOGGLE, CONF_compression,     .desc="Compress data. Saves bandwidth, uses more CPU." },
                { "Agent forwarding",ST_TOGGLE, CONF_agentfwd,         .desc="Let the server use your local SSH keys (convenient but risky)" },
                { "Show banner",     ST_TOGGLE, CONF_ssh_show_banner,  .desc="Show the message the server prints before login" },
            }},
            { "KEEPALIVE", 3, {
                { "Interval (secs)", ST_INT,    CONF_ping_interval,   .desc="Seconds between keepalive pings (0 = off)" },
                { "TCP no delay",    ST_TOGGLE, CONF_tcp_nodelay,     .desc="Send packets faster (Nagle off). Less lag, more traffic." },
                { "TCP keepalives",  ST_TOGGLE, CONF_tcp_keepalives,  .desc="Also use the OS's TCP-level keepalives" },
            }},
            { "ADDRESS FAMILY", 1, {
                { "Prefer", ST_CHOICE, CONF_addressfamily, 3,
                  {"Auto","IPv4","IPv6"}, {0,1,2},
                  "Which IP version to prefer when the host has both" },
            }},
        }
    },

    /* ---- VIEW 3: SSH AUTH ---- */
    {
        "SSH Auth", "Private key, agent, and authentication options.",
        3,
        {
            { "AUTHENTICATION", 3, {
                { "Private key file", ST_FILE,   CONF_keyfile,  .desc="Your .ppk file for passwordless login" },
                { "Try SSH agent",    ST_TOGGLE, CONF_tryagent, .desc="Use Pageant if it's running" },
                { "Agent forwarding", ST_TOGGLE, CONF_agentfwd, .desc="Let the server use your local keys (convenient but risky)" },
            }},
            { "AUTH OPTIONS", 4, {
                { "No user auth",        ST_TOGGLE, CONF_ssh_no_userauth,         .desc="Skip the login step entirely (for specific legacy setups)" },
                { "No trivial auth",     ST_TOGGLE, CONF_ssh_no_trivial_userauth, .desc="Reject servers that accept anyone without checking" },
                { "Show banner",         ST_TOGGLE, CONF_ssh_show_banner,         .desc="Show the pre-login banner the server sends" },
                { "Keyboard-interactive",ST_TOGGLE, CONF_try_ki_auth,             .desc="Answer custom questions (like 2FA prompts) from the server" },
            }},
            { "SSH PROTOCOL", 2, {
                { "Version", ST_CHOICE, CONF_sshprot, 2,
                  {"SSH-1 only","SSH-2 only"}, {0,3},
                  "Which SSH protocol version to use. Always use SSH-2 unless forced." },
                { "Allow username change", ST_TOGGLE, CONF_change_username, .desc="Let you re-enter the username if the first attempt fails" },
            }},
        }
    },

    /* ---- VIEW 4: LOGGING ---- */
    {
        "Logging", "Session logging type, file, and privacy options.",
        4,
        {
            { "LOG TYPE", 1, {
                { "Type", ST_CHOICE, CONF_logtype, 4,
                  {"None","Printable","All traffic","SSH packets"}, {0,1,2,3},
                  "What to log. 'Printable' is usually enough." },
            }},
            { "LOG FILE", 1, {
                { "File path", ST_FILE, CONF_logfilename, .desc="Where to save the log" },
            }},
            { "OPTIONS", 4, {
                { "Flush frequently",  ST_TOGGLE, CONF_logflush,    .desc="Write to disk as you go (safer on crash, slower)" },
                { "Include header",    ST_TOGGLE, CONF_logheader,   .desc="Add a timestamp banner at the top of each log" },
                { "Omit passwords",    ST_TOGGLE, CONF_logomitpass, .desc="Scrub anything that looks like a password" },
                { "Omit session data", ST_TOGGLE, CONF_logomitdata, .desc="Log only your input, not what the server sent back" },
            }},
            { "IF FILE EXISTS", 1, {
                { "Action", ST_CHOICE, CONF_logxfovr, 3,
                  {"Overwrite","Append","Ask"}, {0,1,2},
                  "What to do when the log file is already there" },
            }},
        }
    },

    /* ---- VIEW 5: COLOURS ----
     * For ST_COLOUR items, conf_key holds the CONF_COLOUR_xxx slot (0-21).
     * The actual conf key is always CONF_colours (handled internally).
     * This build uses the 22-colour variant (no underline/selection slots). */
    {
        "Colours", "Click any swatch to open the colour picker.",
        4,
        {
            { "DEFAULT TEXT", 4, {
                { "Foreground",      ST_COLOUR, CONF_COLOUR_fg },
                { "Bold foreground", ST_COLOUR, CONF_COLOUR_fg_bold },
                { "Background",      ST_COLOUR, CONF_COLOUR_bg },
                { "Bold background", ST_COLOUR, CONF_COLOUR_bg_bold },
            }},
            { "CURSOR", 2, {
                { "Cursor text",   ST_COLOUR, CONF_COLOUR_cursor_fg },
                { "Cursor colour", ST_COLOUR, CONF_COLOUR_cursor_bg },
            }},
            { "ANSI NORMAL", 8, {
                { "Black",   ST_COLOUR, CONF_COLOUR_black },
                { "Red",     ST_COLOUR, CONF_COLOUR_red },
                { "Green",   ST_COLOUR, CONF_COLOUR_green },
                { "Yellow",  ST_COLOUR, CONF_COLOUR_yellow },
                { "Blue",    ST_COLOUR, CONF_COLOUR_blue },
                { "Magenta", ST_COLOUR, CONF_COLOUR_magenta },
                { "Cyan",    ST_COLOUR, CONF_COLOUR_cyan },
                { "White",   ST_COLOUR, CONF_COLOUR_white },
            }},
            { "ANSI BOLD", 8, {
                { "Black (bold)",   ST_COLOUR, CONF_COLOUR_black_bold },
                { "Red (bold)",     ST_COLOUR, CONF_COLOUR_red_bold },
                { "Green (bold)",   ST_COLOUR, CONF_COLOUR_green_bold },
                { "Yellow (bold)",  ST_COLOUR, CONF_COLOUR_yellow_bold },
                { "Blue (bold)",    ST_COLOUR, CONF_COLOUR_blue_bold },
                { "Magenta (bold)", ST_COLOUR, CONF_COLOUR_magenta_bold },
                { "Cyan (bold)",    ST_COLOUR, CONF_COLOUR_cyan_bold },
                { "White (bold)",   ST_COLOUR, CONF_COLOUR_white_bold },
            }},
        }
    },

    /* ---- VIEW 6: PROXY ---- */
    {
        "Proxy", "Route the connection through a SOCKS or HTTP proxy.",
        4,
        {
            { "PROXY TYPE", 1, {
                { "Protocol", ST_CHOICE, CONF_proxy_type, 4,
                  {"None","SOCKS 4","SOCKS 5","HTTP"},
                  {0, 1, 2, 3},
                  "Which kind of proxy. SOCKS 5 is most common." },
            }},
            { "PROXY SERVER", 2, {
                { "Host", ST_STR, CONF_proxy_host, .desc="Proxy address (IP or hostname)" },
                { "Port", ST_INT, CONF_proxy_port, .desc="Proxy port number" },
            }},
            { "AUTHENTICATION", 2, {
                { "Username", ST_STR, CONF_proxy_username, .desc="Login for the proxy (leave blank if none)" },
                { "Password", ST_STR, CONF_proxy_password, .desc="Password for the proxy" },
            }},
            { "OPTIONS", 3, {
                { "Exclude list", ST_STR, CONF_proxy_exclude_list, .desc="Comma-separated hosts to connect to directly, skipping the proxy" },
                { "Resolve names", ST_CHOICE, CONF_proxy_dns, 3,
                  {"Locally","Via proxy","Auto"},
                  {1, 0, 2},
                  "Who looks up hostnames: your PC or the proxy" },
                { "Proxy localhost", ST_TOGGLE, CONF_even_proxy_localhost, .desc="Use the proxy even for 127.0.0.1 / localhost" },
            }},
        }
    },

    /* ---- VIEW 7: APPEARANCE ---- */
    {
        "Appearance", "Transparency, background image, and custom window icon.",
        3,
        {
            { "TRANSPARENCY", 1, {
                { "Transparency (0-255)", ST_INT, CONF_transparencynumber, .desc="0 = invisible, 255 = solid" },
            }},
            { "BACKGROUND", 5, {
                { "Type", ST_CHOICE, CONF_bg_type, 3,
                  {"Solid","Wallpaper","Image"}, {0,1,2},
                  "Where the background comes from" },
                { "Opacity (0-255)",  ST_INT,  CONF_bg_opacity,           .desc="How solid the background is (0 = see-through, 255 = opaque)" },
                { "Image file",       ST_FILE, CONF_bg_image_filename,    .desc="Path to the background image" },
                { "Image style", ST_CHOICE, CONF_bg_image_style, 6,
                  {"Tile","Center","Stretch","Abs","Blank","Stretch+"},
                  {0,1,2,3,4,5},
                  "How the image fills the window" },
                { "Slideshow interval", ST_INT, CONF_bg_slideshow,         .desc="Seconds between image changes (0 = no slideshow)" },
            }},
            { "ICON", 2, {
                { "Use custom icon", ST_TOGGLE, CONF_icone,      .desc="Override the default window icon" },
                { "Icon file",       ST_FILE,   CONF_iconefile,  .desc="Your .ico file" },
            }},
        }
    },

    /* ---- VIEW 8: BEHAVIOUR ---- */
    {
        "Behaviour", "Window state, focus, scroll, and interaction.",
        2,
        {
            { "WINDOW STATE", 6, {
                { "Start maximized",     ST_TOGGLE, CONF_maximize,               .desc="Open the window at full size" },
                { "Start fullscreen",    ST_TOGGLE, CONF_fullscreen,             .desc="Open the window without borders" },
                { "Fullscreen on Alt+Enter", ST_TOGGLE, CONF_fullscreenonaltenter, .desc="Toggle fullscreen with Alt+Enter" },
                { "Send to tray on minimize", ST_TOGGLE, CONF_sendtotray,        .desc="Hide to system tray instead of the taskbar" },
                { "Save window position", ST_TOGGLE, CONF_save_windowpos,        .desc="Remember window position for next time" },
                { "Save session on exit", ST_TOGGLE, CONF_saveonexit,            .desc="Save any config changes when closing" },
            }},
            { "INTERACTION", 6, {
                { "Foreground on bell",       ST_TOGGLE, CONF_foreground_on_bell, .desc="Bring the window forward when the server beeps" },
                { "Ctrl+Tab window switch",   ST_CHOICE, CONF_ctrl_tab_switch, 2,
                  {"Off","On"}, {0,1},
                  "Let Ctrl+Tab cycle between open RhinoSSH windows" },
                { "Warn on OSC52 clipboard",  ST_TOGGLE, CONF_osc52_warn_before_cliboard_sync, .desc="Ask before the server copies text into your clipboard" },
                { "Scroll lines per wheel",   ST_INT,    CONF_scrolllines,       .desc="How many lines one wheel notch scrolls" },
                { "Print tunnel in title",    ST_TOGGLE, CONF_ssh_tunnel_print_in_title, .desc="Show active port forwards in the titlebar" },
                { "Disable AltGr",            ST_TOGGLE, CONF_disablealtgr,      .desc="Treat AltGr as a regular right Alt" },
            }},
        }
    },

    /* ---- VIEW 9: URLS (MOD_HYPERLINK) ---- */
    {
        "URLs", "Clickable hyperlinks in terminal output.",
        3,
        {
            { "DETECTION", 2, {
                { "Ctrl+Click opens URL", ST_CHOICE, CONF_url_ctrl_click, 2,
                  {"Off","On"}, {0,1},
                  "Hold Ctrl and click a link to open it" },
                { "Underline URLs", ST_CHOICE, CONF_url_underline, 3,
                  {"Always","Hover","Never"}, {0,1,2},
                  "When to underline detected links" },
            }},
            { "BROWSER", 2, {
                { "Use default browser", ST_CHOICE, CONF_url_defbrowser, 2,
                  {"Off","On"}, {0,1},
                  "Open with your system's default browser" },
                { "Custom browser", ST_FILE, CONF_url_browser, .desc="Or pick a specific browser .exe" },
            }},
            { "REGEX", 2, {
                { "Use default pattern", ST_CHOICE, CONF_url_defregex, 2,
                  {"Off","On"}, {0,1},
                  "Use the built-in URL matcher" },
                { "Custom pattern", ST_STR, CONF_url_regex, .desc="Advanced: regex for detecting URLs" },
            }},
        }
    },

    /* ---- VIEW 10: AUTOMATION ---- */
    {
        "Automation", "Commands, anti-idle, login script, and auto-reconnect.",
        4,
        {
            { "AUTO COMMANDS", 3, {
                { "On connect",    ST_STR, CONF_autocommand,     .desc="Run this right after login" },
                { "On disconnect", ST_STR, CONF_autocommandout,  .desc="Run this before closing the connection" },
                { "Anti-idle",     ST_STR, CONF_antiidle,        .desc="Send this text periodically to stop timeouts" },
            }},
            { "LOGIN SCRIPT", 2, {
                { "Script file",    ST_FILE, CONF_scriptfile,        .desc="A script to run at login" },
                { "Script content", ST_STR,  CONF_scriptfilecontent, .desc="Or type the script right here (runs at connect)" },
            }},
            { "AUTO-RECONNECT (MOD_RECONNECT)", 2, {
                { "On wakeup (secs)",  ST_INT, CONF_wakeup_reconnect,  .desc="Seconds after PC wake-up to reconnect (0 = off)" },
                { "On failure (secs)", ST_INT, CONF_failure_reconnect, .desc="Seconds to wait before retrying a dropped connection (0 = off)" },
            }},
            { "SESSION NAME", 1, {
                { "Name for scripts", ST_STR, CONF_sessionname, .desc="Label scripts can use to identify this session" },
            }},
        }
    },

    /* ---- VIEW 11: ADVANCED ---- */
    {
        "Advanced", "Session metadata, delays, port knocking, ZModem, printing.",
        7,
        {
            { "DEFAULTS", 1, {
                { "Restore defaults...", ST_BUTTON, 0, .desc="Reset settings on any page back to factory defaults" },
            }},
            { "SESSION METADATA", 3, {
                { "Folder",     ST_STR, CONF_folder,   .desc="Group this session under a folder in the saved sessions list" },
                { "Comment",    ST_STR, CONF_comment,  .desc="Your personal notes about this session" },
                { "Host alias", ST_STR, CONF_host_alt, .desc="Backup hostname to try if the main one fails" },
            }},
            { "TERMINAL HINTS", 3, {
                { "Terminal type",    ST_STR, CONF_termtype,   .desc="What the server thinks your terminal is (usually xterm)" },
                { "Terminal speed",   ST_STR, CONF_termspeed,  .desc="Baud rate the server sees, e.g. 38400,38400 (cosmetic)" },
                { "Answerback",       ST_STR, CONF_answerback, .desc="What to send when the server asks 'who are you?' (Ctrl+E)" },
            }},
            { "DELAYS", 2, {
                { "Back-channel (ms)", ST_INT, CONF_bcdelay,   .desc="Milliseconds between keystrokes sent to the server" },
                { "Initial (ms)",      ST_INT, CONF_initdelay, .desc="Pause before sending anything after connecting" },
            }},
            { "PORT KNOCKING", 1, {
                { "Sequence", ST_STR, CONF_portknockingoptions, .desc="Knock sequence: 'host:port:proto,host:port:proto,...' before connect" },
            }},
            { "ZMODEM", 5, {
                { "Receive command", ST_FILE, CONF_rzcommand,   .desc="Path to rz.exe (receives files via ZModem)" },
                { "Receive options", ST_STR,  CONF_rzoptions,   .desc="Arguments passed to rz" },
                { "Send command",    ST_FILE, CONF_szcommand,   .desc="Path to sz.exe (sends files via ZModem)" },
                { "Send options",    ST_STR,  CONF_szoptions,   .desc="Arguments passed to sz" },
                { "Download dir",    ST_STR,  CONF_zdownloaddir, .desc="Where received files end up" },
            }},
            { "PRINTING", 1, {
                { "Printer", ST_STR, CONF_printer, .desc="Name of the printer for ANSI printing commands" },
            }},
        }
    },
};

/* ============================================================
 *  MODULE STATE
 * ============================================================ */

static HWND   g_dialog   = NULL;
static HWND   g_hwnd     = NULL;   /* the settings panel window */
static Conf  *g_conf     = NULL;
static HFONT  g_font     = NULL;
static HFONT  g_font_title = NULL;
static HFONT  g_font_small = NULL;
static HFONT  g_font_label = NULL;
static int    g_current_view = -1;
static BOOL   g_visible  = FALSE;
static BOOL   g_loading  = FALSE;  /* suppresses EN_CHANGE during load_view_values */
static int    g_scroll_y = 0;      /* current scroll offset for the active view */
static int    g_content_h = 0;     /* total pixel height of the active view's content */

/* Hover tracking */
static int    g_hover_card = -1;
static int    g_hover_item = -1;
static int    g_hover_pill = -1;   /* for ST_CHOICE / ST_TOGGLE */

static const char *SETTINGS_CLASS = "RhinoSshSettings";

/* ============================================================
 *  GEOMETRY CONSTANTS (match content.c style)
 * ============================================================ */

#define HEADER_H     70
#define CARD_RADIUS   8
#define CARD_PAD_X   18
#define CARD_PAD_Y   14
#define INPUT_H      28
#define LABEL_H      16
#define PILL_H       26
#define PILL_PAD_X   12
#define PILL_GAP      6
#define CARD_GAP     12
#define ROW_H        32     /* height of one setting row (toggle/choice) */
#define FOOTER_H     24
#define INT_INPUT_W  80
#define STR_INPUT_W 260

/* Edit control base ID — we encode (card*100 + item) as offset.
 * edit IDs: 7000 + card*100 + item — max 7000 + 6*100 + 7 = 7607
 * browse IDs: 8000 + card*100 + item — max 8000 + 6*100 + 7 = 8607
 * (Bases must be far enough apart to avoid overlap.) */
#define IDC_SETTINGS_EDIT_BASE 7000
#define IDC_SETTINGS_BROWSE_BASE 8000
#define BROWSE_BTN_W  32

static int edit_id(int card, int item) {
    return IDC_SETTINGS_EDIT_BASE + card * 100 + item;
}
static int browse_id(int card, int item) {
    return IDC_SETTINGS_BROWSE_BASE + card * 100 + item;
}

/* ============================================================
 *  PAINT HELPERS (reused from content.c pattern)
 * ============================================================ */

static void fill_round(HDC hdc, RECT *rc, COLORREF color, int radius)
{
    HBRUSH br = CreateSolidBrush(color);
    HPEN pen = CreatePen(PS_SOLID, 1, color);
    HBRUSH ob = (HBRUSH)SelectObject(hdc, br);
    HPEN op = (HPEN)SelectObject(hdc, pen);
    RoundRect(hdc, rc->left, rc->top, rc->right, rc->bottom, radius, radius);
    SelectObject(hdc, ob); SelectObject(hdc, op);
    DeleteObject(br); DeleteObject(pen);
}

static void stroke_round(HDC hdc, RECT *rc, COLORREF color, int radius)
{
    HPEN pen = CreatePen(PS_SOLID, 1, color);
    HPEN op = (HPEN)SelectObject(hdc, pen);
    HBRUSH ob = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
    RoundRect(hdc, rc->left, rc->top, rc->right, rc->bottom, radius, radius);
    SelectObject(hdc, ob); SelectObject(hdc, op);
    DeleteObject(pen);
}

/* ============================================================
 *  CONF READ/WRITE HELPERS
 * ============================================================ */

static int read_conf_bool(int key)
{
    return g_conf ? conf_get_bool(g_conf, key) : 0;
}

static int read_conf_int(int key)
{
    return g_conf ? conf_get_int(g_conf, key) : 0;
}

static const char *read_conf_str(int key)
{
    return g_conf ? conf_get_str(g_conf, key) : "";
}

static void write_conf_bool(int key, int val)
{
    if (g_conf) conf_set_bool(g_conf, key, val);
}

static void write_conf_int(int key, int val)
{
    if (g_conf) conf_set_int(g_conf, key, val);
}

static void write_conf_str(int key, const char *val)
{
    if (g_conf) conf_set_str(g_conf, key, val);
}

/* ST_COLOUR: colour slot 0-33, stored as 3 ints (R,G,B) at slot*3+{0,1,2} */
static COLORREF read_conf_colour(int slot)
{
    if (!g_conf) return RGB(0, 0, 0);
    int r = conf_get_int_int(g_conf, CONF_colours, slot * 3 + 0);
    int g = conf_get_int_int(g_conf, CONF_colours, slot * 3 + 1);
    int b = conf_get_int_int(g_conf, CONF_colours, slot * 3 + 2);
    return RGB(r & 0xff, g & 0xff, b & 0xff);
}

static void write_conf_colour(int slot, COLORREF c)
{
    if (!g_conf) return;
    conf_set_int_int(g_conf, CONF_colours, slot * 3 + 0, GetRValue(c));
    conf_set_int_int(g_conf, CONF_colours, slot * 3 + 1, GetGValue(c));
    conf_set_int_int(g_conf, CONF_colours, slot * 3 + 2, GetBValue(c));
}

/* Read the current value for any item (returns int; for STR see edit) */
static int item_current_value(SettingItem *si)
{
    switch (si->type) {
      case ST_TOGGLE: return read_conf_bool(si->conf_key);
      case ST_CHOICE: return read_conf_int(si->conf_key);
      case ST_INT:    return read_conf_int(si->conf_key);
      default:        return 0;
    }
}

/* ============================================================
 *  LOAD VALUES FROM CONF INTO EDIT CONTROLS
 * ============================================================ */

static void load_view_values(int view_idx)
{
    if (view_idx < 0 || view_idx >= RHINO_SETTINGS_COUNT) return;
    SettingsView *v = &g_views[view_idx];

    g_loading = TRUE;

    for (int c = 0; c < v->n_cards; c++) {
        SettingCard *card = &v->cards[c];
        for (int i = 0; i < card->n_items; i++) {
            SettingItem *si = &card->items[i];
            if (si->edit_hwnd && IsWindow(si->edit_hwnd)) {
                if (si->type == ST_INT) {
                    char buf[32];
                    snprintf(buf, sizeof(buf), "%d", read_conf_int(si->conf_key));
                    SetWindowTextA(si->edit_hwnd, buf);
                } else if (si->type == ST_STR) {
                    SetWindowTextA(si->edit_hwnd, read_conf_str(si->conf_key));
                } else if (si->type == ST_FILE) {
                    Filename *fn = conf_get_filename(g_conf, si->conf_key);
                    const char *path = fn ? filename_to_str(fn) : "";
                    SetWindowTextA(si->edit_hwnd, path ? path : "");
                } else if (si->type == ST_FONT) {
                    FontSpec *fs = conf_get_fontspec(g_conf, si->conf_key);
                    if (fs && fs->name) {
                        char desc[256];
                        snprintf(desc, sizeof(desc), "%s, %dpt%s",
                                 fs->name, fs->height,
                                 fs->isbold ? ", Bold" : "");
                        SetWindowTextA(si->edit_hwnd, desc);
                    } else {
                        SetWindowTextA(si->edit_hwnd, "(default)");
                    }
                }
            }
        }
    }
    g_loading = FALSE;
}

/* ============================================================
 *  SAVE VALUES FROM EDIT CONTROLS BACK TO CONF
 * ============================================================ */

static void save_edit_values(int view_idx)
{
    if (view_idx < 0 || view_idx >= RHINO_SETTINGS_COUNT) return;
    SettingsView *v = &g_views[view_idx];

    for (int c = 0; c < v->n_cards; c++) {
        SettingCard *card = &v->cards[c];
        for (int i = 0; i < card->n_items; i++) {
            SettingItem *si = &card->items[i];
            if (!si->edit_hwnd || !IsWindow(si->edit_hwnd)) continue;
            if (si->type == ST_INT) {
                char buf[32];
                GetWindowTextA(si->edit_hwnd, buf, sizeof(buf));
                int val = atoi(buf);
                write_conf_int(si->conf_key, val);
            } else if (si->type == ST_STR) {
                char buf[512];
                GetWindowTextA(si->edit_hwnd, buf, sizeof(buf));
                write_conf_str(si->conf_key, buf);
            } else if (si->type == ST_FILE) {
                char buf[512];
                GetWindowTextA(si->edit_hwnd, buf, sizeof(buf));
                if (g_conf) {
                    Filename *fn = filename_from_str(buf);
                    conf_set_filename(g_conf, si->conf_key, fn);
                    filename_free(fn);
                }
            }
        }
    }
}

/* ============================================================
 *  LAYOUT COMPUTATION
 * ============================================================ */

static void compute_layout(int w, int h, int view_idx)
{
    if (view_idx < 0 || view_idx >= RHINO_SETTINGS_COUNT) return;
    SettingsView *v = &g_views[view_idx];

    int x0 = 24;
    int y_orig = HEADER_H + 8;     /* unscrolled starting y */
    int y = y_orig - g_scroll_y;   /* scrolled content starts higher */
    int card_w = w - x0 * 2;

    /* Real text measurement for accurate pill widths */
    HDC measure_dc = g_hwnd ? GetDC(g_hwnd) : NULL;
    HFONT old_font = NULL;
    if (measure_dc && g_font_small)
        old_font = (HFONT)SelectObject(measure_dc, g_font_small);

    for (int c = 0; c < v->n_cards; c++) {
        SettingCard *card = &v->cards[c];
        int cy = y + CARD_PAD_Y;
        /* Card title takes LABEL_H + 8 */
        cy += LABEL_H + 8;

        for (int i = 0; i < card->n_items; i++) {
            SettingItem *si = &card->items[i];
            int cx = x0 + CARD_PAD_X;
            int cw = card_w - CARD_PAD_X * 2;
            int row_extra = 0;  /* extra height if pills wrap */

            si->label_rc = (RECT){ cx, cy + 2, cx + 160, cy + ROW_H - 2 };

            switch (si->type) {
              case ST_TOGGLE: {
                /* Single on/off pill to the right of label */
                int pw = 44;  /* "ON" / "OFF" pill width */
                int px = cx + cw - pw;
                si->ctrl_rc = (RECT){ px, cy + 2, px + pw, cy + PILL_H + 2 };
                break;
              }
              case ST_CHOICE: {
                /* Row of pills after the label — measure real text width,
                 * wrap to next row if pill exceeds card width */
                int px = cx + 164;
                int py = cy + 2;
                int right_edge = cx + cw;
                for (int j = 0; j < si->n_opts; j++) {
                    int tw;
                    if (measure_dc) {
                        SIZE sz;
                        GetTextExtentPoint32A(measure_dc, si->opt_labels[j],
                                              (int)strlen(si->opt_labels[j]), &sz);
                        tw = sz.cx + PILL_PAD_X * 2;
                    } else {
                        tw = (int)strlen(si->opt_labels[j]) * 7 + PILL_PAD_X * 2;
                    }
                    /* Wrap if this pill would exceed card right edge */
                    if (j > 0 && px + tw > right_edge) {
                        px = cx + 164;
                        py += PILL_H + 4;
                        row_extra += PILL_H + 4;
                    }
                    si->pill_rcs[j] = (RECT){ px, py, px + tw, py + PILL_H };
                    px += tw + PILL_GAP;
                }
                si->ctrl_rc = si->pill_rcs[0];
                break;
              }
              case ST_INT: {
                int ex = cx + cw - INT_INPUT_W;
                si->ctrl_rc = (RECT){ ex, cy + 1, ex + INT_INPUT_W, cy + INPUT_H + 1 };
                break;
              }
              case ST_STR: {
                int sw = cw - 164;
                if (sw > STR_INPUT_W) sw = STR_INPUT_W;
                int ex = cx + 164;
                si->ctrl_rc = (RECT){ ex, cy + 1, ex + sw, cy + INPUT_H + 1 };
                break;
              }
              case ST_FILE: {
                int sw = cw - 164 - BROWSE_BTN_W - 6;
                if (sw > STR_INPUT_W - BROWSE_BTN_W - 6)
                    sw = STR_INPUT_W - BROWSE_BTN_W - 6;
                int ex = cx + 164;
                si->ctrl_rc = (RECT){ ex, cy + 1, ex + sw, cy + INPUT_H + 1 };
                si->browse_rc = (RECT){ ex + sw + 6, cy + 1,
                                        ex + sw + 6 + BROWSE_BTN_W, cy + INPUT_H + 1 };
                break;
              }
              case ST_FONT: {
                /* Read-only display + wider "Choose..." button */
                int btnW = 56;
                int sw = cw - 164 - btnW - 6;
                if (sw > STR_INPUT_W - btnW - 6)
                    sw = STR_INPUT_W - btnW - 6;
                int ex = cx + 164;
                si->ctrl_rc = (RECT){ ex, cy + 1, ex + sw, cy + INPUT_H + 1 };
                si->browse_rc = (RECT){ ex + sw + 6, cy + 1,
                                        ex + sw + 6 + btnW, cy + INPUT_H + 1 };
                break;
              }
              case ST_COLOUR: {
                /* Swatch (60x22) on the right */
                int swatchW = 60;
                int swatchH = 22;
                int px = cx + cw - swatchW;
                si->ctrl_rc = (RECT){ px, cy + 3, px + swatchW, cy + 3 + swatchH };
                break;
              }
              case ST_BUTTON: {
                /* Button on the right, sized to label text */
                int btnW = 160;
                int btnH = INPUT_H;
                int px = cx + cw - btnW;
                si->ctrl_rc = (RECT){ px, cy + 1, px + btnW, cy + 1 + btnH };
                break;
              }
            }
            cy += ROW_H + 2 + row_extra;
            /* Reserve an extra line for the help description */
            if (si->desc && si->desc[0])
                cy += 14;
        }
        cy += CARD_PAD_Y - 4;
        card->card_rc = (RECT){ x0, y, x0 + card_w, cy };
        y = cy + CARD_GAP;
    }

    if (measure_dc) {
        if (old_font) SelectObject(measure_dc, old_font);
        ReleaseDC(g_hwnd, measure_dc);
    }

    /* Remember total content height in pixels (just the card area —
     * header/footer are fixed chrome handled separately).
     * y here is in scrolled coordinates; y_orig was the top of the
     * first card's scrollable area, so add scroll back to get the
     * unscrolled bottom, then subtract y_orig to get just the height. */
    g_content_h = (y + g_scroll_y) - y_orig + 8;  /* +8 = bottom margin */
    if (g_content_h < 0) g_content_h = 0;
}

/* ============================================================
 *  POSITION EDIT CONTROLS
 * ============================================================ */

static void position_edits(int view_idx)
{
    if (view_idx < 0 || view_idx >= RHINO_SETTINGS_COUNT) return;
    SettingsView *v = &g_views[view_idx];

    for (int c = 0; c < v->n_cards; c++) {
        SettingCard *card = &v->cards[c];
        for (int i = 0; i < card->n_items; i++) {
            SettingItem *si = &card->items[i];
            if (si->edit_hwnd && IsWindow(si->edit_hwnd)) {
                RECT *r = &si->ctrl_rc;
                SetWindowPos(si->edit_hwnd, NULL,
                             r->left + 1, r->top + 1,
                             r->right - r->left - 2, r->bottom - r->top - 2,
                             SWP_NOZORDER | SWP_NOACTIVATE);
            }
            if (si->browse_hwnd && IsWindow(si->browse_hwnd)) {
                RECT *b = &si->browse_rc;
                SetWindowPos(si->browse_hwnd, NULL,
                             b->left, b->top,
                             b->right - b->left, b->bottom - b->top,
                             SWP_NOZORDER | SWP_NOACTIVATE);
            }
        }
    }
}

/* ============================================================
 *  SHOW/HIDE EDIT CONTROLS FOR CURRENT VIEW
 * ============================================================ */

static void show_view_edits(int view_idx, int show)
{
    if (view_idx < 0 || view_idx >= RHINO_SETTINGS_COUNT) return;
    SettingsView *v = &g_views[view_idx];
    int cmd = show ? SW_SHOW : SW_HIDE;

    for (int c = 0; c < v->n_cards; c++) {
        SettingCard *card = &v->cards[c];
        for (int i = 0; i < card->n_items; i++) {
            SettingItem *si = &card->items[i];
            if (si->edit_hwnd && IsWindow(si->edit_hwnd))
                ShowWindow(si->edit_hwnd, cmd);
            if (si->browse_hwnd && IsWindow(si->browse_hwnd))
                ShowWindow(si->browse_hwnd, cmd);
        }
    }
}

/* ============================================================
 *  PAINT
 * ============================================================ */

static void paint_header(HDC hdc, int w, int view_idx)
{
    if (view_idx < 0 || view_idx >= RHINO_SETTINGS_COUNT) return;
    SettingsView *v = &g_views[view_idx];

    /* Title */
    HFONT of = g_font_title ? (HFONT)SelectObject(hdc, g_font_title) : NULL;
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RHINO_TEXT);
    RECT tr = { 28, 22, 300, 50 };
    DrawTextA(hdc, v->title, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    if (of) SelectObject(hdc, of);

    /* Subtitle */
    HFONT sf = g_font_small ? (HFONT)SelectObject(hdc, g_font_small) : NULL;
    SetTextColor(hdc, RHINO_TEXT_MUTED);
    RECT sr = { 28, 54, w - 28, 68 };
    DrawTextA(hdc, v->subtitle, -1, &sr, DT_LEFT | DT_TOP | DT_SINGLELINE);
    if (sf) SelectObject(hdc, sf);
}

static void paint_toggle(HDC hdc, SettingItem *si, int card_idx, int item_idx)
{
    int val = item_current_value(si);
    RECT *rc = &si->ctrl_rc;
    BOOL hover = (card_idx == g_hover_card && item_idx == g_hover_item);

    if (val) {
        fill_round(hdc, rc, RHINO_ACCENT, 6);
    } else {
        fill_round(hdc, rc, RHINO_BG_EDIT, 6);
        stroke_round(hdc, rc, hover ? RHINO_ACCENT : RHINO_BORDER, 6);
    }

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, val ? RHINO_TEXT_ON_ACCENT : RHINO_TEXT_MUTED);
    HFONT of = g_font_small ? (HFONT)SelectObject(hdc, g_font_small) : NULL;
    DrawTextA(hdc, val ? "ON" : "OFF", -1, rc,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    if (of) SelectObject(hdc, of);
}

static void paint_choice(HDC hdc, SettingItem *si, int card_idx, int item_idx)
{
    int val = item_current_value(si);
    SetBkMode(hdc, TRANSPARENT);
    HFONT of = g_font_small ? (HFONT)SelectObject(hdc, g_font_small) : NULL;

    for (int j = 0; j < si->n_opts; j++) {
        RECT *rc = &si->pill_rcs[j];
        BOOL active = (si->opt_values[j] == val);
        BOOL hover = (card_idx == g_hover_card && item_idx == g_hover_item
                      && j == g_hover_pill);

        if (active)
            fill_round(hdc, rc, RHINO_ACCENT, 6);
        else {
            fill_round(hdc, rc, RHINO_BG_EDIT, 6);
            stroke_round(hdc, rc, hover ? RHINO_ACCENT : RHINO_BORDER, 6);
        }

        SetTextColor(hdc, active ? RHINO_TEXT_ON_ACCENT : RHINO_TEXT);
        DrawTextA(hdc, si->opt_labels[j], -1, rc,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    if (of) SelectObject(hdc, of);
}

static void paint_colour(HDC hdc, SettingItem *si, int card_idx, int item_idx)
{
    RECT *rc = &si->ctrl_rc;
    BOOL hover = (card_idx == g_hover_card && item_idx == g_hover_item);

    /* Read the current colour from Conf */
    COLORREF c = read_conf_colour(si->conf_key);

    /* Draw the filled swatch */
    fill_round(hdc, rc, c, 4);
    stroke_round(hdc, rc,
                 hover ? RHINO_ACCENT : RHINO_BORDER, 4);

    /* Draw hex value centred over swatch (with contrasting text colour) */
    int luma = (GetRValue(c) * 299 + GetGValue(c) * 587
                + GetBValue(c) * 114) / 1000;
    COLORREF textCol = (luma > 128) ? RGB(0, 0, 0) : RGB(255, 255, 255);

    char hex[16];
    snprintf(hex, sizeof(hex), "#%02X%02X%02X",
             GetRValue(c), GetGValue(c), GetBValue(c));
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, textCol);
    HFONT of = g_font_small ? (HFONT)SelectObject(hdc, g_font_small) : NULL;
    DrawTextA(hdc, hex, -1, rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    if (of) SelectObject(hdc, of);
}

static void paint_view(HDC hdc, int w, int h, int view_idx)
{
    if (view_idx < 0 || view_idx >= RHINO_SETTINGS_COUNT) return;
    SettingsView *v = &g_views[view_idx];

    /* Background */
    RECT full = { 0, 0, w, h };
    HBRUSH bgBr = CreateSolidBrush(RHINO_BG_DEEP);
    FillRect(hdc, &full, bgBr);
    DeleteObject(bgBr);

    /* Clip the card area so scrolled-off content doesn't draw over
     * the header or footer strips */
    HRGN clip = CreateRectRgn(0, HEADER_H, w, h - FOOTER_H);
    SelectClipRgn(hdc, clip);

    HFONT labelf = g_font ? (HFONT)SelectObject(hdc, g_font) : NULL;
    SetBkMode(hdc, TRANSPARENT);

    for (int c = 0; c < v->n_cards; c++) {
        SettingCard *card = &v->cards[c];

        /* Card background + border */
        fill_round(hdc, &card->card_rc, RHINO_BG_SURFACE, CARD_RADIUS);
        stroke_round(hdc, &card->card_rc, RHINO_BORDER, CARD_RADIUS);

        /* Card title */
        HFONT cf = g_font_label ? (HFONT)SelectObject(hdc, g_font_label) : NULL;
        SetTextColor(hdc, RHINO_TEXT_MUTED);
        RECT ctr = { card->card_rc.left + CARD_PAD_X,
                     card->card_rc.top + CARD_PAD_Y,
                     card->card_rc.right - CARD_PAD_X,
                     card->card_rc.top + CARD_PAD_Y + LABEL_H };
        DrawTextA(hdc, card->title, -1, &ctr,
                  DT_LEFT | DT_TOP | DT_SINGLELINE);
        if (cf) SelectObject(hdc, cf);

        /* Items */
        for (int i = 0; i < card->n_items; i++) {
            SettingItem *si = &card->items[i];

            /* Label (buttons have their own centered label, no row label) */
            if (si->type != ST_BUTTON) {
                if (g_font) SelectObject(hdc, g_font);
                SetTextColor(hdc, RHINO_TEXT);
                DrawTextA(hdc, si->label, -1, &si->label_rc,
                          DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            }

            /* Control */
            switch (si->type) {
              case ST_TOGGLE:
                paint_toggle(hdc, si, c, i);
                break;
              case ST_CHOICE:
                paint_choice(hdc, si, c, i);
                break;
              case ST_COLOUR:
                paint_colour(hdc, si, c, i);
                break;
              case ST_INT:
              case ST_STR:
              case ST_FILE:
              case ST_FONT:
                /* Edit controls are HWND children — just draw border */
                stroke_round(hdc, &si->ctrl_rc, RHINO_BORDER, 4);
                break;
              case ST_BUTTON: {
                /* Cyan accent-filled button with centered label */
                BOOL hovered = (g_hover_card == c && g_hover_item == i);
                COLORREF bg = hovered ? RHINO_ACCENT : RHINO_ACCENT_DIM;
                fill_round(hdc, &si->ctrl_rc, bg, 6);
                HFONT of = g_font ? (HFONT)SelectObject(hdc, g_font) : NULL;
                SetTextColor(hdc, hovered ? RHINO_BG_DEEP : RHINO_ACCENT);
                DrawTextA(hdc, si->label, -1, &si->ctrl_rc,
                          DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                if (of) SelectObject(hdc, of);
                break;
              }
            }

            /* Help description — small muted text below the row */
            if (si->desc && si->desc[0]) {
                RECT dr = si->label_rc;
                dr.top = si->label_rc.bottom + 2;
                dr.bottom = dr.top + 14;
                dr.right = card->card_rc.right - CARD_PAD_X;
                HFONT ofd = g_font_small ?
                    (HFONT)SelectObject(hdc, g_font_small) : NULL;
                SetBkMode(hdc, TRANSPARENT);
                SetTextColor(hdc, RHINO_TEXT_MUTED);
                DrawTextA(hdc, si->desc, -1, &dr,
                          DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);
                if (ofd) SelectObject(hdc, ofd);
            }
        }
    }

    /* Release clip, then paint sticky header on top of cards */
    SelectClipRgn(hdc, NULL);
    DeleteObject(clip);
    paint_header(hdc, w, view_idx);

    /* Footer */
    RECT ftr = { 0, h - FOOTER_H, w, h };
    HBRUSH ftBg = CreateSolidBrush(RHINO_BG_HEADER);
    FillRect(hdc, &ftr, ftBg); DeleteObject(ftBg);
    RECT ftBdr = { 0, ftr.top, w, ftr.top + 1 };
    HBRUSH ftBdBr = CreateSolidBrush(RHINO_BORDER_SOFT);
    FillRect(hdc, &ftBdr, ftBdBr); DeleteObject(ftBdBr);
    HFONT sf = g_font_small ? (HFONT)SelectObject(hdc, g_font_small) : NULL;
    SetTextColor(hdc, RHINO_TEXT_MUTED);
    RECT ftL = ftr; ftL.left += 16;
    DrawTextA(hdc, "Changes apply on next connect", -1, &ftL,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    if (sf) SelectObject(hdc, sf);

    if (labelf) SelectObject(hdc, labelf);
}

/* ============================================================
 *  HIT-TESTING
 * ============================================================ */

/* Returns TRUE if (x,y) hits an interactive control.
 * Sets *out_card, *out_item, *out_pill. */
static BOOL hittest(int view_idx, int x, int y,
                    int *out_card, int *out_item, int *out_pill)
{
    if (view_idx < 0 || view_idx >= RHINO_SETTINGS_COUNT) return FALSE;
    SettingsView *v = &g_views[view_idx];
    POINT pt = { x, y };

    for (int c = 0; c < v->n_cards; c++) {
        SettingCard *card = &v->cards[c];
        if (!PtInRect(&card->card_rc, pt)) continue;

        for (int i = 0; i < card->n_items; i++) {
            SettingItem *si = &card->items[i];
            if (si->type == ST_TOGGLE) {
                if (PtInRect(&si->ctrl_rc, pt)) {
                    *out_card = c; *out_item = i; *out_pill = 0;
                    return TRUE;
                }
            } else if (si->type == ST_CHOICE) {
                for (int j = 0; j < si->n_opts; j++) {
                    if (PtInRect(&si->pill_rcs[j], pt)) {
                        *out_card = c; *out_item = i; *out_pill = j;
                        return TRUE;
                    }
                }
            } else if (si->type == ST_COLOUR) {
                if (PtInRect(&si->ctrl_rc, pt)) {
                    *out_card = c; *out_item = i; *out_pill = 0;
                    return TRUE;
                }
            } else if (si->type == ST_BUTTON) {
                if (PtInRect(&si->ctrl_rc, pt)) {
                    *out_card = c; *out_item = i; *out_pill = 0;
                    return TRUE;
                }
            }
        }
    }
    return FALSE;
}

/* ============================================================
 *  RESTORE DEFAULTS
 * ============================================================ */

/* Copy one item's value from src conf to g_conf. Based on item type. */
static void copy_item_from_defaults(Conf *src, SettingItem *si)
{
    if (!src || !g_conf) return;
    switch (si->type) {
      case ST_TOGGLE:
        /* MOD_PERSO conf_get_bool handles INT-typed bool keys too */
        conf_set_bool(g_conf, si->conf_key, conf_get_bool(src, si->conf_key));
        break;
      case ST_CHOICE:
      case ST_INT:
        conf_set_int(g_conf, si->conf_key, conf_get_int(src, si->conf_key));
        break;
      case ST_STR:
        conf_set_str(g_conf, si->conf_key, conf_get_str(src, si->conf_key));
        break;
      case ST_FILE: {
        Filename *fn = conf_get_filename(src, si->conf_key);
        if (fn) conf_set_filename(g_conf, si->conf_key, fn);
        break;
      }
      case ST_FONT: {
        FontSpec *fs = conf_get_fontspec(src, si->conf_key);
        if (fs) conf_set_fontspec(g_conf, si->conf_key, fs);
        break;
      }
      case ST_COLOUR: {
        int slot = si->conf_key;
        for (int k = 0; k < 3; k++) {
            int v = conf_get_int_int(src, CONF_colours, slot * 3 + k);
            conf_set_int_int(g_conf, CONF_colours, slot * 3 + k, v);
        }
        break;
      }
      case ST_BUTTON:
        /* not a real setting */
        break;
    }
}

/* Reset one view's settings to PuTTY defaults. Operates on in-memory
 * g_conf only — nothing hits disk until the user clicks SAVE. */
static void restore_defaults_for_view(int view_idx)
{
    if (view_idx < 0 || view_idx >= RHINO_SETTINGS_COUNT) return;
    if (!g_conf) return;

    /* Load factory defaults into a scratch Conf */
    Conf *tmp = conf_new();
    if (!tmp) return;
    do_defaults(NULL, tmp);

    /* Also pre-init keys settings.c's load path doesn't touch (see
     * rhinossh_settings_create for the same pair). Without this,
     * conf_get_int asserts on a fresh default Conf. */
    conf_set_int(tmp, CONF_bcdelay, 0);
    conf_set_int(tmp, CONF_initdelay, 0);

    SettingsView *v = &g_views[view_idx];
    for (int c = 0; c < v->n_cards; c++) {
        SettingCard *card = &v->cards[c];
        for (int i = 0; i < card->n_items; i++) {
            copy_item_from_defaults(tmp, &card->items[i]);
        }
    }

    conf_free(tmp);

    /* Refresh the UI if we're currently on this view */
    if (view_idx == g_current_view) {
        load_view_values(view_idx);
        if (g_hwnd) InvalidateRect(g_hwnd, NULL, FALSE);
    }
}

/* Modal picker: which view to restore? Returns view index 0..COUNT-1,
 * -1 for All pages, -2 for cancel. Uses a TrackPopupMenu — no custom
 * dialog needed. */
static int show_restore_defaults_picker(HWND parent)
{
    static const char *view_names[RHINO_SETTINGS_COUNT] = {
        "Terminal", "Window", "Connection", "SSH Auth", "Logging",
        "Colours", "Proxy", "Appearance", "Behaviour",
        "URLs", "Automation", "Advanced"
    };

    HMENU m = CreatePopupMenu();
    if (!m) return -2;
    AppendMenuA(m, MF_STRING, 100, "All pages");
    AppendMenuA(m, MF_SEPARATOR, 0, NULL);
    for (int i = 0; i < RHINO_SETTINGS_COUNT; i++)
        AppendMenuA(m, MF_STRING, 200 + i, view_names[i]);

    /* Anchor at the button's screen position */
    POINT pt;
    GetCursorPos(&pt);

    int cmd = (int)TrackPopupMenu(m,
        TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN,
        pt.x, pt.y, 0, parent, NULL);
    DestroyMenu(m);

    if (cmd == 0) return -2;           /* cancelled */
    if (cmd == 100) return -1;         /* all */
    if (cmd >= 200 && cmd < 200 + RHINO_SETTINGS_COUNT)
        return cmd - 200;
    return -2;
}

static void do_restore_defaults_button(void)
{
    int choice = show_restore_defaults_picker(g_dialog);
    if (choice == -2) return;  /* cancelled */

    if (choice == -1) {
        /* All pages */
        for (int v = 0; v < RHINO_SETTINGS_COUNT; v++)
            restore_defaults_for_view(v);
    } else {
        restore_defaults_for_view(choice);
    }
}

/* ============================================================
 *  CLICK HANDLING
 * ============================================================ */

static void handle_click(int view_idx, int card, int item, int pill)
{
    if (view_idx < 0 || view_idx >= RHINO_SETTINGS_COUNT) return;
    SettingItem *si = &g_views[view_idx].cards[card].items[item];

    if (si->type == ST_TOGGLE) {
        int cur = read_conf_bool(si->conf_key);
        write_conf_bool(si->conf_key, !cur);
    } else if (si->type == ST_CHOICE) {
        if (pill >= 0 && pill < si->n_opts)
            write_conf_int(si->conf_key, si->opt_values[pill]);
    } else if (si->type == ST_COLOUR) {
        /* Open ChooseColor dialog, write RGB back to conf */
        static COLORREF cust_colours[16] = {
            RGB(0,0,0),       RGB(128,0,0),     RGB(0,128,0),     RGB(128,128,0),
            RGB(0,0,128),     RGB(128,0,128),   RGB(0,128,128),   RGB(192,192,192),
            RGB(128,128,128), RGB(255,0,0),     RGB(0,255,0),     RGB(255,255,0),
            RGB(0,0,255),     RGB(255,0,255),   RGB(0,255,255),   RGB(255,255,255)
        };
        CHOOSECOLORA cc;
        memset(&cc, 0, sizeof(cc));
        cc.lStructSize  = sizeof(cc);
        cc.hwndOwner    = g_dialog;
        cc.lpCustColors = cust_colours;
        cc.rgbResult    = read_conf_colour(si->conf_key);
        cc.Flags        = CC_FULLOPEN | CC_RGBINIT | CC_ANYCOLOR;
        if (ChooseColorA(&cc)) {
            write_conf_colour(si->conf_key, cc.rgbResult);
        }
    } else if (si->type == ST_BUTTON) {
        /* Only one button in the whole settings system right now —
         * the Restore Defaults button on the Advanced view. */
        do_restore_defaults_button();
    }

    if (g_hwnd) InvalidateRect(g_hwnd, NULL, FALSE);
}

/* ============================================================
 *  FILE BROWSE (GetOpenFileName)
 * ============================================================ */

static void handle_browse(int view_idx, WORD btn_id)
{
    if (view_idx < 0 || view_idx >= RHINO_SETTINGS_COUNT) return;

    /* Decode card + item from button ID */
    int raw = btn_id - IDC_SETTINGS_BROWSE_BASE;
    int card = raw / 100;
    int item = raw % 100;

    SettingsView *v = &g_views[view_idx];
    if (card < 0 || card >= v->n_cards) return;
    SettingCard *cd = &v->cards[card];
    if (item < 0 || item >= cd->n_items) return;
    SettingItem *si = &cd->items[item];
    if (!si->edit_hwnd) return;

    if (si->type == ST_FILE) {
        /* File browse dialog */
        const char *filter;
        if (si->conf_key == CONF_keyfile)
            filter = "PuTTY Private Key (*.ppk)\0*.ppk\0All Files (*.*)\0*.*\0";
        else
            filter = "Log Files (*.log)\0*.log\0All Files (*.*)\0*.*\0";

        char path[512];
        GetWindowTextA(si->edit_hwnd, path, sizeof(path));

        OPENFILENAMEA ofn;
        memset(&ofn, 0, sizeof(ofn));
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner   = g_dialog;
        ofn.lpstrFilter  = filter;
        ofn.lpstrFile    = path;
        ofn.nMaxFile     = sizeof(path);
        ofn.Flags        = OFN_EXPLORER | OFN_NOCHANGEDIR;
        if (si->conf_key == CONF_keyfile)
            ofn.Flags |= OFN_FILEMUSTEXIST;

        BOOL ok = (si->conf_key == CONF_keyfile)
                  ? GetOpenFileNameA(&ofn) : GetSaveFileNameA(&ofn);
        if (ok) {
            SetWindowTextA(si->edit_hwnd, path);
            Filename *fn = filename_from_str(path);
            conf_set_filename(g_conf, si->conf_key, fn);
            filename_free(fn);
        }

    } else if (si->type == ST_FONT) {
        /* Font chooser dialog */
        FontSpec *fs = g_conf ? conf_get_fontspec(g_conf, si->conf_key) : NULL;

        LOGFONTA lf;
        memset(&lf, 0, sizeof(lf));
        if (fs && fs->name)
            strncpy(lf.lfFaceName, fs->name, LF_FACESIZE - 1);
        if (fs) {
            /* PuTTY stores height as positive pt size.
             * ChooseFont wants negative pixel height for lfHeight,
             * but CF_INITTOLOGFONTSTRUCT + iPointSize also works.
             * Use HDC to convert pt → device units. */
            HDC hdc = GetDC(g_dialog);
            int dpi = hdc ? GetDeviceCaps(hdc, LOGPIXELSY) : 96;
            if (hdc) ReleaseDC(g_dialog, hdc);
            lf.lfHeight = -MulDiv(fs->height, dpi, 72);
            lf.lfWeight = fs->isbold ? FW_BOLD : FW_NORMAL;
            lf.lfCharSet = (BYTE)fs->charset;
        }

        CHOOSEFONTA cf;
        memset(&cf, 0, sizeof(cf));
        cf.lStructSize = sizeof(cf);
        cf.hwndOwner   = g_dialog;
        cf.lpLogFont   = &lf;
        cf.Flags       = CF_SCREENFONTS | CF_FIXEDPITCHONLY |
                         CF_INITTOLOGFONTSTRUCT;

        if (ChooseFontA(&cf)) {
            /* Convert back to FontSpec */
            int pt = cf.iPointSize / 10;  /* iPointSize is in 1/10 pt */
            if (pt <= 0) pt = 10;
            bool bold = (lf.lfWeight >= FW_BOLD);
            FontSpec *newfs = fontspec_new(lf.lfFaceName, bold, pt,
                                           lf.lfCharSet);
            conf_set_fontspec(g_conf, si->conf_key, newfs);
            fontspec_free(newfs);

            /* Update display */
            char desc[256];
            snprintf(desc, sizeof(desc), "%s, %dpt%s",
                     lf.lfFaceName, pt, bold ? ", Bold" : "");
            SetWindowTextA(si->edit_hwnd, desc);
        }
    }
}

/* ============================================================
 *  WINDOW PROC
 * ============================================================ */

static LRESULT CALLBACK SettingsWndProc(HWND hwnd, UINT msg,
                                        WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
      case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);
        /* Re-layout every paint so scroll offset actually moves the cards */
        compute_layout(rc.right, rc.bottom, g_current_view);
        position_edits(g_current_view);
        paint_view(hdc, rc.right, rc.bottom, g_current_view);

        /* Update the standard Windows scrollbar to reflect current scroll */
        int viewport_h = rc.bottom - HEADER_H - FOOTER_H;
        SCROLLINFO si = { sizeof(si) };
        si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS | SIF_DISABLENOSCROLL;
        si.nMin  = 0;
        si.nMax  = g_content_h > 0 ? g_content_h - 1 : 0;
        si.nPage = viewport_h > 0 ? viewport_h : 1;
        si.nPos  = g_scroll_y;
        SetScrollInfo(hwnd, SB_VERT, &si, TRUE);

        EndPaint(hwnd, &ps);
        return 0;
      }

      case WM_VSCROLL: {
        SCROLLINFO si = { sizeof(si) };
        si.fMask = SIF_ALL;
        GetScrollInfo(hwnd, SB_VERT, &si);
        int prev = g_scroll_y;
        int max_scroll = si.nMax - (int)si.nPage + 1;
        if (max_scroll < 0) max_scroll = 0;
        switch (LOWORD(wParam)) {
          case SB_LINEUP:        g_scroll_y -= 20; break;
          case SB_LINEDOWN:      g_scroll_y += 20; break;
          case SB_PAGEUP:        g_scroll_y -= si.nPage; break;
          case SB_PAGEDOWN:      g_scroll_y += si.nPage; break;
          case SB_THUMBTRACK:
          case SB_THUMBPOSITION: g_scroll_y = si.nTrackPos; break;
          case SB_TOP:           g_scroll_y = 0; break;
          case SB_BOTTOM:        g_scroll_y = max_scroll; break;
        }
        if (g_scroll_y < 0) g_scroll_y = 0;
        if (g_scroll_y > max_scroll) g_scroll_y = max_scroll;
        if (g_scroll_y != prev) {
            SetScrollPos(hwnd, SB_VERT, g_scroll_y, TRUE);
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
      }

      case WM_MOUSEWHEEL: {
        int delta = GET_WHEEL_DELTA_WPARAM(wParam);
        int prev = g_scroll_y;
        g_scroll_y -= (delta / WHEEL_DELTA) * 60;
        RECT rc; GetClientRect(hwnd, &rc);
        int viewport_h = rc.bottom - HEADER_H - FOOTER_H;
        int max_scroll = g_content_h - viewport_h;
        if (max_scroll < 0) max_scroll = 0;
        if (g_scroll_y < 0) g_scroll_y = 0;
        if (g_scroll_y > max_scroll) g_scroll_y = max_scroll;
        if (g_scroll_y != prev) {
            SetScrollPos(hwnd, SB_VERT, g_scroll_y, TRUE);
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
      }

      case WM_LBUTTONDOWN: {
        int x = (short)LOWORD(lParam);
        int y = (short)HIWORD(lParam);
        int c, i, p;
        if (hittest(g_current_view, x, y, &c, &i, &p))
            handle_click(g_current_view, c, i, p);
        return 0;
      }

      case WM_MOUSEMOVE: {
        int x = (short)LOWORD(lParam);
        int y = (short)HIWORD(lParam);
        int c = -1, i = -1, p = -1;
        hittest(g_current_view, x, y, &c, &i, &p);
        if (c != g_hover_card || i != g_hover_item || p != g_hover_pill) {
            g_hover_card = c; g_hover_item = i; g_hover_pill = p;
            InvalidateRect(hwnd, NULL, FALSE);
            if (c >= 0) {
                TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
                TrackMouseEvent(&tme);
            }
        }
        return 0;
      }

      case WM_MOUSELEAVE:
        g_hover_card = -1; g_hover_item = -1; g_hover_pill = -1;
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;

      case WM_SIZE: {
        RECT rc;
        GetClientRect(hwnd, &rc);
        compute_layout(rc.right, rc.bottom, g_current_view);
        /* Window may have grown — clamp stale scroll offset */
        int viewport_h = rc.bottom - HEADER_H - FOOTER_H;
        int max_scroll = g_content_h - viewport_h;
        if (max_scroll < 0) max_scroll = 0;
        if (g_scroll_y > max_scroll) {
            g_scroll_y = max_scroll;
            compute_layout(rc.right, rc.bottom, g_current_view);
        }
        position_edits(g_current_view);
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
      }

      case WM_COMMAND: {
        WORD id    = LOWORD(wParam);
        WORD notif = HIWORD(wParam);
        /* Edit control notifications — write back to Conf (but NOT during load) */
        if (!g_loading && (notif == EN_CHANGE || notif == EN_KILLFOCUS)) {
            save_edit_values(g_current_view);
        }
        /* Browse button clicks - range covers card*100+item for up to MAX_CARDS */
        if (notif == BN_CLICKED &&
            id >= IDC_SETTINGS_BROWSE_BASE &&
            id < IDC_SETTINGS_BROWSE_BASE + MAX_CARDS * 100 + MAX_CARD_ITEMS) {
            handle_browse(g_current_view, id);
        }
        return 0;
      }

      case WM_CTLCOLOREDIT:
      case WM_CTLCOLORSTATIC:
      case WM_CTLCOLORBTN:
      case WM_CTLCOLORLISTBOX: {
        HDC hdc = (HDC)wParam;
        SetTextColor(hdc, RHINO_TEXT);
        SetBkMode(hdc, OPAQUE);
        BOOL isEdit = (msg == WM_CTLCOLOREDIT || msg == WM_CTLCOLORLISTBOX);
        SetBkColor(hdc, isEdit ? RHINO_BG_EDIT : RHINO_BG_DEEP);
        static HBRUSH br_edit = NULL, br_dlg = NULL;
        if (!br_edit) br_edit = CreateSolidBrush(RHINO_BG_EDIT);
        if (!br_dlg)  br_dlg  = CreateSolidBrush(RHINO_BG_DEEP);
        return (LRESULT)(isEdit ? br_edit : br_dlg);
      }

      case WM_ERASEBKGND:
        return 1;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

/* ============================================================
 *  CREATE / DESTROY EDIT CONTROLS
 * ============================================================ */

static void create_all_edits(HINSTANCE hinst)
{
    for (int vi = 0; vi < RHINO_SETTINGS_COUNT; vi++) {
        SettingsView *v = &g_views[vi];
        for (int c = 0; c < v->n_cards; c++) {
            SettingCard *card = &v->cards[c];
            for (int i = 0; i < card->n_items; i++) {
                SettingItem *si = &card->items[i];
                si->edit_hwnd = NULL;
                si->browse_hwnd = NULL;
                if (si->type != ST_INT && si->type != ST_STR &&
                    si->type != ST_FILE && si->type != ST_FONT)
                    continue;
                DWORD style = WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL;
                if (si->type == ST_INT) style |= ES_NUMBER;
                if (si->type == ST_FONT) style |= ES_READONLY;
                if (si->type == ST_STR && si->conf_key == CONF_proxy_password)
                    style |= ES_PASSWORD;
                si->edit_hwnd = CreateWindowExA(
                    0, "EDIT", "",
                    style,
                    0, 0, 100, INPUT_H,
                    g_hwnd,
                    (HMENU)(ULONG_PTR)edit_id(c, i),
                    hinst, NULL);
                if (si->edit_hwnd && g_font)
                    SendMessage(si->edit_hwnd, WM_SETFONT, (WPARAM)g_font, TRUE);

                /* Browse/Choose button for file pickers and font picker */
                if (si->type == ST_FILE || si->type == ST_FONT) {
                    const char *btn_text = (si->type == ST_FONT)
                                           ? "Choose..." : "...";
                    si->browse_hwnd = CreateWindowExA(
                        0, "BUTTON", btn_text,
                        WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON,
                        0, 0, BROWSE_BTN_W, INPUT_H,
                        g_hwnd,
                        (HMENU)(ULONG_PTR)browse_id(c, i),
                        hinst, NULL);
                    if (si->browse_hwnd && g_font)
                        SendMessage(si->browse_hwnd, WM_SETFONT,
                                    (WPARAM)g_font, TRUE);
                }
            }
        }
    }
}

static void destroy_all_edits(void)
{
    for (int vi = 0; vi < RHINO_SETTINGS_COUNT; vi++) {
        SettingsView *v = &g_views[vi];
        for (int c = 0; c < v->n_cards; c++) {
            SettingCard *card = &v->cards[c];
            for (int i = 0; i < card->n_items; i++) {
                SettingItem *si = &card->items[i];
                if (si->edit_hwnd && IsWindow(si->edit_hwnd))
                    DestroyWindow(si->edit_hwnd);
                si->edit_hwnd = NULL;
                if (si->browse_hwnd && IsWindow(si->browse_hwnd))
                    DestroyWindow(si->browse_hwnd);
                si->browse_hwnd = NULL;
            }
        }
    }
}

/* ============================================================
 *  PUBLIC API
 * ============================================================ */

static void register_class_once(HINSTANCE hinst)
{
    static BOOL done = FALSE;
    if (done) return;
    WNDCLASSEXA wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = SettingsWndProc;
    wc.hInstance     = hinst;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.lpszClassName = SETTINGS_CLASS;
    RegisterClassExA(&wc);
    done = TRUE;
}

void rhinossh_settings_create(HWND dialog, HFONT font, void *conf,
                              int x, int y, int w, int h)
{
    if (!dialog) return;
    g_dialog = dialog;
    g_font   = font;
    g_conf   = (Conf *)conf;
    g_current_view = -1;
    g_visible = FALSE;

    /* Initialize conf keys that PuTTY's settings.c load path never touches.
     * Without these, conf_get_int asserts (entry not found) on first read. */
    if (g_conf) {
        conf_set_int(g_conf, CONF_bcdelay, 0);
        conf_set_int(g_conf, CONF_initdelay, 0);
    }

    HINSTANCE hinst = (HINSTANCE)GetWindowLongPtr(dialog, GWLP_HINSTANCE);
    register_class_once(hinst);

    g_font_title = CreateFontW(-18, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                DEFAULT_PITCH | FF_DONTCARE,
                                L"Segoe UI Variable");
    g_font_small = CreateFontW(-11, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                DEFAULT_PITCH | FF_DONTCARE,
                                L"Segoe UI Variable");
    g_font_label = CreateFontW(-10, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                DEFAULT_PITCH | FF_DONTCARE,
                                L"Segoe UI Variable");

    g_hwnd = CreateWindowExA(
        WS_EX_COMPOSITED, SETTINGS_CLASS, "",
        WS_CHILD | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_VSCROLL,
        x, y, w, h, dialog, NULL, hinst, NULL);

    if (!g_hwnd) return;

    /* Dark scrollbar — same line the terminal window uses */
    SetWindowTheme(g_hwnd, L"DarkMode_Explorer", NULL);

    create_all_edits(hinst);

    /* Hide all edits initially — they'll be shown when a view is selected */
    for (int vi = 0; vi < RHINO_SETTINGS_COUNT; vi++)
        show_view_edits(vi, 0);
}

void rhinossh_settings_switch_view(int view)
{
    if (!g_hwnd || view < 0 || view >= RHINO_SETTINGS_COUNT) return;

    /* Save previous view's edit values */
    if (g_current_view >= 0 && g_current_view < RHINO_SETTINGS_COUNT) {
        save_edit_values(g_current_view);
        show_view_edits(g_current_view, 0);
    }

    g_current_view = view;
    g_hover_card = -1;
    g_hover_item = -1;
    g_hover_pill = -1;
    g_scroll_y = 0;   /* always start at top when switching to a new view */

    /* Compute layout for this view */
    RECT rc;
    GetClientRect(g_hwnd, &rc);
    compute_layout(rc.right, rc.bottom, view);

    /* Load values from Conf into edits */
    load_view_values(view);
    show_view_edits(view, 1);
    position_edits(view);

    ShowWindow(g_hwnd, SW_SHOW);
    g_visible = TRUE;
    InvalidateRect(g_hwnd, NULL, TRUE);

    /* Update dialog window title */
    if (g_dialog) {
        char title[128];
        snprintf(title, sizeof(title), "RhinoSSH 0.83 - %s",
                 g_views[view].title);
        SetWindowTextA(g_dialog, title);
    }
}

void rhinossh_settings_hide(void)
{
    if (!g_hwnd) return;
    if (g_current_view >= 0 && g_current_view < RHINO_SETTINGS_COUNT)
        save_edit_values(g_current_view);
    ShowWindow(g_hwnd, SW_HIDE);
    g_visible = FALSE;
}

BOOL rhinossh_settings_is_visible(void)
{
    return g_visible;
}

void rhinossh_settings_save_current_view(void)
{
    if (g_current_view >= 0 && g_current_view < RHINO_SETTINGS_COUNT)
        save_edit_values(g_current_view);
}

void rhinossh_settings_destroy(void)
{
    if (g_current_view >= 0 && g_current_view < RHINO_SETTINGS_COUNT)
        save_edit_values(g_current_view);
    destroy_all_edits();
    if (g_font_title) { DeleteObject(g_font_title); g_font_title = NULL; }
    if (g_font_small) { DeleteObject(g_font_small); g_font_small = NULL; }
    if (g_font_label) { DeleteObject(g_font_label); g_font_label = NULL; }
    g_hwnd = NULL; g_dialog = NULL; g_conf = NULL;
    g_current_view = -1; g_visible = FALSE;
}

HWND rhinossh_settings_get_hwnd(void)
{
    return g_hwnd;
}
