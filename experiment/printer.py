"""

    Print the result of the experiments in CSV file.

    Each experiment produce three files, one for protocol
    under test, inside the corresponding folder.

"""

import pandas


path_to_folder_exp_1 = "results/experiment_1/"
path_to_folder_exp_2 = "results/experiment_2/"
path_to_folder_exp_3 = "results/experiment_3/"
path_to_folder_exp_4 = "results/experiment_4/"

file_rnlp = "rnlp_samples.csv"
file_omip = "omip_samples.csv"
file_gipp = "gipp_samples.csv"

paths = [
    path_to_folder_exp_1,
    path_to_folder_exp_2,
    path_to_folder_exp_3,
    path_to_folder_exp_4]

files = [
    file_rnlp,
    file_omip,
    file_gipp]

header = [
    "mcsl",
    "samples"]

"""
    PRINTER
        Input:
            experiment_num - index of the experiment
            mcsl_values - list of mcsl values
            samples - list of three elements, each one is a list of the number of 
                schedulable tasksets computed respectively with RNLP, OMIP and GIPP
        
        Output:
            experiment_x/rnlp_samples.csv - for each mcsl value, 
                the number of schedulable tasksets over the whole 
                tasksets generated, using RNLP. 
            experiment_x/omip_samples.csv - same as before, using OMIP. 
            experiment_x/gipp_samples.csv - same as before, using GIPP.
"""
def printer(
        experiment_num,
        mcsl_values,
        samples):

    for i in range(0, 3):

        path = paths[experiment_num] + files[i]

        output_csv = pandas.DataFrame({header[0]: mcsl_values, header[1]: samples[i]})
        output_csv.to_csv(path, index=False)

