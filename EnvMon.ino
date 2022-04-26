#include <DS3232RTC.h>    //http://github.com/JChristensen/DS3232RTC
#include <Time.h>         //http://www.arduino.cc/playground/Code/Time
#include <Wire.h>         //http://arduino.cc/en/Reference/Wire (included with Arduino IDE)
#include <LiquidCrystal.h>
#include <SPI.h>
#include <SD.h>
#include <string.h>
#include <stdlib.h>

#define FILENAME "ENVLOG.TXT"
//----------------------------------------------------------------------------
/*
  Connect DS3231 RTC as follows:

  SDA -> Arduino A4
  SCL -> Arduino A5

*/
//----------------------------------------------------------------------------
/* Connect standard HD44780 based 2x16 module
   as follows:

  LCD                Arduino
  ===                =======
* Pin  1 (Vcc)   ->  Gnd
* Pin  2 (GND)   ->  +5V
* Pin  3 (Vbias) ->  contrast potentiometer wiper
* Pin  4 (RS)    ->  Pin 7
* Pin  5 (R/W*)  ->  GND
* Pin  6 (E)     ->  Pin 6
* Pin 11 (DB4)   ->  Pin 5
* Pin 12 (DB5)   ->  Pin 4
* Pin 13 (DB6)   ->  Pin 3
* Pin 14 (DB7)   ->  Pin 2
*/
//----------------------------------------------------------------------------
#define RS 7
#define ENABLE 6
#define DB4 5
#define DB5 4
#define DB6 3
#define DB7 2
//----------------------------------------------------------------------------
// Declare the lcd structure globally so it can be updated in functions
   LiquidCrystal lcd(RS, ENABLE, DB4, DB5, DB6, DB7);
//----------------------------------------------------------------------------
// Declare the RTC structure so that all functions can access it
   DS3232RTC RTC;
//----------------------------------------------------------------------------
// Logging to SD card provided by SD library via SeeedStudio SD Shield V3.1
/*
    Connect SD card to SPI bus as follows:
    MOSI - pin 11
    MISO - pin 12
    CLK  - pin 13
    CS   - pin 10
*/
#define SDCS  10
//----------------------------------------------------------------------------
/*
 * Start of global variables
 *
 */
static char  strDate[12];                     // YYYY/MM/DD + NUL
static char  strTime[10];                     // HH:MM:SS + NUL
static char  strTemperature[8];               // -tt.tt + NULL
static float minTemp = 99.9;                  // minimum temperature observed
static float maxTemp = -99.9;                 // maximum temperature observed
// Initial values set to extreme limits to ensure that the first reading will
//  set max and min to observed values.
//----------------------------------------------------------------------------
// Convert normal decimal numbers to binary coded decimal
byte decToBcd(byte val) {
   return( (val/10*16) + (val%10) );
}
//----------------------------------------------------------------------------
// Convert binary coded decimal to normal decimal numbers
byte bcdToDec(byte val) {
   return( (val/16*10) + (val%16) );
}
//----------------------------------------------------------------------------
void LCDDigits(int digits) {
   // utility function for digital clock display: prints leading 0
   if(digits < 10)
      lcd.print('0');
   lcd.print(digits);
}
//----------------------------------------------------------------------------
void printDigits(int digits) {
   // utility function for digital clock display: prints leading 0
   if(digits < 10)
      Serial.print('0');
   Serial.print(digits);
}
//----------------------------------------------------------------------------
//something fatal happened - flash onboard LED as sign of trouble
void halt(int ontime, int offtime) {
   while(1) {
      digitalWrite(13, HIGH);
      delay(ontime);
      digitalWrite(13, LOW);
      delay(offtime);
   } //while
} // halt()
//----------------------------------------------------------------------------
//print date and time to Serial
void printDateTime(time_t t) {
   printDate(t);
   Serial.print(' ');
   printTime(t);
}
//----------------------------------------------------------------------------
//print time to Serial
void printTime(time_t t) {
   printI00(hour(t), ':');
   printI00(minute(t), ':');
   printI00(second(t), ' ');
}
//----------------------------------------------------------------------------
//print date to Serial
void printDate(time_t t) {
   Serial.print( (year(t)));
   Serial.print('/');
   Serial.print(month(t));
   Serial.print('/');
   printI00(day(t), 0);
}
//----------------------------------------------------------------------------
//Print an integer in "00" format (with leading zero),
//followed by a delimiter character to Serial.
//Input value assumed to be between 0 and 99.
void printI00(int val, char delim) {
   if (val < 10)
      Serial.print('0');
   Serial.print( (val));
   if (delim > 0)
      Serial.print(delim);
   return;
}
//----------------------------------------------------------------------------
// ds => DS3231 temperature
void printSensors(float ds, char delim) {    //display sensor readings on serial console
   printTemp(ds);  //display temperature
   if (delim > 0)
      Serial.print(delim);
}
//----------------------------------------------------------------------------
void printTemp(float temp) {
   Serial.print(temp, 1); //print only 1 decimal place
}
//----------------------------------------------------------------------------
// ds => DS3231 temperature
// >>>> This does not appear to ever be called <<<<
/*
void lcdSensors(float ds) {
   lcd.setCursor(0,1);  //1st position of line 2 
   lcdPrintTemp1(round(ds));
}
*/
//----------------------------------------------------------------------------
void lcdPrintTemp1(int t) {
   lcd.setCursor(11, 1);            // First (0) column of 2nd (1) row
   lcd.print(t, DEC);
   lcd.write(byte(223));           //the degree symbol
   lcd.print("C");
}
//----------------------------------------------------------------------------
void lcdDateTime(time_t t) { //display date and time on LCD - strftime would make this easier
   lcd.clear();
   lcdTime(t);
   lcdDate(t);
}
//----------------------------------------------------------------------------
void lcdTime(time_t t) {
   String strTim = String("");   //8 chars + 1 terminator
   String delim  = String(":");
   String zero   = String("0");

   strTim += (hour(t)   <= 9 ? (zero + hour(t))   : hour(t)) + delim;
   strTim += (minute(t) <= 9 ? (zero + minute(t)) : minute(t))  + delim;
   strTim += (second(t) <= 9 ? (zero + second(t)) : second(t));

   lcd.setCursor(0, 0);  
   lcd.print(strTim);
}
//----------------------------------------------------------------------------
void lcdDate(time_t t) {
   String strDat = String(""); //10 chars + 1 terminator
   String delim  = String("/");
   String zero   = String("0");

   //strDat += year(t) + delim;  //not enought room on 16x2 display
   strDat += (month(t) <=9 ? (zero + month(t)) : month(t)) + delim;
   strDat += (day(t) <= 9 ? (zero + day(t)) : day(t));

   lcd.setCursor(11, 0); //just a guess for now
   lcd.print(strDat);
}
//----------------------------------------------------------------------------
void sdLog(time_t t, float t1) { //time, temp
   String strDat = String("");
   String delim  = String(",");
   String sep    = String("/");
   String colon  = String(":");
   String zero   = String("0");
   String nl     = String("\n");
   
   strDat += year(t) + sep;
   strDat += (month(t) <=9 ? (zero + month(t)) : month(t)) + sep;
   strDat += (day(t) <= 9 ? (zero + day(t)) : day(t)) + delim;
   
   strDat += (hour(t) <=9 ? (zero + hour(t)) : hour(t)) + colon;
   strDat += (minute(t) <=9 ? (zero + minute(t)) : minute(t)) + delim;
   
   strDat += t1;
   
   File dataFile = SD.open("ENVLOG.TXT", FILE_WRITE);
   // if the file is available, write to it:
   if (dataFile) {
      dataFile.println(strDat);   //Write results to file 
      dataFile.close();
      // print to the serial port too: can remove this once verified operational
      //Serial.print(strDat);
   }
   // if the file isn't open, pop up an error:
   else {
      Serial.println("error opening datalog.txt");
      halt(200,200);
   }
}
   
