#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <M5Unified.h>
#include <M5GFX.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>


#define SD_SPI_CS_PIN 4
#define SD_SPI_SCK_PIN 18
#define SD_SPI_MISO_PIN 38
#define SD_SPI_MOSI_PIN 23

enum Screen {
  HOME_SCREEN,
  EXERCISE_SCREEN,
  CELEBRATION_SCREEN
};



Screen currentScreen = HOME_SCREEN;
int latestValue = 0;
int exerciseCount = 0;
unsigned long celebrationStartTime = 0;
bool catEyesOpen = true;
unsigned long lastBlinkTime = 0;
const unsigned long BLINK_INTERVAL = 1500;  // 1.5秒ごとにまばたきをする
bool needsBackgroundRedraw = false; // 背景の読み込みなおすかどうか
uint16_t bgColors[] = {
  M5.Lcd.color565(221,247,208),
  M5.Lcd.color565(181,215,190),
  M5.Lcd.color565(109,199,207),
  M5.Lcd.color565(197,227,253), // ← 277は255を超えるので修正しました
  M5.Lcd.color565(194,179,218),
  M5.Lcd.color565(205,179,218),
  M5.Lcd.color565(219,180,199),
  M5.Lcd.color565(238,175,175),
  M5.Lcd.color565(242,172,134),
  M5.Lcd.color565(255,248,178)
};
const int bgColorCount = sizeof(bgColors) / sizeof(bgColors[0]);
// 紙吹雪のパーティクル構造体
struct Confetti {
  int x, y;
  int prevX, prevY; // 1つ前のフレームを覚えておく変数
  int vx, vy;
  uint16_t color;
  bool active;
  bool wasActive; // １つ前のフレームでアクティブだったか覚えておく変数
};

Confetti confetti[30];

// =============================
//  ボタンの設定をする
// =============================
struct SimpleButton {
  int x, y, w, h;
  const char *label;
  uint16_t color;
  void (*callback)();
};

SimpleButton startButton;

// =============================
//  おめでとうの紙ふぶきをちらす設定
// =============================
void initConfetti() {
  needsBackgroundRedraw = true;
  for (int i = 0; i < 30; i++) {
    confetti[i].x = random(0, 320);
    confetti[i].y = random(-50, 0);
    confetti[i].prevX = confetti[i].x;
    confetti[i].prevY = confetti[i].y;
    confetti[i].vx = random(-2, 3);
    confetti[i].vy = random(1, 4);
    confetti[i].color = M5.Lcd.color565(random(100, 255), random(100, 255), random(100, 255));
    confetti[i].active = true;
    confetti[i].wasActive = false;
  }
}

// // 音声のMP3をつかう
// void playMP3(char *filename){
//   file = new AudioFileSourceSD(filename);
//   id3 = new AudioFileSourceID3(file);
//   out = new AudioOutputI2S(0, 1); // Output to builtInDAC
//   out->SetOutputModeMono(true);
//   out->SetGain(1.0);
//   mp3 = new AudioGeneratorMP3();
//   mp3->begin(id3, out);
//   while(mp3->isRunning()) {
//     if (!mp3->loop()) mp3->stop();
//   }
// }


void updateConfetti() {
  // 今は紙ふぶきを散らしているかどうかチェック
  bool hasActiveConfetti = false;
  for (int i = 0; i < 30; i++) {
    if (confetti[i].active) {
      hasActiveConfetti = true;
      break;
    }
  }
  
  // 紙ふぶきやっていないばあいはスルーする
  if (!hasActiveConfetti) {
    return;
  }

  for (int i = 0; i < 30; i++) {
    // １つ前のフレームでアクティブだった場合は古い位置をクリアする
    if (confetti[i].wasActive && !needsBackgroundRedraw) {
      // 背景を白にして画面をきれいにする
      M5.Lcd.fillRect(confetti[i].prevX, confetti[i].prevY, 4, 4, WHITE);
    }
    
    if (confetti[i].active) {
      // 位置を更新
      confetti[i].prevX = confetti[i].x;
      confetti[i].prevY = confetti[i].y;
      confetti[i].x += confetti[i].vx;
      confetti[i].y += confetti[i].vy;
      confetti[i].vy += 1; // 重力効果
      
      // 紙ふぶきが画面の外に出たら消す
      if (confetti[i].x < -10 || confetti[i].x > 330 || confetti[i].y > 250) {
        confetti[i].active = false;
      } else {
        // 消したぶん新しい位置に1つ増やす
        M5.Lcd.fillRect(confetti[i].x, confetti[i].y, 4, 4, confetti[i].color);
      }
    }
    
    // １つ前のフレームの状態をおぼえておく
    confetti[i].wasActive = confetti[i].active;
  }
}

