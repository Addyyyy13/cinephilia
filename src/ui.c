/*
 * ui.c — Cinephilia's terminal interface. See ui.h.
 *
 * Rendering: every screen is composed into a string builder and written in
 * one call. Lines are overwritten in place (cursor home, each line ends with
 * "erase to end of line") rather than clearing the screen first, which keeps
 * redraws free of flicker.
 */
#include "ui.h"

#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/time.h>
#include <termios.h>
#include <unistd.h>

#include "text.h"
#include "wordmark.h"

#define TAGLINE "WHAT IS THIS WORLD WITHOUT ART?"
#define ARCHIVE_LABEL "PERSONAL CINEMA ARCHIVE"
#define ENTER_PROMPT "PRESS ENTER TO ENTER THE ARCHIVE"
#define BRAND "CINEPHILIA"
#define LINE_MODE_WIDTH 76
#define LINE_MODE_MARGIN 2

#define G_MIDDOT "\xC2\xB7"
#define G_MARKER "\xE2\x80\xBA"
#define G_RULE "\xE2\x94\x80"
#define G_BLOCK "\xE2\x96\x88"
#define G_UPPER "\xE2\x96\x80"
#define G_LOWER "\xE2\x96\x84"
#define G_SHADE "\xE2\x96\x92"

/* ------------------------------------------------------------------ */
/* State and palette                                                  */
/* ------------------------------------------------------------------ */

typedef struct {
    unsigned char r, g, b;
    unsigned char x256; /* nearest xterm-256 colour (macOS Terminal has no truecolor) */
} Rgb;

static const Rgb PALETTE[UI_COLOR_COUNT] = {
    [UI_IVORY] = {0xEA, 0xE4, 0xD8, 254},
    [UI_STONE] = {0x96, 0x90, 0x87, 246},
    [UI_GREY] = {0x77, 0x73, 0x6D, 243},
    [UI_RED] = {0xC7, 0x6D, 0x61, 167},
    [UI_GOLD] = {0xB8, 0x9A, 0x63, 137},
    [UI_ASH] = {0x35, 0x31, 0x2D, 236},
    [UI_BRASS] = {0x5E, 0x50, 0x37, 94},
};
static const Rgb MIDNIGHT = {0x0A, 0x0A, 0x0A, 232};
static const Rgb CHARCOAL = {0x15, 0x15, 0x15, 233};
static const Rgb GRAPHITE = {0x1D, 0x1B, 0x19, 234};

static struct {
    bool interactive;
    bool color;
    bool truecolor;
    bool animate;
    bool ascii;
    bool active; /* terminal currently configured by us */
    bool raw;
    bool have_orig;
    bool eof;
    bool echo_input;
    struct termios orig;
    struct termios rawios;
    bool has_flash;
    UiTone flash_tone;
    char flash[512];
    char last_section[64];
    void (*hook)(UiEvent);
} U;

static volatile sig_atomic_t g_resized = 0;

static const char ENTER_SEQ[] = "\033[?1049h\033[?25l";
static const char RESTORE_SEQ[] = "\033[0m\033[?25h\033[?1049l";

static void fire(UiEvent e)
{
    if (U.hook)
        U.hook(e);
}

/* ------------------------------------------------------------------ */
/* Output, with the ASCII fallback applied on the way out             */
/* ------------------------------------------------------------------ */

static const struct {
    const char *utf8;
    char ascii;
} ASCII_MAP[] = {
    {"\xC2\xB7", '-'},     {"\xE2\x80\x94", '-'}, {"\xE2\x80\x93", '-'}, {"\xE2\x80\x9C", '"'},
    {"\xE2\x80\x9D", '"'}, {"\xE2\x80\x98", '\''}, {"\xE2\x80\x99", '\''}, {"\xE2\x80\xA6", '.'},
    {"\xE2\x80\xBA", '>'}, {"\xE2\x94\x80", '-'}, {"\xE2\x96\x88", '#'}, {"\xE2\x96\x80", '"'},
    {"\xE2\x96\x84", '_'}, {"\xE2\x96\x92", ':'}, {"\xE2\x96\x81", '.'}, {"\xE2\x96\x82", ':'},
    {"\xE2\x96\x83", '-'}, {"\xE2\x96\x85", '+'}, {"\xE2\x96\x86", '*'}, {"\xE2\x96\x87", '%'},
    {"\xE2\x96\x8F", '#'}, {"\xE2\x96\x8E", '#'}, {"\xE2\x96\x8D", '#'}, {"\xE2\x96\x8C", '#'},
    {"\xE2\x96\x8B", '#'}, {"\xE2\x96\x8A", '#'}, {"\xE2\x96\x89", '#'}, {"\xE2\x98\x85", '*'},
    {"\xE2\x9C\x93", '+'}, {"\xC3\x97", 'x'},     {"\xE2\x86\x91", '^'}, {"\xE2\x86\x93", 'v'},
};

static void emit(const char *s, size_t n)
{
    if (!U.ascii) {
        fwrite(s, 1, n, stdout);
        return;
    }
    size_t i = 0;
    while (i < n) {
        unsigned char c = (unsigned char)s[i];
        bool mapped = false;
        if (c == 0xC2 || c == 0xC3 || c == 0xE2) {
            for (size_t k = 0; k < sizeof ASCII_MAP / sizeof ASCII_MAP[0]; k++) {
                size_t len = strlen(ASCII_MAP[k].utf8);
                if (i + len <= n && memcmp(s + i, ASCII_MAP[k].utf8, len) == 0) {
                    fputc(ASCII_MAP[k].ascii, stdout);
                    i += len;
                    mapped = true;
                    break;
                }
            }
        }
        if (!mapped)
            fputc(s[i++], stdout);
    }
}

static void emit_sb(const Sb *sb)
{
    emit(sb_str(sb), sb->len);
    fflush(stdout);
}

static void out_printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void out_printf(const char *fmt, ...)
{
    char buf[2048];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (n < 0)
        return;
    emit(buf, (size_t)n < sizeof buf ? (size_t)n : sizeof buf - 1);
    fflush(stdout);
}

/* ------------------------------------------------------------------ */
/* Terminal setup and restoration                                     */
/* ------------------------------------------------------------------ */

static void raw_on(void)
{
    if (!U.active || U.raw)
        return;
    struct termios t = U.orig;
    t.c_lflag &= ~(tcflag_t)(ICANON | ECHO | IEXTEN); /* ISIG stays: Ctrl+C works */
    t.c_iflag &= ~(tcflag_t)(IXON | ICRNL);
    t.c_cc[VMIN] = 1;
    t.c_cc[VTIME] = 0;
    U.rawios = t;
    if (tcsetattr(STDIN_FILENO, TCSANOW, &t) == 0)
        U.raw = true;
}

static void raw_off(void)
{
    if (!U.raw)
        return;
    tcsetattr(STDIN_FILENO, TCSANOW, &U.orig);
    U.raw = false;
}

/* Async-signal-safe: write(2) and tcsetattr(3) only. */
static void restore_terminal_from_signal(void)
{
    if (U.active) {
        ssize_t n = write(STDOUT_FILENO, RESTORE_SEQ, sizeof RESTORE_SEQ - 1);
        (void)n;
    }
    if (U.have_orig)
        tcsetattr(STDIN_FILENO, TCSANOW, &U.orig);
}

/* Async-signal-safe cleanup supplied by the application (stops sounds). */
static void (*g_signal_cleanup)(void);

static void on_fatal_signal(int sig)
{
    if (g_signal_cleanup)
        g_signal_cleanup();
    restore_terminal_from_signal();
    signal(sig, SIG_DFL);
    raise(sig);
}

static void on_resize(int sig)
{
    (void)sig;
    g_resized = 1;
}

static void install_handler(int sig, void (*fn)(int), bool restart)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = fn;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = restart ? SA_RESTART : 0;
    sigaction(sig, &sa, NULL);
}

static void on_suspend(int sig)
{
    (void)sig;
    restore_terminal_from_signal();
    signal(SIGTSTP, SIG_DFL);
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGTSTP);
    sigprocmask(SIG_UNBLOCK, &set, NULL);
    raise(SIGTSTP);
    /* Resumed after SIGCONT. */
    install_handler(SIGTSTP, on_suspend, true);
    if (U.active) {
        ssize_t n = write(STDOUT_FILENO, ENTER_SEQ, sizeof ENTER_SEQ - 1);
        (void)n;
        if (U.raw)
            tcsetattr(STDIN_FILENO, TCSANOW, &U.rawios);
    }
    g_resized = 1;
}

static bool locale_is_utf8(void)
{
    const char *vars[] = {"LC_ALL", "LC_CTYPE", "LANG"};
    for (size_t i = 0; i < 3; i++) {
        const char *v = getenv(vars[i]);
        if (v && *v)
            return text_contains_ci(v, "UTF-8") || text_contains_ci(v, "utf8");
    }
    return false;
}

