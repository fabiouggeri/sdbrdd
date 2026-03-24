#include <string.h>
#include <stdio.h>

#include "hbapi.h"
#include "hbapiitm.h"

#include "sdb_param.h"
#include "sdb_util.h"
#include "sdb_log.h"

#define DEFAULT_LIST_CAPACITY 12

#define PARAM_MOVE(parDest, parOri) SDB_MEM_COPY(parDest, parOri, sizeof(SDB_PARAM))


static void param_init(SDB_PARAMP param, CFL_UINT32 pos, const char *name, CFL_UINT32 nameLen, SDB_CLP_DATATYPE type, CFL_UINT64 length, CFL_BOOL bOut,
        CFL_BOOL bTrim, CFL_BOOL bNullable) {
   SDB_LOG_DEBUG(("param_init: name=%s, type=%d, trim=%s out=%s", name, type, bTrim ? "true" : "false", bOut ? "true" : "false"));
   param->objectType = SDB_OBJ_PARAM;
   param->isOut = bOut;
   param->pItemBind = NULL;
   param->pItem = hb_itemNew(NULL);
   param->pos = pos;
   param->name = (! sdb_util_isEmpty(name) && nameLen > 0) ? cfl_str_newBuffer(name) : NULL;
   param->type = sdb_util_isValidType(type) ? type : SDB_CLP_UNKNOWN;
   param->length = length;
   param->isTrim = bTrim;
   param->isNullable = bNullable;
   param->handle = NULL;
   param->freeHandle = NULL;
}

static void freeParamData(SDB_PARAMP param) {
   SDB_LOG_TRACE(("freeParamData: param=%p, free_fun=%p handle=%p", param, param->freeHandle, param->handle));
   if (param->freeHandle) {
      param->freeHandle(param);
   }
   if (param->pItem != NULL) {
      hb_itemRelease(param->pItem);
   }
   if (param->name != NULL) {
      cfl_str_free(param->name);
   }
}

void sdb_param_free(SDB_PARAMP param) {
   ENTER_FUN;
   freeParamData(param);
   SDB_MEM_FREE(param);
   RETURN;
}

PHB_ITEM sdb_param_setValue(SDB_PARAMP param, PHB_ITEM pItem) {
   SDB_LOG_TRACE(("sdb_param_setValue: param: %p type: %u value: %s", param, param->type, ITEM_STR(pItem)));
   if (pItem != NULL) {
      hb_itemCopy(param->pItem, pItem);
      if (param->type == SDB_CLP_UNKNOWN) {
         param->type = sdb_util_itemToFieldType(pItem);
      }
   } else {
      hb_itemClear(param->pItem);
   }
   return param->pItem;
}

/*********************** PARAMS LIST API **************************/
static void freeParamsData(SDB_PARAMLISTP params) {
   CFL_UINT32 i;
   CFL_UINT32 len = cfl_array_length(&params->items);
   SDB_LOG_TRACE(("freeParamsData: params=%p, len=%d", params, len));
   for (i = 0; i < len; i++) {
      freeParamData((SDB_PARAMP)cfl_array_get(&params->items, i));
   }
}

SDB_PARAMLISTP sdb_param_listNew(void) {
   SDB_PARAMLISTP paramList = SDB_MEM_ALLOC(sizeof(SDB_PARAMLIST));
   cfl_array_init(&paramList->items, DEFAULT_LIST_CAPACITY, sizeof(SDB_PARAM));
   return paramList;
}

void sdb_param_listFree(SDB_PARAMLISTP params) {
   ENTER_FUN;
   if (params != NULL) {
      freeParamsData(params);
      cfl_array_free(&params->items);
      SDB_MEM_FREE(params);
   }
   RETURN;
}

void sdb_param_listClear(SDB_PARAMLISTP params) {
   ENTER_FUN;
   if (params != NULL) {
      freeParamsData(params);
      cfl_array_clear(&params->items);
   }
   RETURN;
}

SDB_PARAMP sdb_param_listAdd(SDB_PARAMLISTP params, const char *name, CFL_UINT32 nameLen, SDB_CLP_DATATYPE type, CFL_UINT64 length,
        CFL_BOOL bOut, CFL_BOOL bTrim, CFL_BOOL bNullable) {
   SDB_PARAMP param = cfl_array_add(&params->items);
   if (param != NULL) {
      param_init(param, cfl_array_length(&params->items), name, nameLen, type, length, bOut, bTrim, bNullable);
   }
   return param;
}

SDB_PARAMP sdb_param_listAddFirst(SDB_PARAMLISTP params, const char *name, CFL_UINT32 nameLen, SDB_CLP_DATATYPE type, CFL_UINT64 length,
        CFL_BOOL bOut, CFL_BOOL bTrim, CFL_BOOL bNullable) {
   SDB_PARAMP param = cfl_array_insert(&params->items, 0);
   if (param != NULL) {
      CFL_UINT32 len = cfl_array_length(&params->items);
      CFL_UINT32 i;
      for (i = 1; i < len; i++) {
         param->pos = i + 1;
      }
      param_init(param, 1, name, nameLen, type, length, bOut, bTrim, bNullable);
   }
   return param;
}

CFL_BOOL sdb_param_listIsEmpty(SDB_PARAMLISTP params) {
   return cfl_array_length(&params->items) == 0;
}

CFL_UINT32 sdb_param_listLength(SDB_PARAMLISTP params) {
   return cfl_array_length(&params->items);
}

SDB_PARAMP sdb_param_listGet(SDB_PARAMLISTP params, const char *paramName) {
   CFL_UINT32 len;
   CFL_UINT32 i;
   SDB_PARAMP param;

   ENTER_FUN;
   len = cfl_array_length(&params->items);
   for (i = 0; i < len; i++) {
      param = (SDB_PARAMP) cfl_array_get(&params->items, i);
      if (param->name != NULL && cfl_str_bufferEqualsIgnoreCase(param->name, paramName)) {
         RETURN param;
      }
   }
   RETURN NULL;
}

SDB_PARAMP sdb_param_listGetPos(SDB_PARAMLISTP params, CFL_UINT32 pos) {
   return (SDB_PARAMP) cfl_array_get(&params->items, pos - 1);
}

CFL_ITERATORP sdb_param_listIterator(SDB_PARAMLISTP paramList) {
   return cfl_array_iterator(&paramList->items);
}

void sdb_param_listMoveAll(SDB_PARAMLISTP destList, SDB_PARAMLISTP sourceList) {
   CFL_UINT32 len;
   CFL_UINT32 i;
   CFL_UINT32 newItemPos;

   ENTER_FUN;
   len = cfl_array_length(&sourceList->items);
   newItemPos = cfl_array_length(&destList->items) + 1;
   for (i = 0; i < len; i++) {
      SDB_PARAMP parDest = cfl_array_add(&destList->items);
      if (parDest == NULL) {
         break;
      }
      PARAM_MOVE(parDest, (SDB_PARAMP) cfl_array_get(&sourceList->items, i));
      parDest->pos = newItemPos++;
   }
   cfl_array_clear(&sourceList->items);
   RETURN;
}

void sdb_param_listUpdateBinds(SDB_PARAMLISTP list) {
   CFL_UINT32 len;
   CFL_UINT32 i;

   ENTER_FUN;
   len = cfl_array_length(&list->items);
   for (i = 0; i < len; i++) {
      SDB_PARAMP param = (SDB_PARAMP) cfl_array_get(&list->items, i);
      sdb_param_updateBind(param);
   }
   RETURN;
}