// =============================
//  ボタンを表示するのと処理の設定
// =============================
void drawButton(SimpleButton &btn) {
  M5.Lcd.fillRoundRect(btn.x + 3, btn.y + 3, btn.w, btn.h, 12, BLACK);
  M5.Lcd.fillRoundRect(btn.x, btn.y, btn.w, btn.h, 12, btn.color);
  // M5.Lcd.drawRoundRect(btn.x, btn.y, btn.w, btn.h, 12, btn.color);
  M5.Lcd.loadFont(SD, "/genshin-regular-32pt.vlw");
  M5.Lcd.setTextColor(WHITE);
  M5.Lcd.setTextSize(1.5);

  // 文字幅を取得して中央寄せ
  int textWidth = M5.Lcd.textWidth(btn.label);
  int textHeight = 32;  // フォントサイズの高さ
  int tx = btn.x + (btn.w - textWidth) / 2;
  int ty = btn.y + (btn.h - textHeight) / 2 - textHeight/2;
  M5.Lcd.setCursor(tx, ty);
  M5.Lcd.print(btn.label);
}

void handleTouch(SimpleButton &btn) {
  if (M5.Touch.getCount() > 0) {
    auto tp = M5.Touch.getDetail(0);
    if (tp.x >= btn.x && tp.x <= btn.x + btn.w && tp.y >= btn.y && tp.y <= btn.y + btn.h) {
      if (btn.callback) btn.callback();
      delay(300);  // チャタリング防止
    }
  }
}

// =============================
//  各画面描画
// =============================
void drawHomeScreen() {
  M5.Lcd.fillScreen(WHITE);

  // 猫の画像
  int imgW = 250;
  int imgH = 250;
  int x = (M5.Lcd.width() - imgW) / 2;
  int y = (M5.Lcd.height() - imgH) / 2;

  M5.Display.drawPngFile(SD, "/YellowCatBig.png", x, y);

  // ボタン描画
  drawButton(startButton);
}

void backHome() {
  currentScreen = HOME_SCREEN;
  drawHomeScreen();
}

void drawExerciseScreen() {
  int colorIndex = exerciseCount;
  if (colorIndex >= bgColorCount) colorIndex = bgColorCount - 1;
  uint16_t bgColor = bgColors[colorIndex];

  M5.Lcd.fillScreen(bgColor);  // ← 背景を塗る
  

  // M5.Display.drawPngFile(SD, "/Legs_up.png", 170, 0);

  // startButton = { 10, 80, 300, 80, "途中で終わる", RED, backHome };
  // drawButton(startButton);


  M5.Lcd.setTextColor(BLACK);
  M5.Lcd.setTextSize(5);
  M5.Lcd.setCursor(107, 0);
  M5.Lcd.printf("%d", exerciseCount);

  M5.Lcd.setTextColor(BLACK);
  M5.Lcd.setTextSize(1.5);
  M5.Lcd.setCursor(80, 180);
  int remainingCount = 10-exerciseCount;
  M5.Lcd.printf("あと %d回", remainingCount);
}

void drawCelebrationScreen() {
  static bool backgroundDrawn = false;
  bool shouldRedrawBackground = false;
  
  // 初回の場合
  if (millis() - celebrationStartTime < 100) {
    shouldRedrawBackground = true;
    initConfetti();
  }
  
  // まばたき処理
  if (millis() - lastBlinkTime > BLINK_INTERVAL) {
    catEyesOpen = !catEyesOpen;
    lastBlinkTime = millis();
    shouldRedrawBackground = true;
  }
  
  // 背景を再描画する必要がある場合
  if (shouldRedrawBackground) {
    needsBackgroundRedraw = true;
    
    M5.Lcd.fillScreen(WHITE);
    int imgW = 150;
    int imgH = 150;
    int x = (M5.Lcd.width() - imgW) / 2;
    int y = (M5.Lcd.height() - imgH) / 2;
    if (catEyesOpen) {
      M5.Display.drawPngFile(SD, "/YellowCat2.png", x, y);
    } else {
      M5.Display.drawPngFile(SD, "/YellowCat.png", x, y);
    }
    M5.Lcd.setTextColor(M5.Lcd.color565(31, 117, 0));
    M5.Lcd.setTextSize(2);
    M5.Lcd.setCursor(60, 170);
    M5.Lcd.println("頑張ったね！");
    
    backgroundDrawn = true;
  }
  
  // 紙吹雪のアニメーション
  updateConfetti();
  
  // 背景再描画フラグをリセット
  needsBackgroundRedraw = false;
}

