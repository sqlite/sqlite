/*
** 2026-10-10
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
** The sqlite3_test_control() interface with SQLITE_TESTCTRL_OPTIMIZATIONS
** and SQLITE_TESTCTRL_GETOPT are used to enable and/or disable various
** optimizations within SQLite.  Optimizations are identified by a bitmask.
** This file contains external code that will map symbolic names into
** bitmask values, and bitmask values into symbolic names.
**
** This code is not part of the SQLite core because we don't want to use
** code space to store all the symbolic names.
*/
#include "sqlite3.h"

/*
** Convert an optimization number (as recorded in sqliteInt.h) into
** a mask for that optimization.
*/
#define OPT_MASK(N)   ((1ull)<<((N)-1))


/*
** This is the mapping between bits and names.
**
** Some bit values have multiple names.  The preferred name always
** comes first.
*/
static const struct OptName {
  sqlite3_uint64 uMask;
  const char *zName;
} aOpt[] = {
    { OPT_MASK(1),           "QueryFlattener" },
    { OPT_MASK(1),           "Flatten" },         /* Alias for prior */
    { OPT_MASK(2),           "WindowFunc" },
    { OPT_MASK(3),           "GroupByOrder" },
    { OPT_MASK(4),           "JoinMiss" },
    { OPT_MASK(5),           "DistinctOpt" },
    { OPT_MASK(6),           "CoverIdxScan" },
    { OPT_MASK(7),           "OrderByIdxJoin" },
    { OPT_MASK(8),           "Transitive" },
    { OPT_MASK(9),           "OmitNoopJoin" },
    { OPT_MASK(10),          "CountOfView" },
    { OPT_MASK(11),          "CursorHints" },
    { OPT_MASK(12),          "Stat4" },
    { OPT_MASK(13),          "PushDown" },
    { OPT_MASK(14),          "SimplifyJoin" },
    { OPT_MASK(15),          "SkipScan" },
    { OPT_MASK(16),          "PropagateConst" },
    { OPT_MASK(17),          "MinMaxOpt" },
    { OPT_MASK(18),          "SeekScan" },
    { OPT_MASK(19),          "OmitOrderBy" },
    { OPT_MASK(20),          "BloomFilter" },
    /* 21 currently unused */
    { OPT_MASK(22),          "BalancedMerge" },
    { OPT_MASK(23),          "ReleaseReg" },
    { OPT_MASK(24),          "FlttnUnionAll" },
    { OPT_MASK(25),          "IndexedExpr" },
    { OPT_MASK(26),          "Coroutines" },
    { OPT_MASK(27),          "NullUnusedCols" },
    { OPT_MASK(28),          "OnePass" },
    { OPT_MASK(29),          "OrderBySubq" },
    { OPT_MASK(29),          "OrderBySubquery" },  /* Alias for prior */
    { OPT_MASK(30),          "StarQuery" },
    { OPT_MASK(31),          "ExistsToJoin" },
    { OPT_MASK(32),          "UnionLimit" },
    /* Special names */
    { 0xffffffffffffffffULL, "All" },
    { 0x0000000000000000ULL, "None" },
};

/*
** Given a symbolic name, try to compute the corresponding mask.
**
** The return value is:
**
**     SQLITE_OK           Successful match
**     SQLITE_NOTFOUND     No match
**     SQLITE_MISUSE       Bad arguments
*/
int sqlite3_optimization_mask(const char *zName, sqlite3_uint64 *pMask){
  int i;
  if( zName==0 || pMask==0 ){ return SQLITE_MISUSE; }
  for(i=0; i<sizeof(aOpt)/sizeof(aOpt[0]); i++){
    if( sqlite3_stricmp(zName, aOpt[i].zName)==0 ){
      *pMask = aOpt[i].uMask;
      return SQLITE_OK;
    }
  }
  *pMask = 0;
  return SQLITE_NOTFOUND;
}

/*
** Given a bitmap, return the canonical symbolic name.  Return 0 for
** a bitmap input of 0 or a bitmap value that matches nothing.
*/
const char *sqlite3_optimization_name(sqlite3_int64 mask){
  int i;
  for(i=0; i<sizeof(aOpt)/sizeof(aOpt[0]); i++){
    if( aOpt[i].uMask==mask ){ return aOpt[i].zName; }
  }
  return 0;
}
