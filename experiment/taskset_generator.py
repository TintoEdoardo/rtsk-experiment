""""
    Generates the tasksets for he experiment using Emstada generator.
"""

import schedcat.model.resources as res
import random
import group_spec as group_spec
import schedcat.generator.generator_emstada as gen_emstada

from taskset_generator_util import generate_groups, assign_cs, compute_groups_number, assign_cs_asymmetric
from schedcat.util.time import ms2us

"""
    GENERATE_TASKSET
    Input:
        taskset configuraton parameters
        
    Output:
        a tuple of three element, the first is the taskset for GIPP tests,
        the second the taskset for OMIP tests, the third the taskset for RNLP tests. 
        [Note. GIPP and RNLP taskset are identical, consider removing the latter]
        
"""
def generate_taskset(
        n_tasks,
        n_ls_tasks,
        utilization,
        utilization_ls,
        n_cpu,
        group_size_nls,
        group_type_nls,
        cs_length_nls_range,
        cs_length_ls_range,
        max_issued_req_nls,
        max_issued_req_ls,
        period_ls,
        period_nls,
        n_resources_ls,
        n_resources_nls,
        asymmetric_comp):

    # Initialize values
    number_of_tasks     = n_tasks
    number_of_ls_tasks  = n_ls_tasks
    number_of_nls_tasks = n_tasks - n_ls_tasks

    utilization_nls = utilization - utilization_ls

    # Define taskset for experiment:
    # - RNLP requires a single global lock for all resources in the system,
    # - OMIP supports coarse-grained locking, therefore nested requests should be implemented
    #   as group locks,
    # - GIPP supports fine-grained locking.

    # Taskset generation
    taskset_ls = None
    if number_of_ls_tasks > 0:

        taskset_ls  \
            = gen_emstada.gen_taskset(period_ls, period_ls, number_of_ls_tasks, utilization_ls, scale=ms2us)

        res.initialize_nested_resource_model(taskset_ls)

        t_ls_gipp  = taskset_ls

    taskset_nls \
        = gen_emstada.gen_taskset(period_nls, period_nls, number_of_nls_tasks, utilization_nls, scale=ms2us)

    res.initialize_nested_resource_model(taskset_nls)

    t_nls_gipp = taskset_nls

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
        elif group_type_nls == "wide_2":
            group_conf_nls = group_spec.group_4_wide_2
        else:
            group_conf_nls = group_spec.group_4_deep
            group_conf_ls = group_spec.group_3_deep

    else:
        return 0

    # Assign tasks to group
    group_associations_nls \
        = generate_groups(number_of_nls_tasks, n_resources_nls, group_conf_nls)

    if number_of_ls_tasks > 0:

        group_associations_ls  \
            = generate_groups(number_of_ls_tasks, n_resources_ls, group_conf_ls)

        # Initialize ls requests array
        issued_req_ls = [random.randint(1, max_issued_req_ls) for _ in range(0, number_of_ls_tasks)]

        # Assign critical sections
        critical_sections_ls \
            = assign_cs(group_conf_ls, group_associations_ls, number_of_ls_tasks, issued_req_ls, n_resources_ls)

        # Compute critical section length for ls tasks
        critical_sections_length_ls = [[] for _ in xrange(0, number_of_ls_tasks)]
        for t in xrange(0, number_of_ls_tasks):

            for i in xrange(0, len(critical_sections_ls[t])):

                cs = []

                for j in xrange(0, len(critical_sections_ls[t][i])):
                    length = random.randint(cs_length_ls_range[0], cs_length_ls_range[1])
                    cs.append(length)

                critical_sections_length_ls[t].append(cs)

        # Generate critical sections for ls tasks
        ls_offset = group_conf_nls["resources"]

        for i in xrange(0, len(t_ls_gipp)):

            task_i     = t_ls_gipp[i]
            cs_task_i  = critical_sections_ls[i]
            csl_task_i = critical_sections_length_ls[i]

            for ocs in xrange(0, len(cs_task_i)):

                ocs_length = csl_task_i[ocs][0]
                o_res_index = cs_task_i[ocs][0] + ls_offset
                outer_cs \
                    = task_i.critical_sections.add_outermost(o_res_index, ocs_length)

                for cs in xrange(1, len(cs_task_i[ocs])):

                    cs_length = csl_task_i[ocs][cs]
                    res_index = cs_task_i[ocs][cs] + ls_offset
                    nested_cs \
                        = task_i.critical_sections.add_nested(outer_cs, res_index, cs_length)

    # Initialize nls requests array
    issued_req_nls = [random.randint(1, max_issued_req_nls) for _ in range(0, number_of_nls_tasks)]

    # Just for asymmetric critical sections assignment
    if asymmetric_comp:
        issued_req_nls[0] = len(group_conf_nls["minimal_requests"])
        critical_sections_nls  \
            = assign_cs_asymmetric(
                group_conf_nls,
                group_associations_nls,
                number_of_nls_tasks,
                issued_req_nls,
                n_resources_nls)

    else:
        critical_sections_nls \
            = assign_cs(
                group_conf_nls,
                group_associations_nls,
                number_of_nls_tasks,
                issued_req_nls,
                n_resources_nls)

    # Compute critical section length for nls tasks
    critical_sections_length_nls = [[] for _ in range(0, number_of_nls_tasks)]
    for t in range(0, number_of_nls_tasks):

        for i in xrange(0, issued_req_nls[t]):

            cs = []

            # According to [Brandenburg 2020] each outermost
            # critical section contains at most one critical section
            # (outermost_cs, nested_cs) = critical_sections_nls[t]
            for j in xrange(0, len(critical_sections_nls[t][i])):
                length = random.randint(cs_length_nls_range[0], cs_length_nls_range[1])
                cs.append(length)

            critical_sections_length_nls[t].append(cs)

    # Generate critical sections for nls tasks.
    # Iterate over resource groups, in order to distinguish between
    # resources accessed in each group
    number_of_groups_nls = compute_groups_number(n_resources_nls, group_conf_nls)
    for g in xrange(0, number_of_groups_nls):

        # Offset over resource index
        group_offset = g * group_conf_nls["resources"]

        for i in xrange(0, len(t_nls_gipp)):

            if group_associations_nls[i] == g:

                task_i     = t_nls_gipp[i]
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

    # Compute the taskset for GIPP, OMIP and RNLP
    # GIPP:
    if number_of_ls_tasks > 0:
        t_nls_gipp.extend(t_ls_gipp)

    # OMIP
    t_omip = t_nls_gipp.copy()
    res.convert_to_group_locks(t_omip)

    # RNLP
    t_rnlp = t_nls_gipp.copy()

    return t_nls_gipp, t_omip, t_rnlp
