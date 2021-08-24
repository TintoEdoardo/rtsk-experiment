#include "lp_common.h"
#include "blocking.h"
#include "nested_cs.h"

BlockingBounds* lp_gipp_bounds(const ResourceSharingInfo& info,
                               const ResourceLocality& locality, bool use_RTA = true)
{
    /*
     *  To implement
     */
}

// Constraint 1 in [Brandenburg 2020]
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
        unsigned int max_overlapping_jobs = ceil(
                (ti.get_response() + tx->get_response()) / ti.get_period());
        unsigned int t = tx->get_id();
        foreach(cst.get_cs(), cs)
        {
            if(cs->is_outermost())
            {
                unsigned int q = cs->resource_id;
                for(unsigned int v = 0; v < max_overlapping_jobs; v++)
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