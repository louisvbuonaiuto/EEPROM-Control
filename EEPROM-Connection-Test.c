/*
    Test connection between PI and single EEPROM with a read and write test
    - 

*/

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>

#include <wiringPi.h>
#include <wiringPiI2C.h>

#define EEPROM_ADDR 0x50
#define MEM_ADDR    0x00

int main(void) 
{
   wiringPiSetupPinType(WPI_PIN_BCM); // PIN numbering: WiringPi-Numbering

    // I2C Bus write and read test
    int fd = wiringPiI2CSetup(EEPROM_ADDR);

    if (fd < 0) {
        fprintf(stderr, "Error: Wiring Pi Setup Failed\n");
        return EXIT_FAILURE;
        }

    uint8_t writeData = 0x55;

    /* Write one byte to EEPROM location 0x00 */
    if (wiringPiI2CWriteReg8(fd, MEM_ADDR, writeData) < 0)
    {
        fprintf(stderr, "EEPROM write failed.\n");
        return EXIT_FAILURE;
    }
    

    /* Wait for EEPROM write cycle */
    usleep(10000);      // 10 ms

    /* Read it back */
    int readData = wiringPiI2CReadReg8(fd, MEM_ADDR);

    if (readData < 0)
    {
        fprintf(stderr, "EEPROM read failed.\n");
        return EXIT_FAILURE;
    }

    printf("Wrote : 0x%02X\n", writeData);
    printf("Read  : 0x%02X\n", readData & 0xFF);

    if ((readData & 0xFF) == writeData)
        printf("PASS\n");
    else
        printf("FAIL\n");

    return EXIT_SUCCESS;
}

