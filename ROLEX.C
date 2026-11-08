/*
 * ROLEX.C -- Reloj de pantalla para MS-DOS, al estilo del ROLEX.COM
 *            de RIX SoftWorks (1986).  Edicion FOTOREALISTA AUTONOMA.
 *
 * C para Open Watcom v2 (16 bits, modelo tiny, salida .COM).  Todo el
 * programa cabe en UN fichero, ROLEX.COM: la imagen del reloj (caja,
 * bisel, brazalete Y esfera fotograficos) viaja DENTRO del ejecutable,
 * comprimida con un RLE sencillo, junto con su paleta de 256 colores.
 *
 * La esfera no se sintetiza con fuentes: prep_img.py reduce la esfera
 * REAL (esfera2.jpg) y la pega sobre la foto de la caja (reloj2.png).
 * Todo va en escala de grises (indices 0..249, rampa de 250 niveles)
 * y la reduccion es por PROMEDIO DE AREA: los textos (corona, ROLEX,
 * OYSTER PERPETUAL, DATEJUST, SUPERLATIVE CHRONOMETER, SWISS MADE) y los
 * numeros de la pista de minutos salen como un gris proporcional al
 * grosor del trazo.  Antes se umbralizaban a tinta pura y los numeros
 * de minutos desaparecian.  Los indices 250..255 son los colores que
 * usa este programa para las agujas.
 *
 * Optimizacion (objetivo 8086, -0):
 *   - SOLO SE REDIBUJA CUANDO CAMBIA UN ANGULO.  El angulo va en 1/256
 *     de vuelta y sale de los tics del BIOS (18.2 por segundo): el
 *     segundero cambia unas 4 veces por segundo y las otras agujas casi
 *     nunca.  El resto del tiempo el bucle solo mira tics y teclado.
 *   - Fuera multiplicaciones y divisiones de los caminos calientes
 *     (los bucles por pixel y por fila): el offset de cada fila de la
 *     esfera sale de una TABLA (fila170[], rellena por sumas; en un
 *     8086 un desplazamiento por CL cuesta 4 ciclos por bit, de modo
 *     que 170 = 128+32+8+2 no ganaba al MUL, y la tabla gana a los
 *     dos), los 320 de la VRAM son 256+64 sobre constantes, las
 *     aristas de los poligonos se recorren con error de Bresenham
 *     (solo sumas y comparaciones), el segundero avanza por puntero,
 *     el disco y la fecha usan sumas y restas, y las proporciones de
 *     las agujas salen de sumas de desplazamientos (hw <= 5).  Quedan,
 *     solo al redibujar, los productos seno*radio de los vertices (en
 *     int de 16 bits) y las cuatro divisiones largas de la conversion
 *     de hora, que se evaluan unicamente cuando cambia el tic.
 *   - FRANJAS SUCIAS: cada primitiva anota, por fila, el tramo de x que
 *     ha pintado.  Para el fotograma siguiente solo se borran del marco
 *     (copiando de la esfera limpia) los tramos que pintaron las agujas
 *     anteriores, y a la VRAM solo se vuelcan los tramos de las agujas
 *     viejas y nuevas: unos pocos KB en vez de copiar 2 x 28.900 bytes.
 *     Los tramos se ensanchan a palabras (x par..x impar) y DIAL_X es
 *     PAR, asi que todas las copias son rep movsw alineados (8086/286).
 *   - Bloques con memset/memcpy siempre que se puede: el fondo RLE se
 *     decodifica por tiras y la ventana de la fecha se borra por filas.
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
 *     (Las primeras versiones usaban la pagina B000 como doble bufer:
 *     en modo 13h el BIOS no la mapea y sale blanca.  En RAM va bien.)
 *   - La ventanita de la fecha se "cocina" UNA VEZ en la esfera limpia
 *     (interior blanco + dia del mes, sin cero inicial); a medianoche
 *     se vuelve a cocinar con el dia nuevo (y se marca toda la esfera
 *     como sucia para que el marco y la VRAM la recojan entera).  El
 *     fotograma es solo: borrar agujas viejas -> agujas -> volcar.
 *
 * ANTI-PARPADEO: el fotograma se compone en RAM, se espera el retrazo
 * vertical y SOLO ENTONCES se vuelca a la VRAM; en pantalla nunca hay
 * estado intermedio.
 *
 * Hora: tics del BIOS en 0040:006C -> segundos con fraccion
 * (tics * 2000 / 36413; 18.2065 tics/s, sin deriva).  Fecha del DOS
 * (int 21h AH=2Ah) al arrancar y al cruzar medianoche.  ESC leyendo el
 * buffer del BIOS en 0040:001A.
 * Aritmetica entera: tabla de senos Q8.  El angulo va en 1/256 de
 * vuelta (0 = las 12, horario).
 *
 * Compilar (Open Watcom v2, 16 bits, DOS):
 *      wcl -mt -0 -bt=dos -ox -ot -d0 ROLEX.C        -> ROLEX.COM
 *      wcl -mt -0 -bt=dos -ox -ot -d0 -l=dos ROLEX.C -> ROLEX.EXE
 * (-ox = -obmiler -s: incluye -oe, que expande en linea las funciones
 *  __inline como sucia(), y -oi, que expande memset/memcpy/inp/outp;
 *  -ot prima la velocidad sobre el tamano)
 *
 * Regenerar la imagen desde las fotos (reloj2.png, esfera2.jpg):
 *      python3 prep_img.py reloj2.png esfera2.jpg IMGRLE.H
 */

