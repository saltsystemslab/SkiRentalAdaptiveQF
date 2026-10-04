import glob
import os
import shutil
import subprocess

from sacred import Experiment
from tqdm import tqdm

# TODO(Chesetti): Lot's of hardcoded variables. Move them to strings.

ex = Experiment()

work_dir = './sponge'
exe_dir = './build' # KEEP in SYNC with the Makefile. All exe are run with cwd=work_dir
build_log_dir = './sponge'

all_filters = ["adaptive", "nonAdaptive", "skiQF", "hybridSkiQF"]


def storage_cache_for_filter(filter_name, storage_cache_size_mb, reverse_map_cache_size_mb):
    """Give every filter the same total external-memory cache budget."""
    if filter_name == "nonAdaptive":
        return storage_cache_size_mb + reverse_map_cache_size_mb
    return storage_cache_size_mb


def collect_wiredtiger_stats(source_pattern, destination):
    """Combine all WiredTiger statistics-log segments into one artifact."""
    source_paths = sorted(glob.glob(source_pattern))
    if not source_paths:
        raise FileNotFoundError(
            f"No WiredTiger statistics files matched {source_pattern}"
        )

    with open(destination, "wb") as destination_file:
        for source_path in source_paths:
            with open(source_path, "rb") as source_file:
                shutil.copyfileobj(source_file, destination_file)


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
    # SplinterDB requires a larger cache. Paper runs using WiredTiger override
    # this to 4 MB; the Table 1 engine comparison keeps both engines at 64 MB.
    reverse_map_cache_size_mb = 64
    zipf_constant = 1.2
    workload_only = False
    is_phased_test = False
    num_phases = 2
    start_with_adversarial_phase = False
    is_insert_test = False
    skip_db_parse = False
    sort_and_insert_keys=True
    sort_and_insert_fingerprints=True
    storage_sleep_us = 0
    reverse_sleep_us = 0


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
    reverse_map_cache_size_mb,
    _seed,
    zipf_constant,
    workload_only,
    is_phased_test,
    num_phases,
    start_with_adversarial_phase,
    sort_and_insert_keys,
    sort_and_insert_fingerprints,
    is_insert_test,
    skip_db_parse,
    storage_sleep_us,
    reverse_sleep_us,
):
    filters = all_filters
    if is_insert_test:
        filters=['adaptive', 'nonAdaptive']

    print(filters)

    extra_build_flags = ""
    if capture_extra_stats:
        extra_build_flags = " EXTRA_STATS=1"

    ### Make runs in current directory, tool invocations run in cwd=sponge
    ### Assumes binaries are located in '../'

    ### Build Workload Generator Tool
    cmd = "make clean workload_gen"
    log_file_path = os.path.join(build_log_dir, "workload-build.log")
    with open(log_file_path, "w") as log_file:
        with tqdm(
            desc="Building Workload-Generator\n" + cmd + extra_build_flags,
            bar_format="{desc}\nElapsed:{elapsed}",
        ) as pbar:
            ret = subprocess.run(
                cmd + extra_build_flags, shell=True, stdout=log_file, stderr=subprocess.STDOUT
            )
    ex.add_artifact(log_file_path)
    if ret.returncode != 0:
        print("Workload-Generator build failed, check %s" % log_file_path)
        return

    ### Generate Workload
    cmd = os.path.join(exe_dir, "workload_gen")
    log_file_path = os.path.join(work_dir, "workload-gen.log")
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

    with open(log_file_path, "w") as log_file:
        with tqdm(
            desc="\nGenerating Workload\n" + cmd, bar_format="{desc}\nElapsed:{elapsed}"
        ) as pbar:
            ret = subprocess.run(cmd, cwd=work_dir, shell=True, stdout=log_file, stderr=subprocess.STDOUT)
    ex.add_artifact(log_file_path)
    if ret.returncode != 0:
        print("Generating workload failed! Check: " + log_file_path)
        exit()
    #ex.add_artifact("queryStats")
    #ex.add_artifact("rankFreq")

    if workload_only:
        return

    ### Build benchmark
    for filter in filters:
        build_cmd = ""
        log_file_path = os.path.join(build_log_dir, filter + "_build.log")
        if filter == "nonAdaptive":
            build_cmd = (
                "make clean && make bench_variants" + extra_build_flags # ADD USE_CQF=1 if you want to use original CQF code.
            )
        else:
            build_cmd = "make clean && make bench_variants" + extra_build_flags

        with open(log_file_path, "w") as log_file:
            with tqdm(
                desc="\nBuilding benchmark for " + filter + "\n" + build_cmd,
                bar_format="{desc}\nElapsed:{elapsed}",
            ) as pbar:
                ret = subprocess.run(
                    build_cmd, shell=True, stdout=log_file, stderr=subprocess.STDOUT
                )
        ex.add_artifact(log_file_path)
        if ret.returncode != 0:
            print("Building benchmark failed! Check " + log_file_path)
            exit()

        ### Running benchmark
        filter_storage_cache_size_mb = storage_cache_for_filter(
            filter, storage_cache_size_mb, reverse_map_cache_size_mb
        )
        argDict = {
            "-q": quotient_bits,
            "-r": remainder_bits,
            "--numQueries": num_queries,
            "--numRounds": num_rounds,
            "--queryWorkload": query_workload,
            "--storageEngine": storage_engine,
            "--reverseMapEngine": reverse_map_engine,
            "--storageCacheSizeMB": str(filter_storage_cache_size_mb),
            "--reverseMapCacheSizeMB": str(reverse_map_cache_size_mb),
            "--storageSleepUs": storage_sleep_us,
            "--reverseSleepUs": reverse_sleep_us,
            "--advFreq": adv_freq,
            "--breakEven": break_even,
            "--numPhases": num_phases,
        }
        cmd = "%s/bench_variants --filter %s " % (exe_dir, filter)
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

        if sort_and_insert_fingerprints:
            cmd = cmd + " --sortAndInsertFingerprints"
        else:
            cmd = cmd + " --sortAndInsertFingerprints=False"

        if sort_and_insert_keys:
            cmd = cmd + " --sortAndInsertKeys"
        else:
            cmd = cmd + " --sortAndInsertKeys=False"

        with tqdm(
            desc="\nRunning benchmark for " + filter + "\n" + cmd,
            bar_format="{desc}\nElapsed:{elapsed}",
        ) as pbar:
            ret = subprocess.run(cmd, cwd=work_dir, shell=True, stderr=subprocess.STDOUT)
        if ret.returncode != 0:
            print("Benchmark failed!")
            exit()

        csv_path = os.path.join(work_dir, '%s.csv' % filter)
        summary_path = os.path.join(work_dir, '%s_summary.csv' % filter)
        latency_path = os.path.join(work_dir, '%s_latency.csv' % filter)
        fp_stats_path = os.path.join(work_dir, '%s_fp_stats.csv' % filter)

        ex.add_artifact(csv_path)
        ex.add_artifact(summary_path)
        if capture_extra_stats:
            ex.add_artifact(latency_path)
            ex.add_artifact(fp_stats_path)

        ### Copying DB Stats
        if collect_db_stats and not microbench and storage_engine == "wiredTiger":
            with tqdm(
                desc="\nCopying DB Stats for " + filter,
                bar_format="{desc}\nElapsed:{elapsed}",
            ) as pbar:
                db_stats_path = os.path.join(work_dir, "%s_db_stats.json" % filter)
                collect_wiredtiger_stats(
                    os.path.join(
                        work_dir,
                        "database_wiredTiger",
                        "WiredTigerStat*",
                    ),
                    db_stats_path,
                )
            ex.add_artifact(db_stats_path)

        if (
            collect_db_stats
            and not microbench
            and reverse_map_engine == "wiredTiger"
            and filter != "nonAdaptive"
        ):
            with tqdm(
                desc="\nCopying reverse-map stats for " + filter,
                bar_format="{desc}\nElapsed:{elapsed}",
            ) as pbar:
                rm_stats_path = os.path.join(work_dir, "%s_rm_stats.json" % filter)
                collect_wiredtiger_stats(
                    os.path.join(
                        work_dir,
                        "reverseMap_wiredTiger",
                        "WiredTigerStat*",
                    ),
                    rm_stats_path,
                )
            ex.add_artifact(rm_stats_path)

    ### Parsing DB Stats
    should_parse_db_stats = (
        not skip_db_parse
        and collect_db_stats
        and not microbench
        and not is_insert_test
        and (storage_engine == "wiredTiger" or reverse_map_engine == "wiredTiger")
    )
    if should_parse_db_stats:
        with tqdm(
            desc="Parsing DB Stats", bar_format="{desc}\nElapsed:{elapsed}"
        ) as pbar:
            subprocess.run(
                ["python3", "./bench/parse_db_stats.py", work_dir],
                check=True,
                stderr=subprocess.STDOUT,
            )
        if storage_engine == "wiredTiger":
            ex.add_artifact(os.path.join(work_dir, "db_stats.csv"))
        if reverse_map_engine == "wiredTiger":
            ex.add_artifact(os.path.join(work_dir, "rm_stats.csv"))


@ex.automain
def run_experiment():
    run_filter_bench()
