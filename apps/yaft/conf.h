/* See LICENSE for licence details. */
/* conf.h: define custom variables */

/* color: index number of color_palette[] (see color.h) */
enum {
  DEFAULT_FG = 7,
  DEFAULT_BG = 0,
  ACTIVE_CURSOR_COLOR = 2,
  PASSIVE_CURSOR_COLOR = 1,
};

/* misc */
enum {
  /* write dump of input to stdout, debug message to stderr */
  VERBOSE = true,
  /* hardware tabstop */
  TABSTOP = 8,
  /* don't draw when input data size is larger than BUFSIZE */
  LAZY_DRAW = true,
  /* always draw even if vt is not active */
  BACKGROUND_DRAW = false,
  /* handle vt switching */
  VT_CONTROL = false,
  /* force KD_TEXT mode (not use KD_GRAPHICS mode) */
  FORCE_TEXT_MODE = false,
  /* used for missing glyph(single width): U+0020 (SPACE) */
  SUBSTITUTE_HALF = 0x0020,
  /* used for missing glyph(double width): U+3000 (IDEOGRAPHIC SPACE) */
  SUBSTITUTE_WIDE = 0x003F,
  /* used for malformed UTF-8 sequence   : U+003F (QUESTION MARK) */
  REPLACEMENT_CHAR = 0x003F,

  REMARKABLE = true
};

/* TERM value */
static const char* term_name = "yaft-256color";

/* framubuffer device */
#if defined(__linux__)
static const char* fb_path = "/dev/fb0";
#elif defined(__FreeBSD__)
static const char* fb_path = "/dev/ttyv0";
#elif defined(__NetBSD__)
static const char* fb_path = "/dev/ttyE0";
#elif defined(__OpenBSD__)
static const char* fb_path = "/dev/ttyC0";
#elif defined(__ANDROID__)
static const char* fb_path = "/dev/graphics/fb0";
#endif

/* shell */
#if defined(__linux__) || defined(__MACH__)
static const char* shell_cmd = "/bin/bash";
#elif defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
static const char* shell_cmd = "/bin/csh";
#elif defined(__ANDROID__)
static const char* shell_cmd = "/system/bin/sh";
#endif
