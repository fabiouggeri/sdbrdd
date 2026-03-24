#ifndef SDB_OCI_LIB_H_

#define SDB_OCI_LIB_H_

#if defined(__BORLANDC__) && __BORLANDC__ < 0x0600
#include <cstddef.h>
#endif

#include "cfl_types.h"
#include "sdb_oci_types.h"

#ifdef _MSC_VER
   #if _MSC_VER < 1900
      #define PRId64                  "I64d"
      #define PRIu64                  "I64u"
      #define snprintf                _snprintf
   #endif
#elif defined(__BORLANDC__)
   #define PRId64                   "I64d"
   #define PRIu64                   "I64u"
   #define ERROR_MUI_FILE_NOT_FOUND 15100L
#endif

/*================================================================================================================================*/
/*                                                      From OCI Headers                                                          */
/*================================================================================================================================*/
#define SDB_OCI_DEFAULT         0x00000000
#define SDB_OCI_THREADED        0x00000001  /* appl. in threaded environment */
#define SDB_OCI_OBJECT          0x00000002  /* application in object environment */
#define SDB_OCI_EVENTS          0x00000004  /* application is enabled for events */

/*------------------------Error Return Values--------------------------------*/
#define SDB_OCI_SUCCESS              0      /* maps to SQL_SUCCESS of SAG CLI */
#define SDB_OCI_SUCCESS_WITH_INFO    1      /* maps to SQL_SUCCESS_WITH_INFO */
#define SDB_OCI_RESERVED_FOR_INT_USE 200    /* reserved */
#define SDB_OCI_NO_DATA              100    /* maps to SQL_NO_DATA */
#define SDB_OCI_ERROR                -1     /* maps to SQL_ERROR */
#define SDB_OCI_INVALID_HANDLE       -2     /* maps to SQL_INVALID_HANDLE */
#define SDB_OCI_NEED_DATA            99     /* maps to SQL_NEED_DATA */
#define SDB_OCI_STILL_EXECUTING      -3123  /* OCI would block error */

/*--------------------- User Callback Return Values -------------------------*/
#define SDB_OCI_CONTINUE      -24200    /* Continue with the body of the OCI function */
#define SDB_OCI_ROWCBK_DONE   -24201    /* done with user row callback */

#define SDB_OCI_CHARSET_ID_ASCII     1
#define SDB_OCI_CHARSET_ID_UTF8      873
#define SDB_OCI_CHARSET_ID_UTF16     1000
#define SDB_OCI_CHARSET_ID_UTF16BE   2000
#define SDB_OCI_CHARSET_ID_UTF16LE   2002
#define SDB_OCI_CHARSET_NAME_ASCII   "ASCII"
#define SDB_OCI_CHARSET_NAME_UTF8    "UTF-8"
#define SDB_OCI_CHARSET_NAME_UTF16   "UTF-16"
#define SDB_OCI_CHARSET_NAME_UTF16BE "UTF-16BE"
#define SDB_OCI_CHARSET_NAME_UTF16LE "UTF-16LE"

// define handle types used for allocating OCI handles
#define SDB_OCI_HTYPE_FIRST                 1                     /* start value of handle type */
#define SDB_OCI_HTYPE_ENV                   1                     /* environment handle */
#define SDB_OCI_HTYPE_ERROR                 2                     /* error handle */
#define SDB_OCI_HTYPE_SVCCTX                3                     /* service handle */
#define SDB_OCI_HTYPE_STMT                  4                     /* statement handle */
#define SDB_OCI_HTYPE_BIND                  5                     /* bind handle */
#define SDB_OCI_HTYPE_DEFINE                6                     /* define handle */
#define SDB_OCI_HTYPE_DESCRIBE              7                     /* describe handle */
#define SDB_OCI_HTYPE_SERVER                8                     /* server handle */
#define SDB_OCI_HTYPE_SESSION               9                     /* authentication handle */
#define SDB_OCI_HTYPE_AUTHINFO             SDB_OCI_HTYPE_SESSION  /* SessionGet auth handle */
#define SDB_OCI_HTYPE_TRANS                10                     /* transaction handle */
#define SDB_OCI_HTYPE_COMPLEXOBJECT        11                     /* complex object retrieval handle */
#define SDB_OCI_HTYPE_SECURITY             12                     /* security handle */
#define SDB_OCI_HTYPE_SUBSCRIPTION         13                     /* subscription handle */
#define SDB_OCI_HTYPE_DIRPATH_CTX          14                     /* direct path context */
#define SDB_OCI_HTYPE_DIRPATH_COLUMN_ARRAY 15                     /* direct path column array */
#define SDB_OCI_HTYPE_DIRPATH_STREAM       16                     /* direct path stream */
#define SDB_OCI_HTYPE_PROC                 17                     /* process handle */
#define SDB_OCI_HTYPE_DIRPATH_FN_CTX       18                     /* direct path function context */
#define SDB_OCI_HTYPE_DIRPATH_FN_COL_ARRAY 19                     /* dp object column array */
#define SDB_OCI_HTYPE_XADSESSION           20                     /* access driver session */
#define SDB_OCI_HTYPE_XADTABLE             21                     /* access driver table */
#define SDB_OCI_HTYPE_XADFIELD             22                     /* access driver field */
#define SDB_OCI_HTYPE_XADGRANULE           23                     /* access driver granule */
#define SDB_OCI_HTYPE_XADRECORD            24                     /* access driver record */
#define SDB_OCI_HTYPE_XADIO                25                     /* access driver I/O */
#define SDB_OCI_HTYPE_CPOOL                26                     /* connection pool handle */
#define SDB_OCI_HTYPE_SPOOL                27                     /* session pool handle */
#define SDB_OCI_HTYPE_ADMIN                28                     /* admin handle */
#define SDB_OCI_HTYPE_EVENT                29                     /* HA event handle */
#define SDB_OCI_HTYPE_LAST                 29                     /* last value of a handle type */

