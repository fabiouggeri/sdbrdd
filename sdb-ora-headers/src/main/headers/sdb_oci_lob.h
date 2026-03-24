#ifndef SDB_OCI_LOB_H_

#define SDB_OCI_LOB_H_

#include "cfl_types.h"
#include "sdb_oci_lib.h"
#include "sdb_oci_types.h"

struct _SDB_OCI_LOB {
   void               *handle;
   CFL_UINT16          type;
   CFL_UINT32          refCount;
   SDB_OCI_CONNECTION *conn;
   CFL_BOOL            isTemporary;
};

extern SDB_OCI_LOB * sdb_oci_lob_new(SDB_OCI_CONNECTION *conn, CFL_UINT16 lobType, CFL_BOOL bTemporary);
extern void sdb_oci_lob_free(SDB_OCI_LOB *lob);
extern void * sdb_oci_lob_handle(SDB_OCI_LOB *lob);
extern CFL_BOOL sdb_oci_lob_open(SDB_OCI_LOB *lob);
extern CFL_BOOL sdb_oci_lob_close(SDB_OCI_LOB *lob);
extern CFL_UINT32 sdb_oci_lob_incRef(SDB_OCI_LOB *lob);
extern CFL_BOOL sdb_oci_lob_read(SDB_OCI_LOB *lob, const char *buffer, CFL_UINT64 offset, CFL_UINT64 *amount);
extern CFL_BOOL sdb_oci_lob_write(SDB_OCI_LOB *lob, const char *buffer, CFL_UINT64 offset, CFL_UINT64 amount);

#endif

