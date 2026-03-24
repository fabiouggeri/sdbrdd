#include <time.h>

#include "hbapiitm.h"
#include "hbdate.h"

#include "sdb_statement.h"
#include "sdb_connection.h"
#include "sdb_param.h"
#include "sdb_info.h"
#include "sdb_lob.h"
#include "sdb_util.h"
#include "sdb_field.h"
#include "sdb_log.h"
#include "sdb_error.h"
#include "sdb_api.h"
#include "sdb_thread.h"

#define FUNCTION_GET1_SET1(funName, typeName, sdbType, itemGetFun, itemPutFun, defValue) \
   typeName sdb_stmt_get##funName(SDB_STATEMENTP pStatement, const char *name) { \
      SDB_PARAMP param = sdb_param_listGet(sdb_stmt_getParams(pStatement), name); \
      if (param != NULL) { \
         return (typeName) itemGetFun(sdb_param_getItem(param)); \
      } \
      return defValue; \
   } \
   typeName sdb_stmt_get##funName##ByPos(SDB_STATEMENTP pStatement, CFL_UINT32 pos) { \
      SDB_PARAMP param = sdb_param_listGetPos(sdb_stmt_getParams(pStatement), pos); \
      if (param != NULL) { \
         return (typeName) itemGetFun(sdb_param_getItem(param)); \
      } \
      return defValue; \
   } \
   SDB_PARAMP sdb_stmt_set##funName(SDB_STATEMENTP pStatement, const char *name, typeName val, CFL_BOOL out) { \
      SDB_PARAMP param = findAddParamByName(sdb_stmt_getParams(pStatement), name); \
      if (param != NULL) { \
         sdb_param_setOut(param, out); \
         sdb_param_setType(param, sdbType); \
         itemPutFun(sdb_param_getItem(param), val); \
      } \
      return param; \
   } \
   SDB_PARAMP sdb_stmt_set##funName##ByPos(SDB_STATEMENTP pStatement, CFL_UINT32 pos, typeName val, CFL_BOOL out) { \
      SDB_PARAMP param = findAddParamByPos(sdb_stmt_getParams(pStatement), pos); \
      if (param != NULL) { \
         sdb_param_setOut(param, out); \
         sdb_param_setType(param, sdbType); \
         itemPutFun(sdb_param_getItem(param), val); \
      } \
      return param; \
   } \
   SDB_PARAMP sdb_stmt_set##funName##Null(SDB_STATEMENTP pStatement, const char *name, CFL_BOOL out) { \
      SDB_PARAMP param = findAddParamByName(sdb_stmt_getParams(pStatement), name); \
      if (param != NULL) { \
         sdb_param_setOut(param, out); \
         sdb_param_setValue(param, NULL); \
      } \
      return param; \
   } \
   SDB_PARAMP sdb_stmt_set##funName##NullByPos(SDB_STATEMENTP pStatement, CFL_UINT32 pos, CFL_BOOL out) { \
      SDB_PARAMP param = findAddParamByPos(sdb_stmt_getParams(pStatement), pos); \
      if (param != NULL) { \
         sdb_param_setOut(param, out); \
         sdb_param_setType(param, sdbType); \
         sdb_param_setValue(param, NULL); \
      } \
      return param; \
   }

