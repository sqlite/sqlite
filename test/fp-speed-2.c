/*
** Performance testing of floating-point decimal-to-binary conversion for
** SQLite versus the standard library.
**
** This module compares library atof() against SQLite's sqlite3AtoF().
** To go the other direction (binary-to-decimal) see fp-speed-1.c.
**
** To compile:
**
**    make sqlite3.c
**    gcc -Os -c sqlite3.c
**    gcc -I. -Os test/fp-speed-2.c sqlite3.o -ldl -lm -lpthread
**
** To run the test:
**
**    ./a.out 10000000
**
** Notes:
**
**   *  A first loop is run to measure the testing overhead.  This
**      overhead is then subtracted from the times of subsequent loops
**      so that the estimates are for the calls to atof() or
**      sqlite3AtoF() only.
*/
#include "sqlite3.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#ifdef _WIN32
# include <windows.h>
#else
# include <sys/time.h>
#endif

static const char *aVal[] = {
  "-1.01638304862856430",
  "+0.00492438073915869",
  "+7.38187324073439948",
  "+7.06785952192257171",
  "+9.28072663198500256",
  "+5.88710508619333946",
  "-2.29980236212596628",
  "-1.59035819249108474",
  "+2.43134418168449782",
  "-3.82909873289453990",
  "+1.87879140627440013",
  "+0.72706535604871466",
  "+0.06395776979131836",
  "+5.22923388793158619",
  "+6.37476826728722313",
  "+8.69723395383291069",
  "-9.50744860515976919",
  "-8.64802578453687530",
  "-3.56575957757974703",
  "-7.83231061797317613",
  "+7.78138752741208003",
  "-1.87397189283601965",
  "+8.68981459155933572",
  "+6.05667663598778378",
  "+4.17033792171481606",
  "+2.12838712887466515",
  "-6.83952360838918106",
  "-6.21142997639395291",
  "-2.07535257426146373",
  "+5.87274598039442902",
  "+8.58889910620021018",
  "+6.86244610313559176",
  "-3.30539867566709051",
  "-4.35968431527634449",
  "+0.08341395201040996",
  "-8.85819865489042224",
  "-3.66220954287276982",
  "-6.69658522970250632",
  "+1.82041693474064884",
  "+6.52345080386490003",
  "+1.59230060182190114",
  "+1.73625552916563894",
  "+7.28754319768547858",
  "+1.28358801059589267",
  "+8.05162533203208194",
  "+6.63246333993811454",
  "-1.71265000702800620",
  "+1.69957383415837123",
  "+7.60487669237386637",
  "+0.61591172354494550",
  "+5.75448943554159432",
  "+8.29702285926905818",
  "-6.55319253601630674",
  "+5.83213346061870300",
  "+5.65571665095718917",
  "+0.33227897084384087",
  "-7.12106487766986866",
  "-9.67212625267063433",
  "-3.45839165713773950",
  "+4.78960943232147507",
  "-9.69260280400041378",
  "+7.06838482753813854",
  "-5.29701141821629619",
  "-4.42870212009053932",
  "+0.07288911557328087",
  "-9.18554620258794474",
  "+3.72941262341310077",
  "+2.68574218827927192",
  "-4.70706073336246853",
  "+7.21758207682793342",
  "-8.36784125342611634",
  "+2.21748443042418221",
  "+0.19498245886068610",
  "-9.73340529556720719",
  "-9.77938877669369998",
  "-5.15611645874169315",
  "-7.50489935777651747",
  "+7.35560765686877845",
  "-5.06816285755335998",
  "+1.52097056420277478",
  "-7.59897825350482960",
  "+1.36541372033897758",
  "-1.64417205546513720",
  "-4.90424331961411259",
  "-7.70636119616491307",
  "+0.16994274609307662",
  "+8.33743178495722168",
  "-5.23553304804695800",
  "-3.85100459421941479",
  "-6.35136225443263398",
  "+2.38693034844544289",
  "+3.83527158716203602",
  "-3.12631204931368879",
  "-5.57947970025564908",
  "-8.81098744795956043",
  "-4.37273601202032169",
  "-3.11099511896685399",
  "-9.48418780317042682",
  "-3.73984516683044072",
  "+4.89840420089159599",
};
#define NN (sizeof(aVal)/sizeof(aVal[0]))

