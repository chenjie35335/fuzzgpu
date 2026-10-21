# Fuzzing Open-Source GPU Hardware

FuzzGPU is a fuzzing framework designed for testing RISC-V based GPGPU hardware designs. It generates test programs with complex SIMT (Single Instruction, Multiple Threads) control flow, memory access patterns, and barrier synchronization to uncover bugs in GPGPU implementations.

---

## Quick Start

1. Follow the "A.3 Set-up" in the Artifact Appendix.
```bash
make docker
```

You can create the container with the image built in phase 1 via:
```bash
make run
```
In the container, you can enter the working directory via:
```bash
cd /root/fuzzgpu
```

Tip: After the container has been created or terminated, you can restart and attach to it with:

```bash
docker start -ai fuzzgpu-new
```


2. Follow the "A.4.2 Experiments" in the Artifact Appendix. Note that E1 is optional if you have completed "A.3 Set-up".

### (E5): Ablation study (validating C6).
Execution:
```
scripts/run_E5.sh
```
Results: It reproduces Section 5.4.

### (E6): Fuzzing campaign (validating C2, C4, and C7).
Execution:
```
scripts/run_E6.sh
```
Results: FuzzGPU bug summaries, runtime breakdown (`results/plots/time_breakdown_fuzzgpu.pdf`), and bug discovery times (`results/plots/bug_discover_time.pdf`).

### What to expect
The absolute values from our AE server may differ from those reported in the paper because we are using a different hardware configuration—AMD EPYC 9T95 rather than Intel® Xeon® 6982P-C due to resource limitation, and fuzzing performance is hardware-dependent.

Nevertheless, the results should be sufficient to validate the claim.

## Generate Documentation (for reusing, optionally)

Generate the Doxygen documentation:

```bash
./build.sh --docs
```

Then view the generated guide at:

```text
docs/html/index.html
```

Use the Doxygen navigation bar to browse the generated API reference:

- `Main Page` shows this project overview.
- `Namespaces` groups documented symbols by C++ namespace.
- `Classes` lists documented classes and structs.
- `Files` lists headers and source files included in the documentation.
- The search box can find symbols, files, classes, functions, and typedefs.

---

## Supported Architectures

| Architecture | XLEN | Description |
|--------------|------|-------------|
| **Vortex** | 32-bit | Open-source RISC-V GPGPU from Georgia Tech |
| **Vortex** | 64-bit | 64-bit variant of Vortex |
| **Ventus** | 32-bit | RISC-V GPGPU from Tsinghua University |

---

## Building (for reusing, optionally)

For the full list of build options, run:

```bash
./build.sh --help
```

Basic examples:

```bash
# Build all supported configurations.
./build.sh --all

# Build for Ventus 32-bit with the default configuration.
./build.sh --arch ventus --xlen 32

# Build for Vortex 64-bit with line coverage and difftest.
./build.sh --arch vortex --xlen 64 --cov-line --dft
```

---

## Reduced-Parameter Experiment Runs (for reusing, optionally)

Note: This section is intended to facilitate future reuse. The procedure for running the experiments has already been described earlier and is therefore NOT repeated in this section.

The `scripts/run_E5.sh` and `scripts/run_E6.sh` wrappers run the ablation
study and FuzzGPU fuzzing campaign. Some experiments may be run with reduced parameters to complete in
a reasonable amount of time, while conserving the tendency shown in the paper.
Reduced runs are intended for quick validation of trends, not for regenerating
the exact paper numbers.

The easiest way to shorten a run is to open the corresponding `scripts/run_*.sh`
file and reduce the variables near the top of the script. The wrappers
`run_E5.sh` and `run_E6.sh` call lower-level scripts; edit
those lower-level scripts directly.

### E5: Ablation Study

Edit both `scripts/run_ablation_program_metric.sh` and
`scripts/run_ablation_coverage.sh`.

Recommended quick-run changes for `scripts/run_ablation_program_metric.sh`:

- Reduce `--num-seeds` from `5000` to `100` or `500`.
- Keep `--arch ventus` for the OpenGPGPU result used in the paper.
- Keep `--force-max-num-instrs 10000` unless testing a much smaller smoke run.

Recommended quick-run changes for `scripts/run_ablation_coverage.sh`:

- Reduce `NUM_SEEDS` from `100` to a smaller value such as `20` or `50`.
- Set `COV_TYPES` to only `expr` for the OpenGPGPU expression coverage result.
- Set `CONFIGS` to a subset such as `baseline no_emu_guide` for a very short
  check, or leave it empty to run all ablations.
- Set `MAX_TIME` to cap each ablation configuration if needed.

`scripts/tab_ablation.py` uses the latest `expr` ablation coverage result for
the OpenGPGPU expression coverage table.

### E6: Bug Discovery and Runtime Breakdown

Edit `scripts/run_fuzzing.sh`.

Recommended quick-run changes:

- Reduce `TARGET_TIME` from `300` seconds to `60`.
- Reduce `START_SEED_INDICES` from ten entries to one or two entries.
- Keep only one entry in `ARCH_XLEN_CONFIGS`.
- Keep only one method in `ALL_METHODS`, or run the script with `--method
  fuzzgpu`.
