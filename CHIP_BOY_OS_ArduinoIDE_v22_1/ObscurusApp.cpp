// ============================================================
//  OBSCURUS — CHIP-BOY (WatchOS App Module)
//  Display: SSD1306 (I2C D0/D1, addr 0x3C)
//  Buttons: MCP23008 (BTN1/2/3 + nav switch)
//  Pause:   Encoder
//
//  Controls in game:
//    BTN2  (GP6) = JUMP (double jump)
//    BTN1  (GP0) = ATTACK / SHOOT
//    BTN3  (GP1) = CAST SPELL (once learned)
//    DOWN  (GP4) = switch to bow/switch to sword
//    LEFT  (GP2) = move left
//    RIGHT (GP5) = move right
//    ENC SW      = PAUSE
//
//  Pause menu:
//    BTN1 = PLAY (resume)
//    BTN3 = EXIT (back to games menu)
// ============================================================

#include "ObscurusApp.h"
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <math.h>

// ============================================================================
// OS APP WRAPPER STATE
// ============================================================================
static bool ob_active   = false;
static bool ob_setupRan = false;
static bool ob_inProgress = false;  // true while a game session is live (survives exit-to-menu)

bool obscurusIsActive() { return ob_active; }

extern void gameExitToMenu();
static void ob_initBoss();
static void ob_checkArrowHitBoss(int i);
static void ob_bossDeathSequence();
static void ob_shakeRoom(int steps);
static void ob_drawPlayer();
static void ob_drawSprite14(const uint8_t src[14][14], int ox, int oy, bool mirror, uint16_t color);
static void ob_drawSprite7(const uint8_t src[7][7], int ox, int oy, bool mirror, uint16_t color, bool flipV = false);
static void ob_drawRoom();
static void ob_drawTurret();
static void ob_drawBat();
static void ob_drawBullets();
static void ob_drawArrows();
static void ob_drawHearts();
static void ob_drawHUD();
static void obscurusSetup_Internal();
static void ob_doGameOver();
static void ob_drawFrame();
// ============================================================================
// SHARED HARDWARE
// ============================================================================
#define OB_SCREEN_W  128
#define OB_SCREEN_H   64
#define OB_OLED_ADDR  0x3C

extern Adafruit_SSD1306 display;
extern bool displayReady;

// ============================================================================
// MCP23008
// ============================================================================
#define OB_MCP_ADDR  0x20
#define OB_MCP_IODIR 0x00
#define OB_MCP_GPPU  0x06
#define OB_MCP_GPIO  0x09

#define OB_GP_BTN1   0
#define OB_GP_BTN3   1
#define OB_GP_LEFT   2
#define OB_GP_UP     3
#define OB_GP_DOWN   4
#define OB_GP_RIGHT  5
#define OB_GP_BTN2   6

#ifndef OB_BUZZER_PIN
#define OB_BUZZER_PIN D2
#endif

static uint8_t ob_mcpGpio  = 0xFF;
static uint8_t ob_prevGpio = 0xFF;

static bool ob_mcpWrite(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(OB_MCP_ADDR);
  Wire.write(reg); Wire.write(val);
  return Wire.endTransmission() == 0;
}
static bool ob_mcpRead(uint8_t &val) {
  Wire.beginTransmission(OB_MCP_ADDR);
  Wire.write(OB_MCP_GPIO);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((int)OB_MCP_ADDR, 1) != 1) return false;
  val = Wire.read();
  return true;
}
static inline bool ob_btnDown(uint8_t p)        { return !(ob_mcpGpio & (1u << p)); }
static inline bool ob_btnJustPressed(uint8_t p) { return !(ob_mcpGpio & (1u << p)) && (ob_prevGpio & (1u << p)); }

// ============================================================================
// ENCODER SWITCH
// ============================================================================
static const unsigned long OB_ENC_DEBOUNCE = 35;
static bool   ob_encSwStable    = HIGH;
static bool   ob_encSwLastRead  = HIGH;
static unsigned long ob_encSwLastChange = 0;

static bool ob_encPressed() {
  bool r = (ob_mcpGpio & (1u << 7)) ? HIGH : LOW;
  if (r != ob_encSwLastRead) { ob_encSwLastRead = r; ob_encSwLastChange = millis(); }
  if ((millis() - ob_encSwLastChange) >= OB_ENC_DEBOUNCE && r != ob_encSwStable) {
    bool fell = (ob_encSwStable == HIGH && r == LOW);
    ob_encSwStable = r;
    if (fell) return true;
  }
  return false;
}

// ============================================================================
// PAUSE STATE
// ============================================================================
static bool ob_paused             = false;
static bool ob_pauseNeedsRelease  = false;
static unsigned long ob_pauseIgnoreUntil = 0;
static bool ob_pauseBtn1WasDown   = false;
static bool ob_pauseBtn3WasDown   = false;

// ============================================================================
// TILE / MAP CONSTANTS
// ============================================================================
#define OB_TILE_SIZE  8
#define OB_MAP_COLS   16
#define OB_MAP_ROWS   8
#define OB_TILE_EMPTY 0
#define OB_TILE_SOLID 1

// ── Open-world layout (rooms 0-11 stitched into a 4×3 scrolling map) ────
// Room grid:  Rm0  Rm1  Rm2  Rm3   (grid row 0, tile rows  0-7 )
//             Rm4  Rm5  Rm6  Rm7   (grid row 1, tile rows  8-15)
//             Rm8  Rm9  Rm10w Rm11w (grid row 2, tile rows 16-23)
// Rm11w right wall = boss door → enters special rooms 10 & 11 (screen-flip)
#define OB_WORLD_TILE_COLS  64   // 4 rooms × 16 tiles
#define OB_WORLD_TILE_ROWS  24   // 3 rooms ×  8 tiles
#define OB_WORLD_PX_W       512  // 64 × 8
#define OB_WORLD_PX_H       192  // 24 × 8

// Boss door: right wall of Rm11w — 8×16 sprite (1 tile wide, 2 tiles tall)
// Sits on the floor: Rm11w wyOrig=128, tile rows 5-6 → world y 168-183.
// Sprite is drawn at world x = OB_WORLD_PX_W - 8 (last 8px column of world).
// Physics trigger fires when player touches x=OB_WORLD_PX_W while vertically
// overlapping the door gap.
#define OB_BOSS_DOOR_Y0   168   // world y of door top    (128 + 5*8)
#define OB_BOSS_DOOR_Y1   184   // world y of door bottom (128 + 7*8 = floor row)

// Camera — top-left of the 128×64 viewport in world pixels
static int16_t ob_camX = 0;
static int16_t ob_camY = 0;

// Flat world tile array built at game-start from rooms 0-9
static uint8_t ob_worldTiles[OB_WORLD_TILE_ROWS][OB_WORLD_TILE_COLS];

// true  = classic screen-flip mode for rooms 10 / 11
// false = open-world scrolling for rooms 0-9
static bool ob_inSpecialRoom = false;

// World-space position saved when entering a special room
// so we can return the player to the right spot on exit
static int16_t ob_specialReturnWorldX = 0;
static int16_t ob_specialReturnWorldY = 0;


// ============================================================================
// PHYSICS CONSTANTS
// ============================================================================
#define OB_PLAYER_W        6
#define OB_PLAYER_H        8
#define OB_GRAVITY         1
#define OB_JUMP_VY        -7
#define OB_JUMP_VY2       -6
#define OB_WALK_SPEED      2
#define OB_MAX_FALL_SPEED  6
#define OB_MAX_JUMPS       2

// ============================================================================
// WEAPON CONSTANTS
// ============================================================================
#define OB_WEAPON_SWORD  0
#define OB_WEAPON_BOW    1
#define OB_ATTACK_FRAMES 3
#define OB_SWORD_LEN     5
#define OB_BOW_FRAMES    4
#define OB_ARROW_LEN     4
#define OB_ARROW_SPEED   4

// ============================================================================
// WORLD / TURRET / BULLET CONSTANTS
// ============================================================================
#define OB_NUM_ROOMS             12
#define OB_NO_ROOM               0xFF
#define OB_MAX_HP                5
#define OB_INVINCIBLE_FRAMES     40
#define OB_TURRET_W              4
#define OB_TURRET_H              6
#define OB_BARREL_LEN            2
#define OB_TURRET_SHOOT_INTERVAL 60
#define OB_SIGHT_Y_TOLERANCE     4
#define OB_MAX_BULLETS           4
#define OB_BULLET_SPEED          3
#define OB_MAX_ARROWS            2

// ── Bat constants ────────────────────────────────────────────
#define OB_BAT_W            7
#define OB_BAT_H            7
#define OB_BAT_SPEED        1          // pixels per frame while chasing
#define OB_BAT_WAIT_FRAMES  15         // ~0.5 s at 33 ms/frame
#define OB_BAT_FLEE_FRAMES  60         // ~2 s flee after hitting player

// ── Boss constants ────────────────────────────────────────────
#define OB_BOSS_ROOM          10     // room where boss lives
#define OB_BOSS_W             12     // twice the player width (6 * 2)
#define OB_BOSS_H             16     // twice the player height (8 * 2)
#define OB_BOSS_WEAPON_W      14     // boss weapon sprite width
#define OB_BOSS_WEAPON_H      14     // boss weapon sprite height
#define OB_BOSS_MAX_HP        5      // hits to kill
#define OB_BOSS_SPEED         1      // pixels per frame
#define OB_BOSS_INVINCIBLE_FRAMES 50
#define OB_BOSS_CHARGE_INTERVAL  90  // frames between charges (~3 s)
// ============================================================================
//  SPRITE DEFINITIONS  (7×7 pixels, 1=ON 0=OFF)
//  Row 0 = top row,  Row 6 = bottom row.
//  Col 0 = leftmost, Col 6 = rightmost.
//  Same rules as the sword/bow sprites above them.
// ============================================================================

// SWORD sprite — do not rename
static const uint8_t ob_kSwordSprite[7][7] PROGMEM = {
  { 0,0,0,0,0,0,0 },
  { 0,1,0,0,0,0,0 },
  { 1,1,1,1,1,1,1 },
  { 0,1,0,0,0,0,0 },
  { 0,0,0,0,0,0,0 },
  { 0,0,0,0,0,0,0 },
  { 0,0,0,0,0,0,0 },
};

// BOW sprite — do not rename
static const uint8_t ob_kBowSprite[7][7] PROGMEM = {
  { 1,0,0,0,0,0,0 },
  { 1,1,0,0,0,0,0 },
  { 1,0,1,0,0,0,0 },
  { 1,1,1,1,1,0,0 },
  { 1,0,1,0,0,0,0 },
  { 1,1,0,0,0,0,0 },
  { 1,0,0,0,0,0,0 },
};

// ── KEY sprite ──────────────────────────────────
static const uint8_t ob_kKeySprite[7][7] PROGMEM = {
  { 0,0,0,1,0,0,0 },  
  { 0,0,1,0,1,0,0 },  
  { 0,0,1,0,1,0,0 },  
  { 0,0,0,1,0,0,0 },  
  { 0,0,0,1,0,0,0 },   
  { 0,0,1,1,0,0,0 },  
  { 0,0,1,1,0,0,0 },   
};

// ── CHEST sprite ────────────────────────────────
static const uint8_t ob_kChestSprite[7][7] PROGMEM = {
  { 1,1,1,1,1,1,1 },  
  { 1,0,0,0,0,0,1 },   
  { 1,1,1,1,1,1,1 },  
  { 1,0,0,1,0,0,1 },   
  { 1,0,0,0,0,0,1 },   
  { 1,0,0,0,0,0,1 },   
  { 1,1,1,1,1,1,1 },   
};


// ── SPELL BOOK sprite ──────────────────────────

static const uint8_t ob_kSpellBookSprite[7][7] PROGMEM = {
  { 1,1,1,1,1,0,0 },   
  { 1,0,0,0,1,1,0 },   
  { 1,1,0,1,1,1,0 }, 
  { 1,0,1,0,1,1,0 },  
  { 1,0,0,0,1,1,0 },
  { 1,1,1,1,1,1,0 },  
  { 0,1,1,1,1,1,0 },   
};

// ── TOMBSTONE sprite ───────────────────────────
static const uint8_t ob_kTombstoneSprite[7][7] PROGMEM = {
  { 0,0,1,1,1,0,0 },   
  { 0,1,0,0,0,1,0 },   
  { 1,0,1,1,1,0,1 },   
  { 1,0,0,0,0,0,1 },   
  { 1,0,1,1,1,0,1 },  
  { 1,0,0,0,0,0,1 },  
  { 1,1,1,1,1,1,1 },   
};

// ── MAGIC ORB sprite ───
static const uint8_t ob_kOrbSprite[7][7] PROGMEM = {
  { 0,0,1,1,1,0,0 },  
  { 0,1,0,1,1,1,0 },   
  { 1,0,1,1,1,1,1 },   
  { 1,0,1,1,1,1,1 },  
  { 1,1,1,1,1,1,1 },   
  { 0,1,1,1,1,1,0 },  
  { 0,0,1,1,1,0,0 },   
};
// ── BAT sprite ──────────────────────────────────

static const uint8_t ob_kBatSprite[7][7] PROGMEM = {
  { 0,0,0,0,0,0,0 },  
  { 0,0,1,0,1,0,0 },   
  { 1,1,1,1,1,1,1 },   
  { 1,0,0,1,0,0,1 },  
  { 0,1,0,0,0,1,0 },   
  { 0,0,1,1,1,0,0 },   
  { 0,0,1,0,1,0,0 },   
};

// ── PLAYER sprite — 6 wide × 8 tall ───────────
//  Col 0 = left, Col 5 = right.  Row 0 = top, Row 7 = bottom.
//  The sprite is drawn facing RIGHT; it is horizontally mirrored
//  automatically when the player faces left.
//  Legs (rows 5-7) swap between two poses: standing and airborne.
static const uint8_t ob_kPlayerSprite[9][6] PROGMEM = {
  { 0,1,0,0,1,0 },   // row 0 
  { 0,0,1,1,0,0 },   // row 1  
  { 1,0,1,1,0,0 },   // row 2 
  { 0,1,1,1,1,0 },   // row 3 
  { 0,1,1,1,1,0 },   // row 4 
  { 0,1,1,1,1,0 },   // row 5  
  { 0,1,1,1,1,0 },   // row 6  
  { 0,1,1,1,1,0 },   // row 7  
  { 0,1,1,1,1,0 },   // row 8  
};

// ── LIFE (HP pip) sprite — 5 wide × 5 tall ────
//  Drawn in the HUD for each full life remaining.
//  col:  0 1 2 3 4
static const uint8_t ob_kLifeSprite[5][5] PROGMEM = {
  { 0,1,0,1,0 },   // row 0
  { 1,1,1,1,1 },   // row 1
  { 1,1,1,1,1 },   // row 2
  { 0,1,1,1,0 },   // row 3
  { 0,0,1,0,0 },   // row 4
};
// Row 0 = top,  Row 13 = bottom
// Col 0 = left edge when boss faces RIGHT  (auto-flipped when facing left)
static const uint8_t ob_kBossSprite[14][14] PROGMEM = {
  { 0,0,1,1,1,0,0,0,0,1,1,1,0,0 },  // row  7
  { 0,1,0,0,1,1,0,0,1,1,0,0,1,0 },  // row  8 
  { 1,0,0,0,1,1,1,1,1,1,0,0,0,1 },  // row  3
  { 1,0,0,1,1,1,1,1,1,1,1,0,0,1 },  // row  7
  { 1,0,0,1,1,1,1,1,1,1,1,0,0,1 },  // row  8  
  { 0,1,0,1,1,0,0,1,0,0,1,0,1,0 },  // row  6
  { 0,1,0,1,1,0,0,1,0,0,1,0,1,0 },  // row  7
  { 0,1,0,1,1,1,1,1,1,1,1,0,1,0 },  // row  8  
  { 1,0,0,1,1,1,1,0,1,1,1,0,0,1 },  // row  9
  { 0,0,1,1,0,1,0,1,0,1,0,1,0,0 },  // row 10
  { 0,1,0,0,1,0,1,0,1,0,0,0,1,0 },  // row 11
  { 0,1,0,0,1,1,1,1,1,1,0,0,1,0 },  // row 12  
  { 0,0,1,0,0,0,0,0,0,0,0,1,0,0 },  // row 13
};

