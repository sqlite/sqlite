/*
** This program generates C code for the tables used to generate powers
** of 10 in the powerOfTen() subroutine in util.c.
**
** The objective of the powerOfTen() subroutine is to provide the most
** significant 96 bits of any power of 10 between -348 and +347.  Rather
** than generate a massive 8K table, three much smaller tables are constructed,
** which can then generate the requested power of 10 using a single
** 160-bit multiple.
**
** This program works by internally generating a table of powers of
** 10 accurate to 256 bits each.  It then that full-sized, high-accuracy
** table to construct the three smaller tables needed by powerOfTen().
**
** LIMITATION:
**
** This program uses the __uint128_t datatype, available in gcc/clang.
** It won't build using other compilers.
*/
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>

typedef unsigned __int128 u128;          /* 128-bit unsigned integer */
typedef unsigned long long int u64;      /* 64-bit unsigned integer */

/* There is no native 256-bit unsigned integer type, so synthesize one
** using four 64-bit unsigned integers.  Must significant first. */
struct u256 {
  u64 a[4];  /* big-endian */
};
typedef struct u256 u256;

/*
** Return a u64 with the N-th bit set.
*/
#define U64_BIT(N)  (((u64)1)<<(N))

/* Multiple *pX by 10, in-place */
static void u256_times_10(u256 *pX){
  u64 carry = 0;
  int i;
  for(i=3; i>=0; i--){
    u128 y = (10 * (u128)pX->a[i]) + carry;
    pX->a[i] = (u64)y;
    carry = (u64)(y>>64);
  }
}

/* Multiple *pX by 2, in-place.  AKA, left-shift */
static void u256_times_2(u256 *pX){
  u64 carry = 0;
  int i;
  for(i=3; i>=0; i--){
    u64 y = pX->a[i];
    pX->a[i] = (y<<1) | carry;
    carry = y>>63;
  }
}

/* Divide *pX by 10, in-place */
static void u256_div_10(u256 *pX){
  u64 rem = 0;
  int i;
  for(i=0; i<4; i++){
    u128 acc = (((u128)rem)<<64) | pX->a[i];
    pX->a[i] = acc/10;
    rem = (u64)(acc%10);
  }
}

/* Divide *pX by 2, in-place,  AKA, right-shift */
static void u256_div_2(u256 *pX){
  u64 rem = 0;
  int i;
  for(i=0; i<4; i++){
    u64 y = pX->a[i];
    pX->a[i] = (y>>1) | (rem<<63);
    rem = y&1;
  }
}

/* Note: The main table is a little larger on the low end than the required
** range of -348..+347, since we need the -351 value for the reduced tables.
*/
#define SCALE_FIRST  (-351)                          /* Least power-of-10 */
#define SCALE_LAST   (+347)                          /* Largest power-of-10 */
#define SCALE_COUNT  (SCALE_LAST - SCALE_FIRST + 1)  /* Size of main table */
#define SCALE_ZERO   (351)

