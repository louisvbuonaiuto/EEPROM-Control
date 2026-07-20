/*
 * eeprom-bit-test.c
 * This program sets the bits in an eeprom and records if any changes happens in them to a csv file
 *
 */

//========================================
// eeprom-bit-test.c
//========================================

#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <time.h>
#include <stdbool.h>
#include <stdlib.h>

#include <wiringPi.h>
#include <wiringPiI2C.h>
#include "eeprom-i2c.h"

#define EEPROM_ADDRESS 0x50
#define EEPROM_SERIAL_NUM_ADDRESS 0x58
#define EEPROM_SIZE 128	// in bytes
#define WRITE_DELAY 5	// ms, refer to datasheet for max write cycle time
#define RUNNING_TIME_SEC 3
#define INIT_BYTES_VALUE 0xFF


void initEEPROM(EEPROM* eeprom) {

	eeprom->size = EEPROM_SIZE;
	eeprom->capacity = 16;	// initial capacity for dynamic array
	eeprom->maxHardFaultBits = (size_t)eeprom->size * 8;

	// allocate memory tracking structures
	eeprom->priorState   = (uint8_t*)malloc(eeprom->size * sizeof(uint8_t));
	eeprom->flippedBitsPtr = (BitRecord*)malloc(eeprom->capacity * sizeof(BitRecord));
	eeprom->bitLookup    = (int*)malloc(eeprom->size * 8 * sizeof(int));

	// NULL checks for all three allocations
	if (eeprom->priorState == NULL || eeprom->flippedBitsPtr == NULL || eeprom->bitLookup == NULL) {
		printf(ANSI_COLOR_RED"Error: "ANSI_COLOR_RESET"failed to allocate EEPROM tracking structures\n");
		eeprom->i2cAddr_fd = -1; // not opened yet, tell eepromClose() to skip close()
		eepromClose(eeprom);
	}

	// init lookup table to -1 (meaning bit has no history yet)
	for (int i = 0; i < (eeprom->size * 8); i++) {
		eeprom->bitLookup[i] = -1;
	}

	/* Intialize EEPROM Connection
	 * wiringPiI2CSetup() is a function that initializes an I2C device by passing its device address
	 * This address can be found with "$ i2detect -y 1"
	 * The functions return value is the standard Linux filehandle, -1 if any error
	 * -1 is for linux file handle and wiringPi also returns -1 for failures
	 */
	eeprom->i2cAddr_fd = wiringPiI2CSetup(EEPROM_ADDRESS);

	if (eeprom->i2cAddr_fd < 0) {
		printf(ANSI_COLOR_RED"Error: "ANSI_COLOR_RESET"Failed to initialize EEPROM at address 0x%X\n", EEPROM_ADDRESS);
		eepromClose(eeprom);
	}

	// initialize all bits in EEPROM to 1 by setting each byte to INIT_BYTES_VALUE.

	bool init = true;
	for (int reg = 0; reg < eeprom->size; reg++) {
		if (wiringPiI2CWriteReg8(eeprom->i2cAddr_fd, reg, INIT_BYTES_VALUE) < 0) {
			printf(ANSI_COLOR_RED"Error: "ANSI_COLOR_RESET"Failed to write to EEPROM at register: %d\n", reg);
			init = false;
			break;
		}
		delay(WRITE_DELAY);
		eeprom->priorState[reg] = (uint8_t)INIT_BYTES_VALUE;
	}

	if (init == false) eepromClose(eeprom);

	printf(ANSI_COLOR_GREEN"---- EEPROM Intialized ----"ANSI_COLOR_RESET
	"\nSet %d bytes to %d\n", EEPROM_SIZE, INIT_BYTES_VALUE);
}

