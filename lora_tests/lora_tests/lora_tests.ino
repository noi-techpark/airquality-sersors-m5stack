#include <M5StickCPlus.h>

String inputString = "";        // a string to hold incoming data
boolean stringComplete = false; // whether the string is complete

void ATCommand(String cmd, String val, uint32_t timeout = 1000)
{
  while (Serial2.available())
  {
    String str = Serial2.readString();
  }
  char buf[256] = {0};
  if (val.isEmpty())
  {
    sprintf(buf, "AT+%s\r", cmd.c_str());
  }
  else
  {
    sprintf(buf, "AT+%s=%s\r", cmd.c_str(), val.c_str());
  }
  Serial2.write(buf);
  Serial2.flush();
  delay(100);
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

String printHex(String str)
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
  ATCommand("CDEVADDR", "00620a28");
  ATCommand("CAPPSKEY", "8cb2ec439432dbbf5846579c091c3d1f");
  ATCommand("CNWKSKEY", "7bf1cbc08a27639a4b9d7cc40fefac77");
  ATCommand("CULDLMODE", "1");
  ReceiveAT(500);

  while (!CheckDeviceConnect())
    ;
  Serial.println("Lora connected");

  ATCommand("CCLASS", "2");
  ATCommand("CWORKMODE", "2");
  ReceiveAT(500);

  // ATCommand("CSAVE", "");
  // ReceiveAT(500);
  // ATCommand("IREBOOT", "1");
  //   ReceiveAT(1000);

  // ATCommand("CNBTRIALS", "0,2");
  // ReceiveAT(500);

  ATCommand("CJOIN?", "");
  ReceiveAT(500);

  // ATCommand("CJOIN", "0,0,10,8");
  // ReceiveAT(500);

  // ATCommand("CJOIN", "1,1,10,8");
  // ReceiveAT(500);

  Serial.println("Write the DTRX content and press ENTER");
}

void loop()
{

  while (Serial.available())
  {
    char inChar = (char)Serial.read(); // read the incoming byte:
    inputString += inChar;

    // if the incoming character is a newline, set a flag so the main loop can
    // do something about it:
    if (inChar == '\n')
    {
      stringComplete = true;
    }
  }

  if (stringComplete)
  {
    inputString = inputString.substring(0, inputString.length() - 1);
    Serial.println("'" + inputString + "'"); // print the string when a newline arrives:

    Serial2.print("AT+DTRX=0,2," + String(inputString.length()) + "," + printHex(inputString) + "\r\n");
    delay(100);
    Serial2.flush();

    Serial.println("Available for write:" + String(Serial2.availableForWrite()));
    delay(100);
    ReceiveAT(1000);

    inputString = ""; // clear the string:
    stringComplete = false;
    Serial.println("Write the DTRX content and press ENTER");
  }
  else
  {
    Serial2.print("AT+CSTATUS?\r");
    Serial2.flush();
  }

  ReceiveAT(1000);
  delay(1000);
}
