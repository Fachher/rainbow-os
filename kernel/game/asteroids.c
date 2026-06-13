/* Asteroids — a vector-style game for the 800x600x8bpp graphical console.
 *
 * Renders into a RAM back buffer (fast, no bank switching) and blits it to the
 * framebuffer once per frame. Paced by the PIT timer; held-key input via the
 * keyboard driver's key-state API. All math is fixed-point (no FPU). */

#include "asteroids.h"
#include "drivers/svga.h"
#include "drivers/keyboard.h"
#include "drivers/vga.h"
#include "drivers/timer.h"
#include "lib/string.h"
#include "include/types.h"

#define W           SVGA_WIDTH
#define H           SVGA_HEIGHT

#define FBITS       8                 /* position/velocity fractional bits */
#define FP(n)       ((int32_t)(n) << FBITS)
#define TO_PX(v)    ((int32_t)(v) >> FBITS)

#define ASHIFT      14                /* sin/cos table scale = 1<<14 */
#define SIN(a)      ((int32_t)sin_tbl[(uint8_t)(a)])
#define COS(a)      ((int32_t)sin_tbl[(uint8_t)((a) + 64)])

#define FRAME_TICKS 3                 /* PIT ticks per frame (100Hz -> ~33fps) */

#define MAX_BULLETS 8
#define MAX_ROCKS   64

/* Colors (console palette indices 0-15) */
#define C_BLACK     0
#define C_WHITE     15
#define C_CYAN      11
#define C_YELLOW    14
#define C_RED       12
#define C_GREY      7

/* Scancodes (raw Set 1) */
#define SC_ESC      0x01
#define SC_W        0x11
#define SC_Q        0x10
#define SC_A        0x1E
#define SC_D        0x20
#define SC_SPACE    0x39
#define EXT_LEFT    0x4B
#define EXT_RIGHT   0x4D
#define EXT_UP      0x48

static const int16_t sin_tbl[256] = {
         0,   402,   804,  1205,  1606,  2006,  2404,  2801,
      3196,  3590,  3981,  4370,  4756,  5139,  5520,  5897,
      6270,  6639,  7005,  7366,  7723,  8076,  8423,  8765,
      9102,  9434,  9760, 10080, 10394, 10702, 11003, 11297,
     11585, 11866, 12140, 12406, 12665, 12916, 13160, 13395,
     13623, 13842, 14053, 14256, 14449, 14635, 14811, 14978,
     15137, 15286, 15426, 15557, 15679, 15791, 15893, 15986,
     16069, 16143, 16207, 16261, 16305, 16340, 16364, 16379,
     16384, 16379, 16364, 16340, 16305, 16261, 16207, 16143,
     16069, 15986, 15893, 15791, 15679, 15557, 15426, 15286,
     15137, 14978, 14811, 14635, 14449, 14256, 14053, 13842,
     13623, 13395, 13160, 12916, 12665, 12406, 12140, 11866,
     11585, 11297, 11003, 10702, 10394, 10080,  9760,  9434,
      9102,  8765,  8423,  8076,  7723,  7366,  7005,  6639,
      6270,  5897,  5520,  5139,  4756,  4370,  3981,  3590,
      3196,  2801,  2404,  2006,  1606,  1205,   804,   402,
         0,  -402,  -804, -1205, -1606, -2006, -2404, -2801,
     -3196, -3590, -3981, -4370, -4756, -5139, -5520, -5897,
     -6270, -6639, -7005, -7366, -7723, -8076, -8423, -8765,
     -9102, -9434, -9760,-10080,-10394,-10702,-11003,-11297,
    -11585,-11866,-12140,-12406,-12665,-12916,-13160,-13395,
    -13623,-13842,-14053,-14256,-14449,-14635,-14811,-14978,
    -15137,-15286,-15426,-15557,-15679,-15791,-15893,-15986,
    -16069,-16143,-16207,-16261,-16305,-16340,-16364,-16379,
    -16384,-16379,-16364,-16340,-16305,-16261,-16207,-16143,
    -16069,-15986,-15893,-15791,-15679,-15557,-15426,-15286,
    -15137,-14978,-14811,-14635,-14449,-14256,-14053,-13842,
    -13623,-13395,-13160,-12916,-12665,-12406,-12140,-11866,
    -11585,-11297,-11003,-10702,-10394,-10080, -9760, -9434,
     -9102, -8765, -8423, -8076, -7723, -7366, -7005, -6639,
     -6270, -5897, -5520, -5139, -4756, -4370, -3981, -3590,
     -3196, -2801, -2404, -2006, -1606, -1205,  -804,  -402,
};

