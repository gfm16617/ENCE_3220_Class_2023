// ---------------------------------------------------------------------------------------
//
// Code for a simple webserver on the ESP32 (device used for tests: ESP32-WROOM-32D).
// The code generates two random numbers on the ESP32 and uses Websockets to continuously
// update the web-clients. For data transfer JSON encapsulation is used.
//
// For installation, the following libraries need to be installed:
// * Websockets by Markus Sattler (can be tricky to find -> search for "Arduino Websockets"
// * ArduinoJson by Benoit Blanchon
//
// NOTE: in principle this code is universal and can be used on Arduino AVR as well. However, AVR is only supported with version 1.3 of the webSocketsServer. Also, the Websocket
// library will require quite a bit of memory, so wont load on Arduino UNO for instance. The ESP32 and ESP8266 are cheap and powerful, so use of this platform is recommended. 
//
// Refer to https://youtu.be/15X0WvGaVg8
//
// Written by mo thunderz (last update: 27.08.2022)
//
// ---------------------------------------------------------------------------------------

//#include <WiFi.h>                                     // needed to connect to WiFi
#include <ESP8266WiFi.h>
//#include <WebServer.h>                                // needed to create a simple webserver (make sure tools -> board is set to ESP32, otherwise you will get a "WebServer.h: No such file or directory" error)
#include <ESP8266WebServer.h>
#include <WebSocketsServer.h>                         // needed for instant communication between client and server through Websockets
#include <ArduinoJson.h>                              // needed for JSON encapsulation (send multiple variables with one string)

// SSID and password of Wifi connection:
const char* ssid = "Kitchen Timer IoT";
const char* password = "";

// The String below "webpage" contains the complete HTML code that is sent to the client whenever someone connects to the webserver
String webpage = "<!DOCTYPE html><html><head><meta charset='UTF-8'> <title>Kitchen Timer IoT</title> <style>.container-timer{display: flex;flex-direction: row;justify-content: left;align-items: center;margin: 1rem;}.container-text {max-width: 500px;margin: 0 auto;padding: 2rem;box-shadow: 0px 2px 6px rgba(0, 0, 0, 0.1);background-color: #fff;border-radius: 0.5rem;line-height: 1;font-size: 1.2rem;color: #333;text-align: justify;text-justify: inter-word;word-wrap: break-word;}input[type='text'] {width: 10%;padding: 0.5rem;font-size: 1.2rem;border: none;border-radius: 0.5rem;box-shadow: 0px 2px 6px rgba(0, 0, 0, 0.1);background-color: #f5f5f5;font-family: Arial, sans-serif;transition: box-shadow 0.2s ease-in-out;}input[type='text']:focus {outline: none;box-shadow: 0px 4px 8px rgba(0, 0, 0, 0.2);} /* Style for the number display */ #number-display { font-size: 5rem; text-align: center; padding: 1rem; background-color: #eee; border-radius: 0.5rem; margin-bottom: 2rem; } /* Styles for the buttons */ .button { display: inline-block; background-color: #4CAF50; color: white; padding: 1rem 2rem; text-align: center; text-decoration: none; font-size: 1.5rem; margin: 0 1rem; cursor: pointer; border-radius: 0.5rem; border: none; } .button:hover { background-color: #3e8e41; } </style></head><body><p><div id='number-display'>00:00</div><button class='button' id='BTN_INC'>Increment</button><button class='button' id='BTN_START'>Start</button><button class='button' id='BTN_STOP'>Stop</button></p><div class='container-timer'><p><button class='button' id='BTN_SET'>Set Timer</button></p><input type='text' id='SET_TIME' placeholder='01:30'></div><div class='container-text'><p>Version 1.0: April 2023</p><p>Set Timer Format: MM:SS</p><p><b>Note:</b> Set Timer should not exceed 59mins:59secs (e.g.: 59:59)</p><p>This version doesn't not check if Set Timer was introduce properly. Make sure that you follow the specified format.</p></div></body><script> var Socket; document.getElementById('BTN_INC').addEventListener('click', button_inc); document.getElementById('BTN_START').addEventListener('click', button_start); document.getElementById('BTN_STOP').addEventListener('click', button_stop); document.getElementById('BTN_SET').addEventListener('click', button_set); function init() { Socket = new WebSocket('ws://' + window.location.hostname + ':81/'); Socket.onmessage = function(event) { processCommand(event); }; } function button_set() {var textBox = document.getElementById('SET_TIME'); var msg = {type: 'button_set',state: 'pressed',value: textBox.value};Socket.send(JSON.stringify(msg)); } function button_inc() { var msg = {type: 'button_inc',state: 'pressed'};Socket.send(JSON.stringify(msg)); } function button_start() { var msg = {type: 'button_start',state: 'pressed'};Socket.send(JSON.stringify(msg)); } function button_stop() { var msg = {type: 'button_stop',state: 'pressed'};Socket.send(JSON.stringify(msg)); } function processCommand(event) { var obj = JSON.parse(event.data);document.getElementById('number-display').innerHTML = obj.time;console.log(obj.time); } window.onload = function(event) { init(); }</script></html>";

