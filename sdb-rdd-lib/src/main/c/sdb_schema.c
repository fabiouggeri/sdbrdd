#include "sdb_schema.h"
#include "sdb_table.h"
#include "sdb_dict.h"
#include "sdb_util.h"
#include "sdb_log.h"

#define sdb_schema_lock(s)  cfl_lock_acquire(&(s)->lock)
#define sdb_schema_unlock(s) cfl_lock_release(&(s)->lock)

SDB_SCHEMAP sdb_schema_new(const char *name, CFL_UINT32 nameLen) {
   SDB_SCHEMAP schema;
   ENTER_FUN_NAME("sdb_schema_new");
   schema = SDB_MEM_ALLOC(sizeof(SDB_SCHEMA));
   cfl_lock_init(&(schema->lock));
   schema->objectType = SDB_OBJ_SCHEMA;
   schema->name = cfl_str_toUpper(cfl_str_trim(cfl_str_newBufferLen(name, (size_t) nameLen)));
   schema->tables = cfl_list_new(50);
   schema->dict = NULL;
   RETURN schema;
}

void sdb_schema_free(SDB_SCHEMAP schema) {
   ENTER_FUN_NAME("sdb_schema_free");
   if (schema) {
      CFL_UINT32 len;
      CFL_UINT32 i;

      sdb_schema_lock(schema);
      len = cfl_list_length(schema->tables);
      for (i = 0; i < len; i++) {
         sdb_table_free((SDB_TABLEP) cfl_list_get(schema->tables, i));
      }
      cfl_str_free(schema->name);
      cfl_list_free(schema->tables);
      sdb_schema_unlock(schema);
      cfl_lock_free(&schema->lock);
      SDB_MEM_FREE(schema);
   }
   RETURN;
}

void sdb_schema_addTable(SDB_SCHEMAP schema, SDB_TABLEP table) {
   ENTER_FUN_NAME("sdb_schema_addTable");
   if (table->clpSchema) {
      sdb_schema_delTable(table->clpSchema, cfl_str_getPtr(table->clpName));
   }
   sdb_schema_lock(schema);
   cfl_list_add(schema->tables, table);
   table->clpSchema = schema;
   sdb_schema_unlock(schema);
   RETURN;
}

SDB_TABLEP sdb_schema_getTable(SDB_SCHEMAP schema, const char *tableName) {
   CFL_UINT32 len;
   CFL_UINT32 i;

   ENTER_FUN_NAME("sdb_schema_getTable");
   sdb_schema_lock(schema);
   len = cfl_list_length(schema->tables);
   for (i = 0; i < len; i++) {
      SDB_TABLEP table = (SDB_TABLEP) cfl_list_get(schema->tables, i);
      if (cfl_str_bufferEqualsIgnoreCase(table->clpName, tableName)) {
         sdb_schema_unlock(schema);
         RETURN table;
      }
   }
   sdb_schema_unlock(schema);
   RETURN NULL;
}

SDB_TABLEP sdb_schema_delTable(SDB_SCHEMAP schema, const char *tableName) {
   CFL_UINT32 len;
   CFL_UINT32 i;

   ENTER_FUN_NAME("sdb_schema_delTable");
   sdb_schema_lock(schema);
   len = cfl_list_length(schema->tables);
   for (i = 0; i < len; i++) {
      SDB_TABLEP table = (SDB_TABLEP) cfl_list_get(schema->tables, i);
      if (cfl_str_bufferEqualsIgnoreCase(table->clpName, tableName)) {
         cfl_list_del(schema->tables, i);
         sdb_schema_unlock(schema);
         RETURN table;
      }
   }
   sdb_schema_unlock(schema);
   RETURN NULL;
}
