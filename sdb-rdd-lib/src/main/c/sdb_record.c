#include <string.h>

#include "hbapi.h"
#include "hbapiitm.h"
#include "hbapirdd.h"
#include "hbapierr.h"
#include "hbdate.h"
#ifdef __XHB__
#include "hbfast.h"
#endif

#include "cfl_list.h"

#include "sdb_record.h"
#include "sdb_area.h"
#include "sdb_table.h"
#include "sdb_field.h"
#include "sdb_util.h"
#include "sdb_log.h"
#include "sdb_statement.h"

#define FIELD_DATA(T, R, F) *((T *)&((R)->data[(R)->fieldsOffsets[(F)->tablePos - 1]]))
#define FIELD_DATA_PTR(T, R, F) ((T)&(R)->data[(R)->fieldsOffsets[(F)->tablePos - 1]])

static CFL_UINT32 fieldSize(SDB_FIELDP field) {
   CFL_UINT32 size;
   switch (field->clpType) {
      case SDB_CLP_CHARACTER:
         size = sizeof(CFL_UINT32) + field->length + 1;
         break;

      case SDB_CLP_LOGICAL:
         size = 1;
         break;

      case SDB_CLP_DATE:
         size = sizeof(long);
         break;

      case SDB_CLP_NUMERIC:
         if (field->decimals > 0) {
            size = sizeof(double);
         } else if (field->length < 3) {
            size = sizeof(CFL_INT8);
         } else if (field->length < 5) {
            size = sizeof(CFL_INT16);
         } else if (field->length < 10) {
            size = sizeof(CFL_INT32);
         } else {
            size = sizeof(CFL_INT64);
         }
         break;

      case SDB_CLP_INTEGER:
         size = sizeof(CFL_INT32);
         break;

      case SDB_CLP_BIGINT:
         size = sizeof(CFL_INT64);
         break;

      case SDB_CLP_DOUBLE:
         size = sizeof(double);
         break;

      case SDB_CLP_FLOAT:
         size = sizeof(double);
        break;

      case SDB_CLP_TIMESTAMP:
         size = sizeof(double);
         break;

      case SDB_CLP_MEMO_LONG:
      case SDB_CLP_CLOB:
      case SDB_CLP_BLOB:
      case SDB_CLP_IMAGE:
      case SDB_CLP_ROWID:
      case SDB_CLP_CURSOR:
         size = sizeof(PHB_ITEM);
         break;

      default:
         size = 0;
         break;
   }
   return size;
}

static void initData(SDB_RECORDP record) {
   CFL_UINT32 len = cfl_list_length(record->table->fields);
   CFL_UINT32 i;

   ENTER_FUN_NAME("initData");
   for (i = 0; i < len; i++) {
      SDB_FIELDP field = (SDB_FIELDP) cfl_list_get(record->table->fields, i);
      switch (field->clpType) {
         case SDB_CLP_CHARACTER:
         case SDB_CLP_LOGICAL:
         case SDB_CLP_DATE:
         case SDB_CLP_NUMERIC:
         case SDB_CLP_INTEGER:
         case SDB_CLP_BIGINT:
         case SDB_CLP_DOUBLE:
         case SDB_CLP_TIMESTAMP:
            break;

         case SDB_CLP_ROWID:
         case SDB_CLP_MEMO_LONG:
         case SDB_CLP_CLOB:
         case SDB_CLP_BLOB:
         case SDB_CLP_IMAGE:
         case SDB_CLP_CURSOR:
            (*(PHB_ITEM *)&(record->data[record->fieldsOffsets[i]])) = hb_itemNew(NULL);
            break;
      }
   }
   RETURN;
}

static void releaseData(SDB_RECORDP record) {
   CFL_UINT32 len = cfl_list_length(record->table->fields);
   CFL_UINT32 i;

   for (i = 0; i < len; i++) {
      SDB_FIELDP field = (SDB_FIELDP) cfl_list_get(record->table->fields, i);
      switch (field->clpType) {
         case SDB_CLP_CHARACTER:
         case SDB_CLP_LOGICAL:
         case SDB_CLP_DATE:
         case SDB_CLP_NUMERIC:
         case SDB_CLP_INTEGER:
         case SDB_CLP_BIGINT:
         case SDB_CLP_DOUBLE:
         case SDB_CLP_TIMESTAMP:
            break;

         case SDB_CLP_ROWID:
         case SDB_CLP_MEMO_LONG:
         case SDB_CLP_CLOB:
         case SDB_CLP_BLOB:
         case SDB_CLP_IMAGE:
         case SDB_CLP_CURSOR:
            hb_itemRelease(FIELD_DATA(PHB_ITEM, record, field));
            break;
      }
   }
}

