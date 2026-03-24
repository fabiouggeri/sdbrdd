#ifndef _SDB_LOCK_SERVER_H_

#define _SDB_LOCK_SERVER_H_

#include <winsock2.h>
#include <ws2tcpip.h>
#ifndef __XHB__
#include <Ws2ipdef.h>
#endif

#include "cfl_types.h"
#include "cfl_str.h"
#include "cfl_lock.h"
#include "cfl_hash.h"
#include "cfl_buffer.h"


#define BIT_FIELD

#define SDB_DEFAULT_LOCK_SERVER_PORT 7655

/* Lock Control messages */
#define SDB_LS_CMD_CONNECT        'A'
#define SDB_LS_CMD_DISCONNECT     'B'
#define SDB_LS_CMD_OPEN_SHARED    'C'
#define SDB_LS_CMD_OPEN_EXCLUSIVE 'D'
#define SDB_LS_CMD_CLOSE          'E'
#define SDB_LS_CMD_CLOSE_ALL      'F'
#define SDB_LS_CMD_LOCK_FILE      'G'
#define SDB_LS_CMD_LOCK_REC       'H'
#define SDB_LS_CMD_UNLOCK_REC     'I'
#define SDB_LS_CMD_UNLOCK_ALL     'J'
#define SDB_LS_CMD_LIST_SESSIONS  'K'
#define SDB_LS_CMD_SESSION_INFO   'L'
#define SDB_LS_CMD_SHUTDOWN       'M'
#define SDB_LS_CMD_LOG_LEVEL      'N'

#define SDB_LS_IO_BUFFER_SIZE      1024 // 8192

#define SDB_LS_RET_SUCCESS         0
#define SDB_LS_RET_FAILED          1
#define SDB_LS_RET_INVALID_REQUEST 0xFFFF

#define SDB_LS_RECNO CFL_INT64

#define COMPLETION_KEY_NONE        0
#define COMPLETION_KEY_KILL_WORKER 1
#define COMPLETION_KEY_IO          2
#define COMPLETION_KEY_DISCONNECT  3
#define COMPLETION_KEY_SHUTDOWN    4

#define WAIT_READ  'R'
#define WAIT_WRITE 'W'
#define WAIT_NONE  '.'

struct _SDB_LS_REC_LOCK;
typedef struct _SDB_LS_REC_LOCK SDB_LS_REC_LOCK;
typedef struct _SDB_LS_REC_LOCK *SDB_LS_REC_LOCKP;

struct _SDB_LS_CONTEXT;
typedef struct _SDB_LS_CONTEXT SDB_LS_CONTEXT;
typedef struct _SDB_LS_CONTEXT *SDB_LS_CONTEXTP;

struct _SDB_LS_TABLE;
typedef struct _SDB_LS_TABLE SDB_LS_TABLE;
typedef struct _SDB_LS_TABLE *SDB_LS_TABLEP;

struct _SDB_LS_SCHEMA;
typedef struct _SDB_LS_SCHEMA SDB_LS_SCHEMA;
typedef struct _SDB_LS_SCHEMA *SDB_LS_SCHEMAP;

struct _SDB_LS_DATABASE;
typedef struct _SDB_LS_DATABASE SDB_LS_DATABASE;
typedef struct _SDB_LS_DATABASE *SDB_LS_DATABASEP;

struct _SDB_LS_WORKER;
typedef struct _SDB_LS_WORKER SDB_LS_WORKER;
typedef struct _SDB_LS_WORKER *SDB_LS_WORKERP;

struct _SDB_LS_CLIENT;
typedef struct _SDB_LS_CLIENT SDB_LS_CLIENT;
typedef struct _SDB_LS_CLIENT *SDB_LS_CLIENTP;

struct _SDB_LS_SERVER;
typedef struct _SDB_LS_SERVER  SDB_LS_SERVER;
typedef struct _SDB_LS_SERVER *SDB_LS_SERVERP;

typedef void (* SDB_LS_REQ_FUN)(SDB_LS_SERVERP server);

