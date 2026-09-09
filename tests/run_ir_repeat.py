"""Run the actual transmit functions with mock peripherals on an ARM emulator.

Usage: python tests/run_ir_repeat.py --keil D:/software/Keil_MDK
Unicorn may be installed into MDK-ARM/Objects/ir_repeat_test/python_deps.
Use --fvp instead if a licensed Keil Cortex-M4 FVP is available.
"""
import argparse
import pathlib
import re
import subprocess
import struct
import sys


def emulate(path, dependencies):
    sys.path.insert(0, str(dependencies))
    import unicorn as uc
    from unicorn.arm_const import UC_ARM_REG_SP, UC_ARM_REG_PC, UC_ARM_REG_R0, UC_ARM_REG_R1

    cpu = uc.Uc(uc.UC_ARCH_ARM, uc.UC_MODE_THUMB | uc.UC_MODE_MCLASS)
    cpu.mem_map(0, 0x200000)
    cpu.mem_map(0x20000000, 0x40000)
    elf = path.read_bytes()
    phoff = struct.unpack_from("<I", elf, 28)[0]
    phsize, phcount = struct.unpack_from("<HH", elf, 42)
    for i in range(phcount):
        kind, offset, address, _, size, _, _, _ = struct.unpack_from("<8I", elf, phoff + i * phsize)
        if kind == 1 and size:
            cpu.mem_write(address, elf[offset:offset + size])
    sp, pc = struct.unpack("<II", cpu.mem_read(0, 8))
    cpu.reg_write(UC_ARM_REG_SP, sp)
    output = []
    exited = []

    def word(address):
        return struct.unpack("<I", cpu.mem_read(address, 4))[0]

    def trap(cpu, number, _):
        pc = cpu.reg_read(UC_ARM_REG_PC)
        if cpu.mem_read(pc, 2) == b"\xab\xbe":
            cpu.reg_write(UC_ARM_REG_PC, (pc + 2) | 1)
        elif cpu.mem_read(pc - 2, 2) != b"\xab\xbe":
            raise RuntimeError(f"Unexpected ARM exception {number} at {pc:#x}")
        operation = cpu.reg_read(UC_ARM_REG_R0)
        arg = cpu.reg_read(UC_ARM_REG_R1)
        result = 0
        if operation == 1:  # SYS_OPEN, standard console
            result = 1
        elif operation == 3:  # SYS_WRITEC
            output.append(bytes(cpu.mem_read(arg, 1)).decode(errors="replace"))
        elif operation == 4:  # SYS_WRITE0
            length = 0
            while cpu.mem_read(arg + length, 1) != b"\0":
                length += 1
            output.append(bytes(cpu.mem_read(arg, length)).decode(errors="replace"))
        elif operation == 5:  # SYS_WRITE
            data, length = word(arg + 4), word(arg + 8)
            output.append(bytes(cpu.mem_read(data, length)).decode(errors="replace"))
        elif operation == 6:  # SYS_READ: EOF
            result = word(arg + 8)
        elif operation == 9:  # SYS_ISTTY
            result = 1
        elif operation in (2, 8, 0x0C, 0x10, 0x11, 0x13):
            result = 0
        elif operation == 0x15:  # SYS_GET_CMDLINE
            cpu.mem_write(word(arg), b"\0")
            cpu.mem_write(arg + 4, struct.pack("<I", 0))
        elif operation == 0x16:  # SYS_HEAPINFO
            cpu.mem_write(arg, struct.pack("<4I", 0x20010000, 0x20018000,
                                           0x20030000, 0x20020000))
        elif operation in (0x18, 0x20):
            exited.append(word(arg + 4) if operation == 0x20 else 0)
            cpu.emu_stop()
        else:
            raise RuntimeError(f"Unsupported semihost call {operation:#x}")
        cpu.reg_write(UC_ARM_REG_R0, result)

    cpu.hook_add(uc.UC_HOOK_INTR, trap)
    try:
        cpu.emu_start(pc | 1, 0x1FFFFE, timeout=40000000, count=500000000)
    except uc.UcError as error:
        address = cpu.reg_read(UC_ARM_REG_PC)
        print("".join(output), end="")
        raise RuntimeError(f"ARM execution failed at {address:#x}: {error}") from error
    text = "".join(output)
    print(text, end="")
    if not exited or exited[0] or "IR REPEAT TESTS PASSED" not in text:
        raise SystemExit(f"ARM tests did not pass (PC={cpu.reg_read(UC_ARM_REG_PC):#x})")


