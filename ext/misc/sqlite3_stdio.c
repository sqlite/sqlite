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
  if( isatty(_fileno(in)) ){
    /* When reading from the command-prompt in Windows, it is necessary
    ** to use _O_WTEXT input mode to read UTF-16 characters, then translate
    ** that into UTF-8.  Otherwise, non-ASCII characters all get translated
    ** into '?'.
    */
    wchar_t *b1 = malloc( sz*sizeof(wchar_t) );
    if( b1==0 ) return 0;
    _setmode(_fileno(in), _O_WTEXT);
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
  if( isatty(_fileno(out)) ){
    /* When writing to the command-prompt in Windows, it is necessary
    ** to use _O_WTEXT input mode and write UTF-16 characters.
    */
    int sz = (int)strlen(z);
    wchar_t *b1 = malloc( (sz+1)*sizeof(wchar_t) );
    if( b1==0 ) return 0;
    sz = MultiByteToWideChar(CP_UTF8, 0, z, sz, b1, sz);
    b1[sz] = 0;
    _setmode(_fileno(out), _O_WTEXT);
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
  if( isatty(fileno(out)) ){
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
** Set the mode for a stream.  mode argument is typically _O_BINARY or
** _O_TEXT.
*/
void sqlite3_fsetmode(FILE *fp, int mode){
  fflush(fp);
  _setmode(_fileno(fp), mode);
}

#endif /* defined(_WIN32) */
