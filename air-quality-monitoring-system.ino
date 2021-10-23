#include <LiquidCrystal_I2C.h>
#include "RTClib.h"
#include <SD.h>
#include <DHT.h>
#include <MQ135.h>
#include <EEPROM.h>

// if calibrating set to true
bool isCalibrating = false;
float rZero = 0.0;

//~~~~ LIBRARIES & PIN CONFIGURATIONS ~~~~~~~//
HardwareSerial &gsmSerial = Serial1;
LiquidCrystal_I2C lcd = LiquidCrystal_I2C(0x27, 20, 4);
RTC_DS3231 rtc;
File file;
DHT dht(3, DHT11);
const byte co2Pin = A0;
MQ135 co2Sensor = MQ135(co2Pin);
const byte buzzerPin = 4;

//~~~~ GLOBAL VARIABLES ~~~~~~~//
// gsm
bool isDoneCheckingStatus = false;
bool isCheckingNetworkStatus = false;
bool isGettingTimeAndDate = false;
bool isResponseReady = false;
bool isFinishSyncing = false;
const byte numChars = 64;
char gsmResponse[numChars];
unsigned long prevMillis = 0;
const unsigned int gsmTimeout = 1000;

// network time and date
struct cellularNetwork {
  char _year[5];
  char _month[3];
  char _date[3];
  char _hour[3];
  char _minute[3];
  char _second[3];
} network;

// temp sensor
float humidity = 0.0;
float temperature = 0.0;
const float temperatureThreshold = 36.0;
const float humidityThreshold = 90.0;

// co2 sensor
float ppm = 0.0;
const float ppmThreshold = 1000.0;
const float earlyWarningThreshold = 900.0;
//unsigned int preheatTime = 300000;
unsigned int preheatTime = 5000;
float currentMaxPpm = 0.0;

// SD / logging
//const unsigned long logTimeout = 3600000;
const unsigned long logTimeout = 300000;
unsigned long lastLogTime = 0;
const char *tabChar = "\t";
const char *colonChar = ":";
const char *forwardSlashChar = "/";
const char *txtFilename = "dev.txt"; // for development
//const char *txtFilename = "logs.txt"; // for production

// buzzer
unsigned long buzzStartedAt = 0;
const unsigned int buzzTimeout = 5000;

// sms
bool hasBeenNotifiedTemperature = false;
bool hasBeenNotifiedHumidity = false;
bool hasBeenNotifiedCo2 = false;
bool hasStartedSendingSms = false;

unsigned long startedAt = 0;
const unsigned int smsTimeout = 1000;
char currentCommand[8];
char prevCommand[8];
char msgBuff[64];

const byte maxMessageSentCount = 1;
byte messageSentCount = 0;
char message[64];
bool isSendingNotification = false;

// sensors on check
byte humiditySampleCount = 0;
byte ppmSampleCount = 0;
byte temperatureSampleCount = 0;
const byte maxSampleCount = 2;
char currentSensorOnCheck[16] = "humidity";

// EEPROM
struct Date {
  int _month;
  int _day;
  int _year;
};
int dateLastCheckedAddress = 100;
int prevMaxReadingAddress = 200;

// icons
byte clockIcon[] = {
  B01010,
  B01110,
  B10001,
  B10101,
  B10111,
  B10001,
  B10001,
  B01110
};
byte temperatureIcon[] = {
  B01110,
  B10001,
  B10111,
  B10001,
  B10111,
  B10001,
  B10001,
  B01110
};
byte humidityIcon[] = {
  B00000,
  B00100,
  B01110,
  B11111,
  B11101,
  B11101,
  B11001,
  B01110
};
byte co2Icon[] = {
  B00010,
  B00101,
  B00010,
  B01100,
  B10010,
  B10010,
  B01100,
  B00000
};

// general
const unsigned int initScreenDelay = 2000;
unsigned long timeElapsed = 0;
unsigned long lastSensorRead = 0;
const unsigned int sensorReadTimeout = 2000;
unsigned long lastScreenRefresh = 0;
const unsigned int screenTimeout = 1000;
//~~~~~~~~~~~~~~~~~~~~~//

