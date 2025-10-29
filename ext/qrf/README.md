# SQLite Query Result Formatting Subsystem

The "Query Result Formatter" or "QRF" subsystem is a C-language
subroutine that formats the output from an SQLite query.  The
output format is configurable.  The application can request CSV,
or a table, or any of several other formats, according to needs.

## 1.0 Overview Of Operation

Suppose `pStmt` is a pointer to an SQLite prepared statement
(a pointer to an `sqlite3_stmt` object) that has been reset and
bound and is ready to run.  Then to format the output from this
prepared statement, use code similar to the following:

> ~~~
sqlite3_qrf_spec spec;  /* Format specification */
char *zErrMsg;          /* Text error message (optional) */
int rc;                 /* Result code */

memset(&spec, 0, sizeof(spec));
// Fill out the spec object to describe the desired output format
zErrMsg = 0;
rc = sqlite3_format_query_result(pStmt, &spec, &zErrMsg);
if( rc ) printf("Error (%d): %s\n", rc, zErrMsg);
sqlite3_free(zErrMsg);
~~~

The `sqlite3_qrf_spec` object describes the desired output format
and what to do with the generated output. Most of the work in using
the QRF involves filling out the sqlite3_qrf_spec.

## 2.0 The `sqlite3_qrf_spec` object

This structure defines how the results of a query are to be
formatted, and what to do with the formatted text.

> ~~~
typedef struct sqlite3_qrf_spec sqlite3_qrf_spec;
struct sqlite3_qrf_spec {
  unsigned char iVersion;     /* Version number of this structure */
  unsigned char eStyle;       /* Formatting style.  "box", "csv", etc... */
  unsigned char eEsc;         /* How to escape control characters in text */
  unsigned char eText;        /* Quoting style for text */
  unsigned char eBlob;        /* Quoting style for BLOBs */
  unsigned char bColumnNames; /* True to show column names */
  unsigned char bWordWrap;    /* Try to wrap on word boundaries */
  unsigned char bTextJsonb;   /* Render JSONB blobs as JSON text */
  short int mxWidth;          /* Maximum width of any column */
  int nWidth;                 /* Number of column width parameters */
  short int *aWidth;          /* Column widths */
  const char *zColumnSep;     /* Alternative column separator */
  const char *zRowSep;        /* Alternative row separator */
  const char *zTableName;     /* Output table name */
  const char *zNull;          /* Rendering of NULL */
  char *(*xRender)(void*,sqlite3_value*);                /* Render a value */
  ssize_t (*xWrite)(void*,const unsigned char*,size_t);  /* Write callback */
  void *pRenderArg;           /* First argument to the xRender callback */
  void *pWriteArg;            /* First argument to the xWrite callback */
  char **pzOutput;            /* Storage location for output string */
  /* Additional fields may be added in the future */
};
~~~

The sqlite3_qrf_spec object must be fully initialized prior
to calling `sqlite3_format_query_result()`.

### 2.1 Structure Version Number

The sqlite3_qrf_spec.iVersion field must be 1.  Future enhancements to 
the QRF might add new fields onto the bottom of the sqlite3_qrf_spec
object. Those new fields will only be accessible if the iVersion is greater
than 1. Thus the iVersion field is used to support upgradability.

### 2.2 Output Deposition (xWrite and pzOutput)

The formatted output can either be sent to a callback function
or accumulated into an output buffer in memory obtained
from system malloc().  If the sqlite3_qrf_spec.xWrite column is not NULL,
then that function is invoked (using sqlite3_qrf_spec.xWriteArg as its
first argument) to transmit the formatted output.  Or, if
sqlite3_qrf_spec.pzOutput points to a pointer to a character, then that
pointer is made to point to memory obtained from sqlite3_malloc() that
contains the complete text of the formatted output.

One of sqlite3_qrf_spec.xWrite and sqlite3_qrf_spec.pzOutput must be
non-NULL and the other must be NULL.

### 2.3 Output Format

The sqlite3_qrf_spec.eStyle field is an integer code that defines the
specific output format that will be generated.  See the
output format describes below for additional detail.