struct _SDB_LS_REC_LOCK {
   SDB_LS_CONTEXTP context;
   SDB_LS_RECNO    recno;
   SDB_LS_CLIENTP  clientLocking;
};

struct _SDB_LS_CONTEXT {
   CFL_UINT32     id;
   SDB_LS_TABLEP  table;
   CFL_STRP       contextValue;
   CFL_UINT32     openShared;
   CFL_HASHP      lockedRecords;
   SDB_LS_CLIENTP clientLocking;
   CFL_LOCK       lock;
   CFL_BOOL       isOpenExclusive BIT_FIELD;
   CFL_BOOL       isLocked BIT_FIELD;
};

struct _SDB_LS_TABLE {
   SDB_LS_SCHEMAP schema;
   CFL_STRP       name;
   SDB_LS_CONTEXT defaultContext;
   CFL_HASHP      contexts;
};

struct _SDB_LS_SCHEMA {
   SDB_LS_DATABASEP database;
   CFL_STRP         name;
   CFL_HASHP        tables;
};

struct _SDB_LS_DATABASE {
   SDB_LS_SERVERP server;
   CFL_STRP       name;
   CFL_HASHP      schemas;
};

struct _SDB_LS_WORKER {
   HANDLE         hThread;
   SDB_LS_SERVERP server;
   SDB_LS_WORKERP next;
   SDB_LS_WORKERP previous;
   CFL_BOOL       isRunning BIT_FIELD;
};

typedef struct _SDB_LS_CLIENT {
   SDB_LS_SERVERP server;
   CFL_UINT32     id;
   CFL_STRP       address;
   WSAOVERLAPPED  overlapped;
   SOCKET         socket;
   CFL_LOCK       lock;
   SDB_LS_REQ_FUN execAfterResponse;
   CFL_HASHP      openContexts;
   CFL_HASHP      lockedRecords;
   CFL_UINT32     commandLen;
   char           operation;
   CFL_UINT8      recnoSize;
   CFL_BUFFERP    data;
   CFL_UINT8      ioBuffer[SDB_LS_IO_BUFFER_SIZE];
   CFL_BOOL       isLoggingOut;
} SDB_LS_CLIENT, *SDB_LS_CLIENTP;

struct _SDB_LS_SERVER {
   CFL_STRP               address;
   unsigned short         port;
   SOCKET                 listenSocket;
   WSAEVENT               listenEvent;
   HANDLE                 listenThread;
   HANDLE                 completionPort;
   int                    minWorkers;
   int                    maxWorkers;
   int                    workersBusy;
   int                    workersCount;
   SDB_LS_WORKERP         tailWorker;
   CFL_LOCK               criticalSection;
   CFL_LOCK               lockDictionary;
   CFL_HASHP              clients;
   CFL_HASHP              databases;
   CFL_UINT32             nextContextId;
   CFL_UINT32             nextClientId;
   LPTHREAD_START_ROUTINE externalShutdownProc;
   CFL_BOOL               isRunning BIT_FIELD;
};

extern SDB_LS_SERVERP sdb_lcksrv_createServer(char *address, int serverPort, int minWorkers, int maxWorkers);
extern void sdb_lcksrv_runServer(SDB_LS_SERVERP server, CFL_BOOL wait);
extern void sdb_lcksrv_shutdownServer(SDB_LS_SERVERP server);
extern void sdb_lcksrv_releaseServer(SDB_LS_SERVERP server);
extern void sdb_lcksrv_disconnectClient(SDB_LS_CLIENTP client);
extern int sdb_lcksrv_serverGetMinWorkers(SDB_LS_SERVERP server);
extern void sdb_lcksrv_serverSetMinWorkers(SDB_LS_SERVERP server, int minWorkers);
extern int sdb_lcksrv_serverGetMaxWorkers(SDB_LS_SERVERP server);
extern void sdb_lcksrv_serverSetMaxWorkers(SDB_LS_SERVERP server, int maxWorkers);

#endif
