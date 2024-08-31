/*
** 2013-05-28
**
** The author disclaims copyright to this source code.  In place of
** a legal notice, here is a blessing:
**
**    May you do good and not evil.
**    May you find forgiveness for yourself and forgive others.
**    May you share freely, never taking more than you give.
**
******************************************************************************
**
** This file contains code to implement the percentile(Y,P) SQL function
** as described below:
**
**   (1)  The percentile(Y,P) function is an aggregate function taking
**        exactly two arguments.
**
**   (2)  If the P argument to percentile(Y,P) is not the same for every
**        row in the aggregate then an error is thrown.  The word "same"
**        in the previous sentence means that the value differ by less
**        than 0.001.
**
**   (3)  If the P argument to percentile(Y,P) evaluates to anything other
**        than a number in the range of 0.0 to 100.0 inclusive then an
**        error is thrown.
**
**   (4)  If any Y argument to percentile(Y,P) evaluates to a value that
**        is not NULL and is not numeric then an error is thrown.
**
**   (5)  If any Y argument to percentile(Y,P) evaluates to plus or minus
**        infinity then an error is thrown.  (SQLite always interprets NaN
**        values as NULL.)
**
**   (6)  Both Y and P in percentile(Y,P) can be arbitrary expressions,
**        including CASE WHEN expressions.
**
**   (7)  The percentile(Y,P) aggregate is able to handle inputs of at least
**        one million (1,000,000) rows.
**
**   (8)  If there are no non-NULL values for Y, then percentile(Y,P)
**        returns NULL.
**
**   (9)  If there is exactly one non-NULL value for Y, the percentile(Y,P)
**        returns the one Y value.
**
**  (10)  If there N non-NULL values of Y where N is two or more and
**        the Y values are ordered from least to greatest and a graph is
**        drawn from 0 to N-1 such that the height of the graph at J is
**        the J-th Y value and such that straight lines are drawn between
**        adjacent Y values, then the percentile(Y,P) function returns
**        the height of the graph at P*(N-1)/100.
**
**  (11)  The percentile(Y,P) function always returns either a floating
**        point number or NULL.
**
**  (12)  The percentile(Y,P) is implemented as a single C99 source-code
**        file that compiles into a shared-library or DLL that can be loaded
**        into SQLite using the sqlite3_load_extension() interface.
**
**  (13)  A separate median(Y) function is the equivalent percentile(Y,50).
**
**  (14)  Both median() and percentile(Y,P) can be used as window functions.
**
** Differences from standard SQL:
**
**  *  The percentile(X,P) function is equivalent to the following in
**     standard SQL:
**
**         (percentile(P/100.0) WITHIN GROUP (ORDER BY X))
**
**     The SQLite syntax is much more compact.  Note also that the
**     range of the P argument is 0..100 in SQLite, but 0..1 in the
**     standard.
**
**  *  No merge(X) function exists in the standard.  Application developers
**     are expected to write "percentile_cont(0.5)WITHIN GROUP(ORDER BY X)".
**
** Implementation notes as of 2024-08-31:
**
**  *  The regular aggregate-function versions of the merge() and percentile(),
**     routines work by accumulating all values in an array of doubles, then
**     sorting that array using a quicksort before computing the answer. Thus
**     the runtime is O(NlogN) where N is the number of rows of input.
**
**  *  For the window-function versions of these routines, the array of
**     inputs is sorted as soon as the first value is computed.  Thereafter,
**     the array is kept in sorted order using an insert-sort.  This
**     results in O(N*K) performance where K is the size of the window.
**     One can imagine alternative implementations that give O(N*logN*logK)
**     performance, but they require more complex logic and data structures.
**     The developers have elected to keep the asymptotically slower
**     algorithm for now, for simplicity, under the theory that window
**     functions are seldom used and when they are, the window size K is
**     often small.  The developers might revisit that decision later,
**     should the need arise.
*/
#include "sqlite3ext.h"
SQLITE_EXTENSION_INIT1
#include <assert.h>
#include <string.h>
#include <stdlib.h>

/* The following object is the session context for a single percentile()
** function.  We have to remember all input Y values until the very end.
** Those values are accumulated in the Percentile.a[] array.
*/
typedef struct Percentile Percentile;
struct Percentile {
  unsigned nAlloc;     /* Number of slots allocated for a[] */
  unsigned nUsed;      /* Number of slots actually used in a[] */
  char bSorted;        /* True if a[] is already in sorted order */
  char bKeepSorted;    /* True if advantageous to keep a[] sorted */
  double rPct;         /* 1.0 more than the value for P */
  double *a;           /* Array of Y values */
};

/*
** Return TRUE if the input floating-point number is an infinity.
*/
static int percentIsInfinity(double r){
  sqlite3_uint64 u;
  assert( sizeof(u)==sizeof(r) );
  memcpy(&u, &r, sizeof(u));
  return ((u>>52)&0x7ff)==0x7ff;
}

/*
** Return TRUE if two doubles differ by 0.001 or less.
*/
static int percentSameValue(double a, double b){
  a -= b;
  return a>=-0.001 && a<=0.001;
}

