#ifndef SDB_PARAM_H_

#define SDB_PARAM_H_

#include "hbapi.h"

#include "cfl_types.h"
#include "cfl_str.h"
#include "cfl_array.h"
#include "cfl_iterator.h"

#include "sdb_defs.h"

/* get functions  */
#define sdb_param_getName(p)           ((p)->name)
#define sdb_param_getNameChar(p)       ((p)->name != NULL ? cfl_str_getPtr((p)->name) : "")
#define sdb_param_getNameLen(p)        ((p)->name != NULL ? cfl_str_getLength((p)->name) : 0)
#define sdb_param_getPos(p)            ((p)->pos)
#define sdb_param_getType(p)           ((p)->type)
#define sdb_param_getLength(p)         ((p)->length)
#define sdb_param_getItem(p)           ((p)->pItem)
#define sdb_param_getItemBind(p)       ((p)->pItemBind)
#define sdb_param_getHandle(p)         ((p)->handle)
#define sdb_param_getHandleFreeFun(p)  ((p)->freeHandle)
#define sdb_param_isOut(p)             ((p)->isOut)
#define sdb_param_isTrim(p)            ((p)->isTrim)
#define sdb_param_isNullable(p)        ((p)->isNullable)
#define sdb_param_isBind(p)            ((p)->pItemBind != NULL)
#define sdb_param_isArray(p)           HB_IS_ARRAY((p)->pItem)
#define sdb_param_isNamed(p)           ((p)->name != NULL)

/* set functions  */
#define sdb_param_setName(p, n)         cfl_str_setStr((p)->name,n)
#define sdb_param_setNameChar(p, n)     cfl_str_setValue((p)->name,n)
#define sdb_param_setType(p,v)          ((p)->type = v)
#define sdb_param_setLength(p,v)        ((p)->length = v)
#define sdb_param_unbindItem(p)         ((p)->pItemBind = NULL)
#define sdb_param_bindItem(p, i)        ((p)->pItemBind = i)
#define sdb_param_setHandle(p,v)        ((p)->handle = v)
#define sdb_param_setHandleFreeFun(p,v) ((p)->freeHandle = v)
#define sdb_param_setOut(p,v)           ((p)->isOut = v)
#define sdb_param_setTrim(p,v)          ((p)->isTrim = v)
#define sdb_param_setNullable(p,v)      ((p)->isNullable = v)
#define sdb_param_updateBind(p)         if ((p)->pItemBind != NULL) hb_itemCopy((p)->pItemBind, (p)->pItem)

typedef void ( * SDB_DB_FNC_FREE_PARAM )(SDB_PARAMP param);

struct _SDB_PARAM {
   CFL_UINT8             objectType;
   CFL_STRP              name;
   CFL_UINT32            pos;
   SDB_CLP_DATATYPE      type;
   CFL_UINT64            length;
   PHB_ITEM              pItem;
   PHB_ITEM              pItemBind;
   void                  *handle;
   SDB_DB_FNC_FREE_PARAM freeHandle;
   CFL_BOOL              isOut      BIT_FIELD;  // Clipper variable will be updated after statement execution
   CFL_BOOL              isTrim     BIT_FIELD;  // When true and datatype is string, then trim spaces from end of string before execution
   CFL_BOOL              isNullable BIT_FIELD;  // Accept NULL
};

struct _SDB_PARAMLIST {
   CFL_ARRAY items;
};


extern void sdb_param_free(SDB_PARAMP param);
extern PHB_ITEM sdb_param_setValue(SDB_PARAMP param, PHB_ITEM pItem);

extern SDB_PARAMLISTP sdb_param_listNew(void);
extern void sdb_param_listFree(SDB_PARAMLISTP params);
extern void sdb_param_listClear(SDB_PARAMLISTP params);
extern SDB_PARAMP sdb_param_listAddFirst(SDB_PARAMLISTP params, const char *name, CFL_UINT32 nameLen, SDB_CLP_DATATYPE type,
        CFL_UINT64 length, CFL_BOOL bOut, CFL_BOOL bTrim, CFL_BOOL bNullable);
extern SDB_PARAMP sdb_param_listAdd(SDB_PARAMLISTP params, const char *name, CFL_UINT32 nameLen, SDB_CLP_DATATYPE type,
        CFL_UINT64 length, CFL_BOOL bOut, CFL_BOOL bTrim, CFL_BOOL bNullable);
extern CFL_BOOL sdb_param_listIsEmpty(SDB_PARAMLISTP params);
extern CFL_UINT32 sdb_param_listLength(SDB_PARAMLISTP params);
extern SDB_PARAMP sdb_param_listGet(SDB_PARAMLISTP params, const char *paramName);
extern SDB_PARAMP sdb_param_listGetPos(SDB_PARAMLISTP params, CFL_UINT32 pos);
extern CFL_ITERATORP sdb_param_listIterator(SDB_PARAMLISTP paramList);
extern void sdb_param_listMoveAll(SDB_PARAMLISTP destList, SDB_PARAMLISTP sourceList);
extern void sdb_param_listUpdateBinds(SDB_PARAMLISTP list);

#endif