Other fields in sqlite3_qrf_spec may be used or may be
ignored, depending on the value of eStyle.

### 2.4 Show Column Names (bColumnNames)

The sqlite3_qrf_spec.bColumnNames field is a boolean.  If true, then column
names appear in the output.  If false, column names are omitted.

### 2.5 Control Character Escapes (eEsc)

The sqlite3_qrf_spec.eEsc determines how ASCII control characters are
formatted when displaying TEXT values in the result.  These are the allowed
values:

> ~~~
#define QRF_ESC_Ascii   0 /* Unix-style escapes.  Ex: U+0007 shows ^G */
#define QRF_ESC_Symbol  1 /* Unicode escapes. Ex: U+0007 shows U+2407 */
#define QRF_ESC_Off     2 /* Do not escape control characters */
~~~

If the value of eEsc is zero, then the control character
with value X is displayed as ^Y where Y is X+0x40.  Hence, a
backspace character (U+0008) is shown as "^H".  This is the
default.

If eEsc is one, then control characters in the range of U+0001
through U+001f are mapped into U+2401 through U+241f, respectively.

If the value of eEsc is two, then no translation occurs
and control characters that appear in TEXT string are transmitted
to the formatted output as-is.  This an be dangerous in applications,
since an adversary who can control TEXT values might be able to
inject ANSI cursor movement sequences to hide nefarious values.

The TAB (U+0009), LF (U+000a) and CR-LF (U+000d,U+000a) character
sequence are always output literally and are not mapped to alternative
display values, regardless of this setting.

### 2.6 Display of TEXT values (eText)

The sqlite3_qrf_spec.eText field can have one of the following values:

> ~~~
#define QRF_TEXT_Off     0 /* Literal text */
#define QRF_TEXT_Sql     1 /* Quote as an SQL literal */
#define QRF_TEXT_Csv     2 /* CSV-style quoting */
#define QRF_TEXT_Html    3 /* HTML-style quoting */
#define QRF_TEXT_Tcl     4 /* C/Tcl quoting */
#define QRF_TEXT_Json    5 /* JSON quoting */
~~~

A value of QRF_TEXT_Off means that text value appear in the output exactly
as they are found in the database file, with no translation.

A value of QRF_TEXT_Sql means that text values are escaped so that they
appears as SQL literals.  That means the value will be surrounded by
single-quotes (U+0027) and any single-quotes contained within the text
will be doubled.

A value of QRF_TEXT_Csv means that text values are escaped in accordance
with RFC&nbsp;4180, which defines Comma-Separated-Value or CSV files.
Text strings that contain no special values appears as-is.  Text strings
that contain special values are contained in double-quotes (U+0022) and
any double-quotes within the value are doubled.

A value of QRF_TEXT_Html means that text values are escaped for use in
HTML.  Special characters "&lt;", "&amp;", "&gt;", "&quot;", and "&#39;"
are displayed as "&amp;lt;", "&amp;amp;", "&amp;gt;", "&amp;quot;",
and "&amp;#39;", respectively.

A value of QRF_TEXT_Tcl means that text values are displayed inside of
double-quotes and special characters within the string are escaped using
backslash escape, as in ANSI-C or TCL or Perl or other popular programming
languages.

A value of QRF_TEXT_Json gives similar results as QRF_TEXT_Tcl except that the
rules are adjusted so that the displayed string is strictly conforming
the JSON specification.

### 2.7 How to display BLOB values (eBlob and bTextJsonb)

If the sqlite3_qrf_spec.bTextJsonb flag is true and if the value to be
displayed is JSONB, then the JSONB is translated into text JSON and the
text is shown according to the sqlite3_qrf_spec.eText setting as
described in the previous section.

If the bTextJsonb flag is false (the usual case) or if the BLOB value to
be displayed is not JSONB, then the sqlite3_qrf_spec.eBlob field determines
how the BLOB value is formatted.  The following options are available;

