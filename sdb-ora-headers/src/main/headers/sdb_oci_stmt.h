#ifndef SDB_OCI_STMT_H_

#define SDB_OCI_STMT_H_

#include "cfl_types.h"
#include "cfl_list.h"
#include "cfl_array.h"

#include "sdb_oci_lib.h"
#include "sdb_oci_types.h"
#include "sdb_statement.h"

#define sdb_oci_stmt_getFetchedRows(s)  ((s)->fetchedRows)

#define IS_QUERY(s) (sdb_oci_stmt_getType(s) == SDB_STMT_QUERY)
#define IS_PLSQL(s) (sdb_oci_stmt_getType(s) == SDB_STMT_PLSQL)
#define IS_DDL(s)   (sdb_oci_stmt_getType(s) == SDB_STMT_DDL)
#define IS_DML(s)   (sdb_oci_stmt_getType(s) == SDB_STMT_DML)

struct _SDB_OCI_STMT {
   void               *handle;
   CFL_UINT32         refCount;
   SDB_OCI_CONNECTION *conn;
   CFL_UINT8          type;
   CFL_BOOL           isReturning;
   CFL_BOOL           isPrepared;
   CFL_LISTP          vars;
   CFL_ARRAYP         columnsInfo;
   CFL_UINT32         varsPrefetchCount;
   CFL_UINT32         prefetchRows;
   CFL_UINT32         fetchedRows;
};

struct _SDB_OCI_COL_INFO {
   char       *colName;
   CFL_UINT32 colNameLen;
   CFL_UINT16 dataType;
   CFL_INT16  precision;
   CFL_INT8   scale;
   CFL_UINT32 sizeInBytes;
   CFL_UINT32 sizeInChars;
   CFL_BOOL   isNullable;
};

extern SDB_OCI_STMT * sdb_oci_stmt_new(SDB_OCI_CONNECTION *conn, void *handle);
extern void * sdb_oci_stmt_handle(SDB_OCI_STMT *stmt);
extern void sdb_oci_stmt_free(SDB_OCI_STMT *stmt);
extern CFL_UINT32 sdb_oci_stmt_incRef(SDB_OCI_STMT *stmt);
extern CFL_UINT8 sdb_oci_stmt_type(SDB_OCI_STMT *stmt);
extern CFL_BOOL sdb_oci_stmt_bindByName(SDB_OCI_STMT *stmt, SDB_OCI_VAR *var, char *parName, CFL_UINT32 parNameLen);
extern CFL_BOOL sdb_oci_stmt_bindByPos(SDB_OCI_STMT *stmt, SDB_OCI_VAR *var, CFL_UINT32 pos);
extern CFL_UINT32 sdb_oci_stmt_getPrefetchSize(SDB_OCI_STMT *stmt);
extern CFL_BOOL sdb_oci_stmt_setPrefetchSize(SDB_OCI_STMT *stmt, CFL_UINT32 fetchSize);
extern CFL_BOOL sdb_oci_stmt_execute(SDB_OCI_STMT *stmt, CFL_UINT32 numIters);
extern CFL_UINT64 sdb_oci_stmt_getRowCount(SDB_OCI_STMT *stmt);
extern CFL_UINT32 sdb_oci_stmt_getColCount(SDB_OCI_STMT *stmt);
extern CFL_BOOL sdb_oci_stmt_fetchNext(SDB_OCI_STMT *stmt, CFL_BOOL *bEof);
extern SDB_OCI_COL_INFO *sdb_oci_stmt_getColInfo(SDB_OCI_STMT *stmt, CFL_UINT32 colPos);
extern SDB_OCI_VAR *sdb_oci_stmt_getColVar(SDB_OCI_STMT *stmt, CFL_UINT32 colPos);
extern CFL_UINT32 sdb_oci_stmt_colInfoCount(SDB_OCI_STMT *stmt);
extern CFL_UINT32 sdb_oci_stmt_varCount(SDB_OCI_STMT *stmt);
extern CFL_UINT8 sdb_oci_stmt_getType(SDB_OCI_STMT *stmt);

#endif
