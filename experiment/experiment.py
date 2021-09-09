
"""
    Function to start the experiment for comparison
    between GIPP, OMIP, RNLP.
"""
from experiment.experiment_spec import experiments, cluster_size
from experiment.experiment_util import perform_schedulability_test
from experiment.taskset_generator import generate_taskset
from experiment.taskset_spec import taskset_const
from schedcat.locking.bounds import apply_omip_bounds, apply_gipp_bounds, assign_edf_preemption_levels


def run_experiment():

    # For each mcsl point, 200 samples are computed,
    # therefore each interval of 5 values contains 1000 samples
    n_sample   = 200
    mcsl_range = taskset_const["cs_length_nls_range"]
    mcsl_step  = taskset_const["mcsl_step"]

    # Computed samples
    samples = [[] for _ in experiments]

    # Iterates over experiment configurations
    for e in experiments:

        # samples[e] contains a list organized as follows:
        #   samples[e][0] = gipp samples
        #   samples[e][1] = omip samples
        #   sample[e][2]  = rnlp samles
        #
        # Each element samples[i][j] contains a list of value
        # where the kth element corresponds to the number of
        # schedulable taskset in the kth mcsl interval
        samples[e] = [[] for _ in xrange(0, 3)]
        mcsl_index = 0

        for i in xrange(mcsl_range[0], mcsl_range[1], mcsl_step):

            gipp_mcsl_sample = 0
            omip_mcsl_sample = 0
            rnlp_mcsl_sample = 0

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

                    # Define functions for bounds evaluation
                    gipp_eval_func = apply_gipp_bounds
                    omip_eval_func = apply_omip_bounds

                    def rnlp_eval_func(rnlp_ts, rnlp_cpun, rnlp_cs):
                        return apply_gipp_bounds(rnlp_ts, rnlp_cpun, rnlp_cs, True)

                    # Compute the number of cluster
                    n_cluster = e["cpu_number"] / cluster_size

                    # Perform tests
                    if(perform_schedulability_test(
                            t_gipp,
                            n_cluster,
                            cluster_size,
                            e["cpu_number"],
                            gipp_eval_func)):
                        gipp_mcsl_sample = gipp_mcsl_sample + 1

                    if(perform_schedulability_test(
                            t_omip,
                            n_cluster,
                            cluster_size,
                            e["cpu_number"],
                            omip_eval_func)):
                        omip_mcsl_sample = omip_mcsl_sample + 1

                    if(perform_schedulability_test(
                            t_rnlp,
                            n_cluster,
                            cluster_size,
                            e["cpu_number"],
                            rnlp_eval_func)):
                        rnlp_mcsl_sample = rnlp_mcsl_sample + 1

            # Add GIPP samples
            samples[e][0][mcsl_index] = gipp_mcsl_sample

            # Add OMIP samples
            samples[e][1][mcsl_index] = omip_mcsl_sample

            # Add GIPP samples
            samples[e][1][mcsl_index] = rnlp_mcsl_sample

            # Increment mcsl_index
            mcsl_index = mcsl_index + 1

