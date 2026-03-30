// ==============================
// IRON TIDES — CHIP-BOY PORT
// Display: SSD1306 (I2C D0/D1, addr 0x3C)
// Buttons: MCP23008 (BTN1/2/3 + nav switch)
// Pause: Encoder switch (debounced press event)
// ==============================

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ============================================================================
// WATCH OS APP WRAPPER API
// ============================================================================
static bool s_gameActive = false;
static bool s_setupRan = false;

bool ironTidesIsActive() { return s_gameActive; }
extern void gameExitToMenu();   

// Forward-declare public app API (normally in a header)
void ironTidesEnter();
void ironTidesUpdate();
void ironTidesExit();

// =====================
// Structs
// =====================

struct BtnState {
  uint8_t pin;               // MCP bit index 0..7
  bool lastRead = HIGH;      // HIGH=released (pull-ups)
  bool stable = HIGH;        // debounced stable level
  unsigned long lastChange = 0;
};

struct Ship { float x,y; float dx,dy; bool active; int lives; };

struct Bullet { float x,y; float vx,vy; bool active; bool fromEnemy; };

// =====================
// Forward Declarations
// =====================

// MCP helpers
static bool mcpWriteReg(uint8_t reg, uint8_t val);
static bool mcpReadReg(uint8_t reg, uint8_t &out);
static inline bool mcpReadBit(uint8_t bit);



// Button helpers
static void btnUpdate(BtnState &b);
static inline bool btnDown(const BtnState &b);

// Encoder switch
static bool encSwitchPressedEvent();

// Collision / gameplay helpers
static bool rectsOverlap(float ax,float ay,float aw,float ah,float bx,float by,float bw,float bh);
static bool shipWillCollide(float nx,float ny,float dx,float dy,float ox,float oy,float odx,float ody);
static bool bulletHitsShip(Bullet &b, Ship &s);
static bool bulletHitsBoss(Bullet &b);

// Drawing
static void drawShipOutline(float cx,float cy,float dx,float dy);
static void drawShipFilledArrow(float cx,float cy,float dx,float dy);
static void drawBossShip(float cx,float cy,float dx,float dy);
static void drawFrame();

// Bullets / firing
static void addBullet(float sx,float sy,float vx,float vy,bool fromEnemy);
static void fireLeft(Ship &s,unsigned long &last,bool fromEnemy);
static void fireRight(Ship &s,unsigned long &last,bool fromEnemy);
static void fireTopBurst(Ship &s,unsigned long &last,bool fromEnemy);
static void fireLeftForBoss();
static void fireRightForBoss();
static void fireTopBurstForBoss();

// UI / spawning / reset
static void showCenteredText(const char* msg,int size,int delayTime);
static void spawnEnemyAtRandomBorder(Ship &e);
static void spawnEnemiesForLevel();
static void spawnBossAtRandomBorder();
static void resetGame();

static void drawTitleWaveArches(int xShift);

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// -------- Display ----------
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_ADDR 0x3C

extern Adafruit_SSD1306 display;


extern bool displayReady;


