#include <time.h>
#include <string.h>
#include <stdio.h>

#ifdef __XHB__
   #include "hbfast.h"
   #include "hashapi.h"
#else
   #include "hbvmint.h"
#endif

#include "hbvmpub.h"
#include "hbapi.h"
#include "hbapiitm.h"
#include "hbinit.h"
#include "hbapierr.h"
#include "hbapirdd.h"
#include "hbapilng.h"
#include "hbset.h"
#include "hbdate.h"
#include "rddsys.ch"
#include "dbinfo.ch"
#include "hbvm.h"
#include "error.ch"
#include "hbdbferr.h"
#include "hbstack.h"

#ifndef __XHB__
   #include "hbapicls.h"
#endif

#include "sdb.h"
#include "sdb_api.h"
#include "sdb_util.h"
#include "cfl_list.h"
#include "sdb_database.h"
#include "sdb_table.h"
#include "sdb_field.h"
#include "sdb_index.h"
#include "sdb_record.h"
#include "sdb_schema.h"
#include "sdb_thread.h"
#include "sdb_statement.h"
#include "sdb_param.h"
#include "sdb_connection.h"
#include "sdb_area.h"
#include "sdb_error.h"
#include "sdb_log.h"
#include "sdb_info.h"
#include "sdb_expr.h"
#include "sdb_product.h"
#include "cfl_str.h"
#include "sdb_lock_client.h"
#include "sdb_lob.h"

HB_FUNC_EXTERN(SDB_COLUMNINFO);

static void arrayToParams(PHB_ITEM pArray, SDB_PARAMLISTP params, CFL_BOOL bTrim, CFL_BOOL bNullable) {
   HB_SIZE i;
   HB_SIZE parCount = hb_arrayLen(pArray);

   sdb_param_listClear(params);
   for (i = 1; i <= parCount; i++) {
      PHB_ITEM pItemArray = hb_arrayGetItemPtr(pArray, i);
      HB_BOOL bByRef = HB_IS_BYREF(pItemArray);
      SDB_PARAMP param;

      param = sdb_param_listAdd(params, NULL, 0, SDB_CLP_UNKNOWN, 0, bByRef, bTrim, bNullable);
      if (param == NULL) {
         sdb_api_genError(NULL, EG_ARG, SDB_ERROR_INVALID_ARGUMENT, NULL, EF_NONE, "Can't allocate param", NULL, NULL);
         break;
      }
      if (bByRef) {
         pItemArray = hb_itemUnRef(pItemArray);
         sdb_param_bindItem(param, pItemArray);
      }
      sdb_param_setValue(param, pItemArray);
   }
}

void sdb_clp_hashToParams(PHB_ITEM pHash, SDB_PARAMLISTP params, CFL_BOOL bTrim, CFL_BOOL bNullable) {
   HB_SIZE nLen = hb_hashLen(pHash);
   HB_SIZE nPos;

   sdb_param_listClear(params);
   for (nPos = 1; nPos <= nLen; nPos++) {
      PHB_ITEM pKey = hb_hashGetKeyAt(pHash, nPos);
      PHB_ITEM pValue = hb_hashGetValueAt(pHash, nPos);
      char *name = (char *) hb_itemGetCPtr(pKey);
      CFL_UINT32 nameLen = (CFL_UINT32) hb_itemGetCLen(pKey);
      HB_BOOL bByRef = HB_IS_BYREF(pValue);
      SDB_PARAMP param;

      if (name) {
         if (name[0] == ':') {
            ++name;
            --nameLen;
         }
         param = sdb_param_listAdd(params, name, nameLen, SDB_CLP_UNKNOWN, 0, bByRef, bTrim, bNullable);
         if (param == NULL) {
            sdb_api_genError(NULL, EG_ARG, SDB_ERROR_INVALID_ARGUMENT, NULL, EF_NONE, "Can't allocate param", NULL, NULL);
            return;
         }
         if (bByRef) {
            pValue = hb_itemUnRef(pValue);
            sdb_param_bindItem(param, pValue);
         }
         sdb_param_setValue(param, pValue);
      } else {
         sdb_api_genError(NULL, EG_ARG, SDB_ERROR_INVALID_ARGUMENT, NULL, EF_NONE,
                           "The key value of hash items must be a string", NULL, NULL);
         return;
      }
   }
}

static void funParamToClpParamList(int iPar, SDB_PARAMLISTP params, char *name, CFL_UINT32 nameLen,
        SDB_CLP_DATATYPE type, CFL_UINT64 dataLen, CFL_BOOL bTrim, CFL_BOOL bNullable) {
   CFL_BOOL bByRef = HB_ISBYREF(iPar) ? CFL_TRUE : CFL_FALSE;
   PHB_ITEM pValue = hb_param(iPar, HB_IT_ANY);
   SDB_PARAMP param;

   /* ignore initial : */
   if (name != NULL && name[0] == ':') {
      ++name;
      --nameLen;
   }
   param = sdb_param_listAdd(params, name, nameLen, type, dataLen, bByRef, bTrim, bNullable);
   if (param == NULL) {
      sdb_api_genError(NULL, EG_ARG, SDB_ERROR_INVALID_ARGUMENT, NULL, EF_NONE, "Can't allocate param", NULL, NULL);
      return;
   }
   if (bByRef) {
      pValue = hb_itemUnRef(pValue);
      sdb_param_bindItem(param, pValue);
   }
   sdb_param_setValue(param, pValue);
}

static void funParamToStmt(int iPar, SDB_STATEMENTP pStmt, char *name, CFL_UINT32 pos, SDB_CLP_DATATYPE type, CFL_UINT64 dataLen, CFL_BOOL bTrim, CFL_BOOL bNullable) {
   CFL_BOOL bByRef = HB_ISBYREF(iPar) ? CFL_TRUE : CFL_FALSE;
   PHB_ITEM pItem = bByRef ? hb_itemUnRef(hb_param(iPar, HB_IT_ANY)) : hb_param(iPar, HB_IT_ANY);
   SDB_PARAMP param;

   /* ignore initial : */
   if (name != NULL) {
      if (name[0] == ':') {
         ++name;
      }
      param = sdb_stmt_setValue(pStmt, name, pItem, bByRef);
   } else {
      param = sdb_stmt_setValueByPos(pStmt, pos, pItem, bByRef);
   }
   if (param != NULL) {
      if (type != SDB_CLP_UNKNOWN) {
         sdb_param_setType(param, type);
      }
      if (bByRef) {
         sdb_param_bindItem(param, pItem);
      }
      sdb_param_setTrim(param, bTrim);
      sdb_param_setNullable(param, bNullable);
      sdb_param_setLength(param, dataLen);
   }
}

void sdb_clp_functionParamsToParamsList(int iStart, SDB_PARAMLISTP params, CFL_BOOL bPositional, CFL_BOOL bTrim, CFL_BOOL bNullable) {
   int parCount = hb_pcount();
   int i;

   sdb_param_listClear(params);
   if (bPositional) {
      for (i = iStart; i <= parCount; i++) {
         funParamToClpParamList(i, params, NULL, 0, SDB_CLP_UNKNOWN, 0, bTrim, bNullable);
      }
   } else {
      CFL_UINT16 parPosition = 1;
      char parName[6];
      int parNameLen;
      for (i = iStart; i <= parCount; i++) {
         parNameLen = sprintf(parName, "%hu", parPosition++);
         funParamToClpParamList(i, params, parName, parNameLen, SDB_CLP_UNKNOWN, 0, bTrim, bNullable);
      }
   }
}

static SDB_CONNECTIONP sdb_paramConnection(int arg, CFL_BOOL bGenError) {
   SDB_CONNECTIONP conn;
   PHB_ITEM pConn = hb_param(arg, HB_IT_ANY);
   if (pConn == NULL) {
      conn = NULL;
   } else if (HB_IS_POINTER(pConn)) {
      conn = (SDB_CONNECTIONP) hb_itemGetPtr(pConn);
   } else if (HB_IS_NUMERIC(pConn)) {
      conn = sdb_api_getConnection((CFL_UINT16) hb_itemGetNI(pConn));
   } else {
      conn = NULL;
   }
   if (conn == NULL && bGenError) {
      sdb_api_genError(NULL, EG_ARG, SDB_ERROR_INVALID_ARGUMENT, NULL, EF_NONE, "Invalid argument", NULL, NULL);
   }
   return conn;
}

static SDB_CONNECTIONP sdb_getConnection(PHB_ITEM pConn, CFL_BOOL bGenError) {
   SDB_CONNECTIONP conn;
   if (pConn == NULL || HB_IS_NIL(pConn)) {
      conn = sdb_thread_getData()->connection;
      if (conn == NULL && bGenError) {
         sdb_api_genError(NULL, SDB_ERROR_TYPE_SDB, SDB_ERROR_NOT_CONNECTED, NULL, EF_NONE, "Not connected to database", NULL, NULL);
      }
   } else if (HB_IS_POINTER(pConn)) {
      conn = (SDB_CONNECTIONP) hb_itemGetPtr(pConn);
   } else if (HB_IS_NUMERIC(pConn)) {
      conn = sdb_api_getConnection((CFL_UINT32) hb_itemGetNI(pConn));
   } else if (bGenError) {
      conn = NULL;
      sdb_api_genError(NULL, EG_ARG, SDB_ERROR_INVALID_ARGUMENT, NULL, EF_NONE, "Invalid argument", NULL, NULL);
   } else {
      conn = NULL;
   }
   return conn;
}

// sdb_execProcedure("PKG_CCRP_Processamento.FNC_LIQUIDA_AUTOMATICO", cAgencia, cCodigo_coop, cPosto, cUsuario, @cOid, Str( nRotina ) )
static void sdb_executeProcedure(SDB_CONNECTIONP conn, int firstArgPos, const char *apiFunName) {
   PHB_ITEM pFunc;
   SDB_PARAMLISTP params;

   ENTER_FUN_NAME("sdb_executeProcedure");
   sdb_thread_cleanError();
   pFunc = hb_param(firstArgPos, HB_IT_STRING);
   if (pFunc == NULL) {
      sdb_api_genError(NULL, EG_ARG, SDB_ERROR_INVALID_ARGUMENT, NULL, EF_NONE, "Invalid arguments", apiFunName, NULL);
      RETURN;
   }
   if (hb_pcount() > firstArgPos) {
      SDB_THREAD_DATAP thData = sdb_thread_getData();
      params = sdb_param_listNew();
      sdb_clp_functionParamsToParamsList(firstArgPos + 1, params, CFL_TRUE, sdb_thread_isTrimParams(thData), sdb_thread_isNullable(thData));
      sdb_connection_executeProcedure(conn, hb_itemGetCPtr(pFunc), params, CFL_TRUE);
      sdb_param_listFree(params);
   } else {
      sdb_connection_executeProcedure(conn, hb_itemGetCPtr(pFunc), NULL, CFL_TRUE);
   }
   if (sdb_thread_hasError()) {
      sdb_api_genErrorFromSDBError(NULL, NULL, EF_NONE, sdb_thread_getLastError(), hb_itemGetCPtr(pFunc), NULL);
   }
   RETURN;
}

static CFL_UINT8 returnType(PHB_ITEM pItem) {
   if (HB_IS_STRING(pItem)) {
      char cType = hb_itemGetCPtr(pItem)[0];
      switch (cType) {
         case 'C':
         case 'M':
            return SDB_CLP_CHARACTER;
         case 'N':
            return SDB_CLP_NUMERIC;
         case 'D':
            return SDB_CLP_DATE;
         case 'L':
            return SDB_CLP_LOGICAL;
         case 'T':
            return SDB_CLP_TIMESTAMP;
      }
   } else if (HB_IS_NUMERIC(pItem) && sdb_util_isValidType(hb_itemGetNI(pItem))) {
      return hb_itemGetNI(pItem);
   }
   return SDB_CLP_CHARACTER;
}

// sdb_execFunction("N", PKG_CCRP_Processamento.FNC_LIQUIDA_AUTOMATICO", cAgencia, cCodigo_coop, cPosto, cUsuario, @cOid, Str( nRotina ) )
static void sdb_executeFunction(SDB_CONNECTIONP conn, int firstArgPos, const char *apiFunName) {
   PHB_ITEM pFunc;
   SDB_PARAMLISTP params;
   PHB_ITEM pResult;

   ENTER_FUN_NAME("sdb_executeFunction");
   sdb_thread_cleanError();
   pFunc = hb_param(firstArgPos + 1, HB_IT_STRING);
   if (pFunc && hb_pcount() > firstArgPos) {
      SDB_THREAD_DATAP thData = sdb_thread_getData();
      params = sdb_param_listNew();
      sdb_clp_functionParamsToParamsList(firstArgPos + 2, params, CFL_TRUE, sdb_thread_isTrimParams(thData), sdb_thread_isNullable(thData));
      pResult = sdb_connection_executeFunction(conn, hb_itemGetCPtr(pFunc), returnType(hb_param(firstArgPos, HB_IT_ANY)), params, CFL_TRUE);
      sdb_param_listFree(params);
      hb_itemReturnRelease(pResult);
      if (sdb_thread_hasError()) {
         sdb_api_genErrorFromSDBError(NULL, NULL, EF_NONE, sdb_thread_getLastError(), hb_itemGetCPtr(pFunc), NULL);
      }
   } else {
      sdb_api_genError(NULL, EG_ARG, SDB_ERROR_INVALID_ARGUMENT, NULL, EF_NONE, "Invalid arguments", apiFunName, NULL);
   }
   RETURN;
}

// sdb_execSql("Begin :1 := PKG_CCRP_Processamento.FNC_LIQUIDA_AUTOMATICO(:2,:3,:4,:5,:6,:7)"", @lRet, cAgencia, cCodigo_coop, cPosto, cUsuario, @cOid, Str( nRotina ))
// sdb_execSql("Begin :v1 := PKG_CCRP_Processamento.FNC_LIQUIDA_AUTOMATICO(:v2,:v3,:v4,:v5,:v6,:v7)",
//             {":v1" => @lRet, ":v2" => cAgencia, ":v3" => cCodigo_coop, ":v4" => cPosto, ":v5" => cUsuario, ":v6" => @cOid, ":v7" => Str( nRotina )})
// sdb_execSql(oStmt)
static void sdb_execSql(SDB_CONNECTIONP conn, int firstArgPos, const char *apiFunName) {
   SDB_THREAD_DATAP thData;
   PHB_ITEM pSql;
   CFL_UINT64 ulAffectedRows = 0;
   SDB_STATEMENTP pStatement;
   CFL_BOOL bFreeStmt = CFL_FALSE;
   PHB_ITEM pResult;

   ENTER_FUN_NAME("sdb_execSql");
   sdb_thread_cleanError();
   pSql = hb_param(firstArgPos, HB_IT_ANY);
   if (HB_IS_STRING(pSql)) {
      pStatement = sdb_connection_prepareStatementBufferLen(conn, hb_itemGetCPtr(pSql), (CFL_UINT32) hb_itemGetCLen(pSql));
      if (pStatement == NULL) {
         sdb_api_genErrorFromSDBError(NULL, NULL, EF_NONE, sdb_thread_getLastError(), apiFunName, NULL);
         RETURN;
      }
      bFreeStmt = CFL_TRUE;
   } else {
      pStatement = sdb_stmt_itemGet(pSql);
      if (pStatement == NULL) {
         sdb_api_genError(NULL, EG_ARG, SDB_ERROR_INVALID_ARGUMENT, NULL, EF_NONE, "Invalid parameters", apiFunName, NULL);
         RETURN;
      }
   }

   thData = sdb_thread_getData();
   /* Params passed by argument overrides the statement params */
   if (hb_pcount() > firstArgPos) {
      PHB_ITEM pHash = hb_param(firstArgPos + 1, HB_IT_HASH);
      if (pHash) {
         sdb_clp_hashToParams(pHash, sdb_stmt_getParams(pStatement), sdb_thread_isTrimParams(thData), sdb_thread_isNullable(thData));
      } else {
         sdb_clp_functionParamsToParamsList(firstArgPos + 1, sdb_stmt_getParams(pStatement), CFL_TRUE, sdb_thread_isTrimParams(thData), sdb_thread_isNullable(thData));
      }
   /* Verify if statement has a list of params */
   } else if (sdb_thread_hasParams(thData) && ! sdb_stmt_hasParams(pStatement)) {
      sdb_param_listMoveAll(sdb_stmt_getParams(pStatement), sdb_thread_getParams(thData));
      sdb_thread_freeParams(thData);
   }

   pResult = hb_itemNew(NULL);
   if (pStatement->type == SDB_STMT_QUERY) {
      pStatement->bufferFetchSize = 1;
      pStatement->fetchSize = 1;
      if (sdb_stmt_execute(pStatement, &ulAffectedRows)) {
         if (sdb_stmt_fetchNext(pStatement, CFL_TRUE)) {
            pResult = sdb_stmt_getQueryValue(pStatement, 1, pResult);
            if (HB_IS_DOUBLE(pResult)) {
               SDB_QUERY_COL_INFO queryInfo;
               sdb_queryInfo_init(&queryInfo);
               sdb_stmt_getQueryInfo(pStatement, 1, &queryInfo);
               if (queryInfo.precision == 0) {
                  hb_itemPutNDLen(pResult, hb_itemGetND(pResult), pStatement->precision - pStatement->scale - 1, pStatement->scale);
               }
               sdb_queryInfo_free(&queryInfo);
            }
         }
      } else if (sdb_thread_hasError()) {
         sdb_api_genErrorFromSDBError(NULL, NULL, EF_NONE, sdb_thread_getLastError(), apiFunName, NULL);
      }
   } else if (sdb_stmt_execute(pStatement, &ulAffectedRows)) {
      hb_itemPutNInt(pResult, (HB_MAXINT) ulAffectedRows);
   } else if (sdb_thread_hasError()) {
      sdb_api_genErrorFromSDBError(NULL, NULL, EF_NONE, sdb_thread_getLastError(), apiFunName, NULL);
   }

   if (bFreeStmt) {
      sdb_stmt_free(pStatement);
      if (sdb_thread_hasError()) {
         sdb_api_genErrorFromSDBError(NULL, NULL, EF_NONE, sdb_thread_getLastError(), apiFunName, NULL);
      }
   }

   hb_itemReturnRelease(pResult);
   if (sdb_thread_hasError()) {
      sdb_api_genErrorFromSDBError(NULL, NULL, EF_NONE, sdb_thread_getLastError(), apiFunName, NULL);
   }
   RETURN;
}

static void sdb_execSqlMany(SDB_CONNECTIONP conn, int firstArgPos, const char *apiFunName) {
   SDB_THREAD_DATAP thData;
   PHB_ITEM pSql;
   CFL_UINT64 ulAffectedRows = 0;
   SDB_STATEMENTP pStatement;
   CFL_BOOL bFreeStmt = CFL_FALSE;

   ENTER_FUN_NAME("sdb_execSqlMany");
   sdb_thread_cleanError();
   pSql = hb_param(firstArgPos, HB_IT_ANY);
   if (HB_IS_STRING(pSql)) {
      pStatement = sdb_connection_prepareStatementBufferLen(conn, hb_itemGetCPtr(pSql), (CFL_UINT32) hb_itemGetCLen(pSql));
      if (pStatement == NULL) {
         sdb_api_genErrorFromSDBError(NULL, NULL, EF_NONE, sdb_thread_getLastError(), apiFunName, NULL);
         RETURN;
      }
      bFreeStmt = CFL_TRUE;
   } else {
      pStatement = sdb_stmt_itemGet(pSql);
      if (pStatement == NULL) {
         sdb_api_genError(NULL, EG_ARG, SDB_ERROR_INVALID_ARGUMENT, NULL, EF_NONE, "Invalid parameters", apiFunName, NULL);
         RETURN;
      }
   }
   thData = sdb_thread_getData();
   /* Params passed by argument overrides the statement params */
   if (hb_pcount() > firstArgPos) {
      PHB_ITEM pHash = hb_param(firstArgPos + 1, HB_IT_HASH);
      if (pHash) {
         sdb_clp_hashToParams(pHash, sdb_stmt_getParams(pStatement), sdb_thread_isTrimParams(thData), sdb_thread_isNullable(thData));
      } else {
         sdb_clp_functionParamsToParamsList(firstArgPos + 1, sdb_stmt_getParams(pStatement), CFL_TRUE, sdb_thread_isTrimParams(thData), sdb_thread_isNullable(thData));
      }
   /* Verify if statement has a list of params */
   } else if (! sdb_stmt_hasParams(pStatement) && sdb_thread_hasParams(thData)) {
      sdb_param_listMoveAll(sdb_stmt_getParams(pStatement), sdb_thread_getParams(thData));
      sdb_thread_freeParams(thData);
   }

   sdb_stmt_executeMany(pStatement, &ulAffectedRows);
   hb_retnint(ulAffectedRows);

   if (bFreeStmt) {
      sdb_stmt_free(pStatement);
   }
   if (sdb_thread_hasError()) {
      sdb_api_genErrorFromSDBError(NULL, NULL, EF_NONE, sdb_thread_getLastError(), apiFunName, NULL);
   }
   RETURN;
}

// sdb_execStmt(pStmt)
static void sdb_execStmt(SDB_STATEMENTP pStatement, int firstArgPos, const char *apiFunName) {
   CFL_UINT64 ulAffectedRows = 0;
   CFL_BOOL bSuccess;

   ENTER_FUN_NAME("sdb_execStmt");
   sdb_thread_cleanError();
   if (pStatement == NULL) {
      sdb_api_genError(NULL, EG_ARG, SDB_ERROR_INVALID_ARGUMENT, NULL, EF_NONE,
                       "Invalid arguments: argument 1 must be a statement", apiFunName, NULL);
      RETURN;
   }

   /* Params passed by argument overrides the statement params */
   if (hb_pcount() > firstArgPos) {
      SDB_THREAD_DATAP thData = sdb_thread_getData();
      PHB_ITEM pHash = hb_param(firstArgPos + 1, HB_IT_HASH);
      if (pHash) {
         sdb_clp_hashToParams(pHash, sdb_stmt_getParams(pStatement), sdb_thread_isTrimParams(thData), sdb_thread_isNullable(thData));
      } else {
         sdb_clp_functionParamsToParamsList(firstArgPos + 1, sdb_stmt_getParams(pStatement), CFL_TRUE, sdb_thread_isTrimParams(thData), sdb_thread_isNullable(thData));
      }
   }

   bSuccess = sdb_stmt_execute(pStatement, &ulAffectedRows);
   if (!bSuccess && sdb_thread_hasError()) {
      sdb_api_genErrorFromSDBError(NULL, NULL, EF_NONE, sdb_thread_getLastError(), apiFunName, NULL);
      RETURN;
   }
   if (pStatement->type == SDB_STMT_DML) {
      hb_retnint(ulAffectedRows);
   } else {
      hb_retl((HB_BOOL) bSuccess);
   }

   RETURN;
}

