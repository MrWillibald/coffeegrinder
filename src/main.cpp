#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <EEPROM.h>
#include "screen.h"
#include "credentials.h"


// Debug flag
//#define MAN_DEBUG
#ifdef MAN_DEBUG
#define DEBUG
#endif

// WiFi credentials
const char *ssid = STASSID;
const char *password = STAPSK;

// Grinding times in ms
#define SINGLE_SHOT_TIME 6300
#define DOUBLE_SHOT_TIME 9300
typedef struct
{
  unsigned long singleShot = SINGLE_SHOT_TIME;
  unsigned long doubleShout = DOUBLE_SHOT_TIME;
} TShotTimes;
TShotTimes times;

// Update intervalls in ms
#define PIN_INTERVAL 10
#define SCREEN_INTERVAL 80

// define first and last page
#define FIRST_PAGE 0
#define LAST_PAGE 1

// current state
typedef struct
{
  // program state
  boolean grinding = false;
  boolean programming = false;
  byte page = FIRST_PAGE;
  unsigned long time = 0;
  boolean redrawScreen = false;
  boolean updateScreen = false;
  // update timestamps
  unsigned long lastPinUpdate = 0;
  unsigned long lastScreenUpdate = 0;
  unsigned long lastScreenRedraw = 0;
  // input pin states
  boolean lastGrindPin = false;
  boolean lastPageUpPin = false;
  boolean lastPageDownPin = false;
} TState;
TState state;

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

  ArduinoOTA.onStart([]()
                     {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else {  // U_FS
      type = "filesystem";
    }

    // NOTE: if updating FS this would be the place to unmount FS using FS.end()
    Serial.println("Start updating " + type); });
  ArduinoOTA.onEnd([]()
                   { Serial.println("\nEnd"); });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total)
                        { Serial.printf("Progress: %u%%\r", (progress / (total / 100))); });
  ArduinoOTA.onError([](ota_error_t error)
                     {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) {
      Serial.println("Auth Failed");
    } else if (error == OTA_BEGIN_ERROR) {
      Serial.println("Begin Failed");
    } else if (error == OTA_CONNECT_ERROR) {
      Serial.println("Connect Failed");
    } else if (error == OTA_RECEIVE_ERROR) {
      Serial.println("Receive Failed");      state.updateScreen = false;
    } else if (error == OTA_END_ERROR) {
      Serial.println("End Failed");
    } });
  ArduinoOTA.begin();
}

// get grinding time for current page
unsigned long get_grind_time(int page)
{
  unsigned long grindTime = 0;
  switch (page)
  {
  case FIRST_PAGE:
    grindTime = times.singleShot;
    break;
  case LAST_PAGE:
    grindTime = times.doubleShout;
    break;
  default:
    break;
  }
  return grindTime;
}

void setup(void)
{
  // serial init
  Serial.begin(115200);
  Serial.println();
  Serial.println("Booting ...");

#ifdef DEBUG
  // put times to EEPROM
  EEPROM.begin(16);
  EEPROM.put(0, times);
  EEPROM.commit();
  EEPROM.end();
#endif

  // get times from EEPROM
  EEPROM.begin(16);
  EEPROM.get(0, times);
  EEPROM.commit();
  EEPROM.end();

  // pin init
  pinMode(D5, INPUT_PULLUP); // grinding request
  pinMode(D6, INPUT_PULLUP); // page up
  pinMode(D7, INPUT_PULLUP); // page down
  pinMode(D8, OUTPUT);       // grinding output

  // init display
  init_screen();

  // start WIFI and connect
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.waitForConnectResult() != WL_CONNECTED)
  {
    Serial.println("Connection Failed! Rebooting...");
    delay(5000);
    ESP.restart();
  }

  // start OTA
  OTA_init();

  while (3000 > millis())
  {
  };

  // first page
  state.page = FIRST_PAGE;
  state.time = times.singleShot;
  draw_page(state.page, state.grinding, state.time);
  unsigned long now = millis();
  state.lastScreenRedraw = now;
  state.lastScreenUpdate = now;
  state.lastPinUpdate = now;

  // summary
  Serial.println("Ready");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

