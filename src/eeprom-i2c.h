/*

functions for readings and writing data to an EEPROM

*/
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <wiringPi.h> // for delay()

// ansi color macros
#define ANSI_COLOR_RESET "\x1b[0m"
#define ANSI_COLOR_RED "\x1b[31m"
#define ANSI_COLOR_GREEN "\x1b[32m"

#define SERIAL_NUMBER_LEN 16 // length of EEPROM serial number in bytes


typedef struct {
	int size;				// size in bytes of EEPROM
	int i2cAddr_fd;			// file descriptor of the i2c
	size_t failures;		// how many times has this EEPROM experienced a flip
	uint8_t* priorState;	// addresses that we know have failed

	// types of bit flips
	size_t oneToZeroFlips;	// number of 0->1 bit flips
	size_t zeroToOneFlips;	// number of 1->0 bit flips
	size_t hardFaultBits;	// number of hardfaulting bits
} EEPROM;

int readSerialNumber(int fd, char *serial_number_ptr) {
	/*
	 * reads factory programmed serial number and writes the serial number to char *serial_number_ptr
	 */

	// according to the AT24CS01 datasheet a serial read requires a dummy write sequence before 
	// the serial number read. It turns out wiringPiI2CReadReg8 already does this so you just
	// have to read 16 bytes contiguously from the first byte of the serial number 0x80 or 80h (80 hex, idk why they write it like this in the data sheet
	// important note: The serial number is a combination of all the 16 bytes that are stored in memory to make one long hexidecimal number. To get the serial number you must
	// combine all of the bytes into one long hexadecimal number. reg1:0x09 reg2:0x40... Serialnumber 0x0940...
	uint8_t serial_bytes[SERIAL_NUMBER_LEN] = {0};
	for (uint16_t reg = 0; reg < SERIAL_NUMBER_LEN; reg++) {
		int value = wiringPiI2CReadReg8(fd, 0x80 + reg);
		if (value == -1) {
			fprintf(stderr, "Error: data read failure at reg: %d\n", reg);
			return EXIT_FAILURE;
		}
		//printf("reg: %d, value: %d\n",reg,value);
		serial_bytes[reg] = (uint8_t)value; // cast as uint8_t to keep hex value
	}

	// convert this serial_bytes to a character array with hex values concatennated
	for (int i = 0; i < SERIAL_NUMBER_LEN; i++) {
		snprintf(&serial_number_ptr[i * 2], 3, "%02X", serial_bytes[i]);
	}
	serial_number_ptr[SERIAL_NUMBER_LEN * 2] = '\0';
	return EXIT_SUCCESS;
}

void eepromClose(EEPROM* eeprom) {
	close(eeprom->i2cAddr_fd);
	exit(EXIT_FAILURE);
}

/*
 

int readMemoryByte16(int fd, uint16_t addr) {
	uint8_t addressBuffer[2];
	uint8_t dataBuffer[1];

	// split the 16 bit address into Big-Endian format (High Byte first)
	addressBuffer[0] = (memAddress >> 8) & 0xFF;
	addressBuffer[1] = memAddress & 0xFF;

	// tell eeprom which address to point to
	if (write(memFd, addressBuffer, 2) != 2) {
		fprintf(stderr, "EEPROM Read Pointer Setup Error at address 0x%04X: %s\n", memAddress, strerror(errno));
		return -1;
	}
	
	// read the actual byte resting at that address slot
	if (read(memFd, dataBuffer, 1) != 1) {
		printf("Error: Failed to read data byte from EEPROM\n");
		return -1;
	}
	
	// return successfully read byte
	return dataBuffer[0];
}

int writeMemoryByte16(int memFd, uint16_t memAddress, uint8_t data) {
	uint8_t buffer[3];

	// pack the 16 bit address in Big-Endian order
	buffer[0] = (memAddress >> 8) & 0xFF;	// high byte
	buffer[1] = memAddress & 0xFF;			// low byte
	
	// pack the data payload
	buffer[2] = data;

	// write all 3 bytes at once to the linux file handle
	if (write(memFd, buffer, 3) != 3) {
		fprintf(stderr, "EEPROM Write Error at address 0x%04X: %s\n", memAddress, strerror(errno));
		return -1;
	}
	delay(5); // EEPROM hardware writes need to sleep for 5ms
	
	return EXIT_SUCCESS;
}

// reads and returns factory programmed 128-bit (16-byte) unique serial number
*/
