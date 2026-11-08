=====================================================================
 ROLEX para MS-DOS -- reloj de pantalla FOTOREALISTA (modo 13h)
=====================================================================

Clone libre del salvapantallas ROLEX.COM de RIX SoftWorks (1986),
esta vez con un reloj real: la foto de un Rolex Oyster Perpetual
Datejust de acero sobre fondo negro, y la esfera limpia (sin agujas)
sobre la que se dibujan hora, minutos, segundos y la fecha del dia.

La esfera es la foto real; las agujas se dibujan por software como
bastones de acero pulido y se actualizan ~70 veces por segundo con
barrido suave (no van a saltos) y en el sentido correcto (horario).

Novedad de esta version: el dial ha crecido a 170 pixels (antes 148)
y TODO VIAJA DENTRO DEL EJECUTABLE: la foto, comprimida con el RLE
del PCX, va incrustada en ROLEX.COM junto con su paleta.  Ya no hace
falta ningun fichero de graficos externo ni la API de ficheros de
DOS: ROLEX.COM es una sola pieza que se copia y se ejecuta.

---------------------------------------------------------------------
 1. QUE HACE FALTA
---------------------------------------------------------------------
  - MS-DOS (o DOSBox) con VGA de 128 KB o mas (cualquier tarjeta
    VGA estandar; la pagina oculta de video B000 se usa de banco de
    trabajo y en una VGA de 64 KB, como el MCGA del PS/2 Modelo 30,
    no cabria).
  - 640 KB de memoria convencional.
  - NADA MAS: la imagen va dentro del ejecutable.

Ficheros del paquete:

  ROLEX.COM   programa completo (25,1 KB: codigo + paleta + foto
              comprimida RLE).  Binario plano: copiar y ejecutar.
  ROLEX.EXE   el mismo programa en formato MZ (por si se prefiere)
  ROLEX.C     fuente unico, comentado, 100% C
  IMGRLE.H    la imagen incrustada (paleta + fondo comprimido RLE)
              y las constantes de geometria calibradas; lo genera
              prep_img.py y ROLEX.C lo incluye
  BUILD.BAT   construccion con Open Watcom en DOS
  MAKEFILE    lo mismo para wmake
  CAPTURA.PNG aspecto del programa (10:09:36, dia 8)
  LEEME.TXT   este fichero
  verificacion/  arnes para comprobar el programa en un PC moderno

---------------------------------------------------------------------
 2. USO
---------------------------------------------------------------------
  ROLEX           (o ROLEX.EXE)

  Pulsa ESC para salir.  Cualquier otra tecla se descarta.

  La fecha se lee del RTC del CMOS al arrancar y al cruzar la
  medianoche; la hora viene del contador de tics del BIOS.  La
  ventanita muestra el dia del mes sin cero inicial (8, no 08).

---------------------------------------------------------------------
 3. CONSTRUIR (Open Watcom v2, 16 bits)
---------------------------------------------------------------------
  En DOS:   BUILD.BAT        -> ROLEX.COM
            BUILD.BAT exe    -> ROLEX.EXE
  Con wmake:  wmake          -> ROLEX.COM
              wmake rolex.exe -> ROLEX.EXE

  (BUILD.BAT y MAKEFILE usan wcl, que compila con wcc -ms y enlaza
  con "system com"/"system dos" anadiendo las bibliotecas; equivale
  a: wcl -mt -0 -bt=dos -os -d0 rolex.c)

  IMGRLE.H debe estar junto a ROLEX.C (viaja en el paquete).  El
  binario no arrastra la libc de Watcom: ya no hay ni carga de
  ficheros ni mensajes (la imagen va incrustada), solo la reserva
  de fecha int 21h AH=2Ah si el RTC delirara.

---------------------------------------------------------------------
 4. COMO ESTA HECHO
