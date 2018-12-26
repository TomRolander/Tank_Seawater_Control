/*
  Tank Seawater Control

  xxxxx
  
  The circuit:

  created 2018
  by Tom Rolander
*/
#define MODIFIED "2018-12-24"
#define VERSION "0.2"

#define DELAY_DIN_CHECKING_SEC  5
#define DELAY_LOGGING_MIN 1

long tickCounterSec = 0;
int  nextLoggingMin = 0;

bool bSDLogging = true;

// Date and time functions using a DS1307 RTC connected via I2C and Wire lib
#include <Wire.h>
#include "RTClib.h"

RTC_PCF8523 rtc;

#include <SD.h>

File fileSDCard;

// SD Shield
//
// change this to match your SD shield or module;
// Arduino Ethernet shield: pin 4
// Adafruit SD shields and modules: pin 10
// Sparkfun SD shield: pin 8
// MKRZero SD: SDCARD_SS_PIN
#define chipSelectSDCard 10

// 74HC595 8-bit, serial-in, parallel-out shift register
//
#define latchPin74HC595 3  //Pin connected to latch pin (ST_CP) of 74HC595
#define clockPin74HC595 2  //Pin connected to clock pin (SH_CP) of 74HC595
#define dataPin74HC595  1  //Pin connected to data in (DS) of 74HC595

/*
Din3  Din2  Din1  Din0    Dout4 Dout3 Dout2 Dout1 Dout0     
0     0     0     0       0     0     0     0     0       Water level between the high and low levels, this is the normal case  
0     0     0     1       1     0     0     0     0       error condition   shut off the pump   flash the light 
0     0     1     0       0     0     0     0     1       water level low  open the bypass valve to raise the water level 
0     0     1     1       1     0     0     0     0       water level very low   shut off the pump   flash the light  
0     1     0     0       0     0     0     0     0       water level high    close the bypass valve to lower the water level 
0     1     0     1       1     0     0     0     0       error condition   shut off the pump   flash the light 
0     1     1     0       1     0     0     0     0       error condition   flash the light 
0     1     1     1       1     0     0     0     0       error condition shut off the pump  flash the light  
1     0     0     0       1     0     0     0     0       error conditon  turn off the incoming water  flash the light  
1     0     0     1       1     0     0     0     0       error condition   shut off the pump   flash the light 
1     0     1     0       1     0     0     0     0       error condition  flash the light 
1     0     1     1       1     0     0     0     0       error condition   shut off the pump   flash the light 
1     1     0     0       1     0     0     0     0       water level very high shut off the incoming water  flash the light  probable cause clogged filters
1     1     0     1       1     0     0     0     0       error condition shut off the pump  flash the light  
1     1     1     0       1     0     0     0     0       error condition flash the light 
1     1     1     1       1     0     0     0     0       error condition shut off the pump  flash the light  
 */

// Digital In
#define Din0 A0
#define Din1 A1
#define Din2 A2
#define Din3 A3


// Digital Out
#define DOUT0 B00000001   // Bypass Valve
#define DOUT1 B00000010   // Shutoff / Inlet Valve
#define DOUT2 B00000100   // Discharge / Output Pump
#define DOUT3 B00001000   // UV Sterilizers
#define DOUT4 B00010000   // Flashing Light
#define DOUT5 B00100000

int digitalOutputState = DOUT0 | DOUT1 | DOUT2 | DOUT3 | DOUT5;  // Value to show in the 6 LEDs and relay drivers



#include <LiquidCrystal.h>

// initialize the library by associating any needed LCD interface pin
// with the arduino pin number it is connected to
const int rs = 4, en = 5, d4 = 9, d5 = 8, d6 = 7, d7 = 6;
LiquidCrystal lcd(rs, en, d4, d5, d6, d7);


bool flashing = false;

int digitalInputState_Saved = B11111111;
int digitalInputState_New;

// Constant Strings:


void setup() {

  // Immediately set the state of the DOUT's
  Setup_74HC595();
  
  // set up the LCD's number of columns and rows:
  lcd.begin(16, 2);
  // Print a message to the LCD.
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(F("Tnk Swtr Ctl "));
  lcd.print(F(VERSION));
  lcd.setCursor(0, 1);
  lcd.print(F("TAR "));
  lcd.print(F(MODIFIED));
  delay(2000);

// NOTE: Do NOT use Serial because Auduino UNO pin 1 is used by 74HC595 shift register
//Serial.begin(9600);

  if (! rtc.begin()) {
    lcd.setCursor(0, 0);
    lcd.print(F("*** ERROR ***   "));
    lcd.setCursor(0, 1);
    lcd.print(F("Couldnt find RTC"));
    while (1);
  } 
  if (! rtc.initialized()) {
    
    lcd.setCursor(0, 0);
    lcd.print(F("*** WARN ***    "));
    lcd.setCursor(0, 1);
    lcd.print(F("RTC isnt running"));
    
    // following line sets the RTC to the date & time this sketch was compiled
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    // This line sets the RTC with an explicit date & time, for example to set
    // January 21, 2014 at 3am you would call:
    // rtc.adjust(DateTime(2014, 1, 21, 3, 0, 0));
  }
  DateTime now = rtc.now();

  lcd.setCursor(0, 0);
  lcd.print(F("*** DATE ***    "));
  lcd.setCursor(0, 1);
  lcd.print(now.year(), DEC);
  lcd.print("/");
  LCDPrintTwoDigits(now.month());
  lcd.print("/");
  LCDPrintTwoDigits(now.day());
  lcd.print(" ");
  LCDPrintTwoDigits(now.hour());
  lcd.print(":");
  LCDPrintTwoDigits(now.minute());
  delay(2000);

  nextLoggingMin = (now.minute() + DELAY_LOGGING_MIN) % 60;
  
  // initialize the DIP Switch 4 position pins as an input:
  pinMode(Din0, INPUT_PULLUP);
  pinMode(Din1, INPUT_PULLUP);
  pinMode(Din2, INPUT_PULLUP);
  pinMode(Din3, INPUT_PULLUP);

  // Initial state display in LCD
  lcd.setCursor(0, 0);
  lcd.print(F("I=1 O=1 BV=1 UV1"));
  lcd.setCursor(0, 1);
  lcd.print(F("Tank Level Norml"));
  //SetupSDCardSwitch();
  SetupSDCardOperations();
}


