#include <time.h>

#include "error.ch"
#include "hbapierr.h"
#include "hbapiitm.h"
#include "hbapirdd.h"
#include "hbdbferr.h"
#include "hbset.h"
#include "hbvm.h"


#include "cfl_hash.h"
#include "cfl_list.h"

#include "sdb_api.h"
#include "sdb_area.h"
#include "sdb_connection.h"
#include "sdb_database.h"
#include "sdb_dict.h"
#include "sdb_error.h"
#include "sdb_expr.h"
#include "sdb_field.h"
#include "sdb_index.h"
#include "sdb_info.h"
#include "sdb_lob.h"
#include "sdb_lock_client.h"
#include "sdb_log.h"
#include "sdb_param.h"
#include "sdb_product.h"
#include "sdb_record.h"
#include "sdb_schema.h"
#include "sdb_statement.h"
#include "sdb_table.h"
#include "sdb_thread.h"
#include "sdb_transaction.h"
#include "sdb_util.h"


#define SDB_RECNO_REFRESH_ATTEMPS 5
#define SDB_MAX_ATTEMPS_APPEND 40

#define TABLE_FETCH_SIZE(t) sdb_api_nextBufferFetchSize(t)

#define IS_QUERY_MORE(a)                                                       \
  ((a)->pCurrentCommand != NULL &&                                             \
   ((a)->pCurrentCommand->uiQueryMore == QRY_MORE_YES ||                       \
    ((a)->pCurrentCommand->uiQueryMore == QRY_MORE_IF_FOUND &&                 \
     (a)->pStatement->fetchCount > 0)))

#define LOCK_AREA(a)   // sdb_area_lock(a)
#define UNLOCK_AREA(a) // sdb_area_unlock(a)

#define LOCK_API() cfl_lock_acquire(&s_apiLock)
#define UNLOCK_API() cfl_lock_release(&s_apiLock)

#define IS_RETRY_INSERT(a, p, c)                                               \
  ((p != NULL || (a)->table->pkField->dbExpression != NULL) &&                 \
   sdb_thread_getLastError()->type == SDB_ERROR_TYPE_DB_ERROR &&               \
   sdb_api_isErrorType((a)->connection, sdb_thread_getLastError(),             \
                       SDB_DB_ERROR_UNIQUE_VIOLATION) &&                       \
   (c)++ < SDB_MAX_ATTEMPS_APPEND)

HB_ERRCODE sdb_rdd_goBottom(SDB_AREAP pSDBArea, CFL_BOOL bFetch);

static char s_rddName[HB_RDD_MAX_DRIVERNAME_LEN + 1] = _SDB_RDDNAME_;
// static CFL_INT16 s_rddID = -1;
static CFL_BOOL s_dataInitialized = CFL_FALSE;
static CFL_LOCK s_apiLock;
static CFL_LISTP s_products = NULL;
static CFL_LISTP s_connections = NULL;
static CFL_UINT32 s_nextConnectionId = 1;
static CFL_UINT32 s_intervalStmtVerification = 30;

static char s_IndexColPrefix[30] = "IE$";
static CFL_UINT8 s_defaultMemoType = SDB_CLP_BLOB;
static CFL_BOOL s_bTrimParams = CFL_FALSE;
static CFL_BOOL s_bPadFields = CFL_FALSE;
static CFL_BOOL s_bNullable = CFL_FALSE;
static CFL_UINT16 s_DefaultBufferFetchSize = SDB_STMT_FETCH_SIZE;

static void releaseConnection(SDB_CONNECTIONP connection) {

  ENTER_FUN_NAME("releaseConnection");
  if (connection) {
    int iCurrArea;
    CFL_UINT32 i;
    CFL_LISTP areas = cfl_list_new(sdb_connection_areasCount(connection));
    sdb_connection_moveAreas(connection, areas);
    iCurrArea = hb_rddGetCurrentWorkAreaNumber();
    for (i = 0; i < cfl_list_length(areas); i++) {
      AREAP pArea = (AREAP)cfl_list_get(areas, i);
      hb_rddSelectWorkAreaNumber(pArea->uiArea);
      hb_rddReleaseCurrentArea();
    }
    hb_rddSelectWorkAreaNumber(iCurrArea);
    cfl_list_free(areas);
    if (connection->transaction) {
      sdb_connection_rollback(connection, CFL_FALSE);
    }
    connection->dbAPI->disconnect(connection);
    sdb_connection_free(connection);
  }
  RETURN;
}

static void releaseThreadUnusedStatements(SDB_AREAP pCurrSDBArea,
                                          CFL_UINT8 iCurrCommand) {
  SDB_THREAD_DATAP thData = sdb_thread_getData();
  CFL_UINT32 lenAreas;
  CFL_UINT32 iArea;
  time_t currentTime;

  time(&currentTime);
  if (difftime(currentTime, thData->lastStmtVerification) >
      s_intervalStmtVerification) {
    SDB_LOG_TRACE(("Verifying unused workareas: thread=%lld currTime=%d "
                   "lastVerif=%d interval=%d",
                   thData->threadId, currentTime, thData->lastStmtVerification,
                   s_intervalStmtVerification));
    lenAreas = cfl_list_length(thData->areas);
    for (iArea = 0; iArea < lenAreas; iArea++) {
      SDB_AREAP pSDBArea = (SDB_AREAP)cfl_list_get(thData->areas, iArea);
      if (pSDBArea != NULL && !pSDBArea->isQuery) {
        int iCmd;
        for (iCmd = 0; iCmd < SDB_CMD_COUNT; iCmd++) {
          if (pSDBArea != pCurrSDBArea || iCmd != iCurrCommand) {
            SDB_STATEMENTP pStmt = pSDBArea->commands[iCmd].pStmt;
            if (pStmt != NULL && difftime(thData->lastStmtVerification,
                                          pStmt->lastUseTime) > 0.0) {
              sdb_stmt_free(pStmt);
              pSDBArea->commands[iCmd].pStmt = NULL;
              SDB_LOG_TRACE(("Statement released: workarea=%s statement=%d",
                             sdb_area_getAlias(pSDBArea), iCmd));
            }
          }
        }
      }
    }
    time(&thData->lastStmtVerification);
  }
}

CFL_UINT16 sdb_api_nextBufferFetchSize(SDB_TABLEP table) {
  SDB_THREAD_DATAP thData = sdb_thread_getData();
  if (thData->nextBufferFetchSize > 0) {
    CFL_UINT16 nextSize = thData->nextBufferFetchSize;
    thData->nextBufferFetchSize = 0;
    return nextSize;
  }
  if (table && table->bufferFetchSize > 0) {
    return table->bufferFetchSize;
  }
  if (thData->defaultBufferFetchSize > 0) {
    return thData->defaultBufferFetchSize;
  }
  return s_DefaultBufferFetchSize;
}

// CFL_INT16 sdb_api_getRddId(void) {
//    ENTER_FUN_NAME("sdb_api_getRddId");
//    if (s_rddID == -1) {
//       HB_USHORT usRddId;
//
//       hb_rddFindNode(_SDB_RDDNAME_, &usRddId);
//       s_rddID = (CFL_INT16) usRddId;
//    }
//    RETURN s_rddID;
// }

void sdb_api_finalize(void) {
  ENTER_FUN_NAME("sdb_api_finalize");

  if (s_dataInitialized) {
    CFL_UINT32 len;
    CFL_UINT32 index;
    LOCK_API();
    if (s_dataInitialized) {
      s_dataInitialized = CFL_FALSE;
      sdb_thread_cleanError();
      len = cfl_list_length(s_connections);
      for (index = 0; index < len; index++) {
        releaseConnection((SDB_CONNECTIONP)cfl_list_get(s_connections, index));
      }
      cfl_list_free(s_connections);
      s_connections = NULL;

      len = cfl_list_length(s_products);
      for (index = 0; index < len; index++) {
        SDB_PRODUCTP product = (SDB_PRODUCTP)cfl_list_get(s_products, index);
        product->dbAPI->finalize(product);
        sdb_product_free(product);
      }
      cfl_list_free(s_products);
      s_products = NULL;
    }
    UNLOCK_API();
  }
  RETURN;
}

CFL_BOOL sdb_api_initializeInternalData(void) {
  if (!s_dataInitialized) {
    cfl_lock_init(&s_apiLock);
    s_dataInitialized = CFL_TRUE;
    s_products = cfl_list_new(3);
    s_connections = cfl_list_new(5);
    return CFL_TRUE;
  }
  return CFL_FALSE;
}

void sdb_api_lockApi(void) { LOCK_API(); }

void sdb_api_unlockApi(void) { UNLOCK_API(); }

CFL_BOOL sdb_api_registerProductAPI(const char *productName,
                                    const char *displayName,
                                    SDB_DB_APIP dbAPI) {
  CFL_UINT32 len;
  CFL_UINT32 i;
  CFL_BOOL bSuccess = CFL_TRUE;
  SDB_PRODUCTP product;

  ENTER_FUN_NAME("sdb_api_registerProductAPI");
  LOCK_API();
  len = cfl_list_length(s_products);
  for (i = 0; i < len; i++) {
    SDB_PRODUCTP productFound = (SDB_PRODUCTP)cfl_list_get(s_products, i);
    if (cfl_str_bufferEqualsIgnoreCase(productFound->name, productName)) {
      RETURN CFL_FALSE;
    }
  }
  product = sdb_product_new(productName, displayName, dbAPI);
  if (dbAPI->initialize(product)) {
    cfl_list_add(s_products, product);
  } else {
    sdb_product_free(product);
    bSuccess = CFL_FALSE;
  }
  UNLOCK_API();
  RETURN bSuccess;
}

SDB_PRODUCTP sdb_api_getProduct(const char *productName) {
  ENTER_FUN_NAME("sdb_api_getProduct");
  if (s_products) {
    CFL_UINT32 len;
    CFL_UINT32 i;
    len = cfl_list_length(s_products);
    if (productName) {
      for (i = 0; i < len; i++) {
        SDB_PRODUCTP product = (SDB_PRODUCTP)cfl_list_get(s_products, i);
        if (cfl_str_bufferEqualsIgnoreCase(product->name, productName)) {
          RETURN product;
        }
      }
      /* First product is the default */
    } else if (len > 0) {
      RETURN(SDB_PRODUCTP) cfl_list_get(s_products, 0);
    }
  }
  RETURN NULL;
}

SDB_CONNECTIONP sdb_api_getConnection(CFL_UINT32 idConnection) {
  SDB_CONNECTIONP conn = NULL;
  ENTER_FUN_NAME("sdb_api_getConnection");
  LOCK_API();
  if (s_connections != NULL) {
    CFL_UINT32 len = cfl_list_length(s_connections);
    CFL_UINT32 i;
    for (i = 0; i < len; i++) {
      SDB_CONNECTIONP connAux = (SDB_CONNECTIONP)cfl_list_get(s_connections, i);
      if (connAux != NULL && connAux->id == idConnection) {
        break;
      }
    }
  }
  UNLOCK_API();
  RETURN conn;
}

SDB_CONNECTIONP sdb_api_defaultConnection(void) {
  SDB_CONNECTIONP conn;
  ENTER_FUN_NAME("sdb_api_defaultConnection");
  LOCK_API();
  if (s_connections != NULL && cfl_list_length(s_connections) > 0) {
    conn = cfl_list_get(s_connections, 0);
  }
  UNLOCK_API();
  RETURN conn;
}

SDB_CONNECTIONP sdb_api_connect(SDB_PRODUCTP product, const char *database,
                                const char *username, const char *pswd,
                                CFL_BOOL registerConn) {
  SDB_CONNECTIONP conn = NULL;

  ENTER_FUN_NAME("sdb_api_connect");
  if (product) {
    conn = sdb_connection_new(product, database, username, pswd);
    if (product->dbAPI->connect(conn, database, username, pswd)) {
      LOCK_API();
      conn->id = s_nextConnectionId++;
      if (registerConn) {
        cfl_list_add(s_connections, conn);
      }
      UNLOCK_API();
    } else {
      sdb_connection_free(conn);
      conn = NULL;
    }
  }
  RETURN conn;
}

void sdb_api_disconnect(SDB_CONNECTIONP connection) {
  CFL_UINT32 len;
  CFL_UINT32 i;

  ENTER_FUN_NAME("sdb_api_disconnect");
  LOCK_API();
  len = cfl_list_length(s_connections);
  for (i = 0; i < len; i++) {
    SDB_CONNECTIONP connAux = (SDB_CONNECTIONP)cfl_list_get(s_connections, i);
    if (connAux != NULL && connAux->id == connection->id) {
      cfl_list_del(s_connections, i);
      break;
    }
  }
  UNLOCK_API();
  releaseConnection(connection);
  RETURN;
}

void sdb_api_registerArea(SDB_AREAP pSDBArea) {
  SDB_LOG_TRACE(("sdb_api_registerArea: area=%u", pSDBArea->area.uiArea));
  sdb_thread_addArea(pSDBArea);
  sdb_connection_addArea(pSDBArea->connection, pSDBArea);
}

void sdb_api_deregisterArea(SDB_AREAP pSDBArea) {
  SDB_LOG_TRACE(("sdb_api_deregisterArea: area=%u", pSDBArea->area.uiArea));
  sdb_thread_delArea(pSDBArea);
  sdb_connection_delArea(pSDBArea->connection, pSDBArea);
}

SDB_TABLEP sdb_api_getTable(SDB_CONNECTIONP connection, const char *schemaName,
                            const char *tableName) {
  SDB_SCHEMAP schema;
  SDB_TABLEP table = NULL;
  char *tableNameUpper;

  ENTER_FUN_NAME("sdb_api_getTable");
  schema = sdb_database_getCreateSchema(connection->database, schemaName);
  if (schema) {
    table = sdb_schema_getTable(schema, tableName);
    if (table == NULL) {
      SDB_DICTIONARYP dict;
      tableNameUpper = sdb_util_strnDupUpperTrim(tableName, strlen(tableName));
      dict = sdb_schema_getDict(schema, connection);
      if (dict != NULL) {
        table = dict->getTable(connection, schema, tableNameUpper);
        if (table) {
          sdb_schema_addTable(schema, table);
        }
      } else {
        sdb_thread_setError(SDB_ERROR_TYPE_SDB, SDB_ERROR_DICT_NOT_FOUND,
                            "Dictionary not found in schema %s",
                            cfl_str_getPtr(schema->name));
      }
      SDB_MEM_FREE(tableNameUpper);
    }
  }
  RETURN table;
}

