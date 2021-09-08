#include "lp_common.h"
#include "blocking.h"
#include "nested_cs.h"

typedef std::vector<LockSet> ResourceGroup;
typedef std::vector<CriticalSections > OutermostCS;

class PartitionedGIPPLP : protected LinearProgram
{

private:

    VarMapper vars;

    const int i;
    const TaskInfo& ti;
    const CriticalSectionsOfTask& csi;
    const TaskInfos& taskset;
    const CriticalSectionsOfTasks& taskset_cs;

    const unsigned int cpu_number; // number of CPUs, discovered from taskset
    const unsigned int cluster_size;

    ResourceGroup res_groups; // set of resource groups
    OutermostCS outermost_cs; // vector of set containing outermost cs per task

    // Utility methods
    unsigned int max_overlapping_jobs(const TaskInfo& tx) const;
    unsigned int compute_token_waiting_times(const LockSet& g) const;
    unsigned int count_conflicting_outermost_cs(unsigned int x,
                                                const LockSet& s) const;
    unsigned int total_length(unsigned int x,
                              unsigned int y) const;
    unsigned int count_competing_tasks_in_cluster(const LockSet& g,
                                                  unsigned int k) const;
    unsigned int issued_outermost_request(const LockSet& g) const;

    void configure_outermost_cs();
    LockSets subset_acquired_by_other(const LockSet& g) const;
    bool cs_is_subset_of_s(unsigned int x,
                           unsigned int y,
                           const LockSet& s) const;
    bool are_cs_conflicting(const LockSet& ls,
                            const LockSet& ls1) const;

    // Constraints according to [Brandenburg 2020]
    void add_cs_blocking_constraints();
    void add_token_blocking_constraint();
    void add_aggregate_token_blocking_constraint();
    void add_percluster_RSM_constraint();
    void add_detailed_RSM_constraint();

    // Composable methods
    void add_gipp_constraints();
    void set_blocking_objective_gipp();


public:

    PartitionedGIPPLP(
            const ResourceSharingInfo& tsk,
            const CriticalSectionsOfTaskset& tsk_cs,
            int task_under_analysis,
            unsigned int cpu_num,
            unsigned int c_size);

    // LP solver invocation here
    unsigned long solve();

};

PartitionedGIPPLP::PartitionedGIPPLP(
        const ResourceSharingInfo& tsk,
        const CriticalSectionsOfTaskset& tsk_cs,
        int task_under_analysis,
        unsigned int cpu_num,
        unsigned int c_size)
      : i(task_under_analysis),
        ti(tsk.get_tasks()[i]),
        csi(tsk_cs.get_tasks()[i]),
        taskset(tsk.get_tasks()),
        taskset_cs(tsk_cs.get_tasks()),
        cpu_number(cpu_num),
        cluster_size(c_size),
        res_groups(tsk_cs.get_resource_groups()),
        outermost_cs()
{
    std::cout << "Enter constructor" << std::endl;
    configure_outermost_cs();
    std::cout << "End outer_cs creation" << std::endl;
    set_blocking_objective_gipp();
    std::cout << "End obj creation" << std::endl;
    vars.seal();
    add_gipp_constraints();
    std::cout << "End constraints" << std::endl;
}

/* Compute the number of jobs of task T_x
 * overlapping with the job J_i of T_i. */
unsigned int PartitionedGIPPLP::max_overlapping_jobs(const TaskInfo& tx) const
{
    return tx.get_max_num_jobs(ti.get_response());
}

/* Compute the sets of outermost critical
 * sections, grouped by task requesting them. */
void PartitionedGIPPLP::configure_outermost_cs()
{
    unsigned int cst_index = 0;
    enumerate(taskset_cs, cst, cst_index)
    {
        foreach(cst->get_cs(), cs)
        {
            CriticalSections ocs;
            if(cs->is_outermost())
            {
                ocs.push_back(*cs);
            }
            outermost_cs.push_back(ocs);
        }
    }
}

// Constraint 22 in [Brandenburg 2020]
// Prevent any blocking critical section from being counted twice.
void PartitionedGIPPLP::add_cs_blocking_constraints()
{
    unsigned int x = 0;
    enumerate(taskset, tx, x)
    {
        if(tx->get_id() == ti.get_id())
            continue;

        unsigned int overlapping_jobs = max_overlapping_jobs(*tx);
        unsigned int y                = 0;
        CriticalSectionsOfTask cst    = taskset_cs[x];
        foreach(cst.get_cs(), cs)
        {
            if(cs->is_outermost())
            {
                for(int v = 0; v < (int) overlapping_jobs; v++)
                {
                    LinearExpression *exp = new LinearExpression();
                    unsigned int var_id;

                    var_id = vars.lookup(x, y, v, BLOCKING_TOKEN);
                    exp->add_var(var_id);

                    var_id = vars.lookup(x, y, v, BLOCKING_RSM);
                    exp->add_var(var_id);

                    add_inequality(exp, 1);
                }
                y++;
            }
        }
    }
}

