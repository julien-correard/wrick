/*
 * CRT emulation through OpenGL, included by sysvid.c
 *
 * Single-pass fragment shader inspired by libretro CRT presets
 * (curvature, scanlines, aperture grille, halation, vignette).
 * All GL entry points are resolved at runtime via SDL_GL_GetProcAddress,
 * so nothing new is linked.
 */

#define GLT_TEXTURE_2D          0x0DE1
#define GLT_RGBA                0x1908
#define GLT_UNSIGNED_BYTE       0x1401
#define GLT_NEAREST             0x2600
#define GLT_QUADS               0x0007
#define GLT_PROJECTION          0x1701
#define GLT_MODELVIEW           0x1700
#define GLT_TEXTURE_WRAP_S      0x2802
#define GLT_TEXTURE_WRAP_T      0x2803
#define GLT_CLAMP_TO_EDGE       0x812F
#define GLT_TEXTURE_MIN_FILTER  0x2801
#define GLT_TEXTURE_MAG_FILTER  0x2800
#define GLT_FRAGMENT_SHADER     0x8B30
#define GLT_VERTEX_SHADER       0x8B31
#define GLT_COMPILE_STATUS      0x8B81
#define GLT_LINK_STATUS         0x8B82
#define GLT_COLOR_BUFFER_BIT    0x00004000
#define GLT_DEPTH_TEST          0x0B71
#define GLT_LIGHTING            0x0B50
#define GLT_VENDOR              0x1F00
#define GLT_RENDERER            0x1F01

static void crt_cleanup(void);
static void sysvid_buildStretch(void);
void sysvid_restorePalette(void);

#ifdef _WIN32
#define CRTAPIENTRY __stdcall
#else
#define CRTAPIENTRY
#endif

typedef int GLint_t;
typedef int GLsizei_t;
typedef float GLfloat_t;

typedef void (CRTAPIENTRY *pfn_glEnable_t)(unsigned);
typedef void (CRTAPIENTRY *pfn_glDisable_t)(unsigned);
typedef void (CRTAPIENTRY *pfn_glGenTextures_t)(int, unsigned *);
typedef void (CRTAPIENTRY *pfn_glBindTexture_t)(unsigned, unsigned);
typedef void (CRTAPIENTRY *pfn_glTexImage2D_t)(unsigned, int, int, int, int, int, unsigned, unsigned, const void *);
typedef void (CRTAPIENTRY *pfn_glTexSubImage2D_t)(unsigned, int, int, int, int, int, unsigned, unsigned, const void *);
typedef void (CRTAPIENTRY *pfn_glTexParameteri_t)(unsigned, unsigned, int);
typedef void (CRTAPIENTRY *pfn_glViewport_t)(int, int, int, int);
typedef void (CRTAPIENTRY *pfn_glClear_t)(unsigned);
typedef void (CRTAPIENTRY *pfn_glMatrixMode_t)(unsigned);
typedef void (CRTAPIENTRY *pfn_glLoadIdentity_t)(void);
typedef void (CRTAPIENTRY *pfn_glBegin_t)(unsigned);
typedef void (CRTAPIENTRY *pfn_glEnd_t)(void);
typedef void (CRTAPIENTRY *pfn_glTexCoord2f_t)(float, float);
typedef void (CRTAPIENTRY *pfn_glVertex2f_t)(float, float);
typedef const char *(CRTAPIENTRY *pfn_glGetString_t)(unsigned);
typedef unsigned (CRTAPIENTRY *pfn_glCreateShader_t)(unsigned);
typedef void (CRTAPIENTRY *pfn_glDeleteShader_t)(unsigned);
typedef void (CRTAPIENTRY *pfn_glShaderSource_t)(unsigned, int, const char **, const int *);
typedef void (CRTAPIENTRY *pfn_glCompileShader_t)(unsigned);
typedef void (CRTAPIENTRY *pfn_glGetShaderiv_t)(unsigned, unsigned, int *);
typedef void (CRTAPIENTRY *pfn_glGetShaderInfoLog_t)(unsigned, int, int *, char *);
typedef unsigned (CRTAPIENTRY *pfn_glCreateProgram_t)(void);
typedef void (CRTAPIENTRY *pfn_glDeleteProgram_t)(unsigned);
typedef void (CRTAPIENTRY *pfn_glAttachShader_t)(unsigned, unsigned);
typedef void (CRTAPIENTRY *pfn_glLinkProgram_t)(unsigned);
typedef void (CRTAPIENTRY *pfn_glGetProgramiv_t)(unsigned, unsigned, int *);
typedef void (CRTAPIENTRY *pfn_glUseProgram_t)(unsigned);

