#include <stdlib.h>

#include "hbapi.h"
#include "hbapiitm.h"

#include "sdb_connection.h"
#include "sdb_util.h"
#include "sdb_table.h"
#include "sdb_field.h"
#include "sdb_area.h"
#include "sdb_log.h"
#include "cfl_str.h"
#include "sdb_expr.h"
#include "sdb_product.h"
#include "cfl_list.h"

static CFL_BOOL clpExpressionToSql(SDB_AREAP pSDBArea, SDB_EXPRESSION_NODEP expr, CFL_STRP str, CFL_UINT8 exprType);
static void defineExpressionDataType(SDB_AREAP pSDBArea, SDB_EXPRESSION_NODEP expr);

static void clpExprArgsDataType(SDB_AREAP pSDBArea, SDB_EXPRESSION_NODEP expr) {
   SDB_EXPRESSION_NODEP arg = sdb_expr_getFirstArg(expr);
   while (arg) {
      defineExpressionDataType(pSDBArea, arg);
      arg = sdb_expr_getNextArg(arg);
   }
}

// ALLTRIM, DTOS, LEFT, RIGHT, STR, STRZERO, SUBSTR, YEAR
static CFL_BOOL clpFunctionCallToSql(SDB_AREAP pSDBArea, SDB_EXPRESSION_NODEP expr, CFL_STRP str, CFL_UINT8 exprType) {
   SDB_PRODUCTP product;
   CFL_UINT32 numArgs;
   SDB_EXPRESSION_TRANSLATIONP trans;
   
   product = pSDBArea->connection->product;
   numArgs = sdb_expr_getArgsCount(expr);
   trans = sdb_product_getTranslation(product, cfl_str_getPtr(expr->value), numArgs);
   if (trans) {
      CFL_LISTP args = cfl_list_new(numArgs);
      SDB_EXPRESSION_NODEP arg = sdb_expr_getFirstArg(expr);
      CFL_BOOL bSuccess = CFL_TRUE;
      CFL_UINT32 len;
      CFL_UINT32 i;
      while (arg && bSuccess) {
         CFL_STRP strArg = cfl_str_new(100);
         if (clpExpressionToSql(pSDBArea, arg, strArg, exprType)) {
            cfl_list_add(args, strArg);
         } else {
            bSuccess = CFL_FALSE;
         }
         arg = sdb_expr_getNextArg(arg);
      }
      if (bSuccess) {
         bSuccess = sdb_expr_translate(trans, str, args);
      }
      len = cfl_list_length(args);
      for (i = 0; i < len; i++) {
         CFL_STRP str = (CFL_STRP )cfl_list_get(args, i);
         if (str) {
            cfl_str_free(str);
         }
      }
      cfl_list_free(args);
      return bSuccess;
   }
   return CFL_FALSE;
}

static void clpFunctionCallDataType(SDB_AREAP pSDBArea, SDB_EXPRESSION_NODEP expr) {
   SDB_PRODUCTP product;
   CFL_UINT32 numArgs;
   SDB_EXPRESSION_TRANSLATIONP trans;
   
   clpExprArgsDataType(pSDBArea, expr);
   product = pSDBArea->connection->product;
   numArgs = sdb_expr_getArgsCount(expr);
   trans = sdb_product_getTranslation(product, cfl_str_getPtr(expr->value), numArgs);
   if (trans) {
      expr->dataType = trans->dataType;
   }
}

static CFL_BOOL isParentRelational(SDB_EXPRESSION_NODEP expr) {
   SDB_EXPRESSION_NODEP parent = expr->parent;
   while (parent && parent->type == PAR_EXPR) {
      parent = parent->parent;
   }
   return parent != NULL && parent->type == RELATIONAL_EXPR;
}

