#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <time.h>
#include <stdbool.h>
#include <stdlib.h>

#include <wiringPiI2C.h>
#include "../include/eeprom-i2c.h"


#define EEPROM_ADDRESS 0x50
#define EEPROM_SIZE 4096	// in bytes
#define RUNNING_TIME_SEC 5

typedef struct {
	int size;				// size in bytes of EEPROM
	int i2cAddr_fd;			// file descriptor of the i2c bus
	size_t failures;		// how many times has this EEPROM experienced a flip
	uint8_t* priorState;	// addresses that we know have failed

	// types of bit flips
	size_t oneToZeroFlips;	// number of 0->1 bit flips
	size_t zeroToOneFlips;	// number of 1->0 bit flips
	size_t hardFaultBits;	// number of hardfaulting bits
} EEPROM;



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
		return;
		}
	
	bool init = true;

	/* initialize all bits in EEPROM to 1 by setting each byte to 0xFF.
	*/
	for (int byte = 0; byte < eeprom->size; byte++) {
		if (writeMemoryByte16(eeprom->i2cAddr_fd, byte, 0xFF) == -1) {
			printf("Failed to write to EEPROM at byte %d\n", byte);
			init = false;
			break;
		}
	}
	if (init) {
		eeprom->priorState = (uint8_t*) calloc(eeprom->size, sizeof(uint8_t));
		printf("Initialized EEPROM at address 0x%X\n", EEPROM_ADDRESS);
	}
	
	close(eeprom->i2cAddr_fd);
}

void logger(time_t startTime, FILE* csv_file, EEPROM* eeprom) {
	time_t currTime = 0;
	int elapsedTime = 0;
	
	eeprom->i2cAddr_fd = wiringPiI2CSetup(EEPROM_ADDRESS);

	/*
	Check for bit flips and types 
	*/
	if (eeprom->i2cAddr_fd >= 0) {
		for (int byte = 0; byte < eeprom->size; byte++) {
			uint8_t data = readMemoryByte16(eeprom->i2cAddr_fd, byte);
			printf("data: %d byte:%d\n",data,byte);

			if (data != eeprom->priorState[byte] && data > 0) { // flip detected
				eeprom->failures = eeprom->failures + 1;	// increment failure count

				for (uint8_t i = 0; i < 8; i++) { // this byte doesn't match the previous state
					// extract bit we are looking at
					uint8_t currBit = (data & (1<<i)) >> i;
					uint8_t priorBit = ((eeprom->priorState)[byte] & (1 << i)) >> i;
					// check what type of bit flip
					if (priorBit == 1 && currBit == 0) {
						eeprom->oneToZeroFlips += 1;
					}
					if(priorBit == 0 && currBit == 1) {
						eeprom->zeroToOneFlips += 1;
					}
				}
				// restore byte to prior state before bit flip
				writeMemoryByte16(eeprom->i2cAddr_fd, byte, (eeprom->priorState[byte]));
				// update prior state
				(eeprom->priorState)[byte] = data;

			} else {
				printf("failed to read from EEPROM\n");
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
	//wiringPiSetup();

	time_t ctime = time(NULL);

	// file creation
	char filename[100];
	snprintf(filename, sizeof(filename), "data/board-data.csv");
	
	FILE *csv_file = fopen(filename, "a");
	if (csv_file == NULL) {
		printf("Failed to open CSV file\n");
		return -1;
	}

	fprintf(csv_file, "Elapsed Time, Failures\n");
	fclose(csv_file);
	csv_file = fopen(filename, "a");

	EEPROM eeprom = {0};

	initEEPROM(&eeprom);

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
