#include "sdb_field.h"
#include "sdb_util.h"
#include "sdb_log.h"

SDB_FIELDP sdb_field_new(const char *clpColName, CFL_UINT32 clpColNameLen, const char *dbColName, CFL_UINT32 dbColNameLen,
                         CFL_UINT8 fieldType, CFL_UINT8 clpType, CFL_UINT32 clpDataLen, CFL_UINT8 clpDataDec, CFL_BOOL bRightPadded) {
   SDB_FIELDP field = SDB_MEM_ALLOC(sizeof(SDB_FIELD));
   field->objectType = SDB_OBJ_FIELD;
   field->clpName = cfl_str_toUpper(cfl_str_trim(cfl_str_newBufferLen(clpColName, clpColNameLen)));
   if (dbColName) {
      field->dbName = cfl_str_toUpper(cfl_str_trim(cfl_str_newBufferLen(dbColName, dbColNameLen)));
   } else {
      field->dbName = field->clpName;
   }
   field->fieldType = fieldType;
   field->clpType = clpType;
   field->tablePos = 0;
   field->length = clpDataLen;
   field->decimals = clpDataDec;
   field->setMode = SET_MODE_CLIENT;
   field->dbExpression = NULL;
   field->isVirtual = CFL_FALSE;
   field->isRightPadded = bRightPadded;
   field->clpIndexName = NULL;
   field->dbIndexName = NULL;
   field->indexAscHint = NULL;
   field->indexDescHint = NULL;
   field->clpExpression = NULL;
   field->order = 0;
   field->contextPos = 0;
   field->contextVal = NULL;
   return field;
}

void sdb_field_free(SDB_FIELDP field) {
   if (field) {
      if (field->dbName != field->clpName) {
         cfl_str_free(field->dbName);
      }
      cfl_str_free(field->clpName);
      if (field->dbExpression) {
         cfl_str_free(field->dbExpression);
      }
      if (field->clpIndexName) {
         cfl_str_free(field->clpIndexName);
      }
      if (field->dbIndexName) {
         cfl_str_free(field->dbIndexName);
      }
      if (field->indexAscHint) {
         cfl_str_free(field->indexAscHint);
      }
      if (field->indexDescHint) {
         cfl_str_free(field->indexDescHint);
      }
      if (field->clpExpression) {
         cfl_str_free(field->clpExpression);
      }
      if (field->contextVal) {
         cfl_str_free(field->contextVal);
      }
      SDB_MEM_FREE(field);
   }
}

CFL_BOOL sdb_field_isMemo(SDB_FIELDP field) {
   switch(field->clpType) {
      case SDB_CLP_MEMO_LONG:
      case SDB_CLP_IMAGE:
      case SDB_CLP_LONG_RAW:
      case SDB_CLP_BLOB:
      case SDB_CLP_CLOB:
         return CFL_TRUE;
   }
   return CFL_FALSE;
}
