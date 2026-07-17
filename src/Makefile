CC = gcc
CFLAGS = -std=c99 -pedantic -Wall -Wextra -Werror
LDFLAGS = -lwiringPi

bit-test: eeprom-bit-test.c
	$(CC) $(CFLAGS) eeprom-bit-test.c -o bit-test $(LDFLAGS)

read-write-test: eeprom-read-write-test.c
	$(CC) $(CFLAGS) eeprom-read-write-test.c -o read-write-test $(LDFLAGS)

serial-number-read: eeprom-serial-number-read.c
	$(CC) $(CFLAGS) eeprom-serial-number-read.c -o serial-number-read $(LDFLAGS)