/*-------------------------Descriptor Types----------------------------------*/
#define SDB_OCI_DTYPE_FIRST                50 /* start value of descriptor type */
#define SDB_OCI_DTYPE_LOB                  50 /* lob  locator */
#define SDB_OCI_DTYPE_SNAP                 51 /* snapshot descriptor */
#define SDB_OCI_DTYPE_RSET                 52 /* result set descriptor */
#define SDB_OCI_DTYPE_PARAM                53 /* a parameter descriptor obtained from ocigparm */
#define SDB_OCI_DTYPE_ROWID                54  /* rowid descriptor */
#define SDB_OCI_DTYPE_COMPLEXOBJECTCOMP    55 /* complex object retrieval descriptor */
#define SDB_OCI_DTYPE_FILE                 56  /* File Lob locator */
#define SDB_OCI_DTYPE_AQENQ_OPTIONS        57 /* enqueue options */
#define SDB_OCI_DTYPE_AQDEQ_OPTIONS        58 /* dequeue options */
#define SDB_OCI_DTYPE_AQMSG_PROPERTIES     59 /* message properties */
#define SDB_OCI_DTYPE_AQAGENT              60 /* aq agent */
#define SDB_OCI_DTYPE_LOCATOR              61 /* LOB locator */
#define SDB_OCI_DTYPE_INTERVAL_YM          62 /* Interval year month */
#define SDB_OCI_DTYPE_INTERVAL_DS          63 /* Interval day second */
#define SDB_OCI_DTYPE_AQNFY_DESCRIPTOR     64 /* AQ notify descriptor */
#define SDB_OCI_DTYPE_DATE                 65 /* Date */
#define SDB_OCI_DTYPE_TIME                 66 /* Time */
#define SDB_OCI_DTYPE_TIME_TZ              67 /* Time with timezone */
#define SDB_OCI_DTYPE_TIMESTAMP            68 /* Timestamp */
#define SDB_OCI_DTYPE_TIMESTAMP_TZ         69 /* Timestamp with timezone */
#define SDB_OCI_DTYPE_TIMESTAMP_LTZ        70 /* Timestamp with local tz */
#define SDB_OCI_DTYPE_UCB                  71 /* user callback descriptor */
#define SDB_OCI_DTYPE_SRVDN                72 /* server DN list descriptor */
#define SDB_OCI_DTYPE_SIGNATURE            73 /* signature */
#define SDB_OCI_DTYPE_RESERVED_1           74 /* reserved for internal use */
#define SDB_OCI_DTYPE_AQLIS_OPTIONS        75 /* AQ listen options */
#define SDB_OCI_DTYPE_AQLIS_MSG_PROPERTIES 76 /* AQ listen msg props */
#define SDB_OCI_DTYPE_CHDES                77 /* Top level change notification desc */
#define SDB_OCI_DTYPE_TABLE_CHDES          78 /* Table change descriptor           */
#define SDB_OCI_DTYPE_ROW_CHDES            79 /* Row change descriptor            */
#define SDB_OCI_DTYPE_CQDES                80 /* Query change descriptor */
#define SDB_OCI_DTYPE_LOB_REGION           81 /* LOB Share region descriptor */
#define SDB_OCI_DTYPE_RESERVED_82          82 /* reserved */
#define SDB_OCI_DTYPE_LAST                 82 /* last value of a descriptor type */

