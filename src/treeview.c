/*
** 2015-06-08
**
** The author disclaims copyright to this source code.  In place of
** a legal notice, here is a blessing:
**
**    May you do good and not evil.
**    May you find forgiveness for yourself and forgive others.
**    May you share freely, never taking more than you give.
**
*************************************************************************
**
** This file contains C code to implement the TreeView debugging routines.
** These routines print a parse tree to standard output for debugging and
** analysis. 
**
** The interfaces in this file is only available when compiling
** with SQLITE_DEBUG.
*/
#include "sqliteInt.h"
#ifdef SQLITE_DEBUG

/*
** Add a new subitem to the tree.  The moreToFollow flag indicates that this
** is not the last item in the tree.
*/
static void sqlite3TreeViewPush(TreeView **pp, u8 moreToFollow){
  TreeView *p = *pp;
  if( p==0 ){
    *pp = p = sqlite3_malloc64( sizeof(*p) );
    if( p==0 ) return;
    memset(p, 0, sizeof(*p));
  }else{
    p->iLevel++;
  }
  assert( moreToFollow==0 || moreToFollow==1 );
  if( p->iLevel<(int)sizeof(p->bLine) ) p->bLine[p->iLevel] = moreToFollow;
}

/*
** Finished with one layer of the tree
*/
static void sqlite3TreeViewPop(TreeView **pp){
  TreeView *p = *pp;
  if( p==0 ) return;
  p->iLevel--;
  if( p->iLevel<0 ){
    sqlite3_free(p);
    *pp = 0;
  }
}

/*
** Generate a single line of output for the tree, with a prefix that contains
** all the appropriate tree lines
*/
void sqlite3TreeViewLine(TreeView *p, const char *zFormat, ...){
  va_list ap;
  int i;
  StrAccum acc;
  char zBuf[1000];
  sqlite3StrAccumInit(&acc, 0, zBuf, sizeof(zBuf), 0);
  if( p ){
    for(i=0; i<p->iLevel && i<(int)sizeof(p->bLine)-1; i++){
      sqlite3_str_append(&acc, p->bLine[i] ? "|   " : "    ", 4);
    }
    sqlite3_str_append(&acc, p->bLine[i] ? "|-- " : "'-- ", 4);
  }
  if( zFormat!=0 ){
    va_start(ap, zFormat);
    sqlite3_str_vappendf(&acc, zFormat, ap);
    va_end(ap);
    assert( acc.nChar>0 || acc.accError );
    sqlite3_str_append(&acc, "\n", 1);
  }
  sqlite3StrAccumFinish(&acc);
  fprintf(stdout,"%s", zBuf);
  fflush(stdout);
}

/*
** Shorthand for starting a new tree item that consists of a single label
*/
static void sqlite3TreeViewItem(TreeView *p, const char *zLabel,u8 moreFollows){
  sqlite3TreeViewPush(&p, moreFollows);
  sqlite3TreeViewLine(p, "%s", zLabel);
}

/*
** Show a list of Column objects in tree format.
*/
void sqlite3TreeViewColumnList(
  TreeView *pView,
  const Column *aCol,
  int nCol,
  u8 moreToFollow
){
  int i;
  sqlite3TreeViewPush(&pView, moreToFollow);
  sqlite3TreeViewLine(pView, "COLUMNS");
  for(i=0; i<nCol; i++){
    u16 flg = aCol[i].colFlags;
    int colMoreToFollow = i<(nCol - 1);
    sqlite3TreeViewPush(&pView, colMoreToFollow);
    sqlite3TreeViewLine(pView, 0);
    printf(" %s", aCol[i].zCnName);
    switch( aCol[i].eCType ){
      case COLTYPE_ANY:      printf(" ANY");        break;
      case COLTYPE_BLOB:     printf(" BLOB");       break;
      case COLTYPE_INT:      printf(" INT");        break;
      case COLTYPE_INTEGER:  printf(" INTEGER");    break;
      case COLTYPE_REAL:     printf(" REAL");       break;
      case COLTYPE_TEXT:     printf(" TEXT");       break;
      case COLTYPE_CUSTOM: {
        if( flg & COLFLAG_HASTYPE ){
          const char *z = aCol[i].zCnName;
          z += strlen(z)+1;
          printf(" X-%s", z);
          break;
        }
      }
    }
    if( flg & COLFLAG_PRIMKEY ) printf(" PRIMARY KEY");
    if( flg & COLFLAG_HIDDEN ) printf(" HIDDEN");
#ifdef COLFLAG_NOEXPAND
    if( flg & COLFLAG_NOEXPAND ) printf(" NO-EXPAND");
#endif
    if( flg ) printf(" flags=%04x", flg);
    printf("\n");      
    fflush(stdout);
    sqlite3TreeViewPop(&pView);
  }
  sqlite3TreeViewPop(&pView);
}

/*
** Generate a human-readable description of a WITH clause.
*/
void sqlite3TreeViewWith(TreeView *pView, const With *pWith, u8 moreToFollow){
  int i;
  if( pWith==0 ) return;
  if( pWith->nCte==0 ) return;
  if( pWith->pOuter ){
    sqlite3TreeViewLine(pView, "WITH (0x%p, pOuter=0x%p)",pWith,pWith->pOuter);
  }else{
    sqlite3TreeViewLine(pView, "WITH (0x%p)", pWith);
  }
  if( pWith->nCte>0 ){
    sqlite3TreeViewPush(&pView, moreToFollow);
    for(i=0; i<pWith->nCte; i++){
      StrAccum x;
      char zLine[1000];
      const struct Cte *pCte = &pWith->a[i];
      sqlite3StrAccumInit(&x, 0, zLine, sizeof(zLine), 0);
      sqlite3_str_appendf(&x, "%s", pCte->zName);
      if( pCte->pCols && pCte->pCols->nExpr>0 ){
        char cSep = '(';
        int j;
        for(j=0; j<pCte->pCols->nExpr; j++){
          sqlite3_str_appendf(&x, "%c%s", cSep, pCte->pCols->a[j].zEName);
          cSep = ',';
        }
        sqlite3_str_appendf(&x, ")");
      }
      if( pCte->eM10d!=M10d_Any ){
        sqlite3_str_appendf(&x, " %sMATERIALIZED", 
           pCte->eM10d==M10d_No ? "NOT " : "");
      }
      if( pCte->pUse ){
        sqlite3_str_appendf(&x, " (pUse=0x%p, nUse=%d)", pCte->pUse,
                 pCte->pUse->nUse);
      }
      sqlite3StrAccumFinish(&x);
      sqlite3TreeViewItem(pView, zLine, i<pWith->nCte-1);
      sqlite3TreeViewSelect(pView, pCte->pSelect, 0);
      sqlite3TreeViewPop(&pView);
    }
    sqlite3TreeViewPop(&pView);
  }
}

/*
** Generate a human-readable description of a SrcList object.
*/
void sqlite3TreeViewSrcList(TreeView *pView, const SrcList *pSrc){
  int i;
  if( pSrc==0 ) return;
  for(i=0; i<pSrc->nSrc; i++){
    const SrcItem *pItem = &pSrc->a[i];
    StrAccum x;
    int n = 0;
    char zLine[1000];
    sqlite3StrAccumInit(&x, 0, zLine, sizeof(zLine), 0);
    x.printfFlags |= SQLITE_PRINTF_INTERNAL;
    sqlite3_str_appendf(&x, "{%d:*} %!S", pItem->iCursor, pItem);
    if( pItem->pSTab ){
      sqlite3_str_appendf(&x, " tab=%Q nCol=%d ptr=%p used=%llx%s",
           pItem->pSTab->zName, pItem->pSTab->nCol, pItem->pSTab, 
           pItem->colUsed,
           pItem->fg.rowidUsed ? "+rowid" : "");
    }
    if( (pItem->fg.jointype & (JT_LEFT|JT_RIGHT))==(JT_LEFT|JT_RIGHT) ){
      sqlite3_str_appendf(&x, " FULL-OUTER-JOIN");
    }else if( pItem->fg.jointype & JT_LEFT ){
      sqlite3_str_appendf(&x, " LEFT-JOIN");
    }else if( pItem->fg.jointype & JT_RIGHT ){
      sqlite3_str_appendf(&x, " RIGHT-JOIN");
    }else if( pItem->fg.jointype & JT_CROSS ){
      sqlite3_str_appendf(&x, " CROSS-JOIN");
    }
    if( pItem->fg.jointype & JT_LTORJ ){
      sqlite3_str_appendf(&x, " LTORJ");
    }
    if( pItem->fg.fromDDL ){
      sqlite3_str_appendf(&x, " DDL");
    }
    if( pItem->fg.isCte ){
      static const char *aMat[] = {",MAT", "", ",NO-MAT"};
      sqlite3_str_appendf(&x, " CteUse=%d%s",
                          pItem->u2.pCteUse->nUse,
                          aMat[pItem->u2.pCteUse->eM10d]);
    }
    if( pItem->fg.isOn || (pItem->fg.isUsing==0 && pItem->u3.pOn!=0) ){
      sqlite3_str_appendf(&x, " isOn");
    }
    if( pItem->fg.isTabFunc )      sqlite3_str_appendf(&x, " isTabFunc");
    if( pItem->fg.isCorrelated )   sqlite3_str_appendf(&x, " isCorrelated");
    if( pItem->fg.isMaterialized ) sqlite3_str_appendf(&x, " isMaterialized");
    if( pItem->fg.viaCoroutine )   sqlite3_str_appendf(&x, " viaCoroutine");
    if( pItem->fg.notCte )         sqlite3_str_appendf(&x, " notCte");
    if( pItem->fg.isNestedFrom )   sqlite3_str_appendf(&x, " isNestedFrom");
    if( pItem->fg.fixedSchema )    sqlite3_str_appendf(&x, " fixedSchema");
    if( pItem->fg.hadSchema )      sqlite3_str_appendf(&x, " hadSchema");
    if( pItem->fg.isSubquery )     sqlite3_str_appendf(&x, " isSubquery");

    sqlite3StrAccumFinish(&x);
    sqlite3TreeViewItem(pView, zLine, i<pSrc->nSrc-1);
    n = 0;
    if( pItem->fg.isSubquery ) n++;
    if( pItem->fg.isTabFunc ) n++;
    if( pItem->fg.isUsing || pItem->u3.pOn!=0 ) n++;
    if( pItem->fg.isUsing ){
      sqlite3TreeViewIdList(pView, pItem->u3.pUsing, (--n)>0, "USING");
    }else if( pItem->u3.pOn!=0 ){
      sqlite3TreeViewItem(pView, "ON", (--n)>0);
      sqlite3TreeViewExpr(pView, pItem->u3.pOn, 0);
      sqlite3TreeViewPop(&pView);
    }
    if( pItem->fg.isSubquery ){
      assert( n==1 );
      if( pItem->pSTab ){
        Table *pTab = pItem->pSTab;
        sqlite3TreeViewColumnList(pView, pTab->aCol, pTab->nCol, 1);
      }
      assert( (int)pItem->fg.isNestedFrom == IsNestedFrom(pItem) );
      sqlite3TreeViewSelect(pView, pItem->u4.pSubq->pSelect, 0);
    }
    if( pItem->fg.isTabFunc ){
      sqlite3TreeViewExprList(pView, pItem->u1.pFuncArg, 0, "func-args:");
    }
    sqlite3TreeViewPop(&pView);
  }
}

/*
** Generate a human-readable description of a Select object.
*/
void sqlite3TreeViewSelect(TreeView *pView, const Select *p, u8 moreToFollow){
  int n = 0;
  int cnt = 0;
  if( p==0 ){
    sqlite3TreeViewLine(pView, "nil-SELECT");
    return;
  } 
  sqlite3TreeViewPush(&pView, moreToFollow);
  if( p->pWith ){
    sqlite3TreeViewWith(pView, p->pWith, 1);
    cnt = 1;
    sqlite3TreeViewPush(&pView, 1);
  }
  do{
    if( p->selFlags & SF_WhereBegin ){
      sqlite3TreeViewLine(pView, "sqlite3WhereBegin()");
    }else{
      sqlite3TreeViewLine(pView,
        "SELECT%s%s (%u/%p) selFlags=0x%x nSelectRow=%d",
        ((p->selFlags & SF_Distinct) ? " DISTINCT" : ""),
        ((p->selFlags & SF_Aggregate) ? " agg_flag" : ""),
        p->selId, p, p->selFlags,
        (int)p->nSelectRow
      );
    }
    if( cnt++ ) sqlite3TreeViewPop(&pView);
    if( p->pPrior ){
      n = 1000;
    }else{
      n = 0;
      if( p->pSrc && p->pSrc->nSrc && p->pSrc->nAlloc ) n++;
      if( p->pWhere ) n++;
      if( p->pGroupBy ) n++;
      if( p->pHaving ) n++;
      if( p->pOrderBy ) n++;
      if( p->pLimit ) n++;
#ifndef SQLITE_OMIT_WINDOWFUNC
      if( p->pWin ) n++;
      if( p->pWinDefn ) n++;
#endif
    }
    if( p->pEList ){
      sqlite3TreeViewExprList(pView, p->pEList, n>0, "result-set");
    }
    n--;
#ifndef SQLITE_OMIT_WINDOWFUNC
    if( p->pWin ){
      Window *pX;
      sqlite3TreeViewPush(&pView, (n--)>0);
      sqlite3TreeViewLine(pView, "window-functions");
      for(pX=p->pWin; pX; pX=pX->pNextWin){
        sqlite3TreeViewWinFunc(pView, pX, pX->pNextWin!=0);
      }
      sqlite3TreeViewPop(&pView);
    }
#endif
    if( p->pSrc && p->pSrc->nSrc && p->pSrc->nAlloc ){
      sqlite3TreeViewPush(&pView, (n--)>0);
      sqlite3TreeViewLine(pView, "FROM");
      sqlite3TreeViewSrcList(pView, p->pSrc);
      sqlite3TreeViewPop(&pView);
    }
    if( p->pWhere ){
      sqlite3TreeViewItem(pView, "WHERE", (n--)>0);
      sqlite3TreeViewExpr(pView, p->pWhere, 0);
      sqlite3TreeViewPop(&pView);
    }
    if( p->pGroupBy ){
      sqlite3TreeViewExprList(pView, p->pGroupBy, (n--)>0, "GROUPBY");
    }
    if( p->pHaving ){
      sqlite3TreeViewItem(pView, "HAVING", (n--)>0);
      sqlite3TreeViewExpr(pView, p->pHaving, 0);
      sqlite3TreeViewPop(&pView);
    }
#ifndef SQLITE_OMIT_WINDOWFUNC
    if( p->pWinDefn ){
      Window *pX;
      sqlite3TreeViewItem(pView, "WINDOW", (n--)>0);
      for(pX=p->pWinDefn; pX; pX=pX->pNextWin){
        sqlite3TreeViewWindow(pView, pX, pX->pNextWin!=0);
      }
      sqlite3TreeViewPop(&pView);
    }
#endif
    if( p->pOrderBy ){
      sqlite3TreeViewExprList(pView, p->pOrderBy, (n--)>0, "ORDERBY");
    }
    if( p->pLimit ){
      sqlite3TreeViewItem(pView, "LIMIT", (n--)>0);
      sqlite3TreeViewExpr(pView, p->pLimit->pLeft, p->pLimit->pRight!=0);
      if( p->pLimit->pRight ){
        sqlite3TreeViewItem(pView, "OFFSET", 0);
        sqlite3TreeViewExpr(pView, p->pLimit->pRight, 0);
        sqlite3TreeViewPop(&pView);
      }
      sqlite3TreeViewPop(&pView);
    }
    if( p->pPrior ){
      const char *zOp = "UNION";
      switch( p->op ){
        case TK_ALL:         zOp = "UNION ALL";  break;
        case TK_INTERSECT:   zOp = "INTERSECT";  break;
        case TK_EXCEPT:      zOp = "EXCEPT";     break;
      }
      sqlite3TreeViewItem(pView, zOp, 1);
    }
    p = p->pPrior;
  }while( p!=0 );
  sqlite3TreeViewPop(&pView);
}

#ifndef SQLITE_OMIT_WINDOWFUNC
/*
** Generate a description of starting or stopping bounds
*/
void sqlite3TreeViewBound(
  TreeView *pView,        /* View context */
  u8 eBound,              /* UNBOUNDED, CURRENT, PRECEDING, FOLLOWING */
  Expr *pExpr,            /* Value for PRECEDING or FOLLOWING */
  u8 moreToFollow         /* True if more to follow */
){
  switch( eBound ){
    case TK_UNBOUNDED: {
      sqlite3TreeViewItem(pView, "UNBOUNDED", moreToFollow);
      sqlite3TreeViewPop(&pView);
      break;
    }
    case TK_CURRENT: {
      sqlite3TreeViewItem(pView, "CURRENT", moreToFollow);
      sqlite3TreeViewPop(&pView);
      break;
    }
    case TK_PRECEDING: {
      sqlite3TreeViewItem(pView, "PRECEDING", moreToFollow);
      sqlite3TreeViewExpr(pView, pExpr, 0);
      sqlite3TreeViewPop(&pView);
      break;
    }
    case TK_FOLLOWING: {
      sqlite3TreeViewItem(pView, "FOLLOWING", moreToFollow);
      sqlite3TreeViewExpr(pView, pExpr, 0);
      sqlite3TreeViewPop(&pView);
      break;
    }
  }
}
#endif /* SQLITE_OMIT_WINDOWFUNC */

#ifndef SQLITE_OMIT_WINDOWFUNC
/*
** Generate a human-readable explanation for a Window object
*/
void sqlite3TreeViewWindow(TreeView *pView, const Window *pWin, u8 more){
  int nElement = 0;
  if( pWin==0 ) return;
  if( pWin->pFilter ){
    sqlite3TreeViewItem(pView, "FILTER", 1);
    sqlite3TreeViewExpr(pView, pWin->pFilter, 0);
    sqlite3TreeViewPop(&pView);
    if( pWin->eFrmType==TK_FILTER ) return;
  }
  sqlite3TreeViewPush(&pView, more);
  if( pWin->zName ){
    sqlite3TreeViewLine(pView, "OVER %s (%p)", pWin->zName, pWin);
  }else{
    sqlite3TreeViewLine(pView, "OVER (%p)", pWin);
  }
  if( pWin->zBase )    nElement++;
  if( pWin->pOrderBy ) nElement++;
  if( pWin->eFrmType!=0 && pWin->eFrmType!=TK_FILTER ) nElement++;
  if( pWin->eExclude ) nElement++;
  if( pWin->zBase ){
    sqlite3TreeViewPush(&pView, (--nElement)>0);
    sqlite3TreeViewLine(pView, "window: %s", pWin->zBase);
    sqlite3TreeViewPop(&pView);
  }
  if( pWin->pPartition ){
    sqlite3TreeViewExprList(pView, pWin->pPartition, nElement>0,"PARTITION-BY");
  }
  if( pWin->pOrderBy ){
    sqlite3TreeViewExprList(pView, pWin->pOrderBy, (--nElement)>0, "ORDER-BY");
  }
  if( pWin->eFrmType!=0 && pWin->eFrmType!=TK_FILTER ){
    char zBuf[30];
    const char *zFrmType = "ROWS";
    if( pWin->eFrmType==TK_RANGE ) zFrmType = "RANGE";
    if( pWin->eFrmType==TK_GROUPS ) zFrmType = "GROUPS";
    sqlite3_snprintf(sizeof(zBuf),zBuf,"%s%s",zFrmType,
        pWin->bImplicitFrame ? " (implied)" : "");
    sqlite3TreeViewItem(pView, zBuf, (--nElement)>0);
    sqlite3TreeViewBound(pView, pWin->eStart, pWin->pStart, 1);
    sqlite3TreeViewBound(pView, pWin->eEnd, pWin->pEnd, 0);
    sqlite3TreeViewPop(&pView);
  }
  if( pWin->eExclude ){
    char zBuf[30];
    const char *zExclude;
    switch( pWin->eExclude ){
      case TK_NO:      zExclude = "NO OTHERS";   break;
      case TK_CURRENT: zExclude = "CURRENT ROW"; break;
      case TK_GROUP:   zExclude = "GROUP";       break;
      case TK_TIES:    zExclude = "TIES";        break;
      default:
        sqlite3_snprintf(sizeof(zBuf),zBuf,"invalid(%d)", pWin->eExclude);
        zExclude = zBuf;
        break;
    }
    sqlite3TreeViewPush(&pView, 0);
    sqlite3TreeViewLine(pView, "EXCLUDE %s", zExclude);
    sqlite3TreeViewPop(&pView);
  }
  sqlite3TreeViewPop(&pView);
}
#endif /* SQLITE_OMIT_WINDOWFUNC */

#ifndef SQLITE_OMIT_WINDOWFUNC
/*
** Generate a human-readable explanation for a Window Function object
*/
void sqlite3TreeViewWinFunc(TreeView *pView, const Window *pWin, u8 more){
  if( pWin==0 ) return;
  sqlite3TreeViewPush(&pView, more);
  sqlite3TreeViewLine(pView, "WINFUNC %s(%d)",
                       pWin->pWFunc->zName, pWin->pWFunc->nArg);
  sqlite3TreeViewWindow(pView, pWin, 0);
  sqlite3TreeViewPop(&pView);
}
#endif /* SQLITE_OMIT_WINDOWFUNC */

