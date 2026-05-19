/*
** 2026-04-13
**
** The author disclaims copyright to this source code.  In place of
** a legal notice, here is a blessing:
**
**    May you do good and not evil.
**    May you find forgiveness for yourself and forgive others.
**    May you share freely, never taking more than you give.
**
******************************************************************************
**
** Partial reimplement of the sqlite3_analyzer utility program as
** loadable SQL function.
*/
#include "sqlite3ext.h"
SQLITE_EXTENSION_INIT1
#include <assert.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

/*
** State information for the analysis
*/
typedef struct Analysis Analysis;
struct Analysis {
  sqlite3 *db;               /* Database connection */
  sqlite3_context *context;  /* SQL function context */
  sqlite3_str *pOut;         /* Write output here */
  char *zSU;                 /* Name of the temp.space_used table */
  const char *zSchema;       /* Schema to be analyzed */
};

/*
** Free all resources that the Analysis object references and
** reset the Analysis object.
**
** Call this routine multiple times on the same Analysis object
** is a harmless no-op, as long as the memory for the object itself
** has not been freed.
*/
static void analysisReset(Analysis *p){
  if( p->zSU ){
    char *zSql = sqlite3_mprintf("DROP TABLE temp.%s;", p->zSU);
    if( zSql ){
      sqlite3_exec(p->db, zSql, 0, 0, 0);
      sqlite3_free(zSql);
    }
  }
  sqlite3_str_free(p->pOut);
  sqlite3_free(p->zSU);
  memset(p, 0, sizeof(*p));
}

/*
** Report an error using formatted text.  If zFormat==NULL then report
** an OOM error.
*/
static void analysisError(Analysis *p, const char *zFormat, ...){
  char *zErr;
  if( zFormat ){
    va_list ap;
    va_start(ap, zFormat);
    zErr = sqlite3_vmprintf(zFormat, ap);
    va_end(ap);
  }else{
    zErr = 0;
  }
  if( zErr==0 ){
    sqlite3_result_error_nomem(p->context);
  }else{
    sqlite3_result_error(p->context, zErr, -1);
    sqlite3_free(zErr);
  }
  analysisReset(p);
}

/*
** Prepare and return an SQL statement.
*/
static sqlite3_stmt *analysisVPrep(Analysis *p, const char *zFmt, va_list ap){
  char *zSql;
  int rc;
  sqlite3_stmt *pStmt = 0;
  zSql = sqlite3_vmprintf(zFmt, ap);
  if( zSql==0 ){ analysisError(p,0); return 0; }
  rc = sqlite3_prepare_v2(p->db, zSql, -1, &pStmt, 0);
  if( rc ){
    analysisError(p, "SQL parse error: %s\nOriginal SQL: %s",
                                 sqlite3_errmsg(p->db), zSql);
    sqlite3_finalize(pStmt);
    analysisReset(p);
    pStmt = 0;
  }
  sqlite3_free(zSql);
  return pStmt;
}
static sqlite3_stmt *analysisPrepare(Analysis *p, const char *zFormat, ...){
  va_list ap;
  sqlite3_stmt *pStmt = 0;
  va_start(ap, zFormat);
  pStmt = analysisVPrep(p,zFormat,ap);
  va_end(ap);
  return pStmt;
}

/*
** If rc is something other than SQLITE_DONE or SQLITE_OK, then report
** an error and return true.
**
** If rc is SQLITE_DONE or SQLITE_OK, then return false.
**
** The prepared statement is closed in either case.
*/
static int analysisStmtFinish(Analysis *p, int rc, sqlite3_stmt *pStmt){
  if( rc==SQLITE_DONE ){
    rc = SQLITE_OK;
  }
  if( rc!=SQLITE_OK || (rc = sqlite3_reset(pStmt))!=SQLITE_OK ){
    analysisError(p, "SQL run-time error: %s\nOriginal SQL: %s",
                  sqlite3_errmsg(p->db), sqlite3_sql(pStmt));
    analysisReset(p);
  }
  sqlite3_finalize(pStmt);
  return rc;
}

/*
** Run SQL.  Return the number of errors. 
*/
static int analysisSql(Analysis *p, const char *zFormat, ...){
  va_list ap;
  int rc;
  sqlite3_stmt *pStmt = 0;
  va_start(ap, zFormat);
  pStmt = analysisVPrep(p,zFormat,ap);
  va_end(ap);
  if( pStmt==0 ) return 1;
  while( (rc = sqlite3_step(pStmt))==SQLITE_ROW ){}
  if( rc==SQLITE_DONE ){
    rc = SQLITE_OK;
  }else{
    analysisError(p, "SQL run-time error: %s\nOriginal SQL: %s",
                  sqlite3_errmsg(p->db), sqlite3_sql(pStmt));
    analysisReset(p);
  }
  sqlite3_finalize(pStmt);
  return rc;
}

