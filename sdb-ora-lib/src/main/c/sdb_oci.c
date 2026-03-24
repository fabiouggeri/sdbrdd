#include "cfl_types.h"
#include <stdlib.h>
#include <string.h>

#include "hbapi.h"
#ifdef __XHB__
#include "hbfast.h"
#endif
#include "hbset.h"
#include "hbdate.h"
#include "hbapiitm.h"
#include "hbapierr.h"

#include "sdb_oci.h"
#include "sdb_product.h"
#include "cfl_list.h"
#include "cfl_array.h"
#include "cfl_str.h"
#include "sdb_api.h"
#include "sdb_area.h"
#include "sdb_statement.h"
#include "sdb_transaction.h"
#include "sdb_schema.h"
#include "sdb_table.h"
#include "sdb_field.h"
#include "sdb_index.h"
#include "sdb_thread.h"
#include "sdb_param.h"
#include "sdb_connection.h"
#include "sdb_param.h"
#include "sdb_log.h"
#include "sdb_info.h"
#include "sdb_expr.h"
#include "sdb_error.h"
#include "sdb_record.h"
#include "sdb_util.h"
#include "sdb_lob.h"
#include "sdb_dict.h"
#include "cfl_sql.h"
#include "sdb_oci_lib.h"
#include "sdb_oci_var.h"
#include "sdb_oci_stmt.h"
#include "sdb_oci_lob.h"
#include "sdb_oci_rowid.h"

#define VAR_PARAM_OUT_LEN 4096

#define EXEC_IMMEDIATE_CONST(c, s, r)   sdb_connection_executeImmediateLen(c, s, sizeof(s) - 1, r)

#define PREPARE_STMT_CONST(c, s) sdb_connection_prepareStatementBufferLen(c, s, sizeof(s) - 1)

typedef struct {
   long formatID;
   long gtrid_length;
   long bqual_length;
   char data[SDB_XA_XIDDATASIZE];
} SDB_OCI_XID;

static int s_initCount = 0;

static CFL_SQL_BUILDER s_sqlBuilder;

static SDB_OCI_SYMBOLS *s_ociSymbols = NULL;

static CFL_BOOL lobLength(SDB_OCI_CONNECTION *cn, void *lobHandle, CFL_UINT64 *lobSize) {
   int status;
   void *errorHandle = sdb_oci_getErrorHandle();

   status = s_ociSymbols->OCILobGetLength2(cn->serviceHandle, errorHandle, lobHandle, lobSize);
   CHECK_STATUS_RETURN(status, errorHandle, "Error getting LOB length", CFL_FALSE);
   return CFL_TRUE;
}

static CFL_BOOL invalidParamDataType(SDB_PARAMP param) {
   if (sdb_param_getName(param) == NULL) {
      sdb_thread_setError(SDB_ERROR_TYPE_SDB, SDB_ERROR_INVALID_DATATYPE, "Invalid data type for param %u.", sdb_param_getPos(param));
   } else {
      sdb_thread_setError(SDB_ERROR_TYPE_SDB, SDB_ERROR_INVALID_DATATYPE, "Invalid data type for param :%s.", sdb_param_getNameChar(param));
   }
   return CFL_FALSE;
}

static void initSqlBuilder(void) {
   cfl_sql_initBuilder(&s_sqlBuilder);
}

static void registerTranslations(SDB_PRODUCTP product) {
   sdb_product_addTranslation(product, sdb_expr_translationNew("ALLTRIM", SDB_CLP_CHARACTER, 1, "TRIM(#1)"));
   sdb_product_addTranslation(product, sdb_expr_translationNew("DTOS", SDB_CLP_CHARACTER, 1, "TO_CHAR(#1, 'YYYYMMDD')"));
   sdb_product_addTranslation(product, sdb_expr_translationNew("YEAR", SDB_CLP_NUMERIC, 1, "TO_NUMBER(TO_CHAR(#1, 'YYYY'))"));
   sdb_product_addTranslation(product, sdb_expr_translationNew("VAL", SDB_CLP_NUMERIC, 1, "TO_NUMBER(#1)"));
   sdb_product_addTranslation(product, sdb_expr_translationNew("LEFT", SDB_CLP_CHARACTER, 2, "SUBSTR(#1,1,#2)"));
   sdb_product_addTranslation(product, sdb_expr_translationNew("RIGHT", SDB_CLP_CHARACTER, 2, "SUBSTR(#1,-#2)"));
   sdb_product_addTranslation(product, sdb_expr_translationNew("STR", SDB_CLP_CHARACTER, 1, "TO_CHAR(#1)"));
   sdb_product_addTranslation(product, sdb_expr_translationNew("STR", SDB_CLP_CHARACTER, 2, "REPLACE(TO_CHAR(#1,RPAD('9',#2,'9')),' ', '')"));
   sdb_product_addTranslation(product, sdb_expr_translationNew("STR", SDB_CLP_CHARACTER, 3, "REPLACE(TO_CHAR(#1,RPAD('9',#2,'9')||'.'||RPAD('9',#3,'9')),' ', '')"));
   sdb_product_addTranslation(product, sdb_expr_translationNew("STRZERO", SDB_CLP_CHARACTER, 2, "REPLACE(TO_CHAR(#1,RPAD('0',#2,'0')),' ', '')"));
   sdb_product_addTranslation(product, sdb_expr_translationNew("SUBSTR", SDB_CLP_CHARACTER, 1, "SUBSTR(#1)"));
   sdb_product_addTranslation(product, sdb_expr_translationNew("SUBSTR", SDB_CLP_CHARACTER, 2, "SUBSTR(#1,#2)"));
   sdb_product_addTranslation(product, sdb_expr_translationNew("SUBSTR", SDB_CLP_CHARACTER, 3, "SUBSTR(#1,#2,#3)"));
}

static CFL_BOOL sdb_oci_initialize(SDB_PRODUCTP product) {
   ENTER_FUN_NAME("sdb_oci_initialize");
   if (s_initCount == 0) {
      if (! sdb_oci_loadOCILibrary()) {
         RETURN CFL_FALSE;
      }
      if (! sdb_oci_createEnv(SDB_OCI_THREADED | SDB_OCI_OBJECT)) {
         RETURN CFL_FALSE;
      }
      s_ociSymbols = sdb_oci_getSymbols();
      product->pkName = cfl_str_newConstLen("RECNO", 5);
      product->pkDefaultExpr = NULL;
      product->delName = cfl_str_newConstLen("IS_DELETED", 10);
      registerTranslations(product);
      sdb_dict_registerDictionary(sdb_dict_getDefault());
      initSqlBuilder();
   }
   ++s_initCount;
   RETURN CFL_TRUE;
}

static void sdb_oci_finalize(SDB_PRODUCTP product) {
   ENTER_FUN_NAME("sdb_oci_finalize");
   HB_SYMBOL_UNUSED(product);
   if (--s_initCount == 0 && sdb_oci_isInitialized()) {
      sdb_oci_freeEnv();
      sdb_oci_freeOCILibrary();
      s_ociSymbols = NULL;
   }
   RETURN;
}

static void freeConnectionHandles(void* serverHandle, void* serviceHandle, void* sessionHandle){
   int status;
   void* errorHandle = sdb_oci_getErrorHandle();

   if (serverHandle != NULL) {
      if (sessionHandle != NULL) {
         status = s_ociSymbols->OCISessionEnd(serviceHandle, errorHandle, sessionHandle, SDB_OCI_DEFAULT);
         CHECK_STATUS_ERROR(status, errorHandle, "Error ending session");
      }
      status = s_ociSymbols->OCIServerDetach(serverHandle, errorHandle, SDB_OCI_DEFAULT);
      CHECK_STATUS_ERROR(status, errorHandle, "Error detaching from server");
      status = s_ociSymbols->OCIHandleFree(serverHandle, SDB_OCI_HTYPE_SERVER);
      CHECK_STATUS_LOG(status, ("Free server handle %p failed", serverHandle));
   }
   if (serviceHandle != NULL) {
      status = s_ociSymbols->OCIHandleFree(serviceHandle, SDB_OCI_HTYPE_SVCCTX);
      CHECK_STATUS_LOG(status, ("Free service handle %p failed", serviceHandle));
   }
   sdb_oci_freeErrorHandle();
   if (sessionHandle != NULL) {
      status = s_ociSymbols->OCIHandleFree(sessionHandle, SDB_OCI_HTYPE_SESSION);
      CHECK_STATUS_LOG(status, ("Free session handle %p failed", sessionHandle));
   }
}

static void getClientVersion(SDB_OCI_VERSION_INFO *versionInfo) {
   s_ociSymbols->OCIClientVersion(&versionInfo->versionNum, &versionInfo->releaseNum, &versionInfo->updateNum,
                                 &versionInfo->portReleaseNum, &versionInfo->portUpdateNum);
   versionInfo->fullVersionNum = (CFL_UINT32) SDB_ORACLE_VERSION_TO_NUMBER(versionInfo->versionNum,
                                                                           versionInfo->releaseNum,
                                                                           versionInfo->updateNum,
                                                                           versionInfo->portReleaseNum,
                                                                           versionInfo->portUpdateNum);
}

static SDB_OCI_CONNECTION * createConnection(const char *username, const char *password, const char *database) {
   SDB_OCI_CONNECTION *cn;
   int status;
   void *serverHandle;
   void *serviceHandle;
   void *sessionHandle;
   void *errorHandle = sdb_oci_getErrorHandle();
   if (errorHandle == NULL) {
      return NULL;
   }
   /* server contexts */
   status = s_ociSymbols->OCIHandleAlloc(sdb_oci_getEnv(), &serviceHandle, SDB_OCI_HTYPE_SVCCTX, 0, NULL);
   CHECK_STATUS_RETURN(status, errorHandle, "Login error", NULL);
   status = s_ociSymbols->OCIHandleAlloc(sdb_oci_getEnv(), &serverHandle, SDB_OCI_HTYPE_SERVER, 0, NULL);
   CHECK_STATUS_RETURN(status, errorHandle, "Login error", NULL);
   status = s_ociSymbols->OCIAttrSet(serviceHandle, SDB_OCI_HTYPE_SVCCTX, serverHandle, 0, SDB_OCI_ATTR_SERVER, errorHandle);
   CHECK_STATUS_RETURN(status, errorHandle, "Login error", NULL);
   status = s_ociSymbols->OCIHandleAlloc(sdb_oci_getEnv(), &sessionHandle, SDB_OCI_HTYPE_SESSION, 0, NULL);
   CHECK_STATUS_RETURN(status, errorHandle, "Login error", NULL);
   status = s_ociSymbols->OCIServerAttach(serverHandle, errorHandle, database, (CFL_UINT32) strlen(database), SDB_OCI_DEFAULT);
   CHECK_STATUS_RETURN(status, errorHandle, "Login error", NULL);
   status = s_ociSymbols->OCIAttrSet(sessionHandle, SDB_OCI_HTYPE_SESSION, (void *) username, (CFL_UINT32) strlen(username), SDB_OCI_ATTR_USERNAME, errorHandle);
   CHECK_STATUS_RETURN(status, errorHandle, "Login error", NULL);
   status = s_ociSymbols->OCIAttrSet(sessionHandle, SDB_OCI_HTYPE_SESSION, (void *) password, (CFL_UINT32) strlen(password), SDB_OCI_ATTR_PASSWORD, errorHandle);
   CHECK_STATUS_RETURN(status, errorHandle, "Login error", NULL);
   status = s_ociSymbols->OCISessionBegin(serviceHandle, errorHandle, sessionHandle, SDB_OCI_CRED_RDBMS, SDB_OCI_DEFAULT);
   CHECK_STATUS_RETURN(status, errorHandle, "Login error", NULL);
   status = s_ociSymbols->OCIAttrSet(serviceHandle, SDB_OCI_HTYPE_SVCCTX, sessionHandle, 0, SDB_OCI_ATTR_SESSION, errorHandle);
   CHECK_STATUS_RETURN(status, errorHandle, "Login error", NULL);
   cn = SDB_MEM_ALLOC(sizeof (SDB_OCI_CONNECTION));
   if (cn != NULL) {
       cn->serverHandle = serverHandle;
       cn->serviceHandle = serviceHandle;
       cn->sessionHandle = sessionHandle;
       cn->commitMode = SDB_OCI_DEFAULT;
       getClientVersion(&cn->clientVersion);
       memset(&cn->serverVersion, 0, sizeof(cn->serverVersion));
   } else {
      freeConnectionHandles(serverHandle, serviceHandle, sessionHandle);
   }
   return cn;
}

static CFL_BOOL prepareSession(SDB_CONNECTIONP conn) {
   CFL_UINT64 rows;
   EXEC_IMMEDIATE_CONST(conn, "ALTER SESSION SET NLS_LENGTH_SEMANTICS='CHAR'", &rows);
   EXEC_IMMEDIATE_CONST(conn, "ALTER SESSION SET NLS_SORT=BINARY", &rows);
   EXEC_IMMEDIATE_CONST(conn, "ALTER SESSION SET NLS_COMP=BINARY", &rows);
   // EXEC_IMMEDIATE_CONST(conn, "ALTER SESSION SET STATISTICS_LEVEL=BASIC", &rows);
   EXEC_IMMEDIATE_CONST(conn, "ALTER SESSION SET QUERY_REWRITE_ENABLED=FALSE", &rows);
   return sdb_connection_createObjects(conn);
}

static CFL_BOOL sdb_oci_connect(SDB_CONNECTIONP conn, const char *database, const char *username, const char *password) {
   SDB_OCI_CONNECTION *cn;

   ENTER_FUN_NAME("sdb_oci_connect");
   SDB_LOG_DEBUG(("sdb_oci_connect: login=%s@%s", username, database));
   cn = createConnection(username, password, database);
   if (cn == NULL) {
      RETURN CFL_FALSE;
   }
   conn->handle = cn;
   RETURN prepareSession(conn);
}

static void sdb_oci_disconnect(SDB_CONNECTIONP conn) {
   ENTER_FUN_NAME("sdb_oci_disconnect");
   if (conn->handle != NULL) {
      SDB_OCI_CONNECTION *cn = (SDB_OCI_CONNECTION *) conn->handle;
      freeConnectionHandles(cn->serverHandle, cn->serviceHandle, cn->sessionHandle);
      SDB_MEM_FREE(cn);
   }
   RETURN;
}

static CFL_UINT8 sdb_oci_statementType(void *handle) {
   return sdb_oci_stmt_type((SDB_OCI_STMT *)handle);
}

static void * sdb_oci_beginTransaction(SDB_CONNECTIONP conn, CFL_INT32 formatId, CFL_STRP transId, CFL_STRP branchId) {
   SDB_OCI_CONNECTION *cn;
   void *transactionHandle;
   int status;
   void *errorHandle = sdb_oci_getErrorHandle();

   SDB_LOG_TRACE(("sdb_oci_beginTransaction"));
   /* If isn't a distributed transaction, Oracle already has a implicit transaction in use */
   if (formatId < 0) {
      return conn->handle;
   }
   /* Invalid transaction ID */
   if (transId == NULL || branchId == NULL) {
      return NULL;
   }
   cn = (SDB_OCI_CONNECTION *) conn->handle;
   if (cfl_str_getLength(transId) > SDB_XA_MAXGTRIDSIZE) {
      sdb_thread_setError(SDB_ERROR_TYPE_DB_ERROR, SDB_OCI_ERROR_BEGIN_TRANS, "Transaction ID too large. Max size is %d", SDB_XA_MAXGTRIDSIZE);
      return NULL;
   }
   if (cfl_str_getLength(branchId) > SDB_XA_MAXBQUALSIZE) {
      sdb_thread_setError(SDB_ERROR_TYPE_DB_ERROR, SDB_OCI_ERROR_BEGIN_TRANS, "Branch ID too large. Max size is %d", SDB_XA_MAXBQUALSIZE);
      return NULL;
   }

   status = s_ociSymbols->OCIAttrGet(cn->serviceHandle, SDB_OCI_HTYPE_SVCCTX, &transactionHandle, 0, SDB_OCI_ATTR_TRANS, errorHandle);
   CHECK_STATUS_RETURN(status, errorHandle, "Error getting transaction handle", NULL);

   if (transactionHandle == NULL) {
      status = s_ociSymbols->OCIHandleAlloc(sdb_oci_getEnv(), &transactionHandle, SDB_OCI_HTYPE_TRANS, 0, NULL);
      CHECK_STATUS_RETURN(status, errorHandle, "Error creating transaction handle", NULL);

      status = s_ociSymbols->OCIAttrSet(cn->serviceHandle, SDB_OCI_HTYPE_SVCCTX, transactionHandle, 0, SDB_OCI_ATTR_TRANS, errorHandle);
      if (STATUS_ERROR(status)) {
         s_ociSymbols->OCIHandleFree(transactionHandle, SDB_OCI_HTYPE_TRANS);
         sdb_oci_setErrorFromOCI(errorHandle, SDB_OCI_HTYPE_ERROR, "Error assigning transaction to session");
         return NULL;
      }
   }

   // set the XID for the transaction, if applicable
   if (formatId != -1) {
      SDB_OCI_XID xid;
      xid.formatID = formatId;
      xid.gtrid_length = cfl_str_getLength(transId);
      xid.bqual_length = cfl_str_getLength(branchId);
      if (cfl_str_getLength(transId) > 0) {
         strncpy(xid.data, cfl_str_getPtr(transId), cfl_str_getLength(transId));
      }
      if (cfl_str_getLength(branchId) > 0) {
         strncpy(&xid.data[cfl_str_getLength(transId)], cfl_str_getPtr(branchId), cfl_str_getLength(branchId));
      }
      status = s_ociSymbols->OCIAttrSet(transactionHandle, SDB_OCI_HTYPE_TRANS, &xid, sizeof(SDB_OCI_XID), SDB_OCI_ATTR_XID, errorHandle);
      if (STATUS_ERROR(status)) {
         sdb_oci_setErrorFromOCI(errorHandle, SDB_OCI_HTYPE_ERROR, "Error setting XID to transaction");
         return NULL;
      }
   }

   status = s_ociSymbols->OCITransStart(conn->handle, errorHandle, 0, SDB_OCI_TRANS_NEW);
   CHECK_STATUS_RETURN(status, errorHandle, "Error on begin transaction", NULL);

   return conn->handle;
}


static CFL_BOOL sdb_oci_prepareTransaction(SDB_CONNECTIONP conn) {
   SDB_OCI_CONNECTION *cn = (SDB_OCI_CONNECTION *) conn->handle;
   int status;
   CFL_BOOL commitNeeded;
   void *errorHandle = sdb_oci_getErrorHandle();

    status = s_ociSymbols->OCITransPrepare(cn->serviceHandle, errorHandle, SDB_OCI_DEFAULT);
    CHECK_STATUS_RETURN(status, errorHandle, "Error on prepare transaction", CFL_FALSE);
    commitNeeded = (status == SDB_OCI_SUCCESS);
    if (commitNeeded) {
       cn->commitMode = SDB_OCI_TRANS_TWOPHASE;
    }
   return commitNeeded;
}