#include <dos.h>     /* MK_FP, union REGS, SREGS, int86x, FP_OFF */
#include <conio.h>   /* inp, outp, cputs                  */
#include <string.h>  /* memcpy, memset, _fmemcpy, _fmemset */

/* ==================== imagen incrustada ==================== */

/* IMGRLE.H lo genera prep_img.py desde las FOTOS (esfera2.jpg y
   reloj2.png): geometria calibrada (DIAL_*, STRIP_*, VEN_* y agujas),
   la paleta PALETA[768] y el fondo FONDO_RLE[] (RLE propio). */
#include "IMGRLE.H"

/* ============================ constantes ============================ */

#define DCX (DIAL_W / 2)       /* centro del dial en coords locales */
#define DCY (DIAL_H / 2)
#define DIAL_TAM ((unsigned)DIAL_W * DIAL_H)   /* constante: 28900 */

/* offset de fila de la VRAM (320 = 256 + 64).  SOLO con argumento
   constante: el compilador lo resuelve en tiempo de compilacion.  El
   maximo es 199*320 = 63680: cabe en unsigned. */
#define OFF320(y) (((unsigned)(y) << 8) + ((unsigned)(y) << 6))

/* offsets de fila de la esfera (y * 170) SIN multiplicar ni desplazar:
   tabla de DIAL_H palabras que rellena montar() por sumas.  El maximo
   es 169*170 = 28730: cabe en unsigned. */
static unsigned fila170[DIAL_H];
#define OFF170(y) (fila170[y])

/* tamano de la ventanita de la fecha, en pixeles, y de los digitos
   (5x7 con un pixel de separacion: dos digitos = 11 de ancho) */
#define VEN_ANCHO (VEN_X1 - VEN_X0 + 1)
#define VEN_ALTO  (VEN_Y1 - VEN_Y0 + 1)
#define DIG_W    5
#define DIG_ALTO 7

/* comprobaciones de la cabecera generada (en compilacion, sin coste):
   el segundero se dibuja sin recortar y la fecha sin guardas */
#if (R_AG_S >= DCX) || (COL_S >= DCX)
#error El segundero se sale del dial: linea() no recorta
#endif
#if (VEN_ANCHO < 2 * DIG_W + 1) || (VEN_ALTO < DIG_ALTO)
#error La ventanita de la fecha no cabe para dos digitos
#endif

/* buferes de trabajo en RAM convencional; los fija reservar() */
static unsigned char far *MARCO;   /* fotograma compuesto          */
static unsigned char far *ESFERA;  /* esfera limpia (base)         */

