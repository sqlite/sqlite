/*
** 2024-09-24
**
** The author disclaims copyright to this source code.  In place of
** a legal notice, here is a blessing:
**
**    May you do good and not evil.
**    May you find forgiveness for yourself and forgive others.
**    May you share freely, never taking more than you give.
**
*************************************************************************
**
** Implementation of standard I/O interfaces for UTF-8 that are missing
** on Windows.
*/
#ifdef _WIN32  /* This file is a no-op on all platforms except Windows */
#ifndef _SQLITE3_STDIO_H_
#include "sqlite3_stdio.h"
#endif
#undef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include "sqlite3.h"
#include <ctype.h>
#include <stdarg.h>
#include <io.h>
#include <fcntl.h>

/*
** If the SQLITE_U8TEXT_ONLY option is defined, then only use
** _O_U8TEXT, _O_WTEXT, and similar together with the UTF-16
** interfaces to the Windows CRT.  The use of ANSI-only routines
** like fputs() and ANSI modes like _O_TEXT and _O_BINARY is
** avoided.
**
** The downside of using SQLITE_U8TEXT_ONLY is that it becomes
** impossible to output a bare newline character (0x0a) - that is,
** a newline that is not preceded by a carriage return (0x0d).
** And without that capability, sometimes the output will be slightly
** incorrect, as extra 0x0d characters will have been inserted where
** they do not belong.
**
** The SQLITE_U8TEXT_STDIO compile-time option is a compromise.
** It always enables _O_WTEXT or similar for stdin, stdout, stderr,
** but allows other streams to be _O_TEXT and/or O_BINARY.  The
** SQLITE_U8TEXT_STDIO option has the same downside as SQLITE_U8TEXT_ONLY
** in that stray 0x0d characters might appear where they ought not, but
** at least with this option those characters only appear on standard
** I/O streams, and not on new streams that might be created by the
** application using sqlite3_fopen() or sqlite3_popen().
*/
#if defined(SQLITE_U8TEXT_ONLY)
# define UseWtextForOutput(fd) 1
# define UseWtextForInput(fd)  1
# define IsConsole(fd)         _isatty(_fileno(fd))
#elif defined(SQLITE_U8TEXT_STDIO)
# define UseWtextForOutput(fd) ((fd)==stdout || (fd)==stderr)
# define UseWtextForInput(fd)  ((fd)==stdin)
# define IsConsole(fd)         _isatty(_fileno(fd))
#else
# define UseWtextForOutput(fd) _isatty(_fileno(fd))
# define UseWtextForInput(fd)  _isatty(_fileno(fd))
# define IsConsole(fd)         1
#endif

/*
** Work-alike for the fopen() routine from the standard C library.
*/
FILE *sqlite3_fopen(const char *zFilename, const char *zMode){
  FILE *fp = 0;
  wchar_t *b1, *b2;
  int sz1, sz2;

  sz1 = (int)strlen(zFilename);
  sz2 = (int)strlen(zMode);
  b1 = malloc( (sz1+1)*sizeof(b1[0]) );
  b2 = malloc( (sz2+1)*sizeof(b1[0]) );
  if( b1 && b2 ){
    sz1 = MultiByteToWideChar(CP_UTF8, 0, zFilename, sz1, b1, sz1);
    b1[sz1] = 0;
    sz2 = MultiByteToWideChar(CP_UTF8, 0, zMode, sz2, b2, sz2);
    b2[sz2] = 0;
    fp = _wfopen(b1, b2);
  }
  free(b1);
  free(b2);
  return fp;
}


/*
** Work-alike for the popen() routine from the standard C library.
*/
FILE *sqlite3_popen(const char *zCommand, const char *zMode){
  FILE *fp = 0;
  wchar_t *b1, *b2;
  int sz1, sz2;

  sz1 = (int)strlen(zCommand);
  sz2 = (int)strlen(zMode);
  b1 = malloc( (sz1+1)*sizeof(b1[0]) );
  b2 = malloc( (sz2+1)*sizeof(b1[0]) );
  if( b1 && b2 ){
    sz1 = MultiByteToWideChar(CP_UTF8, 0, zCommand, sz1, b1, sz1);
    b1[sz1] = 0;
    sz2 = MultiByteToWideChar(CP_UTF8, 0, zMode, sz2, b2, sz2);
    b2[sz2] = 0;
    fp = _wpopen(b1, b2);
  }
  free(b1);
  free(b2);
  return fp;
}

/*
** Work-alike for fgets() from the standard C library.
*/
char *sqlite3_fgets(char *buf, int sz, FILE *in){
  if( UseWtextForInput(in) ){
    /* When reading from the command-prompt in Windows, it is necessary
    ** to use _O_WTEXT input mode to read UTF-16 characters, then translate
    ** that into UTF-8.  Otherwise, non-ASCII characters all get translated
    ** into '?'.
    */
    wchar_t *b1 = malloc( sz*sizeof(wchar_t) );
    if( b1==0 ) return 0;
    _setmode(_fileno(in), IsConsole(in) ? _O_WTEXT : _O_U8TEXT);
    if( fgetws(b1, sz/4, in)==0 ){
      sqlite3_free(b1);
      return 0;
    }
    WideCharToMultiByte(CP_UTF8, 0, b1, -1, buf, sz, 0, 0);
    sqlite3_free(b1);
    return buf;
  }else{
    /* Reading from a file or other input source, just read bytes without
    ** any translation. */
    return fgets(buf, sz, in);
  }
}

/*
** Work-alike for fputs() from the standard C library.
*/
int sqlite3_fputs(const char *z, FILE *out){
  if( UseWtextForOutput(out) ){
    /* When writing to the command-prompt in Windows, it is necessary
    ** to use _O_WTEXT input mode and write UTF-16 characters.
    */
    int sz = (int)strlen(z);
    wchar_t *b1 = malloc( (sz+1)*sizeof(wchar_t) );
    if( b1==0 ) return 0;
    sz = MultiByteToWideChar(CP_UTF8, 0, z, sz, b1, sz);
    b1[sz] = 0;
    _setmode(_fileno(out), _O_U8TEXT);
    fputws(b1, out);
    sqlite3_free(b1);
    return 0;
  }else{
    /* Writing to a file or other destination, just write bytes without
    ** any translation. */
    return fputs(z, out);
  }
}


/*
** Work-alike for fprintf() from the standard C library.
*/
int sqlite3_fprintf(FILE *out, const char *zFormat, ...){
  int rc;
  if( UseWtextForOutput(out) ){
    /* When writing to the command-prompt in Windows, it is necessary
    ** to use _O_WTEXT input mode and write UTF-16 characters.
    */
    char *z;
    va_list ap;

    va_start(ap, zFormat);
    z = sqlite3_vmprintf(zFormat, ap);
    va_end(ap);
    sqlite3_fputs(z, out);
    rc = (int)strlen(z);
    sqlite3_free(z);
  }else{
    /* Writing to a file or other destination, just write bytes without
    ** any translation. */
    va_list ap;
    va_start(ap, zFormat);
    rc = vfprintf(out, zFormat, ap);
    va_end(ap);
  }
  return rc;
}

/*
** Set the mode for an output stream.  mode argument is typically _O_BINARY or
** _O_TEXT.
*/
void sqlite3_fsetmode(FILE *fp, int mode){
  if( !UseWtextForOutput(fp) ){
    fflush(fp);
    _setmode(_fileno(fp), mode);
  }
}

#endif /* defined(_WIN32) */
