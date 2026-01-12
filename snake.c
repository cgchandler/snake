// © 2026 Christopher G Chandler
// Licensed under the MIT License. See LICENSE file in the project root.
#include <c64/joystick.h>
#include <c64/vic.h>
#include <c64/keyboard.h>
#include <stdlib.h>
#include <string.h>

// SID register macros (raw access)
#define SID_V1_FREQ_LO  (*(volatile byte*)0xD400)
#define SID_V1_FREQ_HI  (*(volatile byte*)0xD401)
#define SID_V1_PW_LO    (*(volatile byte*)0xD402)
#define SID_V1_PW_HI    (*(volatile byte*)0xD403)
#define SID_V1_CTRL     (*(volatile byte*)0xD404)
#define SID_V1_AD       (*(volatile byte*)0xD405)
#define SID_V1_SR       (*(volatile byte*)0xD406)

#define SID_V2_FREQ_LO  (*(volatile byte*)0xD407)
#define SID_V2_FREQ_HI  (*(volatile byte*)0xD408)
#define SID_V2_PW_LO    (*(volatile byte*)0xD409)
#define SID_V2_PW_HI    (*(volatile byte*)0xD40A)
#define SID_V2_CTRL     (*(volatile byte*)0xD40B)
#define SID_V2_AD       (*(volatile byte*)0xD40C)
#define SID_V2_SR       (*(volatile byte*)0xD40D)

#define SID_V3_FREQ_LO  (*(volatile byte*)0xD40E)
#define SID_V3_FREQ_HI  (*(volatile byte*)0xD40F)
#define SID_V3_PW_LO    (*(volatile byte*)0xD410)
#define SID_V3_PW_HI    (*(volatile byte*)0xD411)
#define SID_V3_CTRL     (*(volatile byte*)0xD412)
#define SID_V3_AD       (*(volatile byte*)0xD413)
#define SID_V3_SR       (*(volatile byte*)0xD414)

#define SID_MODE_VOL    (*(volatile byte*)0xD418)

#define CIA1_TA_LO      (*(volatile byte*)0xDC04)
#define CIA1_TA_HI      (*(volatile byte*)0xDC05)
#define VIC_RASTER      (*(volatile byte*)0xD012)


// Control bits
#define SID_CTRL_GATE   0x01
#define SID_CTRL_SYNC   0x02
#define SID_CTRL_RING   0x04
#define SID_CTRL_TEST   0x08
#define SID_CTRL_TRI    0x10
#define SID_CTRL_SAW    0x20
#define SID_CTRL_RECT   0x40
#define SID_CTRL_NOISE  0x80

#define PETSCII_CIRCLE  81      // PETSCII code for filled circle (used for snake head)
#define PETSCII_HEART   83      // PETSCII code for heart (used for fruit)
#define PETSCII_BLOCK   160     // PETSCII code for solid block (used for borders)

// Position/Direction on screen
struct Point
{
	sbyte	x, y;
};

struct Snake
{
	Point	head;		// Position of head
	Point	dir;		// Direction of head
	Point	tail[256];	// Position of tail
	byte	length;		// Length of tail
	byte	pos;		// Tail start
};

enum GameState
{
	GS_READY,		// Getting ready
	GS_PLAYING,		// Playing the game
	GS_COLLIDE,		// Collided with something
	GS_PAUSED   	// New 12/04/2025 CC
};

struct Game
{
    GameState   state;	
    byte        count;
    Snake       snake;          // use an array for multiplayer
    byte        pauseButtonPrev;
    byte        pauseFlashCounter;
    byte        pauseVisible;
    word        score;          // hearts collected
    word        highScore;      // NEW: best score so far
}   TheGame;

enum ControlMode
{
    CTRL_JOYSTICK = 0,
    CTRL_KEYBOARD = 1
};

static byte g_controlMode = CTRL_JOYSTICK;

// Screen and color ram address

#define Screen ((byte *)0x0400)
#define Color ((byte *)0xd800)

#define MAX_DELAY_FRAMES 20	
#define MIN_DELAY_FRAMES 4

#define PAUSE_W  11
#define PAUSE_H  3
#define PAUSE_X  ((40 - PAUSE_W) / 2)
#define PAUSE_Y  ((25 - PAUSE_H) / 2)
#define PAUSE_FLASH_FRAMES 30
#define HS_FLASH_INTERVAL 4   // frames between on/off, tweak for faster/slower flash
#define SPEED_MAX_VALUE   (MAX_DELAY_FRAMES - MIN_DELAY_FRAMES)   // 16 for 20..4
#define SPEED_CURVE_SCALE 6   // try 2, 3, or 4 to adjust how fast it ramps
#define COLLIDE_FRAMES 120    // frames to show collision flash

