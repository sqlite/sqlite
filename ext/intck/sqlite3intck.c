/*
** 2024-02-08
**
** The author disclaims copyright to this source code.  In place of
** a legal notice, here is a blessing:
**
**    May you do good and not evil.
**    May you find forgiveness for yourself and forgive others.
**    May you share freely, never taking more than you give.
**
*************************************************************************
*/

#include "sqlite3intck.h"
#include <string.h>
#include <assert.h>
#include <stdio.h>

struct sqlite3_intck {
  sqlite3 *db;
  const char *zDb;                /* Copy of zDb parameter to _open() */

  sqlite3_stmt *pListTables;

  sqlite3_stmt *pCheck;
  int nCheck;

  int rc;                         /* SQLite error code */
  char *zErr;                     /* Error message */

  char *zTestSql;                 /* Returned by sqlite3_intck_test_sql() */
};


/*
** Some error has occurred while using database p->db. Save the error message
** and error code currently held by the database handle in p->rc and p->zErr.
*/
static void intckSaveErrmsg(sqlite3_intck *p){
  const char *zDberr = sqlite3_errmsg(p->db);
  p->rc = sqlite3_errcode(p->db);
  if( zDberr ){
    sqlite3_free(p->zErr);
    p->zErr = sqlite3_mprintf("%s", zDberr);
  }
}

static sqlite3_stmt *intckPrepare(sqlite3_intck *p, const char *zFmt, ...){
  sqlite3_stmt *pRet = 0;
  va_list ap;
  char *zSql = 0;
  va_start(ap, zFmt);
  zSql = sqlite3_vmprintf(zFmt, ap);
  if( p->rc==SQLITE_OK ){
    if( zSql==0 ){
      p->rc = SQLITE_NOMEM;
    }else{
      p->rc = sqlite3_prepare_v2(p->db, zSql, -1, &pRet, 0);
      fflush(stdout);
      if( p->rc!=SQLITE_OK ){
      printf("ERROR: %s\n", zSql);
      printf("MSG: %s\n", sqlite3_errmsg(p->db));
      if( sqlite3_error_offset(p->db)>=0 ){
        int iOff = sqlite3_error_offset(p->db);
        printf("AT: %.40s\n", &zSql[iOff]);
      }
      fflush(stdout);
        intckSaveErrmsg(p);
        assert( pRet==0 );
      }
    }
  }
  sqlite3_free(zSql);
  va_end(ap);
  return pRet;
}

static void intckFinalize(sqlite3_intck *p, sqlite3_stmt *pStmt){
  int rc = sqlite3_finalize(pStmt);
  if( p->rc==SQLITE_OK && rc!=SQLITE_OK ){
    intckSaveErrmsg(p);
  }
}

/*
** Return an SQL statement that will itself return a single row for each
** table in the target schema. The row contains two columns:
**
**   0: table_name - name of table
**   1: without_rowid - true for WITHOUT ROWID tables, false otherwise.
**
*/
static sqlite3_stmt *intckListTables(sqlite3_intck *p){
  return intckPrepare(p, 
    "WITH tables(table_name) AS (" 
    "  SELECT name"
    "  FROM %Q.sqlite_schema WHERE type='table' OR type='index'"
    "  UNION ALL "
    "  SELECT 'sqlite_schema'"
    ")"
    "SELECT * FROM tables"
    , p->zDb
  );
}

static char *intckStrdup(sqlite3_intck *p, const char *zIn){
  char *zOut = 0;
  if( p->rc==SQLITE_OK ){
    int nIn = strlen(zIn);
    zOut = sqlite3_malloc(nIn+1);
    if( zOut==0 ){
      p->rc = SQLITE_NOMEM;
    }else{
      memcpy(zOut, zIn, nIn+1);
    }
  }
  return zOut;
}

