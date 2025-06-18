// Include the new secrets file at the top
#include "secrets.h"

#include <Adafruit_GFX.h>
#include <Adafruit_GrayOLED.h>
#include <Adafruit_SPITFT_Macros.h>
#include <Adafruit_SPITFT.h>
#include <gfxfont.h>
#include <Adafruit_SSD1306.h>
#include <splash.h>

#include <Servo.h>
#include <SPI.h>
#include <Wire.h>
#include <Fonts/FreeSansBold12pt7b.h>

#include <Key.h>
#include <Keypad.h>

/* ------------------------------------------- */
/* INI PINS                                    */
/* ------------------------------------------- */

#define PIN_KEYPAD_COL_1 5
#define PIN_KEYPAD_COL_2 4
#define PIN_KEYPAD_COL_3 14
#define PIN_KEYPAD_ROW_1 12
#define PIN_KEYPAD_ROW_2 13
#define PIN_KEYPAD_ROW_3 3

#define PIN_SERVO_PWM 15

#define PIN_SDA 2
#define PIN_SCL 0

#define TIMER_OPENING_BOX 5000
#define TIMER_PROGRESS 500
#define TIMER_MESSAGE 3000

#define DEG_OPEN_ALL 180
#define DEG_OPEN_PARCEL 130

/* ------------------------------------------- */
// The PIN lists have been moved to secrets.h
/* ------------------------------------------- */


/* ------------------------------------------- */
/* INI KEYPAD                                  */
/* ------------------------------------------- */
#define ROWS 3
#define COLS 3

char hexaKeys[ROWS][COLS] = {
  { '1', '2', '3' },
  { '4', '5', '6' },
  { '7', '8', '9' }
};
byte rowPins[ROWS] = { PIN_KEYPAD_ROW_1, PIN_KEYPAD_ROW_2, PIN_KEYPAD_ROW_3 };
byte colPins[COLS] = { PIN_KEYPAD_COL_1, PIN_KEYPAD_COL_2, PIN_KEYPAD_COL_3 };

Keypad customKeypad = Keypad(makeKeymap(hexaKeys), rowPins, colPins, ROWS, COLS);

/* ------------------------------------------- */
/* INI OLED DISPLAY                            */
/* ------------------------------------------- */

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

/* ------------------------------------------- */
/* INI SERVO                                   */
/* ------------------------------------------- */

Servo opener_servo;

/* ------------------------------------------- */
/* INI STATE VARIABLES                         */
/* ------------------------------------------- */

char input_keypad_buffer[PINCODE_LENGTH + 1];
char input_keypad_last_key_pressed = '\0';

#define INI 0
#define CODE 1
#define OPEN_PARCEL_BOX 3
#define OPEN_ALL_BOX 4
#define MESSAGE 8

byte current_action;
bool action_changed;
bool action_changed_processed;

byte message_callback;
char message[20];
unsigned long message_timer;

unsigned long opening_position;
bool opening_direction;

unsigned long progress_timer;

void setup() {
  Serial.begin(9600);
  Serial.println("Setup started...");

  setAction(INI);

  progress_timer = millis();

  Serial.println("Setup finished!");
}

void loop() {
  action_changed_processed = true;

  input();
  process();
  output();

  if (action_changed_processed) {
    action_changed = false;
  }
}

/* ------------------------------------------- */
/* INPUT                                       */
/* ------------------------------------------- */

void input() {
  inputKeypad();
}

void inputKeypad() {
  input_keypad_last_key_pressed = '\0';

  char key = customKeypad.getKey();

  if (key != NO_KEY) {
    input_keypad_last_key_pressed = key;

    // Prevent buffer overflow
    byte cur_len = strlen(input_keypad_buffer);
    if (cur_len < PINCODE_LENGTH) {
      input_keypad_buffer[cur_len] = input_keypad_last_key_pressed;
      input_keypad_buffer[cur_len + 1] = '\0';
    }
  }
}

/* ------------------------------------------- */
/* PROCESS                                     */
/* ------------------------------------------- */

void process() {
  switch (current_action) {
    case INI:
      processIni();
      break;
    case CODE:
      processCode();
      break;
    case OPEN_PARCEL_BOX:
    case OPEN_ALL_BOX:
      processOpenBox();
      break;
    case MESSAGE:
      processMessage();
      break;
  }
}

void processMessage() {
  if (action_changed) {
    message_timer = millis();
  }

  if (millis() - message_timer > TIMER_MESSAGE) {
    setAction(message_callback);
  }
}

void processIni() {
  Serial.println("Initializing...");

  Wire.begin(PIN_SDA, PIN_SCL);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed"));
    for (;;)
      ;
  }

  opener_servo.attach(PIN_SERVO_PWM);
  opener_servo.write(0);

  setAction(CODE);

  Serial.println("Initialization finished!");
}

void processCode() {
  if (strlen(input_keypad_buffer) == PINCODE_LENGTH) {
    Serial.print("Checking code: ");
    Serial.println(input_keypad_buffer);

    if (isCodeOk(input_keypad_buffer, half_open_pins, NUM_HALF_OPEN_PINS)) {
      Serial.println("Correct PIN for parcel opening.");
      setAction(OPEN_PARCEL_BOX);
    } else if (isCodeOk(input_keypad_buffer, full_open_pins, NUM_FULL_OPEN_PINS)) {
      Serial.println("Correct PIN for full opening.");
      setAction(OPEN_ALL_BOX);
    } else {
      Serial.println("Wrong PIN.");
      showMessage(F("Wrong PIN"), CODE);
    }

    input_keypad_buffer[0] = '\0'; // Clear buffer after check
  }
}