static pfn_glEnable_t glenable;
static pfn_glDisable_t gldisable;
static pfn_glGenTextures_t glgentextures;
static pfn_glBindTexture_t glbindtexture;
static pfn_glTexImage2D_t glteximage2d;
static pfn_glTexSubImage2D_t gltexsubimage2d;
static pfn_glTexParameteri_t gltexparameteri;
static pfn_glViewport_t glviewport;
static pfn_glClear_t glclear;
static pfn_glMatrixMode_t glmatrixmode;
static pfn_glLoadIdentity_t glloadidentity;
static pfn_glBegin_t glbegin;
static pfn_glEnd_t glend;
static pfn_glTexCoord2f_t gltexcoord2f;
static pfn_glVertex2f_t glvertex2f;
static pfn_glGetString_t glgetstring;
static pfn_glCreateShader_t glcreateshader;
static pfn_glDeleteShader_t gldeleteshader;
static pfn_glShaderSource_t glshadersource;
static pfn_glCompileShader_t glcompileshader;
static pfn_glGetShaderiv_t glgetshaderiv;
static pfn_glGetShaderInfoLog_t glgetshaderinfolog;
static pfn_glCreateProgram_t glcreateprogram;
static pfn_glDeleteProgram_t gldeleteprogram;
static pfn_glAttachShader_t glattachshader;
static pfn_glLinkProgram_t gllinkprogram;
static pfn_glGetProgramiv_t glgetprogramiv;
static pfn_glUseProgram_t gluseprogram;

static U8 crt_on = FALSE;
static unsigned crt_tex = 0, crt_prog = 0;
static U32 *crt_rgba = NULL;
static U8 crt_palrgb[768];

static const char crt_vsrc[] =
"void main()\n"
"{\n"
"  gl_TexCoord[0] = gl_MultiTexCoord0;\n"
"  gl_Position = ftransform();\n"
"}\n";

static const char crt_fsrc[] =
"#version 110\n"
"uniform sampler2D u_tex;\n"
"vec2 curve(vec2 uv)\n"
"{\n"
"  uv = uv * 2.0 - 1.0;\n"
"  vec2 o = abs(uv.yx) / vec2(17.0, 13.0);\n"
"  uv += uv * o * o;\n"
"  return uv * 0.5 + 0.5;\n"
"}\n"
"void main()\n"
"{\n"
"  vec2 tx = vec2(1.0 / 320.0, 1.0 / 200.0);\n"
"  vec2 uv = curve(gl_TexCoord[0].xy);\n"
"  if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {\n"
"    gl_FragColor = vec4(0.0, 0.0, 0.0, 1.0);\n"
"    return;\n"
"  }\n"
"  vec3 c  = texture2D(u_tex, uv).rgb;\n"
"  vec3 cl = texture2D(u_tex, uv - vec2(tx.x * 0.6, 0.0)).rgb;\n"
"  vec3 cr = texture2D(u_tex, uv + vec2(tx.x * 0.6, 0.0)).rgb;\n"
"  vec3 col = (c * 4.0 + cl + cr) * 0.1667;\n"
"  vec3 h = texture2D(u_tex, uv - vec2(tx.x * 2.0, 0.0)).rgb\n"
"         + texture2D(u_tex, uv + vec2(tx.x * 2.0, 0.0)).rgb;\n"
"  h *= h;\n"
"  col += h * 0.03;\n"
"  float sy = fract(uv.y * 200.0);\n"
"  float scan = 0.80 + 0.20 * pow(sin(sy * 3.14159265), 2.0);\n"
"  float m = mod(gl_FragCoord.x, 3.0);\n"
"  vec3 mask;\n"
"  if (m < 1.0)      mask = vec3(1.18, 0.86, 0.86);\n"
"  else if (m < 2.0) mask = vec3(0.86, 1.18, 0.86);\n"
"  else              mask = vec3(0.86, 0.86, 1.18);\n"
"  float v = pow(16.0 * uv.x * uv.y * (1.0 - uv.x) * (1.0 - uv.y), 0.30);\n"
"  v = mix(0.55, 1.0, v);\n"
"  col *= scan * mask * v * 1.22 * vec3(0.98, 0.99, 1.04);\n"
"  gl_FragColor = vec4(min(col, vec3(1.0)), 1.0);\n"
"}\n";