static CFL_STRP contextId(SDB_AREAP pSDBArea) {
  if (pSDBArea->context != NULL && cfl_list_length(pSDBArea->context) > 0) {
    CFL_UINT32 len;
    CFL_UINT32 i;
    CFL_STRP ctxVal = cfl_str_new(64);
    PHB_ITEM pItem = hb_itemNew(NULL);

    len = cfl_list_length(pSDBArea->context);
    for (i = 0; i < len; i++) {
      SDB_WAFIELDP waField = (SDB_WAFIELDP)cfl_list_get(pSDBArea->context, i);
      if (i > 0) {
        cfl_str_appendChar(ctxVal, ',');
      }
      sdb_area_getFieldValue(pSDBArea, waField->field, pItem);
      cfl_str_appendStr(ctxVal, waField->field->clpName);
      cfl_str_appendChar(ctxVal, '=');
      sdb_util_strAppendItem(ctxVal, pItem);
    }
    hb_itemRelease(pItem);
    return ctxVal;
  }
  return NULL;
}

CFL_BOOL sdb_api_lockAreaTable(SDB_AREAP pSDBArea, int lockMode) {
  ENTER_FUN_NAME("sdb_api_lockAreaTable");
  switch (pSDBArea->connection->lockControl) {
  case SDB_LOCK_CONTROL_DB:
    if (!pSDBArea->table->locked) {
      pSDBArea->table->locked =
          pSDBArea->connection->dbAPI->lockTable(pSDBArea, lockMode);
    }
    RETURN pSDBArea->table->locked;
  case SDB_LOCK_CONTROL_SERVER:
    switch (lockMode) {
    case SDB_LOCK_SHARED:
      if (pSDBArea->lockServerCtxId == 0) {
        CFL_STRP context = contextId(pSDBArea);
        pSDBArea->lockServerCtxId = sdb_lckcli_openShared(
            pSDBArea->connection->lockClient, pSDBArea->connection->database,
            pSDBArea->table, context);
        if (context != NULL) {
          cfl_str_free(context);
        }
        RETURN pSDBArea->lockServerCtxId != 0;
      } else if (pSDBArea->isShared) {
        sdb_lckcli_unlockAll(pSDBArea->connection->lockClient,
                             pSDBArea->lockServerCtxId);
        pSDBArea->table->locked = CFL_FALSE;
        RETURN CFL_TRUE;
      }
      break;
    case SDB_LOCK_LOCK_ALL:
      if (!pSDBArea->table->locked) {
        pSDBArea->table->locked = sdb_lckcli_lockTable(
            pSDBArea->connection->lockClient, pSDBArea->lockServerCtxId);
      }
      RETURN pSDBArea->table->locked;
    case SDB_LOCK_EXCLUSIVE:
      if (pSDBArea->lockServerCtxId == 0) {
        CFL_STRP context = contextId(pSDBArea);
        pSDBArea->lockServerCtxId = sdb_lckcli_openExclusive(
            pSDBArea->connection->lockClient, pSDBArea->connection->database,
            pSDBArea->table, context);
        pSDBArea->table->locked = pSDBArea->lockServerCtxId != 0;
        if (context != NULL) {
          cfl_str_free(context);
        }
      }
      RETURN pSDBArea->table->locked;
    }
  }
  RETURN CFL_TRUE;
}

CFL_BOOL sdb_api_unlockAreaTable(SDB_AREAP pSDBArea) {
  CFL_UINT32 errorCount = sdb_thread_errorCount();
  ENTER_FUN_NAME("sdb_api_unlockAreaTable");
  switch (pSDBArea->connection->lockControl) {
  case SDB_LOCK_CONTROL_DB:
    pSDBArea->connection->dbAPI->unlockTable(pSDBArea);
    break;
  case SDB_LOCK_CONTROL_SERVER:
    /* Avoid send unecessary command to lock server */
    if (pSDBArea->lockServerCtxId > 0) {
      sdb_lckcli_closeTable(pSDBArea->connection->lockClient,
                            pSDBArea->lockServerCtxId);
      pSDBArea->lockServerCtxId = 0;
    }
    break;
  }
  pSDBArea->table->locked = CFL_FALSE;
  RETURN !sdb_thread_hasNewErrors(errorCount);
}

CFL_BOOL sdb_api_isErrorType(SDB_CONNECTIONP connection, SDB_ERRORP error,
                             CFL_INT32 errorCode) {
  return connection->dbAPI->isErrorType(error, errorCode);
}

static CFL_BOOL tableNextPK(SDB_CONNECTIONP conn, SDB_TABLEP table,
                            CFL_BOOL updatePK, PHB_ITEM pItem) {
  if (table->pkField->dbExpression == NULL) {
    SDB_DICTIONARYP dict = sdb_schema_getDict(table->clpSchema, conn);
    if (dict != NULL) {
      CFL_UINT32 errorCount = sdb_thread_errorCount();
      sdb_util_itemPutRecno(pItem, dict->nextRecno(conn, table, updatePK));
      return !sdb_thread_hasNewErrors(errorCount);
    } else {
      sdb_api_genError(NULL, SDB_ERROR_TYPE_SDB, SDB_ERROR_DICT_NOT_FOUND, NULL,
                       EF_NONE, "Dictionary not found in schema",
                       cfl_str_getPtr(table->clpSchema->name), NULL);
      return CFL_FALSE;
    }
  } else {
    return CFL_TRUE;
  }
}

static SDB_PARAMP setStmtValueForField(SDB_STATEMENTP pStatement,
                                       CFL_UINT32 paramPos, SDB_FIELDP field,
                                       PHB_ITEM pItem, CFL_BOOL bTrim,
                                       CFL_BOOL bOut) {
  SDB_PARAMP param = sdb_stmt_setValueByPos(pStatement, paramPos, pItem, bOut);
  if (param != NULL) {
    sdb_param_setLength(param, field->length);
    sdb_param_setType(param, field->clpType);
    sdb_param_setTrim(param, bTrim);
    sdb_param_setNullable(param, CFL_FALSE);
  }
  return param;
}

static void setContexParams(SDB_AREAP pSDBArea, SDB_STATEMENTP pStmt,
                            CFL_UINT32 *paramPos) {
  ENTER_FUN_NAME("setContextParams");
  if (pSDBArea->isContextActive) {
    CFL_UINT32 ulFields = cfl_list_length(pSDBArea->context);
    CFL_UINT32 ulIndex;
    PHB_ITEM pItem = hb_itemNew(NULL);

    for (ulIndex = 0; ulIndex < ulFields; ulIndex++) {
      SDB_WAFIELDP waField =
          (SDB_WAFIELDP)cfl_list_get(pSDBArea->context, ulIndex);
      sdb_area_getFieldValue(pSDBArea, waField->field, pItem);
      setStmtValueForField(pStmt, (*paramPos)++, waField->field, pItem,
                           CFL_FALSE, CFL_FALSE);
    }
    hb_itemRelease(pItem);
  }
  RETURN;
}

static void closeQueryStatements(SDB_AREAP pSDBArea, int iCommand,
                                 CFL_UINT8 uiSort) {
  SDB_LOG_TRACE(("closeQueryStatements: cmd=%d order=%u redo_queries=%s "
                 "sort=%u deleted=%s",
                 iCommand, pSDBArea->uiOrder, BOOL_STR(pSDBArea->isRedoQueries),
                 uiSort, BOOL_STR(hb_setGetDeleted())));
  if (pSDBArea->isRedoQueries) {
    pSDBArea->isRedoQueries = CFL_FALSE;
    sdb_area_closeQueryStatements(pSDBArea);
  } else if (iCommand >= SDB_CMD_FIRST_QUERY &&
             pSDBArea->commands[iCommand].pStmt != NULL &&
             sdb_area_discardCommand(pSDBArea, &pSDBArea->commands[iCommand],
                                     uiSort)) {
    sdb_stmt_free(pSDBArea->commands[iCommand].pStmt);
    pSDBArea->commands[iCommand].pStmt = NULL;
  }
}

static SDB_STATEMENTP prepareQueryAreaStmt(SDB_AREAP pSDBArea, int iCommand,
                                           CFL_STRP sql) {
  pSDBArea->pStatement =
      sdb_connection_prepareStatement(pSDBArea->connection, sql);
  if (pSDBArea->pStatement != NULL) {
    pSDBArea->pStatement->bufferFetchSize = TABLE_FETCH_SIZE(pSDBArea->table);
    pSDBArea->pStatement->fetchSize = 1;
    pSDBArea->commands[iCommand].pStmt = pSDBArea->pStatement;
  }
  return pSDBArea->pStatement;
}

static void prepareQueryCommand(SDB_AREAP pSDBArea, int iCommand,
                                CFL_UINT8 uiSort, CFL_UINT8 uiQueryMore) {
  SDB_LOG_DEBUG(("prepareCommand: cmd=%d sort=%d, query_more=%d, stmt=%p",
                 iCommand, uiSort, uiQueryMore,
                 pSDBArea->commands[iCommand].pStmt));
  pSDBArea->commands[iCommand].uiOrder = pSDBArea->uiOrder;
  pSDBArea->commands[iCommand].uiQueryMore = uiQueryMore;
  pSDBArea->commands[iCommand].uiSort = uiSort;
  pSDBArea->commands[iCommand].isFilterDeleted = hb_setGetDeleted();
  pSDBArea->pCurrentCommand = &pSDBArea->commands[iCommand];
  pSDBArea->isUnpositioned = CFL_FALSE;
  pSDBArea->pStatement = pSDBArea->commands[iCommand].pStmt;
}

static CFL_STRP orderHint(SDB_TABLEP table, SDB_INDEXP index,
                          CFL_UINT8 uiSort) {
  if (uiSort == SDB_SORT_ASC) {
    if (index != NULL && !cfl_str_isBlank(index->field->indexAscHint)) {
      return index->field->indexAscHint;
    } else {
      return table->hintAsc;
    }
  } else if (index != NULL && !cfl_str_isBlank(index->field->indexDescHint)) {
    return index->field->indexDescHint;
  } else {
    return table->hintDesc;
  }
}

static void querySetHint(CFL_SQL_QUERYP query, SDB_AREAP pSDBArea,
                         CFL_UINT8 uiSort) {
  if (uiSort != SDB_SORT_NO && pSDBArea->connection->isHintsEnable) {
    CFL_STRP hint =
        orderHint(pSDBArea->table, sdb_area_getCurrentOrder(pSDBArea), uiSort);
    if (!cfl_str_isBlank(hint)) {
      query->hint(query, hint);
    }
  }
}

static CFL_SQL_QUERYP selectFrom(SDB_AREAP pSDBArea, CFL_UINT8 uiSort) {
  CFL_SQL_BUILDERP b = pSDBArea->connection->dbAPI->sqlBuilder();
  SDB_CONNECTIONP conn = pSDBArea->connection;
  SDB_TABLEP table = pSDBArea->table;
  CFL_SQL_QUERYP query = b->query();
  CFL_UINT16 pos = 1;
  CFL_UINT32 ulIndex;
  SDB_INDEXP index = sdb_area_getCurrentOrder(pSDBArea);

  querySetHint(query, pSDBArea, uiSort);

  for (ulIndex = 0; ulIndex < pSDBArea->fieldsCount; ulIndex++) {
    SDB_WAFIELDP waField = sdb_area_getFieldByPos(pSDBArea, ulIndex);
    switch (waField->field->fieldType) {
    case SDB_FIELD_DATA:
    case SDB_FIELD_PK:
    case SDB_FIELD_DEL_FLAG:
    case SDB_FIELD_DATA_INDEX:
      query->select(query, b->id(waField->field->dbName), NULL);
      waField->queryPos = pos++;
      break;
    case SDB_FIELD_ROWID:
      if (conn->isUseRowId) {
        query->select(query, b->id(waField->field->dbName), NULL);
        waField->queryPos = pos++;
      } else {
        waField->queryPos = 0;
      }
      break;
    case SDB_FIELD_INDEX:
      if (index != NULL && index->field == waField->field) {
        query->select(query, b->id(waField->field->dbName), NULL);
        waField->queryPos = pos++;
      } else {
        waField->queryPos = 0;
      }
      break;
    }
  }

  query->from(query, b->quali_id(table->dbSchema->name, table->dbName, NULL),
              NULL);
  return query;
}

static CFL_SQLP contexCondition(SDB_AREAP pSDBArea) {
  CFL_SQLP condition = NULL;
  ENTER_FUN_NAME("contexCondition");
  if (pSDBArea->isContextActive) {
    CFL_SQL_BUILDERP b = pSDBArea->connection->dbAPI->sqlBuilder();
    CFL_UINT32 ulFields = cfl_list_length(pSDBArea->context);
    CFL_UINT32 ulIndex;
    char parName[12];
    for (ulIndex = 0; ulIndex < ulFields; ulIndex++) {
      SDB_WAFIELDP fieldInfo =
          (SDB_WAFIELDP)cfl_list_get(pSDBArea->context, ulIndex);
      sprintf(parName, "C%u", ulIndex + 1);
      condition = b->and (condition, b->equal(b->id(fieldInfo->field->dbName),
                                              b->c_param(parName)));
    }
  }
  RETURN condition;
}

static void queryOrderBy(CFL_SQL_QUERYP query, SDB_AREAP pSDBArea,
                         CFL_UINT8 uiSort) {
  if (uiSort != SDB_SORT_NO) {
    CFL_SQL_BUILDERP b = pSDBArea->connection->dbAPI->sqlBuilder();
    SDB_TABLEP table = pSDBArea->table;
    SDB_INDEXP index = sdb_area_getCurrentOrder(pSDBArea);
    if (index != NULL) {
      if (uiSort == SDB_SORT_DESC) {
        query->orderBy(query, b->desc(b->id(index->field->dbName)), NULL);
      } else {
        query->orderBy(query, b->id(index->field->dbName), NULL);
      }
    }
    if (uiSort == SDB_SORT_DESC) {
      query->orderBy(query, b->desc(b->id(table->pkField->dbName)), NULL);
    } else {
      query->orderBy(query, b->id(table->pkField->dbName), NULL);
    }
  }
}

