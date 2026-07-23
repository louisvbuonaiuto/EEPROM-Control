/*
 * eeprom-bit-test.c
 * This program sets the bits in an eeprom and records if any changes happens in them to a csv file
 *
 */

//========================================
// eeprom-bit-test.c
//========================================
// SSHM -> SOIC
// XHM  -> TSSOP
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <time.h>
#include <stdbool.h>
#include <stdlib.h>

#include <wiringPi.h>
#include <wiringPiI2C.h>
#include "eeprom-i2c.h"

#define EEPROM_ADDRESS 0x50
#define EEPROM_SERIAL_NUM_ADDRESS 0x58
#define EEPROM_SIZE 128	// in bytes
#define WRITE_DELAY 5	// ms, refer to datasheet for max write cycle time
#define RUNNING_TIME_SEC 86400 //86400= 24 hours 43200 seconds=12 hours 
#define INIT_BYTES_VALUE 0xAA	// 0x55 01010101, 0xAA 10101010


#define POLL_INTERVAL_MS 1000			// wait this long between logger() passes
#define CHECKPOINT_INTERVAL_SEC 300	// how often to dump a full per-bit snapshot (5 min)

void initEEPROM(EEPROM* eeprom) {

	eeprom->size = EEPROM_SIZE;
	eeprom->capacity = 16;	// initial capacity for dynamic array
	eeprom->maxHardFaultBits = (size_t)eeprom->size * 8;

	// allocate memory tracking structures
	eeprom->priorState   = (uint8_t*)malloc(eeprom->size * sizeof(uint8_t));
	eeprom->flippedBitsPtr = (BitRecord*)malloc(eeprom->capacity * sizeof(BitRecord));
	eeprom->bitLookup    = (int*)malloc(eeprom->size * 8 * sizeof(int));

	// NULL checks for all three allocations
	if (eeprom->priorState == NULL || eeprom->flippedBitsPtr == NULL || eeprom->bitLookup == NULL) {
		printf(ANSI_COLOR_RED"Error: "ANSI_COLOR_RESET"failed to allocate EEPROM tracking structures\n");
		eeprom->i2cAddr_fd = -1; // not opened yet, tell eepromClose() to skip close()
		eepromClose(eeprom);
	}

	// init lookup table to -1 (meaning bit has no history yet)
	for (int i = 0; i < (eeprom->size * 8); i++) {
		eeprom->bitLookup[i] = -1;
	}

	/* Intialize EEPROM Connection
	 * wiringPiI2CSetup() is a function that initializes an I2C device by passing its device address
	 * This address can be found with "$ i2detect -y 1"
	 * The functions return value is the standard Linux filehandle, -1 if any error
	 * -1 is for linux file handle and wiringPi also returns -1 for failures
	 */
	eeprom->i2cAddr_fd = wiringPiI2CSetup(EEPROM_ADDRESS);

	if (eeprom->i2cAddr_fd < 0) {
		printf(ANSI_COLOR_RED"Error: "ANSI_COLOR_RESET"Failed to initialize EEPROM at address 0x%X\n", EEPROM_ADDRESS);
		eepromClose(eeprom);
	}

	// initialize all bits in EEPROM to 1 by setting each byte to INIT_BYTES_VALUE.

	bool init = true;
	for (int reg = 0; reg < eeprom->size; reg++) {
		if (wiringPiI2CWriteReg8(eeprom->i2cAddr_fd, reg, INIT_BYTES_VALUE) < 0) {
			printf(ANSI_COLOR_RED"Error: "ANSI_COLOR_RESET"Failed to write to EEPROM at register: %d\n", reg);
			init = false;
			break;
		}
		delay(WRITE_DELAY);
		eeprom->priorState[reg] = (uint8_t)INIT_BYTES_VALUE;
	}

	if (init == false) eepromClose(eeprom);

	printf(ANSI_COLOR_GREEN"---- EEPROM Intialized ----"ANSI_COLOR_RESET
	"\nSet %d bytes to %d\n", EEPROM_SIZE, INIT_BYTES_VALUE);
}

