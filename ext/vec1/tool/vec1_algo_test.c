
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

#include "vec1.c"


#ifdef VEC1_HAVE_NEON
/*
** Run nIter iterations of a loop containing N independent, back-to-back
** FMA chains (N float32x4_t accumulators, each updated once per
** iteration) and return the elapsed time on vec1HardwareTimer().
**
** N is a literal compile-time constant at every call site (see
** MAKE_FMA_TEST below), so the "for(c...)" loop below is unrolled by
** the compiler into straight-line code - there is no loop-control
** instruction diluting the measurement, and for N==1 this is a single
** dependent chain (true FMA latency); for larger N it is N independent
** chains (FMA throughput, once N is large enough to hide the latency).
*/
#define MAKE_FMA_TEST(N)                                                 \
static u64 vec1FmaTest##N(int nIter){                                    \
  float32x4_t acc[N];                                                    \
  float32x4_t a = vdupq_n_f32(1.000001f);                                \
  float32x4_t b = vdupq_n_f32(0.999999f);                                \
  int c, r;                                                              \
  u64 t;                                                                 \
  for(c=0; c<N; c++) acc[c] = vdupq_n_f32(1.0f + 0.001f*(float)c);       \
  t = vec1HardwareTimer();                                               \
  for(r=0; r<nIter; r++){                                                \
    for(c=0; c<N; c++) acc[c] = vfmaq_f32(acc[c], a, b);                 \
  }                                                                      \
  t = vec1HardwareTimer() - t;                                           \
  /* Force the result to be used, so the compiler can't prove the   */   \
  /* whole loop is dead and delete it.                              */   \
  {                                                                      \
    float s = 0.0f;                                                     \
    for(c=0; c<N; c++) s += vaddvq_f32(acc[c]);                          \
    if( s!=s ) fprintf(stderr, "unreachable: %f\n", (double)s);          \
  }                                                                      \
  return t;                                                              \
}
MAKE_FMA_TEST(1)
MAKE_FMA_TEST(2)
MAKE_FMA_TEST(3)
MAKE_FMA_TEST(4)
MAKE_FMA_TEST(6)
MAKE_FMA_TEST(8)
MAKE_FMA_TEST(10)
MAKE_FMA_TEST(12)
MAKE_FMA_TEST(16)
MAKE_FMA_TEST(20)
MAKE_FMA_TEST(24)
MAKE_FMA_TEST(28)
MAKE_FMA_TEST(32)
#undef MAKE_FMA_TEST

/*
** Measure NEON FMA latency and throughput on this host and use
** Little's Law (accumulators_needed = latency * throughput) to derive
** the minimum number of independent accumulator chains required to
** keep the FMA pipeline(s) saturated.
*/
static void vec1FmaBench(void){
  const int nIter = 200*1000*1000;
  struct { int n; u64 t; } a[13];
  int i;
  double latency;              /* ticks per FMA, N==1 (dependent chain) */
  double bestRate = 0.0;       /* FMAs per tick, best N tested           */
  int nOptimal;

  a[0].n=1;  a[0].t = vec1FmaTest1(nIter);
  a[1].n=2;  a[1].t = vec1FmaTest2(nIter);
  a[2].n=3;  a[2].t = vec1FmaTest3(nIter);
  a[3].n=4;  a[3].t = vec1FmaTest4(nIter);
  a[4].n=6;  a[4].t = vec1FmaTest6(nIter);
  a[5].n=8;  a[5].t = vec1FmaTest8(nIter);
  a[6].n=10; a[6].t = vec1FmaTest10(nIter);
  a[7].n=12; a[7].t = vec1FmaTest12(nIter);
  a[8].n=16; a[8].t = vec1FmaTest16(nIter);
  a[9].n=20; a[9].t = vec1FmaTest20(nIter);
  a[10].n=24; a[10].t = vec1FmaTest24(nIter);
  a[11].n=28; a[11].t = vec1FmaTest28(nIter);
  a[12].n=32; a[12].t = vec1FmaTest32(nIter);

  printf("%-12s %-16s %-16s %-10s\n", "nChains", "ticks/iter", "ticks/FMA", "FMA/tick");
  for(i=0; i<(int)(sizeof(a)/sizeof(a[0])); i++){
    double ticksPerIter = (double)a[i].t / nIter;
    double ticksPerFma = ticksPerIter / a[i].n;
    double fmaPerTick = 1.0 / ticksPerFma;
    printf("%-12d %-16.3f %-16.4f %-10.3f\n",
        a[i].n, ticksPerIter, ticksPerFma, fmaPerTick);
    if( i==0 ) latency = ticksPerFma;
    if( fmaPerTick>bestRate ) bestRate = fmaPerTick;
  }

  nOptimal = (int)ceil(latency * bestRate);

  printf("\n");
  printf("FMA latency (N=1, dependent chain):   %.3f ticks\n", latency);
  printf("FMA throughput (best of N tested):    %.3f FMA/tick\n", bestRate);
  printf("Little's Law: accumulators needed = latency * throughput\n");
  printf("            = %.3f * %.3f = %.2f  ->  use %d independent accumulators\n",
      latency, bestRate, latency*bestRate, nOptimal);
}
#endif /* VEC1_HAVE_NEON */

