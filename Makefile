
CC := gcc
CFLAGS	:= -std=c99 -pedantic -Wall -Wextra -Werror
LDFLAGS	:= -l wiringPi

SRCS := eeprom-bit-test.c
TESTS := eeprom-connection-test.c

all: src/$(SRCS)
	$(CC) $(CFLAGS) src/$(SRCS) -o build/eeprom-bit-test $(LDFLAGS)

test: tests/eeprom-connection-test.c
	$(CC) $(CFLAGS) tests/$(TESTS) -o build/eeprom-conection-test $(LDFLAGS)

clean:
	rm build/*