static CFL_SQLP subqueryLimitsCondition(SDB_AREAP pSDBArea, CFL_UINT8 uiSort) {
  SDB_FIELDP fieldOrder;
  CFL_SQL_BUILDERP b = pSDBArea->connection->dbAPI->sqlBuilder();
  SDB_INDEXP index = sdb_area_getCurrentOrder(pSDBArea);
  CFL_SQL_QUERYP sub = b->query();
  CFL_SQLP subCondition = NULL;
  char *funName;

  if (index != NULL) {
    fieldOrder = index->field;
  } else {
    fieldOrder = pSDBArea->table->pkField;
  }
  querySetHint(sub, pSDBArea, uiSort);
  funName = uiSort == SDB_SORT_DESC ? "max" : "min";
  sub->select(sub, b->c_fun(funName, b->id(fieldOrder->dbName), NULL));
  sub->from(sub,
            b->quali_id(pSDBArea->table->dbSchema->name,
                        pSDBArea->table->dbName, NULL),
            NULL);
  if (hb_setGetDeleted()) {
    subCondition =
        b->equal(b->id(pSDBArea->table->delField->dbName), b->c_param("D"));
  }
  subCondition = b->and (subCondition, contexCondition(pSDBArea));
  sub->where(sub, subCondition);
  return b->equal(b->id(fieldOrder->dbName), b->parentheses((CFL_SQLP)sub));
}

static CFL_SQLP queryFilterCondition(SDB_AREAP pSDBArea) {
  CFL_SQLP filter = NULL;
  ENTER_FUN_NAME("queryFilterCondition");
  if (!sdb_util_itemIsNull(pSDBArea->sqlFilter)) {
    CFL_SQL_BUILDERP b = pSDBArea->connection->dbAPI->sqlBuilder();
    filter = b->c_custom((char *)hb_itemGetCPtr(pSDBArea->sqlFilter));
  }
  RETURN filter;
}

static CFL_BOOL tableQueryLimits(SDB_AREAP pSDBArea, CFL_UINT8 iCommand,
                                 CFL_UINT8 uiSort) {
  CFL_UINT64 ulAffectedRows;
  CFL_UINT32 paramPos = 1;
  CFL_UINT32 errorCount = sdb_thread_errorCount();

  ENTER_FUN_NAME("tableQueryLimits");

  releaseThreadUnusedStatements(pSDBArea, iCommand);
  LOCK_AREA(pSDBArea);
  closeQueryStatements(pSDBArea, iCommand, uiSort);
  prepareQueryCommand(pSDBArea, iCommand, uiSort, QRY_MORE_IF_FOUND);
  if (pSDBArea->pStatement == NULL) {
    CFL_SQL_BUILDERP b = pSDBArea->connection->dbAPI->sqlBuilder();
    CFL_SQL_QUERYP query = selectFrom(pSDBArea, uiSort);
    CFL_SQLP condition = NULL;
    CFL_STRP sql = cfl_str_new(2048);

    /* Se nao deve trazer os deletados, filtra eles */
    if (hb_setGetDeleted()) {
      condition =
          b->equal(b->id(pSDBArea->table->delField->dbName), b->c_param("D"));
    }
    condition = b->and (condition, contexCondition(pSDBArea));
    condition = b->and (condition, subqueryLimitsCondition(pSDBArea, uiSort));
    condition = b->and (condition, queryFilterCondition(pSDBArea));
    query->where(query, condition);
    queryOrderBy(query, pSDBArea, uiSort);
    CFL_SQL_TO_STRING(query, sql);
    prepareQueryAreaStmt(pSDBArea, iCommand, sql);
    cfl_str_free(sql);
    CFL_SQL_FREE(query);
  }
  /* Se nao deve trazer os deletados, filtra eles */
  if (hb_setGetDeleted()) {
    sdb_stmt_setBooleanByPos(pSDBArea->pStatement, paramPos++, CFL_FALSE,
                             CFL_FALSE);
  }
  setContexParams(pSDBArea, pSDBArea->pStatement, &paramPos);
  /* Subquery params */
  if (hb_setGetDeleted()) {
    sdb_stmt_setBooleanByPos(pSDBArea->pStatement, paramPos++, CFL_FALSE,
                             CFL_FALSE);
  }
  setContexParams(pSDBArea, pSDBArea->pStatement, &paramPos);
  time(&pSDBArea->pStatement->lastUseTime);
  UNLOCK_AREA(pSDBArea);
  if (!sdb_thread_hasNewErrors(errorCount)) {
    sdb_stmt_execute(pSDBArea->pStatement, &ulAffectedRows);
  }
  RETURN !sdb_thread_hasNewErrors(errorCount);
}

static CFL_BOOL tableQueryAll(SDB_AREAP pSDBArea, CFL_UINT8 iCommand,
                              CFL_UINT8 uiSort) {
  CFL_UINT64 ulAffectedRows;
  CFL_UINT32 paramPos = 1;
  CFL_UINT32 errorCount = sdb_thread_errorCount();

  ENTER_FUN_NAME("tableQueryAll");

  releaseThreadUnusedStatements(pSDBArea, iCommand);
  LOCK_AREA(pSDBArea);
  closeQueryStatements(pSDBArea, iCommand, uiSort);
  prepareQueryCommand(pSDBArea, iCommand, uiSort, QRY_MORE_NO);
  if (pSDBArea->pStatement == NULL) {
    CFL_SQL_BUILDERP b = pSDBArea->connection->dbAPI->sqlBuilder();
    CFL_SQL_QUERYP query = selectFrom(pSDBArea, uiSort);
    CFL_SQLP condition = NULL;
    CFL_STRP sql = cfl_str_new(2048);

    /* Se nao deve trazer os deletados, filtra eles */
    if (hb_setGetDeleted()) {
      condition =
          b->equal(b->id(pSDBArea->table->delField->dbName), b->c_param("D"));
    }
    condition = b->and (condition, contexCondition(pSDBArea));
    condition = b->and (condition, queryFilterCondition(pSDBArea));
    query->where(query, condition);
    queryOrderBy(query, pSDBArea, uiSort);
    CFL_SQL_TO_STRING(query, sql);
    prepareQueryAreaStmt(pSDBArea, iCommand, sql);
    cfl_str_free(sql);
    CFL_SQL_FREE(query);
  }
  /* Se nao deve trazer os deletados, filtra eles */
  if (hb_setGetDeleted()) {
    sdb_stmt_setBooleanByPos(pSDBArea->pStatement, paramPos++, CFL_FALSE,
                             CFL_FALSE);
  }
  setContexParams(pSDBArea, pSDBArea->pStatement, &paramPos);
  time(&pSDBArea->pStatement->lastUseTime);
  UNLOCK_AREA(pSDBArea);
  if (!sdb_thread_hasNewErrors(errorCount)) {
    sdb_stmt_execute(pSDBArea->pStatement, &ulAffectedRows);
  }
  RETURN !sdb_thread_hasNewErrors(errorCount);
}

CFL_BOOL sdb_api_tableGoTop(SDB_AREAP pSDBArea) {
  ENTER_FUN_NAME("sdb_api_tableGoTop");
  if (pSDBArea->uiQueryTopMode == QRY_TOP_MIN && pSDBArea->uiOrder > 0) {
    RETURN tableQueryLimits(pSDBArea, SDB_CMD_GO_TOP, SDB_SORT_ASC);
  } else {
    RETURN tableQueryAll(pSDBArea, SDB_CMD_GO_TOP, SDB_SORT_ASC);
  }
}

CFL_BOOL sdb_api_tableGoBottom(SDB_AREAP pSDBArea) {
  ENTER_FUN_NAME("sdb_api_tableGoBottom");
  if (pSDBArea->uiQueryBottomMode == QRY_BOTTOM_MAX && pSDBArea->uiOrder > 0) {
    RETURN tableQueryLimits(pSDBArea, SDB_CMD_GO_BOTTOM, SDB_SORT_DESC);
  } else {
    RETURN tableQueryAll(pSDBArea, SDB_CMD_GO_BOTTOM, SDB_SORT_DESC);
  }
}

CFL_BOOL sdb_api_tableGoTo(SDB_AREAP pSDBArea, PHB_ITEM pRecId) {
  CFL_UINT8 iCommand;
  CFL_UINT64 ulAffectedRows;
  CFL_UINT32 paramPos = 1;
  CFL_UINT32 errorCount = sdb_thread_errorCount();

  // HB_SIZE strLen;
  // HB_BOOL freeStr;
  // char *strValue;

  ENTER_FUN_NAME("sdb_api_tableGoTo");

  iCommand =
      pSDBArea->connection->isUseRowId && hb_itemType(pRecId) & HB_IT_STRING
          ? SDB_CMD_GO_TO_ROWID
          : SDB_CMD_GO_TO_RECNO;
  releaseThreadUnusedStatements(pSDBArea, iCommand);
  LOCK_AREA(pSDBArea);
  closeQueryStatements(pSDBArea, iCommand, SDB_SORT_NO);
  prepareQueryCommand(pSDBArea, iCommand, SDB_SORT_NO, QRY_MORE_YES);
  if (pSDBArea->pStatement == NULL) {
    CFL_SQL_BUILDERP b = pSDBArea->connection->dbAPI->sqlBuilder();
    CFL_SQL_QUERYP query = selectFrom(pSDBArea, SDB_SORT_NO);
    CFL_SQLP condition;
    CFL_STRP sql = cfl_str_new(2048);

    if (iCommand == SDB_CMD_GO_TO_ROWID) {
      condition =
          b->equal(b->id(pSDBArea->table->rowIdField->dbName), b->c_param("R"));
    } else {
      condition =
          b->equal(b->id(pSDBArea->table->pkField->dbName), b->c_param("R"));
    }
    condition = b->and (condition, contexCondition(pSDBArea));
    query->where(query, condition);
    CFL_SQL_TO_STRING(query, sql);
    prepareQueryAreaStmt(pSDBArea, iCommand, sql);
    cfl_str_free(sql);
    CFL_SQL_FREE(query);
  }
  // strValue = hb_itemString(pRecId, &strLen, &freeStr);
  // sdb_stmt_setCharLenByPos(pSDBArea->pStatement, paramPos++, strValue,
  // (CFL_UINT32) strLen, CFL_FALSE); if (freeStr) {
  //    hb_xfree(strValue);
  // }
  sdb_stmt_setValueByPos(pSDBArea->pStatement, paramPos++, pRecId, CFL_FALSE);
  setContexParams(pSDBArea, pSDBArea->pStatement, &paramPos);
  time(&pSDBArea->pStatement->lastUseTime);
  UNLOCK_AREA(pSDBArea);
  if (!sdb_thread_hasNewErrors(errorCount)) {
    sdb_stmt_execute(pSDBArea->pStatement, &ulAffectedRows);
  }
  RETURN !sdb_thread_hasNewErrors(errorCount);
}

CFL_BOOL sdb_api_tableUnlockRecord(SDB_AREAP pSDBArea, PHB_ITEM pRecno,
                                   PHB_ITEM pRowId) {
  ENTER_FUN_NAME("sdb_api_tableUnlockRecord");
  if (!pSDBArea->table->locked) {
    CFL_UINT32 errorCount = sdb_thread_errorCount();
    switch (pSDBArea->connection->lockControl) {
    case SDB_LOCK_CONTROL_DB:
      pSDBArea->connection->dbAPI->unlockRecord(pSDBArea, pRecno, pRowId);
      break;
    case SDB_LOCK_CONTROL_SERVER:
      sdb_lckcli_unlockRecord(pSDBArea->connection->lockClient,
                              pSDBArea->lockServerCtxId,
                              (SDB_RECNO)hb_itemGetNLL(pRecno));
      break;
    }
    RETURN !sdb_thread_hasNewErrors(errorCount);
  }
  RETURN CFL_TRUE;
}

static CFL_BOOL lockRecord(SDB_AREAP pSDBArea, PHB_ITEM pRecno,
                           PHB_ITEM pRowId) {
  switch (pSDBArea->connection->lockControl) {
  case SDB_LOCK_CONTROL_DB:
    return pSDBArea->connection->dbAPI->lockRecord(pSDBArea, pRecno, pRowId);
  case SDB_LOCK_CONTROL_SERVER:
    return sdb_lckcli_lockRecord(pSDBArea->connection->lockClient,
                                 pSDBArea->lockServerCtxId,
                                 (SDB_RECNO)hb_itemGetNLL(pRecno));
  }
  return CFL_TRUE;
}

CFL_BOOL sdb_api_tableLockRecord(SDB_AREAP pSDBArea, PHB_ITEM pRecno,
                                 PHB_ITEM pRowId) {
  CFL_UINT8 iCommand;
  CFL_UINT64 ulAffectedRows;
  CFL_UINT32 errorCount = sdb_thread_errorCount();
  CFL_UINT32 paramPos = 1;

  ENTER_FUN_NAME("sdb_api_tableLockRecord");

  iCommand = pRowId ? SDB_CMD_LOCK_ROWID : SDB_CMD_LOCK_RECNO;
  releaseThreadUnusedStatements(pSDBArea, iCommand);
  if (!lockRecord(pSDBArea, pRecno, pRowId)) {
    RETURN CFL_FALSE;
  }
  LOCK_AREA(pSDBArea);
  closeQueryStatements(pSDBArea, iCommand, SDB_SORT_NO);
  prepareQueryCommand(pSDBArea, iCommand, SDB_SORT_NO, QRY_MORE_NO);
  if (pSDBArea->pStatement == NULL) {
    CFL_SQL_BUILDERP b = pSDBArea->connection->dbAPI->sqlBuilder();
    CFL_SQL_QUERYP query = selectFrom(pSDBArea, SDB_SORT_NO);
    CFL_SQLP condition;
    CFL_STRP sql = cfl_str_new(2048);

    if (iCommand == SDB_CMD_GO_TO_ROWID) {
      condition =
          b->equal(b->id(pSDBArea->table->rowIdField->dbName), b->c_param("R"));
    } else {
      condition =
          b->equal(b->id(pSDBArea->table->pkField->dbName), b->c_param("R"));
    }
    condition = b->and (condition, contexCondition(pSDBArea));
    query->where(query, condition);
    query->forUpdate(query, pSDBArea->waitTimeLock);
    CFL_SQL_TO_STRING(query, sql);
    prepareQueryAreaStmt(pSDBArea, iCommand, sql);
    cfl_str_free(sql);
    CFL_SQL_FREE(query);
  }
  if (iCommand == SDB_CMD_LOCK_ROWID) {
    sdb_stmt_setValueByPos(pSDBArea->pStatement, paramPos++, pRowId, CFL_FALSE);
  } else {
    sdb_stmt_setValueByPos(pSDBArea->pStatement, paramPos++, pRecno, CFL_FALSE);
  }
  setContexParams(pSDBArea, pSDBArea->pStatement, &paramPos);
  time(&pSDBArea->pStatement->lastUseTime);
  UNLOCK_AREA(pSDBArea);
  if (!sdb_thread_hasNewErrors(errorCount)) {
    sdb_stmt_execute(pSDBArea->pStatement, &ulAffectedRows);
  }
  RETURN !sdb_thread_hasNewErrors(errorCount);
}