// sdb_prepareQueryOpen( "SELECT * FROM TABELA WHERE nome=:1", cNome )
static void sdb_prepareQueryOpen(SDB_CONNECTIONP conn, int firstArgPos, const char *apiFunName) {
   SDB_STATEMENTP pStatement;
   SDB_THREAD_DATAP thData;
   PHB_ITEM pSql;

   ENTER_FUN_NAME("sdb_prepareQueryOpen");
   sdb_thread_cleanError();
   thData = sdb_thread_getData();
   pSql = hb_param(firstArgPos, HB_IT_ANY);
   if (HB_IS_STRING(pSql)) {
      if (thData->pStatement != NULL) {
         sdb_stmt_free(thData->pStatement);
         thData->pStatement = NULL;
         if (sdb_thread_hasError()) {
            sdb_api_genErrorFromSDBError(NULL, NULL, EF_NONE, sdb_thread_getLastError(), apiFunName, NULL);
            RETURN;
         }
      }
      pStatement = sdb_connection_prepareStatementBufferLen(conn, hb_itemGetCPtr(pSql), (CFL_UINT32) hb_itemGetCLen(pSql));
      if (pStatement == NULL) {
         sdb_api_genErrorFromSDBError(NULL, NULL, EF_NONE, sdb_thread_getLastError(), apiFunName, NULL);
         RETURN;
      }
      pStatement->isReleaseOnClose = CFL_TRUE;
      thData->pStatement = pStatement;
      if (hb_pcount() > firstArgPos) {
         PHB_ITEM pHash = hb_param(firstArgPos + 1, HB_IT_HASH);
         if (pHash) {
            sdb_clp_hashToParams(pHash, sdb_stmt_getParams(pStatement), sdb_thread_isTrimParams(thData), sdb_thread_isNullable(thData));
         } else {
            sdb_clp_functionParamsToParamsList(firstArgPos + 1, sdb_stmt_getParams(pStatement), CFL_TRUE, sdb_thread_isTrimParams(thData), sdb_thread_isNullable(thData));
         }
      } else if (sdb_thread_hasParams(thData)) {
         sdb_param_listMoveAll(sdb_stmt_getParams(pStatement), sdb_thread_getParams(thData));
         sdb_thread_freeParams(thData);
      }

   } else {
      pStatement = sdb_stmt_itemGet(pSql);
      if (pStatement == NULL) {
         sdb_api_genError(NULL, EG_ARG, SDB_ERROR_INVALID_ARGUMENT, NULL, EF_NONE, "Invalid arguments", apiFunName, NULL);
         RETURN;
      }
      if (pStatement->connection != conn) {
         sdb_api_genError(NULL, EG_ARG, SDB_ERROR_INVALID_ARGUMENT, NULL, EF_NONE,
                           "Statement prepared with another connection", apiFunName, NULL);
         RETURN;
      }
      if (thData->pStatement != pStatement) {
         /* Previous prepared query was not used? */
         if (thData->pStatement != NULL && thData->pStatement->isReleaseOnClose) {
            sdb_stmt_free(thData->pStatement);
            thData->pStatement = NULL;
            if (sdb_thread_hasError()) {
               sdb_api_genErrorFromSDBError(NULL, NULL, EF_NONE, sdb_thread_getLastError(), apiFunName, NULL);
               RETURN;
            }
         }
         thData->pStatement = pStatement;
      }
      if (! pStatement->isCursor ) {
         if (hb_pcount() > firstArgPos) {
            PHB_ITEM pHash = hb_param(firstArgPos + 1, HB_IT_HASH);
            if (pHash) {
               sdb_clp_hashToParams(pHash, sdb_stmt_getParams(pStatement), sdb_thread_isTrimParams(thData), sdb_thread_isNullable(thData));
            } else {
               sdb_clp_functionParamsToParamsList(firstArgPos + 1, sdb_stmt_getParams(pStatement), CFL_TRUE, sdb_thread_isTrimParams(thData), sdb_thread_isNullable(thData));
            }
         /* Verify if statement has a list of params */
         } else if (! sdb_stmt_hasParams(pStatement) && sdb_thread_hasParams(thData)) {
            sdb_param_listMoveAll(sdb_stmt_getParams(pStatement), sdb_thread_getParams(thData));
            sdb_thread_freeParams(thData);
         }
      } else if (hb_pcount() > firstArgPos) {
         sdb_api_genError(NULL, EG_ARG, SDB_ERROR_INVALID_ARGUMENT, NULL, EF_NONE,
                           "Cursor can't receive arguments", apiFunName, NULL);
      }
   }
   RETURN;
}

static void sdb_prepareStatement(SDB_CONNECTIONP conn, int firstArgPos, const char *apiFunName) {
   PHB_ITEM pSql;
   SDB_STATEMENTP pStatement;

   ENTER_FUN_NAME("sdb_prepareStatement");
   sdb_thread_cleanError();
   pSql = hb_param(firstArgPos, HB_IT_STRING);
   if (pSql == NULL) {
      sdb_api_genError(NULL, EG_ARG, SDB_ERROR_INVALID_ARGUMENT, NULL, EF_NONE, "Invalid arguments", apiFunName, NULL);
      RETURN;
   }

   pStatement = sdb_connection_prepareStatementBufferLen(conn, hb_itemGetCPtr(pSql), (CFL_UINT32) hb_itemGetCLen(pSql));
   if (pStatement == NULL) {
      sdb_api_genErrorFromSDBError(NULL, NULL, EF_NONE, sdb_thread_getLastError(), apiFunName, NULL);
      RETURN;
   }
   hb_itemReturnRelease(sdb_stmt_itemPut(NULL, pStatement));
   RETURN;
}

static void beginTransaction(SDB_CONNECTIONP conn, int firstArgPos, const char *apiFunName) {
   PHB_ITEM pFormatId;
   PHB_ITEM pGlobalId;
   PHB_ITEM pBranchId;

   sdb_thread_cleanError();
   if (conn->transaction == NULL) {
      CFL_INT32 formatId;
      const char *globalId;
      const char *branchId;

      pFormatId = hb_param(firstArgPos, HB_IT_NUMERIC);
      formatId = pFormatId != NULL ? hb_itemGetNL(pFormatId) : -1;

      pGlobalId = hb_param(firstArgPos + 1, HB_IT_STRING);
      globalId = pGlobalId != NULL ? hb_itemGetCPtr(pGlobalId) : NULL;

      pBranchId = hb_param(firstArgPos + 2, HB_IT_STRING);
      branchId = pBranchId != NULL ? hb_itemGetCPtr(pBranchId) : NULL;

      if (! sdb_beginTransaction(conn, formatId, globalId, branchId)) {
         sdb_api_genErrorFromSDBError(NULL, NULL, EF_NONE, sdb_thread_getLastError(), apiFunName, NULL);
      }
   }
}

static void sdb_isTable(SDB_CONNECTIONP conn, int firstArgPos, const char *apiFunName) {
   PHB_ITEM pSchema = hb_param(firstArgPos, HB_IT_STRING);
   PHB_ITEM pTable = hb_param(firstArgPos + 1, HB_IT_STRING);
   CFL_STRP tableName;
   CFL_STRP schemaName;

   ENTER_FUN_NAME("sdb_isTable");
   sdb_thread_cleanError();
   if (pTable == NULL) {
      hb_retl(CFL_FALSE);
      RETURN;
   }
   tableName = CFL_STR_ITEM_TRIM_UPPER(pTable);
   if (sdb_util_itemIsEmpty(pSchema)) {
      schemaName = cfl_str_newStr(conn->schema->name);
   } else {
      schemaName = CFL_STR_ITEM_TRIM_UPPER(pSchema);
   }
   if (sdb_api_existsTable(conn, cfl_str_getPtr(schemaName), cfl_str_getPtr(tableName))) {
      hb_retl(HB_TRUE);
   } else if (sdb_thread_hasError()) {
      sdb_api_genErrorFromSDBError(NULL, NULL, EF_NONE, sdb_thread_getLastError(), apiFunName, NULL);
      hb_retl(HB_FALSE);
   } else {
      hb_retl(HB_FALSE);
   }

   cfl_str_free(schemaName);
   cfl_str_free(tableName);
   RETURN;
}

static void clearAreaContext(SDB_AREAP pSDBArea) {
   sdb_api_unlockAreaTable(pSDBArea);
   sdb_area_clearContext(pSDBArea);
   sdb_area_resetLockId(pSDBArea);
   sdb_area_closeStatements(pSDBArea);
}

static int logLevelValue(PHB_ITEM pLevel) {
   if (HB_IS_STRING(pLevel)) {
      const char *szLevel = hb_itemGetCPtr(pLevel);
      if (hb_strnicmp(szLevel, "OFF", 3) == 0) {
         return SDB_LOG_LEVEL_OFF;
      } else if (hb_strnicmp(szLevel, "ERROR", 5) == 0) {
         return SDB_LOG_LEVEL_ERROR;
      } else if (hb_strnicmp(szLevel, "WARN", 4) == 0) {
         return SDB_LOG_LEVEL_WARN;
      } else if (hb_strnicmp(szLevel, "INFO", 4) == 0) {
         return SDB_LOG_LEVEL_INFO;
      } else if (hb_strnicmp(szLevel, "DEBUG", 5) == 0) {
         return SDB_LOG_LEVEL_DEBUG;
      } else if (hb_strnicmp(szLevel, "TRACE", 5) == 0) {
         return SDB_LOG_LEVEL_TRACE;
      } else {
         return SDB_LOG_LEVEL_ERROR;
      }
   } else if (HB_IS_NUMERIC(pLevel)) {
      int iLevel =  hb_itemGetNI(pLevel);
      return iLevel >= SDB_LOG_LEVEL_OFF && iLevel <= SDB_LOG_LEVEL_TRACE ? iLevel : SDB_LOG_LEVEL_ERROR;
   }

   return SDB_LOG_LEVEL_ERROR;
}

static CFL_UINT32 stmtFindColumn(SDB_STATEMENTP pStatement, const char *columnName) {
   CFL_UINT32 col;
   CFL_UINT32 colCount = sdb_stmt_getNumCols(pStatement);
   SDB_QUERY_COL_INFO info;

   sdb_queryInfo_init(&info);
   for (col = 1; col <= colCount; col++) {
      if (sdb_stmt_getQueryInfo(pStatement, col, &info) &&
          hb_stricmp((const char *)cfl_str_getPtr(info.name), columnName) == 0) {
         sdb_queryInfo_free(&info);
         return col;
      }
   }

   sdb_queryInfo_free(&info);
   return 0;
}

static PHB_ITEM createObject(PHB_FUNC classFunction, const char *className, const char *newMethod) {
   PHB_DYNS pDynSym;

   HB_SYMBOL_UNUSED(classFunction);

   pDynSym = hb_dynsymGet(className);
   if (pDynSym != NULL) {
      PHB_ITEM pObj = hb_itemNew(NULL);
      #ifdef __XHB__
         hb_vmPushSymbol(pDynSym->pSymbol);
      #else
         hb_vmPushDynSym(pDynSym);
      #endif
      hb_vmPushNil();
      hb_vmDo(0);
      hb_objSendMsg(hb_stackReturnItem(), newMethod, 0);
      hb_itemMove(pObj, hb_stackReturnItem());
      return pObj;
   }
   return NULL;
}

static PHB_ITEM createColumnInfoObject(CFL_UINT32 col, SDB_QUERY_COL_INFOP info) {
   PHB_ITEM pObj = createObject(HB_FUNCNAME(SDB_COLUMNINFO), "SDB_COLUMNINFO", "NEW" );
   if (pObj != NULL) {
      PHB_ITEM pValue = hb_itemNew( NULL );
      hb_itemPutNI(pValue, (int) col);
      hb_objSendMsg(pObj, "_POSITION"   , 1, pValue);
      hb_itemPutCL(pValue, (const char *)cfl_str_getPtr(info->name), cfl_str_length(info->name));
      hb_objSendMsg(pObj, "_NAME"       , 1, pValue);
      hb_itemPutNI(pValue, info->clpType);
      hb_objSendMsg(pObj, "_CLPTYPE"    , 1, pValue);
      hb_itemPutCL(pValue, (const char *)cfl_str_getPtr(info->dbType), cfl_str_length(info->dbType));
      hb_objSendMsg(pObj, "_DBTYPE"     , 1, pValue);
      hb_itemPutNI(pValue, info->size);
      hb_objSendMsg(pObj, "_SIZE"       , 1, pValue);
      hb_itemPutNI(pValue, info->sizeInBytes);
      hb_objSendMsg(pObj, "_SIZEINBYTES", 1, pValue);
      hb_itemPutNI(pValue, info->precision);
      hb_objSendMsg(pObj, "_PRECISION"  , 1, pValue);
      hb_itemPutNI(pValue, info->scale);
      hb_objSendMsg(pObj, "_SCALE"      , 1, pValue);
      hb_itemPutL(pValue, info->isNullable);
      hb_objSendMsg(pObj, "_NULLABLE"      , 1, pValue);
      hb_itemRelease(pValue);
      return pObj;
   }
   return NULL;
}

/**
 * Executes a call to a remote database procedure.
 *   Ex.: sdb_execProcedure( "PRC_TEST", cPar1, cPar2, @cPar3 )
 * @param procName Name of the procedure to be executed
 * @param pn List of arguments to be passed to the procedure. Accept by reference arguments.
 */
HB_FUNC(SDB_EXECPROCEDURE) {
   SDB_CONNECTIONP conn = sdb_getConnection(NULL, CFL_TRUE);
   if (conn != NULL) {
      sdb_executeProcedure(conn, 1, "SDB_EXECPROCEDURE");
   }
}

/**
 * Executes a call to a remote database procedure.
 *   Ex.: sdb_conn_execProcedure( oCon, "PRC_TEST", cPar1, cPar2, @cPar3 )
 * @param oCon Connection to database
 * @param procName Name of the procedure to be executed
 * @param pn List of arguments to be passed to the procedure. Accept by reference arguments.
 */
HB_FUNC(SDB_CONN_EXECPROCEDURE) {
   SDB_CONNECTIONP conn = sdb_paramConnection(1, CFL_TRUE);
   if (conn != NULL) {
      sdb_executeProcedure(conn, 2, "SDB_CONN_EXECPROCEDURE");
   }
}

/**
 * Executes a call to a remote database procedure in which the first argument is a cursor. However, this first argument does not
 * be passed in list of arguments. After this call, a call to DBUseArea function must be done to open cursor as a workarea.
 *   Ex.: sdb_SqlParams('par3')
 *        sdb_PrepareProcCursor( "PRC_TESTE", "'teste', 1, ?" )
 *        DBUseArea( .T., "SDBRDD", cAlias )
 * @param procName Name of the procedure to be executed. The first argument of procedure must be an out cursor.
 * @param params A string informing procedure params. Don't pass the cursor argument.
 * @param lC1Logical Defines if character columns with size 1 must be returned as logical
 * @param nScale Set scale of numerical columns
 */
HB_FUNC(SDB_PREPAREPROCCURSOR) {
   SDB_THREAD_DATAP thData;
   PHB_ITEM pProcName;
   PHB_ITEM pParams;
   SDB_PARAMP paramCursor;
   CFL_STRP strFunc;
   SDB_CONNECTIONP conn;

   ENTER_FUN_NAME("SDB_PREPRAREPROCCURSOR");
   sdb_thread_cleanError();
   conn = sdb_getConnection(NULL, CFL_TRUE);
   if (conn == NULL) {
      RETURN;
   }
   pProcName = hb_param(1, HB_IT_STRING);
   if (pProcName == NULL || hb_pcount() < 2) {
      sdb_api_genError(NULL, EG_ARG, SDB_ERROR_INVALID_ARGUMENT, NULL, EF_NONE,
                       "The first argument must be the function name", "SDB_PREPAREPROCCURSOR", NULL);
      RETURN;
   }
   pParams = hb_param(2, HB_IT_STRING);
   thData = sdb_thread_getData();
   strFunc = cfl_str_new((CFL_UINT32) (hb_itemGetCLen(pProcName) + hb_itemGetCLen(pParams) + 4));
   cfl_str_appendLen(strFunc, hb_itemGetCPtr(pProcName), (int) hb_itemGetCLen(pProcName));
   if (pParams) {
      cfl_str_appendChar(strFunc, '(');
      CFL_STR_APPEND_CONST(strFunc, ":cur,");
      cfl_str_appendLen(strFunc, hb_itemGetCPtr(pParams), (int) hb_itemGetCLen(pParams));
      cfl_str_appendChar(strFunc, ')');
   } else {
      CFL_STR_APPEND_CONST(strFunc, "(:cur)");
   }
   paramCursor = sdb_param_listAddFirst(sdb_thread_getCreateParams(thData), NULL, 0, SDB_CLP_CURSOR, 0, CFL_TRUE, CFL_FALSE, CFL_FALSE);
   if (sdb_connection_executeProcedure(conn, cfl_str_getPtr(strFunc), sdb_thread_getParams(thData), CFL_FALSE)) {
         SDB_STATEMENTP pStmt = sdb_stmt_itemDetach(sdb_param_getItem(paramCursor));
         if (pStmt != NULL) {
            /* Previous prepared query was not used? */
            if (thData->pStatement != NULL && thData->pStatement->isReleaseOnClose) {
               sdb_stmt_free(thData->pStatement);
               thData->pStatement = NULL;
               if (sdb_thread_hasError()) {
                  sdb_api_genErrorFromSDBError(NULL, NULL, EF_NONE, sdb_thread_getLastError(), "SDB_PREPAREPROCCURSOR", NULL);
               }
            }
            if (hb_pcount() > 2) {
               pStmt->isChar1AsLogical = (CFL_BOOL) hb_parl(3);
            }
            if (hb_pcount() > 3 && hb_parni(4) >= 0) {
               pStmt->scale = (CFL_UINT8) hb_parni(4);
            }
            thData->pStatement = pStmt;
         } else {
            sdb_api_genError(NULL, EG_DATATYPE, SDB_ERROR_INVALID_RESULT, NULL, EF_NONE,
                              "The function does not return an cursor", "SDB_PREPAREPROCCURSOR", NULL);
         }
   } else if (sdb_thread_hasError()) {
      sdb_api_genErrorFromSDBError(NULL, NULL, EF_NONE, sdb_thread_getLastError(), "SDB_PREPAREPROCCURSOR", NULL);
   }
   cfl_str_free(strFunc);
   sdb_thread_freeParams(thData);
   RETURN;
}

/**
 * Executes a call to a remote database function.
 *   Ex.: sdb_execFunction( "N", "FNC_TEST", cPar1, cPar2, @cPar3 )
 *
 * @param funcName Name of the function to be executed.
 * @param pn List of arguments to be passed to the procedure. Accept by reference arguments. Don't pass the cursor argument.
 */
HB_FUNC(SDB_EXECFUNCTION) {
   SDB_CONNECTIONP conn = sdb_getConnection(NULL, CFL_TRUE);
   if (conn != NULL) {
      sdb_executeFunction(conn, 1, "SDB_EXECFUNCTION");
   }
}

/**
 * Executes a call to a remote database function.
 *   Ex.: sdb_conn_execFunction( oCon, "N", "FNC_TEST", cPar1, cPar2, @cPar3 )
 *
 * @param oCon Connection to database
 * @param funcName Name of the function to be executed.
 * @param pn List of arguments to be passed to the procedure. Accept by reference arguments. Don't pass the cursor argument.
 */
HB_FUNC(SDB_CONN_EXECFUNCTION) {
   SDB_CONNECTIONP conn = sdb_paramConnection(1, CFL_TRUE);
   if (conn != NULL) {
      sdb_executeFunction(conn, 2, "SDB_CONN_EXECFUNCTION");
   }
}

/**
 * Executes a call to a remote database function that returns a cursor. After this call, a call to DBUseArea function must
 * be done to open cursor as a workarea.
 *   Ex.: sdb_SqlParams('par3')
 *        sdb_PrepareFuncCursor( "FNC_TESTE", "'teste', 1, ?" )
 *        DBUseArea( .T., "SDBRDD", cAlias )
 * @param funcName Name of the function to be executed. The function must return a cursor.
 * @param params A string informing procedure params. Don't pass the cursor argument.
 * @param lC1Logical Defines if character columns with size 1 must be returned as logical
 * @param nScale Set scale of numerical columns
 */
HB_FUNC(SDB_PREPAREFUNCCURSOR) {
   SDB_THREAD_DATAP thData;
   PHB_ITEM pFuncName;
   PHB_ITEM pParams;
   PHB_ITEM pCursor;
   CFL_STRP strFunc;
   SDB_CONNECTIONP conn;

   ENTER_FUN_NAME("SDB_PREPAREFUNCCURSOR");
   sdb_thread_cleanError();
   conn = sdb_getConnection(NULL, CFL_TRUE);
   if (conn == NULL) {
      RETURN;
   }
   pFuncName = hb_param(1, HB_IT_STRING);
   if (pFuncName && hb_pcount() > 1) {
      thData = sdb_thread_getData();
      pParams = hb_param(2, HB_IT_STRING);
      strFunc = cfl_str_new((CFL_UINT32) (hb_itemGetCLen(pFuncName) + hb_itemGetCLen(pParams) + 4));
      cfl_str_appendLen(strFunc, hb_itemGetCPtr(pFuncName), (int) hb_itemGetCLen(pFuncName));
      if (pParams) {
         cfl_str_appendChar(strFunc, '(');
         cfl_str_appendLen(strFunc, hb_itemGetCPtr(pParams), (int) hb_itemGetCLen(pParams));
         cfl_str_appendChar(strFunc, ')');
      }
      pCursor = sdb_connection_executeFunction(conn, cfl_str_getPtr(strFunc), SDB_CLP_CURSOR, sdb_thread_getCreateParams(thData), CFL_FALSE);
      sdb_thread_freeParams(thData);
      cfl_str_free(strFunc);
      if (pCursor) {
         SDB_STATEMENTP pStmt = sdb_stmt_itemDetach(pCursor);
         hb_itemRelease(pCursor);
         if (pStmt != NULL) {
            /* Previous prepared query was not used? */
            if (thData->pStatement != NULL && thData->pStatement->isReleaseOnClose) {
               sdb_stmt_free(thData->pStatement);
               thData->pStatement = NULL;
               if (sdb_thread_hasError()) {
                  sdb_api_genErrorFromSDBError(NULL, NULL, EF_NONE, sdb_thread_getLastError(), "SDB_PREPAREFUNCCURSOR", NULL);
               }
            }
            if (hb_pcount() > 2) {
               pStmt->isChar1AsLogical = (CFL_BOOL) hb_parl(3);
            }
            if (hb_pcount() > 3 && hb_parni(4) >= 0) {
               pStmt->scale = (CFL_UINT8) hb_parni(4);
            }
            thData->pStatement = pStmt;
         } else {
           sdb_api_genError(NULL, EG_DATATYPE, SDB_ERROR_INVALID_RESULT, NULL, EF_NONE,
                            "The function does not return an cursor", "SDB_PREPAREFUNCCURSOR", NULL);
         }
      } else {
         sdb_api_genErrorFromSDBError(NULL, NULL, EF_NONE, sdb_thread_getLastError(), "SDB_PREPAREFUNCCURSOR", NULL);
      }
   } else {
      sdb_api_genError(NULL, EG_ARG, SDB_ERROR_INVALID_ARGUMENT, NULL, EF_NONE,
                       "The first argument must be the function name", "SDB_PREPAREFUNCCURSOR", NULL);
   }
   RETURN;
}