static uint8_t back[W * H];

static uint32_t rng = 0x1234abcd;
static uint32_t rnd(void) {
    rng = rng * 1103515245u + 12345u;
    return (rng >> 16) & 0x7FFF;
}
static int rnd_range(int lo, int hi) {
    return lo + (int)(rnd() % (uint32_t)(hi - lo + 1));
}

/* ---- Entities ---------------------------------------------------------- */

#define NV 10   /* vertices per asteroid polygon */

struct ship {
    int32_t x, y, vx, vy;
    uint8_t angle;
    int invuln;     /* frames of spawn invulnerability */
};

struct bullet {
    int32_t x, y, vx, vy;
    int life;
    int active;
};

struct rock {
    int32_t x, y, vx, vy;
    int size;       /* 0 small, 1 medium, 2 large */
    int radius;     /* px */
    uint8_t rot, spin;
    int8_t jag[NV];
    int active;
};

static struct ship   ship;
static struct bullet bullets[MAX_BULLETS];
static struct rock   rocks[MAX_ROCKS];
static int score, lives, level;

static const int rock_radius[3] = { 12, 24, 42 };

/* ---- Drawing into the back buffer -------------------------------------- */

static inline void px(int x, int y, uint8_t c) {
    if ((unsigned)x < (unsigned)W && (unsigned)y < (unsigned)H)
        back[y * W + x] = c;
}

static void gline(int x0, int y0, int x1, int y1, uint8_t c) {
    int dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int dy = y1 > y0 ? y1 - y0 : y0 - y1;
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx - dy;
    for (;;) {
        px(x0, y0, c);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 <  dx) { err += dx; y0 += sy; }
    }
}

static void gfill(int x, int y, int w, int h, uint8_t c) {
    for (int yy = y; yy < y + h; yy++)
        for (int xx = x; xx < x + w; xx++)
            px(xx, yy, c);
}

static void gchar(int x, int y, char ch, uint8_t c) {
    const uint8_t *g = svga_rom_font() + (uint32_t)(uint8_t)ch * 16;
    for (int row = 0; row < 16; row++)
        for (int col = 0; col < 8; col++)
            if (g[row] & (0x80 >> col))
                px(x + col, y + row, c);
}

static void gtext(int x, int y, const char *s, uint8_t c) {
    while (*s) {
        gchar(x, y, *s++, c);
        x += 8;
    }
}

static void gnum(int x, int y, int n, uint8_t c) {
    char buf[12];
    int i = 0;
    if (n == 0) buf[i++] = '0';
    while (n > 0) { buf[i++] = (char)('0' + n % 10); n /= 10; }
    while (i > 0) { gchar(x, y, buf[--i], c); x += 8; }
}

/* ---- Game logic -------------------------------------------------------- */

static void wrap(int32_t *x, int32_t *y) {
    if (*x < 0)         *x += FP(W);
    if (*x >= FP(W))    *x -= FP(W);
    if (*y < 0)         *y += FP(H);
    if (*y >= FP(H))    *y -= FP(H);
}

static void spawn_rock(int32_t x, int32_t y, int size) {
    for (int i = 0; i < MAX_ROCKS; i++) {
        if (rocks[i].active) continue;
        struct rock *r = &rocks[i];
        r->active = 1;
        r->size = size;
        r->radius = rock_radius[size];
        r->x = x;
        r->y = y;
        int spd = 80 + (2 - size) * 90;     /* smaller = faster */
        r->vx = rnd_range(-spd, spd);
        r->vy = rnd_range(-spd, spd);
        r->rot = (uint8_t)rnd_range(0, 255);
        r->spin = (uint8_t)rnd_range(0, 3) - 1;
        for (int v = 0; v < NV; v++)
            r->jag[v] = (int8_t)rnd_range(-r->radius / 4, r->radius / 4);
        return;
    }
}

static void draw_rock(const struct rock *r) {
    int cx = TO_PX(r->x), cy = TO_PX(r->y);
    int px0 = 0, py0 = 0, fx = 0, fy = 0;
    for (int v = 0; v <= NV; v++) {
        int k = v % NV;
        uint8_t a = (uint8_t)(r->rot + k * (256 / NV));
        int rr = r->radius + r->jag[k];
        int vx = cx + ((rr * COS(a)) >> ASHIFT);
        int vy = cy + ((rr * SIN(a)) >> ASHIFT);
        if (v == 0) { fx = vx; fy = vy; }
        else        gline(px0, py0, vx, vy, C_GREY);
        px0 = vx; py0 = vy;
        if (v == NV) gline(px0, py0, fx, fy, C_GREY);
    }
}

