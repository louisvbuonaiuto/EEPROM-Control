/*

functions for readings and writing data to an EEPROM

*/
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <wiringPi.h> // for delay()

// functions are static inline so they will be placed directly into the code once compiled
static inline int readMemoryByte16(int memFd, uint16_t memAddress) {
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

static inline int writeMemoryByte16(int memFd, uint16_t memAddress, uint8_t data) {
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
