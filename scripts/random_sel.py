#!/usr/bin/env python3
"""Random optimizer for instruction probability distribution.

This module implements an optimizer to randomly perturb instruction probabilities
based on category-level uniform distribution for fuzzgpu test generation.

The optimizer groups instructions by (category, group) pairs and maintains a
probability distribution over these categories. Within each category, instructions
are uniformly distributed.

The boost values are derived from cascade's ISAINSTRCLASS_INITIAL_BOOSTERS to maintain
semantic consistency with the original fuzzer's instruction class weights.
"""

import json
import subprocess
import numpy as np
import random
from pathlib import Path
from typing import List, Dict, Optional, Tuple
from collections import defaultdict


# Category booster values mapped from cascade's ISAINSTRCLASS_INITIAL_BOOSTERS
# Key format: "category+group" matching the instr_to_category mapping
# Values are derived from the corresponding ISAInstrClass boosters
CATEGORY_INITIAL_BOOSTERS: Dict[str, float] = {
    # Load instructions
    "load+rv32f": 0.2,      # MEMFPU
    "load+rv32i": 0.5,      # MEM
    "load+rvv": 0.1,        # RVV_LOAD
    
    # Store instructions
    "store+rv32f": 0.2,     # MEMFPU
    "store+rv32i": 0.5,     # MEM
    "store+rvv": 0.1,       # RVV_STORE
    
    # Shift instructions
    "shift+rv32i": 0.1,     # ALU
    
    # Arithmetic instructions
    "arithmetic+rv32f": 0.1,  # FPU
    "arithmetic+rv32i": 0.1,  # ALU
    "arithmetic+rv32m": 0.1,  # MULDIV
    "arithmetic+rvv": 0.1,    # RVV
    
    # Logical instructions
    "logical+rv32i": 0.1,   # ALU
    "logical+rvv": 0.1,     # RVV
    
    # Compare instructions
    "compare+rv32f": 0.1,   # FPU
    "compare+rv32i": 0.1,   # ALU
    "compare+rvv": 0.1,     # RVV
    
    # Branch instructions
    "branch+rv32i": 0.1,    # BRANCH
    "branch+rvv": 0.3,      # VENTUS_BRANCH
    
    # Jump instructions
    "jump+rv32i": 0.25,     # Average of JAL (0.1) and JALR (0.4)
    
    # Synchronization instructions
    "synch+rv32i": 0.00005, # SPECIAL (0.0001/2)
    
    # CSR instructions
    "csr+rv32i": 0.1,      # RANDOM_CSR
    
    # Thread control instructions
    "thread_ctrl+rvv": 0.3, # VENTUS_BAR
    
    # Thread merge instructions
    "thread_merge+rvv": 0.1, # RVV
    
    # Register control instructions
    "reg_ctrl+rvv": 1.0,    # REGFSM
    
    # Tensor instructions
    "tensor+rvv": 0.1,      # RVV
    
    # Pseudo instructions
    "pseudo+rv32i": 0.0001, # Very low weight for pseudo instructions
}

# Default booster for unknown categories
DEFAULT_CATEGORY_BOOSTER = 0.1


