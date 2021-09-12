"""
    Specification for some resource groups.
    They are used to replicate the four experimental graph in
    [Brandenburg 2020].
"""

"""
    GROUP_SPEC
        resources        - number of resources per group
        minimal_requests - minimal number of requests to form a group
        nn_requests      - non-nested requests a task may issue in a group
        n_requests       - nested requests a task may issue in a group
"""

group_1 = dict()
# Top level resources = [0]
# Other resources     = []
group_1["resources"]        = 1
group_1["type"]             = ""
group_1["minimal_requests"] = [(0,)]
group_1["nn_requests"]      = [(0,)]
group_1["n_requests"]       = group_1["nn_requests"]    # Is not acceptable to have an empty set of request

group_3_deep = dict()
# Top level resources = [0]
# Other resources     = [1, 2]
group_3_deep["resources"]        = 3
group_3_deep["type"]             = "deep"
group_3_deep["minimal_requests"] = [(0, 1), (0, 2)]
group_3_deep["nn_requests"]      = [(0,), (1,), (2,)]
group_3_deep["n_requests"]       = group_3_deep["minimal_requests"] + [(1, 2)]

group_3_wide = dict()
# Top level resources = [0, 1]
# Other resources     = [2]
group_3_wide["resources"]        = 3
group_3_wide["type"]             = "wide"
group_3_wide["minimal_requests"] = [(0, 2), (1, 2)]
group_3_wide["nn_requests"]      = [(0,), (1,), (2,)]
group_3_wide["n_requests"]       = group_3_wide["minimal_requests"]

group_4_deep = dict()
# Top level resources = [0]
# Other resources     = [1, 2, 3]
group_4_deep["resources"]        = 4
group_3_deep["type"]             = "deep"
group_4_deep["minimal_requests"] = [(0, 1), (0, 2), (0, 3)]
group_4_deep["nn_requests"]      = [(0,), (1,), (2,), (3,)]
group_4_deep["n_requests"]       = group_4_deep["minimal_requests"]
# group_4_deep["n_requests"]       = group_4_deep["minimal_requests"] + [(1, 2), (1, 3), (2, 3)]

group_4_wide = dict()
# Top level resources = [0, 1, 2]
# Other resources     = [3]
group_4_wide["resources"]        = 4
group_4_wide["type"]             = "wide"
group_4_wide["minimal_requests"] = [(0, 3), (1, 3), (2, 3)]
group_4_wide["nn_requests"]      = [(0,), (1,), (2,)]
group_4_wide["n_requests"]       = group_4_wide["minimal_requests"]
# + [(1, 2), (2, 3)]

group_4_wide_2 = dict()
# Top level resources = [0, 1]
# Other resources     = [2, 3]
group_4_wide["resources"]        = 4
group_4_wide["type"]             = "wide"
group_4_wide["minimal_requests"] = [(0, 2), (0, 3), (1, 3)]
group_4_wide["nn_requests"]      = [(0,), (1,), (2,), (3,)]
group_4_wide["n_requests"]       = group_4_wide["minimal_requests"] + [(1, 2), (2, 3)]