// returns true only if every bit flagged in mismatchMask already has
// hardFault == true recorded for it. Used to skip a pointless restore/verify
// cycle on a byte whose flipped bits are all already known to be stuck
bool allFlaggedBitsAreHardFault(EEPROM* eeprom, int reg, uint8_t mismatchMask) {
	for (uint8_t i = 0; i < 8; i++) {
		if (mismatchMask & (1 << i)) {
			int idx = eeprom->bitLookup[(reg * 8) + i];
			// if a flagged bit somehow has no record yet, or isn't marked
			// hardFault, we can't skip -- treat conservatively as "not all faulted"
			if (idx == -1 || !eeprom->flippedBitsPtr[idx].hardFault) {
				return false;
			}
		}
	}
	return true;
}

// writes a full snapshot of every BitRecord collected so far to a
// separate CSV. Called periodically (every CHECKPOINT_INTERVAL_SEC) from
// main() rather than from inside logger(), since it's a time-based
// checkpoint, not a per-register or per-pass action.
//
// This exists so a 12-hour unattended run that crashes or loses power
// partway through doesn't lose the detailed per-bit data (which bit,
// how many times, hard fault status, first/last flip time) -- only the
// aggregate CSV columns would otherwise survive a crash. The file is
// fully overwritten each checkpoint (not appended), since it's meant to
// always reflect the most current complete state, not a running history.
void writeBitRecordSnapshot(EEPROM* eeprom, const char* filename) {
	FILE* snapshot = fopen(filename, "w"); // "w" intentional: overwrite with latest full state
	if (snapshot == NULL) {
		printf(ANSI_COLOR_RED"Error: "ANSI_COLOR_RESET"failed to open checkpoint file %s\n", filename);
		return; // non-fatal -- don't abort a 12 hour run just because a checkpoint write failed
	}

	fprintf(snapshot, "Byte Offset, Bit Index, Hard Fault, Times Flipped, First Flip Time (s), Last Flip Time (s)\n");
	for (size_t n = 0; n < eeprom->numBitFlips + eeprom->numHardFaultBits; n++) {
		BitRecord* r = &eeprom->flippedBitsPtr[n];
		fprintf(snapshot, "%zu, %zu, %s, %zu, %ld, %ld\n",
			r->byteOffset, r->bitIndex, r->hardFault ? "TRUE" : "FALSE",
			r->timesFlipped, (long)r->firstFlipTime, (long)r->lastFlipTime);
	}

	fclose(snapshot);
}

