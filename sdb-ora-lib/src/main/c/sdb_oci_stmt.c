#include "sdb_oci_stmt.h"
#include "sdb_oci_var.h"
#include "sdb_defs.h"
#include "sdb_error.h"
#include "sdb_thread.h"
#include "sdb_log.h"

static void statementType(SDB_OCI_STMT *stmt) {
   void *errorHandle = sdb_oci_getErrorHandle();
   CFL_UINT16 stmtType = SDB_OCI_STMT_UNKNOWN;
   int status;
   int returning = 0;

   status = sdb_oci_getSymbols()->OCIAttrGet(stmt->handle, SDB_OCI_HTYPE_STMT, &stmtType, 0, SDB_OCI_ATTR_STMT_TYPE, errorHandle);
   CHECK_STATUS_ERROR(status, errorHandle, "Error checking statement type");
   switch (stmtType) {
      case SDB_OCI_STMT_BEGIN:
      case SDB_OCI_STMT_DECLARE:
      case SDB_OCI_STMT_CALL:
         stmt->type = SDB_STMT_PLSQL;
         stmt->isReturning = CFL_FALSE;
         break;

      case SDB_OCI_STMT_ALTER:
      case SDB_OCI_STMT_CREATE:
      case SDB_OCI_STMT_DROP:
         stmt->type = SDB_STMT_DDL;
         stmt->isReturning = CFL_FALSE;
         break;

      case SDB_OCI_STMT_DELETE:
      case SDB_OCI_STMT_INSERT:
      case SDB_OCI_STMT_UPDATE:
         stmt->type = SDB_STMT_DML;
         status = sdb_oci_getSymbols()->OCIAttrGet(stmt->handle, SDB_OCI_HTYPE_STMT, &returning, 0, SDB_OCI_ATTR_STMT_IS_RETURNING,
                 errorHandle);
         CHECK_STATUS_ERROR(status, errorHandle, "Error checking statement type");
         stmt->isReturning = returning ? CFL_TRUE : CFL_FALSE;
         break;

      case SDB_OCI_STMT_SELECT:
         stmt->type = SDB_STMT_QUERY;
         stmt->isReturning = CFL_FALSE;
         break;
      default:
         stmt->type = SDB_STMT_UNKNOWN;
         stmt->isReturning = CFL_FALSE;
         break;
   }
}

SDB_OCI_STMT * sdb_oci_stmt_new(SDB_OCI_CONNECTION *conn, void *initHandle) {
   SDB_OCI_STMT *stmt;
   void *errorHandle;
   void *handle = NULL;
   int status;
   CFL_BOOL bPrepared;

   if (initHandle != NULL) {
      handle = initHandle;
      bPrepared = CFL_TRUE;
   } else {
      errorHandle = sdb_oci_getErrorHandle();
      status = sdb_oci_getSymbols()->OCIHandleAlloc(sdb_oci_getEnv(), &handle, SDB_OCI_HTYPE_STMT, 0, NULL);
      CHECK_STATUS_RETURN(status, errorHandle, "Error allocating STMT descriptor", NULL);
      bPrepared = CFL_FALSE;
   }
   stmt = SDB_MEM_ALLOC(sizeof(SDB_OCI_STMT));
   if (stmt != NULL) {
      stmt->refCount = 1;
      stmt->conn = conn;
      stmt->handle = handle;
      stmt->isPrepared = bPrepared;
      stmt->vars = NULL;
      stmt->columnsInfo = NULL;
      stmt->varsPrefetchCount = 0;
      stmt->prefetchRows = 1;
      stmt->fetchedRows = 0;
      statementType(stmt);
   } else if (initHandle == NULL) {
      sdb_thread_setError(SDB_ERROR_TYPE_DB_ERROR, SDB_OCI_ERROR_DESCRIPTOR, "Cannot allocate memory to create STMT");
      sdb_oci_getSymbols()->OCIHandleFree(handle, SDB_OCI_HTYPE_STMT);
   }
   return stmt;
}

CFL_UINT8 sdb_oci_stmt_getType(SDB_OCI_STMT *stmt) {
   if (stmt->type == SDB_STMT_UNKNOWN) {
      statementType(stmt);
   }
   return stmt->type;
}

void * sdb_oci_stmt_handle(SDB_OCI_STMT *stmt) {
   return stmt->handle;
}

