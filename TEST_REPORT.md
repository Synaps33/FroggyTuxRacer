# Test report — Tux Racer GB300 / SF2000

Data: 2026-10-26  
Cel: MIPS32 LE, brak FPU, 320×240 RGB565, GB300V2 / SF2000  
Uwaga: „Rux Racer” potwierdzone jako klasyczny **Tux Racer** (Sunspire, GPL-2.0).

## 1. Wersja i platforma

* Gra bazowa: **Tux Racer 0.61** + oryginalne dane 0.61.
* Port: własny renderer software, fizyka i UI na liczbach stałoprzecinkowych.
* Tux: oryginalna hierarchia 3D z `tux.tcl` (86 węzłów, 34 elipsoidy),
  pieczona do czterech pozycji (prosto, lewo, prawo, lot), renderowana
  jako siatka 3D (ok. 968 wierzchołków / 944 trójkąty na pozę).
* Brak FPU: kompilacja `-msoft-float`; w finalnym ELF:
  * `Tag_GNU_MIPS_ABI_FP: Soft float`,
  * `CPR1 size: 0`,
  * **0** instrukcji FPU (`add.s`, `mul.s`, `mtc1`, `lwc1`, `cvt.*` itd.).
* Ekran: natywne 320×240 RGB565, 60 Hz, libretro.
* Rdzenie: **202 456 B** (ok. 197,7 KiB) dla SF2000 i GB300V2.
* Assety: 2,9 MB; katalog `ROMS/tuxracer` na karcie: 4,7 MB.
* Teren: adaptacyjny LOD z pełną siatką 2 m w strefie bliskiej kamery (10 rzędów) oraz 4 m w dali, z uśrednionym doborem tekstury (lód/skała/śnieg).

## 2. Buildy

| Build | Kompilator / środowisko | Wynik |
|---|---|---|
| Host x86_64 | gcc 14, `-O2` | OK, 389 klatek w **3,35 s** (~8,6 ms/klatkę) |
| MIPS soft-float | musl `mipsel-linux-muslsf-gcc`, `-static -msoft-float` | OK, QEMU 389 klatek w **17,67 s** (~45 ms/klatkę) |
| QEMU soak | ten sam runner, 1812 klatek | **42,1 s** (~23 ms/klatkę) |
| SF2000 | `mips-mti-elf-gcc`, loader Multicore | OK, `dist/tuxracer.sf2k` |
| GB300V2 | `mips-mti-elf-gcc`, loader Multicore | OK, `dist/tuxracer.gb3k` |

QEMU nie jest benchmarkiem sprzętowym. Na realnym 918 MHz B210 wynik powinien
być w podobnym rzędzie; gra używa czasu rzeczywistego z `os_get_tick_count()`,
więc przy mniejszym FPS nie zwalnia fizyki.

## 3. Testy funkcjonalne

Runner przechodzi:

* ekran tytułowy i animowany śnieg,
* wybór trasy (5 tras, podglądy, opisy, par time, liczba śledzi; wybór działa
  też z pauzy przez pozycję COURSES),
* start wyścigu, odliczanie 3-2-1-GO,
* jazdę, skręcanie, skok, pauzę, powrót, wynik,
* powrót do wyboru trasy i do tytułu.

Dodatkowo:

* **wszystkie 5 tras** ładuje się i renderuje z poprawnym HUD-em,
* automatyczny przejazd (hold Up) kończy 4/5 tras:

| Trasa | Czas auto | Śledzie | Wynik |
|---|---:|---:|---|
| Bunny Hill | 27,37 s | 2 | meta |
| Twisty Slope | 32,00 s | 8 | meta |
| Bumpy Ride | 19,32 s | 12 | meta |
| Frozen River | — | 1 | auto w linii prostej zatrzymuje się na stromym brzegu; trasa wymaga skręcania (człowiek) |
| Path of Daggers | 33,57 s | 3 | meta |

* Zbieranie śledzi działa: po zebraniu ryba natychmiast znika z trasy
  (`item_taken` sprawdzane w rendererze); test hook zebrał 1 rybę,
  licznik HUD = 1, ryba nie jest już rysowana.