static bool probeI2C(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

static void ensureDisplayInit() {
  if (displayReady) return;

  // Only init if the OLED ACKs on I2C
  if (!probeI2C(OLED_ADDR)) return;

  if (display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    displayReady = true;
    display.setTextColor(SSD1306_WHITE);
    display.setTextWrap(false);
    display.clearDisplay();
    display.display();
  }
}


// Rotary encoder switch (pause) — D6 to GND (INPUT_PULLUP)
//#define ENC_SW_PIN D6
// Encoder debouncer vars removed — main loop handles ENC_SW_PIN.


#define ENC_A_PIN D8
#define ENC_B_PIN D7

// Buzzer pin 
#ifndef BUZZER_PIN
#define BUZZER_PIN D2
#endif

// -------- MCP23008 ----------
// And mapping: BTN1->GP0, BTN2->GP6, BTN3->GP1, UP->GP4, DOWN->GP3, RIGHT->GP2, LEFT->GP5

#define MCP_ADDR   0x20
#define MCP_IODIR  0x00
#define MCP_GPIO   0x09
#define MCP_GPPU   0x06

static uint8_t g_mcp_gpio = 0xFF;
static bool g_mcp_ok = false;

static bool mcpWriteReg(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(MCP_ADDR);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}

static bool mcpReadReg(uint8_t reg, uint8_t &out) {
  Wire.beginTransmission(MCP_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((int)MCP_ADDR, 1) != 1) return false;
  out = Wire.read();
  return true;
}

//Button debounce struct 

static const unsigned long BTN_DEBOUNCE_MS = 25;

static inline bool mcpReadBit(uint8_t bit) {
  // MCP pull-ups => released=1, pressed=0
  return (g_mcp_gpio & (1u << bit)) ? HIGH : LOW;
}

static void btnUpdate(BtnState &b) {
  bool r = mcpReadBit(b.pin);
  if (r != b.lastRead) {
    b.lastRead = r;
    b.lastChange = millis();
  }
  if ((millis() - b.lastChange) >= BTN_DEBOUNCE_MS) {
    b.stable = b.lastRead;
  }
}

static inline bool btnDown(const BtnState &b) {
  return (b.stable == LOW);
}

// Encoder switch: one true event per debounced press (falling edge)
// encSwitchPressedEvent() removed — pause driven by g_gamePauseRequest.

// Button instances (MCP bits)
static BtnState b1     { .pin = 0 }; // BTN1 -> GP0
static BtnState b2     { .pin = 6 }; // BTN2 -> GP6
static BtnState b3     { .pin = 1 }; // BTN3 -> GP1
static BtnState bUp    { .pin = 4 }; // UP -> GP4
static BtnState bDown  { .pin = 3 }; // DOWN -> GP3
static BtnState bRight { .pin = 2 }; // RIGHT -> GP2
static BtnState bLeft  { .pin = 5 }; // LEFT -> GP5

// ============================================================================
// GAME CODE 
// ============================================================================


Ship enemies[3];
Ship player;
float speed = 0.35;


#define MAX_BULLETS 60
Bullet bullets[MAX_BULLETS];

int playerLives;
int enemiesDefeated = 0;
int totalEnemiesThisLevel = 3;
int currentLevel = 1;
bool gameOver = false;
bool playerWon = false;
int enemiesSpawnedThisLevel = 0;

unsigned long lastFireLeft = 0;
unsigned long lastFireRight = 0;
unsigned long lastFireTop = 0;
const unsigned long fireCooldown = 750;
const unsigned long fireCooldownTop = 750;

unsigned long enemyLastFireLeft[3] = {0,0,0};
unsigned long enemyLastFireRight[3] = {0,0,0};
unsigned long enemyLastFireTop[3] = {0,0,0};
const unsigned long enemyFireCooldown = 1500;
const unsigned long enemyFireCooldownTop = 3000;

bool paused = false;
static bool pauseOverlayDrawn = false;
static unsigned long pauseIgnoreUntilMs = 0;
static bool pauseNeedsRelease = false;

bool enemyDisintegrating = false;
int disintegratingEnemyIndex = -1;
unsigned long disintegrateStart = 0;
const unsigned long disintegrateDuration = 1000;


// Boss
bool bossActive = false;
float bossX, bossY;
float bossDX, bossDY;
int bossLives = 8;
unsigned long bossLastFireLeft = 0;
unsigned long bossLastFireRight = 0;
unsigned long bossLastFireTop = 0;
const unsigned long bossFireCooldown = 3000;
const unsigned long bossFireCooldownTop = 4000;
float bossAngle = 0;
bool bossDisintegrating = false;

// Player disintegration
bool playerDisintegrating = false;
unsigned long playerDisintegrateStart = 0;
const unsigned long playerDisintegrateDuration = 1600;


static bool rectsOverlap(float ax,float ay,float aw,float ah,float bx,float by,float bw,float bh){
  return (fabs(ax-bx)*2 < (aw+bw)) && (fabs(ay-by)*2 < (ah+bh));
}
static bool shipWillCollide(float nx,float ny,float dx,float dy,float ox,float oy,float odx,float ody){
  float aw=(dx==0)?3:6; float ah=(dx==0)?6:3;
  float bw=(odx==0)?3:6; float bh=(ody==0)?6:3;
  return rectsOverlap(nx,ny,aw,ah,ox,oy,bw,bh);
}
static bool bulletHitsShip(Bullet &b, Ship &s){
  float sw=(s.dx==0)?3:6; float sh=(s.dx==0)?6:3;
  return fabs(b.x-s.x)*2<sw && fabs(b.y-s.y)*2<sh;
}
static bool bulletHitsBoss(Bullet &b){
  float sw = 6 * 2.5;
  float sh = 3 * 2.5;
  return fabs(b.x - bossX)*2<sw && fabs(b.y - bossY)*2<sh;
}

static void drawShipOutline(float cx,float cy,float dx,float dy){
  float len=sqrt(dx*dx+dy*dy); if(len==0) len=1;
  dx/=len; dy/=len;
  float halfWidth=1,length=6;
  float px=-dy,py=dx;
  float rearX=cx-dx*(length/2),rearY=cy-dy*(length/2);
  float frontX=cx+dx*(length/2),frontY=cy+dy*(length/2);
  float p1x=rearX+px*halfWidth,p1y=rearY+py*halfWidth;
  float p2x=rearX-px*halfWidth,p2y=rearY-py*halfWidth;
  float p3x=frontX-px*halfWidth,p3y=frontY-py*halfWidth;
  float p4x=frontX+px*halfWidth,p4y=frontY+py*halfWidth;
  float tipX=frontX+dx*2,tipY=frontY+dy*2;

  display.drawLine((int)p1x,(int)p1y,(int)p2x,(int)p2y,SSD1306_WHITE);
  display.drawLine((int)p1x,(int)p1y,(int)p4x,(int)p4y,SSD1306_WHITE);
  display.drawLine((int)p2x,(int)p2y,(int)p3x,(int)p3y,SSD1306_WHITE);
  display.drawLine((int)p3x,(int)p3y,(int)tipX,(int)tipY,SSD1306_WHITE);
  display.drawLine((int)p4x,(int)p4y,(int)tipX,(int)tipY,SSD1306_WHITE);
}

static void drawShipFilledArrow(float cx,float cy,float dx,float dy){
  float len=sqrt(dx*dx+dy*dy); if(len==0) len=1;
  dx/=len; dy/=len;
  float halfWidth=1,length=6;
  float px=-dy,py=dx;
  float rearX=cx-dx*(length/2),rearY=cy-dy*(length/2);
  float frontX=cx+dx*(length/2),frontY=cy+dy*(length/2);
  float p1x=rearX+px*halfWidth,p1y=rearY+py*halfWidth;
  float p2x=rearX-px*halfWidth,p2y=rearY-py*halfWidth;
  float p3x=frontX-px*halfWidth,p3y=frontY-py*halfWidth;
  float p4x=frontX+px*halfWidth,p4y=frontY+py*halfWidth;
  float tipX=frontX+dx*2,tipY=frontY+dy*2;

  display.fillTriangle((int)p3x,(int)p3y,(int)p4x,(int)p4y,(int)tipX,(int)tipY,SSD1306_WHITE);
  display.fillTriangle((int)p1x,(int)p1y,(int)p2x,(int)p2y,(int)p3x,(int)p3y,SSD1306_WHITE);
  display.fillTriangle((int)p1x,(int)p1y,(int)p3x,(int)p3y,(int)p4x,(int)p4y,SSD1306_WHITE);
}
static void drawBossShip(float cx,float cy,float dx,float dy){
  float len=sqrt(dx*dx+dy*dy); if(len==0) len=1;
  dx/=len; dy/=len;
  float halfWidth = 3.0f;
  float length    = 18.0f;   
  float px=-dy,py=dx;
  float rearX=cx-dx*(length/2),rearY=cy-dy*(length/2);
  float frontX=cx+dx*(length/2),frontY=cy+dy*(length/2);
  float p1x=rearX+px*halfWidth,p1y=rearY+py*halfWidth;
  float p2x=rearX-px*halfWidth,p2y=rearY-py*halfWidth;
  float p3x=frontX-px*halfWidth,p3y=frontY-py*halfWidth;
  float p4x=frontX+px*halfWidth,p4y=frontY+py*halfWidth;
  float tipX=frontX+dx*5,tipY=frontY+dy*5;

  display.fillTriangle((int)lroundf(p3x),(int)lroundf(p3y),
                     (int)lroundf(p4x),(int)lroundf(p4y),
                     (int)lroundf(tipX),(int)lroundf(tipY), SSD1306_WHITE);

  display.fillTriangle((int)lroundf(p1x),(int)lroundf(p1y),
                     (int)lroundf(p2x),(int)lroundf(p2y),
                     (int)lroundf(p3x),(int)lroundf(p3y), SSD1306_WHITE);

  display.fillTriangle((int)lroundf(p1x),(int)lroundf(p1y),
                     (int)lroundf(p3x),(int)lroundf(p3y),
                     (int)lroundf(p4x),(int)lroundf(p4y), SSD1306_WHITE);

}

static void drawFrame() {
  display.clearDisplay();

  // player
  drawShipOutline(player.x, player.y, player.dx, player.dy);

  // boss
  if (bossActive) drawBossShip(bossX, bossY, bossDX, bossDY);

  // enemies
  for (int i=0;i<3;i++) if (enemies[i].active)
    drawShipFilledArrow(enemies[i].x, enemies[i].y, enemies[i].dx, enemies[i].dy);

  // bullets
  for (int i=0;i<MAX_BULLETS;i++) if (bullets[i].active)
    display.drawPixel((int)bullets[i].x,(int)bullets[i].y,SSD1306_WHITE);

  // player lives bottom-left
  for (int i=0;i<playerLives;i++) display.drawPixel(i*3, SCREEN_HEIGHT-1, SSD1306_WHITE);

  // boss lives bottom-right
  if (bossActive) {
    for (int i=0;i<bossLives;i++) {
      int x = SCREEN_WIDTH - 1 - (i*3);
      int y = SCREEN_HEIGHT - 3;
      display.drawPixel(x, y, SSD1306_WHITE);
      display.drawPixel(x-1, y, SSD1306_WHITE);
      display.drawPixel(x, y+1, SSD1306_WHITE);
      display.drawPixel(x-1, y+1, SSD1306_WHITE);
    }
  }

  display.display();
}

static void addBullet(float sx,float sy,float vx,float vy,bool fromEnemy){
  for(int i=0;i<MAX_BULLETS;i++){
    if(!bullets[i].active){
      bullets[i].active=true;
      bullets[i].x=sx; bullets[i].y=sy;
      bullets[i].vx=vx; bullets[i].vy=vy;
      bullets[i].fromEnemy=fromEnemy;
      break;
    }
  }
}

static void fireLeft(Ship &s, unsigned long &last, bool fromEnemy){
  if(fromEnemy){ if(millis()-last<enemyFireCooldown)return; last=millis(); }
  else{ if(millis()-last<fireCooldown)return; last=millis(); }

  // ENEMY SHOTS: aim at player, but spawn from LEFT cannon
  if (fromEnemy) {
    float tx = player.x - s.x;
    float ty = player.y - s.y;
    float dist = sqrtf(tx*tx + ty*ty);
    if (dist < 0.001f) dist = 1.0f;

    float ux = tx / dist;
    float uy = ty / dist;

    // "Right" vector for the aim direction
    float rx = -uy;
    float ry =  ux;

    // Left cannon = negative right-vector offset
    float off = 2.0f;
    float sx = s.x - rx * off;
    float sy = s.y - ry * off;

    addBullet(sx, sy, ux*2.0f, uy*2.0f, true);
    return;
  }

  // PLAYER SHOTS (original behavior)
  float len=sqrt(s.dx*s.dx+s.dy*s.dy); if(len==0)len=1;
  float px=s.dy/len,py=-s.dx/len;
  addBullet(s.x,s.y,px*2,py*2,fromEnemy);
}

static void fireRight(Ship &s, unsigned long &last, bool fromEnemy){
  if(fromEnemy){ if(millis()-last<enemyFireCooldown)return; last=millis(); }
  else{ if(millis()-last<fireCooldown)return; last=millis(); }

  // ENEMY SHOTS: aim at player, but spawn from RIGHT cannon
  if (fromEnemy) {
    float tx = player.x - s.x;
    float ty = player.y - s.y;
    float dist = sqrtf(tx*tx + ty*ty);
    if (dist < 0.001f) dist = 1.0f;

    float ux = tx / dist;
    float uy = ty / dist;

    float rx = -uy;
    float ry =  ux;

    // Right cannon = positive right-vector offset
    float off = 2.0f;
    float sx = s.x + rx * off;
    float sy = s.y + ry * off;

    addBullet(sx, sy, ux*2.0f, uy*2.0f, true);
    return;
  }

  // PLAYER SHOTS (original behavior)
  float len=sqrt(s.dx*s.dx+s.dy*s.dy); if(len==0)len=1;
  float px=-s.dy/len,py=s.dx/len;
  addBullet(s.x,s.y,px*2,py*2,fromEnemy);
}

static void fireTopBurst(Ship &s, unsigned long &last, bool fromEnemy){
  if(fromEnemy){ if(millis()-last<enemyFireCooldownTop)return; last=millis(); }
  else{ if(millis()-last<fireCooldownTop)return; last=millis(); }

  // ENEMY SHOTS: triple burst aimed at player (forward + ±45°)
  if (fromEnemy) {
    float tx = player.x - s.x;
    float ty = player.y - s.y;
    float dist = sqrtf(tx*tx + ty*ty);
    if (dist < 0.001f) dist = 1.0f;

    float fx = tx / dist;
    float fy = ty / dist;

    addBullet(s.x, s.y, fx*2.0f, fy*2.0f, true);

    float a=-M_PI/4;
    float r1x=fx*cos(a)-fy*sin(a), r1y=fx*sin(a)+fy*cos(a);
    addBullet(s.x, s.y, r1x*2.0f, r1y*2.0f, true);

    a= M_PI/4;
    float r2x=fx*cos(a)-fy*sin(a), r2y=fx*sin(a)+fy*cos(a);
    addBullet(s.x, s.y, r2x*2.0f, r2y*2.0f, true);
    return;
  }

  // PLAYER SHOTS (original behavior)
  float len=sqrt(s.dx*s.dx+s.dy*s.dy); if(len==0)len=1;
  float fx=s.dx/len,fy=s.dy/len;

  addBullet(s.x,s.y,fx*2,fy*2,fromEnemy);
  float a=-M_PI/4;
  float r1x=fx*cos(a)-fy*sin(a), r1y=fx*sin(a)+fy*cos(a);
  addBullet(s.x,s.y,r1x*2,r1y*2,fromEnemy);
  a=M_PI/4;
  float r2x=fx*cos(a)-fy*sin(a), r2y=fx*sin(a)+fy*cos(a);
  addBullet(s.x,s.y,r2x*2,r2y*2,fromEnemy);
}

static void fireLeftForBoss() {
  if (millis() - bossLastFireLeft < bossFireCooldown) return;
  bossLastFireLeft = millis();

  float tx = player.x - bossX;
  float ty = player.y - bossY;
  float dist = sqrtf(tx*tx + ty*ty);
  if (dist < 0.001f) dist = 1.0f;

  float ux = tx / dist;
  float uy = ty / dist;

  float rx = -uy, ry = ux;     // right vector
  float off = 4.0f;            // boss is bigger, offset more

  float sx = bossX - rx * off; // LEFT cannon
  float sy = bossY - ry * off;

  // Main shot toward player
  addBullet(sx, sy, ux*2.0f, uy*2.0f, true);

  
  float angle=0.25f, cosA=cos(angle), sinA=sin(angle);
  float lx = ux*cosA - uy*sinA, ly = ux*sinA + uy*cosA;
  float rx2 = ux*cosA + uy*sinA, ry2 = -ux*sinA + uy*cosA;
  addBullet(sx, sy, lx*2.0f, ly*2.0f, true);
  addBullet(sx, sy, rx2*2.0f, ry2*2.0f, true);
}

static void fireRightForBoss() {
  if (millis() - bossLastFireRight < bossFireCooldown) return;
  bossLastFireRight = millis();

  float tx = player.x - bossX;
  float ty = player.y - bossY;
  float dist = sqrtf(tx*tx + ty*ty);
  if (dist < 0.001f) dist = 1.0f;

  float ux = tx / dist;
  float uy = ty / dist;

  float rx = -uy, ry = ux;
  float off = 4.0f;

  float sx = bossX + rx * off; // RIGHT cannon
  float sy = bossY + ry * off;

  addBullet(sx, sy, ux*2.0f, uy*2.0f, true);

  float angle=0.25f, cosA=cos(angle), sinA=sin(angle);
  float lx = ux*cosA - uy*sinA, ly = ux*sinA + uy*cosA;
  float rx2 = ux*cosA + uy*sinA, ry2 = -ux*sinA + uy*cosA;
  addBullet(sx, sy, lx*2.0f, ly*2.0f, true);
  addBullet(sx, sy, rx2*2.0f, ry2*2.0f, true);
}

static void fireTopBurstForBoss(){
  if(millis()-bossLastFireTop<bossFireCooldownTop)return;
  bossLastFireTop=millis();

  float tx = player.x - bossX;
  float ty = player.y - bossY;
  float dist = sqrtf(tx*tx + ty*ty);
  if (dist < 0.001f) dist = 1.0f;

  float fx = tx / dist;
  float fy = ty / dist;

  addBullet(bossX, bossY, fx*2.0f, fy*2.0f, true);

  float a=-M_PI/4;
  float r1x=fx*cos(a)-fy*sin(a), r1y=fx*sin(a)+fy*cos(a);
  addBullet(bossX, bossY, r1x*2.0f, r1y*2.0f, true);

  a= M_PI/4;
  float r2x=fx*cos(a)-fy*sin(a), r2y=fx*sin(a)+fy*cos(a);
  addBullet(bossX, bossY, r2x*2.0f, r2y*2.0f, true);
}


static void showCenteredText(const char* msg,int size,int delayTime){
  display.clearDisplay();
  display.setTextSize(size);
  display.setTextColor(SSD1306_WHITE);
  int16_t x1,y1; uint16_t w,h;
  display.getTextBounds(msg,0,0,&x1,&y1,&w,&h);
  display.setCursor((SCREEN_WIDTH-w)/2,(SCREEN_HEIGHT-h)/2);
  display.print(msg);
  display.display();
  if(delayTime>0) delay(delayTime);
}

static void drawTitleWaveArches(int xShift) {
  // Chain of U-shaped arches: ∪∪∪∪ across the screen (bottom half ellipses)
  const int yBase = 56;   // top of the U line sits around here; tweak 54–60
  const int rx    = 9;    // wider (horizontal radius)
  const int ry    = 4;    // a little taller (vertical radius)
  const int step  = 2 * rx;

  // Wrap shift so it repeats nicely
  int shift = xShift % step;
  if (shift < 0) shift += step;

  for (int cx = -rx - shift; cx < SCREEN_WIDTH + rx; cx += step) {

    int prevX = 0, prevY = 0;
    bool havePrev = false;

    for (int dx = -rx; dx <= rx; dx++) {
      int x = cx + dx;

      // Ellipse bottom half:
      // yOffset = ry * sqrt(1 - (dx^2 / rx^2))
      float t = 1.0f - ((float)(dx * dx) / (float)(rx * rx));
      if (t < 0.0f) t = 0.0f;

      int yOffset = (int)lroundf((float)ry * sqrtf(t));
      int y = yBase + yOffset;   // FLIPPED: bottom half (∪)

      // clip
      if (x >= 0 && x < SCREEN_WIDTH && y >= 0 && y < SCREEN_HEIGHT) {
        if (havePrev) display.drawLine(prevX, prevY, x, y, SSD1306_WHITE);
        prevX = x; prevY = y; havePrev = true;
      } else {
        havePrev = false;
      }
    }
  }
}



static void spawnEnemyAtRandomBorder(Ship &e){
  int edge = random(0,4);
  if(edge==0){ e.x=random(0,SCREEN_WIDTH); e.y=-5; e.dx=0; e.dy=1; }
  else if(edge==1){ e.x=random(0,SCREEN_WIDTH); e.y=SCREEN_HEIGHT+5; e.dx=0; e.dy=-1; }
  else if(edge==2){ e.x=-5; e.y=random(0,SCREEN_HEIGHT); e.dx=1; e.dy=0; }
  else { e.x=SCREEN_WIDTH+5; e.y=random(0,SCREEN_HEIGHT); e.dx=-1; e.dy=0; }
  e.active=true; e.lives=3;
}

static void spawnEnemiesForLevel(){
  for(int i=0;i<3;i++) enemies[i].active=false;
  enemiesSpawnedThisLevel=0;
  if(currentLevel==1){
    totalEnemiesThisLevel=3;
    spawnEnemyAtRandomBorder(enemies[0]);
    enemiesSpawnedThisLevel=1;
  } else if(currentLevel==2){
    totalEnemiesThisLevel=6;
    spawnEnemyAtRandomBorder(enemies[0]);
    spawnEnemyAtRandomBorder(enemies[1]);
    enemiesSpawnedThisLevel=2;
  }
  enemiesDefeated=0;
}

static void spawnBossAtRandomBorder() {
  int edge = random(0, 4);
  if (edge == 0) { bossX=random(0,SCREEN_WIDTH); bossY=-5; bossDX=0; bossDY=1; bossAngle=M_PI/2; }
  else if (edge == 1) { bossX=random(0,SCREEN_WIDTH); bossY=SCREEN_HEIGHT+5; bossDX=0; bossDY=-1; bossAngle=-M_PI/2; }
  else if (edge == 2) { bossX=-5; bossY=random(0,SCREEN_HEIGHT); bossDX=1; bossDY=0; bossAngle=0; }
  else { bossX=SCREEN_WIDTH+5; bossY=random(0,SCREEN_HEIGHT); bossDX=-1; bossDY=0; bossAngle=M_PI; }
}

static void resetGame(){
  for(int i=0;i<MAX_BULLETS;i++) bullets[i].active=false;
  bossActive=false;
  playerLives=10;
  gameOver=false;
  playerWon=false;
  paused=false;
  enemyDisintegrating=false;
  bossDisintegrating=false;
  currentLevel=1;

  player.x=SCREEN_WIDTH/2;
  player.y=SCREEN_HEIGHT/2;
  int dir=random(0,4);
  if(dir==0){player.dx=1;player.dy=0;}
  else if(dir==1){player.dx=-1;player.dy=0;}
  else if(dir==2){player.dx=0;player.dy=1;}
  else {player.dx=0;player.dy=-1;}

  spawnEnemiesForLevel();
}

// ============================================================================
// ORIGINAL setup() AND loop() ARE KEPT, BUT RENAMED INTO APP FUNCTIONS
// (since WatchOS can only have one real Arduino setup/loop)
// ============================================================================

static void ironTidesSetup_Internal() {
  Serial.begin(115200);

  Wire.begin(D0, D1);
  Wire.setClock(400000);

  Serial.println("I2C probe...");
  Serial.print(" OLED 0x3C: "); Serial.println(probeI2C(OLED_ADDR) ? "OK" : "MISSING");
  Serial.print(" MCP  0x20: "); Serial.println(probeI2C(MCP_ADDR)  ? "OK" : "MISSING");

  ensureDisplayInit();
  if (!displayReady) {
    Serial.println("OLED not detected on I2C. Check SDA/SCL pins + address + wiring.");
    while (1) delay(10);
  }
  display.clearDisplay();
  display.display();

  // MCP init: inputs + pull-ups on GP0..GP6
  bool ok = mcpWriteReg(MCP_IODIR, 0xFF);
  ok = ok && mcpWriteReg(MCP_GPPU, 0xFF);
  g_mcp_ok = ok;

  // Encoder switch for pause
  
  pinMode(ENC_A_PIN, INPUT_PULLUP);
  pinMode(ENC_B_PIN, INPUT_PULLUP);

  // Buzzer
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  randomSeed((uint32_t)esp_random());

    // Title screen + animated wave
    int melody[]={294,147,196,247,294,262,247,196,220,392};

    const uint32_t NOTE_MS = 125;
    const uint32_t GAP_MS  = 20;

    for (int i = 0; i < 10; i++) {

      uint32_t startTime = millis();   // lock tempo

      display.clearDisplay();
      display.setTextSize(2);
      display.setTextColor(SSD1306_WHITE);
      int16_t tx1, ty1; uint16_t tw, th;

      display.getTextBounds("Iron", 0, 0, &tx1, &ty1, &tw, &th);
      display.setCursor((SCREEN_WIDTH - tw) / 2, 10);
      display.print("Iron");

      display.getTextBounds("Tides", 0, 0, &tx1, &ty1, &tw, &th);
      display.setCursor((SCREEN_WIDTH - tw) / 2, 36);
      display.print("Tides");

      drawTitleWaveArches(i * 3);

      display.display();

      tone(BUZZER_PIN, melody[i], NOTE_MS);

      // wait ONLY until 125ms total has passed (no slower)
      while (millis() - startTime < NOTE_MS) {}

      noTone(BUZZER_PIN);
      delay(GAP_MS);
    }



  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  int16_t dx1,dy1; uint16_t dw,dh;
  display.getTextBounds("Defend your ship!",0,0,&dx1,&dy1,&dw,&dh);
  display.setCursor((SCREEN_WIDTH-dw)/2,(SCREEN_HEIGHT-dh)/2);
  display.print("Defend your ship!");
  display.display();
  delay(2000);

  resetGame();
  drawFrame();
}

static void ironTidesLoop_Internal() {
  // --- read MCP + update debounced buttons
  uint8_t v;
  if (mcpReadReg(MCP_GPIO, v)) { g_mcp_gpio = v; g_mcp_ok = true; }
  else { g_mcp_ok = false; g_mcp_gpio = 0xFF; }

  btnUpdate(b1); btnUpdate(b2); btnUpdate(b3);
  btnUpdate(bUp); btnUpdate(bDown); btnUpdate(bRight); btnUpdate(bLeft);

  // ====== PAUSE TOGGLE (driven by main-loop encoder) ======
  extern bool g_gamePauseRequest;
  if (g_gamePauseRequest) {
    g_gamePauseRequest = false;
    if (!paused) {
      paused = true;
      pauseOverlayDrawn = false;
    }
  }

  // ====== GAME OVER ======
  if (gameOver) {
    if (playerWon) {
      int winMelody[] = {392,220,196,247,262,294,247,196,147,392};
      int noteCount = (int)(sizeof(winMelody)/sizeof(winMelody[0]));
      for (int i=0;i<2;i++){
        display.clearDisplay();
        display.setTextSize(2);
        display.setTextColor(SSD1306_WHITE);
        int16_t x1,y1; uint16_t w,h;
        display.getTextBounds("VICTORY!",0,0,&x1,&y1,&w,&h);
        display.setCursor((SCREEN_WIDTH-w)/2,(SCREEN_HEIGHT-h)/2);
        display.print("VICTORY!");
        display.display();
        for (int j=0;j<noteCount;j++){
          tone(BUZZER_PIN, winMelody[j], 125);
          delay(125);
          noTone(BUZZER_PIN);
          delay(20);
        }
        display.clearDisplay();
        display.display();
        delay(150);
      }
    } else {
      int loseTones[] = {600, 400, 200};
      for (int i=0;i<3;i++){
        display.clearDisplay();
        display.setTextSize(2);
        display.setTextColor(SSD1306_WHITE);
        int16_t x1,y1; uint16_t w,h;
        display.getTextBounds("DEFEATED",0,0,&x1,&y1,&w,&h);
        display.setCursor((SCREEN_WIDTH-w)/2,(SCREEN_HEIGHT-h)/2);
        display.print("DEFEATED");
        display.display();
        for (int j=0;j<3;j++){
          tone(BUZZER_PIN, loseTones[j], 150);
          delay(150);
          noTone(BUZZER_PIN);
          delay(50);
        }
        display.clearDisplay();
        display.display();
        delay(150);
      }
    }

    noTone(BUZZER_PIN);
    resetGame();
    drawFrame();
    delay(30);
    return;
  }

  // ====== PAUSE SCREEN ======
  if (paused) {
    // --- NEW: while paused, allow BTN1 resume and BTN3 exit ---
    static bool b1WasDown = false;
    static bool b3WasDown = false;

    bool b1Currently = btnDown(b1);
    bool b3Currently = btnDown(b3);

    // BTN1: fire on RELEASE 
    if (b1WasDown && !b1Currently) {
      b1WasDown = false;
      paused = false;
      pauseOverlayDrawn = false;
    } else {
      b1WasDown = b1Currently;
    }

    // BTN3: fire on RELEASE 
    if (b3WasDown && !b3Currently) {
      b3WasDown = false;
      gameExitToMenu();
      return;
    } else {
      b3WasDown = b3Currently;
    }

    // --- pause drawing, plus PLAY/EXIT labels ---
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(2);

    // Center "PAUSE"
    display.setCursor(34, 22);
    display.print("PAUSE");

    // Bottom labels
    display.setTextSize(1);
    display.setCursor(0, 55);
    display.print("PLAY");

    // right aligned "EXIT" (4 chars * 6px = 24px wide)
    display.setCursor(SCREEN_WIDTH - 24, 55);
    display.print("EXIT");

    display.display();
    delay(30);
    return;
  } else {
    pauseOverlayDrawn = false; // ensure we redraw next time we pause
  }

  // ====== PLAYER DISINTEGRATION ======
  if (playerDisintegrating) {
    unsigned long elapsed = millis() - playerDisintegrateStart;

    display.clearDisplay();

    // show bullets during disintegration
    for (int i=0;i<MAX_BULLETS;i++) if (bullets[i].active)
      display.drawPixel((int)bullets[i].x,(int)bullets[i].y,SSD1306_WHITE);

    // Disintegration pixels around player position
    int remaining = max(0, 35 - (int)(elapsed/25));
    for (int p=0; p<remaining; p++){
      int rx = random(-4, 5);
      int ry = random(-4, 5);
      display.drawPixel((int)player.x + rx, (int)player.y + ry, SSD1306_WHITE);
    }

    // Draw lives bar 
    for (int i=0;i<playerLives;i++) display.drawPixel(i*3, SCREEN_HEIGHT-1, SSD1306_WHITE);

    display.display();

    if (elapsed > playerDisintegrateDuration) {
      playerDisintegrating = false;
      gameOver = true;
      playerWon = false;
    }

    delay(50);
    return;
  }


  // ====== ENEMY DISINTEGRATION ======
  if (enemyDisintegrating) {
    unsigned long elapsed = millis() - disintegrateStart;
    display.clearDisplay();
    drawShipOutline(player.x, player.y, player.dx, player.dy);

    for (int i=0;i<MAX_BULLETS;i++) if (bullets[i].active)
      display.drawPixel((int)bullets[i].x,(int)bullets[i].y,SSD1306_WHITE);

    int remaining = max(0, 30 - (int)(elapsed/30));
    for (int p=0;p<remaining;p++){
      int rx = random(-3,4);
      int ry = random(-3,4);
      display.drawPixel((int)enemies[disintegratingEnemyIndex].x+rx,
                        (int)enemies[disintegratingEnemyIndex].y+ry,
                        SSD1306_WHITE);
    }
    for (int i=0;i<playerLives;i++) display.drawPixel(i*3, SCREEN_HEIGHT-1, SSD1306_WHITE);
    display.display();

    if (elapsed > disintegrateDuration) {
      enemyDisintegrating = false;
      enemies[disintegratingEnemyIndex].active = false;
      disintegratingEnemyIndex = -1;
      enemiesDefeated++;

      if (!bossActive && enemiesDefeated >= totalEnemiesThisLevel) {
        for (int i=0;i<4;i++){
          showCenteredText("Fleet defeated!", 1, 0);
          tone(BUZZER_PIN,200,50); delay(50);
          tone(BUZZER_PIN,800,150); delay(50);
          noTone(BUZZER_PIN);
          delay(150);
        }

        if (currentLevel == 1) {
          currentLevel = 2;
          playerLives = 10;
          spawnEnemiesForLevel();
          showCenteredText(" Enemy fleet spotted!", 1, 2000);
        } else {
          display.clearDisplay();
          display.setTextSize(1);
          display.setTextColor(SSD1306_WHITE);

          const char* line1 = "Enemy command";
          const char* line2 = "ship spotted!";

          int16_t x1, y1;
          uint16_t w, h;

          // line 1 centered
          display.getTextBounds(line1, 0, 0, &x1, &y1, &w, &h);
          display.setCursor((SCREEN_WIDTH - w) / 2, (SCREEN_HEIGHT / 2) - 8);
          display.print(line1);

          // line 2 centered
          display.getTextBounds(line2, 0, 0, &x1, &y1, &w, &h);
          display.setCursor((SCREEN_WIDTH - w) / 2, (SCREEN_HEIGHT / 2) + 8);
          display.print(line2);

          display.display();
          delay(2000);


          bossActive = true;
          spawnBossAtRandomBorder();
          bossLives = 8;
          playerLives = 10;

          spawnEnemyAtRandomBorder(enemies[0]);
          spawnEnemyAtRandomBorder(enemies[1]);
          enemies[0].lives = 3; enemies[1].lives = 3;
          enemies[0].active = true; enemies[1].active = true;
        }
      } else {
        if (!bossActive) {
          if (currentLevel == 1) {
            spawnEnemyAtRandomBorder(enemies[0]);
            enemiesSpawnedThisLevel++;
          } else {
            bool bothInactive = !enemies[0].active && !enemies[1].active;
            if (bothInactive && enemiesSpawnedThisLevel < totalEnemiesThisLevel) {
              spawnEnemyAtRandomBorder(enemies[0]);
              spawnEnemyAtRandomBorder(enemies[1]);
              enemiesSpawnedThisLevel += 2;
            }
          }
        }
      }
    }

    delay(50);
    return;
  }

  // ====== BOSS DISINTEGRATION ======
  if (bossDisintegrating) {
    unsigned long elapsed = millis() - disintegrateStart;
    display.clearDisplay();
    drawShipOutline(player.x, player.y, player.dx, player.dy);

    for (int i=0;i<MAX_BULLETS;i++) if (bullets[i].active)
      display.drawPixel((int)bullets[i].x,(int)bullets[i].y,SSD1306_WHITE);

    int remaining = max(0, 50 - (int)(elapsed/20));
    for (int p=0;p<remaining;p++){
      int rx = random(-5,6);
      int ry = random(-5,6);
      display.drawPixel((int)bossX+rx,(int)bossY+ry,SSD1306_WHITE);
    }
    for (int i=0;i<playerLives;i++) display.drawPixel(i*3, SCREEN_HEIGHT-1, SSD1306_WHITE);
    display.display();

    if (elapsed > playerDisintegrateDuration) {
      bossDisintegrating = false;
      bossActive = false;
      playerWon = true;
      gameOver = true;
    }
    delay(50);
    return;
  }

  // ====== BOSS AI ======
  if (bossActive) {
    float dx = player.x - bossX;
    float dy = player.y - bossY;
    float circlingW = (player.dx==0 ? 3 : 6) + 24;
    float circlingH = (player.dx==0 ? 6 : 3) + 24;
    bool insideCircling = (fabs(dx)*2 < circlingW) && (fabs(dy)*2 < circlingH);

    if (!insideCircling) {
      float targetAngle = atan2(dy, dx);
      float diff = targetAngle - bossAngle;
      while (diff > PI) diff -= 2*PI;
      while (diff < -PI) diff += 2*PI;
      float rotationSpeed = 0.1;
      if (fabs(diff) < rotationSpeed) bossAngle = targetAngle;
      else bossAngle += (diff > 0 ? rotationSpeed : -rotationSpeed);

      bossDX = cos(bossAngle);
      bossDY = sin(bossAngle);
      fireTopBurstForBoss();
    } else {
      if (fabs(dx) > fabs(dy)) { bossDX=0; bossDY=(dy>0)?1:-1; }
      else { bossDY=0; bossDX=(dx>0)?1:-1; }
      if (dx < 0) fireLeftForBoss();
      else fireRightForBoss();
    }

    bossX += bossDX * speed * 0.5;
    bossY += bossDY * speed * 0.5;
    if (bossX < 10) bossX = 10;
    if (bossX > SCREEN_WIDTH-10) bossX = SCREEN_WIDTH-10;
    if (bossY < 10) bossY = 10;
    if (bossY > SCREEN_HEIGHT-10) bossY = SCREEN_HEIGHT-10;
  }

  // ====== PLAYER CONTROLS (NAV SWITCH) ======
  if (btnDown(bUp))    { player.dx=0; player.dy= 1; }  // up -> move down
  if (btnDown(bDown))  { player.dx=0; player.dy=-1; }  // down -> move up
  if (btnDown(bLeft))  { player.dx= 1; player.dy=0; }  // left -> move right
  if (btnDown(bRight)) { player.dx=-1; player.dy=0; }  // right -> move left

  // ====== FIRE CONTROLS ======
  // BTN1 = left cannon
  // BTN2 = center triple shot
  // BTN3 = right cannon
  if (btnDown(b1)) fireLeft(player, lastFireLeft, false);
  if (btnDown(b3)) fireRight(player, lastFireRight, false);
  if (btnDown(b2)) fireTopBurst(player, lastFireTop, false);

  // ====== ENEMY AI ======
  for (int ei=0; ei<3; ei++) {
    if (!enemies[ei].active) continue;

    float dx = player.x - enemies[ei].x;
    float dy = player.y - enemies[ei].y;
    float hitboxW = (player.dx==0?3:6)+12;
    float hitboxH = (player.dx==0?6:3)+12;
    bool insideHitbox = (fabs(dx)*2<hitboxW) && (fabs(dy)*2<hitboxH);

    if (!insideHitbox) {
      if (fabs(dx)>fabs(dy)) { enemies[ei].dx=(dx>0)?1:-1; enemies[ei].dy=0; }
      else { enemies[ei].dy=(dy>0)?1:-1; enemies[ei].dx=0; }

      float flen = sqrt(enemies[ei].dx*enemies[ei].dx+enemies[ei].dy*enemies[ei].dy);
      float fx = (flen>0)?enemies[ei].dx/flen:1.0;
      float fy = (flen>0)?enemies[ei].dy/flen:0.0;
      float dot = dx*fx+dy*fy;
      float dist = sqrt(dx*dx+dy*dy);
      float cosAngle = (dist>0)?dot/dist:1.0;
      if (dot>0 && cosAngle>0.8f) fireTopBurst(enemies[ei],enemyLastFireTop[ei],true);
    } else {
      if (fabs(dx)>fabs(dy)) { enemies[ei].dx=0; enemies[ei].dy=(dy>0)?1:-1; }
      else { enemies[ei].dy=0; enemies[ei].dx=(dx>0)?1:-1; }
      if (dx<0) fireLeft(enemies[ei],enemyLastFireLeft[ei],true);
      else fireRight(enemies[ei],enemyLastFireRight[ei],true);
    }

    float nex = enemies[ei].x + enemies[ei].dx*speed*0.9;
    float ney = enemies[ei].y + enemies[ei].dy*speed*0.9;

    bool enemyCanMoveX=true, enemyCanMoveY=true;
    if (shipWillCollide(nex,enemies[ei].y,enemies[ei].dx,enemies[ei].dy,player.x,player.y,player.dx,player.dy)) enemyCanMoveX=false;
    if (shipWillCollide(enemies[ei].x,ney,enemies[ei].dx,enemies[ei].dy,player.x,player.y,player.dx,player.dy)) enemyCanMoveY=false;

    for(int ej=0; ej<3; ej++){
      if(ej==ei) continue;
      if(enemies[ej].active){
        if(shipWillCollide(nex,enemies[ei].y,enemies[ei].dx,enemies[ei].dy,enemies[ej].x,enemies[ej].y,enemies[ej].dx,enemies[ej].dy)) enemyCanMoveX=false;
        if(shipWillCollide(enemies[ei].x,ney,enemies[ei].dx,enemies[ei].dy,enemies[ej].x,enemies[ej].y,enemies[ej].dx,enemies[ej].dy)) enemyCanMoveY=false;
      }
    }

    if(nex>=-6 && nex<=SCREEN_WIDTH+6 && enemyCanMoveX) enemies[ei].x=nex;
    if(ney>=-6 && ney<=SCREEN_HEIGHT+6 && enemyCanMoveY) enemies[ei].y=ney;
  }

  // ====== PLAYER MOVEMENT + COLLISION ======
  float npx = player.x + player.dx * speed;
  float npy = player.y + player.dy * speed;
  bool playerCanMoveX = true, playerCanMoveY = true;

  for (int e=0; e<3; e++) {
    if (enemies[e].active) {
      if (shipWillCollide(npx, player.y, player.dx, player.dy, enemies[e].x, enemies[e].y, enemies[e].dx, enemies[e].dy)) playerCanMoveX = false;
      if (shipWillCollide(player.x, npy, player.dx, player.dy, enemies[e].x, enemies[e].y, enemies[e].dx, enemies[e].dy)) playerCanMoveY = false;
    }
  }

  if (bossActive) {
    float bossAW = 15, bossAH = 15;
    if (rectsOverlap(npx, player.y, (player.dx==0?3:6),(player.dx==0?6:3), bossX,bossY,bossAW,bossAH)) playerCanMoveX=false;
    if (rectsOverlap(player.x, npy, (player.dx==0?3:6),(player.dx==0?6:3), bossX,bossY,bossAW,bossAH)) playerCanMoveY=false;
  }

  if (npx>=1 && npx<=SCREEN_WIDTH-2 && playerCanMoveX) player.x=npx;
  if (npy>=1 && npy<=SCREEN_HEIGHT-2 && playerCanMoveY) player.y=npy;

  // ====== BULLET UPDATES ======
  for (int i=0; i<MAX_BULLETS; i++) {
    if (!bullets[i].active) continue;

    bullets[i].x += bullets[i].vx;
    bullets[i].y += bullets[i].vy;

    if (bullets[i].x<0||bullets[i].x>SCREEN_WIDTH||bullets[i].y<0||bullets[i].y>SCREEN_HEIGHT){
      bullets[i].active=false;
      continue;
    }

    if (!bullets[i].fromEnemy) {
      for (int e=0; e<3; e++) {
        if (enemies[e].active && bulletHitsShip(bullets[i],enemies[e])) {
          bullets[i].active=false;
          enemies[e].lives--;
          tone(BUZZER_PIN,300,50); delay(60); noTone(BUZZER_PIN);

          if (enemies[e].lives <= 0) {
            for(int c=0;c<8;c++){ tone(BUZZER_PIN,50+random(0,75),20); delay(30); }
            noTone(BUZZER_PIN);
            enemyDisintegrating=true;
            disintegratingEnemyIndex=e;
            disintegrateStart=millis();
            enemies[e].dx=0; enemies[e].dy=0;
          }
        }
      }

      if (bossActive && bulletHitsBoss(bullets[i])) {
        bullets[i].active=false;
        bossLives--;
        tone(BUZZER_PIN,300,50); delay(60); noTone(BUZZER_PIN);

        if (bossLives <= 0) {
          for(int c=0;c<8;c++){ tone(BUZZER_PIN,50+random(0,75),20); delay(30); }
          noTone(BUZZER_PIN);
          bossDisintegrating=true;
          disintegrateStart=millis();
          bossDX=0; bossDY=0;
        }
      }
    } else {
      if (bulletHitsShip(bullets[i], player)) {
        bullets[i].active=false;
        playerLives--;
        tone(BUZZER_PIN,150,50); delay(60); noTone(BUZZER_PIN);
                if (playerLives <= 0) {
          // Start disintegration 
          if (!playerDisintegrating) {
            playerDisintegrating = true;
            playerDisintegrateStart = millis();

            // Freeze player motion so it doesn't drift during the effect
            player.dx = 0;
            player.dy = 0;
          }
        }

      }
    }
  }

  drawFrame();
  delay(30);
}

// ============================================================================
// PUBLIC WATCHOS-FRIENDLY API
// ============================================================================

void ironTidesEnter() {
  s_gameActive = true;

  // Run original setup once (so re-entering doesn’t re-init I2C forever)
  if (!s_setupRan) {
    ironTidesSetup_Internal();
    s_setupRan = true;
  }
}

void ironTidesUpdate() {
  if (!s_gameActive) return;
  ironTidesLoop_Internal();
}

void ironTidesExit() {
  s_gameActive = false;

  noTone(BUZZER_PIN);

}