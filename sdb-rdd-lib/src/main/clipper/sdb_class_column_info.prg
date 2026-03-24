#include "hbclass.ch"
#include "sdb.ch"

#ifndef __XHB__

Create Class SDB_ColumnInfo
   Hidden:
      Data nPosition
      Data cName
      Data nClpType
      Data cDBType
      Data nSize
      Data nSizeInBytes
      Data nPrecision
      Data nScale
      Data bNullable

   Exported:
      Access Position                InLine ::nPosition
      Assign Position( nPos )        InLine ::nPosition := nPos
          
      Access Name                    InLine ::cName
      Assign Name( cName )           InLine ::cName := cName
          
      Access ClpType                 InLine ::nClpType
      Assign ClpType( nType )        InLine ::nClpType := nType
    
      Access DBType                  InLine ::cDBType
      Assign DBType( cType )         InLine ::cDBType := cType
    
      Access Size                    InLine ::nSize
      Assign Size( nSize )           InLine ::nSize := nSize

      Access SizeInBytes             InLine ::nSizeInBytes
      Assign SizeInBytes( nSize )    InLine ::nSizeInBytes := nSize

      Access Precision               InLine ::nPrecision
      Assign Precision( nPrecision ) InLine ::nPrecision := nPrecision

      Access Scale                   InLine ::nScale
      Assign Scale( nScale )         InLine ::nScale := nScale

      Access Nullable                InLine ::bNullable
      Assign Nullable( bNull )       InLine ::bNullable := bNull

      Method New() Constructor

EndClass

Method New() Class SDB_ColumnInfo
Return Self

#endif