#include "cfl_ints.h"

#include <stdlib.h>
#include <ctype.h>

#include "hbapiitm.h"
#include "hbdefs.h"
#include "hbcomp.h"
#ifdef __XHB__
   #include "hashapi.h"
#endif
#include "hbset.h"

#include "sdb_util.h"
#include "sdb_connection.h"
#include "sdb_field.h"
#include "sdb_lob.h"
#include "sdb_log.h"
#include "sdb_statement.h"
#include "sdb_lob.h"
#include "sdb_schema.h"

#ifdef __XHB__
   #define ITEM_TYPE(p) (hb_itemType( p ) & ~HB_IT_BYREF)
#else
   #define ITEM_TYPE(p) (hb_itemType( p ) & ~HB_IT_DEFAULT)
#endif

CFL_BOOL sdb_util_itemIsNull(PHB_ITEM pItem) {
   return pItem == NULL || HB_IS_NIL(pItem);
}

CFL_BOOL sdb_util_itemIsEmpty(PHB_ITEM pItem) {
   if (pItem) {
      switch (ITEM_TYPE( pItem )) {
         case HB_IT_ARRAY:
            return hb_arrayLen( pItem ) == 0;

         case HB_IT_HASH:
            return hb_hashLen( pItem ) == 0;

         case HB_IT_STRING:
         case HB_IT_MEMO:
            return (CFL_BOOL) hb_strEmpty( hb_itemGetCPtr( pItem ), hb_itemGetCLen( pItem ) );

         case HB_IT_INTEGER:
            return hb_itemGetNI( pItem ) == 0;

         case HB_IT_LONG:
            return hb_itemGetNLL( pItem ) == 0;

         case HB_IT_DOUBLE:
            return hb_itemGetND( pItem ) == 0.0;

         case HB_IT_DATE:
            return hb_itemGetDL( pItem ) == 0;

#ifndef __XHB__
         case HB_IT_TIMESTAMP:
         {
            long lDate, lTime;
            hb_itemGetTDT( pItem, &lDate, &lTime );
            return lDate == 0 && lTime == 0;
         }
#endif
         case HB_IT_LOGICAL:
            return ! hb_itemGetL( pItem );

         case HB_IT_BLOCK:
            return CFL_FALSE;

         case HB_IT_POINTER:
            return hb_itemGetPtr( pItem ) == NULL;

#ifndef __XHB__
         case HB_IT_SYMBOL:
         {
            PHB_SYMB pSym = hb_itemGetSymbol( pItem );
            if( pSym && ( pSym->scope.value & HB_FS_DEFERRED ) && \
                pSym->pDynSym )
               pSym = hb_dynsymSymbol( pSym->pDynSym );
            return pSym == NULL || pSym->value.pFunPtr == NULL;
         }
#endif
      }
   }
   return CFL_TRUE;
}

CFL_BOOL sdb_util_evalLogicalResult(PHB_ITEM pCodeBlock) {
   PHB_ITEM pItem;
   CFL_BOOL bResult;

   pItem = hb_itemDo(pCodeBlock, 0);
   bResult = (CFL_BOOL) hb_itemGetL(pItem);
   hb_itemRelease(pItem);
   return bResult;
}

PHB_ITEM sdb_util_itemStrToNumber(PHB_ITEM pText) {
   const char *szText = hb_itemGetCPtr(pText);
   int iWidth;
   int iDec;
   HB_SIZE iLen = hb_itemGetCLen(pText);
   HB_BOOL fDbl;
   HB_MAXINT lValue;
   double dValue;

   fDbl = hb_valStrnToNum(szText, iLen, &lValue, &dValue, &iDec, &iWidth);
   if (fDbl) {
      return hb_itemPutNLen(NULL, dValue, iWidth, iDec);
   } else {
      return hb_itemPutNLLLen(NULL, lValue, iWidth);
   }
}