void ui_set_signal_cleanup(void (*fn)(void))
{
    g_signal_cleanup = fn;
}

void ui_init(const UiOptions *opts)
{
    memset(&U, 0, sizeof U);
    const char *term = getenv("TERM");
    bool dumb = !term || !*term || strcmp(term, "dumb") == 0;
    bool tty_in = isatty(STDIN_FILENO);
    bool tty_out = isatty(STDOUT_FILENO);
    const char *no_color = getenv("NO_COLOR");
    const char *no_anim = getenv("CINEPHILIA_NO_ANIMATION");
    const char *colorterm = getenv("COLORTERM");
    const char *ascii = getenv("CINEPHILIA_ASCII");

    U.interactive = tty_in && tty_out && !dumb;
    U.color = U.interactive && !(no_color && *no_color) && !(opts && opts->no_color);
    U.truecolor = colorterm && (strstr(colorterm, "truecolor") || strstr(colorterm, "24bit"));
    U.animate = U.interactive && !(opts && opts->no_animation) && !(no_anim && *no_anim);
    U.ascii = (ascii && *ascii) || !locale_is_utf8();
    U.echo_input = !tty_in;

    if (!U.interactive)
        return;
    if (tcgetattr(STDIN_FILENO, &U.orig) != 0) {
        U.interactive = U.color = U.animate = false;
        return;
    }
    U.have_orig = true;
    setvbuf(stdout, NULL, _IOFBF, 1 << 16);
    install_handler(SIGINT, on_fatal_signal, false);
    install_handler(SIGTERM, on_fatal_signal, false);
    install_handler(SIGHUP, on_fatal_signal, false);
    install_handler(SIGQUIT, on_fatal_signal, false);
    install_handler(SIGTSTP, on_suspend, true);
    install_handler(SIGWINCH, on_resize, true);
    atexit(ui_shutdown);

    U.active = true;
    fputs(ENTER_SEQ, stdout);
    fflush(stdout);
    raw_on();
}

void ui_shutdown(void)
{
    if (!U.active)
        return;
    raw_off();
    fputs(RESTORE_SEQ, stdout);
    fflush(stdout);
    U.active = false;
}

bool ui_interactive(void) { return U.interactive; }
bool ui_input_closed(void) { return U.eof; }
bool ui_ascii(void) { return U.ascii; }
bool ui_animation(void) { return U.animate; }
void ui_set_event_hook(void (*hook)(UiEvent)) { U.hook = hook; }

void ui_set_animation(bool on)
{
    U.animate = U.interactive && on;
}

/* ------------------------------------------------------------------ */
/* Geometry                                                           */
/* ------------------------------------------------------------------ */

int ui_cols(void)
{
    struct winsize ws;
    if (U.interactive && ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0)
        return ws.ws_col;
    return 80;
}

int ui_rows(void)
{
    struct winsize ws;
    if (U.interactive && ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_row > 0)
        return ws.ws_row;
    return 24;
}

int ui_content_width(void)
{
    if (!U.interactive)
        return LINE_MODE_WIDTH;
    int c = ui_cols();
    int w = c - 8;
    if (w > 92)
        w = 92;
    if (w < 24)
        w = c > 2 ? c - 2 : c;
    return w;
}

int ui_margin(void)
{
    if (!U.interactive)
        return LINE_MODE_MARGIN;
    int m = (ui_cols() - ui_content_width()) / 2;
    return m < 0 ? 0 : m;
}

/* ------------------------------------------------------------------ */
/* Styling                                                            */
/* ------------------------------------------------------------------ */

static void sb_rgb(Sb *sb, const Rgb *c, bool background)
{
    if (!U.color)
        return;
    if (U.truecolor)
        sb_printf(sb, "\033[%d;2;%u;%u;%um", background ? 48 : 38, c->r, c->g, c->b);
    else
        sb_printf(sb, "\033[%d;5;%um", background ? 48 : 38, c->x256);
}

void ui_sb_fg(Sb *sb, UiColor c) { sb_rgb(sb, &PALETTE[c], false); }
static void sb_bg(Sb *sb, const Rgb *c) { sb_rgb(sb, c, true); }

void ui_sb_bold(Sb *sb)
{
    if (U.color)
        sb_puts(sb, "\033[1m");
}

void ui_sb_italic(Sb *sb)
{
    if (U.color)
        sb_puts(sb, "\033[3m");
}

void ui_sb_plain(Sb *sb)
{
    if (!U.color)
        return;
    sb_puts(sb, "\033[22;23;24m");
    sb_bg(sb, &MIDNIGHT);
    ui_sb_fg(sb, UI_IVORY);
}

void ui_sb_margin(Sb *sb)
{
    sb_spaces(sb, ui_margin());
}

void ui_sb_tracked(Sb *sb, const char *text)
{
    char up[256];
    text_upper(up, sizeof up, text);
    const char *p = up;
    bool first = true;
    while (*p) {
        unsigned cp;
        int n = text_utf8_decode(p, &cp);
        if (n < 0)
            n = 1;
        if (!first)
            sb_putc(sb, ' ');
        sb_putn(sb, p, (size_t)n);
        first = false;
        p += n;
    }
}

static int tracked_width(const char *text)
{
    int w = text_width(text);
    return w > 0 ? w * 2 - 1 : 0;
}

/* 3x3 numerals from half blocks. */
static const char *const BIG_DIGITS[10][3] = {
    {G_BLOCK G_UPPER G_BLOCK, G_BLOCK " " G_BLOCK, G_BLOCK G_LOWER G_BLOCK},
    {G_UPPER G_BLOCK " ", " " G_BLOCK " ", G_LOWER G_BLOCK G_LOWER},
    {G_UPPER G_UPPER G_BLOCK, G_BLOCK G_UPPER G_UPPER, G_BLOCK G_LOWER G_LOWER},
    {G_UPPER G_UPPER G_BLOCK, " " G_UPPER G_BLOCK, G_LOWER G_LOWER G_BLOCK},
    {G_BLOCK " " G_BLOCK, G_UPPER G_UPPER G_BLOCK, "  " G_BLOCK},
    {G_BLOCK G_UPPER G_UPPER, G_UPPER G_UPPER G_BLOCK, G_LOWER G_LOWER G_BLOCK},
    {G_BLOCK G_UPPER G_UPPER, G_BLOCK G_UPPER G_BLOCK, G_BLOCK G_LOWER G_BLOCK},
    {G_UPPER G_UPPER G_BLOCK, "  " G_BLOCK, "  " G_BLOCK},
    {G_BLOCK G_UPPER G_BLOCK, G_BLOCK G_UPPER G_BLOCK, G_BLOCK G_LOWER G_BLOCK},
    {G_BLOCK G_UPPER G_BLOCK, G_UPPER G_UPPER G_BLOCK, G_LOWER G_LOWER G_BLOCK},
};

int ui_sb_bignum(Sb rows[3], const char *text)
{
    int width = 0;
    if (U.ascii) { /* block numerals degrade badly in ASCII: use plain bold text */
        sb_puts(&rows[1], text);
        width = text_width(text);
        sb_spaces(&rows[0], width);
        sb_spaces(&rows[2], width);
        return width;
    }
    for (const char *p = text; *p; p++) {
        if (p != text) {
            for (int r = 0; r < 3; r++)
                sb_putc(&rows[r], ' ');
            width++;
        }
        if (*p >= '0' && *p <= '9') {
            for (int r = 0; r < 3; r++)
                sb_puts(&rows[r], BIG_DIGITS[*p - '0'][r]);
            width += 3;
        } else if (*p == '.') {
            sb_puts(&rows[0], " ");
            sb_puts(&rows[1], " ");
            sb_puts(&rows[2], G_LOWER);
            width += 1;
        } else if (*p == '/') {
            sb_puts(&rows[0], "  " G_LOWER);
            sb_puts(&rows[1], " " G_LOWER G_UPPER);
            sb_puts(&rows[2], G_LOWER G_UPPER " ");
            width += 3;
        } else {
            for (int r = 0; r < 3; r++)
                sb_putc(&rows[r], ' ');
            width += 1;
        }
    }
    return width;
}

static void tone_style(UiTone tone, UiColor *color, const char **mark)
{
    switch (tone) {
    case UI_TONE_OK: *color = UI_GOLD; *mark = "\xE2\x9C\x93"; break;  /* ✓ */
    case UI_TONE_WARN: *color = UI_RED; *mark = "!"; break;
    case UI_TONE_ERROR: *color = UI_RED; *mark = "\xC3\x97"; break;    /* × */
    default: *color = UI_STONE; *mark = G_MIDDOT; break;
    }
}

/* ------------------------------------------------------------------ */
/* Keyboard input                                                     */
/* ------------------------------------------------------------------ */

