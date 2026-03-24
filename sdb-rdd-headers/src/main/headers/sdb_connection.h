#ifndef SDB_CONNECTION_H_

#define SDB_CONNECTION_H_

#include "hbapi.h"

#include "cfl_types.h"
#include "cfl_lock.h"
#include "cfl_str.h"
#include "cfl_list.h"

#include "sdb_defs.h"

#define SDB_LOCK_CONTROL_NONE   0
#define SDB_LOCK_CONTROL_DB     10
#define SDB_LOCK_AUTO_GET       20
#define SDB_LOCK_AUTO_PUT       30
#define SDB_LOCK_CONTROL_SERVER 40

struct _SDB_CONNECTION {
   CFL_UINT8        objectType;
   void             *handle;
   SDB_DB_APIP      dbAPI;
   CFL_STRP         password;
   SDB_PRODUCTP     product;
   SDB_DATABASEP    database;
   SDB_SCHEMAP      schema;
   CFL_LISTP        areas;
   CFL_UINT32       id;
   SDB_TRANSACTIONP transaction;
   CFL_LOCK         lock;
   CFL_UINT8        queryDefaultPrecision;
   CFL_UINT8        queryDefaultScale;
   CFL_UINT8        lockControl;
   SDB_LS_CLIENTP   lockClient;
   CFL_STRP         defaultPKName;
   CFL_STRP         pkDefaultExpr;
   CFL_STRP         defaultDelName;
   CFL_BOOL         isUseRowId BIT_FIELD;
   CFL_BOOL         isRollbackOnError BIT_FIELD;
   CFL_BOOL         isChar1AsLogical BIT_FIELD;
   CFL_BOOL         isHintsEnable BIT_FIELD;
   CFL_BOOL         isLogicalParamAsChar1 BIT_FIELD;
};

extern SDB_CONNECTIONP sdb_connection_new(SDB_PRODUCTP product, const char *database, const char *username, const char *pswd);
extern void sdb_connection_free(SDB_CONNECTIONP conn);
extern void sdb_connection_addArea(SDB_CONNECTIONP conn, SDB_AREAP pSDBArea);
extern void sdb_connection_delArea(SDB_CONNECTIONP conn, SDB_AREAP pSDBArea);
extern CFL_BOOL sdb_connection_beginTransaction(SDB_CONNECTIONP conn, CFL_INT32 formatId, CFL_STRP globalId, CFL_STRP branchId);
extern CFL_BOOL sdb_connection_prepareTransaction(SDB_CONNECTIONP conn);
extern CFL_BOOL sdb_connection_rollback(SDB_CONNECTIONP conn, CFL_BOOL lForce);
extern CFL_BOOL sdb_connection_commit(SDB_CONNECTIONP conn, CFL_BOOL lForce);
extern CFL_BOOL sdb_connection_isOpen(SDB_CONNECTIONP conn);
extern CFL_BOOL sdb_connection_executeProcedure(SDB_CONNECTIONP conn, const char *procName, SDB_PARAMLISTP params, CFL_BOOL bImplicitArgs);
extern PHB_ITEM sdb_connection_executeFunction(SDB_CONNECTIONP conn, const char *funcName, CFL_UINT8 resultType, SDB_PARAMLISTP params, CFL_BOOL bImplicitArgs);
extern SDB_LOBP sdb_connection_createLob(SDB_CONNECTIONP conn, CFL_UINT8 lobType);
extern PHB_ITEM sdb_connection_getServerVersion(SDB_CONNECTIONP conn);
extern PHB_ITEM sdb_connection_getClientVersion(SDB_CONNECTIONP conn);
extern SDB_SCHEMAP sdb_connection_getCurrentSchema(SDB_CONNECTIONP conn);
extern CFL_BOOL sdb_connection_setCurrentSchema(SDB_CONNECTIONP conn, const char *schema);
extern CFL_UINT32 sdb_connection_getStmtCacheSize(SDB_CONNECTIONP conn);
extern CFL_BOOL sdb_connection_setStmtCacheSize(SDB_CONNECTIONP conn, CFL_UINT32 cacheSize);
extern CFL_BOOL sdb_connection_breakOperation(SDB_CONNECTIONP conn);
extern SDB_STATEMENTP sdb_connection_prepareStatement(SDB_CONNECTIONP conn, CFL_STRP sql);
extern SDB_STATEMENTP sdb_connection_prepareStatementBuffer(SDB_CONNECTIONP conn, const char *sql);
extern SDB_STATEMENTP sdb_connection_prepareStatementBufferLen(SDB_CONNECTIONP conn, const char *sql, CFL_UINT32 len);
extern CFL_BOOL sdb_connection_executeImmediate(SDB_CONNECTIONP conn, const char *sql, CFL_UINT64 *pulAffectedRows);
extern CFL_BOOL sdb_connection_executeImmediateLen(SDB_CONNECTIONP conn, const char *sql, CFL_UINT32 sqlLen, CFL_UINT64 *pulAffectedRows);
extern CFL_BOOL sdb_connection_createObjects(SDB_CONNECTIONP conn);
extern SDB_CONNECTIONP sdb_connection_current(void);
extern void sdb_connection_moveAreas(SDB_CONNECTIONP conn, CFL_LISTP areas);
extern CFL_UINT32 sdb_connection_areasCount(SDB_CONNECTIONP conn);

#endif
