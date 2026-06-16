/*

    Updated & Better C Code for EEPROM Control for Research
	This includes advanced functionality:
	- Ability to count 1->0 and 0->1 bit flips
	- Ability to reinitialize any failed bits to good state
	- Ability to choose checkerboarding (FF00FF00) or uniform (all 00 or all FF) 

    August 21, 2024
    Author: Fraser Dougall 
	Email: fdougall@purdue.edu

*/

// Libraries
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>

//#include <wiringPi.h>
//#include <wiringPiI2C.h>

#include <time.h>
#include <stdbool.h>
#include <stdlib.h>

// Selector Pins
#define BANK_SELECT_1 0
#define BANK_SELECT_2 1

// EEPROM Banking Information
#define EEPROM_ADDRESS 0x50 // base EEPROM I2C address
#define NUM_BANKS 2
#define EEPROMS_PER_BANK 8
#define MAX_EEPROM_SIZE 512000 // maximum size we have

// How long to run the test - seconds
// default is 30 min -> 1800 seconds
#define RUNNING_TIME_SEC 1

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

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

int totalEEPROMs = NUM_BANKS * EEPROMS_PER_BANK; 

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Return how many bytes are stored in a given bank,EEPROM combo
int getEEPROMSize(int bank, int num) {
	int sizeKB;

	switch(bank){
		case 0: 
			if(num >= 0 && num <= 3) { 
				sizeKB = 1;
			} else {
				sizeKB = 512; 
			}

			break;
		case 1: 
			if(num >= 0 && num <= 3) { 
				sizeKB = 32;
			} else { 
				sizeKB = 128; 
			}

			break;
		case 2: 
			if(num >= 0 && num <= 3) { 
				sizeKB = 4;
			} else {
				sizeKB = -1; 
			} 

			break;
		default:
			return -1; 
			break;
	}

	// kilobytes -> bytes
	return 1000 * sizeKB; 
}

/*
   Initialize GPIO Pins
   */
   void initGPIO() {
   wiringPiSetup();

   pinMode(BANK_SELECT_1, OUTPUT);
   pinMode(BANK_SELECT_2, OUTPUT);
   }

/*
   Choose with bank of EEPROM we are looking at by changing which switch state we are at
   */
   void selectBank(int bank) {
   switch (bank) {
   case 0:
   digitalWrite(BANK_SELECT_1, LOW);
   digitalWrite(BANK_SELECT_2, LOW);

   break;
   case 1:
   digitalWrite(BANK_SELECT_1, HIGH);
   digitalWrite(BANK_SELECT_2, LOW);

   break;
   case 2:
   digitalWrite(BANK_SELECT_1, HIGH);
   digitalWrite(BANK_SELECT_2, HIGH);

   break;
   default:
   printf("Invalid bank number\n");

   break;
   }
   }

/* 
   Initialize all EEPROMs to have 0xFF in all memory locations
   */
void initEEPROMs(allEEPROMs* population) {
	// stores current eeprom
	EEPROM* current = NULL;

	for (int bank = 0; bank < NUM_BANKS; bank++) {
		selectBank(bank);

		for (int eeprom = 0; eeprom < EEPROMS_PER_BANK; eeprom++) {
			// grab current EEPROM from array
			current = (population->all) + (bank * EEPROMS_PER_BANK + eeprom); // this access is safe

			if(current == NULL){
				printf("EEPROM b%de%d is NULL during initEEPROMS!\n", bank, eeprom);
				break;
			}

			current->i2cAddr = wiringPiI2CSetup(EEPROM_ADDRESS + eeprom);


			// make sure EEPROM exists before accessing
			if (current->i2cAddr < 0) {
				printf("Failed to initialize EEPROM %d in bank %d\n", eeprom, bank);

				current->failures = -1; // this will underflow
			} else {
				current->size = getEEPROMSize(bank, eeprom);
				bool init = true;

				// Linearly initialize all locations in EEPROM ; wish this was faster but impossible for better than O(n)
				for (int num = 0; num < current->size; num++) {
					   if (wiringPiI2CWrite(current->i2cAddr, 0xFF) == -1) {
					   printf("Failed to write to EEPROM %d in bank %d\n", eeprom, bank);
					   init = false;

					   break;
					   } 		
				}

				if(init) {
					// malloc our saved addresses array
					current->priorState = calloc(current->size, sizeof(current->priorState));

					printf("Initialized EEPROM %d in bank %d\n", eeprom, bank);
				}

				close(current->i2cAddr);
			}
		}
	}
}