/*
** Generate a human-readable explanation of an expression tree.
*/
void sqlite3TreeViewExpr(TreeView *pView, const Expr *pExpr, u8 moreToFollow){
  const char *zBinOp = 0;   /* Binary operator */
  const char *zUniOp = 0;   /* Unary operator */
  char zFlgs[200];
  sqlite3TreeViewPush(&pView, moreToFollow);
  if( pExpr==0 ){
    sqlite3TreeViewLine(pView, "nil");
    sqlite3TreeViewPop(&pView);
    return;
  }
  if( pExpr->flags || pExpr->affExpr || pExpr->vvaFlags || pExpr->pAggInfo ){
    StrAccum x;
    sqlite3StrAccumInit(&x, 0, zFlgs, sizeof(zFlgs), 0);
    sqlite3_str_appendf(&x, " fg.af=%x.%c",
      pExpr->flags, pExpr->affExpr ? pExpr->affExpr : 'n');
    if( ExprHasProperty(pExpr, EP_OuterON) ){
      sqlite3_str_appendf(&x, " outer.iJoin=%d", pExpr->w.iJoin);
    }
    if( ExprHasProperty(pExpr, EP_InnerON) ){
      sqlite3_str_appendf(&x, " inner.iJoin=%d", pExpr->w.iJoin);
    }
    if( ExprHasProperty(pExpr, EP_FromDDL) ){
      sqlite3_str_appendf(&x, " DDL");
    }
    if( ExprHasVVAProperty(pExpr, EP_Immutable) ){
      sqlite3_str_appendf(&x, " IMMUTABLE");
    }
    if( pExpr->pAggInfo!=0 ){
      sqlite3_str_appendf(&x, " agg-column[%d]", pExpr->iAgg);
    }
    sqlite3StrAccumFinish(&x);
  }else{
    zFlgs[0] = 0;
  }
  switch( pExpr->op ){
    case TK_AGG_COLUMN: {
      sqlite3TreeViewLine(pView, "AGG{%d:%d}%s",
            pExpr->iTable, pExpr->iColumn, zFlgs);
      break;
    }
    case TK_COLUMN: {
      if( pExpr->iTable<0 ){
        /* This only happens when coding check constraints */
        char zOp2[16];
        if( pExpr->op2 ){
          sqlite3_snprintf(sizeof(zOp2),zOp2," op2=0x%02x",pExpr->op2);
        }else{
          zOp2[0] = 0;
        }
        sqlite3TreeViewLine(pView, "COLUMN(%d)%s%s",
                                    pExpr->iColumn, zFlgs, zOp2);
      }else{
        assert( ExprUseYTab(pExpr) );
        sqlite3TreeViewLine(pView, "{%d:%d} pTab=%p%s",
                        pExpr->iTable, pExpr->iColumn,
                        pExpr->y.pTab, zFlgs);
      }
      if( ExprHasProperty(pExpr, EP_FixedCol) ){
        sqlite3TreeViewExpr(pView, pExpr->pLeft, 0);
      }
      break;
    }
    case TK_INTEGER: {
      if( pExpr->flags & EP_IntValue ){
        sqlite3TreeViewLine(pView, "%d", pExpr->u.iValue);
      }else{
        sqlite3TreeViewLine(pView, "%s", pExpr->u.zToken);
      }
      break;
    }
#ifndef SQLITE_OMIT_FLOATING_POINT
    case TK_FLOAT: {
      assert( !ExprHasProperty(pExpr, EP_IntValue) );
      sqlite3TreeViewLine(pView,"%s", pExpr->u.zToken);
      break;
    }
#endif
    case TK_STRING: {
      assert( !ExprHasProperty(pExpr, EP_IntValue) );
      sqlite3TreeViewLine(pView,"%Q", pExpr->u.zToken);
      break;
    }
    case TK_NULL: {
      sqlite3TreeViewLine(pView,"NULL");
      break;
    }
    case TK_TRUEFALSE: {
      sqlite3TreeViewLine(pView,"%s%s",
         sqlite3ExprTruthValue(pExpr) ? "TRUE" : "FALSE", zFlgs);
      break;
    }
#ifndef SQLITE_OMIT_BLOB_LITERAL
    case TK_BLOB: {
      assert( !ExprHasProperty(pExpr, EP_IntValue) );
      sqlite3TreeViewLine(pView,"%s", pExpr->u.zToken);
      break;
    }
#endif
    case TK_VARIABLE: {
      assert( !ExprHasProperty(pExpr, EP_IntValue) );
      sqlite3TreeViewLine(pView,"VARIABLE(%s,%d)",
                          pExpr->u.zToken, pExpr->iColumn);
      break;
    }
    case TK_REGISTER: {
      sqlite3TreeViewLine(pView,"REGISTER(%d)", pExpr->iTable);
      break;
    }
    case TK_ID: {
      assert( !ExprHasProperty(pExpr, EP_IntValue) );
      sqlite3TreeViewLine(pView,"ID \"%w\"", pExpr->u.zToken);
      break;
    }
    case TK_ASTERISK: {
      sqlite3TreeViewLine(pView,"ASKTERISK");
      break;
    }
#ifndef SQLITE_OMIT_CAST
    case TK_CAST: {
      /* Expressions of the form:   CAST(pLeft AS token) */
      assert( !ExprHasProperty(pExpr, EP_IntValue) );
      sqlite3TreeViewLine(pView,"CAST %Q", pExpr->u.zToken);
      sqlite3TreeViewExpr(pView, pExpr->pLeft, 0);
      break;
    }
#endif /* SQLITE_OMIT_CAST */
    case TK_LT:      zBinOp = "LT";     break;
    case TK_LE:      zBinOp = "LE";     break;
    case TK_GT:      zBinOp = "GT";     break;
    case TK_GE:      zBinOp = "GE";     break;
    case TK_NE:      zBinOp = "NE";     break;
    case TK_EQ:      zBinOp = "EQ";     break;
    case TK_IS:      zBinOp = "IS";     break;
    case TK_ISNOT:   zBinOp = "ISNOT";  break;
    case TK_AND:     zBinOp = "AND";    break;
    case TK_OR:      zBinOp = "OR";     break;
    case TK_PLUS:    zBinOp = "ADD";    break;
    case TK_STAR:    zBinOp = "MUL";    break;
    case TK_MINUS:   zBinOp = "SUB";    break;
    case TK_REM:     zBinOp = "REM";    break;
    case TK_BITAND:  zBinOp = "BITAND"; break;
    case TK_BITOR:   zBinOp = "BITOR";  break;
    case TK_SLASH:   zBinOp = "DIV";    break;
    case TK_LSHIFT:  zBinOp = "LSHIFT"; break;
    case TK_RSHIFT:  zBinOp = "RSHIFT"; break;
    case TK_CONCAT:  zBinOp = "CONCAT"; break;
    case TK_DOT:     zBinOp = "DOT";    break;
    case TK_LIMIT:   zBinOp = "LIMIT";  break;

    case TK_UMINUS:  zUniOp = "UMINUS"; break;
    case TK_UPLUS:   zUniOp = "UPLUS";  break;
    case TK_BITNOT:  zUniOp = "BITNOT"; break;
    case TK_NOT:     zUniOp = "NOT";    break;
    case TK_ISNULL:  zUniOp = "ISNULL"; break;
    case TK_NOTNULL: zUniOp = "NOTNULL"; break;

    case TK_TRUTH: {
      int x;
      const char *azOp[] = {
         "IS-FALSE", "IS-TRUE", "IS-NOT-FALSE", "IS-NOT-TRUE"
      };
      assert( pExpr->op2==TK_IS || pExpr->op2==TK_ISNOT );
      assert( pExpr->pRight );
      assert( sqlite3ExprSkipCollateAndLikely(pExpr->pRight)->op
                  == TK_TRUEFALSE );
      x = (pExpr->op2==TK_ISNOT)*2 + sqlite3ExprTruthValue(pExpr->pRight);
      zUniOp = azOp[x];
      break;
    }

    case TK_SPAN: {
      assert( !ExprHasProperty(pExpr, EP_IntValue) );
      sqlite3TreeViewLine(pView, "SPAN %Q", pExpr->u.zToken);
      sqlite3TreeViewExpr(pView, pExpr->pLeft, 0);
      break;
    }

    case TK_COLLATE: {
      /* COLLATE operators without the EP_Collate flag are intended to
      ** emulate collation associated with a table column.  These show
      ** up in the treeview output as "SOFT-COLLATE".  Explicit COLLATE
      ** operators that appear in the original SQL always have the
      ** EP_Collate bit set and appear in treeview output as just "COLLATE" */
      assert( !ExprHasProperty(pExpr, EP_IntValue) );
      sqlite3TreeViewLine(pView, "%sCOLLATE %Q%s",
        !ExprHasProperty(pExpr, EP_Collate) ? "SOFT-" : "",
        pExpr->u.zToken, zFlgs);
      sqlite3TreeViewExpr(pView, pExpr->pLeft, 0);
      break;
    }

    case TK_AGG_FUNCTION:
    case TK_FUNCTION: {
      ExprList *pFarg;       /* List of function arguments */
      Window *pWin;
      if( ExprHasProperty(pExpr, EP_TokenOnly) ){
        pFarg = 0;
        pWin = 0;
      }else{
        assert( ExprUseXList(pExpr) );
        pFarg = pExpr->x.pList;
#ifndef SQLITE_OMIT_WINDOWFUNC
        pWin = IsWindowFunc(pExpr) ? pExpr->y.pWin : 0;
#else
        pWin = 0;
#endif 
      }
      assert( !ExprHasProperty(pExpr, EP_IntValue) );
      if( pExpr->op==TK_AGG_FUNCTION ){
        sqlite3TreeViewLine(pView, "AGG_FUNCTION%d %Q%s agg=%d[%d]/%p",
                             pExpr->op2, pExpr->u.zToken, zFlgs,
                             pExpr->pAggInfo ? pExpr->pAggInfo->selId : 0,
                             pExpr->iAgg, pExpr->pAggInfo);
      }else if( pExpr->op2!=0 ){
        const char *zOp2;
        char zBuf[8];
        sqlite3_snprintf(sizeof(zBuf),zBuf,"0x%02x",pExpr->op2);
        zOp2 = zBuf;
        if( pExpr->op2==NC_IsCheck ) zOp2 = "NC_IsCheck";
        if( pExpr->op2==NC_IdxExpr ) zOp2 = "NC_IdxExpr";
        if( pExpr->op2==NC_PartIdx ) zOp2 = "NC_PartIdx";
        if( pExpr->op2==NC_GenCol ) zOp2 = "NC_GenCol";
        sqlite3TreeViewLine(pView, "FUNCTION %Q%s op2=%s",
                            pExpr->u.zToken, zFlgs, zOp2);
      }else{
        sqlite3TreeViewLine(pView, "FUNCTION %Q%s", pExpr->u.zToken, zFlgs);
      }
      if( pFarg ){
        sqlite3TreeViewExprList(pView, pFarg, pWin!=0 || pExpr->pLeft, 0);
        if( pExpr->pLeft ){
          Expr *pOB = pExpr->pLeft;
          assert( pOB->op==TK_ORDER );
          assert( ExprUseXList(pOB) );
          sqlite3TreeViewExprList(pView, pOB->x.pList, pWin!=0, "ORDERBY");
        }
      }
#ifndef SQLITE_OMIT_WINDOWFUNC
      if( pWin ){
        sqlite3TreeViewWindow(pView, pWin, 0);
      }
#endif
      break;
    }
    case TK_ORDER: {
      sqlite3TreeViewExprList(pView, pExpr->x.pList, 0, "ORDERBY");
      break;
    }
#ifndef SQLITE_OMIT_SUBQUERY
    case TK_EXISTS: {
      assert( ExprUseXSelect(pExpr) );
      sqlite3TreeViewLine(pView, "EXISTS-expr flags=0x%x", pExpr->flags);
      sqlite3TreeViewSelect(pView, pExpr->x.pSelect, 0);
      break;
    }
    case TK_SELECT: {
      assert( ExprUseXSelect(pExpr) );
      sqlite3TreeViewLine(pView, "subquery-expr flags=0x%x", pExpr->flags);
      sqlite3TreeViewSelect(pView, pExpr->x.pSelect, 0);
      break;
    }
    case TK_IN: {
      sqlite3_str *pStr = sqlite3_str_new(0);
      char *z;
      sqlite3_str_appendf(pStr, "IN flags=0x%x", pExpr->flags);
      if( pExpr->iTable ) sqlite3_str_appendf(pStr, " iTable=%d",pExpr->iTable);
      if( ExprHasProperty(pExpr, EP_Subrtn) ){
        sqlite3_str_appendf(pStr, " subrtn(%d,%d)",
            pExpr->y.sub.regReturn, pExpr->y.sub.iAddr);
      }
      z = sqlite3_str_finish(pStr);
      sqlite3TreeViewLine(pView, z);
      sqlite3_free(z);
      sqlite3TreeViewExpr(pView, pExpr->pLeft, 1);
      if( ExprUseXSelect(pExpr) ){
        sqlite3TreeViewSelect(pView, pExpr->x.pSelect, 0);
      }else{
        sqlite3TreeViewExprList(pView, pExpr->x.pList, 0, 0);
      }
      break;
    }
#endif /* SQLITE_OMIT_SUBQUERY */

    /*
    **    x BETWEEN y AND z
    **
    ** This is equivalent to
    **
    **    x>=y AND x<=z
    **
    ** X is stored in pExpr->pLeft.
    ** Y is stored in pExpr->pList->a[0].pExpr.
    ** Z is stored in pExpr->pList->a[1].pExpr.
    */
    case TK_BETWEEN: {
      const Expr *pX, *pY, *pZ;
      pX = pExpr->pLeft;
      assert( ExprUseXList(pExpr) );
      assert( pExpr->x.pList->nExpr==2 );
      pY = pExpr->x.pList->a[0].pExpr;
      pZ = pExpr->x.pList->a[1].pExpr;
      sqlite3TreeViewLine(pView, "BETWEEN%s", zFlgs);
      sqlite3TreeViewExpr(pView, pX, 1);
      sqlite3TreeViewExpr(pView, pY, 1);
      sqlite3TreeViewExpr(pView, pZ, 0);
      break;
    }
    case TK_TRIGGER: {
      /* If the opcode is TK_TRIGGER, then the expression is a reference
      ** to a column in the new.* or old.* pseudo-tables available to
      ** trigger programs. In this case Expr.iTable is set to 1 for the
      ** new.* pseudo-table, or 0 for the old.* pseudo-table. Expr.iColumn
      ** is set to the column of the pseudo-table to read, or to -1 to
      ** read the rowid field.
      */
      sqlite3TreeViewLine(pView, "%s(%d)", 
          pExpr->iTable ? "NEW" : "OLD", pExpr->iColumn);
      break;
    }
    case TK_CASE: {
      sqlite3TreeViewLine(pView, "CASE");
      sqlite3TreeViewExpr(pView, pExpr->pLeft, 1);
      assert( ExprUseXList(pExpr) );
      sqlite3TreeViewExprList(pView, pExpr->x.pList, 0, 0);
      break;
    }
#ifndef SQLITE_OMIT_TRIGGER
    case TK_RAISE: {
      const char *zType = "unk";
      switch( pExpr->affExpr ){
        case OE_Rollback:   zType = "rollback";  break;
        case OE_Abort:      zType = "abort";     break;
        case OE_Fail:       zType = "fail";      break;
        case OE_Ignore:     zType = "ignore";    break;
      }
      assert( !ExprHasProperty(pExpr, EP_IntValue) );
      sqlite3TreeViewLine(pView, "RAISE %s", zType);
      sqlite3TreeViewExpr(pView, pExpr->pLeft, 0);
      break;
    }
#endif
    case TK_MATCH: {
      sqlite3TreeViewLine(pView, "MATCH {%d:%d}%s",
                          pExpr->iTable, pExpr->iColumn, zFlgs);
      sqlite3TreeViewExpr(pView, pExpr->pRight, 0);
      break;
    }
    case TK_VECTOR: {
      char *z = sqlite3_mprintf("VECTOR%s",zFlgs);
      assert( ExprUseXList(pExpr) );
      sqlite3TreeViewBareExprList(pView, pExpr->x.pList, z);
      sqlite3_free(z);
      break;
    }
    case TK_SELECT_COLUMN: {
      sqlite3TreeViewLine(pView, "SELECT-COLUMN %d of [0..%d]%s",
              pExpr->iColumn, pExpr->iTable-1,
              pExpr->pRight==pExpr->pLeft ? " (SELECT-owner)" : "");
      assert( ExprUseXSelect(pExpr->pLeft) );
      sqlite3TreeViewSelect(pView, pExpr->pLeft->x.pSelect, 0);
      break;
    }
    case TK_IF_NULL_ROW: {
      sqlite3TreeViewLine(pView, "IF-NULL-ROW %d", pExpr->iTable);
      sqlite3TreeViewExpr(pView, pExpr->pLeft, 0);
      break;
    }
    case TK_ERROR: {
      Expr tmp;
      sqlite3TreeViewLine(pView, "ERROR");
      tmp = *pExpr;
      tmp.op = pExpr->op2;
      sqlite3TreeViewExpr(pView, &tmp, 0);
      break;
    }
    case TK_ROW: {
      if( pExpr->iColumn<=0 ){
        sqlite3TreeViewLine(pView, "First FROM table rowid");
      }else{
        sqlite3TreeViewLine(pView, "First FROM table column %d",
            pExpr->iColumn-1);
      }
      break;
    }
    default: {
      sqlite3TreeViewLine(pView, "op=%d", pExpr->op);
      break;
    }
  }
  if( zBinOp ){
    sqlite3TreeViewLine(pView, "%s%s", zBinOp, zFlgs);
    sqlite3TreeViewExpr(pView, pExpr->pLeft, 1);
    sqlite3TreeViewExpr(pView, pExpr->pRight, 0);
  }else if( zUniOp ){
    sqlite3TreeViewLine(pView, "%s%s", zUniOp, zFlgs);
   sqlite3TreeViewExpr(pView, pExpr->pLeft, 0);
  }
  sqlite3TreeViewPop(&pView);
}


/*
** Generate a human-readable explanation of an expression list.
*/
void sqlite3TreeViewBareExprList(
  TreeView *pView,
  const ExprList *pList,
  const char *zLabel
){
  if( zLabel==0 || zLabel[0]==0 ) zLabel = "LIST";
  if( pList==0 ){
    sqlite3TreeViewLine(pView, "%s (empty)", zLabel);
  }else{
    int i;
    sqlite3TreeViewLine(pView, "%s", zLabel);
    for(i=0; i<pList->nExpr; i++){
      int j = pList->a[i].u.x.iOrderByCol;
      u8 sortFlags = pList->a[i].fg.sortFlags;
      char *zName = pList->a[i].zEName;
      int moreToFollow = i<pList->nExpr - 1;
      if( j || zName || sortFlags ){
        sqlite3TreeViewPush(&pView, moreToFollow);
        moreToFollow = 0;
        sqlite3TreeViewLine(pView, 0);
        if( zName ){
          switch( pList->a[i].fg.eEName ){
            default:
              fprintf(stdout, "AS %s ", zName);
              break;
            case ENAME_TAB:
              fprintf(stdout, "TABLE-ALIAS-NAME(\"%s\") ", zName);
              if( pList->a[i].fg.bUsed ) fprintf(stdout, "(used) ");
              if( pList->a[i].fg.bUsingTerm ) fprintf(stdout, "(USING-term) ");
              if( pList->a[i].fg.bNoExpand ) fprintf(stdout, "(NoExpand) ");
              break;
            case ENAME_SPAN:
              fprintf(stdout, "SPAN(\"%s\") ", zName);
              break;
          }
        }
        if( j ){
          fprintf(stdout, "iOrderByCol=%d ", j);
        }
        if( sortFlags & KEYINFO_ORDER_DESC ){
          fprintf(stdout, "DESC ");
        }else if( sortFlags & KEYINFO_ORDER_BIGNULL ){
          fprintf(stdout, "NULLS-LAST");
        }
        fprintf(stdout, "\n");
        fflush(stdout);
      }
      sqlite3TreeViewExpr(pView, pList->a[i].pExpr, moreToFollow);
      if( j || zName || sortFlags ){
        sqlite3TreeViewPop(&pView);
      }
    }
  }
}
void sqlite3TreeViewExprList(
  TreeView *pView,
  const ExprList *pList,
  u8 moreToFollow,
  const char *zLabel
){
  sqlite3TreeViewPush(&pView, moreToFollow);
  sqlite3TreeViewBareExprList(pView, pList, zLabel);
  sqlite3TreeViewPop(&pView);
}

/*
** Generate a human-readable explanation of an id-list.
*/
void sqlite3TreeViewBareIdList(
  TreeView *pView,
  const IdList *pList,
  const char *zLabel
){
  if( zLabel==0 || zLabel[0]==0 ) zLabel = "LIST";
  if( pList==0 ){
    sqlite3TreeViewLine(pView, "%s (empty)", zLabel);
  }else{
    int i;
    sqlite3TreeViewLine(pView, "%s", zLabel);
    for(i=0; i<pList->nId; i++){
      char *zName = pList->a[i].zName;
      int moreToFollow = i<pList->nId - 1;
      if( zName==0 ) zName = "(null)";
      sqlite3TreeViewPush(&pView, moreToFollow);
      sqlite3TreeViewLine(pView, 0);
      fprintf(stdout, "%s\n", zName);
      sqlite3TreeViewPop(&pView);
    }
  }
}
void sqlite3TreeViewIdList(
  TreeView *pView,
  const IdList *pList,
  u8 moreToFollow,
  const char *zLabel
){
  sqlite3TreeViewPush(&pView, moreToFollow);
  sqlite3TreeViewBareIdList(pView, pList, zLabel);
  sqlite3TreeViewPop(&pView);
}

/*
** Generate a human-readable explanation of a list of Upsert objects
*/
void sqlite3TreeViewUpsert(
  TreeView *pView,
  const Upsert *pUpsert,
  u8 moreToFollow
){
  if( pUpsert==0 ) return;
  sqlite3TreeViewPush(&pView, moreToFollow);
  while( pUpsert ){
    int n;
    sqlite3TreeViewPush(&pView, pUpsert->pNextUpsert!=0 || moreToFollow);
    sqlite3TreeViewLine(pView, "ON CONFLICT DO %s", 
         pUpsert->isDoUpdate ? "UPDATE" : "NOTHING");
    n = (pUpsert->pUpsertSet!=0) + (pUpsert->pUpsertWhere!=0);
    sqlite3TreeViewExprList(pView, pUpsert->pUpsertTarget, (n--)>0, "TARGET");
    sqlite3TreeViewExprList(pView, pUpsert->pUpsertSet, (n--)>0, "SET");
    if( pUpsert->pUpsertWhere ){
      sqlite3TreeViewItem(pView, "WHERE", (n--)>0);
      sqlite3TreeViewExpr(pView, pUpsert->pUpsertWhere, 0);
      sqlite3TreeViewPop(&pView);
    }
    sqlite3TreeViewPop(&pView);
    pUpsert = pUpsert->pNextUpsert;
  }
  sqlite3TreeViewPop(&pView);
}

#if TREETRACE_ENABLED
/*
** Generate a human-readable diagram of the data structure that go
** into generating an DELETE statement.
*/
void sqlite3TreeViewDelete(
  const With *pWith,
  const SrcList *pTabList,
  const Expr *pWhere,
  const ExprList *pOrderBy,
  const Expr *pLimit,
  const Trigger *pTrigger
){
  int n = 0;
  TreeView *pView = 0;
  sqlite3TreeViewPush(&pView, 0);
  sqlite3TreeViewLine(pView, "DELETE");
  if( pWith ) n++;
  if( pTabList ) n++;
  if( pWhere ) n++;
  if( pOrderBy ) n++;
  if( pLimit ) n++;
  if( pTrigger ) n++;
  if( pWith ){
    sqlite3TreeViewPush(&pView, (--n)>0);
    sqlite3TreeViewWith(pView, pWith, 0);
    sqlite3TreeViewPop(&pView);
  }
  if( pTabList ){
    sqlite3TreeViewPush(&pView, (--n)>0);
    sqlite3TreeViewLine(pView, "FROM");
    sqlite3TreeViewSrcList(pView, pTabList);
    sqlite3TreeViewPop(&pView);
  }
  if( pWhere ){
    sqlite3TreeViewPush(&pView, (--n)>0);
    sqlite3TreeViewLine(pView, "WHERE");
    sqlite3TreeViewExpr(pView, pWhere, 0);
    sqlite3TreeViewPop(&pView);
  }
  if( pOrderBy ){
    sqlite3TreeViewExprList(pView, pOrderBy, (--n)>0, "ORDER-BY");
  }
  if( pLimit ){
    sqlite3TreeViewPush(&pView, (--n)>0);
    sqlite3TreeViewLine(pView, "LIMIT");
    sqlite3TreeViewExpr(pView, pLimit, 0);
    sqlite3TreeViewPop(&pView);
  }
  if( pTrigger ){
    sqlite3TreeViewTrigger(pView, pTrigger, (--n)>0, 1);
  }
  sqlite3TreeViewPop(&pView);
}
#endif /* TREETRACE_ENABLED */

#if TREETRACE_ENABLED
/*
** Generate a human-readable diagram of the data structure that go
** into generating an INSERT statement.
*/
void sqlite3TreeViewInsert(
  const With *pWith,
  const SrcList *pTabList,
  const IdList *pColumnList,
  const Select *pSelect,
  const ExprList *pExprList,
  int onError,
  const Upsert *pUpsert,
  const Trigger *pTrigger
){
  TreeView *pView = 0;
  int n = 0;
  const char *zLabel = "INSERT";
  switch( onError ){
    case OE_Replace:  zLabel = "REPLACE";             break;
    case OE_Ignore:   zLabel = "INSERT OR IGNORE";    break;
    case OE_Rollback: zLabel = "INSERT OR ROLLBACK";  break;
    case OE_Abort:    zLabel = "INSERT OR ABORT";     break;
    case OE_Fail:     zLabel = "INSERT OR FAIL";      break;
  }
  sqlite3TreeViewPush(&pView, 0);
  sqlite3TreeViewLine(pView, zLabel);
  if( pWith ) n++;
  if( pTabList ) n++;
  if( pColumnList ) n++;
  if( pSelect ) n++;
  if( pExprList ) n++;
  if( pUpsert ) n++;
  if( pTrigger ) n++;
  if( pWith ){
    sqlite3TreeViewPush(&pView, (--n)>0);
    sqlite3TreeViewWith(pView, pWith, 0);
    sqlite3TreeViewPop(&pView);
  }
  if( pTabList ){
    sqlite3TreeViewPush(&pView, (--n)>0);
    sqlite3TreeViewLine(pView, "INTO");
    sqlite3TreeViewSrcList(pView, pTabList);
    sqlite3TreeViewPop(&pView);
  }
  if( pColumnList ){
    sqlite3TreeViewIdList(pView, pColumnList, (--n)>0, "COLUMNS");
  }
  if( pSelect ){
    sqlite3TreeViewPush(&pView, (--n)>0);
    sqlite3TreeViewLine(pView, "DATA-SOURCE");
    sqlite3TreeViewSelect(pView, pSelect, 0);
    sqlite3TreeViewPop(&pView);
  }
  if( pExprList ){
    sqlite3TreeViewExprList(pView, pExprList, (--n)>0, "VALUES");
  }
  if( pUpsert ){
    sqlite3TreeViewPush(&pView, (--n)>0);
    sqlite3TreeViewLine(pView, "UPSERT");
    sqlite3TreeViewUpsert(pView, pUpsert, 0);
    sqlite3TreeViewPop(&pView);
  }
  if( pTrigger ){
    sqlite3TreeViewTrigger(pView, pTrigger, (--n)>0, 1);
  }
  sqlite3TreeViewPop(&pView);
}
#endif /* TREETRACE_ENABLED */

#if TREETRACE_ENABLED
/*
** Generate a human-readable diagram of the data structure that go
** into generating an UPDATE statement.
*/
void sqlite3TreeViewUpdate(
  const With *pWith,
  const SrcList *pTabList,
  const ExprList *pChanges,
  const Expr *pWhere,
  int onError,
  const ExprList *pOrderBy,
  const Expr *pLimit,
  const Upsert *pUpsert,
  const Trigger *pTrigger
){
  int n = 0;
  TreeView *pView = 0;
  const char *zLabel = "UPDATE";
  switch( onError ){
    case OE_Replace:  zLabel = "UPDATE OR REPLACE";   break;
    case OE_Ignore:   zLabel = "UPDATE OR IGNORE";    break;
    case OE_Rollback: zLabel = "UPDATE OR ROLLBACK";  break;
    case OE_Abort:    zLabel = "UPDATE OR ABORT";     break;
    case OE_Fail:     zLabel = "UPDATE OR FAIL";      break;
  }
  sqlite3TreeViewPush(&pView, 0);
  sqlite3TreeViewLine(pView, zLabel);
  if( pWith ) n++;
  if( pTabList ) n++;
  if( pChanges ) n++;
  if( pWhere ) n++;
  if( pOrderBy ) n++;
  if( pLimit ) n++;
  if( pUpsert ) n++;
  if( pTrigger ) n++;
  if( pWith ){
    sqlite3TreeViewPush(&pView, (--n)>0);
    sqlite3TreeViewWith(pView, pWith, 0);
    sqlite3TreeViewPop(&pView);
  }
  if( pTabList ){
    sqlite3TreeViewPush(&pView, (--n)>0);
    sqlite3TreeViewLine(pView, "FROM");
    sqlite3TreeViewSrcList(pView, pTabList);
    sqlite3TreeViewPop(&pView);
  }
  if( pChanges ){
    sqlite3TreeViewExprList(pView, pChanges, (--n)>0, "SET");
  }
  if( pWhere ){
    sqlite3TreeViewPush(&pView, (--n)>0);
    sqlite3TreeViewLine(pView, "WHERE");
    sqlite3TreeViewExpr(pView, pWhere, 0);
    sqlite3TreeViewPop(&pView);
  }
  if( pOrderBy ){
    sqlite3TreeViewExprList(pView, pOrderBy, (--n)>0, "ORDER-BY");
  }
  if( pLimit ){
    sqlite3TreeViewPush(&pView, (--n)>0);
    sqlite3TreeViewLine(pView, "LIMIT");
    sqlite3TreeViewExpr(pView, pLimit, 0);
    sqlite3TreeViewPop(&pView);
  }
  if( pUpsert ){
    sqlite3TreeViewPush(&pView, (--n)>0);
    sqlite3TreeViewLine(pView, "UPSERT");
    sqlite3TreeViewUpsert(pView, pUpsert, 0);
    sqlite3TreeViewPop(&pView);
  }
  if( pTrigger ){
    sqlite3TreeViewTrigger(pView, pTrigger, (--n)>0, 1);
  }
  sqlite3TreeViewPop(&pView);
}
#endif /* TREETRACE_ENABLED */

#ifndef SQLITE_OMIT_TRIGGER
/*
** Show a human-readable graph of a TriggerStep
*/
void sqlite3TreeViewTriggerStep(
  TreeView *pView,
  const TriggerStep *pStep,
  u8 moreToFollow,
  u8 showFullList
){
  int cnt = 0;
  if( pStep==0 ) return;
  sqlite3TreeViewPush(&pView, 
      moreToFollow || (showFullList && pStep->pNext!=0));
  do{
    if( cnt++ && pStep->pNext==0 ){
      sqlite3TreeViewPop(&pView);
      sqlite3TreeViewPush(&pView, 0);
    }
    sqlite3TreeViewLine(pView, "%s", pStep->zSpan ? pStep->zSpan : "RETURNING");
  }while( showFullList && (pStep = pStep->pNext)!=0 );
  sqlite3TreeViewPop(&pView);
}
  
/*
** Show a human-readable graph of a Trigger
*/
void sqlite3TreeViewTrigger(
  TreeView *pView,
  const Trigger *pTrigger,
  u8 moreToFollow,
  u8 showFullList
){
  int cnt = 0;
  if( pTrigger==0 ) return;
  sqlite3TreeViewPush(&pView,
     moreToFollow || (showFullList && pTrigger->pNext!=0));
  do{
    if( cnt++ && pTrigger->pNext==0 ){
      sqlite3TreeViewPop(&pView);
      sqlite3TreeViewPush(&pView, 0);
    }
    sqlite3TreeViewLine(pView, "TRIGGER %s", pTrigger->zName);
    sqlite3TreeViewPush(&pView, 0);
    sqlite3TreeViewTriggerStep(pView, pTrigger->step_list, 0, 1);
    sqlite3TreeViewPop(&pView);
  }while( showFullList && (pTrigger = pTrigger->pNext)!=0 );
  sqlite3TreeViewPop(&pView);
}
#endif /* SQLITE_OMIT_TRIGGER */
  

/*****************************************************************************
** Regenerate SQL text from a Select object.
**
** Intended for interactive use in a debugger:
**
**      (gdb) call sqlite3ShowSelectAsSql(p)
**
** This is not an API.  This code is subject to change or removal at any
** time.  This code is not present in production builds.  The SQLite core
** never uses this code.
**
** The output is on a single line and is intended to have the same
** meaning as the original SQL, though not the same text.  The parser
** (and later the resolver and optimizer) discard or rewrite information,
** so the following are only approximate:
**
**    *  Parentheses are regenerated from operator precedence.
**    *  Column references that have been resolved are shown as
**       ALIAS.COLUMN, looked up through the FROM clauses of the enclosing
**       SELECT statements.
**    *  A "*" that has already been expanded is shown expanded.
**    *  NATURAL joins that have been converted into USING, and the
**       ON-to-WHERE movement of the query flattener, show the converted
**       form.
**    *  Window-function rewrites (SF_WinRewrite) and other transformations
**       that synthesize new subqueries are shown as the transformed query.
**    *  Anything not understood is shown as <...>, which is not SQL.
*/
typedef struct ShowSqlScope ShowSqlScope;
struct ShowSqlScope {
  const SrcList *pSrc;           /* A FROM clause that is in scope */
  const ShowSqlScope *pUp;       /* FROM clauses of enclosing SELECTs */
};
typedef struct ShowSql ShowSql;
struct ShowSql {
  sqlite3_str *pOut;             /* Append SQL text here */
  const ShowSqlScope *pScope;    /* Innermost FROM clause in scope */
};

/* Flags for showSqlExprList() */
#define SHOWSQL_ALIAS   0x01     /* Show AS names (result set) */
#define SHOWSQL_SORT    0x02     /* Show ASC/DESC/NULLS (ORDER BY) */

static void showSqlExpr(ShowSql*, const Expr*, int);
static void showSqlExprList(ShowSql*, const ExprList*, int);
static void showSqlSelect(ShowSql*, const Select*);
static void showSqlSrcList(ShowSql*, const SrcList*);

/* Operator precedences.  Larger numbers bind more tightly. */
#define SQLPREC_OR        1
#define SQLPREC_AND       2
#define SQLPREC_NOT       3
#define SQLPREC_EQ        4   /* = <> IS BETWEEN IN LIKE ISNULL ... */
#define SQLPREC_CMP       5   /* < <= > >= */
#define SQLPREC_BIT       7   /* & | << >> */
#define SQLPREC_ADD       8
#define SQLPREC_MUL       9
#define SQLPREC_CONCAT   10   /* || -> ->> */
#define SQLPREC_COLLATE  11
#define SQLPREC_UNARY    12   /* ~ + - */
#define SQLPREC_ATOM     99

/* Append the identifier z, with quotes if they are necessary */
static void showSqlId(sqlite3_str *p, const char *z){
  int i, n;
  int bOk;
  if( z==0 ){
    sqlite3_str_appendall(p, "<null-id>");
    return;
  }
  n = (int)strlen(z);
  bOk = n>0 && (sqlite3Isalpha(z[0]) || z[0]=='_');
  for(i=1; bOk && i<n; i++){
    if( !sqlite3Isalnum(z[i]) && z[i]!='_' && z[i]!='$' ) bOk = 0;
  }
  if( bOk && sqlite3_keyword_check(z, n) ) bOk = 0;
  if( bOk ){
    sqlite3_str_appendall(p, z);
  }else{
    sqlite3_str_appendf(p, "\"%w\"", z);
  }
}

/* Append a name in upper-case */
static void showSqlUpper(sqlite3_str *p, const char *z){
  for(; *z; z++){
    sqlite3_str_appendchar(p, 1, (char)sqlite3Toupper(*z));
  }
}

static void showSqlIdList(sqlite3_str *p, const IdList *pIds){
  int i;
  if( pIds==0 ) return;
  for(i=0; i<pIds->nId; i++){
    if( i ) sqlite3_str_append(p, ",", 1);
    showSqlId(p, pIds->a[i].zName);
  }
}

/* Find the FROM-clause entry with cursor number iCursor, if it is in scope */
static const SrcItem *showSqlFindSrc(const ShowSqlScope *pScope, int iCursor){
  for(; pScope; pScope=pScope->pUp){
    const SrcList *pSrc = pScope->pSrc;
    int i;
    if( pSrc==0 ) continue;
    for(i=0; i<pSrc->nSrc; i++){
      if( pSrc->a[i].iCursor==iCursor ) return &pSrc->a[i];
    }
  }
  return 0;
}

/* A resolved column reference: ALIAS.COLUMN */
static void showSqlColumn(ShowSql *x, const Expr *pExpr){
  sqlite3_str *p = x->pOut;
  const Table *pTab;
  const SrcItem *pItem;
  const char *zQual = 0;
  int iCol = pExpr->iColumn;
  if( pExpr->iTable<0 || !ExprUseYTab(pExpr) || pExpr->y.pTab==0 ){
    sqlite3_str_appendf(p, "<column %d:%d>", pExpr->iTable, iCol);
    return;
  }
  pTab = pExpr->y.pTab;
  pItem = showSqlFindSrc(x->pScope, pExpr->iTable);
  if( pItem==0 ){
    zQual = pTab->zName;
  }else if( pItem->zAlias ){
    zQual = pItem->zAlias;
  }else if( !pItem->fg.isSubquery ){
    zQual = pItem->zName ? pItem->zName : pTab->zName;
  }
  if( zQual ){
    showSqlId(p, zQual);
    sqlite3_str_append(p, ".", 1);
  }
  if( iCol<0 && pTab->iPKey>=0 && pTab->iPKey<pTab->nCol ){
    showSqlId(p, pTab->aCol[pTab->iPKey].zCnName);   /* INTEGER PRIMARY KEY */
  }else if( iCol<0 ){
    sqlite3_str_appendall(p, "rowid");
  }else if( iCol<pTab->nCol ){
    showSqlId(p, pTab->aCol[iCol].zCnName);
  }else{
    sqlite3_str_appendf(p, "<col%d>", iCol);
  }
}

#ifndef SQLITE_OMIT_WINDOWFUNC
/* One end of a window frame */
static void showSqlBound(ShowSql *x, u8 eBound, const Expr *pExpr, int bEnd){
  sqlite3_str *p = x->pOut;
  switch( eBound ){
    case TK_UNBOUNDED:
      sqlite3_str_appendall(p, bEnd ? "UNBOUNDED FOLLOWING"
                                    : "UNBOUNDED PRECEDING");
      break;
    case TK_CURRENT:
      sqlite3_str_appendall(p, "CURRENT ROW");
      break;
    case TK_PRECEDING:
      showSqlExpr(x, pExpr, 0);
      sqlite3_str_appendall(p, " PRECEDING");
      break;
    case TK_FOLLOWING:
      showSqlExpr(x, pExpr, 0);
      sqlite3_str_appendall(p, " FOLLOWING");
      break;
  }
}

/* The parenthesized body of a window definition */
static void showSqlWindowSpec(ShowSql *x, const Window *pWin){
  sqlite3_str *p = x->pOut;
  const char *zSep = "";
  sqlite3_str_append(p, "(", 1);
  if( pWin->zBase ){
    showSqlId(p, pWin->zBase);
    zSep = " ";
  }
  if( pWin->pPartition ){
    sqlite3_str_appendf(p, "%sPARTITION BY ", zSep);
    showSqlExprList(x, pWin->pPartition, 0);
    zSep = " ";
  }
  if( pWin->pOrderBy ){
    sqlite3_str_appendf(p, "%sORDER BY ", zSep);
    showSqlExprList(x, pWin->pOrderBy, SHOWSQL_SORT);
    zSep = " ";
  }
  if( pWin->eFrmType!=0 && pWin->eFrmType!=TK_FILTER
   && !pWin->bImplicitFrame
  ){
    const char *zType = "ROWS";
    if( pWin->eFrmType==TK_RANGE ) zType = "RANGE";
    if( pWin->eFrmType==TK_GROUPS ) zType = "GROUPS";
    sqlite3_str_appendf(p, "%s%s BETWEEN ", zSep, zType);
    showSqlBound(x, pWin->eStart, pWin->pStart, 0);
    sqlite3_str_appendall(p, " AND ");
    showSqlBound(x, pWin->eEnd, pWin->pEnd, 1);
    zSep = " ";
  }
  if( pWin->eExclude ){
    const char *zExclude = "<unknown>";
    switch( pWin->eExclude ){
      case TK_NO:      zExclude = "NO OTHERS";   break;
      case TK_CURRENT: zExclude = "CURRENT ROW"; break;
      case TK_GROUP:   zExclude = "GROUP";       break;
      case TK_TIES:    zExclude = "TIES";        break;
    }
    sqlite3_str_appendf(p, "%sEXCLUDE %s", zSep, zExclude);
  }
  sqlite3_str_append(p, ")", 1);
}

/* The FILTER and OVER clauses that follow a window-function call */
static void showSqlOver(ShowSql *x, const Window *pWin){
  sqlite3_str *p = x->pOut;
  if( pWin->pFilter ){
    sqlite3_str_appendall(p, " FILTER (WHERE ");
    showSqlExpr(x, pWin->pFilter, 0);
    sqlite3_str_append(p, ")", 1);
  }
  if( pWin->eFrmType==TK_FILTER ) return;
  sqlite3_str_appendall(p, " OVER ");
  if( pWin->zName && !pWin->zBase && !pWin->pPartition && !pWin->pOrderBy
   && pWin->eFrmType==0
  ){
    showSqlId(p, pWin->zName);     /* OVER name */
  }else{
    showSqlWindowSpec(x, pWin);
  }
}
#endif /* SQLITE_OMIT_WINDOWFUNC */

/* Operator table for simple binary operators */
static const struct ShowSqlBinOp {
  u8 op;                  /* Expr.op value */
  u8 prec;                /* Precedence */
  const char *z;          /* Text of the operator */
} aShowSqlBinOp[] = {
  { TK_OR,      SQLPREC_OR,     "OR"     },
  { TK_AND,     SQLPREC_AND,    "AND"    },
  { TK_EQ,      SQLPREC_EQ,     "="      },
  { TK_NE,      SQLPREC_EQ,     "<>"     },
  { TK_IS,      SQLPREC_EQ,     "IS"     },
  { TK_ISNOT,   SQLPREC_EQ,     "IS NOT" },
  { TK_LT,      SQLPREC_CMP,    "<"      },
  { TK_LE,      SQLPREC_CMP,    "<="     },
  { TK_GT,      SQLPREC_CMP,    ">"      },
  { TK_GE,      SQLPREC_CMP,    ">="     },
  { TK_BITAND,  SQLPREC_BIT,    "&"      },
  { TK_BITOR,   SQLPREC_BIT,    "|"      },
  { TK_LSHIFT,  SQLPREC_BIT,    "<<"     },
  { TK_RSHIFT,  SQLPREC_BIT,    ">>"     },
  { TK_PLUS,    SQLPREC_ADD,    "+"      },
  { TK_MINUS,   SQLPREC_ADD,    "-"      },
  { TK_STAR,    SQLPREC_MUL,    "*"      },
  { TK_SLASH,   SQLPREC_MUL,    "/"      },
  { TK_REM,     SQLPREC_MUL,    "%"      },
  { TK_CONCAT,  SQLPREC_CONCAT, "||"     },
};

static const struct ShowSqlBinOp *showSqlFindBinOp(int op){
  int i;
  for(i=0; i<(int)(sizeof(aShowSqlBinOp)/sizeof(aShowSqlBinOp[0])); i++){
    if( aShowSqlBinOp[i].op==op ) return &aShowSqlBinOp[i];
  }
  return 0;
}

/*
** Return true if pExpr is a function call that is really an infix operator
** like LIKE, GLOB, REGEXP, MATCH, -> or ->>.  The arguments of the
** function are in reverse order for the keyword operators.
*/
static int showSqlIsInfixFunc(const Expr *pExpr){
  if( pExpr->op!=TK_FUNCTION ) return 0;
  if( ExprHasProperty(pExpr, EP_TokenOnly) ) return 0;
  if( !ExprUseXList(pExpr) || pExpr->x.pList==0 ) return 0;
  if( pExpr->x.pList->nExpr<2 || pExpr->x.pList->nExpr>3 ) return 0;
  if( ExprHasProperty(pExpr, EP_WinFunc) ) return 0;
  if( pExpr->u.zToken==0 ) return 0;
  if( pExpr->u.zToken[0]=='-' ) return pExpr->x.pList->nExpr==2;
  return ExprHasProperty(pExpr, EP_InfixFunc);
}

/*
** Show the infix-operator function pFunc (LIKE, GLOB, REGEXP, MATCH, -> or
** ->>) as an operator.  "X LIKE Y ESCAPE Z" is stored as like(Y,X,Z).
** If bNot, then show the negated operator, as in "X NOT LIKE Y".
*/
static void showSqlInfix(ShowSql *x, const Expr *pFunc, int bNot){
  sqlite3_str *p = x->pOut;
  const ExprList *pList = pFunc->x.pList;
  if( pFunc->u.zToken[0]=='-' ){
    showSqlExpr(x, pList->a[0].pExpr, SQLPREC_CONCAT);
    sqlite3_str_appendf(p, " %s ", pFunc->u.zToken);
    showSqlExpr(x, pList->a[1].pExpr, SQLPREC_CONCAT+1);
  }else{
    showSqlExpr(x, pList->a[1].pExpr, SQLPREC_CMP);
    sqlite3_str_appendall(p, bNot ? " NOT " : " ");
    showSqlUpper(p, pFunc->u.zToken);
    sqlite3_str_append(p, " ", 1);
    showSqlExpr(x, pList->a[0].pExpr, SQLPREC_CMP);
    if( pList->nExpr==3 ){
      sqlite3_str_appendall(p, " ESCAPE ");
      showSqlExpr(x, pList->a[2].pExpr, SQLPREC_CMP);
    }
  }
}

/*
** The parser sets EP_InfixFunc on the TK_NOT, not on the function, for
** "X NOT LIKE Y".  Return true if pExpr is such a TK_NOT.
*/
static int showSqlIsNotInfix(const Expr *pExpr){
  return pExpr->op==TK_NOT && ExprHasProperty(pExpr, EP_InfixFunc)
      && pExpr->pLeft!=0 && pExpr->pLeft->op==TK_FUNCTION
      && !ExprHasProperty(pExpr->pLeft, EP_TokenOnly)
      && ExprUseXList(pExpr->pLeft) && pExpr->pLeft->x.pList!=0
      && pExpr->pLeft->x.pList->nExpr>=2 && pExpr->pLeft->x.pList->nExpr<=3
      && pExpr->pLeft->u.zToken!=0;
}

/* Skip operators that have no SQL syntax of their own */
static const Expr *showSqlSkip(const Expr *p){
  while( p ){
    if( p->op==TK_SPAN || p->op==TK_IF_NULL_ROW
     || (p->op==TK_COLLATE && !ExprHasProperty(p, EP_Collate))
    ){
      p = p->pLeft;
    }else{
      break;
    }
  }
  return p;
}

/* Return the precedence of the outermost operator of pExpr.  The argument
** must already have been passed through showSqlSkip(). */
static int showSqlPrec(const Expr *pExpr){
  const struct ShowSqlBinOp *pOp;
  if( (pOp = showSqlFindBinOp(pExpr->op))!=0 ) return pOp->prec;
  switch( pExpr->op ){
    case TK_NOT:      return showSqlIsNotInfix(pExpr) ? SQLPREC_EQ
                                                      : SQLPREC_NOT;
    case TK_ISNULL:
    case TK_NOTNULL:
    case TK_TRUTH:
    case TK_BETWEEN:
    case TK_IN:       return SQLPREC_EQ;
    case TK_COLLATE:  return SQLPREC_COLLATE;
    case TK_UMINUS:
    case TK_UPLUS:
    case TK_BITNOT:   return SQLPREC_UNARY;
    case TK_FUNCTION: {
      if( showSqlIsInfixFunc(pExpr) ){
        return pExpr->u.zToken[0]=='-' ? SQLPREC_CONCAT : SQLPREC_EQ;
      }
      break;
    }
  }
  return SQLPREC_ATOM;
}