void loop(void)
{
  // handle OTA
  ArduinoOTA.handle();

  // Pin update
  if (millis() - state.lastPinUpdate > PIN_INTERVAL)
  {
    // grind Pin D5
    boolean grindPinState = not digitalRead(D5);
    // page up pin D6
    boolean pageUpPinState = not digitalRead(D6);
    // page down pin D7
    boolean pageDownPinState = not digitalRead(D7);

    // update state diagram
    if ((grindPinState && not state.lastGrindPin && pageUpPinState && not pageDownPinState) ||
        (grindPinState && not state.lastGrindPin && not pageUpPinState && pageDownPinState))
    {
      // inititate programming mode
      state.programming = true;
      state.time = 0;
    }
    else if (state.programming && not grindPinState && not pageUpPinState && not pageDownPinState && (state.lastPageUpPin || state.lastPageDownPin))
    {
      // end programming mode
      state.programming = false;
      switch (state.page)
      {
      case FIRST_PAGE:
        times.singleShot = state.time;
        break;
      case LAST_PAGE:
        times.doubleShout = state.time;
        break;
      default:
        break;
      }
      // write times to EEPROM
      EEPROM.begin(16);
      EEPROM.put(0, times);
      EEPROM.commit();
      EEPROM.end();

      // redraw screen
      state.redrawScreen = true;
    }
    else if (state.programming && grindPinState)
    {
      // program grinding time with counting up
      state.grinding = true;
      state.time = state.time + (millis() - state.lastPinUpdate);
      state.updateScreen = true;
    }
    else if (state.programming && not grindPinState)
    {
      // still in programming but no grinding
      state.grinding = false;
    }
    else if (grindPinState && (0 < state.time) && not state.programming && not pageUpPinState && not pageDownPinState)
    {
      // simple grinding with time counting down
      state.grinding = true;
      state.time = state.time - (millis() - state.lastPinUpdate);
      state.updateScreen = true;
      if (100000 < state.time)
      {
        state.time = 0;
        state.grinding = false;
        state.updateScreen = false;
        state.redrawScreen = true;
      }
    }
    else if (not state.grinding && not grindPinState && not pageUpPinState && not pageDownPinState && state.lastPageUpPin)
    {
      // page up
      (LAST_PAGE == state.page) ? state.page = FIRST_PAGE : state.page++;
      state.time = get_grind_time(state.page);
      state.redrawScreen = true;
    }
    else if (not state.grinding && not grindPinState && not pageUpPinState && not pageDownPinState && state.lastPageDownPin)
    {
      // page down
      (FIRST_PAGE == state.page) ? state.page = LAST_PAGE : state.page--;
      state.time = get_grind_time(state.page);
      state.redrawScreen = true;
    }
    else if (not grindPinState && state.lastGrindPin && (0 == state.time) && not state.programming && not pageUpPinState && not pageDownPinState)
    {
      state.time = get_grind_time(state.page);
      state.redrawScreen = true;
    }
    else
    {
      state.grinding = false;
      state.programming = false;
    }

    // update last pin status
    state.lastGrindPin = grindPinState;
    state.lastPageUpPin = pageUpPinState;
    state.lastPageDownPin = pageDownPinState;

    // set grinding pin
    digitalWrite(D8, state.grinding);
    state.lastPinUpdate = millis();
  }

  // Screen update
  if (millis() - state.lastScreenUpdate > SCREEN_INTERVAL)
  {
    // Screen redraw
    if (state.redrawScreen)
    {
      draw_page(state.page, state.grinding, state.time);
#ifdef DEBUG
      Serial.print("Redraw interval: ");
      Serial.println(millis() - state.lastScreenRedraw);
#endif
      state.lastScreenRedraw = millis();
      state.lastScreenUpdate = state.lastScreenRedraw;
      state.redrawScreen = false;
      state.updateScreen = false;
    }
    // Screen update
    else
    {
      update_page(state.page, state.grinding, state.time);
#ifdef DEBUG
      Serial.print("Update interval: ");
      Serial.println(millis() - state.lastScreenRedraw);
#endif
      state.lastScreenUpdate = millis();
      state.updateScreen = false;
    }

#ifdef DEBUG
    Serial.print("Current grind time: ");
    Serial.println(state.time);
#endif
  }
}