// This function is called exactly ONCE per byte that had a mismatch, after
// all 8 bits have been scanned and classified. It:
//   - writes the whole byte back to the reference (priorState) value
//   - waits for the EEPROM's write cycle to complete
//   - reads the byte back and checks every bit that was flagged in mismatchMask
//   - retries up to RESTORE_VERIFY_RETRIES times if verification fails, since
//     a fresh SEU could land on the same bit in the tiny window between the
//     restore write and the verify read, which would otherwise be
//     misclassified as a hard fault after only one failed attempt
//   - only sets hardFault = true once verification has failed on every retry
//
// Returns true if the byte is confirmed to match priorState by the end
// (whether that took 1 attempt or several), false if a transport-level
// write/read failure prevented verification entirely (distinct from a
// hard fault -- see comments inline).
bool restoreAndVerifyByte(EEPROM* eeprom, int reg, uint8_t mismatchMask, time_t elapsedTime) {
	(void)elapsedTime; // currently unused; kept in the signature so a hard-fault
	                    // confirmation log line could record the timestamp later
	for (int attempt = 0; attempt < RESTORE_VERIFY_RETRIES; attempt++) {

		// write the whole byte back to the known-good reference value.
		// NOTE: this is a *transport* failure path (I2C write call itself
		// failed) -- distinct from a hard fault, which is when the write
		// call succeeds but the value doesn't stick. We don't set
		// hardFault here, since we have no evidence the bit is physically
		// stuck, only that this particular I2C transaction didn't go through.
		if (wiringPiI2CWriteReg8(eeprom->i2cAddr_fd, reg, eeprom->priorState[reg]) < 0) {
			printf(ANSI_COLOR_RED"Error: "ANSI_COLOR_RESET
				"I2C transport failure while restoring EEPROM at register: %d (attempt %d/%d)\n",
				reg, attempt + 1, RESTORE_VERIFY_RETRIES);
			continue; // try again rather than immediately declaring a hard fault
		}

		delay(WRITE_DELAY);

		int verifyData = wiringPiI2CReadReg8(eeprom->i2cAddr_fd, reg);
		if (verifyData < 0) {
			// read-back itself failed -- also a transport issue, not proof of a hard fault
			eeprom->readFailures++;
			printf(ANSI_COLOR_RED"Error: "ANSI_COLOR_RESET
				"Failed to read back register %d during verification (attempt %d/%d)\n",
				reg, attempt + 1, RESTORE_VERIFY_RETRIES);
			continue;
		}

		if ((uint8_t)verifyData == eeprom->priorState[reg]) {
			return true; // restore verified successfully, byte matches priorState
		}

		// verifyData didn't match priorState -- figure out which of the
		// originally-flagged bits are still wrong on this attempt
		for (uint8_t i = 0; i < 8; i++) {
			if (mismatchMask & (1 << i)) {
				uint8_t verifyBit = (verifyData >> i) & 1;
				uint8_t refBit    = (eeprom->priorState[reg] >> i) & 1;
				if (verifyBit != refBit && attempt == RESTORE_VERIFY_RETRIES - 1) {
					// only declare a hard fault after the LAST retry has
					// also failed to restore this bit -- a single failed
					// attempt is not strong enough evidence on its own
					int globalIndex = (reg * 8) + i;
					int lIndex = eeprom->bitLookup[globalIndex];
					if (lIndex != -1 && !eeprom->flippedBitsPtr[lIndex].hardFault) {
						eeprom->flippedBitsPtr[lIndex].hardFault = true;
						eeprom->numHardFaultBits++;
						printf(ANSI_COLOR_RED"HARD FAULT: "ANSI_COLOR_RESET
							"byte %d bit %d confirmed stuck after %d attempts\n",
							reg, i, RESTORE_VERIFY_RETRIES);
					}
				}
			}
		}
		// loop again if attempts remain
	}
	return false; // verification failed and bit is stuck
}