static byte pause_backup_chars[PAUSE_H][PAUSE_W];
static byte pause_backup_colors[PAUSE_H][PAUSE_W];

static word hud_lastScore = 0xFFFF;
static byte hud_lastSpeed = 0xFF;
static word hud_lastHighScore = 0xFFFF;
static byte highScoreFlashCount = 0;   // how many toggles left
static byte highScoreFlashTimer = 0;   // frames until next toggle
static byte highScoreFlashOn    = 0;   // 1 when digits visible during flash


// Forward declarations
byte snake_delay_quadratic(byte length);
byte snake_delay(byte length);

void sound_init(void);
void sound_step(void);
void sound_heart(void);
void sound_update(void);
void sound_highscore(void);
void sound_death(void);
void sound_stop_all(void);

static byte fruit_x = 0;
static byte fruit_y = 0;

// Put one  char on screen
inline void screen_put(byte x, byte y, char ch, char color)
{
	Screen[40 * y + x] = ch;
	Color[40 * y + x] = color;
}

// Get one char from screen
inline char screen_get(byte x, byte y)
{
	return Screen[40 * y + x];
}

// PETSCII to screen code helper
byte petscii_to_screen(char c)
{
	// Handle space explicitly
	if (c == ' ')
		return 32;         // space in screen code

	// Uppercase letters and digits
	if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))
		return (byte)(c & 0x3F);  // maps letters/digits to correct screen codes

	// Fallback: return as is (for punctuation etc)
	return (byte)c;
}

// Print a PETSCII string to screen
void screen_print_petscii(byte x, byte y, const char *text, byte color)
{
	while (*text && x < 40)
	{
		char c = *text++;
		byte sc = petscii_to_screen(c);
		screen_put(x++, y, sc, color);
	}
}

// Draw big 5x5 block text using PETSCII_BLOCK. Characters are drawn
// with no horizontal gap so up to 8 characters fit across 40 columns.
static unsigned char get_font_row(char ch, int row)
{
    switch (ch)
    {
        case 'S':
            // 11111
            // 10000
            // 11111
            // 00001
            // 11111
            return (row==0)?0b11111:(row==1)?0b10000:(row==2)?0b11111:(row==3)?0b00001:0b11111;
        case 'N':
            // 10001
            // 11001
            // 10101
            // 10011
            // 10001
            return (row==0)?0b10001:(row==1)?0b11001:(row==2)?0b10101:(row==3)?0b10011:0b10001;
        case 'A':
            // 01110
            // 10001
            // 11111
            // 10001
            // 10001
            return (row==0)?0b01110:(row==1)?0b10001:(row==2)?0b11111:(row==3)?0b10001:0b10001;
        case 'K':
            // 10001
            // 10010
            // 11100
            // 10010
            // 10001
            return (row==0)?0b10001:(row==1)?0b10010:(row==2)?0b11100:(row==3)?0b10010:0b10001;
        case 'E':
            // 11111
            // 10000
            // 11110
            // 10000
            // 11111
            return (row==0)?0b11111:(row==1)?0b10000:(row==2)?0b11110:(row==3)?0b10000:0b11111;
        case '6':
            // 11111
            // 10000
            // 11111
            // 10001
            // 11110
            return (row==0)?0b11111:(row==1)?0b10000:(row==2)?0b11111:(row==3)?0b10001:0b11111;
        case '4':
            // 10010
            // 10010
            // 11111
            // 00010
            // 00010
            return (row==0)?0b10010:(row==1)?0b10010:(row==2)?0b11111:(row==3)?0b00010:0b00010;
        case ' ':
        default:
            return 0x00;
    }
}

static void draw_big_text(byte x0, byte y0, const char *text, char color)
{
    for (int i = 0; text[i]; i++)
    {
        char ch = text[i];
        for (int row = 0; row < 5; row++)
        {
            unsigned char bits = get_font_row(ch, row) & 0x1F; // 5 bits
            for (int col = 0; col < 5; col++)
            {
                if (bits & (1 << (4 - col)))
                {
                    /* Add one-column gap between characters by using 6-wide
                       spacing (5 pixels + 1 blank). */
                    screen_put((byte)(x0 + i*6 + col), (byte)(y0 + row), PETSCII_BLOCK, color);
                }
                else
                {
                    // leave existing background (or explicitly clear with space)
                }
            }
        }
    }
}

