/* 
Send Temperature (Max6675) and Free Memory values from Arduino to Zabbix.
Uses Adafruit Max6675 library (https://github.com/adafruit/MAX6675-library) and Memory Free Library (https://playground.arduino.cc/Code/AvailableMemory/).

Pablo Hawkes
2019-10-17
*/

#include <max6675.h>
#include <MemoryFree.h>
#include <Ethernet.h>

/*
MAX6675 config:
With ethernet shield, can't be used these pins:
https://www.arduino.cc/en/Main/ArduinoEthernetShieldV1
Arduino communicates with both the W5100 and SD card using the SPI bus (through the ICSP header).
This is on digital pins 10, 11, 12, and 13 on the Uno and pins 50, 51, and 52 on the Mega. 
On both boards, pin 10 is used to select the W5100 and pin 4 for the SD card (Only if used). These pins cannot be used for general I/O.
*/

#define CONFIG_GND_PIN      3 //this line can be deleted if use GNC original pin
#define CONFIG_VCC_PIN      4 //this line can be deleted if use VCC original pin
#define CONFIG_SCLK_PIN     5
#define CONFIG_CS_PIN       6
#define CONFIG_SO_PIN       7

MAX6675 thermocouple(CONFIG_SCLK_PIN, CONFIG_CS_PIN, CONFIG_SO_PIN);

//Local network config:
//byte mac[] = {0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED};
byte mac[] = {0x74, 0xFE, 0x48, 0x00, 0x00, 0x01};
IPAddress ip(172,18,2,230);                             //Arduino Fixed IP

//Zabbix Config:
const char* server="192.168.0.110";                     //ZABBIX Server IP
const String clientHostName = "Arduino";                //Hostname from this client on Zabbix
const int port = 10051;                                 //Zabbix_sender is 10051

//Zabbix items:
const String temperatureItemName = "Temperature";       //Temp item from probe
const String memorySizeItemName = "FreeMemory";         //Arduino Memory size in bytes

//Interval Config: 
const unsigned long interval = 60000;                   //Loop time
const unsigned long ResponseTimeout = 10000;            //Zabbix response timeout

//Global variables:
EthernetClient client;
unsigned long previousMillis = 0;
unsigned long currentMillis = 0;
unsigned long diff = 0;
unsigned long timeout = 0;
int firstLengthByte = 0;
int secondLengthByte = 0;
String zabbixMessagePayload = "";

//Functions:
void zabbix_sender(void);

void setup()
{
  //this line can be deleted if use VCC original pin
  pinMode(CONFIG_VCC_PIN, OUTPUT); digitalWrite(CONFIG_VCC_PIN, HIGH);
  //this line can be deleted if use GND original pin
  pinMode(CONFIG_GND_PIN, OUTPUT); digitalWrite(CONFIG_GND_PIN, LOW);

  Serial.begin(9600);
  Serial.println(F("Connecting to Ethernet. Wait..."));
  delay (2000);

  Ethernet.begin(mac, ip); //Fixed IP
  //Ethernet.begin(mac); //DHCP

  //delay (5000);
  Serial.print(F("IP Address: "));
  Serial.println(Ethernet.localIP());
  
  //Serial.print(F("Free Memory: "));
  //Serial.println(freeMemory());

  //Serial.print(F("Temperature: "));
  //String a = getSensorValue();
}

void loop()
{
  currentMillis = millis();
 
  if ( (currentMillis - previousMillis) > interval) 
  {
    Ethernet.maintain();  //execute periodically to maintain DHCP lease

    //Serial.println("Current: " + String(currentMillis) + " - Previous: " + String(previousMillis));
    previousMillis = currentMillis;

    //Send to zabbix:
    zabbix_sender(temperatureItemName, getSensorValue());  
  }
}

String getSensorValue()
{
  double temp = thermocouple.readCelsius();
  //Serial.print(F("Temperature value (°C): "));
  //Serial.println(temp);
  return String(temp);
}

//void zabbix_sender(String items[], float values[])
void zabbix_sender(String item, String value)
{
  //1. creating message
  String zabbixMessagePayload = "{ \"request\":\"sender data\", \"data\":[ ";
  zabbixMessagePayload = zabbixMessagePayload + "{\"host\":\"" + String(clientHostName) + "\",\"key\":\"" + item + "\",\"value\":\"" + String(value) + "\"}, ";
  zabbixMessagePayload = zabbixMessagePayload + "{\"host\":\"" + String(clientHostName) + "\",\"key\":\"" + memorySizeItemName + "\",\"value\":\"" + String(freeMemory()) + "\"} ";
  zabbixMessagePayload = zabbixMessagePayload + "] }";

  //Serial.print(F("Free Memory: "));
  //Serial.println(freeMemory());

  //2.Sending message to zabbix:

    Serial.print(F("Message sent to zabbix: "));
    Serial.println(zabbixMessagePayload);
    //Serial.print(F("Message Lenght: "));
    //Serial.println(String(zabbixMessagePayload.length()));

  Serial.println("trying connect to zabbix...");
  if (client.connect(server, port) == 1)
  {
    Serial.println("Connected to zabbix");
    
    //Clear garbage:
    firstLengthByte = 0;
    secondLengthByte = 0;
    
    //Send Header:
    char txtHeader[5] = {'Z', 'B', 'X', 'D', 1};     //ZBXD<SOH>
    client.print(txtHeader);

    //Serial.println("Payload lenght: " + String(zabbixMessagePayload.length())); //Payload lenght
    if (zabbixMessagePayload.length() >= 256)
    {
      if(zabbixMessagePayload.length() >= 65536) // too much for arduino memory
      {
        Serial.println(F("ERROR: Message too long. Not sent.")); 
        return;
      }
      else
      {
        secondLengthByte = zabbixMessagePayload.length() / 256;
        firstLengthByte = zabbixMessagePayload.length() % 256 ;
      }
    }
    else
    {
      secondLengthByte = 0;
      firstLengthByte = zabbixMessagePayload.length();
    }

    //Send Length:
    client.write((byte)firstLengthByte);
    client.write((byte)secondLengthByte);
    client.write((byte)0);
    client.write((byte)0);
    client.write((byte)0);
    client.write((byte)0);
    client.write((byte)0);
    client.write((byte)0);
    
    //Send Payload (json):
    client.print(zabbixMessagePayload);   //DO NOT USE PRINTLN, because adds <cr><lf> at the end and changes lenght

    Serial.print(F("Message sent to zabbix: "));
    Serial.println(zabbixMessagePayload);
    //Serial.print(F("Message Lenght: "));
    //Serial.println(String(zabbixMessagePayload.length()));

    Serial.println(F("Message sent. Waiting for response..."));

    //Message timeout try:
    timeout = millis();
    
    while (client.available() == 0)
    {
      if (millis() - timeout > ResponseTimeout)
        {
          Serial.println(F(">>>>> Response Timeout"));
          client.stop();
          return;
        }
    }
    
    //Get response:
    while (client.available())
    {
      //Serial.print(F("Waiting for response..."));
      String response = client.readStringUntil('\r');

      Serial.print(F("Zabbix response OK: "));
      Serial.println(response);
    }
  }
  else
  {
    Serial.println(F("NO Connection to zabbix"));
  }
  client.stop(); 
}
