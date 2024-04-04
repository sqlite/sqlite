/*
** This file contains code that is specific to the FreeBSD Kernel.
*/

#include "sqliteInt.h"

#ifdef FREEBSD_KERNEL

#include "os_kfbsd.h"

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

/*
** Specific code for Kernel VFS implementation.
*/
static int kern_vfs_open(sqlite3_vfs *vfs, sqlite3_filename zName, sqlite3_file *file,
                         int flags, int *pOutFlags) {
    struct thread *td;
    struct file *fp;
    int error;
    int fd;
    int oflags = 0;

    // translate SQLite flags to kernel open flags.
    if (flags & SQLITE_OPEN_READWRITE) {
        oflags |= O_RDWR;
    } else if (flags & SQLITE_OPEN_READONLY) {
        oflags |= O_RDONLY;
    }
    if (flags & SQLITE_OPEN_CREATE) {
        oflags |= O_CREAT;
    }
    if (flags & SQLITE_OPEN_EXCLUSIVE) {
        oflags |= O_EXCL;
    }

    // get the current thread.
    td = curthread;

    // use kern_openat to open the file.
    error = kern_openat(td, AT_FDCWD, zName, UIO_SYSSPACE, oflags, S_IRUSR | S_IWUSR);
    if (error) {
        return SQLITE_CANTOPEN;
    }

    fd = td->td_retval[0];  // kern_openat() returns the file descriptor in td_retval[0].

    // get the file pointer (struct file *) from the descriptor.
    if ((error = fget(td, fd, &cap_no_rights, &fp)) != 0) {
        // If there's an error, close the descriptor and return.
        kern_close(td, fd);
        return SQLITE_CANTOPEN;
    }

    // you can now use `fp->f_ops->fo_read`, `fp->f_ops->fo_write`, etc., for file operations.
    // remember to release the file pointer and close the descriptor when you're done.

    fdrop(fp, td);
    kern_close(td, fd);  // Optionally close here or close after your operations.

    return SQLITE_OK;
}

// delete file in kernelspace
static int kern_vfs_delete(sqlite3_vfs *vfs, const char *zName, int syncDir) {
    struct thread *td = curthread;
    int error;

    error = kern_funlinkat(td, AT_FDCWD, zName, -1, UIO_SYSSPACE, 0, 0);
    if (error) {
        return SQLITE_IOERR_DELETE;
    }
    return SQLITE_OK;
}

static int kern_vfs_access(sqlite3_vfs *vfs, const char *zName, int flags, int *pResOut) {
    struct thread *td = curthread;
    struct stat sb;
    int error;

    error = kern_statat(td, 0, AT_FDCWD, zName, UIO_SYSSPACE, &sb);
    if (error) {
        *pResOut = 0;
        return SQLITE_OK;  // not finding the file is a valid response for access check.
    }

    // mapping SQLite access flags to actual permissions
    if ((flags & SQLITE_ACCESS_EXISTS) && (sb.st_mode & S_IFMT) == S_IFREG) {
        *pResOut = 1;
    } else if (flags & SQLITE_ACCESS_READWRITE && !(sb.st_mode & S_IWUSR)) {
        *pResOut = 0;
    } else {
        *pResOut = 1;  // all other checks pass.
    }

    return SQLITE_OK;
}

static int kern_vfs_full_pathname(sqlite3_vfs *vfs, const char *zName, int nOut, char *zOut) {
    strlcpy(zOut, zName, nOut);
    return SQLITE_OK;
}

static void *kern_vfs_dl_open(sqlite3_vfs *vfs, const char *zFilename) {
    return NULL;  // not supported
}

static void kern_vfs_dl_error(sqlite3_vfs *vfs, int nByte, char *zErrMsg) {
    strlcpy(zErrMsg, "Dynamic loading is not supported", nByte);
}

static void (*kern_vfs_dl_sym(sqlite3_vfs *vfs, void* p, const char *zSymbol))(void) {
    return NULL;  // not supported
}

static void kern_vfs_dl_close(sqlite3_vfs *vfs, void* p) {
    // do nothing
}

static int kern_vfs_randomness(sqlite3_vfs *vfs, int nByte, char *zOut) {
    arc4random_buf(zOut, nByte);
    return nByte;  // return the number of bytes of random data set in zOut
}

static int kern_vfs_sleep(sqlite3_vfs *vfs, int microseconds) {
    // Convert microseconds to ticks
    int ticks = microseconds / (1000000 / hz);
    pause("sqlitesleep", ticks);
    return SQLITE_OK;
}

static int kern_vfs_current_time(sqlite3_vfs *vfs, double *pTime) {
    struct timespec ts;
    getnanotime(&ts);
    *pTime = ts.tv_sec;
    // *pTime = ts.tv_sec + ts.tv_nsec * 1e-9;  // to seconds
    return SQLITE_OK;
}

static int kern_vfs_get_last_error(sqlite3_vfs *vfs, int nBuf, char *zBuf) {
    int error = curthread->td_errno;
    if (error == 0 || nBuf <= 0) {
        return 0;
    }
    
    // convert the error number to a string (however this might be a simple placeholder)
    snprintf(zBuf, nBuf, "Kernel error: %d", error);
    return SQLITE_OK;
}

sqlite3_vfs kern_vfs = {
		1,                          // iVersion
		sizeof(sqlite3_file),       // szOsFile
		SQLITE_MAX_PATHLEN,         // mxPathname
		NULL,                       // pNext
		"kern_vfs",                 // zName
		NULL,                       // pAppData
		kern_vfs_open,              // xOpen
		kern_vfs_delete,            // xDelete
		kern_vfs_access,            // xAccess
		kern_vfs_full_pathname,     // xFullPathname
		kern_vfs_dl_open,           // xDlOpen
		kern_vfs_dl_error,          // xDlError
		kern_vfs_dl_sym,            // xDlSym
		kern_vfs_dl_close,          // xDlClose
		kern_vfs_randomness,        // xRandomness
		kern_vfs_sleep,            // xSleep
		kern_vfs_current_time,      // xCurrentTime
		kern_vfs_get_last_error     // xGetLastError
	};

#endif