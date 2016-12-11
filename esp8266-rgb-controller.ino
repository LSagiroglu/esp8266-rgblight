////////////
// LED For HomeAssistant
// 
// Define in configuration.yaml like this: 
// - platform: mqtt_json
//   name: mqtt_json_light_1
//   state_topic: "home/rgb1"
//   command_topic: "home/rgb1/set"
//   brightness: true
//   rgb: true
//   optimistic: false
//   qos: 0
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// For wifimanager
#include <FS.h>                   //this needs to be first, or it all crashes and burns...
#include <ESP8266WiFi.h>          //https://github.com/esp8266/Arduino
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager
#include <ArduinoJson.h>          //https://github.com/bblanchon/ArduinoJson
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// for pubsub
#include <SPI.h>
#include <PubSubClient.h>
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// for OTA
#include <WiFiClient.h>
//#include <ESP8266WebServer.h> // <-- this is needed for both WifiManager and OTA; included above
#include <ESP8266mDNS.h>
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Compressed everything into this .h file, to clean up the actual function
//#include "esp8266basefunctions.h"
// Functions and variables used in the Wifi 
void loopBaseFunctions();
void setupBaseFunctions();
void reconnect();
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// #wifimanager - these are offered on WifiManager 
char mqtt_server[20];
char mqtt_port[6] = "1883";
char mqtt_subscribe_topic[25] = "home/esp/kitchen/set";

//flag for saving data
bool shouldSaveConfig = false;
void MQTTcallback(char*, byte*, unsigned int);
WiFiClient espClient;
PubSubClient client(espClient);
// Topics
const char* light_state_topic = "home/esp/kitchen";
//const char* mqtt_subscribe_topic = "home/esp/kitchen/set";

// this will hold the hostname, like esp8266-123456 
char host[20] = "esp8266-test";
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


const int redPin = 14;//D5;
const int greenPin = 12;//D1;
const int bluePin = 5;//D6;
const char* on_cmd = "ON";
const char* off_cmd = "OFF";
const int BUFFER_SIZE = JSON_OBJECT_SIZE(10);
// Maintained state for reporting to HA
byte red = 255;
byte green = 255;
byte blue = 255;
byte brightness = 255;
// Real values to write to the LEDs (ex. including brightness and state)
byte realRed = 0;
byte realGreen = 0;
byte realBlue = 0;
bool stateOn = true;
// Globals for fade/transitions
bool startFade = false;
unsigned long lastLoop = 0;
int transitionTime = 0;
bool inFade = false;
int loopCount = 0;
int stepR, stepG, stepB;
int redVal, grnVal, bluVal;

  /*
  SAMPLE PAYLOAD:
    {
      "brightness": 120,
      "color": {
        "r": 255,
        "g": 100,
        "b": 100
      },
      "flash": 2,
      "transition": 5,
      "state": "ON"
    }
  */

  
void setup() {
  // the very FIRST thing to do is turn the light on
  pinMode(redPin, OUTPUT);
  pinMode(greenPin, OUTPUT);
  pinMode(bluePin, OUTPUT);
  analogWriteRange(255);
  digitalWrite(redPin, 200);
  digitalWrite(greenPin, 200);
  digitalWrite(bluePin, 170);
  
  setupBaseFunctions(); // <-- must be FIRST
  // at this point you're connected, MQTT is working serial is enabled 115200
  delay(1000);
  Serial.println("Updating MQTT after boot");
  //and now that we're connected, notify HomeAssistant that we're on and bright
  sendState();
}

void MQTTcallback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");

  char message[length + 1];
  for (int i = 0; i < length; i++) {
    message[i] = (char)payload[i];
  }
  message[length] = '\0';
  Serial.println(message);

  if (!processJson(message)) {
    return;
  }

  if (stateOn) {
    // Update lights
    realRed = map(red, 0, 255, 0, brightness);
    realGreen = map(green, 0, 255, 0, brightness);
    realBlue = map(blue, 0, 255, 0, brightness);
  }
  else {
    realRed = 0;
    realGreen = 0;
    realBlue = 0;
  }

  startFade = true;
  inFade = false; // Kill the current fade
  sendState();
}


bool processJson(char* message) {
  StaticJsonBuffer<BUFFER_SIZE> jsonBuffer;
  JsonObject& root = jsonBuffer.parseObject(message);
  if (!root.success()) {
    Serial.println("parseObject() failed");
    return false;
  }

  if (root.containsKey("state")) {
    if (strcmp(root["state"], on_cmd) == 0) {
      stateOn = true;
    }
    else if (strcmp(root["state"], off_cmd) == 0) {
      stateOn = false;
    }
  }
  if (root.containsKey("color")) {
    red = root["color"]["r"];
    green = root["color"]["g"];
    blue = root["color"]["b"];
  }
  if (root.containsKey("brightness")) {
    brightness = root["brightness"];
  }
  if (root.containsKey("transition")) {
    transitionTime = root["transition"];
  }
  else {
    transitionTime = 0;
  }
  return true;
}