// This function is called exactly ONCE per byte that had a mismatch, after
// all 8 bits have been scanned and classified. It:
//   - writes the whole byte back to the reference (priorState) value
//   - waits for the EEPROM's write cycle to complete
//   - reads the byte back and checks every bit that was flagged in mismatchMask
//   - retries up to RESTORE_VERIFY_RETRIES times if verification fails, since
//     a fresh SEU could land on the same bit in the tiny window between the
//     restore write and the verify read, which would otherwise be
//     misclassified as a hard fault after only one failed attempt
//   - only sets hardFault = true once verification has failed on every retry
//
// Returns true if the byte is confirmed to match priorState by the end
// (whether that took 1 attempt or several), false if a transport-level
// write/read failure prevented verification entirely (distinct from a
// hard fault -- see comments inline).
bool restoreAndVerifyByte(EEPROM* eeprom, int reg, uint8_t mismatchMask, time_t elapsedTime) {
	(void)elapsedTime; // currently unused; kept in the signature so a hard-fault
	                    // confirmation log line could record the timestamp later
	for (int attempt = 0; attempt < RESTORE_VERIFY_RETRIES; attempt++) {

		// write the whole byte back to the known-good reference value.
		// NOTE: this is a *transport* failure path (I2C write call itself
		// failed) -- distinct from a hard fault, which is when the write
		// call succeeds but the value doesn't stick. We don't set
		// hardFault here, since we have no evidence the bit is physically
		// stuck, only that this particular I2C transaction didn't go through.
		if (wiringPiI2CWriteReg8(eeprom->i2cAddr_fd, reg, eeprom->priorState[reg]) < 0) {
			printf(ANSI_COLOR_RED"Error: "ANSI_COLOR_RESET
				"I2C transport failure while restoring EEPROM at register: %d (attempt %d/%d)\n",
				reg, attempt + 1, RESTORE_VERIFY_RETRIES);
			continue; // try again rather than immediately declaring a hard fault
		}

		delay(WRITE_DELAY);

		int verifyData = wiringPiI2CReadReg8(eeprom->i2cAddr_fd, reg);
		if (verifyData < 0) {
			// read-back itself failed -- also a transport issue, not proof of a hard fault
			eeprom->readFailures++;
			printf(ANSI_COLOR_RED"Error: "ANSI_COLOR_RESET
				"Failed to read back register %d during verification (attempt %d/%d)\n",
				reg, attempt + 1, RESTORE_VERIFY_RETRIES);
			continue;
		}

		if ((uint8_t)verifyData == eeprom->priorState[reg]) {
			// restore verified successfully
			return true;
		}

		// verifyData didn't match priorState -- figure out which of the
		// originally-flagged bits are still wrong on this attempt
		for (uint8_t i = 0; i < 8; i++) {
			if (mismatchMask & (1 << i)) {
				uint8_t verifyBit = (verifyData >> i) & 1;
				uint8_t refBit    = (eeprom->priorState[reg] >> i) & 1;
				if (verifyBit != refBit && attempt == RESTORE_VERIFY_RETRIES - 1) {
					// only declare a hard fault after the LAST retry has
					// also failed to restore this bit -- a single failed
					// attempt is not strong enough evidence on its own
					int globalIndex = (reg * 8) + i;
					int lIndex = eeprom->bitLookup[globalIndex];
					if (lIndex != -1 && !eeprom->flippedBitsPtr[lIndex].hardFault) {
						eeprom->flippedBitsPtr[lIndex].hardFault = true;
						eeprom->numHardFaultBits++;
						printf(ANSI_COLOR_RED"HARD FAULT: "ANSI_COLOR_RESET
							"byte %d bit %d confirmed stuck after %d attempts\n",
							reg, i, RESTORE_VERIFY_RETRIES);
					}
				}
			}
		}
		// loop again if attempts remain
	}
	return false; // verification failed and bit is stuck
}

