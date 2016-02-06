CC = g++
ARCH:=
LIBPMCTRACK_DIR=../../../src/lib/libpmctrack
CPPFLAGS=$(ARCH) -Wall -g -I ../../../src/modules/pmcs/include/pmc -I$(LIBPMCTRACK_DIR)/include
LDFLAGS=$(ARCH) -L$(LIBPMCTRACK_DIR) -lpmctrack 
PROG=benchmark_cache_heap
OBJPROG=benchmark_cache_heap.o

all: $(PROG)

$(PROG): $(OBJPROG)
	$(CC) -o $@ $^ $(LDFLAGS) 

clean:
	-rm -f $(PROG) *~ *.o
