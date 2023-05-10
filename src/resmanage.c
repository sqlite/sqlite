/*
** 2023 May 8
**
** The author disclaims copyright to this source code.  In place of
** a legal notice, here is a blessing:
**
**    May you do good and not evil.
**    May you find forgiveness for yourself and forgive others.
**    May you share freely, never taking more than you give.
**
*************************************************************************
** This file implements a simple resource management package, interface
** and function of which is described in resmanage.h .
*/

#include <stdlib.h>
#include <stdio.h>

#include <assert.h>
#include "resmanage.h"

/* Track how to free various held resources. */
typedef enum FreeableResourceKind {
  FRK_Malloc = 0, FRK_DbConn = 1, FRK_DbStmt = 2,
  FRK_DbMem = 3, FRK_File = 4,
#if (!defined(_WIN32) && !defined(WIN32)) || !SQLITE_OS_WINRT
  FRK_Pipe,
#endif
  FRK_CustomBase /* series of values for custom freers */
} FreeableResourceKind;

#if defined(_WIN32) || defined(WIN32)
# if !SQLITE_OS_WINRT
#  include <io.h>
#  include <fcntl.h>
#  undef pclose
#  define pclose _pclose
# endif
#else
extern int pclose(FILE*);
#endif

typedef struct ResourceHeld {
  union {
    void *p_any;
    void *p_malloced;
    sqlite3 *p_conn;
    sqlite3_stmt *p_stmt;
    void *p_s3mem;
    FILE *p_stream;
  } held;
  FreeableResourceKind frk;
} ResourceHeld;

/* The held-resource stack. This is for single-threaded use only. */
static ResourceHeld *pResHold = 0;
static unsigned short numResHold = 0;
static unsigned short numResAlloc = 0;

/* A small set of custom freers. It is linearly searched, used for
** layered heap-allocated (and other-allocated) data structures, so
** tends to have use limited to where slow things are happening.
*/
static unsigned short numCustom = 0; /* number of the set */
static unsigned short numCustomAlloc = 0; /* allocated space */
typedef void (*FreerFunction)(void *);
static FreerFunction *aCustomFreers = 0; /* content of set */

/* Info recorded in support of quit_moan(...) and stack-ripping */
static ResourceMark exit_mark = 0;
#ifndef SHELL_OMIT_LONGJMP
static jmp_buf *p_exit_ripper = 0;
#endif

/* Implementation per header comment */
ResourceMark holder_mark(){
  return numResHold;
}

/* Implementation per header comment */
void quit_moan(const char *zMoan, int errCode){
  if( zMoan ){
    fprintf(stderr, "Quitting due to %s, freeing %d resources.\n",
            zMoan, numResHold);
  }
  holder_free(exit_mark);
#ifndef SHELL_OMIT_LONGJMP
  if( p_exit_ripper!=0 ){
    longjmp(*p_exit_ripper, errCode);
  } else
#endif
    exit(errCode);
}

/* Free a single resource item. (ignorant of stack) */
static void free_rk( ResourceHeld *pRH ){
  if( pRH->held.p_any == 0 ) return;
  switch( pRH->frk ){
  case FRK_Malloc:
    free(pRH->held.p_malloced);
    break;
  case FRK_DbConn:
    sqlite3_close_v2(pRH->held.p_conn);
    break;
  case FRK_DbStmt:
    sqlite3_clear_bindings(pRH->held.p_stmt);
    sqlite3_finalize(pRH->held.p_stmt);
    break;
  case FRK_DbMem:
    sqlite3_free(pRH->held.p_s3mem);
    break;
  case FRK_File:
    fclose(pRH->held.p_stream);
    break;
#if (!defined(_WIN32) && !defined(WIN32)) || !SQLITE_OS_WINRT
  case FRK_Pipe:
    pclose(pRH->held.p_stream);
    break;
#endif
  default:
    {
      int ck = pRH->frk - FRK_CustomBase;
      assert(ck>=0);
      if( ck < numCustom ){
        aCustomFreers[ck]( pRH->held.p_any );
      }
    }
  }
  pRH->held.p_any = 0;
}

