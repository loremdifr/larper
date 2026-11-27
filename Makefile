ifeq ($(OS),Windows_NT)
    CC = mingw64\bin\gcc
    EXT := .exe
    RM := cmd /C del /Q /F
    DS := \\
else
    CC = clang
    EXT :=
    RM := rm -f
    DS := /
endif

SRC_NAME  := $(addprefix $(CURDIR)/, main.c)
BUILD_DIR := $(addprefix $(CURDIR)/, build)
EXEC_NAME  = $(BUILD_DIR)$(DS)larper$(EXT)
INCLUDES_DIR := $(addprefix $(CURDIR)/, ../..)

CFLAGS = -fdiagnostics-color=always -W -Werror -Wextra -pedantic -g -std=gnu23 -Wno-missing-field-initializers
CFLAGS+= -I$(INCLUDES_DIR)

LD_LIBS = -lm

## generate version from git tag

VERSION_H   := $(CURDIR)/src/version.h
GIT_VERSION := $(shell git describe --tags --always --dirty 2>/dev/null || echo "dev")

$(VERSION_H): FORCE
	@mkdir -p $(CURDIR)
	@printf '%s\n'                                            \
			'#ifndef _VERSION_H'                              \
			'#define _VERSION_H'                              \
			'static const char VERSION[] = "$(GIT_VERSION)";' \
			'#endif //_VERSION_H' > $@

###############

clean:
	-$(RM) "$(EXEC_NAME)"

valgrind: clean
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -O0 $(SRC_NAME) -o $(EXEC_NAME) $(LD_LIBS)
	valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes ./$(EXEC_NAME) $(ARGS)

run: clean
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) $(SRC_NAME) -o $(EXEC_NAME) $(LD_LIBS)
ifeq ($(OS),Windows_NT)
	@cmd /C "$(EXEC_NAME) $(ARGS)"
else
	@$(EXEC_NAME) $(ARGS)
endif

#used for syntax checking
check:
	$(CC) -fsyntax-only $(CFLAGS) $(SRC_NAME)

gf2: clean
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) $(SRC_NAME) -o $(EXEC_NAME) $(LD_LIBS)
	gf2 --args ./$(EXEC_NAME) $(ARGS)

#######################################
#######################################

###### RELEASE STUFF
RELEASE_CFLAGS := -W -Werror -Wextra -pedantic -std=gnu23 -O3
RELEASE_CFLAGS += -DNDEBUG -flto -fno-plt -fstack-protector-strong
RELEASE_CFLAGS += -D_FORTIFY_SOURCE=2 -I$(INCLUDES_DIR)
RELEASE_LD_LIBS := -lm -flto
RELEASE_GO_LDFLAGS := -s -w

###### release is the local arch!
release: $(VERSION_H)
release: CFLAGS  := $(RELEASE_CFLAGS)
release: LD_LIBS := $(RELEASE_LD_LIBS)
release: GO_NAME := $(BUILD_DIR)$(DS)importer$(EXT)
release: clean
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) $(SRC_NAME) -o $(EXEC_NAME) $(LD_LIBS)
	go build -C importer -trimpath -ldflags="$(RELEASE_GO_LDFLAGS)" -o $(GO_NAME) .

###### package stuff (all archs in release mode)
VERSION    := $(shell git describe --tags --always 2>/dev/null || echo dev)
PKG_BASE   := $(BUILD_DIR)$(DS)pkg
DIST_DIR   := $(CURDIR)$(DS)dist

CLANG_AMD64 := clang --target=x86_64-linux-gnu
CLANG_ARM64 := clang --target=aarch64-linux-gnu

package: clean
	@mkdir -p "$(DIST_DIR)"
	$(MAKE) package_for_arch ARCH=amd64 CC_PKG="$(CLANG_AMD64)" GOARCH_PKG=amd64
	$(MAKE) package_for_arch ARCH=arm64 CC_PKG="$(CLANG_ARM64)" GOARCH_PKG=arm64

package_for_arch:
	@mkdir -p "$(PKG_BASE)$(DS)$(ARCH)"
	$(CC_PKG) $(RELEASE_CFLAGS) $(SRC_NAME) -o "$(PKG_BASE)$(DS)$(ARCH)$(DS)larper" $(RELEASE_LD_LIBS)
	GOOS=linux GOARCH=$(GOARCH_PKG) go build -C importer -trimpath -ldflags="$(RELEASE_GO_LDFLAGS)" -o "$(PKG_BASE)$(DS)$(ARCH)$(DS)importer" .
	tar czf "$(DIST_DIR)$(DS)larper-$(VERSION)-linux-$(ARCH).tar.gz" -C "$(PKG_BASE)$(DS)$(ARCH)" .

# phony is so that it skips checking the commands as if they
# were files to avoid recompilation. none of those commands are files
# and since we're using a unity build we don't really need separate translation
# units
.PHONY: run valgrind gf2 check clean all release package package_for_arch FORCE