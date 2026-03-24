#include "hbapiitm.h"
#include "hbapi.h"
#include "hbvm.h"
#ifdef __XHB__
#include "hbfast.h"
#endif

#include "sdb_area.h"
#include "sdb.h"
#include "sdb_statement.h"
#include "sdb_table.h"
#include "sdb_field.h"
#include "sdb_index.h"
#include "sdb_record.h"
#include "sdb_util.h"
#include "sdb_log.h"
#include "sdb_schema.h"

void sdb_area_init(SDB_AREAP pSDBArea) {
   int i;
   cfl_lock_init(&(pSDBArea->lock));
   pSDBArea->connection = NULL;
   pSDBArea->table = NULL;
   pSDBArea->fields = NULL;
   pSDBArea->fieldsCount = 0;
   pSDBArea->waPKField = NULL;
   pSDBArea->waDelField = NULL;
   pSDBArea->waRowIdField = NULL;
   pSDBArea->context = cfl_list_new(3);
   pSDBArea->record = NULL;
   pSDBArea->uiRecordLen = 0;
   pSDBArea->uiOrder = 0;
   pSDBArea->orders = cfl_list_new(5);
   pSDBArea->sqlFilter = NULL;
   pSDBArea->isRedoQueries = CFL_TRUE;
   pSDBArea->isUnpositioned = CFL_TRUE;
   pSDBArea->pCurrentCommand = NULL;
   for (i = 0; i < SDB_CMD_COUNT; i++) {
      pSDBArea->commands[i].pStmt = NULL;
      pSDBArea->commands[i].uiOrder = 0;
      pSDBArea->commands[i].uiSort = SDB_SORT_NO;
      pSDBArea->commands[i].isFilterDeleted = CFL_TRUE;
      pSDBArea->commands[i].uiQueryMore = QRY_MORE_NO;
   }
   pSDBArea->pStatement = NULL;
   pSDBArea->uiQueryTopMode = QRY_TOP_ALL;
   pSDBArea->uiQueryBottomMode = QRY_BOTTOM_ALL;
   pSDBArea->lockServerCtxId = 0;
   pSDBArea->waitTimeLock = 0;
   pSDBArea->recordLocked = NULL;
   pSDBArea->lockControl = SDB_LOCK_CONTROL_NONE;
   pSDBArea->lockId = NULL;
   pSDBArea->isReadOnly = CFL_FALSE;
   pSDBArea->isShared = CFL_FALSE;
   pSDBArea->isAppend = CFL_FALSE;
   pSDBArea->isHot = CFL_FALSE;
   pSDBArea->isInsertImmediate = CFL_FALSE;
   pSDBArea->isLocked = CFL_FALSE;
   pSDBArea->isQuery = CFL_FALSE;
   pSDBArea->isFlushImmediate = CFL_TRUE;
   pSDBArea->isContextActive = CFL_FALSE;
   pSDBArea->isValidBuffer = CFL_FALSE;
   pSDBArea->isExactQuery = CFL_TRUE;
   pSDBArea->keyValue = NULL;
}

/* Workarea structs are released by SUPER_RELEASE */
void sdb_area_clear(SDB_AREAP pSDBArea) {
   ENTER_FUN_NAME("sdb_area_clear");
   if (pSDBArea) {
      SDB_RECORDLOCKEDP lock;
      SDB_RECORDLOCKEDP prevLock;

      lock = pSDBArea->recordLocked;
      while (lock) {
         prevLock = lock->previous;
         SDB_MEM_FREE(lock);
         lock = prevLock;
      }
      pSDBArea->recordLocked = NULL;
      sdb_area_clearContext(pSDBArea);
      cfl_list_free(pSDBArea->context);
      pSDBArea->context = NULL;
      hb_itemRelease(pSDBArea->keyValue);
      pSDBArea->keyValue = NULL;
      cfl_list_free(pSDBArea->orders);
      pSDBArea->orders = NULL;
      sdb_record_free(pSDBArea->record);
      pSDBArea->record = NULL;
      hb_itemRelease(pSDBArea->sqlFilter);
      pSDBArea->sqlFilter = NULL;
      /* Release workarea fields */
      if (pSDBArea->fields != NULL) {
         SDB_MEM_FREE(pSDBArea->fields);
         pSDBArea->fields = NULL;
         pSDBArea->fieldsCount = 0;
      }
      if (pSDBArea->lockId != NULL) {
         cfl_str_free(pSDBArea->lockId);
         pSDBArea->lockId = NULL;
      }
      cfl_lock_free(&pSDBArea->lock);
   }
   RETURN;
}

