
CC      ?= gcc
RM      ?= rm
CFLAGS  ?= -I. -std=c99 -pedantic -Wextra -Wall -pthread -O2 -DNDEBUG
LDFLAGS ?=
OBJECTS = file.o \
		  crc.o \
		  e.o \
		  i.o \
		  cat.o \
		  rep.o \
		  util.o \
		  sp.o \
		  recover.o \
		  merge.o \
		  gc.o \
		  cursor.o

TARGET  = libsophia.a

$(TARGET): clean $(OBJECTS)
	$(AR) cru $(TARGET) $(OBJECTS)
.c.o:
	$(CC) $(CFLAGS) -c $<
clean:
	$(RM) -f $(OBJECTS) $(TARGET)