// ── BOSS attack sprite — 14×14 pixels ──────────
// Col 0 = edge attached to boss when swinging RIGHT
static const uint8_t ob_kBossWeaponSprite[14][14] PROGMEM = {
  { 0,0,0,0,0,0,0,0,0,0,0,0,0,0 },  
  { 0,0,0,0,0,0,0,0,0,0,0,0,0,0 }, 
  { 0,0,1,1,0,0,0,0,1,1,0,0,0,1 },  
  { 0,1,0,0,1,0,0,1,0,0,1,0,1,0 },  
  { 1,0,0,0,0,1,1,0,0,0,0,1,0,1 }, 
  { 0,0,0,0,0,0,0,0,0,0,0,0,0,0 },
  { 0,0,1,1,0,0,0,0,1,1,0,0,0,1 }, 
  { 0,1,0,0,1,0,0,1,0,0,1,0,1,0 }, 
  { 1,0,0,0,0,1,1,0,0,0,0,1,0,1 }, 
  { 0,0,0,0,0,0,0,0,0,0,0,0,0,0 }, 
  { 0,0,1,1,0,0,0,0,1,1,0,0,0,1 },  
  { 0,1,0,0,1,0,0,1,0,0,1,0,1,0 }, 
  { 1,0,0,0,0,1,1,0,0,0,0,1,0,1 },  
  { 0,0,0,0,0,0,0,0,0,0,0,0,0,0 },  
};

// ── BOSS DOOR sprite — 8 wide × 16 tall (one tile wide, two tiles tall) ──
//  Drawn flush to the right wall of Rm11w, sitting on the floor.
//  Col 0 = leftmost pixel of door, col 7 = wall face (right world edge).
//  Row 0 = top of door, row 15 = threshold at floor level.
//
//  col:  0 1 2 3 4 5 6 7
static const uint8_t ob_kBossDoorSprite[16][8] PROGMEM = {
  { 1,1,1,1,1,1,1,1 }, 
  { 1,0,0,0,0,0,0,1 },  
  { 1,0,0,0,0,0,0,1 },  
  { 1,1,1,1,1,1,1,1 },  
  { 1,1,1,1,1,1,1,1 },  
  { 1,1,0,0,0,0,1,1 },  
  { 1,0,0,0,0,0,0,1 },   
  { 1,0,1,0,0,1,0,1 }, 
  { 1,0,0,0,0,0,0,1 },  
  { 1,1,0,1,0,0,1,1 },  
  { 1,1,0,0,0,0,1,1 },  
  { 1,1,1,1,1,1,1,1 },  
  { 1,1,1,1,1,1,1,1 },  
  { 1,0,0,0,0,0,0,1 },  
  { 1,0,0,0,0,0,0,1 }, 
  { 1,1,1,1,1,1,1,1 },  
};
// ============================================================================
//  KEY & CHEST PLACEMENT
//  Set the room number and pixel position
// ============================================================================
#define OB_KEY_ROOM   7       // room that contains the key
#define OB_KEY_X      64     
#define OB_KEY_Y      48     

#define OB_CHEST_ROOM 11      // room that contains the chest
#define OB_CHEST_X    88      // pixel x
#define OB_CHEST_Y    49      // pixel y 

// ── Spell Book placement ────────────────────────────
#define OB_SPELLBOOK_ROOM  8
#define OB_SPELLBOOK_X     10   
#define OB_SPELLBOOK_Y     16  

// ── Tombstone placement (Room 3) ─────────────────────────────
#define OB_TOMBSTONE_ROOM  3
#define OB_TOMBSTONE_X     110  
#define OB_TOMBSTONE_Y     18   

// ── World-space item coordinates (room grid offset + local position) ──────

#define OB_KEY_WORLD_X        (384 + OB_KEY_X)
#define OB_KEY_WORLD_Y        (64  + OB_KEY_Y)

#define OB_SPELLBOOK_WORLD_X  (0   + OB_SPELLBOOK_X)
#define OB_SPELLBOOK_WORLD_Y  (128 + OB_SPELLBOOK_Y)

#define OB_TOMBSTONE_WORLD_X  (384 + OB_TOMBSTONE_X)
#define OB_TOMBSTONE_WORLD_Y  (0   + OB_TOMBSTONE_Y)

// ── Screen-space helpers (subtract camera in open-world mode) ─────────────
static inline int16_t ob_toScreenX(int16_t wx) { return ob_inSpecialRoom ? wx : wx - ob_camX; }
static inline int16_t ob_toScreenY(int16_t wy) { return ob_inSpecialRoom ? wy : wy - ob_camY; }

// ============================================================================
// ROOM TILE DATA
// ============================================================================
static const uint8_t ob_kRoom0[OB_MAP_ROWS][OB_MAP_COLS] PROGMEM = {
  { 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1 },
  { 1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0 },
  { 1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0 },
  { 1,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0 },
  { 1,0,0,0,1,1,0,0,0,0,1,1,1,0,0,0 },
  { 1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 },
  { 1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 },
  { 1,1,1,1,1,0,0,0,0,0,0,1,1,1,1,1 },
};
static const uint8_t ob_kRoom1[OB_MAP_ROWS][OB_MAP_COLS] PROGMEM = {
  { 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1 },
  { 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 },
  { 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 },
  { 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 },
  { 0,0,0,0,0,0,0,0,1,1,0,0,1,1,1,1 },
  { 0,0,0,0,0,0,1,1,0,0,0,0,0,1,1,1 },
  { 0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1 },
  { 1,1,1,1,0,0,0,0,0,0,0,0,0,0,1,1 },
};
static const uint8_t ob_kRoom2[OB_MAP_ROWS][OB_MAP_COLS] PROGMEM = {
  { 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1 },
  { 0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1 },
  { 0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,1 },
  { 0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1 },
  { 1,1,1,0,0,0,0,0,0,0,0,0,1,1,1,1 },
  { 1,1,0,0,0,1,0,0,0,0,0,0,0,0,0,0 },
  { 1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0 },
  { 1,0,0,0,0,0,0,0,0,1,1,1,1,1,1,1 },
};
static const uint8_t ob_kRoom3[OB_MAP_ROWS][OB_MAP_COLS] PROGMEM = {
  { 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1 },
  { 1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,1 },
  { 1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,1 },
  { 1,1,1,1,0,0,0,0,0,0,0,0,1,1,1,1 },
  { 1,1,1,0,0,0,0,0,0,1,1,0,0,1,1,1 },
  { 0,0,0,0,0,0,1,1,0,0,0,0,0,0,1,1 },
  { 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1 },
  { 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1 },
};
static const uint8_t ob_kRoom4[OB_MAP_ROWS][OB_MAP_COLS] PROGMEM = {
  { 1,1,1,1,1,0,0,0,0,0,0,1,1,1,1,1 },
  { 1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 },
  { 1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 },
  { 1,0,0,0,1,1,0,0,0,0,1,1,1,1,1,1 },
  { 1,1,0,0,0,0,1,1,1,1,1,1,1,1,1,1 },
  { 1,1,1,0,0,0,0,0,0,0,0,0,0,1,1,1 },
  { 1,1,1,0,0,0,0,0,0,0,0,0,0,0,1,1 },
  { 1,1,1,1,1,1,1,1,1,1,1,0,0,0,1,1 },
};
static const uint8_t ob_kRoom5[OB_MAP_ROWS][OB_MAP_COLS] PROGMEM = {
  { 1,1,1,1,0,0,0,0,0,0,0,0,0,0,1,1 },
  { 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 },
  { 0,0,0,0,1,1,0,0,0,0,0,0,0,0,0,0 },
  { 0,1,0,0,0,1,1,0,0,0,0,0,0,0,1,0 },
  { 0,1,1,0,0,0,0,0,0,1,1,1,1,1,1,0 },
  { 1,1,0,0,0,0,0,0,1,1,1,1,1,1,1,1 },
  { 1,0,0,0,0,0,0,1,1,1,1,1,1,1,1,1 },
  { 1,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1 },
};
static const uint8_t ob_kRoom6[OB_MAP_ROWS][OB_MAP_COLS] PROGMEM = {
  { 1,1,0,0,0,0,0,0,0,1,1,1,1,1,1,1 },
  { 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 },
  { 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 },
  { 0,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0 },
  { 0,1,0,0,0,1,1,0,0,0,0,0,0,0,0,0 },
  { 1,1,0,0,0,0,0,0,0,0,0,0,0,1,1,1 },
  { 1,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1 },
  { 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1 },
};
static const uint8_t ob_kRoom7[OB_MAP_ROWS][OB_MAP_COLS] PROGMEM = {
  { 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1 },
  { 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1 },
  { 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1 },
  { 0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1 },
  { 0,0,0,0,0,0,0,0,0,1,1,0,0,0,0,1 },
  { 1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,1 },
  { 1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,1 },
  { 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1 },
};
static const uint8_t ob_kRoom8[OB_MAP_ROWS][OB_MAP_COLS] PROGMEM = {
  { 1,1,1,1,1,1,1,1,1,1,1,0,0,0,1,1 },
  { 1,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0 },
  { 1,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0 },
  { 1,1,1,1,0,0,0,1,1,0,0,0,1,1,1,1 },
  { 1,1,1,0,0,0,0,1,1,0,0,0,0,0,1,1 },
  { 1,1,0,0,0,1,1,1,1,1,0,0,0,0,0,1 },
  { 1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,1 },
  { 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1 },
};
static const uint8_t ob_kRoom9[OB_MAP_ROWS][OB_MAP_COLS] PROGMEM = {
  { 1,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1 },
  { 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 },
  { 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 },
  { 1,0,0,0,0,0,0,1,1,0,0,0,0,1,1,1 },
  { 1,1,1,0,0,0,0,1,1,1,0,0,0,0,1,1 },
  { 1,1,1,1,1,0,0,1,1,1,1,1,0,0,1,1 },
  { 1,1,1,1,1,0,0,1,1,1,1,1,0,0,1,1 },
  { 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1 },
};
// ── World-grid rooms 10w & 11w (row 2, cols 2-3) — corridor to boss door ──
// Rm10w: left col matches Rm9 right (rows 1-2 open), right col opens to Rm11w
// Rm11w: right wall has door gap rows 3-5 → triggers boss entry when touched
static const uint8_t ob_kRoom10w[OB_MAP_ROWS][OB_MAP_COLS] PROGMEM = {
  { 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1 },  
  { 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 },  
  { 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 }, 
  { 1,1,0,0,0,0,0,0,1,1,1,1,0,0,0,0 },  
  { 1,0,0,0,0,1,1,0,0,0,0,0,0,0,0,0 },
  { 1,0,0,0,1,1,1,0,0,0,0,0,1,1,0,0 },
  { 1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 },
  { 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1 },  
};
static const uint8_t ob_kRoom11w[OB_MAP_ROWS][OB_MAP_COLS] PROGMEM = {
  { 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1 },  
  { 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1 },  
  { 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1 },
  { 0,0,0,0,1,1,1,0,0,0,0,0,0,0,0,1 },  
  { 0,0,0,0,0,0,0,0,1,1,0,0,0,0,0,1 }, 
  { 1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0 },  
  { 1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0 },  
  { 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1 }, 
};
static const uint8_t ob_kRoom10[OB_MAP_ROWS][OB_MAP_COLS] PROGMEM = {
  { 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1 },
  { 1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,1 },
  { 1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,1 },
  { 1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1 },
  { 1,0,0,0,1,0,0,0,0,1,0,0,0,0,1,1 },
  { 0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1 },
  { 0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1 },
  { 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1 },
};

static const uint8_t ob_kRoom10Destroyed[OB_MAP_ROWS][OB_MAP_COLS] PROGMEM = {
  { 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1 },
  { 1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,1 },
  { 1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0 },
  { 1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 },
  { 1,0,0,0,1,0,0,0,0,1,0,0,0,0,0,1 },
  { 0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1 },
  { 0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1 },
  { 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1 },
};
static const uint8_t ob_kRoom11[OB_MAP_ROWS][OB_MAP_COLS] PROGMEM = {
  { 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1 },
  { 1,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1 },
  { 0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1 },
  { 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1 },
  { 1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1 },
  { 1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,1 },
  { 1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,1 },
  { 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1 },
};

// ─── Neighbour table { left, right, up, down } ───────────────
static const uint8_t ob_kNeighbours[OB_NUM_ROOMS][4] PROGMEM = {
  //         LEFT         RIGHT        UP           DOWN
  /* Rm 0 */ { OB_NO_ROOM, 1,           OB_NO_ROOM,  4           },
  /* Rm 1 */ { 0,          2,           OB_NO_ROOM,  5           },
  /* Rm 2 */ { 1,          3,           OB_NO_ROOM,  6           },
  /* Rm 3 */ { 2,          OB_NO_ROOM,  OB_NO_ROOM,  7           },
  /* Rm 4 */ { OB_NO_ROOM, 5,           0,           8           },
  /* Rm 5 */ { 4,          6,           1,           9           },
  /* Rm 6 */ { 5,          7,           2,           OB_NO_ROOM  },
  /* Rm 7 */ { 6,          OB_NO_ROOM,  3,           OB_NO_ROOM  },
  /* Rm 8 */ { OB_NO_ROOM, 9,           4,           OB_NO_ROOM  },
  /* Rm 9 */ { 8,          OB_NO_ROOM,  5,           OB_NO_ROOM  },
  /* Rm10 */ { OB_NO_ROOM, 11,          OB_NO_ROOM,  OB_NO_ROOM  },
  /* Rm11 */ { 10,         OB_NO_ROOM,  OB_NO_ROOM,  OB_NO_ROOM  },
};

typedef const uint8_t ObRoomTiles[OB_MAP_ROWS][OB_MAP_COLS];

// World-grid room table 
static ObRoomTiles* const ob_kWorldRooms[12] PROGMEM = {
  &ob_kRoom0,   &ob_kRoom1,   &ob_kRoom2,  &ob_kRoom3,
  &ob_kRoom4,   &ob_kRoom5,   &ob_kRoom6,  &ob_kRoom7,
  &ob_kRoom8,   &ob_kRoom9,   &ob_kRoom10w, &ob_kRoom11w,
};

// All rooms including special boss rooms (for ob_loadRoom)
static ObRoomTiles* const ob_kRooms[OB_NUM_ROOMS] PROGMEM = {
  &ob_kRoom0,  &ob_kRoom1,  &ob_kRoom2,  &ob_kRoom3,
  &ob_kRoom4,  &ob_kRoom5,  &ob_kRoom6,  &ob_kRoom7,
  &ob_kRoom8,  &ob_kRoom9,  &ob_kRoom10, &ob_kRoom11,
};

static const int8_t ob_kTurretPos[OB_NUM_ROOMS][2] PROGMEM = {
  //  x    y     Room 
  {  56,  18 },  // Rm  0  
  {  56,  34 },  // Rm  1
  {  18,  26 },  // Rm  2 
  {  50,  50 },  // Rm  3
  {  66,  26 },  // Rm  4 
  {  50,  18 },  // Rm  5 
  {  18,  18 },  // Rm  6
  {  72,  26 },  // Rm  7  
  {  26,  18 },  // Rm  8  
  {  34,  34 },  // Rm  9  
  {  34,  26 },  // Rm 10 
  {  72,  50 },  // Rm 11  
};
// Bat anchor
static const int8_t ob_kBatPos[OB_NUM_ROOMS][2] PROGMEM = {
  {  72,  8  },  // Rm  0
  {  64,  8  },  // Rm  1
  {  96,  40  }, // Rm  2
  {  80,  8  },  // Rm  3
  {  48,  40 },  // Rm  4 
  {  112,  8 },  // Rm  5  
  { -1,  -1  },  // Rm  6  
  {  56,  8  },  // Rm  7
  { -1,  -1  },  // Rm  8  
  {  80,  8  },  // Rm  9
  {  64,  8  },  // Rm 10
  { -1,  -1  },  // Rm 11  
};