CFL_BOOL sdb_api_tableSeek(SDB_AREAP pSDBArea, PHB_ITEM pSearchValue,
                           CFL_UINT8 uiSort, CFL_BOOL bSoft) {
  SDB_INDEXP index;
  CFL_BOOL bExact;
  CFL_UINT8 iCommand;
  CFL_UINT64 ulAffectedRows;
  SDB_PARAMP param;
  CFL_UINT32 errorCount = sdb_thread_errorCount();
  CFL_UINT32 paramPos = 1;
  CFL_UINT8 uiMore;

  SDB_LOG_DEBUG(("sdb_api_tableSeek: search=%s sort=%d soft=%s",
                 ITEM_STR(pSearchValue), uiSort, BOOL_STR(bSoft)));
  index = sdb_area_getCurrentOrder(pSDBArea);
  bExact = !bSoft && pSDBArea->isExactQuery &&
           sdb_util_itemLen(pSearchValue, CFL_TRUE) == index->field->length;
  if (bExact) {
    iCommand = SDB_CMD_SEEK_EXACT;
    uiMore = QRY_MORE_IF_FOUND;
  } else {
    iCommand = SDB_CMD_SEEK;
    uiMore = QRY_MORE_NO;
  }
  releaseThreadUnusedStatements(pSDBArea, iCommand);
  LOCK_AREA(pSDBArea);
  closeQueryStatements(pSDBArea, iCommand, uiSort);
  prepareQueryCommand(pSDBArea, iCommand, uiSort, uiMore);
  if (pSDBArea->pStatement == NULL) {
    CFL_SQL_BUILDERP b = pSDBArea->connection->dbAPI->sqlBuilder();
    CFL_SQL_QUERYP query = selectFrom(pSDBArea, uiSort);
    CFL_SQLP condition = NULL;
    CFL_STRP sql = cfl_str_new(2048);

    /* Se nao deve trazer os deletados, filtra eles */
    if (hb_setGetDeleted()) {
      condition =
          b->equal(b->id(pSDBArea->table->delField->dbName), b->c_param("D"));
    }
    if (bExact) {
      condition = b->and (condition, b->equal(b->id(index->field->dbName),
                                              b->c_param("I")));
    } else if (uiSort == SDB_SORT_ASC) {
      condition =
          b->and (condition, b->greaterEqual(b->id(index->field->dbName),
                                             b->c_param("I")));
    } else {
      condition = b->and (condition, b->lessEqual(b->id(index->field->dbName),
                                                  b->c_param("I")));
    }
    condition = b->and (condition, contexCondition(pSDBArea));
    condition = b->and (condition, queryFilterCondition(pSDBArea));
    query->where(query, condition);
    queryOrderBy(query, pSDBArea, uiSort);
    CFL_SQL_TO_STRING(query, sql);
    prepareQueryAreaStmt(pSDBArea, iCommand, sql);
    cfl_str_free(sql);
    CFL_SQL_FREE(query);
  }
  /* Se nao deve trazer os deletados, filtra eles */
  if (hb_setGetDeleted()) {
    sdb_stmt_setBooleanByPos(pSDBArea->pStatement, paramPos++, CFL_FALSE,
                             CFL_FALSE);
  }
  param = sdb_stmt_setValueByPos(pSDBArea->pStatement, paramPos++, pSearchValue,
                                 CFL_FALSE);
  if (param != NULL) {
    sdb_param_setTrim(param, CFL_TRUE);
  }
  setContexParams(pSDBArea, pSDBArea->pStatement, &paramPos);
  time(&pSDBArea->pStatement->lastUseTime);
  UNLOCK_AREA(pSDBArea);
  if (!sdb_thread_hasNewErrors(errorCount)) {
    sdb_stmt_execute(pSDBArea->pStatement, &ulAffectedRows);
  }
  return !sdb_thread_hasNewErrors(errorCount);
}

CFL_BOOL sdb_api_tableQuery(SDB_AREAP pSDBArea, PHB_ITEM pCondition,
                            SDB_PARAMLISTP conditionParams) {
  CFL_UINT64 ulAffectedRows;
  CFL_UINT32 errorCount = sdb_thread_errorCount();
  CFL_SQL_BUILDERP b;
  CFL_SQL_QUERYP query;
  CFL_SQLP condition;
  CFL_STRP sql;
  CFL_UINT32 paramPos = 1;

  ENTER_FUN_NAME("sdb_api_tableQuery");

  releaseThreadUnusedStatements(pSDBArea, 0xFF);
  LOCK_AREA(pSDBArea);
  closeQueryStatements(pSDBArea, SDB_CMD_GO_TOP, SDB_SORT_NO);
  prepareQueryCommand(pSDBArea, SDB_CMD_GO_TOP, SDB_SORT_NO, QRY_MORE_NO);

  sql = cfl_str_new(2048);
  b = pSDBArea->connection->dbAPI->sqlBuilder();
  query = selectFrom(pSDBArea, SDB_SORT_NO);
  condition = contexCondition(pSDBArea);
  condition = b->and (condition, b->format("%s", hb_itemGetCPtr(pCondition)));
  query->where(query, condition);
  CFL_SQL_TO_STRING(query, sql);
  prepareQueryAreaStmt(pSDBArea, SDB_CMD_GO_TOP, sql);
  pSDBArea->pStatement->bufferFetchSize = SDB_STMT_FETCH_SIZE;
  pSDBArea->pStatement->fetchSize = SDB_STMT_FETCH_SIZE;
  cfl_str_free(sql);
  CFL_SQL_FREE(query);
  setContexParams(pSDBArea, pSDBArea->pStatement, &paramPos);
  if (conditionParams != NULL && sdb_param_listLength(conditionParams) > 0) {
    sdb_param_listMoveAll(sdb_stmt_getParams(pSDBArea->pStatement),
                          conditionParams);
  }
  if (!sdb_thread_hasNewErrors(errorCount)) {
    sdb_stmt_execute(pSDBArea->pStatement, &ulAffectedRows);
  }
  UNLOCK_AREA(pSDBArea);
  RETURN !sdb_thread_hasNewErrors(errorCount);
}

CFL_BOOL sdb_api_tableNextRecord(SDB_AREAP pSDBArea, PHB_ITEM pRecId,
                                 PHB_ITEM pKey, CFL_UINT8 uiSort) {
  SDB_INDEXP index;
  CFL_UINT64 ulAffectedRows;
  CFL_UINT8 iCommand;
  CFL_UINT32 errorCount = sdb_thread_errorCount();
  CFL_UINT32 paramPos = 1;

  SDB_LOG_DEBUG(
      ("sdb_api_tableNextRecord: recId=%s key=%s isRedoQueries=%s uiOrder=%d",
       ITEM_STR(pRecId), ITEM_STR(pKey), BOOL_STR(pSDBArea->isRedoQueries),
       pSDBArea->uiOrder));
  iCommand = uiSort == SDB_SORT_DESC ? SDB_CMD_NEXT_RECNO_DESC
                                     : SDB_CMD_NEXT_RECNO_ASC;

  releaseThreadUnusedStatements(pSDBArea, iCommand);
  LOCK_AREA(pSDBArea);
  closeQueryStatements(pSDBArea, iCommand, uiSort);
  index = sdb_area_getCurrentOrder(pSDBArea);
  prepareQueryCommand(pSDBArea, iCommand, uiSort,
                      (index != NULL ? QRY_MORE_YES : QRY_MORE_NO));
  if (pSDBArea->pStatement == NULL) {
    CFL_SQL_BUILDERP b = pSDBArea->connection->dbAPI->sqlBuilder();
    CFL_SQL_QUERYP query = selectFrom(pSDBArea, uiSort);
    CFL_SQLP condition = NULL;
    CFL_STRP sql = cfl_str_new(2048);

    /* Se nao deve trazer os deletados, filtra eles */
    if (hb_setGetDeleted()) {
      condition =
          b->equal(b->id(pSDBArea->table->delField->dbName), b->c_param("D"));
    }
    if (uiSort == SDB_SORT_DESC) {
      condition =
          b->and (condition, b->less(b->id(pSDBArea->table->pkField->dbName),
                                     b->c_param("R")));
    } else {
      condition =
          b->and (condition, b->greater(b->id(pSDBArea->table->pkField->dbName),
                                        b->c_param("R")));
    }

    if (index != NULL) {
      condition = b->and (condition, b->equal(b->id(index->field->dbName),
                                              b->c_param("I")));
    }
    condition = b->and (condition, contexCondition(pSDBArea));
    condition = b->and (condition, queryFilterCondition(pSDBArea));
    query->where(query, condition);
    queryOrderBy(query, pSDBArea, uiSort);
    CFL_SQL_TO_STRING(query, sql);
    prepareQueryAreaStmt(pSDBArea, iCommand, sql);
    cfl_str_free(sql);
    CFL_SQL_FREE(query);
  }
  /* Se nao deve trazer os deletados, filtra eles */
  if (hb_setGetDeleted()) {
    sdb_stmt_setBooleanByPos(pSDBArea->pStatement, paramPos++, CFL_FALSE,
                             CFL_FALSE);
  }
  sdb_stmt_setValueByPos(pSDBArea->pStatement, paramPos++, pRecId, CFL_FALSE);
  if (index) {
    sdb_stmt_setValueByPos(pSDBArea->pStatement, paramPos++, pKey, CFL_FALSE);
  }
  setContexParams(pSDBArea, pSDBArea->pStatement, &paramPos);
  time(&pSDBArea->pStatement->lastUseTime);
  UNLOCK_AREA(pSDBArea);
  if (!sdb_thread_hasNewErrors(errorCount)) {
    sdb_stmt_execute(pSDBArea->pStatement, &ulAffectedRows);
  }
  return !sdb_thread_hasNewErrors(errorCount);
}

CFL_BOOL sdb_api_tableNextKey(SDB_AREAP pSDBArea, PHB_ITEM pKey,
                              CFL_UINT8 uiSort) {
  SDB_INDEXP index;
  CFL_UINT64 ulAffectedRows;
  CFL_UINT8 iCommand;
  CFL_UINT32 errorCount = sdb_thread_errorCount();
  CFL_UINT32 paramPos = 1;

  SDB_LOG_DEBUG(("sdb_api_tableNextKey: key=%s isRedoQueries=%s uiOrder=%d",
                 ITEM_STR(pKey), BOOL_STR(pSDBArea->isRedoQueries),
                 pSDBArea->uiOrder));
  iCommand =
      uiSort == SDB_SORT_DESC ? SDB_CMD_NEXT_KEY_DESC : SDB_CMD_NEXT_KEY_ASC;

  releaseThreadUnusedStatements(pSDBArea, iCommand);
  LOCK_AREA(pSDBArea);
  closeQueryStatements(pSDBArea, iCommand, uiSort);
  prepareQueryCommand(pSDBArea, iCommand, uiSort, QRY_MORE_NO);
  index = sdb_area_getCurrentOrder(pSDBArea);
  if (pSDBArea->pStatement == NULL) {
    CFL_SQL_BUILDERP b = pSDBArea->connection->dbAPI->sqlBuilder();
    CFL_SQL_QUERYP query = selectFrom(pSDBArea, uiSort);
    CFL_SQLP condition = NULL;
    CFL_STRP sql = cfl_str_new(2048);

    /* Se nao deve trazer os deletados, filtra eles */
    if (hb_setGetDeleted()) {
      condition =
          b->equal(b->id(pSDBArea->table->delField->dbName), b->c_param("D"));
    }

    if (uiSort == SDB_SORT_DESC) {
      condition = b->and (condition, b->less(b->id(index->field->dbName),
                                             b->c_param("I")));
    } else {
      condition = b->and (condition, b->greater(b->id(index->field->dbName),
                                                b->c_param("I")));
    }
    condition = b->and (condition, contexCondition(pSDBArea));
    condition = b->and (condition, queryFilterCondition(pSDBArea));
    query->where(query, condition);
    queryOrderBy(query, pSDBArea, uiSort);
    CFL_SQL_TO_STRING(query, sql);
    prepareQueryAreaStmt(pSDBArea, iCommand, sql);
    cfl_str_free(sql);
    CFL_SQL_FREE(query);
  }
  /* Se nao deve trazer os deletados, filtra eles */
  if (hb_setGetDeleted()) {
    sdb_stmt_setBooleanByPos(pSDBArea->pStatement, paramPos++, CFL_FALSE,
                             CFL_FALSE);
  }
  sdb_stmt_setValueByPos(pSDBArea->pStatement, paramPos++, pKey, CFL_FALSE);
  setContexParams(pSDBArea, pSDBArea->pStatement, &paramPos);
  time(&pSDBArea->pStatement->lastUseTime);
  UNLOCK_AREA(pSDBArea);
  if (!sdb_thread_hasNewErrors(errorCount)) {
    sdb_stmt_execute(pSDBArea->pStatement, &ulAffectedRows);
  }
  return !sdb_thread_hasNewErrors(errorCount);
}

