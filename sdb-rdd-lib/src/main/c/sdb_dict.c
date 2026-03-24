#include "hbapiitm.h"

#include "hbapi.h"

#include "cfl_str.h"
#include "cfl_list.h"
#include "cfl_sql.h"

#include "sdb_dict.h"
#include "sdb_api.h"
#include "sdb_product.h"
#include "sdb_connection.h"
#include "sdb_statement.h"
#include "sdb_database.h"
#include "sdb_schema.h"
#include "sdb_table.h"
#include "sdb_field.h"
#include "sdb_util.h"
#include "sdb_index.h"
#include "sdb_log.h"
#include "sdb_error.h"
#include "sdb_thread.h"

static SDB_DICTIONARY s_SdbDict;
static SDB_DICTIONARYP s_dictionaries[8];
static CFL_UINT8 s_dictCount = 0;

static CFL_BOOL isLargeField(SDB_FIELDP field) {
   switch (field->clpType) {
      case SDB_CLP_BLOB:
      case SDB_CLP_CLOB:
      case SDB_CLP_LONG_RAW:
         return CFL_TRUE;
   }
   return CFL_FALSE;
}

static CFL_BOOL queryColumns(SDB_CONNECTIONP connection, SDB_SCHEMAP schema, SDB_TABLEP table) {
   CFL_STRP sql = cfl_str_new(512);
   CFL_UINT64 ulAffectedRows = 0;
   PHB_ITEM pClpName = hb_itemNew(NULL);
   PHB_ITEM pDBName = hb_itemNew(NULL);
   PHB_ITEM pColType = hb_itemNew(NULL);
   PHB_ITEM pDataType = hb_itemNew(NULL);
   PHB_ITEM pColLen = hb_itemNew(NULL);
   PHB_ITEM pColDec = hb_itemNew(NULL);
   PHB_ITEM pExpr = hb_itemNew(NULL);
   PHB_ITEM pValue = hb_itemNew(NULL);
   SDB_STATEMENTP pStmt;
   SDB_FIELDP field;
   CFL_LISTP largeFields = cfl_list_new(5);
   CFL_UINT32 ulLen;
   CFL_UINT32 ulIndex;
   CFL_BOOL bResult = CFL_FALSE;

   CFL_STR_APPEND_CONST(sql,
           "select clp_col_name"   // 1
                ", db_col_name"    // 2
                ", col_type"       // 3
                ", clp_data_type"  // 4
                ", clp_data_len"   // 5
                ", clp_data_dec"   // 6
                ", clp_index_name" // 7
                ", db_index_name"  // 8
                ", clp_expression" // 9
                ", db_expression"  // 10
                ", hint_asc"       // 11
                ", hint_desc"      // 12
                ", col_order"      // 13
                ", set_mode"       // 14
                ", context_order"  // 15
                ", context_val"    // 16
            " from ");
   cfl_str_appendStr(sql, schema->name);
   CFL_STR_APPEND_CONST(sql, ".sdb_tables_columns where clp_tab_name=:tab order by col_order");
   pStmt = sdb_connection_prepareStatement(connection, sql);
   if (pStmt != NULL) {
      sdb_stmt_setString(pStmt, "tab", table->clpName, CFL_FALSE);
      if (sdb_stmt_execute(pStmt, &ulAffectedRows)) {
         while (sdb_stmt_fetchNext(pStmt, CFL_TRUE)) {
            sdb_stmt_getQueryValue(pStmt, 1, pClpName);
            sdb_stmt_getQueryValue(pStmt, 2, pDBName);
            sdb_stmt_getQueryValue(pStmt, 3, pColType);
            sdb_stmt_getQueryValue(pStmt, 4, pDataType);
            sdb_stmt_getQueryValue(pStmt, 5, pColLen);
            sdb_stmt_getQueryValue(pStmt, 6, pColDec);
            field = sdb_field_new(hb_itemGetCPtr(pClpName), (CFL_UINT32) hb_itemGetCLen(pClpName),
                                  ! sdb_util_itemIsEmpty(pDBName) ? hb_itemGetCPtr(pDBName) : NULL, (CFL_UINT32) hb_itemGetCLen(pDBName),
                                  (CFL_UINT8) hb_itemGetNI(pColType),
                                  (CFL_UINT8) hb_itemGetNI(pDataType),
                                  ! sdb_util_itemIsEmpty(pColLen) ? (CFL_UINT32) hb_itemGetNI(pColLen) : 0,
                                  ! sdb_util_itemIsEmpty(pColDec) ? (CFL_UINT8) hb_itemGetNI(pColDec) : 0,
                                  CFL_TRUE);
            sdb_stmt_getQueryValue(pStmt, 10, pExpr);
            if (! sdb_util_itemIsEmpty(pExpr)) {
               field->dbExpression = cfl_str_newBufferLen(hb_itemGetCPtr(pExpr), (CFL_UINT32) hb_itemGetCLen(pExpr));
            }
            sdb_stmt_getQueryValue(pStmt, 13, pValue);
            field->order = (CFL_UINT16) hb_itemGetNI(pValue);
            sdb_stmt_getQueryValue(pStmt, 14, pValue);
            if (!sdb_util_itemIsEmpty(pValue) && (hb_itemGetNI(pValue) & SET_MODE_ALL_FLAGS)) {
               field->setMode = (CFL_UINT8) hb_itemGetNI(pValue);
            } else {
               field->setMode = SET_MODE_CLIENT;
            }
            sdb_stmt_getQueryValue(pStmt, 15, pValue);
            field->contextPos = (CFL_UINT8) hb_itemGetNI(pValue);
            if (field->contextPos > 0) {
               table->isContextualized = CFL_TRUE;
            }
            sdb_stmt_getQueryValue(pStmt, 16, pValue);
            if (!sdb_util_itemIsEmpty(pValue)) {
               field->contextVal = cfl_str_newBufferLen(hb_itemGetCPtr(pValue), (CFL_UINT32) hb_itemGetCLen(pValue));
            }
            if (isLargeField(field)) {
               cfl_list_add(largeFields, field);
            } else {
               sdb_table_addField(table, field);
            }
            switch (field->fieldType) {
               case SDB_FIELD_DATA_INDEX:
               case SDB_FIELD_INDEX:
                  sdb_stmt_getQueryValue(pStmt, 7, pClpName);
                  if (! sdb_util_itemIsEmpty(pClpName)) {
                     SDB_INDEXP index;
                     sdb_stmt_getQueryValue(pStmt, 8, pDBName);
                     sdb_stmt_getQueryValue(pStmt, 9, pExpr);
                     sdb_stmt_getQueryValue(pStmt, 11, pValue);
                     index = sdb_index_new(field,
                             hb_itemGetCPtr(pClpName), (CFL_UINT32) hb_itemGetCLen(pClpName),
                             ! sdb_util_itemIsEmpty(pDBName) ? hb_itemGetCPtr(pDBName) : NULL, (CFL_UINT32) hb_itemGetCLen(pDBName),
                             ! sdb_util_itemIsEmpty(pExpr) ? hb_itemGetCPtr(pExpr) : NULL, (CFL_UINT32) hb_itemGetCLen(pExpr),
                             ! sdb_util_itemIsEmpty(pValue) ? hb_itemGetCPtr(pValue) : NULL, (CFL_UINT32) hb_itemGetCLen(pValue));
                     sdb_stmt_getQueryValue(pStmt, 12, pValue);
                     if (! sdb_util_itemIsEmpty(pValue)) {
                        field->indexDescHint = cfl_str_newBufferLen(hb_itemGetCPtr(pValue), (CFL_UINT32) hb_itemGetCLen(pValue));
                     }
                     sdb_table_addIndex(table, index);
                  }
                  break;
               case SDB_FIELD_PK:
                  field->length = 20;
                  break;
            }
         }
         ulLen = cfl_list_length(largeFields);
         for (ulIndex = 0; ulIndex < ulLen; ulIndex++) {
            sdb_table_addField(table, (SDB_FIELDP) cfl_list_get(largeFields, ulIndex));
         }
         if (cfl_list_length(table->fields) > 0) {
            bResult = CFL_TRUE;
         } else {
            sdb_thread_setError(SDB_ERROR_TYPE_SDB, SDB_ERROR_CORRUPT_DICT, "No fields found in dictionary for table %s", cfl_str_getPtr(table->dbName));
         }
      }
      sdb_stmt_free(pStmt);
   }
   hb_itemRelease(pClpName);
   hb_itemRelease(pDBName);
   hb_itemRelease(pColType);
   hb_itemRelease(pDataType);
   hb_itemRelease(pColLen);
   hb_itemRelease(pColDec);
   hb_itemRelease(pExpr);
   hb_itemRelease(pValue);
   cfl_str_free(sql);
   cfl_list_free(largeFields);
   return bResult;
}

