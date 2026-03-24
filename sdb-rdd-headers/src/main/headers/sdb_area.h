#ifndef SDB_AREA_H_

#define SDB_AREA_H_

#include "hbapirdd.h"

#include "cfl_types.h"
#include "cfl_lock.h"
#include "cfl_list.h"
#include "cfl_str.h"

#include "sdb_defs.h"

#define SDB_SORT_NO   0
#define SDB_SORT_DESC 1
#define SDB_SORT_ASC  2

#define SDB_CMD_INSERT           0
#define SDB_CMD_UPDATE_RECNO     1
#define SDB_CMD_UPDATE_ROWID     2
#define SDB_CMD_LOCK_RECNO       3
#define SDB_CMD_LOCK_ROWID       4
#define SDB_CMD_GO_TO_RECNO      5
#define SDB_CMD_GO_TO_ROWID      6
#define SDB_CMD_GO_TOP           7
#define SDB_CMD_GO_BOTTOM        8
#define SDB_CMD_SEEK             9
#define SDB_CMD_SEEK_EXACT      10
#define SDB_CMD_NEXT_RECNO_ASC  11
#define SDB_CMD_NEXT_RECNO_DESC 12
#define SDB_CMD_NEXT_KEY_ASC    13
#define SDB_CMD_NEXT_KEY_DESC   14
#define SDB_CMD_SEEK_LOCK       15
#define SDB_CMD_SEEK_EXACT_LOCK 16

#define SDB_CMD_FIRST_QUERY     SDB_CMD_GO_TOP
#define SDB_CMD_COUNT           17

#define QRY_TOP_ALL 0
#define QRY_TOP_MIN 1

#define QRY_BOTTOM_ALL 0
#define QRY_BOTTOM_MAX 1

#define QRY_MORE_NO       0
#define QRY_MORE_YES      1
#define QRY_MORE_IF_FOUND 2

#define SDB_LOCK_CONTROL_NONE   0
#define SDB_LOCK_CONTROL_DB     10
#define SDB_LOCK_AUTO_GET       20
#define SDB_LOCK_AUTO_PUT       30
#define SDB_LOCK_CONTROL_SERVER 40

#define sdb_area_getFieldByPos(a, i) (((CFL_UINT32)i) < (a)->fieldsCount ? &(a)->fields[(CFL_UINT32)i] : NULL)
#define sdb_area_getCurrentOrder(a) ((a)->uiOrder > 0 ? ((SDB_INDEXP) cfl_list_get((a)->orders, (a)->uiOrder - 1)) : NULL)
#define sdb_area_discardCommand(a, c, s) (c != NULL && ((c)->uiSort != s || (c)->uiOrder != (a)->uiOrder || (c)->isFilterDeleted != hb_setGetDeleted()))

struct _SDB_COMMAND {
   SDB_STATEMENTP pStmt;
   CFL_UINT8      uiOrder;                    /* active index in the moment of command is prepared */
   CFL_UINT8      uiSort;                     /* sort mode for queries */
   CFL_BOOL       isFilterDeleted BIT_FIELD;  /* Filter deleted records? */
   CFL_UINT8      uiQueryMore;                /* Query more records when eof result set */
};