/*
** Run an SQL query that returns an integer.  Write that integer
** into *piRes.  Return the number of errors. 
*/
static int analysisSqlInt(
  Analysis *p,
  sqlite3_int64 *piRes,
  const char *zFormat, ...
){
  va_list ap;
  int rc;
  sqlite3_stmt *pStmt = 0;
  va_start(ap, zFormat);
  pStmt = analysisVPrep(p,zFormat,ap);
  va_end(ap);
  if( pStmt==0 ) return 1;
  rc = sqlite3_step(pStmt);
  if( rc==SQLITE_ROW ){
    *piRes = sqlite3_column_int64(pStmt, 0);
    rc = SQLITE_OK;
  }else if( rc==SQLITE_DONE ){
    rc = SQLITE_OK;
  }else{
    if( p->db ){
      /* p->db is NULL if there was some prior error */
      analysisError(p, "SQL run-time error: %s\nOriginal SQL: %s",
                    sqlite3_errmsg(p->db), sqlite3_sql(pStmt));
    }
    analysisReset(p);
  }
  sqlite3_finalize(pStmt);
  return rc;
}

/*
** Add to the output a title line that contains the text determined
** by the format string.  If the output is initially empty, begin
** the title line with "/" so that it forms the beginning of a C-style
** comment.  Otherwise begin with a new-line.  Always finish with a
** newline.
*/
static void analysisTitle(Analysis *p, const char *zFormat, ...){
  char *zFirst;
  char *zTitle;
  size_t nTitle;
  va_list ap;
  va_start(ap, zFormat);
  zTitle = sqlite3_vmprintf(zFormat, ap);
  va_end(ap);
  if( zTitle==0 ){
    analysisError(p, 0);
    return;
  }
  zFirst = sqlite3_str_length(p->pOut)==0 ? "/" : "\n*";
  nTitle = strlen(zTitle);
  if( nTitle>=75 ){
    sqlite3_str_appendf(p->pOut, "%s** %z\n\n", zFirst, zTitle);
  }else{
    int nExtra = 74 - (int)nTitle;
    sqlite3_str_appendf(p->pOut, "%s** %z %.*c\n\n", zFirst, zTitle,
                        nExtra, '*');
  }
}

/*
** Add an output line that begins with the zDesc text extended out to
** 50 columns with "." characters, and followed by whatever text is
** described by zFormat.
*/
static void analysisLine(
  Analysis *p,             /* Analysis context */
  const char *zDesc,       /* Description */
  const char *zFormat,     /* Argument to the description */
  ...
){
  char *zTxt;
  size_t nDesc;
  va_list ap;
  va_start(ap, zFormat);
  zTxt = sqlite3_vmprintf(zFormat, ap);
  va_end(ap);
  if( zTxt==0 ){
    analysisError(p, 0);
    return;
  }
  nDesc = strlen(zDesc);
  if( nDesc>=50 ){
    sqlite3_str_appendf(p->pOut, "%s %z", zDesc, zTxt);
  }else{
    int nExtra = 50 - (int)nDesc;
    sqlite3_str_appendf(p->pOut, "%s%.*c %z", zDesc, nExtra, '.', zTxt);
  }
}

/*
** Write a percentage into the output.  The number written should show
** two or three significant digits, with the decimal point being the fourth
** character.  
*/
static void analysisPercent(Analysis *p, double r){
  char zNum[100];
  char *zDP;
  int nLeadingDigit;
  int sz;
  sqlite3_snprintf(sizeof(zNum)-5, zNum, r>=10.0 ? "%.3g" :"%.2g", r);
  sz = (int)strlen(zNum);
  zDP = strchr(zNum, '.');
  if( zDP==0 ){
    memcpy(zNum+sz,".0",3);
    nLeadingDigit = sz;
    sz += 2;
  }else{
    nLeadingDigit = (int)(zDP - zNum);
  }
  if( nLeadingDigit<3 ){
    sqlite3_str_appendchar(p->pOut, 3-nLeadingDigit, ' ');
  }
  sqlite3_str_append(p->pOut, zNum, sz);
  sqlite3_str_append(p->pOut, "%\n", 2);
}

