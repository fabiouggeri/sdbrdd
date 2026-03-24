#ifndef SDB_UTIL_H_

#define SDB_UTIL_H_

#include "hbapi.h"

#include "cfl_types.h"
#include "cfl_str.h"

#include "sdb_defs.h"

#define CFL_STR_ITEM_TRIM_UPPER(i) cfl_str_toUpper(cfl_str_trim(cfl_str_appendLen(cfl_str_new((CFL_UINT32) hb_itemGetCLen(i)), hb_itemGetCPtr(i), (int) hb_itemGetCLen(i))))
#define CFL_STR_ITEM_TRIM_LOWER(i) cfl_str_toLower(cfl_str_trim(cfl_str_appendLen(cfl_str_new((CFL_UINT32) hb_itemGetCLen(i)), hb_itemGetCPtr(i), (int) hb_itemGetCLen(i))))
#define CFL_STR_SET_NULL(s) if ((s) != NULL) cfl_str_free(s); s = NULL

#define sdb_util_itemPutRecno(i, r) hb_itemPutNLL(i, r)

extern CFL_BOOL sdb_util_itemIsNull(PHB_ITEM pItem);
extern CFL_BOOL sdb_util_itemIsEmpty(PHB_ITEM pItem);
extern CFL_BOOL sdb_util_evalLogicalResult(PHB_ITEM pCodeBlock);
extern char *sdb_util_schemaFromPath(const char *path);
extern HB_SIZE sdb_util_itemLen(PHB_ITEM pItem, CFL_BOOL trimmed);
extern CFL_UINT8 sdb_util_clpToSDBType(const char *clpType);
extern CFL_UINT8 sdb_util_itemToFieldType(PHB_ITEM pItem);
extern CFL_BOOL sdb_util_isBigDataType(SDB_FIELDP field);
extern CFL_BOOL sdb_util_isEmpty(const char *str);
extern CFL_BOOL sdb_util_isNumeric(const char *str);
extern CFL_STRP base64_encode(const unsigned char *bufferToEncode, unsigned int len, CFL_STRP result);
extern CFL_STRP base64_decode(const char *bufferToDecode, int len, CFL_STRP result);
extern CFL_UINT32 sdb_util_itemHash(PHB_ITEM item);
extern PHB_ITEM sdb_util_itemCopyRTrim(PHB_ITEM pDest, PHB_ITEM pSource, CFL_BOOL isEmpty1Char);
extern char * sdb_util_strdup(const char * pszText, CFL_BOOL bTrim);
extern char * sdb_util_strndup(const char * pszText, CFL_UINT32 nLen, CFL_BOOL bTrim);
extern char * sdb_util_strnDupUpperTrim(const char * source, size_t sourceLen);
extern char * sdb_util_strncpy(char * pDest, const char * pSource, CFL_UINT32 nLen, CFL_BOOL bTrim);
extern HB_SIZE sdb_util_trimmedLen(const char * str, HB_SIZE nStrLen);
extern CFL_STRP sdb_util_strAppendItem(CFL_STRP sb, PHB_ITEM item);
extern CFL_BOOL sdb_util_isValidType(CFL_UINT8 type);
extern char * sdb_util_getSchemaName(SDB_CONNECTIONP connection, const char * szPath);

#ifdef __XHB__

   extern CFL_BOOL hb_itemEqual( PHB_ITEM pItem1, PHB_ITEM pItem2 );
   extern void hb_vmDestroyBlockOrMacro(PHB_ITEM pBlock);
   extern void * hb_rddGetWorkAreaPointer(int iArea);
   extern CFL_BOOL hb_setGetFixed(void);
   extern CFL_BOOL hb_setSetItem(int set_specifier, PHB_ITEM pItem);
   extern CFL_BOOL hb_setGetDeleted(void);
   extern void * hb_itemGetPtrGC(PHB_ITEM pItem, const HB_GARBAGE_FUNC_PTR pFunc);

#endif

#endif
