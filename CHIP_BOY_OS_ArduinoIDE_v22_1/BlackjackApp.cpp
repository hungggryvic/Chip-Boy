// ==============================
// BLACKJACK — CHIP-BOY PORT
// Display: SSD1306 (I2C D0/D1, addr 0x3C)
// Buttons: MCP23008 (BTN1/2/3 + nav switch)
// Pause:   Encoder switch D6 (same as IronTides)
//
// Controls in game:
//   BTN1  (GP0) = HIT  /  decrease bet (on deal screen)
//   BTN2  (GP6) = DEAL
//   BTN3  (GP1) = STAY /  increase bet (on deal screen)
//   ENC SW      = PAUSE
//
// Betting:
//   Denominations: $1 $2 $5 $10 $20 $50 $100
//   BTN1 = step bet down, BTN3 = step bet up (on deal/splash screen)
//   Bank starts at $0 and persists until power-off.
//   Win = +bet, Lose/Bust = -bet, Tie = no change.
//
// Pause menu:
//   BTN1 = PLAY (resume)
//   BTN3 = EXIT (back to games menu)
// ==============================

#include "BlackjackApp.h"
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ============================================================================
// OS APP WRAPPER STATE
// ============================================================================
static bool bj_active    = false;
static bool bj_setupRan  = false;

bool blackjackIsActive() { return bj_active; }

extern void gameExitToMenu();

static void blackjackSetup_Internal();
static void blackjackLoop_Internal();

// ============================================================================
// SHARED HARDWARE
// ============================================================================
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT  64
#define OLED_ADDR     0x3C

extern Adafruit_SSD1306 display;
extern bool displayReady;

// ============================================================================
// MCP23008
// ============================================================================
#define BJ_MCP_ADDR  0x20
#define BJ_MCP_IODIR 0x00
#define BJ_MCP_GPIO  0x09
#define BJ_MCP_GPPU  0x06

static uint8_t bj_mcp_gpio = 0xFF;

static bool bjMcpWrite(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(BJ_MCP_ADDR);
  Wire.write(reg); Wire.write(val);
  return Wire.endTransmission() == 0;
}
static bool bjMcpRead(uint8_t &val) {
  Wire.beginTransmission(BJ_MCP_ADDR);
  Wire.write(BJ_MCP_GPIO);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((int)BJ_MCP_ADDR, 1) != 1) return false;
  val = Wire.read();
  return true;
}

// ============================================================================
// BUTTON DEBOUNCE
// ============================================================================
struct BjBtnState {
  uint8_t pin;
  bool lastRead    = HIGH;
  bool stable      = HIGH;
  unsigned long lastChange = 0;
};

static const unsigned long BJ_DEBOUNCE_MS = 25;

static inline bool bjMcpBit(uint8_t bit) {
  return (bj_mcp_gpio & (1u << bit)) ? HIGH : LOW;
}
static void bjBtnUpdate(BjBtnState &b) {
  bool r = bjMcpBit(b.pin);
  if (r != b.lastRead) { b.lastRead = r; b.lastChange = millis(); }
  if ((millis() - b.lastChange) >= BJ_DEBOUNCE_MS) b.stable = b.lastRead;
}
static inline bool bjBtnDown(const BjBtnState &b) { return b.stable == LOW; }

static BjBtnState bj_b1   { .pin = 0 };
static BjBtnState bj_b2   { .pin = 6 };
static BjBtnState bj_b3   { .pin = 1 };

// ============================================================================
// ENCODER / BUZZER PINS
// ============================================================================
//#ifndef ENC_SW_PIN
//#define ENC_SW_PIN D6
//#endif
#ifndef BUZZER_PIN
#define BUZZER_PIN D2
#endif

// ============================================================================
// PAUSE STATE
// ============================================================================
static bool bj_paused             = false;
static bool bj_pauseNeedsRelease  = false;
static unsigned long bj_pauseIgnoreUntil = 0;
static bool bj_pauseBtn1WasDown   = false;
static bool bj_pauseBtn3WasDown   = false;

