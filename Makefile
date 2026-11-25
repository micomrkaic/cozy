# Cozy build.
#
# Toolchain: a C23 compiler. gcc 14+ spells the flag -std=c23; gcc 13 calls the
# same thing -std=c2x. Apple Clang from Xcode 15+ accepts -std=c2x and the C23
# features used here (nullptr, [[noreturn]], enum : uint8_t). `cc` resolves to
# gcc on Linux and to Apple Clang on macOS.
#
# Common overrides:
#   make CC=clang            choose the compiler
#   make STD=c23             newer std spelling (gcc 14+, recent clang)
#   make WERROR=             drop -Werror (handy on a new compiler whose warning
#                            set differs from the one this was tuned against)
CC      ?= cc
STD     ?= c2x
WERROR  ?= -Werror
CFLAGS   = -std=$(STD) -Wall -Wextra $(WERROR) -O2
ASANFLAGS_BASE = -std=$(STD) -Wall -Wextra -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer
LDFLAGS ?=
SRCS     = lexer.c arena.c ast.c parser.c value.c eval.c chunk.c compile.c vm.c repl.c lineedit.c main.c sparse.c kalman.c linalg_tier0.c serve.c
HDRS     = lexer.h arena.h ast.h parser.h value.h eval.h repl.h chunk.h compile.h vm.h nrt.h linalg.h sparse.h kalman.h lineedit.h
BIN      = cozy
LIBS     = -lm $(LINALG_LIBS) $(OPTIM_LIBS)

# Line editing is isocline (third_party/isocline, MIT), compiled in: no
# readline, no libedit, nothing to detect (0.1.50). The same editor runs in
# every build, so history, completion and keys behave the same everywhere.
ISOCLINE_INC = -Ithird_party/isocline/include
CFLAGS += $(ISOCLINE_INC)

# The workbench forks the REPL onto a pseudo-terminal (serve.c): forkpty is
# in libc on macOS and on glibc 2.34+, in libutil on older glibc and the BSDs.
PTY_LIBS := $(shell printf '\043include <stdlib.h>\nint forkpty(int*,char*,void*,void*);\nint main(void){int m;return forkpty(&m,0,0,0);}\n' \
              | $(CC) -xc - -o /dev/null 2>/dev/null || echo -lutil)

# ---------------------------------------------------------------------------
# Object build: each .c compiles once into build/obj-BACKEND-OPTIM/, with
# compiler-generated header dependencies (-MMD -MP), so an edit recompiles
# exactly the affected translation units and `make -jN` parallelizes the
# rest. The core objects are shared between `cozy` and `vmtest` (identical
# flags); the sanitizer build lives in its own tree (build/asan/) because
# ASan objects cannot mix.
#
# One object tree per configuration (0.1.55): `make install` (openblas+nlopt)
# followed by a plain `make test` (tier0) once reused the NLopt-built eval.o
# — timestamps said it was current — and the link died on nlopt_create. Now
# each BACKEND/OPTIM pair has its own tree, a flags stamp in each tree
# recompiles it when CC or CFLAGS change, and a link stamp relinks cozy,
# vmtest and libcozy.a whenever the configuration differs from the last
# link. Switching back and forth costs a relink, not a rebuild.
# Tip:  make -j$(nproc)
# ---------------------------------------------------------------------------
.DEFAULT_GOAL := $(BIN)
ASANDIR  = build/asan
# Linear-algebra backend (design entry 2): one kernel table, chosen here.
# tier0 = the hand-rolled kernels (zero dependencies; always works). Future
# backends add a linalg_<name>.c and a branch below — eval.c never changes.
BACKEND ?= tier0
ifeq ($(BACKEND),tier0)
  LINALG = linalg_tier0
  LINALG_LIBS =
else ifeq ($(BACKEND),openblas)
  LINALG = linalg_openblas
  CFLAGS += -DCOZY_OPENBLAS
  LINALG_LIBS = -lopenblas
else ifeq ($(BACKEND),accelerate)
  LINALG = linalg_openblas
  CFLAGS += -DCOZY_LAPACK_NAME='"accelerate"'
  LINALG_LIBS = -framework Accelerate
else
  $(error unknown BACKEND '$(BACKEND)' — available: tier0, openblas, accelerate)