// Print a fixed width decimal number at (x,y)
void screen_print_number(byte x, byte y, word value, byte width, byte color)
{
	char buf[5]; // up to 4 digits + null
	byte i;

	if (width > 4) width = 4;

	for (i = 0; i < width; i++)
	{
		buf[width - 1 - i] = '0' + (value % 10);
		value /= 10;
	}

	for (i = 0; i < width && x < 40; i++, x++)
	{
		byte sc = petscii_to_screen(buf[i]);
		screen_put(x, y, sc, color);
	}
}

// Map delay (frames) to speed 1..SPEED_MAX_VALUE
byte snake_speed_from_delay(byte delay)
{
	byte speed;

	// Clamp delay to allowed range
	if (delay <= MIN_DELAY_FRAMES)
		return SPEED_MAX_VALUE;  // fastest
	if (delay >= MAX_DELAY_FRAMES)
		return 1;                // slowest

	speed = MAX_DELAY_FRAMES - delay; // 1..(MAX-MIN)

	if (speed < 1)
		speed = 1;
	else if (speed > SPEED_MAX_VALUE)
		speed = SPEED_MAX_VALUE;

	return speed;
}

// Current speed based on snake length
byte snake_current_speed(void)
{
    byte delay = snake_delay(TheGame.snake.length);
    return snake_speed_from_delay(delay);
}

// Draw HUD labels once and reset cached values
void hud_init(void)
{
    // Clear row 0 once
    for (byte x = 0; x < 40; x++)
    {
        screen_put(x, 0, ' ', VCOL_BLACK);
    }

    // Score label
    screen_print_petscii(1, 0, "SCORE:", VCOL_LT_GREY);

    // Speed label
    screen_print_petscii(16, 0, "SPD:", VCOL_LT_GREY);

    // High score label (right aligned block)
    screen_print_petscii(33, 0, "HI:", VCOL_LT_GREY);

    // Force first update to draw numbers
    hud_lastScore     = 0xFFFF;
    hud_lastSpeed     = 0xFF;
    hud_lastHighScore = 0xFFFF;

    highScoreFlashCount = 0;
    highScoreFlashTimer = 0;
    highScoreFlashOn    = 0;
}

// Update HUD numeric values only when they change
void hud_update(void)
{
    byte speed = snake_current_speed();

    if (TheGame.score != hud_lastScore)
    {
        screen_print_number(8, 0, TheGame.score, 3, VCOL_WHITE);
        hud_lastScore = TheGame.score;
    }

    if (speed != hud_lastSpeed)
    {
        screen_print_number(21, 0, speed, 2, VCOL_WHITE);
        hud_lastSpeed = speed;
    }

    // High score flashing logic
    if (highScoreFlashCount)
    {
        // count down to next toggle
        if (highScoreFlashTimer > 0)
        {
            highScoreFlashTimer--;
        }
        else
        {
            // time to toggle visibility
            highScoreFlashTimer = HS_FLASH_INTERVAL;
            highScoreFlashOn = !highScoreFlashOn;
            highScoreFlashCount--;

            if (highScoreFlashOn)
            {
                // show high score digits
                screen_print_number(36, 0, TheGame.highScore, 3, VCOL_WHITE);
            }
            else
            {
                // hide digits by printing spaces
                screen_print_petscii(36, 0, "   ", VCOL_BLACK);
            }
        }
    }
    else
    {
        // normal static display when not flashing
        if (TheGame.highScore != hud_lastHighScore)
        {
            screen_print_number(36, 0, TheGame.highScore, 3, VCOL_WHITE);
            hud_lastHighScore = TheGame.highScore;
        }
    }
}

// Initialize the random number generation using CIA and Current Scan Line
void random_init(void)
{
    // Seed RNG after user interaction so it differs each run
    {
        unsigned seed = ((unsigned)CIA1_TA_HI << 8) | CIA1_TA_LO;
        seed ^= (unsigned)VIC_RASTER;
        srand(seed);
    }
}

