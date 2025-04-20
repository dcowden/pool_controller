#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h> 
#include "RunningAverage.h"
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <DallasTemperature.h>
#include <OneWire.h>

void updateTemperature();
void updateSolarHeater();

// ====== WiFi Configuration ======
const char* ssid = "bluedirt_IOT_2G";
const char* password = "allthethings!";

// ====== SPI Pin Definitions for MAX6675 on VSPI ======
//const int thermoSCK = 18;
//const int thermoCS  = 5;
//const int thermoSO  = 19;
const int ONE_WIRE_PIN = 23;
const int SOLAR_RELAY_PIN = 26;
const int SUN_PANEL_PIN = 36;
const int SOLAR_PUMP_OVERRIDE_PIN=22;
const float MAX_TEMP_F=89.0; //89

//NC switch coupled to ground with input_pullup: 0 = overide enabled, 1 = no override
const int SOLAR_PUMP_REQUEST_PUMP_OFF=0;
const int HEATER_PUMP_OFF = 0;
const int HEATER_PUMP_ON = 1;

const float  SUN_ON_LEVEL = 2850.0; //2950
const int TEMP_UPDATE_INTERVAL_MILLIS = 2000;
const int WIFI_CONNECT_INTERVAL_MILLIS = 30000;
const int NUM_SUN_SAMPLES=50;
const int MAIN_TEMP_INDEX=0;
const bool DEBUG = true;

//local vars
int heater_on = 0;
float tempC = 0.0;
float tempF = 0.0;
float sunLevel = 0;
unsigned long lastTempUpdate = 0;
unsigned long lastSolarUpdate = 0;
unsigned long lastWifiCheck = 0;

AsyncWebServer  server(80);
RunningAverage sunLevelAvg(NUM_SUN_SAMPLES);
OneWire oneWire(ONE_WIRE_PIN);
DallasTemperature temp_sensors(&oneWire);
portMUX_TYPE myMutex = portMUX_INITIALIZER_UNLOCKED;

// ====== Read Temperature Helper ======
float readTemperatureC() {
  temp_sensors.requestTemperatures();
  float tC = temp_sensors.getTempCByIndex(MAIN_TEMP_INDEX);
  if (isnan(tC) || tC < -100.0) {
    Serial.print("Error: Invalid temperature reading: ");Serial.println(tC);
    return NAN;
  }
  return tC;
}


// ====== Convert Celsius to Fahrenheit ======
float celsiusToFahrenheit(float tempC) {
  return (tempC * 9.0 / 5.0) + 32.0;
}


// ====== Setup and Loop ======
void setup() {
  Serial.begin(115200);
  delay(1000); // let serial settle
  pinMode(SOLAR_RELAY_PIN,OUTPUT);
  pinMode(SOLAR_PUMP_OVERRIDE_PIN,INPUT_PULLUP);

  // Start WiFi
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  if ( DEBUG ){
    Serial.println("\nConnected to WiFi");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
  }


  temp_sensors.begin();

  // ====== HTTP Handlers ======
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    char html[512];

    if (isnan(tempC)) {
      snprintf(html, sizeof(html),
        "<!DOCTYPE html><html><head><title>ESP32 Pool Controller</title></head><body>"
        "<h1>ESP32 Pool Controller</h1>"
        "<p>Error reading temperature.</p></body></html>"
      );
    } else {
      snprintf(html, sizeof(html),
        "<!DOCTYPE html><html><head><title>ESP32 Pool Controller</title></head><body>"
        "<h1>ESP32 Pool Controller</h1>"
        "<p>Current Temperature: %.2f&deg;C (%.2f&deg;F)</p></body></html>",
        tempC, tempF
      );
    }

    request->send(200, "text/html", html);
  });

  server.on("/gettemperature", HTTP_GET, [](AsyncWebServerRequest *request){
    char json[512];
    float localC, localF, localSun;
    int localHeater;

    portENTER_CRITICAL(&myMutex);
    localC = tempC;
    localF = tempF;
    localSun = sunLevel;
    localHeater = heater_on;
    portEXIT_CRITICAL(&myMutex);


    snprintf(json, sizeof(json),
      "{\"temperature_c\": %.2f, \"temperature_f\": %.2f, \"sun_level\": %.2f, \"heater\": %s, \"heap_free\" : %d }",
      localC, localF, localSun, localHeater ? "true" : "false", ESP.getFreeHeap()
    );
    request->send(200, "application/json", json);
  });  

  server.begin();

  Serial.println("HTTP server started");
  sunLevelAvg.clear();

  
}