static CFL_BOOL existsDict(SDB_CONNECTIONP connection, SDB_SCHEMAP schema) {
   SDB_STATEMENTP pStmt;
   CFL_UINT64 ulAffectedRows = 0;
   CFL_BOOL fFound = CFL_FALSE;

   ENTER_FUNP(("product=%s", cfl_str_getPtr(connection->product->name)));
   if (cfl_str_bufferEqualsIgnoreCase(connection->product->name, "ORACLE")) {
      pStmt = sdb_connection_prepareStatementBuffer(connection,
              "select 1 from all_tables where table_name=:tablename and owner=:username");
      if (pStmt != NULL) {
         sdb_stmt_setChar(pStmt, "tablename", "SDB_TABLES", CFL_FALSE);
         sdb_stmt_setString(pStmt, "username", schema->name, CFL_FALSE);
         fFound = sdb_stmt_execute(pStmt, &ulAffectedRows) && sdb_stmt_fetchNext(pStmt, CFL_TRUE);
         sdb_stmt_free(pStmt);
      }
   } else if (cfl_str_bufferEqualsIgnoreCase(connection->product->name, "SQLITE")) {
      pStmt = sdb_connection_prepareStatementBuffer(connection,
              "select name from sqlite_master where type='table' and name=:tablename");
      if (pStmt != NULL) {
         sdb_stmt_setChar(pStmt, "tablename", "SDB_TABLES", CFL_FALSE);
         fFound = sdb_stmt_execute(pStmt, &ulAffectedRows) && sdb_stmt_fetchNext(pStmt, CFL_TRUE);
         sdb_stmt_free(pStmt);
      }
   }
   RETURN fFound;
}

