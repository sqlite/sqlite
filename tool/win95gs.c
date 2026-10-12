/*
** 2026
**
** The author disclaims copyright to this source code.  In place of
** a legal notice, here is a blessing:
**
**    May you do good and not evil.
**    May you find forgiveness for yourself and forgive others.
**    May you share freely, never taking more than you give.
**
*******************************************************************************
**
** A drop-in replacement for the MinGW-W64 stack-protector support object,
** provided so that SQLite can be linked into an executable that loads on
** plain Windows 95.
**
** This file is not part of the SQLite amalgamation and is not referenced
** by any makefile.  It is linked in by hand, and only for a build that is
** deliberately targeting Windows 95:
**
**     i686-w64-mingw32-gcc -O2 -DSQLITE_OS_WIN=1 -DSQLITE_OS_WINNT=0 \
**         -D_WIN32_WINNT=0x0400 -DWINVER=0x0400 \
**         -DSQLITE_OMIT_WAL=1 -DSQLITE_MAX_MMAP_SIZE=0 \
**         -o sqlite3.exe shell.c sqlite3.c tool/win95gs.c -ladvapi32
**
** Why this is needed
** ------------------
**
** crt2.o, the MinGW C runtime startup, calls __security_init_cookie() on
** every program start to seed the stack-protector guard with time- and
** address-derived entropy.  The only definition of that function in the
** toolchain lives in an archive member of libmingw32.a that also imports
** GetSystemTimeAsFileTime.  That API first appeared in Windows 98, so
** Windows 95 has no such export to import.
**
** A static import has to be satisfied by the loader before main() runs, so
** the executable refuses to start on Windows 95 no matter what SQLite
** itself does -- and SQLite does not, by this point, need anything that
** Windows 95 lacks.  No compiler flag avoids it, because the reference
** comes from crt2.o rather than from user code.
**
** Supplying our own definitions of the four symbols the archive member
** provides means the linker resolves them locally and never extracts the
** archive member at all, so the Win98-only import never reaches the
** executable's import table.
**
** Naming and calling convention
** ------------------------------
**
** crt2.o asks for these by the exact undecorated names below.  On i386
** Windows a cdecl identifier "X" is decorated to "_X", so a C identifier
** of two underscores yields the three that the linker wants.  They must
** be cdecl and not WINAPI: a stdcall definition is decorated to
** "name@N", which will not match crt2.o's reference, and the linker will
** silently fall back to the archive member -- which is exactly the bug
** this file exists to prevent.
**
** The seed is deliberately a function of the system time, the process and
** thread ids, the tick count and the performance counter, all of which
** exist on Windows 95.  GetSystemTimeAsFileTime() is not used, nor is
** anything else introduced after Windows 95.
**
** What this does not do: Windows 95 also lacks WAL mode and memory mapped
** files, which is why the build above passes SQLITE_OMIT_WAL and
** SQLITE_MAX_MMAP_SIZE=0.  SQLite raises an #error if those are left on
** for a SQLITE_OS_WINNT=0 build.
*/
#include <windows.h>

/*
** The stack-rotating cookies.  The default guard value must never be
** used, so __security_init_cookie() below replaces it unconditionally
** and refuses the one value that would be indistinguishable from it.
*/
unsigned long __security_cookie            = 0xBB40E64EUL;
unsigned long __security_cookie_complement = 0x44BF19B1UL;

/*
** Reached only if the stack-rotating guard was actually tripped.
*/
void __report_gsfailure(void){
  TerminateProcess(GetCurrentProcess(), 0xC0000409UL);
}

/*
** Called from crt2.o before the user's main().  Seeds the guard with a
** mixture of values that are all available on Windows 95.
*/
void __security_init_cookie(void){
  FILETIME ft;
  SYSTEMTIME st;
  LARGE_INTEGER pc;
  unsigned long c;

  GetSystemTime(&st);
  if( SystemTimeToFileTime(&st, &ft) ){
    c = ft.dwLowDateTime ^ ft.dwHighDateTime;
  }else{
    c = 0;
  }
  c ^= GetCurrentProcessId();
  c ^= GetCurrentThreadId();
  c ^= GetTickCount();
  QueryPerformanceCounter(&pc);
  c ^= (unsigned long)(pc.LowPart ^ pc.HighPart);
  if( c==0xBB40E64EUL ) c = 0xBB40E64DUL;
  __security_cookie = c;
  __security_cookie_complement = ~c;
}
