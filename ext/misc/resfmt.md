# SQLite Result Formatting Subsystem

The "resfmt" subsystem is a set of C-language subroutines that work
together to format the output from an SQLite query.  The output format
is configurable.  The application can request CSV, or a table, or
any of several other formats, according to needs.

## 1.0 Overview Of Operation

Suppose `pStmt` is a pointer to an SQLite prepared statement
(a pointer to an `sqlite3_stmt` object) that has been reset and
bound and is ready to run.  Then to format the output from this
prepared statement, use code similar to the following:

> ~~~
ResfmtSpec spec;     /* Formatter spec */
Resfmt *pFmt;        /* Formatter object */
int errCode;         /* Error code */
char *zErrMsg;       /* Text error message (optional) */

memset(&spec, 0, sizeof(spec));
// Additional spec initialization here
pFmt = sqlite3_resfmt_begin(pStmt, &spec);
while( SQLITE_ROW==sqlite3_step(pStmt) ){
  sqlite3_resfmt_row(pFmt, pStmt);
}
sqlite3_resfmt_finish(pFmt, &errCode, &zErrMsg);
// Do something with errcode and zErrMsg
sqlite3_free(zErrMsg);
~~~

The `ResfmtSpec` structure (defined below) describes the desired
output format.  The `pFmt` variable is a pointer to an opaque Resfmt
object that maintains the statement of the formatter.
The pFmt object is used as the first parameter to two other
routines, `sqlite3_resfmt_row()` and `sqlite3_resfmt_finish()`, and
is not usable for any other purpose by the caller.  The
`sqlite3_resfmt_finish()` interface serves as a destructor for
the pFmt object.

## 2.0 The `ResfmtSpec` object

A pointer to an instance of the following structure is the second
parameter to the `sqlite3_resfmt_begin()` interface.  This structure
defines how the rules of the statement are to be formatted.

> ~~~
typedef struct ResfmtSpec ResfmtSpec;
struct ResfmtSpec {
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
  int nWidth;                 /* Number of column width parameters */
  int *aWidth;                /* Column widths */
  ssize_t (*pWrite)(void*,const unsigned char*,ssize_t);  /* Write callback */
  void *pWriteArg;            /* First argument to write callback */
  char **pzOutput;            /* Storage location for output string */
  /* Additional fields may be added in the future */
};
~~~

The ResfmtSpec object must be fully initialized prior
to calling `sqlite3_resfmt_begin()` and its value must not change
by the application until after the corresponding call to
`sqlite3_resfmt_finish()`.  Note that the result formatter itself
might change values in the ResfmtSpec object as it runs.
But the application should not try to change or use any fields of
the ResfmtSpec object while the formatter is running.

### 2.1 Structure Version Number

The ResfmtSpec.iVersion field must be 1.  Future enhancements to this
subsystem might add new fields onto the bottom of the ResfmtSpec object.
Those new fields will only be accessible if the iVersion is greater than 1.
Thus the iVersion field is used to support upgradability.

### 2.2 Output Deposition

The formatted output can either be sent to a callback function
or accumulated into an output buffer in memory obtained
from system malloc().  If the ResfmtSpec.pWrite column is not NULL,
then that function is invoked (using ResfmtSpec.pWriteArg as its
first argument) to transmit the formatted output.  Or, if
ResfmtSpec.pzOutput points to a pointer to a character, then that
pointer is made to point to memory obtained from malloc() that
contains the complete text of the formatted output.

When `sqlite3_resfmt_begin()` is called,
one of ResfmtSpec.pWrite and ResfmtSpec.pzOutput must be non-NULL
and the other must be NULL.

Output might be generated row by row, on each call to
`sqlite3_resfmt_row()` or it might be written all at once
on the final call to `sqlite3_resfmt_finish()`, depending
on the output format.

### 2.3 Output Format

The ResfmtSpec.eFormat field is an integer code that defines the
specific output format that will be generated.  See the
output format describes below for additional detail.

### 2.4 Show Column Names

The ResfmtSpec.bShowCNames field is a boolean.  If true, then column
names appear in the output.  If false, column names are omitted.

### 2.5 Control Character Escapes

The ResfmtSpec.eEscMode determines how ASCII control characters are
formatted in the output.  If this value is zero, then the control character
with value X is displayed as ^Y where Y is X+0x40.  Hence, a
backspace character (U+0008) is shown as "^H".  This is the default.
If eEscMode is one, then control characters in the range of U+0001
through U+001f are mapped into U+2401 through U+241f, respectively.
If eEscMode is 2, then control characters are output directly, with
no translation.

The TAB (U+0009), LF (U+000a) and CR-LF (U+000d,U+000a) character
sequence are always output literally and are not mapped to alternative
display values, regardless of this setting.

### 2.6 Word Wrapping In Columnar Modes

For output modes that attempt to display equal-width columns, the
ResfmtSpec.bWordWrap boolean determines whether long values are broken
at word boundaries, or at arbitrary characters.  The ResfmtSpec.mxWidth
determines the maximum width of an output column.