void playSound(const unsigned char* wavData, unsigned int wavLen) {
  // 簡単なWAV再生
  for (unsigned int i = 44; i < wavLen && i < wavLen; i++) {
    dacWrite(25, wavData[i]);
    delayMicroseconds(125); // 8kHz用
  }
  dacWrite(25, 0);
}

//音を鳴らしてみる
void playTone() {
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setCursor(0, 0);
  M5.Lcd.print("Volume : ");
  M5.Speaker.setVolume(0.1);
  M5.Speaker.tone(440, 200);
}

// =============================
//  ボタンコールバック
// =============================
void onStartButton() {
  currentScreen = EXERCISE_SCREEN;
  exerciseCount = 0;
  Serial.println("Exercise started. Count reset to 0.");
  drawExerciseScreen();
}

// =============================
//  運動カウント（受信ベース）
// =============================
void updateExerciseCount() {
  if (Serial2.available()) {
    int ch = Serial2.read();
    Serial.write(ch);  // エコーバック
    
    // デバッグ出力
    Serial.print("Received: ");
    Serial.print(ch);
    Serial.print(" (0x");
    Serial.print(ch, HEX);
    Serial.print(") ");
    
    // 文字として表示
    if (ch >= 32 && ch <= 126) {
      Serial.print("char: '");
      Serial.write(ch);
      Serial.print("' ");
    }
    
    // '1'を受信したらカウントアップ（連続受信対策なし版）
    if (ch == '1') {
      exerciseCount++;
      playTone();
      // playMP3("/1.mp3");
      Serial.print("-> COUNT UP! exerciseCount: ");
      Serial.println(exerciseCount);
      
      // 画面を即座に更新
      drawExerciseScreen();
      
      
      // 目標達成チェック
      if (exerciseCount >= 10) {
        currentScreen = CELEBRATION_SCREEN;
        celebrationStartTime = millis();
        Serial.println("Goal reached! Switching to celebration screen.");
        drawCelebrationScreen();
      }
    } else {
      Serial.println("(ignored - not '1')");
    }
  }
}

// =============================
//  setup / loop
// =============================
void setup() {
  M5.begin();
  Serial.begin(9600);
  Serial2.begin(9600, SERIAL_8N1, 32, 33);

  Serial.println("M5Stack Exercise Counter Starting...");
  Serial.println("Serial2 initialized on pins 32(RX), 33(TX)");

  randomSeed(analogRead(0));
  SPI.begin(SD_SPI_SCK_PIN, SD_SPI_MISO_PIN, SD_SPI_MOSI_PIN, SD_SPI_CS_PIN);
  if (!SD.begin(SD_SPI_CS_PIN, SPI, 25000000)) {
    M5.Display.print("\n SD card not detected\n");
    Serial.println("SD card initialization failed!");
    while (1)
      ;
  }
  
  Serial.println("SD card initialized successfully");

  // スタートボタン設定（横いっぱい、縦1/3）
  startButton = { 10, 80, 300, 80, "はじめる!", M5.Lcd.color565(31, 117, 0), onStartButton };

  Serial.println("Drawing home screen...");
  drawHomeScreen();
}

void loop() {
  M5.update();

  switch (currentScreen) {
    case HOME_SCREEN:
      handleTouch(startButton);
      if (M5.BtnA.wasPressed()) {
        Serial.println("Button A pressed - starting exercise");
        onStartButton();
      }
      break;

    case EXERCISE_SCREEN:
      updateExerciseCount();
      if (M5.BtnB.wasPressed()) {
        Serial.println("Button B pressed - returning to home");
        currentScreen = HOME_SCREEN;
        drawHomeScreen();
      }
      break;

    case CELEBRATION_SCREEN:
      drawCelebrationScreen();
      if (millis() - celebrationStartTime > 5000 || 
          M5.BtnA.wasPressed() || M5.BtnB.wasPressed() || M5.BtnC.wasPressed()) {
        Serial.println("Returning to home screen from celebration");
        currentScreen = HOME_SCREEN;
        drawHomeScreen();
      }
      break;
  }

  // PCからのデータをMicrobitへ転送
  if (Serial.available()) {
    int ch = Serial.read();
    Serial2.write(ch);
  }

  delay(50);
}