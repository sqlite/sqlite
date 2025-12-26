/*
** 2025-12-26
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
** This file implements a substitute process entry point for windows
** that correctly interprets command-line arguments as UTF-8.  This is
** a work-around for the (unfixed) Windows bug known as "WorstFit" and
** described at:
**
**   *  https://news.ycombinator.com/item?id=42647101
**
** USAGE:
**
** If you have a command-line program coded to C-language standard in which
** the entry point is:
**
**       int main(int argc, char **argv){...}
**
** That does not work on Windows if there are non-ASCII characters on the
** command line.  Programs are expected to use an alternative entry point:
**
**       int wmain(int wargc, wchar_t **wargv){...}
**
** This file implements _wmain() but then converts all of the wargv elements
** to UTF-8 and passes them off to utf8_main().
**
** So, a command-line tool that implements the standard entry point can be
** modified by adding the following lines just prior to main():
**
**      #ifdef _WIN32
**      #define main utf8_main
**      #endif
**
** Then, include this file in the list of files that implement the program,
** and the program will work correctly, even on Windows.
*/
#include <windows.h>
#include <stdio.h>

extern int utf8_main(int,char**);
int wmain(int argc, wchar_t **wargv){
  int rc, i;
  char **argv = malloc( sizeof(char*) * (argc+1) );
  char **orig = argv;
  if( argv==0 ){
    fprintf(stderr, "malloc failed\n");
    exit(1);
  }
  for(i=0; i<argc; i++){
    int nByte = WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, 0, 0, 0, 0);
    if( nByte==0 ){
      argv[i] = 0;
    }else{
      argv[i] = malloc( nByte );
      if( argv[i]==0 ){
        fprintf(stderr, "malloc failed\n");
        exit(1);
      }
      nByte = WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, argv[i],nByte,0,0);
      if( nByte==0 ){
        free(argv[i]);
        argv[i] = 0;
      }
    }
  }
  argv[argc] = 0;
  rc = utf8_main(argc, argv);
  for(i=0; i<argc; i++) free(orig[i]);
  return rc;
}