/**
 * Executes a SQL or a prepared statement. If the statement is a query, the first field of the first row will be returned.
 * Ex.: nVal := SDB_ExecSql( "SELECT 1 FROM dual" )
 *      SDB_ExecSql( "Begin :x := Fnc_Test(:y); End;", @nX, cY )
 *   Another way:
 *      SDB_SqlParams( @nX, cY )
 *      SDB_ExecSql( "Begin :x := Fnc_Test(:y); End;" )
 *   And Another:
 *      SDB_ExecSql( "Begin Prc_Test(:y + :x); End;", { "x" => nX, "y" => cY } )
 *   @param cSql SQL command to be executed
 *   @return Amount of records affected by command or the content of the first column of the first row of a query
 */
HB_FUNC(SDB_EXECSQL) {
   SDB_CONNECTIONP conn = sdb_getConnection(NULL, CFL_TRUE);
   if (conn != NULL) {
      sdb_execSql(conn, 1, "SDB_EXECSQL");
   }
}

/**
 * Executes a SQL or a prepared statement. If the statement is a query, the first field of the first row will be returned.
 * Ex.: nVal := sdb_conn_ExecSql( oCon, "SELECT 1 FROM dual" )
 *   Another way:
 *      SDB_SqlParams( @nX, cY )
 *      sdb_conn_execSql( oCon, "Begin :x := Fnc_Test(:y); End;", @nX, cY )
 *   And Another:
 *      sdb_conn_execSql( oCon, "Begin Prc_Test(:y + :x); End;", { "x" => nX, "y" => cY } )
 *   @param oCon Connection to database
 *   @param cSql SQL command to be executed
 *   @return Amount of records affected by command or the content of the first column of the first row of a query
 */
HB_FUNC(SDB_CONN_EXECSQL) {
   SDB_CONNECTIONP conn = sdb_paramConnection(1, CFL_TRUE);
   if (conn != NULL) {
      sdb_execSql(conn, 2, "SDB_CONN_EXECSQL");
   }
}

/**
 * Executes a SQL or a prepared statement many times, according to the length of array passed as arguments.
 * All arrays used as argument must have the same length and the elements must be of the same datatype.
 * Ex.: SDB_ExecSqlMany( "INSERT INTO TAB(col1,col2) VALUES(:1,:2)", {"VAL1", "VAL2"}, {1, 2} )
 *   Another way:
 *      SDB_SqlParams( {"VAL1", "VAL2"}, {1, 2} )
 *      SDB_ExecSqlMany( "INSERT INTO TAB(col1,col2) VALUES(:1,:2)" )
 *   And Another:
 *      SDB_ExecSqlMany( "INSERT INTO TAB(col1,col2) VALUES(:x,:y)", { "x" => {"VAL1", "VAL2"}, "y" => {1, 2} } )
 *   @param cSql SQL command to be executed
 *   @return Amount of records affected by command or the content of the first column of the first row of a query
 */
HB_FUNC(SDB_EXECSQLMANY) {
   SDB_CONNECTIONP conn = sdb_getConnection(NULL, CFL_TRUE);
   if (conn != NULL) {
      sdb_execSqlMany(conn, 1, "SDB_EXECSQLMANY");
   }
}

/**
 * Executes a SQL or a prepared statement many times, according to the length of array passed as arguments.
 * All arrays used as argument must have the same length and the elements must be of the same datatype.
 * Ex.: SDB_Conn_ExecSqlMany( oCon, "INSERT INTO TAB(col1,col2) VALUES(:1,:2)", {"VAL1", "VAL2"}, {1, 2} )
 *   Another way:
 *      SDB_SqlParams( {"VAL1", "VAL2"}, {1, 2} )
 *      SDB_Conn_ExecSqlMany( oCon, "INSERT INTO TAB(col1,col2) VALUES(:1,:2)" )
 *   And Another:
 *      SDB_Conn_ExecSqlMany( oCon, "INSERT INTO TAB(col1,col2) VALUES(:x,:y)", { "x" => {"VAL1", "VAL2"}, "y" => {1, 2} } )
 *   @param cSql SQL command to be executed
 *   @return Amount of records affected by command or the content of the first column of the first row of a query
 */
HB_FUNC(SDB_CONN_EXECSQLMANY) {
   SDB_CONNECTIONP conn = sdb_paramConnection(1, CFL_TRUE);
   if (conn != NULL) {
      sdb_execSqlMany(conn, 2, "SDB_CONN_EXECSQLMANY");
   }
}

/**
 * Prepare a query to be opened as a workarea. In the sequence a call to the DBUseArea function must be done to open the workarea.
 * Ex.: SDB_PrepareQueryOpen( "SELECT col1, col2, col3 FROM table where col4 = :1", cCol4Value )
 *      DBUseArea( .T., "SDBRDD", cAlias )
 *   Another way
 *      SDB_SqlParams( cCol4Value )
 *      SDB_PrepareQueryOpen( "SELECT col1, col2, col3 FROM table where col4 = :1" )
 *      DBUseArea( .T., "SDBRDD", cAlias )
 *   @param cSql SQL command to be executed
 */
HB_FUNC(SDB_PREPAREQUERYOPEN) {
   SDB_CONNECTIONP conn = sdb_getConnection(NULL, CFL_TRUE);
   if (conn != NULL) {
      sdb_prepareQueryOpen(conn, 1, "SDB_PREPAREQUERYOPEN");
   }
}

/**
 * Prepare a query to be opened as a workarea. In the sequence a call to the DBUseArea function must be done to open the workarea.
 * Ex.: SDB_Conn_PrepareQueryOpen( oCon, "SELECT col1, col2, col3 FROM table where col4 = :1", cCol4Value )
 *      DBUseArea( .T., "SDBRDD", cAlias )
 *   Another way
 *      SDB_SqlParams( cCol4Value )
 *      SDB_Conn_PrepareQueryOpen( oCon, "SELECT col1, col2, col3 FROM table where col4 = :1" )
 *      DBUseArea( .T., "SDBRDD", cAlias )
 *   @param oCon Connection to database.
 *   @param cSql SQL command to be executed.
 */
HB_FUNC(SDB_CONN_PREPAREQUERYOPEN) {
   SDB_CONNECTIONP conn = sdb_paramConnection(1, CFL_TRUE);
   if (conn != NULL) {
      sdb_prepareQueryOpen(conn, 2, "SDB_CONN_PREPAREQUERYOPEN");
   }
}

/**
 * Prepare a statement to be executed later.
 * Ex.: oStmt := SDB_StmtPrepare( "INSERT INTO table(col1, col2, col3) VALUES(:1, :2, :3)" )
 *      SDB_ExecSql( oStmt, cCol1Val, nCol2Val, dColVal3 )
 *      SDB_StmtFree( oStmt )
 *   Another way
 *      oStmt := SDB_StmtPrepare( "INSERT INTO table(col1, col2, col3) VALUES(:1, :2, :3)" )
 *      SDB_SqlParams( cCol1Val, nCol2Val, dColVal3 )
 *      SDB_ExecSql( oStmt )
 *      SDB_StmtFree( oStmt )
 *   @param cSql SQL command to be executed.
 *
 *   @return Prepared statement to later execution.
 */
HB_FUNC(SDB_STMTPREPARE) {
   SDB_CONNECTIONP conn = sdb_getConnection(NULL, CFL_TRUE);
   if (conn != NULL) {
      sdb_prepareStatement(conn, 1, "SDB_STMTPREPARE");
   }
}

/**
 * Prepare a statement to be executed later.
 * Ex.: oStmt := SDB_Conn_StmtPrepare( oCon, "INSERT INTO table(col1, col2, col3) VALUES(:1, :2, :3)" )
 *      SDB_ExecSql( oStmt, cCol1Val, nCol2Val, dColVal3 )
 *      SDB_StmtFree( oStmt )
 *   Another way
 *      oStmt := SDB_Conn_StmtPrepare( oCon, "INSERT INTO table(col1, col2, col3) VALUES(:1, :2, :3)" )
 *      SDB_SqlParams( cCol1Val, nCol2Val, dColVal3 )
 *      SDB_ExecSql( oStmt )
 *      SDB_StmtFree( oStmt )
 *   @param oCon Connection to database.
 *   @param cSql SQL command to be executed.
 *
 *   @return Prepared statement to later execution.
 */
HB_FUNC(SDB_CONN_STMTPREPARE) {
   SDB_CONNECTIONP conn = sdb_paramConnection(1, CFL_TRUE);

   if (conn != NULL) {
      sdb_prepareStatement(conn, 2, "SDB_CONN_STMTPREPARE");
   }
}

/**
 * Executes a statement previously prepared. The difference of SDB_ExecSql is that if the command is a query no value is fetched.
 * Ex.:
 *    pStmt := SDB_PrepareStmt( "UPDATE tab SET col2 = :1 WHERE col1 = :2" )
 *    nRows := SDB_StmtExecute( pStmt, cNewVale, cCondition )
 *    ? nRows, "updated"
 *
 *    pStmt := SDB_PrepareStmt( "SELECT col1 FROM tab WHERE col1 = :1" )
 *    If SDB_StmtExecute( pStmt, cCondition )
 *       While SDB_StmtFetch( pStmt )
 *          ? "Content:", SDB_StmtGetValue( 1 )
 *       EndDo
 *    EndIF
 *    ? nRows, "updated"
 *
 * @param pStatement
 *
 * @return number of affected records if statement is a DML or a boolean value indicating success or failure
 */
HB_FUNC(SDB_STMTEXECUTE) {
   ENTER_FUN_NAME("SDB_STMTEXECUTE");
   sdb_execStmt(sdb_stmt_param(1), 2, "SDB_STMTEXECUTE");
   RETURN;
}

/**
 * Fetch next record from statement
 *
 * @param pStatement statement to fetch next record
 *
 * @return .T. if fetch or .F. if no record to fetch
 *
 */
HB_FUNC(SDB_STMTFETCH) {
   SDB_STATEMENTP pStatement;

   ENTER_FUN_NAME("SDB_STMTFETCH");

   sdb_thread_cleanError();
   pStatement = sdb_stmt_param(1);
   if (pStatement == NULL) {
      sdb_api_genError(NULL, EG_ARG, SDB_ERROR_INVALID_ARGUMENT, NULL, EF_NONE,
                       "Invalid arguments: argument 1 must be a statement", "SDB_STMTFETCH", NULL);
      RETURN;
   }

   hb_retl(sdb_stmt_fetchNext(pStatement, CFL_FALSE));

   if (sdb_thread_hasError()) {
      sdb_api_genErrorFromSDBError(NULL, NULL, EF_NONE, sdb_thread_getLastError(), "SDB_STMTFETCH", NULL);
   }
   RETURN;
}

/**
 * Return an array with statement columns info when the statement is a query
 *
 * @param pStatement statement to get columns info
 *
 * @return array with columns info. Empty array if not a query statement
 *
 */
HB_FUNC( SDB_STMTGETCOLUMNS ) {
   SDB_QUERY_COL_INFO info;
   SDB_STATEMENTP pStatement;
   CFL_UINT32 colCount;
   CFL_UINT32 col;
   PHB_ITEM pArray;

   ENTER_FUN_NAME("SDB_STMTGETCOLUMNS");
   sdb_thread_cleanError();

   pStatement = sdb_stmt_param(1);
   if (pStatement == NULL) {
      sdb_api_genError(NULL, EG_ARG, SDB_ERROR_INVALID_ARGUMENT, NULL, EF_NONE,
                       "Invalid arguments: argument 1 must be a statement", "SDB_STMTGETCOLUMNS", NULL);
      RETURN;
   }

   sdb_queryInfo_init(&info);
   colCount = sdb_stmt_getNumCols(pStatement);
   pArray = hb_itemNew(NULL);
   hb_arrayNew(pArray, (HB_SIZE)colCount);
   for (col = 1; col <= colCount; col++) {
      if (sdb_stmt_getQueryInfo(pStatement, col, &info)) {
         PHB_ITEM pColInfo = createColumnInfoObject(col, &info);
         if (pColInfo != NULL) {
            hb_arraySetForward(pArray, (HB_SIZE)col, pColInfo);
            hb_itemRelease(pColInfo);
         }
      }
   }
   hb_itemReturnRelease(pArray);
   sdb_queryInfo_free(&info);
   RETURN;
}

/**
 * Return an object with information of the column
 *
 * @param pStatement statement to get column info
 * @param xCol Column name or column position
 *
 * @return object with column info or NIL
 *
 */
HB_FUNC( SDB_STMTGETCOLUMN ) {
   SDB_QUERY_COL_INFO info;
   SDB_STATEMENTP pStatement;
   int col;

   ENTER_FUN_NAME("SDB_STMTGETCOLUMN");

   sdb_thread_cleanError();
   pStatement = sdb_stmt_param(1);

   if (pStatement == NULL) {
      sdb_api_genError(NULL, EG_ARG, SDB_ERROR_INVALID_ARGUMENT, NULL, EF_NONE,
                       "Invalid arguments: argument 1 must be a statement", "SDB_STMTGETCOLUMN", NULL);
      RETURN;
   }

   col = hb_parni(2);
   if (col < 1 || col > (int) sdb_stmt_getNumCols(pStatement)) {
      sdb_api_genError(NULL, EG_ARG, SDB_ERROR_INVALID_ARGUMENT, NULL, EF_NONE,
                       "Invalid arguments: argument 2 must be the column position", "SDB_STMTGETCOLUMN", NULL);
      RETURN;
   }
   sdb_queryInfo_init(&info);
   if (sdb_stmt_getQueryInfo(pStatement, col, &info)) {
      PHB_ITEM pColInfo = createColumnInfoObject(col, &info);
      if (pColInfo != NULL) {
         hb_itemReturnRelease(pColInfo);
      } else {
         hb_ret();
      }
   } else {
      hb_ret();
   }
   sdb_queryInfo_free(&info);
   RETURN;
}

/**
 * Return the count of columns if a query statement
 *
 * @param pStatement statement to get columns info
 *
 * @return columns count of query statement
 *
 */
HB_FUNC(SDB_STMTNUMCOLS) {
   SDB_STATEMENTP pStatement;

   ENTER_FUN_NAME("SDB_STMTNUMCOLS");
   sdb_thread_cleanError();

   pStatement = sdb_stmt_param(1);
   if (pStatement == NULL) {
      sdb_api_genError(NULL, EG_ARG, SDB_ERROR_INVALID_ARGUMENT, NULL, EF_NONE,
                       "Invalid arguments: argument 1 must be a statement", "SDB_STMTNUMCOLS", NULL);
      RETURN;
   }
   hb_retni((int) sdb_stmt_getNumCols(pStatement));
}

/**
 * Return the type of statement.
 * The possible return values are:
 *    #define SDB_STMT_UNKNOWN 0
 *    #define SDB_STMT_QUERY   1
 *    #define SDB_STMT_DML     2
 *    #define SDB_STMT_DDL     4
 *    #define SDB_STMT_PLSQL   8
 * @param pStatement statement to get type
 *
 * @return the type of statement
 *
 */
HB_FUNC(SDB_STMTGETTYPE) {
   SDB_STATEMENTP pStatement;

   ENTER_FUN_NAME("SDB_STMTGETTYPE");
   sdb_thread_cleanError();

   pStatement = sdb_stmt_param(1);
   if (pStatement == NULL) {
      sdb_api_genError(NULL, EG_ARG, SDB_ERROR_INVALID_ARGUMENT, NULL, EF_NONE,
                       "Invalid arguments: argument 1 must be a statement", "SDB_STMTGETTYPE", NULL);
      RETURN;
   }
   hb_retni(sdb_stmt_getType(pStatement));
}


/**
 * Return the number of rows fetched in statement.
 * @param pStatement statement to get rows fetched
 *
 * @return the count of fetched rows in statement
 *
 */
HB_FUNC(SDB_STMTROWCOUNT) {
   SDB_STATEMENTP pStatement;

   ENTER_FUN_NAME("SDB_STMTGETTYPE");
   sdb_thread_cleanError();

   pStatement = sdb_stmt_param(1);
   if (pStatement == NULL) {
      sdb_api_genError(NULL, EG_ARG, SDB_ERROR_INVALID_ARGUMENT, NULL, EF_NONE,
                       "Invalid arguments: argument 1 must be a statement", "SDB_STMTROWCOUNT", NULL);
      RETURN;
   }
   hb_retnint(sdb_stmt_fetchCount(pStatement));
}

/**
 * Return the value of column in current record.
 *
 * @param pStatement the statement to get record value
 * @param xCol the column position or column name
 *
 * @return the value of column in current record
 *
 */
HB_FUNC(SDB_STMTGETVALUE) {
   SDB_STATEMENTP pStatement;
   CFL_UINT32 col;

   ENTER_FUN_NAME("SDB_STMTGETVALUE");
   sdb_thread_cleanError();
   pStatement = sdb_stmt_param(1);
   if (pStatement == NULL) {
      sdb_api_genError(NULL, EG_ARG, SDB_ERROR_INVALID_ARGUMENT, NULL, EF_NONE,
                       "Invalid arguments: argument 1 must be a statement", "SDB_STMTGETVALUE", NULL);
      RETURN;
   }

   if (HB_ISNUM(2)) {
      col = (CFL_UINT32) hb_parni(2);
   } else if (HB_ISCHAR(2)) {
      col = stmtFindColumn(pStatement, hb_parc(2));
      if (col == 0) {
         sdb_api_genError(NULL, EG_ARG, SDB_ERROR_INVALID_FIELD, NULL, EF_NONE,
                        "Invalid column name in argument 2", "SDB_STMTGETVALUE", NULL);
         RETURN;
      }
   } else {
      sdb_api_genError(NULL, EG_ARG, SDB_ERROR_INVALID_ARGUMENT, NULL, EF_NONE,
                       "Invalid arguments: argument 2 must be the column position or name", "SDB_STMTGETVALUE", NULL);
      RETURN;
   }

   hb_itemReturnRelease(sdb_stmt_getQueryValue(pStatement, col, NULL));

   if (sdb_thread_hasError()) {
      sdb_api_genErrorFromSDBError(NULL, NULL, EF_NONE, sdb_thread_getLastError(), "SDB_STMTGETVALUE", NULL);
   }
   RETURN;
}

/**
 * Add a value to the list of arguments that will be used in statement.
 * Ex.: SDB_StmtSetParam( pStmt, 1, cVal )
 *      SDB_StmtSetParam( pStmt, ":x", cVal )
 * @param Statement handle
 * @param xPosName name or position of the parameter
 * @param xValue to be set in statement
 * @param nLength. Length of the parameter. Useful when need reusing param in future executions of statement
 *        with different length of current content. Optional.
 * @param xDataType. Useful when the datatype can't be inferred from data or when the data value
 *        can be used with more than one datatype. Optional.
 * @param bTrimValue. Trim right spaces from string params. If not received, uses the default for thread.
 * @param bNullable. Determines whether the value should be replaced by a space when it is a null or an empty
 *        string. If not received, uses the default for thread.
 */
HB_FUNC(SDB_STMTSETPARAM) {
   SDB_THREAD_DATAP thData;
   PHB_ITEM pType;
   CFL_UINT8 type;
   SDB_STATEMENTP pStmt;
   CFL_BOOL bTrim;
   CFL_BOOL bNullable;

   ENTER_FUN_NAME("SDB_STMTSETPARAM");
   sdb_thread_cleanError();
   pStmt = sdb_stmt_itemGet(hb_param(1, HB_IT_POINTER));
   if (pStmt == NULL) {
      sdb_api_genError(NULL, EG_DATATYPE, SDB_ERROR_INVALID_DATATYPE, NULL, EF_NONE,
              "Invalid arguments: argument 1 must be a statement", "SDB_STMTSETPARAM", NULL);
      RETURN;
   }
   if (hb_pcount() < 3) {
      sdb_api_genError(NULL, EG_DATATYPE, SDB_ERROR_INVALID_DATATYPE, NULL, EF_NONE,
              "Invalid arguments: the value of param not provided in argument 3", "SDB_STMTSETPARAM", NULL);
      RETURN;
   }
   pType = hb_param(5, HB_IT_ANY);
   if (pType != NULL && HB_IS_NUMERIC(pType)) {
      type = (CFL_UINT8) hb_itemGetNI(pType);
      if (! sdb_util_isValidType(type)) {
         sdb_api_genError(NULL, EG_DATATYPE, SDB_ERROR_INVALID_DATATYPE, NULL, EF_NONE,
                 "Invalid arguments: invalid data type in argument 5", "SDB_STMTSETPARAM", NULL);
      }
   } else if (pType != NULL && HB_IS_STRING(pType)) {
      type = sdb_util_clpToSDBType(hb_itemGetCPtr(pType));
   } else {
      type = SDB_CLP_UNKNOWN;
   }
   thData = sdb_thread_getData();
   bTrim = HB_ISLOG(6) ? hb_parl(6) : sdb_thread_isTrimParams(thData);
   bNullable = HB_ISLOG(7) ? hb_parl(7) : sdb_thread_isNullable(thData);
   if (HB_ISCHAR(2)) {
      funParamToStmt(3, pStmt, (char *) hb_parc(2), 0, type, (CFL_UINT32) hb_parni(4), bTrim, bNullable);
   } else if (HB_ISNUM(2)) {
      funParamToStmt(3, pStmt, NULL, (CFL_UINT32) hb_parnl(2), type, (CFL_UINT32) hb_parni(4), bTrim, bNullable);
   } else {
      sdb_api_genError(NULL, EG_DATATYPE, SDB_ERROR_INVALID_DATATYPE, NULL, EF_NONE,
              "Invalid arguments: argument 2 must be the param name or position ", "SDB_STMTSETPARAM", NULL);
   }
   RETURN;
}