void sendState() {
  StaticJsonBuffer<BUFFER_SIZE> jsonBuffer;
  JsonObject& root = jsonBuffer.createObject();
  root["state"] = (stateOn) ? on_cmd : off_cmd;
  JsonObject& color = root.createNestedObject("color");
  color["r"] = red;
  color["g"] = green;
  color["b"] = blue;
  root["brightness"] = brightness;
  char buffer[root.measureLength() + 1];
  root.printTo(buffer, sizeof(buffer));
  client.publish(light_state_topic, buffer, true);
}


void setColor(int inR, int inG, int inB) {
  analogWrite(redPin, inR);
  analogWrite(greenPin, inG);
  analogWrite(bluePin, inB);
}



void loop() {
  if (startFade) {
    // If we don't want to fade, skip it.
    if (transitionTime == 0) {
      setColor(realRed, realGreen, realBlue);
      redVal = realRed;
      grnVal = realGreen;
      bluVal = realBlue;
      startFade = false;
    }
    else {
      loopCount = 0;
      stepR = calculateStep(redVal, realRed);
      stepG = calculateStep(grnVal, realGreen);
      stepB = calculateStep(bluVal, realBlue);
      inFade = true;
    }
  }
  if (inFade) {
    startFade = false;
    unsigned long now = millis();
    if (now - lastLoop > transitionTime) {
      if (loopCount <= 1020) {
        lastLoop = now;
        
        redVal = calculateVal(stepR, redVal, loopCount);
        grnVal = calculateVal(stepG, grnVal, loopCount);
        bluVal = calculateVal(stepB, bluVal, loopCount);
        
        setColor(redVal, grnVal, bluVal); // Write current values to LED pins

        Serial.print("Loop count: ");
        Serial.println(loopCount);
        loopCount++;
      }
      else {
        inFade = false;
      }
    }
  }
  //run at the end of each cycle:
  loopBaseFunctions();
}


int calculateStep(int prevValue, int endValue) {
    int step = endValue - prevValue; // What's the overall gap?
    if (step) {                      // If its non-zero, 
        step = 1020/step;            //   divide by 1020
    }
    
    return step;
}

/* The next function is calculateVal. When the loop value, i,
*  reaches the step size appropriate for one of the
*  colors, it increases or decreases the value of that color by 1. 
*  (R, G, and B are each calculated separately.)
*/
int calculateVal(int step, int val, int i) {
    if ((step) && i % step == 0) { // If step is non-zero and its time to change a value,
        if (step > 0) {              //   increment the value if step is positive...
            val += 1;
        }
        else if (step < 0) {         //   ...or decrement it if step is negative
            val -= 1;
        }
    }
    
    // Defensive driving: make sure val stays in the range 0-255
    if (val > 255) {
        val = 255;
    }
    else if (val < 0) {
        val = 0;
    }
    
    return val;
}




//WifiManager will call this to save configuration
void saveConfigCallback () {
  Serial.println("Should save config");
  shouldSaveConfig = true;
}


void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    
    if (client.connect(host)) {
      Serial.println("connected");
      // Once connected, publish an announcement...
      client.publish("esp8266boot","connected");
      // ... and resubscribe
      client.subscribe(mqtt_subscribe_topic);
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }

}

ESP8266WebServer server(80);
//WiFiServer TelnetServer(8266);
const char* serverIndex = "<form method='POST' action='/update' enctype='multipart/form-data'><input type='file' name='update'><input type='submit' value='Update'></form>";