static void freeStatementDefines(SDB_OCI_STMT *stmt) {
   if (stmt->vars != NULL) {
      CFL_UINT32 len = cfl_list_length(stmt->vars);
      CFL_UINT32 i;
      for (i = 0; i < len; i++) {
         SDB_OCI_VAR *var = cfl_list_get(stmt->vars, i);
         if (var != NULL) {
            sdb_oci_var_free(var);
         }
      }
      cfl_list_free(stmt->vars);
      stmt->vars = NULL;
   }
   if (stmt->columnsInfo != NULL) {
      cfl_array_free(stmt->columnsInfo);
      stmt->columnsInfo = NULL;
   }
}

void sdb_oci_stmt_free(SDB_OCI_STMT *stmt) {
   SDB_LOG_TRACE(("sdb_oci_stmt_free. stmt=%p handle=%p refCount=%lu", stmt, stmt->handle, stmt->refCount));
   if (stmt->refCount > 0) {
      --stmt->refCount;
   }
   if (stmt->refCount == 0) {
      int status;
      void *errorHandle = sdb_oci_getErrorHandle();

      freeStatementDefines(stmt);
      if (stmt->isPrepared) {
         status = sdb_oci_getSymbols()->OCIStmtRelease(stmt->handle, errorHandle, NULL, 0, SDB_OCI_DEFAULT);
      } else {
         status = sdb_oci_getSymbols()->OCIHandleFree(stmt->handle, SDB_OCI_HTYPE_STMT);
      }
      CHECK_STATUS_ERROR(status, errorHandle, "Error releasing statement");
      SDB_MEM_FREE(stmt);
   }
}

CFL_UINT32 sdb_oci_stmt_incRef(SDB_OCI_STMT *stmt) {
   return ++stmt->refCount;
}

CFL_UINT8 sdb_oci_stmt_type(SDB_OCI_STMT *stmt) {
   return stmt->type;
}

CFL_BOOL sdb_oci_stmt_bindByName(SDB_OCI_STMT *stmt, SDB_OCI_VAR *var, char *parName, CFL_UINT32 parNameLen) {
   SDB_OCI_SYMBOLS *ociSymbols = sdb_oci_getSymbols();
   void *errorHandle = sdb_oci_getErrorHandle();
   int status;
   CFL_BOOL bDynamic;

   SDB_LOG_TRACE(("sdb_oci_stmt_bindByName. stmt=%p var=%p var->dataType=%u var->isDynamic=%s var->isArray=%s var->maxItems=%lu var->itemsCount=%lu parName=%.*s",
           stmt, var, var->dataType, BOOL_STR(var->isDynamic), BOOL_STR(var->isArray), var->maxItems, var->itemsCount, parNameLen, parName));
   bDynamic = stmt->isReturning || var->isDynamic;
   if (ociSymbols->OCIBindByName2 != NULL) {
      status = ociSymbols->OCIBindByName2(stmt->handle, &var->handle, errorHandle, parName, parNameLen,
              bDynamic ? NULL : sdb_oci_var_getDataBuffer(var),
              var->isDynamic ? SDB_MAX_ITEM_LEN : sdb_oci_var_itemSize(var),
              var->dataType,
              bDynamic ? NULL : sdb_oci_var_getIndBuffer(var),
              bDynamic ? NULL : sdb_oci_var_getLenBuffer32(var),
              bDynamic ? NULL : sdb_oci_var_getRetCodBuffer(var),
              var->isArray ? sdb_oci_var_maxItems(var) : 0,
              var->isArray ? &var->itemsCount : NULL,
              bDynamic ? SDB_OCI_DATA_AT_EXEC : SDB_OCI_DEFAULT);
   } else {
      status = ociSymbols->OCIBindByName(stmt->handle, &var->handle, errorHandle, parName, parNameLen,
              bDynamic ? NULL : sdb_oci_var_getDataBuffer(var),
              var->isDynamic ? SDB_MAX_ITEM_LEN : (CFL_INT32) sdb_oci_var_itemSize(var),
              var->dataType,
              bDynamic ? NULL : sdb_oci_var_getIndBuffer(var),
              bDynamic ? NULL : sdb_oci_var_getLenBuffer16(var),
              bDynamic ? NULL : sdb_oci_var_getRetCodBuffer(var),
              var->isArray ? sdb_oci_var_maxItems(var) : 0,
              var->isArray ? &var->itemsCount : NULL,
              bDynamic ? SDB_OCI_DATA_AT_EXEC : SDB_OCI_DEFAULT);
   }
   if (! STATUS_ERROR(status) && bDynamic) {
    status = ociSymbols->OCIBindDynamic(var->handle, errorHandle, var, (void*) sdb_oci_var_inCallback,
            var, (void*) sdb_oci_var_outCallback);
   }
   CHECK_STATUS_RETURN(status, errorHandle, "Error binding variable", CFL_FALSE);
   return CFL_TRUE;
}

