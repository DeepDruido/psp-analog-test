/*
 * PSP Analog Test - diagnostica dello stick analogico e dei tasti.
 *
 * Colonna sinistra: valori grezzi, min/max, centro a riposo, oscillazione,
 * stato dei tasti. Destra: traccia grafica di tutte le posizioni raggiunte.
 *
 * HOME = esci, SELECT = azzera (da premere senza toccare lo stick).
 *
 * Copyright (c) 2026 Edoardo Puccilli
 * SPDX-License-Identifier: MIT
 */
#include <pspkernel.h>
#include <pspdebug.h>
#include <pspctrl.h>
#include <pspdisplay.h>
#include <pspge.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

PSP_MODULE_INFO("AnalogTest", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);

/* Il framebuffer della debug screen e' in formato 8888 ABGR */
#define RGB(r, g, b) (0xFF000000u | ((u32)(b) << 16) | ((u32)(g) << 8) | (u32)(r))

#define COL_WHITE  RGB(255, 255, 255)
#define COL_GRAY   RGB(130, 130, 130)
#define COL_CYAN   RGB(80, 220, 255)
#define COL_GREEN  RGB(60, 230, 90)
#define COL_YELLOW RGB(255, 220, 60)
#define COL_RED    RGB(255, 70, 70)
#define COL_BOXBG  RGB(20, 20, 26)
#define COL_AXIS   RGB(60, 60, 70)
#define COL_ZONE   RGB(140, 115, 30)

#define BUF_W      512   /* larghezza riga del framebuffer in pixel */
#define BOX        200   /* lato del riquadro grafico in pixel */
#define BOX_X      272
#define BOX_Y      40
#define TEXT_COLS  33    /* colonne di testo a sinistra del riquadro */
#define RIGHT_COL  34
#define CENTER_RAW 128
#define ZONE       16    /* tolleranza del centro (valori grezzi) */
#define HIST       60    /* campioni per l'oscillazione (~1 s a 60 Hz) */

static u32 *vram;
static unsigned char cov[BOX][BOX];
static unsigned char hist_x[HIST], hist_y[HIST];
static int hist_pos, hist_count;

static int rest_x, rest_y;
static int min_x, max_x, min_y, max_y;
static unsigned int ever_pressed;

/* ---------- callback di uscita (tasto HOME) ---------- */

static int exit_callback(int arg1, int arg2, void *common)
{
    sceKernelExitGame();
    return 0;
}

static int callback_thread(SceSize args, void *argp)
{
    int cbid = sceKernelCreateCallback("Exit Callback", exit_callback, NULL);
    sceKernelRegisterExitCallback(cbid);
    sceKernelSleepThreadCB();
    return 0;
}

static void setup_callbacks(void)
{
    int thid = sceKernelCreateThread("update_thread", callback_thread, 0x11, 0xFA0, 0, 0);
    if (thid >= 0)
        sceKernelStartThread(thid, 0, 0);
}

/* ---------- disegno ---------- */

static inline void put_px(int x, int y, u32 c)
{
    if (x >= 0 && x < 480 && y >= 0 && y < 272)
        vram[y * BUF_W + x] = c;
}

static inline int map_raw(int v)
{
    return v * (BOX - 1) / 255;
}

/* Stampa una riga a sinistra, riempita di spazi per cancellare il contenuto precedente */
static void line(int row, u32 color, const char *fmt, ...)
{
    char buf[64];
    va_list ap;
    int n;

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    n = strlen(buf);
    if (n > TEXT_COLS)
        n = TEXT_COLS;
    while (n < TEXT_COLS)
        buf[n++] = ' ';
    buf[TEXT_COLS] = '\0';

    pspDebugScreenSetXY(0, row);
    pspDebugScreenSetTextColor(color);
    pspDebugScreenPrintf("%s", buf);
}

static void text_at(int col, int row, u32 color, const char *s)
{
    pspDebugScreenSetXY(col, row);
    pspDebugScreenSetTextColor(color);
    pspDebugScreenPrintf("%s", s);
}

static void draw_border(void)
{
    int i;
    for (i = -1; i <= BOX; i++) {
        put_px(BOX_X + i, BOX_Y - 1, COL_GRAY);
        put_px(BOX_X + i, BOX_Y + BOX, COL_GRAY);
        put_px(BOX_X - 1, BOX_Y + i, COL_GRAY);
        put_px(BOX_X + BOX, BOX_Y + i, COL_GRAY);
    }
}