/*
** Return the size in bytes of the first token in nul-terminated buffer z.
** For the purposes of this call, a token is either:
**
**   *  a quoted SQL string,
*    *  a contiguous series of ascii alphabet characters, or
*    *  any other single byte.
*/
static int intckGetToken(const char *z){
  char c = z[0];
  int iRet = 1;
  if( c=='\'' || c=='"' || c=='`' ){
    while( 1 ){
      if( z[iRet]==c ){
        iRet++;
        if( z[iRet+1]!=c ) break;
      }
      iRet++;
    }
  }
  else if( c=='[' ){
    while( z[iRet++]!=']' && z[iRet] );
  }
  else if( (c>='A' && c<='Z') || (c>='a' && c<='z') ){
    while( (z[iRet]>='A' && z[iRet]<='Z') || (z[iRet]>='a' && z[iRet]<='z') ){
      iRet++;
    }
  }

  return iRet;
}

static int intckIsSpace(char c){
  return (c==' ' || c=='\t' || c=='\n' || c=='\r');
}

static int intckTokenMatch(
  const char *zToken, 
  int nToken, 
  const char *z1, 
  const char *z2
){
  return (
      (strlen(z1)==nToken && 0==sqlite3_strnicmp(zToken, z1, nToken))
   || (z2 && strlen(z2)==nToken && 0==sqlite3_strnicmp(zToken, z2, nToken))
  );
}

/*
** Argument z points to the text of a CREATE INDEX statement. This function
** identifies the part of the text that contains either the index WHERE 
** clause (if iCol<0) or the iCol'th column of the index.
**
** If (iCol<0), the identified fragment does not include the "WHERE" keyword,
** only the expression that follows it. If (iCol>=0) then the identified
** fragment does not include any trailing sort-order keywords - "ASC" or 
** "DESC".
**
** If the CREATE INDEX statement does not contain the requested field or
** clause, NULL is returned and (*pnByte) is set to 0. Otherwise, a pointer to
** the identified fragment is returned and output parameter (*pnByte) set
** to its size in bytes.
*/
static const char *intckParseCreateIndex(const char *z, int iCol, int *pnByte){
  int iOff = 0;
  int iThisCol = 0;
  int iStart = 0;
  int nOpen = 0;

  const char *zRet = 0;
  int nRet = 0;

  int iEndOfCol = 0;

  /* Skip forward until the first "(" token */
  while( z[iOff]!='(' ){
    iOff += intckGetToken(&z[iOff]);
    if( z[iOff]=='\0' ) return 0;
  }
  assert( z[iOff]=='(' );

  nOpen = 1;
  iOff++;
  iStart = iOff;
  while( z[iOff] ){
    const char *zToken = &z[iOff];
    int nToken = 0;

    /* Check if this is the end of the current column - either a "," or ")"
    ** when nOpen==1.  */
    if( nOpen==1 ){
      if( z[iOff]==',' || z[iOff]==')' ){
        if( iCol==iThisCol ){
          int iEnd = iEndOfCol ? iEndOfCol : iOff;
          nRet = (iEnd - iStart);
          zRet = &z[iStart];
          break;
        }
        iStart = iOff+1;
        while( intckIsSpace(z[iStart]) ) iStart++;
        iThisCol++;
      }
      if( z[iOff]==')' ) break;
    }
    if( z[iOff]=='(' ) nOpen++;
    if( z[iOff]==')' ) nOpen--;
    nToken = intckGetToken(zToken);

    if( intckTokenMatch(zToken, nToken, "ASC", "DESC") ){
      iEndOfCol = iOff;
    }else if( 0==intckIsSpace(zToken[0]) ){
      iEndOfCol = 0;
    }

    iOff += nToken;
  }

  /* iStart is now the byte offset of 1 byte passed the final ')' in the
  ** CREATE INDEX statement. Try to find a WHERE clause to return.  */
  while( zRet==0 && z[iOff] ){
    int n = intckGetToken(&z[iOff]);
    if( n==5 && 0==sqlite3_strnicmp(&z[iOff], "where", 5) ){
      zRet = &z[iOff+5];
      nRet = strlen(zRet);
    }
    iOff += n;
  }

  /* Trim any whitespace from the start and end of the returned string. */
  if( zRet ){
    while( intckIsSpace(zRet[0]) ){
      nRet--;
      zRet++;
    }
    while( nRet>0 && intckIsSpace(zRet[nRet-1]) ) nRet--;
  }

  *pnByte = nRet;
  return zRet;
}