/* indices fijos de la paleta (0..249 es la rampa de grises de la foto;
   250..255 los fija prep_img.py para lo que dibuja este programa) */
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

/* Digitos 5x7 para la fecha.  Bit 0x80 = pixel izquierdo (solo se usan
   los 5 bits altos).  Con 7 de ancho no cabia el pixel de separacion
   entre los dos digitos en una ventanita de 14: se pegaban. */
static const unsigned char FUENTE[10][DIG_ALTO] = {
    { 0x70, 0x88, 0x98, 0xA8, 0xC8, 0x88, 0x70 },  /* 0 */
    { 0x20, 0x60, 0x20, 0x20, 0x20, 0x20, 0x70 },  /* 1 */
    { 0x70, 0x88, 0x08, 0x10, 0x20, 0x40, 0xF8 },  /* 2 */
    { 0xF0, 0x08, 0x08, 0x70, 0x08, 0x08, 0xF0 },  /* 3 */
    { 0x10, 0x30, 0x50, 0x90, 0xF8, 0x10, 0x10 },  /* 4 */
    { 0xF8, 0x80, 0xF0, 0x08, 0x08, 0x88, 0x70 },  /* 5 */
    { 0x30, 0x40, 0x80, 0xF0, 0x88, 0x88, 0x70 },  /* 6 */
    { 0xF8, 0x08, 0x10, 0x20, 0x40, 0x40, 0x40 },  /* 7 */
    { 0x70, 0x88, 0x88, 0x70, 0x88, 0x88, 0x70 },  /* 8 */
    { 0x70, 0x88, 0x88, 0x78, 0x08, 0x10, 0x60 }   /* 9 */
};

/* tablas de cruces del relleno de poligonos: se reponen POR BLOQUE
   con memset (0x7F repetido da un entero muy positivo y 0x80 uno muy
   negativo: centinelas validos tanto con int de 16 como de 32 bits) */
static int cruce_min[DIAL_H];
static int cruce_max[DIAL_H];

/* ==================== primitivas de dibujo ==================== */

/* franjas sucias: [x min, x max] pintado en cada fila por las
   primitivas, en dos juegos (fotograma actual y anterior; act es el del
   ultimo fotograma compuesto).  Vacio = min muy positivo y max muy
   negativo (0x7F / 0x80 por bloque, como las tablas de cruces).
   cmin/cmax apuntan al juego en el que se anota. */
static int suc_min[2][DIAL_H];
static int suc_max[2][DIAL_H];
static int act;
static int *cmin = suc_min[0];
static int *cmax = suc_max[0];

static __inline void sucia(int x1, int x2, int y)
{
    if (x1 < cmin[y]) cmin[y] = x1;
    if (x2 > cmax[y]) cmax[y] = x2;
}

/* toda la esfera sucia: tras cocinar la fecha, para que el marco y la
   VRAM recojan la esfera entera en el siguiente fotograma */
static void sucio_todo(void)
{
    int y;
    for (y = 0; y < DIAL_H; y++) {
        suc_min[act][y] = 0;
        suc_max[act][y] = DIAL_W - 1;
    }
}

static void linea_h(int x1, int x2, int y, unsigned char c)
{
    int t;
    if (y < 0 || y >= DIAL_H) return;
    if (x1 > x2) { t = x1; x1 = x2; x2 = t; }
    if (x1 < 0) x1 = 0;
    if (x2 >= DIAL_W) x2 = DIAL_W - 1;
    if (x1 > x2) return;
    sucia(x1, x2, y);
    /* bloque horizontal: memset en vez de bucle de pixeles */
    _fmemset(MARCO + OFF170(y) + (unsigned)x1, c,
             (unsigned)(x2 - x1 + 1));
}

/* poligono convexo relleno por barrido de scanlines, recortado al
   dial.  Cada arista se recorre SIN dividir: error de Bresenham por
   filas (suma y comparacion; x avanza los pixeles que pida la
   pendiente), con el error inicial a media pendiente para que x salga
   REDONDEADA y no truncada, y las filas de los dos extremos incluidas
   (la inferior se perdia: la aguja salia un pixel mas corta por la
   cola).  Los cruces de cada fila se apuntan en las tablas, que se
   reponen con memset y se pintan al final con linea_h (memset). */
