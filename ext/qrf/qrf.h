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
#ifndef SQLITE_QRF_H
#define SQLITE_QRF_H
#include <stdlib.h>
#include "sqlite3.h"

/*
** Specification used by clients to define the output format they want
*/
typedef struct sqlite3_qrf_spec sqlite3_qrf_spec;
struct sqlite3_qrf_spec {
  unsigned char iVersion;     /* Version number of this structure */
  unsigned char eStyle;       /* Formatting style.  "box", "csv", etc... */
  unsigned char eEsc;         /* How to escape control characters in text */
  unsigned char eText;        /* Quoting style for text */
  unsigned char eTitle;       /* Quating style for the text of column names */
  unsigned char eBlob;        /* Quoting style for BLOBs */
  unsigned char bTitles;      /* True to show column names */
  unsigned char bWordWrap;    /* Try to wrap on word boundaries */
  unsigned char bTextJsonb;   /* Render JSONB blobs as JSON text */
  unsigned char bTextNull;    /* Apply eText encoding to zNull[] */
  unsigned char eDfltAlign;   /* Default alignment, no covered by aAlignment */
  unsigned char eTitleAlign;  /* Alignment for column headers */
  short int nWrap;            /* Wrap columns wider than this */
  short int nScreenWidth;     /* Maximum overall table width */
  short int nLineLimit;       /* Maximum number of lines for any row */
  int nCharLimit;             /* Maximum number of characters in a cell */
  int nWidth;                 /* Number of entries in aWidth[] */
  int nAlign;                 /* Number of entries in aAlignment[] */
  short int *aWidth;          /* Column widths */
  unsigned char *aAlign;      /* Column alignments */
  char *zColumnSep;           /* Alternative column separator */
  char *zRowSep;              /* Alternative row separator */
  char *zTableName;           /* Output table name */
  char *zNull;                /* Rendering of NULL */
  char *(*xRender)(void*,sqlite3_value*);           /* Render a value */
  int (*xWrite)(void*,const char*,sqlite3_int64);   /* Write output */
  void *pRenderArg;           /* First argument to the xRender callback */
  void *pWriteArg;            /* First argument to the xWrite callback */
  char **pzOutput;            /* Storage location for output string */
  /* Additional fields may be added in the future */
};

/*
** Interfaces
*/
int sqlite3_format_query_result(
  sqlite3_stmt *pStmt,             /* SQL statement to run */
  const sqlite3_qrf_spec *pSpec,   /* Result format specification */
  char **pzErr                     /* OUT: Write error message here */
);

/*
** Range of values for sqlite3_qrf_spec.aWidth[] entries and for
** sqlite3_qrf_spec.mxColWidth and .nScreenWidth
*/
#define QRF_MAX_WIDTH    10000
#define QRF_MIN_WIDTH    0

/*
** Output styles:
*/
#define QRF_STYLE_Auto      0 /* Choose a style automatically */
#define QRF_STYLE_Box       1 /* Unicode box-drawing characters */
#define QRF_STYLE_Column    2 /* One record per line in neat columns */
#define QRF_STYLE_Count     3 /* Output only a count of the rows of output */
#define QRF_STYLE_Csv       4 /* Comma-separated-value */
#define QRF_STYLE_Eqp       5 /* Format EXPLAIN QUERY PLAN output */
#define QRF_STYLE_Explain   6 /* EXPLAIN output */
#define QRF_STYLE_Html      7 /* Generate an XHTML table */
#define QRF_STYLE_Insert    8 /* Generate SQL "insert" statements */
#define QRF_STYLE_Json      9 /* Output is a list of JSON objects */
#define QRF_STYLE_JsonLine 10 /* Independent JSON objects for each row */
#define QRF_STYLE_Line     11 /* One column per line. */
#define QRF_STYLE_List     12 /* One record per line with a separator */
#define QRF_STYLE_Markdown 13 /* Markdown formatting */
#define QRF_STYLE_Off      14 /* No query output shown */
#define QRF_STYLE_Quote    15 /* SQL-quoted, comma-separated */
#define QRF_STYLE_Stats    16 /* EQP-like output but with performance stats */
#define QRF_STYLE_StatsEst 17 /* EQP-like output with planner estimates */
#define QRF_STYLE_StatsVm  18 /* EXPLAIN-like output with performance stats */
#define QRF_STYLE_Table    19 /* MySQL-style table formatting */