static void sdb_oci_commitTransaction(SDB_CONNECTIONP conn) {
   SDB_OCI_CONNECTION *cn = (SDB_OCI_CONNECTION *) conn->handle;
   int status;
   void *errorHandle = sdb_oci_getErrorHandle();

   ENTER_FUN_NAME("sdb_oci_commitTransaction");
   status = s_ociSymbols->OCITransCommit(cn->serviceHandle, errorHandle, cn->commitMode);
   cn->commitMode = SDB_OCI_DEFAULT;
   CHECK_STATUS_ERROR(status, errorHandle, "Commit error");
   RETURN;
}

static void sdb_oci_rollbackTransaction(SDB_CONNECTIONP conn) {
   SDB_OCI_CONNECTION *cn = (SDB_OCI_CONNECTION *) conn->handle;
   int status;
   void *errorHandle = sdb_oci_getErrorHandle();

   ENTER_FUN_NAME("sdb_oci_rollbackTransaction");
   status = s_ociSymbols->OCITransRollback(cn->serviceHandle, errorHandle, SDB_OCI_DEFAULT);
   cn->commitMode = SDB_OCI_DEFAULT;
   CHECK_STATUS_ERROR(status, errorHandle, "Rollback error");
   RETURN;
}

static void commitIfAuto(SDB_CONNECTIONP conn, SDB_OCI_STMT *stmt) {
   ENTER_FUN_NAME("commitIfAuto");
   if (conn->transaction == NULL && (IS_DML(stmt) || IS_PLSQL(stmt))) {
      sdb_oci_commitTransaction(conn);
   }
   RETURN;
}

static void freeVar(SDB_PARAMP param) {
   SDB_OCI_VAR *var = (SDB_OCI_VAR *) param->handle;
   param->handle = NULL;
   param->freeHandle = NULL;
   if (var != NULL) {
      if (var->handle != NULL) {
         s_ociSymbols->OCIHandleFree(var->handle, SDB_OCI_HTYPE_BIND);
      }
      sdb_oci_var_free(var);
   }
}

static SDB_OCI_VAR *paramNewVar(SDB_OCI_CONNECTION *conn, SDB_PARAMP param, CFL_UINT16 dataType, CFL_UINT64 length,
        CFL_UINT32 maxItems, CFL_BOOL bDynamic) {
   SDB_OCI_VAR *var;

   ENTER_FUN_NAME("paramNewVar");
   if (param->handle) {
      freeVar(param);
   }
   if (s_ociSymbols->OCIBindByPos2 != NULL) {
      var = sdb_oci_var_new2(conn, dataType, (CFL_INT64) length, maxItems, bDynamic);
   } else {
      var = sdb_oci_var_new(conn, dataType, (CFL_INT64) length, maxItems, bDynamic);
   }
   if (var != NULL) {
      param->handle = var;
      param->freeHandle = freeVar;
      if (length > 0) {
         param->length = length;
      }
   } else {
      param->handle = NULL;
      param->freeHandle = NULL;
      param->length = 0;
   }
   RETURN var;
}

static CFL_BOOL ociBindVar(SDB_OCI_STMT *stmt, SDB_PARAMP param, SDB_OCI_VAR *var) {
   SDB_LOG_TRACE(("ociBindVar."));
   if (sdb_param_getName(param) == NULL) {
      return sdb_oci_stmt_bindByPos(stmt, var, sdb_param_getPos(param));
   } else {
      return sdb_oci_stmt_bindByName(stmt, var, sdb_param_getNameChar(param), sdb_param_getNameLen(param));
   }
}

static SDB_OCI_VAR *paramGetOCIVar(SDB_OCI_STMT *stmt, SDB_PARAMP param, CFL_UINT16 dataType, CFL_INT64 varLen, CFL_UINT32 arraySize,
        CFL_BOOL isArray) {
   SDB_OCI_VAR *var;

   ENTER_FUN_NAME("paramGetOCIVar");
   var = (SDB_OCI_VAR *) param->handle;
   if (var == NULL || ! sdb_oci_var_fitsContent(var, dataType, varLen, arraySize, sdb_param_isOut(param))) {
      var = paramNewVar(stmt->conn, param, dataType, varLen, arraySize, sdb_param_isOut(param));
      if (var != NULL) {
         if (isArray) {
            sdb_oci_var_setArray(var, CFL_TRUE);
            sdb_oci_var_setItemsCount(var, arraySize);
         }
         if (! ociBindVar(stmt, param, var)) {
            param->handle = NULL;
            param->freeHandle = NULL;
            param->length = 0;
            sdb_oci_var_free(var);
            var = NULL;
         }
      }
   }
   RETURN var;
}

static CFL_BOOL stmtSetParamString(SDB_OCI_STMT *stmt, SDB_PARAMP param, CFL_BOOL execMany) {
   const char *str;
   HB_SIZE strLen;
   SDB_OCI_VAR *var;
   CFL_UINT32 arraySize;
   CFL_UINT32 index;
   PHB_ITEM pItemParam;
   PHB_ITEM pItem;
   CFL_BOOL isArray;
   CFL_BOOL bSuccess;
   CFL_INT64 paramLen;

   ENTER_FUN_NAME("stmtSetParamString");
   pItemParam = sdb_param_getItem(param);
   if (hb_itemType(pItemParam) & HB_IT_ARRAY) {
      arraySize = (CFL_UINT32) hb_arrayLen(pItemParam);
      strLen = 0;
      for (index = 1; index <= arraySize; index++) {
         pItem = hb_arrayGetItemPtr(pItemParam, index);
         if (hb_itemType(pItem) & (HB_IT_STRING | HB_IT_MEMO)) {
            if (hb_itemGetCLen(pItem) > strLen) {
               strLen = hb_itemGetCLen(pItem);
            }
         } else if (! HB_IS_NIL(pItem)) {
            RETURN invalidParamDataType(param);
         }
      }
      isArray = CFL_TRUE;
   } else if (hb_itemType(pItemParam) & (HB_IT_STRING | HB_IT_MEMO)) {
      strLen = hb_itemGetCLen(pItemParam);
      arraySize = 1;
      isArray = CFL_FALSE;
   } else if (HB_IS_NIL(pItemParam)) {
      strLen = 0;
      arraySize = 1;
      isArray = CFL_FALSE;
   } else {
      RETURN invalidParamDataType(param);
   }
   if (strLen < param->length) {
      paramLen = param->length;
   } else if (sdb_param_isOut(param) && strLen == 0) {
      paramLen = VAR_PARAM_OUT_LEN;
   } else {
      paramLen = (CFL_INT64) strLen;
   }
   var = paramGetOCIVar(stmt, param, SDB_SQLT_CHR, paramLen, arraySize, ! execMany && isArray);
   if (var == NULL) {
      RETURN CFL_FALSE;
   }
   bSuccess = CFL_TRUE;
   if (isArray) {
      for (index = 0; index < arraySize && bSuccess; index++) {
         pItem = hb_arrayGetItemPtr(pItemParam, index + 1);
         if (hb_itemType(pItem) & (HB_IT_STRING | HB_IT_MEMO)) {
            strLen = hb_itemGetCLen(pItem);
            str = hb_itemGetCPtr(pItem);
         } else if (HB_IS_NIL(pItem)) {
            str = "";
            strLen = 0;
         } else {
            RETURN invalidParamDataType(param);
         }

         /* Calc trimmed size */
         if (sdb_param_isTrim(param) && strLen > 0) {
            strLen = sdb_util_trimmedLen(str, strLen);
         }

         if (strLen == 0) {
            if (param->isNullable) {
               bSuccess = sdb_oci_var_setNull(var, index);
            } else {
               bSuccess = sdb_oci_var_setString(var, index, " ", 1);
            }
         } else {
            bSuccess = sdb_oci_var_setString(var, index, str, (CFL_UINT32) strLen);
         }
      }
   } else if(HB_IS_NIL(pItemParam)) {
      if (param->isNullable) {
         bSuccess = sdb_oci_var_setNull(var, 0);
      } else {
         bSuccess = sdb_oci_var_setString(var, 0, " ", 1);
      }
   } else {
      str = hb_itemGetCPtr(pItemParam);
      strLen = hb_itemGetCLen(pItemParam);
      /* Calc trimmed size */
      if (sdb_param_isTrim(param) && strLen > 0) {
         strLen = sdb_util_trimmedLen(str, strLen);
      }

      if (strLen == 0) {
         if (param->isNullable) {
            bSuccess = sdb_oci_var_setNull(var, 0);
         } else {
            bSuccess = sdb_oci_var_setString(var, 0, " ", 1);
         }
      } else {
         bSuccess = sdb_oci_var_setString(var, 0, str, (CFL_UINT32) strLen);
      }
   }
   RETURN bSuccess;
}

static CFL_BOOL stmtSetParamNumber(SDB_OCI_STMT *stmt, SDB_PARAMP param, CFL_BOOL execMany) {
   SDB_OCI_VAR *var;
   CFL_BOOL isArray;
   CFL_UINT32 arraySize;
   CFL_UINT32 index;
   PHB_ITEM pItemParam;
   PHB_ITEM pItem;
   CFL_UINT16 dataType;
   CFL_BOOL bSuccess = CFL_TRUE;

   ENTER_FUN_NAME("stmtSetParamNumber");
   pItemParam = sdb_param_getItem(param);
   if (hb_itemType(pItemParam) & HB_IT_ARRAY) {
      arraySize = (CFL_UINT32) hb_arrayLen(pItemParam);
      dataType = SDB_SQLT_INT;
      for (index = 1; index <= arraySize; index++) {
         pItem = hb_arrayGetItemPtr(pItemParam, index);
         if (HB_IS_DOUBLE(pItem)) {
            dataType = SDB_SQLT_BDOUBLE;
            break;
         }
      }
      isArray = CFL_TRUE;
   } else if (HB_IS_NUMINT(pItemParam)) {
      dataType = SDB_SQLT_INT;
      arraySize = 1;
      isArray = CFL_FALSE;
   } else if (HB_IS_NUMERIC(pItemParam)) {
      dataType = SDB_SQLT_BDOUBLE;
      arraySize = 1;
      isArray = CFL_FALSE;
   } else if (HB_IS_NIL(pItemParam)) {
      dataType = SDB_SQLT_INT;
      arraySize = 1;
      isArray = CFL_FALSE;
   } else {
      RETURN invalidParamDataType(param);
   }

   var = paramGetOCIVar(stmt, param, dataType, 0, arraySize, ! execMany && isArray);
   if (var == NULL) {
      RETURN CFL_FALSE;
   }
   if (isArray) {
      for (index = 0; index < arraySize && bSuccess; index++) {
         pItem = hb_arrayGetItemPtr(pItemParam, index + 1);
         if (HB_IS_NIL(pItem)) {
            if (param->isNullable) {
               bSuccess = sdb_oci_var_setNull(var, index);
            } else if (dataType == SDB_SQLT_INT) {
               bSuccess = sdb_oci_var_setInt64(var, index, 0);
            } else {
               bSuccess = sdb_oci_var_setDouble(var, index, 0.0);
            }
         } else if (HB_IS_NUMERIC(pItem)) {
            if (dataType == SDB_SQLT_INT) {
               bSuccess = sdb_oci_var_setInt64(var, index, (CFL_INT64) hb_itemGetNLL(pItem));
            } else {
               bSuccess = sdb_oci_var_setDouble(var, index, hb_itemGetND(pItem));
            }
         } else {
            RETURN invalidParamDataType(param);
         }
      }
   } else if (HB_IS_NIL(pItemParam)) {
      if (param->isNullable) {
         bSuccess = sdb_oci_var_setNull(var, 0);
      } else if (dataType == SDB_SQLT_INT) {
         bSuccess = sdb_oci_var_setInt64(var, 0, 0);
      } else {
         bSuccess = sdb_oci_var_setDouble(var, 0, 0.0);
      }
   } else if (dataType == SDB_SQLT_INT) {
      bSuccess = sdb_oci_var_setInt64(var, 0, (CFL_INT64) hb_itemGetNLL(pItemParam));
   } else {
      bSuccess = sdb_oci_var_setDouble(var, 0, hb_itemGetND(pItemParam));
   }
   RETURN bSuccess;
}

static CFL_BOOL stmtSetParamLogical(SDB_OCI_STMT *stmt, SDB_PARAMP param, CFL_BOOL booleanSupported, CFL_BOOL execMany) {
   SDB_OCI_VAR *var;
   CFL_BOOL isArray;
   CFL_UINT32 arraySize;
   CFL_UINT32 index;
   PHB_ITEM pItemParam;
   PHB_ITEM pItem;
   CFL_BOOL bSuccess = CFL_TRUE;

   ENTER_FUN_NAME("stmtSetParamLogical");
   pItemParam = sdb_param_getItem(param);
   if (hb_itemType(pItemParam) & HB_IT_ARRAY) {
      arraySize = (CFL_UINT32) hb_arrayLen(pItemParam);
      isArray = CFL_TRUE;
   } else if (HB_IS_LOGICAL(pItemParam)) {
      arraySize = 1;
      isArray = CFL_FALSE;
   } else if (HB_IS_NIL(pItemParam)) {
      arraySize = 1;
      isArray = CFL_FALSE;
   } else {
      RETURN invalidParamDataType(param);
   }
   if (stmt->conn->clientVersion.versionNum < 12) {
      booleanSupported = CFL_FALSE;
   }
   if (booleanSupported) {
      var = paramGetOCIVar(stmt, param, SDB_SQLT_BOL, 1, arraySize, ! execMany && isArray);
   } else {
      var = paramGetOCIVar(stmt, param, SDB_SQLT_CHR, 1, arraySize, ! execMany && isArray);
   }
   if (var == NULL) {
      RETURN CFL_FALSE;
   }
   if (isArray) {
      for (index = 0; index < arraySize && bSuccess; index++) {
         pItem = hb_arrayGetItemPtr(pItemParam, index + 1);
         if (HB_IS_NIL(pItem)) {
            if (param->isNullable) {
               bSuccess = sdb_oci_var_setNull(var, index);
            } else if (booleanSupported) {
               bSuccess = sdb_oci_var_setBool(var, index, CFL_FALSE);
            } else {
               bSuccess = sdb_oci_var_setString(var, index, "N", 1);
            }
         } else if (HB_IS_LOGICAL(pItem)) {
            if (booleanSupported) {
               bSuccess = sdb_oci_var_setBool(var, index, hb_itemGetL(pItem) ? CFL_TRUE : CFL_FALSE);
            } else {
               bSuccess = sdb_oci_var_setString(var, index, hb_itemGetL(pItem) ? "Y" : "N", 1);
            }
         } else {
            RETURN invalidParamDataType(param);
         }
      }
   } else if (HB_IS_NIL(pItemParam)) {
      if (param->isNullable) {
         bSuccess = sdb_oci_var_setNull(var, 0);
      } else if (booleanSupported) {
         bSuccess = sdb_oci_var_setBool(var, 0, CFL_FALSE);
      } else {
         bSuccess = sdb_oci_var_setString(var, 0, "N", 1);
      }
   } else if (booleanSupported) {
      bSuccess = sdb_oci_var_setBool(var, 0, hb_itemGetL(pItemParam) ? CFL_TRUE : CFL_FALSE);
   } else {
      bSuccess = sdb_oci_var_setString(var, 0, hb_itemGetL(pItemParam) ? "Y" : "N", 1);
   }
   RETURN bSuccess;
}

static CFL_BOOL stmtSetParamLong(SDB_OCI_STMT *stmt, SDB_PARAMP param, CFL_BOOL execMany) {
   SDB_OCI_VAR *var;
   CFL_UINT32 arraySize;
   CFL_UINT32 index;
   PHB_ITEM pItemParam;
   PHB_ITEM pItem;
   CFL_BOOL isArray;
   HB_SIZE strLen;
   CFL_BOOL bSuccess = CFL_TRUE;

   ENTER_FUN_NAME("stmtSetParamLong");
   pItemParam = sdb_param_getItem(param);
   if (hb_itemType(pItemParam) & HB_IT_ARRAY) {
      arraySize = (CFL_UINT32) hb_arrayLen(pItemParam);
      isArray = CFL_TRUE;
      strLen = 0;
      for (index = 1; index <= arraySize && bSuccess; index++) {
         pItem = hb_arrayGetItemPtr(pItemParam, index);
         if (hb_itemType(pItem) & (HB_IT_STRING | HB_IT_MEMO)) {
            if (hb_itemGetCLen(pItem) > strLen) {
               strLen = hb_itemGetCLen(pItem);
            }
         } else if (! HB_IS_NIL(pItem)) {
            RETURN invalidParamDataType(param);
         }
      }
   } else if (hb_itemType(pItemParam) & (HB_IT_STRING | HB_IT_MEMO)) {
      arraySize = 1;
      isArray = CFL_FALSE;
      strLen = hb_itemGetCLen(pItemParam);
   } else if (HB_IS_NIL(pItemParam)) {
      arraySize = 1;
      isArray = CFL_FALSE;
      strLen = 0;
   } else {
      RETURN invalidParamDataType(param);
   }

   var = paramGetOCIVar(stmt, param, SDB_SQLT_BIN, strLen, arraySize, ! execMany && isArray);
   if (var == NULL) {
      RETURN CFL_FALSE;
   }

   if (isArray) {
      for (index = 0; index < arraySize && bSuccess; index++) {
         pItem = hb_arrayGetItemPtr(pItemParam, index + 1);
         if (hb_itemType(pItem) & (HB_IT_STRING | HB_IT_MEMO)) {
            bSuccess = sdb_oci_var_setString(var, index, hb_itemGetCPtr(pItem), (CFL_UINT32) hb_itemGetCLen(pItem));
         } else if (HB_IS_NIL(pItem)) {
            if (param->isNullable) {
               bSuccess = sdb_oci_var_setNull(var, index);
            } else {
               bSuccess = sdb_oci_var_setString(var, index, "", 0);
            }
         } else {
            RETURN invalidParamDataType(param);
         }
      }
   } else if (HB_IS_NIL(pItemParam)) {
      if (param->isNullable) {
         bSuccess = sdb_oci_var_setNull(var, 0);
      } else  {
         bSuccess = sdb_oci_var_setString(var, 0, "", 0);
      }
   } else {
      bSuccess = sdb_oci_var_setString(var, 0, hb_itemGetCPtr(pItemParam), (CFL_UINT32) hb_itemGetCLen(pItemParam));
   }
   RETURN bSuccess;
}