// Put a fruit/heart at random position
void screen_fruit(void)
{
    byte x, y;
    do
    {       
		// Draw a random position inside new borders:
		// x: 1..38, y: 2..23 (since top border is at y=1, bottom at y=24)
        x = 1 + (rand() % 38);     // 1..38
        y = 2 + (rand() % 22);     // 2..23
		// Ensure it is an empty place	
    } while (screen_get(x, y) != ' ');

	// Put the heart on screen
    screen_put(x, y, PETSCII_HEART, VCOL_RED);

	// Save the heart position
    fruit_x = x;
    fruit_y = y;
}

// Clear screen and draw borders (top border now at row 1)
void screen_init(void)
{
	// Fill screen with spaces
	memset(Screen, ' ', 1000);

	// Bottom and top row (top at y=1, bottom at y=24)
	for(byte x=0; x<40; x++)
	{
		screen_put(x,  1, PETSCII_BLOCK, VCOL_LT_GREY);
		screen_put(x, 24, PETSCII_BLOCK, VCOL_LT_GREY);
	}

	// Left and right column, from y=1 to y=24
	for(byte y=1; y<25; y++)
	{
		screen_put( 0,  y, PETSCII_BLOCK, VCOL_LT_GREY);
		screen_put( 39, y, PETSCII_BLOCK, VCOL_LT_GREY);			
	}
}

// Initialize a snake
void snake_init(Snake * s)
{
	// Length of tail is one
	s->length = 1;
	s->pos = 0;

	// Snake in the center of playfield (inside new borders)
	s->head.x = 20;
	s->head.y = 13;   // was 12; this keeps it visually centered between 2..23

	// Starting to the right
	s->dir.x = 1;
	s->dir.y = 0;

	// Show head
	screen_put(s->head.x, s->head.y, PETSCII_CIRCLE, VCOL_WHITE);
}

bool snake_advance(Snake * s)
{
	// Promote head to start of tail
	s->tail[s->pos] = s->head;
	s->pos++;

	// step sound on every advance
	sound_step();

	screen_put(s->head.x, s->head.y, PETSCII_CIRCLE, VCOL_LT_BLUE);

	// Advance head
	s->head.x += s->dir.x;
	s->head.y += s->dir.y;

	// Get character at new head position
	char ch = screen_get(s->head.x, s->head.y);

	// Draw head
	screen_put(s->head.x, s->head.y, PETSCII_CIRCLE, VCOL_WHITE);

	// Clear tail
	byte tpos = (byte)(s->pos - s->length);
	screen_put(s->tail[tpos].x, s->tail[tpos].y, ' ', VCOL_BLACK);

	// Did snake collect the fruit
    if (ch == PETSCII_HEART)
    {
        // Extend tail
        s->length++;
        screen_fruit();

        // Increase score (hearts collected)
        TheGame.score++;

        // Update high score and start flash if beaten
        if (TheGame.score > TheGame.highScore)
        {
            TheGame.highScore = TheGame.score;

            // three flashes = six toggles (on/off)
            highScoreFlashCount = 6;
            highScoreFlashTimer = 0;    // toggle immediately on next hud_update
            highScoreFlashOn    = 1;    // start in "on" state
            hud_lastHighScore   = 0xFFFF; // force redraw logic later

            // New: high score coin sound
            sound_highscore();
        }

        // pickup sound on fruit
        sound_heart();		
    }
	else if (ch != ' ')
	{
		// Snake collided with something (wall, body, etc)
		return true;
	}

    // Sanity check: ensure there is still a heart at the remembered fruit position.
    // If something has erased it (and the head is not currently there), spawn a new one.
    {
        char c = screen_get(fruit_x, fruit_y);
        if (c != PETSCII_HEART &&
            !(s->head.x == fruit_x && s->head.y == fruit_y))
        {
            screen_fruit();
        }
    }

	return false;
}

// flash the snake after collision
void snake_flash(Snake * s, char c)
{
	// Loop over all tail elements
	for(byte i = 0; i < s->length; i++)
	{
		// Set color
		byte tpos = (byte)(s->pos - i - 1);
		screen_put(s->tail[tpos].x, s->tail[tpos].y, PETSCII_CIRCLE, c);
	}
}

// Change snake direction based on user input
void snake_control(Snake * s, sbyte jx, sbyte jy)
{
	// First change from horizontal to vertical, otherwise
	// check vertical to horizontal
	if (s->dir.x && jy)
	{
		s->dir.x = 0;
		s->dir.y = jy;
	}
	else if (s->dir.y && jx)
	{
		s->dir.y = 0;
		s->dir.x = jx;
	}
}

