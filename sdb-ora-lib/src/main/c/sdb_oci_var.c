// #include "hbapi.h"
// #ifdef __XHB__
// #include "hbfast.h"
// #endif

#include <stdlib.h>

#if defined(_SDB_OS_WIN_)
#include <string.h>
#else
#include <strings.h>
#endif

#include "cfl_array.h"
#include "cfl_list.h"
#include "sdb_defs.h"
#include "sdb_error.h"
#include "sdb_log.h"
#include "sdb_oci_lob.h"
#include "sdb_oci_rowid.h"
#include "sdb_oci_stmt.h"
#include "sdb_oci_var.h"
#include "sdb_statement.h"
#include "sdb_thread.h"

#if defined(_SDB_OS_WIN_)
#define strtoint64 _atoi64
#define strCmpIgnoreCase _strnicmp
#else
#define strtoint64 atoll
#define strCmpIgnoreCase strncasecmp
#endif

#define MAX_STATIC_BUFFER_SIZE 32767

#define SDB_MAX_INT_PRECISION 18

#define DATA_SIZE(s, m) (s * m)
#define INDICATOR_SIZE(m) (sizeof(CFL_INT16) * m)
#define RETURN_SIZE(m) (sizeof(CFL_UINT16) * m)
#define DESCRIPTOR_SIZE(t, m) (DESCRIPTOR_TYPE_SIZE(t) * m)
#define LEN_SIZE(t, m, bDyn) ((!bDyn && IS_CHAR_DATATYPE(t) ? sizeof(CFL_UINT32) : 0) * m)

/* data[]  + indicator[] + returnCodes[] + (data len[] | descriptors []) */
#define BUFFERS_SIZE(t, s, m, bDyn)                                                                                                \
   (DATA_SIZE(s, m) + INDICATOR_SIZE(m) + RETURN_SIZE(m) + DESCRIPTOR_SIZE(t, m) + LEN_SIZE(t, m, bDyn))

#if defined(_WIN32) || defined(__i386__)
#define CAST_INDEX(value) ((CFL_UINT32)value)
#else
#define CAST_INDEX(value) (value)
#endif

#define INDICATOR_BUFFER(v) ((CFL_INT16 *)&(v)->data[CAST_INDEX(DATA_SIZE((v)->itemSize, (v)->maxItems))])

#define RETURN_BUFFER(v)                                                                                                           \
   ((CFL_UINT16 *)&((v)->data[CAST_INDEX(DATA_SIZE((v)->itemSize, (v)->maxItems) + INDICATOR_SIZE((v)->maxItems))]))

#define LEN_BUFFER(v)                                                                                                              \
   &((v)->data[CAST_INDEX(DATA_SIZE((v)->itemSize, (v)->maxItems) + INDICATOR_SIZE((v)->maxItems) + RETURN_SIZE((v)->maxItems))])

#define DESCRIPTOR_BUFFER(v)                                                                                                       \
   ((void **)&((v)->data[CAST_INDEX(DATA_SIZE((v)->itemSize, (v)->maxItems) + INDICATOR_SIZE((v)->maxItems) +                      \
                                    RETURN_SIZE((v)->maxItems))]))

#define INITIAL_DYN_BUFFER_SIZE 4096

static CFL_BOOL dataTypeAndSize(CFL_UINT16 *dataType, CFL_INT64 *itemSize, CFL_UINT32 maxItems, CFL_BOOL bBindOut) {
   CFL_BOOL bDynamic;
   switch (*dataType) {
   case SDB_SQLT_AFC:
   case SDB_SQLT_AVC:
   case SDB_SQLT_CHR:
   case SDB_SQLT_STR:
   case SDB_SQLT_VCS:
      *dataType = SDB_SQLT_CHR;
      bDynamic = maxItems == 1 && (bBindOut || *itemSize > MAX_STATIC_BUFFER_SIZE) ? CFL_TRUE : CFL_FALSE;
      *itemSize = bDynamic ? sizeof(void *) : ((*itemSize) * sizeof(char));
      break;

   case SDB_SQLT_INT:
   case SDB_SQLT_UIN:
      *dataType = SDB_SQLT_INT;
      *itemSize = sizeof(CFL_INT64);
      bDynamic = CFL_FALSE;
      break;

   case SDB_SQLT_FLT:
   case SDB_SQLT_BFLOAT:
   case SDB_SQLT_IBFLOAT:
   case SDB_SQLT_PDN:
   case SDB_SQLT_BDOUBLE:
   case SDB_SQLT_IBDOUBLE:
      *dataType = SDB_SQLT_BDOUBLE;
      *itemSize = sizeof(double);
      bDynamic = CFL_FALSE;
      break;

   case SDB_SQLT_NUM:
   case SDB_SQLT_VNU:
      //*dataType = SDB_SQLT_NUM;
      //*dataType = SDB_SQLT_VNU;
      *itemSize = sizeof(SDB_OCI_NUM);
      bDynamic = CFL_FALSE;
      break;

   case SDB_SQLT_TIME:
   case SDB_SQLT_TIME_TZ:
   case SDB_SQLT_TIMESTAMP:
   case SDB_SQLT_TIMESTAMP_TZ:
   case SDB_SQLT_TIMESTAMP_LTZ:
   case SDB_SQLT_DATE:
   case SDB_SQLT_DAT:
   case SDB_SQLT_ODT:
      *dataType = SDB_SQLT_ODT;
      *itemSize = sizeof(SDB_OCI_DATE);
      bDynamic = CFL_FALSE;
      break;

   case SDB_SQLT_LVC:
   case SDB_SQLT_LVB:
   case SDB_SQLT_LNG:
   case SDB_SQLT_BIN:
   case SDB_SQLT_LBI:
      *dataType = SDB_SQLT_BIN;
      *itemSize = sizeof(void *);
      bDynamic = CFL_TRUE;
      break;

   case SDB_SQLT_RDD:
   case SDB_SQLT_CLOB:
   case SDB_SQLT_BLOB:
   case SDB_SQLT_RSET:
      *itemSize = sizeof(void *);
      bDynamic = CFL_FALSE;
      break;

   case SDB_SQLT_BOL:
      *itemSize = sizeof(int);
      bDynamic = CFL_FALSE;
      break;

   case SDB_SQLT_CFILEE:
   case SDB_SQLT_BFILEE:
   default:
      sdb_thread_setError(SDB_ERROR_TYPE_DB_ERROR, SDB_OCI_ERROR_INVALID_DATATYPE, "Invalid datatype for variable: %d", *dataType);
      *dataType = 0;
      *itemSize = 0;
      bDynamic = CFL_FALSE;
      break;
   }
   return bDynamic;
}

