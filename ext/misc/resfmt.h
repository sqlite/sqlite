/*
** 2025-10-20
**
** The author disclaims copyright to this source code.  In place of
** a legal notice, here is a blessing:
**
**    May you do good and not evil.
**    May you find forgiveness for yourself and forgive others.
**    May you share freely, never taking more than you give.
**
*************************************************************************
** Header file for the Result-Format or "resfmt" utility library for SQLite.
** See the resfmt.md documentation for additional information.
*/
#include "sqlite3.h"

/*
** Specification used by clients to define the output format they want
*/
typedef struct sqlite3_resfmt_spec sqlite3_resfmt_spec;
struct sqlite3_resfmt_spec {
  int iVersion;               /* Version number of this structure */
  int eFormat;                /* Output format */
  unsigned char bShowCNames;  /* True to show column names */
  unsigned char eEscMode;     /* How to deal with control characters */
  unsigned char bQuote;       /* Quote output values as SQL literals */
  unsigned char bWordWrap;    /* Try to wrap on word boundaries */
  int mxWidth;                /* Maximum column width in columnar modes */
  const char *zColumnSep;     /* Alternative column separator */
  const char *zRowSep;        /* Alternative row separator */
  const char *zTableName;     /* Output table name */
  const char *zNull;          /* Rendering of NULL */
  const char *zFloatFmt;      /* printf-style string for rendering floats */
  int nWidth;                 /* Number of column width parameters */
  short int *aWidth;          /* Column widths */
  char *(*xRender)(void*,sqlite3_value*);                 /* Render a value */
  ssize_t (*xWrite)(void*,const unsigned char*,ssize_t);  /* Write callback */
  void *pRenderArg;           /* First argument to the xRender callback */
  void *pWriteArg;            /* First argument to the xWrite callback */
  char **pzOutput;            /* Storage location for output string */
  /* Additional fields may be added in the future */
};

/*
** Opaque state structure used by this library.
*/
typedef struct sqlite3_resfmt sqlite3_resfmt;

/*
** Interfaces
*/
sqlite3_resfmt *sqlite3_resfmt_begin(sqlite3_stmt*, sqlite3_resfmt_spec*);
int sqlite3_resfmt_row(sqlite3_resfmt*)
int sqlite3_resfmt_finish(sqlite3_resfmt*,int*,char**);

/*
** Output styles:
*/
#define RESFMT_Line      0 /* One column per line. */
#define RESFMT_Column    1 /* One record per line in neat columns */
#define RESFMT_List      2 /* One record per line with a separator */
#define RESFMT_Html      3 /* Generate an XHTML table */
#define RESFMT_Insert    4 /* Generate SQL "insert" statements */
#define RESFMT_Tcl       5 /* Generate ANSI-C or TCL quoted elements */
#define RESFMT_Csv       6 /* Quote strings, numbers are plain */
#define RESFMT_Explain   7 /* Like RESFMT_Column, but do not truncate data */
#define RESFMT_Pretty    8 /* Pretty-print schemas */
#define RESFMT_EQP       9 /* Converts EXPLAIN QUERY PLAN output into a graph */
#define RESFMT_Json     10 /* Output JSON */
#define RESFMT_Markdown 11 /* Markdown formatting */
#define RESFMT_Table    12 /* MySQL-style table formatting */
#define RESFMT_Box      13 /* Unicode box-drawing characters */
#define RESFMT_Count    14 /* Output only a count of the rows of output */
#define RESFMT_Off      15 /* No query output shown */
#define RESFMT_ScanExp  16 /* Like RESFMT_Explain, but for ".scanstats vm" */
#define RESFMT_Www      17 /* Full web-page output */
