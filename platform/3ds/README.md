# Phase Distorter 3DS

Port para Nintendo 3DS de [Phase Distorter](https://github.com/TheRunaway5/Phase-Distorter)
(port nativo de EarthBound / Mother 2, licencia MIT). Usa las dos pantallas:

- **Superior (400×240):** el juego, centrado (256×224) o en modo panorámico de 400 px.
- **Inferior (táctil):** HP/PP y nivel de cada integrante en vivo, botones táctiles
  *Hablar* (L) y *Mapa* (X), y un menú de *Opciones* (panorámico, rendimiento, salir).

No incluye ningún asset del juego: necesitas tu propia ROM.

## Compilar

### Opción A — GitHub Actions (sin instalar nada)
Sube este fork a GitHub. El workflow `.github/workflows/build-3ds.yml` compila con la
imagen oficial `devkitpro/devkitarm` y deja `phase-distorter-3ds.3dsx` como artefacto
en la pestaña **Actions**.

### Opción B — Local con devkitPro
1. Instala devkitPro y el grupo `3ds-dev` (incluye devkitARM, libctru y `3ds-cmake`).
2. Desde la raíz del repo:
   ```
   cmake -S platform/3ds -B build-3ds -DCMAKE_TOOLCHAIN_FILE=$DEVKITPRO/cmake/3DS.cmake -DCMAKE_BUILD_TYPE=Release
   cmake --build build-3ds -j
   ```
3. Resultado: `build-3ds/phase-distorter-3ds.3dsx`.

## Instalar en la consola

Requiere una 3DS con homebrew (Luma3DS + Homebrew Launcher).

```
sdmc:/3ds/phase-distorter-3ds.3dsx
sdmc:/3ds/phase-distorter/earthbound.ebpak   ← desde la versión de PC, o bien:
sdmc:/3ds/phase-distorter/TU_ROM.sfc         ← se importa sola la primera vez
sdmc:/3ds/dspfirm.cdc                         ← para el audio (dúmpealo con DSP1)
```

- La **SRAM** se guarda en `sdmc:/3ds/phase-distorter/earthbound.srm` (8 KiB, mismo
  formato que el `.srm` de PC: puedes copiar tu partida en ambos sentidos).
  Se escribe automáticamente cuando el juego guarda.
- Si la ROM tiene cabecera de 512 bytes (`.smc`) y no la reconoce, impórtala en la
  versión de PC (`--import-rom`) y copia el `.ebpak`.

## CIA y New 3DS

El `.cia` se arma con `platform/3ds/cia.rsf` (804 MHz, caché L2 y modo de 124 MB en
New 3DS). **No uses `-desc` en makerom:** el preset reemplaza esa configuración por
la de Old 3DS (268 MHz, sin L2). El `.3dsx` desde el Homebrew Launcher activa la
velocidad con `osSetSpeedupEnable`.

## Rendimiento

Con *Mostrar rendimiento* activo, la pantalla inferior muestra fps, ms por frame y el
desglose: **Juego** (lógica del CPU traducido), **Video** (PPU por scanline),
**Audio** (SPC700 + DSP) y **Pant** (copia a la pantalla). El audio se sincroniza de
forma perezosa (`GameSession::set_lazy_audio_sync`): mismo resultado bit a bit,
verificado por `cpp/tests/lazy_audio_sync_tests.cpp`.

## Controles

| 3DS | SNES |
|---|---|
| A / B / X / Y | A / B / X / Y |
| L / R | L / R |
| D-pad o Circle Pad | D-pad |
| START / SELECT | Start / Select |
| Táctil *Hablar* / *Mapa* | L / X |

## Estado y limitaciones

- **Verificado:** todo el núcleo compila para ARMv6K (CPU de la 3DS) sin errores ni
  advertencias y enlaza estático sin dependencias de escritorio; el frontend pasa la
  verificación contra los headers de libctru; la UI inferior y el blit superior tienen
  pruebas en el host (`tools/preview_ui.cpp`).
- **No verificado todavía en hardware.** El mayor riesgo es el **rendimiento**: el núcleo
  ejecuta el programa traducido del 65816, el PPU por software y el SPC700 + DSP. En PC
  tarda ~5,5 ms por frame; un ARM11 es bastante más lento. Apunta a **New 3DS**
  (804 MHz); en Old 3DS probablemente irá por debajo de 60 fps. La pantalla inferior
  muestra fps y ms por frame para medirlo.
- Si el juego corre bajo 60 fps, el audio tendrá cortes (no hay time-stretching aún).
- El modo panorámico cuesta más CPU; viene desactivado.
- Tamaño: ~19 MB de código (incluye EarthBound y Mother 2). Compilar una sola región
  reduciría casi a la mitad.

## Archivos

- `source/main.cpp` — arranque, carga/importación, bucle, entrada, SRAM.
- `source/canvas.hpp` — dibujo por software en framebuffers rotados BGR8 de la 3DS.
- `source/bottom_ui.*` — interfaz de la pantalla inferior.
- `source/audio_3ds.*` — salida de audio NDSP (32 kHz estéreo).
- `source/font_6x11.hpp` — fuente generada desde DejaVu Sans Mono (`tools/make_font.py`).
- Cambio en el núcleo: `GameSession::party_status()` (solo lectura de WRAM).