/**
 * Get a value from the list of arguments used in statement.
 * Ex.: SDB_StmtGetParam( pStmt, 1 )
 *      SDB_StmtGetParam( pStmt, ":x" )
 * @param Statement handle
 * @param Name or position of the parameter
 *
 * @return The value of param
 */
HB_FUNC(SDB_STMTGETPARAM) {
   SDB_STATEMENTP pStmt;

   ENTER_FUN_NAME("SDB_STMTGETPARAM");
   sdb_thread_cleanError();
   pStmt = sdb_stmt_itemGet(hb_param(1, HB_IT_POINTER));
   if (pStmt == NULL) {
      sdb_api_genError(NULL, EG_DATATYPE, SDB_ERROR_INVALID_DATATYPE, NULL, EF_NONE,
              "Invalid arguments: argument 1 must be a statement", "SDB_STMTGETPARAM", NULL);
      RETURN;
   }
   if (HB_ISCHAR(2)) {
      hb_itemReturn(sdb_stmt_getItem(pStmt, (char *) hb_parc(2)));
   } else if (HB_ISNUM(2)) {
      hb_itemReturn(sdb_stmt_getItemByPos(pStmt, (CFL_UINT32) hb_parnl(2)));
   } else {
      sdb_api_genError(NULL, EG_DATATYPE, SDB_ERROR_INVALID_DATATYPE, NULL, EF_NONE,
              "Invalid arguments: argument 2 must be the param name or position ", "SDB_STMTGETPARAM", NULL);
   }
   RETURN;
}

/**
 * Releases a prepared statement.
 * Ex.: SDB_StmtFree( oStmt )
 * @param oStmt Statment to be released.
 */
HB_FUNC(SDB_STMTFREE) {
   PHB_ITEM pStmtItem;
   SDB_STATEMENTP pStmt;

   ENTER_FUN_NAME("SDB_STMTFREE");
   sdb_thread_cleanError();
   pStmtItem = hb_param(1, HB_IT_POINTER);
   if (pStmtItem == NULL) {
      sdb_api_genError(NULL, EG_DATATYPE, SDB_ERROR_INVALID_DATATYPE, NULL, EF_NONE, "Invalid arguments", "SDB_STMTFREE", NULL);
      RETURN;
   }
   pStmt = sdb_stmt_itemGet(pStmtItem);
   if (pStmt == NULL) {
      sdb_api_genError(NULL, EG_DATATYPE, SDB_ERROR_INVALID_DATATYPE, NULL, EF_NONE, "Invalid arguments", "SDB_STMTFREE", NULL);
      RETURN;
   }
   sdb_thread_cleanError();
   sdb_stmt_free(pStmt);
   // hb_itemClear(pStmtItem);
   if (sdb_thread_hasError()) {
      sdb_api_genErrorFromSDBError(NULL, NULL, EF_NONE, sdb_thread_getLastError(), "SDB_STMTFREE", NULL);
   }
   RETURN;
}

/**
 * Add a value to the list of arguments that will be used in next SQL command.
 * Ex.: SDB_SqlAddParam( cVal )
 *      SDB_SqlAddParam( cVal, "x" )
 * @param Argument value added to the list of arguments
 * @param Optional name of the parameter
 * @param Optional length of the parameter. Useful when need reusing param in future executions of statement
 *        with different length of current content.
 * @param Optional data type. Useful when the datatype can't be inferred from data or when the data value
 *        can be used with more than one datatype.
 * @param bTrimValue.
 * @param bNullable.
 */
HB_FUNC(SDB_SQLADDPARAM) {

   ENTER_FUN_NAME("SDB_SQLADDPARAM");
   sdb_thread_cleanError();
   if (hb_pcount() > 0) {
      PHB_ITEM pName = hb_param(2, HB_IT_STRING);
      SDB_THREAD_DATAP thData = sdb_thread_getData();
      PHB_ITEM pType = hb_param(4, HB_IT_ANY);
      CFL_UINT8 type;
      CFL_BOOL bTrim;
      CFL_BOOL bNullable;

      if (pType != NULL && HB_IS_NUMERIC(pType)) {
         type = (CFL_UINT8) hb_itemGetNI(pType);
         if (! sdb_util_isValidType(type)) {
            sdb_api_genError(NULL, EG_DATATYPE, SDB_ERROR_INVALID_DATATYPE, NULL, EF_NONE, "Invalid data type", "SDB_SQLADDPARAM", NULL);
         }
      } else if (pType != NULL && HB_IS_STRING(pType)) {
         type = sdb_util_clpToSDBType(hb_itemGetCPtr(pType));
      } else {
         type = SDB_CLP_UNKNOWN;
      }
      bTrim = HB_ISLOG(6) ? hb_parl(5) : sdb_thread_isTrimParams(thData);
      bNullable = HB_ISLOG(7) ? hb_parl(6) : sdb_thread_isNullable(thData);
      if (pName) {
         funParamToClpParamList(1, sdb_thread_getCreateParams(thData), (char *) hb_itemGetCPtr(pName), (CFL_UINT32) hb_itemGetCLen(pName), type, (CFL_UINT64) hb_parnint(3), bTrim, bNullable);
      } else {
         funParamToClpParamList(1, sdb_thread_getCreateParams(thData), NULL, 0, type, (CFL_UINT64) hb_parnint(3), bTrim, bNullable);
      }
   } else {
      sdb_api_genError(NULL, EG_DATATYPE, SDB_ERROR_INVALID_DATATYPE, NULL, EF_NONE, "Invalid arguments", "SDB_SQLADDPARAM", NULL);
   }
   RETURN;
}

/**
 * Set the list of arguments to be passed that will be used in next SQL command. Each argument will be named according your
 * position. For example. arg1 wil be named ":1", arg2 will be named ":2" and so on.
 * Ex.: SDB_SqlParams( cVal1, nVal2, dVal3 )
 * @param args List of arguments. Accept arguments by reference.
 */
HB_FUNC(SDB_SQLPARAMS) {

   SDB_LOG_DEBUG(("SDB_SQLPARAMS byRef=%s ", HB_ISBYREF(1) ? "true" : "false"));
   sdb_thread_cleanError();
   if (hb_pcount() > 0) {
      SDB_THREAD_DATAP thData = sdb_thread_getData();
      sdb_clp_functionParamsToParamsList(1, sdb_thread_getCreateParams(thData), CFL_FALSE, sdb_thread_isTrimParams(thData), sdb_thread_isNullable(thData));
   }
   return;
}

/**
 * Set the list of arguments by position to be passed that will be used in next SQL command.
 * Ex.: SDB_SqlParamsByPos( cVal1, nVal2, dVal3 )
 * @param args List of arguments. Accept arguments by reference.
 */
HB_FUNC(SDB_SQLPARAMSBYPOS) {
   SDB_THREAD_DATAP thData;

   SDB_LOG_DEBUG(("SDB_SQLPARAMS byRef=%s ", HB_ISBYREF(1) ? "true" : "false"));
   sdb_thread_cleanError();
   thData = sdb_thread_getData();
   if (hb_pcount() > 0) {
      sdb_clp_functionParamsToParamsList(1, sdb_thread_getCreateParams(thData), CFL_TRUE, sdb_thread_isTrimParams(thData), sdb_thread_isNullable(thData));
   }
   return;
}

/**
 * Set the list of arguments to be passed that will be used in next SQL command.
 * Ex.: SDB_SqlArrayParams( { cVal1, nVal2, dVal3 } )
 * @param args Array with arguments.
 */
HB_FUNC(SDB_SQLARRAYPARAMS) {
   SDB_THREAD_DATAP thData;
   PHB_ITEM pArray;

   ENTER_FUN_NAME("SDB_SQLARRAYPARAMS");
   sdb_thread_cleanError();
   thData = sdb_thread_getData();
   pArray = hb_param(1, HB_IT_ARRAY);
   if (pArray) {
      arrayToParams(pArray, sdb_thread_getCreateParams(thData), sdb_thread_isTrimParams(thData), sdb_thread_isNullable(thData));
   }
   RETURN;
}

/**
 * Open a database transaction.
 * Ex.: SDB_BeginTransaction()
 * @param nFormatId Format Id.
 * @param cGlobalId Global Id.
 * @param cBranchId Branch Id.
 */
HB_FUNC(SDB_BEGINTRANSACTION) {
   SDB_CONNECTIONP conn;
   ENTER_FUN_NAME("SDB_BEGINTRANSACTION");
   conn = sdb_getConnection(NULL, CFL_TRUE);
   if (conn != NULL) {
      beginTransaction(conn, 1, "SDB_BEGINTRANSACTION");
   }
   RETURN;
}

/**
 * Open a database transaction.
 * Ex.: SDB_Conn_BeginTransaction()
 * @param oCon Optional connection to database. If not provided, uses current active connection.
 */
HB_FUNC(SDB_CONN_BEGINTRANSACTION) {
   SDB_CONNECTIONP conn;
   ENTER_FUN_NAME("SDB_CONN_BEGINTRANSACTION");
   conn = sdb_paramConnection(1, CFL_TRUE);
   if (conn != NULL) {
      beginTransaction(conn, 2, "SDB_CONN_BEGINTRANSACTION");
   }
   RETURN;
}

/**
 * Prepares a distributed transaction to commit
 * Ex.: SDB_PrepareTransaction()
 * @param oCon Optional connection to database. If not provided, uses current active connection.
 * @return .T. if a commit is needed
 */
HB_FUNC(SDB_PREPARETRANSACTION) {
   SDB_CONNECTIONP conn;

   ENTER_FUN_NAME("SDB_PREPARETRANSACTION");
   sdb_thread_cleanError();
   conn = sdb_getConnection(hb_param(1, HB_IT_ANY), CFL_TRUE);
   if (conn != NULL) {
      hb_retl(sdb_connection_prepareTransaction(conn));
      if (sdb_thread_hasError()) {
         sdb_api_genErrorFromSDBError(NULL, NULL, EF_NONE, sdb_thread_getLastError(), "SDB_PREPARETRANSACTION", NULL);
      }
   }
   RETURN;
}

/**
 * Commit current database transaction.
 * Ex.: SDB_CommitTransaction()
 * @param oCon Optional connection to database. If not provided, uses current active connection.
 */
HB_FUNC(SDB_COMMITTRANSACTION) {
   SDB_CONNECTIONP conn;

   ENTER_FUN_NAME("SDB_COMMITTRANSACTION");
   conn = sdb_getConnection(hb_param(1, HB_IT_ANY), CFL_TRUE);
   if (conn != NULL && ! sdb_commitTransaction(conn)) {
      sdb_api_genErrorFromSDBError(NULL, NULL, EF_NONE, sdb_thread_getLastError(), "SDB_COMMITTRANSACTION", NULL);
   }
   RETURN;
}

/**
 * Rollback current database transaction.
 * Ex.: SDB_RollbackTransaction()
 * @param oCon Optional connection to database. If not provided, uses current active connection.
 */
HB_FUNC(SDB_ROLLBACKTRANSACTION) {
   SDB_CONNECTIONP conn;

   ENTER_FUN_NAME("SDB_ROLLBACKTRANSACTION");
   conn = sdb_getConnection(hb_param(1, HB_IT_ANY), CFL_TRUE);
   if (conn != NULL && !sdb_rollbackTransaction(conn)) {
      sdb_api_genErrorFromSDBError(NULL, NULL, EF_NONE, sdb_thread_getLastError(), "SDB_ROLLBACKTRANSACTION", NULL);
   }
   RETURN;
}

/**
 * Returns true if there is an open connection.
 * Ex.: lOpenTrans := SDB_IsTransaction()
 * @param oCon Optional connection to database. If not provided, uses current active connection.
 * @return true if there is an open connection.
 */
HB_FUNC(SDB_ISTRANSACTION) {
   ENTER_FUN_NAME("SDB_ISTRANSACTION");
   hb_retl(sdb_isTransaction(sdb_getConnection(hb_param(1, HB_IT_ANY), CFL_FALSE)));
   RETURN;
}

/**
 * Returns true if the provided table exists in SDB dictionary.
 * Ex.: lExists := SDB_IsTable( "TAB_OWNER\TABLE1" )
 * @param cTabPathName Path and name of the table to verify if exists
 * @return true if the table exists in SDB dictionary.
 */
HB_FUNC(SDB_ISTABLE) {
   SDB_CONNECTIONP conn = sdb_getConnection(NULL, CFL_TRUE);
   if (conn != NULL) {
      sdb_isTable(conn, 1, "SDB_ISTABLE");
   }
}

/**
 * Returns true if the provided table exists in SDB dictionary.
 * Ex.: lExists := SDB_Conn_IsTable( oCon, "TAB_OWNER\TABLE1" )
 * @param oCon Connection to database
 * @param cTabPathName Path and name of the table to verify if exists
 * @return true if the table exists in SDB dictionary.
 */
HB_FUNC(SDB_CONN_ISTABLE) {
   SDB_CONNECTIONP conn = sdb_paramConnection(1, CFL_TRUE);
   if (conn != NULL) {
      sdb_isTable(conn, 2, "SDB_CONN_ISTABLE");
   }
}

/**
 * Returns the table name of the passed.
 * Ex.: cTabName := SDB_TableName()
 *      cTabName := SDB_TableName( Select( "Alias" ) )
 * @param nWorkarea Optional argument informing the workarea number. If it's not present, assumes the current workarea.
 * @return the table name of the provided workarea.
 */
HB_FUNC(SDB_TABLENAME) {
   PHB_ITEM pNumArea = hb_param(1, HB_IT_NUMERIC);
   int iArea;
   AREAP area;

   if (pNumArea) {
      iArea = hb_itemGetNI(pNumArea);
   } else {
      iArea = hb_rddGetCurrentWorkAreaNumber();
   }
   if (iArea > 0) {
      area = (AREAP) hb_rddGetWorkAreaPointer(iArea);
      if (sdb_area_isSDBArea(area) && ! ((SDB_AREAP)area)->isQuery) {
         CFL_STRP tabName = ((SDB_AREAP)area)->table->dbName;
         hb_retclen(cfl_str_getPtr(tabName), cfl_str_getLength(tabName));
      } else {
         hb_ret();
      }
   } else {
      hb_ret();
   }
}

/**
 * Returns the table's owner for the provided workarea.
 * Ex.: cSchema := SDB_TableOwner()
 *      cSchema := SDB_TableOwner( Select( "Alias" ) )
 * @param nWorkarea Optional argument informing the workarea number. If it's not present, assumes the current workarea.
 * @return the table's owner name of the provided workarea.
 */
HB_FUNC(SDB_TABLEOWNER) {
   PHB_ITEM pNumArea = hb_param(1, HB_IT_NUMERIC);
   int iArea;
   AREAP area;

   if (pNumArea) {
      iArea = hb_itemGetNI(pNumArea);
   } else {
      iArea = hb_rddGetCurrentWorkAreaNumber();
   }
   if (iArea > 0) {
      area = (AREAP) hb_rddGetWorkAreaPointer(iArea);
      if (sdb_area_isSDBArea(area) && ! ((SDB_AREAP)area)->isQuery) {
         CFL_STRP owner = ((SDB_AREAP)area)->table->clpSchema->name;
         hb_retclen(cfl_str_getPtr(owner), cfl_str_getLength(owner));
      } else {
         hb_ret();
      }
   } else {
      hb_ret();
   }
}

/**
 * Lock or unlock a workarea for current context
 *
 * @param lLock .T. to lock the workarea and .F. to unlock
 * @param lExclusive .T. indicates exlusive lock mode
 *
 * @return .T. if the lock/unlock was sucessfull, .F. otherwise
 */
HB_FUNC(SDB_LOCKCONTEXT) {
   AREAP area = (AREAP) hb_rddGetCurrentWorkAreaPointer();
   PHB_ITEM pLock = hb_param(1, HB_IT_LOGICAL);
   PHB_ITEM pExclusive = hb_param(2, HB_IT_LOGICAL);

   SDB_AREAP pSDBArea;
   sdb_thread_cleanError();
   if (! sdb_area_isSDBArea(area)) {
      sdb_api_genError(NULL, HB_EI_RDDINVALID, SDB_ERROR_INVALID_OPERATION, "Not SDBRDD workarea", EF_NONE, NULL, "SDB_LOCKCONTEXT", NULL);
      return;
   }
   pSDBArea = (SDB_AREAP) area;
   if (pSDBArea->isQuery) {
      sdb_api_genError(pSDBArea->connection, EG_UNSUPPORTED, SDB_ERROR_INVALID_OPERATION, "Invalid operation for a query", EF_NONE, NULL, "SDB_LOCKCONTEXT", NULL);
      return;
   }
   if (pSDBArea->table->isContextualized) {
      sdb_api_genError(pSDBArea->connection, EG_UNSUPPORTED, SDB_ERROR_INVALID_OPERATION, "Table with predefined context", EF_NONE, NULL, "SDB_LOCKCONTEXT", NULL);
      return;
   }

   hb_retl(HB_FALSE);
   if (hb_itemGetL(pLock)) {
      if (hb_itemGetL(pExclusive)) {
         if (sdb_api_lockAreaTable(pSDBArea, SDB_LOCK_EXCLUSIVE)) {
            pSDBArea->isShared = CFL_FALSE;
            hb_retl(HB_TRUE);
         }
      } else if (sdb_api_lockAreaTable(pSDBArea, SDB_LOCK_SHARED)) {
         pSDBArea->isShared = CFL_TRUE;
         hb_retl(HB_TRUE);
      }
   /* Release LOCK */
   } else if (sdb_api_unlockAreaTable(pSDBArea)) {
      pSDBArea->isShared = CFL_FALSE;
      hb_retl(HB_TRUE);
   } else if (sdb_thread_hasError()) {
      sdb_api_genErrorFromSDBError(NULL, NULL, EF_NONE, sdb_thread_getLastError(), "SDB_LOCKCONTEXT", NULL);
   }
}

/**
 * Sets one or more columns and their corresponding values as a context for the current workarea
 * Ex.: Area->( SDB_SetContext( "COL1", "COL_VALUE", .T. ) )
 *    OR
 *      Area->( SDB_SetContext( { "COL1", "COL2" }, { "COL1_VAL", CToD( "01/01/2017" ) }, .T. ) )
 * @param xFields Name of a column or array with column names to define a context.
 * @param xValues Value of a column or array with column values to define a context.
 * @param lActive Indicates if the context must be activated
 */
HB_FUNC(SDB_SETCONTEXT) {
   PHB_ITEM pFields = hb_param(1, HB_IT_ANY);
   PHB_ITEM pValues = hb_param(2, HB_IT_ANY);
   PHB_ITEM pActive = hb_param(3, HB_IT_LOGICAL);
   AREAP area = (AREAP) hb_rddGetCurrentWorkAreaPointer();
   SDB_AREAP pSDBArea;
   HB_SIZE len;
   HB_SIZE index;
   SDB_WAFIELDP waField;

   SDB_LOG_DEBUG(("HB_FUN_SDB_SETCONTEXT(%s, %s, %s)",
           ITEM_STR(pFields),
           ITEM_STR(pValues),
           (hb_itemGetL(pActive)? "true" : "false")));
   sdb_thread_cleanError();
   if (! sdb_area_isSDBArea(area)) {
      sdb_api_genError(NULL, HB_EI_RDDINVALID, SDB_ERROR_INVALID_OPERATION, "Not SDBRDD workarea",
                       EF_NONE, NULL, "SDB_SETCONTEXT", NULL);
      return;
   }
   pSDBArea = (SDB_AREAP) area;
   if (pSDBArea->isQuery) {
      sdb_api_genError(pSDBArea->connection, EG_UNSUPPORTED, SDB_ERROR_INVALID_OPERATION, "Invalid operation for a query",
                       EF_NONE, NULL, "SDB_SETCONTEXT", NULL);
      return;
   }
   if (pSDBArea->table->isContextualized) {
      sdb_api_genError(pSDBArea->connection, EG_UNSUPPORTED, SDB_ERROR_INVALID_OPERATION, "Table with predefined context",
                       EF_NONE, NULL, "SDB_SETCONTEXT", NULL);
      return;
   }

   if (hb_pcount() == 0) {
      clearAreaContext(pSDBArea);
   } else if (pFields == NULL) {
      sdb_api_genError(pSDBArea->connection, EG_ARG, SDB_ERROR_INVALID_DATATYPE,
                       "First argument must be an array of fields names or an field name", EF_NONE, NULL, "SDB_SETCONTEXT", NULL);
   } else if (HB_IS_ARRAY(pFields)) {
      if (HB_IS_ARRAY(pValues)) {
         len = hb_arrayLen(pFields);
         if (len == hb_arrayLen(pValues)) {
            clearAreaContext(pSDBArea);
            for (index = 1; index <= len; index++) {
               waField = sdb_area_getField(pSDBArea, hb_itemGetCPtr(hb_arrayGetItemPtr(pFields, index)));
               if (waField != NULL && IS_DATA_FIELD(waField->field)) {
                  waField->isContext = CFL_TRUE;
                  sdb_record_setValue(pSDBArea->record, waField->field, hb_arrayGetItemPtr(pValues, index));
                  cfl_list_add(pSDBArea->context, waField);
               } else {
                  sdb_api_genError(pSDBArea->connection, EG_NOVAR, SDB_ERROR_INVALID_FIELD, "Invalid field name",
                                   EF_NONE, NULL, "SDB_SETCONTEXT", NULL);
                  return;
               }
            }
            pSDBArea->isContextActive = (CFL_BOOL) hb_itemGetL(pActive);
            pSDBArea->isRedoQueries = CFL_TRUE;
            sdb_area_sortContextByPartitionKeys(pSDBArea);
         } else {
            sdb_api_genError(pSDBArea->connection, EG_ARG, SDB_ERROR_INVALID_ARGUMENT,
                             "Number of fields and values must be the same", EF_NONE, NULL, "SDB_SETCONTEXT", NULL);
         }
      } else {
         sdb_api_genError(pSDBArea->connection, EG_ARG, SDB_ERROR_INVALID_DATATYPE, "Both arguments must be array",
                          EF_NONE, NULL, "SDB_SETCONTEXT", NULL);
      }
   } else if (HB_IS_STRING(pFields)) {
      waField = sdb_area_getField(pSDBArea, hb_itemGetCPtr(pFields));
      if (waField != NULL && IS_DATA_FIELD(waField->field)) {
         if (! sdb_util_itemIsNull(pValues)) {
            clearAreaContext(pSDBArea);
            cfl_list_add(pSDBArea->context, waField);
            waField->isContext = CFL_TRUE;
            sdb_record_setValue(pSDBArea->record, waField->field, pValues);
            pSDBArea->isContextActive = (CFL_BOOL) hb_itemGetL(pActive);
            pSDBArea->isRedoQueries = CFL_TRUE;
         } else {
            sdb_api_genError(pSDBArea->connection, EG_NOVAR, SDB_ERROR_INVALID_DATATYPE, "Value can not be NIL",
                             EF_NONE, NULL, "SDB_SETCONTEXT", NULL);
         }
      } else {
         sdb_api_genError(pSDBArea->connection, EG_NOVAR, SDB_ERROR_INVALID_FIELD, "Invalid field name",
                          EF_NONE, NULL, "SDB_SETCONTEXT", NULL);
      }
   } else {
      sdb_api_genError(pSDBArea->connection, EG_ARG, SDB_ERROR_INVALID_DATATYPE,
                       "First argument must be an array of fields names or an field name", EF_NONE, NULL, "SDB_SETCONTEXT", NULL);
   }
}

