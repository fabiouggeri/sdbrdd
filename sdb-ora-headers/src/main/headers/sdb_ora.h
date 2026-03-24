#ifndef SDB_ORA_H_

#define SDB_ORA_H_

#include "hbapi.h"
#include "sdb_defs.h"

#define ORA_LCK_NL_MODE  1
#define ORA_LCK_SS_MODE  2
#define ORA_LCK_SX_MODE  3
#define ORA_LCK_S_MODE   4
#define ORA_LCK_SSX_MODE 5
#define ORA_LCK_X_MODE   6

extern PHB_ITEM sdb_ora_clipperToSql(SDB_AREAP pSDBArea, SDB_EXPRESSION_NODEP expr, CFL_UINT8 exprType);
extern SDB_DB_APIP sdb_ora_getAPI(void);

#endif /* SDB_ORA_H_ */

