/*
** After SQLITE_FCNTL_NULL_IO has closed the database file descriptor, the
** descriptor number must not be parked on the pending-close list when the
** connection closes while another connection holds a lock.  Otherwise the
** number, which the process may meanwhile reuse, is closed a second time.
** C is needed because only the C interface can issue the file control and
** open an unrelated file descriptor.  Unix only.
** Bug report https://sqlite.org/bugs/info/af7f95a83f3be11d
*/
#include <stdio.h>
#include "sqlite3.h"
#if !defined(_WIN32)
# include <fcntl.h>
# include <unistd.h>
#endif

int main(void){
#if !defined(_WIN32)
  sqlite3 *dbA, *dbB;
  sqlite3_stmt *pStmt;
  int fd;
  int nErr = 0;

  sqlite3_initialize();
  unlink("unix-nullio1.db");
  sqlite3_open("unix-nullio1.db", &dbB);
  sqlite3_exec(dbB, "CREATE TABLE t1(x); INSERT INTO t1 VALUES(1);", 0, 0, 0);

  /* Connection dbB holds a read lock on the database file. */
  sqlite3_prepare_v2(dbB, "SELECT x FROM t1", -1, &pStmt, 0);
  sqlite3_step(pStmt);

  /* Connection dbA opens the file, drops its descriptor, and closes. */
  sqlite3_open("unix-nullio1.db", &dbA);
  sqlite3_exec(dbA, "SELECT x FROM t1", 0, 0, 0);
  sqlite3_file_control(dbA, "main", SQLITE_FCNTL_NULL_IO, 0);
  sqlite3_close(dbA);

  /* The lowest free descriptor is the one that dbA gave up. */
  fd = open("unix-nullio1.db", O_RDONLY);

  /* Releasing the last lock closes any pending descriptors. */
  sqlite3_finalize(pStmt);
  sqlite3_close(dbB);

  if( fcntl(fd, F_GETFD)<0 ){
    printf("descriptor %d was closed by SQLite\n", fd);
    nErr++;
  }else{
    close(fd);
  }
  unlink("unix-nullio1.db");
  return nErr ? 1 : 0;
#else
  return 0;
#endif
}
