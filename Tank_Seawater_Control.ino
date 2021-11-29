/*
  Tank Seawater Control
  Hopkins Marine Station

  Original: Visual Basic
            John Lee

  Modified: Arduino
            Tom Rolander
*/

#define MODIFIED "2021-11-29"
#define VERSION "0.9"

#define RESET_HOUR  6

#define DELAY_DIN_CHECKING_SEC  5
#define DELAY_UVTIMER_SEC 900

//bool bForceOneMinuteLogging; // If you remove this line the Arduino IDE compile will fail !!??

bool bSDLogFail = false;
int  iToggle = 0;

bool bSlowDischarge = false;

long tickCounterSec = 0;
unsigned long tickCounterMilliseconds = 0;
unsigned long tickCounterMillisecondsBottomSwitch = 0;

#define TIMER_BOTTOM_SWITCH_MINUTES 5L 
#define TIMER_BOTTOM_SWITCH_DELAY_MS (TIMER_BOTTOM_SWITCH_MINUTES * 60L * 1000L)

#define SLOW_DSCHG_MINUTES  10L
#define SLOW_DSCHG_MILLISECONDS (SLOW_DSCHG_MINUTES * 60L * 1000L)

int UVtimer = 0;
int UVtimerMax = DELAY_UVTIMER_SEC / DELAY_DIN_CHECKING_SEC;
bool bTurnOnUV = false;

// Date and time functions using a DS1307 RTC connected via I2C and Wire lib
#include <Wire.h>
#include "RTClib.h"

RTC_PCF8523 rtc;

// SD Card used for data logging
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

/*  Data In and corresponding Data Out states for Tank Seawater Control
Din3 Din2 Din1 Din0  Dout4 Dout3 Dout2 Dout1 Dout0     
0    0    0    0     1     x     x     1     x     Wtr lvl between the high and low levels, this is the normal case  
0    0    0    1     0     x     0     1     x     Error: shut off the pump, flash the light 
0    0    1    0     x     x     x     1     1     Wtr lvl low  open the bypass valve to raise the Wtr lvl 
0    0    1    1     0     x     0     1     1     Wtr lvl very low, shut off the pump, flash the light  
0    1    0    0     x     1     1     x     0     Wtr lvl high,  close the bypass valve to lower the Wtr lvl 
0    1    0    1     0     x     x     x     x     Error: shut off the pump, flash the light 
0    1    1    0     0     x     x     x     x     Error: flash the light 
0    1    1    1     0     x     x     x     x     Error: shut off the pump  flash the light  
1    0    0    0     0     1     1     0     0     Error: turn off the incoming water, flash the light  
1    0    0    1     0     x     x     x     x     Error: shut off the pump, flash the light 
1    0    1    0     0     x     x     x     x     Error: flash the light 
1    0    1    1     0     x     x     x     x     Error: shut off the pump, flash the light 
1    1    0    0     0     1     1     0     0     Wtr lvl very high, incoming water off, flash the light, clogged filters?
1    1    0    1     0     x     x     x     x     Error: shut off the pump, flash the light  
1    1    1    0     0     x     x     x     x     Error: flash the light 
1    1    1    1     0     x     x     x     x     Error: shut off the pump, flash the light  
*/