class RandomSelection:
    """Random selection for category-based instruction distribution.
    
    Groups instructions by (category, group) pairs and adjusts category probabilities
    using the boost+random algorithm from cascade's fuzzerstate:
        weight = (random.random() + 0.05) * CATEGORY_INITIAL_BOOSTERS[category]
    
    Within each category, instructions are uniformly distributed.
    
    Args:
        n_instrs: Number of instructions (dimension of probability vector)
        instr_names: List of instruction names (ordered, matching fuzzgpu registration order)
        instr_info: List of instruction info dicts with 'name', 'category', 'group' fields
        seed: Random seed for reproducibility (follows generator seed)
    """
    
    def __init__(self, n_instrs: int, instr_names: List[str], 
                 instr_info: Optional[List[Dict[str, str]]] = None,
                 seed: Optional[int] = None):
        # Set random seed for reproducibility
        self.seed = seed
        if seed is not None:
            random.seed(seed)
            np.random.seed(seed)
        
        self.n_instrs = n_instrs
        self.instr_names = instr_names
        assert self.n_instrs == len(self.instr_names), \
            f"n_instrs {n_instrs} does not match length of instr_names {len(instr_names)}"
        
        # Build category mapping from instr_info
        # Category key: (category, group) tuple
        self.instr_to_category: Dict[str, Tuple[str, str]] = {}
        self.category_to_instrs: Dict[Tuple[str, str], List[int]] = defaultdict(list)
        
        if instr_info is not None:
            assert len(instr_info) == n_instrs, \
                f"instr_info length {len(instr_info)} does not match n_instrs {n_instrs}"
            for idx, info in enumerate(instr_info):
                name = info['name']
                category_key = (info['category'], info['group'])
                self.instr_to_category[name] = category_key
                self.category_to_instrs[category_key].append(idx)
        else:
            # Fallback: treat all instructions as one category
            default_key = ('default', 'default')
            for idx, name in enumerate(instr_names):
                self.instr_to_category[name] = default_key
                self.category_to_instrs[default_key].append(idx)
        
        # Category list (ordered)
        self.categories = list(self.category_to_instrs.keys())
        self.n_categories = len(self.categories)
        self.category_to_idx = {cat: idx for idx, cat in enumerate(self.categories)}
        
        # Build category booster array from CATEGORY_INITIAL_BOOSTERS
        # Matching the ISAINSTRCLASS_INITIAL_BOOSTERS approach from cascade
        self.category_boosters = np.zeros(self.n_categories, dtype=np.float64)
        for cat_idx, category in enumerate(self.categories):
            cat_key = f"{category[0]}+{category[1]}"
            self.category_boosters[cat_idx] = CATEGORY_INITIAL_BOOSTERS.get(
                cat_key, DEFAULT_CATEGORY_BOOSTER
            )
        
        # Initialize category probabilities using boost+random algorithm
        # weight = (random.random() + 0.05) * booster
        self.category_p = self._gen_boosted_random_weights()
        
        # Instruction probabilities (computed from category probabilities)
        self.p = np.zeros(n_instrs, dtype=np.float64)
        self._compute_instr_prob()
        
        # Statistics
        self.total_samples = 0
        self.iteration = 0
    
    def _gen_boosted_random_weights(self) -> np.ndarray:
        """Generate category weights using boost+random algorithm.
        
        Implements the same algorithm as cascade's fuzzerstate.gen_pick_weights():
            weight = (random.random() + 0.05) * INITIAL_BOOSTER
        
        Returns:
            Normalized array of category probabilities
        """
        # Generate random factors: (random.random() + 0.05) for each category
        random_factors = np.array([random.random() + 0.05 for _ in range(self.n_categories)], 
                                  dtype=np.float64)
        
        # Apply booster: weight = factor * booster
        weights = random_factors # * self.category_boosters
        
        # Normalize to probability distribution
        return self._normalize(weights)
    
    def _compute_instr_prob(self) -> None:
        """Compute instruction probabilities from category probabilities.
        
        Within each category, instructions are uniformly distributed.
        P(instr) = P(category) / num_instrs_in_category
        """
        self.p.fill(0.0)
        for cat_idx, category in enumerate(self.categories):
            instr_indices = self.category_to_instrs[category]
            n_instrs_in_cat = len(instr_indices)
            if n_instrs_in_cat > 0:
                prob_per_instr = self.category_p[cat_idx] / n_instrs_in_cat
                for instr_idx in instr_indices:
                    self.p[instr_idx] = prob_per_instr
        self.p = np.array([random.random() + 0.05 for _ in range(len(self.p))], dtype=np.float64)
        # Normalize to ensure sum is exactly 1.0 (handle floating point errors)
        self.p = self._normalize(self.p)
    
    def get_current_prob(self) -> np.ndarray:
        """Get current probability distribution.
        
        Returns:
            Array of probabilities for each instruction
        """
        return self.p.copy()
    
    def get_category_prob(self) -> np.ndarray:
        """Get current category probability distribution.
        
        Returns:
            Array of probabilities for each category
        """
        return self.category_p.copy()
    
    def _normalize(self, p: np.ndarray) -> np.ndarray:
        """Normalize probability vector to sum to 1."""
        p_sum = np.sum(p)
        if p_sum > 0:
            return p / p_sum
        return np.full(len(p), 1.0 / len(p))
    
    def update(self, observed_freq: Optional[np.ndarray] = None) -> Dict[str, float]:
        """Update category probability distribution with boost+random algorithm.
        
        Regenerates category weights using the cascade fuzzerstate algorithm:
            weight = (random.random() + 0.05) * CATEGORY_INITIAL_BOOSTERS[category]
        
        Args:
            observed_freq: Observed instruction frequency vector (ignored by RandomSelection)
            
        Returns:
            Dictionary with metrics (empty for RandomSelection)
        """
        self.total_samples += 1
        self.iteration += 1
        
        # Regenerate category probabilities using boost+random algorithm
        self.category_p = self._gen_boosted_random_weights()
        
        # Recompute instruction probabilities
        self._compute_instr_prob()
        
        return {}
    
    def update_from_insn_stats(self, insn_stats: Dict[str, int]) -> Dict[str, float]:
        """Update probability distribution (interface method).
        
        Args:
            insn_stats: Dictionary mapping instruction names to execution counts
            
        Returns:
            Dictionary with metrics
        """
        # RandomSelection does not use execution stats
        return self.update()
    
    def get_prob_json(self) -> dict:
        """Get current probability distribution as JSON-serializable dict.
        
        Returns:
            Dictionary with 'prob' field containing probability list
        """
        return {
            'prob': self.p.tolist()
        }
    
    def save_prob_json(self, filepath: Path) -> None:
        """Save current probability distribution to JSON file.
        
        Args:
            filepath: Output JSON file path
        """
        with open(filepath, 'w') as f:
            json.dump(self.get_prob_json(), f, indent=2)
    
    def get_statistics(self) -> dict:
        """Get optimizer statistics.
        
        Returns:
            Dictionary with optimizer statistics
        """
        return {
            'total_samples': self.total_samples,
            'iteration': self.iteration,
            'n_instrs': self.n_instrs,
            'n_categories': self.n_categories,
            'prob_max': float(np.max(self.p)),
            'prob_min': float(np.min(self.p)),
            'prob_mean': float(np.mean(self.p)),
            'prob_std': float(np.std(self.p)),
            'category_prob_max': float(np.max(self.category_p)),
            'category_prob_min': float(np.min(self.category_p)),
            'category_prob_mean': float(np.mean(self.category_p)),
            'category_prob_std': float(np.std(self.category_p)),
            'category_booster_max': float(np.max(self.category_boosters)),
            'category_booster_min': float(np.min(self.category_boosters)),
        }
    
    def get_category_info(self) -> Dict[str, Dict]:
        """Get category information including instruction counts, boosters and probabilities.
        
        Returns:
            Dictionary mapping category names to info dicts
        """
        info = {}
        for cat_idx, category in enumerate(self.categories):
            cat_name = f"{category[0]}+{category[1]}"
            info[cat_name] = {
                'category': category[0],
                'group': category[1],
                'n_instrs': len(self.category_to_instrs[category]),
                'booster': float(self.category_boosters[cat_idx]),
                'prob': float(self.category_p[cat_idx]),
                'instr_indices': self.category_to_instrs[category],
            }
        return info
    
    def save_state(self, filepath: Path) -> None:
        """Save complete optimizer state to file.
        
        Args:
            filepath: Output file path
        """
        # Convert category keys (tuples) to strings for JSON serialization
        category_to_instrs_json = {
            f"{cat[0]}+{cat[1]}": indices 
            for cat, indices in self.category_to_instrs.items()
        }
        instr_to_category_json = {
            name: f"{cat[0]}+{cat[1]}" 
            for name, cat in self.instr_to_category.items()
        }
        categories_json = [f"{cat[0]}+{cat[1]}" for cat in self.categories]
        
        state = {
            'n_instrs': self.n_instrs,
            'n_categories': self.n_categories,
            'instr_names': self.instr_names,
            'categories': categories_json,
            'category_to_instrs': category_to_instrs_json,
            'instr_to_category': instr_to_category_json,
            'category_boosters': self.category_boosters.tolist(),
            'category_p': self.category_p.tolist(),
            'p': self.p.tolist(),
            'total_samples': self.total_samples,
            'iteration': self.iteration,
            'seed': self.seed,
        }
        with open(filepath, 'w') as f:
            json.dump(state, f, indent=2)
    
    @classmethod
    def load_state(cls, filepath: Path) -> 'RandomSelection':
        """Load selection state from file.
        
        Args:
            filepath: State file path
            
        Returns:
            Restored RandomSelection instance
        """
        with open(filepath, 'r') as f:
            state = json.load(f)
        
        # Reconstruct instr_info from saved state
        instr_to_category_json = state['instr_to_category']
        instr_info = []
        for name in state['instr_names']:
            cat_str = instr_to_category_json[name]
            parts = cat_str.split('+', 1)
            category = parts[0]
            group = parts[1] if len(parts) > 1 else ''
            instr_info.append({
                'name': name,
                'category': category,
                'group': group
            })
        
        # Restore seed if available (for backward compatibility)
        seed = state.get('seed', None)
        
        optimizer = cls(
            n_instrs=state['n_instrs'],
            instr_names=state['instr_names'],
            instr_info=instr_info,
            seed=seed
        )
        
        # Restore saved boosters if available (for backward compatibility)
        if 'category_boosters' in state:
            optimizer.category_boosters = np.array(state['category_boosters'], dtype=np.float64)
        
        optimizer.category_p = np.array(state['category_p'], dtype=np.float64)
        optimizer.p = np.array(state['p'], dtype=np.float64)
        optimizer.total_samples = state['total_samples']
        optimizer.iteration = state['iteration']
        
        return optimizer