// Number of times Ji issues an outermost request for a 􏰈􏰏resource in g.
unsigned int PartitionedGIPPLP::issued_outermost_request(const LockSet& g) const
{
    unsigned int outermost_request_count = 0;

    // Iterate over outermost cs of task i
    foreach(outermost_cs[i], ocs)
    {
        // The resource is in g?
        if(g.find(ocs->resource_id) != g.end())
        {
            outermost_request_count++;
        }
    }
    return outermost_request_count;
}

// Number of tasks in cluster k that request a resource in g.
unsigned int PartitionedGIPPLP::count_competing_tasks_in_cluster(
        const LockSet& g,
        unsigned int k) const
{
    unsigned int competing_tasks_count  = 0;
    bool is_competing                   = false;

    foreach_task_in_cluster(taskset, k, tx)
    {
        foreach(tx->get_requests(), req)
        {
            if(g.find(req->get_resource_id()) != g.end())
                is_competing = true;
        }
        if(is_competing)
            competing_tasks_count++;
    }

    return competing_tasks_count;
}

// Wi,g upper-bounds the number of times Ji must wait for
// a token of group g.
unsigned int PartitionedGIPPLP::compute_token_waiting_times(
        const LockSet& g) const
{
    unsigned int k                 = ti.get_cluster();
    unsigned int issued_outer_reqs = issued_outermost_request(g);
    unsigned int W_i_g;

    // Compute the number of times that other tasks require
    // a token while Ji is pending.
    unsigned int token_required_while_pending = 0;

    foreach_task_in_cluster(taskset, k, tx)
    {
        if(tx->get_id() == ti.get_id())
            continue;

        token_required_while_pending +=
                issued_outer_reqs * max_overlapping_jobs(*tx);

    }
    token_required_while_pending =
            token_required_while_pending - cluster_size + 1;

    // Compute Wi,g.
    if(count_competing_tasks_in_cluster(g, k) < cluster_size)
        W_i_g = 0;
    else
        W_i_g = std::min(issued_outer_reqs, token_required_while_pending);

    return W_i_g;
}

// Constraints 25 in [Brandenburg 2020]
void PartitionedGIPPLP::add_token_blocking_constraint()
{
    foreach(res_groups, g)
    {
        unsigned int x = 0;
        enumerate(taskset, tx, x)
        {
            if(tx->get_id() == ti.get_id())
                continue;

            const TaskInfo& t                       = *tx;

            unsigned int gt_waiting = compute_token_waiting_times(*g);
            unsigned int y          = 0;

            LinearExpression *exp = new LinearExpression();
            unsigned int var_id;

            enumerate(outermost_cs[x], ocs, y)
            {
                unsigned int q = ocs->resource_id;
                if((g->find(q) != g->end()))
                {
                    for(int v = 0; v < (int) max_overlapping_jobs(t); v++)
                    {
                        var_id = vars.lookup(x, y, v, BLOCKING_TOKEN);
                        exp->add_var(var_id);
                    }
                }
                y++;
            }
            add_inequality(exp, gt_waiting);
        }
    }
}

// Constraints 26 in [Brandenburg 2020]
void PartitionedGIPPLP::add_aggregate_token_blocking_constraint()
{
    foreach(res_groups, g)
    {
        unsigned int gt_waiting             = compute_token_waiting_times(*g);
        unsigned int cluster_ti              = ti.get_cluster();
        for(int k = 0; k < (int) cluster_size; k++)
        {
            unsigned int competing_tasks    = count_competing_tasks_in_cluster(*g, cluster_ti);

            LinearExpression *exp = new LinearExpression();
            unsigned int var_id;

            unsigned int x = 0;
            enumerate(taskset, tx, x)
            {
                if(tx->get_cluster() != cluster_ti)
                    continue;

                unsigned int y = 0;
                enumerate(outermost_cs[x], ocs, y)
                {
                    unsigned int q = ocs->resource_id;
                    if(g->find(q) != g->end())
                    {
                        for(int v = 0; v < (int) max_overlapping_jobs(*tx); v++)
                        {
                            var_id = vars.lookup(x, y, v, BLOCKING_TOKEN);
                            exp->add_var(var_id);
                        }
                    }
                }
            }
            add_inequality(exp, gt_waiting * std::min(cluster_size, competing_tasks));
        }
    }
}

