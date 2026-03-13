/***************************************************************************************
* Copyright (c) 2014-2021 Zihao Yu, Nanjing University
* Copyright (c) 2020-2022 Institute of Computing Technology, Chinese Academy of Sciences
*
* NEMU is licensed under Mulan PSL v2.
* You can use this software according to the terms and conditions of the Mulan PSL v2.
* You may obtain a copy of Mulan PSL v2 at:
*          http://license.coscl.org.cn/MulanPSL2
*
* THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
* EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
* MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
*
* See the Mulan PSL v2 for more details.
***************************************************************************************/

#ifndef __INTR_H__
#define __INTR_H__

#include <cpu/decode.h>
#include "csr.h"

enum {
  IRQ_USIP,  // reserved yet
  IRQ_SSIP,
  IRQ_VSSIP,
  IRQ_MSIP,
  IRQ_UTIP,  // reserved yet
  IRQ_STIP,
  IRQ_VSTIP,
  IRQ_MTIP,
  IRQ_UEIP,  // reserved yet
  IRQ_SEIP,
  IRQ_VSEIP,
  IRQ_MEIP,
  IRQ_SGEI,  // Supervisor guest external interrupt
  IRQ_LCOFI, // Local counter overflow interrupt
};

#define INTR_BIT (1ULL << 63)

#define VSI_MASK   (MIP_VSSIP | MIP_VSTIP | MIP_VSEIP)
#define HSI_MASK   (VSI_MASK | MIP_SGEIP)
#define SI_MASK    (MIP_SSIP | MIP_STIP | MIP_SEIP)
#define LCI_MASK   (~0x1FFFULL)
#define LCI_EXCLUDE_LCOFI_MASK (~0x3FFFULL)
#define EXCLUDE_SEI_MASK ~(0x1ULL << IRQ_SEIP)

// now NEMU does not support EX_IAM,
// so it may ok to use EX_IAM to indicate a successful memory access
#define MEM_OK 0

word_t raise_intr(word_t NO, vaddr_t epc);
#define return_on_mem_ex() do { if (cpu.mem_exception != MEM_OK) return; } while (0)
word_t gen_gva(word_t NO, bool is_hls, bool is_mem_access_virtual);
bool intr_deleg_S(word_t exceptionNO);
bool intr_deleg_VS(word_t exceptionNO);


#ifdef CONFIG_RVH
#define SELECT_DUT_INTR_TVAL_REG(ex) ((intr_deleg_VS(ex)) ? (word_t)cpu.execution_guide.vstval :(intr_deleg_S(ex)) ? (word_t)cpu.execution_guide.stval : (word_t)cpu.execution_guide.mtval)
#else
#define SELECT_DUT_INTR_TVAL_REG(ex) ((intr_deleg_S(ex)) ? (word_t)cpu.execution_guide.stval : (word_t)cpu.execution_guide.mtval)
#endif

#endif
