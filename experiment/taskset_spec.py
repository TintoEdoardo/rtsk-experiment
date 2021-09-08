"""
    Specification for tasksets.
    They are used to replicate the four experimental graph in
    [Brandenburg 2020].
"""

"""
    TASKSET_CONST
        period_ls           - values of period for ls task
        cs_len_ls_range     - critical section length range for ls tasks
        resources_ls        - number of resources shared by ls tasks
        max_issued_req_ls   - max number of requests issued by ls tasks
        groups_ls           - number of resource groups for ls tasks
        
        period_nls          - values of period for nls task
        cs_length_nls_range - critical section length range for nls tasks
        mcsl_step           - increment on critical section length of nls tasks
        resources_nls       - number of resources shared by nls tasks
        max_issued_req_nls  - max number of requests issued by nls tasks
"""
taskset_const = dict()

# Latency sensitive tasks
taskset_const["period_ls"]           = [1, 2, 4, 5, 8]
taskset_const["cs_len_ls_range"]     = (1, 15)
taskset_const["resources_ls"]        = 3
taskset_const["max_issued_req_ls"]   = 2
taskset_const["groups_ls"]           = 1

# Non-Latency sensitive tasks
taskset_const["period_nls"]          = [10, 20, 25, 40, 50, 100, 125, 200, 250, 500, 1000]
taskset_const["cs_length_nls_range"] = (5, 1000)
taskset_const["mcsl_step"]           = 5
taskset_const["resources_nls"]       = 12
taskset_const["max_issued_req_nls"]  = 3