#if 0
/* Verify that the elements of the Percentile p are in fact sorted.
** Used for testing and debugging only.
*/
static void percentAssertSorted(Percentile *p){
  int i;
  for(i=p->nUsed-2; i>=0 && p->a[i]<=p->a[i+1]; i--){}
  assert( i<0 );
}
#else
# define percentAssertSorted(X)
#endif

/*
** Search p (which must have p->bSorted) looking for an entry with
** value y.  Return the index of that entry.
**
** If bExact is true, return -1 if the entry is not found.
**
** If bExact is false, return the index at which a new entry with
** value y should be insert in order to keep the values in sorted
** order.  The smallest return value in this case will be 0, and
** the largest return value will be p->nUsed.
*/
static int percentBinarySearch(Percentile *p, double y, int bExact){
  int iFirst = 0;              /* First element of search range */
  int iLast = p->nUsed - 1;    /* Last element of search range */
  while( iLast>=iFirst ){
    int iMid = (iFirst+iLast)/2;
    double x = p->a[iMid];
    if( x<y ){
      iFirst = iMid + 1;
    }else if( x>y ){
      iLast = iMid - 1;
    }else{
      return iMid;
    }
  }
  if( bExact ) return -1;
  return iFirst;
}

/*
** The "step" function for percentile(Y,P) is called once for each
** input row.
*/
static void percentStep(sqlite3_context *pCtx, int argc, sqlite3_value **argv){
  Percentile *p;
  double rPct;
  int eType;
  double y;
  assert( argc==2 || argc==1 );

  if( argc==1 ){
    /* Requirement 13:  median(Y) is the same as percentile(Y,50). */
    rPct = 50.0;
  }else{
    /* Requirement 3:  P must be a number between 0 and 100 */
    eType = sqlite3_value_numeric_type(argv[1]);
    rPct = sqlite3_value_double(argv[1]);
    if( (eType!=SQLITE_INTEGER && eType!=SQLITE_FLOAT)
     || rPct<0.0 || rPct>100.0 ){
       sqlite3_result_error(pCtx, "2nd argument to percentile() is not "
                           "a number between 0.0 and 100.0", -1);
      return;
    }
  }

  /* Allocate the session context. */
  p = (Percentile*)sqlite3_aggregate_context(pCtx, sizeof(*p));
  if( p==0 ) return;

  /* Remember the P value.  Throw an error if the P value is different
  ** from any prior row, per Requirement (2). */
  if( p->rPct==0.0 ){
    p->rPct = rPct+1.0;
  }else if( !percentSameValue(p->rPct,rPct+1.0) ){
    sqlite3_result_error(pCtx, "2nd argument to percentile() is not the "
                               "same for all input rows", -1);
    return;
  }

  /* Ignore rows for which Y is NULL */
  eType = sqlite3_value_type(argv[0]);
  if( eType==SQLITE_NULL ) return;

  /* If not NULL, then Y must be numeric.  Otherwise throw an error.
  ** Requirement 4 */
  if( eType!=SQLITE_INTEGER && eType!=SQLITE_FLOAT ){
    sqlite3_result_error(pCtx, "1st argument to percentile() is not "
                               "numeric", -1);
    return;
  }

  /* Throw an error if the Y value is infinity or NaN */
  y = sqlite3_value_double(argv[0]);
  if( percentIsInfinity(y) ){
    sqlite3_result_error(pCtx, "Inf input to percentile()", -1);
    return;
  }

  /* Allocate and store the Y */
  if( p->nUsed>=p->nAlloc ){
    unsigned n = p->nAlloc*2 + 250;
    double *a = sqlite3_realloc64(p->a, sizeof(double)*n);
    if( a==0 ){
      sqlite3_free(p->a);
      memset(p, 0, sizeof(*p));
      sqlite3_result_error_nomem(pCtx);
      return;
    }
    p->nAlloc = n;
    p->a = a;
  }
  if( p->nUsed==0 ){
    p->a[p->nUsed++] = y;
    p->bSorted = 1;
  }else if( !p->bSorted || y>=p->a[p->nUsed-1] ){
    p->a[p->nUsed++] = y;
  }else if( p->bKeepSorted ){
    int i;
    percentAssertSorted(p);
    i = percentBinarySearch(p, y, 0);
    if( i<p->nUsed ){
      memmove(&p->a[i+1], &p->a[i], (p->nUsed-i)*sizeof(p->a[0]));
    }
    p->a[i] = y;
    p->nUsed++;
  }else{
    p->a[p->nUsed++] = y;
    p->bSorted = 0;
  }
}