CFL_BOOL sdb_oci_stmt_bindByPos(SDB_OCI_STMT *stmt, SDB_OCI_VAR *var, CFL_UINT32 pos) {
   SDB_OCI_SYMBOLS *ociSymbols = sdb_oci_getSymbols();
   void *errorHandle = sdb_oci_getErrorHandle();
   int status;
   CFL_BOOL bDynamic;

   SDB_LOG_TRACE(("sdb_oci_stmt_bindByPos. stmt=%p var=%p var->dataType=%u var->isDynamic=%s var->isArray=%s var->maxItems=%lu var->itemsCount=%lu pos=%lu",
           stmt, var, var->dataType, BOOL_STR(var->isDynamic), BOOL_STR(var->isArray), var->maxItems, var->itemsCount, pos));
   bDynamic = stmt->isReturning || var->isDynamic;
   if (ociSymbols->OCIBindByPos2 != NULL) {
      status = ociSymbols->OCIBindByPos2(stmt->handle, &var->handle, errorHandle, pos,
              bDynamic ? NULL : sdb_oci_var_getDataBuffer(var),
              var->isDynamic ? SDB_MAX_ITEM_LEN : sdb_oci_var_itemSize(var),
              var->dataType,
              bDynamic ? NULL : sdb_oci_var_getIndBuffer(var),
              bDynamic ? NULL : sdb_oci_var_getLenBuffer32(var),
              bDynamic ? NULL : sdb_oci_var_getRetCodBuffer(var),
              var->isArray ? sdb_oci_var_maxItems(var) : 0,
              var->isArray ? &var->itemsCount : NULL,
              bDynamic ? SDB_OCI_DATA_AT_EXEC : SDB_OCI_DEFAULT);
   } else {
      status = ociSymbols->OCIBindByPos(stmt->handle, &var->handle, errorHandle, pos,
              bDynamic ? NULL : sdb_oci_var_getDataBuffer(var),
              var->isDynamic ? SDB_MAX_ITEM_LEN : (CFL_INT32) sdb_oci_var_itemSize(var),
              var->dataType,
              bDynamic ? NULL : sdb_oci_var_getIndBuffer(var),
              bDynamic ? NULL : sdb_oci_var_getLenBuffer16(var),
              bDynamic ? NULL : sdb_oci_var_getRetCodBuffer(var),
              var->isArray ? sdb_oci_var_maxItems(var) : 0,
              var->isArray ? &var->itemsCount : NULL,
              bDynamic ? SDB_OCI_DATA_AT_EXEC : SDB_OCI_DEFAULT);
   }
   if (! STATUS_ERROR(status) && bDynamic) {
    status = ociSymbols->OCIBindDynamic(var->handle, errorHandle, var, (void*) sdb_oci_var_inCallback,
            var, (void*) sdb_oci_var_outCallback);
   }
   CHECK_STATUS_RETURN(status, errorHandle, "Error binding variable", CFL_FALSE);
   return CFL_TRUE;
}

CFL_UINT32 sdb_oci_stmt_getPrefetchSize(SDB_OCI_STMT *stmt) {
   return stmt->prefetchRows;
}

CFL_BOOL sdb_oci_stmt_setPrefetchSize(SDB_OCI_STMT *stmt, CFL_UINT32 fetchSize) {
   void *errorHandle = sdb_oci_getErrorHandle();
   int status;

   status = sdb_oci_getSymbols()->OCIAttrSet(stmt->handle, SDB_OCI_HTYPE_STMT, &fetchSize, sizeof(fetchSize),
           SDB_OCI_ATTR_PREFETCH_ROWS, errorHandle);
   if (STATUS_ERROR(status)) {
      sdb_oci_setErrorFromOCI(errorHandle, SDB_OCI_HTYPE_ERROR, "Error setting pre-fetch rows size");
      return CFL_FALSE;
   }
   stmt->prefetchRows = fetchSize;
   return CFL_TRUE;
}