// Constraints 27 in [Brandenburg 2020]
void PartitionedGIPPLP::add_percluster_RSM_constraint()
{
    foreach(res_groups, g)
    {
        //const CriticalSectionsOfTask& ti_cs = get_critical_section_of_task(info, cst, ti);
        unsigned int gt_waiting             = compute_token_waiting_times(*g);

        for(int k = 0; k < (int) (cpu_number / cluster_size); k++)
        {
            unsigned int competing_tasks    = count_competing_tasks_in_cluster(*g, k);
            unsigned int x                  = 0;
            LinearExpression *exp = new LinearExpression();
            unsigned int var_id;

            enumerate(taskset, tx, x)
            {
                if(tx->get_cluster() != ti.get_cluster())
                    continue;

                if(tx->get_id() != ti.get_id())
                    continue;

                unsigned int y = 0;
                enumerate(outermost_cs[x], ocs, y)
                {
                    unsigned int q = ocs->resource_id;

                    if(g->find(q) != g->end())
                    {
                        for(int v = 0; v < (int) max_overlapping_jobs(*tx); v++)
                        {
                            var_id = vars.lookup(x, y, v, BLOCKING_RSM);
                            exp->add_var(var_id);
                        }
                    }
                }
            }

            unsigned int exp_value;
            if((int) ti.get_cluster() == k)
                exp_value = gt_waiting * std::min(cluster_size, competing_tasks);
            else
                exp_value = gt_waiting * std::min(cluster_size - 1, competing_tasks -1);
            add_inequality(exp, exp_value);
        }
    }
}

// Compute the set of all combinations of resources in group g
// acquired by other tasks.
LockSets PartitionedGIPPLP::subset_acquired_by_other(const LockSet& g) const
{
    LockSets result;
    unsigned int x = 0;
    enumerate(taskset, tx, x)
    {
        if(tx->get_id() == ti.get_id())
            continue;

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
        result.push_back(ls);
    }
    return result;
}

// Check if resources accessed in cs are a subset of s.
bool PartitionedGIPPLP::cs_is_subset_of_s(
        unsigned int x,
        unsigned int y,
        const LockSet& s) const
{

    const CriticalSectionsOfTask& css_x = taskset_cs[x];
    //unsigned int cs_x_y_id       = css_x.get_cs()[y].resource_id;

    // LockSet for nested resources in cs.
    LockSet cs_lockset = css_x.get_nested_cs_resource(y);

    /*
    // Insert cs resource into his lockset.
    cs_lockset.insert(cs_x_y.resource_id);

    // Insert nested resources.
    unsigned int cs_index = 0;
    enumerate(css_x.get_cs(), cs, cs_index)
    {
        if(!cs->is_outermost())
        {
            if(css_x.get_outermost(cs_index) == cs_x_y.resource_id)
            {
                cs_lockset.insert(cs->resource_id);
            }
        }
    }*/

    return is_subset_of(cs_lockset, s);
}

// Check if ls an ls1 are possibly conflicting.
bool PartitionedGIPPLP::are_cs_conflicting(
        const LockSet& ls,
        const LockSet& ls1) const
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
    foreach(ls1, lb_id)
    {
        foreach(taskset_cs, cst)
        {
            foreach(cst->get_cs(), la)
            {
                if(ls.find(la->resource_id) != ls.end()
                    && la->outer == (int) *lb_id)
                    no_relation = false;
            }
        }
    }
    return (!empty_intersection) || (!no_relation);
}

// Compute the number of possibly conflicting outermost cs
unsigned int PartitionedGIPPLP::count_conflicting_outermost_cs(
        unsigned int x,
        const LockSet& s) const
{
    unsigned int result        = 0;

    unsigned int y = 0;
    enumerate(taskset_cs[x].get_cs(), cs, y)
    {
        if(cs->is_outermost())
        {
            LockSet nested_res = taskset_cs[x].get_nested_cs_resource(y);

            if(are_cs_conflicting(nested_res, s))
                result++;
        }
    }

    return result;
}

