#ifndef SDB_INDEX_H_

#define SDB_INDEX_H_

#include "cfl_types.h"

#include "sdb_defs.h"

#define SDB_IDX_COL_LIST  0
#define SDB_IDX_COL_EXPR  1

struct _SDB_INDEX {
   CFL_UINT8    objectType;
   SDB_FIELDP   field;
   PHB_ITEM     compiledExpr;
};

extern SDB_INDEXP sdb_index_new(SDB_FIELDP field, const char *clpIndexName, CFL_UINT32 clpNameLen, 
                                const char *dbIndexName, CFL_UINT32 dbNameLen, const char *clpIndexExpr, CFL_UINT32 clpExprLen, 
                                const char *hint, CFL_UINT32 hintLen);
extern void sdb_index_free(SDB_INDEXP index);

#endif