//----------------------------------------------------------------------------
void setup() {
   // Establish on-board LED as status monitor and turn it off
   pinMode(13, OUTPUT);
   digitalWrite(13, LOW);

   lcd.begin(16,2);           //set display mode to 16 cols x 2 lines
   lcd.clear();               //clear the display

   //Setup serial monitor
   Serial.begin(9600);
   Serial.println(F("Setting up..."));

   Serial.println(F("Initializing SD card..."));
   pinMode(SDCS, OUTPUT); //if your SD shield doesn't use pin 10, set pin 10 to output anyway or SD library has problems
   // see if the card is present and can be initialized:
   if (!SD.begin(SDCS)) {
      Serial.println(F("Error initializing SD card"));
      halt(200, 200); // Endlessly blink the on-board LED to indicate SD failure
   }
   else {//if
      Serial.println(F("SD card initialized."));
   }

   Serial.println(F("Setup RTC..."));
   RTC.begin();
   setSyncProvider(RTC.get);   // the function to get the time from the RTC
   if(timeStatus() != timeSet) {
      Serial.println(F("Unable to sync with the RTC"));
      halt(500, 200); // Endlessly blink the on-board LED to indicate SD failure
   } //if
   else {
      Serial.println(F("RTC has set the system time"));
   } //else
} // setup()

void loop () {
   static time_t tLast;                      // epoch time of last check
   struct tm * currentTime;                  // current time as a stuct tm
   static float curTemp = 99.9;              // current time - bogus value

   time_t tNow = now();                      // epoch time of current time

   if ( (tNow = now() ) != tLast) {          //when true the time has changed (seconds roll-over)
      tLast = tNow;
      currentTime = localtime(&tNow);        // get local time as a struct tm

      // Print date time and temperature to LCD display
      lcdDateTime(tNow);
      lcdPrintTemp1((int)curTemp);

      memset(strDate, 0, sizeof(strDate));   // clear out the string buffers
      memset(strTime, 0, sizeof(strDate));
      memset(strTemperature, 0, sizeof(strTemperature));

      strftime(strDate, sizeof(strDate), "%Y/%m/%d", currentTime);   // load buffers with date
      strftime(strTime, sizeof(strTime), "%H:%M", currentTime);      // time
      sprintf(strTemperature, "%2.2f", curTemp);                     // and temperature

      if (((minute(tNow) % 2) == 0) && (second(tNow) == 0)) { //every 2 minutes (DS3231 updates every 64 seconds)  & ((ti - lastminute) >=120)
         digitalWrite(13, HIGH);
         delay(120);
         digitalWrite(13, LOW);     // Put in a little heartbeat for debugging purposes

         // Read the RTC temperature as Celsius
         curTemp = RTC.temperature() / 4.;
         
         sdLog(currentTime, curTemp);
         //Serial.print(F("\n"));

         printDateTime(currentTime);         //display on Serial console
         printSensors(curTemp, ',');         //display sensor readings on serial console every two minutes
         Serial.print(F("\n"));

         if (curTemp > maxTemp) {            // Do we have a new high temp?
            maxTemp = curTemp;               // Remember the new high
            // Remember when this happened
         }

         if (curTemp < minTemp) {            // Do we have a new low temp?
            minTemp = curTemp;               // Remember the new low
            // Remember when this happened
         }
      } //if
   } //if
}