static CFL_BOOL stmtSetParamRowId(SDB_OCI_STMT *stmt, SDB_PARAMP param, CFL_BOOL execMany) {
   SDB_OCI_VAR *var;
   CFL_UINT32 arraySize;
   CFL_UINT32 index;
   PHB_ITEM pItemParam;
   PHB_ITEM pItem;
   CFL_BOOL isArray;
   void *pRowId;
   CFL_BOOL bSuccess = CFL_TRUE;

   ENTER_FUN_NAME("stmtSetParamRowId");
   pItemParam = sdb_param_getItem(param);
   if (hb_itemType(pItemParam) & HB_IT_ARRAY) {
      arraySize = (CFL_UINT32) hb_arrayLen(pItemParam);
      isArray = CFL_TRUE;
   } else if (hb_itemType(pItemParam) & HB_IT_POINTER) {
      arraySize = 1;
      isArray = CFL_FALSE;
   } else if (HB_IS_NIL(pItemParam)) {
      arraySize = 1;
      isArray = CFL_FALSE;
   } else {
      RETURN invalidParamDataType(param);
   }

   var = paramGetOCIVar(stmt, param, SDB_SQLT_RDD, 0, arraySize, ! execMany && isArray);
   if (var == NULL) {
      RETURN CFL_FALSE;
   }

   if (isArray) {
      for (index = 0; index < arraySize && bSuccess; index++) {
         pItem = hb_arrayGetItemPtr(pItemParam, index + 1);
         if (hb_itemType(pItem) & HB_IT_POINTER) {
            pRowId = hb_itemGetPtr(pItem);
            if (pRowId != NULL) {
               bSuccess = sdb_oci_var_setRowId(var, index, pRowId);
            } else {
               bSuccess = invalidParamDataType(param);
            }
         } else if (HB_IS_NIL(pItem)) {
            if (param->isNullable) {
               bSuccess = sdb_oci_var_setNull(var, index);
            } else {
               bSuccess = invalidParamDataType(param);
            }
         } else {
            bSuccess = invalidParamDataType(param);
         }
      }
   } else if (hb_itemType(pItemParam) & HB_IT_POINTER) {
      pRowId = hb_itemGetPtr(pItemParam);
      if (pRowId != NULL) {
         bSuccess = sdb_oci_var_setRowId(var, 0, pRowId);
      } else {
         bSuccess = invalidParamDataType(param);
      }
   } else if (HB_IS_NIL(pItemParam) && param->isNullable) {
      bSuccess = sdb_oci_var_setNull(var, 0);
   } else {
      bSuccess = invalidParamDataType(param);
   }
   RETURN bSuccess;
}

static CFL_BOOL stmtSetParamDate(SDB_OCI_STMT *stmt, SDB_PARAMP param, CFL_BOOL execMany) {
   SDB_OCI_VAR *var;
   CFL_BOOL isArray;
   CFL_UINT32 arraySize;
   CFL_UINT32 index;
   PHB_ITEM pItemParam;
   PHB_ITEM pItem;
   CFL_BOOL bSuccess = CFL_TRUE;

   ENTER_FUN_NAME("stmtSetParamDate");
   pItemParam = sdb_param_getItem(param);
   if (hb_itemType(pItemParam) & HB_IT_ARRAY) {
      arraySize = (CFL_UINT32) hb_arrayLen(pItemParam);
      isArray = CFL_TRUE;
   } else if (HB_IS_DATE(pItemParam)) {
      arraySize = 1;
      isArray = CFL_FALSE;
#ifdef __HBR__
   } else if (HB_IS_TIMESTAMP(pItemParam)) {
      arraySize = 1;
      isArray = CFL_FALSE;
#endif
   } else if(HB_IS_NIL(pItemParam)) {
      arraySize = 1;
      isArray = CFL_FALSE;
   } else {
      RETURN invalidParamDataType(param);
   }

   var = paramGetOCIVar(stmt, param, SDB_SQLT_ODT, 0, arraySize, ! execMany && isArray);
   if (var == NULL) {
      RETURN CFL_FALSE;
   }

   if (isArray) {
      for (index = 0; index < arraySize && bSuccess; index++) {
         pItem = hb_arrayGetItemPtr(pItemParam, index + 1);
         if (HB_IS_NIL(pItem)) {
            if (param->isNullable) {
               bSuccess = sdb_oci_var_setNull(var, index);
            } else {
               bSuccess = sdb_oci_var_setDate(var, index, -4712, 1, 1, 0, 0, 0);
            }
         } else if (HB_IS_DATE(pItem)) {
            if (sdb_util_itemIsEmpty(pItem)) {
               if (param->isNullable) {
                  bSuccess = sdb_oci_var_setNull(var, index);
               } else {
                  bSuccess = sdb_oci_var_setDate(var, index, -4712, 1, 1, 0, 0, 0);
               }
            } else {
               int iYear, iMonth, iDay;
               hb_dateDecode(hb_itemGetDL(pItem), &iYear, &iMonth, &iDay);
               bSuccess = sdb_oci_var_setDate(var, index, (CFL_INT16) iYear, (CFL_UINT8) iMonth, (CFL_UINT8) iDay, 0, 0, 0);
            }
#ifdef __HBR__
         } else if (HB_IS_TIMESTAMP(pItem)) {
            if (sdb_util_itemIsEmpty(pItem)) {
               if (param->isNullable) {
                  bSuccess = sdb_oci_var_setNull(var, index);
               } else {
                  bSuccess = sdb_oci_var_setDate(var, index, -4712, 1, 1, 0, 0, 0);
               }
            } else {
               int iYear, iMonth, iDay;
               int iHour, iMin, iSec, iMSec;
               hb_timeStampUnpack(hb_itemGetTD(pItem), &iYear, &iMonth, &iDay, &iHour, &iMin, &iSec, &iMSec);
               bSuccess = sdb_oci_var_setDate(var, index, (CFL_INT16) iYear, (CFL_UINT8) iMonth, (CFL_UINT8) iDay, 0, 0, 0);
            }
#endif
         } else {
            bSuccess = invalidParamDataType(param);
         }
      }
   } else if (HB_IS_NIL(pItemParam)) {
      if (param->isNullable) {
         bSuccess = sdb_oci_var_setNull(var, 0);
      } else {
         bSuccess = sdb_oci_var_setDate(var, 0, -4712, 1, 1, 0, 0, 0);
      }
   } else if (HB_IS_DATE(pItemParam)) {
      if (sdb_util_itemIsEmpty(pItemParam)) {
         if (param->isNullable) {
            bSuccess = sdb_oci_var_setNull(var, 0);
         } else {
            bSuccess = sdb_oci_var_setDate(var, 0, -4712, 1, 1, 0, 0, 0);
         }
      } else {
         int iYear, iMonth, iDay;
         hb_dateDecode(hb_itemGetDL(pItemParam), &iYear, &iMonth, &iDay);
         bSuccess = sdb_oci_var_setDate(var, 0, (CFL_INT16) iYear, (CFL_UINT8) iMonth, (CFL_UINT8) iDay, 0, 0, 0);
      }
#ifdef __HBR__
   } else if (HB_IS_TIMESTAMP(pItemParam)) {
      if (sdb_util_itemIsEmpty(pItemParam)) {
         if (param->isNullable) {
            bSuccess = sdb_oci_var_setNull(var, 0);
         } else {
            bSuccess = sdb_oci_var_setDate(var, 0, -4712, 1, 1, 0, 0, 0);
         }
      } else {
         int iYear, iMonth, iDay;
         int iHour, iMin, iSec, iMSec;
         hb_timeStampUnpack(hb_itemGetTD(pItemParam), &iYear, &iMonth, &iDay, &iHour, &iMin, &iSec, &iMSec);
         bSuccess = sdb_oci_var_setDate(var, 0, (CFL_INT16) iYear, (CFL_UINT8) iMonth, (CFL_UINT8) iDay, 0, 0, 0);
      }
#endif
   }
   RETURN bSuccess;
}


static CFL_BOOL stmtSetParamTimestamp(SDB_OCI_STMT *stmt, SDB_PARAMP param, CFL_BOOL execMany) {
   SDB_OCI_VAR *var;
   CFL_BOOL isArray;
   CFL_UINT32 arraySize;
   CFL_UINT32 index;
   PHB_ITEM pItemParam;
   PHB_ITEM pItem;
   CFL_BOOL bSuccess = CFL_TRUE;

   ENTER_FUN_NAME("stmtSetParamTimestamp");
   pItemParam = sdb_param_getItem(param);
   if (hb_itemType(pItemParam) & HB_IT_ARRAY) {
      arraySize = (CFL_UINT32) hb_arrayLen(pItemParam);
      isArray = CFL_TRUE;
   } else if (HB_IS_DATE(pItemParam)) {
      arraySize = 1;
      isArray = CFL_FALSE;
#ifdef __HBR__
   } else if (HB_IS_TIMESTAMP(pItemParam)) {
      arraySize = 1;
      isArray = CFL_FALSE;
#endif
   } else if(HB_IS_NIL(pItemParam)) {
      arraySize = 1;
      isArray = CFL_FALSE;
   } else {
      RETURN invalidParamDataType(param);
   }

   var = paramGetOCIVar(stmt, param, SDB_SQLT_ODT, 0, arraySize, ! execMany && isArray);
   if (var == NULL) {
      RETURN CFL_FALSE;
   }

   if (isArray) {
      for (index = 0; index < arraySize && bSuccess; index++) {
         pItem = hb_arrayGetItemPtr(pItemParam, index + 1);
         if (HB_IS_NIL(pItem)) {
            if (param->isNullable) {
               bSuccess = sdb_oci_var_setNull(var, index);
            } else {
               bSuccess = sdb_oci_var_setDate(var, index, 0, 0, 0, 0, 0, 0);
            }
         } else if (HB_IS_DATE(pItem)) {
            int iYear, iMonth, iDay;
            hb_dateDecode(hb_itemGetDL(pItem), &iYear, &iMonth, &iDay);
            bSuccess = sdb_oci_var_setDate(var, index, (CFL_INT16) iYear, (CFL_UINT8) iMonth, (CFL_UINT8) iDay, 0, 0, 0);
   #ifdef __HBR__
         } else if (HB_IS_TIMESTAMP(pItem)) {
            int iYear, iMonth, iDay;
            int iHour, iMin, iSec, iMSec;
            hb_timeStampUnpack(hb_itemGetTD(pItem), &iYear, &iMonth, &iDay, &iHour, &iMin, &iSec, &iMSec);
            bSuccess = sdb_oci_var_setDate(var, index, (CFL_INT16) iYear, (CFL_UINT8) iMonth, (CFL_UINT8) iDay,
                    (CFL_UINT8) iHour, (CFL_UINT8) iMin, (CFL_UINT8) iSec);
   #endif
         } else {
            bSuccess = invalidParamDataType(param);
         }
      }
   } else if (HB_IS_NIL(pItemParam)) {
      if (param->isNullable) {
         bSuccess = sdb_oci_var_setNull(var, 0);
      } else {
         bSuccess = sdb_oci_var_setDate(var, 0, 0, 0, 0, 0, 0, 0);
      }
   } else if (HB_IS_DATE(pItemParam)) {
      int iYear, iMonth, iDay;
      hb_dateDecode(hb_itemGetDL(pItemParam), &iYear, &iMonth, &iDay);
      bSuccess = sdb_oci_var_setDate(var, 0, (CFL_INT16) iYear, (CFL_UINT8) iMonth, (CFL_UINT8) iDay, 0, 0, 0);
#ifdef __HBR__
   } else if (HB_IS_TIMESTAMP(pItemParam)) {
      int iYear, iMonth, iDay;
      int iHour, iMin, iSec, iMSec;
      hb_timeStampUnpack(hb_itemGetTD(pItemParam), &iYear, &iMonth, &iDay, &iHour, &iMin, &iSec, &iMSec);
      bSuccess = sdb_oci_var_setDate(var, 0, (CFL_INT16) iYear, (CFL_UINT8) iMonth, (CFL_UINT8) iDay,
              (CFL_UINT8) iHour, (CFL_UINT8) iMin, (CFL_UINT8) iSec);
#endif
   }
   RETURN bSuccess;
}


static CFL_BOOL stmtSetParamLob(SDB_OCI_STMT *stmt, SDB_PARAMP param, CFL_BOOL execMany) {
   SDB_OCI_VAR *var;
   CFL_UINT32 arraySize;
   CFL_UINT32 index;
   PHB_ITEM pItemParam;
   PHB_ITEM pItem;
   CFL_BOOL isArray;
   SDB_LOBP pLob;
   CFL_BOOL bSuccess = CFL_TRUE;

   ENTER_FUN_NAME("stmtSetParamLob");
   pItemParam = sdb_param_getItem(param);
   if (hb_itemType(pItemParam) & HB_IT_ARRAY) {
      arraySize = (CFL_UINT32) hb_arrayLen(pItemParam);
      isArray = CFL_TRUE;
   } else if (hb_itemType(pItemParam) & HB_IT_POINTER) {
      arraySize = 1;
      isArray = CFL_FALSE;
   } else if (hb_itemType(pItemParam) & (HB_IT_STRING | HB_IT_MEMO)) {
      arraySize = 1;
      isArray = CFL_FALSE;
   } else if (HB_IS_NIL(pItemParam)) {
      arraySize = 1;
      isArray = CFL_FALSE;
   } else {
      RETURN invalidParamDataType(param);
   }

   if (param->type == SDB_CLP_BLOB || param->type == SDB_CLP_IMAGE) {
      var = paramGetOCIVar(stmt, param, SDB_SQLT_BLOB, 0, arraySize, ! execMany && isArray);
   } else {
      var = paramGetOCIVar(stmt, param, SDB_SQLT_CLOB, 0, arraySize, ! execMany && isArray);
   }
   if (var == NULL) {
      RETURN CFL_FALSE;
   }

   if (isArray) {
      for (index = 0; index < arraySize && bSuccess; index++) {
         pItem = hb_arrayGetItemPtr(pItemParam, index + 1);
         if (hb_itemType(pItem) & HB_IT_POINTER) {
            pLob = sdb_lob_itemGet(pItem);
            if (pLob != NULL) {
               bSuccess = sdb_oci_var_setLob(var, index, pLob->handle);
            } else {
               bSuccess = invalidParamDataType(param);
            }
         } else if (hb_itemType(pItem) & (HB_IT_STRING | HB_IT_MEMO)) {
            bSuccess = sdb_oci_var_setString(var, index, hb_itemGetCPtr(pItem), (CFL_UINT32) hb_itemGetCLen(pItem));
         } else if (HB_IS_NIL(pItem)) {
            if (param->isNullable) {
               bSuccess = sdb_oci_var_setNull(var, index);
            } else {
               bSuccess = sdb_oci_var_setString(var, index, "", 0);
            }
         } else {
            bSuccess = invalidParamDataType(param);
         }
      }
   } else if (hb_itemType(pItemParam) & HB_IT_POINTER) {
      pLob = sdb_lob_itemGet(pItemParam);
      if (pLob != NULL) {
         bSuccess = sdb_oci_var_setLob(var, 0, pLob->handle);
      } else {
         bSuccess = invalidParamDataType(param);
      }
   } else if (hb_itemType(pItemParam) & (HB_IT_STRING | HB_IT_MEMO)) {
      bSuccess = sdb_oci_var_setString(var, 0, hb_itemGetCPtr(pItemParam), (CFL_UINT32) hb_itemGetCLen(pItemParam));
   } else if (HB_IS_NIL(pItemParam)) {
      if (param->isNullable) {
         bSuccess = sdb_oci_var_setNull(var, 0);
      } else {
         bSuccess = sdb_oci_var_setString(var, 0, "", 0);
      }
   }
   RETURN bSuccess;
}

static CFL_BOOL stmtSetParamStatement(SDB_OCI_STMT *stmt, SDB_PARAMP param, CFL_BOOL execMany) {
   SDB_OCI_VAR *var;
   CFL_BOOL isArray;
   CFL_UINT32 arraySize;
   CFL_UINT32 index;
   PHB_ITEM pItemParam;
   PHB_ITEM pItem;
   SDB_STATEMENTP pStmt;
   CFL_BOOL bSuccess = CFL_TRUE;

   ENTER_FUN_NAME("stmtSetParamStatement");
   pItemParam = sdb_param_getItem(param);
   if (hb_itemType(pItemParam) & HB_IT_ARRAY) {
      arraySize = (CFL_UINT32) hb_arrayLen(pItemParam);
      isArray = CFL_TRUE;
   } else if (HB_IS_POINTER(pItemParam)) {
      arraySize = 1;
      isArray = CFL_FALSE;
   } else if (HB_IS_NIL(pItemParam)) {
      arraySize = 1;
      isArray = CFL_FALSE;
   } else {
      RETURN invalidParamDataType(param);
   }

   var = paramGetOCIVar(stmt, param, SDB_SQLT_RSET, 0, arraySize, ! execMany && isArray);
   if (var == NULL) {
      RETURN CFL_FALSE;
   }

   if (isArray) {
      for (index = 0; index < arraySize && bSuccess; index++) {
         pItem = hb_arrayGetItemPtr(pItemParam, index + 1);
          if (HB_IS_POINTER(pItem)) {
            pStmt = sdb_stmt_itemGet(pItem);
            if (pStmt != NULL) {
               bSuccess = sdb_oci_var_setStmt(var, index, pStmt->handle);
            } else {
               bSuccess = invalidParamDataType(param);
            }
         } else if (HB_IS_NIL(pItem)) {
            bSuccess = sdb_oci_var_setNull(var, index);
         } else {
            bSuccess = invalidParamDataType(param);
         }
      }
   } else if (HB_IS_NIL(pItemParam)) {
      bSuccess = sdb_oci_var_setNull(var, 0);
   } else {
      pStmt = sdb_stmt_itemGet(pItemParam);
      if (pStmt != NULL) {
         bSuccess = sdb_oci_var_setStmt(var, 0, pStmt->handle);
      } else {
         bSuccess = invalidParamDataType(param);
      }
   }
   RETURN bSuccess;
}

static CFL_BOOL stmtSetParam(SDB_OCI_STMT *stmt, SDB_PARAMP param, CFL_BOOL booleanSupported, CFL_BOOL execMany) {
   CFL_BOOL fSuccess;

   SDB_LOG_DEBUG(("stmtSetParam: name=%s, pos=%d, hb_type=%s, sdb_type=%d, value=%s, out=%s, support bool=%s, trim=%s, nullable=%s",
                  sdb_param_getNameChar(param),
                  param->pos,
                  hb_itemTypeStr(sdb_param_getItem(param)),
                  param->type,
                  ITEM_STR(sdb_param_getItem(param)),
                  BOOL_STR(sdb_param_isOut(param)),
                  BOOL_STR(booleanSupported),
                  BOOL_STR(sdb_param_isTrim(param)),
                  BOOL_STR(sdb_param_isNullable(param))));

   switch (param->type) {
      case SDB_CLP_CHARACTER:
         fSuccess = stmtSetParamString(stmt, param, execMany);
         break;

      case SDB_CLP_FLOAT:
      case SDB_CLP_INTEGER:
      case SDB_CLP_BIGINT:
      case SDB_CLP_DOUBLE:
      case SDB_CLP_NUMERIC:
         fSuccess = stmtSetParamNumber(stmt, param, execMany);
         break;

      case SDB_CLP_LOGICAL:
         fSuccess = stmtSetParamLogical(stmt, param, booleanSupported, execMany);
         break;

      case SDB_CLP_MEMO_LONG:
      case SDB_CLP_LONG_RAW:
         fSuccess = stmtSetParamLong(stmt, param, execMany);
         break;

      case SDB_CLP_ROWID:
         fSuccess = stmtSetParamRowId(stmt, param, execMany);
         break;

      case SDB_CLP_DATE:
         fSuccess = stmtSetParamDate(stmt, param, execMany);
         break;

      case SDB_CLP_TIMESTAMP:
         fSuccess = stmtSetParamTimestamp(stmt, param, execMany);
         break;

      case SDB_CLP_IMAGE:
      case SDB_CLP_BLOB:
      case SDB_CLP_CLOB:
         fSuccess = stmtSetParamLob(stmt, param, execMany);
         break;

      case SDB_CLP_CURSOR:
         fSuccess = stmtSetParamStatement(stmt, param, execMany);
         break;

      default:
         if (sdb_param_isNamed(param)) {
            sdb_thread_setError(SDB_ERROR_TYPE_SDB, SDB_ERROR_INVALID_DATATYPE, "Internal error: invalid data type for param :%s.",
                    sdb_param_getNameChar(param));
         } else {
            sdb_thread_setError(SDB_ERROR_TYPE_SDB, SDB_ERROR_INVALID_DATATYPE, "Internal error: invalid data type for param %u.",
                    sdb_param_getPos(param));
         }
         fSuccess = CFL_FALSE;
         break;
   }
   return fSuccess;
}

