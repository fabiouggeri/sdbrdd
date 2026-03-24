#include "cfl_hash.h"

#include "sdb_connection.h"
#include "hbapiitm.h"


#include "sdb.h"
#include "sdb_api.h"
#include "sdb_product.h"
#include "sdb_database.h"
#include "sdb_schema.h"
#include "sdb_statement.h"
#include "sdb_transaction.h"
#include "sdb_area.h"
#include "sdb_lock_client.h"
#include "sdb_lob.h"
#include "sdb_table.h"
#include "sdb_field.h"
#include "sdb_util.h"
#include "sdb_log.h"
#include "sdb_error.h"
#include "sdb_thread.h"
#include "sdb_dict.h"

#define sdb_connection_lock(c)   cfl_lock_acquire(&(c)->lock)
#define sdb_connection_unlock(c) cfl_lock_release(&(c)->lock)

SDB_CONNECTIONP sdb_connection_new(SDB_PRODUCTP product, const char *database, const char *username, const char *pswd) {
   SDB_CONNECTIONP conn;

   ENTER_FUN_NAME("sdb_connection_new");
   conn = (SDB_CONNECTIONP) SDB_MEM_ALLOC(sizeof(SDB_CONNECTION));
   cfl_lock_init(&(conn->lock));
   conn->objectType = SDB_OBJ_CONNECTION;
   conn->product = product;
   conn->dbAPI = product->dbAPI;
   conn->database = sdb_product_getCreateDatabase(product, database);
   conn->schema = sdb_database_getCreateSchema(conn->database, username);
   conn->areas = cfl_list_new(50);
   conn->handle = NULL;
   conn->transaction = NULL;
   conn->password = cfl_str_newBuffer(pswd);
   conn->isUseRowId = product->dbAPI->isRowIdSuppported();
   conn->isRollbackOnError = CFL_TRUE;
   conn->queryDefaultPrecision = 21;
   conn->queryDefaultScale = 6;
   conn->isChar1AsLogical = CFL_FALSE;
   conn->isHintsEnable = CFL_TRUE;
   conn->isLogicalParamAsChar1 = CFL_FALSE;
   conn->lockControl = SDB_LOCK_CONTROL_DB;
   conn->lockClient = NULL;
   conn->id = 0;
   conn->defaultPKName = product->pkName != NULL ? cfl_str_newStr(product->pkName) : cfl_str_newBuffer("RECNO");
   conn->pkDefaultExpr = product->pkDefaultExpr != NULL ? cfl_str_newStr(product->pkDefaultExpr) : NULL;
   conn->defaultDelName = product->delName != NULL ? cfl_str_newStr(product->delName) : cfl_str_newBuffer("IS_DELETED");
   RETURN conn;
}

void sdb_connection_free(SDB_CONNECTIONP conn) {

   ENTER_FUN_NAME("sdb_connection_free");

   cfl_list_free(conn->areas);
   if (conn->transaction) {
      sdb_transaction_free(conn->transaction);
   }
   cfl_str_free(conn->password);
   cfl_str_free(conn->defaultPKName);
   cfl_str_free(conn->defaultDelName);
   if (conn->pkDefaultExpr) {
      cfl_str_free(conn->pkDefaultExpr);
   }
   cfl_lock_free(&conn->lock);
   SDB_MEM_FREE(conn);
   RETURN;
}

void sdb_connection_addArea(SDB_CONNECTIONP conn, SDB_AREAP pSDBArea) {
   ENTER_FUN_NAME("sdb_connection_addArea");
   sdb_connection_lock(conn);
   cfl_list_add(conn->areas, pSDBArea);
   sdb_connection_unlock(conn);
   pSDBArea->connection = conn;
   RETURN;
}