static CFL_BOOL addTableColumn(SDB_TABLEP table, SDB_QUERY_COL_INFOP info,
                               CFL_UINT8 precision, CFL_UINT8 scale,
                               CFL_BOOL bRightPadded) {
  SDB_FIELDP field;

  SDB_LOG_DEBUG(("addTableColumn: name=%s, type=%d, precision=%d, scale=%d, "
                 "defaultPrecision=%d, defaultScale=%d",
                 cfl_str_getPtr(info->name), info->clpType, info->precision,
                 info->scale, precision, scale));
  switch (info->clpType) {
  case SDB_CLP_CHARACTER:
    field = sdb_field_new(
        cfl_str_getPtr(info->name), cfl_str_getLength(info->name), NULL, 0,
        SDB_FIELD_DATA, SDB_CLP_CHARACTER, info->size, 0, bRightPadded);
    break;

  case SDB_CLP_NUMERIC:
    if (info->precision > 0) {
      field = sdb_field_new(cfl_str_getPtr(info->name),
                            cfl_str_getLength(info->name), NULL, 0,
                            SDB_FIELD_DATA, SDB_CLP_NUMERIC, info->precision,
                            info->scale, bRightPadded);
    } else {
      field = sdb_field_new(
          cfl_str_getPtr(info->name), cfl_str_getLength(info->name), NULL, 0,
          SDB_FIELD_DATA, SDB_CLP_NUMERIC, precision, scale, bRightPadded);
    }
    break;

  case SDB_CLP_LOGICAL:
    field = sdb_field_new(cfl_str_getPtr(info->name),
                          cfl_str_getLength(info->name), NULL, 0,
                          SDB_FIELD_DATA, info->clpType, 1, 0, bRightPadded);
    break;

  case SDB_CLP_DATE:
  case SDB_CLP_TIMESTAMP:
    field = sdb_field_new(cfl_str_getPtr(info->name),
                          cfl_str_getLength(info->name), NULL, 0,
                          SDB_FIELD_DATA, info->clpType, 8, 0, bRightPadded);
    break;

  case SDB_CLP_ROWID:
  case SDB_CLP_CURSOR:
  case SDB_CLP_INTEGER:
  case SDB_CLP_BIGINT:
  case SDB_CLP_FLOAT:
  case SDB_CLP_DOUBLE:
    field = sdb_field_new(cfl_str_getPtr(info->name),
                          cfl_str_getLength(info->name), NULL, 0,
                          SDB_FIELD_DATA, info->clpType, 0, 0, bRightPadded);
    break;

  case SDB_CLP_IMAGE:
  case SDB_CLP_CLOB:
  case SDB_CLP_BLOB:
  case SDB_CLP_MEMO_LONG:
  case SDB_CLP_LONG_RAW:
    field = sdb_field_new(cfl_str_getPtr(info->name),
                          cfl_str_getLength(info->name), NULL, 0,
                          SDB_FIELD_DATA, info->clpType, 10, 0, bRightPadded);
    break;

  default:
    sdb_thread_setError(SDB_ERROR_TYPE_SDB, SDB_ERROR_INVALID_DATATYPE,
                        "Invalid datatype");
    return CFL_FALSE;
  }
  sdb_table_addField(table, field);
  return field != NULL;
}

static SDB_TABLEP openQuery(SDB_STATEMENTP pStmt, CFL_BOOL bRightPadded) {
  CFL_UINT32 numCols;
  CFL_UINT64 affectedRows;
  SDB_TABLEP table;
  CFL_UINT32 i;
  SDB_QUERY_COL_INFO info;

  SDB_LOG_DEBUG(("openQuery. type=%u", sdb_stmt_getType(pStmt)));
  if (sdb_stmt_getType(pStmt) != SDB_STMT_QUERY) {
    sdb_thread_setError(SDB_ERROR_TYPE_SDB, SDB_ERROR_EXECUTING_STMT,
                        "Statement is not a query");
    return NULL;
  }
  if (pStmt->isCursor) {
    if (pStmt->execCount == 0) {
      pStmt->execCount = 1;
    } else {
      sdb_thread_setError(SDB_ERROR_TYPE_SDB, SDB_ERROR_EXECUTING_STMT,
                          "Cursor cannot be reopened");
      return NULL;
    }
  } else if (!sdb_stmt_execute(pStmt, &affectedRows)) {
    return NULL;
  }
  numCols = sdb_stmt_getNumCols(pStmt);
  table = sdb_table_new(pStmt->connection->schema, "queryTab", 8, NULL, 0);
  sdb_queryInfo_init(&info);
  for (i = 1; i <= numCols; i++) {
    if (!sdb_stmt_getQueryInfo(pStmt, i, &info) ||
        !addTableColumn(table, &info, pStmt->precision, pStmt->scale,
                        bRightPadded)) {
      sdb_table_free(table);
      return NULL;
    }
  }
  sdb_queryInfo_free(&info);
  return table;
}

SDB_TABLEP sdb_api_openQueryStmt(SDB_AREAP pSDBArea, SDB_STATEMENTP pStatement,
                                 CFL_BOOL bRightPadded) {
  SDB_TABLEP table;

  ENTER_FUN_NAME("sdb_api_openQueryStmt");
  releaseThreadUnusedStatements(pSDBArea, 0xFF);
  table = openQuery(pStatement, bRightPadded);
  time(&pStatement->lastUseTime);
  if (table) {
    pSDBArea->isQuery = CFL_TRUE;
    pSDBArea->pCurrentCommand = NULL;
    pSDBArea->pStatement = pStatement;
  }
  RETURN table;
}

CFL_BOOL sdb_api_tableFromRecno(SDB_AREAP pSDBArea, HB_BYTE uiSort,
                                CFL_BOOL bNextRecno) {
  PHB_ITEM pRecId;
  CFL_BOOL bSuccess = CFL_TRUE;
  HB_BOOL bEof;

  SDB_LOG_DEBUG(
      ("sdb_api_tableFromRecno(%s,%d)", sdb_area_getAlias(pSDBArea), uiSort));
  if (SELF_EOF((AREAP)pSDBArea, &bEof) == HB_FAILURE) {
    return CFL_FALSE;
  }
  if (bEof) {
    if (uiSort == SDB_SORT_DESC) {
      bSuccess = sdb_rdd_goBottom(pSDBArea, CFL_FALSE) == HB_SUCCESS;
    } else {
      bSuccess = CFL_FALSE;
    }
  } else {
    pRecId = hb_itemNew(NULL);
#ifdef __XHB__
    SELF_RECNO((AREAP)pSDBArea, pRecId);
#else
    SELF_RECID((AREAP)pSDBArea, pRecId);
#endif
    if (!bNextRecno) {
      if (uiSort == SDB_SORT_DESC) {
        sdb_util_itemPutRecno(pRecId, hb_itemGetNLL(pRecId) + 1);
      } else {
        sdb_util_itemPutRecno(pRecId, hb_itemGetNLL(pRecId) - 1);
      }
    }
    if (pSDBArea->uiOrder > 0) {
      SDB_INDEXP index = sdb_area_getCurrentOrder(pSDBArea);
      PHB_ITEM pIndexValue = hb_itemNew(NULL);
      if (index->field->fieldType == SDB_FIELD_DATA_INDEX ||
          (pSDBArea->pCurrentCommand != NULL &&
           pSDBArea->pCurrentCommand->uiOrder == pSDBArea->uiOrder)) {
        sdb_record_getValue(pSDBArea->record, index->field, pIndexValue,
                            CFL_FALSE);
      } else {
        pIndexValue = sdb_api_queryFieldValue(pSDBArea, index->field, pRecId,
                                              pIndexValue);
      }
      if (!sdb_api_tableNextRecord(pSDBArea, pRecId, pIndexValue, uiSort)) {
        sdb_api_genErrorFromSDBError(pSDBArea->connection,
                                     sdb_area_getAlias(pSDBArea), EF_NONE,
                                     sdb_thread_getLastError(), NULL, NULL);
        bSuccess = CFL_FALSE;
      }
      hb_itemRelease(pIndexValue);
    } else if (!sdb_api_tableNextRecord(pSDBArea, pRecId, NULL, uiSort)) {
      sdb_api_genErrorFromSDBError(pSDBArea->connection,
                                   sdb_area_getAlias(pSDBArea), EF_NONE,
                                   sdb_thread_getLastError(), NULL, NULL);
      bSuccess = CFL_FALSE;
    }
    pSDBArea->isValidBuffer = CFL_FALSE;
    hb_itemRelease(pRecId);
  }
  return bSuccess;
}

CFL_BOOL sdb_api_areaQueryNextKey(SDB_AREAP pSDBArea, HB_BYTE uiSort) {
  SDB_INDEXP index = sdb_area_getCurrentOrder(pSDBArea);
  PHB_ITEM pKeyValue = hb_itemNew(NULL);
  CFL_BOOL bSuccess = CFL_TRUE;

  SDB_LOG_DEBUG(
      ("sdb_api_areaQueryNextKey(%s,%d)", sdb_area_getAlias(pSDBArea), uiSort));
  if (index->field->fieldType == SDB_FIELD_DATA_INDEX ||
      (pSDBArea->pCurrentCommand != NULL &&
       pSDBArea->pCurrentCommand->uiOrder == pSDBArea->uiOrder)) {
    sdb_record_getValue(pSDBArea->record, index->field, pKeyValue, CFL_FALSE);
  } else {
    PHB_ITEM pRecId = hb_itemNew(NULL);
#ifdef __XHB__
    SELF_RECNO((AREAP)pSDBArea, pRecId);
#else
    SELF_RECID((AREAP)pSDBArea, pRecId);
#endif
    sdb_api_queryFieldValue(pSDBArea, index->field, pRecId, pKeyValue);
    hb_itemRelease(pRecId);
  }
  if (!sdb_api_tableNextKey(pSDBArea, pKeyValue, uiSort)) {
    sdb_api_genErrorFromSDBError(pSDBArea->connection,
                                 sdb_area_getAlias(pSDBArea), EF_NONE,
                                 sdb_thread_getLastError(), NULL, NULL);
    bSuccess = CFL_FALSE;
  }
  hb_itemRelease(pKeyValue);
  pSDBArea->isValidBuffer = CFL_FALSE;
  return bSuccess;
}

static CFL_BOOL queryMoreRecords(SDB_AREAP pSDBArea, CFL_BOOL bNextRecno,
                                 CFL_BOOL bNextKey) {
  if (pSDBArea->pCurrentCommand != NULL) {
    if (bNextKey) {
      return sdb_api_areaQueryNextKey(pSDBArea,
                                      pSDBArea->pCurrentCommand->uiSort);
    } else {
      return sdb_api_tableFromRecno(pSDBArea, pSDBArea->pCurrentCommand->uiSort,
                                    bNextRecno);
    }
  } else {
    return CFL_FALSE;
  }
}

CFL_BOOL sdb_api_areaFetchNext(SDB_AREAP pSDBArea, CFL_BOOL *fError) {
  CFL_BOOL bFetched;
  CFL_UINT32 errorCount = sdb_thread_errorCount();

  *fError = CFL_FALSE;
  if (pSDBArea->isQuery) {
    if (pSDBArea->pStatement != NULL && pSDBArea->pStatement->handle != NULL) {
      bFetched = sdb_stmt_fetchNext(pSDBArea->pStatement, CFL_TRUE);
    } else {
      sdb_thread_setError(SDB_ERROR_TYPE_SDB, SDB_ERROR_UNEXPECTED,
                          "Invalid statement");
      *fError = CFL_TRUE;
      bFetched = CFL_FALSE;
    }
  } else {
    if ((pSDBArea->pStatement == NULL ||
         pSDBArea->pStatement->handle == NULL) &&
        !queryMoreRecords(pSDBArea, CFL_TRUE, CFL_FALSE)) {
      *fError = CFL_TRUE;
      bFetched = CFL_FALSE;
    } else if (sdb_stmt_fetchNext(pSDBArea->pStatement, CFL_TRUE)) {
      bFetched = CFL_TRUE;
    } else if (IS_QUERY_MORE(pSDBArea)) {
      if (!sdb_thread_hasNewErrors(errorCount)) {
        if (queryMoreRecords(pSDBArea, CFL_TRUE, CFL_TRUE)) {
          bFetched = sdb_stmt_fetchNext(pSDBArea->pStatement, CFL_TRUE);
          *fError = sdb_thread_hasNewErrors(errorCount);
        } else {
          bFetched = CFL_FALSE;
          *fError = CFL_TRUE;
        }
      } else {
        bFetched = CFL_FALSE;
        *fError = CFL_TRUE;
      }
    } else {
      bFetched = CFL_FALSE;
    }
  }
  SDB_LOG_DEBUG(("sdb_api_areaFetchNext: fetched=%s", BOOL_STR(bFetched)));
  return bFetched;
}

CFL_BOOL sdb_api_areaGetValue(SDB_AREAP pSDBArea, CFL_UINT16 pos,
                              SDB_FIELDP pField, CFL_BOOL bReadLarge) {
  SDB_STATEMENTP pStatement = pSDBArea->pStatement;

  SDB_LOG_TRACE(("sdb_api_areaGetValue: name=%s, field pos=%u query pos=%u",
                 cfl_str_getPtr(pField->clpName), pField->tablePos, pos));
  // intentionally not cleaning error because performance
  if (!pSDBArea->isQuery &&
      (pSDBArea->pCurrentCommand == NULL ||
       pSDBArea->pCurrentCommand->pStmt == NULL) &&
      !queryMoreRecords(pSDBArea, CFL_FALSE, CFL_FALSE)) {
    return CFL_FALSE;
  }
  time(&pStatement->lastUseTime);
  pStatement = pSDBArea->pStatement;
  if (bReadLarge) {
    return pStatement->connection->dbAPI->getMemoValue(
        pStatement, pos, pSDBArea->table, pField, pSDBArea->record,
        sdb_record_getInt64(pSDBArea->record, pSDBArea->table->pkField));
  } else {
    return pStatement->connection->dbAPI->getFieldValue(pStatement, pos, pField,
                                                        pSDBArea->record);
  }
}

static CFL_BOOL insertMemos(SDB_AREAP pSDBArea, CFL_LISTP memoFields) {
  CFL_BOOL bResult = CFL_TRUE;
  CFL_UINT32 i;
  SDB_STATEMENTP pStmt;
  CFL_SQL_BUILDERP b;
  CFL_SQL_INSERTP insert;
  CFL_STRP sql;
  CFL_STRP tableName;
  CFL_UINT32 errorCount = sdb_thread_errorCount();
  CFL_UINT64 ulAffectedRows;
  CFL_UINT32 len = cfl_list_length(memoFields);
  PHB_ITEM pItem = hb_itemNew(NULL);

  ENTER_FUN_NAME("insertMemos");
  b = pSDBArea->connection->dbAPI->sqlBuilder();
  tableName = cfl_str_toUpper(cfl_str_newStr(pSDBArea->table->dbName));
  CFL_STR_APPEND_CONST(tableName, "_MEMO");
  insert = b->insert();
  insert->into(insert,
               b->quali_id(pSDBArea->table->dbSchema->name, tableName, NULL));
  insert->columns(insert, b->id(pSDBArea->table->pkField->dbName),
                  b->c_id("COL_NAME"), b->c_id("MEMO"), NULL);
  insert->values(insert, b->c_param("R"), b->c_param("C"), b->c_param("M"),
                 NULL);
  sql = cfl_str_new(512);
  CFL_SQL_TO_STRING(insert, sql);
  pStmt = sdb_connection_prepareStatement(pSDBArea->connection, sql);
  cfl_str_free(sql);
  cfl_str_free(tableName);
  CFL_SQL_FREE(insert);
  if (pStmt == NULL) {
    hb_itemRelease(pItem);
    return CFL_FALSE;
  } else if (!sdb_thread_hasNewErrors(errorCount)) {
    sdb_stmt_setValueByPos(
        pStmt, 1,
        sdb_area_getFieldValue(pSDBArea, pSDBArea->table->pkField, pItem),
        CFL_FALSE);
    for (i = 0; i < len; i++) {
      SDB_WAFIELDP waField = (SDB_WAFIELDP)cfl_list_get(memoFields, i);
      SDB_PARAMP param;
      sdb_stmt_setCharLenByPos(
          pStmt, 2, cfl_str_getPtr(waField->field->clpName),
          cfl_str_getLength(waField->field->clpName), CFL_FALSE);
      param = sdb_stmt_setValueByPos(
          pStmt, 3, sdb_area_getFieldValue(pSDBArea, waField->field, pItem),
          CFL_FALSE);
      sdb_param_setType(param, SDB_CLP_MEMO_LONG);
      if (!sdb_stmt_execute(pStmt, &ulAffectedRows)) {
        bResult = CFL_FALSE;
      }
    }
  }
  hb_itemRelease(pItem);
  sdb_stmt_free(pStmt);
  RETURN bResult;
}