/* Return the current wall-clock time in microseconds since the
** Unix epoch (1970-01-01T00:00:00Z)
*/
static sqlite3_int64 timeOfDay(void){
#if defined(_WIN64) && _WIN32_WINNT >= _WIN32_WINNT_WIN8
  sqlite3_uint64 t;
  FILETIME tm;
  GetSystemTimePreciseAsFileTime(&tm);
  t =  ((sqlite3_uint64)tm.dwHighDateTime<<32) |
          (sqlite3_uint64)tm.dwLowDateTime;
  t += 116444736000000000LL;
  t /= 10;
  return t;
#elif defined(_WIN32)
  static sqlite3_vfs *clockVfs = 0;
  sqlite3_int64 t;
  if( clockVfs==0 ) clockVfs = sqlite3_vfs_find(0);
  if( clockVfs==0 ) return 0;  /* Never actually happens */
  if( clockVfs->iVersion>=2 && clockVfs->xCurrentTimeInt64!=0 ){
    clockVfs->xCurrentTimeInt64(clockVfs, &t);
  }else{
    double r;
    clockVfs->xCurrentTime(clockVfs, &r);
    t = (sqlite3_int64)(r*86400000.0);
  }
  return t*1000;
#else
  struct timeval sNow;
  (void)gettimeofday(&sNow,0);
  return ((sqlite3_int64)sNow.tv_sec)*1000000 + sNow.tv_usec;
#endif
}

/*
** Generate text of the i-th test floating-point literal.
*/
static int fpLiteral(int i, char *z){
  int e, ex, len, ix;
  ex = i%401;
  e = ex - 200;
  len = (i/401)%16 + 4;
  ix = (i/(401*16))%NN;
  memcpy(z, aVal[ix], len);
  z[len++] = 'e';
  if( e<0 ){
    z[len++] = '-';
    e = -e;
  }else{
    z[len++] = '+';
  }
  z[len++] = e/100 + '0';
  z[len++] = (e/10)%10 + '0';
  z[len++] = e%10 + '0';
  z[len] = 0;
  return ex;
}

int main(int argc, char **argv){
  int i;
  int cnt;
  sqlite3_int64 tm[3];
  double arSum[401];
  char z[1000];

  if( argc!=2 ){
    printf("Usage:  %s COUNT\n", argv[0]);
    printf("Suggested value for COUNT is 10 million\n");
    return 1;
  }
  cnt = atoi(argv[1]);
  if( cnt<100 ){
    printf("Minimum COUNT value is 100");
    return 1;
  }

  printf("test-overhead: ");
  fflush(stdout);
  memset(arSum, 0, sizeof(arSum));
  tm[2] = timeOfDay();
  for(i=0; i<cnt; i++){
    double r = (double)i;
    int ex = fpLiteral(i,z);
    arSum[ex] += r;
  }
  tm[2] = timeOfDay() - tm[2];
  for(i=1; i<=400; i++) arSum[0] += arSum[i];
  printf("%6.1f ns/test, %9.6f sec total\n", tm[2]*1e3/cnt, tm[2]*1e-6);

  printf("C-lib atof():  ");
  fflush(stdout);
  memset(arSum, 0, sizeof(arSum));
  tm[0] = timeOfDay();
  for(i=0; i<cnt; i++){
    int ex = fpLiteral(i,z);
    arSum[ex] += atof(z);
  }
  tm[0] = timeOfDay() - tm[0] - tm[2];
  for(i=1; i<=400; i++) arSum[0] += arSum[i];
  printf("%6.1f ns/test, %9.6f sec net", tm[0]*1e3/cnt, tm[0]*1e-6);
  printf(", cksum: %g\n", arSum[0]);

  printf("sqlite3AtoF(): ");
  fflush(stdout);
  memset(arSum, 0, sizeof(arSum));
  tm[1] = timeOfDay();
  for(i=0; i<cnt; i++){
    double r = 0.0;
    int ex = fpLiteral(i,z);
    sqlite3_test_control(SQLITE_TESTCTRL_ATOF, z, &r);
    arSum[ex] += r;
  }
  tm[1] = timeOfDay() - tm[1] - tm[2];
  for(i=1; i<=400; i++) arSum[0] += arSum[i];
  printf("%6.1f ns/test, %9.6f sec net", tm[1]*1e3/cnt, tm[1]*1e-6);
  printf(", cksum: %g\n", arSum[0]);

  if( tm[0] < tm[1] ){
    printf("atof() is about %g times faster than sqlite3AtoF()\n",
           (double)tm[1]/(double)tm[0]);
  }else{
    printf("sqlite3AtoF() is about %g times faster than atof()\n",
           (double)tm[0]/(double)tm[1]);
  }
  return 0;
}