void sdb_connection_delArea(SDB_CONNECTIONP conn, SDB_AREAP pSDBArea) {
   CFL_UINT32 len;
   CFL_UINT32 index;

   ENTER_FUN_NAME("sdb_connection_delArea");
   sdb_connection_lock(conn);
   len = cfl_list_length(conn->areas);
   for (index = 0; index < len; index++) {
      SDB_AREAP area = (SDB_AREAP) cfl_list_get(conn->areas, index);
      if (area == pSDBArea) {
         cfl_list_del(conn->areas, index);
         break;
      }
   }
   sdb_connection_unlock(conn);
   RETURN;
}

SDB_STATEMENTP sdb_connection_prepareStatement(SDB_CONNECTIONP conn, CFL_STRP sql) {
   void *handle;

   SDB_LOG_DEBUG(("sdb_connection_prepareStatement: %s", cfl_str_getPtr(sql)));
   handle = conn->dbAPI->prepareStatement(conn, cfl_str_getPtr(sql), cfl_str_getLength(sql));
   if (handle != NULL) {
      SDB_STATEMENTP pStmt = sdb_stmt_new(conn, handle);
      pStmt->type = conn->dbAPI->statementType(handle);
      if (pStmt->type == SDB_STMT_QUERY) {
         pStmt->bufferFetchSize = sdb_api_nextBufferFetchSize(NULL);
         pStmt->fetchSize = pStmt->bufferFetchSize;
      }
      return pStmt;
   }
   return NULL;
}

SDB_STATEMENTP sdb_connection_prepareStatementBuffer(SDB_CONNECTIONP conn, const char *sql) {
   void *handle;

   SDB_LOG_DEBUG(("sdb_connection_prepareStatementBuffer: %s", sql));
   handle = conn->dbAPI->prepareStatement(conn, sql, (CFL_UINT32) strlen(sql));
   if (handle != NULL) {
      SDB_STATEMENTP pStmt = sdb_stmt_new(conn, handle);
      pStmt->type = conn->dbAPI->statementType(handle);
      if (pStmt->type == SDB_STMT_QUERY) {
         pStmt->bufferFetchSize = sdb_api_nextBufferFetchSize(NULL);
         pStmt->fetchSize = pStmt->bufferFetchSize;
      }
      return pStmt;
   }
   return NULL;
}

SDB_STATEMENTP sdb_connection_prepareStatementBufferLen(SDB_CONNECTIONP conn, const char *sql, CFL_UINT32 len) {
   SDB_STATEMENTP pStmt;
   SDB_LOG_DEBUG(("sdb_connection_prepareStatementBufferLen: len=%u sql=%s", len, sql));

   pStmt = sdb_stmt_new(conn, NULL);
   if (! sdb_stmt_prepareBufferLen(pStmt, sql, len)) {
      sdb_stmt_free(pStmt);
      pStmt = NULL;
   }
   return pStmt;
}

CFL_BOOL sdb_connection_executeImmediate(SDB_CONNECTIONP conn, const char *sql, CFL_UINT64 *pulAffectedRows) {
   SDB_STATEMENTP pStmt;
   CFL_BOOL bSuccess = CFL_FALSE;

   ENTER_FUN_NAME("sdb_connection_executeImmediate");
   pStmt = sdb_connection_prepareStatementBuffer(conn, sql);
   if (pStmt) {
      bSuccess = sdb_stmt_execute(pStmt, pulAffectedRows);
      sdb_stmt_free(pStmt);
   }
   RETURN bSuccess;
}

CFL_BOOL sdb_connection_executeImmediateLen(SDB_CONNECTIONP conn, const char *sql, CFL_UINT32 sqlLen, CFL_UINT64 *pulAffectedRows) {
   SDB_STATEMENTP pStmt;
   CFL_BOOL bSuccess = CFL_FALSE;

   ENTER_FUN_NAME("sdb_connection_executeImmediateLen");
   pStmt = sdb_connection_prepareStatementBufferLen(conn, sql, sqlLen);
   if (pStmt) {
      bSuccess = sdb_stmt_execute(pStmt, pulAffectedRows);
      sdb_stmt_free(pStmt);
   }
   RETURN bSuccess;
}

