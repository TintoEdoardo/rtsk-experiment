#include "lp_common.h"
#include "blocking.h"
#include "nested_cs.h"

typedef std::set<LockSet> LockSets;

// Maximum number of jobs of Tx overlapping with Ji.
static unsigned int max_overlapping_jobs(
        const TaskInfo& ti,
        const TaskInfo& tx)
{
    return ceil((ti.get_response() + tx.get_response()) / ti.get_period());
}

// Constraint 22 in [Brandenburg 2020]
// Prevent any blocking critical section from being counted twice.
static void add_cs_blocking_constraints(
        VarMapper& vars,
        const ResourceSharingInfo& info,
        const TaskInfo& ti,
        const CriticalSectionsOfTask& cst,
        LinearProgram& lp)
{

    foreach_task_except(info.get_tasks(), ti, tx)
    {
        unsigned int overlapping_jobs = max_overlapping_jobs(ti, *tx);
        unsigned int t = tx->get_id();

        foreach(cst.get_cs(), cs)
        {
            if(cs->is_outermost())
            {
                unsigned int q = cs->resource_id;

                for(int v = 0; v < (int) overlapping_jobs; v++)
                {
                    LinearExpression *exp = new LinearExpression();
                    unsigned int var_id;

                    var_id = vars.lookup(t, q, v, BLOCKING_TOKEN);
                    exp->add_var(var_id);

                    var_id = vars.lookup(t, q, v, BLOCKING_RSM);
                    exp->add_var(var_id);

                    lp.add_inequality(exp, 1);
                }
            }
        }
    }
}

// Number of times Ji issues an outermost request for a 􏰈􏰏resource in g.
static unsigned int issued_outermost_request(
        const CriticalSectionsOfTask& cst_i,
        const LockSet& g)
{
    unsigned int outermost_request_count = 0;

    foreach(cst_i.get_cs(), cs)
    {
        // The resource is in g?
        if(g.find(cs->resource_id) != g.end())
        {
            // The request is outermost?
            if(cs->is_outermost())
            {
                outermost_request_count++;
            }
        }
    }
    return outermost_request_count;
}

// Number of tasks in cluster k that request a resource in g.
static unsigned int count_competing_tasks_in_cluster(
        const LockSet& g,
        const ResourceSharingInfo& info,
        const unsigned int k)
{
    unsigned int competing_tasks_count = 0;
    bool is_competing                   = false;

    foreach_task_in_cluster(info.get_tasks(), k, tx)
    {
        foreach(tx->get_requests(), req)
        {
            unsigned int res_id = req->get_resource_id();
            if(g.find(res_id) != g.end())
                is_competing = true;
        }
        if(is_competing)
            competing_tasks_count++;
    }

    return competing_tasks_count;
}

// Compute the set of clusters.
static std::set<unsigned int> compute_clusters_set(
        const ResourceSharingInfo& info)
{
    std::set<unsigned int> clusters;

    foreach(info.get_tasks(), tx)
    {
        unsigned int cluster_id = tx->get_cluster();
        if(clusters.find(cluster_id) == clusters.end())
            clusters.insert(cluster_id);
    }

    return clusters;
}

// Wi,g upper-bounds the number of times Ji must wait for
// a token of group g.
static unsigned int compute_token_waiting_times(
        const LockSet& g,
        const ResourceSharingInfo& info,
        const TaskInfo& ti,
        const CriticalSectionsOfTask& cst)
{
    unsigned int k                 = ti.get_cluster();
    unsigned int clusters_number   = compute_clusters_set(info).size();
    unsigned int issued_outer_reqs = issued_outermost_request(cst, g);
    unsigned int w_i_g;

    // Compute the number of times that other tasks require
    // a token while Ji is pending.
    unsigned int token_required_while_pending = 0;

    foreach_task_in_cluster(info.get_tasks(), k, tx)
    {
        if(tx->get_id() != ti.get_id())
        {
            token_required_while_pending +=
                    issued_outermost_request(cst, g) * max_overlapping_jobs(ti, *tx);
        }
    }
    token_required_while_pending =
            token_required_while_pending - clusters_number + 1;

    // Compute Wi,g.
    if(count_competing_tasks_in_cluster(g, info, k) < clusters_number)
        w_i_g = 0;
    else
        w_i_g = std::min(issued_outer_reqs, token_required_while_pending);

    return w_i_g;
}