void ensureWiFiConnected() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi disconnected, trying to reconnect...");
    WiFi.disconnect();
    WiFi.begin(ssid, password);
    unsigned long startAttempt = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < 10000) {
      delay(500);
      Serial.print(".");
    }
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("Reconnected.");
    } else {
      Serial.println("Reconnect failed.");
    }
  }else{
    if ( DEBUG ){
      Serial.println("Wifi Already Connected");
    }
    
  }
}

void updateTemperature(){


  float newC = readTemperatureC();
  float newF = celsiusToFahrenheit(newC);

  portENTER_CRITICAL(&myMutex);
  tempC = newC;
  tempF = newF;
  portEXIT_CRITICAL(&myMutex);


  if (DEBUG){
    if (!isnan(tempC)) {
      Serial.print("Current Temperature: ");
      Serial.print(tempC);
      Serial.print("°C / ");
      Serial.print(tempF);
      Serial.println("°F");
    }  
  }

}

void updateSolarHeater(){
  int rawSunLevel = analogRead(SUN_PANEL_PIN);
  int solarPumpOverride = digitalRead(SOLAR_PUMP_OVERRIDE_PIN); 

  if ( DEBUG ){
    Serial.print("Raw SunLevel: ");Serial.println(rawSunLevel);
    Serial.print("Pump Override: "); Serial.println(solarPumpOverride);
  }
  
  float sL = (float)(rawSunLevel);
  sunLevelAvg.addValue(sL);
  sunLevel = sunLevelAvg.getAverage();

  if ( sunLevel > SUN_ON_LEVEL) {
    if ( tempF <= MAX_TEMP_F){
      heater_on = HEATER_PUMP_ON;
    }
    else{
      heater_on = HEATER_PUMP_OFF;
      if ( DEBUG){
        Serial.println("Heater is disabled: > max temp.");
      }
    }
  }
  else{
    heater_on = HEATER_PUMP_OFF;
    if ( DEBUG){
      Serial.println("Heater is disabled: Sun not bright enough.");
    }       
  }

  if ( SOLAR_PUMP_REQUEST_PUMP_OFF != solarPumpOverride ){
    digitalWrite(SOLAR_RELAY_PIN,heater_on);
  }
  else{
    digitalWrite(SOLAR_RELAY_PIN,HEATER_PUMP_OFF);
    if ( DEBUG){
      Serial.println("Heater is disabled: manual override.");
    }    
  }



  if ( DEBUG ){
    Serial.print("Pump Override to Off: ");
    if ( SOLAR_PUMP_REQUEST_PUMP_OFF == solarPumpOverride){
      Serial.println("True-- disable pump");
    }
    else{
      Serial.println("False:  pump enabled");
    }

    Serial.print("SunLevel: ");Serial.print(sunLevel);
    Serial.print(" Heater: ");Serial.print(heater_on);
    Serial.print("  Free heap: ");  Serial.print(ESP.getFreeHeap());  
    Serial.printf(" Min free heap: %u\n", esp_get_minimum_free_heap_size());
  }

}

void loop() {

  unsigned long now = millis();
  if (now - lastTempUpdate >= TEMP_UPDATE_INTERVAL_MILLIS) {
    lastTempUpdate = now;
    updateTemperature();
  }
  if (now - lastSolarUpdate >= TEMP_UPDATE_INTERVAL_MILLIS) {
    lastSolarUpdate = now;
    updateSolarHeater();
  }
  if (now - lastWifiCheck >= WIFI_CONNECT_INTERVAL_MILLIS) {
    lastWifiCheck = now;
    ensureWiFiConnected();
  }

  if (ESP.getFreeHeap() < 10000) {
    Serial.println("Heap too low, restarting...");
    ESP.restart();
  }

  
  delay(1);


}
