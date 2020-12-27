#include "secrets.h"

#include <ArduinoJson.h>

#include <Adafruit_GFX.h>
#include <Adafruit_GrayOLED.h>
#include <Adafruit_SPITFT_Macros.h>
#include <Adafruit_SPITFT.h>
#include <gfxfont.h>
#include <Adafruit_SSD1306.h>
#include <splash.h>

#include <ESP8266WiFi.h>

#include <Servo.h>
#include <SPI.h>
#include <Wire.h>
#include <Fonts/FreeSansBold12pt7b.h>

#include <Adafruit_MQTT.h>
#include <Adafruit_MQTT_Client.h>

#include <Key.h>
#include <Keypad.h>

/* ------------------------------------------- */
/* INI PINS                                    */
/* ------------------------------------------- */

#define PIN_KEYPAD_COL_1 5  // My Keboard 14
#define PIN_KEYPAD_COL_2 4  // My Keboard 04
#define PIN_KEYPAD_COL_3 14 // My Keboard 09
#define PIN_KEYPAD_ROW_1 12 // My Keboard 02
#define PIN_KEYPAD_ROW_2 13 // My Keboard 07
#define PIN_KEYPAD_ROW_3 3  // My Keboard 12

#define PIN_SERVO_PWM 15

#define PIN_SDA 2
#define PIN_SCL 0

#define TIMER_AUTOSLEEP 20000
#define TIMER_OPENING_BOX 5000
#define TIMER_PROGRESS 500
#define TIMER_MESSAGE 3000

#define PINCODE_LENGTH 6

#define DEG_OPEN_ALL 110
#define DEG_OPEN_PARCEL 50

/* ------------------------------------------- */
/* INI KEYPAD                                  */
/* ------------------------------------------- */
#define ROWS 3
#define COLS 3

char hexaKeys[ROWS][COLS] = {
    {'1', '2', '3'},
    {'4', '5', '6'},
    {'7', '8', '9'}};
byte rowPins[ROWS] = {PIN_KEYPAD_ROW_1, PIN_KEYPAD_ROW_2, PIN_KEYPAD_ROW_3};
byte colPins[COLS] = {PIN_KEYPAD_COL_1, PIN_KEYPAD_COL_2, PIN_KEYPAD_COL_3};

Keypad customKeypad = Keypad(makeKeymap(hexaKeys), rowPins, colPins, ROWS, COLS);

/* ------------------------------------------- */
/* INI OLED DISPLAY                            */
/* ------------------------------------------- */

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels

// Declaration for an SSD1306 display connected to I2C (SDA, SCL pins)
#define OLED_RESET -1 // Reset pin # (or -1 if sharing Arduino reset pin)
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

/* ------------------------------------------- */
/* INI SERVO                                   */
/* ------------------------------------------- */

Servo opener_servo;

/* ------------------------------------------- */
/* INI WIFI                                    */
/* ------------------------------------------- */

char ssid[] = WIFI_SSID;
char pass[] = WIFI_PASSWORD;
int status = WL_IDLE_STATUS;

WiFiClient client;

/* ------------------------------------------- */
/* INI MQTT                                    */
/* ------------------------------------------- */

Adafruit_MQTT_Client mqtt(&client, MQTT_SERVER, MQTT_SERVERPORT, MQTT_CID, MQTT_USERNAME, MQTT_KEY);
Adafruit_MQTT_Subscribe full_open_pins_subscribe = Adafruit_MQTT_Subscribe(&mqtt, "openparcelbox/full_open_pins");
Adafruit_MQTT_Subscribe half_open_pins_subscribe = Adafruit_MQTT_Subscribe(&mqtt, "openparcelbox/half_open_pins");

Adafruit_MQTT_Subscribe do_full_open_subscribe = Adafruit_MQTT_Subscribe(&mqtt, "openparcelbox/do_full_open");
Adafruit_MQTT_Subscribe do_half_open_subscribe = Adafruit_MQTT_Subscribe(&mqtt, "openparcelbox/do_half_open");

Adafruit_MQTT_Publish last_full_open_by_publish = Adafruit_MQTT_Publish(&mqtt, "openparcelbox/last_full_open_by");
Adafruit_MQTT_Publish last_half_open_by_publish = Adafruit_MQTT_Publish(&mqtt, "openparcelbox/last_half_open_by");

/* ------------------------------------------- */
/* INI                                         */
/* ------------------------------------------- */

char half_open_pins[10][PINCODE_LENGTH + 1];
char full_open_pins[2][PINCODE_LENGTH + 1];
const char admin_code_menu[PINCODE_LENGTH + 1] = "111111";

char input_keypad_buffer[PINCODE_LENGTH + 1];
char input_keypad_last_key_pressed = '\0';

#define INI 0
#define CODE 1
#define MENU 2
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
unsigned long gotosleep_timer;