#define FUNCTION_GET2_SET2(funName, typeName, sdbType, itemGetFun, itemPutFun, defValue) \
   typeName sdb_stmt_get##funName(SDB_STATEMENTP pStatement, const char *name, typeName arg) { \
      SDB_PARAMP param = sdb_param_listGet(sdb_stmt_getParams(pStatement), name); \
      if (param != NULL) { \
         return (typeName) itemGetFun(sdb_param_getItem(param), arg); \
      } \
      return defValue; \
   } \
   typeName sdb_stmt_get##funName##ByPos(SDB_STATEMENTP pStatement, CFL_UINT32 pos, typeName arg) { \
      SDB_PARAMP param = sdb_param_listGetPos(sdb_stmt_getParams(pStatement), pos); \
      if (param != NULL) { \
         return (typeName) itemGetFun(sdb_param_getItem(param), arg); \
      } \
      return defValue; \
   } \
   SDB_PARAMP sdb_stmt_set##funName(SDB_STATEMENTP pStatement, const char *name, typeName val, CFL_BOOL out) { \
      SDB_PARAMP param = findAddParamByName(sdb_stmt_getParams(pStatement), name); \
      if (param != NULL) { \
         sdb_param_setOut(param, out); \
         sdb_param_setType(param, sdbType); \
         itemPutFun(sdb_param_getItem(param), val); \
      } \
      return param; \
   } \
   SDB_PARAMP sdb_stmt_set##funName##ByPos(SDB_STATEMENTP pStatement, CFL_UINT32 pos, typeName val, CFL_BOOL out) { \
      SDB_PARAMP param = findAddParamByPos(sdb_stmt_getParams(pStatement), pos); \
      if (param != NULL) { \
         sdb_param_setOut(param, out); \
         sdb_param_setType(param, sdbType); \
         itemPutFun(sdb_param_getItem(param), val); \
      } \
      return param; \
   } \
   SDB_PARAMP sdb_stmt_set##funName##Null(SDB_STATEMENTP pStatement, const char *name, CFL_BOOL out) { \
      SDB_PARAMP param = findAddParamByName(sdb_stmt_getParams(pStatement), name); \
      if (param != NULL) { \
         sdb_param_setOut(param, out); \
         sdb_param_setType(param, sdbType); \
         sdb_param_setValue(param, NULL); \
      } \
      return param; \
   } \
   SDB_PARAMP sdb_stmt_set##funName##NullByPos(SDB_STATEMENTP pStatement, CFL_UINT32 pos, CFL_BOOL out) { \
      SDB_PARAMP param = findAddParamByPos(sdb_stmt_getParams(pStatement), pos); \
      if (param != NULL) { \
         sdb_param_setOut(param, out); \
         sdb_param_setType(param, sdbType); \
         sdb_param_setValue(param, NULL); \
      } \
      return param; \
   }

static HB_GARBAGE_FUNC(stmt_destructor) {
   SDB_STATEMENTP *pStmtPtr = (SDB_STATEMENTP *) Cargo;

   if (pStmtPtr != NULL) {
      SDB_STATEMENTP pStatement = (SDB_STATEMENTP) *pStmtPtr;
      if (pStatement != NULL) {
         pStatement->handle = NULL;
         sdb_stmt_free(pStatement);
         *pStmtPtr = NULL;
      }
   }
}

#ifdef __HBR__
static const HB_GC_FUNCS s_gcStmtFuncs = { stmt_destructor, hb_gcDummyMark };
#endif

SDB_STATEMENTP sdb_stmt_new(SDB_CONNECTIONP conn, void *handle) {
   SDB_STATEMENTP pStatement = (SDB_STATEMENTP) SDB_MEM_ALLOC(sizeof(SDB_STATEMENT));
   pStatement->objectType = SDB_OBJ_STATEMENT;
   pStatement->connection = conn;
   pStatement->handle = handle;
   pStatement->type = SDB_STMT_UNKNOWN;
   pStatement->precision = conn ? conn->queryDefaultPrecision : 21;
   pStatement->scale = conn ? conn->queryDefaultScale : 6;
   pStatement->isEof = CFL_TRUE;
   pStatement->isReleaseOnClose = CFL_FALSE;
   pStatement->isChar1AsLogical = conn ? conn->isChar1AsLogical : CFL_FALSE;
   pStatement->isLogicalParamAsChar1 = conn ? conn->isLogicalParamAsChar1 : CFL_FALSE;
   pStatement->fetchCount = 0;
   pStatement->execCount = 0;
   pStatement->numCols = 0;
   pStatement->bufferFetchSize = SDB_STMT_FETCH_SIZE;
   pStatement->fetchSize = SDB_STMT_FETCH_SIZE;
   pStatement->isCursor = CFL_FALSE;
   pStatement->paramList = sdb_param_listNew();
   time(&pStatement->lastUseTime);
   return pStatement;
}

void sdb_stmt_free(SDB_STATEMENTP pStatement) {
   if (pStatement) {
      SDB_LOG_DEBUG(("sdb_stmt_free: stmt=%p handle=%p", pStatement, pStatement->handle));
      sdb_param_listFree(pStatement->paramList);
      if (pStatement->handle != NULL) {
         pStatement->connection->dbAPI->closeStatement(pStatement->handle);
      }
      SDB_MEM_FREE(pStatement);
   }
}

