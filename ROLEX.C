/*
 * ROLEX.C -- Reloj de pantalla para MS-DOS, al estilo del ROLEX.COM
 *            de RIX SoftWorks (1986).  Edicion FOTOREALISTA AUTONOMA.
 *
 * C para Open Watcom v2 (16 bits, modelo tiny, salida .COM).  Todo el
 * programa cabe en UN fichero, ROLEX.COM: la imagen del reloj (caja,
 * bisel, brazalete Y esfera fotograficos) viaja DENTRO del ejecutable,
 * comprimida con el RLE del PCX, junto con su paleta de 256 colores.
 *
 * La esfera es la FOTO REAL (esfera2.jpg): corona, ROLEX, OYSTER
 * PERPETUAL DATEJUST, minuteria CON sus numerales, bastones de acero,
 * ventana de fecha y textos de certificacion; nada se sintetiza con
 * fuentes ni con geometria.  prep_img.py la reduce, le da contraste y
 * la reindexa contra la paleta (sin dithering, indice exacto).
 *
 * Optimizacion (objetivo 8086, -0):
 *   - No se redibuja NADA si los tres angulos no cambian: el bucle
 *     solo trabaja cuando el contador de tics del BIOS avanza (18,2
 *     veces/s) y los angulos solo se mueven ~4,3 veces/s: esas son
 *     las unicas actualizaciones (antes: fotograma completo a 70 Hz).
 *   - Redibujado PARCIAL por cajas: cada aguja recuerda el rectangulo
 *     que toco; al actualizar se restaura el fondo solo en la caja
 *     vieja, se dibuja la aguja nueva y se vuela a VRAM solo la union
 *     de cajas vieja/nueva (el segundero suele tocar 2-4 kB, no 29 kB
 *     de ida y otro tanto de vuelta como el volcado completo).
 *   - Fuera multiplicaciones y divisiones de los caminos calientes:
 *     filas por desplazamientos (170 = 128+32+8+2, 320 = 256+64),
 *     relleno de poligonos DDA (una division por arista), disco con
 *     raiz cuadrada incremental y proporciones de aguja por sumas.
 *   - Bloques con memset/memcpy: fondo RLE por tiras, restauraciones
 *     y volcados por fila con _fmemcpy (rep movsw), esfera en UN
 *     bloque de 28.900 bytes.
 *
 * Al arrancar:
 *   - PALETA (768 bytes) -> DAC (puertos 3C8h/3C9h), valores /4.
 *   - FONDO_RLE -> franja central de la VRAM en A000:0000 (a los lados
 *     solo hay negro puro, que no viaja).
 *   - La esfera limpia (170x170) se recorta de la VRAM a un bloque de
 *     64 KB de RAM convencional (int 21h AH=48h), detras del marco de
 *     trabajo:
 *        bloque:00000  marco de trabajo  (fotograma compuesto, 28.900 B)
 *        bloque:28900  esfera limpia     (copia del fondo,    28.900 B)
 *   - La ventanita de la fecha se "cocina" UNA VEZ en la esfera limpia
 *     (interior blanco + dia del mes, sin cero inicial); a medianoche
 *     se vuelve a cocinar con el dia nuevo.
 *
 * ANTI-PARPADEO: el fotograma se compone entero en RAM y se vuela a
 * la VRAM; en pantalla nunca hay estado intermedio de una aguja.
 *
 * Barrido suave: tics del BIOS en 0040:006C -> segundos con fraccion
 * (tics * 2000 / 36413; 18.2065 tics/s, sin deriva).  Fecha del RTC
 * del CMOS (70h/71h) al arrancar y al cruzar medianoche (reserva int
 * 21h AH=2Ah).  ESC leyendo el buffer del BIOS en 0040:001A.
 * Aritmetica entera: tabla de senos Q8.  El angulo va en 1/256 de
 * vuelta (0 = las 12, horario).
 *
 * Compilar (Open Watcom v2, 16 bits, DOS):
 *      wcl -mt -0 -bt=dos -os -d0 ROLEX.C     -> ROLEX.COM
 *      wcl -mt -0 -bt=dos -os -d0 -l=dos ROLEX.C -> ROLEX.EXE
 *
 * Regenerar la imagen desde la foto (esfera2.jpg):
 *      python3 verificacion/prep_img.py
 *
 * Verificacion en un PC moderno: scripts/host_main.c incluye este
 * fichero con ROLEX_HOST definido, simula la VRAM, los puertos y la
 * int 21h, comprueba que el redibujado parcial da EXACTAMENTE el mismo
 * fotograma que la composicion completa y vuelca el resultado a PPM.
 * El codigo DOS no cambia.
 */

#ifdef ROLEX_HOST
/* ------------------------------------------------------------------
   Arnés de verificacion en anfitrion moderno (gcc): RAM simulada de
   1 MB + puertos falsos.  La imagen viaja incrustada (IMGRLE.H).
   ------------------------------------------------------------------ */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>

