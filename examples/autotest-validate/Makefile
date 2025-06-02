SRC := autotest-validate.c autotest-validate-main.c
OBJS := $(SRC:.c=.o)
TARGET ?= autotest-validate

all: $(TARGET)

$(TARGET) : $(OBJS)
	$(CC) $(CFLAGS) $(INCLUDES) $(OBJS) -o $(TARGET) $(LDFLAGS)

clean:
	-rm -f *.o $(TARGET) *.elf *.map
