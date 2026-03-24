#include "hbapi.h"
#include "hbapierr.h"
#include "hbapilng.h"

#include "sdb_error.h"
#include "sdb_util.h"
#include "sdb_log.h"

const char * sdb_error_getDescription(CFL_UINT16 uiGenCode, CFL_UINT16 uiSubCode) {
   switch (uiSubCode) {
      case SDB_ERROR_INVALID_DATATYPE:
         return "Invalid datatype.";
      case SDB_ERROR_INVALID_FIELD_TYPE:
         return "Invalid field type";
      case SDB_ERROR_PRODUCT_NOT_FOUND:
         return "Product not registered.";
      case SDB_ERROR_TRANSACTION:
         return "Transaction error.";
      case SDB_ERROR_INVALID_OPERATION:
         return "Invalid operation.";
      case SDB_ERROR_INVALID_FIELD:
         return "Invalid field";
      case SDB_ERROR_READ_RECORD:
         return "Error reading data from resultset.";
      case SDB_ERROR_SET_FIELD_MEM:
         return "Error assigning value to internal record.";
      case SDB_ERROR_GET_FIELD_MEM:
         return "Error getting value from internal record.";
      case SDB_ERROR_CORRUPT_DICT:
         return "Dictionary corruption.";
      case SDB_ERROR_UNSUPPORTED:
         return "Unsupported operation.";
      case SDB_ERROR_NOT_INDEXED:
         return "Table is not indexed.";
      case SDB_ERROR_READONLY:
         return "Workarea in read only mode.";
      case SDB_ERROR_UNLOCKED:
         return "Workarea is not locked.";
      case SDB_ERROR_WRITE:
         return "Error writing in table.";
      case SDB_ERROR_SHARED:
         return "Workarea in shared mode.";
      case SDB_ERROR_CREATE_TABLE:
         return "Error trying create table.";
      case SDB_ERROR_CREATE_INDEX:
         return "Error trying create index.";
      case SDB_ERROR_INVALID_KEY:
         return "Invalid key.";
      case SDB_ERROR_OPEN:
         return "Error trying open table.";
      case SDB_ERROR_INVALID_ARGUMENT:
         return "Invalid argument.";
      case SDB_ERROR_EXECUTE_PROCEDURE:
         return "Error executing stored procedure.";
      case SDB_ERROR_EXECUTE_SQL:
         return "Error executing sql.";
      case SDB_ERROR_PREPARING_STMT:
         return "Error preparing statement.";
      case SDB_ERROR_CLOSING_STMT:
         return "Error closing statement";
      case SDB_ERROR_EXECUTING_STMT:
         return "Error executing statement.";
      case SDB_ERROR_BEGIN_TRANS:
         return "Error begining transaction.";
      case SDB_ERROR_COMMIT_TRANS:
         return "Error commiting transaction.";
      case SDB_ERROR_ROLLBACK_TRANS:
         return "Error rollbacking transaction.";
      case SDB_ERROR_NO_TABLE:
         return "Table not found.";
   }
   return hb_langDGetErrorDesc(uiGenCode);
}

void sdb_error_init(SDB_ERRORP error) {
   error->type = SDB_ERROR_TYPE_NO_ERROR;
   error->code = SDB_ERROR_NONE;
   error->message = cfl_str_new(128);
   error->errorCount = 0;
   error->isAllocated = CFL_FALSE;
}

SDB_ERRORP sdb_error_new(void) {
   SDB_ERRORP error = SDB_MEM_ALLOC(sizeof(SDB_ERROR));
   sdb_error_init(error);
   error->isAllocated = CFL_TRUE;
   return error;
}

void sdb_error_clear(SDB_ERRORP error) {
   error->type = SDB_ERROR_TYPE_NO_ERROR;
   error->code = SDB_ERROR_NONE;
   error->errorCount = 0;
   cfl_str_clear(error->message);
}

void sdb_error_free(SDB_ERRORP error) {
   if (error) {
      cfl_str_free(error->message);
      if (error->isAllocated) {
         SDB_MEM_FREE(error);
      }
   }
}