#define far
extern unsigned char emu[0x110000];
extern void *rolex_host_ptr;      /* ultimo puntero pasado a FP_OFF */
#define MK_FP(seg, off) ((void *)(emu + ((unsigned long)(seg) << 4) + (unsigned long)(off)))
#define _fmemcpy memcpy
#define _fmemset memset

union REGS {
    struct { unsigned char al, ah, bl, bh, cl, ch, dl, dh; } h;
    struct { unsigned short ax, bx, cx, dx, si, di, cflag, flags; } x;
    struct { unsigned long ax, bx, cx, dx, si, di, cflag, flags; } w;
};
struct SREGS {
    unsigned int es, cs, ss, ds;
};

#define FP_OFF(p) (rolex_host_ptr = (void *)(size_t)(p), \
                   (unsigned)(size_t)(p) & 0xFFFFu)
#define FP_SEG(p) (0)
#define segread(s) ((void)(s))

unsigned char inp(unsigned puerto);
void outp(unsigned puerto, unsigned char valor);
int int86(int num, union REGS *entrada, union REGS *salida);
int int86x(int num, union REGS *entrada, union REGS *salida,
           struct SREGS *segs);

#else
/* ------------------------------------------------------------------
   Programa DOS real (Open Watcom, 16 bits, modelo tiny)
   ------------------------------------------------------------------ */
#include <dos.h>     /* MK_FP, union REGS, SREGS, int86x, FP_OFF */
#include <conio.h>   /* inp, outp                         */
#include <string.h>  /* memcpy, memset, _fmemcpy, _fmemset */
#endif

/* ==================== imagen incrustada ==================== */

/* IMGRLE.H lo genera prep_img.py desde la FOTO (esfera2.jpg):
   geometria calibrada (DIAL_*, STRIP_*, VEN_* y agujas), la paleta
   PALETA[768] y el fondo FONDO_RLE[] (RLE estilo PCX). */
#include "IMGRLE.H"

/* ============================ constantes ============================ */

#define DCX (DIAL_W / 2)       /* centro del dial en coords locales */
#define DCY (DIAL_H / 2)
#define DIAL_TAM ((unsigned)DIAL_W * DIAL_H)   /* constante: 28900 */

/* offsets de fila SIN multiplicar (camino caliente de todos los
   bucles de dibujo):
     170 = 128 + 32 + 8 + 2      320 = 256 + 64
   El maximo es 169*170 = 28730 y 199*320 = 63680: caben en unsigned. */
#define OFF170(y) (((unsigned)(y) << 7) + ((unsigned)(y) << 5) \
                 + ((unsigned)(y) << 3) + ((unsigned)(y) << 1))
#define OFF320(y) (((unsigned)(y) << 8) + ((unsigned)(y) << 6))

/* tamano de la ventanita de la fecha, en pixeles */
#define VEN_ANCHO (VEN_X1 - VEN_X0 + 1)
#define VEN_ALTO  (VEN_Y1 - VEN_Y0 + 1)

/* buferes de trabajo en RAM convencional; los fija reservar() */
static unsigned char far *MARCO;   /* fotograma compuesto          */
static unsigned char far *ESFERA;  /* esfera limpia (base)         */

/* las primitivas de dibujo pintan aqui: MARCO (fotograma) o ESFERA
   (cuando se cocina la fecha, una vez por dia) */
static unsigned char far *lienzo;

/* indices fijos de la paleta (247..249 trae prep_img.py para los
   dorados de la foto, si los hay; 250..255 son para lo que dibuja
   este programa) */
#define C_OSC 250           /* acero oscuro (bordes de las agujas)   */
#define C_CLA 252           /* acero claro (cuerpo)                   */
#define C_HI  253           /* acero brillo (faceta central)          */
#define C_NEG 254           /* negro (digitos de la fecha)            */
#define C_BLA 255           /* blanco (interior de la ventana)        */

/* ============================ datos ============================ */

/* Tabla de seno Q8: SEN[k] = round(sin(k*2*pi/256)*256), k = 0..64;
   el resto por simetria (sen256). */
static const int SEN[65] = {
      0,    6,   13,   19,   25,   31,   38,   44,
     50,   56,   62,   68,   74,   80,   86,   92,
     98,  104,  109,  115,  121,  126,  132,  137,
    142,  147,  152,  157,  162,  167,  172,  177,
    181,  185,  190,  194,  198,  202,  206,  209,
    213,  216,  220,  223,  226,  229,  231,  234,
    237,  239,  241,  243,  245,  247,  248,  250,
    251,  252,  253,  254,  255,  255,  256,  256,  256
};