CFL_UINT64 sdb_oci_stmt_getRowCount(SDB_OCI_STMT *stmt) {
   void *errorHandle = sdb_oci_getErrorHandle();
   SDB_OCI_CONNECTION *cn = (SDB_OCI_CONNECTION *) stmt->conn;
   CFL_UINT64 rowCount64;
   int status;

   if (cn->clientVersion.versionNum < 12) {
      CFL_UINT32 rowCount32;
      status = sdb_oci_getSymbols()->OCIAttrGet(stmt->handle, SDB_OCI_HTYPE_STMT, &rowCount32, 0,
              SDB_OCI_ATTR_ROW_COUNT, errorHandle);
      CHECK_STATUS_RETURN(status, errorHandle, "Error getting row count statement", 0);
      rowCount64 = (CFL_UINT64) rowCount32;
   } else {
      status = sdb_oci_getSymbols()->OCIAttrGet(stmt->handle, SDB_OCI_HTYPE_STMT, &rowCount64, 0,
              SDB_OCI_ATTR_UB8_ROW_COUNT, errorHandle);
      CHECK_STATUS_RETURN(status, errorHandle, "Error getting row count statement", 0);
   }
   return rowCount64;
}

static CFL_BOOL stmtColInfo(SDB_OCI_STMT *stmt, CFL_UINT32 colPos, SDB_OCI_COL_INFO *info, void *errorHandle) {
   SDB_OCI_SYMBOLS *ociSymbols = sdb_oci_getSymbols();
   void * colHandle = NULL;
   CFL_UINT8 isNullable = 0;
   int status;
   CFL_UINT16 size;

   SDB_LOG_TRACE(("stmtColInfo. info=%p pos=%lu", info, colPos));
   status = ociSymbols->OCIParamGet(stmt->handle, SDB_OCI_HTYPE_STMT, errorHandle, &colHandle, colPos);
   CHECK_STATUS_RETURN(status, errorHandle, "Error getting query column handle", CFL_FALSE);

   status = ociSymbols->OCIAttrGet(colHandle, SDB_OCI_DTYPE_PARAM, (void *) &info->colName, &info->colNameLen, SDB_OCI_ATTR_NAME, errorHandle);
   if (STATUS_ERROR(status)) {
      sdb_oci_setErrorFromOCI(errorHandle, SDB_OCI_HTYPE_ERROR, "Error getting query column name");
      ociSymbols->OCIDescriptorFree(colHandle, SDB_OCI_DTYPE_PARAM);
      return CFL_FALSE;
   }
   status = ociSymbols->OCIAttrGet(colHandle, SDB_OCI_DTYPE_PARAM, &info->dataType, 0, SDB_OCI_ATTR_DATA_TYPE, errorHandle);
   if (STATUS_ERROR(status)) {
      sdb_oci_setErrorFromOCI(errorHandle, SDB_OCI_HTYPE_ERROR, "Error getting query column data type");
      ociSymbols->OCIDescriptorFree(colHandle, SDB_OCI_DTYPE_PARAM);
      return CFL_FALSE;
   }
   if (IS_NUM_DATATYPE(info->dataType)) {
      status = ociSymbols->OCIAttrGet(colHandle, SDB_OCI_DTYPE_PARAM, &info->precision, 0, SDB_OCI_ATTR_PRECISION, errorHandle);
      if (STATUS_ERROR(status)) {
         sdb_oci_setErrorFromOCI(errorHandle, SDB_OCI_HTYPE_ERROR, "Error getting query column precision");
         ociSymbols->OCIDescriptorFree(colHandle, SDB_OCI_DTYPE_PARAM);
         return CFL_FALSE;
      }
      status = ociSymbols->OCIAttrGet(colHandle, SDB_OCI_DTYPE_PARAM, &info->scale, 0, SDB_OCI_ATTR_SCALE, errorHandle);
      if (STATUS_ERROR(status)) {
         sdb_oci_setErrorFromOCI(errorHandle, SDB_OCI_HTYPE_ERROR, "Error getting query column sacle");
         ociSymbols->OCIDescriptorFree(colHandle, SDB_OCI_DTYPE_PARAM);
         return CFL_FALSE;
      }
      info->sizeInBytes = 0;
      info->sizeInChars = 0;
      if (info->precision == 0 && info->scale >= 0) {
         info->dataType = SDB_SQLT_BDOUBLE;
      } else if (info->scale < 0) {
         info->dataType = SDB_SQLT_INT;
      } else {
         info->dataType = SDB_SQLT_BDOUBLE;
      }
   } else if (IS_CHAR_DATATYPE(info->dataType) || info->dataType == SDB_SQLT_RDD) {
      status = ociSymbols->OCIAttrGet(colHandle, SDB_OCI_DTYPE_PARAM, &size, 0, SDB_OCI_ATTR_DATA_SIZE, errorHandle);
      if (STATUS_ERROR(status)) {
         sdb_oci_setErrorFromOCI(errorHandle, SDB_OCI_HTYPE_ERROR, "Error getting query column size in bytes");
         ociSymbols->OCIDescriptorFree(colHandle, SDB_OCI_DTYPE_PARAM);
         return CFL_FALSE;
      }
      info->sizeInBytes = (CFL_UINT32) size;
      status = ociSymbols->OCIAttrGet(colHandle, SDB_OCI_DTYPE_PARAM, &size, 0, SDB_OCI_ATTR_CHAR_SIZE, errorHandle);
      if (STATUS_ERROR(status)) {
         sdb_oci_setErrorFromOCI(errorHandle, SDB_OCI_HTYPE_ERROR, "Error getting query column size in chars");
         ociSymbols->OCIDescriptorFree(colHandle, SDB_OCI_DTYPE_PARAM);
         return CFL_FALSE;
      }
      info->sizeInChars = (CFL_UINT32) size;
      info->precision = 0;
      info->scale = 0;
   } else {
      info->sizeInBytes = 0;
      info->sizeInChars = 0;
      info->precision = 0;
      info->scale = 0;
   }
   status = ociSymbols->OCIAttrGet(colHandle, SDB_OCI_DTYPE_PARAM, (void*) &isNullable, 0, SDB_OCI_ATTR_IS_NULL, errorHandle);
   CHECK_STATUS_ERROR(status, errorHandle, "Error getting query column is nullable");
   info->isNullable = isNullable ? CFL_TRUE : CFL_FALSE;
   ociSymbols->OCIDescriptorFree(colHandle, SDB_OCI_DTYPE_PARAM);
   return CFL_TRUE;
}

