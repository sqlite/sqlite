/*
** 2026-02-04
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
** This file contains the SQLite vector ANN extension "vec1". To build
** as a loadable module on x86_64 with gcc:
**
**    gcc -O3 -DNDEBUG -mavx2 -mfma vec1.c -shared -fPIC -o vec1.so
** 
** Documentation available at:
**
**    https://sqlite.org/vec1
*/

/*
** The two distance metrics supported by the system. 
*/
#define VEC1_DISTANCE_L2   1
#define VEC1_DISTANCE_COS  2

/*
** Default values for training parameters.
*/
#define VEC1_TRAINING_DEFAULT_CODESIZE   0
#define VEC1_TRAINING_DEFAULT_NBUCKET    0
#define VEC1_TRAINING_DEFAULT_SVD_VERIFY 0
#define VEC1_TRAINING_DEFAULT_OPQ        0
#define VEC1_TRAINING_DEFAULT_NOPQ_ROUND 5
#define VEC1_TRAINING_DEFAULT_RESIDUAL   1
#define VEC1_TRAINING_DEFAULT_DISTANCE   VEC1_DISTANCE_L2

/*
** Max and min values for various parameters.
**
** VEC1_MAX_K:
**   At present, a KANN query with an index allocates K*16 bytes for
**   the heap data structure used to accumulate the K best matches.
**   In its default configuration, sqlite3_malloc() is limited to
**   1GB allocations. So limit K so as to at least avoid SQLITE_NOMEM 
**   errors in the default configuration.
*/
#define VEC1_MAX_K 32*1024*1024

/*
** Software version. Returned by "SELECT vec1_info()".
*/
#define VEC1_VERSION "0.8"

/*
** Values for Vec1ModelHeader.iVersion:
**
** VEC1_HEADER_VERSION:
**   Used by default, if nothing below applies.
**
** VEC1_HEADER_VERSION_BIN:
**   Used if the model uses a binary-quantizer.
*/
#define VEC1_HEADER_VERSION     (0*1000 + 4)
#define VEC1_HEADER_VERSION_BIN (0*1000 + 7)

/*
** Shadow tables version. Stored in %_config table. Corresponds to the 
** minimum version that matches the current shadow table setup.
*/
#define VEC1_CURRENT_FMTVERSION (0*1000 + 4)


#include "sqlite3.h"

/*
** Unless VEC1_STATIC is defined, compile as a loadable extension.
*/
#ifndef VEC1_STATIC

# include "sqlite3ext.h"
  SQLITE_EXTENSION_INIT3

#endif

/* System include files */
# include <assert.h>
# include <string.h>
# include <stdarg.h>
# include <math.h>
# include <stdio.h>
# include <stdlib.h>

/* Standard sized types. */
typedef sqlite3_int64  i64;
typedef sqlite3_uint64 u64;
typedef unsigned char   u8;
typedef unsigned int   u32;
typedef unsigned short u16;
typedef float          f32;
typedef int            i32;
typedef char            i8;

#if !defined(VEC1_TEST_SCALAR)

#define VERTICAL_SUM_GENERIC(aAcc, nAcc, I) {                                 \
  int dd;                                                                     \
  for(dd=4; dd<nAcc; dd++) aAcc[dd & 3] = I (aAcc[dd & 3], aAcc[dd]);         \
  for(dd=2; dd<4 && dd<nAcc; dd++) aAcc[dd & 1] = I (aAcc[dd & 1], aAcc[dd]); \
  if( nAcc>1 ) aAcc[0] = I (aAcc[0], aAcc[1]);                                \
}

#ifdef __AVX2__

# define VEC1_HAVE_AVX2 1

# define VEC1_TRANSPOSE_WIDTH 32
# define VEC1_NTRANSPOSE_WIDTH 8

/*
** Dimensions used for vec1BestMatchN() and vec1BestMatchTransposeN().
*/
# define VEC1_MULTIMATCH_NVEC  8
# define VEC1_MULTIMATCH_NCENT 8

/* AVX2 builds should use transposed arrays of centroids where possible.
** This avoids horizontal sums (expensive by design on AVX2) and frees
** up registers, allowing more concurrent FMA and other operations. */
# define VEC1_USE_TRANSPOSITION 1

/*
** When assembling the covariance matrix (part of OPQ rotation training),
** deal with (VEC1_COVAR_MULTIMATCH_MULT * VEC1_MULTIMATCH_NVEC) vectors
** at a time. See vec1CovarianceWork() for details.
*/
# define VEC1_COVAR_MULTIMATCH_MULT 1

# include <immintrin.h>

# if defined(__FMA__) || defined(_MSC_VER)
   /* With MSVC, __AVX2__ implies __FMA__ */
#  define FMADD(a,b,c) _mm256_fmadd_ps((a),(b),(c))
#  define FMADD64(a,b,c) _mm256_fmadd_pd((a),(b),(c))
# else
#  define FMADD(a,b,c) _mm256_add_ps((c), _mm256_mul_ps((a),(b)))
#  define FMADD64(a,b,c) _mm256_add_pd((c), _mm256_mul_pd((a),(b)))
# endif

/* Load 8 unaligned floats. Or 4 unaligned doubles. */
# define LOADU(a) _mm256_loadu_ps(a)
# define LOADU64(a) _mm256_loadu_pd(a)

/*
** Parameter vec is a __m256 register containing 8 floats. Parameter
** s is a float. This macro sets s to the sum of the 8 values in vec. i.e.
**
**       s = vec[0] + vec[1] + vec[2] + vec[3] 
**         + vec[4] + vec[5] + vec[6] + vec[7];
*/
# define HORIZONTAL_SUM(s, vec) \
    {                                                    \
      __m128 vc_high = _mm256_extractf128_ps(vec, 1);    \
      __m128 vc_low = _mm256_castps256_ps128(vec);       \
      __m128 vc128 = _mm_add_ps(vc_high, vc_low);        \
      vc128 = _mm_hadd_ps(vc128, vc128);                 \
      vc128 = _mm_hadd_ps(vc128, vc128);                 \
      s = _mm_cvtss_f32(vc128);                          \
    }

/*
** Parameter vec is a __m256 register containing 4 doubles. Parameter
** s is a double. This macro sets s to the sum of the 4 values in vec. i.e.
**
**       s = vec[0] + vec[1] + vec[2] + vec[3] 
*/
# define HORIZONTAL_SUM64(s, vec) \
    {                                                              \
      __m128d _lo = _mm256_castpd256_pd128(vec);                   \
      __m128d _hi = _mm256_extractf128_pd(vec, 1);                 \
      __m128d _sum = _mm_add_pd(_lo, _hi);                         \
      (s) = _mm_cvtsd_f64(_mm_hadd_pd(_sum, _sum));                \
    }

/*
** aAcc[] is an nAcc element array of type _m256 containing 8 32-bit floats.
** This macro sums the vectors and writes the results to aAcc[0].
*/
# define VERTICAL_SUM(aAcc, nAcc) VERTICAL_SUM_GENERIC(aAcc,nAcc,_mm256_add_ps)

#elif defined(__aarch64__)

# define VEC1_HAVE_NEON 1

# define VEC1_TRANSPOSE_WIDTH 32

# define VEC1_MULTIMATCH_NVEC  4
# define VEC1_MULTIMATCH_NCENT 4

# define VEC1_COVAR_MULTIMATCH_MULT 4

# include <arm_neon.h>

/*
** aAcc[] is an nAcc element array of type float32x4. This macro sums the
** vectors and writes the results to aAcc[0].
*/
#define VERTICAL_SUM(aAcc, nAcc) VERTICAL_SUM_GENERIC(aAcc, nAcc, vaddq_f32)

#endif

#endif /* VEC1_TEST_SCALAR */


#ifndef VEC1_TRANSPOSE_WIDTH

/* Scalar fallback. Even scalar needs transpose widths. */
# define VEC1_TRANSPOSE_WIDTH 8

# define VEC1_MULTIMATCH_NVEC  4
# define VEC1_MULTIMATCH_NCENT 4

# define VEC1_COVAR_MULTIMATCH_MULT 1

#endif

#define VEC1_LARGEST_INT64  (0xffffffff|(((i64)0x7fffffff)<<32))
#define VEC1_SMALLEST_INT64 (((i64)-1) - VEC1_LARGEST_INT64)

#define SWAP(T, a, b) { T tmp; tmp = a; a = b; b = tmp;  }

/*
** Return a hardware counter value. These are used to calculate the 
** percentages in the performance reports from vec1cat.qprofile and the 
** final progress callback from within vec1_train().
*/
static u64 vec1HardwareTimer(void){
  u64 ret = 0;

#if !defined(__STRICT_ANSI__)
# if defined(__aarch64__)
  __asm__ __volatile__("mrs %0, cntvct_el0" : "=r"(ret));
# elif defined(__GNUC__) && defined(__x86_64__)
  unsigned int lo, hi;
  __asm__ __volatile__("rdtscp" : "=a"(lo), "=d"(hi) :: "%rcx");
  ret = ((u64)hi << 32) | lo;
# endif
#endif

  return ret;
}


/* 
** Figure out which threading model to use. Define VEC1_THREADS_WINDOWS
** to use windows threads, VEC1_THREADS_PTHREADS to use pthreads, or
** neither to use no threads.
*/
#if !defined(VEC1_THREADS) || VEC1_THREADS!=0
# if defined(_WIN32)
#  define VEC1_THREADS_WINDOWS 1
#  define VEC1_THREADS 1
# elif defined(__unix__) || defined(__APPLE__) || defined(__linux__)
#  define VEC1_THREADS_PTHREADS
#  define VEC1_THREADS 1
# else
#  define VEC1_THREADS 0
# endif
#endif

#define size_of_array(X) ((int)(sizeof(X)/sizeof((X)[0])))
#define MAX(a,b) ((a<b) ? (b) : (a))
#define MIN(a,b) ((a>b) ? (b) : (a))

#define UNUSED_PARAMETER(x)    (void)(x)
#define UNUSED_PARAMETER2(x,y) (void)(x),(void)(y)

typedef struct Vec1Tab Vec1Tab;
typedef struct Vec1TabList Vec1TabList;

/*
** An instance of the following object is allocated when registering 
** the vec1 module with a database handle. A pointer is passed as the
** the context pointer to sqlite3_create_module_v2(). It maintains a
** list of all Vec1Tab tables attached to the database handle.
**
** This object does not need a mutex, as it belongs to a single db handle.
** The db handle mutex ensures exclusive access.
*/
struct Vec1TabList {
  int nRef;
  sqlite3 *db;
  Vec1Tab *pFirst;
  int nThread;                    /* Number of threads to use for various ops */
  double nProbeArg;               /* Default "nprobe" for queries */
};

/*
** This structure represents a model-header. When serialized, the format
** is a packed array of six 32-bit big-endian integers, in the order
** shown in the struct below.
*/
typedef struct Vec1ModelHeader Vec1ModelHeader;
struct Vec1ModelHeader {
  u32 iVersion;                   /* Version number currently 0004 */
  u32 flags;
  u32 nElem;                      /* Size of vectors in elements */
  u32 nCodebook;                  /* Number of codebooks for PQ */
  u32 nBucket;                    /* Number of buckets for IVF */
  u32 eDistance;                  /* A VEC1_DISTANCE_XXX value */
};
#define VEC1_HEADER_SIZE (6*sizeof_u32)

/*
** Candidate values for the "flags" field of a Vec1ModelHeader. As follows:
**
** VEC1_MODEL_INDEX: 
**   Set for any configuration except "none".
**
** VEC1_MODEL_ROTATE: 
**   Set if model includes a full rotation matrix.
**
** VEC1_MODEL_RESIDUAL: 
**   Set if residual, not full, vectors are encoded.
**
** VEC1_MODEL_BITQUANT: 
**   Set to use bitwise quantization instead of PQ. Compressed vectors are
**   a 32-bit float (the norm) followed by bitwise sketch of the vector
**   or residual vector.
**
** VEC1_MODEL_WHT: 
**   Do Walsh Hadamard transform.
*/
#define VEC1_MODEL_INDEX    0x01
#define VEC1_MODEL_ROTATE   0x02
#define VEC1_MODEL_RESIDUAL 0x04
#define VEC1_MODEL_BITQUANT 0x08
#define VEC1_MODEL_WHT      0x10

/* 
** Use these instead of actual sizeof() macros to avoid signed/unsigned
** compiler warnings. 
*/
#define sizeof_u16 2
#define sizeof_u32 4
#define sizeof_f32 4
#define sizeof_f64 8

/*
** The three types of vector elements (that will eventually be) supported.
*/
#define VEC1_TYPE_FLOAT32 0
#define VEC1_TYPE_INT32   1
#define VEC1_TYPE_INT8    2

/*
** Maximum allowed threads for both vec1 and vec1_train().
*/
#define VEC1_MAX_NTHREAD 128

static int vec1CorruptError(){
  return SQLITE_CORRUPT_VTAB;
}
#define VEC1_CORRUPT vec1CorruptError();

static void vec1PutU32(u8 *aBuf, u32 val){
  aBuf[0] = (val >> 24) & 0xFF;
  aBuf[1] = (val >> 16) & 0xFF;
  aBuf[2] = (val >>  8) & 0xFF;
  aBuf[3] = (val >>  0) & 0xFF;
}

/*
** Write a model header into buffer aBuf[].
*/
static void vec1HeaderWrite(
  u8 *aBuf, 
  int flags,
  int nElem,
  int nCodebook,
  int nBucket,
  int eDistance
){
  /* VEC1_HEADER_VERSION_BIN if the model uses a binary quantizer, 
  ** VEC1_HEADER_VERSION otherwise.  */
  u32 eHdrVersion = VEC1_HEADER_VERSION;
  if( flags & VEC1_MODEL_BITQUANT ){
    eHdrVersion = VEC1_HEADER_VERSION_BIN;
  }

  vec1PutU32(&aBuf[0], eHdrVersion);
  vec1PutU32(&aBuf[4], flags);
  vec1PutU32(&aBuf[8], nElem);
  vec1PutU32(&aBuf[12], nCodebook);
  vec1PutU32(&aBuf[16], nBucket);
  vec1PutU32(&aBuf[20], eDistance);
}


static u32 vec1GetU32(const u8 *aBuf){
  return (((u32)aBuf[0]) << 24)
       + (((u32)aBuf[1]) << 16)
       + (((u32)aBuf[2]) <<  8)
       + (((u32)aBuf[3]) <<  0);
}

/*
** Deserialize buffer aBuf into object p.
*/
static void vec1HeaderRead(const u8 *aBuf, Vec1ModelHeader *p){
  p->iVersion = vec1GetU32(&aBuf[0]);
  p->flags = vec1GetU32(&aBuf[4]);
  p->nElem = vec1GetU32(&aBuf[8]);
  p->nCodebook = vec1GetU32(&aBuf[12]);
  p->nBucket = vec1GetU32(&aBuf[16]);
  p->eDistance = vec1GetU32(&aBuf[20]);
}

/*
** Version of sqlite3_free() that can be used as a destructor function with
** things like sqlite3_bind_blob().
*/
static void vec1SqliteFree(void *x){
  sqlite3_free(x);
}

typedef struct Vec1AnnResult Vec1AnnResult;
struct Vec1AnnResult {
  sqlite3_int64 iRowid;
  double fDist;
};

typedef struct Vec1AnnHeap Vec1AnnHeap;
struct Vec1AnnHeap {
  Vec1AnnResult *aRes;            /* Heap array */
  i64 nRes;                       /* Current number of elements */
  i64 nMax;                       /* Capacity */
  i64 nAlloc;                     /* Current allocated size of aRes[] */
  int rc;                         /* Error code (perhaps SQLITE_NOMEM) */
  int bStreaming;
  double fMin;                    /* Current threshold for admittance */
};

typedef struct Vec1Buffer Vec1Buffer;
struct Vec1Buffer {
  unsigned char *a;
  int n;
  int nAlloc;
};

/*
** Return the dot-product of vectors A and B, each d elements in size.
*/
static float vec1DotProduct(
  int d,
  const float *A,
  const float *B
){
  float sum = 0.0f;
  int j = 0;

#ifdef VEC1_HAVE_AVX2
# ifndef VEC1_DOT_NACC
#  define VEC1_DOT_NACC 4
# endif
  const int nAcc = VEC1_DOT_NACC;
  int cc;

  /* Create and initialize to zero nAcc accumulator registers. */
  __m256 acc[VEC1_DOT_NACC];
  for(cc=0; cc<nAcc; cc++) acc[cc] = _mm256_setzero_ps();

  for(; j<=(d-(8*nAcc)); j+=(8*nAcc)){
    for(cc=0; cc<nAcc; cc++){
      acc[cc] = FMADD(LOADU(&A[j+cc*8]), LOADU(&B[j+cc*8]), acc[cc]);
    }
  }

  /* Sum all nAcc accumulators into acc[0]. */
  VERTICAL_SUM(acc, nAcc);

  /* Process the part of the tail that can be done 8 elements at a time
  ** using acc[0] as the accumulator. Then sum all 8 elements of acc[0]
  ** into scalar float "sum".  */
  for(; j<=(d-8); j+=8){
    acc[0] = FMADD(LOADU(&A[j]), LOADU(&B[j]), acc[0]);
  }
  HORIZONTAL_SUM(sum, acc[0]);
#endif
#ifdef VEC1_HAVE_NEON
# ifndef VEC1_DOT_NACC
#  define VEC1_DOT_NACC 8
# endif
  const int nAcc = VEC1_DOT_NACC;
  int cc;

  /* Create and initialize to zero nAcc accumulator registers. */
  float32x4_t acc[VEC1_DOT_NACC];
  for(cc=0; cc<nAcc; cc++) acc[cc] = vdupq_n_f32(0.0f);

  for(; j<=(d-(4*nAcc)); j+=(4*nAcc)){
    for(cc=0; cc<nAcc; cc++){
      float32x4_t a = vld1q_f32(&A[j+cc*4]);
      float32x4_t b = vld1q_f32(&B[j+cc*4]);
      acc[cc] = vfmaq_f32(acc[cc], a, b);
    }
  }

  /* Sum all nAcc accumulators into acc[0]. */
  VERTICAL_SUM(acc, nAcc);

  /* Process the part of the tail that can be done 4 elements at a time
  ** using acc[0] as the accumulator. Then sum all 4 elements of acc[0]
  ** into scalar float "sum".  */
  for(; j<=(d-4); j+=4){
    acc[0] = vfmaq_f32(acc[0], vld1q_f32(&A[j]), vld1q_f32(&B[j]));
  }
  sum = vaddvq_f32(acc[0]);
#endif

  for(; j<d; j++){
    sum += A[j] * B[j];
  }

  return sum;
}

/*
** Rotate x -> y using row-major matrix A.
*/
static void vec1RotateVector(
  int d,
  const float* A,                 /* d x d, row-major */
  const float* x,                 /* input vector, length d */
  float* y                        /* output vector, length d */
){
  int i;
  for(i=0; i<d; i++){
    y[i] = vec1DotProduct(d, &A[i*d], x);
  }
}

/*
** Normalize nElem dimension vector aElem. 
**
** Assume a vector with magnitude 0.0 normalizes to itself. 
*/
static void vec1NormalizeVector(float *aElem, int nElem){
  int ii;
  double fSum = vec1DotProduct(nElem, aElem, aElem);
  if( fSum==0.0 ) return;
  fSum = 1.0 / sqrt(fSum);
  for(ii=0; ii<nElem; ii++){
    aElem[ii] = (float)(aElem[ii] * fSum);
  }
}

#define VEC1_INITIAL_STREAMING_ALLOC ((128*1024)/16)

/*
** Initialize a new, existing, heap object. 
*/
static int vec1HeapInit(Vec1AnnHeap *p, i64 nMax, int bStreaming){
  i64 nAlloc = (bStreaming ? VEC1_INITIAL_STREAMING_ALLOC : nMax);
  nAlloc = MAX(nAlloc, nMax);
  p->aRes = (Vec1AnnResult *)sqlite3_malloc64(sizeof(Vec1AnnResult) * nAlloc);
  if( p->aRes==0 ){
    return SQLITE_NOMEM;
  }
  p->nRes = 0;
  p->nMax = nMax;
  p->nAlloc = nAlloc;
  p->bStreaming = bStreaming;
  p->fMin = INFINITY;
  return SQLITE_OK;
}

/*
** A new element, element i, has just been added to the heap as a new leaf
** node. This function repairs the heap by bubbling-up from the new node
** to the root.
*/
static void vec1HeapBubbleUp(Vec1AnnHeap *p, int i){
  int idx = i;
  while( idx>0 ){
    int idxParent = (idx-1)/2;
    if( p->aRes[idxParent].fDist>p->aRes[idx].fDist ) break;
    SWAP(Vec1AnnResult, p->aRes[idx], p->aRes[idxParent]);
    idx = idxParent;
  }
}

/*
** A new element has just replaced the heap root node (i.e. the previous
** entry with the largest distance in the heap has been replaced by a 
** smaller distance. This function repairs the heap by bubbling-down from 
** the new root towards the leaves.
*/
static void vec1HeapBubbleDown(Vec1AnnHeap *p){
  int idx = 0;
  for(;;){
    int idxLeft = idx*2 + 1;
    int idxRight = idxLeft + 1;
    int idxMax = idx;

    if( idxLeft<p->nMax && p->aRes[idxLeft].fDist>p->aRes[idxMax].fDist ){
      idxMax = idxLeft;
    }
    if( idxRight<p->nMax && p->aRes[idxRight].fDist>p->aRes[idxMax].fDist ){
      idxMax = idxRight;
    }
    if( idxMax==idx ) break;

    SWAP(Vec1AnnResult, p->aRes[idx], p->aRes[idxMax]);
    idx = idxMax;
  }
}

/*
** Add an entry to the heap structure. If the heap is already full, the
** entry with the largest fDist value seen so far is discarded.
*/
static void vec1HeapInsert(Vec1AnnHeap *p, sqlite3_int64 iRowid, double fDist){
  if( p->nRes<p->nMax ){
    p->aRes[p->nRes].iRowid = iRowid;
    p->aRes[p->nRes].fDist = fDist;
    vec1HeapBubbleUp(p, (int)p->nRes);
    p->nRes++;
  }else if( p->bStreaming ){

    if( p->nRes>=p->nAlloc ){
      i64 nNew = p->nAlloc * 2;
      Vec1AnnResult *aNew = 0;
      if( p->rc==SQLITE_OK ){
        i64 nByte = nNew*sizeof(Vec1AnnResult);
        aNew = (Vec1AnnResult*)sqlite3_realloc64(p->aRes, nByte);
      }
      if( aNew==0 ){
        p->rc = SQLITE_NOMEM;
        return;
      }
      p->aRes = aNew;
      p->nAlloc = nNew;
    }

    p->aRes[p->nRes].iRowid = iRowid;
    p->aRes[p->nRes].fDist = fDist;
    if( fDist<p->aRes[0].fDist ){
      SWAP(Vec1AnnResult, p->aRes[0], p->aRes[p->nRes]);
      vec1HeapBubbleDown(p);
    }
    p->nRes++;
  }else if( fDist<p->aRes[0].fDist ){
    p->aRes[0].iRowid = iRowid;
    p->aRes[0].fDist = fDist;
    vec1HeapBubbleDown(p);
    p->fMin = p->aRes[0].fDist;
  }
}


/*
** Ensure there is room in the buffer for another nByte bytes of data. That
** is, make sure the allocated size is at least (pBuf->n + nByte).
**
** Return SQLITE_OK if successful, or SQLITE_NOMEM if an OOM error is
** encountered.
*/
static int vec1BufferGrow(Vec1Buffer *pBuf, sqlite3_int64 nByte){
  if( pBuf->n+nByte>pBuf->nAlloc ){
    sqlite3_int64 nNew = pBuf->nAlloc;
    unsigned char *aNew = 0;

    if( nNew==0 ) nNew = nByte + 100;
    while( nNew < (pBuf->n + nByte) ){
      nNew = nNew * 2;
    }
    aNew = sqlite3_realloc64(pBuf->a, nNew);
    if( aNew==0 ){
      return SQLITE_NOMEM;
    }
    pBuf->a = aNew;
    pBuf->nAlloc = (int)nNew;
  }

  return SQLITE_OK;
}

/*
** Ensure that the buffer passed as the first argument has at least nByte
** bytes of space allocated.
*/
static int vec1BufferSize(Vec1Buffer *pBuf, sqlite3_int64 nByte){
  if( nByte>pBuf->nAlloc ){
    u8 *aNew = sqlite3_realloc64(pBuf->a, nByte);
    if( aNew==0 ){
      return SQLITE_NOMEM;
    }
    pBuf->a = aNew;
    pBuf->nAlloc = (int)nByte;
  }
  return SQLITE_OK;
}

/*
** Free the allocation and zero the buffer structure.
*/
static void vec1BufferFree(Vec1Buffer *pBuf){
  sqlite3_free(pBuf->a);
  memset(pBuf, 0, sizeof(Vec1Buffer));
}


/*
** Set the error message in context object pCtx to the result of formatting
** zFmt printf() style.
*/
static void vec1ResultErrorF(
  sqlite3_context *pCtx,          /* Context to set error in */
  const char *zFmt,               /* printf() style format for error message */
  ...                             /* Trailing arguments for printf() */
){
  char *zMsg = 0;
  va_list ap;
  va_start(ap, zFmt);
  zMsg = sqlite3_vmprintf(zFmt, ap);
  va_end(ap);
  sqlite3_result_error(pCtx, zMsg, -1);
  sqlite3_free(zMsg);
}

/*
** Attempt to allocate and zero a block of nByte bytes of memory. If 
** succesful, return a pointer to the new allocation. It is the responsibility
** of the caller to eventually free the allocation using sqlite3_free().
**
** If the allocation attempt fails, return 0 and leave an OOM error in 
** context object pCtx.
*/
static void *vec1ContextMalloc(sqlite3_context *pCtx, sqlite3_int64 nByte){
  void *pRet = sqlite3_malloc64(nByte);
  if( pRet==0 ){
    sqlite3_result_error_nomem(pCtx);
  }else{
    memset(pRet, 0, nByte);
  }
  return pRet;
}

/*
** Implementation of scalar function vec1_info().
*/
static void vec1InfoFunc(
  sqlite3_context *pCtx, 
  int nVal, 
  sqlite3_value **aVal
){
  char *z = 0;
  const char *zSimd = 0;
  const char *zThreads = 0;

  UNUSED_PARAMETER2(nVal, aVal);

#if defined(VEC1_HAVE_AVX2)
  zSimd = "AVX2";
#elif defined(VEC1_HAVE_NEON)
  zSimd = "NEON";
#else
  zSimd = "Scalar";
#endif

#if VEC1_THREADS
  zThreads = "multi-threaded";
#else
  zThreads = "single-threaded";
#endif

  z = sqlite3_mprintf("version %s (%s, %s)", VEC1_VERSION, zSimd, zThreads);

  if( z==0 ){
    sqlite3_result_error_nomem(pCtx);
  }else{
    sqlite3_result_text(pCtx, z, -1, vec1SqliteFree);
  }
}


/*
** Return true if character c counts as whitespace. This is used
** by vec1_from_json().
*/
static int vec1_isspace(char c){
  return (c==' ');
}

/*
** Implementation of scalar function vec1_from_json(). Converts a json array
** of numbers into a vector in vec1 native format (32-bit little-endian 
** floats).
*/
static void vec1FromJsonFunc(
  sqlite3_context *pCtx, 
  int nVal, 
  sqlite3_value **aVal
){
  const char *zJson = (const char*)sqlite3_value_text(aVal[0]);
  int nJson = sqlite3_value_bytes(aVal[0]);
  const char *p = zJson;

  assert( nVal==1 );
  UNUSED_PARAMETER(nVal);

  float *aVec = 0;
  int nVec = 0;

  /* vec1_from_json(NULL) returns NULL */
  if( sqlite3_value_type(aVal[0])==SQLITE_NULL ) return;

  /* Allocate space for the return value */
  aVec = (float*)sqlite3_malloc(sizeof_f32*(nJson/2));
  if( !aVec ){
    sqlite3_result_error_nomem(pCtx);
    return;
  }

  while( vec1_isspace(*p) ) p++;
  if( *p!='[' ) goto parse_failed;
  p++;

  while( 1 ){
    char *p2 = 0;
    double rVal = 0.0;

    while( vec1_isspace(*p) ) p++;
    rVal = strtod(p, &p2);
    if( p==p2 ) goto parse_failed;

    p = p2;
    aVec[nVec] = (float)rVal;
    nVec++;

    while( vec1_isspace(*p) ) p++;
    if( *p==']' ){
      p++;
      break;
    }
    if( *p!=',' ) goto parse_failed;
    p++;
  }

  while( vec1_isspace(*p) ) p++;
  if( *p ) goto parse_failed;

  sqlite3_result_blob(pCtx, (void*)aVec, nVec*sizeof_f32, vec1SqliteFree);
  return;

 parse_failed:
  sqlite3_free(aVec);
  sqlite3_result_error(pCtx, "error parsing json vector", -1);
}

/*
** SQL Scalar Functions:
**
**     vec1_to_json(BLOB)
**     vec1_to_json_f(BLOB)
**     vec1_to_json_i(BLOB)
*/
static void vec1ToJsonGeneric(
  sqlite3_context *pCtx, 
  int eType,
  int nBytePerElem,
  sqlite3_value *pVal
){
  int ii;
  sqlite3 *db = sqlite3_context_db_handle(pCtx);
  sqlite3_str *pStr = 0;

  int n = sqlite3_value_bytes(pVal);
  const void *a = sqlite3_value_blob(pVal);

  if( n==0 || (n%nBytePerElem)!=0 ){
    sqlite3_result_error(pCtx, "vec1: invalid vector argument", -1);
    return;
  }
  n = n / nBytePerElem;

  pStr = sqlite3_str_new(db);
  if( eType==VEC1_TYPE_FLOAT32 ){
    const float *aFloat = (const float*)a;
    sqlite3_str_appendf(pStr, "[%.9g", aFloat[0]);
    for(ii=1; ii<n; ii++){
      sqlite3_str_appendf(pStr, ",%.9g", aFloat[ii]);
    }
#if 0
  }else if( eType==VEC1_TYPE_INT8 ){
    const i8 *aInt = (const i8*)a;
    sqlite3_str_appendf(pStr, "[%d", (int)aInt[0]);
    for(ii=1; ii<n; ii++){
      sqlite3_str_appendf(pStr, ",%d", (int)aInt[ii]);
    }
#endif
  }else{
    const int *aInt = (const int*)a;
    assert( eType==VEC1_TYPE_INT32 );
    sqlite3_str_appendf(pStr, "[%d", aInt[0]);
    for(ii=1; ii<n; ii++){
      sqlite3_str_appendf(pStr, ",%d", aInt[ii]);
    }
  }

  sqlite3_str_appendall(pStr, "]");
  if( sqlite3_str_errcode(pStr)!=SQLITE_OK ){
    sqlite3_result_error_code(pCtx, sqlite3_str_errcode(pStr));
  }else{
    sqlite3_result_text(pCtx, sqlite3_str_value(pStr), -1, vec1SqliteFree);
  }
  sqlite3_str_finish(pStr);
}

/*
** Scalar function vec1_to_json_f().
*/
static void vec1ToJsonFFunc(
  sqlite3_context *pCtx, 
  int nVal, 
  sqlite3_value **aVal
){
  assert( nVal==1 );
  UNUSED_PARAMETER(nVal);
  vec1ToJsonGeneric(pCtx, VEC1_TYPE_FLOAT32, sizeof_f32, aVal[0]);
}

/*
** Scalar function vec1_to_json_i().
*/
static void vec1ToJsonIFunc(
  sqlite3_context *pCtx, 
  int nVal, 
  sqlite3_value **aVal
){
  assert( nVal==1 );
  UNUSED_PARAMETER(nVal);
  vec1ToJsonGeneric(pCtx, VEC1_TYPE_INT32, sizeof(int), aVal[0]);
}

/*
** This function is used to parse the small json objects used to specify
** configuration options to components in this module.
**
** For each key-value pair in json object zJson, the callback x is 
** invoked once, as follows:
**
**      x(pCtx, zKey, eType, iValue, fValue, zValue, pzErr)
**
** Parameters are as follows:
**
**   pCtx:  Copy of pCtx passed to this function.
**   zKey:  Name of key.
**   eType: One of SQLITE_INTEGER, SQLITE_FLOAT or SQLITE_TEXT.
**   iValue: For SQLITE_INTEGER, the value. Otherwise undefined.
**   fValue: For SQLITE_INTEGER or SQLITE_FLOAT, the value. Or undefined.
**   zValue: For SQLITE_TEXT the value. Or undefined.
**   pzErr: Output variable used by callback to set an error message.
**
** If x() returns other than SQLITE_OK, processing stops and the error
** code is returned to the caller. In this case x() may set *pzErr to
** an sqlite3_malloc() buffer containing an error message.
*/
static int vec1ParseJsonConfig(
  sqlite3 *db,
  const char *zJson,
  int (*x)(
      void *pCtx, 
      const char *zKey, 
      int eType, 
      i64 iValue, 
      double fValue, 
      const char *zValue, 
      char **pzErr
  ),
  void *pCtx,
  char **pz
){
  sqlite3_stmt *pStmt = 0;
  int rc = SQLITE_OK;
  int rc2 = SQLITE_OK;

  rc = sqlite3_prepare_v2(
      db, "SELECT key, value FROM json_each(?)", -1, &pStmt, 0
  );

  if( rc==SQLITE_OK ){
    rc = sqlite3_bind_text(pStmt, 1, zJson, -1, SQLITE_STATIC);
  }
  while( rc==SQLITE_OK && SQLITE_ROW==sqlite3_step(pStmt) ){
    const char *zKey = (const char*)sqlite3_column_text(pStmt, 0);
    double fVal = 0;
    i64 iVal = 0;
    int eType = sqlite3_value_numeric_type(sqlite3_column_value(pStmt, 1));

    switch( eType ){
      case SQLITE_INTEGER:
        iVal = sqlite3_column_int64(pStmt, 1);
        fVal = (double)iVal;
        break;

      case SQLITE_FLOAT:
        fVal = sqlite3_column_double(pStmt, 1);
        if( fVal==(double)(i64)fVal ){
          iVal = (i64)fVal;
          eType = SQLITE_INTEGER;
        }
        break;

      case SQLITE_TEXT:
        break;

      default:
        /* We don't want any NULL values. Or blob, if that is even possible. */
        *pz = sqlite3_mprintf("vec1: null value in json object");
        rc = SQLITE_ERROR;
        break;
    }

    if( rc==SQLITE_OK ){
      const char *zVal = (const char*)sqlite3_column_text(pStmt, 1);
      rc = x(pCtx, zKey, eType, iVal, fVal, zVal, pz);
    }
  }

  rc2 = sqlite3_finalize(pStmt);
  if( rc==SQLITE_OK && rc2!=SQLITE_OK ){
    rc = rc2;
    *pz = sqlite3_mprintf("%s", sqlite3_errmsg(db));
  }

  return rc;
}


/*
** Write val into aBuf[] as a 64-bit unsigned big-endian integer.
*/
static void vec1PutU64(u8 *aBuf, u64 val){
  aBuf[0] = (val >> 56) & 0xFF;
  aBuf[1] = (val >> 48) & 0xFF;
  aBuf[2] = (val >> 40) & 0xFF;
  aBuf[3] = (val >> 32) & 0xFF;
  aBuf[4] = (val >> 24) & 0xFF;
  aBuf[5] = (val >> 16) & 0xFF;
  aBuf[6] = (val >>  8) & 0xFF;
  aBuf[7] = (val >>  0) & 0xFF;
}

/*
** Read and return a 64-bit unsigned big-endian integer from buffer aBuf[].
*/
static u64 vec1GetU64(const u8 *aBuf){
  return (((u64)aBuf[0]) << 56)
       + (((u64)aBuf[1]) << 48)
       + (((u64)aBuf[2]) << 40)
       + (((u64)aBuf[3]) << 32) 
       + (((u64)aBuf[4]) << 24)
       + (((u64)aBuf[5]) << 16)
       + (((u64)aBuf[6]) <<  8)
       + (((u64)aBuf[7]) <<  0);
}

/*
** Subtract a2 from a1 and leave the result in aOut. i.e.
**
**     aOut = a1 - a2
*/
static void vec1Sub(float *aOut, const float *a1, const float *a2, int n){
  int i;
  for(i=0; i<n; i++){
    aOut[i] = a1[i] - a2[i];
  }
}

/*
** Subtract vector a2 from a1. i.e. do:
**
**     a1 = a1 - a2
*/
static void vec1SubInPlace(float *a1, const float *a2, int n){
  vec1Sub(a1, a1, a2, n);
}


/*
** Return the L2 distance between vectors a1[] and a2[], each n elements
** in size. 
*/
static double vec1L2Dist(
  const float *a1,
  const float *a2,
  int n
){
  double ret = 0.0f;
  int i = 0;

#ifdef VEC1_HAVE_AVX2
# ifndef VEC1_L2_NACC 
#  define VEC1_L2_NACC 4
# endif
  const int nAcc = VEC1_L2_NACC;
  int c;

  /* Create and initialize to zero nAcc accumulator registers. */
  __m256 acc[VEC1_L2_NACC];
  for(c=0; c<nAcc; c++) acc[c] = _mm256_setzero_ps();

  for(; i<=(n-(8*nAcc)); i+=(8*nAcc)){
    __m256 d[VEC1_L2_NACC];
    for(c=0; c<nAcc; c++){
      d[c] = _mm256_sub_ps(LOADU(&a1[i+c*8]), LOADU(&a2[i+c*8]));
      acc[c] = FMADD(d[c], d[c], acc[c]);
    }
  }

  /* Sum all nAcc accumulators into acc[0]. */
  VERTICAL_SUM(acc, nAcc);

  /* Process the part of the tail that can be done 8 elements at a time
  ** using acc[0] as the accumulator. Then sum all 8 elements of acc[0] 
  ** into scalar float "ret".  */
  for(; i<=(n-8); i+=8){
    __m256 d = _mm256_sub_ps(LOADU(&a1[i]), LOADU(&a2[i]));
    acc[0] = FMADD(d, d, acc[0]);
  }
  HORIZONTAL_SUM(ret, acc[0]);
#endif /* VEC1_HAVE_AVX2 */

#ifdef VEC1_HAVE_NEON
# ifndef VEC1_L2_NACC 
#  define VEC1_L2_NACC 8
# endif
  const int nAcc = VEC1_L2_NACC;
  int c, s;

  /* Create and initialize to zero nAcc accumulator registers. */
  float32x4_t acc[VEC1_L2_NACC];
  for(c=0; c<nAcc; c++) acc[c] = vdupq_n_f32(0.0f);

  for(; i<=(n-(4*nAcc)); i+=(4*nAcc)){
    float32x4_t d[VEC1_L2_NACC];
    for(c=0; c<nAcc; c++){
      d[c] = vsubq_f32(vld1q_f32(&a1[i+c*4]), vld1q_f32(&a2[i+c*4]));
      acc[c] = vmlaq_f32(acc[c], d[c], d[c]);
    }
  }

  /* Sum all nAcc accumulators into acc[0]. */
  VERTICAL_SUM(acc, nAcc);

  for(; i<=(n-4); i+=4){
    float32x4_t d = vsubq_f32(vld1q_f32(&a1[i]), vld1q_f32(&a2[i]));
    acc[0] = vmlaq_f32(acc[0], d, d);
  }
  ret = vaddvq_f32(acc[0]);
#endif /* VEC1_HAVE_NEON */

  for(; i<n; i++){
    double diff = a1[i] - a2[i];
    ret += (diff * diff);
  }

  return ret;
}

/*
** Return the square of the norm for vector a1[n].
*/
static double vec1VectorNorm2(const float *a1, int n){
  return vec1DotProduct(n, a1, a1);
}

/*
** Return the cosine distance between vectors a1 and a2, each n elements
** in size.
*/
static double vec1CosDist(
  const float *a1,
  const float *a2,
  int n
){
  int i = 0;
  float dot = 0.0f;
  float a1s = 0.0f;
  float a2s = 0.0f;

#ifdef VEC1_HAVE_AVX2
  __m256 va1s = _mm256_setzero_ps();
  __m256 va2s = _mm256_setzero_ps();
  __m256 vdot = _mm256_setzero_ps();

  for(; i<=(n-8); i+=8){
    __m256 va1 = _mm256_loadu_ps(&a1[i]);
    __m256 va2 = _mm256_loadu_ps(&a2[i]);

    va1s = FMADD(va1, va1, va1s);
    va2s = FMADD(va2, va2, va2s);
    vdot = FMADD(va1, va2, vdot);
  }

  HORIZONTAL_SUM(a1s, va1s);
  HORIZONTAL_SUM(a2s, va2s);
  HORIZONTAL_SUM(dot, vdot);
#endif /* VEC1_HAVE_AVX2 */

#ifdef VEC1_HAVE_NEON
  float32x4_t va1s = vdupq_n_f32(0.0f);
  float32x4_t va2s = vdupq_n_f32(0.0f);
  float32x4_t vdot = vdupq_n_f32(0.0f);

  for(; i<=(n-4); i+=4){
    float32x4_t va1 = vld1q_f32(&a1[i]);
    float32x4_t va2 = vld1q_f32(&a2[i]);

    va1s = vmlaq_f32(va1s, va1, va1);
    va2s = vmlaq_f32(va2s, va2, va2);
    vdot = vmlaq_f32(vdot, va1, va2);
  }
  a1s = vaddvq_f32(va1s);
  a2s = vaddvq_f32(va2s);
  dot = vaddvq_f32(vdot);
#endif /* VEC1_HAVE_NEON */

  for(; i<n; i++){
    dot += a1[i] * a2[i];
    a1s += a1[i] * a1[i];
    a2s += a2[i] * a2[i];
  }

  /* If either vector was a zero vector, assume the two inputs are 
  ** perfectly orthogonal. */
  if( a1s==0.0 || a2s==0.0 ) return 1.0;

  return 1.0 - (dot / sqrt(a1s * a2s));
}

/*
** This function must be called with two blobs:
**
**     vec1_l2_distance(BLOB, BLOB)
**
** each of which are treated as vectors of type float in machine byte order.
** This function returns the L2 distance between the two vectors.
*/
static void vec1DistanceFuncL2(
  sqlite3_context *pCtx, 
  int nVal, 
  sqlite3_value **aVal
){
  const float *a1 = (const float*)sqlite3_value_blob(aVal[0]);
  const float *a2 = (const float*)sqlite3_value_blob(aVal[1]);
  int n1 = sqlite3_value_bytes(aVal[0]);
  int n2 = sqlite3_value_bytes(aVal[1]);

  assert( nVal==2 );
  UNUSED_PARAMETER(nVal);

  if( n1!=n2 || (n1 % sizeof_f32)!=0 
   || sqlite3_value_type(aVal[0])!=SQLITE_BLOB
   || sqlite3_value_type(aVal[1])!=SQLITE_BLOB
  ){
    sqlite3_result_error(pCtx, "vec1_l2_distance: bad arguments", -1);
    return;
  }

  sqlite3_result_double(pCtx, vec1L2Dist(a1, a2, n1 / sizeof_f32));
}

/*
** This function must be called with two blobs:
**
**     vec1_cos_distance(BLOB, BLOB)
**
** each of which are treated as vectors of type float in machine byte order.
** This function returns the L2 distance between the two vectors.
*/
static void vec1DistanceFuncCos(
  sqlite3_context *pCtx, 
  int nVal, 
  sqlite3_value **aVal
){
  const float *a1 = (const float*)sqlite3_value_blob(aVal[0]);
  const float *a2 = (const float*)sqlite3_value_blob(aVal[1]);
  int n1 = sqlite3_value_bytes(aVal[0]);
  int n2 = sqlite3_value_bytes(aVal[1]);

  assert( nVal==2 );
  UNUSED_PARAMETER(nVal);

  if( n1!=n2 || (n1 % sizeof_f32)!=0
   || sqlite3_value_type(aVal[0])!=SQLITE_BLOB
   || sqlite3_value_type(aVal[1])!=SQLITE_BLOB
  ){
    sqlite3_result_error(pCtx, "vec1_cos_distance: bad arguments", -1);
    return;
  }

  sqlite3_result_double(pCtx, vec1CosDist(a1, a2, n1 / sizeof_f32));
}

/*
** Allocate and return a zeroed buffer nByte bytes in size.
*/
static void *vec1MallocZero(i64 nByte){
  void *pRet = sqlite3_malloc64(nByte);
  if( pRet ){
    memset(pRet, 0, nByte);
  }
  return pRet;
}

typedef struct Vec1Thread Vec1Thread;
typedef struct Vec1Mutex Vec1Mutex;
typedef struct Vec1Cond Vec1Cond;

/****************************************************************************
****************************************************************************/
/********************************* Unix Pthreads ****************************/
#if defined(VEC1_THREADS_PTHREADS)

#include <pthread.h>

/*
** Unix version of thread object.
*/
struct Vec1Thread {
  pthread_t tid;
};

/*
** Unix version of mutex object.
*/
struct Vec1Mutex {
  pthread_mutex_t mutex;
};

/*
** Unix version of condition variable.
*/
struct Vec1Cond {
  pthread_cond_t cond;
};

/*
** Create a new thread running xTask(pIn). If successful, set *ppThread
** to the new thread handle and return SQLITE_OK. Otherwise, if an
** error occurs, set *ppThread to 0 and return either SQLITE_ERROR 
** or SQLITE_NOMEM as appropriate.
*/
static int vec1ThreadCreate(
  Vec1Thread **ppThread,
  void *(*xTask)(void*),
  void *pIn
){
  Vec1Thread *p = sqlite3_malloc(sizeof(Vec1Thread));
  if( p==0 ){ *ppThread = 0; return SQLITE_NOMEM; }
  if( pthread_create(&p->tid, 0, xTask, pIn)!=0 ){
    sqlite3_free(p);
    *ppThread = 0;
    return SQLITE_ERROR;
  }
  *ppThread = p;
  return SQLITE_OK;
}

/*
** Join the thread pThread. If successful set *ppOut to the value
** returned by the thread's xTask routine and return SQLITE_OK. 
** Otherwise, return SQLITE_ERROR and set *ppOut to 0.
**
** The thread handle may not be used after this function has been
** called. The results of doing so are undefined.
*/
static int vec1ThreadJoin(Vec1Thread *pThread, void **ppOut){
  int rc = SQLITE_OK;
  if( pthread_join(pThread->tid, ppOut)!=0 ){
    rc = SQLITE_ERROR;
    if( ppOut ) *ppOut = 0;
  }
  sqlite3_free(pThread);
  return rc;
}

/*
** Create a new mutex. If successful, set *ppMutex to the new
** mutex handle and return SQLITE_OK. Otherwise, set *ppMutex to 0
** and return an SQLite error code - SQLITE_NOMEM or SQLITE_ERROR as
** appropriate.
*/
static int vec1MutexCreate(Vec1Mutex **ppMutex){
  Vec1Mutex *p = sqlite3_malloc(sizeof(Vec1Mutex));
  if( p==0 ){ *ppMutex = 0; return SQLITE_NOMEM; }
  if( pthread_mutex_init(&p->mutex, 0)!=0 ){
    sqlite3_free(p);
    *ppMutex = 0;
    return SQLITE_ERROR;
  }
  *ppMutex = p;
  return SQLITE_OK;
}


/*
** Destroy (free) the mutex passed as the only argument.
*/
static void vec1MutexDestroy(Vec1Mutex *pMutex){
  pthread_mutex_destroy(&pMutex->mutex);
  sqlite3_free(pMutex);
}


/*
** Enter the mutex passed as the only argument. The results of calling
** this function on a mutex that is already held are undefined.
*/
static void vec1MutexEnter(Vec1Mutex *pMutex){
  pthread_mutex_lock(&pMutex->mutex);
}


/*
** Leave the mutex passed as the only argument.
*/
static void vec1MutexLeave(Vec1Mutex *pMutex){
  pthread_mutex_unlock(&pMutex->mutex);
}


/*
** Create a new posix style condition variable.
*/
static int vec1CondCreate(Vec1Cond **ppCond){
  Vec1Cond *p = sqlite3_malloc(sizeof(Vec1Cond));
  if( p==0 ){ *ppCond = 0; return SQLITE_NOMEM; }
  if( pthread_cond_init(&p->cond, 0)!=0 ){
    sqlite3_free(p);
    *ppCond = 0;
    return SQLITE_ERROR;
  }
  *ppCond = p;
  return SQLITE_OK;
}


/*
** Destroy the condition variable passed as the only argument.
*/
static void vec1CondDestroy(Vec1Cond *pCond){
  pthread_cond_destroy(&pCond->cond);
  sqlite3_free(pCond);
}

/*
** Mutex pMutex must be held when this is called. It causes the
** thread to atomically relinquish the mutex and wait on the 
** condition variable. When the condition is signaled by another
** thread calling vec1CondBroadcast(), this thread will reaquire
** mutex pMutex and continue.
*/
static void vec1CondWait(Vec1Mutex *pMutex, Vec1Cond *pCond){
  pthread_cond_wait(&pCond->cond, &pMutex->mutex);
}

/*
** Wake up threads waiting on the condition variable passed as
** the only argument.
*/
static void vec1CondBroadcast(Vec1Cond *pCond){
  pthread_cond_broadcast(&pCond->cond);
}


#endif /* SQLITE_OS_UNIX && defined(SQLITE_MUTEX_PTHREADS) */
/******************************** End Unix Pthreads *************************/


/********************************* Win32 Threads ****************************/
#if defined(VEC1_THREADS_WINDOWS)

#include <windows.h>

struct Vec1Thread {
  HANDLE hThread;
  void *(*xTask)(void*);
  void *pIn;
  void *pOut;
};

struct Vec1Mutex {
  CRITICAL_SECTION cs;
};

struct Vec1Cond {
  CONDITION_VARIABLE cv;
};

/* Windows thread trampoline - WINAPI calling convention */
static DWORD WINAPI vec1ThreadMain(LPVOID pArg){
  Vec1Thread *p = (Vec1Thread*)pArg;
  p->pOut = p->xTask(p->pIn);
  return 0;
}

static int vec1ThreadCreate(
  Vec1Thread **ppThread,
  void *(*xTask)(void*),
  void *pIn
){
  Vec1Thread *p = sqlite3_malloc(sizeof(Vec1Thread));
  if( p==0 ){ *ppThread = 0; return SQLITE_NOMEM; }
  p->xTask = xTask;
  p->pIn = pIn;
  p->pOut = 0;
  p->hThread = CreateThread(NULL, 0, vec1ThreadMain, p, 0, NULL);
  if( p->hThread==NULL ){
    sqlite3_free(p);
    *ppThread = 0;
    return SQLITE_ERROR;
  }
  *ppThread = p;
  return SQLITE_OK;
}

static int vec1ThreadJoin(Vec1Thread *pThread, void **ppOut){
  int rc = SQLITE_OK;
  if( WaitForSingleObject(pThread->hThread, INFINITE)!=WAIT_OBJECT_0 ){
    rc = SQLITE_ERROR;
    if( ppOut ) *ppOut = 0;
  }else{
    if( ppOut ) *ppOut = pThread->pOut;
  }
  CloseHandle(pThread->hThread);
  sqlite3_free(pThread);
  return rc;
}

static int vec1MutexCreate(Vec1Mutex **ppMutex){
  Vec1Mutex *p = sqlite3_malloc(sizeof(Vec1Mutex));
  if( p==0 ){ *ppMutex = 0; return SQLITE_NOMEM; }
  InitializeCriticalSection(&p->cs);
  *ppMutex = p;
  return SQLITE_OK;
}

static void vec1MutexDestroy(Vec1Mutex *pMutex){
  DeleteCriticalSection(&pMutex->cs);
  sqlite3_free(pMutex);
}

static void vec1MutexEnter(Vec1Mutex *pMutex){
  EnterCriticalSection(&pMutex->cs);
}

static void vec1MutexLeave(Vec1Mutex *pMutex){
  LeaveCriticalSection(&pMutex->cs);
}

static int vec1CondCreate(Vec1Cond **ppCond){
  Vec1Cond *p = sqlite3_malloc(sizeof(Vec1Cond));
  if( p==0 ){ *ppCond = 0; return SQLITE_NOMEM; }
  InitializeConditionVariable(&p->cv);
  *ppCond = p;
  return SQLITE_OK;
}

static void vec1CondDestroy(Vec1Cond *pCond){
  /* Windows condition variables have no destroy function */
  sqlite3_free(pCond);
}

static void vec1CondWait(Vec1Mutex *pMutex, Vec1Cond *pCond){
  SleepConditionVariableCS(&pCond->cv, &pMutex->cs, INFINITE);
}

static void vec1CondBroadcast(Vec1Cond *pCond){
  WakeAllConditionVariable(&pCond->cv);
}



#endif /* SQLITE_OS_WIN_THREADS */
/******************************** End Win32 Threads *************************/

/*
*****************************************************************************
*****************************************************************************/

typedef struct Vec1JobQueue Vec1JobQueue;

#if VEC1_THREADS

typedef struct Vec1Job Vec1Job;

struct Vec1Job {
  void *pCtx;
  void (*xWork)(void *pCtx);
  int (*xFinish)(void *pCtx, int);
  Vec1Job *pJobNext;
};

/*
** nJob is the number of jobs on either the pWaiting or pFinished list,
** or else being worked on by a thread. nJob is incremented when the
** job is added to pWaiting, decremented when it is removed from pFinished.
** Always by the main thread.
*/
struct Vec1JobQueue {
  Vec1Mutex *pMutex;
  Vec1Cond *pCond1;
  Vec1Cond *pCond2;
  Vec1Thread **apWorker;
  int nWorker;                    /* Number of worker threads */
  int bDone;                      /* True if threads should exit */
  int nJob;                       /* Total number of unfinished jobs */

  Vec1Job *pWaiting;              /* List of jobs waiting for xWork() */
  Vec1Job *pFinished;             /* List of jobs waiting for xFinish() */
  Vec1Job *pFree;                 /* List of spare Vec1Job structures */
};

static void *vec1JobWorkerMain(void *pCtx){
  Vec1JobQueue *p = (Vec1JobQueue*)pCtx;

  while( 1 ){
    Vec1Job *pJob = 0;

    /* Try to find a new job to work on. */
    vec1MutexEnter(p->pMutex);
    while( p->pWaiting==0 && p->bDone==0 ){
      vec1CondWait(p->pMutex, p->pCond1);
    }
    pJob = p->pWaiting;
    if( pJob ) p->pWaiting = pJob->pJobNext;
    vec1MutexLeave(p->pMutex);

    /* If pJob is 0, we're done. Thread should exit. */
    if( pJob==0 ) break;

    /* Do the work for this job. */
    pJob->xWork(pJob->pCtx);

    /* Put the job on the finished list. If it is currently empty, the
    ** main thread may be waiting. Signal it in that case.  */
    vec1MutexEnter(p->pMutex);
    if( p->pFinished==0 ) vec1CondBroadcast(p->pCond2);
    pJob->pJobNext = p->pFinished;
    p->pFinished = pJob;
    vec1MutexLeave(p->pMutex);
  }

  return 0;
}

static int vec1JobQueueFinishJobs(Vec1JobQueue *pQueue, int rcin){
  int rc = rcin;

  if( pQueue==0 ) return rc;

  while( 1 ){
    Vec1Job *pFin = 0;
    Vec1Job *pWork = 0;

    /* Try to find a job to help with. If there is nothing, wait on pCond2. */
    vec1MutexEnter(pQueue->pMutex);
    while( pQueue->pFinished==0 && pQueue->pWaiting==0 && pQueue->nJob>0 ){
      vec1CondWait(pQueue->pMutex, pQueue->pCond2);
    }
    if( pQueue->pFinished ){
      pFin = pQueue->pFinished;
      pQueue->pFinished = pFin->pJobNext;
      pQueue->nJob--;
    }else if( pQueue->pWaiting ){
      pWork = pQueue->pWaiting;
      pQueue->pWaiting = pWork->pJobNext;
      pQueue->nJob--;
    }
    vec1MutexLeave(pQueue->pMutex);

    if( pFin==0 && pWork==0 ) break;
    if( pWork ){
      pWork->xWork(pWork->pCtx);
      pFin = pWork;
    }

    {
      int rc2 = pFin->xFinish(pFin->pCtx, rc);
      if( rc==SQLITE_OK ) rc = rc2;
    }

    vec1MutexEnter(pQueue->pMutex);
    pFin->pJobNext = pQueue->pFree;
    pQueue->pFree = pFin;
    vec1MutexLeave(pQueue->pMutex);
  }

  return rc;
}


static void vec1JobQueueFree(Vec1JobQueue *pQueue){
  if( pQueue ){
    if( pQueue->pMutex && pQueue->pCond1 ){
      int ii;
      vec1JobQueueFinishJobs(pQueue, SQLITE_ERROR);

      vec1MutexEnter(pQueue->pMutex);
      pQueue->bDone = 1;
      vec1CondBroadcast(pQueue->pCond1);
      vec1MutexLeave(pQueue->pMutex);
      for(ii=0; ii<pQueue->nWorker; ii++){
        void *dummy = 0;
        vec1ThreadJoin(pQueue->apWorker[ii], &dummy);
      }
    }

    if( pQueue->pMutex ) vec1MutexDestroy(pQueue->pMutex);
    if( pQueue->pCond1 ) vec1CondDestroy(pQueue->pCond1);
    if( pQueue->pCond2 ) vec1CondDestroy(pQueue->pCond2);

    assert( pQueue->pWaiting==0 );
    assert( pQueue->pFinished==0 );
    assert( pQueue->pFree!=0 );
    sqlite3_free(pQueue);
  }
}


/*
** Allocate a new job-queue.
*/
static Vec1JobQueue *vec1JobQueueNew(int nWorker){
  int rc = SQLITE_OK;
  Vec1JobQueue *pNew = 0;
  int nJob = nWorker + 8;

  int nByte = 
    sizeof(Vec1JobQueue) + 
    sizeof(Vec1Thread*)*nWorker + 
    sizeof(Vec1Job) * nJob;

  assert( nWorker>=0 );
  pNew = (Vec1JobQueue*)vec1MallocZero(nByte);
  if( pNew==0 ){
    rc = SQLITE_NOMEM;
  }else{
    int ii;
    Vec1Job *pJob;

    pNew->apWorker = (Vec1Thread**)&pNew[1];
    pJob = (Vec1Job*)&pNew->apWorker[nWorker];
    for(ii=0; ii<nJob; ii++){
      pJob->pJobNext = pNew->pFree;
      pNew->pFree = pJob;
      pJob++;
    }

    rc = vec1MutexCreate(&pNew->pMutex);
    if( rc==SQLITE_OK ) rc = vec1CondCreate(&pNew->pCond1);
    if( rc==SQLITE_OK ) rc = vec1CondCreate(&pNew->pCond2);
    for(ii=0; rc==SQLITE_OK && ii<nWorker; ii++){
      rc = vec1ThreadCreate(&pNew->apWorker[ii], vec1JobWorkerMain, pNew);
      if( rc==SQLITE_OK ){
        pNew->nWorker++;
      }
    }
  }

  if( rc!=SQLITE_OK ){
    vec1JobQueueFree(pNew);
    pNew = 0;
  }

  return pNew;
}

static int vec1DummyFinal(void *pCtx, int rcin){
  UNUSED_PARAMETER(pCtx);
  return rcin;
}

static int vec1JobQueueAddJob(
  Vec1JobQueue *pQueue,
  void (*xWork)(void *pCtx),
  int (*xFinish)(void *pCtx, int),
  void *pCtx
){
  Vec1Job *pFree = 0;
  Vec1Job *pFinish = 0;
  Vec1Job *pWork = 0;
  int rc = SQLITE_OK;

  if( xFinish==0 ) xFinish = vec1DummyFinal;
  if( pQueue==0 ){
    xWork(pCtx);
    return xFinish(pCtx, SQLITE_OK);
  }

  /* Check if this thread needs to do any xFinish() or xWork() calls. And 
  ** try to get a free Vec1Job object at the same time.  */
  vec1MutexEnter(pQueue->pMutex);

  assert( pQueue->pWaiting || pQueue->pFree || pQueue->pFinished );
  if( pQueue->pFree ){
    pFree = pQueue->pFree;
    pQueue->pFree = pFree->pJobNext;
  }else if( pQueue->pFinished ){
    pFinish = pQueue->pFinished;
    pQueue->pFinished = pFinish->pJobNext;
    pQueue->nJob--;
  }else{
    pWork = pQueue->pWaiting;
    pQueue->pWaiting = pWork->pJobNext;
    pQueue->nJob--;
  }

  vec1MutexLeave(pQueue->pMutex);

  /* At most 1 of pFree, pFinish and pWork may be non NULL */
  assert( (pFree!=0)+(pFinish!=0)+(pWork!=0)<=1 );

  if( pWork ){
    pWork->xWork(pWork->pCtx);
    pFinish = pWork;
  }
  if( pFinish ){
    rc = pFinish->xFinish(pFinish->pCtx, SQLITE_OK);
    pFree = pFinish;
  }
  assert( pFree );

  pFree->pCtx = pCtx;
  pFree->xWork = xWork;
  pFree->xFinish = xFinish;
  vec1MutexEnter(pQueue->pMutex);
  if( pQueue->pWaiting==0 ) vec1CondBroadcast(pQueue->pCond1);
  pFree->pJobNext = pQueue->pWaiting;
  pQueue->pWaiting = pFree;
  pQueue->nJob++;
  vec1MutexLeave(pQueue->pMutex);

  return rc;
}

#else  /* VEC1_THREADS=0 */

/*
** Dummy implementation of job-queue for single-threaded builds.
*/
static int vec1JobQueueFinishJobs(Vec1JobQueue *pQueue, int rcin){
  return rcin;
}
static void vec1JobQueueFree(Vec1JobQueue *pQueue){}
static Vec1JobQueue *vec1JobQueueNew(int a){ return 0; }
static int vec1JobQueueAddJob(
  Vec1JobQueue *pQueue,
  void (*xWork)(void *pCtx),
  int (*xFinish)(void *pCtx, int),
  void *pCtx
){
  xWork(pCtx);
  return xFinish ? xFinish(pCtx, SQLITE_OK) : SQLITE_OK;
}

#endif

/*
** Minimum and maximum values allowed for the 'blocksize' command.
*/
#define VEC1_BLOCKSIZE_MIN 1024
#define VEC1_BLOCKSIZE_MAX (16*1024*1024)

#define VEC1_BLOCKSIZE_DEFAULT         (1024*1024)
#define VEC1_BLOCKSIZE_MIN_DEFAULT     (16*1024)

#define VEC1_VECSIZE_MIN      2
#define VEC1_VECSIZE_MAX   4096

/* 
** Number of codes in each codebook. This is always 256. 
*/
#define VEC1_PQ_CODEBOOK_SZ 256

#define VEC1_MIN_CODESIZE              1
#define VEC1_MAX_CODESIZE            256

#define VEC1_MIN_BUCKET                0
#define VEC1_MAX_BUCKET            65536

#define VEC1_MIN_NOPQ_ROUND            1
#define VEC1_MAX_NOPQ_ROUND           32

#define VEC1_PQ_KMEANS_NITER          30
#define VEC1_PQ_KMEANS_STOPVAL         0.999

/* 
** Minimum number of vectors required for PQ training.
*/
#define VEC1_PQ_TRAINING_SET_MIN (VEC1_PQ_CODEBOOK_SZ * 2)

#define VEC1_TRAINING_TOTAL           0
#define VEC1_TRAINING_ROT_CODEBOOKS   1
#define VEC1_TRAINING_ROT_COVARIANCE  2
#define VEC1_TRAINING_ROT_JACOBI      3
#define VEC1_TRAINING_ROT_VECTORS     4

#define VEC1_TRAINING_COARSE_INIT     5
#define VEC1_TRAINING_COARSE_KMEANS   6

#define VEC1_TRAINING_RESIDUALS       7
#define VEC1_TRAINING_CODEBOOKS       8

#define VEC1_TRAINING_NTIMER          9


/*
** When accumulating training data, allocate space in chunks of 
** VEC1_TRAINING_SZCHUNK. Allow at most VEC1_TRAINING_MAXCHUNK chunks of
** training data. With default values, the limit is 64GiB of training
** vectors.
*/
#define VEC1_TRAINING_MAXCHUNK 1024
#define VEC1_TRAINING_SZCHUNK  (32*1024*1024)

/*
** Values for Vec1TrainCtx.eQuant
*/
#define VEC1_QUANTIZE_NONE 0
#define VEC1_QUANTIZE_PQ   1
#define VEC1_QUANTIZE_OPQ  2
#define VEC1_QUANTIZE_BQ   3

/* 
** Training vectors collected by xStep() are stored in an instance 
** of this structure.
*/
typedef struct Vec1TrainVectors Vec1TrainVectors;
struct Vec1TrainVectors {
  int nElem;                      /* Number of elements in each vector */
  int nVec;                       /* Number of vectors in chunk array */
  int nChunk;                     /* Number of chunks allocated so far */
  int nVecPerChunk;               /* Number of vectors in each chunk */
  float *aChunk[VEC1_TRAINING_MAXCHUNK];
};

/*
** Context object for the vec1_train() aggregate function.
**
** eQuant:
**   A VEC1_QUANTIZE_XYZ value. Set by the "quantizer" option.
**
** nCodesize:
**   For PQ or OPQ, the number of vector sub-spaces. For BQ, the number of
**   bytes required to store the bit-vector. For 1 bit per dimension this
**   value is set to ((d+7)/8).
**
** nBucket:
**   Number of IVF buckets used by coarse quantizer. Set to 0 to disable
**   coarse quant altogether.
*/
typedef struct Vec1TrainCtx Vec1TrainCtx;
struct Vec1TrainCtx {
  /* Training vectors collected by xStep() */
  Vec1TrainVectors tv;

  /* Training parameters configured by json argument */
  int eDistance;                  /* VEC1_DISTANCE_L2 or COS */
  int eQuant;                     /* VEC1_QUANTIZE_XX value */
  int nCodesize;                  /* Number of vector sub-spaces */
  int nBucket;                    /* Number of IVF buckets */

  int bOpq;

  int bResidual;
  int bSvdVerify;
  int bWHT;                       /* Use WHT transformation */
  int nThread;                    /* Number of CPU threads to use */
  int nOpqRound;                  /* Number of OPQ rounds */
  char *zLogFunction;             /* Name of user SQL function for logging */
  char *zProfileFunction;         /* Name of user SQL function for profile */
  sqlite3_stmt *pLogStmt;         /* SELECT zLogFunction(?, ?) */

  sqlite3_context *pCtx;          /* Context object for errors */
  int rc;                         /* Error code for training operation */

  /* Used for calculating "percent" value passed to "SELECT zLogFunction()" */
  int nTotalWork;
  int nCompletedWork;

  /* Calculated based on training parameters + size of first vector */
  int nCodeElem;                  /* Number of elements in codebook vectors */

  u64 aTime[VEC1_TRAINING_NTIMER];
};

#define VEC1_TRAINING_WORK_OPQ       2
#define VEC1_TRAINING_WORK_COARSE1   1
#define VEC1_TRAINING_WORK_COARSE2   1
#define VEC1_TRAINING_WORK_CODEBOOKS 1

/*
** Return a pointer to the requested training vector. The results are
** undefined if parameter iVec is out of range.
*/
static float *vec1TrainingVector(Vec1TrainVectors *p, int iVec){
  int iChunk = iVec / p->nVecPerChunk;
  int iOff = (iVec % p->nVecPerChunk) * p->nElem;
  return &((p->aChunk[iChunk])[iOff]);
}

/*
** aSub[] is a vector of nCodeElem elements. aCodebook[] is a codebook of
** nCode vectors, each of nCodeElem elements. Return the index of the best
** match for aSub in aCodebook[].
*/
static int vec1PqBestMatch(
  const float *aCodebook,         /* Codebook of nCodeElem element vectors */
  int nCode,                      /* Number of vectors in aCodebook */
  const float *aSub,              /* nCodeElem element vector */
  int nCodeElem,                  /* Size of vectors in elements */
  double *pfBest                  /* Distance to best match */
){
  double fBestDist = INFINITY;
  int iBest = 0;
  int ii;

  for(ii=0; ii<nCode; ii++){
    double fDist = vec1L2Dist(aSub, &aCodebook[ii*nCodeElem], nCodeElem);
    if( fDist<fBestDist ){
      iBest = ii;
      fBestDist = fDist;
    }
  }
  assert( iBest>=0 && iBest<nCode );

  if( pfBest ) *pfBest += fBestDist;
  return iBest;
}

/*
** Return a pseudo-random positive 32-bit value.
*/
static int vec1Rand31(){
  int val;
  sqlite3_randomness(sizeof(val), (void*)&val);
  return (val & 0x7FFFFFFF);
}

/*
** Access nElem element vector iVec from packed array aPacked.
*/
#define PACKEDVEC(aPacked, nElem, iVec) (&(aPacked)[(iVec)*(nElem)])

/*
** Access training vector i from Vec1TrainCtx p.
*/
#define TRAININGVEC(p, i) PACKEDVEC(p->aVec, p->nElem, i)

typedef struct LloydsJob LloydsJob;
struct LloydsJob {
  /* Data to train on */
  Vec1TrainVectors *pVec;
  int iFirst;                     /* First vector for this job */
  int iEof;                       /* Index of last vector +1 for this job */

  int nK;                         /* Number of centroids */
  const float *aCentroid;         /* Centroids */
#ifdef VEC1_USE_TRANSPOSITION
  float *aTrans;                  /* Transposed centroids */
#endif

  /* Outputs */
  double fTotal;
  float *aSum;
  int *aCount;
};

/*
** Context object for simple PRNG. See functions vec1RandomInit() and
** vec1RandomNext() for details.
*/
typedef struct Vec1Random Vec1Random;
struct Vec1Random {
  u64 state;
  u64 inc;
};

static u32 vec1RandomNext(Vec1Random *p) {
  u32 xorshifted, rot;
  u64 old = p->state;
  p->state = old * 6364136223846793005ULL + p->inc;
  xorshifted = (u32)(((old >> 18u) ^ old) >> 27u);
  rot = (u32)(old >> 59u);
  return ((xorshifted >> rot) | (xorshifted << ((32 - rot) & 31))) & 0x7FFFFFFF;
}

static void vec1RandomInit(Vec1Random *p, u64 seed, u64 id){
  p->state = 0;
  p->inc = (id << 1u) | 1u;
  vec1RandomNext(p);
  p->state += seed;
  vec1RandomNext(p);
}


typedef struct KMeansInitJob KMeansInitJob;
struct KMeansInitJob {
  int iFirst;                     /* First vector for this job */
  int iEof;                       /* Index of last vector +1 for this job */
  double fTotalMin;               /* Total of aMin[iFirst..iEof-1] */

  Vec1TrainVectors *pVec;
  u32 *aWeight;

  float *aMin;
  int *aBest;

  float *aCentroid;
  int nCentroid;
  int iFirstNew;
};

static void vec1KMeansInitWork(void *pCtx){
  KMeansInitJob *p = (KMeansInitJob*)pCtx;
  const int nElem = p->pVec->nElem;
  double fTotalMin = 0.0;
  int ii;

  for(ii=p->iFirst; ii<p->iEof; ii++){
    int jj;
    float *vec = vec1TrainingVector(p->pVec, ii);
    for(jj=p->iFirstNew; jj<p->nCentroid; jj++){
      float *cent = &p->aCentroid[jj * nElem];
      float dist = (float)vec1L2Dist(vec, cent, nElem);
      if( jj==0 || dist<p->aMin[ii] ){
        p->aMin[ii] = dist;
        if( p->aBest ) p->aBest[ii] = jj;
      }
    }
    fTotalMin += ((double)p->aMin[ii] * (p->aWeight ? p->aWeight[ii] : 1));
  }

  p->fTotalMin = fTotalMin;
}

/*
** Macro used to assign vectors to jobs in cases where an algorithm
** parallelizes across vectors. The pJob object must have at least the
** following member:
**
**   struct XyzJob {
**     ...
**     Vec1TrainVectors *pVec;    // Training vectors to use a subset of
**     int iFirst;                // index of first vector in pVec to use
**     int iEof;                  // 1 past index of last vector in pVec to use
**     ...
**   };
*/
#define ASSIGN_VECTORS_TO_JOB(pV, pJob, iJob, nJob)                         \
  pJob->pVec = (pV);                                                        \
  pJob->iFirst = (iJob==0) ? 0 : pJob[-1].iEof;                             \
  pJob->iEof = pJob->iFirst + ((pV)->nVec - pJob->iFirst) / (nJob - iJob);  \
  assert( pJob->iEof<=(pV)->nVec );                                         \
  assert( (iJob==nJob-1)==(pJob->iEof==(pV)->nVec) );

/*
** 
*/
static int vec1KMeansInitOne(
  Vec1TrainVectors *pVec,
  u32 *aWeight,
  Vec1JobQueue *pQueue,           /* Job queue to use (NULL -> 1 thread) */
  int nK,                         /* Required number of outputs */
  int nPerRound,                  /* Number of centroids per round */
  int nTail,                      /* Number of tail rounds */
  float *aOut,                    /* Array to populate with centroids */
  u32 *aHist                      /* Array to populate with histogram */
){
  const int nJob = (pQueue ? pQueue->nWorker + 1 : 1);
  const int nElem = pVec->nElem;
  const int nVByte = nElem * sizeof_f32;

  Vec1Random rnd;

  int nByte;                      /* Bytes of space to allocate */
  KMeansInitJob *aJob = 0;
  float *aMin = 0;
  int *aBest = 0;
  int ii;                         /* Loop counter for per-job loop */
  int rr;                         /* Loop counter for per-round loop */
  int iNext;
  int nCentroid = 0;
  int nNewCentroid = 0;
  float *vec = 0;

  vec1RandomInit(&rnd, vec1Rand31(), 0);

  nByte = 
    (sizeof(KMeansInitJob) * nJob) +
    (pVec->nVec * sizeof_f32) +
    (aHist ? pVec->nVec * sizeof_u32 : 0);
  aJob = vec1MallocZero(nByte);
  if( aJob==0 ){
    return SQLITE_NOMEM;
  }
  aMin = (float*)&aJob[nJob];
  if( aHist ){
    aBest = (int*)&aMin[pVec->nVec];
  }

  for(ii=0, iNext=0; ii<nJob; ii++){
    KMeansInitJob *pJob = &aJob[ii];
    ASSIGN_VECTORS_TO_JOB(pVec, pJob, ii, nJob);
    pJob->aMin = aMin;
    pJob->aBest = aBest;
    pJob->aWeight = aWeight;
    pJob->aCentroid = aOut;
  }

  /* First centroid is allocated by random selection. */
  vec = vec1TrainingVector(pVec, vec1RandomNext(&rnd)%pVec->nVec);
  memcpy(aOut, vec, nVByte);
  nCentroid = nNewCentroid = 1;

  for(rr=1; rr<nK; rr+=nNewCentroid){
    int vv;
    double fTotalMin = 0.0;

    /* Step 2: */
    for(ii=0; ii<nJob; ii++){
      aJob[ii].nCentroid = nCentroid;
      aJob[ii].iFirstNew = nCentroid - nNewCentroid;
      vec1JobQueueAddJob(pQueue, vec1KMeansInitWork, 0, (void*)&aJob[ii]);
    }
    vec1JobQueueFinishJobs(pQueue, SQLITE_OK);

    for(ii=0; ii<nJob; ii++){
      fTotalMin += aJob[ii].fTotalMin;
    }

    /* Determine number of centroids to add: */
    if( (nK-nCentroid)<=nTail ){
      nNewCentroid = 1;
    }else{
      nNewCentroid = MIN(nPerRound, nK-nTail-nCentroid);
    }
    assert( nCentroid+nNewCentroid<=nK );

    /* Choose new centroids */
    if( nNewCentroid==1 ){
      /* KMeans ++ case */
      float fTarget = (float)((fTotalMin * vec1RandomNext(&rnd)) / 0x7FFFFFFF);
      for(vv=0; vv<pVec->nVec-1; vv++){
        fTarget -= (aMin[vv] * (aWeight ? aWeight[vv] : 1));
        if( fTarget<=0.0 ) break;
      }
      memcpy(&aOut[nCentroid*nElem], vec1TrainingVector(pVec, vv), nVByte);
      nCentroid++;
    }else{
      /* KMeans || case */
      int cc;
      vv = vec1RandomNext(&rnd) % pVec->nVec;
      for(cc=0; cc<nNewCentroid; cc++){
        while( 1 ){
          u32 iRnd = vec1RandomNext(&rnd);
          u32 w = (aWeight ? aWeight[vv] : 1);
          double fRnd = ((double)iRnd * fTotalMin) / (double)0x7FFFFFFF;
          if( fRnd<=((double)aMin[vv] * nNewCentroid * w) ) break;
          vv = (vv + 1) % pVec->nVec;
        }

        memcpy(&aOut[nCentroid*nElem], vec1TrainingVector(pVec, vv), nVByte);
        nCentroid++;
      }
    }
  }

  if( aHist ){
    int vv;
    for(vv=0; vv<pVec->nVec; vv++){
      aHist[aBest[vv]]++;
    }
  }

  sqlite3_free(aJob);
  return SQLITE_OK;
}

/*
** Implementation of KMeans || initialization.
*/
static int vec1KMeansParallel(
  Vec1TrainVectors *pVec,
  Vec1JobQueue *pQueue,           /* Job queue to use (NULL -> 1 thread) */
  int nK,                         /* Required number of outputs */
  float *aOut                     /* Array to populate with centroids */
){
  int rc = SQLITE_OK;
  const int nCent = nK * 2;
  const int nByte = nCent * (pVec->nElem * sizeof_f32 + sizeof_u32);
  float *aCent;

  aCent = (float*)vec1MallocZero(nByte);
  if( aCent==0 ){
    rc = SQLITE_NOMEM;
  }else{
    u32 *aWeight = (u32*)&aCent[pVec->nElem * nCent];
    rc = vec1KMeansInitOne(pVec, 0, pQueue, nCent, nCent/10, 0, aCent,aWeight);
    if( rc==SQLITE_OK ){
      Vec1TrainVectors vv;
      memset(&vv, 0, sizeof(vv));
      vv.nElem = pVec->nElem;
      vv.nVec = nCent;
      vv.nVecPerChunk = nCent;
      vv.nChunk = 1;
      vv.aChunk[0] = aCent;
      rc = vec1KMeansInitOne(&vv, aWeight, pQueue, nK, nK, 0, aOut, 0);
    }
    sqlite3_free(aCent);
  }

  return rc;
}

static int vec1KMeansPlusPlus(
  Vec1TrainVectors *pVec,
  Vec1JobQueue *pQueue,           /* Job queue to use (NULL -> 1 thread) */
  int nK,                         /* Required number of outputs */
  float *aOut                     /* Array to populate with centroids */
){
  const int nPerRound = 8;
  const int nTailRound = 32;
  return vec1KMeansInitOne(pVec, 0, pQueue, nK, nPerRound, nTailRound, aOut, 0);
}

#if defined(_MSC_VER)
# define VEC1_NOINLINE __declspec(noinline)
#else
# define VEC1_NOINLINE __attribute__((noinline))
#endif

static VEC1_NOINLINE void vec1AddInPlaceScalar(
  float * a1, 
  const float * a2, 
  int n
){
  int i;
  for(i=0; i<n; i++){
    a1[i] += a2[i];
  }
}

/*
** a1[] and a2[] are both vectors n elements in size. Add a2 to a1:
**
**          a1 = a1 + a2
*/
static void vec1AddInPlace(float * a1, const float * a2, int n){
  int i = 0;
#ifdef VEC1_HAVE_AVX2
  for(; i<=n-8; i+=8){
    __m256 v1 = _mm256_loadu_ps(a1 + i);
    __m256 v2 = _mm256_loadu_ps(a2 + i);
    _mm256_storeu_ps(a1 + i, _mm256_add_ps(v1, v2));
  }
#endif
#ifdef VEC1_HAVE_NEON
  for(; i<=n-4; i+=4){
    float32x4_t v1 = vld1q_f32(&a1[i]);
    float32x4_t v2 = vld1q_f32(&a2[i]);
    vst1q_f32(&a1[i], vaddq_f32(v1, v2));
  }
#endif

  if( i!=n ) vec1AddInPlaceScalar(&a1[i], &a2[i], n-i);
}

#ifdef VEC1_HAVE_NEON
static inline void vec1MultiBestMatch(
  int nElem,                      /* Size of vectors in elements */
  int iCentroid,                  /* Centroid offset */
  const float *aVec[VEC1_MULTIMATCH_NVEC],
  const float *aCent,             /* VEC1_MULTIMATCH_NCENT packed centroids */
  float *aDistIn,
  int *aBestIn
){
  float32x4_t aAcc[VEC1_MULTIMATCH_NVEC][VEC1_MULTIMATCH_NCENT];

  int vv;                         /* 0 to VEC1_MULTIMATCH_NVEC */
  int cc;                         /* 0 to VEC1_MULTIMATCH_NCENT */
  int d;                          /* 0 to nElem */

  float aDist[VEC1_MULTIMATCH_NVEC];
  int aBest[VEC1_MULTIMATCH_NVEC];

  for(vv=0; vv<VEC1_MULTIMATCH_NVEC; vv++){
    for(cc=0; cc<VEC1_MULTIMATCH_NCENT; cc++){
      aAcc[vv][cc] = vdupq_n_f32(0.0f);
    }
  }

  for(d=0; d<=(nElem-4); d+=4){
    float32x4_t aV[VEC1_MULTIMATCH_NVEC];
    for(vv=0; vv<VEC1_MULTIMATCH_NVEC; vv++){
      aV[vv] = vld1q_f32(&aVec[vv][d]);
    }
    for(cc=0; cc<VEC1_MULTIMATCH_NCENT; cc++){
      float32x4_t cent = vld1q_f32(&aCent[cc*nElem+d]);
      for(vv=0; vv<VEC1_MULTIMATCH_NVEC; vv++){
        float32x4_t diff = vsubq_f32(aV[vv], cent);
        aAcc[vv][cc] = vfmaq_f32(aAcc[vv][cc], diff, diff);
      }
    }
  }

  /* Scalar tail for cases where the number of dimensions is not 
  ** divisible by 4. */
  for(; d<nElem; d++){
    for(cc=0; cc<VEC1_MULTIMATCH_NCENT; cc++){
      float cent = aCent[cc*nElem+d];
      for(vv=0; vv<VEC1_MULTIMATCH_NVEC; vv++){
        float diff = cent - aVec[vv][d];
        aAcc[vv][cc] = vsetq_lane_f32(
            vgetq_lane_f32(aAcc[vv][cc], 0) + (diff*diff), aAcc[vv][cc], 0
        );
      }
    }
  }

#if 0
  {
    float32x4_t vBestDist = vld1q_f32(aDist);
    int32x4_t vBestIdx = vld1q_s32(aBest);

    for(cc=0; cc<VEC1_MULTIMATCH_NCENT; cc++){
      float32x4_t cand = vdupq_n_f32(0.0f);
      uint32x4_t mask;

      cand = vsetq_lane_f32(vaddvq_f32(aAcc[0][cc]), cand, 0);
      cand = vsetq_lane_f32(vaddvq_f32(aAcc[1][cc]), cand, 1);
      cand = vsetq_lane_f32(vaddvq_f32(aAcc[2][cc]), cand, 2);
      cand = vsetq_lane_f32(vaddvq_f32(aAcc[3][cc]), cand, 3);

      mask = vcltq_f32(cand, vBestDist);
      vBestDist = vbslq_f32(mask, cand, vBestDist);
      vBestIdx = vbslq_s32(mask, vdupq_n_s32(iCentroid+cc), vBestIdx);
    }

    vst1q_f32(aDist, vBestDist);
    vst1q_s32(aBest, vBestIdx);
  }
#endif

  memcpy(aDist, aDistIn, sizeof_f32*VEC1_MULTIMATCH_NVEC);
  memcpy(aBest, aBestIn, sizeof_u32*VEC1_MULTIMATCH_NVEC);
  for(cc=0; cc<VEC1_MULTIMATCH_NCENT; cc++){
    for(vv=0; vv<VEC1_MULTIMATCH_NVEC; vv++){
      float dist = vaddvq_f32(aAcc[vv][cc]);
      if( dist<aDist[vv] ){
        aDist[vv] = dist;
        aBest[vv] = iCentroid + cc;
      }
    }
  }
  memcpy(aDistIn, aDist, sizeof_f32*VEC1_MULTIMATCH_NVEC);
  memcpy(aBestIn, aBest, sizeof_u32*VEC1_MULTIMATCH_NVEC);
}
#endif


/*
** Find the best match for each of VEC1_MULTIMATCH_NVEC vectors from the
** nCent centroids encoded in array aCentNT. The difference between this 
** function and vec1BestMatchN() is the format of aCentNT. For this
** function, centroids are arranged into blocks of VEC1_NTRANSPOSE_WIDTH
** centroids per block and transposed. e.g. if VEC1_NTRANSPOSE_WIDTH==8, 
** aCent begins with:
** 
**   *  d0 value for each of the first 8 vectors, followed by
**   *  d1 value for each of the first 8 vectors, followed by
**   ...
**   *  d(nElem-1) value for each of the first 8 vectors, then
**   *  d0 value for each of the second 8 vectors, etc.
**   ...
*/
#ifdef VEC1_USE_TRANSPOSITION

static void vec1BestMatchTransposeN(
  int nElem,
  const float *aCent,
  const float *aCentNT, 
  int nCent, 
  const float *aVec[VEC1_MULTIMATCH_NVEC],
  int aBestOut[VEC1_MULTIMATCH_NVEC],
  double *pfTotalOut
){
  const int nWidth = VEC1_NTRANSPOSE_WIDTH;
  float aDist[VEC1_MULTIMATCH_NVEC];
  int aBest[VEC1_MULTIMATCH_NVEC];
  int vv;                         /* For looping 0..VEC1_MULTIMATCH_NVEC-1 */
  int iCent;
  const float *pTrans = aCentNT;

  for(vv=0; vv<VEC1_MULTIMATCH_NVEC; vv++){
    aDist[vv] = INFINITY;
  }

  /* All the AVX2 code below assumes VEC1_NTRANSPOSE_WIDTH==8 */
  assert( nWidth==8 );

  for(iCent=0; iCent<=(nCent-nWidth); iCent+=nWidth){
    __m256 aAcc[VEC1_MULTIMATCH_NVEC];
    int d;

    for(vv=0; vv<VEC1_MULTIMATCH_NVEC; vv++){
      aAcc[vv] = _mm256_setzero_ps();
    }

    for(d=0; d<nElem; d++){
      __m256 cval = LOADU( pTrans );
      for(vv=0; vv<VEC1_MULTIMATCH_NVEC; vv++){
        __m256 sval = _mm256_set1_ps( aVec[vv][d] );
        __m256 diff = _mm256_sub_ps( cval, sval );
        aAcc[vv] = FMADD(diff, diff, aAcc[vv]);
      }
      pTrans += nWidth;
    }

    /* At this point, each register in aAcc[] contains the distances
    ** from the corresponding training vector (vector ii + vv) to 
    ** centroids iCent to (iCent + VEC1_MULTIMATCH_NCENT). Populate the 
    ** aDist[] and aBest[] scalar arrays accordingly. */
    for(vv=0; vv<VEC1_MULTIMATCH_NVEC; vv++){
      __m256 vMin = _mm256_set1_ps(aDist[vv]);
      __m256 mask = _mm256_cmp_ps(aAcc[vv], vMin, _CMP_LT_OQ);
      if( _mm256_movemask_ps(mask)!=0 ){
        /* This branch runs if aAcc[vv] contains one of more values less 
        ** than scalar value aDist[vv]. */
        int is;
        float aScalar[VEC1_NTRANSPOSE_WIDTH];
        _mm256_storeu_ps(aScalar, aAcc[vv]);
        for(is=0; is<VEC1_NTRANSPOSE_WIDTH; is++){
          if( aScalar[is]<aDist[vv] ){
            aDist[vv] = aScalar[is];
            aBest[vv] = is + iCent;
          }
        }
      }
    }
  }

  for( ; iCent<nCent; iCent++){
    const float *cent = &aCent[iCent*nElem];
    for(vv=0; vv<VEC1_MULTIMATCH_NVEC; vv++){
      float dist = (float)vec1L2Dist(aVec[vv], cent, nElem);
      if( dist<aDist[vv] ){
        aDist[vv] = dist;
        aBest[vv] = iCent;
      }
    }
  }

  memcpy(aBestOut, aBest, sizeof(aBest));
  if( pfTotalOut ){
    double total = 0.0;
    for(vv=0; vv<VEC1_MULTIMATCH_NVEC; vv++){
      total += aDist[vv];
    }
    *pfTotalOut += total;
  }
}

#define vec1BestMatchPlatformN(nElem, aCent, aTrans, nCent, aVec, aBest, p) \
  vec1BestMatchTransposeN(nElem, aCent, aTrans, nCent, aVec, aBest, p)
  
#else

/*
** Array aCent[] contains nCent centroid vectors, each nElem elements in
** length. Array aVec[] contains VEC1_MULTIMATCH_NVEC vectors, also each
** nElem elements in length. This function finds the best match among
** the centroids for each of the VEC1_MULTIMATCH_NVEC vectors, and populates
** output array aBest with the index of each. Additionally, if it is not
** NULL, then (*pfTotalDist) is incremented by the total distance between
** each vector and it's nearest centroid.
*/
static void vec1BestMatchN(
  int nElem,                      /* Number of elements in vectors */
  const float *aCent,             /* nCent packed centroids */
  int nCent,                      /* Number of centroids */
  const float *aVec[VEC1_MULTIMATCH_NVEC],
  int aBest[VEC1_MULTIMATCH_NVEC],
  double *pfTotalDist             /* IN/OUT: Increment by total distortion */
){
  int cc;
  float aDist[VEC1_MULTIMATCH_NVEC];

  for(cc=0; cc<VEC1_MULTIMATCH_NVEC; cc++){
    aDist[cc] = INFINITY;
  }

  for(cc=0; cc<=(nCent-VEC1_MULTIMATCH_NCENT); cc+=VEC1_MULTIMATCH_NCENT){
#if defined(VEC1_HAVE_NEON)
    vec1MultiBestMatch(nElem, cc, aVec, &aCent[cc*nElem], aDist, aBest);
#else
    int ii;
    int vv;
    for(ii=0; ii<VEC1_MULTIMATCH_NCENT; ii++){
      const float *cent = &aCent[(ii+cc)*nElem];
      for(vv=0; vv<VEC1_MULTIMATCH_NCENT; vv++){
        float dist = (float)vec1L2Dist(cent, aVec[vv], nElem);
        if( dist<aDist[vv] ){
          aDist[vv] = dist;
          aBest[vv] = ii+cc;
        }
      }
    }
#endif
  }

  /* Scalar tail in case the number of centroids is not an integer multiple
  ** of VEC1_MULTIMATCH_NCENT.  */
  for(; cc<nCent; cc++){
    int vv;
    const float *cent = &aCent[cc*nElem];
    for(vv=0; vv<VEC1_MULTIMATCH_NVEC; vv++){
      float dist = (float)vec1L2Dist(aVec[vv], cent, nElem);
      if( dist<aDist[vv] ){
        aDist[vv] = dist;
        aBest[vv] = cc;
      }
    }
  }

  if( pfTotalDist ){
    double fDist = *pfTotalDist;
    for(cc=0; cc<VEC1_MULTIMATCH_NVEC; cc++){
      fDist += aDist[cc];
    }
    *pfTotalDist = fDist;
  }
}

/*
** In this case - if VEC1_USE_TRANSPOSITION is not defined, wire all
** vec1BestMatchTransposeN() calls directly to vec1BestMatchN().
*/
#define vec1BestMatchPlatformN(nElem, aCent, aTrans, nCent, aVec, aBest, p) \
    vec1BestMatchN(nElem, aCent, nCent, aVec, aBest, p)

#endif

static void vec1LloydsWork(void *pCtx){
  LloydsJob *p = (LloydsJob*)pCtx;
  Vec1TrainVectors *pVec = p->pVec;
  int nElem = pVec->nElem;
  int ii;

  p->fTotal = 0.0f;
  memset(p->aSum, 0, sizeof_f32 * p->nK * nElem);
  memset(p->aCount, 0, sizeof(int) * p->nK);

  /* Loop through those vectors that are our responsibility, 
  ** VEC1_MULTIMATCH_NVEC vectors at a time. Each iteration of this outer 
  ** loop will compare VEC1_MULTIMATCH_NVEC training vectors to all candidate 
  ** centroids, and update p->aCount[] and p->aSum[] with the results.  */
  ii = p->iFirst;
  for( ; ii<=(p->iEof-VEC1_MULTIMATCH_NVEC); ii+=VEC1_MULTIMATCH_NVEC){
    int vv;

    const float *aVec[VEC1_MULTIMATCH_NVEC];
    int aBest[VEC1_MULTIMATCH_NVEC];

    /* Initialize the training vector array with the VEC1_MULTIMATCH_NVEC
    ** training vectors that will be considered by this loop.  */
    for(vv=0; vv<VEC1_MULTIMATCH_NVEC; vv++){
      aVec[vv] = vec1TrainingVector(pVec, ii+vv);
    }

    vec1BestMatchPlatformN(
        nElem, p->aCentroid, p->aTrans, p->nK, aVec, aBest, &p->fTotal
    );

    /* Scalar array aBest[] now contains the index of the best centroid
    ** for each of the VEC1_MULTIMATCH_NVEC training vectors being considered
    ** by this loop. Update aCount[] and aSum[] avvordingly.   */
    for(vv=0; vv<VEC1_MULTIMATCH_NVEC; vv++){
      int iBest = aBest[vv];
      p->aCount[iBest]++;
      vec1AddInPlace(&p->aSum[iBest*nElem], aVec[vv], nElem);
    }

#if 0
    /* Check (assert) using vec1PqBestMatch() that the fancy code above
    ** actually worked.  */
    for(vv=0; vv<VEC1_MULTIMATCH_NVEC; vv++){
      int iBest2 = vec1PqBestMatch(
          p->aCentroid, p->nK, aVec[vv], nElem, 0
      );
      if( iBest2!=aBest[vv] ){
        const float *c1 = &p->aCentroid[aBest[vv] * nElem];
        const float *c2 = &p->aCentroid[iBest2 * nElem];
        float d1 = vec1L2Dist(aVec[vv], c1, nElem);
        float d2 = vec1L2Dist(aVec[vv], c2, nElem);
        assert( d1==d2
            || (d1>=d2 && ((d1 - d2) / d1) < 0.00001)
            || (d2>d1 && ((d2 - d1) / d2) < 0.00001)
        );
      }
    }
#endif
  }

  /* Scalar tail - handle training vectors that were not done above as
  ** part of a batch of VEC1_MULTIMATCH_NVEC. */
  for(; ii<p->iEof; ii++){
    const float *aSub = vec1TrainingVector(pVec, ii);
    int iBest = vec1PqBestMatch(p->aCentroid, p->nK, aSub, nElem, &p->fTotal);
    assert( iBest>=0 && iBest<p->nK );
    p->aCount[iBest]++;
    vec1AddInPlace(&p->aSum[iBest * nElem], aSub, nElem);
  }
}

/*
** Parameter aIn points to an array of centroids containing nK vectors,
** each nElem elements in size. i.e. so that element j of vector i is 
** stored in aIn[i*nElem + j]. Total size of the array is nK*nElem values. 
**
** Parameter aOut[] also points to an array of nK*nElem elements. This
** function populates that array with the transposed version of the codebook
** before returning.
*/
static void vec1CodebookTranspose(
  const float *aIn,               /* Centroid array in on-disk format */
  int nElem,                      /* Number of elements in each vectors */
  int nWidth,                     /* Transpose width */
  int nK,                         /* Number of centroids */
  float *aOut                     /* Populate this array with transformation */
){
  int K, d;
  float *pOut = aOut;

  /* If nK is not divisible by nWidth, just ignore any tail vectors. */
  assert( (nK % nWidth)==0 || nWidth==VEC1_MULTIMATCH_NCENT );
  nK = (nK / nWidth) * nWidth;

  for(K=0; K<nK; K+=nWidth){
    for(d=0; d<nElem; d++){
      int ii;
      for(ii=0; ii<nWidth; ii++){
        (*pOut++) = aIn[ (K+ii)*nElem + d ];
      }
    }
  }
  assert( pOut==&aOut[ nElem * nK ] );
}

static void vec1TrainLog(Vec1TrainCtx *p, const char *zFmt, ...){
  if( p->pLogStmt && p->rc==SQLITE_OK ){
    int percent = ((100*p->nCompletedWork) / p->nTotalWork);
    char *zMsg = 0;
    va_list ap;
    va_start(ap, zFmt);
    zMsg = sqlite3_vmprintf(zFmt, ap);
    va_end(ap);

    if( zMsg==0 ){
      p->rc = SQLITE_NOMEM;
    }else{
      sqlite3_bind_int(p->pLogStmt, 1, percent);
      sqlite3_bind_text(p->pLogStmt, 2, zMsg, -1, SQLITE_TRANSIENT);
      sqlite3_step(p->pLogStmt);
      sqlite3_free(zMsg);
      p->rc = sqlite3_reset(p->pLogStmt);
    }

    if( p->rc==SQLITE_NOMEM ){
      sqlite3_result_error_nomem(p->pCtx);
    }else if( p->rc!=SQLITE_OK ){
      sqlite3 *db = sqlite3_db_handle(p->pLogStmt);
      vec1ResultErrorF(p->pCtx, "%s", sqlite3_errmsg(db));
    }
  }
}

#define START_TRAINING_TIMER(p, i) { p->aTime[i] -= vec1HardwareTimer(); }
#define END_TRAINING_TIMER(p, i) { p->aTime[i] += vec1HardwareTimer(); }

static int vec1Ann1KMeans(
  Vec1TrainCtx *p,                /* Training data + parameters */
  Vec1TrainVectors *pVec,         /* Vectors to train K-Means on */
  Vec1JobQueue *pQueue,           /* Job-queue, or NULL for single thread */
  int nK,                         /* Value of K in K-means */
  float *aCode                    /* OUT: Populate this array */
){
  i64 nByte = 0;
  int ii;
  int iIter;
  int nElem = pVec->nElem;
  int *pCsr = 0;
  double fPrevTotal = 0.0;

  int *aCount = 0;
  float *aSum = 0;

  LloydsJob *aJob = 0;
  int rc = SQLITE_OK;
  int nThread = p ? p->nThread : 1;

  /* Allocate array of jobs to use for multi-threaded Lloyd's algorithm. 
  ** We use this even if there are no worker threads - in that case 
  ** allocate an array of 1. The algorithm parallelizes across vectors - each
  ** jobs is assigned a subset of the vectors to work on. 
  **
  ** Each job requires the following working space:
  **
  **   + aCount      -> (sizeof(int) * K) bytes
  **   + aSum        -> (sizeof_f32 * nElem * K) bytes
  */
  nByte = nThread * (
      sizeof(LloydsJob) +
      nElem * nK * sizeof_f32 +       /* aSum[] */
      nK * sizeof(int)                      /* aCount[] */
  );

  aJob = (LloydsJob*)sqlite3_malloc64(nByte);
  if( aJob==0 ) return SQLITE_NOMEM;
  memset(aJob, 0, sizeof(LloydsJob) * nThread);

  pCsr = (int*)&aJob[nThread];

  /* Initialize each job object in the array */
  for(ii=0; ii<nThread; ii++){
    LloydsJob *pJob = &aJob[ii];

    /* Assign a range of the training vectors to this job. This job will
    ** process vectors from iFirst to (iEof-1), inclusive.  */
    ASSIGN_VECTORS_TO_JOB(pVec, pJob, ii, nThread);

    pJob->nK = nK;
    pJob->aCentroid = aCode;
    pJob->aSum = (float*)pCsr;
    pCsr += (nK * nElem);
    pJob->aCount = pCsr;
    pCsr += nK;
  }
  aCount = aJob[0].aCount;
  aSum = aJob[0].aSum;

#ifdef VEC1_USE_TRANSPOSITION
  /* If this build uses transposed arrays of centroids, allocate space
  ** for the array here, and give each job a pointer to it. */
  aJob[0].aTrans = (float*)sqlite3_malloc64( sizeof_f32 * (nElem * nK) );
  if( aJob[0].aTrans==0 ){
    rc = SQLITE_NOMEM;
    goto vec1_kmeans_out;
  }
  for(ii=1; ii<nThread; ii++){
    aJob[ii].aTrans = aJob[0].aTrans;
  }
#endif

  assert( (u8*)pCsr==((u8*)aJob)+nByte );

  /* If pQueue is not NULL, then this is training the coarse quantizer
  ** and so we have exclusive access to the Vec1TrainCtx object. So
  ** it's ok to record timings. We can't do that if pQueue is NULL,
  ** as in that case multiple threads may be running this code with
  ** the same Vec1TrainCtx object.  */
  if( p ){
    START_TRAINING_TIMER(p, VEC1_TRAINING_COARSE_INIT);
    vec1TrainLog(p, "k-means++ initalization for coarse quant");
  }

  rc = vec1KMeansPlusPlus(pVec, pQueue, nK, aCode);
  if( rc!=SQLITE_OK ) goto vec1_kmeans_out;

  if( p ){
    END_TRAINING_TIMER(p, VEC1_TRAINING_COARSE_INIT);
    p->nCompletedWork += VEC1_TRAINING_WORK_COARSE1;
  }

  /* Initial centroids have now been chosen. So do K-means iteration to 
  ** improve the centroids. */
  if( p ){
    START_TRAINING_TIMER(p, VEC1_TRAINING_COARSE_KMEANS);
    vec1TrainLog(p, "k-means iterations for coarse quant");
  }
  for(iIter=0; iIter<VEC1_PQ_KMEANS_NITER; iIter++){
    double fTotal = 0.0;
    int jj;

#ifdef VEC1_USE_TRANSPOSITION
    vec1CodebookTranspose(
        aCode, nElem, VEC1_NTRANSPOSE_WIDTH, nK, aJob[0].aTrans
    );
#endif

    /* Now loop through all training vectors. Extract the sub-vector associated
    ** with this codebook and compare it with each of the current nK centroids.
    ** Update the aSum[] and aCount[] entries that correspond to the closest
    ** centroid found.  */
    for(ii=0; ii<nThread; ii++){
      vec1JobQueueAddJob(pQueue, vec1LloydsWork, 0, &aJob[ii]);
    }
    vec1JobQueueFinishJobs(pQueue, SQLITE_OK);

    fTotal = aJob[0].fTotal;
    for(ii=1; ii<nThread; ii++){
      for(jj=0; jj<nK; jj++){
        aCount[jj] += aJob[ii].aCount[jj];
      }
      vec1AddInPlace(aSum, aJob[ii].aSum, nElem*nK);
      fTotal += aJob[ii].fTotal;
    }

#if 0
    /* TODO: This is in theory better, but in practice it seems to mess up
    ** recall when using PQ codes and residual:1. So omit this for now. */
    if( p && p->eDistance==VEC1_DISTANCE_COS ){
      /* If this call is training a coarse quantizer for cosine distance,
      ** use "spherical k-means". This means that instead of just updating
      ** the centroid to be the average of all points assigned to it, we
      ** do that and then normalize the result - so that the centroid
      ** remains on the surface of the unit sphere.  */
      memcpy(aCode, aSum, sizeof_f32 * nK * nElem);
      for(ii=0; ii<nK; ii++){
        vec1NormalizeVector(&aCode[ii*nElem], nElem);
      }
    }else
#endif
    {
      /* Update each centroid to be the average of points assigned to it. */
      for(ii=0; ii<nK; ii++){
        if( aCount[ii]>0 ){
          int iElem;
          for(iElem=0; iElem<nElem; iElem++){
            int iCodeElem = ii*nElem + iElem;
            aCode[iCodeElem] = aSum[iCodeElem] / aCount[ii];
          }
        }
      }
    }

    /* If the sum of the L2-squared distortion produced by encoding the 
    ** training set using the current codebook is greater than
    ** VEC1_PQ_KMEANS_STOPVAL (which might be say 0.999) times that of the
    ** previous iteration, end the loop now.  */
    if( iIter>0 && (fTotal / fPrevTotal)>=VEC1_PQ_KMEANS_STOPVAL ){
      iIter = VEC1_PQ_KMEANS_NITER;
    }
    fPrevTotal = fTotal;
  }
  if( p ){ 
    END_TRAINING_TIMER(p, VEC1_TRAINING_COARSE_KMEANS);
    p->nCompletedWork += VEC1_TRAINING_WORK_COARSE2;
  }

 vec1_kmeans_out:
#ifdef VEC1_USE_TRANSPOSITION
  sqlite3_free( aJob[0].aTrans );
#endif
  sqlite3_free(aJob);
  return rc;
}

/*
** This function is called to train a coarse quantizer from within a 
** vec1_train() invocation. Output array aCentroid[], which must be
** (p->tv.nElem * p->nBucket) elements in size, is packed with 
** the p->nBucket centroids to use for coarse quantization before 
** returning.
*/
static void vec1TrainCoarseQuant(
  Vec1TrainCtx *p,                /* Training data + parameters */
  Vec1JobQueue *pQueue,           /* Job-queue, or NULL for single thread */
  float *aCentroid                /* OUT: Populate this array */
){
  if( p->rc==SQLITE_OK ){
    int rc = vec1Ann1KMeans(p, &p->tv, pQueue, p->nBucket, aCentroid);
    assert( rc==SQLITE_OK || rc==SQLITE_NOMEM );
    if( p->rc==SQLITE_OK && rc!=SQLITE_OK ){
      p->rc = rc;
      sqlite3_result_error_nomem(p->pCtx);
    }
  }
}

static int vec1ConfigInt(
  int eType,
  i64 iVal,                       /* Proposed parameter value */
  int nMin,                       /* Minimum allowed value */
  int nMax,                       /* Maximum allowed value */
  int bAllowZero,                 /* True if 0 is also allowed */
  const char *zName,              /* Name of parameter */
  char **pzErr                    /* OUT: Error message */
){
  int rc = SQLITE_OK;
  if( eType!=SQLITE_INTEGER
   || (!(bAllowZero && iVal==0) && (iVal<nMin || iVal>nMax))
  ){
    *pzErr = sqlite3_mprintf(
        "vec1: %s must be set to an integer value between %d and %d%s",
        zName, nMin, nMax, (bAllowZero ? ", or 0" : "")
    );
    rc = SQLITE_ERROR;
  }
  return rc;
}

static int vec1ConfigBool(
  int *pbOut,
  int eType,
  i64 iVal,
  const char *zName,
  char **pzErr
){
  int rc = SQLITE_OK;
  if( eType==SQLITE_INTEGER && (iVal==0 || iVal==1) ){
    *pbOut = (iVal!=0);
  }else{
    *pzErr = sqlite3_mprintf("vec1: %s must be set to a boolean value", zName);
    rc = SQLITE_ERROR;
  }
  return rc;
}

static int vec1ConfigEnum(
  int *piOpt,                     /* OUT: Index of selected value */
  const char *zVal,               /* Candidate value */
  const char **azOpt,             /* Array of options */
  const char *zName,              /* Name of parameter */
  char **pzErr                    /* OUT: Error message */
){
  int ii;
  char *zErr = 0;
  for(ii=0; azOpt[ii]; ii++){
    if( sqlite3_stricmp(zVal, azOpt[ii])==0 ){
      *piOpt = ii;
      return SQLITE_OK;
    }
  }

  zErr = sqlite3_mprintf("unrecognized %s '%s', should be one of", zName, zVal);
  for(ii=0; azOpt[ii]; ii++){
    if( azOpt[ii+1] ){
      zErr = sqlite3_mprintf("%z %s,", zErr, azOpt[ii]);
    }else{
      zErr = sqlite3_mprintf("%z or %s", zErr, azOpt[ii]);
    }
  }
  *pzErr = zErr;
  return SQLITE_ERROR;
}

static int vec1ConfigDistance(
  int *peDist,                    /* OUT: VEC1_DISTANCE_L2 or DISTANCE_COS */
  const char *zVal,               /* Candidate value */
  char **pzErr                    /* OUT: Error message */
){
  const char *azDist[] = {"l2", "cos", 0};
  int iOpt = 0;
  int rc = vec1ConfigEnum(&iOpt, zVal, azDist, "distance", pzErr);
  assert( VEC1_DISTANCE_L2==1 );
  assert( VEC1_DISTANCE_COS==2 );
  *peDist = iOpt+1;
  return rc;
}

/*
** Parameter zVal points to a string that was the value for the "quantize"
** field of a training configuration object. If the string is a recognized
** value for this field, output parameter (*peQuant) is set to the 
** corresponding VEC1_QUANTIZE_XXX value and SQLITE OK returned. 
**
** Otherwise, if the value cannot be recognized, (*pzErr) is set to point
** to a buffer containing an English language error message and SQLITE_ERROR
** is returned. It is the responsibility of the caller to eventually free
** the (*pzErr) buffer using sqlite3_free(). The final value of (*peQuant)
** is undefined in this case.
*/
static int vec1ConfigQuantizer(
  int *peQuant,                   /* OUT: VEC1_QUANT_XXX value */
  const char *zVal,               /* Candidate value */
  char **pzErr                    /* OUT: Error message */
){
  const char *azQuant[] = {"none", "pq", "opq", "bq", 0};
  
  assert( sqlite3_stricmp("none", azQuant[VEC1_QUANTIZE_NONE])==0 );
  assert( sqlite3_stricmp("pq", azQuant[VEC1_QUANTIZE_PQ])==0 );
  assert( sqlite3_stricmp("opq", azQuant[VEC1_QUANTIZE_OPQ])==0 );
  assert( sqlite3_stricmp("bq", azQuant[VEC1_QUANTIZE_BQ])==0 );

  return vec1ConfigEnum(peQuant, zVal, azQuant, "quantizer", pzErr);
}

static int vec1Ann1TrainCfg(
  void *pCtx,
  const char *zOpt,
  int eType,
  i64 iVal,
  double fVal,
  const char *zVal,
  char **pz
){
  int rc = SQLITE_OK;
  Vec1TrainCtx *p = (Vec1TrainCtx*)pCtx;

  const char *azOpt[] = {
    "distance",            /* 0 */
    "codesize",            /* 1 */
    "nbucket",             /* 2 */
    "quantizer",           /* 3 */

    "nthread",             /* 4 */
    "svd_verify",          /* 5 */
    "opq",                 /* 6 */
    "progress",            /* 7 */
    "residual",            /* 8 */
    "wht",                 /* 9 */
    "nopq_round",          /* 10 */
    "profile",             /* 11 */

    "index",               /* 12 */
     0
  };
  int eOpt = -1;

  UNUSED_PARAMETER(fVal);

  rc = vec1ConfigEnum(&eOpt, zOpt, azOpt, "option", pz);
  if( rc==SQLITE_OK ){
    switch( eOpt ){
      case 0:  assert( sqlite3_stricmp("distance", zOpt)==0 );
        rc = vec1ConfigDistance(&p->eDistance, zVal, pz);
        break;

      case 1:  assert( sqlite3_stricmp("codesize", zOpt)==0 );
        rc = vec1ConfigInt(eType, iVal, 
          VEC1_MIN_CODESIZE, VEC1_MAX_CODESIZE, 1, zOpt, pz
        );
        p->nCodesize = (int)iVal;
        break;

      case 2:  assert( sqlite3_stricmp("nbucket", zOpt)==0 );
        rc = vec1ConfigInt(eType, iVal, 
            VEC1_MIN_BUCKET, VEC1_MAX_BUCKET, 1, zOpt, pz
        );
        p->nBucket = (int)iVal;
        break;

      case 3:  assert( sqlite3_stricmp("quantizer", zOpt)==0 );
        rc = vec1ConfigQuantizer(&p->eQuant, zVal, pz);
        break;

      case 4:  assert( sqlite3_stricmp("nthread", zOpt)==0 );
        rc = vec1ConfigInt(eType, iVal, 1, VEC1_MAX_NTHREAD, 0, zOpt, pz);
#if VEC1_THREADS
        p->nThread = (int)iVal;
#endif
        break;

      case 5:  assert( sqlite3_stricmp("svd_verify", zOpt)==0 );
        rc = vec1ConfigBool(&p->bSvdVerify, eType, iVal, zOpt, pz);
        break;

      case 6:  assert( sqlite3_stricmp("opq", zOpt)==0 );
        rc = vec1ConfigBool(&p->bOpq, eType, iVal, zOpt, pz);
        break;

      case 7:  assert( sqlite3_stricmp("progress", zOpt)==0 ); 
        sqlite3_free(p->zLogFunction);
        p->zLogFunction = sqlite3_mprintf("%s", zVal);
        if( p->zLogFunction==0 ) rc = SQLITE_NOMEM;
        break;

      case 8:  assert( sqlite3_stricmp("residual", zOpt)==0 ); 
        rc = vec1ConfigBool(&p->bResidual, eType, iVal, zOpt, pz);
        break;

      case 9:  assert( sqlite3_stricmp("wht", zOpt)==0 ); 
        rc = vec1ConfigBool(&p->bWHT, eType, iVal, zOpt, pz);
        break;

      case 10: assert( sqlite3_stricmp("nopq_round", zOpt)==0 );
        rc = vec1ConfigInt(eType, iVal, 
            VEC1_MIN_NOPQ_ROUND, VEC1_MAX_NOPQ_ROUND, 0, zOpt, pz
        );
        p->nOpqRound = (int)iVal;
        break;

      case 11: assert( sqlite3_stricmp("profile", zOpt)==0 ); 
        sqlite3_free(p->zProfileFunction);
        p->zProfileFunction = sqlite3_mprintf("%s", zVal);
        if( p->zProfileFunction==0 ) rc = SQLITE_NOMEM;
        break;

      default: assert( sqlite3_stricmp("index", zOpt)==0 ); 
        assert( eOpt==12 );  /* index */
        if( sqlite3_stricmp("trained", zVal) ){
          *pz = sqlite3_mprintf(
              "unrecognized index '%s', should be 'trained'", zVal
          );
          rc = SQLITE_ERROR;
        }
        break;
    }
  }

  return rc;
}

#define VEC1_INDEX_NONE 1
#define VEC1_INDEX_FLAT 2

typedef struct Vec1FlatIndex Vec1FlatIndex;
struct Vec1FlatIndex {
  int eDistance;
  int eIndex;
};

static int vec1FlatIndexCfg(
  void *pCtx,
  const char *zOpt,
  int eType,
  i64 iVal,
  double fVal,
  const char *zVal,
  char **pz
){
  int rc = SQLITE_OK;
  Vec1FlatIndex *p = (Vec1FlatIndex*)pCtx;

  UNUSED_PARAMETER(eType);
  UNUSED_PARAMETER(iVal);
  UNUSED_PARAMETER(fVal);

  if( sqlite3_stricmp("distance", zOpt)==0 ){
    rc = vec1ConfigDistance(&p->eDistance, zVal, pz);
  }else
  if( sqlite3_stricmp("index", zOpt)==0 ){
    const char *azIdx[] = {"none", "flat", 0};
    int iOpt = 0;
    rc = vec1ConfigEnum(&iOpt, zVal, azIdx, "index", pz);
    assert( VEC1_INDEX_NONE==1 );
    assert( VEC1_INDEX_FLAT==2 );
    p->eIndex = iOpt+1;
  }else{
    *pz = sqlite3_mprintf("unknown parameter: %s", zOpt);
    rc = SQLITE_ERROR;
  }

  return rc;
}

/*
** aVec[] is a vector nElem elements in size. If any of these elements is
** non-finite - either NaN or an Infinity value - return SQLITE_ERROR. 
** Return SQLITE_OK if all elements are finite.
*/
static int vec1VectorIsNonFinite(const float *aVec, int nElem){
  static const u32 MASK = 0x7F800000;
  int ii = 0;

#if defined(VEC1_HAVE_AVX2)
  const __m256i exp_mask = _mm256_set1_epi32(MASK);
  for(; ii<=(nElem-8); ii+=8){
    __m256i bits = _mm256_loadu_si256((__m256i*)&aVec[ii]);
    __m256i exp_bits = _mm256_and_si256(bits, exp_mask);
    __m256i nonfinite = _mm256_cmpeq_epi32(exp_bits, exp_mask);
    if( _mm256_movemask_ps(_mm256_castsi256_ps(nonfinite)) ){
      goto error_out;
    }
  }
#endif

#if defined(VEC1_HAVE_NEON)
  const uint32x4_t exp_mask = vdupq_n_u32(MASK);
  for(; ii<=(nElem-4); ii+=4){
    uint32x4_t bits = vld1q_u32((u32*)&aVec[ii]);
    uint32x4_t exp_bits = vandq_u32(bits, exp_mask);
    uint32x4_t nonfinite = vceqq_u32(exp_bits, exp_mask);
    if( vmaxvq_u32(nonfinite) ){
      goto error_out;
    }
  }
#endif

  for(; ii<nElem; ii++){
    if( (((u32*)aVec)[ii] & MASK)==MASK ){
      goto error_out;
    }
  }

  return SQLITE_OK;

 error_out:
  return SQLITE_ERROR;
}

#define VEC1_REALLOC_NVEC 10000

/*
** The xStep() method for the vec1_train() aggregate function:
**
**     vec1_train(VECTOR, CONFIG)
**
** All that this step function does is populate the Vec1TrainCtx.aVec[] array.
** The interesting stuff happens in vec1TrainFinal().
*/
static void vec1TrainStep(
  sqlite3_context *pCtx, 
  int nArg, 
  sqlite3_value **aArg
){
  Vec1TabList *pList = (Vec1TabList*)sqlite3_user_data(pCtx);
  Vec1TrainCtx *p = 0;
  assert( nArg==1 || nArg==2 );
  int n = 0;
  const u8 *a = 0;
  int iChunk = 0;

  p = (Vec1TrainCtx*)sqlite3_aggregate_context(pCtx, sizeof(*p));
  if( !p ) return;

  n = sqlite3_value_bytes(aArg[0]);
  a = (const u8*)sqlite3_value_blob(aArg[0]);
  if( (n==0)
   || (n%sizeof_f32)!=0 
   || (p->tv.nElem!=0 && p->tv.nElem!=(int)(n/sizeof_f32))
  ){
    sqlite3_result_error(pCtx, "vec1_train: bad argument", -1);
    return;
  }
  if( vec1VectorIsNonFinite((const float*)a, (n/sizeof_f32)) ){
    sqlite3_result_error(pCtx, 
        "vec1_train: non-finite value not allowed in vector", -1
    );
    return;
  }

  /* If this is the first call to xStep() for this aggregate, initialize 
  ** the various "training parameters" fields of Vec1TrainCtx.  */
  if( p->tv.nElem==0 ){
    p->tv.nElem = n/sizeof_f32;
    p->tv.nVecPerChunk = (VEC1_TRAINING_SZCHUNK / (p->tv.nElem*sizeof_f32));

    /* Configure default values */
    p->eDistance = VEC1_TRAINING_DEFAULT_DISTANCE;
    p->nBucket = VEC1_TRAINING_DEFAULT_NBUCKET;
    p->nThread = pList->nThread;
    p->nOpqRound = VEC1_TRAINING_DEFAULT_NOPQ_ROUND;
    p->bSvdVerify = VEC1_TRAINING_DEFAULT_SVD_VERIFY;
    p->bResidual = VEC1_TRAINING_DEFAULT_RESIDUAL;
    p->eQuant = -1;
    p->nCodesize = -1;
    p->bWHT = -1;
    p->bOpq = -1;

    /* If a configuration object was passed, update the Vec1TrainCtx with
    ** its field values now.  */
    if( nArg==2 ){
      sqlite3 *db = sqlite3_context_db_handle(pCtx);
      const char *zCfg = (const char*)sqlite3_value_text(aArg[1]);
      char *zErr = 0;
      int rc = vec1ParseJsonConfig(db, zCfg, vec1Ann1TrainCfg, (void*)p, &zErr);
      if( rc ){
        if( zErr!=0 ){
          vec1ResultErrorF(pCtx, "%z", zErr);
        }else{
          sqlite3_result_error_code(pCtx, rc);
        }
        return;
      }
    }

    /* If no quantizer was specified, select one automatically now. */
    if( p->eQuant<0 ){
      if( p->nCodesize<=0 ){
        p->eQuant = VEC1_QUANTIZE_NONE;
      }else if( p->bOpq>0 ){
        p->eQuant = VEC1_QUANTIZE_OPQ;
      }else{
        p->eQuant = VEC1_QUANTIZE_PQ;
      }
    }

    /* If no "wht" value was explicitly specified, enable the Walsh-Hadamard
    ** Transform for binary quantizers. Disable it for other quantizers.  */
    if( p->bWHT<0 ) p->bWHT = (p->eQuant==VEC1_QUANTIZE_BQ);

    /* If no value was specified for "codesize", choose a default. This
    ** is 32 for PQ or OPQ, 0 for "none" quantizer, and (nElem+7)/8 for BQ. */
    if( p->eQuant==VEC1_QUANTIZE_NONE ){
      p->nCodesize = 0;
      p->bResidual = 0;
    }else if( p->eQuant==VEC1_QUANTIZE_BQ ){
      if( p->nCodesize<=0 || p->nCodesize>=((p->tv.nElem+7)/8) ){
        /* No explicit codesize. Or an explicit codesize large enough to
        ** use 1 bit per dimension. So do that - use 1 bit per dimension. */
        p->nCodesize = (p->tv.nElem + 7) / 8;
      }else{
        /* Explicit codesize too small for 1 bit per dimension. So use 1 bit
        ** for each two dimensions.  */
        p->nCodesize = (p->tv.nElem + 15) / 16;
      }
    }else if( p->nCodesize<=0 ){
      if( p->eQuant==VEC1_QUANTIZE_PQ ){
        p->nCodesize = (p->tv.nElem + 7) / 8;
      }else{
        assert( p->eQuant==VEC1_QUANTIZE_OPQ );
        p->nCodesize = 32;
      }
    }

    if( p->eQuant==VEC1_QUANTIZE_PQ || p->eQuant==VEC1_QUANTIZE_OPQ ){
      /* Usually, tv.nElem will be an integer multiple of nCodebook. In
      ** this case set nCodeElem is just set to (tv.nElem / nCodebook) and
      ** nCodebook remains as configured.
      **
      ** Or, if tv.nElem is not a multiple of nCodebook, set nCodeElem to
      ** the smallest value that allows us to cover the entire vector with
      ** nCodebook codebooks. Then reduce nCodebook to the minimum number
      ** of codebooks required to cover the vector with nCodeElem 
      ** sub-vectors.  */
      p->nCodeElem = ((p->tv.nElem+p->nCodesize-1) / p->nCodesize);
      p->nCodesize = ((p->tv.nElem+p->nCodeElem-1) / p->nCodeElem);
    }
  }

  iChunk = (p->tv.nVec+1)/p->tv.nVecPerChunk;
  if( iChunk>=p->tv.nChunk ){
    p->tv.aChunk[iChunk] = (float*)sqlite3_malloc(VEC1_TRAINING_SZCHUNK);
    if( p->tv.aChunk[iChunk]==0 ){
      sqlite3_result_error_nomem(pCtx);
      return;
    }
    p->tv.nChunk++;
  }

  /* Append a copy of the vector to Vec1TrainCtx.aVec[]. */
  memcpy(vec1TrainingVector(&p->tv, p->tv.nVec), sqlite3_value_blob(aArg[0]),n);
  p->tv.nVec++;
}

/*
** Normalize all the training vectors.
*/
static void vec1TrainNormalizeAll(Vec1TrainCtx *p){
  int ii;
  for(ii=0; ii<p->tv.nVec; ii++){
    vec1NormalizeVector(vec1TrainingVector(&p->tv, ii), p->tv.nElem);
  }
}

static void vec1FreeVectors(Vec1TrainVectors *pFree){
  if( pFree ){
    int ii;
    for(ii=0; ii<pFree->nChunk; ii++){
      sqlite3_free(pFree->aChunk[ii]);
    }
    sqlite3_free(pFree);
  }
}

/*
** An instance of the following structure is allocated for each PQ codebook
** to train. Each codebook can then be trained in a separate thread.
*/
typedef struct Vec1CodebookJob Vec1CodebookJob;
struct Vec1CodebookJob {
  Vec1TrainCtx *p;
  Vec1TrainVectors *pTrain;
  int iOff;
  float *aCode;
  int rc;
};

static void vec1CodebookWork(void *pCtx){
  Vec1CodebookJob *pJob = (Vec1CodebookJob*)pCtx;
  Vec1TrainCtx *p = pJob->p;
  int nByte = sizeof(Vec1TrainVectors);

  Vec1TrainVectors *pVec = (Vec1TrainVectors*)vec1MallocZero(nByte);

  if( pVec==0 ){
    pJob->rc = SQLITE_NOMEM;
  }else{
    int ii;
    int nShort = 0;
    int nBytePerVec = p->nCodeElem * sizeof_f32;
    pVec->nElem = p->nCodeElem;
    pVec->nVec = pJob->pTrain->nVec;
    pVec->nVecPerChunk = (VEC1_TRAINING_SZCHUNK / nBytePerVec);
    pVec->nChunk = (pVec->nVec+pVec->nVecPerChunk-1) / pVec->nVecPerChunk;

    /* If the training vector size is not an integer multiple of the 
    ** sub-vector size and this is the rightmost sub-vector (the one that
    ** will need to be padded with 0.0 values), set nShort to non-zero. */
    if( (pJob->iOff + p->nCodeElem)>pJob->pTrain->nElem ){
      nShort = pJob->pTrain->nElem - pJob->iOff;
    }

    /* Allocate chunks for the new Vec1TrainVectors structure. If nShort
    ** is set, also zero them out.  */
    for(ii=0; ii<pVec->nChunk; ii++){
      /* Allocate space for this chunk. Either VEC1_TRAINING_SZCHUNK or
      ** enough space for all remaining sub-vectors, whichever is smaller. */
      int nAlloc = MIN(VEC1_TRAINING_SZCHUNK, 
          (pVec->nVec - (ii * pVec->nVecPerChunk)) * nBytePerVec
      );
      pVec->aChunk[ii] = (float*)sqlite3_malloc(nAlloc);

      if( pVec->aChunk[ii]==0 ){
        pJob->rc = SQLITE_NOMEM;
        break;
      }
      if( nShort>0 ){
        memset(pVec->aChunk[ii], 0, nAlloc);
      }
    }

    if( pJob->rc==SQLITE_OK ){
      int nCopy = (nShort ? nShort : pVec->nElem) * sizeof_f32;
      for(ii=0; ii<pVec->nVec; ii++){
        float *aVec = vec1TrainingVector(pJob->pTrain, ii);
        memcpy(vec1TrainingVector(pVec, ii), &aVec[pJob->iOff], nCopy);
      }

      pJob->rc = vec1Ann1KMeans(0, pVec, 0, VEC1_PQ_CODEBOOK_SZ, pJob->aCode);
    }

    vec1FreeVectors(pVec);
  }
}

static int vec1CodebookFinal(void *pCtx, int rcin){
  UNUSED_PARAMETER(rcin);
  return ((Vec1CodebookJob*)pCtx)->rc;
}

static void vec1TrainPQCodebooks(
  Vec1TrainCtx *p,
  Vec1TrainVectors *pVec,
  Vec1JobQueue *pQueue,
  float *aBook
){
  if( p->rc==SQLITE_OK ){
    Vec1CodebookJob *aJob = 0;

    aJob = vec1MallocZero(sizeof(Vec1CodebookJob) * p->nCodesize);
    if( aJob ){
      int ii;
      int rc = SQLITE_OK;
      for(ii=0; rc==SQLITE_OK && ii<p->nCodesize; ii++){
        Vec1CodebookJob *pJob = &aJob[ii];
        pJob->p = p;
        pJob->iOff = ii * p->nCodeElem;
        pJob->aCode = &aBook[ii * p->nCodeElem * VEC1_PQ_CODEBOOK_SZ];
        pJob->rc = SQLITE_OK;
        pJob->pTrain = pVec;
        rc = vec1JobQueueAddJob(
            pQueue, vec1CodebookWork, vec1CodebookFinal, pJob
        );
      }
      p->rc = vec1JobQueueFinishJobs(pQueue, rc);
    }else{
      p->rc = SQLITE_NOMEM;
    }

    if( p->rc!=SQLITE_OK ){
      sqlite3_result_error_code(p->pCtx, p->rc);
    }
    sqlite3_free(aJob);
  }
}

static void vec1TransposeMatrix(double *aMatrix, int nElem) {
  for (int i = 0; i < nElem - 1; i++) {
    for (int j = i + 1; j < nElem; j++) {
      double tmp = aMatrix[i * nElem + j];
      aMatrix[i * nElem + j] = aMatrix[j * nElem + i];
      aMatrix[j * nElem + i] = tmp;
    }
  }
}
static int vec1JacobiDoOnePair(
  int p, int q,
  double *A,
  int d,
  double *Vt,
  double fTol
){
  double alpha = 0.0, beta = 0.0, gamma = 0.0;
  int r;

  double *Ap = A + p*d;
  double *Aq = A + q*d;

  assert( p<q );

  r = 0;
#ifdef VEC1_HAVE_AVX2
  __m256d valpha = _mm256_setzero_pd();
  __m256d vbeta  = _mm256_setzero_pd();
  __m256d vgamma = _mm256_setzero_pd();
  for(/*no-op*/; r<(d-3); r+=4) {
    __m256d vAp = _mm256_loadu_pd(Ap + r);
    __m256d vAq = _mm256_loadu_pd(Aq + r);
    valpha = FMADD64(vAp, vAp, valpha);
    vbeta  = FMADD64(vAq, vAq, vbeta);
    vgamma = FMADD64(vAp, vAq, vgamma);
  }
  HORIZONTAL_SUM64(alpha, valpha);
  HORIZONTAL_SUM64(beta, vbeta);
  HORIZONTAL_SUM64(gamma, vgamma);
#endif
#ifdef VEC1_HAVE_NEON
  float64x2_t valpha  = vdupq_n_f64(0.0);
  float64x2_t vbeta  = vdupq_n_f64(0.0);
  float64x2_t vgamma  = vdupq_n_f64(0.0);
  for(/*no-op*/; r<=(d-2); r+=2) {
    float64x2_t vAp = vld1q_f64(&Ap[r]);
    float64x2_t vAq = vld1q_f64(&Aq[r]);
    valpha = vfmaq_f64(valpha, vAp, vAp);
    vbeta = vfmaq_f64(vbeta, vAq, vAq);
    vgamma = vfmaq_f64(vgamma, vAp, vAq);
  }
  alpha = vaddvq_f64(valpha);
  beta = vaddvq_f64(vbeta);
  gamma = vaddvq_f64(vgamma);
#endif

  for(/*no-op*/; r<d; r++) {
    alpha += Ap[r] * Ap[r];
    beta  += Aq[r] * Aq[r];
    gamma += Ap[r] * Aq[r];
  }

  if( gamma!=0.0 && fabs(gamma)>=fTol*sqrt(alpha*beta) ){
    double zeta = (beta - alpha) / (2.0 * gamma);
    double t, c, s;
    if (zeta >= 0.0){
      t =  1.0 / ( zeta + sqrt(1.0 + zeta * zeta));
    }else{
      t = -1.0 / (-zeta + sqrt(1.0 + zeta * zeta));
    }

    /* Damping factor apparently sometimes required by parallel Jacobi. 
    ** Wait to see if we have a problem first I suppose... */
    /* t = t*0.5; */
    c = 1.0 / sqrt(1.0 + t * t);
    s = -c * t;

    /* Rotate columns p and q of A */
    r = 0;
#ifdef VEC1_HAVE_AVX2
    __m256d vc = _mm256_set1_pd(c);
    __m256d vs = _mm256_set1_pd(s);
    __m256d vms = _mm256_set1_pd(-s);
    for(; r<d-3; r+=4) {
      __m256d vap = _mm256_loadu_pd(&Ap[r]);
      __m256d vaq = _mm256_loadu_pd(&Aq[r]);

      _mm256_storeu_pd(&Ap[r], FMADD64(vc, vap, _mm256_mul_pd(vs, vaq)));
      _mm256_storeu_pd(&Aq[r], FMADD64(vms, vap, _mm256_mul_pd(vc, vaq)));
    }
#endif
#ifdef VEC1_HAVE_NEON
    float64x2_t vc  = vdupq_n_f64(c);
    float64x2_t vs  = vdupq_n_f64(s);
    float64x2_t vms = vdupq_n_f64(-s);
    for(; r<=d-2; r+=2) {
      float64x2_t vap = vld1q_f64(&Ap[r]);
      float64x2_t vaq = vld1q_f64(&Aq[r]);
      vst1q_f64(&Ap[r], vfmaq_f64(vmulq_f64(vs,  vaq), vc,  vap));
      vst1q_f64(&Aq[r], vfmaq_f64(vmulq_f64(vc,  vaq), vms, vap));
    }
#endif
    for(/*no-op*/; r<d; r++) {
      double wp = Ap[r];
      double wq = Aq[r];
      Ap[r] =  c * wp + s * wq;
      Aq[r] = -s * wp + c * wq;
    }

    /* Accumulate into Vt. Vt is accumulated here in ROW-MAJOR
    ** format. This is so that rows are contiguous to avoid thrashing
    ** the cache. 
    **
    ** We are building V by right-multiplying by each Jacobi
    ** rotation G(p,q,theta).  Equivalently, we update Vt by
    ** left-multiplying its rows p and q.
    **
    **   Vt(p, r) = c * Vt(p,r) + s * Vt(q,r)
    **   Vt(q, r) = -s * Vt(p,r_old) + c * Vt(q,r)
    */
    double *Vtp = Vt + p * d;
    double *Vtq = Vt + q * d;
    r = 0;
#ifdef VEC1_HAVE_AVX2
    for(; r<d-3; r+=4) {
      __m256d vvp = _mm256_loadu_pd(&Vtp[r]);
      __m256d vvq = _mm256_loadu_pd(&Vtq[r]);
      _mm256_storeu_pd(&Vtp[r], FMADD64(vc, vvp, _mm256_mul_pd(vs, vvq)));
      _mm256_storeu_pd(&Vtq[r], FMADD64(vms,vvp, _mm256_mul_pd(vc, vvq)));
    }
#endif
#ifdef VEC1_HAVE_NEON
    for (; r<=d-2; r+=2) {
      float64x2_t vvp = vld1q_f64(&Vtp[r]);
      float64x2_t vvq = vld1q_f64(&Vtq[r]);
      vst1q_f64(&Vtp[r], vfmaq_f64(vmulq_f64(vs,  vvq), vc,  vvp));
      vst1q_f64(&Vtq[r], vfmaq_f64(vmulq_f64(vc,  vvq), vms, vvp));
    }
#endif
    for(; r < d; r++){
      double vp = Vtp[r];
      double vq = Vtq[r];
      Vtp[r] =  c * vp + s * vq;
      Vtq[r] = -s * vp + c * vq;
    }

    return 1;
  }

  return 0;
}


/*
** Return the kth pair of round r of a round-robin with n contestents.
*/
static void vec1RoundRobinPair(
  int n,                          /* Number of elements to pair */
  int r,                          /* Round number [0..n-2] */
  int k,                          /* Pair number [0..(n/2)-1] */
  int *i,                         /* First element of pair */
  int *j                          /* Second element of pair */
) {
  assert( (n%2)==0 );
  if( k==0 ){
    *i = 0;
    *j = (n - 1 - r);
  } else {
    int a = ((n-1)+k - 1 - r) % (n-1);
    int b = ((n-2)+(n-k-1) - r) % (n-1);
    *i = a + 1;
    *j = b + 1;
  }
}

#if 0
static int vec1TestRoundRobin(sqlite3_context *pCtx){
#define VEC1_RR_SZTEST 512
  int aCol[VEC1_RR_SZTEST];
  int aSeen[VEC1_RR_SZTEST];
  int r;
  int k;

  for(k=0; k<VEC1_RR_SZTEST; k++) aCol[k] = k;

  for(r=0; r<(VEC1_RR_SZTEST-1); r++){
    int last = aCol[VEC1_RR_SZTEST-1];
    memset(aSeen, 0, sizeof(int)*VEC1_RR_SZTEST);
    for(k=0; k<(VEC1_RR_SZTEST/2); k++){
      int a, b;
      vec1RoundRobinPair(VEC1_RR_SZTEST, r, k, &a, &b);
      if( a!=aCol[k] || b!=aCol[VEC1_RR_SZTEST-k-1] ){
        vec1ResultErrorF(pCtx, "error in round-robin test");
        return SQLITE_ERROR;
      }
      if( aSeen[a] || aSeen[b] ){
        vec1ResultErrorF(pCtx, "big error in round-robin test");
      }
      aSeen[a] = 1;
      aSeen[b] = 1;
    }

    /* Hold aCol[0] constant, rotate the remaining elements of the vector. */
    memmove(&aCol[2], &aCol[1], sizeof(int)*(VEC1_RR_SZTEST-2));
    aCol[1] = last;
  }

  return SQLITE_OK;
}
#endif

/*
** Each job in the paralell blocked jacobi transformation is represented
** by an instance of this structure.
*/
typedef struct Vec1JacobiJob Vec1JacobiJob;
struct Vec1JacobiJob {
  double *A;                      /* Input matrix (column-major) */
  int nElem;                      /* Matrices are nElem*nElem */
  double *Vt;                     /* Vt matrix (row-major) */
  double fTol;                    /* Tolerance constant */

  int iJob;                       /* Job number */
  int r;                          /* Current round-robin round */
  int nColPerBlk;                 /* Number of columns in each block */
  int nBlk;                       /* Number of blocks */

  int nApply;                     /* Number of rotations applied this sweep. */
};

static Vec1JacobiJob *vec1JacobiAlloc(
  Vec1TrainCtx *p,
  double *A,
  int nElem,
  double *Vt,
  double fTol,
  int *pnJob,                     /* OUT: Number of objects in returned array */
  int *pnStep
){
  int nJob = p->nThread + (p->nThread==1);
  int nBlk = nJob*2;
  int nByte = nJob * sizeof(Vec1JacobiJob);

  Vec1JacobiJob *aJob = (Vec1JacobiJob*)vec1MallocZero(nByte);
  *pnJob = 0;
  if( aJob==0 ){
    p->rc = SQLITE_NOMEM;
    sqlite3_result_error_nomem(p->pCtx);
  }else{
    int ii;
    int nColPerBlk = (nElem + nBlk-1)/ nBlk;
    for(ii=0; ii<nJob; ii++){
      Vec1JacobiJob *pJob = &aJob[ii];
      pJob->A = A;
      pJob->nElem = nElem;
      pJob->Vt = Vt;
      pJob->fTol = fTol;
      pJob->iJob = ii;
      pJob->nColPerBlk = nColPerBlk;
      pJob->nBlk = nBlk;
    }

    *pnJob = nJob;
    *pnStep = nBlk-1;
  }
  return aJob;
}

static void vec1JacobiWork(void *pArg){
  Vec1JacobiJob *pJob = (Vec1JacobiJob*)pArg;
  int iBlk1 = 0;
  int iBlk2 = 0;
  int p0, p1, q0, q1;
  int p;

  vec1RoundRobinPair(pJob->nBlk, pJob->r, pJob->iJob, &iBlk1, &iBlk2);
  if( iBlk1>iBlk2 ){
    SWAP(int, iBlk1, iBlk2);
  }

  p0 = iBlk1 * pJob->nColPerBlk;
  q0 = iBlk2 * pJob->nColPerBlk;
  p1 = MIN(p0 + pJob->nColPerBlk, pJob->nElem);
  q1 = MIN(q0 + pJob->nColPerBlk, pJob->nElem);

  /* Do cross-block pairs */
  for(p=p0; p<p1; p++){
    int q;
    for(q=q0; q<q1; q++){
      pJob->nApply += vec1JacobiDoOnePair(
          p, q, pJob->A, pJob->nElem, pJob->Vt, pJob->fTol
      );
    }
  }

  /* If this is round 0, also do intra-block pairs */
  if( pJob->r==0 ){
    for(p=p0; p<p1; p++){
      int q;
      for(q=p+1; q<p1; q++){
        pJob->nApply += vec1JacobiDoOnePair(
            p, q, pJob->A, pJob->nElem, pJob->Vt, pJob->fTol
        );
      }
    }
    for(p=q0; p<q1; p++){
      int q;
      for(q=p+1; q<q1; q++){
        pJob->nApply += vec1JacobiDoOnePair(
            p, q, pJob->A, pJob->nElem, pJob->Vt, pJob->fTol
        );
      }
    }
  }
}

/*
** This routine computes SVD of a column-major square matrix A (d x d):
**
**     A = U * diag(S) * Vt
**
** Parameters:
**   A        - input [d*d], column-major. MODIFIED BY THIS ROUTINE
**   d        - matrix dimension
**   U        - output [d*d], column-major. Columns = left singular vectors.
**   S        - output [d]. Singular values, not sorted.
**   Vt       - output [d*d], column-major storage of V^T.
**              Row j of V^T (right singular vector j) occupies
**              column j in memory: elements Vt[j*d .. j*d+d-1].
**   nMaxIter - max sweeps
**   fTol     - convergence tolerance
**
** Returns number of sweeps performed.
**/
static void vec1JacobiSVD(
  Vec1TrainCtx *pTrain,
  Vec1JobQueue *pQueue,
  double *A,
  int d,
  double *U,
  double *S,
  double *Vt,
  int nMaxIter,                   /* Maximum number of sweeps */
  double fTol
){
  int j = 0;                      /* Iterator */
  int nSweep = 0;                 /* Number of sweeps performed so far */
  int nStep = 0;                  /* Number of steps per sweep */

  Vec1JacobiJob *aJob = 0;
  int nJob = 0;

  assert( nMaxIter>0 && fTol>0.0 );
  aJob = vec1JacobiAlloc(pTrain, A, d, Vt, fTol, &nJob, &nStep);
  if( aJob==0 ) return;

  /* Vt is initialised to an identity matrix. */
  memset(Vt, 0, d * d * sizeof(double));
  for(j=0; j<d; j++){
    Vt[j*d + j] = 1.0;
  }

  for(nSweep=0; nSweep<nMaxIter; nSweep++){
    int iStep;
    for(j=0; j<nJob; j++){
      aJob[j].nApply = 0;
    }

    for(iStep=0; iStep<nStep; iStep++){
      for(j=0; j<nJob; j++){
        aJob[j].r = iStep;
        vec1JobQueueAddJob(pQueue, vec1JacobiWork, 0, &aJob[j]);
      }
      vec1JobQueueFinishJobs(pQueue, SQLITE_OK);
    }

    for(j=0; j<nJob; j++){
      if( aJob[j].nApply>0 ) break;
    }
    if( j==nJob ){
      nSweep = nMaxIter;
    }
  }

  /* Vt was accumulated in row-major format. Flip it back to column-major */
  vec1TransposeMatrix(Vt, d);

  /*
  ** Columns of A are now orthogonal. The singular value j is the norm
  ** of column j of A. Column j of U is column j of A, normalised.
  */
  for(j=0; j<d; j++){
    int r;
    double *Aj = A + j * d;
    double *Uj = U + j * d;

    double norm = 0.0;
    for(r=0; r<d; r++){
      norm += Aj[r] * Aj[r];
    }
    norm = sqrt(norm);
    S[j] = norm;

    if( norm>fTol ){
      for (r=0; r<d; r++) Uj[r] = Aj[r] / norm;
    } else {
      for (r=0; r<d; r++) Uj[r] = (r == j) ? 1.0 : 0.0;
    }
  }

  sqlite3_free(aJob);
}

/*
** Compute:
**
**   aOut = a1 x a2
**
** All matrixes are in column-major (Fortran) order. 
*/
static void vec1MatrixMultiply(
  float *aOut,                    /* (nElem*nElem) output array */
  double *a1,  
  double *a2, 
  int nElem
){
  int j;
  memset(aOut, 0, sizeof_f32*nElem*nElem);

  for(j=0; j<nElem; j++){                  /* column of aOut */
    int k;
    for(k=0; k<nElem; k++){                /* column of a1 / row of a2 */
      double a2_kj = a2[k + j*nElem];
      int i;
      for (i = 0; i < nElem; i++) {    /* row of aOut */
        aOut[i + j*nElem] += (float)(a1[i + k*nElem] * a2_kj);
      }
    }
  }
}

/*
** Compute:
**
**   aOut = a1 x a2
**
** All matrixes are in column-major (Fortran) order. The difference between
** this routine and vec1MatrixMultiply() is that the input matrices are
** composed of type float for this function.
*/
static void vec1MatrixMultiplyF(
  float *aOut,                    /* (nElem*nElem) output array */
  float *a1,                      /* (nElem*nElem) input matrix 1 */
  float *a2,                      /* (nElem*nElem) input matrix 2 */
  int nElem                       /* Size of matrices */
){
  int j;
  memset(aOut, 0, sizeof_f32*nElem*nElem);

  for(j=0; j<nElem; j++){                  /* column of aOut */
    int k;
    for(k=0; k<nElem; k++){                /* column of a1 / row of a2 */
      float a2_kj = a2[k + j*nElem];
      int i;
      for (i = 0; i < nElem; i++) {    /* row of aOut */
        aOut[i + j*nElem] += a1[i + k*nElem] * a2_kj;
      }
    }
  }
}

/*
** aMatrix is an nElem*nElem matrix in column-major format. It is supposed to
** be orthogonal, implying that:
**
**     * The dot-product of each pair of columns is 0.0, and 
**     * The magnitude of each column is 1.0.
**
** This function tests these properties and returns the sum of the errors.
** If the returned value is very close to 0.0 then the matrix is orthogonal.
** This is used as part of internal verification of the SVD calculation.
*/
static double vec1TestOrthogonality(const double *aMatrix, int nElem) {
  double err = 0.0;
  for (int i = 0; i < nElem; i++) {
    for (int j = 0; j < nElem; j++) {
      /* dot product of column i and column j */
      double dot = 0.0;
      int r;
      for(r=0; r<nElem; r++){
        dot += aMatrix[i*nElem + r] * aMatrix[j*nElem + r];
      }
      /* subtract identity */
      double diff = dot - (i == j ? 1.0 : 0.0);
      err += diff * diff;
    }
  }
  return sqrt(err);
}

/*
** Parameter A is an nElem*nElem matrix in column major format that has
** just undergone SVD, the outputs of which were U (nElem*nElem matrix),
** S (the nElem singular values from diagonal matrix E) and VT 
** (another nElem*nElem matrix.
**
** This function tests that the product of U.E.VT is in fact A, and 
** returns the magnitude of the discrepancy as a real value. If the
** returned value is close to 0.0, then the U.E.VT is a good decomposition
** of A. 
*/
static double vec1TestReconstruction(
  const double *A,   /* input matrix [nElem*nElem], column-major */
  const double *U,   /* left singular vectors [nElem*nElem], column-major */
  const double *S,   /* singular values [nElem] */
  const double *Vt,  /* right singular vectors [nElem*nElem], column-major */
  int nElem
) {
  double err = 0.0;
  double nrm = 0.0;
  int row, col;

  for(row=0; row<nElem; row++){
    for(col=0; col<nElem; col++){
      /* Compute (U * diag(S) * Vt)[row, col] */
      double val = 0.0;
      double a, diff;
      for (int k = 0; k < nElem; k++){
        val += U[k*nElem + row] * S[k] * Vt[col*nElem + k];
      }
      a = A[col*nElem + row];
      diff = a - val;
      err += diff * diff;
      nrm += a * a;
    }
  }

  return sqrt(err/nrm);
}

/*
** Check that U, S and Vt represent an SVD of matrix A. Return SQLITE_OK
** if they do, or an SQLite error code otherwise.
**
** U, S and Vt are considered a valid SVD of A if:
**
**   + U is an orthogonal matrix, and
**   + Vt is an orthogonal matrix, and
**   + U.S.Vt is equal to A.
*/ 
static int vec1SVDSelfTest(
  sqlite3_context *pCtx,
  const double *A,   /* input matrix, column-major */
  const double *U,   /* left singular vector matrix, column-major */
  const double *S,   /* singular values */
  const double *Vt,  /* right singular vector matrix , column-major */
  int nElem
){
  const double threshold = 0.00001;

  const double fOrthU = vec1TestOrthogonality(U, nElem);
  const double fOrthVT = vec1TestOrthogonality(Vt, nElem);
  const double fReconstruction = vec1TestReconstruction(A, U, S, Vt, nElem);

  /* COVERAGE: These three branches are difficult to hit, as the SVD
  ** computation always works.  */
  if( fOrthU>threshold || fOrthVT>threshold || fReconstruction>threshold ){
    vec1ResultErrorF(pCtx, "vec1: SVD error - "
        "fOrthU=%f, fOrthVT=%f, fReconstruction=%f", 
        fOrthU, fOrthVT, fReconstruction
    );
    return SQLITE_ERROR;
  }

  return SQLITE_OK;
}

typedef struct Vec1CovarianceJob Vec1CovarianceJob;
struct Vec1CovarianceJob {
  double *aM;          /* output matrix - each job writes distinct rows */
  float *aBook;        /* codebook */

  Vec1TrainVectors *pVec;

  int    nCodeElem;    /* Number of elements in each subspace */
  int    iSub;         /* Our job to do this subspace */
  double fTotalDist;   /* private accumulator */
#ifdef VEC1_USE_TRANSPOSITION
  float *aTrans;        /* Transposed version of this job's codebook */
#endif
};

/* 
** One job per sub-space. 
*/
static Vec1CovarianceJob *vec1CovarianceAlloc(
  Vec1TrainCtx *p,
  double *aM,
  Vec1TrainVectors *pVec,
  float *aBook,
  int *pnJob                      /* OUT: number of jobs */
){
  int nByte = sizeof(Vec1CovarianceJob) * p->nCodesize;
  Vec1CovarianceJob *aJob;

#ifdef VEC1_USE_TRANSPOSITION
  /* Add space for each job to create a transposed version of its codebook */
  nByte += sizeof_f32 * p->nCodeElem * VEC1_PQ_CODEBOOK_SZ * p->nCodesize;
#endif
  aJob = (Vec1CovarianceJob*)vec1MallocZero(nByte);

  assert( p->rc==SQLITE_OK );
  *pnJob = 0;
  if( aJob ){
    int ii;
#ifdef VEC1_USE_TRANSPOSITION
    float *aTrans = (float*)&aJob[p->nCodesize];
    for(ii=0; ii<p->nCodesize; ii++){
      aJob[ii].aTrans = &aTrans[ii * p->nCodeElem * VEC1_PQ_CODEBOOK_SZ];
    }
#endif
    for(ii=0; ii<p->nCodesize; ii++){
      Vec1CovarianceJob *pJob = &aJob[ii];
      pJob->aM = aM;
      pJob->aBook = aBook;
      pJob->pVec = pVec;
      pJob->nCodeElem = p->nCodeElem;
      pJob->iSub = ii;
    }
    *pnJob = p->nCodesize;
  }else{
    sqlite3_result_error_nomem(p->pCtx);
    p->rc = SQLITE_NOMEM;
  }

  return aJob;
}

#if defined(VEC1_HAVE_AVX2)
/*
** Convert each element of the 8-float vector fVec to a double, and add
** each scalar value to the corresponding aOut[] element.
*/
static void vec1AddToDoubleArrayAVX2(double *aOut, __m256 fVec){
  __m256d lo = _mm256_cvtps_pd(_mm256_castps256_ps128(fVec) );
  __m256d hi = _mm256_cvtps_pd(_mm256_extractf128_ps(fVec, 1));
  __m256d lo1 = LOADU64(&aOut[0]);
  __m256d hi1 = LOADU64(&aOut[4]);
  _mm256_storeu_pd(&aOut[0], _mm256_add_pd(lo, lo1));
  _mm256_storeu_pd(&aOut[4], _mm256_add_pd(hi, hi1));
}
#endif

#if defined(VEC1_HAVE_NEON)
/*
** Convert each element of the 4-float vector fVec to a double, and add
** each scalar value to the corresponding aOut[] element.
*/
static void vec1AddToDoubleArrayNEON(double *aOut, float32x4_t fVec){
  float64x2_t lo = vcvt_f64_f32(vget_low_f32(fVec));
  float64x2_t hi = vcvt_high_f64_f32(fVec);
  float64x2_t lo1 = vld1q_f64(&aOut[0]);
  float64x2_t hi1 = vld1q_f64(&aOut[2]);
  vst1q_f64(&aOut[0], vaddq_f64(lo, lo1));
  vst1q_f64(&aOut[2], vaddq_f64(hi, hi1));
}
#endif

/*
** The number of vectors handled by each pass in vec1CovarianceWork().
*/ 
#define VEC1_COVAR_NVEC (VEC1_MULTIMATCH_NVEC * VEC1_COVAR_MULTIMATCH_MULT)

/*
** For each vv in (0..VEC1_COVAR_NVEC-1), and each ii in
** (0..nElem-1), do the following:
**
**   aOut[ii] = aOut[ii] + SUM(aScalar[vv] * aIn[vv][ii])
*/
static void vec1CovarianceAccumulateN(
  double *aOut,
  const float *aIn[VEC1_COVAR_NVEC],
  const float aScalar[VEC1_COVAR_NVEC],
  int nElem
){
  int ii = 0;
  int vv;

#ifdef VEC1_HAVE_AVX2
#define VEC1_COVAR_NACC 8
  __m256 vs[VEC1_COVAR_NVEC];
  for(vv=0; vv<VEC1_COVAR_NVEC; vv++){
    vs[vv] = _mm256_set1_ps(aScalar[vv]);
  }

  for(; ii<=(nElem-(8*VEC1_COVAR_NACC)); ii+=(8*VEC1_COVAR_NACC)){
    __m256 aAcc[VEC1_COVAR_NACC];
    int aa;
    for(aa=0; aa<VEC1_COVAR_NACC; aa++){
      aAcc[aa] = _mm256_set1_ps(0.0f);
    }
    for(vv=0; vv<VEC1_COVAR_NVEC; vv++){
      for(aa=0; aa<VEC1_COVAR_NACC; aa++){
        __m256 vf = LOADU(&aIn[vv][ii+8*aa]);
        aAcc[aa] = FMADD(vs[vv], vf, aAcc[aa]);
      }
    }
    for(aa=0; aa<VEC1_COVAR_NACC; aa++){
      vec1AddToDoubleArrayAVX2(&aOut[ii+8*aa], aAcc[aa]);
    }
  }

  for(; ii<=(nElem-8); ii+=8){
    __m256 acc = _mm256_set1_ps(0.0f);
    for(vv=0; vv<VEC1_COVAR_NVEC; vv++){
      __m256 vf = LOADU(&aIn[vv][ii]);
      acc = FMADD(vs[vv], vf, acc);
    }
    vec1AddToDoubleArrayAVX2(&aOut[ii], acc);
  }
#endif /* VEC1_HAVE_AVX2 */
#ifdef VEC1_HAVE_NEON
/* Chosen so that VEC1_COVAR_NACC_NEON + VEC1_COVAR_NVEC (the vs[] array)
** stays comfortably within the 24 caller-saved NEON vector registers,
** to avoid spilling vs[] to the stack inside the hot loop. */
#define VEC1_COVAR_NACC_NEON (16/VEC1_COVAR_MULTIMATCH_MULT)
  float32x4_t vs[VEC1_COVAR_NVEC];
  for(vv=0; vv<VEC1_COVAR_NVEC; vv++){
    vs[vv] = vdupq_n_f32(aScalar[vv]);
  }

  for(; ii<=(nElem-(4*VEC1_COVAR_NACC_NEON)); ii+=(4*VEC1_COVAR_NACC_NEON)){
    float32x4_t aAcc[VEC1_COVAR_NACC_NEON];
    int aa;
    for(aa=0; aa<VEC1_COVAR_NACC_NEON; aa++){
      aAcc[aa] = vdupq_n_f32(0.0f);
    }
    for(vv=0; vv<VEC1_COVAR_NVEC; vv++){
      for(aa=0; aa<VEC1_COVAR_NACC_NEON; aa++){
        float32x4_t vf = vld1q_f32(&aIn[vv][ii+4*aa]);
        aAcc[aa] = vfmaq_f32(aAcc[aa], vs[vv], vf);
      }
    }
    for(aa=0; aa<VEC1_COVAR_NACC_NEON; aa++){
      vec1AddToDoubleArrayNEON(&aOut[ii+4*aa], aAcc[aa]);
    }
  }

  for(; ii<=(nElem-4); ii+=4){
    float32x4_t acc = vdupq_n_f32(0.0f);
    for(vv=0; vv<VEC1_COVAR_NVEC; vv++){
      float32x4_t vf = vld1q_f32(&aIn[vv][ii]);
      acc = vfmaq_f32(acc, vs[vv], vf);
    }
    vec1AddToDoubleArrayNEON(&aOut[ii], acc);
  }
#endif /* VEC1_HAVE_NEON */

  for(; ii<nElem; ii++){
    double acc = aOut[ii];
    for(vv=0; vv<VEC1_COVAR_NVEC; vv++){
      acc += (double)aScalar[vv] * aIn[vv][ii];
    }
    aOut[ii] = acc;
  }
}

static void vec1CovarianceWork(void *pArg){
  Vec1CovarianceJob *pJob = (Vec1CovarianceJob*)pArg;
  const int iSubOff = pJob->iSub*pJob->nCodeElem;
  float *aCode = &pJob->aBook[iSubOff * VEC1_PQ_CODEBOOK_SZ];
  int ii, kk;

  int nVec = pJob->pVec->nVec;
  int nElem = pJob->pVec->nElem;
  int nKK = MIN(pJob->nCodeElem, nElem - iSubOff);

#if defined(VEC1_USE_TRANSPOSITION)
  /* Populate this job's transposed copy of its slice of the codebook. */
  vec1CodebookTranspose(
      aCode, pJob->nCodeElem, VEC1_NTRANSPOSE_WIDTH, VEC1_PQ_CODEBOOK_SZ,
      pJob->aTrans
  );
#endif

  /* Loop through the training vectors that are our responsibility,
  ** VEC1_COVAR_NVEC at a time. Each iteration of this outer loop
  ** compares VEC1_COVAR_NVEC sub-vectors to all candidate codebook
  ** entries (VEC1_MULTIMATCH_NVEC at a time, as that is the width the
  ** best-match routines operate on), then updates pJob->aM[] with the
  ** results.  */
  ii = 0;
  for(; ii<=(nVec-VEC1_COVAR_NVEC); ii+=VEC1_COVAR_NVEC){
    int vv, iBatch;
    const float *aVec[VEC1_COVAR_NVEC];
    const float *aSub[VEC1_COVAR_NVEC];
    int aBest[VEC1_COVAR_NVEC];
    const float *aSubHat[VEC1_COVAR_NVEC];

    for(vv=0; vv<VEC1_COVAR_NVEC; vv++){
      aVec[vv] = vec1TrainingVector(pJob->pVec, ii+vv);
      aSub[vv] = &aVec[vv][iSubOff];
    }

    for(iBatch=0; iBatch<VEC1_COVAR_MULTIMATCH_MULT; iBatch++){
      int off = iBatch*VEC1_MULTIMATCH_NVEC;
      vec1BestMatchPlatformN(
          pJob->nCodeElem, aCode, pJob->aTrans, VEC1_PQ_CODEBOOK_SZ,
          &aSub[off], &aBest[off], &pJob->fTotalDist
      );
    }

    for(vv=0; vv<VEC1_COVAR_NVEC; vv++){
      aSubHat[vv] = &aCode[aBest[vv] * pJob->nCodeElem];
    }

    /* For each row of pJob->aM[], gather the VEC1_COVAR_NVEC
    ** scalars that apply to it and stream through the whole row once,
    ** rather than once per training vector. */
    for(kk=0; kk<nKK; kk++){
      float aScalar[VEC1_COVAR_NVEC];
      for(vv=0; vv<VEC1_COVAR_NVEC; vv++){
        aScalar[vv] = aSubHat[vv][kk];
      }
      vec1CovarianceAccumulateN(
          &pJob->aM[(kk+iSubOff) * nElem], aVec, aScalar, nElem
      );
    }
  }

  /* Scalar tail - handle training vectors not covered by the batched
  ** loop above because nVec is not a multiple of VEC1_COVAR_NVEC. */
  for(; ii<nVec; ii++){
    float *aVec = vec1TrainingVector(pJob->pVec, ii);
    const float *aSub = &aVec[iSubOff];
    int iBest = vec1PqBestMatch(
        aCode, VEC1_PQ_CODEBOOK_SZ, aSub, pJob->nCodeElem, &pJob->fTotalDist
    );
    float *aSubHat = &aCode[iBest * pJob->nCodeElem];

    for(kk=0; kk<nKK; kk++){
      float subhat_kk = aSubHat[kk];
      double *aMCol = &pJob->aM[(kk+iSubOff) * nElem];
      int iVec;
      for(iVec=0; iVec<nElem; iVec++){
        aMCol[iVec] += subhat_kk * aVec[iVec];
      }
    }
  }
}

/*
** aVecTrain:
**   Packed array of p->nVec vectors, each p->nElem elements each.
*/
static void vec1PqFindRotation(
  Vec1TrainCtx *p, 
  Vec1JobQueue *pQueue,
  Vec1TrainVectors *pTrain,       /* Rotated vectors to train on */
  float *aBook,                   /* Current codebook */
  float *aRotation                /* OUT: best rotation */
){
  if( p->rc==SQLITE_OK ){
    double *aM = 0;
    double *aU = 0;
    double *aVT = 0;
    double *aS = 0;
    double *aW = 0;
    int ii;
    int nElem2 = (p->tv.nElem * p->tv.nElem);
  
    Vec1CovarianceJob *aJob = 0;
    int nJob = 0;
  
    /* Allocate space for 4 nElem*nElem matrices - W, M, U and VT. And one
    ** nElem vector - S. */
    aM = (double*)vec1MallocZero(
        4 * nElem2 * sizeof(double) +
        p->tv.nElem * sizeof(double)
    );
    if( aM==0 ){
      p->rc = SQLITE_NOMEM;
      sqlite3_result_error_nomem(p->pCtx);
      return;
    }
    aVT = &aM[nElem2];
    aU = &aVT[nElem2];
    aW = &aU[nElem2];
    aS = &aW[nElem2];
  
    /* Step 1. Calculate the cross-covariance matrix M between training set X 
    ** and the quantized version of the training set X^. i.e. the sum of the 
    ** following for all x in X:
    **
    **     xT . x^
    **
    **   (x1)                        (x1*x1^, x1*x2^, x1*x3^)
    **   (x2) . (x1^, x2^, x3^)  ==  (x2*x1^, x2*x2^, x2*x3^)
    **   (x3)                        (x3*x1^, x3*x2^, x3*x3^)
    **
    ** Because the covariance matrix is going to LAPACK, it has to be in
    ** column-major order. So aM[1]==(x2*x1^).
    */
    START_TRAINING_TIMER(p, VEC1_TRAINING_ROT_COVARIANCE);
    aJob = vec1CovarianceAlloc(p, aM, pTrain, aBook, &nJob);
    if( aJob ){
      for(ii=0; ii<nJob; ii++){
        vec1JobQueueAddJob(pQueue, vec1CovarianceWork, 0, &aJob[ii]);
      }
      vec1JobQueueFinishJobs(pQueue, SQLITE_OK);
      sqlite3_free(aJob);
    }
    END_TRAINING_TIMER(p, VEC1_TRAINING_ROT_COVARIANCE);
  
    START_TRAINING_TIMER(p, VEC1_TRAINING_ROT_JACOBI);
    memcpy(aW, aM, sizeof(double)*nElem2);
    vec1JacobiSVD(p, pQueue, aW, p->tv.nElem, aU, aS, aVT, 50, 1e-14);
    END_TRAINING_TIMER(p, VEC1_TRAINING_ROT_JACOBI);
    vec1MatrixMultiply(aRotation, aU, aVT, p->tv.nElem);
  
    if( p->rc==SQLITE_OK && p->bSvdVerify ){
      p->rc = vec1SVDSelfTest(p->pCtx, aM, aU, aS, aVT, p->tv.nElem);
    }
    sqlite3_free(aM);
  }
}

typedef struct Vec1RotationJob Vec1RotationJob;
struct Vec1RotationJob {

  Vec1TrainVectors *pVec;         /* Input vectors */
  Vec1TrainVectors *pOut;         /* Output vectors */

  int iFirst;
  int iEof;

  const float *aRotation;         /* nElem*nElem rotation matrix */
#ifdef VEC1_USE_TRANSPOSITION
  const float *aRotationNT;       /* Transposed rotation matrix */
#endif
};

static Vec1RotationJob *vec1RotationAlloc(
  Vec1TrainCtx *p,
  const float *aRotation,
  const float *aRotationNT,
  Vec1TrainVectors *pOut          /* Store rotated vectors here */
){
  int nByte = p->nThread * sizeof(Vec1RotationJob);
  Vec1RotationJob *aJob = vec1MallocZero(nByte);
  if( aJob ){
    int ii;
    for(ii=0; ii<p->nThread; ii++){
      Vec1RotationJob *pJob = &aJob[ii];
      ASSIGN_VECTORS_TO_JOB(&p->tv, pJob, ii, p->nThread);
      pJob->aRotation = aRotation;
      pJob->pOut = pOut;
#ifdef VEC1_USE_TRANSPOSITION
      pJob->aRotationNT = aRotationNT;
#else
      (void)aRotationNT;
#endif
    }

  }
  return aJob;
}

#if defined(VEC1_USE_TRANSPOSITION)
static void vec1RotateVectorTransposeN(
  float *aOut[VEC1_MULTIMATCH_NVEC],
  const float *aIn[VEC1_MULTIMATCH_NVEC],
  int nElem,
  const float *aRotation,
  const float *aRotationNT
){
  int vv;
  int d;
  int ii;

  /* ii is the first output element in the VEC1_MULTIMATCH_NVEC output
  ** vectors. Each iteration of this outer loop populates 8 elements
  ** of each of them  */
  for(ii=0; ii<=(nElem-8); ii+=8){
    const float *aNT = &aRotationNT[(ii * nElem)];
    __m256 aAcc[VEC1_MULTIMATCH_NVEC];

    for(vv=0; vv<VEC1_MULTIMATCH_NVEC; vv++){
      aAcc[vv] = _mm256_set1_ps(0.0f);
    }

    for(d=0; d<nElem; d++){
      __m256 vec = LOADU( &aNT[ d * 8 ] );
      for(vv=0; vv<VEC1_MULTIMATCH_NVEC; vv++){
        aAcc[vv] = FMADD(vec, _mm256_set1_ps(aIn[vv][d]), aAcc[vv] );
      }
    }

    for(vv=0; vv<VEC1_MULTIMATCH_NVEC; vv++){
      _mm256_storeu_ps(&aOut[vv][ii], aAcc[vv]);
    }
  }

  for( ; ii<nElem; ii++){
    for(vv=0; vv<VEC1_MULTIMATCH_NVEC; vv++){
      aOut[vv][ii] = vec1DotProduct(nElem, aIn[vv], &aRotation[ii*nElem]);
    }
  }
}
#else

static void vec1RotateVectorN(
  float *aOut[VEC1_MULTIMATCH_NVEC],
  const float *aIn[VEC1_MULTIMATCH_NVEC],
  int nElem,
  const float *aRotation
){
  int vv;
  int d = 0;

#if defined(VEC1_HAVE_NEON)
  /* Each iteration of this outer loop calculates values d..(d+3) for each 
  ** of the VEC1_MULTIMATCH_NVEC vectors. */
  for(; d<=(nElem-4); d+=4){
    /* Accumulator aAcc[x][y] accumulates values such that its horizontal
    ** sum is the dot product of input vector aIn[x] and row (d+y) of 
    ** the rotation matrix.  */
    const float *aRow[4];
    int x, y, d2;
    float32x4_t aAcc[VEC1_MULTIMATCH_NVEC][4];

    for(y=0; y<4; y++) aRow[y] = &aRotation[(d+y)*nElem];

    /* Zero all accumulators to start with. */
    for(x=0; x<VEC1_MULTIMATCH_NVEC; x++){
      for(y=0; y<4; y++) aAcc[x][y] = vdupq_n_f32(0.0f);
    }

    for(d2=0; d2<=(nElem-4); d2+=4){
      float32x4_t aRot[4];
      for(y=0; y<4; y++){
        aRot[y] = vld1q_f32(&aRow[y][d2]);
      }

      for(x=0; x<VEC1_MULTIMATCH_NVEC; x++){
        float32x4_t vec = vld1q_f32(&aIn[x][d2]);
        for(y=0; y<4; y++){
          aAcc[x][y] = vfmaq_f32(aAcc[x][y], vec, aRot[y]);
        }
      }
    }

    for(x=0; x<VEC1_MULTIMATCH_NVEC; x++){
      /* Reduce the 4 accumulators for vector x to a single register
      ** containing output values d..(d+3). Then add in the scalar tail,
      ** if any. This is the same order of operations as vec1DotProduct(),
      ** so results are identical to those of vec1RotateVector().  */
      float32x4_t res = vpaddq_f32(
          vpaddq_f32(aAcc[x][0], aAcc[x][1]),
          vpaddq_f32(aAcc[x][2], aAcc[x][3])
      );
      if( d2<nElem ){
        float aTmp[4];
        int k;
        vst1q_f32(aTmp, res);
        for(k=d2; k<nElem; k++){
          for(y=0; y<4; y++) aTmp[y] += aIn[x][k] * aRow[y][k];
        }
        res = vld1q_f32(aTmp);
      }
      vst1q_f32(&aOut[x][d], res);
    }
  }
#endif
  
  for(; d<nElem; d++){
    for(vv=0; vv<VEC1_MULTIMATCH_NVEC; vv++){
      aOut[vv][d] = vec1DotProduct(nElem, aIn[vv], &aRotation[d*nElem]);
    }
  }
}
#endif


static void vec1RotationWork(void *pArg){
  Vec1RotationJob *pJob = (Vec1RotationJob*)pArg;
  int nElem = pJob->pVec->nElem;

  int ii = pJob->iFirst;
  for(; ii<=(pJob->iEof-VEC1_MULTIMATCH_NVEC); ii+=VEC1_MULTIMATCH_NVEC){
    const float *aIn[VEC1_MULTIMATCH_NVEC];
    float *aOut[VEC1_MULTIMATCH_NVEC];
    int vv;
    for(vv=0; vv<VEC1_MULTIMATCH_NVEC; vv++){
      aIn[vv] = vec1TrainingVector(pJob->pVec, vv+ii);
      aOut[vv] = vec1TrainingVector(pJob->pOut, vv+ii);
    }

#ifdef VEC1_USE_TRANSPOSITION
    vec1RotateVectorTransposeN(
        aOut, aIn, nElem, pJob->aRotation, pJob->aRotationNT
    );
#else
    vec1RotateVectorN(aOut, aIn, nElem, pJob->aRotation);
#endif
  }

  for(; ii<pJob->iEof; ii++){
    const float *pIn = vec1TrainingVector(pJob->pVec, ii);
    float *pOut = vec1TrainingVector(pJob->pOut, ii);
    vec1RotateVector(nElem, pJob->aRotation, pIn, pOut);
  }
}

/*
** Create and return a deep copy of the training vectors passed as the
** only argument. 
*/
static Vec1TrainVectors *vec1CopyVectors(Vec1TrainVectors *pIn){
  Vec1TrainVectors *pRet = 0;
  int ii;

  pRet = (Vec1TrainVectors*)vec1MallocZero(sizeof(Vec1TrainVectors));
  if( pRet==0 ) return pRet;
  pRet->nElem = pIn->nElem;
  pRet->nVec = pIn->nVec;
  pRet->nChunk = pIn->nChunk;
  pRet->nVecPerChunk = pIn->nVecPerChunk;

  for(ii=0; ii<pIn->nChunk; ii++){
    int nByte = pRet->nVecPerChunk * pIn->nElem * sizeof_f32;
    pRet->aChunk[ii] = (float*)sqlite3_malloc(VEC1_TRAINING_SZCHUNK);
    if( pRet->aChunk[ii]==0 ){
      vec1FreeVectors(pRet);
      return 0;
    }
    if( ii==pIn->nChunk-1 ){
      nByte = (pRet->nVec - ii*pRet->nVecPerChunk) * pRet->nElem*sizeof_f32;
    }
    assert( nByte<=VEC1_TRAINING_SZCHUNK );
    memcpy(pRet->aChunk[ii], pIn->aChunk[ii], nByte);
  }

  return pRet;
}

static void vec1TrainRotation(
  Vec1TrainCtx *p,
  Vec1JobQueue *pQueue,
  float *aBook,
  float *aRotation
){
  const int nElem = p->tv.nElem;
  const int nMatrixByte = sizeof_f32 * nElem * nElem;
  int iRound = 0;                 /* Current OPQ round */
  int ii;
  float *aRot = (float*)sqlite3_malloc(nMatrixByte * 2);
  float *aRot2 = &aRot[nElem*nElem];
  Vec1TrainVectors *pTrain = vec1CopyVectors(&p->tv);
  Vec1RotationJob *aJob = 0;
  aJob = vec1RotationAlloc(p, aRotation, aRot2, pTrain);

  if( pTrain==0 || aRot==0 || aJob==0 ){
    sqlite3_result_error_nomem(p->pCtx);
    p->rc = SQLITE_NOMEM;
  }

  for(iRound=0; iRound<p->nOpqRound; iRound++){
    vec1TrainLog(p, "OPQ round %d/%d", iRound+1, p->nOpqRound);
    START_TRAINING_TIMER(p, VEC1_TRAINING_ROT_CODEBOOKS);
    vec1TrainPQCodebooks(p, pTrain, pQueue, aBook);
    END_TRAINING_TIMER(p, VEC1_TRAINING_ROT_CODEBOOKS);

    vec1PqFindRotation(p, pQueue, pTrain, aBook, aRot);

    if( p->rc==SQLITE_OK ){
      if( iRound==0 ){
        memcpy(aRotation, aRot, nMatrixByte);
      }else{
        memcpy(aRot2, aRotation, nMatrixByte);
        vec1MatrixMultiplyF(aRotation, aRot2, aRot, nElem);
      }

      START_TRAINING_TIMER(p, VEC1_TRAINING_ROT_VECTORS);
#ifdef VEC1_USE_TRANSPOSITION
      vec1CodebookTranspose(aRotation, 
          nElem, VEC1_NTRANSPOSE_WIDTH, nElem, aRot2
      );
#endif
      for(ii=0; ii<p->nThread; ii++){
        vec1JobQueueAddJob(pQueue, vec1RotationWork, 0, &aJob[ii]);
      }
      vec1JobQueueFinishJobs(pQueue, SQLITE_OK);
      END_TRAINING_TIMER(p, VEC1_TRAINING_ROT_VECTORS);
    }
    p->nCompletedWork += VEC1_TRAINING_WORK_OPQ;
  }

  if( p->rc==SQLITE_OK ){
    for(ii=0; ii<p->tv.nChunk; ii++){
      SWAP(float*, p->tv.aChunk[ii], pTrain->aChunk[ii]);
    } 
  }
  sqlite3_free(aJob);
  sqlite3_free(aRot);
  vec1FreeVectors(pTrain);
}

static void vec1TrainLogTimes(Vec1TrainCtx *p){
  double fTotal = (double)p->aTime[VEC1_TRAINING_TOTAL];
  if( p->zProfileFunction ){
    struct Counter {
      const char *zName;
      int iTime;
    } aCounter[] = {
      { "rot_codebooks", VEC1_TRAINING_ROT_CODEBOOKS },
      { "rot_covariance", VEC1_TRAINING_ROT_COVARIANCE },
      { "rot_jacobi", VEC1_TRAINING_ROT_JACOBI },
      { "rot_vectors", VEC1_TRAINING_ROT_VECTORS },
      { "coarse_init", VEC1_TRAINING_COARSE_INIT },
      { "coarse_lloyds", VEC1_TRAINING_COARSE_KMEANS },
      { "residuals", VEC1_TRAINING_RESIDUALS },
      { "codebooks", VEC1_TRAINING_CODEBOOKS }
    };
    int icall;
    for(icall=0; icall<2; icall++){
      int ii;
      char *zSql = 0;

      sqlite3_str *pSql = sqlite3_str_new(0);
      sqlite3_str_appendf(pSql, "SELECT \"%w\"('{", p->zProfileFunction);
      if( icall==0 ){
        for(ii=0; ii<size_of_array(aCounter); ii++){
          sqlite3_str_appendf(pSql, "%s\"%s\": %.2f", ((ii>0) ? ", " : ""),
              aCounter[ii].zName, p->aTime[ aCounter[ii].iTime ]*100.00 / fTotal
          );
        }
      }else{
        for(ii=0; ii<size_of_array(aCounter); ii++){
          u64 val = p->aTime[ aCounter[ii].iTime ];
          sqlite3_str_appendf(pSql, "%s\"%s\": ", 
              ((ii>0) ? ", " : ""), aCounter[ii].zName
              );
          if( val>(1000*1000*1000) ){
            sqlite3_str_appendf(pSql, "\"%.2fG\"", (double)val / 1000000000.0);
          }else if( val>(10*1000*1000) ){
            sqlite3_str_appendf(pSql, "\"%dM\"", (int)(val / (1000*1000)));
          }else if( val>(1*1000*1000) ){
            sqlite3_str_appendf(pSql, "\"%.2fM\"", (double)val / 1000000.0);
          }else if( val>(10*1000) ){
            sqlite3_str_appendf(pSql, "\"%dK\"", (int)(val / (1000)));
          }else if( val>(1000) ){
            sqlite3_str_appendf(pSql, "\"%.2fK\"", (double)val / 1000.0);
          }else{
            sqlite3_str_appendf(pSql, "\"%d\"", (int)val);
          }
        }
      }

      sqlite3_str_appendf(pSql, "}')");
      zSql = sqlite3_str_finish(pSql);

      if( zSql==0 ){
        sqlite3_result_error_nomem(p->pCtx);
        break;
      }else{
        sqlite3 *db = sqlite3_context_db_handle(p->pCtx);
        char *zErrmsg = 0;
        int rc = sqlite3_exec(db, zSql, 0, 0, &zErrmsg);
        if( rc!=SQLITE_OK ){
          sqlite3_result_error(p->pCtx, zErrmsg, -1);
          sqlite3_result_error_code(p->pCtx, rc);
          break;
        }
        sqlite3_free(zErrmsg);
        sqlite3_free(zSql);
      }
    }
  }
}

typedef struct Vec1ResidualJob Vec1ResidualJob;
struct Vec1ResidualJob {
  /* Data to train on */
  const Vec1TrainCtx *p;
  Vec1TrainVectors *pVec;
  int iFirst;
  int iEof;
  const float *aCentroid;
  int nCentroid;
};

static void vec1TrainResidualWorker(void *pCtx){
  Vec1ResidualJob *p = (Vec1ResidualJob*)pCtx;
  int ii;
  int nElem = p->pVec->nElem;

  for(ii=p->iFirst; ii<p->iEof; ii++){
    float *a = vec1TrainingVector(p->pVec, ii);
    int iBest = vec1PqBestMatch(p->aCentroid, p->nCentroid, a, nElem, 0);
    vec1SubInPlace(a, &p->aCentroid[iBest * nElem], nElem);
  }
}

static void vec1TrainFindResiduals(
  Vec1TrainCtx *p, 
  Vec1JobQueue *pQueue,
  const float *aCentroid
){
  if( p->rc==SQLITE_OK ){
    int ii;
    int nJob = p->nThread;
    Vec1ResidualJob *aJob = 0;
    int rc = SQLITE_OK;

    assert( nJob>=1 );
    aJob = (Vec1ResidualJob*)vec1MallocZero(sizeof(Vec1ResidualJob) * nJob);
    if( aJob==0 ){
      sqlite3_result_error_nomem(p->pCtx);
      p->rc = SQLITE_NOMEM;
      return;
    }

    for(ii=0; ii<nJob; ii++){
      Vec1ResidualJob *pJob = &aJob[ii];
      ASSIGN_VECTORS_TO_JOB(&p->tv, pJob, ii, nJob);
      pJob->p = p;
      pJob->aCentroid = aCentroid;
      pJob->nCentroid = p->nBucket;
      vec1JobQueueAddJob(pQueue, vec1TrainResidualWorker, 0, (void*)pJob);
    }

    vec1JobQueueFinishJobs(pQueue, rc);
    sqlite3_free(aJob);
  }
}

static void vec1TrainPrepareLog(Vec1TrainCtx *p){
  if( p->zLogFunction ){
    char *zSql = sqlite3_mprintf("SELECT %s(?, ?)", p->zLogFunction);
    if( zSql==0 ){
      sqlite3_result_error_nomem(p->pCtx);
      p->rc = SQLITE_NOMEM;
    }else{
      sqlite3 *db = sqlite3_context_db_handle(p->pCtx);
      p->rc = sqlite3_prepare_v2(db, zSql, -1, &p->pLogStmt, 0);
      sqlite3_free(zSql);
      if( p->rc==SQLITE_ERROR ){
        sqlite3_result_error(p->pCtx, sqlite3_errmsg(db), -1);
      }else if( p->rc!=SQLITE_OK ){
        sqlite3_result_error_code(p->pCtx, p->rc);
      }
    }
  }
}

static void vec1TrainWorkInit(Vec1TrainCtx *p){
  int nTotal = 0;

  if( p->eQuant==VEC1_QUANTIZE_OPQ ){
    nTotal += VEC1_TRAINING_WORK_OPQ * p->nOpqRound;
  }
  if( p->nBucket>0 ){
    nTotal += VEC1_TRAINING_WORK_COARSE1 + VEC1_TRAINING_WORK_COARSE2;
  }
  if( p->eQuant==VEC1_QUANTIZE_PQ || p->eQuant==VEC1_QUANTIZE_OPQ ){
    nTotal += VEC1_TRAINING_WORK_CODEBOOKS;
  }

  p->nTotalWork = (nTotal ? nTotal : 1);
}

/*
** Return true if n is a power of 2, or false otherwise.
*/
static int vec1IsPow2(int n) { 
  return n > 0 && (n & (n-1)) == 0; 
}

/*
** Apply Walsh-Hadamard Transform to n element vector x. n must
** be a power of 2.
*/
static void vec1WHT(float *x, int n){
  assert( vec1IsPow2(n) );
  for (int stride = 1; stride < n; stride <<= 1) {
    for (int i = 0; i < n; i += stride << 1) {
      for (int j = i; j < i + stride; j++) {
        float a = x[j];
        float b = x[j + stride];
        x[j]        = a + b;
        x[j+stride] = a - b;
      }
    }
  }
}

/*
** Apply Walsh-Hadamard transform to nElem element vector aVec. 
*/
static void vec1ApplyWHT(float *aVec, int nElem){
  if (vec1IsPow2(nElem)) {
    vec1WHT(aVec, nElem);
  }else{
    int p = 1;
    while( (p<<1)<nElem ) p = p<<1;
    vec1WHT(aVec, p);
    vec1WHT(aVec + nElem - p, p);
  }
}

/*
** Apply the Walsh-Hadamard Transform to all training vectors used by
** the training context.
*/
static void vec1TrainApplyWHT(Vec1TrainCtx *p){
  int ii;
  for(ii=0; ii<p->tv.nVec; ii++){
    vec1ApplyWHT(vec1TrainingVector(&p->tv, ii), p->tv.nElem);
  }
}

/*
** The xFinal() method for the vec1_train() aggregate function.
**
** Output a trained model blob.
*/
static void vec1TrainFinal(sqlite3_context *pCtx){
  Vec1TrainCtx *p = 0;
  float *aBook = 0;               /* Code books */
  float *aCentroid = 0;           /* Centroids */
  float *aRotation = 0;           /* Rotation */

  int nByte = 0;
  u8 *aByte = 0;

  float *pCsr = 0;
  Vec1JobQueue *pQueue = 0;
  u32 flags = 0;
  int ii;

  int bPQ = 0;

  /* Check that sufficient training vectors were provided. */
  p = (Vec1TrainCtx*)sqlite3_aggregate_context(pCtx, sizeof(*p));
  if( p==0 ) return;
  p->pCtx = pCtx;
  bPQ = (p->eQuant==VEC1_QUANTIZE_PQ || p->eQuant==VEC1_QUANTIZE_OPQ);

  if( p->tv.nVec==0 ){
    vec1ResultErrorF(pCtx,
        "too few training vectors (have 0, require more than that)"
    );
    goto train_final_out;
  }
  if( p->nBucket || bPQ ){
    int nReq = p->nBucket * 4;
    if( bPQ ) nReq = MAX(VEC1_PQ_TRAINING_SET_MIN, nReq);
    if( p->tv.nVec<nReq ){
      vec1ResultErrorF(pCtx,
          "too few training vectors (require %d, have %d)",
          nReq, p->tv.nVec
      );
      goto train_final_out;
    }
  }

  vec1TrainWorkInit(p);
  vec1TrainPrepareLog(p);
  vec1TrainLog(p, "vectors loaded, starting training");
  if( p->rc!=SQLITE_OK ) goto train_final_out;

  START_TRAINING_TIMER(p, VEC1_TRAINING_TOTAL);

  /* If this is training for "cos" distance, not "l2", normalize all 
  ** training vectors before proceding as usual. */
  if( p->eDistance==VEC1_DISTANCE_COS ){
    vec1TrainNormalizeAll(p);
  }

  /* Allocate space for the model. Space is required for (A) the header and
  ** coarse centroids, (B) the PQ codebooks and (C) the OPQ rotation
  ** matrix.  */
  nByte = VEC1_HEADER_SIZE + (sizeof_f32 * p->nBucket * p->tv.nElem);  /* A */
  if( bPQ ){
    nByte += sizeof_f32*p->nCodesize*p->nCodeElem*VEC1_PQ_CODEBOOK_SZ; /* B */
  }
  if( p->eQuant==VEC1_QUANTIZE_OPQ ){
    nByte += sizeof_f32 * p->tv.nElem * p->tv.nElem;                   /* C */
  }

  aByte = (u8*)vec1ContextMalloc(pCtx, nByte);
  if( aByte==0 ) goto train_final_out;
  pCsr = (float*)&aByte[VEC1_HEADER_SIZE];
  if( bPQ ){
    aBook = pCsr;
    pCsr += (p->nCodesize * p->nCodeElem * VEC1_PQ_CODEBOOK_SZ);
  }
  if( p->nBucket>0 ){
    aCentroid = pCsr;
    pCsr += (p->nBucket * p->tv.nElem);
  }
  if( p->eQuant==VEC1_QUANTIZE_OPQ ){
    aRotation = pCsr;
    pCsr += (p->tv.nElem * p->tv.nElem);
  }
  assert( (u8*)pCsr==&aByte[nByte] );

  /* Allocate a jobs queue, if multi-threading is required. If it is not
  ** required, leave pQueue set to NULL. Things will proceed in a single
  ** thread in that case.  */
#if VEC1_THREADS
  if( p->nThread>1 ){
    pQueue = vec1JobQueueNew(p->nThread-1);
    if( pQueue==0 ){
      sqlite3_result_error_nomem(pCtx);
      goto train_final_out;
    }
  }
#endif

  /* Write the model header into the output buffer. */
  flags = VEC1_MODEL_INDEX 
        | ((p->nBucket>0 && p->bResidual) ? VEC1_MODEL_RESIDUAL : 0)
        | ((p->eQuant==VEC1_QUANTIZE_OPQ) ? VEC1_MODEL_ROTATE : 0)
        | (p->bWHT ? VEC1_MODEL_WHT : 0)
        | ((p->eQuant==VEC1_QUANTIZE_BQ) ? VEC1_MODEL_BITQUANT : 0);
  vec1HeaderWrite(aByte, flags, 
      p->tv.nElem, p->nCodesize, p->nBucket, p->eDistance
  );

  /* If an OPQ rotation is require, calculate one now */
  if( p->eQuant==VEC1_QUANTIZE_OPQ ){
    vec1TrainRotation(p, pQueue, aBook, aRotation);
  }else if( p->bWHT ){
    vec1TrainApplyWHT(p);
  }

  if( p->nBucket>0 ){
    vec1TrainCoarseQuant(p, pQueue, aCentroid);

    /* For each training vector, find the closest centroid. Then subtract
    ** the centroid from the training vector, to leave the residual vector.
    ** This is what the PQ step trains on. */
    if( bPQ && p->bResidual ){
      vec1TrainLog(p, "calculating residuals");
      START_TRAINING_TIMER(p, VEC1_TRAINING_RESIDUALS);
      vec1TrainFindResiduals(p, pQueue, aCentroid);
      END_TRAINING_TIMER(p, VEC1_TRAINING_RESIDUALS);
    }
  }

  /* Now train the PQ codebooks, if required. Each codebook is trained in
  ** parallel, using a separate thread if threads were configured. */
  if( bPQ ){
    vec1TrainLog(p, "PQ codebook training");
    START_TRAINING_TIMER(p, VEC1_TRAINING_CODEBOOKS);
    vec1TrainPQCodebooks(p, &p->tv, pQueue, aBook);
    p->nCompletedWork += VEC1_TRAINING_WORK_CODEBOOKS;
    END_TRAINING_TIMER(p, VEC1_TRAINING_CODEBOOKS);
  }

  /* Return the result blob. */
  if( p->rc==SQLITE_OK ){
    sqlite3_result_blob(pCtx, (void*)aByte, nByte, vec1SqliteFree);
    aByte = 0;
  }

 train_final_out: 
  END_TRAINING_TIMER(p, VEC1_TRAINING_TOTAL);
  vec1TrainLog(p, "training complete");
  vec1TrainLogTimes(p);
  sqlite3_finalize(p->pLogStmt);
  sqlite3_free(p->zLogFunction);
  sqlite3_free(p->zProfileFunction);
  for(ii=0; ii<p->tv.nChunk; ii++){
    sqlite3_free(p->tv.aChunk[ii]);
  }

  vec1JobQueueFree(pQueue);
  sqlite3_free(aByte);
}


typedef struct Vec1Model Vec1Model;
struct Vec1Model {
  Vec1ModelHeader hdr;
  const float *aModel;
  const float *aCentroid;
  const float *aRotation;

  int nCodeElem;
  float *aModelT;

#ifdef VEC1_USE_TRANSPOSITION
  float *aCentroidNT;
  float *aModelNT;
  float *aRotationNT;
#endif
};

static int vec1DecodeModel(
  const u8 *aBlob,
  int nByte,
  Vec1Model *pMod,
  char **pzErr
){
  const u8 *pCsr = aBlob;
  int nCodebook = 0;

  if( nByte<VEC1_HEADER_SIZE ){
    *pzErr = sqlite3_mprintf("vec1: model too small - %d bytes", nByte);
    return SQLITE_ERROR;
  }
  vec1HeaderRead(aBlob, &pMod->hdr);
  pCsr += VEC1_HEADER_SIZE;

  assert( VEC1_HEADER_VERSION_BIN>VEC1_HEADER_VERSION );
  if( pMod->hdr.iVersion!=VEC1_HEADER_VERSION 
   && pMod->hdr.iVersion!=VEC1_HEADER_VERSION_BIN
  ){
    if( pMod->hdr.iVersion<VEC1_HEADER_VERSION_BIN ){
      *pzErr = sqlite3_mprintf("vec1: unrecognized model version");
    }else{
      *pzErr = sqlite3_mprintf(
          "vec1: unsupported model version - require vec1 %d.%d",
          (pMod->hdr.iVersion / 1000), (pMod->hdr.iVersion % 1000)
      );
    }
    return SQLITE_ERROR;
  }

  /* Check header fields are within the acceptable ranges. This also serves
  ** to ensure that the 32-bit arithmetic used to calculate offsets below
  ** does not overflow.  */
  if( pMod->hdr.nElem>VEC1_VECSIZE_MAX 
   || pMod->hdr.nBucket>VEC1_MAX_BUCKET 
   || pMod->hdr.nCodebook>pMod->hdr.nElem
  ){
    *pzErr = sqlite3_mprintf("vec1: model is corrupt");
    return SQLITE_ERROR;
  }

  nCodebook = pMod->hdr.nCodebook;
  if( nCodebook>0 ){
    if( (pMod->hdr.flags & VEC1_MODEL_BITQUANT)==0 ){
      pMod->nCodeElem = (pMod->hdr.nElem + nCodebook-1)/nCodebook;
      pMod->aModel = (float*)pCsr;
      pCsr += (nCodebook*pMod->nCodeElem*VEC1_PQ_CODEBOOK_SZ * sizeof_f32);
    }else{
      if( ((pMod->hdr.nElem + 7)/8)==(u32)nCodebook ){
        pMod->nCodeElem = 8;
      }else if( ((pMod->hdr.nElem + 15)/16)==(u32)nCodebook ){
        pMod->nCodeElem = 16;
      }else{
        *pzErr = sqlite3_mprintf("vec1: unrecognized bq codesize");
        return SQLITE_ERROR;
      }
    }
  }
  if( pMod->hdr.nBucket>0 ){
    pMod->aCentroid = (float*)pCsr;
    pCsr += (pMod->hdr.nElem * pMod->hdr.nBucket * sizeof_f32);
  }
  if( (pMod->hdr.flags & VEC1_MODEL_ROTATE) ){
    pMod->aRotation = (float*)pCsr;
    pCsr += (pMod->hdr.nElem * pMod->hdr.nElem * sizeof_f32);
  }

  if( (pCsr - aBlob)!=nByte ){
    *pzErr = sqlite3_mprintf(
        "vec1: model size mismatch: have %d, expected %d", nByte, (pCsr-aBlob)
    );
    return SQLITE_ERROR;
  }

  return SQLITE_OK;
}

#if defined(VEC1_USE_TRANSPOSITION)
static int vec1ModelAllocTCent(Vec1Model *pMod){
  int rc = SQLITE_OK;
  if( pMod->aCentroidNT==0 && (pMod->hdr.nBucket>0 || pMod->aModel) ){
    int nElem = pMod->hdr.nElem;
    int nByte = sizeof_f32 *  nElem * pMod->hdr.nBucket;
    if( pMod->aModel ){
      int nFloatPerCodebook = pMod->nCodeElem * pMod->hdr.nCodebook;
      nByte += (VEC1_PQ_CODEBOOK_SZ * nFloatPerCodebook * sizeof_f32);
      if( pMod->aRotation ){
        nByte += (nElem * nElem) * sizeof_f32;
      }
    }
    pMod->aCentroidNT = (float*)sqlite3_malloc(nByte);
    if( pMod->aCentroidNT==0 ){
      rc = SQLITE_NOMEM;
    }else{
      assert( pMod->aCentroid || pMod->hdr.nBucket==0 );
      vec1CodebookTranspose(pMod->aCentroid, nElem,
          VEC1_NTRANSPOSE_WIDTH, pMod->hdr.nBucket, pMod->aCentroidNT
      );

      if( pMod->aModel ){
        u32 M;
        int szCodebook = pMod->nCodeElem * VEC1_PQ_CODEBOOK_SZ;
        pMod->aModelNT = &pMod->aCentroidNT[nElem * pMod->hdr.nBucket];
        for(M=0; M<pMod->hdr.nCodebook; M++){
          vec1CodebookTranspose(
              &pMod->aModel[szCodebook*M], 
              pMod->nCodeElem, VEC1_NTRANSPOSE_WIDTH, VEC1_PQ_CODEBOOK_SZ, 
              &pMod->aModelNT[szCodebook*M]
          );
        }

        if( pMod->aRotation ){
          /* Create a transposed version of the rotation matrix. */
          pMod->aRotationNT = &pMod->aModelNT[szCodebook * M];
          vec1CodebookTranspose(pMod->aRotation, 
              nElem, VEC1_NTRANSPOSE_WIDTH, nElem, pMod->aRotationNT
          );
        }
      }
    }
  }
  return rc;
}

static void vec1ModelFreeTCent(Vec1Model *pMod){
  sqlite3_free(pMod->aCentroidNT);
  pMod->aCentroidNT = 0;
  pMod->aModelNT = 0;
  pMod->aRotationNT = 0;
}
#else
# define vec1ModelAllocTCent(pMod) SQLITE_OK
# define vec1ModelFreeTCent(pMod)
#endif

/*************************************************************************
** Start of virtual table code 
*/

/* %_IDX TABLE LIST FORMAT:
**
**   Each list begins with 3 32-bit unsigned integers in big-endian format.
**   As follows:
**
**      bytes 0..3:  Flags value.
**      bytes 4..7:  Total number of entries in list.
**      bytes 8..11: Number of tombstone entries in list
**
**   Following these is an array of rowid values - either unsigned 32-bit
**   values or 64-bit signed values, depending on the flags set in the
**   first field value of the list. In the 32-bit format, the tombstone
**   value is 0xFFFFFFFF. For the 64-bit format, it is 0. Rowids are stored
**   in big-endian format.
**
** There are two formats - one that uses 32-bit rowids, and another that
** uses 64-bit rowids. Format consists of one or more blocks. Each block
** stores 16 PQ codes:
**
**   * 16 rowid values - either 16 * 32-bit unsigned or 16 * 64-bit 
**     signed. The only way to know which is to read the first/last
**     column values from the %_idx table.
**
**   * The lookup value for the first codebook for each of the 16 
**     codes in the block. Followed by the lookup value for the second
**     codebook for each of the 16 codes, and so on.
**
** A PQ code list always consists of an exact number of blocks. In the
** 32-bit rowid format, a rowid value of 0xFFFFFFFF indicates an invalid 
** entry. In the 64-bit format, an invalid entry is one with a rowid value
** of 0.
*/

/* %_META TABLE LIST FORMAT:
**
**   Meta table lists always begin with 2 32-bit unsigned integers in
**   big-endian format:
**
**       Bytes 0..3: 32-bit big-endian flags field.
**       Bytes 4..7: 32-bit big-endian nEntry field.
**
**   The flags field is set to a combination of VEC1_META_XXX flags. The
**   nEntry fields is set to the number of entries in the list. The format
**   of the rest of the list is different depending on the flags:
**
**   VEC1_META_GENERIC:
**  
**     This is the fallback format used if none of the other formats can be
**     used. It encodes each value separately as a varint type followed by
**     encoded data. Specifically, for type values 0, 1 and 2:
**  
**        0: NULL value. 0 byte payload.
**        1: Unsigned integer value in range [0..254]. 1 byte payload.
**        2: Integer value. 4 byte payload in range [-2147483647..+2147483647].
**        3: Integer value. 8 byte payload.
**        4: Real value. 8 byte payload. 
**  
**     Odd values larger than 2 are text values, with size in bytes of 
**     (type - 2)/2. Even values larger than 2 are blobs.
**  
**   VEC1_META_1BYTEINT:
**     This format may be used if all values in the list are either NULL or
**     integers between 0 and 254, inclusive. Each value is stored as
**     a 1 byte unsigned value, with 255 (0xFF) representing NULL.
**  
**   VEC1_META_4BYTEINT:
**     This format may be used if all values in the list are either NULL or
**     integers between -2147483648 and +2147483646, inclusive. Each value is
**     stored as a 4 byte two's complement value, with +2147483647 
**     (0x7FFFFFFF) representing NULL.
**
**   VEC1_META_REAL:
**     This format may be used if all values in the list are real or NULL.
**     Bit pattern 0x7FF8000000000001 is used for NULL.
**
**   Flag VEC1_META_HASNULL is set if the list contains at least one NULL
**   value.
*/
#define VEC1_META_GENERIC         0x01
#define VEC1_META_1BYTEINT        0x02
#define VEC1_META_4BYTEINT        0x04
#define VEC1_META_REAL            0x08
#define VEC1_META_HASNULL         0x10

#define VEC1_META_TYPEMASK ( \
  VEC1_META_GENERIC | VEC1_META_1BYTEINT | VEC1_META_4BYTEINT | VEC1_META_REAL \
)

#define VEC1_META_SZHDR 8
#define VEC1_META_1BYTENULL 255

#define VEC1_META_REALNULL  0x7FF8000000000001

#define VEC1_META_4BYTEMIN   (-2147483647-1)
#define VEC1_META_4BYTEMAX   +2147483646
#define VEC1_META_4BYTENULL  +2147483647

typedef struct Vec1Tab Vec1Tab;
typedef struct Vec1Csr Vec1Csr;

/*
** Flags that may be part of the first 32-bit unsigned integer of a PQ list.
*/
#define VEC1_LIST_64BIT   0x01    /* Set for 64-bit rowids, clear for 32-bit */
#define VEC1_LIST_SORTED  0x02    /* True if rowid array is in sorted order */

/* Size of list header in byte */
#define VEC1_LIST_SZHDR (3*sizeof_u32)

#define VEC1_TOMBSTONE_32    0xFFFFFFFF
#define VEC1_TOMBSTONE_64             0


/*
** Fields in the virtual table:
*/
#define VEC1_COLUMN_CMD      0
#define VEC1_COLUMN_ARG      1
#define VEC1_COLUMN_DISTANCE 2
#define VEC1_COLUMN_VECTOR   3
/* Followed by Vec1Tab.nMeta meta-data columns */

/* 
** MODEL (0):
**   This integer value is set to 1 when a model is first provided to 
**   the table, and incremented each time the model is updated thereafter.
**
** BLOCKSIZE (1):
**   The config entry for the target size (in bytes) for chunks stored in 
**   the %_idx table.
**
** FMTVERSION (5):
**   This is an integer - the current format version. Currently this is
**   always 1, but will be increased if the format changes. If a client
**   finds a format it does not understand, it has to rebuild the index.
*/

#define VEC1_CONFIG_MODEL         0
#define VEC1_CONFIG_BLOCKSIZE     1
#define VEC1_CONFIG_NELEM         2
#define VEC1_CONFIG_BLOCKSIZE_MIN 3
#define VEC1_CONFIG_FMTVERSION    5



/*
** Return the shadow-schema used by the vec1_ann table named zTab. Parameter
** zDb must be the name of the database (e.g. "main", "temp" or the name
** of an attached database).
*/
static char *vec1ShadowSchema(const char *zDb, const char *zTab, int nMeta){
  const char zFmt[] = 
    
    /* Configuration table. This contains persistent configuration parameters,
    ** including the training data. Keys must be one of the VEC1_CONFIG_XXX 
    ** values defined above.  */
    "CREATE TABLE %Q.'%q_config'(id INTEGER PRIMARY KEY, val ANY);"

    /* Base table.  This contains the raw data as provided by the user. */
    "CREATE TABLE %Q.'%q_base'("
        "id INTEGER PRIMARY KEY, "
        "vector BLOB%z"
    ");"

    /* Index data */
    "CREATE TABLE %Q.'%q_idx'("
        "id INTEGER PRIMARY KEY, "/* Id of blob */
        "bucket INTEGER, "        /* Index of bucket for this blob (or NULL) */
        "first INTEGER, "         /* Smallest rowid in blob */
        "last INTEGER, "          /* Largest rowid in blob */
        "val BLOB"                /* Blob containing rowids + PQ codes */
    ");"
    "CREATE INDEX %Q.'%q_idx_idx' ON '%q_idx'(bucket, first, last);"

    /* Training data (codebooks and centroids). This is stored as a single
    ** blob with id=1, in the same format as returned by vec1_train(). */
    "CREATE TABLE %Q.'%q_model'(id INTEGER PRIMARY KEY, val BLOB);"

    /* Meta table. Contains arrays of meta values stored in parallel with
    ** arrays in the %_idx table. For column iMeta and %_idx array id=iId,
    ** the id used in this table is ((iId<<8) + iMeta).  */
    "CREATE TABLE %Q.'%q_meta'("
        "id INTEGER PRIMARY KEY, "
        "val BLOB"
    ");"
  ;

  int ii;
  char *zMeta = 0;
  for(ii=0; ii<nMeta; ii++){
    zMeta = sqlite3_mprintf("%z, c%d", zMeta, ii);
    if( zMeta==0 ) return 0;
  }

  return sqlite3_mprintf(zFmt, 
    zDb, zTab, zDb, zTab, 
    zMeta,
    zDb, zTab, 
    zDb, zTab, zTab,
    zDb, zTab,
    zDb, zTab                     /* CREATE TABLE %_meta */
  );
}

static char *vec1DropShadowSchema(const char *zDb, const char *zTab){
  const char zFmt[] = 
    "DROP TABLE IF EXISTS %Q.'%q_config';"
    "DROP TABLE IF EXISTS %Q.'%q_base';"
    "DROP TABLE IF EXISTS %Q.'%q_idx';"
    "DROP TABLE IF EXISTS %Q.'%q_model';"
    "DROP TABLE IF EXISTS %Q.'%q_meta';"
  ;

  return sqlite3_mprintf(zFmt, 
      zDb, zTab, zDb, zTab, zDb, zTab, zDb, zTab, zDb, zTab
  );
}

typedef struct Vec1Config Vec1Config;
struct Vec1Config {
  i64 iModelVersion;
  int nBlocksize;
  int nBlocksizeMin;
  int nElem;
};

/*
** The number of PQ codes grouped together.
*/
#define VEC1_PQ_BLOCKSIZE 16

/*
** flags:
**   This is a mask of the VEC1_META_XYZ flags. However, each flag
**   is used slightly differently than in the mask in the header of
**   a meta-value list. Specifically:
**
**     1.  VEC1_META_1BYTEINT, VEC1_META_4BYTEINT and VEC1_META_REAL are 
**         each set if the current contents of the array does not preclude 
**         it from using that form (VEC1_META_GENERIC is always implicitly
**         set in this sense).
**
**     2.  VEC1_META_HASNULL is set if the array contains at least one 
**         NULL value.
**
** format:
**   This is set to either VEC1_META_GENERIC, 1BYTEINT, 4BYTEINT or REAL.
**   To indicate the actual format currently stored in buffer buf.
*/
typedef struct Vec1MetaBuilder Vec1MetaBuilder;
struct Vec1MetaBuilder {
  Vec1Buffer buf;

  u32 flags;
  u32 format;
};

typedef struct Vec1ListBuilder Vec1ListBuilder;
struct Vec1ListBuilder {
  Vec1Tab *pTab;                  /* Table this object belongs to */
  int nVectorSize;                /* Size of [compressed] vectors in bytes */
  int iBucket;                    /* Value for "bucket" column */
  int eBlocked;                   /* VEC1_BLOCKED_XYZ value */

  i64 iId;                        /* Rowid for %_idx, or 0 for NULL */
  i64 iFirst;                     /* Smallest rowid */
  i64 iLast;                      /* Largest rowid */
  int bSorted;                    /* True if rowids are sorted */
  int szRowid;                    /* Size of rowids (4 or 8) */
  int nTombstone;                 /* Current number of tombstone entries */

  Vec1Buffer bufRowid;            /* Buffer containing rowids so far */
  Vec1Buffer bufData;             /* Buffer containing data so far */

  Vec1MetaBuilder *aMeta;         /* Array of pTab->nMeta meta-list builders */
};

#define VEC1_BLOCKED_NONE 0
#define VEC1_BLOCKED_PQ   1
#define VEC1_BLOCKED_BIN  2

/*
** An object of this type is used to accumulate one or more new vector
** index entries in memory before they are flushed to disk.
*/
typedef struct Vec1Writer Vec1Writer;
struct Vec1Writer {
  Vec1Tab *pTab;                  /* Vec1 table object */
  int bRebuild;                   /* True if this is a 'rebuild' op */
  float *aResidual;               /* Temp space for residual vector */
  u8 *aPQ;                        /* Temp space for PQ */
  int nBld;                       /* Size of aBld[] */
  Vec1ListBuilder *aBld;          /* Array of list builders */
};

#define VEC1_INSTRUMENT_QUERIES 1

#ifdef VEC1_INSTRUMENT_QUERIES

# define VEC1_QINSTR_TOTAL     0
# define VEC1_QINSTR_INITQUERY 1
# define VEC1_QINSTR_COARSE    2
# define VEC1_QINSTR_IDXREAD   3
# define VEC1_QINSTR_METAREAD  4
# define VEC1_QINSTR_METASCAN  5
# define VEC1_QINSTR_LUT       6
# define VEC1_QINSTR_PQSCAN    7
# define VEC1_QINSTR_FINALSORT 8

# define VEC1_QINSTR_NCOUNTER 9


# define VEC1_QINSTR_START(pTab, eCounter) {         \
  (pTab)->aCycle[eCounter] -= vec1HardwareTimer();  \
}

# define VEC1_QINSTR_STOP(pTab, eCounter) {          \
  (pTab)->aCount[eCounter]++;                       \
  (pTab)->aCycle[eCounter] += vec1HardwareTimer();  \
}

#else
# define VEC1_QINSTR_STOP(pTab, eCounter)
# define VEC1_QINSTR_START(pTab, eCounter)
#endif

#define VEC1_META_COLUMN_BITS 8
#define VEC1_MAX_META_COLUMNS (1<<VEC1_META_COLUMN_BITS)

/* Virtual table object */
struct Vec1Tab {
  sqlite3_vtab base;
  sqlite3 *db;                    /* Database handle */
  char *zDb;                      /* Database containing vtab */
  char *zName;                    /* Name of vtab */
  char *zTrainTbl;                /* Name of %_model table */
  char *zIdxTbl;                  /* Name of %_idx table */
  int nMeta;                      /* Number of meta columns */
  sqlite3_stmt *aStmt[21];

  Vec1Config cfg;                 /* Values read from %_config table */
  Vec1Model mod;                  /* Model read from %_model table */
  Vec1Writer *pWriter;            /* Writer within active transaction */

  float *aTmpVec;                 /* Temp buffer for transformed vector */
  int nTmpVec;                    /* Size of aTmpVector in elements */

  void *pModBlob;                 /* Pointer to blob to free */
#ifdef VEC1_INSTRUMENT_QUERIES
  u64 aCycle[VEC1_QINSTR_NCOUNTER];
  u64 aCount[VEC1_QINSTR_NCOUNTER];
#endif
  Vec1Tab *pTabNext;
  Vec1TabList *pTabList;
};

typedef struct Vec1FlatIter Vec1FlatIter;
struct Vec1FlatIter {
  /* Private variables */
  const u8 *aBlob;
  int nBlob;
  int szRowid;
  int nEntry;
  int ii;

  /* Public variables */
  int szVec;
  const u8 *aVec;
  i64 iRowid;
};

typedef struct Vec1MetaValue Vec1MetaValue;
struct Vec1MetaValue {
  int eType;
  i64 iVal;                       /* Value for integer, size in bytes for T/B */
  double fVal;                    /* Value for float type */
  const u8 *pPtr;                 /* Pointer to data */
};

typedef struct Vec1Filter Vec1Filter;
struct Vec1Filter {
  char vop;                       /* VEC1_OP_XXX value */
  int iMeta;                      /* Index of meta-value column on LHS of op */
  int eType;                      /* Type of RHS value (SQLITE_INTEGER etc.) */
  i64 iVal;                       /* Value for integer, size in bytes for T/B */
  double fVal;                    /* Value for float type */
  u8 *pPtr;                       /* Pointer to data */
};

typedef struct Vec1BucketResult Vec1BucketResult;
struct Vec1BucketResult {
  int iBucket;
  float fDist;
};

typedef struct Vec1Query Vec1Query;
struct Vec1Query {
  Vec1Tab *pTab;

  /* Query parameters set by vec1SetupKANNQuery() */
  i64 K;                          /* Number of results required */
  int nProbe;                     /* Number of buckets to probe */
  double nProbeSlack;             /* Number of buckets to probe */
  int bStreaming;                 /* True for streaming query */
  int nFilter;                    /* Number of meta-value constraints */
  Vec1Filter *aFilter;            /* Array of meta-value constraints */

  float *aVector;                 /* Vector to query NN of */
  float *aTransform;              /* Vector to query NN of */
  u8 *aBit;                       /* Space for bitwise encode of 1 vector */

  /* Used by streaming queries only */
  int nBucket;                    /* Size of aBucket array */ 
  Vec1BucketResult *aBucket;      /* Remaining buckets for streaming queries */
  int nOrigRes;                   /* Number of original results */

  Vec1AnnHeap heap;               /* Heap object if this is indexed query */
};

/* 
** Cursor object.
**
** bSqlAnnQuery:
**   Set to true if this is an NN query against an index=none table - one
**   implemented by a single SQL statement.
*/
struct Vec1Csr {
  sqlite3_vtab_cursor base;
  sqlite3_stmt *pStmt;

  /* Values used for ANN queries against %_idx data */
  int iCurrentRes;
  int nCurrentRes;
  int bSeek;                      /* pStmt points at current entry already */

  int bLoadConfig;

  /* Cache of "distance" column for non-NN queries */
  char *zDistance;

  int bSqlNNQuery;
  Vec1Query *pQuery;
};

#define VEC1_OP_EQ      'A'
#define VEC1_OP_LT      'B'
#define VEC1_OP_GT      'C'
#define VEC1_OP_LE      'D'
#define VEC1_OP_GE      'E'
#define VEC1_OP_IS      'F'
#define VEC1_OP_ISNULL  'G'
#define VEC1_OP_NOTNULL 'H'
#define VEC1_OP_IN      'I'

#define VEC1_OP_LIMIT  'L'
#define VEC1_OP_PARAMS 'P'

#define VEC1_QUERY_DEFAULT_NPROBE 0.05

static char *vec1Strdup(int *pRc, const char *z){
  char *zRet = 0;
  if( *pRc==SQLITE_OK ){
    zRet = sqlite3_mprintf("%s", z);
    if( zRet==0 ){
      *pRc = SQLITE_NOMEM;
    }
  }
  return zRet;
}

static char *vec1MPrintf(int *pRc, const char *zFmt, ...){
  char *zRet = 0;
  va_list ap;
  va_start(ap, zFmt);
  zRet = sqlite3_vmprintf(zFmt, ap);
  va_end(ap);

  if( *pRc==SQLITE_OK ){
    if( zRet==0 ){
      *pRc = SQLITE_NOMEM;
    }
  }else{
    sqlite3_free(zRet);
    zRet = 0;
  }

  return zRet;
}

static void vec1VtabError(Vec1Tab *pTab, const char *zFmt, ...){
  char *zMsg = 0;
  va_list ap;
  va_start(ap, zFmt);
  zMsg = sqlite3_vmprintf(zFmt, ap);
  va_end(ap);
  sqlite3_free(pTab->base.zErrMsg);
  pTab->base.zErrMsg = zMsg;
}

/*
** Prepare the SQL statement passed as the second argument. If successful,
** set (*pStmt) to point to the prepared statement handle and return
** SQLITE_OK. It is the responsibility of the caller to eventually finalize
** the statement using sqlite3_finalize().
**
** Or, if an error occurs, set (*ppStmt) to NULL, return an SQLite error
** code and leave an error message in the virtual table object.
*/
static int vec1PrepareSql(
  Vec1Tab *pTab, 
  sqlite3_stmt **ppStmt,
  const char *zFmt, 
  ...
){
  char *zSql = 0;
  int rc = SQLITE_OK;
  va_list ap;

  va_start(ap, zFmt);
  zSql = sqlite3_vmprintf(zFmt, ap);
  va_end(ap);

  *ppStmt = 0;
  if( zSql ){
    rc = sqlite3_prepare_v2(pTab->db, zSql, -1, ppStmt, 0);
    sqlite3_free(zSql);
    if( rc!=SQLITE_OK ){
      vec1VtabError(pTab, "%s", sqlite3_errmsg(pTab->db));
    }
  }else{
    rc = SQLITE_NOMEM;
  }
  return rc;
}

#define VEC1_SQL_SCAN_BASE     1
#define VEC1_SQL_INSERT_IDX    2

#define VEC1_SQL_SCAN_IDX      3
#define VEC1_SQL_CNT_IDX       4

#define VEC1_SQL_FILTER_IDX    5

#define VEC1_SQL_WRITE_CONFIG  5
#define VEC1_SQL_INCR_CONFIG   6
#define VEC1_SQL_LOAD_CONFIG   7




static int vec1GetSql(Vec1Tab *pTab, int eSql, sqlite3_stmt **ppStmt){
  const char *azSql[] = {
#define VEC1_SQL_INSERT_BASE   0
    "INSERT INTO %Q.'%q_base' VALUES(?, ?%s)",
    "SELECT * FROM %Q.'%q_base' ORDER BY 1",
    "INSERT INTO %Q.'%q_idx'(id, bucket, first, last, val)VALUES(?,?,?,?,?)",

    "SELECT val, bucket, rowid FROM %Q.'%q_idx'",
    "SELECT sum( length(val) / ? ) FROM %Q.'%q_idx'",

    "REPLACE INTO %Q.'%q_config'(id, val) VALUES(?, ?)",
    "INSERT INTO %Q.'%q_config'(id, val) VALUES(?, 1) "
      "ON CONFLICT(id) DO UPDATE SET val=val+1",

    "SELECT id, val FROM %Q.'%q_config'",

#define VEC1_SQL_LOOKUP_BASE   8
    "SELECT * FROM %Q.'%q_base' WHERE id=?",

#define VEC1_SQL_EDIT_IDX      9
    "SELECT id, min( length(val) ) FROM %Q.'%q_idx' WHERE bucket=?",

#define VEC1_SQL_DEL_LOOKUP_BASE 10
    /* Used when removing a row by id. This statement deletes the %_base
    ** entry and returns the vector so that the user may use it to 
    ** find the %_idx entry to delete.  */
    "DELETE FROM %Q.'%q_base' WHERE id=? RETURNING vector",

#define VEC1_SQL_SEARCH_IDX 11
    "SELECT id FROM %Q.'%q_idx' WHERE bucket=? AND ? BETWEEN first AND last",

#define VEC1_SQL_SEARCH_IDX_FOR_INSERT 12
    "SELECT id, first, last, val "
      "FROM %Q.'%q_idx' WHERE bucket=? AND length(val)<? LIMIT 1",

#define VEC1_SCAN_BUCKET 13
    "SELECT val, bucket, rowid FROM %Q.'%q_idx' WHERE bucket = 0+?",

#define VEC1_SQL_UPDATE_BASE   14
    "UPDATE %Q.'%q_base' SET vector = ? WHERE id=?",

#define VEC1_SQL_ZERO_BASE_RANGE 15
    "UPDATE %Q.'%q_base' SET vector = 0, id=id WHERE id BETWEEN ? AND ?",

#define VEC1_SQL_DELETE_FROM_IDX 16
    "DELETE FROM %Q.'%q_idx' WHERE id = ?",

#define VEC1_SQL_UPDATE_BASE2   17
    "UPDATE %Q.'%q_base' SET vector = ?, id=id WHERE id=?",

#define VEC1_SQL_READ_META   18
    "SELECT val FROM %Q.'%q_meta' WHERE id=?",

#define VEC1_SQL_WRITE_META   19
    "REPLACE INTO %Q.'%q_meta' VALUES(?, ?)",

#define VEC1_SQL_DELETE_FROM_META 20
    "DELETE FROM %Q.'%q_meta' WHERE id BETWEEN (?1<<8) AND (?1<<8)+255",
  };
  int rc = SQLITE_OK;

  assert( eSql>=0 && eSql<size_of_array(azSql) );
  assert( size_of_array(azSql)==size_of_array(pTab->aStmt) );

  if( pTab->aStmt[eSql]==0 ){

    if( eSql==VEC1_SQL_INSERT_BASE ){
      int ii;
      char *zExtra = 0;
      for(ii=0; ii<pTab->nMeta; ii++){
        zExtra = vec1MPrintf(&rc, "%z,?", zExtra);
      }
      if( rc==SQLITE_OK ){
        rc = vec1PrepareSql(
            pTab, &pTab->aStmt[eSql], azSql[eSql], 
            pTab->zDb, pTab->zName, zExtra
        );
      }
      sqlite3_free(zExtra);
    }else{
      rc = vec1PrepareSql(
          pTab, &pTab->aStmt[eSql], azSql[eSql], pTab->zDb, pTab->zName
      );
    }
  }

  *ppStmt = pTab->aStmt[eSql];
  return rc;
}

static void vec1UnloadModel(Vec1Tab *pTab){
  sqlite3_free(pTab->pModBlob);
  sqlite3_free(pTab->mod.aModelT);
  vec1ModelFreeTCent(&pTab->mod);
  pTab->pModBlob = 0;
  pTab->cfg.iModelVersion = 0;
  memset(&pTab->mod, 0, sizeof(pTab->mod));
}

static void vec1UnloadConfig(Vec1Tab *pTab){
  pTab->cfg.nElem = 0;
}

/*
** xDisconnect method. Free the virtual table object passed as the only
** argument.
*/
static int vec1DisconnectMethod(sqlite3_vtab *pVtab){
  Vec1Tab *pTab = (Vec1Tab*)pVtab;
  int ii;

  /* Remove this table object from the database handle list of vec1 tables */
  if( pTab->pTabList ){
    Vec1Tab **pp = &pTab->pTabList->pFirst;
    while( *pp!=pTab ) pp = &(*pp)->pTabNext;
    *pp = pTab->pTabNext;
  }

  for(ii=0; ii<size_of_array(pTab->aStmt); ii++){
    sqlite3_finalize(pTab->aStmt[ii]);
  }
  vec1UnloadModel(pTab);
  sqlite3_free(pTab->zDb);
  sqlite3_free(pTab->zName);
  sqlite3_free(pTab->zTrainTbl);
  sqlite3_free(pTab->zIdxTbl);
  sqlite3_free(pTab->aTmpVec);
  sqlite3_free(pTab);
  return SQLITE_OK;
}

static int vec1SqlExec(Vec1Tab *pTab, const char *zSqlfmt, char **pzErr){
  char *zSql = 0;
  int rc = SQLITE_OK;

  zSql = sqlite3_mprintf(zSqlfmt, pTab->zDb, pTab->zName);
  if( zSql==0 ){
    rc = SQLITE_NOMEM;
  }else{
    rc = sqlite3_exec(pTab->db, zSql, 0, 0, pzErr);
    sqlite3_free(zSql);
  }

  return rc;
}

static void vec1StmtReset(int *pRc, sqlite3_stmt *pStmt){
  int rc = sqlite3_reset(pStmt);
  if( *pRc==SQLITE_OK ) *pRc = rc;
}

static void vec1StmtFinalize(int *pRc, sqlite3_stmt *pStmt){
  int rc = sqlite3_finalize(pStmt);
  if( *pRc==SQLITE_OK ) *pRc = rc;
}

/*
** Return true if character c is an ASCII range whitespace character.
*/
static int vec1IsWhitespace(char c){
  return (c==' ' || c=='\t' || c=='\r' || c=='\n');
}

/*
** Check that zName is a legal name for a vec1 column. A legal column name
** is any single token or quoted string, with no trailing garbage. For
** the purposes of this function, a token is a contiguous sequence of
** non-whitespace characters that do not begin with a quote character.
**
** If zName is a legal name (or an illegal name that will be detected by
** sqlite3_declare_vtab()), SQLITE_OK is returned and (*pzErr) is left
** unchanged. Or, if it is not a legal name, SQLITE_ERROR is returned, and
** *pzErr is left to point to a buffer containing an English language
** error message. It is the responsibility of the caller to eventually
** free this buffer using sqlite3_free().
*/
static int vec1CheckColumnName(const char *zName, char **pzErr){
  char aName[16];
  int nName = 0;
  const char *z = zName;

  while( vec1IsWhitespace(*z) ) z++;

  if( (*z)=='"' || (*z)=='\'' || (*z)=='`' ){
    char q = *z++;
    while( 1 ){
      if( (*z)=='\0' ){
        *pzErr = sqlite3_mprintf("vec1: unmatched quote character");
        return SQLITE_ERROR;
      }
      if( (*z)==q ){
        z++;
        if( (*z)!=q ) break;
      }
      if( nName<(int)sizeof(aName) ) aName[nName++] = *z;
      z++;
    }
  }
  else if( (*z)=='[' ){
    z++;
    while( (*z) && (*z)!=']' ){
      if( nName<(int)sizeof(aName) ) aName[nName++] = *z;
      z++;
    }
    if( (*z)=='\0' ){
      *pzErr = sqlite3_mprintf("vec1: unmatched [ character");
      return SQLITE_ERROR;
    }
    z++;
  }
  else {
    while( (*z) && 0==vec1IsWhitespace(*z) ){
      if( nName<(int)sizeof(aName) ) aName[nName++] = *z;
      z++;
    }
  }

  while( vec1IsWhitespace(*z) ) z++;
  if( (*z)!='\0' ){
    *pzErr = sqlite3_mprintf("vec1: trailing characters after column name");
    return SQLITE_ERROR;
  }

  if( nName==5 && 0==sqlite3_strnicmp(aName, "rowid", 5) 
   || nName==8 && 0==sqlite3_strnicmp(aName, "distance", 8) 
  ){
    *pzErr = sqlite3_mprintf("vec1: reserved column name: %s", zName);
    return SQLITE_ERROR;
  }

  return SQLITE_OK;
}

/*
** Invoke sqlite3_declare_vtab() for this table.
*/
static int vec1DeclareVtab(
  sqlite3 *db, 
  int argc, 
  const char *const* argv,
  char **pzErr
){
  const char *zVector = "vector";
  char *zExtra = 0;
  char *zCreate = 0;
  int rc = SQLITE_OK;
  int ii;

  for(ii=3; rc==SQLITE_OK && ii<argc; ii++){
    rc = vec1CheckColumnName(argv[ii], pzErr);
  }

  if( rc==SQLITE_OK ){
    if( argc>3 ){
      zVector = argv[3];
    }
    for(ii=4; ii<argc; ii++){
      zExtra = vec1MPrintf(&rc, "%z, %s", zExtra, argv[ii]);
    }
  }

  zCreate = vec1MPrintf(&rc, 
      "CREATE TABLE v1(cmd HIDDEN, arg HIDDEN, distance HIDDEN, %s BLOB%z)",
      zVector, zExtra
  );
  if( rc==SQLITE_OK ){
    rc = sqlite3_declare_vtab(db, zCreate);
    sqlite3_free(zCreate);
    if( rc!=SQLITE_OK ){
      *pzErr = sqlite3_mprintf("vec1: %s", sqlite3_errmsg(db));
    }
  }

  return rc;
}

/*
** Arguments:
**
**     argv[0] - module name ("vec1")
**     argv[1] - database name
**     argv[2] - table name
*/
static int vec1CreateConnect(
  int bCreate,
  Vec1TabList *pTabList,
  sqlite3 *db,
  int argc, const char *const *argv,
  sqlite3_vtab **ppVtab,
  char **pzErr
){
  Vec1Tab *pTab;
  int rc = SQLITE_OK;

  assert( *ppVtab==0 );

  sqlite3_vtab_config(db, SQLITE_VTAB_CONSTRAINT_SUPPORT, 1);
  sqlite3_vtab_config(db, SQLITE_VTAB_INNOCUOUS, 1);

  pTab = sqlite3_malloc(sizeof(*pTab));
  if( pTab==0 ){
    return SQLITE_NOMEM;
  }
  memset(pTab, 0, sizeof(*pTab));
  pTab->zDb = vec1Strdup(&rc, argv[1]);
  pTab->zName = vec1Strdup(&rc, argv[2]);
  pTab->zTrainTbl = vec1MPrintf(&rc, "%s_model", argv[2]);
  pTab->zIdxTbl = vec1MPrintf(&rc, "%s_idx", argv[2]);
  pTab->db = db;
  pTab->mod.hdr.eDistance = VEC1_DISTANCE_L2;

  pTab->nMeta = MAX(argc-4, 0);
  if( pTab->nMeta>VEC1_MAX_META_COLUMNS ){
    *pzErr = sqlite3_mprintf(
        "too many meta-data columns (max is %d)", VEC1_MAX_META_COLUMNS
    );
    rc = SQLITE_ERROR;
  }

  if( rc==SQLITE_OK ){
    rc = vec1DeclareVtab(db, argc, argv, pzErr);
  }

  if( rc==SQLITE_OK && bCreate ){
    char *zShadowSchema = vec1ShadowSchema(pTab->zDb, pTab->zName, pTab->nMeta);
    if( zShadowSchema==0 ){
      rc = SQLITE_NOMEM;
    }else{
      rc = sqlite3_exec(db, zShadowSchema, 0, 0, pzErr);
      sqlite3_free(zShadowSchema);
    }
    if( rc==SQLITE_OK ){
      sqlite3_stmt *pStmt = 0;
      rc = vec1GetSql(pTab, VEC1_SQL_WRITE_CONFIG, &pStmt);
      if( rc==SQLITE_OK ){
        sqlite3_bind_int(pStmt, 1, VEC1_CONFIG_FMTVERSION);
        sqlite3_bind_int(pStmt, 2, VEC1_CURRENT_FMTVERSION);
        sqlite3_step(pStmt);
        vec1StmtReset(&rc, pStmt);
      }
    }
  }

  if( rc!=SQLITE_OK ){
    if( pTab->base.zErrMsg ){
      *pzErr = pTab->base.zErrMsg;
      pTab->base.zErrMsg = 0;
    }
    vec1DisconnectMethod((sqlite3_vtab*)pTab);
    return rc;
  }

  /* Add the new table to the database handle list of vec1 tables. It
  ** will be eventually removed by vec1DisconnectMethod(). */
  pTab->pTabList = pTabList;
  pTab->pTabNext = pTabList->pFirst;
  pTabList->pFirst = pTab;

  *ppVtab = &pTab->base;
  return SQLITE_OK;
}

/*
** xCreate / xConnect
*/
static int vec1ConnectMethod(
  sqlite3 *db,
  void *p,                        /* Pointer to Vec1TabList for this db */
  int argc, 
  const char *const *argv,
  sqlite3_vtab **ppVtab,
  char **pzErr
){
  return vec1CreateConnect(0, (Vec1TabList*)p, db, argc, argv, ppVtab, pzErr);
}

static int vec1CreateMethod(
  sqlite3 *db,
  void *p,                        /* Pointer to Vec1TabList for this db */
  int argc,
  const char *const *argv,
  sqlite3_vtab **ppVtab,
  char **pzErr
){
  return vec1CreateConnect(1, (Vec1TabList*)p, db, argc, argv, ppVtab, pzErr);
}

static int vec1DestroyMethod(sqlite3_vtab *pVtab){
  Vec1Tab *pTab = (Vec1Tab*)pVtab;
  int rc = SQLITE_OK;

  char *zDrop = vec1DropShadowSchema(pTab->zDb, pTab->zName);
  if( zDrop==0 ){
    rc = SQLITE_NOMEM;
  }else{
    rc = sqlite3_exec(pTab->db, zDrop, 0, 0, 0);
    sqlite3_free(zDrop);
  }

  if( rc==SQLITE_OK ){
    vec1DisconnectMethod(pVtab);
  }

  return rc;
}

/*
** Cursor methods
*/
static int vec1Open(
  sqlite3_vtab *pVtab,
  sqlite3_vtab_cursor **ppCursor
){
  Vec1Csr *pCur;

  UNUSED_PARAMETER(pVtab);

  pCur = sqlite3_malloc(sizeof(*pCur));
  if( pCur==0 ){
    return SQLITE_NOMEM;
  }
  memset(pCur, 0, sizeof(*pCur));

  *ppCursor = &pCur->base;
  return SQLITE_OK;
}

static void vec1QueryFree(Vec1Query *pQuery){
  if( pQuery ){
    int ii;
    for(ii=0; ii<pQuery->nFilter; ii++){
      sqlite3_free(pQuery->aFilter[ii].pPtr);
    }
    sqlite3_free(pQuery->heap.aRes);
    sqlite3_free(pQuery->aBucket);
    sqlite3_free(pQuery);
  }
}

static void vec1ResetCsr(Vec1Csr *pCsr){
  vec1QueryFree(pCsr->pQuery);
  sqlite3_finalize(pCsr->pStmt);
  pCsr->pStmt = 0;
  pCsr->bSeek = 0;
  sqlite3_free(pCsr->zDistance);
  pCsr->zDistance = 0;
  pCsr->bSqlNNQuery = 0;
}

static int vec1Close(sqlite3_vtab_cursor *cur){
  Vec1Csr *pCsr = (Vec1Csr*)cur;
  vec1ResetCsr(pCsr);
  sqlite3_free(pCsr);
  return SQLITE_OK;
}

static void vec1FlatIterNext(Vec1FlatIter *pIter){
  while( 1 ){
    pIter->ii++;
    if( pIter->ii>=pIter->nEntry ){
      pIter->iRowid = 0;
      pIter->aVec = 0;
      break;
    }else{
      i64 iRowid = 0;
      int iOff = VEC1_LIST_SZHDR + pIter->ii * pIter->szRowid;
      if( pIter->szRowid==4 ){
        iRowid = (i64)vec1GetU32(&pIter->aBlob[iOff]);
        if( iRowid==VEC1_TOMBSTONE_32 ) continue;
      }else{
        iRowid = (i64)vec1GetU64(&pIter->aBlob[iOff]);
        if( iRowid==VEC1_TOMBSTONE_64 ) continue;
      }

      iOff = VEC1_LIST_SZHDR + 
             (pIter->nEntry * pIter->szRowid) + 
             (pIter->ii * pIter->szVec);
      pIter->aVec = &pIter->aBlob[iOff];
      pIter->iRowid = iRowid;
      break;
    }
  }
}

/*
** Return the encoded vector size for the model. Two cases:
**
**   a) PQ encoding. In this case encoded vector size is hdr.nCodebook.
**
**   b) Bitwise encoding. In this case encoded vector size is 4 bytes + 
**      1 bit per dimension.
*/
static int vec1EncodedVectorSize(const Vec1Model *pMod){
  int ret = (int)pMod->hdr.nCodebook;
  if( pMod->hdr.flags & VEC1_MODEL_BITQUANT ) ret += sizeof_f32;
  return ret;
}

/*
** nBlob byte buffer aBlob[] contains a list read from the %_idx table.
** This function returns zero if the list size matches the embedded
** number-of-entries field, and so the list may be accessed safely without
** bounds checking, or non-zero otherwise.
*/
static int vec1CheckIdxSize(Vec1Tab *p, const u8 *aBlob, i64 nBlob){
  int bRet = 1;

  /* If the blob is smaller than 8 bytes, it must be corrupt */
  if( nBlob>=VEC1_LIST_SZHDR ){

    /* Read the flags and number-of-entries fields */
    u32 flags = vec1GetU32(&aBlob[0]);
    i64 nEntry = (int)vec1GetU32(&aBlob[4]);
    const i64 szRowid = ((flags & VEC1_LIST_64BIT) ? 8 : 4);

    if( p->mod.hdr.nCodebook>0 ){
      const i64 nBlk = ((nEntry+VEC1_PQ_BLOCKSIZE-1) / VEC1_PQ_BLOCKSIZE);
      const i64 szBlk = VEC1_PQ_BLOCKSIZE * vec1EncodedVectorSize(&p->mod);
      bRet = (nBlob!=(VEC1_LIST_SZHDR + nEntry*szRowid + nBlk*szBlk));
    }else{
      const i64 szVec = p->cfg.nElem*sizeof_f32;
      bRet = (nBlob!=(VEC1_LIST_SZHDR + nEntry*szRowid + nEntry*szVec));
    }
  }
  return bRet;
}

static int vec1FlatIterStart(
  Vec1Tab *pTab, 
  Vec1FlatIter *pIter,
  const u8 *aBlob, 
  int nBlob
){
  u32 flags;

  pIter->aBlob = aBlob;
  pIter->nBlob = nBlob;
  pIter->ii = 0;

  if( vec1CheckIdxSize(pTab, aBlob, nBlob) ){
    return VEC1_CORRUPT;
  }

  flags = vec1GetU32(aBlob);
  pIter->szRowid = (flags & VEC1_LIST_64BIT) ? 8 : 4;
  pIter->nEntry = vec1GetU32(&aBlob[4]);
  pIter->szVec = (pTab->cfg.nElem * sizeof_f32);
  pIter->ii = -1;
  vec1FlatIterNext(pIter);
  return SQLITE_OK;
}

/*
** Merge the two sorted subarrays a[p..q-1] and a[q..r-1] using
** the temporary buffer aTmp.
*/
static void vec1Merge(
  Vec1AnnResult *a,
  Vec1AnnResult *aTmp,
  int p,
  int q,
  int r
){
  int i = p;
  int j = q;
  int k = p;

  while( i<q && j<r ){
    if( a[i].fDist <= a[j].fDist ){
      aTmp[k++] = a[i++];
    }else{
      aTmp[k++] = a[j++];
    }
  }
  while( i<q ){
    aTmp[k++] = a[i++];
  }
  while( j<r ){
    aTmp[k++] = a[j++];
  }

  for(i=p; i<r; i++){
    a[i] = aTmp[i];
  }
}

/*
** Recursive merge-sort implementation.
*/
static void vec1MergeSort(
  Vec1AnnResult *a,
  Vec1AnnResult *aTmp,
  int p,
  int r
){
  int q;
  if( r-p<=1 ) return;

  q = p + (r-p)/2;
  vec1MergeSort(a, aTmp, p, q);
  vec1MergeSort(a, aTmp, q, r);
  vec1Merge(a, aTmp, p, q, r);
}

/*
** Sort array a[0..n-1] in ascending order of fDist.
*/
static int vec1AnnResultSort(Vec1AnnResult *a, i64 n){
  int rc = SQLITE_OK;

  if( n>1 ){
    i64 nByte = sizeof(Vec1AnnResult) * n;
    Vec1AnnResult *aTmp = (Vec1AnnResult*)sqlite3_malloc64(nByte);
    if( aTmp==0 ){
      rc = SQLITE_NOMEM;
    }else{
      vec1MergeSort(a, aTmp, 0, (int)n);
      sqlite3_free(aTmp);
    }
  }

  return rc;
}

static int vec1DoKANNBucket(Vec1Query*,int,Vec1Buffer*,Vec1Buffer*,Vec1Buffer*);

static int vec1StreamingNext(Vec1Csr *pCsr){
  int rc = SQLITE_OK;
  Vec1Query *pQuery = pCsr->pQuery;
  Vec1AnnHeap *pHeap = &pQuery->heap;

  int ii;
  int nSort = (int)(pHeap->nRes - pCsr->nCurrentRes);

  {
    i64 nRes = pHeap->nRes;
    pHeap->nRes = 0;
    for(ii=pCsr->nCurrentRes; ii<nRes; ii++){
      vec1HeapInsert(pHeap, pHeap->aRes[ii].iRowid, pHeap->aRes[ii].fDist);
    }
  }

  while( pQuery->nBucket>0 && nSort<=(pQuery->nOrigRes/2) ){
    Vec1Buffer a = {0,0,0};
    Vec1Buffer b = {0,0,0};
    Vec1Buffer c = {0,0,0};
    int iBest = 0;
    int iBucket = 0;

    /* Find the nearest remaining bucket to the query vector. Remove it
    ** from the aBucket[] array.  */
    for(ii=1; ii<pQuery->nBucket; ii++){
      if( pQuery->aBucket[ii].fDist<pQuery->aBucket[iBest].fDist ){
        iBest = ii;
      }
    }
    iBucket = pQuery->aBucket[iBest].iBucket;
    pQuery->nBucket--;
    if( iBest!=pQuery->nBucket ){
      pQuery->aBucket[iBest] = pQuery->aBucket[pQuery->nBucket];
    }

    rc = vec1DoKANNBucket(pQuery, iBucket, &a, &b, &c);

    vec1BufferFree(&a);
    vec1BufferFree(&b);
    vec1BufferFree(&c);
    if( rc!=SQLITE_OK ) return rc;
  }

  nSort = (int)MIN(pHeap->nMax, pHeap->nRes);
  rc = vec1AnnResultSort(pHeap->aRes, nSort);
  if( rc==SQLITE_OK ){
    pCsr->iCurrentRes = 0;
    pCsr->nCurrentRes = nSort;
  }

  return rc;
}

/*
** xNext() method for vec1 cursors.
*/
static int vec1NextMethod(sqlite3_vtab_cursor *cur){
  int rc = SQLITE_OK;
  Vec1Csr *pCsr = (Vec1Csr *)cur;

  if( pCsr->zDistance ){
    sqlite3_free(pCsr->zDistance );
    pCsr->zDistance = 0;
  }

  if( pCsr->pQuery ){
    pCsr->iCurrentRes++;
    pCsr->bSeek = 0;
    if( pCsr->iCurrentRes>=pCsr->nCurrentRes ){
      /* If this is a streaming query, try to get some more results. */
      if( pCsr->pQuery->bStreaming ){
        rc = vec1StreamingNext(pCsr);
      }

      /* If this was not a streaming query, or no further results are
      ** available, set pStmt to 0 to indicate EOF.  */
      if( pCsr->iCurrentRes>=pCsr->nCurrentRes ){
        sqlite3_finalize(pCsr->pStmt);
        pCsr->pStmt = 0;
      }
    }
    sqlite3_reset(pCsr->pStmt);
  }else{
    rc = sqlite3_step(pCsr->pStmt);
    if( rc!=SQLITE_ROW ){
      rc = sqlite3_finalize(pCsr->pStmt);
      pCsr->pStmt = 0;
    }else{
      rc = SQLITE_OK;
    }
  }
  return rc;
}

static int vec1ModelTransform(
  Vec1Model *pMod, 
  float **paOut
){
  if( pMod->aModel ){
    const int nCodebook = pMod->hdr.nCodebook;
    const int nCodeElem = pMod->nCodeElem;
    int M;
  
    float *pOut = (float*)sqlite3_malloc(
        sizeof_f32 * nCodebook * nCodeElem * VEC1_PQ_CODEBOOK_SZ
    );
    if( pOut==0 ) return SQLITE_NOMEM;
    *paOut = pOut;
  
    /* For VEC1_TRANSPOSE_WIDTH==8, the order of elements in output is:
    **
    **     aModel[M=0][d=0]            [K=0..7]
    **     aModel[M=0][d=1]            [K=0..7]
    **     aModel[M=0][d=2]            [K=0..7]
    **     ...
    **     aModel[M=0][d=(nCodeElem-1)][K=0..7]
    **     aModel[M=0][d=0]            [K=8..15]
    **     aModel[M=0][d=1]            [K=8..15]
    **     aModel[M=0][d=2]            [K=8..15]
    **     ...
    **     aModel[M=0][d=(nCodeElem-1)][K=248..255]
    **     aModel[M=1][d=0]            [K=0..7]
    **     ...
    */
    for(M=0; M<nCodebook; M++){
      vec1CodebookTranspose(
        &pMod->aModel[ M * nCodeElem * VEC1_PQ_CODEBOOK_SZ ],
        nCodeElem, VEC1_TRANSPOSE_WIDTH, VEC1_PQ_CODEBOOK_SZ,
        &pOut[ M * nCodeElem * VEC1_PQ_CODEBOOK_SZ ]
      );
    }
  }

  return SQLITE_OK;
}


/*
** If it is not already loaded, load the PQ model from disk.
*/
static int vec1LoadModel(Vec1Tab *pTab){
  int rc = SQLITE_OK;

  sqlite3 *db = pTab->db;
  sqlite3_blob *pBlob = 0;

  vec1UnloadModel(pTab);
  rc = sqlite3_blob_open(db, pTab->zDb, pTab->zTrainTbl, "val", 1, 0, &pBlob);
  if( rc==SQLITE_OK ){
    int nByte = sqlite3_blob_bytes(pBlob);
    void *pData = sqlite3_malloc(nByte);
    if( pData==0 ){
      rc = SQLITE_NOMEM;
    }else{
      rc = sqlite3_blob_read(pBlob, pData, nByte, 0);
    }
    if( rc==SQLITE_OK ){
      rc = vec1DecodeModel(pData, nByte, &pTab->mod, &pTab->base.zErrMsg);
    }
    if( rc==SQLITE_OK ){
      pTab->pModBlob = pData;
    }else{
      sqlite3_free(pData);
    }
    sqlite3_blob_close(pBlob);
  }

  if( rc==SQLITE_OK ){
    rc = vec1ModelTransform(&pTab->mod, &pTab->mod.aModelT);
  }

  return rc;
}

#if 0
static int vec1AnnResultPartition(Vec1AnnResult *aArr, int iLeft, int iRight){
  double fPivot = aArr[iRight].fDist;  /* choose last element */
  int iSmall = iLeft;                  /* Next small element goes here */
  int ii;
  for(ii=iLeft; ii<iRight; ii++){
    if( aArr[ii].fDist<fPivot ){
      SWAP(Vec1AnnResult, aArr[iSmall], aArr[ii]);
      iSmall++;
    }
  }
  SWAP(Vec1AnnResult, aArr[iSmall], aArr[iRight]);
  return iSmall;
}

static void vec1AnnResultSelect(Vec1AnnResult *aArr, int nArr, int K){
  int iLeft = 0;
  int iRight = nArr-1;
  while (iLeft <= iRight) {
    size_t iPivot = vec1AnnResultPartition(aArr, iLeft, iRight);
    if( iPivot==K ){
      return;
    }else if( iPivot>K ){
      iRight = iPivot - 1;
    } else {
      iLeft = iPivot + 1;
    }
  }
}
#endif

/*
** Merge the two sorted subarrays a[p..q-1] and a[q..r-1] using
** the temporary buffer aTmp.
*/
static void vec1BucketMerge(
  Vec1BucketResult *a,
  Vec1BucketResult *aTmp,
  int p,
  int q,
  int r
){
  int i = p;
  int j = q;
  int k = p;

  while( i<q && j<r ){
    if( a[i].fDist <= a[j].fDist ){
      aTmp[k++] = a[i++];
    }else{
      aTmp[k++] = a[j++];
    }
  }
  while( i<q ){
    aTmp[k++] = a[i++];
  }
  while( j<r ){
    aTmp[k++] = a[j++];
  }

  for(i=p; i<r; i++){
    a[i] = aTmp[i];
  }
}

/*
** Recursive merge-sort implementation.
*/
static void vec1BucketMergeSort(
  Vec1BucketResult *a,
  Vec1BucketResult *aTmp,
  int p,
  int r
){
  int q;
  if( r-p<=1 ) return;

  q = p + (r-p)/2;
  vec1BucketMergeSort(a, aTmp, p, q);
  vec1BucketMergeSort(a, aTmp, q, r);
  vec1BucketMerge(a, aTmp, p, q, r);
}

/*
** Sort array a[0..n-1] in ascending order of fDist.
*/
static int vec1BucketSort(Vec1BucketResult *a, i64 n){
  int rc = SQLITE_OK;

  if( n>1 ){
    i64 nByte = sizeof(Vec1BucketResult) * n;
    Vec1BucketResult *aTmp = (Vec1BucketResult*)sqlite3_malloc64(nByte);
    if( aTmp==0 ){
      rc = SQLITE_NOMEM;
    }else{
      vec1BucketMergeSort(a, aTmp, 0, (int)n);
      sqlite3_free(aTmp);
    }
  }

  return rc;
}


/*
** Partition the part of the array between elements iLeft and iRight
** (inclusive). Return the index of the partition key.
*/
static int vec1BucketPartition(Vec1BucketResult *aArr, int iLeft, int iRight){
  float fPivot = aArr[iRight].fDist;   /* choose last element */
  int iSmall = iLeft;                  /* Next small element goes here */
  int ii;
  for(ii=iLeft; ii<iRight; ii++){
    if( aArr[ii].fDist<fPivot ){
      SWAP(Vec1BucketResult, aArr[iSmall], aArr[ii]);
      iSmall++;
    }
  }
  SWAP(Vec1BucketResult, aArr[iSmall], aArr[iRight]);
  return iSmall;
}


/*
** Use quick-select to shuffle array aArr[] so that the first K elements
** all have smaller fDist values that then remaining (nArr-K) elements.
*/
static void vec1BucketSelect(Vec1BucketResult *aArr, int nArr, int K){
  int iLeft = 0;
  int iRight = nArr-1;
  assert( iLeft<=iRight && K<=iRight && K>=0 );
  while( 1 ){
    int iPivot = vec1BucketPartition(aArr, iLeft, iRight);
    if( iPivot==K ){
      return;
    }else if( iPivot>K ){
      iRight = iPivot - 1;
    } else {
      iLeft = iPivot + 1;
    }
  }
}

static int vec1FindBuckets(
  Vec1Tab *pTab,
  const float *aVec,
  int nProbe,
  Vec1BucketResult **paBucket
){
  int nBucket = pTab->mod.hdr.nBucket;
  int nElem = pTab->mod.hdr.nElem;
  Vec1BucketResult *aRes = 0;
  int rc = SQLITE_OK;

  aRes = (Vec1BucketResult*)sqlite3_malloc(sizeof(Vec1BucketResult) * nBucket);

  if( aRes ){
    int ii;
    for(ii=0; ii<nBucket; ii++){
      const float *aCentroid = &pTab->mod.aCentroid[ii*nElem];
      aRes[ii].iBucket = ii;
      aRes[ii].fDist = (float)vec1L2Dist(aVec, aCentroid, nElem);
    }

    if( nProbe<nBucket ){
      vec1BucketSelect(aRes, nBucket, nProbe);
      rc = vec1BucketSort(aRes, nProbe);
#ifndef NDEBUG
      for(ii=0; ii<nBucket; ii++){
        if( ii<nProbe ){
          assert( aRes[ii].fDist<=aRes[nProbe].fDist );
        }else{
          assert( aRes[ii].fDist>=aRes[nProbe].fDist );
        }
      }
#endif
#if 0
      for(ii=0; ii<nProbe; ii++){
        printf("%f ", aRes[ii].fDist);
      }
      printf("\n");
#endif
    }

  }else{
    rc = SQLITE_NOMEM;
  }

  *paBucket = aRes;
  return rc;
}

static const float *vec1TransformInputVector(
  const Vec1Model *pMod,
  float *aTmp,
  const float *aInput
){
  const float *aRet = aInput;
  if( (pMod->hdr.flags & VEC1_MODEL_ROTATE) ){
    vec1RotateVector(pMod->hdr.nElem, pMod->aRotation, aInput, aTmp);
    if( pMod->hdr.eDistance==VEC1_DISTANCE_COS ){
      vec1NormalizeVector(aTmp, pMod->hdr.nElem);
    }
    aRet = aTmp;
  }
  else if( (pMod->hdr.flags & VEC1_MODEL_WHT) ){
    memcpy(aTmp, aInput, pMod->hdr.nElem * sizeof_f32);
    vec1ApplyWHT(aTmp, pMod->hdr.nElem);
    aRet = aTmp;
  }
  else if( pMod->hdr.eDistance==VEC1_DISTANCE_COS ){
    memcpy(aTmp, aInput, pMod->hdr.nElem * sizeof_f32);
    vec1NormalizeVector(aTmp, pMod->hdr.nElem);
    aRet = aTmp;
  }

  if( aRet!=aTmp
   && pMod->hdr.nCodebook>0 
   && (pMod->hdr.nCodebook*pMod->nCodeElem)!=pMod->hdr.nElem
  ){
    memcpy(aTmp, aRet, pMod->hdr.nElem * sizeof_f32);
    aRet = aTmp;
  }

  return aRet;
}

/*
** Return true if vec1TransformRequired() is not a no-op. i.e. if any
** transformation is required for input vectors.
*/
static int vec1TransformRequired(const Vec1Model *pMod){
  Vec1ModelHeader const *p = &pMod->hdr;
  return 
       (p->flags & (VEC1_MODEL_ROTATE|VEC1_MODEL_WHT)) 
    || (p->eDistance==VEC1_DISTANCE_COS);
}


/*
** This routine is the core of both the LUT builder and the vector encoder.
**
** pIn:
**   pIn is an array of VEC1_TRANSPOSE_WIDTH nCodeElem-dimension vectors in 
**   column major format. That is to say, pIn[0] is the first element of 
**   the first vector, pIn[1] is the first element of the second vector, 
**   and so on.
**
**   In other words, to find element i of vector v:
**
**     pIn[i * VEC1_TRANSPOSE_WIDTH + v]
**
** aVec:
**   A single vector with nCodeElem elements..
**
** nCodeElem:
**   Size of vectors in elements.
**
** aOut:
**   Array of VEC1_TRANSPOSE_WIDTH. Populated with the square of the L2 distance
**   between aVec[] and each of the vectors in pIn.
*/
static void vec1ModelTDist(
  const float *pIn,
  const float *aVec,
  int nCodeElem,
  float *aOut
){
  int d;

#if defined(VEC1_HAVE_AVX2)
  __m256 acc[ VEC1_TRANSPOSE_WIDTH / 8 ];
  const int nAcc = (VEC1_TRANSPOSE_WIDTH / 8);
  int c;

  for(c=0; c<nAcc; c++) acc[c] = _mm256_setzero_ps();
  for(d=0; d<nCodeElem; d++){
    __m256 v = _mm256_set1_ps(aVec[d]);
    for(c=0; c<nAcc; c++){
      __m256 diff = _mm256_sub_ps(v, LOADU(&pIn[c*8]));
      acc[c] = FMADD(diff, diff, acc[c]);
    }
    pIn += VEC1_TRANSPOSE_WIDTH;
  }
  for(c=0; c<nAcc; c++) _mm256_storeu_ps(&aOut[c*8], acc[c]);

#elif defined(VEC1_HAVE_NEON)
  float32x4_t acc[ VEC1_TRANSPOSE_WIDTH / 4 ];
  const int nAcc = (VEC1_TRANSPOSE_WIDTH / 4);
  int c;

  for(c=0; c<nAcc; c++) acc[c] = vdupq_n_f32(0.0f);
  for(d=0; d<nCodeElem; d++){
    float32x4_t v = vdupq_n_f32( aVec[d] );
    for(c=0; c<nAcc; c++){
      float32x4_t diff = vsubq_f32(v, vld1q_f32(&pIn[c*4]));
      acc[c] = vmlaq_f32(acc[c], diff, diff);
    }
    pIn += VEC1_TRANSPOSE_WIDTH;
  }
  for(c=0; c<nAcc; c++) vst1q_f32(&aOut[c*4], acc[c]);

#else
  memset(aOut, 0, sizeof_f32 * VEC1_TRANSPOSE_WIDTH);
  for(d=0; d<nCodeElem; d++){
    float vv = aVec[d];
    const float *aVC = pIn;
    int ii;
    for(ii=0; ii<VEC1_TRANSPOSE_WIDTH; ii++){
      aOut[ii] += ((vv - aVC[ii]) * (vv - aVC[ii]));
    }
    pIn += VEC1_TRANSPOSE_WIDTH;
  }
#endif
}

/*
** Write value v into buffer aBuf[] as an SQLite varint.
*/
static int vec1PutVarint(u8 *aBuf, u64 v){
  if( v<=0x7f ){
    aBuf[0] = v&0x7f;
    return 1;
  }
  if( v<=0x3fff ){
    aBuf[0] = ((v>>7)&0x7f)|0x80;
    aBuf[1] = v&0x7f;
    return 2;
  }
  {
    u8 a[10];
    int ii;
    int nRet;
    for(nRet=0; v; nRet++){
      a[nRet] = (v&0x7F);
      v = v >> 7;
    }
    for(ii=0; ii<nRet; ii++){
      aBuf[ii] = a[nRet-1-ii] | ((ii==nRet-1) ? 0x00 : 0x80);
    }
    return nRet;
  }
}

static int vec1GetVarint(const u8 *aBuf, u64 *piVal){
  int nRet = 0;
  u64 out = 0;
  do {
    out = (out<<7) + (aBuf[nRet] & 0x7F);
  } while( aBuf[nRet++] & 0x80 );
  *piVal = out;
  return nRet;
}

/*
** Populate an LUT to use to calculate distances when scanning a list of
** PQ codes. Parameter aVec[] is the (possibly residual) vector that will be 
** compared with the PQ encoded vectors.
*/
static int vec1PQLUTBuild(
  Vec1Tab *pTab,
  const float *aVec,              /* Residual vector */
  float *aDist                    /* Populate this nCodebook*256 array */
){
  const int nCodebook = pTab->mod.hdr.nCodebook;
  const int nCodeElem = pTab->mod.nCodeElem;
  int M, K;
  float *pIn = pTab->mod.aModelT;

  for(M=0; M<nCodebook; M++){
    for(K=0; K<VEC1_PQ_CODEBOOK_SZ; K+=VEC1_TRANSPOSE_WIDTH){
      vec1ModelTDist(
          pIn, &aVec[M*nCodeElem], nCodeElem, 
          &aDist[(M * VEC1_PQ_CODEBOOK_SZ) + K]
      );
      pIn += (VEC1_TRANSPOSE_WIDTH * nCodeElem);
    }
  }

  return SQLITE_OK;
}

/*
** Size of the LUT for a scan of BQ compressed nElem element vectors.
*/
static int vec1BinaryQuantSize(int nElem){
  const int B = 8;
  return (((nElem+(B-1))/B) * (1<<B) * sizeof_f32);
}

static int vec1PQLUTSize(int nCodebook){
  return (nCodebook * VEC1_PQ_CODEBOOK_SZ) * sizeof_f32;
}

static void vec1BinaryQuantLUTBuild(
  Vec1Tab *pTab,
  const float *aVec,              /* Residual vector */
  float *aDist                    /* Populate this nCodebook*256 array */
){
  int nVec = pTab->cfg.nElem;
  int nCodebook = pTab->mod.hdr.nCodebook;
  const int nCodeElem = pTab->mod.nCodeElem;

  int M;
  float c = 1.0f / sqrtf((float)nVec);

  assert( nCodeElem==8 || nCodeElem==16 );

  for(M=0; M<nCodebook; M++){
    float aZero[8];
    float *aLUT = &aDist[M*256];
    const float *aSub = &aVec[M*nCodeElem];
    int iNext = 1;

    int ii;
    aLUT[0] = 0.0;
    if( nCodeElem==8 ){
      for(ii=0; ii<8; ii++){
        assert( (ii+M*nCodeElem)<nVec || aSub[ii]==0.0 );
        aZero[ii] = (aSub[ii] * c);
        aLUT[0] -= aZero[ii];
      }
    }else{
      for(ii=0; ii<16; ii+=2){
        aZero[ii/2] = (aSub[ii] + aSub[ii+1]) * c;
        aLUT[0] -= aZero[ii/2];
      }
    }

    for(ii=0; ii<8; ii++){
      int jj;
      float add = 2 * aZero[ii];
      for(jj=0; jj<iNext; jj++){
        aLUT[iNext+jj] = (aLUT[jj] + add);
      }
      iNext = iNext+iNext;
    }
  }
}

/*
** Read an entry from the %_meta table into buffer pBuf.
**
** Return SQLITE_OK if successful, or an SQLite error code otherwise.
*/
static int vec1ReadMeta(
  Vec1Tab *pTab,                  /* Vec1 virtual table */
  Vec1Buffer *pBuf,               /* Buffer to read into */
  i64 iId,                        /* Id of %_idx row */
  int iMeta                       /* Index of meta-column */
){
  sqlite3_stmt *pStmt = 0;
  int rc = vec1GetSql(pTab, VEC1_SQL_READ_META, &pStmt);

  assert( iMeta>=0 && iMeta<VEC1_MAX_META_COLUMNS );

  if( rc==SQLITE_OK ){
    i64 iMetaId = (iId << VEC1_META_COLUMN_BITS) + iMeta;
    pBuf->n = 0;
    sqlite3_bind_int64(pStmt, 1, iMetaId);
    if( SQLITE_ROW==sqlite3_step(pStmt) ){
      int nByte = sqlite3_column_bytes(pStmt, 0);
      rc = vec1BufferGrow(pBuf, nByte);
      if( rc==SQLITE_OK ){
        const u8 *a = sqlite3_column_blob(pStmt, 0);
        memcpy(pBuf->a, a, nByte);
        pBuf->n = nByte;
      }
    }

    vec1StmtReset(&rc, pStmt);
    if( rc==SQLITE_OK && pBuf->n==0 ){
      rc = VEC1_CORRUPT;
    }
  }

  return rc;
}

/*
**        0: NULL value. 0 byte payload.
**        1: Integer value. 1 byte payload.
**        2: Integer value. 4 byte payload.
**        3: Integer value. 8 byte payload.
**        4: Real value. 8 byte payload.
*/
static int vec1MetaValueRead(
  Vec1Buffer *pMeta, 
  int *piOff, 
  Vec1MetaValue *pVal
){
  int iOff = *piOff;
  switch( pMeta->a[iOff] ){
    case 0: {
      pVal->eType = SQLITE_NULL;
      iOff++;
      break;
    }

    case 1: {
      pVal->eType = SQLITE_INTEGER;
      pVal->iVal = (i64)(u8)pMeta->a[iOff+1];
      iOff += 2;
      break;
    }

    case 2: {
      pVal->eType = SQLITE_INTEGER;
      pVal->iVal = (int)vec1GetU32(&pMeta->a[iOff+1]);
      iOff += 5;
      break;
    }

    case 3: {
      pVal->eType = SQLITE_INTEGER;
      pVal->iVal = (i64)vec1GetU64(&pMeta->a[iOff+1]);
      iOff += 9;
      break;
    }

    case 4: {
      u64 iVal = vec1GetU64(&pMeta->a[iOff+1]);
      memcpy(&pVal->fVal, &iVal, sizeof(u64));
      pVal->eType = SQLITE_FLOAT;
      iOff += 9;
      break;
    }

    default: {
      u64 eType = 0;
      iOff += vec1GetVarint(&pMeta->a[iOff], &eType);
      pVal->pPtr = &pMeta->a[iOff];
      pVal->iVal = (eType-5) / 2;
      iOff += (int)pVal->iVal;
      pVal->eType = (eType & 0x01) ? SQLITE_TEXT : SQLITE_BLOB;
      break;
    }
  }

  *piOff = iOff;
  return SQLITE_OK;
}

/*
** Compare numeric values a and b. Return -1, 0 or +1 if a is smaller than,
** equal to, or greater than b, respectively. i.e. (a) - (b).
*/
#define COMPARE(a, b) ((a)==(b) ? 0 : ((a)>(b) ? +1 : -1))

/*
** Return 0 if the meta value passes the filter, or 1 if it is excluded
** by the filter. This is the slow path that can do any operator for
** any types of values.
*/
static int vec1MetaValueFilter(Vec1Filter *pFilter, Vec1MetaValue *pMeta){
  int cmp = 0;

  if( pFilter->vop==VEC1_OP_IN ){
    int ii;
    for(ii=0; ii<pFilter->iVal; ii++){
      if( vec1MetaValueFilter(&pFilter[ii+1], pMeta)==0 ) return 0;
    }
    return 1;
  }

  if( pMeta->eType==SQLITE_INTEGER ){
    switch( pFilter->eType ){
      case SQLITE_NULL:  return 1;

      case SQLITE_INTEGER: {
        cmp = COMPARE(pFilter->iVal, pMeta->iVal);
        break;
      }
      case SQLITE_FLOAT: {
        cmp = COMPARE(pFilter->fVal, (double)pMeta->iVal);
        break;
      }
      default: {
        /* All text and blob are bigger than all numbers */
        cmp = +1;
        break;
      }
    }
  }
  else if( pMeta->eType==SQLITE_FLOAT ){
    switch( pFilter->eType ){
      case SQLITE_NULL:  return 1;

      case SQLITE_INTEGER: {
        cmp = COMPARE((double)pFilter->iVal, pMeta->fVal);
        break;
      }
      case SQLITE_FLOAT: {
        cmp = COMPARE(pFilter->fVal, pMeta->fVal);
        break;
      }
      default: {
        /* All text and blob are bigger than all numbers */
        cmp = +1;
        break;
      }
    }
  }else if( pMeta->eType==SQLITE_NULL ){
    if( pFilter->vop==VEC1_OP_IS && pFilter->eType==SQLITE_NULL ) return 0;
    return 1;
  }else if( pMeta->eType==SQLITE_TEXT ){
    switch( pFilter->eType ){
      case SQLITE_NULL: return 1;

      case SQLITE_INTEGER:
      case SQLITE_FLOAT: {
        cmp = -1;
        break;
      }

      case SQLITE_TEXT: {
        i64 n = MIN(pMeta->iVal, pFilter->iVal);
        if( n>0 ){
          cmp = memcmp(pFilter->pPtr, pMeta->pPtr, n);
        }
        if( cmp==0 ) cmp = COMPARE(pFilter->iVal, pMeta->iVal);
        break;
      }

      default: {
        assert( pFilter->eType==SQLITE_BLOB );
        cmp = 1;
        break;
      }
    }
  }else{
    assert( pMeta->eType==SQLITE_BLOB );
    switch( pFilter->eType ){
      case SQLITE_NULL: return 1;

      case SQLITE_INTEGER:
      case SQLITE_FLOAT: 
      case SQLITE_TEXT: {
        cmp = -1;
        break;
      }

      default: {
        i64 n = MIN(pMeta->iVal, pFilter->iVal);
        if( n>0 ) cmp = memcmp(pFilter->pPtr, pMeta->pPtr, n);
        if( cmp==0 ) cmp = COMPARE(pFilter->iVal, pMeta->iVal);
        assert( pFilter->eType==SQLITE_BLOB );
        break;
      }
    }
  }

  switch( pFilter->vop ){
    case VEC1_OP_IS:
    case VEC1_OP_EQ: return !(cmp==0);
    case VEC1_OP_LT: return !(cmp>0);
    case VEC1_OP_LE: return !(cmp>=0);
    case VEC1_OP_GT: return !(cmp<0);
    case VEC1_OP_GE: return !(cmp<=0);
  }

  assert( pFilter->vop==VEC1_OP_NOTNULL );
  return 0;
}

/*
** This is called as part of meta-data filtering when the meta-data field
** in question is stored in either VEC1_META_1BYTEINT or VEC1_META_4BYTEINT
** format.
**
** iMin, iMax, eNull:
**   These three parameters describe the integer format used to store
**   the meta-data field (either VEC1_META_1BYTEINT or VEC1_META_4BYTEINT).
**   They pass the minimum integer value, maximum possible integer value
**   and the value used to indicate NULL. So, for the two formats, they
**   are:
**
**     VEC1_META_1BYTEINT:   0, 254, 255.
**     VEC1_META_4BYTEINT:   -2147483648, +2147483646, +2147483647
**
** pOp:
**   Output parameter. If it is guaranteed that no entries in the meta-data
**   field will match the filter (i.e. so zero rows will be returned), set
**   this to 0. Otherwise, set it to one of the VEC1_OP_XXX constants to
**   indicate the operation that should be used to test each value in the
**   meta-data field.
**
** pRval:
**   OUTPUT parameter. If (*pOp) is VEC1_OP_IN, then this is set to the 
**   number of entries in the (*paRval) array output. Otherwise, it is
**   set to the integer value to use as the RHS of the comparison when
**   testing each meta-data value.
**
** paRval:
**   OUTPUT parameter. If (*pOp) is VEC1_OP_IN, an array of values to
**   compare each meta-data value to. Otherwise, 0.
**
** Return SQLITE_OK if successful, or an SQLite error code (SQLITE_NOMEM)
** if an error occurs.
*/
static int vec1FilterToIntOp(
  Vec1Filter *pFilter,            /* The constraint doing the filtering */
  i64 iMin, i64 iMax, i64 eNull,  /* Meta-data format description */
  int *pRval,
  int **paRval,
  u8 *pOp
){
  /* Set the following three values as follows:
  **
  **   res:  0 if the result can be determined to be false for all list
  **         entries. 1 if it is true for all non-null list entries. Or
  **         -1 if scanning the list using op and rval is required to
  **         determine which list entries match the condition.
  **
  **   op:   If res is -1, one of VEC1_OP_EQ, VEC1_OP_GT, VEC1_OP_GE,
  **         VEC1_OP_LT or VEC1_OP_LE.
  **
  **   rval: A value to compare using op with all list entries.
  **
  ** Then, if res is not 0 or 1, run through the list comparing each
  ** list value with value rval using operation op.
  */
  int res = -1;
  int rval = 0;
  u8 op = 0;

  assert( (iMin==0 && iMax==254 && eNull==255) ||  (
        iMin==VEC1_META_4BYTEMIN 
     && iMax==VEC1_META_4BYTEMAX 
     && eNull==VEC1_META_4BYTENULL
  ));

  if( pFilter->vop==VEC1_OP_IN ){
    int *aRval = (int*)vec1MallocZero(sizeof_u32 * (pFilter->iVal+1));
    if( aRval==0 ){
      return SQLITE_NOMEM;
    }else{
      int ii;
      int nRval = 0;
      for(ii=0; ii<pFilter->iVal; ii++){
        vec1FilterToIntOp(&pFilter[ii+1], iMin, iMax, eNull, &rval, 0, &op);
        if( op==VEC1_OP_EQ ){
          aRval[nRval++] = rval;
        }
      }
      op = VEC1_OP_IN;
      rval = nRval;
      *paRval = aRval;
    }
  }else if( pFilter->vop==VEC1_OP_NOTNULL ){
    res = 1;
  }else if( pFilter->eType==SQLITE_NULL && pFilter->vop==VEC1_OP_IS ){
    rval = (int)eNull;
    op = VEC1_OP_EQ;

  }else if( pFilter->eType!=SQLITE_NULL ){
    i64 iVal = pFilter->iVal;

    if( pFilter->eType==SQLITE_TEXT || pFilter->eType==SQLITE_BLOB ){
      /* All text and blob values are greater than all 1-byte ints */
      iVal = iMax+10;
    }else if( pFilter->eType==SQLITE_FLOAT ){
      iVal = (i64)pFilter->fVal;
      if( (double)iVal!=pFilter->fVal ){
        if( pFilter->vop==VEC1_OP_EQ || pFilter->vop==VEC1_OP_IS ){
          iVal = iMin-10;
        }else if( pFilter->vop==VEC1_OP_LT || pFilter->vop==VEC1_OP_GE ){
          iVal = iVal+1;
        }
      }
    }

    if( iVal<iMin ){
      res = (pFilter->vop==VEC1_OP_GT || pFilter->vop==VEC1_OP_GE);
    }else if( iVal>iMax ){
      res = (pFilter->vop==VEC1_OP_LT || pFilter->vop==VEC1_OP_LE);
    }else{
      rval = (int)iVal;
      op = (pFilter->vop==VEC1_OP_IS ? VEC1_OP_EQ : pFilter->vop);
    }
  }

  if( res>0 ){
    assert( eNull==(iMax+1) );
    op = VEC1_OP_LT;
    rval = (int)eNull;
  }

  *pRval = rval;
  *pOp = op;
  return SQLITE_OK;
}


static void vec1MetaFilterIntTail(
  const u8 *aMeta,
  int iBitOff,
  u8 *aBitmask,
  int nEntry,
  int szInt,
  int rval,
  int *aRval,
  u8 op
){
  const int eNull = szInt==1 ? VEC1_META_1BYTENULL : VEC1_META_4BYTENULL;
  int jj;
  assert( szInt==1 || szInt==4 );
  for(jj=0; jj<nEntry; jj++){
    int bSet = 0;
    int iVal;
    
    if( szInt==1 ){
      iVal = (int)aMeta[jj];
    }else{
      iVal = (int)vec1GetU32(&aMeta[jj*sizeof_u32]);
    }
    switch( op ){
      case VEC1_OP_EQ:
        bSet = (iVal==rval);
        break;

      case VEC1_OP_GT:
        bSet = (iVal>rval) && iVal!=eNull;
        break;

      case VEC1_OP_LT:
        bSet = (iVal<rval);
        break;

      case VEC1_OP_GE:
        bSet = (iVal>=rval) && iVal!=eNull;
        break;

      case VEC1_OP_LE:
        bSet = (iVal<=rval);
        break;

      default: {
        int ii;
        assert( op==VEC1_OP_IN );
        for(ii=0; ii<rval; ii++){
          if( iVal==aRval[ii] ){
            bSet = 1;
            break;
          }
        }
        break;
      }
    }

    if( bSet==0 ){
      int ii = jj+iBitOff;
      aBitmask[ii / 8] |= (1 << (ii % 8));
    }
  }
}

static void vec1MetaFilterScan1ByteArray(
  const u8 *a,
  u8 *aBitmask,
  int nEntry,
  int rval, int *aRval, u8 op
){
  int jj = 0;

#if defined(VEC1_HAVE_AVX2)
  __m256i rhs = _mm256_set1_epi8((char)rval);
  __m256i sign = _mm256_set1_epi8((char)0x80);
  __m256i null = _mm256_set1_epi8((char)VEC1_META_1BYTENULL);
  
  for(; jj<(nEntry-31); jj+=32){
    __m256i lhs = _mm256_loadu_si256((__m256i*)&a[jj]);
    u32 mask = 0;
    u32 prev = 0;
  
    switch( op ){
      case VEC1_OP_EQ: {
        __m256i cmp = _mm256_cmpeq_epi8(lhs, rhs);
        mask = (u32)_mm256_movemask_epi8(cmp);
        break;
      }
  
      case VEC1_OP_GT: {
        __m256i cmp = _mm256_cmpgt_epi8(
            _mm256_xor_si256(sign, lhs),
            _mm256_xor_si256(sign, rhs) 
        );
        mask = (u32)_mm256_movemask_epi8(cmp);
        cmp = _mm256_cmpeq_epi8(lhs, null);
        mask &= ~(u32)_mm256_movemask_epi8(cmp);
        break;
      }
  
      case VEC1_OP_LT: {
        __m256i cmp = _mm256_cmpgt_epi8(
            _mm256_xor_si256(sign, rhs),
            _mm256_xor_si256(sign, lhs)
        );
        mask = (u32)_mm256_movemask_epi8(cmp);
        break;
      }
  
      case VEC1_OP_GE: {
        __m256i cmp = _mm256_cmpgt_epi8(
            _mm256_xor_si256(sign, rhs),
            _mm256_xor_si256(sign, lhs)
        );
        mask = ~(u32)_mm256_movemask_epi8(cmp);
        cmp = _mm256_cmpeq_epi8(lhs, null);
        mask &= ~(u32)_mm256_movemask_epi8(cmp);
        break;
      }
  
      case VEC1_OP_LE: {
        __m256i cmp = _mm256_cmpgt_epi8(
            _mm256_xor_si256(sign, lhs),
            _mm256_xor_si256(sign, rhs)
        );
        mask = ~(u32)_mm256_movemask_epi8(cmp);
        break;
      }

      default: {
        int ii;
        assert( op==VEC1_OP_IN );
        for(ii=0; ii<rval; ii++){
          __m256i rhs2 = _mm256_set1_epi8((char)aRval[ii]);
          __m256i cmp = _mm256_cmpeq_epi8(lhs, rhs2);
          mask |= (u32)_mm256_movemask_epi8(cmp);
        }
        break;
      }
  
    }
  
    memcpy(&prev, &aBitmask[jj/8], sizeof_u32);
    mask = (~mask | prev);
    memcpy(&aBitmask[jj/8], &mask, sizeof_u32);
  }
#endif

#if defined(VEC1_HAVE_NEON)
  uint8x16_t rhs_n  = vdupq_n_u8((uint8_t)rval);
  uint8x16_t sign_n = vdupq_n_u8(0x80);
  uint8x16_t null_n = vdupq_n_u8((uint8_t)VEC1_META_1BYTENULL);

  for(; jj<(nEntry-15); jj+=16){
    uint8x16_t lhs = vld1q_u8((const uint8_t*)&a[jj]);
    uint8x16_t cmp;

    u8 m1 = 0;
    u8 m2 = 0;

    switch( op ){
      case VEC1_OP_IN: {
        int ii;
        cmp = vdupq_n_u8(0);
        for(ii=0; ii<rval; ii++){
          uint8x16_t r2 = vdupq_n_u8((uint8_t)aRval[ii]);
          cmp = vorrq_u8(cmp, vceqq_u8(lhs, r2));
        }
        break;
      }
      case VEC1_OP_EQ: {
        cmp = vceqq_u8(lhs, rhs_n);
        break;
      }

      case VEC1_OP_GT: {
        cmp = vcgtq_u8(lhs, rhs_n);
        cmp = vbicq_u8(cmp, vceqq_u8(lhs, null_n));
        break;
      }
      case VEC1_OP_LT: {
        cmp = vcltq_u8(lhs, rhs_n);
        break;
      }
      case VEC1_OP_GE: {
        cmp = vcgeq_u8(lhs, rhs_n);
        cmp = vbicq_u8(cmp, vceqq_u8(lhs, null_n));
        break;
      }
      case VEC1_OP_LE: {
        cmp = vcleq_u8(lhs, rhs_n);
        break;
      }
      default:
        cmp = vdupq_n_u8(0);
        break;
    }

    {
      static const uint8_t weights[8] = {1,2,4,8,16,32,64,128};
      uint8x8_t w   = vld1_u8(weights);
      uint8x8_t lo  = vget_low_u8(cmp);
      uint8x8_t hi  = vget_high_u8(cmp);
      lo = vshr_n_u8(lo, 7);                     /* 0xFF->0x01 */
      hi = vshr_n_u8(hi, 7);
      m1 = (u16)vaddlvq_u16(vmull_u8(lo, w));
      m2 = (u16)vaddlvq_u16(vmull_u8(hi, w));
    }

    aBitmask[(jj/8)] |= ~m1;
    aBitmask[(jj/8)+1] |= ~m2;
  }
#endif

  vec1MetaFilterIntTail(
      &a[jj], jj, aBitmask, nEntry-jj, 1, rval, aRval, op
  );
}

static void vec1MetaFilterScan4ByteArray(
  const int *a,
  u8 *aBitmask,
  int nEntry,
  int rval, int *aRval, u8 op
){
  int jj = 0;

#if defined(VEC1_HAVE_AVX2)
  const __m256i rhs = _mm256_set1_epi32(rval);
  const __m256i null = _mm256_set1_epi32(VEC1_META_4BYTENULL);

  /* Shuffle mask used to swap the byte-order of the 32-bit integers
  ** in this meta-data list if required. There are no big-endian AVX2
  ** systems, so no need for a runtime check here.  */
  const __m256i swap = _mm256_setr_epi8(
      3,2,1,0,  7,6,5,4,  11,10,9,8,  15,14,13,12,
      3,2,1,0,  7,6,5,4,  11,10,9,8,  15,14,13,12
  );
  
  for(; jj<(nEntry-7); jj+=8){
    __m256i lhs = _mm256_loadu_si256((__m256i*)&a[jj]);
    u8 mask = 0;

    /* Shuffle the 8 32-bit int values to little-endian order */
    lhs = _mm256_shuffle_epi8(lhs, swap);
  
    switch( op ){

      case VEC1_OP_EQ: {
        __m256i cmp = _mm256_cmpeq_epi32(lhs, rhs);
        mask = (u8)_mm256_movemask_ps(_mm256_castsi256_ps(cmp));
        break;
      }
  
      case VEC1_OP_GT: {
        __m256i cmp = _mm256_cmpgt_epi32(lhs, rhs);
        mask = (u8)_mm256_movemask_ps(_mm256_castsi256_ps(cmp));
        cmp = _mm256_cmpeq_epi32(lhs, null);
        mask &= ~(u8)_mm256_movemask_ps(_mm256_castsi256_ps(cmp));
        break;
      }
  
      case VEC1_OP_LT: {
        __m256i cmp = _mm256_cmpgt_epi32(rhs, lhs);
        mask = (u8)_mm256_movemask_ps(_mm256_castsi256_ps(cmp));
        break;
      }
  
      case VEC1_OP_GE: {
        __m256i cmp = _mm256_cmpgt_epi32(rhs, lhs);
        mask = ~(u8)_mm256_movemask_ps(_mm256_castsi256_ps(cmp));
        cmp = _mm256_cmpeq_epi32(lhs, null);
        mask &= ~(u8)_mm256_movemask_ps(_mm256_castsi256_ps(cmp));
        break;
      }
  
      case VEC1_OP_LE: {
        __m256i cmp = _mm256_cmpgt_epi32(lhs, rhs);
        mask = ~(u8)_mm256_movemask_ps(_mm256_castsi256_ps(cmp));
        break;
      }

      default: {
        int ii;
        assert( op==VEC1_OP_IN );
        for(ii=0; ii<rval; ii++){
          __m256i rhs2 = _mm256_set1_epi32(aRval[ii]);
          __m256i cmp = _mm256_cmpeq_epi32(lhs, rhs2);
          mask |= (u8)_mm256_movemask_ps(_mm256_castsi256_ps(cmp));
        }
        break;
      }
  
    }
  
    mask = ~mask;
    aBitmask[jj/8] |= mask;
  }
#endif
#if defined(VEC1_HAVE_NEON)
  const int32x4_t rhs  = vdupq_n_s32(rval);
  const int32x4_t null = vdupq_n_s32(VEC1_META_4BYTENULL);

  for(; jj<(nEntry-3); jj+=4){
    uint32x4_t cmp = vdupq_n_u32(0);
    int32x4_t lhs = 
      vreinterpretq_s32_u8(vrev32q_u8(vld1q_u8((uint8_t*)&a[jj]))
    );

    /* cmp is a vector of 4 u32 values. This block sets each value to
    ** 0xFFFFFFFF if the constraint is true for the corresponding meta-data
    ** value (i.e. if the row will not be filtered out) or to 0x00000000 
    ** otherwise.  */
    switch( op ){
      case VEC1_OP_IN: {
        int ii;
        for(ii=0; ii<rval; ii++){
          int32x4_t rhs2 = vdupq_n_s32(aRval[ii]);
          cmp = vorrq_u32(cmp, vceqq_s32(lhs, rhs2));
        }
        break;
      }
      case VEC1_OP_EQ: {
        cmp = vceqq_s32(lhs, rhs);
        break;
      }
      case VEC1_OP_GT: {
        cmp = vcgtq_s32(lhs, rhs);
        cmp = vbicq_u32(cmp, vceqq_s32(lhs, null));
        break;
      }
      case VEC1_OP_LT: {
        cmp = vcltq_s32(lhs, rhs);
        break;
      }
      case VEC1_OP_GE: {
        cmp = vcgeq_s32(lhs, rhs);
        cmp = vbicq_u32(cmp, vceqq_s32(lhs, null));
        break;
      }
      case VEC1_OP_LE: {
        cmp = vcleq_s32(lhs, rhs);
        break;
      }
    }

    /* Collapse 4 x 32-bit mask lanes to a 4-bit nibble.
    ** NEON has no movemask, so shift the high bit of each lane
    ** into position and OR them together. */
    u32 mask = (u8)(
      (vgetq_lane_u32(cmp, 0) & 0x01) |
      (vgetq_lane_u32(cmp, 1) & 0x02) |
      (vgetq_lane_u32(cmp, 2) & 0x04) |
      (vgetq_lane_u32(cmp, 3) & 0x08) 
    );
    mask = (~mask) & 0xF;
    aBitmask[jj/8] |= (jj & 4) ? (mask << 4) : mask;
  }
#endif

  vec1MetaFilterIntTail(
      (const u8*)&a[jj], jj, aBitmask, nEntry-jj, 4, rval, aRval, op
  );
}

/*
** Buffer pMeta contains an nEntry entry meta-value list in either
** 1BYTEINT (if szInt==1) or 4BYTEINT (if szInt==4) format. This function 
** runs filter pFilter over the list, and sets the corresponding bit in
** pBitmask for each entry that does not match the filter (i.e. that
** should be excluded from the query results).
**
** SQLITE_OK is returned if successful, or an error code otherwise.
*/
static int vec1MetaFilterIntList(
  Vec1Filter *pFilter,
  int szInt,
  int nEntry,
  Vec1Buffer *pMeta,
  Vec1Buffer *pBitmask
){
  int rc = SQLITE_OK;
  if( pMeta->n<(VEC1_META_SZHDR+(nEntry*szInt)) ){
    rc = VEC1_CORRUPT;
  }else{
    int *aRval = 0;
    int rval = 0;
    u8 op = 0;

    rc = vec1FilterToIntOp(pFilter,
        (szInt==4 ? VEC1_META_4BYTEMIN : 0), 
        (szInt==4 ? VEC1_META_4BYTEMAX : 254),
        (szInt==4 ? VEC1_META_4BYTENULL : VEC1_META_1BYTENULL),
        &rval, &aRval, &op
    );
    if( rc==SQLITE_OK ){
      if( op==0 ){
        /* No entries will match. Set the "filtered-out" bit for all rows. */
        memset(pBitmask->a, 0xFF, pBitmask->n);
      }else if( szInt==4 ){
        const int *a = (const int*)&pMeta->a[VEC1_META_SZHDR];
        vec1MetaFilterScan4ByteArray(a, pBitmask->a, nEntry, rval, aRval, op);
      }else{
        assert( szInt==1 );
        const u8 *a = (const u8*)&pMeta->a[VEC1_META_SZHDR];
        vec1MetaFilterScan1ByteArray(a, pBitmask->a, nEntry, rval, aRval, op);
      }
    }
    sqlite3_free(aRval);
  }

  return rc;
}

static VEC1_NOINLINE int vec1DoMetaFilters(
  Vec1Tab *pTab, 
  Vec1Query *pQuery, 
  i64 iId, 
  int nEntry, 
  Vec1Buffer *pMeta,
  Vec1Buffer *pBitmask
){
  int rc = SQLITE_OK;
  int ii;

  /* Grow and zero the bitmask array. */
  int nByte = (nEntry+7)/8;
  pBitmask->n = 0;
  rc = vec1BufferGrow(pBitmask, nByte);
  if( rc==SQLITE_OK ){
    memset(pBitmask->a, 0, nByte);
  }
  pBitmask->n = nByte;

  assert( pQuery->nFilter>0 );

  for(ii=0; rc==SQLITE_OK && ii<pQuery->nFilter; ii++){
    Vec1Filter *pFilter = &pQuery->aFilter[ii];
    VEC1_QINSTR_START(pTab, VEC1_QINSTR_METAREAD);
    rc = vec1ReadMeta(pTab, pMeta, iId, pFilter->iMeta);
    VEC1_QINSTR_STOP(pTab, VEC1_QINSTR_METAREAD);

    VEC1_QINSTR_START(pTab, VEC1_QINSTR_METASCAN);
    if( rc==SQLITE_OK ){
      int jj = 0;
      u32 f = vec1GetU32(pMeta->a);

      if( f & VEC1_META_1BYTEINT ){
        /* Single-byte integer format */
        rc = vec1MetaFilterIntList(pFilter, 1, nEntry, pMeta, pBitmask);
      }else if( f & VEC1_META_4BYTEINT ){
        /* 4-byte integer format */
        rc = vec1MetaFilterIntList(pFilter, 4, nEntry, pMeta, pBitmask);
      }else if( f & VEC1_META_REAL ){
        Vec1MetaValue val = {0, 0, 0, 0};
        for(jj=0; jj<nEntry; jj++){
          u64 iVal = vec1GetU64(
              &pMeta->a[VEC1_META_SZHDR+jj*sizeof_f64]
          );

          if( iVal==VEC1_META_REALNULL ){
            val.eType = SQLITE_NULL;
          }else{
            val.eType = SQLITE_FLOAT;
            memcpy(&val.fVal, &iVal, sizeof(u64));
          }

          if( vec1MetaValueFilter(pFilter, &val) ){
            pBitmask->a[jj / 8] |= (1 << (jj % 8));
          }
        }
      }else{
        /* Generic format */
        int iOff = VEC1_META_SZHDR;
        for(jj=0; jj<nEntry; jj++){
          Vec1MetaValue val;
          vec1MetaValueRead(pMeta, &iOff, &val);
          if( vec1MetaValueFilter(pFilter, &val) ){
            pBitmask->a[jj / 8] |= (1 << (jj % 8));
          }
        }
      }
    }

    if( pFilter->vop==VEC1_OP_IN ){
      ii += (int)pFilter->iVal;
    }
    VEC1_QINSTR_STOP(pTab, VEC1_QINSTR_METASCAN);
  }
  return rc;
}

static void vec1EncodeVectorBit(
  const Vec1Model *pMod,          /* Model */
  const float *aVec,              /* Input vector */
  u8 *aCode                       /* OUT: Encoded vector */
){
  const int nElem = (int)pMod->hdr.nElem;
  float norm;
  int ii;
  u8 *pOut;

  assert( pMod->hdr.flags & VEC1_MODEL_BITQUANT );
  norm = (float)vec1VectorNorm2(aVec, nElem);
  norm = sqrtf(norm);
  memcpy(aCode, &norm, sizeof_f32);

  assert( pMod->nCodeElem==8 || pMod->nCodeElem==16 );

  pOut = &aCode[4];
  for(ii=0; ii<nElem; ii+=pMod->nCodeElem){
    u8 v = 0;
    int jj;
    if( pMod->nCodeElem==8 ){
      for(jj=0; jj<8; jj++){
        if( aVec[ii+jj]>0.0 ){
          v |= (1<<jj);
        }
      }
    }else{
      for(jj=0; jj<8; jj+=1){
        if( (aVec[ii+jj*2]+aVec[ii+jj*2+1])>0.0 ){
          v |= (1<<jj);
        }
      }
    }

    *pOut = v;
    pOut++;
  }
}

static void vec1EncodeVectorPQ(
  const Vec1Model *pMod,
  const float *aVec,
  u8 *aCode
){
  const int nCodebook = pMod->hdr.nCodebook;
  const int nCodeElem = pMod->nCodeElem;
  const float *pIn = pMod->aModelT;
  int M, K;

  for(M=0; M<nCodebook; M++){
    float fBestDist = INFINITY;
    for(K=0; K<VEC1_PQ_CODEBOOK_SZ; K+=VEC1_TRANSPOSE_WIDTH){
      int ii;
      float aAcc[VEC1_TRANSPOSE_WIDTH];

      vec1ModelTDist(pIn, &aVec[M*nCodeElem], nCodeElem, aAcc);
      pIn += (VEC1_TRANSPOSE_WIDTH * nCodeElem);

      for(ii=0; ii<VEC1_TRANSPOSE_WIDTH; ii++){
        if( aAcc[ii]<fBestDist ){
          fBestDist = aAcc[ii];
          aCode[M] = (u8)(K+ii);
        }
      }
    }
  }
}

static void vec1EncodeVectorPQSimple(
  const Vec1Model *pMod,          /* Model to encode with */
  const float *aVec,              /* Vector to encode */
  u8 *aOut,                       /* OUT: Quantized vector */
  double *pfTotalDist             /* OUT: reconstruction L2 squared from orig */
){
  const int nCodebook = (int)pMod->hdr.nCodebook;
  const int nCodeElem = pMod->nCodeElem;
  int ii = 0;

  for(ii=0; ii<nCodebook; ii++){
    const float *pCodebook = &pMod->aModel[ii*nCodeElem*VEC1_PQ_CODEBOOK_SZ];
    const float *aSub = &aVec[ii*nCodeElem];
    aOut[ii] = (u8)vec1PqBestMatch(
        pCodebook, VEC1_PQ_CODEBOOK_SZ, aSub, nCodeElem, pfTotalDist
    );
  }
}


static void vec1EncodeVector(
  const Vec1Model *pMod,
  const float *aVec,
  u8 *aCode
){
  if( pMod->hdr.flags & VEC1_MODEL_BITQUANT ){
    vec1EncodeVectorBit(pMod, aVec, aCode);
  }else{
    vec1EncodeVectorPQ(pMod, aVec, aCode);
  }
}

/*
** Buffer aCode[] contains a vector quantized according to model pMod.
** This function (lossily) reconstructs the original vector and writes
** it to buffer aVec[] before returning.
*/
static void vec1DecodeVector(
  const Vec1Model *pMod,          /* Model used to encode vector */
  const u8 *aCode,                /* Encoded vector */
  float *aVec                     /* Populate this buffer with full vector */
){
  const int nElem = (int)pMod->hdr.nElem;
  if( pMod->hdr.flags & VEC1_MODEL_BITQUANT ){
    float fNorm = 0.0;
    float fVal = 0.0;
    int ii;

    /* The first 4 bytes of the encoded vector store the norm of the vector,
    ** as a 32-bit float. */
    memcpy(&fNorm, aCode, sizeof_f32);

    /* The reconstructed vector uses the following value as the magnitude 
    ** of each dimension. The value is -ve if the corresponding bit is clear,
    ** or +ve if it is set.  */
    fVal = (float)sqrt((fNorm*fNorm) / nElem);

    assert( pMod->nCodeElem==8 || pMod->nCodeElem==16 );
    for(ii=0; ii<nElem; ii++){
      if( pMod->nCodeElem==8 ){
        if( aCode[sizeof_f32 + (ii/8)] & (1 << (ii%8)) ){
          aVec[ii] = fVal;
        }else{
          aVec[ii] = -1 * fVal;
        }
      }else{
        if( aCode[sizeof_f32 + (ii/16)] & (1 << ((ii/2)%8)) ){
          aVec[ii] = fVal;
        }else{
          aVec[ii] = -1 * fVal;
        }
      }
    }
  }else{
    int ii;
    for(ii=0; ii<(int)pMod->hdr.nCodebook; ii++){
      const float *aCopy = &pMod->aModel[
        (ii * VEC1_PQ_CODEBOOK_SZ + aCode[ii]) * pMod->nCodeElem
      ];
      int nCopy = MIN(pMod->nCodeElem, nElem - ii*pMod->nCodeElem);
      memcpy(&aVec[ii * pMod->nCodeElem], aCopy, nCopy*sizeof_f32);
    }
  }
}

/*
** Return true if any value in aAcc[] is less than fMin. False otherwise.
*/
static int vec1AnyLessThan(const float aAcc[16], float fMin){
#if defined(VEC1_HAVE_AVX2)
  __m256 vMin = _mm256_set1_ps(fMin);
  __m256 cmp0 = _mm256_cmp_ps(_mm256_loadu_ps(aAcc), vMin, _CMP_LT_OQ);
  __m256 cmp1 = _mm256_cmp_ps(_mm256_loadu_ps(aAcc+8), vMin, _CMP_LT_OQ);
  __m256 mask = _mm256_or_ps(cmp0, cmp1);
  return _mm256_movemask_ps(mask)!=0;
#elif defined(VEC1_HAVE_NEON)
  float32x4_t vMin = vdupq_n_f32(fMin);
  uint32x4_t cmp0 = vcltq_f32(vld1q_f32(aAcc),    vMin);
  uint32x4_t cmp1 = vcltq_f32(vld1q_f32(aAcc+4),  vMin);
  uint32x4_t cmp2 = vcltq_f32(vld1q_f32(aAcc+8),  vMin);
  uint32x4_t cmp3 = vcltq_f32(vld1q_f32(aAcc+12), vMin);
  uint32x4_t mask = vorrq_u32(vorrq_u32(cmp0, cmp1), vorrq_u32(cmp2, cmp3));
  return vmaxvq_u32(mask)!=0;
#else
  int ii;
  for(ii=0; ii<16; ii++){
    if( aAcc[ii]<fMin ) return 1;
  }
  return 0;
#endif
}

static void vec1ScanBlocked(
  Vec1AnnHeap *pHeap,
  u8 *aMask,
  const float *aDist,
  int nCodebook,
  const u8 *aBlob, int nBlob,
  int szRowid,
  int nEntry,
  int nNorm,
  float fResidualNorm2
){
  const int szBlk = VEC1_PQ_BLOCKSIZE * (nCodebook + nNorm);
  int iOff = 0;
  int iBlk = 0;

  int iEarly = nCodebook;
  if( nNorm==0 && pHeap->bStreaming==0 ){
    iEarly = (nCodebook/4);
  }

  assert( nNorm==sizeof_f32 || nNorm==0 );

  /* One iteration of this loop for each block in the blob. */
  for(iOff=VEC1_LIST_SZHDR + nEntry*szRowid;
      iOff<nBlob; 
      iOff+=szBlk, iBlk++
  ){
    int ii;
    const u8 *aBlk = &aBlob[iOff];
    const u8 *aRowid = &aBlob[VEC1_LIST_SZHDR + iBlk*VEC1_PQ_BLOCKSIZE*szRowid];
    int nSlot = MIN(VEC1_PQ_BLOCKSIZE, (nEntry - iBlk*VEC1_PQ_BLOCKSIZE));
    int iBook;
    float aAcc[VEC1_PQ_BLOCKSIZE];

    memset(aAcc, 0, sizeof(aAcc));
    for(iBook=0; iBook<nCodebook; iBook++){
      const float *aTable = &aDist[iBook * VEC1_PQ_CODEBOOK_SZ];
      const u8 *aCode = &aBlk[(iBook + nNorm) * VEC1_PQ_BLOCKSIZE];
      int jj;
      for(jj=0; jj<VEC1_PQ_BLOCKSIZE; jj++){
        aAcc[jj] += aTable[ aCode[jj] ];
      }

      /* If all 16 distances sums are already greater than the required
      ** minimum, quit processing this block early.  */
      if( iBook>=iEarly && (iBook & 0x03)==0x03
       && vec1AnyLessThan(aAcc, (float)pHeap->fMin)==0
      ){
        goto skip;
      }
    }

    if( nNorm ){
      int jj;
      for(jj=0; jj<nSlot; jj++){
        float norm;
        memcpy(&norm, &aBlk[jj*sizeof_f32], sizeof_f32);
        aAcc[jj] = (float)(fResidualNorm2 + (norm - 2.0 * aAcc[jj]) * norm);
      }
    }

    for(ii=0; ii<nSlot; ii++){
      float dist = aAcc[ii];

      if( dist<pHeap->fMin 
      && (aMask==0 || (aMask[(iBlk*VEC1_PQ_BLOCKSIZE+ii)/8] & (1 << (ii%8)))==0)
      ){
        i64 iRowid = 0; 
        if( szRowid==4 ){
          iRowid = vec1GetU32(&aRowid[ii * szRowid]);
          if( iRowid==VEC1_TOMBSTONE_32 ) continue;
        }else{
          iRowid = vec1GetU64(&aRowid[ii * szRowid]);
          if( iRowid==VEC1_TOMBSTONE_64 ) continue;
        }
        vec1HeapInsert(pHeap, iRowid, dist);
      }
    }

    skip:;         /* semi-colon required to avoid C23 warning/error */
  }
}

static int vec1DoKANNBucket(
  Vec1Query *pQuery,
  int iBucket,
  Vec1Buffer *pDist,
  Vec1Buffer *pMeta,
  Vec1Buffer *pBitmask
){
  Vec1Tab *pTab = pQuery->pTab;
  Vec1AnnHeap *pHeap = &pQuery->heap;

  const int nElem = pTab->cfg.nElem;
  const int bUseBQ = (pTab->mod.hdr.flags & VEC1_MODEL_BITQUANT) ? 1 : 0;
  const int nCodebook = (int)pTab->mod.hdr.nCodebook;
  const int bUsePQ = (nCodebook>0 && bUseBQ==0);

  sqlite3_stmt *pScanIdx = 0;
  int rc = SQLITE_OK;
  float *aDist = 0;
  float fResidualNorm2 = 0.0;

  if( iBucket<0 ){
    rc = vec1GetSql(pTab, VEC1_SQL_SCAN_IDX, &pScanIdx);
  }else{
    rc = vec1GetSql(pTab, VEC1_SCAN_BUCKET, &pScanIdx);
    if( rc==SQLITE_OK ){
      sqlite3_bind_int(pScanIdx, 1, iBucket);
    }
  }

  assert( 0==pDist->n || 0==(pTab->mod.hdr.flags & VEC1_MODEL_RESIDUAL) );
  if( (bUsePQ || bUseBQ) ){
    /* If using PQ or BQ codes, build a LUT for this bucket. */
    if( rc==SQLITE_OK && pDist->n==0 ){
      const float *aEncode = pQuery->aTransform;
      int nByte = (bUsePQ ? 
        vec1PQLUTSize(nCodebook)   : /* Product quant */
        vec1BinaryQuantSize(nElem)   /* Binary quant */
      );

      rc = vec1BufferSize(pDist, nByte);
      if( rc!=SQLITE_OK ) return rc;
      aDist = (float*)pDist->a;

      VEC1_QINSTR_START(pTab, VEC1_QINSTR_LUT);
      if( pTab->mod.hdr.flags & VEC1_MODEL_RESIDUAL ){
        float *aTmp = pTab->aTmpVec;
        assert( iBucket>=0 && iBucket<(int)pTab->mod.hdr.nBucket );
        vec1Sub(aTmp, aEncode, &pTab->mod.aCentroid[iBucket*nElem], nElem);
        aEncode = aTmp;
      }
      if( bUsePQ ){
        vec1PQLUTBuild(pTab, aEncode, aDist);
      }else{
        vec1BinaryQuantLUTBuild(pTab, aEncode, aDist);
        fResidualNorm2 = (float)vec1VectorNorm2(aEncode, pTab->cfg.nElem);
        vec1EncodeVectorBit(&pTab->mod, aEncode, pQuery->aBit);
      }
      VEC1_QINSTR_STOP(pTab, VEC1_QINSTR_LUT);
      pDist->n = nByte;
    }
    aDist = (float*)pDist->a;
  }

  while( rc==SQLITE_OK ){
    i64 iId = 0;
    int res;
      
    /* Load the next index entry from the %_idx table */
    VEC1_QINSTR_START(pTab, VEC1_QINSTR_IDXREAD);
    res = sqlite3_step(pScanIdx);
    VEC1_QINSTR_STOP(pTab, VEC1_QINSTR_IDXREAD);
    if( res!=SQLITE_ROW ) break;

    const u8 *aBlob = (const u8*)sqlite3_column_blob(pScanIdx, 0);
    int nBlob = sqlite3_column_bytes(pScanIdx, 0);
    int nEntry = 0;
    int flags = 0;
    int ii;
    int szRowid = 4;

    if( vec1CheckIdxSize(pTab, aBlob, nBlob) ){
      rc = VEC1_CORRUPT;
      break;
    }

    nEntry = vec1GetU32(&aBlob[4]);
    iId = sqlite3_column_int64(pScanIdx, 2);
    if( pQuery->nFilter>0 ){
      rc = vec1DoMetaFilters(pTab, pQuery, iId, nEntry, pMeta, pBitmask);
      if( rc!=SQLITE_OK ) break;
    }

    /* Loop through each of the PQ vectors in aBlob[] */
    VEC1_QINSTR_START(pTab, VEC1_QINSTR_PQSCAN);
    flags = vec1GetU32(&aBlob[0]);
    if( flags & VEC1_LIST_64BIT ) szRowid = 8;
    if( aDist ){
      vec1ScanBlocked(
          pHeap, pBitmask->a, aDist, nCodebook, aBlob, nBlob, szRowid, 
          nEntry, (bUseBQ ? sizeof_f32 : 0), fResidualNorm2
      );
    }else{
      const int szVec = sizeof_f32*nElem;
      assert( bUseBQ==0 && bUsePQ==0 );
      for(ii=0; ii<nEntry; ii++){
        int iRowidOff = VEC1_LIST_SZHDR + ii*szRowid;
        int iVecOff = VEC1_LIST_SZHDR + nEntry*szRowid + ii*szVec;
        const float *aFull = (const float*)&aBlob[iVecOff];
        double fDist = 0.0;
        i64 iRowid; 

        /* Check if meta-value filters excluded this row. If so, skip
        ** directly to the next iteration of the loop.  */
        if( pBitmask->n>0 && (pBitmask->a[ii/8] & (1 << (ii % 8))) ){
          continue;
        }

        if( szRowid==4 ){
          iRowid = vec1GetU32(&aBlob[iRowidOff]);
          if( iRowid==VEC1_TOMBSTONE_32 ) continue;
        }else{
          iRowid = vec1GetU64(&aBlob[iRowidOff]);
          if( iRowid==VEC1_TOMBSTONE_64 ) continue;
        }

        if( pTab->mod.hdr.eDistance==VEC1_DISTANCE_COS ){
          fDist = vec1CosDist(pQuery->aVector, aFull, nElem);
        }else{
          fDist = vec1L2Dist(pQuery->aVector, aFull, nElem);
        }

        vec1HeapInsert(pHeap, iRowid, fDist);
      }
    }
    VEC1_QINSTR_STOP(pTab, VEC1_QINSTR_PQSCAN);
  }

  vec1StmtReset(&rc, pScanIdx);
  if( rc==SQLITE_OK ) rc = pHeap->rc;
  return rc;
}

static int vec1DoKANNQuery(Vec1Csr *pCsr){
  int rc = SQLITE_OK;
  Vec1Tab *pTab = (Vec1Tab*)(pCsr->base.pVtab);
  Vec1Query *pQuery = pCsr->pQuery;
  Vec1AnnHeap *pHeap = &pQuery->heap;

  /* Table of distances used for ANN queries against the PQ quantized 
  ** vectors in the %_idx table. This is an array of (nCodebook * 256)
  ** float values. The 256 distances for each codebook are stored
  ** contiguously, so to lookup code iCode in codebook iBook:
  **
  **       aDist[(iBook * 256) + iCode]
  */

  Vec1BucketResult *aBucket = 0;
  const float *aVector = pQuery->aVector;
  int iProbe = 0;

  Vec1Buffer meta = {0, 0, 0};    /* Buffer to load meta arrays */
  Vec1Buffer bitmask = {0, 0, 0}; /* Buffer for meta filter bitmask */
  Vec1Buffer dist = {0, 0, 0};    /* Buffer for LUT */

  VEC1_QINSTR_START(pTab, VEC1_QINSTR_INITQUERY);

  /* Transform the input vector if required. If no transformation is 
  ** required, then pQuery->aTransform already points to the same buffer 
  ** as pQuery->aVector.  */
  assert( vec1TransformRequired(&pTab->mod)==(pQuery->aTransform!=aVector) );
  vec1TransformInputVector(&pTab->mod, pQuery->aTransform, aVector);

  rc = vec1HeapInit(pHeap, pQuery->K, pQuery->bStreaming);

  VEC1_QINSTR_STOP(pTab, VEC1_QINSTR_INITQUERY);

  /* Do the coarse quantizer step. If nbucket is not 0, this block allocates
  ** the aBucket[] array. The first nProbe elements of which are the ones
  ** that should be scanned by this call. The others may be used later if
  ** this is a streaming query, but are usually ignored.  */
  VEC1_QINSTR_START(pTab, VEC1_QINSTR_COARSE);
  if( rc==SQLITE_OK ){
    if( pTab->mod.hdr.nBucket==0 ){
      assert( pQuery->nProbe==1 );
    }else{
      assert( pQuery->nProbe>=1 && pQuery->nProbe<=(int)pTab->mod.hdr.nBucket );
      rc = vec1FindBuckets(pTab, pQuery->aTransform, pQuery->nProbe, &aBucket);
    }
  }
  VEC1_QINSTR_STOP(pTab, VEC1_QINSTR_COARSE);

  for(iProbe=0; iProbe<pQuery->nProbe && rc==SQLITE_OK; iProbe++){
    int iBucket = -1;
    if( pTab->mod.hdr.nBucket>0 ){
      iBucket = aBucket[iProbe].iBucket;
      if( pQuery->nProbeSlack>0.0 
       && aBucket[iProbe].fDist>aBucket[0].fDist*(1.0+pQuery->nProbeSlack)
      ){
        break;
      }
    }
    if( pTab->mod.hdr.flags & VEC1_MODEL_RESIDUAL ){
      dist.n = 0;
    }
    rc = vec1DoKANNBucket(pQuery, iBucket, &dist, &meta, &bitmask);
  }

  VEC1_QINSTR_START(pTab, VEC1_QINSTR_FINALSORT);

  if( rc==SQLITE_OK ){
    i64 nSort = MIN( pHeap->nRes, pQuery->K );

    if( pQuery->bStreaming ){
      /* For a streaming query, set the pQuery->aBucket[] array to contain
      ** the remaining buckets - the ones that have not yet been scanned. */
      pQuery->nBucket = pTab->mod.hdr.nBucket - iProbe;
      if( pQuery->nBucket>0 ){
        int nCopy = sizeof(Vec1BucketResult) * pQuery->nBucket;
        memmove(aBucket, &aBucket[iProbe], nCopy);
        pQuery->aBucket = aBucket;
        aBucket = 0;
      }

      pQuery->nOrigRes = (int)MAX(16, pHeap->nRes);
    }

    /* For a streaming query, all candidate vectors were added to the 
    ** aRes[] array. The heap has guaranteed that the first nSort elements 
    ** of the Vec1AnnHeap.aRes[] array have distances <= than the remaining 
    ** elements. So it is still correct just to sort the first nSort
    ** elements of aRes[] and set nCurrentRes=nSort to configure the first
    ** block of rows to return.  */
    rc = vec1AnnResultSort(pHeap->aRes, nSort);
    pCsr->nCurrentRes = (int)nSort;
  }
  sqlite3_free(aBucket);
  vec1BufferFree(&meta);
  vec1BufferFree(&bitmask);
  vec1BufferFree(&dist);
  VEC1_QINSTR_STOP(pTab, VEC1_QINSTR_FINALSORT);

  return rc;
}

/*
** Load the contents of the %_config table into the pTab->cfg structure.
** if the iModelVersion field has changed since %_config was last loaded,
** also load the model from the %_model table.
*/
static int vec1LoadConfig(Vec1Tab *pTab){
  sqlite3_stmt *pStmt = 0;
  int rc = SQLITE_OK;
  int nReq = 0;

  pTab->cfg.nBlocksize = VEC1_BLOCKSIZE_DEFAULT;
  pTab->cfg.nBlocksizeMin = VEC1_BLOCKSIZE_MIN_DEFAULT;
  pTab->cfg.nElem = 0;

  rc = vec1GetSql(pTab, VEC1_SQL_LOAD_CONFIG, &pStmt);
  if( rc==SQLITE_OK ){
    while( sqlite3_step(pStmt)==SQLITE_ROW ){
      int iField = sqlite3_column_int(pStmt, 0);
      switch( iField ){
        case VEC1_CONFIG_MODEL: {
          i64 iNew = sqlite3_column_int64(pStmt, 1);
          if( pTab->cfg.iModelVersion!=iNew ){
            rc = vec1LoadModel(pTab);
            if( rc==SQLITE_OK ){
              pTab->cfg.iModelVersion = iNew;
            }
          }
          break;
        }

        case VEC1_CONFIG_BLOCKSIZE: {
          pTab->cfg.nBlocksize = sqlite3_column_int(pStmt, 1);
          pTab->cfg.nBlocksize = MAX(VEC1_BLOCKSIZE_MIN, pTab->cfg.nBlocksize);
          pTab->cfg.nBlocksize = MIN(VEC1_BLOCKSIZE_MAX, pTab->cfg.nBlocksize);
          break;
        }

        case VEC1_CONFIG_BLOCKSIZE_MIN: {
          int nMin = sqlite3_column_int(pStmt, 1);
          nMin = MAX(VEC1_BLOCKSIZE_MIN, nMin);
          nMin = MIN(VEC1_BLOCKSIZE_MAX, nMin);
          pTab->cfg.nBlocksizeMin = nMin;
          break;
        }

        case VEC1_CONFIG_NELEM: {
          pTab->cfg.nElem = sqlite3_column_int(pStmt, 1);
          break;
        }

        case VEC1_CONFIG_FMTVERSION: {
          int iVersion = sqlite3_column_int(pStmt, 1);
          if( iVersion<VEC1_CURRENT_FMTVERSION ){
            vec1VtabError(pTab, "unrecognized index version");
            rc = SQLITE_ERROR;
          }else if( iVersion>VEC1_CURRENT_FMTVERSION ){
            vec1VtabError(pTab, 
                "unsupported index version - requires vec1 %d.%d",
                (iVersion / 1000), (iVersion % 1000)
            );
            rc = SQLITE_ERROR;
          }
          break;
        }

        default: {
          vec1VtabError(pTab, "unrecognized config entry: %d", iField);
          rc = SQLITE_ERROR;
          break;
        }
      }
    }

    vec1StmtReset(&rc, pStmt);
  }

  nReq = MAX(pTab->mod.hdr.nElem, pTab->mod.nCodeElem*pTab->mod.hdr.nCodebook);
  if( (rc==SQLITE_OK) && nReq>0 && nReq!=pTab->nTmpVec ){
    sqlite3_free(pTab->aTmpVec);
    pTab->aTmpVec = (float*)vec1MallocZero(nReq * sizeof_f32);
    if( pTab->aTmpVec ){
      pTab->nTmpVec = nReq;
    }else{
      pTab->nTmpVec = 0;
      rc = SQLITE_NOMEM;
    }
  }

  if( rc==SQLITE_OK 
   && pTab->mod.hdr.nElem!=0 
   && (u32)pTab->cfg.nElem!=pTab->mod.hdr.nElem 
  ){
    vec1VtabError(pTab, "model/data vector size mismatch: %d/%d",
        pTab->mod.hdr.nElem, pTab->cfg.nElem
    );
    vec1UnloadModel(pTab);
    rc = SQLITE_ERROR;
  }

  return rc;
}

/*
** Load the configuration if it is not already loaded. The configuration
** is assumed not to be loaded if cfg.nElem==0.
**
** Return SQLITE_OK if successful, or an SQLite error code otherwise. If
** an error occurs, this function may leave an error message in the virtual
** table object.
*/
static int vec1LoadConfigIfRequired(Vec1Tab *pTab){
  int rc = SQLITE_OK;
  if( pTab->cfg.nElem==0 ){
    rc = vec1LoadConfig(pTab);
    if( rc!=SQLITE_OK ){
      pTab->cfg.nElem = 0;
    }
  }
  return rc;
}

static int vec1InterpretNProbe(Vec1Tab *pTab, double fVal){
  int nProbe;
  if( fVal>=1.0 ){
    nProbe = (int)fVal;
  }else{
    nProbe = (int)(fVal * pTab->mod.hdr.nBucket);
  }
  nProbe = MIN(nProbe, (int)pTab->mod.hdr.nBucket);
  nProbe = MAX(nProbe, 1);
  return nProbe;
}

static int vec1ParseQueryParamCb(
  void *pCtx, 
  const char *zOpt, 
  int eType,
  i64 iVal,
  double fVal, 
  const char *zVal,
  char **pzErr
){
  Vec1Query *pQuery = (Vec1Query*)pCtx;
  const char *azOpt[] = {
    "nprobe",        /* 0 */
    "K",             /* 1 */
    "streaming",     /* 2 */
    "nprobe_slack",  /* 3 */
    0
  };
  int iOpt = 0;

  UNUSED_PARAMETER2(iVal, eType);
  UNUSED_PARAMETER2(pzErr, zVal);

  int rc = vec1ConfigEnum(&iOpt, zOpt, azOpt, "option", pzErr);
  if( rc==SQLITE_OK ){
    switch( iOpt ){
      case 0: assert( sqlite3_stricmp("nprobe", zOpt)==0 );
        if( fVal<=0.0 ){
          *pzErr = sqlite3_mprintf(
              "vec1: nprobe must be a numeric value greater than 0.0"
          );
          rc = SQLITE_ERROR;
        }
        pQuery->nProbe = vec1InterpretNProbe(pQuery->pTab, fVal);
        break;
  
      case 1: assert( sqlite3_stricmp("K", zOpt)==0 );
        if( eType==SQLITE_INTEGER ){
          pQuery->K = iVal;
        }else{
          pQuery->K = -1;
        }
        break;
  
      case 2: assert( sqlite3_stricmp("streaming", zOpt)==0 );
        pQuery->bStreaming = ((int)fVal) ? 1 : 0;
        break;
  
      default: assert( sqlite3_stricmp("nprobe_slack", zOpt)==0 );
        pQuery->nProbeSlack = fVal;
        break;
    }
  }

  return rc;
}

static int vec1ModelIsFlat(Vec1Model *pMod){
  return ((pMod->hdr.flags & VEC1_MODEL_INDEX) && pMod->hdr.nCodebook==0);
}

static const char aHexDigit[] = {
  '0', '1', '2', '3', '4', '5', '6', '7',
  '8', '9', 'a', 'b', 'c', 'd', 'e', 'f',
};

static int vec1DecodeMetaIdx(const char *z){
  int iRet = 0;
  assert( (z[0]>='0' && z[0]<='9') || (z[0]>='a' && z[0]<='f') );
  assert( (z[1]>='0' && z[1]<='9') || (z[1]>='a' && z[1]<='f') );

  if( z[0]<='9' ){
    iRet = (int)(z[0] - '0');
  }else{
    iRet = (int)(10 + z[0] - 'a');
  }
  iRet = iRet << 4;
  if( z[1]<='9' ){
    iRet |= (int)(z[1] - '0');
  }else{
    iRet |= (int)(10 + z[1] - 'a');
  }
  return iRet;
}

/*
** This is invoked when a value of type eType (which is not SQLITE_BLOB)
** has been passed to vec1 as a vector. This is an error. Configure the 
** table with an error message and return SQLITE_ERROR.
*/
static int vec1VectorTypeError(Vec1Tab *pTab, int eType){
  const char *azType[] = {0, "INTEGER", "FLOAT", "TEXT", "BLOB", "NULL"};
  assert( eType!=SQLITE_BLOB && eType>=1 && eType<=5 );
  vec1VtabError(pTab, 
      "vec1: unexpected vector type %s, expected BLOB", azType[eType]
  );
  return SQLITE_ERROR;
}

/*
** This is invoked when a blob nHave bytes in size has been passed to vec1 
** as a vector when a (pTab->cfg.nElem*sizeof_f32) blob was expected.  
** Configure the table with an error message and return SQLITE_ERROR.
*/
static int vec1VectorSizeError(Vec1Tab *pTab, int nHave){
  assert( pTab->cfg.nElem>0 && (pTab->cfg.nElem*sizeof_f32)!=nHave );
  vec1VtabError(pTab, 
      "vec1: unexpected vector blob size %d bytes, expected %d", 
      nHave, pTab->cfg.nElem*sizeof_f32
  );
  return SQLITE_ERROR;
}

static int vec1FilterArraySize(
  const char *idxStr,
  sqlite3_value **argv,
  int *pnFilter
){
  int ii;
  int nFilter = 0;
  int iArg = 1;
  int rc = SQLITE_OK;

  for(ii=0; idxStr[ii]; ii++){
    char c = idxStr[ii];
    if( c==VEC1_OP_IN ){
      sqlite3_value *pDummy = 0;
      sqlite3_value *pList = argv[iArg];
      nFilter++;
      for(rc=sqlite3_vtab_in_first(pList, &pDummy); 
          rc==SQLITE_OK;
          rc=sqlite3_vtab_in_next(pList, &pDummy)
      ){
        assert( pDummy!=0 );
        nFilter++;
      }
      if( rc==SQLITE_DONE ) rc = SQLITE_OK;
      ii += 2;
    }else if( c!=VEC1_OP_PARAMS && c!=VEC1_OP_LIMIT ){
      ii += 2;
      nFilter++;
    }
    iArg++;
  }

  *pnFilter = nFilter;
  return rc;
}

static void *vec1Dup(int *pRc, const void *p, int n){
  void *pRet = 0;
  assert( *pRc==SQLITE_OK );
  if( p==0 ){
    *pRc = SQLITE_NOMEM;
  }else{
    pRet = vec1MallocZero(n+1);
    if( pRet==0 ){
      *pRc = SQLITE_NOMEM;
    }else if( n>0 ){
      memcpy(pRet, p, n);
    }
  }
  return pRet;
}

static void vec1ValueToFilter(
  int *pRc, 
  sqlite3_value *pVal, 
  Vec1Filter *pFilter
){
  pFilter->eType = sqlite3_value_type(pVal);
  switch( pFilter->eType ){
    case SQLITE_NULL:
      break;
    case SQLITE_INTEGER:
      pFilter->iVal = sqlite3_value_int64(pVal);
      break;
    case SQLITE_FLOAT:
      pFilter->fVal = sqlite3_value_double(pVal);
      break;
    case SQLITE_TEXT: 
      pFilter->iVal = sqlite3_value_bytes(pVal);
      pFilter->pPtr = (u8*)vec1Dup(
          pRc, sqlite3_value_text(pVal), (int)pFilter->iVal
      );
      break;
    default: {
      int nByte = sqlite3_value_bytes(pVal);
      assert( pFilter->eType==SQLITE_BLOB );
      pFilter->iVal = (i64)nByte;
      pFilter->pPtr = (u8*)vec1Dup(
          pRc, nByte? sqlite3_value_blob(pVal) : (const void*)"", nByte
      );
      break;
    }
  }
}

/*
** Return true if value pVal contains an integer - either an SQL integer,
** text that looks like an integer, or a double that can be converted
** to an integer without loss of data.
*/
static int vecIsIntegerValue(sqlite3_value *pVal){
  if( sqlite3_value_numeric_type(pVal)==SQLITE_INTEGER ) return 1;
  if( sqlite3_value_numeric_type(pVal)==SQLITE_FLOAT ){
    double fVal = sqlite3_value_double(pVal);
    if( fVal==(double)((i64)fVal) ) return 1;
  }
  return 0;
}

static int vec1SetupKANNQuery(
  Vec1Tab *pTab,
  const char *idxStr,             /* idxStr passed to xFilter */
  sqlite3_value **argv,           /* Parameter values passed to xFilter */
  Vec1Query **ppOut               /* OUT: Query object */
){
  const int nElem = pTab->cfg.nElem;
  const int nSpace = MAX(pTab->cfg.nElem, pTab->nTmpVec);
  int nMax = 0;                   /* Max possible meta-data filters */
  int nVec = sqlite3_value_bytes(argv[0]);
  const u8 *aVec = sqlite3_value_blob(argv[0]);

  int iArg = 1;
  int iIdxStr = 0;
  int rc = SQLITE_OK;
  Vec1Query *p = 0;

  int bTransform = vec1TransformRequired(&pTab->mod);
  int nBit = 0;

  if( pTab->mod.hdr.flags & VEC1_MODEL_BITQUANT ){
    nBit = sizeof_f32 + pTab->mod.hdr.nCodebook;
  }

  /* Determine the required size of the Vec1Query.aFilter[] array. */
  rc = vec1FilterArraySize(idxStr, argv, &nMax);
  
  p = (Vec1Query*)vec1MallocZero(
        sizeof(Vec1Query)                          /* Vec1Query object itself */
      + nMax * sizeof(Vec1Filter)                  /* Vec1Query.aFilter[] */
      + nSpace * sizeof_f32                        /* Vec1Query.aVector[] */
      + bTransform * (nSpace * sizeof_f32)         /* aTransform[] */
      + nBit                                       /* aBit[] */
  );
  if( p==0 ) return SQLITE_NOMEM;
  p->aFilter = (Vec1Filter*)&p[1];
  p->aVector = (float*)&p->aFilter[nMax];
  if( bTransform ){
    p->aTransform = &p->aVector[nSpace];
  }else{
    p->aTransform = p->aVector;
  }
  if( nBit ){
    p->aBit = (u8*)&p->aTransform[nSpace];
  }

  /* Initialize default query parameters.
  **
  ** Use VEC1_SMALLEST_INT64 to signify that no K value has been supplied
  ** If somebody specifies this value via a JSON query parameter, they
  ** may get a slightly inaccurate error message. So be it.  */
  p->pTab = pTab;
  p->K = VEC1_SMALLEST_INT64;
  p->nProbe = vec1InterpretNProbe(pTab, pTab->pTabList->nProbeArg);
  p->bStreaming = 0;

  /* Check the query vector is the right size and copy it into the 
  ** aVector[] buffer.  
  **
  ** Omit this check if nElem==0. This occurs when the table has not yet
  ** received the first vector. So carry on checking the remainder of the
  ** parameters, but don't worry about checking the input vector or 
  ** copying it into aVector[], as the query will return zero rows in
  ** any case.  */
  if( nElem!=0 ){
    if( nVec!=(nElem*sizeof_f32) ){
      rc = vec1VectorSizeError(pTab, nVec);
    }else{
      memcpy(p->aVector, aVec, nVec);
    }
  }

  /* Check that the query vector does not contain any NaN or Infinity values */
  if( rc==SQLITE_OK ){
    rc = vec1VectorIsNonFinite((float*)aVec, (nVec/sizeof_f32));
    if( rc!=SQLITE_OK ){
      vec1VtabError(pTab, "vec1: non-finite value not allowed in vector");
    }
  }

  /* Check if parameters where supplied via hidden column "arg". If so,
  ** the first character of idxStr is 'P' - parameters. */
  if( rc==SQLITE_OK && idxStr[0]==VEC1_OP_PARAMS ){
    sqlite3_value *pVal = argv[iArg++];
    if( vecIsIntegerValue(pVal) ){
      p->K = sqlite3_value_int64(pVal);
    }else if( sqlite3_value_type(pVal)==SQLITE_TEXT ){
      assert( pTab->base.zErrMsg==0 );
      rc = vec1ParseJsonConfig(pTab->db, 
          (const char*)sqlite3_value_text(pVal), vec1ParseQueryParamCb, 
          (void*)p, &pTab->base.zErrMsg
      );
    }else{
      p->K = -1;
    }
    iIdxStr = 1;
  }

  /* Process remaining constraints, if any. */
  if( rc==SQLITE_OK && idxStr[iIdxStr]!='\0' ){
    char c;
    while( (c = idxStr[iIdxStr++])!='\0' ){
      sqlite3_value *pVal = argv[iArg++];
      if( c==VEC1_OP_LIMIT ){
        int iVal = sqlite3_value_int(pVal);
        if( iVal>=0 && (p->K==VEC1_SMALLEST_INT64 || p->K>0) ){
          /* A non-negative LIMIT clause. If no K value was specified, use
          ** this value as K.  Or, if a K value was specified, use the
          ** minimum of K and this value.  */
          p->K = ((p->K < 0) ? iVal : MIN(p->K, iVal));
        }
      }else{
        /* Filter on a meta value. The operand character is followed by
        ** 2 hexadecimal digits encoding the index of the meta column
        ** on the LHS of the constraint.  */
        Vec1Filter *pFilter = &p->aFilter[p->nFilter++];
        pFilter->vop = c;
        pFilter->iMeta = vec1DecodeMetaIdx(&idxStr[iIdxStr]);
        iIdxStr += 2;
        if( c==VEC1_OP_IN ){
          sqlite3_value *pInVal = 0;
          assert( pFilter->iVal==0 );
          for(rc=sqlite3_vtab_in_first(pVal, &pInVal);
              rc==SQLITE_OK; 
              rc=sqlite3_vtab_in_next(pVal, &pInVal)
          ){
            Vec1Filter *pInFilter = &p->aFilter[p->nFilter++];
            vec1ValueToFilter(&rc, pInVal, pInFilter);
            if( rc!=SQLITE_OK ) break;
            pInFilter->vop = VEC1_OP_EQ;
            pInFilter->iMeta = pFilter->iMeta;
            pFilter->iVal++;
          }
          if( rc==SQLITE_DONE ) rc = SQLITE_OK;

        }else if( c==VEC1_OP_NOTNULL ){
          pFilter->eType = SQLITE_BLOB;
          pFilter->iVal = 0;
        }else if( c==VEC1_OP_ISNULL ){
          pFilter->eType = SQLITE_NULL;
          pFilter->vop = VEC1_OP_IS;

        }else{
          vec1ValueToFilter(&rc, pVal, pFilter);
        }
      }
    }
  }
  assert( rc!=SQLITE_OK || p->nFilter==nMax );

  /* Check that a usable K value has been specified. Either explicitly or
  ** via a visible LIMIT clause.  */
  if( rc==SQLITE_OK ){
    if( p->K==VEC1_SMALLEST_INT64 ){
      vec1VtabError(pTab, "vec1: no K value or visible LIMIT clause");
      rc = SQLITE_ERROR;
    }else if( p->K<0 || p->K>VEC1_MAX_K ){
      vec1VtabError(pTab, 
          "vec1: K must be an integer value between 0 and %d", VEC1_MAX_K
      );
      rc = SQLITE_ERROR;
    }
  }

  /* If an error occurred, free any dynamic allocations and zero the output
  ** structure before returning. Otherwise, sort the aFilter[] array by
  ** iMeta value. */
  if( rc!=SQLITE_OK ){
    vec1QueryFree(p);
    p = 0;
  }else if( 0 ){
    /* Sort aFilter[] by Vec1Filter.iMeta value. The sort must be stable,
    ** in order to avoid disrupting the arguments to a VEC1_OP_IN filter. */
    int i1, i2;
    for(i1=0; i1<p->nFilter; i1++){
      Vec1Filter tmp = p->aFilter[i1];
      for(i2=i1-1; i2>=0 && p->aFilter[i2].iMeta>tmp.iMeta; i2--){
        p->aFilter[i2+1] = p->aFilter[i2];
      }
      p->aFilter[i2+1] = tmp;
    }

#ifndef NDEBUG
    /* Assert() that the sort worked. */
    for(i1=1; i1<p->nFilter; i1++){ 
      assert( p->aFilter[i1-1].iMeta<=p->aFilter[i1].iMeta );
    }
#endif
  }

  *ppOut = p;
  return rc;
}

/*
** This function is called in index:"none" mode.
*/
static int vec1QueryToSql(Vec1Csr *pCsr){
  Vec1Tab *pTab = (Vec1Tab*)(pCsr->base.pVtab);
  Vec1Query *pQuery = pCsr->pQuery;
  int ii;
  int rc = SQLITE_OK;
  char *zWhere = 0;

  int iNextVar = 1;

  sqlite3_str *pWhere = sqlite3_str_new(pTab->db);
  const char *zAnd = "";

  for(ii=0; ii<pQuery->nFilter; ii++){
    Vec1Filter *p = &pQuery->aFilter[ii];
    int bRhs = 1;
    const char *zOp = "";
    if( p->vop==VEC1_OP_IN ){
      int jj;
      sqlite3_str_appendf(pWhere, "%sc%d IN(?%d", zAnd, p->iMeta, iNextVar++);
      for(jj=2; jj<=p->iVal; jj++){
        sqlite3_str_appendf(pWhere, ", ?%d", iNextVar++);
      }
      ii += (int)p->iVal;
      sqlite3_str_appendf(pWhere, ")");
    }else{
      switch( p->vop ){
        case VEC1_OP_EQ: zOp = "="; break;
        case VEC1_OP_LT: zOp = "<"; break;
        case VEC1_OP_GT: zOp = ">"; break;
        case VEC1_OP_LE: zOp = "<="; break;
        case VEC1_OP_GE: zOp = ">="; break;
        case VEC1_OP_IS: zOp = "IS"; break;
        default:
          assert( p->vop==VEC1_OP_NOTNULL );
          zOp = "IS NOT NULL"; 
          bRhs = 0; 
          break;
      }
  
      sqlite3_str_appendf(pWhere, "%sc%d %s", zAnd, p->iMeta, zOp);

      if( bRhs ){
        sqlite3_str_appendf(pWhere, " ?%d", iNextVar++);
      }
    }
    zAnd = " AND ";
  }

  rc = sqlite3_str_errcode(pWhere);
  zWhere = sqlite3_str_finish(pWhere);

  if( rc==SQLITE_OK ){
    const char *zDistance = "vec1_l2_distance";
    if( pTab->mod.hdr.eDistance==VEC1_DISTANCE_COS ){
      zDistance = "vec1_cos_distance";
    }

    rc = vec1PrepareSql(pTab, &pCsr->pStmt,
        "SELECT *, %s(?%d, vector) AS d FROM %Q.'%q_base'%s%s "
        "ORDER BY d LIMIT %lld",
        zDistance, iNextVar,
        pTab->zDb, pTab->zName, 
        zWhere ? " WHERE " : "", zWhere,
        pQuery->K
    );
  }
  if( rc==SQLITE_OK ){
    int n = pTab->cfg.nElem * sizeof_f32;
    rc = sqlite3_bind_blob(
        pCsr->pStmt, iNextVar, pQuery->aVector, n, SQLITE_TRANSIENT
    );
    iNextVar = 1;
    for(ii=0; rc==SQLITE_OK && ii<pQuery->nFilter; ii++){
      Vec1Filter *p = &pQuery->aFilter[ii];
      if( p->vop!=VEC1_OP_IN && p->vop!=VEC1_OP_NOTNULL ){
        switch( p->eType ){
          case SQLITE_NULL:
            break;
          case SQLITE_INTEGER:
            sqlite3_bind_int64(pCsr->pStmt, iNextVar, p->iVal);
            break;
          case SQLITE_FLOAT:
            sqlite3_bind_double(pCsr->pStmt, iNextVar, p->fVal);
            break;
          case SQLITE_TEXT:
            rc = sqlite3_bind_text(pCsr->pStmt, 
                iNextVar, (const char*)p->pPtr, (int)p->iVal, SQLITE_TRANSIENT
            );
            break;
          default:
            assert( p->eType==SQLITE_BLOB );
            rc = sqlite3_bind_blob(
                pCsr->pStmt, iNextVar, p->pPtr, (int)p->iVal, SQLITE_TRANSIENT
            );
            break;
        }

        iNextVar++;
      }
    }
  }

  vec1QueryFree(pQuery);
  pCsr->pQuery = 0;
  pCsr->bSqlNNQuery = 1;
  sqlite3_free(zWhere);
  return rc;
}

/*
** There are three basic query plans supported by xBestIndex:
**
**   0: Full table scan.
**   1: Lookup by rowid.
**   2: K-nearest-neighbours search.
*/
static int vec1FilterMethod(
  sqlite3_vtab_cursor *cur,
  int idxNum,
  const char *idxStr,
  int argc,
  sqlite3_value **argv
){
  Vec1Csr *pCsr = (Vec1Csr *)cur;
  Vec1Tab *pTab = (Vec1Tab*)(cur->pVtab);
  int rc = SQLITE_OK;

  UNUSED_PARAMETER(argc);
  assert( ((idxNum==0 || idxNum==1) && argc==0) 
       || ((idxNum==2 || idxNum==3) && argc==1)
       || (idxNum==5 && argc>=1)
  );
  vec1ResetCsr(pCsr);

  if( pCsr->bLoadConfig==0 && (idxNum & 0x01) ){
    rc = vec1LoadConfig(pTab);
    if( rc!=SQLITE_OK ) return rc;
    pCsr->bLoadConfig = 1;
  }

  if( idxNum==0 || idxNum==1 ){
    /* Full-table scan. */
    rc = vec1PrepareSql(pTab, &pCsr->pStmt,
        "SELECT * FROM %Q.'%q_base'", pTab->zDb, pTab->zName
    );
  }else if( idxNum==2 || idxNum==3 ){
    /* Lookup by rowid */
    rc = vec1PrepareSql(pTab, &pCsr->pStmt,
        "SELECT * FROM %Q.'%q_base' WHERE rowid=?", 
        pTab->zDb, pTab->zName
    );
    if( rc==SQLITE_OK ){
      sqlite3_bind_value(pCsr->pStmt, 1, argv[0]);
    }
  }else{
    /* ANN query */
    Vec1Query *pQuery = 0;

    assert( idxNum==5 );

    VEC1_QINSTR_START(pTab, VEC1_QINSTR_TOTAL);

    /* Parse the query parameters. */
    rc = vec1SetupKANNQuery(pTab, idxStr, argv, &pQuery);
    pCsr->pQuery = pQuery;

    if( rc==SQLITE_OK ){
      if( (pTab->mod.hdr.flags & VEC1_MODEL_INDEX)==0 ){
        rc = vec1QueryToSql(pCsr);
        assert( pCsr->pQuery==0 );
        assert( pCsr->bSqlNNQuery );
      }else{
        rc = vec1PrepareSql(pTab, &pCsr->pStmt,
            "SELECT * FROM %Q.'%q_base' WHERE id=?", 
            pTab->zDb, pTab->zName
        );
        if( rc==SQLITE_OK ){
          rc = vec1DoKANNQuery(pCsr);
          pCsr->iCurrentRes = -1;
        }
      }
    }

    VEC1_QINSTR_STOP(pTab, VEC1_QINSTR_TOTAL);
  }

  if( rc==SQLITE_OK ){
    rc = vec1NextMethod(cur);
  }
  return rc;
}

static int vec1EofMethod(sqlite3_vtab_cursor *cur){
  Vec1Csr *pCur = (Vec1Csr *)cur;
  return (pCur->pStmt==0);
}

static int vec1SeekCsr(Vec1Csr *pCsr){
  int rc = SQLITE_OK;
  if( pCsr->pQuery && pCsr->bSeek==0 ){
    i64 iRowid = pCsr->pQuery->heap.aRes[pCsr->iCurrentRes].iRowid;
    sqlite3_bind_int64(pCsr->pStmt, 1, iRowid);
    if( SQLITE_ROW!=sqlite3_step(pCsr->pStmt) ){
      rc = sqlite3_finalize(pCsr->pStmt);
      if( rc==SQLITE_OK ){
        rc = VEC1_CORRUPT;
      }
      pCsr->pStmt = 0;
    }
    pCsr->bSeek = 1;
  }
  return rc;
}

typedef struct Vec1RowidLocation Vec1RowidLocation;
struct Vec1RowidLocation {
  int iEntry;
  int nEntry;
  int nTombstone;
  int szRowid;

  Vec1ListBuilder *pList;
  sqlite3_blob *pBlob;
  i64 iIdx;
};

static int vec1FindByRowid(
  Vec1Tab *pTab, 
  int iBucket,                    /* Row belongs to this bucket */
  int bWrite,                     /* Use writable blob-handle */
  i64 iRowid,                     /* Rowid to locate */
  Vec1RowidLocation *pOut         /* OUT: Result */
){
  int rc = SQLITE_OK;
  Vec1Buffer buf = {0, 0, 0};
  sqlite3_stmt *pSearch = 0;      /* For looping through index entries */
  sqlite3_blob *pBlob = 0;

  assert( (pTab->mod.hdr.flags & VEC1_MODEL_INDEX) );

  memset(pOut, 0, sizeof(*pOut));

  /* Check to see if the index entry for this vector is currently 
  ** in main memory. */
  if( pTab->pWriter ){
    Vec1ListBuilder *pBld = &pTab->pWriter->aBld[iBucket];
    int ii;
    for(ii=0; ii<pBld->bufRowid.n; ii+=pBld->szRowid){
      i64 iRead;
      if( pBld->szRowid==4 ){
        iRead = vec1GetU32(&pBld->bufRowid.a[ii]);
      }else{
        iRead = vec1GetU64(&pBld->bufRowid.a[ii]);
      }
      if( iRead==iRowid ){
        pOut->iEntry = (ii / pBld->szRowid);
        pOut->nEntry = (pBld->bufRowid.n / pBld->szRowid);
        pOut->nTombstone = pBld->nTombstone;
        pOut->szRowid = pBld->szRowid;
        pOut->pList = pBld;
        return SQLITE_OK;
      }
    }
  }

  rc = vec1GetSql(pTab, VEC1_SQL_SEARCH_IDX, &pSearch);
  if( rc==SQLITE_OK ){
    sqlite3_bind_int(pSearch, 1, iBucket);
    sqlite3_bind_int64(pSearch, 2, iRowid);
  }

  while( rc==SQLITE_OK && SQLITE_ROW==sqlite3_step(pSearch) ){
    int nEntry = 0;
    u32 nTombstone = 0;
    u32 flags = 0;
    int szRowid = 4;
    i64 iIdx = sqlite3_column_int64(pSearch, 0);

    rc = sqlite3_blob_open(
        pTab->db, pTab->zDb, pTab->zIdxTbl, "val", iIdx, bWrite, &pBlob
    );
    if( rc==SQLITE_OK ){
      u8 a[VEC1_LIST_SZHDR];
      rc = sqlite3_blob_read(pBlob, a, VEC1_LIST_SZHDR, 0);
      flags = vec1GetU32(&a[0]);
      nEntry = (int)vec1GetU32(&a[4]);
      nTombstone = vec1GetU32(&a[8]);
      if( flags & VEC1_LIST_64BIT ) szRowid = 8;
    }

    /* Now scan through the array of rowids at the start of the %_idx entry
    ** to find the rowid to be deleted. But, do not do this if the rowid
    ** happens to have the same value as the tombstone for this %_idx entry. */
    if( (szRowid==4 && iRowid!=VEC1_TOMBSTONE_32) 
     || (szRowid==8 && iRowid!=VEC1_TOMBSTONE_64)
    ){
      if( rc==SQLITE_OK ){
        buf.n = 0;
        rc = vec1BufferGrow(&buf, VEC1_LIST_SZHDR+szRowid*nEntry);
      }
      if( rc==SQLITE_OK ){
        rc = sqlite3_blob_read(
            pBlob, &buf.a[VEC1_LIST_SZHDR], nEntry*szRowid, VEC1_LIST_SZHDR
        );
      }

      if( rc==SQLITE_OK ){
        int ii;
        for(ii=0; ii<nEntry; ii++){
          i64 iRead = 0;
          if( szRowid==4 ){
            iRead = vec1GetU32(&buf.a[VEC1_LIST_SZHDR + ii*szRowid]);
          }else{
            iRead = vec1GetU64(&buf.a[VEC1_LIST_SZHDR + ii*szRowid]);
          }
          if( iRead==iRowid ){
            pOut->iEntry = ii;
            pOut->nEntry = nEntry;
            pOut->nTombstone = nTombstone;
            pOut->szRowid = szRowid;
            pOut->pBlob = pBlob;
            pOut->iIdx = iIdx;
            pBlob = 0;
            goto findbyrowid_done;
          }
        }
      }
    }
    (void)sqlite3_blob_close(pBlob);
  }
  
 findbyrowid_done:
  vec1StmtReset(&rc, pSearch);
  vec1BufferFree(&buf);
  return rc;
}

/*
** Obtain the full vector associated with the current row of cursor pCsr.
*/
static int vec1GetVector(
  Vec1Csr *pCsr,
  const u8 **paVec,               /* OUT: Pointer to vector buffer */
  int *pnVec,                     /* OUT: Size of (*paVec) in bytes */
  u8 **pFree                      /* OUT: Buffer for caller to free (or NULL) */
){
  int rc = SQLITE_OK;
  Vec1Tab *pTab = (Vec1Tab*)(pCsr->base.pVtab);
  rc = vec1SeekCsr(pCsr);
  if( rc==SQLITE_OK ){
    if( vec1ModelIsFlat(&pTab->mod) ){
      i64 iRowid = sqlite3_column_int64(pCsr->pStmt, 0);
      int iBucket = sqlite3_column_int(pCsr->pStmt, 1);
      Vec1RowidLocation loc;
      rc = vec1FindByRowid(pTab, iBucket, 0, iRowid, &loc);
      if( rc==SQLITE_OK ){
        int nVec = sizeof_f32 * pTab->cfg.nElem;
        u8 *aVec = (u8*)sqlite3_malloc(nVec);
        if( aVec==0 ){
          rc = SQLITE_NOMEM;
        }else if( loc.pList ){
          memcpy(aVec, &loc.pList->bufData.a[nVec * loc.iEntry], nVec);
        }else if( loc.pBlob ){
          rc = sqlite3_blob_read(loc.pBlob, aVec, nVec,
              VEC1_LIST_SZHDR + (loc.nEntry*loc.szRowid) + (loc.iEntry * nVec)
          );
        }else{
          sqlite3_free(aVec);
          nVec = 0;
          aVec = 0;
        }

        if( rc!=SQLITE_OK ){
          sqlite3_free(aVec);
          *paVec = 0;
          *pnVec = 0;
          *pFree = 0;
        }else{
          *paVec = *pFree = aVec;
          *pnVec = nVec;
        }
        (void)sqlite3_blob_close(loc.pBlob);
      }
    }else{
      *paVec = sqlite3_column_blob(pCsr->pStmt, 1);
      *pnVec = sqlite3_column_bytes(pCsr->pStmt, 1);
      *pFree = 0;
    }
  }
  return rc;
}

/*
** This function quantizes vector aIn[], then dequantizes it and finds
** the squared L2 distance between the original and the round-tripped
** vector. If successful, output parameter (*prError) is set to the
** squared distance value and SQLITE_OK is returned. Or, if an error
** occurs, an SQLite error code (SQLITE_NOMEM) is returned.
*/
static int vec1QuantizeReconstructionError2(
  Vec1Model *pMod,                /* Model to use for quantization */
  const float *aIn,               /* Input vector */
  char **pzEnc,
  double *prError                 /* OUT: Round trip error */
){
  const int nElem = (int)pMod->hdr.nElem;
  float *aRound = 0;
  u8 *aCode = 0;
  int nCode = vec1EncodedVectorSize(pMod);
  char *zEnc = 0;
  int iEnc = 0;
  int ii;

  zEnc = (char*)vec1MallocZero(
      nElem * 8 +
      nElem * sizeof_f32 +             /* aRound[] */
      nCode                            /* aCode[] */
  );
  if( zEnc==0 ){
    return SQLITE_NOMEM;
  }
  aRound = (float*)&zEnc[nElem * 8];
  aCode = (u8*)&aRound[nElem];

  vec1EncodeVector(pMod, aIn, aCode);
  vec1DecodeVector(pMod, aCode, aRound);

  *prError = vec1L2Dist(aIn, aRound, nElem);
  zEnc[iEnc++] = '[';
  for(ii=0; ii<nCode; ii++){
    if( ii!=0 ) zEnc[iEnc++] = ',';
    if( aCode[ii]>=100 ) zEnc[iEnc++] = (aCode[ii] / 100) + '0';
    if( aCode[ii]>=10 ) zEnc[iEnc++] = ((aCode[ii] / 10) % 10) + '0';
    zEnc[iEnc++] = (aCode[ii] % 10) + '0';
  }
  zEnc[iEnc++] = ']';

  *pzEnc = zEnc;
  return SQLITE_OK;
}

static void vec1DistanceStats(sqlite3_context *ctx, Vec1Csr *pCsr){
  int rc = SQLITE_OK;

  if( pCsr->zDistance==0 ){
    const float *aVec = 0;
    int nVec = 0;
    u8 *pFree = 0;
    rc = vec1GetVector(pCsr, (const u8**)&aVec, &nVec, &pFree);
    if( rc==SQLITE_OK && aVec ){
      Vec1Tab *pTab = (Vec1Tab*)(pCsr->base.pVtab);
      int nElem = pTab->cfg.nElem;
      Vec1Model *pMod = &pTab->mod;
      int iBucket = 0;
      double fCoarseError = 0.0;
      double fReconError = 0.0;
      double fNorm = 0.0;
      const float *aTransform = 0;
      char *zEnc = 0;
  
      if( pMod->hdr.nBucket>1 || pMod->hdr.nCodebook>0 ){
        aTransform = vec1TransformInputVector(pMod, pTab->aTmpVec, aVec); 
        fNorm = vec1VectorNorm2(aTransform, nElem);
      }
  
      /* Find bucket and coarse error if applicable. */
      if( pMod->hdr.nBucket>1 ){
        iBucket = vec1PqBestMatch(
            pMod->aCentroid, pMod->hdr.nBucket, aVec, nElem, &fCoarseError
        );
        if( aTransform!=pTab->aTmpVec ){
          memcpy(pTab->aTmpVec, aTransform, nElem*sizeof_f32);
          aTransform = pTab->aTmpVec;
        }
        if( pTab->mod.hdr.flags & VEC1_MODEL_RESIDUAL ){
          vec1SubInPlace(pTab->aTmpVec, &pMod->aCentroid[iBucket*nElem], nElem);
        }
      }
  
      /* Find reconstruction error if applicable. */
      if( pMod->hdr.nCodebook>0 ){
        rc = vec1QuantizeReconstructionError2(
            pMod, aTransform, &zEnc, &fReconError
        );
      }
  
      pCsr->zDistance = vec1MPrintf(&rc, 
          "{bucket:%d, coarse_error:%f, reconstruction_error:%f, code: %s}",
          iBucket, 
          fNorm==0.0 ? 0.0 : sqrt(fCoarseError/fNorm), 
          fNorm==0.0 ? 0.0 : sqrt(fReconError/fNorm),
          zEnc ? zEnc : "null"
      );
      sqlite3_free(zEnc);
    }
    sqlite3_free(pFree);
  }

  if( rc!=SQLITE_OK ){
    sqlite3_result_error_code(ctx, rc);
  }else{
    sqlite3_result_text(ctx, pCsr->zDistance, -1, SQLITE_TRANSIENT);
  }
}

static int vec1ColumnMethod(
  sqlite3_vtab_cursor *cur,
  sqlite3_context *ctx,
  int iCol
){
  Vec1Csr *pCsr = (Vec1Csr *)cur;
  int rc = SQLITE_OK;
  if( iCol==VEC1_COLUMN_DISTANCE ){
    if( pCsr->pQuery ){
      double fDist = pCsr->pQuery->heap.aRes[pCsr->iCurrentRes].fDist;
      sqlite3_result_double(ctx, fDist);
    }else if( pCsr->bSqlNNQuery ){
      int ii = sqlite3_column_count(pCsr->pStmt)-1;
      sqlite3_result_value(ctx, sqlite3_column_value(pCsr->pStmt, ii));
    }else{
      vec1DistanceStats(ctx, pCsr);
    }
  }else if( iCol==VEC1_COLUMN_VECTOR ){
    const u8 *aVec = 0;
    int nVec = 0;
    u8 *pFree = 0;
    rc = vec1GetVector(pCsr, &aVec, &nVec, &pFree);
    if( rc==SQLITE_OK ){
      sqlite3_result_blob(ctx, aVec, nVec, SQLITE_TRANSIENT);
    }else{
      sqlite3_result_error_code(ctx, rc);
    }
    sqlite3_free(pFree);
  }else if( iCol>VEC1_COLUMN_VECTOR ){
    rc = vec1SeekCsr(pCsr);
    if( rc==SQLITE_OK ){
      sqlite3_result_value(ctx, sqlite3_column_value(pCsr->pStmt, iCol-2));
    }
  }

  return rc;
}

static int vec1Rowid(
  sqlite3_vtab_cursor *cur,
  sqlite3_int64 *pRowid
){
  Vec1Csr *pCsr = (Vec1Csr *)cur;
  if( pCsr->pQuery ){
    *pRowid = pCsr->pQuery->heap.aRes[pCsr->iCurrentRes].iRowid;
  }else{
    *pRowid = sqlite3_column_int64(pCsr->pStmt, 0);
  }
  return SQLITE_OK;
}

/*
** Return true if constraint iCons of pInfo uses the "binary" collation
** sequence. Or false it it uses anything else.
*/
static int vec1IsBinary(sqlite3_index_info *pInfo, int iCons){
  return 0==sqlite3_stricmp("binary", sqlite3_vtab_collation(pInfo, iCons));
}


/*
** xBestIndex implementation. Three plans are supported:
**
**   + Full scan.        (idxNum==0 or idxNum=1)
**   + Lookup by rowid . (idxNum==2 or idxNum=3)
**   + KANN search.      (idxNum==5)
**
** For KANN search, meta-data constraints are supported. They are encoded
** in idxStr. Specifically, for each handled constraint, there is one
** character to identify the operation, usually followed by one or more
** characters to identify the column the constraint applies to.
**
** idxStr format:
*/
static int vec1BestIndexMethod(
  sqlite3_vtab *pVtab,
  sqlite3_index_info *pIdxInfo
){
  struct Vec1IdxOp {
    int idxop;
    char vec1op;
  } aOp[] = {
    { SQLITE_INDEX_CONSTRAINT_EQ,        VEC1_OP_EQ },
    { SQLITE_INDEX_CONSTRAINT_LT,        VEC1_OP_LT },
    { SQLITE_INDEX_CONSTRAINT_GT,        VEC1_OP_GT },
    { SQLITE_INDEX_CONSTRAINT_LE,        VEC1_OP_LE },
    { SQLITE_INDEX_CONSTRAINT_GE,        VEC1_OP_GE },
    { SQLITE_INDEX_CONSTRAINT_IS,        VEC1_OP_IS },
    { SQLITE_INDEX_CONSTRAINT_ISNULL,    VEC1_OP_ISNULL },
    { SQLITE_INDEX_CONSTRAINT_ISNOTNULL, VEC1_OP_NOTNULL },
  };
  const int bDistance = (
      pIdxInfo->colUsed & ((1<<VEC1_COLUMN_DISTANCE)|(1<<VEC1_COLUMN_VECTOR))
  );
  int iCmd = -1;
  int iArg = -1;
  int iRowid = -1;
  int ii;

  UNUSED_PARAMETER(pVtab);

  /* First pass. Look for == on VEC1_COLUMN_CMD and VEC1_COLUMN_ARG. 
  ** Or an == on the rowid column.  */
  for(ii=0; ii<pIdxInfo->nConstraint; ii++){
    struct sqlite3_index_constraint *p = &pIdxInfo->aConstraint[ii];
    if( p->op==SQLITE_INDEX_CONSTRAINT_EQ ){
      if( p->usable ){
        if( p->iColumn==VEC1_COLUMN_CMD ) iCmd = ii;
        else if( p->iColumn==VEC1_COLUMN_ARG ) iArg = ii;
        else if( p->iColumn<0 ) iRowid = ii;
      }else{
        if( p->iColumn==VEC1_COLUMN_CMD || p->iColumn==VEC1_COLUMN_ARG ){
          return SQLITE_CONSTRAINT;
        }
      }
    }
  }

  if( iCmd>=0 ){
    int iLimit = -1;
    char *idxStr = vec1MallocZero((pIdxInfo->nConstraint*8)+1);
    int iStr = 0;
    int argvIndex = 0;

    if( idxStr==0 ) return SQLITE_NOMEM;
    pIdxInfo->idxNum = 5;
    pIdxInfo->needToFreeIdxStr = 1;
    pIdxInfo->idxStr = idxStr;
    pIdxInfo->aConstraintUsage[iCmd].argvIndex = (++argvIndex);
    pIdxInfo->aConstraintUsage[iCmd].omit = 1;
    if( iArg>=0 ){
      pIdxInfo->aConstraintUsage[iArg].argvIndex = (++argvIndex);
      pIdxInfo->aConstraintUsage[iArg].omit = 1;
      idxStr[iStr++] = VEC1_OP_PARAMS;
    }

    /* Meta-data constraints */
    for(ii=0; ii<pIdxInfo->nConstraint; ii++){
      struct sqlite3_index_constraint *p = &pIdxInfo->aConstraint[ii];
      if( p->usable==0 ) continue;
      if( p->iColumn>VEC1_COLUMN_VECTOR ){
        int jj;
        if( 0==vec1IsBinary(pIdxInfo, ii) ) continue;
        for(jj=0; jj<size_of_array(aOp); jj++){
          if( p->op==aOp[jj].idxop ){
            int iMeta = (p->iColumn - 1 - VEC1_COLUMN_VECTOR);
            if( jj==0 && sqlite3_vtab_in(pIdxInfo, ii, -1) ){
              idxStr[iStr++] = VEC1_OP_IN;
              sqlite3_vtab_in(pIdxInfo, ii, 1);
            }else{
              idxStr[iStr++] = aOp[jj].vec1op;
            }
            idxStr[iStr++] = aHexDigit[iMeta>>4];
            idxStr[iStr++] = aHexDigit[iMeta&0xF];
            pIdxInfo->aConstraintUsage[ii].argvIndex = (++argvIndex);
            pIdxInfo->aConstraintUsage[ii].omit = 1;
            break;
          }
        }
      }
      if( p->op==SQLITE_INDEX_CONSTRAINT_LIMIT ){
        iLimit = ii;
      }
    }

    if( iLimit>=0 
     && pIdxInfo->nOrderBy==0 
     && argvIndex==(pIdxInfo->nConstraint-1) 
    ){
      idxStr[iStr++] = VEC1_OP_LIMIT;
      pIdxInfo->aConstraintUsage[iLimit].argvIndex = (++argvIndex);
      pIdxInfo->aConstraintUsage[iLimit].omit = 1;
    }
  }else if( iRowid>=0 ){
    pIdxInfo->idxNum = (bDistance ? 3 : 2);
    pIdxInfo->aConstraintUsage[iRowid].argvIndex = 1;
    pIdxInfo->aConstraintUsage[iRowid].omit = 1;
    pIdxInfo->idxFlags |= SQLITE_INDEX_SCAN_UNIQUE;

    pIdxInfo->estimatedCost = 10.0;
    pIdxInfo->estimatedRows = 1;
  }else if( bDistance ){
    pIdxInfo->idxNum = 1;
  }

  return SQLITE_OK;
}

static void vec1ListBuilderInitMetaArray(Vec1ListBuilder *p){
  const u32 flags = VEC1_META_1BYTEINT|VEC1_META_4BYTEINT|VEC1_META_REAL;
  int ii;
  for(ii=0; ii<p->pTab->nMeta; ii++){
    p->aMeta[ii].flags = flags;
    p->aMeta[ii].format = VEC1_META_GENERIC;
    p->aMeta[ii].buf.n = VEC1_META_SZHDR;
  }
}

static int vec1ListBuilderInit(Vec1ListBuilder *p, Vec1Tab *pTab, int iBucket){
  int rc = SQLITE_OK;

  p->pTab = pTab;
  p->iBucket = iBucket;
  p->szRowid = 4;
  p->bSorted = 1;

  if( pTab->mod.hdr.nCodebook ){
    if( pTab->mod.hdr.flags & VEC1_MODEL_BITQUANT ){
      p->eBlocked = VEC1_BLOCKED_BIN;
    }else{
      p->eBlocked = VEC1_BLOCKED_PQ;
    }
    p->nVectorSize = vec1EncodedVectorSize(&pTab->mod);
  }else{
    p->nVectorSize = pTab->cfg.nElem * sizeof_f32;
    p->eBlocked = VEC1_BLOCKED_NONE;
  }

  if( pTab->nMeta>0 ){
    int nMeta = pTab->nMeta;
    p->aMeta = (Vec1MetaBuilder*)vec1MallocZero(sizeof(Vec1MetaBuilder)*nMeta);
    if( p->aMeta==0 ){
      rc = SQLITE_NOMEM;
    }else{
      vec1ListBuilderInitMetaArray(p);
    }
  }

  return rc;
}

static int vec1WriteMetaBlob(
  Vec1Tab *pTab,                  /* Vec1 virtual table */
  Vec1Buffer *pBuf,               /* Buffer to write from */
  i64 iId,                        /* Id of %_idx row */
  int iMeta                       /* Index of meta-column */
){
  sqlite3_stmt *pStmt = 0;
  int rc = vec1GetSql(pTab, VEC1_SQL_WRITE_META, &pStmt);

  assert( iMeta>=0 && iMeta<VEC1_MAX_META_COLUMNS );
  if( rc==SQLITE_OK ){
    i64 iMetaId = (iId << VEC1_META_COLUMN_BITS) + iMeta;
    sqlite3_bind_int64(pStmt, 1, iMetaId);
    rc = sqlite3_bind_blob(pStmt, 2, pBuf->a, pBuf->n, SQLITE_STATIC);
    sqlite3_step(pStmt);
    vec1StmtReset(&rc, pStmt);
    sqlite3_bind_null(pStmt, 2);
  }

  return rc;
}

static int vec1WriteMeta(
  Vec1Tab *pTab,                  /* Vec1 virtual table */
  Vec1MetaBuilder *p,             /* Buffer to write from */
  int nEntry,
  i64 iId,                        /* Id of %_idx row */
  int iMeta                       /* Index of meta-column */
){
  u32 f = 0;

  if( (p->format & VEC1_META_GENERIC)
   && (p->flags & (VEC1_META_1BYTEINT|VEC1_META_4BYTEINT|VEC1_META_REAL))
  ){
    int iIn = VEC1_META_SZHDR;
    int iOut = VEC1_META_SZHDR;
    Vec1MetaValue val = {0,0,0,0};
    Vec1Buffer buf = {0, 0, 0};

    int rc = SQLITE_OK;
    int szElem;
    i64 iNull;
    if( p->flags & VEC1_META_1BYTEINT ){
      szElem = 1;
      iNull = VEC1_META_1BYTENULL;
      p->format = VEC1_META_1BYTEINT;
    }else if( p->flags & VEC1_META_4BYTEINT ){
      szElem = sizeof_u32;
      iNull = VEC1_META_4BYTENULL;
      p->format = VEC1_META_4BYTEINT;
    }else{
      szElem = sizeof_f64;
      iNull = VEC1_META_REALNULL;
      p->format = VEC1_META_REAL;
    }

    rc = vec1BufferSize(&buf, VEC1_META_SZHDR + nEntry * szElem);
    if( rc!=SQLITE_OK ) return rc;

    while( iIn<p->buf.n ){
      vec1MetaValueRead(&p->buf, &iIn, &val);
      if( val.eType==SQLITE_NULL ){
        val.iVal = iNull;
      }else if( p->format==VEC1_META_REAL ){
        if( val.eType==SQLITE_INTEGER ){
          val.fVal = (double)val.iVal;
        }
        memcpy(&val.iVal, &val.fVal, sizeof_f64);
      }
      if( szElem==1 ){
        buf.a[iOut] = (val.iVal & 0xFF);
      }else if( szElem==sizeof_u32 ){
        vec1PutU32(&buf.a[iOut], (u32)val.iVal);
      }else{
        vec1PutU64(&buf.a[iOut], (u64)val.iVal);
      }
      iOut += szElem;
    }
    buf.n = iOut;

    SWAP(Vec1Buffer, p->buf, buf);
    vec1BufferFree(&buf);
  }

  f = (p->format | (p->flags & VEC1_META_HASNULL));
  vec1PutU32(&p->buf.a[0], f);
  vec1PutU32(&p->buf.a[4], nEntry);
  return vec1WriteMetaBlob(pTab, &p->buf, iId, iMeta);
}

/*
** A serialized meta-value is stored in buffer aBuf. Return the size in
** bytes of the meta value.
*/
static int vec1MetaValueSize(const u8 *aBuf){
  switch( aBuf[0] ){
    case 0: return 1;               /* NULL */
    case 1: return 2;               /* 1-byte integer */
    case 2: return 5;               /* 4-byte float */
    case 3: return 9;               /* 8-byte integer */
    case 4: return 9;               /* 8-byte real */
    default: {
      /* Either a blob or text field. If the size of the value in bytes
      ** is N, then the varint value V is:
      **
      **   text (odd V values) : V = (2N + 5)  ->  N = (V - 5) / 2
      **   blob (even V values): V = (2N + 6)  ->  N = (V - 6) / 2
      */
      i64 V = 0;
      int n = vec1GetVarint(aBuf, (u64*)&V);
      return (int)(((V-5)/2) + n);
    }
  }
}

#ifndef NDEBUG
/*
** This may be used in assert() statements to check that the meta-value
** list contains the expected number of elements.
*/
static int vec1MetaValueCheck(Vec1Buffer *pBuf, int nEntry){
  int iOff = 0;
  int ii;
  for(ii=0; ii<nEntry; ii++){
    iOff += vec1MetaValueSize(&pBuf->a[iOff]);
  }
  assert( iOff==pBuf->n );
  return 1;
}
#endif

static int vec1ListBuilderLoad(
  Vec1ListBuilder *p,
  i64 iId,
  i64 iFirst,
  i64 iLast,
  const u8 *aVal, int nVal
){
  Vec1Tab *pTab = p->pTab;
  int nEntry = 0;
  int flags = 0;
  int rc = SQLITE_OK;
  int nRowid = 0;
  int nData = 0;
  int ii;                         /* Iterator variable */

  assert( p->bufRowid.n==0 );
  assert( p->szRowid==4 );

  if( vec1CheckIdxSize(pTab, aVal, nVal) ){
    return VEC1_CORRUPT;
  }
  flags = vec1GetU32(&aVal[0]);
  nEntry = vec1GetU32(&aVal[4]);
  p->nTombstone = vec1GetU32(&aVal[8]);
  if( flags & VEC1_LIST_64BIT ) p->szRowid = 8;
  
  /* Set Vec1ListBuilder.eBlocked */
  if( pTab->mod.hdr.flags & VEC1_MODEL_BITQUANT ){
    p->eBlocked = VEC1_BLOCKED_BIN;
  }else if( pTab->mod.hdr.nCodebook>0 ){
    p->eBlocked = VEC1_BLOCKED_PQ;
  }else{
    p->eBlocked = VEC1_BLOCKED_NONE;
  }

  p->bSorted = ((flags & VEC1_LIST_SORTED)!=0);
  p->iFirst = iFirst;
  p->iLast = iLast;
  p->iId = iId;

  nRowid = nEntry * p->szRowid;
  nData = nVal - VEC1_LIST_SZHDR - nRowid;
  rc = vec1BufferGrow(&p->bufRowid, nRowid);
  if( rc==SQLITE_OK ){
    memcpy(p->bufRowid.a, &aVal[VEC1_LIST_SZHDR], nRowid);
    p->bufRowid.n = nRowid;
    rc = vec1BufferGrow(&p->bufData, nData);
  }
  if( rc==SQLITE_OK ){
    memcpy(p->bufData.a, &aVal[VEC1_LIST_SZHDR+nRowid], nData);
    p->bufData.n = nData;
  }

  /* Load the array for each meta-data column from the %_meta table */
  for(ii=0; rc==SQLITE_OK && ii<pTab->nMeta; ii++){
    Vec1MetaBuilder *pMeta = &p->aMeta[ii];
    rc = vec1ReadMeta(pTab, &pMeta->buf, iId, ii);
    if( rc==SQLITE_OK ){
      u32 f = vec1GetU32(pMeta->buf.a);
      pMeta->format = (f & VEC1_META_TYPEMASK);
      pMeta->flags = f;
    }
  }

  return rc;
}

static int vec1ListBuilderFlush(Vec1ListBuilder *p){
  int rc = SQLITE_OK;
  if( p->bufRowid.n>0 ){
    int nEntry = p->bufRowid.n / p->szRowid;
    i64 iId = 0;
    int ii;
    sqlite3_stmt *pStmt = 0;
    u8 *aBlob = 0;
    int nBlob = 0;

    nBlob = VEC1_LIST_SZHDR + p->bufRowid.n + p->bufData.n;
    aBlob = (u8*)sqlite3_malloc(nBlob);
    if( aBlob==0 ){
      rc = SQLITE_NOMEM;
    }else{
      int flags = 0;
      if( p->bSorted ) flags |= VEC1_LIST_SORTED;
      if( p->szRowid==8 ) flags |= VEC1_LIST_64BIT;
      vec1PutU32(&aBlob[0], flags);
      vec1PutU32(&aBlob[4], nEntry);
      vec1PutU32(&aBlob[8], p->nTombstone);
      memcpy(&aBlob[VEC1_LIST_SZHDR], p->bufRowid.a, p->bufRowid.n);
      memcpy(&aBlob[VEC1_LIST_SZHDR+p->bufRowid.n], p->bufData.a, p->bufData.n);

      if( p->iId>0 ){
        /* If there is an existing entry, remove it */
        rc = vec1GetSql(p->pTab, VEC1_SQL_DELETE_FROM_IDX, &pStmt);
        if( rc==SQLITE_OK ){
          sqlite3_bind_int64(pStmt, 1, p->iId);
          sqlite3_step(pStmt);
          vec1StmtReset(&rc, pStmt);
        }
      }

      if( rc==SQLITE_OK ){
        rc = vec1GetSql(p->pTab, VEC1_SQL_INSERT_IDX, &pStmt);
      }
      if( rc==SQLITE_OK ){
        if( p->iId==0 ){
          sqlite3_bind_null(pStmt, 1);
        }else{
          sqlite3_bind_int64(pStmt, 1, p->iId);
        }
        sqlite3_bind_int(pStmt, 2, p->iBucket);
        sqlite3_bind_int64(pStmt, 3, p->iFirst);
        sqlite3_bind_int64(pStmt, 4, p->iLast);

        rc = sqlite3_bind_blob(pStmt, 5, aBlob, nBlob, SQLITE_STATIC);
        sqlite3_step(pStmt);
        vec1StmtReset(&rc, pStmt);
        if( rc==SQLITE_OK ){
          iId = sqlite3_last_insert_rowid(p->pTab->db);
        }
      }
      sqlite3_free(aBlob);

      /* If an error occurred within the sqlite3_step() call above, then the
      ** transaction may have been rolled back and (*p) freed. So take
      ** care not to access it again if rc!=SQLITE_OK.  */
      if( rc==SQLITE_OK ){
        p->bufRowid.n = 0;
        p->bufData.n = 0;
        p->iId = 0;
        p->iId = 0;
      }
    }

    for(ii=0; rc==SQLITE_OK && ii<p->pTab->nMeta; ii++){
      rc = vec1WriteMeta(p->pTab, &p->aMeta[ii], nEntry, iId, ii);
      if( rc==SQLITE_OK ) p->aMeta[ii].buf.n = VEC1_META_SZHDR;
    }
    
    /* Same reasoning as above - if rc!=SQLITE_OK, then the transaction
    ** may have been rolled back and (*p) freed already. */
    if( rc==SQLITE_OK ){
      p->bSorted = 1;
    }
  }else if( p->iId>0 ){
    sqlite3_stmt *pDelete = 0;
    rc = vec1GetSql(p->pTab, VEC1_SQL_DELETE_FROM_IDX, &pDelete);
    if( rc==SQLITE_OK ){
      sqlite3_bind_int64(pDelete, 1, p->iId);
      sqlite3_step(pDelete);
      vec1StmtReset(&rc, pDelete);
    }
    if( p->pTab->nMeta>0 && rc==SQLITE_OK ){
      rc = vec1GetSql(p->pTab, VEC1_SQL_DELETE_FROM_META, &pDelete);
      if( rc==SQLITE_OK ){
        sqlite3_bind_int64(pDelete, 1, p->iId);
        sqlite3_step(pDelete);
        vec1StmtReset(&rc, pDelete);
      }
    }
  }
  return rc;
}


/*
** Return the offset of the first field of vector ii within a blocked 
** storage buffer. Each vector is nVectorSize bytes in size (including
** the norm for binary-quantized vectors). szElem0 is the size of the
** first element in each vector (4 for binary-quant, 1 for product-quant).
*/
#define VEC1_LIST_BLOCKED_OFFSET(ii, nVectorSize) (              \
  ((ii / VEC1_PQ_BLOCKSIZE) * VEC1_PQ_BLOCKSIZE * nVectorSize) + \
  (ii % VEC1_PQ_BLOCKSIZE)                                       \
)

/*
** Return the offset of the norm field for vector ii within a blocked 
** buffer containing binary quanitized vectors. Each quantized vector
** is nCodebook bytes in size, not including the 4 byte norm.
*/
#define VEC1_LIST_NORM_OFFSET(ii, nCodebook) (                                \
  ((ii / VEC1_PQ_BLOCKSIZE) * VEC1_PQ_BLOCKSIZE * (nCodebook+sizeof_f32)) + \
  (ii % VEC1_PQ_BLOCKSIZE) * sizeof_f32                          \
)

#define VEC1_LIST_CODE_OFFSET(ii, nCodebook) (                                \
  ((ii / VEC1_PQ_BLOCKSIZE) * VEC1_PQ_BLOCKSIZE * (nCodebook+sizeof_f32)) + \
  (VEC1_PQ_BLOCKSIZE * sizeof_f32) +                                          \
  (ii % VEC1_PQ_BLOCKSIZE)                                                    \
)

/*
** Shuffle the contents of the list-builder around so that it 
** contains 0 tombstones. 
*/
static int vec1ListBuilderCompress(
  Vec1ListBuilder *p,
  i64 iDel
){
  const int nMeta = p->pTab->nMeta;
  int ii;
  int iOut = 0;
  int nEntry = p->bufRowid.n / p->szRowid;
  int *aMetaIn = 0;               /* Meta-data input offsets */
  int *aMetaOut = 0;              /* Meta-data output offsets */

  i64 tomb1 = p->szRowid==8 ? VEC1_TOMBSTONE_64 : VEC1_TOMBSTONE_32;
  i64 tomb2 = iDel;

  /* Allocate space for aMetaIn[] and aMetaOut[] if requried */
  if( nMeta>0 ){
    aMetaIn = vec1MallocZero(nMeta * 2 * sizeof(int));
    if( aMetaIn==0 ) return SQLITE_NOMEM;
    aMetaOut = &aMetaIn[nMeta];
    for(ii=0; ii<nMeta; ii++){
      aMetaIn[ii] = VEC1_META_SZHDR;
      aMetaOut[ii] = VEC1_META_SZHDR;
    }
  }

  p->iFirst = VEC1_LARGEST_INT64;
  p->iLast = VEC1_SMALLEST_INT64;

  for(ii=0; ii<nEntry; ii++){
    int iOff = p->szRowid*ii;
    i64 iRowid;
    int bTombstone = 0;
    int iMeta;                    /* Iterator used for meta-data columns */

    if( p->szRowid==8 ){
      iRowid = vec1GetU64(&p->bufRowid.a[iOff]);
    }else{
      iRowid = vec1GetU32(&p->bufRowid.a[iOff]);
    }
    if( iRowid==tomb1 || iRowid==tomb2 ) bTombstone = 1;

    if( bTombstone==0 ){
      p->iFirst = MIN(p->iFirst, iRowid);
      p->iLast = MAX(p->iLast, iRowid);

      if( ii!=iOut ){
        int iOff2 = p->szRowid*iOut;
        u8 *a = p->bufData.a; 
        int iFrom, iTo;

        memcpy(&p->bufRowid.a[iOff2], &p->bufRowid.a[iOff], p->szRowid);
        if( p->eBlocked==VEC1_BLOCKED_NONE ){
          int i1 = p->nVectorSize * iOut;
          int i2 = p->nVectorSize * ii;
          memcpy(&p->bufData.a[i1], &p->bufData.a[i2], p->nVectorSize);
        }else{
          int nCodebook;          /* Size of quantized vector without norm */
          int jj;

          if( p->eBlocked==VEC1_BLOCKED_BIN ){
            nCodebook = p->nVectorSize - sizeof_f32;
            memcpy(&a[ VEC1_LIST_NORM_OFFSET(iOut, nCodebook) ],
                   &a[ VEC1_LIST_NORM_OFFSET(ii, nCodebook) ],
                   sizeof_f32
            );
            iFrom = VEC1_LIST_CODE_OFFSET(ii, nCodebook);
            iTo = VEC1_LIST_CODE_OFFSET(iOut, nCodebook);
          }else{
            assert( p->eBlocked==VEC1_BLOCKED_PQ );
            nCodebook = p->nVectorSize;
            iFrom = VEC1_LIST_BLOCKED_OFFSET(ii, nCodebook);
            iTo = VEC1_LIST_BLOCKED_OFFSET(iOut, nCodebook);
          }

          for(jj=0; jj<nCodebook; jj++){
            u8 v = p->bufData.a[iFrom + jj*VEC1_PQ_BLOCKSIZE];
            p->bufData.a[iTo + jj*VEC1_PQ_BLOCKSIZE] = v;
          }
        }
      }
      iOut++;
    }

    for(iMeta=0; iMeta<nMeta; iMeta++){
      Vec1MetaBuilder *pMeta = &p->aMeta[iMeta];
      u8 *a = pMeta->buf.a;


      if( pMeta->format==VEC1_META_GENERIC ){
        int nIn = vec1MetaValueSize(&a[aMetaIn[iMeta]]);
        if( bTombstone==0 ){
          if( aMetaIn[iMeta]!=aMetaOut[iMeta] ){
            memmove(&a[ aMetaOut[iMeta] ], &a[ aMetaIn[iMeta] ], nIn);
          }
          aMetaOut[iMeta] += nIn;
        }
        aMetaIn[iMeta] += nIn;
      }else{
        int szElem = 
          (pMeta->format==VEC1_META_1BYTEINT) ? 1 :
          (pMeta->format==VEC1_META_4BYTEINT) ? sizeof_u32 : sizeof_f64
        ;
        if( bTombstone==0 ){
          memmove(&a[ aMetaOut[iMeta] ], &a[ aMetaIn[iMeta] ], szElem);
          aMetaOut[iMeta] += szElem;
        }
        aMetaIn[iMeta] += szElem;
      }
    }
  }

  p->nTombstone = 0;

  p->bufRowid.n = iOut*p->szRowid;
  if( p->eBlocked==VEC1_BLOCKED_NONE ){
    p->bufData.n = p->nVectorSize * iOut;
  }else{
    p->bufData.n = p->nVectorSize * VEC1_PQ_BLOCKSIZE * (
        ((iOut+VEC1_PQ_BLOCKSIZE-1)/VEC1_PQ_BLOCKSIZE)
    );
  }
  for(ii=0; ii<nMeta; ii++){
    p->aMeta[ii].buf.n = aMetaOut[ii];
  }

  sqlite3_free(aMetaIn);
  return SQLITE_OK;
}

/*
** Append a rowid/[PQ|vector] pair to a list builder object.
*/
static int vec1ListBuilderAdd(
  Vec1ListBuilder *p,
  i64 iRowid,                     /* Rowid value to add to builder */
  const u8 *aPQ                   /* Pointer to buffer containing PQ enc. */
){
  int rc = SQLITE_OK;
  int nFinal = 0;
  const int vs = p->nVectorSize;

  assert( p->szRowid==4 || p->szRowid==8 );

  /* Check if adding this entry would cause the final blob to be larger
  ** than the configured 'blocksize'. If so, flush it to disk. 
  **
  ** If the proposed rowid may not be added to the current list because
  ** it is too large or a tombstone value, flush the current list to disk
  ** in that case as well.
  */
  nFinal = p->bufRowid.n + p->bufData.n + p->szRowid + p->nVectorSize;
  if( nFinal>p->pTab->cfg.nBlocksize 
   || (p->szRowid==4 && (iRowid<0 || iRowid>=VEC1_TOMBSTONE_32))
   || (p->szRowid==8 && iRowid==VEC1_TOMBSTONE_64)
  ){
    rc = vec1ListBuilderFlush(p);
    if( rc!=SQLITE_OK ) return rc;
    p->szRowid = ((iRowid<0 || iRowid>=VEC1_TOMBSTONE_32) ? 8 : 4);
    p->bSorted = 1;
    p->iId = 0;
    p->nTombstone = 0;
    vec1ListBuilderInitMetaArray(p);
  }

  /* Update Vec1ListBuilder.iFirst, iLast and bSorted to account for the
  ** rowid value being added by this call.  */
  if( p->bufRowid.n==0 ){
    p->iLast = p->iFirst = iRowid;
  }else if( iRowid<p->iLast ){
    p->bSorted = 0;
    p->iFirst = MIN(p->iFirst, iRowid);
  }else{
    p->iLast = iRowid;
  }

  /* Ensure that there is room in the two buffers for this entry. The
  ** rowids buffer always grows by szRowid bytes. How much the data
  ** buffer grows by depends on whether it is blocked or not.  */
  rc = vec1BufferGrow(&p->bufRowid, p->szRowid);
  if( rc==SQLITE_OK ){
    if( p->eBlocked==VEC1_BLOCKED_NONE ){
      rc = vec1BufferGrow(&p->bufData, p->nVectorSize);
    }else{
      int nEntry = (p->bufRowid.n / p->szRowid);
      if( (nEntry % VEC1_PQ_BLOCKSIZE)==0 ){
        int nReq = (VEC1_PQ_BLOCKSIZE * p->nVectorSize);
        rc = vec1BufferGrow(&p->bufData, nReq);
        if( rc==SQLITE_OK ){
          memset(&p->bufData.a[p->bufData.n], 0, nReq);
          p->bufData.n += nReq;
        }
      }
    }
  }

  if( rc==SQLITE_OK ){

    /* Write the vector or PQ data */
    if( p->eBlocked==VEC1_BLOCKED_NONE ){
      memcpy(&p->bufData.a[p->bufData.n], aPQ, p->nVectorSize);
      p->bufData.n += p->nVectorSize;
    }else{
      const int iEntry = (p->bufRowid.n / p->szRowid);
      const int iBlkOff = (iEntry / VEC1_PQ_BLOCKSIZE) * vs * VEC1_PQ_BLOCKSIZE;
      const int iBlkEntry = iEntry % VEC1_PQ_BLOCKSIZE;

      int ii = 0;
      int iOff = iBlkOff + iBlkEntry;

      if( p->eBlocked==VEC1_BLOCKED_BIN ){
        /* In this case, the first 4 bytes of the encoded vector is its norm.
        ** Each block begins with an array of VEC1_PQ_BLOCKSIZE norm values.
        ** Copy the norm for this vector in now.  */
        const int iNorm = iBlkOff + (sizeof_f32 * iBlkEntry);
        memcpy(&p->bufData.a[iNorm], aPQ, sizeof_f32);
        ii = 4;
        iOff += (sizeof_f32 * VEC1_PQ_BLOCKSIZE) - ii*VEC1_PQ_BLOCKSIZE;
      }

      /* Write each byte of the PQ or binary quant code */
      for(/* no-op */; ii<p->nVectorSize; ii++){
        p->bufData.a[iOff + ii*VEC1_PQ_BLOCKSIZE] = aPQ[ii];
      }
    }

    /* Write the rowid */
    if( p->szRowid==8 ){
      vec1PutU64(&p->bufRowid.a[p->bufRowid.n], iRowid);
    }else{
      vec1PutU32(&p->bufRowid.a[p->bufRowid.n], (u32)iRowid);
    }
    p->bufRowid.n += p->szRowid;
  }

  return rc;
}

static void vec1ListBuilderFree(Vec1ListBuilder *p){
  int ii;
  vec1BufferFree(&p->bufRowid);
  vec1BufferFree(&p->bufData);
  if( p->aMeta ){
    for(ii=0; ii<p->pTab->nMeta; ii++){
      vec1BufferFree(&p->aMeta[ii].buf);
    }
  }
  sqlite3_free(p->aMeta);
}

/*
** Allocate a new writer object for vec1 table pTab. 
*/
static int vec1WriterAlloc(
  Vec1Tab *pTab,                  /* Vec1 table */
  int bRebuild,                   /* True if this is 'rebuild' op */
  Vec1Writer **pp                 /* OUT: New writer object */
){
  int rc = SQLITE_OK;
  Vec1Writer *p = 0;
  int nBld = pTab->mod.hdr.nBucket ? pTab->mod.hdr.nBucket : 1;
  int nEncoded = vec1EncodedVectorSize(&pTab->mod);

  int nByte =
      sizeof(Vec1Writer) +                  /* Vec1Writer */
      sizeof(Vec1ListBuilder) * nBld +      /* Vec1Writer.aBld */
      sizeof_f32 * pTab->nTmpVec +          /* Vec1Writer.aResidual */
      nEncoded;                             /* Vec1Writer.aPQ */

  p = (Vec1Writer*)vec1MallocZero(nByte);
  if( p==0 ){
    rc = SQLITE_NOMEM;
  }else{
    memset(p, 0, nByte);
    p->aBld = (Vec1ListBuilder*)&p[1];
    p->aResidual = (float*)&p->aBld[nBld];
    p->aPQ = (u8*)&p->aResidual[pTab->nTmpVec];
    p->pTab = pTab;
    p->bRebuild = bRebuild;
    p->nBld = nBld;
    if( bRebuild ){
      int ii;
      for(ii=0; rc==SQLITE_OK && ii<nBld; ii++){
        rc = vec1ListBuilderInit(&p->aBld[ii], pTab, ii);
      }
    }
  }
  *pp = p;

  return rc;
}

/*
** Quantize a vector so that it can be written to the index.
*/
static void vec1QuantizeVector(
  const Vec1Model *pMod,          /* Current model */
  float *aTmp,                    /* Temporary space - same dim as vectors */
  const float *aVector,           /* Vector to quantize */
  int *piBucket,                  /* OUT: Bucket to put vector in */
  u8 *aCode                       /* OUT: Write PQ code (if any) here */ 
){
  const int nBucket = pMod->hdr.nBucket;
  const float *aVec;
  int iBucket = 0;

  aVec = vec1TransformInputVector(pMod, aTmp, aVector);
  if( nBucket>0 ){
    int nElem = pMod->hdr.nElem;
    iBucket = vec1PqBestMatch(pMod->aCentroid, nBucket, aVec, nElem, 0);
    if( pMod->hdr.nCodebook>0 && (pMod->hdr.flags & VEC1_MODEL_RESIDUAL) ){
      vec1Sub(aTmp, aVec, &pMod->aCentroid[iBucket*nElem], nElem);
      aVec = aTmp;
    }
  }

  if( pMod->hdr.nCodebook>0 ){
    vec1EncodeVector(pMod, aVec, aCode);
  }

  *piBucket = iBucket;
}


static int vec1WriterVector(
  Vec1Writer *p,                  /* Writer object to write to */
  i64 iRowid,                     /* Rowid of new entry */
  const float *aVector,           /* New vector */
  int *piBucket
){
  Vec1Tab *pTab = p->pTab;
  const u8 *aStore;
  Vec1ListBuilder *pBld = 0;
  const Vec1Model *pMod = &pTab->mod;
  int rc = SQLITE_OK;
  int iBld = *piBucket;

  /* Transform and quantize the vector */
  if( iBld<0 ){
    vec1QuantizeVector(pMod, p->aResidual, aVector, &iBld, p->aPQ);
  }

  if( pMod->hdr.nCodebook>0 ){
    aStore = p->aPQ;
  }else{
    /* If the index is storing full vectors, not PQ codes, store the original,
    ** not the transformed vector.  */
    aStore = (const u8*)aVector;
  }

  pBld = &p->aBld[iBld];
  if( pBld->pTab==0 ){
    /* Attempt to load blob smaller than 'blocksize-min' belonging to
    ** this bucket to append to. */
    sqlite3_stmt *pSearch = 0;
    rc = vec1ListBuilderInit(pBld, pTab, iBld);
    if( rc==SQLITE_OK ){
      rc = vec1GetSql(pTab, VEC1_SQL_SEARCH_IDX_FOR_INSERT, &pSearch);
    }
    if( rc==SQLITE_OK ){
      sqlite3_bind_int(pSearch, 1, iBld);
      sqlite3_bind_int(pSearch, 2, pTab->cfg.nBlocksizeMin);
      if( SQLITE_ROW==sqlite3_step(pSearch) ){
        i64 iId = sqlite3_column_int64(pSearch, 0);
        i64 iFirst = sqlite3_column_int64(pSearch, 1);
        i64 iLast = sqlite3_column_int64(pSearch, 2);
        const u8 *aVal = (const u8*)sqlite3_column_blob(pSearch, 3);
        int nVal = sqlite3_column_bytes(pSearch, 3);
        rc = vec1ListBuilderLoad(pBld, iId, iFirst, iLast, aVal, nVal);
      }
      vec1StmtReset(&rc, pSearch);
    }
  }

  if( rc==SQLITE_OK ){
    rc = vec1ListBuilderAdd(pBld, iRowid, aStore);
  }

  *piBucket = iBld;
  return rc;
}

/*
** Serialize value pVal and append it to buffer pBuf. Vec1 uses a serialization
** format based on SQLite's. First a varint indicating the type and size
** of the value, followed by the data for the value itself. Text values
** are always encoded using utf-8.
**
**        0: NULL value. 0 byte payload.
**        1: Integer value (unsigned). 1 byte payload.
**        2: Integer value (signed). 4 byte payload.
**        3: Integer value (signed). 8 byte payload.
**        4: Real value. 8 byte payload.
**
** Greater than 4 and odd is a text value. Payload size ((v-5)/2). Greater 
** than 4 and even is a blob value. Payload size ((v-6)/2).
*/
static int vec1AppendMetaValue(
  Vec1Buffer *pBuf,
  sqlite3_value *pVal
){
  int rc = SQLITE_OK;
  switch( sqlite3_value_type(pVal) ){
    case SQLITE_NULL: {
      rc = vec1BufferGrow(pBuf, 1);
      if( rc==SQLITE_OK ){
        pBuf->a[pBuf->n++] = 0x00;
      }
      break;
    }

    case SQLITE_INTEGER: {
      i64 iVal = sqlite3_value_int64(pVal);
      rc = vec1BufferGrow(pBuf, 9);
      if( rc==SQLITE_OK ){
        if( iVal>=0 && iVal<=254 ){
          pBuf->a[pBuf->n++] = 0x01;
          pBuf->a[pBuf->n++] = (iVal & 0xFF);
        }
        else if( iVal>=VEC1_META_4BYTEMIN && iVal<=VEC1_META_4BYTEMAX ){
          pBuf->a[pBuf->n++] = 0x02;
          vec1PutU32(&pBuf->a[pBuf->n], (int)iVal);
          pBuf->n += 4;
        }else{
          pBuf->a[pBuf->n++] = 0x03;
          vec1PutU64(&pBuf->a[pBuf->n], iVal);
          pBuf->n += 8;
        }
      }
      break;
    }

    case SQLITE_FLOAT: {
      double fVal = sqlite3_value_double(pVal);
      rc = vec1BufferGrow(pBuf, 9);
      if( rc==SQLITE_OK ){
        i64 iVal;
        memcpy(&iVal, &fVal, sizeof(fVal));
        pBuf->a[pBuf->n++] = 0x04;
        vec1PutU64(&pBuf->a[pBuf->n], iVal);
        pBuf->n += 8;
      }
      break;
    }

    case SQLITE_TEXT: {
      const char *z = (const char*)sqlite3_value_text(pVal);
      int n = sqlite3_value_bytes(pVal);
      rc = vec1BufferGrow(pBuf, n+5);
      if( rc==SQLITE_OK ){
        pBuf->n += vec1PutVarint(&pBuf->a[pBuf->n], (n*2 + 5));
        memcpy(&pBuf->a[pBuf->n], z, n);
        pBuf->n += n;
      }
      break;
    }

    default: {
      const u8 *z = (const u8*)sqlite3_value_blob(pVal);
      int n = sqlite3_value_bytes(pVal);
      rc = vec1BufferGrow(pBuf, n+5);
      if( rc==SQLITE_OK ){
        pBuf->n += vec1PutVarint(&pBuf->a[pBuf->n], (n*2 + 6));
        memcpy(&pBuf->a[pBuf->n], z, n);
        pBuf->n += n;
      }
      assert( sqlite3_value_type(pVal)==SQLITE_BLOB );
      break;
    }
  }

  return rc;
}

static void vec1MetaValueUpdateFlags(const u8 *aMeta, u32 *pFlags){
  switch( aMeta[0] ){
    case 0:             /* NULL value */
      *pFlags |= VEC1_META_HASNULL;
      break;

    case 1:             /* 1-byte integer */
      break;

    case 2:             /* 4-byte integer */
      *pFlags &= ~(VEC1_META_1BYTEINT);
      break;

    case 4:             /* 8-byte real */
      *pFlags &= ~(VEC1_META_1BYTEINT|VEC1_META_4BYTEINT);
      break;

    default:
      *pFlags &= ~(VEC1_META_1BYTEINT|VEC1_META_4BYTEINT|VEC1_META_REAL);
      break;
  }
}

/*
** The meta-list builder passed as the only argument currently contains
** a list of values in non-generic format (e.g. VEC1_META_1BYTEINT format).
** This function converts the list to generic format.
**
** Return SQLITE_OK if successful, or an SQLite error code (SQLITE_NOMEM)
** otherwise.
*/
static int vec1ExpandMetaValue(Vec1MetaBuilder *p){
  Vec1Buffer orig;
  int rc = SQLITE_OK;
  int bHasNull = 0;

  memcpy(&orig, &p->buf, sizeof(Vec1Buffer));
  p->buf.a = 0;
  p->buf.n = VEC1_META_SZHDR;
  p->buf.nAlloc = 0;

  assert( p->format==VEC1_META_1BYTEINT 
       || p->format==VEC1_META_4BYTEINT 
       || p->format==VEC1_META_REAL 
  );

  rc = vec1BufferGrow(&p->buf, (orig.n * 2));
  if( rc==SQLITE_OK ){
    int iIn = VEC1_META_SZHDR;
    int iOut = VEC1_META_SZHDR;

    while( iIn<orig.n ){
      if( p->format==VEC1_META_1BYTEINT ){
        int v = orig.a[iIn++];
        if( v==VEC1_META_1BYTENULL ){
          p->buf.a[iOut++] = 0x00;
          bHasNull = 1;
        }else{
          p->buf.a[iOut++] = 0x01;
          p->buf.a[iOut++] = (v & 0xFF);
        }
      }else if( p->format==VEC1_META_4BYTEINT ){
        u32 v = vec1GetU32(&orig.a[iIn]);
        iIn += sizeof_u32;
        if( v==VEC1_META_4BYTENULL ){
          p->buf.a[iOut++] = 0x00;
          bHasNull = 1;
        }else{
          p->buf.a[iOut++] = 0x02;
          vec1PutU32(&p->buf.a[iOut], (u32)v);
          iOut += sizeof_u32;
        }
      }else{
        u64 v = vec1GetU64(&orig.a[iIn]);
        iIn += sizeof_f64;
        if( v==VEC1_META_REALNULL ){
          p->buf.a[iOut++] = 0x00;
          bHasNull = 1;
        }else{
          p->buf.a[iOut++] = 0x04;
          vec1PutU64(&p->buf.a[iOut], v);
          iOut += sizeof_f64;
        }
      }
    }

    p->buf.n = iOut;
  }

  if( p->format==VEC1_META_1BYTEINT ){
    p->flags = VEC1_META_4BYTEINT | VEC1_META_1BYTEINT | VEC1_META_REAL;
  }else if( p->format==VEC1_META_4BYTEINT ){
    p->flags = VEC1_META_4BYTEINT | VEC1_META_REAL;
  }else{
    p->flags = VEC1_META_REAL;
  }
  p->format = VEC1_META_GENERIC;
  if( bHasNull ){
    p->flags |= VEC1_META_HASNULL;
  }

  vec1BufferFree(&orig);
  return rc;
}

static int vec1WriterMetaFromArray(
  Vec1Writer *pWriter,            /* Writer object accumulating changes */
  int iBucket,                    /* Bucket to write to */
  sqlite3_value **apMeta          /* Meta values associated with prev vector */
){
  Vec1ListBuilder *p = &pWriter->aBld[iBucket];
  int ii;
  int rc = SQLITE_OK;
  assert( p->pTab!=0 );
  for(ii=0; rc==SQLITE_OK && ii<p->pTab->nMeta; ii++){
    Vec1MetaBuilder *pMeta = &p->aMeta[ii];
    Vec1Buffer *pBuf = &pMeta->buf;
    sqlite3_value *pVal = apMeta[ii];

    if( pMeta->format==VEC1_META_1BYTEINT 
     || pMeta->format==VEC1_META_4BYTEINT 
     || pMeta->format==VEC1_META_REAL 
    ){
      int eType = sqlite3_value_type(pVal);

      rc = vec1BufferGrow(pBuf, sizeof_f64);
      if( rc!=SQLITE_OK ) continue;

      if( eType==SQLITE_INTEGER ){
        i64 iVal = sqlite3_value_int64(pVal);

        if( pMeta->format==VEC1_META_1BYTEINT ){
          if( iVal>=0 && iVal<=254 ){
            pBuf->a[pBuf->n++] = (iVal & 0xFF);
            pVal = 0;
          }
        }else{
          if( iVal>=VEC1_META_4BYTEMIN && iVal<=VEC1_META_4BYTEMAX ){
            if( pMeta->format==VEC1_META_4BYTEINT ){
              vec1PutU32(&pBuf->a[pBuf->n], (u32)iVal);
              pBuf->n += sizeof_u32;
            }else{
              double fVal = (double)iVal;
              u64 v;
              memcpy(&v, &fVal, sizeof_f64);
              vec1PutU64(&pBuf->a[pBuf->n], v);
              pBuf->n += sizeof_f64;
            }
            pVal = 0;
          }
        }
      }else if( eType==SQLITE_FLOAT && pMeta->format==VEC1_META_REAL ){
        double fVal = sqlite3_value_double(pVal);
        u64 v;
        memcpy(&v, &fVal, sizeof_f64);
        vec1PutU64(&pBuf->a[pBuf->n], v);
        pBuf->n += sizeof_f64;
        pVal = 0;
      }else if( eType==SQLITE_NULL ){
        rc = vec1BufferGrow(pBuf, sizeof_f64);
        if( pMeta->format==VEC1_META_1BYTEINT ){
          pBuf->a[pBuf->n++] = (VEC1_META_1BYTENULL & 0xFF);
        }
        else if( pMeta->format==VEC1_META_4BYTEINT ){
          vec1PutU32(&pBuf->a[pBuf->n], (u32)VEC1_META_4BYTENULL);
          pBuf->n += sizeof_u32;
        }else{
          vec1PutU64(&pBuf->a[pBuf->n], (u64)VEC1_META_REALNULL);
          pBuf->n += sizeof_f64;
        }
        pVal = 0;
      }

      if( pVal ){
        rc = vec1ExpandMetaValue(pMeta);
      }
    }

    if( pMeta->format==VEC1_META_GENERIC ){
      int iOff = pBuf->n;
      rc = vec1AppendMetaValue(pBuf, pVal);
      if( rc==SQLITE_OK ){
        vec1MetaValueUpdateFlags(&pBuf->a[iOff], &pMeta->flags);
      }
    }
  }
  return rc;
}

static int vec1WriterMetaFromStmt(
  Vec1Writer *pWriter,            /* Writer object accumulating changes */
  int iBucket,                    /* Bucket to write to */
  sqlite3_stmt *pStmt
){
  Vec1ListBuilder *p = &pWriter->aBld[iBucket];
  int ii;
  int rc = SQLITE_OK;
  assert( p->pTab!=0 );
  for(ii=0; rc==SQLITE_OK && ii<p->pTab->nMeta; ii++){
    Vec1Buffer *pBuf = &p->aMeta[ii].buf;
    sqlite3_value *pVal = sqlite3_column_value(pStmt, ii+2);
    int iOff = pBuf->n;
    rc = vec1AppendMetaValue(pBuf, pVal);
    if( rc==SQLITE_OK ){
      vec1MetaValueUpdateFlags(&pBuf->a[iOff], &p->aMeta[ii].flags);
    }
  }
  return rc;
}

/*
** Read nMeta values from buffer pBuf. Use them as the meta values associated
** with the (compressed) vector just appended to bucket iBucket.
*/
static int vec1WriterMetaFromPacked(
  Vec1Writer *pWriter,            /* Writer object accumulating changes */
  int iBucket,                    /* Bucket to write to */
  Vec1Buffer *pBuf,
  int *piOff                      /* IN/OUT: Buffer offset */
){
  int ii;
  int iOff = *piOff;
  for(ii=0; ii<pWriter->pTab->nMeta; ii++){
    Vec1MetaBuilder *pTo = &pWriter->aBld[iBucket].aMeta[ii];
    int n = vec1MetaValueSize(&pBuf->a[iOff]);
    int rc = vec1BufferGrow(&pTo->buf, n);
    vec1MetaValueUpdateFlags(&pBuf->a[iOff], &pTo->flags);
    if( rc!=SQLITE_OK ) return rc;
    memcpy(&pTo->buf.a[pTo->buf.n], &pBuf->a[iOff], n);
    pTo->buf.n += n;
    iOff += n;
  }
  *piOff = iOff;
  return SQLITE_OK;
}

static int vec1PackMetaValues(
  Vec1Tab *pTab,
  Vec1Buffer *pBuf,
  sqlite3_stmt *pStmt
){
  int ii;
  int rc = SQLITE_OK;
  for(ii=0; rc==SQLITE_OK && ii<pTab->nMeta; ii++){
    rc = vec1AppendMetaValue(pBuf, sqlite3_column_value(pStmt, 2+ii));
  }
  return rc;
}

static int vec1WriterQuantized(
  Vec1Writer *pWriter,
  i64 iRowid,
  int iBucket,
  const u8 *aStore
){
  return vec1ListBuilderAdd(&pWriter->aBld[iBucket], iRowid, aStore);
}

static int vec1WriterFinish(Vec1Writer *p, int rcin){
  int rc = rcin;
  if( p ){
    int ii;
    for(ii=0; ii<p->nBld; ii++){
      if( rc==SQLITE_OK ){
        rc = vec1ListBuilderFlush(&p->aBld[ii]);
      }
      vec1ListBuilderFree(&p->aBld[ii]);
    }
    sqlite3_free(p);
  }
  return rc;
}

static void vec1EncodeVectorN(
  const Vec1Model *pMod,
  const float *aVec[VEC1_MULTIMATCH_NVEC],
  u8 *aCode
){
  if( pMod->hdr.flags & VEC1_MODEL_BITQUANT ){
    int nCode = vec1EncodedVectorSize(pMod);
    int vv;
    for(vv=0; vv<VEC1_MULTIMATCH_NVEC; vv++){
      vec1EncodeVector(pMod, aVec[vv], &aCode[vv*nCode]);
    }
  }else{
    const int nCodebook = pMod->hdr.nCodebook;
    const int nCodeElem = pMod->nCodeElem;
    const float *aSub[VEC1_MULTIMATCH_NVEC];
    int vv;
    int k;
    for(k=0; k<nCodebook; k++){
      int aBest[VEC1_MULTIMATCH_NVEC];
      for(vv=0; vv<VEC1_MULTIMATCH_NVEC; vv++){
        aSub[vv] = &aVec[vv][k*nCodeElem];
      }

      vec1BestMatchPlatformN(
          nCodeElem, 
          &pMod->aModel[k * nCodeElem * VEC1_PQ_CODEBOOK_SZ],
          &pMod->aModelNT[k * nCodeElem * VEC1_PQ_CODEBOOK_SZ],
          VEC1_PQ_CODEBOOK_SZ, aSub, aBest, 0
      );

      for(vv=0; vv<VEC1_MULTIMATCH_NVEC; vv++){
        aCode[vv*nCodebook + k] = (u8)aBest[vv];
      }
    }
  }
}

static void vec1TransformInputVectorN(
  const Vec1Model *pMod,          /* Model to transform according to */
  const float *aIn,
  const float *aOut[VEC1_MULTIMATCH_NVEC],
  float *aTmp,                    /* Temp space for vectors, if required */
  int nStride                     /* Pad output vectors to this many fields */
){

  const int nElem = pMod->hdr.nElem;
  int vv;

  for(vv=0; vv<VEC1_MULTIMATCH_NVEC; vv++){
    aOut[vv] = &aIn[vv*nElem];
  }

  if( (pMod->hdr.flags & VEC1_MODEL_ROTATE) ){
    float *aRot[VEC1_MULTIMATCH_NVEC];
    for(vv=0; vv<VEC1_MULTIMATCH_NVEC; vv++){
      aRot[vv] = &aTmp[vv*nStride];
    }

#if defined(VEC1_USE_TRANSPOSITION)
    vec1RotateVectorTransposeN(
        aRot, aOut, nElem, pMod->aRotation, pMod->aRotationNT
    );
#else
    vec1RotateVectorN(aRot, aOut, nElem, pMod->aRotation);
#endif

    memcpy(aOut, aRot, sizeof(aRot));
  }

  else if( (pMod->hdr.flags & VEC1_MODEL_WHT) ){
    for(vv=0; vv<VEC1_MULTIMATCH_NVEC; vv++){
      float *tmp = &aTmp[vv*nStride];
      memcpy(tmp, aOut[vv], nElem * sizeof_f32);
      vec1ApplyWHT(tmp, nElem);
      aOut[vv] = tmp;
    }
  }

  else if( pMod->hdr.eDistance==VEC1_DISTANCE_COS || nStride!=nElem ){
    for(vv=0; vv<VEC1_MULTIMATCH_NVEC; vv++){
      float *tmp = &aTmp[vv*nStride];
      memcpy(tmp, aOut[vv], nElem * sizeof_f32);
      aOut[vv] = tmp;
    }
  }

  /* If this model uses cosine distance, normalize the vectors. */
  if( pMod->hdr.eDistance==VEC1_DISTANCE_COS ){
    for(vv=0; vv<VEC1_MULTIMATCH_NVEC; vv++){
      assert( aOut[vv]==&aTmp[vv*nStride] );
      vec1NormalizeVector(&aTmp[vv*nStride], nElem);
    }
  }
}

/*
** Quantize N vectors so that they can be written to the index. N is always
** VEC1_MULTIMATCH_NVEC. 
*/
static void vec1QuantizeVectorN(
  const Vec1Model *pMod,          /* Current model */
  float *aTmp,                    /* Temporary space - same dim as vectors */
  const float *aVector,           /* Vector to quantize */
  int *aBucket,                   /* OUT: Buckets to put vector in */
  u8 *aCode                       /* OUT: Write PQ codes (if any) here */ 
){
  int nElem = pMod->hdr.nElem;
  const int nBucket = pMod->hdr.nBucket;
  const int nStride = MAX(nElem, pMod->nCodeElem * (int)pMod->hdr.nCodebook);
  const float *aVec[VEC1_MULTIMATCH_NVEC];
  int vv;

  vec1TransformInputVectorN(pMod, aVector, aVec, aTmp, nStride);

  if( nBucket>0 ){
    vec1BestMatchPlatformN(
        nElem, pMod->aCentroid, pMod->aCentroidNT, nBucket, aVec, aBucket, 0
    );

    if( pMod->hdr.nCodebook>0 && (pMod->hdr.flags & VEC1_MODEL_RESIDUAL) ){
      for(vv=0; vv<VEC1_MULTIMATCH_NVEC; vv++){
        float *tmp = &aTmp[vv*nStride];
        vec1Sub(tmp, aVec[vv], &pMod->aCentroid[aBucket[vv]*nElem], nElem);
        aVec[vv] = tmp;
      }
    }

    for(vv=0; vv<VEC1_MULTIMATCH_NVEC; vv++){
      assert( aBucket[vv]>=0 && aBucket[vv]<nBucket );
    }
  }else{
    memset(aBucket, 0, sizeof(int)*VEC1_MULTIMATCH_NVEC);
  }

  if( pMod->hdr.nCodebook>0 ){
    vec1EncodeVectorN(pMod, aVec, aCode);
  }
}

/*
** Each Vec1QuantizeJob has capacity for this many vectors.
*/
#define VEC1_QUANTIZE_JOB_SZ 1024

typedef struct Vec1QuantizeJob Vec1QuantizeJob;
struct Vec1QuantizeJob {
  Vec1Model *pModel;
  Vec1Writer *pWriter;
  int nVector;                    /* Number of vectors to quantize */
  i64 *aRowid;                    /* Array of nVector rowids */
  float *aTmp;                    /* Temp space for one vector */
  float *aVec;                    /* Packed vectors */ 
  int *aBucket;                   /* OUT: Bucket for each vector */
  u8 *aCode;                      /* OUT: Packed codes for vectors */
  int nEncoded;                   /* Size of each encoded vector in bytes */
  Vec1Buffer meta;                /* Packed array of meta values */
};

static void vec1QuantizeJob(void *pCtx){
  Vec1QuantizeJob *p = (Vec1QuantizeJob*)pCtx;
  const int nQuant = VEC1_MULTIMATCH_NVEC;
  const int nElem = p->pModel->hdr.nElem;
  int ii = 0;

  /* Handle the bulk of the vectors VEC1_QUANTIZE_JOB_QUANTA at a time. */
  for(; ii<=(p->nVector-nQuant); ii += nQuant){
    float *v = &p->aVec[nElem * ii];
    u8 *a = &p->aCode[p->nEncoded * ii];
    vec1QuantizeVectorN(p->pModel, p->aTmp, v, &p->aBucket[ii], a);
  }

  /* Scalar tail - handle remaining vectors one at a time. */
  for(; ii<p->nVector; ii++){
    float *v = &p->aVec[nElem * ii];
    u8 *a = &p->aCode[p->nEncoded * ii];
    vec1QuantizeVector(p->pModel, p->aTmp, v, &p->aBucket[ii], a);
  }
}

static int vec1UpdateBase(
  Vec1Tab *pTab,
  i64 iId,
  int iBucket
){
  sqlite3_stmt *pStmt = 0;
  int rc = vec1GetSql(pTab, VEC1_SQL_UPDATE_BASE2, &pStmt);
  if( rc==SQLITE_OK ){
    sqlite3_bind_int(pStmt, 1, iBucket);
    sqlite3_bind_int64(pStmt, 2, iId);
    sqlite3_step(pStmt);
    vec1StmtReset(&rc, pStmt);
  }
  return rc;
}


static int vec1QuantizeJobFinish(void *pCtx, int rcin){
  Vec1QuantizeJob *p = (Vec1QuantizeJob*)pCtx;
  const int nElem = p->pModel->hdr.nElem;
  int rc = rcin;
  int ii;
  int bBucketBase = vec1ModelIsFlat(p->pModel);
  int iOffMeta = 0;

  assert( bBucketBase==0 || p->pModel->hdr.nCodebook==0 );

  for(ii=0; rc==SQLITE_OK && ii<p->nVector; ii++){
    int iBucket = p->aBucket[ii];
    const u8 *aStore;
    if( p->nEncoded>0 ){
      aStore = &p->aCode[ii * p->nEncoded];
    }else{
      aStore = (const u8*)&p->aVec[ii * nElem];
    }
    rc = vec1WriterQuantized(p->pWriter, p->aRowid[ii], iBucket, aStore);
    if( bBucketBase && rc==SQLITE_OK ){
      rc = vec1UpdateBase(p->pWriter->pTab, p->aRowid[ii], iBucket);
    }
    if( rc==SQLITE_OK ){
      rc = vec1WriterMetaFromPacked(p->pWriter, iBucket, &p->meta, &iOffMeta);
    }
  }

  vec1BufferFree(&p->meta);
  sqlite3_free(p);
  return rc;
}

static int vec1UpdateBaseRows(
  Vec1Tab *pTab,
  i64 iFirst,
  i64 iLast
){
  sqlite3_stmt *pStmt = 0;
  int rc = vec1GetSql(pTab, VEC1_SQL_ZERO_BASE_RANGE, &pStmt);
  if( rc==SQLITE_OK ){
    sqlite3_bind_int64(pStmt, 1, iFirst);
    sqlite3_bind_int64(pStmt, 2, iLast);
    sqlite3_step(pStmt);
    vec1StmtReset(&rc, pStmt);
  }
  return rc;
}


#define VEC1_ROWS_PER_BASE_UPDATE 64

/*
** Rebuild the entire ANN index from scratch, based on the current contents 
** of the %_base table.
*/
static int vec1RebuildIndex(Vec1Tab *pTab, int nThread){
  const int nElem = pTab->cfg.nElem;
  sqlite3_stmt *pStmt = 0;
  int rc = SQLITE_OK;
  int bZeroBase = 0;              /* True to set base.vector=0 */

  Vec1Writer *p = 0;              /* Write buffer object */
  Vec1JobQueue *pQueue = 0;       /* Job queue for multi-threaded rebuilds */
  Vec1QuantizeJob *pQJ = 0;
  int nRow = 0;
  i64 iFirstInRange = VEC1_SMALLEST_INT64;

  assert( pTab->cfg.nElem==(int)pTab->mod.hdr.nElem || pTab->mod.hdr.nElem==0 );

  /* Clear any existing ANN index */
  rc = vec1SqlExec(pTab, "DELETE FROM %Q.'%q_idx'", 0);
  if( rc==SQLITE_OK ){
    rc = vec1SqlExec(pTab, "DELETE FROM %Q.'%q_meta'", 0);
  }

  if( rc!=SQLITE_OK || (pTab->mod.hdr.flags & VEC1_MODEL_INDEX)==0 ) return rc;


  /* Allocate a writer object. Create transposed centroids if required. */
  rc = vec1WriterAlloc(pTab, 1, &p);
  if( rc==SQLITE_OK ){
    rc = vec1ModelAllocTCent(&pTab->mod);
  }

  /* If multiple threads have been configured and the model is not 'flat',
  ** create a job queue so that the rebuild can use multiple threads. There
  ** is nothing for background threads to do for 'flat' indexes.  */
  if( vec1ModelIsFlat(&pTab->mod) && pTab->mod.hdr.nBucket==0 ){
    bZeroBase = 1;
  }
#if VEC1_THREADS
  if( rc==SQLITE_OK && nThread>1 && bZeroBase==0 ){
    pQueue = vec1JobQueueNew(nThread-1);
    if( pQueue==0 ){
      rc = SQLITE_NOMEM;
    }
  }
#endif /* VEC1_THREADS */

  if( rc==SQLITE_OK ){
    rc = vec1GetSql(pTab, VEC1_SQL_SCAN_BASE, &pStmt);
  }

  while( rc==SQLITE_OK && SQLITE_ROW==sqlite3_step(pStmt) ){
    i64 id = sqlite3_column_int64(pStmt, 0);
    int nVec = 0;
    const float *aVec = 0;

    nRow++;
    if( bZeroBase && nRow==VEC1_ROWS_PER_BASE_UPDATE ){
      rc = vec1UpdateBaseRows(pTab, iFirstInRange, id);
      if( rc!=SQLITE_OK ) break;
      iFirstInRange = id+1;
      nRow = 0;
    }

    nVec = sqlite3_column_bytes(pStmt, 1);
    aVec = (const float*)sqlite3_column_blob(pStmt, 1);

    if( nVec!=(sizeof_f32*nElem) ){
      rc = SQLITE_ERROR;
      vec1VtabError(
        pTab, "vec1: unexpected vector size in %q_base: %d", pTab->zName, nVec
      );
    }else{
      if( bZeroBase ){
        int iBucket = -1;         /* Must be -1 for vec1WriterVector()! */
        rc = vec1WriterVector(p, id, aVec, &iBucket);
        if( rc==SQLITE_OK ){
          rc = vec1WriterMetaFromStmt(p, iBucket, pStmt);
        }
      }else{
        if( pQJ==0 ){
          int nEncoded = vec1EncodedVectorSize(&pTab->mod);
          int nByte = sizeof(Vec1QuantizeJob) +
            sizeof(i64) * VEC1_QUANTIZE_JOB_SZ +               /* aRowid */
            pTab->nTmpVec * sizeof_f32 * VEC1_MULTIMATCH_NVEC +/* aTmp */
            nElem * sizeof_f32 * VEC1_QUANTIZE_JOB_SZ +        /* aVec */
            sizeof(int) * VEC1_QUANTIZE_JOB_SZ +               /* aBucket */
            nEncoded * VEC1_QUANTIZE_JOB_SZ;                   /* aCode */

          pQJ = (Vec1QuantizeJob*)sqlite3_malloc(nByte);
          if( pQJ==0 ){
            rc = SQLITE_NOMEM;
            break;
          }else{
            memset(pQJ, 0, sizeof(Vec1QuantizeJob));
            pQJ->pModel = &pTab->mod;
            pQJ->pWriter = p;
            pQJ->aRowid = (i64*)&pQJ[1];
            pQJ->aTmp = (float*)&pQJ->aRowid[VEC1_QUANTIZE_JOB_SZ];
            pQJ->aVec = &pQJ->aTmp[pTab->nTmpVec * VEC1_MULTIMATCH_NVEC];
            pQJ->aBucket = (int*)&pQJ->aVec[nElem * VEC1_QUANTIZE_JOB_SZ];
            pQJ->aCode = (u8*)&pQJ->aBucket[VEC1_QUANTIZE_JOB_SZ];
            pQJ->nEncoded = nEncoded;
            memset(pQJ->aTmp, 0, sizeof_f32*pTab->nTmpVec*VEC1_MULTIMATCH_NVEC);
          }
        }

        memcpy(&pQJ->aVec[nElem * pQJ->nVector], aVec, nVec);
        pQJ->aRowid[pQJ->nVector] = id;
        pQJ->nVector++;
        rc = vec1PackMetaValues(pTab, &pQJ->meta, pStmt);

        if( rc==SQLITE_OK && pQJ->nVector==VEC1_QUANTIZE_JOB_SZ ){
          rc = vec1JobQueueAddJob(
              pQueue, vec1QuantizeJob, vec1QuantizeJobFinish, (void*)pQJ
          );
          pQJ = 0;
        }
      }
    }
  }

  if( rc==SQLITE_OK && bZeroBase ){
    rc = vec1UpdateBaseRows(pTab, iFirstInRange, VEC1_LARGEST_INT64);
  }

  if( pQJ ){
    if( rc==SQLITE_OK ){
      rc = vec1JobQueueAddJob(
          pQueue, vec1QuantizeJob, vec1QuantizeJobFinish, (void*)pQJ
      );
    }else{
      vec1BufferFree(&pQJ->meta);
      sqlite3_free(pQJ);
    }
  }
  rc = vec1JobQueueFinishJobs(pQueue, rc);
  vec1JobQueueFree(pQueue);

  rc = vec1WriterFinish(p, rc);
  vec1StmtReset(&rc, pStmt);
  vec1ModelFreeTCent(&pTab->mod);
  return rc;
}

/*
** The table currently has a flat index installed - an index with 
** nCodesize==0 - and bucket numbers instead of vectors in the %_base 
** table. This function scans the index, copying full vectors back into
** the %_base table and deleting %_idx entries as it goes.
**
** Return SQLITE_OK if successful, or an SQLite error code otherwise.
*/
static int vec1FlatToNone(Vec1Tab *pTab){
  int rc = SQLITE_OK;
  sqlite3_stmt *pStmt = 0;
  sqlite3_stmt *pUp = 0;
  sqlite3_stmt *pDel = 0;

  rc = vec1GetSql(pTab, VEC1_SQL_SCAN_IDX, &pStmt);
  if( rc==SQLITE_OK ){
    rc = vec1GetSql(pTab, VEC1_SQL_UPDATE_BASE, &pUp);
  }
  if( rc==SQLITE_OK ){
    rc = vec1GetSql(pTab, VEC1_SQL_DELETE_FROM_IDX, &pDel);
  }
  while( rc==SQLITE_OK && SQLITE_ROW==sqlite3_step(pStmt) ){
    Vec1FlatIter iter;
    const u8 *aBlob = (const u8*)sqlite3_column_blob(pStmt, 0);
    int nBlob = sqlite3_column_bytes(pStmt, 0);
    i64 iRowid = sqlite3_column_int64(pStmt, 2);
    rc = vec1FlatIterStart(pTab, &iter, aBlob, nBlob);

    while( rc==SQLITE_OK && iter.aVec!=0 ){
      rc = sqlite3_bind_blob(pUp, 1, iter.aVec, iter.szVec, SQLITE_STATIC);
      sqlite3_bind_int64(pUp, 2, iter.iRowid);
      sqlite3_step(pUp);
      vec1StmtReset(&rc, pUp);
      vec1FlatIterNext(&iter);
    }

    if( rc==SQLITE_OK ){
      sqlite3_bind_int64(pDel, 1, iRowid);
      sqlite3_step(pDel);
      vec1StmtReset(&rc, pDel);
    }
  }
  if( pUp ) sqlite3_clear_bindings(pUp);
  vec1StmtReset(&rc, pStmt);

  return rc;
}

/*
** Symbols for the commands the vtab accepts. These must match the array
** in vec1InterpretCmdName().
*/
#define VEC1_CMD_BLOCKSIZE     0
#define VEC1_CMD_REBUILD       1
#define VEC1_CMD_BLOCKSIZE_MIN 2

static int vec1InterpretCmdName(Vec1Tab *pTab, const char *zCmd, int *piCmd){
  const char *azCmd[] = {
    "blocksize",
    "rebuild",
    "blocksize-min",
    0
  };
  int iCmd;

  for(iCmd=0; iCmd<size_of_array(azCmd); iCmd++){
    if( 0==sqlite3_stricmp(zCmd, azCmd[iCmd]) ){
      *piCmd = iCmd;
      return SQLITE_OK;
    }
  }

  vec1VtabError(pTab, "vec1: unknown command: %s", zCmd);
  return SQLITE_ERROR;
}

static int vec1InterpretInteger(
  Vec1Tab *pTab,
  sqlite3_value *pVal,
  const char *zName,
  i64 iMin,
  i64 iMax
){
  if( sqlite3_value_numeric_type(pVal)==SQLITE_INTEGER ){
    i64 iVal = sqlite3_value_int64(pVal);
    if( iVal>=iMin && iVal<=iMax ) return SQLITE_OK;
  }

  vec1VtabError(
      pTab, "'%s' requires an integer value between %lld and %lld", 
      zName, iMin, iMax
  );
  return SQLITE_ERROR;
}

static int vec1WriteConfigI64(Vec1Tab *pTab, int iField, i64 iVal){
  sqlite3_stmt *pStmt = 0;
  int rc = SQLITE_OK;

  rc = vec1GetSql(pTab, VEC1_SQL_WRITE_CONFIG, &pStmt);
  if( rc==SQLITE_OK ){
    sqlite3_bind_int(pStmt, 1, iField);
    sqlite3_bind_int64(pStmt, 2, iVal);
    sqlite3_step(pStmt);
    rc = sqlite3_reset(pStmt);
  }

  return rc;
}

/*
** Fix the vector size to nVec *bytes*. This is done when the first 
** vector is inserted into the table. Return SQLITE_OK if successful, or
** an SQLite error code if an error occurs.
*/
static int vec1FixVectorSize(Vec1Tab *pTab, int nVec){
  int rc = SQLITE_OK;

  if( (nVec % sizeof_f32) 
   || (nVec<(VEC1_VECSIZE_MIN*sizeof_f32))
   || (nVec>(VEC1_VECSIZE_MAX*sizeof_f32))
  ){
    vec1VtabError(pTab, "invalid vector size: %d bytes", nVec);
    rc = SQLITE_ERROR;
  }else{
    int nElem = nVec/sizeof_f32;
    rc = vec1WriteConfigI64(pTab, VEC1_CONFIG_NELEM, nElem);
    if( rc==SQLITE_OK ) pTab->cfg.nElem = nElem;
  }

  return rc;
}


/*
** Handle a "special" INSERT command on a vec1 table. A special INSERT takes
** the form:
**
**     INSERT INTO vec1_table(cmd, vector) VALUES('cmd-name', pArg);
**
** The zCmd argument to this function is the text value specified for column
** "cmd" in the INSERT statement. Parameter pArg is the pArg value from the
** INSERT.
**
** Return SQLITE_OK if successful, or an SQLite error code otherwise. If
** an error code is returned, an error message may be left in pTab.
*/
static int vec1SpecialInsert(
  Vec1Tab *pTab, 
  const char *zCmd, 
  sqlite3_value *pArg
){
  int rc = SQLITE_OK;
  int iCmd = 0;

  rc = vec1InterpretCmdName(pTab, zCmd, &iCmd);

  if( rc==SQLITE_OK ){
    switch( iCmd ){
      case VEC1_CMD_BLOCKSIZE: {
        rc = vec1InterpretInteger(
            pTab, pArg, "blocksize", VEC1_BLOCKSIZE_MIN, VEC1_BLOCKSIZE_MAX
        );
        if( rc==SQLITE_OK ){
          rc = vec1WriteConfigI64(
              pTab, VEC1_CONFIG_BLOCKSIZE, sqlite3_value_int64(pArg)
          );
        }
        break;
      };
      case VEC1_CMD_BLOCKSIZE_MIN: {
        rc = vec1InterpretInteger(
            pTab, pArg, "blocksize-min", VEC1_BLOCKSIZE_MIN, VEC1_BLOCKSIZE_MAX
        );
        if( rc==SQLITE_OK ){
          rc = vec1WriteConfigI64(
              pTab, VEC1_CONFIG_BLOCKSIZE_MIN, sqlite3_value_int64(pArg)
          );
        }
        break;
      }

      default: assert( iCmd==VEC1_CMD_REBUILD ); {

        /* TODO: Should this be disallowed if there are active readers? */
        if( rc==SQLITE_OK && sqlite3_value_type(pArg)!=SQLITE_NULL ){
          u8 aFlat[VEC1_HEADER_SIZE];
          Vec1Model mod;
          int nByte = 0;
          const u8 *aBlob = 0;
          sqlite3_stmt *pStmt = 0;

          memset(&mod, 0, sizeof(mod));
          if( sqlite3_value_type(pArg)==SQLITE_BLOB ){
            nByte = sqlite3_value_bytes(pArg);
            aBlob = sqlite3_value_blob(pArg);
          }else{
            Vec1FlatIndex idx = {0, 0};
            const char *zText = (const char*)sqlite3_value_text(pArg);
            char *zErr = 0;

            idx.eIndex = 0;
            idx.eDistance = VEC1_DISTANCE_L2;
            rc = vec1ParseJsonConfig(
                pTab->db, zText, vec1FlatIndexCfg, &idx, &zErr
            );
            if( rc==SQLITE_OK && idx.eIndex==0 ){
              zErr = sqlite3_mprintf("required parameter missing: index");
              rc = SQLITE_ERROR;
            }
            if( zErr!=0 ){
              vec1VtabError(pTab, "vec1: %z", zErr);
            }
            if( rc==SQLITE_OK ){
              vec1HeaderWrite(aFlat, 
                  ((idx.eIndex==VEC1_INDEX_FLAT) ? VEC1_MODEL_INDEX : 0), 
                  0, 0, 0, idx.eDistance
              );
              aBlob = aFlat;
              nByte = VEC1_HEADER_SIZE;
            }
          }

          /* Decode the model blob here. Just to check if doing so causes any
          ** errors. If it does, refuse to store it.  */
          if( rc==SQLITE_OK ){
            rc = vec1DecodeModel(aBlob, nByte, &mod, &pTab->base.zErrMsg);
          }

          if( rc==SQLITE_OK && mod.hdr.nElem>0 ){
            if( pTab->cfg.nElem==0 ){
              rc = vec1FixVectorSize(pTab, mod.hdr.nElem*sizeof_f32);
            }else if( pTab->cfg.nElem!=(int)mod.hdr.nElem ){
              vec1VtabError(pTab, 
                  "vec1: model/data vector size mismatch: %d/%d",
                  (int)mod.hdr.nElem, pTab->cfg.nElem
              );
              rc = SQLITE_ERROR;
            }
          }

          /* Store the new model in the %_model table. */
          if( rc==SQLITE_OK ){
            const char zStore[] = "REPLACE INTO %Q.'%q_model' VALUES(1, ?)";
            rc = vec1PrepareSql(pTab, &pStmt, zStore, pTab->zDb, pTab->zName);
          }
          if( rc==SQLITE_OK ){
            rc = sqlite3_bind_blob(pStmt, 1, aBlob, nByte, SQLITE_STATIC);
            sqlite3_step(pStmt);
            vec1StmtFinalize(&rc, pStmt);
          }

          /* Increment (or create) the model version number in %_config */
          if( rc==SQLITE_OK ){
            rc = vec1GetSql(pTab, VEC1_SQL_INCR_CONFIG, &pStmt);
          }
          if( rc==SQLITE_OK ){
            sqlite3_bind_int(pStmt, 1, VEC1_CONFIG_MODEL);
            sqlite3_step(pStmt);
            rc = sqlite3_reset(pStmt);
          }
        }

        if( rc==SQLITE_OK && vec1ModelIsFlat(&pTab->mod) ){
          rc = vec1FlatToNone(pTab);
        }
        if( rc==SQLITE_OK ){
          rc = vec1LoadConfig(pTab);
        }
        if( rc==SQLITE_OK ){
          rc = vec1RebuildIndex(pTab, pTab->pTabList->nThread);
        }
      }
    }
  }

  return rc;
}

/*
** The maximum permitted number of tombstone entries in list containing
** nEntry entries. If this threshold is exceeded, the list is reorganized
** to reduce the number of tombstones to 0.
*/
#define VEC1_TOMBSTONE_THRESHOLD(nEntry) ((nEntry * 2) / 10)

/*
** Delete the %_base entry for the row with rowid=iRowid. Also zero-out
** the corresponding %_idx entry.
*/
static int vec1DeleteByRowid(Vec1Tab *pTab, i64 iRowid){
  const u8 aTomb4[4] = {0xFF, 0xFF, 0xFF, 0xFF};
  const u8 aTomb8[8] = {0, 0, 0, 0, 0, 0, 0, 0};

  sqlite3_stmt *pDelete = 0;
  int rc = SQLITE_OK;

  rc = vec1GetSql(pTab, VEC1_SQL_DEL_LOOKUP_BASE, &pDelete);
  if( rc==SQLITE_OK ){
    int iBucket = 0;
    sqlite3_bind_int64(pDelete, 1, iRowid);
    if( SQLITE_ROW==sqlite3_step(pDelete) && pTab->mod.hdr.nBucket>0 ){
      if( pTab->mod.hdr.nCodebook==0 ){
        iBucket = sqlite3_column_int(pDelete, 0);
      }else{
        const u8 *aVec = sqlite3_column_blob(pDelete, 0);
        int nVec = sqlite3_column_bytes(pDelete, 0);
        const float *aTransform = 0;

        aTransform = vec1TransformInputVector(
            &pTab->mod, pTab->aTmpVec, (const float*)aVec
        );

        if( nVec!=(pTab->cfg.nElem*sizeof_f32) ){
          rc = VEC1_CORRUPT;
        }else{
          iBucket = vec1PqBestMatch(
              pTab->mod.aCentroid, pTab->mod.hdr.nBucket, 
              aTransform, pTab->cfg.nElem, 0
          );
        }
      }
    }

    vec1StmtReset(&rc, pDelete);

    if( rc==SQLITE_OK && (pTab->mod.hdr.flags & VEC1_MODEL_INDEX) ){
      Vec1RowidLocation loc;

      rc = vec1FindByRowid(pTab, iBucket, 1, iRowid, &loc);

      if( rc==SQLITE_OK ){
        if( loc.pList==0 && loc.pBlob==0 ){
          /* Missing row. This is an error. */
          rc = VEC1_CORRUPT;
        }else if( (loc.nTombstone+1) > VEC1_TOMBSTONE_THRESHOLD(loc.nEntry) ){
          /* This delete would cause the list's tombstone count to exceed
          ** the threshold. So reorganize the list and remove iRowid in
          ** one go.  */
          if( loc.pList ){
            vec1ListBuilderCompress(loc.pList, iRowid);
          }else{
            /* Load the list into memory, compress it so there are no
            ** tombstones, then write it back to the database.  */
            Vec1ListBuilder list;
            int nVal = sqlite3_blob_bytes(loc.pBlob);
            u8 *aVal = sqlite3_malloc(nVal);
            memset(&list, 0, sizeof(list));
            if( aVal==0 ){
              rc = SQLITE_NOMEM;
            }else{
              rc = sqlite3_blob_read(loc.pBlob, aVal, nVal, 0);
            }
            if( rc==SQLITE_OK ){
              rc = vec1ListBuilderInit(&list, pTab, iBucket);
            }
            if( rc==SQLITE_OK ){
              /* Initialize iFirst and iLast to 0. vec1ListBuilderCompress() 
              ** will set the correct values. */
              rc = vec1ListBuilderLoad(&list, loc.iIdx, 0, 0, aVal, nVal);
            }
            if( rc==SQLITE_OK ){
              rc = vec1ListBuilderCompress(&list, iRowid);
            }
            if( rc==SQLITE_OK ){
              rc = vec1ListBuilderFlush(&list);
            }
            vec1ListBuilderFree(&list);
            sqlite3_free(aVal);
          }
        }else{
          /* Replace the rowid with a tombstone and increment the tombstone
          ** count.  */
          const u8 *aTomb = loc.szRowid==4 ? aTomb4 : aTomb8;
          if( loc.pList ){
            int iOff = loc.iEntry * loc.szRowid;
            memcpy(&loc.pList->bufRowid.a[iOff], aTomb, loc.szRowid);
            loc.pList->nTombstone++;
          }else{
            int iOff = VEC1_LIST_SZHDR + (loc.iEntry * loc.szRowid);
            rc = sqlite3_blob_write(loc.pBlob, aTomb, loc.szRowid, iOff);
            if( rc==SQLITE_OK ){
              u8 aTS[4];
              vec1PutU32(aTS, loc.nTombstone+1);
              rc = sqlite3_blob_write(loc.pBlob, aTS, sizeof(aTS), 8);
            }
          }
        }

        if( loc.pBlob ){
          int rc2 = sqlite3_blob_close(loc.pBlob);
          if( rc==SQLITE_OK ) rc = rc2;
        }
      }
    }
  }

  return rc;
}

/*
** xUpdate
**
** argc/argv meanings:
**   argc==1                 DELETE
**   argc>1 && argv[0]==NULL INSERT
**   argc>1 && argv[0]!=NULL UPDATE
**
** argv[0]  = old rowid (or NULL)
** argv[1]  = new rowid (or NULL to auto-assign)
** argv[2+] = column values
*/
static int vec1UpdateMethod(
  sqlite3_vtab *pVtab,
  int argc,
  sqlite3_value **argv,
  sqlite3_int64 *pRowid
){
  int rc = SQLITE_OK;
  Vec1Tab *pTab = (Vec1Tab*)pVtab;
  const int iCmd = 2 + VEC1_COLUMN_CMD;
  int bDeleteRow = (argc==1);     /* Delete row at end of this function */

  /* Ensure the configuration has been loaded. */
  if( (rc = vec1LoadConfigIfRequired(pTab))!=SQLITE_OK ) return rc;

  if( argc>1 ){
    int nVec = sqlite3_value_bytes(argv[2+VEC1_COLUMN_VECTOR]);
    const void *aVec = sqlite3_value_blob(argv[2+VEC1_COLUMN_VECTOR]);
    int eType = sqlite3_value_type(argv[2+VEC1_COLUMN_VECTOR]);

    /* Check for a special INSERT. Handle these separately. */
    if( sqlite3_value_type(argv[iCmd])!=SQLITE_NULL ){
      const char *zCmd = (char*)sqlite3_value_text(argv[iCmd]);
      sqlite3_value *pVal = argv[2+VEC1_COLUMN_ARG];
      if( sqlite3_value_type(pVal)==SQLITE_NULL ){
        pVal = argv[2+VEC1_COLUMN_VECTOR];
      }
      return vec1SpecialInsert(pTab, zCmd, pVal);
    }

    /* Check that the supplied vector is the right type and size. If this
    ** is the first vector written to this table, fix the vector size
    ** now.  */
    if( eType!=SQLITE_BLOB ){
      rc = vec1VectorTypeError(pTab, eType);
    }else if( pTab->cfg.nElem>0 ){
      if( nVec!=pTab->cfg.nElem*sizeof_f32 ){
        rc = vec1VectorSizeError(pTab, nVec);
      }
    }else{
      rc = vec1FixVectorSize(pTab, nVec);
    }

    /* Check that the new vector does not contain any NaN or Infinity values */
    if( rc==SQLITE_OK ){
      rc = vec1VectorIsNonFinite((float*)aVec, pTab->cfg.nElem);
      if( rc!=SQLITE_OK ){
        vec1VtabError(pTab, "vec1: non-finite value not allowed in vector");
      }
    }

    /* If this is an UPDATE that does not modify the rowid, then delete
    ** the existing row now.  Or, if it is an UPDATE that does modify
    ** the rowid, schedule deletion of the existing row at the end
    ** of this function. */
    if( rc==SQLITE_OK && sqlite3_value_type(argv[0])==SQLITE_INTEGER ){
      if( sqlite3_value_int64(argv[0])==sqlite3_value_int64(argv[1]) ){
        rc = vec1DeleteByRowid(pTab, sqlite3_value_int64(argv[0]));
      }else{
        bDeleteRow = 1;
      }
    }
  
    /* If this is an INSERT or UPDATE, write the new row. */
    if( rc==SQLITE_OK ){
      int iBucket = 0;

      /* If there is an index (i.e. not index:"none"), then allocate an
      ** index writer. */
      if( rc==SQLITE_OK 
       && (pTab->mod.hdr.flags & VEC1_MODEL_INDEX) 
       && pTab->pWriter==0 
      ){
        rc = vec1WriterAlloc(pTab, 0, &pTab->pWriter);
        assert( rc!=SQLITE_OK || pTab->pWriter!=0 );
      }

      /* Write to the %_base table. */
      if( rc==SQLITE_OK ){
        sqlite3_stmt *pInsert = 0;

        /* If required, quantize the vector. This is because in
        ** some configurations, the bucket is written into the %_base 
        ** table, so it must be determined now, before the base table entry
        ** is written */
        if( pTab->pWriter ){
          Vec1Writer *p = pTab->pWriter;
          vec1QuantizeVector(&pTab->mod, p->aResidual, aVec, &iBucket, p->aPQ);
        }

        *pRowid = 0;
        rc = vec1GetSql(pTab, VEC1_SQL_INSERT_BASE, &pInsert);
        if( rc==SQLITE_OK ){
          int ii;
          sqlite3_bind_value(pInsert, 1, argv[1]);
          if( vec1ModelIsFlat(&pTab->mod) ){
            sqlite3_bind_int(pInsert, 2, iBucket);
          }else{
            sqlite3_bind_blob(pInsert, 2, aVec, nVec, SQLITE_STATIC);
          }
          for(ii=0; ii<pTab->nMeta; ii++){
            sqlite3_bind_value(pInsert, 3+ii, argv[2+VEC1_COLUMN_VECTOR+1+ii]);
          }

          sqlite3_step(pInsert);
          rc = sqlite3_reset(pInsert);
          *pRowid = sqlite3_last_insert_rowid(pTab->db);
        }
      }

      /* If the table has been trained, write the %_idx entry. And, if
      ** there are any meta-data columns, the %_meta entries as well. */
      if( pTab->mod.hdr.flags & VEC1_MODEL_INDEX ){
        if( rc==SQLITE_OK ){
          rc = vec1WriterVector(pTab->pWriter, *pRowid, aVec, &iBucket);
        }
        if( rc==SQLITE_OK ){
          sqlite3_value **aMeta = &argv[2+VEC1_COLUMN_VECTOR+1];
          rc = vec1WriterMetaFromArray(pTab->pWriter, iBucket, aMeta);
        }
      }
    }
  }

  if( rc==SQLITE_OK && bDeleteRow ){
    rc = vec1DeleteByRowid(pTab, sqlite3_value_int64(argv[0]));
  }

  return rc;
}

static int vec1FinishWriter(Vec1Tab *pTab, int bDiscard){
  int rc = SQLITE_OK;
  if( bDiscard ){
    vec1WriterFinish(pTab->pWriter, SQLITE_ERROR);
  }else{
    rc = vec1WriterFinish(pTab->pWriter, SQLITE_OK);
  }
  pTab->pWriter = 0;
  return rc;
}

static int vec1SyncMethod(sqlite3_vtab *pVtab){
  int rc = SQLITE_OK;
  Vec1Tab *pTab = (Vec1Tab*)pVtab;
  if( (rc = vec1LoadConfigIfRequired(pTab))!=SQLITE_OK ) return rc;
  return vec1FinishWriter(pTab, 0);
}

static int vec1BeginMethod(sqlite3_vtab *pVtab){
  Vec1Tab *pTab = (Vec1Tab*)pVtab;
  int rc = SQLITE_OK;
  rc = vec1LoadConfig(pTab);
  return rc;
}

/*
** xRename method for vec1 virtual table. This renames all shadow tables
** and the %_idx_idx index on the %_idx table.
*/
static int vec1RenameMethod(sqlite3_vtab *pVtab, const char *zNew){
  const char zFmt[] = 
    "ALTER TABLE %Q.'%q_config' RENAME TO '%q_config';"
    "ALTER TABLE %Q.'%q_base' RENAME TO '%q_base';"
    "ALTER TABLE %Q.'%q_idx' RENAME TO '%q_idx';"
    "ALTER TABLE %Q.'%q_model' RENAME TO '%q_model';"
    "ALTER TABLE %Q.'%q_meta' RENAME TO '%q_meta';"
    "UPDATE %Q.sqlite_schema SET name = '%q_idx_idx', sql="
    "  'CREATE INDEX ' || quote('%q_idx_idx') || "
    "  ' ON ' || quote('%q_idx') || '(bucket, first, last)'"
    "WHERE type='index' AND name='%q_idx_idx';"
  ;

  Vec1Tab *pTab = (Vec1Tab*)pVtab;/* Virtual table object */
  int rc = SQLITE_OK;             /* Return value */
  int bDefensive = 0;             /* Initial value of DEFENSIVE */
  int bWritable = 0;              /* Initial value of WRITABLE_SCHEMA */
  char *zSql = 0;                 /* SQL script to rename shadow objects */

  /* Query the initial values of the DEFENSIVE and WRITABLE_SCHEMA db-config 
  ** options. Then clear DEFENSIVE and set WRITABLE_SCHEMA so that we can
  ** UPDATE the sqlite_schema entry of the index directly.  */
  sqlite3_db_config(pTab->db, SQLITE_DBCONFIG_DEFENSIVE, -1, &bDefensive);
  sqlite3_db_config(pTab->db, SQLITE_DBCONFIG_WRITABLE_SCHEMA, -1, &bWritable);
  sqlite3_db_config(pTab->db, SQLITE_DBCONFIG_DEFENSIVE, 0, (int*)0);
  sqlite3_db_config(pTab->db, SQLITE_DBCONFIG_WRITABLE_SCHEMA, 1, (int*)0);

  /* Run the SQL script to rename the shadow tables and index */
  zSql = sqlite3_mprintf(zFmt, 
      pTab->zDb, pTab->zName, zNew,         /* %_config */
      pTab->zDb, pTab->zName, zNew,         /* %_base */
      pTab->zDb, pTab->zName, zNew,         /* %_idx */
      pTab->zDb, pTab->zName, zNew,         /* %_model */
      pTab->zDb, pTab->zName, zNew,         /* %_meta */
      pTab->zDb, zNew, zNew, zNew, pTab->zName
  );
  if( zSql==0 ){
    rc = SQLITE_NOMEM;
  }else{
    rc = sqlite3_exec(pTab->db, zSql, 0, 0, &pTab->base.zErrMsg);
    sqlite3_free(zSql);
  }

  /* Restore the values of DEFENSIVE and WRITABLE_SCHEMA */
  sqlite3_db_config(pTab->db,SQLITE_DBCONFIG_DEFENSIVE, bDefensive, (int*)0);
  sqlite3_db_config(pTab->db,SQLITE_DBCONFIG_WRITABLE_SCHEMA,bWritable,(int*)0);

  return rc;
}

static int vec1RollbackMethod(sqlite3_vtab *pVtab){
  Vec1Tab *pTab = (Vec1Tab*)pVtab;
  vec1UnloadModel(pTab);
  return vec1FinishWriter(pTab, 1);
}
static int vec1SavepointMethod(sqlite3_vtab *pVtab, int iSavepoint){
  Vec1Tab *pTab = (Vec1Tab*)pVtab;
  int rc = SQLITE_OK;
  UNUSED_PARAMETER(iSavepoint);
  if( (rc = vec1LoadConfigIfRequired(pTab))!=SQLITE_OK ) return rc;
  return vec1FinishWriter(pTab, 0);
}
static int vec1ReleaseMethod(sqlite3_vtab *pVtab, int iSavepoint){
  Vec1Tab *pTab = (Vec1Tab*)pVtab;
  int rc = SQLITE_OK;
  UNUSED_PARAMETER(iSavepoint);
  if( (rc = vec1LoadConfigIfRequired(pTab))!=SQLITE_OK ) return rc;
  return vec1FinishWriter(pTab, 0);
}
static int vec1RollbackToMethod(sqlite3_vtab *pVtab, int iSavepoint){
  Vec1Tab *pTab = (Vec1Tab*)pVtab;
  UNUSED_PARAMETER(iSavepoint);
  vec1FinishWriter(pTab, 1);
  vec1UnloadConfig(pTab);
  return SQLITE_OK;
}

/*
** This function is used by the integrity-check code to handle meta-data.
** Argument pBuf contains a meta-data record, complete with VEC1_META_SZHDR
** byte header. When this function is called, (*piOff) contains the offset
** into this buffer of the next meta-data value to read.
**
** When called, this function reads one meta-data value from (*piOff) and
** advances (*piOff) to point to the next value. If pVal is NULL, it returns
** SQLITE_OK at that point. Or, if pVal is not NULL, it is compared to
** the value just read. If they match, SQLITE_OK is returned. Otherwise,
** SQLITE_ERROR.
*/
static int vec1IntegrityMetaValue(
  Vec1Buffer *pBuf,
  int *piOff,
  sqlite3_value *pVal
){
  u32 f = vec1GetU32(pBuf->a);
  int iOff = *piOff;
  Vec1MetaValue val;
  int rc = SQLITE_OK;

  memset(&val, 0, sizeof(val));
  if( (f & VEC1_META_1BYTEINT) ){
    val.iVal = pBuf->a[iOff++];
    if( val.iVal==VEC1_META_1BYTENULL ){
      val.eType = SQLITE_NULL;
    }else{
      val.eType = SQLITE_INTEGER;
    }
  }else if( (f & VEC1_META_4BYTEINT) ){
    val.iVal = (int)vec1GetU32(&pBuf->a[iOff]);
    iOff += sizeof_u32;
    if( (u32)val.iVal==VEC1_META_4BYTENULL ){
      val.eType = SQLITE_NULL;
    }else{
      val.eType = SQLITE_INTEGER;
    }
  }else if( (f & VEC1_META_REAL) ){
    u64 v = vec1GetU64(&pBuf->a[iOff]);
    iOff += sizeof_f64;
    if( v==VEC1_META_REALNULL ){
      val.eType = SQLITE_NULL;
    }else{
      val.eType = SQLITE_FLOAT;
      memcpy(&val.fVal, &v, sizeof_f64);
    }
  }else{
    vec1MetaValueRead(pBuf, &iOff, &val);
  }

  if( pVal ){
    int eType = sqlite3_value_type(pVal);
    int bMatch = 0;
    switch( eType ){

      case SQLITE_NULL:
        bMatch = (val.eType==SQLITE_NULL);
        break;

      case SQLITE_INTEGER: {
        i64 iVal = sqlite3_value_int64(pVal);
        bMatch = (
            (val.eType==SQLITE_INTEGER && val.iVal==iVal)
         || (val.eType==SQLITE_FLOAT && val.fVal==(double)iVal)
        );
        break;
      }

      case SQLITE_FLOAT: {
        double fVal = sqlite3_value_double(pVal);
        bMatch = (val.eType==SQLITE_FLOAT && val.fVal==fVal);
        break;
      }

      case SQLITE_TEXT: {
        int nVal = sqlite3_value_bytes(pVal);
        if( val.eType==SQLITE_TEXT && val.iVal==nVal ){
          const char *zVal = (const char*)sqlite3_value_text(pVal);
          bMatch = (0==memcmp(val.pPtr, zVal, nVal));
        }
        break;
      }

      default: {
        int nVal = sqlite3_value_bytes(pVal);
        if( val.eType==SQLITE_BLOB && val.iVal==nVal ){
          const u8 *zVal = (const u8*)sqlite3_value_blob(pVal);
          bMatch = (0==memcmp(val.pPtr, zVal, nVal));
        }
        assert( eType==SQLITE_BLOB );
        break;
      }
    }

    if( bMatch==0 ) rc = SQLITE_ERROR;
  }

  *piOff = iOff;
  return rc;
}

/*
** Return true if the difference between f1 and f2 is greater than 
** (fTol * max(f1, f2)). Or false if they differ by more than this.
*/
static int vec1FloatWithinTolerance(float f1, float f2, float fTol){
  if( f1==0.0 || f2==0.0 ) return 0;
  if( f2>f1 ){
    if( ((f2-f1) / f2) > fTol ) return 0;
  }else{
    if( ((f1-f2) / f1) > fTol ) return 0;
  }
  return 1;
}
  
/*
** Check that the encoded version of aVec[] matches the encoded vector
** stored at entry iEntry of aBlob[]. For BQ encodings, the bit-quantized
** vector must match exactly and the stored norm must be within a small
** tolerance of the calculated value. For PQ encodings, the encoded vectors
** are compared using their reconstruction errors, allowing for small
** differences in floating-point arithmetic.
**
** Return non-zero if the encoded vector is inconsistent with aVec[] or if
** an error occurs. Set *pRc to SQLITE_NOMEM if temporary space cannot be
** allocated.
*/
static int vec1IntegrityCheckEncoded(
  int *pRc,                       /* IN/OUT: Error code */
  Vec1Model *pMod,                /* Model used to quantize vectors */
  const float *aVec,              /* Vector to compare quantized entry to */
  int iEntry,                     /* Entry of aBlob[] to compare to */
  const u8 *aBlob,                /* %_idx.val value */
  Vec1Buffer *pBuf                /* Buffer to use for temp space */
){
  const int nEnc = vec1EncodedVectorSize(pMod);
  const int nElem = (int)pMod->hdr.nElem;

  /* Allocate the maximum amount of space required by this function - 
  ** enough for one full vector and two encoded vectors. */
  int nReq = (nElem * sizeof_f32) + nEnc*2;
  if( vec1BufferSize(pBuf, nReq) ){
    *pRc = SQLITE_NOMEM;
    return 1;
  }else{
    const int nCodebook = pMod->hdr.nCodebook;
    float *v1 = (float*)pBuf->a;  /* Space for decoded vector */
    u8 *aEnc = (u8*)&v1[nElem];   /* Encoded version of aVec[] */
    u8 *aIdx = &aEnc[nEnc];       /* Encoded vector read from aBlob[] */

    u32 flags = vec1GetU32(&aBlob[0]);
    u32 nEntry = (int)vec1GetU32(&aBlob[4]);
    int iOut = 0;
    int iOff;
    int ii;

    /* Encode the vector into aEnc */
    vec1EncodeVector(pMod, aVec, aEnc);

    /* Read the encoded version of the vector from aBlob[] into aIdx[] */
    iOff = nEntry * ((flags & VEC1_LIST_64BIT) ? 8 : 4) + VEC1_LIST_SZHDR;
    if( pMod->hdr.flags & VEC1_MODEL_BITQUANT ){
      int iNorm = iOff + VEC1_LIST_NORM_OFFSET(iEntry, nCodebook);
      memcpy(aIdx, &aBlob[iNorm], sizeof_f32);
      iOff += VEC1_LIST_CODE_OFFSET(iEntry, nCodebook);
      iOut = sizeof_f32;
    }else{
      iOff += VEC1_LIST_BLOCKED_OFFSET(iEntry, nCodebook);
    }
    for(ii=0; ii<nCodebook; ii++){
      aIdx[iOut++] = aBlob[iOff];
      iOff += VEC1_PQ_BLOCKSIZE;
    }

    if( memcmp(aEnc, aIdx, nEnc) ){
      /* The encoded version of the vector does not exactly match the version
      ** just read from the index. This might be corruption, or it might be
      ** down to small differences in the floating point arithmetic.  
      **
      ** For BQ encodings, we require the body of the encoding to be
      ** identical, but allow a small tolerance for the norm field.
      **
      ** For PQ encodings, we measure the reconstruction errors of both
      ** the aEnc[] and aIdx[] encodings. If they are within a small tolerance
      ** of each other, then both encodings are deemed to be valid.  */
      const float fTol = 0.000001f;

      if( pMod->hdr.flags & VEC1_MODEL_BITQUANT ){
        float norm1;
        float norm2;
        memcpy(&norm1, aEnc, sizeof_f32);
        memcpy(&norm2, aIdx, sizeof_f32);

        /* Check the norms are close enough. If not, return 1. */
        if( vec1FloatWithinTolerance(norm1, norm2, fTol)==0 ) return 1;

        /* Check the arrays of bits are identical. If not, return 1. */
        if( memcmp(&aEnc[sizeof_f32], &aIdx[sizeof_f32], nEnc-sizeof_f32) ){
          return 1;
        }
      }else{
        float e1 = 0.0;           /* aEnc[] squared reconstruction error */
        float e2 = 0.0;           /* aIdx[] squared reconstruction error */
        vec1DecodeVector(pMod, aEnc, v1);
        e1 = (float)vec1L2Dist(v1, aVec, nElem);
        vec1DecodeVector(pMod, aIdx, v1);
        e2 = (float)vec1L2Dist(v1, aVec, nElem);
        if( vec1FloatWithinTolerance(e1, e2, fTol)==0 ) return 1;
      }
    }
  }

  return 0;
}

/*
** Integrity check method. If the table has been supplied with a model,
** loop through the %_idx table. For each 
*/
static int vec1IntegrityMethod(
  sqlite3_vtab *pVTab, 
  const char *zSchema,
  const char *zTabName, 
  int mFlags, 
  char **pzErr
){
  Vec1Tab *pTab = (Vec1Tab*)pVTab;
  int rc = SQLITE_OK;
  sqlite3_stmt *pScan = 0;
  sqlite3_stmt *pLookup = 0;
  float *aResidual = 0;
  char *zErr = 0;
  Vec1Buffer *aBufMeta = 0;
  Vec1Buffer tmp = {0, 0, 0};
  u8 *aPQ = 0;

  UNUSED_PARAMETER2(mFlags, zSchema);

  rc = vec1LoadConfig(pTab);
  if( rc==SQLITE_OK && (pTab->mod.hdr.flags & VEC1_MODEL_INDEX) ){
    int bFlat = vec1ModelIsFlat(&pTab->mod);
    Vec1Model *pMod = &pTab->mod;
    const int nCodebook = pMod->hdr.nCodebook;
    const int nElem = pTab->cfg.nElem;

    int *aiOffMeta = 0;
    i64 nTotalEntry = 0;
    u8 *aIdxPQ = 0;

    if( nCodebook>0 && pMod->hdr.nBucket>0 ){
      aResidual = vec1MallocZero(pTab->nTmpVec * sizeof_f32);
      if( aResidual==0 ){
        rc = SQLITE_NOMEM;
        goto integrity_failed;
      }
    }

    if( nCodebook>0 ){
      aPQ = (u8*)vec1MallocZero((nCodebook+sizeof_f32) * 2);
      if( aPQ==0 ){
        rc = SQLITE_NOMEM;
        goto integrity_failed;
      }
      aIdxPQ = &aPQ[nCodebook + sizeof_f32];
    }

    rc = vec1GetSql(pTab, VEC1_SQL_SCAN_IDX, &pScan);
    if( rc==SQLITE_OK ){
      rc = vec1GetSql(pTab, VEC1_SQL_LOOKUP_BASE, &pLookup);
    }

    /* If there are one or more meta-value columns, allocate space for
    ** buffers to load each of them from the database */
    if( rc==SQLITE_OK && pTab->nMeta>0 ){
      int nByte = pTab->nMeta * (sizeof(Vec1Buffer) + sizeof(int));
      aBufMeta = (Vec1Buffer*)vec1MallocZero(nByte);
      if( aBufMeta==0 ){
        rc = SQLITE_NOMEM;
      }else{
        aiOffMeta = (int*)&aBufMeta[pTab->nMeta];
      }
    }

    while( rc==SQLITE_OK && SQLITE_ROW==sqlite3_step(pScan) ){
      const u8 *aBlob = sqlite3_column_blob(pScan, 0);
      int nBlob = sqlite3_column_bytes(pScan, 0);
      int iBucket = sqlite3_column_int(pScan, 1);
      i64 iId = sqlite3_column_int64(pScan, 2);
      int szRowid = 4;

      int nEntry = 0;
      u32 flags = 0;
      int nTombstone = 0;
      int ii = 0;
      int iMeta = 0;
      int nNonTombstone = 0;

      if( vec1CheckIdxSize(pTab, aBlob, nBlob) ){
        const char *zFmt = "%s: %%_idx entry id=%lld is corrupt";
        zErr = sqlite3_mprintf(zFmt, zTabName, sqlite3_column_int64(pScan, 2));
        goto integrity_failed;
      }

      flags = vec1GetU32(&aBlob[0]);
      nEntry = (int)vec1GetU32(&aBlob[4]);
      nTombstone = (int)vec1GetU32(&aBlob[8]);
      if( flags & VEC1_LIST_64BIT ){
        szRowid = 8;
      }

      /* Load the array for each meta-value column from disk */
      for(iMeta=0; iMeta<pTab->nMeta; iMeta++){
        rc = vec1ReadMeta(pTab, &aBufMeta[iMeta], iId, iMeta);
        if( rc!=SQLITE_OK ){
          if( rc==SQLITE_CORRUPT_VTAB ){
            const char *zFmt = "%s: error reading meta-list id=%lld,meta=%d";
            zErr = sqlite3_mprintf(zFmt, zTabName, iId, iMeta);
            rc = SQLITE_OK;
          }
          goto integrity_failed;
        }
        aiOffMeta[iMeta] = VEC1_META_SZHDR;
      }

      for(ii=0; ii<nEntry; ii++){
        int iRowidOff = VEC1_LIST_SZHDR + szRowid*ii;
        int bExists = 0;
        int bTombstone = 0;

        i64 iRowid;
        if( szRowid==4 ){
          iRowid = vec1GetU32(&aBlob[iRowidOff]);
          if( iRowid==VEC1_TOMBSTONE_32 ) bTombstone = 1;
        }else{
          iRowid = vec1GetU64(&aBlob[iRowidOff]);
          if( iRowid==VEC1_TOMBSTONE_64 ) bTombstone = 1;
        }

        if( bTombstone ){
          /* Advance the meta-data data iterators 1 value */
          for(iMeta=0; iMeta<pTab->nMeta; iMeta++){
            vec1IntegrityMetaValue(&aBufMeta[iMeta], &aiOffMeta[iMeta], 0);
          }
          continue;
        }

        nNonTombstone++;

        sqlite3_bind_int64(pLookup, 1, iRowid);
        if( sqlite3_step(pLookup)==SQLITE_ROW ){
          bExists = 1;

          /* Check that the meta-value arrays contain the correct data */
          for(iMeta=0; iMeta<pTab->nMeta; iMeta++){
            Vec1Buffer *pBuf = &aBufMeta[iMeta];
            sqlite3_value *pVal = sqlite3_column_value(pLookup, 2+iMeta);

            if( vec1IntegrityMetaValue(pBuf, &aiOffMeta[iMeta], pVal) ){
              const char *zFmt = "%s: meta-value value mismatch for row %lld";
              zErr = sqlite3_mprintf(zFmt, zTabName, iRowid);
              goto integrity_failed;
            }
          }

          if( bFlat ){
            int iBaseBucket = sqlite3_column_int(pLookup, 1);
            if( iBaseBucket!=iBucket ){
              const char *zFmt = 
                "%s: bucket value in %%_base row %lld is incorrect";
              zErr = sqlite3_mprintf(zFmt, zTabName, iRowid);
              goto integrity_failed;
            }
          }else{
            const u8 *aBaseVec = sqlite3_column_blob(pLookup, 1);
            const float *aTransform = 0;
            int nBaseVec = sqlite3_column_bytes(pLookup, 1);
            int iCalc = 0;
  
            assert( nCodebook>0 );

            aTransform = vec1TransformInputVector(
                &pTab->mod, pTab->aTmpVec, (const float*)aBaseVec
            ); 
  
            /* Check the vector is the right size. */
            if( nBaseVec!=nElem*sizeof_f32 ){
              const char *zFmt = "%s: vector in %%_base row %lld is wrong size";
              zErr = sqlite3_mprintf(zFmt, zTabName, iRowid);
              goto integrity_failed;
            }
  
            /* Check the vector is in the right bucket. */
            if( pMod->hdr.nBucket>0 ){
              iCalc = vec1PqBestMatch(pMod->aCentroid, 
                  pMod->hdr.nBucket, aTransform, nElem, 0
              );
            }
            if( iCalc!=iBucket ){
              const char *zFmt = "%s: vector from row %lld is in wrong bucket";
              zErr = sqlite3_mprintf(zFmt, zTabName, iRowid);
              goto integrity_failed;
            }
  
            /* Check that the contents of the index entry look right. */
            {
              /* Calculate a PQ code based on the transformed vector. Store
              ** this in aPQ[]. For binary quantization, aPQ[] contains the
              ** norm followed by nCodebook bytes of data. For PQ or OPQ,
              ** aPQ[] just contains the nCodebook bytes of encoded vector. */
              const float *aEnc = (const float*)aTransform;
              if( (pMod->hdr.flags & VEC1_MODEL_RESIDUAL) ){
                vec1Sub(aResidual, aEnc, &pMod->aCentroid[iCalc*nElem], nElem);
                aEnc = aResidual;
              }
              if( vec1IntegrityCheckEncoded(&rc, pMod, aEnc, ii, aBlob, &tmp) ){
                const char *zType;
                const char *zFmt = 
                  "%s: %%_idx %s does not match calculated %s for row %lld";
                zType = (pMod->hdr.flags & VEC1_MODEL_BITQUANT) ? "BQ" : "PQ";
                zErr = sqlite3_mprintf(zFmt, zTabName, zType, zType, iRowid);
                goto integrity_failed;
              }
            }
          }
        }
        vec1StmtReset(&rc, pLookup);

        if( rc==SQLITE_OK && bExists==0 ){
          const char *zFmt = "%s: %%_idx row %lld missing from %%_base";
          zErr = sqlite3_mprintf(zFmt, zTabName, iRowid);
          goto integrity_failed;
        }
      }

      if( rc==SQLITE_OK && (nNonTombstone+nTombstone)!=nEntry ){
        const char *zFmt = "%s: bad tombstone count";
        zErr = sqlite3_mprintf(zFmt, zTabName);
        goto integrity_failed;
      }

      nTotalEntry += nNonTombstone;
    }

    if( rc==SQLITE_OK ){
      i64 nGot = -1;
      sqlite3_stmt *pStmt = 0;
      const char *zCount = "SELECT count(*) FROM %Q.'%q_base'";
      rc = vec1PrepareSql(pTab, &pStmt, zCount, pTab->zDb, pTab->zName);
      if( rc==SQLITE_OK ){
        if( sqlite3_step(pStmt)==SQLITE_ROW ){
          nGot = sqlite3_column_int64(pStmt, 0);
        }
      }
      vec1StmtFinalize(&rc, pStmt);
      if( rc==SQLITE_OK && nGot!=nTotalEntry ){
        const char *zFmt = 
          "%s: wrong number of entries in %%_base - have %lld, expect %lld";
        zErr = sqlite3_mprintf(zFmt, zTabName, nGot, nTotalEntry);
        goto integrity_failed;
      }
    }
  }

 integrity_failed:
  *pzErr = zErr;
  sqlite3_free(aResidual);
  sqlite3_free(aPQ);
  vec1BufferFree(&tmp);
  if( aBufMeta ){
    int ii;
    for(ii=0; ii<pTab->nMeta; ii++){
      vec1BufferFree(&aBufMeta[ii]);
    }
    sqlite3_free(aBufMeta);
  }
  sqlite3_free(pTab->base.zErrMsg);
  pTab->base.zErrMsg = 0;
  vec1StmtReset(&rc, pScan);
  vec1StmtReset(&rc, pLookup);
  return rc;
}

/*************************************************************************
** Start of vec1_cat virtual table.
*/

#define VEC1CAT_SCHEMA \
  "CREATE TABLE vec1cat(database, name, model, qprofile)"

typedef struct vec1cat_vtab vec1cat_vtab;
struct vec1cat_vtab {
  sqlite3_vtab base;
  Vec1TabList *pList;
};

typedef struct vec1cat_cursor vec1cat_cursor;
struct vec1cat_cursor {
  sqlite3_vtab_cursor base;
  sqlite3_int64 iRowid;
};

static int vec1catConnectMethod(
  sqlite3 *db,
  void *pAux,
  int argc,
  const char *const *argv,
  sqlite3_vtab **ppVtab,
  char **pzErr
){
  vec1cat_vtab *pNew;
  int rc;

  UNUSED_PARAMETER(pzErr);
  UNUSED_PARAMETER2(argv, argc);

  rc = sqlite3_declare_vtab(db, VEC1CAT_SCHEMA);
  if( rc==SQLITE_OK ){
    pNew = vec1MallocZero(sizeof(*pNew));
    *ppVtab = (sqlite3_vtab*)pNew;
    if( pNew==0 ) return SQLITE_NOMEM;
    pNew->pList = (Vec1TabList*)pAux;
  }
  return rc;
}

static int vec1catDisconnectMethod(sqlite3_vtab *pVtab){
  vec1cat_vtab *p = (vec1cat_vtab *)pVtab;
  sqlite3_free(p);
  return SQLITE_OK;
}

static int vec1catOpenMethod(sqlite3_vtab *pVtab, sqlite3_vtab_cursor **ppCur){
  vec1cat_cursor *pCur;
  UNUSED_PARAMETER(pVtab);
  pCur = vec1MallocZero(sizeof(*pCur));
  if( pCur==0 ) return SQLITE_NOMEM;
  *ppCur = &pCur->base;
  return SQLITE_OK;
}

static int vec1catCloseMethod(sqlite3_vtab_cursor *cur){
  vec1cat_cursor *pCur = (vec1cat_cursor *)cur;
  sqlite3_free(pCur);
  return SQLITE_OK;
}

static int vec1catBestIndexMethod(
  sqlite3_vtab *pVtab,
  sqlite3_index_info *pIdxInfo
){
  UNUSED_PARAMETER(pVtab);
  pIdxInfo->estimatedCost = (double)10;
  pIdxInfo->estimatedRows = 10;
  return SQLITE_OK;
}

static int vec1catFilterMethod(
  sqlite3_vtab_cursor *cur,
  int idxNum,
  const char *idxStr,
  int argc,
  sqlite3_value **argv
){
  Vec1TabList *pList = ((vec1cat_vtab*)(cur->pVtab))->pList;
  vec1cat_cursor *pCur = (vec1cat_cursor *)cur;
  sqlite3_stmt *pSql1 = 0;
  int rc = SQLITE_OK;

  const char *zSql1 = "SELECT name FROM pragma_database_list";
  const char *zSql2 = 
   "WITH tables(name) AS ("
   "  SELECT name FROM %Q.sqlite_schema "
   "  WHERE sql LIKE 'CREATE%%VIRTUAL%%USING%%vec1%%'"
   ")"
   "SELECT * FROM tables CROSS JOIN pragma_table_info(tables.name, %Q)";

  UNUSED_PARAMETER2(argc, argv);
  UNUSED_PARAMETER2(idxStr, idxNum);

  rc = sqlite3_prepare_v2(pList->db, zSql1, -1, &pSql1, 0);
  if( rc==SQLITE_OK ){
    while( sqlite3_step(pSql1)==SQLITE_ROW ){
      const char *zDb = (const char*)sqlite3_column_text(pSql1, 0);
      sqlite3_stmt *pSql2 = 0;
      char *z = vec1MPrintf(&rc, zSql2, zDb, zDb);
      if( rc==SQLITE_OK ){
        rc = sqlite3_prepare(pList->db, z, -1, &pSql2, 0);
        sqlite3_free(z);
      }
      if( rc==SQLITE_OK ){
        while( sqlite3_step(pSql2)==SQLITE_ROW );
        vec1StmtFinalize(&rc, pSql2);
      }
    }
    vec1StmtFinalize(&rc, pSql1);
  }

  pCur->iRowid = 1;
  return rc;
}

static int vec1catNextMethod(sqlite3_vtab_cursor *cur){
  vec1cat_cursor *pCur = (vec1cat_cursor *)cur;
  pCur->iRowid++;
  return SQLITE_OK;
}

static Vec1Tab *vec1catGetTable(vec1cat_cursor *pCur){
  Vec1TabList *pList = ((vec1cat_vtab*)(pCur->base.pVtab))->pList;
  Vec1Tab *pTab = pList->pFirst;
  int ii;

  for(ii=1; pTab && ii<pCur->iRowid; ii++){
    pTab = pTab->pTabNext;
  }

  return pTab;
}

static int vec1catEofMethod(sqlite3_vtab_cursor *cur){
  return vec1catGetTable((vec1cat_cursor*)cur)==0;
}

/*
** xColumn() method for ephemeral virtual table vec1cat.
*/
static int vec1catColumnMethod(
  sqlite3_vtab_cursor *cur,
  sqlite3_context *ctx,
  int i
){
  vec1cat_cursor *pCur = (vec1cat_cursor*)cur;
  Vec1Tab *pTab = vec1catGetTable(pCur);
  int rc = SQLITE_OK;

  switch( i ){
    case 0:     /* database */
      sqlite3_result_text(ctx, pTab->zDb, -1, SQLITE_TRANSIENT);
      break;

    case 1:     /* name */
      sqlite3_result_text(ctx, pTab->zName, -1, SQLITE_TRANSIENT);
      break;

    case 2: {   /* model */
      Vec1ModelHeader *pHdr = &pTab->mod.hdr;
      const char *zDist = 0;
      char *zRet = 0;

      rc = vec1LoadConfig(pTab);
      zDist = ((pHdr->eDistance==VEC1_DISTANCE_L2) ? "l2" : "cos");
      if( pHdr->nCodebook==0 && pHdr->nBucket==0 ){
        zRet = vec1MPrintf(&rc, 
            "{\"index\": \"%s\", \"distance\": \"%s\"}",
            ((pHdr->flags & VEC1_MODEL_INDEX) ? "flat" : "none"), zDist
        );
      }else{
        const char *zQuant = "none";
        if( pHdr->nCodebook ){
          if( pHdr->flags & VEC1_MODEL_BITQUANT ){
            zQuant = "bq";
          }else if( pHdr->flags & VEC1_MODEL_ROTATE ){
            zQuant = "opq";
          }else{
            zQuant = "pq";
          }
        }

        zRet = vec1MPrintf(&rc, "{"
            "\"index\": \"trained\", "
            "\"distance\": \"%s\", "
            "\"nbucket\": %d, "
            "\"quantizer\": \"%s\", "
            "\"codesize\": %d, "
            "\"residual\": %s, "
            "\"wht\": %s}",
            zDist, pHdr->nBucket, zQuant, pHdr->nCodebook,
            (pHdr->flags & VEC1_MODEL_RESIDUAL) ? "true" : "false",
            (pHdr->flags & VEC1_MODEL_WHT) ? "true" : "false"
        );
      }

      if( rc==SQLITE_OK ){
        sqlite3_result_text(ctx, zRet, -1, vec1SqliteFree);
      }else{
        sqlite3_result_error_code(ctx, rc);
      }
      break;
    }

    default: { /* qprofile */
      const char *azName[VEC1_QINSTR_NCOUNTER] = {
        "total",                      /* VEC1_QINSTR_TOTAL */
        "init",                       /* VEC1_QINSTR_INITQUERY */
        "coarse",                     /* VEC1_QINSTR_COARSE */
        "idxread",                    /* VEC1_QINSTR_IDXREAD */
        "metaread",                   /* VEC1_QINSTR_METAREAD */
        "metascan",                   /* VEC1_QINSTR_METASCAN */
        "lut",                        /* VEC1_QINSTR_LUT */
        "pqscan",                     /* VEC1_QINSTR_PQSCAN */
        "finalsort"                   /* VEC1_QINSTR_FINALSORT */
      };

      char *zRet = 0;
      int ii;

      for(ii=0; ii<VEC1_QINSTR_NCOUNTER; ii++){
        zRet = vec1MPrintf(&rc, 
            "%z%s\n  \"%s\": {\"cnt\": %lld, \"cycle\": %lld}",
            zRet, (ii==0 ? "" : ","),
            azName[ii], pTab->aCount[ii], pTab->aCycle[ii]
        );
      }
      zRet = vec1MPrintf(&rc, "{%z\n}", zRet);

      if( rc==SQLITE_OK ){
        sqlite3_result_text(ctx, zRet, -1, vec1SqliteFree);
      }else{
        sqlite3_result_error_code(ctx, rc);
      }
    }
  }
  return SQLITE_OK;
}

static int vec1catRowidMethod(
  sqlite3_vtab_cursor *cur,
  sqlite3_int64 *pRowid
){
  vec1cat_cursor *pCur = (vec1cat_cursor *)cur;
  *pRowid = pCur->iRowid;
  return SQLITE_OK;
}

/*
** End of vec1_cat virtual table.
*************************************************************************/


/*
** Used to get/set per-connection transient parameters. Syntax:
**
**     SELECT vec1_config(PARAM)
**     SELECT vec1_config(PARAM, VALUE)
**
** There are currently two parameters:
**
**     nthread:
**       Default number of threads to use for training and rebuilding.
**
**     nprobe:
**       Default nprobe value for queries that do not specify one.
**
*/
static void vec1ConfigFunc(
  sqlite3_context *pCtx, 
  int nVal, 
  sqlite3_value **aVal
){
  Vec1TabList *pList = (Vec1TabList*)sqlite3_user_data(pCtx);
  const char *zParam = (const char*)sqlite3_value_text(aVal[0]);

  if( 0==sqlite3_stricmp(zParam, "nthread") ){
    if( nVal==2 ){
      int nNew = sqlite3_value_int(aVal[1]);
      if( nNew<1 || nNew>VEC1_MAX_NTHREAD ){
        vec1ResultErrorF(pCtx, "vec1: "
            "nthread requires an integer value between 1 and %d",
            VEC1_MAX_NTHREAD
        );
        return;
      }
#if VEC1_THREADS
      pList->nThread = nNew;
#endif
    }
    sqlite3_result_int(pCtx, pList->nThread);
  }else if( 0==sqlite3_stricmp(zParam, "nprobe") ){
    if( nVal==2 ){
      int eNumeric = sqlite3_value_numeric_type(aVal[1]);
      double fNew = 0.0;
      if( eNumeric==SQLITE_INTEGER || eNumeric==SQLITE_FLOAT ){
        fNew = sqlite3_value_double(aVal[1]);
      }
      if( fNew<=0.0 ){
        vec1ResultErrorF(pCtx, 
            "vec1: nprobe must be a numeric value greater than 0.0"
        );
        return;
      }
      pList->nProbeArg = fNew;
    }
    sqlite3_result_double(pCtx, pList->nProbeArg);
  }else{
    vec1ResultErrorF(pCtx, "vec1: no such parameter: %s", zParam);
  }
}

/*
** Destructor for the vec1 module registered using sqlite3_create_module_v2().
** Free the Vec1TabList structure. SQLite should have ensured any virtual
** tables are closed before this is called.
*/
static void vec1ListFree(void *pCtx){
  Vec1TabList *pList = (Vec1TabList*)pCtx;
  pList->nRef--;
  if( pList->nRef==0 ){
    assert( pList->pFirst==0 );
    sqlite3_free(pList);
  }
}

static int initExtension(
  sqlite3 *db, 
  char **pzErr,
  const sqlite3_api_routines *pApi
){
  static sqlite3_module vec1catModule = {
    0,                         /* iVersion     */
    0,                         /* xCreate      */
    vec1catConnectMethod,      /* xConnect     */
    vec1catBestIndexMethod,    /* xBestIndex   */
    vec1catDisconnectMethod,   /* xDisconnect  */
    0,                         /* xDestroy     */
    vec1catOpenMethod,         /* xOpen        */
    vec1catCloseMethod,        /* xClose       */
    vec1catFilterMethod,       /* xFilter      */
    vec1catNextMethod,         /* xNext        */
    vec1catEofMethod,          /* xEof         */
    vec1catColumnMethod,       /* xColumn      */
    vec1catRowidMethod,        /* xRowid       */
    0,                         /* xUpdate      */
    0,                         /* xBegin       */
    0,                         /* xSync        */
    0,                         /* xCommit      */
    0,                         /* xRollback    */
    0,                         /* xFindMethod  */
    0,                         /* xRename      */
    0,                         /* xSavepoint   */
    0,                         /* xRelease     */
    0,                         /* xRollbackTo  */
    0,                         /* xShadowName  */
    0,                         /* xIntegrity   */
  };

  static sqlite3_module vec1Module = {
    4,                    /* iVersion */
    vec1CreateMethod,     /* xCreate */
    vec1ConnectMethod,    /* xConnect */
    vec1BestIndexMethod,  /* xBestIndex */
    vec1DisconnectMethod, /* xDisconnect */
    vec1DestroyMethod,    /* xDestroy */
    vec1Open,             /* xOpen */
    vec1Close,            /* xClose */
    vec1FilterMethod,     /* xFilter */
    vec1NextMethod,       /* xNext */
    vec1EofMethod,        /* xEof */
    vec1ColumnMethod,     /* xColumn */
    vec1Rowid,            /* xRowid */
    vec1UpdateMethod,     /* xUpdate */
    vec1BeginMethod,      /* xBegin */
    vec1SyncMethod,       /* xSync */
    0,                    /* xCommit */
    vec1RollbackMethod,   /* xRollback */
    0,                    /* xFindFunction */
    vec1RenameMethod,     /* xRename */
    vec1SavepointMethod,  /* xSavepoint */
    vec1ReleaseMethod,    /* xRelease */
    vec1RollbackToMethod, /* xRollbackTo */
    0,                    /* xShadowName */
    vec1IntegrityMethod   /* xIntegrity */
  };
  
  static struct Func {
    const char *zName;
    int nArg;
    void (*x)(sqlite3_context*,int,sqlite3_value**);
  } aFunc[] = {
    { "vec1_info",        0,      vec1InfoFunc     },
    { "vec1_to_json",     1,      vec1ToJsonFFunc  },
    { "vec1_from_json",   1,      vec1FromJsonFunc },

    { "vec1_l2_distance", 2,      vec1DistanceFuncL2 },
    { "vec1_cos_distance", 2,     vec1DistanceFuncCos },

    { "vec1_config",  1, vec1ConfigFunc  },
    { "vec1_config",  2, vec1ConfigFunc  },

    /* Currently undocumented functions. These may become documented when
    ** datatypes other than 32-bit float are supported for vectors */
    { "vec1_to_json_f",   1,      vec1ToJsonFFunc  },
    { "vec1_to_json_i",   1,      vec1ToJsonIFunc  },
  };

  static struct AggFunc {
    const char *zName;
    int nArg;
    void (*xStep)(sqlite3_context*,int,sqlite3_value**);
    void (*xFinal)(sqlite3_context*);
  } aAgg[] = {
    { "vec1_train", 1, vec1TrainStep, vec1TrainFinal },
    { "vec1_train", 2, vec1TrainStep, vec1TrainFinal },
  };

  int ii = 0;
  int rc = SQLITE_OK;
  Vec1TabList *pList = vec1MallocZero(sizeof(Vec1TabList));

  UNUSED_PARAMETER2(pApi, pzErr);

  /* Assuming the Vec1TabList object was allocated without error, populate
  ** it and create the main virtual table module - vec1. */
  if( pList==0 ){
    rc = SQLITE_NOMEM;
  }else{
    pList->nThread = 1;
    pList->db = db;
    pList->nProbeArg = VEC1_QUERY_DEFAULT_NPROBE;

    pList->nRef = 1;
    rc = sqlite3_create_module_v2(db, "vec1", &vec1Module, pList, vec1ListFree);
  }

  /* Create the other vtab module - vec1cat */
  if( rc==SQLITE_OK ){
    pList->nRef++;
    rc = sqlite3_create_module_v2(
        db, "vec1cat", &vec1catModule, pList, vec1ListFree
    );
  }

  /* Create the scalar functions */
  for(ii=0; rc==SQLITE_OK && ii<size_of_array(aFunc); ii++){
    rc = sqlite3_create_function(
        db, aFunc[ii].zName, aFunc[ii].nArg, SQLITE_UTF8|SQLITE_INNOCUOUS, 
        (void*)pList, aFunc[ii].x, 0, 0
    );
  }

  /* Create the aggregate functions */
  for(ii=0; rc==SQLITE_OK && ii<size_of_array(aAgg); ii++){
    struct AggFunc *p = &aAgg[ii];
    rc = sqlite3_create_function(
        db, p->zName, p->nArg, SQLITE_UTF8|SQLITE_INNOCUOUS, (void*)pList, 0, 
        p->xStep, p->xFinal
    );
  }

  /* If an error occurred, unregister everything */
  if( rc!=SQLITE_OK ){
    sqlite3_create_module(db, "vec1", 0, 0);
    sqlite3_create_module(db, "vec1cat", 0, 0);

    for(ii=0; ii<size_of_array(aFunc); ii++){
      const char *z = aFunc[ii].zName;
      sqlite3_create_function(db, z, aFunc[ii].nArg, SQLITE_UTF8, 0, 0, 0, 0);
    }
    for(ii=0; ii<size_of_array(aAgg); ii++){
      const char *z = aAgg[ii].zName;
      sqlite3_create_function(db, z, aAgg[ii].nArg, SQLITE_UTF8, 0, 0, 0, 0);
    }
  }

  return rc;
}

/**************************************************************************
***************************************************************************
** Below this point gets ugly. Almost all of this is only built for x86
** binaries that support multiple SIMD architectures. 
*/

#ifdef VEC1SIMD

/* Arbitrarily selected values for symbols "SCALAR" and "AVX2". */
#define SCALAR 281243987
#define AVX2   949193913

/* Check that VEC1SIMD was set to a recognized value */
# if (VEC1SIMD!=SCALAR && VEC1SIMD!=AVX2)
#  error "VEC1SIMD must be defined as either SCALAR or AVX2"
# endif

int sqlite3Vec1InitAVX2(sqlite3*, char**, const sqlite3_api_routines*);

# if VEC1SIMD==AVX2
int sqlite3Vec1InitAVX2(
  sqlite3 *db, 
  char **pzErr,
  const sqlite3_api_routines *pApi
){
  return initExtension(db, pzErr, pApi);
}
# endif  /* VEC1SIMD==AVX2 */

# if VEC1SIMD==SCALAR

# if defined(_MSC_VER)
#  include <intrin.h>
static int vec1HasAvx2(void)
{
  int regs[4];
  __cpuid(regs, 1);
  int ecx = regs[2];
  if (!(ecx & (1 << 28))) return 0;  // AVX
  if (!(ecx & (1 << 27))) return 0;  // OSXSAVE
  unsigned long long xcr0 = _xgetbv(0);
  if ((xcr0 & 0x6) != 0x6) return 0;
  __cpuidex(regs, 7, 0);
  int ebx = regs[1];
  return (ebx & (1 << 5)) != 0;      // AVX2
}
# else
static int vec1HasAvx2(){
  /* Clang/GCC */
  __builtin_cpu_init();
  return __builtin_cpu_supports("avx2");
}
# endif

static int initMultiExtension(
  sqlite3 *db, 
  char **pzErr,
  const sqlite3_api_routines *pApi
){
  int rc = SQLITE_OK;
  if( vec1HasAvx2() ){
    /* Use AVX2 */
    rc = sqlite3Vec1InitAVX2(db, pzErr, pApi); 
  }else{
    /* Fall back to scalar. */
    rc = initExtension(db, pzErr, pApi); 
  }

  return rc;
}
#  define initExtension initMultiExtension
# endif  /* VEC1SIMD==SCALAR */
#endif  /* defined(VEC1SIMD) */

#if !defined(VEC1SIMD) || VEC1SIMD==SCALAR

#ifndef VEC1_STATIC
SQLITE_EXTENSION_INIT1;
#endif

#ifdef _WIN32
__declspec(dllexport)
#endif
int sqlite3_extension_init(
  sqlite3 *db, 
  char **pzErrMsg, 
  const sqlite3_api_routines *pApi
){
#ifdef SQLITE_EXTENSION_INIT2
  SQLITE_EXTENSION_INIT2(pApi);
#endif
  return initExtension(db, pzErrMsg, pApi);
}

#ifdef VEC1_STATIC
int sqlite3_vec1_extra_init(const char *z){
  sqlite3_auto_extension((void(*)(void))initExtension);
  return SQLITE_OK;
}
#endif

#endif /* !defined(VEC1SIMD) || VEC1SIMD==SCALAR */

