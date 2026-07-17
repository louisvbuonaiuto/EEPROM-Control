/*
    Test connection between PI and single EEPROM with a read and write test
    User specifies EEPROM size in bytes and value to write all registers to
	EX: $ ./read-write-test <EEPROM-SIZE-BYTES> <VALUE>
	Note:
		This program assumes EEPROM_ADDRESS = 0x50
		This program doesn't know the memory size of the EEPROM and if it is entered incorrectly it will look like it worked but won't. 
		check eeprom memory with $ i2cdump -y 1 0x50
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

// TODO currently this program is limited by the wiringPi library. The two functions that are used only
// send a maximum of 255 for device address bit. This works for at24cso01 since there is 128 bytes but 
// anything greater than that will not be able to be addressed. I need to make a function for sending multiple address bytes followed by awk for i2c

int main(int argc, char* argv[]) {
	if (argc != 3) {
		printf("Error: Incorrect arguments, $ ./read-write-test <EEPROM-SIZE_BYTES> <VALUE>\n");
		return(EXIT_FAILURE);
	}
	
	uint8_t EEPROM_SIZE = atoi(argv[1]);
	uint8_t VALUE = atoi(argv[2]);

	uint8_t read_values[EEPROM_SIZE];

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
	 * initialize all bits in eeprom by setting each register to VALUE
	 */
	bool init = true;
	for (int reg = 0; reg < eeprom.size; reg++) {
		if (wiringPiI2CWriteReg8(eeprom.i2cAddr_fd, reg, VALUE) < 0) {
			fprintf(stderr,"\nError: Failed to write to EEPROM at register:%d\n", reg);
			init = false;
			break;
		}
		delay(5);	
		printf("\rWRITE: register: %d value: %d", reg, VALUE);
		fflush(stdout);
	}
	close(eeprom.i2cAddr_fd);	
	if (init) printf("\nFinished writing to %d bytes\n",EEPROM_SIZE);	

	else return EXIT_FAILURE;

	/*
	 * reopen connection
	 */
	eeprom.i2cAddr_fd = wiringPiI2CSetup(EEPROM_ADDRESS);
	if (eeprom.i2cAddr_fd < 0) {
		printf("\nError: Failed to initialize EEPROM at address 0x%X\n", EEPROM_ADDRESS);
		init = false;
		return EXIT_FAILURE;
	}

	/*
	 * read from eeprom
	 */
	for (int reg = 0; reg < eeprom.size; reg++) {	
		int data = wiringPiI2CReadReg8(eeprom.i2cAddr_fd, reg);
		if (data < 0) {
			printf("\nError: data read failure at reg:%d\n",reg);
			init = false;
			break;
		}
		printf("\rREAD: byte: %d data:%d", reg, data);
		read_values[reg] = data;
		fflush(stdout);
	}
	close(eeprom.i2cAddr_fd);

	if (init) printf("\nFinished reading from all %d registers\n",EEPROM_SIZE);
	else return EXIT_FAILURE;

	// check if read matches write
	// its possible for the functions from wiringPi to return without error without successfully read/write data
	bool read_matches_write = true;
	for (int byte = 0; byte < EEPROM_SIZE; byte++) {
		if (read_values[byte] != VALUE) {
			read_matches_write = false;
			break;
		}
	}

	if (read_matches_write) {
		printf(ANSI_COLOR_GREEN"Success: "ANSI_COLOR_RESET"Write and Read match\n\n");
		return EXIT_SUCCESS;
	} else { 
	fprintf(stderr, "\nError: Write does not match Read values\n");
	return EXIT_SUCCESS;
	}
}
