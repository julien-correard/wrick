# Chaîne de cross-compilation Windows pour xrick

Ce dossier contient la chaîne de cross-compilation (i686-w64-mingw32) utilisée par
`Makefile.win` pour produire `build/win/xrick.exe` à partir de Linux.

Contenu (résultat installé, prêt à l'emploi) :

- `SDL-1.2.15/`  — en-têtes + `libSDL.a` + `bin/SDL.dll` (SDL 1.2.15, MinGW)
- `zlib-1.3.1/`  — `zlib.h`/`zconf.h` + `libz.a` (zlib 1.3.1, MinGW)

## Utilisation

La Makefile pointe déjà vers ces chemins relatifs au repo. Pour compiler :

    make -f Makefile.win all

Livrable produit dans `build/win/` (xrick.exe, SDL.dll, data.zip).

## Reconstruire la chaîne

La `SDL.dll` installée est auto-suffisante (n'importe que les DLL système, pas
`libgcc_s`). Pour recompiler entièrement la chaîne depuis les sources (télécharge
SDL 1.2.15 et zlib 1.3.1) :

    ./build-toolchain.sh

Prérequis : paquet `gcc-mingw-w64-i686` (`i686-w64-mingw32-gcc`).
