#ifndef _SDB_API_TYPES_H_

#define _SDB_API_TYPES_H_

#include "cfl_types.h"

struct _SDB_DB_API;
typedef struct _SDB_DB_API SDB_DB_API;
typedef struct _SDB_DB_API *SDB_DB_APIP;

struct _SDB_PRODUCT;
typedef struct _SDB_PRODUCT SDB_PRODUCT;
typedef struct _SDB_PRODUCT *SDB_PRODUCTP;

struct _SDB_DATABASE;
typedef struct _SDB_DATABASE SDB_DATABASE;
typedef struct _SDB_DATABASE *SDB_DATABASEP;

struct _SDB_SCHEMA;
typedef struct _SDB_SCHEMA SDB_SCHEMA;
typedef struct _SDB_SCHEMA *SDB_SCHEMAP;

struct _SDB_TABLE;
typedef struct _SDB_TABLE SDB_TABLE;
typedef struct _SDB_TABLE *SDB_TABLEP;

struct _SDB_FIELD;
typedef struct _SDB_FIELD SDB_FIELD;
typedef struct _SDB_FIELD *SDB_FIELDP;

struct _SDB_WAFIELD;
typedef struct _SDB_WAFIELD SDB_WAFIELD;
typedef struct _SDB_WAFIELD *SDB_WAFIELDP;

struct _SDB_INDEX;
typedef struct _SDB_INDEX SDB_INDEX;
typedef struct _SDB_INDEX *SDB_INDEXP;

struct _SDB_RECORD;
typedef struct _SDB_RECORD SDB_RECORD;
typedef struct _SDB_RECORD *SDB_RECORDP;

struct _SDB_RECORDLOCKED;
typedef struct _SDB_RECORDLOCKED SDB_RECORDLOCKED;
typedef struct _SDB_RECORDLOCKED *SDB_RECORDLOCKEDP;

struct _SDB_ERROR;
typedef struct _SDB_ERROR SDB_ERROR;
typedef struct _SDB_ERROR *SDB_ERRORP;

struct _SDB_TRANSACTION;
typedef struct _SDB_TRANSACTION SDB_TRANSACTION;
typedef struct _SDB_TRANSACTION *SDB_TRANSACTIONP;

struct _SDB_CONNECTION;
typedef struct _SDB_CONNECTION SDB_CONNECTION;
typedef struct _SDB_CONNECTION *SDB_CONNECTIONP;

struct _SDB_COMMAND;
typedef struct _SDB_COMMAND SDB_COMMAND;
typedef struct _SDB_COMMAND *SDB_COMMANDP;

struct _SDB_AREA;
typedef struct _SDB_AREA SDB_AREA;
typedef struct _SDB_AREA *SDB_AREAP;

struct _SDB_THREAD_DATA;
typedef struct _SDB_THREAD_DATA SDB_THREAD_DATA;
typedef struct _SDB_THREAD_DATA *SDB_THREAD_DATAP;

struct _SDB_PARAM;
typedef struct _SDB_PARAM SDB_PARAM;
typedef struct _SDB_PARAM *SDB_PARAMP;

struct _SDB_PARAMLIST;
typedef struct _SDB_PARAMLIST SDB_PARAMLIST;
typedef struct _SDB_PARAMLIST *SDB_PARAMLISTP;

struct _SDB_STATEMENT;
typedef struct _SDB_STATEMENT SDB_STATEMENT;
typedef struct _SDB_STATEMENT *SDB_STATEMENTP;

struct _SDB_LOB;
typedef struct _SDB_LOB SDB_LOB;
typedef struct _SDB_LOB *SDB_LOBP;

struct _SDB_QUERY_COL_INFO;
typedef struct _SDB_QUERY_COL_INFO SDB_QUERY_COL_INFO;
typedef struct _SDB_QUERY_COL_INFO *SDB_QUERY_COL_INFOP;

enum _SDB_EXPRESSION_TYPE;
typedef enum _SDB_EXPRESSION_TYPE SDB_EXPRESSION_TYPE;

struct _SDB_EXPRESSION_NODE;
typedef struct _SDB_EXPRESSION_NODE SDB_EXPRESSION_NODE;
typedef struct _SDB_EXPRESSION_NODE *SDB_EXPRESSION_NODEP;

struct _SDB_EXPRESSION_TRANSLATION;
typedef struct _SDB_EXPRESSION_TRANSLATION SDB_EXPRESSION_TRANSLATION;
typedef struct _SDB_EXPRESSION_TRANSLATION *SDB_EXPRESSION_TRANSLATIONP;

struct _SDB_LS_CLIENT;
typedef struct _SDB_LS_CLIENT SDB_LS_CLIENT;
typedef struct _SDB_LS_CLIENT *SDB_LS_CLIENTP;

struct _SDB_DICTIONARY;
typedef struct _SDB_DICTIONARY SDB_DICTIONARY;
typedef struct _SDB_DICTIONARY *SDB_DICTIONARYP;

#endif
