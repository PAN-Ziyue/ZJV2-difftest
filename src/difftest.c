#include <signal.h>
#include <stdlib.h>
#include <sys/prctl.h>
#include <unistd.h>

#include "qemu.h"
#include "reg.h"
#include "dut.h"

void print_qemu_registers(qemu_regs_t *regs, bool wpc) {
  if (wpc) eprintf("$pc:%08x\n", regs->pc - 4);
  eprintf(
      "$cs:0x%08x  $hi:0x%08x  $lo:0x%08x  $sr:0x%08x\n",
      regs->cause, regs->hi, regs->lo, regs->sr);
  eprintf(
      "$0 :0x%08x  $at:0x%08x  $v0:0x%08x  $v1:0x%08x\n",
      regs->gpr[0], regs->gpr[1], regs->gpr[2],
      regs->gpr[3]);
  eprintf(
      "$a0:0x%08x  $a1:0x%08x  $a2:0x%08x  $a3:0x%08x\n",
      regs->gpr[4], regs->gpr[5], regs->gpr[6],
      regs->gpr[7]);
  eprintf(
      "$t0:0x%08x  $t1:0x%08x  $t2:0x%08x  $t3:0x%08x\n",
      regs->gpr[8], regs->gpr[9], regs->gpr[10],
      regs->gpr[11]);
  eprintf(
      "$t4:0x%08x  $t5:0x%08x  $t6:0x%08x  $t7:0x%08x\n",
      regs->gpr[12], regs->gpr[13], regs->gpr[14],
      regs->gpr[15]);
  eprintf(
      "$s0:0x%08x  $s1:0x%08x  $s2:0x%08x  $s3:0x%08x\n",
      regs->gpr[16], regs->gpr[17], regs->gpr[18],
      regs->gpr[19]);
  eprintf(
      "$s4:0x%08x  $s5:0x%08x  $s6:0x%08x  $s7:0x%08x\n",
      regs->gpr[20], regs->gpr[21], regs->gpr[22],
      regs->gpr[23]);
  eprintf(
      "$t8:0x%08x  $t9:0x%08x  $k0:0x%08x  $k1:0x%08x\n",
      regs->gpr[24], regs->gpr[25], regs->gpr[26],
      regs->gpr[27]);
  eprintf(
      "$gp:0x%08x  $sp:0x%08x  $fp:0x%08x  $ra:0x%08x\n",
      regs->gpr[28], regs->gpr[29], regs->gpr[30],
      regs->gpr[31]);
}

bool inst_is_branch(Inst inst) {
  if (0x2 <= inst.op && inst.op <= 0x7) return true;
  if (0x14 <= inst.op && inst.op <= 0x17) return true;

  if (inst.op == 0x00) { // special table
    if (inst.func == 0x08 || inst.func == 0x9) return true;
    return false;
  }

  if (inst.op == 0x01) { // regimm table
    if (0x00 <= inst.rt && inst.rt <= 0x03) return true;
    if (0x10 <= inst.rt && inst.rt <= 0x13) return true;
    return false;
  }

  return false;
}

bool inst_is_mfc0(Inst inst) {
  if (inst.op == 0x10 && inst.rs == 0x0 && inst.shamt == 0)
    return true;
  return false;
}

bool inst_is_load_mmio(Inst inst, qemu_regs_t *regs) {
  if (inst.op == 0x23) {
    uint32_t addr = regs->gpr[inst.rs] + inst.simm;
    return 0xA0000000 <= addr && addr < 0xC0000000;
  }
  return false;
}

bool inst_is_store_mmio(Inst inst, qemu_regs_t *regs) {
  if (inst.op == 0x2b) {
    uint32_t addr = regs->gpr[inst.rs] + inst.simm;
    return 0xA0000000 <= addr && addr < 0xC0000000;
  }
  return false;
}

bool inst_is_mmio(Inst inst, qemu_regs_t *regs) {
  return inst_is_load_mmio(inst, regs) ||
         inst_is_store_mmio(inst, regs);
}

bool pc_in_ex_entry(uint32_t pc) {
  return pc == 0xbfc00200 || pc == 0xbfc00380 ||
         pc == 0xbfc00400 || pc == 0x80000000 ||
         pc == 0x80000180 || pc == 0x80000200;
}

void difftest_start_qemu(int port, int ppid) {
  // install a parent death signal in the child
  int r = prctl(PR_SET_PDEATHSIG, SIGTERM);
  if (r == -1) { panic("prctl error"); }

  if (getppid() != ppid) { panic("parent has died"); }

  close(0); // close STDIN

  extern char *symbol_file;
  qemu_start(symbol_file, port);    // start qemu in single-step mode and stub gdb
}

// #define DiffAssert(cond, fmt, ...)                        \
//   do {                                                    \
//     if (!(cond)) {                                        \
//       nemu_epilogue();                                    \
//       eprintf("nemu: %s:%d: %s: Assertion `%s' failed\n", \
//           __FILE__, __LINE__, __func__, #cond);           \
//       eprintf("\e[1;31mmessage: " fmt "\e[0m\n",          \
//           ##__VA_ARGS__);                                 \
//       difftest_finish_qemu(conn);                         \
//     }                                                     \
//   } while (0)