// define values used for getting/setting OCI attributes
#define SDB_OCI_ATTR_DATA_SIZE                      1
#define SDB_OCI_ATTR_DATA_TYPE                      2
#define SDB_OCI_ATTR_ENV                            5
#define SDB_OCI_ATTR_PRECISION                      5
#define SDB_OCI_ATTR_SCALE                          6
#define SDB_OCI_ATTR_NAME                           4
#define SDB_OCI_ATTR_SERVER                         6
#define SDB_OCI_ATTR_SESSION                        7
#define SDB_OCI_ATTR_IS_NULL                        7
#define SDB_OCI_ATTR_TRANS                          8
#define SDB_OCI_ATTR_TYPE_NAME                      8
#define SDB_OCI_ATTR_SCHEMA_NAME                    9
#define SDB_OCI_ATTR_ROW_COUNT                      9
#define SDB_OCI_ATTR_PREFETCH_ROWS                  11
#define SDB_OCI_ATTR_PARAM_COUNT                    18
#define SDB_OCI_ATTR_ROWID                          19
#define SDB_OCI_ATTR_USERNAME                       22
#define SDB_OCI_ATTR_PASSWORD                       23
#define SDB_OCI_ATTR_STMT_TYPE                      24
#define SDB_OCI_ATTR_INTERNAL_NAME                  25
#define SDB_OCI_ATTR_EXTERNAL_NAME                  26
#define SDB_OCI_ATTR_XID                            27
#define SDB_OCI_ATTR_CHARSET_ID                     31
#define SDB_OCI_ATTR_CHARSET_FORM                   32
#define SDB_OCI_ATTR_MAXDATA_SIZE                   33
#define SDB_OCI_ATTR_ROWS_RETURNED                  42
#define SDB_OCI_ATTR_VISIBILITY                     47
#define SDB_OCI_ATTR_CONSUMER_NAME                  50
#define SDB_OCI_ATTR_DEQ_MODE                       51
#define SDB_OCI_ATTR_NAVIGATION                     52
#define SDB_OCI_ATTR_WAIT                           53
#define SDB_OCI_ATTR_DEQ_MSGID                      54
#define SDB_OCI_ATTR_PRIORITY                       55
#define SDB_OCI_ATTR_DELAY                          56
#define SDB_OCI_ATTR_EXPIRATION                     57
#define SDB_OCI_ATTR_CORRELATION                    58
#define SDB_OCI_ATTR_ATTEMPTS                       59
#define SDB_OCI_ATTR_EXCEPTION_QUEUE                61
#define SDB_OCI_ATTR_ENQ_TIME                       62
#define SDB_OCI_ATTR_MSG_STATE                      63
#define SDB_OCI_ATTR_ORIGINAL_MSGID                 69
#define SDB_OCI_ATTR_QUEUE_NAME                     70
#define SDB_OCI_ATTR_NUM_DML_ERRORS                 73
#define SDB_OCI_ATTR_DML_ROW_OFFSET                 74
#define SDB_OCI_ATTR_SUBSCR_NAME                    94
#define SDB_OCI_ATTR_SUBSCR_CALLBACK                95
#define SDB_OCI_ATTR_SUBSCR_CTX                     96
#define SDB_OCI_ATTR_SUBSCR_NAMESPACE               98
#define SDB_OCI_ATTR_REF_TDO                        110
#define SDB_OCI_ATTR_PARAM                          124
#define SDB_OCI_ATTR_PARSE_ERROR_OFFSET             129
#define SDB_OCI_ATTR_SERVER_STATUS                  143
#define SDB_OCI_ATTR_STATEMENT                      144
#define SDB_OCI_ATTR_DEQCOND                        146
#define SDB_OCI_ATTR_SUBSCR_RECPTPROTO              149
#define SDB_OCI_ATTR_CURRENT_POSITION               164
#define SDB_OCI_ATTR_STMTCACHESIZE                  176
#define SDB_OCI_ATTR_BIND_COUNT                     190
#define SDB_OCI_ATTR_TRANSFORMATION                 196
#define SDB_OCI_ATTR_ROWS_FETCHED                   197
#define SDB_OCI_ATTR_SPOOL_STMTCACHESIZE            208
#define SDB_OCI_ATTR_TYPECODE                       216
#define SDB_OCI_ATTR_STMT_IS_RETURNING              218
#define SDB_OCI_ATTR_CURRENT_SCHEMA                 224
#define SDB_OCI_ATTR_SUBSCR_QOSFLAGS                225
#define SDB_OCI_ATTR_COLLECTION_ELEMENT             227
#define SDB_OCI_ATTR_SUBSCR_TIMEOUT                 227
#define SDB_OCI_ATTR_NUM_TYPE_ATTRS                 228
#define SDB_OCI_ATTR_SUBSCR_CQ_QOSFLAGS             229
#define SDB_OCI_ATTR_LIST_TYPE_ATTRS                229
#define SDB_OCI_ATTR_SUBSCR_CQ_REGID                230
#define SDB_OCI_ATTR_SUBSCR_NTFN_GROUPING_CLASS     231
#define SDB_OCI_ATTR_SUBSCR_NTFN_GROUPING_VALUE     232
#define SDB_OCI_ATTR_SUBSCR_NTFN_GROUPING_TYPE      233
#define SDB_OCI_ATTR_SUBSCR_NTFN_GROUPING_REPEAT_COUNT 235
#define SDB_OCI_ATTR_NCHARSET_ID                    262
#define SDB_OCI_ATTR_APPCTX_SIZE                    273
#define SDB_OCI_ATTR_APPCTX_LIST                    274
#define SDB_OCI_ATTR_APPCTX_NAME                    275
#define SDB_OCI_ATTR_APPCTX_ATTR                    276
#define SDB_OCI_ATTR_APPCTX_VALUE                   277
#define SDB_OCI_ATTR_CLIENT_IDENTIFIER              278
#define SDB_OCI_ATTR_CHAR_SIZE                      286
#define SDB_OCI_ATTR_EDITION                        288
#define SDB_OCI_ATTR_CQ_QUERYID                     304
#define SDB_OCI_ATTR_SPOOL_TIMEOUT                  308
#define SDB_OCI_ATTR_SPOOL_GETMODE                  309
#define SDB_OCI_ATTR_SPOOL_BUSY_COUNT               310
#define SDB_OCI_ATTR_SPOOL_OPEN_COUNT               311
#define SDB_OCI_ATTR_MODULE                         366
#define SDB_OCI_ATTR_ACTION                         367
#define SDB_OCI_ATTR_CLIENT_INFO                    368
#define SDB_OCI_ATTR_SUBSCR_PORTNO                  390
#define SDB_OCI_ATTR_CHNF_ROWIDS                    402
#define SDB_OCI_ATTR_CHNF_OPERATIONS                403
#define SDB_OCI_ATTR_CHDES_DBNAME                   405
#define SDB_OCI_ATTR_CHDES_NFYTYPE                  406
#define SDB_OCI_ATTR_NFY_FLAGS                      406
#define SDB_OCI_ATTR_CHDES_XID                      407
#define SDB_OCI_ATTR_MSG_DELIVERY_MODE              407
#define SDB_OCI_ATTR_CHDES_TABLE_CHANGES            408
#define SDB_OCI_ATTR_CHDES_TABLE_NAME               409
#define SDB_OCI_ATTR_CHDES_TABLE_OPFLAGS            410
#define SDB_OCI_ATTR_CHDES_TABLE_ROW_CHANGES        411
#define SDB_OCI_ATTR_CHDES_ROW_ROWID                412
#define SDB_OCI_ATTR_CHDES_ROW_OPFLAGS              413
#define SDB_OCI_ATTR_CHNF_REGHANDLE                 414
#define SDB_OCI_ATTR_CQDES_OPERATION                422
#define SDB_OCI_ATTR_CQDES_TABLE_CHANGES            423
#define SDB_OCI_ATTR_CQDES_QUERYID                  424
#define SDB_OCI_ATTR_DRIVER_NAME                    424
#define SDB_OCI_ATTR_CHDES_QUERIES                  425
#define SDB_OCI_ATTR_CONNECTION_CLASS               425
#define SDB_OCI_ATTR_PURITY                         426
#define SDB_OCI_ATTR_RECEIVE_TIMEOUT                436
#define SDB_OCI_ATTR_LOBPREFETCH_LENGTH             440
#define SDB_OCI_ATTR_SUBSCR_IPADDR                  452
#define SDB_OCI_ATTR_UB8_ROW_COUNT                  457
#define SDB_OCI_ATTR_SPOOL_AUTH                     460
#define SDB_OCI_ATTR_LTXID                          462
#define SDB_OCI_ATTR_DML_ROW_COUNT_ARRAY            469
#define SDB_OCI_ATTR_ERROR_IS_RECOVERABLE           472
#define SDB_OCI_ATTR_TRANSACTION_IN_PROGRESS        484
#define SDB_OCI_ATTR_DBOP                           485
#define SDB_OCI_ATTR_SPOOL_MAX_LIFETIME_SESSION     490
#define SDB_OCI_ATTR_BREAK_ON_NET_TIMEOUT           495
#define SDB_OCI_ATTR_SHARDING_KEY                   496
#define SDB_OCI_ATTR_SUPER_SHARDING_KEY             497
#define SDB_OCI_ATTR_FIXUP_CALLBACK                 501
#define SDB_OCI_ATTR_SPOOL_WAIT_TIMEOUT             506
#define SDB_OCI_ATTR_CALL_TIMEOUT                   531

/*-------------------------Credential Types----------------------------------*/
#define SDB_OCI_CRED_RDBMS       1  /* database username/password */
#define SDB_OCI_CRED_EXT         2  /* externally provided credentials */
#define SDB_OCI_CRED_PROXY       3  /* proxy authentication */

#define SDB_OCI_V7_SYNTAX      2 /* V815 language - for backwards compatibility */
#define SDB_OCI_V8_SYNTAX      3 /* V815 language - for backwards compatibility */
#define SDB_OCI_NTV_SYNTAX     1 /* Use what so ever is the native lang of server */