static CFL_UINT32 *allocFieldOffsets(SDB_AREAP pSDBArea, CFL_UINT32 *recordLen) {
   CFL_UINT32 len = cfl_list_length(pSDBArea->table->fields);
   CFL_UINT32 i;
   CFL_UINT32 offset = 0;
   CFL_UINT32 *fieldsOffsets;

   fieldsOffsets = (CFL_UINT32 *) SDB_MEM_ALLOC(cfl_list_length(pSDBArea->table->fields) * sizeof(CFL_UINT32));
   for (i = 0; i < len; i++) {
      SDB_FIELDP field = (SDB_FIELDP) cfl_list_get(pSDBArea->table->fields, i);
      fieldsOffsets[i] = offset;
      offset += fieldSize(field);
   }
   *recordLen = offset;
   SDB_LOG_DEBUG(("allocFieldOffsets=%d", *recordLen));
   return fieldsOffsets;
}

SDB_RECORDP sdb_record_new(SDB_AREAP pSDBArea) {
   SDB_RECORDP record;
   CFL_UINT32  *fieldsOffsets;
   CFL_UINT32  recordLen;

   SDB_LOG_DEBUG(("sdb_record_new: len=%d", pSDBArea->uiRecordLen));
   fieldsOffsets = allocFieldOffsets(pSDBArea, &recordLen);
   record = (SDB_RECORDP) SDB_MEM_ALLOC(sizeof(SDB_RECORD) + recordLen);
   record->fieldsOffsets = fieldsOffsets;
   record->objectType = SDB_OBJ_RECORD;
   record->ulRowNum = 0;
   record->isChanged = CFL_FALSE;
   record->isLocked = CFL_FALSE;
   record->table = pSDBArea->table;
   memset(record->data, 0, recordLen);
   initData(record);
   return record;
}

void sdb_record_free(SDB_RECORDP record) {
   if (record) {
      releaseData(record);
      SDB_MEM_FREE(record->fieldsOffsets);
      SDB_MEM_FREE(record);
   }
}

static HB_ERRCODE setFieldValue(SDB_RECORDP record, SDB_FIELDP field, PHB_ITEM pItem) {
   HB_ERRCODE errCode = HB_SUCCESS;

   SDB_LOG_TRACE(("setFieldValue: name=%s, length=%d, decimals=%d value=%s",
           cfl_str_getPtr(field->clpName),
           field->length,
           field->decimals,
           ITEM_STR(pItem)));

   switch (field->clpType) {
      case SDB_CLP_CHARACTER:
         if (HB_IS_STRING(pItem)) {
            char *data = FIELD_DATA_PTR(char *, record, field);
            CFL_UINT32 *strLen = (CFL_UINT32 *) data;
            char *strPtr = data + sizeof(CFL_UINT32);
            HB_SIZE len = hb_itemGetCLen(pItem);
            if (len >= (HB_SIZE) field->length) {
               *strLen = field->length;
               SDB_MEM_COPY(strPtr, hb_itemGetCPtr(pItem), field->length);
               strPtr[field->length] = '\0';
            } else {
               *strLen = (CFL_UINT32) len;
               SDB_MEM_COPY(strPtr, hb_itemGetCPtr(pItem), len);
               if (field->isRightPadded) {
                  SDB_MEM_SET(strPtr + len, ' ', field->length - len);
                  strPtr[field->length] = '\0';
               } else {
                  strPtr[len] = '\0';
               }
            }
            SDB_LOG_TRACE(("set str=%.*s", *strLen, strPtr));
         } else {
            errCode = EG_DATATYPE;
         }
         break;

      case SDB_CLP_LOGICAL:
         if (HB_IS_STRING(pItem)) {
            FIELD_DATA(char, record, field) = hb_strnicmp(hb_itemGetCPtr(pItem), "Y", 1) == 0 ? 'T' : 'F';
            SDB_LOG_TRACE(("set logical=%c", FIELD_DATA(char, record, field)));
         } else if (HB_IS_LOGICAL(pItem)) {
            FIELD_DATA(char, record, field) = hb_itemGetL(pItem) ? 'T' : 'F';
            SDB_LOG_TRACE(("set logical=%c", FIELD_DATA(char, record, field)));
         } else if (HB_IS_NUMERIC(pItem)) {
            FIELD_DATA(char, record, field) = hb_itemGetNI(pItem) == 0 ? 'F' : 'T';
            SDB_LOG_TRACE(("set logical=%c", FIELD_DATA(char, record, field)));
         } else {
            errCode = EG_DATATYPE;
         }
         break;

      case SDB_CLP_DATE:
         if (HB_IS_DATE(pItem)) {
            FIELD_DATA(long, record, field) = hb_itemGetDL(pItem);
            SDB_LOG_TRACE(("set date=%u", FIELD_DATA(long, record, field)));
         } else {
            errCode = EG_DATATYPE;
         }
         break;

      case SDB_CLP_INTEGER:
         if (HB_IS_NUMERIC(pItem)) {
            FIELD_DATA(CFL_INT32, record, field) = (CFL_INT32) hb_itemGetNLL(pItem);
            SDB_LOG_TRACE(("set int=%d", FIELD_DATA(CFL_INT32, record, field)));
         } else {
            errCode = EG_DATATYPE;
         }
         break;

      case SDB_CLP_BIGINT:
         if (HB_IS_NUMERIC(pItem)) {
            FIELD_DATA(CFL_INT64, record, field) = (CFL_INT64) hb_itemGetNLL(pItem);
            SDB_LOG_TRACE(("set long=%d", FIELD_DATA(CFL_INT64, record, field)));
         } else {
            errCode = EG_DATATYPE;
         }
         break;

      case SDB_CLP_FLOAT:
      case SDB_CLP_DOUBLE:
         if (HB_IS_NUMERIC(pItem)) {
            FIELD_DATA(double, record, field) = hb_itemGetND(pItem);
            SDB_LOG_TRACE(("set double=%f", FIELD_DATA(double, record, field)));
         } else {
            errCode = EG_DATATYPE;
         }
         break;

      case SDB_CLP_NUMERIC:
         if (HB_IS_NUMERIC(pItem)) {
            if (field->decimals > 0) {
               FIELD_DATA(double, record, field) = hb_itemGetND(pItem);
               SDB_LOG_TRACE(("set num=%f", FIELD_DATA(double, record, field)));
            } else if (field->length < 3) {
               FIELD_DATA(CFL_INT8, record, field) = (CFL_INT8) hb_itemGetNI(pItem);
               SDB_LOG_TRACE(("set num=%d", FIELD_DATA(CFL_INT8, record, field)));
            } else if (field->length < 5) {
               FIELD_DATA(CFL_INT16, record, field) = (CFL_INT16) hb_itemGetNI(pItem);
               SDB_LOG_TRACE(("set num=%d", FIELD_DATA(CFL_INT16, record, field)));
            } else if (field->length < 10) {
               FIELD_DATA(CFL_INT32, record, field) = (CFL_INT32) hb_itemGetNL(pItem);
               SDB_LOG_TRACE(("set num=%d", FIELD_DATA(CFL_INT32, record, field)));
            } else {
               FIELD_DATA(CFL_INT64, record, field) = (CFL_INT64) hb_itemGetNLL(pItem);
               SDB_LOG_TRACE(("set num=%d", FIELD_DATA(CFL_INT64, record, field)));
            }
         } else {
            errCode = EG_DATATYPE;
         }
         break;

#ifdef __HBR__
      case SDB_CLP_TIMESTAMP:
         if (HB_IS_TIMESTAMP(pItem)) {
            FIELD_DATA(double, record, field) = hb_itemGetTD(pItem);
            SDB_LOG_TRACE(("set timestamp: %f", FIELD_DATA(double, record, field)));
         } else {
            errCode = EG_DATATYPE;
         }
         break;
#endif
      case SDB_CLP_MEMO_LONG:
      case SDB_CLP_IMAGE:
      case SDB_CLP_LONG_RAW:
      case SDB_CLP_BLOB:
      case SDB_CLP_CLOB:
         if ((hb_itemType(pItem) & (HB_IT_STRING | HB_IT_MEMO | HB_IT_NUMERIC)))  {
            hb_itemCopy(FIELD_DATA(PHB_ITEM, record, field), pItem);
         } else {
            errCode = EG_DATATYPE;
         }
         break;

      case SDB_CLP_ROWID:
      case SDB_CLP_CURSOR:
         if (HB_IS_POINTER(pItem)) {
            hb_itemCopy(FIELD_DATA(PHB_ITEM, record, field), pItem);
         } else {
            errCode = EG_DATATYPE;
         }
         break;

      default:
         errCode = EG_DATATYPE;
         break;
   }
   return errCode;
}

