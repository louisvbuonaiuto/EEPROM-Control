/*

functions for readings and writing data to an EEPROM

*/
#ifndef EEPROM_I2C_H
#define EEPROM_I2C_H

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <wiringPi.h> // for delay()

// ansi color macros
#define ANSI_COLOR_RESET "\x1b[0m"
#define ANSI_COLOR_RED "\x1b[31m"
#define ANSI_COLOR_GREEN "\x1b[32m"

#define SERIAL_NUMBER_LEN 16 // length of EEPROM serial number in bytes

// number of times to retry a restore-write + verify-read before
// concluding a bit is truly stuck (hard fault) rather than a transient
// re-hit or a bus/read hiccup.
// ADDED: this constant backs the retry logic requested in the logger()
// pseudocode comment ("do this next step 3 times to verify...").
#define RESTORE_VERIFY_RETRIES 3

typedef struct {
	// structure for a single bit in an EEPROM's memory, used to track
	// its flip history over the course of the test.
	// only bits that have flipped at least once get a BitRecord --
	// this keeps memory usage proportional to actual damage rather
	// than allocating a record for every bit in the device up front.
	size_t byteOffset;
	size_t bitIndex;
	bool hardFault;			// true once write-back+read-back verification fails
	size_t timesFlipped;	// ADDED: total flip events observed at this bit (soft errors only)
	time_t firstFlipTime;	// ADDED: elapsed test time (sec) at first observed flip
	time_t lastFlipTime;	// ADDED: elapsed test time (sec) at most recent observed flip
} BitRecord;

typedef struct {
	int size;					// size in bytes of EEPROM
	size_t numBitFlips;			// number of soft bit-flip occurrences (not counting hard faults)
	size_t numHardFaultBits;	// number of bits confirmed stuck (hard fault)
	size_t maxHardFaultBits;	// size*8, used to bound/sanity-check hard fault reporting
	size_t oneToZeroFlips;
	size_t zeroToOneFlips;
	size_t readFailures;		// ADDED: count of failed register reads (I2C/transport issues)
	int i2cAddr_fd;				// file descriptor of the i2c connection
	uint8_t* priorState;		// reference/expected value for each byte -- NEVER assigned
								// the raw "data" value read from hardware; only ever the
								// value we believe SHOULD be sitting in the chip.
	size_t capacity;			// capacity of dynamic flippedBitsPtr array
	int* bitLookup;				// size*8 entries; -1 = bit has no history yet,
								// otherwise index into flippedBitsPtr for O(1) lookup
	BitRecord* flippedBitsPtr;	// dynamic array of BitRecords, one per bit that has ever flipped
} EEPROM;


int readSerialNumber(int fd, char *serial_number_ptr) {
	/*
	 * reads factory programmed serial number and writes the serial number to char *serial_number_ptr
	 */

	// according to the AT24CS01 datasheet a serial read requires a dummy write sequence before 
	// the serial number read. It turns out wiringPiI2CReadReg8 already does this so you just
	// have to read 16 bytes contiguously from the first byte of the serial number 0x80 or 80h (80 hex, idk why they write it like this in the data sheet
	// important note: The serial number is a combination of all the 16 bytes that are stored in memory to make one long hexidecimal number. To get the serial number you must
	// combine all of the bytes into one long hexadecimal number. reg1:0x09 reg2:0x40... Serialnumber 0x0940...
	uint8_t serial_bytes[SERIAL_NUMBER_LEN] = {0};
	for (uint16_t reg = 0; reg < SERIAL_NUMBER_LEN; reg++) {
		int value = wiringPiI2CReadReg8(fd, 0x80 + reg);
		if (value == -1) {
			fprintf(stderr, "Error: data read failure at reg: %d\n", reg);
			return EXIT_FAILURE;
		}
		//printf("reg: %d, value: %d\n",reg,value);
		serial_bytes[reg] = (uint8_t)value; // cast as uint8_t to keep hex value
	}

	// convert this serial_bytes to a character array with hex values concatennated
	for (int i = 0; i < SERIAL_NUMBER_LEN; i++) {
		snprintf(&serial_number_ptr[i * 2], 3, "%02X", serial_bytes[i]);
	}
	serial_number_ptr[SERIAL_NUMBER_LEN * 2] = '\0';
	return EXIT_SUCCESS;
}

// FIX: eepromClose now frees every heap allocation the EEPROM struct owns
// (priorState, flippedBitsPtr, bitLookup) before exiting, rather than just
// closing the fd. Previously only priorState/flippedBitsPtr/bitLookup were
// left dangling on an early-exit path, which either leaked memory or (in
// the very first version of this header) didn't free anything at all.
// Each free() is guarded with a NULL check since eepromClose can be called
// before all three arrays have necessarily been allocated (e.g. if
// wiringPiI2CSetup() fails before malloc() calls happen in initEEPROM).
void eepromClose(EEPROM* eeprom) {
	if (eeprom->i2cAddr_fd >= 0) {
		close(eeprom->i2cAddr_fd);
	}
	if (eeprom->priorState != NULL) {
		free(eeprom->priorState);
	}
	if (eeprom->flippedBitsPtr != NULL) {
		free(eeprom->flippedBitsPtr);
	}
	if (eeprom->bitLookup != NULL) {
		free(eeprom->bitLookup);
	}
	exit(EXIT_FAILURE);
}

#endif // EEPROM_I2C_H