byte snake_delay_linear(byte length)
{
    if (length == 0) length = 1;        // safety
    return MAX_DELAY_FRAMES - ((length * (MAX_DELAY_FRAMES - MIN_DELAY_FRAMES)) / 255);
}

byte snake_delay_quadratic(byte length)
{
    if (length < 1) length = 1;

    // Scale length so the "effective" max is reached sooner
    // Larger SPEED_CURVE_SCALE => faster acceleration
    word x = (word)(length - 1) * SPEED_CURVE_SCALE;

    // Clamp to 0..255 for the quadratic math
    if (x > 255)
        x = 255;

    // Quadratic scaling: (x² / 255)
    word quad = (x * x) / 255;

    byte delay = MAX_DELAY_FRAMES 
               - ((quad * (MAX_DELAY_FRAMES - MIN_DELAY_FRAMES)) / 255);

    if (delay < MIN_DELAY_FRAMES)
        delay = MIN_DELAY_FRAMES;

    return delay;
}

byte snake_delay(byte length)
{
	return snake_delay_quadratic(length);
}

void game_state(GameState state)
{
	// Set new state
	TheGame.state = state;

	switch(state)
	{
    case GS_READY:
        // Clear the screen
        screen_init();
        // Draw HUD labels on row 0
        hud_init();

        TheGame.count = 32;
        TheGame.pauseButtonPrev = 0;   // safe reset
        break;

	case GS_PLAYING:
		// Init the snake
		snake_init(&TheGame.snake);

		// Reset score at start of each game
		TheGame.score = 0;

		// Initial fruit
		screen_fruit();

		TheGame.count = snake_delay(TheGame.snake.length);
		break;

	case GS_COLLIDE:
        TheGame.count = COLLIDE_FRAMES;
		break;

	case GS_PAUSED:
		// Optional: draw something else while paused
		break;
	}
}

// Colors for collision "animation"
char FlashColors[] = {
	VCOL_YELLOW,
	VCOL_WHITE,
	VCOL_LT_GREY,
	VCOL_YELLOW,
	VCOL_ORANGE,
	VCOL_RED,
	VCOL_MED_GREY,
	VCOL_DARK_GREY
};

void pause_backup_region(void)
{
	for (byte y = 0; y < PAUSE_H; y++)
	{
		for (byte x = 0; x < PAUSE_W; x++)
		{
			byte sx = PAUSE_X + x;
			byte sy = PAUSE_Y + y;
			pause_backup_chars[y][x]  = screen_get(sx, sy);
			pause_backup_colors[y][x] = Color[40 * sy + sx];
		}
	}
}

void pause_restore_region(void)
{
	for (byte y = 0; y < PAUSE_H; y++)
	{
		for (byte x = 0; x < PAUSE_W; x++)
		{
			byte sx = PAUSE_X + x;
			byte sy = PAUSE_Y + y;
			screen_put(sx, sy,
			           pause_backup_chars[y][x],
			           pause_backup_colors[y][x]);
		}
	}
}

void pause_draw_banner(void)
{
	const char *text = "GAME PAUSED";

	for (byte y = 0; y < PAUSE_H; y++)
	{
		for (byte x = 0; x < PAUSE_W; x++)
		{
			byte sx = PAUSE_X + x;
			byte sy = PAUSE_Y + y;

			if (y == 1)
			{
				// Middle row: text
				char c = text[x];
				byte sc = petscii_to_screen(c);
				screen_put(sx, sy, sc, VCOL_YELLOW);
			}
			else
			{
				// Top/bottom row: solid block for "large" look
				screen_put(sx, sy, PETSCII_BLOCK, VCOL_DARK_GREY);
			}
		}
	}
}

void pause_enter(void)
{
	pause_backup_region();
	TheGame.pauseFlashCounter = 0;
	TheGame.pauseVisible      = 0;
}

void pause_update(void)
{
	// Flash every X frames
	if (++TheGame.pauseFlashCounter >= PAUSE_FLASH_FRAMES)
	{
		TheGame.pauseFlashCounter = 0;

		if (TheGame.pauseVisible)
		{
			pause_restore_region();
			TheGame.pauseVisible = 0;
		}
		else
		{
			pause_draw_banner();
			TheGame.pauseVisible = 1;
		}
	}
}

void pause_exit(void)
{
	if (TheGame.pauseVisible)
	{
		pause_restore_region();
		TheGame.pauseVisible = 0;
	}
}