/* Digitos 7x7 para la fecha.  Bit 0x80 = pixel izquierdo. */
static const unsigned char FUENTE[10][7] = {
    { 0x7C, 0xC6, 0xCE, 0xD6, 0xE6, 0xC6, 0x7C },  /* 0 */
    { 0x18, 0x38, 0x18, 0x18, 0x18, 0x18, 0x7E },  /* 1 */
    { 0x7C, 0xC6, 0x06, 0x1C, 0x30, 0x60, 0xFE },  /* 2 */
    { 0x7C, 0xC6, 0x06, 0x3C, 0x06, 0xC6, 0x7C },  /* 3 */
    { 0x1C, 0x3C, 0x6C, 0xCC, 0xFE, 0x0C, 0x1E },  /* 4 */
    { 0xFC, 0xC0, 0xFC, 0x06, 0x06, 0xC6, 0x7C },  /* 5 */
    { 0x3C, 0x60, 0xC0, 0xFC, 0xC6, 0xC6, 0x7C },  /* 6 */
    { 0xFE, 0xC6, 0x0C, 0x18, 0x30, 0x30, 0x30 },  /* 7 */
    { 0x7C, 0xC6, 0xC6, 0x7C, 0xC6, 0xC6, 0x7C },  /* 8 */
    { 0x7C, 0xC6, 0xC6, 0x7E, 0x06, 0x0C, 0x78 }   /* 9 */
};

/* tablas de cruces del relleno de poligonos: se reponen POR BLOQUE
   con memset (0x7F repetido da un entero muy positivo y 0x80 uno muy
   negativo: centinelas validos tanto con int de 16 como de 32 bits) */
static int cruce_min[DIAL_H];
static int cruce_max[DIAL_H];

/* caja del elemento que se esta dibujando: poligono, disco y linea la
   engordan; dibuja_agujas() la guarda por aguja (redibujado parcial) */
static int ax0, ay0, ax1, ay1;

static void marca_reset(void)
{
    ax0 = DIAL_W; ay0 = DIAL_H; ax1 = -1; ay1 = -1;
}

static void marca(int x0, int y0, int x1, int y1)
{
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > DIAL_W - 1) x1 = DIAL_W - 1;
    if (y1 > DIAL_H - 1) y1 = DIAL_H - 1;
    if (x0 < ax0) ax0 = x0;
    if (y0 < ay0) ay0 = y0;
    if (x1 > ax1) ax1 = x1;
    if (y1 > ay1) ay1 = y1;
}

/* ==================== primitivas de dibujo ==================== */

static void pixel(int x, int y, unsigned char c)
{
    if ((unsigned)x < DIAL_W && (unsigned)y < DIAL_H)
        lienzo[OFF170(y) + (unsigned)x] = c;
}

static void linea_h(int x1, int x2, int y, unsigned char c)
{
    int t;
    if (y < 0 || y >= DIAL_H) return;
    if (x1 > x2) { t = x1; x1 = x2; x2 = t; }
    if (x1 < 0) x1 = 0;
    if (x2 >= DIAL_W) x2 = DIAL_W - 1;
    if (x1 > x2) return;
    /* bloque horizontal: memset en vez de bucle de pixeles */
    _fmemset(lienzo + OFF170(y) + (unsigned)x1, c,
             (unsigned)(x2 - x1 + 1));
}

/* poligono convexo relleno por barrido de scanlines, recortado al
   dial.  DDA: UNA division por arista al empezar y despues puro
   incremento por fila (nada de multiplicar/dividir dentro del
   bucle).  Los cruces de cada fila se apuntan en las tablas, que se
   reponen con memset y se pintan al final con linea_h (memset). */
static void poligono(int n, const int *xs, const int *ys, unsigned char c)
{
    int i, y, ymin = DIAL_H - 1, ymax = 0;

    for (i = 0; i < n; i++) {
        if (ys[i] < ymin) ymin = ys[i];
        if (ys[i] > ymax) ymax = ys[i];
        marca(xs[i], ys[i], xs[i], ys[i]);   /* caja del elemento */
    }
    if (ymin < 0) ymin = 0;
    if (ymax > DIAL_H - 1) ymax = DIAL_H - 1;
    if (ymin > ymax) return;

    /* tablas a "vacio" por bloque */
    memset((char *)(cruce_min + ymin), 0x7F,
           (unsigned)(ymax - ymin + 1) * sizeof(int));
    memset((char *)(cruce_max + ymin), 0x80,
           (unsigned)(ymax - ymin + 1) * sizeof(int));

    for (i = 0; i < n; i++) {
        int j = (i + 1 == n) ? 0 : i + 1;
        int ya = ys[i], yb = ys[j];
        long acc, paso;               /* DDA en Q8 */
        int y0, yfin;

        if (ya == yb) continue;
        paso = ((long)(xs[j] - xs[i]) << 8) / (yb - ya);
        if (ya < yb) { acc = (long)xs[i] << 8; y0 = ya; yfin = yb; }
        else         { acc = (long)xs[j] << 8; y0 = yb; yfin = ya; }
        if (y0 < ymin) { acc += paso * (long)(ymin - y0); y0 = ymin; }
        if (yfin > ymax + 1) yfin = ymax + 1;
        for (y = y0; y < yfin; y++) {
            int x = (int)(acc >> 8);
            if (x < cruce_min[y]) cruce_min[y] = x;
            if (x > cruce_max[y]) cruce_max[y] = x;
            acc += paso;
        }
    }
    for (y = ymin; y <= ymax; y++)
        if (cruce_max[y] >= cruce_min[y])
            linea_h(cruce_min[y], cruce_max[y], y, c);
}