### 2.7 Row and Column Separator Strings

The ResfmtSpec.zColumnSep and ResfmtSpec.zRowSep strings are alternative
column and row separator character sequences.  If not specified (if these
pointers are left as NULL) then appropriate defaults are used.

### 2.8 The Output Table Name

The ResfmtSpec.zTableName value is the name of the output table
when the MODE_Insert output mode is used.

### 2.9 Column Widths And Alignments

The ResfmtSpec.aWidth[] array, if specified, is an array of integers
that specify the minimum column width and the alignment for all columns
in columnar output modes.  Negative values mean right-justify.  The
absolute value is the minimum of the corresponding column.

The ResfmtSpec.nWidth field is the number of values in the aWidth[]
array.  Any column beyond the nWidth-th column are assumed to have
a minimum width of 0.

## 3.0 The `sqlite3_resfmt_begin()` Interface

Invoke the `sqlite3_resfmt_begin(P,S)` interface to begin formatting
the output of prepared statement P using format specification S.
This routine returns a pointer to an opaque Resfmt object that is
the current state of the formatter.  The `sqlite3_resfmt_finish()`
routine is the destructor for the Resfmt object and must be called
to prevent a memory leak.

If an out-of-memory fault occurs while allocating space for the
Resfmt object, then `sqlite3_resfmt_begin()` will return a NULL
pointer.  The application need not check for this case as the
other routines that use a pointer to the Resfmt object all
interpret a NULL parameter in place of the Resfmt pointer as
a harmless no-op.

## 4.0 The `sqlite3_resfmt_step()` Interface

Invoke the `sqlite3_resfmt_step(F,P)` interface for each row
in the prepared statement that is to be output.  The prepared
statement pointer P must be the same as the P argument passed
into `sqlite3_resfmt_begin()`, or unpredictable things can happen.

The formatter might choose to output some content as each row
is processed, or it might accumulate the output and send it all
at once when `sqlite3_resfmt_finish()` is called.  This is at
the discretion of the output formatter.  Generally, rows are
output one-by-one on each call to `sqlite3_resfmt_row()` when the
output format is such that the row can be computed without knowing
the value of subsequence, such as in CSV output mode, and
the output is accumulated and sent all at once in columnar output
modes where the complete output content is needed to compute column
widths.

### 5.0 The `sqlite3_resfmt_finish()` Interface

Invoke the `sqlite3_resfmt_finish(F,C,E)` interface to finish
the formatting.  C is an optional pointer to an integer.  If C
is not a NULL pointer, then any error code associated with the
formatting operation is written into *C.  E is an optional pointer
to a human-readable text error message.  If E is not a NULL pointer,
then an error message held in memory obtained from sqlite3_malloc()
is written into *E.  It is the responsibility of the calling
application to invoke sqlite3_free() on this error message to
reclaim the space.

### 6.0 Output Modes

The result formatter supports a variety of output modes.  The
set of supported output modes might increase in future versions.
The following output modes are currently defined:

> ~~~
#define MODE_Line      0 /* One column per line. */
#define MODE_Column    1 /* One record per line in neat columns */
#define MODE_List      2 /* One record per line with a separator */
#define MODE_Semi      3 /* Same as MODE_List but append ";" to each line */
#define MODE_Html      4 /* Generate an XHTML table */
#define MODE_Insert    5 /* Generate SQL "insert" statements */
#define MODE_Quote     6 /* Quote values as for SQL */
#define MODE_Tcl       7 /* Generate ANSI-C or TCL quoted elements */
#define MODE_Csv       8 /* Quote strings, numbers are plain */
#define MODE_Explain   9 /* Like MODE_Column, but do not truncate data */
#define MODE_Ascii    10 /* Use ASCII unit and record separators (0x1F/0x1E) */
#define MODE_Pretty   11 /* Pretty-print schemas */
#define MODE_EQP      12 /* Converts EXPLAIN QUERY PLAN output into a graph */
#define MODE_Json     13 /* Output JSON */
#define MODE_Markdown 14 /* Markdown formatting */
#define MODE_Table    15 /* MySQL-style table formatting */
#define MODE_Box      16 /* Unicode box-drawing characters */
#define MODE_Count    17 /* Output only a count of the rows of output */
#define MODE_Off      18 /* No query output shown */
#define MODE_ScanExp  19 /* Like MODE_Explain, but for ".scanstats vm" */
#define MODE_Www      20 /* Full web-page output */
~~~

Additional detail about the meaning of each of these output modes
is pending.

### 7.0 Source Code Files

The SQLite result formatter is implemented in three source code files:

   *  `resfmt.c` &rarr;  The implementation, written in portable C99
   *  `resfmt.h` &rarr;  A header file defining interfaces
   *  `resfmt.md` &rarr;  This documentation, in Markdown

To use the SQLite result formatter, include the "`resfmt.h`" header file
and link the application against the "`resfmt.c`" source file.