endif
OPTIM ?= none
ifeq ($(OPTIM),nlopt)
  # Homebrew on Apple Silicon lives in /opt/homebrew, which is NOT on
  # clang's default search path (Intel Macs used /usr/local) — ask brew
  # where it is and search both, before probing and when building.
  NLOPT_INC :=
  NLOPT_LIB :=
  ifeq ($(shell uname -s),Darwin)
    BREW_PREFIX := $(shell brew --prefix 2>/dev/null)
    ifneq ($(BREW_PREFIX),)
      NLOPT_INC += -I$(BREW_PREFIX)/include
      NLOPT_LIB += -L$(BREW_PREFIX)/lib
    endif
    NLOPT_INC += -I/usr/local/include
    NLOPT_LIB += -L/usr/local/lib
  endif
  # probe via -include so no hash character appears here: macOS ships GNU
  # make 3.81, which treats a hash inside a function call as a comment and
  # dies with "invalid syntax in conditional" (owner's Mac, v0.1.10 reunion)
  ifeq ($(shell $(CC) $(NLOPT_INC) -E -xc -include nlopt.h /dev/null >/dev/null 2>&1 && echo yes),)
    $(error OPTIM=nlopt: nlopt.h not found — install the NLopt dev package \
      (Debian/Ubuntu: sudo apt install libnlopt-dev; macOS: brew install nlopt))
  endif
  CFLAGS += -DCOZY_NLOPT $(NLOPT_INC)
  OPTIM_LIBS = $(NLOPT_LIB) -lnlopt -lm
else
  OPTIM_LIBS =