int main(int argc, char **argv){
  int i, j, iNext;
  int e;
  int bRound = 0;
  int bTruth = 0;
  const u64 top = ((u64)1)<<63;
  u256 v;
  u64 aHi[SCALE_COUNT];
  u64 aLo[SCALE_COUNT];
  int aE[SCALE_COUNT];

  for(i=1; i<argc; i++){
    const char *z = argv[i];
    if( z[0]=='-' && z[1]=='-' && z[2]!=0 ) z++;
    if( strcmp(z,"-round")==0 ){
      bRound = 1;
    }else
    if( strcmp(z,"-truth")==0 ){
      bTruth = 1;
    }else
    {
      fprintf(stderr, "unknown option: \"%s\"\n", argv[i]);
      exit(1);
    }
  }

  /* Generate the master 256-bit power-of-10 table */
  v.a[0] = top;
  v.a[1] = 0;
  v.a[2] = 0;
  v.a[3] = 0;
  for(i=SCALE_ZERO, e=63; i>=0; i--){
    aHi[i] = v.a[0];
    aLo[i] = v.a[1];
    aE[i] = e;
    u256_div_10(&v);
    while( v.a[0] < top ){
      e++;
      u256_times_2(&v);
    }
  }
  v.a[0] = 0;
  v.a[1] = top;
  v.a[2] = 0;
  v.a[3] = 0;
  for(i=SCALE_ZERO+1, e=63; i<SCALE_COUNT; i++){
    u256_times_10(&v);
    while( v.a[0]>0 ){
      e--;
      u256_div_2(&v);
    }
    aHi[i] = v.a[1];
    aLo[i] = v.a[2];
    aE[i] = e;
  }

  if( bTruth ){
    /* With the --truth flag, also output the aTruth[] table that
    ** contains 128 bits of every power-of-two in the range */
    printf("  /* Powers of ten, accurate to 128 bits each */\n");
    printf("  static const struct {u64 hi; u64 lo;} aTruth[] = {\n");
    for(i=0; i<SCALE_COUNT; i++){
      u64 x = aHi[i];
      u64 y = aLo[i];
      int e = aE[i];
      char *zOp;
      if( e>0 ){
        zOp = "<<";
      }else{
        e = -e;
        zOp = ">>";
      }
      printf("   {0x%016llx, 0x%016llx}, /* %2d: 1.0e%+d %s %d */\n",
           x, y, i, i+SCALE_FIRST, zOp, e);
    }
    printf("  };\n");
  }

  /* The aBase[] table contains powers of 10 between 0 and 26.  These
  ** all fit in a single 64-bit integer.
  */
  printf("  static const u64 aBase[] = {\n");
  for(i=SCALE_ZERO, j=0; i<SCALE_ZERO+27; i++, j++){
    u64 x = aHi[i];
    int e = aE[i];
    char *zOp;
    if( e>0 ){
      zOp = "<<";
    }else{
      e = -e;
      zOp = ">>";
    }
    printf("    UINT64_C(0x%016llx), /* %2d: 1.0e%+d %s %d */\n",
           x, j, i+SCALE_FIRST, zOp, e);
  }
  printf("  };\n");

  /* For powers of 10 outside the range [0..26], we have to multiple
  ** on of the aBase[] entries by a scaling factor to get the true
  ** power of ten.  The scaling factors are all approximates accurate
  ** to 96 bytes, represented by a 64-bit integer in aScale[] for the
  ** most significant bits and a 32-bit integer in aScaleLo[] for the
  ** next 32 bites.
  **
  ** The scale factors are at increments of 27.  Except, the entry for 0
  ** is replaced by the -1 value as a special case.
  */
  printf("  static const u64 aScale[] = {\n");
  for(i=j=0; i<SCALE_COUNT; i=iNext, j++){
    const char *zExtra = "";
    iNext = i+27;
    if( i==SCALE_ZERO ){ i--; zExtra = " (special case)"; }
    u64 x = aHi[i];
    int e = aE[i];
    char *zOp;
    if( e>0 ){
      zOp = "<<";
    }else{
      e = -e;
      zOp = ">>";
    }
    printf("    UINT64_C(0x%016llx), /* %2d: 1.0e%+d %s %d%s */\n",
           x, j, i+SCALE_FIRST, zOp, e, zExtra);
  }
  printf("  };\n");
  printf("  static const unsigned int aScaleLo[] = {\n");
  for(i=j=0; i<SCALE_COUNT; i=iNext, j++){
    const char *zExtra = "";
    iNext = i+27;
    if( i==SCALE_ZERO ){ i--; zExtra = " (special case)"; }
    u64 x = aLo[i];
    int e = aE[i];
    char *zOp;
    if( bRound && (x & U64_BIT(31))!=0 && i!=SCALE_ZERO-1 ) x += U64_BIT(32);
    if( e>0 ){
      zOp = "<<";
    }else{
      e = -e;
      zOp = ">>";
    }
    printf("    0x%08llx, /* %2d: 1.0e%+d %s %d%s */\n",
           x>>32, j, i+SCALE_FIRST, zOp, e, zExtra);
  }
  printf("  };\n");
  return 0;
}
