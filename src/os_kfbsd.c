/*
** This file contains code that is specific to the FreeBSD Kernel.
*/

#include "sqliteInt.h"

#ifdef FREEBSD_KERNEL

/*
** Initialize the operating system interface.
*/
int sqlite3_os_init(void){
  /* TODO: Implement FreeBSD-specific initialization here. */
  return SQLITE_OK;
}

/*
** Deinitialize the operating system interface.
*/
int sqlite3_os_end(void){
  /* TODO: Implement FreeBSD-specific deinitialization here. */
  return SQLITE_OK;
}

#endif