// ============================================================================
// BETTING
// ============================================================================
static const int BJ_DENOMS[]     = { 1, 2, 5, 10, 20, 50, 100 };
static const int BJ_NUM_DENOMS   = 7;
static int  bj_betIndex          = 0;    // index into BJ_DENOMS, resets to 0 each enter
static long bj_bank              = 0;    // persists until power-off (static = RAM only)

static inline int bjCurrentBet() { return BJ_DENOMS[bj_betIndex]; }

static void bjBetDecrease() {
  if (bj_betIndex > 0) {
    bj_betIndex--;
    tone(BUZZER_PIN, 600, 25);
  }
}
static void bjBetIncrease() {
  if (bj_betIndex < BJ_NUM_DENOMS - 1) {
    bj_betIndex++;
    tone(BUZZER_PIN, 900, 25);
  }
}

// ============================================================================
// SUIT SPRITES  5×5
// ============================================================================
static const uint8_t kClub[5][5] PROGMEM = {
  { 0,1,1,1,0 },
  { 1,1,0,1,1 },
  { 0,1,1,1,0 },
  { 0,0,1,0,0 },
  { 0,1,1,1,0 },
};
static const uint8_t kDiamond[5][5] PROGMEM = {
  { 0,0,1,0,0 },
  { 0,1,1,1,0 },
  { 1,1,1,1,1 },
  { 0,1,1,1,0 },
  { 0,0,1,0,0 },
};
static const uint8_t kSpade[5][5] PROGMEM = {
  { 0,0,1,0,0 },
  { 0,1,1,1,0 },
  { 1,1,1,1,1 },
  { 0,0,1,0,0 },
  { 0,1,1,1,0 },
};
static const uint8_t kHeart[5][5] PROGMEM = {
  { 0,1,0,1,0 },
  { 1,1,1,1,1 },
  { 1,1,1,1,1 },
  { 0,1,1,1,0 },
  { 0,0,1,0,0 },
};

// ============================================================================
// DECK / CARD DATA
// ============================================================================
#define BJ_CARD_W   14
#define BJ_CARD_H   19
#define BJ_CARD_GAP  2
#define BJ_MAX_HAND  7

#define BJ_DEALER_Y   1
#define BJ_PLAYER_Y  (SCREEN_HEIGHT - BJ_CARD_H - 10)

struct BjCard { uint8_t value; uint8_t suit; };

static BjCard bj_deck[52];
static uint8_t bj_deckTop;

static BjCard bj_playerHand[BJ_MAX_HAND];
static uint8_t bj_playerCount;

static BjCard bj_dealerHand[BJ_MAX_HAND];
static uint8_t bj_dealerCount;

static bool bj_playerBust;
static bool bj_dealerBust;

// ============================================================================
// GAME STATE
// ============================================================================
enum BjState {
  BJS_SPLASH,
  BJS_BETWEEN_ROUNDS,
  BJS_PLAYER_TURN,
  BJS_RESULT
};
static BjState bj_state;

static bool bj_b1Prev = HIGH;
static bool bj_b2Prev = HIGH;
static bool bj_b3Prev = HIGH;

static inline bool bj_justPressed(BjBtnState &b, bool &prev) {
  bool cur = b.stable;
  bool edge = (prev == HIGH && cur == LOW);
  prev = cur;
  return edge;
}

// ============================================================================
// RNG
// ============================================================================
static uint32_t bj_rng = 12345;
static uint32_t bjRand() {
  bj_rng = bj_rng * 1664525UL + 1013904223UL;
  return bj_rng;
}

// ============================================================================
// DECK OPERATIONS
// ============================================================================
static void bjBuildDeck() {
  uint8_t i = 0;
  for (uint8_t s = 0; s < 4; s++)
    for (uint8_t v = 1; v <= 13; v++) {
      bj_deck[i].value = v; bj_deck[i].suit = s; i++;
    }
}
static void bjShuffle() {
  bj_rng ^= (uint32_t)millis();
  for (int i = 51; i > 0; i--) {
    int j = bjRand() % (i + 1);
    BjCard tmp = bj_deck[i]; bj_deck[i] = bj_deck[j]; bj_deck[j] = tmp;
  }
  bj_deckTop = 0;
}
static BjCard bjDeal() {
  if (bj_deckTop >= 52) { bjBuildDeck(); bjShuffle(); }
  return bj_deck[bj_deckTop++];
}
static int bjHandValue(BjCard* hand, uint8_t count) {
  int total = 0, aces = 0;
  for (int i = 0; i < count; i++) {
    int v = hand[i].value;
    if (v == 1)        { aces++; total += 11; }
    else if (v >= 10)    total += 10;
    else                 total += v;
  }
  while (total > 21 && aces > 0) { total -= 10; aces--; }
  return total;
}

