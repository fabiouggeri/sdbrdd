#include <stdio.h>
#include <string.h>
#include <time.h>

#ifdef __XHB__
#include "hashapi.h"
#include "hbfast.h"

#else
#include "hbvmint.h"
#endif
#include "dbinfo.ch"
#include "error.ch"
#include "hbapierr.h"
#include "hbapiitm.h"
#include "hbapilng.h"
#include "hbapirdd.h"
#include "hbdate.h"
#include "hbdbferr.h"
#include "hbinit.h"
#include "hbset.h"
#include "hbvm.h"
#include "rddsys.ch"

#include "cfl_list.h"
#include "cfl_str.h"
#include "sdb_api.h"
#include "sdb_area.h"
#include "sdb_connection.h"
#include "sdb_database.h"
#include "sdb_error.h"
#include "sdb_expr.h"
#include "sdb_field.h"
#include "sdb_index.h"
#include "sdb_log.h"
#include "sdb_param.h"
#include "sdb_record.h"
#include "sdb_schema.h"
#include "sdb_statement.h"
#include "sdb_table.h"
#include "sdb_thread.h"
#include "sdb_util.h"

#define __PRG_SOURCE__ __FILE__
#ifdef HB_PCODE_VER
#undef HB_PRG_PCODE_VER
#define HB_PRG_PCODE_VER HB_PCODE_VER
#endif

#define SDB_FILE_EXT ".dbf"

#define SUPERTABLE (&sdbSuper)

/* Esta macro nao existe no header hbapirdd.h */
#ifdef __XHB__
#define SUPER_ORDDESTROY(w, p) ((*(SUPERTABLE)->orderDestroy)(w, p))
#define hb_dynsymSetAreaHandle(x, y) (x)->hArea = y
#endif

HB_FUNC(SDBRDD);
HB_FUNC(SDBRDD_GETFUNCTABLE);

static char s_rddNameFunctionName[HB_SYMBOL_NAME_LEN + 1];

static char s_funcTabFunctionName[HB_SYMBOL_NAME_LEN + 1];

static HB_SYMB s_symRddName = {_SDB_RDDNAME_, HB_FS_PUBLIC, {HB_FUNCNAME(SDBRDD)}, NULL};

static HB_SYMB s_symFuncTab = {_SDB_RDDNAME_ "_GETFUNCTABLE", HB_FS_PUBLIC, {HB_FUNCNAME(SDBRDD_GETFUNCTABLE)}, NULL};

static RDDFUNCS sdbSuper;

static RDDFUNCS *s_pSDBTable = NULL;

/******************************************************************************
                             Auxiliar functions
 *******************************************************************************/
static PHB_ITEM sdb_rdd_evalIndexExpr(PHB_ITEM pItem, SDB_INDEXP index) {
   SDB_LOG_TRACE(("sdb_rdd_evalIndexExpr: field=%s, expr=%s", cfl_str_getPtr(index->field->clpName),
                  cfl_str_getPtr(index->field->clpExpression)));
   if (pItem == NULL) {
      pItem = hb_itemNew(NULL);
   }
   hb_itemMove(pItem, hb_vmEvalBlockOrMacro(index->compiledExpr));
   return pItem;
}

static void sdb_rdd_blankRecord(SDB_AREAP pSDBArea) {
   CFL_UINT32 ulIndex;
   SDB_RECORDP record = pSDBArea->record;

   ENTER_FUN_NAME("sdb_rdd_blankRecord");
   if (record == NULL) {
      RETURN;
   }

   for (ulIndex = 0; ulIndex < pSDBArea->fieldsCount; ulIndex++) {
      SDB_WAFIELDP waField = sdb_area_getFieldByPos(pSDBArea, ulIndex);
      if (!waField->isContext) {
         sdb_record_setValue(record, waField->field, NULL);
      }
      waField->isChanged = CFL_FALSE;
   }
   record->isLocked = CFL_FALSE;
   record->isChanged = CFL_FALSE;
   record->ulRowNum = 0;
   /* Queries doesn't have PK Field */
   if (pSDBArea->table->pkField) {
      /* set RECNO to max value */
      sdb_record_setInt64(record, pSDBArea->table->pkField, sdb_api_tableMaxRecno(pSDBArea) + 1);
   }
   hb_itemRelease(pSDBArea->keyValue);
   pSDBArea->keyValue = NULL;
   pSDBArea->isValidBuffer = CFL_TRUE;
   RETURN;
}

static HB_ERRCODE field2FieldInfo(SDB_FIELDP field, LPDBFIELDINFO dbFieldInfo) {
   HB_ERRCODE errCode = HB_SUCCESS;

   dbFieldInfo->atomName = cfl_str_getPtr(field->clpName);
   dbFieldInfo->uiTypeExtended = 0;
   switch (field->clpType) {
   case SDB_CLP_CHARACTER:
      dbFieldInfo->uiType = HB_FT_STRING;
      dbFieldInfo->uiLen = (HB_USHORT)field->length;
      dbFieldInfo->uiDec = 0;
      break;

   case SDB_CLP_LOGICAL:
      dbFieldInfo->uiType = HB_FT_LOGICAL;
      dbFieldInfo->uiLen = 1;
      dbFieldInfo->uiDec = 0;
      break;

   case SDB_CLP_DATE:
      dbFieldInfo->uiType = HB_FT_DATE;
      dbFieldInfo->uiLen = sizeof(long);
      dbFieldInfo->uiDec = 0;
      break;

   case SDB_CLP_NUMERIC:
      dbFieldInfo->uiType = HB_FT_LONG;
      dbFieldInfo->uiLen = (HB_USHORT)field->length;
      dbFieldInfo->uiDec = field->decimals;
      break;

   case SDB_CLP_INTEGER:
      dbFieldInfo->uiType = HB_FT_INTEGER;
      dbFieldInfo->uiLen = sizeof(CFL_INT32);
      dbFieldInfo->uiDec = 0;
      break;

   case SDB_CLP_BIGINT:
      dbFieldInfo->uiType = HB_FT_LONG;
      dbFieldInfo->uiLen = sizeof(CFL_INT64);
      dbFieldInfo->uiDec = 0;
      break;

   case SDB_CLP_DOUBLE:
      dbFieldInfo->uiType = HB_FT_DOUBLE;
      dbFieldInfo->uiLen = sizeof(double);
      if (field->decimals == 0) {
         dbFieldInfo->uiDec = 6;
      }
      break;

   case SDB_CLP_MEMO_LONG:
   case SDB_CLP_CLOB:
   case SDB_CLP_BLOB:
      dbFieldInfo->uiType = HB_FT_MEMO;
      dbFieldInfo->uiLen = sizeof(PHB_ITEM);
      break;
#ifndef __XHB__
   case SDB_CLP_ROWID:
      dbFieldInfo->uiType = HB_FT_ANY;
      dbFieldInfo->uiLen = sizeof(PHB_ITEM);
      dbFieldInfo->uiDec = 0;
      break;

   case SDB_CLP_FLOAT:
      dbFieldInfo->uiType = HB_FT_FLOAT;
      dbFieldInfo->uiLen = sizeof(double);
      if (field->decimals == 0) {
         dbFieldInfo->uiDec = 6;
      }
      break;

   case SDB_CLP_TIMESTAMP:
      dbFieldInfo->uiType = HB_FT_TIMESTAMP;
      dbFieldInfo->uiLen = sizeof(double);
      break;

   case SDB_CLP_IMAGE:
      dbFieldInfo->uiType = HB_FT_IMAGE;
      dbFieldInfo->uiLen = sizeof(PHB_ITEM);
#ifdef __HBR__
      dbFieldInfo->uiFlags |= HB_FF_BINARY;
#endif
      break;
#endif
   default:
      errCode = HB_FAILURE;
      break;
   }
   return errCode;
}

static CFL_BOOL errorConnection(SDB_CONNECTIONP conn, const char *apiFunName) {
   if (conn != NULL) {
      return CFL_FALSE;
   }
   sdb_api_genError(NULL, SDB_ERROR_TYPE_SDB, SDB_ERROR_NOT_CONNECTED, NULL, EF_NONE, "Not connected to database", apiFunName,
                    NULL);
   return CFL_TRUE;
}

static CFL_BOOL sdb_rdd_readField(SDB_AREAP pSDBArea, SDB_WAFIELDP waField, CFL_BOOL bReadLarge) {
   if (waField->queryPos > 0) {
      if (!sdb_api_areaGetValue(pSDBArea, waField->queryPos, waField->field, bReadLarge)) {
         sdb_api_genErrorFromSDBError(pSDBArea->connection, sdb_area_getAlias(pSDBArea), EF_NONE, sdb_thread_getLastError(),
                                      cfl_str_getPtr(waField->field->clpName), NULL);
         return CFL_FALSE;
      }
   } else {
      sdb_record_setValue(pSDBArea->record, waField->field, NULL);
   }
   return CFL_TRUE;
}

static CFL_BOOL sdb_rdd_readRecord(SDB_AREAP pSDBArea, CFL_BOOL bLocking) {
   CFL_UINT32 ulIndex;

   SDB_LOG_DEBUG(("sdb_rdd_readRecord: %s", cfl_str_getPtr(pSDBArea->table->clpName)));
   if (pSDBArea->record == NULL) {
      return CFL_FALSE;
   } else if (!pSDBArea->isValidBuffer && !pSDBArea->area.fEof) {
      pSDBArea->record->isLocked = bLocking;
      for (ulIndex = 0; ulIndex < pSDBArea->fieldsCount; ulIndex++) {
         SDB_WAFIELDP waField = sdb_area_getFieldByPos(pSDBArea, ulIndex);
         waField->isChanged = CFL_FALSE;
         if (!waField->isContext && !sdb_rdd_readField(pSDBArea, waField, CFL_FALSE)) {
            return CFL_FALSE;
         }
      }
      hb_itemRelease(pSDBArea->keyValue);
      pSDBArea->keyValue = NULL;
      pSDBArea->record->isChanged = CFL_FALSE;
      pSDBArea->isValidBuffer = CFL_TRUE;
   }
   return CFL_TRUE;
}

static CFL_BOOL sdb_rdd_fetchNextRecord(SDB_AREAP pSDBArea, CFL_UINT8 uiSort) {
   CFL_BOOL bFetched;
   CFL_BOOL bError;

   SDB_LOG_DEBUG(
       ("sdb_rdd_fetchNextRecord: stmt=%p cursor eof=%s", pSDBArea->pStatement, (pSDBArea->pStatement->isEof ? "true" : "false")));
   if ((pSDBArea->area.fEof && uiSort != SDB_SORT_DESC) || (pSDBArea->area.fBof && uiSort == SDB_SORT_DESC)) {
      bFetched = CFL_FALSE;
      pSDBArea->isValidBuffer = CFL_TRUE;
   } else if (sdb_api_areaFetchNext(pSDBArea, &bError)) {
      ++pSDBArea->record->ulRowNum;
      pSDBArea->area.fBof = HB_FALSE;
      pSDBArea->area.fEof = HB_FALSE;
      pSDBArea->isValidBuffer = CFL_FALSE;
      bFetched = CFL_TRUE;
   } else {
      pSDBArea->area.fBof = uiSort == SDB_SORT_DESC;
      pSDBArea->area.fEof = uiSort != SDB_SORT_DESC;
      pSDBArea->isValidBuffer = CFL_TRUE;
      bFetched = CFL_FALSE;
      if (uiSort != SDB_SORT_DESC) {
         sdb_rdd_blankRecord(pSDBArea);
      }
      if (bError) {
         sdb_api_genErrorFromSDBError(pSDBArea->connection, sdb_area_getAlias(pSDBArea), EF_NONE, sdb_thread_getLastError(), NULL,
                                      NULL);
      }
   }
   pSDBArea->area.fFound = HB_FALSE;
   return bFetched;
}

static SDB_INDEXP sdb_rdd_getIndexByName(CFL_LISTP indexes, const char *indexName) {
   CFL_UINT32 ulLen = cfl_list_length(indexes);
   CFL_UINT32 ulIndex;
   for (ulIndex = 0; ulIndex < ulLen; ++ulIndex) {
      SDB_INDEXP index = (SDB_INDEXP)cfl_list_get(indexes, ulIndex);
      if (cfl_str_bufferEqualsIgnoreCase(index->field->clpIndexName, indexName)) {
         return index;
      }
   }
   return NULL;
}

static HB_INT32 sdb_rdd_getIndexPosByName(CFL_LISTP indexes, const char *indexName) {
   CFL_UINT32 ulLen = cfl_list_length(indexes);
   CFL_UINT32 ulIndex;
   for (ulIndex = 0; ulIndex < ulLen; ++ulIndex) {
      SDB_INDEXP index = (SDB_INDEXP)cfl_list_get(indexes, ulIndex);
      if (cfl_str_bufferEqualsIgnoreCase(index->field->clpIndexName, indexName)) {
         return ulIndex;
      }
   }
   return -1;
}

static HB_ERRCODE sdb_rdd_flushRecord(SDB_AREAP pSDBArea, CFL_BOOL bForce) {
   ENTER_FUN_NAME("sdb_rdd_flushRecord");
   if (!pSDBArea->isQuery) {
      pSDBArea->isFlushImmediate = bForce || pSDBArea->connection->transaction == NULL;
      RETURN SELF_GOCOLD((AREAP)pSDBArea);
   }
   RETURN HB_SUCCESS;
}

static void rdd_resetAreaFlags(SDB_AREAP pSDBArea) {
   pSDBArea->area.fFound = HB_FALSE;
   pSDBArea->area.fBof = HB_FALSE;
   pSDBArea->area.fEof = HB_FALSE;
   pSDBArea->isValidBuffer = CFL_FALSE;
}

static HB_ERRCODE sdb_rdd_reloadCurrentRecord(SDB_AREAP pSDBArea, CFL_BOOL bLock) {
   HB_ERRCODE errCode = HB_SUCCESS;
   CFL_BOOL bError = CFL_FALSE;
   PHB_ITEM pRecno;
   CFL_BOOL bReload;
   PHB_ITEM pRowId;
   SDB_STATEMENTP pCurrStmt;
   SDB_COMMANDP pCurrCmd;

   SDB_LOG_DEBUG(("sdb_rdd_reloadCurrentRecord: lock=%s", (bLock ? "true" : "false")));

   if (pSDBArea->area.fEof) {
      return HB_SUCCESS;
      /* Don't read all fields if invalid buffer, because we will reload record */
   } else if (!pSDBArea->isValidBuffer) {
      if (!sdb_rdd_readField(pSDBArea, pSDBArea->waPKField, CFL_FALSE)) {
         return HB_FAILURE;
      }
      if (pSDBArea->waRowIdField != NULL && !sdb_rdd_readField(pSDBArea, pSDBArea->waRowIdField, CFL_FALSE)) {
         return HB_FAILURE;
      }
   }

   pRecno = hb_itemNew(NULL);
   sdb_record_getValue(pSDBArea->record, pSDBArea->table->pkField, pRecno, CFL_TRUE);
   if (pSDBArea->table->rowIdField) {
      pRowId = hb_itemNew(NULL);
      sdb_record_getValue(pSDBArea->record, pSDBArea->table->rowIdField, pRowId, CFL_TRUE);
   } else {
      pRowId = NULL;
   }
   if (pSDBArea->keyValue != NULL) {
      hb_itemRelease(pSDBArea->keyValue);
      pSDBArea->keyValue = NULL;
   }
   pCurrCmd = pSDBArea->pCurrentCommand;
   pCurrStmt = pSDBArea->pStatement;
   if (bLock) {
      bReload = sdb_api_tableLockRecord(pSDBArea, pRecno, pRowId);
   } else {
      bReload = sdb_api_tableGoTo(pSDBArea, pRecno);
   }
   pSDBArea->isValidBuffer = CFL_FALSE;
   if (!bReload || !sdb_api_areaFetchNext(pSDBArea, &bError) || !sdb_rdd_readRecord(pSDBArea, bLock)) {
      if (sdb_thread_hasError() || bError) {
         sdb_api_genErrorFromSDBError(pSDBArea->connection, sdb_area_getAlias(pSDBArea), EF_NONE, sdb_thread_getLastError(), NULL,
                                      NULL);
      }
      errCode = HB_FAILURE;
   }
   hb_itemRelease(pRecno);
   hb_itemRelease(pRowId);
   pSDBArea->pStatement = pCurrStmt;
   pSDBArea->pCurrentCommand = pCurrCmd;
   return errCode;
}

static HB_ERRCODE sdb_rdd_rawLock(SDB_AREAP pSDBArea, HB_USHORT uiAction, SDB_RECNO ulRecno) {
   HB_ERRCODE errCode = HB_SUCCESS;

   ENTER_FUN_NAME("sdb_rdd_rawLock");
   SDB_LOG_DEBUG(("sdb_rdd_rawLock: action=%hu, recno=%lld", uiAction, ulRecno));
   sdb_thread_cleanError();
   if (pSDBArea->isQuery) {
      sdb_api_genError(pSDBArea->connection, EG_READONLY, SDB_ERROR_READONLY, sdb_area_getAlias(pSDBArea), EF_NONE, NULL, NULL,
                       NULL);
      RETURN HB_FAILURE;
   }

   if (pSDBArea->isShared) {
      switch (uiAction) {
      case FILE_LOCK:
         if (!pSDBArea->isLocked) {
            if (sdb_api_lockAreaTable(pSDBArea, SDB_LOCK_LOCK_ALL)) {
               pSDBArea->isLocked = CFL_TRUE;
            } else {
               if (sdb_thread_hasError()) {
                  sdb_api_genErrorFromSDBError(pSDBArea->connection, sdb_area_getAlias(pSDBArea), EF_NONE,
                                               sdb_thread_getLastError(), NULL, NULL);
               }
               errCode = HB_FAILURE;
            }
         }
         break;

      case FILE_UNLOCK:
         if (pSDBArea->isLocked) {
            if (!sdb_api_lockAreaTable(pSDBArea, SDB_LOCK_SHARED) && sdb_thread_hasError()) {
               sdb_api_genErrorFromSDBError(pSDBArea->connection, sdb_area_getAlias(pSDBArea), EF_NONE, sdb_thread_getLastError(),
                                            NULL, NULL);
            }
            pSDBArea->isLocked = CFL_FALSE;
         }
         break;

      case REC_LOCK:
         if (!pSDBArea->isValidBuffer && !sdb_rdd_readField(pSDBArea, pSDBArea->waPKField, CFL_FALSE)) {
            errCode = HB_FAILURE;
            break;
         }
         if ((SDB_RECNO)sdb_record_getInt64(pSDBArea->record, pSDBArea->table->pkField) == ulRecno) {
            if (!pSDBArea->record->isLocked) {
               errCode = sdb_rdd_reloadCurrentRecord(pSDBArea, CFL_TRUE);
            }
         } else {
            PHB_ITEM pRecno = hb_itemNew(NULL);
            sdb_util_itemPutRecno(pRecno, ulRecno);
            if (!sdb_api_tableLockRecord(pSDBArea, pRecno, NULL)) {
               if (sdb_thread_hasError()) {
                  sdb_api_genErrorFromSDBError(pSDBArea->connection, sdb_area_getAlias(pSDBArea), EF_NONE,
                                               sdb_thread_getLastError(), NULL, NULL);
               }
               errCode = HB_FAILURE;
            }
            hb_itemRelease(pRecno);
         }
         break;

      case REC_UNLOCK: {
         PHB_ITEM pRecno = sdb_util_itemPutRecno(NULL, ulRecno);
         PHB_ITEM pRowId = NULL;
         if (pSDBArea->table->rowIdField) {
            pRowId = hb_itemNew(NULL);
            sdb_record_getValue(pSDBArea->record, pSDBArea->table->rowIdField, pRowId, CFL_TRUE);
         }
         if (!sdb_api_tableUnlockRecord(pSDBArea, pRecno, pRowId)) {
            if (sdb_thread_hasError()) {
               sdb_api_genErrorFromSDBError(pSDBArea->connection, sdb_area_getAlias(pSDBArea), EF_NONE, sdb_thread_getLastError(),
                                            NULL, NULL);
            }
            errCode = HB_FAILURE;
         }
         hb_itemRelease(pRowId);
         hb_itemRelease(pRecno);
      } break;
      }
   }
   RETURN errCode;
}

