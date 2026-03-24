#include "cfl_types.h"

#include <hbapi.h>
#include <hbapiitm.h>

#include "yapp_mem_buffer.h"
#include "yapp_visitor.h"
#include "yapp_node.h"
#include "ClipperExpressionParser.h"

#include "sdb_expr.h"
#include "sdb_util.h"

SDB_EXPRESSION_NODEP sdb_expr_new(SDB_EXPRESSION_TYPE type) {
   SDB_EXPRESSION_NODEP expr = (SDB_EXPRESSION_NODEP) SDB_MEM_ALLOC(sizeof(SDB_EXPRESSION_NODE));
   expr->type = type;
   expr->dataType = SDB_CLP_UNKNOWN;
   expr->value = NULL;
   expr->parent = NULL;
   expr->firstArg = NULL;
   expr->lastArg = NULL;
   expr->nextArg = NULL;
   expr->argsCount = 0;
   return expr;
}

SDB_EXPRESSION_NODEP sdb_expr_newValue(SDB_EXPRESSION_TYPE type, const char *text, CFL_UINT32 textLen) {
   SDB_EXPRESSION_NODEP expr = (SDB_EXPRESSION_NODEP) SDB_MEM_ALLOC(sizeof(SDB_EXPRESSION_NODE));
   expr->type = type;
   expr->dataType = SDB_CLP_UNKNOWN;
   expr->value = cfl_str_newBufferLen(text, textLen);
   expr->parent = NULL;
   expr->firstArg = NULL;
   expr->lastArg = NULL;
   expr->nextArg = NULL;
   expr->argsCount = 0;
   return expr;
}

void sdb_expr_free(SDB_EXPRESSION_NODEP expr) {
   if (expr) {
      sdb_expr_free(expr->firstArg);
      sdb_expr_free(expr->nextArg);
      if (expr->value) {
         cfl_str_free(expr->value);
      }
      SDB_MEM_FREE(expr);
   }
}

void sdb_expr_addArg(SDB_EXPRESSION_NODEP expr, SDB_EXPRESSION_NODEP arg) {
   arg->parent = expr;
   ++expr->argsCount;
   if (expr->firstArg) {
      expr->lastArg->nextArg = arg;
      expr->lastArg = arg;
   } else {
      expr->firstArg = arg;
      expr->lastArg = arg;
   }
}

SDB_EXPRESSION_TYPE sdb_expr_getType(SDB_EXPRESSION_NODEP expr) {
   return expr->type;
}

CFL_STRP sdb_expr_getValue(SDB_EXPRESSION_NODEP expr) {
   return expr->value;
}

SDB_EXPRESSION_NODEP sdb_expr_getParent(SDB_EXPRESSION_NODEP expr) {
   return expr->parent;
}

SDB_EXPRESSION_NODEP sdb_expr_getFirstArg(SDB_EXPRESSION_NODEP expr) {
   return expr->firstArg;
}

SDB_EXPRESSION_NODEP sdb_expr_getLastArg(SDB_EXPRESSION_NODEP expr) {
   return expr->lastArg;
}

SDB_EXPRESSION_NODEP sdb_expr_getNextArg(SDB_EXPRESSION_NODEP arg) {
   return arg->nextArg;
}

static void enterStringLiteral(YAPP_VISITOR *visitor, YAPP_NODE *node) {
   SDB_EXPRESSION_NODEP currExpr = (SDB_EXPRESSION_NODEP) visitor->data;
   SDB_EXPRESSION_NODEP newExpr;
   char *text = yapp_node_getTextPointer(node, visitor->buffer);
   ++text;
   newExpr = sdb_expr_newValue(STRING_LITERAL, text, yapp_node_getLength(node) - 2);
   sdb_expr_addArg(currExpr, newExpr);
   visitor->data = newExpr;
}

static void exitStringLiteral(YAPP_VISITOR *visitor, YAPP_NODE *node) {
   SDB_EXPRESSION_NODEP currExpr = (SDB_EXPRESSION_NODEP) visitor->data;
   visitor->data = currExpr->parent;
}

static void enterNumberLiteral(YAPP_VISITOR *visitor, YAPP_NODE *node) {
   SDB_EXPRESSION_NODEP currExpr = (SDB_EXPRESSION_NODEP) visitor->data;
   char *text = yapp_node_getTextPointer(node, visitor->buffer);
   SDB_EXPRESSION_NODEP newExpr = sdb_expr_newValue(NUMBER_LITERAL, text, yapp_node_getLength(node));
   sdb_expr_addArg(currExpr, newExpr);
   visitor->data = newExpr;
}

