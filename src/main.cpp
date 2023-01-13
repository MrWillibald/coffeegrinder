#include "screen.h"

// Debug flag
#define DEBUG

// Grinding times in ms
#define SINGLE_SHOT_TIME 10000
#define DOUBLE_SHOT_TIME 11100

// Update intervalls in ms
#define PIN_INTERVAL 10
#define SCREEN_INTERVAL 80

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
unsigned long update_remaining_grind_time(boolean grindActive, unsigned long lastUpdate, unsigned long grindTime)
{
  if (grindActive)
  {
    grindTime = grindTime - (millis() - lastUpdate);
  }
  return grindTime;
}

void setup(void) {
  // pin init
  pinMode(D5, INPUT_PULLUP); // grinding request
  pinMode(D6, INPUT_PULLUP); // page up
  pinMode(D7, INPUT_PULLUP); // page down
  pinMode(D3, OUTPUT);  // grinding output

  // init display
  init_screen();

  // serial init
  #ifdef DEBUG
  Serial.begin(115200);
  Serial.println();
  Serial.println("Setup completed ...");
  #endif

  delay(3000);

  // first page
  currentPage = 0;
  grinding = false;
  remainingGrindTime = SINGLE_SHOT_TIME;
  draw_page(currentPage, grinding, remainingGrindTime);
  lastScreenRedraw = millis();
  lastScreenUpdate = lastScreenRedraw;
  lastPinUpdate = millis();
}

void loop(void) {
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
    remainingGrindTime = update_remaining_grind_time(grinding, lastPinUpdate, remainingGrindTime);
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
    // set grinding pin
    digitalWrite(D3, grinding);
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

