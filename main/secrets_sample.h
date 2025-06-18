#ifndef SECRETS_H
#define SECRETS_H

// ====================================================================
//                 PIN CODE CONFIGURATION
// ====================================================================

// Define the required length for all PIN codes.
#define PINCODE_LENGTH 6

// --- PINs for Parcel Opening (Partial Open) ---
// Add your PINs to the list below.
// IMPORTANT: Update NUM_HALF_OPEN_PINS to match the number of PINs in the list.
#define NUM_HALF_OPEN_PINS 2
const char half_open_pins[NUM_HALF_OPEN_PINS][PINCODE_LENGTH + 1] = {
  "123456", 
  "654321"
};


// --- PINs for Full Opening ---
// Add your PINs to the list below.
// IMPORTANT: Update NUM_FULL_OPEN_PINS to match the number of PINs in the list.
#define NUM_FULL_OPEN_PINS 2
const char full_open_pins[NUM_FULL_OPEN_PINS][PINCODE_LENGTH + 1] = {
  "111111", 
  "222222"  
};


#endif // SECRETS_H