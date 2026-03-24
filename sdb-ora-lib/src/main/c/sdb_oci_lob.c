#include "sdb_oci_lob.h"
#include "sdb_defs.h"
#include "sdb_error.h"
#include "sdb_thread.h"
#include "sdb_log.h"

SDB_OCI_LOB * sdb_oci_lob_new(SDB_OCI_CONNECTION *conn, CFL_UINT16 lobType, CFL_BOOL bTemporary) {
   SDB_OCI_LOB *lob;
   void *handle = NULL;
   int status;
   void *errorHandle = sdb_oci_getErrorHandle();

   status = sdb_oci_getSymbols()->OCIDescriptorAlloc(sdb_oci_getEnv(), &handle, SDB_OCI_DTYPE_LOB, 0, NULL);
   CHECK_STATUS_RETURN(status, errorHandle, "Error allocating LOB descriptor", NULL);
   lob = SDB_MEM_ALLOC(sizeof(SDB_OCI_LOB));
   if (lob != NULL) {
      lob->handle = handle;
      lob->conn = conn;
      lob->type = lobType;
      lob->refCount = 1;
      lob->isTemporary = bTemporary;
      if (bTemporary) {
         status = sdb_oci_getSymbols()->OCILobCreateTemporary(conn->serviceHandle, errorHandle, handle, SDB_OCI_DEFAULT,
                 SDB_SQLCS_IMPLICIT, lobType == SDB_SQLT_CLOB ? SDB_OCI_TEMP_CLOB : SDB_OCI_TEMP_BLOB, 1, SDB_OCI_DURATION_SESSION);
         if (STATUS_ERROR(status)) {
            sdb_oci_setErrorFromOCI(errorHandle, SDB_OCI_HTYPE_ERROR, "Error creating temporary LOB");
            sdb_oci_getSymbols()->OCIDescriptorFree(handle, SDB_OCI_DTYPE_LOB);
            SDB_MEM_FREE(lob);
            lob = NULL;
         }
      }
   } else {
      sdb_thread_setError(SDB_ERROR_TYPE_DB_ERROR, SDB_OCI_ERROR_DESCRIPTOR, "Cannot allocate memory to create LOB");
      sdb_oci_getSymbols()->OCIDescriptorFree(handle, SDB_OCI_DTYPE_LOB);
   }
   return lob;
}

void sdb_oci_lob_free(SDB_OCI_LOB *lob) {
   if (lob->refCount > 0) {
      --lob->refCount;
   }
   if (lob->refCount == 0) {
      void *errorHandle = sdb_oci_getErrorHandle();
      int status;
      //sdb_oci_lob_close(lob);
      if (lob->isTemporary) {
         status = sdb_oci_getSymbols()->OCILobFreeTemporary(lob->conn->serviceHandle, errorHandle, sdb_oci_lob_handle(lob));
         CHECK_STATUS_ERROR(status, errorHandle, "Error releasing temporary LOB");
      }
      status = sdb_oci_getSymbols()->OCIDescriptorFree(sdb_oci_lob_handle(lob), SDB_OCI_DTYPE_LOB);
      CHECK_STATUS_ERROR(status, errorHandle, "Error releasing LOB descriptor");
      SDB_MEM_FREE(lob);
   }
}

void * sdb_oci_lob_handle(SDB_OCI_LOB *lob) {
   return lob->handle;
}

CFL_UINT32 sdb_oci_lob_incRef(SDB_OCI_LOB *lob) {
   return ++lob->refCount;
}

CFL_BOOL sdb_oci_lob_open(SDB_OCI_LOB *lob) {
   void *errorHandle = sdb_oci_getErrorHandle();
   int status;
   status = sdb_oci_getSymbols()->OCILobOpen(lob->conn->serviceHandle, errorHandle, sdb_oci_lob_handle(lob),
           SDB_OCI_LOB_READWRITE);
   CHECK_STATUS_RETURN(status, errorHandle, "Error opening LOB", CFL_FALSE);
   return CFL_TRUE;
}

CFL_BOOL sdb_oci_lob_close(SDB_OCI_LOB *lob) {
   void *errorHandle = sdb_oci_getErrorHandle();
   int status = sdb_oci_getSymbols()->OCILobClose(lob->conn->serviceHandle, errorHandle, sdb_oci_lob_handle(lob));
   CHECK_STATUS_RETURN(status, errorHandle, "Error closing LOB", CFL_FALSE);
   return CFL_TRUE;
}

CFL_BOOL sdb_oci_lob_read(SDB_OCI_LOB *lob, const char *buffer, CFL_UINT64 offset, CFL_UINT64 *amount) {
   int status;
   CFL_UINT64 amountInBytes;
   CFL_UINT64 amountInChars;
   void *errorHandle = sdb_oci_getErrorHandle();

   SDB_LOG_TRACE(("sdb_oci_lob_read: offset=%llu, amount=%llu", offset, *amount));
   if (lob->type == SDB_SQLT_CLOB) {
      amountInChars = *amount;
      amountInBytes = 0;
   } else {
      amountInChars = 0;
      amountInBytes = *amount;
   }
   status = sdb_oci_getSymbols()->OCILobRead2(lob->conn->serviceHandle, errorHandle, sdb_oci_lob_handle(lob),
           &amountInBytes, &amountInChars, offset, (void *) buffer, *amount, SDB_OCI_ONE_PIECE, NULL, NULL, 0, SDB_SQLCS_IMPLICIT);
   CHECK_STATUS_RETURN(status, errorHandle, "Error reading LOB", CFL_FALSE);
   if (lob->type == SDB_SQLT_CLOB) {
      *amount = amountInChars;
   } else {
      *amount = amountInBytes;
   }
   return CFL_TRUE;
}

CFL_BOOL sdb_oci_lob_write(SDB_OCI_LOB *lob, const char *buffer, CFL_UINT64 offset, CFL_UINT64 amount) {
   CFL_UINT64 lengthInBytes;
   CFL_UINT64 lengthInChars;
   int status;
   void *errorHandle = sdb_oci_getErrorHandle();

   if (lob->type == SDB_SQLT_CLOB) {
      lengthInChars = amount;
      lengthInBytes = 0;
   } else {
      lengthInChars = 0;
      lengthInBytes = amount;
   }
   SDB_LOG_TRACE(("sdb_oci_lob_write: offset=%llu, amount=%llu", offset, amount));
   status = sdb_oci_getSymbols()->OCILobWrite2(lob->conn->serviceHandle, errorHandle, sdb_oci_lob_handle(lob),
           &lengthInBytes, &lengthInChars, offset, (void*) buffer, amount, SDB_OCI_ONE_PIECE, NULL, NULL, 0, SDB_SQLCS_IMPLICIT);
   CHECK_STATUS_RETURN(status, errorHandle, "Error writing LOB", CFL_FALSE);
   return CFL_TRUE;
}

