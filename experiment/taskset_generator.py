
""""
    Generates the tasksets for he experiment using Emstada generator.
"""

    import schedcat.model.resources as res
    import random
    import schedcat.generator.generator_emstada as gen_emstada


def generate_taskset(
        n_tasks,
        n_ls_tasks,
        utilization,
        #num_res_ls = 3,
        #num_res_nls = 12,
        #group_size_nls = 3,
        #group_type_nls = GROUP_WIDE,
        period_ls = [1, 2, 4, 5, 8],
        period_nls = [10, 20, 25, 40, 50, 100, 125, 200, 250, 500, 1000],
        max_issued_outer_req_ls = 2,
        max_issued_outer_req_nls = 3,
        cs_length_ls = (1, 15),
        cs_length_nls = (5, 1000),
        top_probability = 0.5
):

    number_of_tasks     = n_tasks
    number_of_ls_tasks  = n_ls_tasks
    number_of_nls_tasks = n_tasks - n_ls_tasks

    # Value fixed arbitrary
    utilization_ls  = utilization * (number_of_ls_tasks / number_of_tasks)
    utilization_nls = utilization - utilization_ls

    # Taskset generation
    taskset_ls   = gen_emstada.gen_taskset(period_ls, period_ls, number_of_ls_tasks, utilization_ls, scale=ms2us)
    taskset_nls = gen_emstada.gen_taskset(period_nls, period_nls, number_of_nls_tasks, utilization_nls, scale=ms2us)

    res.initialize_nested_resource_model(taskset_ls)
    res.initialize_nested_resource_model(taskset_nls)

    return (ts, ts_nn)