// World-space turret/bat for the two world corridor rooms 
static const int8_t ob_kWorldCorridorTurretPos[2][2] PROGMEM = {
  {  72,  50 },  
  {  48,  50 },  
};
static const int8_t ob_kWorldCorridorBatPos[2][2] PROGMEM = {
  {  96,  8  },  
  {  96,  8  }, 
};
// ============================================================================
// STRUCTS
// ============================================================================
struct ObPlayer {
  int16_t x, y, vx, vy;
  bool    onGround, facingRight, jumpQueued;
  uint8_t jumpsLeft, walkFrame, walkTick;
  uint8_t attackFrames;
  uint8_t bowFrames;
  int8_t  hp;
  uint8_t invincibleFrames;
  uint8_t weapon;
};
struct ObTurret {
  int16_t x, y;
  bool    alive;
  uint8_t shootTimer;
  uint8_t blinkFrames;
};
struct ObBullet { int16_t x, y; int8_t vx; bool active; };
struct ObArrow  { int16_t x, y; int8_t vx; bool active; };

enum ObBatState {
  OB_BAT_IDLE,    // hanging, waiting for player to enter room
  OB_BAT_WAIT,    // player entered — counting down before launch
  OB_BAT_CHASE,   // flying toward player
  OB_BAT_FLEE,    // hit player — flying away for 2 s
  OB_BAT_DEAD     // killed this session
};

struct ObBat {
  int16_t    x, y;          // current pixel position (top-left of sprite)
  int16_t    anchorX, anchorY;  // spawn/reset position
  ObBatState state;
  uint8_t    timer;          // countdown for WAIT and FLEE states
  bool       alive;          // false = no bat in this room
  bool       killedThisRun;  // permanent kill (like turrets)
  uint8_t    flutterFrame;   // simple oscillation for hanging animation
};

struct ObBoss {
  int16_t  x, y;
  int16_t  vx;
  bool     alive;
  bool     killedPermanent;   // stays dead for the whole run
  int8_t   hp;
  uint8_t  invincibleFrames;
  bool     facingRight;
  bool     weaponOut;         // true while attack swing is showing
  uint8_t  weaponFrames;      // countdown for weapon display
  uint8_t  chargeTimer;       // counts down to next charge
  bool     introShown;        // boss intro card shown this visit
};
// ============================================================================
// GAME GLOBALS
// ============================================================================
static ObPlayer  ob_player;
static uint8_t   ob_currentRoom = 0;
static uint8_t   ob_roomTiles[OB_MAP_ROWS][OB_MAP_COLS];
static ObBullet  ob_bullets[OB_MAX_BULLETS];
static ObArrow   ob_arrows[OB_MAX_ARROWS];
static ObBoss    ob_boss;

// ── Open-world enemies: one persistent entry per world-grid room (0-11w) ──
// Rooms 0-9 use ob_kTurretPos/ob_kBatPos.
// Rooms 10w/11w (indices 10-11 here) use ob_kWorldCorridorTurretPos/BatPos.
#define OB_WORLD_ROOMS 12
static ObTurret ob_worldTurrets[OB_WORLD_ROOMS];
static ObBat    ob_worldBats[OB_WORLD_ROOMS];

// ── Single turret/bat for special rooms 10 & 11 (screen-space) ───────────
static ObTurret ob_turret;
static ObBat    ob_bat;

// ── Key / Chest state ────────────────────────────────────────
static bool ob_playerHasKey = false;   // set true when key is touched
static bool ob_keyPickedUp  = false;   // latched true so key never reappears
static bool ob_chestOpened  = false;   // latched true after cleared screen

// ── Spell Book / Tombstone state ─────────────────────────────
static bool ob_spellBookPickedUp  = false;  // player has the spell book
static bool ob_spellBookFound     = false;  // intro screen shown once
static bool ob_tombstoneRead      = false;  // tomb message has been deciphered (spell learned)
static bool ob_tombstoneDone      = false;  // tomb permanently inert after spell learned
static unsigned long ob_tombstoneCooldownUntil = 0; // millis() after which tomb can trigger again

// ── Spell of Fortitude state ─────────────────────────────────
#define OB_FORTITUDE_RADIUS      10   // circle radius in pixels
#define OB_FORTITUDE_ACTIVE_FRAMES  30   // 1 s at 33 ms/frame  (active solid)
#define OB_FORTITUDE_BLINK_TOTAL    45   // 3 × 0.5 s blink (15 frames each)
#define OB_FORTITUDE_COOLDOWN_FRAMES 90  // 3 s cooldown

static uint8_t ob_fortitudeFrames   = 0;   // remaining active+blink frames (0=off)
static uint8_t ob_fortitudeCooldown = 0;   // cooldown countdown

// ============================================================================
// HEART DROP SYSTEM
// ============================================================================
#define OB_MAX_HEARTS    6
#define OB_HEART_W       5
#define OB_HEART_H       5
#define OB_HEART_GRAVITY 1
#define OB_HEART_MAX_FALL 5

struct ObHeart { int16_t x, y; int8_t vy; bool active; };
static ObHeart ob_hearts[OB_MAX_HEARTS];

static void ob_clearHearts() {
  for (int i = 0; i < OB_MAX_HEARTS; i++) ob_hearts[i].active = false;
}
static void ob_spawnHeart(int16_t x, int16_t y) {
  for (int i = 0; i < OB_MAX_HEARTS; i++) {
    if (!ob_hearts[i].active) {
      ob_hearts[i] = { x, y, 0, true };
      return;
    }
  }
}

// ============================================================================
// GAME-OVER FLAG
// ============================================================================
static bool ob_gameOver = false;

// ============================================================================
// TILE HELPERS
// ============================================================================
static uint8_t ob_getTile(int c, int r) {
  if (ob_inSpecialRoom) {
    if (c < 0 || c >= OB_MAP_COLS || r < 0 || r >= OB_MAP_ROWS) return OB_TILE_SOLID;
    return ob_roomTiles[r][c];
  } else {
    if (c < 0 || c >= OB_WORLD_TILE_COLS || r < 0 || r >= OB_WORLD_TILE_ROWS) return OB_TILE_SOLID;
    return ob_worldTiles[r][c];
  }
}
static bool ob_rectOverlapsSolid(int16_t px, int16_t py, int16_t pw, int16_t ph) {
  for (int r = py/OB_TILE_SIZE; r <= (py+ph-1)/OB_TILE_SIZE; r++)
    for (int c = px/OB_TILE_SIZE; c <= (px+pw-1)/OB_TILE_SIZE; c++)
      if (ob_getTile(c,r) == OB_TILE_SOLID) return true;
  return false;
}
static bool ob_rectsOverlap(int16_t ax,int16_t ay,int16_t aw,int16_t ah,
                             int16_t bx,int16_t by,int16_t bw,int16_t bh) {
  return (ax < bx+bw && ax+aw > bx && ay < by+bh && ay+ah > by);
}
static void ob_clearBullets() { for (int i=0;i<OB_MAX_BULLETS;i++) ob_bullets[i].active=false; }
static void ob_clearArrows()  { for (int i=0;i<OB_MAX_ARROWS; i++) ob_arrows[i].active=false;  }

// Returns true if Spell of Fortitude shield is currently blocking damage
static bool ob_fortitudeActive() { return ob_fortitudeFrames > 0; }

// ============================================================================
// WORLD BUILDER  (rooms 0-9 stitched into one flat tile array at game-start)
// ============================================================================
// Room N is at grid col = N%4, row = N/4.
// Rooms 10/11 are NOT in the world grid — they stay as secret special rooms.
static void ob_buildWorld() {
  memset(ob_worldTiles, OB_TILE_SOLID, sizeof(ob_worldTiles));
  for (int rm = 0; rm < 12; rm++) {
    int tc0 = (rm % 4) * OB_MAP_COLS;
    int tr0 = (rm / 4) * OB_MAP_ROWS;
    ObRoomTiles* src = (ObRoomTiles*)pgm_read_ptr(&ob_kWorldRooms[rm]);
    for (int r = 0; r < OB_MAP_ROWS; r++)
      for (int c = 0; c < OB_MAP_COLS; c++)
        ob_worldTiles[tr0 + r][tc0 + c] = pgm_read_byte(&((*src)[r][c]));
  }
}

// Camera update — called each open-world frame after physics
static void ob_updateCamera() {
  ob_camX = ob_player.x + OB_PLAYER_W / 2 - OB_SCREEN_W / 2;
  ob_camY = ob_player.y + OB_PLAYER_H / 2 - OB_SCREEN_H / 2;
  if (ob_camX < 0) ob_camX = 0;
  if (ob_camY < 0) ob_camY = 0;
  if (ob_camX > OB_WORLD_PX_W - OB_SCREEN_W) ob_camX = OB_WORLD_PX_W - OB_SCREEN_W;
  if (ob_camY > OB_WORLD_PX_H - OB_SCREEN_H) ob_camY = OB_WORLD_PX_H - OB_SCREEN_H;
}

// Initialise all open-world enemy state from PROGMEM tables.
// Called ONCE at game-start. Enemies are never reloaded after this.
static void ob_initWorldEnemies() {
  for (uint8_t rm = 0; rm < OB_WORLD_ROOMS; rm++) {
    int16_t wxOrig = (int16_t)(rm % 4) * OB_SCREEN_W;
    int16_t wyOrig = (int16_t)(rm / 4) * OB_SCREEN_H;

    int8_t tx, ty, bx, by;
    if (rm < 10) {
      // Rooms 0-9: use the main PROGMEM tables
      tx = (int8_t)pgm_read_byte(&ob_kTurretPos[rm][0]);
      ty = (int8_t)pgm_read_byte(&ob_kTurretPos[rm][1]);
      bx = (int8_t)pgm_read_byte(&ob_kBatPos[rm][0]);
      by = (int8_t)pgm_read_byte(&ob_kBatPos[rm][1]);
    } else {
      // Rooms 10w/11w (indices 10-11): use corridor tables
      uint8_t ci = rm - 10;
      tx = (int8_t)pgm_read_byte(&ob_kWorldCorridorTurretPos[ci][0]);
      ty = (int8_t)pgm_read_byte(&ob_kWorldCorridorTurretPos[ci][1]);
      bx = (int8_t)pgm_read_byte(&ob_kWorldCorridorBatPos[ci][0]);
      by = (int8_t)pgm_read_byte(&ob_kWorldCorridorBatPos[ci][1]);
    }

    ob_worldTurrets[rm].x           = wxOrig + tx;
    ob_worldTurrets[rm].y           = wyOrig + ty;
    ob_worldTurrets[rm].alive       = true;
    ob_worldTurrets[rm].shootTimer  = OB_TURRET_SHOOT_INTERVAL;
    ob_worldTurrets[rm].blinkFrames = 0;

    if (bx < 0) {
      ob_worldBats[rm].alive = false;
    } else {
      ob_worldBats[rm].alive        = true;
      ob_worldBats[rm].anchorX      = wxOrig + bx;
      ob_worldBats[rm].anchorY      = wyOrig + by;
      ob_worldBats[rm].x            = wxOrig + bx;
      ob_worldBats[rm].y            = wyOrig + by;
      ob_worldBats[rm].state        = OB_BAT_IDLE;
      ob_worldBats[rm].timer        = OB_BAT_WAIT_FRAMES;
      ob_worldBats[rm].flutterFrame = 0;
      ob_worldBats[rm].killedThisRun = false;
    }
  }
}

// ============================================================================
// CLASSIC ROOM LOAD  (special rooms 10 & 11 only)
// ============================================================================
static void ob_loadRoom(uint8_t idx) {
  ob_currentRoom = idx;
  ObRoomTiles* src;
  if (idx == OB_BOSS_ROOM && ob_boss.killedPermanent)
    src = (ObRoomTiles*)ob_kRoom10Destroyed;
  else
    src = (ObRoomTiles*)pgm_read_ptr(&ob_kRooms[idx]);
  for (int r = 0; r < OB_MAP_ROWS; r++)
    for (int c = 0; c < OB_MAP_COLS; c++)
      ob_roomTiles[r][c] = pgm_read_byte(&((*src)[r][c]));
  ob_turret.x           = (int8_t)pgm_read_byte(&ob_kTurretPos[idx][0]);
  ob_turret.y           = (int8_t)pgm_read_byte(&ob_kTurretPos[idx][1]);
  ob_turret.alive       = true;   // special-room turret always starts alive (boss room resets each visit)
  ob_turret.shootTimer  = OB_TURRET_SHOOT_INTERVAL;
  ob_turret.blinkFrames = 0;
  ob_clearBullets();
  ob_clearArrows();
  int8_t bx = (int8_t)pgm_read_byte(&ob_kBatPos[idx][0]);
  int8_t by = (int8_t)pgm_read_byte(&ob_kBatPos[idx][1]);
  if (bx < 0) {
    ob_bat.alive = false;
  } else {
    ob_bat.alive        = true;
    ob_bat.anchorX      = bx;
    ob_bat.anchorY      = by;
    ob_bat.x            = bx;
    ob_bat.y            = by;
    ob_bat.state        = OB_BAT_IDLE;
    ob_bat.timer        = OB_BAT_WAIT_FRAMES;
    ob_bat.flutterFrame = 0;
  }
  if (idx == OB_BOSS_ROOM && !ob_boss.killedPermanent) {
    ob_initBoss();
  }
}