/*
** Sort an array of doubles.
**
** Algorithm: quicksort
**
** This is implemented separately rather than using the qsort() routine
** from the standard library because:
**
**    (1)  To avoid a dependency on qsort()
**    (2)  To avoid the function call to the comparison routine for each
**         comparison.
*/
static void sortDoubles(double *a, int n){
  int iLt;       /* Entries with index less than iLt are less than rPivot */
  int iGt;       /* Entries with index iGt or more are greater than rPivot */
  int i;         /* Loop counter */
  double rPivot; /* The pivot value */
  double rTmp;   /* Temporary used to swap two values */

  if( n<2 ) return;
  if( n>5 ){
    rPivot = (a[0] + a[n/2] + a[n-1])/3.0;
  }else{
    rPivot = a[n/2];
  }
  iLt = i = 0;
  iGt = n;
  while( i<iGt ){
    if( a[i]<rPivot ){
      if( i>iLt ){
        rTmp = a[i];
        a[i] = a[iLt];
        a[iLt] = rTmp;
      }
      iLt++;
      i++;
    }else if( a[i]>rPivot ){
      do{
        iGt--;
      }while( iGt>i && a[iGt]>rPivot );
      rTmp = a[i];
      a[i] = a[iGt];
      a[iGt] = rTmp;
    }else{
      i++;
    }
  }
  if( iLt>=2 ) sortDoubles(a, iLt);
  if( n-iGt>=2 ) sortDoubles(a+iGt, n-iGt);

/* Uncomment for testing */
#if 0
  for(i=0; i<n-1; i++){
    assert( a[i]<=a[i+1] );
  }
#endif
}


/*
** The "inverse" function for percentile(Y,P) is called to remove a
** row that was previously inserted by "step".
*/
static void percentInverse(sqlite3_context *pCtx,int argc,sqlite3_value **argv){
  Percentile *p;
  int eType;
  double y;
  int i;
  assert( argc==2 || argc==1 );

  /* Allocate the session context. */
  p = (Percentile*)sqlite3_aggregate_context(pCtx, sizeof(*p));
  assert( p!=0 );

  /* Ignore rows for which Y is NULL */
  eType = sqlite3_value_type(argv[0]);
  if( eType==SQLITE_NULL ) return;

  /* If not NULL, then Y must be numeric.  Otherwise throw an error.
  ** Requirement 4 */
  if( eType!=SQLITE_INTEGER && eType!=SQLITE_FLOAT ){
    return;
  }

  /* Ignore the Y value if it is infinity or NaN */
  y = sqlite3_value_double(argv[0]);
  if( percentIsInfinity(y) ){
    return;
  }
  if( p->bSorted==0 ){
    sortDoubles(p->a, p->nUsed);
    p->bSorted = 1;
  }else{
    percentAssertSorted(p);
  }
  p->bKeepSorted = 1;

  /* Find and remove the row */
  i = percentBinarySearch(p, y, 1);
  if( i>=0 ){
    p->nUsed--;
    if( i<p->nUsed ){
      memmove(&p->a[i], &p->a[i+1], (p->nUsed - i)*sizeof(p->a[0]));
    }
  }
  percentAssertSorted(p);
}

/*
** Compute the final output of percentile().  Clean up all allocated
** memory if and only if bIsFinal is true.
*/
static void percentCompute(sqlite3_context *pCtx, int bIsFinal){
  Percentile *p;
  unsigned i1, i2;
  double v1, v2;
  double ix, vx;
  p = (Percentile*)sqlite3_aggregate_context(pCtx, 0);
  if( p==0 ) return;
  if( p->a==0 ) return;
  if( p->nUsed ){
    if( p->bSorted==0 ){
      sortDoubles(p->a, p->nUsed);
      p->bSorted = 1;
    }else{
      percentAssertSorted(p);
    }
    ix = (p->rPct-1.0)*(p->nUsed-1)*0.01;
    i1 = (unsigned)ix;
    i2 = ix==(double)i1 || i1==p->nUsed-1 ? i1 : i1+1;
    v1 = p->a[i1];
    v2 = p->a[i2];
    vx = v1 + (v2-v1)*(ix-i1);
    sqlite3_result_double(pCtx, vx);
  }
  if( bIsFinal ){
    sqlite3_free(p->a);
    memset(p, 0, sizeof(*p));
  }else{
    p->bKeepSorted = 1;
  }
}
static void percentFinal(sqlite3_context *pCtx){
  percentCompute(pCtx, 1);
}
static void percentValue(sqlite3_context *pCtx){
  percentCompute(pCtx, 0);
}

#ifdef _WIN32
__declspec(dllexport)
#endif
int sqlite3_percentile_init(
  sqlite3 *db, 
  char **pzErrMsg, 
  const sqlite3_api_routines *pApi
){
  int rc = SQLITE_OK;
  SQLITE_EXTENSION_INIT2(pApi);
  (void)pzErrMsg;  /* Unused parameter */
  rc = sqlite3_create_window_function(db, "percentile", 2, 
                                 SQLITE_UTF8|SQLITE_INNOCUOUS, 0,
                                 percentStep, percentFinal,
                                 percentValue, percentInverse, 0);
  if( rc==SQLITE_OK ){
    rc = sqlite3_create_window_function(db, "median", 1, 
                                 SQLITE_UTF8|SQLITE_INNOCUOUS, 0,
                                 percentStep, percentFinal,
                                 percentValue, percentInverse, 0);
  }
  return rc;
}