/*--------------------------- OCI Statement Types ---------------------------*/
#define  SDB_OCI_STMT_UNKNOWN 0                             /* Unknown statement */
#define  SDB_OCI_STMT_SELECT  1                              /* select statement */
#define  SDB_OCI_STMT_UPDATE  2                              /* update statement */
#define  SDB_OCI_STMT_DELETE  3                              /* delete statement */
#define  SDB_OCI_STMT_INSERT  4                              /* Insert Statement */
#define  SDB_OCI_STMT_CREATE  5                              /* create statement */
#define  SDB_OCI_STMT_DROP    6                                /* drop statement */
#define  SDB_OCI_STMT_ALTER   7                               /* alter statement */
#define  SDB_OCI_STMT_BEGIN   8                   /* begin ... (pl/sql statement)*/
#define  SDB_OCI_STMT_DECLARE 9                /* declare .. (pl/sql statement ) */
#define  SDB_OCI_STMT_CALL    10                      /* corresponds to kpu call */

/*------------------------ Transaction Start Flags --------------------------*/
/* NOTE: OCI_TRANS_JOIN and OCI_TRANS_NOMIGRATE not supported in 8.0.X       */
#define SDB_OCI_TRANS_NEW          0x00000001 /* start a new local or global txn */
#define SDB_OCI_TRANS_JOIN         0x00000002 /* join an existing global txn */
#define SDB_OCI_TRANS_RESUME       0x00000004 /* resume the global txn branch */
#define SDB_OCI_TRANS_PROMOTE      0x00000008 /* promote the local txn to global */
#define SDB_OCI_TRANS_STARTMASK    0x000000ff /* mask for start operation flags */
#define SDB_OCI_TRANS_READONLY     0x00000100 /* start a readonly txn */
#define SDB_OCI_TRANS_READWRITE    0x00000200 /* start a read-write txn */
#define SDB_OCI_TRANS_SERIALIZABLE 0x00000400 /* start a serializable txn */
#define SDB_OCI_TRANS_ISOLMASK     0x0000ff00 /* mask for start isolation flags */
#define SDB_OCI_TRANS_LOOSE        0x00010000 /* a loosely coupled branch */
#define SDB_OCI_TRANS_TIGHT        0x00020000 /* a tightly coupled branch */
#define SDB_OCI_TRANS_TYPEMASK     0x000f0000 /* mask for branch type flags */
#define SDB_OCI_TRANS_NOMIGRATE    0x00100000 /* non migratable transaction */
#define SDB_OCI_TRANS_SEPARABLE    0x00200000 /* separable transaction (8.1.6+) */
#define SDB_OCI_TRANS_OTSRESUME    0x00400000 /* OTS resuming a transaction */
#define SDB_OCI_TRANS_OTHRMASK     0xfff00000 /* mask for other start flags */

/*------------------------ Transaction End Flags ----------------------------*/
#define SDB_OCI_TRANS_TWOPHASE      0x01000000  /* use two phase commit */
#define SDB_OCI_TRANS_WRITEBATCH    0x00000001 /* force cmt-redo for local txns */
#define SDB_OCI_TRANS_WRITEIMMED    0x00000002 /* no force cmt-redo */
#define SDB_OCI_TRANS_WRITEWAIT     0x00000004 /* no sync cmt-redo */
#define SDB_OCI_TRANS_WRITENOWAIT   0x00000008 /* sync cmt-redo for local txns */

#define SDB_OCI_BATCH_MODE               0x00000001 /* batch the oci stmt for exec */
#define SDB_OCI_EXACT_FETCH              0x00000002 /* fetch exact rows specified */
#define SDB_OCI_STMT_SCROLLABLE_READONLY 0x00000008 /* if result set is scrollable */
#define SDB_OCI_DESCRIBE_ONLY            0x00000010 /* only describe the statement */
#define SDB_OCI_COMMIT_ON_SUCCESS        0x00000020 /* commit, if successful exec */
#define SDB_OCI_NON_BLOCKING             0x00000040 /* non-blocking */
#define SDB_OCI_BATCH_ERRORS             0x00000080 /* batch errors in array dmls */
#define SDB_OCI_PARSE_ONLY               0x00000100 /* only parse the statement */
#define SDB_OCI_EXACT_FETCH_RESERVED_1   0x00000200 /* reserved */
#define SDB_OCI_SHOW_DML_WARNINGS        0x00000400

