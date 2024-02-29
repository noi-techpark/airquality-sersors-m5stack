#include "M5Atom.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <SensirionI2CScd4x.h>

CRGB color = 0x000000;

String baseAPIUrl = "https://noi-door-signage.codethecat.dev/";
DynamicJsonDocument doc(1024);
StaticJsonDocument<200> sesnorDataDoc;

char prettyJsonCalendar[512];
class CalendarEvent
{
public:
  String Title;
  String Organizer;
  String StartAt;
  String EndAt;
  bool bookedByLabel;
  String ToString()
  {
    return Title + Organizer + StartAt + EndAt;
  }
};

class SensorData
{
public:
  int CO2;
  float temperature;
  float humidity;
};

SensirionI2CScd4x scd4x;

int timeToNextEvent = 0;
bool isFree = true;

bool roomDataUpdated = false;
bool roomStatusChanged = true;
bool isFirstRoomDataUpdate = true;

String wifiSSID = "openAiR";

CalendarEvent *currentEvent = new CalendarEvent();
SensorData *sensorData = new SensorData();

void QueryRoomStatus()
{
  Serial.println("-- QueryRoomStatus");
  HTTPClient http;
  http.begin(baseAPIUrl + "api/room/status");
  http.addHeader("label-id", WiFi.macAddress());

  int httpCode = http.GET();
  if (httpCode == 200)
  {
    String payload = http.getString();
    deserializeJson(doc, payload);

    String currentStatus = isFree ? "yes" : "no" + currentEvent->ToString();

    isFree = doc["isFree"];
    timeToNextEvent = doc["timeToNextEvent"];

    JsonVariant ce = doc["currentEvent"];
    if (ce.isNull())
    {
      isFree = true;
    }
    else
    {
      isFree = false;
      currentEvent->Title = ce["title"].as<String>();
      currentEvent->Organizer = ce["organizer"].as<String>();
      currentEvent->StartAt = ce["startAt"].as<String>();
      currentEvent->EndAt = ce["endAt"].as<String>();
      currentEvent->bookedByLabel = ce["bookedByLabel"].as<bool>();
    }

    String newStatus = isFree ? "yes" : "no" + currentEvent->ToString();
    roomStatusChanged = isFirstRoomDataUpdate || currentStatus != newStatus;

    roomDataUpdated = true;
    isFirstRoomDataUpdate = false;

    serializeJsonPretty(doc, prettyJsonCalendar);
    Serial.println(prettyJsonCalendar);
  }
  else
  {
    Serial.println("Error on HTTP request: " + String(httpCode));
  }
  http.end();
}

void SendSensorData()
{
  Serial.println("-- SendSensorData");
  HTTPClient http;
  http.begin(baseAPIUrl + "api/room/airquality");
  http.addHeader("label-id", WiFi.macAddress());
  http.addHeader("Content-Type", "application/json");

  sesnorDataDoc["co2"] = sensorData->CO2;
  sesnorDataDoc["temperature"] = sensorData->temperature;
  sesnorDataDoc["humidity"] = sensorData->humidity;

  String output;
  serializeJson(sesnorDataDoc, output);

  int httpResponseCode = http.POST(output);

  // Print the response code
  Serial.print("HTTP Response code: ");
  Serial.println(httpResponseCode);

  http.end();
}

void ReadSensorData()
{
  Serial.println("-- ReadSensorData");
  uint16_t error;
  char errorMessage[256];

  // Read Measurement
  uint16_t co2 = 0;
  float temperature = 0.0f;
  float humidity = 0.0f;
  bool isDataReady = false;

  error = scd4x.getDataReadyFlag(isDataReady);
  if (error)
  {
    Serial.print("Error trying to execute getDataReadyFlag(): ");
    errorToString(error, errorMessage, 256);
    Serial.println(errorMessage);
    return;
  }

  if (!isDataReady)
  {
    return;
  }

  error = scd4x.readMeasurement(co2, temperature, humidity);
  if (error)
  {
    Serial.print("Error trying to execute readMeasurement(): ");
    errorToString(error, errorMessage, 256);
    Serial.println(errorMessage);
  }
  else if (co2 == 0)
  {
    Serial.println("Invalid sample detected, skipping.");
  }
  else
  {
    Serial.printf("Co2:%d\n", co2);
    Serial.printf("Temperature:%f\n", temperature);
    Serial.printf("Humidity:%f\n\n", humidity);

    sensorData->CO2 = co2;
    sensorData->temperature = temperature;
    sensorData->humidity = humidity;

    SendSensorData();
  }
}

// Function to read status from URL
void QueryRoomStatusTask(void *parameter)
{
  for (;;) // infinite loop
  {
    ReadSensorData();
    QueryRoomStatus();
    vTaskDelay(2000 / portTICK_PERIOD_MS); // delay for 30 sec
  }
}

void SetupWiFiTask()
{
  // Create a task that will be executed in the QueryRoomStatus function, with priority 1 and executed on core 0
  xTaskCreatePinnedToCore(
      QueryRoomStatusTask,   /* Function to implement the task */
      "QueryRoomStatusTask", /* Name of the task */
      10000,                 /* Stack size in words */
      NULL,                  /* Task input parameter */
      1,                     /* Priority of the task */
      NULL,                  /* Task handle. */
      1);                    /* Core where the task should run */
}

void setup()
{
  M5.begin(true, false,
           true); // Init Atom-Matrix(Initialize serial port, LED matrix).

  WiFi.begin(wifiSSID, "");
  Serial.print("Connect to the WiFi '" + wifiSSID + "'");
  int wifiCount = 0;

  while (WiFi.status() != WL_CONNECTED && wifiCount < 15)
  {
    delay(500);
    Serial.print(".");
    wifiCount++;
  }

  if (WiFi.status() != WL_CONNECTED)
  {
    // Reboot the device
    Serial.println("cannot connect..rebooting!");
    ESP.restart();
  }

  Serial.println("connected!");
  Serial.println("MAC: " + WiFi.macAddress());

  if (!Wire.begin(26, 32))
    Serial.println("It was not possible to initialize the Wire");

  uint16_t error;
  char errorMessage[256];
  scd4x.begin(Wire);

  // stop potentially previously started measurement
  error = scd4x.stopPeriodicMeasurement();
  if (error)
  {
    Serial.print("Error trying to execute stopPeriodicMeasurement(): ");
    errorToString(error, errorMessage, 256);
    Serial.println(errorMessage);
  }

  // Start Measurement
  error = scd4x.startPeriodicMeasurement();
  if (error)
  {
    Serial.print("Error trying to execute startPeriodicMeasurement(): ");
    errorToString(error, errorMessage, 256);
    Serial.println(errorMessage);
  }

  Serial.println("Setup done");

  SetupWiFiTask();
}

void loop()
{
  M5.update();

  M5.dis.clear();

  if (sensorData->CO2 > 1200)
  {
    M5.dis.fillpix(0xfff000); // Yellow
    Serial.println("WARNING: bad air quality (" + String(sensorData->CO2) + "ppb) !!!");
    color = 0xfff000;
    delay(400);
  }

  // put your main code here, to run repeatedly:
  if (isFree)
  {
    M5.dis.fillpix(0x00ff00); // Green
    color = 0x00ff00;
  }
  else
  {
    M5.dis.fillpix(0xff0000); // Red
    color = 0xff0000;
  }

  delay(400);
}
