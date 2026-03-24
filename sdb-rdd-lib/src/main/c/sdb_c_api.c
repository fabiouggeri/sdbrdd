#include <stdlib.h>

#include "hbapirdd.h"

#include "cfl_types.h"

#include "sdb_api.h"
#include "sdb_connection.h"
#include "sdb_product.h"
#include "sdb_error.h"
#include "sdb_log.h"
#include "sdb_lock_client.h"
#include "sdb_thread.h"
#include "sdb_util.h"

SDB_CONNECTIONP sdb_login(const char *database, const char *username, const char *password, const char *productId,
                          const char *lockControl, CFL_UINT16 lockServerPort, CFL_BOOL registerConn) {
   SDB_PRODUCTP product;
   SDB_CONNECTIONP conn;

   ENTER_FUNP(("sdb_login. database=%s username=%s product=%s lock-mode=%s", database, username, productId, lockControl));
   sdb_thread_cleanError();
   if (sdb_util_isEmpty(productId)) {
      product = sdb_api_getProduct(NULL);
   } else {
      product = sdb_api_getProduct(productId);
   }
   if (product == NULL) {
      sdb_thread_setError(SDB_ERROR_TYPE_SDB, SDB_ERROR_PRODUCT_NOT_FOUND, "Product not registered");
   } else if (database == NULL) {
      sdb_thread_setError(SDB_ERROR_TYPE_SDB, SDB_ERROR_INVALID_ARGUMENT, "Database not provided");
   } else if (username == NULL) {
      sdb_thread_setError(SDB_ERROR_TYPE_SDB, SDB_ERROR_INVALID_ARGUMENT, "User not provided");
   } else if (password == NULL) {
      sdb_thread_setError(SDB_ERROR_TYPE_SDB, SDB_ERROR_INVALID_ARGUMENT, "Password not provided");
   } else {
      CFL_UINT8 lockMode;
      SDB_LS_CLIENTP lockClient = NULL;

      if (! sdb_util_isEmpty(lockControl)) {
         if (sdb_util_isNumeric(lockControl)) {
            lockMode = (CFL_UINT8) strtol(lockControl, NULL, 10);
            if(lockMode >= SDB_LOCK_CONTROL_SERVER) {
               lockMode = SDB_LOCK_CONTROL_DB;
            }
         } else if (hb_strnicmp(lockControl, "true", 4) == 0) {
            lockMode = SDB_LOCK_CONTROL_DB;
         } else if (hb_strnicmp(lockControl, "false", 4) == 0) {
            lockMode = SDB_LOCK_CONTROL_NONE;
         } else {
            lockMode = SDB_LOCK_CONTROL_SERVER;
            lockClient = sdb_lckcli_connect(lockControl, lockServerPort == 0 ? SDB_DEFAULT_LOCK_SERVER_PORT : lockServerPort);
            if (lockClient == NULL) {
               RETURN NULL;
            }
         }
      } else {
         lockMode = SDB_LOCK_CONTROL_DB;
      }

      conn = sdb_api_connect(product, database, username, password, registerConn);
      if (conn) {
         conn->lockControl = lockMode;
         conn->lockClient = lockClient;
         RETURN conn;
      } else if (lockClient != NULL) {
         sdb_lckcli_disconnect(lockClient);
      }
   }
   RETURN NULL;
}

CFL_BOOL sdb_logout(SDB_CONNECTIONP conn) {
   ENTER_FUN_NAME("sdb_logout");
   sdb_thread_cleanError();
   if (conn == NULL) {
      RETURN CFL_FALSE;
   }
   if (conn->lockClient) {
      sdb_lckcli_disconnect(conn->lockClient);
   }
   sdb_api_disconnect(conn);
   RETURN ! sdb_thread_hasError() ? CFL_TRUE : CFL_FALSE;
}

CFL_BOOL sdb_isLogged(SDB_CONNECTIONP conn) {
   CFL_BOOL logged;
   ENTER_FUN_NAME("sdb_isLogged");
   logged = conn != NULL && sdb_connection_isOpen(conn) ? CFL_TRUE : CFL_FALSE;
   RETURN logged;
}

