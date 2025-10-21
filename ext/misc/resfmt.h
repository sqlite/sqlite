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
#include <stdlib.h>
#include "sqlite3.h"

/*
** Specification used by clients to define the output format they want
*/
typedef struct sqlite3_resfmt_spec sqlite3_resfmt_spec;
struct sqlite3_resfmt_spec {
  short int iVersion;         /* Version number of this structure */
  unsigned char eFormat;      /* Output format */
  unsigned char bShowCNames;  /* True to show column names */
  unsigned char eEscape;      /* How to deal with control characters */
  unsigned char eQuote;       /* Quoting style for text */
  unsigned char eBlob;        /* Quoting style for BLOBs */
  unsigned char bWordWrap;    /* Try to wrap on word boundaries */
  short int mxWidth;          /* Maximum column width in columnar modes */
  int nWidth;                 /* Number of column width parameters */
  short int *aWidth;          /* Column widths */
  const char *zColumnSep;     /* Alternative column separator */
  const char *zRowSep;        /* Alternative row separator */
  const char *zTableName;     /* Output table name */
  const char *zNull;          /* Rendering of NULL */
  const char *zFloatFmt;      /* printf-style string for rendering floats */
  char *(*xRender)(void*,sqlite3_value*);                /* Render a value */
  ssize_t (*xWrite)(void*,const unsigned char*,size_t);  /* Write callback */
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
int sqlite3_resfmt_row(sqlite3_resfmt*);
int sqlite3_resfmt_finish(sqlite3_resfmt*,int*,char**);

/*
** Output styles:
*/
#define RESFMT_List      0 /* One record per line with a separator */
#define RESFMT_Line      1 /* One column per line. */
#define RESFMT_Html      2 /* Generate an XHTML table */
#define RESFMT_Insert    3 /* Generate SQL "insert" statements */
#define RESFMT_Explain   4 /* EXPLAIN output */
#define RESFMT_ScanExp   5 /* EXPLAIN output with vm stats */
#define RESFMT_EQP       6 /* Converts EXPLAIN QUERY PLAN output into a graph */
#define RESFMT_Markdown  7 /* Markdown formatting */
#define RESFMT_Column    8 /* One record per line in neat columns */
#define RESFMT_Table     9 /* MySQL-style table formatting */
#define RESFMT_Box      10 /* Unicode box-drawing characters */
#define RESFMT_Count    11 /* Output only a count of the rows of output */
#define RESFMT_Off      12 /* No query output shown */

/*
** Quoting styles for text.
** Allowed values for sqlite3_resfmt_spec.eQuote
*/
#define RESFMT_Q_Off     0 /* Literal text */
#define RESFMT_Q_Sql     1 /* Quote as an SQL literal */
#define RESFMT_Q_Csv     2 /* CSV-style quoting */
#define RESFMT_Q_Html    3 /* HTML-style quoting */
#define RESFMT_Q_Tcl     4 /* C/Tcl quoting */
#define RESFMT_Q_Json    5 /* JSON quoting */

/*
** Quoting styles for BLOBs
** Allowed values for sqlite3_resfmt_spec.eBlob
*/
#define RESFMT_B_Auto    0 /* Determine BLOB quoting using eQuote */
#define RESFMT_B_Text    1 /* Display content exactly as it is */
#define RESFMT_B_Sql     2 /* Quote as an SQL literal */
#define RESFMT_B_Hex     3 /* Hexadecimal representation */
#define RESFMT_B_Tcl     4 /* "\000" notation */
#define RESFMT_B_Json    5 /* A JSON string */

/*
** Control-character escape modes.
** Allowed values for sqlite3_resfmt_spec.eEscape
*/
#define RESFMT_E_Off     0 /* Do not escape control characters */
#define RESFMT_E_Ascii   1 /* Unix-style escapes.  Ex: U+0007 shows ^G */
#define RESFMT_E_Symbol  2 /* Unicode escapes. Ex: U+0007 shows U+2407 */
