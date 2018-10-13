#include <ESP8266WiFi.h>
#include "./DNSServer.h"                  // Patched lib
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>
#include <FS.h>   // Include the SPIFFS library
#include <Servo.h>
#include <EEPROM.h>
#include <ESP8266mDNS.h>        // Include the mDNS library

String getContentType(String filename); // convert the file extension to the MIME type
bool handleFileRead(String path);       // send the right file to the client (if it exists)3


const byte        DNS_PORT = 53;          // Capture DNS requests on port 53
IPAddress         apIP(10, 10, 10, 1);    // Private network for server
DNSServer         dnsServer;              // Create the DNS object
ESP8266WebServer  webServer(80);          // HTTP server

Servo myservo;

void setup() {
  myservo.attach(0);
  myservo.write(0);
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  WiFi.softAP("KoalaIOT", "zxcasdqwe");
  EEPROM.begin(4096);
  dnsServer.start(DNS_PORT, "*", apIP);


  webServer.on("/status", [](){
    int position = myservo.read();
    // digitalWrite(2, HIGH);
    webServer.send(200, "application/json", "{\"position\":\""+String(position)+"\"}");
  });
  
  webServer.on("/config", HTTP_POST, [](){
    String ssid = urldecode(webServer.arg("ssid")); //Recibimos los valores que envia por GET el formulario web
    String password = urldecode(webServer.arg("password"));
    if(verifyAccount(ssid, password)) {
      writeEEPROM(70, "configured");
      writeEEPROM(1, ssid);
      writeEEPROM(30, password);
      dnsServer.stop();
      webServer.send(200, "application/json", "{\"status\":true, \"ssid\": "+ssid+", \"passwd\":\""+password+"\", \"ip\":\""+WiFi.localIP().toString()+"\" }");
    } else {
      writeEEPROM(70, "noconfigured");
      webServer.send(200, "application/json", "{\"status\":false, \"ssid\": "+ssid+"}");
    }
  });
  webServer.on("/disconnect", [](){
    writeEEPROM(70, "noconfigured");
    writeEEPROM(1, "");
    writeEEPROM(30, "");
    WiFi.disconnect(true);
  });
  
  webServer.on("/networks", [](){
    String json = "{\"networks\":[";
    int networks = WiFi.scanNetworks();
    for (int i = 0; i < networks; i++){
      json += "\""+String(WiFi.SSID(i))+"\"";
      if(i < networks-1) {
        json += ",";
      }
    }
    json += "]}";
    webServer.send(200, "application/json", json);
  });
  webServer.on("/get", [](){
    String url = webServer.arg("url");
    HTTPClient http;
    http.begin(url);
    http.addHeader("Content-Type", "application/json");
    int httpCode = http.GET();
    if(httpCode == HTTP_CODE_OK) {
      String response = http.getString();
      webServer.send(200, "text/html", response);
    } else {
      webServer.send(200, "text/html", "no data");
    }
    http.end();
  });
  webServer.on("/on", [](){
    myservo.write(180);
    webServer.send(200, "application/json", "{\"status\": \"ON\"}");
  });
  webServer.on("/off", [](){
    myservo.write(0);
    webServer.send(200, "application/json", "{\"status\": \"OFF\"}");
  });
  webServer.on("/move", [](){
    int position = webServer.arg("position").toInt();
    if(position >= 0 && position <= 180) {
      myservo.write(position);
      webServer.send(200, "application/json", "{\"position\":\""+String(position)+"\"}");
    } else {
      webServer.send(200, "application/json", "{\"error\": \"no valid position\"}");
    }
  });
  // replay to all requests with same HTML
  webServer.onNotFound(handleNotFound);
  webServer.begin();
  SPIFFS.begin();                           // Start the SPI Flash Files System
  tryConnect();
}

void handleNotFound() {  // if the requested file or page doesn't exist, return a 404 not found error
  if (!handleFileRead(webServer.uri())) {        // check if the file exists in the flash memory (SPIFFS), if so, send it
    handleFileRead("/");
  }
}

void loop() {
  dnsServer.processNextRequest();
  webServer.handleClient();
}

String getContentType(String filename) { // convert the file extension to the MIME type
  if (filename.endsWith(".html")) return "text/html";
  else if (filename.endsWith(".css")) return "text/css";
  else if (filename.endsWith(".js")) return "application/javascript";
  else if (filename.endsWith(".ico")) return "image/x-icon";
  return "text/plain";
}

bool handleFileRead(String path) { // send the right file to the client (if it exists)
  if (path.endsWith("/")) path += "index.html";         // If a folder is requested, send the index file
  String contentType = getContentType(path);            // Get the MIME type
  if (SPIFFS.exists(path)) {                            // If the file exists
    File file = SPIFFS.open(path, "r");                 // Open it
    size_t sent = webServer.streamFile(file, contentType); // And send it to the client
    file.close();                                       // Then close the file again
    return true;
  }
  return false;                                         // If the file doesn't exist, return false
}
bool verifyAccount(String ssid, String passwd) {
  int count = 0;
  bool status = true;
  WiFi.begin(ssid.c_str(), passwd.c_str());     //Intentamos conectar
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    count++;
    if (count > 20) {
      status = false;
      return status;
    }
  }
  
  return status;
}
void tryConnect() {
  if (readEEPROM(70).equals("configured")) {
    String ssid = readEEPROM(1);      //leemos ssid y password
    String passwd = readEEPROM(30);
    if(!verifyAccount(ssid, passwd)) {
      writeEEPROM(70, "noconfigured");
    } else {
      dnsServer.stop();
    }
  }
}
String readEEPROM(int address) {
 String value;
 int intValue;
 int sizeValue = EEPROM.read(address);
 for (int i = 0;i < sizeValue; i++) {
  address++;
  intValue = EEPROM.read(address);
  value += (char)intValue;
 }
 return value;
}
void writeEEPROM(int address, String value) {
 int sizeValue = (value.length() + 1);
 char inchar[30];    //'30' Tamaño maximo del string
 value.toCharArray(inchar, sizeValue);
 EEPROM.write(address, sizeValue);
 for (int i = 0; i < sizeValue; i++) {
  address++;
  EEPROM.write(address, inchar[i]);
 }
 EEPROM.commit();
}
String urldecode(String value) {
  value.replace("%C3%A1", "á");
  value.replace("%C3%A9", "é");
  value.replace("%C3%A", "i");
  value.replace("%C3%B3", "ó");
  value.replace("%C3%BA", "ú");
  value.replace("%21", "!");
  value.replace("%23", "#");
  value.replace("%24", "$");
  value.replace("%25", "%");
  value.replace("%26", "&");
  value.replace("%27", "/");
  value.replace("%28", "(");
  value.replace("%29", ")");
  value.replace("%3D", "=");
  value.replace("%3F", "?");
  value.replace("%27", "'");
  value.replace("%C2%BF", "¿");
  value.replace("%C2%A1", "¡");
  value.replace("%C3%B1", "ñ");
  value.replace("%C3%91", "Ñ");
  value.replace("+", " ");
  value.replace("%2B", "+");
  value.replace("%22", "\"");
  return value;
}