/*
** Append SQL for the expression pExpr.  Put parentheses around it if its
** top-level operator binds less tightly than minPrec.
*/
static void showSqlExpr(ShowSql *x, const Expr *pExpr, int minPrec){
  sqlite3_str *p = x->pOut;
  const struct ShowSqlBinOp *pOp;
  int prec;
  pExpr = showSqlSkip(pExpr);
  if( pExpr==0 ){
    sqlite3_str_appendall(p, "NULL");
    return;
  }
  prec = showSqlPrec(pExpr);
  if( prec<minPrec ) sqlite3_str_append(p, "(", 1);

  if( (pOp = showSqlFindBinOp(pExpr->op))!=0 ){
    /* All of these operators are left-associative */
    showSqlExpr(x, pExpr->pLeft, prec);
    sqlite3_str_appendf(p, " %s ", pOp->z);
    showSqlExpr(x, pExpr->pRight, prec+1);
  }else{
    switch( pExpr->op ){
      case TK_COLUMN:
      case TK_AGG_COLUMN: {
        if( pExpr->op==TK_COLUMN ){
          showSqlColumn(x, pExpr);
        }else{
          sqlite3_str_appendf(p, "<agg-column %d:%d>",
                              pExpr->iTable, pExpr->iColumn);
        }
        break;
      }
      case TK_INTEGER: {
        if( pExpr->flags & EP_IntValue ){
          sqlite3_str_appendf(p, "%d", pExpr->u.iValue);
        }else{
          sqlite3_str_appendall(p, pExpr->u.zToken);
        }
        break;
      }
      case TK_FLOAT:
      case TK_BLOB: {
        /* The token text is the literal, including the x'..' of a blob */
        sqlite3_str_appendall(p, pExpr->u.zToken);
        break;
      }
      case TK_STRING: {
        sqlite3_str_appendf(p, "%Q", pExpr->u.zToken);
        break;
      }
      case TK_NULL: {
        sqlite3_str_appendall(p, "NULL");
        break;
      }
      case TK_TRUEFALSE: {
        sqlite3_str_appendall(p, sqlite3ExprTruthValue(pExpr)?"TRUE":"FALSE");
        break;
      }
      case TK_VARIABLE: {
        if( pExpr->u.zToken ){
          sqlite3_str_appendall(p, pExpr->u.zToken);
        }else{
          sqlite3_str_appendf(p, "?%d", pExpr->iColumn);
        }
        break;
      }
      case TK_ID: {
        showSqlId(p, pExpr->u.zToken);
        break;
      }
      case TK_DOT: {
        showSqlExpr(x, pExpr->pLeft, SQLPREC_ATOM);
        sqlite3_str_append(p, ".", 1);
        showSqlExpr(x, pExpr->pRight, SQLPREC_ATOM);
        break;
      }
      case TK_ASTERISK: {
        sqlite3_str_append(p, "*", 1);
        break;
      }
#ifndef SQLITE_OMIT_CAST
      case TK_CAST: {
        sqlite3_str_appendall(p, "CAST(");
        showSqlExpr(x, pExpr->pLeft, 0);
        sqlite3_str_appendall(p, " AS ");
        sqlite3_str_appendall(p, pExpr->u.zToken);
        sqlite3_str_append(p, ")", 1);
        break;
      }
#endif
      case TK_COLLATE: {
        showSqlExpr(x, pExpr->pLeft, SQLPREC_COLLATE);
        sqlite3_str_appendall(p, " COLLATE ");
        showSqlId(p, pExpr->u.zToken);
        break;
      }
      case TK_NOT: {
        if( showSqlIsNotInfix(pExpr) ){
          showSqlInfix(x, pExpr->pLeft, 1);
          break;
        }
        sqlite3_str_appendall(p, "NOT ");
        showSqlExpr(x, pExpr->pLeft, SQLPREC_NOT);
        break;
      }
      case TK_UMINUS:
      case TK_UPLUS:
      case TK_BITNOT: {
        char cOp = pExpr->op==TK_UMINUS ? '-' : pExpr->op==TK_UPLUS ? '+' : '~';
        ShowSql y;
        char *zArg;
        y.pOut = sqlite3_str_new(0);
        y.pScope = x->pScope;
        showSqlExpr(&y, pExpr->pLeft, SQLPREC_UNARY);
        zArg = sqlite3_str_finish(y.pOut);
        /* Avoid forming "--", which starts an SQL comment */
        sqlite3_str_appendf(p, "%c%s%s", cOp,
                            (cOp=='-' && zArg && zArg[0]=='-') ? " " : "",
                            zArg ? zArg : "");
        sqlite3_free(zArg);
        break;
      }
      case TK_ISNULL:
      case TK_NOTNULL: {
        showSqlExpr(x, pExpr->pLeft, SQLPREC_CMP);
        sqlite3_str_appendall(p,
             pExpr->op==TK_ISNULL ? " IS NULL" : " IS NOT NULL");
        break;
      }
      case TK_TRUTH: {
        static const char *const azOp[] = {
           " IS FALSE", " IS TRUE", " IS NOT FALSE", " IS NOT TRUE"
        };
        int i = (pExpr->op2==TK_ISNOT)*2
                   + (pExpr->pRight ? sqlite3ExprTruthValue(
                                  showSqlSkip(pExpr->pRight)) : 0);
        showSqlExpr(x, pExpr->pLeft, SQLPREC_CMP);
        sqlite3_str_appendall(p, azOp[i]);
        break;
      }
      case TK_BETWEEN: {
        assert( ExprUseXList(pExpr) );
        showSqlExpr(x, pExpr->pLeft, SQLPREC_CMP);
        sqlite3_str_appendall(p, " BETWEEN ");
        showSqlExpr(x, pExpr->x.pList->a[0].pExpr, SQLPREC_CMP);
        sqlite3_str_appendall(p, " AND ");
        showSqlExpr(x, pExpr->x.pList->a[1].pExpr, SQLPREC_CMP);
        break;
      }
      case TK_VECTOR: {
        assert( ExprUseXList(pExpr) );
        sqlite3_str_append(p, "(", 1);
        showSqlExprList(x, pExpr->x.pList, 0);
        sqlite3_str_append(p, ")", 1);
        break;
      }
      case TK_CASE: {
        const ExprList *pList = pExpr->x.pList;
        int i;
        assert( ExprUseXList(pExpr) );
        sqlite3_str_appendall(p, "CASE");
        if( pExpr->pLeft ){
          sqlite3_str_append(p, " ", 1);
          showSqlExpr(x, pExpr->pLeft, 0);
        }
        for(i=0; i+1<pList->nExpr; i+=2){
          sqlite3_str_appendall(p, " WHEN ");
          showSqlExpr(x, pList->a[i].pExpr, 0);
          sqlite3_str_appendall(p, " THEN ");
          showSqlExpr(x, pList->a[i+1].pExpr, 0);
        }
        if( i<pList->nExpr ){
          sqlite3_str_appendall(p, " ELSE ");
          showSqlExpr(x, pList->a[i].pExpr, 0);
        }
        sqlite3_str_appendall(p, " END");
        break;
      }
      case TK_AGG_FUNCTION:
      case TK_FUNCTION: {
        const ExprList *pFarg = 0;
#ifndef SQLITE_OMIT_WINDOWFUNC
        const Window *pWin = 0;
#endif
        if( showSqlIsInfixFunc(pExpr) ){
          showSqlInfix(x, pExpr, 0);
          break;
        }
        if( !ExprHasProperty(pExpr, EP_TokenOnly) ){
          assert( ExprUseXList(pExpr) );
          pFarg = pExpr->x.pList;
#ifndef SQLITE_OMIT_WINDOWFUNC
          if( ExprHasProperty(pExpr, EP_WinFunc) ) pWin = pExpr->y.pWin;
#endif
        }
        sqlite3_str_appendall(p, pExpr->u.zToken);
        sqlite3_str_append(p, "(", 1);
        if( ExprHasProperty(pExpr, EP_Distinct) ){
          sqlite3_str_appendall(p, "DISTINCT ");
        }
        if( pFarg ){
          showSqlExprList(x, pFarg, 0);
          if( pExpr->pLeft && pExpr->pLeft->op==TK_ORDER ){
            sqlite3_str_appendall(p, " ORDER BY ");
            showSqlExprList(x, pExpr->pLeft->x.pList, SHOWSQL_SORT);
          }
        }else if( sqlite3StrICmp(pExpr->u.zToken, "count")==0 ){
          sqlite3_str_append(p, "*", 1);
        }
        sqlite3_str_append(p, ")", 1);
#ifndef SQLITE_OMIT_WINDOWFUNC
        if( pWin ) showSqlOver(x, pWin);
#endif
        break;
      }
#ifndef SQLITE_OMIT_SUBQUERY
      case TK_EXISTS: {
        assert( ExprUseXSelect(pExpr) );
        sqlite3_str_appendall(p, "EXISTS(");
        showSqlSelect(x, pExpr->x.pSelect);
        sqlite3_str_append(p, ")", 1);
        break;
      }
      case TK_SELECT: {
        assert( ExprUseXSelect(pExpr) );
        sqlite3_str_append(p, "(", 1);
        showSqlSelect(x, pExpr->x.pSelect);
        sqlite3_str_append(p, ")", 1);
        break;
      }
      case TK_IN: {
        showSqlExpr(x, pExpr->pLeft, SQLPREC_CMP);
        sqlite3_str_appendall(p, " IN (");
        if( ExprUseXSelect(pExpr) ){
          showSqlSelect(x, pExpr->x.pSelect);
        }else{
          showSqlExprList(x, pExpr->x.pList, 0);
        }
        sqlite3_str_append(p, ")", 1);
        break;
      }
#endif /* SQLITE_OMIT_SUBQUERY */
      default: {
        sqlite3_str_appendf(p, "<op=%d>", pExpr->op);
        break;
      }
    }
  }
  if( prec<minPrec ) sqlite3_str_append(p, ")", 1);
}

/* Append a comma-separated list of expressions */
static void showSqlExprList(ShowSql *x, const ExprList *pList, int flags){
  sqlite3_str *p = x->pOut;
  int i;
  if( pList==0 ) return;
  for(i=0; i<pList->nExpr; i++){
    const struct ExprList_item *pItem = &pList->a[i];
    if( i ) sqlite3_str_append(p, ",", 1);
    showSqlExpr(x, pItem->pExpr, 0);
    if( (flags & SHOWSQL_ALIAS)!=0
     && pItem->zEName && pItem->fg.eEName==ENAME_NAME
    ){
      sqlite3_str_appendall(p, " AS ");
      showSqlId(p, pItem->zEName);
    }
    if( flags & SHOWSQL_SORT ){
      int bDesc = (pItem->fg.sortFlags & KEYINFO_ORDER_DESC)!=0;
      if( bDesc ) sqlite3_str_appendall(p, " DESC");
      if( pItem->fg.bNulls ){
        int bBigNull = (pItem->fg.sortFlags & KEYINFO_ORDER_BIGNULL)!=0;
        sqlite3_str_appendall(p,
             bDesc==bBigNull ? " NULLS FIRST" : " NULLS LAST");
      }
    }
  }
}

/* Append the FROM clause (without the FROM keyword) */
static void showSqlSrcList(ShowSql *x, const SrcList *pSrc){
  sqlite3_str *p = x->pOut;
  int i;
  if( pSrc==0 ) return;
  for(i=0; i<pSrc->nSrc; i++){
    const SrcItem *pItem = &pSrc->a[i];
    int jt = pItem->fg.jointype;
    int bConstraint = (pItem->fg.isUsing && pItem->u3.pUsing!=0)
                   || (!pItem->fg.isUsing && pItem->u3.pOn!=0);
    if( i>0 ){
      if( jt & JT_NATURAL ) sqlite3_str_appendall(p, " NATURAL");
      if( (jt & (JT_LEFT|JT_RIGHT))==(JT_LEFT|JT_RIGHT) ){
        sqlite3_str_appendall(p, " FULL JOIN ");
      }else if( jt & JT_LEFT ){
        sqlite3_str_appendall(p, " LEFT JOIN ");
      }else if( jt & JT_RIGHT ){
        sqlite3_str_appendall(p, " RIGHT JOIN ");
      }else if( jt & JT_CROSS ){
        sqlite3_str_appendall(p, " CROSS JOIN ");
      }else if( bConstraint || (jt & JT_NATURAL) ){
        sqlite3_str_appendall(p, " JOIN ");
      }else{
        sqlite3_str_appendall(p, ", ");
      }
    }
    if( pItem->fg.isSubquery ){
      sqlite3_str_append(p, "(", 1);
      if( IsNestedFrom(pItem) ){
        /* A parenthesized join:  FROM (a JOIN b) */
        showSqlSrcList(x, pItem->u4.pSubq->pSelect->pSrc);
      }else{
        showSqlSelect(x, pItem->u4.pSubq->pSelect);
      }
      sqlite3_str_append(p, ")", 1);
    }else{
      if( !pItem->fg.fixedSchema && pItem->u4.zDatabase ){
        showSqlId(p, pItem->u4.zDatabase);
        sqlite3_str_append(p, ".", 1);
      }
      showSqlId(p, pItem->zName);
      if( pItem->fg.isTabFunc ){
        sqlite3_str_append(p, "(", 1);
        showSqlExprList(x, pItem->u1.pFuncArg, 0);
        sqlite3_str_append(p, ")", 1);
      }
    }
    if( pItem->zAlias ){
      sqlite3_str_appendall(p, " AS ");
      showSqlId(p, pItem->zAlias);
    }
    if( pItem->fg.isIndexedBy ){
      sqlite3_str_appendall(p, " INDEXED BY ");
      showSqlId(p, pItem->u1.zIndexedBy);
    }else if( pItem->fg.notIndexed ){
      sqlite3_str_appendall(p, " NOT INDEXED");
    }
    if( i>0 && !(jt & JT_NATURAL) ){
      if( pItem->fg.isUsing ){
        if( pItem->u3.pUsing ){
          sqlite3_str_appendall(p, " USING(");
          showSqlIdList(p, pItem->u3.pUsing);
          sqlite3_str_append(p, ")", 1);
        }
      }else if( pItem->u3.pOn ){
        sqlite3_str_appendall(p, " ON ");
        showSqlExpr(x, pItem->u3.pOn, 0);
      }
    }
  }
}