static CFL_BOOL createVarDefine(SDB_OCI_STMT *stmt, SDB_OCI_COL_INFO *info, CFL_UINT32 pos, void *errorHandle) {
   SDB_OCI_SYMBOLS *ociSymbols = sdb_oci_getSymbols();
   SDB_OCI_VAR *var;
   int status;

   SDB_LOG_TRACE(("createVarDefine. name=%.*s pos=%lu dataType=%hu precision=%hd scale=%hhd sizeInBytes=%lu sizeInChars=%lu",
           info->colNameLen, info->colName, pos, info->dataType, info->precision, info->scale, info->sizeInBytes, info->sizeInChars));
   if (ociSymbols->OCIDefineByPos2 != NULL) {
      var = sdb_oci_var_new2(stmt->conn, info->dataType, info->sizeInBytes, 1, CFL_FALSE);
   } else {
      var = sdb_oci_var_new(stmt->conn, info->dataType, info->sizeInBytes, 1, CFL_FALSE);
   }
   if (var == NULL) {
      sdb_thread_setError(SDB_ERROR_TYPE_DB_ERROR, SDB_OCI_ERROR_EXEC_STMT, "Can't allocate define buffer for column %s", info->colName);
      return CFL_FALSE;
   }
   if (ociSymbols->OCIDefineByPos2 != NULL) {
      status = ociSymbols->OCIDefineByPos2(stmt->handle,
              &var->handle,
              errorHandle,
              pos,
              var->isDynamic ? NULL : sdb_oci_var_getDataBuffer(var),
              var->isDynamic ? SDB_MAX_ITEM_LEN : sdb_oci_var_itemSize(var),
              var->dataType,
              var->isDynamic ? NULL : sdb_oci_var_getIndBuffer(var),
              var->isDynamic ? NULL : sdb_oci_var_getLenBuffer32(var),
              var->isDynamic ? NULL : sdb_oci_var_getRetCodBuffer(var),
              var->isDynamic ? SDB_OCI_DYNAMIC_FETCH : SDB_OCI_DEFAULT);
      if (STATUS_ERROR(status)) {
         sdb_oci_setErrorFromOCI(errorHandle, SDB_OCI_HTYPE_ERROR, "Error defining column");
         sdb_oci_var_free(var);
         return CFL_FALSE;
      }
   } else {
      status = ociSymbols->OCIDefineByPos(stmt->handle,
              &var->handle,
              errorHandle,
              pos,
              var->isDynamic ? NULL : sdb_oci_var_getDataBuffer(var),
              var->isDynamic ? SDB_MAX_ITEM_LEN : (CFL_INT32) sdb_oci_var_itemSize(var),
              var->dataType,
              var->isDynamic ? NULL : sdb_oci_var_getIndBuffer(var),
              var->isDynamic ? NULL : sdb_oci_var_getLenBuffer16(var),
              var->isDynamic ? NULL : sdb_oci_var_getRetCodBuffer(var),
              var->isDynamic ? SDB_OCI_DYNAMIC_FETCH : SDB_OCI_DEFAULT);
      if (STATUS_ERROR(status)) {
         sdb_oci_setErrorFromOCI(errorHandle, SDB_OCI_HTYPE_ERROR, "Error defining column");
         sdb_oci_var_free(var);
         return CFL_FALSE;
      }
   }
   if (var->isDynamic) {
      status = ociSymbols->OCIDefineDynamic(var->handle, errorHandle, var, (void*) sdb_oci_var_defineCallback);
      if (STATUS_ERROR(status)) {
         sdb_oci_setErrorFromOCI(errorHandle, SDB_OCI_HTYPE_ERROR, "Error defining callback for column");
         sdb_oci_var_free(var);
         return CFL_FALSE;
      }
      ++stmt->varsPrefetchCount;
   }
   if (HAS_DESCRIPTOR(var->dataType)) {
      ++stmt->varsPrefetchCount;
   }
   cfl_list_add(stmt->vars, var);
   return CFL_TRUE;
}

