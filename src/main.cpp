#include "screen.h"
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include "credentials.h"

// Debug flag
#ifndef DEBUG
#define DEBUG
#endif

// WiFi credentials
const char* ssid = STASSID;
const char* password = STAPSK;

// Grinding times in ms
#define SINGLE_SHOT_TIME 8700
#define DOUBLE_SHOT_TIME 12600

// Update intervalls in ms
#define PIN_INTERVAL 10
#define SCREEN_INTERVAL 80

// define first and last page
#define FIRST_PAGE 0
#define LAST_PAGE 1

// update timestamps
unsigned long lastPinUpdate = 0;
unsigned long lastScreenUpdate = 0;
unsigned long lastScreenRedraw = 0;
boolean doScreenRedraw = false;

// grinding
unsigned long remainingGrindTime = 0;
boolean grinding = false;

// input pin states
boolean lastGrindPin = false;
boolean lastPageUpPin = false;
boolean lastPageDownPin = false;

// current screen
int currentPage = 0;

// current state
struct
{
  boolean grinding = false;
  boolean programming = false;
  byte page = 0;
  unsigned long time = 0;
} state;


void OTA_init()
{
    // Port defaults to 8266
  // ArduinoOTA.setPort(8266);

  // Hostname defaults to esp8266-[ChipID]
  // ArduinoOTA.setHostname("myesp8266");

  // No authentication by default
  // ArduinoOTA.setPassword("admin");

  // Password can be set with it's md5 value as well
  // MD5(admin) = 21232f297a57a5a743894a0e4a801fc3
  // ArduinoOTA.setPasswordHash("21232f297a57a5a743894a0e4a801fc3");

  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else {  // U_FS
      type = "filesystem";
    }

    // NOTE: if updating FS this would be the place to unmount FS using FS.end()
    Serial.println("Start updating " + type);
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) {
      Serial.println("Auth Failed");
    } else if (error == OTA_BEGIN_ERROR) {
      Serial.println("Begin Failed");
    } else if (error == OTA_CONNECT_ERROR) {
      Serial.println("Connect Failed");
    } else if (error == OTA_RECEIVE_ERROR) {
      Serial.println("Receive Failed");
    } else if (error == OTA_END_ERROR) {
      Serial.println("End Failed");
    }
  });
  ArduinoOTA.begin();
}

// get grinding time for current page
unsigned long get_grind_time(int page)
{
  unsigned long grindTime = 0;
  switch (page)
  {
    case 0: grindTime = SINGLE_SHOT_TIME; break;
    case 1: grindTime = DOUBLE_SHOT_TIME; break;
    default: break;
  }
  return grindTime;
}

// update remaining grind time
void update_remaining_grind_time(boolean grindActive, unsigned long lastUpdate, unsigned long* grindTime)
{
  if (grindActive)
  {
    *grindTime = *grindTime - (millis() - lastUpdate);
  }
  //return *grindTime;
}

// update new grind time
void update_new_grind_time(boolean grindActive, unsigned long lastUpdate, unsigned long* grindTime)
{
  if (grindActive)
  {
    *grindTime = *grindTime + (millis() - lastUpdate);
  }
  //return *grindTime;
}

void setup(void) {
    // serial init
  Serial.begin(115200);
  Serial.println();
  Serial.println("Booting ...");

  // pin init
  pinMode(D5, INPUT_PULLUP); // grinding request
  pinMode(D6, INPUT_PULLUP); // page up
  pinMode(D7, INPUT_PULLUP); // page down
  pinMode(D8, OUTPUT);  // grinding output

  // init display
  init_screen();

  // start WIFI and connect
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println("Connection Failed! Rebooting...");
    delay(5000);
    ESP.restart();
  }

  // start OTA
  OTA_init();

  while (3000 > millis()) {};

  // first page
  currentPage = 0;
  grinding = false;
  remainingGrindTime = SINGLE_SHOT_TIME;
  draw_page(currentPage, grinding, remainingGrindTime);
  lastScreenRedraw = millis();
  lastScreenUpdate = lastScreenRedraw;
  lastPinUpdate = millis();

  // summary
  Serial.println("Ready");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