void logger(time_t startTime, FILE* csv_file, EEPROM* eeprom) {
	time_t currTime = 0;
	int elapsedTime = 0;

	/*
	pseudocode for bit flip checking

	read each register
	if read failed: log read failure, skip register, continue
	compare register to priorState
	if mismatch:
		for each of the 8 bits in the byte:
			if bit differs from priorState:
				record it in mismatchMask
				look up (or create) its BitRecord via bitLookup
				if not already hardFault: increment numBitFlips + directional counter,
					update firstFlipTime/lastFlipTime
		# ONLY after all 8 bits have been scanned:
		call restoreAndVerifyByte() ONCE for the whole byte:
			write priorState[reg] back
			delay for write-cycle time
			read back and compare
			retry up to RESTORE_VERIFY_RETRIES times
			only set hardFault=true if every retry still mismatches
	*/
	//===================================
	// Check for bit flips and types 
	//===================================
	int data = -1;	// data stores the value in the read register

	// read each register
	for (int reg = 0; reg < eeprom->size; reg++) {
		data = wiringPiI2CReadReg8(eeprom->i2cAddr_fd, reg);	// store read value into data

		if (data < 0) { // failed to read
			eeprom->readFailures++; // ADDED: track transport read failures separately from bit flips
			printf(ANSI_COLOR_RED"ERROR: "ANSI_COLOR_RESET"failed to read from EEPROM at register: %d data: %d eeprom->priorState: %d\n",
				reg, data, eeprom->priorState[reg]);
			continue; // skip this register this pass
		}

		// successful read, data >= 0 
		// compare read byte to priorState of byte
		if ((uint8_t)data != eeprom->priorState[reg]) {
			// read byte doesn't match priorState -- scan all 8 bits first,
			// build up mismatchMask, THEN do a single restore+verify for
			// the whole byte (moved out of this loop -- see restoreAndVerifyByte)
			uint8_t mismatchMask = 0;

			for (uint8_t i = 0; i < 8; i++) {
				uint8_t currBit  = (data & (1 << i)) >> i;
				uint8_t priorBit = (eeprom->priorState[reg] & (1 << i)) >> i;

				if (currBit != priorBit) { // found a bit that flipped
					mismatchMask |= (1 << i); // track which bit(s) flipped for verification later

					int globalBitIndex = (reg * 8) + i;
					int lookupIndex = eeprom->bitLookup[globalBitIndex];
					BitRecord* currentBitRecord = NULL;

					// if bit has no history, create new record
					if (lookupIndex == -1) {
						if (eeprom->numBitFlips + eeprom->numHardFaultBits >= eeprom->capacity) {
							size_t newCapacity = eeprom->capacity * 2;
							BitRecord* resized = (BitRecord*)realloc(eeprom->flippedBitsPtr, newCapacity * sizeof(BitRecord));
							// FIX: realloc's return value is now checked before use.
							// If realloc fails it returns NULL while leaving the
							// original block untouched -- the old code overwrote
							// eeprom->flippedBitsPtr unconditionally, which on
							// failure would leak the original allocation AND
							// immediately segfault on the next dereference below.
							if (resized == NULL) {
								printf(ANSI_COLOR_RED"Error: "ANSI_COLOR_RESET
									"realloc failed while growing flippedBitsPtr (capacity %zu)\n", newCapacity);
								eepromClose(eeprom); // safe: frees whatever is still valid
							}
							eeprom->flippedBitsPtr = resized;
							eeprom->capacity = newCapacity;
						}
						lookupIndex = (int)(eeprom->numBitFlips + eeprom->numHardFaultBits);
						eeprom->bitLookup[globalBitIndex] = lookupIndex;
						currentBitRecord = &eeprom->flippedBitsPtr[lookupIndex];

						currentBitRecord->byteOffset   = reg;
						currentBitRecord->bitIndex     = i;
						currentBitRecord->hardFault     = false;
						currentBitRecord->timesFlipped  = 0;		
						currentBitRecord->firstFlipTime = elapsedTime;
						currentBitRecord->lastFlipTime  = elapsedTime;
					} else {
						currentBitRecord = &eeprom->flippedBitsPtr[lookupIndex];
					}

					// only increment flips if the bit isn't already known to be stuck
					if (currentBitRecord->hardFault == false) {
						currentBitRecord->timesFlipped++;
						currentBitRecord->lastFlipTime = elapsedTime;
						if (priorBit == 1 && currBit == 0) eeprom->oneToZeroFlips += 1;
						if (priorBit == 0 && currBit == 1) eeprom->zeroToOneFlips += 1;
						eeprom->numBitFlips += 1; 
					}
				}
			}

			// FIX: restore + verify now happens exactly ONCE here, after the
			// full byte has been scanned -- not once per bit index inside
			// the loop above. This removes the redundant repeated writes
			// and is what actually makes hard-fault detection reachable.
			restoreAndVerifyByte(eeprom, reg, mismatchMask, (time_t)elapsedTime);
		}

		// check time
		currTime = time(NULL);
		elapsedTime = (int) difftime(currTime, startTime);

		// log to .CSV file
		fprintf(csv_file, "%d, %zu, %zu, %zu, %zu, %zu\n",
			elapsedTime, eeprom->numBitFlips, eeprom->oneToZeroFlips,
			eeprom->zeroToOneFlips, eeprom->numHardFaultBits, eeprom->readFailures);

		// monitor test
		printf("\rTime:%-6d byte:%-6d data:%-3d Bit Flips:%-6zu 1->0:%-5zu 0->1:%-5zu, Hard Faults:%-6zu Read Fails:%-5zu ",
			elapsedTime, reg, data, eeprom->numBitFlips, eeprom->oneToZeroFlips,
			eeprom->zeroToOneFlips, eeprom->numHardFaultBits, eeprom->readFailures);
		fflush(stdout);
	}
}