static void parseCreateIndexFunc(
  sqlite3_context *pCtx, 
  int nVal, 
  sqlite3_value **apVal
){
  const char *zSql = (const char*)sqlite3_value_text(apVal[0]);
  int idx = sqlite3_value_int(apVal[1]);
  const char *zRes = 0;
  int nRes = 0;

  assert( nVal==2 );
  if( zSql ){
    zRes = intckParseCreateIndex(zSql, idx, &nRes);
  }
  sqlite3_result_text(pCtx, zRes, nRes, SQLITE_TRANSIENT);
}

/*
** Return true if sqlite3_intck.db has automatic indexes enabled, false
** otherwise.
*/
static int intckGetAutoIndex(sqlite3_intck *p){
  int bRet = 0;
  sqlite3_stmt *pStmt = 0;
  pStmt = intckPrepare(p, "PRAGMA automatic_index");
  if( p->rc==SQLITE_OK && SQLITE_ROW==sqlite3_step(pStmt) ){
    bRet = sqlite3_column_int(pStmt, 0);
  }
  intckFinalize(p, pStmt);
  return bRet;
}

/*
** Return true if zObj is an index, or false otherwise.
*/
static int intckIsIndex(sqlite3_intck *p, const char *zObj){
  int bRet = 0;
  sqlite3_stmt *pStmt = 0;
  pStmt = intckPrepare(p, 
      "SELECT 1 FROM %Q.sqlite_schema WHERE name=%Q AND type='index'",
      p->zDb, zObj
  );
  if( p->rc==SQLITE_OK && SQLITE_ROW==sqlite3_step(pStmt) ){
    bRet = 1;
  }
  intckFinalize(p, pStmt);
  return bRet;
}

static void intckExec(sqlite3_intck *p, const char *zSql){
  sqlite3_stmt *pStmt = 0;
  pStmt = intckPrepare(p, "%s", zSql);
  while( p->rc==SQLITE_OK && SQLITE_ROW==sqlite3_step(pStmt) );
  intckFinalize(p, pStmt);
}