// ============================================================================
// SPECIAL-ROOM ENTRY / EXIT
// ============================================================================
// Rm10 is entered from the bottom of Rm6's column (world tileCol 32-47).
// Rm11 is entered from the bottom of Rm7's column (world tileCol 48-63).
static void ob_enterSpecialRoom(uint8_t roomIdx) {
  ob_specialReturnWorldX = OB_WORLD_PX_W - OB_PLAYER_W - 2;
  ob_specialReturnWorldY = OB_BOSS_DOOR_Y0 + (OB_BOSS_DOOR_Y1 - OB_BOSS_DOOR_Y0) / 2 - OB_PLAYER_H;
  ob_inSpecialRoom = true;

  if (roomIdx == OB_BOSS_ROOM && !ob_boss.killedPermanent) {
    // ── Step 1: load destroyed layout so player sees it first ────
    for (int r = 0; r < OB_MAP_ROWS; r++)
      for (int c = 0; c < OB_MAP_COLS; c++)
        ob_roomTiles[r][c] = pgm_read_byte(&ob_kRoom10Destroyed[r][c]);
    ob_currentRoom = roomIdx;

    // Place player at entry position before drawing
    ob_player.x  = 12;
    ob_player.y  = OB_SCREEN_H - OB_PLAYER_H - OB_TILE_SIZE;
    ob_player.vx = 0;
    ob_player.vy = 0;

    // Draw one frame so player sees the destroyed room
    ob_drawFrame();
    display.display();
    delay(400);

    // ── Step 2: init boss position early so it appears during shake ──
    ob_initBoss();

    // ── Step 3: shake — tiles + boss + player all visible ────────────
    {
      static const int8_t shakeX[] = { 3,-3, 4,-4, 2,-3, 4,-2, 3,-4, 2,-3 };
      static const int8_t shakeY[] = { 1,-1, 2,-2, 1,-2, 2,-1, 2,-1, 1,-2 };
      for (int i = 0; i < 8; i++) {
        int8_t ox = shakeX[i % 12];
        int8_t oy = shakeY[i % 12];
        display.clearDisplay();
        // Tiles
        for (int r = 0; r < OB_MAP_ROWS; r++)
          for (int c = 0; c < OB_MAP_COLS; c++)
            if (ob_roomTiles[r][c] == OB_TILE_SOLID) {
              int px = c*OB_TILE_SIZE+ox, py = r*OB_TILE_SIZE+oy;
              display.fillRect(px, py, OB_TILE_SIZE, OB_TILE_SIZE, WHITE);
              display.drawPixel(px,                   py,                   BLACK);
              display.drawPixel(px+OB_TILE_SIZE-1,    py,                   BLACK);
              display.drawPixel(px,                   py+OB_TILE_SIZE-1,    BLACK);
              display.drawPixel(px+OB_TILE_SIZE-1,    py+OB_TILE_SIZE-1,    BLACK);
            }
        // Boss
        ob_drawSprite14(ob_kBossSprite, ob_boss.x+ox, ob_boss.y+oy,
                        !ob_boss.facingRight, WHITE);
        // Player
        ob_drawPlayer();
        display.display();
        tone(OB_BUZZER_PIN, 80 + i*20, 60);
        delay(60);
      }
    }

    // ── Step 4: swap to live Room10 ───────────────────────────────
    for (int r = 0; r < OB_MAP_ROWS; r++)
      for (int c = 0; c < OB_MAP_COLS; c++)
        ob_roomTiles[r][c] = pgm_read_byte(&ob_kRoom10[r][c]);

    // ── Step 5: seal entrance — only rows 5 & 6 col 0 (the two open tiles) ─
    ob_roomTiles[5][0] = OB_TILE_SOLID;
    ob_roomTiles[6][0] = OB_TILE_SOLID;

    // ── Step 6: spawn turret/bat ──────────────────────────────────
    ob_turret.x           = (int8_t)pgm_read_byte(&ob_kTurretPos[roomIdx][0]);
    ob_turret.y           = (int8_t)pgm_read_byte(&ob_kTurretPos[roomIdx][1]);
    ob_turret.alive       = true;
    ob_turret.shootTimer  = OB_TURRET_SHOOT_INTERVAL;
    ob_turret.blinkFrames = 0;
    int8_t bx = (int8_t)pgm_read_byte(&ob_kBatPos[roomIdx][0]);
    int8_t by = (int8_t)pgm_read_byte(&ob_kBatPos[roomIdx][1]);
    if (bx < 0) { ob_bat.alive = false; }
    else {
      ob_bat.alive = true; ob_bat.anchorX = bx; ob_bat.anchorY = by;
      ob_bat.x = bx; ob_bat.y = by;
      ob_bat.state = OB_BAT_IDLE; ob_bat.timer = OB_BAT_WAIT_FRAMES;
      ob_bat.flutterFrame = 0;
    }

  } else {
    // Non-boss special rooms (room 11 chest, or killed boss re-entry): load normally
    ob_loadRoom(roomIdx);
    ob_player.x  = 12;
    ob_player.y  = OB_SCREEN_H - OB_PLAYER_H - OB_TILE_SIZE;
    ob_player.vx = 0;
    ob_player.vy = 0;
  }
}

static void ob_exitSpecialRoom() {
  ob_inSpecialRoom = false;
  ob_clearBullets();
  ob_clearArrows();
  // Return to the door gap in Rm11w — just far enough left to clear the wall
  ob_player.x          = OB_WORLD_PX_W - OB_PLAYER_W - 9;
  ob_player.y          = OB_BOSS_DOOR_Y0 + 2;
  ob_player.vx         = -OB_WALK_SPEED;   // walking left, out through the door
  ob_player.vy         = 0;
  ob_player.facingRight = false;
}

// ============================================================================
// CLASSIC TRANSITION  (only within special rooms 10 <-> 11)
// ============================================================================
static void ob_tryTransition(uint8_t dir) {
  uint8_t next = pgm_read_byte(&ob_kNeighbours[ob_currentRoom][dir]);
  if (next == OB_NO_ROOM) {
    if (dir == 2) { ob_exitSpecialRoom(); return; }   // hit ceiling → return to world
    if (dir == 0) { ob_exitSpecialRoom(); return; }   // hit left wall → return to world
    if (dir == 1) { ob_player.x = OB_SCREEN_W - OB_PLAYER_W; ob_player.vx = 0; }
    if (dir == 3) { ob_player.y = OB_SCREEN_H - OB_PLAYER_H; ob_player.vy = 0; }
    return;
  }
  ob_loadRoom(next);
  if (dir == 0) ob_player.x = OB_SCREEN_W - OB_PLAYER_W - 2;
  if (dir == 1) ob_player.x = 2;
  if (dir == 2) ob_player.y = OB_SCREEN_H - OB_PLAYER_H - OB_TILE_SIZE;
  if (dir == 3) { ob_player.y = OB_TILE_SIZE; ob_player.vy = 0; }
}

// ============================================================================
// PHYSICS
// ============================================================================
static void ob_updatePhysics() {
  if (ob_inSpecialRoom) {
    // ── Classic screen-space physics (rooms 10 & 11) ──────────────────────
    int16_t newX = ob_player.x + ob_player.vx;
    if      (newX < 0)                         { ob_tryTransition(0); return; }
    else if (newX + OB_PLAYER_W > OB_SCREEN_W) { ob_tryTransition(1); return; }
    else if (ob_rectOverlapsSolid(newX, ob_player.y, OB_PLAYER_W, OB_PLAYER_H))
      ob_player.vx = 0;
    else
      ob_player.x = newX;

    if (!ob_player.onGround) {
      ob_player.vy += OB_GRAVITY;
      if (ob_player.vy > OB_MAX_FALL_SPEED) ob_player.vy = OB_MAX_FALL_SPEED;
    }
    int16_t newY = ob_player.y + ob_player.vy;
    if      (newY < 0)                         { ob_tryTransition(2); return; }
    else if (newY + OB_PLAYER_H > OB_SCREEN_H) { ob_tryTransition(3); return; }

    ob_player.onGround = false;
    if (ob_rectOverlapsSolid(ob_player.x, newY, OB_PLAYER_W, OB_PLAYER_H)) {
      if (ob_player.vy >= 0) {
        int16_t safeY = ob_player.y;
        for (int s = 1; s <= ob_player.vy; s++) {
          if (ob_rectOverlapsSolid(ob_player.x, ob_player.y + s, OB_PLAYER_W, OB_PLAYER_H)) break;
          safeY = ob_player.y + s;
        }
        ob_player.y         = safeY;
        ob_player.vy        = 0;
        ob_player.onGround  = true;
        ob_player.jumpsLeft = OB_MAX_JUMPS;
      } else {
        ob_player.vy = 0;
      }
    } else {
      ob_player.y = newY;
    }
    if (ob_player.vy == 0 && !ob_player.onGround) {
      if (ob_rectOverlapsSolid(ob_player.x, ob_player.y + 1, OB_PLAYER_W, OB_PLAYER_H)) {
        ob_player.onGround  = true;
        ob_player.jumpsLeft = OB_MAX_JUMPS;
      }
    }

  } else {
    // ── Open-world physics (player.x/y are world coordinates) ─────────────
    int16_t newX = ob_player.x + ob_player.vx;
    if (newX < 0) { newX = 0; ob_player.vx = 0; }

    // Boss door: right wall of Rm11w. Requires key to enter.
    if (newX + OB_PLAYER_W >= OB_WORLD_PX_W) {
      if (ob_player.y + OB_PLAYER_H > OB_BOSS_DOOR_Y0 && ob_player.y < OB_BOSS_DOOR_Y1) {
        if (ob_playerHasKey) {
          ob_enterSpecialRoom(10);
          return;
        }
        // No key — bounce player back (locked door)
        newX = OB_WORLD_PX_W - OB_PLAYER_W - 1;
        ob_player.vx = 0;
      } else {
        newX = OB_WORLD_PX_W - OB_PLAYER_W;
        ob_player.vx = 0;
      }
    }

    if (ob_rectOverlapsSolid(newX, ob_player.y, OB_PLAYER_W, OB_PLAYER_H))
      ob_player.vx = 0;
    else
      ob_player.x = newX;

    if (!ob_player.onGround) {
      ob_player.vy += OB_GRAVITY;
      if (ob_player.vy > OB_MAX_FALL_SPEED) ob_player.vy = OB_MAX_FALL_SPEED;
    }
    int16_t newY = ob_player.y + ob_player.vy;
    if (newY < 0) { newY = 0; ob_player.vy = 0; }
    // World bottom — solid floor clamp
    if (newY + OB_PLAYER_H > OB_WORLD_PX_H) {
      newY = OB_WORLD_PX_H - OB_PLAYER_H;
      ob_player.vy = 0;
      ob_player.onGround  = true;
      ob_player.jumpsLeft = OB_MAX_JUMPS;
      goto physics_jump;
    }

    ob_player.onGround = false;
    if (ob_rectOverlapsSolid(ob_player.x, newY, OB_PLAYER_W, OB_PLAYER_H)) {
      if (ob_player.vy >= 0) {
        int16_t safeY = ob_player.y;
        for (int s = 1; s <= ob_player.vy; s++) {
          if (ob_rectOverlapsSolid(ob_player.x, ob_player.y + s, OB_PLAYER_W, OB_PLAYER_H)) break;
          safeY = ob_player.y + s;
        }
        ob_player.y         = safeY;
        ob_player.vy        = 0;
        ob_player.onGround  = true;
        ob_player.jumpsLeft = OB_MAX_JUMPS;
      } else {
        ob_player.vy = 0;
      }
    } else {
      ob_player.y = newY;
    }
    if (ob_player.vy == 0 && !ob_player.onGround) {
      if (ob_rectOverlapsSolid(ob_player.x, ob_player.y + 1, OB_PLAYER_W, OB_PLAYER_H)) {
        ob_player.onGround  = true;
        ob_player.jumpsLeft = OB_MAX_JUMPS;
      }
    }
  }

  physics_jump:
  if (ob_player.jumpQueued) {
    ob_player.jumpQueued = false;
    if (ob_player.jumpsLeft > 0) {
      bool dj            = !ob_player.onGround;
      ob_player.vy       = dj ? OB_JUMP_VY2 : OB_JUMP_VY;
      ob_player.onGround = false;
      ob_player.jumpsLeft--;
      tone(OB_BUZZER_PIN, dj ? 250 : 250, dj ? 35 : 40);
    }
  }
}

static bool ob_playerInSightOf(const ObTurret& t) {
  return (abs((t.y + OB_TURRET_H/2) - (ob_player.y + OB_PLAYER_H/2)) <= OB_SIGHT_Y_TOLERANCE);
}
static bool ob_playerInSight() {
  return ob_playerInSightOf(ob_turret);
}
static void ob_spawnBulletFrom(const ObTurret& t, bool goRight) {
  for (int i = 0; i < OB_MAX_BULLETS; i++) {
    if (!ob_bullets[i].active) {
      ob_bullets[i].active = true;
      ob_bullets[i].vx     = goRight ? OB_BULLET_SPEED : -OB_BULLET_SPEED;
      ob_bullets[i].x      = goRight ? t.x + OB_TURRET_W + OB_BARREL_LEN
                                     : t.x - OB_BARREL_LEN - 1;
      ob_bullets[i].y      = t.y + OB_TURRET_H / 2;
      return;
    }
  }
}
static void ob_spawnBullet(bool goRight) { ob_spawnBulletFrom(ob_turret, goRight); }

static void ob_killTurret() { ob_turret.blinkFrames = 16; tone(OB_BUZZER_PIN, 250, 80); }

// Update a single world-turret by index.
static void ob_updateWorldTurret(uint8_t rm) {
  ObTurret& t = ob_worldTurrets[rm];
  if (!t.alive) return;
  // Viewport cull — skip if entirely off screen
  int16_t sx = t.x - ob_camX;
  if (sx + OB_TURRET_W + 32 < 0 || sx - 32 >= OB_SCREEN_W) return;

  if (t.blinkFrames > 0) {
    if (--t.blinkFrames == 0) { ob_spawnHeart(t.x, t.y); t.alive = false; }
    return;
  }
  bool shootRight = (ob_player.x > t.x);
  if (ob_playerInSightOf(t)) {
    if (t.shootTimer > 0) t.shootTimer--;
    else { t.shootTimer = OB_TURRET_SHOOT_INTERVAL; ob_spawnBulletFrom(t, shootRight); tone(OB_BUZZER_PIN, 500, 15); }
  }
  if (ob_player.attackFrames > 0) {
    int16_t sw = ob_player.facingRight ? ob_player.x + OB_PLAYER_W : ob_player.x - 7;
    if (ob_rectsOverlap(sw, ob_player.y, 7, OB_PLAYER_H, t.x, t.y, OB_TURRET_W, OB_TURRET_H))
      { t.blinkFrames = 16; tone(OB_BUZZER_PIN, 250, 80); }
  }
}

static void ob_updateTurret() {
  if (!ob_turret.alive) return;
  if (ob_turret.blinkFrames > 0) {
    if (--ob_turret.blinkFrames == 0) { ob_spawnHeart(ob_turret.x, ob_turret.y); ob_turret.alive = false; }
    return;
  }
  bool shootRight = (ob_player.x > ob_turret.x);
  if (ob_playerInSight()) {
    if (ob_turret.shootTimer > 0) ob_turret.shootTimer--;
    else { ob_turret.shootTimer = OB_TURRET_SHOOT_INTERVAL; ob_spawnBullet(shootRight); tone(OB_BUZZER_PIN, 500, 15); }
  }
  if (ob_player.attackFrames > 0) {
    int16_t sx = ob_player.facingRight ? ob_player.x + OB_PLAYER_W : ob_player.x - 7;
    if (ob_rectsOverlap(sx, ob_player.y, 7, OB_PLAYER_H, ob_turret.x, ob_turret.y, OB_TURRET_W, OB_TURRET_H))
      ob_killTurret();
  }
}

// ============================================================================
// BAT
// ============================================================================