static HB_ERRCODE sdb_rdd_unlockAllRecords(SDB_AREAP pSDBArea) {
   SDB_RECORDLOCKEDP lock;
   SDB_RECORDLOCKEDP prevLock;
   HB_ERRCODE errCode = HB_SUCCESS;

   ENTER_FUN_NAME("sdb_rdd_unlockAllRecords");
   lock = pSDBArea->recordLocked;
   if (lock) {
      errCode = sdb_rdd_flushRecord(pSDBArea, CFL_FALSE);
      while (lock != NULL) {
         prevLock = lock->previous;
         sdb_rdd_rawLock(pSDBArea, REC_UNLOCK, lock->ulRecno);
         SDB_MEM_FREE(lock);
         lock = prevLock;
      }
      pSDBArea->recordLocked = NULL;
   }
   RETURN errCode;
}

static CFL_BOOL sdb_rdd_isRecordLocked(SDB_AREAP pSDBArea, SDB_RECNO ulRecno) {
   SDB_RECORDLOCKEDP lock;

   ENTER_FUN_NAME("sdb_rdd_isRecordLocked");
   lock = pSDBArea->recordLocked;
   while (lock != NULL) {
      if (lock->ulRecno == ulRecno) {
         RETURN CFL_TRUE;
      }
      lock = lock->previous;
   }
   RETURN CFL_FALSE;
}

static HB_ERRCODE sdb_rdd_lockRecord(SDB_AREAP pSDBArea, SDB_RECNO ulRecno, CFL_UINT16 *pResult, CFL_BOOL bExclusive) {
   SDB_RECNO currentRecno;
   PHB_ITEM pRecno;

   ENTER_FUN_NAME("sdb_rdd_lockRecord");
   SDB_LOG_DEBUG(("sdb_rdd_lockRecord: recno=%u", ulRecno));

   if (pSDBArea->isQuery) {
      sdb_api_genError(pSDBArea->connection, EG_UNSUPPORTED, SDB_ERROR_INVALID_OPERATION, sdb_area_getAlias(pSDBArea), EF_NONE,
                       NULL, NULL, NULL);
      RETURN HB_FAILURE;
   }

   if (!pSDBArea->isShared || pSDBArea->isLocked) {
      *pResult = CFL_TRUE;
      RETURN HB_SUCCESS;
   }

   if (!pSDBArea->isValidBuffer && !sdb_rdd_readRecord(pSDBArea, CFL_FALSE)) {
      RETURN HB_FAILURE;
   }

   pRecno = hb_itemNew(NULL);
#ifdef __XHB__
   SELF_RECNO((AREAP)pSDBArea, pRecno);
#else
   SELF_RECID((AREAP)pSDBArea, pRecno);
#endif
   currentRecno = (SDB_RECNO)hb_itemGetNLL(pRecno);
   hb_itemRelease(pRecno);
   if (ulRecno == 0) {
      ulRecno = currentRecno;
   }

   if (bExclusive) {
      sdb_rdd_unlockAllRecords(pSDBArea);
   } else if (pSDBArea->recordLocked != NULL && sdb_rdd_isRecordLocked(pSDBArea, ulRecno)) {
      *pResult = CFL_TRUE;
      RETURN HB_SUCCESS;
   }

   /* Prevents scenario where workarea was locked by FLock, unlocked and record
    * locked by RLock. In this case, record is not flagged as locked. In other
    * scenarios, rawLock doesn't executes LOCK because isLocked flag. */
   if (pSDBArea->isHot && ulRecno == currentRecno) {
      if (bExclusive) {
         SDB_RECORDLOCKEDP lock = SDB_MEM_ALLOC(sizeof(SDB_RECORDLOCKED));
         lock->previous = pSDBArea->recordLocked;
         lock->ulRecno = ulRecno;
         pSDBArea->recordLocked = lock;
      }
      pSDBArea->record->isLocked = CFL_TRUE;
      *pResult = CFL_TRUE;
      RETURN HB_SUCCESS;
   }

   if (sdb_rdd_rawLock(pSDBArea, REC_LOCK, ulRecno) == HB_SUCCESS) {
      SDB_RECORDLOCKEDP lock = SDB_MEM_ALLOC(sizeof(SDB_RECORDLOCKED));
      lock->previous = pSDBArea->recordLocked;
      lock->ulRecno = ulRecno;
      pSDBArea->recordLocked = lock;
      *pResult = CFL_TRUE;
   } else {
      *pResult = CFL_FALSE;
   }
   RETURN HB_SUCCESS;
}

static HB_ERRCODE sdb_rdd_lockFile(SDB_AREAP pSDBArea, CFL_UINT16 *pResult) {
   ENTER_FUN_NAME("sdb_rdd_lockFile");
   SDB_LOG_DEBUG(("sdb_rdd_lockFile: alias=%s", sdb_area_getAlias(pSDBArea)));
   if (!pSDBArea->isLocked) {

      if (!pSDBArea->isValidBuffer && !sdb_rdd_readRecord(pSDBArea, CFL_FALSE)) {
         RETURN HB_FAILURE;
      }

      sdb_rdd_unlockAllRecords(pSDBArea);

      if (pSDBArea->isHot && sdb_rdd_flushRecord(pSDBArea, CFL_FALSE) != HB_SUCCESS) {
         RETURN HB_FAILURE;
      }
      sdb_rdd_rawLock(pSDBArea, FILE_LOCK, 0);
      *pResult = (CFL_UINT16)pSDBArea->isLocked;
   } else {
      *pResult = CFL_TRUE;
   }

   RETURN HB_SUCCESS;
}

static HB_ERRCODE sdb_rdd_unlockRecord(SDB_AREAP pSDBArea, SDB_RECNO ulRecno) {
   HB_ERRCODE errCode = HB_SUCCESS;
   SDB_RECORDLOCKEDP priorLock = NULL;
   SDB_RECORDLOCKEDP lock = pSDBArea->recordLocked;

   /* Search the locked record */
   while (lock != NULL && lock->ulRecno != ulRecno) {
      priorLock = lock;
      lock = lock->previous;
   }

   if (lock != NULL) {
      errCode = sdb_rdd_flushRecord(pSDBArea, CFL_FALSE);
      if (errCode != HB_SUCCESS) {
         return errCode;
      }
      sdb_rdd_rawLock(pSDBArea, REC_UNLOCK, ulRecno);
      if (lock == pSDBArea->recordLocked) {
         pSDBArea->recordLocked = lock->previous;
      } else if (priorLock) {
         priorLock->previous = lock->previous;
      }
      SDB_MEM_FREE(lock);
   }
   return errCode;
}

static CFL_BOOL sdb_rdd_keyChanged(SDB_AREAP pSDBArea) {
   ENTER_FUN_NAME("sdb_rdd_keyChanged");
   SDB_LOG_DEBUG(("sdb_rdd_keyChanged: order=%u, changed=%s, keyValue=%s", pSDBArea->uiOrder, BOOL_STR(pSDBArea->record->isChanged),
                  ITEM_STR(pSDBArea->keyValue)));
   if (pSDBArea->uiOrder > 0 && pSDBArea->keyValue != NULL) {
      CFL_BOOL bResult;
      PHB_ITEM currKey;
      SDB_INDEXP index;

      index = sdb_area_getCurrentOrder(pSDBArea);
      currKey = sdb_rdd_evalIndexExpr(NULL, index);
      bResult = !hb_itemEqual(pSDBArea->keyValue, currKey);
      hb_itemRelease(currKey);
      RETURN bResult;
   }
   RETURN CFL_FALSE;
}

static CFL_BOOL isStartsWithSelect(const char *filePathName) {
   char *ptr = (char *)filePathName;
   while (*ptr == ' ' || *ptr == '\t' || *ptr == '\r' || *ptr == '\n') {
      ++ptr;
   }
   if (hb_strnicmp(ptr, "SELECT", 6) == 0) {
      ptr += 6;
      if (*ptr == ' ' || *ptr == '\t' || *ptr == '\r' || *ptr == '\n') {
         return CFL_TRUE;
      }
   }
   return CFL_FALSE;
}

/******************************************************************************
                              RDD FUNCTIONS API
 *******************************************************************************/
#ifdef __XHB__
static HB_ERRCODE rdd_sysName(SDB_AREAP pSDBArea, HB_BYTE *pBuffer)
#else
static HB_ERRCODE rdd_sysName(SDB_AREAP pSDBArea, const char *pBuffer)
#endif
{
   HB_SYMBOL_UNUSED(pSDBArea);
   ENTER_FUN_NAME("rdd_sysName");

   strcpy((char *)pBuffer, sdb_api_getRddName());
   RETURN HB_SUCCESS;
}

static HB_ERRCODE rdd_structSize(SDB_AREAP pSDBArea, HB_USHORT *param) {
   HB_SYMBOL_UNUSED(pSDBArea);
   ENTER_FUN_NAME("rdd_structureSize");
   *param = sizeof(SDB_AREA);
   RETURN HB_SUCCESS;
}

static HB_USHORT dataFieldCount(SDB_TABLEP table) {
   CFL_UINT32 uiFieldCount = cfl_list_length(table->fields);
   CFL_UINT32 uiField;
   HB_USHORT count = 0;

   /* count data fields */
   for (uiField = 0; uiField < uiFieldCount; uiField++) {
      SDB_FIELDP field = (SDB_FIELDP)cfl_list_get(table->fields, uiField);
      if (field->fieldType == SDB_FIELD_DATA || field->fieldType == SDB_FIELD_DATA_INDEX) {
         ++count;
      }
   }
   return count;
}

static void sdb_rdd_buildIndexesCodeBlocks(SDB_AREAP pSDBArea) {
   HB_ERRCODE errCode;
   CFL_UINT32 len;
   CFL_UINT32 i;
   SDB_INDEXP index;
   CFL_LISTP codeblocks;

   ENTER_FUN_NAME("rdd_buildIndexesCodeBlocks");
   len = cfl_list_length(pSDBArea->table->indexes);
   if (!pSDBArea->table->isIndexExpressionsUpdated) {
      sdb_table_lock(pSDBArea->table);
      if (!pSDBArea->table->isIndexExpressionsUpdated) {
         pSDBArea->table->isIndexExpressionsUpdated = CFL_TRUE;
         if (len > 0) {
            codeblocks = cfl_list_new(len);
            sdb_rdd_blankRecord(pSDBArea);
            for (i = 0; i < len; i++) {
               index = (SDB_INDEXP)cfl_list_get(pSDBArea->table->indexes, i);
               if (index->field->clpExpression == NULL) {
                  cfl_list_free(codeblocks);
                  sdb_api_genError(pSDBArea->connection, EG_CORRUPTION, 1000, cfl_str_getPtr(pSDBArea->table->clpName), EF_NONE,
                                   "Invalid index expression", NULL, NULL);
                  RETURN;
               } else if (index->compiledExpr == NULL) {
                  errCode = SELF_COMPILE((AREAP)pSDBArea, cfl_str_getPtr(index->field->clpExpression));
                  if (errCode != HB_SUCCESS) {
                     CFL_UINT32 j;
                     for (j = 0; j < len; j++) {
                        hb_itemRelease((PHB_ITEM)cfl_list_get(codeblocks, j));
                     }
                  }
                  cfl_list_add(codeblocks, pSDBArea->area.valResult);
                  pSDBArea->area.valResult = NULL;
               } else {
                  cfl_list_add(codeblocks, NULL);
               }
            }
            for (i = 0; i < len; i++) {
               PHB_ITEM pBlock;

               index = (SDB_INDEXP)cfl_list_get(pSDBArea->table->indexes, i);
               pBlock = (PHB_ITEM)cfl_list_get(codeblocks, i);
               if (pBlock) {
                  index->compiledExpr = pBlock;
               }
            }
            cfl_list_free(codeblocks);
         }
      }
      sdb_table_unlock(pSDBArea->table);
   }
   RETURN;
}

static HB_ERRCODE setDefaulContext(SDB_AREAP pSDBArea) {
   HB_ERRCODE errCode = HB_SUCCESS;
   if (pSDBArea->table->isContextualized) {
      CFL_UINT32 ulIndex;

      for (ulIndex = 0; ulIndex < pSDBArea->fieldsCount; ulIndex++) {
         SDB_WAFIELDP waField = sdb_area_getFieldByPos(pSDBArea, ulIndex);
         if (waField->field->contextPos > 0) {
            cfl_list_add(pSDBArea->context, waField);
            errCode = SELF_COMPILE((AREAP)pSDBArea, cfl_str_getPtr(waField->field->contextVal));
            if (errCode == HB_SUCCESS) {
               waField->isContext = CFL_TRUE;
               sdb_record_setValue(pSDBArea->record, waField->field, pSDBArea->area.valResult);
               hb_itemRelease(pSDBArea->area.valResult);
               pSDBArea->area.valResult = NULL;
               pSDBArea->isContextActive = CFL_TRUE;
               pSDBArea->isRedoQueries = CFL_TRUE;
            }
         }
      }
      sdb_area_sortContextByPartitionKeys(pSDBArea);
   }
   return errCode;
}

static CFL_BOOL addFieldsToWorkarea(SDB_AREAP pSDBArea, CFL_BOOL open) {
   CFL_UINT32 uiField;
   DBFIELDINFO fieldInfo;
   SDB_FIELDP field;
   CFL_UINT32 uiFieldCount = cfl_list_length(pSDBArea->table->fields);
   /* Set fields */
   SELF_SETFIELDEXTENT((AREAP)pSDBArea, dataFieldCount(pSDBArea->table));
   pSDBArea->fields = (SDB_WAFIELDP)SDB_MEM_ALLOC(uiFieldCount * sizeof(SDB_WAFIELD));
   pSDBArea->fieldsCount = uiFieldCount;
   for (uiField = 0; uiField < uiFieldCount; uiField++) {
      SDB_WAFIELDP waField = &pSDBArea->fields[uiField];
      field = (SDB_FIELDP)cfl_list_get(pSDBArea->table->fields, uiField);
      if (field2FieldInfo(field, &fieldInfo) == HB_SUCCESS) {
         waField->field = field;
         waField->queryPos = uiField + 1;
         waField->isContext = CFL_FALSE;
         waField->isChanged = CFL_FALSE;
         if (open && IS_DATA_FIELD(field)) {
            pSDBArea->uiRecordLen += fieldInfo.uiLen;
            SELF_ADDFIELD((AREAP)pSDBArea, &fieldInfo);
         }
         switch (field->fieldType) {
         case SDB_FIELD_PK:
            pSDBArea->waPKField = waField;
            break;
         case SDB_FIELD_DEL_FLAG:
            pSDBArea->waDelField = waField;
            break;
         case SDB_FIELD_ROWID:
            pSDBArea->waRowIdField = waField;
            break;
         default:
            break;
         }
      } else {
         return CFL_FALSE;
      }
   }
   return CFL_TRUE;
}