/* disco solido (contrapeso del segundero y remache central): radio
   incremental, raiz cuadrada incremental y cada fila de UNA vez con
   linea_h (memset), sin bucles de pixeles.  Radio pequeno: el
   producto r*r directo es mas barato que cualquier truco. */
static void disco(int cx, int cy, int r, unsigned char c)
{
    int dy, dy2 = 0;        /* dy*dy, incremental */
    int r2 = r * r;
    int h = r, h2 = r2;     /* semiancho de fila y su cuadrado */

    marca(cx - r, cy - r, cx + r, cy + r);
    for (dy = 0; dy <= r; dy++) {
        int limite = r2 - dy2;      /* queda r*r - dy*dy */
        while (h2 > limite) {       /* baja h hasta h*h <= limite */
            h2 -= (h << 1) - 1;
            h--;
        }
        linea_h(cx - h, cx + h, cy + dy, c);
        if (dy != 0)
            linea_h(cx - h, cx + h, cy - dy, c);
        dy2 += (dy << 1) + 1;
    }
}

/* seno/coseno de "ang" en 1/256 de vuelta (0 = las 12, horario) */
static int sen256(unsigned a)
{
    unsigned q = (a >> 6) & 3u;
    unsigned i = a & 63u;
    if (q == 0u) return  SEN[i];
    if (q == 1u) return  SEN[64u - i];
    if (q == 2u) return -SEN[i];
    return -SEN[64u - i];
}

/* punto del dial: rr a lo largo de la aguja (negativo = cola),
   k perpendicular.  La Y de la pantalla crece hacia abajo, de ahi el
   signo menos del coseno: sin el, las agujas girarian al reves. */
static void punto(unsigned ang, int rr, int k, int *x, int *y)
{
    int s = sen256(ang), c = sen256((unsigned)(64u - ang));
    *x = DCX + (int)(((long)s * rr + (long)c * k) >> 8);
    *y = DCY + (int)((-(long)c * rr + (long)s * k) >> 8);
}

/* linea de Bresenham (eje del segundero) */
static void linea(int x0, int y0, int x1, int y1, unsigned char c)
{
    int dx = x1 - x0, dy = y1 - y0;
    int sx, sy, err, e2;
    marca(x0, y0, x1, y1);          /* antes de mutar x0/y0 */
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;
    sx = (x0 < x1) ? 1 : -1;
    sy = (y0 < y1) ? 1 : -1;
    err = dx - dy;
    for (;;) {
        pixel(x0, y0, c);
        if (x0 == x1 && y0 == y1) break;
        e2 = err * 2;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 <  dx) { err += dx; y0 += sy; }
    }
}

/* proporciones de la aguja con sumas de desplazamientos, exactas para
   hw de 1 a 5 (los valores que usa baston2):
     hwt   = 0.60 * hw  (77/128)     hwtip = 0.55 * hw  (141/256) */
#define HWT(hw)   ((((hw) << 6) + ((hw) << 3) + ((hw) << 2) + (hw)) >> 7)
#define HWTIP(hw) ((((hw) << 7) + ((hw) << 3) + ((hw) << 2) + (hw)) >> 8)

/* baston: aguja hexagonal (punta plana, flancos anchos junto al
   centro, cola estrecha), como las de oro blanco del Datejust */
static void baston(unsigned ang, int r1, int cola, int hw, unsigned char c)
{
    int rrm = r1 >> 2;               /* donde la aguja es mas ancha */
    int hwt = HWT(hw);               /* semiancho de la cola        */
    int hwtip = HWTIP(hw);           /* semiancho en la punta       */
    int x[6], y[6];
    punto(ang, r1, -hwtip, &x[0], &y[0]);
    punto(ang, r1,  hwtip, &x[1], &y[1]);
    punto(ang, rrm,  hw,   &x[2], &y[2]);
    punto(ang, -cola,  hwt, &x[3], &y[3]);
    punto(ang, -cola, -hwt, &x[4], &y[4]);
    punto(ang, rrm, -hw,   &x[5], &y[5]);
    poligono(6, x, y, c);
}

/* baston con cuerpo: contorno oscuro + faceta clara, dos hexagonos */
static void baston2(unsigned ang, int r1, int cola, int hw)
{
    baston(ang, r1, cola, hw, C_OSC);
    if (hw >= 2) baston(ang, r1, cola, hw - 1, C_HI);
    if (hw >= 3) baston(ang, r1, cola, hw - 2, C_CLA);
}