/**
 * Returns an array with columns and values to the current context of current workarea
 * Ex.: aContext := Area->( SDB_GetContext() )
 *    The context format is { { "col_name1", "col_value1" }, ..., { "col_nameN", "col_valueN" } }
 * @return an array with columns names and values to the current context of current workarea
 */
HB_FUNC(SDB_GETCONTEXT) {
   AREAP area = (AREAP) hb_rddGetCurrentWorkAreaPointer();
   SDB_AREAP pSDBArea;
   CFL_UINT32 index;
   CFL_UINT32 len;

   sdb_thread_cleanError();
   if (! sdb_area_isSDBArea(area)) {
      sdb_api_genError(NULL, HB_EI_RDDINVALID, SDB_ERROR_INVALID_OPERATION, "Not SDBRDD workarea", EF_NONE, NULL, NULL, NULL);
      hb_ret();
      return;
   }
   pSDBArea = (SDB_AREAP) area;
   if (pSDBArea->isQuery) {
      sdb_api_genError(pSDBArea->connection, EG_UNSUPPORTED, SDB_ERROR_INVALID_OPERATION, "Invalid operation for a query",
                       EF_NONE, NULL, NULL, NULL);
      return;
   }

   len = cfl_list_length(pSDBArea->context);
   if (len > 0) {
      PHB_ITEM pArray = hb_itemNew(NULL);
      hb_arrayNew(pArray, len);
      for (index = 0; index < len; index++) {
         SDB_WAFIELDP waField = (SDB_WAFIELDP) cfl_list_get(pSDBArea->context, index);
         PHB_ITEM pArrayItem = hb_arrayGetItemPtr(pArray, index + 1);
         PHB_ITEM pCol;
         hb_arrayNew(pArrayItem, 2);
         pCol = hb_arrayGetItemPtr(pArrayItem, 1);
         hb_itemPutCL(pCol, cfl_str_getPtr(waField->field->clpName), cfl_str_getLength(waField->field->clpName));
         sdb_record_getValue(pSDBArea->record, waField->field, hb_arrayGetItemPtr(pArrayItem, 2), CFL_TRUE);
      }
      hb_itemReturnRelease(pArray);
   } else {
      hb_ret();
   }
}

/**
 * Define a column to be used as context to the current workarea
 * Ex.: Area->( SDB_SetContextCol( "COL1" ) )
 * @param cColName Name of the column to be used as context
 */
HB_FUNC(SDB_SETCONTEXTCOL) {
   AREAP area = (AREAP) hb_rddGetCurrentWorkAreaPointer();
   PHB_ITEM pField = hb_param(1, HB_IT_STRING);
   SDB_AREAP pSDBArea;

   SDB_LOG_DEBUG(("HB_FUN_SDB_SETCONTEXTCOL(%s)", ITEM_STR(pField)));
   sdb_thread_cleanError();
   if (! sdb_area_isSDBArea(area)) {
      sdb_api_genError(NULL, HB_EI_RDDINVALID, SDB_ERROR_INVALID_OPERATION, "Not SDBRDD workarea",
                       EF_NONE, NULL, "SDB_SETCONTEXTCOL", NULL);
      return;
   }
   pSDBArea = (SDB_AREAP) area;
   if (pSDBArea->isQuery) {
      sdb_api_genError(pSDBArea->connection, EG_UNSUPPORTED, SDB_ERROR_INVALID_OPERATION, "Invalid operation for a query",
                       EF_NONE, NULL, "SDB_SETCONTEXTCOL", NULL);
      return;
   }
   if (pSDBArea->table->isContextualized) {
      sdb_api_genError(pSDBArea->connection, EG_UNSUPPORTED, SDB_ERROR_INVALID_OPERATION, "Table with predefined context",
                       EF_NONE, NULL, "SDB_SETCONTEXTCOL", NULL);
      return;
   }

   if (pField != NULL) {
      SDB_WAFIELDP waField;

      clearAreaContext(pSDBArea);
      waField = sdb_area_getField(pSDBArea, hb_itemGetCPtr(pField));
      if (waField != NULL && IS_DATA_FIELD(waField->field)) {
         waField->isContext = CFL_TRUE;
         cfl_list_add(pSDBArea->context, waField);
      } else {
         sdb_api_genError(pSDBArea->connection, EG_NOVAR, SDB_ERROR_INVALID_FIELD, "Invalid field name", EF_NONE, NULL, NULL, NULL);
      }
   } else {
      sdb_api_genError(pSDBArea->connection, EG_ARG, SDB_ERROR_INVALID_FIELD_TYPE, "Argument must be an field name",
                       EF_NONE, NULL, NULL, NULL);
   }
   hb_ret();
}

/**
 * Define a value to be used in current context of current workarea
 * Ex.: Area->( SDB_SetContextVal( "VALUE1" ) )
 * @param cColValue Value to be used in current context
 */
HB_FUNC(SDB_SETCONTEXTVAL) {
   AREAP area = (AREAP) hb_rddGetCurrentWorkAreaPointer();
   PHB_ITEM pValue = hb_param(1, HB_IT_ANY);
   SDB_AREAP pSDBArea;

   SDB_LOG_DEBUG(("HB_FUN_SDB_SETCONTEXTVAL(%s)", ITEM_STR(pValue)));
   sdb_thread_cleanError();
   if (! sdb_area_isSDBArea(area)) {
      sdb_api_genError(NULL, HB_EI_RDDINVALID, SDB_ERROR_INVALID_OPERATION, "Not SDBRDD workarea",
                       EF_NONE, NULL, "SDB_CONTEXTACTIVE", NULL);
      return;
   }
   pSDBArea = (SDB_AREAP) area;
   if (pSDBArea->isQuery) {
      sdb_api_genError(pSDBArea->connection, EG_UNSUPPORTED, SDB_ERROR_INVALID_OPERATION, "Invalid operation for a query",
                       EF_NONE, NULL, "SDB_CONTEXTACTIVE", NULL);
      return;
   }
   if (pSDBArea->table->isContextualized) {
      sdb_api_genError(pSDBArea->connection, EG_UNSUPPORTED, SDB_ERROR_INVALID_OPERATION, "Table with predefined context",
                       EF_NONE, NULL, "SDB_CONTEXTACTIVE", NULL);
      return;
   }

   if (! sdb_util_itemIsNull(pValue)) {
      SDB_WAFIELDP waField;
      if (cfl_list_length(pSDBArea->context) == 1) {
         waField = (SDB_WAFIELDP) cfl_list_get(pSDBArea->context, 0);
         if (waField->isContext) {
            sdb_record_setValue(pSDBArea->record, waField->field, pValue);
            sdb_area_closeStatements(pSDBArea);
         } else {
            sdb_api_genError(pSDBArea->connection, EG_NOVAR, SDB_ERROR_INVALID_FIELD, "Field is not selected for context",
                             EF_NONE, NULL, NULL, NULL);
         }
      } else if (cfl_list_length(pSDBArea->context) > 1) {
         sdb_api_genError(pSDBArea->connection, EG_NOVAR, SDB_ERROR_INVALID_FIELD, "Context already selected",
                          EF_NONE, NULL, NULL, NULL);
      } else {
         sdb_api_genError(pSDBArea->connection, EG_NOVAR, SDB_ERROR_INVALID_FIELD, "No field select for context",
                          EF_NONE, NULL, NULL, NULL);
      }
   } else {
      sdb_api_genError(pSDBArea->connection, EG_ARG, SDB_ERROR_INVALID_FIELD_TYPE, "Argument can not be NIL",
                       EF_NONE, NULL, NULL, NULL);
   }
   hb_ret();
}

/**
 * Enables or disables the context for the current workarea
 * Ex.: lOldStatus := Area->( SDB_ContextActive( .F. ) )
 * @param lActive Indicates if enables or disables the context
 * @return the previous context activation status
 */
HB_FUNC(SDB_CONTEXTACTIVE) {
   AREAP area = (AREAP) hb_rddGetCurrentWorkAreaPointer();
   PHB_ITEM pValue = hb_param(1, HB_IT_LOGICAL);
   SDB_AREAP pSDBArea;
   CFL_BOOL ctxActive;

   sdb_thread_cleanError();
   if (! sdb_area_isSDBArea(area)) {
      sdb_api_genError(NULL, HB_EI_RDDINVALID, SDB_ERROR_INVALID_OPERATION, "Not SDBRDD workarea",
                       EF_NONE, NULL, "SDB_CONTEXTACTIVE", NULL);
      return;
   }
   pSDBArea = (SDB_AREAP) area;
   if (pSDBArea->isQuery) {
      sdb_api_genError(pSDBArea->connection, EG_UNSUPPORTED, SDB_ERROR_INVALID_OPERATION, "Invalid operation for a query",
                       EF_NONE, NULL, "SDB_CONTEXTACTIVE", NULL);
      return;
   }
   if (pSDBArea->table->isContextualized) {
      if (pValue != NULL) {
         sdb_api_genError(pSDBArea->connection, EG_UNSUPPORTED, SDB_ERROR_INVALID_OPERATION, "Table with predefined context",
                          EF_NONE, NULL, "SDB_CONTEXTACTIVE", NULL);
      } else {
         hb_retl(CFL_TRUE);
      }
      return;
   }
   ctxActive = pSDBArea->isContextActive;
   if (pValue != NULL && pSDBArea->isContextActive != hb_itemGetL(pValue)) {
      pSDBArea->isContextActive = (CFL_BOOL) hb_itemGetL(pValue);
      pSDBArea->isRedoQueries = CFL_TRUE;
      sdb_area_closeStatements(pSDBArea);
   }
   hb_retl(ctxActive);
}

/**
 * Returns if the app is logged in database
 * Ex.: lLogged := SDB_IsLogged()
 * @return if the app is logged in database
 */
HB_FUNC(SDB_ISLOGGED) {
   ENTER_FUN_NAME("SDB_ISLOGGED");
   hb_retl(sdb_isLogged(sdb_getConnection(hb_param(1, HB_IT_ANY), CFL_FALSE)));
   RETURN;
}

/**
 * Returns the workarea connection
 * Ex.: oConnection : = Area->( SDB_GetAreaConnection() )
 *    OR
 *     oConnection : = SDB_GetAreaConnection( Select( "AREA" ) )
 * @return the workarea connection
 */
HB_FUNC(SDB_GETAREACONNECTION) {
   AREAP area;
   PHB_ITEM pArea = hb_param(1, HB_IT_NUMERIC);
   SDB_AREAP pSDBArea;

   ENTER_FUN_NAME("SDB_GETAREACONNECTION");
   sdb_thread_cleanError();
   if (hb_pcount() == 0) {
      area = (AREAP) hb_rddGetCurrentWorkAreaPointer();
   } else if(pArea != NULL && HB_IS_NUMERIC(pArea)) {
      area = (AREAP) hb_rddGetWorkAreaPointer(hb_itemGetNI(pArea));
   } else {
      area = NULL;
   }
   if (sdb_area_isSDBArea(area)) {
      pSDBArea = (SDB_AREAP) area;
      hb_retni(pSDBArea->connection->id);
   } else {
      hb_retni(0);
   }
   RETURN;
}


/**
 * Sets, for the current thread, if string arguments must be trimmed when passed to SQL statements. Don't affect others threads.
 * Ex.: lOldTrimOpt := SDB_TrimSqlParams( .T. )
 *
 * @param lTrim Indicates if SQL parameters must be trimmed
 * @param lNewThreads Apply configuration to new threads. Default is .T.
 *
 * @return the previous trimmed status
 */
HB_FUNC(SDB_TRIMSQLPARAMS) {
   SDB_THREAD_DATAP thData = sdb_thread_getData();
   sdb_thread_cleanError();
   hb_retl(sdb_thread_isTrimParams(thData));
   if (HB_ISLOG(1)) {
      sdb_thread_setTrimParams(thData, (CFL_BOOL) hb_parl(1));
      if (! HB_ISLOG(2) || hb_parl(2)) {
         sdb_api_setTrimParams((CFL_BOOL) hb_parl(1));
      }
   }
}

/**
 * Sets, for current thread, if string fields in queries must be padded with spaces to fill the field length. Don't affect other threads.
 * Ex.: lOldPad := SDB_PadQueryFields( .F. )
 *
 * @param lPad Indicates if queries fields must be right padded
 * @param lNewThreads Apply configuration to new threads. Default is .T.
 *
 * @return the previous value
 */
HB_FUNC(SDB_PADQUERYFIELDS) {
   SDB_THREAD_DATAP thData = sdb_thread_getData();
   sdb_thread_cleanError();
   hb_retl(sdb_thread_isPadFields(thData));
   if (HB_ISLOG(1)) {
      sdb_thread_setPadFields(thData, (CFL_BOOL) hb_parl(1));
      if (! HB_ISLOG(2) || hb_parl(2)) {
         sdb_api_setPadFields((CFL_BOOL) hb_parl(1));
      }
   }
}

/**
 * Sets, for the current thread, if string params must be filled with one space if the param value is NIL or empty string. Don't affect other threads.
 * Ex.: lOldNull := SDB_NullableSQLParams( .T. )
 *
 * @param lNullable Indicates if fields can't be null
 * @param lNewThreads Apply configuration to new threads. Default is .T.
 *
 * @return the previous value
 */
HB_FUNC(SDB_NULLABLESQLPARAMS) {
   SDB_THREAD_DATAP thData = sdb_thread_getData();
   sdb_thread_cleanError();
   hb_retl(sdb_thread_isNullable(thData));
   if (HB_ISLOG(1)) {
      sdb_thread_setNullable(thData, (CFL_BOOL) hb_parl(1));
      if (! HB_ISLOG(2) || hb_parl(2)) {
         sdb_api_setNullable((CFL_BOOL) hb_parl(1));
      }
   }
}

/**
 * Sets if the APPEND should be postponed to avoid an empty INSERT followed by an UPDATE. This option affects the behavior
 * of the next open workareas.
 * Ex.: lOldDelayOpt := SDB_DelayAppend( .T. )
 * @param lDelay Indicaters if the driver should try avoid INSERT + UPDATE, preferring a single INSERT.
 * @return the previous status of delay append option
 */
HB_FUNC(SDB_DELAYAPPEND) {
   SDB_THREAD_DATAP thData;
   PHB_ITEM pValue;
   CFL_BOOL delayApp;

   ENTER_FUN_NAME("SDB_DELAYAPPEND");
   sdb_thread_cleanError();
   thData = sdb_thread_getData();
   delayApp = thData->isDelayAppend;
   pValue = hb_param(1, HB_IT_LOGICAL);
   if (pValue != NULL) {
      thData->isDelayAppend = (CFL_BOOL) hb_itemGetL(pValue);
   }
   hb_retl(delayApp);
   RETURN;
}

/**
 * Return the path and name for the table of current workarea
 * Ex.: cTabPathName := Area->( SDB_TablePathName() )
 * @param lDelay Indicaters if the driver should try avoid INSERT + UPDATE, preferring a single INSERT.
 * @return the path and name of the table of current workarea
 */
HB_FUNC(SDB_TABLEPATHNAME) {
   AREAP area = (AREAP) hb_rddGetCurrentWorkAreaPointer();
   SDB_AREAP pSDBArea;
   CFL_STRP pathName;

   sdb_thread_cleanError();
   if (! sdb_area_isSDBArea(area)) {
      sdb_api_genError(NULL, HB_EI_RDDINVALID, SDB_ERROR_INVALID_OPERATION, "Not SDBRDD workarea", EF_NONE, NULL, NULL, NULL);
      hb_retl(CFL_FALSE);
      return;
   }
   pSDBArea = (SDB_AREAP) area;
   pathName = cfl_str_new(50);
   cfl_str_appendLen(pathName, cfl_str_getPtr(pSDBArea->table->clpSchema->name), cfl_str_getLength(pSDBArea->table->clpSchema->name));
   cfl_str_appendChar(pathName, '\\');
   cfl_str_appendLen(pathName, cfl_str_getPtr(pSDBArea->table->clpName), cfl_str_getLength(pSDBArea->table->clpName));
   hb_retclen(cfl_str_getPtr(pathName), cfl_str_getLength(pathName));
   cfl_str_free(pathName);
}

/**
 * Sets and/or return the app default connection
 * Ex.: nOldConn := SDB_DefaultConnection( nNewDefConn )
 * @param nNewConn Number of the new default connection
 * @return the previous default connection number
 */
HB_FUNC(SDB_DEFAULTCONNECTION) {
   SDB_THREAD_DATAP thData = sdb_thread_getData();
   PHB_ITEM pValue = hb_param(1, HB_IT_NUMERIC);
   CFL_UINT32 connNum = thData->connection->id;

   if (pValue != NULL) {
      SDB_CONNECTIONP conn = sdb_api_getConnection((CFL_UINT32) hb_itemGetNI(pValue));
      if (conn) {
         thData->connection = conn;
      }
   }
   hb_retni(connNum);
}

/**
 * Sets and/or return the app default connection
 * Ex.: pOldConn := SDB_CurrentConnection( pNewConn )
 * @param pNewConn Pointer to the new current connection
 * @return the previous current connection
 */
HB_FUNC(SDB_CURRENTCONNECTION) {
   SDB_THREAD_DATAP thData = sdb_thread_getData();
   SDB_CONNECTIONP previousConn = thData->connection;

   if (hb_pcount() > 0) {
      if (HB_ISNIL(1)) {
         thData->connection = NULL;
      } else if(HB_ISPOINTER(1))  {
         thData->connection = hb_parptr(1);
      }
   }
   if (previousConn != NULL) {
      hb_retptr(previousConn);
   } else {
      hb_ret();
   }
}

/**
 * Sets a SQL filter for the currente workarea. After that, every query send to database
 * will include this filter until it is removed.
 * Ex.: Area->( SDB_SetSqlFilter( "field1 = 1 AND field2 = 2" ) )
 * @param cSQlFilter SQL condition to be used in WHERE condition
 */
HB_FUNC(SDB_SETSQLFILTER) {
   PHB_ITEM pNumArea = hb_param(1, HB_IT_NUMERIC);
   PHB_ITEM pSqlFilter = hb_param(2, HB_IT_STRING);
   AREAP area;
   SDB_AREAP pSDBArea;

   sdb_thread_cleanError();
   if (pNumArea) {
      area = (AREAP) hb_rddGetWorkAreaPointer(hb_itemGetNI(pNumArea));
   } else {
      area = (AREAP) hb_rddGetCurrentWorkAreaPointer();
   }
   if (area != NULL) {
      if (! sdb_area_isSDBArea(area)) {
         sdb_api_genError(NULL, HB_EI_RDDINVALID, SDB_ERROR_INVALID_OPERATION, "Not SDBRDD workarea", EF_NONE, NULL, NULL, NULL);
         return;
      }
      pSDBArea = (SDB_AREAP) area;
      if (pSDBArea->sqlFilter) {
         hb_itemRelease(pSDBArea->sqlFilter);
         if (pSqlFilter) {
            pSDBArea->sqlFilter = hb_itemNew(pSqlFilter);
         } else {
            pSDBArea->sqlFilter = NULL;
         }
         pSDBArea->isRedoQueries = CFL_TRUE;
      } else if (pSqlFilter) {
         pSDBArea->sqlFilter = hb_itemNew(pSqlFilter);
         pSDBArea->isRedoQueries = CFL_TRUE;
      }
   }
}

/**
 * Returns if an index exists for the provided workarea or table name.
 * Ex.: lExists := SDB_IsIndex( "SCH1\TAB", "IDX1" )
 *      lExists := SDB_IsIndex( Select( "AREA" ), "IDX1" )
 *      lExists := area->( SDB_IsIndex( "IDX1" ) )
 * @param xWATable Workarea number or table pathname
 * @param cIndexName Index name to be verified
 * @return .T. if the index exists
 */
HB_FUNC(SDB_ISINDEX) {
   PHB_ITEM pWaItem;
   PHB_ITEM pIndexName;
   AREAP area;
   PHB_FNAME pTableFile;
   PHB_FNAME pIndexFile;
   SDB_INDEXP pIndex;
   char * szSchema;
   CFL_BOOL bResult;

   sdb_thread_cleanError();
   if (hb_pcount() > 1) {
      pWaItem = hb_param(1, HB_IT_STRING | HB_IT_NUMERIC);
      if (pWaItem == NULL) {
         sdb_thread_setError(SDB_ERROR_TYPE_SDB, SDB_ERROR_INVALID_TABLE_NAME, "Table name or workarea not provided");
         hb_retl(CFL_FALSE);
         return;
      }
      pIndexName = hb_param(2, HB_IT_STRING);
      if (pIndexName == NULL) {
         sdb_thread_setError(SDB_ERROR_TYPE_SDB, SDB_ERROR_INVALID_INDEX_NAME, "Index name not provided");
         hb_retl(CFL_FALSE);
         return;
      }
      if (HB_IS_NUMERIC(pWaItem)) {
         area = (AREAP) hb_rddGetWorkAreaPointer(hb_itemGetNI(pWaItem));
         if (area != NULL) {
            if (! sdb_area_isSDBArea(area)) {
               sdb_api_genError(NULL, HB_EI_RDDINVALID, SDB_ERROR_INVALID_WORKAREA, "Not SDBRDD workarea", EF_NONE, NULL, NULL, NULL);
               hb_retl(CFL_FALSE);
               return;
            }
            pIndexFile = hb_fsFNameSplit(hb_itemGetCPtr(pIndexName));
            pIndex = sdb_table_getIndex(((SDB_AREAP) area)->table, pIndexFile->szName);
            bResult = pIndex != NULL;
            hb_xfree(pIndexFile);
         } else {
            bResult = CFL_FALSE;
         }
      } else {
         SDB_CONNECTIONP conn = sdb_getConnection(NULL, CFL_TRUE);
         if (conn == NULL) {
            hb_retl(CFL_FALSE);
            return;
         }
         pTableFile = hb_fsFNameSplit(hb_itemGetCPtr(pWaItem));
         pIndexFile = hb_fsFNameSplit(hb_itemGetCPtr(pIndexName));
         szSchema = sdb_util_getSchemaName(conn, pTableFile->szPath);
         bResult = sdb_api_existsIndex(conn, szSchema, pTableFile->szName, pIndexFile->szName);
         SDB_MEM_FREE(szSchema);
         hb_xfree(pIndexFile);
         hb_xfree(pTableFile);
         if (sdb_thread_hasError()) {
            sdb_api_genErrorFromSDBError(NULL, NULL, EF_NONE, sdb_thread_getLastError(), "SDB_ISINDEX", NULL);
         }
      }
  } else {
      area = (AREAP) hb_rddGetCurrentWorkAreaPointer();
      if (area == NULL) {
         hb_retl(CFL_FALSE);
         return;
      }
      if (! sdb_area_isSDBArea(area)) {
         sdb_api_genError(NULL, HB_EI_RDDINVALID, SDB_ERROR_INVALID_WORKAREA, "Not SDBRDD workarea", EF_NONE, NULL, NULL, NULL);
         hb_retl(CFL_FALSE);
         return;
      }
      pIndexName = hb_param(1, HB_IT_STRING);
      if (pIndexName == NULL) {
         sdb_thread_setError(SDB_ERROR_TYPE_SDB, SDB_ERROR_INVALID_INDEX_NAME, "Index name not provided");
         hb_retl(CFL_FALSE);
         return;
      }
      pIndexFile = hb_fsFNameSplit(hb_itemGetCPtr(pIndexName));
      pIndex = sdb_table_getIndex(((SDB_AREAP) area)->table, pIndexFile->szName);
      hb_xfree(pIndexFile);
      bResult = pIndex != NULL;
   }
   hb_retl(bResult);
}

