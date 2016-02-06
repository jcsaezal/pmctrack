CC ?= gcc
#To build for 32-bit system run: 'make ARCH=-m32'
ARCH :=
LIBPMCTRACK_DIR=../../lib/libpmctrack
CFLAGS=$(ARCH) -DUSE_VFORK -Wall -g -I ../../modules/pmcs/include/pmc -I$(LIBPMCTRACK_DIR)/include
LDFLAGS=$(ARCH) -L$(LIBPMCTRACK_DIR) -lpmctrack -static
#LDFLAGS=-lrt 
PROG=../../../bin/pmctrack
OBJPROG=pmctrack.o

# Para depurar usar: make debug=1
ifeq ($(debug),1)
 CFLAGS += -DDEBUG
endif

all: $(PROG)

$(PROG): $(OBJPROG)
	$(CC) -o $@ $^ $(LDFLAGS)

clean:
	-rm -f $(PROG) *~ *.o
