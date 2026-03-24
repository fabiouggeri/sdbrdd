#ifndef SDB_TRANSACTION_H_

#define SDB_TRANSACTION_H_

#include "cfl_types.h"
#include "cfl_str.h"

#include "sdb_defs.h"

struct _SDB_TRANSACTION {
   CFL_UINT8 objectType;
   void      *handle;
   CFL_INT32 formatId;
   CFL_STRP  globalId;
   CFL_STRP  branchId;
};

extern SDB_TRANSACTIONP sdb_transaction_new(void *handle, CFL_INT32 formatId, CFL_STRP globalId, CFL_STRP branchId);
extern void sdb_transaction_free(SDB_TRANSACTIONP trans);

#endif