static CFL_BOOL stmtSetParams(SDB_OCI_STMT *stmt, SDB_PARAMLISTP params, CFL_BOOL booleanSupported, CFL_BOOL execMany) {
   ENTER_FUN_NAME("stmtSetParams");
   if (params) {
      CFL_UINT32 len = sdb_param_listLength(params);
      CFL_UINT32 i;
      for (i = 1; i <= len; i++) {
         SDB_PARAMP param = sdb_param_listGetPos(params, i);
         if (!stmtSetParam(stmt, param, booleanSupported, execMany)) {
            RETURN CFL_FALSE;
         }
      }
   }
   RETURN CFL_TRUE;
}

static void paramGetValuePos(SDB_CONNECTIONP connection, SDB_PARAMP param, CFL_UINT32 iVarPos, PHB_ITEM pItem) {
   SDB_OCI_VAR *var;
   CFL_UINT32 iElements;
   CFL_UINT32 i;
   CFL_BOOL isArray;
   PHB_ITEM pValue;

   ENTER_FUN_NAME("paramGetValuePos");
   SDB_LOG_DEBUG(("paramGetValuePos. pos=%lu", iVarPos));
   var = (SDB_OCI_VAR *) param->handle;
   if (! sdb_oci_var_isNull(var, iVarPos)) {
      isArray = sdb_oci_var_isArray(var) > 1 || HB_IS_ARRAY(pItem);
      if (isArray) {
         iElements = sdb_oci_var_itemsCount(var);
         hb_arrayNew(pItem, iElements);
         pValue = hb_itemNew(NULL);
      } else {
         iElements = 1;
         pValue = pItem;
      }
      for (i = 0; i < iElements; i++) {
         switch (var->dataType) {
            case SDB_SQLT_RDD:
               {
                  SDB_OCI_ROWID *rowId = sdb_oci_var_getRowId(var, i);
                  hb_itemPutPtr(pValue, rowId);
                  if (rowId != NULL) {
                     sdb_oci_rowid_incRef(rowId);
                  }
               }
               break;

            case SDB_SQLT_STR:
            case SDB_SQLT_CHR:
               if (param->type == SDB_CLP_LOGICAL) {
                  hb_itemPutL(pValue, hb_strnicmp(sdb_oci_var_getString(var, i), "Y", sdb_oci_var_getStringLen(var, i)) == 0 ? HB_TRUE : HB_FALSE);
               } else {
                  hb_itemPutCL(pValue, sdb_oci_var_getString(var, i), sdb_oci_var_getStringLen(var, i));
               }
               break;

            case SDB_SQLT_BDOUBLE:
               hb_itemPutND(pValue, sdb_oci_var_getDouble(var, i));
               break;

            case SDB_SQLT_INT:
               hb_itemPutNLL(pValue, (HB_LONGLONG) sdb_oci_var_getInt64(var, i));
               break;

            case SDB_SQLT_DAT:
            case SDB_SQLT_ODT:
               {
                  CFL_INT16 year;
                  CFL_INT8 month, day, hour, min, sec;
                  sdb_oci_var_getDate(var, i, &year, &month, &day, &hour, &min, &sec);
                  if (param->type == SDB_CLP_DATE) {
                     hb_itemPutD(pValue, year, month, day);
                  } else {
                     #ifdef __HBR__
                        hb_itemPutTDT(pValue, hb_dateEncode(year, month, day), hb_timeEncode(hour, min, sec, 0));
                     #else
                        hb_itemPutD(pValue, year, month, day);
                     #endif
                  }
               }
               break;

            case SDB_SQLT_BOL:
               hb_itemPutL(pValue, sdb_oci_var_getBool(var, i));
               break;

            case SDB_SQLT_CLOB:
               {
                  SDB_OCI_LOB *lob = sdb_oci_var_getLob(var, i);
                  sdb_lob_itemPut(pValue, sdb_lob_new(connection, SDB_CLP_CLOB, lob));
                  if (lob != NULL) {
                     sdb_oci_lob_incRef(lob);
                  }
               }
               break;

            case SDB_SQLT_BLOB:
               {
                  SDB_OCI_LOB *lob = sdb_oci_var_getLob(var, i);
                  sdb_lob_itemPut(pValue, sdb_lob_new(connection, SDB_CLP_BLOB, lob));
                  if (lob != NULL) {
                     sdb_oci_lob_incRef(lob);
                  }
               }
               break;

            case SDB_SQLT_LNG:
               hb_itemPutCL(pValue, sdb_oci_var_getString(var, i), sdb_oci_var_getStringLen(var, i));
               break;

            case SDB_SQLT_RSET:
               {
                  SDB_OCI_STMT *ociStmt = sdb_oci_var_getStmt(var, i);
                  SDB_STATEMENTP pStmt = sdb_stmt_new(connection, ociStmt);
                  if (ociStmt != NULL) {
                     sdb_oci_stmt_incRef(ociStmt);
                  }
                  pStmt->isCursor = CFL_TRUE;
                  sdb_stmt_setNumCols(pStmt, sdb_oci_stmt_getColCount(ociStmt));
                  sdb_stmt_setType(pStmt, sdb_oci_stmt_getType(ociStmt));
                  sdb_stmt_itemPut(pValue, pStmt);
               }
               break;

            default:
               hb_itemClear(pValue);
               break;
         }
         if (isArray) {
            hb_arraySetForward(pItem, i + 1, pValue);
         }
      }
      if (isArray) {
         hb_itemRelease(pValue);
      }
   } else {
      hb_itemClear(pItem);
   }
   RETURN;
}

static void paramGetValue(SDB_CONNECTIONP connection, SDB_PARAMP param, CFL_UINT32 len) {
   CFL_UINT32 varPos;
   PHB_ITEM pItem;

   ENTER_FUN_NAME("paramGetValue");
   SDB_LOG_DEBUG(("paramGetValue: name=%s len=%lu", sdb_param_getNameChar(param), len));
   pItem = sdb_param_getItem(param);
   if (len > 1) {
      hb_arrayNew(pItem, len);
      for (varPos = 0; varPos < len; varPos++) {
         paramGetValuePos(connection, param, varPos, hb_arrayGetItemPtr(pItem, varPos + 1));
      }
   } else {
      paramGetValuePos(connection, param, 0, pItem);
   }
   RETURN;
}

static void paramListGetValues(SDB_CONNECTIONP connection, SDB_PARAMLISTP params, CFL_UINT32 len) {
   ENTER_FUN_NAME("paramListGetValues");
   if (params != NULL) {
      CFL_UINT32 parCount = sdb_param_listLength(params);
      CFL_UINT32 i;
      for (i = 1; i <= parCount; i++) {
         SDB_PARAMP param = sdb_param_listGetPos(params, i);
         if (sdb_param_isOut(param)) {
            paramGetValue(connection, param, len);
            sdb_param_updateBind(param);
         }
      }
   }
   RETURN;
}

static void * sdb_oci_prepareStatement(SDB_CONNECTIONP conn, const char *sql, CFL_UINT32 sqlLen) {
   SDB_OCI_CONNECTION *cn = (SDB_OCI_CONNECTION *) conn->handle;
   void *errorHandle = sdb_oci_getErrorHandle();
   SDB_OCI_STMT *stmt;
   void *stmtHandle = NULL;
   int status;

   SDB_LOG_TRACE(("sdb_oci_prepareStatement: len=%u sql=%s", sqlLen, sql));

   status = s_ociSymbols->OCIStmtPrepare2(cn->serviceHandle, &stmtHandle, errorHandle, sql, sqlLen, NULL, 0,
                                         SDB_OCI_NTV_SYNTAX, SDB_OCI_DEFAULT);
   CHECK_STATUS_RETURN(status, errorHandle, "Error preparing statement", NULL);
   stmt = sdb_oci_stmt_new(cn, stmtHandle);
   if (stmt == NULL) {
      s_ociSymbols->OCIHandleFree(stmtHandle, SDB_OCI_HTYPE_STMT);
   }
   return stmt;
}

static CFL_BOOL sdb_oci_executeStatement(SDB_STATEMENTP pStatement, CFL_UINT64 *ulAffectedRows) {
   SDB_OCI_STMT *stmt = (SDB_OCI_STMT *) pStatement->handle;
   SDB_PARAMLISTP params;

   SDB_LOG_TRACE(("sdb_oci_executeStatement. stmt=%p handle=%p", pStatement, stmt));

   pStatement->fetchCount = 0;
   pStatement->isEof = CFL_FALSE;
   *ulAffectedRows = 0;
   if (IS_QUERY(stmt) && ! sdb_oci_stmt_setPrefetchSize(stmt, pStatement->fetchSize)) {
      return CFL_FALSE;
   }
   params = sdb_stmt_getParams(pStatement);
   if (! stmtSetParams(stmt, params, (CFL_BOOL) (IS_PLSQL(stmt) && ! pStatement->isLogicalParamAsChar1), CFL_FALSE)) {
      return CFL_FALSE;
   }
   if (! sdb_oci_stmt_execute(stmt, IS_QUERY(stmt) ? 0 : 1)) {
      return CFL_FALSE;
   }
   ++pStatement->execCount;
   if (IS_QUERY(stmt)) {
      sdb_stmt_setNumCols(pStatement, sdb_oci_stmt_getColCount(stmt));
   } else {
      commitIfAuto(pStatement->connection, stmt);
      paramListGetValues(pStatement->connection, params, 1);
      *ulAffectedRows = sdb_oci_stmt_getRowCount(stmt);
   }
   return CFL_TRUE;
}

static CFL_BOOL paramListItemsLen(SDB_PARAMLISTP params, CFL_UINT32 *totalItens) {
   SDB_PARAMP param;
   HB_SIZE arrayLen = 0;
   PHB_ITEM pItemParam;
   CFL_UINT32 paramPos = 1;

   ENTER_FUN_NAME("paramListItemsLen");
   param = sdb_param_listGetPos(params, paramPos++);
   if (param != NULL) {
      pItemParam = sdb_param_getItem(param);
      if (hb_itemType(pItemParam) & HB_IT_ARRAY) {
         arrayLen = hb_arrayLen(pItemParam);
      } else {
         sdb_thread_setError(SDB_ERROR_TYPE_SDB, SDB_ERROR_INVALID_DATATYPE, "Param %s is not an array.",
                             sdb_param_getNameChar(param));
         RETURN CFL_FALSE;
      }
      param = sdb_param_listGetPos(params, paramPos++);
      while (param) {
         pItemParam = sdb_param_getItem(param);
         if (hb_itemType(pItemParam) & HB_IT_ARRAY) {
            if (hb_arrayLen(pItemParam) != arrayLen) {
               if (sdb_param_getNameLen(param) > 0) {
                  sdb_thread_setError(SDB_ERROR_TYPE_SDB, SDB_ERROR_INVALID_DATATYPE,
                                      "All params must have the same number of elements: found %u in pos 1 and %u in param '%s'.",
                                      arrayLen, (pItemParam), sdb_param_getNameChar(param));
               } else {
                  sdb_thread_setError(SDB_ERROR_TYPE_SDB, SDB_ERROR_INVALID_DATATYPE,
                                      "All params must have the same number of elements: found %u in pos 1 and %u in pos %u.",
                                      arrayLen, (pItemParam), sdb_param_getPos(param));
               }
               RETURN CFL_FALSE;
            }
         } else {
            sdb_thread_setError(SDB_ERROR_TYPE_SDB, SDB_ERROR_INVALID_DATATYPE, "Param %s is not an array.",
                                sdb_param_getNameChar(param));
            RETURN CFL_FALSE;
         }
         param = sdb_param_listGetPos(params, paramPos++);
      }
   }
   *totalItens = (CFL_UINT32) arrayLen;
   RETURN CFL_TRUE;
}

static CFL_BOOL sdb_oci_executeStatementMany(SDB_STATEMENTP pStatement, CFL_UINT64 *ulAffectedRows) {
   SDB_OCI_STMT *stmt = (SDB_OCI_STMT *) pStatement->handle;
   SDB_PARAMLISTP params;
   CFL_UINT32 arrayLen;

   ENTER_FUN_NAME("sdb_oci_executeStatementMany");

   pStatement->fetchCount = 0;
   pStatement->isEof = CFL_FALSE;
   params = sdb_stmt_getParams(pStatement);

   *ulAffectedRows = 0;
   if (IS_QUERY(stmt)) {
      sdb_thread_setError(SDB_ERROR_TYPE_SDB, SDB_OCI_ERROR_EXEC_STMT, "Statement cannot be a query.");
      RETURN CFL_FALSE;
   }
   ++pStatement->execCount;
   if (! paramListItemsLen(params, &arrayLen)) {
      RETURN CFL_FALSE;
   }
   if (arrayLen == 0) {
      *ulAffectedRows = 0;
      RETURN CFL_TRUE;
   }
   if (! stmtSetParams(stmt, params, (CFL_BOOL) (IS_PLSQL(stmt) && ! pStatement->isLogicalParamAsChar1), CFL_TRUE)) {
      RETURN CFL_FALSE;
   }
   if (! sdb_oci_stmt_execute(stmt, arrayLen)) {
      RETURN CFL_FALSE;
   }
   commitIfAuto(pStatement->connection, stmt);
   paramListGetValues(pStatement->connection, params, arrayLen);
   *ulAffectedRows = sdb_oci_stmt_getRowCount(stmt);
   RETURN CFL_TRUE;
}

static CFL_BOOL sdb_oci_getClientVersion(SDB_CONNECTIONP conn, CFL_STRP version) {
   SDB_OCI_CONNECTION *cn = (SDB_OCI_CONNECTION *) conn->handle;
   ENTER_FUN_NAME("sdb_oci_getClientVersion");
   cfl_str_setFormat(version, "%d.%d.%d.%d.%d",
           cn->clientVersion.versionNum, cn->clientVersion.releaseNum,
           cn->clientVersion.updateNum, cn->clientVersion.portReleaseNum,
           cn->clientVersion.portUpdateNum);
   RETURN CFL_TRUE;
}

static CFL_BOOL lockId(SDB_AREAP pSDBArea, CFL_INT32 oraLockMode) {
   CFL_INT32 lockStatus = -1;
   SDB_STATEMENTP pStmt;
   CFL_UINT64 affectedRows;

   ENTER_FUN_NAME("lockId");
   SDB_LOG_DEBUG(("lockId: name=%s.%s mode=%d",
           cfl_str_getPtr(pSDBArea->table->clpSchema->name),
           cfl_str_getPtr(pSDBArea->table->clpName),
           oraLockMode));
   pStmt = PREPARE_STMT_CONST(pSDBArea->connection, "call fnc_sdb_lockid(:lockId, :lckMode) into :res");
   if (pStmt != NULL) {
      sdb_stmt_setString(pStmt, "lockId", sdb_area_getLockId(pSDBArea), CFL_FALSE);
      sdb_stmt_setInt32(pStmt, "lckMode", oraLockMode, CFL_FALSE);
      sdb_stmt_setInt32(pStmt, "res", 0, CFL_TRUE);
      if (sdb_stmt_execute(pStmt, &affectedRows)) {
         lockStatus = sdb_stmt_getInt32(pStmt, "res");
      }
      sdb_stmt_free(pStmt);
   }
   RETURN lockStatus == 0;
}

static CFL_BOOL unlockId(SDB_AREAP pSDBArea) {
   int lockStatus;
   SDB_STATEMENTP pStmt;
   CFL_UINT64 affectedRows;

   ENTER_FUN_NAME("unlockId");
   SDB_LOG_DEBUG(("unlock id: name=%s.%s", cfl_str_getPtr(pSDBArea->table->clpSchema->name), cfl_str_getPtr(pSDBArea->table->clpName)));
   pStmt = PREPARE_STMT_CONST(pSDBArea->connection, "call fnc_sdb_unlockid(:lockId) into :res");
   if (pStmt != NULL) {
      sdb_stmt_setString(pStmt, "lockId", sdb_area_getLockId(pSDBArea), CFL_FALSE);
      sdb_stmt_setInt32(pStmt, "res", 0, CFL_TRUE);
      if (sdb_stmt_execute(pStmt, &affectedRows)) {
         lockStatus = sdb_stmt_getInt32(pStmt, "res");
      } else {
         lockStatus = -1;
      }
      sdb_stmt_free(pStmt);
   } else {
      lockStatus = -1;
   }
   RETURN lockStatus == 0;
}

static CFL_BOOL sdb_oci_lockTable(SDB_AREAP pSDBArea, int lockMode) {
   CFL_INT32 oraLockMode;

   SDB_LOG_DEBUG(("sdb_oci_lockTable: name=%s.%s mode=%d", cfl_str_getPtr(pSDBArea->table->clpSchema->name),
           cfl_str_getPtr(pSDBArea->table->clpName), lockMode));
   switch (lockMode) {
      case SDB_LOCK_SHARED:
         oraLockMode = ORA_LCK_SS_MODE;
         break;

      case SDB_LOCK_EXCLUSIVE:
         oraLockMode = ORA_LCK_X_MODE;
         break;

      case SDB_LOCK_LOCK_ALL:
         oraLockMode = ORA_LCK_SSX_MODE;
         break;
      default:
         return CFL_FALSE;
   }
   return lockId(pSDBArea, oraLockMode);
}

static CFL_BOOL sdb_oci_unlockTable(SDB_AREAP pSDBArea) {
   SDB_LOG_DEBUG(("sdb_oci_unlockTable: name=%s.%s", cfl_str_getPtr(pSDBArea->table->clpSchema->name),
           cfl_str_getPtr(pSDBArea->table->clpName)));
   return unlockId(pSDBArea);
}

static CFL_BOOL sdb_oci_setPreFetchSize(SDB_STATEMENTP pStatement, CFL_UINT16 fetchSize) {
   SDB_LOG_DEBUG(("sdb_oci_setPreFetchSize: stmt=%p handle=%p", pStatement, pStatement->handle));
   return sdb_oci_stmt_setPrefetchSize((SDB_OCI_STMT *) pStatement->handle, fetchSize);
}

static CFL_BOOL sdb_oci_fetchNext(SDB_STATEMENTP pStatement) {
   SDB_OCI_STMT *stmt;
   CFL_BOOL bFound;

   SDB_LOG_DEBUG(("sdb_oci_fetchNext: stmt=%p handle=%p", pStatement, pStatement->handle));
   stmt = (SDB_OCI_STMT *) pStatement->handle;
   if (sdb_oci_stmt_fetchNext(stmt, &bFound) && bFound) {
      ++pStatement->fetchCount;
      return CFL_TRUE;
   }
   return CFL_FALSE;
}

