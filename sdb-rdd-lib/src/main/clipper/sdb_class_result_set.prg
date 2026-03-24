#include "hbclass.ch"
#include "error.ch"

#include "sdb.ch"
#include "sdb_errors.ch"

#ifndef __XHB__

#xTranslate CheckStmt() => If ::oStatement == Nil; __errRT_BASE( EG_ARG, SDB_ERROR_INVALID_STMT, "Invalid statement handle", ProcName( 0 ) ); EndIf

Create Class SDB_ResultSet
   Hidden:
      Data oStatement
      Data hColumns
      Data aColumns
      Method ColumnsInfo()
         
   Exported:
      Method New( oStatement ) Constructor
      Method Close()
      Method ColumnInfo( xCol )
      Method ColumnIndex( cColumn )
      Method GetValue( xColumn )
      Method PreFetchSize( nSize )
      Method Next()
      Method IsEOF()
      Method RowNum()
EndClass

Method New( oStatement ) Class SDB_ResultSet
   If ! HB_IsObject( oStatement )
      __errRT_BASE( EG_ARG, SDB_ERROR_INVALID_STMT, "Invalid statemnent", ProcName( 0 ) )
   EndIf
   If ! oStatement:IsQuery()
      __errRT_BASE( EG_ARG, SDB_ERROR_INVALID_STMT, "Statemnent is not a query", ProcName( 0 ) )
   EndIf
   ::oStatement := oStatement
Return Self

Method Close() Class SDB_ResultSet
   ::oStatement := Nil
   ::hColumns := Nil
   ::aColumns := Nil
Return Self

Method ColumnsInfo() Class SDB_ResultSet
   Local nNumCols
   Local nCol 
   Local oColInfo

   If ! HB_IsHash( ::hColumns ) .Or. ! HB_IsArray( ::aColumns )
      nNumCols := sdb_StmtNumCols( ::oStatement:Handle )
      ::hColumns := {=>}
      ::aColumns := Array( nNumCols )
      For nCol := 1 To nNumCols
         oColInfo := sdb_StmtGetColumn( ::oStatement:Handle, nCol )
         If oColInfo != Nil
            ::hColumns[ oColInfo:Name ] := oColInfo
            ::aColumns[ nCol ] := oColInfo
         EndIf
      Next
   EndIf
Return Self

Method ColumnIndex( cColumn ) Class SDB_ResultSet
   Local cColUpper := Upper( Alltrim( cColumn ) )
   Local hInfo := ::ColumnsInfo():hColumns

   If HB_HHasKey( hInfo, cColUpper )
      Return hInfo[ cColUpper ]:Position
   EndIf
Return 0

Method ColumnInfo( xCol ) Class SDB_ResultSet
   Local xInfo
   Local cColUpper

   If HB_IsNumeric( xCol )
      xInfo := ::ColumnsInfo():aColumns
      If xCol > 0 .And. xCol <= Len( xInfo )
         Return xInfo[ xCol ]
      EndIf
   ElseIf HB_IsString( xCol ) 
      cColUpper := Upper( Alltrim( xCol ) )
      xInfo := ::ColumnsInfo():hColumns
      If HB_HHasKey( xInfo, cColUpper )
         Return xInfo[ cColUpper ]
      EndIf
   EndIf
Return Nil

Method PreFetchSize( nSize ) Class SDB_ResultSet 
   CheckStmt()
Return StmtPreFetchSize( ::oStatement:Handle, nSize )

Method Next() Class SDB_ResultSet 
   CheckStmt()
Return sdb_StmtFetch( ::oStatement:Handle )

Method GetValue( xColumn ) Class SDB_ResultSet
   Local nCol
   Local oColInfo
   CheckStmt()
   If HB_IsString( xColumn )
      oColInfo := ::ColumnInfo( xColumn )
      If oColInfo == Nil
         __errRT_BASE( EG_ARG, SDB_ERROR_INVALID_COLUMN, "Invalid column name '" + xColumn + "'", ProcName( 0 ) )
      EndIf
      nCol := oColInfo:Position
   Else
      nCol := xColumn
   EndIf
Return sdb_StmtGetValue( ::oStatement:Handle, nCol )

Method IsEOF() Class SDB_ResultSet 
   CheckStmt()
Return StmtIsEof( ::oStatement:Handle )

Method RowNum() Class SDB_ResultSet 
   CheckStmt()
Return sdb_StmtRowCount( ::oStatement:Handle )


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