static CFL_BOOL clpVarToSql(SDB_AREAP pSDBArea, SDB_EXPRESSION_NODEP expr, CFL_STRP str, CFL_UINT8 exprType) {
   if (cfl_str_indexOfBuffer(expr->value, "->", 2, 0) < 0) {
      SDB_FIELDP field = sdb_table_getField(pSDBArea->table, cfl_str_getPtr(expr->value));
      if (field) {
         if (exprType & SDB_EXPR_TRIGGER) {
            if (field->clpType == SDB_CLP_CHARACTER) {
               char fieldLen[33];
               CFL_STR_APPEND_CONST(str, "RPAD(:NEW.");
               cfl_str_appendStr(str, field->dbName);
               cfl_str_appendChar(str, ',');
               sprintf(fieldLen, "%u", field->length);
               cfl_str_append(str, fieldLen, NULL);
               cfl_str_appendChar(str, ')');
            } else {
               CFL_STR_APPEND_CONST(str, ":NEW.");
               cfl_str_appendStr(str, field->dbName);
            }
         } else {
            cfl_str_appendStr(str, field->dbName);
         }
         if (field->clpType == SDB_CLP_LOGICAL && ! isParentRelational(expr)) {
            CFL_STR_APPEND_CONST(str, "= 'Y'");
         }
         return CFL_TRUE;
      }
   }
   return CFL_FALSE;
}

static void clpVarDataType(SDB_AREAP pSDBArea, SDB_EXPRESSION_NODEP expr) {
   clpExprArgsDataType(pSDBArea, expr);
   if (cfl_str_indexOfBuffer(expr->value, "->", 2, 0) < 0) {
      SDB_FIELDP field = sdb_table_getField(pSDBArea->table, cfl_str_getPtr(expr->value));
      if (field) {
         expr->dataType = field->clpType;
      }
   }
}

static CFL_BOOL clpFieldToSql(SDB_AREAP pSDBArea, SDB_EXPRESSION_NODEP expr, CFL_STRP str, CFL_UINT8 exprType) {
   CFL_INT32 pos = cfl_str_indexOfBuffer(expr->value, "->", 2, 0);
   CFL_STRP fieldName;
   SDB_FIELDP field;
   if (pos > 0) {
      CFL_STRP alias = cfl_str_substr(expr->value, 0, pos);
      if (! cfl_str_bufferEqualsIgnoreCase(alias, sdb_area_getAlias(pSDBArea))) {
         cfl_str_free(alias);
         return CFL_FALSE;
      }
      cfl_str_free(alias);
      fieldName = cfl_str_substr(expr->value, pos + 2, cfl_str_getLength(expr->value));
   } else {
      fieldName = cfl_str_newStr(expr->value);
   }
   field = sdb_table_getField(pSDBArea->table, cfl_str_getPtr(fieldName));
   cfl_str_free(fieldName);
   if (field) {
      if (exprType & SDB_EXPR_TRIGGER) {
         if (field->clpType == SDB_CLP_CHARACTER) {
            char fieldLen[33];
            CFL_STR_APPEND_CONST(str, "RPAD(:NEW.");
            cfl_str_appendStr(str, field->dbName);
            cfl_str_appendChar(str, ',');
            sprintf(fieldLen, "%u", field->length);
            cfl_str_append(str, fieldLen, NULL);
            cfl_str_appendChar(str, ')');
         } else {
            CFL_STR_APPEND_CONST(str, ":NEW.");
            cfl_str_appendStr(str, field->dbName);
         }
      } else {
         cfl_str_appendStr(str, field->dbName);
      }
      if (field->clpType == SDB_CLP_LOGICAL && ! isParentRelational(expr)) {
         CFL_STR_APPEND_CONST(str, "= 'Y'");
      }
      return CFL_TRUE;
   }
   return CFL_FALSE;
}

static void clpFieldDataType(SDB_AREAP pSDBArea, SDB_EXPRESSION_NODEP expr) {
   CFL_INT32 pos = cfl_str_indexOfBuffer(expr->value, "->", 2, 0);
   CFL_STRP fieldName;
   SDB_FIELDP field;
   clpExprArgsDataType(pSDBArea, expr);
   if (pos > 0) {
      CFL_STRP alias = cfl_str_substr(expr->value, 0, pos);
      if (! cfl_str_bufferEqualsIgnoreCase(alias, sdb_area_getAlias(pSDBArea))) {
         cfl_str_free(alias);
         return;
      }
      cfl_str_free(alias);
      fieldName = cfl_str_substr(expr->value, pos + 2, cfl_str_getLength(expr->value));
   } else {
      fieldName = cfl_str_newStr(expr->value);
   }
   field = sdb_table_getField(pSDBArea->table, cfl_str_getPtr(fieldName));
   cfl_str_free(fieldName);
   if (field) {
      expr->dataType = field->clpType;
   }
}

