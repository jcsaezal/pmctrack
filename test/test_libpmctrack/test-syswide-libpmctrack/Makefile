CC = gcc
ARCH:=
LIBPMCTRACK_DIR=../../../src/lib/libpmctrack
CFLAGS=$(ARCH) -Wall -g -I ../../../src/modules/pmcs/include/pmc -I$(LIBPMCTRACK_DIR)/include
LDFLAGS=$(ARCH) -L$(LIBPMCTRACK_DIR) -lpmctrack 
PROG=test-syswide
OBJPROG=$(PROG).o

all: $(PROG)

$(PROG): $(OBJPROG)
	$(CC) -o $@ $^ $(LDFLAGS) 

clean:
	-rm -f $(PROG) *~ *.o