// --------------------------
// Simple SID sound engine
// Voice 1 - death SFX
// Voice 2 - step and heart SFX
// Voice 3 - high score SFX
// --------------------------

static byte step_toggle  = 0;

// Voice 1 death sound state
static byte      death_frames = 0; // frames remaining for death sound
static unsigned  death_freq   = 0; // current frequency for slide
static byte      v1_ctrl      = 0; // shadow copy of SID_V1_CTRL

// Voice 2 state
static byte      sfx2_frames  = 0;      // lifetime of current sound on voice 2
static byte      v2_ctrl      = 0;      // shadow copy of SID_V2_CTRL

// Voice 3 high score state
static byte      hs_active    = 0;      // 1 while coin sound is playing
static byte      hs_timer     = 0;      // frames until next arpeggio step
static byte      hs_index     = 0;      // which step of the arpeggio we are on
static byte      v3_ctrl      = 0;      // shadow copy of SID_V3_CTRL

// Simple upward arpeggio for coin style sound
static const unsigned hs_freqs[] = {
    0x1800,
    0x1C00,
    0x2000,
    0x2400
};

#define HS_STEPS (sizeof(hs_freqs) / sizeof(hs_freqs[0]))
#define HS_STEP_FRAMES 3          // frames per note in the arpeggio

void sound_init(void)
{
    // Master volume 15, no filter
    SID_MODE_VOL = 0x0F;

    // Voice 1: death SFX
    SID_V1_AD = 0x28;          // fast attack, short decay
    SID_V1_SR = 0x88;          // moderate sustain, release

    // Pulse width is not important for SAW, but set something valid
    SID_V1_PW_LO = 0x00;
    SID_V1_PW_HI = 0x08;

    // Sawtooth waveform, gate off to start
    v1_ctrl = SID_CTRL_SAW;
    SID_V1_CTRL = v1_ctrl;

    death_frames = 0;
    death_freq   = 0;

    // Voice 2 - general SFX voice
    SID_V2_AD = 0x48;          // attack 4, decay 8
    SID_V2_SR = 0x88;          // sustain 8, release 8

    // Triangle waveform, gate off
    v2_ctrl = SID_CTRL_TRI;    // triangle only, no gate
    SID_V2_CTRL = v2_ctrl;

    // Voice 3 - high score coin
    SID_V3_AD = 0x28;          // fast attack, short decay
    SID_V3_SR = 0x88;          // moderate sustain, release

    SID_V3_PW_LO = 0x00;       // pulse width around 1/4
    SID_V3_PW_HI = 0x08;

    v3_ctrl = SID_CTRL_RECT;   // pulse waveform, gate off
    SID_V3_CTRL = v3_ctrl;

    hs_active = 0;
    hs_timer  = 0;
    hs_index  = 0;
}

void sound_step(void)
{
    // Two mid range frequencies
    unsigned freq;

    if (!step_toggle)
        freq = 0x0900;          // medium pitch
    else
        freq = 0x0B00;          // slightly higher

    step_toggle ^= 1;

    SID_V2_FREQ_LO = (byte)(freq & 0xFF);
    SID_V2_FREQ_HI = (byte)(freq >> 8);

    // Triangle waveform, gate ON
    v2_ctrl = SID_CTRL_TRI | SID_CTRL_GATE;
    SID_V2_CTRL = v2_ctrl;

    // Short beep
    sfx2_frames = 6;
}

void sound_heart(void)
{
    // Higher, bright pickup sound
    unsigned freq = 0x1400;    // significantly higher pitch

    SID_V2_FREQ_LO = (byte)(freq & 0xFF);
    SID_V2_FREQ_HI = (byte)(freq >> 8);

    // Same triangle waveform, gate ON
    v2_ctrl = SID_CTRL_TRI | SID_CTRL_GATE;
    SID_V2_CTRL = v2_ctrl;

    // Longer beep for heart
    sfx2_frames = 14;
}

void sound_highscore(void)
{
    // Start or restart the coin arpeggio on voice 3
    hs_active = 1;
    hs_index  = 0;

    // Kick off first note immediately
    {
        unsigned freq = hs_freqs[0];
        SID_V3_FREQ_LO = (byte)(freq & 0xFF);
        SID_V3_FREQ_HI = (byte)(freq >> 8);
    }

    // Gate on pulse waveform
    v3_ctrl = SID_CTRL_RECT | SID_CTRL_GATE;
    SID_V3_CTRL = v3_ctrl;

    hs_timer = HS_STEP_FRAMES;
}