static void prepareInsert(SDB_AREAP pSDBArea) {
  CFL_SQL_BUILDERP b = pSDBArea->connection->dbAPI->sqlBuilder();
  CFL_SQL_INSERTP insert = b->insert();
  CFL_STRP sql = cfl_str_new(2048);
  CFL_UINT32 i;
  CFL_UINT32 len;
  CFL_UINT32 ulParCount = 1;
  CFL_LISTP returnFields = cfl_list_new(3);
  char parName[10];

  insert->into(insert, b->quali_id(pSDBArea->table->dbSchema->name,
                                   pSDBArea->table->dbName, NULL));
  for (i = 0; i < pSDBArea->fieldsCount; i++) {
    SDB_WAFIELDP waField = sdb_area_getFieldByPos(pSDBArea, i);
    if (!IS_SERVER_SET_INSERT(waField->field)) {
      if (waField->field->dbExpression != NULL) {
        insert->columns(insert, b->id(waField->field->dbName), NULL);
        insert->values(insert, b->custom(waField->field->dbExpression), NULL);
        cfl_list_add(returnFields, waField);
      } else {
        sprintf(parName, "F%u", ulParCount++);
        insert->columns(insert, b->id(waField->field->dbName), NULL);
        insert->values(insert, b->c_param(parName), NULL);
      }
    } else if (!IS_INDEX_EXPR_FIELD(waField->field)) {
      cfl_list_add(returnFields, waField);
    }
  }
  len = cfl_list_length(returnFields);
  for (i = 0; i < len; i++) {
    SDB_WAFIELDP waField = cfl_list_get(returnFields, i);
    sprintf(parName, "F%u", ulParCount++);
    insert->returning(insert, b->id(waField->field->dbName),
                      b->c_param(parName));
  }
  CFL_SQL_TO_STRING(insert, sql);
  pSDBArea->commands[SDB_CMD_INSERT].pStmt =
      sdb_connection_prepareStatement(pSDBArea->connection, sql);
  cfl_str_free(sql);
  CFL_SQL_FREE(insert);
  cfl_list_free(returnFields);
}

static SDB_PARAMP paramsInsert(SDB_AREAP pSDBArea, CFL_LISTP returnFields,
                               CFL_LISTP returnParams, CFL_LISTP memoFields) {
  SDB_PARAMP pkParam = NULL;
  CFL_UINT32 i;
  CFL_UINT32 len;
  CFL_UINT32 paramPos = 1;
  SDB_STATEMENTP pStmt = pSDBArea->commands[SDB_CMD_INSERT].pStmt;
  PHB_ITEM pItem = hb_itemNew(NULL);

  for (i = 0; i < pSDBArea->fieldsCount; i++) {
    SDB_WAFIELDP waField = sdb_area_getFieldByPos(pSDBArea, i);
    if (!IS_SERVER_SET_INSERT(waField->field)) {
      if (waField->field->dbExpression != NULL) {
        cfl_list_add(returnFields, waField);
      } else if (waField->field->fieldType == SDB_FIELD_PK) {
        tableNextPK(pSDBArea->connection, pSDBArea->table, CFL_FALSE, pItem);
        pkParam = sdb_stmt_setValueByPos(pStmt, paramPos++, pItem, CFL_FALSE);
      } else if (waField->field->clpType == SDB_CLP_MEMO_LONG) {
        sdb_area_getFieldValue(pSDBArea, waField->field, pItem);
        if (hb_itemType(pItem) & (HB_IT_STRING | HB_IT_MEMO)) {
          sdb_stmt_setUInt64ByPos(pStmt, paramPos++,
                                  (CFL_UINT64)hb_itemGetCLen(pItem), CFL_FALSE);
          cfl_list_add(memoFields, waField);
        } else {
          sdb_stmt_setUInt64ByPos(pStmt, paramPos++,
                                  (CFL_UINT64)hb_itemGetNLL(pItem), CFL_FALSE);
        }
      } else {
        sdb_area_getFieldValue(pSDBArea, waField->field, pItem);
        setStmtValueForField(pStmt, paramPos++, waField->field, pItem, CFL_TRUE,
                             CFL_FALSE);
      }
    } else if (!IS_INDEX_EXPR_FIELD(waField->field)) {
      cfl_list_add(returnFields, waField);
    }
  }
  len = cfl_list_length(returnFields);
  for (i = 0; i < len; i++) {
    SDB_WAFIELDP waField = cfl_list_get(returnFields, i);
    cfl_list_add(returnParams,
                 setStmtValueForField(pStmt, paramPos++, waField->field, NULL,
                                      CFL_FALSE, CFL_TRUE));
  }
  hb_itemRelease(pItem);
  return pkParam;
}

static void returningFields(SDB_AREAP pSDBArea, CFL_LISTP returnFields,
                            CFL_LISTP returnParams) {
  CFL_UINT32 i;
  CFL_UINT32 len = cfl_list_length(returnFields);
  for (i = 0; i < len; i++) {
    SDB_WAFIELDP waField = (SDB_WAFIELDP)cfl_list_get(returnFields, i);
    SDB_PARAMP param = (SDB_PARAMP)cfl_list_get(returnParams, i);
    sdb_area_setFieldValue(pSDBArea, waField->field, sdb_param_getItem(param));
    if (IS_PK_FIELD(waField->field) &&
        sdb_table_maxPK(pSDBArea->table) <
            sdb_record_getInt64(pSDBArea->record, pSDBArea->table->pkField)) {
      sdb_table_cacheNextPK(
          pSDBArea->table,
          sdb_record_getInt64(pSDBArea->record, pSDBArea->table->pkField) + 1,
          sdb_table_getPKCacheSize(t));
    }
  }
}

CFL_BOOL sdb_api_insertRecord(SDB_AREAP pSDBArea) {
  SDB_STATEMENTP pStmt;
  SDB_PARAMP pkParam;
  CFL_UINT64 ulAffectedRows;
  CFL_LISTP returnFields;
  CFL_LISTP returnParams;
  CFL_LISTP memoFields;
  CFL_UINT32 errorCount = sdb_thread_errorCount();

  ENTER_FUN_NAME("sdb_api_insertRecord");

  releaseThreadUnusedStatements(pSDBArea, SDB_CMD_INSERT);
  LOCK_AREA(pSDBArea);
  if (pSDBArea->commands[SDB_CMD_INSERT].pStmt == NULL) {
    prepareInsert(pSDBArea);
  }
  pStmt = pSDBArea->commands[SDB_CMD_INSERT].pStmt;
  returnFields = cfl_list_new(3);
  returnParams = cfl_list_new(3);
  memoFields = cfl_list_new(3);
  pkParam = paramsInsert(pSDBArea, returnFields, returnParams, memoFields);
  time(&pStmt->lastUseTime);
  UNLOCK_AREA(pSDBArea);
  if (!sdb_thread_hasNewErrors(errorCount)) {
    int tryCount = 1;
    CFL_BOOL tryAgain = CFL_FALSE;
    do {
      sdb_stmt_execute(pStmt, &ulAffectedRows);
      if (!sdb_thread_hasNewErrors(errorCount)) {
        if (cfl_list_length(returnFields) > 0) {
          returningFields(pSDBArea, returnFields, returnParams);
        }
        if (cfl_list_length(memoFields) > 0) {
          insertMemos(pSDBArea, memoFields);
        }
        /* If control of RECNO is on the table and occurs error of PK violation,
         * retry with new RECNO */
      } else if (IS_RETRY_INSERT(pSDBArea, pkParam, tryCount)) {
        sdb_thread_cleanError();
        errorCount = sdb_thread_errorCount();
        tryAgain = tableNextPK(pSDBArea->connection, pSDBArea->table,
                               (tryCount % SDB_RECNO_REFRESH_ATTEMPS) == 0,
                               sdb_param_getItem(pkParam));
      } else {
        sdb_record_setInt64(pSDBArea->record, pSDBArea->table->pkField, 0);
      }
    } while (tryAgain);
  }
  cfl_list_free(memoFields);
  cfl_list_free(returnFields);
  cfl_list_free(returnParams);
  RETURN !sdb_thread_hasNewErrors(errorCount);
}

CFL_BOOL updateMemos(SDB_AREAP pSDBArea, CFL_LISTP memoFields) {
  CFL_BOOL bResult = CFL_TRUE;
  CFL_UINT32 i;
  CFL_UINT32 len = cfl_list_length(memoFields);
  SDB_STATEMENTP pStmt;
  CFL_SQL_BUILDERP b;
  CFL_SQL_UPDATEP update;
  CFL_STRP sql;
  CFL_STRP tableName;
  CFL_LISTP memosInsert;
  CFL_UINT32 errorCount = sdb_thread_errorCount();
  CFL_UINT64 ulAffectedRows;
  PHB_ITEM pItem = hb_itemNew(NULL);

  ENTER_FUN_NAME("updateMemos");
  b = pSDBArea->connection->dbAPI->sqlBuilder();
  tableName = cfl_str_toUpper(cfl_str_newStr(pSDBArea->table->dbName));
  CFL_STR_APPEND_CONST(tableName, "_MEMO");
  sql = cfl_str_new(512);
  update = b->update();
  update->table(update,
                b->quali_id(pSDBArea->table->dbSchema->name, tableName, NULL));
  update->set(update, b->c_id("MEMO"), b->c_param("M"));
  update->where(update,
                b->and (b->equal(b->id(pSDBArea->table->pkField->dbName),
                                 b->c_param("R")),
                        b->equal(b->c_id("COL_NAME"), b->c_param("C"))));
  CFL_SQL_TO_STRING(update, sql);
  pStmt = sdb_connection_prepareStatement(pSDBArea->connection, sql);
  cfl_str_free(sql);
  cfl_str_free(tableName);
  CFL_SQL_FREE(update);
  if (pStmt == NULL) {
    hb_itemRelease(pItem);
    return CFL_FALSE;
  } else if (!sdb_thread_hasNewErrors(errorCount)) {
    memosInsert = cfl_list_new(3);
    sdb_area_getFieldValue(pSDBArea, pSDBArea->table->pkField, pItem);
    sdb_stmt_setValueByPos(pStmt, 2, pItem, CFL_FALSE);
    for (i = 0; i < len; i++) {
      SDB_WAFIELDP waField = (SDB_WAFIELDP)cfl_list_get(memoFields, i);
      SDB_PARAMP param;
      sdb_stmt_setCharLenByPos(
          pStmt, 3, cfl_str_getPtr(waField->field->clpName),
          cfl_str_getLength(waField->field->clpName), CFL_FALSE);
      sdb_area_getFieldValue(pSDBArea, waField->field, pItem);
      param = sdb_stmt_setValueByPos(pStmt, 1, pItem, CFL_FALSE);
      sdb_param_setType(param, SDB_CLP_MEMO_LONG);
      if (sdb_stmt_execute(pStmt, &ulAffectedRows)) {
        if (ulAffectedRows == 0) {
          cfl_list_clear(memosInsert);
          cfl_list_add(memosInsert, waField);
          if (insertMemos(pSDBArea, memosInsert)) {
            bResult = CFL_FALSE;
          }
        }
      } else {
        bResult = CFL_FALSE;
      }
    }
    cfl_list_free(memosInsert);
  }
  hb_itemRelease(pItem);
  sdb_stmt_free(pStmt);
  RETURN bResult;
}

static void prepareUpdate(SDB_AREAP pSDBArea, CFL_UINT8 iCommand) {
  CFL_SQL_BUILDERP b = pSDBArea->connection->dbAPI->sqlBuilder();
  CFL_SQL_UPDATEP update = b->update();
  CFL_SQLP condition;
  CFL_STRP sql = cfl_str_new(2048);
  CFL_UINT32 ulParCount = 1;
  char parName[10];
  CFL_UINT32 ulIndex;
  CFL_LISTP returnFields;
  CFL_UINT32 len;

  returnFields = cfl_list_new(3);
  update->table(update, b->quali_id(pSDBArea->table->dbSchema->name,
                                    pSDBArea->table->dbName, NULL));
  for (ulIndex = 0; ulIndex < pSDBArea->fieldsCount; ulIndex++) {
    SDB_WAFIELDP waField = sdb_area_getFieldByPos(pSDBArea, ulIndex);
    if (!waField->isContext) {
      if (!IS_SERVER_SET_UPDATE(waField->field)) {
        sprintf(parName, "F%u", ulParCount++);
        update->set(update, b->id(waField->field->dbName), b->c_param(parName));
      } else if (IS_DATA_FIELD(waField->field)) {
        cfl_list_add(returnFields, waField);
      }
    }
  }
  condition =
      b->equal(b->id(pSDBArea->table->pkField->dbName), b->c_param("R"));
  if (iCommand == SDB_CMD_UPDATE_RECNO) {
    condition = b->and (condition, contexCondition(pSDBArea));
  }
  update->where(update, condition);
  len = cfl_list_length(returnFields);
  for (ulIndex = 0; ulIndex < len; ulIndex++) {
    SDB_WAFIELDP waField = (SDB_WAFIELDP)cfl_list_get(returnFields, ulIndex);
    sprintf(parName, "F%u", ulParCount++);
    update->returning(update, b->id(waField->field->dbName),
                      b->c_param(parName));
  }
  CFL_SQL_TO_STRING(update, sql);
  pSDBArea->commands[iCommand].pStmt =
      sdb_connection_prepareStatement(pSDBArea->connection, sql);
  cfl_str_free(sql);
  CFL_SQL_FREE(update);
  cfl_list_free(returnFields);
}

