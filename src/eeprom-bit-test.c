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
#define RUNNING_TIME_SEC 3
#define INIT_BYTES_VALUE 0xFF


void initEEPROM(EEPROM* eeprom) {

	eeprom->size = EEPROM_SIZE;
	eeprom->priorState = NULL;

	/* Intialize EEPROM Connection
	wiringPiI2CSetup() is a function that initializes an I2C device by passing its device address
	This address can be found with "$ i2detect -y 1"
	The functions return value is the standard Linux filehandle, -1 if any error
	-1 is for linux file handle and wiringPi also returns -1 for failures

	*/
	eeprom->i2cAddr_fd = wiringPiI2CSetup(EEPROM_ADDRESS);
	//printf("i2cAddr:%d\n", eeprom->i2cAddr);
	
	if (eeprom->i2cAddr_fd < 0) {
		printf(ANSI_COLOR_RED"Error: "ANSI_COLOR_RESET"Failed to initialize EEPROM at address 0x%X\n", EEPROM_ADDRESS);
		eeprom->failures = -1; // this will underflow, very big number
		exit(EXIT_FAILURE);
	}
	
	eeprom->priorState = (uint8_t*)calloc(eeprom->size, sizeof(uint8_t));
	if (eeprom->priorState == NULL) {
		printf(ANSI_COLOR_RED"Error: "ANSI_COLOR_RESET"failed allocate priorState\n");
		exit(EXIT_FAILURE);
	}

	bool init = true;

	/* initialize all bits in EEPROM to 1 by setting each byte to 0xFF.
	*/
	for (int reg = 0; reg < eeprom->size; reg++) {
		if (wiringPiI2CWriteReg8(eeprom->i2cAddr_fd, reg, INIT_BYTES_VALUE) < 0) {
			printf(ANSI_COLOR_RED"Error: "ANSI_COLOR_RESET"Failed to write to EEPROM at register: %d\n", reg);
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
}

void logger(time_t startTime, FILE* csv_file, EEPROM* eeprom) {
	time_t currTime = 0;
	int elapsedTime = 0;
	
	eeprom->i2cAddr_fd = wiringPiI2CSetup(EEPROM_ADDRESS);

	//===================================
	// Check for bit flips and types 
	//===================================
	int data = -1;	// store data that is read from register
	if (eeprom->i2cAddr_fd < 0) {	
		eeprom->failures =-1337;
		printf(ANSI_COLOR_RED"ERROR: "ANSI_COLOR_RESET"failed to initalize eeprom fd at %d\n", EEPROM_ADDRESS);
		exit(EXIT_FAILURE);
	}
	for (int reg = 0; reg < eeprom->size; reg++) {
		data = wiringPiI2CReadReg8(eeprom->i2cAddr_fd, reg);
		printf("\rdata: %d byte:%d",data,reg);
		fflush(stdout);

		if (data>0) { // successful read

			//===================================
			// bit flip detection and tracking
			//===================================
			if ((uint8_t)data != eeprom->priorState[reg]) { // flip detected
				eeprom->failures = eeprom->failures + 1;	// increment failure count

				for (uint8_t i = 0; i < 8; i++) { // this byte doesn't match the prior state of the byte
					// extract bit we are looking at to see which bit failed
					uint8_t currBit = (data & (1<<i) ) >> i;
					uint8_t priorBit = (((eeprom->priorState)[reg]) & (1 << i)) >> i;
					// check what type of bit flip and increase counter accordingly
					if (priorBit == 1 && currBit == 0) {
						eeprom->oneToZeroFlips += 1;
					}
					if(priorBit == 0 && currBit == 1) {
						eeprom->zeroToOneFlips += 1;
					}
				// restore byte to prior state before bit flip
				if (wiringPiI2CWriteReg8(eeprom->i2cAddr_fd, reg, (eeprom->priorState[reg])) < 0) {
					printf(ANSI_COLOR_RED"Error: "ANSI_COLOR_RESET"Failed to write to previous value EEPROM at register: %d\n", reg);
					break;
				}
				delay(5);
				
				// update prior state
				(eeprom->priorState)[reg] = data;
				}
			}
		} 
		else { // unsuccessful read
			printf(ANSI_COLOR_RED"ERROR: "ANSI_COLOR_RESET"failed to read from EEPROM at register: %d data: %d eeprom->priorState: %d\n", reg, data, eeprom->priorState[reg]);
		}
		
		// check time
		currTime = time(NULL);
		elapsedTime = (int) difftime(currTime, startTime);
		
		// log to .CSV file
		fprintf(csv_file, "%d, %ld, %ld, %ld\n", elapsedTime, eeprom->failures, eeprom->oneToZeroFlips, eeprom->zeroToOneFlips);
	}
	close(eeprom->i2cAddr_fd);	

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
	delay(2000); // 2 second delay
	
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
	delay(3000);

	//==============================
	// init EEPROM and file
	//==============================
	printf("\n---- Initializing EEPROM and file ----\n\n");

	// setup eeprom
	EEPROM eeprom = {0};	
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

	fprintf(csv_file, "Elapsed Time (s), Failures, 1->0 flips, 0->1 flips\n");
	fclose(csv_file);
	csv_file = fopen(filename, "a");
	delay(5000);

	//=============================
	// begin bit test
	//=============================
	time_t ctime = time(NULL);
	printf("\n---- It's logging time ----\n\n");

	while (((int)difftime(time(NULL),ctime)) <= RUNNING_TIME_SEC) {
		logger(ctime, csv_file, &eeprom);
		fclose(csv_file);
		csv_file = fopen(filename, "a");
	}

	//=============================
	// close and free memory
	//=============================
	fclose(csv_file);

	if (eeprom.priorState != NULL) {	// stops from freeing twice
		free(eeprom.priorState);
	}

	printf(ANSI_COLOR_GREEN"\n\n---- bit test complete ----\n\n"ANSI_COLOR_RESET);
	return EXIT_SUCCESS;
}