/*
** Populate aTrans[] with transposed version of the array of centroids in 
** aCentroid[].
*/
static void vec1TransposeCentroids(
  const float *aCentroid,         /* Packed array of centroids */
  int nElem,                      /* Number of elements in each vector */
  int nVector,                    /* Number of centroid vectors */
  int nWidth,                     /* Transpose width */
  float *aTrans                   /* Populate this array with transformation */
){
  int c, d;
  float *pOut = aTrans;

  /* nVector must be an integer multiple of nWidth */
  assert( (nVector % nWidth)==0 );

  for(c=0; c<nVector; c+=nWidth){
    for(d=0; d<nElem; d++){
      int ii;
      for(ii=0; ii<nWidth; ii++){
        (*pOut++) = aCentroid[ (c+ii)*nElem + d ];
      }
    }
  }

  assert( pOut==&aTrans[nElem * nVector] );
}

static double vec1RandomFloat(Vec1Random *p) {
  return (vec1RandomNext(p) >> 8) * (1.0 / 16777216.0);  // 24-bit mantissa
}

static void vec1RandomArray(Vec1Random *p, float *aRnd, int nRnd){
  int ii;
  for(ii=0; ii<nRnd; ii++){
    aRnd[ii] = vec1RandomFloat(p);
  }
}

static void usage(const char *zExec){
  fprintf(stderr, 
      "usage: %s NELEM NCENTROID NTRAIN NREPEAT ALGORITHM\n", zExec
  );
  exit(-1);
}

static void xLloydsNaive(
  int nElem,
  const float *aCentroid,
  int nCentroid,
  const float *aTrain,
  int nTrain,
  int *aBest
){
  int ii;

  for(ii=0; ii<nTrain; ii++){
    const float *pVec = &aTrain[ii*nElem];
    double fBest = INFINITY;
    int iBest = -1;
    int jj;

    for(jj=0; jj<nCentroid; jj++){
      double fNew = vec1L2Dist(pVec, &aCentroid[jj*nElem], nElem);
      if( fNew<fBest ){
        iBest = jj;
        fBest = fNew;
      }
    }
    aBest[ii] = iBest;
  }
}

static void xLloydsStride(
  int nElem,
  const float *aCentroid,
  int nCentroid,
  const float *aTrain,
  int nTrain,
  int *aBest
){
#define VEC1_LLOYDS_STRIDE 8
  const int nStride = VEC1_LLOYDS_STRIDE;
  int ii;

  for(ii=0; ii<nTrain; ii+=nStride){
    float aDist[VEC1_LLOYDS_STRIDE];
    int aRes[VEC1_LLOYDS_STRIDE];
    int cc;
    int jj;

    for(cc=0; cc<nStride; cc++) aDist[cc] = INFINITY;

    for(jj=0; jj<nCentroid; jj++){
      const float *pCent = &aCentroid[jj * nElem];
      for(cc=0; cc<nStride; cc++){
        float fDist = vec1L2Dist(pCent, &aTrain[(cc+ii)*nElem], nElem);
        if( fDist<aDist[cc] ){
          aDist[cc] = fDist;
          aRes[cc] = jj;
        }
      }
    }

    memcpy(&aBest[ii], aRes, sizeof(aRes));
  }
}

