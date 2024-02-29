#include <M5StickCPlus.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>

// For the LoraWan device activation we will use the OTAA way
// https://www.thethingsnetwork.org/docs/lorawan/end-device-activation/#over-the-air-activation-in-lorawan-11

// WARNING !!!
// The M5_LoraWan.h has an error: place a '#endif' at the ond of that file!

String baseAPIUrl = "https://noi-door-signage.codethecat.dev/";
DynamicJsonDocument doc(1024);
char prettyJsonSensorData[512];
String wifiSSID = "openAiR";

class SensorData
{
public:
  int CO2;
  float temperature;
  float humidity;
};
SensorData *sensorData = new SensorData();

String response;
bool dataUpdated = false;
bool status03detected = false;

char buf[1024] = {0};

void QuerySensorData()
{
  dataUpdated = false;
  Serial.println("-- QuerySensorData --");
  HTTPClient http;
  http.begin(baseAPIUrl + "api/room/airquality");
  http.addHeader("label-id", WiFi.macAddress()); // Used as easy auth

  int httpCode = http.GET();
  if (httpCode == 200)
  {
    String payload = http.getString();
    deserializeJson(doc, payload);

    sensorData->CO2 = doc["cO2"].as<int>();
    sensorData->temperature = doc["temperature"].as<float>();
    sensorData->humidity = doc["humidity"].as<float>();

    serializeJsonPretty(doc, prettyJsonSensorData);
    Serial.println(prettyJsonSensorData);

    dataUpdated = true;
  }
  else
  {
    Serial.println("Error on HTTP request: " + String(httpCode));
  }
  http.end();
  Serial.println("--");
}

void SetupWiFiTask()
{
  // Create a task that will be executed in the QueryRoomStatus function, with priority 1 and executed on core 0
  xTaskCreatePinnedToCore(
      QuerySensordataTask,   /* Function to implement the task */
      "QuerySensordataTask", /* Name of the task */
      10000,                 /* Stack size in words */
      NULL,                  /* Task input parameter */
      1,                     /* Priority of the task */
      NULL,                  /* Task handle. */
      1);                    /* Core where the task should run */
}

// Function to read status from URL
void QuerySensordataTask(void *parameter)
{
  for (;;) // infinite loop
  {
    QuerySensorData();
    DisplaySensorData();
    vTaskDelay(2000 / portTICK_PERIOD_MS); // delay for 30 sec
  }
}

void clearBuffer(char *buffer, unsigned int size)
{
  for (unsigned int i = 0; i < size; i++)
  {
    buffer[i] = 0;
  }
}

void ATCommand(String cmd, String val, uint32_t timeout = 1000)
{
  // while (Serial2.available()) {
  //   String str = Serial2.readString();
  // }
  clearBuffer(buf, 1024);
  int sent;
  if (val.isEmpty())
  {
    sent = sprintf(buf, "AT+%s\r\n", cmd.c_str());
  }
  else
  {
    sent = sprintf(buf, "AT+%s=%s\r\n", cmd.c_str(), val.c_str());
  }
  Serial.print("TX (strlen: " + String(strlen(buf)) + ",sent:" + String(sent) + "):");
  Serial.print(buf);
  Serial2.write(buf);
  Serial2.flush();

  delay(200);
}

bool CheckDeviceConnect()
{
  String restr;
  ATCommand("CGMI?", "");
  restr = ReceiveAT(500);
  if (restr.indexOf("OK") == -1)
  {
    return false;
  }
  else
  {
    return true;
  }
}

String ReceiveAT(uint32_t timeout)
{
  uint32_t nowtime = millis();
  String result;
  while (millis() - nowtime < timeout)
  {
    if (Serial2.available() != 0)
    {
      String str = Serial2.readString();
      result.concat(str);
    }
  }
  if (!result.isEmpty())
    Serial.println(result);

  return result;
}

String string2hex(String str)
{
  String hexString = "";
  for (int i = 0; i < str.length(); i++)
  {
    char c = str[i];
    if (c < 16)
    {
      hexString += "0";
    }
    hexString += String(c, HEX);
  }
  return hexString;
}

void setup()
{
  M5.begin();
  M5.Lcd.setRotation(3);
  M5.Lcd.fillScreen(TFT_BLACK);
  M5.Lcd.setCursor(5, 5, 2);
  Serial.println("");

  Serial2.begin(115200, SERIAL_8N1, 33, 32);
  Serial2.flush();
  delay(100);

  ATCommand("AT?", "");
  ReceiveAT(500);

  // ATCommand("CRESTORE", "");
  ATCommand("ILOGLVL", "5");
  ReceiveAT(500);

  ATCommand("CJOINMODE", "1"); // OTTA
  // ATCommand("CDEVEUI", "53b1981b4dda814e");
  ATCommand("CDEVADDR", "xxx"); // Device Address
  ATCommand("CAPPSKEY", "xxx"); // App Session Key
  ATCommand("CNWKSKEY", "xxx"); // Network Session Key
  ATCommand("CULDLMODE", "1");
  ReceiveAT(500);

  while (!CheckDeviceConnect())
    ;
  Serial.println("Lora connected");

  ATCommand("CCLASS", "2");
  ATCommand("CWORKMODE", "2");
  ReceiveAT(500);

  M5.Lcd.println("Connect WiFi");
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
  M5.Lcd.println("..connected!");

  Serial.println("");
  SetupWiFiTask();

  SendSensorData();
}

String waitRevice()
{
  String recvStr;
  do
  {
    recvStr = Serial2.readStringUntil('\n');
  } while (recvStr.length() == 0);
  return recvStr;
}

void SendSensorData()
{
  String data = "d#" + String(sensorData->CO2) + "#" + String(sensorData->temperature) + "#" + String(sensorData->humidity);

  Serial2.print("AT+DTRX=0,2," + String(data.length()) + "," + string2hex(data) + "\r\n");
  delay(100);
  Serial2.flush();

  Serial.println("Available for write:" + String(Serial2.availableForWrite()));
  delay(100);
  ReceiveAT(1000);
}

void DisplaySensorData()
{
  M5.Lcd.fillScreen(TFT_BLACK);
  M5.Lcd.setCursor(5, 5, 2);
  M5.Lcd.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Lcd.setTextSize(2);
  M5.Lcd.println("CO2  " + String(sensorData->CO2) + " ppb");
  M5.Lcd.println("Temp " + String(sensorData->temperature) + " C");
  M5.Lcd.println("Hum  " + String(sensorData->humidity) + " %RH");
}

int countStatus03 = 1;
void loop()
{

  if (dataUpdated && (status03detected || countStatus03 > 10))
  {
    status03detected = false;
    SendSensorData();
    dataUpdated = false;
  }
  else
  {
    Serial2.print("AT+CSTATUS?\r");
    Serial2.flush();

    String status = ReceiveAT(1000);
    if (status.indexOf("03") != -1)
    {
      Serial.println("Status 03 detected");
      status03detected = true;
      countStatus03 = 0;
    }
    else
    {
      countStatus03++;
    }
  }
  delay(1000);
}
