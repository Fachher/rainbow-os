/* Space Invaders — a ring-3 user program for Rainbow-OS.
 *
 * Ported from the in-kernel version: draws into its own RAM back buffer and
 * reaches the kernel only through syscalls (blit, ticks, keydown, getfont,
 * yield, clear). Pure integer math. */

#include "syscall.h"
#include "types.h"

#define W           800
#define H           600
#define FRAME_TICKS 3                 /* ~33 fps */

/* Colors (console palette 0-15) */
#define C_BLACK  0
#define C_WHITE  15
#define C_CYAN   11
#define C_GREEN  10
#define C_YELLOW 14
#define C_RED    12
#define C_GREY   7

/* Scancodes */
#define SC_ESC   0x01
#define SC_Q     0x10
#define SC_A     0x1E
#define SC_D     0x20
#define SC_SPACE 0x39
#define EXT_LEFT  0x4B
#define EXT_RIGHT 0x4D

/* ---- Fleet / layout ---------------------------------------------------- */
#define ROWS   5
#define COLS   11
#define SCALE  3
#define INV_W  11
#define INV_H  8
#define INV_PW (INV_W * SCALE)
#define INV_PH (INV_H * SCALE)
#define CELL_W 48
#define CELL_H 40
#define STEP_X 14
#define STEP_Y 26

#define MAX_BOMBS 5
#define NBUNK 4
#define BUNK_COLS 8
#define BUNK_ROWS 5
#define BLOCK 8

static uint8_t back[W * H];
static uint8_t font[4096];            /* filled via sys_getfont at startup */

static uint32_t rng = 0x51a9c3e7;
static uint32_t rnd(void) { rng = rng * 1103515245u + 12345u; return (rng >> 16) & 0x7FFF; }

/* ---- Sprites (11 wide x 8 tall, 2 animation frames, 3 types) ----------- */
static const uint16_t inv_sprite[3][2][INV_H] = {
  { { 0x070,0x1FC,0x3FE,0x6DB,0x7FF,0x1DC,0x186,0x0D8 },
    { 0x070,0x1FC,0x3FE,0x6DB,0x7FF,0x124,0x2FA,0x603 } },
  { { 0x104,0x088,0x1FC,0x376,0x7FF,0x5FD,0x505,0x0D8 },
    { 0x104,0x489,0x5FD,0x777,0x7FF,0x3FE,0x104,0x202 } },
  { { 0x0F8,0x3FE,0x7FF,0x727,0x7FF,0x0D8,0x18C,0x306 },
    { 0x0F8,0x3FE,0x7FF,0x727,0x7FF,0x1FC,0x326,0x0D8 } },
};

#define PLW 13
static const uint16_t cannon[INV_H] = {
    0x0040, 0x00E0, 0x00E0, 0x0FFE, 0x1FFF, 0x1FFF, 0x1FFF, 0x1FFF,
};

/* ---- Drawing into the back buffer -------------------------------------- */
static void px(int x, int y, uint8_t c) {
    if ((unsigned)x < (unsigned)W && (unsigned)y < (unsigned)H) back[y * W + x] = c;
}

static void gfill(int x, int y, int w, int h, uint8_t c) {
    for (int yy = y; yy < y + h; yy++)
        for (int xx = x; xx < x + w; xx++) px(xx, yy, c);
}

static void gchar(int x, int y, char ch, uint8_t c) {
    const uint8_t *g = font + (uint32_t)(uint8_t)ch * 16;
    for (int row = 0; row < 16; row++)
        for (int col = 0; col < 8; col++)
            if (g[row] & (0x80 >> col)) px(x + col, y + row, c);
}

static void gtext(int x, int y, const char *s, uint8_t c) {
    while (*s) { gchar(x, y, *s++, c); x += 8; }
}

static void gnum(int x, int y, int n, uint8_t c) {
    char buf[12]; int i = 0;
    if (n == 0) buf[i++] = '0';
    while (n > 0) { buf[i++] = (char)('0' + n % 10); n /= 10; }
    while (i > 0) { gchar(x, y, buf[--i], c); x += 8; }
}

static void gsprite(int x, int y, const uint16_t *rows, int nrows, int ncols,
                    int scale, uint8_t c) {
    for (int r = 0; r < nrows; r++) {
        uint16_t bits = rows[r];
        for (int col = 0; col < ncols; col++)
            if (bits & (1 << (ncols - 1 - col)))
                gfill(x + col * scale, y + r * scale, scale, scale, c);
    }
}

/* ---- Game state -------------------------------------------------------- */
static uint8_t alive[ROWS][COLS];
static int fleet_x, fleet_y, fleet_dir, anim, step_timer;
static int player_x, player_invuln;
static int bx, by, bullet_active;
static struct { int x, y, active; } bombs[MAX_BOMBS];
static uint8_t bunk[NBUNK][BUNK_ROWS][BUNK_COLS];
static int score, lives, wave, bomb_timer;