void loop() {
  bool bLogged = false;
  delay(1000);
  DateTime now = rtc.now();    
  if (flashing)
  {
    if ((tickCounterSec & B00000001) == B00000001)
       digitalOutputState = digitalOutputState | DOUT4;
    else
       digitalOutputState = digitalOutputState & (~DOUT4);
    SetDigitalOutputState();
  }
  if ((tickCounterSec % DELAY_DIN_CHECKING_SEC) == 0)
  {
  // Sample digital inputs and set digital outputs
    digitalInputState_New = SampleDigitalInputs();
    if ((digitalInputState_New != digitalInputState_Saved) || (bSDLogging == false))
    {
      bLogged = true;
      bSDLogging = true;
      digitalInputState_Saved = digitalInputState_New;
      switch (digitalInputState_New)
      {
        case (B00000000):   // Water level between the high and low levels, this is the normal case
                            // Note: Bypass Valve may be open if low level switch hit last,
                            //       or closed if high level switch hit last
          flashing = false;
          digitalOutputState = digitalOutputState | DOUT1;    // send one to DO1 to open inlet valve
          digitalOutputState = digitalOutputState | DOUT5;    // send one to DO5 to show green LED

          lcd.setCursor(0, 0);
          lcd.print(F("I=1 O=1 BV=1 UV1"));
          lcd.setCursor(0, 1);
          SDLogging(true, F("Tank Level Norml"));
          break;
          
        case (B00000001):   // Pump shut off check float switches
          flashing = true;
          digitalOutputState = digitalOutputState & (~DOUT2); // send zero to DO2 to turn off the pump
          digitalOutputState = digitalOutputState | DOUT1;    // send one  to DO1 to open inlet valve
          lcd.setCursor(0, 0);
          lcd.print(F("I=1 O=0 BV=1 UV1"));
          lcd.setCursor(0, 1);
          SDLogging(true, F("FLOAT SWITCH ERR"));
          break;
          
        case (B00000010):   // Tank level low opening bypass
          flashing = false;
          digitalOutputState = digitalOutputState | DOUT0 | DOUT1;

          lcd.clear();
          lcd.setCursor(0, 0);
          lcd.print(F("I=1 O=0 BV=1 UV1"));
          lcd.setCursor(0, 1);
          SDLogging(true, F("Tank Level Low"));
          break;
          
        case (B00000011):   // Tank very low shutting off pump
          flashing = true;
          digitalOutputState = DOUT0 | DOUT1 | DOUT2 | DOUT4;
          
          lcd.setCursor(0, 0);
          lcd.print(F("I=1 O=0 BV=1 UV1"));
          lcd.setCursor(0, 1);
          SDLogging(true, F("TNK LVL VERY LOW"));
          break;
          
        case (B00000100):   // Tank level high closing bypass
          flashing = false;
          digitalOutputState = DOUT0 | DOUT1;

          lcd.setCursor(0, 0);
          lcd.print(F("I=1 O=0 BV=0 UV1"));
          lcd.setCursor(0, 1);
          SDLogging(true, F("Tank Level High"));
          break;
          
        case (B00001100):   // Tank very high shutting off pump
          flashing = true;
          digitalOutputState = DOUT0 | DOUT1 | DOUT2 | DOUT4;
          
          lcd.setCursor(0, 0);
          lcd.print(F("I=0 O=0 BV=1 UV1"));
          lcd.setCursor(0, 1);
          SDLogging(true, F("TNK LVL VRY HIGH"));
          break;
          
        default:   // Float switch error shut off pump
          flashing = true;
          digitalOutputState = DOUT4;
          
          lcd.setCursor(0, 0);
          lcd.print(F("I=1 O=0 BV=1 UV1"));
          lcd.setCursor(0, 1);
          SDLogging(true, F("FLOAT SWITCH ERR"));
          break;
      }
      SetDigitalOutputState();
    }
  }
  tickCounterSec++;

  if (now.minute() == nextLoggingMin)
  {
    nextLoggingMin = (now.minute() + DELAY_LOGGING_MIN) % 60;
    if (bLogged == false)
      SDLogging(true, F(""));
  }
}

int SampleDigitalInputs() 
{
  int digitalInputState_Current = B00000000;
  if (digitalRead(Din0) == LOW)
    digitalInputState_Current |= B00000001;
  if (digitalRead(Din1) == LOW)
    digitalInputState_Current |= B00000010;
  if (digitalRead(Din2) == LOW)
    digitalInputState_Current |= B00000100;
  if (digitalRead(Din3) == LOW)
    digitalInputState_Current |= B00001000;
    return (digitalInputState_Current);
}

