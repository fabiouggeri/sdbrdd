#ifndef SDB_EXPR_H_

#define SDB_EXPR_H_

#include "cfl_types.h"
#include "cfl_str.h"
#include "cfl_list.h"

#include "sdb_defs.h"

#define SDB_EXPR_ANY        1
#define SDB_EXPR_CONDITION  2
#define SDB_EXPR_EXPRESSION 4
#define SDB_EXPR_TRIGGER    8

enum _SDB_EXPRESSION_TYPE {
   EXPRESSION,
   FUNCTION_CALL,
   AREA_FIELD,
   AND_EXPR,
   OR_EXPR,
   PREFIX_EXPR,
   STRING_LITERAL,
   NUMBER_LITERAL,
   LOGICAL_LITERAL,
   DATE_LITERAL,
   MATH_EXPR,
   RELATIONAL_EXPR,
   IIF_EXPR,
   VAR_EXPR,
   PAR_EXPR
};

struct _SDB_EXPRESSION_NODE {
   SDB_EXPRESSION_TYPE type;
   CFL_STRP value;
   CFL_UINT8 dataType;
   CFL_UINT32 argsCount;
   SDB_EXPRESSION_NODEP parent;
   SDB_EXPRESSION_NODEP firstArg;
   SDB_EXPRESSION_NODEP lastArg;
   SDB_EXPRESSION_NODEP nextArg;
};

struct _SDB_EXPRESSION_TRANSLATION {
   CFL_STRP   name;
   CFL_STRP   expression;
   CFL_UINT8  dataType;
   CFL_UINT32 parCount;
};

extern SDB_EXPRESSION_NODEP sdb_expr_new(SDB_EXPRESSION_TYPE type);
extern SDB_EXPRESSION_NODEP sdb_expr_newValue(SDB_EXPRESSION_TYPE type, const char *expr, CFL_UINT32 exprLen);
extern void sdb_expr_free(SDB_EXPRESSION_NODEP oper);
extern SDB_EXPRESSION_TYPE sdb_expr_getType(SDB_EXPRESSION_NODEP oper);
extern CFL_STRP sdb_expr_getValue(SDB_EXPRESSION_NODEP oper);
extern SDB_EXPRESSION_NODEP sdb_expr_getParent(SDB_EXPRESSION_NODEP oper);
extern SDB_EXPRESSION_NODEP sdb_expr_getFirstArg(SDB_EXPRESSION_NODEP oper);
extern SDB_EXPRESSION_NODEP sdb_expr_getLastArg(SDB_EXPRESSION_NODEP oper);
extern SDB_EXPRESSION_NODEP sdb_expr_getNextArg(SDB_EXPRESSION_NODEP arg);
extern void sdb_expr_addArg(SDB_EXPRESSION_NODEP oper, SDB_EXPRESSION_NODEP arg);
extern CFL_UINT32 sdb_expr_getArgsCount(SDB_EXPRESSION_NODEP oper);
extern SDB_EXPRESSION_NODEP sdb_expr_clipperExpression(const char *expr, CFL_UINT32 exprLen);

extern SDB_EXPRESSION_TRANSLATIONP sdb_expr_translationNew(const char *name, CFL_UINT8 dataType, CFL_UINT32 parCount, const char *expression);
extern void sdb_expr_translationFree(SDB_EXPRESSION_TRANSLATIONP trans);
extern CFL_BOOL sdb_expr_translate(SDB_EXPRESSION_TRANSLATIONP trans, CFL_STRP str, CFL_LISTP args);

#endif