typedef enum {
    K_NONE, K_UP, K_DOWN, K_LEFT, K_RIGHT, K_PGUP, K_PGDN, K_HOME, K_END,
    K_ENTER, K_BACK, K_CHAR, K_RESIZE, K_EOF
} KeyKind;

typedef struct {
    KeyKind kind;
    int ch;
} Key;

static bool input_ready(int ms)
{
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    struct timeval tv = {ms / 1000, (ms % 1000) * 1000};
    return select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv) > 0;
}

/* A byte, -1 on timeout or interruption, -2 at end of input. */
static int read_byte_timeout(int ms)
{
    if (!input_ready(ms))
        return -1;
    unsigned char c;
    ssize_t n = read(STDIN_FILENO, &c, 1);
    if (n == 1)
        return c;
    if (n < 0 && errno == EINTR)
        return -1;
    return -2;
}

static Key key(KeyKind kind)
{
    Key k = {kind, 0};
    return k;
}

static Key read_key(void)
{
    fflush(stdout);
    for (;;) {
        if (g_resized) {
            g_resized = 0;
            return key(K_RESIZE);
        }
        int c = read_byte_timeout(250);
        if (c == -1)
            continue;
        if (c == -2)
            return key(K_EOF);
        if (c == 27) {
            int b = read_byte_timeout(40);
            if (b < 0)
                return key(K_BACK);
            if (b != '[' && b != 'O')
                return key(K_NONE);
            int d = read_byte_timeout(40);
            switch (d) {
            case 'A': return key(K_UP);
            case 'B': return key(K_DOWN);
            case 'C': return key(K_RIGHT);
            case 'D': return key(K_LEFT);
            case 'H': return key(K_HOME);
            case 'F': return key(K_END);
            default: break;
            }
            if (d >= '0' && d <= '9') {
                int num = d - '0';
                int e;
                while ((e = read_byte_timeout(40)) >= '0' && e <= '9')
                    num = num * 10 + (e - '0');
                if (e == '~') {
                    switch (num) {
                    case 1: case 7: return key(K_HOME);
                    case 4: case 8: return key(K_END);
                    case 5: return key(K_PGUP);
                    case 6: return key(K_PGDN);
                    default: break;
                    }
                }
            }
            return key(K_NONE);
        }
        if (c == '\r' || c == '\n')
            return key(K_ENTER);
        if (c == 127 || c == 8)
            return key(K_BACK);
        Key k = {K_CHAR, c};
        return k;
    }
}

/* Waits up to ms; if a key arrives, swallows pending input and returns true. */
static bool wait_or_key(int ms)
{
    fflush(stdout);
    if (input_ready(ms)) {
        unsigned char junk[64];
        ssize_t n = read(STDIN_FILENO, junk, sizeof junk);
        if (n == 0)
            U.eof = true;
        return true;
    }
    return false;
}

/* ------------------------------------------------------------------ */
/* Line input                                                         */
/* ------------------------------------------------------------------ */

static bool read_line(char *buf, size_t cap, bool *truncated)
{
    *truncated = false;
    for (;;) {
        errno = 0;
        if (fgets(buf, (int)cap, stdin))
            break;
        if (ferror(stdin) && errno == EINTR) {
            clearerr(stdin);
            continue;
        }
        if (U.interactive)
            clearerr(stdin); /* Ctrl+D at a prompt cancels the form, not the app */
        return false;
    }
    size_t n = strlen(buf);
    if (n > 0 && buf[n - 1] == '\n') {
        buf[--n] = '\0';
    } else {
        int c;
        while ((c = getchar()) != EOF && c != '\n')
            *truncated = true;
    }
    if (n > 0 && buf[n - 1] == '\r')
        buf[--n] = '\0';
    if (U.echo_input)
        out_printf("%s\n", buf);
    return true;
}

/* ------------------------------------------------------------------ */
/* Frame pieces: header, footer, flash, transitions                   */
/* ------------------------------------------------------------------ */

static void frame_start(Sb *f)
{
    sb_puts(f, "\033[H");
    ui_sb_plain(f);
}

static void line_end(Sb *f)
{
    sb_puts(f, "\033[K\n");
}

static int header_rows(const char *section)
{
    return section ? 4 : 1;
}

/* Row 2: the brand label at left, the section designation at right.
 * Row 3: a hairline rule (omitted while a transition draws it). */
static void draw_header(Sb *f, const char *section, bool rule)
{
    line_end(f);
    if (!section)
        return;
    int cw = ui_content_width();
    char up[96];
    text_upper(up, sizeof up, section);
    int brand_w = tracked_width(BRAND);
    ui_sb_margin(f);
    ui_sb_fg(f, UI_GOLD);
    ui_sb_tracked(f, BRAND);
    int room = cw - brand_w - 4;
    if (room > 4) {
        char fitted[200];
        int w = text_fit(fitted, sizeof fitted, up, room);
        sb_spaces(f, cw - brand_w - w);
        ui_sb_fg(f, UI_STONE);
        sb_puts(f, fitted);
    }
    line_end(f);
    ui_sb_margin(f);
    if (rule) {
        ui_sb_fg(f, UI_ASH);
        sb_repeat(f, G_RULE, cw);
    }
    line_end(f);
    line_end(f);
}

/* Section changes are marked by the rule being drawn across, left to
 * right, in about a tenth of a second. A keypress finishes it at once and
 * stays queued for the screen. */
static bool section_is_new(const char *section)
{
    if (!section)
        return false;
    return strcmp(U.last_section, section) != 0;
}

static void finish_transition(const char *section, bool animate)
{
    if (!section)
        return;
    text_copy(U.last_section, sizeof U.last_section, section);
    if (!animate)
        return;
    int cw = ui_content_width();
    const int frames = 8;
    for (int i = 1; i <= frames; i++) {
        Sb f;
        sb_init(&f);
        sb_printf(&f, "\033[3;%dH", ui_margin() + 1);
        sb_bg(&f, &MIDNIGHT);
        ui_sb_fg(&f, UI_ASH);
        int w = i == frames ? cw : cw * i / frames;
        sb_repeat(&f, G_RULE, w);
        emit_sb(&f);
        sb_free(&f);
        if (i < frames && input_ready(14))
            i = frames - 1; /* draw the full rule next and stop */
    }
}

static void draw_flash(Sb *f, int row)
{
    if (!U.has_flash)
        return;
    UiColor color;
    const char *mark;
    tone_style(U.flash_tone, &color, &mark);
    sb_printf(f, "\033[%d;1H", row);
    ui_sb_margin(f);
    ui_sb_fg(f, color);
    sb_puts(f, mark);
    sb_puts(f, "  ");
    ui_sb_fg(f, U.flash_tone == UI_TONE_INFO ? UI_STONE : UI_IVORY);
    sb_fit(f, U.flash, ui_content_width() - 3, false);
    sb_puts(f, "\033[K");
}

/* The bottom row is a charcoal band carrying keyboard hints. */
static void draw_footer(Sb *f, const char *hints, const char *right)
{
    int cw = ui_content_width();
    int rw = right ? text_width(right) : 0;
    sb_printf(f, "\033[%d;1H", ui_rows());
    sb_bg(f, &CHARCOAL);
    ui_sb_margin(f);
    ui_sb_fg(f, UI_GREY);
    sb_fit(f, hints, cw - (rw ? rw + 2 : 0), true);
    if (right) {
        sb_spaces(f, 2);
        ui_sb_fg(f, UI_STONE);
        sb_puts(f, right);
    }
    sb_puts(f, "\033[K");
    sb_bg(f, &MIDNIGHT);
}