// ============================================================================
// DRAW HELPERS
// ============================================================================
static void bjDrawSuit(uint8_t suit, int ox, int oy) {
  const uint8_t (*s)[5];
  switch (suit) {
    case 0:  s = kClub;    break;
    case 1:  s = kDiamond; break;
    case 2:  s = kSpade;   break;
    default: s = kHeart;   break;
  }
  for (int r = 0; r < 5; r++)
    for (int c = 0; c < 5; c++)
      if (pgm_read_byte(&s[r][c]))
        display.drawPixel(ox + c, oy + r, SSD1306_WHITE);
}

static void bjDrawLabel(uint8_t value, int sx, int sy) {
  char buf[3];
  uint8_t len;
  switch (value) {
    case  1: buf[0]='A'; buf[1]=0; len=1; break;
    case 11: buf[0]='J'; buf[1]=0; len=1; break;
    case 12: buf[0]='Q'; buf[1]=0; len=1; break;
    case 13: buf[0]='K'; buf[1]=0; len=1; break;
    case 10: buf[0]='1'; buf[1]='0'; buf[2]=0; len=2; break;
    default: buf[0]='0'+value; buf[1]=0; len=1; break;
  }
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  int tx = sx + (BJ_CARD_W - len * 6) / 2;
  int ty = sy + (BJ_CARD_H - 7) / 2;
  display.setCursor(tx, ty);
  display.print(buf);
}

static void bjDrawCard(BjCard c, int sx, int sy, bool faceDown) {
  display.drawRect(sx, sy, BJ_CARD_W, BJ_CARD_H, SSD1306_WHITE);
  if (faceDown) {
    for (int i = 2; i < BJ_CARD_H - 2; i += 2)
      display.drawFastHLine(sx + 2, sy + i, BJ_CARD_W - 4, SSD1306_WHITE);
    return;
  }
  bjDrawSuit(c.suit, sx + 1, sy + 1);
  bjDrawSuit(c.suit, sx + BJ_CARD_W - 6, sy + BJ_CARD_H - 6);
  bjDrawLabel(c.value, sx, sy);
}

static void bjDrawHand(BjCard* hand, uint8_t count, int y, bool showFirst) {
  uint8_t maxShow = 5;
  uint8_t start = (count > maxShow) ? count - maxShow : 0;
  int x = 1;
  for (uint8_t i = start; i < count; i++) {
    bjDrawCard(hand[i], x, y, (!showFirst && i == 0));
    x += BJ_CARD_W + BJ_CARD_GAP;
  }
}

static void bjDrawScore(int val, int x, int y) {
  display.setTextSize(1); display.setTextColor(SSD1306_WHITE);
  display.setCursor(x, y); display.print(val);
}

static void bjPrintCentered(const char* msg, int y) {
  int len = strlen(msg);
  int x   = (SCREEN_WIDTH - len * 6) / 2;
  if (x < 0) x = 0;
  display.setTextSize(1); display.setTextColor(SSD1306_WHITE);
  display.setCursor(x, y); display.print(msg);
}

