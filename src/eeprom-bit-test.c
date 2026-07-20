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

	// allocate memory tracking structures
	eeprom->priorState = (uint8_t*)malloc(eeprom->size * sizeof(uint8_t));
	eeprom->flippedBitsPtr = (BitRecord*)malloc(eeprom->capacity * sizeof(BitRecord));
	eeprom->bitLookup = (int*)malloc(eeprom->size * 8 * sizeof(int));

	// init lookup table to -1 (meaning bit has no history yet)
	for (int i=0; i < (eeprom->size * 8); i++) {
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
		exit(EXIT_FAILURE);
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
	"\nSet %d bytes to %d\n",EEPROM_SIZE,INIT_BYTES_VALUE);
}

void logger(time_t startTime, FILE* csv_file, EEPROM* eeprom) {
	time_t currTime = 0;
	int elapsedTime = 0;

	/*
	pseudocode

	read each register
	compare between register and previous register state
	if mismatch:
		check which bits have flipped
		for each flipped bit
			check if bit has experienced flip before
			if hasn't
				realloc dynamic array
				create BitRecord struct for that bit and record byteOffset and bitIndex
			else has
				use reference to BitRecord
			if hardFault=true
				pass
			increment numFlippedBits
			check what type of bit flip
			increment type of bit flip
		for the entire byte write the reference value back
		// do this next step 3 times to verify since one write back could be an accidental flip back
		assuming write function had no error: read register to check if the bits flipped back
		if still_mismatch
			hardfault error detected, hardFault=true
	*/
	//===================================
	// Check for bit flips and types 
	//===================================
	int data = -1;	// data stores the value in the read register

	// read each register
	for (int reg = 0; reg < eeprom->size; reg++) {
		data = wiringPiI2CReadReg8(eeprom->i2cAddr_fd, reg);	// store read value into data
		
		if (data < 0 ) { // failed to read
			printf(ANSI_COLOR_RED"ERROR: "ANSI_COLOR_RESET"failed to read from EEPROM at register: %d data: %d eeprom->priorState: %d\n", reg, data, eeprom->priorState[reg]);
			continue; // skip this register
		}

		// successful read, data >= 0
		printf("\rdata: %d byte:%d ", data, reg);
		fflush(stdout);
		
		// compare read byte to priorState of byte
		if ((uint8_t)data != eeprom->priorState[reg]) {
			// read byte doesn't match priorState
			bool bitRequiresRestore = false;
			uint8_t mismatchMask = 0;		
			
			// extract individual bits from byte to check which bit(s) flipped, note: there may be multiple in one byte
			for (uint8_t i = 0; i < 8; i++) {
				uint8_t currBit = (data & (1<<i) ) >> i;	// these bitwise operations extract only the i'th bit
				uint8_t priorBit = (eeprom->priorState[reg] & (1 << i)) >> i;
				
				if (currBit != priorBit) {	// found a bit that flipped
					bitRequiresRestore = true;
					mismatchMask |= (1 << i);	// track which bit has flipped for verification later

					int globalBitIndex = (reg * 8) + i;
					int lookupIndex = eeprom->bitLookup[globalBitIndex];
					BitRecord* currentBitRecord = NULL;
				
					// if bit has no history, create new record
					if (lookupIndex == -1) {
						if (eeprom->numBitFlips >= eeprom->capacity) {
							eeprom->capacity *= 2;
							eeprom->flippedBitsPtr = (BitRecord*)realloc(eeprom->flippedBitsPtr, eeprom->capacity * sizeof(BitRecord));
						}
						lookupIndex = eeprom->numBitFlips;
						eeprom->bitLookup[globalBitIndex] = lookupIndex;
						currentBitRecord = &eeprom->flippedBitsPtr[lookupIndex];

						currentBitRecord->byteOffset = reg;
						currentBitRecord->bitIndex = i;
						currentBitRecord->hardFault = false;
						
						eeprom->numBitFlips++;
					}
					else {
						currentBitRecord = &eeprom->flippedBitsPtr[lookupIndex];
					}
				// only increment flips if the bit isn't already stuck
					if (currentBitRecord->hardFault == false) {
						if (priorBit == 1 && currBit == 0) eeprom->oneToZeroFlips += 1;
						if (priorBit == 0 && currBit == 1) eeprom->zeroToOneFlips += 1;
					}
				}
				

				// restore whole byte to prior state 
				if (bitRequiresRestore) {
					if (wiringPiI2CWriteReg8(eeprom->i2cAddr_fd, reg, eeprom->priorState[reg]) < 0) {
						printf(ANSI_COLOR_RED"Error: "ANSI_COLOR_RESET
						"I2C Transport failure while restoring EEPROM at register: %d\n", reg);
						break;
					}
				}
				else {
					delay(WRITE_DELAY);
					
					// read data back to verify flip was SEU or Hard Fault
					int verifyData = wiringPiI2CReadReg8(eeprom->i2cAddr_fd, reg);

					if (verifyData >= 0 && (uint8_t)verifyData != eeprom->priorState[reg]) {
						uint8_t verifyBit = (verifyData >> i) & 1;
						uint8_t refBit = (eeprom->priorState[reg] >> i) & 1;

						if ((verifyBit != refBit) && (mismatchMask & (1<<i))) {
							int globalIndex = (reg * 8) + i;
							int lIndex = eeprom->bitLookup[globalIndex];

							if (lIndex != -1 && !eeprom->flippedBitsPtr[lIndex].hardFault) {
								eeprom->flippedBitsPtr[lIndex].hardFault = true;
								eeprom->numHardFaultBits++;
							}
						}
					}
				}
			}
		}
		// check time
		currTime = time(NULL);
		elapsedTime = (int) difftime(currTime, startTime);
		
		// log to .CSV file
		fprintf(csv_file, "%d, %ld, %ld, %ld, %ld\n", elapsedTime, eeprom->numBitFlips, eeprom->oneToZeroFlips, eeprom->zeroToOneFlips, eeprom->numHardFaultBits);
		
		// monitor test
		printf("\rTime: %-5d byte: %-6d data: %-3d, Bit Flips: %-5ld, 1->0: %-5ld, 0->1: %-5ld, Hard Faults: %-5ld      ",elapsedTime, reg, data, eeprom->numBitFlips, eeprom->oneToZeroFlips, eeprom->zeroToOneFlips, eeprom->numHardFaultBits);
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
	if (scanf(" %c",&confirm) != 1 || confirm != 'y') {
		return EXIT_FAILURE;
	}
	delay(1000); // 1 second delay
	
	//===============================
	// read serial number
	//===============================
	EEPROM serial_eeprom = {0};
	serial_eeprom.i2cAddr_fd = wiringPiI2CSetup(EEPROM_SERIAL_NUM_ADDRESS);
	if (serial_eeprom.i2cAddr_fd < 0) {
		fprintf(stderr,ANSI_COLOR_RED"Error: "ANSI_COLOR_RESET"Failed to initialize EEPROM at address 0x%X\n", EEPROM_SERIAL_NUM_ADDRESS);
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
	EEPROM eeprom = {0};	// all members of the structure are intialized to 0, if its a pointer then NULL
	initEEPROM(&eeprom);	// note: this function initializes an eeprom by setting all of its bits to a #defined VALUE							   // once this value is set it closes the eeprom connection and logger opens and reopens 
							// this connection everytime its opened
	// file creation
	char filename[100];
	snprintf(filename, sizeof(filename), "data/bit-test-%s-data.csv",serial_number);
	
	FILE *csv_file = fopen(filename, "a");
	if (csv_file == NULL) {
		printf(ANSI_COLOR_RED"Error: "ANSI_COLOR_RESET"Failed to open CSV file\n");
		return EXIT_FAILURE;
	}
	printf("Saving data to file: %s\n",filename);

	fprintf(csv_file, "Elapsed Time (s), Total Bit Flips, 1->0 flips, 0->1 flips, Hard Faults\n");
	fclose(csv_file);
	csv_file = fopen(filename, "a");
	delay(3000);

	//=============================
	// begin bit test
	//=============================
	printf("\n---- It's logging time ----\n\n");

	time_t ctime =time(NULL);
	while (((int)difftime(time(NULL),ctime)) <= RUNNING_TIME_SEC) {
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

	if (eeprom.priorState != NULL) {	// stops from freeing twice
		free(eeprom.priorState);
	}

	printf(ANSI_COLOR_GREEN"\n\n---- bit test complete ----\n\n"ANSI_COLOR_RESET);
	return EXIT_SUCCESS;
}