static void prepareDescriptor(SDB_OCI_VAR *var, CFL_UINT32 index) {
   if (var->descriptors != NULL) {
      switch (var->dataType) {
      case SDB_SQLT_RDD:
         if (var->rowids[index] != NULL) {
            sdb_oci_rowid_free(var->rowids[index]);
         }
         var->rowids[index] = sdb_oci_rowid_new(var->conn);
         var->pointerData[index] = var->rowids[index]->handle;
         break;
      case SDB_SQLT_CLOB:
      case SDB_SQLT_BLOB:
         if (var->lobs[index] != NULL) {
            sdb_oci_lob_free(var->lobs[index]);
         }
         var->lobs[index] = sdb_oci_lob_new(var->conn, var->dataType, CFL_FALSE);
         var->pointerData[index] = var->lobs[index]->handle;
         break;
      case SDB_SQLT_RSET:
         if (var->statements[index] != NULL) {
            sdb_oci_stmt_free(var->statements[index]);
         }
         var->statements[index] = sdb_oci_stmt_new(var->conn, NULL);
         var->pointerData[index] = var->statements[index]->handle;
         break;
      }
   }
}

static void prepareBuffer(SDB_OCI_VAR *var, CFL_UINT32 index) {
   if (var->bufferData[index] != NULL) {
      var->bufferData[index]->length = 0;
      var->bufferData[index]->read = 0;
   }
}

static void prepareDataAndDescriptors(SDB_OCI_VAR *var) {
   CFL_UINT32 i;
   if (HAS_DESCRIPTOR(var->dataType)) {
      var->descriptors = DESCRIPTOR_BUFFER(var);
      for (i = 0; i < var->maxItems; i++) {
         prepareDescriptor(var, i);
      }
   } else if (var->dataType == SDB_SQLT_NUM || var->dataType == SDB_SQLT_VNU) {
      SDB_OCI_SYMBOLS *ociSymbols = sdb_oci_getSymbols();
      void *errorHandle = sdb_oci_getErrorHandle();
      for (i = 0; i < var->maxItems; i++) {
         ociSymbols->OCINumberSetZero(errorHandle, (void *)&var->numData[i]);
      }
      var->descriptors = NULL;
   } else {
      var->descriptors = NULL;
   }
   var->needPrefetch = CFL_FALSE;
}

static SDB_OCI_VAR *var_new(SDB_OCI_CONNECTION *conn, CFL_UINT16 dataType, CFL_INT64 itemSize, CFL_UINT32 maxItems,
                            CFL_BOOL bBindOut, CFL_BOOL b32BitsLen) {
   SDB_OCI_VAR *var = NULL;
   CFL_UINT64 dataSize;
   size_t varSize;
   CFL_BOOL bDynamic;

   SDB_LOG_TRACE(("var_new. dataType=%hu maxItems=%lu out=%hhu b32BitsLen=%hhu "
                  "itemSize=%lld",
                  dataType, maxItems, bBindOut, b32BitsLen, itemSize));
   if (maxItems == 0) {
      maxItems = 1;
   }
   bDynamic = dataTypeAndSize(&dataType, &itemSize, maxItems, bBindOut);
   if (dataType != 0) {
      dataSize = (CFL_UINT64)BUFFERS_SIZE(dataType, itemSize, maxItems, bDynamic);
      varSize = sizeof(SDB_OCI_VAR) + (maxItems > 1 ? 0 : dataSize);
      var = SDB_MEM_ALLOC(varSize);
      if (var != NULL) {
         memset(var, 0, varSize);
         var->conn = conn;
         var->dataType = dataType;
         var->itemSize = itemSize;
         var->maxItems = maxItems;
         var->isDynamic = bDynamic;
         var->isArray = CFL_FALSE;
         var->is32BitLen = b32BitsLen;

         /* if max items equals 1, then buffers were allocate along the variable */
         if (maxItems == 1) {
            var->data = var->buffer;
         } else {
            var->data = SDB_MEM_ALLOC(dataSize);
            if (var->data == NULL) {
               SDB_MEM_FREE(var);
               return NULL;
            }
         }
         var->indicator = INDICATOR_BUFFER(var);
         if (!var->isDynamic) {
            var->returnCodes = RETURN_BUFFER(var);
            if (IS_CHAR_DATATYPE(var->dataType)) {
               var->dataLen = LEN_BUFFER(var);
            } else {
               var->dataLen = NULL;
            }
            prepareDataAndDescriptors(var);
         } else {
            var->returnCodes = NULL;
            var->dataLen = NULL;
            var->descriptors = NULL;
         }
      } else {
         sdb_thread_setError(SDB_ERROR_TYPE_SDB, SDB_OCI_ERROR_VAR_CREATE, "Cannot allocate memory to var.");
      }
   }
   return var;
}

SDB_OCI_VAR *sdb_oci_var_new(SDB_OCI_CONNECTION *conn, CFL_UINT16 dataType, CFL_INT64 itemSize, CFL_UINT32 maxItems,
                             CFL_BOOL bBindOut) {
   return var_new(conn, dataType, itemSize, maxItems, bBindOut, CFL_FALSE);
}

SDB_OCI_VAR *sdb_oci_var_new2(SDB_OCI_CONNECTION *conn, CFL_UINT16 dataType, CFL_INT64 itemSize, CFL_UINT32 maxItems,
                              CFL_BOOL bBindOut) {
   return var_new(conn, dataType, itemSize, maxItems, bBindOut, CFL_TRUE);
}