char *sdb_util_schemaFromPath(const char *path) {
   char *schema = NULL;
   if (path) {
      char *ptr = (char *) path;
      char *start = (char *) path;
      int len = 0;
      char lastChar = '\\';
      while (*ptr != '\0') {
         if (*ptr != '\\' && *ptr != '/') {
            if (lastChar == '\\' || lastChar == '/') {
               start = ptr;
               len = 0;
            }
            ++len;
         }
         lastChar = *ptr;
         ++ptr;
      }
      if (len > 0) {
         schema = SDB_MEM_ALLOC(sizeof (char) * (len + 1));
         memcpy(schema, start, len);
         schema[len] = '\0';
      }
   }
   return schema;
}

CFL_UINT8 sdb_util_clpToSDBType(const char *clpType) {
   if (clpType != NULL) {
      switch (clpType[0]) {
         case 'M':
         case 'm':
            return SDB_CLP_MEMO_LONG;

         case 'C':
         case 'c':
            return SDB_CLP_CHARACTER;

         case 'N':
         case 'n':
            return SDB_CLP_NUMERIC;

         case 'L':
         case 'l':
            return SDB_CLP_LOGICAL;

         case 'D':
         case 'd':
            return SDB_CLP_DATE;

         case 'T':
         case 't':
            return SDB_CLP_TIMESTAMP;

//         case 'P':
//         case 'p':
//         case 'A':
//         case 'a':
//         case 'H':
//         case 'h':
//         case 'O':
//         case 'o':
//         case 'B':
//         case 'b':
//         case 'S':
//         case 's':
         default:
            return SDB_CLP_UNKNOWN;
      }
   }
   return SDB_CLP_UNKNOWN;
}

CFL_UINT8 sdb_util_itemToFieldType(PHB_ITEM pItem) {
   SDB_LOG_TRACE(("sdb_util_itemToFieldType: value=%u", ITEM_STR(pItem)));
   if (pItem) {
      switch (ITEM_TYPE( pItem )) {
         case HB_IT_MEMO:
            return SDB_CLP_BLOB;

         case HB_IT_STRING:
            return SDB_CLP_CHARACTER;

         case HB_IT_INTEGER:
         case HB_IT_LONG:
         case HB_IT_DOUBLE:
            return SDB_CLP_NUMERIC;

         case HB_IT_LOGICAL:
            return SDB_CLP_LOGICAL;

         case HB_IT_DATE:
            return SDB_CLP_DATE;

#ifndef __XHB__
         case HB_IT_TIMESTAMP:
            return SDB_CLP_TIMESTAMP;
#endif
         case HB_IT_POINTER:
            {
               CFL_UINT8 *obj = (CFL_UINT8 *) hb_itemGetPtr(pItem);
               if (obj != NULL) {
                  if (obj[0] == SDB_OBJ_STATEMENT) {
                     return SDB_CLP_CURSOR;
                  } else if (obj[0] == SDB_OBJ_LOB) {
                     return ((SDB_LOBP)obj)->type;
                  }
               }
            }
            break;

         case HB_IT_ARRAY:
            if (hb_arrayLen(pItem) > 0) {
               PHB_ITEM pFirstItem = hb_arrayGetItemPtr(pItem, 1);
               if (! (hb_itemType( pFirstItem ) & HB_IT_ARRAY)) {
                  return sdb_util_itemToFieldType(pFirstItem);
               }
            }
            break;
//         case 'H':
//         case 'O':
//         case 'B':
//         case 'S':
//            break;
      }
   }
   return SDB_CLP_UNKNOWN;
}

