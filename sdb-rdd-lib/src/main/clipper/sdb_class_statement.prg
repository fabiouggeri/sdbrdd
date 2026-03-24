#include "hbclass.ch"
#include "error.ch"

#include "sdb.ch"
#include "sdb_errors.ch"

#ifndef __XHB__

#xTranslate CheckStmtHandle() => If ::pHandle == Nil; __errRT_BASE( EG_ARG, SDB_ERROR_INVALID_STMT, "Invalid statement handle", ProcName( 0 ) ); EndIf

Create Class SDB_Statement
   Hidden:
      Data pHandle
         
   Exported:
      Access Handle InLine ::pHandle
      Method New( oConnection, cSQL ) Constructor
      Method Free()
      Method Execute()
      Method SetParam( xPar, xVal )
      Method GetParam( xPar )
      Method PreFetchSize( nSize )
      Method IsQuery()
      Method IsPlSql()
      Method IsDDL()
      Method IsDML()
      Method Type()
EndClass

Method New( oConnection, cSQL ) Class SDB_Statement
   If ! HB_IsObject( oConnection )
      __errRT_BASE( EG_ARG, SDB_ERROR_INVALID_STMT, "Invalid connection", ProcName( 0 ) )
   EndIf
   If ! HB_IsString( cSQL ) .Or. Empty( cSQL )
      __errRT_BASE( EG_ARG, SDB_ERROR_INVALID_STMT, "SQL statement not provided", ProcName( 0 ) )
   EndIf
   ::pHandle := sdb_conn_StmtPrepare( oConnection:Handle, cSql )
Return Self

Method Free() Class SDB_Statement 
   CheckStmtHandle()
   sdb_StmtFree( ::pHandle )
   ::pHandle := Nil
Return Nil

Method Execute() Class SDB_Statement 
   Local xResult
   CheckStmtHandle()
   xResult := SDB_StmtExecute( ::pHandle )
   If ! ::IsQuery() 
      Return xResult
   EndIf

   If HB_IsLogical( xResult ) .And. xResult
      Return SDB_ResultSet():New( Self )
   EndIf
Return Nil

Method SetParam( xPar, xVal  ) Class SDB_Statement 
   CheckStmtHandle()
   sdb_StmtSetParam( ::pHandle, xPar, xVal )
Return Self

Method GetParam( xPar ) Class SDB_Statement 
   CheckStmtHandle()
Return sdb_StmtGetParam( ::pHandle, xPar )

Method PreFetchSize( nSize ) Class SDB_Statement 
   CheckStmtHandle()
Return StmtPreFetchSize( ::pHandle, nSize )

Method IsQuery() Class SDB_Statement 
   CheckStmtHandle()
Return sdb_StmtGetType( ::pHandle ) == SDB_STMT_QUERY

Method IsPlSql() Class SDB_Statement 
   CheckStmtHandle()
Return sdb_StmtGetType( ::pHandle ) == SDB_STMT_PLSQL

Method IsDDL() Class SDB_Statement 
   CheckStmtHandle()
Return sdb_StmtGetType( ::pHandle ) == SDB_STMT_DDL

Method IsDML() Class SDB_Statement 
   CheckStmtHandle()
Return sdb_StmtGetType( ::pHandle ) == SDB_STMT_DML

Method Type() Class SDB_Statement 
   CheckStmtHandle()
Return sdb_StmtGetType( ::pHandle )

#pragma BEGINDUMP

#include "hbapi.h"
#include "hbvmpub.h"
#include "hbapiitm.h"
#include "hbapierr.h"
#include "hbapicls.h"

#include "sdb_defs.h"
#include "sdb_statement.h"
#include "sdb_info.h"
#include "sdb_errors.ch"

#include "cfl_str.h"

HB_FUNC_STATIC(STMTISEOF) {
   SDB_STATEMENTP pStmt = hb_parptr(1);
   if (pStmt) {
      hb_retl((HB_BOOL)sdb_stmt_isEOF(pStmt));
   } else {
      hb_retl(HB_TRUE);
   }
}

HB_FUNC_STATIC(STMTPREFETCHSIZE) {
   SDB_STATEMENTP pStmt = hb_parptr(1);

   if (pStmt == NULL) {
      hb_errRT_BASE_SubstR(EG_ARG, SDB_ERROR_INVALID_STMT, "Invalid statement", HB_ERR_FUNCNAME, HB_ERR_ARGS_BASEPARAMS);
      return;
   }
   hb_retni(pStmt->fetchSize);
   if (HB_ISNUM(2)) {
      sdb_stmt_setPreFetchSize(pStmt, hb_parni(2));
   }
}

#pragma ENDDUMP

#endif