CFL_BOOL sdb_breakOperation(SDB_CONNECTIONP conn) {
   CFL_BOOL breaked;
   ENTER_FUN_NAME("sdb_breakOperation");
   sdb_thread_cleanError();
   breaked = conn != NULL && sdb_connection_breakOperation(conn) ? CFL_TRUE : CFL_FALSE;
   RETURN breaked;
}

SDB_CONNECTIONP sdb_currentConnection(void) {
   return sdb_thread_getData()->connection;
}

void sdb_setCurrentConnection(SDB_CONNECTIONP conn) {
   sdb_thread_getData()->connection = conn;
}

char *sdb_getErrorMsg(void) {
   return cfl_str_getPtr(sdb_thread_getLastError()->message);
}

CFL_BOOL sdb_beginTransaction(SDB_CONNECTIONP conn, CFL_INT32 formatId, const char *globalId, const char *branchId) {
   CFL_UINT32 len;
   CFL_UINT32 index;
   CFL_BOOL bBeginTransaction = CFL_FALSE;

   sdb_thread_cleanError();
   if (conn != NULL && conn->transaction == NULL) {
      CFL_STR strGlobalId;
      CFL_STR strBranchId;

      len = cfl_list_length(conn->areas);
      for (index = 0; index < len; index++) {
         if (SELF_FLUSH((AREAP)cfl_list_get(conn->areas, index)) != HB_SUCCESS) {
            RETURN CFL_FALSE;
         }
      }
      if (globalId != NULL) {
         cfl_str_initConst(&strGlobalId, globalId);
      }
      if (branchId != NULL) {
         cfl_str_initConst(&strBranchId, branchId);
      }
      bBeginTransaction = sdb_connection_beginTransaction(conn,
                                                          formatId,
                                                          globalId != NULL ? &strGlobalId : NULL,
                                                          branchId != NULL ?  &strBranchId : NULL);
      if (globalId != NULL) {
         cfl_str_free(&strGlobalId);
      }
      if (branchId != NULL) {
         cfl_str_free(&strBranchId);
      }
   }
   RETURN bBeginTransaction;
}

CFL_BOOL sdb_commitTransaction(SDB_CONNECTIONP conn) {
   CFL_UINT32 len;
   CFL_UINT32 index;
   CFL_BOOL bCommit = CFL_FALSE;

   ENTER_FUN_NAME("sdb_commitTransaction");
   sdb_thread_cleanError();
   if (conn != NULL) {
      len = cfl_list_length(conn->areas);
      for (index = 0; index < len; index++) {
         SDB_AREAP pSDBArea = (SDB_AREAP) cfl_list_get(conn->areas, index);
         if (SELF_UNLOCK( (AREAP)pSDBArea, 0 ) != HB_SUCCESS) {
            RETURN CFL_FALSE;
         }
      }
      bCommit = sdb_connection_commit(conn, CFL_FALSE) && ! sdb_thread_hasError() ? CFL_TRUE : CFL_FALSE;
   }
   RETURN bCommit;
}

CFL_BOOL sdb_rollbackTransaction(SDB_CONNECTIONP conn) {
   CFL_BOOL bRollback;

   ENTER_FUN_NAME("sdb_rollbackTransaction");
   sdb_thread_cleanError();
   bRollback = conn != NULL && sdb_connection_rollback(conn, CFL_FALSE) && ! sdb_thread_hasError() ? CFL_TRUE : CFL_FALSE;
   RETURN bRollback;
}

CFL_BOOL sdb_isTransaction(SDB_CONNECTIONP conn) {
   return conn != NULL && conn->transaction != NULL ? CFL_TRUE : CFL_FALSE;
}

/**
 * Last error code
 * @return the code of the last error occurred
 */
CFL_INT32 sdb_getErrorCode(void) {
   return sdb_thread_getLastError()->code;
}