void setupBaseFunctions() {
  // put your setup code here, to run once:
  Serial.begin(115200);
  Serial.println();

  sprintf(host, "esp8266-%d", ESP.getChipId());

  //read configuration from FS json
  Serial.println("mounting FS...");

  if (SPIFFS.begin()) {
    //clean FS, for testing
    //
    //SPIFFS.format();              // this will forget all custom params (MQTT)
    Serial.println("mounted file system");
    if (SPIFFS.exists("/config.json")) {
      //file exists, reading and loading
      Serial.println("reading config file");
      File configFile = SPIFFS.open("/config.json", "r");
      if (configFile) {
        Serial.println("opened config file");
        size_t size = configFile.size();
        // Allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> buf(new char[size]);

        configFile.readBytes(buf.get(), size);
        DynamicJsonBuffer jsonBuffer;
        JsonObject& json = jsonBuffer.parseObject(buf.get());
        json.printTo(Serial);
        if (json.success()) {
          Serial.println("\nparsed json");

          strcpy(mqtt_server, json["mqtt_server"]);
          strcpy(mqtt_port, json["mqtt_port"]);
          strcpy(mqtt_subscribe_topic, json["mqtt_subscribe_topic"]);

        } else {
          Serial.println("failed to load json config");
        }
      }
    }
  } else {
    Serial.println("failed to mount FS");
  }
  //end read

  // The extra parameters to be configured (can be either global or just in the setup)
  // After connecting, parameter.getValue() will get you the configured value
  // id/name placeholder/prompt default length
  WiFiManagerParameter custom_mqtt_server("server", "192.168.140.14", mqtt_server, 20);
  WiFiManagerParameter custom_mqtt_port("port", "1883", mqtt_port, 5);
  WiFiManagerParameter custom_mqtt_subscribe("subscribe", "home/esp/kitchen/set", mqtt_subscribe_topic, 25);
  
  WiFiManager wifiManager;

  //reset settings - for testing - DO BOTH!!
  //wifiManager.resetSettings();  // this will forget wifi/pass 
  //SPIFFS.format();              // this will forget all custom params (MQTT)
  // you may need to do SPIFFS.format() above if a new param is involved

  
  //set config save notify callback
  wifiManager.setSaveConfigCallback(saveConfigCallback);

  //set static ip
  //wifiManager.setSTAStaticIPConfig(IPAddress(10,0,1,99), IPAddress(10,0,1,1), IPAddress(255,255,255,0));

  //add all your parameters here
  wifiManager.addParameter(&custom_mqtt_server);
  wifiManager.addParameter(&custom_mqtt_port);
  wifiManager.addParameter(&custom_mqtt_subscribe);


  //set minimu quality of signal so it ignores AP's under that quality
  //defaults to 8%
  //wifiManager.setMinimumSignalQuality();

  //sets timeout until configuration portal gets turned off
  //useful to make it all retry or go to sleep
  //in seconds
  //wifiManager.setTimeout(120);

  //fetches ssid and pass and tries to connect
  //if it does not connect it starts an access point with the specified name
  //here  "AutoConnectAP"
  //and goes into a blocking loop awaiting configuration
  if (!wifiManager.autoConnect()) {
    Serial.println("failed to connect and hit timeout");
    delay(3000);
    //reset and try again, or maybe put it to deep sleep
    ESP.reset();
    delay(5000);
  }

  //if you get here you have connected to the WiFi
  Serial.println("connected...yeey :)");

  //read updated parameters
  strcpy(mqtt_server, custom_mqtt_server.getValue());
  strcpy(mqtt_port, custom_mqtt_port.getValue());
  strcpy(mqtt_subscribe_topic, custom_mqtt_subscribe.getValue());

  //save the custom parameters to FS
  if (shouldSaveConfig) {
    Serial.println("saving config");
    DynamicJsonBuffer jsonBuffer;
    JsonObject& json = jsonBuffer.createObject();
    json["mqtt_server"] = mqtt_server;
    json["mqtt_port"] = mqtt_port;
    json["mqtt_subscribe_topic"] = mqtt_subscribe_topic;

    File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile) {
      Serial.println("failed to open config file for writing");
    }

    json.printTo(Serial);
    json.printTo(configFile);
    configFile.close();
    //end save
  }

  Serial.println("local ip");
  Serial.println(WiFi.localIP());
  client.setServer(mqtt_server, atoi(mqtt_port));
  client.setCallback(MQTTcallback);

  if (WiFi.waitForConnectResult() == WL_CONNECTED) {
    MDNS.begin(host);
    server.on("/", HTTP_GET, []() {
      server.sendHeader("Connection", "close");
      server.sendHeader("Access-Control-Allow-Origin", "*");
      server.send(200, "text/html", serverIndex);
    });
    server.on("/update", HTTP_POST, []() {
      server.sendHeader("Connection", "close");
      server.sendHeader("Access-Control-Allow-Origin", "*");
      server.send(200, "text/plain", (Update.hasError()) ? "FAIL" : "OK");
      ESP.restart();
    }, []() {
      HTTPUpload& upload = server.upload();
      if (upload.status == UPLOAD_FILE_START) {
        Serial.setDebugOutput(true);
        WiFiUDP::stopAll();
        Serial.printf("Update: %s\n", upload.filename.c_str());
        uint32_t maxSketchSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
        if (!Update.begin(maxSketchSpace)) { //start with max available size
          Update.printError(Serial);
        }
      } else if (upload.status == UPLOAD_FILE_WRITE) {
        if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
          Update.printError(Serial);
        }
      } else if (upload.status == UPLOAD_FILE_END) {
        if (Update.end(true)) { //true to set the size to the current progress
          Serial.printf("Update Success: %u\nRebooting...\n", upload.totalSize);
        } else {
          Update.printError(Serial);
        }
        Serial.setDebugOutput(false);
      }
      yield();
    });
    server.begin();
    MDNS.addService("http", "tcp", 80);
    Serial.printf("Ready! Open http://%s.local in your browser\n", host);
  } else {
    Serial.println("WiFi Failed");
  }
  // send it through a loop of the normal run to connect and get MQTT working before proceeding
  loopBaseFunctions();
  loopBaseFunctions();
  Serial.printf("Base heap size: %u\n", ESP.getFreeHeap());
}

void loopBaseFunctions() {
  // This is the PubSubClient
  if (!client.connected()) {
    reconnect();
  }
  client.loop();
  // This is for the firmware OTA
  server.handleClient();
  
}


