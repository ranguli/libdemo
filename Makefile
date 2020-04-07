SILENT	?= @
VERSION	 = 0.3

BINARY	 = libdemo.a

CC	 = gcc 
AR	 = ar

ARFLAGS	 = cr

ifeq ($(DEBUG),YES)
CFLAGS	+= -g -O0
else
endif

OBJ	 = demo.o

OBJS	 = $(addprefix $(OBJDIR)/,$(OBJ))

HEADERS	 = $(INCDIR)/demo.h
DEPS	 = Makefile

OBJDIR	 = obj
SRCDIR	 = src
INCDIR	 = inc

default: all

#
# build targets
#

.PHONY: all clean

all: $(BINARY)

$(BINARY): $(OBJS)
	@echo "Archiving $(OBJS) => $@"
	$(SILENT)$(AR) $(ARFLAGS) $@ $(OBJS)

$(OBJS): $(HEADERS) $(DEPS) | $(OBJDIR)

$(OBJDIR):
	$(SILENT)mkdir $@

$(OBJDIR)/%.o: $(SRCDIR)/%.c
	@echo "Compiling $< => $@"
	$(SILENT)$(CC) -c -I$(INCDIR) $(CFLAGS) $< -o $@

clean:
	$(SILENT)rm -fr $(OBJDIR) $(BINARY)