// ── Draw the bottom HUD bar ──────────────────────────────────────────────────
// mode: "game"  → HIT left, bank centre, STAY right
//       "bet"   → -$X left, DEAL centre, +$X right  (splash + between rounds)
//       "result"→ plain centre label only, no bet controls
static void bjDrawHUD(bool showHitStay, const char* centreLabel, bool showBet = false) {
  int y = SCREEN_HEIGHT - 7;
  display.setTextSize(1); display.setTextColor(SSD1306_WHITE);

  if (showHitStay) {
    // Gameplay: HIT left, bank centre, STAY right
    display.setCursor(0, y);
    display.print("HIT");
    display.setCursor(SCREEN_WIDTH - 24, y);
    display.print("STAY");
    char bankBuf[12];
    snprintf(bankBuf, sizeof(bankBuf), "$%ld", bj_bank);
    int bw = strlen(bankBuf) * 6;
    display.setCursor((SCREEN_WIDTH - bw) / 2, y);
    display.print(bankBuf);
    return;
  }

  if (centreLabel) {
    if (showBet) {
      // Bet-adjust screen: "- $X"  DEAL  "+ $X"
      char decBuf[10], incBuf[10];
      snprintf(decBuf, sizeof(decBuf), "- $%d", bjCurrentBet());
      snprintf(incBuf, sizeof(incBuf), "+ $%d", bjCurrentBet());

      display.setCursor(0, y);
      display.print(decBuf);

      int rw = strlen(incBuf) * 6;
      display.setCursor(SCREEN_WIDTH - rw, y);
      display.print(incBuf);

      int len = strlen(centreLabel);
      int cx  = (SCREEN_WIDTH - len * 6) / 2;
      display.setCursor(cx, y);
      display.print(centreLabel);
    } else {
      // Result screen: centre label only, no bet controls
      int len = strlen(centreLabel);
      int cx  = (SCREEN_WIDTH - len * 6) / 2;
      display.setCursor(cx, y);
      display.print(centreLabel);
    }
  }
}



// ============================================================================
// DEAL ANIMATION HELPER
// ============================================================================
static void bjRenderDealInProgress() {
  display.clearDisplay();
  if (bj_dealerCount > 0) bjDrawHand(bj_dealerHand, bj_dealerCount, BJ_DEALER_Y, false);
  if (bj_playerCount > 0) bjDrawHand(bj_playerHand, bj_playerCount, BJ_PLAYER_Y, true);
  display.drawFastHLine(0, BJ_DEALER_Y + BJ_CARD_H + 1, SCREEN_WIDTH, SSD1306_WHITE);
  display.display();
}

static void bjDealHands() {
  bj_playerCount = bj_dealerCount = 0;
  bj_playerBust  = bj_dealerBust  = false;

  bj_playerHand[bj_playerCount++] = bjDeal();
  tone(BUZZER_PIN, 300, 40); bjRenderDealInProgress(); delay(1000);

  bj_dealerHand[bj_dealerCount++] = bjDeal();
  tone(BUZZER_PIN, 300, 40); bjRenderDealInProgress(); delay(1000);

  bj_playerHand[bj_playerCount++] = bjDeal();
  tone(BUZZER_PIN, 300, 40); bjRenderDealInProgress(); delay(1000);

  bj_dealerHand[bj_dealerCount++] = bjDeal();
  tone(BUZZER_PIN, 300, 40); bjRenderDealInProgress(); delay(600);

  delay(400);
  bj_state = BJS_PLAYER_TURN;
}

// ============================================================================
// SCREEN RENDERS
// ============================================================================
static void bjRenderSplash() {
  display.clearDisplay();
  bjPrintCentered("BLACKJACK", 4);

  int cardY    = 16;
  int cardSpan = 3 * BJ_CARD_W + 2 * BJ_CARD_GAP;
  int startX   = (SCREEN_WIDTH - cardSpan) / 2;
  for (int i = 0; i < 3; i++) {
    int cx = startX + i * (BJ_CARD_W + BJ_CARD_GAP);
    display.drawRect(cx, cardY, BJ_CARD_W, BJ_CARD_H, SSD1306_WHITE);
    for (int r = 2; r < BJ_CARD_H - 2; r += 2)
      display.drawFastHLine(cx + 2, cardY + r, BJ_CARD_W - 4, SSD1306_WHITE);
  }

  bjDrawHUD(false, "DEAL", true);
  display.display();

  // Startup jingle
  tone(BUZZER_PIN, 523,  80); delay(100);
  tone(BUZZER_PIN, 659,  80); delay(100);
  tone(BUZZER_PIN, 784,  80); delay(100);
  tone(BUZZER_PIN, 1047, 150); delay(170);
}

