"""
    Configuration for critical sections and resources management
"""
import random


"""
    GENERATE_GROUPS
    Input: 
        num_tasks - number of nls tasks in the system
        group_conf - configuration identified by g_size and g_type
        
    Output: 
        array of groups index, where the nth element is the index of 
            the group for the nth task
"""
def generate_groups(
        num_tasks,
        group_conf):
    groups_association = []
    num_groups = 12 / group_conf["resources"]
    minimal_requests = len(group_conf["minimal_requests"])

    # Check that the group is possible to create
    assert (num_tasks <= minimal_requests * num_groups)

    # To avoid relocating tasks, we initialize each groups_association
    # value to -1, in order to verify if the task has already been assigned
    for i in xrange(0, num_tasks, 1):
        groups_association[1] = -1

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
            groups_association[t] = random.randint(0, num_groups)

    return groups_association


"""
    ASSIGN_CS
    Input: 
        group_conf - configuration identified by g_size and g_type
        groups_association - array of groups index, where the nth  
            element is the index of the group for the nth task
        num_tasks - number of nls tasks in the system
        max_requests - max number of issued requests
        
    Output:
        array of critical sections, where the nth element contains
            the list of the critical sections of the nth task
    
"""
def assign_cs(
        group_conf,
        groups_association,
        num_tasks,
        max_requests):
    num_groups = 12 / group_conf["resources"]
    critical_sections = []

    # Assign critical sections an populate
    # the array critical_sections
    for g in xrange(0, num_groups):

        current_minimal_req_index = 0
        last_minimal_req_index = len(group_conf["minimal_requests"]) - 1

        for t in xrange(0, num_tasks):

            # Assign critical sections if task with index t
            # is assigned to group of index g
            if groups_association[t] == g:

                for n in xrange(0, max_requests):

                    # Minimal requests needed
                    if current_minimal_req_index < last_minimal_req_index:
                        critical_sections[t][n] \
                            = group_conf["minimal_requests"][current_minimal_req_index]

                        current_minimal_req_index \
                            = current_minimal_req_index + 1

                    # A random request is chosen then
                    else:
                        nested_requests = random.randint(0, 1)
                        if nested_requests == 0:
                            nn_request_index \
                                = random.randint(0, len(group_conf["nn_requests"]) - 1)

                            critical_sections[t][n] \
                                = group_conf["nn_requests"][nn_request_index]
                        else:
                            request_index \
                                = random.randint(0, len(group_conf["n_requests"]) - 1)

                            critical_sections[t][n] \
                                = group_conf["n_requests"][request_index]

    return critical_sections