/*
Dout  Description    Value
 0    Bypass Valve   0 = Close
                     1 = Open
 1    Inlet Valve    0 = Close
                     1 = Open
 2    Pump           0 = Off
                     1 = On
 3    UV Sterilizer  0 = Off
                     1 = On
 4    Warn Light     0 = On, flashing
                     1 = Off
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

int digitalOutputState = DOUT0 | DOUT1 | DOUT2 | DOUT3 | DOUT4;  // Value to show in the 5 LEDs and relay drivers

// NOTE: Using Version 1.0.6
//       Compile/Link FAILS with Version 1.0.7
#include <LiquidCrystal.h>

// Initialize the LCD library by associating LCD interface pins
// with the arduino pin number it is connected to
const int rs = 4, en = 5, d4 = 9, d5 = 8, d6 = 7, d7 = 6;
LiquidCrystal lcd(rs, en, d4, d5, d6, d7);


int digitalInputState_Saved = B11111111;
int digitalInputState_New;

// Proc to reset the Arduino
void (* re_set)(void) = 0x00;

void setup() 
{
  Wire.begin(); // fix weird characters in LCD display?
  
  // Immediately set the state of the DOUT's
  Setup_74HC595();
  
  // set up the LCD's number of columns and rows:
  lcd.begin(16, 2);
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

// Initialize the Real Time Clock
  if (! rtc.begin()) 
  {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(F("*** ERROR ***   "));
    lcd.setCursor(0, 1);
    lcd.print(F("Couldnt find RTC"));
    while (1);
  } 
  if (! rtc.initialized()) 
  {
    lcd.clear();
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

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(F("*** DATE ***    "));
  lcd.setCursor(0, 1);
  lcd.print(now.year(), DEC);
  lcd.print(F("/"));
  LCDPrintTwoDigits(now.month());
  lcd.print(F("/"));
  LCDPrintTwoDigits(now.day());
  lcd.print(F(" "));
  LCDPrintTwoDigits(now.hour());
  lcd.print(F(":"));
  LCDPrintTwoDigits(now.minute());
  delay(2000);

  // initialize the DIP Switch 4 position pins as an input using the internal pullups:
  pinMode(Din0, INPUT_PULLUP);
  pinMode(Din1, INPUT_PULLUP);
  pinMode(Din2, INPUT_PULLUP);
  pinMode(Din3, INPUT_PULLUP);

  // Initial state display in LCD
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(F("B=1 I=1 P=1 U=1 "));
  lcd.setCursor(0, 1);
  lcd.print(F("Tnk Normal"));

  //SetupSDCardSwitch();
  SetupSDCardOperations();  
}


void loop() 
{
  const __FlashStringHelper *cStatus;

// NOTE: Consider running the loop() on a 1 second timer interrupt instead of a delay of 1 second
  delay(1000);
  
  DateTime now = rtc.now();

  bTurnOnUV = false;

  if ((tickCounterSec % DELAY_DIN_CHECKING_SEC) == 0)
  {
    // Sample digital inputs and set digital outputs approximately every 5 seconds
    
    digitalInputState_New = SampleDigitalInputs();

    switch (digitalInputState_New)
    {
      case (B00000000):   // Water level between the high and low levels, this is the normal case
                          // Note: Bypass Valve may be open if low level switch hit last,
                          //       or closed if high level switch hit last
        digitalOutputState = digitalOutputState | DOUT1;    // send one to DO1 to open inlet valve
        digitalOutputState = digitalOutputState | DOUT4;    // send one to DO4 to turn off flashing
        tickCounterMillisecondsBottomSwitch = 0;
        cStatus = F("Tnk Normal");
        break;
        
      case (B00000001):   // Pump shut off check float switches
        digitalOutputState = digitalOutputState | DOUT1;    // send one  to DO1 to open inlet valve
        digitalOutputState = digitalOutputState & (~DOUT2); // send zero to DO2 to turn off the pump
        digitalOutputState = digitalOutputState & (~DOUT4); // send zero to DO4 to turn on flashing
        cStatus = F("FLT SW ERR");
        UVtimer = UVtimer + 1;
        break;
        
      case (B00000010):   // Tank level low opening bypass
        digitalOutputState = digitalOutputState | DOUT0;    // send one  to DO0 to open bypass valve
        digitalOutputState = digitalOutputState | DOUT1;    // send one  to DO1 to open inlet valve
        digitalOutputState = digitalOutputState | DOUT4;    // send one to DO4 to turn off flashing
        tickCounterMillisecondsBottomSwitch = 0;
        UVtimer = UVtimer + 1;
        cStatus = F("Lo  Opn Bp");
        break;
        
      case (B00000011):   // Tank very low shutting off pump
        digitalOutputState = digitalOutputState | DOUT0;    // send one  to DO0 to open bypass valve
        digitalOutputState = digitalOutputState | DOUT1;    // send one  to DO1 to open inlet valve      
        digitalOutputState = digitalOutputState & (~DOUT2); // send zero to DO2 to turn off the pump
        //digitalOutputState = digitalOutputState & (~DOUT4); // send zero to DO4 to turn on flashing
        if (tickCounterMillisecondsBottomSwitch == 0)
        {
          tickCounterMillisecondsBottomSwitch = millis();
        }
        else
        {
          if ((tickCounterMillisecondsBottomSwitch + TIMER_BOTTOM_SWITCH_DELAY_MS) < millis())
          {
            digitalOutputState = digitalOutputState & (~DOUT4); // send zero to DO4 to turn on flashing
          }
        }
        
        cStatus = F("LO! Pm Off");
        UVtimer = UVtimer + 1;
        break;
        
      case (B00000100):   // Tank level high closing bypass
        digitalOutputState = digitalOutputState & (~DOUT0); // send zero to DO0 to close bypass valve
        digitalOutputState = digitalOutputState | DOUT2;    // send one to DO2 to turn on the pump
        if ((digitalOutputState & DOUT3) != DOUT3)
          bTurnOnUV = true;
        cStatus = F("Hi  Cls Bp");
        UVtimer = 0;
        break;
        
      case (B00001000):   // Tank very high shutting off pump
        digitalOutputState = digitalOutputState & (~DOUT0); // send zero to DO0 to close bypass valve
        digitalOutputState = digitalOutputState & (~DOUT1); // send zero to DO1 to close input valve
        digitalOutputState = digitalOutputState | DOUT2;    // send one to DO2 to turn on the pump
        if ((digitalOutputState & DOUT3) != DOUT3)
          bTurnOnUV = true;
        digitalOutputState = digitalOutputState & (~DOUT4); // send zero to DO4 to turn on flashing
        cStatus = F("FLT SW ERR");
        UVtimer = 0;
        break;
        
      case (B00001100):   // Tank very high shutting off pump
        digitalOutputState = digitalOutputState & (~DOUT0); // send zero to DO0 to close bypass valve
        digitalOutputState = digitalOutputState & (~DOUT1); // send zero to DO1 to close input valve
        digitalOutputState = digitalOutputState | DOUT2;    // send one to DO2 to turn on the pump
        if ((digitalOutputState & DOUT3) != DOUT3)
          bTurnOnUV = true;
        digitalOutputState = digitalOutputState & (~DOUT4); // send zero to DO4 to turn on flashing
        cStatus = F("HI! CkFilt");
        UVtimer = 0;
        break;
        
      default:   // Float switch error
        digitalOutputState = digitalOutputState & (~DOUT4); // send zero to DO4 to turn on flashing
        cStatus = F("FLT SW ERR");
        break;
    }

    if ((digitalInputState_New & B00000100) != B00000100)
    {
      tickCounterMilliseconds = 0;
      bSlowDischarge = false;
    }
    else
    {
      if (tickCounterMilliseconds == 0)
        tickCounterMilliseconds = millis();
      else
      {
        // Previously detected Tank level high
// NOTE: the Arduino IDE compiler fails to correctly do the unsigned long constant math on the following line ?!
//        if (millis() > (tickCounterMilliseconds + (unsigned long) SLOW_DSCHG_MILLISECONDS))
        if (millis() > (tickCounterMilliseconds + 600000L))
        {
          digitalOutputState = digitalOutputState & (~DOUT4); // send zero to DO4 to turn on flashing
          if (bSlowDischarge == false)
          {
            cStatus = F("SLOW DsChg");        
            LCDStatusUpdate_SDLogging(cStatus);
            bSlowDischarge = true;
          }
        }
      }
    }  
    LCDDigitalOutputUpdate();
    
    // Turn off UV sterilizers when discharge pump is off for more than 15 minutes
    if (UVtimer > UVtimerMax)
    {
      if ((digitalOutputState & DOUT3) == DOUT3)
      {
        digitalOutputState = digitalOutputState & (~DOUT3);   // send zero to DO3 to turn off UV
        digitalOutputState = digitalOutputState & (~DOUT4);   // send zero to DO4 to turn on flashing
        LCDDigitalOutputUpdate();
        LCDStatusUpdate_SDLogging(F("UV is OFF "));
      }
    }

    if (bTurnOnUV)
    {
        digitalOutputState = digitalOutputState | DOUT3;      // send one to DO3 to turn on the UV
        LCDDigitalOutputUpdate();
        LCDStatusUpdate_SDLogging(F("UV is ON  "));
    }

    if ((digitalInputState_New != digitalInputState_Saved) || bSDLogFail || bTurnOnUV)
    {
      digitalInputState_Saved = digitalInputState_New;
      LCDStatusUpdate_SDLogging(cStatus);
    }
  }

  SetDigitalOutputState();
  
  tickCounterSec++;

#if 1
  // Every minute clear the LCD and redisplay
  if (now.second() == 0)
      {
        lcd.clear();
        lcd.setCursor(0, 1);
        lcd.print(cStatus);
//        LCDDigitalOutputUpdate();
//        delay(1000);
      }
#endif

// Flash the colon in the time display every second
  lcd.setCursor(13,1);
  if ((tickCounterSec & B00000001) == B00000001)
  {
    lcd.print(F(":"));
    lcd.setCursor(15, 0);
    lcd.write(' ');  
  }
  else
  {
    lcd.print(F(" "));

    // Set upper right corner LCD to input switch value in Hex on even seconds
    lcd.setCursor(15, 0);
    char digitalInputDisplay;
    if (digitalInputState_Saved < 10)
      digitalInputDisplay = '0' + digitalInputState_Saved;
    else
      digitalInputDisplay = 'A' + (digitalInputState_Saved-10);    
    lcd.write(digitalInputDisplay);  
  }

#if 1
  // Detect time RESET_HOUR:00:0? and re_set()
  if (now.hour() == RESET_HOUR &&
      now.minute() == 0 &&
      now.second() < 10)
      {
        delay(10000);
        re_set();
      }
}
#endif

// Read all 4 float switches
// Note: with pullups the state is active low
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

// Initialize the SD for operations
// If the LOGGING.CSV file is not present create the file with the first line of column headings
void SetupSDCardOperations()
{   
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(F("*** STATUS ***  "));
  lcd.setCursor(0, 1);
  lcd.print(F("SD Init Start   "));

  if (!SD.begin(chipSelectSDCard)) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(F("*** ERROR ***   "));
    lcd.setCursor(0, 1);
    lcd.print(F("SD Init Failed  "));
    while (1);
  }

  delay(2000);
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(F("* Test clock.csv"));
  lcd.setCursor(0, 1);
  if (SD.exists("clock.csv")) 
  {
    lcd.print(F("  Set Clock     "));
    delay(2000);

    fileSDCard = SD.open("clock.csv");
    if (fileSDCard) 
    {
      if (fileSDCard.available())
      {
        char strClockSetting[128];
        fileSDCard.read(strClockSetting, sizeof(strClockSetting));
        strClockSetting[sizeof(strClockSetting)] = '\0';
        int iDateTime[6] = {0,0,0,0,0,0};
        char *ptr1 = &strClockSetting[0];      
        for (int i=0; i<6; i++)
        {
          char *ptr2 = strchr(ptr1,',');
          if (ptr2 != 0)
          {
            *ptr2 = '\0';
            iDateTime[i] = atoi(ptr1);
            ptr1 = &ptr2[1];
          }
          else
          {
            break;
          }
        }
          
        rtc.adjust(DateTime(iDateTime[0],iDateTime[1],iDateTime[2],iDateTime[3],iDateTime[4],iDateTime[5]));
        SD.remove("clock.csv");

        lcd.setCursor(0, 1);
        lcd.print(F("* removed *     "));
        delay(2000);
      
      }
      fileSDCard.close();
    } 
    
  } else 
  {
    lcd.print(F("* does not exist"));
  }
  delay(2000);


// open the file for reading:
  fileSDCard = SD.open("LOGGING.CSV");
  if (fileSDCard) 
  {
    if (fileSDCard.available())
    {
    }
    fileSDCard.close();
  } 
  else
  {
    fileSDCard = SD.open("LOGGING.CSV", FILE_WRITE);
    if (fileSDCard) 
    {
      fileSDCard.println(F("\"Date\",\"Time\",\"Dout\",\"Din\",\"Status\""));
      fileSDCard.close();
    }
    else
    {
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print(F("*** ERROR ***   "));
      lcd.setCursor(0, 1);
      lcd.print(F("SD Write Failed "));
      while (1);
    }
  }
  SD.end();

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(F("*** STATUS ***  "));
  lcd.setCursor(0, 1);
  lcd.print(F("SD Init Finish  "));
  delay(2000);
  lcd.clear();
  LCDDigitalOutputUpdate();
  LCDStatusUpdate_SDLogging(F("Start Up  "));
}

void LCDStatusUpdate_SDLogging(const __FlashStringHelper*status)
{
  lcd.setCursor(0, 1);
  lcd.print(status);

  DateTime now = rtc.now();      
  lcd.setCursor(10,1);
  lcd.print(F(" "));
  LCDPrintTwoDigits(now.hour());
  lcd.setCursor(14,1);
  LCDPrintTwoDigits(now.minute());   

  if (!SD.begin(chipSelectSDCard)) 
  {
    bSDLogFail = true;
    iToggle++;
    if ((iToggle & B00000001) == 0)
    {
      lcd.setCursor(0, 1);
      lcd.print(F("SD LogFail"));
    }
    return;
  }
  bSDLogFail = false;
  iToggle = 0;

//  if ((strcmp((const char*) status, "") != 0) || bForceOneMinuteLogging)
  {
    fileSDCard = SD.open("LOGGING.CSV", FILE_WRITE);
  
    // if the file opened okay, write to it:
    if (fileSDCard) 
    {
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
      SDPrintBinary(digitalOutputState,5);
      fileSDCard.print(",");
      SDPrintBinary(digitalInputState_Saved,4);
      fileSDCard.print(",");
      fileSDCard.print(status);
      fileSDCard.println("");
      fileSDCard.close();
      SD.end();
    } 
    else 
    {
      // if the file didn't open, display an error:
      lcd.setCursor(0, 0);
      lcd.print(F("*** ERROR ***   "));
      lcd.setCursor(0, 1);
      lcd.print(F("Open LOGGING.CSV"));
    }  
  }
}

//setup 8-bit, serial-in, parallel-out shift register
void Setup_74HC595()
{   
  pinMode(latchPin74HC595, OUTPUT);
  pinMode(dataPin74HC595, OUTPUT);  
  pinMode(clockPin74HC595, OUTPUT);
  
  SetDigitalOutputState();
}

void LCDDigitalOutputUpdate()
{
  DateTime now = rtc.now();      
  lcd.setCursor(10,1);
  lcd.print(F(" "));
  LCDPrintTwoDigits(now.hour());
  lcd.setCursor(14,1);
  LCDPrintTwoDigits(now.minute());   

  lcd.setCursor(0, 0);
  lcd.print(F("B="));
  if ((digitalOutputState & DOUT0) == 0)
    lcd.write('0');
  else
    lcd.write('1');
  lcd.print(F(" I="));
  if ((digitalOutputState & DOUT1) == 0)
    lcd.write('0');
  else
    lcd.write('1');
  lcd.print(F(" P="));
  if ((digitalOutputState & DOUT2) == 0)
    lcd.write('0');
  else
    lcd.write('1');
  lcd.print(F(" U="));
  if ((digitalOutputState & DOUT3) == 0)
    lcd.write('0');
  else
    lcd.write('1');
#if 0  // Disable asterisk display in upper right LCD when alarm on
  if ((digitalOutputState & DOUT4) != DOUT4)
    lcd.write('*');
  else
    lcd.write(' ');    
#endif
}

void SetDigitalOutputState()
{
  int iOutput = digitalOutputState;
  digitalWrite(latchPin74HC595, LOW);          //Pull latch LOW to start sending data
  shiftOut(dataPin74HC595, clockPin74HC595, MSBFIRST, iOutput);         //Send the data
  digitalWrite(latchPin74HC595, HIGH);         //Pull latch HIGH to stop sending data
}

void LCDPrintTwoDigits(int iVal)
{
  if (iVal < 10)
    lcd.print(F("0"));
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
