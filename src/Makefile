CFLAGS = -std=c99 -pedantic -Wall -Wextra -Werror
LDFLAGS = -lwiringPi

bit-test: eeprom-bit-test.c
	gcc $(CFLAGS) eeprom-bit-test.c -o bit-test $(LDFLAGS)


read-write-test: eeprom-read-write-test.c
	gcc $(CFLAGS) eeprom-read-write-test.c -o read-write-test $(LDFLAGS)
