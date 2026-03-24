#ifndef SDB_OCI_ROWID_H_

#define SDB_OCI_ROWID_H_

#include "cfl_types.h"
#include "sdb_oci_lib.h"
#include "sdb_oci_types.h"

struct _SDB_OCI_ROWID {
   void               *handle;
   CFL_UINT32          refCount;
   SDB_OCI_CONNECTION *conn;
};

extern SDB_OCI_ROWID * sdb_oci_rowid_new(SDB_OCI_CONNECTION *conn);
extern void * sdb_oci_rowid_handle(SDB_OCI_ROWID *rowId);
extern void sdb_oci_rowid_free(SDB_OCI_ROWID *rowId);
extern CFL_UINT32 sdb_oci_rowid_incRef(SDB_OCI_ROWID *rowId);

#endif