void* take_held(ResourceMark mark, unsigned short offset){
  unsigned short rix = mark + offset;
  if( rix < numResHold && numResHold > 0 ){
    void *rv = pResHold[rix].held.p_any;
    pResHold[rix].held.p_any = 0;
    return rv;
  }else return 0;
}

/* Shared resource-stack pushing code */
static void res_hold(void *pv, FreeableResourceKind frk){
  ResourceHeld rh = { pv, frk };
  if( numResHold == numResAlloc ){
    size_t nrn = numResAlloc + (numResAlloc>>2) + 5;
    ResourceHeld *prh;
    prh = (ResourceHeld*)realloc(pResHold, nrn*sizeof(ResourceHeld));
    if( prh!=0 ){
      pResHold = prh;
      numResAlloc = nrn;
    }else{
      quit_moan("Out of memory",1);
    }
  }
  pResHold[numResHold++] = rh;
}

/* Implementation per header comment */
void* mmem_holder(void *pm){
  res_hold(pm, FRK_Malloc);
  return pm;
}
/* Implementation per header comment */
char* mstr_holder(char *z){
  if( z!=0 ) res_hold(z, FRK_Malloc);
  return z;
}
/* Implementation per header comment */
char* sstr_holder(char *z){
  if( z!=0 ) res_hold(z, FRK_DbMem);
  return z;
}
/* Implementation per header comment */
void file_holder(FILE *pf){
  if( pf!=0 ) res_hold(pf, FRK_File);
}
#if (!defined(_WIN32) && !defined(WIN32)) || !SQLITE_OS_WINRT
/* Implementation per header comment */
void pipe_holder(FILE *pp){
  if( pp!=0 ) res_hold(pp, FRK_Pipe);
}
#endif
/* Implementation per header comment */
void* any_holder(void *pm, void (*its_freer)(void*)){
  int i = 0;
  while( i < numCustom ){
    if( its_freer == aCustomFreers[i] ) break;
    ++i;
  }
  if( i == numCustom ){
    size_t ncf = numCustom + 2;
    FreerFunction *pcf;
    pcf = (FreerFunction *)realloc(aCustomFreers, ncf*sizeof(FreerFunction));
    if( pcf!=0 ){
      numCustomAlloc = ncf;
      aCustomFreers = pcf;
      aCustomFreers[numCustom++] = its_freer;
    }else{
      quit_moan("Out of memory",1);
    }
  }
  res_hold(pm, i + FRK_CustomBase);
  return pm;
}
/* Implementation per header comment */
void* smem_holder(void *pm){
  res_hold(pm, FRK_DbMem);
  return pm;
}
/* Implementation per header comment */
void conn_holder(sqlite3 *pdb){
  res_hold(pdb, FRK_DbConn);
}
/* Implementation per header comment */
void stmt_holder(sqlite3_stmt *pstmt){
  res_hold(pstmt, FRK_DbStmt);
}

/* Implementation per header comment */
void holder_free(ResourceMark mark){
  while( numResHold > mark ){
    free_rk(&pResHold[--numResHold]);
  }
  if( mark==0 ){
    if( numResAlloc>0 ){
      free(pResHold);
      pResHold = 0;
      numResAlloc = 0;
    }
    if( numCustomAlloc>0 ){
      free(aCustomFreers);
      aCustomFreers = 0;
      numCustom = 0;
      numCustomAlloc = 0;
    }
  }
}

#ifndef SHELL_OMIT_LONGJMP
/* Implementation per header comment */
void register_exit_ripper(jmp_buf *pjb, ResourceMark rip_mark){
  exit_mark = rip_mark;
  p_exit_ripper = pjb;
}
/* Implementation per header comment */
void forget_exit_ripper(jmp_buf *pjb){
  exit_mark = 0;
  assert(p_exit_ripper == pjb);
  p_exit_ripper = 0;
}
#endif
