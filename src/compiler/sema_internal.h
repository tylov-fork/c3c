#pragma once

// Copyright (c) 2020 Christoffer Lerno. All rights reserved.
// Use of this source code is governed by a LGPLv3.0
// a copy of which can be found in the LICENSE file.


#include "compiler_internal.h"

int sema_check_comp_time_bool(SemaContext *context, Expr *expr);
bool sema_analyse_function_body(SemaContext *context, Decl *func);
#define SCOPE_OUTER_START \
do {                                  \
  DynamicScope stored_scope = context->active_scope; \
  context_change_scope_with_flags(context, SCOPE_NONE);
#define SCOPE_OUTER_END \
  assert(context->active_scope.defer_last == context->active_scope.defer_start); \
  context->active_scope = stored_scope;  \
  } while(0)
#define SCOPE_START SCOPE_START_WITH_FLAGS(SCOPE_NONE)
#define SCOPE_START_WITH_FLAGS(flags) \
 do {                                  \
  DynamicScope old_scope = context->active_scope; \
  context_change_scope_with_flags(context, flags);
#define SCOPE_START_WITH_LABEL(label) \
 do {                                  \
  DynamicScope old_scope = context->active_scope; \
  context_change_scope_for_label(context, label);
#define SCOPE_END \
  assert(context->active_scope.defer_last == context->active_scope.defer_start); \
  context->active_scope = old_scope;  \
  } while(0)
#define SCOPE_POP_ERROR() ((bool)(context->active_scope = old_scope, false))
#define SCOPE_ERROR_END_OUTER() \
  do { context->active_scope = stored_scope; } while(0)

void context_pop_defers_to(SemaContext *context, DeferList *list);
Expr *context_pop_defers_and_wrap_expr(SemaContext *context, Expr *expr);
void context_pop_defers_and_replace_expr(SemaContext *context, Expr *expr);
void context_pop_defers_and_replace_ast(SemaContext *context, Ast *ast);
void context_change_scope_for_label(SemaContext *context, Decl *label);
void context_change_scope_with_flags(SemaContext *context, ScopeFlags flags);

#define PUSH_X(ast, X) AstId _old_##X##_defer = context->X##_defer; AstId _old_##X = context->X##_target; context->X##_target = ast ? astid(ast) : 0; context->X##_defer = context->active_scope.defer_last
#define POP_X(X) context->X##_target = _old_##X; context->X##_defer = _old_##X##_defer
#define PUSH_CONTINUE(ast) PUSH_X(ast, continue)
#define POP_CONTINUE() POP_X(continue)
#define PUSH_BREAK(ast) PUSH_X(ast, break)
#define POP_BREAK() POP_X(break)
#define PUSH_NEXT(ast, sast) PUSH_X(ast, next); Ast *_old_next_switch = context->next_switch; context->next_switch = sast
#define POP_NEXT() POP_X(next); context->next_switch = _old_next_switch
#define PUSH_BREAKCONT(ast) PUSH_CONTINUE(ast); PUSH_BREAK(ast)
#define POP_BREAKCONT() POP_CONTINUE(); POP_BREAK()

AttributeType sema_analyse_attribute(SemaContext *context, Attr *attr, AttributeDomain domain);
bool expr_is_ltype(Expr *expr);

void sema_context_init(SemaContext *context, CompilationUnit *unit);
void sema_context_destroy(SemaContext *context);
Decl **global_context_acquire_locals_list(void);
void generic_context_release_locals_list(Decl **);
void sema_analysis_pass_process_imports(Module *module);
void sema_analysis_pass_register_globals(Module *module);
void sema_analysis_pass_conditional_compilation(Module *module);
void sema_analysis_pass_decls(Module *module);
void sema_analysis_pass_ct_assert(Module *module);
void sema_analysis_pass_functions(Module *module);
void sema_analyze_stage(Module *module, AnalysisStage stage);
Decl *sema_find_operator(SemaContext *context, Expr *expr, const char *kw);
bool sema_analyse_expr_lvalue(SemaContext *context, Expr *expr);
bool sema_expr_analyse_macro_call(SemaContext *context, Expr *call_expr, Expr *struct_var, Decl *decl, bool failable);
void expr_rewrite_to_int_const(Expr *expr_to_rewrite, Type *type, uint64_t value, bool narrowable);

