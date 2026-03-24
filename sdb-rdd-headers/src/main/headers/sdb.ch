#IfNDef _SDB_CH_

#Define _SDB_CH_

#IfNDef _SDB_RDDNAME_
   #Define _SDB_RDDNAME_ "SDBRDD"
#EndIf

#Define SDB_MEMO_LONG 1
#Define SDB_MEMO_BLOB 2
#Define SDB_MEMO_CLOB 4

#define SDB_FILTER_CLIENT 0
#define SDB_FILTER_SERVER 1

#define QRY_TOP_ALL 0
#define QRY_TOP_MIN 1

#define QRY_BOTTOM_ALL 0
#define QRY_BOTTOM_MAX 1

#define SDB_LOCK_CONTROL_NONE   0 
#define SDB_LOCK_CONTROL_DB     10
#define SDB_LOCK_AUTO_GET       20
#define SDB_LOCK_AUTO_PUT       30
#define SDB_LOCK_CONTROL_SERVER 40

#define SDB_STMT_UNKNOWN 0
#define SDB_STMT_QUERY   1
#define SDB_STMT_DML     2
#define SDB_STMT_DDL     4
#define SDB_STMT_PLSQL   8

#command SET FILTERING ON CLIENT => sdb_SetFilterOn(SDB_FILTER_CLIENT)
#command SET FILTERING ON SERVER => sdb_SetFilterOn(SDB_FILTER_SERVER)

#command SET SQL FILTER TO <cexp> => sdb_SetSqlFilter(<cexp>)

#command SET QUERY PRECISION [TO] <p> => sdb_SetQueryPrecision(<p>)

/* Query execution command */
#command USE <(db)>                 ;
		  AS <select>						;
        [ALIAS <a>]                 ;
        [<new: NEW>]                ;
        [PRECISION <p>]		         ;
	     [<c1log: C1LOGICAL>]        ;
        =>                          ;
		; sdb_prepareQueryOpen(<select>)	;
	   ; dbUseArea(<.new.>, sdb_RddName(), <(db)>, <(a)>, .F., .T.)  

/* stored function execution command */
#command USE <(db)>			  ;
		  AS FUN <funname>	  ;
		  WITH <params>		  ;
        [ALIAS <a>]			  ;
        [<new: NEW>]			  ;
        [PRECISION <p>]		  ;
	     [<c1log: C1LOGICAL>] ;
        => ;
        ; sdb_PrepareFuncCursor(<funname>,<params>,<.c1log.>, <p>) ; 
        ; dbUseArea(<.new.>,sdb_RddName(),<(db)>,<(a)>,.F.,.T.)  

/* stored procedure execution command */
#command USE <(db)>			  ;
		  AS PROC <procname>	  ;
		  WITH <params>		  ;
         [ALIAS <a>]			  ;
         [<new: NEW>]		  ;
         [PRECISION <p>]	  ;
	     [<c1log: C1LOGICAL>] ;
        =>                   ;
        ; sdb_PrepareProcCursor(<procname>,<params>,<.c1log.>, <p>) ;
        ; dbUseArea(<.new.>,sdb_RddName(),<(db)>,<(a)>,.F.,.T.)  

#command BEGIN TRANSACTION => sdb_BeginTransaction() 
#command BEGIN TRANSACTION TIMEOUT <tv> => sdb_BeginTransaction() 
#command COMMIT TRANSACTION => sdb_CommitTransaction() 
#command ROLLBACK TRANSACTION =>	sdb_RollbackTransaction()

// #command SET LOCK INTERVAL [TO] <tv> => sdb_SetLockTimeout(<tv>)

//#command SET APPEND TIMEOUT [TO] => sdb_SetAppendTimeout(0) 
//#command SET APPEND TIMEOUT [TO] <tm> => sdb_SetAppendTimeout(<tm>)

#define SDB_EXPR_ANY        1
#define SDB_EXPR_CONDITION  2
#define SDB_EXPR_EXPRESSION 4
#define SDB_EXPR_TRIGGER    8

#define SDB_CLP_UNKNOWN   0
#define SDB_CLP_CHARACTER 1
#define SDB_CLP_NUMERIC   2
#define SDB_CLP_LOGICAL   4
#define SDB_CLP_DATE      8
#define SDB_CLP_TIMESTAMP 9
#define SDB_CLP_INTEGER   10
#define SDB_CLP_BIGINT    11
#define SDB_CLP_FLOAT     12
#define SDB_CLP_DOUBLE    13
#define SDB_CLP_MEMO_LONG 65
#define SDB_CLP_BLOB      67
#define SDB_CLP_CLOB      68
#define SDB_CLP_IMAGE     70
#define SDB_CLP_LONG_RAW  71
#define SDB_CLP_CURSOR    72
#define SDB_CLP_ROWID     99

#define SDB_LOG_LEVEL_OFF       0
#define SDB_LOG_LEVEL_ERROR     1
#define SDB_LOG_LEVEL_WARN      2
#define SDB_LOG_LEVEL_INFO      3
#define SDB_LOG_LEVEL_DEBUG     4
#define SDB_LOG_LEVEL_TRACE     5

#define SDB_LOG_FORMAT_DEFAULT  0
#define SDB_LOG_FORMAT_GELF     1

#define DBI_SDB_ISRECNO64	   32120	/* does the table use 64-bit RECNO? */
#define DBI_SDB_RECCOUNT64	   32121	/* get record count as 64-bit number */
#define DBI_SDB_DBRUNLOCK64	32122	/* unlock selectively record specified as 64-bit number (DBRUNLOCK(rec64)) */

#EndIf