int main(void) {

	//==================================
	// user confirmation before test
	//==================================
	char confirm = 'n';
	printf("This file is setup for the AT24CS01-XHM-B, %d bytes, to run for %d minutes \n", EEPROM_SIZE, RUNNING_TIME_SEC/60);
	printf("Confirm? (y,N): ");
	if (scanf(" %c", &confirm) != 1 || confirm != 'y') {
		return EXIT_FAILURE;
	}
	delay(1000); // 1 second delay

	//===============================
	// read serial number
	//===============================
	EEPROM serial_eeprom = {0};
	serial_eeprom.i2cAddr_fd = wiringPiI2CSetup(EEPROM_SERIAL_NUM_ADDRESS);
	if (serial_eeprom.i2cAddr_fd < 0) {
		fprintf(stderr, ANSI_COLOR_RED"Error: "ANSI_COLOR_RESET"Failed to initialize EEPROM at address 0x%X\n", EEPROM_SERIAL_NUM_ADDRESS);
		eepromClose(&serial_eeprom);
	}
	char serial_number[SERIAL_NUMBER_LEN+1]; // +1 for null terminator
	if (readSerialNumber(serial_eeprom.i2cAddr_fd, serial_number) == 1) {
		fprintf(stderr, ANSI_COLOR_RED"Error: "ANSI_COLOR_RESET"readSerialNumber() failed\n");
		eepromClose(&serial_eeprom);
	}
	close(serial_eeprom.i2cAddr_fd);
	printf("Serial Number: %s\n", serial_number);
	delay(1000);

	//==============================
	// init EEPROM and file
	//==============================
	printf("\n---- Initializing EEPROM and file ----\n\n");

	// setup eeprom
	EEPROM eeprom = {0};	// all members of the structure are intialized to 0, if it is a pointer then NULL
	initEEPROM(&eeprom);	// note: this function initializes an eeprom by setting all of its bits to a #defined VALUE
							// once this value is set the connection is kept open and reused for the rest of the test
							// no need to keep using wiringPiI2CSetup()

	// file creation
	char filename[100];
	snprintf(filename, sizeof(filename), "data/bit-test-%s-data.csv", serial_number);

	FILE *csv_file = fopen(filename, "a");
	if (csv_file == NULL) {
		printf(ANSI_COLOR_RED"Error: "ANSI_COLOR_RESET"Failed to open CSV file\n");
		return EXIT_FAILURE;
	}
	printf("Saving data to file: %s\n", filename);

	fprintf(csv_file, "Elapsed Time (s), Total Bit Flips, 1->0 flips, 0->1 flips, Hard Faults, Read Failures\n");
	fclose(csv_file);
	csv_file = fopen(filename, "a");
	delay(3000);

	//=============================
	// begin bit test
	//=============================
	printf("\n---- It's logging time ----\n\n");

	time_t ctime = time(NULL);
	while (((int)difftime(time(NULL), ctime)) <= RUNNING_TIME_SEC) {
		logger(ctime, csv_file, &eeprom);
		fclose(csv_file);
		csv_file = fopen(filename, "a");
	}

	//=============================
	// close and free memory
	//=============================
	fclose(csv_file);
	close(eeprom.i2cAddr_fd);
	free(eeprom.flippedBitsPtr);
	free(eeprom.bitLookup);

	if (eeprom.priorState != NULL) {	// stops from freeing twice
		free(eeprom.priorState);
	}

	printf(ANSI_COLOR_GREEN"\n\n---- bit test complete ----\n\n"ANSI_COLOR_RESET);
	return EXIT_SUCCESS;
}
