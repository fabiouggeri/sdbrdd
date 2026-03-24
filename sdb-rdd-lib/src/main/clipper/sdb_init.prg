#include "rddsys.ch"
#include "error.ch"
#include "sdb_errors.ch"
#include "sdb.ch"

#pragma -b-



Function SDB_RegisterWithOracleDrive( cDriverAlias )
   Local oError
   sdb_InitLog()
   sdb_RegisterRDD( cDriverAlias )
   If ! sdb_OraRegisterDriver()
      oError := ErrorNew()
      oError:GenCode     := SDB_ERROR_REGISTERING_DRIVER
      oError:SubSystem   := "SDBRDD"
      oError:FileName    := "sdb_init.prg"
      oError:Operation   := "SDB_RegisterOracleDrive"
      oError:Description := "Unable to register Oracle driver in SDB : " + AllTrim( Str( sdb_GetErrorCode() ) ) + " => " + sdb_GetErrorMsg()
      oError:Severity    := ES_ERROR
      Eval( ErrorBlock(), oError )
   EndIf
Return Nil

Static Function LogLevelToValue( cLevel )
   Local nLevel

   cLevel := Upper( AllTrim( cLevel ) )
   If cLevel == "OFF"
      nLevel := SDB_LOG_LEVEL_OFF
   ElseIf cLevel == "ERROR"
      nLevel := SDB_LOG_LEVEL_ERROR
   ElseIf cLevel == "WARN"
      nLevel := SDB_LOG_LEVEL_WARN
   ElseIf cLevel == "INFO"
      nLevel := SDB_LOG_LEVEL_INFO
   ElseIf cLevel == "DEBUG"
      nLevel := SDB_LOG_LEVEL_DEBUG
   ElseIf cLevel == "TRACE"
      nLevel := SDB_LOG_LEVEL_TRACE
   Else
      nLevel := Val( cLevel )
      If nLevel < SDB_LOG_LEVEL_OFF .Or. nLevel > SDB_LOG_LEVEL_TRACE
         nLevel := SDB_LOG_LEVEL_ERROR
      EndIf
   EndIf

Return nLevel

Function SDB_InitLog()
   Local cLevel
   Local cLogPathName

   cLevel := GetEnv( "SDB_LOG_LEVEL" )
   If HB_IsString( cLevel ) .And. ! Empty( cLevel )
      sdb_LogLevel( LogLevelToValue( cLevel ) )
   EndIf

   cLogPathName  := GetEnv( "SDB_LOG_NAME" )
   If HB_IsString( cLogPathName ) .And. ! Empty( cLogPathName )
      sdb_LogPathName( cLogPathName )
   EndIf
Return Nil
