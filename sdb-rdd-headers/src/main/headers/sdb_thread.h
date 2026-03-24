#ifndef SDB_THREAD_H_

#define SDB_THREAD_H_

#include <time.h>
#ifdef __XHB__
   #include <windows.h>
#endif

#include "hbdefs.h"
#ifdef __HBR__
   #include "hbthread.h"
#endif

#include "cfl_types.h"
#include "cfl_list.h"

#include "sdb_defs.h"
#include "sdb_error.h"

#define sdb_thread_hasError()                     (sdb_thread_getLastError()->type != SDB_ERROR_TYPE_NO_ERROR)
#define sdb_thread_errorCount()                   sdb_error_getErrorCount(&sdb_thread_getData()->error)
#define sdb_thread_hasNewErrors(previous)         (sdb_error_getErrorCount(&sdb_thread_getData()->error) > previous)
#define sdb_thread_hasParams(d)                   ((d)->paramList != NULL && ! sdb_param_listIsEmpty((d)->paramList))
#define sdb_thread_getParams(d)                   ((d)->paramList)
#define sdb_thread_getDefaultBufferFetchSize(d)   ((d)->defaultBufferFetchSize)
#define sdb_thread_setDefaultBufferFetchSize(d,v) ((d)->defaultBufferFetchSize = v)
#define sdb_thread_getDefaultMemoType(d)          ((d)->defaultMemoType)
#define sdb_thread_setDefaultMemoType(d,v)        ((d)->defaultMemoType = v)
#define sdb_thread_isTrimParams(d)                ((d)->isTrimParams)
#define sdb_thread_setTrimParams(d,v)             ((d)->isTrimParams = v)
#define sdb_thread_isPadFields(d)                 ((d)->isPadFields)
#define sdb_thread_setPadFields(d,v)              ((d)->isPadFields = v)
#define sdb_thread_isNullable(d)                  ((d)->isNullable)
#define sdb_thread_setNullable(d,v)               ((d)->isNullable = v)

struct _SDB_THREAD_DATA {
   CFL_UINT8       objectType;
   HB_THREAD_NO    threadId;
   SDB_CONNECTIONP connection;
   SDB_ERROR       error;
   SDB_STATEMENTP  pStatement;
   SDB_PARAMLISTP  paramList;
   CFL_LISTP       areas;
   time_t          lastStmtVerification;
   void            *dbAPIData;
   CFL_UINT16      nextBufferFetchSize;
   CFL_UINT16      defaultBufferFetchSize;
   CFL_UINT8       defaultMemoType;
   CFL_BOOL        isTrimParams BIT_FIELD;
   CFL_BOOL        isPadFields BIT_FIELD;
   CFL_BOOL        isNullable BIT_FIELD;
   CFL_BOOL        isDelayAppend BIT_FIELD;
};

extern SDB_THREAD_DATAP sdb_thread_getData(void);
extern void sdb_thread_clear(SDB_THREAD_DATAP data);
extern SDB_ERRORP sdb_thread_getLastError(void);
extern void sdb_thread_cleanError(void);
extern void sdb_thread_setError(int errorType, int errorCode, const char * message, ...);
extern void sdb_thread_addArea(SDB_AREAP pSDBArea);
extern void sdb_thread_delArea(SDB_AREAP pSDBArea);
extern SDB_PARAMLISTP sdb_thread_getCreateParams(SDB_THREAD_DATAP data);
extern void sdb_thread_freeParams(SDB_THREAD_DATAP data);

#endif
