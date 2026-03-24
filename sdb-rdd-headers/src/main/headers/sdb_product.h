#ifndef SDB_PRODUCT_H_

#define SDB_PRODUCT_H_

#include "cfl_types.h"
#include "cfl_str.h"
#include "cfl_list.h"

#include "sdb_defs.h"

#define MAX_PRODUCTS         10

struct _SDB_PRODUCT {
   CFL_UINT8           objectType;
   CFL_STRP            name;
   CFL_STRP            displayName;
   CFL_LISTP           databases;
   CFL_LISTP           translations;
   CFL_STRP            pkName;
   CFL_STRP            pkDefaultExpr;
   CFL_STRP            delName;
   struct _SDB_DB_API *dbAPI;
};

extern SDB_PRODUCTP sdb_product_new(const char *name, const char *displayName, SDB_DB_APIP dbAPI);
extern void sdb_product_free(SDB_PRODUCTP product);
extern void sdb_product_addDatabase(SDB_PRODUCTP product, SDB_DATABASEP database);
extern SDB_DATABASEP sdb_product_getDatabase(SDB_PRODUCTP product, const char *id);
extern SDB_DATABASEP sdb_product_getCreateDatabase(SDB_PRODUCTP product, const char *id);
extern SDB_DATABASEP sdb_product_delDatabase(SDB_PRODUCTP product, const char *id);

extern CFL_BOOL sdb_product_addTranslation(SDB_PRODUCTP product, SDB_EXPRESSION_TRANSLATIONP trans);
extern SDB_EXPRESSION_TRANSLATIONP sdb_product_getTranslation(SDB_PRODUCTP product, const char *name, CFL_UINT32 parCount);

#endif
