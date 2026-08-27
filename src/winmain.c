/*
 * winmain.c — Windows entry point shim (MinGW builds only).
 *
 * Modern mingw-w64 CRTs ship a default main() (crtexewin) that simply
 * calls WinMain(). We therefore keep the game's own main() under its
 * SDL name (-Dmain=SDL_main) and provide WinMain here, forwarding to it.
 * This replaces libSDLmain, whose prebuilt archives are incompatible
 * with current mingw-w64 runtimes.
 */

#ifdef _WIN32

#include <windows.h>
#include <stdio.h>
#include <unistd.h>

int SDL_main(int argc, char *argv[]);

int WINAPI
WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
        LPSTR lpCmdLine, int nCmdShow)
{
  setvbuf(stdout, NULL, _IONBF, 0);
  if (dup2(2, 1) == -1) {}
  setvbuf(stderr, NULL, _IONBF, 0);
  return SDL_main(__argc, __argv);
}

#endif /* _WIN32 */
