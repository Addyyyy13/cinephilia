# Cinephilia — build with Clang (the default compiler on macOS).
#
#   make            build ./cinephilia and synthesise the sound effects
#   make run        build and open the archive
#   make test       unit tests, end-to-end tests and sound checks
#   make sanitize   build with AddressSanitizer and UBSan, then run the tests
#   make sounds     (re)generate assets/audio/*.wav
#   make clean      remove build products (never touches data/)

CC      = clang
CFLAGS  = -std=gnu11 -Wall -Wextra -Wpedantic -O2
CPPFLAGS = -Iinclude

SRC     := $(wildcard src/*.c src/dsa/*.c)
HDR     := $(wildcard include/*.h include/dsa/*.h)
CORE    := $(filter-out src/main.c,$(SRC))
BIN     := cinephilia
SOUNDS  := assets/audio/startup.wav

.PHONY: all run test sanitize sounds clean

all: $(BIN) $(SOUNDS)

$(BIN): $(SRC) $(HDR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SRC) -o $@

build/gen_sounds: tools/gen_sounds.c
	@mkdir -p build
	$(CC) $(CFLAGS) $< -o $@ -lm

$(SOUNDS): build/gen_sounds
	@mkdir -p assets
	./build/gen_sounds assets/audio

sounds: build/gen_sounds
	@mkdir -p assets
	./build/gen_sounds assets/audio

build/test_core: tests/test_core.c $(CORE) $(HDR)
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) -g tests/test_core.c $(CORE) -o $@

run: all
	./$(BIN)

test: all build/test_core
	./build/test_core
	sh tests/e2e.sh ./$(BIN)
	@if command -v python3 >/dev/null 2>&1; then python3 tests/check_sounds.py assets/audio; \
	 else echo "python3 not found: sound checks skipped"; fi

sanitize: $(SOUNDS)
	@mkdir -p build
	$(CC) $(CPPFLAGS) -std=gnu11 -Wall -Wextra -Wpedantic -g -O1 -fsanitize=address,undefined \
	    -fno-omit-frame-pointer $(SRC) -o build/cinephilia-san
	$(CC) $(CPPFLAGS) -std=gnu11 -Wall -Wextra -Wpedantic -g -O1 -fsanitize=address,undefined \
	    -fno-omit-frame-pointer tests/test_core.c $(CORE) -o build/test_core-san
	./build/test_core-san
	sh tests/e2e.sh ./build/cinephilia-san

clean:
	rm -rf build $(BIN)
