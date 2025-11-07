# SQLite Query Result Formatting Subsystem

The "Query Result Formatter" or "QRF" subsystem is a C-language
subroutine that formats the output from an SQLite query.  The
output format is configurable.  The application can request CSV,
or a table, or any of several other formats, according to needs.

For the first 25 years of SQLite's existance, the
[command-line interface](https://sqlite.org/cli.html) (CLI)
formatted query results using a hodge-podge of routines
that had grown slowly by accretion.  The QRF was created
in fall of 2025 to refactor and reorganize this code into
a more usable form. The idea behind QRF is to implement all the
query result formatting capabilities of the CLI in a subroutine
that can be incorporated and reused by other applications.

## 1.0 Overview Of Operation

Suppose `pStmt` is a pointer to an SQLite prepared statement
(a pointer to an `sqlite3_stmt` object) that has been reset and
bound and is ready to run.  Then to format the output from this
prepared statement, use code similar to the following:

> ~~~
sqlite3_qrf_spec spec;  /* Format specification */
char *zErrMsg;          /* Text error message (optional) */
char *zResult = 0;      /* Formatted output written here */
int rc;                 /* Result code */

memset(&spec, 0, sizeof(spec));       /* Initialize the spec */
spec.iVersion = 1;                    /* Version number must be 1 */
spec.pzOutput = &zResult;             /* Write results in variable zResult */
/* Optionally fill in other settings in spec here, as needed */
zErrMsg = 0;                          /* Not required; just being pedantic */
rc = sqlite3_format_query_result(pStmt, &spec, &zErrMsg); /* Format results */
if( rc ){
  printf("Error (%d): %s\n", rc, zErrMsg);  /* Report an error */
  sqlite3_free(zErrMsg);              /* Free the error message text */
}else{
  printf("%s", zResult);              /* Report the results */
  sqlite3_free(zResult);              /* Free memory used to hold results */
}
~~~

The `sqlite3_qrf_spec` object describes the desired output format
and what to do with the generated output. Most of the work in using
the QRF involves filling out the sqlite3_qrf_spec.

## 2.0 The `sqlite3_qrf_spec` object

The `sqlite3_qrf_spec` structure defines how the results of a query
are to be formatted, and what to do with the formatted text.  The
most recent definition of `sqlite3_qrf_spec` is as follows:

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
  short int mxColWidth;       /* Maximum width of any individual column */
  short int mxTotalWidth;     /* Maximum overall table width */
  int mxLength;               /* Maximum content to display per element */
  int nWidth;                 /* Number of column width parameters */
  short int *aWidth;          /* Column widths */
  const char *zColumnSep;     /* Alternative column separator */
  const char *zRowSep;        /* Alternative row separator */
  const char *zTableName;     /* Output table name */
  const char *zNull;          /* Rendering of NULL */
  char *(*xRender)(void*,sqlite3_value*);                /* Render a value */
  sqlite3_int64 (*xWrite)(void*,const unsigned char*,sqlite3_int64);
  void *pRenderArg;           /* First argument to the xRender callback */
  void *pWriteArg;            /* First argument to the xWrite callback */
  char **pzOutput;            /* Storage location for output string */
  /* Additional fields may be added in the future */
};
~~~

The `sqlite3_qrf_spec` object must be fully initialized prior
to calling `sqlite3_format_query_result()`.  Initialization can be mostly
accomplished using memset() to zero the spec object, as most fields
use a zero value as a reasonable default.  However, the iVersion
field must be set to 1 and one of either the pzOutput or xWrite fields
must be initialized to a non-NULL value to tell QRF where to send
its output.

### 2.1 Structure Version Number

The sqlite3_qrf_spec.iVersion field must be 1.  Future enhancements to 
the QRF might add new fields onto the bottom of the sqlite3_qrf_spec
object. Those new fields will only be accessible if the iVersion is greater
than 1. Thus the iVersion field is used to support upgradability.

### 2.2 Output Deposition (xWrite and pzOutput)

The formatted output can either be sent to a callback function
or accumulated into an output buffer in memory obtained
from sqlite3_malloc().  If the sqlite3_qrf_spec.xWrite column is not NULL,
then that function is invoked (using sqlite3_qrf_spec.xWriteArg as its
first argument) to transmit the formatted output.  Or, if
sqlite3_qrf_spec.pzOutput points to a pointer to a character, then that
pointer is made to point to memory obtained from sqlite3_malloc() that
contains the complete text of the formatted output.  If spec.pzOutput\[0\]
is initially non-NULL, then it is assumed to point to memory obtained
from sqlite3_malloc().  In that case, the buffer is resized using
sqlite3_realloc() and the new text is appended.