void setup() {
  initializeLcd();
  welcomeScreen();
  initializeGsm();
  initializeRtc();
  initializeSdCard();
  initializeTempAndHumidity();
  initializeCo2();
  initializeBuzzer();
  done();
}

void loop() {
  timeElapsed = millis();
  readSensorData();
  sendNotification();
  turnOffBuzzAlert();
  logReadings(false);
  showSensorDataAndTime();
}

//~~~ INITIALIZATION FUNCTIONS ~~~//
void initializeLcd() {
  lcd.init();
  lcd.backlight();
  lcd.createChar(0, clockIcon);
  lcd.createChar(1, temperatureIcon);
  lcd.createChar(2, humidityIcon);
  lcd.createChar(3, co2Icon);
}
void welcomeScreen() {
  lcd.setCursor(5, 0);
  lcd.print("Welcome to");
  lcd.setCursor(1, 1);
  lcd.print("Indoor Air Quality");
  lcd.setCursor(1, 2);
  lcd.print("Monitoring System");
  delay(initScreenDelay * 2);
}
void initializeGsm() {
  gsmSerial.begin(9600);
  lcd.clear();
  lcd.setCursor(3, 1);
  lcd.print("Connecting to");
  lcd.setCursor(2, 2);
  lcd.print("cellular network");

  while (!isDoneCheckingStatus) {
    if (millis() - prevMillis >= gsmTimeout) {
      prevMillis = millis();
      if (!isCheckingNetworkStatus) {
        gsmSerial.println("AT+CREG?\r");
        isCheckingNetworkStatus = true;
      }
    }
    readGsmResponse();
    getNetworkStatus();
  }
}
void initializeRtc() {
  delay(initScreenDelay);
  lcd.clear();
  lcd.setCursor(2, 1);
  lcd.print("Initializing RTC");
  if (!rtc.begin()) {
    lcd.clear();
    lcd.setCursor(2, 5);
    lcd.print("RTC failed");
    while (1);
  }

  // adjust time according to compile time
  // rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));

  // adjust time manually
  // year, month, date, hour, min, second
  // rtc.adjust(DateTime(2021, 8, 3, 10, 29, 0));
}
void initializeSdCard() {
  delay(initScreenDelay);
  lcd.clear();
  lcd.setCursor(0, 1);
  lcd.print("Initializing SD card");

  if (!SD.begin(53)) {
    lcd.clear();
    lcd.setCursor(1, 1);
    lcd.print("SD card failed, or");
    lcd.setCursor(3, 2);
    lcd.print("is not present");
    while (1);
  }

  prepareTextFile();
}
void initializeTempAndHumidity() {
  delay(initScreenDelay);
  lcd.clear();
  lcd.setCursor(1, 1);
  lcd.print("Preparing humidity");
  lcd.setCursor(0, 2);
  lcd.print("& temperature sensor");
  dht.begin();
}
void initializeCo2() {
  delay(initScreenDelay);
  pinMode(co2Pin, INPUT);
  lcd.clear();
  lcd.setCursor(3, 1);
  lcd.print("Preheating CO2");
  lcd.setCursor(0, 2);
  lcd.print("sensor, please wait");
  while (preheatTime > 0) {
    preheatTime -= 1000;
    delay(1000);
  }
}
void initializeBuzzer() {
  pinMode(buzzerPin, OUTPUT);
}
void done() {
  delay(initScreenDelay);
  lcd.clear();
  lcd.setCursor(2, 1);
  lcd.print("Device is ready!");
  buzz(true);
  delay(20);
  buzz(false);
  delay(initScreenDelay);
  lcd.clear();
}
//~~~~~~~~~~~~~~~~~~~~~//