static CFL_BOOL sdb_oci_tableDropIndex(SDB_CONNECTIONP conn, SDB_TABLEP table, SDB_INDEXP index, CFL_BOOL physically) {
   CFL_STRP sql;
   CFL_BOOL bSuccess = CFL_FALSE;
   SDB_DICTIONARYP dict;
   CFL_UINT64 affectedRows;

   dict = sdb_schema_getDict(table->clpSchema, conn);
   if (dict == NULL) {
      sdb_thread_setError(SDB_ERROR_TYPE_SDB, SDB_ERROR_DICT_NOT_FOUND, "Dictionary not found in schema %s",
              cfl_str_getPtr(table->clpSchema->name));
      return CFL_FALSE;
   }
   sql = cfl_str_new(50);
   do {
      if (physically && index->field->fieldType == SDB_FIELD_INDEX) {
         SDB_LOG_INFO(("Drop index column physically: %s.%s.%s",
                 cfl_str_getPtr(table->dbSchema->name), cfl_str_getPtr(table->dbName), cfl_str_getPtr(index->field->dbName)));
         if (dict->deleteColumn(conn, table, index->field)) {
            sdb_connection_commit(conn, CFL_TRUE);
         }
         cfl_str_append(sql, "alter table ", cfl_str_getPtr(table->dbSchema->name), ".", cfl_str_getPtr(table->dbName),
                 " drop column ", cfl_str_getPtr(index->field->dbName), NULL);
         bSuccess = sdb_connection_executeImmediateLen(conn, cfl_str_getPtr(sql), cfl_str_getLength(sql), &affectedRows);
      } else {
         SDB_LOG_INFO(("Remove index column logically: %s.%s.%s",
                 cfl_str_getPtr(table->dbSchema->name), cfl_str_getPtr(table->dbName), cfl_str_getPtr(index->field->dbName)));
         CFL_STR_SET_NULL(index->field->clpIndexName);
         CFL_STR_SET_NULL(index->field->dbIndexName);
         CFL_STR_SET_NULL(index->field->clpExpression);
         CFL_STR_SET_NULL(index->field->dbExpression);
         CFL_STR_SET_NULL(index->field->indexAscHint);
         if (dict->updateColumn(conn, table, index->field)) {
            bSuccess = sdb_connection_commit(conn, CFL_TRUE);
         }
      }
      cfl_str_clear(sql);
      if (bSuccess) {
         cfl_str_append(sql, "drop index ", cfl_str_getPtr(table->dbSchema->name), ".", cfl_str_getPtr(index->field->dbIndexName), NULL);
         bSuccess = sdb_connection_executeImmediateLen(conn, cfl_str_getPtr(sql), cfl_str_getLength(sql), &affectedRows);
      }
   } while (0);
   cfl_str_free(sql);
   return bSuccess;
}

static CFL_BOOL sdb_oci_lockRecord(SDB_AREAP pSDBArea, PHB_ITEM pRecno, PHB_ITEM pRowId) {
   HB_SYMBOL_UNUSED(pRecno);
   HB_SYMBOL_UNUSED(pRowId);
   return ! pSDBArea->isShared || lockId(pSDBArea, ORA_LCK_SX_MODE);
}

static CFL_BOOL sdb_oci_unlockRecord(SDB_AREAP pSDBArea, PHB_ITEM pRecno, PHB_ITEM pRowId) {
   HB_SYMBOL_UNUSED(pRecno);
   HB_SYMBOL_UNUSED(pRowId);
    return ! pSDBArea->isShared || lockId(pSDBArea, ORA_LCK_SS_MODE);
}

static CFL_BOOL dropTableMemo(SDB_CONNECTIONP conn, SDB_TABLEP table) {
   CFL_BOOL bSuccess;
   CFL_STRP sql = cfl_str_new(64);
   CFL_UINT64 affectedRows;

   CFL_STR_APPEND_CONST(sql, "drop table ");
   cfl_str_appendStr(sql, table->dbSchema->name);
   cfl_str_appendChar(sql, '.');
   cfl_str_appendStr(sql, table->dbName);
   CFL_STR_APPEND_CONST(sql, "_MEMO");
   bSuccess = sdb_connection_executeImmediateLen(conn, cfl_str_getPtr(sql), cfl_str_getLength(sql), &affectedRows);
   SDB_LOG_INFO(("%s drop memo table: %s.%s_MEMO", bSuccess ? "Success" : "Fail",
                 cfl_str_getPtr(table->dbSchema->name), cfl_str_getPtr(table->dbName)));
   cfl_str_free(sql);
   return bSuccess;
}

static CFL_BOOL dropTableSequence(SDB_CONNECTIONP conn, SDB_TABLEP table) {
   CFL_BOOL bSuccess;
   CFL_STRP sql = cfl_str_new(64);
   CFL_UINT64 affectedRows;

   CFL_STR_APPEND_CONST(sql, "drop sequence ");
   cfl_str_appendStr(sql, table->dbSchema->name);
   cfl_str_appendChar(sql, '.');
   CFL_STR_APPEND_CONST(sql, "SEQ_");
   cfl_str_appendStr(sql, table->dbName);
   cfl_str_appendChar(sql, '_');
   cfl_str_appendStr(sql, table->pkField->dbName);
   bSuccess = sdb_connection_executeImmediateLen(conn, cfl_str_getPtr(sql), cfl_str_getLength(sql), &affectedRows);
   SDB_LOG_INFO(("%s drop sequence: %s.SEQ_%s_%s", bSuccess ? "Success" : "Fail",
                 cfl_str_getPtr(table->dbSchema->name),
                 cfl_str_getPtr(table->dbName),
                 cfl_str_getPtr(table->pkField->dbName)));
   cfl_str_free(sql);
   return bSuccess;
}

static CFL_BOOL dropTable(SDB_CONNECTIONP conn, SDB_TABLEP table) {
   CFL_BOOL bSuccess;
   CFL_STRP sql = cfl_str_new(64);
   CFL_UINT64 affectedRows;

   ENTER_FUN_NAME("dropTable");
   CFL_STR_APPEND_CONST(sql, "drop table ");
   cfl_str_appendStr(sql, table->dbSchema->name);
   cfl_str_appendChar(sql, '.');
   cfl_str_appendStr(sql, table->dbName);
   bSuccess = sdb_connection_executeImmediateLen(conn, cfl_str_getPtr(sql), cfl_str_getLength(sql), &affectedRows);
   SDB_LOG_INFO(("%s drop Table: %s.%s", bSuccess ? "Success" : "Fail",
                 cfl_str_getPtr(table->dbSchema->name), cfl_str_getPtr(table->dbName)));
   cfl_str_free(sql);
   RETURN bSuccess;
}

static CFL_BOOL sdb_oci_dropTable(SDB_CONNECTIONP conn, SDB_TABLEP table) {
   CFL_BOOL bSuccess = CFL_TRUE;
   CFL_UINT32 numCols;
   CFL_UINT32 index;
   SDB_DICTIONARYP dict;

   ENTER_FUN_NAME("sdb_oci_dropTable");
   dict = sdb_schema_getDict(table->clpSchema, conn);
   if (dict == NULL) {
      sdb_thread_setError(SDB_ERROR_TYPE_SDB, SDB_ERROR_DICT_NOT_FOUND, "Dictionary not found in schema %s",
              cfl_str_getPtr(table->clpSchema->name));
      RETURN CFL_FALSE;
   }
   SDB_LOG_DEBUG(("drop table %s.%s", cfl_str_getPtr(table->dbSchema->name), cfl_str_getPtr(table->dbName)));
   if (dict->deleteTable(conn, table)) {
      bSuccess = sdb_connection_commit(conn, CFL_TRUE);
      if (bSuccess) {
         numCols = cfl_list_length(table->fields);
         for (index = 0; index < numCols; index++) {
            SDB_FIELDP field = (SDB_FIELDP) cfl_list_get(table->fields, index);
            if (field->clpType == SDB_CLP_MEMO_LONG) {
               bSuccess = dropTableMemo(conn, table);
               break;
            }
         }
         if (bSuccess) {
            bSuccess = dropTable(conn, table);
            if (bSuccess && IS_SERVER_SET(table->pkField)) {
               bSuccess = dropTableSequence(conn, table);
            }
         }
      }
   }
   RETURN bSuccess;
}

static CFL_BOOL appendDBFieldType(CFL_STRP sb, SDB_FIELDP field) {
   if (field->isVirtual) {
      cfl_str_append(sb, "as (", cfl_str_getPtr(field->dbExpression), ") virtual", NULL);
   } else {
      switch (field->clpType) {
         case SDB_CLP_CHARACTER:
         {
            char buffer[33];
            sprintf(buffer, "%u", field->length);
            cfl_str_append(sb, "varchar2(", buffer, ") default ' '", NULL);
            break;
         }
         case SDB_CLP_LOGICAL:
            CFL_STR_APPEND_CONST(sb, "char(1) default 'N'");
            break;
         case SDB_CLP_DATE:
            CFL_STR_APPEND_CONST(sb, "date default to_date(1,'J')");
            break;
         case SDB_CLP_NUMERIC:
         {
            char buffer[33];
            sprintf(buffer, "%u", field->length);
            cfl_str_append(sb, "number(", buffer, NULL);
            if (field->decimals > 0) {
               sprintf(buffer, "%u", field->decimals);
               cfl_str_append(sb, ",", buffer, NULL);
            }
            CFL_STR_APPEND_CONST(sb, ") default 0");
            break;
         }
         case SDB_CLP_FLOAT:
            CFL_STR_APPEND_CONST(sb, "binary_float default 0");
            break;
         case SDB_CLP_INTEGER:
         case SDB_CLP_BIGINT:
            CFL_STR_APPEND_CONST(sb, "number(*) default 0");
            break;
         case SDB_CLP_DOUBLE:
            CFL_STR_APPEND_CONST(sb, "binary_double default 0");
            break;
         case SDB_CLP_TIMESTAMP:
            CFL_STR_APPEND_CONST(sb, "timestamp default to_date(1,'J')");
            break;
         case SDB_CLP_MEMO_LONG:
            CFL_STR_APPEND_CONST(sb, "number(*) default 0");
            break;
         case SDB_CLP_CLOB:
            CFL_STR_APPEND_CONST(sb, "clob");
            break;
         case SDB_CLP_IMAGE:
         case SDB_CLP_BLOB:
            CFL_STR_APPEND_CONST(sb, "blob");
            break;
         default:
            RETURN CFL_FALSE;
      }
   }
   RETURN CFL_TRUE;
}

static CFL_BOOL createMemoTable(SDB_CONNECTIONP conn, const char *schema, SDB_TABLEP table) {
   CFL_STRP sql = cfl_str_new(200);
   CFL_BOOL bResult;
   CFL_UINT64 affectedRows;

   ENTER_FUN_NAME("createMemoTable");
   cfl_str_append(sql, "create table ", schema, ".", cfl_str_getPtr(table->dbName), "_MEMO(", NULL);
   CFL_STR_APPEND_CONST(sql, "RECNO NUMBER(*) NOT NULL, COL_NAME VARCHAR2(44) NOT NULL, MEMO LONG RAW");
   cfl_str_append(sql, ", CONSTRAINT ", cfl_str_getPtr(table->dbName), "_MEMO_PK PRIMARY KEY (RECNO, COL_NAME)", NULL);
   cfl_str_append(sql, ", CONSTRAINT ", cfl_str_getPtr(table->dbName), "_MEMO_FK FOREIGN KEY (RECNO) REFERENCES ",
           schema, ".", cfl_str_getPtr(table->dbName), " ON DELETE CASCADE ENABLE)", NULL);
   SDB_LOG_DEBUG(("Create memo table: %s", cfl_str_getPtr(sql)));
   bResult = sdb_connection_executeImmediateLen(conn, cfl_str_getPtr(sql), cfl_str_getLength(sql), &affectedRows);
   cfl_str_free(sql);
   RETURN bResult;
}

static CFL_BOOL createTableSequence(SDB_CONNECTIONP conn, SDB_SCHEMAP schema, SDB_TABLEP table) {
   CFL_STRP sql;
   CFL_STRP seqName = cfl_str_new(64);
   CFL_BOOL bSuccess;
   SDB_STATEMENTP pStmt;
   CFL_UINT64 ulAffectedRows = 0;
   CFL_BOOL fFound = CFL_FALSE;

   cfl_str_appendFormat(seqName, "SEQ_%s_%s", cfl_str_getPtr(table->dbName), cfl_str_getPtr(table->pkField->dbName));
   pStmt = PREPARE_STMT_CONST(conn, "select 1 from all_sequences where sequence_name=:name and sequence_owner=:username");
   if (pStmt) {
      sdb_stmt_setString(pStmt, "name", seqName, CFL_FALSE);
      sdb_stmt_setString(pStmt, "username", schema->name, CFL_FALSE);
      fFound = sdb_stmt_execute(pStmt, &ulAffectedRows) && sdb_stmt_fetchNext(pStmt, CFL_TRUE);
      sdb_stmt_free(pStmt);
   }

   if (! fFound) {
      sql = cfl_str_new(128);
      cfl_str_appendFormat(sql, "create sequence %s.%s increment by 1 start with 1 cache 20",
                           cfl_str_getPtr(schema->name),
                           cfl_str_getPtr(seqName));
      bSuccess = sdb_connection_executeImmediateLen(conn, cfl_str_getPtr(sql), cfl_str_getLength(sql), &ulAffectedRows);
      cfl_str_free(sql);
   } else {
      bSuccess = CFL_TRUE;
   }
   cfl_str_free(seqName);
   return bSuccess;
}

static CFL_BOOL createTableTrigger(SDB_CONNECTIONP conn, SDB_SCHEMAP schema, SDB_TABLEP table) {
   CFL_STRP sql = cfl_str_new(512);
   CFL_BOOL bSuccess;
   CFL_UINT64 affectedRows;

   cfl_str_appendFormat(sql, "create or replace trigger %s.TRG_%s_%s\r"
                             "   before insert\r"
                             "   on %s.%s\r"
                             "   for each row\r"
                             "declare\r"
                             "begin\r"
                             "   :new.%s := SEQ_%s_%s.nextval;\r"
                             "end;\r",
                            cfl_str_getPtr(schema->name),
                            cfl_str_getPtr(table->dbName),
                            cfl_str_getPtr(table->pkField->dbName),
                            cfl_str_getPtr(schema->name),
                            cfl_str_getPtr(table->dbName),
                            cfl_str_getPtr(table->pkField->dbName),
                            cfl_str_getPtr(table->dbName),
                            cfl_str_getPtr(table->pkField->dbName));
   bSuccess = sdb_connection_executeImmediateLen(conn, cfl_str_getPtr(sql), cfl_str_getLength(sql), &affectedRows);
   cfl_str_free(sql);
   return bSuccess;
}

static CFL_BOOL sdb_oci_createTable(SDB_CONNECTIONP conn, SDB_SCHEMAP schema, SDB_TABLEP table) {
   CFL_STRP sql;
   CFL_UINT32 ulLen = cfl_list_length(table->fields);
   CFL_UINT32 ulIndex;
   CFL_BOOL bMemoAux = CFL_FALSE;
   CFL_BOOL bSuccess;
   SDB_DICTIONARYP dict;
   CFL_UINT64 affectedRows;

   ENTER_FUN_NAME("sdb_oci_createTable");
   dict = sdb_schema_getDict(schema, conn);
   if (dict == NULL) {
      sdb_thread_setError(SDB_ERROR_TYPE_SDB, SDB_ERROR_DICT_NOT_FOUND, "Dictionary not found in schema %s", cfl_str_getPtr(schema->name));
      RETURN CFL_FALSE;
   }
   sql = cfl_str_new(1024);
   // Verificar no dicionario a existencia do indice
   cfl_str_append(sql, "create table ", cfl_str_getPtr(schema->name), ".", cfl_str_getPtr(table->dbName), "(", NULL);
   for (ulIndex = 0; ulIndex < ulLen; ulIndex++) {
      SDB_FIELDP field = (SDB_FIELDP) cfl_list_get(table->fields, ulIndex);
      cfl_str_appendChar(sql, '\n');
      if (ulIndex > 0) {
         cfl_str_appendChar(sql, ',');
      }
      cfl_str_appendStr(sql, field->dbName);
      cfl_str_appendChar(sql, ' ');
      if (!appendDBFieldType(sql, field)) {
         sdb_thread_setError(SDB_ERROR_TYPE_SDB, SDB_ERROR_INVALID_DATATYPE, "Invalid datatype at field");
         cfl_str_free(sql);
         RETURN CFL_FALSE;
      }
      if (field->clpType == SDB_CLP_MEMO_LONG) {
         bMemoAux = CFL_TRUE;
      }
      switch (field->fieldType) {
         case SDB_FIELD_PK:
            CFL_STR_APPEND_CONST(sql, " constraint ");
            cfl_str_appendStr(sql, table->dbName);
            CFL_STR_APPEND_CONST(sql, "_PK primary key");
            break;
         case SDB_FIELD_DEL_FLAG:
            CFL_STR_APPEND_CONST(sql, " not null");
            break;
      }
   }
   cfl_str_appendChar(sql, ')');
   SDB_LOG_DEBUG(("Create table: %s.%s", cfl_str_getPtr(schema->name), cfl_str_getPtr(table->dbName)));
   if (sdb_connection_executeImmediateLen(conn, cfl_str_getPtr(sql), cfl_str_getLength(sql), &affectedRows) &&
           createTableSequence(conn, schema, table) &&
           createTableTrigger(conn, schema, table)) {
      ulLen = cfl_list_length(table->indexes);
      for (ulIndex = 0; ulIndex < ulLen; ulIndex++) {
         SDB_INDEXP index = (SDB_INDEXP) cfl_list_get(table->indexes, ulIndex);
         cfl_str_clear(sql);
         cfl_str_append(sql, "create index ", cfl_str_getPtr(schema->name), ".", cfl_str_getPtr(index->field->dbIndexName),
                 " on ", cfl_str_getPtr(schema->name), ".", cfl_str_getPtr(table->dbName), "(", NULL);
         cfl_str_append(sql, cfl_str_getPtr(index->field->dbName), ",", cfl_str_getPtr(table->pkField->dbName), NULL);
         cfl_str_appendChar(sql, ')');
         SDB_LOG_DEBUG(("Create index: %s", cfl_str_getPtr(sql)));
         if (! sdb_connection_executeImmediateLen(conn, cfl_str_getPtr(sql), cfl_str_getLength(sql), &affectedRows)) {
            cfl_str_free(sql);
            RETURN CFL_FALSE;
         }
      }
   } else {
      cfl_str_free(sql);
      RETURN CFL_FALSE;
   }
   cfl_str_free(sql);
   if (bMemoAux && !createMemoTable(conn, cfl_str_getPtr(schema->name), table)) {
      RETURN CFL_FALSE;
   }
   bSuccess = dict->insertTable(conn, schema, table);
   if (bSuccess) {
      sdb_connection_commit(conn, CFL_TRUE);
   }
   RETURN bSuccess;
}