static void freeRowIds(SDB_OCI_VAR *var) {
   CFL_UINT32 i;
   for (i = 0; i < var->maxItems; i++) {
      if (var->rowids[i] != NULL) {
         sdb_oci_rowid_free(var->rowids[i]);
         var->rowids[i] = NULL;
         var->pointerData[i] = NULL;
      }
   }
}

static void freeLobs(SDB_OCI_VAR *var) {
   CFL_UINT32 i;
   for (i = 0; i < var->maxItems; i++) {
      if (var->lobs[i] != NULL) {
         sdb_oci_lob_free(var->lobs[i]);
         var->lobs[i] = NULL;
         var->pointerData[i] = NULL;
      }
   }
}

static void freeStatements(SDB_OCI_VAR *var) {
   CFL_UINT32 i;
   for (i = 0; i < var->maxItems; i++) {
      if (var->statements[i] != NULL) {
         sdb_oci_stmt_free(var->statements[i]);
         var->statements[i] = NULL;
         var->pointerData[i] = NULL;
      }
   }
}

static void freeDynamicBuffers(SDB_OCI_VAR *var) {
   CFL_UINT32 i;
   for (i = 0; i < var->maxItems; i++) {
      if (var->bufferData[i] != NULL) {
         SDB_MEM_FREE(var->bufferData[i]);
         var->bufferData[i] = NULL;
      }
   }
}

static void freeDescriptors(SDB_OCI_VAR *var) {
   switch (var->dataType) {
   case SDB_SQLT_RDD:
      freeRowIds(var);
      break;
   case SDB_SQLT_CLOB:
   case SDB_SQLT_BLOB:
      freeLobs(var);
      break;
   case SDB_SQLT_RSET:
      freeStatements(var);
      break;
   }
}

static void freeBuffers(SDB_OCI_VAR *var) {
   if (var->isDynamic && IS_BUFFER_DATATYPE(var->dataType)) {
      freeDynamicBuffers(var);
   } else {
      freeDescriptors(var);
   }
   if (var->maxItems > 1) {
      SDB_MEM_FREE(var->data);
   }
}

void sdb_oci_var_free(SDB_OCI_VAR *var) {
   freeBuffers(var);
   SDB_MEM_FREE(var);
}

CFL_BOOL sdb_oci_var_fitsContent(SDB_OCI_VAR *var, CFL_UINT16 dataType, CFL_INT64 itemSize, CFL_UINT32 maxItems,
                                 CFL_BOOL bBindOut) {
   CFL_BOOL bDynamic = dataTypeAndSize(&dataType, &itemSize, maxItems == 0 ? 1 : maxItems, bBindOut);
   if (var->isDynamic == bDynamic && maxItems <= var->maxItems && var->dataType == dataType && itemSize <= var->itemSize) {
      return CFL_TRUE;
   }
   return CFL_FALSE;
}

void *sdb_oci_var_getDataBuffer(SDB_OCI_VAR *var) {
   return (void *)var->data;
}

CFL_UINT16 *sdb_oci_var_getLenBuffer16(SDB_OCI_VAR *var) {
   return var->isDynamic ? NULL : var->dataLen16;
}

CFL_UINT32 *sdb_oci_var_getLenBuffer32(SDB_OCI_VAR *var) {
   return var->isDynamic ? NULL : var->dataLen32;
}

void *sdb_oci_var_getIndBuffer(SDB_OCI_VAR *var) {
   return var->indicator;
}

CFL_UINT16 *sdb_oci_var_getRetCodBuffer(SDB_OCI_VAR *var) {
   return var->returnCodes;
}

CFL_BOOL sdb_oci_var_isNull(SDB_OCI_VAR *var, CFL_UINT32 index) {
   return (index < var->maxItems && var->indicator[index] == SDB_OCI_IND_NULL) ? CFL_TRUE : CFL_FALSE;
}

CFL_BOOL sdb_oci_var_getBool(SDB_OCI_VAR *var, CFL_UINT32 index) {
   if (index < var->maxItems) {
      if (var->dataType == SDB_SQLT_BOL) {
         return var->boolData[index] ? CFL_TRUE : CFL_FALSE;
      } else if (var->dataType == SDB_SQLT_CHR) {
         return strCmpIgnoreCase(sdb_oci_var_getString(var, index), "Y", sdb_oci_var_getStringLen(var, index)) == 0 ? CFL_TRUE
                                                                                                                    : CFL_FALSE;
      }
   } else {
      sdb_thread_setError(SDB_ERROR_TYPE_SDB, SDB_OCI_ERROR_VAR_SET, "Variable index out of bound. index=%lu maxItems=%lu", index,
                          var->maxItems);
   }
   return CFL_FALSE;
}

char *sdb_oci_var_getString(SDB_OCI_VAR *var, CFL_UINT32 index) {
   if (index < var->maxItems) {
      if (var->isDynamic && IS_BUFFER_DATATYPE(var->dataType)) {
         if (var->bufferData[index] != NULL) {
            return var->bufferData[index]->data;
         }
      } else if (var->dataType == SDB_SQLT_CHR) {
         return &var->charData[CAST_INDEX(var->itemSize * (index))];
      }
   } else {
      sdb_thread_setError(SDB_ERROR_TYPE_SDB, SDB_OCI_ERROR_VAR_SET, "Variable index out of bound. index=%lu maxItems=%lu", index,
                          var->maxItems);
   }
   return "";
}

CFL_UINT32 sdb_oci_var_getStringLen(SDB_OCI_VAR *var, CFL_UINT32 index) {
   if (index < var->maxItems) {
      if (var->isDynamic && IS_BUFFER_DATATYPE(var->dataType)) {
         if (var->bufferData[index] != NULL) {
            SDB_OCI_BUFFER *buffer = var->bufferData[index];
            if (buffer->read > 0) {
               buffer->length += buffer->read;
               buffer->read = 0;
            }
            return buffer->length;
         }
      } else if (var->dataType == SDB_SQLT_CHR) {
         if (var->is32BitLen) {
            return var->dataLen32[index];
         } else {
            return (CFL_UINT32)var->dataLen16[index];
         }
      }
   } else {
      sdb_thread_setError(SDB_ERROR_TYPE_SDB, SDB_OCI_ERROR_VAR_SET, "Variable index out of bound. index=%lu maxItems=%lu", index,
                          var->maxItems);
   }
   return 0;
}