static HB_ERRCODE setFieldEmpty(SDB_RECORDP record, SDB_FIELDP field) {
   HB_ERRCODE errCode = HB_SUCCESS;

   SDB_LOG_TRACE(("setFieldEmpty: name=%s", cfl_str_getPtr(field->clpName)));
   switch (field->clpType) {
      case SDB_CLP_CHARACTER:
         {
            char *data = FIELD_DATA_PTR(char *, record, field);
            CFL_UINT32 *strLen = (CFL_UINT32 *) data;
            char *strPtr = data + sizeof(CFL_UINT32);
            if (field->isRightPadded) {
              *strLen = field->length;
               SDB_MEM_SET(strPtr, ' ', field->length);
               strPtr[field->length] = '\0';
            } else {
               *strLen =0;
               strPtr[0] = '\0';
            }
         }
         break;

      case SDB_CLP_LOGICAL:
         FIELD_DATA(char, record, field) = 'F';
         break;

      case SDB_CLP_DATE:
         FIELD_DATA(long, record, field) = 0L;
         break;

      case SDB_CLP_INTEGER:
         FIELD_DATA(CFL_INT32, record, field) = 0;
         break;

      case SDB_CLP_BIGINT:
         FIELD_DATA(CFL_INT64, record, field) = 0;
         break;

      case SDB_CLP_FLOAT:
         FIELD_DATA(float, record, field) = 0.0;
         break;

      case SDB_CLP_DOUBLE:
         FIELD_DATA(double, record, field) = 0.0;
         break;

      case SDB_CLP_NUMERIC:
         if (field->decimals > 0) {
            FIELD_DATA(double, record, field) = 0.0;
         } else if (field->length < 3) {
            FIELD_DATA(CFL_INT8, record, field) = 0;
         } else if (field->length < 5) {
            FIELD_DATA(CFL_INT16, record, field) = 0;
         } else if (field->length < 10) {
            FIELD_DATA(CFL_INT32, record, field) = 0;
         } else {
            FIELD_DATA(CFL_INT64, record, field) = 0;
         }
         break;

      case SDB_CLP_TIMESTAMP:
         #ifdef __HBR__
            FIELD_DATA(long, record, field) = 0;
         #else
            FIELD_DATA(double, record, field) = 0.0;
         #endif
         break;

      case SDB_CLP_MEMO_LONG:
      case SDB_CLP_IMAGE:
      case SDB_CLP_LONG_RAW:
      case SDB_CLP_BLOB:
      case SDB_CLP_CLOB:
         hb_itemPutNI(FIELD_DATA(PHB_ITEM, record, field), 0);
         break;

      case SDB_CLP_ROWID:
      case SDB_CLP_CURSOR:
         hb_itemSetNil(FIELD_DATA(PHB_ITEM, record, field));
         break;

      default:
         errCode = EG_DATATYPE;
         break;
   }
   return errCode;
}