/*
** Create a subreport on a subset of tables and/or indexes.
**
** The title if the subreport is given by zTitle.  zWhere is
** a boolean expression that can go in the WHERE clause to select
** the relevant rows of the s.zSU table.
*/
static int analysisSubreport(
  Analysis *p,                  /* Analysis context */
  char *zTitle,                 /* Title for this subreport */
  char *zWhere,                 /* WHERE clause for this subreport */
  sqlite3_int64 pgsz,           /* Database page size */
  sqlite3_int64 nPage           /* Number of pages in entire database */
){
  sqlite3_stmt *pStmt;          /* Statement to query p->zSU */
  sqlite3_int64 nentry;         /* Number of btree entires */
  sqlite3_int64 payload;        /* Payload in bytes */
  sqlite3_int64 ovfl_payload;   /* overflow payload in bytes */
  sqlite3_int64 mx_payload;     /* largest individual payload */
  sqlite3_int64 ovfl_cnt;       /* Number entries using overflow */
  sqlite3_int64 leaf_pages;     /* Leaf pages */
  sqlite3_int64 int_pages;      /* internal pages */
  sqlite3_int64 ovfl_pages;     /* overflow pages */
  sqlite3_int64 leaf_unused;    /* unused bytes on leaf pages */
  sqlite3_int64 int_unused;     /* unused bytes on internal pages */
  sqlite3_int64 ovfl_unused;    /* unused bytes on overflow pages */
  sqlite3_int64 int_cell;       /* B-tree entries on internal pages */
  sqlite3_int64 depth;          /* btree depth */
  sqlite3_int64 cnt;            /* Number of s.zSU entries that match */
  sqlite3_int64 storage;        /* Total bytes */
  sqlite3_int64 total_pages;    /* Total page count */
  sqlite3_int64 total_unused;   /* Total unused bytes */
  sqlite3_int64 total_meta;     /* Total metadata */
  int rc;

  if( zTitle==0 || zWhere==0 ){
    analysisError(p, 0);
    return SQLITE_NOMEM;
  }
  pStmt = analysisPrepare(p,
    "SELECT\n"
    "  sum(if(is_without_rowid OR is_index,nentry,leaf_entries)),\n" /* 0 */
    "  sum(payload),\n"            /* 1 */
    "  sum(ovfl_payload),\n"       /* 2 */
    "  max(mx_payload),\n"         /* 3 */
    "  sum(ovfl_cnt),\n"           /* 4 */
    "  sum(leaf_pages),\n"         /* 5 */
    "  sum(int_pages),\n"          /* 6 */
    "  sum(ovfl_pages),\n"         /* 7 */
    "  sum(leaf_unused),\n"        /* 8 */
    "  sum(int_unused),\n"         /* 9 */
    "  sum(ovfl_unused),\n"        /* 10 */
    "  max(depth),\n"              /* 11 */
    "  count(*),\n"                /* 12 */
    "  sum(int_entries)\n"         /* 13 */
    " FROM temp.%s WHERE %s",
    p->zSU, zWhere);
  if( pStmt==0 ) return 1;
  rc = sqlite3_step(pStmt);
  if( rc==SQLITE_ROW ){
    analysisTitle(p, zTitle);

    nentry = sqlite3_column_int64(pStmt, 0);
    payload = sqlite3_column_int64(pStmt, 1);
    ovfl_payload = sqlite3_column_int64(pStmt, 2);
    mx_payload = sqlite3_column_int64(pStmt, 3);
    ovfl_cnt = sqlite3_column_int64(pStmt, 4);
    leaf_pages = sqlite3_column_int64(pStmt, 5);
    int_pages = sqlite3_column_int64(pStmt, 6);
    ovfl_pages = sqlite3_column_int64(pStmt, 7);
    leaf_unused = sqlite3_column_int64(pStmt, 8);
    int_unused = sqlite3_column_int64(pStmt, 9);
    ovfl_unused = sqlite3_column_int64(pStmt, 10);
    depth = sqlite3_column_int64(pStmt, 11);
    cnt = sqlite3_column_int64(pStmt, 12);
    int_cell = sqlite3_column_int64(pStmt, 13);
    rc = SQLITE_DONE;

    total_pages = leaf_pages + int_pages + ovfl_pages;
    analysisLine(p, "Percentage of total database", "%.3g%%\n",
                 (total_pages*100.0)/(double)nPage);
    analysisLine(p, "Number of entries", "%lld\n", nentry);
    storage = total_pages*pgsz;
    analysisLine(p, "Bytes of storage consumed", "%lld\n", storage);
    analysisLine(p, "Bytes of payload", "%-11lld ", payload);
    analysisPercent(p, payload*100.0/(double)storage);
    if( ovfl_cnt>0 ){
      analysisLine(p, "Bytes of payload in overflow","%-11lld ",ovfl_payload);
      analysisPercent(p, ovfl_payload*100.0/(double)payload);
    }
    total_unused = leaf_unused + int_unused + ovfl_unused;
    total_meta = storage - payload - total_unused;
    analysisLine(p, "Bytes of metadata","%-11lld ", total_meta);
    analysisPercent(p, total_meta*100.0/(double)storage);
    if( cnt==1 ){
      analysisLine(p, "B-tree depth", "%lld\n", depth);
      if( int_cell>1 ){
        analysisLine(p, "Average fanout", "%.1f\n",
                     (double)(int_cell+int_pages)/(double)int_pages);
      }
    }
    if( nentry>0 ){
      analysisLine(p, "Average payload per entry", "%.1f\n",
                   (double)payload/(double)nentry);
      analysisLine(p, "Average unused bytes per entry", "%.1f\n",
                   (double)total_unused/(double)nentry);
      analysisLine(p, "Average metadata per entry", "%.1f\n",
                   (double)total_meta/(double)nentry);
    }
    analysisLine(p, "Maximum single-entry payload", "%lld\n", mx_payload);
    if( nentry>0 ){
      analysisLine(p, "Entries that use overflow", "%-11lld ", ovfl_cnt);
      analysisPercent(p, ovfl_cnt*100.0/(double)nentry);
    }
    if( int_pages>0 ){
      analysisLine(p, "Index pages used", "%lld\n", int_pages);
    }
    analysisLine(p, "Primary pages used", "%lld\n", leaf_pages);
    if( ovfl_cnt ){
      analysisLine(p, "Overflow pages used", "%lld\n", ovfl_pages);
    }
    analysisLine(p, "Total pages used", "%lld\n", total_pages);
    if( int_pages>0 ){
      analysisLine(p, "Unused bytes on index pages", "%lld\n", int_unused);
    }
    analysisLine(p, "Unused bytes on primary pages", "%lld\n", leaf_unused);
    if( ovfl_cnt ){
      analysisLine(p, "Unused bytes on overflow pages", "%lld\n", ovfl_unused);
    }
    analysisLine(p, "Unused bytes on all pages", "%-11lld ", total_unused);
    analysisPercent(p, total_unused*100.0/(double)storage);
  }
  return analysisStmtFinish(p, rc, pStmt);
}

