#include "sdb_table.h"
#include "sdb_schema.h"
#include "sdb_field.h"
#include "sdb_index.h"
#include "sdb_util.h"
#include "sdb_log.h"

SDB_TABLEP sdb_table_new(SDB_SCHEMAP dbSchema, const char * clpTableName, CFL_UINT32 clpTabNameLen, const char *dbTableName, CFL_UINT32 dbTabNameLen) {
   SDB_TABLEP table;
   ENTER_FUN;
   table = (SDB_TABLEP) SDB_MEM_ALLOC(sizeof(SDB_TABLE));
   table->objectType = SDB_OBJ_TABLE;
   cfl_lock_init(&(table->lock));
   table->clpName = cfl_str_toUpper(cfl_str_newBufferLen(clpTableName, clpTabNameLen));
   table->dbSchema = dbSchema;
   table->clpSchema = NULL;
   if (dbTableName) {
      table->dbName = cfl_str_toUpper(cfl_str_newBufferLen(dbTableName, dbTabNameLen));
   } else {
      table->dbName = cfl_str_newStr(table->clpName);
   }
   table->hintAsc = NULL;
   table->hintDesc = NULL;
   table->dataLen = 0;
   table->fields = cfl_list_new(20);
   table->rowIdField = NULL;
   table->pkField = NULL;
   table->delField = NULL;
   table->indexes = cfl_list_new(5);
   table->locked = CFL_FALSE;
   table->isIndexExpressionsUpdated = CFL_FALSE;
   table->isContextualized = CFL_FALSE;
   table->nextRecno = 1;
   table->maxRecno = 0;
   table->bufferFetchSize = 0;
   RETURN table;
}

void sdb_table_free(SDB_TABLEP table) {
   ENTER_FUN;
   if (table) {
      CFL_UINT32 len;
      CFL_UINT32 i;

      cfl_str_free(table->clpName);
      cfl_str_free(table->dbName);
      if (table->hintAsc) {
         cfl_str_free(table->hintAsc);
      }
      if (table->hintDesc) {
         cfl_str_free(table->hintDesc);
      }
      len = cfl_list_length(table->indexes);
      for (i = 0; i < len; i++) {
         sdb_index_free((SDB_INDEXP) cfl_list_get(table->indexes, i));
      }
      len = cfl_list_length(table->fields);
      for (i = 0; i < len; i++) {
         sdb_field_free((SDB_FIELDP) cfl_list_get(table->fields, i));
      }
      cfl_list_free(table->fields);
      cfl_list_free(table->indexes);
      SDB_MEM_FREE(table);
   }
   RETURN;
}

void sdb_table_setHintAsc(SDB_TABLEP table, const char *hint) {
   ENTER_FUN;
   if (table->hintAsc) {
      cfl_str_setValue(table->hintAsc, hint);
   } else if(hint) {
      table->hintAsc = cfl_str_newBuffer(hint);
   }
   RETURN;
}

void sdb_table_setHintDesc(SDB_TABLEP table, const char *hint) {
   ENTER_FUN;
   if (table->hintDesc) {
      cfl_str_setValue(table->hintDesc, hint);
   } else if(hint) {
      table->hintDesc = cfl_str_newBuffer(hint);
   }
   RETURN;
}


void sdb_table_addField(SDB_TABLEP table, SDB_FIELDP field) {
   ENTER_FUN;
   SDB_LOG_DEBUG(("name=%s", cfl_str_getPtr(field->clpName)));
   if (field->order == 0) {
      CFL_UINT16 dataOrder;
      CFL_UINT16 indOrder;
      sdb_table_maxOrder(table, &dataOrder, &indOrder);
      switch (field->fieldType) {
         case SDB_FIELD_INDEX:
            field->order = indOrder + 1;
            break;
         case SDB_FIELD_PK:
            field->order = SDB_FIELD_PK_ORDER;
            break;
         case SDB_FIELD_DEL_FLAG:
            field->order = SDB_FIELD_DEL_ORDER;
            break;
         case SDB_FIELD_ROWID:
            field->order = SDB_FIELD_ROWID_ORDER;
            break;
         case SDB_FIELD_DATA:
         case SDB_FIELD_DATA_INDEX:
         default:
            field->order = dataOrder + 1;
            break;
      }
   }
   cfl_list_add(table->fields, field);
   field->tablePos = (CFL_UINT16) cfl_list_length(table->fields);
   switch (field->fieldType) {
      case SDB_FIELD_ROWID:
         table->rowIdField = field;
         break;
      case SDB_FIELD_PK:
         table->pkField = field;
         break;
      case SDB_FIELD_DEL_FLAG:
         table->delField = field;
         break;
   }
   RETURN;
}

SDB_FIELDP sdb_table_getField(SDB_TABLEP table, const char *fieldName) {
   CFL_UINT32 len;
   CFL_UINT32 i;

   ENTER_FUN;
   len = cfl_list_length(table->fields);
   for (i = 0; i < len; i++) {
      SDB_FIELDP field = (SDB_FIELDP) cfl_list_get(table->fields, i);
      if (cfl_str_bufferEqualsIgnoreCase(field->clpName, fieldName)) {
         RETURN field;
      }
   }
   RETURN NULL;
}

