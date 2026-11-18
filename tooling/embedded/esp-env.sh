#!/usr/bin/env bash

# Select the target-specific ESP-IDF tools after export.sh. ESP-IDF's shell
# export is intentionally target-neutral in this checkout, while the C3 uses
# the separately installed RISC-V compiler and QEMU binary.
aisthra_embedded_select_esp_tools()
{
    local target="$1"
    local compiler_bin
    local qemu_bin

    case "$target" in
        esp32|esp32s3)
            return 0
            ;;
        esp32c3)
            compiler_bin="$(find "${IDF_TOOLS_PATH:-}"/tools/riscv32-esp-elf \
                -type f -name riscv32-esp-elf-gcc -printf '%h\n' -quit 2>/dev/null)"
            qemu_bin="$(find "${IDF_TOOLS_PATH:-}"/tools/qemu-riscv32 \
                -type f -name qemu-system-riscv32 -printf '%h\n' -quit 2>/dev/null)"
            if [[ -z "$compiler_bin" || -z "$qemu_bin" ]]; then
                return 1
            fi
            export PATH="${compiler_bin}:${qemu_bin}:${PATH}"
            # CMake is invoked by idf.py in a clean subprocess for this
            # target; provide absolute compiler paths so it cannot lose the
            # target-specific PATH during set-target.
            export CC="${compiler_bin}/riscv32-esp-elf-gcc"
            export CXX="${compiler_bin}/riscv32-esp-elf-g++"
            export ASM="${compiler_bin}/riscv32-esp-elf-gcc"
            ;;
        *)
            return 1
            ;;
    esac
}