/*
** SQL Function:   analyze(SCHEMA)
**
** Analyze the database schema named in the argument.  Return text
** containing the analysis.
*/
static void analyzeFunc(
  sqlite3_context *context,
  int argc,
  sqlite3_value **argv
){
  int rc;
  sqlite3_stmt *pStmt;
  int n;
  sqlite3_int64 i64;
  sqlite3_int64 pgsz;
  sqlite3_int64 nPage;
  sqlite3_int64 nPageInUse;
  sqlite3_int64 nFreeList;
  sqlite3_int64 nIndex;
  sqlite3_int64 nWORowid;
  Analysis s;
  sqlite3_uint64 r[2];

  memset(&s, 0, sizeof(s));
  s.db = sqlite3_context_db_handle(context);
  s.context = context;
  s.pOut = sqlite3_str_new(0);
  if( sqlite3_str_errcode(s.pOut) ){
    analysisError(&s, 0);
    return;
  }
  s.zSchema = (const char*)sqlite3_value_text(argv[0]);
  if( s.zSchema==0 ){
    s.zSchema = "main";
  }else if( sqlite3_strlike("temp",s.zSchema,0)==0 ){
    analysisReset(&s);
    sqlite3_result_text(context, "cannot analyze \"temp\"",-1,SQLITE_STATIC);
    return;
  }
  i64 = 0;
  rc = analysisSqlInt(&s,&i64,"SELECT 1 FROM pragma_database_list"
                             " WHERE name=%Q COLLATE nocase",s.zSchema);
  if( rc || i64==0 ){
    analysisReset(&s);
    sqlite3_result_text(context,"no such database",-1,SQLITE_STATIC);
    return;
  }
  sqlite3_randomness(sizeof(r), &r);
  s.zSU = sqlite3_mprintf("analysis%016llx%016llx", r[0], r[1]);
  if( s.zSU==0 ){ analysisError(&s, 0); return; }

  /* The s.zSU table contains the data used for the analysis.
  ** The table name contains 128-bits of randomness to avoid
  ** collisions with preexisting tables in temp.
  */
  rc = analysisSql(&s,
    "CREATE TABLE temp.%s(\n"
    "   name text,                -- A table or index\n"
    "   tblname text,             -- Table that owns name\n"
    "   is_index boolean,         -- TRUE if it is an index\n"
    "   is_without_rowid boolean, -- TRUE if WITHOUT ROWID table\n"
    "   nentry int,               -- Number of entries in the BTree\n"
    "   leaf_entries int,         -- Number of leaf entries\n"
    "   depth int,                -- Depth of the b-tree\n"
    "   payload int,              -- Total data stored in this table/index\n"
    "   ovfl_payload int,         -- Total data stored on overflow pages\n"
    "   ovfl_cnt int,             -- Number of entries that use overflow\n"
    "   mx_payload int,           -- Maximum payload size\n"
    "   int_pages int,            -- Interior pages used\n"
    "   leaf_pages int,           -- Leaf pages used\n"
    "   ovfl_pages int,           -- Overflow pages used\n"
    "   int_unused int,           -- Unused bytes on interior pages\n"
    "   leaf_unused int,          -- Unused bytes on primary pages\n"
    "   ovfl_unused int,          -- Unused bytes on overflow pages\n"
    "   int_entries int           -- Btree cells on internal pages\n"
    ");",
    s.zSU
  );
  if( rc ) return;

  /* Populate the s.zSU table
  */
  rc = analysisSql(&s,
    "WITH\n"
    "  allidx(idxname) AS (\n"
    "    SELECT name FROM \"%w\".sqlite_schema WHERE type='index'\n"
    "  ),\n"
    "  allobj(allname,tblname,isidx,isworowid) AS (\n"
    "    SELECT 'sqlite_schema',\n"
    "           'sqlite_schema',\n"
    "           0,\n"
    "           0\n"
    "    UNION ALL\n"
    "    SELECT name,\n"
    "           tbl_name,\n"
    "           type='index',\n"
    "           EXISTS(SELECT 1\n"
    "                    FROM pragma_index_list(sqlite_schema.name,%Q)\n"
    "                   WHERE pragma_index_list.origin='pk'\n"
    "                     AND pragma_index_list.name NOT IN allidx)\n"
    "      FROM \"%w\".sqlite_schema\n"
    "  )\n"
    "INSERT INTO temp.%s\n"
    "  SELECT\n"
    "    allname,\n"
    "    tblname,\n"
    "    isidx,\n"
    "    isworowid,\n"
    "    sum(ncell),\n"
    "    sum((pagetype='leaf')*ncell),\n"
    "    max((length(if(path GLOB '*+*','',path))+3)/4),\n"
    "    sum(payload),\n"
    "    sum((pagetype='overflow')*payload),\n"
    "    sum(path GLOB '*+000000'),\n"
    "    max(mx_payload),\n"
    "    sum(pagetype='internal'),\n"
    "    sum(pagetype='leaf'),\n"
    "    sum(pagetype='overflow'),\n"
    "    sum((pagetype='internal')*unused),\n"
    "    sum((pagetype='leaf')*unused),\n"
    "    sum((pagetype='overflow')*unused),\n"
    "    sum(if(pagetype='internal',ncell))\n"
    "  FROM allobj CROSS JOIN dbstat(%Q) \n"
    "  WHERE dbstat.name=allobj.allname\n"
    "  GROUP BY allname;\n",
    s.zSchema,   /* %w.sqlite_schema -- in allidx */
    s.zSchema,   /* pragma_index_list(...,%Q) */
    s.zSchema,   /* %w.sqlite_schema */
    s.zSU,       /* INTO temp.%s */
    s.zSchema    /* JOIN dbstat(%Q) */
  );
  if( rc ) return;

  nPage = 0;
  rc = analysisSqlInt(&s, &nPage, "PRAGMA \"%w\".page_count", s.zSchema);
  if( rc ) return;
  if( nPage<=0 ){
    /* Very brief reply for an empty database */
    analysisReset(&s);
    sqlite3_result_text(context, "empty database", -1, SQLITE_STATIC);
    return;
  }

  /* Begin generating the report */
  analysisTitle(&s, "Database storage utilization report");
  pgsz = 0;
  rc = analysisSqlInt(&s, &pgsz, "PRAGMA \"%w\".page_size", s.zSchema);
  if( rc ) return;
  analysisLine(&s, "Page size in bytes","%lld\n",pgsz);
  analysisLine(&s, "Pages in the database", "%lld\n", nPage);

  nPageInUse = 0;
  rc = analysisSqlInt(&s, &nPageInUse, 
       "SELECT sum(leaf_pages+int_pages+ovfl_pages) FROM temp.%s", s.zSU);
  if( rc ) return;
  analysisLine(&s, "Pages that store data", "%-11lld ", nPageInUse);
  analysisPercent(&s, (nPageInUse*100.0)/(double)nPage);

  nFreeList = 0;
  rc = analysisSqlInt(&s, &nFreeList, "PRAGMA \"%w\".freelist_count",s.zSchema);
  if( rc ) return;
  analysisLine(&s, "Pages on the freelist", "%-11lld ", nFreeList);
  analysisPercent(&s, (nFreeList*100.0)/(double)nPage);

  i64 = 0;
  rc = analysisSqlInt(&s, &i64, "PRAGMA \"%w\".auto_vacuum", s.zSchema);
  if( rc ) return;
  if( i64==0 || nPage<=1 ){
    i64 = 0;
  }else{
    double rPtrsPerPage = pgsz/5;
    double rAvPage = (nPage-1.0)/(rPtrsPerPage+1.0);
    i64 = (sqlite3_int64)ceil(rAvPage);
  }
  analysisLine(&s, "Pages of auto-vacuum overhead", "%-11lld ", i64);
  analysisPercent(&s, (i64*100.0)/(double)nPage);

  i64 = 0;
  rc = analysisSqlInt(&s, &i64, 
       "SELECT count(*)+1 FROM \"%w\".sqlite_schema WHERE type='table'",
       s.zSchema);
  if( rc ) return;
  analysisLine(&s, "Number of tables", "%lld\n", i64);
  nWORowid = 0;
  rc = analysisSqlInt(&s, &nWORowid,
       "SELECT count(*) FROM \"%w\".pragma_table_list WHERE wr",
       s.zSchema);
  if( rc ) return;
  if( nWORowid>0 ){
    analysisLine(&s, "Number of WITHOUT ROWID tables", "%lld\n", nWORowid);
    analysisLine(&s, "Number of rowid tables", "%lld\n", i64 - nWORowid);
  }
  nIndex = 0;
  rc = analysisSqlInt(&s, &nIndex, 
       "SELECT count(*) FROM \"%w\".sqlite_schema WHERE type='index'",
       s.zSchema);
  if( rc ) return;
  analysisLine(&s, "Number of indexes", "%lld\n", nIndex);
  i64 = 0;
  rc = analysisSqlInt(&s, &i64, 
       "SELECT count(*) FROM \"%w\".sqlite_schema"
       " WHERE name GLOB 'sqlite_autoindex_*' AND type='index'",
       s.zSchema);
  if( rc ) return;
  analysisLine(&s, "Number of defined indexes", "%lld\n", nIndex - i64);
  analysisLine(&s, "Number of implied indexes", "%lld\n", i64);
  analysisLine(&s, "Size of the database in bytes", "%lld\n", pgsz*nPage);
  i64 = 0;
  rc = analysisSqlInt(&s, &i64, 
       "SELECT sum(payload) FROM temp.%s"
       " WHERE NOT is_index AND name NOT LIKE 'sqlite_schema'",
       s.zSU);
  if( rc ) return;
  analysisLine(&s, "Bytes of payload", "%-11lld ", i64);
  analysisPercent(&s, i64*100.0/(double)(pgsz*nPage));

  analysisTitle(&s, "Page counts for all tables with their indexes");
  pStmt = analysisPrepare(&s,
    "SELECT upper(tblname),\n"
    "       sum(int_pages+leaf_pages+ovfl_pages)\n"
    "  FROM temp.%s\n"
    " GROUP BY 1\n"
    " ORDER BY 2 DESC, 1;",
    s.zSU);
  if( pStmt==0 ) return;
  while( (rc = sqlite3_step(pStmt))==SQLITE_ROW ){
    sqlite3_int64 nn = sqlite3_column_int64(pStmt,1);
    analysisLine(&s, (const char*)sqlite3_column_text(pStmt,0), "%-11lld ", nn);
    analysisPercent(&s, (nn*100.0)/(double)nPage);
  }
  if( analysisStmtFinish(&s, rc, pStmt) ) return;

  analysisTitle(&s, "Page counts for all tables and indexes separately");
  pStmt = analysisPrepare(&s,
    "SELECT upper(name),\n"
    "       sum(int_pages+leaf_pages+ovfl_pages)\n"
    "  FROM temp.%s\n"
    " GROUP BY 1\n"
    " ORDER BY 2 DESC, 1;",
    s.zSU);
  if( pStmt==0 ) return;
  while( (rc = sqlite3_step(pStmt))==SQLITE_ROW ){
    sqlite3_int64 nn = sqlite3_column_int64(pStmt,1);
    analysisLine(&s, (const char*)sqlite3_column_text(pStmt,0), "%-11lld ", nn);
    analysisPercent(&s, (nn*100.0)/(double)nPage);
  }
  if( analysisStmtFinish(&s, rc, pStmt) ) return;

  rc = analysisSubreport(&s, "All tables and indexes", "1", pgsz, nPage);
  if( rc ) return;
  rc = analysisSubreport(&s, "All tables", "NOT is_index", pgsz, nPage);
  if( rc ) return;
  if( nWORowid>0 ){
    rc = analysisSubreport(&s, "All WITHOUT ROWID tables", "is_without_rowid",
                           pgsz, nPage);
    if( rc ) return;
    rc = analysisSubreport(&s, "All rowid tables",
                           "NOT is_without_rowid AND NOT is_index",
                           pgsz, nPage);
    if( rc ) return;
  }
  rc = analysisSubreport(&s, "All indexes", "is_index", pgsz, nPage);
  if( rc ) return;

  pStmt = analysisPrepare(&s,
    "SELECT upper(tblname), tblname, sum(is_index) FROM temp.%s"
    " GROUP BY 1 ORDER BY 1",
    s.zSU);
  if( pStmt==0 ) return;
  while( (rc = sqlite3_step(pStmt))==SQLITE_ROW ){
    const char *zUpper = (const char*)sqlite3_column_text(pStmt, 0);
    const char *zName = (const char*)sqlite3_column_text(pStmt, 1);
    int nSubIndex = sqlite3_column_int(pStmt, 2);
    if( nSubIndex==0 ){
      char *zTitle = sqlite3_mprintf("Table %s", zUpper);
      char *zWhere = sqlite3_mprintf("name=%Q", zName);
      rc = analysisSubreport(&s, zTitle, zWhere, pgsz, nPage);
      sqlite3_free(zTitle);
      sqlite3_free(zWhere);
      if( rc ) break;
    }else{
      sqlite3_stmt *pS2;
      char *zTitle = sqlite3_mprintf("Table %s and all its indexes", zUpper);
      char *zWhere = sqlite3_mprintf("tblname=%Q", zName);
      rc = analysisSubreport(&s, zTitle, zWhere, pgsz, nPage);
      sqlite3_free(zTitle);
      sqlite3_free(zWhere);
      if( rc ) break;
      zTitle = sqlite3_mprintf("Table %s w/o any indexes", zUpper);
      zWhere = sqlite3_mprintf("name=%Q", zName);
      rc = analysisSubreport(&s, zTitle, zWhere, pgsz, nPage);
      sqlite3_free(zTitle);
      sqlite3_free(zWhere);
      if( rc ) break;
      if( nSubIndex>1 ){
        zTitle = sqlite3_mprintf("All indexes of table %s", zUpper);
        zWhere = sqlite3_mprintf("tblname=%Q AND is_index", zName);
        rc = analysisSubreport(&s, zTitle, zWhere, pgsz, nPage);
        sqlite3_free(zTitle);
        sqlite3_free(zWhere);
        if( rc ) break;
      }
      pS2 = analysisPrepare(&s,
              "SELECT name, upper(name) FROM temp.%s"
              " WHERE is_index AND tblname=%Q",
              s.zSU, zName);
      if( pS2==0 ){
        rc = SQLITE_NOMEM;
        break;
      }
      while( (rc = sqlite3_step(pS2))==SQLITE_ROW ){
        const char *zU = (const char*)sqlite3_column_text(pS2, 1);
        const char *zN = (const char*)sqlite3_column_text(pS2, 0);
        zTitle = sqlite3_mprintf("Index %s", zU);
        zWhere = sqlite3_mprintf("name=%Q", zN);
        rc = analysisSubreport(&s, zTitle, zWhere, pgsz, nPage);
        sqlite3_free(zTitle);
        sqlite3_free(zWhere);
        if( rc ) break;
      }
      rc = analysisStmtFinish(&s, rc, pS2);
      if( rc ) break;
    }
  }
  if( analysisStmtFinish(&s, rc, pStmt) ) return;

  /* Append SQL statements that will recreate the raw data used for
  ** the analysis.
  */
  analysisTitle(&s, "Raw data used to generate this report");
  sqlite3_str_appendf(s.pOut,
    "The following SQL will create a table named \"space_used\" which\n"
    "contains most of the information used to generate the report above.\n"
    "*/\n"
  );
  sqlite3_str_appendf(s.pOut,
    "BEGIN;\n"
    "CREATE TABLE space_used(\n"
    "   name text,                -- A table or index\n"                /* 0 */
    "   tblname text,             -- Table that owns name\n"            /* 1 */
    "   is_index boolean,         -- TRUE if it is an index\n"          /* 2 */
    "   is_without_rowid boolean, -- TRUE if WITHOUT ROWID table\n"     /* 3 */
    "   nentry int,               -- Number of entries in the BTree\n"  /* 4 */
    "   leaf_entries int,         -- Number of leaf entries\n"          /* 5 */
    "   depth int,                -- Depth of the b-tree\n"             /* 6 */
    "   payload int,              -- Total data in this table/index\n"  /* 7 */
    "   ovfl_payload int,         -- Total data on overflow pages\n"    /* 8 */
    "   ovfl_cnt int,             -- Entries that use overflow\n"       /* 9 */
    "   mx_payload int,           -- Maximum payload size\n"            /* 10 */
    "   int_pages int,            -- Interior pages used\n"             /* 11 */
    "   leaf_pages int,           -- Leaf pages used\n"                 /* 12 */
    "   ovfl_pages int,           -- Overflow pages used\n"             /* 13 */
    "   int_unused int,           -- Unused bytes on interior pages\n"  /* 14 */
    "   leaf_unused int,          -- Unused bytes on primary pages\n"   /* 15 */
    "   ovfl_unused int,          -- Unused bytes on overflow pages\n"  /* 16 */
    "   int_entries int           -- B-tree entries on internal pages\n"/* 17 */
    ");\n"
    "INSERT INTO space_used VALUES\n"
  );
  pStmt = analysisPrepare(&s,
     "SELECT quote(name), quote(tblname),\n"                        /* 0..1 */
     "       is_index, is_without_rowid, nentry, leaf_entries,\n"   /* 2..5 */
     "       depth, payload, ovfl_payload, ovfl_cnt, mx_payload,\n" /* 6..10 */
     "       int_pages, leaf_pages, ovfl_pages, int_unused,\n"      /* 11..14 */
     "       leaf_unused, ovfl_unused, int_entries\n"               /* 15..17 */
     "  FROM temp.%s;",
     s.zSU);
  if( pStmt==0 ) return;
  n = 0;
  while( (rc = sqlite3_step(pStmt))==SQLITE_ROW ){
    if( n++ ) sqlite3_str_appendf(s.pOut,",\n");
    sqlite3_str_appendf(s.pOut,
      " (%s,%s,%lld,%lld,%lld,%lld,%lld,%lld,%lld,"
      "%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld)",
      sqlite3_column_text(pStmt, 0),
      sqlite3_column_text(pStmt, 1),
      sqlite3_column_int64(pStmt, 2),
      sqlite3_column_int64(pStmt, 3),
      sqlite3_column_int64(pStmt, 4),
      sqlite3_column_int64(pStmt, 5),
      sqlite3_column_int64(pStmt, 6),
      sqlite3_column_int64(pStmt, 7),
      sqlite3_column_int64(pStmt, 8),
      sqlite3_column_int64(pStmt, 9),
      sqlite3_column_int64(pStmt, 10),
      sqlite3_column_int64(pStmt, 11),
      sqlite3_column_int64(pStmt, 12),
      sqlite3_column_int64(pStmt, 13),
      sqlite3_column_int64(pStmt, 14),
      sqlite3_column_int64(pStmt, 15),
      sqlite3_column_int64(pStmt, 16),
      sqlite3_column_int64(pStmt, 17));
  }
  if( rc!=SQLITE_DONE ){
    analysisError(&s, "SQL run-time error: %s\nSQL: %s",
                  sqlite3_errmsg(s.db), sqlite3_sql(pStmt));
    sqlite3_finalize(pStmt);
    return;
  }
  sqlite3_str_appendf(s.pOut,";\nCOMMIT;");
  sqlite3_finalize(pStmt);

  if( sqlite3_str_length(s.pOut) ){
    sqlite3_result_text(context, sqlite3_str_finish(s.pOut), -1,
                        sqlite3_free);
    s.pOut = 0;
  }
  analysisReset(&s);
}


#ifdef _WIN32
__declspec(dllexport)
#endif
int sqlite3_analyze_init(
  sqlite3 *db, 
  char **pzErrMsg, 
  const sqlite3_api_routines *pApi
){
  int rc = SQLITE_OK;
  SQLITE_EXTENSION_INIT2(pApi);
  (void)pzErrMsg;  /* Unused parameter */
  rc = sqlite3_create_function(db, "analyze", 1,
                   SQLITE_UTF8|SQLITE_INNOCUOUS,
                   0, analyzeFunc, 0, 0);
  return rc;
}