SDB_OCI_LOB *sdb_oci_var_getLob(SDB_OCI_VAR *var, CFL_UINT32 index) {
   if (index < var->maxItems) {
      if (var->dataType == SDB_SQLT_BLOB || var->dataType == SDB_SQLT_CLOB) {
         return var->lobs[index];
      }
   } else {
      sdb_thread_setError(SDB_ERROR_TYPE_SDB, SDB_OCI_ERROR_VAR_SET, "Variable index out of bound. index=%lu maxItems=%lu", index,
                          var->maxItems);
   }
   return NULL;
}

SDB_OCI_ROWID *sdb_oci_var_getRowId(SDB_OCI_VAR *var, CFL_UINT32 index) {
   if (index < var->maxItems) {
      if (var->dataType == SDB_SQLT_RDD) {
         return var->rowids[index];
      }
   } else {
      sdb_thread_setError(SDB_ERROR_TYPE_SDB, SDB_OCI_ERROR_VAR_SET, "Variable index out of bound. index=%lu maxItems=%lu", index,
                          var->maxItems);
   }
   return NULL;
}

SDB_OCI_STMT *sdb_oci_var_getStmt(SDB_OCI_VAR *var, CFL_UINT32 index) {
   if (index < var->maxItems) {
      if (var->dataType == SDB_SQLT_RSET) {
         return var->statements[index];
      }
   } else {
      sdb_thread_setError(SDB_ERROR_TYPE_SDB, SDB_OCI_ERROR_VAR_SET, "Variable index out of bound. index=%lu maxItems=%lu", index,
                          var->maxItems);
   }
   return NULL;
}

CFL_INT64 sdb_oci_var_getInt64(SDB_OCI_VAR *var, CFL_UINT32 index) {
   if (index < var->maxItems) {
      switch (var->dataType) {
      case SDB_SQLT_INT:
         return var->int64Data[index];

      case SDB_SQLT_BDOUBLE:
         return (CFL_INT64)var->doubleData[index];

      case SDB_SQLT_CHR:
         return (CFL_INT64)strtoint64(&var->charData[CAST_INDEX(var->itemSize * index)]);

      case SDB_SQLT_NUM:
      case SDB_SQLT_VNU: {
         void *errorHandle = sdb_oci_getErrorHandle();
         SDB_OCI_NUM *numPtr = &var->numData[index];
         CFL_INT64 value;
         int status;
         status = sdb_oci_getSymbols()->OCINumberToInt(errorHandle, numPtr, sizeof(value), SDB_OCI_NUMBER_SIGNED, &value);
         if (status == SDB_OCI_SUCCESS) {
            return value;
         } else {
            value = 0;
            sdb_oci_setErrorFromOCI(errorHandle, SDB_OCI_HTYPE_ERROR, "Error converting OCINumber to Int64");
         }
         return value;
      }
      }
   } else {
      sdb_thread_setError(SDB_ERROR_TYPE_SDB, SDB_OCI_ERROR_VAR_SET, "Variable index out of bound. index=%lu maxItems=%lu", index,
                          var->maxItems);
   }
   return 0;
}

double sdb_oci_var_getDouble(SDB_OCI_VAR *var, CFL_UINT32 index) {
   if (index < var->maxItems) {
      switch (var->dataType) {
      case SDB_SQLT_INT:
         return (double)var->int64Data[index];

      case SDB_SQLT_BDOUBLE:
         return var->doubleData[index];

      case SDB_SQLT_CHR:
         return atof(&var->charData[CAST_INDEX(var->itemSize * index)]);

      case SDB_SQLT_NUM:
      case SDB_SQLT_VNU: {
         void *errorHandle = sdb_oci_getErrorHandle();
         SDB_OCI_NUM *numPtr = &var->numData[index];
         double value;
         int status;
         status = sdb_oci_getSymbols()->OCINumberToReal(errorHandle, numPtr, sizeof(value), &value);
         if (status != SDB_OCI_SUCCESS) {
            value = 0.0;
            sdb_oci_setErrorFromOCI(errorHandle, SDB_OCI_HTYPE_ERROR, "Error converting OCINumber to double");
         }
         return value;
      }
      }
   } else {
      sdb_thread_setError(SDB_ERROR_TYPE_SDB, SDB_OCI_ERROR_VAR_SET, "Variable index out of bound. index=%lu maxItems=%lu", index,
                          var->maxItems);
   }
   return 0.0;
}

void sdb_oci_var_getDate(SDB_OCI_VAR *var, CFL_UINT32 index, CFL_INT16 *year, CFL_INT8 *month, CFL_INT8 *day, CFL_INT8 *hour,
                         CFL_INT8 *min, CFL_INT8 *sec) {
   if (index < var->maxItems) {
      if (var->dataType == SDB_SQLT_ODT) {
         SDB_OCI_DATE *datePtr = &var->dateData[index];
         *year = *((CFL_INT16 *)&datePtr->value[0]);
         *month = *((CFL_INT8 *)&datePtr->value[2]);
         *day = *((CFL_INT8 *)&datePtr->value[3]);
         *hour = *((CFL_INT8 *)&datePtr->value[4]);
         *min = *((CFL_INT8 *)&datePtr->value[5]);
         *sec = *((CFL_INT8 *)&datePtr->value[6]);
      } else {
         *year = 0;
         *month = 0;
         *day = 0;
         *hour = 0;
         *min = 0;
         *sec = 0;
      }
   } else {
      sdb_thread_setError(SDB_ERROR_TYPE_SDB, SDB_OCI_ERROR_VAR_SET, "Variable index out of bound. index=%lu maxItems=%lu", index,
                          var->maxItems);
   }
}