static U8
crt_loadprocs(void)
{
  struct { const char *n; void **p; } t[] = {
    { "glEnable",             (void **)&glenable },
    { "glDisable",            (void **)&gldisable },
    { "glGenTextures",        (void **)&glgentextures },
    { "glBindTexture",        (void **)&glbindtexture },
    { "glTexImage2D",         (void **)&glteximage2d },
    { "glTexSubImage2D",      (void **)&gltexsubimage2d },
    { "glTexParameteri",      (void **)&gltexparameteri },
    { "glViewport",           (void **)&glviewport },
    { "glClear",              (void **)&glclear },
    { "glMatrixMode",         (void **)&glmatrixmode },
    { "glLoadIdentity",       (void **)&glloadidentity },
    { "glBegin",              (void **)&glbegin },
    { "glEnd",                (void **)&glend },
    { "glTexCoord2f",         (void **)&gltexcoord2f },
    { "glVertex2f",           (void **)&glvertex2f },
    { "glGetString",          (void **)&glgetstring },
    { "glCreateShader",       (void **)&glcreateshader },
    { "glDeleteShader",       (void **)&gldeleteshader },
    { "glShaderSource",       (void **)&glshadersource },
    { "glCompileShader",      (void **)&glcompileshader },
    { "glGetShaderiv",        (void **)&glgetshaderiv },
    { "glGetShaderInfoLog",   (void **)&glgetshaderinfolog },
    { "glCreateProgram",      (void **)&glcreateprogram },
    { "glDeleteProgram",      (void **)&gldeleteprogram },
    { "glAttachShader",       (void **)&glattachshader },
    { "glLinkProgram",        (void **)&gllinkprogram },
    { "glGetProgramiv",       (void **)&glgetprogramiv },
    { "glUseProgram",         (void **)&gluseprogram },
  };
  U8 i;

  for (i = 0; i < sizeof(t) / sizeof(t[0]); i++) {
    *t[i].p = SDL_GL_GetProcAddress(t[i].n);
    if (!*t[i].p)
      return FALSE;
  }
  return TRUE;
}

static void
crt_printlog(unsigned sh)
{
  char buf[1024];
  int n = 0;

  glgetshaderinfolog(sh, sizeof(buf), &n, buf);
  if (n > 0)
    sys_printf("xrick/video: %s\n", buf);
}

static unsigned
crt_compile(unsigned type, const char *src)
{
  unsigned sh;
  int ok = 0;

  sh = glcreateshader(type);
  glshadersource(sh, 1, &src, NULL);
  glcompileshader(sh);
  glgetshaderiv(sh, GLT_COMPILE_STATUS, &ok);
  if (!ok) {
    sys_printf("xrick/video: shader compile failed\n");
    crt_printlog(sh);
    gldeleteshader(sh);
    return 0;
  }
  return sh;
}

static U8
crt_buildprogram(void)
{
  unsigned vs, fs;
  int ok = 0;

  vs = crt_compile(GLT_VERTEX_SHADER, crt_vsrc);
  fs = crt_compile(GLT_FRAGMENT_SHADER, crt_fsrc);
  if (!vs || !fs)
    return FALSE;
  crt_prog = glcreateprogram();
  glattachshader(crt_prog, vs);
  glattachshader(crt_prog, fs);
  gllinkprogram(crt_prog);
  glgetprogramiv(crt_prog, GLT_LINK_STATUS, &ok);
  gldeleteshader(vs);
  gldeleteshader(fs);
  if (!ok) {
    sys_printf("xrick/video: program link failed\n");
    return FALSE;
  }
  return TRUE;
}

static U16
crt_w(void)
{
  return (videoFlags & SDL_FULLSCREEN) ? fsw : (U16)(SYSVID_WIDTH * zoom);
}

static U16
crt_h(void)
{
  return (videoFlags & SDL_FULLSCREEN) ? fsh : (U16)(SYSVID_HEIGHT * zoom);
}

static U8
crt_setvideo(void)
{
  SDL_GL_SetAttribute((SDL_GLattr)4, 1); /* SDL_GL_DOUBLEBUFFER */
  screen = SDL_SetVideoMode(crt_w(), crt_h(), 24,
                            SDL_OPENGL | (videoFlags & SDL_FULLSCREEN));
  if (!screen)
    return FALSE;
  return TRUE;
}

static void
sw_reinit(void)
{
  if (videoFlags & SDL_FULLSCREEN) {
    sysvid_buildStretch();
    screen = initScreen(fsw, fsh, 8, videoFlags);
  }
  else {
    screen = initScreen(SYSVID_WIDTH * zoom,
                        SYSVID_HEIGHT * zoom,
                        8, videoFlags);
  }
  if (!screen)
    sys_panic("xrick/video: could not set video mode\n");
  sysvid_restorePalette();
}

