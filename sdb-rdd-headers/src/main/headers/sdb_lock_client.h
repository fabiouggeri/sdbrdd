#ifndef SDB_LOCK_CLIENT_H_

#define SDB_LOCK_CLIENT_H_

#include "cfl_types.h"
#include "cfl_str.h"
#include "cfl_socket.h"
#include "cfl_buffer.h"

#include "sdb_defs.h"

struct _SDB_LS_CLIENT {
   CFL_STRP    serverAddress;
   CFL_UINT16  serverPort;
   CFL_SOCKET  socket;
   CFL_UINT32  clientId;
   CFL_BUFFERP buffer;
};

extern SDB_LS_CLIENTP sdb_lckcli_connect(const char *serverAddress, CFL_UINT16 serverPort);
extern void sdb_lckcli_disconnect(SDB_LS_CLIENTP client);
extern CFL_UINT32 sdb_lckcli_openShared(SDB_LS_CLIENTP client, SDB_DATABASEP database, SDB_TABLEP table, CFL_STRP context);
extern CFL_UINT32 sdb_lckcli_openExclusive(SDB_LS_CLIENTP client, SDB_DATABASEP database, SDB_TABLEP table, CFL_STRP context);
extern CFL_BOOL sdb_lckcli_lockTable(SDB_LS_CLIENTP client, CFL_UINT32 contextId);
extern CFL_BOOL sdb_lckcli_closeTable(SDB_LS_CLIENTP client, CFL_UINT32 contextId);
extern CFL_BOOL sdb_lckcli_closeAll(SDB_LS_CLIENTP client);
extern CFL_BOOL sdb_lckcli_lockRecord(SDB_LS_CLIENTP client, CFL_UINT32 contextId, SDB_RECNO recno);
extern CFL_BOOL sdb_lckcli_unlockRecord(SDB_LS_CLIENTP client, CFL_UINT32 contextId, SDB_RECNO recno);
extern CFL_BOOL sdb_lckcli_unlockAll(SDB_LS_CLIENTP client, CFL_UINT32 contextId);

#endif