static CFL_BOOL sdb_oci_tableAddColumn(SDB_CONNECTIONP conn, SDB_TABLEP table, SDB_FIELDP field) {
   CFL_STRP sql;
   CFL_BOOL fSuccess = CFL_TRUE;
   SDB_DICTIONARYP dict;
   CFL_UINT64 ulAffectedRows = 0;

   ENTER_FUN_NAME("sdb_oci_tableAddColumn");
   dict = sdb_schema_getDict(table->clpSchema, conn);
   if (dict == NULL) {
      sdb_thread_setError(SDB_ERROR_TYPE_SDB, SDB_ERROR_DICT_NOT_FOUND, "Dictionary not found in schema %s", cfl_str_getPtr(table->clpSchema->name));
      RETURN CFL_FALSE;
   }
   sql = cfl_str_new(200);
   CFL_STR_APPEND_CONST(sql, "alter table ");
   cfl_str_appendStr(sql, table->dbSchema->name);
   cfl_str_appendChar(sql, '.');
   cfl_str_appendStr(sql, table->dbName);
   CFL_STR_APPEND_CONST(sql, " add(");
   cfl_str_appendStr(sql, field->dbName);
   cfl_str_appendChar(sql, ' ');
   if (!appendDBFieldType(sql, field)) {
      sdb_thread_setError(SDB_ERROR_TYPE_SDB, SDB_ERROR_INVALID_DATATYPE, "Invalid datatype at field");
      cfl_str_free(sql);
      RETURN CFL_FALSE;
   }
   cfl_str_appendChar(sql, ')');
   if (sdb_connection_executeImmediateLen(conn, cfl_str_getPtr(sql), cfl_str_getLength(sql), &ulAffectedRows)) {
      fSuccess = dict->insertColumn(conn, table, field);
      if (fSuccess) {
         sdb_connection_commit(conn, CFL_TRUE);
      }
      SDB_LOG_INFO(("Table add col: %s.%s.%s", cfl_str_getPtr(table->dbSchema->name), cfl_str_getPtr(table->dbName), cfl_str_getPtr(field->dbName)));
   }
   cfl_str_free(sql);
   RETURN fSuccess;
}

static CFL_BOOL sdb_oci_tableModifyColumn(SDB_CONNECTIONP conn, SDB_TABLEP table, SDB_FIELDP field) {
   CFL_STRP sql;
   CFL_BOOL fSuccess = CFL_TRUE;
   SDB_DICTIONARYP dict;
   CFL_UINT64 ulAffectedRows = 0;

   ENTER_FUN_NAME("sdb_oci_tableModifyColumn");

   dict = sdb_schema_getDict(table->clpSchema, conn);
   if (dict == NULL) {
      sdb_thread_setError(SDB_ERROR_TYPE_SDB, SDB_ERROR_DICT_NOT_FOUND, "Dictionary not found in schema %s", cfl_str_getPtr(table->clpSchema->name));
      RETURN CFL_FALSE;
   }
   sql = cfl_str_new(200);
   CFL_STR_APPEND_CONST(sql, "alter table ");
   cfl_str_appendStr(sql, table->dbSchema->name);
   cfl_str_appendChar(sql, '.');
   cfl_str_appendStr(sql, table->dbName);
   CFL_STR_APPEND_CONST(sql, " modify(");
   cfl_str_appendStr(sql, field->dbName);
   cfl_str_appendChar(sql, ' ');
   if (!appendDBFieldType(sql, field)) {
      sdb_thread_setError(SDB_ERROR_TYPE_SDB, SDB_ERROR_INVALID_DATATYPE, "Invalid datatype at field");
      cfl_str_free(sql);
      RETURN CFL_FALSE;
   }
   cfl_str_appendChar(sql, ')');
   if (sdb_connection_executeImmediateLen(conn, cfl_str_getPtr(sql), cfl_str_getLength(sql), &ulAffectedRows)) {
      fSuccess = dict->updateColumn(conn, table, field);
      if (fSuccess) {
         sdb_connection_commit(conn, CFL_TRUE);
      }
      SDB_LOG_INFO(("Modify col: %s.%s.%s", cfl_str_getPtr(table->dbSchema->name), cfl_str_getPtr(table->dbName), cfl_str_getPtr(field->dbName)));
   }
   cfl_str_free(sql);
   RETURN fSuccess;
}

static CFL_BOOL sdb_oci_createIndex(SDB_CONNECTIONP conn, SDB_TABLEP table, SDB_INDEXP index) {
   CFL_STRP sql;
   CFL_BOOL fSuccess;
   SDB_DICTIONARYP dict;
   CFL_UINT64 affectedRows;

   ENTER_FUN_NAME("sdb_oci_createIndex");

   dict = sdb_schema_getDict(table->clpSchema, conn);
   if (dict == NULL) {
      sdb_thread_setError(SDB_ERROR_TYPE_SDB, SDB_ERROR_DICT_NOT_FOUND, "Dictionary not found in schema %s", cfl_str_getPtr(table->clpSchema->name));
      RETURN CFL_FALSE;
   }
   sql = cfl_str_new(256);
   CFL_STR_APPEND_CONST(sql, "create index ");
   cfl_str_appendStr(sql, table->dbSchema->name);
   cfl_str_appendChar(sql, '.');
   cfl_str_appendStr(sql, index->field->dbIndexName);
   CFL_STR_APPEND_CONST(sql, " on ");
   cfl_str_appendStr(sql, table->dbSchema->name);
   cfl_str_appendChar(sql, '.');
   cfl_str_appendStr(sql, table->dbName);
   cfl_str_appendChar(sql, '(');
   cfl_str_appendStr(sql, index->field->dbName);
   cfl_str_appendChar(sql, ',');
   cfl_str_appendStr(sql, table->pkField->dbName);
   cfl_str_appendChar(sql, ')');
   fSuccess = sdb_connection_executeImmediateLen(conn, cfl_str_getPtr(sql), cfl_str_getLength(sql), &affectedRows);
   if (fSuccess) {
      SDB_LOG_INFO(("Index created: %s.%s", cfl_str_getPtr(table->dbSchema->name), cfl_str_getPtr(index->field->dbIndexName)));
      if (cfl_str_isBlank(index->field->indexAscHint)) {
         index->field->indexAscHint = CFL_STR_SET_CONST(index->field->indexAscHint, "INDEX(");
         cfl_str_appendStr(index->field->indexAscHint, table->dbName);
         cfl_str_appendChar(index->field->indexAscHint, ' ');
         cfl_str_appendStr(index->field->indexAscHint, index->field->dbIndexName);
         cfl_str_appendChar(index->field->indexAscHint, ')');
      }
      fSuccess = dict->updateColumn(conn, table, index->field);
      if (fSuccess) {
         sdb_connection_commit(conn, CFL_TRUE);
      }
   }
   cfl_str_free(sql);
   RETURN fSuccess;
}

static void appendRemoteCallArgs(CFL_STRP sql, SDB_PARAMLISTP params, CFL_BOOL bImplicitArgs) {
   if (bImplicitArgs && !sdb_param_listIsEmpty(params)) {
      SDB_PARAMP param;
      CFL_UINT32 paramPos = 1;

      cfl_str_appendChar(sql, '(');
      param = sdb_param_listGetPos(params, paramPos);
      while (param) {
         if (paramPos > 1) {
            CFL_STR_APPEND_CONST(sql, ",:");
         } else {
            cfl_str_appendChar(sql, ':');
         }
         if (sdb_param_getName(param) == NULL) {
            cfl_str_appendFormat(sql, "%u", paramPos);
         } else {
            cfl_str_appendStr(sql, sdb_param_getName(param));
         }
         ++paramPos;
         param = sdb_param_listGetPos(params, paramPos);
      }
      cfl_str_appendChar(sql, ')');
   }
}

static CFL_BOOL sdb_oci_executeProcedure(SDB_CONNECTIONP connection, const char *procName, SDB_PARAMLISTP params, CFL_BOOL bImplicitArgs) {
   SDB_STATEMENTP pStmt;
   CFL_STRP sql = cfl_str_new(120);
   CFL_UINT64 affectedRows;
   CFL_BOOL bSuccess = CFL_FALSE;

   ENTER_FUN_NAME("sdb_oci_executeProcedure");

   CFL_STR_APPEND_CONST(sql, "call ");
   cfl_str_append(sql, procName, NULL);
   appendRemoteCallArgs(sql, params, bImplicitArgs);
   SDB_LOG_DEBUG(("Execute procedure: %s", cfl_str_getPtr(sql)));

   pStmt = sdb_connection_prepareStatement(connection, sql);
   if (pStmt != NULL) {
      sdb_param_listMoveAll(sdb_stmt_getParams(pStmt), params);
      bSuccess = sdb_stmt_execute(pStmt, &affectedRows);
      sdb_stmt_free(pStmt);
   }
   cfl_str_free(sql);
   RETURN bSuccess;
}

static PHB_ITEM sdb_oci_executeFunction(SDB_CONNECTIONP conn, const char *funcName, CFL_UINT8 resultType, SDB_PARAMLISTP params, CFL_BOOL bImplicitArgs) {
   SDB_STATEMENTP pStmt;
   CFL_STRP sql = cfl_str_new(256);
   PHB_ITEM pResult = NULL;
   CFL_UINT64 affectedRows;

   CFL_STR_APPEND_CONST(sql, "call ");
   cfl_str_append(sql, funcName, NULL);
   appendRemoteCallArgs(sql, params, bImplicitArgs);
   CFL_STR_APPEND_CONST(sql, " into :65535");
   SDB_LOG_DEBUG(("Execute function: %s", cfl_str_getPtr(sql)));

   pStmt = sdb_connection_prepareStatement(conn, sql);
   if (pStmt != NULL) {
      SDB_PARAMP paramResult;
      if (params != NULL) {
         sdb_param_listMoveAll(sdb_stmt_getParams(pStmt), params);
      }
      paramResult = sdb_param_listAdd(sdb_stmt_getParams(pStmt), NULL, 0, resultType, 0, CFL_TRUE, CFL_FALSE, CFL_TRUE);
      if (sdb_stmt_execute(pStmt, &affectedRows)) {
         pResult = hb_itemNew(sdb_param_getItem(paramResult));
      }
      sdb_stmt_free(pStmt);
   }
   cfl_str_free(sql);
   return pResult;
}

static void sdb_oci_closeStatement(void *handle) {
   ENTER_FUN_NAME("sdb_oci_closeStatement");
   sdb_oci_stmt_free((SDB_OCI_STMT *) handle);
   RETURN;
}

static CFL_BOOL sdb_oci_isConnected(SDB_CONNECTIONP conn) {
   SDB_OCI_CONNECTION *cn = (SDB_OCI_CONNECTION *) conn->handle;
   int status;
   CFL_BOOL bSuccess;
   void *errorHandle = sdb_oci_getErrorHandle();

   ENTER_FUN_NAME("sdb_oci_isConnected");
   status = s_ociSymbols->OCIPing(cn->serviceHandle, errorHandle, SDB_OCI_DEFAULT);
   bSuccess = ! STATUS_ERROR(status);
   RETURN bSuccess;
}

static CFL_BOOL sdb_oci_tableRename(SDB_CONNECTIONP conn, SDB_TABLEP table, const char *newClpName, const char *newDBName) {
   CFL_BOOL bResult = CFL_TRUE;
   SDB_DICTIONARYP dict;

   SDB_LOG_DEBUG(("sdb_oci_tableRename: %s(%s) -> %s(%s)", cfl_str_getPtr(table->clpName), cfl_str_getPtr(table->dbName), newClpName, newDBName));
   dict = sdb_schema_getDict(table->clpSchema, conn);
   if (dict == NULL) {
      sdb_thread_setError(SDB_ERROR_TYPE_SDB, SDB_ERROR_DICT_NOT_FOUND, "Dictionary not found in schema %s", cfl_str_getPtr(table->clpSchema->name));
      return CFL_FALSE;
   }
   if (!sdb_util_isEmpty(newDBName)) {
      CFL_UINT64 affectedRows;
      CFL_STRP sql = cfl_str_new(50);
      cfl_str_append(sql, "alter table ", cfl_str_getPtr(table->dbSchema->name), ".", cfl_str_getPtr(table->dbName),
              " rename to ", newDBName, NULL);
      SDB_LOG_DEBUG(("rename sql: %s", cfl_str_getPtr(sql)));
      if (sdb_connection_executeImmediateLen(conn, cfl_str_getPtr(sql), cfl_str_getLength(sql), &affectedRows)) {
         cfl_str_setValue(table->dbName, newDBName);
      } else {
         bResult = CFL_FALSE;
      }
      cfl_str_free(sql);
   }
   if (bResult && !sdb_util_isEmpty(newClpName)) {
      if (dict->deleteTable(conn, table)) {
         cfl_str_setValue(table->clpName, newClpName);
         bResult = dict->insertTable(conn, table->clpSchema, table);
      } else {
         bResult = CFL_FALSE;
      }
      if (bResult) {
         sdb_connection_commit(conn, CFL_TRUE);
      } else {
         sdb_connection_rollback(conn, CFL_TRUE);
      }
   }
   return bResult;
}

static CFL_BOOL sdb_oci_isRowIdSuppported(void) {
   return CFL_TRUE;
}

static CFL_BOOL sdb_oci_isErrorType(SDB_ERRORP pError, int code) {
   if (pError->type == SDB_ERROR_TYPE_DB_ERROR) {
      switch (code) {
         case SDB_DB_ERROR_UNIQUE_VIOLATION:
            return pError->code == 1;
      }
   }
   return CFL_FALSE;
}

static void setOracleTypeName(CFL_STRP strName, CFL_UINT16 oracleType) {
   switch (oracleType) {
      case SDB_SQLT_CHR:
         CFL_STR_SET_CONST(strName, "VARCHAR2");
         break;

      case SDB_SQLT_VCS:
         CFL_STR_SET_CONST(strName, "VARCHAR");
         break;

      case SDB_SQLT_AFC:
         CFL_STR_SET_CONST(strName, "CHAR");
         break;

      case SDB_SQLT_FLT:
      case SDB_SQLT_BFLOAT:
      case SDB_SQLT_IBFLOAT:
         CFL_STR_SET_CONST(strName, "FLOAT");
         break;

      case SDB_SQLT_BDOUBLE:
      case SDB_SQLT_IBDOUBLE:
         CFL_STR_SET_CONST(strName, "DOUBLE");
         break;

      case SDB_SQLT_INT:
         CFL_STR_SET_CONST(strName, "INT");
         break;

      case SDB_SQLT_UIN:
         CFL_STR_SET_CONST(strName, "UINT");
         break;

      case SDB_SQLT_NUM:
         CFL_STR_SET_CONST(strName, "NUMBER");
         break;

      case SDB_SQLT_DAT:
      case SDB_SQLT_ODT:
         CFL_STR_SET_CONST(strName, "DATE");
         break;

      case SDB_SQLT_LVC:
         CFL_STR_SET_CONST(strName, "LONG_VARCHAR");
         break;

      case SDB_SQLT_LVB:
         CFL_STR_SET_CONST(strName, "LONG_RAW");
         break;

      case SDB_SQLT_CLOB:
         CFL_STR_SET_CONST(strName, "CLOB");
         break;

      case SDB_SQLT_BLOB:
         CFL_STR_SET_CONST(strName, "BLOB");
         break;

      case SDB_SQLT_TIME:
      case SDB_SQLT_TIME_TZ:
      case SDB_SQLT_TIMESTAMP:
      case SDB_SQLT_TIMESTAMP_TZ:
      case SDB_SQLT_TIMESTAMP_LTZ:
         CFL_STR_SET_CONST(strName, "TIMESTAMP");
         break;

      case SDB_SQLT_RID:
      case SDB_SQLT_RDD:
         CFL_STR_SET_CONST(strName, "ROWID");
         break;

      case SDB_SQLT_RSET:
         CFL_STR_SET_CONST(strName, "STATEMENT");
         break;

      case SDB_SQLT_BOL:
         CFL_STR_SET_CONST(strName, "BOOLEAN");
         break;

      case SDB_SQLT_BFILEE:
         CFL_STR_SET_CONST(strName, "BFILE");
         break;

      case SDB_SQLT_NTY:
         CFL_STR_SET_CONST(strName, "OBJECT");
         break;

      default:
         CFL_STR_SET_CONST(strName, "UNKNOWN");
         break;
   }
}

static CFL_UINT8 ociToSdbDatatype(CFL_UINT16 oracleType, CFL_BOOL char1IsLogical, CFL_UINT32 size) {
   switch (oracleType) {
      case SDB_SQLT_AFC:
         return char1IsLogical && size == 1 ? SDB_CLP_LOGICAL : SDB_CLP_CHARACTER;

      case SDB_SQLT_CHR:
      case SDB_SQLT_STR:
      case SDB_SQLT_VCS:
      case SDB_SQLT_AVC:
         return SDB_CLP_CHARACTER;

      case SDB_SQLT_NUM:
      case SDB_SQLT_INT:
      case SDB_SQLT_UIN:
      case SDB_SQLT_FLT:
      case SDB_SQLT_VNU:
      case SDB_SQLT_PDN:
      case SDB_SQLT_BFLOAT:
      case SDB_SQLT_BDOUBLE:
      case SDB_SQLT_IBFLOAT:
      case SDB_SQLT_IBDOUBLE:
         return SDB_CLP_NUMERIC;

      case SDB_SQLT_DAT:
      case SDB_SQLT_ODT:
         return SDB_CLP_DATE;

      case SDB_SQLT_LVC:
      case SDB_SQLT_LVB:
         return SDB_CLP_MEMO_LONG;

      case SDB_SQLT_CLOB:
      case SDB_SQLT_CFILEE:
         return SDB_CLP_CLOB;

      case SDB_SQLT_BLOB:
      case SDB_SQLT_BFILEE:
         return SDB_CLP_BLOB;

      case SDB_SQLT_TIME:
      case SDB_SQLT_TIME_TZ:
      case SDB_SQLT_TIMESTAMP:
      case SDB_SQLT_TIMESTAMP_TZ:
      case SDB_SQLT_TIMESTAMP_LTZ:
         return SDB_CLP_TIMESTAMP;

      case SDB_SQLT_RID:
      case SDB_SQLT_RDD:
         return SDB_CLP_ROWID;

      case SDB_SQLT_RSET:
         return SDB_CLP_CURSOR;

      case SDB_SQLT_BOL:
         return SDB_CLP_LOGICAL;
   }
   return SDB_CLP_UNKNOWN;
}

static CFL_BOOL sdb_oci_getQueryInfo(SDB_STATEMENTP pStmt, CFL_UINT16 pos, SDB_QUERY_COL_INFOP info) {
   SDB_OCI_COL_INFO *colInfo;

   ENTER_FUN_NAME("sdb_oci_getQueryInfo");
   colInfo = sdb_oci_stmt_getColInfo((SDB_OCI_STMT *) pStmt->handle, pos);
   if (colInfo != NULL) {
      cfl_str_setValueLen(info->name, colInfo->colName, colInfo->colNameLen);
      info->size = colInfo->sizeInChars > 0 ? colInfo->sizeInChars : colInfo->sizeInBytes;
      info->sizeInBytes = colInfo->sizeInBytes;
      info->clpType = ociToSdbDatatype(colInfo->dataType, pStmt->isChar1AsLogical, info->size);
      setOracleTypeName(info->dbType, colInfo->dataType);
      info->isNullable = colInfo->isNullable;
      info->precision = (CFL_UINT8) colInfo->precision;
      info->scale = colInfo->scale;
      RETURN CFL_TRUE;
   }
   RETURN CFL_FALSE;
}

static void * sdb_oci_createLob(SDB_CONNECTIONP conn, CFL_UINT8 lobType) {
   SDB_OCI_LOB *lob;
   ENTER_FUN_NAME("sdb_oci_createLob");
   lob = sdb_oci_lob_new((SDB_OCI_CONNECTION *) conn->handle, lobType == SDB_CLP_CLOB ? SDB_SQLT_CLOB : SDB_SQLT_BLOB, CFL_TRUE);
   RETURN lob;
}

