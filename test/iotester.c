/*
** 2024-08-13
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
** A program to test I/O through a VFS implementations.
**
** This program is intended as a template for custom test module used
** to verify ports of SQLite to non-standard platforms.  Typically
** porting SQLite to a new platform requires writing a new VFS object
** to implement the I/O methods required by SQLite.  This file contains
** code that attempts exercise such a VFS object and verify that it is
** operating correctly.
*/
#include "sqlite3.h"
#include <stdlib.h>
#include <string.h>
#include <assert.h>

/* Number of elements in static array X */
#define COUNT(X)  (sizeof(X)/sizeof(X[0]))

/*
** State of the tester.
*/
typedef struct IOTester IOTester;
struct IOTester {
  int pgsz;                    /* Databases should use this page size */
  const char *zJMode;          /* Databases should use this journal mode */
  int isExclusive;             /* True for exclusive locking mode */
  sqlite3_int64 szMMap;        /* Memory-map size */
  const char *zTestModule;     /* Test module currently running */
  int iTestNum;                /* Test number currently running */
  int nTest;                   /* Total number of tests run */
  int nFault;                  /* Number of test failures */
  int nOOM;                    /* Out of memory faults */
  int bCatch;                  /* If true, suppress errors */
  int nCatch;                  /* Number of deliberately suppressed errors */
  int eVerbosity;              /* How much output */
  sqlite3_str *errTxt;         /* Error text written here */
};


/* Forward declarations of subroutines and functions. */
static void iotestError(IOTester*,const char *zFormat, ...);
static void iotestBeginTest(IOTester *p, int iTestnum);
static void iotestBasic1(IOTester*);
static void iotestBasic2(IOTester*);



/******************************************************************************
** The SHA3 Hash Engine
*/
/*
** Macros to determine whether the machine is big or little endian,
** and whether or not that determination is run-time or compile-time.
**
** For best performance, an attempt is made to guess at the byte-order
** using C-preprocessor macros.  If that is unsuccessful, or if
** -DSHA3_BYTEORDER=0 is set, then byte-order is determined
** at run-time.
*/
#ifndef SHA3_BYTEORDER
# if defined(i386)     || defined(__i386__)   || defined(_M_IX86) ||    \
     defined(__x86_64) || defined(__x86_64__) || defined(_M_X64)  ||    \
     defined(_M_AMD64) || defined(_M_ARM)     || defined(__x86)   ||    \
     defined(__arm__)
#   define SHA3_BYTEORDER    1234
# elif defined(sparc)    || defined(__ppc__)
#   define SHA3_BYTEORDER    4321
# else
#   define SHA3_BYTEORDER 0
# endif
#endif


/*
** State structure for a SHA3 hash in progress
*/
typedef sqlite3_uint64 u64;
typedef struct SHA3Context SHA3Context;
struct SHA3Context {
  union {
    u64 s[25];                /* Keccak state. 5x5 lines of 64 bits each */
    unsigned char x[1600];    /* ... or 1600 bytes */
  } u;
  unsigned nRate;        /* Bytes of input accepted per Keccak iteration */
  unsigned nLoaded;      /* Input bytes loaded into u.x[] so far this cycle */
  unsigned ixMask;       /* Insert next input into u.x[nLoaded^ixMask]. */
  unsigned iSize;        /* 224, 256, 358, or 512 */
};

/*
** A single step of the Keccak mixing function for a 1600-bit state
*/
static void KeccakF1600Step(SHA3Context *p){
  int i;
  u64 b0, b1, b2, b3, b4;
  u64 c0, c1, c2, c3, c4;
  u64 d0, d1, d2, d3, d4;
  static const u64 RC[] = {
    0x0000000000000001ULL,  0x0000000000008082ULL,
    0x800000000000808aULL,  0x8000000080008000ULL,
    0x000000000000808bULL,  0x0000000080000001ULL,
    0x8000000080008081ULL,  0x8000000000008009ULL,
    0x000000000000008aULL,  0x0000000000000088ULL,
    0x0000000080008009ULL,  0x000000008000000aULL,
    0x000000008000808bULL,  0x800000000000008bULL,
    0x8000000000008089ULL,  0x8000000000008003ULL,
    0x8000000000008002ULL,  0x8000000000000080ULL,
    0x000000000000800aULL,  0x800000008000000aULL,
    0x8000000080008081ULL,  0x8000000000008080ULL,
    0x0000000080000001ULL,  0x8000000080008008ULL
  };
# define a00 (p->u.s[0])
# define a01 (p->u.s[1])
# define a02 (p->u.s[2])
# define a03 (p->u.s[3])
# define a04 (p->u.s[4])
# define a10 (p->u.s[5])
# define a11 (p->u.s[6])
# define a12 (p->u.s[7])
# define a13 (p->u.s[8])
# define a14 (p->u.s[9])
# define a20 (p->u.s[10])
# define a21 (p->u.s[11])
# define a22 (p->u.s[12])
# define a23 (p->u.s[13])
# define a24 (p->u.s[14])
# define a30 (p->u.s[15])
# define a31 (p->u.s[16])
# define a32 (p->u.s[17])
# define a33 (p->u.s[18])
# define a34 (p->u.s[19])
# define a40 (p->u.s[20])
# define a41 (p->u.s[21])
# define a42 (p->u.s[22])
# define a43 (p->u.s[23])
# define a44 (p->u.s[24])
# define ROL64(a,x) ((a<<x)|(a>>(64-x)))

  for(i=0; i<24; i+=4){
    c0 = a00^a10^a20^a30^a40;
    c1 = a01^a11^a21^a31^a41;
    c2 = a02^a12^a22^a32^a42;
    c3 = a03^a13^a23^a33^a43;
    c4 = a04^a14^a24^a34^a44;
    d0 = c4^ROL64(c1, 1);
    d1 = c0^ROL64(c2, 1);
    d2 = c1^ROL64(c3, 1);
    d3 = c2^ROL64(c4, 1);
    d4 = c3^ROL64(c0, 1);

    b0 = (a00^d0);
    b1 = ROL64((a11^d1), 44);
    b2 = ROL64((a22^d2), 43);
    b3 = ROL64((a33^d3), 21);
    b4 = ROL64((a44^d4), 14);
    a00 =   b0 ^((~b1)&  b2 );
    a00 ^= RC[i];
    a11 =   b1 ^((~b2)&  b3 );
    a22 =   b2 ^((~b3)&  b4 );
    a33 =   b3 ^((~b4)&  b0 );
    a44 =   b4 ^((~b0)&  b1 );

    b2 = ROL64((a20^d0), 3);
    b3 = ROL64((a31^d1), 45);
    b4 = ROL64((a42^d2), 61);
    b0 = ROL64((a03^d3), 28);
    b1 = ROL64((a14^d4), 20);
    a20 =   b0 ^((~b1)&  b2 );
    a31 =   b1 ^((~b2)&  b3 );
    a42 =   b2 ^((~b3)&  b4 );
    a03 =   b3 ^((~b4)&  b0 );
    a14 =   b4 ^((~b0)&  b1 );

    b4 = ROL64((a40^d0), 18);
    b0 = ROL64((a01^d1), 1);
    b1 = ROL64((a12^d2), 6);
    b2 = ROL64((a23^d3), 25);
    b3 = ROL64((a34^d4), 8);
    a40 =   b0 ^((~b1)&  b2 );
    a01 =   b1 ^((~b2)&  b3 );
    a12 =   b2 ^((~b3)&  b4 );
    a23 =   b3 ^((~b4)&  b0 );
    a34 =   b4 ^((~b0)&  b1 );

    b1 = ROL64((a10^d0), 36);
    b2 = ROL64((a21^d1), 10);
    b3 = ROL64((a32^d2), 15);
    b4 = ROL64((a43^d3), 56);
    b0 = ROL64((a04^d4), 27);
    a10 =   b0 ^((~b1)&  b2 );
    a21 =   b1 ^((~b2)&  b3 );
    a32 =   b2 ^((~b3)&  b4 );
    a43 =   b3 ^((~b4)&  b0 );
    a04 =   b4 ^((~b0)&  b1 );

    b3 = ROL64((a30^d0), 41);
    b4 = ROL64((a41^d1), 2);
    b0 = ROL64((a02^d2), 62);
    b1 = ROL64((a13^d3), 55);
    b2 = ROL64((a24^d4), 39);
    a30 =   b0 ^((~b1)&  b2 );
    a41 =   b1 ^((~b2)&  b3 );
    a02 =   b2 ^((~b3)&  b4 );
    a13 =   b3 ^((~b4)&  b0 );
    a24 =   b4 ^((~b0)&  b1 );

    c0 = a00^a20^a40^a10^a30;
    c1 = a11^a31^a01^a21^a41;
    c2 = a22^a42^a12^a32^a02;
    c3 = a33^a03^a23^a43^a13;
    c4 = a44^a14^a34^a04^a24;
    d0 = c4^ROL64(c1, 1);
    d1 = c0^ROL64(c2, 1);
    d2 = c1^ROL64(c3, 1);
    d3 = c2^ROL64(c4, 1);
    d4 = c3^ROL64(c0, 1);

    b0 = (a00^d0);
    b1 = ROL64((a31^d1), 44);
    b2 = ROL64((a12^d2), 43);
    b3 = ROL64((a43^d3), 21);
    b4 = ROL64((a24^d4), 14);
    a00 =   b0 ^((~b1)&  b2 );
    a00 ^= RC[i+1];
    a31 =   b1 ^((~b2)&  b3 );
    a12 =   b2 ^((~b3)&  b4 );
    a43 =   b3 ^((~b4)&  b0 );
    a24 =   b4 ^((~b0)&  b1 );

    b2 = ROL64((a40^d0), 3);
    b3 = ROL64((a21^d1), 45);
    b4 = ROL64((a02^d2), 61);
    b0 = ROL64((a33^d3), 28);
    b1 = ROL64((a14^d4), 20);
    a40 =   b0 ^((~b1)&  b2 );
    a21 =   b1 ^((~b2)&  b3 );
    a02 =   b2 ^((~b3)&  b4 );
    a33 =   b3 ^((~b4)&  b0 );
    a14 =   b4 ^((~b0)&  b1 );

    b4 = ROL64((a30^d0), 18);
    b0 = ROL64((a11^d1), 1);
    b1 = ROL64((a42^d2), 6);
    b2 = ROL64((a23^d3), 25);
    b3 = ROL64((a04^d4), 8);
    a30 =   b0 ^((~b1)&  b2 );
    a11 =   b1 ^((~b2)&  b3 );
    a42 =   b2 ^((~b3)&  b4 );
    a23 =   b3 ^((~b4)&  b0 );
    a04 =   b4 ^((~b0)&  b1 );

    b1 = ROL64((a20^d0), 36);
    b2 = ROL64((a01^d1), 10);
    b3 = ROL64((a32^d2), 15);
    b4 = ROL64((a13^d3), 56);
    b0 = ROL64((a44^d4), 27);
    a20 =   b0 ^((~b1)&  b2 );
    a01 =   b1 ^((~b2)&  b3 );
    a32 =   b2 ^((~b3)&  b4 );
    a13 =   b3 ^((~b4)&  b0 );
    a44 =   b4 ^((~b0)&  b1 );

    b3 = ROL64((a10^d0), 41);
    b4 = ROL64((a41^d1), 2);
    b0 = ROL64((a22^d2), 62);
    b1 = ROL64((a03^d3), 55);
    b2 = ROL64((a34^d4), 39);
    a10 =   b0 ^((~b1)&  b2 );
    a41 =   b1 ^((~b2)&  b3 );
    a22 =   b2 ^((~b3)&  b4 );
    a03 =   b3 ^((~b4)&  b0 );
    a34 =   b4 ^((~b0)&  b1 );

    c0 = a00^a40^a30^a20^a10;
    c1 = a31^a21^a11^a01^a41;
    c2 = a12^a02^a42^a32^a22;
    c3 = a43^a33^a23^a13^a03;
    c4 = a24^a14^a04^a44^a34;
    d0 = c4^ROL64(c1, 1);
    d1 = c0^ROL64(c2, 1);
    d2 = c1^ROL64(c3, 1);
    d3 = c2^ROL64(c4, 1);
    d4 = c3^ROL64(c0, 1);

    b0 = (a00^d0);
    b1 = ROL64((a21^d1), 44);
    b2 = ROL64((a42^d2), 43);
    b3 = ROL64((a13^d3), 21);
    b4 = ROL64((a34^d4), 14);
    a00 =   b0 ^((~b1)&  b2 );
    a00 ^= RC[i+2];
    a21 =   b1 ^((~b2)&  b3 );
    a42 =   b2 ^((~b3)&  b4 );
    a13 =   b3 ^((~b4)&  b0 );
    a34 =   b4 ^((~b0)&  b1 );

    b2 = ROL64((a30^d0), 3);
    b3 = ROL64((a01^d1), 45);
    b4 = ROL64((a22^d2), 61);
    b0 = ROL64((a43^d3), 28);
    b1 = ROL64((a14^d4), 20);
    a30 =   b0 ^((~b1)&  b2 );
    a01 =   b1 ^((~b2)&  b3 );
    a22 =   b2 ^((~b3)&  b4 );
    a43 =   b3 ^((~b4)&  b0 );
    a14 =   b4 ^((~b0)&  b1 );

    b4 = ROL64((a10^d0), 18);
    b0 = ROL64((a31^d1), 1);
    b1 = ROL64((a02^d2), 6);
    b2 = ROL64((a23^d3), 25);
    b3 = ROL64((a44^d4), 8);
    a10 =   b0 ^((~b1)&  b2 );
    a31 =   b1 ^((~b2)&  b3 );
    a02 =   b2 ^((~b3)&  b4 );
    a23 =   b3 ^((~b4)&  b0 );
    a44 =   b4 ^((~b0)&  b1 );

    b1 = ROL64((a40^d0), 36);
    b2 = ROL64((a11^d1), 10);
    b3 = ROL64((a32^d2), 15);
    b4 = ROL64((a03^d3), 56);
    b0 = ROL64((a24^d4), 27);
    a40 =   b0 ^((~b1)&  b2 );
    a11 =   b1 ^((~b2)&  b3 );
    a32 =   b2 ^((~b3)&  b4 );
    a03 =   b3 ^((~b4)&  b0 );
    a24 =   b4 ^((~b0)&  b1 );

    b3 = ROL64((a20^d0), 41);
    b4 = ROL64((a41^d1), 2);
    b0 = ROL64((a12^d2), 62);
    b1 = ROL64((a33^d3), 55);
    b2 = ROL64((a04^d4), 39);
    a20 =   b0 ^((~b1)&  b2 );
    a41 =   b1 ^((~b2)&  b3 );
    a12 =   b2 ^((~b3)&  b4 );
    a33 =   b3 ^((~b4)&  b0 );
    a04 =   b4 ^((~b0)&  b1 );

    c0 = a00^a30^a10^a40^a20;
    c1 = a21^a01^a31^a11^a41;
    c2 = a42^a22^a02^a32^a12;
    c3 = a13^a43^a23^a03^a33;
    c4 = a34^a14^a44^a24^a04;
    d0 = c4^ROL64(c1, 1);
    d1 = c0^ROL64(c2, 1);
    d2 = c1^ROL64(c3, 1);
    d3 = c2^ROL64(c4, 1);
    d4 = c3^ROL64(c0, 1);

    b0 = (a00^d0);
    b1 = ROL64((a01^d1), 44);
    b2 = ROL64((a02^d2), 43);
    b3 = ROL64((a03^d3), 21);
    b4 = ROL64((a04^d4), 14);
    a00 =   b0 ^((~b1)&  b2 );
    a00 ^= RC[i+3];
    a01 =   b1 ^((~b2)&  b3 );
    a02 =   b2 ^((~b3)&  b4 );
    a03 =   b3 ^((~b4)&  b0 );
    a04 =   b4 ^((~b0)&  b1 );

    b2 = ROL64((a10^d0), 3);
    b3 = ROL64((a11^d1), 45);
    b4 = ROL64((a12^d2), 61);
    b0 = ROL64((a13^d3), 28);
    b1 = ROL64((a14^d4), 20);
    a10 =   b0 ^((~b1)&  b2 );
    a11 =   b1 ^((~b2)&  b3 );
    a12 =   b2 ^((~b3)&  b4 );
    a13 =   b3 ^((~b4)&  b0 );
    a14 =   b4 ^((~b0)&  b1 );

    b4 = ROL64((a20^d0), 18);
    b0 = ROL64((a21^d1), 1);
    b1 = ROL64((a22^d2), 6);
    b2 = ROL64((a23^d3), 25);
    b3 = ROL64((a24^d4), 8);
    a20 =   b0 ^((~b1)&  b2 );
    a21 =   b1 ^((~b2)&  b3 );
    a22 =   b2 ^((~b3)&  b4 );
    a23 =   b3 ^((~b4)&  b0 );
    a24 =   b4 ^((~b0)&  b1 );

    b1 = ROL64((a30^d0), 36);
    b2 = ROL64((a31^d1), 10);
    b3 = ROL64((a32^d2), 15);
    b4 = ROL64((a33^d3), 56);
    b0 = ROL64((a34^d4), 27);
    a30 =   b0 ^((~b1)&  b2 );
    a31 =   b1 ^((~b2)&  b3 );
    a32 =   b2 ^((~b3)&  b4 );
    a33 =   b3 ^((~b4)&  b0 );
    a34 =   b4 ^((~b0)&  b1 );

    b3 = ROL64((a40^d0), 41);
    b4 = ROL64((a41^d1), 2);
    b0 = ROL64((a42^d2), 62);
    b1 = ROL64((a43^d3), 55);
    b2 = ROL64((a44^d4), 39);
    a40 =   b0 ^((~b1)&  b2 );
    a41 =   b1 ^((~b2)&  b3 );
    a42 =   b2 ^((~b3)&  b4 );
    a43 =   b3 ^((~b4)&  b0 );
    a44 =   b4 ^((~b0)&  b1 );
  }
}

/*
** Initialize a new hash.  iSize determines the size of the hash
** in bits and should be one of 224, 256, 384, or 512.  Or iSize
** can be zero to use the default hash size of 256 bits.
*/
static void SHA3Init(SHA3Context *p, int iSize){
  memset(p, 0, sizeof(*p));
  p->iSize = iSize;
  if( iSize>=128 && iSize<=512 ){
    p->nRate = (1600 - ((iSize + 31)&~31)*2)/8;
  }else{
    p->nRate = (1600 - 2*256)/8;
  }
#if SHA3_BYTEORDER==1234
  /* Known to be little-endian at compile-time. No-op */
#elif SHA3_BYTEORDER==4321
  p->ixMask = 7;  /* Big-endian */
#else
  {
    static unsigned int one = 1;
    if( 1==*(unsigned char*)&one ){
      /* Little endian.  No byte swapping. */
      p->ixMask = 0;
    }else{
      /* Big endian.  Byte swap. */
      p->ixMask = 7;
    }
  }
#endif
}

/*
** Make consecutive calls to the SHA3Update function to add new content
** to the hash
*/
static void SHA3Update(
  SHA3Context *p,
  const unsigned char *aData,
  unsigned int nData
){
  unsigned int i = 0;
  if( aData==0 ) return;
#if SHA3_BYTEORDER==1234
  if( (p->nLoaded % 8)==0 && ((aData - (const unsigned char*)0)&7)==0 ){
    for(; i+7<nData; i+=8){
      p->u.s[p->nLoaded/8] ^= *(u64*)&aData[i];
      p->nLoaded += 8;
      if( p->nLoaded>=p->nRate ){
        KeccakF1600Step(p);
        p->nLoaded = 0;
      }
    }
  }
#endif
  for(; i<nData; i++){
#if SHA3_BYTEORDER==1234
    p->u.x[p->nLoaded] ^= aData[i];
#elif SHA3_BYTEORDER==4321
    p->u.x[p->nLoaded^0x07] ^= aData[i];
#else
    p->u.x[p->nLoaded^p->ixMask] ^= aData[i];
#endif
    p->nLoaded++;
    if( p->nLoaded==p->nRate ){
      KeccakF1600Step(p);
      p->nLoaded = 0;
    }
  }
}

/*
** After all content has been added, invoke SHA3Final() to compute
** the final hash.  The function returns a pointer to the binary
** hash value.
*/
static unsigned char *SHA3Final(SHA3Context *p){
  unsigned int i;
  if( p->nLoaded==p->nRate-1 ){
    const unsigned char c1 = 0x86;
    SHA3Update(p, &c1, 1);
  }else{
    const unsigned char c2 = 0x06;
    const unsigned char c3 = 0x80;
    SHA3Update(p, &c2, 1);
    p->nLoaded = p->nRate - 1;
    SHA3Update(p, &c3, 1);
  }
  for(i=0; i<p->nRate; i++){
    p->u.x[i+p->nRate] = p->u.x[i^p->ixMask];
  }
  return &p->u.x[p->nRate];
}

/*
** Implementation of the sha3(X,SIZE) function.
**
** Return a BLOB which is the SIZE-bit SHA3 hash of X.  The default
** size is 256.  If X is a BLOB, it is hashed as is.  
** For all other non-NULL types of input, X is converted into a UTF-8 string
** and the string is hashed without the trailing 0x00 terminator.  The hash
** of a NULL value is NULL.
*/
static void sha3Func(
  sqlite3_context *context,
  int argc,
  sqlite3_value **argv
){
  SHA3Context cx;
  int eType = sqlite3_value_type(argv[0]);
  int nByte = sqlite3_value_bytes(argv[0]);
  int iSize;
  if( argc==1 ){
    iSize = 256;
  }else{
    iSize = sqlite3_value_int(argv[1]);
    if( iSize!=224 && iSize!=256 && iSize!=384 && iSize!=512 ){
      sqlite3_result_error(context, "SHA3 size should be one of: 224 256 "
                                    "384 512", -1);
      return;
    }
  }
  if( eType==SQLITE_NULL ) return;
  SHA3Init(&cx, iSize);
  if( eType==SQLITE_BLOB ){
    SHA3Update(&cx, sqlite3_value_blob(argv[0]), nByte);
  }else{
    SHA3Update(&cx, sqlite3_value_text(argv[0]), nByte);
  }
  sqlite3_result_blob(context, SHA3Final(&cx), iSize/8, SQLITE_TRANSIENT);
}

/* Compute a string using sqlite3_vsnprintf() with a maximum length
** of 50 bytes and add it to the hash.
*/
static void sha3_step_vformat(
  SHA3Context *p,                 /* Add content to this context */
  const char *zFormat,
  ...
){
  va_list ap;
  int n;
  char zBuf[50];
  va_start(ap, zFormat);
  sqlite3_vsnprintf(sizeof(zBuf),zBuf,zFormat,ap);
  va_end(ap);
  n = (int)strlen(zBuf);
  SHA3Update(p, (unsigned char*)zBuf, n);
}

/*
** Update a SHA3Context using a single sqlite3_value.
*/
static void sha3UpdateFromValue(SHA3Context *p, sqlite3_value *pVal){
  switch( sqlite3_value_type(pVal) ){
    case SQLITE_NULL: {
      SHA3Update(p, (const unsigned char*)"N",1);
      break;
    }
    case SQLITE_INTEGER: {
      sqlite3_uint64 u;
      int j;
      unsigned char x[9];
      sqlite3_int64 v = sqlite3_value_int64(pVal);
      memcpy(&u, &v, 8);
      for(j=8; j>=1; j--){
        x[j] = u & 0xff;
        u >>= 8;
      }
      x[0] = 'I';
      SHA3Update(p, x, 9);
      break;
    }
    case SQLITE_FLOAT: {
      sqlite3_uint64 u;
      int j;
      unsigned char x[9];
      double r = sqlite3_value_double(pVal);
      memcpy(&u, &r, 8);
      for(j=8; j>=1; j--){
        x[j] = u & 0xff;
        u >>= 8;
      }
      x[0] = 'F';
      SHA3Update(p,x,9);
      break;
    }
    case SQLITE_TEXT: {
      int n2 = sqlite3_value_bytes(pVal);
      const unsigned char *z2 = sqlite3_value_text(pVal);
      sha3_step_vformat(p,"T%d:",n2);
      SHA3Update(p, z2, n2);
      break;
    }
    case SQLITE_BLOB: {
      int n2 = sqlite3_value_bytes(pVal);
      const unsigned char *z2 = sqlite3_value_blob(pVal);
      sha3_step_vformat(p,"B%d:",n2);
      SHA3Update(p, z2, n2);
      break;
    }
  }
}

/*
** xStep function for sha3_agg().
*/
static void sha3AggStep(
  sqlite3_context *context,
  int argc,
  sqlite3_value **argv
){
  SHA3Context *p;
  p = (SHA3Context*)sqlite3_aggregate_context(context, sizeof(*p));
  if( p==0 ) return;
  if( p->nRate==0 ){
    int sz = 256;
    if( argc==2 ){
      sz = sqlite3_value_int(argv[1]);
      if( sz!=224 && sz!=384 && sz!=512 ){
        sz = 256;
      }
    }
    SHA3Init(p, sz);
  }
  sha3UpdateFromValue(p, argv[0]);
}


/*
** xFinal function for sha3_agg().
*/
static void sha3AggFinal(sqlite3_context *context){
  SHA3Context *p;
  p = (SHA3Context*)sqlite3_aggregate_context(context, sizeof(*p));
  if( p==0 ) return;
  if( p->iSize ){
    sqlite3_result_blob(context, SHA3Final(p), p->iSize/8, SQLITE_TRANSIENT);
  }
}

/*
** Add the sha3() and sha3_agg() functions to database connection "db"
*/
static int sha3Register(sqlite3 *db){
  int rc = SQLITE_OK;
  rc = sqlite3_create_function(db, "sha3", 1,
                      SQLITE_UTF8 | SQLITE_INNOCUOUS | SQLITE_DETERMINISTIC,
                      0, sha3Func, 0, 0);
  if( rc==SQLITE_OK ){
    rc = sqlite3_create_function(db, "sha3_agg", 1,
                      SQLITE_UTF8 | SQLITE_INNOCUOUS | SQLITE_DETERMINISTIC,
                      0, 0, sha3AggStep, sha3AggFinal);
  }
  return rc;
}

/******************************** stmtrand() *******************************
**
** A pseudo-random number generator that gives the same sequence within
** a single statement.
*/

/* State of the pseudo-random number generator */
typedef struct Stmtrand {
  unsigned int x, y;
} Stmtrand;

/* auxdata key */
#define STMTRAND_KEY  (-4418371)

/*
** Function:     stmtrand(SEED)
**
** Return a pseudo-random number.
*/
static void stmtrandFunc(
  sqlite3_context *context,
  int argc,
  sqlite3_value **argv
){
  Stmtrand *p;

  p = (Stmtrand*)sqlite3_get_auxdata(context, STMTRAND_KEY);
  if( p==0 ){
    unsigned int seed;
    p = sqlite3_malloc( sizeof(*p) );
    if( p==0 ){
      sqlite3_result_error_nomem(context);
      return;
    }
    if( argc>=1 ){
      seed = (unsigned int)sqlite3_value_int(argv[0]);
    }else{
      seed = 0;
    }
    p->x = seed | 1;
    p->y = seed;
    sqlite3_set_auxdata(context, STMTRAND_KEY, p, sqlite3_free);
    p = (Stmtrand*)sqlite3_get_auxdata(context, STMTRAND_KEY);
    if( p==0 ){
      sqlite3_result_error_nomem(context);
      return;
    }
  }
  p->x = (p->x>>1) ^ ((1+~(p->x&1)) & 0xd0000001);
  p->y = p->y*1103515245 + 12345;
  sqlite3_result_int(context, (int)((p->x ^ p->y)&0x7fffffff));
}

static int stmtrandRegister(sqlite3 *db){
  int rc = SQLITE_OK;
  rc = sqlite3_create_function(db, "stmtrand", 1, SQLITE_UTF8, 0,
                               stmtrandFunc, 0, 0);
  if( rc==SQLITE_OK ){
    rc = sqlite3_create_function(db, "stmtrand", 0, SQLITE_UTF8, 0,
                                 stmtrandFunc, 0, 0);
  }
  return rc;
}

/********************************** Utilities *******************************
**
** Functions intended for common use by all test modules.
*/

/*
** Record a test failure.
*/
void iotestError(IOTester *p, const char *zFormat, ...){
  va_list ap;
  if( p->bCatch ){
    p->nCatch++;
    return;
  }
  p->nFault++;
  if( p->errTxt==0 ){
    p->errTxt = sqlite3_str_new(0);
    if( p==0 ){
      p->nOOM++;
      return;
    }
  }
  sqlite3_str_appendf(p->errTxt,
     "FAULT: %s-%d pgsz=%d journal-mode=%s mmap-size=%lld",
     p->zTestModule, p->iTestNum,
     p->pgsz, p->zJMode, p->szMMap);
  if( p->isExclusive ){
     sqlite3_str_appendf(p->errTxt, " exclusive-locking-mode");
  }
  sqlite3_str_appendf(p->errTxt, "\n");
  if( zFormat ){
    va_start(ap, zFormat);
    sqlite3_str_vappendf(p->errTxt, zFormat, ap);
    va_end(ap);
  }
}

/*
** Create a new prepared statement based on SQL text.
*/
sqlite3_stmt *iotestVPrepare(
  IOTester *p,
  sqlite3 *db,
  const char *zFormat,
  va_list ap
){
  char *zSql = 0;
  int rc;
  sqlite3_stmt *pStmt = 0;
  if( db==0 ) return 0;
  zSql = sqlite3_vmprintf(zFormat, ap);
  if( zSql==0 ){
    p->nOOM++;
    return 0;
  }
  rc = sqlite3_prepare_v2(db, zSql, -1, &pStmt, 0);
  if( rc!=SQLITE_OK || pStmt==0 ){
    iotestError(p, "unable to prepare statement: \"%s\"\n", zSql);
    sqlite3_free(zSql);
    sqlite3_finalize(pStmt);
  }
  return pStmt;
}


/*
** Run a statement against a database.  Expect no results.
*/
static void iotestRun(
  IOTester *p,
  sqlite3 *db,
  const char *zSql,
  ...
){
  va_list ap;
  sqlite3_stmt *pStmt = 0;
  if( db==0 ) return;
  va_start(ap, zSql);
  pStmt = iotestVPrepare(p, db, zSql, ap);
  va_end(ap);
  if( pStmt ){
    int rc;
    while( (rc = sqlite3_step(pStmt))==SQLITE_ROW ){}
    if( rc==SQLITE_ERROR ){
      iotestError(p, "error running SQL statement %s: %s\n",
                   sqlite3_sql(pStmt), sqlite3_errmsg(db));
    }
  }
  sqlite3_finalize(pStmt);
}

/*
** Run a query against a database that returns a single integer.
*/
static sqlite3_int64 iotestQueryInt(
  IOTester *p,
  sqlite3 *db,
  sqlite3_int64 iDflt,
  const char *zSql,
  ...
){
  va_list ap;
  sqlite3_stmt *pStmt = 0;
  sqlite3_int64 res = iDflt;
  int rc;
  if( db==0 ) return iDflt;
  va_start(ap, zSql);
  pStmt = iotestVPrepare(p, db, zSql, ap);
  va_end(ap);
  if( pStmt==0 ){
    return iDflt;
  }
  rc = sqlite3_step(pStmt);
  if( rc==SQLITE_ROW ){
    res = sqlite3_column_int64(pStmt, 0);
  }else if( rc==SQLITE_ERROR ){
    iotestError(p, "error while running \"%s\": %s\n",
                 sqlite3_sql(pStmt), sqlite3_errmsg(db));
    res = iDflt;
  }
  sqlite3_finalize(pStmt);
  return res;
}

/*
** Run a query against a database that returns a text value.
** space to store that text is obtained from sqlite3_malloc()
** and needs to be freed by the caller.
*/
static char *iotestQueryText(
  IOTester *p,
  sqlite3 *db,
  const char *zDflt,
  const char *zSql,
  ...
){
  va_list ap;
  sqlite3_stmt *pStmt = 0;
  char *zRes = 0;
  int rc;
  if( db==0 ) return sqlite3_mprintf("%s", zDflt);
  va_start(ap, zSql);
  pStmt = iotestVPrepare(p, db, zSql, ap);
  va_end(ap);
  if( pStmt==0 ){
    return sqlite3_mprintf("%s", zDflt);
  }
  rc = sqlite3_step(pStmt);
  if( rc==SQLITE_ROW ){
    zRes = sqlite3_mprintf("%s", sqlite3_column_text(pStmt, 0));
  }else if( rc==SQLITE_ERROR ){
    iotestError(p, "error while running \"%s\": %s\n",
                 sqlite3_sql(pStmt), sqlite3_errmsg(db));
    zRes = sqlite3_mprintf("%s",zDflt);
  }else{
    zRes = sqlite3_mprintf("%s",zDflt);
  }
  sqlite3_finalize(pStmt);
  return zRes;
}

/*
** Delete a file by name using the xDelete method of the default VFS.
*/
void iotestDeleteFile(IOTester *p, const char *zFilename){
  sqlite3_vfs *pVfs = sqlite3_vfs_find(0);
  int rc;
  assert( pVfs!=0 );
  assert( pVfs->xDelete!=0 );
  rc = pVfs->xDelete(pVfs, zFilename, 0);
  if( rc!=SQLITE_OK && rc!=SQLITE_IOERR_DELETE_NOENT ){
    iotestError(p, "cannot delete file \"%s\"\n", zFilename);
  }
}

/*
** Open a database.  Return a pointer to that database connection,
** or if something goes wrong, return a NULL pointer.
**
** If the database does not previously exist, then do all the necessary
** setup as proscribed by the IOTester configuration.
*/
sqlite3 *iotestOpen(IOTester *p, const char *zDbFilename){
  sqlite3 *db;
  int rc;

  rc = sqlite3_open(zDbFilename, &db);
  if( rc!=SQLITE_OK ){
    iotestError(p, "cannot open database \"%s\"\n", zDbFilename);
    sqlite3_close(db);
    return 0;
  }
  if( iotestQueryInt(p, db, -1, "PRAGMA page_count")==0 ){
    iotestRun(p, db, "PRAGMA page_size=%d", p->pgsz);
    iotestRun(p, db, "PRAGMA journal_mode=%s", p->zJMode);
    iotestRun(p, db, "PRAGMA mmap_size=%lld", p->szMMap);
    if( p->isExclusive ){
      iotestRun(p, db, "PRAGMA locking_mode=EXCLUSIVE;");
    }
  }
  iotestRun(p, db, "PRAGMA cache_size=2;");
  iotestRun(p, db, "PRAGMA temp_store=FILE;");
  sha3Register(db);
  stmtrandRegister(db);
  return db;
}


/*********************************** Test Modules ****************************
*/
static void iotestBasic1(IOTester *p){
  sqlite3 *db = 0;
  p->zTestModule = "basic1";
  iotestBeginTest(p, 1);
  iotestDeleteFile(p, "basic1.db");
  if( p->nFault ) return;
  iotestBeginTest(p, 2);
  db = iotestOpen(p, "basic1.db");
  if( p->nFault ) goto basic1_exit;
  iotestBeginTest(p, 3);
  iotestRun(p, db, "CREATE TABLE t1(a,b,c);");
  if( p->nFault ) goto basic1_exit;
  iotestBeginTest(p, 4);
  iotestRun(p, db, "DROP TABLE t1;");

basic1_exit:
  sqlite3_close(db);
  iotestDeleteFile(p, "basic1.db");
}

static void iotestBasic2(IOTester *p){
  sqlite3 *db = 0;
  sqlite3 *db2 = 0;
  char *zH1 = 0;
  static const char *zDBName = "basic2.db";
  static const char *zExpected1 = 
     "7180714EBF13B8B3D872801D246C5E814227319F091578F8ECA7F51C20A5596E";
  p->zTestModule = "basic2";

  iotestBeginTest(p, 1);
  iotestDeleteFile(p, zDBName);
  if( p->nFault ) return;

  iotestBeginTest(p, 2);
  db = iotestOpen(p, zDBName);
  if( p->nFault ) goto basic2_exit;

  iotestBeginTest(p, 3);
  iotestRun(p, db, "CREATE TABLE t1(a INTEGER PRIMARY KEY, b TEXT);");
  iotestRun(p, db,
    "WITH c(i,r) AS ("
    "  VALUES(1,stmtrand())"
    "  UNION ALL SELECT i+1, stmtrand() FROM c WHERE i<1000"
    ")"
    "INSERT INTO t1(a,b) SELECT i, "
    "  format('%%.*c', 3200+r%%100, char(0x61+(r/100)%%26)) FROM c;"
  );
  if( p->nFault ) goto basic2_exit;

  iotestBeginTest(p, 4);
  zH1 = iotestQueryText(p, db,"?","SELECT hex(sha3_agg(b ORDER BY a)) FROM t1");
  if( strcmp(zH1,zExpected1)!=0 ){
    iotestError(p, "expected %s but got %s\n", zExpected1, zH1);
    goto basic2_exit;
  }
  sqlite3_free(zH1);
  zH1 = 0;

  iotestBeginTest(p, 5);
  if( p->isExclusive ) p->bCatch = 1;
  db2 = iotestOpen(p, zDBName);
  zH1 = iotestQueryText(p, db2, "?",
           "SELECT hex(sha3_agg(b ORDER BY a)) FROM t1");
  if( strcmp(zH1,zExpected1)!=0 ){
    iotestError(p, "expected %s but got %s\n", zExpected1, zH1);
  }
  sqlite3_free(zH1);
  zH1 = 0;
  if( p->isExclusive ){
    p->bCatch = 0;
    if( p->nCatch==0 ){
      iotestError(p, "ought not be able to use a secondary database "
                     "connection on \"%s\" while in EXCLUSIVE locking mode\n",
                     zDBName);
    }
  }

basic2_exit:
  sqlite3_close(db);
  sqlite3_close(db2);
  sqlite3_free(zH1);
  iotestDeleteFile(p, zDBName);
}

/********************** Out-Of-Band System Interactions **********************
**
** Any customizations that need to be made to port this test program to
** new platforms are made below this point.
*/
#include <stdio.h>

/*
** Start a new test case.  (This is under the "Out-of-band" section because
** of its optional use of printf().)
*/
static void iotestBeginTest(IOTester *p, int iTN){
  p->iTestNum = iTN;
  p->nTest++;
  if( p->eVerbosity>=2 ) printf("%s-%d\n", p->zTestModule, iTN);
}

/*********************************** Main ************************************
**
** The main test driver.
*/
int main(int argc, char **argv){
  IOTester x;                    /* Main testing context */
  int i, j, n;                   /* Loop counters and limits */
  int fixedPgsz = -1;            /* Override value for x.pgsz */
  const char *fixedJMode = 0;    /* Override value for x.zJMode */
  sqlite3_int64 fixedMMap = -1;  /* Override value for x.szMMap */
  int fixedExcl = -1;            /* Override value for x.isExclusive */

  /* Values for x.pgsz, if not overridden */
  static const int aPgsz[] = {
     512, 1024, 2048, 4096, 8192, 16384, 32768, 65536
  };

  /* Values for x.zJMode, if not overridden */
  static const char *azJMode[] = {
     "delete", "truncate", "persist", "wal"
  };

  /* Values for x.szMMap, if not overridden */
  static const sqlite3_int64 aszMMap[] = {
     0, 2097152, 2147483648
  };



  memset(&x, 0, sizeof(x));

  /* Process command-line arguments */
  for(i=1; i<argc; i++){
    const char *z = argv[i];
    if( z[0]=='-' && z[1]=='-' && z[2]!=0 ) z++;
    if( strcmp(z,"-v")==0 ){
      x.eVerbosity++;
    }else if( strcmp(z, "-vv")==0 ){
      x.eVerbosity += 2;
    }else if (strcmp(z, "-vfs")==0 && i<argc-1 ){
      int rc;
      sqlite3_vfs *pNew = sqlite3_vfs_find(argv[++i]);
      if( pNew==0 ){
        iotestError(&x, "No such VFS: \"%s\"\n", argv[i]);
        break;
      }
      rc = sqlite3_vfs_register(pNew, 1);
      if( rc!=SQLITE_OK ){
        iotestError(&x, "Unable to register VFS \"%s\" - result code %d\n",
                     argv[i], rc);
        break;
      }
    }else if (strcmp(z, "-pgsz")==0 && i<argc-1 ){
      fixedPgsz = atoi(argv[++i]);
      if( fixedPgsz<512 || fixedPgsz>65536 || (fixedPgsz&(fixedPgsz-1))!=0 ){
        iotestError(&x, "Not a valid page size: --pgsz %d", fixedPgsz);
        break;
      }
    }else if (strcmp(z, "-jmode")==0 && i<argc-1 ){
      fixedJMode = argv[++i];
      for(j=0; j<COUNT(azJMode) && strcmp(fixedJMode,azJMode[j])!=0; j++){}
      if( j>=COUNT(azJMode) ){
        iotestError(&x, "Not a valid journal mode: --jmode %s\n", fixedJMode);
        break;
      }
    }else if (strcmp(z, "-mmap")==0 && i<argc-1 ){
      fixedMMap = atoll(argv[++i]);
    }else if (strcmp(z, "-exclusive")==0 && i<argc-1 ){
      fixedExcl = atoi(argv[++i]);
    }else{
      iotestError(&x, "unknown option: \"%s\"\n", argv[i]);
      break;
    }
  }

  /* Run the tests */
  n = 1;
  if( fixedPgsz<0 ) n *= COUNT(aPgsz);
  if( fixedJMode==0 ) n *= COUNT(azJMode);
  if( fixedMMap<0 ) n *= COUNT(aszMMap);
  if( fixedExcl<0 ) n *= 2;
  for(i=0; x.nFault==0 && i<n; i++){
    int j,k;
    j = i;
    if( fixedPgsz>0 ){
      x.pgsz = fixedPgsz;
    }else{
      k = j % COUNT(aPgsz);
      x.pgsz = aPgsz[k];
      j /= COUNT(aPgsz);
    }
    if( fixedJMode ){
      x.zJMode = fixedJMode;
    }else{
      k = j % COUNT(azJMode);
      x.zJMode = azJMode[k];
      j /= COUNT(azJMode);
    }
    if( fixedMMap>=0 ){
      x.szMMap = fixedMMap;
    }else{
      k = j % COUNT(aszMMap);
      x.szMMap = aszMMap[k];
      j /= COUNT(aszMMap);
    }
    if( fixedExcl>=0 ){
      x.isExclusive = fixedExcl!=0;
    }else{
      k = j % 2;
      x.isExclusive = k;
      j /= 2;
    }

    if( x.eVerbosity>=1 ){
      printf("pgsz=%d journal_mode=%s mmap_size=%lld exclusive=%d\n",
             x.pgsz, x.zJMode, x.szMMap, x.isExclusive);
    }

    iotestBasic1(&x);
    if( x.nFault ) break;
    iotestBasic2(&x);
    if( x.nFault ) break;
  }

  /* Report results */
  printf("%d tests and %d errors\n", x.nTest, x.nFault);
  if( x.nOOM ) printf("%d out-of-memory faults\n", x.nOOM);
  if( x.errTxt ){
    char *zMsg = sqlite3_str_finish(x.errTxt);
    if( zMsg ){
      printf("%s", zMsg);
      sqlite3_free(zMsg);
    }
  }
  return x.nFault>0; 
}
