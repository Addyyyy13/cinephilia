# CINEPHILIA

*What is this world without art?*

Cinephilia is a personal archive of cinema for the terminal: a quiet, typographic
catalogue of the films you have seen and the ones you mean to see, with a
viewing journal, transparent recommendations, statistics and an optional,
understated sound design. It is written in C with no dependencies beyond the
standard library and POSIX, and it is built for the macOS Terminal.

```
                 P E R S O N A L   C I N E M A   A R C H I V E

     █████  ████  ██   ██  █████  █████   ██  ██  ████  ██     ████   ████
    ██       ██   ███  ██  ██     ██  ██  ██  ██   ██   ██      ██   ██  ██
    ██       ██   ██ █ ██  ████   █████   ██████   ██   ██      ██   ██████
    ██       ██   ██  ███  ██     ██      ██  ██   ██   ██      ██   ██  ██
     █████  ████  ██   ██  █████  ██      ██  ██  ████  █████  ████  ██  ██

                            ────────────────────────
                        WHAT IS THIS WORLD WITHOUT ART?

                      ›  PRESS ENTER TO ENTER THE ARCHIVE
```

## Features

**The collection.** Add films with title, director, release year, genre, a
personal rating out of ten and notes. Browse them as numbered catalogue
entries, open each one as a catalogue card, edit any detail, and delete with
confirmation (and undo, until you quit). Search titles and directors with
matches highlighted; filter by genre, minimum rating, release years or
status; order by title (ignoring *The / A / An*), director, year, rating or
date added. Long titles and wide characters (花様年華) never break the layout.

**Watchlist and journal.** Every film is either *watched* or *on the
watchlist*, always shown in words as well as colour. Marking a film watched
records the date, your rating and a few words in the cinema journal; later
viewings add further entries, so the journal becomes a dated notebook of
everything you have watched.

**Tonight's selection.** *Surprise me* picks from the whole collection, *From
the watchlist* from what you haven't seen; a short reel-like reveal settles
on the choice. *Recommended* ranks your watchlist by how you have rated other
films of the same genre and by the same director, and states the reason for
each ("Wong Kar-wai — you average 9.5 across 2 of their films"). *A double
feature* pairs two films that share a director, a genre or a decade. All of
it is computed from your own archive: no network, no AI.

**Statistics.** Films archived and average rating in large numerals; watched
and watchlist counts; highest-rated films; favourite genres; viewings over the
last twelve months; decades; directors; your rating distribution. Every
figure is calculated from the stored data.

