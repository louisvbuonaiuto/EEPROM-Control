#include <stdio.h>
#include <wiringPi.h>

int main(void) {

    printf("Raspberry Pi -- 4 pin led sequence\n");  

    // initialize with wiring pi numbering
    wiringPiSetupPinType(WPI_PIN_WPI);

    // set pin modes
    for (int pin = 0; pin < 4; pin++) {
        pinMode(pin,OUTPUT);
    }

    for(int i = 0; i < 4; i++) {
		digitalWrite(i,HIGH);
		delay(200); 			// ms
		digitalWrite(i,LOW);
		delay(200);
	}


    return 0;
}