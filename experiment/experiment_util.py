"""
    Utility functions for the experiments.
"""
from schedcat.locking.bounds import assign_edf_preemption_levels
from schedcat.mapping.binpack import worst_fit, report_failure, DidNotFit
from schedcat.model.tasks import TaskSystem
from schedcat.sched.edf import is_schedulable

"""
    PERFORM_SCHEDULABILITY_TEST
        Input:
            taskset - taskset to test
            n_cluster - number of cluster in the system
            cluster_size - size of the clusters in the system
            n_cpu - number of CPU
            apply_sched_bounds - function for applying schedulability bounds
        
        Output: 
            boolean value, True if the taskset is schedulable, False otherwise
"""
def perform_schedulability_test(
        taskset,
        n_cluster,
        cluster_size,
        n_cpu,
        apply_sched_bounds):

    # Assign preemption level
    assign_edf_preemption_levels(taskset)

    # Response time should be added manually,
    # to be schedulable, a task response time should be
    # at most equal to his deadline
    for t in taskset:
        t.response_time = t.deadline

    # Then we should provide a partition
    # for each task, and possibly distribute
    # utilization fairly between processors
    weight_fun = lambda task: task.utilization()

    is_taskset_schedulable = True

    try:
        partitions \
            = worst_fit(taskset, n_cluster, cluster_size, weight_fun, misfit=report_failure, empty_bin=TaskSystem)

        for cluster_index in xrange(0, len(partitions)):

            for t in partitions[cluster_index]:
                t.partition = cluster_index

        # We apply the apply_sched_bounds function
        # to the taskset
        apply_sched_bounds(taskset, n_cpu, cluster_size)

        # Perform schedulability test,
        # being the taskset partitioned, we can perform
        # feasibility test for each cluster
        for p in partitions:

            if not is_schedulable(cluster_size, p, rta_min_step=1000):
                is_taskset_schedulable = False

    except DidNotFit:
        is_taskset_schedulable = False

    return is_taskset_schedulable

