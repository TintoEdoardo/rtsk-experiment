#include "lp_common.h"
#include "blocking.h"
#include "nested_cs.h"

typedef std::vector<LockSet> ResourceGroup;
typedef std::vector<CriticalSections > OutermostCS;
typedef std::vector<std::vector<unsigned int > > Phi_i_g;
typedef std::vector<std::vector<unsigned int > > Beta_k_g;
typedef std::vector<unsigned int > TaskToResGroup;
typedef std::vector<unsigned int > WaitForToken;
typedef std::vector<std::vector<unsigned int > > LengthOutermostCS;



class PartitionedGIPPLP : protected LinearProgram
{

private:

    VarMapper vars;

    /* Index of the task under test. */
    const int i;
    const TaskInfo& ti;
    const CriticalSectionsOfTask& csi;
    const TaskInfos& taskset;
    const CriticalSectionsOfTasks& taskset_cs;

    /* Number of CPUs. */
    const unsigned int cpu_number;
    const unsigned int cluster_size;

    /* A vector of resource groups.  */
    ResourceGroup res_groups;

    /* A vector of set containing the set of outermost cs per task. */
    OutermostCS outermost_cs;

    /* Constants for constraints computation.
     * Names according to [Bandenburg 2020]. */
    Phi_i_g phi;
    Beta_k_g beta;
    WaitForToken W_i_g;
    TaskToResGroup task_to_group;
    ResourceGroup res_acquired_by_other;

    /* Not used now, might be useful for further extensions.  */
    LengthOutermostCS length_outermost_cs;

    /* Utility methods. */
    void compute_subset_acquired_by_other();
    void compute_token_waiting_times();
    unsigned int max_overlapping_jobs(const TaskInfo& tx) const;
    unsigned int count_conflicting_outermost_cs(const LockSet& s) const;
    bool are_cs_possibly_conflicting(const LockSet& ls,
                                     const LockSet& ls1) const;

    /* Constraints according to [Brandenburg 2020]. */
    void add_cs_blocking_constraints();
    void add_per_task_token_blocking_constraint();
    void add_per_cluster_token_blocking_constraint();
    void add_per_cluster_RSM_constraint();
    void add_per_task_RSM_constraint();

    /* Composable methods. */
    void add_gipp_constraints();
    void set_blocking_objective_gipp();


public:

    PartitionedGIPPLP(
            const ResourceSharingInfo& tsk,
            const CriticalSectionsOfTaskset& tsk_cs,
            const ResourceGroup& r_groups,
            const OutermostCS& outer_cs,
            int task_under_analysis,
            unsigned int cpu_num,
            unsigned int c_size,
            Phi_i_g p,
            Beta_k_g b,
            TaskToResGroup ttg,
            LengthOutermostCS l_ocs);

    /* LP solver invocation here. */
    unsigned long solve();

};

PartitionedGIPPLP::PartitionedGIPPLP(
        const ResourceSharingInfo& tsk,
        const CriticalSectionsOfTaskset& tsk_cs,
        const ResourceGroup& r_groups,
        const OutermostCS& outer_cs,
        int task_under_analysis,
        unsigned int cpu_num,
        unsigned int c_size,
        Phi_i_g ps,
        Beta_k_g bs,
        TaskToResGroup ttg,
        LengthOutermostCS l_ocs)
      : i(task_under_analysis),
        ti(tsk.get_tasks()[i]),
        csi(tsk_cs.get_tasks()[i]),
        taskset(tsk.get_tasks()),
        taskset_cs(tsk_cs.get_tasks()),
        cpu_number(cpu_num),
        cluster_size(c_size),
        res_groups(r_groups),
        outermost_cs(outer_cs),
        phi(ps),
        beta(bs),
        task_to_group(ttg),
        res_acquired_by_other(),
        length_outermost_cs(l_ocs)
{

    compute_token_waiting_times();

    compute_subset_acquired_by_other();

    set_blocking_objective_gipp();

    vars.seal();

    add_gipp_constraints();

}

/* Compute the number of jobs of task T_x
 * overlapping with the job J_i of T_i. */
