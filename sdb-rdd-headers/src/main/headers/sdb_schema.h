#ifndef SDB_SCHEMA_H_

#define SDB_SCHEMA_H_

#include "cfl_types.h"
#include "cfl_lock.h"
#include "cfl_str.h"
#include "cfl_list.h"

#include "sdb_defs.h"

/* GET */
#define sdb_schema_getName(s)     ((s)->name)
#define sdb_schema_getNameChar(s) ((s)->name != NULL ? cfl_str_getPtr((s)->name) : "")
#define sdb_schema_getDict(s, c)  ((s)->dict == NULL ? ((s)->dict = sdb_dict_getDictionary(c, s)) : (s)->dict)

/* SET */
#define sdb_schema_setName(s, n)     cfl_str_setStr((s)->name,n)
#define sdb_schema_setNameChar(s, n) cfl_str_setValue((s)->name,n)

struct _SDB_SCHEMA {
   CFL_UINT8       objectType;
   CFL_STRP        name;
   CFL_LOCK        lock;
   CFL_LISTP       tables;
   SDB_DICTIONARYP dict;
};

extern SDB_SCHEMAP sdb_schema_new(const char *name, CFL_UINT32 nameLen);
extern void sdb_schema_free(SDB_SCHEMAP schema);
extern void sdb_schema_addTable(SDB_SCHEMAP schema, SDB_TABLEP table);
extern SDB_TABLEP sdb_schema_getTable(SDB_SCHEMAP schema, const char *tableName);
extern SDB_TABLEP sdb_schema_delTable(SDB_SCHEMAP schema, const char *tableName);

#endif

