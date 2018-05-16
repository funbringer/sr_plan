#include "postgres.h"

#include "access/heapam.h"
#include "access/htup_details.h"
#include "access/xact.h"
#include "catalog/indexing.h"
#include "catalog/pg_extension.h"
#include "catalog/pg_type.h"
#include "commands/explain.h"
#include "commands/extension.h"
#include "nodes/nodeFuncs.h"
#include "optimizer/planner.h"
#include "parser/analyze.h"
#include "tcop/utility.h"
#include "utils/builtins.h"
#include "utils/fmgroids.h"
#include "utils/inval.h"
#include "utils/lsyscache.h"
#include "utils/syscache.h"
#include "fmgr.h"

#include "sr_relations.h"

#if PG_VERSION_NUM >= 100000
#include "utils/queryenvironment.h"
#include "catalog/index.h"
#endif


#define IsFakeProcExpr(expr) \
	( \
		IsA((expr), FuncExpr) && \
		((FuncExpr *) (expr))->funcid == sr_get_fake_proc_oid() \
	)


PG_MODULE_MAGIC;


PG_FUNCTION_INFO_V1(sr_plan_invalid_table);
PG_FUNCTION_INFO_V1(explain_jsonb_plan);
PG_FUNCTION_INFO_V1(_p);


typedef struct
{
	List *consts;
	List *params;
} QueryFeatures;


void _PG_init(void);

static void sr_analyze(ParseState *pstate, Query *query);

static PlannedStmt *sr_planner(Query *parse,
							   int cursorOptions,
							   ParamListInfo boundParams);
static PlannedStmt *sr_get_plan(Query *parse,
								int cursorOptions,
								ParamListInfo boundParams);

static void sr_store_plan(PlannedStmt *stmt);
static PlannedStmt *sr_load_plan(uint32 query_hash, uint32 plan_hash);

static QueryFeatures sr_extract_features(Query *query);
static bool sr_extract_features_walker(Node *node, void *context);

static Oid sr_get_relation_oid(const char *relname);
static Oid sr_get_fake_proc_oid(void);
static Oid sr_get_namespace_oid(void);


const char	   *query_text;
static bool 	sr_plan_write_mode = false;

static post_parse_analyze_hook_type	post_parse_analyze_hook_next = NULL;
static planner_hook_type			planner_hook_next = NULL;


/* Main parse & analyze hook */
static void
sr_analyze(ParseState *pstate, Query *query)
{
	query_text = pstate->p_sourcetext;

	if (post_parse_analyze_hook_next)
		post_parse_analyze_hook_next(pstate, query);
}

/* Main planner hook */
static PlannedStmt *
sr_planner(Query *parse,
		   int cursorOptions,
		   ParamListInfo boundParams)
{
	elog(INFO, "nsp  = %u", sr_get_namespace_oid());
	elog(INFO, "proc = %u", sr_get_fake_proc_oid());
	elog(INFO, "rel  = %u", sr_get_relation_oid(SR_PLANS_TABLE_NAME));

	return sr_get_plan(parse, cursorOptions, boundParams);
}

static PlannedStmt *
sr_get_plan(Query *parse,
			int cursorOptions,
			ParamListInfo boundParams)
{
	QueryFeatures features;
	PlannedStmt *stmt;

	/* Nothing to do here if sr_plan isn't installed */
	if (!OidIsValid(sr_get_namespace_oid()))
		return standard_planner(parse, cursorOptions, boundParams);

	features = sr_extract_features(parse);
	elog(INFO, "found %d param(s)", list_length(features.params));
	elog(INFO, "found %d const(s)", list_length(features.consts));

	stmt = standard_planner(parse, cursorOptions, boundParams);
	elog(INFO, "plan length: %zu bytes", strlen(nodeToString(stmt->planTree)));

	return stmt;
}

static void
sr_store_plan(PlannedStmt *stmt)
{
	Relation	rel;
	HeapTuple	htup;
	Datum		values[Natts_sr_plans];
	bool		isnull[Natts_sr_plans] = { false };
	Oid			relid = sr_get_relation_oid(SR_PLANS_TABLE_NAME);
	LOCKMODE	lockmode = RowExclusiveLock;

	values[Anum_sr_plans_query_hash]	= Int32GetDatum(0);
	values[Anum_sr_plans_plan_hash]		= Int32GetDatum(0);
	values[Anum_sr_plans_enable]		= BoolGetDatum(false);
	values[Anum_sr_plans_valid]			= BoolGetDatum(false);
	values[Anum_sr_plans_plan]			= CStringGetDatum(nodeToString(stmt));
	values[Anum_sr_plans_query]			= CStringGetDatum("[ERROR]");

	rel = heap_open(relid, lockmode);

	/* Save tuple to the dedicated table */
	htup = heap_form_tuple(RelationGetDescr(rel), values, isnull);
	simple_heap_insert(rel, htup);
	heap_freetuple(htup);

	heap_close(rel, lockmode);
}

static PlannedStmt *
sr_load_plan(uint32 query_hash, uint32 plan_hash)
{
	return NULL;
}

static QueryFeatures
sr_extract_features(Query *query)
{
	QueryFeatures features = { NIL };
	query_tree_walker(query, sr_extract_features_walker, &features, 0);
	return features;
}