static void new_wave(void) {
    level++;
    int n = 3 + level;
    if (n > 11) n = 11;
    for (int i = 0; i < n; i++) {
        int32_t x = FP(rnd_range(0, W - 1));
        int32_t y = FP(rnd_range(0, 60));        /* spawn near top edge */
        spawn_rock(x, y, 2);
    }
}

static void reset_ship(void) {
    ship.x = FP(W / 2);
    ship.y = FP(H / 2);
    ship.vx = ship.vy = 0;
    ship.angle = 192;    /* pointing up (screen y is down) */
    ship.invuln = 90;
}

static void fire_bullet(void) {
    for (int i = 0; i < MAX_BULLETS; i++) {
        if (bullets[i].active) continue;
        struct bullet *b = &bullets[i];
        b->active = 1;
        b->life = 50;
        int nose = 16;
        b->x = ship.x + ((nose * COS(ship.angle)) << (FBITS - 0) >> ASHIFT);
        b->y = ship.y + ((nose * SIN(ship.angle)) << (FBITS - 0) >> ASHIFT);
        int spd = 2200;
        b->vx = ship.vx + ((COS(ship.angle) * spd) >> ASHIFT);
        b->vy = ship.vy + ((SIN(ship.angle) * spd) >> ASHIFT);
        return;
    }
}

static void hit_rock(int idx) {
    struct rock *r = &rocks[idx];
    int size = r->size;
    int32_t x = r->x, y = r->y;
    r->active = 0;
    if (size == 2)      score += 20;
    else if (size == 1) score += 50;
    else                score += 100;
    if (size > 0) {
        spawn_rock(x, y, size - 1);
        spawn_rock(x, y, size - 1);
    }
}

static int count_rocks(void) {
    int n = 0;
    for (int i = 0; i < MAX_ROCKS; i++)
        if (rocks[i].active) n++;
    return n;
}

/* Returns 1 if the player asked to quit. */
static int read_quit(void) {
    return keyboard_is_down(SC_Q) || keyboard_is_down(SC_ESC);
}

static void draw_ship(int blink) {
    if (blink) return;
    int cx = TO_PX(ship.x), cy = TO_PX(ship.y);
    /* local triangle: nose +x, two rear corners */
    static const int lx[3] = { 16, -10, -10 };
    static const int ly[3] = {  0,  -9,   9 };
    int sxp[3], syp[3];
    for (int i = 0; i < 3; i++) {
        sxp[i] = cx + ((lx[i] * COS(ship.angle) - ly[i] * SIN(ship.angle)) >> ASHIFT);
        syp[i] = cy + ((lx[i] * SIN(ship.angle) + ly[i] * COS(ship.angle)) >> ASHIFT);
    }
    gline(sxp[0], syp[0], sxp[1], syp[1], C_CYAN);
    gline(sxp[1], syp[1], sxp[2], syp[2], C_CYAN);
    gline(sxp[2], syp[2], sxp[0], syp[0], C_CYAN);
}

static void render(int frame) {
    memset(back, C_BLACK, sizeof(back));

    for (int i = 0; i < MAX_ROCKS; i++)
        if (rocks[i].active) draw_rock(&rocks[i]);

    for (int i = 0; i < MAX_BULLETS; i++)
        if (bullets[i].active)
            gfill(TO_PX(bullets[i].x) - 1, TO_PX(bullets[i].y) - 1, 3, 3, C_YELLOW);

    draw_ship(ship.invuln > 0 && (frame & 4));

    gtext(10, 8, "SCORE", C_WHITE);
    gnum(58, 8, score, C_WHITE);
    gtext(W - 110, 8, "LIVES", C_WHITE);
    gnum(W - 38, 8, lives, C_WHITE);

    svga_blit(back);
}

