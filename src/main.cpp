#include <Arduino.h>
#include <avr/power.h>
#include "LowPower.h"
#include <Wire.h>
#include <SPI.h>
#include "ClosedCube_HDC1080.h"
#include "SD.h"
#include <FastLED.h>
#include <RtcDS3231.h>
#define PRTIM4 4
#define power_timer4_disable() (PRR1 |= (uint8_t)(1 << PRTIM4))

#define DEBUG 0
#if DEBUG == 0
// disable Serial output
#define Serial DisabledSerial
static class
{
public:
  void begin(...) {}
  void print(...) {}
  void println(...) {}
} Serial;
#endif

#define countof(a) (sizeof(a) / sizeof(a[0]))

#define NUM_LEDS 1
#define LED_PIN 10
CRGB leds[NUM_LEDS];
uint8_t ledFadeTo = 10; // reduce brightness

uint8_t CARD_PIN = 9;

ClosedCube_HDC1080 sensor;
Sd2Card card;
File dataFile;
File root;
char filename[11];

RtcDS3231<TwoWire> Rtc(Wire);

void flash(int error, CRGB color = CRGB::Green)
{
  CRGB fadedColor = color %= ledFadeTo;
  if (error)
  {
    //flash error
    for (int i = 0; i < error; i++)
    {
      leds[0] = fadedColor;
      FastLED.show();
      delay(100);
      leds[0] = CRGB::Black;
      FastLED.show();
      delay(100);
    }
  }
  else
  {
    // flash OK
    leds[0] = fadedColor;
    FastLED.show();
    delay(200);
    leds[0] = CRGB::Black;
    FastLED.show();
    delay(200);
    leds[0] = fadedColor;
    FastLED.show();
    delay(200);
    leds[0] = CRGB::Black;
    FastLED.show();
  }
}

RtcDateTime getDateTime()
{
  if (!Rtc.IsDateTimeValid())
  {
    if (Rtc.LastError() != 0)
    {
      Serial.print(F("RTC communications error = "));
      Serial.println(Rtc.LastError());
      flash(10, CRGB::Red);
    }
    else
    {
      Serial.println(F("RTC lost confidence in the DateTime! Check battery"));
      flash(5, CRGB::Yellow);
    }
  }

  return Rtc.GetDateTime();
}

void setupRTC()
{
  Serial.print(F("Compiled: "));
  Serial.print(__DATE__);
  Serial.print(F(" "));
  Serial.println(__TIME__);

  Rtc.Begin();
  RtcDateTime compiled = RtcDateTime(__DATE__, __TIME__);

  if (!Rtc.IsDateTimeValid())
  {
    if (Rtc.LastError() != 0)
    {
      // see https://www.arduino.cc/en/Reference/WireEndTransmission
      Serial.print(F("RTC communications error = "));
      Serial.println(Rtc.LastError());
      flash(10, CRGB::Red);
    }
    else
    {
      Serial.println(F("RTC lost confidence in the DateTime! Check battery"));
      Rtc.SetDateTime(compiled);
    }
  }

  if (!Rtc.GetIsRunning())
  {
    Serial.println(F("RTC was not actively running, starting now"));
    Rtc.SetIsRunning(true);
  }

  RtcDateTime now = Rtc.GetDateTime();
  if (now < compiled)
  {
    Serial.println(F("RTC is older than compile time!  (Updating DateTime)"));
    Rtc.SetDateTime(compiled);
    flash(2, CRGB::Cyan);
  }
  else if (now > compiled)
  {
    Serial.println(F("RTC is newer than compile time. (this is expected)"));
    flash(0, CRGB::Blue);
  }
  else if (now == compiled)
  {
    Serial.println(F("RTC is the same as compile time! (not expected but all is fine)"));
    flash(2, CRGB::Purple);
  }

  // never assume the Rtc was last configured by you, so
  // just clear them to your needed state
  Rtc.Enable32kHzPin(false);
  Rtc.SetSquareWavePin(DS3231SquareWavePin_ModeNone);
}

void setupSensor()
{
  sensor.begin(0x40);
  Serial.print(F("Manufacturer ID=0x"));
  Serial.println(sensor.readManufacturerId(), HEX);
  Serial.print(F("Device ID=0x"));
  Serial.println(sensor.readDeviceId(), HEX);
  flash(0, CRGB::Orange);
}