static SDB_TABLEP getTable(SDB_CONNECTIONP connection, SDB_SCHEMAP schema, const char *tableName) {
   SDB_TABLEP table = NULL;
   CFL_STRP sql = cfl_str_new(160);
   SDB_STATEMENTP pStmt;
   CFL_UINT64 ulAffectedRows = 0;
   PHB_ITEM pItem = hb_itemNew(NULL);
   SDB_SCHEMAP schemaTab;

   CFL_STR_APPEND_CONST(sql, "select db_schema"        // 1
                                  ", db_tab_name"      // 2
                                  ", hint_asc"         // 3
                                  ", hint_desc"        // 4
                                  ", buffer_length"    // 5
                              " from ");
   cfl_str_appendStr(sql, schema->name);
   CFL_STR_APPEND_CONST(sql, ".sdb_tables where clp_tab_name=:tab");
   pStmt = sdb_connection_prepareStatement(connection, sql);
   if (pStmt != NULL) {
      sdb_stmt_setCharByPos(pStmt, 1, tableName, CFL_FALSE);
      if (sdb_stmt_execute(pStmt, &ulAffectedRows)) {
         if (sdb_stmt_fetchNext(pStmt, CFL_TRUE)) {
            sdb_stmt_getQueryValue(pStmt, 1, pItem);
            if (! sdb_util_itemIsEmpty(pItem)) {
               schemaTab = sdb_database_getCreateSchema(connection->database, hb_itemGetCPtr(pItem));
            } else {
               schemaTab = schema;
            }
            sdb_stmt_getQueryValue(pStmt, 2, pItem);
            if (! sdb_util_itemIsEmpty(pItem)) {
               table = sdb_table_new(schemaTab, tableName, (CFL_UINT32) strlen(tableName), hb_itemGetCPtr(pItem), (CFL_UINT32) hb_itemGetCLen(pItem));
            } else {
               table = sdb_table_new(schemaTab, tableName, (CFL_UINT32) strlen(tableName), NULL, 0);
            }
            sdb_stmt_getQueryValue(pStmt, 3, pItem);
            if (! sdb_util_itemIsEmpty(pItem)) {
               table->hintAsc = cfl_str_newBufferLen(hb_itemGetCPtr(pItem), (CFL_UINT32) hb_itemGetCLen(pItem));
            }
            sdb_stmt_getQueryValue(pStmt, 4, pItem);
            if (! sdb_util_itemIsEmpty(pItem)) {
               table->hintDesc = cfl_str_newBufferLen(hb_itemGetCPtr(pItem), (CFL_UINT32) hb_itemGetCLen(pItem));
            }
            sdb_stmt_getQueryValue(pStmt, 5, pItem);
            if (! sdb_util_itemIsEmpty(pItem)) {
               table->bufferFetchSize = (CFL_UINT16) hb_itemGetNI(pItem);
            }
            if (! queryColumns(connection, schema, table)) {
               sdb_table_free(table);
               table = NULL;
            }
         }
      }
      sdb_stmt_free(pStmt);
   }
   hb_itemRelease(pItem);
   cfl_str_free(sql);
   return table;
}

static CFL_BOOL insertTable(SDB_CONNECTIONP connection, SDB_SCHEMAP schema, SDB_TABLEP table) {
   CFL_STRP sql = cfl_str_new(128);
   SDB_STATEMENTP pStmt;
   CFL_UINT64 ulAffectedRows;
   CFL_BOOL fSuccess = CFL_FALSE;
   CFL_UINT32 ulLen;
   CFL_UINT32 ulIndex;

   CFL_STR_APPEND_CONST(sql, "insert into ");
   cfl_str_appendStr(sql, schema->name);
   CFL_STR_APPEND_CONST(sql, ".sdb_tables(clp_tab_name,db_schema,db_tab_name,hint_asc, hint_desc) values(:1,:2,:3,:4,:5)");
   pStmt = sdb_connection_prepareStatement(connection, sql);
   if (pStmt != NULL) {
      sdb_stmt_setStringByPos(pStmt, 1, table->clpName, CFL_FALSE);
      sdb_stmt_setStringByPos(pStmt, 2, table->dbSchema->name, CFL_FALSE);
      sdb_stmt_setStringByPos(pStmt, 3, table->dbName, CFL_FALSE);
      sdb_stmt_setStringByPos(pStmt, 4, table->hintAsc, CFL_FALSE);
      sdb_stmt_setStringByPos(pStmt, 5, table->hintDesc, CFL_FALSE);
      sdb_stmt_execute(pStmt, &ulAffectedRows);
      fSuccess = ulAffectedRows > 0;
      sdb_stmt_free(pStmt);
      if (fSuccess) {
         // insert sdb_columns
         cfl_str_clear(sql);
         CFL_STR_APPEND_CONST(sql, "insert into ");
         cfl_str_appendStr(sql, schema->name);
         CFL_STR_APPEND_CONST(sql, ".sdb_tables_columns(clp_tab_name"    // 1
                                                      ",clp_col_name"    // 2
                                                      ",db_col_name"     // 3
                                                      ",col_type"        // 4
                                                      ",clp_data_type"   // 5
                                                      ",clp_data_len"    // 6
                                                      ",clp_data_dec"    // 7
                                                      ",clp_index_name"  // 8
                                                      ",db_index_name"   // 9
                                                      ",clp_expression"  // 10
                                                      ",db_expression"   // 11
                                                      ",hint_asc"        // 12
                                                      ",hint_desc"       // 13
                                                      ",col_order "      // 14
                                                      ",context_order "  // 15
                                                      ",context_val) "   // 16
                                              "values(:1,:2,:3,:4,:5,:6,:7,:8,:9,:10,:11,:12,:13,:14,:15,:16)");

         pStmt = sdb_connection_prepareStatement(connection, sql);
         if (pStmt != NULL) {
            ulLen = cfl_list_length(table->fields);
            for (ulIndex = 0; ulIndex < ulLen && fSuccess; ulIndex++) {
               SDB_FIELDP field = (SDB_FIELDP) cfl_list_get(table->fields, ulIndex);
               if (field->fieldType != SDB_FIELD_ROWID) {
                  sdb_stmt_setStringByPos(pStmt, 1, table->clpName, CFL_FALSE);
                  sdb_stmt_setStringByPos(pStmt, 2, field->clpName, CFL_FALSE);
                  sdb_stmt_setStringByPos(pStmt, 3, field->dbName, CFL_FALSE);
                  sdb_stmt_setUInt8ByPos(pStmt, 4, field->fieldType, CFL_FALSE);
                  sdb_stmt_setUInt16ByPos(pStmt, 5, field->clpType, CFL_FALSE);
                  sdb_stmt_setUInt32ByPos(pStmt, 6, field->length, CFL_FALSE);
                  sdb_stmt_setUInt8ByPos(pStmt, 7, field->decimals, CFL_FALSE);
                  sdb_stmt_setStringByPos(pStmt, 8, field->clpIndexName, CFL_FALSE);
                  sdb_stmt_setStringByPos(pStmt, 9, field->dbIndexName, CFL_FALSE);
                  sdb_stmt_setStringByPos(pStmt, 10, field->clpExpression, CFL_FALSE);
                  sdb_stmt_setStringByPos(pStmt, 11, field->dbExpression, CFL_FALSE);
                  sdb_stmt_setStringByPos(pStmt, 12, field->indexAscHint, CFL_FALSE);
                  sdb_stmt_setStringByPos(pStmt, 13, field->indexDescHint, CFL_FALSE);
                  sdb_stmt_setUInt16ByPos(pStmt, 14, field->order, CFL_FALSE);
                  sdb_stmt_setUInt8ByPos(pStmt, 15, field->contextPos, CFL_FALSE);
                  sdb_stmt_setStringByPos(pStmt, 16, field->contextVal, CFL_FALSE);
                  fSuccess = sdb_stmt_execute(pStmt, &ulAffectedRows);
               }
            }
            sdb_stmt_free(pStmt);
         }
      }
   }
   cfl_str_free(sql);
   return fSuccess;
}