void SetupSDCardOperations()
{   
  lcd.setCursor(0, 0);
  lcd.print(F("*** STATUS ***  "));
  lcd.setCursor(0, 1);
  lcd.print(F("SD Init Start   "));

  if (!SD.begin(chipSelectSDCard)) {
    lcd.setCursor(0, 0);
    lcd.print(F("*** ERROR ***   "));
    lcd.setCursor(0, 1);
    lcd.print(F("SD Init Failed  "));
    while (1);
  }

// open the file for reading:
  fileSDCard = SD.open("LOGGING.CSV");
  if (fileSDCard) {
    fileSDCard.close();
  } 
  else
  {
    fileSDCard = SD.open("LOGGING.CSV", FILE_WRITE);
    if (fileSDCard) 
    {
      fileSDCard.println(F("\"Date\",\"Time\",\"DOut\",\"Din\",\"Status\""));
      fileSDCard.close();
    }
    else
    {
      lcd.setCursor(0, 0);
      lcd.print(F("*** ERROR ***   "));
      lcd.setCursor(0, 1);
      lcd.print(F("SD Write Failed "));
    }
  }
  SD.end();
  if (!SD.begin(chipSelectSDCard)) 
  {
    lcd.setCursor(0, 0);
    lcd.print(F("*** ERROR ***   "));
    lcd.setCursor(0, 1);
    lcd.print(F("SD Init Failed  "));
    while (1);
  }

  lcd.setCursor(0, 0);
  lcd.print(F("*** STATUS ***  "));
  lcd.setCursor(0, 1);
  lcd.print(F("SD Init Finish  "));
  SDLogging(false, F("Start Up"));
}

void SDLogging(bool bShowLCDMessage, const __FlashStringHelper*status)
{
  if (bShowLCDMessage)
  {
    lcd.setCursor(0, 1);
    lcd.print(status);
  }
      
  SD.end();
  if (!SD.begin(chipSelectSDCard)) 
  {
    lcd.setCursor(0, 0);
    lcd.print(F("*** ERROR ***   "));
    lcd.setCursor(0, 1);
    lcd.print(F("SD Logging Fail "));
    bSDLogging = false;
    return;
  }
  
  // open the file. note that only one file can be open at a time,
  // so you have to close this one before opening another.
  fileSDCard = SD.open("LOGGING.CSV", FILE_WRITE);

  // if the file opened okay, write to it:
  if (fileSDCard) 
  {
    DateTime now = rtc.now();
      
    fileSDCard.print(now.year(), DEC);
    fileSDCard.print("/");
    fileSDCard.print(now.month(), DEC);
    fileSDCard.print("/");
    fileSDCard.print(now.day(), DEC);
    fileSDCard.print(",");
    fileSDCard.print(now.hour(), DEC);
    fileSDCard.print(":");
    fileSDCard.print(now.minute(), DEC);
    fileSDCard.print(":");
    fileSDCard.print(now.second(), DEC);
    fileSDCard.print(",");
    SDPrintBinary(digitalOutputState,6);
    fileSDCard.print(",");
    SDPrintBinary(digitalInputState_Saved,4);
    fileSDCard.print(",");
    fileSDCard.print(status);
    fileSDCard.println("");
    // close the file:
    fileSDCard.close();
    //Serial.println("done.");
  } else 
  {
    // if the file didn't open, print an error:
    lcd.setCursor(0, 0);
    lcd.print(F("*** ERROR ***   "));
    lcd.setCursor(0, 1);
    lcd.print(F("Open LOGGING.CSV"));
  }  
}

void Setup_74HC595()
{ 
  //setup 8-bit, serial-in, parallel-out shift register
  
  //set pins to output because they are addressed in the main loop
  pinMode(latchPin74HC595, OUTPUT);
  pinMode(dataPin74HC595, OUTPUT);  
  pinMode(clockPin74HC595, OUTPUT);
  
  SetDigitalOutputState();
}

void SetDigitalOutputState()
{
  digitalWrite(latchPin74HC595, LOW);          //Pull latch LOW to start sending data
  shiftOut(dataPin74HC595, clockPin74HC595, MSBFIRST, digitalOutputState);         //Send the data
  digitalWrite(latchPin74HC595, HIGH);         //Pull latch HIGH to stop sending data
}

void LCDPrintTwoDigits(int iVal)
{
  if (iVal < 10)
    lcd.print("0");
  lcd.print(iVal, DEC);
}

void SDPrintBinary(byte inByte, int nBits)
{
  fileSDCard.print("B");
  for (int b = nBits-1; b >= 0; b--)
  {
    fileSDCard.print(bitRead(inByte, b), BIN);
  }
}
