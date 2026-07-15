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
#include "eeprom-i2c.h"

#define EEPROM_ADDRESS 0x50
#define EEPROM_SIZE 128 // bytes
#define VALUE 0x02


int main(void) 
{
	/*
	 * init eeprom connection
	 */
	EEPROM eeprom = {0};
	eeprom.size = EEPROM_SIZE;
	eeprom.i2cAddr_fd = wiringPiI2CSetup(EEPROM_ADDRESS);
	if (eeprom.i2cAddr_fd < 0) {
		printf("Error: Failed to initialize EEPROM at address 0x%X\n", EEPROM_ADDRESS);
		return EXIT_FAILURE;
	}
	/*
	 * initialize all bits in eeprom to 1 by setting each register to VALUE
	 */
	bool init = true;
	for (int reg = 0; reg < eeprom.size; reg++) {
		if (wiringPiI2CWriteReg8(eeprom.i2cAddr_fd, reg, VALUE) == -1) {
			printf("Error: Failed to write to EEPROM at register:%d\n", reg);
			init = false;
			break;
		}
		delay(5);	
		printf("WRITE: register: %d value: %d\n", reg, VALUE);
	}
	close(eeprom.i2cAddr_fd);
	
	if (init) printf("Successfully wrote to all bytes\n");	

	else return EXIT_FAILURE;

	/*
	 * reopen connection
	 */
	eeprom.i2cAddr_fd = wiringPiI2CSetup(EEPROM_ADDRESS);
	if (eeprom.i2cAddr_fd < 0) {
		printf("Error: Failed to initialize EEPROM at address 0x%X\n", EEPROM_ADDRESS);
		init = false;
		return EXIT_FAILURE;
	}

	/*
	 * read from eeprom
	 */
	for (int reg = 0; reg < eeprom.size; reg++) {
		int data = wiringPiI2CReadReg8(eeprom.i2cAddr_fd, reg);
		if (data == -1) {
			printf("Error: data read failure at reg:%d\n",reg);
			init = false;
			break;
		}
		printf("READ: byte: %d data:%d \n", reg, data);
	}
	close(eeprom.i2cAddr_fd);

	if (init) printf("Successfully Read from all registers\n");
	else return EXIT_FAILURE;
	return EXIT_SUCCESS;
}