/* input data types */
#define SDB_SQLT_CHR            1                        /* (ORANET TYPE) character string */
#define SDB_SQLT_NUM            2                          /* (ORANET TYPE) oracle numeric */
#define SDB_SQLT_INT            3                                 /* (ORANET TYPE) integer */
#define SDB_SQLT_FLT            4                   /* (ORANET TYPE) Floating point number */
#define SDB_SQLT_STR            5                                /* zero terminated string */
#define SDB_SQLT_VNU            6                        /* NUM with preceding length byte */
#define SDB_SQLT_PDN            7                  /* (ORANET TYPE) Packed Decimal Numeric */
#define SDB_SQLT_LNG            8                                                  /* long */
#define SDB_SQLT_VCS            9                             /* Variable character string */
#define SDB_SQLT_NON            10                      /* Null/empty PCC Descriptor entry */
#define SDB_SQLT_RID            11                                                /* rowid */
#define SDB_SQLT_DAT            12                                /* date in oracle format */
#define SDB_SQLT_VBI            15                                 /* binary in VCS format */
#define SDB_SQLT_BFLOAT         21                                   /* Native Binary float*/
#define SDB_SQLT_BDOUBLE        22                                 /* NAtive binary double */
#define SDB_SQLT_BIN            23                                  /* binary data(DTYBIN) */
#define SDB_SQLT_LBI            24                                          /* long binary */
#define SDB_SQLT_UIN            68                                     /* unsigned integer */
#define SDB_SQLT_SLS            91                        /* Display sign leading separate */
#define SDB_SQLT_LVC            94                                  /* Longer longs (char) */
#define SDB_SQLT_LVB            95                                   /* Longer long binary */
#define SDB_SQLT_AFC            96                                      /* Ansi fixed char */
#define SDB_SQLT_AVC            97                                        /* Ansi Var char */
#define SDB_SQLT_IBFLOAT        100                              /* binary float canonical */
#define SDB_SQLT_IBDOUBLE       101                             /* binary double canonical */
#define SDB_SQLT_CUR            102                                        /* cursor  type */
#define SDB_SQLT_RDD            104                                    /* rowid descriptor */
#define SDB_SQLT_LAB            105                                          /* label type */
#define SDB_SQLT_OSL            106                                        /* oslabel type */
#define SDB_SQLT_NTY            108                                   /* named object type */
#define SDB_SQLT_REF            110                                            /* ref type */
#define SDB_SQLT_CLOB           112                                       /* character lob */
#define SDB_SQLT_BLOB           113                                          /* binary lob */
#define SDB_SQLT_BFILEE         114                                     /* binary file lob */
#define SDB_SQLT_CFILEE         115                                  /* character file lob */
#define SDB_SQLT_RSET           116                                     /* result set type */
#define SDB_SQLT_NCO            122      /* named collection type (varray or nested table) */
#define SDB_SQLT_VST            155                                      /* OCIString type */
#define SDB_SQLT_ODT            156                                        /* OCIDate type */
#define SDB_SQLT_DATE           184                                           /* ANSI Date */
#define SDB_SQLT_TIME           185                                                /* TIME */
#define SDB_SQLT_TIME_TZ        186                                 /* TIME WITH TIME ZONE */
#define SDB_SQLT_TIMESTAMP      187                                           /* TIMESTAMP */
#define SDB_SQLT_TIMESTAMP_TZ   188                            /* TIMESTAMP WITH TIME ZONE */
#define SDB_SQLT_INTERVAL_YM    189                              /* INTERVAL YEAR TO MONTH */
#define SDB_SQLT_INTERVAL_DS    190                              /* INTERVAL DAY TO SECOND */
#define SDB_SQLT_TIMESTAMP_LTZ  232                             /* TIMESTAMP WITH LOCAL TZ */
#define SDB_SQLT_PNTY           241                /* pl/sql representation of named types */
#define SDB_SQLT_REC            250                       /* pl/sql 'record' (or %rowtype) */
#define SDB_SQLT_TAB            251                              /* pl/sql 'indexed table' */
#define SDB_SQLT_BOL            252                                    /* pl/sql 'boolean' */
#define SDB_SQLT_FILE           SDB_SQLT_BFILEE                         /* binary file lob */
#define SDB_SQLT_CFILE          SDB_SQLT_CFILEE
#define SDB_SQLT_BFILE          SDB_SQLT_BFILEE

/*------------------------Bind and Define Options----------------------------*/
#define SDB_OCI_SB2_IND_PTR       0x00000001                           /* unused */
#define SDB_OCI_DATA_AT_EXEC      0x00000002             /* data at execute time */
#define SDB_OCI_DYNAMIC_FETCH     0x00000002                /* fetch dynamically */
#define SDB_OCI_PIECEWISE         0x00000004          /* piecewise DMLs or fetch */
#define SDB_OCI_DEFINE_RESERVED_1 0x00000008                         /* reserved */
#define SDB_OCI_BIND_RESERVED_2   0x00000010                         /* reserved */
#define SDB_OCI_DEFINE_RESERVED_2 0x00000020                         /* reserved */
#define SDB_OCI_BIND_SOFT         0x00000040              /* soft bind or define */
#define SDB_OCI_DEFINE_SOFT       0x00000080              /* soft bind or define */
#define SDB_OCI_BIND_RESERVED_3   0x00000100                         /* reserved */
#define SDB_OCI_IOV               0x00000200   /* For scatter gather bind/define */

/*------------------------(Scrollable Cursor) Fetch Options-------------------
 * For non-scrollable cursor, the only valid (and default) orientation is  OCI_FETCH_NEXT */
#define SDB_OCI_FETCH_CURRENT    0x00000001      /* refetching current position  */
#define SDB_OCI_FETCH_NEXT       0x00000002                          /* next row */
#define SDB_OCI_FETCH_FIRST      0x00000004       /* first row of the result set */
#define SDB_OCI_FETCH_LAST       0x00000008    /* the last row of the result set */
#define SDB_OCI_FETCH_PRIOR      0x00000010  /* previous row relative to current */
#define SDB_OCI_FETCH_ABSOLUTE   0x00000020        /* absolute offset from first */
#define SDB_OCI_FETCH_RELATIVE   0x00000040        /* offset relative to current */

/*--------------------------------LOB types ---------------------------------*/
#define SDB_OCI_TEMP_BLOB 1                /* LOB type - BLOB ------------------ */
#define SDB_OCI_TEMP_CLOB 2                /* LOB type - CLOB ------------------ */

/* CHAR/NCHAR/VARCHAR2/NVARCHAR2/CLOB/NCLOB char set "form" information */
#define SDB_SQLCS_IMPLICIT 1     /* for CHAR, VARCHAR2, CLOB w/o a specified set */
#define SDB_SQLCS_NCHAR    2                  /* for NCHAR, NCHAR VARYING, NCLOB */
#define SDB_SQLCS_EXPLICIT 3   /* for CHAR, etc, with "CHARACTER SET ..." syntax */
#define SDB_SQLCS_FLEXIBLE 4                 /* for PL/SQL "flexible" parameters */
#define SDB_SQLCS_LIT_NULL 5      /* for typecheck of NULL and empty_clob() lits */

/*--------------------------- LOB open modes --------------------------------*/
#define SDB_OCI_LOB_READONLY      1         /* readonly mode open for ILOB types */
#define SDB_OCI_LOB_READWRITE     2         /* read write mode open for ILOBs */
#define SDB_OCI_LOB_WRITEONLY     3         /* Writeonly mode open for ILOB types*/
#define SDB_OCI_LOB_APPENDONLY    4         /* Appendonly mode open for ILOB types */
#define SDB_OCI_LOB_FULLOVERWRITE 5         /* Completely overwrite ILOB */
#define SDB_OCI_LOB_FULLREAD      6         /* Doing a Full Read of ILOB */

#define SDB_OCI_ONE_PIECE   0                                         /* one piece */
#define SDB_OCI_FIRST_PIECE 1                                 /* the first piece */
#define SDB_OCI_NEXT_PIECE  2                          /* the next of many pieces */
#define SDB_OCI_LAST_PIECE  3                                   /* the last piece */

/*
 * OCIInd -- a variable of this type contains (null) indicator information
 */
#define SDB_OCI_IND_NOTNULL     0                        /* not NULL */
#define SDB_OCI_IND_NULL        (-1)                     /* NULL */
#define SDB_OCI_IND_BADNULL     (-2)                     /* BAD NULL */
#define SDB_OCI_IND_NOTNULLABLE (-3)                     /* not NULLable */