static void exitNumberLiteral(YAPP_VISITOR *visitor, YAPP_NODE *node) {
   SDB_EXPRESSION_NODEP currExpr = (SDB_EXPRESSION_NODEP) visitor->data;
   visitor->data = currExpr->parent;
}

static void enterLogicalLiteral(YAPP_VISITOR *visitor, YAPP_NODE *node) {
   SDB_EXPRESSION_NODEP currExpr = (SDB_EXPRESSION_NODEP) visitor->data;
   char *text = yapp_node_getTextPointer(node, visitor->buffer);
   SDB_EXPRESSION_NODEP newExpr = sdb_expr_newValue(LOGICAL_LITERAL, text, yapp_node_getLength(node));
   sdb_expr_addArg(currExpr, newExpr);
   visitor->data = newExpr;
}

static void exitLogicalLiteral(YAPP_VISITOR *visitor, YAPP_NODE *node) {
   SDB_EXPRESSION_NODEP currExpr = (SDB_EXPRESSION_NODEP) visitor->data;
   visitor->data = currExpr->parent;
}

static void enterDateLiteral(YAPP_VISITOR *visitor, YAPP_NODE *node) {
   SDB_EXPRESSION_NODEP currExpr = (SDB_EXPRESSION_NODEP) visitor->data;
   char *text = yapp_node_getTextPointer(node, visitor->buffer);
   SDB_EXPRESSION_NODEP newExpr = sdb_expr_newValue(DATE_LITERAL, text, yapp_node_getLength(node));
   sdb_expr_addArg(currExpr, newExpr);
   visitor->data = newExpr;
}

static void exitDateLiteral(YAPP_VISITOR *visitor, YAPP_NODE *node) {
   SDB_EXPRESSION_NODEP currExpr = (SDB_EXPRESSION_NODEP) visitor->data;
   visitor->data = currExpr->parent;
}

static void enterFunctionCall(YAPP_VISITOR *visitor, YAPP_NODE *node) {
   SDB_EXPRESSION_NODEP currExpr = (SDB_EXPRESSION_NODEP) visitor->data;
   YAPP_NODE *nameNode = yapp_node_getFirstSemanticChild(node);
   char *text = yapp_node_getTextPointer(nameNode, visitor->buffer);
   SDB_EXPRESSION_NODEP newExpr = sdb_expr_newValue(FUNCTION_CALL, text, yapp_node_getLength(nameNode));
   sdb_expr_addArg(currExpr, newExpr);
   visitor->data = newExpr;
}

static void exitFunctionCall(YAPP_VISITOR *visitor, YAPP_NODE *node) {
   SDB_EXPRESSION_NODEP currExpr = (SDB_EXPRESSION_NODEP) visitor->data;
   visitor->data = currExpr->parent;
}

static void enterAliasedField(YAPP_VISITOR *visitor, YAPP_NODE *node) {
   SDB_EXPRESSION_NODEP currExpr;
   CFL_STRP text = cfl_str_new(yapp_node_getLength(node));
   SDB_EXPRESSION_NODEP newExpr;
   YAPP_NODE *alias = yapp_node_getFirstSemanticChild(node);
   YAPP_NODE *field = yapp_node_getSemanticSibling(alias);
   currExpr = (SDB_EXPRESSION_NODEP) visitor->data;
   cfl_str_appendLen(text, yapp_node_getTextPointer(alias, visitor->buffer), yapp_node_getLength(alias));
   CFL_STR_APPEND_CONST(text, "->");
   cfl_str_appendLen(text, yapp_node_getTextPointer(field, visitor->buffer), yapp_node_getLength(field));
   newExpr = sdb_expr_new(AREA_FIELD);
   newExpr->value = text;
   sdb_expr_addArg(currExpr, newExpr);
   visitor->data = newExpr;
}

static void exitAliasedField(YAPP_VISITOR *visitor, YAPP_NODE *node) {
   SDB_EXPRESSION_NODEP currExpr = (SDB_EXPRESSION_NODEP) visitor->data;
   visitor->data = currExpr->parent;
}

static void enterAndExpression(YAPP_VISITOR *visitor, YAPP_NODE *node) {
   SDB_EXPRESSION_NODEP currExpr = (SDB_EXPRESSION_NODEP) visitor->data;
   SDB_EXPRESSION_NODEP newExpr = sdb_expr_new(AND_EXPR);
   sdb_expr_addArg(currExpr, newExpr);
   visitor->data = newExpr;
}

static void exitAndExpression(YAPP_VISITOR *visitor, YAPP_NODE *node) {
   SDB_EXPRESSION_NODEP currExpr = (SDB_EXPRESSION_NODEP) visitor->data;
   visitor->data = currExpr->parent;
}