static char *intckCheckObjectSql(sqlite3_intck *p, const char *zObj){
  char *zRet = 0;
  sqlite3_stmt *pStmt = 0;
  int bAutoIndex = 0;
  int bIsIndex = 0;

  const char *zCommon = 
      /* Relation without_rowid also contains just one row. Column "b" is
      ** set to true if the table being examined is a WITHOUT ROWID table,
      ** or false otherwise.  */
      ", without_rowid(b) AS ("
      "  SELECT EXISTS ("
      "    SELECT 1 FROM tabname, pragma_index_list(tab, db) AS l"
      "      WHERE origin='pk' "
      "      AND NOT EXISTS (SELECT 1 FROM sqlite_schema WHERE name=l.name)"
      "  )"
      ")"
      ""
      /* Table idx_cols contains 1 row for each column in each index on the
      ** table being checked. Columns are:
      **
      **   idx_name: Name of the index.
      **   idx_ispk: True if this index is the PK of a WITHOUT ROWID table.
      **   col_name: Name of indexed column, or NULL for index on expression.
      **   col_expr: Indexed expression, including COLLATE clause.
      **   col_alias: Alias used for column in 'intck_wrapper' table.
      */
      ", idx_cols(idx_name, idx_ispk, col_name, col_expr, col_alias) AS ("
      "  SELECT l.name, (l.origin=='pk' AND w.b), i.name, COALESCE(("
      "    SELECT parse_create_index(sql, i.seqno) FROM "
      "    sqlite_schema WHERE name = l.name"
      "  ), format('\"%w\"', i.name) || ' COLLATE ' || quote(i.coll)),"
      "  'c' || row_number() OVER ()"
      "  FROM "
      "      tabname t,"
      "      without_rowid w,"
      "      pragma_index_list(t.tab, t.db) l,"
      "      pragma_index_xinfo(l.name) i"
      "      WHERE i.key"
      "  UNION ALL"
      "  SELECT '', 1, '_rowid_', '_rowid_', 'r1' FROM without_rowid WHERE b=0"
      ")"
      ""
      ""
      ", tabpk(db, tab, idx, o_pk, i_pk, q_pk, eq_pk, ps_pk, pk_pk) AS ("
      "    WITH pkfields(f, a) AS ("
      "      SELECT i.col_name, i.col_alias FROM idx_cols i WHERE i.idx_ispk"
      "    )"
      "    SELECT t.db, t.tab, t.idx, "
      "           group_concat('o.'||a, ', '), "
      "           group_concat('i.'||quote(f), ', '), "
      "           group_concat('quote(o.'||a||')', ' || '','' || '),  "
      "           format('(%s)==(%s)',"
      "               group_concat('o.'||a, ', '), "
      "               group_concat(format('\"%w\"', f), ', ')"
      "           ),"
      "           group_concat('%s', ','),"
      "           group_concat('quote('||a||')', ', ')  "
      "    FROM tabname t, pkfields"
      ")"
      ""
      ", idx(name, match_expr, partial, partial_alias, idx_ps, idx_idx) AS ("
      "  SELECT idx_name,"
      "    format('(%s) IS (%s)', "
      "           group_concat(i.col_expr, ', '),"
      "           group_concat('o.'||i.col_alias, ', ')"
      "    ), "
      "    parse_create_index("
      "        (SELECT sql FROM sqlite_schema WHERE name=idx_name), -1"
      "    ),"
      "    'cond' || row_number() OVER ()"
      "    , group_concat('%s', ',')"
      "    , group_concat('quote('||i.col_alias||')', ', ')"
      "  FROM tabpk t, "
      "       without_rowid w,"
      "       idx_cols i"
      "  WHERE i.idx_ispk==0 "
      "  GROUP BY idx_name"
      ")"
      ""
      ", wrapper_with(s) AS ("
      "  SELECT 'intck_wrapper AS (\n  SELECT\n    ' || ("
      "      WITH f(a, b) AS ("
      "        SELECT col_expr, col_alias FROM idx_cols"
      "          UNION ALL "
      "        SELECT partial, partial_alias FROM idx WHERE partial IS NOT NULL"
      "      )"
      "      SELECT group_concat(format('%s AS %s', a, b), ',\n    ') FROM f"
      "    )"
      "    || format('\n  FROM %Q.%Q ', t.db, t.tab)"
           /* If the object being checked is a table, append "NOT INDEXED".
           ** Otherwise, append "INDEXED BY <index>", and then, if the index 
           ** is a partial index " WHERE <condition>".  */
      "    || CASE WHEN t.idx IS NULL THEN "
      "        'NOT INDEXED'"
      "       ELSE"
      "        format('INDEXED BY %Q%s', t.idx, ' WHERE '||i.partial)"
      "       END"
      "    || '\n)'"
      "    FROM tabname t LEFT JOIN idx i ON (i.name=t.idx)"
      ")"
      ""
  ;

  bAutoIndex = intckGetAutoIndex(p);
  if( bAutoIndex ) intckExec(p, "PRAGMA automatic_index = 0");

  bIsIndex = intckIsIndex(p, zObj);
  if( bIsIndex ){
    pStmt = intckPrepare(p,
      /* Table idxname contains a single row. The first column, "db", contains
      ** the name of the db containing the table (e.g. "main") and the second,
      ** "tab", the name of the table itself.  */
      "WITH tabname(db, tab, idx) AS ("
      "  SELECT %Q, (SELECT tbl_name FROM %Q.sqlite_schema WHERE name=%Q), %Q "
      ")"
      "%s" /* zCommon */
      ""
      ", case_statement(c) AS ("
      "  SELECT "
      "    'CASE WHEN (' || group_concat(col_alias, ', ') || ') IS (\n  ' "
      "    || 'SELECT ' || group_concat(col_expr, ', ') || ' FROM '"
      "    || format('%%Q.%%Q NOT INDEXED WHERE %%s\n)', t.db, t.tab, p.eq_pk)"
      "    || '\nTHEN NULL\n'"
      "    || 'ELSE format(''surplus entry ('"
      "    ||   group_concat('%%s', ',') || ',' || p.ps_pk"
      "    || ') in index ' || t.idx || ''', ' "
      "    ||   group_concat('quote('||i.col_alias||')', ', ') || ', ' || p.pk_pk"
      "    || ')'"
      "    || '\nEND'"
      "  FROM tabname t, tabpk p, idx_cols i WHERE i.idx_name=t.idx"
      ""
      ")"
      ""
      ", main_select(m) AS ("
      "  SELECT format("
      "      'WITH %%s\nSELECT %%s\nFROM intck_wrapper AS o',"
      "      ww.s, c"
      "  )"
      "  FROM case_statement, wrapper_with ww"
      ")"

      "SELECT m FROM main_select"
      , p->zDb, p->zDb, zObj, zObj
      , zCommon
      );
  }else{
    pStmt = intckPrepare(p,
      /* Table tabname contains a single row. The first column, "db", contains
      ** the name of the db containing the table (e.g. "main") and the second,
      ** "tab", the name of the table itself.  */
      "WITH tabname(db, tab, idx) AS (SELECT %Q, %Q, NULL)"
      ""
      "%s" /* zCommon */

      /* expr(e) contains one row for each index on table zObj. Value e
      ** is set to an expression that evaluates to NULL if the required
      ** entry is present in the index, or an error message otherwise.  */
      ", expr(e, p) AS ("
      "  SELECT format('CASE WHEN (%%s) IN\n"
      "    (SELECT %%s FROM %%Q.%%Q AS i INDEXED BY %%Q WHERE %%s%%s)\n"
      "    THEN NULL\n"
      "    ELSE format(''entry (%%s,%%s) missing from index %%s'', %%s, %%s)\n"
      "  END\n'"
      "    , t.o_pk, t.i_pk, t.db, t.tab, i.name, i.match_expr, "
      "    ' AND (' || partial || ')',"
      "      i.idx_ps, t.ps_pk, i.name, i.idx_idx, t.pk_pk),"
      "    CASE WHEN partial IS NULL THEN NULL ELSE i.partial_alias END"
      "  FROM tabpk t, idx i"
      ")"

      ", numbered(ii, cond, e) AS ("
      "  SELECT 0, 'n.ii=0', 'NULL'"
      "    UNION ALL "
      "  SELECT row_number() OVER (),"
      "      '(n.ii='||row_number() OVER ()||COALESCE(' AND '||p||')', ')'), e"
      "  FROM expr"
      ")"

      ", counter_with(w) AS ("
      "    SELECT 'WITH intck_counter(ii) AS (\n  ' || "
      "       group_concat('SELECT '||ii, ' UNION ALL\n  ') "
      "    || '\n)' FROM numbered"
      ")"
      ""
      ", case_statement(c) AS ("
      "    SELECT 'CASE ' || "
      "    group_concat(format('\n  WHEN %%s THEN (%%s)', cond, e), '') ||"
      "    '\nEND AS error_message'"
      "    FROM numbered"
      ")"

      ", main_select(m) AS ("
      "  SELECT format("
      "      '%%s, %%s\nSELECT %%s\nFROM intck_wrapper AS o"
               ", intck_counter AS n ORDER BY %%s', "
      "      w, ww.s, c, t.o_pk"
      "  )"
      "  FROM case_statement, tabpk t, counter_with, wrapper_with ww"
      ")"

      "SELECT m FROM main_select",
      p->zDb, zObj, zCommon
    );
  }

  while( p->rc==SQLITE_OK && SQLITE_ROW==sqlite3_step(pStmt) ){
#if 0
    int nField = sqlite3_column_count(pStmt);
    int ii;
    for(ii=0; ii<nField; ii++){
      const char *zName = sqlite3_column_name(pStmt, ii);
      const char *zVal = (const char*)sqlite3_column_text(pStmt, ii);
      printf("FIELD %s = %s\n", zName, zVal ? zVal : "(null)");
    }
    printf("\n");
    fflush(stdout);
#else
    zRet = intckStrdup(p, (const char*)sqlite3_column_text(pStmt, 0));
#endif
  }
  intckFinalize(p, pStmt);

  if( bAutoIndex ) intckExec(p, "PRAGMA automatic_index = 1");
  return zRet;
}

