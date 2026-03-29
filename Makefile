# brocolli26/Makefile
#
# Extend the existing Kore template Makefile with these additions.
# Place this at the top of your existing Makefile (or merge the vars).
#
# Prerequisites (Ubuntu/Debian):
#   sudo apt install libseccomp-dev
#   # SQLite3 is vendored — no system package needed
#
# Usage:
#   make        — build brocolli.so (the Kore module)
#   make run    — start kore with the brocolli config
#   make clean  — remove build artefacts

# ---------- project name (must match conf/brocolli.conf module line) ----------
APPLICATION=	brocolli

# ---------- source files -------------------------------------------------------
SRCS=	src/brocolli.c       \
	src/sandbox.c            \
	src/workflow.c           \
	vendor/sqlite3/sqlite3.c

# ---------- compiler flags -----------------------------------------------------
CFLAGS+=	-std=c11 -D_GNU_SOURCE
CFLAGS+=	-Wall -Wextra -Wpedantic
CFLAGS+=	-Ivendor/sqlite3
CFLAGS+=	-O2 -g

# SQLITE_THREADSAFE=1 is the default but make it explicit
CFLAGS+=	-DSQLITE_THREADSAFE=1

# ---------- linker flags -------------------------------------------------------
#   -lseccomp   libseccomp (sandbox seccomp filter)
#   -lpthread   POSIX threads (workflow worker thread)
#   -ldl        required by SQLite3 amalgamation on some systems
LDFLAGS+=	-lseccomp -lpthread -ldl

# ---------- kore build system --------------------------------------------------
# If you use kodev (recommended):
#   kodev build
#
# If you call gcc directly (for reference):
#
#   gcc $(CFLAGS) -fPIC -shared -o $(APPLICATION).so $(SRCS) $(LDFLAGS)
#
# The lines below integrate with Kore's own Makefile include:
include $(KORE_DIR)/share/kore/build.mk
# If KORE_DIR is not set, find it with: kodev root

# ---------- convenience targets ------------------------------------------------
.PHONY: run clean

run: $(APPLICATION).so
	kore -c conf/brocolli.conf -f

clean:
	rm -f $(APPLICATION).so *.o src/*.o vendor/sqlite3/*.o