//~~~ MAIN FUNCTIONS ~~~//
void sendNotification() {
  if (isSendingNotification) {
    bool isSent = sendSms(message);
    if (isSent) {
      if (messageSentCount <= maxMessageSentCount) {
        messageSentCount++;
        return;
      }
      if (messageSentCount >= maxMessageSentCount) {
        if (!strcmp(currentSensorOnCheck, "co2")) {
          hasBeenNotifiedCo2 = true;
        }
        if (!strcmp(currentSensorOnCheck, "temperature")) {
          hasBeenNotifiedTemperature = true;
        }
        if (!strcmp(currentSensorOnCheck, "humidity")) {
          hasBeenNotifiedHumidity = true;
        }

        message[0] = NULL;
        isSendingNotification = false;
        messageSentCount = 0;
        setNextSensor();
      }
    }
  }
}
void readSensorData() {
  if (timeElapsed - lastSensorRead >= sensorReadTimeout) {
    lastSensorRead = timeElapsed;

    ppm = co2Sensor.getPPM();

    if (!isnan(dht.readTemperature()) && !isnan(dht.readHumidity())) {
      temperature = dht.readTemperature();
      humidity = dht.readHumidity();
    }

    if (isCalibrating) {
      rZero = co2Sensor.getRZero();
    }

    checkCo2();
    checkTemperature();
    checkHumidity();
  }
}
void checkCo2() {
  if (isSendingNotification || strcmp(currentSensorOnCheck, "co2")) {
    return;
  }

  if (ppm >= ppmThreshold || ppm >= earlyWarningThreshold) {
    if (hasBeenNotifiedCo2) {
      setNextSensor();
      return;
    }

    if (strlen(message) > 0) {
      return;
    }

    if (ppmSampleCount < maxSampleCount) {
      ppmSampleCount++;
      return;
    }

    char ppmStr[16];
    dtostrf(ppm, 4, 2, ppmStr);
    sprintf(message, "Co2 concentration is %s ppm and is not safe!", ppmStr);

    buzzStartedAt = timeElapsed;
    buzz(true);

    isSendingNotification = true;
    messageSentCount++;

    currentMaxPpm = ppm;
    checkNewMaximum();

    return;
  }

  ppmSampleCount = 0;
  if (!hasBeenNotifiedCo2) {
    setNextSensor();
  } else {
    hasBeenNotifiedCo2 = false;
  }
}
void checkTemperature() {
  if (isSendingNotification || strcmp(currentSensorOnCheck, "temperature")) {
    return;
  }

  if (temperature >= temperatureThreshold) {
    if (hasBeenNotifiedTemperature) {
      setNextSensor();
      return;
    }

    if (strlen(message) > 0) {
      return;
    }

    if (temperatureSampleCount < maxSampleCount) {
      temperatureSampleCount++;
      return;
    }

    char tempStr[16];
    dtostrf(temperature, 4, 2, tempStr);
    sprintf(message, "Temperature is %s degree celsius and is not safe!", tempStr);

    isSendingNotification = true;
    messageSentCount++;
    return;
  }

  temperatureSampleCount = 0;
  if (!hasBeenNotifiedTemperature) {
    setNextSensor();
  } else {
    hasBeenNotifiedTemperature = false;
  }
}
void checkHumidity() {
  if (isSendingNotification || strcmp(currentSensorOnCheck, "humidity")) {
    return;
  }

  if (humidity >= humidityThreshold) {
    if (hasBeenNotifiedHumidity) {
      setNextSensor();
      return;
    }

    if (strlen(message) > 0) {
      return;
    }

    if (humiditySampleCount < maxSampleCount) {
      humiditySampleCount++;
      return;
    }

    char humStr[16];
    dtostrf(humidity, 4, 2, humStr);
    sprintf(message, "Humidity is %s%% and is not safe!", humStr);

    isSendingNotification = true;
    messageSentCount++;
    return;
  }

  humiditySampleCount = 0;
  if (!hasBeenNotifiedHumidity) {
    setNextSensor();
  } else {
    hasBeenNotifiedHumidity = false;
  }
}

