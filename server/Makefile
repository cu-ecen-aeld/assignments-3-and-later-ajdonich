SRC := connthread.c aesdsocket.c 
OBJS := $(SRC:.c=.o)
TARGET ?= aesdsocket
CFLAGS ?= -g -Wall -Werror

ifdef CROSS_COMPILE
CC ?= $(CROSS_COMPILE)gcc
$(info CC=$(shell which $(CC)))
endif

all: $(TARGET)

$(TARGET) : $(OBJS)
	$(CC) $(CFLAGS) $(INCLUDES) $(OBJS) -o $(TARGET) $(LDFLAGS)

clean:
	-rm -f *.o $(TARGET) *.elf *.map
