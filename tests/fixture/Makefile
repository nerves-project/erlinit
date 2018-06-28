
LDFLAGS += -fPIC -shared -pedantic -ldl
CFLAGS ?= -fPIC -O2 -Wall -Wextra -Wno-unused-parameter

TARGET=erlinit_fixture.so

SRC=$(wildcard *.c)
OBJ=$(SRC:.c=.o)

all: $(TARGET)

$(OBJ): $(wildcard *.h)

%.o: %.c
	$(CC) -c $(CFLAGS) -o $@ $<

$(TARGET): $(OBJ)
	$(CC) $^ $(LDFLAGS) -o $@

clean:
	$(RM) $(TARGET) $(OBJ)

.PHONY: all clean
