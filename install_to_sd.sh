#!/usr/bin/env bash
set -euo pipefail

PROJECT="$(cd "$(dirname "$0")" && pwd)"
DIST="$PROJECT/dist"
SD="${1:-}"

if [ -z "$SD" ]; then
    for p in /media/Sajnaps/GB300 /media/Sajnaps/SF2000 /media/* /mnt/*; do
        if [ -d "$p/system/Deimos/cores" ] || [ -d "$p/cores" ]; then
            SD="$p"; break
        fi
    done
fi
if [ -z "$SD" ] || [ ! -d "$SD" ]; then
    echo "Nie wykryto karty SD. Uzycie: $0 /sciezka/do/karty"
    exit 1
fi

IS_GB300=0
if [ "${FORCE_GB300:-0}" = "1" ] || [[ "$SD" =~ GB300 ]] || \
   [ -f "$SD/cores/sonicmania.gb3k" ] || [ -f "$SD/system/Deimos/cores/sonicmania.gb3k" ]; then
    IS_GB300=1
fi

if [ "$IS_GB300" -eq 1 ]; then
    CORE="$DIST/tuxracer.gb3k"
    EXT=gb3k
    echo "Wykryto GB300 / GB300V2: $SD"
else
    CORE="$DIST/tuxracer.sf2k"
    EXT=sf2k
    echo "Wykryto Data Frog SF2000: $SD"
fi

if [ ! -f "$CORE" ]; then
    echo "Brak $CORE - uruchom najpierw: make device"
    exit 1
fi
if [ ! -f "$DIST/res/TuxRacer.rgb565" ]; then
    python3 "$PROJECT/tools/package.py"
fi

mkdir -p "$SD/system/Deimos/cores" "$SD/cores/tuxracer" \
         "$SD/ROMS/tuxracer/.res" "$SD/ROMS/.res" \
         "$SD/configs/custom_cores/tuxracer"

# The frontend opens <core>.sf2k on both devices; keep both spellings.
cp -f "$CORE" "$SD/system/Deimos/cores/tuxracer.$EXT"
cp -f "$CORE" "$SD/system/Deimos/cores/tuxracer.sf2k"
cp -f "$CORE" "$SD/cores/tuxracer/core_87000000"
cp -f "$CORE" "$SD/ROMS/tuxracer/core_87000000"
cp -f "$CORE" "$SD/ROMS/tuxracer/tuxracer.$EXT"
cp -f "$CORE" "$SD/ROMS/tuxracer/tuxracer.sf2k"

echo "Kopiowanie danych (4 MB)..."
rm -f "$SD/ROMS/tuxracer/tex/tux.tex"
cp -a "$PROJECT/assets/." "$SD/ROMS/tuxracer/"

STUB='tuxracer;tuxracer;TuxRacer.gba'
printf '%s' "$STUB" > "$SD/ROMS/TuxRacer.gba"
printf '%s' "$STUB" > "$SD/ROMS/tuxracer/TuxRacer.gba"
printf '%s' "$STUB" > "$SD/ROMS/gba/TuxRacer.gba" 2>/dev/null || true

cp -f "$DIST/res/TuxRacer.png" "$SD/ROMS/tuxracer/.res/TuxRacer.png"
cp -f "$DIST/res/TuxRacer.rgb565" "$SD/ROMS/tuxracer/.res/TuxRacer.rgb565"
cp -f "$DIST/res/TuxRacer.rgb565" "$SD/ROMS/tuxracer/TuxRacer.rgb565"
cp -f "$DIST/res/TuxRacer.rgb565" "$SD/ROMS/TuxRacer.rgb565"
cp -f "$DIST/res/TuxRacer.rgb565" "$SD/ROMS/.res/TuxRacer.rgb565"

cat > "$SD/configs/custom_cores/tuxracer/games.opt" <<'GAMES'
TuxRacer.gba|tuxracer
TuxRacer|tuxracer
tuxracer.gb3k|tuxracer
tuxracer.sf2k|tuxracer
GAMES

if [ "${INSTALL_BIOS:-0}" = "1" ]; then
    BIOS="$DIST/bisrv_gb300.asd"
    [ "$IS_GB300" -eq 0 ] && BIOS="$DIST/bisrv_sf2000.asd"
    if [ -f "$BIOS" ]; then
        cp -f "$BIOS" "$SD/bios/bisrv.asd"
        echo "Zaktualizowano bios/bisrv.asd"
    fi
fi

sync
echo "=========================================================="
echo " Tux Racer zainstalowany."
echo " Uruchom z listy GBA: ROMS/TuxRacer.gba"
echo " Dane gry:           ROMS/tuxracer/"
echo " Rdzen:              system/Deimos/cores/tuxracer.$EXT"
echo "=========================================================="