CFL_BOOL sdb_stmt_prepareBufferLen(SDB_STATEMENTP pStmt, const char *sql, CFL_UINT32 len) {
   SDB_LOG_DEBUG(("sdb_stmt_prepareBufferLen: stmt=%p sql=%s", pStmt, sql));
   if (pStmt->handle != NULL) {
      pStmt->connection->dbAPI->closeStatement(pStmt->handle);
   }
   pStmt->fetchCount = 0;
   pStmt->execCount = 0;
   pStmt->handle = pStmt->connection->dbAPI->prepareStatement(pStmt->connection, sql, len);
   if (pStmt->handle == NULL) {
      pStmt->type = SDB_STMT_UNKNOWN;
      pStmt->isEof = CFL_TRUE;
      return CFL_FALSE;
   }
   SDB_LOG_DEBUG(("sdb_stmt_prepareBufferLen: stmt=%p handle=%p", pStmt, pStmt->handle));
   pStmt->type = pStmt->connection->dbAPI->statementType(pStmt->handle);
   pStmt->isEof = CFL_FALSE;
   if (pStmt->type == SDB_STMT_QUERY) {
      pStmt->bufferFetchSize = sdb_api_nextBufferFetchSize(NULL);
      pStmt->fetchSize = pStmt->bufferFetchSize;
   }
   return CFL_TRUE;
}

CFL_BOOL sdb_stmt_prepare(SDB_STATEMENTP pStmt, CFL_STRP sql) {
   return sdb_stmt_prepareBufferLen(pStmt, cfl_str_getPtr(sql), cfl_str_getLength(sql));
}

//SDB_PARAMLISTP sdb_stmt_getCreateParams(SDB_STATEMENTP pStmt) {
//   if (pStmt->paramList == NULL) {
//      pStmt->paramList = sdb_param_listNew();
//   }
//   return pStmt->paramList;
//}

SDB_STATEMENTP sdb_stmt_param(int iParam) {
//#ifdef __HBR__
//   SDB_STATEMENTP *pStmtPtr = (SDB_STATEMENTP *) hb_parptrGC(&s_gcStmtFuncs, iParam);
//#else
//   SDB_STATEMENTP *pStmtPtr = (SDB_STATEMENTP *) hb_parptr(iParam);
//#endif
//   return pStmtPtr ? *pStmtPtr : NULL;
   return (SDB_STATEMENTP) hb_parptr(iParam);
}

SDB_STATEMENTP sdb_stmt_itemGet(PHB_ITEM pItem) {
//#ifdef __HBR__
//   SDB_STATEMENTP * pStmtPtr = (SDB_STATEMENTP *) hb_itemGetPtrGC(pItem, &s_gcStmtFuncs);
//#else
//   SDB_STATEMENTP * pStmtPtr = (SDB_STATEMENTP *) hb_itemGetPtrGC(pItem, stmt_destructor);
//#endif
//   return pStmtPtr ? *pStmtPtr : NULL;
   return (SDB_STATEMENTP) hb_itemGetPtr(pItem);
}

PHB_ITEM sdb_stmt_itemPut(PHB_ITEM pItem, SDB_STATEMENTP pStmt) {
//   SDB_STATEMENTP pCurrStmt = sdb_stmt_itemGet(pItem);
//   SDB_STATEMENTP *pStmtPtr;
//
//   if (pCurrStmt != NULL && pStmt != NULL && pCurrStmt->handle == pStmt->handle) {
//      pCurrStmt->handle = NULL;
//   }
//#ifdef __HBR__
//   pStmtPtr = (SDB_STATEMENTP *) hb_gcAllocate(sizeof(SDB_STATEMENTP), &s_gcStmtFuncs);
//#else
//   pStmtPtr = (SDB_STATEMENTP *) hb_gcAlloc(sizeof(SDB_STATEMENTP), stmt_destructor);
//#endif
//   *pStmtPtr = pStmt;
//   return hb_itemPutPtrGC(pItem, pStmtPtr);
   return hb_itemPutPtr(pItem, pStmt);
}

