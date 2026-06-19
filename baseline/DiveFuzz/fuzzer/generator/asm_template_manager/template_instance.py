# Copyright (c) 2024-2025 Institute of Information Engineering, Chinese Academy of Sciences
#
# DiveFuzz is licensed under Mulan PSL v2.
# You can use this software according to the terms and conditions of the Mulan PSL v2.
# You may obtain a copy of Mulan PSL v2 at:
#          http://license.coscl.org.cn/MulanPSL2
#
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
# EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
# MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
#
# See the Mulan PSL v2 for more details.

from dataclasses import dataclass
from .constants import TemplateType


@dataclass(frozen=True)
class TemplateInstance:
    """
    Immutable template instance with pre-rendered header/footer.

    Each seed should create its own instance for independent randomization.
    The frozen=True ensures thread safety when sharing across threads.

    Attributes:
        header: Pre-rendered assembly header (startup, init, exception handlers)
        footer: Pre-rendered assembly footer (support routines, data sections)
        template_type: The type of template used (M-mode, S-mode, U-mode variants)
        isa: ISA string (e.g., 'rv64gc')
        arch_bits: Architecture bit width (32 or 64)
    """
    header: str
    footer: str
    template_type: TemplateType
    isa: str
    arch_bits: int

    def get_complete_template(self, instructions: str, architecture: str, nop_template = False) -> str:
        """
        Get complete template with instructions inserted between header and footer.

        Args:
            instructions: The generated instruction sequence to insert

        Returns:
            Complete assembly template as string
        """
        header = self.header
        if not nop_template and architecture == 'ventus':
            import re
            # Find first vsetvli instruction and modify preceding li x1, 1 to li x1, 32, enabling the multi-thread
            vsetvli_match = re.search(r'vsetvli', self.header)
            if vsetvli_match:
                # Find li x1, 1 before the vsetvli
                before_vsetvli = self.header[:vsetvli_match.start()]
                li_x1_pattern = r'li\s+x1,\s*1\b'
                # Find all matches and replace the last one (closest to vsetvli)
                matches = list(re.finditer(li_x1_pattern, before_vsetvli))
                if matches:
                    last_match = matches[-1]
                    header = (self.header[:last_match.start()] + 
                                  'li x1, 32' + 
                                  self.header[last_match.end():])
                else:
                    assert False, 'vsetvli found but no preceding li x1, 1'
            else:
                assert False, 'No vsetvli instruction found'

        return f"{header}{instructions}{self.footer}"
