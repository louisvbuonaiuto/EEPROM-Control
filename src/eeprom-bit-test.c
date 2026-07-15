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
#define RUNNING_TIME_SEC 5

int initEEPROM(EEPROM* eeprom) {

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
		return EXIT_FAILURE;
		}
	
	bool init = true;

	/* initialize all bits in EEPROM to 1 by setting each byte to 0xFF.
	*/
	for (int reg = 0; reg < eeprom->size; reg++) {
		if (wiringPiI2CWriteReg8(eeprom->i2cAddr_fd, reg, 0xFF) == -1) {
			printf("ERROR: Failed to write to EEPROM at register: %d\n", reg);
			init = false;
			break;
		}
		delay(5);
	}
	close(eeprom->i2cAddr_fd);

	if (init == false) return EXIT_FAILURE;
	
	eeprom->priorState = (uint8_t*) calloc(eeprom->size, sizeof(uint8_t));
	printf("Initialized EEPROM at address 0x%X\n", EEPROM_ADDRESS);
	return EXIT_SUCCESS;
	
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
	if (eeprom->i2cAddr_fd >= 0) {
		for (int reg = 0; reg < eeprom->size; reg++) {
			int data = wiringPiI2CReadReg8(eeprom->i2cAddr_fd, reg);
			
			printf("data: %d byte:%d\n",data,reg);

			if (data != eeprom->priorState[reg] && data > 0) { // flip detected
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
				}
				// restore byte to prior state before bit flip
				wiringPiI2CWriteReg8(eeprom->i2cAddr_fd, reg, (eeprom->priorState[reg]));
				// update prior state
				(eeprom->priorState)[reg] = data;

			} else {
				printf("ERROR: failed to read from EEPROM at register: %d\n", reg);
					
			}
		}
	
		// check time
		currTime = time(NULL);
		elapsedTime = (int) difftime(currTime, startTime);
		
		// log to .CSV file
		fprintf(csv_file, "%d, %ld\n", elapsedTime, eeprom->failures);

		close(eeprom->i2cAddr_fd);
	}
	else {
		eeprom->failures =-1337;
		printf("EEPROM not responding at address 0x%X\n", EEPROM_ADDRESS);

	}
}

int main(void) {

	time_t ctime = time(NULL);

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

	EEPROM eeprom = {0};

	if (initEEPROM(&eeprom) != 0) {
		printf("ERROR: eeprom not initialized\n");
		return EXIT_FAILURE;
	}

	// reset time and start test
	ctime = time(NULL);
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

	printf("Completed and written to file.\n");
	return EXIT_SUCCESS;
}