static CFL_BOOL sdb_oci_closeLob(SDB_LOBP pLob) {
   CFL_BOOL bSuccess;
   ENTER_FUN_NAME("sdb_oci_closeLob");
   bSuccess = sdb_oci_lob_close((SDB_OCI_LOB *) pLob->handle);
   RETURN bSuccess;
}

static CFL_BOOL sdb_oci_releaseLob(SDB_LOBP pLob) {
   ENTER_FUN_NAME("sdb_oci_releaseLob");
   SDB_LOG_DEBUG(("sdb_oci_releaseLob. lob=%p lob handle=%p", pLob, pLob->handle));
   sdb_oci_lob_free((SDB_OCI_LOB *) pLob->handle);
   RETURN CFL_TRUE;
}

static CFL_BOOL sdb_oci_openLob(SDB_LOBP pLob) {
   CFL_BOOL bSuccess;
   ENTER_FUN_NAME("sdb_oci_openLob");
   bSuccess = sdb_oci_lob_open((SDB_OCI_LOB *) pLob->handle);
   RETURN bSuccess;
}

static CFL_BOOL sdb_oci_readLob(SDB_LOBP pLob, const char *buffer, CFL_UINT64 offset, CFL_UINT64 *amount) {
   CFL_BOOL bSuccess;

   ENTER_FUN_NAME("sdb_oci_readLob");
   bSuccess = sdb_oci_lob_read((SDB_OCI_LOB *) pLob->handle, buffer, offset, amount);
   RETURN bSuccess;
}

static CFL_BOOL sdb_oci_writeLob(SDB_LOBP pLob, const char *buffer, CFL_UINT64 offset, CFL_UINT64 amount) {
   CFL_BOOL bSuccess;

   ENTER_FUN_NAME("sdb_oci_writeLob");
   bSuccess = sdb_oci_lob_write((SDB_OCI_LOB *) pLob->handle, buffer, offset, amount);
   RETURN bSuccess;
}

static CFL_BOOL sdb_oci_getServerVersion(SDB_CONNECTIONP conn, CFL_STRP version) {
   SDB_OCI_CONNECTION *cn = (SDB_OCI_CONNECTION *) conn->handle;
   CFL_UINT32 serverRelease;
   int status;

   ENTER_FUN_NAME("sdb_oci_getServerVersion");

   // nothing to do if the server version has been determined earlier
   if (cn->serverVersion.versionNum == 0) {
      void *errorHandle = sdb_oci_getErrorHandle();
      char buffer[512];

      if (s_ociSymbols->OCIServerRelease2 != NULL) {
         status = s_ociSymbols->OCIServerRelease2(cn->serviceHandle, errorHandle, buffer, sizeof(buffer), SDB_OCI_HTYPE_SVCCTX,
                 &serverRelease, SDB_OCI_DEFAULT);
      } else {
         status = s_ociSymbols->OCIServerRelease(cn->serviceHandle, errorHandle, buffer, sizeof(buffer), SDB_OCI_HTYPE_SVCCTX,
                 &serverRelease);
      }
      CHECK_STATUS_RETURN(status, errorHandle, "Error getting server version", CFL_FALSE);
      cn->serverVersion.versionNum = (int) ((serverRelease >> 24) & 0xFF);
      if (cn->serverVersion.versionNum >= 18) {
         cn->serverVersion.releaseNum = (int) ((serverRelease >> 16) & 0xFF);
         cn->serverVersion.updateNum = (int) ((serverRelease >> 12) & 0x0F);
         cn->serverVersion.portReleaseNum = (int) ((serverRelease >> 4) & 0xFF);
         cn->serverVersion.portUpdateNum = (int) ((serverRelease) & 0xF);
      } else {
         cn->serverVersion.releaseNum = (int) ((serverRelease >> 20) & 0x0F);
         cn->serverVersion.updateNum = (int) ((serverRelease >> 12) & 0xFF);
         cn->serverVersion.portReleaseNum = (int) ((serverRelease >> 8) & 0x0F);
         cn->serverVersion.portUpdateNum = (int) ((serverRelease) & 0xFF);
      }
      cn->serverVersion.fullVersionNum = (CFL_UINT32) SDB_ORACLE_VERSION_TO_NUMBER(cn->serverVersion.versionNum,
              cn->serverVersion.releaseNum, cn->serverVersion.updateNum, cn->serverVersion.portReleaseNum,
              cn->serverVersion.portUpdateNum);
   }

   cfl_str_setFormat(version, "%d.%d.%d.%d.%d", cn->serverVersion.versionNum, cn->serverVersion.releaseNum,
           cn->serverVersion.updateNum, cn->serverVersion.portReleaseNum, cn->serverVersion.portUpdateNum);
   RETURN CFL_TRUE;
}


static CFL_BOOL sdb_oci_getCurrentSchema(SDB_CONNECTIONP conn, CFL_STRP schema) {
   SDB_OCI_CONNECTION *cn = (SDB_OCI_CONNECTION *) conn->handle;
   int status;
   char *curSchema;
   CFL_UINT32 curSchemaLen;
   void *errorHandle = sdb_oci_getErrorHandle();

   status = s_ociSymbols->OCIAttrGet(cn->sessionHandle, SDB_OCI_HTYPE_SESSION, &curSchema, &curSchemaLen, SDB_OCI_ATTR_CURRENT_SCHEMA, errorHandle);
   CHECK_STATUS_RETURN(status, errorHandle, "Error getting current schema", CFL_FALSE);
   cfl_str_setValueLen(schema, curSchema, curSchemaLen);
   return CFL_TRUE;
}

static CFL_BOOL sdb_oci_setCurrentSchema(SDB_CONNECTIONP conn, CFL_STRP schema) {
   SDB_OCI_CONNECTION *cn = (SDB_OCI_CONNECTION *) conn->handle;
   int status;
   char *newSchema = cfl_str_getPtr(schema);
   CFL_UINT32 newSchemaLen = cfl_str_getLength(schema);
   void *errorHandle = sdb_oci_getErrorHandle();

   status = s_ociSymbols->OCIAttrSet(cn->sessionHandle, SDB_OCI_HTYPE_SESSION, newSchema, newSchemaLen, SDB_OCI_ATTR_CURRENT_SCHEMA, errorHandle);
   CHECK_STATUS_RETURN(status, errorHandle, "Error setting current schema", CFL_FALSE);
   return CFL_TRUE;
}

static CFL_UINT32 sdb_oci_getStatementCache(SDB_CONNECTIONP conn) {
   SDB_OCI_CONNECTION *cn = (SDB_OCI_CONNECTION *) conn->handle;
   int status;
   CFL_UINT32 cacheSize;
   void *errorHandle = sdb_oci_getErrorHandle();

   status = s_ociSymbols->OCIAttrGet(cn->serviceHandle, SDB_OCI_HTYPE_SVCCTX, &cacheSize, 0, SDB_OCI_ATTR_STMTCACHESIZE, errorHandle);
   CHECK_STATUS_RETURN(status, errorHandle, "Error getting statement cache size", 0);
   return cacheSize;
}

static CFL_BOOL sdb_oci_setStatementCache(SDB_CONNECTIONP conn, CFL_UINT32 cacheSize) {
   SDB_OCI_CONNECTION *cn = (SDB_OCI_CONNECTION *) conn->handle;
   int status;
   void *errorHandle = sdb_oci_getErrorHandle();

   status = s_ociSymbols->OCIAttrSet(cn->serviceHandle, SDB_OCI_HTYPE_SVCCTX, &cacheSize, 0, SDB_OCI_ATTR_STMTCACHESIZE, errorHandle);
   CHECK_STATUS_RETURN(status, errorHandle, "Error getting statement cache size", CFL_FALSE);
   return CFL_TRUE;
}

static CFL_BOOL sdb_oci_breakExecution(SDB_CONNECTIONP conn) {
   SDB_OCI_CONNECTION *cn = (SDB_OCI_CONNECTION *) conn->handle;
   int status;
   void *errorHandle = sdb_oci_getErrorHandle();

   status = s_ociSymbols->OCIBreak(cn->serviceHandle, errorHandle);
   CHECK_STATUS_RETURN(status, errorHandle, "Error breaking execution", CFL_FALSE);
   return CFL_TRUE;
}


static CFL_BOOL resultsetGetFieldValue(SDB_CONNECTIONP pConnection, SDB_OCI_STMT *stmt, CFL_UINT16 position, SDB_FIELDP pField,
        SDB_RECORDP pRecord, CFL_BOOL bReadLarge) {
   SDB_OCI_VAR *var;
   SDB_OCI_COL_INFO *info;

   if (sdb_oci_stmt_colInfoCount(stmt) == 0) {
      sdb_thread_setError(SDB_ERROR_TYPE_SDB, SDB_OCI_ERROR_STMT_GET, "Columns info not found");
   } else if (sdb_oci_stmt_varCount(stmt) == 0) {
      sdb_thread_setError(SDB_ERROR_TYPE_SDB, SDB_OCI_ERROR_STMT_GET, "Query data variables not found");
   } else if (position < 1 || ((CFL_UINT32) position > sdb_oci_stmt_colInfoCount(stmt))) {
      sdb_thread_setError(SDB_ERROR_TYPE_SDB, SDB_OCI_ERROR_STMT_GET, "Invalid column position (%u). Max column number is %u",
              position, cfl_array_length(stmt->columnsInfo));
   } else if (sdb_oci_stmt_getFetchedRows(stmt) == 0) {
      sdb_thread_setError(SDB_ERROR_TYPE_SDB, SDB_OCI_ERROR_STMT_GET, "No records fetched for query");
   } else {
      info = sdb_oci_stmt_getColInfo(stmt, position);
      var = sdb_oci_stmt_getColVar(stmt, position);

      SDB_LOG_TRACE(("resultsetGetFieldValue: field=%s, clp type=%u, pos=%u, col=%.*s, ora type=%u, precision=%d, scale=%d",
              cfl_str_getPtr(pField->clpName), pField->clpType, position, info->colNameLen, info->colName,
              info->dataType, info->precision, info->scale));
      switch (info->dataType) {
         case SDB_SQLT_AFC:
         case SDB_SQLT_CHR:
         case SDB_SQLT_STR:
         case SDB_SQLT_VCS:
         case SDB_SQLT_AVC:
            if (pField->clpType == SDB_CLP_CHARACTER) {
               if (sdb_oci_var_isNull(var, 0) || sdb_oci_var_getStringLen(var, 0) == 0) {
                  sdb_record_setString(pRecord, pField, "", 0);
               } else {
                  sdb_record_setString(pRecord, pField, sdb_oci_var_getString(var, 0), sdb_oci_var_getStringLen(var, 0));
               }

            } else if (pField->clpType == SDB_CLP_LOGICAL) {
               if (sdb_oci_var_isNull(var, 0) || sdb_oci_var_getStringLen(var, 0) == 0) {
                  sdb_record_setLogical(pRecord, pField, CFL_FALSE);
               } else {
                  sdb_record_setLogical(pRecord, pField, hb_strnicmp(sdb_oci_var_getString(var, 0), "Y", 1) == 0 ? CFL_TRUE : CFL_FALSE);
               }
            } else {
               sdb_thread_setError(SDB_ERROR_TYPE_SDB, SDB_ERROR_INVALID_DATATYPE, "Invalid datatype at field");
            }
            break;

         case SDB_SQLT_NUM:
         case SDB_SQLT_VNU:
            if (sdb_oci_var_isNull(var, 0)) {
               if (pField->decimals > 0) {
                  sdb_record_setDouble(pRecord, pField, 0.0);
               } else {
                  sdb_record_setInt64(pRecord, pField, 0);
               }
            } else if (pField->clpType == SDB_CLP_NUMERIC ||
                    pField->clpType == SDB_CLP_BIGINT ||
                    pField->clpType == SDB_CLP_INTEGER ||
                    pField->clpType == SDB_CLP_DOUBLE ||
                    pField->clpType == SDB_CLP_FLOAT ||
                    pField->clpType == SDB_CLP_MEMO_LONG) {
               if (pField->decimals > 0) {
                  sdb_record_setDouble(pRecord, pField, sdb_oci_var_getDouble(var, 0));
               } else {
                  sdb_record_setInt64(pRecord, pField, sdb_oci_var_getInt64(var, 0));
               }
            } else if (pField->clpType == SDB_CLP_MEMO_LONG) {
               sdb_record_setInt64(pRecord, pField, sdb_oci_var_getInt64(var, 0));
            } else {
               sdb_thread_setError(SDB_ERROR_TYPE_SDB, SDB_ERROR_INVALID_DATATYPE, "Invalid datatype at field");
            }
            break;

         case SDB_SQLT_INT:
         case SDB_SQLT_UIN:
            if (pField->clpType == SDB_CLP_NUMERIC ||
                    pField->clpType == SDB_CLP_BIGINT ||
                    pField->clpType == SDB_CLP_INTEGER ||
                    pField->clpType == SDB_CLP_MEMO_LONG) {
               if (sdb_oci_var_isNull(var, 0)) {
                  sdb_record_setInt64(pRecord, pField, 0);
               } else {
                  sdb_record_setInt64(pRecord, pField, sdb_oci_var_getInt64(var, 0));
               }
            } else {
               sdb_thread_setError(SDB_ERROR_TYPE_SDB, SDB_ERROR_INVALID_DATATYPE, "Invalid datatype at field");
            }
            break;

         case SDB_SQLT_FLT:
         case SDB_SQLT_IBFLOAT:
         case SDB_SQLT_BFLOAT:
            if (pField->clpType == SDB_CLP_NUMERIC ||
                pField->clpType == SDB_CLP_DOUBLE  ||
                pField->clpType == SDB_CLP_FLOAT   ||
                pField->clpType == SDB_CLP_MEMO_LONG) {
               if (sdb_oci_var_isNull(var, 0)) {
                  sdb_record_setDouble(pRecord, pField, 0.0);
               } else {
                  sdb_record_setDouble(pRecord, pField, sdb_oci_var_getDouble(var, 0));
               }
            } else {
               sdb_thread_setError(SDB_ERROR_TYPE_SDB, SDB_ERROR_INVALID_DATATYPE, "Invalid datatype at field");
            }
            break;

         case SDB_SQLT_PDN:
         case SDB_SQLT_BDOUBLE:
         case SDB_SQLT_IBDOUBLE:
            if (pField->clpType == SDB_CLP_NUMERIC ||
                pField->clpType == SDB_CLP_DOUBLE  ||
                pField->clpType == SDB_CLP_FLOAT   ||
                pField->clpType == SDB_CLP_MEMO_LONG) {
               if (sdb_oci_var_isNull(var, 0)) {
                  sdb_record_setDouble(pRecord, pField, 0.0);
               } else {
                  sdb_record_setDouble(pRecord, pField, sdb_oci_var_getDouble(var, 0));
               }
            } else {
               sdb_thread_setError(SDB_ERROR_TYPE_SDB, SDB_ERROR_INVALID_DATATYPE, "Invalid datatype at field");
            }
            break;


         case SDB_SQLT_TIME:
         case SDB_SQLT_TIME_TZ:
         case SDB_SQLT_TIMESTAMP:
         case SDB_SQLT_TIMESTAMP_TZ:
         case SDB_SQLT_TIMESTAMP_LTZ:
         case SDB_SQLT_DATE:
         case SDB_SQLT_DAT:
         case SDB_SQLT_ODT:
            {
               CFL_INT16 year;
               CFL_INT8 month, day, hour, min, sec;
               if (pField->clpType == SDB_CLP_DATE) {
                  if (sdb_oci_var_isNull(var, 0)) {
                     sdb_record_setDate(pRecord, pField, 0, 0, 0);
                  } else {
                     sdb_oci_var_getDate(var, 0, &year, &month, &day, &hour, &min, &sec);
                     sdb_record_setDate(pRecord, pField, year, month, day);
                  }
               } else if (pField->clpType == SDB_CLP_TIMESTAMP) {
                  if (sdb_oci_var_isNull(var, 0)) {
                     sdb_record_setTimestamp(pRecord, pField, 0, 0, 0, 0, 0, 0, 0);
                  } else {
                     sdb_oci_var_getDate(var, 0, &year, &month, &day, &hour, &min, &sec);
                     sdb_record_setTimestamp(pRecord, pField, year, month, day, hour, min, sec, 0);
                  }
               } else {
                  sdb_thread_setError(SDB_ERROR_TYPE_SDB, SDB_ERROR_INVALID_DATATYPE, "Invalid datatype at field");
               }
            }
            break;

         case SDB_SQLT_LVC:
         case SDB_SQLT_LVB:
         case SDB_SQLT_LNG:
         case SDB_SQLT_BIN:
         case SDB_SQLT_LBI:
            if (pField->clpType == SDB_CLP_LONG_RAW) {
               if (sdb_oci_var_isNull(var, 0)) {
                  if (bReadLarge) {
                     hb_itemPutCL(sdb_record_getItem(pRecord, pField), "", 0);
                  } else {
                     hb_itemPutNI(sdb_record_getItem(pRecord, pField), 0);
                  }
               } else if (!bReadLarge) {
                  hb_itemPutNI(sdb_record_getItem(pRecord, pField), 1);
               } else {
                  hb_itemPutCL(sdb_record_getItem(pRecord, pField), (const char *) sdb_oci_var_getString(var, 0),
                          sdb_oci_var_getStringLen(var, 0));
               }
            } else {
               sdb_thread_setError(SDB_ERROR_TYPE_SDB, SDB_ERROR_INVALID_DATATYPE, "Invalid datatype at field");
            }
            break;

         case SDB_SQLT_CLOB:
         case SDB_SQLT_BLOB:
            if (pField->clpType == SDB_CLP_BLOB || pField->clpType == SDB_CLP_CLOB || pField->clpType == SDB_CLP_IMAGE) {
               if (sdb_oci_var_isNull(var, 0)) {
                  if (bReadLarge) {
                     hb_itemPutCL(sdb_record_getItem(pRecord, pField), "", 0);
                  } else {
                     hb_itemPutNI(sdb_record_getItem(pRecord, pField), 0);
                  }
               } else if (!bReadLarge) {
                  hb_itemPutNI(sdb_record_getItem(pRecord, pField), 1);
               } else {
                  SDB_OCI_LOB *lob = sdb_oci_var_getLob(var, 0);
                  char *buffer;
                  CFL_UINT64 lobLen;

                  if (lobLength((SDB_OCI_CONNECTION *)pConnection->handle, sdb_oci_lob_handle(lob), &lobLen)) {
                     if (lobLen > 0) {
                        buffer = SDB_MEM_ALLOC(lobLen);
                        if (sdb_oci_lob_read(lob, buffer, 0, &lobLen)) {
                           hb_itemPutCLPtr(sdb_record_getItem(pRecord, pField), buffer, (CFL_UINT32) lobLen);
                        } else {
                           hb_xfree(buffer);
                           return CFL_FALSE;
                        }
                     } else {
                        hb_itemPutCL(sdb_record_getItem(pRecord, pField), "", 0);
                     }
                  } else {
                     return CFL_FALSE;
                  }
               }
            } else {
               sdb_thread_setError(SDB_ERROR_TYPE_SDB, SDB_ERROR_INVALID_DATATYPE, "Invalid datatype at field");
            }
            break;

         case SDB_SQLT_BOL:
            if (pField->clpType == SDB_CLP_LOGICAL) {
               if (sdb_oci_var_isNull(var, 0)) {
                  sdb_record_setLogical(pRecord, pField, CFL_FALSE);
               } else {
                  sdb_record_setLogical(pRecord, pField, sdb_oci_var_getBool(var, 0));
               }
            } else {
               sdb_thread_setError(SDB_ERROR_TYPE_SDB, SDB_ERROR_INVALID_DATATYPE, "Invalid datatype at field");
            }
            break;

         case SDB_SQLT_RID:
         case SDB_SQLT_RDD:
            if (pField->clpType == SDB_CLP_ROWID) {
               if (sdb_oci_var_isNull(var, 0)) {
                  hb_itemClear(sdb_record_getItem(pRecord, pField));
               } else {
                  SDB_OCI_ROWID *rowId = sdb_oci_var_getRowId(var, 0);
                  hb_itemPutPtr(sdb_record_getItem(pRecord, pField), rowId);
                  if (rowId != NULL) {
                     sdb_oci_rowid_incRef(rowId);
                  }
               }
            } else {
               sdb_thread_setError(SDB_ERROR_TYPE_SDB, SDB_ERROR_INVALID_DATATYPE, "Invalid datatype at field");
            }
            break;

         case SDB_SQLT_RSET:
            if (pField->clpType == SDB_CLP_CURSOR) {
               if (sdb_oci_var_isNull(var, 0)) {
                  hb_itemClear(sdb_record_getItem(pRecord, pField));
               } else {
                  SDB_OCI_STMT *ociStmt = sdb_oci_var_getStmt(var, 0);
                  SDB_STATEMENTP pStmt = sdb_stmt_new(pConnection, ociStmt);
                  sdb_stmt_itemPut(sdb_record_getItem(pRecord, pField), pStmt);
                  pStmt->isCursor = CFL_TRUE;
                  sdb_stmt_setNumCols(pStmt, sdb_oci_stmt_getColCount(ociStmt));
                  sdb_stmt_setType(pStmt, sdb_oci_stmt_getType(ociStmt));
                  if (ociStmt != NULL) {
                     sdb_oci_stmt_incRef(ociStmt);
                  }
               }
            } else {
               sdb_thread_setError(SDB_ERROR_TYPE_SDB, SDB_ERROR_INVALID_DATATYPE, "Invalid datatype at field");
            }
            break;

         default:
            sdb_thread_setError(SDB_ERROR_TYPE_SDB, SDB_ERROR_INVALID_DATATYPE, "Invalid datatype at field");
            return CFL_FALSE;
      }
      return CFL_TRUE;
   }
   return CFL_FALSE;
}