static const char *scroll_hint(int top, int height, int total)
{
    if (top > 0 && top + height < total)
        return "\xE2\x86\x91\xE2\x86\x93 MORE";
    if (top > 0)
        return "\xE2\x86\x91 MORE";
    if (top + height < total)
        return "\xE2\x86\x93 MORE";
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Menu items                                                         */
/* ------------------------------------------------------------------ */

static bool selectable(const char *item)
{
    return item[0] != '\0' && item[0] != '\f';
}

static int next_selectable(const UiMenu *m, int from, int dir)
{
    for (int i = from; i >= 0 && i < m->count; i += dir)
        if (selectable(m->items[i]))
            return i;
    return -1;
}

static int item_lines(const char *item)
{
    int n = 1;
    for (const char *p = item; *p; p++)
        if (*p == '\n')
            n++;
    return n;
}

/* Copies line `idx` of an item (without the '\n') into out. */
static void item_line(const char *item, int idx, char *out, size_t cap)
{
    const char *p = item;
    for (int i = 0; i < idx && p; i++) {
        p = strchr(p, '\n');
        if (p)
            p++;
    }
    if (!p) {
        out[0] = '\0';
        return;
    }
    const char *end = strchr(p, '\n');
    size_t n = end ? (size_t)(end - p) : strlen(p);
    if (n >= cap)
        n = cap - 1;
    memcpy(out, p, n);
    out[n] = '\0';
}

/* Splits the first line into label and detail (after a TAB). */
static void split_label(const char *line, char *label, size_t cap, const char **detail)
{
    const char *tab = strchr(line, '\t');
    size_t n = tab ? (size_t)(tab - line) : strlen(line);
    if (n >= cap)
        n = cap - 1;
    memcpy(label, line, n);
    label[n] = '\0';
    *detail = tab ? tab + 1 : NULL;
}

/* Length of a leading index such as "01  " or "124  " (digits followed by
 * two spaces), or 0 when the label has none. */
static int index_len(const char *label)
{
    int d = 0;
    while (d < 4 && label[d] >= '0' && label[d] <= '9')
        d++;
    if (d >= 2 && label[d] == ' ' && label[d + 1] == ' ')
        return d + 2;
    return 0;
}

/* Writes s fitted to width, underlining the first case-insensitive match
 * of `needle` in gold. */
static void sb_fit_highlight(Sb *f, const char *s, int width, bool pad, const char *needle,
                             UiColor base)
{
    char tmp[1700];
    int w = text_fit(tmp, sizeof tmp, s, width);
    size_t m = needle ? strlen(needle) : 0;
    const char *hit = NULL;
    for (const char *p = tmp; m && *p && !hit; p++)
        if (text_ncasecmp(p, needle, m) == 0)
            hit = p;
    ui_sb_fg(f, base);
    if (!hit) {
        sb_puts(f, tmp);
    } else {
        sb_putn(f, tmp, (size_t)(hit - tmp));
        ui_sb_fg(f, UI_GOLD);
        if (U.color)
            sb_puts(f, "\033[4m");
        sb_putn(f, hit, m);
        if (U.color)
            sb_puts(f, "\033[24m");
        ui_sb_fg(f, base);
        sb_puts(f, hit + m);
    }
    if (pad && w < width)
        sb_spaces(f, width - w);
}

typedef struct {
    int width;  /* content width */
    int left_w; /* label column width when details are left-aligned */
} ItemLayout;

static ItemLayout item_layout(const UiMenu *m)
{
    ItemLayout L = {ui_content_width(), 0};
    char line[512];
    char label[512];
    const char *detail;
    for (int i = 0; i < m->count; i++) {
        if (!selectable(m->items[i]))
            continue;
        item_line(m->items[i], 0, line, sizeof line);
        split_label(line, label, sizeof label, &detail);
        if (m->detail_right && index_len(label))
            continue; /* catalogue rows align their detail to the right */
        if (detail) {
            int w = text_width(label);
            if (w > L.left_w)
                L.left_w = w;
        }
    }
    if (L.left_w > L.width * 55 / 100)
        L.left_w = L.width * 55 / 100;
    return L;
}

static void render_item_line(Sb *f, const UiMenu *m, int idx, int line_no, bool sel,
                             const ItemLayout *L)
{
    const char *item = m->items[idx];
    char line[1800];
    item_line(item, line_no, line, sizeof line);
    int width = L->width;

    ui_sb_margin(f);
    if (item[0] == '\f') { /* heading */
        sb_spaces(f, 2);
        ui_sb_fg(f, UI_GREY);
        sb_fit(f, line + 1, width - 2, false);
        return;
    }
    /* The selection band covers the item's text lines, not its spacing. */
    if (sel && (line_no == 0 || line[0] != '\0'))
        sb_bg(f, &GRAPHITE);
    else
        sel = sel && line_no == 0;

    if (line_no == 0) {
        char label[1800];
        const char *detail;
        split_label(line, label, sizeof label, &detail);
        if (sel) {
            ui_sb_fg(f, UI_RED);
            sb_puts(f, G_MARKER " ");
        } else {
            sb_puts(f, "  ");
        }
        int used = 2;
        const char *text = label;
        int il = index_len(label);
        if (il) {
            ui_sb_fg(f, sel ? UI_RED : UI_GREY);
            sb_putn(f, label, (size_t)il);
            used += il;
            text = label + il;
        }
        UiColor base = sel ? UI_IVORY : UI_STONE;
        if (sel)
            ui_sb_bold(f);
        int avail = width - used - 1;
        if (detail && m->detail_right && il) {
            int dw = text_width(detail);
            if (dw > avail / 2)
                dw = avail / 2;
            int lw = avail - dw - 2;
            sb_fit_highlight(f, text, lw, true, m->highlight, base);
            if (U.color)
                sb_puts(f, "\033[22m");
            sb_spaces(f, 2);
            ui_sb_fg(f, sel ? UI_GOLD : UI_GREY);
            int w = sb_fit(f, detail, dw, false);
            used += lw + 2 + w;
        } else if (detail && L->left_w > 0) {
            int lw = L->left_w - (used - 2);
            if (lw > avail)
                lw = avail;
            if (lw < 4)
                lw = 4;
            sb_fit_highlight(f, text, lw, true, m->highlight, base);
            if (U.color)
                sb_puts(f, "\033[22m");
            used += lw;
            int rest = width - used - 4;
            if (rest > 0) {
                sb_spaces(f, 3);
                ui_sb_fg(f, sel ? UI_GOLD : UI_GREY);
                used += 3 + sb_fit(f, detail, rest, false);
            }
        } else {
            used += text_width(text) < avail ? text_width(text) : avail;
            sb_fit_highlight(f, text, avail, false, m->highlight, base);
        }
        if (sel && used < width)
            sb_spaces(f, width - used);
    } else {
        /* Secondary lines sit under the label text. */
        char label0[64];
        item_line(item, 0, label0, sizeof label0);
        int indent = 2 + index_len(label0);
        sb_spaces(f, indent);
        const char *text = line;
        bool italic = text[0] == '\x0e';
        if (italic) {
            text++;
            ui_sb_italic(f);
        }
        int w = 0;
        if (*text)
            w = text_width(text) < width - indent ? text_width(text) : width - indent;
        sb_fit_highlight(f, text, width - indent, false, m->highlight,
                         italic ? UI_STONE : (sel ? UI_STONE : UI_GREY));
        if (sel && indent + w < width)
            sb_spaces(f, width - indent - w);
    }
    ui_sb_plain(f);
}

/* Line mode: the same items, numbered, answered by number. */
static int menu_line_mode(const UiMenu *m)
{
    if (m->section) {
        char up[96];
        text_upper(up, sizeof up, m->section);
        out_printf("\n%*sCINEPHILIA  " G_MIDDOT "  %s\n", LINE_MODE_MARGIN, "", up);
    } else {
        out_printf("\n");
    }
    Sb rule;
    sb_init(&rule);
    sb_spaces(&rule, LINE_MODE_MARGIN);
    sb_repeat(&rule, "-", LINE_MODE_WIDTH);
    sb_putc(&rule, '\n');
    emit_sb(&rule);
    sb_free(&rule);

    if (m->preamble)
        for (size_t i = 0; i < m->preamble->count; i++)
            out_printf("%s\n", m->preamble->items[i]);

    int *map = malloc((size_t)(m->count + 1) * sizeof *map);
    if (!map) {
        U.eof = true;
        return UI_BACK;
    }
    char line[1800];
    char label[1800];
    const char *detail;
    int left_w = 0;
    for (int i = 0; i < m->count; i++) {
        if (!selectable(m->items[i]))
            continue;
        item_line(m->items[i], 0, line, sizeof line);
        split_label(line, label, sizeof label, &detail);
        const char *t = label + index_len(label);
        if (detail && text_width(t) > left_w)
            left_w = text_width(t);
    }
    if (left_w > 46)
        left_w = 46;

    int number = 0;
    for (int i = 0; i < m->count; i++) {
        const char *item = m->items[i];
        if (item[0] == '\0') {
            out_printf("\n");
            continue;
        }
        Sb sb;
        sb_init(&sb);
        sb_spaces(&sb, LINE_MODE_MARGIN);
        if (item[0] == '\f') {
            item_line(item, 0, line, sizeof line);
            sb_spaces(&sb, 4);
            sb_fit(&sb, line + 1, LINE_MODE_WIDTH - 4, false);
            sb_putc(&sb, '\n');
        } else {
            map[++number] = i;
            item_line(item, 0, line, sizeof line);
            split_label(line, label, sizeof label, &detail);
            const char *t = label + index_len(label);
            sb_printf(&sb, "%2d) ", number);
            if (detail && *detail) {
                sb_fit(&sb, t, left_w, true);
                sb_spaces(&sb, 3);
                sb_fit(&sb, detail, LINE_MODE_WIDTH - 4 - left_w - 3, false);
            } else {
                sb_fit(&sb, t, LINE_MODE_WIDTH - 4, false);
            }
            sb_putc(&sb, '\n');
            int lines = item_lines(item);
            for (int k = 1; k < lines; k++) {
                item_line(item, k, line, sizeof line);
                const char *x = line[0] == '\x0e' ? line + 1 : line;
                if (!*x) {
                    sb_putc(&sb, '\n');
                    continue;
                }
                sb_spaces(&sb, LINE_MODE_MARGIN + 4);
                sb_fit(&sb, x, LINE_MODE_WIDTH - 4, false);
                sb_putc(&sb, '\n');
            }
        }
        emit_sb(&sb);
        sb_free(&sb);
    }
    out_printf("%*s 0) %s\n", LINE_MODE_MARGIN, "", m->back_label ? m->back_label : "Back");
    if (m->postamble)
        for (size_t i = 0; i < m->postamble->count; i++)
            out_printf("%s\n", m->postamble->items[i]);

    int result = UI_BACK;
    for (;;) {
        out_printf("%*s> ", LINE_MODE_MARGIN, "");
        char in[128];
        bool truncated;
        if (!read_line(in, sizeof in, &truncated)) {
            U.eof = true;
            break;
        }
        text_trim(in);
        long n;
        if (strcmp(in, "0") == 0 || text_casecmp(in, "q") == 0 || text_casecmp(in, "b") == 0)
            break;
        if (!truncated && number > 0 && text_parse_int(in, 1, number, &n)) {
            result = map[n];
            break;
        }
        fire(UI_EVENT_ERROR);
        if (number > 0)
            out_printf("%*sThat isn't one of the options. Choose 1-%d, or 0 to go back.\n",
                       LINE_MODE_MARGIN, "", number);
        else
            out_printf("%*sThere's nothing to choose here. Enter 0 to go back.\n", LINE_MODE_MARGIN, "");
    }
    free(map);
    return result;
}

typedef struct {
    short kind; /* 0 preamble, 1 item, 2 postamble */
    short line;
    int index;
} Row;

int ui_menu(const UiMenu *m)
{
    if (U.eof)
        return UI_BACK;
    if (!U.interactive)
        return menu_line_mode(m);

    int sel = m->selected;
    if (sel >= m->count)
        sel = m->count - 1;
    if (sel < 0)
        sel = 0;
    int s = next_selectable(m, sel, +1);
    if (s < 0)
        s = next_selectable(m, sel, -1);
    sel = s;

    /* Flatten preamble, item lines and postamble into rows. */
    int pre_n = m->preamble ? (int)m->preamble->count : 0;
    int post_n = m->postamble ? (int)m->postamble->count : 0;
    int total = pre_n + post_n;
    for (int i = 0; i < m->count; i++)
        total += item_lines(m->items[i]);
    Row *rows = malloc((size_t)(total > 0 ? total : 1) * sizeof *rows);
    int *first_row = malloc((size_t)(m->count > 0 ? m->count : 1) * sizeof *first_row);
    if (!rows || !first_row) {
        free(rows);
        free(first_row);
        U.eof = true;
        return UI_BACK;
    }
    int r = 0;
    for (int i = 0; i < pre_n; i++)
        rows[r++] = (Row){0, 0, i};
    for (int i = 0; i < m->count; i++) {
        first_row[i] = r;
        int lines = item_lines(m->items[i]);
        for (int k = 0; k < lines; k++)
            rows[r++] = (Row){1, (short)k, i};
    }
    for (int i = 0; i < post_n; i++)
        rows[r++] = (Row){2, 0, i};

    char back[32];
    text_copy(back, sizeof back, m->back_label ? m->back_label : "Back");
    char back_up[32];
    text_upper(back_up, sizeof back_up, back);
    char hint[128];
    snprintf(hint, sizeof hint, "\xE2\x86\x91 \xE2\x86\x93  MOVE     ENTER  OPEN     %sQ  %s",
             m->number_keys ? "1-9  JUMP     " : "", back_up);

    bool transition = section_is_new(m->section) && U.animate;
    int top = 0;
    bool follow = true;
    int result = UI_BACK;

    for (;;) {
        int screen_rows = ui_rows();
        int height = screen_rows - header_rows(m->section) - 2;
        if (height < 1)
            height = 1;

        if (sel >= 0) {
            int a = first_row[sel];
            int b = a + item_lines(m->items[sel]) - 1;
            if (follow && b < height)
                top = 0; /* keep the preamble in view whenever it fits */
            if (b >= top + height)
                top = b - height + 1;
            if (a < top)
                top = a;
        }
        if (top > total - height)
            top = total - height;
        if (top < 0)
            top = 0;
        follow = true;

        ItemLayout L = item_layout(m);
        Sb f;
        sb_init(&f);
        frame_start(&f);
        draw_header(&f, m->section, !transition);
        for (int v = top; v < total && v < top + height; v++) {
            const Row *row = &rows[v];
            if (row->kind == 0)
                sb_puts(&f, m->preamble->items[row->index]);
            else if (row->kind == 2)
                sb_puts(&f, m->postamble->items[row->index]);
            else if (m->items[row->index][0])
                render_item_line(&f, m, row->index, row->line, row->index == sel, &L);
            ui_sb_plain(&f);
            line_end(&f);
        }
        sb_puts(&f, "\033[J");
        draw_flash(&f, screen_rows - 1);
        draw_footer(&f, hint, scroll_hint(top, height, total));
        emit_sb(&f);
        sb_free(&f);
        if (transition) {
            finish_transition(m->section, true);
            transition = false;
        } else {
            finish_transition(m->section, false);
        }

        Key k = read_key();
        if (k.kind != K_RESIZE && k.kind != K_NONE)
            U.has_flash = false;

        int t;
        int before = sel;
        bool done = false;
        switch (k.kind) {
        case K_UP:
            t = sel >= 0 ? next_selectable(m, sel - 1, -1) : -1;
            if (t >= 0) {
                sel = t;
            } else if (top > 0) { /* first option: scroll the preamble instead */
                top--;
                follow = false;
            }
            break;
        case K_DOWN:
            t = sel >= 0 ? next_selectable(m, sel + 1, +1) : -1;
            if (t >= 0)
                sel = t;
            else if (top + height < total) { /* last option: reveal the postamble */
                top++;
                follow = false;
            }
            break;
        case K_PGUP:
        case K_PGDN:
            if (sel >= 0) {
                int dir = k.kind == K_PGUP ? -1 : 1;
                int steps = height / 3 > 1 ? height / 3 : 1;
                t = sel;
                for (int i = 0; i < steps; i++) {
                    int n = next_selectable(m, t + dir, dir);
                    if (n < 0)
                        break;
                    t = n;
                }
                sel = t;
            }
            break;
        case K_HOME:
            t = next_selectable(m, 0, +1);
            if (t >= 0)
                sel = t;
            break;
        case K_END:
            t = next_selectable(m, m->count - 1, -1);
            if (t >= 0)
                sel = t;
            break;
        case K_ENTER:
        case K_RIGHT:
            if (sel >= 0) {
                result = sel;
                done = true;
            }
            break;
        case K_BACK:
        case K_LEFT:
            done = true;
            break;
        case K_EOF:
            U.eof = true;
            done = true;
            break;
        case K_CHAR:
            if (k.ch == 'q' || k.ch == 'Q') {
                done = true;
            } else if (k.ch == 'k' || k.ch == 'j') {
                t = sel >= 0 ? next_selectable(m, sel + (k.ch == 'k' ? -1 : 1), k.ch == 'k' ? -1 : 1) : -1;
                if (t >= 0)
                    sel = t;
            } else if (m->number_keys && k.ch >= '1' && k.ch <= '9') {
                int want = k.ch - '0';
                for (int i = 0, seen = 0; i < m->count; i++)
                    if (selectable(m->items[i]) && ++seen == want) {
                        result = i;
                        done = true;
                        break;
                    }
            }
            break;
        default:
            break;
        }
        if (sel != before && !done)
            fire(UI_EVENT_MOVE);
        if (done)
            break;
    }
    free(rows);
    free(first_row);
    return result;
}

/* ------------------------------------------------------------------ */
/* Pages                                                              */
/* ------------------------------------------------------------------ */

void ui_page(const char *section, const StrList *lines)
{
    if (U.eof)
        return;
    if (!U.interactive) {
        if (section) {
            char up[96];
            text_upper(up, sizeof up, section);
            out_printf("\n%*sCINEPHILIA  " G_MIDDOT "  %s\n", LINE_MODE_MARGIN, "", up);
        }
        for (size_t i = 0; i < lines->count; i++)
            out_printf("%s\n", lines->items[i]);
        return;
    }
    bool transition = section_is_new(section) && U.animate;
    int top = 0;
    int total = (int)lines->count;
    for (;;) {
        int screen_rows = ui_rows();
        int height = screen_rows - header_rows(section) - 2;
        if (height < 1)
            height = 1;
        if (top > total - height)
            top = total - height;
        if (top < 0)
            top = 0;

        Sb f;
        sb_init(&f);
        frame_start(&f);
        draw_header(&f, section, !transition);
        for (int v = top; v < total && v < top + height; v++) {
            sb_puts(&f, lines->items[v]);
            ui_sb_plain(&f);
            line_end(&f);
        }
        sb_puts(&f, "\033[J");
        draw_flash(&f, screen_rows - 1);
        draw_footer(&f, total > height ? "\xE2\x86\x91 \xE2\x86\x93  SCROLL     Q  BACK" : "ENTER OR Q  BACK",
                    scroll_hint(top, height, total));
        emit_sb(&f);
        sb_free(&f);
        finish_transition(section, transition);
        transition = false;

        Key k = read_key();
        if (k.kind != K_RESIZE && k.kind != K_NONE)
            U.has_flash = false;
        switch (k.kind) {
        case K_UP: top--; break;
        case K_DOWN: top++; break;
        case K_PGUP: top -= height - 1; break;
        case K_PGDN: top += height - 1; break;
        case K_HOME: top = 0; break;
        case K_END: top = total; break;
        case K_ENTER: case K_BACK: case K_LEFT: return;
        case K_EOF: U.eof = true; return;
        case K_CHAR:
            if (k.ch == 'q' || k.ch == 'Q')
                return;
            if (k.ch == 'j')
                top++;
            if (k.ch == 'k')
                top--;
            if (k.ch == ' ')
                top += height - 1;
            break;
        default: break;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Forms and prompts                                                  */
/* ------------------------------------------------------------------ */

void ui_form_begin(const char *section, const char *heading, const char *instructions)
{
    if (U.eof)
        return;
    if (!U.interactive) {
        if (section) {
            char up[96];
            text_upper(up, sizeof up, section);
            out_printf("\n%*sCINEPHILIA  " G_MIDDOT "  %s\n", LINE_MODE_MARGIN, "", up);
        }
        if (heading)
            out_printf("%*s%s\n", LINE_MODE_MARGIN, "", heading);
        if (instructions)
            out_printf("%*s%s\n", LINE_MODE_MARGIN, "", instructions);
        return;
    }
    bool transition = section_is_new(section) && U.animate;
    int cw = ui_content_width();
    Sb f;
    sb_init(&f);
    frame_start(&f);
    sb_puts(&f, "\033[2J\033[H");
    draw_header(&f, section, !transition);
    if (heading) {
        char up[512];
        text_upper(up, sizeof up, heading);
        ui_sb_margin(&f);
        ui_sb_fg(&f, UI_IVORY);
        ui_sb_bold(&f);
        sb_fit(&f, up, cw, false);
        ui_sb_plain(&f);
        line_end(&f);
    }
    if (instructions) {
        char line[512];
        const char *p = instructions;
        size_t n;
        while ((n = text_wrap_next(p, cw, line, sizeof line)) > 0) {
            ui_sb_margin(&f);
            ui_sb_fg(&f, UI_GREY);
            ui_sb_italic(&f);
            sb_puts(&f, line);
            ui_sb_plain(&f);
            line_end(&f);
            p += n;
        }
    }
    ui_sb_margin(&f);
    ui_sb_fg(&f, UI_ASH);
    sb_repeat(&f, G_RULE, 12);
    ui_sb_plain(&f);
    line_end(&f);
    sb_puts(&f, "\033[J");
    emit_sb(&f);
    sb_free(&f);
    finish_transition(section, transition);
    /* Leave the cursor below the heading for the prompts that follow. */
    Sb pos;
    sb_init(&pos);
    int row = header_rows(section) + 1 + (heading ? 1 : 0) + 1;
    if (instructions) {
        char line[512];
        const char *p = instructions;
        size_t n;
        while ((n = text_wrap_next(p, cw, line, sizeof line)) > 0) {
            row++;
            p += n;
        }
    }
    sb_printf(&pos, "\033[%d;1H", row);
    emit_sb(&pos);
    sb_free(&pos);
}

PromptResult ui_prompt(const char *label, const char *hint, const char *current,
                       char *out, size_t cap)
{
    if (cap > 0)
        out[0] = '\0';
    if (U.eof)
        return PROMPT_CANCEL;

    Sb s;
    sb_init(&s);
    sb_putc(&s, '\n');
    ui_sb_margin(&s);
    ui_sb_fg(&s, UI_STONE);
    char up[128];
    text_upper(up, sizeof up, label);
    sb_puts(&s, up);
    if (current && *current) {
        ui_sb_fg(&s, UI_GREY);
        sb_puts(&s, "   now: ");
        ui_sb_fg(&s, UI_IVORY);
        sb_fit(&s, current, 40, false);
    }
    sb_putc(&s, '\n');
    if (hint && *hint) {
        ui_sb_margin(&s);
        ui_sb_fg(&s, UI_GREY);
        ui_sb_italic(&s);
        sb_puts(&s, hint);
        ui_sb_plain(&s);
        sb_putc(&s, '\n');
    }
    ui_sb_margin(&s);
    ui_sb_fg(&s, UI_RED);
    sb_puts(&s, U.interactive ? G_MARKER " " : "> ");
    ui_sb_fg(&s, UI_IVORY);
    if (U.active) {
        raw_off();
        sb_puts(&s, "\033[?25h");
    }
    emit_sb(&s);
    sb_free(&s);

    char line[4096];
    bool truncated = false;
    bool got = read_line(line, sizeof line, &truncated);
    if (U.active) {
        fputs("\033[?25l", stdout);
        fflush(stdout);
        raw_on();
    }
    if (!got) {
        if (!U.interactive)
            U.eof = true;
        return PROMPT_CANCEL;
    }
    text_sanitize(line);
    text_trim(line);
    if (line[0] == '\0')
        return PROMPT_BLANK;
    size_t n = text_copy(out, cap, line);
    if (truncated || n < strlen(line))
        ui_say(UI_TONE_WARN, "That was long, so it was shortened to %d characters.", text_width(out));
    return PROMPT_OK;
}

PromptResult ui_prompt_int(const char *label, const char *hint, const char *current,
                           int min, int max, int *out)
{
    char buf[64];
    for (;;) {
        PromptResult r = ui_prompt(label, hint, current, buf, sizeof buf);
        if (r != PROMPT_OK)
            return r;
        if (strcmp(buf, "-") == 0)
            return PROMPT_CLEAR;
        long v;
        if (text_parse_int(buf, min, max, &v)) {
            *out = (int)v;
            return PROMPT_OK;
        }
        ui_say(UI_TONE_ERROR, "Please enter a whole number from %d to %d.", min, max);
    }
}

PromptResult ui_prompt_date(const char *label, const char *hint, const char *current,
                            const char *max_date, char out[11])
{
    char buf[64];
    for (;;) {
        PromptResult r = ui_prompt(label, hint, current, buf, sizeof buf);
        if (r != PROMPT_OK)
            return r;
        if (strcmp(buf, "-") == 0)
            return PROMPT_CLEAR;
        if (text_casecmp(buf, "today") == 0 && max_date) {
            text_copy(out, DATE_LEN, max_date);
            return PROMPT_OK;
        }
        if (!date_valid(buf)) {
            ui_say(UI_TONE_ERROR, "Please use the form YYYY-MM-DD, for example 2024-03-17.");
            continue;
        }
        if (max_date && strcmp(buf, max_date) > 0) {
            ui_say(UI_TONE_ERROR, "That date hasn't happened yet.");
            continue;
        }
        text_copy(out, DATE_LEN, buf);
        return PROMPT_OK;
    }
}

bool ui_confirm(const char *question, bool default_yes)
{
    if (U.eof)
        return false;
    Sb s;
    sb_init(&s);
    sb_putc(&s, '\n');
    ui_sb_margin(&s);
    ui_sb_fg(&s, UI_IVORY);
    sb_puts(&s, question);
    ui_sb_fg(&s, UI_GREY);
    sb_puts(&s, default_yes ? "   Y / n  " : "   y / N  ");
    emit_sb(&s);
    sb_free(&s);

    bool answer = default_yes;
    if (U.interactive) {
        for (;;) {
            Key k = read_key();
            if (k.kind == K_EOF) {
                U.eof = true;
                answer = false;
                break;
            }
            if (k.kind == K_ENTER)
                break;
            if (k.kind == K_BACK) {
                answer = false;
                break;
            }
            if (k.kind == K_CHAR && (k.ch == 'y' || k.ch == 'Y')) {
                answer = true;
                break;
            }
            if (k.kind == K_CHAR && (k.ch == 'n' || k.ch == 'N')) {
                answer = false;
                break;
            }
        }
        Sb a;
        sb_init(&a);
        ui_sb_fg(&a, answer ? UI_GOLD : UI_STONE);
        sb_puts(&a, answer ? "yes\n" : "no\n");
        ui_sb_plain(&a);
        emit_sb(&a);
        sb_free(&a);
        return answer;
    }
    char line[64];
    bool truncated;
    if (!read_line(line, sizeof line, &truncated)) {
        U.eof = true;
        return false;
    }
    text_trim(line);
    if (line[0] == '\0')
        return default_yes;
    return line[0] == 'y' || line[0] == 'Y';
}

static void say_line(UiTone tone, const char *msg)
{
    UiColor color;
    const char *mark;
    tone_style(tone, &color, &mark);
    Sb s;
    sb_init(&s);
    ui_sb_margin(&s);
    ui_sb_fg(&s, color);
    sb_puts(&s, mark);
    sb_puts(&s, "  ");
    ui_sb_fg(&s, tone == UI_TONE_INFO ? UI_STONE : UI_IVORY);
    sb_puts(&s, msg);
    ui_sb_plain(&s);
    sb_putc(&s, '\n');
    emit_sb(&s);
    sb_free(&s);
}

void ui_say(UiTone tone, const char *fmt, ...)
{
    char msg[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    text_sanitize(msg);
    if (tone == UI_TONE_ERROR)
        fire(UI_EVENT_ERROR);
    say_line(tone, msg);
}

void ui_flash(UiTone tone, const char *fmt, ...)
{
    char msg[sizeof U.flash];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    text_sanitize(msg);
    if (tone == UI_TONE_ERROR)
        fire(UI_EVENT_ERROR);
    if (!U.interactive) {
        say_line(tone, msg);
        return;
    }
    text_copy(U.flash, sizeof U.flash, msg);
    U.flash_tone = tone;
    U.has_flash = true;
}

/* ------------------------------------------------------------------ */
/* The opening sequence                                               */
/* ------------------------------------------------------------------ */

typedef struct {
    bool full;    /* block-letter wordmark; else the tracked word */
    bool strips;  /* room for the film-strip motifs */
    int gap;
    int wm_width; /* including the drop shadow */
    int wm_col;
    int label_row, wm_row, divider_row, tag_row, prompt_row, count_row;
} IntroLayout;

static IntroLayout intro_layout(void)
{
    IntroLayout L;
    memset(&L, 0, sizeof L);
    int cols = ui_cols();
    int rows = ui_rows();
    L.gap = 2;
    int w = wordmark_width(WM_WORD, L.gap) + 1;
    if (w + 8 > cols) {
        L.gap = 1;
        w = wordmark_width(WM_WORD, L.gap) + 1;
    }
    L.full = w + 4 <= cols && rows >= 18;
    L.wm_width = L.full ? w : tracked_width(BRAND);
    L.wm_col = (cols - L.wm_width) / 2 + 1;
    L.strips = L.full && rows >= 24 && cols >= 60 && !U.ascii;
    int base = rows > 24 ? (rows - 24) / 2 : 0;
    if (L.strips) {
        L.label_row = base + 6;
    } else {
        L.label_row = rows >= 20 ? 3 : 2;
    }
    L.wm_row = L.label_row + 2;
    int wm_h = L.full ? WM_ROWS + 1 : 1;
    L.divider_row = L.wm_row + wm_h + 1;
    L.tag_row = L.divider_row + 1;
    L.count_row = L.tag_row + 1;
    L.prompt_row = L.tag_row + 3;
    return L;
}

static int wm_cell(char rows[WM_ROWS][WM_MAX_COLS + 1], int w, int r, int c)
{
    if (r < WM_ROWS && c < w && rows[r][c] == '#')
        return 2;
    if (!U.ascii && r >= 1 && c >= 1 && r - 1 < WM_ROWS && c - 1 < w && rows[r - 1][c - 1] == '#')
        return 1; /* drop shadow, one row down and one column right */
    return 0;
}

static void sb_at(Sb *f, int row, int col)
{
    sb_printf(f, "\033[%d;%dH", row, col < 1 ? 1 : col);
}

static void sb_centered_at(Sb *f, int row, const char *text, int width)
{
    sb_at(f, row, (ui_cols() - width) / 2 + 1);
    sb_puts(f, text);
}

/* Film strip: rows of sprocket holes, frames, sprocket holes, built from
 * a repeating six-column unit; k is the position along the film. */
static void sb_strip_cell(Sb *sb, int row, int k)
{
    int u = ((k % 6) + 6) % 6;
    const char *glyph = " ";
    if (row == 1)
        glyph = u < 5 ? G_SHADE : " ";
    else if (u % 2 == 0 && u < 5)
        glyph = row == 0 ? G_LOWER : G_UPPER;
    sb_puts(sb, glyph);
}

/* `x` of `len` columns visible: the upper strip enters from the left edge,
 * the lower one from the right edge. */
static void draw_strips(Sb *f, int x, int len, int phase)
{
    int cols = ui_cols();
    int rows = ui_rows();
    for (int r = 0; r < 3; r++) {
        UiColor color = r == 1 ? UI_BRASS : UI_ASH;
        sb_at(f, 2 + r, 1);
        ui_sb_fg(f, color);
        for (int i = 0; i < len; i++) {
            if (i < x)
                sb_strip_cell(f, r, i + (len - x) + phase);
            else
                sb_putc(f, ' ');
        }
        sb_at(f, rows - 3 + r, cols - len + 1);
        ui_sb_fg(f, color);
        for (int i = 0; i < len; i++) {
            if (i >= len - x)
                sb_strip_cell(f, r, i - (len - x) - phase);
            else
                sb_putc(f, ' ');
        }
    }
}

static void fade_fg(Sb *sb, UiColor target, int step, int steps)
{
    if (!U.color)
        return;
    const Rgb *to = &PALETTE[target];
    if (U.truecolor) {
        Rgb c;
        c.r = (unsigned char)(MIDNIGHT.r + (to->r - MIDNIGHT.r) * step / steps);
        c.g = (unsigned char)(MIDNIGHT.g + (to->g - MIDNIGHT.g) * step / steps);
        c.b = (unsigned char)(MIDNIGHT.b + (to->b - MIDNIGHT.b) * step / steps);
        c.x256 = to->x256;
        sb_rgb(sb, &c, false);
    } else {
        int grey = step >= steps ? to->x256 : 233 + (243 - 233) * step / steps;
        sb_printf(sb, "\033[38;5;%dm", grey);
    }
}

static void draw_wordmark(Sb *f, const IntroLayout *L)
{
    if (!L->full) {
        sb_at(f, L->wm_row, L->wm_col);
        ui_sb_fg(f, UI_IVORY);
        ui_sb_bold(f);
        ui_sb_tracked(f, BRAND);
        ui_sb_plain(f);
        return;
    }
    char wm[WM_ROWS][WM_MAX_COLS + 1];
    int w = wordmark_render(WM_WORD, L->gap, wm);
    for (int r = 0; r <= WM_ROWS; r++) {
        sb_at(f, L->wm_row + r, L->wm_col);
        int current = -1;
        for (int c = 0; c <= w; c++) {
            int cell = wm_cell(wm, w, r, c);
            if (cell && cell != current) {
                ui_sb_fg(f, cell == 2 ? UI_IVORY : UI_ASH);
                current = cell;
            }
            sb_puts(f, cell ? G_BLOCK : " ");
        }
    }
}

static void draw_divider(Sb *f, const IntroLayout *L, int width)
{
    ui_sb_fg(f, UI_ASH);
    sb_at(f, L->divider_row, (ui_cols() - width) / 2 + 1);
    sb_repeat(f, G_RULE, width);
}

static void draw_intro_static(Sb *f, const IntroLayout *L, const char *count_line, bool strips)
{
    sb_puts(f, "\033[H");
    sb_bg(f, &MIDNIGHT);
    sb_puts(f, "\033[2J");
    if (strips && L->strips) {
        int len = ui_cols() / 3 > 36 ? 36 : ui_cols() / 3;
        draw_strips(f, len, len, 0);
    }
    ui_sb_fg(f, UI_GOLD);
    sb_at(f, L->label_row, (ui_cols() - tracked_width(ARCHIVE_LABEL)) / 2 + 1);
    ui_sb_tracked(f, ARCHIVE_LABEL);
    draw_wordmark(f, L);
    draw_divider(f, L, 24);
    ui_sb_fg(f, UI_STONE);
    sb_centered_at(f, L->tag_row, TAGLINE, text_width(TAGLINE));
    ui_sb_fg(f, UI_RED);
    int pw = text_width(ENTER_PROMPT) + 3;
    sb_at(f, L->prompt_row, (ui_cols() - pw) / 2 + 1);
    sb_puts(f, G_MARKER "  ");
    ui_sb_fg(f, UI_GREY);
    sb_puts(f, ENTER_PROMPT);
    if (count_line && L->count_row < ui_rows()) {
        ui_sb_fg(f, UI_ASH);
        sb_centered_at(f, L->count_row, count_line, text_width(count_line));
    }
    ui_sb_plain(f);
}

bool ui_intro(const char *count_line, void (*on_start)(void))
{
    if (!U.interactive || U.eof)
        return !U.eof;
    IntroLayout L = intro_layout();
    int len = ui_cols() / 3 > 36 ? 36 : ui_cols() / 3;
    bool skip = false;
    Sb f;
    sb_init(&f);

    if (U.animate) {
        /* Black first. */
        sb_puts(&f, "\033[?25l\033[H");
        sb_bg(&f, &MIDNIGHT);
        sb_puts(&f, "\033[2J");
        emit_sb(&f);
        sb_clear(&f);
        if (on_start)
            on_start();
        skip = wait_or_key(120);

        /* 1. Film strips glide in from the upper-left and lower-right,
         *    easing to rest. */
        const int enter_frames = 12;
        for (int t = 1; L.strips && t <= enter_frames && !skip; t++) {
            double p = (double)t / enter_frames;
            double eased = 1.0 - (1.0 - p) * (1.0 - p) * (1.0 - p);
            draw_strips(&f, (int)(len * eased + 0.5), len, enter_frames - t);
            emit_sb(&f);
            sb_clear(&f);
            skip = wait_or_key(24);
        }

        /* 2. The archival label fades up. */
        for (int s = 1; s <= 5 && !skip; s++) {
            fade_fg(&f, UI_GOLD, s, 5);
            sb_at(&f, L.label_row, (ui_cols() - tracked_width(ARCHIVE_LABEL)) / 2 + 1);
            ui_sb_tracked(&f, ARCHIVE_LABEL);
            emit_sb(&f);
            sb_clear(&f);
            skip = wait_or_key(30);
        }

        /* 3. The wordmark is uncovered left to right, like a projector beam
         *    sweeping across it: the leading edge glows gold, then settles. */
        if (!skip && L.full) {
            char wm[WM_ROWS][WM_MAX_COLS + 1];
            int w = wordmark_render(WM_WORD, L.gap, wm);
            int total = w + 1;
            int prev = 0;
            for (int x = 0; x < total && !skip;) {
                int nx = x + 3 < total ? x + 3 : total;
                for (int c = prev; c < nx; c++) {
                    for (int r = 0; r <= WM_ROWS; r++) {
                        int cell = wm_cell(wm, w, r, c);
                        sb_at(&f, L.wm_row + r, L.wm_col + c);
                        ui_sb_fg(&f, cell == 2 ? (c >= x ? UI_GOLD : UI_IVORY) : UI_ASH);
                        sb_puts(&f, cell ? G_BLOCK : " ");
                    }
                }
                emit_sb(&f);
                sb_clear(&f);
                skip = wait_or_key(16);
                prev = x;
                x = nx;
            }
        } else if (!skip) {
            char up[64];
            text_upper(up, sizeof up, BRAND);
            for (size_t i = 1; i <= strlen(up) && !skip; i++) {
                sb_at(&f, L.wm_row, L.wm_col);
                ui_sb_fg(&f, UI_IVORY);
                for (size_t k = 0; k < i; k++) {
                    if (k)
                        sb_putc(&f, ' ');
                    sb_putc(&f, up[k]);
                }
                emit_sb(&f);
                sb_clear(&f);
                skip = wait_or_key(45);
            }
        }

        /* 4. The divider opens from the centre; the tagline fades up. */
        for (int s = 1; s <= 6 && !skip; s++) {
            draw_divider(&f, &L, 24 * s / 6);
            fade_fg(&f, UI_STONE, s, 6);
            sb_centered_at(&f, L.tag_row, TAGLINE, text_width(TAGLINE));
            emit_sb(&f);
            sb_clear(&f);
            skip = wait_or_key(40);
        }
    }

    /* The settled title card. */
    draw_intro_static(&f, &L, count_line, true);
    emit_sb(&f);
    sb_clear(&f);

    bool proceed = true;
    if (!skip) { /* a key that skipped the animation also enters */
        for (;;) {
            Key k = read_key();
            if (k.kind == K_EOF) {
                U.eof = true;
                proceed = false;
                break;
            }
            if (k.kind == K_RESIZE) {
                L = intro_layout();
                draw_intro_static(&f, &L, count_line, true);
                emit_sb(&f);
                sb_clear(&f);
                continue;
            }
            if (k.kind == K_CHAR && (k.ch == 'q' || k.ch == 'Q')) {
                proceed = false;
                break;
            }
            if (k.kind != K_NONE)
                break;
        }
    }
    if (U.eof)
        proceed = false;

    /* 5. The strips withdraw and the archive opens. */
    if (proceed && U.animate && L.strips) {
        for (int t = 10; t >= 0; t -= 2) {
            double p = (double)t / 10;
            double eased = 1.0 - (1.0 - p) * (1.0 - p) * (1.0 - p);
            draw_strips(&f, (int)(len * eased + 0.5), len, t);
            emit_sb(&f);
            sb_clear(&f);
            if (input_ready(16))
                break;
        }
    }
    sb_free(&f);
    return proceed;
}

void ui_reveal(const char *section, const char *heading, const char *const *names, size_t n,
               const char *final_title)
{
    if (!U.interactive || U.eof)
        return;
    /* Lay out exactly as the result screen will: header, heading, blank,
     * title, so the screen settles into place without a jump. */
    int title_row = header_rows(section) + 3;
    Sb f;
    sb_init(&f);
    frame_start(&f);
    sb_puts(&f, "\033[2J\033[H");
    draw_header(&f, section, true);
    ui_sb_margin(&f);
    ui_sb_fg(&f, UI_GOLD);
    ui_sb_tracked(&f, heading);
    emit_sb(&f);
    sb_clear(&f);
    text_copy(U.last_section, sizeof U.last_section, section);

    char up[512];
    if (U.animate && n > 0) {
        /* The reel turns quickly, then slows, like a projector settling. */
        static const int delays[] = {40, 40, 45, 50, 60, 72, 88, 108, 135};
        size_t frames = sizeof delays / sizeof delays[0];
        for (size_t i = 0; i < frames; i++) {
            text_upper(up, sizeof up, names[(i * 7 + 3) % n]);
            sb_at(&f, title_row, ui_margin() + 1);
            ui_sb_fg(&f, i + 3 < frames ? UI_ASH : UI_GREY);
            sb_fit(&f, up, ui_content_width(), true);
            emit_sb(&f);
            sb_clear(&f);
            if (wait_or_key(delays[i]))
                break;
        }
    }
    text_upper(up, sizeof up, final_title);
    sb_at(&f, title_row, ui_margin() + 1);
    ui_sb_fg(&f, UI_IVORY);
    ui_sb_bold(&f);
    sb_fit(&f, up, ui_content_width(), true);
    ui_sb_plain(&f);
    emit_sb(&f);
    sb_free(&f);
}

void ui_farewell(int ms)
{
    if (!U.interactive)
        return;
    Sb f;
    sb_init(&f);
    sb_puts(&f, "\033[H");
    sb_bg(&f, &MIDNIGHT);
    sb_puts(&f, "\033[2J");
    const char *line = "The lights come up. Until the next screening.";
    ui_sb_fg(&f, UI_STONE);
    ui_sb_italic(&f);
    sb_centered_at(&f, ui_rows() / 2, line, text_width(line));
    ui_sb_plain(&f);
    emit_sb(&f);
    sb_free(&f);
    if (ms > 0)
        wait_or_key(ms);
}

void ui_print_banner(void)
{
    char rows[WM_ROWS][WM_MAX_COLS + 1];
    int w = wordmark_render(WM_WORD, 2, rows);
    bool ascii = U.ascii || !locale_is_utf8() || (getenv("CINEPHILIA_ASCII") && *getenv("CINEPHILIA_ASCII"));
    Sb sb;
    sb_init(&sb);
    int label_pad = 2 + (w - tracked_width(ARCHIVE_LABEL)) / 2;
    sb_putc(&sb, '\n');
    sb_spaces(&sb, label_pad);
    ui_sb_tracked(&sb, ARCHIVE_LABEL);
    sb_puts(&sb, "\n\n");
    for (int r = 0; r < WM_ROWS; r++) {
        sb_spaces(&sb, 2);
        for (int c = 0; c < w; c++)
            sb_puts(&sb, rows[r][c] == '#' ? (ascii ? "#" : G_BLOCK) : " ");
        sb_putc(&sb, '\n');
    }
    sb_putc(&sb, '\n');
    sb_spaces(&sb, 2 + (w - 24) / 2);
    sb_repeat(&sb, ascii ? "-" : G_RULE, 24);
    sb_putc(&sb, '\n');
    sb_spaces(&sb, 2 + (w - text_width(TAGLINE)) / 2);
    sb_puts(&sb, TAGLINE);
    sb_puts(&sb, "\n\n");
    fputs(sb_str(&sb), stdout);
    sb_free(&sb);
}
