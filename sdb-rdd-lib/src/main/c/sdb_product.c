#include "sdb_product.h"
#include "sdb_database.h"
#include "sdb_expr.h"
#include "sdb_api.h"
#include "sdb_util.h"
#include "sdb_log.h"
#include "sdb_dict.h"

SDB_PRODUCTP sdb_product_new(const char *name, const char *displayName, SDB_DB_APIP dbAPI) {
   SDB_PRODUCTP product;

   ENTER_FUN;
   product = (SDB_PRODUCTP) SDB_MEM_ALLOC(sizeof(SDB_PRODUCT));
   product->objectType = SDB_OBJ_PRODUCT;
   product->name = cfl_str_toUpper(cfl_str_trim(cfl_str_newBuffer(name)));
   product->displayName = cfl_str_trim(cfl_str_newBuffer(displayName));
   product->databases = cfl_list_new(3);
   product->translations = cfl_list_new(10);
   product->dbAPI = dbAPI;
   product->pkName = NULL;
   product->pkDefaultExpr = NULL;
   product->delName = NULL;
   RETURN product;
}

static void freeTranslations(SDB_PRODUCTP product) {
   CFL_UINT32 len = cfl_list_length(product->translations);
   CFL_UINT32 i;
   for (i = 0; i < len; i++) {
      sdb_expr_translationFree((SDB_EXPRESSION_TRANSLATIONP) cfl_list_get(product->translations, i));
   }
   cfl_list_free(product->translations);
}


static void freeDatabases(SDB_PRODUCTP product) {
   CFL_UINT32 len;
   CFL_UINT32 i;

   len = cfl_list_length(product->databases);
   for (i = 0; i < len; i++) {
      sdb_database_free((SDB_DATABASEP) cfl_list_get(product->databases, i));
   }
   cfl_list_free(product->databases);
}


void sdb_product_free(SDB_PRODUCTP product) {
   ENTER_FUN;
   if (product) {
      freeDatabases(product);
      freeTranslations(product);
      cfl_str_free(product->name);
      cfl_str_free(product->displayName);
      if (product->pkName) {
         cfl_str_free(product->pkName);
      }
      if (product->pkDefaultExpr) {
         cfl_str_free(product->pkDefaultExpr);
      }
      if (product->delName) {
         cfl_str_free(product->delName);
      }
      SDB_MEM_FREE(product);
   }
   RETURN;
}

void sdb_product_addDatabase(SDB_PRODUCTP product, SDB_DATABASEP database) {
   ENTER_FUN;
   cfl_list_add(product->databases, database);
   RETURN;
}

SDB_DATABASEP sdb_product_getDatabase(SDB_PRODUCTP product, const char *id) {
   CFL_UINT32 len;
   CFL_UINT32 i;

   ENTER_FUN;
   len = cfl_list_length(product->databases);
   for (i = 0; i < len; i++) {
      SDB_DATABASEP db = (SDB_DATABASEP) cfl_list_get(product->databases, i);
      if (cfl_str_bufferEqualsIgnoreCase(db->name, id)) {
         RETURN db;
      }
   }
   RETURN NULL;
}

SDB_DATABASEP sdb_product_getCreateDatabase(SDB_PRODUCTP product, const char *id) {
   SDB_DATABASEP db;

   ENTER_FUN;
   db = sdb_product_getDatabase(product, id);
   if (db == NULL) {
      db = sdb_database_new(id, (CFL_UINT32) strlen(id));
      sdb_product_addDatabase(product, db);
   }
   RETURN db;
}

SDB_DATABASEP sdb_product_delDatabase(SDB_PRODUCTP product, const char *id) {
   CFL_UINT32 len;
   CFL_UINT32 i;

   ENTER_FUN;
   len = cfl_list_length(product->databases);
   for (i = 0; i < len; i++) {
      SDB_DATABASEP db = (SDB_DATABASEP) cfl_list_get(product->databases, i);
      if (cfl_str_bufferEqualsIgnoreCase(db->name, id)) {
         cfl_list_del(product->databases, i);
         RETURN db;
      }
   }
   RETURN NULL;
}

SDB_EXPRESSION_TRANSLATIONP sdb_product_getTranslation(SDB_PRODUCTP product, const char *name, CFL_UINT32 parCount) {
   CFL_UINT32 len = cfl_list_length(product->translations);
   CFL_UINT32 i;
   for (i = 0; i < len; i++) {
      SDB_EXPRESSION_TRANSLATIONP trans = (SDB_EXPRESSION_TRANSLATIONP) cfl_list_get(product->translations, i);
      if (cfl_str_bufferEqualsIgnoreCase(trans->name, name) && trans->parCount == parCount) {
         return trans;
      }
   }
   return NULL;
}

CFL_BOOL sdb_product_addTranslation(SDB_PRODUCTP product, SDB_EXPRESSION_TRANSLATIONP trans) {
   if (sdb_product_getTranslation(product, cfl_str_getPtr(trans->name), trans->parCount) == NULL) {
      cfl_list_add(product->translations, trans);
      return CFL_TRUE;
   }
   return CFL_FALSE;
}