void logger(time_t startTime, int greedy, int boardNum, FILE* csv_file, allEEPROMs* population) {
	time_t currTime = 0;
	int elapsedTime = 0; 
	int eepromSize = 0; 

	EEPROM* current = NULL;

	// Go through all EEPROMs and banks 
	for (int bank = 0; bank < NUM_BANKS; bank++) {
		selectBank(bank); 

		for (int eeprom = 0; eeprom < EEPROMS_PER_BANK; eeprom++) {
			// Get current EEPROM from total population
			current = (population->all) + (bank * EEPROMS_PER_BANK + eeprom);

			if(current == NULL){
				printf("EEPROM b%de%d is NULL during log time!\n", bank, eeprom);
				break;
			}

			// make sure the EEPROM is still alive
			current->i2cAddr = wiringPiI2CSetup(EEPROM_ADDRESS + eeprom); 

			// make sure our EEPROM actually exists lol 
			if(current->i2cAddr >= 0) {
				for (int byte = 0; byte < current->size; byte++) {     
					// TODO: read from the specific location
//					uint8_t data = wiringPiI2CRead(current->i2cAddr);

					uint8_t data = wiringPiI2CReadReg8(current->i2cAddr, byte);

					if(data != (current->priorState)[byte] && data > 0){
						current->failures = current->failures + 1; 
						
						for(uint8_t i = 0; i<8; i++){
							// extract the bit we are looking at
							uint8_t currBit = (data & (1 << i)) >> i; 
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
						
						// restore to prior state (this should be okay but verify later)
						wiringPiI2CWriteReg8(current->i2cAddr, byte, (current->priorState)[byte]);

						// update our prior state
						(current->priorState)[byte] = data; 
					} else{
						printf("Failed to read from EEPROM b%de%d\n");
					}
				}

				// get current time and calculate how long since we've started
				currTime = time(NULL); 
				elapsedTime = difftime(currTime, startTime); 

				// check if we are above our elapsed time limit & break here?

				// Log to CSV file 
				fprintf(csv_file, "%d, %d, %d, %d\n", elapsedTime, bank, eeprom, current->failures);

				close(current->i2cAddr); 
			} else {
				// this'll just underflow since failures is unsigned
				current->failures = -1337; // since it doesn't exist 
			}
		}
	}
}


int main() {
	initGPIO();

	// illusion of choice ^-^ 

	printf("Board number: ");
	int num; 
	scanf("%d", &num); // todo: maybe do some error handling here?

	time_t ctime = time(NULL); 

	char filename[50];

	sprintf(filename, "board %d data.csv", num); // this is susceptible to buffer overflow if num is exceptionally large
	FILE *csv_file = fopen(filename, "a");

	if (csv_file == NULL) {
		printf("Failed to open CSV file\n");
		return -1;
	}

	fprintf(csv_file, "Elapsed Time, Bank, EEPROM, Failures\n");
	fclose(csv_file);
	fopen(filename, "a");

	// malloc our entire EEPROM handler
	allEEPROMs* population = (allEEPROMs*) malloc(sizeof(*population));

	// malloc storage of all our eeprom structs
	population->all = (EEPROM*) calloc(totalEEPROMs, sizeof(EEPROM));

	// Initialize everything
	initEEPROMs(population);

	// reset
	ctime = time(NULL); 

	printf("it's logging time\n");

	// Continuously log data - no sleep needed since takes time to read EEPROMs
	//
	// TODO: more finegrained control over how long we're running? (run exactly for 30 seconds)
	// if so need to figure out what timers we have access to on Pi
	while ( (int)(difftime(time(NULL), ctime)) <= RUNNING_TIME_SEC ) {
		logger(ctime, 0, num, csv_file, population);
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

	return 0;
}
