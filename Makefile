CFLAGS := -std=c11 -D_ISOC11_SOURCE -D_POSIX_C_SOURCE=200809L \
    -fno-signed-char -fno-strict-aliasing

ifeq ($(DEBUG), 1)
    CFLAGS += -g3 -DDEBUG -O0
else
    CFLAGS += -fomit-frame-pointer -flto -DNDEBUG -O2
endif

ifeq ($(32BIT), 1)
    CFLAGS += -m32
    LDFLAGS := -m32
else
    CFLAGS += -m64
    LDFLAGS := -m64
endif

CFLAGS += -Wall -Werror -Wno-uninitialized
CC_IS_CLANG := $(findstring clang, $(shell $(CC) --version))

ifdef CC_IS_CLANG
    CFLAGS += -Wextra -pedantic \
        -Wsign-conversion \
        -Wconversion \
        -Wshadow \
        -Wno-gnu-designator \
        -Wno-gnu-conditional-omitted-operand \
        -Wno-gnu-statement-expression \
        -Wno-gnu-zero-variadic-macro-arguments
endif

SRCDIR := ./src
OUTDIR := ./build/make
OBJDIR := $(OUTDIR)/obj
EXNAME := $(OUTDIR)/quaint
DEPFILE := $(OUTDIR)/.deps

SRCS := $(wildcard $(SRCDIR)/*.c)
OBJS := $(addprefix $(OBJDIR)/, $(notdir $(SRCS:.c=.o)))

all: $(EXNAME)

depend: $(DEPFILE)

$(DEPFILE): $(SRCS)
	@mkdir -p $(OUTDIR)
	$(CC) $(CFLAGS) -MM $^ | sed -E 's|([a-z]+)\.o|$(OBJDIR)/\1\.o|' > $(DEPFILE)

-include $(DEPFILE)

$(OBJS): $(OBJDIR)/%.o : $(SRCDIR)/%.c
	@mkdir -p $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(EXNAME): $(OBJS)
	$(CC) -o $(EXNAME) $^ $(LDFLAGS)

.PHONY: clean

clean:
	rm -rf $(OUTDIR)