/* Append a WITH clause, followed by a space */
static void showSqlWith(ShowSql *x, const With *pWith){
  sqlite3_str *p = x->pOut;
  int i;
  int bRecursive = 0;
  if( pWith==0 || pWith->nCte<=0 ) return;
  for(i=0; i<pWith->nCte; i++){
    const Select *pS;
    for(pS=pWith->a[i].pSelect; pS; pS=pS->pPrior){
      if( pS->selFlags & SF_Recursive ) bRecursive = 1;
    }
  }
  sqlite3_str_appendall(p, bRecursive ? "WITH RECURSIVE " : "WITH ");
  for(i=0; i<pWith->nCte; i++){
    const Cte *pCte = &pWith->a[i];
    if( i ) sqlite3_str_append(p, ",", 1);
    showSqlId(p, pCte->zName);
    if( pCte->pCols && pCte->pCols->nExpr>0 ){
      int j;
      sqlite3_str_append(p, "(", 1);
      for(j=0; j<pCte->pCols->nExpr; j++){
        if( j ) sqlite3_str_append(p, ",", 1);
        showSqlId(p, pCte->pCols->a[j].zEName);
      }
      sqlite3_str_append(p, ")", 1);
    }
    sqlite3_str_appendall(p, " AS ");
    if( pCte->eM10d==M10d_Yes ) sqlite3_str_appendall(p, "MATERIALIZED ");
    if( pCte->eM10d==M10d_No ) sqlite3_str_appendall(p, "NOT MATERIALIZED ");
    sqlite3_str_append(p, "(", 1);
    showSqlSelect(x, pCte->pSelect);
    sqlite3_str_append(p, ")", 1);
  }
  sqlite3_str_append(p, " ", 1);
}

/* ORDER BY and LIMIT clauses of p, with a leading space */
static void showSqlTail(ShowSql *x, const Select *p){
  sqlite3_str *p2 = x->pOut;
  if( p->pOrderBy ){
    sqlite3_str_appendall(p2, " ORDER BY ");
    showSqlExprList(x, p->pOrderBy, SHOWSQL_SORT);
  }
  if( p->pLimit ){
    sqlite3_str_appendall(p2, " LIMIT ");
    showSqlExpr(x, p->pLimit->pLeft, 0);
    if( p->pLimit->pRight ){
      sqlite3_str_appendall(p2, " OFFSET ");
      showSqlExpr(x, p->pLimit->pRight, 0);
    }
  }
}

/*
** One simple SELECT, without ORDER BY and LIMIT and without any
** compound-select operator.  If bValuesCont is true then this term is a
** continuation of a multi-row VALUES clause and so omits the keyword.
*/
static void showSqlCore(ShowSql *x, const Select *p, int bValuesCont){
  sqlite3_str *s = x->pOut;
  ShowSqlScope scope;
  scope.pSrc = p->pSrc;
  scope.pUp = x->pScope;
  x->pScope = &scope;
  if( (p->selFlags & SF_Values)!=0 && p->pSrc && p->pSrc->nSrc==1
   && p->pSrc->a[0].fg.viaCoroutine && p->pSrc->a[0].fg.isSubquery
  ){
    /* A multi-row VALUES clause implemented using a co-routine.  The
    ** subquery holds only the first row.  The expressions for the other
    ** rows were coded as the statement was parsed and are not retained. */
    const SrcItem *pItem = &p->pSrc->a[0];
    showSqlSelect(x, pItem->u4.pSubq->pSelect);
    if( pItem->u1.nRow>1 ){
      sqlite3_str_appendf(s, " /* %u rows in all; the others are not "
                             "in the parse tree */", pItem->u1.nRow);
    }
  }else if( (p->selFlags & SF_Values)!=0 && p->pEList!=0 ){
    /* One row of a VALUES clause */
    if( !bValuesCont ) sqlite3_str_appendall(s, "VALUES");
    sqlite3_str_append(s, "(", 1);
    showSqlExprList(x, p->pEList, 0);
    sqlite3_str_append(s, ")", 1);
  }else{
    sqlite3_str_appendall(s, "SELECT ");
    if( p->selFlags & SF_Distinct ) sqlite3_str_appendall(s, "DISTINCT ");
    if( p->pEList ){
      showSqlExprList(x, p->pEList, SHOWSQL_ALIAS);
    }else{
      sqlite3_str_append(s, "*", 1);
    }
    if( p->pSrc && p->pSrc->nSrc>0 ){
      sqlite3_str_appendall(s, " FROM ");
      showSqlSrcList(x, p->pSrc);
    }
    if( p->pWhere ){
      sqlite3_str_appendall(s, " WHERE ");
      showSqlExpr(x, p->pWhere, 0);
    }
    if( p->pGroupBy ){
      sqlite3_str_appendall(s, " GROUP BY ");
      showSqlExprList(x, p->pGroupBy, 0);
    }
    if( p->pHaving ){
      sqlite3_str_appendall(s, " HAVING ");
      showSqlExpr(x, p->pHaving, 0);
    }
#ifndef SQLITE_OMIT_WINDOWFUNC
    if( p->pWinDefn ){
      const Window *pWin;
      sqlite3_str_appendall(s, " WINDOW ");
      for(pWin=p->pWinDefn; pWin; pWin=pWin->pNextWin){
        if( pWin!=p->pWinDefn ) sqlite3_str_append(s, ",", 1);
        showSqlId(s, pWin->zName);
        sqlite3_str_appendall(s, " AS ");
        showSqlWindowSpec(x, pWin);
      }
    }
#endif
  }
  x->pScope = scope.pUp;
}

/*
** Show the terms of a compound SELECT, from left to right.  Only the
** right-most term pTop carries the ORDER BY and LIMIT of the whole
** compound.  Should some other term have its own, it is wrapped in
** a subquery.
*/
static void showSqlTerms(ShowSql *x, const Select *p, const Select *pTop){
  sqlite3_str *s = x->pOut;
  int bValuesCont = 0;
  if( p->pPrior ){
    const char *zOp = "UNION";
    showSqlTerms(x, p->pPrior, pTop);
    switch( p->op ){
      case TK_ALL:         zOp = "UNION ALL";  break;
      case TK_INTERSECT:   zOp = "INTERSECT";  break;
      case TK_EXCEPT:      zOp = "EXCEPT";     break;
    }
    if( p->op==TK_ALL
     && (p->selFlags & SF_Values)!=0 && (p->pPrior->selFlags & SF_Values)!=0
    ){
      sqlite3_str_append(s, ",", 1);
      bValuesCont = 1;
    }else{
      sqlite3_str_appendf(s, " %s ", zOp);
    }
  }
  if( p!=pTop && (p->pOrderBy || p->pLimit) ){
    sqlite3_str_appendall(s, "SELECT * FROM (");
    showSqlCore(x, p, bValuesCont);
    showSqlTail(x, p);
    sqlite3_str_append(s, ")", 1);
  }else{
    showSqlCore(x, p, bValuesCont);
  }
}

/* Append a complete SELECT statement. */
static void showSqlSelect(ShowSql *x, const Select *p){
  ShowSqlScope scope;
  if( p==0 ){
    sqlite3_str_appendall(x->pOut, "<nil-select>");
    return;
  }
  if( p->selFlags & SF_WhereBegin ){
    sqlite3_str_appendall(x->pOut, "<sqlite3WhereBegin()>");
    return;
  }
  showSqlWith(x, p->pWith);
  showSqlTerms(x, p, p);
  /* The ORDER BY and LIMIT of a compound refer to its result columns, but
  ** the FROM clause of the right-most term is as good a scope as any. */
  scope.pSrc = p->pSrc;
  scope.pUp = x->pScope;
  x->pScope = &scope;
  showSqlTail(x, p);
  x->pScope = scope.pUp;
}

/*
** Print on standard output SQL text that has (approximately) the meaning of
** the Select object p.  See the comment above for limitations.
*/
void sqlite3ShowSelectAsSql(const Select *p){
  ShowSql x;
  char *z;
  x.pOut = sqlite3_str_new(0);
  x.pScope = 0;
  showSqlSelect(&x, p);
  z = sqlite3_str_finish(x.pOut);
  fprintf(stdout, "%s;\n", z ? z : "<out of memory>");
  fflush(stdout);
  sqlite3_free(z);
}
/* End of sqlite3ShowSelectAsSql()
******************************************************************************/

/*
** These simplified versions of the tree-view routines omit unnecessary
** parameters.  These variants are intended to be used from a symbolic
** debugger, such as "gdb", during interactive debugging sessions.
**
** This routines are given external linkage so that they will always be
** accessible to the debugging, and to avoid warnings about unused
** functions.  But these routines only exist in debugging builds, so they
** do not contaminate the interface.
**
** See Also:
**
**     sqlite3ShowWhereTerm() in where.c
*/
void sqlite3ShowExpr(const Expr *p){ sqlite3TreeViewExpr(0,p,0); }
void sqlite3ShowExprList(const ExprList *p){ sqlite3TreeViewExprList(0,p,0,0);}
void sqlite3ShowIdList(const IdList *p){ sqlite3TreeViewIdList(0,p,0,0); }
void sqlite3ShowSrcList(const SrcList *p){
  TreeView *pView = 0;
  sqlite3TreeViewPush(&pView, 0);
  sqlite3TreeViewLine(pView, "SRCLIST");
  sqlite3TreeViewSrcList(pView,p);
  sqlite3TreeViewPop(&pView);
}
void sqlite3ShowSelect(const Select *p){ sqlite3TreeViewSelect(0,p,0); }
void sqlite3ShowWith(const With *p){ sqlite3TreeViewWith(0,p,0); }
void sqlite3ShowUpsert(const Upsert *p){ sqlite3TreeViewUpsert(0,p,0); }
#ifndef SQLITE_OMIT_TRIGGER
void sqlite3ShowTriggerStep(const TriggerStep *p){
  sqlite3TreeViewTriggerStep(0,p,0,0);
}
void sqlite3ShowTriggerStepList(const TriggerStep *p){
  sqlite3TreeViewTriggerStep(0,p,0,1);
}
void sqlite3ShowTrigger(const Trigger *p){ sqlite3TreeViewTrigger(0,p,0,0); }
void sqlite3ShowTriggerList(const Trigger *p){ sqlite3TreeViewTrigger(0,p,0,1);}
#endif
#ifndef SQLITE_OMIT_WINDOWFUNC
void sqlite3ShowWindow(const Window *p){ sqlite3TreeViewWindow(0,p,0); }
void sqlite3ShowWinFunc(const Window *p){ sqlite3TreeViewWinFunc(0,p,0); }
#endif

#endif /* SQLITE_DEBUG */