HB_SIZE sdb_util_itemLen(PHB_ITEM pItem, CFL_BOOL trimmed) {
   int iLen;
   int iDec;

   switch (ITEM_TYPE(pItem)) {
      case HB_IT_STRING:
      case HB_IT_STRING | HB_IT_MEMO:
         if (trimmed) {
            return sdb_util_trimmedLen(hb_itemGetCPtr(pItem), hb_itemGetCLen(pItem));
         } else {
            return (CFL_UINT32) hb_itemGetCLen(pItem);
         }

      case HB_IT_INTEGER:
      case HB_IT_LONG:
      case HB_IT_DOUBLE:
         hb_itemGetNLen(pItem, &iLen, &iDec);
         if (iDec) {
            return (CFL_UINT32) iLen + iDec + 1;
         } else {
            return (CFL_UINT32) iLen;
         }

      case HB_IT_DATE:
         return 8;

#ifndef __XHB__
      case HB_IT_TIMESTAMP:
         return 17;
#endif
      case HB_IT_LOGICAL:
         return 1;

      default:
         return 0;
   }
}

CFL_BOOL sdb_util_isBigDataType(SDB_FIELDP field) {
   if (field != NULL) {
      switch (field->clpType) {
         case 'M':
         case 'P':
         case 'W':
            return CFL_TRUE;
      }
   }
   return CFL_FALSE;
}

CFL_BOOL sdb_util_isEmpty(const char *str) {
   if (str != NULL) {
      int i = 0;
      while (str[i] != '\0') {
         if (str[i] != ' ') {
            return CFL_FALSE;
         }
         ++i;
      }
   }
   return CFL_TRUE;
}

CFL_BOOL sdb_util_isNumeric(const char *str) {
   if (str != NULL) {
      int i = 0;
      while (str[i] != '\0') {
         if ((str[i] < '0' || str[i] > '9') && str[i] != '.') {
            return CFL_FALSE;
         }
         ++i;
      }
   }
   return CFL_TRUE;
}

CFL_UINT32 sdb_util_itemHash(PHB_ITEM pItem) {
   CFL_UINT32 hash;
   HB_SIZE len;
   HB_SIZE i;
   const char *str;
   long julian;
   long millisec;

   switch (ITEM_TYPE(pItem)) {
      case HB_IT_STRING:
      case HB_IT_STRING | HB_IT_MEMO:
         hash = 0;
         len = hb_itemGetCLen(pItem);
         str = hb_itemGetCPtr(pItem);
         for (i = 0; i < len; i++) {
            hash = 31 * hash + str[i];
         }
         break;

      case HB_IT_INTEGER:
      case HB_IT_LONG:
      case HB_IT_DOUBLE:
      case HB_IT_NUMERIC:
         hash = (CFL_UINT32) hb_itemGetNLL(pItem);
         break;

      case HB_IT_DATE:
         julian = hb_itemGetDL(pItem);
         hash = (CFL_UINT32) julian ^ (CFL_UINT32) (julian >> sizeof(long));
         break;

#ifndef __XHB__
      case HB_IT_TIMESTAMP:
         hb_itemGetTDT(pItem, &julian, &millisec);
         hash = ((CFL_UINT32) julian ^ (CFL_UINT32) (julian >> sizeof(long))) +
                ((CFL_UINT32) millisec ^ (CFL_UINT32) (millisec >> sizeof(long)));
         break;
#endif
      case HB_IT_LOGICAL:
         hash = hb_itemGetL(pItem) ? 1231 : 1237;
         break;
      default:
         hash = 0;
         break;
   }
   return hash;
}

PHB_ITEM sdb_util_itemCopyRTrim(PHB_ITEM pDest, PHB_ITEM pSource, CFL_BOOL isEmpty1Char) {
   const char *sourceBuf = hb_itemGetCPtr(pSource);
   int sourceLen = (int) hb_itemGetCLen(pSource) - 1;

   if (pDest == NULL) {
      pDest = hb_itemNew(NULL);
   }
   while (sourceLen >= 0 && sourceBuf[sourceLen] == ' ') {
      --sourceLen;
   }
   if (sourceLen >= 0) {
      hb_itemPutCL(pDest, sourceBuf, sourceLen + 1);
   } else if (isEmpty1Char) {
      hb_itemPutCL(pDest, " ", 1);
   } else {
      hb_itemPutCL(pDest, "", 0);
   }
   return pDest;
}