static void enterOrExpression(YAPP_VISITOR *visitor, YAPP_NODE *node) {
   SDB_EXPRESSION_NODEP currExpr = (SDB_EXPRESSION_NODEP) visitor->data;
   SDB_EXPRESSION_NODEP newExpr = sdb_expr_new(OR_EXPR);
   sdb_expr_addArg(currExpr, newExpr);
   visitor->data = newExpr;
}

static void exitOrExpression(YAPP_VISITOR *visitor, YAPP_NODE *node) {
   SDB_EXPRESSION_NODEP currExpr = (SDB_EXPRESSION_NODEP) visitor->data;
   visitor->data = currExpr->parent;
}

static void enterPrefixedExpression(YAPP_VISITOR *visitor, YAPP_NODE *node) {
   SDB_EXPRESSION_NODEP currExpr = (SDB_EXPRESSION_NODEP) visitor->data;
   YAPP_NODE *nameNode = yapp_node_getFirstSemanticChild(node);
   char *text = yapp_node_getTextPointer(nameNode, visitor->buffer);
   SDB_EXPRESSION_NODEP newExpr = sdb_expr_newValue(PREFIX_EXPR, text, yapp_node_getLength(nameNode));
   sdb_expr_addArg(currExpr, newExpr);
   visitor->data = newExpr;
}

static void exitPrefixedExpression(YAPP_VISITOR *visitor, YAPP_NODE *node) {
   SDB_EXPRESSION_NODEP currExpr = (SDB_EXPRESSION_NODEP) visitor->data;
   visitor->data = currExpr->parent;
}

static void enterMathExpression(YAPP_VISITOR *visitor, YAPP_NODE *node) {
   SDB_EXPRESSION_NODEP currExpr = (SDB_EXPRESSION_NODEP) visitor->data;
   YAPP_NODE *operatorNode = yapp_node_getSemanticSibling(yapp_node_getFirstSemanticChild(node));
   char *text = yapp_node_getTextPointer(operatorNode, visitor->buffer);
   SDB_EXPRESSION_NODEP newExpr = sdb_expr_newValue(MATH_EXPR, text, yapp_node_getLength(operatorNode));
   sdb_expr_addArg(currExpr, newExpr);
   visitor->data = newExpr;
}

static void exitMathExpression(YAPP_VISITOR *visitor, YAPP_NODE *node) {
   SDB_EXPRESSION_NODEP currExpr = (SDB_EXPRESSION_NODEP) visitor->data;
   visitor->data = currExpr->parent;
}

static void enterRelationalExpression(YAPP_VISITOR *visitor, YAPP_NODE *node) {
   SDB_EXPRESSION_NODEP currExpr = (SDB_EXPRESSION_NODEP) visitor->data;
   YAPP_NODE *operatorNode = yapp_node_getSemanticSibling(yapp_node_getFirstSemanticChild(node));
   char *text = yapp_node_getTextPointer(operatorNode, visitor->buffer);
   SDB_EXPRESSION_NODEP newExpr = sdb_expr_newValue(RELATIONAL_EXPR, text, yapp_node_getLength(operatorNode));
   sdb_expr_addArg(currExpr, newExpr);
   visitor->data = newExpr;
}

static void exitRelationalExpression(YAPP_VISITOR *visitor, YAPP_NODE *node) {
   SDB_EXPRESSION_NODEP currExpr = (SDB_EXPRESSION_NODEP) visitor->data;
   visitor->data = currExpr->parent;
}

static void enterIIfExpression(YAPP_VISITOR *visitor, YAPP_NODE *node) {
   SDB_EXPRESSION_NODEP currExpr = (SDB_EXPRESSION_NODEP) visitor->data;
   SDB_EXPRESSION_NODEP newExpr = sdb_expr_new(IIF_EXPR);
   sdb_expr_addArg(currExpr, newExpr);
   visitor->data = newExpr;
}

static void exitIIfExpression(YAPP_VISITOR *visitor, YAPP_NODE *node) {
   SDB_EXPRESSION_NODEP currExpr = (SDB_EXPRESSION_NODEP) visitor->data;
   visitor->data = currExpr->parent;
}

static void enterVariableExpression(YAPP_VISITOR *visitor, YAPP_NODE *node) {
   SDB_EXPRESSION_NODEP currExpr = (SDB_EXPRESSION_NODEP) visitor->data;
   YAPP_NODE *nameNode = yapp_node_getFirstSemanticChild(node);
   char *text = yapp_node_getTextPointer(nameNode, visitor->buffer);
   SDB_EXPRESSION_NODEP newExpr = sdb_expr_newValue(VAR_EXPR, text, yapp_node_getLength(nameNode));
   sdb_expr_addArg(currExpr, newExpr);
   visitor->data = newExpr;
}