static CFL_BOOL clpAndExprToSql(SDB_AREAP pSDBArea, SDB_EXPRESSION_NODEP expr, CFL_STRP str, CFL_UINT8 exprType) {
   if (sdb_expr_getArgsCount(expr) == 2) {
      SDB_EXPRESSION_NODEP left = sdb_expr_getFirstArg(expr);
      if (clpExpressionToSql(pSDBArea, left, str, exprType)) {
         CFL_STR_APPEND_CONST(str, " AND ");
         return clpExpressionToSql(pSDBArea, sdb_expr_getNextArg(left), str, exprType);
      }
   }
   return CFL_FALSE;
}

static CFL_BOOL clpOrExprToSql(SDB_AREAP pSDBArea, SDB_EXPRESSION_NODEP expr, CFL_STRP str, CFL_UINT8 exprType) {
   if (sdb_expr_getArgsCount(expr) == 2) {
      SDB_EXPRESSION_NODEP left = sdb_expr_getFirstArg(expr);
      if (clpExpressionToSql(pSDBArea, left, str, exprType)) {
         CFL_STR_APPEND_CONST(str, " OR ");
         return clpExpressionToSql(pSDBArea, sdb_expr_getNextArg(left), str, exprType);
      }
   }
   return CFL_FALSE;
}

static CFL_BOOL clpPrefixExprToSql(SDB_AREAP pSDBArea, SDB_EXPRESSION_NODEP expr, CFL_STRP str, CFL_UINT8 exprType) {
   if (sdb_expr_getArgsCount(expr) == 1) {
      SDB_EXPRESSION_NODEP arg = sdb_expr_getFirstArg(expr);
      if (cfl_str_bufferEqualsIgnoreCase(expr->value, "!") ||  cfl_str_bufferEqualsIgnoreCase(expr->value, ".NOT.")) {
         CFL_STR_APPEND_CONST(str, "NOT ");
         return clpExpressionToSql(pSDBArea, arg, str, exprType);
      } else if (cfl_str_bufferEqualsIgnoreCase(expr->value, "-")) {
         cfl_str_appendChar(str, '-');
         return clpExpressionToSql(pSDBArea, arg, str, exprType);
      } else if (cfl_str_bufferEqualsIgnoreCase(expr->value, "+")) {
         cfl_str_appendChar(str, '-');
         return clpExpressionToSql(pSDBArea, arg, str, exprType);
      }
   }
   return CFL_FALSE;
}

static void clpPrefixExprDataType(SDB_AREAP pSDBArea, SDB_EXPRESSION_NODEP expr) {
   clpExprArgsDataType(pSDBArea, expr);
   if (cfl_str_bufferEqualsIgnoreCase(expr->value, "!") ||  cfl_str_bufferEqualsIgnoreCase(expr->value, ".NOT.")) {
      expr->dataType = SDB_CLP_LOGICAL;
   } else if (cfl_str_bufferEqualsIgnoreCase(expr->value, "-")) {
      expr->dataType = SDB_CLP_NUMERIC;
   } else if (cfl_str_bufferEqualsIgnoreCase(expr->value, "+")) {
      expr->dataType = SDB_CLP_NUMERIC;
   }
}

static CFL_BOOL clpDateToSql(SDB_EXPRESSION_NODEP expr, CFL_STRP str) {
   if (sdb_expr_getArgsCount(expr) == 0) {
      if (cfl_str_bufferStartsWithIgnoreCase(expr->value, "0d")) {
         CFL_STRP subs = cfl_str_substr(str, 2, cfl_str_getLength(expr->value));
         CFL_STR_APPEND_CONST(str, "TO_CHAR(");
         cfl_str_appendStr(str, subs);
         CFL_STR_APPEND_CONST(str, ",'YYYYMMDD')");
         cfl_str_free(subs);
         return CFL_TRUE;
      }
   }
   return CFL_FALSE;
}