CFL_BOOL sdb_oci_var_setNull(SDB_OCI_VAR *var, CFL_UINT32 index) {
   if (index < var->maxItems) {
      var->indicator[index] = SDB_OCI_IND_NULL;
      if (HAS_DESCRIPTOR(var->dataType) && var->descriptors[index] != NULL && var->needPrefetch) {
         prepareDescriptor(var, index);
      }
      return CFL_TRUE;
   } else {
      sdb_thread_setError(SDB_ERROR_TYPE_SDB, SDB_OCI_ERROR_VAR_SET, "Variable index out of bound. index=%lu maxItems=%lu", index,
                          var->maxItems);
   }
   return CFL_FALSE;
}

static SDB_OCI_BUFFER *ensureBufferCapacity(SDB_OCI_BUFFER *buffer, CFL_UINT32 length) {
   if (buffer == NULL) {
      buffer = SDB_MEM_ALLOC(sizeof(SDB_OCI_BUFFER) + length + 1);
      buffer->capacity = length;
      buffer->length = 0;
      buffer->read = 0;
   } else if (buffer->capacity < length) {
      CFL_UINT32 newCapacity = (buffer->capacity >> 1) + 1 + length;
      buffer = SDB_MEM_REALLOC(buffer, sizeof(SDB_OCI_BUFFER) + newCapacity);
      buffer->capacity = newCapacity;
   }
   return buffer;
}

CFL_BOOL sdb_oci_var_setString(SDB_OCI_VAR *var, CFL_UINT32 index, const char *str, CFL_UINT32 strLen) {
   SDB_LOG_TRACE(("sdb_oci_var_setString. var=%p index=%u value='%.*s'", var, index, strLen, str));
   if (index < var->maxItems) {
      if (var->isDynamic) {
         if (IS_BUFFER_DATATYPE(var->dataType)) {
            var->bufferData[index] = ensureBufferCapacity(var->bufferData[index], strLen);
            if (var->bufferData[index] != NULL) {
               SDB_OCI_BUFFER *buffer;
               buffer = var->bufferData[index];
               buffer->length = strLen;
               buffer->read = 0;
               memcpy(buffer->data, str, strLen);
               var->indicator[index] = SDB_OCI_IND_NOTNULL;
               return CFL_TRUE;
            } else {
               sdb_thread_setError(SDB_ERROR_TYPE_SDB, SDB_OCI_ERROR_VAR_SET, "Cannot allocate dynamic data buffer.");
            }
         } else {
            sdb_thread_setError(SDB_ERROR_TYPE_SDB, SDB_OCI_ERROR_VAR_SET, "Invalid variable data type.");
         }
      } else if (var->dataType == SDB_SQLT_CHR) {
         CFL_UINT32 itemSize = strLen < var->itemSize ? strLen : (CFL_UINT32)var->itemSize;
         memcpy(&var->charData[CAST_INDEX(var->itemSize * index)], str, itemSize);
         if (var->is32BitLen) {
            var->dataLen32[index] = itemSize;
         } else {
            var->dataLen16[index] = (CFL_UINT16)itemSize;
         }
         var->indicator[index] = SDB_OCI_IND_NOTNULL;
         return CFL_TRUE;
      } else {
         sdb_thread_setError(SDB_ERROR_TYPE_SDB, SDB_OCI_ERROR_VAR_SET, "Invalid variable data type.");
      }
   } else {
      sdb_thread_setError(SDB_ERROR_TYPE_SDB, SDB_OCI_ERROR_VAR_SET, "Variable index out of bound. index=%lu maxItems=%lu", index,
                          var->maxItems);
   }
   return CFL_FALSE;
}

CFL_BOOL sdb_oci_var_setInt64(SDB_OCI_VAR *var, CFL_UINT32 index, CFL_INT64 value) {
   SDB_LOG_TRACE(("sdb_oci_var_setInt64. var=%p index=%u value=%lld", var, index, value));
   if (index < var->maxItems) {
      switch (var->dataType) {
      case SDB_SQLT_INT:
         var->int64Data[index] = value;
         var->indicator[index] = SDB_OCI_IND_NOTNULL;
         return CFL_TRUE;

      case SDB_SQLT_BDOUBLE:
         var->doubleData[index] = (double)value;
         var->indicator[index] = SDB_OCI_IND_NOTNULL;
         return CFL_TRUE;

      case SDB_SQLT_CHR:
         snprintf(&var->charData[CAST_INDEX(var->itemSize * index)], var->itemSize, "%lld", value);
         return CFL_TRUE;

      case SDB_SQLT_NUM:
      case SDB_SQLT_VNU: {
         void *errorHandle = sdb_oci_getErrorHandle();
         SDB_OCI_NUM *numPtr = &var->numData[index];
         int status;
         status = sdb_oci_getSymbols()->OCINumberFromInt(errorHandle, &value, sizeof(value), SDB_OCI_NUMBER_SIGNED, numPtr);
         if (status == SDB_OCI_SUCCESS) {
            var->indicator[index] = SDB_OCI_IND_NOTNULL;
            return CFL_TRUE;
         } else {
            sdb_oci_setErrorFromOCI(errorHandle, SDB_OCI_HTYPE_ERROR, "Error converting int64 to OCINumber");
         }
      } break;
      default:
         sdb_thread_setError(SDB_ERROR_TYPE_SDB, SDB_OCI_ERROR_VAR_SET, "Invalid variable data type.");
         break;
      }
   } else {
      sdb_thread_setError(SDB_ERROR_TYPE_SDB, SDB_OCI_ERROR_VAR_SET, "Variable index out of bound. index=%lu maxItems=%lu", index,
                          var->maxItems);
   }
   return CFL_FALSE;
}

