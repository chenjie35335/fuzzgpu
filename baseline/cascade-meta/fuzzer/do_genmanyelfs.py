# Copyright 2023 Flavien Solt, ETH Zurich.
# Licensed under the General Public License, Version 3.0, see LICENSE for details.
# SPDX-License-Identifier: GPL-3.0-only

# This script generates many Cascade ELFs.

from analyzeelfs.genmanyelfs import gen_many_elfs
from params.runparams import PATH_TO_TMP
import params.fuzzparams as fuzzparams

import os
import sys
import argparse

if __name__ == '__main__':
    if "CASCADE_ENV_SOURCED" not in os.environ:
        raise Exception("The Cascade environment must be sourced prior to running the Python recipes.")

    parser = argparse.ArgumentParser(description='Generate many Cascade ELFs.')
    parser.add_argument('num_elfs', type=int, nargs='?', default=500, help='Number of ELFs to generate')
    parser.add_argument('target_dir', type=str, nargs='?', default=os.path.join(PATH_TO_TMP, 'manyelfs'), help='Target directory')
    parser.add_argument('--arch', type=str, choices=['vortex32', 'vortex64', 'ventus'], default='vortex32', help='Architecture to target')
    parser.add_argument('--num-warps-per-core', type=int, help='Number of warps per core')
    parser.add_argument('--num-threads-per-warp', type=int, help='Number of threads per warp')
    parser.add_argument('--vortex-max-num-barrier', type=int, help='Vortex max num barrier')
    parser.add_argument('--vortex-max-barrier-count', type=int, help='Vortex max barrier count')
    parser.add_argument('--vortex-cores-per-clusters', type=int, help='Vortex cores per cluster')
    parser.add_argument('--emulator-path', type=str, help='Path to emulator executable')
    parser.add_argument('--num-instrs', type=int, help='Max number of instructions', default=None)
    parser.add_argument('--seed-id', type=int, default=0, help='Seed ID to ensure non-overlapping randseeds (randseed = seed_id * num_elfs + i)')
    parser.add_argument('--bug-filter-json', type=str, default=None, help='fuzzgpu-compatible bug filter JSON')

    args = parser.parse_args()

    if args.num_warps_per_core is not None:
        fuzzparams.NUM_WARPS_PER_CORE = args.num_warps_per_core
    if args.num_threads_per_warp is not None:
        fuzzparams.NUM_THREADS_PER_WARP = args.num_threads_per_warp
    if args.vortex_max_num_barrier is not None:
        fuzzparams.VORTEX_MAX_NUM_BARRIER = args.vortex_max_num_barrier
    if args.vortex_max_barrier_count is not None:
        fuzzparams.VORTEX_MAX_BARRIER_COUNT = args.vortex_max_barrier_count
    if args.vortex_cores_per_clusters is not None:
        fuzzparams.NUM_CORES_PER_CLUSTERS = args.vortex_cores_per_clusters

    num_cores = int(os.getenv('CASCADE_JOBS', 160))

    gen_many_elfs(args.arch, num_cores, args.num_elfs, args.target_dir, emulator_path=args.emulator_path, max_num_instructions=args.num_instrs, seed_id=args.seed_id, bug_filter_json=args.bug_filter_json)
else:
    raise Exception("This module must be at the toplevel.")