---------------------------------------------------------------------
  Imagen  IMGRLE.H lleva PALETA[768] y FONDO_RLE[]: la franja central
          del fondo (247 columnas de 320: solo el ancho con contenido
          real; a los lados hay negro puro, que comprimido son ~2
          bytes por fila) comprimida con el RLE DEL PCX:
             byte < C0h  ->  un pixel de ese indice
             byte >= C0h ->  contador (byte & 3Fh) de pixeles del
                             byte siguiente
          Es el mismo algoritmo del formato PCX, trivial de
          descomprimir en una docena de lineas de C.  La foto ocupa
          49.400 bytes sin comprimir y 18.836 en RLE; junto con la
          paleta y el codigo, el COM completo pesa 25,1 KB, mucho
          menos que los 64 KB de un segmento tiny.

  Video   modo 13h (320x200, 256 colores).  Al arrancar se deja la
          VRAM a negro y la imagen se DESCOMPRIME DIRECTAMENTE a la
          franja central de A000:0000; no se toca mas.

  Pagina  A000 es la pantalla visible; B000 (la segunda pagina de la
  oculta  VGA) guarda el area de trabajo:
             B000:00000  marco de trabajo (fotograma compuesto)
             B000:28900  esfera limpia (copia 170x170 del fondo)
          En la RAM del programa apenas queda nada: codigo, paleta,
          imagen comprimida y las tablas de senos y digitos.

  ANTI-   Cada fotograma se compone entero en el marco de B000: copia
  PARPADEO de la esfera limpia + fecha + agujas, y se vuelca a la
          VRAM visible con UNA sola copia por linea al empezar el
          retrazo vertical (puerto 3DAh).  En la pantalla nunca hay
          un estado intermedio visible: por eso no parpadea nada.

  Hora    contador de tics del BIOS en 0040:006C.  El PIT marca
          18.2065 tics/s, asi que segundos = tics*2000/36413, exacto
          y sin deriva; el resto da la fraccion de segundo y de ahi
          el barrido suave del segundero (y de los otros dos).

  Agujas  hexagonos (baston con punta plana, panza y cola) rellenos
          por scanline, tres tonos de acero: borde oscuro, cuerpo
          brillante y faceta clara.  Tabla de senos en punto fijo
          Q8 y raiz entera; ni una operacion de coma flotante.  El
          angulo 0 apunta a las 12 y crece en sentido horario (la Y
          de la pantalla crece hacia abajo, y el signo del coseno en
          punto() lo compensa).  Los radios de las agujas se escalan
          con el dial y viajan calibrados en IMGRLE.H.

  Fecha   RTC del CMOS por los puertos 70h/71h (esperando el bit
          UIP, BCD -> binario); si el RTC delira, reserva int 21h
          AH=2Ah.  Se escribe en la ventanita con una fuente propia
          de digitos 8x8, centrada en la abertura.  Solo se relee a
          medianoche.

  Teclado ESC se lee del buffer del BIOS en 0040:001A avanzando la
          cabeza a mano (nada de int 16h).

  Paleta  los colores cuantizados de la foto (hasta 250; la foto es
          practicamente monocroma y se queda en ~120, con la esfera
          y el resto compartiendo la misma rampa de grises) + 6
          reservados (250..255: aceros, negro y blanco de la fecha).
          Se programa por el DAC (3C8h/3C9h, valores /4).

  Calidad el generador (prep_img.py) enfoca la esfera ANTES de
          reducir (conserva los bordes del texto), reescala con
          LANCZOS, estira los niveles en su interior (texto negro,
          fondo blanco) y remata con dos mascaras de enfoque.
          Cuantiza sin difusion de error en toda la imagen: dentro
          de la esfera el texto no se emborrona, y fuera el metal
          queda mas limpio y el RLE se hace mas pequeno (la foto es
          casi monocroma y la paleta tiene grises de sobra).

---------------------------------------------------------------------
 5. FORMATO DE LA IMAGEN INCRUSTADA (IMGRLE.H)