void loop(void) {
  // handle OTA
  ArduinoOTA.handle();

  // Pin update
  if (millis() - lastPinUpdate > PIN_INTERVAL)
  {
    // grind Pin D5
    boolean grindPinState = not digitalRead(D5);
    if (grindPinState != lastGrindPin)
    {
      grinding = grindPinState;
      lastGrindPin = grindPinState;
    }
    // page up pin D6
    boolean pageUpPinState = not digitalRead(D6);
    if (pageUpPinState != lastPageUpPin)
    {
      currentPage = currentPage + pageUpPinState;
      if (1 < currentPage)
      {
        currentPage = 0;
      }
      if (pageUpPinState)
      {
        remainingGrindTime = get_grind_time(currentPage);
      }
      doScreenRedraw = pageUpPinState;
      lastPageUpPin = pageUpPinState;
    }
    // page down pin D7
    boolean pageDownPinState = not digitalRead(D7);
    if (pageDownPinState != lastPageDownPin)
    {
      currentPage = currentPage - pageDownPinState;
      if (0 > currentPage)
      {
        currentPage = 1;
      }
      if (pageDownPinState)
      {
        remainingGrindTime = get_grind_time(currentPage);
      }
      doScreenRedraw = pageDownPinState;
      lastPageDownPin = pageDownPinState;
    }
    // calculate remaining grind time
    update_remaining_grind_time(grinding, lastPinUpdate, &remainingGrindTime);
    // reset grind time if time elapsed and update page
    if (1000000 < remainingGrindTime)
    {
      grinding = false;
      doScreenRedraw = true;
      remainingGrindTime = 0;
    }
    // Redraw page if grind pin released
    if ((0 == remainingGrindTime) && (not grindPinState))
    {
      doScreenRedraw = true;
      remainingGrindTime = get_grind_time(currentPage);
    }

    // update state diagram
    if (grindPinState && not pageUpPinState && not pageDownPinState)
    {
      state.grinding = true;
      state.programming = false;
      state.time = state.time - (millis() - lastPinUpdate);
    }
    else if ((grindPinState && pageUpPinState && not pageDownPinState) ||
      (grindPinState && not pageUpPinState && pageDownPinState))
    {
      state.grinding = true;
      state.programming = true;
      state.time = state.time + (millis() - lastPinUpdate);
    }
    else if (not grindPinState && not pageUpPinState && not pageDownPinState && lastPageUpPin)
    {
      (LAST_PAGE == state.page) ? state.page = FIRST_PAGE : state.page++;
    }
    else if (not grindPinState && not pageUpPinState && not pageDownPinState && lastPageDownPin)
    {
      (FIRST_PAGE == state.page) ? state.page = LAST_PAGE : state.page--;
    }

    // set grinding pin
    digitalWrite(D8, grinding);
    lastPinUpdate = millis();
  }

  // Screen update
  if (millis() - lastScreenUpdate > SCREEN_INTERVAL)
  {
    // Screen redraw
    if (doScreenRedraw)
    {
      draw_page(currentPage, grinding, remainingGrindTime);
      #ifdef DEBUG
      Serial.print("Redraw interval: ");
      Serial.println(millis() - lastScreenRedraw);
      #endif
      lastScreenRedraw = millis();
      lastScreenUpdate = lastScreenRedraw;
      doScreenRedraw = false;
    }
    // Screen update
    else
    {
      update_page(currentPage, grinding, remainingGrindTime);
      #ifdef DEBUG
      Serial.print("Update interval: ");
      Serial.println(millis() - lastScreenUpdate);
      #endif
      lastScreenUpdate = millis();
    }

    #ifdef DEBUG
    Serial.print("Remaining grind time: ");
    Serial.println(remainingGrindTime);
    #endif
  }
}

