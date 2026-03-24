#include "sdb_oci_rowid.h"
#include "sdb_defs.h"
#include "sdb_error.h"
#include "sdb_log.h"

SDB_OCI_ROWID * sdb_oci_rowid_new(SDB_OCI_CONNECTION *conn) {
   SDB_OCI_ROWID *rowId;
   void *errorHandle = sdb_oci_getErrorHandle();
   void *handle = NULL;
   int status;
   
   status = sdb_oci_getSymbols()->OCIDescriptorAlloc(sdb_oci_getEnv(), &handle, SDB_OCI_DTYPE_ROWID, 0, NULL);
   CHECK_STATUS_RETURN(status, errorHandle, "Error allocating ROWID descriptor", NULL);
   rowId = SDB_MEM_ALLOC(sizeof(SDB_OCI_ROWID));
   if (rowId != NULL) {
      rowId->conn = conn;
      rowId->handle = handle;
      rowId->refCount = 1;
   } else {
      sdb_oci_getSymbols()->OCIDescriptorFree(handle, SDB_OCI_DTYPE_ROWID);
   }
   return rowId;
}

void * sdb_oci_rowid_handle(SDB_OCI_ROWID *rowId) {
   return rowId->handle;
}

void sdb_oci_rowid_free(SDB_OCI_ROWID *rowId) {
   if (rowId->refCount > 0) {
      --rowId->refCount;
   }
   if (rowId->refCount == 0) {
      void *errorHandle = sdb_oci_getErrorHandle();
      int status = sdb_oci_getSymbols()->OCIDescriptorFree(sdb_oci_rowid_handle(rowId), SDB_OCI_DTYPE_ROWID);
      CHECK_STATUS_ERROR(status, errorHandle, "Error releasing ROWID");
      SDB_MEM_FREE(rowId);
   }
}

CFL_UINT32 sdb_oci_rowid_incRef(SDB_OCI_ROWID *rowId) {
   return ++rowId->refCount;
}