endif
OBJDIR   = build/obj-$(BACKEND)-$(OPTIM)
# Stamps: a file whose content is the configuration, rewritten only when
# that content changes, so its mtime marks the last change. Written at parse
# time with $(shell), quotes escaped for sh (accelerate's CFLAGS carry some).
# No hash character anywhere: macOS ships GNU make 3.81.
stamp = $(shell mkdir -p $(dir $(1)) && printf '%s\n' '$(subst ','"'"',$(2))' > $(1).new && \
          { cmp -s $(1).new $(1) && rm -f $(1).new || mv -f $(1).new $(1); } && echo $(1))
ifeq ($(filter clean wasm% install,$(MAKECMDGOALS)),)
  FLAGS_STAMP := $(call stamp,$(OBJDIR)/flags,$(CC) $(CFLAGS))
  ifneq ($(filter %asan,$(MAKECMDGOALS)),)
    ASAN_STAMP := $(call stamp,$(ASANDIR)/flags,$(CC) $(ASANFLAGS_BASE))
  endif
  LINK_STAMP  := $(call stamp,build/link-config,$(CC) $(CFLAGS) $(LDFLAGS) $(LIBS) $(OBJDIR))
endif
CORE     = lexer arena ast parser value eval chunk compile vm sparse kalman $(LINALG)
CORE_O   = $(CORE:%=$(OBJDIR)/%.o)
ASAN_O   = $(CORE:%=$(ASANDIR)/%.o) $(ASANDIR)/vmtest.o
ASANFLAGS = $(ASANFLAGS_BASE)

$(OBJDIR) $(ASANDIR):
	mkdir -p $@

$(OBJDIR)/%.o: %.c $(FLAGS_STAMP) | $(OBJDIR)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

# ASan/UBSan objects: -O1 matters — setjmp-clobber bugs (handlers reading
# register-cached locals) only manifest when the optimizer register-allocates;
# at -O0 they hide.
$(ASANDIR)/%.o: %.c $(ASAN_STAMP) | $(ASANDIR)
	$(CC) $(ASANFLAGS) -MMD -MP -c $< -o $@

-include $(wildcard $(OBJDIR)/*.d) $(wildcard $(ASANDIR)/*.d)

$(BIN): $(CORE_O) $(OBJDIR)/repl.o $(OBJDIR)/lineedit.o $(OBJDIR)/isocline.o $(OBJDIR)/serve.o $(OBJDIR)/main.o $(LINK_STAMP)
	$(CC) $(CFLAGS) $(LDFLAGS) $(filter %.o,$^) $(LIBS) $(PTY_LIBS) -o $@

# isocline is vendored, not ours: its own warning set, never -Werror (third_party/isocline/COZY.md)
$(OBJDIR)/isocline.o: third_party/isocline/src/isocline.c $(wildcard third_party/isocline/src/*.[ch]) third_party/isocline/include/isocline.h $(FLAGS_STAMP) | $(OBJDIR)
	$(CC) -std=$(STD) -O2 -Wall -Wno-unused-function $(ISOCLINE_INC) -c $< -o $@

# Headless test driver: same VM as `cozy`, but reads stdin line by line and
# echoes each result (no line editor), so piping a script in gives one result per
# line. Handy for batch/regression testing and ASan runs.
# Cozy as a library (cozy.h, embed.c): the interpreter's core plus a string-in,
# numbers-out session API, for C programs that want the language as their
# numerical engine. Link with -lm, plus whatever BACKEND and OPTIM add.
lib: build/libcozy.a
build/libcozy.a: $(CORE_O) $(OBJDIR)/embed.o $(LINK_STAMP)
	rm -f $@ && ar rcs $@ $(filter %.o,$^)

vmtest: $(CORE_O) $(OBJDIR)/vmtest.o $(LINK_STAMP)
	$(CC) $(CFLAGS) $(LDFLAGS) $(filter %.o,$^) -lm $(LINALG_LIBS) $(OPTIM_LIBS) -o $@

vmtest-asan: $(ASAN_O) $(LINK_STAMP)
	$(CC) $(ASANFLAGS) $(filter %.o,$^) -lm $(LINALG_LIBS) $(OPTIM_LIBS) -o $@

# Regression suite: golden-output tests in tests/*.test (see tests/run.sh),
# plus bytecode-disassembly goldens in tests/dis/ (see tests/run_dis.sh).
test: vmtest $(BIN)
	@bash tests/run.sh
	@bash tests/run_dis.sh
	@bash tests/run_manual.sh
	@bash tests/run_plot.sh
	@bash tests/run_examples.sh
	@bash tests/run_pkgdocs.sh
	@bash tests/run_ascii_plot.sh
	@bash tests/run_longline.sh
	@timeout 300 python3 tests/run_lineedit.py
	@bash tests/run_svg.sh
	@bash tests/run_demo.sh
	@sh tests/run_packeur.sh ./vmtest 2>/dev/null || sh tests/run_packeur.sh ./cozy
	@bash tests/run_io.sh
	@python3 tests/run_pager.py
	@CC="$(CC)" STD=$(STD) BACKEND=$(BACKEND) OPTIM=$(OPTIM) EMBED_LIBS="$(LINALG_LIBS) $(OPTIM_LIBS)" bash tests/run_embed.sh
	@python3 tests/run_doclint.py
	@python3 tools/check_release.py
	@python3 tools/gen_pkg_hints.py --check
	@python3 tools/gen_demo.py --check
	@python3 tools/gen_doc_table.py --check
	@python3 tools/gen_reference.py --check
	@python3 tools/gen_dsge_page.py --check
	@node tests/run_nowcast_page.js
	@node tests/run_playground_page.js
	@bash tests/run_emacs.sh

# Same corpus, every input run under AddressSanitizer/UBSan; fails on any leak.
test-asan: vmtest-asan
	@bash tests/run.sh --asan

# doc/pdf_glyphs.tex supplies the characters Latin Modern lacks (Greek,
# super/subscripts, a few math signs); without it they print as blanks.
PDF_GLYPHS = -H doc/pdf_glyphs.tex

# Regenerate MANUAL.pdf from MANUAL.md (needs pandoc + xelatex; the styling
# header is optional). On a full TeX Live / MacTeX install the stock template
# works as-is.
manual:
	pandoc MANUAL.md -o MANUAL.pdf --pdf-engine=xelatex --toc --toc-depth=2 $(PDF_GLYPHS) \
	  -V geometry:margin=2.4cm -V fontsize=10pt -V colorlinks=true

# All three books. The verdict is the exit code: never pipe these through
# a filter or silence them — 0.0.18 shipped four releases of stale PDFs
# behind a > /dev/null (LESSONS: the pandoc that failed in silence).
pdfs: manual
	pandoc BOOK.md -o BOOK.pdf --pdf-engine=xelatex --resource-path=.:docs \
	  --toc --toc-depth=1 $(PDF_GLYPHS) -V geometry:margin=2.4cm -V fontsize=10pt -V colorlinks=true
	pandoc PACKAGES.md -o PACKAGES.pdf --pdf-engine=xelatex --toc --toc-depth=1 $(PDF_GLYPHS) \
	  -V geometry:margin=2.4cm -V fontsize=10pt -V colorlinks=true

run:    $(BIN); ./$(BIN)
repl:   $(BIN); ./$(BIN)
sample: $(BIN); ./$(BIN) --sample
ast:    $(BIN); ./$(BIN) --ast
tokens: $(BIN); ./$(BIN) --tokens
clean:; rm -rf $(BIN) vmtest vmtest-asan build

# ---- install / uninstall (owner's ruling: ~/.local by default) ----------
# Installs the CLI, the workbench launcher (the GUI: cozy-workbench
# serves the browser UI and opens it), every runtime asset (packages,
# sample data, the docs bundle the workbench serves), and the man page.
# The binary finds installed assets through exe_dir()/../share/cozy —
# and a dev tree always wins, because exe-beside paths are tried first.
PREFIX ?= $(HOME)/.local
# make install autodetects the best configuration (the same probes as
# deploy.sh — Darwin: Accelerate; Linux: OpenBLAS by link test; both:
# nlopt.h, with the brew prefix on macOS) unless BACKEND= / OPTIM= were
# given explicitly. Plain `make` stays tier0: the charter's floor is
# untouched; install is where ambition lives. Hash-free probes: macOS
# ships GNU make 3.81. No clean first since 0.1.55: the configuration
# builds in its own object tree (build/obj-BACKEND-OPTIM) and the link
# stamp relinks, so your everyday tier0 tree survives an install.
ifeq ($(origin BACKEND),command line)
  INST_BACKEND = $(BACKEND)
else ifeq ($(shell uname -s),Darwin)
  INST_BACKEND = accelerate
else ifeq ($(shell echo 'int main(void){return 0;}' | $(CC) -xc - -lopenblas -o /dev/null 2>/dev/null && echo yes),yes)
  INST_BACKEND = openblas
else
  INST_BACKEND = tier0
endif
ifeq ($(origin OPTIM),command line)
  INST_OPTIM = $(OPTIM)
else
  INST_BREW := $(shell brew --prefix 2>/dev/null)
  INST_NLOPT_INC := $(if $(INST_BREW),-I$(INST_BREW)/include,)
  ifeq ($(shell $(CC) $(INST_NLOPT_INC) -E -xc -include nlopt.h /dev/null >/dev/null 2>&1 && echo yes),yes)
    INST_OPTIM = nlopt
  else
    INST_OPTIM = none
  endif
endif
# COMPARE.md sets Cozy beside NumPy and Octave. Its Cozy transcripts are in
# `make test`; this runs the other two sides (needs NumPy, SciPy and Octave,
# which is why it is not part of the suite: a missing one is announced).
compare:
	@python3 tools/compare_check.py

install:
	@echo "install: building BACKEND=$(INST_BACKEND) OPTIM=$(INST_OPTIM)"
	@if [ "$(INST_BACKEND)" = tier0 ]; then echo "install: NOTE no OpenBLAS found (sudo apt install libopenblas-dev for LAPACK speed)"; fi
	@if [ "$(INST_OPTIM)" = none ]; then echo "install: NOTE no NLopt found (libnlopt-dev / brew install nlopt for the professional optimizer)"; fi
	$(MAKE) BACKEND=$(INST_BACKEND) OPTIM=$(INST_OPTIM) $(BIN)
	$(MAKE) BACKEND=$(INST_BACKEND) OPTIM=$(INST_OPTIM) install-files

# the books `manual` pages and the workbench's Docs pane serves; both look
# in ../share/cozy beside the binary, so an installed cozy needs them there
BOOKS = MANUAL.md BOOK.md PACKAGES.md CHANGELOG.md LESSONS.md DESIGN_NOTES.md README.md
install-files: $(BIN)
	install -d $(PREFIX)/bin $(PREFIX)/share/cozy $(PREFIX)/share/man/man1
	install -m 755 $(BIN) $(PREFIX)/bin/cozy
	install -m 755 tools/cozy-workbench $(PREFIX)/bin/cozy-workbench
	rm -rf $(PREFIX)/share/cozy/packages $(PREFIX)/share/cozy/data $(PREFIX)/share/cozy/docs
	cp -r packages data docs $(PREFIX)/share/cozy/
	install -m 644 $(BOOKS) $(PREFIX)/share/cozy/
	install -m 644 doc/cozy.1 $(PREFIX)/share/man/man1/cozy.1
	@echo "installed: $(PREFIX)/bin/cozy, cozy-workbench, share/cozy/{packages,data,docs,*.md}, man1/cozy.1"
	@echo "note: ensure $(PREFIX)/bin is on PATH and $(PREFIX)/share/man on MANPATH (usually automatic)"

uninstall:
	rm -f  $(PREFIX)/bin/cozy $(PREFIX)/bin/cozy-workbench
	rm -rf $(PREFIX)/share/cozy
	rm -f  $(PREFIX)/share/man/man1/cozy.1
	@echo "uninstalled from $(PREFIX)"

# WebAssembly browser build: compiles the interpreter to a single self-contained
# docs/cozy.js (the .wasm is embedded as base64 via SINGLE_FILE, so there is
# no separate file to fetch and GitHub Pages needs no configuration). Requires
# Emscripten (emcc) on PATH; a current emsdk needs no extra flags. The prebuilt
# docs/cozy.js is committed, so this target is only needed after changing the
# interpreter. wasm_api.c is the string-in/string-out driver (nu_init/nu_eval).
#
# EMCC_C23 is empty for a current emsdk (full C23). Only the older Ubuntu-
# packaged emscripten 3.1.6 (clang-15, partial C23) needs polyfills; for that,
# build with:  make wasm EMCC_C23="-Dnullptr=NULL '-Dalignof(x)=_Alignof(x)' -Dstatic_assert=_Static_assert"
EMCC       ?= emcc
EMCC_C23   ?=
WASM_SRCS   = lexer.c arena.c ast.c parser.c value.c eval.c chunk.c compile.c vm.c wasm_api.c sparse.c kalman.c linalg_tier0.c \
              repl.c lineedit.c third_party/isocline/src/isocline.c
WASM_FLAGS  = -sMODULARIZE=1 -sEXPORT_NAME=Cozy -sALLOW_MEMORY_GROWTH=1 \
              -sSUPPORT_LONGJMP=1 -sENVIRONMENT=web,worker -sSINGLE_FILE=1 -sASYNCIFY=1 \
              -sEXPORTED_FUNCTIONS=_cozy_init,_cozy_eval,_cozy_version,_cozy_set_matrix,_cozy_get_matrix,_cozy_get_dims,_cozy_repl,_cozy_term_active,_malloc,_free \
              -sEXPORTED_RUNTIME_METHODS=cwrap,ccall,UTF8ToString,stringToUTF8,lengthBytesUTF8,FS,HEAPF64,HEAP32
wasm: $(WASM_SRCS) $(HDRS) wasm_api.c version.h
	$(EMCC) -std=gnu2x -O2 $(EMCC_C23) $(ISOCLINE_INC) $(WASM_FLAGS) $(WASM_OPTIM_FLAGS) \
	  --embed-file packages --embed-file data --embed-file MANUAL.md --embed-file PACKAGES.md --embed-file BOOK.md \
	  --embed-file CHANGELOG.md --embed-file LESSONS.md --embed-file DESIGN_NOTES.md \
	  $(WASM_SRCS) $(WASM_OPTIM_SRCS) -o docs/cozy.js  # gnu2x: EM_ASM needs GNU extensions; docs+packages ride in the bundle
	@python3 tools/gen_dsge_page.py
	@echo "built docs/cozy.js ($$(wc -c < docs/cozy.js) bytes) — commit and push to update GitHub Pages"

# wasm-ubuntu: the whole recipe for a stock Ubuntu 24 container (see
# PLAYBOOK "The Ubuntu emscripten recipe"): distro emscripten 3.1.6 pins
# clang-15, whose gnu2x predates four C23 spellings — the shims below map
# them to the C11 forms; NODE_PATH finds the distro's acorn for the JS
# minifier. A modern emsdk needs none of this: plain `make wasm`.
wasm-ubuntu:
	NODE_PATH=/usr/share/nodejs $(MAKE) wasm EMCC_C23='-Dnullptr=NULL -Dalignof=_Alignof -Dtypeof=__typeof__ -Dstatic_assert=_Static_assert'

# The workout: heavier than the rite; run before big releases and after
# backend work. make stress (tier0) or make BACKEND=openblas stress.
stress: $(BIN) vmtest-asan
	bash stress/run.sh

# The wasm rite: compile vmtest itself to wasm for node (NODERAWFS reads the
# repo's files; SINGLE_FILE sidesteps emscripten-3.1's fetch() path under
# modern node) and run the whole golden suite through it. Native-only
# goldens (first line "# native-only") are skipped. Browser behavior is now
# conformance-tested, not assumed.
# NLopt in the browser (audit item 7, half one): the vendored C subset in
# third_party/nlopt compiles into both the web bundle and the wasm vmtest,
# so buildinfo().optim says "nlopt" in the tab and the optim package
# dispatches to SLSQP there too. WASM_OPTIM=none keeps the pure path.
WASM_OPTIM ?= nlopt
NLOPT_DIR   = third_party/nlopt
NLOPT_ALGS  = $(wildcard $(NLOPT_DIR)/algs/*/*.c)
NLOPT_API   = $(NLOPT_DIR)/api/general.c $(NLOPT_DIR)/api/optimize.c $(NLOPT_DIR)/api/options.c $(NLOPT_DIR)/api/deprecated.c
NLOPT_UTIL  = $(NLOPT_DIR)/util/mt19937ar.c $(NLOPT_DIR)/util/qsort_r.c $(NLOPT_DIR)/util/redblack.c \
              $(NLOPT_DIR)/util/rescale.c $(NLOPT_DIR)/util/sobolseq.c $(NLOPT_DIR)/util/stop.c $(NLOPT_DIR)/util/timer.c
NLOPT_INCS  = -I$(NLOPT_DIR) -I$(NLOPT_DIR)/api -I$(NLOPT_DIR)/util $(patsubst %/,-I%,$(dir $(NLOPT_ALGS)))
ifeq ($(WASM_OPTIM),nlopt)
  WASM_OPTIM_FLAGS = -DCOZY_NLOPT -DNLOPT_LUKSAN $(NLOPT_INCS) -Wno-unused-but-set-variable
  WASM_OPTIM_SRCS  = $(NLOPT_ALGS) $(NLOPT_API) $(NLOPT_UTIL)
else
  WASM_OPTIM_FLAGS =
  WASM_OPTIM_SRCS  =
endif
WASM_TEST_SRCS = lexer.c arena.c ast.c parser.c value.c eval.c chunk.c compile.c vm.c sparse.c kalman.c linalg_tier0.c wasm_test_stub.c vmtest.c
build/vmtest-wasm.js: $(WASM_TEST_SRCS) $(HDRS) version.h
	@mkdir -p build
	$(EMCC) -std=gnu2x -O2 $(EMCC_C23) $(WASM_OPTIM_FLAGS) -sSUPPORT_LONGJMP=1 -sALLOW_MEMORY_GROWTH=1 \
	  -sENVIRONMENT=node -sNODERAWFS=1 -sEXIT_RUNTIME=1 -sSINGLE_FILE=1 \
	  $(WASM_TEST_SRCS) $(WASM_OPTIM_SRCS) -o $@
test-wasm: build/vmtest-wasm.js
	@printf '#!/bin/sh\nexec node "$$(dirname "$$0")/vmtest-wasm.js" "$$@"\n' > build/vmtest-wasm && chmod +x build/vmtest-wasm
	@VMTEST=./build/vmtest-wasm VMTEST_WASM=1 bash tests/run.sh
test-wasm-ubuntu:
	NODE_PATH=/usr/share/nodejs $(MAKE) test-wasm EMCC_C23='-Dnullptr=NULL -Dalignof=_Alignof -Dtypeof=__typeof__ -Dstatic_assert=_Static_assert'

# wasm via the pinned toolchain image (tools/wasm.Dockerfile) — immune to
# distro drift; build the image once: docker build -t cozy-wasm -f tools/wasm.Dockerfile .
wasm-docker:
	docker run --rm -u "$$(id -u)" -v "$$PWD":/src -w /src cozy-wasm make wasm-ubuntu

.PHONY: lib run repl sample ast tokens clean test test-asan wasm wasm-ubuntu wasm-docker test-wasm test-wasm-ubuntu manual pdfs stress install install-files uninstall