static HB_ERRCODE rdd_open(SDB_AREAP pSDBArea, LPDBOPENINFO pOpenInfo) {
   SDB_CONNECTIONP connection;
   HB_ERRCODE errCode = HB_SUCCESS;
   SDB_THREAD_DATAP thData;
   CFL_BOOL isQuery;
   CFL_BOOL isQueryArg = CFL_FALSE;
   char szAlias[HB_RDD_MAX_ALIAS_LEN + 1];

   ENTER_FUN_NAME("rdd_open");
   SDB_LOG_DEBUG(("rdd_open: name=%s", pOpenInfo->abName));
   sdb_thread_cleanError();

   thData = sdb_thread_getData();
   isQuery = thData->pStatement != NULL;

// #if __HARBOUR__ >= 0x030200 || __XHARBOUR__ >= 0x0123
#ifdef __HBR__
   if (pOpenInfo->ulConnection == 0) {
      connection = thData->connection;
   } else {
      connection = sdb_api_getConnection((CFL_UINT16)pOpenInfo->ulConnection);
   }
#else
   connection = thData->connection;
#endif
   if (connection == NULL) {
      if (thData->pStatement != NULL) {
         sdb_stmt_free(thData->pStatement);
         thData->pStatement = NULL;
      }
      SELF_CLOSE((AREAP)pSDBArea);
      hb_rddSetNetErr(HB_TRUE);
      sdb_api_genError(NULL, EG_OPEN, 1000, pOpenInfo->abName, EF_CANDEFAULT, "No connection", NULL, NULL);
      RETURN HB_FAILURE;
   }
   pSDBArea->connection = connection;
   pSDBArea->lockControl = connection->lockControl;

   /* Verify if was passed an query to open */
   if (!isQuery && isStartsWithSelect(pOpenInfo->abName)) {
      isQueryArg = CFL_TRUE;
      thData->pStatement = sdb_connection_prepareStatementBuffer(connection, pOpenInfo->abName);
      if (thData->pStatement != NULL) {
         thData->pStatement->isReleaseOnClose = CFL_TRUE;
         if (sdb_thread_hasParams(thData)) {
            sdb_param_listMoveAll(sdb_stmt_getParams(thData->pStatement), sdb_thread_getParams(thData));
            sdb_thread_freeParams(thData);
         }
         if (thData->pStatement->type == SDB_STMT_QUERY) {
            isQuery = CFL_TRUE;
         } else {
            sdb_stmt_free(thData->pStatement);
            thData->pStatement = NULL;
            SELF_CLOSE((AREAP)pSDBArea);
            hb_rddSetNetErr(HB_TRUE);
            sdb_api_genError(connection, EG_OPEN, 1000, pOpenInfo->abName, EF_CANDEFAULT, "Invalid query", NULL, NULL);
            RETURN HB_FAILURE;
         }
      } else if (sdb_thread_hasError()) {
         // SELF_CLOSE((AREAP) pSDBArea);
         hb_rddSetNetErr(HB_TRUE);
         sdb_api_genErrorFromSDBError(connection, "Databse error", EF_NONE, sdb_thread_getLastError(), NULL, NULL);
         RETURN HB_FAILURE;
      }
   }

   pSDBArea->isReadOnly = isQuery || pOpenInfo->fReadonly;
   pSDBArea->isShared = !isQuery && pOpenInfo->fShared;
   pSDBArea->isQuery = isQuery;

   if (isQuery) {
      pSDBArea->table = sdb_api_openQueryStmt(pSDBArea, thData->pStatement, sdb_thread_isPadFields(thData));
      pSDBArea->isValidBuffer = CFL_FALSE;
      if (pSDBArea->table) {
         thData->pStatement = NULL;
         if (!pOpenInfo->atomAlias) {
            if (isQueryArg) {
               hb_rddGetTempAlias(szAlias);
            } else {
               hb_strncpyUpperTrim(szAlias, pOpenInfo->abName, sizeof(szAlias) - 1);
            }
            pOpenInfo->atomAlias = szAlias;
         }
      } else if (sdb_thread_hasError()) {
         hb_strncpyUpperTrim(szAlias, sdb_area_getAlias(pSDBArea), sizeof(szAlias) - 1);
         // SELF_CLOSE((AREAP) pSDBArea);
         hb_rddSetNetErr(HB_TRUE);
         sdb_api_genErrorFromSDBError(connection, (szAlias[0] != '\0' ? szAlias : "Databse error"), EF_NONE,
                                      sdb_thread_getLastError(), NULL, NULL);
         RETURN HB_FAILURE;
      }

   } else {
      PHB_ITEM pError;
      CFL_STRP tableName;
      PHB_FNAME pFileName = hb_fsFNameSplit(pOpenInfo->abName);
      char *schemaName = sdb_util_getSchemaName(connection, pFileName->szPath);
      tableName = cfl_str_toUpper(cfl_str_trim(cfl_str_newBuffer(pFileName->szName)));
      pError = NULL;
      do {
         pSDBArea->table = sdb_api_getTable(connection, schemaName, cfl_str_getPtr(tableName));
         if (pSDBArea->table != NULL) {
            errCode = HB_SUCCESS;
            break;
         } else if (pError == NULL) {
            pError = hb_errNew();
         }
         errCode = sdb_api_genError(connection, EG_OPEN, SDB_ERROR_OPEN, pOpenInfo->abName, EF_CANRETRY | EF_CANDEFAULT,
                                    "Table not found", NULL, &pError);
      } while (errCode == E_RETRY);
      if (errCode != HB_SUCCESS) {
         hb_xfree(pFileName);
         cfl_str_free(tableName);
         SDB_MEM_FREE(schemaName);
         SELF_CLOSE((AREAP)pSDBArea);
         hb_rddSetNetErr(HB_TRUE);
         RETURN errCode;
      }

      if (!pOpenInfo->atomAlias) {
         const char *szName = strrchr(pFileName->szName, ':');
         if (szName == NULL) {
            szName = pFileName->szName;
         } else {
            ++szName;
         }
         hb_strncpyUpperTrim(szAlias, szName, sizeof(szAlias) - 1);
         pOpenInfo->atomAlias = szAlias;
      }

      if (pError) {
         hb_itemRelease(pError);
      }
      hb_xfree(pFileName);
      cfl_str_free(tableName);
      SDB_MEM_FREE(schemaName);
   }

   if (pSDBArea->table == NULL) {
      SELF_CLOSE((AREAP)pSDBArea);
      hb_rddSetNetErr(HB_TRUE);
      if (isQuery) {
         sdb_api_genErrorFromSDBError(connection, pOpenInfo->abName, EF_NONE, sdb_thread_getLastError(), NULL, NULL);
      } else {
         sdb_api_genError(connection, EG_OPEN, 1000, pOpenInfo->abName, EF_CANDEFAULT, "Table does not exist", NULL, NULL);
      }
      RETURN HB_FAILURE;
   }

   if (!addFieldsToWorkarea(pSDBArea, CFL_TRUE)) {
      // SELF_CLOSE((AREAP) pSDBArea);
      hb_rddSetNetErr(HB_TRUE);
      sdb_api_genError(connection, EG_CORRUPTION, SDB_ERROR_CORRUPT_DICT, pOpenInfo->abName, EF_CANDEFAULT, NULL, NULL, NULL);
      RETURN HB_FAILURE;
   }

   if (!isQuery) {
      /* File already opened in exclusive mode */
      /* I have to do this check here because, in case of error, SELF_CLOSE() is
       * called however */
      if (!sdb_api_lockAreaTable(pSDBArea, pOpenInfo->fShared ? SDB_LOCK_SHARED : SDB_LOCK_EXCLUSIVE)) {
         // SELF_CLOSE((AREAP) pSDBArea);
         hb_rddSetNetErr(HB_TRUE);
         if (sdb_thread_hasError()) {
            sdb_api_genErrorFromSDBError(connection, sdb_area_getAlias(pSDBArea), EF_NONE, sdb_thread_getLastError(), NULL, NULL);
         }
         RETURN HB_FAILURE;
      }
   }

   /* If successful call SUPER_OPEN to finish system jobs */
#ifdef __XHB__
   pSDBArea->area.atomAlias = hb_rddAllocWorkAreaAlias((char *)pOpenInfo->atomAlias, (int)pOpenInfo->uiArea);
#else
   errCode = SUPER_OPEN((AREAP)pSDBArea, pOpenInfo);
#endif

   if (errCode == HB_SUCCESS) {
      sdb_api_registerArea(pSDBArea);
   } else {
      SELF_CLOSE((AREAP)pSDBArea);
      hb_rddSetNetErr(HB_TRUE);
      RETURN HB_FAILURE;
   }

   pSDBArea->record = sdb_record_new(pSDBArea);
   if (isQuery) {
      pSDBArea->isShared = CFL_FALSE;
      rdd_resetAreaFlags(pSDBArea);
      if (sdb_rdd_fetchNextRecord(pSDBArea, SDB_SORT_ASC)) {
         sdb_rdd_readRecord(pSDBArea, CFL_FALSE);
      }
   } else {
      pSDBArea->isInsertImmediate = !thData->isDelayAppend;
      sdb_rdd_buildIndexesCodeBlocks(pSDBArea);
      errCode = setDefaulContext(pSDBArea);
      if (errCode == HB_SUCCESS) {
         errCode = SELF_GOTOP((AREAP)pSDBArea);
      }
   }
   if (errCode != HB_SUCCESS) {
      SELF_CLOSE((AREAP)pSDBArea);
      hb_rddSetNetErr(HB_TRUE);
   } else {
      hb_rddSetNetErr(HB_FALSE);
   }
   RETURN errCode;
}

static HB_ERRCODE rdd_release(SDB_AREAP pSDBArea) {
   HB_ERRCODE errorCode;
   SDB_CONNECTIONP connection = pSDBArea->connection;
   SDB_LOG_DEBUG(("rdd_release: alias=%s query=%s", sdb_area_getAlias(pSDBArea), (pSDBArea->isQuery ? "true" : "false")));
   sdb_thread_cleanError();
   sdb_api_deregisterArea(pSDBArea);
   if (pSDBArea->isQuery) {
      if (pSDBArea->pStatement != NULL && pSDBArea->pStatement->isReleaseOnClose) {
         sdb_stmt_free(pSDBArea->pStatement);
         pSDBArea->pStatement = NULL;
      }
      sdb_area_clear(pSDBArea);
      sdb_table_free(pSDBArea->table);
   } else {
      sdb_area_closeStatements(pSDBArea);
      sdb_area_clear(pSDBArea);
   }
   errorCode = SUPER_RELEASE((AREAP)pSDBArea);
   if (sdb_thread_hasError()) {
      sdb_api_genErrorFromSDBError(connection, sdb_area_getAlias(pSDBArea), EF_NONE, sdb_thread_getLastError(), NULL, NULL);
      errorCode = HB_FAILURE;
   }
   return errorCode;
}

static HB_ERRCODE rdd_close(SDB_AREAP pSDBArea) {
   SDB_LOG_DEBUG(("rdd_close: alias=%s", sdb_area_getAlias(pSDBArea)));
   sdb_thread_cleanError();

   if (pSDBArea->table != NULL) {
      if (sdb_rdd_flushRecord(pSDBArea, CFL_TRUE) == HB_FAILURE) {
         RETURN HB_FAILURE;
      }
      if (!pSDBArea->isQuery) {
         SELF_UNLOCK((AREAP)pSDBArea, 0);
         if (!sdb_api_unlockAreaTable(pSDBArea) && sdb_thread_hasError()) {
            sdb_api_genErrorFromSDBError(pSDBArea->connection, sdb_area_getAlias(pSDBArea), EF_NONE, sdb_thread_getLastError(),
                                         NULL, NULL);
         }
         SELF_ORDLSTCLEAR((AREAP)pSDBArea);
      }
   }
   return SUPER_CLOSE((AREAP)pSDBArea);
}

static HB_ERRCODE rdd_goTop(SDB_AREAP pSDBArea) {
   SDB_LOG_DEBUG(("rdd_goTop: alias=%s", sdb_area_getAlias(pSDBArea)));
   sdb_thread_cleanError();

   if (sdb_rdd_flushRecord(pSDBArea, CFL_TRUE) == HB_FAILURE) {
      return HB_FAILURE;
   }

   pSDBArea->area.fTop = HB_TRUE;
   pSDBArea->area.fBottom = HB_FALSE;
   rdd_resetAreaFlags(pSDBArea);
   if (pSDBArea->isQuery) {
      CFL_UINT64 ulAffectedRows;
      if (sdb_stmt_execute(pSDBArea->pStatement, &ulAffectedRows)) {
         if (sdb_rdd_fetchNextRecord(pSDBArea, SDB_SORT_ASC)) {
            sdb_rdd_readRecord(pSDBArea, CFL_FALSE);
         }
      } else {
         sdb_api_genErrorFromSDBError(pSDBArea->connection, sdb_area_getAlias(pSDBArea), EF_NONE, sdb_thread_getLastError(), NULL,
                                      NULL);
         return HB_FAILURE;
      }
   } else if (sdb_api_tableGoTop(pSDBArea)) {
      if (sdb_rdd_fetchNextRecord(pSDBArea, SDB_SORT_ASC)) {
         sdb_rdd_readRecord(pSDBArea, CFL_FALSE);
         return SELF_SKIPFILTER((AREAP)pSDBArea, 1);
      }
   } else {
      sdb_api_genErrorFromSDBError(pSDBArea->connection, sdb_area_getAlias(pSDBArea), EF_NONE, sdb_thread_getLastError(), NULL,
                                   NULL);
      return HB_FAILURE;
   }
   return HB_SUCCESS;
}

HB_ERRCODE sdb_rdd_goBottom(SDB_AREAP pSDBArea, CFL_BOOL bFetch) {
   SDB_LOG_DEBUG(("sdb_rdd_goBottom: alias=%s", sdb_area_getAlias(pSDBArea)));
   sdb_thread_cleanError();

   if (sdb_rdd_flushRecord(pSDBArea, CFL_TRUE) == HB_FAILURE) {
      return HB_FAILURE;
   }

   pSDBArea->area.fTop = HB_FALSE;
   pSDBArea->area.fBottom = HB_TRUE;
   rdd_resetAreaFlags(pSDBArea);
   if (sdb_api_tableGoBottom(pSDBArea)) {
      if (bFetch && sdb_rdd_fetchNextRecord(pSDBArea, SDB_SORT_DESC)) {
         sdb_rdd_readRecord(pSDBArea, CFL_FALSE);
         return SELF_SKIPFILTER((AREAP)pSDBArea, -1);
      }
   } else {
      sdb_api_genErrorFromSDBError(pSDBArea->connection, sdb_area_getAlias(pSDBArea), EF_NONE, sdb_thread_getLastError(), NULL,
                                   NULL);
      return HB_FAILURE;
   }
   return HB_SUCCESS;
}

static HB_ERRCODE rdd_goBottom(SDB_AREAP pSDBArea) {
   SDB_LOG_DEBUG(("rdd_goBottom: alias=%s", sdb_area_getAlias(pSDBArea)));
   if (!pSDBArea->isQuery) {
      return sdb_rdd_goBottom(pSDBArea, CFL_TRUE);
   }
   return HB_SUCCESS;
}

static HB_ERRCODE rdd_goTo(SDB_AREAP pSDBArea, HB_RECNO ulRecord) {
   HB_ERRCODE errCode;
   PHB_ITEM pRecno;

   ENTER_FUN_NAME("rdd_goTo");
   pRecno = hb_itemPutNLL(NULL, (HB_LONGLONG)ulRecord);
   errCode = SELF_GOTOID((AREAP)pSDBArea, pRecno);
   hb_itemRelease(pRecno);
   RETURN errCode;
}

static HB_ERRCODE rdd_goToId(SDB_AREAP pSDBArea, PHB_ITEM pRecId) {
   ENTER_FUN_NAME("rdd_goToId");
   SDB_LOG_DEBUG(("rdd_goToId: alias=%s rec=%s", sdb_area_getAlias(pSDBArea), ITEM_STR(pRecId)));
   sdb_thread_cleanError();
   if (pSDBArea->isQuery) {
      sdb_api_genError(pSDBArea->connection, EG_UNSUPPORTED, SDB_ERROR_INVALID_OPERATION, sdb_area_getAlias(pSDBArea), EF_NONE,
                       NULL, NULL, NULL);
      RETURN HB_FAILURE;
   }

   if (sdb_rdd_flushRecord(pSDBArea, CFL_TRUE) == HB_FAILURE) {
      RETURN HB_FAILURE;
   }

   rdd_resetAreaFlags(pSDBArea);
   if (HB_IS_NUMERIC(pRecId) && (SDB_RECNO)hb_itemGetNLL(pRecId) <= 0) {
      /* Clear record buffer */
      pSDBArea->area.fBof = HB_TRUE;
      pSDBArea->area.fEof = HB_TRUE;
      sdb_rdd_blankRecord(pSDBArea);
      RETURN HB_SUCCESS;
   }

   if (!sdb_api_tableGoTo(pSDBArea, pRecId)) {
      sdb_api_genErrorFromSDBError(pSDBArea->connection, sdb_area_getAlias(pSDBArea), EF_NONE, sdb_thread_getLastError(), NULL,
                                   NULL);
      RETURN HB_FAILURE;
   }
   if (sdb_rdd_fetchNextRecord(pSDBArea, SDB_SORT_ASC)) {
      sdb_rdd_readRecord(pSDBArea, CFL_FALSE);
   }
   RETURN HB_SUCCESS;
}

static HB_ERRCODE rdd_skipRaw(SDB_AREAP pSDBArea, HB_INT32 lToSkip) {
   HB_ERRCODE errCode = HB_SUCCESS;

   ENTER_FUN_NAME("rdd_skipRaw");

   SDB_LOG_DEBUG(("rdd_skipRaw: skip=%d order=%u unpositioned=%s redo query=%s", lToSkip, pSDBArea->uiOrder,
                  (pSDBArea->isUnpositioned ? "yes" : "no"), (pSDBArea->isRedoQueries ? "yes" : "no")));

   sdb_thread_cleanError();
   pSDBArea->area.fTop = HB_FALSE;
   pSDBArea->area.fBottom = HB_FALSE;
   if (pSDBArea->isQuery) {
      if (lToSkip == 0) {
         errCode = sdb_rdd_readRecord(pSDBArea, CFL_FALSE) ? HB_SUCCESS : HB_FAILURE;
      } else if (lToSkip > 0) {
         while (lToSkip > 0) {
            if (!sdb_rdd_fetchNextRecord(pSDBArea, SDB_SORT_ASC)) {
               break;
            }
            --lToSkip;
         }
         errCode = sdb_rdd_readRecord(pSDBArea, CFL_FALSE) ? HB_SUCCESS : HB_FAILURE;
      } else {
         sdb_api_genError(pSDBArea->connection, EG_UNSUPPORTED, SDB_ERROR_UNSUPPORTED, sdb_area_getAlias(pSDBArea), EF_NONE, NULL,
                          NULL, NULL);
         RETURN HB_FAILURE;
      }
   } else {
      if (lToSkip < 0) {
         if (sdb_rdd_flushRecord(pSDBArea, CFL_TRUE) == HB_FAILURE) {
            RETURN HB_FAILURE;
         }
         if (pSDBArea->isUnpositioned || sdb_area_discardCommand(pSDBArea, pSDBArea->pCurrentCommand, SDB_SORT_DESC) ||
             sdb_rdd_keyChanged(pSDBArea) || pSDBArea->isRedoQueries) {
            if (!sdb_api_tableFromRecno(pSDBArea, SDB_SORT_DESC, CFL_TRUE)) {
               RETURN HB_FAILURE;
            }
         }
         while (lToSkip < 0) {
            if (!sdb_rdd_fetchNextRecord(pSDBArea, SDB_SORT_DESC)) {
               break;
            }
            ++lToSkip;
         }
         errCode = sdb_rdd_readRecord(pSDBArea, CFL_FALSE) ? HB_SUCCESS : HB_FAILURE;
      } else if (!pSDBArea->area.fEof) {
         if (lToSkip > 0) {
            if (sdb_rdd_flushRecord(pSDBArea, CFL_TRUE) == HB_FAILURE) {
               RETURN HB_FAILURE;
            }
            if (pSDBArea->isUnpositioned || sdb_area_discardCommand(pSDBArea, pSDBArea->pCurrentCommand, SDB_SORT_ASC) ||
                sdb_rdd_keyChanged(pSDBArea) || pSDBArea->isRedoQueries) {
               if (!sdb_api_tableFromRecno(pSDBArea, SDB_SORT_ASC, CFL_TRUE)) {
                  RETURN HB_FAILURE;
               }
            }
            while (lToSkip > 0) {
               if (!sdb_rdd_fetchNextRecord(pSDBArea, SDB_SORT_ASC)) {
                  break;
               }
               --lToSkip;
            }
            errCode = sdb_rdd_readRecord(pSDBArea, CFL_FALSE) ? HB_SUCCESS : HB_FAILURE;
         } else {
            errCode = sdb_rdd_reloadCurrentRecord(pSDBArea, CFL_FALSE);
         }
      }
   }
   RETURN errCode;
}

static HB_ERRCODE rdd_newArea(SDB_AREAP pSDBArea) {
   ENTER_FUN_NAME("rdd_newArea");
   if (SUPER_NEW((AREAP)pSDBArea) == HB_SUCCESS) {
      sdb_area_init(pSDBArea);
      RETURN HB_SUCCESS;
   }
   RETURN HB_FAILURE;
}