static void bjRenderBetweenRounds() {
  display.clearDisplay();
  display.drawRect(1, BJ_DEALER_Y, BJ_CARD_W, BJ_CARD_H, SSD1306_WHITE);
  display.drawRect(1, BJ_PLAYER_Y, BJ_CARD_W, BJ_CARD_H, SSD1306_WHITE);
  bjPrintCentered("DEALER", BJ_DEALER_Y + 6);
  bjPrintCentered("PLAYER", BJ_PLAYER_Y + 6);
  display.drawFastHLine(0, BJ_DEALER_Y + BJ_CARD_H + 1, SCREEN_WIDTH, SSD1306_WHITE);
  bjDrawHUD(false, "DEAL", true);
  display.display();
}

static void bjRenderPlayerTurn() {
  display.clearDisplay();
  bjDrawHand(bj_dealerHand, bj_dealerCount, BJ_DEALER_Y, false);
  bjDrawHand(bj_playerHand, bj_playerCount, BJ_PLAYER_Y, true);
  BjCard visCard[1] = { bj_dealerHand[1] };
  bjDrawScore(bjHandValue(visCard, 1), SCREEN_WIDTH - 14, BJ_DEALER_Y);
  bjDrawScore(bjHandValue(bj_playerHand, bj_playerCount), SCREEN_WIDTH - 14, BJ_PLAYER_Y);
  display.drawFastHLine(0, BJ_DEALER_Y + BJ_CARD_H + 1, SCREEN_WIDTH, SSD1306_WHITE);
  bjDrawHUD(true, NULL);
  display.display();
}

static void bjRenderDealerReveal() {
  display.clearDisplay();
  bjDrawHand(bj_dealerHand, bj_dealerCount, BJ_DEALER_Y, true);
  bjDrawHand(bj_playerHand, bj_playerCount, BJ_PLAYER_Y, true);
  bjDrawScore(bjHandValue(bj_dealerHand, bj_dealerCount), SCREEN_WIDTH - 14, BJ_DEALER_Y);
  bjDrawScore(bjHandValue(bj_playerHand, bj_playerCount), SCREEN_WIDTH - 14, BJ_PLAYER_Y);
  display.drawFastHLine(0, BJ_DEALER_Y + BJ_CARD_H + 1, SCREEN_WIDTH, SSD1306_WHITE);
  display.display();
}

static void bjRunDealer() {
  tone(BUZZER_PIN, 300, 40); bjRenderDealerReveal(); delay(1000);
  while (bjHandValue(bj_dealerHand, bj_dealerCount) < 17 && bj_dealerCount < BJ_MAX_HAND) {
    bj_dealerHand[bj_dealerCount++] = bjDeal();
    tone(BUZZER_PIN, 300, 40); bjRenderDealerReveal(); delay(1000);
  }
  if (bjHandValue(bj_dealerHand, bj_dealerCount) > 21) bj_dealerBust = true;
  bj_state = BJS_RESULT;
}