static void intckCheckObject(sqlite3_intck *p){
  const char *zTab = (const char*)sqlite3_column_text(p->pListTables, 0);
  char *zSql = intckCheckObjectSql(p, zTab);
  p->pCheck = intckPrepare(p, "%s", zSql);
  sqlite3_free(zSql);
}

int sqlite3_intck_open(
  sqlite3 *db,                    /* Database handle to operate on */
  const char *zDbArg,             /* "main", "temp" etc. */
  const char *zFile,              /* Path to save-state db on disk (or NULL) */
  sqlite3_intck **ppOut           /* OUT: New integrity-check handle */
){
  sqlite3_intck *pNew = 0;
  int rc = SQLITE_OK;
  const char *zDb = zDbArg ? zDbArg : "main";
  int nDb = strlen(zDb);

  pNew = (sqlite3_intck*)sqlite3_malloc(sizeof(*pNew) + nDb + 1);
  if( pNew==0 ){
    rc = SQLITE_NOMEM;
  }else{
    memset(pNew, 0, sizeof(*pNew));
    pNew->db = db;
    pNew->zDb = (const char*)&pNew[1];
    memcpy(&pNew[1], zDb, nDb+1);
    sqlite3_create_function(db, "parse_create_index", 
        2, SQLITE_UTF8, 0, parseCreateIndexFunc, 0, 0
    );
  }

  *ppOut = pNew;
  return rc;
}