static U8
crt_start(void)
{
  IFDEBUG_VIDEO(sys_printf("xrick/video: trying OpenGL for CRT\n"););

  if (!crt_setvideo()) {
    IFDEBUG_VIDEO(sys_printf("xrick/video: no GL context\n"););
    goto fail;
  }

  if (!crt_loadprocs())
    goto fail;

  IFDEBUG_VIDEO(
    sys_printf("xrick/video: GL %s / %s\n",
               glgetstring(GLT_VENDOR), glgetstring(0x1F01));
    );

  glviewport(0, 0, crt_w(), crt_h());
  glmatrixmode(GLT_PROJECTION);
  glloadidentity();
  glmatrixmode(GLT_MODELVIEW);
  glloadidentity();
  gldisable(GLT_DEPTH_TEST);
  gldisable(GLT_LIGHTING);

  glgentextures(1, &crt_tex);
  glbindtexture(GLT_TEXTURE_2D, crt_tex);
  gltexparameteri(GLT_TEXTURE_2D, GLT_TEXTURE_MIN_FILTER, GLT_NEAREST);
  gltexparameteri(GLT_TEXTURE_2D, GLT_TEXTURE_MAG_FILTER, GLT_NEAREST);
  gltexparameteri(GLT_TEXTURE_2D, GLT_TEXTURE_WRAP_S, GLT_CLAMP_TO_EDGE);
  gltexparameteri(GLT_TEXTURE_2D, GLT_TEXTURE_WRAP_T, GLT_CLAMP_TO_EDGE);
  {
    static U8 black[320 * 200 * 4];
    glteximage2d(GLT_TEXTURE_2D, 0, GLT_RGBA,
                 SYSVID_WIDTH, SYSVID_HEIGHT, 0,
                 GLT_RGBA, GLT_UNSIGNED_BYTE, black);
  }

  if (!crt_buildprogram()) {
    goto fail;
  }

  crt_rgba = malloc(SYSVID_WIDTH * SYSVID_HEIGHT * 4);
  if (!crt_rgba)
    goto fail;

  return TRUE;

fail:
  crt_cleanup();
  sw_reinit();
  return FALSE;
}

void
crt_cleanup(void)
{
  if (crt_prog && gldeleteprogram)
    gldeleteprogram(crt_prog);
  crt_prog = 0;
  crt_tex = 0;
  free(crt_rgba);
  crt_rgba = NULL;
}

static void
crt_draw(void)
{
  U8 *s = sysvid_fb;
  U8 *d = (U8 *)crt_rgba;
  U16 i, j;

  for (i = 0; i < SYSVID_HEIGHT; i++) {
    s = sysvid_fb + i * SYSVID_WIDTH;
    d = (U8 *)crt_rgba + i * SYSVID_WIDTH * 4;
    for (j = 0; j < SYSVID_WIDTH; j++) {
      U8 idx = s[j];
      d[0] = crt_palrgb[idx * 3];
      d[1] = crt_palrgb[idx * 3 + 1];
      d[2] = crt_palrgb[idx * 3 + 2];
      d[3] = 255;
      d += 4;
    }
  }

  glbindtexture(GLT_TEXTURE_2D, crt_tex);
  gltexsubimage2d(GLT_TEXTURE_2D, 0, 0, 0,
                  SYSVID_WIDTH, SYSVID_HEIGHT,
                  GLT_RGBA, GLT_UNSIGNED_BYTE, crt_rgba);

  glclear(GLT_COLOR_BUFFER_BIT);
  gluseprogram(crt_prog);
  glbegin(GLT_QUADS);
  gltexcoord2f(0.0, 1.0); glvertex2f(-1.0, -1.0);
  gltexcoord2f(1.0, 1.0); glvertex2f(1.0, -1.0);
  gltexcoord2f(1.0, 0.0); glvertex2f(1.0, 1.0);
  gltexcoord2f(0.0, 0.0); glvertex2f(-1.0, 1.0);
  glend();

  SDL_GL_SwapBuffers();
}

void
sysvid_toggleCrt(void)
{
  if (!crt_on) {
    if (crt_start()) {
      crt_on = TRUE;
      sys_printf("xrick/video: CRT actif (F12)\n");
      sysvid_update(&SCREENRECT);
    }
    else {
      sys_printf("xrick/video: CRT indisponible ici\n");
    }
  }
  else {
    crt_on = FALSE;
    crt_cleanup();
    sw_reinit();
    sys_printf("xrick/video: CRT desactive\n");
    sysvid_update(&SCREENRECT);
  }
}

/* eof */
