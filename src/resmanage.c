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
  FRK_Indirect = 1<<15,
  FRK_Malloc = 0, FRK_DbConn, FRK_DbStmt, FRK_SqStr,
  FRK_DbMem, FRK_File,
#if (!defined(_WIN32) && !defined(WIN32)) || !SQLITE_OS_WINRT
  FRK_Pipe,
#endif
#ifdef SHELL_MANAGE_TEXT
  FRK_Text,
#endif
  FRK_AnyRef, FRK_VdtorRef,
  FRK_CountOf
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
#ifdef SHELL_MANAGE_TEXT
    ShellText *p_text;
#endif
    sqlite3_str *p_sqst;
    AnyResourceHolder *p_any_rh;
    VirtualDtorNthObject *p_vdfo;
  } held;
  unsigned short frk; /* a FreeableResourceKind value */
  unsigned short offset;
} ResourceHeld;

/* The held-resource stack. This is for single-threaded use only. */
static ResourceHeld *pResHold = 0;
static ResourceCount numResHold = 0;
static ResourceCount numResAlloc = 0;

const char *resmanage_oom_message = "out of memory, aborting";

/* Info recorded in support of quit_moan(...) and stack-ripping */
static RipStackDest *pRipStack = 0;

/* Current position of the held-resource stack */
ResourceMark holder_mark(){
  return numResHold;
}

/* Strip resource stack then strip call stack (or exit.) */
void quit_moan(const char *zMoan, int errCode){
  RipStackDest *pRSD = pRipStack;
  int nFreed;
  if( zMoan ){
    fprintf(stderr, "Error: Terminating due to %s.\n", zMoan);
  }
  fprintf(stderr, "Auto-freed %d resources.\n",
          release_holders_mark( (pRSD)? pRSD->resDest : 0 ));
  pRipStack = (pRSD)? pRSD->pPrev : 0;
#ifndef SHELL_OMIT_LONGJMP
  if( pRSD!=0 ){
    pRSD->pPrev = 0;
    longjmp(pRSD->exeDest, errCode);
  } else
#endif
    exit(errCode);
}

/* Free a single resource item. (ignorant of stack) */
static int free_rk( ResourceHeld *pRH ){
  int rv = 0;
  if( pRH->held.p_any == 0 ) return 0;
  if( pRH->frk & FRK_Indirect ){
    void **ppv = (void**)pRH->held.p_any;
    pRH->held.p_any = *ppv;
    *ppv = (void*)0;
    if( pRH->held.p_any == 0 ) return 0;
    pRH->frk &= ~FRK_Indirect;
  }
  ++rv;
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
#ifdef SHELL_MANAGE_TEXT
  case FRK_Text:
    freeText(pRH->held.p_text);
    break;
#endif
  case FRK_SqStr:
    {
      char *z = sqlite3_str_finish(pRH->held.p_sqst);
      if( z!=0 ) sqlite3_free(z);
    }
    break;
  case FRK_AnyRef:
    (pRH->held.p_any_rh->its_freer)(pRH->held.p_any_rh->pAny);
    break;
  case FRK_VdtorRef:
    {
      VirtualDtorNthObject *po = pRH->held.p_vdfo;
      (po->p_its_freer[pRH->offset])(po);
    }
    break;
  default:
    assert(pRH->frk < FRK_CountOf);
  }
  pRH->held.p_any = 0;
  return rv;
}

/* Take back a held resource pointer, leaving held as NULL. (no-op) */
void* take_held(ResourceMark mark, ResourceCount offset){
  return swap_held(mark,offset,0);
}

/* Swap a held resource pointer for a new one. */
void* swap_held(ResourceMark mark, ResourceCount offset, void *pNew){
  ResourceCount rix = mark + offset;
  assert(rix < numResHold && numResHold > 0);
  if( rix < numResHold && numResHold > 0 ){
    void *rv = pResHold[rix].held.p_any;
    pResHold[rix].held.p_any = pNew;
    return rv;
  }else return 0;
}

/* Reserve room for at least count additional holders, with no chance
** of an OOM abrupt exit. This is used internally only by this package.
** The return is either 1 for success, or 0 indicating an OOM failure.
 */
static int more_holders_try(ResourceCount count){
  ResourceHeld *prh;
  ResourceCount newAlloc = numResHold + count;
  if( newAlloc < numResHold ) return 0; /* Overflow, sorry. */
  if( newAlloc <= numResAlloc ) return 1;
  prh = (ResourceHeld*)realloc(pResHold, newAlloc*sizeof(ResourceHeld));
  if( prh == 0 ) return 0;
  pResHold = prh;
  numResAlloc = newAlloc;
  return 1;
}

/* Lose one or more holders. */
void* drop_holder(void){
  assert(numResHold>0);
  if( numResHold>0 ) return pResHold[--numResHold].held.p_any;
  return 0;
}
void drop_holders(ResourceCount num){
  assert(numResHold>=num);
  if( numResHold>=num ) numResHold -= num;
  else numResHold = 0;
}

/* Drop one or more holders while freeing their holdees. */
void release_holder(void){
  assert(numResHold>0);
  free_rk(&pResHold[--numResHold]);
}
void release_holders(ResourceCount num){
  assert(num<=numResHold);
  while( num>0 ){
    free_rk(&pResHold[--numResHold]);
    --num;
  }
}