static void bjRenderResult() {
  display.clearDisplay();
  bjDrawHand(bj_dealerHand, bj_dealerCount, BJ_DEALER_Y, true);
  bjDrawHand(bj_playerHand, bj_playerCount, BJ_PLAYER_Y, true);
  int ps = bjHandValue(bj_playerHand, bj_playerCount);
  int ds = bjHandValue(bj_dealerHand, bj_dealerCount);
  bjDrawScore(ds, SCREEN_WIDTH - 14, BJ_DEALER_Y);
  bjDrawScore(ps, SCREEN_WIDTH - 14, BJ_PLAYER_Y);
  display.drawFastHLine(0, BJ_DEALER_Y + BJ_CARD_H + 1, SCREEN_WIDTH, SSD1306_WHITE);

  int msgY = BJ_DEALER_Y + BJ_CARD_H + 3;

  // Determine outcome and update bank
  bool playerWon = false;
  bool tie       = false;

  if (bj_playerBust) {
    bjPrintCentered("BUST! YOU LOSE", msgY);
    bj_bank -= bjCurrentBet();
  } else if (bj_dealerBust) {
    bjPrintCentered("DEALER BUST!", msgY);
    bj_bank += bjCurrentBet();
    playerWon = true;
  } else if (ps > ds) {
    bjPrintCentered("YOU WIN!", msgY);
    bj_bank += bjCurrentBet();
    playerWon = true;
  } else if (ps == ds) {
    bjPrintCentered("TIE / PUSH", msgY);
    tie = true;
  } else {
    bjPrintCentered("DEALER WINS", msgY);
    bj_bank -= bjCurrentBet();
  }

  // Show bet placed and running bank total below outcome message
  // Bottom bar: "Bet $X"  PLAY  "W $X" or "L $-X"
  int y = SCREEN_HEIGHT - 7;
  display.setTextSize(1); display.setTextColor(SSD1306_WHITE);

  // Left: bet placed this round
  char betBuf[10];
  snprintf(betBuf, sizeof(betBuf), "Bet $%d", bjCurrentBet());
  display.setCursor(0, y);
  display.print(betBuf);

  // Right: W $X or L $-X (negative bank shows as L $-50 etc)
  char bankBuf[12];
  if (bj_bank >= 0)
    snprintf(bankBuf, sizeof(bankBuf), "W $%ld", bj_bank);
  else
    snprintf(bankBuf, sizeof(bankBuf), "L $%ld", bj_bank);  // %ld prints minus sign automatically
  int bw = strlen(bankBuf) * 6;
  display.setCursor(SCREEN_WIDTH - bw, y);
  display.print(bankBuf);

  // Centre: PLAY
  display.setCursor((SCREEN_WIDTH - 24) / 2, y);
  display.print("PLAY");
  display.display();

  // Result jingle
  if (playerWon) {
    tone(BUZZER_PIN, 1047, 80); delay(90);
    tone(BUZZER_PIN, 1319, 80); delay(90);
    tone(BUZZER_PIN, 1568, 120);
  } else if (!tie) {
    tone(BUZZER_PIN, 300, 200);
  }
}

// ============================================================================
// PAUSE SCREEN
// ============================================================================
static void bjDrawPauseScreen() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(2);
  display.setCursor(34, 22);
  display.print("PAUSE");
  display.setTextSize(1);
  display.setCursor(0, 55);
  display.print("PLAY");
  display.setCursor(SCREEN_WIDTH - 24, 55);
  display.print("EXIT");
  display.display();
}

// ============================================================================
// INTERNAL SETUP
// ============================================================================
static void blackjackSetup_Internal() {
  bjBuildDeck();
  bjShuffle();
  bj_state = BJS_SPLASH;
  bj_paused = false;
  bj_pauseNeedsRelease = false;
  bj_b1Prev = HIGH;
  bj_b2Prev = HIGH;
  bj_b3Prev = HIGH;
  bj_betIndex = 0;
  // bj_bank intentionally NOT reset here — persists until power-off
  bjRenderSplash();
}

