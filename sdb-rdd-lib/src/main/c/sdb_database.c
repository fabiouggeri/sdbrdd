#include "sdb_database.h"
#include "sdb_schema.h"
#include "sdb_util.h"
#include "sdb_log.h"


#define sdb_database_lock(d)   cfl_lock_acquire(&(d)->lock)
#define sdb_database_unlock(d) cfl_lock_release(&(d)->lock)

SDB_DATABASEP sdb_database_new(const char *name, CFL_UINT32 nameLen) {
   SDB_DATABASEP db;
   ENTER_FUN;
   db = SDB_MEM_ALLOC(sizeof(SDB_DATABASE));
   cfl_lock_init(&(db->lock));
   db->objectType = SDB_OBJ_DATABASE;
   db->name = cfl_str_toUpper(cfl_str_trim(cfl_str_newBufferLen(name, nameLen)));
   db->schemas = cfl_list_new(6);
   RETURN db;
}

void sdb_database_free(SDB_DATABASEP db) {
   ENTER_FUN;
   if (db) {
      CFL_UINT32 len;
      CFL_UINT32 i;
      sdb_database_lock(db);
      len = cfl_list_length(db->schemas);
      for (i = 0; i < len; i++) {
         sdb_schema_free((SDB_SCHEMAP) cfl_list_get(db->schemas, i));
      }
      cfl_str_free(db->name);
      cfl_list_free(db->schemas);
      sdb_database_unlock(db);
      cfl_lock_free(&db->lock);
      SDB_MEM_FREE(db);
   }
   RETURN;
}

void sdb_database_addSchema(SDB_DATABASEP db, SDB_SCHEMAP schema) {
   ENTER_FUN;
   sdb_database_lock(db);
   cfl_list_add(db->schemas, schema);
   sdb_database_unlock(db);
   RETURN;
}

static SDB_SCHEMAP findSchema(SDB_DATABASEP db, const char *schemaName) {
   CFL_UINT32 len;
   CFL_UINT32 i;

   ENTER_FUN;
   len = cfl_list_length(db->schemas);
   for (i = 0; i < len; i++) {
      SDB_SCHEMAP schema = (SDB_SCHEMAP) cfl_list_get(db->schemas, i);
      if (cfl_str_bufferEqualsIgnoreCase(schema->name, schemaName)) {
         RETURN schema;
      }
   }
   RETURN NULL;
}

SDB_SCHEMAP sdb_database_getSchema(SDB_DATABASEP db, const char *schemaName) {
   SDB_SCHEMAP schema;

   ENTER_FUN;
   sdb_database_lock(db);
   schema = findSchema(db, schemaName);
   sdb_database_unlock(db);
   RETURN schema;
}

SDB_SCHEMAP sdb_database_delSchema(SDB_DATABASEP db, const char *schemaName) {
   CFL_UINT32 len;
   CFL_UINT32 i;

   ENTER_FUN;
   sdb_database_lock(db);
   len = cfl_list_length(db->schemas);
   for (i = 0; i < len; i++) {
      SDB_SCHEMAP schema = (SDB_SCHEMAP) cfl_list_get(db->schemas, i);
      if (cfl_str_bufferEqualsIgnoreCase(schema->name, schemaName)) {
         cfl_list_del(db->schemas, i);
         sdb_database_unlock(db);
         RETURN schema;
      }
   }
   sdb_database_unlock(db);
   RETURN NULL;
}

SDB_SCHEMAP sdb_database_getCreateSchema(SDB_DATABASEP db, const char* schemaName){
   SDB_SCHEMAP schema;

   ENTER_FUN;
   sdb_database_lock(db);
   schema = findSchema(db, schemaName);
   if (schema == NULL) {
      schema = sdb_schema_new(schemaName, (CFL_UINT32) strlen(schemaName));
      cfl_list_add(db->schemas, schema);
   }
   sdb_database_unlock(db);
   RETURN schema;
}