static bool
sr_extract_features_walker(Node *node, void *context)
{
	QueryFeatures *features = (QueryFeatures *) context;

	if (node == NULL)
		return false;

	if (IsA(node, Const))
	{
		/* Remember this const */
		features->consts = lappend(features->consts, node);

		return false;
	}

	if (IsFakeProcExpr(node))
	{
		FuncExpr *expr = (FuncExpr *) node;

		/* There should be exactly 1 argument */
		Assert(list_length(expr->args) == 1);

		/* Remember this parameter */
		features->params = lappend(features->params, linitial(expr->args));

		/* We probably shouldn't go deeper */
		return false;
	}

	return expression_tree_walker(node, sr_extract_features_walker, context);
}


void
_PG_init(void)
{
	DefineCustomBoolVariable("sr_plan.write_mode",
							 "Save plans for all queries",
							 NULL,
							 &sr_plan_write_mode,
							 false,
							 PGC_SUSET,
							 0,
							 NULL,
							 NULL,
							 NULL);

	/* Save existing planner hook */
	if (planner_hook)
		planner_hook_next = planner_hook;

	/* Save existing query analyzer hook */
	if (post_parse_analyze_hook)
		post_parse_analyze_hook_next = post_parse_analyze_hook;

	/* Setup new hooks */
	planner_hook = &sr_planner;
	post_parse_analyze_hook = &sr_analyze;
}


Datum
_p(PG_FUNCTION_ARGS)
{
	PG_RETURN_DATUM(PG_GETARG_DATUM(0));
}

Datum
explain_jsonb_plan(PG_FUNCTION_ARGS)
{
	/* TODO: implement explain for serialized plans */
	PG_RETURN_NULL();
}


/* NOTE: do not use these OIDs directly! */
static Oid	sr_fake_proc = InvalidOid;
static Oid	sr_namespace = InvalidOid;


static void
sr_invalidate_syscache_cb(Datum arg, int cacheid, uint32 hash)
{
	uint32 sr_obj_hash = *(uint32 *) DatumGetPointer(arg);

	/* Zero hash means whole cache has been dropped */
	if (sr_obj_hash == hash || hash == 0)
	{
		switch (cacheid)
		{
			/* Either change implies full reset */
			case PROCNAMEARGSNSP:
			case NAMESPACEOID:
				sr_fake_proc = InvalidOid;
				sr_namespace = InvalidOid;
				break;

			default:
				break;
		}
	}
}


/* Return oid of table containing plans if possible */
static Oid
sr_get_relation_oid(const char *relname)
{
	return get_relname_relid(relname, sr_get_namespace_oid());
}

/* Return oid of fake procedure _p() if possible */
static Oid
sr_get_fake_proc_oid(void)
{
	Oid				arg_types[] = { ANYELEMENTOID };
	Datum			key1, key2, key3;

	static uint32	sr_fake_proc_hash = 0;
	static bool		callback_ready = false;

	Assert(IsTransactionState());

	/* Install callback */
	if (!callback_ready)
	{
		CacheRegisterSyscacheCallback(PROCNAMEARGSNSP,
									  sr_invalidate_syscache_cb,
									  PointerGetDatum(&sr_fake_proc_hash));
		callback_ready = true;
	}

	if (OidIsValid(sr_fake_proc))
		return sr_fake_proc;

	key1 = CStringGetDatum("_p");
	key2 = PointerGetDatum(buildoidvector(arg_types, lengthof(arg_types)));
	key3 = PointerGetDatum(sr_get_namespace_oid());

	sr_fake_proc_hash = GetSysCacheHashValue3(PROCNAMEARGSNSP, key1, key2, key3);
	sr_fake_proc = GetSysCacheOid3(PROCNAMEARGSNSP, key1, key2, key3);

	return sr_fake_proc;
}

/* Return sr_plan's namespace oid if possible */
static Oid
sr_get_namespace_oid(void)
{
	Relation		rel;
	HeapTuple		htup;
	NameData		extname;
	ScanKeyData		skey[1];
	SysScanDesc		scandesc;
	LOCKMODE		lockmode = AccessShareLock;
	Oid				result = InvalidOid;
	Datum			key1;

	static uint32	sr_namespace_hash = 0;
	static bool		callback_ready = false;

	Assert(IsTransactionState());

	/* Install callback */
	if (!callback_ready)
	{
		CacheRegisterSyscacheCallback(NAMESPACEOID,
									  sr_invalidate_syscache_cb,
									  PointerGetDatum(&sr_namespace_hash));
		callback_ready = true;
	}

	if (OidIsValid(sr_namespace))
		return sr_namespace;

	/* Search by extension's name */
	namestrcpy(&extname, "sr_plan");
	ScanKeyInit(&skey[0],
				Anum_pg_extension_extname,
				BTEqualStrategyNumber, F_NAMEEQ,
				NameGetDatum(&extname));

	rel = heap_open(ExtensionRelationId, lockmode);
	scandesc = systable_beginscan(rel, ExtensionNameIndexId, true,
								  NULL, lengthof(skey), skey);

	htup = systable_getnext(scandesc);

	/* We assume that there can be at most one matching tuple */
	if (HeapTupleIsValid(htup))
		result = ((Form_pg_extension) GETSTRUCT(htup))->extnamespace;

	systable_endscan(scandesc);
	heap_close(rel, lockmode);

	key1 = ObjectIdGetDatum(result);

	sr_namespace_hash = GetSysCacheHashValue1(NAMESPACEOID, key1);
	sr_namespace = result;

	return sr_namespace;
}