CFL_BOOL sdb_oci_var_setDouble(SDB_OCI_VAR *var, CFL_UINT32 index, double value) {
   SDB_LOG_TRACE(("sdb_oci_var_setDouble. var=%p index=%u value=%f", var, index, value));
   if (index < var->maxItems) {
      switch (var->dataType) {
      case SDB_SQLT_INT:
         var->int64Data[index] = (CFL_INT64)value;
         var->indicator[index] = SDB_OCI_IND_NOTNULL;
         return CFL_TRUE;

      case SDB_SQLT_BDOUBLE:
         var->doubleData[index] = value;
         var->indicator[index] = SDB_OCI_IND_NOTNULL;
         return CFL_TRUE;

      case SDB_SQLT_CHR:
         snprintf(&var->charData[CAST_INDEX(var->itemSize * index)], var->itemSize, "%f", value);
         return CFL_TRUE;

      case SDB_SQLT_NUM:
      case SDB_SQLT_VNU: {
         void *errorHandle = sdb_oci_getErrorHandle();
         SDB_OCI_NUM *numPtr = &var->numData[index];
         int status;
         status = sdb_oci_getSymbols()->OCINumberFromReal(errorHandle, &value, sizeof(value), numPtr);
         if (status == SDB_OCI_SUCCESS) {
            var->indicator[index] = SDB_OCI_IND_NOTNULL;
            return CFL_TRUE;
         } else {
            sdb_oci_setErrorFromOCI(errorHandle, SDB_OCI_HTYPE_ERROR, "Error converting double to OCINumber");
         }
      } break;
      default:
         sdb_thread_setError(SDB_ERROR_TYPE_SDB, SDB_OCI_ERROR_VAR_SET, "Invalid variable data type.");
         break;
      }
   } else {
      sdb_thread_setError(SDB_ERROR_TYPE_SDB, SDB_OCI_ERROR_VAR_SET, "Variable index out of bound. index=%lu maxItems=%lu", index,
                          var->maxItems);
   }
   return CFL_FALSE;
}

CFL_BOOL sdb_oci_var_setBool(SDB_OCI_VAR *var, CFL_UINT32 index, CFL_BOOL value) {
   SDB_LOG_TRACE(("sdb_oci_var_setBool. var=%p index=%u value=%d", var, index, (value ? "TRUE" : "FALSE")));
   if (index < var->maxItems) {
      if (var->dataType == SDB_SQLT_BOL) {
         var->boolData[index] = value ? 1 : 0;
         var->indicator[index] = SDB_OCI_IND_NOTNULL;
         return CFL_TRUE;
      } else {
         sdb_thread_setError(SDB_ERROR_TYPE_SDB, SDB_OCI_ERROR_VAR_SET, "Invalid variable data type.");
      }
   } else {
      sdb_thread_setError(SDB_ERROR_TYPE_SDB, SDB_OCI_ERROR_VAR_SET, "Variable index out of bound. index=%lu maxItems=%lu", index,
                          var->maxItems);
   }
   return CFL_FALSE;
}

CFL_BOOL sdb_oci_var_setDate(SDB_OCI_VAR *var, CFL_UINT32 index, CFL_INT16 year, CFL_INT8 month, CFL_INT8 day, CFL_INT8 hour,
                             CFL_INT8 min, CFL_INT8 sec) {
   SDB_LOG_TRACE(("sdb_oci_var_setDate. var=%p index=%u date=%d-%d-%dT%d:%d:%d", var, index, year, month, day, hour, min, sec));
   if (index < var->maxItems) {
      if (var->dataType == SDB_SQLT_ODT) {
         SDB_OCI_DATE *date = &var->dateData[index];
         *((CFL_INT16 *)&date->value[0]) = year;
         *((CFL_INT8 *)&date->value[2]) = month;
         *((CFL_INT8 *)&date->value[3]) = day;
         *((CFL_INT8 *)&date->value[4]) = hour;
         *((CFL_INT8 *)&date->value[5]) = min;
         *((CFL_INT8 *)&date->value[6]) = sec;
         var->indicator[index] = SDB_OCI_IND_NOTNULL;
         return CFL_TRUE;
      } else {
         sdb_thread_setError(SDB_ERROR_TYPE_SDB, SDB_OCI_ERROR_VAR_SET, "Invalid variable data type.");
      }
   } else {
      sdb_thread_setError(SDB_ERROR_TYPE_SDB, SDB_OCI_ERROR_VAR_SET, "Variable index out of bound. index=%lu maxItems=%lu", index,
                          var->maxItems);
   }
   return CFL_FALSE;
}

CFL_BOOL sdb_oci_var_setRowId(SDB_OCI_VAR *var, CFL_UINT32 index, SDB_OCI_ROWID *rowId) {
   SDB_LOG_TRACE(("sdb_oci_var_setRowId. var=%p index=%u rowId=%p", var, index, rowId));
   if (index < var->maxItems) {
      if (var->dataType == SDB_SQLT_RDD) {
         SDB_OCI_ROWID *curRowId = var->rowids[index];
         if (curRowId != rowId) {
            if (curRowId != NULL) {
               sdb_oci_rowid_free(curRowId);
            }
            var->rowids[index] = rowId;
            var->pointerData[index] = rowId->handle;
            if (rowId != NULL) {
               sdb_oci_rowid_incRef(rowId);
               var->indicator[index] = SDB_OCI_IND_NOTNULL;
            } else {
               var->indicator[index] = SDB_OCI_IND_NULL;
            }
         }
         return CFL_TRUE;
      } else {
         sdb_thread_setError(SDB_ERROR_TYPE_SDB, SDB_OCI_ERROR_VAR_SET, "Invalid variable data type.");
      }
   } else {
      sdb_thread_setError(SDB_ERROR_TYPE_SDB, SDB_OCI_ERROR_VAR_SET, "Variable index out of bound. index=%lu maxItems=%lu", index,
                          var->maxItems);
   }
   return CFL_FALSE;
}