char * sdb_util_strdup(const char * strSource, CFL_BOOL bTrim) {
   char * strDest;
   HB_SIZE nDestLen;
   HB_SIZE nSourceLen = 0;

   while (strSource[nSourceLen]) {
      ++nSourceLen;
   }

   /* Allocate sapce for full text */
   strDest = (char *) hb_xgrab(nSourceLen + 1);

   nDestLen = nSourceLen;
   if (bTrim) {
      while (nDestLen && strSource[nDestLen - 1] == ' ') {
         --nDestLen;
      }
   }
   memcpy(strDest, strSource, nDestLen);
   strDest[nDestLen] = '\0';
   return strDest;
}

char * sdb_util_strndup(const char * strSource, CFL_UINT32 nSourceLen, CFL_BOOL bTrim) {
   char * strDest;
   CFL_UINT32 nDestLen;

   /* Allocate sapce for full text */
   strDest = (char *) hb_xgrab(nSourceLen + 1);

   nDestLen = nSourceLen;
   if (bTrim) {
      while (nDestLen && strSource[nDestLen - 1] == ' ') {
         --nDestLen;
      }
   }
   memcpy(strDest, strSource, nDestLen);
   strDest[nDestLen] = '\0';
   return strDest;
}

char * sdb_util_strnDupUpperTrim(const char * source, size_t sourceLen) {
   char *str;
   char *buffer;
   char *start = (char *) source;
   char *end = (char *) &source[sourceLen - 1];

   while (start < end && *start <= ' ') {
      ++start;
   }

   while (end > start && *end <= ' ') {
      --end;
   }

   buffer = (char *) SDB_MEM_ALLOC(end - start + 2);
   str = buffer;
   while (start <= end) {
      *buffer++ = toupper(*start++);
   }
   *buffer = '\0';
   return str;
}

char * sdb_util_strncpy(char * strDest, const char * strSource, CFL_UINT32 nSourceLen, CFL_BOOL bTrim) {
   CFL_UINT32 nDestLen;

   nDestLen = nSourceLen;
   if (bTrim) {
      while (nDestLen && strSource[nDestLen - 1] == ' ') {
         --nDestLen;
      }
   }
   memcpy(strDest, strSource, nDestLen);
   strDest[nDestLen] = '\0';

   return strDest;
}

HB_SIZE sdb_util_trimmedLen(const char * str, HB_SIZE nStrLen) {
   HB_SIZE nTrimLen;

   nTrimLen = nStrLen;
   while (nTrimLen > 0 && str[nTrimLen - 1] == ' ') {
      --nTrimLen;
   }
   return nTrimLen;
}

CFL_STRP sdb_util_strAppendItem(CFL_STRP sb, PHB_ITEM item) {
   HB_SIZE strLen;
   HB_BOOL freeStr;
   char *strValue = hb_itemString(item, &strLen, &freeStr);
   cfl_str_appendLen(sb, strValue, (int) strLen);
   if (freeStr) {
      hb_xfree(strValue);
   }
   return sb;
}

CFL_BOOL sdb_util_isValidType(CFL_UINT8 type) {
   switch(type) {
      case SDB_CLP_CHARACTER:
      case SDB_CLP_LOGICAL:
      case SDB_CLP_DATE:
      case SDB_CLP_NUMERIC:
      case SDB_CLP_FLOAT:
      case SDB_CLP_INTEGER:
      case SDB_CLP_BIGINT:
      case SDB_CLP_DOUBLE:
      case SDB_CLP_TIMESTAMP:
      case SDB_CLP_MEMO_LONG:
      case SDB_CLP_IMAGE:
      case SDB_CLP_LONG_RAW:
      case SDB_CLP_BLOB:
      case SDB_CLP_CLOB:
      case SDB_CLP_CURSOR:
      case SDB_CLP_ROWID:
         return CFL_TRUE;
   }
   return CFL_FALSE;
}