/**
 * Returns if an index exists for the provided workarea or table name.
 * Ex.: lExists := SDB_Conn_IsIndex( oConn, "SCH1\TAB", "IDX1" )
 * @param oConn          Connection handle
 * @param cTablePathName Table pathname
 * @param cIndexName     Index name to be verified
 * @return .T. if the index exists
 */
HB_FUNC(SDB_CONN_ISINDEX) {
   SDB_CONNECTIONP conn;
   PHB_ITEM pTableName;
   PHB_ITEM pIndexName;
   PHB_FNAME pTableFile;
   PHB_FNAME pIndexFile;
   char * szSchema;
   CFL_BOOL bResult;

   sdb_thread_cleanError();
   conn = sdb_paramConnection(1, CFL_TRUE);
   if (conn == NULL) {
      return;
   }
   pTableName = hb_param(2, HB_IT_STRING);
   if (pTableName == NULL) {
      sdb_thread_setError(SDB_ERROR_TYPE_SDB, SDB_ERROR_INVALID_TABLE_NAME, "Table name not provided");
      hb_retl(CFL_FALSE);
      return;
   }
   pIndexName = hb_param(3, HB_IT_STRING);
   if (pIndexName == NULL) {
      sdb_thread_setError(SDB_ERROR_TYPE_SDB, SDB_ERROR_INVALID_INDEX_NAME, "Index name not provided");
      hb_retl(CFL_FALSE);
      return;
   }
   pIndexFile = hb_fsFNameSplit(hb_itemGetCPtr(pIndexName));
   pTableFile = hb_fsFNameSplit(hb_itemGetCPtr(pTableName));
   szSchema = sdb_util_getSchemaName(conn, pTableFile->szPath);
   bResult = sdb_api_existsIndex(conn, szSchema, pTableFile->szName, pIndexFile->szName);
   SDB_MEM_FREE(szSchema);
   hb_xfree(pTableFile);
   hb_xfree(pIndexFile);
   if (sdb_thread_hasError()) {
      sdb_api_genErrorFromSDBError(NULL, NULL, EF_NONE, sdb_thread_getLastError(), "SDB_ISINDEX", NULL);
   }
   hb_retl(bResult);
}


/**
 * Returns the index expression of the provided index.
 * Ex.: cIdxKey := SDB_IndexKey( "SCH1\TAB", "IDX1" )
 *      cIdxKey := SDB_IndexKey( Select( "AREA" ), "IDX1" )
 * @param xWATable Workarea number or table pathname
 * @param cIndexName Name of index to returns key expression
 * @return key expresion of index
 */
HB_FUNC(SDB_INDEXKEY) {
   PHB_ITEM pWaItem = hb_param(1, HB_IT_ANY);
   PHB_ITEM pIndexName = hb_param(2, HB_IT_STRING);
   AREAP area;
   SDB_AREAP pSDBArea;
   PHB_FNAME pTableFile;
   PHB_FNAME pIndexFile;
   SDB_INDEXP pIndex = NULL;
   char * szSchema;

   sdb_thread_cleanError();
   if (pIndexName != NULL) {
      pIndexFile = hb_fsFNameSplit(hb_itemGetCPtr(pIndexName));
   } else {
      sdb_thread_setError(SDB_ERROR_TYPE_SDB, SDB_ERROR_INVALID_ARGUMENT, "Index name not provided");
      hb_retl(CFL_FALSE);
      return;
   }
   if (pWaItem != NULL && HB_IS_NUMERIC(pWaItem)) {
      area = (AREAP) hb_rddGetWorkAreaPointer(hb_itemGetNI(pWaItem));
      if (area != NULL) {
         if (! sdb_area_isSDBArea(area)) {
            sdb_api_genError(NULL, HB_EI_RDDINVALID, SDB_ERROR_INVALID_OPERATION, "Not SDBRDD workarea", EF_NONE, NULL, NULL, NULL);
            hb_retl(CFL_FALSE);
            return;
         }
         pSDBArea = (SDB_AREAP) area;
         pIndex = sdb_table_getIndex(pSDBArea->table, pIndexFile->szName);
      }
   } else if (pWaItem != NULL && HB_IS_STRING(pWaItem)) {
      SDB_CONNECTIONP conn = sdb_getConnection(NULL, CFL_TRUE);
      if (conn != NULL) {
         pTableFile = hb_fsFNameSplit(hb_itemGetCPtr(pWaItem));
         szSchema = sdb_util_getSchemaName(conn, pTableFile->szPath);
         pIndex = sdb_api_getIndex(conn, szSchema, pTableFile->szName, pIndexFile->szName);
         hb_xfree(pTableFile);
         SDB_MEM_FREE(szSchema);
         if (sdb_thread_hasError()) {
            sdb_api_genErrorFromSDBError(NULL, NULL, EF_NONE, sdb_thread_getLastError(), "SDB_INDEXKEY", NULL);
         }
      }
   } else {
      area = (AREAP) hb_rddGetCurrentWorkAreaPointer();
      if (area) {
         if (! sdb_area_isSDBArea(area)) {
            sdb_api_genError(NULL, HB_EI_RDDINVALID, SDB_ERROR_INVALID_OPERATION, "Not SDBRDD workarea", EF_NONE, NULL, NULL, NULL);
            hb_retl(CFL_FALSE);
            return;
         }
         pSDBArea = (SDB_AREAP) area;
         pIndex = sdb_table_getIndex(pSDBArea->table, pIndexFile->szName);
      }
   }
   hb_xfree(pIndexFile);
   if (pIndex) {
      hb_retc(cfl_str_getPtr(pIndex->field->clpExpression));
   } else {
      hb_ret();
   }
}

/**
 * Returns the index expression of the provided index.
 * Ex.: SDB_GetIndexes( Select( "AREA" ), aIndexes )
 * @param nWorkarea Workarea number or NULL to get indexes of current workarea
 * @param aIndexes Array to be filled with indexes names
 * @return the number of indexes
 */
HB_FUNC(SDB_GETINDEXES) {
   PHB_ITEM pWaItem = hb_param(1, HB_IT_NUMERIC);
   PHB_ITEM pIndexes = hb_param(2, HB_IT_ARRAY);
   AREAP area = NULL;
   SDB_AREAP pSDBArea;
   CFL_UINT32 iCount = 0;
   CFL_UINT32 i;

   sdb_thread_cleanError();
   if (pWaItem == NULL) {
      area = (AREAP) hb_rddGetCurrentWorkAreaPointer();
   } else if (HB_IS_NUMERIC(pWaItem)) {
      area = (AREAP) hb_rddGetWorkAreaPointer(hb_itemGetNI(pWaItem));
   }
   if (area != NULL) {
      if (! sdb_area_isSDBArea(area)) {
         sdb_api_genError(NULL, HB_EI_RDDINVALID, SDB_ERROR_INVALID_OPERATION, "Not SDBRDD workarea", EF_NONE, NULL, NULL, NULL);
         return;
      }
      pSDBArea = (SDB_AREAP) area;
      iCount = cfl_list_length(pSDBArea->table->indexes);
      if (pIndexes != NULL) {
         for (i = 0; i < iCount; i++) {
            SDB_INDEXP pIndex = (SDB_INDEXP) cfl_list_get(pSDBArea->table->indexes, i);
            hb_itemPutCL(hb_arrayGetItemPtr(pIndexes, i + 1), cfl_str_getPtr(pIndex->field->clpIndexName), cfl_str_getLength(pIndex->field->clpIndexName));
         }
      }
   }
   hb_retni(iCount);
}

/**
 * Add a column to a table
 * Ex.: SDB_AddColumn( "TAB", "COL3", "N", 10, 4 )
 * @param cTableName Table name
 * @param cColName Column name
 * @param cColType Data type of column
 * @param nLen Precision or length of column
 * @param nDecimals Scale of a numeric column
 * @return .T. if succeed
 */
HB_FUNC(SDB_ADDCOLUMN) {
   PHB_ITEM pTable = hb_param(1, HB_IT_STRING);
   PHB_ITEM pColName = hb_param(2, HB_IT_STRING);
   PHB_ITEM pColType = hb_param(3, HB_IT_STRING);
   PHB_ITEM pLen = hb_param(4, HB_IT_NUMERIC);
   PHB_ITEM pDec = hb_param(5, HB_IT_NUMERIC);
   SDB_TABLEP table;
   SDB_FIELDP field;
   PHB_FNAME pTableFile;
   char *schemaName;
   CFL_BOOL bResult = CFL_FALSE;
   SDB_CONNECTIONP conn;

   sdb_thread_cleanError();
   conn = sdb_getConnection(NULL, CFL_TRUE);
   if (conn != NULL && pTable != NULL && pColName != NULL && pColType != NULL) {
      pTableFile = hb_fsFNameSplit(hb_itemGetCPtr(pTable));
      schemaName = sdb_util_getSchemaName(conn, pTableFile->szPath);
      table = sdb_api_getTable(conn, schemaName, pTableFile->szName);
      hb_xfree(pTableFile);
      SDB_MEM_FREE(schemaName);
      if (table != NULL) {
         field = sdb_field_new(hb_itemGetCPtr(pColName), (CFL_UINT32) hb_itemGetCLen(pColName),
                 hb_itemGetCPtr(pColName), (CFL_UINT32) hb_itemGetCLen(pColName), SDB_FIELD_DATA,
                 sdb_util_clpToSDBType(hb_itemGetCPtr(pColType)), (CFL_UINT32) hb_itemGetNI(pLen),
                 (CFL_UINT8) hb_itemGetNI(pDec), CFL_TRUE);
         bResult = sdb_api_tableAddColumn(conn, table, field);
         if (! bResult) {
            sdb_field_free(field);
         }
         if (sdb_thread_hasError()) {
            sdb_api_genErrorFromSDBError(NULL, NULL, EF_NONE, sdb_thread_getLastError(), "SDB_ADDCOLUMN", NULL);
         }
      }
   }
   hb_retl(bResult);
}

/**
 * Modify a column of a table
 * Ex.: SDB_AlterColumn( "TAB", "COL3", 30 )
 * @param cTableName Table name
 * @param cColName Column name
 * @param nLen Precision or length of column
 * @param nDecimals Scale of a numeric column
 * @return .T. if succeed
 */
HB_FUNC(SDB_ALTERCOLUMN) {
   PHB_ITEM pTable = hb_param(1, HB_IT_STRING);
   PHB_ITEM pColName = hb_param(2, HB_IT_STRING);
   PHB_ITEM pLen = hb_param(3, HB_IT_NUMERIC);
   PHB_ITEM pDec = hb_param(4, HB_IT_NUMERIC);
   SDB_TABLEP table;
   SDB_FIELDP field;
   PHB_FNAME pTableFile;
   char *schemaName;
   CFL_BOOL bResult = CFL_FALSE;
   SDB_CONNECTIONP conn;
   sdb_thread_cleanError();
   conn = sdb_getConnection(NULL, CFL_TRUE);
   if (conn != NULL && pTable != NULL && pColName != NULL && pLen != NULL) {
      pTableFile = hb_fsFNameSplit(hb_itemGetCPtr(pTable));
      schemaName = sdb_util_getSchemaName(conn, pTableFile->szPath);
      table = sdb_api_getTable(conn, schemaName, pTableFile->szName);
      hb_xfree(pTableFile);
      SDB_MEM_FREE(schemaName);
      if (table != NULL) {
         field = sdb_table_getField(table, hb_itemGetCPtr(pColName));
         if (field != NULL) {
            field->length = (CFL_UINT32) hb_itemGetNI(pLen);
            field->decimals = (CFL_UINT8) hb_itemGetNI(pDec);
            bResult = sdb_api_tableModifyColumn(conn, table, field);
            if (sdb_thread_hasError()) {
               sdb_api_genErrorFromSDBError(NULL, NULL, EF_NONE, sdb_thread_getLastError(), "SDB_ALTERCOLUMN", NULL);
            }
         }
      }
   }
   hb_retl(bResult);
}

/**
 * Rename a Clipper table
 * Ex.: SDB_RenameTable( "TABLE1", "TABLE2" )
 *      SDB_RenameTable( "TABLE1", "TABLE2", "TAB_NAME2" )
 *
 * @param cOldClpName Old table clipper name
 * @param cNewClpName New table clipper name
 * @param cNewPhisicalName Optional new phisical name. If not provided, the phisical name will be equals Clipper name.
 * @return .T. if succeed, .F. otherwise
 */
HB_FUNC(SDB_RENAMETABLE) {
   PHB_ITEM pOldName = hb_param(1, HB_IT_STRING);
   PHB_ITEM pNewClpName = hb_param(2, HB_IT_STRING);
   PHB_ITEM pNewDBName = hb_param(3, HB_IT_STRING);
   PHB_FNAME pTableFile;
   char *schemaName;
   CFL_BOOL bResult = CFL_FALSE;
   SDB_CONNECTIONP conn;

   sdb_thread_cleanError();
   conn = sdb_getConnection(NULL, CFL_TRUE);
   if (conn != NULL && pOldName != NULL && (pNewClpName != NULL || pNewDBName != NULL)) {
      pTableFile = hb_fsFNameSplit(hb_itemGetCPtr(pOldName));
      schemaName = sdb_util_getSchemaName(conn, pTableFile->szPath);
      bResult = sdb_api_tableRename(conn, schemaName, pTableFile->szName, hb_itemGetCPtr(pNewClpName), hb_itemGetCPtr(pNewDBName));
      hb_xfree(pTableFile);
      SDB_MEM_FREE(schemaName);
      if (sdb_thread_hasError()) {
         sdb_api_genErrorFromSDBError(NULL, NULL, EF_NONE, sdb_thread_getLastError(), "SDB_RENAMETABLE", NULL);
      }
   }
   hb_retl(bResult);
}

/**
 * Sets the default type for creating memo fields
 *    Ex.: SDB_DefaultMemoType( SDB_MEMO_BLOB )
 * @param nMemoType New default memo type. The valid values are:
 *    #define SDB_MEMO_LONG 1
 *    #define SDB_MEMO_BLOB 2
 *    #define SDB_MEMO_CLOB 4
 * @param lNewThreads Apply configuration to new threads. Default is .T.
 * @return the old default memo type
 */
HB_FUNC(SDB_DEFAULTMEMOTYPE) {
   PHB_ITEM pValue;
   HB_BYTE oldMemoType;

   ENTER_FUN_NAME("SDB_DEFAULTMEMOTYPE");

   sdb_thread_cleanError();
   pValue = hb_param(1, HB_IT_NUMERIC);
   oldMemoType = sdb_api_getDefaultMemoType();
   if (pValue != NULL) {
      SDB_THREAD_DATAP thData = sdb_thread_getData();
      switch(hb_itemGetNI(pValue)) {
         // SDB_MEMO_LONG
         case 1:
            sdb_thread_setDefaultMemoType(thData, SDB_CLP_MEMO_LONG);
            if (! HB_ISLOG(2) || hb_parl(2)) {
               sdb_api_setDefaultMemoType(SDB_CLP_MEMO_LONG);
            }
            break;

         // SDB_MEMO_BLOB
         case 2:
            sdb_thread_setDefaultMemoType(thData, SDB_CLP_BLOB);
            if (! HB_ISLOG(2) || hb_parl(2)) {
               sdb_api_setDefaultMemoType(SDB_CLP_BLOB);
            }
            break;

         // SDB_MEMO_CLOB
         case 4:
            sdb_thread_setDefaultMemoType(thData, SDB_CLP_CLOB);
            if (! HB_ISLOG(2) || hb_parl(2)) {
               sdb_api_setDefaultMemoType(SDB_CLP_CLOB);
            }
            break;

         default:
            break;
      }
   }
   hb_retni(oldMemoType);
   RETURN;
}

/**
 * Estabilishes a conncetion to database
 *    Ex.: pConnection := SDB_Connect( "ORCLDB", "scott", "tiger",, .T.  )
 * @param cDataBase tnsname to connect to database
 * @param cUser username to login in database
 * @param cPswd password to login in database
 * @param cProduct if there is more than one product registered in RDD, this parameter indicates which one should be used.
 *        Ex.: "ORACLE", "SQLITE"
 *
 * @return a pointer to the connection if succesful
 */
HB_FUNC(SDB_CONNECT) {
   SDB_CONNECTIONP conn;

   ENTER_FUN_NAME("SDB_CONNECT");

   conn = sdb_login(hb_parc(1), hb_parc(2), hb_parc(3), hb_parc(4), "false", 0, CFL_FALSE);
   if (conn != NULL) {
      hb_retptr(conn);
   } else if (sdb_thread_hasError()) {
      sdb_api_genErrorFromSDBError(NULL, NULL, EF_NONE, sdb_thread_getLastError(), "SDB_CONNECT", NULL);
   } else {
      hb_ret();
   }
   RETURN;
}

/**
 * Close the connection to the database
 *    Ex.: SDB_Disconnect( oConn )
 * @param oConnection conection pointer.
 *
 * @return .T. if success on logout or .F. if failed
 */
HB_FUNC(SDB_DISCONNECT) {
   SDB_CONNECTIONP conn;

   ENTER_FUN_NAME("SDB_DISCONNECT");
   conn = (SDB_CONNECTIONP) hb_parptr(1);
   if (conn == NULL) {
      sdb_api_genError(NULL, EG_ARG, SDB_ERROR_INVALID_ARGUMENT, NULL, EF_NONE, "Invalid argument", NULL, NULL);
      RETURN;
   }
   if (sdb_thread_getData()->connection == conn) {
      sdb_thread_getData()->connection = NULL;
   }
   hb_retl((HB_BOOL) sdb_logout(conn));
   RETURN;
}

/**
 * Estabilishes a conncetion to database
 *    Ex.: SDB_Login( "ORCLDB", "scott", "tiger",, .T.  )
 * @param cDataBase tnsname to connect to database
 * @param cUser username to login in database
 * @param cPswd password to login in database
 * @param cProduct if there is more than one product registered in RDD, this parameter indicates which one should be used.
 *        Ex.: "ORACLE", "SQLITE"
 * @param xLockControl this parameter indicates how FLOCK and exclusive mode controls record lock.
 *                     .T. or SDB_LOCK_CONTROL_DB   indicates that the locking control is done in database.
 *                     .F. or SDB_LOCK_CONTROL_NONE no lock control
 *                     string                       an address for a server lock
 *                     SDB_LOCK_AUTO_GET            record is locked when first GET VALUE is done
 *                     SDB_LOCK_AUTO_PUT            record is locked when first PUT VALUE is done
 *
 * @param nServerPort port for connecting to the lock server
 * @return a pointer to the connection if succesful
 */
HB_FUNC(SDB_LOGIN) {
   SDB_CONNECTIONP conn;
   const char *lockControl;
   char *lockMode;
   HB_BOOL bFreeReq = HB_FALSE;
   HB_SIZE nLen;
   PHB_ITEM pLockControl;

   ENTER_FUN_NAME("SDB_LOGIN");

   pLockControl = hb_param(5, HB_IT_ANY);
   if (pLockControl == NULL) {
      lockControl = NULL;
   } else if (HB_IS_NUMERIC(pLockControl)) {
      lockMode = hb_itemString(pLockControl, &nLen, &bFreeReq);
      lockControl = lockMode;
   } else if (HB_IS_LOGICAL(pLockControl)) {
      lockControl = hb_itemGetL(pLockControl) ? "true" : "false";
   } else if (HB_IS_STRING(pLockControl)) {
      lockControl = hb_itemGetCPtr(pLockControl);
   } else {
      lockControl = NULL;
   }
   conn = sdb_login(hb_parc(1), hb_parc(2), hb_parc(3), hb_parc(4), lockControl, (CFL_UINT16) hb_parni(6), CFL_TRUE);
   if (bFreeReq) {
      hb_xfree(lockMode);
   }
   if (conn != NULL) {
      sdb_thread_getData()->connection = conn;
      hb_retptr(conn);
   } else if (sdb_thread_hasError()) {
      sdb_api_genErrorFromSDBError(NULL, NULL, EF_NONE, sdb_thread_getLastError(), "SDB_LOGIN", NULL);
   } else {
      hb_ret();
   }
   RETURN;
}

/**
 * Close the connection to the database
 *    Ex.: SDB_Logout()
 *         SDB_Logout( oConn )
 * @param oConnection conection pointer or number of connection to be closed. if not provided, close the current connection if exists.
 * @return .T. if success on logout or .F. if failed
 */
HB_FUNC(SDB_LOGOUT) {
   SDB_CONNECTIONP conn;
   CFL_BOOL bSuccess;
   ENTER_FUN_NAME("SDB_LOGOUT");
   conn = sdb_getConnection(hb_param(1, HB_IT_ANY), CFL_TRUE);
   if (conn != NULL) {
      if (sdb_thread_getData()->connection == conn) {
         sdb_thread_getData()->connection = NULL;
      }
      bSuccess = sdb_logout(conn);
   } else {
      bSuccess = CFL_FALSE;
   }
   hb_retl((HB_BOOL) bSuccess);
   RETURN;
}

/**
 * Finalizes the SDB RDD. Close all connections and releases all allocated resources.
 */
HB_FUNC(SDB_FINALIZE) {
   ENTER_FUN_NAME("SDB_FINALIZE");
   sdb_api_finalize();
   RETURN;
}