CFL_BOOL sdb_oci_var_setLob(SDB_OCI_VAR *var, CFL_UINT32 index, SDB_OCI_LOB *lob) {
   SDB_LOG_TRACE(("sdb_oci_var_setLob. var=%p index=%u rowId=%p", var, index, lob));
   if (index < var->maxItems) {
      if (var->dataType == SDB_SQLT_BLOB || var->dataType == SDB_SQLT_CLOB) {
         SDB_OCI_LOB *curLob = var->lobs[index];
         if (curLob != lob) {
            if (curLob != NULL) {
               sdb_oci_lob_free(curLob);
            }
            var->lobs[index] = lob;
            var->pointerData[index] = lob->handle;
            if (lob != NULL) {
               sdb_oci_lob_incRef(lob);
               var->indicator[index] = SDB_OCI_IND_NOTNULL;
            } else {
               var->indicator[index] = SDB_OCI_IND_NULL;
            }
         }
         return CFL_TRUE;
      } else {
         sdb_thread_setError(SDB_ERROR_TYPE_SDB, SDB_OCI_ERROR_VAR_SET, "Invalid variable data type.");
      }
   } else {
      sdb_thread_setError(SDB_ERROR_TYPE_SDB, SDB_OCI_ERROR_VAR_SET, "Variable index out of bound. index=%lu maxItems=%lu", index,
                          var->maxItems);
   }
   return CFL_FALSE;
}

CFL_BOOL sdb_oci_var_setStmt(SDB_OCI_VAR *var, CFL_UINT32 index, SDB_OCI_STMT *stmt) {
   SDB_LOG_TRACE(("sdb_oci_var_setStmt. var=%p index=%u rowId=%p", var, index, stmt));
   if (index < var->maxItems) {
      if (var->dataType == SDB_SQLT_RSET) {
         SDB_OCI_STMT *curStmt = var->statements[index];
         if (curStmt != stmt) {
            if (curStmt != NULL) {
               sdb_oci_stmt_free(curStmt);
            }
            var->statements[index] = stmt;
            var->pointerData[index] = stmt->handle;
            if (stmt != NULL) {
               sdb_oci_stmt_incRef(stmt);
               var->indicator[index] = SDB_OCI_IND_NOTNULL;
            } else {
               var->indicator[index] = SDB_OCI_IND_NULL;
            }
         }
         return CFL_TRUE;
      } else {
         sdb_thread_setError(SDB_ERROR_TYPE_SDB, SDB_OCI_ERROR_VAR_SET, "Invalid variable data type.");
      }
   } else {
      sdb_thread_setError(SDB_ERROR_TYPE_SDB, SDB_OCI_ERROR_VAR_SET, "Variable index out of bound. index=%lu maxItems=%lu", index,
                          var->maxItems);
   }
   return CFL_FALSE;
}

CFL_INT32 sdb_oci_var_inCallback(SDB_OCI_VAR *var, void *bindp, CFL_UINT32 iter, CFL_UINT32 index, void **bufpp, CFL_UINT32 *alenp,
                                 CFL_UINT8 *piecep, void **indpp) {
   CFL_UINT32 itemPos;
   HB_SYMBOL_UNUSED(bindp);
   SDB_LOG_TRACE(("sdb_oci_var_inCallback. var=%p index=%lu", var, index));
   if (index > 0) {
      if (iter == 0) {
         itemPos = index;
      } else {
         sdb_thread_setError(SDB_ERROR_TYPE_SDB, SDB_OCI_ERROR_BIND_OUT, "SDB OCI don't support many execution with array binds.");
         return SDB_OCI_ERROR;
      }
   } else {
      itemPos = iter;
   }
   *indpp = &var->indicator[itemPos];
   if (var->indicator[itemPos] == SDB_OCI_IND_NULL) {
      *bufpp = NULL;
      *alenp = 0;
   } else {
      switch (var->dataType) {
      case SDB_SQLT_LVC:
      case SDB_SQLT_LVB:
      case SDB_SQLT_LNG:
      case SDB_SQLT_BIN:
      case SDB_SQLT_LBI:
      case SDB_SQLT_AFC:
      case SDB_SQLT_CHR:
      case SDB_SQLT_STR:
      case SDB_SQLT_AVC:
      case SDB_SQLT_VCS:
         if (var->isDynamic) {
            *bufpp = var->bufferData[itemPos]->data;
            *alenp = var->bufferData[itemPos]->length;
         } else if (var->is32BitLen) {
            *bufpp = (void *)&var->data[CAST_INDEX(var->itemSize * itemPos)];
            *alenp = var->dataLen32[itemPos];
         } else {
            *bufpp = (void *)&var->data[CAST_INDEX(var->itemSize * itemPos)];
            *alenp = (CFL_UINT32)var->dataLen16[itemPos];
         }
         break;
      default:
         *bufpp = (void *)&var->data[CAST_INDEX(var->itemSize * itemPos)];
         *alenp = (CFL_UINT32)var->itemSize;
         break;
      }
   }
   *piecep = SDB_OCI_ONE_PIECE;
   return SDB_OCI_CONTINUE;
}

