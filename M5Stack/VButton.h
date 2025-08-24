#ifndef VButton_h
#define VButton_h

#include <Arduino.h>
#include <M5Unified.h>

class VButton {
  public:
    VButton(const char* title,
            void (*callback)(const char*),   // ボタンが押されたら呼ばれる
            int y,
            uint16_t color = RED);

    void draw(bool pressed = false);  // ボタンを描画
    void loop();                      // 押されたかチェック＋必要ならコールバック

  private:
    const char* title;
    void (*callback)(const char*);
    int y;
    uint16_t color;
    int w, h;
    int cx, cy;   // 中心座標
};

#endif
