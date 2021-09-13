
"""
    Function to start the experiment for comparison
    between GIPP, OMIP, RNLP.
"""
import time

import printer as prn
from experiment_spec import experiments, cluster_size
from experiment_util import perform_schedulability_test
from taskset_generator import generate_taskset
from taskset_spec import taskset_const
from schedcat.locking.bounds import apply_omip_bounds, apply_gipp_bounds, assign_edf_preemption_levels


def run_experiment():

    # For each mcsl point, 20 samples are computed,
    # therefore each interval of 5 values contains 100 samples
    n_sample        = 40
    mcsl_range      = taskset_const["cs_length_nls_range"]
    mcsl_big_step   = taskset_const["mcsl_step"]
    mcsl_small_step = 1

    # Compute the number of tasksets generated
    # per mcsl value
    num_taskset_per_mcsl_value = n_sample * (mcsl_big_step / mcsl_small_step)

    print(" - execution started - ")
    print("taskset computed per mcsl values", num_taskset_per_mcsl_value)
    print("-------")

    # Iterates over experiment configurations
    # for e_index in range(0, len(experiments)):
    for e_index in range(2, 3):

        # Compute asymmetric flag
        is_asymmetric = False
        if e_index == 3:
            is_asymmetric = True

        # Computed samples
        gipp_samples = []
        omip_samples = []
        rnlp_samples = []

        # Track mcsl values
        mcsl_values  = []

        # Initialize indexes
        e          = experiments[e_index]
        mcsl_index = 0

        start = time.time()
        print("start experiment ", e_index)

        for i in xrange(mcsl_range[0], mcsl_range[1] + mcsl_big_step, mcsl_big_step):

            gipp_mcsl_sample = 0
            omip_mcsl_sample = 0
            rnlp_mcsl_sample = 0

            mcsl_values.append(i)

            for v in xrange(i, i + mcsl_big_step, mcsl_small_step):

                for s in xrange(0, n_sample):

                    # Compute utilization for ls tasks
                    # utilization_ls = e["utilization"] * (float(e["ls_tasks_number"])/e["tasks_number"])
                    utilization_ls = min(e["utilization"] * 0.5, e["ls_tasks_number"] * 0.5)

                    (t_gipp, t_omip, t_rnlp) = generate_taskset(
                        e["tasks_number"],
                        e["ls_tasks_number"],
                        e["utilization"],
                        utilization_ls,
                        e["cpu_number"],
                        e["group_conf"][0],
                        e["group_conf"][1],
                        (1, v),
                        taskset_const["cs_len_ls_range"],
                        e["max_request"],
                        taskset_const["max_issued_req_ls"],
                        taskset_const["period_ls"],
                        taskset_const["period_nls"],
                        taskset_const["resources_ls"],
                        e["resources_nls"],
                        is_asymmetric)

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
            gipp_samples.append(float(gipp_mcsl_sample) / num_taskset_per_mcsl_value)

            # Add OMIP samples
            omip_samples.append(float(omip_mcsl_sample) / num_taskset_per_mcsl_value)

            # Add RNLP samples
            rnlp_samples.append(float(rnlp_mcsl_sample) / num_taskset_per_mcsl_value)

            # Increment mcsl_index
            mcsl_index = mcsl_index + 1

        prn.printer(e_index, mcsl_values, [rnlp_samples, omip_samples, gipp_samples])
        end = time.time()

        time_elapsed = end - start
        print("end experiment ", e_index, " in ", time_elapsed)
        print("-------")

    """
    Utilization experiments
    """

    # Compute utilization array
    utilizations = [0.1, 0.3, 0.5]
    gipp_samples_for_u = [[] for _ in utilizations]
    mcsl_values_for_u  = []

    for u_index in range(0, 3):

        # Compute asymmetric flag
        is_asymmetric = False

        # Computed samples
        gipp_samples = []

        # Track mcsl values
        mcsl_values  = []

        # Initialize indexes and parameters
        u          = utilizations[u_index]
        e_index    = 4
        e          = experiments[e_index]
        mcsl_index = 0

        start = time.time()
        print("start utilization experiment for u = ", u)

        for i in xrange(mcsl_range[0], mcsl_range[1] + mcsl_big_step, mcsl_big_step):

            gipp_mcsl_sample = 0

            mcsl_values.append(i)

            for v in xrange(i, i + mcsl_big_step, mcsl_small_step):

                for s in xrange(0, n_sample):

                    # Compute utilization for ls tasks
                    utilization_ls = u * e["utilization"]

                    (t_gipp, t_omip, t_rnlp) = generate_taskset(
                        e["tasks_number"],
                        e["ls_tasks_number"],
                        e["utilization"],
                        utilization_ls,
                        e["cpu_number"],
                        e["group_conf"][0],
                        e["group_conf"][1],
                        (1, v),
                        taskset_const["cs_len_ls_range"],
                        e["max_request"],
                        taskset_const["max_issued_req_ls"],
                        taskset_const["period_ls"],
                        taskset_const["period_nls"],
                        taskset_const["resources_ls"],
                        e["resources_nls"],
                        is_asymmetric)

                    # Define functions for bounds evaluation
                    gipp_eval_func = apply_gipp_bounds

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

            # Add GIPP samples
            gipp_samples.append(float(gipp_mcsl_sample) / num_taskset_per_mcsl_value)

            # Increment mcsl_index
            mcsl_index = mcsl_index + 1

        gipp_samples_for_u[u_index] = gipp_samples

        # Save mcsl_values in the last iteration
        if u_index == (len(utilizations) - 1):
            mcsl_values_for_u = mcsl_values

        end = time.time()

        time_elapsed = end - start
        print("end experiment for u = ", u, " in ", time_elapsed)
        print("-------")

    prn.printer_for_u(mcsl_values_for_u, gipp_samples_for_u)


if __name__ == '__main__':
    run_experiment()