static void poligono(int n, const int *xs, const int *ys, unsigned char c)
{
    int i, y, ymin = DIAL_H - 1, ymax = 0;

    for (i = 0; i < n; i++) {
        if (ys[i] < ymin) ymin = ys[i];
        if (ys[i] > ymax) ymax = ys[i];
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
        int x, dx, dy, sx = 1, err, yfin;

        if (ys[i] == ys[j]) continue;
        if (ys[i] < ys[j]) { x = xs[i]; y = ys[i]; dx = xs[j] - x; dy = ys[j] - y; }
        else               { x = xs[j]; y = ys[j]; dx = xs[i] - x; dy = ys[i] - y; }
        if (dx < 0) { sx = -1; dx = -dx; }
        err = dy >> 1;
        yfin = y + dy + 1;
        if (yfin > ymax + 1) yfin = ymax + 1;
        for (; y < yfin; y++) {
            if (y >= ymin) {
                if (x < cruce_min[y]) cruce_min[y] = x;
                if (x > cruce_max[y]) cruce_max[y] = x;
            }
            err += dx;
            while (err >= dy) { err -= dy; x += sx; }
        }
    }
    for (y = ymin; y <= ymax; y++)
        if (cruce_max[y] >= cruce_min[y])
            linea_h(cruce_min[y], cruce_max[y], y, c);
}

/* disco solido (contrapeso del segundero y remache central): radio
   incremental, raiz cuadrada incremental y cada fila de UNA vez con
   linea_h (memset), sin bucles de pixeles */