void checkNewMaximum() {
  Date dateLastChecked;
  float prevMaxReading;

  EEPROM.get(dateLastCheckedAddress, dateLastChecked);
  EEPROM.get(prevMaxReadingAddress, prevMaxReading);

  DateTime current = rtc.now();
  int currentMonth = current.month();
  int currentDay = current.day();
  int currentYear = current.year();

  if (
    dateLastChecked._month == currentMonth &&
    dateLastChecked._day == currentDay &&
    dateLastChecked._year == currentYear
  ) {
    if (currentMaxPpm < prevMaxReading) {
      return;
    }
  }

  logReadings(true);

  EEPROM.put(prevMaxReadingAddress, currentMaxPpm);

  Date currentDate;
  currentDate._month = currentMonth;
  currentDate._day = currentDay;
  currentDate._year = currentYear;
  EEPROM.put(dateLastCheckedAddress, currentDate);
}
void logReadings(bool isForced) {
  if (( timeElapsed - lastLogTime >= logTimeout ) || isForced) {
    lastLogTime = timeElapsed;
    DateTime current = rtc.now();

    file = SD.open(txtFilename, FILE_WRITE);

    file.print(temperature);
    file.print(tabChar);

    file.print(humidity);
    file.print(tabChar);

    file.print(ppm);
    file.print(tabChar);

    if (isCalibrating) {
      file.print(rZero);
      file.print(tabChar);
    }

    if (isForced) {
      file.print(currentMaxPpm);
    } else {
      file.print("");
    }
    file.print(tabChar);

    file.print(current.hour());
    file.print(colonChar);
    file.print(current.minute());
    file.print(tabChar);

    file.print(current.month());
    file.print(forwardSlashChar);
    file.print(current.day());
    file.print(forwardSlashChar);
    file.println(current.year());

    file.close();
  }
}
void showSensorDataAndTime() {
  if (timeElapsed - lastScreenRefresh >= screenTimeout) {
    lastScreenRefresh = timeElapsed;
    showCo2Ppm();
    showTemperature();
    showHumidity();
    showTime();
  }
}

void showTime() {
  DateTime current = rtc.now();

  char currentHourBuff[4];
  char currentMinuteBuff[4];

  int currentHour = current.hour();
  int currentMinute = current.minute();

  if (currentHour < 10) {
    sprintf(currentHourBuff, "0%i", currentHour);
  } else {
    sprintf(currentHourBuff, "%i", currentHour);
  }
  if (currentMinute < 10) {
    sprintf(currentMinuteBuff, "0%i", currentMinute);
  } else {
    sprintf(currentMinuteBuff, "%i", currentMinute);
  }

  lcd.setCursor(14, 0);
  lcd.write(0);
  lcd.setCursor(15, 0);
  lcd.print(currentHourBuff);
  lcd.setCursor(17, 0);
  lcd.print(colonChar);
  lcd.setCursor(18, 0);
  lcd.print(currentMinuteBuff);
}
void showCo2Ppm() {
  char ppmStr[16];
  dtostrf(ppm, 4, 2, ppmStr);

  lcd.setCursor(0, 1);
  lcd.write(3);
  lcd.setCursor(2, 1);
  lcd.print("CO2");
  lcd.setCursor(7, 1);
  lcd.print(ppmStr);

  if (strlen(ppmStr) == 6) {
    lcd.setCursor(13, 1);
    lcd.print("       ");
  }
}
void showTemperature() {
  char tempStr[16];
  dtostrf(temperature, 4, 2, tempStr);

  lcd.setCursor(0, 2);
  lcd.write(1);
  lcd.setCursor(2, 2);
  lcd.print("TEMP");
  lcd.setCursor(7, 2);
  lcd.print(tempStr);

  if (strlen(tempStr) == 5) {
    lcd.setCursor(13, 2);
  }
  if (strlen(tempStr) == 6) {
    lcd.setCursor(14, 2);
  }

  lcd.print("\xdf");
  lcd.print("C    ");
}
void showHumidity() {
  lcd.setCursor(0, 3);
  lcd.write(2);
  lcd.setCursor(2, 3);
  lcd.print("HUM");
  lcd.setCursor(7, 3);
  lcd.print(humidity);

  if (humidity >= 100) {
    lcd.setCursor(15, 3);
  }
  if (humidity < 100 && humidity > 10) {
    lcd.setCursor(13, 3);
  }
  if (humidity < 10) {
    lcd.setCursor(12, 3);
  }

  lcd.print("%   ");
}
//~~~~~~~~~~~~~~~~~~~~~//

