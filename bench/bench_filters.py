from tqdm import tqdm
from sacred import Experiment
import os
import subprocess

ex = Experiment()
all_filters = ["adaptive", "nonAdaptive", "dSkiAdaptive", "sampleDetect"]
# filters = ['repeatDetect']


@ex.config
def test_config():
    quotient_bits = 22
    remainder_bits = 8
    num_queries = 20000
    microbench = False
    num_rounds = 100
    storage_engine = "splinterDB"
    reverse_map_engine = "splinterDB"
    query_workload = "false-positive"
    adv_freq = 5
    max_adv_repeat = 0
    break_even = 24
    collect_db_stats = False
    hash_again_for_zipfian = True
    capture_extra_stats = False
    storage_cache_size_mb = 64
    zipf_constant = 1.2
    workload_only = False
    is_phased_test = False
    num_phases = 2
    start_with_adversarial_phase = False
    is_insert_test = False
    sort_and_insert_keys=True


@ex.capture
def run_filter_bench(
    quotient_bits,
    remainder_bits,
    num_queries,
    num_rounds,
    microbench,
    storage_engine,
    reverse_map_engine,
    query_workload,
    adv_freq,
    max_adv_repeat,
    break_even,
    collect_db_stats,
    hash_again_for_zipfian,
    capture_extra_stats,
    storage_cache_size_mb,
    _seed,
    zipf_constant,
    workload_only,
    is_phased_test,
    num_phases,
    start_with_adversarial_phase,
    sort_and_insert_keys,
    is_insert_test
):
    filters = all_filters
    if is_insert_test:
        filters=['adaptive', 'nonAdaptive']

    print(filters)

    extra_build_flags = ""
    if capture_extra_stats:
        extra_build_flags = " EXTRA_STATS=1"

    ### Build Workload Generator Tool
    cmd = "make clean workload_gen "
    log_file = "workload-build.log"
    with open(log_file, "w") as f:
        with tqdm(
            desc="Building Workload-Generator\n" + cmd + extra_build_flags,
            bar_format="{desc}\nElapsed:{elapsed}",
        ) as pbar:
            ret = subprocess.run(
                cmd + extra_build_flags, shell=True, stdout=f, stderr=subprocess.STDOUT
            )
    ex.add_artifact(log_file)
    if ret.returncode != 0:
        print("Workload-Generator build failed, check %s" % log_file)
        return

    ### Generate Workload
    log_file = "workload-gen.log"
    cmd = "./workload_gen"
    argDict = {
        "-q": quotient_bits,
        "-r": remainder_bits,
        "--numQueries": num_queries,
        "--numRounds": num_rounds,
        "--queryWorkload": query_workload,
        "--seed": _seed,
        "--zipfianConstant": zipf_constant,
    }
    for arg_name in argDict:
        cmd = cmd + (" %s %s" % (arg_name, argDict[arg_name]))
    if hash_again_for_zipfian:
        cmd = cmd + " --hashAgainForZipfian"

    with open(log_file, "w") as f:
        with tqdm(
            desc="\nGenerating Workload\n" + cmd, bar_format="{desc}\nElapsed:{elapsed}"
        ) as pbar:
            ret = subprocess.run(cmd, shell=True, stderr=subprocess.STDOUT)
    if ret.returncode != 0:
        print("Generating workload failed!")
        exit()
    #ex.add_artifact("queryStats")
    #ex.add_artifact("rankFreq")

    if workload_only:
        return

    ### Build benchmark
    for filter in filters:
        build_cmd = ""
        log_file = filter + "_build.log"
        if filter == "nonAdaptive":
            build_cmd = (
                "make clean && make bench_variants" + extra_build_flags
            )
            #build_cmd = (
            #    "make clean && make bench_variants USE_CQF=1" + extra_build_flags
            #)
        elif filter == "blockCount":
            build_cmd = (
                "make clean && make bench_variants SEVEN_BIT_OFFSET=1"
                + extra_build_flags
            )
        else:
            build_cmd = "make clean && make bench_variants" + extra_build_flags

        with open(log_file, "w") as f:
            with tqdm(
                desc="\nBuilding benchmark for " + filter + "\n" + build_cmd,
                bar_format="{desc}\nElapsed:{elapsed}",
            ) as pbar:
                ret = subprocess.run(
                    build_cmd, shell=True, stdout=f, stderr=subprocess.STDOUT
                )
        if ret.returncode != 0:
            print("Building benchmark failed!")
            exit()

        ### Running benchmark
        argDict = {
            "-q": quotient_bits,
            "-r": remainder_bits,
            "--numQueries": num_queries,
            "--numRounds": num_rounds,
            "--queryWorkload": query_workload,
            "--storageEngine": storage_engine,
            "--reverseMapEngine": reverse_map_engine,
            "--storageCacheSizeMB": str(storage_cache_size_mb),
            "--advFreq": adv_freq,
            "--breakEven": break_even,
            "--numPhases": num_phases,
        }
        cmd = "./bench_variants --filter %s " % filter
        for arg_name in argDict:
            cmd = cmd + (" %s %s" % (arg_name, argDict[arg_name]))
        if microbench:
            cmd = cmd + " --microBench=True"
        if collect_db_stats:
            cmd = cmd + " --dbStats"
        if is_phased_test:
            cmd = cmd + " --phasedTest"
        if start_with_adversarial_phase:
            cmd = cmd + " --startWithAdversarialPhase"
        if sort_and_insert_keys:
            cmd = cmd + " --sortAndInsertFingerprints"

        with tqdm(
            desc="\nRunning benchmark for " + filter + "\n" + cmd,
            bar_format="{desc}\nElapsed:{elapsed}",
        ) as pbar:
            ret = subprocess.run(cmd, shell=True, stderr=subprocess.STDOUT)
        if ret.returncode != 0:
            print("Benchmark failed!")
            exit()

        ex.add_artifact("%s.csv" % filter)
        ex.add_artifact("%s_summary.csv" % filter)
        if capture_extra_stats:
            ex.add_artifact("%s_latency.csv" % filter)
            ex.add_artifact("%s_fp_stats.csv" % filter)

        ### Copying DB Stats
        if collect_db_stats and not microbench:
            with tqdm(
                desc="\nCopying DB Stats for " + filter,
                bar_format="{desc}\nElapsed:{elapsed}",
            ) as pbar:
                cmd = "cp database_wiredTiger/WiredTigerStat* %s_db_stats.json" % filter
                ret = subprocess.run(cmd, shell=True, stderr=subprocess.STDOUT)
            ex.add_artifact("%s_db_stats.json" % filter)

            if reverse_map_engine == "wiredTiger" and filter != "nonAdaptive":
                with tqdm(
                    desc="\nCopying DB Stats for " + filter,
                    bar_format="{desc}\nElapsed:{elapsed}",
                ) as pbar:
                    cmd = (
                        "cp reverseMap_wiredTiger/WiredTigerStat* %s_rm_stats.json"
                        % filter
                    )
                    ret = subprocess.run(cmd, shell=True, stderr=subprocess.STDOUT)
                ex.add_artifact("%s_rm_stats.json" % filter)

    ### Parsing DB Stats
    if (not microbench) and (not is_insert_test):
        with tqdm(
            desc="Parsing DB Stats" + filter, bar_format="{desc}\nElapsed:{elapsed}"
        ) as pbar:
            cmd = "python3 ./bench/parse_db_stats.py ."
            ret = subprocess.run(cmd, shell=True, stderr=subprocess.STDOUT)
        ex.add_artifact("db_stats.csv")
        ex.add_artifact("rm_stats.csv")


@ex.automain
def run_experiment():
    run_filter_bench()