/*-------------------------- OCINumberToInt ---------------------------------*/
#define SDB_OCI_NUMBER_UNSIGNED 0                        /* Unsigned type -- ubX */
#define SDB_OCI_NUMBER_SIGNED   2                          /* Signed type -- sbX */

#define SDB_OCI_DURATION_SESSION 10
/*================================================================================================================================*/
/*                                                      OCI API internals                                                         */
/*================================================================================================================================*/

/* Errors check macros */
#define STATUS_ERROR(s) ((s) != SDB_OCI_SUCCESS && (s) != SDB_OCI_SUCCESS_WITH_INFO)
#define CHECK_STATUS_RETURN(s, h, m, r) if (STATUS_ERROR(s) && sdb_oci_setErrorFromOCI(h, SDB_OCI_HTYPE_ERROR, m)) return r
#define CHECK_STATUS_ERROR(s, h, m)     if (STATUS_ERROR(s)) sdb_oci_setErrorFromOCI(h, SDB_OCI_HTYPE_ERROR, m)
#define CHECK_STATUS_LOG(s, p)          if (STATUS_ERROR(s)) SDB_LOG_ERROR(p)


#define SDB_OCI_ERROR_LIB_NOT_LOADED     5000
#define SDB_OCI_ERROR_SYMBOL_NOT_LOADED  5001
#define SDB_OCI_ERROR_LOADING_LIB        5002
#define SDB_OCI_ERROR_OLD_CLIENT         5003
#define SDB_OCI_ERROR_CREATE_ENV         5004
#define SDB_OCI_ERROR_GET_ERROR          5005
#define SDB_OCI_ERROR_LOGIN              5006
#define SDB_OCI_ERROR_EXEC_STMT          5007
#define SDB_OCI_ERROR_BEGIN_TRANS        5008
#define SDB_OCI_ERROR_FREE_STMT          5009
#define SDB_OCI_ERROR_STMT_GET           5010
#define SDB_OCI_ERROR_INVALID_DATATYPE   5011
#define SDB_OCI_ERROR_BIND_IN            5012
#define SDB_OCI_ERROR_BIND_OUT           5013
#define SDB_OCI_ERROR_DESCRIPTOR         5014
#define SDB_OCI_ERROR_VAR_CREATE         5015
#define SDB_OCI_ERROR_VAR_SET            5016

#define SDB_OCI_ERROR_MAX_LEN 4096

#define SDB_ORACLE_VERSION_TO_NUMBER(versionNum, releaseNum, updateNum, portReleaseNum, portUpdateNum)  \
        ((versionNum * 100000000) + (releaseNum * 1000000) + (updateNum * 10000) + (portReleaseNum * 100) + (portUpdateNum))

typedef int (*fn_OCIEnvNlsCreate)(void **envp, CFL_UINT32 mode, void *ctxp, void *malocfp, void *ralocfp, void *mfreefp,
                                  size_t xtramem_sz, void **usrmempp, CFL_UINT16 charset, CFL_UINT16 ncharset);
typedef int (*fn_OCIEnvCreate)(void **envp, CFL_UINT32 mode, void *ctxp, void *malocfp, void *ralocfp, void *mfreefp,
                               size_t xtramem_sz, void **usrmempp);
typedef int (*fn_OCISessionBegin)(void *svchp, void *errhp, void *usrhp, CFL_UINT32 credt, CFL_UINT32 mode);
typedef int (*fn_OCISessionEnd)(void *svchp, void *errhp, void *usrhp, CFL_UINT32 mode);
typedef int (*fn_OCIHandleAlloc)(const void *parenth, void **hndlpp, const CFL_UINT32 type, const size_t xtramem_sz,
                                 void **usrmempp);
typedef int (*fn_OCIHandleFree)(void *hndlp, const CFL_UINT32 type);
typedef int (*fn_OCIAttrGet)(const void  *trgthndlp, CFL_UINT32 trghndltyp, void *attributep, CFL_UINT32 *sizep,
                             CFL_UINT32 attrtype, void *errhp);
typedef int (*fn_OCIAttrSet)(void *trgthndlp, CFL_UINT32 trghndltyp, void *attributep, CFL_UINT32 size, CFL_UINT32 attrtype,
                             void *errhp);
typedef int (*fn_OCIServerAttach)(void *srvhp, void *errhp, const char *dblink, CFL_INT32 dblink_len, CFL_UINT32 mode);
typedef int (*fn_OCIServerDetach)(void *srvhp, void *errhp, CFL_UINT32 mode);
typedef int (*fn_OCIStmtPrepare2)(void *svchp, void **stmtp, void *errhp, const char *stmt, CFL_UINT32 stmt_len,
                                  const char *key, CFL_UINT32 key_len, CFL_UINT32 language, CFL_UINT32 mode);
typedef int (*fn_OCIStmtRelease)(void *stmtp, void *errhp, const char *key, CFL_UINT32 key_len, CFL_UINT32 mode);
typedef int (*fn_OCIDefineByPos)(void *stmtp, void **defnp, void *errhp, CFL_UINT32 position, void *valuep, CFL_INT32 value_sz,
                                 CFL_UINT16 dty, void *indp, CFL_UINT16 *rlenp, CFL_UINT16 *rcodep, CFL_UINT32 mode);
typedef int (*fn_OCIDefineByPos2)(void *stmtp, void **defnp, void *errhp, CFL_UINT32 position, void *valuep, CFL_UINT64 value_sz,
                                  CFL_UINT16 dty, void *indp, CFL_UINT32 *rlenp, CFL_UINT16 *rcodep, CFL_UINT32 mode);
typedef int (*fn_OCIDefineDynamic)(void *defnp, void *errhp, void *octxp, void *ocbfp);
typedef int (*fn_OCIStmtExecute)(void *svchp, void *stmtp, void *errhp, CFL_UINT32 iters, CFL_UINT32 rowoff, const void *snap_in,
                                 void *snap_out, CFL_UINT32 mode);
typedef int (*fn_OCIStmtFetch2)(void *stmtp, void *errhp, CFL_UINT32 nrows, CFL_UINT16 orientation, CFL_INT32 scrollOffset,
                                CFL_UINT32 mode);