static HB_ERRCODE rdd_orderInfo(SDB_AREAP pSDBArea, HB_USHORT info, LPDBORDERINFO pOrdInfo) {
   SDB_INDEXP index;

   ENTER_FUN_NAME("rdd_orderInfo");
   sdb_thread_cleanError();
   if (pOrdInfo->itmOrder == NULL) {
      index = sdb_area_getCurrentOrder(pSDBArea);
   } else {
      const char *cType = hb_itemTypeStr(pOrdInfo->itmOrder);
      switch (cType[0]) {
      case 'C':
         index = sdb_rdd_getIndexByName(pSDBArea->orders, hb_itemGetCPtr(pOrdInfo->itmOrder));
         break;
      case 'N':
         index = sdb_area_getOrder(pSDBArea, (CFL_INT16)hb_itemGetNL(pOrdInfo->itmOrder) - 1);
         break;
      default:
         index = sdb_area_getCurrentOrder(pSDBArea);
      }
   }

   switch (info) {
   case DBOI_EXPRESSION: /* 2 */
      if (index == NULL) {
         hb_itemPutC(pOrdInfo->itmResult, "");
      } else {
         hb_itemPutC(pOrdInfo->itmResult, cfl_str_getPtr(index->field->clpExpression));
      }
      break;
   case DBOI_KEYVAL:
      if (index != NULL) {
         pOrdInfo->itmResult = sdb_rdd_evalIndexExpr(pOrdInfo->itmResult, index);
      } else if (pOrdInfo->itmResult != NULL) {
         hb_itemClear(pOrdInfo->itmResult);
      }

      break;
   case DBOI_POSITION: /* 3 */
      sdb_util_itemPutRecno(pOrdInfo->itmResult, pSDBArea->record->ulRowNum);
      break;
   case DBOI_BAGNAME: /* 7 */
      hb_itemPutC(pOrdInfo->itmResult, "");
      break;
   case DBOI_KEYCOUNT: /* 26 */
      hb_itemPutNL(pOrdInfo->itmResult, 0);
      break;
   case DBOI_SCOPETOP: /* 39 */
      if (pOrdInfo->itmResult != NULL) {
         hb_itemClear(pOrdInfo->itmResult);
         //            hb_itemCopy(pOrdInfo->itmResult, pSDBArea->scopeTop);
      }
      //         TODO: Not supported
      //         if (pOrdInfo->itmNewVal != NULL) {
      //            pSDBArea->scopeTop = hb_itemCopy(pSDBArea->scopeTop,
      //            pOrdInfo->itmNewVal);
      //         }
      break;
   case DBOI_SCOPEBOTTOM: /* 40 */
      if (pOrdInfo->itmResult != NULL) {
         hb_itemClear(pOrdInfo->itmResult);
         //            hb_itemCopy(pOrdInfo->itmResult, pSDBArea->scopeBottom);
      }
      //         TODO: Not supported
      //         if (pOrdInfo->itmNewVal != NULL) {
      //            pSDBArea->scopeBottom = hb_itemCopy(pSDBArea->scopeBottom,
      //            pOrdInfo->itmNewVal);
      //         }
      break;
   case DBOI_SCOPETOPCLEAR: /* 41 */
      if (pOrdInfo->itmResult != NULL) {
         hb_itemClear(pOrdInfo->itmResult);
         //            hb_itemCopy(pOrdInfo->itmResult, pSDBArea->scopeTop);
      }
      //         hb_itemRelease(pSDBArea->scopeTop);
      //         pSDBArea->scopeTop = NULL;
      break;
   case DBOI_SCOPEBOTTOMCLEAR: /* 42 */
      if (pOrdInfo->itmResult != NULL) {
         hb_itemClear(pOrdInfo->itmResult);
         //            hb_itemCopy(pOrdInfo->itmResult, pSDBArea->scopeBottom);
      }
      //         hb_itemRelease(pSDBArea->scopeBottom);
      //         pSDBArea->scopeBottom = NULL;
      break;
   case DBOI_NUMBER:
      hb_itemPutNI(pOrdInfo->itmResult, pSDBArea->uiOrder);
      break;
   case DBOI_NAME:
      if (index) {
         pOrdInfo->itmResult = hb_itemPutC(pOrdInfo->itmResult, cfl_str_getPtr(index->field->clpIndexName));
      } else {
         pOrdInfo->itmResult = hb_itemPutC(pOrdInfo->itmResult, "");
      }
      break;
   case DBOI_ORDERCOUNT:
      pOrdInfo->itmResult = hb_itemPutNI(pOrdInfo->itmResult, cfl_list_length(pSDBArea->orders));
      break;

   default:
      RETURN HB_FAILURE;
   }

   RETURN HB_SUCCESS;
}

static HB_ERRCODE rdd_clearFilter(SDB_AREAP pSDBArea) {
   ENTER_FUN_NAME("rdd_clearFilter");
   sdb_thread_cleanError();
   if (pSDBArea->sqlFilter) {
      hb_itemRelease(pSDBArea->sqlFilter);
      pSDBArea->sqlFilter = NULL;
      pSDBArea->isRedoQueries = CFL_TRUE;
   }
   RETURN SUPER_CLEARFILTER(&pSDBArea->area);
}

static HB_ERRCODE rdd_setFilter(SDB_AREAP pSDBArea, LPDBFILTERINFO pFilterInfo) {
   ENTER_FUN_NAME("rdd_setFilter");
   SDB_LOG_DEBUG(("rdd_setFilter: alias=%s", sdb_area_getAlias(pSDBArea)));
   sdb_thread_cleanError();
   if (SUPER_SETFILTER(&pSDBArea->area, pFilterInfo) != HB_SUCCESS) {
      RETURN HB_FAILURE;
   }
   if (pFilterInfo->abFilterText) {
      PHB_ITEM pSql = sdb_api_clipperToSql(pSDBArea, hb_itemGetCPtr(pFilterInfo->abFilterText),
                                           (CFL_UINT32)hb_itemGetCLen(pFilterInfo->abFilterText), SDB_EXPR_CONDITION);
      if (pSql) {
         pSDBArea->sqlFilter = pSql;
         pSDBArea->isRedoQueries = CFL_TRUE;
      }
   }
   RETURN HB_SUCCESS;
}

static SDB_RECNO sdb_rdd_maxRecno(SDB_AREAP pSDBArea) {
   SDB_RECNO maxRecno;
   ENTER_FUN_NAME("rdd_maxRecno");

   if (pSDBArea->record == NULL) {
      maxRecno = 0;
   } else if (pSDBArea->isQuery) {
      maxRecno = pSDBArea->record->ulRowNum;
      /* If append is set then add 1 to max recno */
   } else if (pSDBArea->isAppend) {
      if (sdb_record_getInt64(pSDBArea->record, pSDBArea->table->pkField) > 0) {
         maxRecno = (SDB_RECNO)sdb_record_getInt64(pSDBArea->record, pSDBArea->table->pkField);
      } else {
         maxRecno = sdb_api_tableMaxRecno(pSDBArea) + 1;
      }
   } else {
      maxRecno = sdb_api_tableMaxRecno(pSDBArea);
   }
   RETURN maxRecno;
}

static HB_ERRCODE rdd_info(SDB_AREAP pSDBArea, HB_USHORT index, PHB_ITEM pItem) {
   HB_ERRCODE errCode = HB_SUCCESS;

   ENTER_FUN_NAME("rdd_info");
   sdb_thread_cleanError();
   switch (index) {
   case DBI_ISDBF:
      hb_itemPutL(pItem, CFL_TRUE);
      break;

   case DBI_CANPUTREC:
      hb_itemPutL(pItem, CFL_FALSE);
      break;

   case DBI_TABLEEXT:
      hb_itemPutC(pItem, SDB_FILE_EXT);
      break;

   case DBI_LASTUPDATE:
      hb_itemPutD(pItem, 1900, 1, 1);
      break;

   case DBI_GETHEADERSIZE:
   case DBI_MEMOBLOCKSIZE:
      hb_itemPutNL(pItem, 0);
      break;

   case DBI_ISFLOCK:
      hb_itemPutL(pItem, pSDBArea->isLocked);
      break;

   case DBI_ISREADONLY:
      hb_itemPutL(pItem, pSDBArea->isReadOnly);
      break;

   case DBI_GETRECSIZE:
      hb_itemPutNL(pItem, pSDBArea->table->dataLen);
      break;

   case DBI_SHARED:
      hb_itemPutL(pItem, pSDBArea->isShared);
      break;

   case DBI_FULLPATH:
#ifdef __HBR__
      hb_itemPutCPtr(pItem, hb_xstrcpy(NULL, cfl_str_getPtr(pSDBArea->table->clpSchema->name), "\\",
                                       cfl_str_getPtr(pSDBArea->table->clpName)));
#else
   {
      char *str =
          hb_xstrcpy(NULL, cfl_str_getPtr(pSDBArea->table->clpSchema->name), "\\", cfl_str_getPtr(pSDBArea->table->clpName));
      hb_itemPutCPtr(pItem, str, strlen(str));
   }
#endif
      break;

   case DBI_SDB_ISRECNO64:
      hb_itemPutL(pItem, !pSDBArea->isQuery && pSDBArea->table->pkField->length > 10);
      break;

   case DBI_SDB_RECCOUNT64: {
      SDB_RECNO ulRecCount;
      sdb_thread_cleanError();
      ulRecCount = sdb_rdd_maxRecno(pSDBArea);
      if (!sdb_thread_hasError()) {
         hb_itemPutNLL(pItem, (HB_LONGLONG)ulRecCount);
      } else {
         sdb_api_genErrorFromSDBError(pSDBArea->connection, sdb_area_getAlias(pSDBArea), EF_NONE, sdb_thread_getLastError(), NULL,
                                      NULL);
         errCode = HB_FAILURE;
      }
   } break;

   case DBI_SDB_DBRUNLOCK64:
#ifdef __XHB__
      if (!pSDBArea->isQuery && pSDBArea->isShared) {
         SDB_RECNO ulRecno = (SDB_RECNO)hb_itemGetNLL(pItem);
         sdb_thread_cleanError();
         if (ulRecno == 0) { /* Unlock All */
            errCode = sdb_rdd_unlockAllRecords(pSDBArea);
         } else {
            errCode = sdb_rdd_unlockRecord(pSDBArea, ulRecno);
         }
         pSDBArea->isLocked = CFL_FALSE;
      }
#else
      errCode = SELF_UNLOCK((AREAP)pSDBArea, pItem);
#endif
      break;

   default:
      errCode = SUPER_INFO((AREAP)pSDBArea, index, pItem);
      break;
   }

   RETURN errCode;
}

static HB_ERRCODE rdd_orderListAdd(SDB_AREAP pSDBArea, LPDBORDERINFO pOrdInfo) {
   HB_ERRCODE errCode;
   SDB_INDEXP index;

   SDB_LOG_DEBUG(("rdd_orderListAdd: alias=%s", sdb_area_getAlias(pSDBArea)));
   ENTER_FUN_NAME("rdd_orderListAdd");
   sdb_thread_cleanError();
   if (pSDBArea->isQuery) {
      sdb_api_genError(pSDBArea->connection, EG_UNSUPPORTED, SDB_ERROR_NOT_INDEXED, sdb_area_getAlias(pSDBArea), EF_NONE, NULL,
                       NULL, NULL);
      RETURN HB_FAILURE;
   }

   errCode = sdb_rdd_flushRecord(pSDBArea, CFL_TRUE);
   if (errCode != HB_SUCCESS) {
      RETURN errCode;
   }

   if (cfl_list_length(pSDBArea->table->indexes) == 0) {
      pSDBArea->uiOrder = 0;
      pSDBArea->record->ulRowNum = 0;
      errCode = HB_FAILURE;
   } else {
      if (sdb_util_itemIsNull(pOrdInfo->itmOrder)) {
         index = sdb_rdd_getIndexByName(pSDBArea->orders, hb_itemGetCPtr(pOrdInfo->atomBagName));
      } else {
         index = sdb_rdd_getIndexByName(pSDBArea->orders, hb_itemGetCPtr(pOrdInfo->itmOrder));
      }
      if (index == NULL) {
         index = sdb_rdd_getIndexByName(pSDBArea->table->indexes, hb_itemGetCPtr(pOrdInfo->atomBagName));
         if (index != NULL) {
            cfl_list_add(pSDBArea->orders, index);
            if (cfl_list_length(pSDBArea->orders) == 1) {
               pSDBArea->uiOrder = 1;
#ifdef __HBR__
               errCode = SELF_GOTOP((AREAP)pSDBArea);
#endif
            }
         } else {
            sdb_api_genError(pSDBArea->connection, EG_UNSUPPORTED, SDB_ERROR_NOT_INDEXED, sdb_area_getAlias(pSDBArea), EF_NONE,
                             "Invalid parameters (Index does not exist) : OrdListAdd", NULL, NULL);
            errCode = HB_FAILURE;
         }
      } else {
         errCode = HB_FAILURE;
      }
   }

   RETURN errCode;
}

static HB_ERRCODE rdd_orderListClear(SDB_AREAP pSDBArea) {
   HB_ERRCODE errCode;

   ENTER_FUN_NAME("rdd_orderListClear");
   SDB_LOG_DEBUG(("rdd_orderListClear: alias=%s", sdb_area_getAlias(pSDBArea)));
   sdb_thread_cleanError();
   errCode = sdb_rdd_flushRecord(pSDBArea, CFL_TRUE);
   if (errCode != HB_SUCCESS) {
      RETURN errCode;
   }

   if (cfl_list_length(pSDBArea->orders) > 0) {
      cfl_list_clear(pSDBArea->orders);
      pSDBArea->uiOrder = 0;
      pSDBArea->record->ulRowNum = 0;
      hb_itemRelease(pSDBArea->keyValue);
      pSDBArea->keyValue = NULL;
   }

   RETURN HB_SUCCESS;
}

static HB_ERRCODE rdd_orderListFocus(SDB_AREAP pSDBArea, LPDBORDERINFO pOrdInfo) {
   SDB_INDEXP index;
   HB_INT32 iNewOrd = -1;

   SDB_LOG_DEBUG(("rdd_orderListFocus: alias=%s itmOrder=%s atomBagName=%s", sdb_area_getAlias(pSDBArea),
                  ITEM_STR(pOrdInfo->itmOrder), ITEM_STR(pOrdInfo->atomBagName)));
   sdb_thread_cleanError();
   index = sdb_area_getCurrentOrder(pSDBArea);
   pOrdInfo->itmResult = index != NULL ? hb_itemPutC(pOrdInfo->itmResult, cfl_str_getPtr(index->field->clpIndexName)) : NULL;

   if (sdb_util_itemIsNull(pOrdInfo->itmOrder)) {
      iNewOrd = sdb_rdd_getIndexPosByName(pSDBArea->orders, hb_itemGetCPtr(pOrdInfo->atomBagName));
   } else {
      const char *cType = hb_itemTypeStr(pOrdInfo->itmOrder);
      switch (cType[0]) {
      case 'C':
         iNewOrd = sdb_rdd_getIndexPosByName(pSDBArea->orders, hb_itemGetCPtr(pOrdInfo->itmOrder));
         break;
      case 'N':
         iNewOrd = hb_itemGetNL(pOrdInfo->itmOrder) - 1;
         break;
      }
   }

   if (iNewOrd >= -1 && pSDBArea->uiOrder != (CFL_UINT8)iNewOrd + 1) {
      pSDBArea->uiOrder = (CFL_UINT8)iNewOrd + 1;
      pSDBArea->record->ulRowNum = 0;
      hb_itemRelease(pSDBArea->keyValue);
      pSDBArea->keyValue = NULL;
   }

   return HB_SUCCESS;
}

static HB_ERRCODE rdd_exit(void) {
   ENTER_FUN_NAME("rdd_exit");
   RETURN HB_SUCCESS;
}

static CFL_BOOL sdb_rdd_keyCompare(PHB_ITEM pSearch, PHB_ITEM pKey) {
   SDB_LOG_TRACE(
       ("sdb_rdd_keyCompare: %s(%s)==%s(%s)", ITEM_STR(pSearch), hb_itemTypeStr(pSearch), ITEM_STR(pKey), hb_itemTypeStr(pKey)));
   if (HB_IS_STRING(pKey)) {
      HB_SIZE lenSearch = hb_itemGetCLen(pSearch);
      HB_SIZE lenKey = hb_itemGetCLen(pKey);

      if (lenSearch <= lenKey) {
         return memcmp(hb_itemGetCPtr(pKey), hb_itemGetCPtr(pSearch), lenSearch) == 0;
      } else if (lenSearch == 0) {
         return CFL_TRUE;
      } else {
         return CFL_FALSE;
      }
   } else {
      return (CFL_BOOL)hb_itemEqual(pKey, pSearch);
   }
}

static HB_ERRCODE rdd_seek(SDB_AREAP pSDBArea, HB_BOOL bSoftSeek, PHB_ITEM pSearch, HB_BOOL bFindLast) {
   SDB_INDEXP index;
   HB_ERRCODE errCode;
   HB_BYTE uiSort;

   SDB_LOG_DEBUG(("rdd_seek: alias=%s order=%d soft=%s last=%s key=%s", sdb_area_getAlias(pSDBArea), pSDBArea->uiOrder,
                  BOOL_STR(bSoftSeek), BOOL_STR(bFindLast), ITEM_STR(pSearch)));
   sdb_thread_cleanError();
   if (pSDBArea->isQuery) {
      sdb_api_genError(pSDBArea->connection, EG_UNSUPPORTED, SDB_ERROR_NOT_INDEXED, sdb_area_getAlias(pSDBArea), EF_NONE, NULL,
                       NULL, NULL);
      return HB_FAILURE;
   }

   errCode = sdb_rdd_flushRecord(pSDBArea, CFL_TRUE);
   if (errCode != HB_SUCCESS) {
      return errCode;
   }

   index = sdb_area_getCurrentOrder(pSDBArea);
   if (index != NULL) {
      pSDBArea->area.fTop = HB_FALSE;
      pSDBArea->area.fBottom = HB_FALSE;
      rdd_resetAreaFlags(pSDBArea);
      uiSort = bFindLast ? SDB_SORT_DESC : SDB_SORT_ASC;
      if (!sdb_api_tableSeek(pSDBArea, pSearch, uiSort, (CFL_BOOL)bSoftSeek)) {
         sdb_api_genErrorFromSDBError(pSDBArea->connection, sdb_area_getAlias(pSDBArea), EF_NONE, sdb_thread_getLastError(), NULL,
                                      NULL);
         return HB_FAILURE;
      }
      if (sdb_rdd_fetchNextRecord(pSDBArea, uiSort)) {
         errCode = SELF_SKIPFILTER((AREAP)pSDBArea, bFindLast ? -1 : 1);
         if (errCode != HB_FAILURE && !pSDBArea->area.fEof) {
            if (sdb_rdd_readRecord(pSDBArea, CFL_FALSE)) {
               if (bSoftSeek) {
                  pSDBArea->area.fFound = CFL_TRUE;
               } else {
                  PHB_ITEM pKeyValue = sdb_rdd_evalIndexExpr(NULL, index);
                  pSDBArea->area.fFound = sdb_rdd_keyCompare(pSearch, pKeyValue);
                  hb_itemRelease(pKeyValue);
                  if (!pSDBArea->area.fFound) {
                     errCode = SELF_GOTO((AREAP)pSDBArea, 0);
                  }
               }
            }
         } else {
            pSDBArea->area.fFound = HB_FALSE;
         }
      } else if (sdb_thread_hasError()) {
         sdb_api_genErrorFromSDBError(NULL, NULL, EF_NONE, sdb_thread_getLastError(), "SEEK", NULL);
      }
   } else {
      sdb_api_genError(pSDBArea->connection, EG_READONLY, SDB_ERROR_NOT_INDEXED, cfl_str_getPtr(pSDBArea->table->clpName),
                       EF_CANDEFAULT, NULL, NULL, NULL);
      errCode = HB_FAILURE;
   }

   return errCode;
}

static HB_ERRCODE rdd_append(SDB_AREAP pSDBArea, HB_BOOL bUnlockAll) {
   SDB_LOG_DEBUG(("rdd_append: alias=%s unlockAll=%s", sdb_area_getAlias(pSDBArea), BOOL_STR(bUnlockAll)));
   sdb_thread_cleanError();
   if (pSDBArea->isQuery) {
      sdb_api_genError(pSDBArea->connection, EG_READONLY, SDB_ERROR_READONLY, sdb_area_getAlias(pSDBArea), EF_NONE, NULL, NULL,
                       NULL);
      return HB_FAILURE;
   }

   if (pSDBArea->isReadOnly) {
      sdb_api_genError(pSDBArea->connection, EG_READONLY, SDB_ERROR_READONLY, cfl_str_getPtr(pSDBArea->table->clpName), EF_NONE,
                       NULL, NULL, NULL);
      return HB_FAILURE;
   }

   if (sdb_rdd_flushRecord(pSDBArea, CFL_TRUE) == HB_FAILURE) {
      return HB_FAILURE;
   }

   /* SHARED ACCESS and FILE NOT LOCKED*/
   if (bUnlockAll && pSDBArea->isShared) {
      sdb_rdd_unlockAllRecords(pSDBArea);
   }
   sdb_rdd_blankRecord(pSDBArea);
   pSDBArea->area.fBof = HB_FALSE;
   pSDBArea->area.fEof = HB_FALSE;
   pSDBArea->area.fFound = HB_FALSE;
   pSDBArea->isHot = CFL_TRUE;
   pSDBArea->isAppend = CFL_TRUE;
   pSDBArea->isUnpositioned = CFL_TRUE;
   pSDBArea->record->isLocked = CFL_TRUE;
   if (pSDBArea->isInsertImmediate) {
      return sdb_rdd_flushRecord(pSDBArea, CFL_TRUE);
   }
   return HB_SUCCESS;
}