static const uint8_t row_type[ROWS]    = { 0, 1, 1, 2, 2 };
static const int      row_points[ROWS] = { 30, 20, 20, 10, 10 };
static const uint8_t  row_color[ROWS]  = { C_CYAN, C_GREEN, C_GREEN, C_YELLOW, C_YELLOW };

#define PLAYER_Y  (H - 60)
#define BUNK_Y    (H - 170)

static int count_alive(void) {
    int n = 0;
    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++) n += alive[r][c];
    return n;
}

static int step_interval(void) {
    int n = count_alive();
    int iv = 2 + (n * 22) / (ROWS * COLS);
    return iv < 2 ? 2 : iv;
}

static void init_bunkers(void) {
    for (int b = 0; b < NBUNK; b++)
        for (int r = 0; r < BUNK_ROWS; r++)
            for (int c = 0; c < BUNK_COLS; c++) {
                int hole = (r >= BUNK_ROWS - 2 && c >= 3 && c <= 4);
                bunk[b][r][c] = hole ? 0 : 1;
            }
}

static int bunk_x(int b) {
    int span = BUNK_COLS * BLOCK;
    int gap = (W - NBUNK * span) / (NBUNK + 1);
    return gap + b * (span + gap);
}

static void init_wave(void) {
    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++) alive[r][c] = 1;
    fleet_x = 140;
    fleet_y = 70 + wave * 12;
    if (fleet_y > 150) fleet_y = 150;
    fleet_dir = 1;
    anim = 0;
    step_timer = step_interval();
    bullet_active = 0;
    for (int i = 0; i < MAX_BOMBS; i++) bombs[i].active = 0;
    bomb_timer = 40;
}

static void fleet_bounds(int *left, int *right, int *bottom) {
    int minc = COLS, maxc = -1, maxr = -1;
    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++)
            if (alive[r][c]) {
                if (c < minc) minc = c;
                if (c > maxc) maxc = c;
                if (r > maxr) maxr = r;
            }
    *left   = fleet_x + minc * CELL_W;
    *right  = fleet_x + maxc * CELL_W + INV_PW;
    *bottom = fleet_y + maxr * CELL_H + INV_PH;
}

static void spawn_bomb(void) {
    int c = rnd() % COLS;
    for (int tries = 0; tries < COLS; tries++) {
        int lr = -1;
        for (int r = ROWS - 1; r >= 0; r--) if (alive[r][c]) { lr = r; break; }
        if (lr >= 0) {
            for (int i = 0; i < MAX_BOMBS; i++) if (!bombs[i].active) {
                bombs[i].active = 1;
                bombs[i].x = fleet_x + c * CELL_W + INV_PW / 2;
                bombs[i].y = fleet_y + lr * CELL_H + INV_PH;
                return;
            }
            return;
        }
        c = (c + 1) % COLS;
    }
}

static int hit_bunker(int x, int y) {
    for (int b = 0; b < NBUNK; b++) {
        int bxp = bunk_x(b);
        if (x < bxp || x >= bxp + BUNK_COLS * BLOCK) continue;
        if (y < BUNK_Y || y >= BUNK_Y + BUNK_ROWS * BLOCK) continue;
        int cc = (x - bxp) / BLOCK;
        int rr = (y - BUNK_Y) / BLOCK;
        if (bunk[b][rr][cc]) { bunk[b][rr][cc] = 0; return 1; }
    }
    return 0;
}

static void reset_player(void) {
    player_x = W / 2 - (PLW * SCALE) / 2;
    player_invuln = 60;
}

/* ---- Render ------------------------------------------------------------ */
static void render(int frame) {
    memset(back, C_BLACK, sizeof(back));

    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++)
            if (alive[r][c])
                gsprite(fleet_x + c * CELL_W, fleet_y + r * CELL_H,
                        inv_sprite[row_type[r]][anim], INV_H, INV_W, SCALE,
                        row_color[r]);

    for (int b = 0; b < NBUNK; b++) {
        int bxp = bunk_x(b);
        for (int r = 0; r < BUNK_ROWS; r++)
            for (int c = 0; c < BUNK_COLS; c++)
                if (bunk[b][r][c])
                    gfill(bxp + c * BLOCK, BUNK_Y + r * BLOCK, BLOCK, BLOCK, C_GREEN);
    }

    if (!(player_invuln > 0 && (frame & 4)))
        gsprite(player_x, PLAYER_Y, cannon, INV_H, PLW, SCALE, C_WHITE);

    if (bullet_active) gfill(bx, by, 3, 14, C_WHITE);
    for (int i = 0; i < MAX_BOMBS; i++)
        if (bombs[i].active) gfill(bombs[i].x - 1, bombs[i].y, 3, 10, C_RED);

    gfill(0, PLAYER_Y + INV_PH + 6, W, 2, C_GREEN);
    gtext(10, 8, "SCORE", C_WHITE);   gnum(58, 8, score, C_WHITE);
    gtext(W / 2 - 28, 8, "WAVE", C_WHITE); gnum(W / 2 + 12, 8, wave, C_WHITE);
    gtext(W - 110, 8, "LIVES", C_WHITE); gnum(W - 38, 8, lives, C_WHITE);

    sys_blit(back);
}