/* segundero: barra fina con eje claro y contrapeso circular */
static void segundero(unsigned ang)
{
    int s, c;
    int x[4], y[4], xa, ya, xb, yb;
    punto(ang,  R_AG_S,  1, &x[0], &y[0]);
    punto(ang,  R_AG_S, -1, &x[1], &y[1]);
    punto(ang, -COL_S, -1, &x[2], &y[2]);
    punto(ang, -COL_S,  1, &x[3], &y[3]);
    poligono(4, x, y, C_OSC);
    punto(ang, -COL_S, 0, &xa, &ya);
    punto(ang,  R_AG_S, 0, &xb, &yb);
    linea(xa, ya, xb, yb, C_CLA);
    s = sen256(ang);
    c = sen256((unsigned)(64u - ang));
    disco(DCX - (int)(((long)s * PESO_S) >> 8),
          DCY + (int)(((long)c * PESO_S) >> 8), PESO_RO, C_OSC);
    disco(DCX - (int)(((long)s * PESO_S) >> 8),
          DCY + (int)(((long)c * PESO_S) >> 8), PESO_RI, C_CLA);
}

/* remache central sobre el eje de las agujas */
static void centro(void)
{
    disco(DCX, DCY, CEN_RO, C_OSC);
    disco(DCX, DCY, CEN_RI, C_CLA);
    disco(DCX, DCY, CEN_PN, C_OSC);
}

/* ==================== fecha cocinada en la esfera ==================== */

/* un digito 7x7 dentro de la ventanita */
static void digito(int x, int y, char ch)
{
    const unsigned char *g = FUENTE[ch - '0'];
    int f, b;
    for (f = 0; f < 7; f++) {
        unsigned char bits = g[f];
        for (b = 0; b < 7; b++)
            if (bits & (unsigned char)(0x80 >> b))
                pixel(x + b, y + f, C_NEG);
    }
}

/* dia del mes en la ventanita de las 3 (sin cero inicial: el dia 8 se
   escribe "8", no "08"), centrado en la abertura de IMGRLE.H.  Se
   cocina UNA VEZ sobre la esfera limpia: el fotograma de cada frame
   la copia y asi no se repinta a cada vuelco.  El interior se borra
   por filas de UNA vez (memset por bloque). */
static void pinta_fecha(int dia)
{
    int ancho = VEN_ANCHO, alto = VEN_ALTO;
    int yd = VEN_Y0 + ((VEN_ALTO - 7) >> 1);
    unsigned off = OFF170(VEN_Y0) + (unsigned)VEN_X0;
    int y;

    /* guarda contra cabeceras editadas a mano: nunca salirse de la
       esfera (el memset cruzaria filas y pisaria el marco) */
    if (VEN_X0 < 0 || VEN_Y0 < 0 ||
        VEN_X0 + ancho > DIAL_W || VEN_Y0 + alto > DIAL_H) return;

    lienzo = ESFERA;
    for (y = 0; y < alto; y++) {
        _fmemset(lienzo + off, C_BLA, (unsigned)ancho);
        off += DIAL_W;
    }
    if (dia < 10) {                     /* un digito, centrado */
        digito(VEN_X0 + ((ancho - 7) >> 1), yd, (char)('0' + dia));
    } else {
        int x0 = VEN_X0 + ((ancho - 13) >> 1);
        digito(x0,     yd, (char)('0' + (dia / 10) % 10));
        digito(x0 + 6, yd, (char)('0' + dia % 10));
    }
}

/* ============================ hardware ============================ */

static void modo_video(unsigned char modo)
{
    union REGS r;
    r.h.ah = 0x00;
    r.h.al = modo;
    int86(0x10, &r, &r);
}

/* contador de tics del BIOS (32 bits) en 0040:006C, con proteccion
   contra el desbordamiento de medianoche */
static unsigned long lee_ticks(void)
{
    unsigned short far *t = (unsigned short far *)MK_FP(0x40, 0x6C);
    unsigned short h1, h2, l;
    do {
        h1 = t[1];
        l  = t[0];
        h2 = t[1];
    } while (h1 != h2);
    return ((unsigned long)h1 << 16) | (unsigned long)l;
}

/* una tecla del buffer del BIOS (0040:001A..003D), sin int 16h:
   avanza la cabeza del buffer a mano */
static int tecla(unsigned char *ascii)
{
    unsigned short far *cab = (unsigned short far *)MK_FP(0x40, 0x1A);
    unsigned short far *cola = (unsigned short far *)MK_FP(0x40, 0x1C);
    unsigned short w;
    if (*cab == *cola) return 0;
    w = *(unsigned short far *)MK_FP(0x40, *cab);
    *cab += 2;
    if (*cab == 0x3E) *cab = 0x1E;
    *ascii = (unsigned char)(w & 0xFF);
    return 1;
}