static CFL_BOOL deleteTable(SDB_CONNECTIONP connection, SDB_TABLEP table) {
   CFL_STRP sql = cfl_str_new(96);
   CFL_UINT64 ulAffectedRows;
   SDB_STATEMENTP pStmt;
   CFL_BOOL fSuccess = CFL_FALSE;

   CFL_STR_APPEND_CONST(sql, "delete from ");
   cfl_str_appendStr(sql, table->clpSchema->name);
   CFL_STR_APPEND_CONST(sql, ".sdb_tables_columns where clp_tab_name=:tab");
   pStmt = sdb_connection_prepareStatement(connection, sql);
   if (pStmt != NULL) {
      sdb_stmt_setString(pStmt, "tab", table->clpName, CFL_FALSE);
      fSuccess = sdb_stmt_execute(pStmt, &ulAffectedRows);
      sdb_stmt_free(pStmt);
   }
   cfl_str_clear(sql);
   CFL_STR_APPEND_CONST(sql, "delete from ");
   cfl_str_appendStr(sql, table->clpSchema->name);
   CFL_STR_APPEND_CONST(sql, ".sdb_tables where clp_tab_name=:tab");
   pStmt = sdb_connection_prepareStatement(connection, sql);
   cfl_str_free(sql);
   if (pStmt != NULL) {
      sdb_stmt_setString(pStmt, "tab", table->clpName, CFL_FALSE);
      fSuccess = sdb_stmt_execute(pStmt, &ulAffectedRows) && fSuccess;
      sdb_stmt_free(pStmt);
   }
   return fSuccess;
}

static CFL_BOOL existsTable(SDB_CONNECTIONP connection, SDB_SCHEMAP schema, const char *tableName) {
   CFL_UINT64 ulAffectedRows;
   CFL_STRP sql = cfl_str_new(96);
   SDB_STATEMENTP pStmt;
   CFL_BOOL fFound = CFL_FALSE;

   // Verificar no dicionario a existencia do indice
   CFL_STR_APPEND_CONST(sql, "select 'Y' from ");
   cfl_str_appendStr(sql, schema->name);
   CFL_STR_APPEND_CONST(sql, ".sdb_tables where clp_tab_name=:tab");
   pStmt = sdb_connection_prepareStatement(connection, sql);
   cfl_str_free(sql);
   if (pStmt != NULL) {
      sdb_stmt_setChar(pStmt, "tab", tableName, CFL_FALSE);
      sdb_stmt_execute(pStmt, &ulAffectedRows);
      fFound = sdb_stmt_fetchNext(pStmt, CFL_TRUE);
      sdb_stmt_free(pStmt);
   }
   return fFound;
}