static HB_ERRCODE rdd_goHot(SDB_AREAP pSDBArea) {
   SDB_LOG_DEBUG(("rdd_goHot: alias=%s", sdb_area_getAlias(pSDBArea)));
   if (pSDBArea->isReadOnly || pSDBArea->isQuery) {
      sdb_api_genError(pSDBArea->connection, EG_READONLY, SDB_ERROR_READONLY, cfl_str_getPtr(pSDBArea->table->clpName), 0, NULL,
                       NULL, NULL);
      return HB_FAILURE;
   } else if (pSDBArea->isShared && !pSDBArea->isLocked && !pSDBArea->record->isLocked) {
      sdb_api_genError(pSDBArea->connection, EG_UNLOCKED, SDB_ERROR_UNLOCKED, cfl_str_getPtr(pSDBArea->table->clpName), EF_CANRETRY,
                       NULL, NULL, NULL);
      return HB_FAILURE;
   }
   pSDBArea->isHot = CFL_TRUE;
   return HB_SUCCESS;
}

static HB_ERRCODE rdd_flush(SDB_AREAP pSDBArea) {
   ENTER_FUN_NAME("rdd_flush");
   RETURN sdb_rdd_flushRecord(pSDBArea, CFL_TRUE);
}

static HB_ERRCODE rdd_goCold(SDB_AREAP pSDBArea) {
   ENTER_FUN_NAME("rdd_goCold");
   sdb_thread_cleanError();
   SDB_LOG_DEBUG(("rdd_goCold: alias=%s is Hot=%s", sdb_area_getAlias(pSDBArea), (pSDBArea->isHot ? "true" : "false")));
   if (pSDBArea->isHot) {
      if (pSDBArea->isFlushImmediate) {
         if (pSDBArea->isAppend) {
            if (!sdb_api_insertRecord(pSDBArea)) {
               sdb_api_genError(pSDBArea->connection, EG_WRITE, SDB_ERROR_WRITE, sdb_area_getAlias(pSDBArea), EF_NONE,
                                cfl_str_getPtr(sdb_thread_getLastError()->message), NULL, NULL);
               RETURN HB_FAILURE;
            }
            pSDBArea->isAppend = CFL_FALSE;
         } else if (!sdb_api_updateRecord(pSDBArea)) {
            sdb_api_genError(pSDBArea->connection, EG_WRITE, SDB_ERROR_WRITE, sdb_area_getAlias(pSDBArea), EF_NONE,
                             cfl_str_getPtr(sdb_thread_getLastError()->message), NULL, NULL);
            RETURN HB_FAILURE;
         }
         pSDBArea->isValidBuffer = CFL_TRUE;
         pSDBArea->isHot = CFL_FALSE;
      }
      pSDBArea->isFlushImmediate = CFL_TRUE;
   }
   RETURN HB_SUCCESS;
}

static HB_ERRCODE rdd_pack(SDB_AREAP pSDBArea) {
   ENTER_FUN_NAME("rdd_pack");
   SDB_LOG_DEBUG(("table alias=%s", sdb_area_getAlias(pSDBArea)));
   sdb_thread_cleanError();
   if (pSDBArea->isQuery) {
      sdb_api_genError(pSDBArea->connection, EG_READONLY, SDB_ERROR_READONLY, sdb_area_getAlias(pSDBArea), EF_NONE, NULL, NULL,
                       NULL);
      RETURN HB_FAILURE;
   }

   if (pSDBArea->isShared) {
      sdb_api_genError(pSDBArea->connection, EG_SHARED, SDB_ERROR_SHARED, cfl_str_getPtr(pSDBArea->table->clpName), EF_NONE, NULL,
                       NULL, NULL);
      RETURN HB_FAILURE;
   }

   if (SELF_GOCOLD(&pSDBArea->area) != HB_SUCCESS) {
      RETURN HB_FAILURE;
   }

   if (!sdb_api_deleteRecords(pSDBArea, CFL_FALSE)) {
      sdb_api_genErrorFromSDBError(pSDBArea->connection, sdb_area_getAlias(pSDBArea), EF_NONE, sdb_thread_getLastError(), NULL,
                                   NULL);
      RETURN HB_FAILURE;
   }

   RETURN SELF_GOTOP((AREAP)pSDBArea);
}

static HB_ERRCODE rdd_zap(SDB_AREAP pSDBArea) {
   ENTER_FUN_NAME("rdd_zap");
   sdb_thread_cleanError();
   if (pSDBArea->isQuery) {
      sdb_api_genError(pSDBArea->connection, EG_READONLY, SDB_ERROR_READONLY, sdb_area_getAlias(pSDBArea), EF_NONE, NULL, NULL,
                       NULL);
      RETURN HB_FAILURE;
   }

   if (pSDBArea->isReadOnly) {
      sdb_api_genError(pSDBArea->connection, EG_READONLY, SDB_ERROR_READONLY, cfl_str_getPtr(pSDBArea->table->clpName), 0, NULL,
                       NULL, NULL);
      RETURN HB_FAILURE;
   }

   if (pSDBArea->isShared) {
      sdb_api_genError(pSDBArea->connection, EG_SHARED, SDB_ERROR_SHARED, cfl_str_getPtr(pSDBArea->table->clpName), 0, NULL, NULL,
                       NULL);
      RETURN HB_FAILURE;
   }

   if (sdb_rdd_flushRecord(pSDBArea, CFL_TRUE) == HB_FAILURE) {
      RETURN HB_FAILURE;
   }

   if (!sdb_api_deleteRecords(pSDBArea, CFL_TRUE)) {
      sdb_api_genErrorFromSDBError(pSDBArea->connection, sdb_area_getAlias(pSDBArea), EF_NONE, sdb_thread_getLastError(), NULL,
                                   NULL);
      RETURN HB_FAILURE;
   }

   /* move to 0 recno */
   SELF_GOTO((AREAP)pSDBArea, 0);

   RETURN HB_SUCCESS;
}

static HB_ERRCODE rdd_orderListDelete(SDB_AREAP pSDBArea, LPDBORDERINFO pOrdInfo) {
   HB_ERRCODE errCode;
   HB_INT32 indexPos;

   ENTER_FUN_NAME("rdd_orderListDelete");
   sdb_thread_cleanError();
   errCode = sdb_rdd_flushRecord(pSDBArea, CFL_TRUE);
   if (errCode != HB_SUCCESS) {
      RETURN errCode;
   }
   if (sdb_util_itemIsNull(pOrdInfo->itmOrder)) {
      indexPos = sdb_rdd_getIndexPosByName(pSDBArea->orders, hb_itemGetCPtr(pOrdInfo->atomBagName));
   } else {
      indexPos = sdb_rdd_getIndexPosByName(pSDBArea->orders, hb_itemGetCPtr(pOrdInfo->itmOrder));
   }
   if (indexPos >= 0) {
      if (indexPos < pSDBArea->uiOrder - 1) {
         --pSDBArea->uiOrder;
      } else if (indexPos == pSDBArea->uiOrder - 1) {
         pSDBArea->uiOrder = 0;
      }
      cfl_list_del(pSDBArea->orders, indexPos);
   }
   RETURN HB_SUCCESS;
}

static SDB_TABLEP sdb_rdd_createTable(SDB_AREAP pSDBArea, SDB_SCHEMAP schema, const char *tableName, CFL_BOOL bRightPadded) {
   SDB_TABLEP table = sdb_table_new(schema, tableName, (CFL_UINT32)strlen(tableName), NULL, 0);
   HB_USHORT uiCount;
   CFL_UINT16 errSubCode = 0;
   SDB_FIELDP tableField;

   ENTER_FUN_NAME("sdb_rdd_createTable");
   for (uiCount = 0; uiCount < pSDBArea->area.uiFieldCount; uiCount++) {
      LPFIELD pField = pSDBArea->area.lpFields + uiCount;
      const char *fieldName = hb_dynsymName((PHB_DYNS)pField->sym);

      tableField = NULL;
      switch (pField->uiType) {
      case HB_FT_STRING:
         tableField = sdb_field_new(fieldName, (CFL_UINT32)strlen(fieldName), NULL, 0, SDB_FIELD_DATA, SDB_CLP_CHARACTER,
                                    pField->uiLen, 0, bRightPadded);
         break;

      case HB_FT_LOGICAL:
         tableField =
             sdb_field_new(fieldName, (CFL_UINT32)strlen(fieldName), NULL, 0, SDB_FIELD_DATA, SDB_CLP_LOGICAL, 1, 0, bRightPadded);
         break;

      case HB_FT_MEMO:
         switch (sdb_api_getDefaultMemoType()) {
         case SDB_CLP_MEMO_LONG:
            tableField = sdb_field_new(fieldName, (CFL_UINT32)strlen(fieldName), NULL, 0, SDB_FIELD_DATA, SDB_CLP_MEMO_LONG, 10, 0,
                                       bRightPadded);
            break;
         case SDB_CLP_CLOB:
            tableField =
                sdb_field_new(fieldName, (CFL_UINT32)strlen(fieldName), NULL, 0, SDB_FIELD_DATA, SDB_CLP_CLOB, 0, 0, bRightPadded);
            break;
         case SDB_CLP_BLOB:
            tableField =
                sdb_field_new(fieldName, (CFL_UINT32)strlen(fieldName), NULL, 0, SDB_FIELD_DATA, SDB_CLP_BLOB, 0, 0, bRightPadded);
            break;
         default:
            errSubCode = SDB_ERROR_INVALID_DATATYPE;
         }
         break;

      case HB_FT_DATE:
         tableField =
             sdb_field_new(fieldName, (CFL_UINT32)strlen(fieldName), NULL, 0, SDB_FIELD_DATA, SDB_CLP_DATE, 0, 0, bRightPadded);
         break;

      case HB_FT_LONG:
         tableField = sdb_field_new(fieldName, (CFL_UINT32)strlen(fieldName), NULL, 0, SDB_FIELD_DATA, SDB_CLP_NUMERIC,
                                    (CFL_UINT32)pField->uiLen, (CFL_UINT8)pField->uiDec, bRightPadded);
         break;

      case HB_FT_DOUBLE:
         tableField =
             sdb_field_new(fieldName, (CFL_UINT32)strlen(fieldName), NULL, 0, SDB_FIELD_DATA, SDB_CLP_DOUBLE, 0, 0, bRightPadded);
         break;

      case HB_FT_INTEGER:
         tableField =
             sdb_field_new(fieldName, (CFL_UINT32)strlen(fieldName), NULL, 0, SDB_FIELD_DATA, SDB_CLP_INTEGER, 0, 0, bRightPadded);
         break;
#ifdef __HBR__
      case HB_FT_CURDOUBLE:
      case HB_FT_CURRENCY:
         tableField =
             sdb_field_new(fieldName, (CFL_UINT32)strlen(fieldName), NULL, 0, SDB_FIELD_DATA, SDB_CLP_DOUBLE, 0, 0, bRightPadded);
         break;

      case HB_FT_BLOB:
         tableField =
             sdb_field_new(fieldName, (CFL_UINT32)strlen(fieldName), NULL, 0, SDB_FIELD_DATA, SDB_CLP_BLOB, 0, 0, bRightPadded);
         break;

      case HB_FT_IMAGE:
         tableField =
             sdb_field_new(fieldName, (CFL_UINT32)strlen(fieldName), NULL, 0, SDB_FIELD_DATA, SDB_CLP_IMAGE, 0, 0, bRightPadded);
         break;

      case HB_FT_FLOAT:
         tableField =
             sdb_field_new(fieldName, (CFL_UINT32)strlen(fieldName), NULL, 0, SDB_FIELD_DATA, SDB_CLP_FLOAT, 0, 0, bRightPadded);
         break;

      case HB_FT_TIMESTAMP:
         tableField = sdb_field_new(fieldName, (CFL_UINT32)strlen(fieldName), NULL, 0, SDB_FIELD_DATA, SDB_CLP_TIMESTAMP, 0, 0,
                                    bRightPadded);
         break;

      case HB_FT_OLE:
      case HB_FT_ANY:
      case HB_FT_ROWVER:
      case HB_FT_VARLENGTH:
      case HB_FT_AUTOINC:
      case HB_FT_TIME:
      case HB_FT_MODTIME:
         errSubCode = SDB_ERROR_INVALID_DATATYPE;
         break;
#endif
      default:
         errSubCode = SDB_ERROR_INVALID_DATATYPE;
      }
      if (errSubCode != SDB_ERROR_INVALID_DATATYPE) {
         sdb_table_addField(table, tableField);
      } else {
         sdb_api_genError(pSDBArea->connection, EG_CREATE, errSubCode, tableName, 0, NULL, NULL, NULL);
      }
   }
   tableField =
       sdb_field_new(cfl_str_getPtr(pSDBArea->connection->defaultPKName), cfl_str_getLength(pSDBArea->connection->defaultPKName),
                     NULL, 0, SDB_FIELD_PK, SDB_CLP_BIGINT, 20, 0, bRightPadded);
   tableField->dbExpression =
       !cfl_str_isEmpty(pSDBArea->connection->pkDefaultExpr) ? cfl_str_newStr(pSDBArea->connection->pkDefaultExpr) : NULL;
   tableField->setMode = SET_MODE_ALL_SERVER;
   sdb_table_addField(table, tableField);
   tableField =
       sdb_field_new(cfl_str_getPtr(pSDBArea->connection->defaultDelName), cfl_str_getLength(pSDBArea->connection->defaultDelName),
                     NULL, 0, SDB_FIELD_DEL_FLAG, SDB_CLP_LOGICAL, 1, 0, bRightPadded);
   sdb_table_addField(table, tableField);
   RETURN table;
}

static HB_ERRCODE rdd_create(SDB_AREAP pSDBArea, LPDBOPENINFO pOpenInfo) {
   SDB_TABLEP pTable;
   PHB_FNAME pFileName;
   SDB_CONNECTIONP connection;
   char *szSchema;
   SDB_SCHEMAP schema;
   HB_ERRCODE errCode = HB_SUCCESS;
   SDB_THREAD_DATAP thData;
   PHB_ITEM pError;

   ENTER_FUN_NAME("rdd_create");
   sdb_thread_cleanError();
   thData = sdb_thread_getData();
// #if __HARBOUR__ >= 0x030200 || __XHARBOUR__ >= 0x0123
#ifdef __HBR__
   if (pOpenInfo->ulConnection == 0) {
      connection = thData->connection;
   } else {
      connection = sdb_api_getConnection((CFL_UINT16)pOpenInfo->ulConnection);
   }
#else
   connection = thData->connection;
#endif
   pSDBArea->connection = connection;

   pFileName = hb_fsFNameSplit(pOpenInfo->abName);
   /* When there is no ALIAS we will create new one using file name */
   if (pOpenInfo->atomAlias == NULL) {
      pOpenInfo->atomAlias = hb_strdup(pFileName->szName);
   }

   szSchema = sdb_util_getSchemaName(connection, pFileName->szPath);
   pTable = sdb_api_getTable(connection, szSchema, pFileName->szName);

   if (pTable != NULL) {
      SDB_MEM_FREE(szSchema);
      sdb_api_genError(connection, EG_CREATE, SDB_ERROR_CREATE_TABLE, pFileName->szName, 0, "Table already exists", NULL, NULL);
      hb_xfree(pFileName);
      RETURN HB_FAILURE;
   }

   schema = sdb_database_getCreateSchema(connection->database, szSchema);
   pTable = sdb_rdd_createTable(pSDBArea, schema, pFileName->szName, CFL_TRUE);
   pError = NULL;
   do {
      if (sdb_api_createTable(connection, szSchema, pTable)) {
         pSDBArea->table = pTable;
         if (addFieldsToWorkarea(pSDBArea, CFL_FALSE)) {
            pSDBArea->record = sdb_record_new(pSDBArea);
            break;
         }
      } else if (pError == NULL) {
         pError = hb_errNew();
      }
   } while (sdb_api_genErrorFromSDBError(connection, pOpenInfo->abName, EF_CANRETRY | EF_CANDEFAULT, sdb_thread_getLastError(),
                                         NULL, &pError) == E_RETRY);

   hb_xfree(pFileName);
   SDB_MEM_FREE(szSchema);

   if (pError) {
      hb_itemRelease(pError);
   }

   if (pSDBArea->table == NULL) {
      return HB_FAILURE;
   }

   if (errCode == HB_SUCCESS) {
#ifdef __XHB__
      sdb_api_registerArea(pSDBArea);
#else
      errCode = SUPER_CREATE((AREAP)pSDBArea, pOpenInfo);
      if (errCode == HB_SUCCESS) {
         sdb_api_registerArea(pSDBArea);
         errCode = SELF_GOTOP((AREAP)pSDBArea);
      }
#endif
   }
   if (errCode != HB_SUCCESS) {
      SELF_CLOSE((AREAP)pSDBArea);
   }
   RETURN errCode;
}

static HB_ERRCODE rdd_deleteRec(SDB_AREAP pSDBArea) {
   ENTER_FUN_NAME("rdd_deleteRec");

   SDB_LOG_DEBUG(("rdd_deleteRec: alias=%s", sdb_area_getAlias(pSDBArea)));
   sdb_thread_cleanError();
   if (pSDBArea->isQuery) {
      sdb_api_genError(pSDBArea->connection, EG_READONLY, SDB_ERROR_READONLY, sdb_area_getAlias(pSDBArea), EF_NONE, NULL, NULL,
                       NULL);
      RETURN HB_FAILURE;
   }

   if (pSDBArea->isReadOnly) {
      sdb_api_genError(pSDBArea->connection, EG_READONLY, SDB_ERROR_READONLY, cfl_str_getPtr(pSDBArea->table->clpName), 0, NULL,
                       NULL, NULL);
      RETURN HB_FAILURE;
   }

   if (!pSDBArea->isValidBuffer && !sdb_rdd_readRecord(pSDBArea, CFL_FALSE)) {
      RETURN HB_FAILURE;
   }

   if (!pSDBArea->area.fEof) {
      if (!pSDBArea->isHot && SELF_GOHOT((AREAP)pSDBArea) == HB_FAILURE) {
         RETURN HB_FAILURE;
      }
      sdb_record_setLogical(pSDBArea->record, pSDBArea->table->delField, CFL_TRUE);
   }
   RETURN HB_SUCCESS;
}

static HB_ERRCODE rdd_deleted(SDB_AREAP pSDBArea, HB_BOOL *bDeleted) {
   ENTER_FUN_NAME("rdd_deleted");

   SDB_LOG_DEBUG(("rdd_deleted: alias=%s", sdb_area_getAlias(pSDBArea)));
   sdb_thread_cleanError();

   if (pSDBArea->isQuery) {
      *bDeleted = CFL_FALSE;
      RETURN HB_SUCCESS;
   }

   if (!pSDBArea->isValidBuffer && !sdb_rdd_readRecord(pSDBArea, CFL_FALSE)) {
      RETURN HB_FAILURE;
   }

   *bDeleted = (HB_BOOL)sdb_record_getLogical(pSDBArea->record, pSDBArea->table->delField);
   RETURN HB_SUCCESS;
}

