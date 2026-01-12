# SQLite Query Result Formatting Subsystem

The "Query Result Formatter" or "QRF" subsystem is a C-language
subroutine that formats the output from an SQLite query for display using
a fix-width font, for example on a terminal window over an SSH connection.
The output format is configurable.  The application can request various
table formats, with flexible column widths and alignments, row-oriented
formats, such as CSV and similar, as well as various special purpose formats
like JSON.

For the first 25 years of SQLite's existance, the
[command-line interface](https://sqlite.org/cli.html) (CLI)
formatted query results using a hodge-podge of routines
that had grown slowly by accretion.  The QRF was created
in fall of 2025 to refactor and reorganize this code into
a more usable form. The idea behind QRF is to implement all the
query result formatting capabilities of the CLI in a subroutine
that can be incorporated and reused by other applications.

## 1.0 Overview Of Operation

Suppose variable `sqlite3_stmt *pStmt` is a pointer to an SQLite
prepared statement that has been reset and bound and is ready to run.
Then to format the output from this prepared statement, use code
similar to the following:

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
}
sqlite3_free(zResult);              /* Free memory used to hold results */
~~~

The `sqlite3_qrf_spec` object describes the desired output format
and where to send the generated output. Most of the work in using
the QRF involves filling out the sqlite3_qrf_spec.

### 1.1 Using QRF with SQL text

If you start with SQL text instead of an sqlite3_stmt pointer, and
especially if the SQL text might comprise two or more statements, then
the SQL text needs to be converted into sqlite3_stmt objects separately.
If the original SQL text is in a variable `const char *zSql` and the
database connection is in variable `sqlite3 *db`, then code
similar to the following should work:

> ~~~
sqlite3_qrf_spec spec;  /* Format specification */
char *zErrMsg;          /* Text error message (optional) */
char *zResult = 0;      /* Formatted output written here */
sqlite3_stmt *pStmt;    /* Next prepared statement */
int rc;                 /* Result code */

memset(&spec, 0, sizeof(spec));       /* Initialize the spec */
spec.iVersion = 1;                    /* Version number must be 1 */
spec.pzOutput = &zResult;             /* Write results in variable zResult */
/* Optionally fill in other settings in spec here, as needed */
zErrMsg = 0;                          /* Not required; just being pedantic */
while( zSql && zSql[0] ){
  pStmt = 0;                          /* Not required; just being pedantic */
  rc = sqlite3_prepare_v2(db, zSql, -1, &pStmt, &zSql);
  if( rc!=SQLITE_OK ){
    printf("Error: %s\n", sqlite3_errmsg(db));
  }else{
    rc = sqlite3_format_query_result(pStmt, &spec, &zErrMsg); /* Get results */
    if( rc ){
      printf("Error (%d): %s\n", rc, zErrMsg);  /* Report an error */
      sqlite3_free(zErrMsg);              /* Free the error message text */
    }else{
      printf("%s", zResult);              /* Report the results */
      sqlite3_free(zResult);              /* Free memory used to hold results */
      zResult = 0;
    }
  }
  sqlite3_finalize(pStmt);
}
~~~

<a id="spec"></a>
## 2.0 The `sqlite3_qrf_spec` object

The `sqlite3_qrf_spec` looks like this:

> ~~~
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
  unsigned char eDfltAlign;   /* Default alignment, no covered by aAlignment */
  unsigned char eTitleAlign;  /* Alignment for column headers */
  unsigned char bSplitColumn; /* Wrap single-column output into many columns */
  unsigned char bBorder;      /* Show outer border in Box and Table styles */
  short int nWrap;            /* Wrap columns wider than this */
  short int nScreenWidth;     /* Maximum overall table width */
  short int nLineLimit;       /* Maximum number of lines for any row */
  short int nTitleLimit;      /* Maximum number of characters in a title */
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
~~~

Do not be alarmed by the complexity of this structure.  Everything can
be zeroed except for:

  *  `.iVersion`
  *  One of `.pzOutput` or `.xWrite`.

You do not need to understand and configure every field of this object
in order to use QRF effectively.  Start by zeroing out the whole structure,
then initializing iVersion and one of pzOutput or xWrite.  Then maybe
tweak one or two other settings to get the output you want.

Further detail on the meanings of each of the fields in the
`sqlite3_qrf_spec` object is in the subsequent sections.

