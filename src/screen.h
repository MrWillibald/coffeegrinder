#include <Arduino.h>
#include <U8g2lib.h>

void init_screen();

void update_page(int page, boolean grindActive, unsigned long time);

void draw_page(int page, boolean grinding, unsigned long time);