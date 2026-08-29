/*
 * Dondji Firmware
 *
 * Copyright (c) 2026 BD1AHN
 *
 * Licensed under the Apache License, Version 2.0
 *
 * Project:
 *     叮咚鸡 (Dondji)
 *
 * Maintainer:
 *     BD1AHN
 *
 * Official Website:
 *     https://ethanyan6.github.io/Dondji/
 *
 * The Dondji name, logo, and official project identity
 * are protected separately from the source code license.
 *
 * Dino Run mini game (Chrome offline-dino style endless runner).
 */

#include "app/dino.h"

#ifdef ENABLE_FEAT_F4HWN_SCREENSHOT
#include "screenshot.h"
#endif

// --- Layout constants ---
#define GROUND_Y      54u   // y pixel of the ground line
#define DINO_X        10u   // fixed horizontal position of the dino

#define DINO_RUN_W     8u
#define DINO_RUN_H    10u
#define DINO_DUCK_W   12u
#define DINO_DUCK_H    6u

#define JUMP_VELOCITY (-7)
#define GRAVITY         1

#define OBS_MIN_W       4u
#define OBS_MAX_W       7u
#define OBS_MIN_H       7u
#define OBS_MAX_H      16u

#define SPEED_START     3u
#define SPEED_MAX       7u
#define SPEED_UP_EVERY 150u // score points needed for +1 speed

typedef struct {
    int16_t x;
    uint8_t w;
    uint8_t h;
    bool    active;
} Obstacle;

static bool isInitialized;
static KeyboardState kbd;

static int16_t dinoY;
static int8_t  dinoVY;
static bool    isJumping;
static bool    isDucking;

static uint16_t score;
static uint16_t bestScore;
static uint8_t  gameSpeed;
static bool     isGameOver;
static uint32_t frameCount;

static Obstacle obs;

static uint32_t randState = 1;

static uint16_t DinoRand(void)
{
    randState = randState * 1103515245u + 12345u;
    return (uint16_t)((randState >> 16) & 0x7FFFu);
}

static uint8_t DinoRandRange(uint8_t lo, uint8_t hi)
{
    return (uint8_t)(lo + (DinoRand() % (uint16_t)(hi - lo + 1)));
}

static uint8_t DinoHeight(void)
{
    return isDucking ? DINO_DUCK_H : DINO_RUN_H;
}

static uint8_t DinoWidth(void)
{
    return isDucking ? DINO_DUCK_W : DINO_RUN_W;
}

static int16_t DinoGroundTopY(void)
{
    return (int16_t)(GROUND_Y - DinoHeight());
}

static void SpawnObstacle(int16_t startX)
{
    obs.active = true;
    obs.x      = startX;
    obs.w      = DinoRandRange(OBS_MIN_W, OBS_MAX_W);
    obs.h      = DinoRandRange(OBS_MIN_H, OBS_MAX_H);
}

static void ResetGame(void)
{
    dinoY      = DinoGroundTopY();
    dinoVY     = 0;
    isJumping  = false;
    isDucking  = false;
    score      = 0;
    gameSpeed  = SPEED_START;
    isGameOver = false;
    frameCount = 0;

    SpawnObstacle(140);
}

static void FillRect(int16_t x, int16_t y, uint8_t w, uint8_t h)
{
    for (int16_t yy = y; yy < y + (int16_t)h; yy++) {
        if (yy < 0 || yy >= LCD_HEIGHT)
            continue;
        for (int16_t xx = x; xx < x + (int16_t)w; xx++) {
            if (xx < 0 || xx >= LCD_WIDTH)
                continue;
            UI_DrawPixelBuffer(gFrameBuffer, (uint8_t)xx, (uint8_t)yy, true);
        }
    }
}

static void DrawGround(void)
{
    for (uint8_t x = 0; x < LCD_WIDTH; x++) {
        UI_DrawPixelBuffer(gFrameBuffer, x, GROUND_Y, true);
    }
}

// Dino sprites, 1 = pixel on, stored MSB-first (bit 15 = leftmost column).
static const uint16_t DINO_RUN_SPRITE[DINO_RUN_H] = {
    0x0200,  // ......#.
    0x0600,  // .....##.
    0x0600,  // .....##.
    0xFE00,  // #######.
    0xFE00,  // #######.
    0xFE00,  // #######.
    0x7C00,  // .#####..
    0x7C00,  // .#####..
    0x6C00,  // .##.##..
    0x4400,  // .#...#..
};