CFL_INT32 sdb_oci_var_outCallback(SDB_OCI_VAR *var, void *bindp, CFL_UINT32 iter, CFL_UINT32 index, void **bufpp,
                                  CFL_UINT32 **alenpp, CFL_UINT8 *piecep, void **indpp, CFL_UINT16 **rcodepp) {
   int status;
   CFL_UINT32 itemPos;

   HB_SYMBOL_UNUSED(bindp);
   SDB_LOG_TRACE(("sdb_oci_var_outCallback. var=%p iter=%lu index=%lu", var, iter, index));
   /* Allocate dynamic buffer to data that overflow current data size */
   if (index == 0) {
      void *errorHandle = sdb_oci_getErrorHandle();
      CFL_UINT32 rowsReturned;
      status = sdb_oci_getSymbols()->OCIAttrGet(var->handle, SDB_OCI_HTYPE_BIND, &rowsReturned, NULL, SDB_OCI_ATTR_ROWS_RETURNED,
                                                errorHandle);
      CHECK_STATUS_RETURN(status, errorHandle, "Error getting rows returned", SDB_OCI_ERROR);
      if (rowsReturned > var->maxItems) {
         CFL_UINT32 dataSize = (CFL_UINT32)BUFFERS_SIZE(var->dataType, var->itemSize, rowsReturned, var->isDynamic);
         freeBuffers(var);
         var->data = SDB_MEM_ALLOC(dataSize);
         if (var->data != NULL) {
            memset(var->data, 0, dataSize);
            var->maxItems = rowsReturned;
            var->indicator = INDICATOR_BUFFER(var);
            var->returnCodes = RETURN_BUFFER(var);
            var->dataLen = LEN_BUFFER(var);
         } else {
            sdb_thread_setError(SDB_ERROR_TYPE_SDB, SDB_OCI_ERROR_BIND_OUT, "Can't allocate memory for data buffer");
            return SDB_OCI_ERROR;
         }
      }
   }
   if (index > 0) {
      if (iter == 0) {
         itemPos = index;
      } else {
         sdb_thread_setError(SDB_ERROR_TYPE_SDB, SDB_OCI_ERROR_BIND_OUT, "SDB OCI don't support many execution with array binds.");
         return SDB_OCI_ERROR;
      }
   } else {
      itemPos = iter;
   }
   if (var->isDynamic) {
      switch (var->dataType) {
      case SDB_SQLT_LVC:
      case SDB_SQLT_LVB:
      case SDB_SQLT_LNG:
      case SDB_SQLT_BIN:
      case SDB_SQLT_LBI:
      case SDB_SQLT_AFC:
      case SDB_SQLT_CHR:
      case SDB_SQLT_STR:
      case SDB_SQLT_AVC:
      case SDB_SQLT_VCS:
         if (*piecep == SDB_OCI_ONE_PIECE || var->bufferData[itemPos] == NULL) {
            var->bufferData[itemPos] = ensureBufferCapacity(var->bufferData[itemPos], INITIAL_DYN_BUFFER_SIZE);
            if (var->bufferData[itemPos] != NULL) {
               var->bufferData[itemPos]->length = 0;
               var->bufferData[itemPos]->read = 0;
            } else {
               sdb_thread_setError(SDB_ERROR_TYPE_SDB, SDB_OCI_ERROR_BIND_OUT, "Error allocating dynamic buffer.");
               return SDB_OCI_ERROR;
            }
         } else {
            var->bufferData[itemPos]->length += var->bufferData[itemPos]->read;
            var->bufferData[itemPos]->read = 0;
            var->bufferData[itemPos] = ensureBufferCapacity(var->bufferData[itemPos], var->bufferData[itemPos]->capacity * 2);
         }
         var->bufferData[itemPos]->read = var->bufferData[itemPos]->capacity - var->bufferData[itemPos]->length;
         *alenpp = &var->bufferData[itemPos]->read;
         *bufpp = &var->bufferData[itemPos]->data[var->bufferData[itemPos]->length];
         break;
      default:
         *bufpp = (void *)&var->data[CAST_INDEX(var->itemSize * itemPos)];
         **alenpp = (CFL_UINT32)var->itemSize;
         break;
      }
   } else if (IS_CHAR_DATATYPE(var->dataType)) {
      var->is32BitLen = CFL_TRUE;
      *alenpp = &var->dataLen32[itemPos];
      *bufpp = (void *)&var->data[CAST_INDEX(var->itemSize * itemPos)];
      *piecep = SDB_OCI_ONE_PIECE;
   } else {
      **alenpp = (CFL_UINT32)var->itemSize;
      *bufpp = (void *)&var->data[CAST_INDEX(var->itemSize * itemPos)];
      *piecep = SDB_OCI_ONE_PIECE;
   }
   *indpp = &var->indicator[itemPos];
   *rcodepp = &var->returnCodes[itemPos];

   return SDB_OCI_CONTINUE;
}

CFL_INT32 sdb_oci_var_defineCallback(SDB_OCI_VAR *var, void *defnp, CFL_UINT32 iter, void **bufpp, CFL_UINT32 **alenpp,
                                     CFL_UINT8 *piecep, void **indpp, CFL_UINT16 **rcodepp) {
   HB_SYMBOL_UNUSED(defnp);
   SDB_LOG_TRACE(("sdb_oci_var_defineCallback. var=%p iter=%lu", var, iter));
   if (var->isDynamic) {
      switch (var->dataType) {
      case SDB_SQLT_LVC:
      case SDB_SQLT_LVB:
      case SDB_SQLT_LNG:
      case SDB_SQLT_BIN:
      case SDB_SQLT_LBI:
         if (var->bufferData[iter] == NULL) {
            var->bufferData[iter] = ensureBufferCapacity(var->bufferData[iter], INITIAL_DYN_BUFFER_SIZE);
            if (var->bufferData[iter] != NULL) {
               var->bufferData[iter]->length = 0;
               var->bufferData[iter]->read = 0;
            } else {
               sdb_thread_setError(SDB_ERROR_TYPE_SDB, SDB_OCI_ERROR_BIND_OUT, "Error allocating dynamic buffer.");
               return SDB_OCI_ERROR;
            }
         } else {
            var->bufferData[iter]->length += var->bufferData[iter]->read;
            var->bufferData[iter]->read = 0;
            var->bufferData[iter] = ensureBufferCapacity(var->bufferData[iter], var->bufferData[iter]->capacity * 2);
         }
         var->bufferData[iter]->read = var->bufferData[iter]->capacity - var->bufferData[iter]->length;
         *alenpp = &var->bufferData[iter]->read;
         *bufpp = &var->bufferData[iter]->data[var->bufferData[iter]->length];
         *indpp = &var->indicator[iter];
         *rcodepp = &var->returnCodes[iter];
         *piecep = SDB_OCI_ONE_PIECE;
         break;
      }
   }
   return SDB_OCI_CONTINUE;
}

void sdb_oci_var_prepareFetch(SDB_OCI_VAR *var) {
   if (var->needPrefetch) {
      if (HAS_DESCRIPTOR(var->dataType)) {
         prepareDescriptor(var, 0);
      } else if (var->isDynamic && IS_BUFFER_DATATYPE(var->dataType)) {
         prepareBuffer(var, 0);
      }
   }
   var->needPrefetch = CFL_TRUE;
}