/**
 * Last error message
 * @return the message of the last error occurred
 */
HB_FUNC(SDB_GETERRORMSG) {
   SDB_ERRORP error = sdb_thread_getLastError();
   hb_retclen(cfl_str_getPtr(error->message), cfl_str_getLength(error->message));
}

/**
 * Last error code
 * @return the code of the last error occurred
 */
HB_FUNC(SDB_GETERRORCODE) {
   SDB_ERRORP error = sdb_thread_getLastError();
   hb_retni(error->code);
}

/**
 * Last type error
 * @return the type of the last error occurred
 */
HB_FUNC(SDB_GETERRORTYPE) {
   SDB_ERRORP error = sdb_thread_getLastError();
   hb_retni(error->type);
}

/**
 * Sets the check interval for unused cursors
 * @param nInterval intefval in seconds to check unused cursors
 * @return the previous value
 */
HB_FUNC(SDB_CLOSECURSORINTERVAL) {
   int newInterval = hb_parni(1);
   sdb_thread_cleanError();
   hb_retni(sdb_api_getIntervalCloseCursor());
   if (newInterval > 0) {
      sdb_api_setIntervalCloseCursor(newInterval);
   }
}

/**
 * Sets to rollback when an error occurs
 * @param xConnection an pointer to or number of connection to set the behavior. if not provided will be made over current connection, if exists
 * @param lRollback .T. defines that should rollback and .F. doesn't
 * @return the previous value
 */
HB_FUNC(SDB_ROLLBACKONERROR) {
   PHB_ITEM pValue;
   SDB_CONNECTIONP conn;

   ENTER_FUN_NAME("SDB_ROLLBACKONERROR");
   sdb_thread_cleanError();
   conn = sdb_getConnection(hb_param(1, HB_IT_ANY), CFL_TRUE);
   if (conn != NULL) {
      hb_retl(conn->isRollbackOnError);
      pValue = hb_param(2, HB_IT_LOGICAL);
      if (pValue) {
         conn->isRollbackOnError = (CFL_BOOL) hb_itemGetL(pValue);
      }
   }
   RETURN;
}

/**
 * Sets the log level for RDD API
 * @param nLevel new log level. Must be one of:
 *     SDB_LOG_LEVEL_OFF   or "OFF"
 *     SDB_LOG_LEVEL_ERROR or "ERROR"
 *     SDB_LOG_LEVEL_WARN  or "WARN"
 *     SDB_LOG_LEVEL_INFO  or "INFO"
 *     SDB_LOG_LEVEL_DEBUG or "DEBUG"
 *     SDB_LOG_LEVEL_TRACE or "TRACE"
 *
 * @return the previous value
 */
HB_FUNC(SDB_LOGLEVEL) {
   PHB_ITEM pLevel = hb_param(1, HB_IT_NUMERIC | HB_IT_STRING);

   ENTER_FUN_NAME("SDB_LOGLEVEL");
   sdb_thread_cleanError();
   hb_retni(sdb_log_getLevel());
   if (pLevel) {
      sdb_log_setLevel(logLevelValue(pLevel));
   }
   RETURN;
}

/**
 * Sets the log path name for RDD API
 *
 * @param cPathName Path and name of log file.
 *
 * @return the previous value
 *
 */
HB_FUNC(SDB_LOGPATHNAME) {
   PHB_ITEM pPathName = hb_param(1, HB_IT_STRING);

   ENTER_FUN_NAME("SDB_LOGPATHNAME");
   sdb_thread_cleanError();
   hb_retc(sdb_log_getPathName());
   if (pPathName) {
      const char *szFile = hb_itemGetCPtr(pPathName);
      if (hb_strnicmp(szFile, "stdout", 6) == 0) {
         sdb_log_setHandle(stdout);
      } else if (hb_strnicmp(szFile, "stderr", 6) == 0) {;
         sdb_log_setHandle(stderr);
      } else {
         sdb_log_setPathName(szFile);
      }
   }
   RETURN;
}

/**
 * Sets the log format for RDD API
 *
 * @param xFormat Supported formats are:
 *     SDB_LOG_FORMAT_DEFAULT or "DEFAULT"
 *     SDB_LOG_FORMAT_GELF    or "GELF"
 *
 * @return the previous value
 *
 */
HB_FUNC(SDB_LOGFORMAT) {
   ENTER_FUN_NAME("SDB_LOGFORMAT");

   sdb_thread_cleanError();
   hb_retni(sdb_log_getFormat());
   if (HB_ISNUM(1)) {
      sdb_log_format(hb_parni(1));
   } else if (HB_ISCHAR(1)) {
      const char *szFormat = hb_parc(1);
      if (hb_strnicmp(szFormat, "DEFAULT", 7) == 0) {
         sdb_log_format(SDB_LOG_FORMAT_DEFAULT);
      } else if (hb_strnicmp(szFormat, "GELF", 4) == 0) {
         sdb_log_format(SDB_LOG_FORMAT_GELF);
      }
   }
   RETURN;
}

/**
 * Sets that seek command for the current workarea should attemp to execute queries with equality in key comparison
 * @param lExact .T. active exact query for the current workarea. .F. disable exact query
 * @return the previous value
 */
HB_FUNC(SDB_SEEKEXACTQUERY) {
   PHB_ITEM pExact = hb_param(1, HB_IT_LOGICAL);
   CFL_BOOL bExact = CFL_FALSE;
   AREAP area;
   SDB_AREAP pSDBArea;

   sdb_thread_cleanError();
   area = (AREAP) hb_rddGetCurrentWorkAreaPointer();
   if (area != NULL) {
      if (! sdb_area_isSDBArea(area)) {
         sdb_api_genError(NULL, HB_EI_RDDINVALID, SDB_ERROR_INVALID_OPERATION, "Not SDBRDD workarea", EF_NONE, NULL, NULL, NULL);
         return;
      }
      pSDBArea = (SDB_AREAP) area;
      bExact = pSDBArea->isExactQuery;
      if (pExact) {
         pSDBArea->isExactQuery = (CFL_BOOL) hb_itemGetL(pExact);
      }
   }
   hb_retl(bExact);
}

/**
 * Sets the default precision for numeric values
 * @param xConnection an pointer to or number of connection to set the behavior. if not provided will be made over current connection, if exists
 * @param nPrecision New precision for numeric values
 * @return the previous value
 */
HB_FUNC(SDB_QUERYDEFAULTPRECISION) {
   PHB_ITEM pPrecision;
   SDB_CONNECTIONP conn;

   ENTER_FUN_NAME("SDB_QUERYDEFAULTPRECISION");
   sdb_thread_cleanError();
   conn = sdb_getConnection(hb_param(1, HB_IT_ANY), CFL_TRUE);
   if (conn != NULL) {
      hb_retni(conn->queryDefaultPrecision);
      pPrecision = hb_param(2, HB_IT_NUMERIC);
      if (pPrecision) {
         conn->queryDefaultPrecision = (CFL_UINT8) hb_itemGetNI(pPrecision);
      }
   } else {
      hb_retni(21);
   }
   RETURN;
}

/**
 * Sets the default scale for numeric values
 * @param xConnection an pointer to or number of connection to set the behavior. if not provided will be made over current connection, if exists
 * @param nSacle New scale for numeric values
 * @return the previous value
 */
HB_FUNC(SDB_QUERYDEFAULTSCALE) {
   PHB_ITEM pScale;
   SDB_CONNECTIONP conn;

   sdb_thread_cleanError();
   conn = sdb_getConnection(hb_param(1, HB_IT_ANY), CFL_TRUE);
   if (conn != NULL) {
      hb_retni(conn->queryDefaultScale);
      pScale = hb_param(2, HB_IT_NUMERIC);
      if (pScale) {
         conn->queryDefaultScale = (CFL_UINT8) hb_itemGetNI(pScale);
      }
   } else {
      hb_retni(6);
   }
}

/**
 * Defines that columns of type character with size one in queries must be returned as logical values
 * @param xConnection an pointer to or number of connection to set the behavior. if not provided will be made over current connection, if exists
 * @param lChar1Logical .T. defines that char(1) columns must be returned as logical. .F. indicates that must be returned as is.
 * @return the previous value
 */
HB_FUNC(SDB_QUERYC1LOGICAL) {
   PHB_ITEM pC1Log;
   SDB_CONNECTIONP conn;

   ENTER_FUN_NAME("SDB_QUERYC1LOGICAL");
   sdb_thread_cleanError();
   conn = sdb_getConnection(hb_param(1, HB_IT_ANY), CFL_TRUE);
   if (conn != NULL) {
      hb_retl(conn->isChar1AsLogical);
      pC1Log = hb_param(2, HB_IT_LOGICAL);
      if (pC1Log) {
         conn->isChar1AsLogical = (CFL_BOOL) hb_itemGetL(pC1Log);
      }
   } else {
      hb_retl(CFL_FALSE);
   }
   RETURN;
}

/**
 * Defines that logical parameters in statements must be passed as character with size one.
 * @param xConnection an pointer to or number of connection to set the behavior. if not provided will be made over current connection, if exists
 * @param l1LogicalParC1 .T. defines that logical values must passed as char(1) to statements. .F. as logical if database supports.
 * @return the previous value
 */
HB_FUNC(SDB_LOGICALPARAMC1) {
   PHB_ITEM pC1Log;
   SDB_CONNECTIONP conn;

   ENTER_FUN_NAME("SDB_LOGICALPARAMC1");
   sdb_thread_cleanError();
   conn = sdb_getConnection(hb_param(1, HB_IT_ANY), CFL_TRUE);
   if (conn != NULL) {
      hb_retl(conn->isLogicalParamAsChar1);
      pC1Log = hb_param(2, HB_IT_LOGICAL);
      if (pC1Log) {
         conn->isLogicalParamAsChar1 = (CFL_BOOL) hb_itemGetL(pC1Log);
      }
   } else {
      hb_retl(CFL_FALSE);
   }
   RETURN;
}

/**
 * Set the number of records allocated for queries fetchs.
 *
 * @param nFetchSize the number of records allocated for fetchs
 * @param lNewThreads Apply configuration to new threads. Default is .T.
 *
 * @return the previous value
 */
HB_FUNC(SDB_DEFAULTBUFFERFETCHSIZE) {
   PHB_ITEM pSize = hb_param(1, HB_IT_NUMERIC);

   sdb_thread_cleanError();
   hb_retni(sdb_api_getDefaultBufferFetchSize());
   if (pSize) {
      sdb_thread_setDefaultBufferFetchSize(sdb_thread_getData(), (CFL_UINT16) hb_itemGetNI(pSize));
      if (! HB_ISLOG(2) || hb_parl(2)) {
         sdb_api_setDefaultBufferFetchSize((CFL_UINT16) hb_itemGetNI(pSize));
      }
   }
}

/**
 * Set the number of records allocated for fetch in next query.
 * @param nNextFetchSize the number of records allocated for next query fetch
 * @return the previous value
 */
HB_FUNC(SDB_NEXTBUFFERFETCHSIZE) {
   PHB_ITEM pNext = hb_param(1, HB_IT_NUMERIC);

   sdb_thread_cleanError();
   hb_retni(sdb_api_getNextBufferFetchSize());
   if (pNext) {
      sdb_api_setNextBufferFetchSize((CFL_UINT16) hb_itemGetNI(pNext));
   }
}

/**
 * Add a translation of Clipper function to a database language expression. Translations ared used to create database expressions
 * for indexes and filters  when possible.
 *   Ex.: sdb_AddTranslation(oConn, "ALLTRIM", SDB_CLP_CHARACTER, 1, "TRIM(#1)"));
 *        sdb_AddTranslation(oConn, "DTOS", SDB_CLP_CHARACTER, 1, "TO_CHAR(#1, 'YYYYMMDD')"));
 *
 * @param xConnection an pointer to or number of connection to set the behavior. if not provided will be made over current connection, if exists
 * @param cFunctionName function name to be translated
 * @param xDataType The return datatype of expression.
 * @param nArgs number of arguments of function
 * @param cDatabaseExpr translated database expression
 * @return the previous value
 */
HB_FUNC(SDB_ADDTRANSLATION) {
   PHB_ITEM pName;
   PHB_ITEM pExpr;
   PHB_ITEM pDataType;
   int iNumArgs;
   SDB_CONNECTIONP conn;
   SDB_EXPRESSION_TRANSLATIONP pTrans;

   sdb_thread_cleanError();
   conn = sdb_getConnection(hb_param(1, HB_IT_ANY), CFL_TRUE);
   if (conn == NULL) {
      return;
   }
   pName = hb_param(2, HB_IT_STRING);
   if (pName == NULL) {
      sdb_api_genError(NULL, HB_EI_RDDINVALID, SDB_ERROR_INVALID_ARGUMENT, "Clipper function name not provided", EF_NONE, NULL, NULL, NULL);
      return;
   }
   pDataType = hb_param(3, HB_IT_ANY);
   iNumArgs = hb_parni(4);
   pExpr = hb_param(5, HB_IT_STRING);
   if (pExpr == NULL) {
      sdb_api_genError(NULL, HB_EI_RDDINVALID, SDB_ERROR_INVALID_ARGUMENT, "SQL expression not provided", EF_NONE, NULL, NULL, NULL);
      return;
   }
   pTrans = sdb_expr_translationNew(hb_itemGetCPtr(pName), returnType(pDataType), iNumArgs, hb_itemGetCPtr(pExpr));
   hb_retl(sdb_product_addTranslation(conn->product, pTrans));
   if (sdb_thread_hasError()) {
      sdb_api_genErrorFromSDBError(NULL, NULL, EF_NONE, sdb_thread_getLastError(), "SDB_ADDTRANSLATION", NULL);
   }
   hb_retl(CFL_FALSE);
}

/**
 * Translate a Clipper expression into database expression for current workarea.
 *    Ex.: sdb_ClipperExprToSql("alltrim(FcNome)") => "Trim(FcNome)"
 *         OBS.: if exists a translation of AllTrim function
 * @param cClipperExpr Clipper expression to be translated
 * @param nExprType expression type
 *                  SDB_EXPR_ANY        default type
 *                  SDB_EXPR_CONDITION  expression for query condition
 *                  SDB_EXPR_EXPRESSION regular expresion
 *                  SDB_EXPR_TRIGGER    expression for trigger
 * @return the clipper expression translated to database expression.
 */
HB_FUNC(SDB_CLIPPEREXPRTOSQL) {
   PHB_ITEM pExpr = hb_param(1, HB_IT_STRING);
   PHB_ITEM pFlags = hb_param(2, HB_IT_NUMERIC);

   sdb_thread_cleanError();
   if (pExpr) {
      AREAP area;

      area = (AREAP) hb_rddGetCurrentWorkAreaPointer();
      if (area != NULL) {
         PHB_ITEM pSql;
         if (! sdb_area_isSDBArea(area)) {
            sdb_api_genError(NULL, HB_EI_RDDINVALID, SDB_ERROR_INVALID_OPERATION, "Not SDBRDD workarea", EF_NONE, NULL, NULL, NULL);
            hb_retc_const("");
            return;
         }
         pSql = sdb_api_clipperToSql((SDB_AREAP) area, hb_itemGetCPtr(pExpr), (CFL_UINT32) hb_itemGetCLen(pExpr), pFlags ? (CFL_UINT8) hb_itemGetNI(pFlags) : SDB_EXPR_ANY);
         hb_itemReturnRelease(pSql);
         if (sdb_thread_hasError()) {
            sdb_api_genErrorFromSDBError(NULL, NULL, EF_NONE, sdb_thread_getLastError(), "SDB_CLIPPEREXPRTOSQL", NULL);
         }
      } else {
         hb_retc_const("");
      }
   } else {
      hb_retc_const("");
   }
}

/**
 * Return the RDD name
 */
HB_FUNC(SDB_RDDNAME) {
   hb_retc(sdb_api_getRddName());
}

/**
 * Determines the behavior of GO TOP command for the current workarea.
 * @param  nMode Mode to execute queries for GO TOP command
 *               QRY_TOP_ALL mount query to return all records from top to bottom
 *               QRY_TOP_MIN mount query to return only the top record
 * @return the previous mode
 */
HB_FUNC(SDB_GOTOPMODE) {
   PHB_ITEM pMode = hb_param(1, HB_IT_NUMERIC);
   int iMode = QRY_TOP_ALL;
   AREAP area;
   SDB_AREAP pSDBArea;

   sdb_thread_cleanError();
   area = (AREAP) hb_rddGetCurrentWorkAreaPointer();
   if (area != NULL) {
      if (! sdb_area_isSDBArea(area)) {
         sdb_api_genError(NULL, HB_EI_RDDINVALID, SDB_ERROR_INVALID_OPERATION, "Not SDBRDD workarea", EF_NONE, NULL, NULL, NULL);
         hb_retni(iMode);
         return;
      }
      pSDBArea = (SDB_AREAP) area;
      iMode = pSDBArea->uiQueryTopMode;
      if (pMode) {
         pSDBArea->uiQueryTopMode = (CFL_UINT8) hb_itemGetNI(pMode);
      }
   }
   hb_retni(iMode);
}

/**
 * Determines the behavior of GO BOTTOM command for the current workarea.
 * @param  nMode Mode to execute queries for GO BOTTOM command
 *               QRY_BOTTOM_ALL mount query to return all records from bottom to top
 *               QRY_BOTTOM_MAX mount query to return only the bottom record
 * @return the previous mode
 */
HB_FUNC(SDB_GOBOTTOMMODE) {
   PHB_ITEM pMode = hb_param(1, HB_IT_NUMERIC);
   int iMode = QRY_BOTTOM_ALL;
   AREAP area;
   SDB_AREAP pSDBArea;

   sdb_thread_cleanError();
   area = (AREAP) hb_rddGetCurrentWorkAreaPointer();
   if (area != NULL) {
      if (! sdb_area_isSDBArea(area)) {
         sdb_api_genError(NULL, HB_EI_RDDINVALID, SDB_ERROR_INVALID_OPERATION, "Not SDBRDD workarea", EF_NONE, NULL, NULL, NULL);
         hb_retni(iMode);
         return;
      }
      pSDBArea = (SDB_AREAP) area;
      iMode = pSDBArea->uiQueryBottomMode;
      if (pMode) {
         pSDBArea->uiQueryBottomMode = (CFL_UINT8) hb_itemGetNI(pMode);
      }
   }
   hb_retni(iMode);
}

/**
 * Sets the wait timeout in seconds when try lock a record for the current workarea.
 *
 * @param  nTime Time in seconds to wait trying lock a record
 * @return the previous timeout value
 */
HB_FUNC(SDB_WAITTIMELOCK) {
   PHB_ITEM pTime = hb_param(1, HB_IT_NUMERIC);
   CFL_UINT16 iCurrentTime = 0;
   AREAP area;
   SDB_AREAP pSDBArea;

   sdb_thread_cleanError();
   area = (AREAP) hb_rddGetCurrentWorkAreaPointer();
   if (area != NULL) {
      if (! sdb_area_isSDBArea(area)) {
         sdb_api_genError(NULL, HB_EI_RDDINVALID, SDB_ERROR_INVALID_OPERATION, "Not SDBRDD workarea", EF_NONE, NULL, NULL, NULL);
         hb_retni(0);
         return;
      }
      pSDBArea = (SDB_AREAP) area;
      iCurrentTime = pSDBArea->waitTimeLock;
      if (pTime) {
         pSDBArea->waitTimeLock = (CFL_UINT16) hb_itemGetNI(pTime);
      }
   }
   hb_retni(iCurrentTime);
}

/**
 * Set the default PK field name. Default is RECNO.
 * @param xConnection an pointer to or number of connection to set the behavior. if not provided will be made over current connection, if exists
 * @param cPkName the PK field name
 */
HB_FUNC(SDB_SETDEFAULTPKNAME) {
   SDB_CONNECTIONP conn;
   PHB_ITEM pName = hb_param(2, HB_IT_STRING);

   sdb_thread_cleanError();
   conn = sdb_getConnection(hb_param(1, HB_IT_ANY), CFL_TRUE);
   if (conn != NULL && pName != NULL && hb_itemGetCLen(pName) > 0) {
      cfl_str_setValueLen(conn->defaultPKName, hb_itemGetCPtr(pName), (CFL_UINT32) hb_itemGetCLen(pName));
      cfl_str_toUpper(conn->defaultPKName);
   }
}

/**
 * Set the default DEL flag field name. Default is IS_DELETED.
 * @param xConnection an pointer to or number of connection to set the behavior. if not provided will be made over current connection, if exists
 * @param cDelName the DEL flag field name
 */
HB_FUNC(SDB_SETDEFAULTDELFLAGNAME) {
   SDB_CONNECTIONP conn;
   PHB_ITEM pName = hb_param(2, HB_IT_STRING);

   sdb_thread_cleanError();
   conn = sdb_getConnection(hb_param(1, HB_IT_ANY), CFL_TRUE);
   if (conn != NULL && pName != NULL && hb_itemGetCLen(pName) > 0) {
      cfl_str_setValueLen(conn->defaultDelName, hb_itemGetCPtr(pName), (CFL_UINT32) hb_itemGetCLen(pName));
      cfl_str_toUpper(conn->defaultDelName);
   }
}

/**
 * Create a LOB variable for the current connection.
 * Ex.: pLob := SDB_LobNew( SDB_CLP_BLOB )
 * @param nLobType The type of LOB that should be created
 *        SDB_CLP_BLOB Binary LOB
 *        SDB_CLP_CLOB Character LOB
 * @return a lob variable
 */
HB_FUNC(SDB_LOBNEW) {
   CFL_UINT8 lobType = (CFL_UINT8) hb_parni(1);
   SDB_LOBP pLob;
   SDB_CONNECTIONP conn;

   ENTER_FUN_NAME("SDB_LOBNEW");
   sdb_thread_cleanError();
   conn = sdb_getConnection(NULL, CFL_TRUE);
   if (conn != NULL) {
      pLob = sdb_connection_createLob(conn, lobType);
      if (pLob != NULL) {
         hb_itemReturnRelease(sdb_lob_itemPut(NULL, pLob));
      } else {
         sdb_api_genErrorFromSDBError(NULL, NULL, EF_NONE, sdb_thread_getLastError(), "SDB_LOBNEW", NULL);
      }
   }
   RETURN;
}

/**
 * Create a LOB variable for the provided connection.
 * Ex.: pLob := SDB_Conn_LobNew( oConn, SDB_CLP_BLOB )
 * @param xConnection an pointer to or number of connection to set the behavior. if not provided will be made over current connection, if exists
 * @param nLobType The type of LOB that should be created
 *        SDB_CLP_BLOB Binary LOB
 *        SDB_CLP_CLOB Character LOB
 * @return a lob variable
 */
