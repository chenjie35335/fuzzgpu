# Copyright 2023 Flavien Solt, ETH Zurich.
# Licensed under the General Public License, Version 3.0, see LICENSE for details.
# SPDX-License-Identifier: GPL-3.0-only

# This module is typically used to generate a lot of Cascade programs

from common.profiledesign import profile_get_medeleg_mask
from common.spike import calibrate_spikespeed, set_emulator_path
from cascade.bugfilter import load_bug_filter_json
from cascade.fuzzfromdescriptor import gen_fuzzerstate_elf_expectedvals, gen_new_test_instance

import multiprocessing as mp
import os
import random
import shutil
from tqdm import tqdm


# @param in_tuple: instance_id: int, memsize: int, design_name: str, check_pc_spike_again: bool, randseed: int, nmax_bbs: int, authorize_privileges: bool, outdir_path: str, emulator_path: str
def __gen_elf_worker(in_tuple):
    instance_id, memsize, design_name, randseed, nmax_bbs, authorize_privileges, check_pc_spike_again, outdir_path, max_num_instructions, bug_filter_json = in_tuple
    load_bug_filter_json(bug_filter_json)
    if max_num_instructions is not None:
        fuzzerstate, elfpath, _, _, _, _ = gen_fuzzerstate_elf_expectedvals(memsize, design_name, randseed, nmax_bbs, authorize_privileges, check_pc_spike_again, max_num_instructions)
    else:
        fuzzerstate, elfpath, _, _, _, _ = gen_fuzzerstate_elf_expectedvals(memsize, design_name, randseed, nmax_bbs, authorize_privileges, check_pc_spike_again)
    # Move the file from elfpath to outdir_path, and name it after the design name and instance id.
    shutil.move(elfpath, os.path.join(outdir_path, f"{design_name}_{instance_id}.elf"))

    # Write the end address (where spike will fail), for further analysis.
    with open(os.path.join(outdir_path, f"{design_name}_{instance_id}_finaladdr.txt"), "w") as f:
        f.write(hex(fuzzerstate.final_bb_base_addr))

    # Count the instructions
    num_instrs = len(fuzzerstate.final_bb)
    for bb in fuzzerstate.instr_objs_seq:
        num_instrs += len(bb)
    with open(os.path.join(outdir_path, f"{design_name}_{instance_id}_numinstrs.txt"), "w") as f:
        f.write(hex(num_instrs))

    with open(os.path.join(outdir_path, f"{design_name}_{instance_id}_active_threads.txt"), "w") as f:
        f.write(str(fuzzerstate.num_active_threads))

    # Save the tuple for debug purposes
    with open(os.path.join(outdir_path, f"{design_name}_{instance_id}_tuple.txt"), "w") as f:
        f.write('(' + ', '.join(map(str, [memsize, design_name, randseed, nmax_bbs, authorize_privileges])) + ')')


def gen_many_elfs(design_name: str, num_cores: int, num_elfs: int, outdir_path, emulator_path: str = None, verbose: bool = True, max_num_instructions: int = None, seed_id: int = 0, bug_filter_json: str = None):
    random.seed(0)
    load_bug_filter_json(bug_filter_json)

    # Set the emulator path if provided
    if emulator_path:
        set_emulator_path(emulator_path)

    # Ensure that the output directory exists.
    os.makedirs(outdir_path, exist_ok=True)

    # Gen the program descriptors using seed_id * num_elfs + i to ensure non-overlapping randseeds
    base_seed = seed_id * num_elfs
    memsizes, _, randseeds, num_bbss, authorize_privilegess = tuple(zip(*[gen_new_test_instance(design_name, base_seed + i, True) for i in range(num_elfs)]))
    
    if max_num_instructions is not None:
        num_bbss = [10000] * num_elfs
    
    workloads = [(i, memsizes[i], design_name, randseeds[i], num_bbss[i], authorize_privilegess[i], False, outdir_path, max_num_instructions, bug_filter_json) for i in range(num_elfs)]

    calibrate_spikespeed()
    profile_get_medeleg_mask(design_name)

    print(f"Starting ELF generation on {num_cores} processes.")
    progress_bar = tqdm(total=num_elfs)
    with mp.Pool(num_cores) as pool:
        results = pool.imap(__gen_elf_worker, workloads)

        for result in results:
            if verbose:
                progress_bar.update(1)

    progress_bar.close()
