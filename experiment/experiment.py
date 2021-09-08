
"""
    Function to start the experiment for comparison
    between GIPP, OMIP, RNLP.
"""
from experiment.experiment_spec import experiments
from experiment.taskset_generator import generate_taskset
from experiment.taskset_spec import taskset_const


def run_experiment():

    # For each mcsl point, 200 samples are computed,
    # therefore each interval of 5 values contains 1000 samples
    n_sample   = 200
    mcsl_range = taskset_const["cs_length_nls_range"]
    mcsl_step  = taskset_const["mcsl_step"]

    # Iterates over experiment configurations
    for e in experiments:

        for i in xrange(mcsl_range[0], mcsl_range[1], mcsl_step):

            for v in xrange(i, i + mcsl_step, 1):

                for s in xrange(1, n_sample):

                    (t_gipp, t_omip, t_rnlp) = generate_taskset(
                        e["tasks_number"],
                        e["ls_tasks_number"],
                        e["utilization"],
                        e["cpu_number"],
                        e["group_conf"]["resources"],
                        e["group_conf"]["type"] ,
                        (mcsl_range[0], v),
                        e["cs_len_ls_range"],
                        e["max_request"],
                        taskset_const["max_issued_req_ls"],
                        taskset_const["period_ls"],
                        taskset_const["period_nls"],
                        taskset_const["resources_ls"],
                        taskset_const["resources_nls"])

