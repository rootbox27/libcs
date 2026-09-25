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

# Floating-point code must be evaluated exactly as written.
CFLAGS_MATH = -ffp-contract=off -fno-fast-math -frounding-math

# OpenBSD's malloc is kept close to upstream rather than to our warning set.
CFLAGS_OMALLOC = -Wno-sign-compare -Wno-unused-function -Wno-unused-variable -Wno-empty-body -Wno-maybe-uninitialized

# Everything reached from _start before the TCB is installed must not use
# the stack protector (%fs is not valid yet).
NOSSP = src/start.c

LIB_C   = $(filter-out src/crt1.c,$(wildcard src/*.c src/math/*.c src/omalloc/*.c))
LIB_S   = $(filter-out src/arch/crt1.S,$(wildcard src/arch/*.S))
LIB_OBJ = $(patsubst src/%.c,obj/%.o,$(LIB_C)) $(patsubst src/%.S,obj/%.o,$(LIB_S))

all: lib/libc.a lib/crt1.o

# -MMD: the compiler records every header each object uses (obj/*.d).
obj/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS_LIB) $(CFLAGS_LIB) -MMD -MP $(if $(filter $<,$(NOSSP)),-fno-stack-protector) $(if $(filter src/math/%,$<),$(CFLAGS_MATH)) $(if $(filter src/omalloc/%,$<),$(CFLAGS_OMALLOC)) -c -o $@ $<

-include $(wildcard obj/*.d obj/*/*.d)

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

TEST_DEPS = test/harness.h lib/libc.a lib/crt1.o $(wildcard include/*.h include/*/*.h)

test/bin/pie/%: test/%.c $(TEST_DEPS)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS_T) $(CFLAGS_T) $(LDFLAGS_T) -static-pie -o $@ lib/crt1.o $< lib/libc.a $(LIBGCC)

test/bin/relr/%: test/%.c $(TEST_DEPS)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS_T) $(CFLAGS_T) $(LDFLAGS_T) -Wl,-z,pack-relative-relocs -static-pie -o $@ lib/crt1.o $< lib/libc.a $(LIBGCC)

test/bin/static/%: test/%.c $(TEST_DEPS)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS_T) $(CFLAGS_T) $(LDFLAGS_T) -static -o $@ lib/crt1.o $< lib/libc.a $(LIBGCC)

check: $(PIE_BINS) $(RELR_BINS) $(STATIC_BINS)
	@./test/run.sh $^

clean:
	rm -rf obj lib test/bin

.PHONY: all check clean
.SECONDARY:
