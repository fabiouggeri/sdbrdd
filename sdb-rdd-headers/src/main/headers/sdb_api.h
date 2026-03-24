#ifndef SDB_API_H_

#define SDB_API_H_

#include "hbapi.h"

#include "cfl_sql.h"
#include "cfl_str.h"
#include "cfl_types.h"


#include "sdb_defs.h"

#define DBI_SDB_ISRECNO64 32120  /* does the table use 64-bit RECNO? */
#define DBI_SDB_RECCOUNT64 32121 /* get record count as 64-bit number */
#define DBI_SDB_DBRUNLOCK64                                                    \
  32122 /* unlock selectively record specified as 64-bit number                \
           (DBRUNLOCK(rec64)) */

#define SDB_LOCK_NONE 0
#define SDB_LOCK_SHARED 1
#define SDB_LOCK_LOCK_ALL 2
#define SDB_LOCK_LOCK_REC 3
#define SDB_LOCK_EXCLUSIVE 4

/* DATABASE API */
typedef CFL_BOOL (*SDB_DB_FNC_INIT)(SDB_PRODUCTP product);
typedef void (*SDB_DB_FNC_FINALIZE)(SDB_PRODUCTP product);
typedef CFL_BOOL (*SDB_DB_FNC_SUP_ROWID)(void);
typedef CFL_BOOL (*SDB_DB_FNC_CONNECT)(SDB_CONNECTIONP conn,
                                       const char *database,
                                       const char *username, const char *pswd);
typedef void (*SDB_DB_FNC_DISCONNECT)(SDB_CONNECTIONP conn);
typedef void *(*SDB_DB_FNC_BEG_TRANS)(SDB_CONNECTIONP conn, CFL_INT32 formatId,
                                      CFL_STRP globalId, CFL_STRP branchId);
typedef CFL_BOOL (*SDB_DB_FNC_PREP_TRANS)(SDB_CONNECTIONP conn);
typedef void (*SDB_DB_FNC_COMM_TRANS)(SDB_CONNECTIONP conn);
typedef void (*SDB_DB_FNC_ROLL_TRANS)(SDB_CONNECTIONP conn);
typedef CFL_BOOL (*SDB_DB_FNC_LCK_TAB)(SDB_AREAP pSDBArea, int lockMode);
typedef CFL_BOOL (*SDB_DB_FNC_ULCK_TAB)(SDB_AREAP pSDBArea);
typedef CFL_BOOL (*SDB_DB_FNC_EXEC_STMT)(SDB_STATEMENTP stmt, CFL_UINT64 *ul1);
typedef CFL_BOOL (*SDB_DB_FNC_EXEC_MANY_STMT)(SDB_STATEMENTP stmt,
                                              CFL_UINT64 *ul1);
typedef CFL_BOOL (*SDB_DB_FNC_GET_FLD_VAL)(SDB_STATEMENTP stmt, CFL_UINT16 pos,
                                           SDB_FIELDP pField,
                                           SDB_RECORDP pRecord);
typedef CFL_BOOL (*SDB_DB_FNC_GET_QRY_VAL)(SDB_STATEMENTP stmt, CFL_UINT16 pos,
                                           PHB_ITEM item);
typedef CFL_BOOL (*SDB_DB_FNC_GET_MEMO_VAL)(SDB_STATEMENTP stmt, CFL_UINT16 pos,
                                            SDB_TABLEP table, SDB_FIELDP field,
                                            SDB_RECORDP pRecord,
                                            SDB_RECNO ulRecno);
typedef CFL_BOOL (*SDB_DB_FNC_LCK_REC)(SDB_AREAP pSDBArea, PHB_ITEM pRecno,
                                       PHB_ITEM pRowId);
typedef CFL_BOOL (*SDB_DB_FNC_ULCK_REC)(SDB_AREAP pSDBArea, PHB_ITEM pRecno,
                                        PHB_ITEM pRowId);
typedef CFL_BOOL (*SDB_DB_FNC_TAB_DROP_IDX)(SDB_CONNECTIONP conn,
                                            SDB_TABLEP table, SDB_INDEXP index,
                                            CFL_BOOL b);
typedef CFL_BOOL (*SDB_DB_FNC_DROP_TAB)(SDB_CONNECTIONP conn, SDB_TABLEP table);
typedef CFL_BOOL (*SDB_DB_FNC_CREATE_TAB)(SDB_CONNECTIONP conn, SDB_SCHEMAP sch,
                                          SDB_TABLEP table);
typedef CFL_BOOL (*SDB_DB_FNC_ADD_COL)(SDB_CONNECTIONP conn, SDB_TABLEP table,
                                       SDB_FIELDP field);
typedef CFL_BOOL (*SDB_DB_FNC_MOD_COL)(SDB_CONNECTIONP conn, SDB_TABLEP table,
                                       SDB_FIELDP field);
typedef CFL_BOOL (*SDB_DB_FNC_CREATE_IDX)(SDB_CONNECTIONP conn,
                                          SDB_TABLEP table, SDB_INDEXP index);
typedef CFL_BOOL (*SDB_DB_FNC_EXEC_PROC)(SDB_CONNECTIONP connection,
                                         const char *funcName,
                                         SDB_PARAMLISTP params,
                                         CFL_BOOL bImplicitArgs);
typedef void (*SDB_DB_FNC_CLOSE_STMT)(void *handle);
typedef CFL_BOOL (*SDB_DB_FNC_SET_PRE_FETCH_SIZE)(SDB_STATEMENTP stmt,
                                                  CFL_UINT16 fetchSize);
typedef CFL_BOOL (*SDB_DB_FNC_FETCH)(SDB_STATEMENTP stmt);
typedef CFL_BOOL (*SDB_DB_FNC_IS_CONN)(SDB_CONNECTIONP conn);
typedef CFL_BOOL (*SDB_DB_FNC_TAB_REN)(SDB_CONNECTIONP conn, SDB_TABLEP table,
                                       const char *str1, const char *str2);
typedef void *(*SDB_DB_FNC_PREP_STMT)(SDB_CONNECTIONP connection,
                                      const char *sql, CFL_UINT32 sqlLen);
typedef CFL_UINT8 (*SDB_DB_FNC_STMT_TYPE)(void *stmtHandle);
typedef CFL_BOOL (*SDB_DB_FNC_IS_ERR_TYPE)(SDB_ERRORP pError, int code);
typedef PHB_ITEM (*SDB_DB_FNC_EXEC_FUN)(SDB_CONNECTIONP connection,
                                        const char *funcName,
                                        CFL_UINT8 resultType,
                                        SDB_PARAMLISTP params,
                                        CFL_BOOL bImplicitArgs);
typedef CFL_BOOL (*SDB_DB_FNC_QRY_INFO)(SDB_STATEMENTP pStatement,
                                        CFL_UINT16 pos,
                                        SDB_QUERY_COL_INFOP info);
typedef PHB_ITEM (*SDB_DB_FNC_CLP2SQL)(SDB_AREAP pSDBArea,
                                       SDB_EXPRESSION_NODEP expr,
                                       CFL_UINT8 exprType);
typedef void *(*SDB_DB_FNC_CREATE_LOB)(SDB_CONNECTIONP connection,
                                       CFL_UINT8 lobType);
typedef CFL_BOOL (*SDB_DB_FNC_FREE_LOB)(SDB_LOBP pLob);
typedef CFL_BOOL (*SDB_DB_FNC_OPEN_LOB)(SDB_LOBP pLob);
typedef CFL_BOOL (*SDB_DB_FNC_CLOSE_LOB)(SDB_LOBP pLob);
typedef CFL_BOOL (*SDB_DB_FNC_READ_LOB)(SDB_LOBP pLob, const char *buffer,
                                        CFL_UINT64 offset, CFL_UINT64 *amount);
typedef CFL_BOOL (*SDB_DB_FNC_WRITE_LOB)(SDB_LOBP pLob, const char *buffer,
                                         CFL_UINT64 offset, CFL_UINT64 amount);
typedef CFL_BOOL (*SDB_DB_FNC_SERVER_VERSION)(SDB_CONNECTIONP connection,
                                              CFL_STRP version);
typedef CFL_BOOL (*SDB_DB_FNC_CLIENT_VERSION)(SDB_CONNECTIONP connection,
                                              CFL_STRP version);
typedef CFL_BOOL (*SDB_DB_FNC_GET_SCHEMA)(SDB_CONNECTIONP connection,
                                          CFL_STRP schema);
typedef CFL_BOOL (*SDB_DB_FNC_SET_SCHEMA)(SDB_CONNECTIONP connection,
                                          CFL_STRP schema);
typedef CFL_UINT32 (*SDB_DB_FNC_GET_STMT_CACHE)(SDB_CONNECTIONP connection);
typedef CFL_BOOL (*SDB_DB_FNC_SET_STMT_CACHE)(SDB_CONNECTIONP connection,
                                              CFL_UINT32 cacheSize);
typedef CFL_BOOL (*SDB_DB_FNC_BREAK_OPERATION)(SDB_CONNECTIONP connection);
typedef CFL_SQL_BUILDERP (*SDB_DB_FNC_SQL_BUILDER)(void);

struct _SDB_DB_API {
  SDB_DB_FNC_INIT initialize;
  SDB_DB_FNC_FINALIZE finalize;
  SDB_DB_FNC_CONNECT connect;
  SDB_DB_FNC_DISCONNECT disconnect;
  SDB_DB_FNC_LCK_TAB lockTable;
  SDB_DB_FNC_ULCK_TAB unlockTable;
  SDB_DB_FNC_SET_PRE_FETCH_SIZE setPreFetchSize;
  SDB_DB_FNC_FETCH fetchNext;
  SDB_DB_FNC_GET_FLD_VAL getFieldValue;
  SDB_DB_FNC_GET_QRY_VAL getQueryValue;
  SDB_DB_FNC_GET_MEMO_VAL getMemoValue;
  SDB_DB_FNC_TAB_DROP_IDX tableDropIndex;
  SDB_DB_FNC_LCK_REC lockRecord;
  SDB_DB_FNC_ULCK_REC unlockRecord;
  SDB_DB_FNC_DROP_TAB dropTable;
  SDB_DB_FNC_CREATE_TAB createTable;
  SDB_DB_FNC_ADD_COL tableAddColumn;
  SDB_DB_FNC_MOD_COL tableModifyColumn;
  SDB_DB_FNC_CREATE_IDX createIndex;
  SDB_DB_FNC_BEG_TRANS beginTransaction;
  SDB_DB_FNC_PREP_TRANS prepareTransaction;
  SDB_DB_FNC_COMM_TRANS commitTransaction;
  SDB_DB_FNC_ROLL_TRANS rollbackTransaction;
  SDB_DB_FNC_EXEC_PROC executeStoredProcedure;
  SDB_DB_FNC_EXEC_STMT executeStatement;
  SDB_DB_FNC_EXEC_MANY_STMT executeStatementMany;
  SDB_DB_FNC_CLOSE_STMT closeStatement;
  SDB_DB_FNC_IS_CONN isConnected;
  SDB_DB_FNC_TAB_REN tableRename;
  SDB_DB_FNC_PREP_STMT prepareStatement;
  SDB_DB_FNC_STMT_TYPE statementType;
  SDB_DB_FNC_SUP_ROWID isRowIdSuppported;
  SDB_DB_FNC_IS_ERR_TYPE isErrorType;
  SDB_DB_FNC_EXEC_FUN executeStoredFunction;
  SDB_DB_FNC_QRY_INFO getQueryInfo;
  SDB_DB_FNC_CLP2SQL clipperToSqlExpression;
  SDB_DB_FNC_CREATE_LOB createLob;
  SDB_DB_FNC_FREE_LOB releaseLob;
  SDB_DB_FNC_OPEN_LOB openLob;
  SDB_DB_FNC_CLOSE_LOB closeLob;
  SDB_DB_FNC_READ_LOB readLob;
  SDB_DB_FNC_WRITE_LOB writeLob;
  SDB_DB_FNC_SERVER_VERSION serverVersion;
  SDB_DB_FNC_GET_SCHEMA getCurrentSchema;
  SDB_DB_FNC_SET_SCHEMA setCurrentSchema;
  SDB_DB_FNC_GET_STMT_CACHE getCacheStatementSize;
  SDB_DB_FNC_SET_STMT_CACHE setCacheStatementSize;
  SDB_DB_FNC_BREAK_OPERATION breakOperation;
  SDB_DB_FNC_CLIENT_VERSION clientVersion;
  SDB_DB_FNC_SQL_BUILDER sqlBuilder;
};

extern CFL_INT16 sdb_api_getRddId(void);
extern CFL_BOOL sdb_api_initializeInternalData(void);
extern CFL_BOOL sdb_api_registerProductAPI(const char *product,
                                           const char *displayName,
                                           SDB_DB_APIP dbAPI);
extern void sdb_api_finalize(void);
extern SDB_PRODUCTP sdb_api_getProduct(const char *product);
extern SDB_CONNECTIONP sdb_api_connect(SDB_PRODUCTP product,
                                       const char *database,
                                       const char *username, const char *pswd,
                                       CFL_BOOL registerConn);
extern void sdb_api_disconnect(SDB_CONNECTIONP connection);
extern SDB_TABLEP sdb_api_getTable(SDB_CONNECTIONP connection,
                                   const char *schema, const char *tableName);
extern CFL_BOOL sdb_api_lockAreaTable(SDB_AREAP pSDBArea, int lockMode);
extern CFL_BOOL sdb_api_unlockAreaTable(SDB_AREAP pSDBArea);
extern CFL_BOOL sdb_api_areaFetchNext(SDB_AREAP pSDBArea, CFL_BOOL *fError);
extern CFL_BOOL sdb_api_areaGetValue(SDB_AREAP pSDBArea, CFL_UINT16 pos,
                                     SDB_FIELDP field, CFL_BOOL bReadLarge);
extern CFL_BOOL sdb_api_insertRecord(SDB_AREAP pSDBArea);
extern CFL_BOOL sdb_api_updateRecord(SDB_AREAP pSDBArea);
extern SDB_RECNO sdb_api_tableMaxRecno(SDB_AREAP pSDBArea);
extern PHB_ITEM sdb_api_queryFieldValue(SDB_AREAP pSDBArea, SDB_FIELDP pField,
                                        PHB_ITEM pRecId, PHB_ITEM pFieldValue);
extern CFL_BOOL sdb_api_tableDropIndex(SDB_CONNECTIONP connection,
                                       SDB_TABLEP table, SDB_INDEXP index,
                                       CFL_BOOL physically);
extern CFL_BOOL sdb_api_tableUnlockRecord(SDB_AREAP pSDBArea, PHB_ITEM pRecno,
                                          PHB_ITEM pRowId);
extern CFL_BOOL sdb_api_dropIndex(SDB_CONNECTIONP connection,
                                  const char *szSchema, const char *szTableName,
                                  const char *szIndexName, CFL_BOOL physically);
extern CFL_BOOL sdb_api_dropTable(SDB_CONNECTIONP connection,
                                  const char *szSchema,
                                  const char *szTableName);
extern CFL_BOOL sdb_api_existsIndex(SDB_CONNECTIONP connection,
                                    const char *szSchema,
                                    const char *szTableName,
                                    const char *szIndexName);
extern CFL_BOOL sdb_api_existsTable(SDB_CONNECTIONP connection,
                                    const char *szSchema,
                                    const char *szTableName);
extern CFL_BOOL sdb_api_tableAddColumn(SDB_CONNECTIONP conn, SDB_TABLEP table,
                                       SDB_FIELDP field);
extern CFL_BOOL sdb_api_tableModifyColumn(SDB_CONNECTIONP conn,
                                          SDB_TABLEP table, SDB_FIELDP field);
extern CFL_BOOL sdb_api_createTable(SDB_CONNECTIONP conn, const char *schema,
                                    SDB_TABLEP table);
extern SDB_INDEXP sdb_api_createIndex(SDB_CONNECTIONP connection,
                                      SDB_TABLEP table, const char *szIndexName,
                                      const char *szClipperExpr, PHB_ITEM value,
                                      const char *sqlDefaultExpr,
                                      CFL_BOOL isVirtual, const char *hint);
extern CFL_UINT8 sdb_api_getDefaultMemoType(void);
extern void sdb_api_setDefaultMemoType(CFL_UINT8 memoType);
extern SDB_CONNECTIONP sdb_api_defaultConnection(void);
extern SDB_TABLEP sdb_api_openQueryStmt(SDB_AREAP pSDBArea,
                                        SDB_STATEMENTP pStatement,
                                        CFL_BOOL bRightPadded);
extern SDB_CONNECTIONP sdb_api_getConnection(CFL_UINT32 idConnection);
extern void sdb_api_registerArea(SDB_AREAP pSDBArea);
extern void sdb_api_deregisterArea(SDB_AREAP pSDBArea);
extern void sdb_api_startCloseStatementThread(void);
extern SDB_INDEXP sdb_api_getIndex(SDB_CONNECTIONP connection,
                                   const char *schema, const char *tableName,
                                   const char *indexName);
extern CFL_BOOL sdb_api_tableRename(SDB_CONNECTIONP connection,
                                    const char *schemaName,
                                    const char *tableName,
                                    const char *newClpName,
                                    const char *newDBName);
extern CFL_BOOL sdb_api_tableGoTop(SDB_AREAP pSDBArea);
extern CFL_BOOL sdb_api_tableGoBottom(SDB_AREAP pSDBArea);
extern CFL_BOOL sdb_api_tableGoTo(SDB_AREAP pSDBArea, PHB_ITEM pRecId);
extern CFL_BOOL sdb_api_tableLockRecord(SDB_AREAP pSDBArea, PHB_ITEM pRecno,
                                        PHB_ITEM pRowId);
extern CFL_BOOL sdb_api_tableSeek(SDB_AREAP pSDBArea, PHB_ITEM pSearchValue,
                                  CFL_UINT8 uiSort, CFL_BOOL bSoft);
extern CFL_BOOL sdb_api_tableQuery(SDB_AREAP pSDBArea, PHB_ITEM pQuery,
                                   SDB_PARAMLISTP params);
extern CFL_BOOL sdb_api_tableNextRecord(SDB_AREAP pSDBArea, PHB_ITEM pRecId,
                                        PHB_ITEM pKey, CFL_UINT8 uiSort);
extern CFL_BOOL sdb_api_tableNextKey(SDB_AREAP pSDBArea, PHB_ITEM pKey,
                                     CFL_UINT8 uiSort);
extern CFL_BOOL sdb_api_tableFromRecno(SDB_AREAP pSDBArea, HB_BYTE uiSort,
                                       CFL_BOOL bNextRecno);
extern CFL_BOOL sdb_api_isTrimParams(void);
extern void sdb_api_setTrimParams(CFL_BOOL bTrimParams);
extern CFL_BOOL sdb_api_isPadFields(void);
extern void sdb_api_setPadFields(CFL_BOOL bPadFields);
extern CFL_BOOL sdb_api_isNullable(void);
extern void sdb_api_setNullable(CFL_BOOL bNullable);
extern CFL_BOOL sdb_api_deleteRecords(SDB_AREAP pSDBArea, CFL_BOOL bDeletedAll);
extern void sdb_api_setIntervalCloseCursor(CFL_UINT32 newInterval);
extern CFL_UINT32 sdb_api_getIntervalCloseCursor(void);
extern void sdb_api_setDefaultBufferFetchSize(CFL_UINT16 newSize);
extern CFL_UINT16 sdb_api_getDefaultBufferFetchSize(void);
extern void sdb_api_setNextBufferFetchSize(CFL_UINT16 newSize);
extern CFL_UINT16 sdb_api_getNextBufferFetchSize(void);
extern CFL_BOOL sdb_api_isErrorType(SDB_CONNECTIONP connection,
                                    SDB_ERRORP error, CFL_INT32 errorCode);
extern PHB_ITEM sdb_api_clipperToSql(SDB_AREAP pSDBArea, const char *str,
                                     CFL_UINT32 strLen, CFL_UINT8 exprType);
extern CFL_UINT16 sdb_api_nextBufferFetchSize(SDB_TABLEP table);
extern void sdb_api_lockApi(void);
extern void sdb_api_unlockApi(void);
extern char *sdb_api_getRddName(void);
extern void sdb_api_setRddName(char *rddName);
extern HB_ERRCODE sdb_api_genError(SDB_CONNECTIONP connection,
                                   CFL_UINT16 uiGenCode, CFL_UINT16 uiSubCode,
                                   const char *filename, CFL_UINT16 uiFlags,
                                   const char *description,
                                   const char *operation, PHB_ITEM *pErrorPtr);
extern HB_ERRCODE
sdb_api_genErrorFromSDBError(SDB_CONNECTIONP connection, const char *filename,
                             CFL_UINT16 uiFlags, SDB_ERRORP pSDBError,
                             const char *operation, PHB_ITEM *pErrorPtr);
extern void sdb_clp_functionParamsToParamsList(int iStart,
                                               SDB_PARAMLISTP params,
                                               CFL_BOOL bPositional,
                                               CFL_BOOL bTrim,
                                               CFL_BOOL bNullable);
extern void sdb_clp_hashToParams(PHB_ITEM pHash, SDB_PARAMLISTP params,
                                 CFL_BOOL bTrim, CFL_BOOL bNullable);

#endif