// Clogger() now takes an additional FILE* firstFlip_csv parameter.
// Whenever a bit is seen to flip for the very first time (i.e. it's getting
// a brand new BitRecord created, lookupIndex was -1), a row is appended
// immediately to this file recording the bit's address (byte offset + bit
// index) and the time it was first observed. This answers the request to
// track/record a bit's address the first time it flips -- written straight
// to its own CSV on the Pi's filesystem as it happens, rather than only
// living in the in-memory BitRecord array (which, per the checkpointing
// discussion above, could otherwise be lost on a crash).
void logger(time_t startTime, FILE* csv_file, FILE* firstFlip_csv, EEPROM* eeprom) {
	time_t currTime = 0;
	int elapsedTime = 0;

	/*
	pseudocode for bit flip checking

	for each register:
		compute elapsedTime FIRST (fixed: previously this was computed at the
			bottom of the loop, so BitRecord timestamps and the restoreAndVerifyByte
			call were using elapsedTime left over from the PREVIOUS register,
			and register 0 always recorded elapsedTime == 0 no matter when it
			actually happened)
		read register
		if read failed: log read failure, skip register, continue
		compare register to priorState
		if mismatch:
			for each of the 8 bits in the byte:
				if bit differs from priorState:
					record it in mismatchMask
					look up (or create) its BitRecord via bitLookup
					if newly created: append (byteOffset, bitIndex, elapsedTime) to firstFlip_csv
					if not already hardFault: increment numBitFlips + directional counter,
						update firstFlipTime/lastFlipTime
			# ONLY after all 8 bits have been scanned:
			if not all flagged bits are already known hard faults:
				call restoreAndVerifyByte() ONCE for the whole byte
			# else: skip the restore/verify entirely, outcome is already known
		log CSV row for this register
	*/
	//===================================
	// Check for bit flips and types 
	//===================================
	int data = -1;	// data stores the value in the read register

	// read each register
	for (int reg = 0; reg < eeprom->size; reg++) {

		// FIX: elapsedTime is now computed at the TOP of the loop, before the
		// read, so every timestamp recorded during this iteration (BitRecord
		// first/lastFlipTime, the firstFlip_csv row, the restoreAndVerifyByte
		// call, and the per-register CSV row) reflects the actual moment this
		// specific register was checked -- not a stale value left over from
		// whichever register was processed just before it.
		currTime = time(NULL);
		elapsedTime = (int) difftime(currTime, startTime);

		data = wiringPiI2CReadReg8(eeprom->i2cAddr_fd, reg);	// store read value into data

		if (data < 0) { // failed to read
			eeprom->readFailures++; 
			printf(ANSI_COLOR_RED"ERROR: "ANSI_COLOR_RESET"failed to read from EEPROM at register: %d data: %d eeprom->priorState: %d\n",
				reg, data, eeprom->priorState[reg]);
			continue; // skip this register this pass
		}

		// successful read, data >= 0 
		// compare read byte to priorState of byte
		if ((uint8_t)data != eeprom->priorState[reg]) {
			// read byte doesn't match priorState -- scan all 8 bits first,
			// build up mismatchMask, THEN do a single restore+verify for
			// the whole byte (moved out of this loop -- see restoreAndVerifyByte)
			uint8_t mismatchMask = 0;

			for (uint8_t i = 0; i < 8; i++) {
				uint8_t currBit  = (data & (1 << i)) >> i;
				uint8_t priorBit = (eeprom->priorState[reg] & (1 << i)) >> i;

				if (currBit != priorBit) { // found a bit that flipped
					mismatchMask |= (1 << i); // track which bit(s) flipped for verification later

					int globalBitIndex = (reg * 8) + i;
					int lookupIndex = eeprom->bitLookup[globalBitIndex];
					BitRecord* currentBitRecord = NULL;

					// if bit has no history, create new record
					if (lookupIndex == -1) {
						if (eeprom->numBitFlips + eeprom->numHardFaultBits >= eeprom->capacity) {
							size_t newCapacity = eeprom->capacity * 2;
							BitRecord* resized = (BitRecord*)realloc(eeprom->flippedBitsPtr, newCapacity * sizeof(BitRecord));
							if (resized == NULL) {
								printf(ANSI_COLOR_RED"Error: "ANSI_COLOR_RESET
									"realloc failed while growing flippedBitsPtr (capacity %zu)\n", newCapacity);
								eepromClose(eeprom); // safe: frees whatever is still valid
							}
							eeprom->flippedBitsPtr = resized;
							eeprom->capacity = newCapacity;
						}
						lookupIndex = (int)(eeprom->numBitFlips + eeprom->numHardFaultBits);
						eeprom->bitLookup[globalBitIndex] = lookupIndex;
						currentBitRecord = &eeprom->flippedBitsPtr[lookupIndex];

						currentBitRecord->byteOffset   = reg;
						currentBitRecord->bitIndex     = i;
						currentBitRecord->hardFault     = false;
						currentBitRecord->timesFlipped  = 0;		
						currentBitRecord->firstFlipTime = elapsedTime;
						currentBitRecord->lastFlipTime  = elapsedTime;

						// ADDED: this bit has never flipped before -- record its
						// address (byte offset + bit index) and the time of this,
						// its first observed flip, to the dedicated first-flip CSV.
						// Written immediately (and flushed) so this record survives
						// even if the program crashes later in the run.
						fprintf(firstFlip_csv, "%d, %d, %d\n", elapsedTime, reg, i);
						fflush(firstFlip_csv);
					} else {
						currentBitRecord = &eeprom->flippedBitsPtr[lookupIndex];
					}

					// only increment flips if the bit isn't already known to be stuck
					if (currentBitRecord->hardFault == false) {
						currentBitRecord->timesFlipped++;
						currentBitRecord->lastFlipTime = elapsedTime;
						if (priorBit == 1 && currBit == 0) eeprom->oneToZeroFlips += 1;
						if (priorBit == 0 && currBit == 1) eeprom->zeroToOneFlips += 1;
						eeprom->numBitFlips += 1; 
					}
				}
			}

			// FIX/ADDED: only call restoreAndVerifyByte if at least one of the
			// flagged bits isn't already a confirmed hard fault. Once a bit is
			// known stuck, repeating the write+verify cycle on it every single
			// pass for the rest of a 12 hour run just burns I2C bandwidth on an
			// outcome you already know -- this can matter a lot once hundreds or
			// thousands of bits have gone hard-fault later in a long run.
			if (!allFlaggedBitsAreHardFault(eeprom, reg, mismatchMask)) {
				restoreAndVerifyByte(eeprom, reg, mismatchMask, (time_t)elapsedTime);
			}
		}

		// log to .CSV file
		fprintf(csv_file, "%d, %zu, %zu, %zu, %zu, %zu\n",
			elapsedTime, eeprom->numBitFlips, eeprom->oneToZeroFlips,
			eeprom->zeroToOneFlips, eeprom->numHardFaultBits, eeprom->readFailures);

		// monitor test
		printf("\rTime:%-6d byte:%-6d data:%-3d Bit Flips:%-6zu 1->0:%-5zu 0->1:%-5zu, Hard Faults:%-6zu Read Fails:%-5zu ",
			elapsedTime, reg, data, eeprom->numBitFlips, eeprom->oneToZeroFlips,
			eeprom->zeroToOneFlips, eeprom->numHardFaultBits, eeprom->readFailures);
		fflush(stdout);
	}
}