static const uint16_t DINO_DUCK_SPRITE[DINO_DUCK_H] = {
    0x0060,  // ...........##
    0x0060,  // ...........##
    0x3FC0,  // ..########..
    0x3FF0,  // ..##########
    0x0FC0,  // ....######..
    0x0CC0,  // ....##..##..
};

static void DrawSprite(int16_t x, int16_t y, const uint16_t *sprite, uint8_t w, uint8_t h)
{
    for (uint8_t row = 0; row < h; row++) {
        for (uint8_t col = 0; col < w; col++) {
            if (sprite[row] & (0x8000u >> col)) {
                const int16_t px = x + (int16_t)col;
                const int16_t py = y + (int16_t)row;
                if (px >= 0 && px < LCD_WIDTH && py >= 0 && py < LCD_HEIGHT) {
                    UI_DrawPixelBuffer(gFrameBuffer, (uint8_t)px, (uint8_t)py, true);
                }
            }
        }
    }
}

static void DrawDino(void)
{
    if (isDucking) {
        DrawSprite((int16_t)DINO_X, dinoY, DINO_DUCK_SPRITE, DINO_DUCK_W, DINO_DUCK_H);
        // eye
        UI_DrawPixelBuffer(gFrameBuffer, (uint8_t)(DINO_X + 10), (uint8_t)(dinoY + 1), false);
    } else {
        DrawSprite((int16_t)DINO_X, dinoY, DINO_RUN_SPRITE, DINO_RUN_W, DINO_RUN_H);
        // eye
        UI_DrawPixelBuffer(gFrameBuffer, (uint8_t)(DINO_X + 6), (uint8_t)(dinoY + 1), false);
    }
}

static void PlayJumpSound(void)
{
    BK4819_PlayTone(880, true);
    AUDIO_AudioPathOn();
    BK4819_ExitTxMute();
    SYSTEM_DelayMs(40);
    BK4819_EnterTxMute();
    AUDIO_AudioPathOff();
}

static void DrawObstacle(void)
{
    if (!obs.active)
        return;
    if (obs.x + (int16_t)obs.w < 0 || obs.x >= LCD_WIDTH)
        return;

    const int16_t topY = (int16_t)GROUND_Y - (int16_t)obs.h;
    FillRect(obs.x, topY, obs.w, obs.h);

    // "arms" to make it read as a cactus rather than a plain bar
    if (obs.h >= 10) {
        FillRect((int16_t)(obs.x - 2), (int16_t)(topY + 3), 2, 2);
        FillRect((int16_t)(obs.x + obs.w), (int16_t)(topY + 6), 2, 2);
    }
}

static void DrawHUD(void)
{
    char str[12];

    sprintf(str, "%u", score);
    GUI_DisplaySmallest(str, 2, 2, false, true);

    sprintf(str, "HI%u", bestScore);
    GUI_DisplaySmallest(str, 90, 2, false, true);
}

static void DrawGameOver(void)
{
    const bool isCn = (gUiLanguage == UI_LANGUAGE_CN);
    const char *msg = isCn ? "游戏结束" : "GAME OVER";
    UI_PrintStringSmallAtPixel(msg, 0, 127, 24, 40, 0);

    char str[16];
    sprintf(str, isCn ? "%u  MENU重开" : "%u  MENU=Retry", score);
    GUI_DisplaySmallest(str, 20, 44, false, true);
}

static bool CheckCollision(void)
{
    if (!obs.active)
        return false;

    const int16_t dx1 = (int16_t)DINO_X;
    const int16_t dx2 = (int16_t)(DINO_X + DinoWidth() - 1);
    const int16_t dy1 = dinoY;
    const int16_t dy2 = (int16_t)(dinoY + DinoHeight() - 1);

    const int16_t ox1 = obs.x;
    const int16_t ox2 = (int16_t)(obs.x + obs.w - 1);
    const int16_t oy1 = (int16_t)GROUND_Y - (int16_t)obs.h;
    const int16_t oy2 = (int16_t)GROUND_Y - 1;

    return (dx1 <= ox2) && (dx2 >= ox1) && (dy1 <= oy2) && (dy2 >= oy1);
}