def get_instr_info(fuzz_exe: str, bug_filter_json: Optional[str] = None, default: bool = True) -> List[Dict[str, str]]:
    """Get instruction info (name, category, group) from fuzzgpu.

    Args:
        fuzz_exe: Path to fuzz_gpu executable
        bug_filter_json: Optional path to bug filter JSON file (same as fuzz --bug-filter-json)

    Returns:
        List of instruction info dicts with 'name', 'category', 'group' fields
        in registration order
    """
    cmd = [fuzz_exe, '--get-registered-instr-info']
    if not default:
        cmd.extend(['--disable-default-bug-filtering',])
    if bug_filter_json:
        cmd.extend(['--bug-filter-json', bug_filter_json])
    
    result = subprocess.run(
        cmd,
        capture_output=True,
        text=True,
        timeout=30
    )
    if result.returncode != 0:
        raise RuntimeError(f"Failed to get instruction info: {result.stderr}")

    output = result.stdout
    
    # Extract JSON between special markers
    begin_marker = "INSTR_INFO_JSON_BEGIN"
    end_marker = "INSTR_INFO_JSON_END"
    
    begin_idx = output.find(begin_marker)
    end_idx = output.find(end_marker)
    
    if begin_idx == -1 or end_idx == -1:
        raise RuntimeError(
            f"Failed to find JSON markers in output. "
            f"Expected '{begin_marker}' and '{end_marker}'.\n"
            f"Output: {output[:500]}"
        )
    
    # Extract content between markers
    json_str = output[begin_idx + len(begin_marker):end_idx].strip()
    
    try:
        instr_info = json.loads(json_str)
    except json.JSONDecodeError as e:
        raise RuntimeError(f"Failed to parse instruction info JSON: {e}\nJSON content: {json_str[:500]}")

    if not isinstance(instr_info, list):
        raise RuntimeError(f"Expected instruction info to be a list, got {type(instr_info)}")

    return instr_info
