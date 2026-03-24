#ifndef SDB_DICT_H_

#define SDB_DICT_H_

#include "cfl_types.h"

#include "sdb_defs.h"

typedef CFL_BOOL   ( * SDBDICT_FNC_EXISTS_DICT )(SDB_CONNECTIONP connection, SDB_SCHEMAP schema);
typedef SDB_TABLEP ( * SDBDICT_FNC_GET_TAB     )(SDB_CONNECTIONP connection, SDB_SCHEMAP schema, const char *tableName);
typedef CFL_BOOL   ( * SDBDICT_FNC_INS_TAB     )(SDB_CONNECTIONP connection, SDB_SCHEMAP schema, SDB_TABLEP table);
typedef CFL_BOOL   ( * SDBDICT_FNC_DEL_TAB     )(SDB_CONNECTIONP connection, SDB_TABLEP table);
typedef CFL_BOOL   ( * SDBDICT_FNC_UPD_TAB     )(SDB_CONNECTIONP connection, SDB_TABLEP table);
typedef CFL_BOOL   ( * SDBDICT_FNC_EXISTS_TAB  )(SDB_CONNECTIONP connection, SDB_SCHEMAP schema, const char *tableName);
typedef CFL_BOOL   ( * SDBDICT_FNC_INS_COL     )(SDB_CONNECTIONP connection, SDB_TABLEP table, SDB_FIELDP field);
typedef CFL_BOOL   ( * SDBDICT_FNC_DEL_COL     )(SDB_CONNECTIONP connection, SDB_TABLEP table, SDB_FIELDP field);
typedef CFL_BOOL   ( * SDBDICT_FNC_UPD_COL     )(SDB_CONNECTIONP connection, SDB_TABLEP table, SDB_FIELDP field);
typedef CFL_BOOL   ( * SDBDICT_FNC_EXISTS_IND  )(SDB_CONNECTIONP connection, SDB_SCHEMAP schema, const char *tableName, const char *indexName);
typedef CFL_BOOL   ( * SDBDICT_FNC_CREATE_OBJ  )(SDB_CONNECTIONP connection);
typedef SDB_RECNO  ( * SDBDICT_FNC_GET_RECNO   )(SDB_CONNECTIONP connection, SDB_TABLEP table, CFL_BOOL update);
typedef void       ( * SDB_DICT_FNC_FREE       )(SDB_DICTIONARYP dict);

struct _SDB_DICTIONARY {
   char                    *name;
   SDBDICT_FNC_EXISTS_DICT existsDict;
   SDBDICT_FNC_GET_TAB     getTable;
   SDBDICT_FNC_INS_TAB     insertTable;
   SDBDICT_FNC_DEL_TAB     deleteTable;
   SDBDICT_FNC_UPD_TAB     updateTable;
   SDBDICT_FNC_EXISTS_TAB  existsTable;
   SDBDICT_FNC_INS_COL     insertColumn;
   SDBDICT_FNC_DEL_COL     deleteColumn;
   SDBDICT_FNC_UPD_COL     updateColumn;
   SDBDICT_FNC_EXISTS_IND  existsIndex;
   SDBDICT_FNC_CREATE_OBJ  createObjects;
   SDBDICT_FNC_GET_RECNO   nextRecno; 
   SDBDICT_FNC_GET_RECNO   maxRecno; 
   SDB_DICT_FNC_FREE       freeDict; 
};

extern SDB_DICTIONARYP sdb_dict_getDefault(void);
extern void sdb_dict_registerDictionary(SDB_DICTIONARYP dict);
extern SDB_DICTIONARYP sdb_dict_getDictionary(SDB_CONNECTIONP connection, SDB_SCHEMAP schema);
        
#endif