HB_ERRCODE sdb_record_setValue(SDB_RECORDP record, SDB_FIELDP field, PHB_ITEM pNewValue) {
   HB_ERRCODE errCode;

   if (pNewValue != NULL && ! HB_IS_NIL(pNewValue)) {
      errCode = setFieldValue(record, field, pNewValue);
   } else {
      errCode = setFieldEmpty(record, field);
   }
   return errCode;
}

HB_ERRCODE sdb_record_getValue(SDB_RECORDP record, SDB_FIELDP field, PHB_ITEM pItem, CFL_BOOL pad) {
   HB_ERRCODE errCode = HB_SUCCESS;

   SDB_LOG_TRACE(("sdb_record_getValue: name=%s type=%u length=%u decimals=%u pad=%s",
           cfl_str_getPtr(field->clpName),
           field->clpType,
           field->length,
           field->decimals,
           (pad ? "true" : "false")));

   switch (field->clpType) {
      case SDB_CLP_CHARACTER:
         {
            char *data = FIELD_DATA_PTR(char *, record, field);
            CFL_UINT32 strLen = *((CFL_UINT32 *) data);
            char *strPtr = data + sizeof(CFL_UINT32);

            hb_itemPutCL(pItem, strPtr, pad ? field->length : strLen);
         }
         break;

      case SDB_CLP_LOGICAL:
         hb_itemPutL(pItem, FIELD_DATA(char, record, field) == 'T');
         break;

      case SDB_CLP_DATE:
         hb_itemPutDL(pItem, FIELD_DATA(long, record, field));
         break;

      case SDB_CLP_INTEGER:
         hb_itemPutNLLen(pItem, (long) FIELD_DATA(CFL_INT32, record, field), 10);
         break;

      case SDB_CLP_BIGINT:
         hb_itemPutNLL(pItem, (HB_LONGLONG) FIELD_DATA(CFL_INT64, record, field));
         break;

      case SDB_CLP_FLOAT:
      case SDB_CLP_DOUBLE:
         hb_itemPutNDLen(pItem, FIELD_DATA(double, record, field), 20 - (field->decimals > 0 ? (field->decimals + 1) : 0), field->decimals);
         break;

      case SDB_CLP_NUMERIC:
         if (field->decimals > 0) {
            hb_itemPutNDLen(pItem, FIELD_DATA(double, record, field), (int)(field->length - field->decimals - 1), (int) field->decimals);
         } else if (field->length < 3) {
            hb_itemPutNILen(pItem, (int) FIELD_DATA(CFL_INT8, record, field), (int) field->length);
         } else if (field->length < 5) {
            hb_itemPutNILen(pItem, (int) FIELD_DATA(CFL_INT16, record, field), (int) field->length);
         } else if (field->length < 10) {
            hb_itemPutNLLen(pItem, (long) FIELD_DATA(CFL_INT32, record, field), (int) field->length);
         } else {
            hb_itemPutNLLLen(pItem, (HB_LONGLONG) FIELD_DATA(CFL_INT64, record, field), (int) field->length);
         }
         break;

#ifdef __HBR__
      case SDB_CLP_TIMESTAMP:
         hb_itemPutND(pItem, FIELD_DATA(double, record, field));
         break;
#endif
      case SDB_CLP_ROWID:
      case SDB_CLP_MEMO_LONG:
      case SDB_CLP_IMAGE:
      case SDB_CLP_LONG_RAW:
      case SDB_CLP_BLOB:
      case SDB_CLP_CLOB:
      case SDB_CLP_CURSOR:
         hb_itemCopy(pItem, FIELD_DATA(PHB_ITEM, record, field));
         break;

      default:
         errCode = EG_DATATYPE;
         break;
   }
   SDB_LOG_DEBUG(("sdb_record_getValue: %s=%s", cfl_str_getPtr(field->clpName), ITEM_STR(pItem)));
   return errCode;
}