static CFL_BOOL insertColumn(SDB_CONNECTIONP connection, SDB_TABLEP table, SDB_FIELDP field) {
   CFL_STRP sql;
   SDB_STATEMENTP pStmt;
   CFL_UINT64 ulAffectedRows;
   CFL_BOOL fSuccess = CFL_FALSE;

   if (field->fieldType != SDB_FIELD_ROWID) {
      sql = cfl_str_new(256);
      CFL_STR_APPEND_CONST(sql, "insert into ");
      cfl_str_appendStr(sql, table->clpSchema->name);
      CFL_STR_APPEND_CONST(sql, ".sdb_tables_columns(clp_tab_name"    // 1
                                                   ",clp_col_name"    // 2
                                                   ",db_col_name"     // 3
                                                   ",col_type"        // 4
                                                   ",clp_data_type"   // 5
                                                   ",clp_data_len"    // 6
                                                   ",clp_data_dec"    // 7
                                                   ",clp_index_name"  // 8
                                                   ",db_index_name"   // 9
                                                   ",clp_expression"  // 10
                                                   ",db_expression"   // 11
                                                   ",hint_asc"        // 12
                                                   ",hint_desc"       // 13
                                                   ",col_order "      // 14
                                                   ",context_order"   // 15
                                                   ",context_val) "   // 16
                                           "values(:1,:2,:3,:4,:5,:6,:7,:8,:9,:10,:11,:12,:13,:14,:15,:16)");

      pStmt = sdb_connection_prepareStatement(connection, sql);
      cfl_str_free(sql);
      if (pStmt != NULL) {
         sdb_stmt_setStringByPos(pStmt, 1, table->clpName, CFL_FALSE);
         sdb_stmt_setStringByPos(pStmt, 2, field->clpName, CFL_FALSE);
         sdb_stmt_setStringByPos(pStmt, 3, field->dbName, CFL_FALSE);
         sdb_stmt_setUInt8ByPos(pStmt, 4, field->fieldType, CFL_FALSE);
         sdb_stmt_setUInt16ByPos(pStmt, 5, field->clpType, CFL_FALSE);
         sdb_stmt_setUInt32ByPos(pStmt, 6, field->length, CFL_FALSE);
         sdb_stmt_setUInt8ByPos(pStmt, 7, field->decimals, CFL_FALSE);
         sdb_stmt_setStringByPos(pStmt, 8, field->clpIndexName, CFL_FALSE);
         sdb_stmt_setStringByPos(pStmt, 9, field->dbIndexName, CFL_FALSE);
         sdb_stmt_setStringByPos(pStmt, 10, field->clpExpression, CFL_FALSE);
         sdb_stmt_setStringByPos(pStmt, 11, field->dbExpression, CFL_FALSE);
         sdb_stmt_setStringByPos(pStmt, 12, field->indexAscHint, CFL_FALSE);
         sdb_stmt_setStringByPos(pStmt, 13, field->indexDescHint, CFL_FALSE);
         sdb_stmt_setUInt16ByPos(pStmt, 14, field->order, CFL_FALSE);
         sdb_stmt_setUInt8ByPos(pStmt, 15, field->contextPos, CFL_FALSE);
         sdb_stmt_setStringByPos(pStmt, 16, field->contextVal, CFL_FALSE);
         fSuccess = sdb_stmt_execute(pStmt, &ulAffectedRows);
         sdb_stmt_free(pStmt);
      }
   }
   return fSuccess;
}

static CFL_BOOL deleteColumn(SDB_CONNECTIONP connection, SDB_TABLEP table, SDB_FIELDP field) {
   CFL_STRP sql = cfl_str_new(128);
   CFL_UINT64 ulAffectedRows = 0;
   SDB_STATEMENTP pStmt;

   cfl_str_append(sql, "delete from ", cfl_str_getPtr(table->clpSchema->name), ".sdb_tables_columns"
                           " where clp_tab_name=:tab and clp_col_name=:col", NULL);
   pStmt = sdb_connection_prepareStatement(connection, sql);
   cfl_str_free(sql);
   if (pStmt != NULL) {
      sdb_stmt_setString(pStmt, "tab", table->clpName, CFL_FALSE);
      sdb_stmt_setString(pStmt, "col", field->clpName, CFL_FALSE);
      sdb_stmt_execute(pStmt, &ulAffectedRows);
      sdb_stmt_free(pStmt);
   }
   return ulAffectedRows > 0;
}

