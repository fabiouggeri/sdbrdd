#include "hbclass.ch"
#include "error.ch"

#include "sdb_errors.ch"

#ifndef __XHB__

#define IS_CONNECTED()  ( ::pConnection != Nil )

#xTranslate CheckConnection() => If ::pConnection == Nil; __errRT_BASE( EG_ARG, SDB_ERROR_NOT_CONNECTED, "Not connected to database", ProcName( 0 ) ); EndIf

Create Class SDB_Connection
   Hidden:
      Data cDatabase
      Data cProduct
      Data pConnection


   Exported:
      Access Handle InLine ::pConnection
      Method New( cDatabase, cProduct ) Constructor
      Method Login( cUser, cPswd )
      Method Logout()
      Method ExecFunction( ... )
      Method ExecProcedure( ... )
      Method ExecSql( ... )
      Method ExecSqlMany( ... )
      Method StmtPrepare( cSQL )
      Method BeginTransaction()
      Method PrepareTransaction()
      Method CommitTransaction()
      Method RollbackTransaction()
      Method IsTransaction()
      Method IsTable( cTable )
      Method IsIndex( cTable, cIndex )
      Method IsLogged()
      Method RollbackOnError( lRollback )
      Method QueryDefaultPrecision( nPrecision )
      Method QueryDefaultScale( nScale )
      Method QueryC1Logical( lC1Logical )
      Method AddTransalation( cClipperFun, xReturnType, nSqlArgs, cExprSQL )
      Method LobNew( nLobType )
      Method ServerVersion()
      Method ClientVersion()
      Method CurrentSchema( cSchema )
      Method StmtCacheSize( nSize )
      Method BreakOperation()
      Method DatabaseName()
      Method HintsEnable( lEnable )

EndClass

Method New( cDatabase, cProduct ) Class SDB_Connection

   If ! HB_IsString( cDatabase ) .Or. Empty( cDatabase )
      __errRT_BASE( EG_ARG, SDB_ERROR_INVALID_DATABASE, "Database not specified", ProcName( 0 ) )
   EndIF

   If cProduct != Nil .And. ! HB_IsString( cProduct )
      __errRT_BASE( EG_ARG, SDB_ERROR_INVALID_PRODUCT, "Invalid product name", ProcName( 0 ) )
   EndIF
   
   ::cDatabase   := cDatabase
   ::cProduct    := cProduct
   ::pConnection := Nil
Return Self

Method Login( cUser, cPswd ) Class SDB_Connection
   If IS_CONNECTED()
      __errRT_BASE( EG_ARG, SDB_ERROR_ALREADY_CONNECTED, "Alreary connected to database", ProcName( 0 ) )
   EndIf

   ::pConnection := SDB_Connect( ::cDatabase, cUser, cPswd, ::cProduct )

Return ::pConnection != Nil

Method Logout() Class SDB_Connection
   If IS_CONNECTED()
      sdb_Disconnect( ::pConnection )
      ::pConnection := Nil
   EndIf
Return .F.

Method ExecFunction( ... ) Class SDB_Connection
   CheckConnection()
Return sdb_conn_ExecFunction( ::pConnection, ... )

Method ExecProcedure( ... ) Class SDB_Connection
   CheckConnection()
Return sdb_conn_ExecProcedure( ::pConnection, ... )

Method ExecSql( ... ) Class SDB_Connection
   CheckConnection()
Return sdb_conn_ExecSql( ::pConnection, ... )

Method ExecSqlMany( ... ) Class SDB_Connection
   CheckConnection()
Return sdb_conn_ExecSqlMany( ::pConnection, ... )

Method StmtPrepare( cSQL ) Class SDB_Connection
   CheckConnection()
Return SDB_Statement():New( Self, cSql )

Method BeginTransaction() Class SDB_Connection
   CheckConnection()
Return sdb_conn_BeginTransaction( ::pConnection )

Method PrepareTransaction() Class SDB_Connection
   CheckConnection()
Return sdb_PrepareTransaction( ::pConnection )

Method CommitTransaction() Class SDB_Connection
   CheckConnection()
Return sdb_CommitTransaction( ::pConnection )

Method RollbackTransaction() Class SDB_Connection
   CheckConnection()
Return sdb_RollbackTransaction( ::pConnection )

Method IsTransaction() Class SDB_Connection
   CheckConnection()
Return sdb_IsTransaction( ::pConnection )

Method IsTable( cTable ) Class SDB_Connection
   CheckConnection()
Return sdb_conn_IsTable( ::pConnection, cTable )

Method IsIndex( cTable, cIndex ) Class SDB_Connection
   CheckConnection()
Return sdb_conn_IsIndex( ::pConnection, cTable, cIndex )

Method IsLogged() Class SDB_Connection
   CheckConnection()
Return sdb_IsLogged( ::pConnection )

Method RollbackOnError( lRollback ) Class SDB_Connection
   CheckConnection()
Return sdb_RollbackOnError( ::pConnection, lRollback )

Method QueryDefaultPrecision( nPrecision ) Class SDB_Connection
   CheckConnection()
Return sdb_QueryDefaultPrecision( ::pConnection, nPrecision )

Method QueryDefaultScale( nScale ) Class SDB_Connection
   CheckConnection()
Return sdb_QueryDefaultScale( ::pConnection, nScale )

Method QueryC1Logical( lC1Logical ) Class SDB_Connection
   CheckConnection()
Return sdb_QueryC1Logical( ::pConnection, lC1Logical )

Method AddTransalation( cClipperFun, xReturnType, nSqlArgs, cExprSQL ) Class SDB_Connection
   CheckConnection()
Return sdb_AddTranslation( ::pConnection, cClipperFun, xReturnType, nSqlArgs, cExprSQL )

Method LobNew( nLobType ) Class SDB_Connection
   CheckConnection()
Return SDB_Lob():New( Self, nLobType )

Method ServerVersion() Class SDB_Connection
   CheckConnection()
Return sdb_ServerVersion( ::pConnection )

Method ClientVersion() Class SDB_Connection
   CheckConnection()
Return sdb_ClientVersion( ::pConnection )

Method CurrentSchema( cSchema ) Class SDB_Connection
   CheckConnection()
Return sdb_CurrentSchema( ::pConnection, cSchema )

Method StmtCacheSize( nSize ) Class SDB_Connection
   CheckConnection()
Return sdb_conn_StmtCacheSize( ::pConnection, nSize )

Method BreakOperation() Class SDB_Connection
   CheckConnection()
Return sdb_BreakOperation( ::pConnection )

Method DatabaseName() Class SDB_Connection
   CheckConnection()
Return sdb_DatabaseName( ::pConnection )

Method HintsEnable( lEnable ) Class SDB_Connection
   CheckConnection()
Return sdb_conn_HintsEnable( ::pConnection, lEnable )

#endif