/* espera al principio del retrazo vertical (puerto 3DAh, bit 3) */
static void espera_retrazo(void)
{
    while ((inp(0x3DA) & 0x08) != 0) ;
    while ((inp(0x3DA) & 0x08) == 0) ;
}

static unsigned char cmos_lee(unsigned char reg)
{
    outp(0x70, reg);
    return inp(0x71);
}

static int bcd(int v)
{
    int alto = v >> 4;
    /* 10*alto con desplazamientos, sin multiplicar */
    return (v & 0x0F) + (alto << 3) + (alto << 1);
}

/* dia del mes desde el RTC del CMOS (70h/71h); mes y anio solo para
   validar; si el RTC delira, reserva con int 21h AH=2Ah */
static void lee_fecha(int *dia)
{
    unsigned char b;
    int d, m, y, c;
    long guardia = 0;                 /* long: con int de 16 bits no llegaria */
    while ((cmos_lee(0x0A) & 0x80) != 0) {   /* espera fin de actualizacion */
        if (++guardia > 100000L) break;
    }
    d = cmos_lee(0x07);
    m = cmos_lee(0x08);
    y = cmos_lee(0x09);
    c = cmos_lee(0x32);
    b = cmos_lee(0x0B);
    if ((b & 0x04) == 0) {       /* BCD -> binario */
        d = bcd(d); m = bcd(m); y = bcd(y); c = bcd(c);
    }
    if (c < 19 || c > 21) c = 20;      /* siglo plausible */
    y += c * 100;
    if (d < 1 || d > 31 || m < 1 || m > 12 || y < 1980 || y > 2099) {
        union REGS r;
        r.w.ax = 0x2A00;
        int86(0x21, &r, &r);
        *dia = r.h.dl;
        return;
    }
    *dia = d;
}

/* ==================== memoria de trabajo ==================== */

/* Pide al DOS un bloque de 64 KB (0x1000 parrafos) para el marco y la
   esfera limpia (57.800 bytes).  Dos caminos, sin mas:

     1. int 21h AH=48h: un bloque libre, si lo hay (el crt0 de Watcom
        suele haber reducido ya la reserva del COM).
     2. Si no lo hay: la reserva del COM cubre TODA la memoria libre;
        se recorta a 64 KB (AH=4Ah, sobra: el COM mide menos de 64 KB)
        y se vuelve a pedir un bloque (AH=48h) con lo liberado.

   Devuelve 1 si consigue los dos buferes, 0 si no hay memoria. */
static int reservar(void)
{
    union REGS r;
    struct SREGS s;

    r.x.ax = 0x4800;
    r.x.bx = 0x1000;
    int86(0x21, &r, &r);
    if (r.x.cflag != 0) {
        r.h.ah = 0x62;                     /* PSP: segmento del COM */
        int86(0x21, &r, &r);
        segread(&s);
        s.es = r.x.bx;
        r.x.ax = 0x4A00;                   /* recortar a 64 KB */
        r.x.bx = 0x1000;
        int86x(0x21, &r, &r, &s);
        if (r.x.cflag != 0) return 0;
        r.x.ax = 0x4800;                   /* reintentar */
        r.x.bx = 0x1000;
        int86(0x21, &r, &r);
        if (r.x.cflag != 0) return 0;
    }
    MARCO  = (unsigned char far *)MK_FP(r.x.ax, 0);
    ESFERA = (unsigned char far *)MK_FP(r.x.ax, DIAL_TAM);
    return 1;
}

/* ==================== montaje de la imagen ==================== */

/* Descompresion RLE estilo PCX directamente a la VRAM visible: una
   sola tira que se escribe de izquierda a derecha por la franja
   central (STRIP_W columnas desde STRIP_X).  Un byte >= C0h es un par
   contador/valor (1..63 pixeles del byte siguiente); un byte < C0h es
   un pixel suelto.  Cada tira de la corrida se escribe con UN memset
   (bloque) y la fila avanza con un puntero, sin multiplicar. */
static void decodificar_fondo(void)
{
    const unsigned char *p   = FONDO_RLE;
    const unsigned char *fin = FONDO_RLE + FONDO_RLE_LEN;
    unsigned char far *fila = (unsigned char far *)MK_FP(0xA000, STRIP_X);
    unsigned x = 0;                       /* columna relativa 0..STRIP_W-1 */

    while (p < fin) {
        unsigned char v = *p++;
        unsigned n = 1;
        if ((v & 0xC0) == 0xC0) {
            n = (unsigned)(v & 0x3Fu);
            v = *p++;
        }
        while (n != 0u) {
            unsigned cabe = (unsigned)STRIP_W - x;  /* px hasta fin de fila */
            if (n < cabe) {
                _fmemset(fila + x, v, n);
                x += n;
                n = 0;
            } else {                     /* la corrida cruza la fila */
                _fmemset(fila + x, v, cabe);
                x = 0;
                fila += 320u;            /* fila siguiente */
                n -= cabe;
            }
        }
    }
}

