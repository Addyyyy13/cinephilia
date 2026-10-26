#ifndef UI_H
#define UI_H

#include "film.h"
#include <sys/ioctl.h>
#include <unistd.h>
#include <termios.h>

/* --- ANSI Color Palette --- */
#define RESET       "\033[0m"
#define BOLD        "\033[1m"
#define DIM         "\033[2m"
#define IVORY       "\033[38;2;234;228;216m"    /* #EAE4D8 */
#define CORAL       "\033[38;2;199;109;97m"     /* #C76D61 */
#define GOLD        "\033[38;2;184;154;99m"     /* #B89A63 */
#define MUTED       "\033[38;2;119;115;109m"    /* #77736D */
#define BG_DARK     "\033[48;2;10;10;10m"       /* #0A0A0A */

/* --- Terminal Utilities --- */
void ui_get_terminal_size(int *cols, int *rows);
void ui_clear_screen(void);
void ui_move_cursor(int row, int col);
void ui_hide_cursor(void);
void ui_show_cursor(void);
void ui_flush(void);
int  ui_is_interactive(void);

/* --- Safe Input --- */
void ui_read_line(const char *prompt, char *buf, int maxlen);
int  ui_read_int(const char *prompt, int *out);
int  ui_read_float(const char *prompt, float *out);
int  ui_confirm(const char *prompt); /* returns 1 for yes, 0 for no */
void ui_press_enter(void); /* "Press Enter to continue..." */

/* --- Display Components --- */
void ui_print_header(const char *title);
void ui_print_divider(void);
void ui_print_film_card(const Film *film, int show_details);
void ui_print_film_list(Archive *ar, int watched_filter); /* -1=all, 0=watchlist, 1=watched */
void ui_print_empty_archive(void);

/* --- Opening Sequence --- */
void ui_opening_animation(void);
void ui_print_title_art(void);

/* --- Main Menu --- */
int  ui_main_menu(void);

/* --- Film Forms --- */
void ui_add_film_form(Archive *ar);
void ui_edit_film_form(Archive *ar);
void ui_delete_film(Archive *ar);
void ui_view_film_detail(Archive *ar);

/* --- Terminal State --- */
void ui_setup_terminal(void);
void ui_restore_terminal(void);

#endif /* UI_H */