def function(source, name):
    match = re.search(r"^(?:static )?(?:void|bool|uint8_t|uint16_t) "
                      + name + r"\([^;]*?\)\s*\{", source, re.M)
    if not match:
        raise ValueError(f"Function not found: {name}")
    start = match.start()
    pos = source.index("{", start)
    depth = 1
    end = pos + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--keil", required=True, type=pathlib.Path)
    parser.add_argument("--fvp", action="store_true")
    args = parser.parse_args()
    root = pathlib.Path(__file__).resolve().parents[1]
    build = root / "MDK-ARM/Objects/ir_repeat_test"
    build.mkdir(parents=True, exist_ok=True)
    ir = (root / "bsp/bsp_ir.c").read_text(encoding="utf-8")
    gb = (root / "protocol/gbe_protocol.c").read_text(encoding="utf-8")
    header = (root / "bsp/bsp_ir.h").read_text(encoding="utf-8")
    header = header.replace('#include "n32l40x.h"', '')
    gb_header = (root / "protocol/gb_protocol.h").read_text(encoding="utf-8")
    frame = gb_header[gb_header.index("typedef struct {"):gb_header.index("} Frame_t;") + 10]
    gb_defines = "\n".join(re.findall(r"^#define (?:CMD_|ERR_).*", gb_header, re.M))
    ir_defines = "\n".join(re.findall(r"^#define (?:US_TO_TICKS|IR_\w+_(?:MS)).*", ir, re.M))
    ir_context = ir[ir.index("typedef struct"):ir.index("/**************接受")]
    gb_context = gb[gb.index("typedef struct"):gb.index("static void gbe_protocol_pc_request_motion")]
    names = ["IR_GetMs", "IR_Start", "IR_Stop", "IR_SetTimerPeriod", "IR_StartFrame",
             "IR_SendData", "IR_SendNecRepeat", "IR_IsSending", "IR_TxSucceeded",
             "IR_CurrentFrameComplete", "IR_TransmitPoll", "TIM6_IRQHandler"]
    bodies = "\n\n".join(function(ir, n) for n in names)
    gb_bodies = "\n\n".join(function(gb, n) for n in
                              ["gbe_ir_aeha_customer_parity",
                               "gbe_protocol_pc_request_ir_tansimit",
                               "gbe_protocol_ir_transmit_poll"])
    template = (root / "tests/ir_repeat_harness.c").read_text(encoding="utf-8")
    source = template.replace("/* SOURCE_TYPES */", "\n".join(
        [header, frame, gb_defines, ir_defines, ir_context, gb_context]))
    source = source.replace("/* SOURCE_FUNCTIONS */", bodies + "\n" + gb_bodies)
    (build / "test.c").write_text(source, encoding="utf-8")
    cc = args.keil / "ARM/ARMCC/bin"
    commands = [
        [cc / "armcc.exe", "--cpu=Cortex-M4", "--c99", "-O2", "-g", "-c", "test.c", "-o", "test.o"],
        [cc / "armasm.exe", "--cpu=Cortex-M4", "--apcs=interwork",
         root / "firmware/CMSIS/device/startup/startup_n32l40x.s", "-o", "startup.o"],
        [cc / "armlink.exe", "--cpu=Cortex-M4", "--entry=Reset_Handler", "--first=__Vectors",
         "--ro_base=0", "--rw_base=0x20000000", "startup.o", "test.o", "-o", "test.axf"],
        [args.keil / "ARM/FVP/MPS2_Cortex-M/FVP_MPS2_Cortex-M4_MDK.exe", "-a", "test.axf",
         "--disable-analytics", "--timelimit", "20", "-C", "fvp_mps2.mps2_visualisation.disable-visualisation=1",
         "-C", "fvp_mps2.telnetterminal0.start_telnet=0",
         "-C", "fvp_mps2.telnetterminal1.start_telnet=0",
         "-C", "fvp_mps2.telnetterminal2.start_telnet=0"],
    ]
    if not args.fvp:
        commands.pop()
    for command in commands:
        result = subprocess.run([str(x) for x in command], cwd=build, capture_output=True,
                                text=True, errors="replace", timeout=45)
        print(result.stdout, end="")
        print(result.stderr, end="")
        if result.returncode:
            raise SystemExit(result.returncode)
    if not args.fvp:
        emulate(build / "test.axf", build / "python_deps")
    elif "IR REPEAT TESTS PASSED" not in result.stdout + result.stderr:
        raise SystemExit("Simulator did not report test completion")


if __name__ == "__main__":
    main()