static CFL_BOOL createColumnsDefines(SDB_OCI_STMT *stmt, CFL_UINT32 colCount, void *errorHandle) {
   CFL_UINT32 i;

   SDB_LOG_TRACE(("createColumnsDefines. col_count=%u", colCount));
   stmt->varsPrefetchCount = 0;
   stmt->vars = cfl_list_new(colCount);
   if (stmt->vars == NULL) {
      sdb_thread_setError(SDB_ERROR_TYPE_DB_ERROR, SDB_OCI_ERROR_EXEC_STMT, "Can't allocate memory for defines buffers");
      return CFL_FALSE;
   }
   stmt->columnsInfo = cfl_array_new(colCount, sizeof(SDB_OCI_COL_INFO));
   if (stmt->columnsInfo == NULL) {
      cfl_list_free(stmt->vars);
      stmt->vars = NULL;
      sdb_thread_setError(SDB_ERROR_TYPE_DB_ERROR, SDB_OCI_ERROR_EXEC_STMT, "Can't allocate memory for columns info");
      return CFL_FALSE;
   }
   for (i = 1; i <= colCount; i++ ) {
      SDB_OCI_COL_INFO *info = cfl_array_add(stmt->columnsInfo);
      if (info != NULL && stmtColInfo(stmt, i, info, errorHandle)) {
         if (! createVarDefine(stmt, info, i, errorHandle)) {
            freeStatementDefines(stmt);
            return CFL_FALSE;
         }
      } else {
         freeStatementDefines(stmt);
         return CFL_FALSE;
      }
   }
   return CFL_TRUE;
}

static CFL_BOOL createQueryDefines(SDB_OCI_STMT *stmt, void *errorHandle) {
   SDB_LOG_TRACE(("createQueryDefines. type=%hhu", stmt->type));
   if (IS_QUERY(stmt)) {
      CFL_UINT32 colCount;
      int status = sdb_oci_getSymbols()->OCIAttrGet(stmt->handle, SDB_OCI_HTYPE_STMT, &colCount, 0,
              SDB_OCI_ATTR_PARAM_COUNT, errorHandle);
      CHECK_STATUS_RETURN(status, errorHandle, "Error describing statement", CFL_FALSE);
      if (stmt->vars == NULL) {
         createColumnsDefines(stmt, colCount, errorHandle);
      } else if (cfl_list_length(stmt->vars) != colCount) {
         freeStatementDefines(stmt);
         createColumnsDefines(stmt, colCount, errorHandle);
      }
   }
   return CFL_TRUE;
}