void asteroids_run(void) {
    rng ^= timer_ticks() * 2654435761u + 1;
    score = 0;
    lives = 3;
    level = 0;
    for (int i = 0; i < MAX_BULLETS; i++) bullets[i].active = 0;
    for (int i = 0; i < MAX_ROCKS; i++)   rocks[i].active = 0;
    reset_ship();
    new_wave();

    int fire_cd = 0;
    int frame = 0;
    uint32_t last = timer_ticks();
    int running = 1;
    int gameover = 0;

    while (running) {
        while (timer_ticks() - last < FRAME_TICKS)
            __asm__ volatile("hlt");
        last += FRAME_TICKS;
        frame++;

        /* ---- input ---- */
        if (read_quit()) break;
        if (keyboard_is_down(SC_A) || keyboard_is_ext_down(EXT_LEFT))  ship.angle -= 5;
        if (keyboard_is_down(SC_D) || keyboard_is_ext_down(EXT_RIGHT)) ship.angle += 5;
        if (keyboard_is_down(SC_W) || keyboard_is_ext_down(EXT_UP)) {
            ship.vx += (COS(ship.angle) * 40) >> ASHIFT;
            ship.vy += (SIN(ship.angle) * 40) >> ASHIFT;
        }
        if (fire_cd > 0) fire_cd--;
        if (keyboard_is_down(SC_SPACE) && fire_cd == 0) {
            fire_bullet();
            fire_cd = 8;
        }

        /* ---- update ship ---- */
        ship.vx -= ship.vx >> 6;     /* drag */
        ship.vy -= ship.vy >> 6;
        ship.x += ship.vx;
        ship.y += ship.vy;
        wrap(&ship.x, &ship.y);
        if (ship.invuln > 0) ship.invuln--;

        /* ---- update bullets ---- */
        for (int i = 0; i < MAX_BULLETS; i++) {
            if (!bullets[i].active) continue;
            bullets[i].x += bullets[i].vx;
            bullets[i].y += bullets[i].vy;
            wrap(&bullets[i].x, &bullets[i].y);
            if (--bullets[i].life <= 0) bullets[i].active = 0;
        }

        /* ---- update rocks ---- */
        for (int i = 0; i < MAX_ROCKS; i++) {
            if (!rocks[i].active) continue;
            rocks[i].x += rocks[i].vx;
            rocks[i].y += rocks[i].vy;
            wrap(&rocks[i].x, &rocks[i].y);
            rocks[i].rot += rocks[i].spin;
        }

        /* ---- collisions: bullet vs rock ---- */
        for (int b = 0; b < MAX_BULLETS; b++) {
            if (!bullets[b].active) continue;
            int bx = TO_PX(bullets[b].x), by = TO_PX(bullets[b].y);
            for (int r = 0; r < MAX_ROCKS; r++) {
                if (!rocks[r].active) continue;
                int dx = bx - TO_PX(rocks[r].x);
                int dy = by - TO_PX(rocks[r].y);
                int rad = rocks[r].radius;
                if (dx * dx + dy * dy < rad * rad) {
                    bullets[b].active = 0;
                    hit_rock(r);
                    break;
                }
            }
        }

        /* ---- collisions: ship vs rock ---- */
        if (ship.invuln == 0) {
            int sx = TO_PX(ship.x), sy = TO_PX(ship.y);
            for (int r = 0; r < MAX_ROCKS; r++) {
                if (!rocks[r].active) continue;
                int dx = sx - TO_PX(rocks[r].x);
                int dy = sy - TO_PX(rocks[r].y);
                int rad = rocks[r].radius + 10;
                if (dx * dx + dy * dy < rad * rad) {
                    lives--;
                    if (lives <= 0) { running = 0; gameover = 1; }
                    else reset_ship();
                    break;
                }
            }
        }

        if (count_rocks() == 0) new_wave();

        render(frame);
    }

    /* ---- game over screen (only on death, not on quit) ---- */
    if (gameover) {
        memset(back, C_BLACK, sizeof(back));
        gtext(W / 2 - 36, H / 2 - 40, "GAME OVER", C_RED);
        gtext(W / 2 - 60, H / 2,      "SCORE", C_WHITE);
        gnum(W / 2 - 60 + 48, H / 2, score, C_WHITE);
        gtext(W / 2 - 84, H / 2 + 40, "PRESS A KEY TO RETURN", C_GREY);
        svga_blit(back);
        keyboard_flush();
        keyboard_getchar();          /* block until any key */
    }

    keyboard_flush();
    vga_clear();
    vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    vga_write("[OK] Returned to console (final score ");
    vga_write_dec((uint32_t)score);
    vga_write(")\n");
    vga_set_color(VGA_WHITE, VGA_BLACK);
}
