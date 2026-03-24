#ifndef SDB_INFO_H_

#define SDB_INFO_H_

#include "cfl_types.h"
#include "cfl_str.h"

#include "sdb_defs.h"

struct _SDB_QUERY_COL_INFO {
   CFL_STRP   name;
   CFL_UINT8  clpType;
   CFL_STRP   dbType;
   CFL_UINT32 size;
   CFL_UINT32 sizeInBytes;
   CFL_UINT8  precision;
   CFL_UINT8  scale;
   CFL_BOOL   isAllocated BIT_FIELD; /* The statement struct was dynamic allocated? */
   CFL_BOOL   isNullable  BIT_FIELD;
};

extern void sdb_queryInfo_init(SDB_QUERY_COL_INFOP info);
extern void sdb_queryInfo_free(SDB_QUERY_COL_INFOP info);

#endif