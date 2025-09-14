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
  PREPARATION_SCREEN,  // 新しく追加した準備画面
  EXERCISE_SCREEN,
  CELEBRATION_SCREEN
};

Screen currentScreen = HOME_SCREEN;
int latestValue = 0;
int exerciseCount = 0;
unsigned long celebrationStartTime = 0;
unsigned long preparationStartTime = 0;  // 準備画面の開始時間
unsigned long exerciseStartTime = 0;     // 運動開始時間（新規追加）
bool catEyesOpen = true;
unsigned long lastBlinkTime = 0;
const unsigned long BLINK_INTERVAL = 1500;  // 1.5秒ごとにまばたきをする
bool needsBackgroundRedraw = false; // 背景の読み込みなおすかどうか
bool randomSeedInitialized = false;  // randomSeedが初期化されたかを管理

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
//  音声ファイル再生（ノイズ対策済み）
// =============================
void playWavFile(const char* path) {
    // 音声再生前にSpeakerを一度停止してクリアな再生を保証
    M5.Speaker.stop();
    delay(10);
    
    File file = SD.open(path);
    if (!file) {
        M5.Display.println(String("Failed to open: ") + path);
        return;
    }

    size_t fileSize = file.size();
    uint8_t* buf = (uint8_t*)malloc(fileSize);
    if (!buf) {
        M5.Display.println("malloc failed!");
        file.close();
        return;
    }

    file.read(buf, fileSize);
    file.close();

    M5.Speaker.playWav(buf, fileSize);

    while (M5.Speaker.isPlaying()) {
        delay(1);
        M5.update();  // M5.update()を呼び続けることで他の処理を妨げない
    }

    free(buf);
    delay(100);  // 音声終了後に少し待機してノイズを防ぐ
}

// =============================
//  おめでとうの紙ふぶきをちらす設定（ノイズ対策済み）
// =============================
void initConfetti() {
  needsBackgroundRedraw = true;
  
  // randomSeedは一度だけ初期化（ノイズ対策）
  if (!randomSeedInitialized) {
    randomSeed(analogRead(0));
    randomSeedInitialized = true;
    delay(50);  // randomSeed後に少し待機
  }
  
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

void drawPreparationScreen() {
  M5.Lcd.fillScreen(M5.Lcd.color565(255, 248, 178));  // 淡い黄色の背景
  
  // 猫の画像を小さめに表示
  int imgW = 120;
  int imgH = 120;
  int x = (M5.Lcd.width() - imgW) / 2;
  int y = 30;

  M5.Display.drawPngFile(SD, "/YellowCat2.png", x, y);

  // メッセージを表示
  M5.Lcd.setTextColor(M5.Lcd.color565(31, 117, 0));
  M5.Lcd.setTextSize(1.3);
  M5.Lcd.setCursor(46, 144);
  M5.Lcd.println("ゆっくり大きく");
  M5.Lcd.setCursor(60, 180);
  M5.Lcd.println("運動してね！");
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

// =============================
//  ボタンコールバック（修正版）
// =============================
void onStartButton() {
  // 準備画面に移行
  currentScreen = PREPARATION_SCREEN;
  preparationStartTime = millis();
  Serial.println("Moving to preparation screen");
  drawPreparationScreen();
  
  // start.wavを再生（ノイズ対策済み）
  playWavFile("/start.wav");
  
  // 音声再生後、運動画面に移行
  currentScreen = EXERCISE_SCREEN;
  exerciseCount = 0;
  exerciseStartTime = millis();  // 運動開始時間を記録
  Serial.println("Exercise started. Count reset to 0.");
  Serial.println("Counting will start after 1 second delay.");
  drawExerciseScreen();
}

// =============================
//  運動カウント（受信ベース・0.8秒遅延対応）
// =============================
void updateExerciseCount() {
  // 運動開始から0.8秒経過していない場合は何もしない
  if (millis() - exerciseStartTime < 800) {
    // シリアルバッファをクリアして開始直後のデータを破棄
    while (Serial2.available()) {
      Serial2.read();
    }
    return;
  }
  
  if (Serial2.available()) {
    int ch = Serial2.read();
    
    // '1'を受信したらカウントアップ（連続受信対策なし版）
    if (ch == '1') {
      exerciseCount++;
      char wavFilePath[20];
      sprintf(wavFilePath, "/%d.wav", exerciseCount);
      playWavFile(wavFilePath);
      
      // 画面を即座に更新
      drawExerciseScreen();
      
      // 目標達成チェック
      if (exerciseCount >= 10) {
        currentScreen = CELEBRATION_SCREEN;
        celebrationStartTime = millis();
        Serial.println("Goal reached! Switching to celebration screen.");
        drawCelebrationScreen();
      }
    }
  }
}

// =============================
//  setup / loop
// =============================
void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  Serial.begin(9600);
  Serial2.begin(9600, SERIAL_8N1, 32, 33);

  Serial.println("M5Stack Exercise Counter Starting...");
  Serial.println("Serial2 initialized on pins 32(RX), 33(TX)");

  SPI.begin(SD_SPI_SCK_PIN, SD_SPI_MISO_PIN, SD_SPI_MOSI_PIN, SD_SPI_CS_PIN);
  auto spk_cfg = M5.Speaker.config();
  M5.Speaker.config(spk_cfg);
  M5.Speaker.begin();
  M5.Speaker.setVolume(255);

  // SDカード初期化
  if (!SD.begin(GPIO_NUM_4, SPI, 25000000)) {
      M5.Display.println("SD init failed!");
      return;
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

    case PREPARATION_SCREEN:
      // 準備画面では特に何もしない（音声再生中）
      // 音声が終了したらonStartButton内で自動的にEXERCISE_SCREENに移行
      if (M5.BtnB.wasPressed()) {
        Serial.println("Button B pressed - returning to home from preparation");
        currentScreen = HOME_SCREEN;
        M5.Speaker.stop();  // 音声を停止
        drawHomeScreen();
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