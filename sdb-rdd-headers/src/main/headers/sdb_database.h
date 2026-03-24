#ifndef SDB_DATABASE_H_

#define SDB_DATABASE_H_

#include "cfl_types.h"
#include "cfl_lock.h"
#include "cfl_str.h"
#include "cfl_list.h"

#include "sdb_defs.h"

struct _SDB_DATABASE {
   CFL_UINT8  objectType;
   CFL_LOCK   lock;
   CFL_STRP   name;
   CFL_LISTP  schemas;
};

extern SDB_DATABASEP sdb_database_new(const char *name, CFL_UINT32 nameLen);
extern void sdb_database_free(SDB_DATABASEP db);
extern void sdb_database_addSchema(SDB_DATABASEP db, SDB_SCHEMAP schema);
extern SDB_SCHEMAP sdb_database_getSchema(SDB_DATABASEP db, const char *schemaName);
extern SDB_SCHEMAP sdb_database_delSchema(SDB_DATABASEP db, const char *schemaName);
extern SDB_SCHEMAP sdb_database_getCreateSchema(SDB_DATABASEP connection, const char* schemaName);

#endif