static HB_ERRCODE rdd_getValue(SDB_AREAP pSDBArea, HB_USHORT uiField, PHB_ITEM pItem) {
   HB_ERRCODE errCode = HB_SUCCESS;
   SDB_WAFIELDP waField;

   ENTER_FUN_NAME("rdd_getValue");
   SDB_LOG_DEBUG(("rdd_getValue: alias=%s field=%u", sdb_area_getAlias(pSDBArea), uiField));
   sdb_thread_cleanError();

   if (!pSDBArea->isValidBuffer && !sdb_rdd_readRecord(pSDBArea, CFL_FALSE)) {
      RETURN HB_FAILURE;
   }

   if (pSDBArea->lockControl == SDB_LOCK_AUTO_GET && !pSDBArea->isQuery && (pSDBArea->isLocked || !pSDBArea->isShared)) {
      SDB_RECNO ulRecno = (SDB_RECNO)sdb_record_getInt64(pSDBArea->record, pSDBArea->table->pkField);
      if (sdb_rdd_rawLock(pSDBArea, REC_LOCK, ulRecno) != HB_SUCCESS) {
         sdb_api_genError(pSDBArea->connection, EG_LOCK, SDB_ERROR_LOCKING, sdb_area_getAlias(pSDBArea), EF_NONE,
                          "Unable to lock record", NULL, NULL);
         RETURN HB_FAILURE;
      }
   }

   if (uiField > pSDBArea->area.uiFieldCount) {
      RETURN HB_FAILURE;
   }

   waField = sdb_area_getFieldByPos(pSDBArea, (CFL_UINT32)uiField - 1);
   if (waField != NULL) {
      switch (waField->field->clpType) {
      case SDB_CLP_BLOB:
      case SDB_CLP_CLOB:
      case SDB_CLP_IMAGE:
      case SDB_CLP_LONG_RAW:
      case SDB_CLP_MEMO_LONG:
         if (sdb_record_getValue(pSDBArea->record, waField->field, pItem, CFL_TRUE) == HB_SUCCESS) {
            if (HB_IS_NUMERIC(pItem)) {
               if (hb_itemGetNI(pItem) > 0) {
                  if (!sdb_api_areaGetValue(pSDBArea, waField->queryPos, waField->field, CFL_TRUE)) {
                     sdb_api_genErrorFromSDBError(pSDBArea->connection, sdb_area_getAlias(pSDBArea), EF_NONE,
                                                  sdb_thread_getLastError(), cfl_str_getPtr(waField->field->clpName), NULL);
                     RETURN HB_FAILURE;
                  }
               } else {
                  hb_itemPutCL(sdb_record_getItem(pSDBArea->record, waField->field), "", 0);
               }
               errCode = sdb_record_getValue(pSDBArea->record, waField->field, pItem, CFL_TRUE);
               if (errCode != HB_SUCCESS) {
                  sdb_api_genError(pSDBArea->connection, EG_READ, SDB_ERROR_SET_FIELD_MEM, sdb_area_getAlias(pSDBArea), EF_NONE,
                                   NULL, cfl_str_getPtr(waField->field->clpName), NULL);
               }
            }
         } else {
            sdb_api_genError(pSDBArea->connection, EG_READ, SDB_ERROR_GET_FIELD_MEM, sdb_area_getAlias(pSDBArea), EF_NONE, NULL,
                             cfl_str_getPtr(waField->field->clpName), NULL);
         }
         break;

      default:
         if (sdb_record_getValue(pSDBArea->record, waField->field, pItem, CFL_TRUE) != HB_SUCCESS) {
            sdb_api_genError(pSDBArea->connection, EG_READ, SDB_ERROR_GET_FIELD_MEM, sdb_area_getAlias(pSDBArea), EF_NONE, NULL,
                             cfl_str_getPtr(waField->field->clpName), NULL);
         }
         break;
      }
   } else if (pSDBArea->isQuery) {
      sdb_api_genError(pSDBArea->connection, EG_NOVAR, SDB_ERROR_INVALID_FIELD, sdb_area_getAlias(pSDBArea), EF_NONE, NULL,
                       cfl_str_getPtr(waField->field->clpName), NULL);
      errCode = HB_FAILURE;
   } else {
      sdb_api_genError(pSDBArea->connection, EG_NOVAR, SDB_ERROR_INVALID_FIELD, cfl_str_getPtr(pSDBArea->table->clpName), EF_NONE,
                       NULL, cfl_str_getPtr(waField->field->clpName), NULL);
      errCode = HB_FAILURE;
   }
   RETURN errCode;
}

static HB_ERRCODE rdd_getVarLen(SDB_AREAP pSDBArea, HB_USHORT uiIndex, HB_UINT32 *ulLen) {
   SDB_WAFIELDP waField;

   ENTER_FUN_NAME("rdd_getVarLen");
   sdb_thread_cleanError();

   waField = sdb_area_getFieldByPos(pSDBArea, uiIndex - 1);
   if (waField != NULL) {
      *ulLen = pSDBArea->area.lpFields[uiIndex - 1].uiLen;
      RETURN HB_SUCCESS;
   }
   RETURN HB_FAILURE;
}

static HB_ERRCODE rdd_putValue(SDB_AREAP pSDBArea, HB_USHORT uiIndex, PHB_ITEM pItem) {
   SDB_WAFIELDP waField;
   HB_ERRCODE errCode;

   ENTER_FUN_NAME("rdd_putValue");
   SDB_LOG_DEBUG(("rdd_putValue: pos=%u, value=%s", uiIndex, ITEM_STR(pItem)));
   sdb_thread_cleanError();

   if (pSDBArea->isQuery) {
      sdb_api_genError(pSDBArea->connection, EG_READONLY, SDB_ERROR_READONLY, sdb_area_getAlias(pSDBArea), EF_NONE, NULL, NULL,
                       NULL);
      RETURN HB_FAILURE;
   }

   if (pSDBArea->area.fEof) {
      RETURN HB_SUCCESS;
   }

   if (!pSDBArea->isValidBuffer && !sdb_rdd_readRecord(pSDBArea, CFL_FALSE)) {
      RETURN HB_FAILURE;
   }

   if (pSDBArea->lockControl == SDB_LOCK_AUTO_PUT && !pSDBArea->isQuery && (pSDBArea->isLocked || !pSDBArea->isShared)) {
      SDB_RECNO ulRecno = (SDB_RECNO)sdb_record_getInt64(pSDBArea->record, pSDBArea->table->pkField);
      if (sdb_rdd_rawLock(pSDBArea, REC_LOCK, ulRecno) != HB_SUCCESS) {
         sdb_api_genError(pSDBArea->connection, EG_LOCK, SDB_ERROR_LOCKING, sdb_area_getAlias(pSDBArea), EF_NONE,
                          "Unable to lock record", NULL, NULL);
         RETURN HB_FAILURE;
      }
   }

   waField = sdb_area_getFieldByPos(pSDBArea, uiIndex - 1);
   if (waField != NULL && IS_DATA_FIELD(waField->field)) {
      if (pSDBArea->isAppend) {
         if (IS_SERVER_SET_INSERT(waField->field)) {
            sdb_api_genError(pSDBArea->connection, EG_READONLY, SDB_ERROR_READONLY, cfl_str_getPtr(pSDBArea->table->clpName),
                             EF_CANDEFAULT, "Field set by server in insert", cfl_str_getPtr(waField->field->clpName), NULL);
            RETURN HB_FAILURE;
         }
      } else if (IS_SERVER_SET_UPDATE(waField->field)) {
         sdb_api_genError(pSDBArea->connection, EG_READONLY, SDB_ERROR_READONLY, cfl_str_getPtr(pSDBArea->table->clpName),
                          EF_CANDEFAULT, "Field set by server in update", cfl_str_getPtr(waField->field->clpName), NULL);
         RETURN HB_FAILURE;
      }
      if (waField->isContext) {
         sdb_api_genError(pSDBArea->connection, EG_READONLY, SDB_ERROR_READONLY, cfl_str_getPtr(pSDBArea->table->clpName),
                          EF_CANDEFAULT, "Context fields can't be updated", cfl_str_getPtr(waField->field->clpName), NULL);
         RETURN HB_FAILURE;
      }

      if (!pSDBArea->isHot && SELF_GOHOT((AREAP)pSDBArea) == HB_FAILURE) {
         RETURN HB_FAILURE;
      }

      if (pSDBArea->keyValue == NULL && pSDBArea->uiOrder > 0) {
         pSDBArea->keyValue = sdb_rdd_evalIndexExpr(NULL, sdb_area_getCurrentOrder(pSDBArea));
      }
      errCode = sdb_record_setValue(pSDBArea->record, waField->field, pItem);
      if (errCode == HB_SUCCESS) {
         waField->isChanged = CFL_TRUE;
         pSDBArea->record->isChanged = CFL_TRUE;
         RETURN HB_SUCCESS;
      } else {
         PHB_ITEM pError = hb_errNew();
         hb_errPutArgs(pError, 1, pItem);
         sdb_api_genError(pSDBArea->connection, (CFL_UINT16)errCode, SDB_ERROR_SET_FIELD_MEM,
                          cfl_str_getPtr(pSDBArea->table->clpName), EF_CANDEFAULT, NULL, cfl_str_getPtr(waField->field->clpName),
                          &pError);
         hb_itemRelease(pError);
         RETURN HB_FAILURE;
      }
   }
   RETURN HB_FAILURE;
}

static HB_ERRCODE rdd_recall(SDB_AREAP pSDBArea) {
   ENTER_FUN_NAME("rdd_recall");
   sdb_thread_cleanError();

   SDB_LOG_DEBUG(("rdd_recall: alias=%s", sdb_area_getAlias(pSDBArea)));
   if (pSDBArea->isQuery) {
      sdb_api_genError(pSDBArea->connection, EG_READONLY, SDB_ERROR_READONLY, sdb_area_getAlias(pSDBArea), EF_NONE, NULL, NULL,
                       NULL);
      RETURN HB_FAILURE;
   }

   if (pSDBArea->isReadOnly) {
      sdb_api_genError(pSDBArea->connection, EG_READONLY, SDB_ERROR_READONLY, cfl_str_getPtr(pSDBArea->table->clpName), 0, NULL,
                       NULL, NULL);
      RETURN HB_FAILURE;
   }

   if (!pSDBArea->isValidBuffer && !sdb_rdd_readRecord(pSDBArea, CFL_FALSE)) {
      RETURN HB_FAILURE;
   }

   if (!pSDBArea->area.fEof) {
      if (!pSDBArea->isHot && SELF_GOHOT((AREAP)pSDBArea) == HB_FAILURE) {
         RETURN HB_FAILURE;
      }
      sdb_record_setLogical(pSDBArea->record, pSDBArea->table->delField, CFL_FALSE);
   }
   RETURN HB_SUCCESS;
}

static HB_ERRCODE rdd_recCount(SDB_AREAP pSDBArea, HB_RECNO *recCount) {
   SDB_RECNO ulMaxRecno;
   ENTER_FUN_NAME("rdd_recCount");
   sdb_thread_cleanError();
   ulMaxRecno = sdb_rdd_maxRecno(pSDBArea);
   *recCount = (HB_RECNO)ulMaxRecno;
   if (sdb_thread_hasError()) {
      sdb_api_genErrorFromSDBError(pSDBArea->connection, sdb_area_getAlias(pSDBArea), EF_NONE, sdb_thread_getLastError(), NULL,
                                   NULL);
      RETURN HB_FAILURE;
   } else {
      RETURN HB_SUCCESS;
   }
}

static HB_ERRCODE rdd_recInfo(SDB_AREAP pSDBArea, PHB_ITEM pRecID, HB_USHORT uiInfoType, PHB_ITEM pInfo) {
   PHB_ITEM pRecno = hb_itemNew(pRecID);
   SDB_RECNO ulRecno;
   HB_ERRCODE errResult = HB_SUCCESS;
   HB_BOOL bDeleted;
   SDB_RECNO ulPrevRec = 0;

   SDB_LOG_DEBUG(("rdd_recInfo: alias=%s", sdb_area_getAlias(pSDBArea)));
   sdb_thread_cleanError();

   if (pSDBArea->isQuery) {
      switch (uiInfoType) {
      case DBRI_RECSIZE:
         hb_itemPutNL(pInfo, pSDBArea->uiRecordLen);
         break;
      default:
         hb_itemPutL(pInfo, CFL_FALSE);
         break;
      }
      return HB_SUCCESS;
   }

   if (!pSDBArea->isValidBuffer && !sdb_rdd_readRecord(pSDBArea, CFL_FALSE)) {
      return HB_FAILURE;
   }

   ulRecno = (SDB_RECNO)hb_itemGetNLL(pRecno);
   if (ulRecno == 0) {
#ifdef __XHB__
      SELF_RECNO((AREAP)pSDBArea, pRecno);
#else
      SELF_RECID((AREAP)pSDBArea, pRecno);
#endif
      ulRecno = (SDB_RECNO)hb_itemGetNLL(pRecno);
   } else if (ulRecno != (SDB_RECNO)sdb_record_getInt64(pSDBArea->record, pSDBArea->table->pkField)) {
      switch (uiInfoType) {
      case DBRI_DELETED: {
         ulPrevRec = (SDB_RECNO)sdb_record_getInt64(pSDBArea->record, pSDBArea->table->pkField);
         errResult = SELF_GOTOID((AREAP)pSDBArea, pRecno);
         if (errResult != HB_SUCCESS) {
            hb_itemRelease(pRecno);
            return errResult;
         }
      } break;
      }
   }

   if (ulRecno > 0) {
      switch (uiInfoType) {
      case DBRI_DELETED:
         errResult = SELF_DELETED((AREAP)pSDBArea, &bDeleted);
         if (errResult == HB_SUCCESS) {
            hb_itemPutL(pInfo, bDeleted);
         }
         break;

      case DBRI_LOCKED:
         /* Clipper also checks only fShared and RLOCK and ignore FLOCK */
         hb_itemPutL(pInfo, !pSDBArea->isShared ||
                                /* pSDBArea->area.fFLocked || */ sdb_rdd_isRecordLocked(pSDBArea, ulRecno));
         break;

      case DBRI_RECSIZE:
         hb_itemPutNL(pInfo, pSDBArea->uiRecordLen);
         break;

      case DBRI_RECNO:
         hb_itemCopy(pInfo, pRecno);
         break;

      case DBRI_UPDATED:
         hb_itemPutL(pInfo,
                     ulRecno == (SDB_RECNO)sdb_record_getInt64(pSDBArea->record, pSDBArea->table->pkField) && pSDBArea->isHot);
         break;

      case DBRI_ENCRYPTED:
         hb_itemPutL(pInfo, CFL_FALSE);
         break;

      default:
         errResult = SUPER_RECINFO((AREAP)pSDBArea, pRecID, uiInfoType, pInfo);
      }
   }
   if (ulPrevRec != 0) {
      hb_itemPutNLL(pRecno, (HB_LONGLONG)ulPrevRec);
      if (SELF_GOTOID((AREAP)pSDBArea, pRecno) != HB_SUCCESS) {
         errResult = HB_FAILURE;
      }
   }
   hb_itemRelease(pRecno);
   return errResult;
}

#ifdef __XHB__

static HB_ERRCODE rdd_recno(SDB_AREAP pSDBArea, PHB_ITEM pRecno) {
   SDB_LOG_DEBUG(("rdd_recno. valid_buffer=%s", (pSDBArea->isValidBuffer ? "true" : "false")));
   sdb_thread_cleanError();

   if (pSDBArea->record == NULL) {
      return HB_FAILURE;
   } else if (pSDBArea->isQuery) {
      sdb_util_itemPutRecno(pRecno, pSDBArea->record->ulRowNum);
      return HB_SUCCESS;
   } else if (pSDBArea->isAppend && !pSDBArea->isInsertImmediate) {
      if (!sdb_api_insertRecord(pSDBArea)) {
         sdb_api_genError(pSDBArea->connection, EG_WRITE, SDB_ERROR_WRITE, sdb_area_getAlias(pSDBArea), EF_NONE,
                          cfl_str_getPtr(sdb_thread_getLastError()->message), NULL, NULL);
         return HB_FAILURE;
      }
      pSDBArea->isAppend = CFL_FALSE;
   } else if (!pSDBArea->isValidBuffer && !sdb_rdd_readRecord(pSDBArea, CFL_FALSE)) {
      return HB_FAILURE;
   }

   sdb_record_getValue(pSDBArea->record, pSDBArea->table->pkField, pRecno, CFL_TRUE);
   return HB_SUCCESS;
}

#else

static HB_ERRCODE rdd_recId(SDB_AREAP pSDBArea, PHB_ITEM pRecno) {
   SDB_LOG_DEBUG(("rdd_recId. valid_buffer=%s", (pSDBArea->isValidBuffer ? "true" : "false")));
   sdb_thread_cleanError();

   if (pSDBArea->record == NULL) {
      return HB_FAILURE;
   } else if (pSDBArea->isQuery) {
      sdb_util_itemPutRecno(pRecno, pSDBArea->record->ulRowNum);
      return HB_SUCCESS;
   } else if (pSDBArea->isAppend && !pSDBArea->isInsertImmediate) {
      if (!sdb_api_insertRecord(pSDBArea)) {
         sdb_api_genError(pSDBArea->connection, EG_WRITE, SDB_ERROR_WRITE, sdb_area_getAlias(pSDBArea), EF_NONE,
                          cfl_str_getPtr(sdb_thread_getLastError()->message), NULL, NULL);
         return HB_FAILURE;
      }
      pSDBArea->isAppend = CFL_FALSE;
   } else if (!pSDBArea->isValidBuffer && !sdb_rdd_readRecord(pSDBArea, CFL_FALSE)) {
      return HB_FAILURE;
   }
   sdb_record_getValue(pSDBArea->record, pSDBArea->table->pkField, pRecno, CFL_TRUE);
   return HB_SUCCESS;
}

static HB_ERRCODE rdd_recno(SDB_AREAP pSDBArea, HB_RECNO *ulRecno) {
   PHB_ITEM pRecno;
   HB_ERRCODE errResult;
   ENTER_FUN_NAME("rdd_recno");
   pRecno = hb_itemNew(NULL);
   errResult = SELF_RECID((AREAP)pSDBArea, pRecno);
   *ulRecno = (HB_RECNO)hb_itemGetNLL(pRecno);
   hb_itemRelease(pRecno);
   RETURN errResult;
}

#endif

