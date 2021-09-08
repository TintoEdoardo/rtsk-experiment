""""
    Generates the tasksets for he experiment using Emstada generator.
"""

import schedcat.model.resources as res
import random
import experiment.group_spec as group_spec
import schedcat.generator.generator_emstada as gen_emstada

from experiment.taskset_generator_util import generate_groups, assign_cs, compute_groups_number
from experiment.taskset_spec import taskset_const
from schedcat.util.time import ms2us

"""
    GENERATE_TASKSET
    Input:
        
    Output:
        
"""
def generate_taskset(
        n_tasks,
        n_ls_tasks,
        utilization,
        n_cpu,
        group_size_nls,
        group_type_nls,
        cs_length_nls_range=taskset_const["cs_len_nls_range"],
        cs_length_ls_range=taskset_const["cs_len_ls_range"],
        max_issued_req_nls=taskset_const["max_issued_req_nls"],
        max_issued_req_ls=taskset_const["max_issued_req_ls"],
        period_ls=taskset_const["period_ls"],
        period_nls=taskset_const["period_nls"],
        n_resources_ls=taskset_const["resources_ls"],
        n_resources_nls=taskset_const["resources_nls"]
):

    # Initialize values
    number_of_tasks     = n_tasks
    number_of_ls_tasks  = n_ls_tasks
    number_of_nls_tasks = n_tasks - n_ls_tasks

    # Utilization is chosen arbitrary
    utilization_ls = utilization * (number_of_ls_tasks / number_of_tasks)
    utilization_nls = utilization - utilization_ls

    # Taskset generation
    taskset_ls  = gen_emstada.gen_taskset(period_ls, period_ls, number_of_ls_tasks, utilization_ls, scale=ms2us)
    taskset_nls = gen_emstada.gen_taskset(period_nls, period_nls, number_of_nls_tasks, utilization_nls, scale=ms2us)

    t_ls  = res.initialize_nested_resource_model(taskset_ls)
    t_nls = res.initialize_nested_resource_model(taskset_nls)


    # Identify group configuration
    group_conf_nls = "not identified yet"
    group_conf_ls = group_spec.group_3_wide
    if group_size_nls == 1:
        group_conf_nls = group_spec.group_1

    elif group_size_nls == 3:
        if group_type_nls == "wide":
            group_conf_nls = group_spec.group_3_wide

    elif group_size_nls == 4:
        if group_type_nls == "wide":
            group_conf_nls = group_spec.group_4_wide
        else:
            group_conf_nls = group_spec.group_4_deep
            group_conf_ls = group_spec.group_3_deep

    else:
        return 0

    # Assign tasks to group
    group_associations_nls \
        = generate_groups(number_of_nls_tasks, n_resources_nls, group_conf_nls)

    group_associations_ls  \
        = generate_groups(number_of_ls_tasks, n_resources_ls, group_conf_ls)

    # Assign critical sections
    critical_sections_nls \
        = assign_cs(group_conf_nls, group_associations_nls, number_of_nls_tasks, max_issued_req_nls)

    critical_sections_ls  \
        = assign_cs(group_conf_ls, group_associations_ls, number_of_ls_tasks, max_issued_req_ls)

    # Compute critical section length for nls tasks
    critical_sections_length_nls = [[] for i in xrange(1, number_of_nls_tasks)]
    for t in xrange(1, number_of_nls_tasks):

        for i in xrange(0, len(critical_sections_nls[t])):

            # According to [Brandenburg 2020] each outermost
            # critical section contains at most one critical section
            # (outermost_cs, nested_cs) = critical_sections_nls[t]
            for j in xrange(0, len(critical_sections_nls[t][i])):
                length = random.randint(cs_length_nls_range[0], cs_length_nls_range[1])
                critical_sections_length_nls[t][i][j] = length

    # Compute critical section length for ls tasks
    critical_sections_length_ls = [[] for i in xrange(1, number_of_ls_tasks)]
    for t in xrange(1, number_of_ls_tasks):

        for i in xrange(0, len(critical_sections_ls[t])):

            for j in xrange(0, len(critical_sections_ls[t][i])):
                length = random.randint(cs_length_ls_range[0], cs_length_ls_range[1])
                critical_sections_length_ls[t][i][j] = length

    # Generate critical sections for nls tasks.
    # Iterate over resource groups, in order to distinguish between
    # resources accessed in each group
    number_of_groups = compute_groups_number(n_resources_nls, group_conf_nls)
    for g in xrange(0, number_of_groups):

        # Offset over resource index
        group_offset = g * group_conf_nls["resources"]

        for i in xrange(0, len(t_nls)):

            if group_associations_nls[i] == g:

                task_i     = t_nls[i]
                cs_task_i  = critical_sections_nls[i]
                csl_task_i = critical_sections_length_nls[i]

                for ocs in xrange(0, len(cs_task_i)):

                    ocs_length = csl_task_i[ocs][0]
                    o_res_index = cs_task_i[ocs][0] + group_offset
                    outer_cs    \
                        = task_i.critical_sections.add_outermost(o_res_index, ocs_length)

                    for cs in xrange(1, len(cs_task_i[ocs])):

                        cs_length = csl_task_i[ocs][cs]
                        res_index = cs_task_i[ocs][cs] + group_offset
                        nested_cs \
                            = task_i.critical_sections.add_nested(outer_cs, res_index, cs_length)



    return ()
