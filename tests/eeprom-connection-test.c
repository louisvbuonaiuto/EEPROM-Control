/*
    Test connection between PI and single EEPROM with a read and write test
    - 
*/

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <stdbool.h>
#include <errno.h>

#include <wiringPi.h>
#include <wiringPiI2C.h>
#include "../include/eeprom-i2c.h"

#define EEPROM_ADDRESS 0x50
#define EEPROM_SIZE 4 // bytes
#define DATA 0xFF

typedef struct {
	int size;	//size in bytes of EEPROM
	int i2cAddr_fd;
} EEPROM;

int main(void) 
{
	// init EEPROM
	EEPROM eeprom = {0};
	eeprom.size = EEPROM_SIZE;
	eeprom.i2cAddr_fd = wiringPiI2CSetup(EEPROM_ADDRESS);
	if (eeprom.i2cAddr_fd < 0) {
		printf("Error: Failed to initialize EEPROM at address 0x%X\n", EEPROM_ADDRESS);
		return EXIT_FAILURE;
	}

	// initialize all bits in EEPROM to 1 by setting each byte to 0xFF
	bool init = true;
	for (int byte; byte < eeprom.size; byte++) {
		if (writeMemoryByte16(eeprom.i2cAddr_fd, byte, DATA) == -1) {
			printf("Error: Failed to write to EEPROM at byte:%d\n", byte);
			init = false;
			break;
		}
		printf("WRITE: byte: %d data: %d\n", byte, DATA);
	}
	close(eeprom.i2cAddr_fd);
	
	if (init) printf("Successfully wrote to all bytes\n");	

	else return EXIT_FAILURE;

	// reopen and read all read bytes
	eeprom.i2cAddr_fd = wiringPiI2CSetup(EEPROM_ADDRESS);
	if (eeprom.i2cAddr_fd < 0) {
		printf("Error: Failed to initialize EEPROM at address 0x%X\n", EEPROM_ADDRESS);
		init = false;
		return EXIT_FAILURE;
	}
	// read
	for (int byte = 0; byte < eeprom.size; byte++) {
		int data = readMemoryByte16(eeprom.i2cAddr_fd, byte);
		if (data == -1) {
			printf("Error: data read failure at byte:%d\n",byte);
			init = false;
			break;
		}
		printf("READ: byte: %d data:%d \n", byte, data);
	}
	close(eeprom.i2cAddr_fd);
	
	if (init) printf("Successfully Read from all bytes\n");
	else return EXIT_FAILURE;
	return EXIT_SUCCESS;
}
