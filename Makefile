# Citadel libc build.
#
#   make          build lib/libc.a and lib/crt1.o
#   make check    build and run the test suite, linking every test three
#                 ways: static-PIE, static-PIE with RELR relocations, static
#   make clean

CC      ?= cc
AR      ?= ar
CC_INC  := $(shell $(CC) -print-file-name=include)

# Library sources are built freestanding so the compiler never turns a
# loop inside memcpy/memset into a call to itself.
CPPFLAGS_LIB = -nostdinc -isystem include -isystem $(CC_INC) -Isrc -D__CITADEL_BUILDING__
CFLAGS_LIB   = -std=c11 -O2 -g -fPIE -ffreestanding -fno-builtin -fno-strict-aliasing \
               -fno-asynchronous-unwind-tables -fstack-protector-strong \
               -Wall -Wextra -Werror -Wno-unused-parameter
ifneq ($(shell $(CC) -v 2>&1 | grep -c '^gcc version'),0)
CFLAGS_LIB  += -fno-tree-loop-distribute-patterns
endif

# Everything reached from _start before the TCB is installed must not use
# the stack protector (%fs is not valid yet).
NOSSP = src/start.c

LIB_C   = $(filter-out src/crt1.c,$(wildcard src/*.c))
LIB_S   = $(filter-out src/arch/crt1.S,$(wildcard src/arch/*.S))
LIB_OBJ = $(patsubst src/%.c,obj/%.o,$(LIB_C)) $(patsubst src/%.S,obj/%.o,$(LIB_S))

all: lib/libc.a lib/crt1.o

obj/%.o: src/%.c src/internal.h $(wildcard include/*.h include/*/*.h)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS_LIB) $(CFLAGS_LIB) $(if $(filter $<,$(NOSSP)),-fno-stack-protector) -c -o $@ $<

obj/%.o: src/%.S
	@mkdir -p $(dir $@)
	$(CC) -fPIE -c -o $@ $<

lib/crt1.o: obj/arch/crt1.o
	@mkdir -p lib
	cp $< $@

lib/libc.a: $(LIB_OBJ)
	@mkdir -p lib
	rm -f $@
	$(AR) rcs $@ $^

# ---- tests ------------------------------------------------------------
# Test programs are ordinary hosted C compiled against our headers and
# linked only against our crt1.o and libc.a.
TEST_SRC   = $(wildcard test/*.c)
TEST_NAMES = $(filter-out harness,$(patsubst test/%.c,%,$(TEST_SRC)))
CPPFLAGS_T = -nostdinc -isystem include -isystem $(CC_INC) -Itest
CFLAGS_T   = -std=gnu11 -O2 -g -fPIE -fstack-protector-strong -Wall -Wextra -Werror \
             -Wno-unused-parameter -fno-builtin
# GCC folds printf return values at compile time; tests must see ours.
ifneq ($(shell $(CC) -v 2>&1 | grep -c '^gcc version'),0)
CFLAGS_T   += -fno-printf-return-value
endif
LDFLAGS_T  = -nostdlib -Wl,-z,relro,-z,now -Wl,-z,noexecstack
LIBGCC     := $(shell $(CC) -print-libgcc-file-name)

PIE_BINS    = $(patsubst %,test/bin/pie/%,$(TEST_NAMES))
RELR_BINS   = $(patsubst %,test/bin/relr/%,$(TEST_NAMES))
STATIC_BINS = $(patsubst %,test/bin/static/%,$(TEST_NAMES))

test/bin/pie/%: test/%.c test/harness.h lib/libc.a lib/crt1.o
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS_T) $(CFLAGS_T) $(LDFLAGS_T) -static-pie -o $@ lib/crt1.o $< lib/libc.a $(LIBGCC)

test/bin/relr/%: test/%.c test/harness.h lib/libc.a lib/crt1.o
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS_T) $(CFLAGS_T) $(LDFLAGS_T) -Wl,-z,pack-relative-relocs -static-pie -o $@ lib/crt1.o $< lib/libc.a $(LIBGCC)

test/bin/static/%: test/%.c test/harness.h lib/libc.a lib/crt1.o
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS_T) $(CFLAGS_T) $(LDFLAGS_T) -static -o $@ lib/crt1.o $< lib/libc.a $(LIBGCC)

check: $(PIE_BINS) $(RELR_BINS) $(STATIC_BINS)
	@./test/run.sh $^

clean:
	rm -rf obj lib test/bin

.PHONY: all check clean
.SECONDARY:
