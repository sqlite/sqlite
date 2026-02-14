/*
** 2001 September 15
**
** The author disclaims copyright to this source code.  In place of
** a legal notice, here is a blessing:
**
**    May you do good and not evil.
**    May you find forgiveness for yourself and forgive others.
**    May you share freely, never taking more than you give.
**
*************************************************************************
** Utility functions used throughout sqlite.
**
** This file contains functions for allocating memory, comparing
** strings, and stuff like that.
**
*/
#include "sqliteInt.h"
#include <stdarg.h>
#ifndef SQLITE_OMIT_FLOATING_POINT
#include <math.h>
#endif

/*
** Calls to sqlite3FaultSim() are used to simulate a failure during testing,
** or to bypass normal error detection during testing in order to let
** execute proceed further downstream.
**
** In deployment, sqlite3FaultSim() *always* return SQLITE_OK (0).  The
** sqlite3FaultSim() function only returns non-zero during testing.
**
** During testing, if the test harness has set a fault-sim callback using
** a call to sqlite3_test_control(SQLITE_TESTCTRL_FAULT_INSTALL), then
** each call to sqlite3FaultSim() is relayed to that application-supplied
** callback and the integer return value form the application-supplied
** callback is returned by sqlite3FaultSim().
**
** The integer argument to sqlite3FaultSim() is a code to identify which
** sqlite3FaultSim() instance is being invoked. Each call to sqlite3FaultSim()
** should have a unique code.  To prevent legacy testing applications from
** breaking, the codes should not be changed or reused.
*/
#ifndef SQLITE_UNTESTABLE
int sqlite3FaultSim(int iTest){
  int (*xCallback)(int) = sqlite3GlobalConfig.xTestCallback;
  return xCallback ? xCallback(iTest) : SQLITE_OK;
}
#endif

#ifndef SQLITE_OMIT_FLOATING_POINT
/*
** Return true if the floating point value is Not a Number (NaN).
**
** Use the math library isnan() function if compiled with SQLITE_HAVE_ISNAN.
** Otherwise, we have our own implementation that works on most systems.
*/
int sqlite3IsNaN(double x){
  int rc;   /* The value return */
#if !SQLITE_HAVE_ISNAN && !HAVE_ISNAN
  u64 y;
  memcpy(&y,&x,sizeof(y));
  rc = IsNaN(y);
#else
  rc = isnan(x);
#endif /* HAVE_ISNAN */
  testcase( rc );
  return rc;
}
#endif /* SQLITE_OMIT_FLOATING_POINT */

#ifndef SQLITE_OMIT_FLOATING_POINT
/*
** Return true if the floating point value is NaN or +Inf or -Inf.
*/
int sqlite3IsOverflow(double x){
  int rc;   /* The value return */
  u64 y;
  memcpy(&y,&x,sizeof(y));
  rc = IsOvfl(y);
  return rc;
}
#endif /* SQLITE_OMIT_FLOATING_POINT */

/*
** Compute a string length that is limited to what can be stored in
** lower 30 bits of a 32-bit signed integer.
**
** The value returned will never be negative.  Nor will it ever be greater
** than the actual length of the string.  For very long strings (greater
** than 1GiB) the value returned might be less than the true string length.
*/
int sqlite3Strlen30(const char *z){
  if( z==0 ) return 0;
  return 0x3fffffff & (int)strlen(z);
}

/*
** Return the declared type of a column.  Or return zDflt if the column
** has no declared type.
**
** The column type is an extra string stored after the zero-terminator on
** the column name if and only if the COLFLAG_HASTYPE flag is set.
*/
char *sqlite3ColumnType(Column *pCol, char *zDflt){
  if( pCol->colFlags & COLFLAG_HASTYPE ){
    return pCol->zCnName + strlen(pCol->zCnName) + 1;
  }else if( pCol->eCType ){
    assert( pCol->eCType<=SQLITE_N_STDTYPE );
    return (char*)sqlite3StdType[pCol->eCType-1];
  }else{
    return zDflt;
  }
}

/*
** Helper function for sqlite3Error() - called rarely.  Broken out into
** a separate routine to avoid unnecessary register saves on entry to
** sqlite3Error().
*/
static SQLITE_NOINLINE void  sqlite3ErrorFinish(sqlite3 *db, int err_code){
  if( db->pErr ) sqlite3ValueSetNull(db->pErr);
  sqlite3SystemError(db, err_code);
}

/*
** Set the current error code to err_code and clear any prior error message.
** Also set iSysErrno (by calling sqlite3System) if the err_code indicates
** that would be appropriate.
*/
void sqlite3Error(sqlite3 *db, int err_code){
  assert( db!=0 );
  db->errCode = err_code;
  if( err_code || db->pErr ){
    sqlite3ErrorFinish(db, err_code);
  }else{
    db->errByteOffset = -1;
  }
}

/*
** The equivalent of sqlite3Error(db, SQLITE_OK).  Clear the error state
** and error message.
*/
void sqlite3ErrorClear(sqlite3 *db){
  assert( db!=0 );
  db->errCode = SQLITE_OK;
  db->errByteOffset = -1;
  if( db->pErr ) sqlite3ValueSetNull(db->pErr);
}

/*
** Load the sqlite3.iSysErrno field if that is an appropriate thing
** to do based on the SQLite error code in rc.
*/
void sqlite3SystemError(sqlite3 *db, int rc){
  if( rc==SQLITE_IOERR_NOMEM ) return;
#if defined(SQLITE_USE_SEH) && !defined(SQLITE_OMIT_WAL)
  if( rc==SQLITE_IOERR_IN_PAGE ){
    int ii;
    int iErr;
    sqlite3BtreeEnterAll(db);
    for(ii=0; ii<db->nDb; ii++){
      if( db->aDb[ii].pBt ){
        iErr = sqlite3PagerWalSystemErrno(sqlite3BtreePager(db->aDb[ii].pBt));
        if( iErr ){
          db->iSysErrno = iErr;
        }
      }
    }
    sqlite3BtreeLeaveAll(db);
    return;
  }
#endif
  rc &= 0xff;
  if( rc==SQLITE_CANTOPEN || rc==SQLITE_IOERR ){
    db->iSysErrno = sqlite3OsGetLastError(db->pVfs);
  }
}

/*
** Set the most recent error code and error string for the sqlite
** handle "db". The error code is set to "err_code".
**
** If it is not NULL, string zFormat specifies the format of the
** error string.  zFormat and any string tokens that follow it are
** assumed to be encoded in UTF-8.
**
** To clear the most recent error for sqlite handle "db", sqlite3Error
** should be called with err_code set to SQLITE_OK and zFormat set
** to NULL.
*/
void sqlite3ErrorWithMsg(sqlite3 *db, int err_code, const char *zFormat, ...){
  assert( db!=0 );
  db->errCode = err_code;
  sqlite3SystemError(db, err_code);
  if( zFormat==0 ){
    sqlite3Error(db, err_code);
  }else if( db->pErr || (db->pErr = sqlite3ValueNew(db))!=0 ){
    char *z;
    va_list ap;
    va_start(ap, zFormat);
    z = sqlite3VMPrintf(db, zFormat, ap);
    va_end(ap);
    sqlite3ValueSetStr(db->pErr, -1, z, SQLITE_UTF8, SQLITE_DYNAMIC);
  }
}

/*
** Check for interrupts and invoke progress callback.
*/
void sqlite3ProgressCheck(Parse *p){
  sqlite3 *db = p->db;
  if( AtomicLoad(&db->u1.isInterrupted) ){
    p->nErr++;
    p->rc = SQLITE_INTERRUPT;
  }
#ifndef SQLITE_OMIT_PROGRESS_CALLBACK
  if( db->xProgress ){
    if( p->rc==SQLITE_INTERRUPT ){
      p->nProgressSteps = 0;
    }else if( (++p->nProgressSteps)>=db->nProgressOps ){
      if( db->xProgress(db->pProgressArg) ){
        p->nErr++;
        p->rc = SQLITE_INTERRUPT;
      }
      p->nProgressSteps = 0;
    }
  }
#endif
}

/*
** Add an error message to pParse->zErrMsg and increment pParse->nErr.
**
** This function should be used to report any error that occurs while
** compiling an SQL statement (i.e. within sqlite3_prepare()). The
** last thing the sqlite3_prepare() function does is copy the error
** stored by this function into the database handle using sqlite3Error().
** Functions sqlite3Error() or sqlite3ErrorWithMsg() should be used
** during statement execution (sqlite3_step() etc.).
*/
void sqlite3ErrorMsg(Parse *pParse, const char *zFormat, ...){
  char *zMsg;
  va_list ap;
  sqlite3 *db = pParse->db;
  assert( db!=0 );
  assert( db->pParse==pParse || db->pParse->pToplevel==pParse );
  db->errByteOffset = -2;
  va_start(ap, zFormat);
  zMsg = sqlite3VMPrintf(db, zFormat, ap);
  va_end(ap);
  if( db->errByteOffset<-1 ) db->errByteOffset = -1;
  if( db->suppressErr ){
    sqlite3DbFree(db, zMsg);
    if( db->mallocFailed ){
      pParse->nErr++;
      pParse->rc = SQLITE_NOMEM;
    }
  }else{
    pParse->nErr++;
    sqlite3DbFree(db, pParse->zErrMsg);
    pParse->zErrMsg = zMsg;
    pParse->rc = SQLITE_ERROR;
    pParse->pWith = 0;
  }
}

/*
** If database connection db is currently parsing SQL, then transfer
** error code errCode to that parser if the parser has not already
** encountered some other kind of error.
*/
int sqlite3ErrorToParser(sqlite3 *db, int errCode){
  Parse *pParse;
  if( db==0 || (pParse = db->pParse)==0 ) return errCode;
  pParse->rc = errCode;
  pParse->nErr++;
  return errCode;
}

/*
** Convert an SQL-style quoted string into a normal string by removing
** the quote characters.  The conversion is done in-place.  If the
** input does not begin with a quote character, then this routine
** is a no-op.
**
** The input string must be zero-terminated.  A new zero-terminator
** is added to the dequoted string.
**
** The return value is -1 if no dequoting occurs or the length of the
** dequoted string, exclusive of the zero terminator, if dequoting does
** occur.
**
** 2002-02-14: This routine is extended to remove MS-Access style
** brackets from around identifiers.  For example:  "[a-b-c]" becomes
** "a-b-c".
*/
void sqlite3Dequote(char *z){
  char quote;
  int i, j;
  if( z==0 ) return;
  quote = z[0];
  if( !sqlite3Isquote(quote) ) return;
  if( quote=='[' ) quote = ']';
  for(i=1, j=0;; i++){
    assert( z[i] );
    if( z[i]==quote ){
      if( z[i+1]==quote ){
        z[j++] = quote;
        i++;
      }else{
        break;
      }
    }else{
      z[j++] = z[i];
    }
  }
  z[j] = 0;
}
void sqlite3DequoteExpr(Expr *p){
  assert( !ExprHasProperty(p, EP_IntValue) );
  assert( sqlite3Isquote(p->u.zToken[0]) );
  p->flags |= p->u.zToken[0]=='"' ? EP_Quoted|EP_DblQuoted : EP_Quoted;
  sqlite3Dequote(p->u.zToken);
}

/*
** Expression p is a QNUMBER (quoted number). Dequote the value in p->u.zToken
** and set the type to INTEGER or FLOAT. "Quoted" integers or floats are those
** that contain '_' characters that must be removed before further processing.
*/
void sqlite3DequoteNumber(Parse *pParse, Expr *p){
  assert( p!=0 || pParse->db->mallocFailed );
  if( p ){
    const char *pIn = p->u.zToken;
    char *pOut = p->u.zToken;
    int bHex = (pIn[0]=='0' && (pIn[1]=='x' || pIn[1]=='X'));
    int iValue;
    assert( p->op==TK_QNUMBER );
    p->op = TK_INTEGER;
    do {
      if( *pIn!=SQLITE_DIGIT_SEPARATOR ){
        *pOut++ = *pIn;
        if( *pIn=='e' || *pIn=='E' || *pIn=='.' ) p->op = TK_FLOAT;
      }else{
        if( (bHex==0 && (!sqlite3Isdigit(pIn[-1]) || !sqlite3Isdigit(pIn[1])))
         || (bHex==1 && (!sqlite3Isxdigit(pIn[-1]) || !sqlite3Isxdigit(pIn[1])))
        ){
          sqlite3ErrorMsg(pParse, "unrecognized token: \"%s\"", p->u.zToken);
        }
      }
    }while( *pIn++ );
    if( bHex ) p->op = TK_INTEGER;

    /* tag-20240227-a: If after dequoting, the number is an integer that
    ** fits in 32 bits, then it must be converted into EP_IntValue.  Other
    ** parts of the code expect this.  See also tag-20240227-b. */
    if( p->op==TK_INTEGER && sqlite3GetInt32(p->u.zToken, &iValue) ){
      p->u.iValue = iValue;
      p->flags |= EP_IntValue;
    }
  }
}

/*
** If the input token p is quoted, try to adjust the token to remove
** the quotes.  This is not always possible:
**
**     "abc"     ->   abc
**     "ab""cd"  ->   (not possible because of the interior "")
**
** Remove the quotes if possible.  This is a optimization.  The overall
** system should still return the correct answer even if this routine
** is always a no-op.
*/
void sqlite3DequoteToken(Token *p){
  unsigned int i;
  if( p->n<2 ) return;
  if( !sqlite3Isquote(p->z[0]) ) return;
  for(i=1; i<p->n-1; i++){
    if( sqlite3Isquote(p->z[i]) ) return;
  }
  p->n -= 2;
  p->z++;
}

/*
** Generate a Token object from a string
*/
void sqlite3TokenInit(Token *p, char *z){
  p->z = z;
  p->n = sqlite3Strlen30(z);
}

/* Convenient short-hand */
#define UpperToLower sqlite3UpperToLower

/*
** Some systems have stricmp().  Others have strcasecmp().  Because
** there is no consistency, we will define our own.
**
** IMPLEMENTATION-OF: R-30243-02494 The sqlite3_stricmp() and
** sqlite3_strnicmp() APIs allow applications and extensions to compare
** the contents of two buffers containing UTF-8 strings in a
** case-independent fashion, using the same definition of "case
** independence" that SQLite uses internally when comparing identifiers.
*/
int sqlite3_stricmp(const char *zLeft, const char *zRight){
  if( zLeft==0 ){
    return zRight ? -1 : 0;
  }else if( zRight==0 ){
    return 1;
  }
  return sqlite3StrICmp(zLeft, zRight);
}
int sqlite3StrICmp(const char *zLeft, const char *zRight){
  unsigned char *a, *b;
  int c, x;
  a = (unsigned char *)zLeft;
  b = (unsigned char *)zRight;
  for(;;){
    c = *a;
    x = *b;
    if( c==x ){
      if( c==0 ) break;
    }else{
      c = (int)UpperToLower[c] - (int)UpperToLower[x];
      if( c ) break;
    }
    a++;
    b++;
  }
  return c;
}
int sqlite3_strnicmp(const char *zLeft, const char *zRight, int N){
  register unsigned char *a, *b;
  if( zLeft==0 ){
    return zRight ? -1 : 0;
  }else if( zRight==0 ){
    return 1;
  }
  a = (unsigned char *)zLeft;
  b = (unsigned char *)zRight;
  while( N-- > 0 && *a!=0 && UpperToLower[*a]==UpperToLower[*b]){ a++; b++; }
  return N<0 ? 0 : UpperToLower[*a] - UpperToLower[*b];
}

/*
** Compute an 8-bit hash on a string that is insensitive to case differences
*/
u8 sqlite3StrIHash(const char *z){
  u8 h = 0;
  if( z==0 ) return 0;
  while( z[0] ){
    h += UpperToLower[(unsigned char)z[0]];
    z++;
  }
  return h;
}
/*
** The following array holds (approximate) powers-of-ten between
** 1.0e-348 and 1.0e+347.  Each value is an unsigned 64-bit integer,
** shifted so that its most significant bit is 1.
**
** For the power-of-ten whose value is pow(10,p), the value
** is shifted left or right in order to multiply it by
** pow(2,63-pow10to2(p)).  Hence, another way to think of the
** entries in this table is:
**
**    for p from -348 to +347:
**      int( pow(10,p)*pow(2,63-pow10to2(p)) )
**
** The int(x) function means the integer part of value x.  See
** the definition of pow10to2() below for more details about that
** function.  There is an assert() in the utility program that
** generates this table that verifies the invariant described above.
*/
static const u64 sqlite3PowerOfTen[] = {
  0xfa8fd5a0081c0288, /*   0: 1.0e-348 << 1220 */
  0x9c99e58405118195, /*   1: 1.0e-347 << 1216 */
  0xc3c05ee50655e1fa, /*   2: 1.0e-346 << 1213 */
  0xf4b0769e47eb5a78, /*   3: 1.0e-345 << 1210 */
  0x98ee4a22ecf3188b, /*   4: 1.0e-344 << 1206 */
  0xbf29dcaba82fdeae, /*   5: 1.0e-343 << 1203 */
  0xeef453d6923bd65a, /*   6: 1.0e-342 << 1200 */
  0x9558b4661b6565f8, /*   7: 1.0e-341 << 1196 */
  0xbaaee17fa23ebf76, /*   8: 1.0e-340 << 1193 */
  0xe95a99df8ace6f53, /*   9: 1.0e-339 << 1190 */
  0x91d8a02bb6c10594, /*  10: 1.0e-338 << 1186 */
  0xb64ec836a47146f9, /*  11: 1.0e-337 << 1183 */
  0xe3e27a444d8d98b7, /*  12: 1.0e-336 << 1180 */
  0x8e6d8c6ab0787f72, /*  13: 1.0e-335 << 1176 */
  0xb208ef855c969f4f, /*  14: 1.0e-334 << 1173 */
  0xde8b2b66b3bc4723, /*  15: 1.0e-333 << 1170 */
  0x8b16fb203055ac76, /*  16: 1.0e-332 << 1166 */
  0xaddcb9e83c6b1793, /*  17: 1.0e-331 << 1163 */
  0xd953e8624b85dd78, /*  18: 1.0e-330 << 1160 */
  0x87d4713d6f33aa6b, /*  19: 1.0e-329 << 1156 */
  0xa9c98d8ccb009506, /*  20: 1.0e-328 << 1153 */
  0xd43bf0effdc0ba48, /*  21: 1.0e-327 << 1150 */
  0x84a57695fe98746d, /*  22: 1.0e-326 << 1146 */
  0xa5ced43b7e3e9188, /*  23: 1.0e-325 << 1143 */
  0xcf42894a5dce35ea, /*  24: 1.0e-324 << 1140 */
  0x818995ce7aa0e1b2, /*  25: 1.0e-323 << 1136 */
  0xa1ebfb4219491a1f, /*  26: 1.0e-322 << 1133 */
  0xca66fa129f9b60a6, /*  27: 1.0e-321 << 1130 */
  0xfd00b897478238d0, /*  28: 1.0e-320 << 1127 */
  0x9e20735e8cb16382, /*  29: 1.0e-319 << 1123 */
  0xc5a890362fddbc62, /*  30: 1.0e-318 << 1120 */
  0xf712b443bbd52b7b, /*  31: 1.0e-317 << 1117 */
  0x9a6bb0aa55653b2d, /*  32: 1.0e-316 << 1113 */
  0xc1069cd4eabe89f8, /*  33: 1.0e-315 << 1110 */
  0xf148440a256e2c76, /*  34: 1.0e-314 << 1107 */
  0x96cd2a865764dbca, /*  35: 1.0e-313 << 1103 */
  0xbc807527ed3e12bc, /*  36: 1.0e-312 << 1100 */
  0xeba09271e88d976b, /*  37: 1.0e-311 << 1097 */
  0x93445b8731587ea3, /*  38: 1.0e-310 << 1093 */
  0xb8157268fdae9e4c, /*  39: 1.0e-309 << 1090 */
  0xe61acf033d1a45df, /*  40: 1.0e-308 << 1087 */
  0x8fd0c16206306bab, /*  41: 1.0e-307 << 1083 */
  0xb3c4f1ba87bc8696, /*  42: 1.0e-306 << 1080 */
  0xe0b62e2929aba83c, /*  43: 1.0e-305 << 1077 */
  0x8c71dcd9ba0b4925, /*  44: 1.0e-304 << 1073 */
  0xaf8e5410288e1b6f, /*  45: 1.0e-303 << 1070 */
  0xdb71e91432b1a24a, /*  46: 1.0e-302 << 1067 */
  0x892731ac9faf056e, /*  47: 1.0e-301 << 1063 */
  0xab70fe17c79ac6ca, /*  48: 1.0e-300 << 1060 */
  0xd64d3d9db981787d, /*  49: 1.0e-299 << 1057 */
  0x85f0468293f0eb4e, /*  50: 1.0e-298 << 1053 */
  0xa76c582338ed2621, /*  51: 1.0e-297 << 1050 */
  0xd1476e2c07286faa, /*  52: 1.0e-296 << 1047 */
  0x82cca4db847945ca, /*  53: 1.0e-295 << 1043 */
  0xa37fce126597973c, /*  54: 1.0e-294 << 1040 */
  0xcc5fc196fefd7d0c, /*  55: 1.0e-293 << 1037 */
  0xff77b1fcbebcdc4f, /*  56: 1.0e-292 << 1034 */
  0x9faacf3df73609b1, /*  57: 1.0e-291 << 1030 */
  0xc795830d75038c1d, /*  58: 1.0e-290 << 1027 */
  0xf97ae3d0d2446f25, /*  59: 1.0e-289 << 1024 */
  0x9becce62836ac577, /*  60: 1.0e-288 << 1020 */
  0xc2e801fb244576d5, /*  61: 1.0e-287 << 1017 */
  0xf3a20279ed56d48a, /*  62: 1.0e-286 << 1014 */
  0x9845418c345644d6, /*  63: 1.0e-285 << 1010 */
  0xbe5691ef416bd60c, /*  64: 1.0e-284 << 1007 */
  0xedec366b11c6cb8f, /*  65: 1.0e-283 << 1004 */
  0x94b3a202eb1c3f39, /*  66: 1.0e-282 << 1000 */
  0xb9e08a83a5e34f07, /*  67: 1.0e-281 << 997 */
  0xe858ad248f5c22c9, /*  68: 1.0e-280 << 994 */
  0x91376c36d99995be, /*  69: 1.0e-279 << 990 */
  0xb58547448ffffb2d, /*  70: 1.0e-278 << 987 */
  0xe2e69915b3fff9f9, /*  71: 1.0e-277 << 984 */
  0x8dd01fad907ffc3b, /*  72: 1.0e-276 << 980 */
  0xb1442798f49ffb4a, /*  73: 1.0e-275 << 977 */
  0xdd95317f31c7fa1d, /*  74: 1.0e-274 << 974 */
  0x8a7d3eef7f1cfc52, /*  75: 1.0e-273 << 970 */
  0xad1c8eab5ee43b66, /*  76: 1.0e-272 << 967 */
  0xd863b256369d4a40, /*  77: 1.0e-271 << 964 */
  0x873e4f75e2224e68, /*  78: 1.0e-270 << 960 */
  0xa90de3535aaae202, /*  79: 1.0e-269 << 957 */
  0xd3515c2831559a83, /*  80: 1.0e-268 << 954 */
  0x8412d9991ed58091, /*  81: 1.0e-267 << 950 */
  0xa5178fff668ae0b6, /*  82: 1.0e-266 << 947 */
  0xce5d73ff402d98e3, /*  83: 1.0e-265 << 944 */
  0x80fa687f881c7f8e, /*  84: 1.0e-264 << 940 */
  0xa139029f6a239f72, /*  85: 1.0e-263 << 937 */
  0xc987434744ac874e, /*  86: 1.0e-262 << 934 */
  0xfbe9141915d7a922, /*  87: 1.0e-261 << 931 */
  0x9d71ac8fada6c9b5, /*  88: 1.0e-260 << 927 */
  0xc4ce17b399107c22, /*  89: 1.0e-259 << 924 */
  0xf6019da07f549b2b, /*  90: 1.0e-258 << 921 */
  0x99c102844f94e0fb, /*  91: 1.0e-257 << 917 */
  0xc0314325637a1939, /*  92: 1.0e-256 << 914 */
  0xf03d93eebc589f88, /*  93: 1.0e-255 << 911 */
  0x96267c7535b763b5, /*  94: 1.0e-254 << 907 */
  0xbbb01b9283253ca2, /*  95: 1.0e-253 << 904 */
  0xea9c227723ee8bcb, /*  96: 1.0e-252 << 901 */
  0x92a1958a7675175f, /*  97: 1.0e-251 << 897 */
  0xb749faed14125d36, /*  98: 1.0e-250 << 894 */
  0xe51c79a85916f484, /*  99: 1.0e-249 << 891 */
  0x8f31cc0937ae58d2, /* 100: 1.0e-248 << 887 */
  0xb2fe3f0b8599ef07, /* 101: 1.0e-247 << 884 */
  0xdfbdcece67006ac9, /* 102: 1.0e-246 << 881 */
  0x8bd6a141006042bd, /* 103: 1.0e-245 << 877 */
  0xaecc49914078536d, /* 104: 1.0e-244 << 874 */
  0xda7f5bf590966848, /* 105: 1.0e-243 << 871 */
  0x888f99797a5e012d, /* 106: 1.0e-242 << 867 */
  0xaab37fd7d8f58178, /* 107: 1.0e-241 << 864 */
  0xd5605fcdcf32e1d6, /* 108: 1.0e-240 << 861 */
  0x855c3be0a17fcd26, /* 109: 1.0e-239 << 857 */
  0xa6b34ad8c9dfc06f, /* 110: 1.0e-238 << 854 */
  0xd0601d8efc57b08b, /* 111: 1.0e-237 << 851 */
  0x823c12795db6ce57, /* 112: 1.0e-236 << 847 */
  0xa2cb1717b52481ed, /* 113: 1.0e-235 << 844 */
  0xcb7ddcdda26da268, /* 114: 1.0e-234 << 841 */
  0xfe5d54150b090b02, /* 115: 1.0e-233 << 838 */
  0x9efa548d26e5a6e1, /* 116: 1.0e-232 << 834 */
  0xc6b8e9b0709f109a, /* 117: 1.0e-231 << 831 */
  0xf867241c8cc6d4c0, /* 118: 1.0e-230 << 828 */
  0x9b407691d7fc44f8, /* 119: 1.0e-229 << 824 */
  0xc21094364dfb5636, /* 120: 1.0e-228 << 821 */
  0xf294b943e17a2bc4, /* 121: 1.0e-227 << 818 */
  0x979cf3ca6cec5b5a, /* 122: 1.0e-226 << 814 */
  0xbd8430bd08277231, /* 123: 1.0e-225 << 811 */
  0xece53cec4a314ebd, /* 124: 1.0e-224 << 808 */
  0x940f4613ae5ed136, /* 125: 1.0e-223 << 804 */
  0xb913179899f68584, /* 126: 1.0e-222 << 801 */
  0xe757dd7ec07426e5, /* 127: 1.0e-221 << 798 */
  0x9096ea6f3848984f, /* 128: 1.0e-220 << 794 */
  0xb4bca50b065abe63, /* 129: 1.0e-219 << 791 */
  0xe1ebce4dc7f16dfb, /* 130: 1.0e-218 << 788 */
  0x8d3360f09cf6e4bd, /* 131: 1.0e-217 << 784 */
  0xb080392cc4349dec, /* 132: 1.0e-216 << 781 */
  0xdca04777f541c567, /* 133: 1.0e-215 << 778 */
  0x89e42caaf9491b60, /* 134: 1.0e-214 << 774 */
  0xac5d37d5b79b6239, /* 135: 1.0e-213 << 771 */
  0xd77485cb25823ac7, /* 136: 1.0e-212 << 768 */
  0x86a8d39ef77164bc, /* 137: 1.0e-211 << 764 */
  0xa8530886b54dbdeb, /* 138: 1.0e-210 << 761 */
  0xd267caa862a12d66, /* 139: 1.0e-209 << 758 */
  0x8380dea93da4bc60, /* 140: 1.0e-208 << 754 */
  0xa46116538d0deb78, /* 141: 1.0e-207 << 751 */
  0xcd795be870516656, /* 142: 1.0e-206 << 748 */
  0x806bd9714632dff6, /* 143: 1.0e-205 << 744 */
  0xa086cfcd97bf97f3, /* 144: 1.0e-204 << 741 */
  0xc8a883c0fdaf7df0, /* 145: 1.0e-203 << 738 */
  0xfad2a4b13d1b5d6c, /* 146: 1.0e-202 << 735 */
  0x9cc3a6eec6311a63, /* 147: 1.0e-201 << 731 */
  0xc3f490aa77bd60fc, /* 148: 1.0e-200 << 728 */
  0xf4f1b4d515acb93b, /* 149: 1.0e-199 << 725 */
  0x991711052d8bf3c5, /* 150: 1.0e-198 << 721 */
  0xbf5cd54678eef0b6, /* 151: 1.0e-197 << 718 */
  0xef340a98172aace4, /* 152: 1.0e-196 << 715 */
  0x9580869f0e7aac0e, /* 153: 1.0e-195 << 711 */
  0xbae0a846d2195712, /* 154: 1.0e-194 << 708 */
  0xe998d258869facd7, /* 155: 1.0e-193 << 705 */
  0x91ff83775423cc06, /* 156: 1.0e-192 << 701 */
  0xb67f6455292cbf08, /* 157: 1.0e-191 << 698 */
  0xe41f3d6a7377eeca, /* 158: 1.0e-190 << 695 */
  0x8e938662882af53e, /* 159: 1.0e-189 << 691 */
  0xb23867fb2a35b28d, /* 160: 1.0e-188 << 688 */
  0xdec681f9f4c31f31, /* 161: 1.0e-187 << 685 */
  0x8b3c113c38f9f37e, /* 162: 1.0e-186 << 681 */
  0xae0b158b4738705e, /* 163: 1.0e-185 << 678 */
  0xd98ddaee19068c76, /* 164: 1.0e-184 << 675 */
  0x87f8a8d4cfa417c9, /* 165: 1.0e-183 << 671 */
  0xa9f6d30a038d1dbc, /* 166: 1.0e-182 << 668 */
  0xd47487cc8470652b, /* 167: 1.0e-181 << 665 */
  0x84c8d4dfd2c63f3b, /* 168: 1.0e-180 << 661 */
  0xa5fb0a17c777cf09, /* 169: 1.0e-179 << 658 */
  0xcf79cc9db955c2cc, /* 170: 1.0e-178 << 655 */
  0x81ac1fe293d599bf, /* 171: 1.0e-177 << 651 */
  0xa21727db38cb002f, /* 172: 1.0e-176 << 648 */
  0xca9cf1d206fdc03b, /* 173: 1.0e-175 << 645 */
  0xfd442e4688bd304a, /* 174: 1.0e-174 << 642 */
  0x9e4a9cec15763e2e, /* 175: 1.0e-173 << 638 */
  0xc5dd44271ad3cdba, /* 176: 1.0e-172 << 635 */
  0xf7549530e188c128, /* 177: 1.0e-171 << 632 */
  0x9a94dd3e8cf578b9, /* 178: 1.0e-170 << 628 */
  0xc13a148e3032d6e7, /* 179: 1.0e-169 << 625 */
  0xf18899b1bc3f8ca1, /* 180: 1.0e-168 << 622 */
  0x96f5600f15a7b7e5, /* 181: 1.0e-167 << 618 */
  0xbcb2b812db11a5de, /* 182: 1.0e-166 << 615 */
  0xebdf661791d60f56, /* 183: 1.0e-165 << 612 */
  0x936b9fcebb25c995, /* 184: 1.0e-164 << 608 */
  0xb84687c269ef3bfb, /* 185: 1.0e-163 << 605 */
  0xe65829b3046b0afa, /* 186: 1.0e-162 << 602 */
  0x8ff71a0fe2c2e6dc, /* 187: 1.0e-161 << 598 */
  0xb3f4e093db73a093, /* 188: 1.0e-160 << 595 */
  0xe0f218b8d25088b8, /* 189: 1.0e-159 << 592 */
  0x8c974f7383725573, /* 190: 1.0e-158 << 588 */
  0xafbd2350644eeacf, /* 191: 1.0e-157 << 585 */
  0xdbac6c247d62a583, /* 192: 1.0e-156 << 582 */
  0x894bc396ce5da772, /* 193: 1.0e-155 << 578 */
  0xab9eb47c81f5114f, /* 194: 1.0e-154 << 575 */
  0xd686619ba27255a2, /* 195: 1.0e-153 << 572 */
  0x8613fd0145877585, /* 196: 1.0e-152 << 568 */
  0xa798fc4196e952e7, /* 197: 1.0e-151 << 565 */
  0xd17f3b51fca3a7a0, /* 198: 1.0e-150 << 562 */
  0x82ef85133de648c4, /* 199: 1.0e-149 << 558 */
  0xa3ab66580d5fdaf5, /* 200: 1.0e-148 << 555 */
  0xcc963fee10b7d1b3, /* 201: 1.0e-147 << 552 */
  0xffbbcfe994e5c61f, /* 202: 1.0e-146 << 549 */
  0x9fd561f1fd0f9bd3, /* 203: 1.0e-145 << 545 */
  0xc7caba6e7c5382c8, /* 204: 1.0e-144 << 542 */
  0xf9bd690a1b68637b, /* 205: 1.0e-143 << 539 */
  0x9c1661a651213e2d, /* 206: 1.0e-142 << 535 */
  0xc31bfa0fe5698db8, /* 207: 1.0e-141 << 532 */
  0xf3e2f893dec3f126, /* 208: 1.0e-140 << 529 */
  0x986ddb5c6b3a76b7, /* 209: 1.0e-139 << 525 */
  0xbe89523386091465, /* 210: 1.0e-138 << 522 */
  0xee2ba6c0678b597f, /* 211: 1.0e-137 << 519 */
  0x94db483840b717ef, /* 212: 1.0e-136 << 515 */
  0xba121a4650e4ddeb, /* 213: 1.0e-135 << 512 */
  0xe896a0d7e51e1566, /* 214: 1.0e-134 << 509 */
  0x915e2486ef32cd60, /* 215: 1.0e-133 << 505 */
  0xb5b5ada8aaff80b8, /* 216: 1.0e-132 << 502 */
  0xe3231912d5bf60e6, /* 217: 1.0e-131 << 499 */
  0x8df5efabc5979c8f, /* 218: 1.0e-130 << 495 */
  0xb1736b96b6fd83b3, /* 219: 1.0e-129 << 492 */
  0xddd0467c64bce4a0, /* 220: 1.0e-128 << 489 */
  0x8aa22c0dbef60ee4, /* 221: 1.0e-127 << 485 */
  0xad4ab7112eb3929d, /* 222: 1.0e-126 << 482 */
  0xd89d64d57a607744, /* 223: 1.0e-125 << 479 */
  0x87625f056c7c4a8b, /* 224: 1.0e-124 << 475 */
  0xa93af6c6c79b5d2d, /* 225: 1.0e-123 << 472 */
  0xd389b47879823479, /* 226: 1.0e-122 << 469 */
  0x843610cb4bf160cb, /* 227: 1.0e-121 << 465 */
  0xa54394fe1eedb8fe, /* 228: 1.0e-120 << 462 */
  0xce947a3da6a9273e, /* 229: 1.0e-119 << 459 */
  0x811ccc668829b887, /* 230: 1.0e-118 << 455 */
  0xa163ff802a3426a8, /* 231: 1.0e-117 << 452 */
  0xc9bcff6034c13052, /* 232: 1.0e-116 << 449 */
  0xfc2c3f3841f17c67, /* 233: 1.0e-115 << 446 */
  0x9d9ba7832936edc0, /* 234: 1.0e-114 << 442 */
  0xc5029163f384a931, /* 235: 1.0e-113 << 439 */
  0xf64335bcf065d37d, /* 236: 1.0e-112 << 436 */
  0x99ea0196163fa42e, /* 237: 1.0e-111 << 432 */
  0xc06481fb9bcf8d39, /* 238: 1.0e-110 << 429 */
  0xf07da27a82c37088, /* 239: 1.0e-109 << 426 */
  0x964e858c91ba2655, /* 240: 1.0e-108 << 422 */
  0xbbe226efb628afea, /* 241: 1.0e-107 << 419 */
  0xeadab0aba3b2dbe5, /* 242: 1.0e-106 << 416 */
  0x92c8ae6b464fc96f, /* 243: 1.0e-105 << 412 */
  0xb77ada0617e3bbcb, /* 244: 1.0e-104 << 409 */
  0xe55990879ddcaabd, /* 245: 1.0e-103 << 406 */
  0x8f57fa54c2a9eab6, /* 246: 1.0e-102 << 402 */
  0xb32df8e9f3546564, /* 247: 1.0e-101 << 399 */
  0xdff9772470297ebd, /* 248: 1.0e-100 << 396 */
  0x8bfbea76c619ef36, /* 249: 1.0e-99 << 392 */
  0xaefae51477a06b03, /* 250: 1.0e-98 << 389 */
  0xdab99e59958885c4, /* 251: 1.0e-97 << 386 */
  0x88b402f7fd75539b, /* 252: 1.0e-96 << 382 */
  0xaae103b5fcd2a881, /* 253: 1.0e-95 << 379 */
  0xd59944a37c0752a2, /* 254: 1.0e-94 << 376 */
  0x857fcae62d8493a5, /* 255: 1.0e-93 << 372 */
  0xa6dfbd9fb8e5b88e, /* 256: 1.0e-92 << 369 */
  0xd097ad07a71f26b2, /* 257: 1.0e-91 << 366 */
  0x825ecc24c873782f, /* 258: 1.0e-90 << 362 */
  0xa2f67f2dfa90563b, /* 259: 1.0e-89 << 359 */
  0xcbb41ef979346bca, /* 260: 1.0e-88 << 356 */
  0xfea126b7d78186bc, /* 261: 1.0e-87 << 353 */
  0x9f24b832e6b0f436, /* 262: 1.0e-86 << 349 */
  0xc6ede63fa05d3143, /* 263: 1.0e-85 << 346 */
  0xf8a95fcf88747d94, /* 264: 1.0e-84 << 343 */
  0x9b69dbe1b548ce7c, /* 265: 1.0e-83 << 339 */
  0xc24452da229b021b, /* 266: 1.0e-82 << 336 */
  0xf2d56790ab41c2a2, /* 267: 1.0e-81 << 333 */
  0x97c560ba6b0919a5, /* 268: 1.0e-80 << 329 */
  0xbdb6b8e905cb600f, /* 269: 1.0e-79 << 326 */
  0xed246723473e3813, /* 270: 1.0e-78 << 323 */
  0x9436c0760c86e30b, /* 271: 1.0e-77 << 319 */
  0xb94470938fa89bce, /* 272: 1.0e-76 << 316 */
  0xe7958cb87392c2c2, /* 273: 1.0e-75 << 313 */
  0x90bd77f3483bb9b9, /* 274: 1.0e-74 << 309 */
  0xb4ecd5f01a4aa828, /* 275: 1.0e-73 << 306 */
  0xe2280b6c20dd5232, /* 276: 1.0e-72 << 303 */
  0x8d590723948a535f, /* 277: 1.0e-71 << 299 */
  0xb0af48ec79ace837, /* 278: 1.0e-70 << 296 */
  0xdcdb1b2798182244, /* 279: 1.0e-69 << 293 */
  0x8a08f0f8bf0f156b, /* 280: 1.0e-68 << 289 */
  0xac8b2d36eed2dac5, /* 281: 1.0e-67 << 286 */
  0xd7adf884aa879177, /* 282: 1.0e-66 << 283 */
  0x86ccbb52ea94baea, /* 283: 1.0e-65 << 279 */
  0xa87fea27a539e9a5, /* 284: 1.0e-64 << 276 */
  0xd29fe4b18e88640e, /* 285: 1.0e-63 << 273 */
  0x83a3eeeef9153e89, /* 286: 1.0e-62 << 269 */
  0xa48ceaaab75a8e2b, /* 287: 1.0e-61 << 266 */
  0xcdb02555653131b6, /* 288: 1.0e-60 << 263 */
  0x808e17555f3ebf11, /* 289: 1.0e-59 << 259 */
  0xa0b19d2ab70e6ed6, /* 290: 1.0e-58 << 256 */
  0xc8de047564d20a8b, /* 291: 1.0e-57 << 253 */
  0xfb158592be068d2e, /* 292: 1.0e-56 << 250 */
  0x9ced737bb6c4183d, /* 293: 1.0e-55 << 246 */
  0xc428d05aa4751e4c, /* 294: 1.0e-54 << 243 */
  0xf53304714d9265df, /* 295: 1.0e-53 << 240 */
  0x993fe2c6d07b7fab, /* 296: 1.0e-52 << 236 */
  0xbf8fdb78849a5f96, /* 297: 1.0e-51 << 233 */
  0xef73d256a5c0f77c, /* 298: 1.0e-50 << 230 */
  0x95a8637627989aad, /* 299: 1.0e-49 << 226 */
  0xbb127c53b17ec159, /* 300: 1.0e-48 << 223 */
  0xe9d71b689dde71af, /* 301: 1.0e-47 << 220 */
  0x9226712162ab070d, /* 302: 1.0e-46 << 216 */
  0xb6b00d69bb55c8d1, /* 303: 1.0e-45 << 213 */
  0xe45c10c42a2b3b05, /* 304: 1.0e-44 << 210 */
  0x8eb98a7a9a5b04e3, /* 305: 1.0e-43 << 206 */
  0xb267ed1940f1c61c, /* 306: 1.0e-42 << 203 */
  0xdf01e85f912e37a3, /* 307: 1.0e-41 << 200 */
  0x8b61313bbabce2c6, /* 308: 1.0e-40 << 196 */
  0xae397d8aa96c1b77, /* 309: 1.0e-39 << 193 */
  0xd9c7dced53c72255, /* 310: 1.0e-38 << 190 */
  0x881cea14545c7575, /* 311: 1.0e-37 << 186 */
  0xaa242499697392d2, /* 312: 1.0e-36 << 183 */
  0xd4ad2dbfc3d07787, /* 313: 1.0e-35 << 180 */
  0x84ec3c97da624ab4, /* 314: 1.0e-34 << 176 */
  0xa6274bbdd0fadd61, /* 315: 1.0e-33 << 173 */
  0xcfb11ead453994ba, /* 316: 1.0e-32 << 170 */
  0x81ceb32c4b43fcf4, /* 317: 1.0e-31 << 166 */
  0xa2425ff75e14fc31, /* 318: 1.0e-30 << 163 */
  0xcad2f7f5359a3b3e, /* 319: 1.0e-29 << 160 */
  0xfd87b5f28300ca0d, /* 320: 1.0e-28 << 157 */
  0x9e74d1b791e07e48, /* 321: 1.0e-27 << 153 */
  0xc612062576589dda, /* 322: 1.0e-26 << 150 */
  0xf79687aed3eec551, /* 323: 1.0e-25 << 147 */
  0x9abe14cd44753b52, /* 324: 1.0e-24 << 143 */
  0xc16d9a0095928a27, /* 325: 1.0e-23 << 140 */
  0xf1c90080baf72cb1, /* 326: 1.0e-22 << 137 */
  0x971da05074da7bee, /* 327: 1.0e-21 << 133 */
  0xbce5086492111aea, /* 328: 1.0e-20 << 130 */
  0xec1e4a7db69561a5, /* 329: 1.0e-19 << 127 */
  0x9392ee8e921d5d07, /* 330: 1.0e-18 << 123 */
  0xb877aa3236a4b449, /* 331: 1.0e-17 << 120 */
  0xe69594bec44de15b, /* 332: 1.0e-16 << 117 */
  0x901d7cf73ab0acd9, /* 333: 1.0e-15 << 113 */
  0xb424dc35095cd80f, /* 334: 1.0e-14 << 110 */
  0xe12e13424bb40e13, /* 335: 1.0e-13 << 107 */
  0x8cbccc096f5088cb, /* 336: 1.0e-12 << 103 */
  0xafebff0bcb24aafe, /* 337: 1.0e-11 << 100 */
  0xdbe6fecebdedd5be, /* 338: 1.0e-10 << 97 */
  0x89705f4136b4a597, /* 339: 1.0e-9 << 93 */
  0xabcc77118461cefc, /* 340: 1.0e-8 << 90 */
  0xd6bf94d5e57a42bc, /* 341: 1.0e-7 << 87 */
  0x8637bd05af6c69b5, /* 342: 1.0e-6 << 83 */
  0xa7c5ac471b478423, /* 343: 1.0e-5 << 80 */
  0xd1b71758e219652b, /* 344: 1.0e-4 << 77 */
  0x83126e978d4fdf3b, /* 345: 1.0e-3 << 73 */
  0xa3d70a3d70a3d70a, /* 346: 1.0e-2 << 70 */
  0xcccccccccccccccc, /* 347: 1.0e-1 << 67 */
  0x8000000000000000, /* 348: 1.0e+0 << 63 */
  0xa000000000000000, /* 349: 1.0e+1 << 60 */
  0xc800000000000000, /* 350: 1.0e+2 << 57 */
  0xfa00000000000000, /* 351: 1.0e+3 << 54 */
  0x9c40000000000000, /* 352: 1.0e+4 << 50 */
  0xc350000000000000, /* 353: 1.0e+5 << 47 */
  0xf424000000000000, /* 354: 1.0e+6 << 44 */
  0x9896800000000000, /* 355: 1.0e+7 << 40 */
  0xbebc200000000000, /* 356: 1.0e+8 << 37 */
  0xee6b280000000000, /* 357: 1.0e+9 << 34 */
  0x9502f90000000000, /* 358: 1.0e+10 << 30 */
  0xba43b74000000000, /* 359: 1.0e+11 << 27 */
  0xe8d4a51000000000, /* 360: 1.0e+12 << 24 */
  0x9184e72a00000000, /* 361: 1.0e+13 << 20 */
  0xb5e620f480000000, /* 362: 1.0e+14 << 17 */
  0xe35fa931a0000000, /* 363: 1.0e+15 << 14 */
  0x8e1bc9bf04000000, /* 364: 1.0e+16 << 10 */
  0xb1a2bc2ec5000000, /* 365: 1.0e+17 << 7 */
  0xde0b6b3a76400000, /* 366: 1.0e+18 << 4 */
  0x8ac7230489e80000, /* 367: 1.0e+19 >> 0 */
  0xad78ebc5ac620000, /* 368: 1.0e+20 >> 3 */
  0xd8d726b7177a8000, /* 369: 1.0e+21 >> 6 */
  0x878678326eac9000, /* 370: 1.0e+22 >> 10 */
  0xa968163f0a57b400, /* 371: 1.0e+23 >> 13 */
  0xd3c21bcecceda100, /* 372: 1.0e+24 >> 16 */
  0x84595161401484a0, /* 373: 1.0e+25 >> 20 */
  0xa56fa5b99019a5c8, /* 374: 1.0e+26 >> 23 */
  0xcecb8f27f4200f3a, /* 375: 1.0e+27 >> 26 */
  0x813f3978f8940984, /* 376: 1.0e+28 >> 30 */
  0xa18f07d736b90be5, /* 377: 1.0e+29 >> 33 */
  0xc9f2c9cd04674ede, /* 378: 1.0e+30 >> 36 */
  0xfc6f7c4045812296, /* 379: 1.0e+31 >> 39 */
  0x9dc5ada82b70b59d, /* 380: 1.0e+32 >> 43 */
  0xc5371912364ce305, /* 381: 1.0e+33 >> 46 */
  0xf684df56c3e01bc6, /* 382: 1.0e+34 >> 49 */
  0x9a130b963a6c115c, /* 383: 1.0e+35 >> 53 */
  0xc097ce7bc90715b3, /* 384: 1.0e+36 >> 56 */
  0xf0bdc21abb48db20, /* 385: 1.0e+37 >> 59 */
  0x96769950b50d88f4, /* 386: 1.0e+38 >> 63 */
  0xbc143fa4e250eb31, /* 387: 1.0e+39 >> 66 */
  0xeb194f8e1ae525fd, /* 388: 1.0e+40 >> 69 */
  0x92efd1b8d0cf37be, /* 389: 1.0e+41 >> 73 */
  0xb7abc627050305ad, /* 390: 1.0e+42 >> 76 */
  0xe596b7b0c643c719, /* 391: 1.0e+43 >> 79 */
  0x8f7e32ce7bea5c6f, /* 392: 1.0e+44 >> 83 */
  0xb35dbf821ae4f38b, /* 393: 1.0e+45 >> 86 */
  0xe0352f62a19e306e, /* 394: 1.0e+46 >> 89 */
  0x8c213d9da502de45, /* 395: 1.0e+47 >> 93 */
  0xaf298d050e4395d6, /* 396: 1.0e+48 >> 96 */
  0xdaf3f04651d47b4c, /* 397: 1.0e+49 >> 99 */
  0x88d8762bf324cd0f, /* 398: 1.0e+50 >> 103 */
  0xab0e93b6efee0053, /* 399: 1.0e+51 >> 106 */
  0xd5d238a4abe98068, /* 400: 1.0e+52 >> 109 */
  0x85a36366eb71f041, /* 401: 1.0e+53 >> 113 */
  0xa70c3c40a64e6c51, /* 402: 1.0e+54 >> 116 */
  0xd0cf4b50cfe20765, /* 403: 1.0e+55 >> 119 */
  0x82818f1281ed449f, /* 404: 1.0e+56 >> 123 */
  0xa321f2d7226895c7, /* 405: 1.0e+57 >> 126 */
  0xcbea6f8ceb02bb39, /* 406: 1.0e+58 >> 129 */
  0xfee50b7025c36a08, /* 407: 1.0e+59 >> 132 */
  0x9f4f2726179a2245, /* 408: 1.0e+60 >> 136 */
  0xc722f0ef9d80aad6, /* 409: 1.0e+61 >> 139 */
  0xf8ebad2b84e0d58b, /* 410: 1.0e+62 >> 142 */
  0x9b934c3b330c8577, /* 411: 1.0e+63 >> 146 */
  0xc2781f49ffcfa6d5, /* 412: 1.0e+64 >> 149 */
  0xf316271c7fc3908a, /* 413: 1.0e+65 >> 152 */
  0x97edd871cfda3a56, /* 414: 1.0e+66 >> 156 */
  0xbde94e8e43d0c8ec, /* 415: 1.0e+67 >> 159 */
  0xed63a231d4c4fb27, /* 416: 1.0e+68 >> 162 */
  0x945e455f24fb1cf8, /* 417: 1.0e+69 >> 166 */
  0xb975d6b6ee39e436, /* 418: 1.0e+70 >> 169 */
  0xe7d34c64a9c85d44, /* 419: 1.0e+71 >> 172 */
  0x90e40fbeea1d3a4a, /* 420: 1.0e+72 >> 176 */
  0xb51d13aea4a488dd, /* 421: 1.0e+73 >> 179 */
  0xe264589a4dcdab14, /* 422: 1.0e+74 >> 182 */
  0x8d7eb76070a08aec, /* 423: 1.0e+75 >> 186 */
  0xb0de65388cc8ada8, /* 424: 1.0e+76 >> 189 */
  0xdd15fe86affad912, /* 425: 1.0e+77 >> 192 */
  0x8a2dbf142dfcc7ab, /* 426: 1.0e+78 >> 196 */
  0xacb92ed9397bf996, /* 427: 1.0e+79 >> 199 */
  0xd7e77a8f87daf7fb, /* 428: 1.0e+80 >> 202 */
  0x86f0ac99b4e8dafd, /* 429: 1.0e+81 >> 206 */
  0xa8acd7c0222311bc, /* 430: 1.0e+82 >> 209 */
  0xd2d80db02aabd62b, /* 431: 1.0e+83 >> 212 */
  0x83c7088e1aab65db, /* 432: 1.0e+84 >> 216 */
  0xa4b8cab1a1563f52, /* 433: 1.0e+85 >> 219 */
  0xcde6fd5e09abcf26, /* 434: 1.0e+86 >> 222 */
  0x80b05e5ac60b6178, /* 435: 1.0e+87 >> 226 */
  0xa0dc75f1778e39d6, /* 436: 1.0e+88 >> 229 */
  0xc913936dd571c84c, /* 437: 1.0e+89 >> 232 */
  0xfb5878494ace3a5f, /* 438: 1.0e+90 >> 235 */
  0x9d174b2dcec0e47b, /* 439: 1.0e+91 >> 239 */
  0xc45d1df942711d9a, /* 440: 1.0e+92 >> 242 */
  0xf5746577930d6500, /* 441: 1.0e+93 >> 245 */
  0x9968bf6abbe85f20, /* 442: 1.0e+94 >> 249 */
  0xbfc2ef456ae276e8, /* 443: 1.0e+95 >> 252 */
  0xefb3ab16c59b14a2, /* 444: 1.0e+96 >> 255 */
  0x95d04aee3b80ece5, /* 445: 1.0e+97 >> 259 */
  0xbb445da9ca61281f, /* 446: 1.0e+98 >> 262 */
  0xea1575143cf97226, /* 447: 1.0e+99 >> 265 */
  0x924d692ca61be758, /* 448: 1.0e+100 >> 269 */
  0xb6e0c377cfa2e12e, /* 449: 1.0e+101 >> 272 */
  0xe498f455c38b997a, /* 450: 1.0e+102 >> 275 */
  0x8edf98b59a373fec, /* 451: 1.0e+103 >> 279 */
  0xb2977ee300c50fe7, /* 452: 1.0e+104 >> 282 */
  0xdf3d5e9bc0f653e1, /* 453: 1.0e+105 >> 285 */
  0x8b865b215899f46c, /* 454: 1.0e+106 >> 289 */
  0xae67f1e9aec07187, /* 455: 1.0e+107 >> 292 */
  0xda01ee641a708de9, /* 456: 1.0e+108 >> 295 */
  0x884134fe908658b2, /* 457: 1.0e+109 >> 299 */
  0xaa51823e34a7eede, /* 458: 1.0e+110 >> 302 */
  0xd4e5e2cdc1d1ea96, /* 459: 1.0e+111 >> 305 */
  0x850fadc09923329e, /* 460: 1.0e+112 >> 309 */
  0xa6539930bf6bff45, /* 461: 1.0e+113 >> 312 */
  0xcfe87f7cef46ff16, /* 462: 1.0e+114 >> 315 */
  0x81f14fae158c5f6e, /* 463: 1.0e+115 >> 319 */
  0xa26da3999aef7749, /* 464: 1.0e+116 >> 322 */
  0xcb090c8001ab551c, /* 465: 1.0e+117 >> 325 */
  0xfdcb4fa002162a63, /* 466: 1.0e+118 >> 328 */
  0x9e9f11c4014dda7e, /* 467: 1.0e+119 >> 332 */
  0xc646d63501a1511d, /* 468: 1.0e+120 >> 335 */
  0xf7d88bc24209a565, /* 469: 1.0e+121 >> 338 */
  0x9ae757596946075f, /* 470: 1.0e+122 >> 342 */
  0xc1a12d2fc3978937, /* 471: 1.0e+123 >> 345 */
  0xf209787bb47d6b84, /* 472: 1.0e+124 >> 348 */
  0x9745eb4d50ce6332, /* 473: 1.0e+125 >> 352 */
  0xbd176620a501fbff, /* 474: 1.0e+126 >> 355 */
  0xec5d3fa8ce427aff, /* 475: 1.0e+127 >> 358 */
  0x93ba47c980e98cdf, /* 476: 1.0e+128 >> 362 */
  0xb8a8d9bbe123f017, /* 477: 1.0e+129 >> 365 */
  0xe6d3102ad96cec1d, /* 478: 1.0e+130 >> 368 */
  0x9043ea1ac7e41392, /* 479: 1.0e+131 >> 372 */
  0xb454e4a179dd1877, /* 480: 1.0e+132 >> 375 */
  0xe16a1dc9d8545e94, /* 481: 1.0e+133 >> 378 */
  0x8ce2529e2734bb1d, /* 482: 1.0e+134 >> 382 */
  0xb01ae745b101e9e4, /* 483: 1.0e+135 >> 385 */
  0xdc21a1171d42645d, /* 484: 1.0e+136 >> 388 */
  0x899504ae72497eba, /* 485: 1.0e+137 >> 392 */
  0xabfa45da0edbde69, /* 486: 1.0e+138 >> 395 */
  0xd6f8d7509292d603, /* 487: 1.0e+139 >> 398 */
  0x865b86925b9bc5c2, /* 488: 1.0e+140 >> 402 */
  0xa7f26836f282b732, /* 489: 1.0e+141 >> 405 */
  0xd1ef0244af2364ff, /* 490: 1.0e+142 >> 408 */
  0x8335616aed761f1f, /* 491: 1.0e+143 >> 412 */
  0xa402b9c5a8d3a6e7, /* 492: 1.0e+144 >> 415 */
  0xcd036837130890a1, /* 493: 1.0e+145 >> 418 */
  0x802221226be55a64, /* 494: 1.0e+146 >> 422 */
  0xa02aa96b06deb0fd, /* 495: 1.0e+147 >> 425 */
  0xc83553c5c8965d3d, /* 496: 1.0e+148 >> 428 */
  0xfa42a8b73abbf48c, /* 497: 1.0e+149 >> 431 */
  0x9c69a97284b578d7, /* 498: 1.0e+150 >> 435 */
  0xc38413cf25e2d70d, /* 499: 1.0e+151 >> 438 */
  0xf46518c2ef5b8cd1, /* 500: 1.0e+152 >> 441 */
  0x98bf2f79d5993802, /* 501: 1.0e+153 >> 445 */
  0xbeeefb584aff8603, /* 502: 1.0e+154 >> 448 */
  0xeeaaba2e5dbf6784, /* 503: 1.0e+155 >> 451 */
  0x952ab45cfa97a0b2, /* 504: 1.0e+156 >> 455 */
  0xba756174393d88df, /* 505: 1.0e+157 >> 458 */
  0xe912b9d1478ceb17, /* 506: 1.0e+158 >> 461 */
  0x91abb422ccb812ee, /* 507: 1.0e+159 >> 465 */
  0xb616a12b7fe617aa, /* 508: 1.0e+160 >> 468 */
  0xe39c49765fdf9d94, /* 509: 1.0e+161 >> 471 */
  0x8e41ade9fbebc27d, /* 510: 1.0e+162 >> 475 */
  0xb1d219647ae6b31c, /* 511: 1.0e+163 >> 478 */
  0xde469fbd99a05fe3, /* 512: 1.0e+164 >> 481 */
  0x8aec23d680043bee, /* 513: 1.0e+165 >> 485 */
  0xada72ccc20054ae9, /* 514: 1.0e+166 >> 488 */
  0xd910f7ff28069da4, /* 515: 1.0e+167 >> 491 */
  0x87aa9aff79042286, /* 516: 1.0e+168 >> 495 */
  0xa99541bf57452b28, /* 517: 1.0e+169 >> 498 */
  0xd3fa922f2d1675f2, /* 518: 1.0e+170 >> 501 */
  0x847c9b5d7c2e09b7, /* 519: 1.0e+171 >> 505 */
  0xa59bc234db398c25, /* 520: 1.0e+172 >> 508 */
  0xcf02b2c21207ef2e, /* 521: 1.0e+173 >> 511 */
  0x8161afb94b44f57d, /* 522: 1.0e+174 >> 515 */
  0xa1ba1ba79e1632dc, /* 523: 1.0e+175 >> 518 */
  0xca28a291859bbf93, /* 524: 1.0e+176 >> 521 */
  0xfcb2cb35e702af78, /* 525: 1.0e+177 >> 524 */
  0x9defbf01b061adab, /* 526: 1.0e+178 >> 528 */
  0xc56baec21c7a1916, /* 527: 1.0e+179 >> 531 */
  0xf6c69a72a3989f5b, /* 528: 1.0e+180 >> 534 */
  0x9a3c2087a63f6399, /* 529: 1.0e+181 >> 538 */
  0xc0cb28a98fcf3c7f, /* 530: 1.0e+182 >> 541 */
  0xf0fdf2d3f3c30b9f, /* 531: 1.0e+183 >> 544 */
  0x969eb7c47859e743, /* 532: 1.0e+184 >> 548 */
  0xbc4665b596706114, /* 533: 1.0e+185 >> 551 */
  0xeb57ff22fc0c7959, /* 534: 1.0e+186 >> 554 */
  0x9316ff75dd87cbd8, /* 535: 1.0e+187 >> 558 */
  0xb7dcbf5354e9bece, /* 536: 1.0e+188 >> 561 */
  0xe5d3ef282a242e81, /* 537: 1.0e+189 >> 564 */
  0x8fa475791a569d10, /* 538: 1.0e+190 >> 568 */
  0xb38d92d760ec4455, /* 539: 1.0e+191 >> 571 */
  0xe070f78d3927556a, /* 540: 1.0e+192 >> 574 */
  0x8c469ab843b89562, /* 541: 1.0e+193 >> 578 */
  0xaf58416654a6babb, /* 542: 1.0e+194 >> 581 */
  0xdb2e51bfe9d0696a, /* 543: 1.0e+195 >> 584 */
  0x88fcf317f22241e2, /* 544: 1.0e+196 >> 588 */
  0xab3c2fddeeaad25a, /* 545: 1.0e+197 >> 591 */
  0xd60b3bd56a5586f1, /* 546: 1.0e+198 >> 594 */
  0x85c7056562757456, /* 547: 1.0e+199 >> 598 */
  0xa738c6bebb12d16c, /* 548: 1.0e+200 >> 601 */
  0xd106f86e69d785c7, /* 549: 1.0e+201 >> 604 */
  0x82a45b450226b39c, /* 550: 1.0e+202 >> 608 */
  0xa34d721642b06084, /* 551: 1.0e+203 >> 611 */
  0xcc20ce9bd35c78a5, /* 552: 1.0e+204 >> 614 */
  0xff290242c83396ce, /* 553: 1.0e+205 >> 617 */
  0x9f79a169bd203e41, /* 554: 1.0e+206 >> 621 */
  0xc75809c42c684dd1, /* 555: 1.0e+207 >> 624 */
  0xf92e0c3537826145, /* 556: 1.0e+208 >> 627 */
  0x9bbcc7a142b17ccb, /* 557: 1.0e+209 >> 631 */
  0xc2abf989935ddbfe, /* 558: 1.0e+210 >> 634 */
  0xf356f7ebf83552fe, /* 559: 1.0e+211 >> 637 */
  0x98165af37b2153de, /* 560: 1.0e+212 >> 641 */
  0xbe1bf1b059e9a8d6, /* 561: 1.0e+213 >> 644 */
  0xeda2ee1c7064130c, /* 562: 1.0e+214 >> 647 */
  0x9485d4d1c63e8be7, /* 563: 1.0e+215 >> 651 */
  0xb9a74a0637ce2ee1, /* 564: 1.0e+216 >> 654 */
  0xe8111c87c5c1ba99, /* 565: 1.0e+217 >> 657 */
  0x910ab1d4db9914a0, /* 566: 1.0e+218 >> 661 */
  0xb54d5e4a127f59c8, /* 567: 1.0e+219 >> 664 */
  0xe2a0b5dc971f303a, /* 568: 1.0e+220 >> 667 */
  0x8da471a9de737e24, /* 569: 1.0e+221 >> 671 */
  0xb10d8e1456105dad, /* 570: 1.0e+222 >> 674 */
  0xdd50f1996b947518, /* 571: 1.0e+223 >> 677 */
  0x8a5296ffe33cc92f, /* 572: 1.0e+224 >> 681 */
  0xace73cbfdc0bfb7b, /* 573: 1.0e+225 >> 684 */
  0xd8210befd30efa5a, /* 574: 1.0e+226 >> 687 */
  0x8714a775e3e95c78, /* 575: 1.0e+227 >> 691 */
  0xa8d9d1535ce3b396, /* 576: 1.0e+228 >> 694 */
  0xd31045a8341ca07c, /* 577: 1.0e+229 >> 697 */
  0x83ea2b892091e44d, /* 578: 1.0e+230 >> 701 */
  0xa4e4b66b68b65d60, /* 579: 1.0e+231 >> 704 */
  0xce1de40642e3f4b9, /* 580: 1.0e+232 >> 707 */
  0x80d2ae83e9ce78f3, /* 581: 1.0e+233 >> 711 */
  0xa1075a24e4421730, /* 582: 1.0e+234 >> 714 */
  0xc94930ae1d529cfc, /* 583: 1.0e+235 >> 717 */
  0xfb9b7cd9a4a7443c, /* 584: 1.0e+236 >> 720 */
  0x9d412e0806e88aa5, /* 585: 1.0e+237 >> 724 */
  0xc491798a08a2ad4e, /* 586: 1.0e+238 >> 727 */
  0xf5b5d7ec8acb58a2, /* 587: 1.0e+239 >> 730 */
  0x9991a6f3d6bf1765, /* 588: 1.0e+240 >> 734 */
  0xbff610b0cc6edd3f, /* 589: 1.0e+241 >> 737 */
  0xeff394dcff8a948e, /* 590: 1.0e+242 >> 740 */
  0x95f83d0a1fb69cd9, /* 591: 1.0e+243 >> 744 */
  0xbb764c4ca7a4440f, /* 592: 1.0e+244 >> 747 */
  0xea53df5fd18d5513, /* 593: 1.0e+245 >> 750 */
  0x92746b9be2f8552c, /* 594: 1.0e+246 >> 754 */
  0xb7118682dbb66a77, /* 595: 1.0e+247 >> 757 */
  0xe4d5e82392a40515, /* 596: 1.0e+248 >> 760 */
  0x8f05b1163ba6832d, /* 597: 1.0e+249 >> 764 */
  0xb2c71d5bca9023f8, /* 598: 1.0e+250 >> 767 */
  0xdf78e4b2bd342cf6, /* 599: 1.0e+251 >> 770 */
  0x8bab8eefb6409c1a, /* 600: 1.0e+252 >> 774 */
  0xae9672aba3d0c320, /* 601: 1.0e+253 >> 777 */
  0xda3c0f568cc4f3e8, /* 602: 1.0e+254 >> 780 */
  0x8865899617fb1871, /* 603: 1.0e+255 >> 784 */
  0xaa7eebfb9df9de8d, /* 604: 1.0e+256 >> 787 */
  0xd51ea6fa85785631, /* 605: 1.0e+257 >> 790 */
  0x8533285c936b35de, /* 606: 1.0e+258 >> 794 */
  0xa67ff273b8460356, /* 607: 1.0e+259 >> 797 */
  0xd01fef10a657842c, /* 608: 1.0e+260 >> 800 */
  0x8213f56a67f6b29b, /* 609: 1.0e+261 >> 804 */
  0xa298f2c501f45f42, /* 610: 1.0e+262 >> 807 */
  0xcb3f2f7642717713, /* 611: 1.0e+263 >> 810 */
  0xfe0efb53d30dd4d7, /* 612: 1.0e+264 >> 813 */
  0x9ec95d1463e8a506, /* 613: 1.0e+265 >> 817 */
  0xc67bb4597ce2ce48, /* 614: 1.0e+266 >> 820 */
  0xf81aa16fdc1b81da, /* 615: 1.0e+267 >> 823 */
  0x9b10a4e5e9913128, /* 616: 1.0e+268 >> 827 */
  0xc1d4ce1f63f57d72, /* 617: 1.0e+269 >> 830 */
  0xf24a01a73cf2dccf, /* 618: 1.0e+270 >> 833 */
  0x976e41088617ca01, /* 619: 1.0e+271 >> 837 */
  0xbd49d14aa79dbc82, /* 620: 1.0e+272 >> 840 */
  0xec9c459d51852ba2, /* 621: 1.0e+273 >> 843 */
  0x93e1ab8252f33b45, /* 622: 1.0e+274 >> 847 */
  0xb8da1662e7b00a17, /* 623: 1.0e+275 >> 850 */
  0xe7109bfba19c0c9d, /* 624: 1.0e+276 >> 853 */
  0x906a617d450187e2, /* 625: 1.0e+277 >> 857 */
  0xb484f9dc9641e9da, /* 626: 1.0e+278 >> 860 */
  0xe1a63853bbd26451, /* 627: 1.0e+279 >> 863 */
  0x8d07e33455637eb2, /* 628: 1.0e+280 >> 867 */
  0xb049dc016abc5e5f, /* 629: 1.0e+281 >> 870 */
  0xdc5c5301c56b75f7, /* 630: 1.0e+282 >> 873 */
  0x89b9b3e11b6329ba, /* 631: 1.0e+283 >> 877 */
  0xac2820d9623bf429, /* 632: 1.0e+284 >> 880 */
  0xd732290fbacaf133, /* 633: 1.0e+285 >> 883 */
  0x867f59a9d4bed6c0, /* 634: 1.0e+286 >> 887 */
  0xa81f301449ee8c70, /* 635: 1.0e+287 >> 890 */
  0xd226fc195c6a2f8c, /* 636: 1.0e+288 >> 893 */
  0x83585d8fd9c25db7, /* 637: 1.0e+289 >> 897 */
  0xa42e74f3d032f525, /* 638: 1.0e+290 >> 900 */
  0xcd3a1230c43fb26f, /* 639: 1.0e+291 >> 903 */
  0x80444b5e7aa7cf85, /* 640: 1.0e+292 >> 907 */
  0xa0555e361951c366, /* 641: 1.0e+293 >> 910 */
  0xc86ab5c39fa63440, /* 642: 1.0e+294 >> 913 */
  0xfa856334878fc150, /* 643: 1.0e+295 >> 916 */
  0x9c935e00d4b9d8d2, /* 644: 1.0e+296 >> 920 */
  0xc3b8358109e84f07, /* 645: 1.0e+297 >> 923 */
  0xf4a642e14c6262c8, /* 646: 1.0e+298 >> 926 */
  0x98e7e9cccfbd7dbd, /* 647: 1.0e+299 >> 930 */
  0xbf21e44003acdd2c, /* 648: 1.0e+300 >> 933 */
  0xeeea5d5004981478, /* 649: 1.0e+301 >> 936 */
  0x95527a5202df0ccb, /* 650: 1.0e+302 >> 940 */
  0xbaa718e68396cffd, /* 651: 1.0e+303 >> 943 */
  0xe950df20247c83fd, /* 652: 1.0e+304 >> 946 */
  0x91d28b7416cdd27e, /* 653: 1.0e+305 >> 950 */
  0xb6472e511c81471d, /* 654: 1.0e+306 >> 953 */
  0xe3d8f9e563a198e5, /* 655: 1.0e+307 >> 956 */
  0x8e679c2f5e44ff8f, /* 656: 1.0e+308 >> 960 */
  0xb201833b35d63f73, /* 657: 1.0e+309 >> 963 */
  0xde81e40a034bcf4f, /* 658: 1.0e+310 >> 966 */
  0x8b112e86420f6191, /* 659: 1.0e+311 >> 970 */
  0xadd57a27d29339f6, /* 660: 1.0e+312 >> 973 */
  0xd94ad8b1c7380874, /* 661: 1.0e+313 >> 976 */
  0x87cec76f1c830548, /* 662: 1.0e+314 >> 980 */
  0xa9c2794ae3a3c69a, /* 663: 1.0e+315 >> 983 */
  0xd433179d9c8cb841, /* 664: 1.0e+316 >> 986 */
  0x849feec281d7f328, /* 665: 1.0e+317 >> 990 */
  0xa5c7ea73224deff3, /* 666: 1.0e+318 >> 993 */
  0xcf39e50feae16bef, /* 667: 1.0e+319 >> 996 */
  0x81842f29f2cce375, /* 668: 1.0e+320 >> 1000 */
  0xa1e53af46f801c53, /* 669: 1.0e+321 >> 1003 */
  0xca5e89b18b602368, /* 670: 1.0e+322 >> 1006 */
  0xfcf62c1dee382c42, /* 671: 1.0e+323 >> 1009 */
  0x9e19db92b4e31ba9, /* 672: 1.0e+324 >> 1013 */
  0xc5a05277621be293, /* 673: 1.0e+325 >> 1016 */
  0xf70867153aa2db38, /* 674: 1.0e+326 >> 1019 */
  0x9a65406d44a5c903, /* 675: 1.0e+327 >> 1023 */
  0xc0fe908895cf3b44, /* 676: 1.0e+328 >> 1026 */
  0xf13e34aabb430a15, /* 677: 1.0e+329 >> 1029 */
  0x96c6e0eab509e64d, /* 678: 1.0e+330 >> 1033 */
  0xbc789925624c5fe0, /* 679: 1.0e+331 >> 1036 */
  0xeb96bf6ebadf77d8, /* 680: 1.0e+332 >> 1039 */
  0x933e37a534cbaae7, /* 681: 1.0e+333 >> 1043 */
  0xb80dc58e81fe95a1, /* 682: 1.0e+334 >> 1046 */
  0xe61136f2227e3b09, /* 683: 1.0e+335 >> 1049 */
  0x8fcac257558ee4e6, /* 684: 1.0e+336 >> 1053 */
  0xb3bd72ed2af29e1f, /* 685: 1.0e+337 >> 1056 */
  0xe0accfa875af45a7, /* 686: 1.0e+338 >> 1059 */
  0x8c6c01c9498d8b88, /* 687: 1.0e+339 >> 1063 */
  0xaf87023b9bf0ee6a, /* 688: 1.0e+340 >> 1066 */
  0xdb68c2ca82ed2a05, /* 689: 1.0e+341 >> 1069 */
  0x892179be91d43a43, /* 690: 1.0e+342 >> 1073 */
  0xab69d82e364948d4, /* 691: 1.0e+343 >> 1076 */
  0xd6444e39c3db9b09, /* 692: 1.0e+344 >> 1079 */
  0x85eab0e41a6940e5, /* 693: 1.0e+345 >> 1083 */
  0xa7655d1d2103911f, /* 694: 1.0e+346 >> 1086 */
  0xd13eb46469447567, /* 695: 1.0e+347 >> 1089 */
};
#define POWERSOF10_FIRST (-348)
#define POWERSOF10_LAST  (+347)
#define POWERSOF10_COUNT (696)

/*
** Two inputs are multiplied to get a 128-bit result.  Return
** the high-order 64 bits of that result.
*/
static u64 sqlite3Multiply128(u64 a, u64 b){
#if (defined(__GNUC__) || defined(__clang__)) \
        && (defined(__x86_64__) || defined(__aarch64__) || defined(__riscv))
  return ((__uint128_t)a * b) >> 64;
#elif defined(_MSC_VER) && defined(_M_X64)
  return __umulh(a, b);
#else
  u32 a1 = (u32)a;
  u32 a2 = a >> 32;
  u32 b1 = (u32)b;
  u32 b2 = b >> 32;
  u32 p0 = a1 * b1;
  u32 p1 = a1 * b2;
  u32 p2 = a2 * b1;
  u32 p3 = a2 * b2;
  u32 mid = p1 + (p0 >> 32);
  mid += p2;
  u32 carry = (mid < p2) ? 1 : 0;
  return p3 + (mid >> 32) + carry;
#endif
}

/*
** pow10to2(x) computes floor(log2(pow(10,x))).
** pow2to10(y) computes floor(log10(pow(2,y))).
**
** Conceptually, pow10to2(p) converts a base-10 exponent p into
** a corresponding base-2 exponent, and pow2to10(e) converts a base-2
** exponent into a base-10 exponent.
**
** The conversions are based on the observation that:
**
**     ln(10.0)/ln(2.0) == 108853/32768     (approximately)
**     ln(2.0)/ln(10.0) == 78913/262144     (approximately)
**
** These ratios are approximate, but they are accurate to 5 digits,
** which is close enough for the usage here.  Right-shift is used
** for division so that rounding of negative numbers happens in the
** right direction.
*/
static int pwr10to2(int p){ return (p*108853) >> 15; }
static int pwr2to10(int p){ return (p*78913) >> 18; }

/*
** Return a u64 with the N-th bit set.
*/
#define U64_BIT(N)  (((u64)1)<<(N))

/*
** Count leading zeros for a 64-bit unsigned integer.
*/
static int countLeadingZeros(u64 m){
#if defined(__GNUC__) || defined(__clang__)
  return __builtin_clzll(m);
#else
  int n = 0;
  if( m <= 0x00000000ffffffffULL) { n += 32; m <<= 32; }
  if( m <= 0x0000ffffffffffffULL) { n += 16; m <<= 16; }
  if( m <= 0x00ffffffffffffffULL) { n += 8;  m <<= 8;  }
  if( m <= 0x0fffffffffffffffULL) { n += 4;  m <<= 4;  }
  if( m <= 0x3fffffffffffffffULL) { n += 2;  m <<= 2;  }
  if( m <= 0x7fffffffffffffffULL) { n += 1;            }
  return n;
#endif
}

/*
** Given m and e, which represent a quantity r == m*pow(2,e),
** return values *pD and *pP such that r == (*pD)*pow(10,*pP),
** approximately.  *pD should contain at least n significant digits.
**
** The input m is required to have its highest bit set.  In other words,
** m should be left-shifted, and e decremented, to maximize the value of m.
*/
static void sqlite3Fp2Convert10(u64 m, int e, int n, u64 *pD, int *pP){
  int p, idx;
  u64 h, out;
  p = n - 1 - pwr2to10(e+63);
  idx = p - POWERSOF10_FIRST;
  h = sqlite3Multiply128(m, sqlite3PowerOfTen[idx]);
  assert( -(e + pwr10to2(p) + 3) >=0 );
  assert( -(e + pwr10to2(p) + 3) <64 );
  out = h >> -(e + pwr10to2(p) + 3);
  *pD = (out + 2 + ((out>>2)&1)) >> 2;
  *pP = -p;
}

/*
** Return an IEEE754 floating point value that approximates d*pow(10,p).
*/
static double sqlite3Fp10Convert2(u64 d, int p){
  u64 out;
  int b;
  int e1;
  int e2;
  int lp;
  int idx;
  u64 h;
  double r;
  assert( (d & U64_BIT(63))==0 );
  assert( d!=0 );
  if( p<POWERSOF10_FIRST ){
    return 0.0;
  }
  if( p>POWERSOF10_LAST ){
    return INFINITY;
  }
  b = 64 - countLeadingZeros(d);
  lp = pwr10to2(p);
  e1 = 53 - b - lp;
  if( e1>1074 ){
    if( -(b + lp) >= 1077 ) return 0.0;
    e1 = 1074;
  }
  e2 = e1 - (64-b);
  idx = p - POWERSOF10_FIRST;
  h = sqlite3Multiply128(d<<(64-b), sqlite3PowerOfTen[idx]);
  assert( -(e2 + lp + 3) >=0 );
  assert( -(e2 + lp + 3) <64 );
  out = (h >> -(e2 + lp + 3)) | 1;
  if( out >= U64_BIT(55)-2 ){
    out = (out>>1) | (out&1);
    e1--;
  }
  if( e1<=(-972) ){
    return INFINITY;
  }
  out = (out + 1 + ((out>>2)&1)) >> 2;
  if( (out & U64_BIT(52))!=0 ){
    out = (out & ~U64_BIT(52)) | ((u64)(1075-e1)<<52);
  }
  memcpy(&r, &out, 8);
  return r;
}

/*
** The string z[] is an text representation of a real number.
** Convert this string to a double and write it into *pResult.
**
** The string z[] is length bytes in length (bytes, not characters) and
** uses the encoding enc.  The string is not necessarily zero-terminated.
**
** Return TRUE if the result is a valid real number (or integer) and FALSE
** if the string is empty or contains extraneous text.  More specifically
** return
**      1          =>  The input string is a pure integer
**      2 or more  =>  The input has a decimal point or eNNN clause
**      0 or less  =>  The input string is not a valid number
**     -1          =>  Not a valid number, but has a valid prefix which
**                     includes a decimal point and/or an eNNN clause
**
** Valid numbers are in one of these formats:
**
**    [+-]digits[E[+-]digits]
**    [+-]digits.[digits][E[+-]digits]
**    [+-].digits[E[+-]digits]
**
** Leading and trailing whitespace is ignored for the purpose of determining
** validity.
**
** If some prefix of the input string is a valid number, this routine
** returns FALSE but it still converts the prefix and writes the result
** into *pResult.
*/
#if defined(_MSC_VER)
#pragma warning(disable : 4756)
#endif
int sqlite3AtoF(const char *z, double *pResult, int length, u8 enc){
#ifndef SQLITE_OMIT_FLOATING_POINT
  int incr;
  const char *zEnd;
  /* sign * significand * (10 ^ (esign * exponent)) */
  int sign = 1;    /* sign of significand */
  u64 s = 0;       /* significand */
  int d = 0;       /* adjust exponent for shifting decimal point */
  int esign = 1;   /* sign of exponent */
  int e = 0;       /* exponent */
  int eValid = 1;  /* True exponent is either not used or is well-formed */
  int nDigit = 0;  /* Number of digits processed */
  int eType = 1;   /* 1: pure integer,  2+: fractional  -1 or less: bad UTF16 */

  assert( enc==SQLITE_UTF8 || enc==SQLITE_UTF16LE || enc==SQLITE_UTF16BE );
  *pResult = 0.0;   /* Default return value, in case of an error */
  if( length==0 ) return 0;

  if( enc==SQLITE_UTF8 ){
    incr = 1;
    zEnd = z + length;
  }else{
    int i;
    incr = 2;
    length &= ~1;
    assert( SQLITE_UTF16LE==2 && SQLITE_UTF16BE==3 );
    testcase( enc==SQLITE_UTF16LE );
    testcase( enc==SQLITE_UTF16BE );
    for(i=3-enc; i<length && z[i]==0; i+=2){}
    if( i<length ) eType = -100;
    zEnd = &z[i^1];
    z += (enc&1);
  }

  /* skip leading spaces */
  while( z<zEnd && sqlite3Isspace(*z) ) z+=incr;
  if( z>=zEnd ) return 0;

  /* get sign of significand */
  if( *z=='-' ){
    sign = -1;
    z+=incr;
  }else if( *z=='+' ){
    z+=incr;
  }

  /* copy max significant digits to significand */
  while( z<zEnd && sqlite3Isdigit(*z) ){
    s = s*10 + (*z - '0');
    z+=incr; nDigit++;
    if( s>=((LARGEST_INT64-9)/10) ){
      /* skip non-significant significand digits
      ** (increase exponent by d to shift decimal left) */
      while( z<zEnd && sqlite3Isdigit(*z) ){ z+=incr; d++; }
    }
  }
  if( z>=zEnd ) goto do_atof_calc;

  /* if decimal point is present */
  if( *z=='.' ){
    z+=incr;
    eType++;
    /* copy digits from after decimal to significand
    ** (decrease exponent by d to shift decimal right) */
    while( z<zEnd && sqlite3Isdigit(*z) ){
      if( s<((LARGEST_INT64-9)/10) ){
        s = s*10 + (*z - '0');
        d--;
        nDigit++;
      }
      z+=incr;
    }
  }
  if( z>=zEnd ) goto do_atof_calc;

  /* if exponent is present */
  if( *z=='e' || *z=='E' ){
    z+=incr;
    eValid = 0;
    eType++;

    /* This branch is needed to avoid a (harmless) buffer overread.  The
    ** special comment alerts the mutation tester that the correct answer
    ** is obtained even if the branch is omitted */
    if( z>=zEnd ) goto do_atof_calc;              /*PREVENTS-HARMLESS-OVERREAD*/

    /* get sign of exponent */
    if( *z=='-' ){
      esign = -1;
      z+=incr;
    }else if( *z=='+' ){
      z+=incr;
    }
    /* copy digits to exponent */
    while( z<zEnd && sqlite3Isdigit(*z) ){
      e = e<10000 ? (e*10 + (*z - '0')) : 10000;
      z+=incr;
      eValid = 1;
    }
  }

  /* skip trailing spaces */
  while( z<zEnd && sqlite3Isspace(*z) ) z+=incr;

do_atof_calc:
  /* Zero is a special case */
  if( s==0 ){
    *pResult = sign<0 ? -0.0 : +0.0;
    goto atof_return;
  }

  /* adjust exponent by d, and update sign */
  e = (e*esign) + d;

  *pResult = sqlite3Fp10Convert2(s,e);
  if( sign<0 ) *pResult = -*pResult;
  assert( !sqlite3IsNaN(*pResult) );

atof_return:
  /* return true if number and no extra non-whitespace characters after */
  if( z==zEnd && nDigit>0 && eValid && eType>0 ){
    return eType;
  }else if( eType>=2 && (eType==3 || eValid) && nDigit>0 ){
    return -1;
  }else{
    return 0;
  }
#else
  return !sqlite3Atoi64(z, pResult, length, enc);
#endif /* SQLITE_OMIT_FLOATING_POINT */
}
#if defined(_MSC_VER)
#pragma warning(default : 4756)
#endif

/*
** Render an signed 64-bit integer as text.  Store the result in zOut[] and
** return the length of the string that was stored, in bytes.  The value
** returned does not include the zero terminator at the end of the output
** string.
**
** The caller must ensure that zOut[] is at least 21 bytes in size.
*/
int sqlite3Int64ToText(i64 v, char *zOut){
  int i;
  u64 x;
  char zTemp[22];
  if( v<0 ){
    x = (v==SMALLEST_INT64) ? ((u64)1)<<63 : (u64)-v;
  }else{
    x = v;
  }
  i = sizeof(zTemp)-2;
  zTemp[sizeof(zTemp)-1] = 0;
  while( 1 /*exit-by-break*/ ){
    zTemp[i] = (x%10) + '0';
    x = x/10;
    if( x==0 ) break;
    i--;
  };
  if( v<0 ) zTemp[--i] = '-';
  memcpy(zOut, &zTemp[i], sizeof(zTemp)-i);
  return sizeof(zTemp)-1-i;
}

/*
** Compare the 19-character string zNum against the text representation
** value 2^63:  9223372036854775808.  Return negative, zero, or positive
** if zNum is less than, equal to, or greater than the string.
** Note that zNum must contain exactly 19 characters.
**
** Unlike memcmp() this routine is guaranteed to return the difference
** in the values of the last digit if the only difference is in the
** last digit.  So, for example,
**
**      compare2pow63("9223372036854775800", 1)
**
** will return -8.
*/
static int compare2pow63(const char *zNum, int incr){
  int c = 0;
  int i;
                    /* 012345678901234567 */
  const char *pow63 = "922337203685477580";
  for(i=0; c==0 && i<18; i++){
    c = (zNum[i*incr]-pow63[i])*10;
  }
  if( c==0 ){
    c = zNum[18*incr] - '8';
    testcase( c==(-1) );
    testcase( c==0 );
    testcase( c==(+1) );
  }
  return c;
}

/*
** Convert zNum to a 64-bit signed integer.  zNum must be decimal. This
** routine does *not* accept hexadecimal notation.
**
** Returns:
**
**    -1    Not even a prefix of the input text looks like an integer
**     0    Successful transformation.  Fits in a 64-bit signed integer.
**     1    Excess non-space text after the integer value
**     2    Integer too large for a 64-bit signed integer or is malformed
**     3    Special case of 9223372036854775808
**
** length is the number of bytes in the string (bytes, not characters).
** The string is not necessarily zero-terminated.  The encoding is
** given by enc.
*/
int sqlite3Atoi64(const char *zNum, i64 *pNum, int length, u8 enc){
  int incr;
  u64 u = 0;
  int neg = 0; /* assume positive */
  int i;
  int c = 0;
  int nonNum = 0;  /* True if input contains UTF16 with high byte non-zero */
  int rc;          /* Baseline return code */
  const char *zStart;
  const char *zEnd = zNum + length;
  assert( enc==SQLITE_UTF8 || enc==SQLITE_UTF16LE || enc==SQLITE_UTF16BE );
  if( enc==SQLITE_UTF8 ){
    incr = 1;
  }else{
    incr = 2;
    length &= ~1;
    assert( SQLITE_UTF16LE==2 && SQLITE_UTF16BE==3 );
    for(i=3-enc; i<length && zNum[i]==0; i+=2){}
    nonNum = i<length;
    zEnd = &zNum[i^1];
    zNum += (enc&1);
  }
  while( zNum<zEnd && sqlite3Isspace(*zNum) ) zNum+=incr;
  if( zNum<zEnd ){
    if( *zNum=='-' ){
      neg = 1;
      zNum+=incr;
    }else if( *zNum=='+' ){
      zNum+=incr;
    }
  }
  zStart = zNum;
  while( zNum<zEnd && zNum[0]=='0' ){ zNum+=incr; } /* Skip leading zeros. */
  for(i=0; &zNum[i]<zEnd && (c=zNum[i])>='0' && c<='9'; i+=incr){
    u = u*10 + c - '0';
  }
  testcase( i==18*incr );
  testcase( i==19*incr );
  testcase( i==20*incr );
  if( u>LARGEST_INT64 ){
    /* This test and assignment is needed only to suppress UB warnings
    ** from clang and -fsanitize=undefined.  This test and assignment make
    ** the code a little larger and slower, and no harm comes from omitting
    ** them, but we must appease the undefined-behavior pharisees. */
    *pNum = neg ? SMALLEST_INT64 : LARGEST_INT64;
  }else if( neg ){
    *pNum = -(i64)u;
  }else{
    *pNum = (i64)u;
  }
  rc = 0;
  if( i==0 && zStart==zNum ){    /* No digits */
    rc = -1;
  }else if( nonNum ){            /* UTF16 with high-order bytes non-zero */
    rc = 1;
  }else if( &zNum[i]<zEnd ){     /* Extra bytes at the end */
    int jj = i;
    do{
      if( !sqlite3Isspace(zNum[jj]) ){
        rc = 1;          /* Extra non-space text after the integer */
        break;
      }
      jj += incr;
    }while( &zNum[jj]<zEnd );
  }
  if( i<19*incr ){
    /* Less than 19 digits, so we know that it fits in 64 bits */
    assert( u<=LARGEST_INT64 );
    return rc;
  }else{
    /* zNum is a 19-digit numbers.  Compare it against 9223372036854775808. */
    c = i>19*incr ? 1 : compare2pow63(zNum, incr);
    if( c<0 ){
      /* zNum is less than 9223372036854775808 so it fits */
      assert( u<=LARGEST_INT64 );
      return rc;
    }else{
      *pNum = neg ? SMALLEST_INT64 : LARGEST_INT64;
      if( c>0 ){
        /* zNum is greater than 9223372036854775808 so it overflows */
        return 2;
      }else{
        /* zNum is exactly 9223372036854775808.  Fits if negative.  The
        ** special case 2 overflow if positive */
        assert( u-1==LARGEST_INT64 );
        return neg ? rc : 3;
      }
    }
  }
}

/*
** Transform a UTF-8 integer literal, in either decimal or hexadecimal,
** into a 64-bit signed integer.  This routine accepts hexadecimal literals,
** whereas sqlite3Atoi64() does not.
**
** Returns:
**
**     0    Successful transformation.  Fits in a 64-bit signed integer.
**     1    Excess text after the integer value
**     2    Integer too large for a 64-bit signed integer or is malformed
**     3    Special case of 9223372036854775808
*/
int sqlite3DecOrHexToI64(const char *z, i64 *pOut){
#ifndef SQLITE_OMIT_HEX_INTEGER
  if( z[0]=='0'
   && (z[1]=='x' || z[1]=='X')
  ){
    u64 u = 0;
    int i, k;
    for(i=2; z[i]=='0'; i++){}
    for(k=i; sqlite3Isxdigit(z[k]); k++){
      u = u*16 + sqlite3HexToInt(z[k]);
    }
    memcpy(pOut, &u, 8);
    if( k-i>16 ) return 2;
    if( z[k]!=0 ) return 1;
    return 0;
  }else
#endif /* SQLITE_OMIT_HEX_INTEGER */
  {
    int n = (int)(0x3fffffff&strspn(z,"+- \n\t0123456789"));
    if( z[n] ) n++;
    return sqlite3Atoi64(z, pOut, n, SQLITE_UTF8);
  }
}

/*
** If zNum represents an integer that will fit in 32-bits, then set
** *pValue to that integer and return true.  Otherwise return false.
**
** This routine accepts both decimal and hexadecimal notation for integers.
**
** Any non-numeric characters that following zNum are ignored.
** This is different from sqlite3Atoi64() which requires the
** input number to be zero-terminated.
*/
int sqlite3GetInt32(const char *zNum, int *pValue){
  sqlite_int64 v = 0;
  int i, c;
  int neg = 0;
  if( zNum[0]=='-' ){
    neg = 1;
    zNum++;
  }else if( zNum[0]=='+' ){
    zNum++;
  }
#ifndef SQLITE_OMIT_HEX_INTEGER
  else if( zNum[0]=='0'
        && (zNum[1]=='x' || zNum[1]=='X')
        && sqlite3Isxdigit(zNum[2])
  ){
    u32 u = 0;
    zNum += 2;
    while( zNum[0]=='0' ) zNum++;
    for(i=0; i<8 && sqlite3Isxdigit(zNum[i]); i++){
      u = u*16 + sqlite3HexToInt(zNum[i]);
    }
    if( (u&0x80000000)==0 && sqlite3Isxdigit(zNum[i])==0 ){
      memcpy(pValue, &u, 4);
      return 1;
    }else{
      return 0;
    }
  }
#endif
  if( !sqlite3Isdigit(zNum[0]) ) return 0;
  while( zNum[0]=='0' ) zNum++;
  for(i=0; i<11 && (c = zNum[i] - '0')>=0 && c<=9; i++){
    v = v*10 + c;
  }

  /* The longest decimal representation of a 32 bit integer is 10 digits:
  **
  **             1234567890
  **     2^31 -> 2147483648
  */
  testcase( i==10 );
  if( i>10 ){
    return 0;
  }
  testcase( v-neg==2147483647 );
  if( v-neg>2147483647 ){
    return 0;
  }
  if( neg ){
    v = -v;
  }
  *pValue = (int)v;
  return 1;
}

/*
** Return a 32-bit integer value extracted from a string.  If the
** string is not an integer, just return 0.
*/
int sqlite3Atoi(const char *z){
  int x = 0;
  sqlite3GetInt32(z, &x);
  return x;
}

/*
** Decode a floating-point value into an approximate decimal
** representation.
**
** If iRound<=0 then round to -iRound significant digits to the
** the left of the decimal point, or to a maximum of mxRound total
** significant digits.
**
** If iRound>0 round to min(iRound,mxRound) significant digits total.
**
** mxRound must be positive.
**
** The significant digits of the decimal representation are
** stored in p->z[] which is a often (but not always) a pointer
** into the middle of p->zBuf[].  There are p->n significant digits.
** The p->z[] array is *not* zero-terminated.
*/
void sqlite3FpDecode(FpDecode *p, double r, int iRound, int mxRound){
  int i;
  u64 v;
  int e, exp = 0;

  p->isSpecial = 0;
  p->z = p->zBuf;
  assert( mxRound>0 );

  /* Convert negative numbers to positive.  Deal with Infinity, 0.0, and
  ** NaN. */
  if( r<0.0 ){
    p->sign = '-';
    r = -r;
  }else if( r==0.0 ){
    p->sign = '+';
    p->n = 1;
    p->iDP = 1;
    p->z = "0";
    return;
  }else{
    p->sign = '+';
  }
  memcpy(&v,&r,8);
  e = (v>>52)&0x7ff;
  if( e==0x7ff ){
    p->isSpecial = 1 + (v!=0x7ff0000000000000LL);
    p->n = 0;
    p->iDP = 0;
    return;
  }
  v &= 0x000fffffffffffffULL;
  if( e==0 ){
    int n = countLeadingZeros(v);
    v <<= n;
    e = -1074 - n;
  }else{
    v = (v<<11) | U64_BIT(63);
    e -= 1086;
  }
  sqlite3Fp2Convert10(v, e, 17, &v, &exp);  

  /* Extract significant digits. */
  i = sizeof(p->zBuf)-1;
  assert( v>0 );
  while( v>=10 ){
    static const char dig[] = 
      "00010203040506070809"
      "10111213141516171819"
      "20212223242526272829"
      "30313233343536373839"
      "40414243444546474849"
      "50515253545556575859"
      "60616263646566676869"
      "70717273747576777879"
      "80818283848586878889"
      "90919293949596979899";
    int kk = (v%100)*2;
    p->zBuf[i] = dig[kk+1];
    p->zBuf[i-1] = dig[kk];
    i -= 2;
    v /=100;
  }
  if( v ){  p->zBuf[i--] = (v%10) + '0'; v /= 10; }
  assert( i>=0 && i<sizeof(p->zBuf)-1 );
  p->n = sizeof(p->zBuf) - 1 - i;
  assert( p->n>0 );
  assert( p->n<sizeof(p->zBuf) );
  p->iDP = p->n + exp;
  if( iRound<=0 ){
    iRound = p->iDP - iRound;
    if( iRound==0 && p->zBuf[i+1]>='5' ){
      iRound = 1;
      p->zBuf[i--] = '0';
      p->n++;
      p->iDP++;
    }
  }
  if( iRound>0 && (iRound<p->n || p->n>mxRound) ){
    char *z = &p->zBuf[i+1];
    if( iRound>mxRound ) iRound = mxRound;
    p->n = iRound;
    if( z[iRound]>='5' ){
      int j = iRound-1;
      while( 1 /*exit-by-break*/ ){
        z[j]++;
        if( z[j]<='9' ) break;
        z[j] = '0';
        if( j==0 ){
          p->z[i--] = '1';
          p->n++;
          p->iDP++;
          break;
        }else{
          j--;
        }
      }
    }
  }
  p->z = &p->zBuf[i+1];
  assert( i+p->n < sizeof(p->zBuf) );
  assert( p->n>0 );
  while( p->z[p->n-1]=='0' ){
    p->n--;
    assert( p->n>0 );
  }
}

/*
** Try to convert z into an unsigned 32-bit integer.  Return true on
** success and false if there is an error.
**
** Only decimal notation is accepted.
*/
int sqlite3GetUInt32(const char *z, u32 *pI){
  u64 v = 0;
  int i;
  for(i=0; sqlite3Isdigit(z[i]); i++){
    v = v*10 + z[i] - '0';
    if( v>4294967296LL ){ *pI = 0; return 0; }
  }
  if( i==0 || z[i]!=0 ){ *pI = 0; return 0; }
  *pI = (u32)v;
  return 1;
}

/*
** The variable-length integer encoding is as follows:
**
** KEY:
**         A = 0xxxxxxx    7 bits of data and one flag bit
**         B = 1xxxxxxx    7 bits of data and one flag bit
**         C = xxxxxxxx    8 bits of data
**
**  7 bits - A
** 14 bits - BA
** 21 bits - BBA
** 28 bits - BBBA
** 35 bits - BBBBA
** 42 bits - BBBBBA
** 49 bits - BBBBBBA
** 56 bits - BBBBBBBA
** 64 bits - BBBBBBBBC
*/

/*
** Write a 64-bit variable-length integer to memory starting at p[0].
** The length of data write will be between 1 and 9 bytes.  The number
** of bytes written is returned.
**
** A variable-length integer consists of the lower 7 bits of each byte
** for all bytes that have the 8th bit set and one byte with the 8th
** bit clear.  Except, if we get to the 9th byte, it stores the full
** 8 bits and is the last byte.
*/
static int SQLITE_NOINLINE putVarint64(unsigned char *p, u64 v){
  int i, j, n;
  u8 buf[10];
  if( v & (((u64)0xff000000)<<32) ){
    p[8] = (u8)v;
    v >>= 8;
    for(i=7; i>=0; i--){
      p[i] = (u8)((v & 0x7f) | 0x80);
      v >>= 7;
    }
    return 9;
  }   
  n = 0;
  do{
    buf[n++] = (u8)((v & 0x7f) | 0x80);
    v >>= 7;
  }while( v!=0 );
  buf[0] &= 0x7f;
  assert( n<=9 );
  for(i=0, j=n-1; j>=0; j--, i++){
    p[i] = buf[j];
  }
  return n;
}
int sqlite3PutVarint(unsigned char *p, u64 v){
  if( v<=0x7f ){
    p[0] = v&0x7f;
    return 1;
  }
  if( v<=0x3fff ){
    p[0] = ((v>>7)&0x7f)|0x80;
    p[1] = v&0x7f;
    return 2;
  }
  return putVarint64(p,v);
}

/*
** Bitmasks used by sqlite3GetVarint().  These precomputed constants
** are defined here rather than simply putting the constant expressions
** inline in order to work around bugs in the RVT compiler.
**
** SLOT_2_0     A mask for  (0x7f<<14) | 0x7f
**
** SLOT_4_2_0   A mask for  (0x7f<<28) | SLOT_2_0
*/
#define SLOT_2_0     0x001fc07f
#define SLOT_4_2_0   0xf01fc07f


/*
** Read a 64-bit variable-length integer from memory starting at p[0].
** Return the number of bytes read.  The value is stored in *v.
*/
u8 sqlite3GetVarint(const unsigned char *p, u64 *v){
  u32 a,b,s;

  if( ((signed char*)p)[0]>=0 ){
    *v = *p;
    return 1;
  }
  if( ((signed char*)p)[1]>=0 ){
    *v = ((u32)(p[0]&0x7f)<<7) | p[1];
    return 2;
  }

  /* Verify that constants are precomputed correctly */
  assert( SLOT_2_0 == ((0x7f<<14) | (0x7f)) );
  assert( SLOT_4_2_0 == ((0xfU<<28) | (0x7f<<14) | (0x7f)) );

  a = ((u32)p[0])<<14;
  b = p[1];
  p += 2;
  a |= *p;
  /* a: p0<<14 | p2 (unmasked) */
  if (!(a&0x80))
  {
    a &= SLOT_2_0;
    b &= 0x7f;
    b = b<<7;
    a |= b;
    *v = a;
    return 3;
  }

  /* CSE1 from below */
  a &= SLOT_2_0;
  p++;
  b = b<<14;
  b |= *p;
  /* b: p1<<14 | p3 (unmasked) */
  if (!(b&0x80))
  {
    b &= SLOT_2_0;
    /* moved CSE1 up */
    /* a &= (0x7f<<14)|(0x7f); */
    a = a<<7;
    a |= b;
    *v = a;
    return 4;
  }

  /* a: p0<<14 | p2 (masked) */
  /* b: p1<<14 | p3 (unmasked) */
  /* 1:save off p0<<21 | p1<<14 | p2<<7 | p3 (masked) */
  /* moved CSE1 up */
  /* a &= (0x7f<<14)|(0x7f); */
  b &= SLOT_2_0;
  s = a;
  /* s: p0<<14 | p2 (masked) */

  p++;
  a = a<<14;
  a |= *p;
  /* a: p0<<28 | p2<<14 | p4 (unmasked) */
  if (!(a&0x80))
  {
    /* we can skip these cause they were (effectively) done above
    ** while calculating s */
    /* a &= (0x7f<<28)|(0x7f<<14)|(0x7f); */
    /* b &= (0x7f<<14)|(0x7f); */
    b = b<<7;
    a |= b;
    s = s>>18;
    *v = ((u64)s)<<32 | a;
    return 5;
  }

  /* 2:save off p0<<21 | p1<<14 | p2<<7 | p3 (masked) */
  s = s<<7;
  s |= b;
  /* s: p0<<21 | p1<<14 | p2<<7 | p3 (masked) */

  p++;
  b = b<<14;
  b |= *p;
  /* b: p1<<28 | p3<<14 | p5 (unmasked) */
  if (!(b&0x80))
  {
    /* we can skip this cause it was (effectively) done above in calc'ing s */
    /* b &= (0x7f<<28)|(0x7f<<14)|(0x7f); */
    a &= SLOT_2_0;
    a = a<<7;
    a |= b;
    s = s>>18;
    *v = ((u64)s)<<32 | a;
    return 6;
  }

  p++;
  a = a<<14;
  a |= *p;
  /* a: p2<<28 | p4<<14 | p6 (unmasked) */
  if (!(a&0x80))
  {
    a &= SLOT_4_2_0;
    b &= SLOT_2_0;
    b = b<<7;
    a |= b;
    s = s>>11;
    *v = ((u64)s)<<32 | a;
    return 7;
  }

  /* CSE2 from below */
  a &= SLOT_2_0;
  p++;
  b = b<<14;
  b |= *p;
  /* b: p3<<28 | p5<<14 | p7 (unmasked) */
  if (!(b&0x80))
  {
    b &= SLOT_4_2_0;
    /* moved CSE2 up */
    /* a &= (0x7f<<14)|(0x7f); */
    a = a<<7;
    a |= b;
    s = s>>4;
    *v = ((u64)s)<<32 | a;
    return 8;
  }

  p++;
  a = a<<15;
  a |= *p;
  /* a: p4<<29 | p6<<15 | p8 (unmasked) */

  /* moved CSE2 up */
  /* a &= (0x7f<<29)|(0x7f<<15)|(0xff); */
  b &= SLOT_2_0;
  b = b<<8;
  a |= b;

  s = s<<4;
  b = p[-4];
  b &= 0x7f;
  b = b>>3;
  s |= b;

  *v = ((u64)s)<<32 | a;

  return 9;
}

/*
** Read a 32-bit variable-length integer from memory starting at p[0].
** Return the number of bytes read.  The value is stored in *v.
**
** If the varint stored in p[0] is larger than can fit in a 32-bit unsigned
** integer, then set *v to 0xffffffff.
**
** A MACRO version, getVarint32, is provided which inlines the
** single-byte case.  All code should use the MACRO version as
** this function assumes the single-byte case has already been handled.
*/
u8 sqlite3GetVarint32(const unsigned char *p, u32 *v){
  u64 v64;
  u8 n;

  /* Assume that the single-byte case has already been handled by
  ** the getVarint32() macro */
  assert( (p[0] & 0x80)!=0 );

  if( (p[1] & 0x80)==0 ){
    /* This is the two-byte case */
    *v = ((p[0]&0x7f)<<7) | p[1];
    return 2;
  }
  if( (p[2] & 0x80)==0 ){
    /* This is the three-byte case */
    *v = ((p[0]&0x7f)<<14) | ((p[1]&0x7f)<<7) | p[2];
    return 3;
  }
  /* four or more bytes */
  n = sqlite3GetVarint(p, &v64);
  assert( n>3 && n<=9 );
  if( (v64 & SQLITE_MAX_U32)!=v64 ){
    *v = 0xffffffff;
  }else{
    *v = (u32)v64;
  }
  return n;
}