//~~~ HELPER FUNCTIONS ~~~//
void getNetworkStatus() {
  if (isResponseReady) {
    static byte counter = 0;
    char *response;
    char *mode;
    char *networkStatus;

    response = strstr(gsmResponse, "+CREG: ");
    mode = strtok(response, ",");
    if (mode != NULL) {
      networkStatus = strtok(NULL, ",");
      if (networkStatus != NULL) {
        if (*networkStatus == '1') {
          isDoneCheckingStatus = true;
          return;
        }
      }
    }

    isResponseReady = false;
    isCheckingNetworkStatus = false;
  }
}
void prepareTextFile() {
  if (SD.exists(txtFilename)) {
    return;
  }

  file = SD.open(txtFilename, FILE_WRITE);
  if (file) {
    file.print("Temperature \t");
    file.print("Humidity \t");
    file.print("Co2 (ppm) \t");

    if (isCalibrating) {
      file.print("RZERO \t");
    }

    file.print("Co2 (ppm) Max reading \t");
    file.print("Time \t");
    file.println("Date \t");
    file.close();
  }
}
bool sendSms(char *message) {
  if (!hasStartedSendingSms) {
    strcpy(currentCommand, "txtMode");
    strcpy(prevCommand, "txtMode");

    gsmSerial.println("AT+CMGF=1");
    Serial.println(F("Sending SMS..."));
    Serial.println("AT+CMGF=1");
    startedAt = timeElapsed;
    hasStartedSendingSms = true;
  }

  if (timeElapsed - startedAt >= smsTimeout && hasStartedSendingSms) {
    startedAt = timeElapsed;
    if (!strcmp(prevCommand, "txtMode")) {
      strcpy(currentCommand, "contact");
      //      gsmSerial.println("AT+CMGS=\"+639064209700\"\r");
      gsmSerial.println("AT+CMGS=\"+639514642872\"\r");
    }
    if (!strcmp(currentCommand, "contact") && strcmp(prevCommand, "txtMode")) {
      strcpy(currentCommand, "message");
      gsmSerial.println(message);
    }
    if (!strcmp(currentCommand, "message") && strcmp(prevCommand, "contact")) {
      strcpy(currentCommand, "end");
      gsmSerial.println((char)26);
    }
    if (!strcmp(currentCommand, "end") && strcmp(prevCommand, "message")) {
      currentCommand[0] = NULL;
      hasStartedSendingSms = false;
      return true;
    }

    strcpy(prevCommand, currentCommand);
  }

  return false;
}
void buzz(bool state) {
  digitalWrite(buzzerPin, state);
}
void turnOffBuzzAlert() {
  if (!strcmp(currentSensorOnCheck, "co2") && timeElapsed - buzzStartedAt >= buzzTimeout) {
    buzz(false);
  }
}
void setNextSensor() {
  if (!strcmp(currentSensorOnCheck, "co2")) {
    strcpy(currentSensorOnCheck, "temperature");
    return;
  }

  if (!strcmp(currentSensorOnCheck, "temperature")) {
    strcpy(currentSensorOnCheck, "humidity");
    return;
  }

  if (!strcmp(currentSensorOnCheck, "humidity")) {
    strcpy(currentSensorOnCheck, "co2");
    return;
  }

}

void readGsmResponse() {
  static byte ndx = 0;
  char endMarker = '\n';
  char rc;

  while (gsmSerial.available() > 0 && isResponseReady == false) {
    rc = gsmSerial.read();

    if (rc != endMarker) {
      gsmResponse[ndx] = rc;
      ndx++;
      if (ndx >= numChars) {
        ndx = numChars - 1;
      }
    }
    else {
      gsmResponse[ndx] = '\0';
      ndx = 0;
      isResponseReady = true;
    }
  }
}
//~~~~~~~~~~~~~~~~~~~~~//
