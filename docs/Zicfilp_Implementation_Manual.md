# NEMU Zicfilp 功能实现手册

## 1. 简介 (Introduction)
本项目在 NEMU (Nanjing University Emulator) 中完整实现了 RISC-V Zicfilp (Zero-cost Instruction Control Flow Integrity Landing Pad) 这个针对前向控制流完整性 (Forward CFI) 保护的硬件安全拓展。Zicfilp 的核心使命是防御 ROP/JOP 等危险的控制流劫持攻击，其主要机制是通过由硬件强制检查所有的间接跳转指令 (`jalr`) 及相应的目标“降落”指令 (`lpad`/即 `auipc rd=x0`) 得以实现。

## 2. 功能设计与模块实现解析
本次实现的核心围绕一个关键的微处理器状态属性：**`ELP` (Expected Landing Pad)**。
- 当具备对应权限的处理器执行到 `jalr` (除去部分如 `x1/x5/x7` 白名单调用的免检返回行为外) 间接跳转指令时，硬件会自动将处理器的 `ELP` 更新设为 `1` (即 LP_EXPECTED)。
- 在 `ELP = 1` 时，下一条执行的紧接指令**必须**是受到标记的降落指令 (`lpad`)。如果指令不是 `lpad` 或该降落指令携带的 Immediate 标签签名与硬件白名单校验寄存器 (`x7`) 中由软件记录的预期标签不匹配，则会立刻被硬件拦截，并触发软件状态检查异常（特权规范编号：`EX_SWC`，即 Exception Code 18）。

我们在 NEMU 中进行了对应各模块源码的全面改造支持，具体修改涵盖三个实现阶段：

### 阶段一：核心状态拓展与系统控制寄存器 (CSR) 支持
**本阶段涉及修改的文件**：
- `src/isa/riscv64/include/isa-def.h`
- `src/isa/riscv64/local-include/csr.h`
- `src/isa/riscv64/system/priv.c`

**核心设计理念与修改详情**：
1. **CPU Tracking**: 在贯穿全局模拟器的 `riscv64_CPU_state` 结构体配置中，添加了 `uint8_t elp;` 属性。在每一条机器指令流转周期中追踪处理器当下是否正在处于“迫切期望一个 `lpad` 指令”的状态。
2. **CSR 与权限机制适配**:
    - 为了支持分别从 Machine、Supervisor 与 User Mode (甚至 Virtual Mode 虚拟化下) 开关 CFI 防御，我们在 `csr.h` 为环境配置相关的 `menvcfg`, `senvcfg`, `henvcfg` 控制结构体补充了比特 2 的 `LPE (Landing Pad Enable)` 开关。
    - 在安全配置寄存器 `mseccfg` 中更新并开启了对 `MLPE (bit 10)` 的掩码检查，使机器级 M-模式 同样支持分支审查。
    - 拓展修改了 `mstatus` 和 `sstatus` 硬件状态结构，添加了 Zicfilp 强行注入陷阱时必须专用于留存当前原本状态的保留位：`SPELP (bit 23)` 与 `MPELP (bit 41)`。我们同步修改了对应的 `MENVCFG_WMASK` 等各级别的掩码校验常数，以确保 NEMU 对于特权软件读写 CSR 指令给予全盘支持。

### 阶段二：异常陷阱隔离、状态存储与环境挂起 (Exception Handling)
**本阶段涉及修改的文件**：
- `src/isa/riscv64/system/intr.c`

**核心设计理念与修改详情**：
控制流安全验证意味着随时可以阻断违规程序的路径并弹射回特权内核捕捉该行为（即触发 Trap），此行为必须保证 Zicfilp 自身被挂板前 `ELP` 的现场能无缝保存、恢复：
1. 更新了 NEMU 主处理异常的枢纽方法 `raise_intr()`。
2. 在通过陷入条件捕获进 `M-Mode` 时，将跳出前的当前 `cpu.elp` 值存入目标 `mstatus->mpelp`，随后安全清除当前寄存器的记录清零 (`cpu.elp = 0`) 便于内核本身免受 ELP=1 期望要求影响。
3. 同样，对于捕获到 `S-Mode` 时存入 `mstatus->spelp`，捕获到虚拟化的 V-模式被注入环境时向 `vsstatus->spelp` 保留位写入。
4. 我们同步在此前的 `riscv64_priv_mret` 和 `riscv64_priv_sret` 等陷阱返回机制 (`priv.c`) 处增加了将内核留存的各项 `<Mode>PELP` 值反向读取赋回给当前 `cpu.elp` 的动作。这保证了合法的上下文倒闸恢复。

### 阶段三：执行解码器判定与指令级别拦截 (Decoder & Executor)
**本阶段涉及修改的文件**：
- `src/cpu/cpu-exec.c`
- `src/isa/riscv64/instr/rvi/control.h`
- `src/isa/riscv64/instr/rvi/compute.h`

**核心设计理念与修改详情**：
该阶段真正落实了基于机器指令周期的 CFI 安全门控硬件级判定：
1. **指令周期调度全域执行判定 (`cpu-exec.c`)**: 在全模拟器的主执行逻辑 `execute()` 预先加入全局检测：如果处理器处于开启要求 `cpu.elp == 1` 的警戒期，必须进行解码层强校验。一旦当前指令的 Opcode 不是 16进制形态标准的 `auipc` 所属家族机器码 (`0x17`)，说明违规越界，会立刻丢出 `longjmp_exception(EX_SWC)`。
2. **`jalr` 指令间接跳转触发规则 (`control.h`)**:
    精准切面对跳转进行拦截：验证特权级模式当前 `zicfilp_en` (`mseccfg` / `menvcfg` / `senvcfg` 中的标志设置) 是否被操作系统赋权使能。如果处于开启且所调用的地址源寄存器 (`rs1`) 不是 ABI 归约下的常规子程序调用白名单寄存器 (`x1`, `x5`, `x7`) 的直接 `return` 或者硬核调度类目标时，立刻将触发下一周期的 `cpu.elp` 置位 1。
3. **着陆点指令的细粒度合法性防线 (`auipc`/`lpad`) (`compute.h`)**:
    一旦上一指令留给了当下周期的 `elp == 1`：
    - (a) 检查当前指令的目标操作寄存器是否为 `rd == 0` （此为规范强制，也是区分常规 `auipc` 还是用作 `lpad` 的依据）。
    - (b) 检查当前的 Program Counter (`PC` 指针) 是否对齐到 4 字节的地址边界。
    - (c) **核心细粒度匹配验证 (Fine-grained check)**: 抽取指令 31 位至 12 位这高 20 位包含的签名编号（即 `LPL` 参数），并与运行时寄存器 `x7` 的对应顶部 20 位记录的哈希授权码对账匹配，且容忍 `LPL=0` 的全粗粒白名单放行。
    若上面三则防线任一条件判断落空，直接向主机抛出 `EX_SWC`。如果安然无恙通过，则降落认证成功，取消通缉拦截机制也就是归还 `elp = 0` 继续执行正常的程序流。

## 3. 测试与总结预留 (Conclusion)
本项目成功深入到基础 ISA, 内存管理以及解码树内核完成了上述拓展。若需要进行真实程序的 Zicfilp 后门与侧信道漏洞注入防范演示，通常需要调用搭载有较新版本且启用 `-mrv64g_zicfilp` 前端控制编译能力的 LLVM/GCC 工具链来构建产生自带有签名且分布 `lpad` 埋点支持验证的完整 ELF / 裸机二进制。导入此二进制于本版本 NEMU 环境中，结合 CSR 使能即可展示对应的安全防护效应。
