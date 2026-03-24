#ifndef SDB_OCI_VAR_H_

#define SDB_OCI_VAR_H_

#include "cfl_types.h"
#include "sdb_oci_lib.h"
#include "sdb_oci_types.h"

#define IS_LONG(t) (t == SDB_SQLT_BIN || t == SDB_SQLT_LBI || t == SDB_SQLT_LVC || t == SDB_SQLT_LVB || t == SDB_SQLT_LNG)

#define IS_CHAR_DATATYPE(t) (t == SDB_SQLT_CHR || t == SDB_SQLT_STR || t == SDB_SQLT_AFC || t == SDB_SQLT_AFC)

#define IS_BUFFER_DATATYPE(t) (IS_LONG(t) || IS_CHAR_DATATYPE(t))

#define DESCRIPTOR_TYPE_SIZE(t) ((t) == SDB_SQLT_RDD ? sizeof(SDB_OCI_ROWID *) : \
                                (((t) == SDB_SQLT_BLOB || (t) == SDB_SQLT_CLOB) ? sizeof(SDB_OCI_LOB *) : \
                                ((t) == SDB_SQLT_RSET ? sizeof(SDB_OCI_STMT *) : 0)))

#define HAS_DESCRIPTOR(t) ((t) == SDB_SQLT_RDD || (t) == SDB_SQLT_BLOB || (t) == SDB_SQLT_CLOB || (t) == SDB_SQLT_RSET)

#define IS_NUM_DATATYPE(t) (t == SDB_SQLT_NUM || t == SDB_SQLT_BDOUBLE || t == SDB_SQLT_INT || \
                            t == SDB_SQLT_FLT || t == SDB_SQLT_BFLOAT  || t == SDB_SQLT_UIN || \
                            t == SDB_SQLT_VNU)

#define IS_DEFINE_DYNAMIC(t) IS_LONG(t)

#define SDB_MAX_ITEM_LEN 2147483647

#define sdb_oci_var_itemSize(v)   ((v)->itemSize)
#define sdb_oci_var_maxItems(v)   ((v)->maxItems)
#define sdb_oci_var_itemsCount(v) ((v)->itemsCount)
#define sdb_oci_var_isArray(v)    ((v)->isArray)

#define sdb_oci_var_setItemsCount(v, c) ((v)->itemsCount = c)
#define sdb_oci_var_setArray(v, b)      ((v)->isArray = b)

/*
#define SDB_VAR_INTERNAL_TYPE_BOOL    0
#define SDB_VAR_INTERNAL_TYPE_STRING  1
#define SDB_VAR_INTERNAL_TYPE_LOB     2
#define SDB_VAR_INTERNAL_TYPE_ROWID   3
#define SDB_VAR_INTERNAL_TYPE_STMT    4
#define SDB_VAR_INTERNAL_TYPE_INT64   5
#define SDB_VAR_INTERNAL_TYPE_DOUBLE  6
#define SDB_VAR_INTERNAL_TYPE_FLOAT   7
#define SDB_VAR_INTERNAL_TYPE_DATE    8
*/

struct _SDB_OCI_NUM {
   unsigned char value[22];
};

struct _SDB_OCI_DATE {
   char value[7];
};

struct _SDB_OCI_BUFFER {
   CFL_UINT32 capacity;
   CFL_UINT32 length;
   CFL_UINT32 read;
   char       data[1];
};

struct _SDB_OCI_VAR {
   SDB_OCI_CONNECTION *conn;
   void               *handle;
   CFL_UINT16          dataType;
   CFL_INT64           itemSize;
   CFL_UINT32          itemsCount;
   CFL_UINT32          maxItems;
   CFL_BOOL            isDynamic;
   CFL_BOOL            isArray;
   CFL_BOOL            is32BitLen;
   CFL_BOOL            needPrefetch;
   union {
      CFL_UINT8       *data;
      char            *charData;
      CFL_INT64       *int64Data;
      double          *doubleData;
      SDB_OCI_NUM     *numData;
      int             *boolData;
      SDB_OCI_DATE    *dateData;
      void           **pointerData;
      SDB_OCI_BUFFER **bufferData;
   };
   CFL_INT16          *indicator;
   CFL_UINT16         *returnCodes;
   union {
      void           **descriptors;
      SDB_OCI_LOB    **lobs;
      SDB_OCI_STMT   **statements;
      SDB_OCI_ROWID  **rowids;
   };
   union {
      void            *dataLen;
      CFL_UINT16      *dataLen16;
      CFL_UINT32      *dataLen32;
   };
   CFL_UINT8           buffer[1];
};

extern SDB_OCI_VAR *sdb_oci_var_new(SDB_OCI_CONNECTION *conn, CFL_UINT16 dataType, CFL_INT64 itemSize, CFL_UINT32 maxItems, CFL_BOOL bBindOut);
extern SDB_OCI_VAR *sdb_oci_var_new2(SDB_OCI_CONNECTION *conn, CFL_UINT16 dataType, CFL_INT64 itemSize, CFL_UINT32 maxItems, CFL_BOOL bBindOut);
extern void sdb_oci_var_free(SDB_OCI_VAR *var);
extern CFL_BOOL sdb_oci_var_fitsContent(SDB_OCI_VAR *var, CFL_UINT16 dataType, CFL_INT64 itemSize, CFL_UINT32 maxItems, CFL_BOOL bBindOut);

