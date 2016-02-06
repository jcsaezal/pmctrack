CC = g++
ARCH:=
LIBPMCTRACK_DIR=../../../src/lib/libpmctrack
CPPFLAGS=$(ARCH) -std=c++11 -Wall -g -I ../../../src/modules/pmcs/include/pmc -I$(LIBPMCTRACK_DIR)/include
LDFLAGS=$(ARCH) -L$(LIBPMCTRACK_DIR) -lpmctrack
PROG=benchmark_cache_fibheap
OBJPROG=benchmark_cache_fibheap.o

all: $(PROG)

$(PROG): $(OBJPROG)
	$(CC) -o $@ $^ $(LDFLAGS)

clean:
	-rm -f $(PROG) *~ *.o
