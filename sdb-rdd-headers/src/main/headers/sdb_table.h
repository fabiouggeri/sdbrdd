#ifndef SDB_TABLE_H_

#define SDB_TABLE_H_

#include "cfl_types.h"
#include "cfl_str.h"
#include "cfl_list.h"
#include "cfl_lock.h"

#include "sdb_defs.h"

#define SDB_PART_NONE           0
#define SDB_PART_PARTITIONED    1
#define SDB_PART_SUBPARTITIONED 2

#define sdb_table_getPKCacheSize(t)    20
#define sdb_table_setNextPK(t, v)      ((t)->nextRecno = v)
#define sdb_table_setMaxPK(t, v)       ((t)->maxRecno = v)
#define sdb_table_nextPK(t)            (++(t)->nextRecno)
#define sdb_table_maxPK(t)             (t)->maxRecno
#define sdb_table_pkInCache(t)         ((t)->nextRecno <= (t)->maxRecno)
#define sdb_table_cacheNextPK(t, v, s) (t)->nextRecno = v; (t)->maxRecno = (t)->nextRecno + (s) - 1

struct _SDB_TABLE {
   CFL_UINT8   objectType;
   CFL_STRP    clpName;                             /* Table name in Clipper */
   SDB_SCHEMAP dbSchema;                            /* Physical schema of table */
   SDB_SCHEMAP clpSchema;                           /* Clipper schema of table */
   CFL_STRP    dbName;                              /* Table name in database */
   CFL_STRP    hintAsc;                             /* Hint to use in queries in ascending order without index active */
   CFL_STRP    hintDesc;                            /* Hint to use in queries in descending order without index active */
   CFL_UINT32  dataLen;                             /* Tamanho da parte de dados do registro */
   CFL_LISTP   fields;
   SDB_FIELDP  rowIdField;
   SDB_FIELDP  pkField;
   SDB_FIELDP  delField;
   CFL_LISTP   indexes;                             /* Indexes of a table */
   SDB_RECNO   nextRecno;
   SDB_RECNO   maxRecno;
   CFL_LOCK    lock;
   CFL_UINT16  bufferFetchSize;                     /* Default buffer fetch size */
   CFL_BOOL    locked BIT_FIELD;                    /* Indica se a tabela esta bloqueada */
   CFL_BOOL    isIndexExpressionsUpdated BIT_FIELD; /* Index has codeblock */
   CFL_BOOL    isContextualized          BIT_FIELD; /* Table has context defined in dictionary */
};

extern SDB_TABLEP sdb_table_new(SDB_SCHEMAP dbSchema, const char * clpTableName, CFL_UINT32 clpTabNameLen, const char *dbTableName, CFL_UINT32 dbTabNameLen);
extern void sdb_table_free(SDB_TABLEP table);
extern void sdb_table_setHintAsc(SDB_TABLEP table, const char *hint);
extern void sdb_table_setHintDesc(SDB_TABLEP table, const char *hint);

extern void sdb_table_addField(SDB_TABLEP table, SDB_FIELDP field);
extern SDB_FIELDP sdb_table_getField(SDB_TABLEP table, const char *fieldName);
extern SDB_FIELDP sdb_table_delField(SDB_TABLEP table, const char *fieldName);

extern void sdb_table_addIndex(SDB_TABLEP table, SDB_INDEXP index);
extern SDB_INDEXP sdb_table_getIndex(SDB_TABLEP table, const char *indexName);
extern SDB_INDEXP sdb_table_delIndex(SDB_TABLEP table, const char *indexName);

extern CFL_UINT16 sdb_table_nextColOrder(SDB_TABLEP table, SDB_FIELDP field);

extern SDB_FIELDP sdb_table_getFieldByDBName(SDB_TABLEP table, const char *name);

extern void sdb_table_maxOrder(SDB_TABLEP table, CFL_UINT16 *dataOrder, CFL_UINT16 *indOrder);

extern void sdb_table_lock(SDB_TABLEP table);
extern void sdb_table_unlock(SDB_TABLEP table);

#endif
