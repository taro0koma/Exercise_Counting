#include "VButton.h"

VButton::VButton(const char* title,
                 void (*callback)(const char*),
                 int y,
                 uint16_t color) {
  this->title = title;
  this->callback = callback;
  this->y = y;
  this->color = color;

  w = 150;
  h = 50;
  cx = M5.Lcd.width() / 2;
  cy = y;

  draw(false);
}

void VButton::draw(bool pressed) {
  uint16_t bg = pressed ? TFT_WHITE : color;
  uint16_t textcol = pressed ? color : TFT_WHITE;

  M5.Lcd.fillRoundRect(cx - w / 2, cy - h / 2, w, h, 10, bg);
  M5.Lcd.drawRoundRect(cx - w / 2, cy - h / 2, w, h, 10, color);

  M5.Lcd.setTextDatum(MC_DATUM);
  M5.Lcd.setTextColor(textcol);
  M5.Lcd.setTextSize(2);
  M5.Lcd.drawString(title, cx, cy);
}

void VButton::loop() {
  if (M5.Touch.getCount() > 0) {
    auto t = M5.Touch.getDetail();

    if (t.x > cx - w / 2 && t.x < cx + w / 2 &&
        t.y > cy - h / 2 && t.y < cy + h / 2) {
      draw(true);   // 押されている描画
      delay(120);
      draw(false);  // 離したら元に戻す
      if (callback) callback(title);
    }
  }
}
