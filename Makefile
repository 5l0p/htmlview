# Pure Makefile for htmlview — app-specific X11/Xft/XRender/Imlib2 HTML renderer

CXX      ?= g++
PKGS     := x11 xft xrender imlib2 fontconfig
CXXFLAGS ?= -std=c++17 -O2 -g -Wall -Wextra
CXXFLAGS += $(shell pkg-config --cflags $(PKGS))
LDFLAGS  += $(shell pkg-config --libs $(PKGS))

SRCDIR   := src
OBJDIR   := build
BIN      := htmlview

# Static linking pulls in the X11/Imlib2/fontconfig dependency chain by hand,
# since pkg-config --static doesn't resolve link order across these libs and
# `ld` needs a --start-group to settle their circular references.
STATIC_LIBS := -lXft -lfontconfig -lfreetype -lImlib2 -lXrender -lX11 -lX11-xcb \
               -lXext -lxcb -lxcb-shm -lXau -lXdmcp -lfribidi -lz -lbz2 -lpng16 \
               -lexpat -lbrotlidec -lbrotlicommon -lm -lpthread

SRCS := $(wildcard $(SRCDIR)/*.cpp)
OBJS := $(patsubst $(SRCDIR)/%.cpp,$(OBJDIR)/%.o,$(SRCS))
DEPS := $(OBJS:.o=.d)

.PHONY: all clean run test test-update static

all: $(BIN)

$(BIN): $(OBJS)
	$(CXX) $(OBJS) -o $@ $(LDFLAGS)

static: $(OBJS)
	$(CXX) $(OBJS) -o $(BIN) -static -Wl,--start-group $(STATIC_LIBS) -Wl,--end-group

$(OBJDIR)/%.o: $(SRCDIR)/%.cpp | $(OBJDIR)
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

$(OBJDIR):
	mkdir -p $(OBJDIR)

run: $(BIN)
	./$(BIN) test/kitchen-sink.html

test: $(BIN)
	./test/run.sh

test-update: $(BIN)
	UPDATE=1 ./test/run.sh

clean:
	rm -rf $(OBJDIR) $(BIN)

-include $(DEPS)