// The JSON library uses static memory, so this will need to be allocated:
// -> in the video I used global variables for "doc_tx" and "doc_rx", however, I now changed this in the code to local variables instead "doc" -> Arduino documentation recomends to use local containers instead of global to prevent data corruption

// We want to periodically send values to the clients, so we need to define an "interval" and remember the last time we sent data to the client (with "previousMillis")
int interval = 1000;                                  // send data to the client every 1000ms -> 1s
unsigned long previousMillis = 0;                     // we use the "millis()" command for time reference and this will output an unsigned long

// Initialization of webserver and websocket
//WebServer server(80);                                 // the server uses port 80 (standard port for websites
ESP8266WebServer server(80);
WebSocketsServer webSocket = WebSocketsServer(81);    // the websocket uses port 81 (standard port for websockets

#define BUFF_SIZE 20
char  gIncomingChar;
char  gCommsMsgBuff[BUFF_SIZE];
int   iBuff = 0;
byte  gPackageFlag = 0;
byte  gProcessDataFlag = 0;

void setup() {

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH); // Turn it off
  
  // Init comms with Kitchen Timer
  Serial.begin(9600);

  WiFi.softAP(ssid, password); // Start the access point
  
  server.on("/", []() {                               // define here wat the webserver needs to do
    server.send(200, "text/html", webpage);           //    -> it needs to send out the HTML string "webpage" to the client
  });
  server.begin();                                     // start server
  
  webSocket.begin();                                  // start websocket
  webSocket.onEvent(webSocketEvent);                  // define a callback function -> what does the ESP32 need to do when an event from the websocket is received? -> run function "webSocketEvent()"
}

void loop() {
  
  server.handleClient();                              // Needed for the webserver to handle all clients
  webSocket.loop();                                   // Update function for the webSockets 
  
  unsigned long now = millis();                       // read out the current "time" ("millis()" gives the time in ms since the Arduino started)
  if ((unsigned long)(now - previousMillis) > interval) { // check if "interval" ms has passed since last time the clients were updated

    // GET request
    Serial.print("$GET\n");

    // Get time through serial port
    gIncomingChar = Serial.read();
    
    // If start of the package
    if(gIncomingChar == '$')
    {
      // Clear Buffer
      for(int i=0; i<BUFF_SIZE; i++)
      {
        gCommsMsgBuff[i] = 0;
      }

      // set gCommsMsgBuff Index to zero
      iBuff = 0;

      gIncomingChar = Serial.read();
      while(gIncomingChar != '\n')
      {
        gCommsMsgBuff[iBuff] = gIncomingChar;
        iBuff++;
        gIncomingChar = Serial.read();

        if(iBuff == BUFF_SIZE)
        {
          gCommsMsgBuff[0] = 'x';
          gCommsMsgBuff[1] = 'x';
          gCommsMsgBuff[2] = ':';
          gCommsMsgBuff[3] = 'x';
          gCommsMsgBuff[4] = 'x';

          break;
        }
      }
    }  
    // --------
    
    // Send time thtough websocket
    String jsonString = "";
    StaticJsonDocument<200> doc;
    JsonObject object = doc.to<JsonObject>();
    object["time"] = String(gCommsMsgBuff); //"54:47";
    serializeJson(doc, jsonString);
    webSocket.broadcastTXT(jsonString);
    // --------
    
    previousMillis = now;                             // reset previousMillis
  }
}

void webSocketEvent(byte num, WStype_t type, uint8_t * payload, size_t length) {      // the parameters of this callback function are always the same -> num: id of the client who send the event, type: type of message, payload: actual data sent and length: length of payload
  switch (type) {                                     // switch on the type of information sent
    case WStype_DISCONNECTED:                         // if a client is disconnected, then type == WStype_DISCONNECTED
      //Serial.println("Client " + String(num) + " disconnected");
      break;
    case WStype_CONNECTED:                            // if a client is connected, then type == WStype_CONNECTED
      //Serial.println("Client " + String(num) + " connected");
      // optionally you can add code here what to do when connected
      break;
    case WStype_TEXT:                                 // if a client has sent data, then type == WStype_TEXT
      // try to decipher the JSON string received
      StaticJsonDocument<200> doc;                    // create a JSON container
      DeserializationError error = deserializeJson(doc, payload);
      if (error) {
        //Serial.print(F("deserializeJson() failed: "));
        //Serial.println(error.f_str());
        return;
      }
      else {
        // JSON string was received correctly, so information can be retrieved:
        const char* g_type = doc["type"];
        const char* g_state = doc["state"];
        const char* g_value = doc["value"];

        if(strcmp(g_type, "button_set") == 0)
        {
          Serial.print("$SET,TMRS,");
          Serial.print(String(g_value));
          Serial.print("\n");
        }

        if(strcmp(g_type, "button_inc") == 0)
        {
          //Serial.print("$SET,TMRS,10:10\n");
          Serial.print("$INC\n");
        }

        if(strcmp(g_type, "button_start") == 0)
        {
          Serial.print("$STR\n");
        }

        if(strcmp(g_type, "button_stop") == 0)
        {
          Serial.print("$STP\n");
        }

        // -- Debug
        digitalWrite(LED_BUILTIN, LOW);
        delay(100);
        digitalWrite(LED_BUILTIN, HIGH);
        delay(100);
        // --
        
      }
      break;
  }
}
