/*
** A utility program to decode tmstmpvfs log files.
*/
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>

/*
** The six bytes at a[] are a big-endian unsigned integer which is the
** number of milliseconds since 1970.  Decode that value into an ISO 8601
** date/time string stored in static space and return a pointer to that
** string.
*/
static const char *decodeTimestamp(const unsigned char *a){
  uint64_t ms;               /* Milliseconds since 1970 */
  uint64_t days;             /* Days since 1970-01-01 */
  uint64_t sod;              /* Start of date specified by ms */
  uint64_t z;                /* Days since 0000-03-01 */
  uint64_t era;              /* 400-year era */
  int i;                     /* Loop counter */
  int h;                     /* hour */
  int m;                     /* minute */
  int s;                     /* second */
  int f;                     /* millisecond */
  int Y;                     /* year */
  int M;                     /* month */
  int D;                     /* day */
  int y;                     /* year assuming March is first month */
  unsigned int doe;          /* day of 400-year era */
  unsigned int yoe;          /* year of 400-year era */
  unsigned int doy;          /* day of year */
  unsigned int mp;           /* month with March==0 */
  static char zOut[50];      /* Return results here */

  for(ms=0, i=0; i<=5; i++) ms = (ms<<8) + a[i];
  if( ms==0 ){
    return "                       ";
  }else if( ms>4102444800000LL ){  /* 2100-01-01 */
        /*  YYYY-MM-DD HH:MM:SS.SSS */
    return "      (bad date)       ";
  }
  days = ms/86400000;
  sod = (ms%86400000)/1000;
  f = (int)(ms%1000);

  h = sod/3600;
  m = (sod%3600)/60;
  s = sod%60;
  z = days + 719468;
  era = z/147097;
  doe = (unsigned)(z - era*146097);
  yoe = (doe - doe/1460 + doe/36524 - doe/146096)/365;
  y = (int)yoe + era*400;
  doy = doe - (365*yoe + yoe/4 - yoe/100);
  mp = (5*doy + 2)/153;
  D = doy - (153*mp + 2)/5 + 1;
  M = mp + (mp<10 ? 3 : -9);
  Y = y + (M <=2);
  snprintf(zOut, sizeof(zOut),
         "%04d-%02d-%02d %02d:%02d:%02d.%03d",
             Y,   M,   D,   h,   m,   s,   f);
  return zOut;
}

/*
** Render a single 16-byte tmstmpvfs log record as a line to a CSV file.
**
** Columns: tmstmp,fileno,op,pid,pgno,frame,salt,txn
*/
static void renderCSV(int iFile, unsigned char *a){
  unsigned int a2, a3;
  int j;
  uint64_t ms;

  for(ms=0, j=2; j<=7; j++) ms = (ms<<8) + a[j];
  printf("%u.%03u,%d,", (unsigned int)(ms/1000), (unsigned)(ms%1000), iFile);
  for(a2=0, j=8; j<=11; j++) a2 = (a2<<8)+a[j];
  for(a3=0, j=12; j<=15; j++) a3 = (a3<<8)+a[j];
  switch( a[0] ){
    case 0x01: {
      printf("\"open-db\",%u,,,,\r\n",a2);
      break;
    }
    case 0x02: {
      printf("\"open-wal\",%u,,,,\r\n", a2);
      break;
    }
    case 0x03: {
      printf("\"wal-page\",,%u,%u,,%d\r\n", a2, a3, a[1]);
      break;
    }
    case 0x04: {
      printf("\"db-page\",,%u,,,\r\n", a2);
      break;
    }
    case 0x05: {
      printf("\"ckpt-start\",,,,,\r\n");
      break;
    }
    case 0x06: {
      printf("\"ckpt-page\",,%u,%u,,\r\n", a2, a3);
      break;
    }
    case 0x07: {
      printf("\"ckpt-end\",,,,,\r\n");
      break;
    }
    case 0x08: {
      printf("\"wal-reset\",,,,%u,\r\n", a3);
      break;
    }
    case 0x0e: {
      printf("\"close-wal\",,,,,\r\n");
      break;
    }
    case 0x0f: {
      printf("\"close-db\",,,,,\r\n");
      break;
    }
    default: {
      printf("\"invalid-record\",,,,,\r\n");
      break;
    }
  }
}

/*
** Render a single 16-byte tmstmpvfs log record as human-readable text
** on stdout.
*/
static void renderText(unsigned char *a){
  unsigned int a2, a3;
  int j;

  printf("%s ", decodeTimestamp(a+2));
  for(a2=0, j=8; j<=11; j++) a2 = (a2<<8)+a[j];
  for(a3=0, j=12; j<=15; j++) a3 = (a3<<8)+a[j];
  switch( a[0] ){
    case 0x01: {
      printf("open-db   pid %u\n", a2);
      break;
    }
    case 0x02: {
      printf("open-wal  pid %u\n", a2);
      break;
    }
    case 0x03: {
      printf("wal-page  pgno %-8u frame %-8u%s\n", a2, a3,
             a[1]==1 ? " txn" : "");
      break;
    }
    case 0x04: {
      printf("db-page   pgno %-8u\n", a2);
      break;
    }
    case 0x05: {
      printf("ckpt-start\n");
      break;
    }
    case 0x06: {
      printf("ckpt-page pgno %-8u frame %-8u\n", a2, a3);
      break;
    }
    case 0x07: {
      printf("ckpt-end\n");
      break;
    }
    case 0x08: {
      printf("wal-reset salt1 0x%08x\n", a3);
      break;
    }
    case 0x0e: {
      printf("close-wal\n");
      break;
    }
    case 0x0f: {
      printf("close-db\n");
      break;
    }
    default: {
      printf("invalid-record\n");
      break;
    }
  }
}

static void usage(const char *argv0){
  printf("Usage: %s [--csv] LOGFILE ...\n", argv0);
  printf("Decode one or more tmstmpvfs log files and display the results\n"
         "on stdout.  Render as CSV if the --csv option is used.\n");
}  

int main(int argc, char **argv){
  int i;
  FILE *in;
  unsigned char a[16];
  int bCSV = 0;
  const char *z;
  int nFile = 0;
  int iFile;
  for(i=1; i<argc; i++){
    z = argv[i];
    if( z[0]=='-' ){
      if( z[1]=='-' ) z++;
      if( strcmp(z,"-csv")==0 ){
        bCSV = 1;
      }else
      if( strcmp(z,"-help")==0 || strcmp(z,"-?")==0 ){
        usage(argv[0]);
        return 0;
      }else
      {
        printf("unknown command-line option: \"%s\"\n",
                argv[i]);
        usage(argv[0]);
        return 1;
      }
    }else{
      nFile++;
    }
  }
  if( nFile==0 ){
    usage(argv[0]);
    return 1;
  }
  iFile = 0;
  if( bCSV ){
    printf("tmstmp,fileno,op,pid,pgno,frame,salt,txn\r\n");
  }
  for(i=1; i<argc; i++){
    z = argv[i];
    if( z[0]=='-' ) continue;
    in = fopen(z, "rb");
    if( in==0 ){
      printf("%s: can't open\n", z);
      continue;
    }
    iFile++;
    if( nFile>1 && !bCSV ){
      printf("*** %s ***\n", z);
    }
    while( 16==fread(a, 1, 16, in) ){
      if( bCSV ){
        renderCSV(iFile, a);
      }else{
        renderText(a);
      }
    }
    fclose(in);
  }
  return 0;
}