static void exitVariableExpression(YAPP_VISITOR *visitor, YAPP_NODE *node) {
   SDB_EXPRESSION_NODEP currExpr = (SDB_EXPRESSION_NODEP) visitor->data;
   visitor->data = currExpr->parent;
}

static void enterParExpression(YAPP_VISITOR *visitor, YAPP_NODE *node) {
   SDB_EXPRESSION_NODEP currExpr = (SDB_EXPRESSION_NODEP) visitor->data;
   SDB_EXPRESSION_NODEP newExpr = sdb_expr_new(PAR_EXPR);
   sdb_expr_addArg(currExpr, newExpr);
   visitor->data = newExpr;
}

static void exitParExpression(YAPP_VISITOR *visitor, YAPP_NODE *node) {
   SDB_EXPRESSION_NODEP currExpr = (SDB_EXPRESSION_NODEP) visitor->data;
   visitor->data = currExpr->parent;
}

CFL_UINT32 sdb_expr_getArgsCount(SDB_EXPRESSION_NODEP oper) {
   return oper->argsCount;
}

SDB_EXPRESSION_NODEP sdb_expr_clipperExpression(const char *expr, CFL_UINT32 exprLen) {
   YAPP_BUFFER *buffer;
   ClipperExpressionParser *parser;
   YAPP_NODE *node;
   YAPP_VISITOR* visitor;
   SDB_EXPRESSION_NODEP exprOper = NULL;

   buffer = yapp_mem_buffer_newLen(expr, exprLen);
   parser = clipperExpressionParser_new(buffer);
   visitor = yapp_visitor_new(buffer, clipperExpressionParser_rulesCount(parser));

   /* Visitor functions */
   yapp_visitor_setEnterRule(visitor, &CLIPPEREXPRESSION_STRING_LITERAL, enterStringLiteral);
   yapp_visitor_setExitRule(visitor, &CLIPPEREXPRESSION_STRING_LITERAL, exitStringLiteral);
   yapp_visitor_setEnterRule(visitor, &CLIPPEREXPRESSION_NUMBER_LITERAL, enterNumberLiteral);
   yapp_visitor_setExitRule(visitor, &CLIPPEREXPRESSION_NUMBER_LITERAL, exitNumberLiteral);
   yapp_visitor_setEnterRule(visitor, &CLIPPEREXPRESSION_LOGICAL_LITERAL, enterLogicalLiteral);
   yapp_visitor_setExitRule(visitor, &CLIPPEREXPRESSION_LOGICAL_LITERAL, exitLogicalLiteral);
   yapp_visitor_setEnterRule(visitor, &CLIPPEREXPRESSION_DATE_TIME_LITERAL, enterDateLiteral);
   yapp_visitor_setExitRule(visitor, &CLIPPEREXPRESSION_DATE_TIME_LITERAL, exitDateLiteral);
   yapp_visitor_setEnterRule(visitor, &CLIPPEREXPRESSION_FUNCTION_CALL, enterFunctionCall);
   yapp_visitor_setExitRule(visitor, &CLIPPEREXPRESSION_FUNCTION_CALL, exitFunctionCall);
   yapp_visitor_setEnterRule(visitor, &CLIPPEREXPRESSION_ALIASED_FIELD, enterAliasedField);
   yapp_visitor_setExitRule(visitor, &CLIPPEREXPRESSION_ALIASED_FIELD, exitAliasedField);
   yapp_visitor_setEnterRule(visitor, &CLIPPEREXPRESSION_AND_EXPRESSION, enterAndExpression);
   yapp_visitor_setExitRule(visitor, &CLIPPEREXPRESSION_AND_EXPRESSION, exitAndExpression);
   yapp_visitor_setEnterRule(visitor, &CLIPPEREXPRESSION_OR_EXPRESSION, enterOrExpression);
   yapp_visitor_setExitRule(visitor, &CLIPPEREXPRESSION_OR_EXPRESSION, exitOrExpression);
   yapp_visitor_setEnterRule(visitor, &CLIPPEREXPRESSION_PREFIXED_EXPRESSION, enterPrefixedExpression);
   yapp_visitor_setExitRule(visitor, &CLIPPEREXPRESSION_PREFIXED_EXPRESSION, exitPrefixedExpression);
   yapp_visitor_setEnterRule(visitor, &CLIPPEREXPRESSION_RELATIONAL_EXPRESSION, enterRelationalExpression);
   yapp_visitor_setExitRule(visitor, &CLIPPEREXPRESSION_RELATIONAL_EXPRESSION, exitRelationalExpression);
   yapp_visitor_setEnterRule(visitor, &CLIPPEREXPRESSION_MATH_EXPRESSION, enterMathExpression);
   yapp_visitor_setExitRule(visitor, &CLIPPEREXPRESSION_MATH_EXPRESSION, exitMathExpression);
   yapp_visitor_setEnterRule(visitor, &CLIPPEREXPRESSION_IF_IN_LINE, enterIIfExpression);
   yapp_visitor_setExitRule(visitor, &CLIPPEREXPRESSION_IF_IN_LINE, exitIIfExpression);
   yapp_visitor_setEnterRule(visitor, &CLIPPEREXPRESSION_VARIABLE, enterVariableExpression);
   yapp_visitor_setExitRule(visitor, &CLIPPEREXPRESSION_VARIABLE, exitVariableExpression);
   yapp_visitor_setEnterRule(visitor, &CLIPPEREXPRESSION_PARENTHESES_EXPRESSION, enterParExpression);
   yapp_visitor_setExitRule(visitor, &CLIPPEREXPRESSION_PARENTHESES_EXPRESSION, exitParExpression);

   node = clipperExpressionParser_parse(parser);
   if (node != NULL) {
      exprOper = sdb_expr_new(EXPRESSION);
      visitor->data = exprOper;
      yapp_visitor_treeWalker(visitor, node);
   } else {
      sdb_expr_free(exprOper);
      exprOper = NULL;
   }
   yapp_visitor_free(visitor);
   clipperExpressionParser_free(parser);
   yapp_buffer_free(buffer);
   return exprOper;
}