static CFL_BOOL clpMathExprToSql(SDB_AREAP pSDBArea, SDB_EXPRESSION_NODEP expr, CFL_STRP str, CFL_UINT8 exprType) {
   if (sdb_expr_getArgsCount(expr) == 2) {
      SDB_EXPRESSION_NODEP left = sdb_expr_getFirstArg(expr);
      SDB_EXPRESSION_NODEP right = sdb_expr_getNextArg(left);
      if (cfl_str_bufferEquals(expr->value, "**")) {
         CFL_STR_APPEND_CONST(str, "POWER(");
         if (clpExpressionToSql(pSDBArea, left, str, exprType)) {
            cfl_str_appendChar(str, ',');
            if (clpExpressionToSql(pSDBArea, right, str, exprType)) {
               cfl_str_appendChar(str, ')');
               return CFL_TRUE;
            }
         }
      } else if (cfl_str_bufferEquals(expr->value, "%")) {
         CFL_STR_APPEND_CONST(str, "MOD(");
         if (clpExpressionToSql(pSDBArea, left, str, exprType)) {
            cfl_str_appendChar(str, ',');
            if (clpExpressionToSql(pSDBArea, right, str, exprType)) {
               cfl_str_appendChar(str, ')');
               return CFL_TRUE;
            }
         }
      } else if (cfl_str_bufferEquals(expr->value, "-")) {
         if (left->dataType == SDB_CLP_CHARACTER || right->dataType == SDB_CLP_CHARACTER) {
            CFL_STR_APPEND_CONST(str, "RTRIM(");
            if (clpExpressionToSql(pSDBArea, left, str, exprType)) {
               CFL_STR_APPEND_CONST(str, ")||");
               if (clpExpressionToSql(pSDBArea, right, str, exprType)) {
                  CFL_STR_APPEND_CONST(str, "||SUBSTR(");
                  if (clpExpressionToSql(pSDBArea, left, str, exprType)) {
                     CFL_STR_APPEND_CONST(str, ",LENGTH(RTRIM(");
                     if (clpExpressionToSql(pSDBArea, left, str, exprType)) {
                        CFL_STR_APPEND_CONST(str, "))+1)");
                        return CFL_TRUE;
                     }
                  }
               }
            }
         } else {
            CFL_STR_APPEND_CONST(str, " - ");
         }
      } else if (clpExpressionToSql(pSDBArea, left, str, exprType)) {
         if (cfl_str_bufferEquals(expr->value, "+")) {
            if (left->dataType == SDB_CLP_CHARACTER || right->dataType == SDB_CLP_CHARACTER) {
               CFL_STR_APPEND_CONST(str, " || ");
            } else {
               CFL_STR_APPEND_CONST(str, " + ");
            }
         } else if (cfl_str_bufferEquals(expr->value, "*")) {
            CFL_STR_APPEND_CONST(str, " * ");
         } else if (cfl_str_bufferEquals(expr->value, "/")) {
            CFL_STR_APPEND_CONST(str, " / ");
         } else {
            return CFL_FALSE;
         }
         return clpExpressionToSql(pSDBArea, right, str, exprType);
      }
   }
   return CFL_FALSE;
}