SDB_STATEMENTP sdb_stmt_itemDetach(PHB_ITEM pItem) {
//#ifdef __HBR__
//   SDB_STATEMENTP *pStmtPtr = (SDB_STATEMENTP *) hb_itemGetPtrGC(pItem, &s_gcStmtFuncs);
//#else
//   SDB_STATEMENTP *pStmtPtr = (SDB_STATEMENTP *) hb_itemGetPtrGC(pItem, stmt_destructor);
//#endif
//   SDB_STATEMENTP pResult = NULL;
//
//   if (pStmtPtr) {
//      pResult = *pStmtPtr;
//      *pStmtPtr = NULL;
//   }
//   return pResult;
   return (SDB_STATEMENTP) hb_itemGetPtr(pItem);
}

CFL_BOOL sdb_stmt_fetchNext(SDB_STATEMENTP pStatement, CFL_BOOL autoPreFetch) {
   CFL_BOOL bFetched;

   time(&pStatement->lastUseTime);
   if (autoPreFetch && pStatement->fetchSize != pStatement->bufferFetchSize) {
      switch (pStatement->fetchCount) {
         case 1:
            if (pStatement->fetchSize < 5 && pStatement->bufferFetchSize >= 5) {
               pStatement->fetchSize = 5;
               pStatement->connection->dbAPI->setPreFetchSize(pStatement, pStatement->fetchSize);
            }
            break;
         case 5:
            if (pStatement->fetchSize < 10 && pStatement->bufferFetchSize >= 10) {
               pStatement->fetchSize = 10;
               pStatement->connection->dbAPI->setPreFetchSize(pStatement, pStatement->fetchSize);
            }
            break;
         case 10:
            if (pStatement->bufferFetchSize > 10) {
               pStatement->fetchSize = pStatement->bufferFetchSize;
               pStatement->connection->dbAPI->setPreFetchSize(pStatement, pStatement->fetchSize);
            }
            break;
      }
   }

   bFetched = pStatement->connection->dbAPI->fetchNext(pStatement);
   SDB_LOG_DEBUG(("sdb_stmt_fetchNext. stmt=%p handle=%p fetched=%s", pStatement, pStatement->handle, BOOL_STR(bFetched)));
   return bFetched;
}

CFL_BOOL sdb_stmt_setPreFetchSize(SDB_STATEMENTP pStatement, CFL_UINT16 fetchSize) {
   if (pStatement->fetchSize == fetchSize) {
      SDB_LOG_DEBUG(("sdb_stmt_setPreFetchSize. NOT CHANGED. stmt=%p handle=%p old=%d new=%d", pStatement, pStatement->handle, pStatement->fetchSize, fetchSize));
      return CFL_TRUE;
   }
   if (pStatement->connection->dbAPI->setPreFetchSize(pStatement, fetchSize)) {
      SDB_LOG_DEBUG(("sdb_stmt_setPreFetchSize. SUCCESS. stmt=%p handle=%p old=%d new=%d", pStatement, pStatement->handle, pStatement->fetchSize, fetchSize));
      pStatement->fetchSize = fetchSize;
      pStatement->bufferFetchSize = fetchSize;
      return CFL_TRUE;
   }
   SDB_LOG_ERROR(("sdb_stmt_setPreFetchSize. FAILED. stmt=%p handle=%p", pStatement, pStatement->handle));
   return CFL_FALSE;
}

PHB_ITEM sdb_stmt_getQueryValue(SDB_STATEMENTP pStatement, CFL_UINT32 pos, PHB_ITEM pItem) {
   if (pItem == NULL) {
      pItem = hb_itemNew(NULL);
   }
   pStatement->connection->dbAPI->getQueryValue(pStatement, pos, pItem);
   return pItem;
}

CFL_BOOL sdb_stmt_execute(SDB_STATEMENTP pStatement, CFL_UINT64 *pulAffectedRows) {
   CFL_BOOL bSuccess;
   SDB_LOG_DEBUG(("sdb_stmt_execute(stmt=%p, handle=%p)", pStatement, pStatement->handle));
   if (sdb_log_isLevelActive(SDB_LOG_LEVEL_DEBUG)) {
      time_t clockStart;
      time_t clockEnd;
      time(&clockStart);
      bSuccess = pStatement->connection->dbAPI->executeStatement(pStatement, pulAffectedRows);
      time(&clockEnd);
      SDB_LOG_DEBUG(("sdb_stmt_execute: %p executed with %s in %f sec.", pStatement, (bSuccess?"success":"fail"), difftime(clockEnd, clockStart)));
   } else {
      bSuccess = pStatement->connection->dbAPI->executeStatement(pStatement, pulAffectedRows);
   }
   time(&pStatement->lastUseTime);
   return bSuccess;
}

