TARGET1=../libpmctrack.so
TARGET2=../libpmctrack.a
SOURCES=core.c pmu_info.c 
OBJECTS=$(patsubst %.c,%.o,$(SOURCES))
HEADERS=$(wildcard ../include/*.h)
#To build for 32-bit system run: 'make ARCH=-m32'
ARCH :=
CFLAGS=$(ARCH) -Wall -g -fpic -I ../include -I ../../../modules/pmcs/include/pmc
LDFLAGS=$(ARCH)
CC = gcc


# Para depurar usar: make debug=1
ifeq ($(debug),1)
 CFLAGS += -DDEBUG
endif

all: $(TARGET1) $(TARGET2)

$(TARGET1): $(OBJECTS)
	$(CC) -shared $(LDFLAGS) -o $(TARGET1) $(OBJECTS)

$(TARGET2): $(OBJECTS)
	ar rcs $(TARGET2) $(OBJECTS) 

.SUFFIXES:      .o .c .h

.h:

.c.o: $(HEADERS)
	$(CC) -c $(CFLAGS) -o $@ $^

clean:
	rm -f *.o
	rm -f $(TARGET1) $(TARGET2)
	rm -f  *~
	rm -f ../lib/*
