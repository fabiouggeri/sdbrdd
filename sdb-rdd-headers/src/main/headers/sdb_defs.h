#ifndef _SDB_API_DEFS_H_

#define _SDB_API_DEFS_H_

#include "sdb_types.h"

#ifndef _SDB_RDDNAME_
   #define _SDB_RDDNAME_ "SDBRDD"
#endif

#ifdef _SDB_CC_BCC_
   #define _strcmpi  strcmpi
   #define _strnicmp strnicmp
#endif

#if defined(_SDB_OS_LINUX_)
   #define MAX_PATH PATH_MAX
#endif

#define STRINGER(x)  #x

#define BIT_FIELD

#ifdef __XHB__

   #ifndef HB_FT_NONE
      #define HB_FT_NONE            HB_IT_NIL
      #define HB_FT_STRING          HB_IT_STRING   /* "C" */
      #define HB_FT_LOGICAL         HB_IT_LOGICAL  /* "L" */
      #define HB_FT_DATE            HB_IT_DATE     /* "D" */
      #define HB_FT_LONG            HB_IT_LONG     /* "N" */
      #define HB_FT_INTEGER         HB_IT_INTEGER  /* "I" */
      #define HB_FT_DOUBLE          HB_IT_DOUBLE   /* "B" */
      #define HB_FT_MEMO            HB_IT_MEMO     /* "M" */
//      #define HB_FT_FLOAT           HB_IT_NIL      /* "F" */
//      #define HB_FT_CURRENCY        HB_IT_NIL      /* "Y" */
//      #define HB_FT_CURDOUBLE       HB_IT_NIL      /* "Z" */
//      #define HB_FT_IMAGE           HB_IT_NIL      /* "P" */
//      #define HB_FT_BLOB            HB_IT_NIL      /* "W" */
//      #define HB_FT_TIME            HB_IT_NIL      /* "T" */
//      #define HB_FT_TIMESTAMP       HB_IT_NIL      /* "@" */
//      #define HB_FT_MODTIME         HB_IT_NIL      /* "=" */
//      #define HB_FT_ROWVER          HB_IT_NIL      /* "^" */
//      #define HB_FT_AUTOINC         HB_IT_NIL      /* "+" */
//      #define HB_FT_VARLENGTH       HB_IT_NIL      /* "Q" */
//      #define HB_FT_ANY             HB_IT_NIL      /* "V" */
//      #define HB_FT_OLE             HB_IT_NIL      /* "G" */
   #endif

   #define HB_INT32  LONG
   #define HB_UINT32 ULONG

   #ifndef HB_SIZE
      #define HB_SIZE ULONG
   #endif

   #ifndef HB_USHORT
      #define HB_USHORT USHORT
   #endif

   #ifndef HB_SHORT
      #define HB_SHORT SHORT
   #endif

   #ifndef HB_LONGLONG
      #define HB_LONGLONG  LONGLONG
   #endif

   #ifndef HB_ULONGLONG
      #define HB_ULONGLONG  ULONGLONG
   #endif

   #ifndef HB_BOOL
      #define HB_BOOL   BOOL
   #endif

   #ifndef HB_BYTE
      #define HB_BYTE   BYTE
   #endif

   #ifndef HB_MAXINT
      #define HB_MAXINT CFL_INT64
   #endif

   #ifndef HB_ERRCODE
      #define HB_ERRCODE ERRCODE
   #endif

   #ifndef HB_SUCCESS
      #define HB_SUCCESS SUCCESS
   #endif

   #ifndef HB_FAILURE
      #define HB_FAILURE FAILURE
   #endif

   #ifndef HB_TRUE
      #define HB_TRUE TRUE
   #endif

   #ifndef HB_FALSE
      #define HB_FALSE FALSE
   #endif

   #ifndef hb_itemMove
      #define hb_itemMove hb_itemForwardValue
   #endif

   #ifndef hb_itemPutCLPtr
      #define hb_itemPutCLPtr hb_itemPutCPtr 
   #endif

   #ifndef hb_dynsymName
      #define hb_dynsymName(s) (s)->pSymbol->szName 
   #endif
  
   #ifndef hb_itemReturnForward
      #define hb_itemReturnForward(i) hb_itemReturn((i))
   #endif

   #ifndef hb_itemSetNil
      #define hb_itemSetNil(i) hb_itemClear((i))
   #endif

   #ifndef hb_itemPutCLConst
      #define hb_itemPutCLConst hb_itemPutCLStatic
   #endif

   #ifndef hb_itemPutCConst
      #define hb_itemPutCConst hb_itemPutCStatic
   #endif

   #ifndef HB_RDD_MAX_ALIAS_LEN
      #define HB_RDD_MAX_ALIAS_LEN 63
   #endif

   #ifndef HB_RDD_MAX_DRIVERNAME_LEN
      #define HB_RDD_MAX_DRIVERNAME_LEN 31
   #endif

   #ifndef HB_THREAD_NO
      #define HB_THREAD_NO DWORD
   #endif

   #ifndef HB_TYPE
      #define HB_TYPE USHORT
   #endif

   #ifndef HB_ISNIL
      #define HB_ISNIL( n ) ISNIL( n )
      #define HB_ISCHAR( n ) ISCHAR( n )
      #define HB_ISNUM( n ) ISNUM( n )
      #define HB_ISLOG( n ) ISLOG( n )
      #define HB_ISDATE( n ) ISDATE( n )
      #define HB_ISMEMO( n ) ISMEMO( n )
      #define HB_ISBYREF( n ) ISBYREF( n )
      #define HB_ISARRAY( n ) ISARRAY( n )
      #define HB_ISOBJECT( n ) ISOBJECT( n )
      #define HB_ISBLOCK( n ) ISBLOCK( n )
      #define HB_ISPOINTER( n ) ISPOINTER( n )
      #define HB_ISHASH( n ) ISHASH( n )
   #endif

#else
   #define HB_INT32  HB_LONG
   #define HB_UINT32 HB_ULONG
#endif

#ifndef HB_RECNO
   #define HB_RECNO HB_UINT32
#endif

#define SDB_RECNO        CFL_INT64
#define SDB_RECNO_EOF    0x7FFFFFFFFFFFFFFF
#define SDB_MAX_RECNO    (SDB_RECNO_EOF - 1)
#define _SDB_RECNO_SIZE_ sizeof(SDB_RECNO)

#define SDB_MAX_SCHEMA_NAME_LEN 60
#define SDB_MAX_TABLE_NAME_LEN 60

#define SDB_DEFAULT_LOCK_SERVER_PORT 7655

/* Lock Control messages */
#define SDB_LS_CMD_CONNECT        'A'
#define SDB_LS_CMD_DISCONNECT     'B'
#define SDB_LS_CMD_OPEN_SHARED    'C'
#define SDB_LS_CMD_OPEN_EXCLUSIVE 'D'
#define SDB_LS_CMD_CLOSE          'E'
#define SDB_LS_CMD_CLOSE_ALL      'F'
#define SDB_LS_CMD_LOCK_FILE      'G'
#define SDB_LS_CMD_LOCK_REC       'H'
#define SDB_LS_CMD_UNLOCK_REC     'I'
#define SDB_LS_CMD_UNLOCK_ALL     'J'
#define SDB_LS_CMD_LIST_SESSIONS  'K'
#define SDB_LS_CMD_SESSION_INFO   'L'
#define SDB_LS_CMD_SHUTDOWN       'M'
#define SDB_LS_CMD_LOG_LEVEL      'N'

#define SDB_LS_IO_BUFFER_SIZE      1024 // 8192

#define SDB_LS_RET_SUCCESS         0
#define SDB_LS_RET_FAILED          1
#define SDB_LS_RET_INVALID_REQUEST 0xFFFF

#define SDB_OBJ_CONNECTION  51
#define SDB_OBJ_STATEMENT   52
#define SDB_OBJ_PRODUCT     53
#define SDB_OBJ_DATABASE    54
#define SDB_OBJ_SCHEMA      55
#define SDB_OBJ_TABLE       56
#define SDB_OBJ_FIELD       57
#define SDB_OBJ_INDEX       58
#define SDB_OBJ_RECORD      59
#define SDB_OBJ_TRANSACTION 60
#define SDB_OBJ_PARAM       61
#define SDB_OBJ_THREAD      62
#define SDB_OBJ_LOB         63

/* Clipper datatypes */
#define SDB_CLP_DATATYPE  CFL_UINT8
#define SDB_CLP_UNKNOWN   0
#define SDB_CLP_CHARACTER 1
#define SDB_CLP_NUMERIC   2
#define SDB_CLP_LOGICAL   4
#define SDB_CLP_DATE      8
#define SDB_CLP_TIMESTAMP 9
#define SDB_CLP_INTEGER   10
#define SDB_CLP_BIGINT    11
#define SDB_CLP_FLOAT     12
#define SDB_CLP_DOUBLE    13
#define SDB_CLP_MEMO_LONG 65
#define SDB_CLP_BLOB      67
#define SDB_CLP_CLOB      68
#define SDB_CLP_IMAGE     70
#define SDB_CLP_LONG_RAW  71
#define SDB_CLP_CURSOR    72
#define SDB_CLP_ROWID     99

#define SDB_MEM_REALLOC(p,s) hb_xrealloc(p,(HB_SIZE)(s))
#define SDB_MEM_COPY(d,s,l)  memcpy(d, s, l)
#define SDB_MEM_SET(s,d,l)   memset(s, d, l)
#define SDB_MEM_ALLOC(s)     hb_xgrab((HB_SIZE)(s))
#define SDB_MEM_FREE(s)      if(s!=NULL) hb_xfree(s)


#endif