CFL_BOOL sdb_record_getLogical(SDB_RECORDP record, SDB_FIELDP field) {
   ENTER_FUN_NAME("sdb_record_getLogical");
   SDB_LOG_DEBUG(("sdb_record_getLogical: name=%s type=%u", cfl_str_getPtr(field->clpName), field->clpType));
   switch (field->clpType) {
      case SDB_CLP_LOGICAL:
         RETURN FIELD_DATA(char, record, field) == 'T';

      case SDB_CLP_INTEGER:
         RETURN FIELD_DATA(CFL_INT32, record, field) > 0;

      case SDB_CLP_BIGINT:
         RETURN FIELD_DATA(CFL_INT64, record, field) > 0;

      case SDB_CLP_FLOAT:
      case SDB_CLP_DOUBLE:
         RETURN FIELD_DATA(double, record, field) > 0.0;

      case SDB_CLP_NUMERIC:
         if (field->decimals > 0) {
            RETURN FIELD_DATA(double, record, field) > 0.0;
         } else if (field->length < 3) {
            RETURN FIELD_DATA(CFL_INT8, record, field) > 0;
         } else if (field->length < 5) {
            RETURN FIELD_DATA(CFL_INT16, record, field) > 0;
         } else if (field->length < 10) {
            RETURN FIELD_DATA(CFL_INT32, record, field) > 0;
         } else {
            RETURN FIELD_DATA(CFL_INT64, record, field) > 0;
         }
   }
   RETURN CFL_FALSE;
}

HB_ERRCODE sdb_record_setLogical(SDB_RECORDP record, SDB_FIELDP field, CFL_BOOL value) {
   ENTER_FUN_NAME("sdb_record_setLogical");
   SDB_LOG_DEBUG(("sdb_record_setLogical: name=%s type=%u value=%s", cfl_str_getPtr(field->clpName), field->clpType, value ? "true" : "false"));
   switch (field->clpType) {
      case SDB_CLP_LOGICAL:
         FIELD_DATA(char, record, field) = value ? 'T' : 'F';
         break;

      case SDB_CLP_INTEGER:
         FIELD_DATA(CFL_INT32, record, field) = value ? 1 : 0;
         break;

      case SDB_CLP_BIGINT:
         FIELD_DATA(CFL_INT64, record, field) = value ? 1 : 0;
         break;

      case SDB_CLP_FLOAT:
      case SDB_CLP_DOUBLE:
         FIELD_DATA(double, record, field) = value ? 1.0 : 0.0;
         break;

      case SDB_CLP_NUMERIC:
         if (field->decimals > 0) {
            FIELD_DATA(double, record, field) = value ? 1.0 : 0.0;
         } else if (field->length < 3) {
            FIELD_DATA(CFL_INT8, record, field) = value ? 1 : 0;
         } else if (field->length < 5) {
            FIELD_DATA(CFL_INT16, record, field) = value ? 1 : 0;
         } else if (field->length < 10) {
            FIELD_DATA(CFL_INT32, record, field) = value ? 1 : 0;
         } else {
            FIELD_DATA(CFL_INT64, record, field) = value ? 1 : 0;
         }
         break;
      default:
         RETURN EG_ARRASSIGN;
   }
   RETURN HB_SUCCESS;
}

HB_ERRCODE sdb_record_setInt64(SDB_RECORDP record, SDB_FIELDP field, CFL_INT64 value) {
   ENTER_FUN_NAME("sdb_record_setInt64");
   SDB_LOG_DEBUG(("sdb_record_setInt64: name=%s type=%u value=%lld", cfl_str_getPtr(field->clpName), field->clpType, value));

   switch (field->clpType) {
      case SDB_CLP_NUMERIC:
         if (field->decimals > 0) {
            FIELD_DATA(double, record, field) = (double) value;
         } else if (field->length < 3) {
            FIELD_DATA(CFL_INT8, record, field) = (CFL_INT8) value;
         } else if (field->length < 5) {
            FIELD_DATA(CFL_INT16, record, field) = (CFL_INT16) value;
         } else if (field->length < 10) {
            FIELD_DATA(CFL_INT32, record, field) = (CFL_INT32) value;
         } else {
            FIELD_DATA(CFL_INT64, record, field) = (CFL_INT64) value;
         }
         break;
      case SDB_CLP_INTEGER:
         FIELD_DATA(CFL_INT32, record, field) = (CFL_INT32) value;
         break;
      case SDB_CLP_BIGINT:
         FIELD_DATA(CFL_INT64, record, field) = value;
         break;
      case SDB_CLP_DOUBLE:
      case SDB_CLP_FLOAT:
         FIELD_DATA(double, record, field) = (double) value;
         break;
      case SDB_CLP_MEMO_LONG:
         hb_itemPutNLL(FIELD_DATA(PHB_ITEM, record, field), (HB_LONGLONG) value);
         break;
      default:
         RETURN EG_ARRASSIGN;
   }
   RETURN HB_SUCCESS;
}