**Sound (optional).** A small set of original, synthesised effects: a
projector spinning up with the shutter flickering at 24 frames per second, a
muted switch for navigation, a catalogue card filed away, a reel slowing to a
stop. Quiet by default, never blocking, and entirely optional. See
[Sound](#sound).

## Requirements

- macOS with the Xcode Command Line Tools, which provide `clang` and `make`:
  ```sh
  xcode-select --install
  ```
- A terminal at least 60 × 20; 80 × 24 or larger shows the full title card.
  Terminal.app and iTerm2 both work; true colour is used when the terminal
  announces it (`COLORTERM=truecolor`), otherwise a 256-colour approximation
  of the same palette.

It also builds and runs on Linux (`clang` or `gcc`); there it is silent,
because sound uses macOS's `afplay`.

## Building and running

```sh
make            # builds ./cinephilia and synthesises assets/audio/*.wav
./cinephilia    # opens data/cinephilia.db (created when you add a film)
```

To try it with an example collection (real films; the ratings and notes are
illustrative):

```sh
mkdir -p data && cp examples/sample_archive.db data/cinephilia.db
./cinephilia
```

Without `make`, the program is a single Clang invocation:

```sh
clang -std=gnu11 -Wall -Wextra -Wpedantic -O2 -Iinclude src/*.c src/dsa/*.c -o cinephilia
clang -std=gnu11 -O2 tools/gen_sounds.c -o gen_sounds -lm && ./gen_sounds assets/audio   # optional
```

### Options

| Option | Effect |
|---|---|
| `-d`, `--data PATH` | Use another archive file (also `CINEPHILIA_DATA`) |
| `--no-animation` | No motion for this session (also `CINEPHILIA_NO_ANIMATION=1`) |
| `--no-color` | Monochrome (also honours `NO_COLOR`) |
| `--mute` | No sound for this session (also `CINEPHILIA_MUTE=1`) |
| `--banner` | Print the title and exit |
| `-h`, `--help` / `-V`, `--version` | Help and version |

`CINEPHILIA_ASCII=1` forces the ASCII fallback described under *Design*.

## Using the archive

| Key | Action |
|---|---|
| `↑` `↓` (or `k` `j`) | Move |
| `Enter` or `→` | Open the selected item |
| `q`, `Esc`, `←` or `Backspace` | Back (on the index: exit) |
| `1`–`9` | Jump to a numbered section on the index |
| `PgUp` `PgDn` `Home` `End` | Move further in long lists |
| Any key | Skip the opening sequence |
| `Ctrl+C` | Quit at once; the terminal is restored and nothing is lost |

In forms, press Enter after each answer. Leaving the title empty cancels; in
optional fields `-` leaves a value out (or clears it when editing); a date
may be typed as `YYYY-MM-DD` or left blank for today. Every change is saved
the moment it is made.

```
    C I N E P H I L I A                              PERSONAL CINEMA ARCHIVE
    ────────────────────────────────────────────────────────────────────────

    T H E   A R C H I V E
    A collection of stories, frames, and feelings.

    ────────────

    › 01  THE COLLECTION        014 films
      02  ADD A FILM            a new record
      03  SEARCH THE ARCHIVE    titles and directors
      04  WATCHLIST             005 to see
      05  TONIGHT'S SELECTION   chance, taste, pairings
      06  CINEMA JOURNAL        011 entries
      07  STATISTICS            the archive in numbers
      08  SETTINGS              motion, sound
      09  EXIT

    014 FILMS ARCHIVED  ·  009 WATCHED  ·  005 TO SEE
    “What is this world without art?”

    ↑ ↓  MOVE     ENTER  OPEN     1-9  JUMP     Q  EXIT
```

```
    C I N E P H I L I A                                       CATALOGUE CARD
    ────────────────────────────────────────────────────────────────────────

    CLEO FROM 5 TO 7
    ────────────

    DIRECTOR          Agnès Varda
    RELEASE YEAR      1962
    GENRE             Drama
    PERSONAL RATING   08/10
    STATUS            Watched  ·  last seen 11 Jul 2026

    NOTES
    Ninety minutes in real time, and Paris turns from mirror to window.

    › LOG A VIEWING             date, rating, a few words
      EDIT DETAILS
      RETURN TO THE WATCHLIST   to see it again
```

```
    S T A T I S T I C S   /   THE ARCHIVE IN NUMBERS

    ▀█  █ █                     █▀█   █▀█
     █  ▀▀█                     █▀█   ▀▀█
    ▄█▄   █                     █▄█ ▄ ▄▄█
    FILMS ARCHIVED              AVERAGE RATING / 10

    HIGHEST RATED
    10/10   IN THE MOOD FOR LOVE                 2000  ·  Wong Kar-wai
    10/10   STALKER                              1979  ·  Andrei Tarkovsky
    09/10   JEANNE DIELMAN, 23 QUAI DU COMMER…   1975  ·  Chantal Akerman
```

When input or output is not a terminal (a pipe, a file, `TERM=dumb`), the
same screens are printed as numbered menus answered by number, with no escape
sequences. This keeps Cinephilia scriptable and is how the end-to-end tests
drive it.

## Data and privacy

Everything stays on your machine. Nothing is sent anywhere, and there is no
telemetry.

- **Where:** `data/cinephilia.db` relative to the current folder, unless
  `--data` or `CINEPHILIA_DATA` says otherwise. Preferences live beside it in
  `settings.conf`. Both are created with owner-only permissions (`0600`, in a
  `0700` folder) and are excluded from Git by `.gitignore`.
- **Saving:** after every change. The new archive is written to
  `cinephilia.db.tmp`, flushed to the disk (`F_FULLFSYNC` on macOS), and then
  atomically renamed over the old file, so an interruption leaves either the
  previous archive or the new one, never a mixture. The version being
  replaced is kept as `cinephilia.db.bak`.
- **Damage:** a malformed line is skipped and counted, never fatal; the rest
  of the archive loads, and the original file is copied to
  `cinephilia.db.damaged-<timestamp>` before anything is rewritten. A file
  that is not a Cinephilia archive, or comes from a newer version, is opened
  read-only and never overwritten. If a save fails (a full disk, a
  read-only folder), you are told, the change stays in memory, and saving is
  retried with the next change and on exit.

### File format (version 1)

Plain UTF-8 text, one record per line, fields separated by a TAB. In text
fields, `\\`, `\t`, `\n` and `\r` stand for a backslash, tab, newline and
carriage return. Lines starting with `#` are comments.

```
CINEPHILIA 1
F <id> <w|l> <year> <rating> <added> <last-seen> <title> <director> <genre> <notes>
J <id> <film-id> <date> <note>
```

`w` is watched and `l` is on the watchlist; year `0` means unknown and rating
`0` unrated; dates are `YYYY-MM-DD` or empty. Ids are never reused. The
header's version number allows future migrations: a newer format is refused
rather than misread.

## Sound

Cinephilia hands each effect to `afplay`, the command-line player built into
macOS, started with `posix_spawn` and an argument list (no shell, so paths
with spaces are safe). It never waits for a sound; it runs at most four
players at once; repeated navigation clicks are throttled (one at a time, at
least 70 ms apart); and every player runs in its own process group, so on
exit, on `Ctrl+C` or when sound is switched off, each player and anything it
started is stopped. If `afplay` or a sound file is missing, the effect is
skipped silently: the application never depends on sound, and every
confirmation is also shown as text.

| Moment | File | Character |
|---|---|---|
| Opening | `startup.wav` | Room tone, projector spin-up, 24 fps shutter, a warm A-major chord timed to the title |
| Navigation | `menu_move.wav` | A muted switch, 45 ms |
| Opening a section | `section_open.wav` | A card turned in a catalogue drawer |
| Adding a film | `film_added.wav` | A card slid into the drawer, a soft two-note bell |
| Saving, searching | `confirm.wav` | A quiet two-note accent |
| Tonight's selection | `film_reel.wav`, `reveal.wav` | A reel slowing down; a gentle chime |
| Deleting | `delete.wav` | A neutral, low close |
| Errors | `error.wav` | Two low taps |
| Leaving | `projector_stop.wav` | The projector winding down |

**The sounds are original.** They are synthesised from sine waves and
filtered noise by [`tools/gen_sounds.c`](tools/gen_sounds.c) when you run
`make`; nothing is sampled or downloaded, so there are no third-party audio
licences to observe. They are deliberately quiet (peaks between −26 and
−12 dBFS, little energy above 6 kHz, soft fades at both ends), which
`tests/check_sounds.py` verifies. The generated `.wav` files are build
products and are not committed.

**Settings** (index → `08 SETTINGS`) switch sound effects and motion on or
off, set the volume from 1 to 10 (the default is 4; it is passed to `afplay`
and never changes your Mac's own volume), let you listen to each sound, and
reset everything. `--mute` silences a single session.

**Not included:** an ambient "empty cinema" loop. A seamless, pleasant loop
needs listening and judgement that could not be done for this version, so it
was left out rather than shipped as a harsh or repetitive drone.

## Design

The interface is set like a printed catalogue: letter-spaced capitals for
headings, upper-case titles, small grey labels on catalogue cards, thin rules
instead of boxes, and generous space.

| Role | Colour |
|---|---|
| Background | midnight `#0A0A0A` |
| Footer band | charcoal `#151515` |
| Selected row | graphite `#1D1B19` |
| Primary text | warm ivory `#EAE4D8` |
| Secondary text | muted stone `#969087` |
| Hints | grey `#77736D` |
| Actions, selection | cinema red `#C76D61` |
| Small highlights | antique gold `#B89A63` |
| Rules, shadows | dark ash `#35312D` |

- **The title** is drawn from a hand-made five-row block font with a drop
  shadow. Its spelling is not taken on trust: the tests decode the rendered
  letters back into text, once with the C font and once with an independent
  copy in Python, and require `CINEPHILIA`.
- **Motion** is brief and skippable: film strips glide in from the
  upper-left and lower-right corners, the title is uncovered left to right
  as if by a projector beam, the tagline fades up. It settles in about
  1.2 seconds. Section changes draw the header rule across in a tenth of a
  second; Tonight's selection spins briefly through titles before settling.
  Nothing moves while you read. Motion is off when the output is not a
  terminal, with `--no-animation`, or from Settings.
- **Selection** is shown by a red `›`, bold text and a graphite band, so it
  never depends on colour alone.
- **ASCII fallback:** when the locale is not UTF-8, every decorative glyph is
  replaced by a one-column ASCII equivalent on output (`·` → `-`, `█` → `#`,
  `›` → `>`), so alignment never shifts. The film strips are left out.
- **Rendering** composes each screen in memory and writes it in one call,
  overwriting lines in place rather than clearing the screen, which avoids
  flicker. The terminal (raw mode, alternate screen, cursor) is restored on
  every exit path, including `Ctrl+C`, `SIGTERM`, `SIGHUP`, and around
  `Ctrl+Z` suspension.

## Architecture

```
src/main.c        start-up, navigation, every screen and flow
src/ui.c          palette, menus, prompts, pages, transitions, opening sequence
src/wordmark.c    the block-letter title and its decoder
src/film.c        film model, the linked-list archive, the journal, undo records
src/search.c      merge sort, search, filters
src/storage.c     versioned file format, recovery, atomic saving
src/discover.c    random picks, recommendations, double features (seedable RNG)
src/stats.c       statistics
src/settings.c    preferences file
src/audio.c       non-blocking, process-controlled playback
src/tmdb.c        optional TMDB lookups through /usr/bin/curl
src/json.c        a small recursive-descent JSON parser (for TMDB replies)
src/text.c        UTF-8 width, truncation, wrapping, sanitising, dates
src/sb.c          string builder and string list
src/dsa/stack.c   LIFO stack (undo)
src/dsa/tally.c   frequency table (genres, directors)
tools/gen_sounds.c  sound synthesis
tests/            unit tests, end-to-end tests, sound checks, screen capture
```

Screens never touch storage directly, and nothing below `main.c` draws to
the terminal, so the archive logic can be tested without a terminal.

**Data structures and algorithms**, and why:

- **Doubly linked list** for the archive. Films are added and removed one at
  a time; removal is O(1) once found; pointers stay valid as others change;
  and the list sorts in place.
- **Merge sort** on that list: stable, O(n log n), needs no extra array and
  no random access (which is what rules out quicksort for a list). Unknown
  years, missing directors and unrated films always sort last.
- **Linear search** with case-insensitive substring matching: "mood" must
  find *In the Mood for Love*, which no sorted order can serve, and a
  personal archive is small enough for a scan to be instant.
- **Stack** for undo: deletions are undone most-recent-first, which is
  exactly LIFO. Each record keeps the film and its journal entries.
- **Frequency table** (an array with linear lookup) for genre and director
  tallies, the simplest structure fast enough for a few dozen keys.
- **Recommendations** use averages shrunk toward your overall mean
  (a prior weight of two films), so a single 10/10 does not dominate.
- **Random choices** use a seedable xorshift generator with rejection
  sampling (no modulo bias); double features use reservoir sampling to choose
  uniformly among the best-matched pairs.

## Testing

```sh
make test       # unit tests, end-to-end tests, sound checks
make sanitize   # the same under AddressSanitizer and UndefinedBehaviorSanitizer
```

- **Unit tests** (`tests/test_core.c`, 1,367 checks): UTF-8 width and
  truncation, sanitising of escape sequences, dates and leap years, the
  wordmark decoding to CINEPHILIA, linked-list integrity after sorts and
  deletes, sort order and stability over 600 random films, filters, storage
  round trips with tabs, backslashes and CJK text, malformed and truncated
  files, foreign and newer files, write failures that must leave the old
  archive intact, uniformity of random picks, recommendation ranking, double
  features, statistics on empty and populated archives, settings, the JSON
  parser and TMDB reply handling, and audio process control (missing player, missing files,
  throttling, the four-player cap, paths with spaces, cleanup with no
  zombies).
- **End-to-end tests** (`tests/e2e.sh`, 96 checks) drive the real binary
  through first launch, adding and displaying, searching for existing and
  missing films, editing, deleting and undoing, reloading after restart,
  random selection with zero, one and many films, invalid choices and
  malformed input, statistics, damaged and foreign files, failed saves, UTF-8
  output and settings.
- **Sound checks** (`tests/check_sounds.py`) measure every generated file:
  format, duration, peak and RMS level, clipping, clicks at the edges, DC
  offset and high-frequency content.
- **Screen capture** (`tests/screen_capture.py`) runs the program in a
  pseudo-terminal, interprets its output with a small terminal emulator and
  saves each screen as text and PNG. It also confirms that the terminal is
  restored after normal exit and after `Ctrl+C`.

## TMDB

Cinephilia can fill in a film's director, release year and genre from
[The Movie Database](https://www.themoviedb.org/). It is optional: without
a key the feature is simply not offered, and everything else works offline.

1. Create a free TMDB account, then request an API key under
   **Settings → API** (choose *Developer*).
2. Make the key available to Cinephilia, for example in `~/.zshrc`:
   ```sh
   export TMDB_API_KEY="your-32-character-key"
   ```
3. Open a new Terminal window and add a film. After you type the title,
   Cinephilia asks *Look this film up on TMDB?*, shows what it found, and
   asks before using it.

How it works: requests are made by macOS's own `/usr/bin/curl`, so there is
nothing extra to install. The key is handed to curl on its standard input,
never on the command line where other users could see it, and it is never
written to the archive or the repository. Replies are read by a small JSON
parser (`src/json.c`); text from TMDB is cleaned of control characters
before it is shown or stored. A missing connection, an unknown title, a
rejected key and TMDB's rate limit each produce a clear message, and you can
always type the details yourself.

*This product uses the TMDB API but is not endorsed or certified by TMDB.*
TMDB's API terms allow free use for non-commercial projects.

## Known limitations

- Sound plays only on macOS, through `afplay`, in an interactive terminal.
- Case-insensitive search and sorting fold ASCII letters only: searching
  `amelie` will not find *Amélie* (searching `am` will).
- Each film has a single genre field.
- Only one copy of Cinephilia should have an archive open at a time; there
  is no locking between simultaneous sessions.
- Undo for deletions lasts until you quit.
- Upper-casing for editorial titles covers ASCII and the common accented
  Latin letters; other scripts are shown as typed.

## Authors

- [Addyyyy13](https://github.com/Addyyyy13)
- [urdadsushi](https://github.com/urdadsushi)

## Licence

No licence has been chosen yet. Until one is added, the default copyright
rules apply. The sound effects are generated by this project's own code and
can be released under whatever licence you choose for it.