static CFL_BOOL updateColumn(SDB_CONNECTIONP connection, SDB_TABLEP table, SDB_FIELDP field) {
   CFL_STRP sql;
   CFL_UINT64 ulAffectedRows = 0;
   SDB_STATEMENTP pStmt;

   if (field->fieldType != SDB_FIELD_ROWID) {
      sql = cfl_str_new(256);
      cfl_str_append(sql, "update ", cfl_str_getPtr(table->clpSchema->name), ".sdb_tables_columns"
              " set col_type = :1"
                 ", clp_data_type = :2"
                 ", clp_data_len = :3"
                 ", clp_data_dec = :4"
                 ", clp_index_name = :5"
                 ", db_index_name = :6"
                 ", clp_expression = :7"
                 ", db_expression = :8"
                 ", set_mode = :9"
                 ", hint_asc = :10"
                 ", hint_desc = :11"
                 ", col_order = :12"
                 ", context_order = :13"
                 ", context_val = :14"
            " where clp_tab_name=:15"
              " and clp_col_name=:16", NULL);
      pStmt = sdb_connection_prepareStatement(connection, sql);
      cfl_str_free(sql);
      if (pStmt != NULL) {
         sdb_stmt_setUInt8ByPos(pStmt, 1, field->fieldType, CFL_FALSE);
         sdb_stmt_setUInt16ByPos(pStmt, 2, field->clpType, CFL_FALSE);
         sdb_stmt_setUInt32ByPos(pStmt, 3, field->length, CFL_FALSE);
         sdb_stmt_setUInt8ByPos(pStmt, 4, field->decimals, CFL_FALSE);
         sdb_stmt_setStringByPos(pStmt, 5, field->clpIndexName, CFL_FALSE);
         sdb_stmt_setStringByPos(pStmt, 6, field->dbIndexName, CFL_FALSE);
         sdb_stmt_setStringByPos(pStmt, 7, field->clpExpression, CFL_FALSE);
         sdb_stmt_setStringByPos(pStmt, 8, field->dbExpression, CFL_FALSE);
         sdb_stmt_setUInt8ByPos(pStmt, 9, field->setMode, CFL_FALSE);
         sdb_stmt_setStringByPos(pStmt, 10, field->indexAscHint, CFL_FALSE);
         sdb_stmt_setStringByPos(pStmt, 11, field->indexDescHint, CFL_FALSE);
         sdb_stmt_setUInt16ByPos(pStmt, 12, field->order, CFL_FALSE);
         sdb_stmt_setUInt8ByPos(pStmt, 13, field->contextPos, CFL_FALSE);
         sdb_stmt_setStringByPos(pStmt, 14, field->contextVal, CFL_FALSE);
         sdb_stmt_setStringByPos(pStmt, 15, table->clpName, CFL_FALSE);
         sdb_stmt_setStringByPos(pStmt, 16, field->clpName, CFL_FALSE);
         sdb_stmt_execute(pStmt, &ulAffectedRows);
         sdb_stmt_free(pStmt);
      }
   }
   return ulAffectedRows > 0;
}

static CFL_BOOL existsIndex(SDB_CONNECTIONP connection, SDB_SCHEMAP schema, const char *tableName, const char *indexName) {
   CFL_STRP sql = cfl_str_new(128);
   SDB_STATEMENTP pStmt;
   CFL_BOOL fFound = CFL_FALSE;
   CFL_UINT64 ulAffectedRows = 0;

   // Verificar no dicionario a existencia do indice
   CFL_STR_APPEND_CONST(sql, "select 'Y' from ");
   cfl_str_appendStr(sql, schema->name);
   CFL_STR_APPEND_CONST(sql, ".sdb_tables_columns where clp_tab_name=:tab and clp_index_name=:idx");
   pStmt = sdb_connection_prepareStatement(connection, sql);
   cfl_str_free(sql);
   if (pStmt != NULL) {
      sdb_stmt_setChar(pStmt, "tab", tableName, CFL_FALSE);
      sdb_stmt_setChar(pStmt, "idx", indexName, CFL_FALSE);
      sdb_stmt_execute(pStmt, &ulAffectedRows);
      fFound = sdb_stmt_fetchNext(pStmt, CFL_TRUE);
      sdb_stmt_free(pStmt);
   }
   return fFound;
}