One of either sqlite3_qrf_spec.xWrite and sqlite3_qrf_spec.pzOutput must be
non-NULL and the other must be NULL.

### 2.3 Output Format

The sqlite3_qrf_spec.eStyle field is an integer code that defines the
specific output format that will be generated.  See section 4.0 below
for details on the meaning of the various style options.

Other fields in sqlite3_qrf_spec might be used or might be
ignored, depending on the value of eStyle.

### 2.4 Show Column Names (bColumnNames)

The sqlite3_qrf_spec.bColumnNames field can be either QRF_SW_Auto,
QRF_SW_On, or QRF_SW_Off.

> ~~~
#define QRF_SW_Auto     0 /* Let QRF choose the best value */
#define QRF_SW_Off      1 /* This setting is forced off */
#define QRF_SW_On       2 /* This setting is forced on */
~~~

If the value is QRF_SW_On, then column names appear in the output.
If the value is QRF_SW_Off, column names are omitted.  If the
value is QRF_SW_Auto, then an appropriate default is chosen.

### 2.5 Control Character Escapes (eEsc)

The sqlite3_qrf_spec.eEsc determines how ASCII control characters are
formatted when displaying TEXT values in the result.  These are the allowed
values:

> ~~~
#define QRF_ESC_Auto    0 /* Choose the ctrl-char escape automatically */
#define QRF_ESC_Off     1 /* Do not escape control characters */
#define QRF_ESC_Ascii   2 /* Unix-style escapes.  Ex: U+0007 shows ^G */
#define QRF_ESC_Symbol  3 /* Unicode escapes. Ex: U+0007 shows U+2407 */
~~~

If the value of eEsc is QRF_ESC_Ascii, then the control character
with value X is displayed as ^Y where Y is X+0x40.  Hence, a
backspace character (U+0008) is shown as "^H".

If eEsc is QRF_ESC_Symbol, then control characters in the range of U+0001
through U+001f are mapped into U+2401 through U+241f, respectively.

If the value of eEsc is QRF_ESC_Off, then no translation occurs
and control characters that appear in TEXT strings are transmitted
to the formatted output as-is.  This can be dangerous in applications,
since an adversary who can control TEXT values might be able to
inject ANSI cursor movement sequences to hide nefarious values.

The QRF_ESC_Auto value for eEsc means that the query result formatter
gets to pick whichever control-character encoding it thinks is best for
the situation.  This will usually be QRF_ESC_Ascii.

The TAB (U+0009), LF (U+000a) and CR-LF (U+000d,U+000a) character
sequence are always output literally and are not mapped to alternative
display values, regardless of this setting.

### 2.6 Display of TEXT values (eText)

The sqlite3_qrf_spec.eText field can have one of the following values:

> ~~~
#define QRF_TEXT_Auto    0 /* Choose text encoding automatically */
#define QRF_TEXT_Off     1 /* Literal text */
#define QRF_TEXT_Sql     2 /* Quote as an SQL literal */
#define QRF_TEXT_Csv     3 /* CSV-style quoting */
#define QRF_TEXT_Html    4 /* HTML-style quoting */
#define QRF_TEXT_Tcl     5 /* C/Tcl quoting */
#define QRF_TEXT_Json    6 /* JSON quoting */
~~~

A value of QRF_TEXT_Auto means that the query result formatter will choose
what it thinks will be the best text encoding.

A value of QRF_TEXT_Off means that text values appear in the output exactly
as they are found in the database file, with no translation.

A value of QRF_TEXT_Sql means that text values are escaped so that they
look like SQL literals.  That means the value will be surrounded by
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

If the sqlite3_qrf_spec.bTextJsonb flag is QRF_SW_On and if the value to be
displayed is JSONB, then the JSONB is translated into text JSON and the
text is shown according to the sqlite3_qrf_spec.eText setting as
described in the previous section.

If the bTextJsonb flag is QRF_SW_Off (the usual case) or if the BLOB value to
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

### 2.8 Maximum displayed content length (mxLength)

If the sqlite3_qrf_spec.mxLength setting is non-zero, then the formatter
*attempts* to show only the first mxLength characters of each value.
This limit is approximate.  The content length might exceed the limit
by a few characters, especially if the limit is very small.

Content length limits only apply to TEXT and BLOB values.  Numeric
values and NULLs always display their full text regardless of the
mxLength setting.

*This setting is a place-holder.
As for 2025-11-07, the mxLength constraint is not yet implemented.
The current behavior is always as if mxLength where zero.*