CFL_BOOL sdb_connection_beginTransaction(SDB_CONNECTIONP conn, CFL_INT32 formatId, CFL_STRP globalId, CFL_STRP branchId) {
   void *handle;

   ENTER_FUN_NAME("sdb_connection_beginTransaction");
   if (conn->transaction == NULL) {
      handle = conn->dbAPI->beginTransaction(conn, formatId, globalId, branchId);
      if (handle) {
         conn->transaction = sdb_transaction_new(handle, formatId, globalId, branchId);
      }
   } else {
      handle = NULL;
      sdb_thread_setError(SDB_ERROR_TYPE_SDB, SDB_ERROR_TRANSACTION, "Already in transaction");
   }
   RETURN handle != NULL;
}

CFL_BOOL sdb_connection_prepareTransaction(SDB_CONNECTIONP conn) {
   CFL_BOOL bSuccess;

   ENTER_FUN_NAME("sdb_connection_prepareTransaction");
   if (conn->transaction) {
      bSuccess = conn->dbAPI->prepareTransaction(conn);
   } else {
      sdb_thread_setError(SDB_ERROR_TYPE_SDB, SDB_ERROR_TRANSACTION, "No active transaction");
      bSuccess = CFL_FALSE;
   }
   RETURN bSuccess;
}

CFL_BOOL sdb_connection_rollback(SDB_CONNECTIONP conn, CFL_BOOL lForce) {
   CFL_BOOL bSuccess;

   ENTER_FUN_NAME("sdb_connection_rollback");
   if (conn->transaction) {
      conn->dbAPI->rollbackTransaction(conn);
      sdb_transaction_free(conn->transaction);
      conn->transaction = NULL;
      bSuccess = CFL_TRUE;
   } else if (lForce){
      conn->dbAPI->rollbackTransaction(conn);
      bSuccess = CFL_TRUE;
   } else {
      sdb_thread_setError(SDB_ERROR_TYPE_SDB, SDB_ERROR_TRANSACTION, "No active transaction");
      bSuccess = CFL_FALSE;
   }
   RETURN bSuccess;
}

CFL_BOOL sdb_connection_commit(SDB_CONNECTIONP conn, CFL_BOOL lForce) {
   CFL_BOOL bSuccess;

   ENTER_FUN_NAME("sdb_connection_commit");
   if (conn->transaction) {
      conn->dbAPI->commitTransaction(conn);
      sdb_transaction_free(conn->transaction);
      conn->transaction = NULL;
      bSuccess = CFL_TRUE;
   } else if (lForce) {
      conn->dbAPI->commitTransaction(conn);
      bSuccess = CFL_TRUE;
   } else {
      sdb_thread_setError(SDB_ERROR_TYPE_SDB, SDB_ERROR_TRANSACTION, "Transaction not open");
      bSuccess = CFL_FALSE;
   }
   RETURN bSuccess;
}

CFL_BOOL sdb_connection_isOpen(SDB_CONNECTIONP conn) {
   return conn->dbAPI->isConnected(conn);
}

CFL_BOOL sdb_connection_executeProcedure(SDB_CONNECTIONP conn, const char *procName, SDB_PARAMLISTP params, CFL_BOOL bImplicitArgs) {
   return conn->dbAPI->executeStoredProcedure(conn, procName, params, bImplicitArgs);
}

PHB_ITEM sdb_connection_executeFunction(SDB_CONNECTIONP conn, const char *funcName, CFL_UINT8 resultType, SDB_PARAMLISTP params, CFL_BOOL bImplicitArgs) {
   return conn->dbAPI->executeStoredFunction(conn, funcName, resultType, params, bImplicitArgs);
}