static void UpdateGame(void)
{
    frameCount++;

    // Score ticks up over time, speed ramps up with it
    if ((frameCount % 4u) == 0u) {
        score++;
        if (score > bestScore)
            bestScore = score;
        if ((score % SPEED_UP_EVERY) == 0u && gameSpeed < SPEED_MAX)
            gameSpeed++;
    }

    // Jump physics
    if (isJumping) {
        dinoVY += GRAVITY;
        dinoY  += dinoVY;
        if (dinoY >= DinoGroundTopY()) {
            dinoY     = DinoGroundTopY();
            dinoVY    = 0;
            isJumping = false;
        }
    } else {
        dinoY = DinoGroundTopY();
    }

    // Obstacle movement / respawn
    if (obs.active) {
        obs.x -= (int16_t)gameSpeed;
        if (obs.x + (int16_t)obs.w < 0) {
            const uint8_t gap = DinoRandRange(20, 55);
            SpawnObstacle((int16_t)(LCD_WIDTH + gap));
        }
    }

    if (CheckCollision()) {
        isGameOver = true;
        BK4819_PlayTone(400, true);
        AUDIO_AudioPathOn();
        BK4819_ExitTxMute();
        SYSTEM_DelayMs(120);
        BK4819_EnterTxMute();
        AUDIO_AudioPathOff();
    }
}

static bool HandleInput(void)
{
    kbd.prev    = kbd.current;
    kbd.current = KEYBOARD_GetKey();

    if (kbd.current == KEY_EXIT) {
        kbd.counter++;
        if (kbd.counter > 20) {
            isInitialized = false;
            return false;
        }
    } else if (kbd.prev == KEY_EXIT && kbd.counter > 0 && kbd.counter <= 20) {
        isInitialized = false;
        return false;
    } else {
        kbd.counter = 0;
    }

    // Duck is level-triggered: held down while running, released to stand
    isDucking = (kbd.current == KEY_DOWN) && !isJumping && !isGameOver;

    if (kbd.current == KEY_INVALID)
        return true;

    if (kbd.current != kbd.prev) {
        switch (kbd.current) {
        case KEY_UP:
        case KEY_MENU:
        case KEY_PTT:
        case KEY_F:
            if (isGameOver) {
                ResetGame();
            } else if (!isJumping && !isDucking) {
                isJumping = true;
                dinoVY    = JUMP_VELOCITY;
                PlayJumpSound();
            }
            break;
        default:
            break;
        }
    }

    return true;
}

bool APP_IsDinoActive(void)
{
    return isInitialized;
}

void APP_RunDino(void)
{
    BACKLIGHT_UpdateTickless();

    UI_DisplayClear();
    memset(gStatusLine, 0, sizeof(gStatusLine));

    randState = gPowerOnSeconds * 2654435761u + frameCount + 1u;

    isInitialized = true;
    kbd.current   = KEY_INVALID;
    kbd.prev      = KEY_INVALID;
    kbd.counter   = 0;

    ResetGame();

    // Wait for the key that opened this screen to be released
    while (KEYBOARD_GetKey() != KEY_INVALID) {
        SYSTEM_DelayMs(10);
    }
    SYSTEM_DelayMs(100);

    while (isInitialized) {
        #ifdef ENABLE_FEAT_F4HWN_SCREENSHOT
            SCREENSHOT_ParseInput();
        #endif

        HandleInput();

        if (!isGameOver) {
            UpdateGame();
        }

        for (int row = 0; row < FRAME_LINES; row++) {
            memset(gFrameBuffer[row], 0, LCD_WIDTH);
        }
        memset(gStatusLine, 0, sizeof(gStatusLine));

        DrawGround();
        DrawDino();
        DrawObstacle();
        DrawHUD();

        if (isGameOver) {
            DrawGameOver();
        }

        ST7565_BlitStatusLine();
        ST7565_BlitFullScreen();

        #ifdef ENABLE_FEAT_F4HWN_SCREENSHOT
            SCREENSHOT_Update(false);
        #endif

        SYSTEM_DelayMs(40);
    }
}