unsigned int PartitionedGIPPLP::max_overlapping_jobs(const TaskInfo& tx) const
{
    return tx.get_max_num_jobs(ti.get_response());
}

/* Constraint 22 in [Brandenburg 2020]
 * Prevent any blocking critical section from being counted twice. */
void PartitionedGIPPLP::add_cs_blocking_constraints()
{

    unsigned int x = 0;
    enumerate(taskset, tx, x)
    {

        /* The task ti does not contribute to
         * to blocking calculation. */
        if(x == (unsigned) i)
        {
            continue;
        }

        unsigned int overlapping_jobs = max_overlapping_jobs(*tx);
        unsigned int y                = 0;
        CriticalSectionsOfTask cst    = taskset_cs[x];
        foreach(cst.get_cs(), cs)
        {

            /* Only outermost critical sections
             * are considered. */
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

/* Wi,g upper-bounds the number of times Ji must wait for
 * a token of group g. */
void PartitionedGIPPLP::compute_token_waiting_times()
{

    unsigned int k = ti.get_cluster();
    W_i_g          = std::vector<unsigned int >(res_groups.size());

    unsigned int g_index;
    enumerate(res_groups, group, g_index)
    {

        if(beta[k][g_index] < cluster_size)
        {

            W_i_g[g_index] = 0;

        }
        else
        {
            int phi_2 = 0;
            unsigned int phi_1 = phi[i][g_index];
            unsigned int tx_index;
            enumerate(taskset, tx, tx_index)
            {

                /* Skip itself. */
                if(tx_index == (unsigned) i)
                {
                    continue;
                }

                /* Skip tasks in other clusters. */
                if(tx->get_cluster() != k)
                {
                    continue;
                }

                phi_2 += (int)phi[tx_index][g_index] * (int)max_overlapping_jobs(*tx);

            }

            phi_2 -= ((int)cluster_size - 1);

            W_i_g[g_index] = std::min((int)phi_1, phi_2);
        }

    }

}

/* Constraints 25 in [Brandenburg 2020].
 * Establish a constraint on token blocking due
 * to each task. */
void PartitionedGIPPLP::add_per_task_token_blocking_constraint()
{

    unsigned int g_index;
    enumerate(res_groups, group, g_index)
    {

        unsigned int tx_index;
        enumerate(taskset, tx, tx_index)
        {

            if((int)tx_index == i)
            {
                continue;
            }

            unsigned int overlapping_jobs = max_overlapping_jobs(*tx);
            unsigned int x                = tx_index;

            LinearExpression *exp = new LinearExpression();
            unsigned int var_id;

            unsigned int y;
            enumerate(outermost_cs[tx_index], ocs, y)
            {

                unsigned int q = ocs->resource_id;

                if((group->find(q) != group->end()))
                {

                    for(int v = 0; v < (int) overlapping_jobs; v++)
                    {
                        var_id = vars.lookup(x, y, v, BLOCKING_TOKEN);
                        exp->add_var(var_id);
                    }

                }

            }

            add_inequality(exp, W_i_g[g_index]);
        }

    }

}

/* Constraints 26 in [Brandenburg 2020].
 * Establish a bound on the aggregate token blocking
 * across all tasks in each cluster. */
void PartitionedGIPPLP::add_per_cluster_token_blocking_constraint()
{

    unsigned int g_index;
    enumerate(res_groups, group, g_index)
    {

        unsigned int cluster_number = cpu_number / cluster_size;

        /* Iterate over each clusters. */
        for(unsigned int k = 0; k <  cluster_number; k++)
        {

            LinearExpression *exp = new LinearExpression();
            unsigned int var_id;

            unsigned int tx_index;
            enumerate(taskset, tx, tx_index)
            {

                unsigned int x = tx_index;

                /* Exclude the task under test. */
                if((int)tx_index == i)
                {
                    continue;
                }

                /* Exclude tasks in other clusters. */
                if(tx->get_cluster() != k)
                {
                    continue;
                }

                unsigned int y = 0;
                enumerate(outermost_cs[x], ocs, y)
                {

                    unsigned int q = ocs->resource_id;
                    if(group->find(q) != group->end())
                    {

                        unsigned int overlapping_jobs = max_overlapping_jobs(*tx);

                        for(int v = 0; v < (int) overlapping_jobs; v++)
                        {

                            var_id = vars.lookup(x, y, v, BLOCKING_TOKEN);
                            exp->add_var(var_id);

                        }

                    }

                }

            }

            add_inequality(exp, W_i_g[g_index] * std::min(cluster_size, beta[k][g_index]));
        }

    }

}

/* Constraints 27 in [Brandenburg 2020].
 * Establish a bound on per-cluster RSM blocking across
 * all tasks in the system. */
void PartitionedGIPPLP::add_per_cluster_RSM_constraint()
{

    unsigned int g_index;
    enumerate(res_groups, group, g_index)
    {

        unsigned int cluster_number = cpu_number / cluster_size;

        for(int k = 0; k < (int) cluster_number; k++)
        {

            LinearExpression *exp = new LinearExpression();
            unsigned int var_id;

            unsigned int tx_index;
            enumerate(taskset, tx, tx_index)
            {

                unsigned int x = tx_index;

                /* Exclude the task under test. */
                if((int)tx_index == i)
                {
                    continue;
                }

                /* Exclude tasks in other clusters. */
                if((int) tx->get_cluster() != k)
                {
                    continue;
                }

                unsigned int overlapping_jobs = max_overlapping_jobs(*tx);
                unsigned int y                = 0;

                enumerate(outermost_cs[x], ocs, y)
                {

                    unsigned int q = ocs->resource_id;

                    if(group->find(q) != group->end())
                    {

                        for(int v = 0; v < (int) overlapping_jobs; v++)
                        {

                            var_id = vars.lookup(x, y, v, BLOCKING_RSM);
                            exp->add_var(var_id);

                        }

                    }

                }

            }

            unsigned int exp_value;
            unsigned int b = beta[k][g_index];

            if((int) ti.get_cluster() != k)
            {

                exp_value = phi[i][g_index] * std::min(cluster_size, b);

            }
            else
            {

                exp_value = phi[i][g_index] * std::min(cluster_size - 1, b - 1);

            }

            add_inequality(exp, exp_value);

        }

    }
}

/* Compute the set of all combinations of resources in group g
 * acquired by other tasks. */
void PartitionedGIPPLP::compute_subset_acquired_by_other()
{

    unsigned int tx_index;
    enumerate(taskset, tx, tx_index)
    {
        /* Exclude the task under test. */
        if((int)tx_index == i)
        {
            continue;
        }

        /* Exclude tasks accessing resources in
         * other groups. */
        if(task_to_group[i] != task_to_group[tx_index])
        {
            continue;
        }

        const CriticalSectionsOfTask& tx_cs = taskset_cs[tx_index];
        LockSet tx_ocs_res;

        unsigned int cs_index;
        enumerate(tx_cs.get_cs(), cs, cs_index)
        {

            /* Exclude nested cs. */
            if(!cs->is_outermost())
            {
                continue;
            }

            tx_ocs_res = taskset_cs[tx_index].get_nested_cs_resource(cs_index);
            tx_ocs_res.insert(cs->resource_id);

        }

        res_acquired_by_other.push_back(tx_ocs_res);

    }

}

/* Check if ls an ls1 are possibly conflicting. */
bool PartitionedGIPPLP::are_cs_possibly_conflicting(
        const LockSet& ls,
        const LockSet& ls1) const
{

    // Check if the intersection between ls and ls1 is empty
    bool empty_intersection = is_disjoint(ls, ls1);

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

/* Compute the number of possibly conflicting outermost cs.
 * This value is identified as Fi(s). */
unsigned int PartitionedGIPPLP::count_conflicting_outermost_cs(
        const LockSet& s) const
{

    unsigned int conflicting_outer_cs = 0;
    unsigned int y                    = 0;
    enumerate(csi.get_cs(), cs, y)
    {

        if(cs->is_outermost())
        {

            LockSet nested_res = csi.get_nested_cs_resource(y);

            if(are_cs_possibly_conflicting(nested_res, s))
            {

                conflicting_outer_cs++;

            }

        }

    }

    return conflicting_outer_cs;
}

/* Constraints 28 in [Brandenburg 2020].
 * Establish a constraint on RSM blocking globally. */
void PartitionedGIPPLP::add_per_task_RSM_constraint()
{

    unsigned int g_index;
    enumerate(res_groups, group, g_index)
    {

        unsigned int s_index;
        enumerate(res_acquired_by_other, s, s_index)
        {

            unsigned int cluster_number = cpu_number / cluster_size;

            for(int k = 0; k < (int) cluster_number; k++)
            {

                LinearExpression *exp = new LinearExpression();
                unsigned int var_id;

                unsigned int tx_index;
                enumerate(taskset, tx, tx_index)
                {

                    /* Exclude the task under test. */
                    if((int)tx_index == i)
                    {
                        continue;
                    }

                    /* Exclude tasks in other clusters. */
                    if((int) tx->get_cluster() != k)
                    {
                        continue;
                    }

                    unsigned int overlapping_jobs = max_overlapping_jobs(*tx);
                    unsigned int x                = tx_index;
                    unsigned int y                = 0;
                    unsigned int cs_index;

                    enumerate(taskset_cs[x].get_cs(), cs, cs_index)
                    {

                        /* Exclude nested critical sections. */
                        if(!cs->is_outermost())
                        {
                            continue;
                        }

                        LockSet s_x_y = taskset_cs[x].get_nested_cs_resource(cs_index);

                        if(is_subset_of(s_x_y, *s))
                        {

                            for(int v = 0; v < (int) overlapping_jobs; v++)
                            {

                                var_id = vars.lookup(x, y, v, BLOCKING_RSM);
                                exp->add_var(var_id);

                            }

                        }

                        y++;

                    }

                }

                unsigned int exp_value;
                unsigned int f_i_s = count_conflicting_outermost_cs(*s);

                if((int) ti.get_cluster() != k)
                {

                    exp_value = f_i_s * std::min(cluster_size, beta[k][g_index]);

                }
                else
                {
                    exp_value = f_i_s * std::min(cluster_size - 1, beta[k][g_index] - 1);
                }

                add_inequality(exp, exp_value);

            }

        }

    }

}

void PartitionedGIPPLP::add_gipp_constraints()
{
    add_cs_blocking_constraints();
    add_per_task_token_blocking_constraint();
    add_per_cluster_token_blocking_constraint();
    add_per_cluster_RSM_constraint();
    add_per_task_RSM_constraint();
}

void PartitionedGIPPLP::set_blocking_objective_gipp()
{

    LinearExpression *obj = get_objective();
    unsigned int tx_index;

    enumerate(taskset, tx, tx_index)
    {

        /* Exclude the tas under test. */
        if((int)tx_index == i)
        {
            continue;
        }

        unsigned int overlaping_jobs        = max_overlapping_jobs(*tx);
        unsigned int x                      = tx_index;
        unsigned int y                      = 0;
        unsigned int cs_index               = 0;
        const CriticalSectionsOfTask& cst_x = taskset_cs[x];

        enumerate(cst_x.get_cs(), cs, cs_index)
        {

            if(cs->is_outermost())
            {

                unsigned int length = cs->length;

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


/*
 *  The next functions have the purpose to execute the
 *  schedulability test on the taskest provided in input.
 *  In addition, resource groups are computed with auxiliary
 *  functions, once for all the tasks in the taskset.
 */

/*  Generate the resource groups for
 *  standard GIPP schedulability test. */
static void generate_resource_groups(
        const TaskInfos& info,
        const CriticalSectionsOfTasks& cst,
        ResourceGroup & resource_groups)
{

    ResourceGroup r_group;

    unsigned int ti_index;

    enumerate(info, ti, ti_index)
    {
        const CriticalSections& cs_ti = cst[ti_index].get_cs();

        unsigned int cs_index;

        enumerate(cs_ti, cs, cs_index)
        {
            /* The idea is to insert couple of elements,
             * merging them as the algorithm proceeds. */

            LockSet ls;

            unsigned int cs_outer_index   = cst[ti_index].get_outermost(cs_index);
            unsigned int cs_outer_id      = cst[ti_index].get_cs()[cs_outer_index].resource_id;
            unsigned int cs_current_id    = cs->resource_id;

            ls.insert(cs_outer_id);
            ls.insert(cs_current_id);

            /* Boolean guard to check if the group has
             * been inserted.  */
            bool is_group_inserted = false;

            for(unsigned int g = 0; g < r_group.size(); g++)
            {
                /* get_union avoid inserting duplicate,
                 * therefore no further check is needed */
                if(!is_disjoint(r_group[g], ls))
                {
                    r_group[g] = get_union(r_group[g], ls);
                    is_group_inserted = true;
                }

            }

            if(!is_group_inserted)
            {
                r_group.push_back(ls);
            }

        }
    }

    // Then we merge the intersecting sets
    for(unsigned int g_index = 0; g_index < r_group.size(); g_index++)
    {
        if(r_group[g_index].empty())
        {
            continue;
        }

        for(unsigned int g_index_2 = g_index + 1; g_index_2 < r_group.size(); g_index_2++)
        {
            if(!is_disjoint(r_group[g_index], r_group[g_index_2]))
            {
                r_group[g_index] = get_union(r_group[g_index], r_group[g_index_2]);
                r_group[g_index_2].clear();
            }
        }

        resource_groups.push_back(r_group[g_index]);

    }

}

/*  Generate the single resource group for
 *  RNLP schedulability test. */
static void generate_single_resource_group(
        const CriticalSectionsOfTaskset& cst,
        LockSets& resource_groups)
{
    // Single group for all he resources
    LockSet group;

    unsigned int t;
    enumerate(cst.get_tasks(), ti, t)
    {

        unsigned int c;
        enumerate(ti->get_cs(), cs, c)
        {
            group.insert(cs->resource_id);
        }
    }

    resource_groups.push_back(group);
}

/*  Generate a vector of outermost critical sections
 *  where the nth value corresponds to a sorted set of
 *  outermost requests issued by the nth task. */
static void generate_outer_cs_group(
        const CriticalSectionsOfTaskset& cst,
        OutermostCS& outer_cs)
{
    unsigned int t_index;
    enumerate(cst.get_tasks(), t, t_index)
    {

        CriticalSections outer_cs_t;
        foreach(t->get_cs(), cs)
        {

            if(cs->is_outermost())
                outer_cs_t.push_back(*cs);

        }

        outer_cs.push_back(outer_cs_t);

    }

}

/* Instead of computing this constants per request,
 * iterating multiple time over the same tasks, we
 * initialize them before creating the taskset. */
void initialize_taskset_constants(
        const ResourceSharingInfo& info,
        const CriticalSectionsOfTaskset& cst,
        const ResourceGroup& rgs,
        const unsigned int n_clusters,
        Phi_i_g& phi_i_g,
        Beta_k_g& beta_k_g,
        TaskToResGroup& task_to_group,
        LengthOutermostCS& l_ocs)
{

    unsigned int n_groups = rgs.size();
    unsigned int n_res;

    /* Initialize some structures to ease
     * the further computations.
     * resources_to_groups is a vector where the nth
     * element corresponds to the index of the group
     * to which it is associated. */
    std::set<unsigned int> resources;
    foreach(cst.get_tasks(), t_cs)
    {

        foreach(t_cs->get_cs(), cs)
        {

            /* If cs-resource_id has been already inserted,
             * insert method avoid duplication. */
            resources.insert(cs->resource_id);

        }

    }
    n_res = resources.size();
    std::vector <unsigned  int> resources_to_groups;
    resources_to_groups = std::vector <unsigned  int>(n_res, 0);
    for(unsigned int g_index = 0; g_index < n_groups; g_index++)
    {

        unsigned int r_index;
        enumerate(rgs[g_index], res, r_index)
        {

            resources_to_groups[*res] = g_index;

        }

    }

    /* Initialize beta_k_g. */
    for(unsigned int k = 0; k < n_clusters; k++)
    {

        beta_k_g.push_back(std::vector<unsigned int>(n_groups, 0));

    }

    /* Initialize task_to_group. */
    task_to_group = std::vector<unsigned int >(info.get_tasks().size());

    /* Initialize l_ocs. */
    for(unsigned int t = 0; t < info.get_tasks().size(); t++)
    {

        l_ocs.push_back(std::vector<unsigned int>());

    }

    const TaskInfos& tasks = info.get_tasks();

    unsigned int tx_index;
    enumerate(tasks, tx, tx_index)
    {

        const CriticalSectionsOfTask& tx_cs = cst.get_tasks()[tx_index];

        /* Initialize phi_i_g in order to
        * be able to access its elements. */
        phi_i_g.push_back(std::vector<unsigned int>(n_groups, 0));

        unsigned int tx_cluster         = tx->get_cluster();
        unsigned int y                  = 0;
        bool has_tx_contributed_to_beta = false;
        bool has_tx_contributed_to_ttg  = false;

        unsigned int cs_index;
        enumerate(tx_cs.get_cs(), cs, cs_index)
        {

            unsigned int g_index = resources_to_groups[cs->resource_id];

            /* Phi counts only outermost request. */
            if(cs->is_outermost())
            {
                phi_i_g[tx_index][g_index]++;

                l_ocs[tx_index].push_back(cs->length);

                /* Iterate on critical sections again. */
                unsigned int cs_index_2;
                enumerate(tx_cs.get_cs(), cs_2, cs_index_2)
                {

                    int outer_index = tx_cs.get_outermost(cs_index_2);

                    /* Exclude outermost critical sections. */
                    if(outer_index == -1)
                    {
                        continue;
                    }

                    /* Exclude critical sections that are nested
                     * to other outermost critical sections. */
                    if(outer_index != (int) cs_index)
                    {
                        continue;
                    }

                    l_ocs[tx_index][y] += cs_2->length;

                }

                y++;

            }

            if(!has_tx_contributed_to_beta)
            {
                beta_k_g[tx_cluster][g_index]++;
                has_tx_contributed_to_beta = true;
            }

            if(!has_tx_contributed_to_ttg)
            {
                task_to_group[tx_index]   = g_index;
                has_tx_contributed_to_ttg = true;
            }

        }

    }

}

BlockingBounds* lp_gipp_bounds(
        const ResourceSharingInfo& info,
        const CriticalSectionsOfTaskset& cst,
        unsigned int cpu_num,
        unsigned int c_size,
        bool apply_to_rnlp = false)
{

    BlockingBounds* results = new BlockingBounds(info);

    /* Define taskset constants.  */
    ResourceGroup r_group;
    OutermostCS outer_cs;
    TaskToResGroup task_to_group;
    Phi_i_g phi;
    Beta_k_g beta;
    LengthOutermostCS l_ocs;

    /* To adapt GIPP analysis to RNLP we
     * must ensure that all resources belong
     * to the same groups. */
    if(apply_to_rnlp)
        generate_single_resource_group(cst, r_group);
    else
        generate_resource_groups(info.get_tasks(), cst.get_tasks(), r_group);

    /* Initialize outer_cs. */
    generate_outer_cs_group(cst, outer_cs);

    /* Initialize constants. */
    initialize_taskset_constants(
            info,
            cst,
            r_group,
            cpu_num / c_size,
            phi,
            beta,
            task_to_group,
            l_ocs);

    /* Tests are performed, group computation is
     * done exactly once per taskset. */
    for (unsigned int i = 0; i < info.get_tasks().size(); i++)
    {
        PartitionedGIPPLP lp(info, cst, r_group, outer_cs, (int) i, cpu_num, c_size, phi, beta, task_to_group, l_ocs);
        (*results)[i] = lp.solve();
    }

    return results;
}