static HB_ERRCODE rdd_orderCreate(SDB_AREAP pSDBArea, LPDBORDERCREATEINFO pOrderInfo) {
   SDB_INDEXP index;
   HB_ERRCODE errCode;
   PHB_ITEM pResult;
   const char *szKey;
   PHB_ITEM pKeyExp;
   HB_BYTE bType;
   CFL_STRP indexName;
   PHB_ITEM pRecno;

   ENTER_FUN_NAME("rdd_orderCreate");
   sdb_thread_cleanError();

   if (pSDBArea->isQuery) {
      sdb_api_genError(pSDBArea->connection, EG_READONLY, SDB_ERROR_READONLY, sdb_area_getAlias(pSDBArea), EF_NONE, NULL, NULL,
                       NULL);
      RETURN HB_FAILURE;
   }

   if (pSDBArea->isShared) {
      sdb_api_genError(pSDBArea->connection, EG_SHARED, SDB_ERROR_CREATE_INDEX,
                       "Table should be open in exclusive mode to perform this operation", EF_NONE, NULL, NULL, NULL);
      RETURN HB_FAILURE;
   }

   errCode = sdb_rdd_flushRecord(pSDBArea, CFL_TRUE);
   if (errCode != HB_SUCCESS) {
      RETURN errCode;
   }

   if (pOrderInfo->abBagName) {
      indexName = cfl_str_toUpper(cfl_str_trim(cfl_str_newBuffer(pOrderInfo->abBagName)));
   } else {
      indexName = cfl_str_toUpper(cfl_str_trim(cfl_str_newBuffer(pOrderInfo->atomBagName)));
   }

   index = sdb_rdd_getIndexByName(pSDBArea->table->indexes, cfl_str_getPtr(indexName));
   if (index != NULL) {
      sdb_api_genError(pSDBArea->connection, EG_CREATE, SDB_ERROR_CREATE_INDEX, cfl_str_getPtr(indexName), 0,
                       "Index already exists", NULL, NULL);
      cfl_str_free(indexName);
      RETURN HB_FAILURE;
   }

   szKey = hb_itemGetCPtr(pOrderInfo->abExpr);
   /* If we have a codeblock for the expression, use it */
   if (pOrderInfo->itmCobExpr != NULL) {
      pKeyExp = hb_itemNew(pOrderInfo->itmCobExpr);
   } else {
      errCode = SELF_COMPILE((AREAP)pSDBArea, szKey);
      if (errCode != HB_SUCCESS) {
         cfl_str_free(indexName);
         RETURN errCode;
      }
      pKeyExp = pSDBArea->area.valResult;
      pSDBArea->area.valResult = NULL;
   }

   /* Get a blank record before testing expression */
   pRecno = hb_itemNew(NULL);
#ifdef __XHB__
   SELF_RECNO((AREAP)pSDBArea, pRecno);
#else
   SELF_RECID((AREAP)pSDBArea, pRecno);
#endif
   errCode = SELF_GOTO((AREAP)pSDBArea, 0);
   if (errCode != HB_SUCCESS) {
      cfl_str_free(indexName);
      RETURN errCode;
   }
   errCode = SELF_EVALBLOCK((AREAP)pSDBArea, pKeyExp);
   if (errCode != HB_SUCCESS) {
      hb_vmDestroyBlockOrMacro(pKeyExp);
      SELF_GOTOID((AREAP)pSDBArea, pRecno);
      cfl_str_free(indexName);
      hb_itemRelease(pRecno);
      RETURN errCode;
   }
   pResult = pSDBArea->area.valResult;
   pSDBArea->area.valResult = NULL;
   bType = hb_itemTypeStr(pResult)[0];

   /* Make sure KEY has proper type and uiLen is not 0 */
   if (bType == 'U' || sdb_util_itemLen(pResult, CFL_FALSE) == 0) {
      cfl_str_free(indexName);
      hb_itemRelease(pResult);
      hb_vmDestroyBlockOrMacro(pKeyExp);
      SELF_GOTOID((AREAP)pSDBArea, pRecno);
      sdb_api_genError(pSDBArea->connection, bType == 'U' ? EG_DATATYPE : EG_DATAWIDTH, SDB_ERROR_INVALID_KEY,
                       cfl_str_getPtr(pSDBArea->table->clpName), 0, NULL, NULL, NULL);
      RETURN HB_FAILURE;
   }
   hb_itemRelease(pRecno);

   index = sdb_api_createIndex(pSDBArea->connection, pSDBArea->table, cfl_str_getPtr(indexName), hb_itemGetCPtr(pOrderInfo->abExpr),
                               pResult, NULL, CFL_FALSE, NULL);

   if (index != NULL) {
      if (sdb_area_getField(pSDBArea, cfl_str_getPtr(index->field->clpName)) == NULL) {
         sdb_area_addField(pSDBArea, index->field);
      }
      SELF_ORDLSTCLEAR((AREAP)pSDBArea);
      index->compiledExpr = hb_itemNew(pKeyExp);
      pSDBArea->uiOrder = 1;
      sdb_record_free(pSDBArea->record);
      pSDBArea->record = sdb_record_new(pSDBArea);
      cfl_list_add(pSDBArea->orders, index);
      hb_itemRelease(pSDBArea->keyValue);
      pSDBArea->keyValue = NULL;
      hb_vmDestroyBlockOrMacro(pKeyExp);
      hb_itemRelease(pResult);
      cfl_str_free(indexName);
      RETURN SELF_GOTOP((AREAP)pSDBArea);
   } else {
      sdb_api_genError(pSDBArea->connection, EG_CREATE, SDB_ERROR_CREATE_INDEX, "Error creating index", 0, NULL, NULL, NULL);
   }

   hb_vmDestroyBlockOrMacro(pKeyExp);
   hb_itemRelease(pResult);
   cfl_str_free(indexName);
   RETURN HB_FAILURE;
}

static HB_ERRCODE rdd_orderDestroy(SDB_AREAP pSDBArea, LPDBORDERINFO pOrdInfo) {
   HB_ERRCODE errCode;
   HB_INT32 orderPos;
   HB_INT32 indexPos;

   ENTER_FUN_NAME("rdd_orderDestroy");
   sdb_thread_cleanError();
   errCode = sdb_rdd_flushRecord(pSDBArea, CFL_TRUE);
   if (errCode != HB_SUCCESS) {
      RETURN errCode;
   }
   if (pSDBArea->isShared) {
      sdb_api_genError(pSDBArea->connection, EG_SHARED, SDB_ERROR_SHARED, cfl_str_getPtr(pSDBArea->table->clpName), 0, NULL, NULL,
                       NULL);
      RETURN HB_FAILURE;
   }
   if (sdb_util_itemIsNull(pOrdInfo->itmOrder)) {
      orderPos = sdb_rdd_getIndexPosByName(pSDBArea->orders, hb_itemGetCPtr(pOrdInfo->atomBagName));
      indexPos = sdb_rdd_getIndexPosByName(pSDBArea->table->indexes, hb_itemGetCPtr(pOrdInfo->atomBagName));
   } else {
      orderPos = sdb_rdd_getIndexPosByName(pSDBArea->orders, hb_itemGetCPtr(pOrdInfo->itmOrder));
      indexPos = sdb_rdd_getIndexPosByName(pSDBArea->table->indexes, hb_itemGetCPtr(pOrdInfo->itmOrder));
   }
   if (indexPos >= 0) {
      SDB_INDEXP index = (SDB_INDEXP)cfl_list_get(pSDBArea->table->indexes, indexPos);
      if (!sdb_api_tableDropIndex(pSDBArea->connection, pSDBArea->table, index, CFL_FALSE) && sdb_thread_hasError()) {
         sdb_api_genErrorFromSDBError(pSDBArea->connection, sdb_area_getAlias(pSDBArea), EF_NONE, sdb_thread_getLastError(), NULL,
                                      NULL);
      }
      cfl_list_del(pSDBArea->table->indexes, indexPos);
   }
   if (orderPos >= 0) {
      if (orderPos < pSDBArea->uiOrder - 1) {
         --pSDBArea->uiOrder;
      }
      cfl_list_del(pSDBArea->orders, orderPos);
   }
   RETURN HB_SUCCESS;
}

static HB_ERRCODE rdd_lock(SDB_AREAP pSDBArea, LPDBLOCKINFO pLockInfo) {
   ENTER_FUN_NAME("rdd_lock");
   sdb_thread_cleanError();
   if (pSDBArea->isShared) {
      switch (pLockInfo->uiMethod) {
      case DBLM_EXCLUSIVE:
         RETURN sdb_rdd_lockRecord(pSDBArea, 0, (CFL_UINT16 *)&(pLockInfo->fResult), CFL_TRUE);

      case DBLM_MULTIPLE:
         RETURN sdb_rdd_lockRecord(pSDBArea, (SDB_RECNO)hb_itemGetNLL(pLockInfo->itmRecID), (CFL_UINT16 *)&(pLockInfo->fResult),
                                   CFL_FALSE);

      case DBLM_FILE:
         RETURN sdb_rdd_lockFile(pSDBArea, (CFL_UINT16 *)&(pLockInfo->fResult));

      default:
         pLockInfo->fResult = CFL_FALSE;
      }
   } else {
      pLockInfo->fResult = CFL_TRUE;
   }

   RETURN HB_SUCCESS;
}

#ifdef __XHB__

static HB_ERRCODE rdd_unlock(SDB_AREAP pSDBArea, HB_RECNO ulRecno) {
   HB_ERRCODE errCode = HB_SUCCESS;

   ENTER_FUN_NAME("rdd_unlock");
   sdb_thread_cleanError();

   if (!pSDBArea->isQuery && pSDBArea->isShared) {
      if (ulRecno == 0) { /* Unlock All */
         errCode = sdb_rdd_unlockAllRecords(pSDBArea);
      } else {
         errCode = sdb_rdd_unlockRecord(pSDBArea, (SDB_RECNO)ulRecno);
      }
      if (pSDBArea->isLocked) {
         errCode = sdb_rdd_flushRecord(pSDBArea, CFL_FALSE);
         if (!sdb_api_lockAreaTable(pSDBArea, SDB_LOCK_SHARED)) {
            errCode = HB_FAILURE;
         }
         pSDBArea->isLocked = CFL_FALSE;
      }
   }
   RETURN errCode;
}
#else

static HB_ERRCODE rdd_unlock(SDB_AREAP pSDBArea, PHB_ITEM pRecno) {
   SDB_RECNO ulRecno;
   HB_ERRCODE errCode = HB_SUCCESS;

   ENTER_FUN_NAME("rdd_unlock");
   sdb_thread_cleanError();

   if (!pSDBArea->isQuery && pSDBArea->isShared) {
      ulRecno = (SDB_RECNO)hb_itemGetNLL(pRecno);

      if (ulRecno <= 0) { /* Unlock All */
         errCode = sdb_rdd_unlockAllRecords(pSDBArea);
      } else {
         errCode = sdb_rdd_unlockRecord(pSDBArea, ulRecno);
      }
      if (pSDBArea->isLocked) {
         errCode = sdb_rdd_flushRecord(pSDBArea, CFL_FALSE);
         if (!sdb_api_lockAreaTable(pSDBArea, SDB_LOCK_SHARED)) {
            errCode = HB_FAILURE;
         }
         pSDBArea->isLocked = CFL_FALSE;
      }
   }
   RETURN errCode;
}
#endif

#ifdef __XHB__

static HB_ERRCODE rdd_drop(PHB_ITEM pItemTable) {
   PHB_FNAME pFileName;
   CFL_BOOL bResult;
   char *szSchema = NULL;
   SDB_THREAD_DATAP thData = sdb_thread_getData();

   ENTER_FUN_NAME("rdd_drop");
   sdb_thread_cleanError();

   /* Try to delete index file */
   if (hb_itemGetCLen(pItemTable) == 0) {
      RETURN HB_FAILURE;
   }

   pFileName = hb_fsFNameSplit(hb_itemGetCPtr(pItemTable));
   if (pFileName->szExtension == NULL || hb_stricmp(pFileName->szExtension, "dbf") == 0) {
      SDB_CONNECTIONP conn = thData->connection;
      if (!errorConnection(conn, "DBDROP")) {
         szSchema = sdb_util_getSchemaName(conn, pFileName->szPath);
         bResult = sdb_api_dropTable(conn, szSchema, pFileName->szName);
         if (!bResult && sdb_thread_hasError()) {
            sdb_api_genErrorFromSDBError(NULL, "Error drop", EF_NONE, sdb_thread_getLastError(), NULL, NULL);
         }
      } else {
         bResult = CFL_FALSE;
      }
   } else {
      bResult = CFL_FALSE;
   }
   SDB_MEM_FREE(szSchema);
   hb_xfree(pFileName);
   RETURN bResult ? HB_SUCCESS : HB_FAILURE;
}

#else

static HB_ERRCODE rdd_drop(LPRDDNODE pRDD, PHB_ITEM pItemTable, PHB_ITEM pItemIndex, HB_UINT32 ulConnect) {
   PHB_FNAME pFileName;
   PHB_FNAME pTableFileName;
   CFL_BOOL fTable = CFL_FALSE;
   CFL_BOOL bResult;
   const char *szFile;
   char *szSchema = NULL;
   SDB_CONNECTIONP connection;
   SDB_THREAD_DATAP thData = sdb_thread_getData();

   ENTER_FUN_NAME("rdd_drop");
   HB_SYMBOL_UNUSED(pRDD);

   sdb_thread_cleanError();
   /* Try to delete index file */
   if (hb_itemGetCLen(pItemIndex) > 0) {
      szFile = hb_itemGetCPtr(pItemIndex);
   } else if (hb_itemGetCLen(pItemTable) > 0) {
      szFile = hb_itemGetCPtr(pItemTable);
      fTable = CFL_TRUE;
   } else {
      RETURN HB_FAILURE;
   }

   if (ulConnect > 0) {
      connection = sdb_api_getConnection((CFL_UINT16)ulConnect);
   } else {
      connection = thData->connection;
   }
   if (errorConnection(connection, "DROP")) {
      RETURN HB_FAILURE;
   }

   pFileName = hb_fsFNameSplit(szFile);
   if (fTable) {
      if (pFileName->szExtension == NULL || hb_stricmp(pFileName->szExtension, "dbf") == 0) {
         szSchema = sdb_util_getSchemaName(connection, pFileName->szPath);
         bResult = sdb_api_dropTable(connection, szSchema, pFileName->szName);
         if (!bResult || sdb_thread_hasError()) {
            sdb_api_genErrorFromSDBError(NULL, "Drop table error", EF_NONE, sdb_thread_getLastError(), NULL, NULL);
         }
      } else {
         bResult = CFL_FALSE;
      }
   } else {
      if (pFileName->szExtension == NULL || hb_stricmp(pFileName->szExtension, "ntx") == 0) {
         pTableFileName = hb_fsFNameSplit(hb_itemGetCPtr(pItemTable));
         szSchema = sdb_util_getSchemaName(connection, pTableFileName->szPath);
         bResult = sdb_api_dropIndex(connection, szSchema, pTableFileName->szName, pFileName->szName, CFL_FALSE);
         if (!bResult && sdb_thread_hasError()) {
            sdb_api_genErrorFromSDBError(NULL, "Drop index error", EF_NONE, sdb_thread_getLastError(), NULL, NULL);
         }
         hb_xfree(pTableFileName);
      } else {
         bResult = CFL_FALSE;
      }
   }
   SDB_MEM_FREE(szSchema);
   hb_xfree(pFileName);
   RETURN bResult ? HB_SUCCESS : HB_FAILURE;
}
#endif

#ifdef __XHB__

static HB_ERRCODE rdd_exists(PHB_ITEM pItemTable, PHB_ITEM pItemIndex) {
   PHB_FNAME pFileName;
   CFL_BOOL bResult;
   char *szSchema = NULL;
   SDB_THREAD_DATAP thData = sdb_thread_getData();

   ENTER_FUN_NAME("rdd_exists");
   HB_SYMBOL_UNUSED(pItemIndex);

   sdb_thread_cleanError();
   /* Try to delete index file */
   if (hb_itemGetCLen(pItemTable) == 0) {
      RETURN HB_FAILURE;
   }

   pFileName = hb_fsFNameSplit(hb_itemGetCPtr(pItemTable));
   if (pFileName->szExtension == NULL || hb_stricmp(pFileName->szExtension, "dbf") == 0) {
      SDB_CONNECTIONP conn = thData->connection;
      if (!errorConnection(conn, "DBEXISTS")) {
         szSchema = sdb_util_getSchemaName(conn, pFileName->szPath);
         bResult = sdb_api_existsTable(conn, szSchema, pFileName->szName);
         if (!bResult && sdb_thread_hasError()) {
            sdb_api_genErrorFromSDBError(NULL, "Exists", EF_NONE, sdb_thread_getLastError(), NULL, NULL);
         }
      } else {
         bResult = CFL_FALSE;
      }
   } else {
      bResult = CFL_FALSE;
   }
   SDB_MEM_FREE(szSchema);
   hb_xfree(pFileName);
   RETURN bResult ? HB_SUCCESS : HB_FAILURE;
}
#else

static HB_ERRCODE rdd_exists(LPRDDNODE pRDD, PHB_ITEM pItemTable, PHB_ITEM pItemIndex, HB_UINT32 ulConnect) {
   PHB_FNAME pFileName;
   PHB_FNAME pTableFileName;
   CFL_BOOL fTable = CFL_FALSE;
   CFL_BOOL bResult = CFL_FALSE;
   const char *szFile;
   char *szSchema = NULL;
   SDB_CONNECTIONP connection;
   SDB_THREAD_DATAP thData = sdb_thread_getData();

   ENTER_FUN_NAME("rdd_exists");
   HB_SYMBOL_UNUSED(pRDD);

   sdb_thread_cleanError();
   if (pItemIndex && hb_itemGetCLen(pItemIndex) > 0) {
      szFile = hb_itemGetCPtr(pItemIndex);
   } else if (hb_itemGetCLen(pItemTable) > 0) {
      szFile = hb_itemGetCPtr(pItemTable);
      fTable = CFL_TRUE;
   } else {
      RETURN HB_FAILURE;
   }

   if (ulConnect > 0) {
      connection = sdb_api_getConnection((CFL_UINT16)ulConnect);
   } else {
      connection = thData->connection;
   }
   if (errorConnection(connection, "DBEXISTS")) {
      return HB_FAILURE;
   }

   pFileName = hb_fsFNameSplit(szFile);
   if (fTable) {
      if (pFileName->szExtension == NULL || hb_stricmp(pFileName->szExtension, "dbf") == 0) {
         szSchema = sdb_util_getSchemaName(connection, pFileName->szPath);
         bResult = sdb_api_existsTable(connection, szSchema, pFileName->szName);
         if (!bResult && sdb_thread_hasError()) {
            sdb_api_genErrorFromSDBError(NULL, "Exists", EF_NONE, sdb_thread_getLastError(), NULL, NULL);
         }
      } else {
         bResult = CFL_FALSE;
      }
   } else {
      if (pFileName->szExtension == NULL || hb_stricmp(pFileName->szExtension, "ntx") == 0) {
         pTableFileName = hb_fsFNameSplit(hb_itemGetCPtr(pItemTable));
         szSchema = sdb_util_getSchemaName(connection, pTableFileName->szPath);
         bResult = sdb_api_existsIndex(connection, szSchema, pTableFileName->szName, pFileName->szName);
         if (!bResult && sdb_thread_hasError()) {
            sdb_api_genErrorFromSDBError(NULL, "Exists", EF_NONE, sdb_thread_getLastError(), NULL, NULL);
         }
         hb_xfree(pTableFileName);
      } else {
         bResult = CFL_FALSE;
      }
   }
   SDB_MEM_FREE(szSchema);
   hb_xfree(pFileName);
   RETURN bResult ? HB_SUCCESS : HB_FAILURE;
}
#endif

/******************************************************************************
 *                   CLIPPER RDD FUNCTIONS
 ******************************************************************************/
/**
 * Performs a goto in current workarea with the to lock the record
 * Ex.: Area->( SDB_GoTo( 5437, .T. ) )
 * @param nRecno Recno to go to
 * @param lLock Lock the record if .T.
 * @return .T. if succeeded in locking the record
 */