// Check if the entered code exists in the provided list of PINs
bool isCodeOk(const char *code, const char inputList[][PINCODE_LENGTH + 1], size_t rows) {
  for (size_t i = 0; i < rows; i++) {
    if (strcmp(inputList[i], code) == 0) {
      return true;
    }
  }
  return false;
}

void processOpenBox() {
  // A timer to automatically return to the code entry screen after the box has been opened.
  if (action_changed) {
    message_timer = millis(); // Start a timer when the opening action begins
  }

  if (millis() - message_timer > TIMER_OPENING_BOX) {
     setAction(CODE);
  }
}

/* ------------------------------------------- */
/* OUTPUT                                      */
/* ------------------------------------------- */

void output() {
  switch (current_action) {
    case CODE:
      outputCode();
      break;
    case OPEN_PARCEL_BOX:
      outputOpenParcelBox();
      break;
    case OPEN_ALL_BOX:
      outputOpenAllBox();
      break;
    case MESSAGE:
      outputMessage();
      break;
  }
}

void outputMessage() {
  display.clearDisplay();
  display.setFont();
  display.setTextSize(2); // Made text a bit larger for visibility
  display.setTextColor(WHITE);
  display.cp437(true);

  int16_t x, y;
  uint16_t w, h;

  display.getTextBounds(message, 0, 0, &x, &y, &w, &h);
  display.setCursor((SCREEN_WIDTH - w) / 2, (SCREEN_HEIGHT - h) / 2);
  display.print(message);

  display.display();
}

void outputCode() {
  displayCode();
}

void displayCode() {
  display.clearDisplay();
  display.setFont(&FreeSansBold12pt7b);
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.cp437(true);

  int16_t x, y;
  uint16_t w, h;

  display.getTextBounds("PIN", 0, 0, &x, &y, &w, &h);
  display.setCursor((SCREEN_WIDTH - w) / 2, h + 5);
  display.print(F("PIN"));

  // Display asterisks instead of the PIN for security
  char display_buffer[PINCODE_LENGTH + 1] = "";
  for (byte i = 0; i < strlen(input_keypad_buffer); i++) {
    strcat(display_buffer, "*");
  }

  display.getTextBounds(display_buffer, 0, 0, &x, &y, &w, &h);
  display.setCursor((SCREEN_WIDTH - w) / 2, SCREEN_HEIGHT - 15);
  display.print(display_buffer);

  display.display();
}

void outputOpenParcelBox() {
  displayOpenBox();
  openBox(DEG_OPEN_PARCEL);
}

void outputOpenAllBox() {
  displayOpenBox();
  openBox(DEG_OPEN_ALL);
}

void openBox(int degree) {
  if (action_changed) {
    opening_position = 0;
    opening_direction = true; // Start by opening
  }

  // A simple animation: open, wait, then close.
  // The processOpenBox() timer will eventually return to the CODE screen.
  if (opening_direction) {
    if (opening_position < degree) {
      opening_position += 2; // Open smoothly
    } else {
      opening_position = degree; // Cap at max degree
      // Once it's fully open, check the timer to decide when to start closing
      if (millis() - message_timer > TIMER_OPENING_BOX / 2) {
          opening_direction = false; // Start closing
      }
    }
  } else { // Closing direction
    if (opening_position > 0) {
      opening_position -= 2; // Close smoothly
    } else {
      opening_position = 0; // Cap at min degree
    }
  }

  opener_servo.write(opening_position);
}

char progress_chars[4] = { '|', '/', '-', '\\' };
byte progress = 0;

void displayOpenBox() {
  display.clearDisplay();
  display.setFont(&FreeSansBold12pt7b);
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.cp437(true);

  int16_t x, y;
  uint16_t w, h;

  display.getTextBounds(F("Opening"), 0, 0, &x, &y, &w, &h);
  display.setCursor((SCREEN_WIDTH - w) / 2, h + 10);
  display.print(F("Opening"));

  char progress_char[2] = { progress_chars[progress], '\0' };
  display.getTextBounds(progress_char, 0, 0, &x, &y, &w, &h);
  display.setCursor((SCREEN_WIDTH - w) / 2, SCREEN_HEIGHT - 10);
  display.print(progress_char);

  display.display();

  if (millis() - progress_timer > TIMER_PROGRESS) {
    progress = (progress + 1) % 4; // Cycle through 0, 1, 2, 3
    progress_timer = millis();
  }
}

/* ------------------------------------------- */
/* HELPER FUNCTIONS                            */
/* ------------------------------------------- */

void setAction(byte action) {
  Serial.print("Setting action to: ");
  Serial.println(action);

  input_keypad_buffer[0] = '\0';
  current_action = action;
  action_changed = true;
  action_changed_processed = false;
}

void showMessage(const __FlashStringHelper *msg, byte clbk) {
  strcpy_P(message, (PGM_P)msg);
  message_callback = clbk;
  setAction(MESSAGE);
}