/* Shared resource-stack pushing code */
static void res_hold(void *pv, FreeableResourceKind frk){
  ResourceHeld rh = { pv, frk, 0 };
  if( numResHold == numResAlloc ){
    ResourceCount nrn = (ResourceCount)((numResAlloc>>2) + 5);
    if( !more_holders_try(nrn) ){
      free_rk(&rh);
      quit_moan(resmanage_oom_message,1);
    }
  }
  pResHold[numResHold++] = rh;
}

/* Hold a NULL (Assure swap_held() cannot fail.) */
void more_holders(ResourceCount more){
  if( !more_holders_try(more) ) quit_moan(resmanage_oom_message,1);
}

/* Hold anything in the malloc() heap */
void* mmem_holder(void *pm){
  res_hold(pm, FRK_Malloc);
  return pm;
}
/* Hold a C string in the malloc() heap */
char* mstr_holder(char *z){
  res_hold(z, FRK_Malloc);
  return z;
}
/* Hold a SQLite dynamic string */
sqlite3_str * sqst_holder(sqlite3_str *psqst){
  res_hold(psqst, FRK_SqStr);
  return psqst;
}
/* Hold a C string in the SQLite heap */
char* sstr_holder(char *z){
  res_hold(z, FRK_DbMem);
  return z;
}
/* Hold a SQLite dynamic string, reference to */
void sqst_ptr_holder(sqlite3_str **ppsqst){
  assert(ppsqst!=0);
  res_hold(ppsqst, FRK_SqStr|FRK_Indirect);
}
/* Hold a C string in the SQLite heap, reference to */
void sstr_ptr_holder(char **pz){
  assert(pz!=0);
  res_hold(pz, FRK_DbMem|FRK_Indirect);
}
/* Hold an open C runtime FILE */
void file_holder(FILE *pf){
  res_hold(pf, FRK_File);
}
#if (!defined(_WIN32) && !defined(WIN32)) || !SQLITE_OS_WINRT
/* Hold an open C runtime pipe */
void pipe_holder(FILE *pp){
  res_hold(pp, FRK_Pipe);
}
#endif
#ifdef SHELL_MANAGE_TEXT
/* a ShellText object, reference to (storage for which not managed) */
static void text_ref_holder(ShellText *pt){
  res_hold(pt, FRK_Text);
}
#endif

/* Hold some SQLite-allocated memory */
void* smem_holder(void *pm){
  res_hold(pm, FRK_DbMem);
  return pm;
}
/* Hold a SQLite database "connection" */
void conn_holder(sqlite3 *pdb){
  res_hold(pdb, FRK_DbConn);
}
/* Hold a SQLite prepared statement */
void stmt_holder(sqlite3_stmt *pstmt){
  res_hold(pstmt, FRK_DbStmt);
}
/* Hold a SQLite prepared statement, reference to */
void stmt_ptr_holder(sqlite3_stmt **ppstmt){
  assert(ppstmt!=0);
  res_hold(ppstmt, FRK_DbStmt|FRK_Indirect);
}
/* Hold a SQLite database ("connection"), reference to */
void conn_ptr_holder(sqlite3 **ppdb){
  assert(ppdb!=0);
  res_hold(ppdb, FRK_DbConn|FRK_Indirect);
}
/* Hold a reference to an AnyResourceHolder (in stack frame) */
void any_ref_holder(AnyResourceHolder *parh){
  assert(parh!=0 && parh->its_freer!=0);
  res_hold(parh, FRK_AnyRef);
}
/* Hold a reference to a VirtualDtorNthObject (in stack frame) */
void dtor_ref_holder(VirtualDtorNthObject *pvdfo, unsigned char n){
  ResourceHeld rh = { (void*)pvdfo, FRK_VdtorRef, n };
  assert(pvdfo!=0 && pvdfo->p_its_freer!=0 && *(pvdfo->p_its_freer)!=0);
  if( numResHold == numResAlloc ){
    ResourceCount nrn = (ResourceCount)((numResAlloc>>2) + 5);
    if( !more_holders_try(nrn) ){
      free_rk(&rh);
      quit_moan(resmanage_oom_message,1);
    }
  }
  pResHold[numResHold++] = rh;
}

/* Free all held resources in excess of given resource stack mark,
** then return how many needed freeing. */
int release_holders_mark(ResourceMark mark){
  int rv = 0;
  while( numResHold > mark ){
    rv += free_rk(&pResHold[--numResHold]);
  }
  if( mark==0 ){
    if( numResAlloc>0 ){
      free(pResHold);
      pResHold = 0;
      numResAlloc = 0;
    }
  }
  return rv;
}

/* Record a resource stack and call stack rip-to position */
void register_exit_ripper(RipStackDest *pRSD){
  assert(pRSD!=0);
  pRSD->pPrev = pRipStack;
  pRSD->resDest = holder_mark();
  pRipStack = pRSD;
}
/* Undo register_exit_ripper effect, back to previous state. */
void forget_exit_ripper(RipStackDest *pRSD){
  if( pRSD==0 ) pRipStack = 0;
  else{
    pRipStack = pRSD->pPrev;
    pRSD->pPrev = 0;
  }
}