void sound_death(void)
{
    // Start a descending sawtooth tone on voice 1.
    // Think "waaah" style game over slide.

    death_frames = 24;        // total lifetime in frames
    death_freq   = 0x0C00;    // start fairly high

    // Set initial frequency
    SID_V1_FREQ_LO = (byte)(death_freq & 0xFF);
    SID_V1_FREQ_HI = (byte)(death_freq >> 8);

    // Gate on sawtooth
    v1_ctrl = SID_CTRL_SAW | SID_CTRL_GATE;
    SID_V1_CTRL = v1_ctrl;
}

void sound_update(void)
{
    // Voice 1 death sound slide
    if (death_frames)
    {
        death_frames--;

        // Slide the pitch downward a bit each frame
        if (death_freq > 0x0200)
        {
            death_freq -= 0x0018;   // tune this for a faster or slower slide
        }

        SID_V1_FREQ_LO = (byte)(death_freq & 0xFF);
        SID_V1_FREQ_HI = (byte)(death_freq >> 8);

        if (!death_frames)
        {
            // End of slide, gate off
            v1_ctrl &= (byte)~SID_CTRL_GATE;
            SID_V1_CTRL = v1_ctrl;
        }
    }

    // Voice 2 lifetime management
    if (sfx2_frames)
    {
        sfx2_frames--;
        if (!sfx2_frames)
        {
            // Turn off gate, keep waveform bits
            v2_ctrl &= (byte)~SID_CTRL_GATE;
            SID_V2_CTRL = v2_ctrl;
        }
    }

    // Voice 3 high score coin arpeggio
    if (hs_active)
    {
        if (hs_timer > 0)
            hs_timer--;

        if (!hs_timer)
        {
            hs_index++;

            if (hs_index >= HS_STEPS)
            {
                // End of arpeggio - turn off gate
                hs_active = 0;
                v3_ctrl &= (byte)~SID_CTRL_GATE;
                SID_V3_CTRL = v3_ctrl;
            }
            else
            {
                unsigned freq = hs_freqs[hs_index];
                SID_V3_FREQ_LO = (byte)(freq & 0xFF);
                SID_V3_FREQ_HI = (byte)(freq >> 8);

                hs_timer = HS_STEP_FRAMES;
            }
        }
    }
}

// Stop any ongoing sounds and turn off SID gates
void sound_stop_all(void)
{
    // Stop timers/state
    death_frames = 0;
    sfx2_frames  = 0;
    hs_active    = 0;
    hs_timer     = 0;
    hs_index     = 0;

    // Turn off gates on all voices
    v1_ctrl &= (byte)~SID_CTRL_GATE;
    SID_V1_CTRL = v1_ctrl;

    v2_ctrl &= (byte)~SID_CTRL_GATE;
    SID_V2_CTRL = v2_ctrl;

    v3_ctrl &= (byte)~SID_CTRL_GATE;
    SID_V3_CTRL = v3_ctrl;

    // Reset step toggle
    step_toggle = 0;
}

// Unified input: fills jx, jy, btn based on selected control mode
// jx: -1 left, +1 right, 0 none
// jy: -1 up,  +1 down,  0 none
// btn: 1 when "button" is pressed, else 0
void read_input(sbyte *jx, sbyte *jy, byte *btn)
{
    *jx = 0;
    *jy = 0;
    *btn = 0;

    if (g_controlMode == CTRL_JOYSTICK)
    {
        // joystick only (port 2)
        joy_poll(0);
        *jx  = joyx[0];
        *jy  = joyy[0];
        *btn = joyb[0] ? 1 : 0;
    }
    else
    {
        // keyboard only
        keyb_poll();

        // WASD for direction
        if (key_pressed(KSCAN_A))      *jx = -1;
        else if (key_pressed(KSCAN_D)) *jx =  1;

        if (key_pressed(KSCAN_W))      *jy = -1;
        else if (key_pressed(KSCAN_S)) *jy =  1;

        // Space as button
        if (key_pressed(KSCAN_SPACE))  *btn = 1;
    }
}

// Check for Joystick button
static int is_fire_pressed(void) {
    joy_poll(0);                // poll joystick port 2
    return joyb[0];             // joystick 2 button pressed
}