CFL_INT64 sdb_record_getInt64(SDB_RECORDP record, SDB_FIELDP field) {
   ENTER_FUN_NAME("sdb_record_getInt64");
   SDB_LOG_DEBUG(("sdb_record_getInt64: name=%s type=%u", cfl_str_getPtr(field->clpName), field->clpType));
   switch (field->clpType) {
      case SDB_CLP_NUMERIC:
         if (field->decimals > 0) {
            RETURN (CFL_INT64) FIELD_DATA(double, record, field);
         } else if (field->length < 3) {
            RETURN (CFL_INT64) FIELD_DATA(CFL_INT8, record, field);
         } else if (field->length < 5) {
            RETURN (CFL_INT64) FIELD_DATA(CFL_INT16, record, field);
         } else if (field->length < 10) {
            RETURN (CFL_INT64) FIELD_DATA(CFL_INT32, record, field);
         } else {
            RETURN (CFL_INT64) FIELD_DATA(CFL_INT64, record, field);
         }
      case SDB_CLP_INTEGER:
         RETURN (CFL_INT64) FIELD_DATA(CFL_INT32, record, field);
      case SDB_CLP_BIGINT:
         RETURN FIELD_DATA(CFL_INT64, record, field);
      case SDB_CLP_DOUBLE:
      case SDB_CLP_FLOAT:
         RETURN (CFL_INT64) FIELD_DATA(double, record, field);
   }
   RETURN 0;
}

HB_ERRCODE sdb_record_setInt32(SDB_RECORDP record, SDB_FIELDP field, CFL_INT32 value) {
   ENTER_FUN_NAME("sdb_record_setInt32");
   SDB_LOG_DEBUG(("sdb_record_setInt32: name=%s type=%u value=%ld", cfl_str_getPtr(field->clpName), field->clpType, value));
   switch (field->clpType) {
      case SDB_CLP_NUMERIC:
         if (field->decimals > 0) {
            FIELD_DATA(double, record, field) = (double) value;
         } else if (field->length < 3) {
            FIELD_DATA(CFL_INT8, record, field) = (CFL_INT8) value;
         } else if (field->length < 5) {
            FIELD_DATA(CFL_INT16, record, field) = (CFL_INT16) value;
         } else if (field->length < 10) {
            FIELD_DATA(CFL_INT32, record, field) = (CFL_INT32) value;
         } else {
            FIELD_DATA(CFL_INT64, record, field) = (CFL_INT64) value;
         }
         break;
      case SDB_CLP_INTEGER:
         FIELD_DATA(CFL_INT32, record, field) = value;
         break;
      case SDB_CLP_BIGINT:
         FIELD_DATA(CFL_INT64, record, field) = (CFL_INT64) value;
         break;
      case SDB_CLP_DOUBLE:
      case SDB_CLP_FLOAT:
         FIELD_DATA(double, record, field) = (double) value;
         break;
      case SDB_CLP_MEMO_LONG:
         hb_itemPutNL(FIELD_DATA(PHB_ITEM, record, field), (long) value);
         break;
      default:
         RETURN EG_ARRASSIGN;
   }
   RETURN HB_SUCCESS;
}

CFL_INT32 sdb_record_getInt32(SDB_RECORDP record, SDB_FIELDP field) {
   ENTER_FUN_NAME("sdb_record_getInt32");
   SDB_LOG_DEBUG(("sdb_record_getInt32: name=%s type=%u", cfl_str_getPtr(field->clpName), field->clpType));
   switch (field->clpType) {
      case SDB_CLP_NUMERIC:
         if (field->decimals > 0) {
            RETURN (CFL_INT32) FIELD_DATA(double, record, field);
         } else if (field->length < 3) {
            RETURN (CFL_INT32) FIELD_DATA(CFL_INT8, record, field);
         } else if (field->length < 5) {
            RETURN (CFL_INT32) FIELD_DATA(CFL_INT16, record, field);
         } else if (field->length < 10) {
            RETURN (CFL_INT32) FIELD_DATA(CFL_INT32, record, field);
         } else {
            RETURN (CFL_INT32) FIELD_DATA(CFL_INT64, record, field);
         }
      case SDB_CLP_INTEGER:
         RETURN FIELD_DATA(CFL_INT32, record, field);
      case SDB_CLP_BIGINT:
         RETURN (CFL_INT32) FIELD_DATA(CFL_INT64, record, field);
      case SDB_CLP_DOUBLE:
      case SDB_CLP_FLOAT:
         RETURN (CFL_INT32) FIELD_DATA(double, record, field);
   }
   RETURN 0;
}