void setup()
{
  Serial.begin(9600);
  Serial.println("Setup started...");

  do_full_open_subscribe.setCallback(do_full_open_Callback);
  mqtt.subscribe(&do_full_open_subscribe);
  do_half_open_subscribe.setCallback(do_half_open_Callback);
  mqtt.subscribe(&do_half_open_subscribe);

  full_open_pins_subscribe.setCallback(full_open_pins_Callback);
  mqtt.subscribe(&full_open_pins_subscribe);
  half_open_pins_subscribe.setCallback(half_open_pins_Callback);
  mqtt.subscribe(&half_open_pins_subscribe);

  setAction(INI);

  progress_timer = millis();

  Serial.println("Setup finished!");
}

void loop()
{
  startWiFiClient();

  MQTT_connect();
  mqtt.processPackets(100);

  action_changed_processed = true;

  input();
  process();
  output();

  if (action_changed_processed)
  {
    action_changed = false;
  }
}

/* ------------------------------------------- */
/* INPUT                                       */
/* ------------------------------------------- */

void input()
{
  inputKeypad();
}

void inputKeypad()
{
  input_keypad_last_key_pressed = '\0';

  char key = customKeypad.getKey();

  if (key != NO_KEY)
  {
    input_keypad_last_key_pressed = key;

    byte cur_len = strlen(input_keypad_buffer);
    input_keypad_buffer[cur_len] = input_keypad_last_key_pressed;
    input_keypad_buffer[cur_len + 1] = '\0';
  }
}

/* ------------------------------------------- */
/* PROCESS                                     */
/* ------------------------------------------- */

void process()
{
  switch (current_action)
  {
  case INI:
    processIni();
  case CODE:
    processCode();
    break;
  case MENU:
    processMenu();
    break;
  case OPEN_PARCEL_BOX:
  case OPEN_ALL_BOX:
    processOpenBox(current_action);
    break;
  case MESSAGE:
    processMessage();
    break;
  }
}

void processMessage()
{
  if (action_changed)
  {
    message_timer = millis();
  }

  if (millis() - message_timer > TIMER_MESSAGE)
  {
    setAction(message_callback);
  }
}

void processIni()
{
  Serial.println("Initializing...");

  Wire.begin(PIN_SDA, PIN_SCL);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C))
  { // Address 0x3D for 128x64
    Serial.println(F("SSD1306 allocation failed"));
    for (;;)
      ; // Don't proceed, loop forever
  }

  opener_servo.write(0);
  opener_servo.attach(PIN_SERVO_PWM);
  opener_servo.write(0);

  setAction(CODE);

  Serial.println("Initialization finished!");

}

void processCode()
{
  byte confirm_length = PINCODE_LENGTH;

  if (strlen(input_keypad_buffer) == confirm_length)
  {
    if (strcmp(input_keypad_buffer, admin_code_menu) == 0)
    {
      //setAction(MENU);
    }
    else if (isCodeOk(input_keypad_buffer, half_open_pins, 10))
    {
      last_half_open_by_publish.publish(input_keypad_buffer);
      setAction(OPEN_PARCEL_BOX);
    }
    else if (isCodeOk(input_keypad_buffer, full_open_pins, 2))
    {
      last_full_open_by_publish.publish(input_keypad_buffer);
      setAction(OPEN_ALL_BOX);
    }
    else
    {
      showMessage(F("Wrong PIN"), CODE);
    }

    input_keypad_buffer[0] = 0;
  }
}

bool isCodeOk(char *code, char inputList[][PINCODE_LENGTH + 1], size_t rows)
{
  for (int i = 0; i < rows; i++)
  {
    if (strcmp(inputList[i], code) == 0)
    {
      return true;
    }
  }

  return false;
}

void processMenu()
{
  switch (input_keypad_last_key_pressed)
  {
  case '9':
    setAction(CODE);
    break;
  }

  input_keypad_buffer[0] = 0;
}

void processOpenBox(byte parcelOrAll)
{
  if (action_changed)
  {
    opening_position = 1;
  }

  if (opening_position == 0)
  {
    setAction(CODE);
  }
}


/* ------------------------------------------- */
/* OUTPUT                                      */
/* ------------------------------------------- */