### 2.1 Structure Version Number

The sqlite3_qrf_spec.iVersion field must be 1.  Future enhancements to 
the QRF might add new fields to the bottom of the sqlite3_qrf_spec
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
is initially non-NULL, then it is assumed to already point to memory obtained
from sqlite3_malloc().  In that case, the buffer is resized using
sqlite3_realloc() and the new text is appended.

One of either sqlite3_qrf_spec.xWrite and sqlite3_qrf_spec.pzOutput must be
non-NULL and the other must be NULL.

The return value from xWrite is an SQLITE result code.  The usual return
should be SQLITE_OK.  But if for some reason the write fails, a different
value might be returned.

### 2.3 Output Format

The sqlite3_qrf_spec.eStyle field is an integer code that defines the
specific output format that will be generated.  See [section 4.0](#style)
below for details on the meaning of the various style options.

Other fields in sqlite3_qrf_spec might be used or might be
ignored, depending on the value of eStyle.

### 2.4 Show Column Names (bTitles)

The sqlite3_qrf_spec.bTitles field can be either QRF_SW_Auto,
QRF_SW_On, or QRF_SW_Off.  Those three constants also have shorter
alternative spellings: QRF_Auto, QRF_No, and
QRF_Yes.

> ~~~
#define QRF_SW_Auto     0 /* Let QRF choose the best value */
#define QRF_SW_Off      1 /* This setting is forced off */
#define QRF_SW_On       2 /* This setting is forced on */
#define QRF_Auto        0 /* Alternate spelling for QRF_SW_Auto and others */
#define QRF_No          1 /* Alternate spelling for QRF_SW_Off */
#define QRF_Yes         2 /* Alternate spelling for QRF_SW_On */
~~~

If the value is QRF_Yes, then column names appear in the output.
If the value is QRF_No, column names are omitted.  If the
value is QRF_Auto, then an appropriate default is chosen.

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

### 2.6 Display of TEXT values (eText, eTitle)

The sqlite3_qrf_spec.eText controls how text values are rendered in the
display.  sqlite3_qrf_spec.eTitle controls how column names are rendered.
Both fields can have one of the following values:

> ~~~
#define QRF_TEXT_Auto    0 /* Choose text encoding automatically */
#define QRF_TEXT_Plain   1 /* Literal text */
#define QRF_TEXT_Sql     2 /* Quote as an SQL literal */
#define QRF_TEXT_Csv     3 /* CSV-style quoting */
#define QRF_TEXT_Html    4 /* HTML-style quoting */
#define QRF_TEXT_Tcl     5 /* C/Tcl quoting */
#define QRF_TEXT_Json    6 /* JSON quoting */
#define QRF_TEXT_Relaxed 7 /* Relaxed SQL quoting */
~~~

A value of QRF_TEXT_Auto means that the query result formatter will choose
what it thinks will be the best text encoding.

A value of QRF_TEXT_Plain means that text values appear in the output exactly
as they are found in the database file, with no translation.

A value of QRF_TEXT_Sql means that text values are escaped so that they
look like SQL literals.  That means the value will be surrounded by
single-quotes (U+0027) and any single-quotes contained within the text
will be doubled.

QRF_TEXT_Relaxed is similar to QRF_TEXT_Sql, except that it automatically
reverts to QRF_TEXT_Plain if the value to be displayed does not contain
special characters and is not easily confused with a NULL or a numeric
value.  QRF_TEXT_Relaxed strives to minimize the amount of quoting syntax
while keeping the result unambiguous and easy for humans to read.  The
precise rules for when quoting is omitted in QRF_TEXT_Relaxed, and when
it is applied, might be adjusted in future releases.

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
#define QRF_BLOB_Size    6 /* Display the blob size only */
~~~

A value of QRF_BLOB_Auto means that display format is selected automatically
by sqlite3_format_query_result() based on eStyle and eText.

A value of QRF_BLOB_Text means that BLOB values are interpreted as UTF8
text and are displayed using formatting results set by eEsc and
eText.

A value of QRF_BLOB_Sql means that BLOB values are shown as SQL BLOB
literals: a prefix "`x'`" following by hexadecimal and ending with a
final "`'`".

A value of QRF_BLOB_Hex means that BLOB values are shown as
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

A value of QRF_BLOB_Size does not show any BLOB content at all.
Instead, it substitutes a text string that says how many bytes
the BLOB contains.

### 2.8 Maximum size of displayed content (nLineLimit, nCharLimit, nTitleLimit)

If the sqlite3_qrf_spec.nCharLimit setting is non-zero, then the formatter
will display only the first nCharLimit characters of each value.
Only characters that take up space are counted when enforcing this
limit.  Zero-width characters and VT100 escape sequences do not count
toward this limit.  The count is in characters, not bytes.  When
imposing this limit, the formatter adds the three characters "..."
to the end of the value.  Those added characters are not counted
as part of the limit.  Very small limits still result in truncation,
but might render a few more characters than the limit.

If the sqlite3_qrf_spec.nLineLimit setting is non-zero, then the
formatter will only display the first nLineLimit lines of each value.
It does not matter if the value is split because it contains a newline
character, or if it split by wrapping.  This setting merely limits
the number of displayed lines.  The nLineLimit setting currently only 
works for **Box**, **Column**, **Line**, **Markdown**, and **Table**
styles, though that limitation might change in future releases.

The idea behind both of these settings is to prevent large renderings
when doing a query that (unexpectedly) contains very large text or
blob values: perhaps megabyes of text.

If the sqlite3_qrf_spec.nTitleLimit is non-zero, then the formatter
attempts to limits the size of column titles to at most nTitleLimit
display characters in width and a single line of text.  The nTitleLimit
is useful for queries that have result columns that are scalar
subqueries or complex expressions.  If those columns lack an AS
clause, then the name of the column will be a copy of the expression
that defines the column, which in some queries can be hundreds of
characters and multiple lines in length, which can reduce the readability
of tabular displays.  An nTitleLimit somewhere in the range of 10 to 20.
can improve readability.   The nTitleLimit setting currently only 
works for **Box**, **Column**, **Line**, **Markdown**, and **Table**
styles, though that limitation might change in future releases.

### 2.9 Word Wrapping In Columnar Styles (nWrap, bWordWrap)

When using columnar formatting modes (QRF_STYLE_Box, QRF_STYLE_Column,
QRF_STYLE_Markdown, or QRF_STYLE_Table), the formatter attempts to limit
the width of any individual column to sqlite3_qrf_spec.nWrap characters
if nWrap is non-zero.  A zero value for nWrap means "unlimited".
The nWrap limit might be exceeded if the limit is very small.

In order to keep individual columns within requested width limits,
it is sometimes necessary to wrap the content for a single row of
a single column across multiple lines.  When this
becomes necessary and if the bWordWrap setting is QRF_Yes, then the
formatter attempts to split the content on whitespace or at a word boundary.
If bWordWrap is QRF_No, then the formatter is free to split content
anywhere, including in the middle of a word.

For narrow columns and wide words, it might sometimes be necessary to split
a column in the middle of a word, even when bWordWrap is QRF_Yes.

### 2.10 Helping The Output To Fit On The Terminal (nScreenWidth)

The sqlite3_qrf_spec.nScreenWidth field can be set the number of
characters that will fit on one line on the viewer output device.
This is typically a number like 80 or 132.  The formatter will attempt
to reduce the length of output lines, depending on the style, so
that all output fits on that screen.

A value of zero for nScreenWidth means "unknown" or "no width limit".
When the value is zero, the formatter makes no attempt to keep the
lines of output short.

The nScreenWidth is a hint to the formatter, not a requirement.
The formatter trieds to keep lines below the nScreenWidth limit,
but it does not guarantee that it will.

The nScreenWidth field currently only makes a difference in
columnar styles (**Box**, **Column**, **Markdown**, and **Table**)
and in the **Line** style.

### 2.11 Individual Column Width (nWidth and aWidth)

The sqlite3_qrf_spec.aWidth field is a pointer to an array of
signed 16-bit integers that control the width of individual columns
in columnar output modes (QRF_STYLE_Box, QRF_STYLE_Column,
QRF_STYLE_Markdown, or QRF_STYLE_Table).  The sqlite3_qrf_spec.nWidth
field is the number of integers in the aWidth array.

If aWidth is a NULL pointer or if nWidth is zero, then the array is
assumed to be all zeros.  If nWidth is less then the number of
columns in the output, then zero is used for the width
for all columns past then end of the aWidth array.

The aWidth array is deliberately an array of 16-bit signed integers.
Only 16 bits are used because no good comes for having very large
column widths.  The range if further restricted as follows:

> ~~~
#define QRF_MAX_WIDTH  10000    /* Maximum column width */
#define QRF_MIN_WIDTH  0        /* Minimum column width */
~~~

A width greater than then QRF_MAX_WIDTH is interpreted as QRF_MAX_WIDTH.

Any aWidth\[\] value of zero means the formatter should use a flexible
width column (limited only by sqlite_qrf_spec.mxWidth) that is just
big enough to hold the largest row.

For historical compatibility, aWidth\[\] can contain negative values,
down to -QRF_MAX_WIDTH.  The column width used is the absolute value
of the number in aWidth\[\].  The only difference is that negative
values cause the default horizontal alignment to be QRF_ALIGN_Right.
The sign of the aWidth\[\] values only affects alignment if the
alignment is not otherwise specified by aAlign\[\] or eDfltAlign.
Again, negative values for aWidth\[\] entries are supported for
backwards compatibility only, and are not recommended for new
applications.

### 2.12 Alignment (nAlignment, aAlignment, eDfltAlign, eTitleAlign)

Some cells in a display table might contain a lot of text and thus
be wide, or they might contain newline characters or be wrapped by
width constraints so that they span many rows of text.  Other cells
might be narrower and shorter.  In columnar formats, the display width
of a cell is the maximum of the widest value in the same column, and the
display height is the height of the tallest value in the same row.
So some cells might be much taller and wider than necessary to hold
their values.

Alignment determines where smaller values are placed within larger cells.

The sqlite3_qrf_spec.aAlign field points to an array of unsigned characters
that specifies alignment (both vertical and horizontal) of individual
columns within the table.  The sqlite3_qrf_spec.nAlign fields holds
the number of entries in the aAlign\[\] array.

If sqlite3_qrf_spec.aAlign is a NULL pointer or if sqlite3_qrf_spec.nAlign
is zero, or for columns to the right of what are specified by
sqlite3_qrf_spec.nAlign, the sqlite3_qrf_spec.eDfltAlign value is used
for the alignment.  Column names can be (and often are) aligned
differently, as specified by sqlite3_qrf_spec.eTitleAlign.

Each alignment value specifies both vertical and horizontal alignment.
Horizontal alignment can be left, center, right, or no preference.
Vertical alignment can be top, middle, bottom, or no preference.
Thus there are 16 possible alignment values, as follows:

> ~~~
/*
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
~~~

Notice how alignment values with an unspecified horizontal
or vertical component can be added to another alignment value
for which that component is specified, to get a fully
specified alignment.  For eample:

> QRF_ALIGN_Center + QRF_ALIGN_Bottom == QRF_ALIGN_S.

The alignment for column names is always determined by the
eTitleAlign setting.  If eTitleAlign is QRF_Auto, then column
names use center-bottom alignment, QRF_ALIGN_W, value 14.
The aAlign\[\] and eDfltAlign settings have no affect on
column names.

For data in the first nAlign columns, the aAlign\[\] array
entry for that column takes precedence.  If either the horizontal
or vertical alignment has an "auto" value for that column or if
a column is beyond the first nAlign entries, then eDfltAlign
is used as a backup.  If neither aAlign\[\] nor eDfltAlign
specify a horizontal alignment, then values are right-aligned
(QRF_ALIGN_Right) if they are numeric and left-aligned
(QRF_ALIGN_Left) otherwise.  If neither aAlign\[\] nor eDfltAlign
specify a vertical alignment, then values are top-aligned
(QRF_ALIGN_Top).

*As of 2025-11-08, only horizontal alignment is implemented.
The vertical alignment settings are currently ignored and 
the vertical alignment is always QRF_ALIGN_Top.*

### 2.13 Row and Column Separator Strings

The sqlite3_qrf_spec.zColumnSep and sqlite3_qrf_spec.zRowSep strings
are alternative column and row separator character sequences.  If not
specified (if these pointers are left as NULL) then appropriate defaults
are used.  Some output styles have hard-coded column and row separators
and these settings are ignored for those styles.

### 2.14 The Output Table Name

The sqlite3_qrf_spec.zTableName value is the name of the output table
when eStyle is QRF_STYLE_Insert.

### 2.15 The Rendering Of NULL (zNull)

If a value is NULL then show the NULL using the string
found in sqlite3_qrf_spec.zNull.  If zNull is itself a NULL pointer
then NULL values are rendered as an empty string.

### 2.16 Optional Value Rendering Callback

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

The xRender routine is expected to do character length limiting itself.
So the nCharLimit setting becomes a no-op if xRender is used.  However
the nLineLimit setting is still applied.  The nTitleLimit setting is
not applicable to xRender because title values come from the
sqlite3_column_name() interface not from sqlite3_column_value(),
and so that names of columns are never processed by xRender.

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

<a id="style"></a>
## 4.0 Output Styles

The result formatter supports a variety of output styles. The
output style (sometimes called "output mode") is determined by
the eStyle field of the sqlite3_qrf_spec object. The set of
supported output modes might increase in future versions.
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
#define QRF_STYLE_JObject  10 /* Independent JSON objects for each row */
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
bTitles setting is honored by these styles; it defaults to QRF_SW_On.

The **Box** style uses Unicode box-drawing character to draw a grid
of columns and rows to show the result.  The **Table** is the same,
except that it uses ASCII-art rather than Unicode box-drawing characters
to draw the grid. The **Column** arranges the results in neat columns
but does not draw in column or row separator, except that it does draw
lines horizontal lines using "`-`" characters to separate the column names
from the data below.  This is very similar to default output styling in
psql.  The **Markdown** renders its result in the Markdown table format.

The **Box** and **Table** styles normally have a border that surrounds
the entire result.  However, if sqlite3_qrf_spec.bBorder is QRF_No, then
that border is omitted, saving a little space both horizontally and
vertically.

#### 4.2.1 Split Column Mode

If the bSplitColumn field is QRF_Yes, and eStyle is QRF_STYLE_Column,
and bTitles is QRF_No, and nScreenWidth is greater than zero, and if
the query only returns a single column, then a special rendering known
as "Split Column Mode" will be used.  In split column mode, instead
of showing all results in one tall column, the content wraps vertically
so that it appears on the screen as multiple columns, as many as will
fit in the available screen width.

### 4.3 Line-oriented Styles

The line-oriented styles output each row of result as it is received from
the prepared statement.

The **List** style is the most familiar line-oriented output format.
The **List** style shows output columns for each row on the
same line, each separated by a single "`|`" character and with lines
terminated by a single newline (\\u000a or \\n).  These column
and row separator choices can be overridden using the zColumnSep
and zRowSep fields of the `sqlite3_qrf_spec` structure.  The text
formatting is QRF_TEXT_Plain, and BLOB encoding is QRF_BLOB_Text.  So
characters appear in the output exactly as they appear in the database.
Except the eEsp mode defaults to `QRF_ESC_On`, so that control
characters are escaped, for safety.

The **Csv** and **Quote** styles are simply variations on **List**
with hard-coded values for some of the sqlite3_qrf_spec settings:

<table border=1 cellpadding=2 cellspacing=0>
<tr><th>&nbsp;<th>Quote<th>Csv
<tr><td>zColumnSep<td>","<td>","
<tr><td>zRowSep<td>"\\n"<td>"\\r\\n"
<tr><td>zNull<td>"NULL"<td>""
<tr><td>eText<td>QRF_TEXT_Sql<td>QRF_TEXT_Csv
<tr><td>eBlob<td>QRF_BLOB_Sql<td>QRF_BLOB_Text
</table>

The **Html** style generates HTML table content, just without
the `<TABLE>..</TABLE>` around the outside.

The **Insert** style generates a series of SQL "INSERT" statements
that will inserts the data that is output into a table whose name is defined
by the zTableName field of `sqlite3_qrf_spec`.  If zTableName is NULL,
then a substitute name is used.

The **Json** and **JObject** styles generates JSON text for the query result.
The **Json** style produces a JSON array of structures with one 
structure per row.  **JObject** outputs independent JSON objects, one per
row, with each structure on a separate line all by itself, and not
part of a larger array.  In both cases, the labels on the elements of the
JSON objects are taken from the column names of the SQL query.  So if
you have an SQL query that has two or more output columns with the same
name, you will end up with JSON structures that have duplicate elements.

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
   *  `README.md` &rarr;  This documentation

To use the SQLite result formatter, include the "`qrf.h`" header file
and link the application against the "`qrf.c`" source file.