// Extract the corresponding critical sections of task.
static const CriticalSectionsOfTask& get_critical_section_of_task(
        const ResourceSharingInfo& info,
        const CriticalSectionsOfTaskset& cst,
        const TaskInfo& ti)
{
    std::size_t ti_index;

    foreach(info.get_tasks(), t)
    {
        if(t->get_id() == ti.get_id())
            ti_index = std::distance(info.get_tasks().begin(), t);
    }

    return cst.get_tasks()[ti_index];
}

// Constraints 25 in [Brandenburg 2020]
static void add_token_blocking_constraint(
        VarMapper& vars,
        const LockSets& ls,
        const ResourceSharingInfo& info,
        const CriticalSectionsOfTaskset& cst,
        LinearProgram& lp)
{
    foreach(ls, g)
    {
        foreach(info.get_tasks(), tx)
        {
            unsigned int tx_id               = tx->get_id();
            TaskInfo t                       = *tx;
            const CriticalSectionsOfTask& cs = get_critical_section_of_task(info, cst, *tx);

            foreach_task_except(info.get_tasks(), t, ti)
            {
                unsigned int gt_waiting = compute_token_waiting_times(*g, info, *ti, cs);

                LinearExpression *exp = new LinearExpression();
                unsigned int var_id;

                foreach(cs.get_cs(), c)
                {
                    unsigned int q = c->resource_id;

                    if(c->is_outermost())
                    {
                        if((g->find(q) != g->end()))
                        {
                            for(int v = 0; v < (int) max_overlapping_jobs(t, *ti); v++)
                            {
                                var_id = vars.lookup(tx_id, q, v, BLOCKING_TOKEN);
                                exp->add_var(var_id);
                            }
                        }
                    }
                }
                lp.add_inequality(exp, gt_waiting);
            }
        }
    }
}

// Constraints 26 in [Brandenburg 2020]
static void add_aggregate_token_blocking_constraint(
        VarMapper& vars,
        const LockSets& ls,
        const ResourceSharingInfo& info,
        const TaskInfo& ti,
        const CriticalSectionsOfTaskset& cst,
        LinearProgram& lp)
{
    foreach(ls, g)
    {
        const CriticalSectionsOfTask& ti_cs = get_critical_section_of_task(info, cst, ti);
        unsigned int gt_waiting             = compute_token_waiting_times(*g, info, ti, ti_cs);
        unsigned int c_size                 = compute_clusters_set(info).size();

        for(int k = 0; k < (int) c_size; k++)
        {
            unsigned int competing_tasks    = count_competing_tasks_in_cluster(*g, info, ti.get_cluster());

            LinearExpression *exp = new LinearExpression();
            unsigned int var_id;

            foreach_task_in_cluster(info.get_tasks(), ti.get_cluster(), tx)
            {
                if(ti.get_id() != tx->get_id())
                {
                    unsigned int tx_id        = tx->get_id();
                    TaskInfo t                = *tx;
                    CriticalSectionsOfTask cs = get_critical_section_of_task(info, cst, t);

                    foreach(cs.get_cs(), c)
                    {
                        if(c->is_outermost())
                        {
                            unsigned int q = c->resource_id;
                            if(g->find(q) != g->end())
                            {
                                for(int v = 0; v < (int) max_overlapping_jobs(ti, t); v++)
                                {
                                    var_id = vars.lookup(tx_id, q, v, BLOCKING_TOKEN);
                                    exp->add_var(var_id);
                                }
                            }
                        }
                    }
                }
            }
            lp.add_inequality(exp, gt_waiting * std::min(c_size, competing_tasks));
        }
    }
}