/* ---- Main -------------------------------------------------------------- */
int main(void) {
    sys_getfont(font);
    rng ^= sys_ticks() * 2654435761u + 1;
    score = 0; lives = 3; wave = 1;
    init_bunkers();
    init_wave();
    reset_player();
    player_invuln = 0;

    int fire_cd = 0, frame = 0, running = 1, gameover = 0;
    uint32_t last = sys_ticks();

    while (running) {
        while (sys_ticks() - last < FRAME_TICKS) sys_yield();
        last += FRAME_TICKS;
        frame++;

        if (sys_keydown(SC_Q) || sys_keydown(SC_ESC)) break;
        int pmax = W - 20 - PLW * SCALE;
        if (sys_keydown(SC_A) || sys_keydown_ext(EXT_LEFT))  player_x -= 7;
        if (sys_keydown(SC_D) || sys_keydown_ext(EXT_RIGHT)) player_x += 7;
        if (player_x < 20) player_x = 20;
        if (player_x > pmax) player_x = pmax;
        if (fire_cd > 0) fire_cd--;
        if (sys_keydown(SC_SPACE) && !bullet_active && fire_cd == 0) {
            bullet_active = 1;
            bx = player_x + (PLW * SCALE) / 2 - 1;
            by = PLAYER_Y - 14;
            fire_cd = 6;
        }
        if (player_invuln > 0) player_invuln--;

        if (--step_timer <= 0) {
            step_timer = step_interval();
            anim ^= 1;
            int left, right, bottom;
            fleet_bounds(&left, &right, &bottom);
            if (fleet_dir > 0 && right + STEP_X > W - 20) { fleet_y += STEP_Y; fleet_dir = -1; }
            else if (fleet_dir < 0 && left - STEP_X < 20) { fleet_y += STEP_Y; fleet_dir = 1; }
            else { fleet_x += fleet_dir * STEP_X; }
            fleet_bounds(&left, &right, &bottom);
            if (bottom >= PLAYER_Y) { running = 0; gameover = 1; }
        }

        if (--bomb_timer <= 0) { bomb_timer = 25 + (rnd() % 30); spawn_bomb(); }
        for (int i = 0; i < MAX_BOMBS; i++) {
            if (!bombs[i].active) continue;
            bombs[i].y += 6;
            if (bombs[i].y > H) { bombs[i].active = 0; continue; }
            if (hit_bunker(bombs[i].x, bombs[i].y)) { bombs[i].active = 0; continue; }
            if (player_invuln == 0 &&
                bombs[i].x >= player_x && bombs[i].x < player_x + PLW * SCALE &&
                bombs[i].y >= PLAYER_Y && bombs[i].y < PLAYER_Y + INV_PH) {
                bombs[i].active = 0;
                if (--lives <= 0) { running = 0; gameover = 1; }
                else reset_player();
            }
        }

        if (bullet_active) {
            by -= 12;
            if (by < 0) bullet_active = 0;
            else if (hit_bunker(bx, by)) bullet_active = 0;
            else {
                for (int r = 0; r < ROWS && bullet_active; r++)
                    for (int c = 0; c < COLS; c++) {
                        if (!alive[r][c]) continue;
                        int ix = fleet_x + c * CELL_W, iy = fleet_y + r * CELL_H;
                        if (bx + 3 > ix && bx < ix + INV_PW &&
                            by < iy + INV_PH && by + 14 > iy) {
                            alive[r][c] = 0;
                            score += row_points[r];
                            bullet_active = 0;
                            break;
                        }
                    }
            }
        }

        if (count_alive() == 0) { wave++; init_wave(); reset_player(); }

        render(frame);
    }

    if (gameover) {
        memset(back, C_BLACK, sizeof(back));
        gtext(W / 2 - 36, H / 2 - 40, "GAME OVER", C_RED);
        gtext(W / 2 - 60, H / 2, "SCORE", C_WHITE);
        gnum(W / 2 - 60 + 48, H / 2, score, C_WHITE);
        gtext(W / 2 - 84, H / 2 + 40, "PRESS A KEY TO RETURN", C_GREY);
        sys_blit(back);
        sys_kbflush();
        sys_getchar();
    }

    sys_kbflush();
    sys_clear();
    return score;
}
