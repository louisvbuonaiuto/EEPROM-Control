/*
    Test connection between PI and single EEPROM with a read and write test
    - 

*/

#include <stdio.h>
#include <stdlib.h>

#include <wiringPi.h>
#include <wiringPiI2C.h>

#define GPIO2 8
#define GPIO3 9

#define I2C_ADDR 0x20
void initGPIO(void)
{
   wiringPiSetupPinType(WPI_PIN_WPI); // PIN numbering: WiringPi-Numbering
}

int main(void) 
{
    initGPIO();

    // sets pin modes
    pinMode(GPIO2, OUTPUT);
    pinMode(GPIO3, OUTPUT)

    digitalWrite(GPIO2, HIGH);
    digitalWrite(GPIO3, HIGH)

    // I2C Bus write and read test
    int fd = wiringPiI2CSetup(I2C_ADDR)

    if (fd < 0) {
        fprintf(stderr, "Error: Wiring Pi Setup Failed\n");
        return EXIT_FAILURE;
        }

    uint8_t i2cvalue = 0x55;
    int result = wiringPiI2CRawWrite(fd, &i2cvalue, 1);

    if (result != 1) {
        fprintf(stderr, "Error: Data writing failure\n");
        return EXIT_FAILURE;
        }
    
    // 1 byte from i2cvalue sent to I2C_ADDR slave 
    // now read that data
    result = wiringPiI2CRawRead(fd,&i2cvalue, 1);
    if (result != 1) {
        fprintf(stderr, "Error: Data reading failure\n");
        return EXIT_FAILURE;
        }

    printf("i2cvalue: %d\n", i2cvalue);

    return EXIT_SUCCESS;
}

