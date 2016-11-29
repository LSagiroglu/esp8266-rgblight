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
#include "esp8266basefunctions.h"




const int redPin = D5;
const int greenPin = D1;
const int bluePin = D6;

// Topics
const char* light_state_topic = "home/rgb1";
const char* light_set_topic = "home/rgb1/set";

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
  // since the LED strips are common anode, 255 = off and 0 = on; invert the numbers here: 
  analogWrite(redPin, 255-inR);
  analogWrite(greenPin, 255-inG);
  analogWrite(bluePin, 255-inB);
}



void setup() {
  // the very FIRST thing to do is turn the light on
  pinMode(redPin, OUTPUT);
  pinMode(greenPin, OUTPUT);
  pinMode(bluePin, OUTPUT);
  analogWriteRange(255);
  digitalWrite(redPin, 0);
  digitalWrite(greenPin, 0);
  digitalWrite(bluePin, 0);
  
  setupBaseFunctions(); // <-- must be FIRST
  // at this point you're connected, MQTT is working serial is enabled 115200
  delay(1000);
  Serial.println("Updating MQTT after boot");
  //and now that we're connected, notify HomeAssistant that we're on and bright
  sendState();
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