static void clpMathExprDataType(SDB_AREAP pSDBArea, SDB_EXPRESSION_NODEP expr) {
   clpExprArgsDataType(pSDBArea, expr);
   if (cfl_str_bufferEquals(expr->value, "**")) {
      expr->dataType = SDB_CLP_NUMERIC;
   } else if (cfl_str_bufferEquals(expr->value, "%")) {
      expr->dataType = SDB_CLP_NUMERIC;
   } else if (cfl_str_bufferEquals(expr->value, "-") || cfl_str_bufferEquals(expr->value, "+")) {
      SDB_EXPRESSION_NODEP left = sdb_expr_getFirstArg(expr);
      SDB_EXPRESSION_NODEP right = sdb_expr_getNextArg(left);
      if (left->dataType == SDB_CLP_UNKNOWN) {
         expr->dataType = right->dataType;
      } else {
         expr->dataType = left->dataType;
      }
   } else if (cfl_str_bufferEquals(expr->value, "*")) {
      expr->dataType = SDB_CLP_NUMERIC;
   } else if (cfl_str_bufferEquals(expr->value, "/")) {
      expr->dataType = SDB_CLP_NUMERIC;
   }
}

static CFL_BOOL clpRelationalExprToSql(SDB_AREAP pSDBArea, SDB_EXPRESSION_NODEP expr, CFL_STRP str, CFL_UINT8 exprType) {
   if (sdb_expr_getArgsCount(expr) == 2) {
      SDB_EXPRESSION_NODEP left = sdb_expr_getFirstArg(expr);
      if (cfl_str_bufferEquals(expr->value, "$") || cfl_str_bufferEquals(expr->value, "in")) {
         SDB_EXPRESSION_NODEP right = sdb_expr_getNextArg(left);
         CFL_STR_APPEND_CONST(str, "INSTR(");
         if (clpExpressionToSql(pSDBArea, right, str, exprType)) {
            cfl_str_appendChar(str, ',');
            if (clpExpressionToSql(pSDBArea, left, str, exprType)) {
               CFL_STR_APPEND_CONST(str, ") > 0");
               return CFL_TRUE;
            }
         }
      } else {
         if (clpExpressionToSql(pSDBArea, left, str, exprType)) {
            if (cfl_str_bufferEquals(expr->value, "=")) {
               CFL_STR_APPEND_CONST(str, " = ");
            } else if (cfl_str_bufferEquals(expr->value, ">=")) {
               CFL_STR_APPEND_CONST(str, " >= ");
            } else if (cfl_str_bufferEquals(expr->value, "<=")) {
               CFL_STR_APPEND_CONST(str, " <= ");
            } else if (cfl_str_bufferEquals(expr->value, "<>")) {
               CFL_STR_APPEND_CONST(str, " != ");
            } else if (cfl_str_bufferEquals(expr->value, ">")) {
               CFL_STR_APPEND_CONST(str, " > ");
            } else if (cfl_str_bufferEquals(expr->value, "<")) {
               CFL_STR_APPEND_CONST(str, " < ");
            } else if (cfl_str_bufferEquals(expr->value, "==")) {
               CFL_STR_APPEND_CONST(str, " = ");
            } else if (cfl_str_bufferEquals(expr->value, "!=")) {
               CFL_STR_APPEND_CONST(str, " != ");
            } else if (cfl_str_bufferEquals(expr->value, "#")) {
               CFL_STR_APPEND_CONST(str, " != ");
            } else {
               return CFL_FALSE;
            }
            return clpExpressionToSql(pSDBArea, sdb_expr_getNextArg(left), str, exprType);
         }
      }
   }
   return CFL_FALSE;
}

static CFL_BOOL clpIIfToSql(SDB_AREAP pSDBArea, SDB_EXPRESSION_NODEP expr, CFL_STRP str, CFL_UINT8 exprType) {
   if (sdb_expr_getArgsCount(expr) == 3) {
      SDB_EXPRESSION_NODEP arg = sdb_expr_getFirstArg(expr);
      CFL_STR_APPEND_CONST(str, "CASE WHEN ");
      if (clpExpressionToSql(pSDBArea, arg, str, exprType)) {
         CFL_STR_APPEND_CONST(str, " THEN ");
         arg = sdb_expr_getNextArg(arg);
         if (clpExpressionToSql(pSDBArea, arg, str, exprType)) {
            arg = sdb_expr_getNextArg(arg);
            CFL_STR_APPEND_CONST(str, " ELSE ");
            if (clpExpressionToSql(pSDBArea, arg, str, exprType)) {
               CFL_STR_APPEND_CONST(str, " END");
               return CFL_TRUE;
            }
         }
      }
   }
   return CFL_FALSE;
}

