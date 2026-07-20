TOP = $(CURDIR)
TMAKE_DIR = $(TOP)/make
include $(TMAKE_DIR)/tmake-init.mk

PROG = tvi
SRCS = $(wildcard src/*.c)
CFLAGS += -Iinclude

include $(TMAKE_DIR)/tmake-prog.mk

FILES = help.txt COPYING.txt README.md
FILESDIR = $(DOCDIR)/tvi
include $(TMAKE_DIR)/tmake-files.mk