typedef int (*fn_OCIBindByName)(void *stmtp, void **bindp, void *errhp, const char *placeholder, CFL_INT32 placeh_len,
                                void *valuep, CFL_INT32 value_sz, CFL_UINT16 dty, void *indp, CFL_UINT16 *alenp, CFL_UINT16 *rcodep,
                                CFL_UINT32 maxarr_len, CFL_UINT32 *curelep, CFL_UINT32 mode);
typedef int (*fn_OCIBindByName2)(void *stmtp, void **bindp, void *errhp, const char *placeholder, CFL_INT32 placeh_len,
                                 void *valuep, CFL_INT64 value_sz, CFL_UINT16 dty, void *indp, CFL_UINT32 *alenp, CFL_UINT16 *rcodep,
                                 CFL_UINT32 maxarr_len, CFL_UINT32 *curelep, CFL_UINT32 mode);
typedef int (*fn_OCIBindByPos)(void *stmtp, void **bindp, void *errhp, CFL_UINT32 position, void *valuep, CFL_INT32 value_sz,
                               CFL_UINT16 dty, void *indp, CFL_UINT16 *alenp, CFL_UINT16 *rcodep, CFL_UINT32 maxarr_len,
                               CFL_UINT32 *curelep, CFL_UINT32 mode);
typedef int (*fn_OCIBindByPos2)(void *stmtp, void **bindp, void *errhp, CFL_UINT32 position, void *valuep, CFL_INT64 value_sz,
                                CFL_UINT16 dty, void *indp, CFL_UINT32 *alenp, CFL_UINT16 *rcodep, CFL_UINT32 maxarr_len,
                                CFL_UINT32 *curelep, CFL_UINT32 mode);
typedef int (*fn_OCIBindDynamic)(void *bindp, void *errhp, void *ictxp, void *icbfp, void *octxp, void *ocbfp);
typedef int (*fn_OCITransCommit)(void *svchp, void *errhp, CFL_UINT32 flags);
typedef int (*fn_OCITransPrepare)(void *svchp, void *errhp, CFL_UINT32 flags);
typedef int (*fn_OCITransRollback)(void *svchp, void *errhp, CFL_UINT32 flags);
typedef int (*fn_OCITransStart)(void *svchp, void *errhp, unsigned int timeout, CFL_UINT32 flags);
typedef int (*fn_OCIBreak)(void *hndlp, void *errhp);
typedef void (*fn_OCIClientVersion)(int *major_version, int *minor_version, int *update_num, int *patch_num, int *port_update_num);
typedef int (*fn_OCILobOpen)(void *svchp, void *errhp, void *locp, CFL_UINT8 mode);
typedef int (*fn_OCILobClose)(void *svchp, void *errhp, void *locp);
typedef int (*fn_OCILobRead2)(void *svchp, void *errhp, void *locp, CFL_UINT64 *byte_amtp, CFL_UINT64 *char_amtp, CFL_UINT64 offset,
                              void *bufp, CFL_UINT64 bufl, CFL_UINT8 piece, void *ctxp, void *cbfp, CFL_UINT16 csid, CFL_UINT8 csfrm);
typedef int (*fn_OCILobWrite2)(void *svchp, void *errhp, void *locp, CFL_UINT64 *byte_amtp, CFL_UINT64 *char_amtp, CFL_UINT64 offset,
                               void *bufp, CFL_UINT64 buflen, CFL_UINT8 piece, void *ctxp, void *cbfp, CFL_UINT16 csid, CFL_UINT8 csfrm);
typedef int (*fn_OCILobCreateTemporary)(void *svchp, void *errhp, void *locp, CFL_UINT16 csid, CFL_UINT8 csfrm, CFL_UINT8 lobtype,
                                        int cache, CFL_UINT16 duration);
typedef int (*fn_OCILobFileExists)(void *svchp, void *errhp, void *filep, int *flag);
typedef int (*fn_OCILobFileGetName)(void *envhp, void *errhp, const void *filep, char *dir_alias, CFL_UINT16 *d_length,
                                    char *filename, CFL_UINT16 *f_length);
typedef int (*fn_OCILobFileSetName)(void *envhp, void *errhp, void **filepp, const char *dir_alias, CFL_UINT16 d_length,
                                    const char *filename, CFL_UINT16 f_length);
typedef int (*fn_OCILobFreeTemporary)(void *svchp, void *errhp, void *locp);
typedef int (*fn_OCILobGetChunkSize)(void *svchp, void *errhp, void *locp, CFL_UINT32 *chunksizep);
typedef int (*fn_OCILobGetLength2)(void *svchp, void *errhp, void *locp, CFL_UINT64 *lenp);
typedef int (*fn_OCILobIsOpen)(void *svchp, void *errhp, void *locp, int *flag);
typedef int (*fn_OCILobIsTemporary)(void *envp, void *errhp, void *locp, int *is_temporary);
typedef int (*fn_OCILobLocatorAssign)(void *svchp, void *errhp, const void *src_locp, void **dst_locpp);
typedef int (*fn_OCILobTrim2)(void *svchp, void *errhp, void *locp, CFL_UINT64 newlen);
typedef int (*fn_OCIParamGet)(const void *hndlp, CFL_UINT32 htype, void *errhp, void **parmdpp, CFL_UINT32 pos);
typedef int (*fn_OCIStmtGetBindInfo)(void *stmtp, void *errhp, CFL_UINT32 size, CFL_UINT32 startloc, CFL_INT32 *found, char *bvnp[],
                                     CFL_UINT8 bvnl[], char *invp[], CFL_UINT8 inpl[], CFL_UINT8 dupl[], void **hndl);
typedef int (*fn_OCIStmtGetNextResult)(void *stmthp, void *errhp, void **result, CFL_UINT32 *rtype, CFL_UINT32 mode);
typedef int (*fn_OCIErrorGet)(void *hndlp, CFL_UINT32 recordno, char *sqlstate, CFL_INT32 *errcodep, char *bufp, CFL_UINT32 bufsiz,
                              CFL_UINT32 type);
typedef int (*fn_OCIDescribeAny)(void *svchp, void *errhp, void *objptr, CFL_UINT32 objnm_len, CFL_UINT8 objptr_typ,
                                 CFL_UINT8 info_level, CFL_UINT8 objtyp, void *dschp);
typedef int (*fn_OCIDescriptorAlloc)(const void *parenth, void **descpp, const CFL_UINT32 type, const size_t xtramem_sz,
                                     void **usrmempp);