// Constraints 27 in [Brandenburg 2020]
static void add_percluster_RSM_constraint(
        VarMapper& vars,
        const LockSets& ls,
        const ResourceSharingInfo& info,
        const TaskInfo& ti,
        const CriticalSectionsOfTaskset& cst,
        LinearProgram& lp)
{
    foreach(ls, g)
    {
        const CriticalSectionsOfTask& ti_cs = get_critical_section_of_task(info, cst, ti);
        unsigned int gt_waiting             = compute_token_waiting_times(*g, info, ti, ti_cs);
        unsigned int c_size                 = compute_clusters_set(info).size();

        for(int k = 0; k < (int) c_size; k++)
        {
            unsigned int competing_tasks    = count_competing_tasks_in_cluster(*g, info, k);

            LinearExpression *exp = new LinearExpression();
            unsigned int var_id;

            foreach_task_in_cluster(info.get_tasks(), ti.get_cluster(), tx)
            {
                if(ti.get_id() != tx->get_id())
                {
                    unsigned int tx_id        = tx->get_id();
                    TaskInfo t                = *tx;
                    CriticalSectionsOfTask cs = get_critical_section_of_task(info, cst, t);

                    foreach(cs.get_cs(), c)
                    {
                        if(c->is_outermost())
                        {
                            unsigned int q = c->resource_id;

                            if(g->find(q) != g->end())
                            {
                                for(int v = 0; v < (int) max_overlapping_jobs(ti, t); v++)
                                {
                                    var_id = vars.lookup(tx_id, q, v, BLOCKING_RSM);
                                    exp->add_var(var_id);
                                }
                            }
                        }
                    }
                }
            }

            unsigned int exp_value;
            if((int) ti.get_cluster() == k)
                exp_value = gt_waiting * std::min(c_size, competing_tasks);
            else
                exp_value = gt_waiting * std::min(c_size - 1, competing_tasks -1);
            lp.add_inequality(exp, exp_value);
        }
    }
}

// Compute the set of all combinations of resources in group g
// acquired by other tasks.
static const LockSets subset_acquired_by_other(
        const LockSet& g,
        const ResourceSharingInfo& info,
        const TaskInfo& ti)
{
    LockSets result;

    foreach_task_except(info.get_tasks(), ti, tx)
    {
        const Requests reqs = tx->get_requests();
        LockSet ls;
        foreach(reqs, req)
        {
            unsigned int res_id = req->get_resource_id();
            if(g.find(res_id) != g.end())
            {
                ls.insert(res_id);
            }
        }
        result.insert(ls);
    }
    return result;
}

// Check if resources accessed in cs are a subset of s.
static bool cs_is_subset_of_s(
        const CriticalSection& cs,
        const CriticalSectionsOfTask& cst,
        const LockSet& s)
{
    // LockSet for nested resources in cs.
    LockSet cs_ls;

    // Insert cs direct resource.
    cs_ls.insert(cs.resource_id);

    // Insert nested resources.
    foreach(cst.get_cs(), c)
    {
        if(cst.get_outermost(c->resource_id) == cs.resource_id)
        {
            cs_ls.insert(c->resource_id);
        }
    }

    return is_subset_of(cs_ls, s);
}

// Check if ls an ls1 are possibly conflicting.
static bool are_cs_conflicting(
        const LockSet& ls,
        const LockSet& ls1,
        const CriticalSectionsOfTaskset& csts)
{
    // Check if the intersection between ls and ls1 is empty
    bool empty_intersection = true;
    foreach(ls, s)
    {
        if(ls1.find(*s) != ls1.end())
            empty_intersection = false;
    }

    // Check if lb > la with lb in ls1 and la in ls.
    bool no_relation = true;
    foreach(ls1, lb)
    {
        foreach(csts.get_tasks(), cst)
        {
            foreach(cst->get_cs(), la)
            {
                if(ls.find(la->resource_id) != ls.end()
                    && la->outer == (int) *lb)
                    no_relation = false;
            }
        }
    }
    return (!empty_intersection) || (!no_relation);
}

// Compute the number of possibly conflicting outermost cs
static unsigned int count_conflicting_outermost_cs(
        const ResourceSharingInfo& info,
        const TaskInfo& ti,
        const LockSet& s,
        const CriticalSectionsOfTaskset& csts)
{
    unsigned int result        = 0;
    CriticalSectionsOfTask cst = get_critical_section_of_task(info, csts, ti);

    foreach(cst.get_cs(), cs)
    {
        if(cs->is_outermost())
        {
            if(are_cs_conflicting(s, cs->get_outer_locks(cst), csts))
                result++;
        }
    }

    return result;
}