// Update a world-bat by index. Bat stays within its room's world-space bounds.
// If player is outside the room, bat returns to anchor instead of chasing.
static void ob_updateWorldBat(uint8_t rm) {
  ObBat& b = ob_worldBats[rm];
  if (!b.alive) return;

  // Room bounds in world pixels
  int16_t roomX0 = (int16_t)(rm % 4) * OB_SCREEN_W;
  int16_t roomY0 = (int16_t)(rm / 4) * OB_SCREEN_H;
  int16_t roomX1 = roomX0 + OB_SCREEN_W;
  int16_t roomY1 = roomY0 + OB_SCREEN_H;

  // Viewport cull — only act when bat is near the screen
  int16_t sx = b.x - ob_camX;
  if (sx + OB_BAT_W + 32 < 0 || sx - 32 >= OB_SCREEN_W) return;

  b.flutterFrame++;

  // Is player inside this bat's room?
  bool playerInRoom = (ob_player.x + OB_PLAYER_W > roomX0 && ob_player.x < roomX1 &&
                       ob_player.y + OB_PLAYER_H > roomY0 && ob_player.y < roomY1);

  switch (b.state) {

    case OB_BAT_IDLE:
      if (playerInRoom) {
        b.state = OB_BAT_WAIT;
        b.timer = OB_BAT_WAIT_FRAMES;
      }
      break;

    case OB_BAT_WAIT:
      if (!playerInRoom) {
        // Player left before launch — go back to idle
        b.state = OB_BAT_IDLE;
      } else if (b.timer > 0) {
        b.timer--;
      } else {
        b.state = OB_BAT_CHASE;
      }
      break;

    case OB_BAT_CHASE: {
      if (!playerInRoom) {
        // Player left the room — return to anchor
        b.state = OB_BAT_IDLE;
        break;
      }
      // Clamp chase target to room bounds
      int16_t targetX = constrain(ob_player.x + OB_PLAYER_W/2 - OB_BAT_W/2, roomX0, roomX1 - OB_BAT_W);
      int16_t targetY = constrain(ob_player.y + OB_PLAYER_H/2 - OB_BAT_H/2, roomY0, roomY1 - OB_BAT_H);
      int16_t dx = targetX - b.x;
      int16_t dy = targetY - b.y;
      if      (dx >  OB_BAT_SPEED) b.x += OB_BAT_SPEED;
      else if (dx < -OB_BAT_SPEED) b.x -= OB_BAT_SPEED;
      else                          b.x  = targetX;
      if      (dy >  OB_BAT_SPEED) b.y += OB_BAT_SPEED;
      else if (dy < -OB_BAT_SPEED) b.y -= OB_BAT_SPEED;
      else                          b.y  = targetY;

      // Fortitude bounce
      if (ob_fortitudeActive()) {
        int16_t cx = ob_player.x + OB_PLAYER_W/2, cy = ob_player.y + OB_PLAYER_H/2;
        int16_t bx = b.x + OB_BAT_W/2,            by = b.y + OB_BAT_H/2;
        if ((int32_t)(bx-cx)*(bx-cx)+(int32_t)(by-cy)*(by-cy) <=
            (int32_t)OB_FORTITUDE_RADIUS*OB_FORTITUDE_RADIUS) {
          b.state = OB_BAT_FLEE; b.timer = OB_BAT_FLEE_FRAMES;
          tone(OB_BUZZER_PIN, 350, 30);
        }
      }
      // Hit player?
      if (ob_player.invincibleFrames == 0 && !ob_fortitudeActive() &&
          ob_rectsOverlap(b.x, b.y, OB_BAT_W, OB_BAT_H, ob_player.x, ob_player.y, OB_PLAYER_W, OB_PLAYER_H)) {
        ob_player.hp--;
        if (ob_player.hp < 0) ob_player.hp = 0;
        ob_player.invincibleFrames = OB_INVINCIBLE_FRAMES;
        tone(OB_BUZZER_PIN, 150, 80);
        b.state = OB_BAT_FLEE; b.timer = OB_BAT_FLEE_FRAMES;
      }
      // Sword hit?
      if (ob_player.attackFrames > 0) {
        int16_t sw = ob_player.facingRight ? ob_player.x + OB_PLAYER_W : ob_player.x - 7;
        if (ob_rectsOverlap(sw, ob_player.y, 7, OB_PLAYER_H, b.x, b.y, OB_BAT_W, OB_BAT_H)) {
          ob_spawnHeart(b.x, b.y);
          b.alive = false; tone(OB_BUZZER_PIN, 300, 60);
        }
      }
      break;
    }

    case OB_BAT_FLEE: {
      // Flee within room bounds
      int16_t dx = b.x - ob_player.x;
      int16_t dy = b.y - ob_player.y;
      if (dx > 0) b.x += OB_BAT_SPEED; else if (dx < 0) b.x -= OB_BAT_SPEED;
      if (dy > 0) b.y += OB_BAT_SPEED; else if (dy < 0) b.y -= OB_BAT_SPEED;
      b.x = constrain(b.x, roomX0, roomX1 - OB_BAT_W);
      b.y = constrain(b.y, roomY0, roomY1 - OB_BAT_H);
      // Sword kill during flee
      if (ob_player.attackFrames > 0) {
        int16_t sw = ob_player.facingRight ? ob_player.x + OB_PLAYER_W : ob_player.x - 7;
        if (ob_rectsOverlap(sw, ob_player.y, 7, OB_PLAYER_H, b.x, b.y, OB_BAT_W, OB_BAT_H)) {
          ob_spawnHeart(b.x, b.y);
          b.alive = false; tone(OB_BUZZER_PIN, 300, 60); break;
        }
      }
      if (b.timer > 0) b.timer--;
      else             b.state = OB_BAT_CHASE;
      break;
    }

    default: break;
  }
}

static void ob_updateBat() {
  if (!ob_bat.alive) return;
  ob_bat.flutterFrame++;
  switch (ob_bat.state) {
    case OB_BAT_IDLE:
      ob_bat.state = OB_BAT_WAIT;
      ob_bat.timer = OB_BAT_WAIT_FRAMES;
      break;
    case OB_BAT_WAIT:
      if (ob_bat.timer > 0) ob_bat.timer--;
      else                  ob_bat.state = OB_BAT_CHASE;
      break;
    case OB_BAT_CHASE: {
      int16_t targetX = ob_player.x + OB_PLAYER_W/2 - OB_BAT_W/2;
      int16_t targetY = ob_player.y + OB_PLAYER_H/2 - OB_BAT_H/2;
      int16_t dx = targetX - ob_bat.x, dy = targetY - ob_bat.y;
      if (dx > OB_BAT_SPEED) ob_bat.x += OB_BAT_SPEED; else if (dx < -OB_BAT_SPEED) ob_bat.x -= OB_BAT_SPEED; else ob_bat.x = targetX;
      if (dy > OB_BAT_SPEED) ob_bat.y += OB_BAT_SPEED; else if (dy < -OB_BAT_SPEED) ob_bat.y -= OB_BAT_SPEED; else ob_bat.y = targetY;
      if (ob_fortitudeActive()) {
        int16_t cx = ob_player.x+OB_PLAYER_W/2, cy = ob_player.y+OB_PLAYER_H/2;
        int16_t bx = ob_bat.x+OB_BAT_W/2,       by = ob_bat.y+OB_BAT_H/2;
        if ((int32_t)(bx-cx)*(bx-cx)+(int32_t)(by-cy)*(by-cy) <= (int32_t)OB_FORTITUDE_RADIUS*OB_FORTITUDE_RADIUS)
          { ob_bat.state = OB_BAT_FLEE; ob_bat.timer = OB_BAT_FLEE_FRAMES; tone(OB_BUZZER_PIN, 350, 30); }
      }
      if (ob_player.invincibleFrames == 0 && !ob_fortitudeActive() &&
          ob_rectsOverlap(ob_bat.x, ob_bat.y, OB_BAT_W, OB_BAT_H, ob_player.x, ob_player.y, OB_PLAYER_W, OB_PLAYER_H)) {
        ob_player.hp--; if (ob_player.hp < 0) ob_player.hp = 0;
        ob_player.invincibleFrames = OB_INVINCIBLE_FRAMES;
        tone(OB_BUZZER_PIN, 150, 80);
        ob_bat.state = OB_BAT_FLEE; ob_bat.timer = OB_BAT_FLEE_FRAMES;
      }
      if (ob_player.attackFrames > 0) {
        int16_t sx = ob_player.facingRight ? ob_player.x+OB_PLAYER_W : ob_player.x-7;
        if (ob_rectsOverlap(sx, ob_player.y, 7, OB_PLAYER_H, ob_bat.x, ob_bat.y, OB_BAT_W, OB_BAT_H))
          { ob_spawnHeart(ob_bat.x, ob_bat.y); ob_bat.alive = false; tone(OB_BUZZER_PIN, 300, 60); }
      }
      break;
    }
    case OB_BAT_FLEE: {
      int16_t dx = ob_bat.x - ob_player.x, dy = ob_bat.y - ob_player.y;
      if (dx > 0) ob_bat.x += OB_BAT_SPEED; else if (dx < 0) ob_bat.x -= OB_BAT_SPEED;
      if (dy > 0) ob_bat.y += OB_BAT_SPEED; else if (dy < 0) ob_bat.y -= OB_BAT_SPEED;
      ob_bat.x = constrain(ob_bat.x, 0, OB_SCREEN_W - OB_BAT_W);
      ob_bat.y = constrain(ob_bat.y, 0, OB_SCREEN_H - OB_BAT_H);
      if (ob_player.attackFrames > 0) {
        int16_t sx = ob_player.facingRight ? ob_player.x+OB_PLAYER_W : ob_player.x-7;
        if (ob_rectsOverlap(sx, ob_player.y, 7, OB_PLAYER_H, ob_bat.x, ob_bat.y, OB_BAT_W, OB_BAT_H))
          { ob_spawnHeart(ob_bat.x, ob_bat.y); ob_bat.alive = false; tone(OB_BUZZER_PIN, 300, 60); break; }
      }
      if (ob_bat.timer > 0) ob_bat.timer--;
      else                  ob_bat.state = OB_BAT_CHASE;
      break;
    }
    default: break;
  }
}

// Arrow hit bat — checks world bats in open world, or single bat in special room
static void ob_checkArrowHitBat(int i) {
  if (ob_inSpecialRoom) {
    if (!ob_bat.alive) return;
    if (ob_bat.state == OB_BAT_WAIT || ob_bat.state == OB_BAT_IDLE) return;
    if (ob_rectsOverlap(ob_arrows[i].x, ob_arrows[i].y, OB_ARROW_LEN, 1,
                         ob_bat.x, ob_bat.y, OB_BAT_W, OB_BAT_H)) {
      ob_arrows[i].active = false;
      ob_spawnHeart(ob_bat.x, ob_bat.y);
      ob_bat.alive = false;
      tone(OB_BUZZER_PIN, 300, 60);
    }
    return;
  }
  for (uint8_t rm = 0; rm < OB_WORLD_ROOMS; rm++) {
    ObBat& b = ob_worldBats[rm];
    if (!b.alive || b.state == OB_BAT_WAIT || b.state == OB_BAT_IDLE) continue;
    if (ob_rectsOverlap(ob_arrows[i].x, ob_arrows[i].y, OB_ARROW_LEN, 1,
                         b.x, b.y, OB_BAT_W, OB_BAT_H)) {
      ob_arrows[i].active = false;
      ob_spawnHeart(b.x, b.y);
      b.alive = false;
      tone(OB_BUZZER_PIN, 300, 60);
      return;
    }
  }
}
// ============================================================================
// ARROWS / BULLETS
// ============================================================================
static void ob_fireArrow() {
  for (int i = 0; i < OB_MAX_ARROWS; i++) {
    if (!ob_arrows[i].active) {
      ob_arrows[i].active = true;
      ob_arrows[i].vx = ob_player.facingRight ? OB_ARROW_SPEED : -OB_ARROW_SPEED;
      ob_arrows[i].x  = ob_player.facingRight ? ob_player.x + OB_PLAYER_W : ob_player.x - OB_ARROW_LEN;
      ob_arrows[i].y  = ob_player.y + OB_PLAYER_H / 2;
      return;
    }
  }
}
static void ob_updateArrows() {
  for (int i = 0; i < OB_MAX_ARROWS; i++) {
    if (!ob_arrows[i].active) continue;
    ob_arrows[i].x += ob_arrows[i].vx;
    {
      int16_t bound = ob_inSpecialRoom ? OB_SCREEN_W : OB_WORLD_PX_W;
      if (ob_arrows[i].x < 0 || ob_arrows[i].x + OB_ARROW_LEN > bound) { ob_arrows[i].active=false; continue; }
    }
    int col = (ob_arrows[i].vx > 0 ? ob_arrows[i].x + OB_ARROW_LEN - 1 : ob_arrows[i].x) / OB_TILE_SIZE;
    if (ob_getTile(col, ob_arrows[i].y/OB_TILE_SIZE) == OB_TILE_SOLID) { ob_arrows[i].active=false; continue; }
    if (ob_inSpecialRoom) {
      if (ob_turret.alive && ob_turret.blinkFrames == 0 &&
          ob_rectsOverlap(ob_arrows[i].x, ob_arrows[i].y, OB_ARROW_LEN, 1,
                          ob_turret.x, ob_turret.y, OB_TURRET_W, OB_TURRET_H))
        { ob_arrows[i].active=false; ob_killTurret(); }
    } else {
      for (uint8_t rm2 = 0; rm2 < OB_WORLD_ROOMS; rm2++) {
        ObTurret& t = ob_worldTurrets[rm2];
        if (t.alive && t.blinkFrames == 0 &&
            ob_rectsOverlap(ob_arrows[i].x, ob_arrows[i].y, OB_ARROW_LEN, 1,
                            t.x, t.y, OB_TURRET_W, OB_TURRET_H))
          { ob_arrows[i].active=false; t.blinkFrames = 16; tone(OB_BUZZER_PIN, 250, 80); break; }
      }
    }
      ob_checkArrowHitBat(i);
      ob_checkArrowHitBoss(i);
  }
}
static void ob_updateBullets() {
  for (int i = 0; i < OB_MAX_BULLETS; i++) {
    if (!ob_bullets[i].active) continue;
    ob_bullets[i].x += ob_bullets[i].vx;
    {
      int16_t bound = ob_inSpecialRoom ? OB_SCREEN_W : OB_WORLD_PX_W;
      if (ob_bullets[i].x < 0 || ob_bullets[i].x >= bound) { ob_bullets[i].active=false; continue; }
    }
    if (ob_getTile(ob_bullets[i].x/OB_TILE_SIZE, ob_bullets[i].y/OB_TILE_SIZE)==OB_TILE_SOLID) { ob_bullets[i].active=false; continue; }

    // Fortitude: destroy bullet when it reaches the circle boundary
    if (ob_fortitudeActive()) {
      int16_t cx = ob_player.x + OB_PLAYER_W / 2;
      int16_t cy = ob_player.y + OB_PLAYER_H / 2;
      int16_t dx = ob_bullets[i].x - cx;
      int16_t dy = ob_bullets[i].y - cy;
      if ((int32_t)dx*dx + (int32_t)dy*dy <= (int32_t)OB_FORTITUDE_RADIUS * OB_FORTITUDE_RADIUS) {
        ob_bullets[i].active = false;  // bullet hits the shield
        continue;
      }
    }

    if (ob_player.invincibleFrames == 0 && !ob_fortitudeActive() &&
        ob_rectsOverlap(ob_bullets[i].x, ob_bullets[i].y, 1, 1,
                        ob_player.x, ob_player.y, OB_PLAYER_W, OB_PLAYER_H)) {
      ob_bullets[i].active = false;
      ob_player.hp--;
      ob_player.invincibleFrames = OB_INVINCIBLE_FRAMES;
      tone(OB_BUZZER_PIN, 200, 60);
      if (ob_player.hp <= 0) ob_player.hp = 0;
    }
  }
}

