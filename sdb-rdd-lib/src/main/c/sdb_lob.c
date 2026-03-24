#include "sdb_lob.h"
#include "sdb_connection.h"
#include "sdb_error.h"
#include "sdb_thread.h"
#include "sdb_api.h"
#include "sdb_log.h"
#include "sdb_util.h"

static HB_GARBAGE_FUNC(lob_destructor) {
   SDB_LOBP *pLobPtr = (SDB_LOBP *) Cargo;

   if (pLobPtr != NULL) {
      SDB_LOBP pLob = (SDB_LOBP) *pLobPtr;
      if (pLob != NULL) {
         pLob->handle = NULL;
         sdb_lob_free(pLob);
         *pLobPtr = NULL;
      }
   }
}

//#ifdef __HBR__
//static const HB_GC_FUNCS s_gcLobFuncs = { lob_destructor, hb_gcDummyMark };
//#endif


SDB_LOBP sdb_lob_new(SDB_CONNECTIONP connection, CFL_UINT8 type, void *handle) {
   SDB_LOBP pLob = (SDB_LOBP) SDB_MEM_ALLOC(sizeof (SDB_LOB));
   pLob->objectType = SDB_OBJ_LOB;
   pLob->connection = connection;
   pLob->type = type;
   pLob->handle = handle;
   return pLob;
}

void sdb_lob_free(SDB_LOBP pLob) {
   if (pLob != NULL) {
      if (pLob->handle != NULL) {
         pLob->connection->dbAPI->releaseLob(pLob);
      }
      SDB_MEM_FREE(pLob);
   }
}

SDB_LOBP sdb_lob_param(int iParam) {
//#ifdef __HBR__
//   SDB_LOBP *pLobPtr = (SDB_LOBP *) hb_parptrGC(&s_gcLobFuncs, iParam);
//#else
//   SDB_LOBP *pLobPtr = (SDB_LOBP *) hb_parptr(iParam);
//#endif
//   return pLobPtr ? *pLobPtr : NULL;
   return (SDB_LOBP) hb_parptr(iParam);
}

SDB_LOBP sdb_lob_itemGet(PHB_ITEM pItem) {
//#ifdef __HBR__
//   SDB_LOBP * pLobPtr = (SDB_LOBP *) hb_itemGetPtrGC(pItem, &s_gcLobFuncs);
//#else
//   SDB_LOBP * pLobPtr = (SDB_LOBP *) hb_itemGetPtrGC(pItem, lob_destructor);
//#endif
//   return pLobPtr ? *pLobPtr : NULL;
   return (SDB_LOBP) hb_itemGetPtr(pItem);
}

PHB_ITEM sdb_lob_itemPut(PHB_ITEM pItem, SDB_LOBP pLob) {
//   SDB_LOBP pCurrLob = sdb_lob_itemGet(pItem);
//   SDB_LOBP *pLobPtr;
//
//   if (pCurrLob != NULL && pLob != NULL && pCurrLob->handle == pLob->handle) {
//      pCurrLob->handle = NULL;
//   }
//#ifdef __HBR__
//   pLobPtr = (SDB_LOBP *) hb_gcAllocate(sizeof(SDB_LOBP), &s_gcLobFuncs);
//#else
//   pLobPtr = (SDB_LOBP *) hb_gcAlloc(sizeof(SDB_LOBP), lob_destructor);
//#endif
//   *pLobPtr = pLob;
//   return hb_itemPutPtrGC(pItem, pLobPtr);
   return hb_itemPutPtr(pItem, pLob);
}

SDB_LOBP sdb_lob_itemDetach(PHB_ITEM pItem) {
//#ifdef __HBR__
//   SDB_LOBP *pLobPtr = (SDB_LOBP *) hb_itemGetPtrGC(pItem, &s_gcLobFuncs);
//#else
//   SDB_LOBP *pLobPtr = (SDB_LOBP *) hb_itemGetPtrGC(pItem, lob_destructor);
//#endif
//   SDB_LOBP pResult = NULL;
//
//   if (pLobPtr) {
//      pResult = *pLobPtr;
//      *pLobPtr = NULL;
//   }
//   return pResult;
   return (SDB_LOBP) hb_itemGetPtr(pItem);
}

CFL_BOOL sdb_lob_open(SDB_LOBP pLob) {
   CFL_UINT32 errorCount = sdb_thread_errorCount();
   pLob->connection->dbAPI->openLob(pLob);
   return ! sdb_thread_hasNewErrors(errorCount);
}

CFL_BOOL sdb_lob_close(SDB_LOBP pLob) {
   CFL_UINT32 errorCount = sdb_thread_errorCount();
   if (pLob->handle != NULL) {
      pLob->connection->dbAPI->closeLob(pLob);
   }
   return ! sdb_thread_hasNewErrors(errorCount);
}

CFL_BOOL sdb_lob_read(SDB_LOBP pLob, const char *buffer, CFL_UINT64 offset, CFL_UINT64 *amount) {
   CFL_UINT32 errorCount = sdb_thread_errorCount();
   pLob->connection->dbAPI->readLob(pLob, buffer, offset, amount);
   return ! sdb_thread_hasNewErrors(errorCount);
}

CFL_BOOL sdb_lob_write(SDB_LOBP pLob, const char *buffer, CFL_UINT64 offset, CFL_UINT64 amount) {
   CFL_UINT32 errorCount = sdb_thread_errorCount();
   pLob->connection->dbAPI->writeLob(pLob, buffer, offset, amount);
   return ! sdb_thread_hasNewErrors(errorCount);
}