/*
** Return the number of bytes that will be needed to store the given
** 64-bit integer.
*/
int sqlite3VarintLen(u64 v){
  int i;
  for(i=1; (v >>= 7)!=0; i++){ assert( i<10 ); }
  return i;
}


/*
** Read or write a four-byte big-endian integer value.
*/
u32 sqlite3Get4byte(const u8 *p){
#if SQLITE_BYTEORDER==4321
  u32 x;
  memcpy(&x,p,4);
  return x;
#elif SQLITE_BYTEORDER==1234 && GCC_VERSION>=4003000
  u32 x;
  memcpy(&x,p,4);
  return __builtin_bswap32(x);
#elif SQLITE_BYTEORDER==1234 && MSVC_VERSION>=1300
  u32 x;
  memcpy(&x,p,4);
  return _byteswap_ulong(x);
#else
  testcase( p[0]&0x80 );
  return ((unsigned)p[0]<<24) | (p[1]<<16) | (p[2]<<8) | p[3];
#endif
}
void sqlite3Put4byte(unsigned char *p, u32 v){
#if SQLITE_BYTEORDER==4321
  memcpy(p,&v,4);
#elif SQLITE_BYTEORDER==1234 && GCC_VERSION>=4003000
  u32 x = __builtin_bswap32(v);
  memcpy(p,&x,4);
#elif SQLITE_BYTEORDER==1234 && MSVC_VERSION>=1300
  u32 x = _byteswap_ulong(v);
  memcpy(p,&x,4);
#else
  p[0] = (u8)(v>>24);
  p[1] = (u8)(v>>16);
  p[2] = (u8)(v>>8);
  p[3] = (u8)v;
#endif
}



/*
** Translate a single byte of Hex into an integer.
** This routine only works if h really is a valid hexadecimal
** character:  0..9a..fA..F
*/
u8 sqlite3HexToInt(int h){
  assert( (h>='0' && h<='9') ||  (h>='a' && h<='f') ||  (h>='A' && h<='F') );
#ifdef SQLITE_ASCII
  h += 9*(1&(h>>6));
#endif
#ifdef SQLITE_EBCDIC
  h += 9*(1&~(h>>4));
#endif
  return (u8)(h & 0xf);
}

#if !defined(SQLITE_OMIT_BLOB_LITERAL)
/*
** Convert a BLOB literal of the form "x'hhhhhh'" into its binary
** value.  Return a pointer to its binary value.  Space to hold the
** binary value has been obtained from malloc and must be freed by
** the calling routine.
*/
void *sqlite3HexToBlob(sqlite3 *db, const char *z, int n){
  char *zBlob;
  int i;

  zBlob = (char *)sqlite3DbMallocRawNN(db, n/2 + 1);
  n--;
  if( zBlob ){
    for(i=0; i<n; i+=2){
      zBlob[i/2] = (sqlite3HexToInt(z[i])<<4) | sqlite3HexToInt(z[i+1]);
    }
    zBlob[i/2] = 0;
  }
  return zBlob;
}
#endif /* !SQLITE_OMIT_BLOB_LITERAL */

/*
** Log an error that is an API call on a connection pointer that should
** not have been used.  The "type" of connection pointer is given as the
** argument.  The zType is a word like "NULL" or "closed" or "invalid".
*/
static void logBadConnection(const char *zType){
  sqlite3_log(SQLITE_MISUSE,
     "API call with %s database connection pointer",
     zType
  );
}

/*
** Check to make sure we have a valid db pointer.  This test is not
** foolproof but it does provide some measure of protection against
** misuse of the interface such as passing in db pointers that are
** NULL or which have been previously closed.  If this routine returns
** 1 it means that the db pointer is valid and 0 if it should not be
** dereferenced for any reason.  The calling function should invoke
** SQLITE_MISUSE immediately.
**
** sqlite3SafetyCheckOk() requires that the db pointer be valid for
** use.  sqlite3SafetyCheckSickOrOk() allows a db pointer that failed to
** open properly and is not fit for general use but which can be
** used as an argument to sqlite3_errmsg() or sqlite3_close().
*/
int sqlite3SafetyCheckOk(sqlite3 *db){
  u8 eOpenState;
  if( db==0 ){
    logBadConnection("NULL");
    return 0;
  }
  eOpenState = db->eOpenState;
  if( eOpenState!=SQLITE_STATE_OPEN ){
    if( sqlite3SafetyCheckSickOrOk(db) ){
      testcase( sqlite3GlobalConfig.xLog!=0 );
      logBadConnection("unopened");
    }
    return 0;
  }else{
    return 1;
  }
}
int sqlite3SafetyCheckSickOrOk(sqlite3 *db){
  u8 eOpenState;
  eOpenState = db->eOpenState;
  if( eOpenState!=SQLITE_STATE_SICK &&
      eOpenState!=SQLITE_STATE_OPEN &&
      eOpenState!=SQLITE_STATE_BUSY ){
    testcase( sqlite3GlobalConfig.xLog!=0 );
    logBadConnection("invalid");
    return 0;
  }else{
    return 1;
  }
}

/*
** Attempt to add, subtract, or multiply the 64-bit signed value iB against
** the other 64-bit signed integer at *pA and store the result in *pA.
** Return 0 on success.  Or if the operation would have resulted in an
** overflow, leave *pA unchanged and return 1.
*/
int sqlite3AddInt64(i64 *pA, i64 iB){
#if GCC_VERSION>=5004000 && !defined(__INTEL_COMPILER)
  return __builtin_add_overflow(*pA, iB, pA);
#else
  i64 iA = *pA;
  testcase( iA==0 ); testcase( iA==1 );
  testcase( iB==-1 ); testcase( iB==0 );
  if( iB>=0 ){
    testcase( iA>0 && LARGEST_INT64 - iA == iB );
    testcase( iA>0 && LARGEST_INT64 - iA == iB - 1 );
    if( iA>0 && LARGEST_INT64 - iA < iB ) return 1;
  }else{
    testcase( iA<0 && -(iA + LARGEST_INT64) == iB + 1 );
    testcase( iA<0 && -(iA + LARGEST_INT64) == iB + 2 );
    if( iA<0 && -(iA + LARGEST_INT64) > iB + 1 ) return 1;
  }
  *pA += iB;
  return 0;
#endif
}
int sqlite3SubInt64(i64 *pA, i64 iB){
#if GCC_VERSION>=5004000 && !defined(__INTEL_COMPILER)
  return __builtin_sub_overflow(*pA, iB, pA);
#else
  testcase( iB==SMALLEST_INT64+1 );
  if( iB==SMALLEST_INT64 ){
    testcase( (*pA)==(-1) ); testcase( (*pA)==0 );
    if( (*pA)>=0 ) return 1;
    *pA -= iB;
    return 0;
  }else{
    return sqlite3AddInt64(pA, -iB);
  }
#endif
}
int sqlite3MulInt64(i64 *pA, i64 iB){
#if GCC_VERSION>=5004000 && !defined(__INTEL_COMPILER)
  return __builtin_mul_overflow(*pA, iB, pA);
#else
  i64 iA = *pA;
  if( iB>0 ){
    if( iA>LARGEST_INT64/iB ) return 1;
    if( iA<SMALLEST_INT64/iB ) return 1;
  }else if( iB<0 ){
    if( iA>0 ){
      if( iB<SMALLEST_INT64/iA ) return 1;
    }else if( iA<0 ){
      if( iB==SMALLEST_INT64 ) return 1;
      if( iA==SMALLEST_INT64 ) return 1;
      if( -iA>LARGEST_INT64/-iB ) return 1;
    }
  }
  *pA = iA*iB;
  return 0;
#endif
}

/*
** Compute the absolute value of a 32-bit signed integer, if possible.  Or
** if the integer has a value of -2147483648, return +2147483647
*/
int sqlite3AbsInt32(int x){
  if( x>=0 ) return x;
  if( x==(int)0x80000000 ) return 0x7fffffff;
  return -x;
}

#ifdef SQLITE_ENABLE_8_3_NAMES
/*
** If SQLITE_ENABLE_8_3_NAMES is set at compile-time and if the database
** filename in zBaseFilename is a URI with the "8_3_names=1" parameter and
** if filename in z[] has a suffix (a.k.a. "extension") that is longer than
** three characters, then shorten the suffix on z[] to be the last three
** characters of the original suffix.
**
** If SQLITE_ENABLE_8_3_NAMES is set to 2 at compile-time, then always
** do the suffix shortening regardless of URI parameter.
**
** Examples:
**
**     test.db-journal    =>   test.nal
**     test.db-wal        =>   test.wal
**     test.db-shm        =>   test.shm
**     test.db-mj7f3319fa =>   test.9fa
*/
void sqlite3FileSuffix3(const char *zBaseFilename, char *z){
#if SQLITE_ENABLE_8_3_NAMES<2
  if( sqlite3_uri_boolean(zBaseFilename, "8_3_names", 0) )
#endif
  {
    int i, sz;
    sz = sqlite3Strlen30(z);
    for(i=sz-1; i>0 && z[i]!='/' && z[i]!='.'; i--){}
    if( z[i]=='.' && ALWAYS(sz>i+4) ) memmove(&z[i+1], &z[sz-3], 4);
  }
}
#endif

/*
** Find (an approximate) sum of two LogEst values.  This computation is
** not a simple "+" operator because LogEst is stored as a logarithmic
** value.
**
*/
LogEst sqlite3LogEstAdd(LogEst a, LogEst b){
  static const unsigned char x[] = {
     10, 10,                         /* 0,1 */
      9, 9,                          /* 2,3 */
      8, 8,                          /* 4,5 */
      7, 7, 7,                       /* 6,7,8 */
      6, 6, 6,                       /* 9,10,11 */
      5, 5, 5,                       /* 12-14 */
      4, 4, 4, 4,                    /* 15-18 */
      3, 3, 3, 3, 3, 3,              /* 19-24 */
      2, 2, 2, 2, 2, 2, 2,           /* 25-31 */
  };
  if( a>=b ){
    if( a>b+49 ) return a;
    if( a>b+31 ) return a+1;
    return a+x[a-b];
  }else{
    if( b>a+49 ) return b;
    if( b>a+31 ) return b+1;
    return b+x[b-a];
  }
}

/*
** Convert an integer into a LogEst.  In other words, compute an
** approximation for 10*log2(x).
*/
LogEst sqlite3LogEst(u64 x){
  static LogEst a[] = { 0, 2, 3, 5, 6, 7, 8, 9 };
  LogEst y = 40;
  if( x<8 ){
    if( x<2 ) return 0;
    while( x<8 ){  y -= 10; x <<= 1; }
  }else{
#if GCC_VERSION>=5004000
    int i = 60 - __builtin_clzll(x);
    y += i*10;
    x >>= i;
#else
    while( x>255 ){ y += 40; x >>= 4; }  /*OPTIMIZATION-IF-TRUE*/
    while( x>15 ){  y += 10; x >>= 1; }
#endif
  }
  return a[x&7] + y - 10;
}

/*
** Convert a double into a LogEst
** In other words, compute an approximation for 10*log2(x).
*/
LogEst sqlite3LogEstFromDouble(double x){
  u64 a;
  LogEst e;
  assert( sizeof(x)==8 && sizeof(a)==8 );
  if( x<=1 ) return 0;
  if( x<=2000000000 ) return sqlite3LogEst((u64)x);
  memcpy(&a, &x, 8);
  e = (a>>52) - 1022;
  return e*10;
}

/*
** Convert a LogEst into an integer.
*/
u64 sqlite3LogEstToInt(LogEst x){
  u64 n;
  n = x%10;
  x /= 10;
  if( n>=5 ) n -= 2;
  else if( n>=1 ) n -= 1;
  if( x>60 ) return (u64)LARGEST_INT64;
  return x>=3 ? (n+8)<<(x-3) : (n+8)>>(3-x);
}

/*
** Add a new name/number pair to a VList.  This might require that the
** VList object be reallocated, so return the new VList.  If an OOM
** error occurs, the original VList returned and the
** db->mallocFailed flag is set.
**
** A VList is really just an array of integers.  To destroy a VList,
** simply pass it to sqlite3DbFree().
**
** The first integer is the number of integers allocated for the whole
** VList.  The second integer is the number of integers actually used.
** Each name/number pair is encoded by subsequent groups of 3 or more
** integers.
**
** Each name/number pair starts with two integers which are the numeric
** value for the pair and the size of the name/number pair, respectively.
** The text name overlays one or more following integers.  The text name
** is always zero-terminated.
**
** Conceptually:
**
**    struct VList {
**      int nAlloc;   // Number of allocated slots
**      int nUsed;    // Number of used slots
**      struct VListEntry {
**        int iValue;    // Value for this entry
**        int nSlot;     // Slots used by this entry
**        // ... variable name goes here
**      } a[0];
**    }
**
** During code generation, pointers to the variable names within the
** VList are taken.  When that happens, nAlloc is set to zero as an
** indication that the VList may never again be enlarged, since the
** accompanying realloc() would invalidate the pointers.
*/
VList *sqlite3VListAdd(
  sqlite3 *db,           /* The database connection used for malloc() */
  VList *pIn,            /* The input VList.  Might be NULL */
  const char *zName,     /* Name of symbol to add */
  int nName,             /* Bytes of text in zName */
  int iVal               /* Value to associate with zName */
){
  int nInt;              /* number of sizeof(int) objects needed for zName */
  char *z;               /* Pointer to where zName will be stored */
  int i;                 /* Index in pIn[] where zName is stored */

  nInt = nName/4 + 3;
  assert( pIn==0 || pIn[0]>=3 );  /* Verify ok to add new elements */
  if( pIn==0 || pIn[1]+nInt > pIn[0] ){
    /* Enlarge the allocation */
    sqlite3_int64 nAlloc = (pIn ? 2*(sqlite3_int64)pIn[0] : 10) + nInt;
    VList *pOut = sqlite3DbRealloc(db, pIn, nAlloc*sizeof(int));
    if( pOut==0 ) return pIn;
    if( pIn==0 ) pOut[1] = 2;
    pIn = pOut;
    pIn[0] = nAlloc;
  }
  i = pIn[1];
  pIn[i] = iVal;
  pIn[i+1] = nInt;
  z = (char*)&pIn[i+2];
  pIn[1] = i+nInt;
  assert( pIn[1]<=pIn[0] );
  memcpy(z, zName, nName);
  z[nName] = 0;
  return pIn;
}

/*
** Return a pointer to the name of a variable in the given VList that
** has the value iVal.  Or return a NULL if there is no such variable in
** the list
*/
const char *sqlite3VListNumToName(VList *pIn, int iVal){
  int i, mx;
  if( pIn==0 ) return 0;
  mx = pIn[1];
  i = 2;
  do{
    if( pIn[i]==iVal ) return (char*)&pIn[i+2];
    i += pIn[i+1];
  }while( i<mx );
  return 0;
}

/*
** Return the number of the variable named zName, if it is in VList.
** or return 0 if there is no such variable.
*/
int sqlite3VListNameToNum(VList *pIn, const char *zName, int nName){
  int i, mx;
  if( pIn==0 ) return 0;
  mx = pIn[1];
  i = 2;
  do{
    const char *z = (const char*)&pIn[i+2];
    if( strncmp(z,zName,nName)==0 && z[nName]==0 ) return pIn[i];
    i += pIn[i+1];
  }while( i<mx );
  return 0;
}