/* get buffers functions */
extern void *sdb_oci_var_getDataBuffer(SDB_OCI_VAR *var);
extern CFL_UINT16 *sdb_oci_var_getLenBuffer16(SDB_OCI_VAR *var);
extern CFL_UINT32 *sdb_oci_var_getLenBuffer32(SDB_OCI_VAR *var);
extern void *sdb_oci_var_getIndBuffer(SDB_OCI_VAR *var);
extern CFL_UINT16 *sdb_oci_var_getRetCodBuffer(SDB_OCI_VAR *var);

/* get functions */
extern CFL_BOOL sdb_oci_var_isNull(SDB_OCI_VAR *var, CFL_UINT32 index);
extern CFL_BOOL sdb_oci_var_getBool(SDB_OCI_VAR *var, CFL_UINT32 index);
extern char *sdb_oci_var_getString(SDB_OCI_VAR *var, CFL_UINT32 index);
extern CFL_UINT32 sdb_oci_var_getStringLen(SDB_OCI_VAR *var, CFL_UINT32 index);
extern SDB_OCI_LOB * sdb_oci_var_getLob(SDB_OCI_VAR *var, CFL_UINT32 index);
extern SDB_OCI_ROWID * sdb_oci_var_getRowId(SDB_OCI_VAR *var, CFL_UINT32 index);
extern SDB_OCI_STMT * sdb_oci_var_getStmt(SDB_OCI_VAR *var, CFL_UINT32 index);
extern CFL_INT64 sdb_oci_var_getInt64(SDB_OCI_VAR *var, CFL_UINT32 index);
extern double sdb_oci_var_getDouble(SDB_OCI_VAR *var, CFL_UINT32 index);
extern void sdb_oci_var_getDate(SDB_OCI_VAR *var, CFL_UINT32 index, CFL_INT16 *year, CFL_INT8 *month, CFL_INT8 *day,
        CFL_INT8 *hour, CFL_INT8 *min, CFL_INT8 *sec);

/* set functions */
extern CFL_BOOL sdb_oci_var_resetDynamicBuffer(SDB_OCI_VAR *var, CFL_UINT32 index);
extern CFL_BOOL sdb_oci_var_setNull(SDB_OCI_VAR *var, CFL_UINT32 index);
extern CFL_BOOL sdb_oci_var_setString(SDB_OCI_VAR *var, CFL_UINT32 index, const char *str, CFL_UINT32 strLen);
extern CFL_BOOL sdb_oci_var_setBool(SDB_OCI_VAR *var, CFL_UINT32 index, CFL_BOOL value);
extern CFL_BOOL sdb_oci_var_setInt64(SDB_OCI_VAR *var, CFL_UINT32 index, CFL_INT64 value);
extern CFL_BOOL sdb_oci_var_setDouble(SDB_OCI_VAR *var, CFL_UINT32 index, double value);
extern CFL_BOOL sdb_oci_var_setDate(SDB_OCI_VAR *var, CFL_UINT32 index, CFL_INT16 year, CFL_INT8 month, CFL_INT8 day, 
        CFL_INT8 hour, CFL_INT8 min, CFL_INT8 sec);
extern CFL_BOOL sdb_oci_var_setRowId(SDB_OCI_VAR *var, CFL_UINT32 index, SDB_OCI_ROWID *descriptor);
extern CFL_BOOL sdb_oci_var_setLob(SDB_OCI_VAR *var, CFL_UINT32 index, SDB_OCI_LOB *descriptor);
extern CFL_BOOL sdb_oci_var_setStmt(SDB_OCI_VAR *var, CFL_UINT32 index, SDB_OCI_STMT *stmt);

extern CFL_INT32 sdb_oci_var_inCallback(SDB_OCI_VAR *var, void *bindp, CFL_UINT32 iter, CFL_UINT32 index, void **bufpp, 
        CFL_UINT32 *alenp, CFL_UINT8 *piecep, void **indpp);
extern CFL_INT32 sdb_oci_var_outCallback(SDB_OCI_VAR *var, void *bindp, CFL_UINT32 iter, CFL_UINT32 index, void **bufpp, 
        CFL_UINT32 **alenpp, CFL_UINT8 *piecep, void **indpp, CFL_UINT16 **rcodepp);

extern CFL_INT32 sdb_oci_var_defineCallback(SDB_OCI_VAR *var, void *defnp, CFL_UINT32 iter, void **bufpp, 
        CFL_UINT32 **alenpp, CFL_UINT8 *piecep, void **indpp, CFL_UINT16 **rcodepp);

extern CFL_BOOL sdb_oci_var_prepareToFetch(SDB_OCI_VAR *var);
extern void sdb_oci_var_prepareFetch(SDB_OCI_VAR *var);

#endif