// Direct Hardware Scan for Spacebar - doesn't rely on keyb_poll() isn't reliable with joystick
static int is_space_pressed(void) {
    *(volatile byte*)0xDC00 = 0x7F; 
    return ((*(volatile byte*)0xDC01) & 0x10) == 0;
}

void select_controls(void)
{
    // Simple selection screen
    screen_init();

    // Large title using 5x5 block font 
    draw_big_text(5, 3, "SNAKE", VCOL_YELLOW);
    draw_big_text(5, 9, "64", VCOL_YELLOW);

    // Copyright notice
    screen_print_petscii(18, 09, "CHRIS CHANDLER", VCOL_CYAN);
    screen_print_petscii(21, 11, "COPYRIGHT", VCOL_WHITE);
    screen_print_petscii(21, 13, "(C)  2026", VCOL_WHITE);

    // Control hints
    screen_print_petscii(16,  16, "CONTROLS", VCOL_LT_RED);
    screen_print_petscii(11,  18, "JOYSTICK ON PORT 2", VCOL_WHITE);
    screen_print_petscii(13,  20, "KEYBOARD  WASD", VCOL_WHITE);
    screen_print_petscii(4,   22, "PAUSE - FIRE BUTTON OR SPACE BAR", VCOL_WHITE);

    // Wait for Joystick button or Spacebar
    while (1)
    {
        vic_waitFrame();

        if (is_fire_pressed())
        {
            g_controlMode = CTRL_JOYSTICK;
            break;
        }

        if (is_space_pressed())
        {
            g_controlMode = CTRL_KEYBOARD;
            break;
        }
    }

    screen_init();
}


// Main game loop, invoked every vsync
void game_loop(void)
{
    switch (TheGame.state)
    {
		case GS_READY:
			if (!--TheGame.count)
				game_state(GS_PLAYING);
			break;

		case GS_PLAYING:
		{
			sbyte jx, jy;
			byte  btn;

			// Read either joystick or keyboard depending on mode
			read_input(&jx, &jy, &btn);

			// Pause button handling (edge detect)
			if (btn && !TheGame.pauseButtonPrev)
			{
				TheGame.pauseButtonPrev = btn;
				pause_enter();
				TheGame.state = GS_PAUSED;
				return;
			}
			TheGame.pauseButtonPrev = btn;

			// Movement control
			snake_control(&TheGame.snake, jx, jy);

			if (!--TheGame.count)
			{
				if (snake_advance(&TheGame.snake))
				{
					sound_death();
					game_state(GS_COLLIDE);
				}
				else
				{
					TheGame.count = snake_delay(TheGame.snake.length);
				}
			}
			break;
		}

        case GS_COLLIDE:
        {
            int flash_count = (int)(sizeof(FlashColors) / sizeof(FlashColors[0]));
            int step = COLLIDE_FRAMES / flash_count;
            int idx = 0;

            /* Avoid division by zero and ensure idx is in range */
            if (step <= 0) step = 1;
            idx = (COLLIDE_FRAMES - TheGame.count) / step;
            if (idx < 0) idx = 0;
            if (idx >= flash_count) idx = flash_count - 1;

            snake_flash(&TheGame.snake, FlashColors[idx]);

            if (!--TheGame.count)
            {
                // Stop sounds then show controls and restart
                sound_stop_all();
                select_controls();
                random_init();
                game_state(GS_READY);
            }
        }
        break;

		case GS_PAUSED:
		{
			sbyte jx, jy;
			byte  btn;

			// Poll same input device while paused
			read_input(&jx, &jy, &btn);

			if (btn && !TheGame.pauseButtonPrev)
			{
				TheGame.pauseButtonPrev = btn;
				pause_exit();
				TheGame.state = GS_PLAYING;
				return;
			}
			else
			{
				TheGame.pauseButtonPrev = btn;
			}

			pause_update();
			break;
		}
    }
}

int main(void)
{
	// Screen color to black
	vic.color_border = VCOL_BLACK;
	vic.color_back = VCOL_BLACK;

	// Init sound
	sound_init();

    // Ask player which input to use
    select_controls();
	
    // Init random number generation
    random_init();

	// Start the game in ready state	
	game_state(GS_READY);

	// Forever
	for(;;)
	{
		// Wait one vsync
		vic_waitFrame();

		// Update sound envelopes / gates
		sound_update();

		// One game loop iteration
		game_loop();

        // Update HUD numeric values if changed
        hud_update();
	}

	// Never reached
	return 0;
}