static void draw_box(int lx, int ly)
{
    int c = map_raw(CENTER_RAW);
    int z0 = map_raw(CENTER_RAW - ZONE), z1 = map_raw(CENTER_RAW + ZONE);
    int x, y, dx, dy;

    for (y = 0; y < BOX; y++) {
        u32 *row = vram + (BOX_Y + y) * BUF_W + BOX_X;
        for (x = 0; x < BOX; x++) {
            u32 col;
            if (cov[y][x])
                col = COL_GREEN;
            else if (((x == z0 || x == z1) && y >= z0 && y <= z1) ||
                     ((y == z0 || y == z1) && x >= z0 && x <= z1))
                col = COL_ZONE;
            else if (x == c || y == c)
                col = COL_AXIS;
            else
                col = COL_BOXBG;
            row[x] = col;
        }
    }

    /* posizione attuale: quadratino rosso 5x5 */
    for (dy = -2; dy <= 2; dy++)
        for (dx = -2; dx <= 2; dx++) {
            int px = map_raw(lx) + dx, py = map_raw(ly) + dy;
            if (px >= 0 && px < BOX && py >= 0 && py < BOX)
                put_px(BOX_X + px, BOX_Y + py, COL_RED);
        }
}

/* ---------- tasti ---------- */

typedef struct {
    unsigned int mask;
    const char *name;
    int col, row;
} Btn;

static const Btn BUTTONS[] = {
    { PSP_CTRL_UP,       "SU",       0,  17 },
    { PSP_CTRL_DOWN,     "GIU",      3,  17 },
    { PSP_CTRL_LEFT,     "SX",       7,  17 },
    { PSP_CTRL_RIGHT,    "DX",       10, 17 },
    { PSP_CTRL_TRIANGLE, "TRIANG",   0,  18 },
    { PSP_CTRL_CIRCLE,   "CERCHIO",  7,  18 },
    { PSP_CTRL_CROSS,    "X",        15, 18 },
    { PSP_CTRL_SQUARE,   "QUADRATO", 17, 18 },
    { PSP_CTRL_LTRIGGER, "L",        0,  19 },
    { PSP_CTRL_RTRIGGER, "R",        2,  19 },
    { PSP_CTRL_START,    "START",    4,  19 },
    { PSP_CTRL_SELECT,   "SELECT",   10, 19 },
};

static void draw_buttons(unsigned int now)
{
    unsigned int i;
    line(17, COL_GRAY, "");
    line(18, COL_GRAY, "");
    line(19, COL_GRAY, "");
    for (i = 0; i < sizeof(BUTTONS) / sizeof(BUTTONS[0]); i++) {
        const Btn *b = &BUTTONS[i];
        u32 col = (now & b->mask) ? COL_GREEN
                : (ever_pressed & b->mask) ? COL_WHITE
                : COL_GRAY;
        text_at(b->col, b->row, col, b->name);
    }
}

/* ---------- logica ---------- */

static void reset_stats(int lx, int ly)
{
    rest_x = lx;
    rest_y = ly;
    min_x = max_x = lx;
    min_y = max_y = ly;
    memset(cov, 0, sizeof(cov));
    hist_pos = hist_count = 0;
}

static int iabs(int v) { return v < 0 ? -v : v; }

static void hist_range(const unsigned char *h, int *out)
{
    int i, lo = 255, hi = 0;
    for (i = 0; i < hist_count; i++) {
        if (h[i] < lo) lo = h[i];
        if (h[i] > hi) hi = h[i];
    }
    *out = hist_count ? hi - lo : 0;
}

static void range_line(int row, const char *axis, int lo, int hi)
{
    if (lo <= 25 && hi >= 230)
        line(row, COL_GREEN, "Escursione %s: OK", axis);
    else
        line(row, COL_YELLOW, "Escursione %s: INCOMPLETA", axis);
}