static void clpIIfDataType(SDB_AREAP pSDBArea, SDB_EXPRESSION_NODEP expr) {
   SDB_EXPRESSION_NODEP first = sdb_expr_getNextArg(sdb_expr_getFirstArg(expr));
   SDB_EXPRESSION_NODEP second = sdb_expr_getNextArg(first);
   clpExprArgsDataType(pSDBArea, expr);
   if (first->dataType == SDB_CLP_UNKNOWN) {
      expr->dataType = second->dataType;
   } else {
      expr->dataType = first->dataType;
   }
}

static CFL_BOOL clpParenthesesToSql(SDB_AREAP pSDBArea, SDB_EXPRESSION_NODEP expr, CFL_STRP str, CFL_UINT8 exprType) {
   if (sdb_expr_getArgsCount(expr) == 1) {
      SDB_EXPRESSION_NODEP arg = sdb_expr_getFirstArg(expr);
      cfl_str_appendChar(str, '(');
      if (clpExpressionToSql(pSDBArea, arg, str, exprType)) {
         cfl_str_appendChar(str, ')');
         return CFL_TRUE;
      }
   }
   return CFL_FALSE;
}

static void clpParenthesesDataType(SDB_AREAP pSDBArea, SDB_EXPRESSION_NODEP expr) {
   SDB_EXPRESSION_NODEP first = sdb_expr_getFirstArg(expr);
   defineExpressionDataType(pSDBArea, first);
   expr->dataType = first->dataType;
}

static CFL_BOOL clpExpressionToSql(SDB_AREAP pSDBArea, SDB_EXPRESSION_NODEP expr, CFL_STRP str, CFL_UINT8 exprType) {
   switch(expr->type) {
      case FUNCTION_CALL:
         return clpFunctionCallToSql(pSDBArea, expr, str, exprType);
         
      case AREA_FIELD:
         return clpFieldToSql(pSDBArea, expr, str, exprType);
         
      case VAR_EXPR:
         return clpVarToSql(pSDBArea, expr, str, exprType);
         
      case AND_EXPR:
         return clpAndExprToSql(pSDBArea, expr, str, exprType);
         
      case OR_EXPR:
         return clpOrExprToSql(pSDBArea, expr, str, exprType);
         
      case PREFIX_EXPR:
         return clpPrefixExprToSql(pSDBArea, expr, str, exprType);
         
      case STRING_LITERAL:
         cfl_str_appendChar(str,'\'');
         cfl_str_appendLen(str,cfl_str_getPtr(expr->value), cfl_str_getLength(expr->value));
         cfl_str_appendChar(str,'\'');
         return CFL_TRUE;
         
      case NUMBER_LITERAL:
         cfl_str_appendLen(str,cfl_str_getPtr(expr->value), cfl_str_getLength(expr->value));
         return CFL_TRUE;
         
      case LOGICAL_LITERAL:
         cfl_str_appendChar(str,'\'');
         if (cfl_str_bufferEqualsIgnoreCase(expr->value, ".T.") || cfl_str_bufferEqualsIgnoreCase(expr->value, ".Y.")) {
            cfl_str_appendChar(str,'Y');
         } else {
            cfl_str_appendChar(str,'N');
         }
         cfl_str_appendChar(str,'\'');
         return CFL_TRUE;
         
      case DATE_LITERAL:
         return clpDateToSql(expr, str);

      case MATH_EXPR:
         return clpMathExprToSql(pSDBArea, expr, str, exprType);

      case RELATIONAL_EXPR:
         return clpRelationalExprToSql(pSDBArea, expr, str, exprType);

      case IIF_EXPR:
         return clpIIfToSql(pSDBArea, expr, str, exprType);
         
      case PAR_EXPR:
         return clpParenthesesToSql(pSDBArea, expr, str, exprType);
   }
   return CFL_FALSE;
}

