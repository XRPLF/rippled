
CC      ?= gcc
RM      ?= rm
CFLAGS  ?= -I. -I../db -Wall -pthread -O0 -g
LDFLAGS ?= -pthread -L../db -lsophia
all: clean i common recover merge crash limit
common: common.o
	$(CC) common.o $(LDFLAGS) -o common
recover: recover.o
	$(CC) recover.o $(LDFLAGS) -o recover
merge: merge.o
	$(CC) merge.o $(LDFLAGS) -o merge
crash: crash.o
	$(CC) crash.o $(LDFLAGS) -o crash
limit: limit.o
	$(CC) limit.o $(LDFLAGS) -o limit
i: i.o
	$(CC) i.o $(LDFLAGS) -o i
.c.o:
	$(CC) $(CFLAGS) -c $<
clean:
	$(RM) -f common.o common recover.o recover merge.o merge
	$(RM) -f crash.o crash i.o i limit.o limit
test:
	./i
	./common
	./recover
	./merge
	./crash
	./limit
