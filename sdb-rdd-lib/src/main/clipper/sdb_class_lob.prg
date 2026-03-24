#include "hbclass.ch"
#include "error.ch"

#include "sdb.ch"
#include "sdb_errors.ch"

#ifndef __XHB__

#xTranslate CheckLob() => If ::pHandle == Nil; __errRT_BASE( EG_ARG, SDB_ERROR_INVALID_LOB, "Invalid LOB handle", ProcName( 0 ) ); EndIf

Create Class SDB_Lob
   Hidden:
      Data pHandle

   Exported:
      Method New( oConn, nLobType ) Constructor
      Method Open()
      Method Close()
      Method Read( cBuffer, nStart, nLen )
      Method Write( cBuffer, nStart, nLen )
      Method Free()
EndClass

Method New( oConn, nLobType ) Class SDB_Lob

   If ! HB_IsNumeric( nLobType )
      __errRT_BASE( EG_ARG, SDB_ERROR_INVALID_DATATYPE, "LOB type not specified", ProcName( 0 ) )
   EndIf

   If nLobType != SDB_CLP_BLOB .and. nLobType != SDB_CLP_CLOB
      __errRT_BASE( EG_ARG, SDB_ERROR_INVALID_DATATYPE, "Invalid LOB type", ProcName( 0 ) )
   EndIf

   If HB_IsObject( oConn )
      ::pHandle := sdb_conn_LobNew( oConn:Handle, nLobType )
   ElseIf HB_IsPointer( oConn )
      ::pHandle := sdb_conn_LobNew( oConn, nLobType )
   Else
      __errRT_BASE( EG_ARG, SDB_ERROR_INVALID_CONNECTION, "Connection not specified", ProcName( 0 ) )
   EndIf

Return Self

Method Open() Class SDB_Lob
   CheckLob()
Return sdb_LobOpen( ::pHandle )

Method Close() Class SDB_Lob
   CheckLob()
Return sdb_LobClose( ::pHandle )

Method Read( cBuffer, nStart, nLen ) Class SDB_Lob
   CheckLob()
Return sdb_LobRead( ::pHandle, @cBuffer, nStart, nLen )

Method Write( cBuffer, nStart, nLen ) Class SDB_Lob
   CheckLob()
Return sdb_LobWrite( ::pHandle, cBuffer, nStart, nLen )

Method Free() Class SDB_Lob
   CheckLob()
   sdb_LobFree( ::pHandle )
   ::pHandle := Nil
Return Nil

#endif