void setupSD()
{
  pinMode(CARD_PIN, INPUT_PULLUP);
  Serial.println(F("Initializing SD card..."));
  pinMode(SS, OUTPUT);
  if (!SD.begin(SS))
  {
    Serial.println(F("Card failed, or not present, blinking LED"));
    flash(5, CRGB::Red);
    return;
  }
  Serial.println(F("Card initialized."));
  root = SD.open("/");
  root.rewindDirectory();
  // check which is the next available filename
  int fileCounter = 0;
  while (true)
  {
    File entry = root.openNextFile();
    if (!entry)
    {
      break;
    }
    if (entry.isDirectory())
    {
      continue;
    }
    String fileName = String(entry.name());
    if (fileName.startsWith("DATA"))
    {
      fileCounter++;
    }
    entry.close();
  }
  snprintf(filename, countof(filename), "data%02d.csv", fileCounter % 100);
  if (fileCounter % 100 < 100)
  {
    SD.remove(filename); // we wrapped around to file zero, so we clear the old file
  }
  dataFile = SD.open(filename, FILE_WRITE);
  if (!dataFile)
  {
    Serial.print(F("error opening file "));
    Serial.println(filename);
    flash(5, CRGB::Orange);
    return;
  }
  dataFile.println("datetime,temperature,humidity");
  dataFile.flush();
  dataFile.close();
  Serial.print(F("Data file OK: "));
  Serial.println(filename);
  flash(0, CRGB::Green); //all good
}

uint8_t checkSD()
{
  return card.init(SPI_HALF_SPEED, SS);
}

void writeToSD(char *str)
{
  if (!SD.exists(filename))
  {
    Serial.print(filename);
    Serial.println(F(" does not exist."));
    flash(2, CRGB::Orange);
    return;
  }
  dataFile = SD.open(filename, FILE_WRITE);
  if (!dataFile)
  {
    Serial.print(F("error opening file "));
    Serial.println(filename);
    flash(5, CRGB::Orange);
    return;
  }
  dataFile.println(str);
  dataFile.close();
  flash(1, CRGB::Green);
}

void setup()
{
  if (DEBUG == 0)
  {
    delay(10000); // VERY IMPORTANT, allow time to reprogram
    ADCSRA &= ~(1 << ADEN);
    power_adc_disable();
    power_usart0_disable();
    power_timer1_disable();
    power_timer3_disable();
    power_timer4_disable();
    power_usart1_disable();
    power_usb_disable();
  }

  FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, NUM_LEDS);
  Serial.begin(9600);
  Serial.println(F("Starting setup"));
  setupRTC();
  setupSensor();
  setupSD();
  Serial.println(F("Init complete"));
}

void loop()
{
  RtcDateTime now = getDateTime();

  double temp = sensor.readTemperature();
  double hum = sensor.readHumidity();
  char tempstr[7];
  char humstr[7];
  dtostrf(temp, 4, 2, tempstr);
  dtostrf(hum, 4, 2, humstr);
  char csvrow[34];
  snprintf_P(csvrow,
             countof(csvrow),
             PSTR("%04u-%02u-%02u %02u:%02u:%02u,%s,%s"),
             now.Year(),
             now.Month(),
             now.Day(),
             now.Hour(),
             now.Minute(),
             now.Second(),
             tempstr,
             humstr);

  Serial.println(csvrow);
  if (digitalRead(CARD_PIN) == 0 && checkSD()) // card inserted
  {
    SD.begin(SS);
    writeToSD(csvrow);
  }
  else
  {
    SD.end();
    Serial.println(F("No SD"));
    flash(2, CRGB::Red);
  }

  // ADC_ON so that it doesn't touch ADC (it was already disabled)
  // last 2 params have no effect for the atmega32u4
  if (DEBUG == 0)
  {
    LowPower.powerSave(SLEEP_8S, ADC_ON, BOD_OFF, TIMER2_OFF);
    LowPower.powerSave(SLEEP_8S, ADC_ON, BOD_OFF, TIMER2_OFF);
  }
  else
  {
    delay(5000);
  }
}
