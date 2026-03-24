#ifndef SDB_H_

#define SDB_H_

#include "sdb_types.h"

#define SDB_LOCK_NONE      0
#define SDB_LOCK_SHARED    1
#define SDB_LOCK_LOCK_ALL  2
#define SDB_LOCK_LOCK_REC  3
#define SDB_LOCK_EXCLUSIVE 4

#define SDB_LOCK_CONTROL_NONE   0 
#define SDB_LOCK_CONTROL_DB     10
#define SDB_LOCK_AUTO_GET       20
#define SDB_LOCK_AUTO_PUT       30
#define SDB_LOCK_CONTROL_SERVER 40

extern SDB_CONNECTIONP sdb_login(const char *database, const char *username, const char *password,
                                 const char *product, const char *lockControl, CFL_UINT16 lockServerPort,
                                 CFL_BOOL registerConn);
extern CFL_BOOL sdb_logout(SDB_CONNECTIONP conn);
extern CFL_BOOL sdb_isLogged(SDB_CONNECTIONP conn);
extern CFL_BOOL sdb_breakOperation(SDB_CONNECTIONP conn);
extern SDB_CONNECTIONP sdb_currentConnection(void);
extern void sdb_setCurrentConnection(SDB_CONNECTIONP conn);
extern char *sdb_getErrorMsg(void);
extern CFL_INT32 sdb_getErrorCode(void);
extern CFL_BOOL sdb_beginTransaction(SDB_CONNECTIONP conn, CFL_INT32 formatId, const char *globalId, const char *branchId);
extern CFL_BOOL sdb_commitTransaction(SDB_CONNECTIONP conn);
extern CFL_BOOL sdb_rollbackTransaction(SDB_CONNECTIONP conn);
extern CFL_BOOL sdb_isTransaction(SDB_CONNECTIONP conn);

#endif
