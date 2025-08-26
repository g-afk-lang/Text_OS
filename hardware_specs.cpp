#include "kernel.h"
#include "terminal_hooks.h"
#include "terminal_io.h"
#include "iostream_wrapper.h"
#include "interrupts.h"
 
 /* Define constants for MSR access */
 #define IA32_EFER           0xC0000080  // Extended Feature Enable Register
 #define MSR_SYSCFG          0xC0010010  // System Configuration
 #define MSR_HWCR            0xC0010015  // Hardware Configuration
 #define MSR_TOP_MEM         0xC001001A  // Top of Memory
 #define MSR_TOP_MEM2        0xC001001D  // Top of Memory 2
 #define MSR_PSTATE_CTRL     0xC0010062  // P-state Control
 #define MSR_PSTATE_STATUS   0xC0010063  // P-state Status
 #define MSR_PSTATE0         0xC0010064  // P-state 0
 #define MSR_CPUID_FEATURES  0xC0011005  // CPUID Features
 

char input[1];
 /* MSR read function */
 uint64_t rdmsr(uint32_t msr) {
     uint32_t low, high;
     __asm__ volatile ("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
     return ((uint64_t)high << 32) | low;
 }
 
 /* CPUID function */
 void cpuid(uint32_t leaf, uint32_t *eax, uint32_t *ebx, uint32_t *ecx, uint32_t *edx) {
     __asm__ volatile ("cpuid"
                      : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
                      : "a"(leaf));
 }
 
 /* Extended CPUID function with subleaf */
 void cpuid_ext(uint32_t leaf, uint32_t subleaf, uint32_t *eax, uint32_t *ebx, uint32_t *ecx, uint32_t *edx) {
     __asm__ volatile ("cpuid"
                      : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
                      : "a"(leaf), "c"(subleaf));
 }
 
 /* CPU information */
 void cmd_cpu() {
     uint32_t eax, ebx, ecx, edx;
     char vendor[13];
     
     cout << "CPU Information:\n";
     
     // Get vendor ID
     cpuid(0, &eax, &ebx, &ecx, &edx);
     
     // Construct vendor string
     ((uint32_t*)vendor)[0] = ebx;
     ((uint32_t*)vendor)[1] = edx;
     ((uint32_t*)vendor)[2] = ecx;
     vendor[12] = '\0';
     
     cout << "  Vendor: " << vendor << "\n";
     
     // Get processor brand string (if supported)
     cpuid(0x80000000, &eax, &ebx, &ecx, &edx);
     
     if (eax >= 0x80000004) {
         char brand[49];
         
         // Get brand string parts
         cpuid(0x80000002, &eax, &ebx, &ecx, &edx);
         ((uint32_t*)brand)[0] = eax;
         ((uint32_t*)brand)[1] = ebx;
         ((uint32_t*)brand)[2] = ecx;
         ((uint32_t*)brand)[3] = edx;
         
         cpuid(0x80000003, &eax, &ebx, &ecx, &edx);
         ((uint32_t*)brand)[4] = eax;
         ((uint32_t*)brand)[5] = ebx;
         ((uint32_t*)brand)[6] = ecx;
         ((uint32_t*)brand)[7] = edx;
         
         cpuid(0x80000004, &eax, &ebx, &ecx, &edx);
         ((uint32_t*)brand)[8] = eax;
         ((uint32_t*)brand)[9] = ebx;
         ((uint32_t*)brand)[10] = ecx;
         ((uint32_t*)brand)[11] = edx;
         
         brand[48] = '\0';
         
         cout << "  Model: " << brand << "\n";
     }
     
     // Get family/model/stepping
     cpuid(1, &eax, &ebx, &ecx, &edx);
     
     uint32_t stepping = eax & 0xF;
     uint32_t model = (eax >> 4) & 0xF;
     uint32_t family = (eax >> 8) & 0xF;
     uint32_t ext_model = (eax >> 16) & 0xF;
     uint32_t ext_family = (eax >> 20) & 0xFF;
     
     // AMD uses extended model and family fields
     if (family == 0xF) {
         family += ext_family;
     }
     
     model += (ext_model << 4);
     
     cout << "  Family: " << family << "\n";
     cout << "  Model: " << model << "\n";
     cout << "  Stepping: " << stepping << "\n";
 }
 
 /* Memory information */
 void cmd_memory() {
     cout << "Memory Information:\n";
     
     // Read top of memory registers
     uint64_t tom1 = rdmsr(MSR_TOP_MEM);
     uint64_t tom2 = rdmsr(MSR_TOP_MEM2);
     cout.dec();
     cout << "  Top of Memory 1: "  << int(tom1);
     cout.dec();
     cout << " (" << int(tom1 / (1024 * 1024)) << " MB)\n";
     
     if (tom2 > 0) {
         cout.dec();

         cout << "  Top of Memory 2: " << int(tom2);
         cout.dec();

         cout << " (" << int(tom2 / (1024 * 1024)) << " MB)\n";
     }
 }
 
 /* Cache information */
 void cmd_cache() {
     uint32_t eax, ebx, ecx, edx;
     
     cout << "Cache Information:\n";
     
     cpuid(0x80000000, &eax, &ebx, &ecx, &edx);
     
     if (eax >= 0x80000006) {
         // L1 Data Cache
         cpuid(0x80000005, &eax, &ebx, &ecx, &edx);
         uint32_t l1d_size = (ecx >> 24) & 0xFF;
         uint32_t l1d_assoc = (ecx >> 16) & 0xFF;
         uint32_t l1d_linesize = (ecx >> 0) & 0xFF;
         
         cout << "  L1 Data Cache: " << l1d_size << " KB, ";
         cout << l1d_assoc << "-way, ";
         cout << l1d_linesize << " byte line size\n";
         
         // L1 Instruction Cache
         uint32_t l1i_size = (edx >> 24) & 0xFF;
         uint32_t l1i_assoc = (edx >> 16) & 0xFF;
         uint32_t l1i_linesize = (edx >> 0) & 0xFF;
         
         cout << "  L1 Instruction Cache: " << l1i_size << " KB, ";
         cout << l1i_assoc << "-way, ";
         cout << l1i_linesize << " byte line size\n";
         
         // L2 Cache
         cpuid(0x80000006, &eax, &ebx, &ecx, &edx);
         uint32_t l2_size = (ecx >> 16) & 0xFFFF;
         uint32_t l2_assoc = (ecx >> 12) & 0xF;
         uint32_t l2_linesize = (ecx >> 0) & 0xFF;
         
         cout << "  L2 Cache: " << l2_size << " KB, ";
         
         // Convert associativity value
         switch (l2_assoc) {
             case 0: cout << "Disabled"; break;
             case 1: cout << "Direct mapped"; break;
             case 2: cout << "2-way"; break;
             case 4: cout << "4-way"; break;
             case 6: cout << "8-way"; break;
             case 8: cout << "16-way"; break;
             case 10: cout << "32-way"; break;
             case 11: cout << "48-way"; break;
             case 12: cout << "64-way"; break;
             case 13: cout << "96-way"; break;
             case 14: cout << "128-way"; break;
             case 15: cout << "Fully associative"; break;
             default: cout << "Unknown"; break;
         }
         
         cout << ", " << l2_linesize << " byte line size\n";
         
         // L3 Cache
         uint32_t l3_size_mb = (edx >> 18) & 0x3FFF;
         uint32_t l3_assoc = (edx >> 12) & 0xF;
         uint32_t l3_linesize = (edx >> 0) & 0xFF;
         
         if (l3_size_mb > 0) {
             uint32_t l3_size = l3_size_mb * 512; // Convert to KB
             
             cout << "  L3 Cache: " << l3_size << " KB, ";
             
             // Convert associativity value (same as L2)
             switch (l3_assoc) {
                 case 0: cout << "Disabled"; break;
                 case 1: cout << "Direct mapped"; break;
                 case 2: cout << "2-way"; break;
                 case 4: cout << "4-way"; break;
                 case 6: cout << "8-way"; break;
                 case 8: cout << "16-way"; break;
                 case 10: cout << "32-way"; break;
                 case 11: cout << "48-way"; break;
                 case 12: cout << "64-way"; break;
                 case 13: cout << "96-way"; break;
                 case 14: cout << "128-way"; break;
                 case 15: cout << "Fully associative"; break;
                 default: cout << "Unknown"; break;
             }
             
             cout << ", " << l3_linesize << " byte line size\n";
         }
     } else {
         cout << "  Extended cache information not supported\n";
     }
 }
 
 /* CPU topology information */
 void cmd_topology() {
     uint32_t eax, ebx, ecx, edx;
     
     cout << "CPU Topology:\n";
     
     // Check for extended topology leaf support
     cpuid(0, &eax, &ebx, &ecx, &edx);
     
     if (eax >= 0xB) {
         uint32_t level_type, logical_processors;
         
         // Level 0: SMT (Thread) level
         cpuid_ext(0xB, 0, &eax, &ebx, &ecx, &edx);
         logical_processors = ebx & 0xFFFF;
         level_type = ecx & 0xFF;
         
         if (level_type == 1) {
             cout << "  Threads per Core: " << logical_processors << "\n";
         }
         
         // Level 1: Core level
         cpuid_ext(0xB, 1, &eax, &ebx, &ecx, &edx);
         logical_processors = ebx & 0xFFFF;
         level_type = ecx & 0xFF;
         
         if (level_type == 2) {
             cout << "  Logical Processors: " << logical_processors << "\n";
         }
     } else {
         // Fallback for older CPUs
         cpuid(1, &eax, &ebx, &ecx, &edx);
         uint32_t logical_processors = (ebx >> 16) & 0xFF;
         
         cout << "  Logical Processors: " << logical_processors << "\n";
         
         // For core count, check AMD extended info
         cpuid(0x80000000, &eax, &ebx, &ecx, &edx);
         
         if (eax >= 0x80000008) {
             cpuid(0x80000008, &eax, &ebx, &ecx, &edx);
             uint32_t core_count = (ecx & 0xFF) + 1;
             
             cout << "  Core Count: " << core_count << "\n";
         }
     }
 }
 
 /* CPU features */
 void cmd_features() {
     uint32_t eax, ebx, ecx, edx;
     
     cout << "CPU Features:\n";
     
     // Standard features
     cpuid(1, &eax, &ebx, &ecx, &edx);
     
     cout << "  Standard Features:\n";
     
     if (edx & (1 << 0))  cout << "    FPU: Floating Point Unit\n";
     if (edx & (1 << 4))  cout << "    TSC: Time Stamp Counter\n";
     if (edx & (1 << 5))  cout << "    MSR: Model Specific Registers\n";
     if (edx & (1 << 6))  cout << "    PAE: Physical Address Extension\n";
     if (edx & (1 << 8))  cout << "    CX8: CMPXCHG8 Instruction\n";
     cout << "\nPress enter to continue\n\n";
     input[1];
     cin >> input;
     if (edx & (1 << 11)) cout << "    SEP: SYSENTER/SYSEXIT Instructions\n";
     if (edx & (1 << 15)) cout << "    CMOV: Conditional Move Instructions\n";
     if (edx & (1 << 19)) cout << "    CLFLUSH: CLFLUSH Instruction\n";
     if (edx & (1 << 23)) cout << "    MMX: MMX Technology\n";
     if (edx & (1 << 24)) cout << "    FXSR: FXSAVE/FXRSTOR\n";
     if (edx & (1 << 25)) cout << "    SSE: SSE Extensions\n";
     if (edx & (1 << 26)) cout << "    SSE2: SSE2 Extensions\n";
     if (edx & (1 << 28)) cout << "    HTT: Hyper-Threading Technology\n";

     cout << "\nPress enter to continue\n\n";
     cin >> input;

     if (ecx & (1 << 0))  cout << "    SSE3: SSE3 Extensions\n";
     if (ecx & (1 << 1))  cout << "    PCLMULQDQ: PCLMULQDQ Instruction\n";
     if (ecx & (1 << 9))  cout << "    SSSE3: SSSE3 Extensions\n";
     if (ecx & (1 << 12)) cout << "    FMA: FMA Extensions\n";
     if (ecx & (1 << 13)) cout << "    CX16: CMPXCHG16B Instruction\n";
     if (ecx & (1 << 19)) cout << "    SSE4.1: SSE4.1 Extensions\n";
     if (ecx & (1 << 20)) cout << "    SSE4.2: SSE4.2 Extensions\n";
     if (ecx & (1 << 21)) cout << "    x2APIC: x2APIC Support\n";
     cout << "\nPress enter to continue\n\n";

     cin >> input;
     if (ecx & (1 << 22)) cout << "    MOVBE: MOVBE Instruction\n";
     if (ecx & (1 << 23)) cout << "    POPCNT: POPCNT Instruction\n";
     if (ecx & (1 << 25)) cout << "    AES: AES Instruction Set\n";
     if (ecx & (1 << 26)) cout << "    XSAVE: XSAVE/XRSTOR Instructions\n";
     if (ecx & (1 << 28)) cout << "    AVX: AVX Support\n";
     if (ecx & (1 << 29)) cout << "    F16C: 16-bit FP Conversion\n";
     if (ecx & (1 << 30)) cout << "    RDRAND: RDRAND Instruction\n";
     
     // Extended features
     cpuid(0x80000000, &eax, &ebx, &ecx, &edx);
     
     if (eax >= 0x80000001) {
         cpuid(0x80000001, &eax, &ebx, &ecx, &edx);
         
         cout << "  Extended Features:\n";
         
         if (edx & (1 << 11)) cout << "    SYSCALL/SYSRET Instructions\n";
         if (edx & (1 << 20)) cout << "    NX: No-Execute Page Protection\n";
         if (edx & (1 << 26)) cout << "    Page1GB: 1GB Pages\n";
         if (edx & (1 << 27)) cout << "    RDTSCP: RDTSCP Instruction\n";
         if (edx & (1 << 29)) cout << "    LM: Long Mode (64-bit)\n";
         if (edx & (1 << 31)) cout << "    3DNow!: 3DNow! Instructions\n";
         cout << "\nPress enter to continue\n\n";
         cin >> input;
         if (ecx & (1 << 0))  cout << "    LAHF/SAHF: LAHF/SAHF in 64-bit\n";
         if (ecx & (1 << 1))  cout << "    CMP_Legacy: Core Multi-Processing Legacy\n";
         if (ecx & (1 << 2))  cout << "    SVM: Secure Virtual Machine\n";
         if (ecx & (1 << 3))  cout << "    ExtApicSpace: Extended APIC Space\n";
         if (ecx & (1 << 4))  cout << "    AltMovCr8: LOCK MOV CR0 means MOV CR8\n";
         if (ecx & (1 << 5))  cout << "    ABM: Advanced Bit Manipulation\n";
         if (ecx & (1 << 6))  cout << "    SSE4A: SSE4A Instructions\n";
         if (ecx & (1 << 7))  cout << "    MisAlignSse: Misaligned SSE Mode\n";

         cout << "\nPress enter to continue\n\n";

         cin >> input;
         if (ecx & (1 << 8))  cout << "    3DNowPrefetch: PREFETCH/PREFETCHW\n";
         if (ecx & (1 << 9))  cout << "    OSVW: OS Visible Workaround\n";
         if (ecx & (1 << 10)) cout << "    IBS: Instruction Based Sampling\n";
         if (ecx & (1 << 11)) cout << "    XOP: Extended Operations\n";
         if (ecx & (1 << 12)) cout << "    SKINIT: SKINIT/STGI Instructions\n";
         if (ecx & (1 << 13)) cout << "    WDT: Watchdog Timer\n";
         if (ecx & (1 << 15)) cout << "    LWP: Lightweight Profiling\n";
         if (ecx & (1 << 16)) cout << "    FMA4: 4-operand FMA\n";
         if (ecx & (1 << 19)) cout << "    NodeId: NodeId MSR\n";
         if (ecx & (1 << 21)) cout << "    TBM: Trailing Bit Manipulation\n";
     }
 }
 
 /* P-state information */
 void cmd_pstates() {
     cout << "Power Management (P-states) Information:\n";
     
     // Current P-state
     uint64_t pstate_status = rdmsr(MSR_PSTATE_STATUS);
     uint32_t cur_pstate = pstate_status & 0x7;
     
     cout << "  Current P-state: P" << cur_pstate << "\n";
     
     // P-state 0 information (highest performance)
     uint64_t pstate0 = rdmsr(MSR_PSTATE0);
     
     bool pstate_valid = (pstate0 >> 31) & 0x1;
     uint32_t cpu_fid = pstate0 & 0x3F;
     uint32_t cpu_did = (pstate0 >> 6) & 0x7;
     uint32_t cpu_vid = (pstate0 >> 14) & 0xFF;
     
     if (pstate_valid) {
         // Calculate frequency - this is a simplified calculation
         // The actual calculation depends on the CPU family/model
         double freq = 100.0 * (cpu_fid + 16) / (1 << cpu_did);
         
         cout << "  P0 (Max Performance):\n";
         cout << "    Frequency: " << int(freq) << " MHz\n";
         cout << "    Voltage ID: " << cpu_vid << "\n";
         cout << "    FID: " << cpu_fid << ", DID: " << cpu_did << "\n";
     } else {
         cout << "  P0 state not valid\n";
     }
     
     // Hardware configuration
     uint64_t hwcr = rdmsr(MSR_HWCR);
     bool cpb_disabled = (hwcr >> 25) & 0x1;
     
     cout << "  Core Performance Boost: " << (cpb_disabled ? "Disabled" : "Enabled") << "\n";
 }
 
 /* Run all commands for full hardware info */
 void cmd_full() {
     cmd_cpu();
     cout << "\n";
     cmd_memory();
     cout << "\n";
     cmd_cache();
     cout << "\n";
     cmd_topology();
     cout << "\n";
     cmd_features();
     cout << "\n";
     cmd_pstates();
 }