/*
** 2008 October 07
**
** The author disclaims copyright to this source code.  In place of
** a legal notice, here is a blessing:
**
**    May you do good and not evil.
**    May you find forgiveness for yourself and forgive others.
**    May you share freely, never taking more than you give.
**
** 2024 May 21 gnn
*************************************************************************
** This file contains the C functions that implement mutexes in the
** FreeBSD kernel.
**
*/
#include "sqliteInt.h"

#ifdef SQLITE_MUTEX_FBSD
struct sx sqlite3_lock;
int sqlite3_lock_initialized = false;

static int fbsdMutexInit(void) {
	if (!sqlite3_lock_initialized) {
		sx_init_flags(&sqlite3_lock, "sqlite3 lock", SX_RECURSE);
		sqlite3_lock_initialized = true;
	}
	return SQLITE_OK;
}

static int fbsdMutexEnd(void) { 
	sx_destroy(&sqlite3_lock);
	sqlite3_lock_initialized = false;
	return SQLITE_OK;
}

static sqlite3_mutex *fbsdMutexAlloc(int id){ 
	UNUSED_PARAMETER(id);

	return ((sqlite3_mutex *)&sqlite3_lock); 
}
static void fbsdMutexFree(sqlite3_mutex *p) {
	UNUSED_PARAMETER(p);
}

static void fbsdMutexEnter(sqlite3_mutex *p){ 
	UNUSED_PARAMETER(p);

	sx_xlock(&sqlite3_lock);
}

static int fbsdMutexTry(sqlite3_mutex *p){
	UNUSED_PARAMETER(p);
  
	if (sx_try_xlock(&sqlite3_lock) == 0) {
		return SQLITE_BUSY;
	}

	return SQLITE_OK;
}
static void fbsdMutexLeave(sqlite3_mutex *p) {
	UNUSED_PARAMETER(p);

	sx_xunlock(&sqlite3_lock);
}

/*
** Try to provide a memory barrier operation, needed for initialization
** and also for the implementation of xShmBarrier in the VFS in cases
** where SQLite is compiled without mutexes.
*/
/* void sqlite3MemoryBarrier(void){ */
/* 	printf("memory barrier\n"); */
/* } */

sqlite3_mutex_methods const *sqlite3DefaultMutex(void){
  static const sqlite3_mutex_methods sMutex = {
    fbsdMutexInit,
    fbsdMutexEnd,
    fbsdMutexAlloc,
    fbsdMutexFree,
    fbsdMutexEnter,
    fbsdMutexTry,
    fbsdMutexLeave,

    0,
    0,
  };

  return &sMutex;
}
#endif /* defined(SQLITE_MUTEX_FBSD) */