/*
** Quoting styles for text.
** Allowed values for sqlite3_qrf_spec.eText
*/
#define QRF_TEXT_Auto    0 /* Choose text encoding automatically */
#define QRF_TEXT_Plain   1 /* Literal text */
#define QRF_TEXT_Sql     2 /* Quote as an SQL literal */
#define QRF_TEXT_Csv     3 /* CSV-style quoting */
#define QRF_TEXT_Html    4 /* HTML-style quoting */
#define QRF_TEXT_Tcl     5 /* C/Tcl quoting */
#define QRF_TEXT_Json    6 /* JSON quoting */

/*
** Quoting styles for BLOBs
** Allowed values for sqlite3_qrf_spec.eBlob
*/
#define QRF_BLOB_Auto    0 /* Determine BLOB quoting using eText */
#define QRF_BLOB_Text    1 /* Display content exactly as it is */
#define QRF_BLOB_Sql     2 /* Quote as an SQL literal */
#define QRF_BLOB_Hex     3 /* Hexadecimal representation */
#define QRF_BLOB_Tcl     4 /* "\000" notation */
#define QRF_BLOB_Json    5 /* A JSON string */

/*
** Control-character escape modes.
** Allowed values for sqlite3_qrf_spec.eEsc
*/
#define QRF_ESC_Auto    0 /* Choose the ctrl-char escape automatically */
#define QRF_ESC_Off     1 /* Do not escape control characters */
#define QRF_ESC_Ascii   2 /* Unix-style escapes.  Ex: U+0007 shows ^G */
#define QRF_ESC_Symbol  3 /* Unicode escapes. Ex: U+0007 shows U+2407 */

/*
** Allowed values for "boolean" fields, such as "bColumnNames", "bWordWrap",
** and "bTextJsonb".  There is an extra "auto" variants so these are actually
** tri-state settings, not booleans.
*/
#define QRF_SW_Auto     0 /* Let QRF choose the best value */
#define QRF_SW_Off      1 /* This setting is forced off */
#define QRF_SW_On       2 /* This setting is forced on */
#define QRF_Auto        0 /* Alternate spelling for QRF_*_Auto */
#define QRF_No          1 /* Alternate spelling for QRF_SW_Off */
#define QRF_Yes         2 /* Alternate spelling for QRF_SW_On */

/*
** Possible alignment values alignment settings
**
**                             Horizontal   Vertial
**                             ----------   --------  */
#define QRF_ALIGN_Auto    0 /*   auto        auto     */
#define QRF_ALIGN_Left    1 /*   left        auto     */
#define QRF_ALIGN_Center  2 /*   center      auto     */
#define QRF_ALIGN_Right   3 /*   right       auto     */
#define QRF_ALIGN_Top     4 /*   auto        top      */
#define QRF_ALIGN_NW      5 /*   left        top      */
#define QRF_ALIGN_N       6 /*   center      top      */
#define QRF_ALIGN_NE      7 /*   right       top      */
#define QRF_ALIGN_Middle  8 /*   auto        middle   */
#define QRF_ALIGN_W       9 /*   left        middle   */
#define QRF_ALIGN_C      10 /*   center      middle   */
#define QRF_ALIGN_E      11 /*   right       middle   */
#define QRF_ALIGN_Bottom 12 /*   auto        bottom   */
#define QRF_ALIGN_SW     13 /*   left        bottom   */
#define QRF_ALIGN_S      14 /*   center      bottom   */
#define QRF_ALIGN_SE     15 /*   right       bottom   */
#define QRF_ALIGN_HMASK   3 /* Horizontal alignment mask */
#define QRF_ALIGN_VMASK  12 /* Vertical alignment mask */

/*
** Auxiliary routines contined within this module that might be useful
** in other contexts, and which are therefore exported.
*/
/*
** Return an estimate of the width, in columns, for the single Unicode
** character c.  For normal characters, the answer is always 1.  But the
** estimate might be 0 or 2 for zero-width and double-width characters.
**
** Different display devices display unicode using different widths.  So
** it is impossible to know that true display width with 100% accuracy.
** Inaccuracies in the width estimates might cause columns to be misaligned.
** Unfortunately, there is nothing we can do about that.
*/
int sqlite3_qrf_wcwidth(int c);




#endif /* !defined(SQLITE_QRF_H) */