void __attribute__((noinline))
difftest_finish_qemu(qemu_conn_t *conn) {
  for (int i = 0; i < 2; i++) {
    qemu_regs_t regs = {0};
    qemu_single_step(conn);
    qemu_getregs(conn, &regs);
    print_qemu_registers(&regs, true);
  }
  abort();
}

bool in_sync_addr(diff_pcs *dut_pcs) {
  static const int alen = 2;
  static uint32_t mfc0_count_pcs[alen] = { 0x800058ec, 0x80024b7c };
  for (int i = 0; i < alen; i++) {
    for (int j = 0; j < 3; j++) {
      if (dut_pcs->mycpu_pcs[j] == mfc0_count_pcs[i]) return true;
    }
  }
  return false;
}

bool difftest_regs(qemu_regs_t *regs, qemu_regs_t *dut_regs, diff_pcs *dut_pcs) {
    const char *alias[32] = {"0","at","v0","v1",
    "a0","a1","a2","a3",
    "t0","t1","t2","t3",
    "t4","t5","t6","t7",
    "s0","s1","s2","s3",
    "s4","s5","s6","s7",
    "t8","t9","k0","k1",
    "gp","sp","fp","ra"};
    // if (regs->pc - 4 != dut_pcs->mycpu_pcs[0] && regs->pc - 4 != dut_pcs->mycpu_pcs[1] && regs->pc - 4 != dut_pcs->mycpu_pcs[2]) {
    //   printf("  |  at DS [%x %x %x]", dut_pcs->mycpu_pcs[0], dut_pcs->mycpu_pcs[1], dut_pcs->mycpu_pcs[2]);
    // }
    static uint32_t last_3_qpcs[3] = { 0 };
    for (int i = 0; i < 32; i++) {
        if (regs->gpr[i] != dut_regs->gpr[i]) {
          if (in_sync_addr(dut_pcs)) {
            dut_sync_reg(i, regs->gpr[i], true);
          } else {
            sleep(2);
            for (int j = 0; j < 3; j++) {
              printf("QEMU PC at [0x%08x]\n", last_3_qpcs[j]);
            }
            printf("\x1B[31mError in $%s, QEMU %x, amipsel %x\x1B[37m\n", alias[i], regs->gpr[i], dut_regs->gpr[i]); 
            return false;
          }    
        }
    }
    last_3_qpcs[0] = last_3_qpcs[1];
    last_3_qpcs[1] = last_3_qpcs[2];
    last_3_qpcs[2] = regs->pc - 4;
    return true;
}

void difftest_body(int port) {
  qemu_regs_t regs = {0};
  qemu_regs_t dut_regs = {0};
  diff_pcs dut_pcs = {0};
  extern char *symbol_file;
  uint32_t bc = 0;
  uint64_t duts = 0;

  static int ugly_cnt = 0;
  static long bypass = 500000;

  qemu_conn_t *conn = qemu_connect(port);

  extern vaddr_t elf_entry;
  regs.pc = elf_entry;
  qemu_setregs(conn, &regs);
  qemu_break(conn, elf_entry);
  qemu_continue(conn);
  qemu_remove_breakpoint(conn, elf_entry);
  qemu_setregs(conn, &regs);

  dut_reset(10, symbol_file);
  dut_sync_reg(0, 0, false);

  // while (1) {
  //   dut_step(1);
  // }

  while (1) {
    dut_step(1);
    bc = 0;
    dut_sync_reg(0, 0, false);
    while(dut_commit() == 0) {
        dut_step(1);
        // duts++;
        // if (duts % 10000 == 0) {
        //   printf("dut step %d\n", duts);
        // }
        bc++;
        if (bc > 2048 * 8) {
          printf("Too many bubbles.\n");break;
        }
    }
    if (bc > 2048 * 8) {
      break;
    }
    for (int i = 0; i < dut_commit(); i++) {
      // printf("\n");
      qemu_single_step(conn);
      // printf("PC at [%08x]", regs.pc - 4);
    }
    qemu_getregs(conn, &regs);
    // ugly patch for difftest
//    if (regs.pc == 0x80024b50) {
//      qemu_single_step(conn);
//      qemu_getregs(conn, &regs);
//    } else if (regs.pc == 0x80024b54) {
//      qemu_single_step(conn);
//      qemu_getregs(conn, &regs);
//    }

    qemu_getregs(conn, &regs);
    dut_getregs(&dut_regs);
    dut_getpcs(&dut_pcs);
    if (!difftest_regs(&regs, &dut_regs, &dut_pcs)) {
        sleep(2);
        printf("\nQEMU\n");
        print_qemu_registers(&regs, true);
        printf("\nDUT\n");
        for (int i = 0; i < 3; i++) {
            printf("$pc_%d:%08x  ", i, dut_pcs.mycpu_pcs[i]);
        }
        printf("\n");
        print_qemu_registers(&dut_regs, false);
        printf("\n");
        break;
    }
  }

  qemu_disconnect(conn);
}

void difftest() {
  int port = 1234;
  int ppid = getpid();

  printf("Welcome to mipsel differential test with QEMU!\n");

  if (fork() != 0) {    // child process
    difftest_body(port);
  } else {              // parent process
    difftest_start_qemu(port, ppid);
  }
}