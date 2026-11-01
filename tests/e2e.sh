#!/bin/sh
# End-to-end tests: drive the real binary through its line mode (input from
# a pipe, numbered menus) and check both what it prints and what it stores.
#
# Usage: tests/e2e.sh ./cinephilia
set -u

BIN=${1:-./cinephilia}
case "$BIN" in /*) ;; *) BIN="$(pwd)/$BIN" ;; esac
WORK=$(mktemp -d "${TMPDIR:-/tmp}/cinephilia-e2e.XXXXXX")
trap 'chmod -R u+w "$WORK" 2>/dev/null; rm -rf "$WORK"' EXIT INT TERM

PASS=0
FAIL=0
OUT="$WORK/out.txt"
DB="$WORK/archive.db"

# Deterministic and ASCII: fixed date, fixed random seed, C locale.
export CINEPHILIA_TODAY=2026-10-22
export CINEPHILIA_SEED=42
export LANG=C LC_ALL=C
unset CINEPHILIA_DATA NO_COLOR

ok() { PASS=$((PASS + 1)); printf '  ok    %s\n' "$1"; }
bad() { FAIL=$((FAIL + 1)); printf '  FAIL  %s\n' "$1"; [ -n "${2:-}" ] && printf '        %s\n' "$2"; }

# run DB INPUT [args...]: feeds INPUT on stdin, captures stdout+stderr in $OUT
# and the exit status in $STATUS.
run() {
    db=$1; input=$2; shift 2
    printf '%b' "$input" | "$BIN" --data "$db" "$@" >"$OUT" 2>&1
    STATUS=$?
}

expect() { # expect "description" "fixed text"
    if grep -qF -- "$2" "$OUT"; then ok "$1"; else bad "$1" "missing: $2"; fi
}
reject() {
    if grep -qF -- "$2" "$OUT"; then bad "$1" "unexpected: $2"; else ok "$1"; fi
}
expect_file() { # expect_file "description" FILE "fixed text"
    if [ -f "$2" ] && grep -qF -- "$3" "$2"; then ok "$1"; else bad "$1" "$2 lacks: $3"; fi
}
reject_file() {
    if [ -f "$2" ] && grep -qF -- "$3" "$2"; then bad "$1" "$2 contains: $3"; else ok "$1"; fi
}
expect_status() {
    if [ "$STATUS" -eq "$2" ]; then ok "$1"; else bad "$1" "exit status $STATUS, wanted $2"; fi
}
fixture() { # three films: Stalker (watched 10), Persona (watched 9), Playtime (watchlist)
    TAB=$(printf '\t')
    {
        echo "CINEPHILIA 1"
        echo "F${TAB}1${TAB}w${TAB}1979${TAB}10${TAB}2026-01-05${TAB}2026-02-01${TAB}Stalker${TAB}Andrei Tarkovsky${TAB}Science Fiction${TAB}The Zone answers wishes."
        echo "F${TAB}2${TAB}w${TAB}1965${TAB}9${TAB}2026-01-06${TAB}2026-03-01${TAB}Persona${TAB}Ingmar Bergman${TAB}Drama${TAB}"
        echo "F${TAB}3${TAB}l${TAB}1967${TAB}0${TAB}2026-01-07${TAB}${TAB}Playtime${TAB}Jacques Tati${TAB}Comedy${TAB}"
        echo "J${TAB}1${TAB}1${TAB}2026-02-01${TAB}Long takes, longer silences."
        echo "J${TAB}2${TAB}2${TAB}2026-03-01${TAB}"
    } >"$1"
}

echo "Cinephilia end-to-end tests ($BIN)"

# --- command line --------------------------------------------------------
echo " command line"
"$BIN" --help >"$OUT" 2>&1; STATUS=$?
expect_status "--help exits 0" 0
expect "--help documents --data" "--data PATH"
"$BIN" --version >"$OUT" 2>&1; STATUS=$?
expect "--version prints the version" "cinephilia 1.0.0"
"$BIN" --bogus >"$OUT" 2>&1; STATUS=$?
expect_status "an unknown option exits 2" 2
"$BIN" --data >"$OUT" 2>&1; STATUS=$?
expect_status "--data without a path exits 2" 2
"$BIN" --banner >"$OUT" 2>&1; STATUS=$?
expect "--banner prints the tagline" "WHAT IS THIS WORLD WITHOUT ART?"
if python3 - "$OUT" >/dev/null <<'PY'
import sys
# An independent decoder: the font is re-typed here, not imported from C.
FONT = {
    "A": [" #### ", "##  ##", "######", "##  ##", "##  ##"],
    "C": [" #####", "##    ", "##    ", "##    ", " #####"],
    "E": ["#####", "##   ", "#### ", "##   ", "#####"],
    "H": ["##  ##", "##  ##", "######", "##  ##", "##  ##"],
    "I": ["####", " ## ", " ## ", " ## ", "####"],
    "L": ["##   ", "##   ", "##   ", "##   ", "#####"],
    "N": ["##   ##", "###  ##", "## # ##", "##  ###", "##   ##"],
    "P": ["##### ", "##  ##", "##### ", "##    ", "##    "],
}
lines = [l.rstrip("\n") for l in open(sys.argv[1])]
rows = [l for l in lines if "#" in l]
assert len(rows) == 5, rows
w = max(len(r) for r in rows)
rows = [r.ljust(w) for r in rows]
blank = [all(r[c] == " " for r in rows) for c in range(w)]
word, c = "", 0
while c < w:
    if blank[c]:
        c += 1
        continue
    s = c
    while c < w and not blank[c]:
        c += 1
    block = [r[s:c] for r in rows]
    match = [k for k, g in FONT.items() if g == block]
    assert match, block
    word += match[0]
print(word)
sys.exit(0 if word == "CINEPHILIA" else 1)
PY

then ok "the banner decodes, letter by letter, to CINEPHILIA"; else bad "the banner decodes to CINEPHILIA"; fi

# --- 1. clean first launch --------------------------------------------------
echo " 1. first launch with no archive"
rm -f "$DB"
run "$DB" '0\n'
expect_status "starts and exits cleanly" 0
expect "shows the archival index" "THE COLLECTION"
expect "offers all nine sections" "TONIGHT'S SELECTION"
expect "notes the empty archive" "THE SHELVES ARE EMPTY"
expect "says goodbye" "Until the next screening."
if [ ! -f "$DB" ]; then ok "nothing is written until something changes"; else bad "no file on a read-only visit"; fi
run "$DB" '1\n0\n0\n'
expect "the empty collection is explained" "Every collection begins with a single film."

# --- 2. adding and displaying ---------------------------------------------------
echo " 2. adding a film and displaying it"
run "$DB" '2\nIn the Mood for Love\nWong Kar-wai\n2000\nromance\ny\n2026-10-20\n9\nThe corridor, the rain, the cheongsams.\n0\n'
expect_status "add flow completes" 0
expect "confirms the filing" 'Filed "In the Mood for Love" in the collection.'
expect_file "the film is stored" "$DB" "In the Mood for Love	Wong Kar-wai	Romance"
expect_file "rating and dates are stored" "$DB" "w	2000	9	2026-10-22	2026-10-20"
expect_file "the viewing is in the journal" "$DB" "J	1	1	2026-10-20"
run "$DB" '1\n0\n0\n'
expect "it appears in the collection, catalogued" "IN THE MOOD FOR LOVE"
expect "with director, year and genre" "Wong Kar-wai  -  2000  -  Romance"
expect "with its rating" "09/10"
run "$DB" '2\nChungking Express\nWong Kar-wai\n1994\nRomance\nn\nA friend insists.\n0\n'
expect "a watchlist addition is confirmed" 'Added "Chungking Express" to your watchlist.'
expect_file "watchlist status is stored" "$DB" "l	1994	0"
run "$DB" '2\nin the mood for love\n\n2000\nn\n0\n'
expect "a duplicate is noticed" 'is already in the archive.'
expect "and not added when declined" "Nothing was added."
if [ "$(grep -c '^F' "$DB")" -eq 2 ]; then ok "the archive still holds two films"; else bad "duplicate was stored"; fi
run "$DB" '4\n0\n0\n'
expect "the watchlist shows the queue" "CHUNGKING EXPRESS"

# --- 3. searching -----------------------------------------------------------------
echo " 3. searching"
run "$DB" '3\nmood\n0\n0\n'
expect "an existing film is found" "1 FOUND"
expect "and listed" "IN THE MOOD FOR LOVE"
run "$DB" '3\nwong\n0\n0\n'
expect "a director search finds both" "2 FOUND"
run "$DB" '3\nkubrick\n0\n0\n'
expect "a missing film is explained" 'Nothing in the archive answers to "kubrick".'
expect "and can be added from there" 'ADD "kubrick" TO THE ARCHIVE'
run "$DB" '3\nkubrick\n2\nThe Shining\nStanley Kubrick\n1980\nHorror\nn\n\n0\n0\n0\n'
expect "adding from search pre-fills the title prompt" 'now: kubrick'
expect_file "and stores the corrected title" "$DB" "The Shining	Stanley Kubrick	Horror"

# --- 4. editing and deleting ---------------------------------------------------
echo " 4. editing and deleting"
fixture "$DB"
# Collection: 1 add, 2 search, 3 filter, 4 order, then Persona(5) Playtime(6) Stalker(7)
run "$DB" '1\n5\n2\n3\n1966\n0\n0\n0\n0\n'
expect "the edit is confirmed" "Saved."
expect_file "the corrected year is stored" "$DB" "F	2	w	1966	9"
run "$DB" '1\n7\n2\n5\n-\n0\n0\n0\n0\n'
expect_file "a rating can be cleared" "$DB" "F	1	w	1979	0"
run "$DB" '1\n6\n3\nn\n0\n0\n0\n'
expect "declining a delete keeps the film" 'Kept "Playtime".'
expect_file "Playtime is still stored" "$DB" "Playtime"
run "$DB" '1\n6\n3\ny\n0\n0\n'
expect "deleting is confirmed" 'Deleted "Playtime".'
reject_file "Playtime is gone from the file" "$DB" "Playtime"
# Persona (watched, with a journal entry): 1 log, 2 edit, 3 return, 4 journal, 5 delete
run "$DB" '1\n5\n5\ny\n5\n0\n0\n'
expect "a deletion can be undone in the same session" 'Restored "Persona".'
expect_file "the restored film keeps its id" "$DB" "F	2	w	1966	9"
expect_file "and its journal entry" "$DB" "J	2	2	2026-03-01"

# --- 5. persistence across restarts -----------------------------------------------
echo " 5. saving and reloading"
fixture "$DB"
run "$DB" '2\nClose-Up\nAbbas Kiarostami\n1990\nDrama\ny\n\n10\n\n0\n'
run "$DB" '1\n0\n0\n'
expect "a new session sees the earlier addition" "CLOSE-UP"
expect "and the fixture films" "STALKER"
if [ -f "$DB.bak" ] && ! grep -qF "Close-Up" "$DB.bak"; then ok "the previous version is kept as .bak"; else bad "backup of the previous version"; fi
if [ ! -f "$DB.tmp" ]; then ok "no temporary file is left behind"; else bad "stray .tmp file"; fi
PERM=$(ls -l "$DB" | cut -c1-10)
if [ "$PERM" = "-rw-------" ]; then ok "the archive is private to its owner (0600)"; else bad "file permissions" "$PERM"; fi

# --- 6. random selection ------------------------------------------------------------
echo " 6. tonight's selection with zero, one and many films"
rm -f "$DB"
run "$DB" '5\n1\n2\n4\n0\n0\n'
expect "zero films: nothing to choose from" "The archive is empty - nothing to choose from yet."
expect "zero on the watchlist: explained" "The watchlist is empty - nothing to choose from yet."
expect "zero films: no double feature" "A double feature needs at least two films"
run "$DB" '2\nSolo\n\n\n\ny\n\n\n\n5\n1\n0\n0\n0\n'
expect "one film: it is chosen" "SOLO"
expect "one film: the reason is given" "It is the only film in the archive."
fixture "$DB"
run "$DB" '5\n1\n2\n2\n2\n0\n0\n0\n'
expect "many films: the selection is announced" "T O N I G H T ,   W E   W A T C H"
expect "many films: the reel can spin again" "SPIN THE REEL AGAIN"
run "$DB" '5\n3\n0\n0\n0\n'
expect "recommendations explain themselves" "RECOMMENDED"
expect "the watchlist film is ranked" "PLAYTIME"
run "$DB" '5\n4\n0\n0\n0\n'
expect "a double feature pairs two films" "A   D O U B L E   F E A T U R E"

# --- 7. invalid and malformed input ---------------------------------------------------
echo " 7. invalid menu choices and malformed input"
run "$DB" 'abc\n99\n-1\n\n3.5\n0\n'
expect_status "invalid choices never crash" 0
COUNT=$(grep -c "That isn't one of the options" "$OUT")
if [ "$COUNT" -eq 5 ]; then ok "each invalid choice is answered (5 of 5)"; else bad "invalid choice messages" "got $COUNT"; fi
run "$DB" '2\nMetropolis\nFritz Lang\nnineteen\n1700\n1927\nSci-Fi\ny\n2026-02-30\n2099-01-01\n2026-01-01\n11\n0\n9\n\n0\n'
expect "a non-number year is refused" "Please enter a whole number from 1878 to 2036."
expect "an impossible date is refused" "Please use the form YYYY-MM-DD"
expect "a future date is refused" "That date hasn't happened yet."
expect "an out-of-range rating is refused" "Please enter a whole number from 1 to 10."
expect_file "and the corrected values are stored" "$DB" "w	1927	9"
LONG=$(head -c 3000 /dev/zero | tr '\0' 'x')
run "$DB" "2\n${LONG}\n\n\n\nn\n\n0\n"
expect "an over-long title is shortened, not overflowed" "it was shortened to 159 characters"
run "$DB" '2\nEvil\033[31mTitle\001\n\n\n\nn\n\n0\n'
if grep -q "$(printf '\033')" "$DB"; then bad "escape codes are stripped before storing"; else ok "escape codes are stripped before storing"; fi
expect_file "the rest of the title is kept" "$DB" "Evil [31mTitle"
run "$DB" '2\nHalf-typed'
expect_status "input ending mid-form exits cleanly" 0
expect "and nothing half-finished is stored" "Nothing was added."
reject_file "the half-typed film is absent" "$DB" "Half-typed"

# --- 8. statistics ------------------------------------------------------------------------
echo " 8. statistics, empty and populated"
rm -f "$DB"
run "$DB" '7\n0\n'
expect "empty: says so" "Nothing to measure yet."
fixture "$DB"
run "$DB" '7\n0\n'
expect "counts the films" "FILMS ARCHIVED"
expect "averages the ratings" "9.5"
expect "lists the highest rated" "10/10   STALKER"
expect "shows genres" "Science Fiction"
expect "shows the watching history" "2 viewings  -  busiest"
expect "breaks down by decade" "1960s"
expect "lists directors" "Andrei Tarkovsky"

# --- 9. storage failures ------------------------------------------------------------------
echo " 9. damaged files and failed writes"
fixture "$DB"
printf 'this line is damaged\nF\tbroken\n' >>"$DB"
run "$DB" '0\n'
expect "damaged lines are reported" "Skipped 2 unreadable lines"
if ls "$DB".damaged-* >/dev/null 2>&1; then ok "the damaged original is preserved"; else bad "damaged copy"; fi
run "$DB" '1\n0\n0\n'
expect "the readable films still load" "STALKER"
FOREIGN="$WORK/notes.txt"
printf 'my shopping list\neggs\n' >"$FOREIGN"
run "$FOREIGN" '2\n0\n'
expect "a foreign file is refused" "does not look like a Cinephilia archive"
expect "and changes are blocked" "Read-only:"
if [ "$(cat "$FOREIGN")" = "$(printf 'my shopping list\neggs')" ]; then ok "the foreign file is untouched"; else bad "foreign file was modified"; fi
LOCKED="$WORK/locked"
mkdir -p "$LOCKED"
fixture "$LOCKED/archive.db"
mkdir "$LOCKED/archive.db.tmp"   # the temporary name is taken: every save fails
run "$LOCKED/archive.db" '2\nUnsaveable\n\n\n\nn\n\n0\n'
expect "a failed save is reported" "Could not save"
expect_status "and exit reports the unsaved change" 1
reject_file "the old archive is intact" "$LOCKED/archive.db" "Unsaveable"
expect_file "with its films" "$LOCKED/archive.db" "Stalker"
printf 'CINEPHILIA 7\n' >"$WORK/future.db"
run "$WORK/future.db" '0\n'
expect "a newer format is refused" "newer than this version"

# --- 10. UTF-8 terminals and settings --------------------------------------------------
echo " 10. UTF-8 output and settings"
fixture "$DB"
LC_ALL=en_US.UTF-8 run "$DB" '1\n0\n0\n'
expect "UTF-8 locales get typographic separators" "Andrei Tarkovsky  $(printf '\302\267')  1979"
run "$DB" '8\n0\n0\n'
expect "settings describe the sound status" "Sound plays only in an interactive terminal."
run "$DB" '8\n1\n3\n7\n0\n0\n'
expect_file "settings persist" "$WORK/settings.conf" "volume=7"
expect_file "toggles persist" "$WORK/settings.conf" "animation=off"

echo
echo "$PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ]