* Wybór drugiej trasy został sprawdzony: RIGHT + A uruchamia „Twisty Slope”
  (HUD i scena), a nie Bunny Hill.
* checkpoint/reset (`SELECT`) działa,
* audio: 69 450 niezerowych sampli w 389 klatkach,
* host i MIPS dają **bajtowo identyczne** wszystkie 14 zrzutów oraz identyczny
  strumień audio (SHA-256 32414c44...).

## 4. Analiza wydajności

Główne optymalizacje:

1. pełny fixed-point (Q16/Q8, tablice sin/cos, brak float w gorących pętlach),
2. teren w LOD 4 m z cullingiem komórek poza ekranem,
3. płaskie cieniowanie i mgła per trójkąt,
4. rasteryzacja z gradientami atrybutów i odwrotnością pola (jedno dzielenie
   na trójkąt zamiast kilkunastu),
5. Tux: oryginalny model 3D pieczony offline do 4 pozycji; jeden mesh na
   klatkę, płaski z-buffer per trójkąt, backface culling, bez dzielenia na
   trójkąt w gorącej pętli,
6. teksty/UI przez blit ze stałoprzecinkowym krokiem (bez dzielenia na piksel).

Wąskim gardłem pozostaje rasteryzacja terenu i UI. Ewentualne dalsze kroki:
LOD 8 m dla dalszych rzędów, cache napisów menu, ewentualne 256×192 + upscale.
Nie zmieniono 320×240, bo taki był wymóg.

## 5. Zrzuty ekranu

W `screenshots/` (14 klatek natywnych 320×240) oraz `screenshots/contact_sheet.png`:

| Plik | Scena |
|---|---|
| `tux_01_title.png` | ekran tytułowy |
| `tux_02_title_snow.png` | animacja śniegu |
| `tux_03_course_select.png` | wybór trasy |
| `tux_04_course_twisty.png` | druga trasa |
| `tux_05_countdown.png` | odliczanie |
| `tux_06_race_start.png` | start / brama START |
| `tux_07_race_steer.png` | jazda, Tux, śnieg, drzewa |
| `tux_08_jump.png` | skok |
| `tux_09_race_left.png` | skręt |
| `tux_10_pause.png` | pauza |
| `tux_11_near_finish.png` | brama FINISH |
| `tux_12_results.png` | wyniki |
| `tux_13_course_back.png` | powrót do wyboru trasy |
| `tux_14_title_back.png` | powrót do tytułu |

Dodatkowo:

* `screenshots/mips/` — te same klatki z MIPS/QEMU (identyczne bajtowo),
* `screenshots/courses/c_course_0..4.png` — start każdej z 5 tras,
* `screenshots/courses_contact.png` — zbiorczy arkusz.

## 6. Instalacja

Zainstalowano na karcie `/media/Sajnaps/GB300`:

```
ROMS/TuxRacer.gba
ROMS/tuxracer/                (assety + kopia rdzenia)
system/Deimos/cores/tuxracer.gb3k
system/Deimos/cores/tuxracer.sf2k
cores/tuxracer/core_87000000
configs/custom_cores/tuxracer/games.opt
```

SHA-256 rdzenia GB300: `8d8d4acebc8e4f0c5bfd655fd0eba502acb49c5f171ba1cd83ce1e31a5198b91`.
SHA-256 rdzenia SF2000: `e3849acff3d6328fd26db6934a881f7c066aa0af3965bea455d4a5201d8e5b8f`.

Nie ruszano `bios/bisrv.asd`.

## 7. Znane ograniczenia

* Brak muzyki IT (tylko SFX WAV); można dodać później przez konwersję modułów.
* Brak save state'ów libretro; najlepsze czasy zapisują się w `tuxracer.sav`.
* Ścieżka „Frozen River” w trybie auto-testu wymaga skręcania; nie jest to
  błąd ładowania — to stromy brzeg/rock.
* Nie testowano na fizycznym egzemplarzu GB300/SF2000 — tylko runner hosta,
  QEMU MIPS soft-float i poprawny link loaderów. Instalacja na SD gotowa.
* Tux jest modelem 3D z oryginalnej hierarchii, ale animacja jest ograniczona
  do czterech pieczonych pozycji (bez płynnej interpolacji stawów).
