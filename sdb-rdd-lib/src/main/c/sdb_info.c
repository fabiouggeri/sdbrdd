#include "sdb_info.h"
#include "sdb_util.h"

void sdb_queryInfo_init(SDB_QUERY_COL_INFOP info) {
   info->name = cfl_str_new(30);
   info->clpType = SDB_CLP_UNKNOWN;
   info->dbType = cfl_str_new(30);
   info->isAllocated = CFL_FALSE;
   info->isNullable = CFL_FALSE;
   info->size = 0;
   info->sizeInBytes = 0;
   info->precision = 0;
   info->scale = 0;
}

void sdb_queryInfo_free(SDB_QUERY_COL_INFOP info) {
   cfl_str_free(info->name);
   cfl_str_free(info->dbType);
   if (info->isAllocated) {
      SDB_MEM_FREE(info);
   }
}