> ~~~
#define QRF_BLOB_Auto    0 /* Determine BLOB quoting using eText */
#define QRF_BLOB_Text    1 /* Display content exactly as it is */
#define QRF_BLOB_Sql     2 /* Quote as an SQL literal */
#define QRF_BLOB_Hex     3 /* Hexadecimal representation */
#define QRF_BLOB_Tcl     4 /* "\000" notation */
#define QRF_BLOB_Json    5 /* A JSON string */
~~~

A value of QRF_BLOB_Auto means that display format is selected automatically
by sqlite3_format_query_result() based on eStyle and eText.

A value of QRF_BLOB_Text means that BLOB values are interpreted as UTF8
text and are displayed using formatting results set by eEsc and
eText.

A value of QRF_BLOB_Sql means that BLOB values are shown as SQL BLOB
literals: a prefix "`x'`" following by hexadecimal and ending with a
final "`'`".

A value of QRF_BLOB_Hex means that BLOB values are shown as pure
hexadecimal text with no delimiters.

A value of QRF_BLOB_Tcl means that BLOB values are shown as a
C/Tcl/Perl string literal where every byte is an octal backslash
escape.  So a BLOB of `x'052881f3'` would be displayed as
`"\005\050\201\363"`.

A value of QRF_BLOB_Json is similar to QRF_BLOB_Tcl except that is
uses unicode backslash escapes, since JSON does not understand 
the C/Tcl/Perl octal backslash escapes.  So the string from the
previous paragraph would be shown as
`"\u0005\u0028\u0081\u00f3"`.

### 2.8 Word Wrapping In Columnar Modes (mxWidth and bWordWrap)

When using columnar formatting modes (QRF_STYLE_Box, QRF_STYLE_Column,
QRF_STYLE_Markdown, or QRF_STYLE_Table) and with sqlite3_qrf_spec.mxWidth
set to some non-zero value, then when an output is two wide to be
displayed in just mxWidth standard character widths, the output is
split into multiple lines, where each line is a maximum of 
mxWidth characters wide.  "Width" hear means the actual displayed
width of the text on a fixed-pitch font.  The code takes into account
zero-width and double-width characters when comput the display width.

If the sqlite3_qrf_spec.bWordWrap flag is set, then the formatter
attempts to split lines at whitespace or word boundaries.
If the bWorkWrap flag is zero, then long lines can be split in the middle
of words.

### 2.9 Column width and alignment (nWidth and aWidth)

The sqlite3_qrf_spec.aWidth field is a pointer to an array of
signed 16-bit integers that control column widths and alignments
in columnar output modes (QRF_STYLE_Box, QRF_STYLE_Column,
QRF_STYLE_Markdown, or QRF_STYLE_Table).  The sqlite3_qrf_spec.nWidth
field is the number of integers in the aWidth array.

If aWidth is a NULL pointer or nWidth is zero, then the array is
assumed to be all zeros.  If nWidth is less then the number of
columns in the output, then zero is used for the width/alignment
setting for all columns past then end of the aWidth array.

The aWidth array is deliberately an array of 16-bit signed integers.
16-bit because no good comes for having very large column widths.
In fact, the interface defines several key width values:

> ~~~
#define QRF_MX_WIDTH    (10000)
#define QRF_MN_WIDTH    (-10000)
#define QRF_MINUS_ZERO  (-32768)    /* Special value meaning -0 */
~~~

A width less then QRF_MN_WIDTH is interpreted as QRF_MN_WIDTH and a width
larger than QRF_MX_WIDTH is interpreted as QRF_MX_WIDTH.  Thus the maximum
width of a column is 10000 characters, which is far wider than any
human-readable column should be.

And aWidth\[\] value of zero means the formatter should use a flexible
width column (limited only by sqlite_qrf_spec.mxWidth) that is just
big enough to hold the largest row, and the all rows should be left-justifed.
The special value of QRF_MINUS_ZERO is interpreted as "-0" and work like
zero, except that shorter rows a right-justifed instead of left-justifed.

Value of aWidth that are not zero (and not QRF_MINUS_ZERO) determine the
size of the column.  Negative values are used for right-justified columns
and positive values are used for left-justified columns.

### 2.10 Row and Column Separator Strings

