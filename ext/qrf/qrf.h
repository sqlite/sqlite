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
typedef struct sqlite3_qrf_spec sqlite3_qrf_spec;
struct sqlite3_qrf_spec {
  unsigned char iVersion;     /* Version number of this structure */
  unsigned char eFormat;      /* Output format */
  unsigned char bShowCNames;  /* True to show column names */
  unsigned char eEscape;      /* How to deal with control characters */
  unsigned char eQuote;       /* Quoting style for text */
  unsigned char eBlob;        /* Quoting style for BLOBs */
  unsigned char bWordWrap;    /* Try to wrap on word boundaries */
  short int mxWidth;          /* Maximum width of any column */
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
** Range of values for sqlite3_qrf_spec.aWidth[] entries:
*/
#define QRF_MX_WIDTH    (10000)
#define QRF_MN_WIDTH    (-10000)
#define QRF_MINUS_ZERO  (-32768)    /* Special value meaning -0 */

/*
** Interfaces
*/
int sqlite3_format_query_result(
  sqlite3_stmt *pStmt,                /* SQL statement to run */
  const sqlite3_qrf_spec *pSpec,   /* Result format specification */
  char **pzErr                        /* OUT: Write error message here */
);

/*
** Output styles:
*/
#define QRF_MODE_List      0 /* One record per line with a separator */
#define QRF_MODE_Line      1 /* One column per line. */
#define QRF_MODE_Html      2 /* Generate an XHTML table */
#define QRF_MODE_Json      3 /* Output is a list of JSON objects */
#define QRF_MODE_Insert    4 /* Generate SQL "insert" statements */
#define QRF_MODE_Csv       5 /* Comma-separated-value */
#define QRF_MODE_Explain   6 /* EXPLAIN output */
#define QRF_MODE_ScanExp   7 /* EXPLAIN output with vm stats */
#define QRF_MODE_EQP       8 /* Format EXPLAIN QUERY PLAN output */
#define QRF_MODE_Markdown  9 /* Markdown formatting */
#define QRF_MODE_Column   10 /* One record per line in neat columns */
#define QRF_MODE_Table    11 /* MySQL-style table formatting */
#define QRF_MODE_Box      12 /* Unicode box-drawing characters */
#define QRF_MODE_Count    13 /* Output only a count of the rows of output */
#define QRF_MODE_Off      14 /* No query output shown */

/*
** Quoting styles for text.
** Allowed values for sqlite3_qrf_spec.eQuote
*/
#define QRF_TXT_Off     0 /* Literal text */
#define QRF_TXT_Sql     1 /* Quote as an SQL literal */
#define QRF_TXT_Csv     2 /* CSV-style quoting */
#define QRF_TXT_Html    3 /* HTML-style quoting */
#define QRF_TXT_Tcl     4 /* C/Tcl quoting */
#define QRF_TXT_Json    5 /* JSON quoting */

/*
** Quoting styles for BLOBs
** Allowed values for sqlite3_qrf_spec.eBlob
*/
#define QRF_BLOB_Auto    0 /* Determine BLOB quoting using eQuote */
#define QRF_BLOB_Text    1 /* Display content exactly as it is */
#define QRF_BLOB_Sql     2 /* Quote as an SQL literal */
#define QRF_BLOB_Hex     3 /* Hexadecimal representation */
#define QRF_BLOB_Tcl     4 /* "\000" notation */
#define QRF_BLOB_Json    5 /* A JSON string */

/*
** Control-character escape modes.
** Allowed values for sqlite3_qrf_spec.eEscape
*/
#define QRF_ESC_Off     0 /* Do not escape control characters */
#define QRF_ESC_Ascii   1 /* Unix-style escapes.  Ex: U+0007 shows ^G */
#define QRF_ESC_Symbol  2 /* Unicode escapes. Ex: U+0007 shows U+2407 */