SDB_LOBP sdb_connection_createLob(SDB_CONNECTIONP conn, CFL_UINT8 lobType) {
   SDB_LOBP pLob = NULL;
   SDB_LOG_DEBUG(("sdb_connection_createLob: type=%u", lobType));
   if (lobType == SDB_CLP_BLOB || lobType == SDB_CLP_CLOB) {
      void *handle;
      handle = conn->dbAPI->createLob(conn, lobType);
      if (handle) {
         pLob = sdb_lob_new(conn, lobType, handle);
      }
   } else {
      sdb_thread_setError(SDB_ERROR_TYPE_SDB, SDB_ERROR_INVALID_ARGUMENT, "Invalid LOB type.");
   }
   return pLob;
}

PHB_ITEM sdb_connection_getServerVersion(SDB_CONNECTIONP conn) {
   PHB_ITEM pVersion;
   CFL_STRP strVer = cfl_str_new(40);
   if (conn->dbAPI->serverVersion(conn, strVer)) {
      pVersion = hb_itemPutCL(NULL, cfl_str_getPtr(strVer), cfl_str_getLength(strVer));
   } else {
      pVersion = NULL;
   }
   cfl_str_free(strVer);
   return pVersion;
}

PHB_ITEM sdb_connection_getClientVersion(SDB_CONNECTIONP conn) {
   PHB_ITEM pVersion;
   CFL_STRP strVer = cfl_str_new(40);
   if (conn->dbAPI->clientVersion(conn, strVer)) {
      pVersion = hb_itemPutCL(NULL, cfl_str_getPtr(strVer), cfl_str_getLength(strVer));
   } else {
      pVersion = NULL;
   }
   cfl_str_free(strVer);
   return pVersion;
}

SDB_SCHEMAP sdb_connection_getCurrentSchema(SDB_CONNECTIONP conn) {
   return conn->schema;
}

CFL_BOOL sdb_connection_setCurrentSchema(SDB_CONNECTIONP conn, const char *schemaName) {
   CFL_BOOL bSuccess = CFL_TRUE;
   CFL_STRP strSchema = cfl_str_newBuffer(schemaName);
   sdb_connection_lock(conn);
   conn->schema = sdb_database_getCreateSchema(conn->database, schemaName);
   sdb_connection_unlock(conn);
   if (! conn->dbAPI->setCurrentSchema(conn, strSchema)) {
      bSuccess = CFL_FALSE;
   }
   cfl_str_free(strSchema);
   return bSuccess;
}

CFL_UINT32 sdb_connection_getStmtCacheSize(SDB_CONNECTIONP conn) {
   return conn->dbAPI->getCacheStatementSize(conn);
}

CFL_BOOL sdb_connection_setStmtCacheSize(SDB_CONNECTIONP conn, CFL_UINT32 cacheSize) {
   return conn->dbAPI->setCacheStatementSize(conn, cacheSize);
}

CFL_BOOL sdb_connection_breakOperation(SDB_CONNECTIONP conn) {
   return conn->dbAPI->breakOperation(conn);
}

CFL_BOOL sdb_connection_createObjects(SDB_CONNECTIONP conn) {
   SDB_DICTIONARYP dict;
   CFL_BOOL bSuccess = CFL_TRUE;

   ENTER_FUN_NAME("sdb_connection_createObjects");
   dict = sdb_schema_getDict(conn->schema, conn);
   if (dict == NULL) {
      dict = sdb_dict_getDefault();
      if (! dict->existsDict(conn, conn->schema)) {
         bSuccess = dict->createObjects(conn);
      }
   }

   RETURN bSuccess;
}

SDB_CONNECTIONP sdb_connection_current(void) {
   return sdb_thread_getData()->connection;
}

void sdb_connection_moveAreas(SDB_CONNECTIONP conn, CFL_LISTP areas) {
   CFL_UINT32 len;
   CFL_UINT32 i;
   sdb_connection_lock(conn);
   len = cfl_list_length(conn->areas);
   for (i = 0; i < len; i++) {
      cfl_list_add(areas, cfl_list_get(conn->areas, i));
   }
   cfl_list_clear(conn->areas);
   sdb_connection_unlock(conn);
}

CFL_UINT32 sdb_connection_areasCount(SDB_CONNECTIONP conn) {
   return cfl_list_length(conn->areas);
}
