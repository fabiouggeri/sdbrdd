#ifndef SDB_LOB_H_

#define SDB_LOB_H_

#include "hbapi.h"
#include "hbapiitm.h"

#include "cfl_types.h"

#include "sdb_defs.h"

struct _SDB_LOB {
   CFL_UINT8       objectType;
   SDB_CONNECTIONP connection;
   CFL_UINT8       type;
   void *          handle;
};

extern SDB_LOBP sdb_lob_new(SDB_CONNECTIONP connection, CFL_UINT8 type, void *handle);
extern void sdb_lob_free(SDB_LOBP pLob);
extern SDB_LOBP sdb_lob_param(int iParam);
extern SDB_LOBP sdb_lob_itemGet(PHB_ITEM pItem);
extern PHB_ITEM sdb_lob_itemPut(PHB_ITEM pItem, SDB_LOBP pStmt);
extern SDB_LOBP sdb_lob_itemDetach(PHB_ITEM pItem);
extern CFL_BOOL sdb_lob_open(SDB_LOBP pLob);
extern CFL_BOOL sdb_lob_close(SDB_LOBP pLob);
extern CFL_BOOL sdb_lob_read(SDB_LOBP pLob, const char *buffer, CFL_UINT64 offset, CFL_UINT64 *amount);
extern CFL_BOOL sdb_lob_write(SDB_LOBP pLob, const char *buffer, CFL_UINT64 offset, CFL_UINT64 amount);

#endif