CFL_BOOL sdb_stmt_executeMany(SDB_STATEMENTP pStatement, CFL_UINT64 *pulAffectedRows) {
   CFL_BOOL bSuccess;
   if (sdb_log_isLevelActive(SDB_LOG_LEVEL_DEBUG)) {
      time_t clockStart;
      time_t clockEnd;
      time(&clockStart);
      bSuccess = pStatement->connection->dbAPI->executeStatementMany(pStatement, pulAffectedRows);
      time(&clockEnd);
      SDB_LOG_DEBUG(("sdb_stmt_executeMany(%p) in %f sec.", pStatement, difftime(clockEnd, clockStart)));
   } else {
      bSuccess = pStatement->connection->dbAPI->executeStatementMany(pStatement, pulAffectedRows);
   }
   return bSuccess;
}

CFL_BOOL sdb_stmt_getQueryInfo(SDB_STATEMENTP pStatement, CFL_UINT32 pos, SDB_QUERY_COL_INFOP info) {
   time(&pStatement->lastUseTime);
   return pStatement->connection->dbAPI->getQueryInfo(pStatement, pos, info);
}

static SDB_PARAMP findAddParamByName(SDB_PARAMLISTP list, const char *name) {
   SDB_PARAMP param;
   param = sdb_param_listGet(list, name);
   if (param == NULL) {
      param = sdb_param_listAdd(list, name, (CFL_UINT32) strlen(name), SDB_CLP_UNKNOWN, 0, CFL_FALSE, CFL_FALSE, CFL_TRUE);
   }
   return param;
}

static SDB_PARAMP findAddParamByPos(SDB_PARAMLISTP list, CFL_UINT32 pos) {
   SDB_PARAMP param = sdb_param_listGetPos(list, pos);
   if (param == NULL && pos > 0) {
      while (sdb_param_listLength(list) < pos) {
         param = sdb_param_listAdd(list, NULL, 0, SDB_CLP_UNKNOWN, 0, CFL_FALSE, CFL_FALSE, CFL_TRUE);
      }
   }
   return param;
}

static CFL_DATEP itemGetDate(PHB_ITEM pItem, CFL_DATEP date) {
   int iYear, iMonth, iDay, iHour, iMin, iSec, iMSec;
   #ifdef __HBR__
      hb_timeStampUnpack(hb_itemGetTD(pItem), &iYear, &iMonth, &iDay, &iHour, &iMin, &iSec, &iMSec);
   #else
      hb_dateDecode(hb_itemGetDL(pItem), &iYear, &iMonth, &iDay);
      iHour = iMin = iSec = iMSec = 0;
   #endif
   cfl_date_setDateTime(date,
                        (CFL_UINT16) iYear,
                        (CFL_UINT8) iMonth,
                        (CFL_UINT8) iDay,
                        (CFL_UINT8) iHour,
                        (CFL_UINT8) iMin,
                        (CFL_UINT8) iSec,
                        (CFL_UINT8) iMSec);
   return date;
}

static void itemPutDate(PHB_ITEM pItem, CFL_DATEP date) {
   #ifdef __HBR__
       hb_itemPutTDT(pItem,
                     hb_dateEncode(cfl_date_getYear(date), cfl_date_getMonth(date), cfl_date_getDay(date)),
                     hb_timeEncode(cfl_date_getHour(date), cfl_date_getMin(date), cfl_date_getSec(date), cfl_date_getMillis(date)));
   #else
       hb_itemPutD(pItem, cfl_date_getYear(date), cfl_date_getMonth(date), cfl_date_getDay(date));
   #endif
}

SDB_PARAMP sdb_stmt_setString(SDB_STATEMENTP pStatement, const char *name, CFL_STRP str, CFL_BOOL out) {
   SDB_PARAMP param = findAddParamByName(sdb_stmt_getParams(pStatement), name);
   if (param != NULL) {
      sdb_param_setType(param, SDB_CLP_CHARACTER);
      sdb_param_setOut(param, out);
      if (str) {
         hb_itemPutCL(sdb_param_getItem(param), cfl_str_getPtr(str), cfl_str_getLength(str));
      } else {
         sdb_param_setValue(param, NULL);
      }
   }
   return param;
}

