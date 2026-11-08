/* host_main.c - Arnés de verificación para ROLEX.C en un PC moderno.
 *
 * Define ROLEX_HOST, incluye el programa DOS tal cual y sustituye el
 * hardware por una RAM simulada: VRAM en emu[0xA0000..], BIOS en
 * emu[0x400..], puertos de video/CMOS/DAC falsos.  Al final vuelca la
 * pantalla a un PPM para inspeccionarla.
 *
 * Uso: host_rolex [HH MM SS] [DD MM AAAA] [frames] [salida.ppm]
 */
#define ROLEX_HOST
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>

#include "../ROLEX.C"  /* aqui viven las declaraciones de inp/outp/int86 */

/* ------------------------- hardware simulado ------------------------- */

unsigned char emu[0x110000];

static unsigned char cmos[0x40];
static unsigned      cmos_addr = 0;
static unsigned char dac[256][3];
static int           dac_idx = 0, dac_comp = 0;

static long vframes = 0;
static long max_frames = 12;
static int  key_injected = 0;

static int hh_f = 10, mm_f = 9, ss_f = 36;
static int dia_f = 8, mes_f = 11, anio_f = 2026;
static const char *salida_f = "frame.ppm";

static void dump_ppm(const char *nombre)
{
    FILE *f = fopen(nombre, "wb");
    int i;
    if (f == NULL) { perror("ppm"); exit(2); }
    fprintf(f, "P6\n320 200\n255\n");
    for (i = 0; i < 320 * 200; i++) {
        unsigned char idx = emu[0xA0000 + i];
        unsigned char rgb[3];
        rgb[0] = (unsigned char)(dac[idx][0] * 4);
        rgb[1] = (unsigned char)(dac[idx][1] * 4);
        rgb[2] = (unsigned char)(dac[idx][2] * 4);
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
    fprintf(stderr, "dump: %s\n", nombre);
}

unsigned char inp(unsigned puerto)
{
    if (puerto == 0x3DA) {
        static int fase = 0;
        fase ^= 1;
        if (fase) {                       /* un retrazo vertical mas */
            vframes++;
            if (vframes % 4 == 0) {       /* ~18,2 tics/s simulados */
                unsigned long tk;
                tk  =  (unsigned long)emu[0x46C]
                     | ((unsigned long)emu[0x46D] << 8)
                     | ((unsigned long)emu[0x46E] << 16)
                     | ((unsigned long)emu[0x46F] << 24);
                tk++;
                emu[0x46C] = (unsigned char)(tk & 0xFF);
                emu[0x46D] = (unsigned char)((tk >> 8) & 0xFF);
                emu[0x46E] = (unsigned char)((tk >> 16) & 0xFF);
                emu[0x46F] = (unsigned char)((tk >> 24) & 0xFF);
            }
            if (vframes >= max_frames && !key_injected) {
                emu[0x41E] = 0x1B;        /* tecla ESC en el buffer 0040:001E */
                emu[0x41F] = 0x01;
                emu[0x41C] = 0x20;        /* cola = cabeza + 2 */
                emu[0x41D] = 0x00;
                key_injected = 1;
            }
        }
        return fase ? 0x08 : 0x00;
    }
    if (puerto == 0x71) return cmos[cmos_addr & 0x3F];
    return 0;
}

void outp(unsigned puerto, unsigned char valor)
{
    if (puerto == 0x70) cmos_addr = valor;
    else if (puerto == 0x3C8) { dac_idx = valor; dac_comp = 0; }
    else if (puerto == 0x3C9) {
        dac[dac_idx & 0xFF][dac_comp % 3] = valor;
        dac_comp++;
    }
    /* 0x71: escritura al CMOS, ignorada */
}

int int86(int num, union REGS *entrada, union REGS *salida)
{
    *salida = *entrada;
    if (num == 0x10) {
        if (entrada->h.ah == 0x00) {
            if (entrada->h.al == 0x03) {  /* vuelta al modo texto: volcar */
                dump_ppm(salida_f);
                exit(0);
            }
        }
    } else if (num == 0x21) {
        if (entrada->h.ah == 0x2A) {      /* obtener fecha */
            salida->w.cx = (unsigned short)anio_f;
            salida->h.dh = (unsigned char)mes_f;
            salida->h.dl = (unsigned char)dia_f;
        } else if (entrada->h.ah == 0x2C) { /* obtener hora */
            salida->h.ch = (unsigned char)hh_f;
            salida->h.cl = (unsigned char)mm_f;
            salida->h.dh = (unsigned char)ss_f;
            salida->h.dl = 0;
        }
    }
    return 0;
}

/* ------------------------------ main ------------------------------ */

int main(int argc, char **argv)
{
    unsigned long tk0, seg0;

    if (argc > 3) { hh_f = atoi(argv[1]); mm_f = atoi(argv[2]); ss_f = atoi(argv[3]); }
    if (argc > 6) { dia_f = atoi(argv[4]); mes_f = atoi(argv[5]); anio_f = atoi(argv[6]); }
    if (argc > 7) max_frames = atol(argv[7]);
    if (argc > 8) salida_f = argv[8];

    /* RTC del CMOS simulado, en BCD, formato 24h */
#define BCD(v) ((((v) / 10) << 4) | ((v) % 10))
    cmos[0x00] = (unsigned char)BCD(ss_f);
    cmos[0x02] = (unsigned char)BCD(mm_f);
    cmos[0x04] = (unsigned char)BCD(hh_f);
    cmos[0x07] = (unsigned char)BCD(dia_f);
    cmos[0x08] = (unsigned char)BCD(mes_f);
    cmos[0x09] = (unsigned char)BCD(anio_f % 100);
    cmos[0x32] = (unsigned char)BCD(anio_f / 100);
    cmos[0x0A] = 0x00;                    /* sin update-in-progress */
    cmos[0x0B] = 0x02;                    /* 24 h, datos BCD */
#undef BCD

    /* tics del BIOS iniciales: seg0 * 182065 / 10000 (sin desbordar) */
    seg0 = (unsigned long)(hh_f * 3600 + mm_f * 60 + ss_f);
    tk0 = (seg0 * 36413UL) / 2000UL;
    emu[0x46C] = (unsigned char)(tk0 & 0xFF);
    emu[0x46D] = (unsigned char)((tk0 >> 8) & 0xFF);
    emu[0x46E] = (unsigned char)((tk0 >> 16) & 0xFF);
    emu[0x46F] = (unsigned char)((tk0 >> 24) & 0xFF);

    /* buffer de teclado del BIOS vacio (cabeza = cola = 1Eh) */
    emu[0x41A] = 0x1E;
    emu[0x41C] = 0x1E;

    programa();

    /* si el programa sale sin restaurar el modo texto, volcar igualmente */
    dump_ppm(salida_f);
    return 0;
}
