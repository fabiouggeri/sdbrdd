#include <time.h>

#include "cfl_types.h"
#include "cfl_thread.h"
#include "cfl_str.h"

#include "hbapiitm.h"

#include "sdb_thread.h"
#include "sdb_connection.h"
#include "sdb_statement.h"
#include "sdb_param.h"
#include "sdb_error.h"
#include "sdb_util.h"
#include "sdb_api.h"
#include "sdb_log.h"

#ifdef __XHB__
   #define HB_CURRENT_THREAD GetCurrentThreadId
#endif

static void initData(void *data);
static void freeData(void *data);

static CFL_THREAD_VAR_INIT(SDB_THREAD_DATA, s_threadData, initData, freeData);

static void initData(void *threadData) {
   if (threadData != NULL) {
      SDB_THREAD_DATAP data = (SDB_THREAD_DATAP) threadData;
      data->objectType = SDB_OBJ_THREAD;
      data->connection = sdb_api_defaultConnection();
      sdb_error_init(&(data->error));
      data->pStatement = NULL;
      data->isDelayAppend = CFL_TRUE;
      data->paramList = NULL;
      #ifdef __XHB__
         data->threadId = HB_CURRENT_THREAD();
      #else
         data->threadId = hb_threadNO();
      #endif
      data->nextBufferFetchSize = 0;
      data->areas = cfl_list_new(50);
      data->dbAPIData = NULL;
      data->defaultMemoType = sdb_api_getDefaultMemoType();
      data->defaultBufferFetchSize = sdb_api_getDefaultBufferFetchSize();
      data->isTrimParams = sdb_api_isTrimParams();
      data->isPadFields = CFL_TRUE;
      data->isNullable = CFL_FALSE;
      time(&data->lastStmtVerification);
   }
}

static void freeData(void *threadData) {
   SDB_THREAD_DATAP data = (SDB_THREAD_DATAP) threadData;
   if (data == NULL) {
      return;
   }
   sdb_error_free(&data->error);
   sdb_thread_freeParams(data);
   cfl_list_free(data->areas);
   data->areas = NULL;
   SDB_MEM_FREE(data);
}

SDB_THREAD_DATAP sdb_thread_getData(void) {
   return (SDB_THREAD_DATAP) cfl_thread_varGet(&s_threadData);
}

void sdb_thread_clear(SDB_THREAD_DATAP data) {
   ENTER_FUN;
   data->connection = NULL;
   sdb_error_clear(&data->error);
   sdb_thread_freeParams(data);
   RETURN;
}

SDB_ERRORP sdb_thread_getLastError(void) {
   return &(sdb_thread_getData()->error);
}

void sdb_thread_cleanError(void) {
   ENTER_FUN_NAME("sdb_thread_cleanError");
   sdb_error_clear(sdb_thread_getLastError());
   RETURN;
}

void sdb_thread_setError(int errorType, int errorCode, const char * message, ...) {
   SDB_ERRORP error = sdb_thread_getLastError();
   va_list pArgs;

   ENTER_FUN_NAME("sdb_thread_setError");
   va_start(pArgs, message);
   sdb_error_setArgs(error, errorType, errorCode, message, pArgs);
   SDB_LOG_ERROR((cfl_str_getPtr(error->message)));
   va_end(pArgs);
   RETURN;
}

void sdb_thread_addArea(SDB_AREAP pSDBArea) {
   cfl_list_add(sdb_thread_getData()->areas, pSDBArea);
}

void sdb_thread_delArea(SDB_AREAP pSDBArea) {
   SDB_THREAD_DATAP thData = sdb_thread_getData();
   CFL_UINT32 len;
   CFL_UINT32 index;

   len = cfl_list_length(thData->areas);
   for (index = 0; index < len; index++) {
      SDB_AREAP area = (SDB_AREAP) cfl_list_get(thData->areas, index);
      if (area == pSDBArea) {
         cfl_list_del(thData->areas, index);
         break;
      }
   }
}

SDB_PARAMLISTP sdb_thread_getCreateParams(SDB_THREAD_DATAP data) {
   if (data->paramList == NULL) {
      data->paramList = sdb_param_listNew();
   }
   return data->paramList;
}

void sdb_thread_freeParams(SDB_THREAD_DATAP data) {
   if (data->paramList != NULL) {
      sdb_param_listFree(data->paramList);
      data->paramList = NULL;
   }
}