// Constraints 28 in [Brandenburg 2020]
void PartitionedGIPPLP::add_detailed_RSM_constraint(
        //VarMapper& vars,
        //const LockSets& ls,
        //const ResourceSharingInfo& info,
        //const TaskInfo& ti,
        //const CriticalSectionsOfTaskset& cst,
        //LinearProgram& lp
        )
{
    foreach(res_groups, g)
    {
        LockSets lss = subset_acquired_by_other(*g);

        foreach(lss, s)
        {
            for(int k = 0; k < (int) (cpu_number / cluster_size); k++)
            {
                unsigned int possibly_conflicting_cs = count_conflicting_outermost_cs(i, *s);
                unsigned int competing_tasks         = count_competing_tasks_in_cluster(*g, k);

                LinearExpression *exp = new LinearExpression();
                unsigned int var_id;
                unsigned int x = 0;

                enumerate(taskset, tx, x)
                {
                    if(tx->get_cluster() != ti.get_cluster())
                        continue;

                    TaskInfo t                       = *tx;
                    unsigned int y                   = 0;

                    enumerate(taskset_cs[x].get_cs(), cs, y)
                    {
                        if(!cs->is_outermost())
                            continue;

                        if(cs_is_subset_of_s(x, y, *s))
                        {
                            for(int v = 0; v < (int) max_overlapping_jobs(*tx); v++)
                            {
                                var_id = vars.lookup(x, y, v, BLOCKING_RSM);
                                exp->add_var(var_id);
                            }
                        }
                    }
                }

                unsigned int exp_value;
                if((int) ti.get_cluster() == k)
                    exp_value = possibly_conflicting_cs * std::min(cluster_size, competing_tasks);
                else
                    exp_value = possibly_conflicting_cs * std::min(cluster_size - 1, competing_tasks - 1);

                add_inequality(exp, exp_value);
            }
        }
    }
}

void PartitionedGIPPLP::add_gipp_constraints()
{
    add_cs_blocking_constraints();
    add_token_blocking_constraint();
    add_aggregate_token_blocking_constraint();
    add_percluster_RSM_constraint();
    add_detailed_RSM_constraint();
}

unsigned int PartitionedGIPPLP::total_length(
        unsigned int x,
        unsigned int y) const
{
    unsigned int length;

    const CriticalSectionsOfTask& cs_x = taskset_cs[x];
    const CriticalSection& cs_x_y      = taskset_cs[x].get_cs()[y];

    if(cs_x_y.is_outermost())
    {
        length = 0;
        unsigned int cs_index = 0;

        enumerate(cs_x.get_cs(), cs, cs_index)
        {
            if(cs->is_outermost())
                continue;

            if(cs_x.get_outermost(cs_index) == cs_x_y.resource_id)
                length += cs->length;
        }
    }
    else
    {
        length = cs_x_y.length;
    }

    return length;
}

void PartitionedGIPPLP::set_blocking_objective_gipp()
{
    LinearExpression *obj = get_objective();
    unsigned int x        = 0;

    enumerate(taskset, tx, x)
    {
        unsigned int overlaping_jobs        = max_overlapping_jobs(*tx);
        unsigned int y                      = 0;
        unsigned int cs_index               = 0;
        const CriticalSectionsOfTask& cst_x = taskset_cs[x];

        enumerate(cst_x.get_cs(), cs, cs_index)
        {
            if(cs->is_outermost())
            {
                unsigned int length = total_length(x, cs_index);
                for(int v = 0; v < (int) overlaping_jobs; v++)
                {
                    unsigned int var_id;

                    var_id = vars.lookup(x, y, v, BLOCKING_TOKEN);
                    obj->add_term(length, var_id);

                    var_id = vars.lookup(x, y, v, BLOCKING_RSM);
                    obj->add_term(length, var_id);
                }
                y++;
            }
        }
    }
}


unsigned long PartitionedGIPPLP::solve()
{
    Solution *solution;
    solution = linprog_solve(*this, vars.get_num_vars());

    long result = ceil(solution->evaluate(*get_objective()));

    delete solution;

    return result;
}


static BlockingBounds* _lp_gipp_bounds(
        const ResourceSharingInfo& info,
        const CriticalSectionsOfTaskset& cst,
        unsigned int cpu_num,
        unsigned int c_size)
{
    BlockingBounds* results = new BlockingBounds(info);

    for (unsigned int i = 0; i < info.get_tasks().size(); i++)
    {
        PartitionedGIPPLP lp(info, cst, i, cpu_num, c_size);
        (*results)[i] = lp.solve();
    }

    return results;
}

BlockingBounds* lp_gipp_bounds(
        const ResourceSharingInfo& info,
        const CriticalSectionsOfTaskset& cst,
        unsigned int cpu_num,
        unsigned int c_size)
{

    BlockingBounds *results = _lp_gipp_bounds(info, cst, cpu_num, c_size);

    return results;
}