### 2.9 Word Wrapping In Columnar Modes (mxColWidth, mxTotalWidth, bWordWrap)

When using columnar formatting modes (QRF_STYLE_Box, QRF_STYLE_Column,
QRF_STYLE_Markdown, or QRF_STYLE_Table), the formatter attempts to limit
the width of any individual column to sqlite3_qrf_spec.mxColWidth characters
if mxColWidth is non-zero.  A zero value for mxColWidth means "unlimited".
The formatter also attempts to limit the width of the entire table to
no more than sqlite3_qrf_spec.mxTotalWidth characters.  Again, a zero
value means "no-limit".

The mxColWidth limit might be exceeded if the limit is very small.
The mxTotalWidth is "best effort"; the formatter might go significantly
beyond the mxTotalWidth if the table has too many columns
to squeeze into the specified space.

In order to keep individual columns, or the entire table, within
requested width limits, it is sometimes necessary to wrap the content
for a single row of a single column across multiple lines.  When this
becomes necessary and if the bWordWrap setting is QRF_SW_On, then the
formatter attempts to split the content on whitespace or at a word boundary.
If bWordWrap is QRF_SW_Off, then the formatter is free to split content
anywhere, including in the middle of a word.

For narrow columns and wide words, it might sometimes be necessary to split
a column in the middle of a word, even when bWordWrap is QRF_SW_On.

*The mxTotalWidth setting is a place-holder.
As for 2025-11-07, the mxTotalWidth constraint is not yet implemented.
The current behavior is always as if mxTotalWidth where zero.*


### 2.10 Column width and alignment (nWidth and aWidth)

The sqlite3_qrf_spec.aWidth field is a pointer to an array of
signed 16-bit integers that control column widths and alignments
in columnar output modes (QRF_STYLE_Box, QRF_STYLE_Column,
QRF_STYLE_Markdown, or QRF_STYLE_Table).  The sqlite3_qrf_spec.nWidth
field is the number of integers in the aWidth array.

If aWidth is a NULL pointer or if nWidth is zero, then the array is
assumed to be all zeros.  If nWidth is less then the number of
columns in the output, then zero is used for the width/alignment
setting for all columns past then end of the aWidth array.

The aWidth array is deliberately an array of 16-bit signed integers.
Only 16 bits are used because no good comes for having very large
column widths. In fact, the interface defines several key width values:

> ~~~
#define QRF_MX_WIDTH    (10000)
#define QRF_MN_WIDTH    (-10000)
#define QRF_MINUS_ZERO  (-32768)    /* Special value meaning -0 */
~~~

A width less then QRF_MN_WIDTH is interpreted as QRF_MN_WIDTH and a width
larger than QRF_MX_WIDTH is interpreted as QRF_MX_WIDTH.  Thus the maximum
width of a column is 10000 characters, which is far wider than any
human-readable column should be.

Any aWidth\[\] value of zero means the formatter should use a flexible
width column (limited only by sqlite_qrf_spec.mxWidth) that is just
big enough to hold the largest row, and the all rows should be left-justifed.
The special value of QRF_MINUS_ZERO is interpreted as "-0" and work like
zero, except that shorter rows are right-justifed instead of left-justifed.

Values of aWidth that are not zero (and not QRF_MINUS_ZERO) determine the
width of the corresponding column.  Negative values are used for
right-justified columns and positive values are used for left-justified
columns.

### 2.11 Row and Column Separator Strings

The sqlite3_qrf_spec.zColumnSep and sqlite3_qrf_spec.zRowSep strings
are alternative column and row separator character sequences.  If not
specified (if these pointers are left as NULL) then appropriate defaults
are used.  Some output styles have hard-coded column and row separators
and these settings are ignored for those styles.

### 2.12 The Output Table Name

The sqlite3_qrf_spec.zTableName value is the name of the output table
when eStyle is QRF_STYLE_Insert.

### 2.13 The Rendering Of NULL

If a value is NULL then show the NULL using the string
found in sqlite3_qrf_spec.zNull.  If zNull is itself a NULL pointer
then NULL values are rendered as an empty string.

### 2.14 Optional Value Rendering Callback

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

## 4.0 Output Styles

The result formatter supports a variety of output styles. The
output style used is determined by the eStyle setting of the
sqlite3_qrf_spec object. The set of supported output modes
might increase in future versions.
The following output modes are currently defined:

> ~~~
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
~~~

In the following subsections, these styles will often be referred 
to without the "QRF_STYLE_" prefix.

### 4.1 Default Style (Auto)