int main(void)
{
    SceCtrlData pad;
    unsigned int old_buttons = 0;
    int i;

    setup_callbacks();
    pspDebugScreenInit();
    pspDebugScreenClear();
    vram = (u32 *)(0x40000000 | (u32)sceGeEdramGetAddr());

    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);

    /* lascia stabilizzare la lettura prima di registrare il centro */
    for (i = 0; i < 20; i++) {
        sceCtrlPeekBufferPositive(&pad, 1);
        sceDisplayWaitVblankStart();
    }
    reset_stats(pad.Lx, pad.Ly);
    old_buttons = pad.Buttons;

    /* testi fissi */
    text_at(RIGHT_COL, 2, COL_CYAN, "TRACCIA STICK");
    text_at(RIGHT_COL, 3, COL_GRAY, "(alto = LY 0)");
    text_at(RIGHT_COL, 31, COL_GREEN, "verde");
    text_at(RIGHT_COL + 5, 31, COL_GRAY, "=raggiunto ");
    text_at(RIGHT_COL + 16, 31, COL_RED, "rosso");
    text_at(RIGHT_COL + 21, 31, COL_GRAY, "=ora");
    text_at(RIGHT_COL, 32, COL_ZONE, "giallo");
    text_at(RIGHT_COL + 6, 32, COL_GRAY, "=zona di riposo");
    draw_border();

    while (1) {
        int lx, ly, nx, ny, off, worst;

        sceCtrlPeekBufferPositive(&pad, 1);
        lx = pad.Lx;
        ly = pad.Ly;

        if ((pad.Buttons & PSP_CTRL_SELECT) && !(old_buttons & PSP_CTRL_SELECT))
            reset_stats(lx, ly);
        old_buttons = pad.Buttons;
        ever_pressed |= pad.Buttons;

        if (lx < min_x) min_x = lx;
        if (lx > max_x) max_x = lx;
        if (ly < min_y) min_y = ly;
        if (ly > max_y) max_y = ly;

        /* traccia: punto 2x2 */
        {
            int px = map_raw(lx), py = map_raw(ly);
            cov[py][px] = 1;
            if (px + 1 < BOX) cov[py][px + 1] = 1;
            if (py + 1 < BOX) cov[py + 1][px] = 1;
            if (px + 1 < BOX && py + 1 < BOX) cov[py + 1][px + 1] = 1;
        }

        hist_x[hist_pos] = lx;
        hist_y[hist_pos] = ly;
        hist_pos = (hist_pos + 1) % HIST;
        if (hist_count < HIST) hist_count++;
        hist_range(hist_x, &nx);
        hist_range(hist_y, &ny);

        sceDisplayWaitVblankStart();

        line(0, COL_CYAN, "PSP ANALOG TEST 1.0");
        line(1, COL_GRAY, "HOME=esci  SELECT=azzera");

        line(3, COL_WHITE, "LX: %3d   (%+4d dal centro)", lx, lx - CENTER_RAW);
        line(4, COL_WHITE, "LY: %3d   (%+4d dal centro)", ly, ly - CENTER_RAW);

        line(6, COL_WHITE, "Min/Max X: %3d - %3d", min_x, max_x);
        line(7, COL_WHITE, "Min/Max Y: %3d - %3d", min_y, max_y);
        range_line(8, "X", min_x, max_x);
        range_line(9, "Y", min_y, max_y);

        off = iabs(rest_x - CENTER_RAW);
        if (iabs(rest_y - CENTER_RAW) > off)
            off = iabs(rest_y - CENTER_RAW);
        line(11, COL_WHITE, "Riposo (a SELECT): %3d,%3d", rest_x, rest_y);
        if (off <= ZONE)
            line(12, COL_GREEN, "Centro: OK (scarto %d)", off);
        else if (off <= 2 * ZONE)
            line(12, COL_YELLOW, "Centro: LIEVE DRIFT (%d)", off);
        else
            line(12, COL_RED, "Centro: DRIFT! (scarto %d)", off);

        worst = nx > ny ? nx : ny;
        line(13, worst <= 4 ? COL_GREEN : worst <= 12 ? COL_YELLOW : COL_RED,
             "Oscillaz. 1s: X %3d  Y %3d", nx, ny);

        line(15, COL_GRAY, "Tasti: verde=premuto ora");
        line(16, COL_GRAY, "bianco=provato, grigio=mai");
        draw_buttons(pad.Buttons);

        if (max_x - min_x < 8 && max_y - min_y < 8)
            line(21, COL_YELLOW, "Nessun movimento rilevato");
        else
            line(21, COL_GRAY, "");

        line(23, COL_CYAN, "COME FARE IL TEST:");
        line(24, COL_WHITE, "1 Non toccare lo stick, premi");
        line(25, COL_WHITE, "  SELECT: centro OK e oscill.");
        line(26, COL_WHITE, "  bassa (verde).");
        line(27, COL_WHITE, "2 Ruota lo stick piano lungo");
        line(28, COL_WHITE, "  tutto il bordo: la traccia");
        line(29, COL_WHITE, "  verde deve essere piena.");
        line(30, COL_WHITE, "3 Puntini sparsi o buchi =");
        line(31, COL_WHITE, "  contatti sporchi/usurati.");

        draw_box(lx, ly);
    }

    return 0;
}