static CFL_BOOL createObjects(SDB_CONNECTIONP connection) {
   CFL_UINT64 ulAffectedRows = 0;
   CFL_STRP sql;

   if (! sdb_connection_executeImmediate(connection,
           "CREATE OR REPLACE FUNCTION FNC_SDB_LOCKID(lockId IN VARCHAR2, lckMode IN NUMBER) RETURN NUMBER \n"
           "IS PRAGMA AUTONOMOUS_TRANSACTION;\n"
           "   lock_handle VARCHAR2(128);\n"
           "   res NUMBER;\n"
           "BEGIN\n"
           "   DBMS_LOCK.ALLOCATE_UNIQUE(lockId, lock_handle);\n"
           "   res := DBMS_LOCK.REQUEST(lockhandle=>lock_handle, lockmode=>lckMode, timeout=>0, release_on_commit=>FALSE);\n"
           "   IF res = 4 THEN\n"
           "      res := DBMS_LOCK.CONVERT(lockhandle=>lock_handle, lockmode=>lckMode, timeout=>0);\n"
           "   END IF;\n"
           "   RETURN res;\n"
           "END FNC_SDB_LOCKID;", &ulAffectedRows)) {
      return CFL_FALSE;
   }
   if (! sdb_connection_executeImmediate(connection,
           "CREATE OR REPLACE FUNCTION FNC_SDB_UNLOCKID(lockId IN VARCHAR2) RETURN NUMBER \n"
           "IS PRAGMA AUTONOMOUS_TRANSACTION;\n"
           "   lock_handle VARCHAR2(128);\n"
           "BEGIN\n"
           "   DBMS_LOCK.ALLOCATE_UNIQUE(lockId, lock_handle);\n"
           "   RETURN DBMS_LOCK.RELEASE(lockhandle=>lock_handle);\n"
           "END FNC_SDB_UNLOCKID;", &ulAffectedRows)) {
      return CFL_FALSE;
   }
   sql = cfl_str_new(512);
   cfl_str_append(sql, "CREATE TABLE ", cfl_str_getPtr(connection->schema->name), ".sdb_tables("
           "clp_tab_name VARCHAR2(30) NOT NULL," // table clipper name
           "db_schema VARCHAR2(30) NOT NULL," // table schema name
           "db_tab_name VARCHAR2(30) NOT NULL," // table database name
           "hint_asc VARCHAR2(150)," // Hint to use in queries in ascending order
           "hint_desc VARCHAR2(150)," // Hint to use in queries in descending order
           "next_recno NUMBER(*) DEFAULT 1 NOT NULL," // Next recno number
           "recno_cache_size NUMBER(6) DEFAULT 20 NOT NULL," // Number of recnos to cache in client
           "buffer_length NUMBER(5) DEFAULT 0 NOT NULL," // Fetch buffer length for table
           "CONSTRAINT sdb_tables_pk PRIMARY KEY (clp_tab_name)) INITRANS 100", NULL);
   if (! sdb_connection_executeImmediate(connection, cfl_str_getPtr(sql), &ulAffectedRows)) {
      cfl_str_free(sql);
      return CFL_FALSE;
   }
   cfl_str_clear(sql);
   cfl_str_append(sql, "CREATE TABLE ", cfl_str_getPtr(connection->schema->name), ".sdb_tables_columns("
           "clp_tab_name VARCHAR2(30) NOT NULL," // 1
           "clp_col_name VARCHAR2(30) NOT NULL," // 2
           "db_col_name VARCHAR2(30) NOT NULL," // 3
           "col_type NUMBER(1) NOT NULL," // 4
           "clp_data_type NUMBER(2) NOT NULL," // 5
           "clp_data_len NUMBER(5)," // 6
           "clp_data_dec NUMBER(2)," // 7
           "clp_index_name VARCHAR2(30)," // 8
           "db_index_name VARCHAR2(30)," // 9
           "clp_expression VARCHAR2(250)," // 10
           "db_expression  VARCHAR2(250)," // 11
           "set_mode  NUMBER(3)," // 12
           "hint_asc VARCHAR2(150)," // 13
           "hint_desc VARCHAR2(150)," // 14
           "col_order NUMBER(5)," // 15
           "context_order NUMBER(5)," // 16
           "context_val VARCHAR2(250)," // 17
           "CONSTRAINT sdb_tables_columns_pk PRIMARY KEY (clp_tab_name, clp_col_name))", NULL);
   if (! sdb_connection_executeImmediate(connection, cfl_str_getPtr(sql), &ulAffectedRows)) {
      cfl_str_free(sql);
      return CFL_FALSE;
   }
   cfl_str_clear(sql);
   cfl_str_append(sql, "CREATE SEQUENCE ", cfl_str_getPtr(connection->schema->name),
           ".SEQ_SDB_RECNO INCREMENT BY 1 START WITH 1 CACHE 10", NULL);
   if (! sdb_connection_executeImmediate(connection, cfl_str_getPtr(sql), &ulAffectedRows)) {
      cfl_str_free(sql);
      return CFL_FALSE;
   }
   if (! sdb_connection_executeImmediate(connection,
           "CREATE OR REPLACE FUNCTION FNC_SDB_NEXTRECNOCACHE(pi_Schema IN VARCHAR2, pi_tableName IN VARCHAR2, pi_cacheSize IN NUMBER) RETURN NUMBER \n"
           "IS PRAGMA AUTONOMOUS_TRANSACTION;\n"
           "   vs_NextRecno NUMBER;\n"
           "BEGIN\n"
           "   EXECUTE IMMEDIATE 'UPDATE ' || pi_Schema || '.sdb_tables\n"
           "                         SET next_recno = next_recno + :1\n"
           "                       WHERE clp_tab_name=:2 RETURNING next_recno INTO :3'\n"
           "               USING pi_cacheSize, pi_tableName\n"
           "      RETURNING INTO vs_nextRecno;\n"
           "   COMMIT;\n"
           "   RETURN vs_NextRecno;\n"
           "END FNC_SDB_NEXTRECNOCACHE;", &ulAffectedRows)) {
      cfl_str_free(sql);
      return CFL_FALSE;
   }
   if (! sdb_connection_executeImmediate(connection,
           "CREATE OR REPLACE PROCEDURE PRC_SDB_UPDATENEXTRECNO(pi_recnoName IN VARCHAR2, pi_Schema IN VARCHAR2, pi_clpTabName IN VARCHAR2, pi_dbTabName IN VARCHAR2) \n"
           "IS PRAGMA AUTONOMOUS_TRANSACTION;\n"
           "   VS_NEXTRECNO NUMBER;\n"
           "BEGIN\n"
           "   EXECUTE IMMEDIATE 'SELECT MAX(' || pi_recnoName || ') FROM ' || pi_Schema || '.' || pi_dbTabName INTO VS_NEXTRECNO;\n"
           "   EXECUTE IMMEDIATE 'UPDATE ' || pi_Schema || '.sdb_tables SET next_recno = :1 + 1\n"
           "                       WHERE clp_tab_name=:2'\n"
           "               USING VS_NEXTRECNO, pi_clpTabName;\n"
           "   COMMIT;\n"
           "END PRC_SDB_UPDATENEXTRECNO;", &ulAffectedRows)) {
      cfl_str_free(sql);
      return CFL_FALSE;
   }
   cfl_str_free(sql);
   return CFL_TRUE;
}

