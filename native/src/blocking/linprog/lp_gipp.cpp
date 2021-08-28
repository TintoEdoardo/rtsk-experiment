#include "lp_common.h"
#include "blocking.h"
#include "nested_cs.h"

typedef std::set<LockSet> LockSets;

BlockingBounds* lp_gipp_bounds(const ResourceSharingInfo& info,
                               const ResourceLocality& locality, bool use_RTA = true)
{
    /*
     *  To implement
     */
}

// Maximum number of jobs of Tx overlapping with Ji
unsigned int max_overlapping_jobs(
        const TaskInfo& ti,
        const TaskInfo& tx)
{
    return ceil((ti.get_response() + tx.get_response()) / ti.get_period());
}

// Constraint 22 in [Brandenburg 2020]
// Prevent any blocking critical section from being counted twice
void add_cs_blocking_constraints(
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

// Denotes the number of times Ji issues an outermost request for a 􏰈􏰏􏰈 􏰌 􏰐􏰈
// resource in g
unsigned int issued_outermost_request(
        const CriticalSectionsOfTask& Ji,
        const LockSet& g)
{
    unsigned int outermost_request_number = 0;
    foreach(Ji.get_cs(), cs)
    {
        if(cs->is_outermost() && (g.find(cs->resource_id) != g.end()))
        {
            outermost_request_number++;
        }
    }
    return outermost_request_number;
}

// Denotes the number of tasks in Ck that request a resource in g
unsigned int lock_competing_tasks_in_cluster(
        const LockSet& g,
        const ResourceSharingInfo& info,
        const unsigned int k)
{
    unsigned int competing_tasks_number = 0;
    bool is_competing                   = false;
    foreach_task_in_cluster(info.get_tasks(), k, tx)
    {
        foreach(tx->get_requests(), req)
        {
            if(g.find(req->get_resource_id()) != g.end()) is_competing = true;
        }
        if(is_competing) competing_tasks_number++;
    }
    return competing_tasks_number;
}

// Compute the set of clusters
std::set<unsigned int> compute_clusters_set(
        const ResourceSharingInfo& info)
{
    std::set<unsigned int> clusters;
    foreach(info.get_tasks(), tx)
    {
        if(clusters.find(tx->get_cluster()) == clusters.end())
            clusters.insert(tx->get_cluster());
    }
    return clusters;
}

// Wi,g upper-bounds the number of times Ji must wait for
// a token of group g
unsigned int group_token_waiting_times_ub(
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
    // a token while Ji is pending
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

    // Compute Wi,g
    if(lock_competing_tasks_in_cluster(g, info, k) < clusters_number)
        w_i_g = 0;
    else
        w_i_g = std::min(issued_outer_reqs, token_required_while_pending);
    return w_i_g;
}

// Extract the corresponding critical section of task
const CriticalSectionsOfTask& get_critical_section_of_task(
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
void add_token_blocking_constraint(
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
            unsigned int tx_id         = tx->get_id();
            TaskInfo t                = *tx;
            const CriticalSectionsOfTask& cs = get_critical_section_of_task(info, cst, *tx);

            foreach_task_except(info.get_tasks(), t, ti)
            {
                unsigned int q;
                unsigned int gt_waiting = group_token_waiting_times_ub(*g, info, *ti, cs);

                LinearExpression *exp = new LinearExpression();
                unsigned int var_id;

                foreach(cs.get_cs(), c)
                {
                    q = c->resource_id;
                    if(c->is_outermost() && (g->find(q) != g->end()))
                    {
                        for(int v = 0; v < (int) max_overlapping_jobs(*ti, t); v++)
                        {
                            var_id = vars.lookup(tx_id, q, v, BLOCKING_TOKEN);
                            exp->add_var(var_id);
                        }
                    }
                }
                lp.add_inequality(exp, gt_waiting);
            }
        }
    }
}

// Constraints 26 in [Brandenburg 2020]
void add_aggregate_token_blocking_constraint(
        VarMapper& vars,
        unsigned int ps,
        const LockSets& ls,
        const ResourceSharingInfo& info,
        const TaskInfo& ti,
        const CriticalSectionsOfTaskset& cst,
        LinearProgram& lp)
{
    unsigned int cn = compute_clusters_set(info).size();
    foreach(ls, g)
    {
        const CriticalSectionsOfTask& ti_cs = get_critical_section_of_task(info, cst, ti);
        unsigned int gt_waiting             = group_token_waiting_times_ub(*g, info, ti, ti_cs);
        unsigned int c_size                 = compute_clusters_set(info).size();

        for(int k = 0; k < (int) c_size; k++)
        {
            unsigned int competing_tasks    = lock_competing_tasks_in_cluster(*g, info, ti.get_cluster());

            LinearExpression *exp = new LinearExpression();
            unsigned int var_id;

            foreach_task_in_cluster(info.get_tasks(), ti.get_cluster(), tx)
            {
                if(ti.get_id() != tx->get_id())
                {
                    unsigned int tx_id        = tx->get_id();
                    TaskInfo t                = *tx;
                    std::size_t tx_index      = std::distance(info.get_tasks().begin(), tx);
                    CriticalSectionsOfTask cs = cst.get_tasks()[tx_index];
                    unsigned int q;

                    foreach(cs.get_cs(), c)
                    {
                        if(c->is_outermost() && (g->find(q) != g->end()))
                        {
                            q = c->resource_id;
                            for(int v = 0; v < (int) max_overlapping_jobs(ti, t); v++)
                            {
                                var_id = vars.lookup(tx_id, q, v, BLOCKING_TOKEN);
                                exp->add_var(var_id);
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
void add_percluster_RSM_constraint(
        VarMapper& vars,
        unsigned int ps,
        const LockSets& ls,
        const ResourceSharingInfo& info,
        const TaskInfo& ti,
        const CriticalSectionsOfTaskset& cst,
        LinearProgram& lp)
{
    foreach(ls, g)
    {
        const CriticalSectionsOfTask& ti_cs = get_critical_section_of_task(info, cst, ti);
        unsigned int gt_waiting             = group_token_waiting_times_ub(*g, info, ti, ti_cs);
        unsigned int c_size                 = compute_clusters_set(info).size();

        for(int k = 0; k < (int) c_size; k++)
        {
            unsigned int competing_tasks    = lock_competing_tasks_in_cluster(*g, info, k);

            LinearExpression *exp = new LinearExpression();
            unsigned int var_id;

            foreach_task_in_cluster(info.get_tasks(), ti.get_cluster(), tx)
            {
                if(ti.get_id() != tx->get_id())
                {
                    unsigned int tx_id        = tx->get_id();
                    TaskInfo t                = *tx;
                    std::size_t tx_index      = std::distance(info.get_tasks().begin(), tx);
                    CriticalSectionsOfTask cs = cst.get_tasks()[tx_index];
                    unsigned int q;
                    foreach(cs.get_cs(), c)
                    {
                        if(c->is_outermost() && (g->find(q) != g->end()))
                        {
                            q = c->resource_id;
                            for(int v = 0; v < (int) max_overlapping_jobs(ti, t); v++)
                            {
                                var_id = vars.lookup(tx_id, q, v, BLOCKING_RSM);
                                exp->add_var(var_id);
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