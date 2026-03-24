#ifndef SDB_STATEMENT_H_

#define SDB_STATEMENT_H_

#if defined(_SDB_OS_LINUX_)
   #include <linux/types.h>
#else
   #include <sys\types.h>
#endif

#include "hbapi.h"

#include "cfl_types.h"
#include "cfl_date.h"
#include "cfl_str.h"

#include "sdb_defs.h"

#define SDB_STMT_FETCH_SIZE 100

#define SDB_STMT_UNKNOWN 0
#define SDB_STMT_QUERY   1
#define SDB_STMT_DML     2
#define SDB_STMT_DDL     4
#define SDB_STMT_PLSQL   8

/* GETS */
#define sdb_stmt_getHandle(s)  ((s)->handle)
#define sdb_stmt_getParams(s)  ((s)->paramList)
#define sdb_stmt_hasParams(s)  ((s)->paramList != NULL && ! sdb_param_listIsEmpty((s)->paramList))
#define sdb_stmt_getType(s)    ((s)->type)
#define sdb_stmt_getNumCols(s) ((s)->numCols)
#define sdb_stmt_isEOF(s)      ((s)->isEof)
#define sdb_stmt_fetchCount(s) ((s)->fetchCount)

/* SETS */
#define sdb_stmt_setHandle(s, h)  ((s)->handle = h)
#define sdb_stmt_setType(s, t)    ((s)->type = t)
#define sdb_stmt_setNumCols(s, n) ((s)->numCols = n)

#define STMT_DECLARE_FUNCTION_SET_GET(funName, typeName) \
   typeName sdb_stmt_get##funName(SDB_STATEMENTP pStatement, const char *name); \
   typeName sdb_stmt_get##funName##ByPos(SDB_STATEMENTP pStatement, CFL_UINT32 pos); \
   SDB_PARAMP sdb_stmt_set##funName(SDB_STATEMENTP pStatement, const char *name, typeName val, CFL_BOOL out); \
   SDB_PARAMP sdb_stmt_set##funName##Null(SDB_STATEMENTP pStatement, const char *name, CFL_BOOL out); \
   SDB_PARAMP sdb_stmt_set##funName##ByPos(SDB_STATEMENTP pStatement, CFL_UINT32 pos, typeName val, CFL_BOOL out); \
   SDB_PARAMP sdb_stmt_set##funName##NullByPos(SDB_STATEMENTP pStatement, CFL_UINT32 pos, CFL_BOOL out)

#define STMT_DECLARE_FUNCTION_SET_GETARG(funName, typeName) \
   typeName sdb_stmt_get##funName(SDB_STATEMENTP pStatement, const char *name, typeName arg); \
   typeName sdb_stmt_get##funName##ByPos(SDB_STATEMENTP pStatement, CFL_UINT32 pos, typeName arg); \
   SDB_PARAMP sdb_stmt_set##funName(SDB_STATEMENTP pStatement, const char *name, typeName val, CFL_BOOL out); \
   SDB_PARAMP sdb_stmt_set##funName##Null(SDB_STATEMENTP pStatement, const char *name, CFL_BOOL out); \
   SDB_PARAMP sdb_stmt_set##funName##ByPos(SDB_STATEMENTP pStatement, CFL_UINT32 pos, typeName val, CFL_BOOL out); \
   SDB_PARAMP sdb_stmt_set##funName##NullByPos(SDB_STATEMENTP pStatement, CFL_UINT32 pos, CFL_BOOL out)

struct _SDB_STATEMENT {
   CFL_UINT8       objectType;
   SDB_CONNECTIONP connection;
   void            *handle;
   time_t          lastUseTime;
   SDB_PARAMLISTP  paramList;
   CFL_UINT64      fetchCount;
   CFL_UINT64      execCount;                       /* Executions count of statement */
   CFL_UINT8       precision;                       /* precision on expression columns */
   CFL_UINT8       scale;                           /* Scale on expression columns */
   CFL_UINT8       type;                            /* Statement type */
   CFL_UINT16      bufferFetchSize;                 /* Maximum of records to fetch in one roundtrip */
   CFL_UINT16      fetchSize;                       /* Number of records to fetch in next roundtrip */
   CFL_UINT32      numCols;                         /* Number of columns if query */
   CFL_BOOL        isEof                 BIT_FIELD;
   CFL_BOOL        isReleaseOnClose      BIT_FIELD; /* Release statement when the related workarea is closed? */
   CFL_BOOL        isChar1AsLogical      BIT_FIELD; /* Convert fields of datatype char(1) to logical? */
   CFL_BOOL        isLogicalParamAsChar1 BIT_FIELD; /* Logical params are converted do char(1)? */
   CFL_BOOL        isCursor              BIT_FIELD;
};

extern SDB_STATEMENTP sdb_stmt_new(SDB_CONNECTIONP conn, void *handle);
extern void sdb_stmt_free(SDB_STATEMENTP stmt);
extern SDB_STATEMENTP sdb_stmt_param(int iParam);
extern SDB_STATEMENTP sdb_stmt_itemGet(PHB_ITEM pItem);
extern PHB_ITEM sdb_stmt_itemPut(PHB_ITEM pItem, SDB_STATEMENTP pStmt);
extern SDB_STATEMENTP sdb_stmt_itemDetach(PHB_ITEM pItem);
extern CFL_BOOL sdb_stmt_prepareBufferLen(SDB_STATEMENTP pStmt, const char *sql, CFL_UINT32 len);
extern CFL_BOOL sdb_stmt_prepare(SDB_STATEMENTP pStmt, CFL_STRP sql);
extern CFL_BOOL sdb_stmt_setPreFetchSize(SDB_STATEMENTP pStatement, CFL_UINT16 fetchSize);
extern CFL_BOOL sdb_stmt_fetchNext(SDB_STATEMENTP pStatement, CFL_BOOL autoPreFetch);
extern PHB_ITEM sdb_stmt_getQueryValue(SDB_STATEMENTP pStatement, CFL_UINT32 pos, PHB_ITEM pItem);
extern CFL_BOOL sdb_stmt_execute(SDB_STATEMENTP pStatement, CFL_UINT64 *pulAffectedRows);
extern CFL_BOOL sdb_stmt_executeMany(SDB_STATEMENTP pStatement, CFL_UINT64 *pulAffectedRows);
extern CFL_BOOL sdb_stmt_getQueryInfo(SDB_STATEMENTP pStatement, CFL_UINT32 pos, SDB_QUERY_COL_INFOP info);
extern SDB_PARAMP sdb_stmt_setString(SDB_STATEMENTP pStatement, const char *name, CFL_STRP str, CFL_BOOL out);
extern SDB_PARAMP sdb_stmt_setStringByPos(SDB_STATEMENTP pStatement, CFL_UINT32 pos, CFL_STRP str, CFL_BOOL out);
extern SDB_PARAMP sdb_stmt_setCharLen(SDB_STATEMENTP pStatement, const char *name, const char *str, CFL_UINT32 strLen, CFL_BOOL out);
extern SDB_PARAMP sdb_stmt_setCharLenByPos(SDB_STATEMENTP pStatement, CFL_UINT32 pos, const char *str, CFL_UINT32 strLen, CFL_BOOL out);
extern SDB_PARAMP sdb_stmt_setChar(SDB_STATEMENTP pStatement, const char *name, const char *str, CFL_BOOL out);
extern SDB_PARAMP sdb_stmt_setCharByPos(SDB_STATEMENTP pStatement, CFL_UINT32 pos, const char *str, CFL_BOOL out);
extern SDB_PARAMP sdb_stmt_setCharNull(SDB_STATEMENTP pStatement, const char *name, CFL_BOOL out);
extern SDB_PARAMP sdb_stmt_setCharNullByPos(SDB_STATEMENTP pStatement, CFL_UINT32 pos, CFL_BOOL out);
extern SDB_PARAMP sdb_stmt_setLobNull(SDB_STATEMENTP pStatement, const char *name, CFL_UINT8 lobType, CFL_BOOL out);
extern SDB_PARAMP sdb_stmt_setLobNullByPos(SDB_STATEMENTP pStatement, CFL_UINT32 pos, CFL_UINT8 lobType, CFL_BOOL out);
extern SDB_LOBP sdb_stmt_getLob(SDB_STATEMENTP pStatement, const char *name);
extern SDB_LOBP sdb_stmt_getLobByPos(SDB_STATEMENTP pStatement, CFL_UINT32 pos);
extern SDB_PARAMP sdb_stmt_setValue(SDB_STATEMENTP pStatement, const char *name, PHB_ITEM pItem, CFL_BOOL bOut);
extern SDB_PARAMP sdb_stmt_setValueByPos(SDB_STATEMENTP pStatement, CFL_UINT32 pos, PHB_ITEM pItem, CFL_BOOL bOut);
extern PHB_ITEM sdb_stmt_getItem(SDB_STATEMENTP pStatement, const char *name);
extern PHB_ITEM sdb_stmt_getItemByPos(SDB_STATEMENTP pStatement, CFL_UINT32 pos);
//extern SDB_PARAMLISTP sdb_stmt_getCreateParams(SDB_STATEMENTP pStmt);

STMT_DECLARE_FUNCTION_SET_GET(Boolean, CFL_BOOL);
STMT_DECLARE_FUNCTION_SET_GETARG(Date, CFL_DATEP);
STMT_DECLARE_FUNCTION_SET_GET(Stmt, SDB_STATEMENTP);
STMT_DECLARE_FUNCTION_SET_GET(Int8, CFL_INT8);
STMT_DECLARE_FUNCTION_SET_GET(UInt8, CFL_UINT8);
STMT_DECLARE_FUNCTION_SET_GET(Int16, CFL_INT16);
STMT_DECLARE_FUNCTION_SET_GET(UInt16, CFL_UINT16);
STMT_DECLARE_FUNCTION_SET_GET(Int32, CFL_INT32);
STMT_DECLARE_FUNCTION_SET_GET(UInt32, CFL_UINT32);
STMT_DECLARE_FUNCTION_SET_GET(Int64, CFL_INT64);
STMT_DECLARE_FUNCTION_SET_GET(UInt64, CFL_UINT64);
STMT_DECLARE_FUNCTION_SET_GET(Float, CFL_FLOAT);
STMT_DECLARE_FUNCTION_SET_GET(Double, CFL_DOUBLE);

#endif