int main(void) {

	//==================================
	// user confirmation before test
	//==================================
	char confirm = 'n';
	// FIX: RUNNING_TIME_SEC/60 gave 720 "minutes", which is correct but no
	// longer a natural unit to confirm against at this scale -- switched to
	// hours (RUNNING_TIME_SEC/3600) so the confirmation prompt matches how
	// you actually think about a 12 hour run.
	printf("This file is setup for the AT24CS01-XHM-B, %d bytes, to run for %d hours \n", EEPROM_SIZE, RUNNING_TIME_SEC/3600);
	printf("Confirm? (y,N): ");
	if (scanf(" %c", &confirm) != 1 || confirm != 'y') {
		return EXIT_FAILURE;
	}
	delay(1000); // 1 second delay

	//===============================
	// read serial number
	//===============================
	EEPROM serial_eeprom = {0};
	serial_eeprom.i2cAddr_fd = wiringPiI2CSetup(EEPROM_SERIAL_NUM_ADDRESS);
	if (serial_eeprom.i2cAddr_fd < 0) {
		fprintf(stderr, ANSI_COLOR_RED"Error: "ANSI_COLOR_RESET"Failed to initialize EEPROM at address 0x%X\n", EEPROM_SERIAL_NUM_ADDRESS);
		eepromClose(&serial_eeprom);
	}
	char serial_number[SERIAL_NUMBER_LEN+1]; // +1 for null terminator
	if (readSerialNumber(serial_eeprom.i2cAddr_fd, serial_number) == 1) {
		fprintf(stderr, ANSI_COLOR_RED"Error: "ANSI_COLOR_RESET"readSerialNumber() failed\n");
		eepromClose(&serial_eeprom);
	}
	close(serial_eeprom.i2cAddr_fd);
	printf("Serial Number: %s\n", serial_number);
	delay(1000);

	//==============================
	// init EEPROM and file
	//==============================
	printf("\n---- Initializing EEPROM and file ----\n\n");

	// setup eeprom
	EEPROM eeprom = {0};	// all members of the structure are intialized to 0, if it is a pointer then NULL
	initEEPROM(&eeprom);	// note: this function initializes an eeprom by setting all of its bits to a #defined VALUE
							// once this value is set the connection is kept open and reused for the rest of the test
							// no need to keep using wiringPiI2CSetup()

	// file creation
	char filename[100];
	snprintf(filename, sizeof(filename), "data/AT24CS01-XHM-B-subcritical-pile-bit-test-%s-0x%02X-data.csv", serial_number,INIT_BYTES_VALUE);

	FILE *csv_file = fopen(filename, "a");
	if (csv_file == NULL) {
		printf(ANSI_COLOR_RED"Error: "ANSI_COLOR_RESET"Failed to open CSV file\n");
		return EXIT_FAILURE;
	}
	printf("Saving data to file: %s\n", filename);

	fprintf(csv_file, "Elapsed Time (s), Total Bit Flips, 1->0 flips, 0->1 flips, Hard Faults, Read Failures\n");

	// track the first flip of every bit into a seperate csv file
	char firstFlipFilename[100];
	snprintf(firstFlipFilename, sizeof(firstFlipFilename), "data/AT24CS01-XHM-B-subcritical-pile-bit-test-%s-0x%02X-first-flips.csv", serial_number, INIT_BYTES_VALUE);
	FILE *firstFlip_csv = fopen(firstFlipFilename, "a");
	if (firstFlip_csv == NULL) {
		printf(ANSI_COLOR_RED"Error: "ANSI_COLOR_RESET"Failed to open first-flip CSV file\n");
		fclose(csv_file);
		return EXIT_FAILURE;
	}
	fprintf(firstFlip_csv, "Elapsed Time (s), Byte Offset, Bit Index\n");
	fflush(firstFlip_csv);
	printf("Logging first-flip addresses to file: %s\n", firstFlipFilename);

	// record a snapshot of the current state 
	char snapshotFilename[100];
	snprintf(snapshotFilename, sizeof(snapshotFilename), "data/AT24CS01-XHM-B-subcritical-pile-bit-test-%s-0x%02X-checkpoint.csv", serial_number,INIT_BYTES_VALUE);

	delay(3000);

	//=============================
	// begin bit test
	//=============================
	printf("\n---- It's logging time ----\n\n");

	time_t ctime = time(NULL);
	time_t lastCheckpointTime = ctime; // ADDED: tracks when we last wrote a checkpoint snapshot

	// FIX: the file is now opened ONCE before the loop and kept open for the
	// whole run. Previously this loop called fclose()+fopen() on csv_file
	// every single pass -- with no pacing between passes, that could mean
	// hundreds of thousands of open/close cycles over 12 hours, which is
	// unnecessary SD card wear and syscall overhead. fflush() (below) gives
	// the same "data survives a crash" guarantee at a fraction of the cost.
	while (((int)difftime(time(NULL), ctime)) <= RUNNING_TIME_SEC) {
		logger(ctime, csv_file, firstFlip_csv, &eeprom);

		// FIX: fflush() instead of fclose()+fopen() every pass -- pushes
		// buffered data to disk without the overhead of closing and
		// reopening the file descriptor each time.
		fflush(csv_file);

		// ADDED: periodic full per-bit checkpoint, independent of the fast
		// per-pass CSV logging above. Only fires once every
		// CHECKPOINT_INTERVAL_SEC of wall-clock time, not every pass.
		time_t nowTime = time(NULL);
		if (difftime(nowTime, lastCheckpointTime) >= CHECKPOINT_INTERVAL_SEC) {
			writeBitRecordSnapshot(&eeprom, snapshotFilename);
			lastCheckpointTime = nowTime;
		}

		// ADDED: pace the polling loop. Bit flips from beam exposure accumulate
		// on the timescale of fluence, not milliseconds -- polling as fast as
		// the I2C bus allows (the old behavior, with no delay here at all)
		// would only inflate the CSV size and SD card writes for a 12 hour
		// run without giving any better time resolution on real events.
		delay(POLL_INTERVAL_MS);
	}

	//=============================
	// close and free memory
	//=============================
	// write one final checkpoint snapshot capturing the complete end-of-run state, regardless of when the last periodic checkpoint fired.
	writeBitRecordSnapshot(&eeprom, snapshotFilename);

	fclose(csv_file);
	fclose(firstFlip_csv);
	close(eeprom.i2cAddr_fd);
	free(eeprom.flippedBitsPtr);
	free(eeprom.bitLookup);

	if (eeprom.priorState != NULL) {	// stops from freeing twice
		free(eeprom.priorState);
	}

	printf(ANSI_COLOR_GREEN"\n\n---- bit test complete ----\n\n"ANSI_COLOR_RESET);
	return EXIT_SUCCESS;
}