SDB_PARAMP sdb_stmt_setStringByPos(SDB_STATEMENTP pStatement, CFL_UINT32 pos, CFL_STRP str, CFL_BOOL out) {
   SDB_PARAMP param = findAddParamByPos(sdb_stmt_getParams(pStatement), pos);
   if (param != NULL) {
      sdb_param_setType(param, SDB_CLP_CHARACTER);
      sdb_param_setOut(param, out);
      if (str) {
         hb_itemPutCL(sdb_param_getItem(param), cfl_str_getPtr(str), cfl_str_getLength(str));
      } else {
         sdb_param_setValue(param, NULL);
      }
   }
   return param;
}

SDB_PARAMP sdb_stmt_setCharLen(SDB_STATEMENTP pStatement, const char *name, const char *str, CFL_UINT32 strLen, CFL_BOOL out) {
   SDB_PARAMP param = findAddParamByName(sdb_stmt_getParams(pStatement), name);
   if (param != NULL) {
      sdb_param_setType(param, SDB_CLP_CHARACTER);
      sdb_param_setOut(param, out);
      if (str) {
         hb_itemPutCL(sdb_param_getItem(param), str, strLen);
      } else {
         sdb_param_setValue(param, NULL);
      }
   }
   return param;
}

SDB_PARAMP sdb_stmt_setCharLenByPos(SDB_STATEMENTP pStatement, CFL_UINT32 pos, const char *str, CFL_UINT32 strLen, CFL_BOOL out) {
   SDB_PARAMP param = findAddParamByPos(sdb_stmt_getParams(pStatement), pos);
   if (param != NULL) {
      sdb_param_setType(param, SDB_CLP_CHARACTER);
      sdb_param_setOut(param, out);
      if (str) {
         hb_itemPutCL(sdb_param_getItem(param), str, strLen);
      } else {
         sdb_param_setValue(param, NULL);
      }
   }
   return param;
}

SDB_PARAMP sdb_stmt_setChar(SDB_STATEMENTP pStatement, const char *name, const char *str, CFL_BOOL out) {
   SDB_PARAMP param;
   SDB_LOG_TRACE(("sdb_stmt_setChar(%p, %s, %s, %s)", pStatement, name, str, BOOL_STR(out)));
   param = findAddParamByName(sdb_stmt_getParams(pStatement), name);
   if (param != NULL) {
      sdb_param_setType(param, SDB_CLP_CHARACTER);
      sdb_param_setOut(param, out);
      if (str) {
         hb_itemPutC(sdb_param_getItem(param), str);
      } else {
         sdb_param_setValue(param, NULL);
      }
   }
   return param;
}

SDB_PARAMP sdb_stmt_setCharByPos(SDB_STATEMENTP pStatement, CFL_UINT32 pos, const char *str, CFL_BOOL out) {
   SDB_PARAMP param = findAddParamByPos(sdb_stmt_getParams(pStatement), pos);
   if (param != NULL) {
      sdb_param_setType(param, SDB_CLP_CHARACTER);
      sdb_param_setOut(param, out);
      if (str) {
         hb_itemPutC(sdb_param_getItem(param), str);
      } else {
         sdb_param_setValue(param, NULL);
      }
   }
   return param;
}

SDB_PARAMP sdb_stmt_setCharNull(SDB_STATEMENTP pStatement, const char *name, CFL_BOOL out) {
   SDB_PARAMP param = findAddParamByName(sdb_stmt_getParams(pStatement), name);
   if (param != NULL) {
      sdb_param_setType(param, SDB_CLP_CHARACTER);
      sdb_param_setOut(param, out);
      sdb_param_setValue(param, NULL);
   }
   return param;
}

SDB_PARAMP sdb_stmt_setCharNullByPos(SDB_STATEMENTP pStatement, CFL_UINT32 pos, CFL_BOOL out) {
   SDB_PARAMP param = findAddParamByPos(sdb_stmt_getParams(pStatement), pos);
   if (param != NULL) {
      sdb_param_setType(param, SDB_CLP_CHARACTER);
      sdb_param_setOut(param, out);
      sdb_param_setValue(param, NULL);
   }
   return param;
}