#if defined(VEC1_HAVE_NEON)
# define TEST_BMT_NVEC 8

/*
** NEON version of vec1BestMatchTransposeN(). Array aCentNT[] contains the
** centroids transposed in blocks of TEST_BMT_NVEC centroids (the same
** width as the number of vectors - see xLloydsTransposed2()). Each block
** of TEST_BMT_NVEC centroids occupies TEST_BMT_NREG float32x4_t registers
** per training vector.
*/
# define TEST_BMT_NREG (TEST_BMT_NVEC/4)

static void vec1BestMatchTransposeNNEON(
  int nElem,
  const float *aCent,
  const float *aCentNT,
  int nCent,
  const float *aVec[TEST_BMT_NVEC],
  int aBestOut[TEST_BMT_NVEC],
  double *pfTotalOut
){
  const int nWidth = TEST_BMT_NVEC;
  float aDist[TEST_BMT_NVEC];
  int aBest[TEST_BMT_NVEC];
  int vv;
  int iCent;
  const float *pTrans = aCentNT;

  /* All the code below assumes TEST_BMT_NVEC is a multiple of 4 */
  assert( (TEST_BMT_NVEC % 4)==0 );

  for(vv=0; vv<TEST_BMT_NVEC; vv++){
    aDist[vv] = INFINITY;
    aBest[vv] = 0;
  }

  for(iCent=0; iCent<=(nCent-nWidth); iCent+=nWidth){
    float32x4_t aAcc[TEST_BMT_NVEC][TEST_BMT_NREG];
    int d;
    int rr;                       /* For looping 0..TEST_BMT_NREG-1 */

    for(vv=0; vv<TEST_BMT_NVEC; vv++){
      for(rr=0; rr<TEST_BMT_NREG; rr++){
        aAcc[vv][rr] = vdupq_n_f32(0.0f);
      }
    }

    for(d=0; d<nElem; d++){
      float32x4_t cval[TEST_BMT_NREG];
      for(rr=0; rr<TEST_BMT_NREG; rr++){
        cval[rr] = vld1q_f32(&pTrans[rr*4]);
      }
      for(vv=0; vv<TEST_BMT_NVEC; vv++){
        float32x4_t sval = vld1q_dup_f32(&aVec[vv][d]);
        for(rr=0; rr<TEST_BMT_NREG; rr++){
          float32x4_t diff = vsubq_f32(cval[rr], sval);
          aAcc[vv][rr] = vfmaq_f32(aAcc[vv][rr], diff, diff);
        }
      }
      pTrans += nWidth;
    }

    /* At this point, the TEST_BMT_NREG registers in aAcc[vv] contain the
    ** distances from training vector vv to centroids iCent to
    ** (iCent + TEST_BMT_NVEC - 1). Populate the aDist[] and aBest[] scalar
    ** arrays accordingly. */
    for(vv=0; vv<TEST_BMT_NVEC; vv++){
      float32x4_t vMin = aAcc[vv][0];
      for(rr=1; rr<TEST_BMT_NREG; rr++){
        vMin = vminq_f32(vMin, aAcc[vv][rr]);
      }
      if( vminvq_f32(vMin)<aDist[vv] ){
        /* This branch runs if aAcc[vv] contains one of more values less
        ** than scalar value aDist[vv]. */
        int is;
        float aScalar[TEST_BMT_NVEC];
        for(rr=0; rr<TEST_BMT_NREG; rr++){
          vst1q_f32(&aScalar[rr*4], aAcc[vv][rr]);
        }
        for(is=0; is<TEST_BMT_NVEC; is++){
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
    for(vv=0; vv<TEST_BMT_NVEC; vv++){
      float dist = vec1L2Dist(aVec[vv], cent, nElem);
      if( dist<aDist[vv] ){
        aDist[vv] = dist;
        aBest[vv] = iCent;
      }
    }
  }

  memcpy(aBestOut, aBest, sizeof(aBest));
  if( pfTotalOut ){
    double total = 0.0;
    for(vv=0; vv<TEST_BMT_NVEC; vv++){
      total += aDist[vv];
    }
    *pfTotalOut += total;
  }
}
# define vec1BestMatchTransposeN vec1BestMatchTransposeNNEON

#elif defined(VEC1_HAVE_AVX2)
# define TEST_BMT_NVEC VEC1_MULTIMATCH_NVEC
#endif

static void xLloydsTransposed2(
  int nElem,
  const float *aCentroid,
  int nCentroid,
  const float *aTrain,
  int nTrain,
  int *aBest
){
  float *aTrans = 0;
  int ii;

  aTrans = (float*)malloc(nElem * nCentroid * sizeof(float));
  vec1TransposeCentroids(
      aCentroid, nElem, nCentroid, TEST_BMT_NVEC, aTrans
  );

  for(ii=0; ii<nTrain; ii+=TEST_BMT_NVEC){
    const float *aVec[TEST_BMT_NVEC];
    int vv;
    for(vv=0; vv<TEST_BMT_NVEC; vv++){
      aVec[vv] = &aTrain[(ii+vv)*nElem];
    }

    vec1BestMatchTransposeN(
        nElem, aCentroid, aTrans, nCentroid, aVec, &aBest[ii], 0
    );

  }

  free(aTrans);
}

static void xLloydsModelTDist(
  int nElem,
  const float *aCentroid,
  int nCentroid,
  const float *aTrain,
  int nTrain,
  int *aBest
){
  float *aTrans = 0;
  int ii;

  aTrans = (float*)malloc(nElem * nCentroid * sizeof(float));
  vec1TransposeCentroids(
      aCentroid, nElem, nCentroid, VEC1_TRANSPOSE_WIDTH, aTrans
  );

  for(ii=0; ii<nTrain; ii++){
    const float *pTrans = aTrans;
    float fBestDist = INFINITY;
    const float *vec = &aTrain[ii*nElem];
    int K;
    for(K=0; K<=(nCentroid-VEC1_TRANSPOSE_WIDTH); K+=VEC1_TRANSPOSE_WIDTH){
      int cc;
      float aDist[VEC1_TRANSPOSE_WIDTH];

      vec1ModelTDist(pTrans, vec, nElem, aDist);
      pTrans += (nElem * VEC1_TRANSPOSE_WIDTH);

      for(cc=0; cc<VEC1_TRANSPOSE_WIDTH; cc++){
        if( aDist[cc]<fBestDist ){
          aBest[ii] = K+cc;
          fBestDist = aDist[cc];
        }
      }
    }

    for(; K<nCentroid; K++){
      float dist = vec1L2Dist(&aCentroid[K*nElem], vec, nElem);
      if( dist<fBestDist ){
        fBestDist = dist;
        aBest[ii] = K;
      }
    }
  }

  free(aTrans);
}


#if 0
static void xLloydsTransposed(
  int nElem,
  const float *aCentroid,
  int nCentroid,
  const float *aTrain,
  int nTrain,
  int *aBest
){
#if !defined(VEC1_HAVE_AVX2) && !defined(VEC1_HAVE_NEON)
  fprintf(stderr, "must define either VEC1_HAVE_AVX2 or VEC1_HAVE_NEON\n");
  exit(1);
#else
  float *aTrans = 0;
  int ii;

/*
#if defined(VEC1_HAVE_AVX2)
# define VEC1_LLOYDS_NACC 8
# define VEC1_LLOYDS_WIDTH 8
#else
# define VEC1_LLOYDS_NACC 8
# define VEC1_LLOYDS_WIDTH 4
#endif
*/

  aTrans = (float*)malloc(nElem * nCentroid * sizeof(float));
  vec1TransposeCentroids(aCentroid, nElem, nCentroid, VEC1_NTRANSPOSE_WIDTH, aTrans);

  for(ii=0; ii<nTrain; ii+=VEC1_LLOYDS_NACC){
    const float *pTrans = aTrans;
    int cc;
    int iCent;

    int aRes[VEC1_LLOYDS_NACC];
    float aDist[VEC1_LLOYDS_NACC];

    /* Initialize the aDist[] array. */
    for(cc=0; cc<VEC1_LLOYDS_NACC; cc++){
      aDist[cc] = INFINITY;
    }

    for(iCent=0; iCent<nCentroid; iCent+=VEC1_LLOYDS_WIDTH){
      int d;

#if defined(VEC1_HAVE_AVX2)
      /* All the AVX2 code below assumes VEC1_LLOYDS_WIDTH==8, so 
      ** assert() that here.  */
      assert( VEC1_LLOYDS_WIDTH==8 );

      __m256 aAcc[VEC1_LLOYDS_NACC];
      for(cc=0; cc<VEC1_LLOYDS_NACC; cc++){
        aAcc[cc] = _mm256_setzero_ps();
      }
      for(d=0; d<nElem; d++){
        __m256 cval = LOADU( pTrans );
        for(cc=0; cc<VEC1_LLOYDS_NACC; cc++){
          __m256 sval = _mm256_set1_ps( aTrain[(ii+cc)*nElem+d] );
          __m256 diff = _mm256_sub_ps( cval, sval );
          aAcc[cc] = FMADD(diff, diff, aAcc[cc]);
        }
        pTrans += VEC1_LLOYDS_WIDTH;
      }

      /* At this point, each register in aAcc[] contains the distances
      ** from the corresponding training vector (vector ii + cc) to 
      ** centroids iCent to (iCent + VEC1_SIMD_WIDTH). Populate the aDist[]
      ** and aRes[] scalar arrays accordingly. */
      for(cc=0; cc<VEC1_LLOYDS_NACC; cc++){
        __m256 vMin = _mm256_set1_ps(aDist[cc]);
        __m256 mask = _mm256_cmp_ps(aAcc[cc], vMin, _CMP_LT_OQ);
        if( _mm256_movemask_ps(mask)!=0 ){
          /* This branch runs if aAcc[cc] contains one of more values less 
          ** than scalar value aDist[cc]. */
          int is;
          float aScalar[VEC1_LLOYDS_WIDTH];
          _mm256_storeu_ps(aScalar, aAcc[cc]);
          for(is=0; is<VEC1_LLOYDS_WIDTH; is++){
            if( aScalar[is]<aDist[cc] ){
              aDist[cc] = aScalar[is];
              aRes[cc] = is + iCent;
            }
          }
        }
      }
#else
      /* All the NEON code below assumes VEC1_LLOYDS_WIDTH==4, so
      ** assert() that here.  */
      assert( VEC1_LLOYDS_WIDTH==4 );

      float32x4_t aAcc[VEC1_LLOYDS_NACC];
      for(cc=0; cc<VEC1_LLOYDS_NACC; cc++){
        aAcc[cc] = vdupq_n_f32(0.0f);
      }
      for(d=0; d<nElem; d++){
        float32x4_t cval = vld1q_f32( pTrans );
        for(cc=0; cc<VEC1_LLOYDS_NACC; cc++){
          float32x4_t sval = vdupq_n_f32( aTrain[(ii+cc)*nElem+d] );
          float32x4_t diff = vsubq_f32( cval, sval );
          aAcc[cc] = vfmaq_f32( aAcc[cc], diff, diff );
        }
        pTrans += VEC1_LLOYDS_WIDTH;
      }

      /* Same "did anything improve" check as the AVX2 movemask test,
      ** using a horizontal max over the comparison mask instead. Any
      ** lane less-than gives an all-1s (0xFFFFFFFF) mask in that lane,
      ** so a nonzero horizontal max means at least one lane improved.
      ** vmaxvq_u32 is aarch64-only (no 32-bit ARMv7 equivalent without
      ** a pairwise-max fallback). */
      for(cc=0; cc<VEC1_LLOYDS_NACC; cc++){
        float32x4_t vMin = vdupq_n_f32(aDist[cc]);
        uint32x4_t mask = vcltq_f32(aAcc[cc], vMin);
        if( vmaxvq_u32(mask)!=0 ){
          int is;
          float aScalar[VEC1_LLOYDS_WIDTH];
          vst1q_f32(aScalar, aAcc[cc]);
          for(is=0; is<VEC1_LLOYDS_WIDTH; is++){
            if( aScalar[is]<aDist[cc] ){
              aDist[cc] = aScalar[is];
              aRes[cc] = is + iCent;
            }
          }
        }
      }
#endif
    }

    /* Scalar array aRes[] now contains the index of the best centroid
    ** for each of the VEC1_LLOYDS_NACC training vectors being considered
    ** by this loop.  */
    memcpy(&aBest[ii], aRes, sizeof(aRes));
  }
#endif
}
#endif

#ifdef VEC1_USE_TRANSPOSITION
/* If VEC1_USE_TRANSPOSITION is defined in vec1.c, then there is no 
** implementation of vec1BestMatchN() to import. So in this case we
** create one here. VEC1_USE_TRANSPOSITION is only ever defined on
** AVX2 builds, so the implementation may assume AVX2.  */
#if !defined(VEC1_HAVE_AVX2) 
# error "A non-AVX2 host with VEC1_USE_TRANSPOSITION defined..."
#endif

static void vec1BestMatchN(
  int nElem,                      /* Number of elements in vectors */
  const float *aCent,             /* nCent packed centroids */
  int nCent,                      /* Number of centroids */
  const float *aVec[VEC1_MULTIMATCH_NVEC],
  int aBest[VEC1_MULTIMATCH_NVEC],
  double *pfTotalDist             /* IN/OUT: Increment by total distortion */
){
  int iCent;
  int vv;
  float aBestDist[VEC1_MULTIMATCH_NVEC];

  for(vv=0; vv<VEC1_MULTIMATCH_NVEC; vv++){
    aBestDist[vv] = INFINITY;
  }

  for(iCent=0; iCent<nCent; iCent++){
    __m256 aAcc[VEC1_MULTIMATCH_NVEC];
    float aDist[VEC1_MULTIMATCH_NVEC];
    int d;

    for(vv=0; vv<VEC1_MULTIMATCH_NVEC; vv++){
      aAcc[vv] = _mm256_setzero_ps();
    }

    for(d=0; d<=(nElem-8); d+=8){
      __m256 cent = LOADU( &aCent[iCent*nElem + d] );
      for(vv=0; vv<VEC1_MULTIMATCH_NVEC; vv++){
        __m256 diff = _mm256_sub_ps(cent, LOADU(&aVec[vv][d]));
        aAcc[vv] = FMADD(diff, diff, aAcc[vv]);
      }
    }

    for(vv=0; vv<VEC1_MULTIMATCH_NVEC; vv++){
      HORIZONTAL_SUM(aDist[vv], aAcc[vv]);
    }

    for(; d<nElem; d++){
      float cent = aCent[iCent*nElem + d];
      for(vv=0; vv<VEC1_MULTIMATCH_NVEC; vv++){
        float diff = cent - aVec[vv][d];
        aDist[vv] += (diff * diff);
      }
    }

    for(vv=0; vv<VEC1_MULTIMATCH_NVEC; vv++){
      if( aDist[vv]<aBestDist[vv] ){
        aBestDist[vv] = aDist[vv];
        aBest[vv] = iCent;
      }
    }
  }

  if( pfTotalDist ){
    for(vv=0; vv<VEC1_MULTIMATCH_NVEC; vv++){
      *pfTotalDist += aBestDist[vv];
    }
  }
}

#endif

/*
** A register-blocked microkernel, GEMM-style.
*/
static void xLloydsTiled(
  int nElem,
  const float *aCentroid,
  int nCentroid,
  const float *aTrain,
  int nTrain,
  int *aBest
){
  int ii;
  for(ii=0; ii<=(nTrain-VEC1_MULTIMATCH_NVEC); ii+=VEC1_MULTIMATCH_NVEC){
    const float *aVec[VEC1_MULTIMATCH_NVEC];
    int vv;
    for(vv=0; vv<VEC1_MULTIMATCH_NVEC; vv++){
      aVec[vv] = &aTrain[(ii+vv) * nElem];
    }

    vec1BestMatchN(nElem, aCentroid, nCentroid, aVec, &aBest[ii], 0);
  }

  for(; ii<nTrain; ii++){
    aBest[ii] = vec1BestMatchSimple(
        aCentroid, nCentroid, &aTrain[ii*nElem], nElem, 0
    );
  }
}

int main(int argc, char **argv){
#ifdef VEC1_HAVE_NEON
  if( argc==2 && strcmp(argv[1], "fmabench")==0 ){
    vec1FmaBench();
    return 0;
  }
#endif
  const struct Alg {
    const char *zName;
    void (*x)(int, const float*, int, const float*, int, int*);
  } aAlg[] = {
    { "naive", xLloydsNaive },
    { "strided", xLloydsStride },
    { "transposed", xLloydsTransposed2 },
    { "tiled", xLloydsTiled },
    { "modeltdist", xLloydsModelTDist },
  };
  /* Command line parameters */
  int nElem = 0;
  int nCentroid = 0;
  int nTrain = 0;
  int nRepeat = 0;

  float *aCentroid;               /* nCentroid centroids, each nElem floats */
  float *aTrain;                  /* nTrain training vectors, each nElem f.. */
  int *aRes;                      /* Result array */

  u64 t;                          /* Hardware timer value */
  int ii;                         /* Loop counter */
  void (*xLloyds)(int, const float*, int, const float*, int, int*) = 0;

  Vec1Random rnd;
  vec1RandomInit(&rnd, vec1HardwareTimer(), 0);

  if( argc!=6 ) usage(argv[0]);

  nElem = atoi( argv[1] );
  nCentroid = atoi( argv[2] );
  nTrain = atoi( argv[3] );
  nRepeat = atoi( argv[4] );

  /* Vector size must be a multiple of 8 */
  if( nElem<=0 || (nElem%8)!=0 ){
    fprintf(stderr, "NELEM must be a multiple of 8 greater than 0\n");
    return 1;
  }

  /* Number of centroids must be a multiple of 32 */
  if( nCentroid<=0 || (nCentroid%32)!=0 ){
    fprintf(stderr, "NCENTROID must be a multiple of 32 greater than 0\n");
    return 1;
  }

  /* Number of training vectors must be a multiple of 32 */
  if( nTrain<=0 || (nTrain%32)!=0 ){
    fprintf(stderr, "NTRAIN must be a multiple of 32 greater than 0\n");
    return 1;
  }

  /* Number of repeats must be greater than 0 */
  if( nRepeat<=0 ){
    fprintf(stderr, "NREPEAT must greater than 0\n");
    return 1;
  }

  for(ii=0; ii<(int)(sizeof(aAlg)/sizeof(aAlg[0])); ii++){
    if( strcmp(argv[5], aAlg[ii].zName)==0 ){
      xLloyds = aAlg[ii].x;
    }
  }
  if( xLloyds==0 ){
    fprintf(stderr, "no such algorithm: %s\n", argv[5]);
    return 1;
  }

  aCentroid = (float*)malloc( nCentroid * nElem * sizeof(float) );
  aTrain = (float*)malloc( nTrain * nElem * sizeof(float) );
  aRes = (int*)malloc( nTrain * sizeof(int) );

  vec1RandomArray(&rnd, aCentroid, nCentroid * nElem);
  vec1RandomArray(&rnd, aTrain, nTrain * nElem);

  t = vec1HardwareTimer();

  for(ii=0; ii<nRepeat; ii++){
    xLloyds(nElem, aCentroid, nCentroid, aTrain, nTrain, aRes);
  }

  t = vec1HardwareTimer() - t;

#if 1
  for(ii=0; ii<nTrain; ii++){
    float *pVec = &aTrain[ii*nElem];
    int iBest = 0;
    double fBest = vec1L2Dist(pVec, aCentroid, nElem);
    int jj;
    for(jj=1; jj<nCentroid; jj++){
      double fNew = vec1L2Dist(pVec, &aCentroid[jj*nElem], nElem);
      if( fNew<fBest ){
        iBest = jj;
        fBest = fNew;
      }
    }

    if( iBest!=aRes[ii] ){
      const double fTol = 0.00001;
      double fDist = vec1L2Dist(pVec, &aCentroid[aRes[ii]*nElem], nElem);
      if( (fDist>fBest && (fDist-fBest)/fDist>fTol) 
       || (fDist<fBest && (fBest-fDist)/fBest>fTol) 
      ){
        fprintf(stderr, "algorithm FAILED...\n");
        return 1;
      }
    }
  }
#endif

  printf("Cycles: ");
  if( t>(1000*1000*1000) ){
    printf("%.2fG\n", (double)t / 1000000000.0);
  }else if( t>(10*1000*1000) ){
    printf("%dM\n", (int)(t / (1000*1000)));
  }else if( t>(1*1000*1000) ){
    printf("%.2fM\n", (double)t / (1000*1000));
  }else{
    printf("%d\n", (int)t);
  }

  return 0;
}