The sqlite3_qrf_spec.zColumnSep and sqlite3_qrf_spec.zRowSep strings
are alternative column and row separator character sequences.  If not
specified (if these pointers are left as NULL) then appropriate defaults
are used.  Some output modes have hard-coded column and row separators
that cannot be overridden.

### 2.11 The Output Table Name

The sqlite3_qrf_spec.zTableName value is the name of the output table
when eStyle is QRF_STYLE_Insert.

### 2.12 The Rendering Of NULL

If a value is NULL and sqlite3_qrf_spec.xRender() does not exist
or declines to provide a rendering, then show the NULL using the string
found in sqlite3_qrf_spec.zNull.  If zNull is itself a NULL pointer
then NULL values are rendered as an empty string.

### 2.13 Optional Value Rendering Callback

If the sqlite3_qrf_spec.xRender field is not NULL, then each
sqlite3_value coming out of the query is first passed to the
xRender function, giving that function an opportunity to render
the results itself, using whatever custom format is desired.
If xRender chooses to render, it should write the rendering
into memory obtained from sqlite3_malloc() and return a pointer
to that memory.  The xRender function can decline
to render (for example, based on the sqlite3_value_type() or other
characteristics of the value) in which case it can simply return a
NULL pointer and the usual default rendering will be used instead.

The sqlite3_format_query_result() function (which calls xRender)
will take responsibility for freeing the string returned by xRender
after it has finished using it.

The eText, eBlob, and eEsc settings above become no-ops if the xRender
routine returns non-NULL.  In other words, the application-supplied
xRender routine is expected to do all of its own quoting and formatting.

## 3.0 The `sqlite3_format_query_result()` Interface

Invoke the `sqlite3_format_query_result(P,S,E)` interface to run
the prepared statement P and format its results according to the
specification found in S.  The sqlite3_format_query_result() function
will return an SQLite result code, usually SQLITE_OK, but perhaps
SQLITE_NOMEM or SQLITE_ERROR or similar.  If an error occurs and if
the E parameter is not NULL, then error message text might be written
into *E.  Any error message text will be stored in memory obtained
from sqlite3_malloc() and it is the responsibility of the caller to
free that memory by a subsequent call to sqlite3_free().

## 4.0 Output Modes

The result formatter supports a variety of output modes. The
output mode used is determined by the eStyle setting of the
sqlite3_qrf_spec object. The set of supported output modes
might increase in future versions.
The following output modes are currently defined:

> ~~~
#define QRF_STYLE_List      0 /* One record per line with a separator */
#define QRF_STYLE_Line      1 /* One column per line. */
#define QRF_STYLE_Html      2 /* Generate an XHTML table */
#define QRF_STYLE_Json      3 /* Output is a list of JSON objects */
#define QRF_STYLE_Insert    4 /* Generate SQL "insert" statements */
#define QRF_STYLE_Csv       5 /* Comma-separated-value */
#define QRF_STYLE_Quote     6 /* SQL-quoted, comma-separated */
#define QRF_STYLE_Explain   7 /* EXPLAIN output */
#define QRF_STYLE_ScanExp   8 /* EXPLAIN output with vm stats */
#define QRF_STYLE_EQP       9 /* Format EXPLAIN QUERY PLAN output */
#define QRF_STYLE_Markdown 10 /* Markdown formatting */
#define QRF_STYLE_Column   11 /* One record per line in neat columns */
#define QRF_STYLE_Table    12 /* MySQL-style table formatting */
#define QRF_STYLE_Box      13 /* Unicode box-drawing characters */
#define QRF_STYLE_Count    14 /* Output only a count of the rows of output */
#define QRF_STYLE_Off      15 /* No query output shown */
~~~

### 5.0 Source Code Files

The SQLite Query Result Formatter is implemented in three source code files:

   *  `qrf.c` &rarr;  The implementation, written in portable C99
   *  `qrf.h` &rarr;  A header file defining interfaces
   *  `qrf.md` &rarr;  This documentation, in Markdown

To use the SQLite result formatter, include the "`qrf.h`" header file
and link the application against the "`qrf.c`" source file.