void sdb_area_lock(SDB_AREAP pSDBArea) {
   ENTER_FUN_NAME("sdb_area_lock");
   cfl_lock_acquire(&pSDBArea->lock);
   RETURN;
}

void sdb_area_unlock(SDB_AREAP pSDBArea) {
   ENTER_FUN_NAME("sdb_area_unlock");
   cfl_lock_release(&pSDBArea->lock);
   RETURN;
}

const char * sdb_area_getAlias(SDB_AREAP pSDBArea) {
   ENTER_FUN_NAME("sdb_area_getAlias");
#ifdef __XHB__
   if (pSDBArea->area.atomAlias && ( ( PHB_DYNS ) pSDBArea->area.atomAlias )->hArea) {
      RETURN ( ( PHB_DYNS ) pSDBArea->area.atomAlias )->pSymbol->szName;
   }
#else
   if (pSDBArea->area.atomAlias && hb_dynsymAreaHandle((PHB_DYNS) pSDBArea->area.atomAlias)) {
      RETURN hb_dynsymName((PHB_DYNS) pSDBArea->area.atomAlias);
   }
#endif
   RETURN "";
}

void sdb_area_clearContext(SDB_AREAP pSDBArea) {
   CFL_UINT32 len;
   CFL_UINT32 i;

   ENTER_FUNP(("sdb_area_clearContext. len=%u", cfl_list_length(pSDBArea->context)));
   len = cfl_list_length(pSDBArea->context);
   for (i = 0; i < len; i++) {
      SDB_WAFIELDP waField = (SDB_WAFIELDP) cfl_list_get(pSDBArea->context, i);
      waField->isContext = CFL_FALSE;
   }
   pSDBArea->isContextActive = CFL_FALSE;
   cfl_list_clear(pSDBArea->context);
   RETURN;
}

SDB_WAFIELDP sdb_area_getField(SDB_AREAP pSDBArea, const char *fieldName) {
   CFL_UINT32 ulIndex;

   ENTER_FUN_NAME("sdb_area_getField");
   for (ulIndex = 0; ulIndex < pSDBArea->fieldsCount; ulIndex++) {
      if (cfl_str_bufferEqualsIgnoreCase(pSDBArea->fields[ulIndex].field->clpName, fieldName)) {
         RETURN &pSDBArea->fields[ulIndex];
      }
   }
   RETURN NULL;
}


SDB_WAFIELDP sdb_area_addField(SDB_AREAP pSDBArea, SDB_FIELDP field) {
   CFL_UINT32 lastField = pSDBArea->fieldsCount++;
   if (pSDBArea->fields != NULL) {
      pSDBArea->fields = (SDB_WAFIELDP) SDB_MEM_REALLOC(pSDBArea->fields, pSDBArea->fieldsCount * sizeof(SDB_WAFIELD));
   } else {
      pSDBArea->fields = (SDB_WAFIELDP) SDB_MEM_ALLOC(pSDBArea->fieldsCount * sizeof(SDB_WAFIELD));
   }
   pSDBArea->fields[lastField].field = field;
   pSDBArea->fields[lastField].isContext = CFL_FALSE;
   pSDBArea->fields[lastField].isChanged = CFL_FALSE;
   return &pSDBArea->fields[lastField];
}

PHB_ITEM sdb_area_getFieldValue(SDB_AREAP pSDBArea, SDB_FIELDP field, PHB_ITEM pValue) {
   SDB_LOG_DEBUG(("sdb_area_getFieldValue: field=%s", cfl_str_getPtr(field->clpName)));
   if (pValue == NULL) {
      pValue = hb_itemNew(NULL);
   }
   if(IS_INDEX_EXPR_FIELD(field)) {
      SDB_INDEXP index = sdb_table_getIndex(pSDBArea->table, cfl_str_getPtr(field->clpIndexName));
      if (index) {
         hb_itemMove(pValue, hb_vmEvalBlockOrMacro(index->compiledExpr));
      } else {
         hb_itemClear(pValue);
      }
   } else {
      sdb_record_getValue(pSDBArea->record, field, pValue, CFL_FALSE);
   }
   return pValue;
}

