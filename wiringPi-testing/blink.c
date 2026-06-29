#include <stdio.h>
#include <wiringPi.h>

#define LED 11 // physical pin 11

int main(void) {

	printf("Raspberry Pi blink\n");

	// initialize using pysical pin numbering
	wiringPiSetupPinType(WPI_PIN_PHYS);	

	// pin mode ..(INPUT, OUTPUT, PWM_OUTPUT, GPIO_CLOCK)
  	// set pin 11 to OUTPUT
  	pinMode(LED, OUTPUT);	

	for(int i = 0; i < 5; i++) {
		digitalWrite(LED,HIGH);
		delay(200); 			// ms
		digitalWrite(LED,LOW);
		delay(200);
	}
	return 0;
}
