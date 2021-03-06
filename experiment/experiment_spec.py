"""
    Specification for some of the experiments in
    [Brandenburg 2020]
"""

experiment_1 = dict()
experiment_1["cpu_number"]      = 8
experiment_1["utilization"]     = 4.80
experiment_1["tasks_number"]    = 24
experiment_1["ls_tasks_number"] = 4
experiment_1["group_conf"]      = (3, "wide")
experiment_1["max_request"]     = 1
experiment_1["resources_nls"]   = 12

experiment_2 = dict()
experiment_2["cpu_number"]      = 4
experiment_2["utilization"]     = 2.40
experiment_2["tasks_number"]    = 12
experiment_2["ls_tasks_number"] = 0
experiment_2["group_conf"]      = (1, "none")
experiment_2["max_request"]     = 3
experiment_2["resources_nls"]   = 12

experiment_3 = dict()
experiment_3["cpu_number"]      = 8
experiment_3["utilization"]     = 4.80
experiment_3["tasks_number"]    = 24
experiment_3["ls_tasks_number"] = 0
experiment_3["group_conf"]      = (4, "deep")
experiment_3["max_request"]     = 3
experiment_3["resources_nls"]   = 12

experiment_4 = dict()
experiment_4["cpu_number"]      = 4
experiment_4["utilization"]     = 2.80
experiment_4["tasks_number"]    = 4
experiment_4["ls_tasks_number"] = 0
experiment_4["group_conf"]      = (4, "wide")
experiment_4["max_request"]     = 10
experiment_4["resources_nls"]   = 4

experiment_5 = dict()
experiment_5["cpu_number"]      = 4
experiment_5["utilization"]     = 1.60
experiment_5["tasks_number"]    = 8
experiment_5["ls_tasks_number"] = 2
experiment_5["group_conf"]      = (4, "wide_2")
experiment_5["max_request"]     = 10
experiment_5["resources_nls"]   = 8


# Experiment array
experiments = [
    experiment_1,
    experiment_2,
    experiment_3,
    experiment_4,
    experiment_5]

# Cluster size
cluster_size = 1

