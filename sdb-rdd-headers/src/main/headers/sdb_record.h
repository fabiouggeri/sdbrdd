#ifndef SDB_RECORD_H_

#define SDB_RECORD_H_

#include "cfl_types.h"

#include "sdb_defs.h"

struct _SDB_RECORDLOCKED {
   SDB_RECNO ulRecno;
   struct _SDB_RECORDLOCKED *previous;
};

struct _SDB_RECORD {
   CFL_UINT8   objectType;
   CFL_BOOL    isChanged BIT_FIELD;
   CFL_BOOL    isLocked BIT_FIELD;
   SDB_RECNO   ulRowNum;
   SDB_TABLEP  table;
   CFL_UINT32  *fieldsOffsets;
   CFL_UINT8   data[1];
};

extern SDB_RECORDP sdb_record_new(SDB_AREAP pSDBArea);
extern void sdb_record_free(SDB_RECORDP record);
extern HB_ERRCODE sdb_record_setValue(SDB_RECORDP record, SDB_FIELDP field, PHB_ITEM pNewValue);
extern HB_ERRCODE sdb_record_getValue(SDB_RECORDP record, SDB_FIELDP field, PHB_ITEM pItem, CFL_BOOL pad);
extern HB_ERRCODE sdb_record_setLogical(SDB_RECORDP record, SDB_FIELDP field, CFL_BOOL value);
extern CFL_BOOL sdb_record_getLogical(SDB_RECORDP record, SDB_FIELDP field);
extern HB_ERRCODE sdb_record_setInt64(SDB_RECORDP record, SDB_FIELDP field, CFL_INT64 value);
extern CFL_INT64 sdb_record_getInt64(SDB_RECORDP record, SDB_FIELDP field);
extern HB_ERRCODE sdb_record_setInt32(SDB_RECORDP record, SDB_FIELDP field, CFL_INT32 val);
extern CFL_INT32 sdb_record_getInt32(SDB_RECORDP record, SDB_FIELDP field);
extern HB_ERRCODE sdb_record_setInt16(SDB_RECORDP record, SDB_FIELDP field, CFL_INT16 val);
extern CFL_INT16 sdb_record_getInt16(SDB_RECORDP record, SDB_FIELDP field);
extern HB_ERRCODE sdb_record_setDouble(SDB_RECORDP record, SDB_FIELDP field, double val);
extern double sdb_record_getDouble(SDB_RECORDP record, SDB_FIELDP field);
extern HB_ERRCODE sdb_record_setString(SDB_RECORDP record, SDB_FIELDP field, const char *str, CFL_UINT32 len);
extern const char * sdb_record_getString(SDB_RECORDP record, SDB_FIELDP field);
extern HB_ERRCODE sdb_record_setDate(SDB_RECORDP record, SDB_FIELDP field, CFL_UINT16 year, CFL_UINT8 month, CFL_UINT8 day);
extern long sdb_record_getDate(SDB_RECORDP record, SDB_FIELDP field, CFL_UINT16 *year, CFL_UINT8 *month, CFL_UINT8 *day);
extern HB_ERRCODE sdb_record_setTimestamp(SDB_RECORDP record, SDB_FIELDP field, CFL_UINT16 year, CFL_UINT8 month, CFL_UINT8 day, CFL_UINT8 hour, CFL_UINT8 min, CFL_UINT8 sec, CFL_UINT32 mSec);
extern double sdb_record_getTimestamp(SDB_RECORDP record, SDB_FIELDP field, CFL_UINT16 *year, CFL_UINT8 *month, CFL_UINT8 *day, CFL_UINT8 *hour, CFL_UINT8 *min, CFL_UINT8 *sec, CFL_UINT32 *mSec);
extern HB_ERRCODE sdb_record_setItem(SDB_RECORDP record, SDB_FIELDP field, PHB_ITEM item);
extern PHB_ITEM sdb_record_getItem(SDB_RECORDP record, SDB_FIELDP field);

#endif