---------------------------------------------------------------------
  PALETA[768]      256 entradas x RGB (0..255); se programa /4 en el
                   DAC.  Los indices 250..255 son fijos (agujas y
                   fecha); los anteriores, la foto cuantizada.
  FONDO_RLE[]      flujo RLE estilo PCX de la franja STRIP_W x 200
                   (247 columnas x 200 filas), fila a fila de
                   izquierda a derecha.
  FONDO_RLE_LEN    longitud del flujo.
  DIAL_W/H,        geometria calibrada: la esfera limpia (170x170)
  DIAL_X/Y,        cae en la pantalla en (75,15); la ventanita de la
  VEN_X0..Y1,      fecha es el rectangulo local 135..149 x 81..88;
  STRIP_W/X;       la franja se pega a partir de la columna 43.  Los
  R_AG_H..CEN_PN   radios de las agujas van escalados al dial.

  La esfera limpia NO va comprimida: el programa la recorta de la
  propia VRAM al arrancar (filas 15..184, columnas 75..244) y la
  guarda en la pagina oculta.  Ninguna copia redundante viaja.

  Todo lo genera el script verificacion/prep_img.py a partir de las
  fotos originales (verificacion/fotos/reloj2.png y esfera.jpg):

      python3 prep_img.py [--sin-difusion]

  Escribe IMGRLE.H en el directorio padre (junto a ROLEX.C) y las
  previsualizaciones prev_*.png.  Si cambias las fotos o el tamano
  del dial, ejecutalo y recompila: ROLEX.C y verifica.py leen las
  constantes de IMGRLE.H, asi que no hay nada que tocar a mano.

---------------------------------------------------------------------
 6. VERIFICACION EN UN PC MODERNO (carpeta verificacion/)
---------------------------------------------------------------------
  El mismo ROLEX.C compila en gcc con un arnes que simula la VRAM
  (A000 visible y B000 oculta), los puertos y la int 21h, y vuelca
  la pantalla a PPM/PNG:

    gcc -O2 -o host_rolex host_main.c
    ./host_rolex 10 9 36 8 11 2026 12 captura.ppm
    python3 ppm2png.py captura.ppm captura.png

  (el arnes no lleva imagen incrustada aparte: usa el mismo
  IMGRLE.H que incluye ROLEX.C, asi que comprueba exactamente el
  mismo codigo y los mismos datos que el COM)

  Comparacion pixel a pixel contra una referencia independiente en
  Python (misma paleta, mismos angulos, mismas primitivas enteras):

    python3 prueba_frames.py

  Comprueba seis instantes (12:00:00, 3:00:00, 9:15:30, 6:30:15,
  un barrido de 30 tics a las 10:09:36 y el cruce de medianoche a
  las 23:59:50).  Resultado actual: 0 pixels distintos de 64000 en
  los seis casos, es decir, sin rastro de agujas sin borrar y con
  los angulos exactos.

---------------------------------------------------------------------
 7. HISTORIA
---------------------------------------------------------------------
  v5 (esta): dial de 148 a 170 pixels; la imagen va DENTRO del COM
             (RLE estilo PCX + paleta en IMGRLE.H, incluido por
             ROLEX.C): un solo fichero, sin carga por int 21h; los
             buferes de trabajo (marco y esfera limpia) pasan a la
             pagina oculta de video B000; franja recortada al ancho
             con contenido (247 columnas); sin difusion de error
             fuera de la esfera (metal mas limpio y RLE menor);
             fecha centrada en la ventanita.
  v4: ROLEX.IMG de 63,3 a 44,5 KB (solo se guarda la franja central
             de 224 columnas con contenido), esfera mas nitida
             (enfoque antes de reducir, estiramiento de niveles y
             doble mascara) y fecha sin cero inicial (8, no 08).
  v3: foto nueva en alta resolucion sobre fondo negro, esfera
             con mascara de enfoque y sin difusion de error (texto
             nitido), paleta 250+6, ROLEX.IMG reducido de 87 a 63,3
             KB (la esfera se recorta de la VRAM, no viaja en el
             fichero), sentido de las agujas corregido (la Y estaba
             con el signo cambiado y giraban al reves).
  v2: esfera y fondo fotorealistas de fotos reales, agujas de acero
             con cuerpo pulido, fecha sobre la ventana del dial,
             redibujo con doble bufer (cero parpadeo), carga de
             graficos por int 21h, fuente reducida a digitos.
  v1: esfera dibujada por codigo (bisel acanalado, indices, leyendas
             con fuente 8x8 propia, segundero rojo).

  Inspirado en el ROLEX.COM de RIX SoftWorks (1986).  Las fotos del
  Datejust las puso el usuario del encargo.
