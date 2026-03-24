#ifndef SDB_ERROR_H_

#define SDB_ERROR_H_

#include "cfl_types.h"
#include "cfl_str.h"

#include "sdb_defs.h"
#include "sdb_errors.ch"

#define sdb_error_getType(e)             ((e)->type)
#define sdb_error_getCode(e)             ((e)->code)
#define sdb_error_getMessage(e)          ((e)->message)
#define sdb_error_getErrorCount(e)       ((e)->errorCount)
#define sdb_error_setType(e,t)           ((e)->type = t)
#define sdb_error_setCode(e,c)           ((e)->code = c)
#define sdb_error_setMessage(e,m)        (cfl_str_setValue((e)->message, m))
#define sdb_error_setMessageArgs(e,f,a)  (cfl_str_setFormatArgs((e)->message, f, a))
#define sdb_error_setErrorCount(e,c)     ((e)->errorCount = c)
#define sdb_error_incErrorCount(e)       (++((e)->errorCount))
#define sdb_error_decErrorCount(e)       (--((e)->errorCount))
#define sdb_error_set(e, t, c, m)        (e)->type=(CFL_UINT16)t;(e)->code=(CFL_INT32)c;++((e)->errorCount);cfl_str_setValue((e)->message, m)
#define sdb_error_setArgs(e, t, c, f, a) (e)->type=(CFL_UINT16)t;(e)->code=(CFL_INT32)c;++((e)->errorCount);cfl_str_setFormatArgs((e)->message, f, a)

struct _SDB_ERROR {
   CFL_STRP   message;
   CFL_INT32  code;
   CFL_UINT32 errorCount;
   CFL_UINT16 type;
   CFL_BOOL   isAllocated BIT_FIELD;
};

extern const char * sdb_error_getDescription(CFL_UINT16 uiGenCode, CFL_UINT16 uiSubCode);
extern void sdb_error_init(SDB_ERRORP error);
extern SDB_ERRORP sdb_error_new(void);
extern void sdb_error_clear(SDB_ERRORP pError);
extern void sdb_error_free(SDB_ERRORP pError);

#endif