typedef int (*fn_OCIDescriptorFree)(void *descp, const CFL_UINT32 type);
typedef int (*fn_OCIRowidToChar)(void *rowidDesc, char *outbfp, CFL_UINT16 *outbflp, void *errhp);
typedef int (*fn_OCIPing)(void *svchp, void *errhp, CFL_UINT32 mode);
typedef int (*fn_OCIServerRelease)(void *hndlp, void *errhp, char *bufp, CFL_UINT32 bufsz, CFL_UINT8 hndltype, CFL_UINT32 *version);
typedef int (*fn_OCIServerRelease2)(void *hndlp, void *errhp, char *bufp, CFL_UINT32 bufsz, CFL_UINT8 hndltype, CFL_UINT32 *version,
        CFL_UINT32 mode);
typedef int (*fn_OCINumberFromInt)(void *err, const void *inum, unsigned int inum_length, unsigned int inum_s_flag, void *number);
typedef int (*fn_OCINumberFromReal)(void *err, const void *number, unsigned int rsl_length, void *rsl);
typedef int (*fn_OCINumberToInt)(void *err, const void *number, unsigned int rsl_length, unsigned int rsl_flag, void *rsl);
typedef int (*fn_OCINumberToReal)(void *err, const void *number, unsigned int rsl_length, void *rsl);
typedef void (*fn_OCINumberSetZero)(void *err, const void *number);

struct _SDB_OCI_SYMBOLS {
   fn_OCIEnvNlsCreate       OCIEnvNlsCreate;
   fn_OCIEnvCreate          OCIEnvCreate;
   fn_OCISessionBegin       OCISessionBegin;
   fn_OCISessionEnd         OCISessionEnd;
   fn_OCIHandleAlloc        OCIHandleAlloc;
   fn_OCIHandleFree         OCIHandleFree;
   fn_OCIAttrGet            OCIAttrGet;
   fn_OCIAttrSet            OCIAttrSet;
   fn_OCIServerAttach       OCIServerAttach;
   fn_OCIServerDetach       OCIServerDetach;
   fn_OCIStmtPrepare2       OCIStmtPrepare2;
   fn_OCIStmtRelease        OCIStmtRelease;
   fn_OCIDefineByPos        OCIDefineByPos;
   fn_OCIDefineByPos2       OCIDefineByPos2;
   fn_OCIDefineDynamic      OCIDefineDynamic;
   fn_OCIStmtExecute        OCIStmtExecute;
   fn_OCIStmtFetch2         OCIStmtFetch2;
   fn_OCIBindByName         OCIBindByName;
   fn_OCIBindByName2        OCIBindByName2;
   fn_OCIBindByPos          OCIBindByPos;
   fn_OCIBindByPos2         OCIBindByPos2;
   fn_OCIBindDynamic        OCIBindDynamic;
   fn_OCITransCommit        OCITransCommit;
   fn_OCITransPrepare       OCITransPrepare;
   fn_OCITransRollback      OCITransRollback;
   fn_OCITransStart         OCITransStart;
   fn_OCIClientVersion      OCIClientVersion;
   fn_OCIBreak              OCIBreak;
   fn_OCILobClose           OCILobClose;
   fn_OCILobRead2           OCILobRead2;
   fn_OCILobWrite2          OCILobWrite2;
   fn_OCILobCreateTemporary OCILobCreateTemporary;
   fn_OCILobFileExists      OCILobFileExists;
   fn_OCILobFileGetName     OCILobFileGetName;
   fn_OCILobFileSetName     OCILobFileSetName;
   fn_OCILobFreeTemporary   OCILobFreeTemporary;
   fn_OCILobGetChunkSize    OCILobGetChunkSize;
   fn_OCILobGetLength2      OCILobGetLength2;
   fn_OCILobIsOpen          OCILobIsOpen;
   fn_OCILobIsTemporary     OCILobIsTemporary;
   fn_OCILobLocatorAssign   OCILobLocatorAssign;
   fn_OCILobOpen            OCILobOpen;
   fn_OCILobTrim2           OCILobTrim2;
   fn_OCIParamGet           OCIParamGet;
   fn_OCIStmtGetBindInfo    OCIStmtGetBindInfo;
   fn_OCIStmtGetNextResult  OCIStmtGetNextResult;
   fn_OCIErrorGet           OCIErrorGet;
   fn_OCIDescribeAny        OCIDescribeAny;
   fn_OCIDescriptorAlloc    OCIDescriptorAlloc;
   fn_OCIDescriptorFree     OCIDescriptorFree;
   fn_OCIRowidToChar        OCIRowidToChar;
   fn_OCIPing               OCIPing;
   fn_OCIServerRelease      OCIServerRelease;
   fn_OCIServerRelease2     OCIServerRelease2;
   fn_OCINumberFromInt      OCINumberFromInt;
   fn_OCINumberFromReal     OCINumberFromReal;
   fn_OCINumberToInt        OCINumberToInt;
   fn_OCINumberToReal       OCINumberToReal;
   fn_OCINumberSetZero      OCINumberSetZero;
};

struct _SDB_OCI_VERSION_INFO {
   int versionNum;
   int releaseNum;
   int updateNum;
   int portReleaseNum;
   int portUpdateNum;
   CFL_UINT32 fullVersionNum;
};

struct _SDB_OCI_CONNECTION{
   void                 *errorHandle;
   void                 *serverHandle;
   void                 *serviceHandle;
   void                 *sessionHandle;
   SDB_OCI_VERSION_INFO clientVersion;
   SDB_OCI_VERSION_INFO serverVersion;
   CFL_UINT32           commitMode;
};

extern CFL_BOOL sdb_oci_loadOCILibrary(void);
extern CFL_BOOL sdb_oci_freeOCILibrary(void);
extern SDB_OCI_SYMBOLS * sdb_oci_getSymbols(void);
extern CFL_BOOL sdb_oci_isInitialized(void);
extern CFL_BOOL sdb_oci_createEnv(CFL_UINT32 mode);
extern CFL_BOOL sdb_oci_freeEnv(void);
extern void * sdb_oci_getEnv(void);
extern CFL_BOOL sdb_oci_minimalClientVersion(SDB_OCI_VERSION_INFO *versionInfo, int minVersionNum, int minReleaseNum);
extern void *sdb_oci_getErrorHandle(void);
extern void sdb_oci_freeErrorHandle(void);
extern CFL_BOOL sdb_oci_setErrorFromOCI(void *handle, CFL_UINT32 handleType, const char * message);

#endif /* SDB_ORA_H_ */