// ============================================================================
// KEY & CHEST LOGIC
// called once per frame from obscurusLoop_Internal, after physics
// ============================================================================
static void ob_updateKeyAndChest() {

  // ── Key pickup ─────────────────────────────────────────────
  // Key is in open-world room 7: compare world coords directly.
  if (!ob_keyPickedUp && !ob_inSpecialRoom) {
    if (ob_rectsOverlap(ob_player.x, ob_player.y, OB_PLAYER_W, OB_PLAYER_H,
                        OB_KEY_WORLD_X, OB_KEY_WORLD_Y, 7, 7)) {
      ob_keyPickedUp  = true;
      ob_playerHasKey = true;
      // Three-note pickup chime
      tone(OB_BUZZER_PIN, 784,  60); delay(70);
      tone(OB_BUZZER_PIN, 1047, 60); delay(70);
      tone(OB_BUZZER_PIN, 1319, 90);
    }
  }

  // ── Chest interaction ──────────────────────────────────────
  // Only fires if: in chest room, chest not yet opened, AND player has the key.
  // If player doesn't have the key, touching the chest does nothing at all.
  if (!ob_chestOpened && ob_playerHasKey && ob_inSpecialRoom && ob_currentRoom == OB_CHEST_ROOM) {
    if (ob_rectsOverlap(ob_player.x, ob_player.y, OB_PLAYER_W, OB_PLAYER_H,
                        OB_CHEST_X, OB_CHEST_Y, 7, 7)) {
      ob_chestOpened = true;

      // ── Magic orb moment: freeze frame + orb over player's head for 2 s ──
      {
        // Draw the current game frame frozen
        ob_drawFrame();
        // Orb hovers 2 px above the player's head, centred horizontally
        int16_t orbX = ob_player.x + (OB_PLAYER_W / 2) - 3;  // centre 7-wide sprite
        int16_t orbY = ob_player.y - 9;                        // 2 px gap above head
        ob_drawSprite7(ob_kOrbSprite, orbX, orbY, false, WHITE);
        display.display();
        delay(2000);
      }

      // Victory fanfare
      tone(OB_BUZZER_PIN, 523,  80); delay(90);
      tone(OB_BUZZER_PIN, 659,  80); delay(90);
      tone(OB_BUZZER_PIN, 784,  80); delay(90);
      tone(OB_BUZZER_PIN, 1047, 150); delay(160);
      tone(OB_BUZZER_PIN, 784,  80); delay(90);
      tone(OB_BUZZER_PIN, 1047, 300); delay(310);

      // Flash "DUNGEON CLEARED" six times then restart
      for (int f = 0; f < 6; f++) {
        display.clearDisplay();
        if (f & 1) {           // odd passes = blank (flash effect)
          display.display();
        } else {
          display.setTextColor(WHITE);
display.setTextSize(2);

int16_t x1, y1;
uint16_t w, h;

// Center "DUNGEON"
display.getTextBounds("DUNGEON", 0, 0, &x1, &y1, &w, &h);
display.setCursor((OB_SCREEN_W - (int)w) / 2, 14);
display.print("DUNGEON");

// Center "CLEARED"
display.getTextBounds("CLEARED", 0, 0, &x1, &y1, &w, &h);
display.setCursor((OB_SCREEN_W - (int)w) / 2, 36);
display.print("CLEARED");

display.display();
        }
        delay(350);
      }

      // Full game restart — obscurusEnter() always calls Setup_Internal
      obscurusEnter();
    }
  }

  // ── Spell Book pickup (Room 8) ────────────────────────────
  if (!ob_spellBookPickedUp && !ob_inSpecialRoom) {
    if (ob_rectsOverlap(ob_player.x, ob_player.y, OB_PLAYER_W, OB_PLAYER_H,
                        OB_SPELLBOOK_WORLD_X, OB_SPELLBOOK_WORLD_Y, 7, 7)) {
      ob_spellBookPickedUp = true;
      // Show find screen for ~3 s
      display.clearDisplay();
      display.setTextColor(WHITE);
      display.setTextSize(1);
      display.setCursor(0, 4);
      display.print("You found a spell");
      display.setCursor(0, 14);
      display.print("book. You can now");
      display.setCursor(0, 24);
      display.print("decipher spells from");
      display.setCursor(0, 34);
      display.print("rune stones.");
      // Draw spell book sprite centred beneath text
      ob_drawSprite7(ob_kSpellBookSprite, (OB_SCREEN_W - 7) / 2, 46, false, WHITE);
      display.display();
      tone(OB_BUZZER_PIN, 784, 60); delay(70);
      tone(OB_BUZZER_PIN, 1047, 90);
      delay(3000);
    }
  }

  // ── Tombstone interaction (Room 3) ───────────────────────
  if (!ob_tombstoneDone && !ob_inSpecialRoom &&
      millis() >= ob_tombstoneCooldownUntil) {
    if (ob_rectsOverlap(ob_player.x, ob_player.y, OB_PLAYER_W, OB_PLAYER_H,
                        OB_TOMBSTONE_WORLD_X, OB_TOMBSTONE_WORLD_Y, 7, 7)) {
      display.clearDisplay();
      display.setTextColor(WHITE);
      display.setTextSize(1);

      if (ob_spellBookPickedUp) {
        // Unscrambled message — spell learned
        display.setCursor(0, 2);
        display.print("Spell of Fortitude:");
        display.setCursor(0, 12);
        display.print("Cast this spell");
        display.setCursor(0, 22);
        display.print("to defend against");
        display.setCursor(0, 32);
        display.print("the darkest of");
        display.setCursor(0, 42);
        display.print("evils.");
        display.setCursor(0, 52);
        display.print("[BTN3 to cast]");
        display.display();
        tone(OB_BUZZER_PIN, 523, 60); delay(70);
        tone(OB_BUZZER_PIN, 659, 60); delay(70);
        tone(OB_BUZZER_PIN, 784, 90);
        delay(3000);
        ob_tombstoneDone = true;   // permanently inert from now on
        ob_tombstoneRead = true;
        // 2 s grace period so player can walk away before tomb checks again
        ob_tombstoneCooldownUntil = millis() + 2000;
      } else {
        // Scrambled message
        display.setCursor(0, 2);
        display.print("lSplel koBo: uYo");
        display.setCursor(0, 12);
        display.print("delaenr het pSell");
        display.setCursor(0, 22);
        display.print("fo iFturtdeo -");
        display.setCursor(0, 32);
        display.print("tacs hist pells");
        display.setCursor(0, 42);
        display.print("ot dndefe gsiant");
        display.setCursor(0, 52);
        display.print("sefo.");
        display.display();
        tone(OB_BUZZER_PIN, 200, 80);
        delay(3000);
        // 2 s grace period so player can walk away before tomb checks again
        ob_tombstoneCooldownUntil = millis() + 2000;
      }
    }
  }
}

// ============================================================================
// DRAW HELPERS
// ============================================================================
static void ob_drawSprite7(const uint8_t src[7][7], int ox, int oy,
                            bool mirror, uint16_t color, bool flipV) {
  for (int r = 0; r < 7; r++)
    for (int c = 0; c < 7; c++)
      if (pgm_read_byte(&src[flipV ? 6-r : r][mirror ? 6-c : c]))
        display.drawPixel(ox + c, oy + r, color);
}

static void ob_drawSprite14(const uint8_t src[14][14], int ox, int oy,
                              bool mirror, uint16_t color) {
  for (int r = 0; r < 14; r++)
    for (int c = 0; c < 14; c++)
      if (pgm_read_byte(&src[r][mirror ? 13-c : c]))
        display.drawPixel(ox + c, oy + r, color);
}

static void ob_initBoss() {
  // Room 10 layout: floor at row 7 (y=56), open space rows 1-6.
  // Place boss standing on the floor, right-side of room.
  ob_boss.x               = 88;
  ob_boss.y               = OB_SCREEN_H - OB_BOSS_H - OB_TILE_SIZE; // floor level
  ob_boss.vx              = -OB_BOSS_SPEED;
  ob_boss.alive           = true;
  ob_boss.hp              = OB_BOSS_MAX_HP;
  ob_boss.invincibleFrames = 0;
  ob_boss.facingRight     = false;
  ob_boss.weaponOut       = false;
  ob_boss.weaponFrames    = 0;
  ob_boss.chargeTimer     = OB_BOSS_CHARGE_INTERVAL;
  ob_boss.introShown      = false;
}

static void ob_drawBossHpPip(int px, int py, bool active) {
  if (active) { display.fillRect(px,py,5,5,WHITE); display.drawPixel(px+2,py+2,BLACK); }
  else        { display.fillRect(px,py,5,5,BLACK); display.drawRect(px,py,5,5,WHITE);  }
}

static void ob_drawBossHUD() {
  if (ob_currentRoom != OB_BOSS_ROOM) return;
  if (!ob_boss.alive && ob_boss.killedPermanent) return;

  int pipY = OB_SCREEN_H - 6;
  // Draw right-to-left so pip 0 is rightmost
  for (int8_t i = 0; i < OB_BOSS_MAX_HP; i++) {
    int px = OB_SCREEN_W - 7 - i * 7;
    ob_drawBossHpPip(px, pipY, i < ob_boss.hp);
  }
}

static void ob_drawBoss() {
  if (ob_currentRoom != OB_BOSS_ROOM) return;
  if (!ob_boss.alive) return;

  // Blink while invincible
  if (ob_boss.invincibleFrames > 0 && (ob_boss.invincibleFrames & 2)) return;

  ob_drawSprite14(ob_kBossSprite, ob_boss.x, ob_boss.y,
                  !ob_boss.facingRight, WHITE);

  // Draw weapon extruding from the side the boss faces
  if (ob_boss.weaponOut) {
    int wx, wy;
    wy = ob_boss.y + OB_BOSS_H/2 - OB_BOSS_WEAPON_H/2;
    if (ob_boss.facingRight) {
      wx = ob_boss.x + OB_BOSS_W;
      ob_drawSprite14(ob_kBossWeaponSprite, wx, wy, false, WHITE);
    } else {
      wx = ob_boss.x - OB_BOSS_WEAPON_W;
      ob_drawSprite14(ob_kBossWeaponSprite, wx, wy, true, WHITE);
    }
  }
}

static void ob_showBossIntro() {
  tone(OB_BUZZER_PIN, 110, 200); delay(220);
  tone(OB_BUZZER_PIN, 110, 200); delay(220);
  tone(OB_BUZZER_PIN, 147, 400); delay(420);
}

// Shake the current room tiles N times with sound — reused for entry and death.
static void ob_shakeRoom(int steps) {
  static const int8_t shakeX[] = { 3, -3,  4, -4,  2, -3,  4, -2,  3, -4,  2, -3 };
  static const int8_t shakeY[] = { 1, -1,  2, -2,  1, -2,  2, -1,  2, -1,  1, -2 };
  for (int blink = 0; blink < steps; blink++) {
    int8_t ox = shakeX[blink % 12];
    int8_t oy = shakeY[blink % 12];
    display.clearDisplay();
    for (int r = 0; r < OB_MAP_ROWS; r++) {
      for (int c = 0; c < OB_MAP_COLS; c++) {
        if (ob_roomTiles[r][c] == OB_TILE_SOLID) {
          int px = c * OB_TILE_SIZE + ox;
          int py = r * OB_TILE_SIZE + oy;
          display.fillRect(px, py, OB_TILE_SIZE, OB_TILE_SIZE, WHITE);
          display.drawPixel(px,                    py,                    BLACK);
          display.drawPixel(px + OB_TILE_SIZE - 1, py,                    BLACK);
          display.drawPixel(px,                    py + OB_TILE_SIZE - 1, BLACK);
          display.drawPixel(px + OB_TILE_SIZE - 1, py + OB_TILE_SIZE - 1, BLACK);
        }
      }
    }
    display.display();
    tone(OB_BUZZER_PIN, 80 + blink * 20, 60);
    delay(60);
  }
}

static void ob_bossDeathSequence() {
  // ── Shake while boss blinks ───────────────────────────────────
  static const int8_t shakeX[] = { 3, -3,  4, -4,  2, -3,  4, -2,  3, -4,  2, -3 };
  static const int8_t shakeY[] = { 1, -1,  2, -2,  1, -2,  2, -1,  2, -1,  1, -2 };

  for (int blink = 0; blink < 6; blink++) {
    int8_t ox = shakeX[blink * 2];
    int8_t oy = shakeY[blink * 2];
    display.clearDisplay();
    for (int r = 0; r < OB_MAP_ROWS; r++) {
      for (int c = 0; c < OB_MAP_COLS; c++) {
        if (ob_roomTiles[r][c] == OB_TILE_SOLID) {
          int px = c * OB_TILE_SIZE + ox;
          int py = r * OB_TILE_SIZE + oy;
          display.fillRect(px, py, OB_TILE_SIZE, OB_TILE_SIZE, WHITE);
          display.drawPixel(px,                    py,                    BLACK);
          display.drawPixel(px + OB_TILE_SIZE - 1, py,                    BLACK);
          display.drawPixel(px,                    py + OB_TILE_SIZE - 1, BLACK);
          display.drawPixel(px + OB_TILE_SIZE - 1, py + OB_TILE_SIZE - 1, BLACK);
        }
      }
    }
    if (!(blink & 1))
      ob_drawSprite14(ob_kBossSprite, ob_boss.x + ox, ob_boss.y + oy,
                      !ob_boss.facingRight, WHITE);
    display.display();
    tone(OB_BUZZER_PIN, 80 + blink * 30, 80);
    delay(50);
  }

  // ── Death fanfare ─────────────────────────────────────────────
  tone(OB_BUZZER_PIN, 330, 80); delay(90);
  tone(OB_BUZZER_PIN, 440, 80); delay(90);
  tone(OB_BUZZER_PIN, 554, 80); delay(90);
  tone(OB_BUZZER_PIN, 659, 200); delay(210);

  // ── Second shake as room crumbles — player visible throughout ───
  {
    static const int8_t shakeX[] = { 3,-3, 4,-4, 2,-3, 4,-2, 3,-4, 2,-3 };
    static const int8_t shakeY[] = { 1,-1, 2,-2, 1,-2, 2,-1, 2,-1, 1,-2 };
    for (int i = 0; i < 8; i++) {
      int8_t ox = shakeX[i % 12];
      int8_t oy = shakeY[i % 12];
      display.clearDisplay();
      for (int r = 0; r < OB_MAP_ROWS; r++)
        for (int c = 0; c < OB_MAP_COLS; c++)
          if (ob_roomTiles[r][c] == OB_TILE_SOLID) {
            int px = c*OB_TILE_SIZE+ox, py = r*OB_TILE_SIZE+oy;
            display.fillRect(px, py, OB_TILE_SIZE, OB_TILE_SIZE, WHITE);
            display.drawPixel(px,                py,                BLACK);
            display.drawPixel(px+OB_TILE_SIZE-1, py,                BLACK);
            display.drawPixel(px,                py+OB_TILE_SIZE-1, BLACK);
            display.drawPixel(px+OB_TILE_SIZE-1, py+OB_TILE_SIZE-1, BLACK);
          }
      ob_drawPlayer();
      display.display();
      tone(OB_BUZZER_PIN, 80 + i*20, 60);
      delay(60);
    }
  }

  // ── Swap to destroyed layout ──────────────────────────────────
  ObRoomTiles* src = (ObRoomTiles*)ob_kRoom10Destroyed;
  for (int r = 0; r < OB_MAP_ROWS; r++)
    for (int c = 0; c < OB_MAP_COLS; c++)
      ob_roomTiles[r][c] = pgm_read_byte(&((*src)[r][c]));

  // ── Unseal entrance — restore only the two tiles that were sealed ─
  ob_roomTiles[5][0] = OB_TILE_EMPTY;
  ob_roomTiles[6][0] = OB_TILE_EMPTY;
}

static void ob_updateBoss() {
  if (ob_currentRoom != OB_BOSS_ROOM) return;
  if (!ob_boss.alive) return;

  // ── Show intro card once per visit ───────────────────────────
  if (!ob_boss.introShown) {
    ob_boss.introShown = true;
    ob_showBossIntro();
  }

  // ── Invincibility countdown ───────────────────────────────────
  if (ob_boss.invincibleFrames > 0) ob_boss.invincibleFrames--;

  // ── Weapon display countdown ─────────────────────────────────
  if (ob_boss.weaponFrames > 0) {
    ob_boss.weaponFrames--;
    if (ob_boss.weaponFrames == 0) ob_boss.weaponOut = false;
  }

  // ── Charge / movement ────────────────────────────────────────
  if (ob_boss.chargeTimer > 0) {
    ob_boss.chargeTimer--;
  } else {
    // New charge: aim directly at player
    ob_boss.chargeTimer = OB_BOSS_CHARGE_INTERVAL;
    ob_boss.vx = (ob_player.x > ob_boss.x) ? OB_BOSS_SPEED*2 : -OB_BOSS_SPEED*2;
    ob_boss.weaponOut    = true;
    ob_boss.weaponFrames = 20;
    tone(OB_BUZZER_PIN, 220, 60);
  }

  // Move horizontally, bounce off walls
  ob_boss.x += ob_boss.vx;
  if (ob_boss.x <= 8) {
    ob_boss.x  = 8;
    ob_boss.vx = OB_BOSS_SPEED;
  }
  if (ob_boss.x + OB_BOSS_W >= OB_SCREEN_W - 8) {
    ob_boss.x  = OB_SCREEN_W - 8 - OB_BOSS_W;
    ob_boss.vx = -OB_BOSS_SPEED;
  }
  ob_boss.facingRight = (ob_boss.vx > 0);

  // Keep boss on floor
  ob_boss.y = OB_SCREEN_H - OB_BOSS_H - OB_TILE_SIZE;

  // ── Weapon hit check on player ───────────────────────────────
  if (ob_boss.weaponOut && ob_player.invincibleFrames == 0 && !ob_fortitudeActive()) {
    int wx = ob_boss.facingRight ? ob_boss.x + OB_BOSS_W : ob_boss.x - OB_BOSS_WEAPON_W;
    int wy = ob_boss.y + OB_BOSS_H/2 - OB_BOSS_WEAPON_H/2;
    if (ob_rectsOverlap(wx, wy, OB_BOSS_WEAPON_W, OB_BOSS_WEAPON_H,
                         ob_player.x, ob_player.y, OB_PLAYER_W, OB_PLAYER_H)) {
      ob_player.hp--;
      if (ob_player.hp < 0) ob_player.hp = 0;
      ob_player.invincibleFrames = OB_INVINCIBLE_FRAMES;
      tone(OB_BUZZER_PIN, 150, 80);
    }
  }

  // ── Body contact with player ─────────────────────────────────
  if (ob_player.invincibleFrames == 0 && !ob_fortitudeActive() &&
      ob_rectsOverlap(ob_boss.x, ob_boss.y, OB_BOSS_W, OB_BOSS_H,
                       ob_player.x, ob_player.y, OB_PLAYER_W, OB_PLAYER_H)) {
    ob_player.hp--;
    if (ob_player.hp < 0) ob_player.hp = 0;
    ob_player.invincibleFrames = OB_INVINCIBLE_FRAMES;
    tone(OB_BUZZER_PIN, 150, 80);
  }

  // ── Player sword hits boss ────────────────────────────────────
  if (ob_boss.invincibleFrames == 0 && ob_player.attackFrames > 0) {
    int16_t sx = ob_player.facingRight ? ob_player.x + OB_PLAYER_W : ob_player.x - 7;
    if (ob_rectsOverlap(sx, ob_player.y, 7, OB_PLAYER_H,
                         ob_boss.x, ob_boss.y, OB_BOSS_W, OB_BOSS_H)) {
      ob_boss.hp--;
      ob_boss.invincibleFrames = OB_BOSS_INVINCIBLE_FRAMES;
      tone(OB_BUZZER_PIN, 400, 40);
      if (ob_boss.hp <= 0) {
        ob_boss.alive           = false;
        ob_boss.killedPermanent = true;
        ob_bossDeathSequence();
      }    // closes if hp <= 0
    }      // closes if rectsOverlap
  }        // closes if attackFrames > 0

  // ── Arrow hits boss ───────────────────────────────────────────
  // (handled separately — see ob_checkArrowHitBoss below)
}

