#include "Wire.h"
#include <ESP8266WiFi.h>  // the name tells already - if that's not clear enough, wifi lib for esp8266 module
#include <PubSubClient.h> // mqtt client lib
#include <ArduinoJson.h>  // send json string to mqtt server
#include <Adafruit_Sensor.h>
#include "SparkFun_SCD30_Arduino_Library.h"
#include "Adafruit_TSL2591.h"
#include <Adafruit_ADS1015.h>

// Declare PAR sensor variables
int val = 0;  // value directly from analog pin
long parData; // converted to par value

// declare sensors data
int SCD30co2;
float temperature = 0.0;
float humidity = 0.0;
int16_t adc0;

// here comes wifi and mqtt to be setup
const char *ssid = "HAMKvisitor";
const char *password = "hamkvisitor";
WiFiClient espClient;
const char *mqtt_server = "iot.research.hamk.fi";
PubSubClient client(espClient);

// sensor object
SCD30 airSensor;
Adafruit_ADS1115 ads;

// variable to store incoming msg
String strReceivedMsg;
String strReceivedTopic;

// set up timer so that data is published after an interval
unsigned long startMillis; //some global variables available anywhere in the program
unsigned long currentMillis;
const unsigned long period = 3000; //the value is a number of milliseconds

// digital output
int digitalco2 = D5;
int digitalnutrition = D6;
int digitalwaterspray = D7;
int digitalPeristalticPump = D3;
int digitalLightRelay = D4;

// functions declarations
void setup_wifi();
void reconnect();
void getPARdata();
void getSCD30data();
void getT9602data();
void showT9602data();
void publishSensorData();
void mqttCallback(char *topic, byte *payload, unsigned int length);

void setup()
{
    pinMode(digitalco2, OUTPUT);
    pinMode(digitalnutrition, OUTPUT);
    pinMode(digitalwaterspray, OUTPUT);
    pinMode(digitalPeristalticPump, OUTPUT);
    pinMode(digitalLightRelay, OUTPUT);
    digitalWrite(digitalco2, HIGH);
    digitalWrite(digitalnutrition, HIGH);
    digitalWrite(digitalwaterspray, HIGH);
    Serial.begin(115200); //  setup serial
    Wire.begin();

    // start sensor reading
    airSensor.begin();

    // start PAR reading
    ads.setGain(GAIN_TWOTHIRDS);
    ads.begin(); 

    // setup wifi and mqtt client
    setup_wifi();
    client.setServer(mqtt_server, 1883);
    client.setCallback(mqttCallback);
}

void loop()
{
    // try to connect to mqtt server
    if (!client.connected())
        reconnect();
    client.loop();

    currentMillis = millis();                  //get the current "time" (actually the number of milliseconds since the program started)
    if (currentMillis - startMillis >= period) //test whether the period has elapsed
    {
        publishSensorData();
    }

    delay(10);
}
void getPARdata()
{
    // getting PAR value
    int16_t val = ads.readADC_Differential_0_1();     // read the input pin
    parData = val*0.1875*0.8;
    Serial.println(val);             // debug value
    Serial.println(parData);
}

void getSCD30data()
{
    if (airSensor.dataAvailable())
    {
        SCD30co2 = airSensor.getCO2();
    }
    else
        Serial.println("No data");
}

void getT9602data(byte *a, byte *b, byte *c, byte *d)
{
    Wire.beginTransmission(40);
    Wire.write(0);
    Wire.endTransmission();
    Wire.requestFrom(40, 4);
    *a = Wire.read();
    *b = Wire.read();
    *c = Wire.read();
    *d = Wire.read();
}

void showT9602data()
{
    byte aa, bb, cc, dd;
    getT9602data(&aa, &bb, &cc, &dd);
    humidity = (float)(((aa & 0x3F) << 8) + bb) / 16384.0 * 100.0;
    temperature = (float)((unsigned)(cc * 64) + (unsigned)(dd >> 2)) / 16384.0 * 165.0 - 40.0;
    Serial.print(temperature);
    Serial.print(" degC  ");
    Serial.print(humidity);
    Serial.println(" %rH");
    ;
}

