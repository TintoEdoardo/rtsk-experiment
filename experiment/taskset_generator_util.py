"""
    Configuration function for critical sections and resource groups.
"""

import random


"""
    COMPUTE_GROUPS_NUMBER
        Input: 
            num_resources - number of resources in the system
            group_conf - configuration identified by g_size and g_type
        
        Output:
            number of resource groups in the system
"""
def compute_groups_number(
        num_resources,
        group_conf):
    num_groups         = num_resources / group_conf["resources"]
    return num_groups


"""
    GENERATE_GROUPS
    Input: 
        num_tasks - number of tasks in the system
        num_resources - number of resources in the system
        group_conf - configuration identified by g_size and g_type
        
    Output: 
        array of groups index, where the nth element is the index of 
            the group for the nth task
"""
def generate_groups(
        num_tasks,
        num_resources,
        group_conf):

    # To avoid relocating tasks, we initialize each groups_association
    # value to -1, in order to verify if the task has already been assigned
    groups_association = [-1 for _ in range(0, num_tasks, 1)]
    num_groups         = num_resources / group_conf["resources"]
    minimal_requests   = len(group_conf["minimal_requests"])

    # Then we assign to each groups a minimal set of tasks
    for i in xrange(0, num_groups, 1):

        assigned_tasks = 0

        for t in xrange(0, num_tasks, 1):

            if (groups_association[t] == -1) & (assigned_tasks < minimal_requests):
                groups_association[t] = i
                assigned_tasks = assigned_tasks + 1

    # Finally, we assign random group to remaining tasks
    for t in xrange(0, num_tasks, 1):

        if groups_association[t] == -1:
            groups_association[t] = random.randint(0, num_groups - 1)

    return groups_association


"""
    ASSIGN_CS
    Input: 
        group_conf - configuration identified by g_size and g_type
        groups_association - array of groups index, where the nth  
            element is the index of the group for the nth task
        num_tasks - number of nls tasks in the system
        max_requests - max number of issued requests
        num_resources - number of shared resources
        
    Output:
        array of critical sections, where the nth element contains
            the list of the critical sections of the nth task
    
"""
def assign_cs(
        group_conf,
        groups_association,
        num_tasks,
        max_requests,
        num_resources):

    num_groups        = num_resources / group_conf["resources"]
    critical_sections = [[] for _ in range(0, num_tasks)]

    requests = sum(max_requests)
    if requests < len(group_conf["minimal_requests"]) * num_groups:
        print "impossible to generate"

    # Assign critical sections and populate
    # the array critical_sections
    for g in xrange(0, num_groups):

        current_minimal_req_index = 0
        last_minimal_req_index = len(group_conf["minimal_requests"])

        for t in xrange(0, num_tasks):

            # Assign critical sections if task with index t
            # is assigned to group of index g
            if groups_association[t] == g:

                for n in xrange(0, max_requests[t]):

                    cs = None

                    # Minimal requests needed
                    if current_minimal_req_index < last_minimal_req_index:
                        cs = group_conf["minimal_requests"][current_minimal_req_index]

                        current_minimal_req_index \
                            = current_minimal_req_index + 1

                    # A random request is chosen then
                    else:
                        nested_requests = random.randint(0, 1)
                        if nested_requests == 0:
                            nn_request_index \
                                = random.randint(0, len(group_conf["nn_requests"]) - 1)

                            cs = group_conf["nn_requests"][nn_request_index]
                        else:
                            request_index \
                                = random.randint(0, len(group_conf["n_requests"]) - 1)

                            cs = group_conf["n_requests"][request_index]

                    critical_sections[t].append(cs)

    return critical_sections


"""
    ASSIGN_CS_ASYMMETRIC
    Input: 
        group_conf - configuration identified by g_size and g_type
        groups_association - array of groups index, where the nth  
            element is the index of the group for the nth task
        num_tasks - number of nls tasks in the system
        max_requests - max number of issued requests
        num_resources - number of shared resources
        
    Output:
        array of critical sections, where the nth element contains
            the list of the critical sections of the nth task and
            all tasks request only top level requests, except the 
            first task of each group which generate the groups
    
"""
def assign_cs_asymmetric(
        group_conf,
        groups_association,
        num_tasks,
        max_requests,
        num_resources):

    num_groups        = num_resources / group_conf["resources"]
    critical_sections = [[] for _ in range(0, num_tasks)]

    # Assign critical sections and populate
    # the array critical_sections
    for g in xrange(0, num_groups):

        current_minimal_req_index = 0
        last_minimal_req_index = len(group_conf["minimal_requests"])

        for t in xrange(0, num_tasks):

            # Assign critical sections if task with index t
            # is assigned to group of index g
            if groups_association[t] == g:

                for n in xrange(0, max_requests[t]):

                    cs = None

                    # Minimal requests needed
                    if current_minimal_req_index < last_minimal_req_index:
                        cs = group_conf["minimal_requests"][current_minimal_req_index]

                        current_minimal_req_index \
                            = current_minimal_req_index + 1

                    # A random request is chosen then
                    else:
                        nn_request_index \
                            = random.randint(0, len(group_conf["nn_requests"]) - 1)

                        cs = group_conf["nn_requests"][nn_request_index]

                    critical_sections[t].append(cs)

    return critical_sections
