#ifndef SDB_FIELD_H_

#define SDB_FIELD_H_

#include "hbapi.h"

#include "cfl_types.h"
#include "cfl_str.h"

#include "sdb_defs.h"

#define SDB_FIELD_DATA       0
#define SDB_FIELD_PK         1
#define SDB_FIELD_DEL_FLAG   2
#define SDB_FIELD_DATA_INDEX 3
#define SDB_FIELD_INDEX      4
#define SDB_FIELD_ROWID      5

#define SDB_FIELD_PK_ORDER    10000
#define SDB_FIELD_DEL_ORDER   10001
#define SDB_FIELD_ROWID_ORDER 10002

#define SDB_VALUE_TEXT 1
#define SDB_VALUE_PARAM 2

#define SET_MODE_CLIENT        0
#define SET_MODE_INSERT_SERVER 1
#define SET_MODE_UPDATE_SERVER 2
#define SET_MODE_ALL_SERVER   (SET_MODE_INSERT_SERVER | SET_MODE_UPDATE_SERVER)
#define SET_MODE_ALL_FLAGS    (SET_MODE_INSERT_SERVER | SET_MODE_UPDATE_SERVER)

#define IS_PK_FIELD(f)          ((f)->fieldType == SDB_FIELD_PK)
#define IS_DEL_FIELD(f)         ((f)->fieldType == SDB_FIELD_DEL_FLAG)
#define IS_ROWID_FIELD(f)       ((f)->fieldType == SDB_FIELD_ROWID)
#define IS_DATA_FIELD(f)        ((f)->fieldType == SDB_FIELD_DATA || (f)->fieldType == SDB_FIELD_DATA_INDEX)
#define IS_INDEX_FIELD(f)       ((f)->fieldType == SDB_FIELD_INDEX || (f)->fieldType == SDB_FIELD_DATA_INDEX)
#define IS_INDEX_EXPR_FIELD(f)  ((f)->fieldType == SDB_FIELD_INDEX)
#define IS_SERVER_SET_UPDATE(f) ((f)->setMode & SET_MODE_UPDATE_SERVER)
#define IS_SERVER_SET_INSERT(f) ((f)->setMode & SET_MODE_INSERT_SERVER)
#define IS_SERVER_SET(f)        (IS_SERVER_SET_INSERT(f) || IS_SERVER_SET_UPDATE(f))

#define SDB_DEFAULT_PK_CACHE_SIZE 20

struct _SDB_FIELD {
   CFL_UINT8    objectType;
   CFL_STRP     clpName;
   CFL_STRP     dbName;
   CFL_UINT8    clpType;
   CFL_UINT8    fieldType;
   CFL_UINT16   tablePos;
   CFL_UINT32   length;
   CFL_UINT8    decimals;
   CFL_UINT8    setMode;
   CFL_STRP     clpIndexName;
   CFL_STRP     dbIndexName;
   CFL_STRP     indexAscHint;
   CFL_STRP     indexDescHint;
   CFL_STRP     clpExpression;
   CFL_STRP     dbExpression;            /* SQL expression */
   CFL_UINT16   order;
   CFL_UINT8    contextPos;
   CFL_STRP     contextVal;
   CFL_BOOL     isVirtual BIT_FIELD;
   CFL_BOOL     isRightPadded BIT_FIELD;
};

struct _SDB_WAFIELD {
   SDB_FIELDP field;
   CFL_UINT16 queryPos;
   CFL_BOOL   isContext BIT_FIELD;
   CFL_BOOL   isChanged BIT_FIELD;
};

extern SDB_FIELDP sdb_field_new(const char *clpColName, CFL_UINT32 clpColNameLen, const char *dbColName, CFL_UINT32 dbColNameLen,
                                CFL_UINT8 fieldType, CFL_UINT8 clpType, CFL_UINT32 clpDataLen, CFL_UINT8 clpDataDec, CFL_BOOL bRightPadded);
extern void sdb_field_free(SDB_FIELDP field);
extern CFL_BOOL sdb_field_isMemo(SDB_FIELDP field);

#endif