HB_ERRCODE sdb_record_setInt16(SDB_RECORDP record, SDB_FIELDP field, CFL_INT16 value) {
   ENTER_FUN_NAME("sdb_record_setInt16");
   SDB_LOG_DEBUG(("sdb_record_setInt16: name=%s type=%u value=%d", cfl_str_getPtr(field->clpName), field->clpType, value));
   switch (field->clpType) {
      case SDB_CLP_NUMERIC:
         if (field->decimals > 0) {
            FIELD_DATA(double, record, field) = (double) value;
         } else if (field->length < 3) {
            FIELD_DATA(CFL_INT8, record, field) = (CFL_INT8) value;
         } else if (field->length < 5) {
            FIELD_DATA(CFL_INT16, record, field) = (CFL_INT16) value;
         } else if (field->length < 10) {
            FIELD_DATA(CFL_INT32, record, field) = (CFL_INT32) value;
         } else {
            FIELD_DATA(CFL_INT64, record, field) = (CFL_INT64) value;
         }
         break;
      case SDB_CLP_INTEGER:
         FIELD_DATA(CFL_INT32, record, field) = (CFL_INT32) value;
         break;
      case SDB_CLP_BIGINT:
         FIELD_DATA(CFL_INT64, record, field) = (CFL_INT64) value;
         break;
      case SDB_CLP_DOUBLE:
      case SDB_CLP_FLOAT:
         FIELD_DATA(double, record, field) = (double) value;
         break;
      default:
         RETURN EG_ARRASSIGN;
   }
   RETURN HB_SUCCESS;
}

CFL_INT16 sdb_record_getInt16(SDB_RECORDP record, SDB_FIELDP field) {
   ENTER_FUN_NAME("sdb_record_getIt16");
   SDB_LOG_DEBUG(("sdb_record_getInt16: name=%s type=%u", cfl_str_getPtr(field->clpName), field->clpType));
   switch (field->clpType) {
      case SDB_CLP_NUMERIC:
         if (field->decimals > 0) {
            return (CFL_INT16) FIELD_DATA(double, record, field);
         } else if (field->length < 3) {
            return (CFL_INT16) FIELD_DATA(CFL_INT8, record, field);
         } else if (field->length < 5) {
            return (CFL_INT16) FIELD_DATA(CFL_INT16, record, field);
         } else if (field->length < 10) {
            return (CFL_INT16) FIELD_DATA(CFL_INT32, record, field);
         } else {
            return (CFL_INT16) FIELD_DATA(CFL_INT64, record, field);
         }
      case SDB_CLP_INTEGER:
         return (CFL_INT16) FIELD_DATA(CFL_INT32, record, field);
      case SDB_CLP_BIGINT:
         return (CFL_INT16) FIELD_DATA(CFL_INT64, record, field);
      case SDB_CLP_DOUBLE:
      case SDB_CLP_FLOAT:
         return (CFL_INT16) FIELD_DATA(double, record, field);
   }
   return 0;
}

HB_ERRCODE sdb_record_setDouble(SDB_RECORDP record, SDB_FIELDP field, double value) {
   ENTER_FUN_NAME("sdb_record_setDouble");
   SDB_LOG_DEBUG(("sdb_record_setDouble: name=%s type=%u value=%f", cfl_str_getPtr(field->clpName), field->clpType, value));
   switch (field->clpType) {
      case SDB_CLP_NUMERIC:
         if (field->decimals > 0) {
            FIELD_DATA(double, record, field) = value;
         } else if (field->length < 3) {
            FIELD_DATA(CFL_INT8, record, field) = (CFL_INT8) value;
         } else if (field->length < 5) {
            FIELD_DATA(CFL_INT16, record, field) = (CFL_INT16) value;
         } else if (field->length < 10) {
            FIELD_DATA(CFL_INT32, record, field) = (CFL_INT32) value;
         } else {
            FIELD_DATA(CFL_INT64, record, field) = (CFL_INT64) value;
         }
         break;
      case SDB_CLP_INTEGER:
         FIELD_DATA(CFL_INT32, record, field) = (CFL_INT32) value;
         break;
      case SDB_CLP_BIGINT:
         FIELD_DATA(CFL_INT64, record, field) = (CFL_INT64) value;
         break;
      case SDB_CLP_DOUBLE:
      case SDB_CLP_FLOAT:
         FIELD_DATA(double, record, field) = value;
         break;
      case SDB_CLP_MEMO_LONG:
         hb_itemPutND(FIELD_DATA(PHB_ITEM, record, field), value);
         break;
      default:
         RETURN EG_ARRASSIGN;
   }
   RETURN HB_SUCCESS;
}

double sdb_record_getDouble(SDB_RECORDP record, SDB_FIELDP field) {
   ENTER_FUN_NAME("sdb_record_getDouble");
   SDB_LOG_DEBUG(("sdb_record_getDouble: name=%s type=%u", cfl_str_getPtr(field->clpName), field->clpType));
   switch (field->clpType) {
      case SDB_CLP_NUMERIC:
         if (field->decimals > 0) {
            RETURN (double) FIELD_DATA(double, record, field);
         } else if (field->length < 3) {
            RETURN (double) FIELD_DATA(CFL_INT8, record, field);
         } else if (field->length < 5) {
            RETURN (double) FIELD_DATA(CFL_INT16, record, field);
         } else if (field->length < 10) {
            RETURN (double) FIELD_DATA(CFL_INT32, record, field);
         } else {
            RETURN (double) FIELD_DATA(CFL_INT64, record, field);
         }
      case SDB_CLP_INTEGER:
         RETURN (double) FIELD_DATA(CFL_INT32, record, field);
      case SDB_CLP_BIGINT:
         RETURN (double) FIELD_DATA(CFL_INT64, record, field);
      case SDB_CLP_DOUBLE:
      case SDB_CLP_FLOAT:
         RETURN FIELD_DATA(double, record, field);
   }
   RETURN 0.0;
}