SDB_PARAMP sdb_stmt_setValue(SDB_STATEMENTP pStatement, const char *name, PHB_ITEM pItem, CFL_BOOL bOut) {
   SDB_PARAMP param = findAddParamByName(sdb_stmt_getParams(pStatement), name);
   if (param != NULL) {
      sdb_param_setOut(param, bOut);
      sdb_param_setValue(param, pItem);
   }
   return param;
}

SDB_PARAMP sdb_stmt_setValueByPos(SDB_STATEMENTP pStatement, CFL_UINT32 pos, PHB_ITEM pItem, CFL_BOOL bOut) {
   SDB_PARAMP param = findAddParamByPos(sdb_stmt_getParams(pStatement), pos);
   if (param != NULL) {
      sdb_param_setOut(param, bOut);
      sdb_param_setValue(param, pItem);
   }
   return param;
}

PHB_ITEM sdb_stmt_getItem(SDB_STATEMENTP pStatement, const char *name) {
   SDB_PARAMP param = sdb_param_listGet(sdb_stmt_getParams(pStatement), name);
   if (param) {
      return sdb_param_getItem(param);
   }
   return NULL;
}

PHB_ITEM sdb_stmt_getItemByPos(SDB_STATEMENTP pStatement, CFL_UINT32 pos) {
   SDB_PARAMP param = sdb_param_listGetPos(sdb_stmt_getParams(pStatement), pos);
   if (param) {
      return sdb_param_getItem(param);
   }
   return NULL;
}

FUNCTION_GET1_SET1(Boolean, CFL_BOOL, SDB_CLP_LOGICAL, hb_itemGetL, hb_itemPutL, 0)
FUNCTION_GET2_SET2(Date, CFL_DATEP, SDB_CLP_DATE, itemGetDate, itemPutDate, NULL)
FUNCTION_GET1_SET1(Stmt, SDB_STATEMENTP, SDB_CLP_CURSOR, sdb_stmt_itemGet, sdb_stmt_itemPut, NULL)
FUNCTION_GET1_SET1(CLob, SDB_LOBP, SDB_CLP_CLOB, sdb_lob_itemGet, sdb_lob_itemPut, NULL)
FUNCTION_GET1_SET1(BLob, SDB_LOBP, SDB_CLP_BLOB, sdb_lob_itemGet, sdb_lob_itemPut, NULL)
FUNCTION_GET1_SET1(Int8, CFL_INT8, SDB_CLP_NUMERIC, hb_itemGetNI, hb_itemPutNI, 0)
FUNCTION_GET1_SET1(UInt8, CFL_UINT8, SDB_CLP_NUMERIC, hb_itemGetNI, hb_itemPutNI, 0)
FUNCTION_GET1_SET1(Int16, CFL_INT16, SDB_CLP_NUMERIC, hb_itemGetNI, hb_itemPutNI, 0)
FUNCTION_GET1_SET1(UInt16, CFL_UINT16, SDB_CLP_NUMERIC, hb_itemGetNL, hb_itemPutNL, 0)
FUNCTION_GET1_SET1(Int32, CFL_INT32, SDB_CLP_NUMERIC, hb_itemGetNL, hb_itemPutNL, 0)
FUNCTION_GET1_SET1(UInt32, CFL_UINT32, SDB_CLP_NUMERIC, hb_itemGetNLL, hb_itemPutNLL, 0)
FUNCTION_GET1_SET1(Int64, CFL_INT64, SDB_CLP_NUMERIC, hb_itemGetNLL, hb_itemPutNLL, 0)
FUNCTION_GET1_SET1(UInt64, CFL_UINT64, SDB_CLP_NUMERIC, hb_itemGetNLL, hb_itemPutNLL, 0)
FUNCTION_GET1_SET1(Float, CFL_FLOAT, SDB_CLP_NUMERIC, hb_itemGetND, hb_itemPutND, 0.0)
FUNCTION_GET1_SET1(Double, CFL_DOUBLE, SDB_CLP_NUMERIC, hb_itemGetND, hb_itemPutND, 0.0)