HB_FUNC(SDB_CONN_LOBNEW) {
   SDB_CONNECTIONP conn;
   SDB_LOBP lob;

   ENTER_FUN_NAME("SDB_CONN_LOBNEW");
   sdb_thread_cleanError();
   conn = sdb_paramConnection(1, CFL_TRUE);
   if (conn != NULL) {
      lob = sdb_connection_createLob(conn, (CFL_UINT8) hb_parni(2));
      if (lob != NULL) {
         hb_itemReturnRelease(sdb_lob_itemPut(NULL, lob));
      } else {
         sdb_api_genErrorFromSDBError(NULL, NULL, EF_NONE, sdb_thread_getLastError(), "SDB_CONN_LOBNEW", NULL);
      }
   }
   RETURN;
}

/**
 * Release the LOB variable.
 *
 * Ex.: pLob := SDB_LobFree( oLob )
 * @param pLob The LOB variable to be released
 */
HB_FUNC(SDB_LOBFREE) {
   PHB_ITEM pItemLob;

   ENTER_FUN_NAME("SDB_LOBFREE");
   sdb_thread_cleanError();
   pItemLob = hb_param(1, HB_IT_POINTER);
   if (pItemLob) {
      SDB_LOBP pLob = sdb_lob_itemGet(pItemLob);
      if (pLob == NULL) {
         sdb_api_genError(NULL, SDB_ERROR_INVALID_ARGUMENT, SDB_ERROR_RELEASING_LOB, NULL, EF_NONE, "Invalid arguments", "SDB_LOBFREE", NULL);
      } else {
         sdb_lob_free(pLob);
         hb_itemClear(pItemLob);
         if (sdb_thread_hasError()) {
            sdb_api_genErrorFromSDBError(NULL, NULL, EF_NONE, sdb_thread_getLastError(), "SDB_LOBFREE", NULL);
         }
      }
   } else {
      sdb_api_genError(NULL, SDB_ERROR_INVALID_ARGUMENT, SDB_ERROR_RELEASING_LOB, NULL, EF_NONE, "Invalid arguments", "SDB_LOBFREE", NULL);
   }
   RETURN;
}

/**
 * Open a lob to be read or write.
 * Ex.: SDB_LobOpen( pLob )
 * @param pLob The LOB variable to be opened
 */
HB_FUNC(SDB_LOBOPEN) {
   PHB_ITEM pItemLob;

   ENTER_FUN_NAME("SDB_LOBOPEN");
   sdb_thread_cleanError();
   pItemLob = hb_param(1, HB_IT_POINTER);
   if (pItemLob) {
      SDB_LOBP pLob = sdb_lob_itemGet(pItemLob);
      if (pLob == NULL) {
         sdb_api_genError(NULL, SDB_ERROR_INVALID_ARGUMENT, SDB_ERROR_OPENING_LOB, NULL, EF_NONE,
                          "Invalid arguments", "SDB_LOBOPEN", NULL);
      } else if (! sdb_lob_open(pLob)) {
         sdb_api_genErrorFromSDBError(NULL, NULL, EF_NONE, sdb_thread_getLastError(), "SDB_LOBOPEN", NULL);
      }
   } else {
      sdb_api_genError(NULL, SDB_ERROR_INVALID_ARGUMENT, SDB_ERROR_OPENING_LOB, NULL, EF_NONE,
                       "Invalid arguments", "SDB_LOBOPEN", NULL);
   }
   RETURN;
}

/**
 * Close a lob.
 * Ex.: SDB_LobClose( pLob )
 * @param pLob The LOB variable to be closed
 */
HB_FUNC(SDB_LOBCLOSE) {
   PHB_ITEM pItemLob;

   ENTER_FUN_NAME("SDB_LOBCLOSE");
   sdb_thread_cleanError();
   pItemLob = hb_param(1, HB_IT_POINTER);
   if (pItemLob) {
      SDB_LOBP pLob = sdb_lob_itemGet(pItemLob);
      if (pLob == NULL) {
         sdb_api_genError(NULL, SDB_ERROR_INVALID_ARGUMENT, SDB_ERROR_CLOSING_LOB, NULL, EF_NONE,
                          "Invalid arguments", "SDB_LOBCLOSE", NULL);
      } else {
         hb_itemClear(pItemLob);
         if (sdb_thread_hasError()) {
            sdb_api_genErrorFromSDBError(NULL, NULL, EF_NONE, sdb_thread_getLastError(), "SDB_LOBCLOSE", NULL);
         }
      }
   } else {
      sdb_api_genError(NULL, SDB_ERROR_INVALID_ARGUMENT, SDB_ERROR_CLOSING_LOB, NULL, EF_NONE,
                       "Invalid arguments", "SDB_LOBCLOSE", NULL);
   }
   RETURN;
}

/**
 * Read the content from a LOB variable.
 * Ex. : cBuffer := Space( 7000 )
 *       SDB_LobRead( pLob, @cBuffer, 1, 6000 )
 * @param pLob The LOB variable to be read
 * @param cBuffer buffer to receive content read from the LOB
 * @param nOffset the offset to start reading. if not provided the offset value is set to 1.
 * @param nAmount the amount of data to de read. if not provided it will attemp to read the buffer length.
 */
HB_FUNC(SDB_LOBREAD) {
   PHB_ITEM pItemLob;
   PHB_ITEM pBuffer;
   PHB_ITEM pOffset;
   PHB_ITEM pAmount;
   SDB_LOBP pLob;
   CFL_UINT64 offset;
   CFL_UINT64 amount;

   ENTER_FUN_NAME("SDB_LOBREAD");
   sdb_thread_cleanError();
   pItemLob = hb_param(1, HB_IT_POINTER);
   if (pItemLob == NULL) {
      sdb_api_genError(NULL, SDB_ERROR_INVALID_ARGUMENT, SDB_ERROR_READING_LOB, NULL, EF_NONE,
                       "Invalid Arguments", "SDB_LOBREAD", NULL);
      RETURN;
   }
   pLob = sdb_lob_itemGet(pItemLob);
   if (pLob == NULL) {
      sdb_api_genError(NULL, SDB_ERROR_INVALID_ARGUMENT, SDB_ERROR_READING_LOB, NULL, EF_NONE,
                       "Invalid Arguments", "SDB_LOBREAD", NULL);
      RETURN;
   }
   pBuffer = hb_param(2, HB_IT_BYREF);
   if (pBuffer == NULL) {
      sdb_api_genError(NULL, SDB_ERROR_INVALID_ARGUMENT, SDB_ERROR_READING_LOB, NULL, EF_NONE,
                       "Invalid Arguments", "SDB_LOBREAD", NULL);
      RETURN;
   }
   pOffset = hb_param(3, HB_IT_NUMERIC);
   if (pOffset != NULL) {
      offset = (CFL_UINT64) hb_itemGetNLL(pOffset);
   } else {
      offset = 0;
   }
   pAmount = hb_param(4, HB_IT_NUMERIC);
   if (pAmount != NULL) {
      if (hb_itemGetNLL(pAmount) > 0 && (HB_SIZE) hb_itemGetNLL(pAmount) <= hb_itemGetCLen(pBuffer)) {
         amount = (CFL_UINT64) hb_itemGetNLL(pAmount);
      } else {
         sdb_api_genError(NULL, SDB_ERROR_INVALID_ARGUMENT, SDB_ERROR_READING_LOB, NULL, EF_NONE,
                          "Amount of data to read must be greater than 0 and less than the buffer capacity", "SDB_LOBREAD", NULL);
         RETURN;
      }
   } else {
      amount = hb_itemGetCLen(pBuffer);
   }
   if (amount == 0) {
      sdb_api_genError(NULL, SDB_ERROR_INVALID_ARGUMENT, SDB_ERROR_READING_LOB, NULL, EF_NONE,
                       "Amount of data to read must be greater than 0", "SDB_LOBREAD", NULL);
      RETURN;
   }
   if (sdb_lob_read((SDB_LOBP) pLob, hb_itemGetCPtr(pBuffer), offset, &amount)) {
      hb_retnint(amount);
   } else {
      sdb_api_genErrorFromSDBError(NULL, NULL, EF_NONE, sdb_thread_getLastError(), "SDB_LOBREAD", NULL);
   }
   RETURN;
}

/**
 * Writes data to a LOB variable.
 * Ex. : SDB_LobWrite( pLob, Replicate( "X", 5000 ) )
 * @param pLob The LOB variable to be write
 * @param cBuffer buffer to receive content read from the LOB
 * @param nOffset the offset to start writing. if not provided the offset value is set to 1.
 * @param nAmount the amount of data to de write. if not provided all buffer content will be written.
 */
HB_FUNC(SDB_LOBWRITE) {
   PHB_ITEM pItemLob;
   PHB_ITEM pBuffer;
   PHB_ITEM pOffset;
   PHB_ITEM pAmount;
   void *pLob;
   CFL_UINT64 offset;
   CFL_UINT64 amount;

   ENTER_FUN_NAME("SDB_LOBWRITE");
   sdb_thread_cleanError();
   pItemLob = hb_param(1, HB_IT_POINTER);
   if (pItemLob == NULL) {
      sdb_api_genError(NULL, SDB_ERROR_INVALID_ARGUMENT, SDB_ERROR_READING_LOB, NULL, EF_NONE,
                       "Invalid Arguments", "SDB_LOBWRITE", NULL);
      RETURN;
   }
   pLob = sdb_lob_itemGet(pItemLob);
   if (pLob == NULL) {
      sdb_api_genError(NULL, SDB_ERROR_INVALID_ARGUMENT, SDB_ERROR_READING_LOB, NULL, EF_NONE,
                       "Invalid Arguments", "SDB_LOBWRITE", NULL);
      RETURN;
   }
   pBuffer = hb_param(2, HB_IT_STRING);
   if (pBuffer == NULL) {
      sdb_api_genError(NULL, SDB_ERROR_INVALID_ARGUMENT, SDB_ERROR_READING_LOB, NULL, EF_NONE,
                       "Invalid Arguments", "SDB_LOBWRITE", NULL);
      RETURN;
   }
   pOffset = hb_param(3, HB_IT_NUMERIC);
   if (pOffset != NULL) {
      offset = (CFL_UINT64) hb_itemGetNLL(pOffset);
   } else {
      offset = 1;
   }
   pAmount = hb_param(4, HB_IT_NUMERIC);
   if (pAmount != NULL) {
      if (hb_itemGetNLL(pAmount) > 0 && (HB_SIZE) hb_itemGetNLL(pAmount) <= hb_itemGetCLen(pBuffer)) {
         amount = (CFL_UINT64) hb_itemGetNLL(pAmount);
      } else {
         sdb_api_genError(NULL, SDB_ERROR_INVALID_ARGUMENT, SDB_ERROR_READING_LOB, NULL, EF_NONE,
                          "Amount of data to write must be greater than 0 and less than the buffer size", "SDB_LOBWRITE", NULL);
         RETURN;
      }
   } else {
      amount = hb_itemGetCLen(pBuffer);
   }
   if (amount == 0) {
      sdb_api_genError(NULL, SDB_ERROR_INVALID_ARGUMENT, SDB_ERROR_READING_LOB, NULL, EF_NONE,
                       "Invalid amount of data to write", "SDB_LOBWRITE", NULL);
      RETURN;
   }
   if (sdb_lob_write((SDB_LOBP) pLob, hb_itemGetCPtr(pBuffer), offset, amount)) {
      hb_retl(HB_TRUE);
   } else {
      sdb_api_genErrorFromSDBError(NULL, NULL, EF_NONE, sdb_thread_getLastError(), "SDB_LOBWRITE", NULL);
   }
   RETURN;
}

/**
 * Get the database version
 * Ex.:
 *    sdb_ServerVersion() => "18.0.0.0.0"
 * @return the database version
 */
HB_FUNC(SDB_SERVERVERSION) {
   PHB_ITEM pVersion;
   SDB_CONNECTIONP conn;

   sdb_thread_cleanError();
   conn = sdb_getConnection(hb_param(1, HB_IT_ANY), CFL_FALSE);
   if (conn != NULL) {
      pVersion = sdb_connection_getServerVersion(conn);
      if (pVersion) {
         hb_itemReturnRelease(pVersion);
      } else {
         hb_retc_const("");
      }
   } else {
      hb_retc_const("");
   }
}

/**
 * Get the database client version
 * Ex.:
 *    sdb_ClientVersion() => "11.2.0.1.0"
 * @return the database client version
 */
HB_FUNC(SDB_CLIENTVERSION) {
   PHB_ITEM pVersion;
   SDB_CONNECTIONP conn;

   sdb_thread_cleanError();
   conn = sdb_getConnection(hb_param(1, HB_IT_ANY), CFL_FALSE);
   if (conn != NULL) {
      pVersion = sdb_connection_getClientVersion(conn);
      if (pVersion) {
         hb_itemReturnRelease(pVersion);
      } else {
         hb_retc_const("");
      }
   } else {
      hb_retc_const("");
   }
}

/**
 * Get the SDB RDD version
 * Ex.:
 *    sdb_Version() => "1.0.1.0"
 * @return the database client version
 */
HB_FUNC(SDB_VERSION) {
   hb_retc(STRINGER(_SDB_VERSION_));
}

/**
 * Get the size of RECNO field in bytes
 * @return the recno size in bytes
 */
HB_FUNC(SDB_RECNOSIZE) {
   hb_retni(_SDB_RECNO_SIZE_);
}

/**
 * Set the current schema and returns the previous for the current connection. if the name of new schema is not provided, then
 * only returns the current schema name
 * @param cSchema new schema name to set as current
 * @return the previous schema name
 */
HB_FUNC(SDB_CURRENTSCHEMA) {
   PHB_ITEM pSchema;
   SDB_SCHEMAP currSchema;
   SDB_CONNECTIONP conn;

   sdb_thread_cleanError();
   conn = sdb_getConnection(NULL, CFL_TRUE);
   if (conn != NULL) {
      pSchema = hb_param(1, HB_IT_STRING);
      currSchema = sdb_connection_getCurrentSchema(conn);
      if (pSchema) {
         if (!sdb_connection_setCurrentSchema(conn, hb_itemGetCPtr(pSchema))) {
            sdb_api_genErrorFromSDBError(NULL, NULL, EF_NONE, sdb_thread_getLastError(), NULL, NULL);
         }
      }
      hb_retc(currSchema ? cfl_str_getPtr(currSchema->name) : "");
   } else {
      hb_retc_const("");
   }
}

/**
 * Set the current schema and returns the previous for the passed connection. if the name of new schema is not provided, then
 * only returns the current schema name
 * @param xConnection an pointer to or number of connection to set the behavior. if not provided will be made over current connection, if exists
 * @param cSchema new schema name to set as current
 * @return the previous schema name
 */
HB_FUNC(SDB_CONN_CURRENTSCHEMA) {
   SDB_CONNECTIONP conn;
   PHB_ITEM pSchema;
   SDB_SCHEMAP currSchema;

   sdb_thread_cleanError();
   conn = sdb_paramConnection(1, CFL_TRUE);
   if (conn != NULL) {
      pSchema = hb_param(2, HB_IT_STRING);
      currSchema = sdb_connection_getCurrentSchema(conn);
      if (pSchema) {
         if (!sdb_connection_setCurrentSchema(conn, hb_itemGetCPtr(pSchema))) {
            sdb_api_genErrorFromSDBError(NULL, NULL, EF_NONE, sdb_thread_getLastError(), NULL, NULL);
         }
      }
      hb_retc(currSchema ? cfl_str_getPtr(currSchema->name) : "");
   }
}

/**
 * Set the statements cache size. if the cache size is not provided, then only returns the current cache size
 * Ex.: sdb_StmtCacheSize( 30 )
 * @param nNewSize new cache statements size.
 * @return the previous size
 */
HB_FUNC(SDB_STMTCACHESIZE) {
   SDB_CONNECTIONP conn;
   PHB_ITEM pCacheSize = hb_param(1, HB_IT_NUMERIC);
   CFL_UINT32 currentSize = 0;

   sdb_thread_cleanError();
   conn = sdb_getConnection(NULL, CFL_TRUE);
   if (conn != NULL) {
      currentSize = sdb_connection_getStmtCacheSize(conn);
      if (pCacheSize) {
         if (!sdb_connection_setStmtCacheSize(conn, hb_itemGetNI(pCacheSize))) {
            sdb_api_genErrorFromSDBError(NULL, NULL, EF_NONE, sdb_thread_getLastError(), NULL, NULL);
         }
      }
   }
   hb_retni(currentSize);
}

/**
 * Set the statements cache size. if the cache size is not provided, then only returns the current cache size
 * Ex.: sdb_Conn_StmtCacheSize( oConn, 30 )
 * @param xConnection an pointer to or number of connection to set the behavior. if not provided will be made over current connection, if exists
 * @param nNewSize new cache statements size.
 * @return the previous size
 */
HB_FUNC(SDB_CONN_STMTCACHESIZE) {
   SDB_CONNECTIONP conn;
   PHB_ITEM pCacheSize;

   sdb_thread_cleanError();
   conn = sdb_paramConnection(1, CFL_TRUE);
   if (conn != NULL) {
      hb_retni(sdb_connection_getStmtCacheSize(conn));
      sdb_thread_cleanError();
      pCacheSize = hb_param(2, HB_IT_NUMERIC);
      if (pCacheSize) {
         if (!sdb_connection_setStmtCacheSize(conn, hb_itemGetNI(pCacheSize))) {
            sdb_api_genErrorFromSDBError(NULL, NULL, EF_NONE, sdb_thread_getLastError(), NULL, NULL);
         }
      }
   }
}

/**
 * Interupt an operation in execution in database for the connection provided or current connection
 * Ex.: sdb_BreakOperation()
 *      sdb_BreakOperation( oConn )
 * @param xConnection an pointer to or number of connection to set the behavior. if not provided will be made over current connection, if exists
 * @return the previous size
 */
HB_FUNC(SDB_BREAKOPERATION) {
   if (sdb_breakOperation(sdb_getConnection(hb_param(1, HB_IT_ANY), CFL_TRUE))) {
      hb_retl(HB_TRUE);
   } else if (sdb_thread_hasError()) {
      sdb_api_genErrorFromSDBError(NULL, NULL, EF_NONE, sdb_thread_getLastError(), "SDB_BREAKOPERATION", NULL);
   } else {
      hb_retl(HB_FALSE);
   }
}

/**
 * Get the database product name
 * @param xConnection an pointer to or number of connection to set the behavior. if not provided will be made over current connection, if exists
 * @return the database product name
 */
HB_FUNC(SDB_DATABASENAME) {
   SDB_CONNECTIONP conn = sdb_getConnection(hb_param(1, HB_IT_ANY), CFL_FALSE);
   if (conn != NULL) {
      hb_retclen(cfl_str_getPtr(conn->product->displayName), cfl_str_getLength(conn->product->displayName));
   } else {
      hb_retc_const("");
   }
}

/**
 * Enable or disable hints in API commands for current connection
 * Ex.: sdb_HintsEnable( .T. )
 *      sdb_HintsEnable( .F. )
 * @param lEnable .T. enable the use of hints, .F. disable
 * @return the previous value
 */
HB_FUNC(SDB_HINTSENABLE) {
   PHB_ITEM pValue;
   SDB_CONNECTIONP conn;

   sdb_thread_cleanError();
   conn = sdb_getConnection(NULL, CFL_TRUE);
   if (conn != NULL) {
      hb_retl(conn->isHintsEnable);
      pValue = hb_param(1, HB_IT_LOGICAL);
      if (pValue != NULL) {
         conn->isHintsEnable = hb_itemGetL(pValue);
      }
   }
}

/**
 * Enable or disable hints in API commands
 * Ex.: sdb_Conn_HintsEnable( oConn, .T. )
 *      sdb_Conn_HintsEnable( oConn, .F. )
 * @param xConnection an pointer to or number of connection to set the behavior. if not provided will be made over current connection, if exists
 * @param lEnable .T. enable the use of hints, .F. disable
 * @return the previous value
 */
HB_FUNC(SDB_CONN_HINTSENABLE) {
   SDB_CONNECTIONP conn = sdb_paramConnection(1, CFL_TRUE);
   PHB_ITEM pValue;

   sdb_thread_cleanError();
   if (conn != NULL) {
      hb_retl(conn->isHintsEnable);
      pValue = hb_param(2, HB_IT_LOGICAL);
      if (pValue != NULL) {
         conn->isHintsEnable = hb_itemGetL(pValue);
      }
   }
}

/**
 * Set the mode how FLOCK and exclusive mode controls the records lock for the current workarea
 * Ex.: area->( sdb_LockMode( SDB_LOCK_AUTO_GET ) )
 *
 * @param nMode Lock mode to be activated for workarea
 *    SDB_LOCK_CONTROL_NONE   Records aren't locked.
 *    SDB_LOCK_CONTROL_DB     The lock is done over table or table partition (when context activated)
 *    SDB_LOCK_AUTO_GET       The record is locked when first field is get from record
 *    SDB_LOCK_AUTO_PUT       The record is locked when first field is changed in record
 *    SDB_LOCK_CONTROL_SERVER The lock is control by Lock Server. Records aren't locked. To use this option, the lock server
 *                            address must be provided in login
 * @return the previous lock mode
 */
HB_FUNC(SDB_LOCKMODE) {
   AREAP area;
   SDB_AREAP pSDBArea;
   PHB_ITEM pMode = hb_param(1, HB_IT_NUMERIC);
   CFL_UINT8 iCurrentLockMode = 0;
   CFL_UINT8 iNewLockMode;

   sdb_thread_cleanError();
   area = hb_rddGetCurrentWorkAreaPointer();
   if (area != NULL) {
      if (! sdb_area_isSDBArea(area)) {
         sdb_api_genError(NULL, HB_EI_RDDINVALID, SDB_ERROR_INVALID_OPERATION, "Not SDBRDD workarea", EF_NONE, NULL, NULL, NULL);
         hb_retni(0);
         return;
      }
      pSDBArea = (SDB_AREAP) area;
      iCurrentLockMode = pSDBArea->lockControl;
      if (pMode) {
         iNewLockMode = (CFL_UINT8) hb_itemGetNI(pMode);
         if (iNewLockMode == SDB_LOCK_CONTROL_NONE || iNewLockMode == SDB_LOCK_AUTO_GET || iNewLockMode == SDB_LOCK_AUTO_PUT) {
            pSDBArea->lockControl = iNewLockMode;
         } else {
           sdb_api_genError(NULL, SDB_ERROR_INVALID_ARGUMENT, SDB_ERROR_INVALID_LOCK_MODE, NULL, EF_NONE,
                            "Invalid lock mode for workarea", "SDB_LOCKMODE", NULL);
         }
      }
   }
   hb_retni(iCurrentLockMode);
}