struct _SDB_AREA {
   AREA              area;                         /* (x)Harbour common AREA struct */
   SDB_CONNECTIONP   connection;                   /* Information to do a connection */
   SDB_TABLEP        table;                        /* Tabela aberta */
   SDB_WAFIELDP      fields;                       /* Data fields with additional infos for workarea */
   CFL_UINT32        fieldsCount;                  /* Data fields with additional infos for workarea */
   SDB_WAFIELDP      waPKField;                    /* PK field */
   SDB_WAFIELDP      waDelField;                   /* Del falg field */
   SDB_WAFIELDP      waRowIdField;                 /* RowId field */
   CFL_LISTP         context;                      /* context columns */
   CFL_UINT32        uiRecordLen;                  /* Size of record */
   CFL_UINT8         uiOrder;                      /* Indice ativo */
   CFL_LISTP         orders;                       /* Indices abertos */
   PHB_ITEM          sqlFilter;
   SDB_COMMAND       commands[SDB_CMD_COUNT];      /* prepared statements for common queries */
   SDB_COMMANDP      pCurrentCommand;              /* Current command */
   SDB_STATEMENTP    pStatement;                   /* Current statement */
   CFL_LOCK          lock;
   PHB_ITEM          keyValue;
   SDB_RECORDP       record;                       /* Registro 'quente' em memoria */
   SDB_RECORDLOCKEDP recordLocked;                 /* Lista encadeada de registros bloqueados */
   CFL_UINT8         uiQueryTopMode;               /* Mode for GO TOP queries */
   CFL_UINT8         uiQueryBottomMode;            /* Mode for GO BOTTOM queries */
   CFL_UINT32        lockServerCtxId;              /* Id of context at lock server */
   CFL_UINT16        waitTimeLock;                 /* Time to wait by select for update */
   CFL_UINT8         lockControl;                  /* Lock mode  to workarea: EARLY AUTO LOCK or LATER AUTO LOCK*/
   CFL_STRP          lockId;                       /* Lock id for workarea to use when lock is controlled by database */
   CFL_BOOL          isReadOnly BIT_FIELD;         /* Open for read only */
   CFL_BOOL          isShared BIT_FIELD;           /* Open in shared mode */
   CFL_BOOL          isAppend BIT_FIELD;           /* Appending a value */
   CFL_BOOL          isHot BIT_FIELD;              /* Current record is changed? */
   CFL_BOOL          isInsertImmediate BIT_FIELD;  /* Insert immediate enabled */
   CFL_BOOL          isLocked BIT_FIELD;           /* Area is locked? */
   CFL_BOOL          isQuery BIT_FIELD;            /* is query? */
   CFL_BOOL          isFlushImmediate BIT_FIELD;   /* is slow up update? */
   CFL_BOOL          isContextActive BIT_FIELD;    /* is context active */
   CFL_BOOL          isValidBuffer BIT_FIELD;      /* Buffer was already readed */
   CFL_BOOL          isExactQuery BIT_FIELD;       /* Use exact query when possible */
   CFL_BOOL          isRedoQueries BIT_FIELD;      /* Any change in workarea context than need redo query statements */
   CFL_BOOL          isUnpositioned BIT_FIELD;     /* Record position changed since last record read */
};

extern void sdb_area_init(SDB_AREAP pSDBArea);
extern void sdb_area_clear(SDB_AREAP pSDBArea);
extern void sdb_area_lock(SDB_AREAP pSDBArea);
extern void sdb_area_unlock(SDB_AREAP pSDBArea);
extern void sdb_area_clearContext(SDB_AREAP pSDBArea);
extern const char * sdb_area_getAlias(SDB_AREAP pSDBArea);
extern SDB_WAFIELDP sdb_area_getField(SDB_AREAP pSDBArea, const char *fieldName);
extern SDB_WAFIELDP sdb_area_addField(SDB_AREAP pSDBArea, SDB_FIELDP field);
extern PHB_ITEM sdb_area_getFieldValue(SDB_AREAP pSDBArea, SDB_FIELDP field, PHB_ITEM pValue);
extern void sdb_area_setFieldValue(SDB_AREAP pSDBArea, SDB_FIELDP field, PHB_ITEM pValue);
extern void sdb_area_resetLockId(SDB_AREAP pSDBArea);
extern CFL_STRP sdb_area_getLockId(SDB_AREAP pSDBArea);
extern SDB_INDEXP sdb_area_getOrder(SDB_AREAP pSDBArea, CFL_INT16 order);
extern void sdb_area_closeStatements(SDB_AREAP pSDBArea);
extern void sdb_area_closeQueryStatements(SDB_AREAP pSDBArea);
extern void sdb_area_sortContextByPartitionKeys(SDB_AREAP pSDBArea);
extern CFL_BOOL sdb_area_isSDBArea(AREAP area);

#endif