SDB_EXPRESSION_TRANSLATIONP sdb_expr_translationNew(const char *name, CFL_UINT8 dataType, CFL_UINT32 parCount, const char *expression) {
   SDB_EXPRESSION_TRANSLATIONP trans = (SDB_EXPRESSION_TRANSLATIONP) SDB_MEM_ALLOC(sizeof(SDB_EXPRESSION_TRANSLATION));
   trans->name = cfl_str_newBuffer(name);
   trans->expression = cfl_str_newBuffer(expression);
   trans->dataType = dataType;
   trans->parCount = parCount;
   return trans;
}

void sdb_expr_translationFree(SDB_EXPRESSION_TRANSLATIONP trans) {
   if (trans->name) {
      cfl_str_free(trans->name);
   }
   if (trans->expression) {
      cfl_str_free(trans->expression);
   }
   SDB_MEM_FREE(trans);
}

CFL_BOOL sdb_expr_translate(SDB_EXPRESSION_TRANSLATIONP trans, CFL_STRP str, CFL_LISTP args) {
   char *start = cfl_str_getPtr(trans->expression);
   char *curr = start;
   if (trans->parCount == cfl_list_length(args)) {
      do {
         while (*curr != '#' && *curr != '\0') {
            ++curr;
         }
         cfl_str_appendLen(str, start, (int) (curr - start));
         if (*curr == '#') {
            int argPos = 0;
            ++curr;
            if (*curr != '#') {
               while (*curr >= '0' && *curr <= '9') {
                  argPos = (argPos * 10) + (*curr - '0');
                  ++curr;
               }
               if (argPos > 0) {
                  CFL_STRP arg = (CFL_STRP) cfl_list_get(args, argPos - 1);
                  if (arg) {
                     cfl_str_appendStr(str, arg);
                  } else {
                     return CFL_FALSE;
                  }
               } else {
                  return CFL_FALSE;
               }
            } else {
               cfl_str_appendChar(str, '#');
               ++curr;
            }
         }
         start = curr;
      } while (*curr != '\0');
      return CFL_TRUE;
   }
   return CFL_FALSE;
}

HB_FUNC(SDB_EXPR_PRINTTREE) {
   PHB_ITEM pExpr = hb_param(1, HB_IT_STRING);
   if (pExpr) {
      YAPP_BUFFER *buffer;
      ClipperExpressionParser *parser;
      YAPP_NODE *node;

      printf("\nExpression: %.*s\n", (int) hb_itemGetCLen(pExpr), hb_itemGetCPtr(pExpr));
      buffer = yapp_mem_buffer_newLen(hb_itemGetCPtr(pExpr), (INT32) hb_itemGetCLen(pExpr));
      parser = clipperExpressionParser_new(buffer);
      node = clipperExpressionParser_parse(parser);
      if (node != NULL) {
         yapp_node_printTree(buffer, node);
      }
      clipperExpressionParser_free(parser);
      yapp_buffer_free(buffer);
   }
}