char * sdb_util_getSchemaName(SDB_CONNECTIONP connection, const char * szPath) {
   char * szSchema;

   szSchema = sdb_util_schemaFromPath(szPath);
   if (szSchema == NULL) {
      szSchema = (char *) SDB_MEM_ALLOC(sizeof(char) * (cfl_str_getLength(connection->schema->name) + 1));
      strcpy(szSchema, cfl_str_getPtr(connection->schema->name));
   }
   return szSchema;
}

#ifdef __XHB__

#include "hbapirdd.h"

CFL_BOOL hb_itemEqual( PHB_ITEM pItem1, PHB_ITEM pItem2 ) {

   if( HB_IS_NIL( pItem1 ) ) {

      return HB_IS_NIL( pItem2 );

   } else if( HB_IS_NIL( pItem2 ) ) {

      return CFL_FALSE;

   } else if( HB_IS_STRING( pItem1 ) && HB_IS_STRING( pItem2 ) ) {

      return hb_itemStrCmp( pItem1, pItem2, CFL_TRUE ) == 0;

   } else if( HB_IS_NUMINT( pItem1 ) && HB_IS_NUMINT( pItem2 ) ) {

      return HB_ITEM_GET_NUMINTRAW( pItem1 ) == HB_ITEM_GET_NUMINTRAW( pItem2 );

   } else if( HB_IS_NUMERIC( pItem1 ) && HB_IS_NUMERIC( pItem2 ) ) {

      return hb_itemGetND( pItem1 ) == hb_itemGetND( pItem2 );

   } else if( HB_IS_LOGICAL( pItem1 ) && HB_IS_LOGICAL( pItem2 ) ) {

      return pItem1->item.asLogical.value == pItem2->item.asLogical.value;

   } else if( HB_IS_ARRAY( pItem1 ) && HB_IS_ARRAY( pItem2 ) ) {

      return pItem1->item.asArray.value == pItem2->item.asArray.value;

   } else if( HB_IS_HASH( pItem1 ) && HB_IS_HASH( pItem2 ) ) {

      return pItem1->item.asHash.value == pItem2->item.asHash.value;

   } else if( HB_IS_POINTER( pItem1 ) && HB_IS_POINTER( pItem2 ) ) {

      return pItem1->item.asPointer.value == pItem2->item.asPointer.value;

   }
   return CFL_FALSE;
}

void hb_vmDestroyBlockOrMacro(PHB_ITEM pBlock) {
   if (hb_itemType(pBlock) != HB_IT_BLOCK) {
      hb_macroDelete((HB_MACRO_PTR) hb_itemGetPtr(pBlock));
   }
   hb_itemRelease(pBlock);
}

void * hb_rddGetWorkAreaPointer(int iArea) {
   int iCurrArea = hb_rddGetCurrentWorkAreaNumber();
   void *area;
   hb_rddSelectWorkAreaNumber(iArea);
   area = hb_rddGetCurrentWorkAreaPointer();
   hb_rddSelectWorkAreaNumber(iCurrArea);
   return area;
}

CFL_BOOL hb_setGetFixed(void) {
   return hb_set.HB_SET_FIXED;
}

CFL_BOOL hb_setSetItem(int set_specifier, PHB_ITEM pItem) {
   if( pItem ) {
      switch( set_specifier ) {
         case HB_SET_FIXED:
            hb_set.HB_SET_FIXED = hb_itemGetL(pItem);
            return CFL_TRUE;
      }
   }
   return CFL_FALSE;
}

CFL_BOOL hb_setGetDeleted(void) {
   return hb_set.HB_SET_DELETED;
}

void * hb_itemGetPtrGC(PHB_ITEM pItem, const HB_GARBAGE_FUNC_PTR pFunc) {

   void *value = hb_itemGetPtr(pItem);
   if (value) {
      HB_GARBAGE_PTR pAlloc = ( HB_GARBAGE_PTR ) value;
      --pAlloc;
      if( pAlloc->pFunc == pFunc) {
         return value;
      }
   }
   return NULL;
}

#endif