SDB_FIELDP sdb_table_delField(SDB_TABLEP table, const char *fieldName) {
   CFL_UINT32 len;
   CFL_UINT32 i;

   ENTER_FUN;
   len = cfl_list_length(table->fields);
   for (i = 0; i < len; i++) {
      SDB_FIELDP field = (SDB_FIELDP) cfl_list_get(table->fields, i);
      if (cfl_str_bufferEqualsIgnoreCase(field->clpName, fieldName)) {
         cfl_list_del(table->fields, i);
         RETURN field;
      }
   }
   RETURN NULL;
}


void sdb_table_addIndex(SDB_TABLEP table, SDB_INDEXP index) {
   ENTER_FUN;
   cfl_list_add(table->indexes, index);
   RETURN;
}

SDB_INDEXP sdb_table_getIndex(SDB_TABLEP table, const char *indexName) {
   CFL_UINT32 len;
   CFL_UINT32 i;

   ENTER_FUN;
   len = cfl_list_length(table->indexes);
   for (i = 0; i < len; i++) {
      SDB_INDEXP index = (SDB_INDEXP) cfl_list_get(table->indexes, i);
      if (cfl_str_bufferEqualsIgnoreCase(index->field->clpIndexName, indexName)) {
         RETURN index;
      }
   }
   RETURN NULL;
}

SDB_INDEXP sdb_table_delIndex(SDB_TABLEP table, const char *indexName) {
   CFL_UINT32 len;
   CFL_UINT32 i;

   ENTER_FUN;
   len = cfl_list_length(table->indexes);
   for (i = 0; i < len; i++) {
      SDB_INDEXP index = (SDB_INDEXP) cfl_list_get(table->indexes, i);
      if (cfl_str_bufferEqualsIgnoreCase(index->field->clpIndexName, indexName)) {
         CFL_STR_FREE(index->field->clpIndexName)
         CFL_STR_FREE(index->field->dbIndexName)
         CFL_STR_FREE(index->field->indexAscHint)
         CFL_STR_FREE(index->field->indexDescHint)
         CFL_STR_FREE(index->field->clpExpression)
         cfl_list_del(table->indexes, i);
         RETURN index;
      }
   }
   RETURN NULL;
}

CFL_UINT16 sdb_table_nextColOrder(SDB_TABLEP table, SDB_FIELDP field) {
   CFL_UINT16 colOrder;
   CFL_UINT32 len;
   CFL_UINT32 i;

   ENTER_FUN;
   switch (field->fieldType) {
      case SDB_FIELD_PK:
         RETURN 10000;
      case SDB_FIELD_DEL_FLAG:
         RETURN 10001;
      case SDB_FIELD_INDEX:
         colOrder = 5000;
         break;
      default:
         colOrder = 0;
   }
   len = cfl_list_length(table->fields);
   for (i = 0; i < len; i++) {
      SDB_FIELDP tabField = (SDB_FIELDP) cfl_list_get(table->fields, i);
      if (field->fieldType == tabField->fieldType) {
         if (tabField->order > colOrder) {
            colOrder = tabField->order;
         }
      }
   }
   RETURN colOrder + 1;
}

SDB_FIELDP sdb_table_getFieldByDBName(SDB_TABLEP table, const char *name) {
   CFL_UINT32 len;
   CFL_UINT32 i;
   len = cfl_list_length(table->fields);
   for (i = 0; i < len; i++) {
      SDB_FIELDP tabField = (SDB_FIELDP) cfl_list_get(table->fields, i);
      if (cfl_str_bufferEqualsIgnoreCase(tabField->dbName, name)) {
         return tabField;
      }
   }
   return NULL;
}

void sdb_table_maxOrder(SDB_TABLEP table, CFL_UINT16 *dataOrder, CFL_UINT16 *indOrder) {
   CFL_UINT32 len;
   CFL_UINT32 i;
   CFL_UINT16 dOrder = 1;
   CFL_UINT16 iOrder = 5000;
   len = cfl_list_length(table->fields);
   for (i = 0; i < len; i++) {
      SDB_FIELDP tabField = (SDB_FIELDP) cfl_list_get(table->fields, i);
      switch (tabField->fieldType){
         case SDB_FIELD_INDEX:
            if (tabField->order > iOrder) {
               iOrder = tabField->order;
            }
            break;
         case SDB_FIELD_DATA:
         case SDB_FIELD_DATA_INDEX:
            if (tabField->order > dOrder) {
               dOrder = tabField->order;
            }
            break;
      }
   }
   *dataOrder = dOrder;
   *indOrder = iOrder;
}

void sdb_table_lock(SDB_TABLEP table) {
   ENTER_FUN_NAME("sdb_table_lock");
   cfl_lock_acquire(&table->lock);
   RETURN;
}

void sdb_table_unlock(SDB_TABLEP table) {
   ENTER_FUN_NAME("sdb_table_unlock");
   cfl_lock_release(&table->lock);
   RETURN;
}