static void ob_checkArrowHitBoss(int i) {
  if (ob_currentRoom != OB_BOSS_ROOM) return;
  if (!ob_boss.alive || ob_boss.invincibleFrames > 0) return;
  if (ob_rectsOverlap(ob_arrows[i].x, ob_arrows[i].y, OB_ARROW_LEN, 1,
                       ob_boss.x, ob_boss.y, OB_BOSS_W, OB_BOSS_H)) {
    ob_arrows[i].active = false;
    ob_boss.hp--;
    ob_boss.invincibleFrames = OB_BOSS_INVINCIBLE_FRAMES;
    tone(OB_BUZZER_PIN, 400, 40);
    if (ob_boss.hp <= 0) {
      ob_boss.alive           = false;
      ob_boss.killedPermanent = true;
      ob_bossDeathSequence();
    }
  }
}

static void ob_drawPlayer() {
  int16_t x  = ob_inSpecialRoom ? ob_player.x : ob_player.x - ob_camX;
  int16_t y  = ob_inSpecialRoom ? ob_player.y : ob_player.y - ob_camY;
  bool    fr = ob_player.facingRight;

  if (ob_player.invincibleFrames > 0 && (ob_player.invincibleFrames & 2)) return;

  // ── Body / head rows (0-5) from editable sprite ──────────────
  for (int r = 0; r < 6; r++) {
    for (int c = 0; c < 6; c++) {
      int sc = fr ? c : 5 - c;   // mirror horizontally when facing left
      if (pgm_read_byte(&ob_kPlayerSprite[r][sc]))
        display.drawPixel(x + c, y + r, WHITE);
    }
  }

  // ── Legs (rows 6-8): dynamic poses ───────────────────────────
  if (!ob_player.onGround) {
    if (ob_player.jumpsLeft == 0) {
      // Double-jump — legs spread wide
      display.fillRect(x,     y + 7, 2, 2, WHITE);
      display.fillRect(x + 4, y + 7, 2, 2, WHITE);
    } else {
      // Single jump — legs together
      display.fillRect(x + 1, y + 6, 4, 2, WHITE);
    }
  } else {
    // Standing — two leg columns
    display.fillRect(x + 1, y + 6, 2, 3, WHITE);
    display.fillRect(x + 3, y + 6, 2, 3, WHITE);
  }

  // ── Weapon ────────────────────────────────────────────────────
  bool swordActive = (ob_player.weapon == OB_WEAPON_SWORD && ob_player.attackFrames > 0);
  bool bowActive   = (ob_player.weapon == OB_WEAPON_BOW   && ob_player.bowFrames    > 0);
  if (swordActive || bowActive) {
    const uint8_t (*spr)[7] = swordActive ? ob_kSwordSprite : ob_kBowSprite;
    int oy2 = y + (OB_PLAYER_H / 2) - 3;
    if (fr) ob_drawSprite7(spr, x + OB_PLAYER_W, oy2, false, WHITE);
    else    ob_drawSprite7(spr, x - 7,            oy2, true,  WHITE);
  }
}

static void ob_drawTurret() {
  if (ob_inSpecialRoom) {
    if (!ob_turret.alive) return;
    if (ob_turret.blinkFrames > 0 && (ob_turret.blinkFrames & 4)) return;
    int16_t sx = ob_turret.x, sy = ob_turret.y;
    bool shootRight = (ob_player.x > ob_turret.x);
    display.fillRect(sx, sy, OB_TURRET_W, OB_TURRET_H, INVERSE);
    display.drawFastHLine(sx+1, sy+1, 2, INVERSE);
    display.drawFastHLine(sx+1, sy+3, 2, INVERSE);
    int16_t bY = sy + OB_TURRET_H/2 - 1;
    if (shootRight) display.fillRect(sx+OB_TURRET_W,  bY, OB_BARREL_LEN, 2, INVERSE);
    else            display.fillRect(sx-OB_BARREL_LEN, bY, OB_BARREL_LEN, 2, INVERSE);
    return;
  }
  for (uint8_t rm = 0; rm < OB_WORLD_ROOMS; rm++) {
    ObTurret& t = ob_worldTurrets[rm];
    if (!t.alive) continue;
    if (t.blinkFrames > 0 && (t.blinkFrames & 4)) continue;
    int16_t sx = t.x - ob_camX, sy = t.y - ob_camY;
    if (sx + OB_TURRET_W < 0 || sx >= OB_SCREEN_W) continue;
    bool shootRight = (ob_player.x > t.x);
    display.fillRect(sx, sy, OB_TURRET_W, OB_TURRET_H, INVERSE);
    display.drawFastHLine(sx+1, sy+1, 2, INVERSE);
    display.drawFastHLine(sx+1, sy+3, 2, INVERSE);
    int16_t bY = sy + OB_TURRET_H/2 - 1;
    if (shootRight) display.fillRect(sx+OB_TURRET_W,  bY, OB_BARREL_LEN, 2, INVERSE);
    else            display.fillRect(sx-OB_BARREL_LEN, bY, OB_BARREL_LEN, 2, INVERSE);
  }
}

static void ob_drawBat() {
  if (ob_inSpecialRoom) {
    if (!ob_bat.alive) return;
    bool upsideDown = (ob_bat.state == OB_BAT_IDLE || ob_bat.state == OB_BAT_WAIT);
    ob_drawSprite7(ob_kBatSprite, ob_bat.x, ob_bat.y, false, INVERSE, upsideDown);
    return;
  }
  for (uint8_t rm = 0; rm < OB_WORLD_ROOMS; rm++) {
    ObBat& b = ob_worldBats[rm];
    if (!b.alive) continue;
    int16_t sx = b.x - ob_camX, sy = b.y - ob_camY;
    if (sx + OB_BAT_W < 0 || sx >= OB_SCREEN_W) continue;
    bool upsideDown = (b.state == OB_BAT_IDLE || b.state == OB_BAT_WAIT);
    ob_drawSprite7(ob_kBatSprite, sx, sy, false, INVERSE, upsideDown);
  }
}

static void ob_drawBullets() {
  for (int i = 0; i < OB_MAX_BULLETS; i++) {
    if (!ob_bullets[i].active) continue;
    int16_t sx = ob_toScreenX(ob_bullets[i].x);
    int16_t sy = ob_toScreenY(ob_bullets[i].y);
    if (sx >= 0 && sx < OB_SCREEN_W && sy >= 0 && sy < OB_SCREEN_H)
      display.drawPixel(sx, sy, WHITE);
  }
}

static void ob_drawArrows() {
  for (int i = 0; i < OB_MAX_ARROWS; i++) {
    if (!ob_arrows[i].active) continue;
    int16_t sx = ob_toScreenX(ob_arrows[i].x);
    int16_t sy = ob_toScreenY(ob_arrows[i].y);
    if (sx + OB_ARROW_LEN >= 0 && sx < OB_SCREEN_W && sy >= 0 && sy < OB_SCREEN_H)
      display.drawFastHLine(sx, sy, OB_ARROW_LEN, WHITE);
  }
}

// ── Draw world tiles visible through the current camera viewport ──────────
static void ob_drawRoom() {
  if (ob_inSpecialRoom) {
    // Classic: draw the single-room buffer at screen coords
    for (int r = 0; r < OB_MAP_ROWS; r++) for (int c = 0; c < OB_MAP_COLS; c++) {
      if (ob_roomTiles[r][c] == OB_TILE_SOLID) {
        int px = c * OB_TILE_SIZE, py = r * OB_TILE_SIZE;
        display.fillRect(px, py, OB_TILE_SIZE, OB_TILE_SIZE, WHITE);
        display.drawPixel(px,                   py,                   BLACK);
        display.drawPixel(px + OB_TILE_SIZE - 1, py,                  BLACK);
        display.drawPixel(px,                   py + OB_TILE_SIZE - 1, BLACK);
        display.drawPixel(px + OB_TILE_SIZE - 1, py + OB_TILE_SIZE - 1, BLACK);
      }
    }
  } else {
    // Open-world: iterate only the tiles visible through the camera window
    int tc0 = ob_camX / OB_TILE_SIZE;
    int tr0 = ob_camY / OB_TILE_SIZE;
    int tc1 = (ob_camX + OB_SCREEN_W - 1) / OB_TILE_SIZE;
    int tr1 = (ob_camY + OB_SCREEN_H - 1) / OB_TILE_SIZE;
    for (int r = tr0; r <= tr1; r++) for (int c = tc0; c <= tc1; c++) {
      if (ob_getTile(c, r) == OB_TILE_SOLID) {
        int px = c * OB_TILE_SIZE - ob_camX;
        int py = r * OB_TILE_SIZE - ob_camY;
        display.fillRect(px, py, OB_TILE_SIZE, OB_TILE_SIZE, WHITE);
        display.drawPixel(px,                   py,                   BLACK);
        display.drawPixel(px + OB_TILE_SIZE - 1, py,                  BLACK);
        display.drawPixel(px,                   py + OB_TILE_SIZE - 1, BLACK);
        display.drawPixel(px + OB_TILE_SIZE - 1, py + OB_TILE_SIZE - 1, BLACK);
      }
    }

    // ── Boss door sprite — 8×16, flush to right wall of Rm11w ────────────
    {
      int16_t sx = OB_WORLD_PX_W - 8 - ob_camX;
      int16_t sy = OB_BOSS_DOOR_Y0    - ob_camY;
      if (sx + 8 > 0 && sx < OB_SCREEN_W && sy + 16 > 0 && sy < OB_SCREEN_H) {
        for (int r = 0; r < 16; r++) {
          for (int c = 0; c < 8; c++) {
            if (pgm_read_byte(&ob_kBossDoorSprite[r][c])) {
              int16_t px = sx + c, py = sy + r;
              if (px >= 0 && px < OB_SCREEN_W && py >= 0 && py < OB_SCREEN_H)
                display.drawPixel(px, py, WHITE);
            }
          }
        }
      }
    }
  }
}

static void ob_drawWorldKey() {
  if (ob_keyPickedUp) return;
  if (ob_inSpecialRoom) return;  // key is in open world only
  int16_t sx = ob_toScreenX(OB_KEY_WORLD_X);
  int16_t sy = ob_toScreenY(OB_KEY_WORLD_Y);
  if (sx + 7 < 0 || sx >= OB_SCREEN_W || sy + 7 < 0 || sy >= OB_SCREEN_H) return;
  ob_drawSprite7(ob_kKeySprite, sx, sy, false, WHITE);
}

static void ob_drawWorldChest() {
  // Chest is in special room 11 — drawn at screen coords when in that room
  if (!ob_inSpecialRoom || ob_currentRoom != OB_CHEST_ROOM || ob_chestOpened) return;
  ob_drawSprite7(ob_kChestSprite, OB_CHEST_X, OB_CHEST_Y, false, WHITE);
}

static void ob_drawWorldSpellBook() {
  if (ob_spellBookPickedUp) return;
  if (ob_inSpecialRoom) return;
  int16_t sx = ob_toScreenX(OB_SPELLBOOK_WORLD_X);
  int16_t sy = ob_toScreenY(OB_SPELLBOOK_WORLD_Y);
  if (sx + 7 < 0 || sx >= OB_SCREEN_W || sy + 7 < 0 || sy >= OB_SCREEN_H) return;
  ob_drawSprite7(ob_kSpellBookSprite, sx, sy, false, INVERSE);
}

static void ob_drawWorldTombstone() {
  if (ob_inSpecialRoom) return;
  int16_t sx = ob_toScreenX(OB_TOMBSTONE_WORLD_X);
  int16_t sy = ob_toScreenY(OB_TOMBSTONE_WORLD_Y);
  if (sx + 7 < 0 || sx >= OB_SCREEN_W || sy + 7 < 0 || sy >= OB_SCREEN_H) return;
  ob_drawSprite7(ob_kTombstoneSprite, sx, sy, false, WHITE);
}

static void ob_drawFortitudeShield() {
  if (!ob_fortitudeActive()) return;
  if (ob_fortitudeFrames <= OB_FORTITUDE_BLINK_TOTAL) {
    if ((ob_fortitudeFrames / 8) & 1) return;
  }
  int16_t cx = ob_toScreenX(ob_player.x) + OB_PLAYER_W / 2;
  int16_t cy = ob_toScreenY(ob_player.y) + OB_PLAYER_H / 2;
  display.drawCircle(cx, cy, OB_FORTITUDE_RADIUS, WHITE);
}

static void ob_drawHpPip(int px, int py, bool active) {
  if (active) {
    // Full life — draw the editable life sprite
    for (int r = 0; r < 5; r++)
      for (int c = 0; c < 5; c++)
        if (pgm_read_byte(&ob_kLifeSprite[r][c]))
          display.drawPixel(px + c, py + r, WHITE);
  } else {
    // Lost life — single dim pixel in the centre
    display.drawPixel(px + 2, py + 2, WHITE);
  }
}

static void ob_drawHUD() {
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(1, 1);
  display.print('R');
  display.print(ob_inSpecialRoom ? ob_currentRoom : (uint8_t)(ob_player.x / OB_WORLD_PX_W * 10));

  // Jump pips — top-right
  for (uint8_t i = 0; i < OB_MAX_JUMPS; i++) {
    int dx = OB_SCREEN_W - 4 - i * 5;
    if (i < ob_player.jumpsLeft) display.fillRect(dx, 2, 3, 3, WHITE);
    else                         display.drawRect(dx, 2, 3, 3, WHITE);
  }

  // HP pips — bottom-left
  int pipY = OB_SCREEN_H - 6;
  for (int8_t i = 0; i < OB_MAX_HP; i++)
    ob_drawHpPip(2 + i * 7, pipY, i < ob_player.hp);

  // Weapon icon
  int iconX = 2 + OB_MAX_HP * 7 + 4;
  int iconY = pipY - 1;
  const uint8_t (*spr)[7] = (ob_player.weapon == OB_WEAPON_SWORD) ? ob_kSwordSprite : ob_kBowSprite;
  ob_drawSprite7(spr, iconX, iconY, false, INVERSE);

  if (ob_playerHasKey) {
    ob_drawSprite7(ob_kKeySprite, iconX + 9, iconY, false, INVERSE);
  }

  if (ob_spellBookPickedUp) {
    int sbIconX = ob_playerHasKey ? iconX + 18 : iconX + 9;
    ob_drawSprite7(ob_kSpellBookSprite, sbIconX, iconY, false, INVERSE);
    if (ob_fortitudeActive()) {
      display.fillRect(sbIconX + 8, iconY + 3, 2, 2, WHITE);
    } else if (ob_fortitudeCooldown > 0) {
      display.drawRect(sbIconX + 8, iconY + 3, 2, 2, WHITE);
    }
  }
}