static void paramsUpdate(SDB_AREAP pSDBArea, CFL_UINT8 iCommand,
                         CFL_LISTP returnFields, CFL_LISTP returnParams,
                         CFL_LISTP memoFields) {
  PHB_ITEM pItem = hb_itemNew(NULL);
  CFL_UINT32 paramPos = 1;
  CFL_UINT32 ulIndex;
  CFL_UINT32 len;
  SDB_STATEMENTP pStmt;

  pStmt = pSDBArea->commands[iCommand].pStmt;
  for (ulIndex = 0; ulIndex < pSDBArea->fieldsCount; ulIndex++) {
    SDB_WAFIELDP waField = sdb_area_getFieldByPos(pSDBArea, ulIndex);
    if (!waField->isContext) {
      if (!IS_SERVER_SET_UPDATE(waField->field)) {
        if (waField->field->clpType == SDB_CLP_MEMO_LONG) {
          sdb_area_getFieldValue(pSDBArea, waField->field, pItem);
          if (hb_itemType(pItem) & (HB_IT_STRING | HB_IT_MEMO)) {
            cfl_list_add(memoFields, waField);
            sdb_stmt_setUInt64ByPos(pStmt, paramPos++,
                                    (CFL_UINT64)hb_itemGetCLen(pItem),
                                    CFL_FALSE);
          } else {
            sdb_stmt_setUInt64ByPos(
                pStmt, paramPos++, (CFL_UINT64)hb_itemGetNLL(pItem), CFL_FALSE);
          }
        } else {
          sdb_area_getFieldValue(pSDBArea, waField->field, pItem);
          setStmtValueForField(pStmt, paramPos++, waField->field, pItem,
                               CFL_FALSE, CFL_FALSE);
        }
      } else if (IS_DATA_FIELD(waField->field)) {
        cfl_list_add(returnFields, waField);
      }
    }
  }
  if (iCommand == SDB_CMD_UPDATE_RECNO) {
    sdb_area_getFieldValue(pSDBArea, pSDBArea->table->pkField, pItem);
    setStmtValueForField(pStmt, paramPos++, pSDBArea->table->pkField, pItem,
                         CFL_FALSE, CFL_FALSE);
    setContexParams(pSDBArea, pSDBArea->pStatement, &paramPos);
  } else {
    sdb_area_getFieldValue(pSDBArea, pSDBArea->table->rowIdField, pItem);
    setStmtValueForField(pStmt, paramPos++, pSDBArea->table->rowIdField, pItem,
                         CFL_FALSE, CFL_FALSE);
  }
  len = cfl_list_length(returnFields);
  for (ulIndex = 0; ulIndex < len; ulIndex++) {
    SDB_WAFIELDP waField = (SDB_WAFIELDP)cfl_list_get(returnFields, ulIndex);
    cfl_list_add(returnParams,
                 setStmtValueForField(pStmt, paramPos++, waField->field, NULL,
                                      CFL_FALSE, CFL_TRUE));
  }
  hb_itemRelease(pItem);
}

CFL_BOOL sdb_api_updateRecord(SDB_AREAP pSDBArea) {
  CFL_UINT8 iCommand;
  CFL_UINT64 ulAffectedRows;
  CFL_UINT32 errorCount = sdb_thread_errorCount();
  CFL_LISTP returnFields;
  CFL_LISTP returnParams;
  CFL_LISTP memoFields;

  ENTER_FUN_NAME("sdb_api_updateRecord");

  iCommand =
      pSDBArea->connection->isUseRowId && pSDBArea->table->rowIdField != NULL
          ? SDB_CMD_UPDATE_ROWID
          : SDB_CMD_UPDATE_RECNO;
  releaseThreadUnusedStatements(pSDBArea, iCommand);
  LOCK_AREA(pSDBArea);
  if (pSDBArea->commands[iCommand].pStmt == NULL) {
    prepareUpdate(pSDBArea, iCommand);
  }
  returnFields = cfl_list_new(3);
  returnParams = cfl_list_new(3);
  memoFields = cfl_list_new(3);
  paramsUpdate(pSDBArea, iCommand, returnFields, returnParams, memoFields);
  time(&pSDBArea->commands[iCommand].pStmt->lastUseTime);
  UNLOCK_AREA(pSDBArea);
  sdb_stmt_execute(pSDBArea->commands[iCommand].pStmt, &ulAffectedRows);
  if (!sdb_thread_hasNewErrors(errorCount)) {
    if (cfl_list_length(returnFields) > 0) {
      returningFields(pSDBArea, returnFields, returnParams);
    }
    if (cfl_list_length(memoFields) > 0) {
      updateMemos(pSDBArea, memoFields);
    }
  } else {
    sdb_record_setInt64(pSDBArea->record, pSDBArea->table->pkField, 0);
  }
  cfl_list_free(memoFields);
  cfl_list_free(returnFields);
  cfl_list_free(returnParams);
  RETURN !sdb_thread_hasNewErrors(errorCount);
}

CFL_BOOL sdb_api_deleteRecords(SDB_AREAP pSDBArea, CFL_BOOL bDeletedAll) {
  CFL_UINT32 errorCount = sdb_thread_errorCount();
  CFL_SQL_BUILDERP b = pSDBArea->connection->dbAPI->sqlBuilder();
  CFL_SQL_DELETEP delete = b->delete ();
  SDB_STATEMENTP pStmt;
  CFL_STRP sql = cfl_str_new(256);
  CFL_UINT64 ulAffectedRows;

  ENTER_FUN_NAME("sdb_api_deleteRecords");
  delete->from(delete, b->quali_id(pSDBArea->table->dbSchema->name,
                                   pSDBArea->table->dbName, NULL));
  if (!bDeletedAll) {
    delete->where(delete, b->equal(b->id(pSDBArea->table->delField->dbName),
                                   b->format("'Y'")));
  }
  CFL_SQL_TO_STRING(delete, sql);
  pStmt = sdb_connection_prepareStatement(pSDBArea->connection, sql);
  cfl_str_free(sql);
  CFL_SQL_FREE(delete);
  if (pStmt != NULL) {
    sdb_stmt_execute(pStmt, &ulAffectedRows);
    sdb_stmt_free(pStmt);
  }
  RETURN !sdb_thread_hasNewErrors(errorCount);
}

SDB_RECNO sdb_api_tableMaxRecno(SDB_AREAP pSDBArea) {
  SDB_DICTIONARYP dict =
      sdb_schema_getDict(pSDBArea->table->clpSchema, pSDBArea->connection);
  SDB_RECNO maxRecno = 0;

  SDB_LOG_DEBUG(
      ("sdb_api_tableMaxRecno: alias=%s", sdb_area_getAlias(pSDBArea)));
  if (dict != NULL) {
    CFL_UINT32 errorCount = sdb_thread_errorCount();
    maxRecno = dict->maxRecno(pSDBArea->connection, pSDBArea->table, CFL_FALSE);
    if (sdb_thread_hasNewErrors(errorCount)) {
      maxRecno = 0;
    }
  } else {
    sdb_api_genError(NULL, SDB_ERROR_TYPE_SDB, SDB_ERROR_DICT_NOT_FOUND, NULL,
                     EF_NONE, "Dictionary not found in schema",
                     cfl_str_getPtr(pSDBArea->table->clpSchema->name), NULL);
  }
  return maxRecno;
}

PHB_ITEM sdb_api_queryFieldValue(SDB_AREAP pSDBArea, SDB_FIELDP pField,
                                 PHB_ITEM pRecId, PHB_ITEM pFieldValue) {
  CFL_SQL_BUILDERP b = pSDBArea->connection->dbAPI->sqlBuilder();
  CFL_SQL_QUERYP query = b->query();
  CFL_STRP sql = cfl_str_new(128);
  SDB_STATEMENTP pStmt;
  CFL_UINT64 ulAffectedRows;
  CFL_SQLP condition;
  CFL_UINT32 paramPos = 1;

  SDB_LOG_DEBUG(("sdb_api_queryFieldValue: alias=%s field=%s recno=%s",
                 sdb_area_getAlias(pSDBArea), cfl_str_getPtr(pField->dbName),
                 ITEM_STR(pRecId)));
  query->select(query, b->id(pField->dbName), NULL);
  query->from(query,
              b->quali_id(pSDBArea->table->dbSchema->name,
                          pSDBArea->table->dbName, NULL),
              NULL);
  condition =
      b->equal(b->id(pSDBArea->table->pkField->dbName), b->c_param("R"));
  condition = b->and (condition, contexCondition(pSDBArea));
  query->where(query, condition);
  CFL_SQL_TO_STRING(query, sql);
  pStmt = sdb_connection_prepareStatement(pSDBArea->connection, sql);
  cfl_str_free(sql);
  CFL_SQL_FREE(query);
  if (pStmt != NULL) {
    sdb_stmt_setValueByPos(pStmt, paramPos++, pRecId, CFL_FALSE);
    setContexParams(pSDBArea, pStmt, &paramPos);
    if (sdb_stmt_execute(pStmt, &ulAffectedRows) &&
        sdb_stmt_fetchNext(pStmt, CFL_FALSE)) {
      pFieldValue = sdb_stmt_getQueryValue(pStmt, 1, pFieldValue);
    } else if (pFieldValue != NULL) {
      hb_itemClear(pFieldValue);
    }
    sdb_stmt_free(pStmt);
  } else if (pFieldValue != NULL) {
    hb_itemClear(pFieldValue);
  }
  return pFieldValue;
}

/* TODO: trazer a logica para a API generica */
CFL_BOOL sdb_api_tableDropIndex(SDB_CONNECTIONP connection, SDB_TABLEP table,
                                SDB_INDEXP index, CFL_BOOL physically) {
  SDB_INDEXP foundIndex;
  CFL_UINT32 errorCount = sdb_thread_errorCount();

  ENTER_FUN_NAME("sdb_api_tableDropIndex");
  if (connection->dbAPI->tableDropIndex(connection, table, index, physically)) {
    foundIndex =
        sdb_table_delIndex(table, cfl_str_getPtr(index->field->clpIndexName));
    if (foundIndex != NULL) {
      if (foundIndex->field->fieldType == SDB_FIELD_INDEX) {
        SDB_FIELDP field = sdb_table_delField(
            table, cfl_str_getPtr(foundIndex->field->clpName));
        if (field != NULL) {
          sdb_field_free(field);
        }
      }
      sdb_index_free(foundIndex);
    }
  }
  RETURN !sdb_thread_hasNewErrors(errorCount);
}

/* TODO: trazer a logica para a API generica */
CFL_BOOL sdb_api_dropIndex(SDB_CONNECTIONP connection, const char *schemaName,
                           const char *tableName, const char *indexName,
                           CFL_BOOL physically) {
  SDB_TABLEP table;
  SDB_INDEXP index;
  CFL_BOOL bSuccess = CFL_FALSE;
  CFL_UINT32 errorCount = sdb_thread_errorCount();

  ENTER_FUN_NAME("sdb_api_dropIndex");
  table = sdb_api_getTable(connection, schemaName, tableName);
  if (table != NULL) {
    index = sdb_table_delIndex(table, indexName);
    if (index != NULL) {
      if (sdb_api_tableDropIndex(connection, table, index, physically) &&
          index->field->fieldType == SDB_FIELD_INDEX && physically) {
        SDB_FIELDP field =
            sdb_table_delField(table, cfl_str_getPtr(index->field->clpName));
        if (field != NULL) {
          sdb_field_free(field);
        }
      }
      sdb_index_free(index);
      bSuccess = CFL_TRUE;
    }
  }
  RETURN bSuccess && !sdb_thread_hasNewErrors(errorCount);
}

/* TODO: trazer a logica para a API generica */
CFL_BOOL sdb_api_dropTable(SDB_CONNECTIONP connection, const char *schemaName,
                           const char *tableName) {
  SDB_TABLEP table;
  CFL_BOOL bSuccess = CFL_FALSE;
  CFL_UINT32 errorCount = sdb_thread_errorCount();

  ENTER_FUN_NAME("sdb_api_dropTable");
  SDB_LOG_DEBUG(("sdb_api_dropTable: table=%s.%s", schemaName, tableName));
  table = sdb_api_getTable(connection, schemaName, tableName);
  if (table != NULL && connection->dbAPI->dropTable(connection, table)) {
    table =
        sdb_schema_delTable(table->clpSchema, cfl_str_getPtr(table->clpName));
    if (table) {
      sdb_table_free(table);
    }
    bSuccess = CFL_TRUE;
  }
  RETURN bSuccess && !sdb_thread_hasNewErrors(errorCount);
}

CFL_BOOL sdb_api_existsIndex(SDB_CONNECTIONP connection, const char *schemaName,
                             const char *tableName, const char *indexName) {
  SDB_SCHEMAP schema;
  SDB_TABLEP table;
  char *tableNameUpper;
  char *indexNameUpper;
  CFL_BOOL bFound;
  SDB_DICTIONARYP dict;

  ENTER_FUN_NAME("sdb_api_existsIndex");
  schema = sdb_database_getCreateSchema(connection->database, schemaName);
  table = sdb_schema_getTable(schema, tableName);
  if (table != NULL && sdb_table_getIndex(table, indexName) != NULL) {
    RETURN CFL_TRUE;
  }
  dict = sdb_schema_getDict(schema, connection);
  if (dict != NULL) {
    tableNameUpper = sdb_util_strnDupUpperTrim(tableName, strlen(tableName));
    indexNameUpper = sdb_util_strnDupUpperTrim(indexName, strlen(indexName));
    bFound =
        dict->existsIndex(connection, schema, tableNameUpper, indexNameUpper);
    SDB_MEM_FREE(tableNameUpper);
    SDB_MEM_FREE(indexNameUpper);
  } else {
    sdb_thread_setError(SDB_ERROR_TYPE_SDB, SDB_ERROR_DICT_NOT_FOUND,
                        "Dictionary not found in schema %s",
                        cfl_str_getPtr(schema->name));
    bFound = CFL_FALSE;
  }
  RETURN bFound;
}

CFL_BOOL sdb_api_existsTable(SDB_CONNECTIONP connection, const char *schemaName,
                             const char *tableName) {
  SDB_SCHEMAP schema;
  char *tableNameUpper;
  CFL_BOOL bFound;
  SDB_DICTIONARYP dict;

  ENTER_FUN_NAME("sdb_api_existsTable");
  schema = sdb_database_getCreateSchema(connection->database, schemaName);
  if (sdb_schema_getTable(schema, tableName) != NULL) {
    RETURN CFL_TRUE;
  }
  dict = sdb_schema_getDict(schema, connection);
  if (dict != NULL) {
    tableNameUpper = sdb_util_strnDupUpperTrim(tableName, strlen(tableName));
    bFound = dict->existsTable(connection, schema, tableName);
    SDB_MEM_FREE(tableNameUpper);
  } else {
    sdb_thread_setError(SDB_ERROR_TYPE_SDB, SDB_ERROR_DICT_NOT_FOUND,
                        "Dictionary not found in schema %s",
                        cfl_str_getPtr(schema->name));
    bFound = CFL_FALSE;
  }
  RETURN bFound;
}