// Constraints 28 in [Brandenburg 2020]
static void add_detailed_RSM_constraint(
        VarMapper& vars,
        const LockSets& ls,
        const ResourceSharingInfo& info,
        const TaskInfo& ti,
        const CriticalSectionsOfTaskset& cst,
        LinearProgram& lp)
{
    unsigned int c_size = compute_clusters_set(info).size();

    foreach(ls, g)
    {
        const CriticalSectionsOfTask& ti_cs = get_critical_section_of_task(info, cst, ti);

        foreach(subset_acquired_by_other(*g, info, ti), s)
        {
            for(int k = 0; k < (int) c_size; k++)
            {
                unsigned int possibly_conclicting_cs = count_conflicting_outermost_cs(info, ti, *s, cst);
                unsigned int competing_tasks         = count_competing_tasks_in_cluster(*g, info, k);

                LinearExpression *exp = new LinearExpression();
                unsigned int var_id;

                foreach_task_in_cluster(info.get_tasks(), ti.get_cluster(), tx)
                {
                    unsigned int tx_id               = tx->get_id();
                    TaskInfo t                       = *tx;
                    const CriticalSectionsOfTask& cs = get_critical_section_of_task(info, cst, *tx);
                    foreach(cs.get_cs(), c)
                    {
                        if(cs_is_subset_of_s(*c, cs, *s))
                        {
                            unsigned int q = c->resource_id;
                            for(int v = 0; v < (int) max_overlapping_jobs(ti, *tx); v++)
                            {
                                var_id = vars.lookup(tx_id, q, v, BLOCKING_RSM);
                                exp->add_var(var_id);
                            }
                        }
                    }
                }

                unsigned int exp_value;
                if((int) ti.get_cluster() == k)
                    exp_value = possibly_conclicting_cs * std::min(c_size, competing_tasks);
                else
                    exp_value = possibly_conclicting_cs * std::min(c_size - 1, competing_tasks -1);
                lp.add_inequality(exp, exp_value);
            }
        }
    }
}

static void add_gipp_constraints(
        VarMapper& vars,
        const LockSets& ls,
        const ResourceSharingInfo& info,
        const TaskInfo& ti,
        const CriticalSectionsOfTaskset& cst,
        LinearProgram& lp)
{
    CriticalSectionsOfTask cs_i = get_critical_section_of_task(info, cst, ti);

    add_cs_blocking_constraints(vars, info, ti, cs_i,lp);
    add_token_blocking_constraint(vars, ls, info, cst, lp);
    add_aggregate_token_blocking_constraint(vars, ls, info, ti, cst, lp);
    add_percluster_RSM_constraint(vars, ls, info, ti, cst, lp);
    add_detailed_RSM_constraint(vars, ls, info, ti, cst, lp);
}

static void apply_gipp_bounds_for_task(
        unsigned int i,
        BlockingBounds& bounds,
        const LockSets& ls,
        const ResourceSharingInfo& info,
        const CriticalSectionsOfTaskset& cst)
{
    LinearProgram lp;
    VarMapper vars;
    const TaskInfo& ti = info.get_tasks()[i];
    unsigned int cluster = ti.get_cluster();

    set_blocking_objective_sob(vars, info, ti, lp);
    vars.seal();

    add_gipp_constraints(vars, ls, info, ti, cst, lp);

    Solution *sol = linprog_solve(lp, vars.get_num_vars());
    assert(sol != NULL);

    Interference total;

    total.total_length = lrint(sol->evaluate(*lp.get_objective()));
    bounds[i] = total;

    delete sol;
}

static BlockingBounds* _lp_gipp_bounds(
        const ResourceSharingInfo& info,
        const LockSets& ls,
        const CriticalSectionsOfTaskset& cst)
{
    BlockingBounds* results = new BlockingBounds(info);

    for (unsigned int i = 0; i < info.get_tasks().size(); i++)
        apply_gipp_bounds_for_task(i, *results, ls, info, cst);

    return results;
}

BlockingBounds* lp_gipp_bounds(
        const ResourceSharingInfo& info,
        const LockSets& ls,
        const CriticalSectionsOfTaskset& cst)
{

    BlockingBounds *results = _lp_gipp_bounds(info, ls, cst);

    return results;
}