// ============================================================================
// PAUSE SCREEN
// ============================================================================
static void ob_drawPauseScreen() {
  display.clearDisplay();
  display.setTextColor(WHITE);
  display.setTextSize(2);
  display.setCursor(22, 22);
  display.print(" PAUSE");
  display.setTextSize(1);
  display.setCursor(0, 55);
  display.print("PLAY");
  display.setCursor(OB_SCREEN_W - 24, 55);
  display.print("EXIT");
  display.display();
}

// ============================================================================
// HEART DROPS — physics, pickup, draw
// ============================================================================
static void ob_updateHearts() {
  for (int i = 0; i < OB_MAX_HEARTS; i++) {
    ObHeart& h = ob_hearts[i];
    if (!h.active) continue;

    // Gravity
    h.vy += OB_HEART_GRAVITY;
    if (h.vy > OB_HEART_MAX_FALL) h.vy = OB_HEART_MAX_FALL;

    // Move down, stop on solid tile
    int16_t newY = h.y + h.vy;
    if (ob_rectOverlapsSolid(h.x, newY, OB_HEART_W, OB_HEART_H)) {
      while (newY > h.y && ob_rectOverlapsSolid(h.x, newY, OB_HEART_W, OB_HEART_H))
        newY--;
      h.y  = newY;
      h.vy = 0;
    } else {
      h.y = newY;
    }

    // Pickup
    if (ob_rectsOverlap(h.x, h.y, OB_HEART_W, OB_HEART_H,
                        ob_player.x, ob_player.y, OB_PLAYER_W, OB_PLAYER_H)) {
      h.active = false;
      if (ob_player.hp < OB_MAX_HP) {
        ob_player.hp++;
        tone(OB_BUZZER_PIN, 1047, 60);
      }
    }
  }
}

static void ob_drawHearts() {
  for (int i = 0; i < OB_MAX_HEARTS; i++) {
    ObHeart& h = ob_hearts[i];
    if (!h.active) continue;
    int16_t sx = ob_inSpecialRoom ? h.x : h.x - ob_camX;
    int16_t sy = ob_inSpecialRoom ? h.y : h.y - ob_camY;
    if (sx + OB_HEART_W < 0 || sx >= OB_SCREEN_W) continue;
    if (sy + OB_HEART_H < 0 || sy >= OB_SCREEN_H) continue;
    for (int r = 0; r < 5; r++)
      for (int c = 0; c < 5; c++)
        if (pgm_read_byte(&ob_kLifeSprite[r][c]))
          display.drawPixel(sx + c, sy + r, INVERSE);
  }
}

// ============================================================================
// GAME OVER SEQUENCE — blocks then restarts the game
// ============================================================================
static void ob_doGameOver() {
  // Blink player 3 times
  for (uint8_t b = 0; b < 6; b++) {
    display.clearDisplay();
    ob_drawRoom();
    ob_drawTurret();
    ob_drawBat();
    ob_drawBullets();
    ob_drawArrows();
    ob_drawHearts();
    ob_drawHUD();
    if (b & 1) ob_drawPlayer();
    display.display();
    delay(120);
  }

  // Draw player lying flat (sprite transposed — cols→rows)
  display.clearDisplay();
  ob_drawRoom();
  ob_drawTurret();
  ob_drawBat();
  ob_drawBullets();
  ob_drawArrows();
  ob_drawHearts();
  ob_drawHUD();
  {
    int16_t bx = ob_inSpecialRoom ? ob_player.x : ob_player.x - ob_camX;
    int16_t by = ob_inSpecialRoom ? ob_player.y : ob_player.y - ob_camY;
    bool    fr = ob_player.facingRight;
    for (int c = 0; c < 6; c++)
      for (int r = 0; r < 6; r++) {
        int sc = fr ? c : 5 - c;
        if (pgm_read_byte(&ob_kPlayerSprite[r][sc]))
          display.drawPixel(bx + r, by + c + 4, WHITE);
      }
    display.fillRect(bx + 6, by + 5, 3, 2, WHITE);
    display.fillRect(bx + 6, by + 7, 3, 2, WHITE);
  }
  display.display();
  delay(800);

  // GAME OVER text
  display.clearDisplay();
  display.setTextColor(WHITE);
  display.setTextSize(2);
  const char* goMsg = "GAME OVER";
  int16_t gx = (OB_SCREEN_W - (int16_t)(strlen(goMsg) * 12)) / 2;
  display.setCursor(gx, (OB_SCREEN_H - 16) / 2);
  display.print(goMsg);
  display.display();
  delay(2500);

  // Restart fresh
  obscurusSetup_Internal();
}

// ============================================================================
// FULL GAME FRAME
// ============================================================================
static void ob_drawFrame() {
  display.clearDisplay();
  ob_drawRoom();
  ob_drawWorldKey();
  ob_drawWorldChest();
  ob_drawWorldSpellBook();
  ob_drawWorldTombstone();
  ob_drawTurret();
  ob_drawBat();
  ob_drawBoss();
  ob_drawBullets();
  ob_drawArrows();
  ob_drawHearts();
  ob_drawFortitudeShield();
  ob_drawPlayer();
  ob_drawHUD();
  ob_drawBossHUD();
  display.display();
}

// ============================================================================
// INTERNAL SETUP
// ============================================================================
static void obscurusSetup_Internal() {
  // Reset boss
  ob_boss.killedPermanent = false;
  ob_boss.alive           = false;
  ob_boss.introShown      = false;

  ob_inSpecialRoom = false;
  ob_buildWorld();
  ob_initWorldEnemies();   // place all open-world enemies at world-space coords

  // Player starts in room 0 (world grid col 0, row 0) — world-space coords
  ob_player.x                = 20;
  ob_player.y                = OB_SCREEN_H - OB_PLAYER_H - OB_TILE_SIZE;
  ob_player.vx               = 0;
  ob_player.vy               = 0;
  ob_player.onGround         = false;
  ob_player.facingRight      = true;
  ob_player.jumpQueued       = false;
  ob_player.jumpsLeft        = OB_MAX_JUMPS;
  ob_player.walkFrame        = 0;
  ob_player.walkTick         = 0;
  ob_player.attackFrames     = 0;
  ob_player.bowFrames        = 0;
  ob_player.hp               = OB_MAX_HP;
  ob_player.invincibleFrames = 0;
  ob_player.weapon           = OB_WEAPON_SWORD;

  ob_clearBullets();
  ob_clearArrows();
  ob_clearHearts();
  ob_gameOver = false;

  // Reset key / chest
  ob_playerHasKey = false;
  ob_keyPickedUp  = false;
  ob_chestOpened  = false;

  // Reset spell book / tombstone / fortitude
  ob_spellBookPickedUp  = false;
  ob_spellBookFound     = false;
  ob_tombstoneRead      = false;
  ob_tombstoneDone      = false;
  ob_tombstoneCooldownUntil = 0;
  ob_fortitudeFrames    = 0;
  ob_fortitudeCooldown  = 0;

  ob_paused            = false;
  ob_pauseNeedsRelease = false;
  ob_pauseBtn1WasDown  = false;
  ob_pauseBtn3WasDown  = false;

  display.clearDisplay();
display.setTextColor(WHITE);

// ── TITLE ─────────────────────────────
display.setTextSize(2);
const char* title = "OBSCURUS";
int16_t titleX = (OB_SCREEN_W - (strlen(title) * 12)) / 2;
display.setCursor(titleX, 6);
display.print(title);

// ── CONTROLS ──────────────────────────
display.setTextSize(1);

const char* line1 = "BTN2  JUMP";
const char* line2 = "BTN1  ATTACK";
const char* line3 = "BTN3  SPELL";
const char* line4 = "L/R   MOVE";

int16_t yStart = 24;
int16_t spacing = 10;

display.setCursor((OB_SCREEN_W - strlen(line1)*6)/2, yStart);
display.print(line1);

display.setCursor((OB_SCREEN_W - strlen(line2)*6)/2, yStart + spacing);
display.print(line2);

display.setCursor((OB_SCREEN_W - strlen(line3)*6)/2, yStart + spacing*2);
display.print(line3);

display.setCursor((OB_SCREEN_W - strlen(line4)*6)/2, yStart + spacing*3);
display.print(line4);

display.display();

  tone(OB_BUZZER_PIN, 523, 80); delay(90);
  tone(OB_BUZZER_PIN, 659, 80); delay(90);
  tone(OB_BUZZER_PIN, 784, 120); delay(130);
  delay(800);
  ob_drawFrame();
}

// ============================================================================
// INTERNAL LOOP
// ============================================================================
static void obscurusLoop_Internal() {
  static unsigned long lastFrame = 0;
  const unsigned long FRAME_MS = 33;
  unsigned long now = millis();
  if (now - lastFrame < FRAME_MS) return;
  lastFrame = now;

  ob_prevGpio = ob_mcpGpio;
  if (!ob_mcpRead(ob_mcpGpio)) ob_mcpGpio = 0xFF;

  // ── PAUSE TOGGLE ──────────────────────────────────────────
  bool swNow = (ob_mcpGpio & (1u << 7)) ? HIGH : LOW;
  if (swNow == HIGH) ob_pauseNeedsRelease = false;

  if (!ob_pauseNeedsRelease && millis() >= ob_pauseIgnoreUntil && ob_encPressed()) {
    if (!ob_paused) {
      ob_paused           = true;
      ob_pauseBtn1WasDown = false;
      ob_pauseBtn3WasDown = false;
    }
    ob_pauseNeedsRelease = true;
    ob_pauseIgnoreUntil  = millis() + 250;
  }

  // ── PAUSE SCREEN ──────────────────────────────────────────
  if (ob_paused) {
    bool b1 = ob_btnDown(OB_GP_BTN1);
    bool b3 = ob_btnDown(OB_GP_BTN3);

    if (ob_pauseBtn1WasDown && !b1) {
      ob_pauseBtn1WasDown = false;
      ob_paused = false;
      ob_prevGpio = 0xFF;
      if (!ob_mcpRead(ob_mcpGpio)) ob_mcpGpio = 0xFF;
      ob_prevGpio = ob_mcpGpio;
      ob_drawFrame();
      return;
    } else { ob_pauseBtn1WasDown = b1; }

    if (ob_pauseBtn3WasDown && !b3) {
      ob_pauseBtn3WasDown = false;
      // Keep ob_paused = true so re-entering the game lands on the pause screen
      ob_inProgress = true;   // remember we have a live session to return to
      gameExitToMenu();
      return;
    } else { ob_pauseBtn3WasDown = b3; }

    ob_drawPauseScreen();
    return;
  }

  // ── WEAPON SWITCH ─────────────────────────────────────────
  // DOWN toggles between sword and bow; UP does nothing for now
  if (ob_btnJustPressed(OB_GP_DOWN)) {
    ob_player.weapon = (ob_player.weapon == OB_WEAPON_SWORD) ? OB_WEAPON_BOW : OB_WEAPON_SWORD;
    tone(OB_BUZZER_PIN, 500, 20);
  }

  // ── SPELL OF FORTITUDE (BTN3) ─────────────────────────────
  if (ob_spellBookPickedUp && ob_tombstoneRead &&
      ob_fortitudeFrames == 0 && ob_fortitudeCooldown == 0 &&
      ob_btnJustPressed(OB_GP_BTN3)) {
    ob_fortitudeFrames = OB_FORTITUDE_ACTIVE_FRAMES + OB_FORTITUDE_BLINK_TOTAL;
    tone(OB_BUZZER_PIN, 880, 80);  // non-blocking single tone, no delay
  }
  // Countdown fortitude
  if (ob_fortitudeFrames > 0) {
    ob_fortitudeFrames--;
    if (ob_fortitudeFrames == 0) {
      ob_fortitudeCooldown = OB_FORTITUDE_COOLDOWN_FRAMES;
    }
  }
  if (ob_fortitudeCooldown > 0) ob_fortitudeCooldown--;

  // ── MOVEMENT ──────────────────────────────────────────────
  ob_player.vx = 0;
  if (ob_btnDown(OB_GP_LEFT)) {
    ob_player.vx = -OB_WALK_SPEED;
    ob_player.facingRight = false;
    if (ob_player.onGround && ++ob_player.walkTick >= 4)
      { ob_player.walkTick = 0; ob_player.walkFrame = (ob_player.walkFrame+1)&3; }
  } else if (ob_btnDown(OB_GP_RIGHT)) {
    ob_player.vx = OB_WALK_SPEED;
    ob_player.facingRight = true;
    if (ob_player.onGround && ++ob_player.walkTick >= 4)
      { ob_player.walkTick = 0; ob_player.walkFrame = (ob_player.walkFrame+1)&3; }
  } else {
    ob_player.walkFrame = ob_player.walkTick = 0;
  }

  // ── JUMP ──────────────────────────────────────────────────
  if (ob_btnJustPressed(OB_GP_BTN2)) ob_player.jumpQueued = true;

  // ── ATTACK / SHOOT ────────────────────────────────────────
  if (ob_btnJustPressed(OB_GP_BTN1)) {
    if (ob_player.weapon == OB_WEAPON_SWORD && ob_player.attackFrames == 0) {
      ob_player.attackFrames = OB_ATTACK_FRAMES;
      tone(OB_BUZZER_PIN, 750, 25);
    } else if (ob_player.weapon == OB_WEAPON_BOW && ob_player.bowFrames == 0) {
      ob_player.bowFrames = OB_BOW_FRAMES;
      ob_fireArrow();
      tone(OB_BUZZER_PIN, 750, 20);
    }
  }

  if (ob_player.attackFrames     > 0) ob_player.attackFrames--;
  if (ob_player.bowFrames        > 0) ob_player.bowFrames--;
  if (ob_player.invincibleFrames > 0) ob_player.invincibleFrames--;

  ob_updatePhysics();
  if (!ob_inSpecialRoom) ob_updateCamera();
  // Update all world enemies every frame (visibility-based, not zone-based)
  if (!ob_inSpecialRoom) {
    for (uint8_t rm = 0; rm < OB_WORLD_ROOMS; rm++) {
      ob_updateWorldTurret(rm);
      ob_updateWorldBat(rm);
    }
  }
  ob_updateTurret();   // special rooms 10/11 only
  ob_updateBat();      // special rooms 10/11 only
  ob_updateBoss();
  ob_updateBullets();
  ob_updateArrows();
  ob_updateHearts();
  ob_updateKeyAndChest();   // key pickup + chest trigger

  // ── DEATH CHECK ───────────────────────────────────────────────
  if (ob_player.hp <= 0) {
    ob_drawFrame();
    ob_doGameOver();
    return;
  }

  ob_drawFrame();
}

// ============================================================================
// PUBLIC WATCHOS API
// ============================================================================
void obscurusEnter() {
  ob_active = true;
  if (!ob_inProgress) {
    // Fresh start — full reset
    obscurusSetup_Internal();
  }
  // If ob_inProgress is true we're resuming from the games submenu — skip reset
  ob_inProgress = false;
  ob_setupRan = true;
}

void obscurusUpdate() {
  if (!ob_active) return;
  obscurusLoop_Internal();
}

void obscurusExit() {
  ob_active = false;
  // NOTE: do NOT clear ob_inProgress here.
  // When exiting via the pause screen (BTN3), ob_inProgress is set true
  // just before gameExitToMenu() calls this function. Clearing it here
  // would destroy that flag before obscurusEnter() can read it.
  // ob_inProgress is cleared in obscurusEnter() after use, and stays
  // false on a cold launch (never set), so fresh starts still work correctly.
  noTone(OB_BUZZER_PIN);
}