void sqlite3_intck_close(sqlite3_intck *p){
  if( p && p->db ){
    sqlite3_create_function(
        p->db, "parse_create_index", 1, SQLITE_UTF8, 0, 0, 0, 0
    );
  }
  sqlite3_free(p->zTestSql);
  sqlite3_free(p->zErr);
  sqlite3_free(p);
}

int sqlite3_intck_step(sqlite3_intck *p){
  if( p->rc==SQLITE_OK ){
    if( p->pListTables==0 ){
      p->pListTables = intckListTables(p);
    }
    assert( p->pListTables || p->rc!=SQLITE_OK );

    if( p->rc==SQLITE_OK && p->pCheck==0 ){
      if( sqlite3_step(p->pListTables)==SQLITE_ROW ){
        intckCheckObject(p);
      }else{
        int rc = sqlite3_finalize(p->pListTables);
        if( rc==SQLITE_OK ){
          p->rc = SQLITE_DONE;
        }else{
          intckSaveErrmsg(p);
        }
        p->pListTables = 0;
      }
    }

    if( p->rc==SQLITE_OK ){
      if( sqlite3_step(p->pCheck)==SQLITE_ROW ){
        /* Fine, whatever... */
      }else{
        if( sqlite3_finalize(p->pCheck)!=SQLITE_OK ){
          intckSaveErrmsg(p);
        }
        p->pCheck = 0;
      }
    }
  }

  return p->rc;
}

const char *sqlite3_intck_message(sqlite3_intck *p){
  if( p->pCheck ){
    return (const char*)sqlite3_column_text(p->pCheck, 0);
  }
  return 0;
}

int sqlite3_intck_error(sqlite3_intck *p, const char **pzErr){
  *pzErr = p->zErr;
  return (p->rc==SQLITE_DONE ? SQLITE_OK : p->rc);
}

int sqlite3_intck_suspend(sqlite3_intck *pCk){
  return SQLITE_OK;
}

const char *sqlite3_intck_test_sql(sqlite3_intck *p, const char *zObj){
  sqlite3_free(p->zTestSql);
  p->zTestSql = intckCheckObjectSql(p, zObj);
  return p->zTestSql;
}