void publishSensorData()
{
    //getting all sensor data
    getPARdata();
    getSCD30data();
    showT9602data();

    // publishing json obj to mqtt server
    static char result_str[500] = "";
    DynamicJsonBuffer jsonBuffer;
    JsonObject &json = jsonBuffer.createObject();
    json["par"] = parData;
    json["temp"] = temperature;
    json["co2"] = SCD30co2;
    json["humid"] = humidity;
    json.printTo(result_str);
    Serial.println(result_str);
    client.publish("BioReactor/sensor", result_str);
    startMillis = currentMillis; //IMPORTANT to save the start time of the current LED state.
}

void setup_wifi()
{
    delay(10);
    // We start by connecting to a WiFi network
    Serial.println("Connecting to ");
    Serial.println(ssid);
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
        Serial.print(".");
    }
    Serial.println("");
    Serial.println("WiFi connected");
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());
}

void reconnect()
{
    // Loop until we're reconnected
    while (!client.connected())
    {
        Serial.print("Attempting MQTT connection...");
        // Attempt to connect
        if (client.connect("ESP8266Client"))
        {
            Serial.println("connected");
            startMillis = millis(); //initial start time
            client.subscribe("BioReactor/actuator");
            client.subscribe("BioReactor/actuator/Arduino/co2");
            client.subscribe("BioReactor/actuator/Arduino/nutrition");
            client.subscribe("BioReactor/actuator/Arduino/waterspray");
            client.subscribe("BioReactor/actuator/Arduino/peristalticPump");
            client.subscribe("BioReactor/actuator/Arduino/light");
            static char result_str[500] = "";
            DynamicJsonBuffer jsonBuffer;
            JsonObject &json = jsonBuffer.createObject();
            json["ready"] = "1";
            json.printTo(result_str);
            Serial.println(result_str);
            client.publish("BioReactor/check", result_str);
        }
        else
        {
            Serial.print("failed, rc=");
            Serial.print(client.state());
            Serial.println(" try again in 5 seconds");
            static char result_str[500] = "";
            DynamicJsonBuffer jsonBuffer;
            JsonObject &json = jsonBuffer.createObject();
            json["ready"] = "0";
            json.printTo(result_str);
            Serial.println(result_str);
            client.publish("BioReactor/check", result_str);
            // Wait 5 seconds before retrying
            delay(5000);
        }
    }
}

void mqttCallback(char *topic, byte *payload, unsigned int length)
{
    Serial.print("Message arrived [");
    Serial.print(topic);
    Serial.print("] ");
    for (uint16_t i = 0; i < length; i++)
    {
        //Serial.print((char)payload[i]);
        strReceivedMsg.concat((char)payload[i]);
    }
    strReceivedTopic = topic;
    if (strReceivedTopic == "BioReactor/actuator/Arduino/co2")
    {
        if (strReceivedMsg == "1")
        {
            digitalWrite(digitalco2, LOW);
        }
        else
        {
            digitalWrite(digitalco2, HIGH);
        }
    }
    if (strReceivedTopic == "BioReactor/actuator/Arduino/nutrition")
    {
        if (strReceivedMsg == "1")
        {
            digitalWrite(digitalnutrition, LOW);
        }
        else
        {
            digitalWrite(digitalnutrition, HIGH);
        }
    }
    if (strReceivedTopic == "BioReactor/actuator/Arduino/waterspray")
    {
        if (strReceivedMsg == "1")
        {
            digitalWrite(digitalwaterspray, LOW);
        }
        else
        {
            digitalWrite(digitalwaterspray, HIGH);
        }
    }
    if (strReceivedTopic == "BioReactor/actuator/Arduino/peristalticPump")
    {
        if (strReceivedMsg == "1")
        {
            digitalWrite(digitalPeristalticPump, LOW);
        }
        else
        {
            digitalWrite(digitalPeristalticPump, HIGH);
        }
    }
    if (strReceivedTopic == "BioReactor/actuator/Arduino/light")
    {
        if (strReceivedMsg == "1")
        {
            digitalWrite(digitalLightRelay, LOW);
        }
        else
        {
            digitalWrite(digitalLightRelay, HIGH);
        }
    }
    strReceivedMsg = "";
    Serial.println();
}