static CFL_BOOL resultsetGetValue(SDB_CONNECTIONP pConnection, SDB_OCI_STMT *stmt, CFL_UINT16 position, PHB_ITEM pItem) {
   SDB_OCI_VAR *var;
   SDB_OCI_COL_INFO *info;

   if (sdb_oci_stmt_colInfoCount(stmt) == 0) {
      sdb_thread_setError(SDB_ERROR_TYPE_SDB, SDB_OCI_ERROR_STMT_GET, "Columns info not found");
   } else if (sdb_oci_stmt_varCount(stmt) == 0) {
      sdb_thread_setError(SDB_ERROR_TYPE_SDB, SDB_OCI_ERROR_STMT_GET, "Query data variables not found");
   } else if (position < 1 || ((CFL_UINT32) position > sdb_oci_stmt_colInfoCount(stmt))) {
      sdb_thread_setError(SDB_ERROR_TYPE_SDB, SDB_OCI_ERROR_STMT_GET, "Invalid column position (%u). Max column number is %u",
              position, cfl_array_length(stmt->columnsInfo));
   } else if (sdb_oci_stmt_getFetchedRows(stmt) == 0) {
      sdb_thread_setError(SDB_ERROR_TYPE_SDB, SDB_OCI_ERROR_STMT_GET, "No records fetched for query");
   } else {
      info = sdb_oci_stmt_getColInfo(stmt, position);
      var = sdb_oci_stmt_getColVar(stmt, position);

      SDB_LOG_TRACE(("resultsetGetValue: pos=%u, col=%.*s, ora type=%u, precision=%d, scale=%d",
              position, info->colNameLen, info->colName, info->dataType, info->precision, info->scale));
      switch (info->dataType) {
         case SDB_SQLT_AFC:
         case SDB_SQLT_CHR:
         case SDB_SQLT_STR:
         case SDB_SQLT_VCS:
         case SDB_SQLT_AVC:
            if (sdb_oci_var_isNull(var, 0) || sdb_oci_var_getStringLen(var, 0) == 0) {
               hb_itemPutCL(pItem, "", 0);
            } else {
               hb_itemPutCL(pItem, sdb_oci_var_getString(var, 0), sdb_oci_var_getStringLen(var, 0));
            }
            break;

         case SDB_SQLT_NUM:
         case SDB_SQLT_VNU:
            if (sdb_oci_var_isNull(var, 0)) {
               if (info->scale > 0) {
                  hb_itemPutND(pItem, 0.0);
               } else {
                  hb_itemPutNI(pItem, 0);
               }
            } else {
               if (info->scale > 0) {
                  hb_itemPutND(pItem, sdb_oci_var_getDouble(var, 0));
               } else {
                  hb_itemPutNLL(pItem, (HB_LONGLONG) sdb_oci_var_getInt64(var, 0));
               }
            }
            break;

         case SDB_SQLT_ODT:
         case SDB_SQLT_DAT:
            {
               CFL_INT16 year;
               CFL_INT8 month, day, hour, min, sec;
               if (sdb_oci_var_isNull(var, 0)) {
                  hb_itemPutD(pItem, 0, 0, 0);
               } else {
                  sdb_oci_var_getDate(var, 0, &year, &month, &day, &hour, &min, &sec);
                  hb_itemPutD(pItem, year, month, day);
               }
            }
            break;

         case SDB_SQLT_TIME:
         case SDB_SQLT_TIME_TZ:
         case SDB_SQLT_TIMESTAMP:
         case SDB_SQLT_TIMESTAMP_TZ:
         case SDB_SQLT_TIMESTAMP_LTZ:
         case SDB_SQLT_DATE:
            {
               CFL_INT16 year;
               CFL_INT8 month, day, hour, min, sec;
               if (sdb_oci_var_isNull(var, 0)) {
                  hb_itemPutD(pItem, 0, 0, 0);
               } else {
                  sdb_oci_var_getDate(var, 0, &year, &month, &day, &hour, &min, &sec);
                  #ifdef __HBR__
                     hb_itemPutTDT(pItem, hb_dateEncode(year, month, day), hb_timeEncode(hour, min, sec, 0));
                  #else
                     hb_itemPutD(pItem, year, month, day);
                  #endif
               }
            }
            break;

         case SDB_SQLT_LVC:
         case SDB_SQLT_LVB:
         case SDB_SQLT_LNG:
         case SDB_SQLT_BIN:
         case SDB_SQLT_LBI:
            if (sdb_oci_var_isNull(var, 0)) {
               hb_itemPutCL(pItem, "", 0);
            } else {
               hb_itemPutCL(pItem, sdb_oci_var_getString(var, 0), sdb_oci_var_getStringLen(var, 0));
            }
            break;

         case SDB_SQLT_CLOB:
            if (sdb_oci_var_isNull(var, 0)) {
               hb_itemClear(pItem);
            } else {
               SDB_OCI_LOB *lob = sdb_oci_var_getLob(var, 0);
               sdb_lob_itemPut(pItem, sdb_lob_new(pConnection, SDB_CLP_CLOB, lob));
               if (lob != NULL) {
                  sdb_oci_lob_incRef(lob);
               }
            }
            break;

         case SDB_SQLT_BLOB:
            if (sdb_oci_var_isNull(var, 0)) {
               hb_itemClear(pItem);
            } else {
               SDB_OCI_LOB *lob = sdb_oci_var_getLob(var, 0);
               sdb_lob_itemPut(pItem, sdb_lob_new(pConnection, SDB_CLP_BLOB, lob));
               if (lob != NULL) {
                  sdb_oci_lob_incRef(lob);
               }
            }
            break;

         case SDB_SQLT_FLT:
         case SDB_SQLT_IBFLOAT:
         case SDB_SQLT_BFLOAT:
            if (sdb_oci_var_isNull(var, 0)) {
               hb_itemPutND(pItem, 0.0);
            } else {
               hb_itemPutND(pItem, sdb_oci_var_getDouble(var, 0));
            }
            break;

         case SDB_SQLT_PDN:
         case SDB_SQLT_BDOUBLE:
         case SDB_SQLT_IBDOUBLE:
            if (sdb_oci_var_isNull(var, 0)) {
               hb_itemPutND(pItem, 0.0);
            } else {
               hb_itemPutND(pItem, sdb_oci_var_getDouble(var, 0));
            }
            break;

         case SDB_SQLT_INT:
         case SDB_SQLT_UIN:
            if (sdb_oci_var_isNull(var, 0)) {
               hb_itemPutNI(pItem, 0);
            } else {
               hb_itemPutNLL(pItem, (HB_LONGLONG) sdb_oci_var_getInt64(var, 0));
            }
            break;

         case SDB_SQLT_RID:
         case SDB_SQLT_RDD:
            if (sdb_oci_var_isNull(var, 0)) {
               hb_itemClear(pItem);
            } else {
               SDB_OCI_ROWID *rowId = sdb_oci_var_getRowId(var, 0);
               hb_itemPutPtr(pItem, rowId);
               if (rowId != NULL) {
                  sdb_oci_rowid_incRef(rowId);
               }
            }
            break;

         case SDB_SQLT_RSET:
            if (sdb_oci_var_isNull(var, 0)) {
               hb_itemClear(pItem);
            } else {
               SDB_OCI_STMT *ociStmt = sdb_oci_var_getStmt(var, 0);
               SDB_STATEMENTP pStmt = sdb_stmt_new(pConnection, ociStmt);
               sdb_stmt_itemPut(pItem, pStmt);
               pStmt->isCursor = CFL_TRUE;
               sdb_stmt_setNumCols(pStmt, sdb_oci_stmt_getColCount(ociStmt));
               sdb_stmt_setType(pStmt, sdb_oci_stmt_getType(ociStmt));
               if (ociStmt != NULL) {
                  sdb_oci_stmt_incRef(ociStmt);
               }
            }
            break;

         case SDB_SQLT_BOL:
            if (sdb_oci_var_isNull(var, 0)) {
               hb_itemPutL(pItem, CFL_FALSE);
            } else {
               hb_itemPutL(pItem, sdb_oci_var_getBool(var, 0));
            }
            break;

         default:
            sdb_thread_setError(SDB_ERROR_TYPE_SDB, SDB_ERROR_INVALID_DATATYPE, "Invalid datatype at field");
            return CFL_FALSE;
      }
      SDB_LOG_TRACE(("resultsetGetValue() value=%s", ITEM_STR(pItem)));
      return CFL_TRUE;
   }
   return CFL_FALSE;
}

static CFL_BOOL sdb_oci_getFieldValue(SDB_STATEMENTP pStatement, CFL_UINT16 pos, SDB_FIELDP field, SDB_RECORDP record) {
   return resultsetGetFieldValue(pStatement->connection, (SDB_OCI_STMT *) pStatement->handle, pos, field, record, CFL_FALSE);
}

static CFL_BOOL sdb_oci_getQueryValue(SDB_STATEMENTP pStatement, CFL_UINT16 pos, PHB_ITEM pItem) {
   return resultsetGetValue(pStatement->connection, (SDB_OCI_STMT *) pStatement->handle, pos, pItem);
}

static CFL_BOOL sdb_oci_getMemoValue(SDB_STATEMENTP pStatement, CFL_UINT16 pos, SDB_TABLEP table, SDB_FIELDP field, SDB_RECORDP record, SDB_RECNO recno) {
   CFL_BOOL fSuccess = CFL_FALSE;

   ENTER_FUN_NAME("sdb_oci_getMemoValue");
   SDB_LOG_DEBUG(("sdb_oci_getMemoValue: recno=%dll", recno));
   if (field->clpType != SDB_CLP_MEMO_LONG) {
      fSuccess = resultsetGetFieldValue(pStatement->connection, (SDB_OCI_STMT *) pStatement->handle, pos, field, record, CFL_TRUE);
   } else {
      SDB_STATEMENTP pMemoStmt;
      CFL_UINT64 affectedRows;
      CFL_STRP sql = cfl_str_new(128);

      cfl_str_setFormat(sql, "select MEMO from %s.%s_MEMO where RECNO=:1 and COL_NAME=:2",
              cfl_str_getPtr(table->dbSchema->name), cfl_str_getPtr(table->dbName));
      SDB_LOG_DEBUG(("Get memo: %s", cfl_str_getPtr(sql)));
      pMemoStmt = sdb_connection_prepareStatement(pStatement->connection, sql);
      if (pMemoStmt != NULL) {
         sdb_stmt_setInt64ByPos(pMemoStmt, 1, recno, CFL_FALSE);
         sdb_stmt_setStringByPos(pMemoStmt, 2, field->clpName, CFL_FALSE);
         if (sdb_stmt_execute(pMemoStmt, &affectedRows)) {
            fSuccess = CFL_TRUE;
            if (sdb_stmt_fetchNext(pMemoStmt, CFL_FALSE)) {
               sdb_stmt_getQueryValue(pMemoStmt, 1, sdb_record_getItem(record, field));
            } else {
               hb_itemPutCL(sdb_record_getItem(record, field), "", 0);
            }
         }
         sdb_stmt_free(pMemoStmt);
      }
      cfl_str_free(sql);
   }
   RETURN fSuccess;
}

static CFL_SQL_BUILDERP sdb_oci_sqlBuilder(void) {
   return &s_sqlBuilder;
}

static SDB_DB_API s_ociTable = {
   (SDB_DB_FNC_INIT) sdb_oci_initialize,
   (SDB_DB_FNC_FINALIZE) sdb_oci_finalize,
   (SDB_DB_FNC_CONNECT) sdb_oci_connect,
   (SDB_DB_FNC_DISCONNECT) sdb_oci_disconnect,
   (SDB_DB_FNC_LCK_TAB) sdb_oci_lockTable,
   (SDB_DB_FNC_ULCK_TAB) sdb_oci_unlockTable,
   (SDB_DB_FNC_SET_PRE_FETCH_SIZE) sdb_oci_setPreFetchSize,
   (SDB_DB_FNC_FETCH) sdb_oci_fetchNext,
   (SDB_DB_FNC_GET_FLD_VAL) sdb_oci_getFieldValue,
   (SDB_DB_FNC_GET_QRY_VAL) sdb_oci_getQueryValue,
   (SDB_DB_FNC_GET_MEMO_VAL) sdb_oci_getMemoValue,
   (SDB_DB_FNC_TAB_DROP_IDX) sdb_oci_tableDropIndex,
   (SDB_DB_FNC_LCK_REC) sdb_oci_lockRecord,
   (SDB_DB_FNC_ULCK_REC) sdb_oci_unlockRecord,
   (SDB_DB_FNC_DROP_TAB) sdb_oci_dropTable,
   (SDB_DB_FNC_CREATE_TAB) sdb_oci_createTable,
   (SDB_DB_FNC_ADD_COL) sdb_oci_tableAddColumn,
   (SDB_DB_FNC_MOD_COL) sdb_oci_tableModifyColumn,
   (SDB_DB_FNC_CREATE_IDX) sdb_oci_createIndex,
   (SDB_DB_FNC_BEG_TRANS) sdb_oci_beginTransaction,
   (SDB_DB_FNC_PREP_TRANS) sdb_oci_prepareTransaction,
   (SDB_DB_FNC_COMM_TRANS) sdb_oci_commitTransaction,
   (SDB_DB_FNC_ROLL_TRANS) sdb_oci_rollbackTransaction,
   (SDB_DB_FNC_EXEC_PROC) sdb_oci_executeProcedure,
   (SDB_DB_FNC_EXEC_STMT) sdb_oci_executeStatement,
   (SDB_DB_FNC_EXEC_MANY_STMT) sdb_oci_executeStatementMany,
   (SDB_DB_FNC_CLOSE_STMT) sdb_oci_closeStatement,
   (SDB_DB_FNC_IS_CONN) sdb_oci_isConnected,
   (SDB_DB_FNC_TAB_REN) sdb_oci_tableRename,
   (SDB_DB_FNC_PREP_STMT) sdb_oci_prepareStatement,
   (SDB_DB_FNC_STMT_TYPE) sdb_oci_statementType,
   (SDB_DB_FNC_SUP_ROWID) sdb_oci_isRowIdSuppported,
   (SDB_DB_FNC_IS_ERR_TYPE) sdb_oci_isErrorType,
   (SDB_DB_FNC_EXEC_FUN) sdb_oci_executeFunction,
   (SDB_DB_FNC_QRY_INFO) sdb_oci_getQueryInfo,
   (SDB_DB_FNC_CLP2SQL) sdb_ora_clipperToSql,
   (SDB_DB_FNC_CREATE_LOB) sdb_oci_createLob,
   (SDB_DB_FNC_FREE_LOB) sdb_oci_releaseLob,
   (SDB_DB_FNC_OPEN_LOB) sdb_oci_openLob,
   (SDB_DB_FNC_CLOSE_LOB) sdb_oci_closeLob,
   (SDB_DB_FNC_READ_LOB) sdb_oci_readLob,
   (SDB_DB_FNC_WRITE_LOB) sdb_oci_writeLob,
   (SDB_DB_FNC_SERVER_VERSION) sdb_oci_getServerVersion,
   (SDB_DB_FNC_GET_SCHEMA) sdb_oci_getCurrentSchema,
   (SDB_DB_FNC_SET_SCHEMA) sdb_oci_setCurrentSchema,
   (SDB_DB_FNC_GET_STMT_CACHE) sdb_oci_getStatementCache,
   (SDB_DB_FNC_SET_STMT_CACHE) sdb_oci_setStatementCache,
   (SDB_DB_FNC_BREAK_OPERATION) sdb_oci_breakExecution,
   (SDB_DB_FNC_CLIENT_VERSION) sdb_oci_getClientVersion,
   (SDB_DB_FNC_SQL_BUILDER) sdb_oci_sqlBuilder
};


SDB_DB_APIP sdb_oci_getAPI(void) {
   return &s_ociTable;
}

HB_FUNC(SDB_ORAGETAPI) {
   hb_retptr(&s_ociTable);
}

HB_FUNC(SDB_ORAREGISTERDRIVER) {
   CFL_BOOL bSuccess;
   sdb_thread_cleanError();
   bSuccess = sdb_api_registerProductAPI("ORACLE", "Oracle Database", &s_ociTable);
   if (sdb_thread_hasError()) {
      sdb_api_genErrorFromSDBError(NULL, "sdb_oci.c", EF_NONE, sdb_thread_getLastError(), "SDB_ORAREGISTERDRIVER", NULL);
   } else if (! bSuccess) {
      sdb_api_genError(NULL, SDB_ERROR_TYPE_OCILIB, SDB_ERROR_REGISTERING_DRIVER, "sdb_oci.c", EF_NONE, "Unable to register Oracle driver in SDB: unexpected error", "SDB_ORAREGISTERDRIVER", NULL);
   }
   hb_retl(bSuccess);
}

// HB_FUNC(SDB_ROWIDNEW)
// HB_FUNC(SDB_ROWITOSTRING)
// HB_FUNC(SDB_ROWIDFREE)
