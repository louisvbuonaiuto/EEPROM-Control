/*
 * This program reads the factory programmed serial number from a single EEPROM device
 * assumes the serial number is stored on address 0x58. Use i2cdetect -y 1 to check addresses and refer to datasheet
 
use this code to confirm if the serial number is correct in the terminal
$ i2ctransfer -y 1 w1@0x58 0x80 r16@0x58

i2ctransfer // combines multiple i2c messages into one transfer 
w1@0x58		// writes 1 byte (w1) at(@) address 0x58
0x80		// the value to write @0x58
r16@0x58	// read 16 bytes @0x58

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

#define EEPROM_SERIAL_NUM_ADDRESS 0x58

void eepromClose(EEPROM* eeprom);


int main(void) 
{
	/*
	 * init eeprom connection
	 */
	EEPROM eeprom = {0};
	eeprom.i2cAddr_fd = wiringPiI2CSetup(EEPROM_SERIAL_NUM_ADDRESS);
	if (eeprom.i2cAddr_fd < 0) {
		fprintf(stderr,"Error: Failed to initialize EEPROM at address 0x%X\n", EEPROM_SERIAL_NUM_ADDRESS);
		eepromClose(&eeprom);
	}
	
	// read serial number
	char serial_number[SERIAL_NUMBER_LEN+1]; // + 1 for null terminator
	if (readSerialNumber(eeprom.i2cAddr_fd, serial_number) == 1) {
		fprintf(stderr,"Error: readSerialNumber failed\n");
		eepromClose(&eeprom);
	}
	close(eeprom.i2cAddr_fd);
	printf("Serial Number: %s\n", serial_number);
	return EXIT_SUCCESS;
}
