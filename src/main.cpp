#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <max6675.h>
#include "Ticker.h"


void updateTemperature();
void updateSolarHeater();

// ====== WiFi Configuration ======
const char* ssid = "bluedirt_IOT_2G";
const char* password = "allthethings!";

// ====== SPI Pin Definitions for MAX6675 on VSPI ======
const int thermoSCK = 18;
const int thermoCS  = 5;
const int thermoSO  = 19;
const int SOLAR_RELAY_PIN = 26;
const int SUN_PANEL_PIN = GPIO_NUM_36;
const int SUN_ON_LEVEL = 3000;
const int TEMP_UPDATE_INTERVAL_MILLIS = 1000;


//local vars
int heater_on = 0;
float tempC = 0.0;
float tempF = 0.0;
int sunLevel = 0;

Ticker updateTempTimer(updateTemperature, TEMP_UPDATE_INTERVAL_MILLIS);
Ticker updateSolarHeaterTimer(updateSolarHeater, TEMP_UPDATE_INTERVAL_MILLIS);
MAX6675 thermocouple(thermoSCK, thermoCS, thermoSO);
WebServer server(80);

// ====== Read Temperature Helper ======
float readTemperatureC() {
  float tempC = thermocouple.readCelsius();
  if (isnan(tempC) || tempC < -100.0) {
    Serial.println("Error: Invalid temperature reading");
    return NAN;
  }
  return tempC;
}


// ====== Convert Celsius to Fahrenheit ======
float celsiusToFahrenheit(float tempC) {
  return (tempC * 9.0 / 5.0) + 32.0;
}

// ====== HTTP Handlers ======
void handleRoot() {

  String html = "<!DOCTYPE html><html><head><title>ESP32 Pool Controller</title></head><body>";
  html += "<h1ESP32 Pool Controller</h1>";
  
  if (isnan(tempC)) {
    html += "<p>Error reading temperature.</p>";
  } else {
    html += "<p>Current Temperature: " + String(tempC, 2) + "&deg;C (" + String(tempF, 2) + "&deg;F)</p>";
  }
  
  html += "</body></html>";
  server.send(200, "text/html", html);
}

void handleGetTemperature() {

  
  String json = "{\"temperature_c\": " + String(tempC, 2)  
    +  ", \"temperature_f\": "  + String(tempF, 2) 
    + ", \"sun_level\": " + String(sunLevel) 
    + ", \"heater\": " +  String(heater_on ? "true" : "false")    
    + "}";

  server.send(200, "application/json", json);

}

// ====== Setup and Loop ======
void setup() {
  Serial.begin(115200);
  delay(1000); // let serial settle
  pinMode(SOLAR_RELAY_PIN,OUTPUT);

  // Start WiFi
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }


  Serial.println("\nConnected to WiFi");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());

  // Start Web Server
  server.on("/", handleRoot);
  server.on("/gettemperature", handleGetTemperature);
  server.begin();
  Serial.println("HTTP server started");

  updateTempTimer.start();
  updateSolarHeaterTimer.start();
}

void updateTemperature(){
  tempC = readTemperatureC();
  tempF = celsiusToFahrenheit(tempC);

  if (!isnan(tempC)) {
    Serial.print("Current Temperature: ");
    Serial.print(tempC);
    Serial.print("°C / ");
    Serial.print(tempF);
    Serial.println("°F");
  }  
}

void updateSolarHeater(){
  sunLevel = analogRead(SUN_PANEL_PIN);

  if ( sunLevel > SUN_ON_LEVEL) {
    heater_on = 1;
  }
  else{
    heater_on = 0;
  }
  digitalWrite(SOLAR_RELAY_PIN,heater_on);
  Serial.print("SunLevel: ");Serial.print(sunLevel);
  Serial.print(" Heater: ");Serial.println(heater_on);
}

void loop() {
  server.handleClient();  // Handle web requests
  updateTempTimer.update();
  updateSolarHeaterTimer.update();

}