The **Auto** style means QRF gets to choose an appropriate output
style.  It will usually choose **Box**, but might also pick one of
**Explain** or **Eqp** if the `sqlite3_stmt_explain()` function
returns 1 or 2, respectively.

### 4.2 Columnar Styles (Box, Column, Markdown, Table)

The **Box**, **Column**, **Markdown**, and **Table**
modes are columnar.  This means the output is arranged into neat,
uniform-width columns.  These styles can use more memory, especially when
the query result has many rows, because they need to load the entire output
into memory first in order to determine how wide to make each column.

The nWidth, aWidth, and mxWidth fields of the `sqlite3_qrf_spec` object
are used by these styles only, and are ignored by all other styles.
The zRowSep and zColumnSep settings are ignored by these styles.  The
bColumnNames setting is honored by these styles; it defaults to QRF_SW_On.

The **Box** style uses Unicode box-drawing character to draw a grid
of columns and rows to show the result.  The **Table** is the same,
except that it uses ASCII-art rather than Unicode box-drawing characters
to draw the grid. The **Column** arranges the results in neat columns
but does not draw in column or row separator, except that it does draw
lines horizontal lines using "`-`" characters to separate the column names
from the data below.  This is very similar to default output styling in
psql.  The **Markdown** renders its result in the
Markdown table format.

### 4.3 Line-oriented Styles

The line-oriented styles output each row of result as it is received from
the prepared statement.

The **List** style is the most familiar line-oriented output format.
The **List** style shows output columns for each row on the
same line, each separated by a single "`|`" character and with lines
terminated by a single newline (\\u000a or \\n).  These column
and row separator choices can be overridden using the zColumnSep
and zRowSep fields of the `sqlite3_qrf_spec` structure.  The text
formatting is QRF_TEXT_Off, and BLOB encoding is QRF_BLOB_Text.  So
characters appear in the output exactly as they appear in the database.
Except the eEsp mode defaults to `QRF_ESC_On`, so that control
characters are escaped, for safety.

The **Csv** and **Quote** styles are simply variations on **List**
with different defaults.
**Csv** sets things up to generate valid CSV file output.
**Quote** displays a comma-separated list of SQL
value literals.

The **Html** style generates HTML table content, just without
the `<TABLE>..</TABLE>` around the outside.

The **Insert** style generates a series of SQL "INSERT" statements
that will inserts the data that is output into a table whose name is defined
by the zTableName field of `sqlite3_qrf_spec`.  If zTableName is NULL,
then a substitute name is used.

The **Json** and **JsonLine** styles generates JSON text for the query result.
The **Json** style produces a JSON array of structures with on 
structure per row.  **JsonLine** outputs independent JSON objects, one per
row, with each structure on a separate line all by itself, and not
part of a larger array.

Finally, the **Line** style paints each column of a row on a
separate line with the column name on the left and a "`=`" separating the
column name from its value.  A single blank line appears between rows.

### 4.4 EXPLAIN Styles (Eqp, Explain)

The **Eqp** and **Explain** styles format output for
EXPLAIN QUERY PLAN and EXPLAIN statements, respectively.  If the input
statement is not already an EXPLAIN QUERY PLAN or EXPLAIN statement is
is temporarily converted for the duration of the rendering, but
is converted back before `sqlite3_format_query_result()` returns.

### 4.5 ScanStatus Styles (Stats, StatsEst, StatsVm)

The **Stats**, **StatsEst**, and **StatsVm** styles are similar to **Eqp**
and **Explain** except that they include profiling information
from prior executions of the input prepared statement.
These modes only work if SQLite has been compiled with
-DSQLITE_ENABLE_STMT_SCANSTATUS and if the SQLITE_DBCONFIG_STMT_SCANSTATUS
is enabled for the database connection.  The **StatsVm** style
also requires the bytecode() virtual table which is enabled using
the -DSQLITE_ENABLE_BYTECODE_VTAB compile-time option.

### 4.6 Other Styles (Count, Off)

The **Count** style discards all query results and returns
a count of the number of rows of output at the end.  The **Off**
style is completely silent; it generates no output.  These corner-case
modes are sometimes useful for debugging.

### 5.0 Source Code Files

The SQLite Query Result Formatter is implemented in three source code files:

   *  `qrf.c` &rarr;  The implementation, written in portable C99
   *  `qrf.h` &rarr;  A header file defining interfaces
   *  `README.md` &rarr;  This documentation, in Markdown

To use the SQLite result formatter, include the "`qrf.h`" header file
and link the application against the "`qrf.c`" source file.
