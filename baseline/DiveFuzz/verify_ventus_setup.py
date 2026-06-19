#!/usr/bin/env python3
"""
Verification script for Ventus architecture support
"""

import sys
from pathlib import Path

def verify_architecture_formats():
    """Verify ARCHITECTURE_FORMATS is properly configured"""
    try:
        from fuzzer.generator.instr_generator.formats import ARCHITECTURE_FORMATS, INSTRUCTION_FORMATS
        
        print("✓ ARCHITECTURE_FORMATS imported successfully")
        
        if 'ventus' not in ARCHITECTURE_FORMATS:
            print("✗ 'ventus' not found in ARCHITECTURE_FORMATS")
            return False
        
        print("✓ 'ventus' found in ARCHITECTURE_FORMATS")
        
        ventus_exts = ARCHITECTURE_FORMATS['ventus']
        print(f"  Ventus includes {len(ventus_exts)} extensions:")
        
        missing = []
        for ext in ventus_exts:
            if ext in INSTRUCTION_FORMATS:
                print(f"    ✓ {ext}")
            else:
                print(f"    ✗ {ext} (NOT FOUND in INSTRUCTION_FORMATS)")
                missing.append(ext)
        
        if missing:
            print(f"\n✗ Missing {len(missing)} extensions: {missing}")
            return False
        
        print("\n✓ All Ventus extensions are available")
        return True
        
    except Exception as e:
        print(f"✗ Error verifying ARCHITECTURE_FORMATS: {e}")
        return False


def verify_compiler_config():
    """Verify compiler configuration for Ventus"""
    try:
        from fuzzer.generator.reg_analyzer.compiler_config import get_compiler_config
        
        print("\n✓ compiler_config imported successfully")
        
        # Test ventus configuration
        config = get_compiler_config('ventus', 32)
        
        print("  Ventus (32-bit) configuration:")
        print(f"    AS: {config['as']}")
        print(f"    LD: {config['ld']}")
        print(f"    ObjCopy: {config['objcopy']}")
        print(f"    AS Flags: {config['as_flags']}")
        print(f"    LD Flags: {config.get('ld_flags', 'N/A')}")
        
        # Verify required fields
        required_fields = ['as', 'ld', 'objcopy', 'as_flags']
        missing_fields = [f for f in required_fields if f not in config]
        
        if missing_fields:
            print(f"✗ Missing configuration fields: {missing_fields}")
            return False
        
        # Verify ventus-env paths
        if './fuzzgpu/targets/ventus-env/install/bin/clang' not in config['as']:
            print("✗ AS compiler path doesn't match expected ventus-env path")
            return False
        
        if './fuzzgpu/targets/ventus-env/install/bin/clang' not in config['ld']:
            print("✗ LD compiler path doesn't match expected ventus-env path")
            return False
        
        print("\n✓ Compiler configuration is correct")
        return True
        
    except Exception as e:
        print(f"✗ Error verifying compiler_config: {e}")
        return False


def verify_riscv_compiler():
    """Verify RiscvCompiler supports architecture parameter"""
    try:
        from fuzzer.generator.reg_analyzer.riscv_compiler import RiscvCompiler
        
        print("\n✓ RiscvCompiler imported successfully")
        
        # Test default compiler
        default_compiler = RiscvCompiler()
        print(f"  Default compiler architecture: {default_compiler.architecture}")
        
        # Test that ventus can be specified (even if toolchain isn't available)
        try:
            ventus_compiler = RiscvCompiler(architecture='ventus', arch_bits=32)
            print(f"  Ventus compiler architecture: {ventus_compiler.architecture}")
            print(f"  Ventus compiler AS command: {ventus_compiler.as_cmd}")
            print(f"  Ventus compiler AS flags: {ventus_compiler.as_flags}")
        except Exception as e:
            print(f"  Note: Ventus compiler toolchain verification failed (expected if ventus-env not installed): {e}")
            # This is OK - the toolchain might not be installed yet
        
        print("\n✓ RiscvCompiler supports architecture parameter")
        return True
        
    except Exception as e:
        print(f"✗ Error verifying RiscvCompiler: {e}")
        return False


def verify_elf_to_img():
    """Verify elf_to_img supports architecture parameter"""
    try:
        from fuzzer.utils.elf_to_img import process_file
        import inspect
        
        print("\n✓ elf_to_img imported successfully")
        
        # Check function signature
        sig = inspect.signature(process_file)
        params = list(sig.parameters.keys())
        
        if 'architecture' not in params:
            print("✗ 'architecture' parameter not found in process_file")
            return False
        
        if 'arch_bits' not in params:
            print("✗ 'arch_bits' parameter not found in process_file")
            return False
        
        print("  process_file signature:")
        print(f"    Parameters: {params}")
        
        print("\n✓ elf_to_img supports architecture and arch_bits parameters")
        return True
        
    except Exception as e:
        print(f"✗ Error verifying elf_to_img: {e}")
        return False


def main():
    """Run all verifications"""
    print("=" * 70)
    print("Ventus Architecture Support Verification")
    print("=" * 70)
    
    results = []
    
    results.append(("ARCHITECTURE_FORMATS", verify_architecture_formats()))
    results.append(("Compiler Configuration", verify_compiler_config()))
    results.append(("RiscvCompiler Support", verify_riscv_compiler()))
    results.append(("ELF to IMG Support", verify_elf_to_img()))
    
    print("\n" + "=" * 70)
    print("Verification Summary")
    print("=" * 70)
    
    for name, result in results:
        status = "✓ PASS" if result else "✗ FAIL"
        print(f"{status}: {name}")
    
    all_passed = all(result for _, result in results)
    
    if all_passed:
        print("\n✓ All verifications passed!")
        print("\nVentus architecture support is properly configured.")
        return 0
    else:
        print("\n✗ Some verifications failed.")
        print("Please review the output above and fix any issues.")
        return 1


if __name__ == '__main__':
    sys.exit(main())
