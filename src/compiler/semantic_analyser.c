// Copyright (c) 2019 Christoffer Lerno. All rights reserved.
// Use of this source code is governed by the GNU LGPLv3.0 license
// a copy of which can be found in the LICENSE file.

#include "sema_internal.h"

void sema_shadow_error(Decl *decl, Decl *old)
{
	SEMA_ERROR(decl, "'%s' would shadow a previous declaration.", decl->name);
	SEMA_PREV(old, "The previous use of '%s' was here.", decl->name);
}

bool sema_resolve_type_info_maybe_inferred(SemaContext *context, TypeInfo *type_info, bool allow_inferred_type)
{
	if (!sema_resolve_type_shallow(context, type_info, allow_inferred_type, false)) return false;
	Type *type = type_no_fail(type_info->type);
	// usize and similar typedefs will not have a decl.
	if (type->type_kind == TYPE_TYPEDEF && type->decl == NULL) return true;
	if (!type_is_user_defined(type)) return true;
	return sema_analyse_decl(context, type->decl);
}

bool sema_resolve_type_info(SemaContext *context, TypeInfo *type_info)
{
	return sema_resolve_type_info_maybe_inferred(context, type_info, false);
}



void context_change_scope_with_flags(SemaContext *context, ScopeFlags flags)
{
	unsigned depth = context->active_scope.depth + 1;
	if (depth > MAX_SCOPE_DEPTH)
	{
		FATAL_ERROR("Too deeply nested scopes.");
	}

	Ast *previous_defer = context->active_scope.in_defer;
	AstId parent_defer = context->active_scope.defer_last;
	unsigned last_local = context->active_scope.current_local;
	assert(parent_defer < 1000000);
	// Defer and expression blocks introduce their own return/break/continue
	// otherwise just merge with the old flags.
	if (!(flags & (SCOPE_DEFER | SCOPE_EXPR_BLOCK)))
	{
		flags = context->active_scope.flags | flags;
	}
	context->active_scope = (DynamicScope) {
			.scope_id = ++context->scope_id,
			.allow_dead_code = false,
			.jump_end = false,
			.depth = depth,
			.current_local = last_local,
			.local_decl_start = last_local,
			.in_defer = previous_defer,
			.defer_last = parent_defer,
			.defer_start = parent_defer,
			.flags = flags,
	};
	if (context->scope_id == 0)
	{
		FATAL_ERROR("Too many scopes.");
	}
}

void context_change_scope_for_label(SemaContext *context, Decl *label)
{
	context_change_scope_with_flags(context, SCOPE_NONE);

	if (label)
	{
		label->label.defer = context->active_scope.defer_last;
		sema_add_local(context, label);
		label->label.scope_defer = astid(context->active_scope.in_defer);
	}
}



void context_pop_defers_to(SemaContext *context, DeferList *list)
{
	list->end = context->active_scope.defer_start;
	assert(context->active_scope.defer_last < 10000000);
	list->start = context->active_scope.defer_last;
	context->active_scope.defer_last = list->end;
}





Expr *context_pop_defers_and_wrap_expr(SemaContext *context, Expr *expr)
{
	DeferList defers = { 0, 0 };
	context_pop_defers_to(context, &defers);
	if (defers.end == defers.start) return expr;
	Expr *wrap = expr_new(EXPR_SCOPED_EXPR, expr->span);
	wrap->type = expr->type;
	wrap->resolve_status = RESOLVE_DONE;
	wrap->expr_scope.expr = expr;
	wrap->expr_scope.defers = defers;
	return expr;
}

void context_pop_defers_and_replace_expr(SemaContext *context, Expr *expr)
{
	DeferList defers = { 0, 0 };
	context_pop_defers_to(context, &defers);
	if (defers.end == defers.start) return;
	Expr *inner = expr_copy(expr);
	expr->expr_kind = EXPR_SCOPED_EXPR;
	expr->expr_scope.expr = inner;
	expr->expr_scope.defers = defers;
}

void context_pop_defers_and_replace_ast(SemaContext *context, Ast *ast)
{
	DeferList defers = { 0, 0 };
	context_pop_defers_to(context, &defers);
	if (defers.end == defers.start) return;
	if (ast->ast_kind == AST_DEFER_STMT)
	{
		assert(defers.start == astid(ast));
		*ast = *ast->defer_stmt.body;
		return;
	}
	assert(ast->ast_kind != AST_COMPOUND_STMT);
	Ast *replacement = ast_copy(ast);
	ast->ast_kind = AST_SCOPED_STMT;
	ast->scoped_stmt.stmt = replacement;
	ast->scoped_stmt.defers = defers;
}

static inline void halt_on_error(void)
{
	if (global_context.errors_found > 0) exit_compiler(EXIT_FAILURE);
}