// ============================================================================
// INTERNAL LOOP
// ============================================================================
static void blackjackLoop_Internal() {
  bjMcpRead(bj_mcp_gpio);
  bjBtnUpdate(bj_b1);
  bjBtnUpdate(bj_b2);
  bjBtnUpdate(bj_b3);

  // ── PAUSE TOGGLE ──────────────────────────────────────────────────────────
  extern bool g_gamePauseRequest;
  if (g_gamePauseRequest) {
    g_gamePauseRequest = false;
    if (!bj_paused) {
      bj_paused = true;
      bj_pauseBtn1WasDown = false;
      bj_pauseBtn3WasDown = false;
      bj_b1Prev = HIGH;
      bj_b2Prev = HIGH;
      bj_b3Prev = HIGH;
    }
  }

  // ── PAUSE SCREEN ──────────────────────────────────────────────────────────
  if (bj_paused) {
    bool b1Currently = bjBtnDown(bj_b1);
    bool b3Currently = bjBtnDown(bj_b3);

    if (bj_pauseBtn1WasDown && !b1Currently) {
      bj_pauseBtn1WasDown = false;
      bj_paused = false;
      bj_b1Prev = HIGH; bj_b2Prev = HIGH; bj_b3Prev = HIGH;
      bjMcpRead(bj_mcp_gpio);
      bjBtnUpdate(bj_b1);
      bjBtnUpdate(bj_b2);
      bjBtnUpdate(bj_b3);
      bj_b1Prev = HIGH; bj_b2Prev = HIGH; bj_b3Prev = HIGH;
      switch (bj_state) {
        case BJS_SPLASH:         bjRenderSplash();        break;
        case BJS_BETWEEN_ROUNDS: bjRenderBetweenRounds(); break;
        case BJS_PLAYER_TURN:    bjRenderPlayerTurn();    break;
        case BJS_RESULT:         bjRenderResult();        break;
      }
      return;
    } else {
      bj_pauseBtn1WasDown = b1Currently;
    }

    if (bj_pauseBtn3WasDown && !b3Currently) {
      bj_pauseBtn3WasDown = false;
      bj_paused = false;
      bj_b1Prev = HIGH; bj_b2Prev = HIGH; bj_b3Prev = HIGH;
      gameExitToMenu();
      return;
    } else {
      bj_pauseBtn3WasDown = b3Currently;
    }

    bjDrawPauseScreen();
    delay(30);
    return;
  }

  // ── EDGE DETECTION ────────────────────────────────────────────────────────
  bool hit   = bj_justPressed(bj_b1, bj_b1Prev);
  bool deal  = bj_justPressed(bj_b2, bj_b2Prev);
  bool stay  = bj_justPressed(bj_b3, bj_b3Prev);

  // ── STATE MACHINE ─────────────────────────────────────────────────────────
  switch (bj_state) {

    case BJS_SPLASH:
      // BTN1 / BTN3 adjust bet on splash screen
      if (hit)  { bjBetDecrease(); bjRenderSplash(); break; }
      if (stay) { bjBetIncrease(); bjRenderSplash(); break; }
      if (deal) {
        bj_state = BJS_BETWEEN_ROUNDS;
        bjRenderBetweenRounds();
      }
      break;

    case BJS_BETWEEN_ROUNDS:
      // BTN1 / BTN3 adjust bet before dealing
      if (hit)  { bjBetDecrease(); bjRenderBetweenRounds(); break; }
      if (stay) { bjBetIncrease(); bjRenderBetweenRounds(); break; }
      if (deal) {
        delay(150);
        bjDealHands();
        bjRenderPlayerTurn();
      }
      break;

    case BJS_PLAYER_TURN:
      // BTN1 / BTN3 are HIT / STAY — no bet changing mid-hand
      if (hit) {
        if (bj_playerCount < BJ_MAX_HAND) {
          bj_playerHand[bj_playerCount++] = bjDeal();
          tone(BUZZER_PIN, 1200, 25);
          delay(400);
        }
        if (bjHandValue(bj_playerHand, bj_playerCount) > 21) {
          bj_playerBust = true;
          bj_state      = BJS_RESULT;
          bjRenderPlayerTurn();
          delay(1800);
          bjRenderResult();
        } else {
          bjRenderPlayerTurn();
        }
      }
      else if (stay) {
        bjRunDealer();
        delay(1200);
        bjRenderResult();
      }
      break;

    case BJS_RESULT:
      // BTN1 / BTN3 adjust bet before next round
      if (deal) {
        delay(150);
        bjShuffle();
        bj_state = BJS_BETWEEN_ROUNDS;
        bjRenderBetweenRounds();
      }
      break;
  }

  delay(30);
}

// ============================================================================
// PUBLIC WATCHOS API
// ============================================================================

void blackjackEnter() {
  bj_active = true;
  if (!bj_setupRan) {
    blackjackSetup_Internal();
    bj_setupRan = true;
  } else {
    bj_paused        = false;
    bj_pauseNeedsRelease = false;
    bj_b1Prev = HIGH; bj_b2Prev = HIGH; bj_b3Prev = HIGH;
    bj_betIndex = 0;
    // bj_bank NOT reset — persists this session
    bjShuffle();
    bj_state = BJS_BETWEEN_ROUNDS;
    bjRenderBetweenRounds();
  }
}

void blackjackUpdate() {
  if (!bj_active) return;
  blackjackLoop_Internal();
}

void blackjackExit() {
  bj_active = false;
  noTone(BUZZER_PIN);
}