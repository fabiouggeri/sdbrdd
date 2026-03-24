#include "sdb_transaction.h"
#include "sdb_util.h"
#include "sdb_log.h"

SDB_TRANSACTIONP sdb_transaction_new(void *handle, CFL_INT32 formatId, CFL_STRP globalId, CFL_STRP branchId) {
   SDB_TRANSACTIONP trans = (SDB_TRANSACTIONP) SDB_MEM_ALLOC(sizeof(SDB_TRANSACTION));
   trans->objectType = SDB_OBJ_TRANSACTION;
   trans->handle = handle;
   trans->formatId = formatId;
   trans->globalId = globalId ? cfl_str_newStr(globalId) : NULL;
   trans->branchId = branchId ? cfl_str_newStr(branchId) : NULL;
   return trans;
}

void sdb_transaction_free(SDB_TRANSACTIONP trans) {
   if (trans->globalId) {
      cfl_str_free(trans->globalId);
   }
   if (trans->branchId) {
      cfl_str_free(trans->branchId);
   }
   if (trans) {
      SDB_MEM_FREE(trans);
   }
}

