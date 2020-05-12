/*
Written by Nicholas Robinson
Last modified 12th May 2020
Hardware used:  Adafruit ESP32 HUZZAH feather + Feather OLED
The relay being controlled is based on Adafruit MQTT Tutorial

***Function***
On start up, the weather information is shown
Pushing "A" shows the weather
Pushing "B" shows stock information
Pushing "C" once shows the current relay state
Pushing "C" again will toggle the relay state

***Useful Links***
https://learn.adafruit.com/mqtt-adafruit-io-and-you/overview
https://github.com/Freeno83/MQTT-Learn-from-home
https://github.com/adafruit/Adafruit_MQTT_Library/issues/20
https://io.adafruit.com/api/docs/mqtt.html#adafruit-io-mqtt-api
https://openweathermap.org/current
https://www.alphavantage.co/documentation/
*/

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "Adafruit_MQTT.h"
#include "Adafruit_MQTT_Client.h"

//Wifi and MQTT Access Credentials
#define WLAN_SSID       "Netowrk Name"
#define WLAN_PASS       "Network Password"
#define AIO_SERVER      "io.adafruit.com"
#define AIO_SERVERPORT  1883
#define AIO_USERNAME    "UserName"
#define AIO_KEY         "Adafruit aioKey"

//HTTP call interval (milliseconds)
long getRate = 300000;     
long prevWeatherCall = getRate * -1;
long prevStockCall = getRate * -1;

//HTTP weather endpoint 
String base_w = "http://api.openweathermap.org/data/2.5/weather";
String appid = "API Key";
String zip = "Zip Code";
String units = "imperial";

String weatherUrl = base_w + "?appid=" + appid + "&zip=" + zip + "&units=" + units;

//HTTP stock endpoint
String base_s = "https://www.alphavantage.co/query?function=";
String function = "GLOBAL_QUOTE";
String symbol = "TSLA";
String apikey = "API Key";

String stockUrl = base_s + function + "&symbol=" + symbol + "&apikey=" +apikey;

//MQTT Feeds
WiFiClient client;
Adafruit_MQTT_Client mqtt(&client, AIO_SERVER, AIO_SERVERPORT, AIO_USERNAME, AIO_USERNAME, AIO_KEY);
Adafruit_MQTT_Subscribe onoff = Adafruit_MQTT_Subscribe(&mqtt, AIO_USERNAME "/feeds/onoff");
Adafruit_MQTT_Publish onoffGet = Adafruit_MQTT_Publish(&mqtt, AIO_USERNAME "/feeds/onoff/get");
Adafruit_MQTT_Publish onoffButton = Adafruit_MQTT_Publish(&mqtt, AIO_USERNAME "/feeds/onoff");

//Create an instance of the adafruit display
Adafruit_SSD1306 display = Adafruit_SSD1306(128, 32, &Wire);

//Red LED pin is next to the USB port
#define RED_LED 13

//OLED Feather Buttons ordered A, B, C
int buttonPin[] = {15, 32, 14};
int buttonVal[] = {1, 1, 1};
int buttonPrevVal[] = {1, 1, 1};

//General global variables
String modes[3] = {"weather", "stock", "relay"};
String displayMode = "weather";
String displayModePrev;
String onoffRead;
String relayValue[4] = {};
String tempUnit = "";
String windUnit = "";

//Global variables to hold HTTP responses
String payload;
String parsedWeather[4] = {};
String parsedStock[4] = {};

//Set memory space for the JSON, check https://arduinojson.org/v6/assistant/ 
StaticJsonDocument<1000> jsonWeather;
StaticJsonDocument<1000> jsonStock;

void setup() {
  Serial.begin(115200);

  //OLED Feather buttons --> LOW means button is pressed
  for(int i = 0; i < 3; i++)
    pinMode(buttonPin[i], INPUT_PULLUP);
    
  //The red LED next to the USB port will flash during HTTP call
  pinMode(RED_LED, OUTPUT);
  digitalWrite(RED_LED, LOW);
  
  chooseUnits();
  oledStart();
  wifiStart();
  mqtt.subscribe(&onoff);
}