static CFL_BOOL refreshMaxRecno(SDB_CONNECTIONP conn, SDB_TABLEP table, CFL_BOOL updateRecno){
   CFL_UINT32 errorCount = sdb_thread_errorCount();
   CFL_SQL_BUILDERP b = conn->dbAPI->sqlBuilder();
   CFL_SQL_UPDATEP update;
   CFL_SQL_BLOCKP block;
   CFL_STRP sql;
   CFL_STR tablesDicTab = CFL_STR_CONST("sdb_tables");
   CFL_STR autonomousPragma = CFL_STR_CONST("autonomous_transaction");
   SDB_STATEMENTP pStmt;

   update = b->update();
   update->table(update, b->quali_id(table->dbSchema->name, &tablesDicTab, NULL));
   if (updateRecno) {
      CFL_SQL_QUERYP query = b->query();
      query->select(query, b->c_fun("max", b->id(table->pkField->dbName), NULL), NULL);
      query->from(query, b->quali_id(table->dbSchema->name, table->dbName, NULL), NULL);
      update->set(update, b->c_id("next_recno"), b->parentheses((CFL_SQLP) query));
   } else {
      update->set(update, b->c_id("next_recno"), b->plus(b->c_id("next_recno"), b->c_id("recno_cache_size")));
   }
   update->where(update, b->equal(b->c_id("clp_tab_name"), b->c_param("tab")));
   update->returning(update, b->c_id("next_recno"), b->c_param("rec"));
   update->returning(update, b->c_id("recno_cache_size"), b->c_param("cache"));

   block = b->block();
   block->pragma(block, &autonomousPragma);
   block->statement(block, (CFL_SQLP) update);
   block->statement(block, b->commit());
   sql = cfl_str_new(2048);
   CFL_SQL_TO_STRING(block, sql);
   pStmt = sdb_connection_prepareStatement(conn, sql);
   cfl_str_free(sql);
   CFL_SQL_FREE(block);
   if (pStmt != NULL) {
      CFL_UINT64 ulAffectedRows;
      sdb_stmt_setString(pStmt, "tab", table->clpName, CFL_FALSE);
      sdb_stmt_setInt64(pStmt, "rec", 0, CFL_TRUE);
      sdb_stmt_setInt64(pStmt, "cache", 0, CFL_TRUE);
      if (sdb_stmt_execute(pStmt, &ulAffectedRows) && sdb_stmt_fetchNext(pStmt, CFL_TRUE)) {
         sdb_table_cacheNextPK(table, sdb_stmt_getInt64(pStmt, "rec"), sdb_stmt_getInt64(pStmt, "cache"));
      }
      sdb_stmt_free(pStmt);
   }
   return ! sdb_thread_hasNewErrors(errorCount);
}

static SDB_RECNO sdbMaxRecno(SDB_CONNECTIONP conn, SDB_TABLEP table, CFL_BOOL updateRecno) {
   SDB_LOG_DEBUG(("sdbMaxRecno: table=%s next=%lld max=%lld", cfl_str_getPtr(table->clpName), table->nextRecno, table->maxRecno));
   if (IS_SERVER_SET_INSERT(table->pkField)) {
      sdb_table_setNextPK(table, 1);
      sdb_table_setMaxPK(table, SDB_MAX_RECNO);
   } else if ((! sdb_table_pkInCache(table) || updateRecno) && ! refreshMaxRecno(conn, table, updateRecno)) {
      return SDB_MAX_RECNO;
   }
   return sdb_table_maxPK(table);
}

static SDB_RECNO sdbNextRecno(SDB_CONNECTIONP conn, SDB_TABLEP table, CFL_BOOL updateRecno) {
   SDB_LOG_DEBUG(("sdbNextRecno: table=%s next=%lld max=%lld", cfl_str_getPtr(table->clpName), table->nextRecno, table->maxRecno));
   if (IS_SERVER_SET_INSERT(table->pkField)) {
      sdb_table_setNextPK(table, 0);
      sdb_table_setMaxPK(table, SDB_MAX_RECNO);
   } else if ((! sdb_table_pkInCache(table) || updateRecno) && ! refreshMaxRecno(conn, table, updateRecno)) {
      return 0;
   }
   return sdb_table_nextPK(table);
}

static void freeSDBDict(SDB_DICTIONARYP dict) {
   HB_SYMBOL_UNUSED(dict);
}

SDB_DICTIONARYP sdb_dict_getDefault(void) {
   s_SdbDict.name = "SDB";
   s_SdbDict.existsDict = existsDict;
   s_SdbDict.getTable = getTable;
   s_SdbDict.insertTable = insertTable;
   s_SdbDict.deleteTable = deleteTable;
   s_SdbDict.existsTable = existsTable;
   s_SdbDict.insertColumn = insertColumn;
   s_SdbDict.deleteColumn = deleteColumn;
   s_SdbDict.updateColumn = updateColumn;
   s_SdbDict.existsIndex = existsIndex;
   s_SdbDict.createObjects = createObjects;
   s_SdbDict.nextRecno = sdbNextRecno;
   s_SdbDict.maxRecno = sdbMaxRecno;
   s_SdbDict.freeDict = freeSDBDict;
   return &s_SdbDict;
}

void sdb_dict_registerDictionary(SDB_DICTIONARYP dict) {
   if (s_dictCount < 8) {
      s_dictionaries[s_dictCount++] = dict;
      SDB_LOG_DEBUG(("Dictionary '%s' registered", dict->name));
   } else {
      sdb_thread_setError(SDB_ERROR_TYPE_SDB, SDB_ERROR_MAX_DICT, "Maximum number of dictionaries already registered.");
   }
}

SDB_DICTIONARYP sdb_dict_getDictionary(SDB_CONNECTIONP connection, SDB_SCHEMAP schema) {
   CFL_UINT8 i;
   for (i = 0; i < s_dictCount; i++) {
      if (s_dictionaries[i]->existsDict(connection, schema)) {
         return s_dictionaries[i];
      }
   }
   return NULL;
}
