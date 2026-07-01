/*

    Updated & Better C Code for EEPROM Control for Research
	This includes advanced functionality:
	- Ability to count 1->0 and 0->1 bit flips
	- Ability to reinitialize any failed bits to good state
	- Ability to choose checkerboarding (FF00FF00) or uniform (all 00 or all FF) 

    August 21, 2024
    Author: Fraser Dougall 
	Email: fdougall@purdue.edu

	Updated
	Jun 30, 2026
    Author: Louis V. Buonaiuto
	Email: lovbuona@iu.edu, louis107@comcast.net

*/

// Libraries
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>

#include <wiringPi.h>
#include <wiringPiI2C.h>

#include <time.h>
#include <stdbool.h>
#include <stdlib.h>


// EEPROM Banking Information
#define EEPROM_ADDRESS 0x50 // base EEPROM I2C address
#define MAX_EEPROM_SIZE 512000 // maximum size we have

// How long to run the test - seconds
// default is 30 min -> 1800 seconds
#define RUNNING_TIME_SEC 5

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
typedef struct {
	int size;             // size in bytes of eeprom
	size_t failures;      // how many times has this EEPROM experienced a flip
	int i2cAddr;          // where on the i2c bus is it
	uint8_t* priorState;  // prior state of the EEPROM
	size_t oneToZeroFlips;// number of 1->0 bit flips
	size_t zeroToOneFlips;// number of 0->1 bit flips
	size_t hardFaultBits; // number of hardfaulting bits
} EEPROM; 

typedef struct {
	EEPROM* all;
} allEEPROMs; 


/*** Initialize GPIO Pins ***/

void initGPIO() 
	{
   	wiringPiSetupPinType(WPI_PIN_BCM); // PIN numbering: WiringPi-Numbering

	}


// remove this when testing on physical hardware
//int correctValue = 0x0F;

void logger(time_t startTime, int greedy, int boardNum, FILE* csv_file, allEEPROMs* population) {
	time_t currTime = 0;
	int elapsedTime = 0; 
	int eepromSize = 0; 

	EEPROM* current = NULL;

	// Go through all EEPROMs and banks 
	for (int bank = 0; bank < NUM_BANKS; bank++) {
		//selectBank(bank); 

		for (int eeprom = 0; eeprom < EEPROMS_PER_BANK; eeprom++) {
			// Get current EEPROM from total population
			current = (population->all) + (bank * EEPROMS_PER_BANK + eeprom);
			// current->i2cAddr = wiringPiI2CSetup(EEPROM_ADDRESS + eeprom); 

			// make sure our EEPROM actually exists lol 
			if(current->i2cAddr >= 0) {
				for (int byte = 0; byte < current->size; byte++) {     
					//int data = wiringPiI2CRead(current->i2cAddr);

					// IN THIS CONTEXT correctValue == data so should be drop in replacement
					if(correctValue != (current->priorState)[byte] && correctValue > 0){
						current->failures = current->failures + 1; 

						uint8_t theByte = correctValue; 
						
						for(uint8_t i = 0; i<8; i++){
							// extract the bit we are looking at
							uint8_t currBit = (theByte & (1 << i)) >> i; 
							uint8_t priorBit= ((current->priorState)[byte] & (1 << i)) >> i; 

							// looks at 1->0 transitions 
							if(priorBit == 1 && currBit == 0){
								current->oneToZeroFlips += 1; 
							}

							// looks at 0->1 transitions
							if(priorBit == 0 && currBit == 1){
								current->zeroToOneFlips += 1; 
							}
						}
						
						// implement byte fixing here?

						// update our prior state
//						(current->priorState)[byte] = (uint8_t) correctValue; 
					} 
				}
				// get current time and calculate how long since we've started
				currTime = time(NULL); 
				elapsedTime = difftime(currTime, startTime); 

				// Log to CSV file 
				fprintf(csv_file, "%d, %d, %d, %zu\n", elapsedTime, bank, eeprom, current->failures);

				close(current->i2cAddr); 
			} else {
				current->failures = -1337; // since it doesn't exist 
			}
		}
	}

	if(correctValue == 0x0F){correctValue = 0xAB;}
	else{correctValue = 0x0F;}

}


int main(void) {
	//    initGPIO();

	// illusion of choice ^-^ 

	int board_num = -1;
	printf("Enter Board number: ");
	scanf("%d", &board_num); // todo: maybe do some error handling here?
	if (board_num < 0) {
		printf("Invalid Board Number\n");
		return EXIT_FAILURE;
	}

	/*** file handling ***/
	char filename[50];

	sprintf(filename, "./data/board-%d-data.csv", board_num); // this is susceptible to buffer overflow if board_num is exceptionally large
	FILE* csv_file = fopen(filename, "a");

	if (csv_file == NULL) {
		printf("Failed to open CSV file\n");
		return EXIT_FAILURE;
	}

	fprintf(csv_file, "Elapsed Time,Bank,EEPROM,Failures\n");
	fclose(csv_file);

	// malloc our entire EEPROM handler
	allEEPROMs* population = (allEEPROMs*) malloc(sizeof(*population));

	// malloc storage of all our eeprom structs
	population->all = (EEPROM*) calloc(totalEEPROMs, sizeof(EEPROM));

	// Initialize everything
	initEEPROMs(population);


	/*** data logging ***/
	time_t ctime;
	printf("it's logging time\n");

	// Continuously log data - no sleep needed since takes time to read EEPROMs
	//
	// TODO: more finegrained control over how long we're running? (run exactly for 30 seconds)
	// if so need to figure out what timers we have access to on Pi
	
	// reset time
	ctime = time(NULL);

	fopen(filename, "a");
	while ( (int)(difftime(time(NULL), ctime)) <= RUNNING_TIME_SEC ) {
		logger(ctime, 0, board_num, csv_file, population);
		fclose(csv_file); // update file & flush buffer
		fopen(filename, "a");
	}

	// Close & Free all allocated stuff
	fclose(csv_file);

	// I am Become Free, Destroyer of EEPROMS
	// TODO: need to verify we're actually freeing everything (no valgrind installed :( )
	for(int i = 0; i < totalEEPROMs; i++){
		EEPROM* curr = (population->all) + i;

		if(curr != NULL && curr->priorState != NULL){
			free(curr->priorState);
		}
	}

	free(population->all);
	free(population);

	printf("Completed and written to file.\n"); 

	return EXIT_SUCCESS;
}