void loop() {
  //Read button pins
  for(int i = 0; i < 3; i++)
    buttonVal[i] = digitalRead(buttonPin[i]);
    
  //Using 1-shot logic, set display mode if a button is pressed
  for(int i = 0; i < 3; i++){
    if(buttonVal[i] == LOW){
      if(buttonVal[i] != buttonPrevVal[i])
        displayMode = modes[i];
    }
  }
  
  //Update weather if longer than get rate, else display previous data
  if(displayMode == "weather"){
    if((millis() - prevWeatherCall) > getRate){
      updateWeather();
      prevWeatherCall = millis();
    }
    else oledDisplay(parsedWeather);
  }
  
  //Update stock if longer than get rate, else display previous data
  if(displayMode == "stock"){
    if((millis() - prevStockCall) > getRate){
      updateStock();
      prevStockCall = millis();
    }
    else oledDisplay(parsedStock);
  }

  //Display current relay value, toggle if "C" is pushed
  if(displayMode == "relay"){
    if(displayMode != displayModePrev){
      MQTT_connect();
      onoffGet.publish(0);
    }
    Adafruit_MQTT_Subscribe *subscription;
    while((subscription = mqtt.readSubscription(100))){
      if(subscription == &onoff){
        onoffRead = String((char *)onoff.lastread);
        relayValue[0] = "Relay: " + onoffRead;
        oledDisplay(relayValue);
      }
    }
    if(displayMode == displayModePrev){
      if(buttonVal[2] == LOW){
        if(buttonVal[2] != buttonPrevVal[2]){
          if(onoffRead == "ON")
            onoffButton.publish("OFF");          
          if(onoffRead == "OFF")
            onoffButton.publish("ON");
        }
      }
    }
    MQTT_connect();   //Used instead of ping to maintain connection
  }
  //Record previous values, needed for 1-shot logic
  for(int i = 0; i < 3; i++)
    buttonPrevVal[i] = buttonVal[i];
  displayModePrev = displayMode;
}

//Call weather API, parse data, send to OLED display
void updateWeather(){
  payload = httpGet(weatherUrl);

  if(payload != "HTTP Error"){
    DeserializationError error = deserializeJson(jsonWeather, payload);
  
    if(error) {
      Serial.print(F("deserializeJson() failed: "));
      Serial.println(error.c_str());
      return;
    }
    parsedWeather[0] = jsonWeather["name"].as<String>();
    parsedWeather[1] = jsonWeather["weather"][0]["description"].as<String>();
    parsedWeather[2] = "Temp: " + jsonWeather["main"]["temp"].as<String>() + " " + tempUnit;
    parsedWeather[3] = "Wind: " + jsonWeather["wind"]["speed"].as<String>() + " " + windUnit;
  }
  else parsedWeather[0] = payload;
  oledDisplay(parsedWeather);
}

//Call stock API, parse data, send to OLED display
void updateStock(){
  payload = httpGet(stockUrl);
  
  if(payload != "HTTP Error"){
    DeserializationError error = deserializeJson(jsonStock, payload);
    
    if(error) {
      Serial.print(F("deserializeJson() failed: "));
      Serial.println(error.c_str());
      return;
    }
    parsedStock[0] = jsonStock["Global Quote"]["01. symbol"].as<String>();
    parsedStock[1] = "Price: " + String(float(jsonStock["Global Quote"]["05. price"]));
    parsedStock[2] = "Prev close: " + String(float(jsonStock["Global Quote"]["08. previous close"]));
    parsedStock[3] = "Change: " + String(float(jsonStock["Global Quote"]["09. change"]));
  }
  else parsedStock[0] = payload;
  oledDisplay(parsedStock);
}

//Make HTTP Get Calll
String httpGet(String url){
  String payload;
  HTTPClient http;
  http.begin(url);
  
  digitalWrite(RED_LED, HIGH);
  int httpCode = http.GET();
  digitalWrite(RED_LED, LOW);
  
  if(httpCode == 200){
    payload = http.getString();
    Serial.println("HTTP Response code: " + httpCode);
  }
  else{
    Serial.println("HTTP Error Code: " + httpCode);
    payload = "HTTP Error";
  }
  http.end();
  return payload;
}

//Initialize Featherwind 128 x 32 OLED
void oledStart(){
  //Initialize the display and show the splash screen
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C); // Address 0x3C for 128x32
  display.display();
  delay(1000);
  display.clearDisplay();
  display.display();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
}

//Display 4 lines of recieved data on the OLED 
void oledDisplay(String parsedJson[4]){
  display.clearDisplay();
  
  for(int i = 0; i < 4; i++)
    display.println(parsedJson[i]);
  
  display.setCursor(0,0);
  display.display();
}

//Establish WiFi Connection
void wifiStart(){
  WiFi.begin(WLAN_SSID, WLAN_PASS);

  Serial.print("Connecting");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(100);
  }
  Serial.print("\nConnected to ");
  Serial.println(WLAN_SSID);
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

//Set displayed weather units based on "units" variable
void chooseUnits(){
  if(units == "metric"){
    tempUnit = "C";
    windUnit = "km/h";
  }
  if(units == "imperial"){
    tempUnit = "F";
    windUnit = "mph";
  }
}

//Connect to Adafruit IO MQTT Broker
void MQTT_connect() {
  int8_t ret;
  if (mqtt.connected())
    return;

  Serial.print("Connecting to MQTT... ");

  uint8_t retries = 3;
  while ((ret = mqtt.connect()) != 0) { // connect will return 0 for connected
       Serial.println(mqtt.connectErrorString(ret));
       Serial.println("Retrying MQTT connection in 5 seconds...");
       mqtt.disconnect();
       delay(100);  // wait 100 ms 
       retries--;
       if (retries == 0) {
         while (1);
       }
  }
  Serial.println("MQTT Connected!");
}