void sema_analyze_stage(Module *module, AnalysisStage stage)
{
	while (module->stage < stage)
	{
		module->stage++;
		switch (module->stage)
		{
			case ANALYSIS_NOT_BEGUN:
				UNREACHABLE
			case ANALYSIS_IMPORTS:
				sema_analysis_pass_process_imports(module);
				break;
			case ANALYSIS_REGISTER_GLOBALS:
				sema_analysis_pass_register_globals(module);
				break;
			case ANALYSIS_CONDITIONAL_COMPILATION:
				sema_analysis_pass_conditional_compilation(module);
				break;
			case ANALYSIS_DECLS:
				sema_analysis_pass_decls(module);
				break;
			case ANALYSIS_CT_ASSERT:
				sema_analysis_pass_ct_assert(module);
				break;
			case ANALYSIS_FUNCTIONS:
				sema_analysis_pass_functions(module);
				break;
		}
		if (global_context.errors_found) return;
	}
}

static void register_generic_decls(Module *module, Decl **decls)
{
	VECEACH(decls, i)
	{
		Decl *decl = decls[i];
		decl->module = module;
		switch (decl->decl_kind)
		{
			case DECL_POISONED:
			case DECL_ENUM_CONSTANT:
			case DECL_ERRVALUE:
			case DECL_IMPORT:
			case DECL_LABEL:
			case DECL_CT_ASSERT:
				continue;
			case DECL_ATTRIBUTE:
				break;
			case DECL_CT_CASE:
				register_generic_decls(module, decl->ct_case_decl.body);
				continue;
			case DECL_CT_ELIF:
				register_generic_decls(module, decl->ct_elif_decl.then);
				continue;
			case DECL_CT_ELSE:
				register_generic_decls(module, decl->ct_else_decl);
				continue;
			case DECL_CT_IF:
				register_generic_decls(module, decl->ct_if_decl.then);
				continue;
			case DECL_CT_SWITCH:
				register_generic_decls(module, decl->ct_switch_decl.cases);
				continue;
			case DECL_MACRO:
			case DECL_DEFINE:
			case DECL_DISTINCT:
			case DECL_ENUM:
			case DECL_GENERIC:
			case DECL_ERRTYPE:
			case DECL_FUNC:
			case DECL_STRUCT:
			case DECL_TYPEDEF:
			case DECL_UNION:
			case DECL_VAR:
			case DECL_BITSTRUCT:
				break;
		}
		stable_set(&module->symbols, decl->name, decl);
	}

}


static void analyze_generic_module(Module *module)
{
	assert(module->parameters && module->is_generic);
	// TODO maybe do this analysis: sema_analysis_pass_process_imports(module);
	VECEACH(module->units, index)
	{
		register_generic_decls(module, module->units[index]->global_decls);
	}
}

static void analyze_to_stage(AnalysisStage stage)
{
	VECEACH(global_context.module_list, i)
	{
		sema_analyze_stage(global_context.module_list[i], stage);
	}
	halt_on_error();
}

void sema_analysis_run(void)
{

	global_context_clear_errors();

	if (global_context.lib_dir && !active_target.no_stdlib)
	{
		file_add_wildcard_files(&global_context.sources, global_context.lib_dir, true, ".c3", ".c3i");
	}
	bool has_error = false;
	VECEACH(global_context.sources, i)
	{
		bool loaded = false;
		File *file = source_file_load(global_context.sources[i], &loaded);
		if (loaded) continue;
		if (!parse_file(file)) has_error = true;
	}

	if (has_error) exit_compiler(EXIT_FAILURE);

	global_context.std_module_path = (Path) { .module = kw_std, .span = INVALID_RANGE, .len = (uint32_t) strlen(kw_std) };
	global_context.std_module = (Module){ .name = &global_context.std_module_path };
	global_context.std_module.stage = ANALYSIS_LAST;
	global_context.locals_list = NULL;

	stable_init(&global_context.std_module.symbols, 0x10000);
	type_func_prototype_init(0x10000);

	if (!global_context.module_list)
	{
		if (global_context.errors_found) exit_compiler(EXIT_FAILURE);
		error_exit("No modules to compile.");
	}
	VECEACH(global_context.generic_module_list, i)
	{
		analyze_generic_module(global_context.generic_module_list[i]);
	}
	for (AnalysisStage stage = ANALYSIS_NOT_BEGUN + 1; stage <= ANALYSIS_LAST; stage++)
	{
		analyze_to_stage(stage);
	}

}

void sema_context_init(SemaContext *context, CompilationUnit *unit)
{
	*context = (SemaContext) { .unit = unit, .compilation_unit = unit, .locals = global_context_acquire_locals_list() };
}

void sema_context_destroy(SemaContext *context)
{
	generic_context_release_locals_list(context->locals);
}

Decl **global_context_acquire_locals_list(void)
{
	if (!vec_size(global_context.locals_list))
	{
		return VECNEW(Decl*, 64);
	}
	Decl **result = VECLAST(global_context.locals_list);
	vec_pop(global_context.locals_list);
	return result;
}

void generic_context_release_locals_list(Decl **list)
{
	vec_add(global_context.locals_list, list);
}