static void defineExpressionDataType(SDB_AREAP pSDBArea, SDB_EXPRESSION_NODEP expr) {
   switch(expr->type) {
      case FUNCTION_CALL:
         clpFunctionCallDataType(pSDBArea, expr);
         break;
         
      case AREA_FIELD:
         clpFieldDataType(pSDBArea, expr);
         break;
         
      case VAR_EXPR:
         clpVarDataType(pSDBArea, expr);
         break;
         
      case RELATIONAL_EXPR:
      case AND_EXPR:
      case OR_EXPR:
         clpExprArgsDataType(pSDBArea, expr);
         expr->dataType = SDB_CLP_LOGICAL;
         break;
         
      case LOGICAL_LITERAL:
         expr->dataType = SDB_CLP_LOGICAL;
         break;
         
      case PREFIX_EXPR:
         clpPrefixExprDataType(pSDBArea, expr);
         break;
         
      case STRING_LITERAL:
         expr->dataType = SDB_CLP_CHARACTER;
         break;
         
      case NUMBER_LITERAL:
         expr->dataType = SDB_CLP_NUMERIC;
         break;
         
      case DATE_LITERAL:
         expr->dataType = SDB_CLP_DATE;
         break;

      case MATH_EXPR:
         clpMathExprDataType(pSDBArea, expr);
         break;

      case IIF_EXPR:
         clpIIfDataType(pSDBArea, expr);
         break;
         
      case PAR_EXPR:
         clpParenthesesDataType(pSDBArea, expr);
         break;
   }
}

PHB_ITEM sdb_ora_clipperToSql(SDB_AREAP pSDBArea, SDB_EXPRESSION_NODEP expr, CFL_UINT8 exprType) {
   PHB_ITEM pSql = NULL;
   SDB_EXPRESSION_NODEP firstExpr;
   SDB_EXPRESSION_NODEP arg;
   if (expr->type == EXPRESSION) {
       firstExpr = sdb_expr_getFirstArg(expr);
   } else {
      firstExpr = expr;
   }
   arg = firstExpr;
   while (arg->type == PAR_EXPR) {
      arg = sdb_expr_getFirstArg(arg);
   }
   
   switch (arg->type) {
      case AND_EXPR:
      case OR_EXPR:
      case RELATIONAL_EXPR:
         if (exprType & SDB_EXPR_ANY || exprType & SDB_EXPR_CONDITION) {
            CFL_STRP sqlExpr = cfl_str_new(256);
            defineExpressionDataType(pSDBArea, firstExpr);
            if (clpExpressionToSql(pSDBArea, firstExpr, sqlExpr, SDB_EXPR_CONDITION | (exprType & SDB_EXPR_TRIGGER))) {
               pSql = hb_itemPutCL(NULL, cfl_str_getPtr(sqlExpr), cfl_str_getLength(sqlExpr));
            }
            cfl_str_free(sqlExpr);
         }
         break;
      default:
         if (exprType & SDB_EXPR_ANY || exprType & SDB_EXPR_EXPRESSION) {
            CFL_STRP sqlExpr = cfl_str_new(256);
            defineExpressionDataType(pSDBArea, firstExpr);
            if (clpExpressionToSql(pSDBArea, firstExpr, sqlExpr, exprType)) {
               if (exprType & SDB_EXPR_TRIGGER) {
                  CFL_STRP triggerExpr = cfl_str_new(cfl_str_getLength(sqlExpr) + 20);
                  CFL_STR_APPEND_CONST(triggerExpr, "NVL(RTRIM(");
                  cfl_str_appendStr(triggerExpr, sqlExpr);
                  CFL_STR_APPEND_CONST(triggerExpr, "), ' ')");
                  pSql = hb_itemPutCL(NULL, cfl_str_getPtr(triggerExpr), cfl_str_getLength(triggerExpr));
                  cfl_str_free(triggerExpr);
               } else {
                  pSql = hb_itemPutCL(NULL, cfl_str_getPtr(sqlExpr), cfl_str_getLength(sqlExpr));
               }
            }
            cfl_str_free(sqlExpr);
         }
         break;
   }
   return pSql;
}