HB_FUNC(SDB_GOTO) {
   PHB_ITEM pRecId = hb_param(1, HB_IT_ANY);
   PHB_ITEM pLock = hb_param(2, HB_IT_LOGICAL);
   AREAP area;
   SDB_AREAP pSDBArea;
   CFL_BOOL bResult;

   area = (AREAP)hb_rddGetCurrentWorkAreaPointer();

   ENTER_FUN_NAME("SDB_GOTO");
   sdb_thread_cleanError();

   if (!sdb_area_isSDBArea(area)) {
      sdb_api_genError(NULL, HB_EI_RDDINVALID, SDB_ERROR_INVALID_OPERATION, "Not SDBRDD workarea", EF_NONE, NULL, NULL, NULL);
      hb_retl(CFL_FALSE);
      RETURN;
   }

   pSDBArea = (SDB_AREAP)area;
   if (pSDBArea->isQuery) {
      sdb_api_genError(pSDBArea->connection, EG_UNSUPPORTED, SDB_ERROR_INVALID_OPERATION, sdb_area_getAlias(pSDBArea), EF_NONE,
                       NULL, NULL, NULL);
      hb_retl(CFL_FALSE);
      RETURN;
   }

   if (SELF_FLUSH((AREAP)pSDBArea) == HB_FAILURE) {
      hb_retl(CFL_FALSE);
      RETURN;
   }

   rdd_resetAreaFlags(pSDBArea);
   if (!sdb_util_itemIsNull(pRecId)) {
      if (hb_itemGetL(pLock)) {
         if (HB_IS_STRING(pRecId)) {
            bResult = sdb_api_tableLockRecord(pSDBArea, NULL, pRecId);
         } else {
            bResult = sdb_api_tableLockRecord(pSDBArea, pRecId, NULL);
         }
      } else {
         bResult = sdb_api_tableGoTo(pSDBArea, pRecId);
      }
      if (bResult) {
         if (sdb_rdd_fetchNextRecord(pSDBArea, SDB_SORT_ASC)) {
            sdb_rdd_readRecord(pSDBArea, hb_itemGetL(pLock));
            hb_retl(HB_TRUE);
         } else {
            if (sdb_thread_hasError()) {
               sdb_api_genErrorFromSDBError(pSDBArea->connection, cfl_str_getPtr(pSDBArea->table->clpName), EF_NONE,
                                            sdb_thread_getLastError(), "SDB_GOTO", NULL);
            }
            hb_retl(HB_FALSE);
         }
      } else if (sdb_thread_hasError()) {
         sdb_api_genErrorFromSDBError(pSDBArea->connection, cfl_str_getPtr(pSDBArea->table->clpName), EF_NONE,
                                      sdb_thread_getLastError(), "SDB_GOTO", NULL);
         hb_retl(HB_FALSE);
      }
   } else {
      /* Clear record buffer */
      pSDBArea->area.fBof = HB_TRUE;
      pSDBArea->area.fEof = HB_TRUE;
      sdb_rdd_blankRecord(pSDBArea);
      hb_retl(HB_FALSE);
   }
   RETURN;
}

/**
 * Performs a query in current workarea with a custom condition. The context
 * fields and del flag will be included automatically. The result will not ber
 * ordered by any field. If you want the result to be sorted, you can append an
 * order by clause after condition. Ex.: area->( sdb_QueryArea("FcNome like
 * :nome order by FcConta", { "nome" => cNome } ) ) area->(
 * sdb_QueryArea("FcNome like :nome order by FcConta", cNome ) )
 * @param cCondition database condition to be used to filter records
 * @param pParams a list of arguments to be passed to statement or a hash with
 * named arguments to be passed
 */
HB_FUNC(SDB_QUERYAREA) {
   SDB_THREAD_DATAP thData = sdb_thread_getData();
   HB_ERRCODE errCode;
   SDB_AREAP pSDBArea;
   SDB_PARAMLISTP params;
   PHB_ITEM pCondition;

   ENTER_FUN_NAME("SDB_QUERYAREA");
   sdb_thread_cleanError();
   pSDBArea = (SDB_AREAP)hb_rddGetCurrentWorkAreaPointer();

   if (pSDBArea->isQuery) {
      sdb_api_genError(pSDBArea->connection, EG_UNSUPPORTED, SDB_ERROR_INVALID_OPERATION, sdb_area_getAlias(pSDBArea), EF_NONE,
                       NULL, NULL, NULL);
      RETURN;
   }

   if (SELF_FLUSH((AREAP)pSDBArea) != HB_SUCCESS) {
      hb_retl(HB_FALSE);
      RETURN;
   }

   pCondition = hb_param(1, HB_IT_STRING);
   if (pCondition) {
      rdd_resetAreaFlags(pSDBArea);
      params = sdb_param_listNew();
      if (hb_pcount() > 1) {
         PHB_ITEM pHash = hb_param(2, HB_IT_HASH);
         if (pHash) {
            sdb_clp_hashToParams(pHash, params, CFL_TRUE, CFL_FALSE);
         } else {
            sdb_clp_functionParamsToParamsList(1, params, CFL_TRUE, CFL_TRUE, CFL_FALSE);
         }
      } else if (sdb_thread_hasParams(thData)) {
         sdb_param_listMoveAll(params, sdb_thread_getParams(thData));
         sdb_thread_freeParams(thData);
      }
      pSDBArea->area.fTop = HB_FALSE;
      pSDBArea->area.fBottom = HB_FALSE;
      if (sdb_api_tableQuery(pSDBArea, pCondition, params)) {
         if (sdb_rdd_fetchNextRecord(pSDBArea, SDB_SORT_ASC)) {
            sdb_rdd_readRecord(pSDBArea, CFL_FALSE);
            errCode = SELF_SKIPFILTER((AREAP)pSDBArea, 1);
         }
         if (errCode != HB_SUCCESS || sdb_thread_hasError()) {
            sdb_api_genErrorFromSDBError(NULL, NULL, EF_NONE, sdb_thread_getLastError(), "SDB_QUERYAREA", NULL);
         }
      } else {
         sdb_api_genErrorFromSDBError(NULL, NULL, EF_NONE, sdb_thread_getLastError(), hb_itemGetCPtr(pCondition), NULL);
      }
      sdb_param_listFree(params);
   } else {
      sdb_api_genError(NULL, EG_ARG, SDB_ERROR_INVALID_ARGUMENT, NULL, EF_NONE, "Invalid arguments", "SDB_QUERYAREA", NULL);
   }
   hb_retl(pSDBArea->area.fFound);
   RETURN;
}

/******************************************************************************
 *                        RDD structures initialization
 ******************************************************************************/
static RDDFUNCS sdbTable = {

    /* Movement and positioning methods */

    (DBENTRYP_BP)NULL, /* rdd_bof */
    (DBENTRYP_BP)NULL, /* rdd_eof */
    (DBENTRYP_BP)NULL, /* rdd_found */
    (DBENTRYP_V)rdd_goBottom, (DBENTRYP_UL)rdd_goTo, (DBENTRYP_I)rdd_goToId, (DBENTRYP_V)rdd_goTop, (DBENTRYP_BIB)rdd_seek,
    (DBENTRYP_L)NULL, /* rdd_skip */
    (DBENTRYP_L)NULL, /* rdd_skipFilter */
    (DBENTRYP_L)rdd_skipRaw,

    /* Data management */

    (DBENTRYP_VF)NULL,                                                      /* rdd_addField */
    (DBENTRYP_B)rdd_append, (DBENTRYP_I)NULL,                               /*rdd_createFields */
    (DBENTRYP_V)rdd_deleteRec, (DBENTRYP_BP)rdd_deleted, (DBENTRYP_SP)NULL, /* rdd_fieldCount */
    (DBENTRYP_VF)NULL,                                                      /* rdd_fieldDisplay */
    (DBENTRYP_SSI)NULL,                                                     /* rdd_fieldInfo */
#ifdef __XHB__
    (DBENTRYP_SVP)NULL, /* rdd_fieldName */
#else
    (DBENTRYP_SCP)NULL, /* rdd_fieldName */
#endif
    (DBENTRYP_V)rdd_flush, (DBENTRYP_PP)NULL, /* rdd_getRec */
    (DBENTRYP_SI)rdd_getValue, (DBENTRYP_SVL)rdd_getVarLen, (DBENTRYP_V)rdd_goCold, (DBENTRYP_V)rdd_goHot,
    (DBENTRYP_P)NULL, /* rdd_putRec */
    (DBENTRYP_SI)rdd_putValue, (DBENTRYP_V)rdd_recall, (DBENTRYP_ULP)rdd_recCount, (DBENTRYP_ISI)rdd_recInfo,
#ifdef __XHB__
    (DBENTRYP_I)rdd_recno,
#else
    (DBENTRYP_ULP)rdd_recno, (DBENTRYP_I)rdd_recId,
#endif
    (DBENTRYP_S)NULL, /* rdd_setFieldExtent */

/* WorkArea/Database management */

#ifdef __XHB__
    (DBENTRYP_P)NULL, /* rdd_alias */
#else
    (DBENTRYP_CP)NULL, /* rdd_alias */
#endif
    (DBENTRYP_V)rdd_close,
#ifdef __XHB__
    (DBENTRYP_VP)rdd_create,
#else
    (DBENTRYP_VO)rdd_create,
#endif
    (DBENTRYP_SI)rdd_info, (DBENTRYP_V)rdd_newArea,
#ifdef __XHB__
    (DBENTRYP_VP)rdd_open,
#else
    (DBENTRYP_VO)rdd_open,
#endif
    (DBENTRYP_V)rdd_release, (DBENTRYP_SP)rdd_structSize,
#ifdef __XHB__
    (DBENTRYP_P)rdd_sysName,
#else
    (DBENTRYP_CP)rdd_sysName,
#endif
    (DBENTRYP_VEI)NULL,                       /* rdd_dbEval */
    (DBENTRYP_V)rdd_pack, (DBENTRYP_LSP)NULL, /* rdd_packRec */
    (DBENTRYP_VS)NULL,                        /* rdd_sort */
    (DBENTRYP_VT)NULL,                        /* rdd_trans */
    (DBENTRYP_VT)NULL,                        /* rdd_transRec */
    (DBENTRYP_V)rdd_zap,

    /* Relational Methods */

    (DBENTRYP_VR)NULL, /* rdd_childEnd */
    (DBENTRYP_VR)NULL, /* rdd_childStart */
    (DBENTRYP_VR)NULL, /* rdd_childSync */
    (DBENTRYP_V)NULL,  /* rdd_syncChildren */
    (DBENTRYP_V)NULL,  /* rdd_clearRel */
    (DBENTRYP_V)NULL,  /* rdd_forceRel */
#ifdef __XHB__
    (DBENTRYP_SVP)NULL, /* rdd_relArea */
#else
    (DBENTRYP_SSP)NULL, /* rdd_relArea */
#endif
    (DBENTRYP_VR)NULL, /* rdd_relEval */
#ifdef __XHB__
    (DBENTRYP_SVP)NULL, /* rdd_relText */
#else
    (DBENTRYP_SI)NULL, /* rdd_relText */
#endif
    (DBENTRYP_VR)NULL, /* rdd_setRel */

/* Order Management */
#ifdef __XHB__
    (DBENTRYP_OI)rdd_orderListAdd, (DBENTRYP_V)rdd_orderListClear, (DBENTRYP_OI)rdd_orderListDelete,
    (DBENTRYP_OI)rdd_orderListFocus, (DBENTRYP_V)NULL, /* rdd_orderListRebuild */
    (DBENTRYP_VOI)NULL,                                /* rdd_orderCondition */
    (DBENTRYP_VOC)rdd_orderCreate, (DBENTRYP_OI)rdd_orderDestroy, (DBENTRYP_OII)rdd_orderInfo,
#else
    (DBENTRYP_VOI)rdd_orderListAdd, (DBENTRYP_V)rdd_orderListClear, (DBENTRYP_VOI)rdd_orderListDelete,
    (DBENTRYP_VOI)rdd_orderListFocus, (DBENTRYP_V)NULL, /* rdd_orderListRebuild */
    (DBENTRYP_VOO)NULL,                                 /* rdd_orderCondition */
    (DBENTRYP_VOC)rdd_orderCreate, (DBENTRYP_VOI)rdd_orderDestroy, (DBENTRYP_SVOI)rdd_orderInfo,
#endif

    /* Filters and Scope Settings */

    (DBENTRYP_V)rdd_clearFilter, (DBENTRYP_V)NULL,   /* rdd_clearLocate */
    (DBENTRYP_V)NULL,                                /* rdd_clearScope */
    (DBENTRYP_VPLP)NULL,                             /* rdd_countScope */
    (DBENTRYP_I)NULL,                                /* rdd_filterText */
    (DBENTRYP_SI)NULL,                               /* rdd_scopeInfo */
    (DBENTRYP_VFI)rdd_setFilter, (DBENTRYP_VLO)NULL, /* rdd_setLocate */
    (DBENTRYP_VOS)NULL,                              /* rdd_setScope */
    (DBENTRYP_VPL)NULL,                              /* rdd_skipScope */
#ifndef __XHB__
    (DBENTRYP_B)NULL, /* rdd_locate */
#endif

/* Miscellaneous */
#ifdef __XHB__
    (DBENTRYP_P)NULL, /* rdd_compile */
#else
    (DBENTRYP_CC)NULL, /* rdd_compile */
#endif
    (DBENTRYP_I)NULL, /* rdd_error */
    (DBENTRYP_I)NULL, /* rdd_evalBlock */

    /* Network operations */

    (DBENTRYP_VSP)NULL, /* rdd_rawLock */
    (DBENTRYP_VL)rdd_lock,
#ifdef __XHB__
    (DBENTRYP_UL)rdd_unlock,
#else
    (DBENTRYP_I)rdd_unlock,
#endif

/* Memofile functions */

#ifdef __XHB__
    (DBENTRYP_V)NULL,    /* rdd_closeMemFile */
    (DBENTRYP_VP)NULL,   /* rdd_createMemFile */
    (DBENTRYP_SVPB)NULL, /* rdd_getValueFile */
    (DBENTRYP_VP)NULL,   /* rdd_openMemFile */
    (DBENTRYP_SVP)NULL,  /* rdd_putValueFile */
#else
    (DBENTRYP_V)NULL,    /* rdd_closeMemFile */
    (DBENTRYP_VO)NULL,   /* rdd_createMemFile */
    (DBENTRYP_SCCS)NULL, /* rdd_getValueFile */
    (DBENTRYP_VO)NULL,   /* rdd_openMemFile */
    (DBENTRYP_SCCS)NULL, /* rdd_putValueFile */
#endif

    /* Database file header handling */

    (DBENTRYP_V)NULL, /* rdd_readDBHeader */
    (DBENTRYP_V)NULL, /* rdd_writeDBHeader */

/* non WorkArea functions       */
#ifdef __XHB__
    (DBENTRYP_I0)rdd_exit, (DBENTRYP_I1)rdd_drop, (DBENTRYP_I2)rdd_exists,
#else
    (DBENTRYP_R)NULL,          /* rdd_init */
    (DBENTRYP_R)rdd_exit,      /* unregister RDD */
    (DBENTRYP_RVVL)rdd_drop,   /* remove table or index */
    (DBENTRYP_RVVL)rdd_exists, /* check if table or index exists */
    (DBENTRYP_RVVVL)NULL,      /* rdd_rename */
    (DBENTRYP_RSLV)NULL,       /* rdd_rddInfo */
#endif

    /* Special and reserved methods */

    (DBENTRYP_SVP)NULL /* rdd_whoCares */
};

HB_FUNC(SDBRDD) {
   ;
}

/* Funcao que retorna a tabela de funcoes. */
HB_FUNC(SDBRDD_GETFUNCTABLE) {
   RDDFUNCS *pTable;
   HB_USHORT *uiCount;

   ENTER_FUN_NAME("SDBRDD_GETFUNCTABLE");

   uiCount = (HB_USHORT *)hb_itemGetPtr(hb_param(1, HB_IT_POINTER));
   pTable = (RDDFUNCS *)hb_itemGetPtr(hb_param(2, HB_IT_POINTER));

   SDB_LOG_DEBUG(("SDBRDD_GETFUNCTABLE: uiCount=%i, table=%p", uiCount, pTable));

   if (pTable) {
      if (uiCount) {
         *uiCount = RDDFUNCSCOUNT;
      }
      hb_retni(hb_rddInherit(pTable, &sdbTable, &sdbSuper, 0));
      s_pSDBTable = pTable;
   } else {
      hb_retni(HB_FAILURE);
   }
   RETURN;
}

static void registerRdd(void) {
#ifdef __XHB__
   PHB_DYNS funSymbol;
   PHB_ITEM pItem;
   funSymbol = hb_dynsymFindName("RDDREGISTER");
   if (funSymbol) {
      hb_vmPushSymbol(funSymbol->pSymbol);
      hb_vmPushNil();
      pItem = hb_itemPutC(NULL, sdb_api_getRddName());
      hb_vmPush(pItem);
      hb_itemPutNI(pItem, RDT_FULL);
      hb_vmPush(pItem);
      hb_vmDo(2);
      hb_itemRelease(pItem);
   } else {
      sdb_api_genError(NULL, SDB_ERROR_TYPE_SDB, SDB_ERROR_REGISTERING_DRIVER, NULL, EF_NONE, "RddRegister not found",
                       "SDB_REGISTERRDD", NULL);
   }
#else
   if (hb_rddRegister(sdb_api_getRddName(), RDT_FULL) > 1) {
      sdb_api_genError(NULL, SDB_ERROR_TYPE_SDB, SDB_ERROR_REGISTERING_DRIVER, NULL, EF_NONE, "Error registering RDD",
                       "SDB_REGISTERRDD", NULL);
   }
#endif
}

HB_FUNC(SDB_REGISTERRDD) {
   PHB_ITEM pName = hb_param(1, HB_IT_ANY);

   if (sdb_api_initializeInternalData()) {
      /* Default SDB RDD name */
      if (sdb_util_itemIsNull(pName)) {
         sdb_api_setRddName(_SDB_RDDNAME_);
#ifdef __XHB__
         hb_dynsymNew(&s_symRddName, NULL);
         hb_dynsymNew(&s_symFuncTab, NULL);
#else
         hb_dynsymNew(&s_symRddName, HB_FALSE);
         hb_dynsymNew(&s_symFuncTab, HB_FALSE);
#endif
         registerRdd();
         /* User defined SDB RDD name */
      } else if (HB_IS_STRING(pName) && hb_itemGetCLen(pName) <= HB_RDD_MAX_DRIVERNAME_LEN) {
         sdb_api_setRddName((char *)hb_itemGetCPtr(pName));
         snprintf(s_rddNameFunctionName, sizeof(s_rddNameFunctionName), "%s", sdb_api_getRddName());
         snprintf(s_funcTabFunctionName, sizeof(s_funcTabFunctionName), "%s_GETFUNCTABLE", sdb_api_getRddName());
         s_symRddName.szName = s_rddNameFunctionName;
         s_symFuncTab.szName = s_funcTabFunctionName;
#ifdef __XHB__
         hb_dynsymNew(&s_symRddName, NULL);
         hb_dynsymNew(&s_symFuncTab, NULL);
#else
         hb_dynsymNew(&s_symRddName, HB_FALSE);
         hb_dynsymNew(&s_symFuncTab, HB_FALSE);
#endif
         registerRdd();
      } else {
         sdb_api_genError(NULL, EG_ARG, SDB_ERROR_INVALID_ARGUMENT, NULL, EF_NONE, "Invalid arguments", "SDB_REGISTERRDD", NULL);
      }
   } else {
      sdb_api_genError(NULL, SDB_ERROR_TYPE_SDB, SDB_ERROR_REGISTERING_DRIVER, NULL, EF_NONE, "RDD already initialized",
                       "SDB_REGISTERRDD", NULL);
   }
}