HB_ERRCODE sdb_record_setString(SDB_RECORDP record, SDB_FIELDP field, const char *str, CFL_UINT32 len) {
   char *data = FIELD_DATA_PTR(char *, record, field);
   CFL_UINT32 *strLen = (CFL_UINT32 *) data;
   char *strPtr = data + sizeof(CFL_UINT32);

   SDB_LOG_DEBUG(("sdb_record_setString: name=%s type=%u value='%.*s'", cfl_str_getPtr(field->clpName), field->clpType, len, str));
   if (str) {
      if (len >= (HB_SIZE) field->length) {
         *strLen = field->length;
         SDB_MEM_COPY(strPtr, str, field->length);
         strPtr[field->length] = '\0';
      } else {
         *strLen = len;
         SDB_MEM_COPY(strPtr, str, len);
         if (field->isRightPadded) {
            SDB_MEM_SET(strPtr + len, ' ', field->length - len);
            strPtr[field->length] = '\0';
         } else {
            strPtr[len] = '\0';
         }
      }

   } else if (field->isRightPadded) {
      *strLen = field->length;
      SDB_MEM_SET(strPtr, ' ', field->length);
      strPtr[field->length] = '\0';
   } else {
      *strLen = 0;
      strPtr[0] = '\0';
   }
   return HB_SUCCESS;
}

const char * sdb_record_getString(SDB_RECORDP record, SDB_FIELDP field) {
   SDB_LOG_DEBUG(("sdb_record_getString: name=%s type=%u", cfl_str_getPtr(field->clpName), field->clpType));
   return FIELD_DATA_PTR(char *, record, field) + sizeof(CFL_UINT32);
}

HB_ERRCODE sdb_record_setDate(SDB_RECORDP record, SDB_FIELDP field, CFL_UINT16 year, CFL_UINT8 month, CFL_UINT8 day) {
   FIELD_DATA(long, record, field) = hb_dateEncode((int) year, (int) month, (int)day);
   return HB_SUCCESS;
}

long sdb_record_getDate(SDB_RECORDP record, SDB_FIELDP field, CFL_UINT16 *year, CFL_UINT8 *month, CFL_UINT8 *day) {
   long date;
   SDB_LOG_DEBUG(("sdb_record_getDate: name=%s type=%u", cfl_str_getPtr(field->clpName), field->clpType));
   date = FIELD_DATA(long, record, field);
   if (year && month && day) {
      hb_dateDecode(date, (int *)year, (int *)month, (int *)day);
   }
   return date;
}

HB_ERRCODE sdb_record_setTimestamp(SDB_RECORDP record, SDB_FIELDP field, CFL_UINT16 year, CFL_UINT8 month, CFL_UINT8 day, CFL_UINT8 hour, CFL_UINT8 min, CFL_UINT8 sec, CFL_UINT32 mSec) {
#ifdef __XHB__
   HB_SYMBOL_UNUSED( hour );
   HB_SYMBOL_UNUSED( min );
   HB_SYMBOL_UNUSED( sec );
   HB_SYMBOL_UNUSED( mSec );
   FIELD_DATA(double, record, field) = (double) hb_dateEncode((int)year, (int)month, (int)day);
#else
   FIELD_DATA(double, record, field) = hb_timeStampPack((int)year, (int)month, (int)day, (int)hour, (int)min, (int)sec, (int)mSec);
#endif
   return HB_SUCCESS;
}

double sdb_record_getTimestamp(SDB_RECORDP record, SDB_FIELDP field, CFL_UINT16 *year, CFL_UINT8 *month, CFL_UINT8 *day, CFL_UINT8 *hour, CFL_UINT8 *min, CFL_UINT8 *sec, CFL_UINT32 *mSec) {
   double timestamp = FIELD_DATA(double, record, field);
   if (year && month && day && hour && min && sec && mSec) {
#ifdef __XHB__
      hb_dateDecode((long) timestamp, (int *)year, (int *)month, (int *)day);
      *hour = *min = *sec = *mSec = 0;
#else
      hb_timeStampUnpack(timestamp, (int *)year, (int *)month, (int *)day, (int *)hour, (int *)min, (int *)sec, (int *)mSec);
#endif
   }
   return timestamp;
}

HB_ERRCODE sdb_record_setItem(SDB_RECORDP record, SDB_FIELDP field, PHB_ITEM item) {
   SDB_LOG_DEBUG(("sdb_record_setItem: name=%s type=%u", cfl_str_getPtr(field->clpName), field->clpType));
   hb_itemCopy(FIELD_DATA(PHB_ITEM, record, field), item);
   return HB_SUCCESS;
}

PHB_ITEM sdb_record_getItem(SDB_RECORDP record, SDB_FIELDP field) {
   ENTER_FUN_NAME("sdb_record_getItem");
   SDB_LOG_DEBUG(("sdb_record_getItem: name=%s type=%u", cfl_str_getPtr(field->clpName), field->clpType));
   RETURN FIELD_DATA(PHB_ITEM, record, field);
}