CFL_BOOL sdb_oci_stmt_execute(SDB_OCI_STMT *stmt, CFL_UINT32 numIters) {
   void *errorHandle = sdb_oci_getErrorHandle();
   int status;
   SDB_LOG_TRACE(("sdb_oci_stmt_execute. stmt=%p stmt->handle=%p stmt->conn=%p type=%d iters=%u",
           stmt, stmt->handle, stmt->conn, stmt->type, numIters));
   stmt->fetchedRows = 0;
   status = sdb_oci_getSymbols()->OCIStmtExecute(stmt->conn->serviceHandle, stmt->handle, errorHandle, numIters,
           0, NULL, NULL, SDB_OCI_DEFAULT);
   CHECK_STATUS_RETURN(status, errorHandle, "Error executing statement", CFL_FALSE);
   if (IS_QUERY(stmt)) {
      return createQueryDefines(stmt, errorHandle);
   }
   return CFL_TRUE;
}

static CFL_BOOL defineVarsPrepareFetch(SDB_OCI_STMT *stmt) {
   CFL_UINT32 len = cfl_list_length(stmt->vars);
   CFL_UINT32 i;

   for (i = 0; i < len; i++) {
      sdb_oci_var_prepareFetch((SDB_OCI_VAR *) cfl_list_get(stmt->vars, i));
   }
   return CFL_TRUE;
}

CFL_BOOL sdb_oci_stmt_fetchNext(SDB_OCI_STMT *stmt, CFL_BOOL *bFound) {
   void *errorHandle;
   int status;

   if (stmt->varsPrefetchCount > 0 && ! defineVarsPrepareFetch(stmt)) {
      *bFound = CFL_FALSE;
      return CFL_FALSE;
   }
   errorHandle = sdb_oci_getErrorHandle();
   status = sdb_oci_getSymbols()->OCIStmtFetch2(stmt->handle, errorHandle, 1, SDB_OCI_FETCH_NEXT, 0, SDB_OCI_DEFAULT);
   if (! STATUS_ERROR(status)) {
      *bFound = CFL_TRUE;
      ++stmt->fetchedRows;
   } else if (status == SDB_OCI_NO_DATA) {
      *bFound = CFL_FALSE;
   } else {
      *bFound = CFL_FALSE;
      sdb_oci_setErrorFromOCI(errorHandle, SDB_OCI_HTYPE_ERROR, "Error fetching data");
      return CFL_FALSE;
   }
   return CFL_TRUE;
}

CFL_UINT32 sdb_oci_stmt_getColCount(SDB_OCI_STMT *stmt) {
   SDB_LOG_DEBUG(("sdb_oci_stmt_getColCount. stmt=%p", stmt));
   if (stmt->vars != NULL || createQueryDefines(stmt, sdb_oci_getErrorHandle())) {
      return cfl_list_length(stmt->vars);
   } else {
      return 0;
   }
}

SDB_OCI_COL_INFO *sdb_oci_stmt_getColInfo(SDB_OCI_STMT *stmt, CFL_UINT32 colPos) {
   if (stmt->columnsInfo != NULL) {
      return (SDB_OCI_COL_INFO *) cfl_array_get(stmt->columnsInfo, colPos - 1);
   }
   return NULL;
}

SDB_OCI_VAR *sdb_oci_stmt_getColVar(SDB_OCI_STMT *stmt, CFL_UINT32 colPos) {
   if (stmt->vars != NULL || createQueryDefines(stmt, sdb_oci_getErrorHandle())) {
      return (SDB_OCI_VAR *) cfl_list_get(stmt->vars, colPos - 1);
   }
   return NULL;
}

CFL_UINT32 sdb_oci_stmt_colInfoCount(SDB_OCI_STMT *stmt) {
   if (stmt->columnsInfo != NULL) {
      return cfl_array_length(stmt->columnsInfo);
   }
   return 0;
}

CFL_UINT32 sdb_oci_stmt_varCount(SDB_OCI_STMT *stmt) {
   if (stmt->vars != NULL || createQueryDefines(stmt, sdb_oci_getErrorHandle())) {
      return cfl_list_length(stmt->vars);
   }
   return 0;
}