/* Monta la escena una sola vez al arrancar: paleta al DAC, pantalla a
   negro, franja central descomprimida a su sitio y esfera limpia
   recortada al bloque de RAM (detras del marco de trabajo).  El
   offset de la VRAM avanza por suma (320 por fila), sin multiplicar. */
static void montar(void)
{
    unsigned off = OFF320(DIAL_Y) + (unsigned)DIAL_X;   /* 15*320+75 */
    int f, i;

    outp(0x3C8, 0);
    for (i = 0; i < 768; i++)
        outp(0x3C9, (unsigned char)(PALETA[i] >> 2));

    _fmemset((unsigned char far *)MK_FP(0xA000, 0), 0, 64000);
    decodificar_fondo();

    /* recorte de la esfera limpia: VRAM visible -> bloque de RAM */
    for (f = 0; f < DIAL_H; f++) {
        _fmemcpy(ESFERA + OFF170(f),
                 (unsigned char far *)MK_FP(0xA000, off), DIAL_W);
        off += 320u;
    }
}

/* ==================== redibujado parcial por cajas ==================== */

/* Cada aguja recuerda la caja (rectangulo) que toco.  Al actualizar:
   1) se restaura el fondo de la caja VIEJA de cada aguja cambiada
      (copiando de la esfera limpia), 2) se dibujan TODAS las agujas
      en orden (hora, minuto, segundero, remache) para respetar el
      solape, y 3) se vuela a VRAM solo la union de cajas vieja/nueva
      de las agujas que cambiaron.  El remache nunca necesita caja
      propia: siempre queda dentro de la del segundero. */
static int caja[3][4];        /* 0 = hora, 1 = minuto, 2 = segundero */
static int hay_cajas = 0;
static unsigned a_h0 = 0xFFFFu, a_m0 = 0xFFFFu, a_s0 = 0xFFFFu;

static void dibuja_agujas(unsigned a_h, unsigned a_m, unsigned a_s)
{
    marca_reset();
    baston2(a_h, R_AG_H, COL_H, HW_H);
    caja[0][0] = ax0; caja[0][1] = ay0; caja[0][2] = ax1; caja[0][3] = ay1;
    marca_reset();
    baston2(a_m, R_AG_M, COL_M, HW_M);
    caja[1][0] = ax0; caja[1][1] = ay0; caja[1][2] = ax1; caja[1][3] = ay1;
    marca_reset();
    segundero(a_s);
    caja[2][0] = ax0; caja[2][1] = ay0; caja[2][2] = ax1; caja[2][3] = ay1;
    centro();                    /* siempre dentro de la caja del segundero */
}

/* fondo limpio (esfera) sobre el marco, en una caja */
static void restaura(int x0, int y0, int x1, int y1)
{
    int y;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > DIAL_W - 1) x1 = DIAL_W - 1;
    if (y1 > DIAL_H - 1) y1 = DIAL_H - 1;
    if (x0 > x1 || y0 > y1) return;
    if (x0 == 0 && y0 == 0 && x1 == DIAL_W - 1 && y1 == DIAL_H - 1) {
        _fmemcpy(MARCO, ESFERA, DIAL_TAM);     /* fotograma completo */
        return;
    }
    for (y = y0; y <= y1; y++)
        _fmemcpy(MARCO + OFF170(y) + (unsigned)x0,
                 ESFERA + OFF170(y) + (unsigned)x0,
                 (unsigned)(x1 - x0 + 1));
}

/* caja del marco compuesto -> VRAM (por filas, _fmemcpy = rep movsw) */
static void vuelca(int x0, int y0, int x1, int y1)
{
    int y;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > DIAL_W - 1) x1 = DIAL_W - 1;
    if (y1 > DIAL_H - 1) y1 = DIAL_H - 1;
    if (x0 > x1 || y0 > y1) return;
    for (y = y0; y <= y1; y++)
        _fmemcpy((unsigned char far *)MK_FP(0xA000,
                 OFF320(DIAL_Y + y) + (unsigned)DIAL_X + (unsigned)x0),
                 MARCO + OFF170(y) + (unsigned)x0,
                 (unsigned)(x1 - x0 + 1));
}

/* un paso de reloj: solo toca la RAM y la VRAM donde hizo falta.
   forzar=1 tras recocinar la fecha (la esfera cambio): fotograma
   entero, que la ventanita puede quedar fuera de todas las cajas. */
