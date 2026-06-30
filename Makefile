# Pure Makefile for htmlview — app-specific X11/Xft/XRender/Imlib2 HTML renderer

CXX      ?= g++
PKGS     := x11 xft xrender imlib2 fontconfig
CXXFLAGS ?= -std=c++17 -O2 -g -Wall -Wextra
CXXFLAGS += $(shell pkg-config --cflags $(PKGS))
LDFLAGS  += $(shell pkg-config --libs $(PKGS))

SRCDIR   := src
OBJDIR   := build
BIN      := htmlview

SRCS := $(wildcard $(SRCDIR)/*.cpp)
OBJS := $(patsubst $(SRCDIR)/%.cpp,$(OBJDIR)/%.o,$(SRCS))
DEPS := $(OBJS:.o=.d)

.PHONY: all clean run test test-update

all: $(BIN)

$(BIN): $(OBJS)
	$(CXX) $(OBJS) -o $@ $(LDFLAGS)

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