void output()
{
  switch (current_action)
  {
  case CODE:
    outputCode();
    break;
  case MENU:
    outputMenu();
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

void outputMessage()
{
  display.clearDisplay();

  display.setFont();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.cp437(true);

  int16_t x, y;
  uint16_t w, h;

  display.getTextBounds(message, 0, 0, &x, &y, &w, &h);
  display.setCursor(62 - w / 2, h + 10); // 62 because tweaking of font
  display.print(message);

  display.display();
}

void outputCode()
{
  displayCode();
}

void displayCode()
{
  display.clearDisplay();

  display.setFont(&FreeSansBold12pt7b);
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.cp437(true);

  int16_t x, y;
  uint16_t w, h;

  display.getTextBounds("PIN", 0, 0, &x, &y, &w, &h);
  display.setCursor(62 - w / 2, h + 10); // 62 because tweaking of font
  display.print(F("PIN"));

  display.getTextBounds(input_keypad_buffer, 0, 0, &x, &y, &w, &h);
  display.setCursor(62 - w / 2, 64 - 10);
  display.print(input_keypad_buffer);

  display.display();
}

void outputMenu()
{
  displayMenu();
}

void displayMenu()
{
  display.clearDisplay();

  display.setFont();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.cp437(true);

  display.setCursor(10, 10);
  display.println(F("Menu:"));
  display.println(F("1: Add pin"));
  display.println(F("2: Remove pin"));
  display.println(F("3: Dump report"));
  display.println(F("9: Exit"));

  display.display();
}

void outputOpenParcelBox()
{
  displayOpenBox();
  openBox(DEG_OPEN_PARCEL);
}

void outputOpenAllBox()
{
  displayOpenBox();
  openBox(DEG_OPEN_ALL);
}

void openBox(int degree)
{
  if (action_changed)
  {
    opening_position = 0;
    opening_direction = true;
  }

  if (opening_direction)
  {
    opening_position += 2;
  }
  else
  {
    opening_position -= 2;
  }

  opener_servo.write(opening_position);

  if (opening_position >= degree)
  {
    opening_direction = false;
  }
}

char progress_chars[4] = {'|', '/', '-', '\\'};

byte progress = 0;

void displayOpenBox()
{
  display.clearDisplay();

  display.setFont(&FreeSansBold12pt7b);
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.cp437(true);

  int16_t x, y;
  uint16_t w, h;

  display.getTextBounds(F("Opening"), 0, 0, &x, &y, &w, &h);
  display.setCursor(62 - w / 2, h + 10); // 62 because tweaking of font
  display.print(F("Opening"));

  char progress_char[2] = {progress_chars[progress], '\0'};
  display.getTextBounds(progress_char, 0, 0, &x, &y, &w, &h);
  display.setCursor(62 - w / 2, 64 - 10);
  display.print(progress_char);

  display.display();

  if (progress == sizeof(progress_chars) - 1)
  {
    progress = 0;
  }
  else if (millis() - progress_timer > TIMER_PROGRESS)
  {
    progress++;
    progress_timer = millis();
  }
}

/* ------------------------------------------- */

void setAction(byte action)
{
  Serial.print("Setting action to: ");
  Serial.print(action);

  input_keypad_buffer[0] = 0;
  current_action = action;
  action_changed = true;
  action_changed_processed = false;

    Serial.print("Action set!");
}

void showMessage(const __FlashStringHelper *msg, byte clbk)
{
  strcpy_P(message, (PGM_P)msg);
  message_callback = clbk;
  setAction(MESSAGE);
}

void startWiFiClient()
{
  // Stop if already connected.
  if (WiFi.localIP().toString() != "(IP unset)")
  {
    return;
  }

  while (WiFi.localIP().toString() == "(IP unset)")
  {
    Serial.print("Attempting to connect to WPA SSID: ");
    Serial.println(ssid);
    Serial.println(status);
    // Connect to WPA/WPA2 network:
    status = WiFi.begin(ssid, pass);

    // wait 10 seconds for connection:
    delay(10000);
  }

  Serial.print("You're connected to the network");
  Serial.println("IP address: " + WiFi.localIP().toString());
}


void MQTT_connect()
{
  int8_t ret;

  // Stop if already connected.
  if (mqtt.connected())
  {
    return;
  }

  Serial.println("Connecting to MQTT...");

  uint8_t retries = 3;
  while ((ret = mqtt.connect()) != 0)
  { // connect will return 0 for connected
    Serial.println(mqtt.connectErrorString(ret));
    Serial.println("Retrying MQTT connection in 10 seconds...");
    mqtt.disconnect();
    delay(10000); // wait 10 seconds
    retries--;
    if (retries == 0)
    {
      // basically die and wait for WDT to reset me
      while (1)
        ;
    }
  }
  Serial.println("MQTT Connected!");
}


void full_open_pins_Callback(char *data, uint16_t len)
{
  receive_pin_codes(data, full_open_pins, 2);
}

void half_open_pins_Callback(char *data, uint16_t len)
{
  receive_pin_codes(data, half_open_pins, 10);
}

void receive_pin_codes(char *data, char destination[][PINCODE_LENGTH + 1], size_t num)
{
  Serial.print("Data received: ");
  Serial.println(data);

  for(int i = 0; i < num; i++)
  { 
    strcpy(destination[i], "\0");
  }

  DynamicJsonDocument doc(200);
  deserializeJson(doc, data);
  JsonArray array = doc.as<JsonArray>();

  int i = 0;
  for(JsonVariant v : array) {
    strcpy(destination[i++], v.as<String>().c_str());
  }
}

void do_full_open_Callback(char *data, uint16_t len)
{
  if (strcmp(data, "True") == 0)
  {
    last_full_open_by_publish.publish("MQTT");
    setAction(OPEN_ALL_BOX);
  }
}

void do_half_open_Callback(char *data, uint16_t len)
{ 
  if (strcmp(data, "True") == 0)
  {
    last_half_open_by_publish.publish("MQTT");
    setAction(OPEN_PARCEL_BOX);
  }
}