static void disco(int cx, int cy, int r, unsigned char c)
{
    int dy, dy2 = 0;        /* dy*dy, incremental */
    int r2 = 0, k;
    int h = r, h2;          /* semiancho de fila y su cuadrado */

    for (k = 1; k <= r; k++)        /* r*r = suma de los r primeros impares */
        r2 += (k << 1) - 1;
    h2 = r2;

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

#define cos256(a) sen256(64u - (a))

/* punto del dial: s y c son seno y coseno (Q8) del angulo de la aguja,
   rr va a lo largo de la aguja (negativo = cola) y k es perpendicular.
   Todo en int de 16 bits: |s|,|c| <= 256 y dentro del dial |rr|+|k| <
   128, asi que cada suma (con el +128 del redondeo) queda por debajo
   de 32768.  El +128 redondea al pixel mas cercano: el >> 8 a secas
   trunca hacia abajo y corria las agujas medio pixel hacia arriba y a
   la izquierda segun el angulo.  La Y de la pantalla crece hacia
   abajo, de ahi el signo menos del coseno: sin el, las agujas girarian
   al reves. */
static void punto(int s, int c, int rr, int k, int *x, int *y)
{
    *x = DCX + ((s * rr + c * k + 128) >> 8);
    *y = DCY + ((s * k - c * rr + 128) >> 8);
}

/* linea de Bresenham (segundero), POR PUNTERO: sin recorte (la
   comprobacion #if de arriba garantiza que cae dentro del dial), sin
   calcular el offset por pixel y anotando la franja sucia UNA vez por
   fila (tramo xr..xa) en vez de en cada pixel */
static void linea(int x0, int y0, int x1, int y1, unsigned char c)
{
    int dx = x1 - x0, dy = y1 - y0;
    int sx, sy, err, e2, xa, xr = x0, paso;
    unsigned char far *p;
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;
    sx = (x0 < x1) ? 1 : -1;
    sy = (y0 < y1) ? 1 : -1;
    paso = (sy > 0) ? DIAL_W : -DIAL_W;
    err = dx - dy;
    p = MARCO + OFF170(y0) + (unsigned)x0;
    for (;;) {
        *p = c;
        if (x0 == x1 && y0 == y1) break;
        xa = x0;                       /* ultimo pixel pintado de la fila */
        e2 = err << 1;
        if (e2 > -dy) { err -= dy; x0 += sx; p += sx; }
        if (e2 <  dx) {
            err += dx;
            if (xr < xa) sucia(xr, xa, y0); else sucia(xa, xr, y0);
            y0 += sy; p += paso;
            xr = x0;                   /* empieza el tramo de la fila nueva */
        }
    }
    if (xr < x0) sucia(xr, x0, y0); else sucia(x0, xr, y0);
}

/* proporciones de la aguja con sumas de desplazamientos (valores
   TRUNCADOS, hw de 1 a 5, los que usa baston2):
     hwt   = 0.60 * hw  (77/128)     hwtip = 0.55 * hw  (141/256) */
#define HWT(hw)   ((((hw) << 6) + ((hw) << 3) + ((hw) << 2) + (hw)) >> 7)
#define HWTIP(hw) ((((hw) << 7) + ((hw) << 3) + ((hw) << 2) + (hw)) >> 8)

/* baston: aguja hexagonal (punta plana, flancos anchos junto al
   centro, cola estrecha), como las de oro blanco del Datejust */
static void baston(int s, int c, int r1, int cola, int hw, unsigned char col)
{
    int rrm = r1 >> 2;               /* donde la aguja es mas ancha */
    int hwt = HWT(hw);               /* semiancho de la cola        */
    int hwtip = HWTIP(hw);           /* semiancho en la punta       */
    int x[6], y[6];
    punto(s, c, r1, -hwtip, &x[0], &y[0]);
    punto(s, c, r1,  hwtip, &x[1], &y[1]);
    punto(s, c, rrm,  hw,   &x[2], &y[2]);
    punto(s, c, -cola,  hwt, &x[3], &y[3]);
    punto(s, c, -cola, -hwt, &x[4], &y[4]);
    punto(s, c, rrm, -hw,   &x[5], &y[5]);
    poligono(6, x, y, col);
}

/* baston con cuerpo: contorno oscuro + faceta brillante + faceta clara.
   Cada capa se encoge un pixel por TODOS los lados (r1, cola y hw): si
   solo se estrechaba el ancho, las facetas tapaban el contorno en la
   punta y en la cola y la aguja quedaba sin borde en los extremos. */
static void baston2(unsigned ang, int r1, int cola, int hw)
{
    int s = sen256(ang), c = cos256(ang);
    baston(s, c, r1, cola, hw, C_OSC);
    if (hw >= 2) baston(s, c, r1 - 1, cola - 1, hw - 1, C_HI);
    if (hw >= 3) baston(s, c, r1 - 2, cola - 2, hw - 2, C_CLA);
}

/* segundero: una linea fina de cola a punta (la real es un hilo
   negro de un pixel; sin contrapeso circular) */
static void segundero(unsigned ang)
{
    int s = sen256(ang), c = cos256(ang);
    int xa, ya, xb, yb;
    punto(s, c, -COL_S, 0, &xa, &ya);
    punto(s, c,  R_AG_S, 0, &xb, &yb);
    linea(xa, ya, xb, yb, C_OSC);
}

/* remache central sobre el eje de las agujas */
static void centro(void)
{
    disco(DCX, DCY, CEN_RO, C_OSC);
    disco(DCX, DCY, CEN_RI, C_CLA);
    disco(DCX, DCY, CEN_PN, C_OSC);
}

/* ==================== fecha cocinada en la esfera ==================== */

/* un digito 5x7 dentro de la ventanita, directo en la esfera limpia
   (sin recorte ni franjas: pinta_fecha marca toda la esfera sucia) */
static void digito(int x, int y, char ch)
{
    const unsigned char *g = FUENTE[ch - '0'];
    unsigned char far *d = ESFERA + OFF170(y) + (unsigned)x;
    unsigned char m;
    int f, b;
    for (f = 0; f < DIG_ALTO; f++) {
        unsigned char bits = g[f];
        for (b = 0, m = 0x80; b < DIG_W; b++, m >>= 1)
            if (bits & m)
                d[b] = C_NEG;
        d += DIAL_W;
    }
}

/* dia del mes en la ventanita de las 3 (sin cero inicial: el dia 8 se
   escribe "8", no "08"), centrado en la abertura de IMGRLE.H.  Se
   cocina UNA VEZ sobre la esfera limpia: el fotograma de cada frame
   la copia y asi no se repinta a 70 Hz.  El interior se borra por
   filas de UNA vez (memset por bloque).  Que la ventanita quepa en la
   esfera y los digitos en la ventanita lo comprueban los #if de
   arriba. */
static void pinta_fecha(int dia)
{
    unsigned off = OFF170(VEN_Y0) + (unsigned)VEN_X0;
    int yd = VEN_Y0 + ((VEN_ALTO - DIG_ALTO) >> 1);
    int y;

    for (y = 0; y < VEN_ALTO; y++) {
        _fmemset(ESFERA + off, C_BLA, (unsigned)VEN_ANCHO);
        off += DIAL_W;
    }
    if (dia < 10) {                     /* un digito, centrado */
        digito(VEN_X0 + ((VEN_ANCHO - DIG_W) >> 1), yd, (char)('0' + dia));
    } else {
        int x0 = VEN_X0 + ((VEN_ANCHO - (2 * DIG_W + 1)) >> 1);
        int dec = 0;
        while (dia >= 10) { dia -= 10; dec++; }   /* dia / 10 y dia % 10 */
        digito(x0,             yd, (char)('0' + dec));
        digito(x0 + DIG_W + 1, yd, (char)('0' + dia));
    }
    sucio_todo();
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

/* dia del mes del DOS (int 21h AH=2Ah; el DOS ya lo lee del RTC al
   arrancar y lo adelanta solo al cruzar medianoche) */
static int lee_fecha(void)
{
    union REGS r;
    r.h.ah = 0x2A;
    int86(0x21, &r, &r);
    return r.h.dl;
}

/* ==================== memoria de trabajo ==================== */

/* Pide al DOS un bloque de 64 KB (0x1000 parrafos) para el marco y la
   esfera limpia (57.800 bytes).  Dos caminos, sin mas:

     1. int 21h AH=48h: un bloque libre, si lo hay (el crt0 de Watcom
        suele haber reducido ya la reserva del COM).
     2. Si no lo hay: la reserva del COM cubre TODA la memoria libre;
        se recorta (AH=4Ah) y se vuelve a pedir un bloque (AH=48h) con
        lo liberado.  Se recorta a 0x1010 parrafos y no a 0x1000: para
        el COM sobra (mide menos de 64 KB), pero si se compila como
        EXE la pila va al final de DGROUP, justo tras el PSP de 16
        parrafos, y 0x1000 la dejaria fuera del bloque.

   Devuelve 1 si consigue los dos buferes, 0 si no hay memoria. */
static int reservar(void)
{
    union REGS r;
    struct SREGS s;

    r.x.ax = 0x4800;
    r.x.bx = 0x1000;
    int86(0x21, &r, &r);
    if (r.x.cflag != 0) {
        r.h.ah = 0x62;                     /* PSP: segmento del programa */
        int86(0x21, &r, &r);
        segread(&s);
        s.es = r.x.bx;
        r.x.ax = 0x4A00;                   /* recortar la reserva */
        r.x.bx = 0x1010;
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

/* Descompresion RLE directamente a la VRAM visible: una sola tira que
   se escribe de izquierda a derecha por la franja central (STRIP_W
   columnas desde STRIP_X).  Los pixeles son indices 0..249, de modo que
   los bytes 250..255 sirven de marca de racha:
       b < 250      un pixel suelto
       b = 251..255 racha de b-248 (3..7) pixeles del byte siguiente
       b = 250      racha larga: byte de cuenta (8..255) y byte valor
   Nueve de cada diez rachas de la imagen son pixeles sueltos (los
   degradados de la foto), asi que van por un camino corto, sin llamar
   a memset.  Cada tira de una racha se escribe con UN memset (bloque)
   y la fila avanza con un puntero, sin multiplicar. */
static void decodificar_fondo(void)
{
    const unsigned char *p   = FONDO_RLE;
    const unsigned char *fin = FONDO_RLE + FONDO_RLE_LEN;
    unsigned char far *fila = (unsigned char far *)MK_FP(0xA000, STRIP_X);
    unsigned x = 0;                       /* columna relativa 0..STRIP_W-1 */

    while (p < fin) {
        unsigned char v = *p++;
        unsigned n;
        if (v < 250) {                    /* pixel suelto */
            fila[x] = v;
            if (++x == (unsigned)STRIP_W) {
                x = 0;
                fila += 320u;             /* fila siguiente */
            }
            continue;
        }
        n = (v == 250) ? (unsigned)*p++ : (unsigned)(v - 248);
        v = *p++;
        while (n != 0u) {
            unsigned cabe = (unsigned)STRIP_W - x;  /* px hasta fin de fila */
            if (n < cabe) {
                _fmemset(fila + x, v, n);
                x += n;
                n = 0;
            } else {                     /* la racha cruza la fila */
                _fmemset(fila + x, v, cabe);
                x = 0;
                fila += 320u;            /* fila siguiente */
                n -= cabe;
            }
        }
    }
}

/* Monta la escena una sola vez al arrancar: tabla de offsets de fila,
   paleta al DAC, pantalla a negro, franja central descomprimida a su
   sitio y esfera limpia recortada al bloque de RAM (detras del marco
   de trabajo).  El offset de la VRAM avanza por suma (320 por fila),
   sin multiplicar. */
static void montar(void)
{
    unsigned off = OFF320(DIAL_Y) + (unsigned)DIAL_X;   /* 15*320+76 */
    unsigned o = 0;
    int f, i;

    for (f = 0; f < DIAL_H; f++) {        /* fila170[f] = f * 170, por sumas */
        fila170[f] = o;
        o += DIAL_W;
    }

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

/* ==================== bucle principal ==================== */

/* fotograma en el marco de RAM.  Primero devuelve a su estado de esfera
   limpia (copiando de ESFERA) las franjas que pintaron las agujas del
   fotograma anterior; despues pinta las nuevas, anotando sus franjas en
   el otro juego.  Las franjas se ensanchan a palabras: x par ..
   x impar (DIAL_W es par, asi que no se sale de la fila). */
static void compone(unsigned a_h, unsigned a_m, unsigned a_s)
{
    const int *pmin = suc_min[act], *pmax = suc_max[act];
    unsigned off = 0;
    int y, x1;

    for (y = 0; y < DIAL_H; y++) {
        if (pmax[y] >= pmin[y]) {
            x1 = pmin[y] & ~1;
            _fmemcpy(MARCO + off + x1, ESFERA + off + x1,
                     (unsigned)((pmax[y] | 1) - x1 + 1));
        }
        off += DIAL_W;
    }
    act ^= 1;
    cmin = suc_min[act];
    cmax = suc_max[act];
    memset((char *)cmin, 0x7F, sizeof(suc_min[0]));
    memset((char *)cmax, 0x80, sizeof(suc_max[0]));

    baston2(a_h, R_AG_H, COL_H, HW_H);
    baston2(a_m, R_AG_M, COL_M, HW_M);
    segundero(a_s);
    centro();
}

/* vuelca a la VRAM lo que cambia: por fila, la union de las franjas del
   fotograma anterior (hay que borrarlas) y del nuevo (hay que pintarlas).
   Se llama justo despues de espera_retrazo(), asi que en la pantalla
   nunca aparece un fotograma a medias. */
static void volcar(void)
{
    const int *pmin = suc_min[act ^ 1], *pmax = suc_max[act ^ 1];
    unsigned mo = 0;                                  /* fila en MARCO */
    unsigned vo = OFF320(DIAL_Y) + (unsigned)DIAL_X;  /* fila en VRAM  */
    int y, x1, x2;

    for (y = 0; y < DIAL_H; y++) {
        x1 = cmin[y]; if (pmin[y] < x1) x1 = pmin[y];
        x2 = cmax[y]; if (pmax[y] > x2) x2 = pmax[y];
        if (x2 >= x1) {
            x1 &= ~1;
            _fmemcpy((unsigned char far *)MK_FP(0xA000, vo + x1),
                     MARCO + mo + x1, (unsigned)((x2 | 1) - x1 + 1));
        }
        mo += DIAL_W;
        vo += 320u;
    }
}

/* convierte tics del BIOS en angulos (1/256 de vuelta) de las agujas.
   El PIT del BIOS marca 1.193182 MHz / 65536 = 18.2065 tics/s, de
   modo que segundos = tics * 2000 / 36413 (exacto, sin deriva) y
   r = resto en unidades de 1/36413 s, que da el barrido suave.
   El tiempo en 1/256 de segundo, t256 = seg*256 + sub, da los tres
   angulos (el contador del BIOS se pone a cero a medianoche: seg <
   86400):
       segundero  = (t256 / 60)    & 255      (60 s por vuelta)
       minutero   = (t256 / 3600)  & 255      (1 h por vuelta)
       horario    = (t256 / 43200) & 255      (12 h por vuelta)
   Las divisiones enteras se encadenan sin perder exactitud:
   t256/3600 = (t256/60)/60 y t256/43200 = (t256/3600)/12, y el
   minutero (<= 6144) ya cabe en 16 bits: solo quedan dos divisiones
   largas para los angulos, la ultima de 16 bits.  Los productos van
   con desplazamientos.  Solo se llama cuando cambia el tic. */
static void angulos(unsigned long tk, unsigned *a_h, unsigned *a_m,
                    unsigned *a_s)
{
    /* tk * 2000 = 1024+512+256+128+64+16 veces tk */
    unsigned long t2k = (tk << 10) + (tk << 9) + (tk << 8)
                      + (tk << 7) + (tk << 6) + (tk << 4);
    unsigned long seg = t2k / 36413UL;                     /* s desde las 0h */
    unsigned long t256 = (seg << 8)
                       + (((t2k - seg * 36413UL) << 8) / 36413UL);
    unsigned long vs = t256 / 60UL;      /* vueltas del segundero, sin recortar */
    unsigned long vm = vs / 60UL;        /* idem minutero: cabe en unsigned */

    *a_s = (unsigned)vs & 255u;
    *a_m = (unsigned)vm & 255u;
    *a_h = ((unsigned)vm / 12u) & 255u;
}

int main(void)
{
    unsigned long tk, tk_ant;
    unsigned a_h, a_m, a_s;
    unsigned p_h = 0xFFFFu, p_m = 0xFFFFu, p_s = 0xFFFFu;  /* ya dibujados */
    unsigned char t;
    union REGS r;

    if (!reservar()) {
        /* en modo texto: aviso y fuera (AH=09h, DS:DX -> cadena $) */
        static const char sin_mem[] =
            "ROLEX: falta memoria libre para el doble bufer (64 KB).\r\n$";
        r.h.ah = 0x09;
        r.x.dx = FP_OFF(sin_mem);
        int86(0x21, &r, &r);
        return 1;
    }

    modo_video(0x13);
    montar();
    pinta_fecha(lee_fecha());
    tk_ant = lee_ticks() - 1;            /* != tk: el primer fotograma sale ya */

    for (;;) {
        while (tecla(&t)) {
            if (t == 27) {                     /* ESC -> salir */
                modo_video(0x03);
                /* \x82 = e con acento en CP437/CP850 (el fuente va en UTF-8) */
                cputs("ROLEX.COM v6 (c) Copyright 2026 by Javier Guti\x82rrez Chamorro (Guti)\r\n");
                return 0;
            }
            /* cualquier otra tecla se descarta del buffer */
        }
        tk = lee_ticks();
        if (tk == tk_ant) continue;            /* mismo tic: nada que hacer */
        if (tk < tk_ant) {                     /* cruzo la medianoche */
            pinta_fecha(lee_fecha());
        }
        tk_ant = tk;
        angulos(tk, &a_h, &a_m, &a_s);
        if (a_h == p_h && a_m == p_m && a_s == p_s) continue;  /* igual */
        p_h = a_h; p_m = a_m; p_s = a_s;
        compone(a_h, a_m, a_s);
        espera_retrazo();                      /* y SOLO ahora, a la VRAM */
        volcar();
    }
}