/* TODO: trazer a logica para a API generica */
CFL_BOOL sdb_api_tableAddColumn(SDB_CONNECTIONP conn, SDB_TABLEP table,
                                SDB_FIELDP field) {
  CFL_UINT32 errorCount = sdb_thread_errorCount();

  ENTER_FUN_NAME("sdb_api_tableAddColumn");
  if (field->clpType == SDB_CLP_MEMO_LONG) {
    field->clpType = sdb_thread_getDefaultMemoType(sdb_thread_getData());
  }
  field->order = sdb_table_nextColOrder(table, field);
  if (conn->dbAPI->tableAddColumn(conn, table, field)) {
    sdb_table_addField(table, field);
  }
  RETURN !sdb_thread_hasNewErrors(errorCount);
}

/* TODO: trazer a logica para a API generica */
CFL_BOOL sdb_api_tableModifyColumn(SDB_CONNECTIONP conn, SDB_TABLEP table,
                                   SDB_FIELDP field) {
  CFL_UINT32 errorCount = sdb_thread_errorCount();

  ENTER_FUN_NAME("sdb_api_tableModifyColumn");
  if (field->clpType == SDB_CLP_MEMO_LONG) {
    field->clpType = sdb_thread_getDefaultMemoType(sdb_thread_getData());
  }
  conn->dbAPI->tableModifyColumn(conn, table, field);
  RETURN !sdb_thread_hasNewErrors(errorCount);
}

/* TODO: trazer a logica para a API generica */
CFL_BOOL sdb_api_createTable(SDB_CONNECTIONP connection, const char *schemaName,
                             SDB_TABLEP table) {
  CFL_UINT32 len;
  CFL_UINT32 i;
  SDB_SCHEMAP schema;
  CFL_UINT32 errorCount = sdb_thread_errorCount();

  ENTER_FUN_NAME("sdb_api_createTable");
  len = cfl_list_length(table->fields);
  for (i = 0; i < len; i++) {
    SDB_FIELDP field = (SDB_FIELDP)cfl_list_get(table->fields, i);
    if (field->clpType == SDB_CLP_MEMO_LONG) {
      field->clpType = sdb_thread_getDefaultMemoType(sdb_thread_getData());
    }
  }
  schema = sdb_database_getCreateSchema(connection->database, schemaName);
  if (connection->dbAPI->createTable(connection, schema, table)) {
    sdb_schema_addTable(schema, table);
  }
  RETURN !sdb_thread_hasNewErrors(errorCount);
}

static void itemLength(PHB_ITEM pItem, int *iLen, int *iDec) {
  switch (hb_itemType(pItem)) {
  case HB_IT_INTEGER:
  case HB_IT_LONG:
  case HB_IT_DOUBLE:
  case HB_IT_NUMERIC:
    hb_itemGetNLen(pItem, iLen, iDec);
    if (*iDec) {
      *iLen += *iDec + 1;
    }
    break;
  case HB_IT_LOGICAL:
    *iLen = 1;
    *iDec = 0;
    break;
  case HB_IT_STRING:
    *iLen = (int)hb_itemGetCLen(pItem);
    *iDec = 0;
    break;
  default:
    *iLen = 0;
    *iDec = 0;
    break;
  }
}

static CFL_STRP nextIndexColName(SDB_TABLEP table) {
  CFL_STRP colName = cfl_str_new(5);
  CFL_UINT32 len = cfl_list_length(table->fields);
  CFL_UINT32 i;
  CFL_UINT16 num = 0;
  char colNum[33];

  for (i = 0; i < len; i++) {
    SDB_FIELDP field = (SDB_FIELDP)cfl_list_get(table->fields, i);
    if (field->fieldType == SDB_FIELD_INDEX) {
      ++num;
    }
  }
  sprintf(colNum, "%u", num);
  cfl_str_append(colName, s_IndexColPrefix, colNum, NULL);
  return colName;
}

/* TODO: trazer a logica para a API generica */
SDB_INDEXP sdb_api_createIndex(SDB_CONNECTIONP connection, SDB_TABLEP table,
                               const char *indexName, const char *clipperExpr,
                               PHB_ITEM value, const char *sqlDefaultExpr,
                               CFL_BOOL isVirtual, const char *hint) {
  SDB_INDEXP index;
  SDB_FIELDP field;
  CFL_STRP dbIndexName;
  CFL_UINT32 clpExprLen;
  CFL_UINT32 hintLen;
  char *indexNameUpper;

  ENTER_FUN_NAME("sdb_api_createIndex");

  field = sdb_table_getField(table, clipperExpr);
  if (field == NULL) {
    int iLen;
    int iDec;
    CFL_STRP indColName = nextIndexColName(table);
    itemLength(value, &iLen, &iDec);
    field =
        sdb_field_new(cfl_str_getPtr(indColName), cfl_str_getLength(indColName),
                      cfl_str_getPtr(indColName), cfl_str_getLength(indColName),
                      SDB_FIELD_INDEX, sdb_util_itemToFieldType(value), iLen,
                      (CFL_UINT8)iDec, CFL_TRUE);
    if (sqlDefaultExpr) {
      field->dbExpression = cfl_str_newBuffer(sqlDefaultExpr);
    }
    field->isVirtual = isVirtual;
    cfl_str_free(indColName);
    if (!sdb_api_tableAddColumn(connection, table, field)) {
      sdb_field_free(field);
      RETURN NULL;
    }
  } else if (field->fieldType == SDB_FIELD_DATA) {
    field->fieldType = SDB_FIELD_DATA_INDEX;
  } else {
    sdb_thread_setError(SDB_ERROR_TYPE_SDB, SDB_ERROR_INVALID_FIELD_TYPE,
                        "Invalid field type for index");
    RETURN NULL;
  }
  indexNameUpper = sdb_util_strnDupUpperTrim(indexName, strlen(indexName));
  dbIndexName = cfl_str_new(30);
  cfl_str_append(dbIndexName, cfl_str_getPtr(table->dbName), "_",
                 indexNameUpper, NULL);
  clpExprLen = (CFL_UINT32)(clipperExpr ? strlen(clipperExpr) : 0);
  hintLen = (CFL_UINT32)(hint ? strlen(hint) : 0);
  index =
      sdb_index_new(field, indexNameUpper, (CFL_UINT32)strlen(indexNameUpper),
                    cfl_str_getPtr(dbIndexName), cfl_str_getLength(dbIndexName),
                    clipperExpr, clpExprLen, hint, hintLen);
  if (connection->dbAPI->createIndex(connection, table, index)) {
    sdb_table_addIndex(table, index);
    cfl_str_free(dbIndexName);
    SDB_MEM_FREE(indexNameUpper);
    RETURN index;
  }
  cfl_str_free(dbIndexName);
  SDB_MEM_FREE(indexNameUpper);
  RETURN NULL;
}

CFL_UINT8 sdb_api_getDefaultMemoType(void) { return s_defaultMemoType; }

void sdb_api_setDefaultMemoType(CFL_UINT8 memoType) {
  s_defaultMemoType = memoType;
}

SDB_INDEXP sdb_api_getIndex(SDB_CONNECTIONP connection, const char *schemaName,
                            const char *tableName, const char *indexName) {
  SDB_TABLEP table;

  ENTER_FUN_NAME("sdb_api_getIndex");
  table = sdb_api_getTable(connection, schemaName, tableName);
  if (table != NULL) {
    RETURN sdb_table_getIndex(table, indexName);
  }
  RETURN NULL;
}

/* TODO: trazer a logica para a API generica */
CFL_BOOL sdb_api_tableRename(SDB_CONNECTIONP connection, const char *schemaName,
                             const char *tableName, const char *newClpName,
                             const char *newDBName) {
  SDB_TABLEP table;
  CFL_BOOL bResult = CFL_FALSE;
  char *newClpNameUpper;
  char *newDBNameUpper;
  CFL_UINT32 errorCount = sdb_thread_errorCount();

  table = sdb_api_getTable(connection, schemaName, tableName);
  if (table != NULL) {
    newClpNameUpper = sdb_util_strnDupUpperTrim(newClpName, strlen(newClpName));
    newDBNameUpper = sdb_util_strnDupUpperTrim(newDBName, strlen(newDBName));
    bResult = connection->dbAPI->tableRename(connection, table, newClpNameUpper,
                                             newDBNameUpper);
    SDB_MEM_FREE(newClpNameUpper);
    SDB_MEM_FREE(newDBNameUpper);
  } else {
    sdb_thread_setError(SDB_ERROR_TYPE_SDB, SDB_ERROR_NO_TABLE,
                        "Table not found.");
  }
  return bResult && !sdb_thread_hasNewErrors(errorCount);
}

// CFL_BOOL sdb_api_trimParams(void) {
// }

CFL_BOOL sdb_api_isTrimParams(void) { return s_bTrimParams; }

void sdb_api_setTrimParams(CFL_BOOL bTrimParams) {
  s_bTrimParams = bTrimParams;
}

CFL_BOOL sdb_api_isPadFields(void) { return s_bPadFields; }

void sdb_api_setPadFields(CFL_BOOL bPadFields) { s_bPadFields = bPadFields; }

CFL_BOOL sdb_api_isNullable(void) { return s_bNullable; }

void sdb_api_setNullable(CFL_BOOL bNullable) { s_bNullable = bNullable; }

void sdb_api_setIntervalCloseCursor(CFL_UINT32 newInterval) {
  s_intervalStmtVerification = newInterval;
}

CFL_UINT32 sdb_api_getIntervalCloseCursor(void) {
  return s_intervalStmtVerification;
}

void sdb_api_setDefaultBufferFetchSize(CFL_UINT16 newSize) {
  s_DefaultBufferFetchSize = newSize;
}

CFL_UINT16 sdb_api_getDefaultBufferFetchSize(void) {
  return s_DefaultBufferFetchSize;
}

void sdb_api_setNextBufferFetchSize(CFL_UINT16 newSize) {
  sdb_thread_getData()->nextBufferFetchSize = newSize;
}

CFL_UINT16 sdb_api_getNextBufferFetchSize(void) {
  return sdb_thread_getData()->nextBufferFetchSize;
}

PHB_ITEM sdb_api_clipperToSql(SDB_AREAP pSDBArea, const char *str,
                              CFL_UINT32 strLen, CFL_UINT8 exprType) {
  SDB_EXPRESSION_NODEP expr;
  PHB_ITEM pSql;
  expr = sdb_expr_clipperExpression(str, strLen);
  if (expr) {
    pSql = pSDBArea->connection->dbAPI->clipperToSqlExpression(pSDBArea, expr,
                                                               exprType);
    sdb_expr_free(expr);
  } else {
    pSql = NULL;
  }
  return pSql;
}

char *sdb_api_getRddName(void) { return s_rddName; }

void sdb_api_setRddName(char *rddName) {
  size_t len = strlen(rddName);
  if (len > 0 && len <= HB_RDD_MAX_DRIVERNAME_LEN) {
    hb_strncpyUpperTrim(s_rddName, rddName, len);
  } else {
    sdb_thread_setError(SDB_ERROR_TYPE_SDB, SDB_ERROR_INVALID_ARGUMENT,
                        "Invalid argument length");
  }
}

HB_ERRCODE sdb_api_genError(SDB_CONNECTIONP pConnection, CFL_UINT16 uiGenCode,
                            CFL_UINT16 uiSubCode, const char *filename,
                            CFL_UINT16 uiFlags, const char *description,
                            const char *operation, PHB_ITEM *pErrorPtr) {
  PHB_ITEM pError;
  HB_ERRCODE errCode = HB_FAILURE;
  CFL_BOOL bRollback = CFL_FALSE;

  ENTER_FUN_NAME("sdb_api_genError");
  if (hb_vmRequestQuery() == 0) {
    if (pErrorPtr) {
      if (!*pErrorPtr) {
        *pErrorPtr = hb_errNew();
      }
      pError = *pErrorPtr;
    } else {
      pError = hb_errNew();
    }
    hb_errPutGenCode(pError, uiGenCode);
    hb_errPutSubCode(pError, uiSubCode);
    if (pConnection && pConnection->transaction &&
        pConnection->isRollbackOnError) {
      CFL_STRP sb = cfl_str_new(128);

      bRollback = CFL_TRUE;
      CFL_STR_APPEND_CONST(sb, "Transaction rolled back (");
      if (description) {
        cfl_str_append(sb, description, NULL);
      } else {
        cfl_str_append(sb, sdb_error_getDescription(uiGenCode, uiSubCode),
                       NULL);
      }
      cfl_str_appendChar(sb, ')');
      hb_errPutDescription(pError, cfl_str_getPtr(sb));
      cfl_str_free(sb);
    } else if (description) {
      hb_errPutDescription(pError, description);
    } else {
      hb_errPutDescription(pError,
                           sdb_error_getDescription(uiGenCode, uiSubCode));
    }
    if (operation) {
      hb_errPutOperation(pError, operation);
    }
    if (filename) {
      hb_errPutFileName(pError, filename);
    }

    if (uiFlags) {
      hb_errPutFlags(pError, uiFlags);
    }

    hb_errPutSeverity(pError, ES_ERROR);
    hb_errPutSubSystem(pError, s_rddName);
    if (bRollback && pConnection) {
      sdb_connection_rollback(pConnection, CFL_TRUE);
    }
    errCode = hb_errLaunch(pError);

    if (!pErrorPtr) {
      hb_itemRelease(pError);
    }
  }
  RETURN errCode;
}

HB_ERRCODE
sdb_api_genErrorFromSDBError(SDB_CONNECTIONP pConnection, const char *filename,
                             CFL_UINT16 uiFlags, SDB_ERRORP pSDBError,
                             const char *operation, PHB_ITEM *pErrorPtr) {
  HB_ERRCODE errCode;
  ENTER_FUN_NAME("sdb_api_genErrorFromSDBError");
  if (!cfl_str_isEmpty(pSDBError->message)) {
    errCode = sdb_api_genError(
        pConnection, pSDBError->type, pSDBError->code, filename, uiFlags,
        cfl_str_getPtr(pSDBError->message), operation, pErrorPtr);
  } else {
    errCode = sdb_api_genError(pConnection, pSDBError->type, pSDBError->code,
                               filename, uiFlags, "Internal error", operation,
                               pErrorPtr);
  }
  RETURN errCode;
}
