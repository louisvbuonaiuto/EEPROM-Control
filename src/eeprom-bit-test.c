/*
 * eeprom-bit-test.c
 * This program sets the bits in an eeprom and records if any changes happens in them to a csv file
 *
 */


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
#define EEPROM_SIZE 128	// in bytes
#define RUNNING_TIME_SEC 3600 // 1 hour test
#define INIT_BYTES_VALUE 0xFF

// it works yay!
// TODO use serial number and date for unique file name each test
// TODO write 0->1 and 1->0 bit flips to file, also save serial number and date to file

void initEEPROM(EEPROM* eeprom) {

	eeprom->size = EEPROM_SIZE;
	eeprom->priorState = NULL;

	/* Intialize EEPROM Connection
	wiringPiI2CSetup() is a function that initializes an I2C device by passing its device address
	This address can be found with "$ i2detect -y 1"
	The functions return value is the standard Linux filehandle, -1 if any error
	*/
	eeprom->i2cAddr_fd = wiringPiI2CSetup(EEPROM_ADDRESS);
	//printf("i2cAddr:%d\n", eeprom->i2cAddr);
	
	if (eeprom->i2cAddr_fd < 0) {
		printf("Failed to initialize EEPROM at address 0x%X\n", EEPROM_ADDRESS);
		eeprom->failures = -1; // this will underflow, very big number
		exit(EXIT_FAILURE);
	}
	
	eeprom->priorState = (uint8_t*)calloc(eeprom->size, sizeof(uint8_t));
	if (eeprom->priorState == NULL) {
		printf("ERROR: failed allocate priorState\n");
		exit(EXIT_FAILURE);
	}

	bool init = true;

	/* initialize all bits in EEPROM to 1 by setting each byte to 0xFF.
	*/
	for (int reg = 0; reg < eeprom->size; reg++) {
		if (wiringPiI2CWriteReg8(eeprom->i2cAddr_fd, reg, INIT_BYTES_VALUE) == -1) {
			printf("ERROR: Failed to write to EEPROM at register: %d\n", reg);
			init = false;
			break;
		}
		delay(5);
		eeprom->priorState[reg] = (uint8_t)INIT_BYTES_VALUE;
	}
	close(eeprom->i2cAddr_fd);
	if (init == false) {
		free(eeprom->priorState);
		exit(EXIT_FAILURE);
	}
	printf("Set %d bytes to %d\n",EEPROM_SIZE,INIT_BYTES_VALUE);
	delay(5000); // 5 seconds
}

void logger(time_t startTime, FILE* csv_file, EEPROM* eeprom) {
	time_t currTime = 0;
	int elapsedTime = 0;
	
	eeprom->i2cAddr_fd = wiringPiI2CSetup(EEPROM_ADDRESS);

	/*
	Check for bit flips and types 
	*/ 
	// TODO This is having problems when reading the data, copy code from read-write test since that is working
	// TODO rewrite this so error handling doesn't nest code, if (error) // error handle code;
	int data = -1;
	if (eeprom->i2cAddr_fd < 0) {	
		eeprom->failures =-1337;
		printf(ANSI_COLOR_RED"ERROR: "ANSI_COLOR_RESET"failed to initalize eeprom fd at %d\n", EEPROM_ADDRESS);
		exit(EXIT_FAILURE);
	}
	for (int reg = 0; reg < eeprom->size; reg++) {
		data = wiringPiI2CReadReg8(eeprom->i2cAddr_fd, reg);
		printf("\rdata: %d byte:%d",data,reg);
		fflush(stdout);

		if (data>0) { // succesful read
			if ((uint8_t)data != eeprom->priorState[reg]) { // flip detected
				eeprom->failures = eeprom->failures + 1;	// increment failure count

				for (uint8_t i = 0; i < 8; i++) { // this byte doesn't match the previous state
					// extract bit we are looking at
					uint8_t currBit = (data & (1<<i)) >> i;
					uint8_t priorBit = ((eeprom->priorState)[reg] & (1 << i)) >> i;
					// check what type of bit flip
					if (priorBit == 1 && currBit == 0) {
						eeprom->oneToZeroFlips += 1;
					}
					if(priorBit == 0 && currBit == 1) {
						eeprom->zeroToOneFlips += 1;
					}
				// restore byte to prior state before bit flip
				wiringPiI2CWriteReg8(eeprom->i2cAddr_fd, reg, (eeprom->priorState[reg]));
				// update prior state
				(eeprom->priorState)[reg] = data;
				}
			}
		} else { // unsuccessful read
			printf(ANSI_COLOR_RED"ERROR: "ANSI_COLOR_RESET"failed to read from EEPROM at register: %d data: %d eeprom->priorState: %d\n",
			reg, data, eeprom->priorState[reg]);
					
		}
		
	
		// check time
		currTime = time(NULL);
		elapsedTime = (int) difftime(currTime, startTime);
		
		// log to .CSV file
		fprintf(csv_file, "%d, %ld\n", elapsedTime, eeprom->failures);
	}
	close(eeprom->i2cAddr_fd);	
}

int main(void) {
	
	// confirm which device this file is setup for
	char confirm = 'n';
	printf("This file is setup for the AT24CS01-XHM-B, %d bytes, to run for %d minutes \n", EEPROM_SIZE, RUNNING_TIME_SEC/60);
	printf("Confirm? (y,N): ");
	if (scanf(" %c",&confirm) != 1 || confirm != 'y') {
		return EXIT_FAILURE;
	}
	printf("Starting bit test...\n");
	

	// setup eeprom
	EEPROM eeprom = {0};
	initEEPROM(&eeprom);

	// file creation
	char filename[100];
	snprintf(filename, sizeof(filename), "data/testing-board-data.csv");
	
	FILE *csv_file = fopen(filename, "a");
	if (csv_file == NULL) {
		printf("Failed to open CSV file\n");
		return EXIT_FAILURE;
	}

	fprintf(csv_file, "Elapsed Time, Failures\n");
	fclose(csv_file);
	csv_file = fopen(filename, "a");

	// start test and keep track of time
	time_t ctime = time(NULL);
	printf("It's logging time\n");

	while (((int)difftime(time(NULL),ctime)) <= RUNNING_TIME_SEC) {
		logger(ctime, csv_file, &eeprom);
		fclose(csv_file);
		csv_file = fopen(filename, "a");
	}

	fclose(csv_file);

	if (eeprom.priorState != NULL) {	// stops from freeing twice
		free(eeprom.priorState);
	}

	printf(ANSI_COLOR_GREEN"\nCompleted and written to file.\n"ANSI_COLOR_RESET);
	return EXIT_SUCCESS;
}
