#include "hbapiitm.h"

#include "cfl_str.h"

#include "sdb_index.h"
#include "sdb_field.h"
#include "sdb_util.h"
#include "sdb_log.h"

SDB_INDEXP sdb_index_new(SDB_FIELDP field, const char *clpIndexName, CFL_UINT32 clpNameLen, const char *dbIndexName, CFL_UINT32 dbNameLen, const char *clpIndexExpr, CFL_UINT32 clpExprLen, const char *hint, CFL_UINT32 hintLen) {
   SDB_INDEXP index;
   if (clpIndexName) {
      index = SDB_MEM_ALLOC(sizeof(SDB_INDEX));
      index->objectType = SDB_OBJ_INDEX;
      index->field = field;
      field->clpIndexName = cfl_str_newBufferLen(clpIndexName, clpNameLen);
      field->dbIndexName = dbIndexName ? cfl_str_newBufferLen(dbIndexName, dbNameLen) : cfl_str_newStr(field->clpIndexName);
      field->clpExpression = clpIndexExpr ? cfl_str_newBufferLen(clpIndexExpr, clpExprLen) : NULL;
      field->indexAscHint = hint ? cfl_str_newBufferLen(hint, hintLen) : NULL;
      index->compiledExpr = NULL;
   } else {
      index = NULL;
   }
   return index;
}


void sdb_index_free(SDB_INDEXP index) {
   if (index) {
      hb_itemRelease(index->compiledExpr);
      SDB_MEM_FREE(index);
   }
}