void sdb_area_setFieldValue(SDB_AREAP pSDBArea, SDB_FIELDP field, PHB_ITEM pValue) {
   if(! IS_INDEX_EXPR_FIELD(field)) {
      sdb_record_setValue(pSDBArea->record, field, pValue);
   }
}

void sdb_area_resetLockId(SDB_AREAP pSDBArea) {
   if (pSDBArea->lockId != NULL) {
      cfl_str_free(pSDBArea->lockId);
      pSDBArea->lockId = NULL;
   }
}

CFL_STRP sdb_area_getLockId(SDB_AREAP pSDBArea) {

   ENTER_FUN_NAME("sdb_area_getLockId");
   if (pSDBArea->lockId == NULL) {
      pSDBArea->lockId = cfl_str_new(100);
      CFL_STR_APPEND_CONST(pSDBArea->lockId, "\"SDB$");
      cfl_str_appendStr(pSDBArea->lockId, pSDBArea->table->clpSchema->name);
      cfl_str_appendChar(pSDBArea->lockId, '.');
      cfl_str_appendStr(pSDBArea->lockId, pSDBArea->table->clpName);
      if (pSDBArea->context != NULL) {
         CFL_UINT32 ulFields = cfl_list_length(pSDBArea->context);
         CFL_UINT32 ulIndex;
         PHB_ITEM pItem = hb_itemNew(NULL);
         for (ulIndex = 0; ulIndex < ulFields; ulIndex++) {
            SDB_WAFIELDP waField = (SDB_WAFIELDP) cfl_list_get(pSDBArea->context, ulIndex);
            cfl_str_appendChar(pSDBArea->lockId, '$');
            cfl_str_appendStr(pSDBArea->lockId, waField->field->dbName);
            cfl_str_appendChar(pSDBArea->lockId, '=');
            sdb_area_getFieldValue(pSDBArea, waField->field, pItem);
            sdb_util_strAppendItem(pSDBArea->lockId, pItem);
         }
         hb_itemRelease(pItem);
      }
      cfl_str_appendChar(pSDBArea->lockId, '"');
   }
   RETURN pSDBArea->lockId;
}

SDB_INDEXP sdb_area_getOrder(SDB_AREAP pSDBArea, CFL_INT16 iOrder) {
   if (iOrder > 0) {
      return (SDB_INDEXP) cfl_list_get(pSDBArea->orders, iOrder - 1);
   }
   return NULL;
}

void sdb_area_closeStatements(SDB_AREAP pSDBArea) {
   int i;
   SDB_LOG_TRACE(("sdb_area_closeStatements"));
   for (i = 0; i < SDB_CMD_COUNT; i++) {
      if (pSDBArea->commands[i].pStmt != NULL) {
         sdb_stmt_free(pSDBArea->commands[i].pStmt);
         pSDBArea->commands[i].pStmt = NULL;
      }
   }
}

void sdb_area_closeQueryStatements(SDB_AREAP pSDBArea) {
   int i;
   for (i = SDB_CMD_FIRST_QUERY; i < SDB_CMD_COUNT; i++) {
      if (pSDBArea->commands[i].pStmt != NULL) {
         sdb_stmt_free(pSDBArea->commands[i].pStmt);
         pSDBArea->commands[i].pStmt = NULL;
      }
   }
}

void sdb_area_sortContextByPartitionKeys(SDB_AREAP pSDBArea) {
   CFL_UINT32 len;
   CFL_UINT32 ind1;
   CFL_UINT32 ind2;
   len = cfl_list_length(pSDBArea->context);
   for (ind1 = 0; ind1 < len - 1; ind1++) {
      for (ind2 = 0; ind2 < len - ind1 - 1; ind2++) {
         SDB_WAFIELDP waField = (SDB_WAFIELDP) cfl_list_get(pSDBArea->context, ind2);
         SDB_WAFIELDP waNextField = (SDB_WAFIELDP) cfl_list_get(pSDBArea->context, ind2 + 1);
         if (waField->field->contextPos > waNextField->field->contextPos) {
            cfl_list_set(pSDBArea->context, ind2, waNextField);
            cfl_list_set(pSDBArea->context, ind2 + 1 , waField);
         }
      }
   }
}

CFL_BOOL sdb_area_isSDBArea(AREAP area) {
#ifdef __XHB__
   return area != NULL;
#else
   return area != NULL;
//   return area != NULL && hb_rddIsDerivedFrom(area->rddID, sdb_api_getRddId());
#endif
}