static void actualiza(unsigned a_h, unsigned a_m, unsigned a_s, int forzar)
{
    int cambio[3];
    int vieja[3][4];
    int i, x0, x1, y0, y1;

    cambio[0] = forzar || (a_h != a_h0);
    cambio[1] = forzar || (a_m != a_m0);
    cambio[2] = forzar || (a_s != a_s0);
    if (!cambio[0] && !cambio[1] && !cambio[2]) return;
    if (forzar) hay_cajas = 0;

    if (hay_cajas) {
        for (i = 0; i < 3; i++) {
            if (!cambio[i]) continue;
            vieja[i][0] = caja[i][0]; vieja[i][1] = caja[i][1];
            vieja[i][2] = caja[i][2]; vieja[i][3] = caja[i][3];
            restaura(vieja[i][0], vieja[i][1], vieja[i][2], vieja[i][3]);
        }
    } else {
        restaura(0, 0, DIAL_W - 1, DIAL_H - 1);   /* fotograma completo */
    }

    lienzo = MARCO;
    dibuja_agujas(a_h, a_m, a_s);

    if (hay_cajas) {
        for (i = 0; i < 3; i++) {
            if (!cambio[i]) continue;
            x0 = (vieja[i][0] < caja[i][0]) ? vieja[i][0] : caja[i][0];
            y0 = (vieja[i][1] < caja[i][1]) ? vieja[i][1] : caja[i][1];
            x1 = (vieja[i][2] > caja[i][2]) ? vieja[i][2] : caja[i][2];
            y1 = (vieja[i][3] > caja[i][3]) ? vieja[i][3] : caja[i][3];
            vuelca(x0, y0, x1, y1);
        }
    } else {
        vuelca(0, 0, DIAL_W - 1, DIAL_H - 1);     /* fotograma completo */
    }

    hay_cajas = 1;
    a_h0 = a_h; a_m0 = a_m; a_s0 = a_s;
}

/* ==================== bucle principal ==================== */

/* convierte tics del BIOS en angulos (1/256 de vuelta) de las agujas.
   El PIT del BIOS marca 1.193182 MHz / 65536 = 18.2065 tics/s, de
   modo que segundos = tics * 2000 / 36413 (exacto, sin deriva) y
   r = resto en unidades de 1/36413 s, que da el barrido suave.
   Los productos van con desplazamientos; quedan solo las tres
   divisiones de los angulos, que se necesitan para no derivar. */
static void angulos(unsigned long tk, unsigned *a_h, unsigned *a_m,
                    unsigned *a_s, unsigned long *sod)
{
    /* tk * 2000 = 1024+512+256+128+64+16 veces tk */
    unsigned long t2k = (tk << 10) + (tk << 9) + (tk << 8)
                      + (tk << 7) + (tk << 6) + (tk << 4);
    unsigned long total = t2k / 36413UL;                   /* s desde las 0h */
    unsigned long sd    = total % 86400UL;
    unsigned long r     = t2k - total * 36413UL;           /* 0..36412 */
    unsigned long sub   = (r << 8) / 36413UL;              /* 0..255 */
    unsigned long h  = sd / 3600UL;
    unsigned long m  = (sd / 60UL) % 60UL;
    unsigned long s  = sd % 60UL;
    unsigned long m60  = (m << 5) + (m << 4) + (m << 3) + (m << 2) + s;
    unsigned long hm   = (((h % 12UL) << 11) + ((h % 12UL) << 10)
                          + ((h % 12UL) << 9) + ((h % 12UL) << 4))
                         + m60;

    *a_s = (unsigned)(((s << 8) + sub) / 60UL);
    *a_m = (unsigned)(((m60 << 8) + sub) / 3600UL);
    *a_h = (unsigned)(((hm << 8) + sub) / 43200UL);
    *sod = sd;
}

void programa(void)
{
    unsigned long tk, tk_ant = 0, sod = 0, sod_ant = 0;
    unsigned a_h = 0, a_m = 0, a_s = 0;
    int primero = 1;
    int dia = 1;
    unsigned char t;
    union REGS r;

    lee_fecha(&dia);

    if (!reservar()) {
        /* en modo texto: aviso y fuera (AH=09h, DS:DX -> cadena $) */
        static const char sin_mem[] =
            "ROLEX: falta memoria libre para el doble bufer (64 KB).\r\n$";
        r.h.ah = 0x09;
        r.x.dx = FP_OFF(sin_mem);
        int86(0x21, &r, &r);
        return;
    }

    modo_video(0x13);
    montar();
    pinta_fecha(dia);

    for (;;) {
        while (tecla(&t)) {
            if (t == 27) {                     /* ESC -> salir */
                modo_video(0x03);
                return;
            }
            /* cualquier otra tecla se descarta del buffer */
        }
        espera_retrazo();
        tk = lee_ticks();
        if (tk == tk_ant) continue;  /* sin tic nuevo no puede moverse nada */
        tk_ant = tk;
        angulos(tk, &a_h, &a_m, &a_s, &sod);
        if (!primero && sod < sod_ant) {       /* cruzo la medianoche */
            lee_fecha(&dia);
            pinta_fecha(dia);
            actualiza(a_h, a_m, a_s, 1);       /* la esfera cambio: entero */
        } else {
            actualiza(a_h, a_m, a_s, 0);       /* solo lo que cambio */
        }
        sod_ant = sod;
        primero = 0;
    }
}

#ifndef ROLEX_HOST
int main(void)
{
    programa();
    return 0;
}
#endif
