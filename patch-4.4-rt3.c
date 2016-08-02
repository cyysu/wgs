diff --git a/Documentation/hwlat_detector.txt b/Documentation/hwlat_detector.txt
new file mode 100644
index 000000000000..cb61516483d3
--- /dev/null
+++ b/Documentation/hwlat_detector.txt
@@ -0,0 +1,64 @@
+Introduction:
+-------------
+
+The module hwlat_detector is a special purpose kernel module that is used to
+detect large system latencies induced by the behavior of certain underlying
+hardware or firmware, independent of Linux itself. The code was developed
+originally to detect SMIs (System Management Interrupts) on x86 systems,
+however there is nothing x86 specific about this patchset. It was
+originally written for use by the "RT" patch since the Real Time
+kernel is highly latency sensitive.
+
+SMIs are usually not serviced by the Linux kernel, which typically does not
+even know that they are occuring. SMIs are instead are set up by BIOS code
+and are serviced by BIOS code, usually for "critical" events such as
+management of thermal sensors and fans. Sometimes though, SMIs are used for
+other tasks and those tasks can spend an inordinate amount of time in the
+handler (sometimes measured in milliseconds). Obviously this is a problem if
+you are trying to keep event service latencies down in the microsecond range.
+
+The hardware latency detector works by hogging all of the cpus for configurable
+amounts of time (by calling stop_machine()), polling the CPU Time Stamp Counter
+for some period, then looking for gaps in the TSC data. Any gap indicates a
+time when the polling was interrupted and since the machine is stopped and
+interrupts turned off the only thing that could do that would be an SMI.
+
+Note that the SMI detector should *NEVER* be used in a production environment.
+It is intended to be run manually to determine if the hardware platform has a
+problem with long system firmware service routines.
+
+Usage:
+------
+
+Loading the module hwlat_detector passing the parameter "enabled=1" (or by
+setting the "enable" entry in "hwlat_detector" debugfs toggled on) is the only
+step required to start the hwlat_detector. It is possible to redefine the
+threshold in microseconds (us) above which latency spikes will be taken
+into account (parameter "threshold=").
+
+Example:
+
+	# modprobe hwlat_detector enabled=1 threshold=100
+
+After the module is loaded, it creates a directory named "hwlat_detector" under
+the debugfs mountpoint, "/debug/hwlat_detector" for this text. It is necessary
+to have debugfs mounted, which might be on /sys/debug on your system.
+
+The /debug/hwlat_detector interface contains the following files:
+
+count			- number of latency spikes observed since last reset
+enable			- a global enable/disable toggle (0/1), resets count
+max			- maximum hardware latency actually observed (usecs)
+sample			- a pipe from which to read current raw sample data
+			  in the format <timestamp> <latency observed usecs>
+			  (can be opened O_NONBLOCK for a single sample)
+threshold		- minimum latency value to be considered (usecs)
+width			- time period to sample with CPUs held (usecs)
+			  must be less than the total window size (enforced)
+window			- total period of sampling, width being inside (usecs)
+
+By default we will set width to 500,000 and window to 1,000,000, meaning that
+we will sample every 1,000,000 usecs (1s) for 500,000 usecs (0.5s). If we
+observe any latencies that exceed the threshold (initially 100 usecs),
+then we write to a global sample ring buffer of 8K samples, which is
+consumed by reading from the "sample" (pipe) debugfs file interface.
diff --git a/Documentation/sysrq.txt b/Documentation/sysrq.txt
index 13f5619b2203..f64d075ba647 100644
--- a/Documentation/sysrq.txt
+++ b/Documentation/sysrq.txt
@@ -59,10 +59,17 @@ On PowerPC - Press 'ALT - Print Screen (or F13) - <command key>,
 On other - If you know of the key combos for other architectures, please
            let me know so I can add them to this section.
 
-On all -  write a character to /proc/sysrq-trigger.  e.g.:
-
+On all -  write a character to /proc/sysrq-trigger, e.g.:
 		echo t > /proc/sysrq-trigger
 
+On all - Enable network SysRq by writing a cookie to icmp_echo_sysrq, e.g.
+		echo 0x01020304 >/proc/sys/net/ipv4/icmp_echo_sysrq
+	 Send an ICMP echo request with this pattern plus the particular
+	 SysRq command key. Example:
+		# ping -c1 -s57 -p0102030468
+	 will trigger the SysRq-H (help) command.
+
+
 *  What are the 'command' keys?
 ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 'b'     - Will immediately reboot the system without syncing or unmounting
diff --git a/Documentation/trace/histograms.txt b/Documentation/trace/histograms.txt
new file mode 100644
index 000000000000..6f2aeabf7faa
--- /dev/null
+++ b/Documentation/trace/histograms.txt
@@ -0,0 +1,186 @@
+		Using the Linux Kernel Latency Histograms
+
+
+This document gives a short explanation how to enable, configure and use
+latency histograms. Latency histograms are primarily relevant in the
+context of real-time enabled kernels (CONFIG_PREEMPT/CONFIG_PREEMPT_RT)
+and are used in the quality management of the Linux real-time
+capabilities.
+
+
+* Purpose of latency histograms
+
+A latency histogram continuously accumulates the frequencies of latency
+data. There are two types of histograms
+- potential sources of latencies
+- effective latencies
+
+
+* Potential sources of latencies
+
+Potential sources of latencies are code segments where interrupts,
+preemption or both are disabled (aka critical sections). To create
+histograms of potential sources of latency, the kernel stores the time
+stamp at the start of a critical section, determines the time elapsed
+when the end of the section is reached, and increments the frequency
+counter of that latency value - irrespective of whether any concurrently
+running process is affected by latency or not.
+- Configuration items (in the Kernel hacking/Tracers submenu)
+  CONFIG_INTERRUPT_OFF_LATENCY
+  CONFIG_PREEMPT_OFF_LATENCY
+
+
+* Effective latencies
+
+Effective latencies are actually occuring during wakeup of a process. To
+determine effective latencies, the kernel stores the time stamp when a
+process is scheduled to be woken up, and determines the duration of the
+wakeup time shortly before control is passed over to this process. Note
+that the apparent latency in user space may be somewhat longer, since the
+process may be interrupted after control is passed over to it but before
+the execution in user space takes place. Simply measuring the interval
+between enqueuing and wakeup may also not appropriate in cases when a
+process is scheduled as a result of a timer expiration. The timer may have
+missed its deadline, e.g. due to disabled interrupts, but this latency
+would not be registered. Therefore, the offsets of missed timers are
+recorded in a separate histogram. If both wakeup latency and missed timer
+offsets are configured and enabled, a third histogram may be enabled that
+records the overall latency as a sum of the timer latency, if any, and the
+wakeup latency. This histogram is called "timerandwakeup".
+- Configuration items (in the Kernel hacking/Tracers submenu)
+  CONFIG_WAKEUP_LATENCY
+  CONFIG_MISSED_TIMER_OFSETS
+
+
+* Usage
+
+The interface to the administration of the latency histograms is located
+in the debugfs file system. To mount it, either enter
+
+mount -t sysfs nodev /sys
+mount -t debugfs nodev /sys/kernel/debug
+
+from shell command line level, or add
+
+nodev	/sys			sysfs	defaults	0 0
+nodev	/sys/kernel/debug	debugfs	defaults	0 0
+
+to the file /etc/fstab. All latency histogram related files are then
+available in the directory /sys/kernel/debug/tracing/latency_hist. A
+particular histogram type is enabled by writing non-zero to the related
+variable in the /sys/kernel/debug/tracing/latency_hist/enable directory.
+Select "preemptirqsoff" for the histograms of potential sources of
+latencies and "wakeup" for histograms of effective latencies etc. The
+histogram data - one per CPU - are available in the files
+
+/sys/kernel/debug/tracing/latency_hist/preemptoff/CPUx
+/sys/kernel/debug/tracing/latency_hist/irqsoff/CPUx
+/sys/kernel/debug/tracing/latency_hist/preemptirqsoff/CPUx
+/sys/kernel/debug/tracing/latency_hist/wakeup/CPUx
+/sys/kernel/debug/tracing/latency_hist/wakeup/sharedprio/CPUx
+/sys/kernel/debug/tracing/latency_hist/missed_timer_offsets/CPUx
+/sys/kernel/debug/tracing/latency_hist/timerandwakeup/CPUx
+
+The histograms are reset by writing non-zero to the file "reset" in a
+particular latency directory. To reset all latency data, use
+
+#!/bin/sh
+
+TRACINGDIR=/sys/kernel/debug/tracing
+HISTDIR=$TRACINGDIR/latency_hist
+
+if test -d $HISTDIR
+then
+  cd $HISTDIR
+  for i in `find . | grep /reset$`
+  do
+    echo 1 >$i
+  done
+fi
+
+
+* Data format
+
+Latency data are stored with a resolution of one microsecond. The
+maximum latency is 10,240 microseconds. The data are only valid, if the
+overflow register is empty. Every output line contains the latency in
+microseconds in the first row and the number of samples in the second
+row. To display only lines with a positive latency count, use, for
+example,
+
+grep -v " 0$" /sys/kernel/debug/tracing/latency_hist/preemptoff/CPU0
+
+#Minimum latency: 0 microseconds.
+#Average latency: 0 microseconds.
+#Maximum latency: 25 microseconds.
+#Total samples: 3104770694
+#There are 0 samples greater or equal than 10240 microseconds
+#usecs	         samples
+    0	      2984486876
+    1	        49843506
+    2	        58219047
+    3	         5348126
+    4	         2187960
+    5	         3388262
+    6	          959289
+    7	          208294
+    8	           40420
+    9	            4485
+   10	           14918
+   11	           18340
+   12	           25052
+   13	           19455
+   14	            5602
+   15	             969
+   16	              47
+   17	              18
+   18	              14
+   19	               1
+   20	               3
+   21	               2
+   22	               5
+   23	               2
+   25	               1
+
+
+* Wakeup latency of a selected process
+
+To only collect wakeup latency data of a particular process, write the
+PID of the requested process to
+
+/sys/kernel/debug/tracing/latency_hist/wakeup/pid
+
+PIDs are not considered, if this variable is set to 0.
+
+
+* Details of the process with the highest wakeup latency so far
+
+Selected data of the process that suffered from the highest wakeup
+latency that occurred in a particular CPU are available in the file
+
+/sys/kernel/debug/tracing/latency_hist/wakeup/max_latency-CPUx.
+
+In addition, other relevant system data at the time when the
+latency occurred are given.
+
+The format of the data is (all in one line):
+<PID> <Priority> <Latency> (<Timeroffset>) <Command> \
+<- <PID> <Priority> <Command> <Timestamp>
+
+The value of <Timeroffset> is only relevant in the combined timer
+and wakeup latency recording. In the wakeup recording, it is
+always 0, in the missed_timer_offsets recording, it is the same
+as <Latency>.
+
+When retrospectively searching for the origin of a latency and
+tracing was not enabled, it may be helpful to know the name and
+some basic data of the task that (finally) was switching to the
+late real-tlme task. In addition to the victim's data, also the
+data of the possible culprit are therefore displayed after the
+"<-" symbol.
+
+Finally, the timestamp of the time when the latency occurred
+in <seconds>.<microseconds> after the most recent system boot
+is provided.
+
+These data are also reset when the wakeup histogram is reset.
diff --git a/arch/Kconfig b/arch/Kconfig
index 4e949e58b192..81592b37a8f7 100644
--- a/arch/Kconfig
+++ b/arch/Kconfig
@@ -9,6 +9,7 @@ config OPROFILE
 	tristate "OProfile system profiling"
 	depends on PROFILING
 	depends on HAVE_OPROFILE
+	depends on !PREEMPT_RT_FULL
 	select RING_BUFFER
 	select RING_BUFFER_ALLOW_SWAP
 	help
diff --git a/arch/arm/Kconfig b/arch/arm/Kconfig
index 34e1569a11ee..79c4603e9453 100644
--- a/arch/arm/Kconfig
+++ b/arch/arm/Kconfig
@@ -33,7 +33,7 @@ config ARM
 	select HARDIRQS_SW_RESEND
 	select HAVE_ARCH_AUDITSYSCALL if (AEABI && !OABI_COMPAT)
 	select HAVE_ARCH_BITREVERSE if (CPU_32v7M || CPU_32v7) && !CPU_32v6
-	select HAVE_ARCH_JUMP_LABEL if !XIP_KERNEL && !CPU_ENDIAN_BE32
+	select HAVE_ARCH_JUMP_LABEL if !XIP_KERNEL && !CPU_ENDIAN_BE32 && !PREEMPT_RT_BASE
 	select HAVE_ARCH_KGDB if !CPU_ENDIAN_BE32
 	select HAVE_ARCH_SECCOMP_FILTER if (AEABI && !OABI_COMPAT)
 	select HAVE_ARCH_TRACEHOOK
@@ -68,6 +68,7 @@ config ARM
 	select HAVE_PERF_EVENTS
 	select HAVE_PERF_REGS
 	select HAVE_PERF_USER_STACK_DUMP
+	select HAVE_PREEMPT_LAZY
 	select HAVE_RCU_TABLE_FREE if (SMP && ARM_LPAE)
 	select HAVE_REGS_AND_STACK_ACCESS_API
 	select HAVE_SYSCALL_TRACEPOINTS
diff --git a/arch/arm/include/asm/switch_to.h b/arch/arm/include/asm/switch_to.h
index 12ebfcc1d539..c962084605bc 100644
--- a/arch/arm/include/asm/switch_to.h
+++ b/arch/arm/include/asm/switch_to.h
@@ -3,6 +3,13 @@
 
 #include <linux/thread_info.h>
 
+#if defined CONFIG_PREEMPT_RT_FULL && defined CONFIG_HIGHMEM
+void switch_kmaps(struct task_struct *prev_p, struct task_struct *next_p);
+#else
+static inline void
+switch_kmaps(struct task_struct *prev_p, struct task_struct *next_p) { }
+#endif
+
 /*
  * For v7 SMP cores running a preemptible kernel we may be pre-empted
  * during a TLB maintenance operation, so execute an inner-shareable dsb
@@ -25,6 +32,7 @@ extern struct task_struct *__switch_to(struct task_struct *, struct thread_info
 #define switch_to(prev,next,last)					\
 do {									\
 	__complete_pending_tlbi();					\
+	switch_kmaps(prev, next);					\
 	last = __switch_to(prev,task_thread_info(prev), task_thread_info(next));	\
 } while (0)
 
diff --git a/arch/arm/include/asm/thread_info.h b/arch/arm/include/asm/thread_info.h
index 776757d1604a..1f36a4eccc72 100644
--- a/arch/arm/include/asm/thread_info.h
+++ b/arch/arm/include/asm/thread_info.h
@@ -49,6 +49,7 @@ struct cpu_context_save {
 struct thread_info {
 	unsigned long		flags;		/* low level flags */
 	int			preempt_count;	/* 0 => preemptable, <0 => bug */
+	int			preempt_lazy_count; /* 0 => preemptable, <0 => bug */
 	mm_segment_t		addr_limit;	/* address limit */
 	struct task_struct	*task;		/* main task structure */
 	__u32			cpu;		/* cpu */
@@ -142,7 +143,8 @@ extern int vfp_restore_user_hwstate(struct user_vfp __user *,
 #define TIF_SYSCALL_TRACE	4	/* syscall trace active */
 #define TIF_SYSCALL_AUDIT	5	/* syscall auditing active */
 #define TIF_SYSCALL_TRACEPOINT	6	/* syscall tracepoint instrumentation */
-#define TIF_SECCOMP		7	/* seccomp syscall filtering active */
+#define TIF_SECCOMP		8	/* seccomp syscall filtering active */
+#define TIF_NEED_RESCHED_LAZY	7
 
 #define TIF_NOHZ		12	/* in adaptive nohz mode */
 #define TIF_USING_IWMMXT	17
@@ -152,6 +154,7 @@ extern int vfp_restore_user_hwstate(struct user_vfp __user *,
 #define _TIF_SIGPENDING		(1 << TIF_SIGPENDING)
 #define _TIF_NEED_RESCHED	(1 << TIF_NEED_RESCHED)
 #define _TIF_NOTIFY_RESUME	(1 << TIF_NOTIFY_RESUME)
+#define _TIF_NEED_RESCHED_LAZY	(1 << TIF_NEED_RESCHED_LAZY)
 #define _TIF_UPROBE		(1 << TIF_UPROBE)
 #define _TIF_SYSCALL_TRACE	(1 << TIF_SYSCALL_TRACE)
 #define _TIF_SYSCALL_AUDIT	(1 << TIF_SYSCALL_AUDIT)
@@ -167,7 +170,8 @@ extern int vfp_restore_user_hwstate(struct user_vfp __user *,
  * Change these and you break ASM code in entry-common.S
  */
 #define _TIF_WORK_MASK		(_TIF_NEED_RESCHED | _TIF_SIGPENDING | \
-				 _TIF_NOTIFY_RESUME | _TIF_UPROBE)
+				 _TIF_NOTIFY_RESUME | _TIF_UPROBE | \
+				 _TIF_NEED_RESCHED_LAZY)
 
 #endif /* __KERNEL__ */
 #endif /* __ASM_ARM_THREAD_INFO_H */
diff --git a/arch/arm/kernel/asm-offsets.c b/arch/arm/kernel/asm-offsets.c
index 871b8267d211..4dbe70de7318 100644
--- a/arch/arm/kernel/asm-offsets.c
+++ b/arch/arm/kernel/asm-offsets.c
@@ -65,6 +65,7 @@ int main(void)
   BLANK();
   DEFINE(TI_FLAGS,		offsetof(struct thread_info, flags));
   DEFINE(TI_PREEMPT,		offsetof(struct thread_info, preempt_count));
+  DEFINE(TI_PREEMPT_LAZY,	offsetof(struct thread_info, preempt_lazy_count));
   DEFINE(TI_ADDR_LIMIT,		offsetof(struct thread_info, addr_limit));
   DEFINE(TI_TASK,		offsetof(struct thread_info, task));
   DEFINE(TI_CPU,		offsetof(struct thread_info, cpu));
diff --git a/arch/arm/kernel/entry-armv.S b/arch/arm/kernel/entry-armv.S
index 3ce377f7251f..d66b1aef2083 100644
--- a/arch/arm/kernel/entry-armv.S
+++ b/arch/arm/kernel/entry-armv.S
@@ -215,11 +215,18 @@ ENDPROC(__dabt_svc)
 #ifdef CONFIG_PREEMPT
 	get_thread_info tsk
 	ldr	r8, [tsk, #TI_PREEMPT]		@ get preempt count
-	ldr	r0, [tsk, #TI_FLAGS]		@ get flags
 	teq	r8, #0				@ if preempt count != 0
+	bne	1f				@ return from exeption
+	ldr	r0, [tsk, #TI_FLAGS]		@ get flags
+	tst	r0, #_TIF_NEED_RESCHED		@ if NEED_RESCHED is set
+	blne	svc_preempt			@ preempt!
+
+	ldr	r8, [tsk, #TI_PREEMPT_LAZY]	@ get preempt lazy count
+	teq	r8, #0				@ if preempt lazy count != 0
 	movne	r0, #0				@ force flags to 0
-	tst	r0, #_TIF_NEED_RESCHED
+	tst	r0, #_TIF_NEED_RESCHED_LAZY
 	blne	svc_preempt
+1:
 #endif
 
 	svc_exit r5, irq = 1			@ return from exception
@@ -234,6 +241,8 @@ ENDPROC(__irq_svc)
 1:	bl	preempt_schedule_irq		@ irq en/disable is done inside
 	ldr	r0, [tsk, #TI_FLAGS]		@ get new tasks TI_FLAGS
 	tst	r0, #_TIF_NEED_RESCHED
+	bne	1b
+	tst	r0, #_TIF_NEED_RESCHED_LAZY
 	reteq	r8				@ go again
 	b	1b
 #endif
diff --git a/arch/arm/kernel/entry-common.S b/arch/arm/kernel/entry-common.S
index 30a7228eaceb..c3bd6cbfce4b 100644
--- a/arch/arm/kernel/entry-common.S
+++ b/arch/arm/kernel/entry-common.S
@@ -36,7 +36,9 @@
  UNWIND(.cantunwind	)
 	disable_irq_notrace			@ disable interrupts
 	ldr	r1, [tsk, #TI_FLAGS]		@ re-check for syscall tracing
-	tst	r1, #_TIF_SYSCALL_WORK | _TIF_WORK_MASK
+	tst	r1, #((_TIF_SYSCALL_WORK | _TIF_WORK_MASK) & ~_TIF_SECCOMP)
+	bne	fast_work_pending
+	tst	r1, #_TIF_SECCOMP
 	bne	fast_work_pending
 
 	/* perform architecture specific actions before user return */
@@ -62,8 +64,11 @@ ENDPROC(ret_fast_syscall)
 	str	r0, [sp, #S_R0 + S_OFF]!	@ save returned r0
 	disable_irq_notrace			@ disable interrupts
 	ldr	r1, [tsk, #TI_FLAGS]		@ re-check for syscall tracing
-	tst	r1, #_TIF_SYSCALL_WORK | _TIF_WORK_MASK
+	tst	r1, #((_TIF_SYSCALL_WORK | _TIF_WORK_MASK) & ~_TIF_SECCOMP)
+	bne 	do_slower_path
+	tst	r1, #_TIF_SECCOMP
 	beq	no_work_pending
+do_slower_path:
  UNWIND(.fnend		)
 ENDPROC(ret_fast_syscall)
 
diff --git a/arch/arm/kernel/process.c b/arch/arm/kernel/process.c
index 4adfb46e3ee9..15f1d94b47c5 100644
--- a/arch/arm/kernel/process.c
+++ b/arch/arm/kernel/process.c
@@ -319,6 +319,30 @@ unsigned long arch_randomize_brk(struct mm_struct *mm)
 }
 
 #ifdef CONFIG_MMU
+/*
+ * CONFIG_SPLIT_PTLOCK_CPUS results in a page->ptl lock.  If the lock is not
+ * initialized by pgtable_page_ctor() then a coredump of the vector page will
+ * fail.
+ */
+static int __init vectors_user_mapping_init_page(void)
+{
+	struct page *page;
+	unsigned long addr = 0xffff0000;
+	pgd_t *pgd;
+	pud_t *pud;
+	pmd_t *pmd;
+
+	pgd = pgd_offset_k(addr);
+	pud = pud_offset(pgd, addr);
+	pmd = pmd_offset(pud, addr);
+	page = pmd_page(*(pmd));
+
+	pgtable_page_ctor(page);
+
+	return 0;
+}
+late_initcall(vectors_user_mapping_init_page);
+
 #ifdef CONFIG_KUSER_HELPERS
 /*
  * The vectors page is always readable from user space for the
diff --git a/arch/arm/kernel/signal.c b/arch/arm/kernel/signal.c
index 7b8f2141427b..96541e00b74a 100644
--- a/arch/arm/kernel/signal.c
+++ b/arch/arm/kernel/signal.c
@@ -572,7 +572,8 @@ do_work_pending(struct pt_regs *regs, unsigned int thread_flags, int syscall)
 	 */
 	trace_hardirqs_off();
 	do {
-		if (likely(thread_flags & _TIF_NEED_RESCHED)) {
+		if (likely(thread_flags & (_TIF_NEED_RESCHED |
+					   _TIF_NEED_RESCHED_LAZY))) {
 			schedule();
 		} else {
 			if (unlikely(!user_mode(regs)))
diff --git a/arch/arm/kernel/smp.c b/arch/arm/kernel/smp.c
index b26361355dae..e5754e3b03c4 100644
--- a/arch/arm/kernel/smp.c
+++ b/arch/arm/kernel/smp.c
@@ -230,8 +230,6 @@ int __cpu_disable(void)
 	flush_cache_louis();
 	local_flush_tlb_all();
 
-	clear_tasks_mm_cpumask(cpu);
-
 	return 0;
 }
 
@@ -247,6 +245,9 @@ void __cpu_die(unsigned int cpu)
 		pr_err("CPU%u: cpu didn't die\n", cpu);
 		return;
 	}
+
+	clear_tasks_mm_cpumask(cpu);
+
 	pr_notice("CPU%u: shutdown\n", cpu);
 
 	/*
diff --git a/arch/arm/kernel/unwind.c b/arch/arm/kernel/unwind.c
index 0bee233fef9a..314cfb232a63 100644
--- a/arch/arm/kernel/unwind.c
+++ b/arch/arm/kernel/unwind.c
@@ -93,7 +93,7 @@ extern const struct unwind_idx __start_unwind_idx[];
 static const struct unwind_idx *__origin_unwind_idx;
 extern const struct unwind_idx __stop_unwind_idx[];
 
-static DEFINE_SPINLOCK(unwind_lock);
+static DEFINE_RAW_SPINLOCK(unwind_lock);
 static LIST_HEAD(unwind_tables);
 
 /* Convert a prel31 symbol to an absolute address */
@@ -201,7 +201,7 @@ static const struct unwind_idx *unwind_find_idx(unsigned long addr)
 		/* module unwind tables */
 		struct unwind_table *table;
 
-		spin_lock_irqsave(&unwind_lock, flags);
+		raw_spin_lock_irqsave(&unwind_lock, flags);
 		list_for_each_entry(table, &unwind_tables, list) {
 			if (addr >= table->begin_addr &&
 			    addr < table->end_addr) {
@@ -213,7 +213,7 @@ static const struct unwind_idx *unwind_find_idx(unsigned long addr)
 				break;
 			}
 		}
-		spin_unlock_irqrestore(&unwind_lock, flags);
+		raw_spin_unlock_irqrestore(&unwind_lock, flags);
 	}
 
 	pr_debug("%s: idx = %p\n", __func__, idx);
@@ -529,9 +529,9 @@ struct unwind_table *unwind_table_add(unsigned long start, unsigned long size,
 	tab->begin_addr = text_addr;
 	tab->end_addr = text_addr + text_size;
 
-	spin_lock_irqsave(&unwind_lock, flags);
+	raw_spin_lock_irqsave(&unwind_lock, flags);
 	list_add_tail(&tab->list, &unwind_tables);
-	spin_unlock_irqrestore(&unwind_lock, flags);
+	raw_spin_unlock_irqrestore(&unwind_lock, flags);
 
 	return tab;
 }
@@ -543,9 +543,9 @@ void unwind_table_del(struct unwind_table *tab)
 	if (!tab)
 		return;
 
-	spin_lock_irqsave(&unwind_lock, flags);
+	raw_spin_lock_irqsave(&unwind_lock, flags);
 	list_del(&tab->list);
-	spin_unlock_irqrestore(&unwind_lock, flags);
+	raw_spin_unlock_irqrestore(&unwind_lock, flags);
 
 	kfree(tab);
 }
diff --git a/arch/arm/kvm/arm.c b/arch/arm/kvm/arm.c
index e06fd299de08..4f5c42a0924c 100644
--- a/arch/arm/kvm/arm.c
+++ b/arch/arm/kvm/arm.c
@@ -498,18 +498,18 @@ static void kvm_arm_resume_guest(struct kvm *kvm)
 	struct kvm_vcpu *vcpu;
 
 	kvm_for_each_vcpu(i, vcpu, kvm) {
-		wait_queue_head_t *wq = kvm_arch_vcpu_wq(vcpu);
+		struct swait_head *wq = kvm_arch_vcpu_wq(vcpu);
 
 		vcpu->arch.pause = false;
-		wake_up_interruptible(wq);
+		swait_wake_interruptible(wq);
 	}
 }
 
 static void vcpu_sleep(struct kvm_vcpu *vcpu)
 {
-	wait_queue_head_t *wq = kvm_arch_vcpu_wq(vcpu);
+	 struct swait_head *wq = kvm_arch_vcpu_wq(vcpu);
 
-	wait_event_interruptible(*wq, ((!vcpu->arch.power_off) &&
+	swait_event_interruptible(*wq, ((!vcpu->arch.power_off) &&
 				       (!vcpu->arch.pause)));
 }
 
diff --git a/arch/arm/kvm/psci.c b/arch/arm/kvm/psci.c
index a9b3b905e661..08148c46c8d0 100644
--- a/arch/arm/kvm/psci.c
+++ b/arch/arm/kvm/psci.c
@@ -70,7 +70,7 @@ static unsigned long kvm_psci_vcpu_on(struct kvm_vcpu *source_vcpu)
 {
 	struct kvm *kvm = source_vcpu->kvm;
 	struct kvm_vcpu *vcpu = NULL;
-	wait_queue_head_t *wq;
+	struct swait_head *wq;
 	unsigned long cpu_id;
 	unsigned long context_id;
 	phys_addr_t target_pc;
@@ -119,7 +119,7 @@ static unsigned long kvm_psci_vcpu_on(struct kvm_vcpu *source_vcpu)
 	smp_mb();		/* Make sure the above is visible */
 
 	wq = kvm_arch_vcpu_wq(vcpu);
-	wake_up_interruptible(wq);
+	swait_wake_interruptible(wq);
 
 	return PSCI_RET_SUCCESS;
 }
diff --git a/arch/arm/mach-exynos/platsmp.c b/arch/arm/mach-exynos/platsmp.c
index 98a2c0cbb833..310dce500d3e 100644
--- a/arch/arm/mach-exynos/platsmp.c
+++ b/arch/arm/mach-exynos/platsmp.c
@@ -230,7 +230,7 @@ static void __iomem *scu_base_addr(void)
 	return (void __iomem *)(S5P_VA_SCU);
 }
 
-static DEFINE_SPINLOCK(boot_lock);
+static DEFINE_RAW_SPINLOCK(boot_lock);
 
 static void exynos_secondary_init(unsigned int cpu)
 {
@@ -243,8 +243,8 @@ static void exynos_secondary_init(unsigned int cpu)
 	/*
 	 * Synchronise with the boot thread.
 	 */
-	spin_lock(&boot_lock);
-	spin_unlock(&boot_lock);
+	raw_spin_lock(&boot_lock);
+	raw_spin_unlock(&boot_lock);
 }
 
 int exynos_set_boot_addr(u32 core_id, unsigned long boot_addr)
@@ -308,7 +308,7 @@ static int exynos_boot_secondary(unsigned int cpu, struct task_struct *idle)
 	 * Set synchronisation state between this boot processor
 	 * and the secondary one
 	 */
-	spin_lock(&boot_lock);
+	raw_spin_lock(&boot_lock);
 
 	/*
 	 * The secondary processor is waiting to be released from
@@ -335,7 +335,7 @@ static int exynos_boot_secondary(unsigned int cpu, struct task_struct *idle)
 
 		if (timeout == 0) {
 			printk(KERN_ERR "cpu1 power enable failed");
-			spin_unlock(&boot_lock);
+			raw_spin_unlock(&boot_lock);
 			return -ETIMEDOUT;
 		}
 	}
@@ -381,7 +381,7 @@ static int exynos_boot_secondary(unsigned int cpu, struct task_struct *idle)
 	 * calibrations, then wait for it to finish
 	 */
 fail:
-	spin_unlock(&boot_lock);
+	raw_spin_unlock(&boot_lock);
 
 	return pen_release != -1 ? ret : 0;
 }
diff --git a/arch/arm/mach-hisi/platmcpm.c b/arch/arm/mach-hisi/platmcpm.c
index b5f8f5ffda79..9753a84df9c4 100644
--- a/arch/arm/mach-hisi/platmcpm.c
+++ b/arch/arm/mach-hisi/platmcpm.c
@@ -61,7 +61,7 @@
 
 static void __iomem *sysctrl, *fabric;
 static int hip04_cpu_table[HIP04_MAX_CLUSTERS][HIP04_MAX_CPUS_PER_CLUSTER];
-static DEFINE_SPINLOCK(boot_lock);
+static DEFINE_RAW_SPINLOCK(boot_lock);
 static u32 fabric_phys_addr;
 /*
  * [0]: bootwrapper physical address
@@ -113,7 +113,7 @@ static int hip04_boot_secondary(unsigned int l_cpu, struct task_struct *idle)
 	if (cluster >= HIP04_MAX_CLUSTERS || cpu >= HIP04_MAX_CPUS_PER_CLUSTER)
 		return -EINVAL;
 
-	spin_lock_irq(&boot_lock);
+	raw_spin_lock_irq(&boot_lock);
 
 	if (hip04_cpu_table[cluster][cpu])
 		goto out;
@@ -147,7 +147,7 @@ static int hip04_boot_secondary(unsigned int l_cpu, struct task_struct *idle)
 
 out:
 	hip04_cpu_table[cluster][cpu]++;
-	spin_unlock_irq(&boot_lock);
+	raw_spin_unlock_irq(&boot_lock);
 
 	return 0;
 }
@@ -162,11 +162,11 @@ static void hip04_cpu_die(unsigned int l_cpu)
 	cpu = MPIDR_AFFINITY_LEVEL(mpidr, 0);
 	cluster = MPIDR_AFFINITY_LEVEL(mpidr, 1);
 
-	spin_lock(&boot_lock);
+	raw_spin_lock(&boot_lock);
 	hip04_cpu_table[cluster][cpu]--;
 	if (hip04_cpu_table[cluster][cpu] == 1) {
 		/* A power_up request went ahead of us. */
-		spin_unlock(&boot_lock);
+		raw_spin_unlock(&boot_lock);
 		return;
 	} else if (hip04_cpu_table[cluster][cpu] > 1) {
 		pr_err("Cluster %d CPU%d boots multiple times\n", cluster, cpu);
@@ -174,7 +174,7 @@ static void hip04_cpu_die(unsigned int l_cpu)
 	}
 
 	last_man = hip04_cluster_is_down(cluster);
-	spin_unlock(&boot_lock);
+	raw_spin_unlock(&boot_lock);
 	if (last_man) {
 		/* Since it's Cortex A15, disable L2 prefetching. */
 		asm volatile(
@@ -203,7 +203,7 @@ static int hip04_cpu_kill(unsigned int l_cpu)
 	       cpu >= HIP04_MAX_CPUS_PER_CLUSTER);
 
 	count = TIMEOUT_MSEC / POLL_MSEC;
-	spin_lock_irq(&boot_lock);
+	raw_spin_lock_irq(&boot_lock);
 	for (tries = 0; tries < count; tries++) {
 		if (hip04_cpu_table[cluster][cpu])
 			goto err;
@@ -211,10 +211,10 @@ static int hip04_cpu_kill(unsigned int l_cpu)
 		data = readl_relaxed(sysctrl + SC_CPU_RESET_STATUS(cluster));
 		if (data & CORE_WFI_STATUS(cpu))
 			break;
-		spin_unlock_irq(&boot_lock);
+		raw_spin_unlock_irq(&boot_lock);
 		/* Wait for clean L2 when the whole cluster is down. */
 		msleep(POLL_MSEC);
-		spin_lock_irq(&boot_lock);
+		raw_spin_lock_irq(&boot_lock);
 	}
 	if (tries >= count)
 		goto err;
@@ -231,10 +231,10 @@ static int hip04_cpu_kill(unsigned int l_cpu)
 		goto err;
 	if (hip04_cluster_is_down(cluster))
 		hip04_set_snoop_filter(cluster, 0);
-	spin_unlock_irq(&boot_lock);
+	raw_spin_unlock_irq(&boot_lock);
 	return 1;
 err:
-	spin_unlock_irq(&boot_lock);
+	raw_spin_unlock_irq(&boot_lock);
 	return 0;
 }
 #endif
diff --git a/arch/arm/mach-omap2/omap-smp.c b/arch/arm/mach-omap2/omap-smp.c
index 79e1f876d1c9..7e625c17f78e 100644
--- a/arch/arm/mach-omap2/omap-smp.c
+++ b/arch/arm/mach-omap2/omap-smp.c
@@ -43,7 +43,7 @@
 /* SCU base address */
 static void __iomem *scu_base;
 
-static DEFINE_SPINLOCK(boot_lock);
+static DEFINE_RAW_SPINLOCK(boot_lock);
 
 void __iomem *omap4_get_scu_base(void)
 {
@@ -74,8 +74,8 @@ static void omap4_secondary_init(unsigned int cpu)
 	/*
 	 * Synchronise with the boot thread.
 	 */
-	spin_lock(&boot_lock);
-	spin_unlock(&boot_lock);
+	raw_spin_lock(&boot_lock);
+	raw_spin_unlock(&boot_lock);
 }
 
 static int omap4_boot_secondary(unsigned int cpu, struct task_struct *idle)
@@ -89,7 +89,7 @@ static int omap4_boot_secondary(unsigned int cpu, struct task_struct *idle)
 	 * Set synchronisation state between this boot processor
 	 * and the secondary one
 	 */
-	spin_lock(&boot_lock);
+	raw_spin_lock(&boot_lock);
 
 	/*
 	 * Update the AuxCoreBoot0 with boot state for secondary core.
@@ -166,7 +166,7 @@ static int omap4_boot_secondary(unsigned int cpu, struct task_struct *idle)
 	 * Now the secondary core is starting up let it run its
 	 * calibrations, then wait for it to finish
 	 */
-	spin_unlock(&boot_lock);
+	raw_spin_unlock(&boot_lock);
 
 	return 0;
 }
diff --git a/arch/arm/mach-prima2/platsmp.c b/arch/arm/mach-prima2/platsmp.c
index e46c91094dde..dcb3ed0c26da 100644
--- a/arch/arm/mach-prima2/platsmp.c
+++ b/arch/arm/mach-prima2/platsmp.c
@@ -22,7 +22,7 @@
 
 static void __iomem *clk_base;
 
-static DEFINE_SPINLOCK(boot_lock);
+static DEFINE_RAW_SPINLOCK(boot_lock);
 
 static void sirfsoc_secondary_init(unsigned int cpu)
 {
@@ -36,8 +36,8 @@ static void sirfsoc_secondary_init(unsigned int cpu)
 	/*
 	 * Synchronise with the boot thread.
 	 */
-	spin_lock(&boot_lock);
-	spin_unlock(&boot_lock);
+	raw_spin_lock(&boot_lock);
+	raw_spin_unlock(&boot_lock);
 }
 
 static const struct of_device_id clk_ids[]  = {
@@ -75,7 +75,7 @@ static int sirfsoc_boot_secondary(unsigned int cpu, struct task_struct *idle)
 	/* make sure write buffer is drained */
 	mb();
 
-	spin_lock(&boot_lock);
+	raw_spin_lock(&boot_lock);
 
 	/*
 	 * The secondary processor is waiting to be released from
@@ -107,7 +107,7 @@ static int sirfsoc_boot_secondary(unsigned int cpu, struct task_struct *idle)
 	 * now the secondary core is starting up let it run its
 	 * calibrations, then wait for it to finish
 	 */
-	spin_unlock(&boot_lock);
+	raw_spin_unlock(&boot_lock);
 
 	return pen_release != -1 ? -ENOSYS : 0;
 }
diff --git a/arch/arm/mach-qcom/platsmp.c b/arch/arm/mach-qcom/platsmp.c
index 9b00123a315d..0a49fe1bc8cf 100644
--- a/arch/arm/mach-qcom/platsmp.c
+++ b/arch/arm/mach-qcom/platsmp.c
@@ -46,7 +46,7 @@
 
 extern void secondary_startup_arm(void);
 
-static DEFINE_SPINLOCK(boot_lock);
+static DEFINE_RAW_SPINLOCK(boot_lock);
 
 #ifdef CONFIG_HOTPLUG_CPU
 static void qcom_cpu_die(unsigned int cpu)
@@ -60,8 +60,8 @@ static void qcom_secondary_init(unsigned int cpu)
 	/*
 	 * Synchronise with the boot thread.
 	 */
-	spin_lock(&boot_lock);
-	spin_unlock(&boot_lock);
+	raw_spin_lock(&boot_lock);
+	raw_spin_unlock(&boot_lock);
 }
 
 static int scss_release_secondary(unsigned int cpu)
@@ -284,7 +284,7 @@ static int qcom_boot_secondary(unsigned int cpu, int (*func)(unsigned int))
 	 * set synchronisation state between this boot processor
 	 * and the secondary one
 	 */
-	spin_lock(&boot_lock);
+	raw_spin_lock(&boot_lock);
 
 	/*
 	 * Send the secondary CPU a soft interrupt, thereby causing
@@ -297,7 +297,7 @@ static int qcom_boot_secondary(unsigned int cpu, int (*func)(unsigned int))
 	 * now the secondary core is starting up let it run its
 	 * calibrations, then wait for it to finish
 	 */
-	spin_unlock(&boot_lock);
+	raw_spin_unlock(&boot_lock);
 
 	return ret;
 }
diff --git a/arch/arm/mach-spear/platsmp.c b/arch/arm/mach-spear/platsmp.c
index fd4297713d67..b0553b2c2d53 100644
--- a/arch/arm/mach-spear/platsmp.c
+++ b/arch/arm/mach-spear/platsmp.c
@@ -32,7 +32,7 @@ static void write_pen_release(int val)
 	sync_cache_w(&pen_release);
 }
 
-static DEFINE_SPINLOCK(boot_lock);
+static DEFINE_RAW_SPINLOCK(boot_lock);
 
 static void __iomem *scu_base = IOMEM(VA_SCU_BASE);
 
@@ -47,8 +47,8 @@ static void spear13xx_secondary_init(unsigned int cpu)
 	/*
 	 * Synchronise with the boot thread.
 	 */
-	spin_lock(&boot_lock);
-	spin_unlock(&boot_lock);
+	raw_spin_lock(&boot_lock);
+	raw_spin_unlock(&boot_lock);
 }
 
 static int spear13xx_boot_secondary(unsigned int cpu, struct task_struct *idle)
@@ -59,7 +59,7 @@ static int spear13xx_boot_secondary(unsigned int cpu, struct task_struct *idle)
 	 * set synchronisation state between this boot processor
 	 * and the secondary one
 	 */
-	spin_lock(&boot_lock);
+	raw_spin_lock(&boot_lock);
 
 	/*
 	 * The secondary processor is waiting to be released from
@@ -84,7 +84,7 @@ static int spear13xx_boot_secondary(unsigned int cpu, struct task_struct *idle)
 	 * now the secondary core is starting up let it run its
 	 * calibrations, then wait for it to finish
 	 */
-	spin_unlock(&boot_lock);
+	raw_spin_unlock(&boot_lock);
 
 	return pen_release != -1 ? -ENOSYS : 0;
 }
diff --git a/arch/arm/mach-sti/platsmp.c b/arch/arm/mach-sti/platsmp.c
index c4ad6eae67fa..e830b20b212f 100644
--- a/arch/arm/mach-sti/platsmp.c
+++ b/arch/arm/mach-sti/platsmp.c
@@ -35,7 +35,7 @@ static void write_pen_release(int val)
 	sync_cache_w(&pen_release);
 }
 
-static DEFINE_SPINLOCK(boot_lock);
+static DEFINE_RAW_SPINLOCK(boot_lock);
 
 static void sti_secondary_init(unsigned int cpu)
 {
@@ -48,8 +48,8 @@ static void sti_secondary_init(unsigned int cpu)
 	/*
 	 * Synchronise with the boot thread.
 	 */
-	spin_lock(&boot_lock);
-	spin_unlock(&boot_lock);
+	raw_spin_lock(&boot_lock);
+	raw_spin_unlock(&boot_lock);
 }
 
 static int sti_boot_secondary(unsigned int cpu, struct task_struct *idle)
@@ -60,7 +60,7 @@ static int sti_boot_secondary(unsigned int cpu, struct task_struct *idle)
 	 * set synchronisation state between this boot processor
 	 * and the secondary one
 	 */
-	spin_lock(&boot_lock);
+	raw_spin_lock(&boot_lock);
 
 	/*
 	 * The secondary processor is waiting to be released from
@@ -91,7 +91,7 @@ static int sti_boot_secondary(unsigned int cpu, struct task_struct *idle)
 	 * now the secondary core is starting up let it run its
 	 * calibrations, then wait for it to finish
 	 */
-	spin_unlock(&boot_lock);
+	raw_spin_unlock(&boot_lock);
 
 	return pen_release != -1 ? -ENOSYS : 0;
 }
diff --git a/arch/arm/mm/fault.c b/arch/arm/mm/fault.c
index daafcf121ce0..b8aa1e9ee8ee 100644
--- a/arch/arm/mm/fault.c
+++ b/arch/arm/mm/fault.c
@@ -430,6 +430,9 @@ do_translation_fault(unsigned long addr, unsigned int fsr,
 	if (addr < TASK_SIZE)
 		return do_page_fault(addr, fsr, regs);
 
+	if (interrupts_enabled(regs))
+		local_irq_enable();
+
 	if (user_mode(regs))
 		goto bad_area;
 
@@ -497,6 +500,9 @@ do_translation_fault(unsigned long addr, unsigned int fsr,
 static int
 do_sect_fault(unsigned long addr, unsigned int fsr, struct pt_regs *regs)
 {
+	if (interrupts_enabled(regs))
+		local_irq_enable();
+
 	do_bad_area(addr, fsr, regs);
 	return 0;
 }
diff --git a/arch/arm/mm/highmem.c b/arch/arm/mm/highmem.c
index d02f8187b1cc..542692dbd40a 100644
--- a/arch/arm/mm/highmem.c
+++ b/arch/arm/mm/highmem.c
@@ -34,6 +34,11 @@ static inline pte_t get_fixmap_pte(unsigned long vaddr)
 	return *ptep;
 }
 
+static unsigned int fixmap_idx(int type)
+{
+	return FIX_KMAP_BEGIN + type + KM_TYPE_NR * smp_processor_id();
+}
+
 void *kmap(struct page *page)
 {
 	might_sleep();
@@ -54,12 +59,13 @@ EXPORT_SYMBOL(kunmap);
 
 void *kmap_atomic(struct page *page)
 {
+	pte_t pte = mk_pte(page, kmap_prot);
 	unsigned int idx;
 	unsigned long vaddr;
 	void *kmap;
 	int type;
 
-	preempt_disable();
+	preempt_disable_nort();
 	pagefault_disable();
 	if (!PageHighMem(page))
 		return page_address(page);
@@ -79,7 +85,7 @@ void *kmap_atomic(struct page *page)
 
 	type = kmap_atomic_idx_push();
 
-	idx = FIX_KMAP_BEGIN + type + KM_TYPE_NR * smp_processor_id();
+	idx = fixmap_idx(type);
 	vaddr = __fix_to_virt(idx);
 #ifdef CONFIG_DEBUG_HIGHMEM
 	/*
@@ -93,7 +99,10 @@ void *kmap_atomic(struct page *page)
 	 * in place, so the contained TLB flush ensures the TLB is updated
 	 * with the new mapping.
 	 */
-	set_fixmap_pte(idx, mk_pte(page, kmap_prot));
+#ifdef CONFIG_PREEMPT_RT_FULL
+	current->kmap_pte[type] = pte;
+#endif
+	set_fixmap_pte(idx, pte);
 
 	return (void *)vaddr;
 }
@@ -106,44 +115,75 @@ void __kunmap_atomic(void *kvaddr)
 
 	if (kvaddr >= (void *)FIXADDR_START) {
 		type = kmap_atomic_idx();
-		idx = FIX_KMAP_BEGIN + type + KM_TYPE_NR * smp_processor_id();
+		idx = fixmap_idx(type);
 
 		if (cache_is_vivt())
 			__cpuc_flush_dcache_area((void *)vaddr, PAGE_SIZE);
+#ifdef CONFIG_PREEMPT_RT_FULL
+		current->kmap_pte[type] = __pte(0);
+#endif
 #ifdef CONFIG_DEBUG_HIGHMEM
 		BUG_ON(vaddr != __fix_to_virt(idx));
-		set_fixmap_pte(idx, __pte(0));
 #else
 		(void) idx;  /* to kill a warning */
 #endif
+		set_fixmap_pte(idx, __pte(0));
 		kmap_atomic_idx_pop();
 	} else if (vaddr >= PKMAP_ADDR(0) && vaddr < PKMAP_ADDR(LAST_PKMAP)) {
 		/* this address was obtained through kmap_high_get() */
 		kunmap_high(pte_page(pkmap_page_table[PKMAP_NR(vaddr)]));
 	}
 	pagefault_enable();
-	preempt_enable();
+	preempt_enable_nort();
 }
 EXPORT_SYMBOL(__kunmap_atomic);
 
 void *kmap_atomic_pfn(unsigned long pfn)
 {
+	pte_t pte = pfn_pte(pfn, kmap_prot);
 	unsigned long vaddr;
 	int idx, type;
 	struct page *page = pfn_to_page(pfn);
 
-	preempt_disable();
+	preempt_disable_nort();
 	pagefault_disable();
 	if (!PageHighMem(page))
 		return page_address(page);
 
 	type = kmap_atomic_idx_push();
-	idx = FIX_KMAP_BEGIN + type + KM_TYPE_NR * smp_processor_id();
+	idx = fixmap_idx(type);
 	vaddr = __fix_to_virt(idx);
 #ifdef CONFIG_DEBUG_HIGHMEM
 	BUG_ON(!pte_none(get_fixmap_pte(vaddr)));
 #endif
-	set_fixmap_pte(idx, pfn_pte(pfn, kmap_prot));
+#ifdef CONFIG_PREEMPT_RT_FULL
+	current->kmap_pte[type] = pte;
+#endif
+	set_fixmap_pte(idx, pte);
 
 	return (void *)vaddr;
 }
+#if defined CONFIG_PREEMPT_RT_FULL
+void switch_kmaps(struct task_struct *prev_p, struct task_struct *next_p)
+{
+	int i;
+
+	/*
+	 * Clear @prev's kmap_atomic mappings
+	 */
+	for (i = 0; i < prev_p->kmap_idx; i++) {
+		int idx = fixmap_idx(i);
+
+		set_fixmap_pte(idx, __pte(0));
+	}
+	/*
+	 * Restore @next_p's kmap_atomic mappings
+	 */
+	for (i = 0; i < next_p->kmap_idx; i++) {
+		int idx = fixmap_idx(i);
+
+		if (!pte_none(next_p->kmap_pte[i]))
+			set_fixmap_pte(idx, next_p->kmap_pte[i]);
+	}
+}
+#endif
diff --git a/arch/arm/plat-versatile/platsmp.c b/arch/arm/plat-versatile/platsmp.c
index 53feb90c840c..b4a8d54fc3f3 100644
--- a/arch/arm/plat-versatile/platsmp.c
+++ b/arch/arm/plat-versatile/platsmp.c
@@ -30,7 +30,7 @@ static void write_pen_release(int val)
 	sync_cache_w(&pen_release);
 }
 
-static DEFINE_SPINLOCK(boot_lock);
+static DEFINE_RAW_SPINLOCK(boot_lock);
 
 void versatile_secondary_init(unsigned int cpu)
 {
@@ -43,8 +43,8 @@ void versatile_secondary_init(unsigned int cpu)
 	/*
 	 * Synchronise with the boot thread.
 	 */
-	spin_lock(&boot_lock);
-	spin_unlock(&boot_lock);
+	raw_spin_lock(&boot_lock);
+	raw_spin_unlock(&boot_lock);
 }
 
 int versatile_boot_secondary(unsigned int cpu, struct task_struct *idle)
@@ -55,7 +55,7 @@ int versatile_boot_secondary(unsigned int cpu, struct task_struct *idle)
 	 * Set synchronisation state between this boot processor
 	 * and the secondary one
 	 */
-	spin_lock(&boot_lock);
+	raw_spin_lock(&boot_lock);
 
 	/*
 	 * This is really belt and braces; we hold unintended secondary
@@ -85,7 +85,7 @@ int versatile_boot_secondary(unsigned int cpu, struct task_struct *idle)
 	 * now the secondary core is starting up let it run its
 	 * calibrations, then wait for it to finish
 	 */
-	spin_unlock(&boot_lock);
+	raw_spin_unlock(&boot_lock);
 
 	return pen_release != -1 ? -ENOSYS : 0;
 }
diff --git a/arch/arm64/Kconfig b/arch/arm64/Kconfig
index 871f21783866..1baa6537cf3f 100644
--- a/arch/arm64/Kconfig
+++ b/arch/arm64/Kconfig
@@ -76,6 +76,7 @@ config ARM64
 	select HAVE_PERF_REGS
 	select HAVE_PERF_USER_STACK_DUMP
 	select HAVE_RCU_TABLE_FREE
+	select HAVE_PREEMPT_LAZY
 	select HAVE_SYSCALL_TRACEPOINTS
 	select IOMMU_DMA if IOMMU_SUPPORT
 	select IRQ_DOMAIN
@@ -562,7 +563,7 @@ config XEN_DOM0
 
 config XEN
 	bool "Xen guest support on ARM64"
-	depends on ARM64 && OF
+	depends on ARM64 && OF && !PREEMPT_RT_FULL
 	select SWIOTLB_XEN
 	help
 	  Say Y if you want to run Linux in a Virtual Machine on Xen on ARM64.
diff --git a/arch/arm64/include/asm/thread_info.h b/arch/arm64/include/asm/thread_info.h
index 90c7ff233735..5f4e89fbc290 100644
--- a/arch/arm64/include/asm/thread_info.h
+++ b/arch/arm64/include/asm/thread_info.h
@@ -49,6 +49,7 @@ struct thread_info {
 	mm_segment_t		addr_limit;	/* address limit */
 	struct task_struct	*task;		/* main task structure */
 	int			preempt_count;	/* 0 => preemptable, <0 => bug */
+	int			preempt_lazy_count; /* 0 => preemptable, <0 => bug */
 	int			cpu;		/* cpu */
 };
 
@@ -103,6 +104,7 @@ static inline struct thread_info *current_thread_info(void)
 #define TIF_NEED_RESCHED	1
 #define TIF_NOTIFY_RESUME	2	/* callback before returning to user */
 #define TIF_FOREIGN_FPSTATE	3	/* CPU's FP state is not current's */
+#define TIF_NEED_RESCHED_LAZY	4
 #define TIF_NOHZ		7
 #define TIF_SYSCALL_TRACE	8
 #define TIF_SYSCALL_AUDIT	9
@@ -118,6 +120,7 @@ static inline struct thread_info *current_thread_info(void)
 #define _TIF_NEED_RESCHED	(1 << TIF_NEED_RESCHED)
 #define _TIF_NOTIFY_RESUME	(1 << TIF_NOTIFY_RESUME)
 #define _TIF_FOREIGN_FPSTATE	(1 << TIF_FOREIGN_FPSTATE)
+#define _TIF_NEED_RESCHED_LAZY	(1 << TIF_NEED_RESCHED_LAZY)
 #define _TIF_NOHZ		(1 << TIF_NOHZ)
 #define _TIF_SYSCALL_TRACE	(1 << TIF_SYSCALL_TRACE)
 #define _TIF_SYSCALL_AUDIT	(1 << TIF_SYSCALL_AUDIT)
@@ -126,7 +129,8 @@ static inline struct thread_info *current_thread_info(void)
 #define _TIF_32BIT		(1 << TIF_32BIT)
 
 #define _TIF_WORK_MASK		(_TIF_NEED_RESCHED | _TIF_SIGPENDING | \
-				 _TIF_NOTIFY_RESUME | _TIF_FOREIGN_FPSTATE)
+				 _TIF_NOTIFY_RESUME | _TIF_FOREIGN_FPSTATE | \
+				 _TIF_NEED_RESCHED_LAZY)
 
 #define _TIF_SYSCALL_WORK	(_TIF_SYSCALL_TRACE | _TIF_SYSCALL_AUDIT | \
 				 _TIF_SYSCALL_TRACEPOINT | _TIF_SECCOMP | \
diff --git a/arch/arm64/kernel/asm-offsets.c b/arch/arm64/kernel/asm-offsets.c
index 25de8b244961..c5038409e050 100644
--- a/arch/arm64/kernel/asm-offsets.c
+++ b/arch/arm64/kernel/asm-offsets.c
@@ -35,6 +35,7 @@ int main(void)
   BLANK();
   DEFINE(TI_FLAGS,		offsetof(struct thread_info, flags));
   DEFINE(TI_PREEMPT,		offsetof(struct thread_info, preempt_count));
+  DEFINE(TI_PREEMPT_LAZY,	offsetof(struct thread_info, preempt_lazy_count));
   DEFINE(TI_ADDR_LIMIT,		offsetof(struct thread_info, addr_limit));
   DEFINE(TI_TASK,		offsetof(struct thread_info, task));
   DEFINE(TI_CPU,		offsetof(struct thread_info, cpu));
diff --git a/arch/arm64/kernel/entry.S b/arch/arm64/kernel/entry.S
index 7ed3d75f6304..dd8fd31f8cba 100644
--- a/arch/arm64/kernel/entry.S
+++ b/arch/arm64/kernel/entry.S
@@ -363,11 +363,16 @@ ENDPROC(el1_sync)
 #ifdef CONFIG_PREEMPT
 	get_thread_info tsk
 	ldr	w24, [tsk, #TI_PREEMPT]		// get preempt count
-	cbnz	w24, 1f				// preempt count != 0
+	cbnz	w24, 2f				// preempt count != 0
 	ldr	x0, [tsk, #TI_FLAGS]		// get flags
-	tbz	x0, #TIF_NEED_RESCHED, 1f	// needs rescheduling?
-	bl	el1_preempt
+	tbnz	x0, #TIF_NEED_RESCHED, 1f	// needs rescheduling?
+
+	ldr	w24, [tsk, #TI_PREEMPT_LAZY]	// get preempt lazy count
+	cbnz	w24, 2f				// preempt lazy count != 0
+	tbz	x0, #TIF_NEED_RESCHED_LAZY, 2f	// needs rescheduling?
 1:
+	bl	el1_preempt
+2:
 #endif
 #ifdef CONFIG_TRACE_IRQFLAGS
 	bl	trace_hardirqs_on
@@ -381,6 +386,7 @@ ENDPROC(el1_irq)
 1:	bl	preempt_schedule_irq		// irq en/disable is done inside
 	ldr	x0, [tsk, #TI_FLAGS]		// get new tasks TI_FLAGS
 	tbnz	x0, #TIF_NEED_RESCHED, 1b	// needs rescheduling?
+	tbnz	x0, #TIF_NEED_RESCHED_LAZY, 1b	// needs rescheduling?
 	ret	x24
 #endif
 
@@ -625,6 +631,7 @@ ENDPROC(cpu_switch_to)
  */
 work_pending:
 	tbnz	x1, #TIF_NEED_RESCHED, work_resched
+	tbnz	x1, #TIF_NEED_RESCHED_LAZY, work_resched
 	/* TIF_SIGPENDING, TIF_NOTIFY_RESUME or TIF_FOREIGN_FPSTATE case */
 	ldr	x2, [sp, #S_PSTATE]
 	mov	x0, sp				// 'regs'
diff --git a/arch/mips/Kconfig b/arch/mips/Kconfig
index 71683a853372..0c0d06a4ae11 100644
--- a/arch/mips/Kconfig
+++ b/arch/mips/Kconfig
@@ -2409,7 +2409,7 @@ config CPU_R4400_WORKAROUNDS
 #
 config HIGHMEM
 	bool "High Memory Support"
-	depends on 32BIT && CPU_SUPPORTS_HIGHMEM && SYS_SUPPORTS_HIGHMEM && !CPU_MIPS32_3_5_EVA
+	depends on 32BIT && CPU_SUPPORTS_HIGHMEM && SYS_SUPPORTS_HIGHMEM && !CPU_MIPS32_3_5_EVA && !PREEMPT_RT_FULL
 
 config CPU_SUPPORTS_HIGHMEM
 	bool
diff --git a/arch/powerpc/Kconfig b/arch/powerpc/Kconfig
index db49e0d796b1..1d2be228661c 100644
--- a/arch/powerpc/Kconfig
+++ b/arch/powerpc/Kconfig
@@ -60,10 +60,11 @@ config LOCKDEP_SUPPORT
 
 config RWSEM_GENERIC_SPINLOCK
 	bool
+	default y if PREEMPT_RT_FULL
 
 config RWSEM_XCHGADD_ALGORITHM
 	bool
-	default y
+	default y if !PREEMPT_RT_FULL
 
 config GENERIC_LOCKBREAK
 	bool
@@ -141,6 +142,7 @@ config PPC
 	select ARCH_HAS_TICK_BROADCAST if GENERIC_CLOCKEVENTS_BROADCAST
 	select GENERIC_STRNCPY_FROM_USER
 	select GENERIC_STRNLEN_USER
+	select HAVE_PREEMPT_LAZY
 	select HAVE_MOD_ARCH_SPECIFIC
 	select MODULES_USE_ELF_RELA
 	select CLONE_BACKWARDS
@@ -319,7 +321,7 @@ menu "Kernel options"
 
 config HIGHMEM
 	bool "High memory support"
-	depends on PPC32
+	depends on PPC32 && !PREEMPT_RT_FULL
 
 source kernel/Kconfig.hz
 source kernel/Kconfig.preempt
diff --git a/arch/powerpc/include/asm/kvm_host.h b/arch/powerpc/include/asm/kvm_host.h
index cfa758c6b4f6..b439518f0253 100644
--- a/arch/powerpc/include/asm/kvm_host.h
+++ b/arch/powerpc/include/asm/kvm_host.h
@@ -286,7 +286,7 @@ struct kvmppc_vcore {
 	struct list_head runnable_threads;
 	struct list_head preempt_list;
 	spinlock_t lock;
-	wait_queue_head_t wq;
+	struct swait_head wq;
 	spinlock_t stoltb_lock;	/* protects stolen_tb and preempt_tb */
 	u64 stolen_tb;
 	u64 preempt_tb;
@@ -626,7 +626,7 @@ struct kvm_vcpu_arch {
 	u8 prodded;
 	u32 last_inst;
 
-	wait_queue_head_t *wqp;
+	struct swait_head *wqp;
 	struct kvmppc_vcore *vcore;
 	int ret;
 	int trap;
diff --git a/arch/powerpc/include/asm/thread_info.h b/arch/powerpc/include/asm/thread_info.h
index 7efee4a3240b..40e6fa1b85b2 100644
--- a/arch/powerpc/include/asm/thread_info.h
+++ b/arch/powerpc/include/asm/thread_info.h
@@ -42,6 +42,8 @@ struct thread_info {
 	int		cpu;			/* cpu we're on */
 	int		preempt_count;		/* 0 => preemptable,
 						   <0 => BUG */
+	int		preempt_lazy_count;	 /* 0 => preemptable,
+						   <0 => BUG */
 	unsigned long	local_flags;		/* private flags for thread */
 
 	/* low level flags - has atomic operations done on it */
@@ -82,8 +84,7 @@ static inline struct thread_info *current_thread_info(void)
 #define TIF_SYSCALL_TRACE	0	/* syscall trace active */
 #define TIF_SIGPENDING		1	/* signal pending */
 #define TIF_NEED_RESCHED	2	/* rescheduling necessary */
-#define TIF_POLLING_NRFLAG	3	/* true if poll_idle() is polling
-					   TIF_NEED_RESCHED */
+#define TIF_NEED_RESCHED_LAZY	3	/* lazy rescheduling necessary */
 #define TIF_32BIT		4	/* 32 bit binary */
 #define TIF_RESTORE_TM		5	/* need to restore TM FP/VEC/VSX */
 #define TIF_SYSCALL_AUDIT	7	/* syscall auditing active */
@@ -101,6 +102,8 @@ static inline struct thread_info *current_thread_info(void)
 #if defined(CONFIG_PPC64)
 #define TIF_ELF2ABI		18	/* function descriptors must die! */
 #endif
+#define TIF_POLLING_NRFLAG	19	/* true if poll_idle() is polling
+					   TIF_NEED_RESCHED */
 
 /* as above, but as bit values */
 #define _TIF_SYSCALL_TRACE	(1<<TIF_SYSCALL_TRACE)
@@ -119,14 +122,16 @@ static inline struct thread_info *current_thread_info(void)
 #define _TIF_SYSCALL_TRACEPOINT	(1<<TIF_SYSCALL_TRACEPOINT)
 #define _TIF_EMULATE_STACK_STORE	(1<<TIF_EMULATE_STACK_STORE)
 #define _TIF_NOHZ		(1<<TIF_NOHZ)
+#define _TIF_NEED_RESCHED_LAZY	(1<<TIF_NEED_RESCHED_LAZY)
 #define _TIF_SYSCALL_DOTRACE	(_TIF_SYSCALL_TRACE | _TIF_SYSCALL_AUDIT | \
 				 _TIF_SECCOMP | _TIF_SYSCALL_TRACEPOINT | \
 				 _TIF_NOHZ)
 
 #define _TIF_USER_WORK_MASK	(_TIF_SIGPENDING | _TIF_NEED_RESCHED | \
 				 _TIF_NOTIFY_RESUME | _TIF_UPROBE | \
-				 _TIF_RESTORE_TM)
+				 _TIF_RESTORE_TM | _TIF_NEED_RESCHED_LAZY)
 #define _TIF_PERSYSCALL_MASK	(_TIF_RESTOREALL|_TIF_NOERROR)
+#define _TIF_NEED_RESCHED_MASK	(_TIF_NEED_RESCHED | _TIF_NEED_RESCHED_LAZY)
 
 /* Bits in local_flags */
 /* Don't move TLF_NAPPING without adjusting the code in entry_32.S */
diff --git a/arch/powerpc/kernel/asm-offsets.c b/arch/powerpc/kernel/asm-offsets.c
index 221d584d089f..d6d0c59ef8ae 100644
--- a/arch/powerpc/kernel/asm-offsets.c
+++ b/arch/powerpc/kernel/asm-offsets.c
@@ -160,6 +160,7 @@ int main(void)
 	DEFINE(TI_FLAGS, offsetof(struct thread_info, flags));
 	DEFINE(TI_LOCAL_FLAGS, offsetof(struct thread_info, local_flags));
 	DEFINE(TI_PREEMPT, offsetof(struct thread_info, preempt_count));
+	DEFINE(TI_PREEMPT_LAZY, offsetof(struct thread_info, preempt_lazy_count));
 	DEFINE(TI_TASK, offsetof(struct thread_info, task));
 	DEFINE(TI_CPU, offsetof(struct thread_info, cpu));
 
diff --git a/arch/powerpc/kernel/entry_32.S b/arch/powerpc/kernel/entry_32.S
index 2405631e91a2..c21b4b42eaa0 100644
--- a/arch/powerpc/kernel/entry_32.S
+++ b/arch/powerpc/kernel/entry_32.S
@@ -818,7 +818,14 @@ user_exc_return:		/* r10 contains MSR_KERNEL here */
 	cmpwi	0,r0,0		/* if non-zero, just restore regs and return */
 	bne	restore
 	andi.	r8,r8,_TIF_NEED_RESCHED
+	bne+	1f
+	lwz	r0,TI_PREEMPT_LAZY(r9)
+	cmpwi	0,r0,0		/* if non-zero, just restore regs and return */
+	bne	restore
+	lwz	r0,TI_FLAGS(r9)
+	andi.	r0,r0,_TIF_NEED_RESCHED_LAZY
 	beq+	restore
+1:
 	lwz	r3,_MSR(r1)
 	andi.	r0,r3,MSR_EE	/* interrupts off? */
 	beq	restore		/* don't schedule if so */
@@ -829,11 +836,11 @@ user_exc_return:		/* r10 contains MSR_KERNEL here */
 	 */
 	bl	trace_hardirqs_off
 #endif
-1:	bl	preempt_schedule_irq
+2:	bl	preempt_schedule_irq
 	CURRENT_THREAD_INFO(r9, r1)
 	lwz	r3,TI_FLAGS(r9)
-	andi.	r0,r3,_TIF_NEED_RESCHED
-	bne-	1b
+	andi.	r0,r3,_TIF_NEED_RESCHED_MASK
+	bne-	2b
 #ifdef CONFIG_TRACE_IRQFLAGS
 	/* And now, to properly rebalance the above, we tell lockdep they
 	 * are being turned back on, which will happen when we return
@@ -1154,7 +1161,7 @@ END_FTR_SECTION_IFSET(CPU_FTR_NEED_PAIRED_STWCX)
 #endif /* !(CONFIG_4xx || CONFIG_BOOKE) */
 
 do_work:			/* r10 contains MSR_KERNEL here */
-	andi.	r0,r9,_TIF_NEED_RESCHED
+	andi.	r0,r9,_TIF_NEED_RESCHED_MASK
 	beq	do_user_signal
 
 do_resched:			/* r10 contains MSR_KERNEL here */
@@ -1175,7 +1182,7 @@ do_resched:			/* r10 contains MSR_KERNEL here */
 	MTMSRD(r10)		/* disable interrupts */
 	CURRENT_THREAD_INFO(r9, r1)
 	lwz	r9,TI_FLAGS(r9)
-	andi.	r0,r9,_TIF_NEED_RESCHED
+	andi.	r0,r9,_TIF_NEED_RESCHED_MASK
 	bne-	do_resched
 	andi.	r0,r9,_TIF_USER_WORK_MASK
 	beq	restore_user
diff --git a/arch/powerpc/kernel/entry_64.S b/arch/powerpc/kernel/entry_64.S
index a94f155db78e..5bb3148e592b 100644
--- a/arch/powerpc/kernel/entry_64.S
+++ b/arch/powerpc/kernel/entry_64.S
@@ -683,7 +683,7 @@ _GLOBAL(ret_from_except_lite)
 #else
 	beq	restore
 #endif
-1:	andi.	r0,r4,_TIF_NEED_RESCHED
+1:	andi.	r0,r4,_TIF_NEED_RESCHED_MASK
 	beq	2f
 	bl	restore_interrupts
 	SCHEDULE_USER
@@ -745,10 +745,18 @@ _GLOBAL(ret_from_except_lite)
 
 #ifdef CONFIG_PREEMPT
 	/* Check if we need to preempt */
-	andi.	r0,r4,_TIF_NEED_RESCHED
-	beq+	restore
-	/* Check that preempt_count() == 0 and interrupts are enabled */
 	lwz	r8,TI_PREEMPT(r9)
+	cmpwi	0,r8,0		/* if non-zero, just restore regs and return */
+	bne	restore
+	andi.	r0,r4,_TIF_NEED_RESCHED
+	bne+	check_count
+
+	andi.	r0,r4,_TIF_NEED_RESCHED_LAZY
+	beq+	restore
+	lwz	r8,TI_PREEMPT_LAZY(r9)
+
+	/* Check that preempt_count() == 0 and interrupts are enabled */
+check_count:
 	cmpwi	cr1,r8,0
 	ld	r0,SOFTE(r1)
 	cmpdi	r0,0
@@ -765,7 +773,7 @@ _GLOBAL(ret_from_except_lite)
 	/* Re-test flags and eventually loop */
 	CURRENT_THREAD_INFO(r9, r1)
 	ld	r4,TI_FLAGS(r9)
-	andi.	r0,r4,_TIF_NEED_RESCHED
+	andi.	r0,r4,_TIF_NEED_RESCHED_MASK
 	bne	1b
 
 	/*
diff --git a/arch/powerpc/kernel/irq.c b/arch/powerpc/kernel/irq.c
index 290559df1e8b..070afa6da35d 100644
--- a/arch/powerpc/kernel/irq.c
+++ b/arch/powerpc/kernel/irq.c
@@ -614,6 +614,7 @@ void irq_ctx_init(void)
 	}
 }
 
+#ifndef CONFIG_PREEMPT_RT_FULL
 void do_softirq_own_stack(void)
 {
 	struct thread_info *curtp, *irqtp;
@@ -631,6 +632,7 @@ void do_softirq_own_stack(void)
 	if (irqtp->flags)
 		set_bits(irqtp->flags, &curtp->flags);
 }
+#endif
 
 irq_hw_number_t virq_to_hw(unsigned int virq)
 {
diff --git a/arch/powerpc/kernel/misc_32.S b/arch/powerpc/kernel/misc_32.S
index ed3ab509faca..8b261416c070 100644
--- a/arch/powerpc/kernel/misc_32.S
+++ b/arch/powerpc/kernel/misc_32.S
@@ -40,6 +40,7 @@
  * We store the saved ksp_limit in the unused part
  * of the STACK_FRAME_OVERHEAD
  */
+#ifndef CONFIG_PREEMPT_RT_FULL
 _GLOBAL(call_do_softirq)
 	mflr	r0
 	stw	r0,4(r1)
@@ -56,6 +57,7 @@ _GLOBAL(call_do_softirq)
 	stw	r10,THREAD+KSP_LIMIT(r2)
 	mtlr	r0
 	blr
+#endif
 
 /*
  * void call_do_irq(struct pt_regs *regs, struct thread_info *irqtp);
diff --git a/arch/powerpc/kernel/misc_64.S b/arch/powerpc/kernel/misc_64.S
index db475d41b57a..96b7ef80e05d 100644
--- a/arch/powerpc/kernel/misc_64.S
+++ b/arch/powerpc/kernel/misc_64.S
@@ -30,6 +30,7 @@
 
 	.text
 
+#ifndef CONFIG_PREEMPT_RT_FULL
 _GLOBAL(call_do_softirq)
 	mflr	r0
 	std	r0,16(r1)
@@ -40,6 +41,7 @@ _GLOBAL(call_do_softirq)
 	ld	r0,16(r1)
 	mtlr	r0
 	blr
+#endif
 
 _GLOBAL(call_do_irq)
 	mflr	r0
diff --git a/arch/powerpc/kvm/Kconfig b/arch/powerpc/kvm/Kconfig
index c2024ac9d4e8..2303788da7e1 100644
--- a/arch/powerpc/kvm/Kconfig
+++ b/arch/powerpc/kvm/Kconfig
@@ -172,6 +172,7 @@ config KVM_E500MC
 config KVM_MPIC
 	bool "KVM in-kernel MPIC emulation"
 	depends on KVM && E500
+	depends on !PREEMPT_RT_FULL
 	select HAVE_KVM_IRQCHIP
 	select HAVE_KVM_IRQFD
 	select HAVE_KVM_IRQ_ROUTING
diff --git a/arch/powerpc/kvm/book3s_hv.c b/arch/powerpc/kvm/book3s_hv.c
index a7352b59e6f9..32be32b61d4c 100644
--- a/arch/powerpc/kvm/book3s_hv.c
+++ b/arch/powerpc/kvm/book3s_hv.c
@@ -114,11 +114,11 @@ static bool kvmppc_ipi_thread(int cpu)
 static void kvmppc_fast_vcpu_kick_hv(struct kvm_vcpu *vcpu)
 {
 	int cpu;
-	wait_queue_head_t *wqp;
+	struct swait_head *wqp;
 
 	wqp = kvm_arch_vcpu_wq(vcpu);
-	if (waitqueue_active(wqp)) {
-		wake_up_interruptible(wqp);
+	if (swaitqueue_active(wqp)) {
+		swait_wake_interruptible(wqp);
 		++vcpu->stat.halt_wakeup;
 	}
 
@@ -707,8 +707,8 @@ int kvmppc_pseries_do_hcall(struct kvm_vcpu *vcpu)
 		tvcpu->arch.prodded = 1;
 		smp_mb();
 		if (vcpu->arch.ceded) {
-			if (waitqueue_active(&vcpu->wq)) {
-				wake_up_interruptible(&vcpu->wq);
+			if (swaitqueue_active(&vcpu->wq)) {
+				swait_wake_interruptible(&vcpu->wq);
 				vcpu->stat.halt_wakeup++;
 			}
 		}
@@ -1447,7 +1447,7 @@ static struct kvmppc_vcore *kvmppc_vcore_create(struct kvm *kvm, int core)
 	INIT_LIST_HEAD(&vcore->runnable_threads);
 	spin_lock_init(&vcore->lock);
 	spin_lock_init(&vcore->stoltb_lock);
-	init_waitqueue_head(&vcore->wq);
+	init_swait_head(&vcore->wq);
 	vcore->preempt_tb = TB_NIL;
 	vcore->lpcr = kvm->arch.lpcr;
 	vcore->first_vcpuid = core * threads_per_subcore;
@@ -2519,10 +2519,9 @@ static void kvmppc_vcore_blocked(struct kvmppc_vcore *vc)
 {
 	struct kvm_vcpu *vcpu;
 	int do_sleep = 1;
+	DEFINE_SWAITER(wait);
 
-	DEFINE_WAIT(wait);
-
-	prepare_to_wait(&vc->wq, &wait, TASK_INTERRUPTIBLE);
+	swait_prepare(&vc->wq, &wait, TASK_INTERRUPTIBLE);
 
 	/*
 	 * Check one last time for pending exceptions and ceded state after
@@ -2536,7 +2535,7 @@ static void kvmppc_vcore_blocked(struct kvmppc_vcore *vc)
 	}
 
 	if (!do_sleep) {
-		finish_wait(&vc->wq, &wait);
+		swait_finish(&vc->wq, &wait);
 		return;
 	}
 
@@ -2544,7 +2543,7 @@ static void kvmppc_vcore_blocked(struct kvmppc_vcore *vc)
 	trace_kvmppc_vcore_blocked(vc, 0);
 	spin_unlock(&vc->lock);
 	schedule();
-	finish_wait(&vc->wq, &wait);
+	swait_finish(&vc->wq, &wait);
 	spin_lock(&vc->lock);
 	vc->vcore_state = VCORE_INACTIVE;
 	trace_kvmppc_vcore_blocked(vc, 1);
@@ -2600,7 +2599,7 @@ static int kvmppc_run_vcpu(struct kvm_run *kvm_run, struct kvm_vcpu *vcpu)
 			kvmppc_start_thread(vcpu, vc);
 			trace_kvm_guest_enter(vcpu);
 		} else if (vc->vcore_state == VCORE_SLEEPING) {
-			wake_up(&vc->wq);
+			swait_wake(&vc->wq);
 		}
 
 	}
diff --git a/arch/powerpc/platforms/ps3/device-init.c b/arch/powerpc/platforms/ps3/device-init.c
index 3f175e8aedb4..c4c02f91904c 100644
--- a/arch/powerpc/platforms/ps3/device-init.c
+++ b/arch/powerpc/platforms/ps3/device-init.c
@@ -752,7 +752,7 @@ static int ps3_notification_read_write(struct ps3_notification_device *dev,
 	}
 	pr_debug("%s:%u: notification %s issued\n", __func__, __LINE__, op);
 
-	res = wait_event_interruptible(dev->done.wait,
+	res = swait_event_interruptible(dev->done.wait,
 				       dev->done.done || kthread_should_stop());
 	if (kthread_should_stop())
 		res = -EINTR;
diff --git a/arch/s390/include/asm/kvm_host.h b/arch/s390/include/asm/kvm_host.h
index efaac2c3bb77..801784f62ca9 100644
--- a/arch/s390/include/asm/kvm_host.h
+++ b/arch/s390/include/asm/kvm_host.h
@@ -427,7 +427,7 @@ struct kvm_s390_irq_payload {
 struct kvm_s390_local_interrupt {
 	spinlock_t lock;
 	struct kvm_s390_float_interrupt *float_int;
-	wait_queue_head_t *wq;
+	struct swait_head *wq;
 	atomic_t *cpuflags;
 	DECLARE_BITMAP(sigp_emerg_pending, KVM_MAX_VCPUS);
 	struct kvm_s390_irq_payload irq;
diff --git a/arch/s390/kvm/interrupt.c b/arch/s390/kvm/interrupt.c
index 6a75352f453c..8b4516b99bcd 100644
--- a/arch/s390/kvm/interrupt.c
+++ b/arch/s390/kvm/interrupt.c
@@ -868,13 +868,13 @@ int kvm_s390_handle_wait(struct kvm_vcpu *vcpu)
 
 void kvm_s390_vcpu_wakeup(struct kvm_vcpu *vcpu)
 {
-	if (waitqueue_active(&vcpu->wq)) {
+	if (swaitqueue_active(&vcpu->wq)) {
 		/*
 		 * The vcpu gave up the cpu voluntarily, mark it as a good
 		 * yield-candidate.
 		 */
 		vcpu->preempted = true;
-		wake_up_interruptible(&vcpu->wq);
+		swait_wake_interruptible(&vcpu->wq);
 		vcpu->stat.halt_wakeup++;
 	}
 }
diff --git a/arch/sh/kernel/irq.c b/arch/sh/kernel/irq.c
index 6c0378c0b8b5..abd58b4dff97 100644
--- a/arch/sh/kernel/irq.c
+++ b/arch/sh/kernel/irq.c
@@ -147,6 +147,7 @@ void irq_ctx_exit(int cpu)
 	hardirq_ctx[cpu] = NULL;
 }
 
+#ifndef CONFIG_PREEMPT_RT_FULL
 void do_softirq_own_stack(void)
 {
 	struct thread_info *curctx;
@@ -174,6 +175,7 @@ void do_softirq_own_stack(void)
 		  "r5", "r6", "r7", "r8", "r9", "r15", "t", "pr"
 	);
 }
+#endif
 #else
 static inline void handle_one_irq(unsigned int irq)
 {
diff --git a/arch/sparc/Kconfig b/arch/sparc/Kconfig
index 56442d2d7bbc..8c9598f534c9 100644
--- a/arch/sparc/Kconfig
+++ b/arch/sparc/Kconfig
@@ -189,12 +189,10 @@ config NR_CPUS
 source kernel/Kconfig.hz
 
 config RWSEM_GENERIC_SPINLOCK
-	bool
-	default y if SPARC32
+	def_bool PREEMPT_RT_FULL
 
 config RWSEM_XCHGADD_ALGORITHM
-	bool
-	default y if SPARC64
+	def_bool !RWSEM_GENERIC_SPINLOCK && !PREEMPT_RT_FULL
 
 config GENERIC_HWEIGHT
 	bool
diff --git a/arch/sparc/kernel/irq_64.c b/arch/sparc/kernel/irq_64.c
index e22416ce56ea..d359de71153a 100644
--- a/arch/sparc/kernel/irq_64.c
+++ b/arch/sparc/kernel/irq_64.c
@@ -854,6 +854,7 @@ void __irq_entry handler_irq(int pil, struct pt_regs *regs)
 	set_irq_regs(old_regs);
 }
 
+#ifndef CONFIG_PREEMPT_RT_FULL
 void do_softirq_own_stack(void)
 {
 	void *orig_sp, *sp = softirq_stack[smp_processor_id()];
@@ -868,6 +869,7 @@ void do_softirq_own_stack(void)
 	__asm__ __volatile__("mov %0, %%sp"
 			     : : "r" (orig_sp));
 }
+#endif
 
 #ifdef CONFIG_HOTPLUG_CPU
 void fixup_irqs(void)
diff --git a/arch/x86/Kconfig b/arch/x86/Kconfig
index db3622f22b61..d8a1650a0f67 100644
--- a/arch/x86/Kconfig
+++ b/arch/x86/Kconfig
@@ -17,6 +17,7 @@ config X86_64
 ### Arch settings
 config X86
 	def_bool y
+	select HAVE_PREEMPT_LAZY
 	select ACPI_LEGACY_TABLES_LOOKUP	if ACPI
 	select ACPI_SYSTEM_POWER_STATES_SUPPORT	if ACPI
 	select ANON_INODES
@@ -212,8 +213,11 @@ config ARCH_MAY_HAVE_PC_FDC
 	def_bool y
 	depends on ISA_DMA_API
 
+config RWSEM_GENERIC_SPINLOCK
+	def_bool PREEMPT_RT_FULL
+
 config RWSEM_XCHGADD_ALGORITHM
-	def_bool y
+	def_bool !RWSEM_GENERIC_SPINLOCK && !PREEMPT_RT_FULL
 
 config GENERIC_CALIBRATE_DELAY
 	def_bool y
@@ -848,7 +852,7 @@ config IOMMU_HELPER
 config MAXSMP
 	bool "Enable Maximum number of SMP Processors and NUMA Nodes"
 	depends on X86_64 && SMP && DEBUG_KERNEL
-	select CPUMASK_OFFSTACK
+	select CPUMASK_OFFSTACK if !PREEMPT_RT_FULL
 	---help---
 	  Enable maximum number of CPUS and NUMA Nodes for this architecture.
 	  If unsure, say N.
diff --git a/arch/x86/crypto/aesni-intel_glue.c b/arch/x86/crypto/aesni-intel_glue.c
index 3633ad6145c5..c6d5458ee7f9 100644
--- a/arch/x86/crypto/aesni-intel_glue.c
+++ b/arch/x86/crypto/aesni-intel_glue.c
@@ -383,14 +383,14 @@ static int ecb_encrypt(struct blkcipher_desc *desc,
 	err = blkcipher_walk_virt(desc, &walk);
 	desc->flags &= ~CRYPTO_TFM_REQ_MAY_SLEEP;
 
-	kernel_fpu_begin();
 	while ((nbytes = walk.nbytes)) {
+		kernel_fpu_begin();
 		aesni_ecb_enc(ctx, walk.dst.virt.addr, walk.src.virt.addr,
-			      nbytes & AES_BLOCK_MASK);
+				nbytes & AES_BLOCK_MASK);
+		kernel_fpu_end();
 		nbytes &= AES_BLOCK_SIZE - 1;
 		err = blkcipher_walk_done(desc, &walk, nbytes);
 	}
-	kernel_fpu_end();
 
 	return err;
 }
@@ -407,14 +407,14 @@ static int ecb_decrypt(struct blkcipher_desc *desc,
 	err = blkcipher_walk_virt(desc, &walk);
 	desc->flags &= ~CRYPTO_TFM_REQ_MAY_SLEEP;
 
-	kernel_fpu_begin();
 	while ((nbytes = walk.nbytes)) {
+		kernel_fpu_begin();
 		aesni_ecb_dec(ctx, walk.dst.virt.addr, walk.src.virt.addr,
 			      nbytes & AES_BLOCK_MASK);
+		kernel_fpu_end();
 		nbytes &= AES_BLOCK_SIZE - 1;
 		err = blkcipher_walk_done(desc, &walk, nbytes);
 	}
-	kernel_fpu_end();
 
 	return err;
 }
@@ -431,14 +431,14 @@ static int cbc_encrypt(struct blkcipher_desc *desc,
 	err = blkcipher_walk_virt(desc, &walk);
 	desc->flags &= ~CRYPTO_TFM_REQ_MAY_SLEEP;
 
-	kernel_fpu_begin();
 	while ((nbytes = walk.nbytes)) {
+		kernel_fpu_begin();
 		aesni_cbc_enc(ctx, walk.dst.virt.addr, walk.src.virt.addr,
 			      nbytes & AES_BLOCK_MASK, walk.iv);
+		kernel_fpu_end();
 		nbytes &= AES_BLOCK_SIZE - 1;
 		err = blkcipher_walk_done(desc, &walk, nbytes);
 	}
-	kernel_fpu_end();
 
 	return err;
 }
@@ -455,14 +455,14 @@ static int cbc_decrypt(struct blkcipher_desc *desc,
 	err = blkcipher_walk_virt(desc, &walk);
 	desc->flags &= ~CRYPTO_TFM_REQ_MAY_SLEEP;
 
-	kernel_fpu_begin();
 	while ((nbytes = walk.nbytes)) {
+		kernel_fpu_begin();
 		aesni_cbc_dec(ctx, walk.dst.virt.addr, walk.src.virt.addr,
 			      nbytes & AES_BLOCK_MASK, walk.iv);
+		kernel_fpu_end();
 		nbytes &= AES_BLOCK_SIZE - 1;
 		err = blkcipher_walk_done(desc, &walk, nbytes);
 	}
-	kernel_fpu_end();
 
 	return err;
 }
@@ -514,18 +514,20 @@ static int ctr_crypt(struct blkcipher_desc *desc,
 	err = blkcipher_walk_virt_block(desc, &walk, AES_BLOCK_SIZE);
 	desc->flags &= ~CRYPTO_TFM_REQ_MAY_SLEEP;
 
-	kernel_fpu_begin();
 	while ((nbytes = walk.nbytes) >= AES_BLOCK_SIZE) {
+		kernel_fpu_begin();
 		aesni_ctr_enc_tfm(ctx, walk.dst.virt.addr, walk.src.virt.addr,
 			              nbytes & AES_BLOCK_MASK, walk.iv);
+		kernel_fpu_end();
 		nbytes &= AES_BLOCK_SIZE - 1;
 		err = blkcipher_walk_done(desc, &walk, nbytes);
 	}
 	if (walk.nbytes) {
+		kernel_fpu_begin();
 		ctr_crypt_final(ctx, &walk);
+		kernel_fpu_end();
 		err = blkcipher_walk_done(desc, &walk, 0);
 	}
-	kernel_fpu_end();
 
 	return err;
 }
diff --git a/arch/x86/crypto/cast5_avx_glue.c b/arch/x86/crypto/cast5_avx_glue.c
index 8648158f3916..d7699130ee36 100644
--- a/arch/x86/crypto/cast5_avx_glue.c
+++ b/arch/x86/crypto/cast5_avx_glue.c
@@ -59,7 +59,7 @@ static inline void cast5_fpu_end(bool fpu_enabled)
 static int ecb_crypt(struct blkcipher_desc *desc, struct blkcipher_walk *walk,
 		     bool enc)
 {
-	bool fpu_enabled = false;
+	bool fpu_enabled;
 	struct cast5_ctx *ctx = crypto_blkcipher_ctx(desc->tfm);
 	const unsigned int bsize = CAST5_BLOCK_SIZE;
 	unsigned int nbytes;
@@ -75,7 +75,7 @@ static int ecb_crypt(struct blkcipher_desc *desc, struct blkcipher_walk *walk,
 		u8 *wsrc = walk->src.virt.addr;
 		u8 *wdst = walk->dst.virt.addr;
 
-		fpu_enabled = cast5_fpu_begin(fpu_enabled, nbytes);
+		fpu_enabled = cast5_fpu_begin(false, nbytes);
 
 		/* Process multi-block batch */
 		if (nbytes >= bsize * CAST5_PARALLEL_BLOCKS) {
@@ -103,10 +103,9 @@ static int ecb_crypt(struct blkcipher_desc *desc, struct blkcipher_walk *walk,
 		} while (nbytes >= bsize);
 
 done:
+		cast5_fpu_end(fpu_enabled);
 		err = blkcipher_walk_done(desc, walk, nbytes);
 	}
-
-	cast5_fpu_end(fpu_enabled);
 	return err;
 }
 
@@ -227,7 +226,7 @@ static unsigned int __cbc_decrypt(struct blkcipher_desc *desc,
 static int cbc_decrypt(struct blkcipher_desc *desc, struct scatterlist *dst,
 		       struct scatterlist *src, unsigned int nbytes)
 {
-	bool fpu_enabled = false;
+	bool fpu_enabled;
 	struct blkcipher_walk walk;
 	int err;
 
@@ -236,12 +235,11 @@ static int cbc_decrypt(struct blkcipher_desc *desc, struct scatterlist *dst,
 	desc->flags &= ~CRYPTO_TFM_REQ_MAY_SLEEP;
 
 	while ((nbytes = walk.nbytes)) {
-		fpu_enabled = cast5_fpu_begin(fpu_enabled, nbytes);
+		fpu_enabled = cast5_fpu_begin(false, nbytes);
 		nbytes = __cbc_decrypt(desc, &walk);
+		cast5_fpu_end(fpu_enabled);
 		err = blkcipher_walk_done(desc, &walk, nbytes);
 	}
-
-	cast5_fpu_end(fpu_enabled);
 	return err;
 }
 
@@ -311,7 +309,7 @@ static unsigned int __ctr_crypt(struct blkcipher_desc *desc,
 static int ctr_crypt(struct blkcipher_desc *desc, struct scatterlist *dst,
 		     struct scatterlist *src, unsigned int nbytes)
 {
-	bool fpu_enabled = false;
+	bool fpu_enabled;
 	struct blkcipher_walk walk;
 	int err;
 
@@ -320,13 +318,12 @@ static int ctr_crypt(struct blkcipher_desc *desc, struct scatterlist *dst,
 	desc->flags &= ~CRYPTO_TFM_REQ_MAY_SLEEP;
 
 	while ((nbytes = walk.nbytes) >= CAST5_BLOCK_SIZE) {
-		fpu_enabled = cast5_fpu_begin(fpu_enabled, nbytes);
+		fpu_enabled = cast5_fpu_begin(false, nbytes);
 		nbytes = __ctr_crypt(desc, &walk);
+		cast5_fpu_end(fpu_enabled);
 		err = blkcipher_walk_done(desc, &walk, nbytes);
 	}
 
-	cast5_fpu_end(fpu_enabled);
-
 	if (walk.nbytes) {
 		ctr_crypt_final(desc, &walk);
 		err = blkcipher_walk_done(desc, &walk, 0);
diff --git a/arch/x86/crypto/glue_helper.c b/arch/x86/crypto/glue_helper.c
index 6a85598931b5..3a506ce7ed93 100644
--- a/arch/x86/crypto/glue_helper.c
+++ b/arch/x86/crypto/glue_helper.c
@@ -39,7 +39,7 @@ static int __glue_ecb_crypt_128bit(const struct common_glue_ctx *gctx,
 	void *ctx = crypto_blkcipher_ctx(desc->tfm);
 	const unsigned int bsize = 128 / 8;
 	unsigned int nbytes, i, func_bytes;
-	bool fpu_enabled = false;
+	bool fpu_enabled;
 	int err;
 
 	err = blkcipher_walk_virt(desc, walk);
@@ -49,7 +49,7 @@ static int __glue_ecb_crypt_128bit(const struct common_glue_ctx *gctx,
 		u8 *wdst = walk->dst.virt.addr;
 
 		fpu_enabled = glue_fpu_begin(bsize, gctx->fpu_blocks_limit,
-					     desc, fpu_enabled, nbytes);
+					     desc, false, nbytes);
 
 		for (i = 0; i < gctx->num_funcs; i++) {
 			func_bytes = bsize * gctx->funcs[i].num_blocks;
@@ -71,10 +71,10 @@ static int __glue_ecb_crypt_128bit(const struct common_glue_ctx *gctx,
 		}
 
 done:
+		glue_fpu_end(fpu_enabled);
 		err = blkcipher_walk_done(desc, walk, nbytes);
 	}
 
-	glue_fpu_end(fpu_enabled);
 	return err;
 }
 
@@ -194,7 +194,7 @@ int glue_cbc_decrypt_128bit(const struct common_glue_ctx *gctx,
 			    struct scatterlist *src, unsigned int nbytes)
 {
 	const unsigned int bsize = 128 / 8;
-	bool fpu_enabled = false;
+	bool fpu_enabled;
 	struct blkcipher_walk walk;
 	int err;
 
@@ -203,12 +203,12 @@ int glue_cbc_decrypt_128bit(const struct common_glue_ctx *gctx,
 
 	while ((nbytes = walk.nbytes)) {
 		fpu_enabled = glue_fpu_begin(bsize, gctx->fpu_blocks_limit,
-					     desc, fpu_enabled, nbytes);
+					     desc, false, nbytes);
 		nbytes = __glue_cbc_decrypt_128bit(gctx, desc, &walk);
+		glue_fpu_end(fpu_enabled);
 		err = blkcipher_walk_done(desc, &walk, nbytes);
 	}
 
-	glue_fpu_end(fpu_enabled);
 	return err;
 }
 EXPORT_SYMBOL_GPL(glue_cbc_decrypt_128bit);
@@ -277,7 +277,7 @@ int glue_ctr_crypt_128bit(const struct common_glue_ctx *gctx,
 			  struct scatterlist *src, unsigned int nbytes)
 {
 	const unsigned int bsize = 128 / 8;
-	bool fpu_enabled = false;
+	bool fpu_enabled;
 	struct blkcipher_walk walk;
 	int err;
 
@@ -286,13 +286,12 @@ int glue_ctr_crypt_128bit(const struct common_glue_ctx *gctx,
 
 	while ((nbytes = walk.nbytes) >= bsize) {
 		fpu_enabled = glue_fpu_begin(bsize, gctx->fpu_blocks_limit,
-					     desc, fpu_enabled, nbytes);
+					     desc, false, nbytes);
 		nbytes = __glue_ctr_crypt_128bit(gctx, desc, &walk);
+		glue_fpu_end(fpu_enabled);
 		err = blkcipher_walk_done(desc, &walk, nbytes);
 	}
 
-	glue_fpu_end(fpu_enabled);
-
 	if (walk.nbytes) {
 		glue_ctr_crypt_final_128bit(
 			gctx->funcs[gctx->num_funcs - 1].fn_u.ctr, desc, &walk);
@@ -347,7 +346,7 @@ int glue_xts_crypt_128bit(const struct common_glue_ctx *gctx,
 			  void *tweak_ctx, void *crypt_ctx)
 {
 	const unsigned int bsize = 128 / 8;
-	bool fpu_enabled = false;
+	bool fpu_enabled;
 	struct blkcipher_walk walk;
 	int err;
 
@@ -360,21 +359,21 @@ int glue_xts_crypt_128bit(const struct common_glue_ctx *gctx,
 
 	/* set minimum length to bsize, for tweak_fn */
 	fpu_enabled = glue_fpu_begin(bsize, gctx->fpu_blocks_limit,
-				     desc, fpu_enabled,
+				     desc, false,
 				     nbytes < bsize ? bsize : nbytes);
-
 	/* calculate first value of T */
 	tweak_fn(tweak_ctx, walk.iv, walk.iv);
+	glue_fpu_end(fpu_enabled);
 
 	while (nbytes) {
+		fpu_enabled = glue_fpu_begin(bsize, gctx->fpu_blocks_limit,
+				desc, false, nbytes);
 		nbytes = __glue_xts_crypt_128bit(gctx, crypt_ctx, desc, &walk);
 
+		glue_fpu_end(fpu_enabled);
 		err = blkcipher_walk_done(desc, &walk, nbytes);
 		nbytes = walk.nbytes;
 	}
-
-	glue_fpu_end(fpu_enabled);
-
 	return err;
 }
 EXPORT_SYMBOL_GPL(glue_xts_crypt_128bit);
diff --git a/arch/x86/entry/common.c b/arch/x86/entry/common.c
index 03663740c866..3ec240f3951a 100644
--- a/arch/x86/entry/common.c
+++ b/arch/x86/entry/common.c
@@ -220,7 +220,7 @@ long syscall_trace_enter(struct pt_regs *regs)
 
 #define EXIT_TO_USERMODE_LOOP_FLAGS				\
 	(_TIF_SIGPENDING | _TIF_NOTIFY_RESUME | _TIF_UPROBE |	\
-	 _TIF_NEED_RESCHED | _TIF_USER_RETURN_NOTIFY)
+	 _TIF_NEED_RESCHED_MASK | _TIF_USER_RETURN_NOTIFY)
 
 static void exit_to_usermode_loop(struct pt_regs *regs, u32 cached_flags)
 {
@@ -236,9 +236,16 @@ static void exit_to_usermode_loop(struct pt_regs *regs, u32 cached_flags)
 		/* We have work to do. */
 		local_irq_enable();
 
-		if (cached_flags & _TIF_NEED_RESCHED)
+		if (cached_flags & _TIF_NEED_RESCHED_MASK)
 			schedule();
 
+#ifdef ARCH_RT_DELAYS_SIGNAL_SEND
+		if (unlikely(current->forced_info.si_signo)) {
+			struct task_struct *t = current;
+			force_sig_info(t->forced_info.si_signo, &t->forced_info, t);
+			t->forced_info.si_signo = 0;
+		}
+#endif
 		if (cached_flags & _TIF_UPROBE)
 			uprobe_notify_resume(regs);
 
diff --git a/arch/x86/entry/entry_32.S b/arch/x86/entry/entry_32.S
index f3b6d54e0042..2d722ee01fc2 100644
--- a/arch/x86/entry/entry_32.S
+++ b/arch/x86/entry/entry_32.S
@@ -278,8 +278,24 @@ END(ret_from_exception)
 ENTRY(resume_kernel)
 	DISABLE_INTERRUPTS(CLBR_ANY)
 need_resched:
+	# preempt count == 0 + NEED_RS set?
 	cmpl	$0, PER_CPU_VAR(__preempt_count)
+#ifndef CONFIG_PREEMPT_LAZY
 	jnz	restore_all
+#else
+	jz test_int_off
+
+	# atleast preempt count == 0 ?
+	cmpl $_PREEMPT_ENABLED,PER_CPU_VAR(__preempt_count)
+	jne restore_all
+
+	cmpl $0,TI_preempt_lazy_count(%ebp)	# non-zero preempt_lazy_count ?
+	jnz restore_all
+
+	testl $_TIF_NEED_RESCHED_LAZY, TI_flags(%ebp)
+	jz restore_all
+test_int_off:
+#endif
 	testl	$X86_EFLAGS_IF, PT_EFLAGS(%esp)	# interrupts off (exception path) ?
 	jz	restore_all
 	call	preempt_schedule_irq
diff --git a/arch/x86/entry/entry_64.S b/arch/x86/entry/entry_64.S
index a55697d19824..316081a2ca85 100644
--- a/arch/x86/entry/entry_64.S
+++ b/arch/x86/entry/entry_64.S
@@ -579,7 +579,23 @@ GLOBAL(retint_user)
 	bt	$9, EFLAGS(%rsp)		/* were interrupts off? */
 	jnc	1f
 0:	cmpl	$0, PER_CPU_VAR(__preempt_count)
+#ifndef CONFIG_PREEMPT_LAZY
 	jnz	1f
+#else
+	jz	do_preempt_schedule_irq
+
+	# atleast preempt count == 0 ?
+	cmpl $_PREEMPT_ENABLED,PER_CPU_VAR(__preempt_count)
+	jnz	1f
+
+	GET_THREAD_INFO(%rcx)
+	cmpl	$0, TI_preempt_lazy_count(%rcx)
+	jnz	1f
+
+	bt	$TIF_NEED_RESCHED_LAZY,TI_flags(%rcx)
+	jnc	1f
+do_preempt_schedule_irq:
+#endif
 	call	preempt_schedule_irq
 	jmp	0b
 1:
@@ -867,6 +883,7 @@ END(native_load_gs_index)
 	jmp	2b
 	.previous
 
+#ifndef CONFIG_PREEMPT_RT_FULL
 /* Call softirq on interrupt stack. Interrupts are off. */
 ENTRY(do_softirq_own_stack)
 	pushq	%rbp
@@ -879,6 +896,7 @@ ENTRY(do_softirq_own_stack)
 	decl	PER_CPU_VAR(irq_count)
 	ret
 END(do_softirq_own_stack)
+#endif
 
 #ifdef CONFIG_XEN
 idtentry xen_hypervisor_callback xen_do_hypervisor_callback has_error_code=0
diff --git a/arch/x86/include/asm/preempt.h b/arch/x86/include/asm/preempt.h
index 01bcde84d3e4..5dbd2d0f91e0 100644
--- a/arch/x86/include/asm/preempt.h
+++ b/arch/x86/include/asm/preempt.h
@@ -79,17 +79,33 @@ static __always_inline void __preempt_count_sub(int val)
  * a decrement which hits zero means we have no preempt_count and should
  * reschedule.
  */
-static __always_inline bool __preempt_count_dec_and_test(void)
+static __always_inline bool ____preempt_count_dec_and_test(void)
 {
 	GEN_UNARY_RMWcc("decl", __preempt_count, __percpu_arg(0), "e");
 }
 
+static __always_inline bool __preempt_count_dec_and_test(void)
+{
+	if (____preempt_count_dec_and_test())
+		return true;
+#ifdef CONFIG_PREEMPT_LAZY
+	return test_thread_flag(TIF_NEED_RESCHED_LAZY);
+#else
+	return false;
+#endif
+}
+
 /*
  * Returns true when we need to resched and can (barring IRQ state).
  */
 static __always_inline bool should_resched(int preempt_offset)
 {
+#ifdef CONFIG_PREEMPT_LAZY
+	return unlikely(raw_cpu_read_4(__preempt_count) == preempt_offset ||
+			test_thread_flag(TIF_NEED_RESCHED_LAZY));
+#else
 	return unlikely(raw_cpu_read_4(__preempt_count) == preempt_offset);
+#endif
 }
 
 #ifdef CONFIG_PREEMPT
diff --git a/arch/x86/include/asm/signal.h b/arch/x86/include/asm/signal.h
index 2138c9ae19ee..3f5b4ee2e2c1 100644
--- a/arch/x86/include/asm/signal.h
+++ b/arch/x86/include/asm/signal.h
@@ -23,6 +23,19 @@ typedef struct {
 	unsigned long sig[_NSIG_WORDS];
 } sigset_t;
 
+/*
+ * Because some traps use the IST stack, we must keep preemption
+ * disabled while calling do_trap(), but do_trap() may call
+ * force_sig_info() which will grab the signal spin_locks for the
+ * task, which in PREEMPT_RT_FULL are mutexes.  By defining
+ * ARCH_RT_DELAYS_SIGNAL_SEND the force_sig_info() will set
+ * TIF_NOTIFY_RESUME and set up the signal to be sent on exit of the
+ * trap.
+ */
+#if defined(CONFIG_PREEMPT_RT_FULL)
+#define ARCH_RT_DELAYS_SIGNAL_SEND
+#endif
+
 #ifndef CONFIG_COMPAT
 typedef sigset_t compat_sigset_t;
 #endif
diff --git a/arch/x86/include/asm/stackprotector.h b/arch/x86/include/asm/stackprotector.h
index 58505f01962f..02fa39652cd6 100644
--- a/arch/x86/include/asm/stackprotector.h
+++ b/arch/x86/include/asm/stackprotector.h
@@ -59,7 +59,7 @@
  */
 static __always_inline void boot_init_stack_canary(void)
 {
-	u64 canary;
+	u64 uninitialized_var(canary);
 	u64 tsc;
 
 #ifdef CONFIG_X86_64
@@ -70,8 +70,15 @@ static __always_inline void boot_init_stack_canary(void)
 	 * of randomness. The TSC only matters for very early init,
 	 * there it already has some randomness on most systems. Later
 	 * on during the bootup the random pool has true entropy too.
+	 *
+	 * For preempt-rt we need to weaken the randomness a bit, as
+	 * we can't call into the random generator from atomic context
+	 * due to locking constraints. We just leave canary
+	 * uninitialized and use the TSC based randomness on top of it.
 	 */
+#ifndef CONFIG_PREEMPT_RT_FULL
 	get_random_bytes(&canary, sizeof(canary));
+#endif
 	tsc = rdtsc();
 	canary += tsc + (tsc << 32UL);
 
diff --git a/arch/x86/include/asm/thread_info.h b/arch/x86/include/asm/thread_info.h
index c7b551028740..ddb63bd90e3c 100644
--- a/arch/x86/include/asm/thread_info.h
+++ b/arch/x86/include/asm/thread_info.h
@@ -58,6 +58,8 @@ struct thread_info {
 	__u32			status;		/* thread synchronous flags */
 	__u32			cpu;		/* current CPU */
 	mm_segment_t		addr_limit;
+	int			preempt_lazy_count;	/* 0 => lazy preemptable
+							  <0 => BUG */
 	unsigned int		sig_on_uaccess_error:1;
 	unsigned int		uaccess_err:1;	/* uaccess failed */
 };
@@ -95,6 +97,7 @@ struct thread_info {
 #define TIF_SYSCALL_EMU		6	/* syscall emulation active */
 #define TIF_SYSCALL_AUDIT	7	/* syscall auditing active */
 #define TIF_SECCOMP		8	/* secure computing */
+#define TIF_NEED_RESCHED_LAZY	9	/* lazy rescheduling necessary */
 #define TIF_USER_RETURN_NOTIFY	11	/* notify kernel of userspace return */
 #define TIF_UPROBE		12	/* breakpointed or singlestepping */
 #define TIF_NOTSC		16	/* TSC is not accessible in userland */
@@ -119,6 +122,7 @@ struct thread_info {
 #define _TIF_SYSCALL_EMU	(1 << TIF_SYSCALL_EMU)
 #define _TIF_SYSCALL_AUDIT	(1 << TIF_SYSCALL_AUDIT)
 #define _TIF_SECCOMP		(1 << TIF_SECCOMP)
+#define _TIF_NEED_RESCHED_LAZY	(1 << TIF_NEED_RESCHED_LAZY)
 #define _TIF_USER_RETURN_NOTIFY	(1 << TIF_USER_RETURN_NOTIFY)
 #define _TIF_UPROBE		(1 << TIF_UPROBE)
 #define _TIF_NOTSC		(1 << TIF_NOTSC)
@@ -152,6 +156,8 @@ struct thread_info {
 #define _TIF_WORK_CTXSW_PREV (_TIF_WORK_CTXSW|_TIF_USER_RETURN_NOTIFY)
 #define _TIF_WORK_CTXSW_NEXT (_TIF_WORK_CTXSW)
 
+#define _TIF_NEED_RESCHED_MASK	(_TIF_NEED_RESCHED | _TIF_NEED_RESCHED_LAZY)
+
 #define STACK_WARN		(THREAD_SIZE/8)
 
 /*
diff --git a/arch/x86/include/asm/uv/uv_bau.h b/arch/x86/include/asm/uv/uv_bau.h
index fc808b83fccb..ebb40118abf5 100644
--- a/arch/x86/include/asm/uv/uv_bau.h
+++ b/arch/x86/include/asm/uv/uv_bau.h
@@ -615,9 +615,9 @@ struct bau_control {
 	cycles_t		send_message;
 	cycles_t		period_end;
 	cycles_t		period_time;
-	spinlock_t		uvhub_lock;
-	spinlock_t		queue_lock;
-	spinlock_t		disable_lock;
+	raw_spinlock_t		uvhub_lock;
+	raw_spinlock_t		queue_lock;
+	raw_spinlock_t		disable_lock;
 	/* tunables */
 	int			max_concurr;
 	int			max_concurr_const;
@@ -776,15 +776,15 @@ static inline int atom_asr(short i, struct atomic_short *v)
  * to be lowered below the current 'v'.  atomic_add_unless can only stop
  * on equal.
  */
-static inline int atomic_inc_unless_ge(spinlock_t *lock, atomic_t *v, int u)
+static inline int atomic_inc_unless_ge(raw_spinlock_t *lock, atomic_t *v, int u)
 {
-	spin_lock(lock);
+	raw_spin_lock(lock);
 	if (atomic_read(v) >= u) {
-		spin_unlock(lock);
+		raw_spin_unlock(lock);
 		return 0;
 	}
 	atomic_inc(v);
-	spin_unlock(lock);
+	raw_spin_unlock(lock);
 	return 1;
 }
 
diff --git a/arch/x86/include/asm/uv/uv_hub.h b/arch/x86/include/asm/uv/uv_hub.h
index ea7074784cc4..01ec643ce66e 100644
--- a/arch/x86/include/asm/uv/uv_hub.h
+++ b/arch/x86/include/asm/uv/uv_hub.h
@@ -492,7 +492,7 @@ struct uv_blade_info {
 	unsigned short	nr_online_cpus;
 	unsigned short	pnode;
 	short		memory_nid;
-	spinlock_t	nmi_lock;	/* obsolete, see uv_hub_nmi */
+	raw_spinlock_t	nmi_lock;	/* obsolete, see uv_hub_nmi */
 	unsigned long	nmi_count;	/* obsolete, see uv_hub_nmi */
 };
 extern struct uv_blade_info *uv_blade_info;
diff --git a/arch/x86/kernel/apic/io_apic.c b/arch/x86/kernel/apic/io_apic.c
index f25321894ad2..db26afddf967 100644
--- a/arch/x86/kernel/apic/io_apic.c
+++ b/arch/x86/kernel/apic/io_apic.c
@@ -1711,7 +1711,8 @@ static bool io_apic_level_ack_pending(struct mp_chip_data *data)
 static inline bool ioapic_irqd_mask(struct irq_data *data)
 {
 	/* If we are moving the irq we need to mask it */
-	if (unlikely(irqd_is_setaffinity_pending(data))) {
+	if (unlikely(irqd_is_setaffinity_pending(data) &&
+		     !irqd_irq_inprogress(data))) {
 		mask_ioapic_irq(data);
 		return true;
 	}
diff --git a/arch/x86/kernel/apic/x2apic_uv_x.c b/arch/x86/kernel/apic/x2apic_uv_x.c
index 4a139465f1d4..ad2afff02b36 100644
--- a/arch/x86/kernel/apic/x2apic_uv_x.c
+++ b/arch/x86/kernel/apic/x2apic_uv_x.c
@@ -947,7 +947,7 @@ void __init uv_system_init(void)
 			uv_blade_info[blade].pnode = pnode;
 			uv_blade_info[blade].nr_possible_cpus = 0;
 			uv_blade_info[blade].nr_online_cpus = 0;
-			spin_lock_init(&uv_blade_info[blade].nmi_lock);
+			raw_spin_lock_init(&uv_blade_info[blade].nmi_lock);
 			min_pnode = min(pnode, min_pnode);
 			max_pnode = max(pnode, max_pnode);
 			blade++;
diff --git a/arch/x86/kernel/asm-offsets.c b/arch/x86/kernel/asm-offsets.c
index 439df975bc7a..b7954ddd6a0a 100644
--- a/arch/x86/kernel/asm-offsets.c
+++ b/arch/x86/kernel/asm-offsets.c
@@ -32,6 +32,7 @@ void common(void) {
 	OFFSET(TI_flags, thread_info, flags);
 	OFFSET(TI_status, thread_info, status);
 	OFFSET(TI_addr_limit, thread_info, addr_limit);
+	OFFSET(TI_preempt_lazy_count, thread_info, preempt_lazy_count);
 
 	BLANK();
 	OFFSET(crypto_tfm_ctx_offset, crypto_tfm, __crt_ctx);
@@ -89,4 +90,5 @@ void common(void) {
 
 	BLANK();
 	DEFINE(PTREGS_SIZE, sizeof(struct pt_regs));
+	DEFINE(_PREEMPT_ENABLED, PREEMPT_ENABLED);
 }
diff --git a/arch/x86/kernel/cpu/mcheck/mce.c b/arch/x86/kernel/cpu/mcheck/mce.c
index 7e8a736d09db..a080b4939019 100644
--- a/arch/x86/kernel/cpu/mcheck/mce.c
+++ b/arch/x86/kernel/cpu/mcheck/mce.c
@@ -41,6 +41,8 @@
 #include <linux/debugfs.h>
 #include <linux/irq_work.h>
 #include <linux/export.h>
+#include <linux/jiffies.h>
+#include <linux/work-simple.h>
 
 #include <asm/processor.h>
 #include <asm/traps.h>
@@ -1236,7 +1238,7 @@ void mce_log_therm_throt_event(__u64 status)
 static unsigned long check_interval = INITIAL_CHECK_INTERVAL;
 
 static DEFINE_PER_CPU(unsigned long, mce_next_interval); /* in jiffies */
-static DEFINE_PER_CPU(struct timer_list, mce_timer);
+static DEFINE_PER_CPU(struct hrtimer, mce_timer);
 
 static unsigned long mce_adjust_timer_default(unsigned long interval)
 {
@@ -1245,32 +1247,18 @@ static unsigned long mce_adjust_timer_default(unsigned long interval)
 
 static unsigned long (*mce_adjust_timer)(unsigned long interval) = mce_adjust_timer_default;
 
-static void __restart_timer(struct timer_list *t, unsigned long interval)
+static enum hrtimer_restart __restart_timer(struct hrtimer *timer, unsigned long interval)
 {
-	unsigned long when = jiffies + interval;
-	unsigned long flags;
-
-	local_irq_save(flags);
-
-	if (timer_pending(t)) {
-		if (time_before(when, t->expires))
-			mod_timer_pinned(t, when);
-	} else {
-		t->expires = round_jiffies(when);
-		add_timer_on(t, smp_processor_id());
-	}
-
-	local_irq_restore(flags);
+	if (!interval)
+		return HRTIMER_NORESTART;
+	hrtimer_forward_now(timer, ns_to_ktime(jiffies_to_nsecs(interval)));
+	return HRTIMER_RESTART;
 }
 
-static void mce_timer_fn(unsigned long data)
+static enum hrtimer_restart mce_timer_fn(struct hrtimer *timer)
 {
-	struct timer_list *t = this_cpu_ptr(&mce_timer);
-	int cpu = smp_processor_id();
 	unsigned long iv;
 
-	WARN_ON(cpu != data);
-
 	iv = __this_cpu_read(mce_next_interval);
 
 	if (mce_available(this_cpu_ptr(&cpu_info))) {
@@ -1293,7 +1281,7 @@ static void mce_timer_fn(unsigned long data)
 
 done:
 	__this_cpu_write(mce_next_interval, iv);
-	__restart_timer(t, iv);
+	return __restart_timer(timer, iv);
 }
 
 /*
@@ -1301,7 +1289,7 @@ static void mce_timer_fn(unsigned long data)
  */
 void mce_timer_kick(unsigned long interval)
 {
-	struct timer_list *t = this_cpu_ptr(&mce_timer);
+	struct hrtimer *t = this_cpu_ptr(&mce_timer);
 	unsigned long iv = __this_cpu_read(mce_next_interval);
 
 	__restart_timer(t, interval);
@@ -1316,7 +1304,7 @@ static void mce_timer_delete_all(void)
 	int cpu;
 
 	for_each_online_cpu(cpu)
-		del_timer_sync(&per_cpu(mce_timer, cpu));
+		hrtimer_cancel(&per_cpu(mce_timer, cpu));
 }
 
 static void mce_do_trigger(struct work_struct *work)
@@ -1326,6 +1314,56 @@ static void mce_do_trigger(struct work_struct *work)
 
 static DECLARE_WORK(mce_trigger_work, mce_do_trigger);
 
+static void __mce_notify_work(struct swork_event *event)
+{
+	/* Not more than two messages every minute */
+	static DEFINE_RATELIMIT_STATE(ratelimit, 60*HZ, 2);
+
+	/* wake processes polling /dev/mcelog */
+	wake_up_interruptible(&mce_chrdev_wait);
+
+	/*
+	 * There is no risk of missing notifications because
+	 * work_pending is always cleared before the function is
+	 * executed.
+	 */
+	if (mce_helper[0] && !work_pending(&mce_trigger_work))
+		schedule_work(&mce_trigger_work);
+
+	if (__ratelimit(&ratelimit))
+		pr_info(HW_ERR "Machine check events logged\n");
+}
+
+#ifdef CONFIG_PREEMPT_RT_FULL
+static bool notify_work_ready __read_mostly;
+static struct swork_event notify_work;
+
+static int mce_notify_work_init(void)
+{
+	int err;
+
+	err = swork_get();
+	if (err)
+		return err;
+
+	INIT_SWORK(&notify_work, __mce_notify_work);
+	notify_work_ready = true;
+	return 0;
+}
+
+static void mce_notify_work(void)
+{
+	if (notify_work_ready)
+		swork_queue(&notify_work);
+}
+#else
+static void mce_notify_work(void)
+{
+	__mce_notify_work(NULL);
+}
+static inline int mce_notify_work_init(void) { return 0; }
+#endif
+
 /*
  * Notify the user(s) about new machine check events.
  * Can be called from interrupt context, but not from machine check/NMI
@@ -1333,19 +1371,8 @@ static DECLARE_WORK(mce_trigger_work, mce_do_trigger);
  */
 int mce_notify_irq(void)
 {
-	/* Not more than two messages every minute */
-	static DEFINE_RATELIMIT_STATE(ratelimit, 60*HZ, 2);
-
 	if (test_and_clear_bit(0, &mce_need_notify)) {
-		/* wake processes polling /dev/mcelog */
-		wake_up_interruptible(&mce_chrdev_wait);
-
-		if (mce_helper[0])
-			schedule_work(&mce_trigger_work);
-
-		if (__ratelimit(&ratelimit))
-			pr_info(HW_ERR "Machine check events logged\n");
-
+		mce_notify_work();
 		return 1;
 	}
 	return 0;
@@ -1639,7 +1666,7 @@ static void __mcheck_cpu_clear_vendor(struct cpuinfo_x86 *c)
 	}
 }
 
-static void mce_start_timer(unsigned int cpu, struct timer_list *t)
+static void mce_start_timer(unsigned int cpu, struct hrtimer *t)
 {
 	unsigned long iv = check_interval * HZ;
 
@@ -1648,16 +1675,17 @@ static void mce_start_timer(unsigned int cpu, struct timer_list *t)
 
 	per_cpu(mce_next_interval, cpu) = iv;
 
-	t->expires = round_jiffies(jiffies + iv);
-	add_timer_on(t, cpu);
+	hrtimer_start_range_ns(t, ns_to_ktime(jiffies_to_usecs(iv) * 1000ULL),
+			0, HRTIMER_MODE_REL_PINNED);
 }
 
 static void __mcheck_cpu_init_timer(void)
 {
-	struct timer_list *t = this_cpu_ptr(&mce_timer);
+	struct hrtimer *t = this_cpu_ptr(&mce_timer);
 	unsigned int cpu = smp_processor_id();
 
-	setup_timer(t, mce_timer_fn, cpu);
+	hrtimer_init(t, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
+	t->function = mce_timer_fn;
 	mce_start_timer(cpu, t);
 }
 
@@ -2376,6 +2404,8 @@ static void mce_disable_cpu(void *h)
 	if (!mce_available(raw_cpu_ptr(&cpu_info)))
 		return;
 
+	hrtimer_cancel(this_cpu_ptr(&mce_timer));
+
 	if (!(action & CPU_TASKS_FROZEN))
 		cmci_clear();
 
@@ -2398,6 +2428,7 @@ static void mce_reenable_cpu(void *h)
 		if (b->init)
 			wrmsrl(MSR_IA32_MCx_CTL(i), b->ctl);
 	}
+	__mcheck_cpu_init_timer();
 }
 
 /* Get notified when a cpu comes on/off. Be hotplug friendly. */
@@ -2405,7 +2436,6 @@ static int
 mce_cpu_callback(struct notifier_block *nfb, unsigned long action, void *hcpu)
 {
 	unsigned int cpu = (unsigned long)hcpu;
-	struct timer_list *t = &per_cpu(mce_timer, cpu);
 
 	switch (action & ~CPU_TASKS_FROZEN) {
 	case CPU_ONLINE:
@@ -2425,11 +2455,9 @@ mce_cpu_callback(struct notifier_block *nfb, unsigned long action, void *hcpu)
 		break;
 	case CPU_DOWN_PREPARE:
 		smp_call_function_single(cpu, mce_disable_cpu, &action, 1);
-		del_timer_sync(t);
 		break;
 	case CPU_DOWN_FAILED:
 		smp_call_function_single(cpu, mce_reenable_cpu, &action, 1);
-		mce_start_timer(cpu, t);
 		break;
 	}
 
@@ -2468,6 +2496,10 @@ static __init int mcheck_init_device(void)
 		goto err_out;
 	}
 
+	err = mce_notify_work_init();
+	if (err)
+		goto err_out;
+
 	if (!zalloc_cpumask_var(&mce_device_initialized, GFP_KERNEL)) {
 		err = -ENOMEM;
 		goto err_out;
diff --git a/arch/x86/kernel/dumpstack_32.c b/arch/x86/kernel/dumpstack_32.c
index 464ffd69b92e..00db1aad1548 100644
--- a/arch/x86/kernel/dumpstack_32.c
+++ b/arch/x86/kernel/dumpstack_32.c
@@ -42,7 +42,7 @@ void dump_trace(struct task_struct *task, struct pt_regs *regs,
 		unsigned long *stack, unsigned long bp,
 		const struct stacktrace_ops *ops, void *data)
 {
-	const unsigned cpu = get_cpu();
+	const unsigned cpu = get_cpu_light();
 	int graph = 0;
 	u32 *prev_esp;
 
@@ -86,7 +86,7 @@ void dump_trace(struct task_struct *task, struct pt_regs *regs,
 			break;
 		touch_nmi_watchdog();
 	}
-	put_cpu();
+	put_cpu_light();
 }
 EXPORT_SYMBOL(dump_trace);
 
diff --git a/arch/x86/kernel/dumpstack_64.c b/arch/x86/kernel/dumpstack_64.c
index 5f1c6266eb30..c331e3fef465 100644
--- a/arch/x86/kernel/dumpstack_64.c
+++ b/arch/x86/kernel/dumpstack_64.c
@@ -152,7 +152,7 @@ void dump_trace(struct task_struct *task, struct pt_regs *regs,
 		unsigned long *stack, unsigned long bp,
 		const struct stacktrace_ops *ops, void *data)
 {
-	const unsigned cpu = get_cpu();
+	const unsigned cpu = get_cpu_light();
 	struct thread_info *tinfo;
 	unsigned long *irq_stack = (unsigned long *)per_cpu(irq_stack_ptr, cpu);
 	unsigned long dummy;
@@ -241,7 +241,7 @@ void dump_trace(struct task_struct *task, struct pt_regs *regs,
 	 * This handles the process stack:
 	 */
 	bp = ops->walk_stack(tinfo, stack, bp, ops, data, NULL, &graph);
-	put_cpu();
+	put_cpu_light();
 }
 EXPORT_SYMBOL(dump_trace);
 
@@ -255,7 +255,7 @@ show_stack_log_lvl(struct task_struct *task, struct pt_regs *regs,
 	int cpu;
 	int i;
 
-	preempt_disable();
+	migrate_disable();
 	cpu = smp_processor_id();
 
 	irq_stack_end	= (unsigned long *)(per_cpu(irq_stack_ptr, cpu));
@@ -291,7 +291,7 @@ show_stack_log_lvl(struct task_struct *task, struct pt_regs *regs,
 			pr_cont(" %016lx", *stack++);
 		touch_nmi_watchdog();
 	}
-	preempt_enable();
+	migrate_enable();
 
 	pr_cont("\n");
 	show_trace_log_lvl(task, regs, sp, bp, log_lvl);
diff --git a/arch/x86/kernel/irq_32.c b/arch/x86/kernel/irq_32.c
index 38da8f29a9c8..ce71f7098f15 100644
--- a/arch/x86/kernel/irq_32.c
+++ b/arch/x86/kernel/irq_32.c
@@ -128,6 +128,7 @@ void irq_ctx_init(int cpu)
 	       cpu, per_cpu(hardirq_stack, cpu),  per_cpu(softirq_stack, cpu));
 }
 
+#ifndef CONFIG_PREEMPT_RT_FULL
 void do_softirq_own_stack(void)
 {
 	struct thread_info *curstk;
@@ -146,6 +147,7 @@ void do_softirq_own_stack(void)
 
 	call_on_stack(__do_softirq, isp);
 }
+#endif
 
 bool handle_irq(struct irq_desc *desc, struct pt_regs *regs)
 {
diff --git a/arch/x86/kernel/process_32.c b/arch/x86/kernel/process_32.c
index 9f950917528b..4dd4beae917a 100644
--- a/arch/x86/kernel/process_32.c
+++ b/arch/x86/kernel/process_32.c
@@ -35,6 +35,7 @@
 #include <linux/uaccess.h>
 #include <linux/io.h>
 #include <linux/kdebug.h>
+#include <linux/highmem.h>
 
 #include <asm/pgtable.h>
 #include <asm/ldt.h>
@@ -210,6 +211,35 @@ start_thread(struct pt_regs *regs, unsigned long new_ip, unsigned long new_sp)
 }
 EXPORT_SYMBOL_GPL(start_thread);
 
+#ifdef CONFIG_PREEMPT_RT_FULL
+static void switch_kmaps(struct task_struct *prev_p, struct task_struct *next_p)
+{
+	int i;
+
+	/*
+	 * Clear @prev's kmap_atomic mappings
+	 */
+	for (i = 0; i < prev_p->kmap_idx; i++) {
+		int idx = i + KM_TYPE_NR * smp_processor_id();
+		pte_t *ptep = kmap_pte - idx;
+
+		kpte_clear_flush(ptep, __fix_to_virt(FIX_KMAP_BEGIN + idx));
+	}
+	/*
+	 * Restore @next_p's kmap_atomic mappings
+	 */
+	for (i = 0; i < next_p->kmap_idx; i++) {
+		int idx = i + KM_TYPE_NR * smp_processor_id();
+
+		if (!pte_none(next_p->kmap_pte[i]))
+			set_pte(kmap_pte - idx, next_p->kmap_pte[i]);
+	}
+}
+#else
+static inline void
+switch_kmaps(struct task_struct *prev_p, struct task_struct *next_p) { }
+#endif
+
 
 /*
  *	switch_to(x,y) should switch tasks from x to y.
@@ -286,6 +316,8 @@ __switch_to(struct task_struct *prev_p, struct task_struct *next_p)
 		     task_thread_info(next_p)->flags & _TIF_WORK_CTXSW_NEXT))
 		__switch_to_xtra(prev_p, next_p, tss);
 
+	switch_kmaps(prev_p, next_p);
+
 	/*
 	 * Leave lazy mode, flushing any hypercalls made here.
 	 * This must be done before restoring TLS segments so
diff --git a/arch/x86/kvm/lapic.c b/arch/x86/kvm/lapic.c
index 4d30b865be30..906a1ecb7606 100644
--- a/arch/x86/kvm/lapic.c
+++ b/arch/x86/kvm/lapic.c
@@ -1195,7 +1195,7 @@ static void apic_update_lvtt(struct kvm_lapic *apic)
 static void apic_timer_expired(struct kvm_lapic *apic)
 {
 	struct kvm_vcpu *vcpu = apic->vcpu;
-	wait_queue_head_t *q = &vcpu->wq;
+	struct swait_head *q = &vcpu->wq;
 	struct kvm_timer *ktimer = &apic->lapic_timer;
 
 	if (atomic_read(&apic->lapic_timer.pending))
@@ -1204,8 +1204,8 @@ static void apic_timer_expired(struct kvm_lapic *apic)
 	atomic_inc(&apic->lapic_timer.pending);
 	kvm_set_pending_timer(vcpu);
 
-	if (waitqueue_active(q))
-		wake_up_interruptible(q);
+	if (swaitqueue_active(q))
+		swait_wake_interruptible(q);
 
 	if (apic_lvtt_tscdeadline(apic))
 		ktimer->expired_tscdeadline = ktimer->tscdeadline;
@@ -1801,6 +1801,7 @@ int kvm_create_lapic(struct kvm_vcpu *vcpu)
 	hrtimer_init(&apic->lapic_timer.timer, CLOCK_MONOTONIC,
 		     HRTIMER_MODE_ABS);
 	apic->lapic_timer.timer.function = apic_timer_fn;
+	apic->lapic_timer.timer.irqsafe = 1;
 
 	/*
 	 * APIC is created enabled. This will prevent kvm_lapic_set_base from
diff --git a/arch/x86/kvm/x86.c b/arch/x86/kvm/x86.c
index 97592e190413..9f685677ebd7 100644
--- a/arch/x86/kvm/x86.c
+++ b/arch/x86/kvm/x86.c
@@ -5787,6 +5787,13 @@ int kvm_arch_init(void *opaque)
 		goto out;
 	}
 
+#ifdef CONFIG_PREEMPT_RT_FULL
+	if (!boot_cpu_has(X86_FEATURE_CONSTANT_TSC)) {
+		printk(KERN_ERR "RT requires X86_FEATURE_CONSTANT_TSC\n");
+		return -EOPNOTSUPP;
+	}
+#endif
+
 	r = kvm_mmu_module_init();
 	if (r)
 		goto out_free_percpu;
diff --git a/arch/x86/mm/highmem_32.c b/arch/x86/mm/highmem_32.c
index a6d739258137..bd24ba1c4a86 100644
--- a/arch/x86/mm/highmem_32.c
+++ b/arch/x86/mm/highmem_32.c
@@ -32,10 +32,11 @@ EXPORT_SYMBOL(kunmap);
  */
 void *kmap_atomic_prot(struct page *page, pgprot_t prot)
 {
+	pte_t pte = mk_pte(page, prot);
 	unsigned long vaddr;
 	int idx, type;
 
-	preempt_disable();
+	preempt_disable_nort();
 	pagefault_disable();
 
 	if (!PageHighMem(page))
@@ -45,7 +46,10 @@ void *kmap_atomic_prot(struct page *page, pgprot_t prot)
 	idx = type + KM_TYPE_NR*smp_processor_id();
 	vaddr = __fix_to_virt(FIX_KMAP_BEGIN + idx);
 	BUG_ON(!pte_none(*(kmap_pte-idx)));
-	set_pte(kmap_pte-idx, mk_pte(page, prot));
+#ifdef CONFIG_PREEMPT_RT_FULL
+	current->kmap_pte[type] = pte;
+#endif
+	set_pte(kmap_pte-idx, pte);
 	arch_flush_lazy_mmu_mode();
 
 	return (void *)vaddr;
@@ -88,6 +92,9 @@ void __kunmap_atomic(void *kvaddr)
 		 * is a bad idea also, in case the page changes cacheability
 		 * attributes or becomes a protected page in a hypervisor.
 		 */
+#ifdef CONFIG_PREEMPT_RT_FULL
+		current->kmap_pte[type] = __pte(0);
+#endif
 		kpte_clear_flush(kmap_pte-idx, vaddr);
 		kmap_atomic_idx_pop();
 		arch_flush_lazy_mmu_mode();
@@ -100,7 +107,7 @@ void __kunmap_atomic(void *kvaddr)
 #endif
 
 	pagefault_enable();
-	preempt_enable();
+	preempt_enable_nort();
 }
 EXPORT_SYMBOL(__kunmap_atomic);
 
diff --git a/arch/x86/mm/iomap_32.c b/arch/x86/mm/iomap_32.c
index 9c0ff045fdd4..dd25dd1671b6 100644
--- a/arch/x86/mm/iomap_32.c
+++ b/arch/x86/mm/iomap_32.c
@@ -56,6 +56,7 @@ EXPORT_SYMBOL_GPL(iomap_free);
 
 void *kmap_atomic_prot_pfn(unsigned long pfn, pgprot_t prot)
 {
+	pte_t pte = pfn_pte(pfn, prot);
 	unsigned long vaddr;
 	int idx, type;
 
@@ -65,7 +66,12 @@ void *kmap_atomic_prot_pfn(unsigned long pfn, pgprot_t prot)
 	type = kmap_atomic_idx_push();
 	idx = type + KM_TYPE_NR * smp_processor_id();
 	vaddr = __fix_to_virt(FIX_KMAP_BEGIN + idx);
-	set_pte(kmap_pte - idx, pfn_pte(pfn, prot));
+	WARN_ON(!pte_none(*(kmap_pte - idx)));
+
+#ifdef CONFIG_PREEMPT_RT_FULL
+	current->kmap_pte[type] = pte;
+#endif
+	set_pte(kmap_pte - idx, pte);
 	arch_flush_lazy_mmu_mode();
 
 	return (void *)vaddr;
@@ -113,6 +119,9 @@ iounmap_atomic(void __iomem *kvaddr)
 		 * is a bad idea also, in case the page changes cacheability
 		 * attributes or becomes a protected page in a hypervisor.
 		 */
+#ifdef CONFIG_PREEMPT_RT_FULL
+		current->kmap_pte[type] = __pte(0);
+#endif
 		kpte_clear_flush(kmap_pte-idx, vaddr);
 		kmap_atomic_idx_pop();
 	}
diff --git a/arch/x86/platform/uv/tlb_uv.c b/arch/x86/platform/uv/tlb_uv.c
index 3b6ec42718e4..7871083de089 100644
--- a/arch/x86/platform/uv/tlb_uv.c
+++ b/arch/x86/platform/uv/tlb_uv.c
@@ -714,9 +714,9 @@ static void destination_plugged(struct bau_desc *bau_desc,
 
 		quiesce_local_uvhub(hmaster);
 
-		spin_lock(&hmaster->queue_lock);
+		raw_spin_lock(&hmaster->queue_lock);
 		reset_with_ipi(&bau_desc->distribution, bcp);
-		spin_unlock(&hmaster->queue_lock);
+		raw_spin_unlock(&hmaster->queue_lock);
 
 		end_uvhub_quiesce(hmaster);
 
@@ -736,9 +736,9 @@ static void destination_timeout(struct bau_desc *bau_desc,
 
 		quiesce_local_uvhub(hmaster);
 
-		spin_lock(&hmaster->queue_lock);
+		raw_spin_lock(&hmaster->queue_lock);
 		reset_with_ipi(&bau_desc->distribution, bcp);
-		spin_unlock(&hmaster->queue_lock);
+		raw_spin_unlock(&hmaster->queue_lock);
 
 		end_uvhub_quiesce(hmaster);
 
@@ -759,7 +759,7 @@ static void disable_for_period(struct bau_control *bcp, struct ptc_stats *stat)
 	cycles_t tm1;
 
 	hmaster = bcp->uvhub_master;
-	spin_lock(&hmaster->disable_lock);
+	raw_spin_lock(&hmaster->disable_lock);
 	if (!bcp->baudisabled) {
 		stat->s_bau_disabled++;
 		tm1 = get_cycles();
@@ -772,7 +772,7 @@ static void disable_for_period(struct bau_control *bcp, struct ptc_stats *stat)
 			}
 		}
 	}
-	spin_unlock(&hmaster->disable_lock);
+	raw_spin_unlock(&hmaster->disable_lock);
 }
 
 static void count_max_concurr(int stat, struct bau_control *bcp,
@@ -835,7 +835,7 @@ static void record_send_stats(cycles_t time1, cycles_t time2,
  */
 static void uv1_throttle(struct bau_control *hmaster, struct ptc_stats *stat)
 {
-	spinlock_t *lock = &hmaster->uvhub_lock;
+	raw_spinlock_t *lock = &hmaster->uvhub_lock;
 	atomic_t *v;
 
 	v = &hmaster->active_descriptor_count;
@@ -968,7 +968,7 @@ static int check_enable(struct bau_control *bcp, struct ptc_stats *stat)
 	struct bau_control *hmaster;
 
 	hmaster = bcp->uvhub_master;
-	spin_lock(&hmaster->disable_lock);
+	raw_spin_lock(&hmaster->disable_lock);
 	if (bcp->baudisabled && (get_cycles() >= bcp->set_bau_on_time)) {
 		stat->s_bau_reenabled++;
 		for_each_present_cpu(tcpu) {
@@ -980,10 +980,10 @@ static int check_enable(struct bau_control *bcp, struct ptc_stats *stat)
 				tbcp->period_giveups = 0;
 			}
 		}
-		spin_unlock(&hmaster->disable_lock);
+		raw_spin_unlock(&hmaster->disable_lock);
 		return 0;
 	}
-	spin_unlock(&hmaster->disable_lock);
+	raw_spin_unlock(&hmaster->disable_lock);
 	return -1;
 }
 
@@ -1901,9 +1901,9 @@ static void __init init_per_cpu_tunables(void)
 		bcp->cong_reps			= congested_reps;
 		bcp->disabled_period =		sec_2_cycles(disabled_period);
 		bcp->giveup_limit =		giveup_limit;
-		spin_lock_init(&bcp->queue_lock);
-		spin_lock_init(&bcp->uvhub_lock);
-		spin_lock_init(&bcp->disable_lock);
+		raw_spin_lock_init(&bcp->queue_lock);
+		raw_spin_lock_init(&bcp->uvhub_lock);
+		raw_spin_lock_init(&bcp->disable_lock);
 	}
 }
 
diff --git a/arch/x86/platform/uv/uv_time.c b/arch/x86/platform/uv/uv_time.c
index 2b158a9fa1d7..5e0b122620cb 100644
--- a/arch/x86/platform/uv/uv_time.c
+++ b/arch/x86/platform/uv/uv_time.c
@@ -57,7 +57,7 @@ static DEFINE_PER_CPU(struct clock_event_device, cpu_ced);
 
 /* There is one of these allocated per node */
 struct uv_rtc_timer_head {
-	spinlock_t	lock;
+	raw_spinlock_t	lock;
 	/* next cpu waiting for timer, local node relative: */
 	int		next_cpu;
 	/* number of cpus on this node: */
@@ -177,7 +177,7 @@ static __init int uv_rtc_allocate_timers(void)
 				uv_rtc_deallocate_timers();
 				return -ENOMEM;
 			}
-			spin_lock_init(&head->lock);
+			raw_spin_lock_init(&head->lock);
 			head->ncpus = uv_blade_nr_possible_cpus(bid);
 			head->next_cpu = -1;
 			blade_info[bid] = head;
@@ -231,7 +231,7 @@ static int uv_rtc_set_timer(int cpu, u64 expires)
 	unsigned long flags;
 	int next_cpu;
 
-	spin_lock_irqsave(&head->lock, flags);
+	raw_spin_lock_irqsave(&head->lock, flags);
 
 	next_cpu = head->next_cpu;
 	*t = expires;
@@ -243,12 +243,12 @@ static int uv_rtc_set_timer(int cpu, u64 expires)
 		if (uv_setup_intr(cpu, expires)) {
 			*t = ULLONG_MAX;
 			uv_rtc_find_next_timer(head, pnode);
-			spin_unlock_irqrestore(&head->lock, flags);
+			raw_spin_unlock_irqrestore(&head->lock, flags);
 			return -ETIME;
 		}
 	}
 
-	spin_unlock_irqrestore(&head->lock, flags);
+	raw_spin_unlock_irqrestore(&head->lock, flags);
 	return 0;
 }
 
@@ -267,7 +267,7 @@ static int uv_rtc_unset_timer(int cpu, int force)
 	unsigned long flags;
 	int rc = 0;
 
-	spin_lock_irqsave(&head->lock, flags);
+	raw_spin_lock_irqsave(&head->lock, flags);
 
 	if ((head->next_cpu == bcpu && uv_read_rtc(NULL) >= *t) || force)
 		rc = 1;
@@ -279,7 +279,7 @@ static int uv_rtc_unset_timer(int cpu, int force)
 			uv_rtc_find_next_timer(head, pnode);
 	}
 
-	spin_unlock_irqrestore(&head->lock, flags);
+	raw_spin_unlock_irqrestore(&head->lock, flags);
 
 	return rc;
 }
@@ -299,13 +299,18 @@ static int uv_rtc_unset_timer(int cpu, int force)
 static cycle_t uv_read_rtc(struct clocksource *cs)
 {
 	unsigned long offset;
+	cycle_t cycles;
 
+	preempt_disable();
 	if (uv_get_min_hub_revision_id() == 1)
 		offset = 0;
 	else
 		offset = (uv_blade_processor_id() * L1_CACHE_BYTES) % PAGE_SIZE;
 
-	return (cycle_t)uv_read_local_mmr(UVH_RTC | offset);
+	cycles = (cycle_t)uv_read_local_mmr(UVH_RTC | offset);
+	preempt_enable();
+
+	return cycles;
 }
 
 /*
diff --git a/block/blk-core.c b/block/blk-core.c
index 33e2f62d5062..4c8cefd2c019 100644
--- a/block/blk-core.c
+++ b/block/blk-core.c
@@ -125,6 +125,9 @@ void blk_rq_init(struct request_queue *q, struct request *rq)
 
 	INIT_LIST_HEAD(&rq->queuelist);
 	INIT_LIST_HEAD(&rq->timeout_list);
+#ifdef CONFIG_PREEMPT_RT_FULL
+	INIT_WORK(&rq->work, __blk_mq_complete_request_remote_work);
+#endif
 	rq->cpu = -1;
 	rq->q = q;
 	rq->__sector = (sector_t) -1;
@@ -233,7 +236,7 @@ EXPORT_SYMBOL(blk_start_queue_async);
  **/
 void blk_start_queue(struct request_queue *q)
 {
-	WARN_ON(!irqs_disabled());
+	WARN_ON_NONRT(!irqs_disabled());
 
 	queue_flag_clear(QUEUE_FLAG_STOPPED, q);
 	__blk_run_queue(q);
@@ -657,7 +660,7 @@ int blk_queue_enter(struct request_queue *q, gfp_t gfp)
 		if (!gfpflags_allow_blocking(gfp))
 			return -EBUSY;
 
-		ret = wait_event_interruptible(q->mq_freeze_wq,
+		ret = swait_event_interruptible(q->mq_freeze_wq,
 				!atomic_read(&q->mq_freeze_depth) ||
 				blk_queue_dying(q));
 		if (blk_queue_dying(q))
@@ -677,7 +680,7 @@ static void blk_queue_usage_counter_release(struct percpu_ref *ref)
 	struct request_queue *q =
 		container_of(ref, struct request_queue, q_usage_counter);
 
-	wake_up_all(&q->mq_freeze_wq);
+	swait_wake_all(&q->mq_freeze_wq);
 }
 
 struct request_queue *blk_alloc_queue_node(gfp_t gfp_mask, int node_id)
@@ -739,7 +742,7 @@ struct request_queue *blk_alloc_queue_node(gfp_t gfp_mask, int node_id)
 	q->bypass_depth = 1;
 	__set_bit(QUEUE_FLAG_BYPASS, &q->queue_flags);
 
-	init_waitqueue_head(&q->mq_freeze_wq);
+	init_swait_head(&q->mq_freeze_wq);
 
 	/*
 	 * Init percpu_ref in atomic mode so that it's faster to shutdown.
@@ -3198,7 +3201,7 @@ static void queue_unplugged(struct request_queue *q, unsigned int depth,
 		blk_run_queue_async(q);
 	else
 		__blk_run_queue(q);
-	spin_unlock(q->queue_lock);
+	spin_unlock_irq(q->queue_lock);
 }
 
 static void flush_plug_callbacks(struct blk_plug *plug, bool from_schedule)
@@ -3246,7 +3249,6 @@ EXPORT_SYMBOL(blk_check_plugged);
 void blk_flush_plug_list(struct blk_plug *plug, bool from_schedule)
 {
 	struct request_queue *q;
-	unsigned long flags;
 	struct request *rq;
 	LIST_HEAD(list);
 	unsigned int depth;
@@ -3266,11 +3268,6 @@ void blk_flush_plug_list(struct blk_plug *plug, bool from_schedule)
 	q = NULL;
 	depth = 0;
 
-	/*
-	 * Save and disable interrupts here, to avoid doing it for every
-	 * queue lock we have to take.
-	 */
-	local_irq_save(flags);
 	while (!list_empty(&list)) {
 		rq = list_entry_rq(list.next);
 		list_del_init(&rq->queuelist);
@@ -3283,7 +3280,7 @@ void blk_flush_plug_list(struct blk_plug *plug, bool from_schedule)
 				queue_unplugged(q, depth, from_schedule);
 			q = rq->q;
 			depth = 0;
-			spin_lock(q->queue_lock);
+			spin_lock_irq(q->queue_lock);
 		}
 
 		/*
@@ -3310,8 +3307,6 @@ void blk_flush_plug_list(struct blk_plug *plug, bool from_schedule)
 	 */
 	if (q)
 		queue_unplugged(q, depth, from_schedule);
-
-	local_irq_restore(flags);
 }
 
 void blk_finish_plug(struct blk_plug *plug)
diff --git a/block/blk-ioc.c b/block/blk-ioc.c
index 381cb50a673c..dc8785233d94 100644
--- a/block/blk-ioc.c
+++ b/block/blk-ioc.c
@@ -7,6 +7,7 @@
 #include <linux/bio.h>
 #include <linux/blkdev.h>
 #include <linux/slab.h>
+#include <linux/delay.h>
 
 #include "blk.h"
 
@@ -109,7 +110,7 @@ static void ioc_release_fn(struct work_struct *work)
 			spin_unlock(q->queue_lock);
 		} else {
 			spin_unlock_irqrestore(&ioc->lock, flags);
-			cpu_relax();
+			cpu_chill();
 			spin_lock_irqsave_nested(&ioc->lock, flags, 1);
 		}
 	}
@@ -187,7 +188,7 @@ void put_io_context_active(struct io_context *ioc)
 			spin_unlock(icq->q->queue_lock);
 		} else {
 			spin_unlock_irqrestore(&ioc->lock, flags);
-			cpu_relax();
+			cpu_chill();
 			goto retry;
 		}
 	}
diff --git a/block/blk-iopoll.c b/block/blk-iopoll.c
index 0736729d6494..3e21e31d0d7e 100644
--- a/block/blk-iopoll.c
+++ b/block/blk-iopoll.c
@@ -35,6 +35,7 @@ void blk_iopoll_sched(struct blk_iopoll *iop)
 	list_add_tail(&iop->list, this_cpu_ptr(&blk_cpu_iopoll));
 	__raise_softirq_irqoff(BLOCK_IOPOLL_SOFTIRQ);
 	local_irq_restore(flags);
+	preempt_check_resched_rt();
 }
 EXPORT_SYMBOL(blk_iopoll_sched);
 
@@ -132,6 +133,7 @@ static void blk_iopoll_softirq(struct softirq_action *h)
 		__raise_softirq_irqoff(BLOCK_IOPOLL_SOFTIRQ);
 
 	local_irq_enable();
+	preempt_check_resched_rt();
 }
 
 /**
@@ -201,6 +203,7 @@ static int blk_iopoll_cpu_notify(struct notifier_block *self,
 				 this_cpu_ptr(&blk_cpu_iopoll));
 		__raise_softirq_irqoff(BLOCK_IOPOLL_SOFTIRQ);
 		local_irq_enable();
+		preempt_check_resched_rt();
 	}
 
 	return NOTIFY_OK;
diff --git a/block/blk-mq-cpu.c b/block/blk-mq-cpu.c
index bb3ed488f7b5..628c6c13c482 100644
--- a/block/blk-mq-cpu.c
+++ b/block/blk-mq-cpu.c
@@ -16,7 +16,7 @@
 #include "blk-mq.h"
 
 static LIST_HEAD(blk_mq_cpu_notify_list);
-static DEFINE_RAW_SPINLOCK(blk_mq_cpu_notify_lock);
+static DEFINE_SPINLOCK(blk_mq_cpu_notify_lock);
 
 static int blk_mq_main_cpu_notify(struct notifier_block *self,
 				  unsigned long action, void *hcpu)
@@ -25,7 +25,10 @@ static int blk_mq_main_cpu_notify(struct notifier_block *self,
 	struct blk_mq_cpu_notifier *notify;
 	int ret = NOTIFY_OK;
 
-	raw_spin_lock(&blk_mq_cpu_notify_lock);
+	if (action != CPU_POST_DEAD)
+		return NOTIFY_OK;
+
+	spin_lock(&blk_mq_cpu_notify_lock);
 
 	list_for_each_entry(notify, &blk_mq_cpu_notify_list, list) {
 		ret = notify->notify(notify->data, action, cpu);
@@ -33,7 +36,7 @@ static int blk_mq_main_cpu_notify(struct notifier_block *self,
 			break;
 	}
 
-	raw_spin_unlock(&blk_mq_cpu_notify_lock);
+	spin_unlock(&blk_mq_cpu_notify_lock);
 	return ret;
 }
 
@@ -41,16 +44,16 @@ void blk_mq_register_cpu_notifier(struct blk_mq_cpu_notifier *notifier)
 {
 	BUG_ON(!notifier->notify);
 
-	raw_spin_lock(&blk_mq_cpu_notify_lock);
+	spin_lock(&blk_mq_cpu_notify_lock);
 	list_add_tail(&notifier->list, &blk_mq_cpu_notify_list);
-	raw_spin_unlock(&blk_mq_cpu_notify_lock);
+	spin_unlock(&blk_mq_cpu_notify_lock);
 }
 
 void blk_mq_unregister_cpu_notifier(struct blk_mq_cpu_notifier *notifier)
 {
-	raw_spin_lock(&blk_mq_cpu_notify_lock);
+	spin_lock(&blk_mq_cpu_notify_lock);
 	list_del(&notifier->list);
-	raw_spin_unlock(&blk_mq_cpu_notify_lock);
+	spin_unlock(&blk_mq_cpu_notify_lock);
 }
 
 void blk_mq_init_cpu_notifier(struct blk_mq_cpu_notifier *notifier,
diff --git a/block/blk-mq.c b/block/blk-mq.c
index 6d6f8feb48c0..3c70813cb6b6 100644
--- a/block/blk-mq.c
+++ b/block/blk-mq.c
@@ -92,7 +92,7 @@ EXPORT_SYMBOL_GPL(blk_mq_freeze_queue_start);
 
 static void blk_mq_freeze_queue_wait(struct request_queue *q)
 {
-	wait_event(q->mq_freeze_wq, percpu_ref_is_zero(&q->q_usage_counter));
+	swait_event(q->mq_freeze_wq, percpu_ref_is_zero(&q->q_usage_counter));
 }
 
 /*
@@ -130,7 +130,7 @@ void blk_mq_unfreeze_queue(struct request_queue *q)
 	WARN_ON_ONCE(freeze_depth < 0);
 	if (!freeze_depth) {
 		percpu_ref_reinit(&q->q_usage_counter);
-		wake_up_all(&q->mq_freeze_wq);
+		swait_wake_all(&q->mq_freeze_wq);
 	}
 }
 EXPORT_SYMBOL_GPL(blk_mq_unfreeze_queue);
@@ -149,7 +149,7 @@ void blk_mq_wake_waiters(struct request_queue *q)
 	 * dying, we need to ensure that processes currently waiting on
 	 * the queue are notified as well.
 	 */
-	wake_up_all(&q->mq_freeze_wq);
+	swait_wake_all(&q->mq_freeze_wq);
 }
 
 bool blk_mq_can_queue(struct blk_mq_hw_ctx *hctx)
@@ -196,6 +196,9 @@ static void blk_mq_rq_ctx_init(struct request_queue *q, struct blk_mq_ctx *ctx,
 	rq->resid_len = 0;
 	rq->sense = NULL;
 
+#ifdef CONFIG_PREEMPT_RT_FULL
+	INIT_WORK(&rq->work, __blk_mq_complete_request_remote_work);
+#endif
 	INIT_LIST_HEAD(&rq->timeout_list);
 	rq->timeout = 0;
 
@@ -325,6 +328,17 @@ void blk_mq_end_request(struct request *rq, int error)
 }
 EXPORT_SYMBOL(blk_mq_end_request);
 
+#ifdef CONFIG_PREEMPT_RT_FULL
+
+void __blk_mq_complete_request_remote_work(struct work_struct *work)
+{
+	struct request *rq = container_of(work, struct request, work);
+
+	rq->q->softirq_done_fn(rq);
+}
+
+#else
+
 static void __blk_mq_complete_request_remote(void *data)
 {
 	struct request *rq = data;
@@ -332,6 +346,8 @@ static void __blk_mq_complete_request_remote(void *data)
 	rq->q->softirq_done_fn(rq);
 }
 
+#endif
+
 static void blk_mq_ipi_complete_request(struct request *rq)
 {
 	struct blk_mq_ctx *ctx = rq->mq_ctx;
@@ -343,19 +359,23 @@ static void blk_mq_ipi_complete_request(struct request *rq)
 		return;
 	}
 
-	cpu = get_cpu();
+	cpu = get_cpu_light();
 	if (!test_bit(QUEUE_FLAG_SAME_FORCE, &rq->q->queue_flags))
 		shared = cpus_share_cache(cpu, ctx->cpu);
 
 	if (cpu != ctx->cpu && !shared && cpu_online(ctx->cpu)) {
+#ifdef CONFIG_PREEMPT_RT_FULL
+		schedule_work_on(ctx->cpu, &rq->work);
+#else
 		rq->csd.func = __blk_mq_complete_request_remote;
 		rq->csd.info = rq;
 		rq->csd.flags = 0;
 		smp_call_function_single_async(ctx->cpu, &rq->csd);
+#endif
 	} else {
 		rq->q->softirq_done_fn(rq);
 	}
-	put_cpu();
+	put_cpu_light();
 }
 
 static void __blk_mq_complete_request(struct request *rq)
@@ -862,14 +882,14 @@ void blk_mq_run_hw_queue(struct blk_mq_hw_ctx *hctx, bool async)
 		return;
 
 	if (!async) {
-		int cpu = get_cpu();
+		int cpu = get_cpu_light();
 		if (cpumask_test_cpu(cpu, hctx->cpumask)) {
 			__blk_mq_run_hw_queue(hctx);
-			put_cpu();
+			put_cpu_light();
 			return;
 		}
 
-		put_cpu();
+		put_cpu_light();
 	}
 
 	kblockd_schedule_delayed_work_on(blk_mq_hctx_next_cpu(hctx),
@@ -1617,7 +1637,7 @@ static int blk_mq_hctx_notify(void *data, unsigned long action,
 {
 	struct blk_mq_hw_ctx *hctx = data;
 
-	if (action == CPU_DEAD || action == CPU_DEAD_FROZEN)
+	if (action == CPU_POST_DEAD)
 		return blk_mq_hctx_cpu_offline(hctx, cpu);
 
 	/*
diff --git a/block/blk-mq.h b/block/blk-mq.h
index 713820b47b31..3cb6feb4fe23 100644
--- a/block/blk-mq.h
+++ b/block/blk-mq.h
@@ -74,7 +74,10 @@ struct blk_align_bitmap {
 static inline struct blk_mq_ctx *__blk_mq_get_ctx(struct request_queue *q,
 					   unsigned int cpu)
 {
-	return per_cpu_ptr(q->queue_ctx, cpu);
+	struct blk_mq_ctx *ctx;
+
+	ctx = per_cpu_ptr(q->queue_ctx, cpu);
+	return ctx;
 }
 
 /*
@@ -85,12 +88,12 @@ static inline struct blk_mq_ctx *__blk_mq_get_ctx(struct request_queue *q,
  */
 static inline struct blk_mq_ctx *blk_mq_get_ctx(struct request_queue *q)
 {
-	return __blk_mq_get_ctx(q, get_cpu());
+	return __blk_mq_get_ctx(q, get_cpu_light());
 }
 
 static inline void blk_mq_put_ctx(struct blk_mq_ctx *ctx)
 {
-	put_cpu();
+	put_cpu_light();
 }
 
 struct blk_mq_alloc_data {
diff --git a/block/blk-softirq.c b/block/blk-softirq.c
index 53b1737e978d..81c3c0a62edf 100644
--- a/block/blk-softirq.c
+++ b/block/blk-softirq.c
@@ -51,6 +51,7 @@ static void trigger_softirq(void *data)
 		raise_softirq_irqoff(BLOCK_SOFTIRQ);
 
 	local_irq_restore(flags);
+	preempt_check_resched_rt();
 }
 
 /*
@@ -93,6 +94,7 @@ static int blk_cpu_notify(struct notifier_block *self, unsigned long action,
 				 this_cpu_ptr(&blk_cpu_done));
 		raise_softirq_irqoff(BLOCK_SOFTIRQ);
 		local_irq_enable();
+		preempt_check_resched_rt();
 	}
 
 	return NOTIFY_OK;
@@ -150,6 +152,7 @@ void __blk_complete_request(struct request *req)
 		goto do_local;
 
 	local_irq_restore(flags);
+	preempt_check_resched_rt();
 }
 
 /**
diff --git a/block/bounce.c b/block/bounce.c
index 1cb5dd3a5da1..2f1ec8a67cbe 100644
--- a/block/bounce.c
+++ b/block/bounce.c
@@ -55,11 +55,11 @@ static void bounce_copy_vec(struct bio_vec *to, unsigned char *vfrom)
 	unsigned long flags;
 	unsigned char *vto;
 
-	local_irq_save(flags);
+	local_irq_save_nort(flags);
 	vto = kmap_atomic(to->bv_page);
 	memcpy(vto + to->bv_offset, vfrom, to->bv_len);
 	kunmap_atomic(vto);
-	local_irq_restore(flags);
+	local_irq_restore_nort(flags);
 }
 
 #else /* CONFIG_HIGHMEM */
diff --git a/crypto/algapi.c b/crypto/algapi.c
index 59bf491fe3d8..f98e79c8cd77 100644
--- a/crypto/algapi.c
+++ b/crypto/algapi.c
@@ -719,13 +719,13 @@ EXPORT_SYMBOL_GPL(crypto_spawn_tfm2);
 
 int crypto_register_notifier(struct notifier_block *nb)
 {
-	return blocking_notifier_chain_register(&crypto_chain, nb);
+	return srcu_notifier_chain_register(&crypto_chain, nb);
 }
 EXPORT_SYMBOL_GPL(crypto_register_notifier);
 
 int crypto_unregister_notifier(struct notifier_block *nb)
 {
-	return blocking_notifier_chain_unregister(&crypto_chain, nb);
+	return srcu_notifier_chain_unregister(&crypto_chain, nb);
 }
 EXPORT_SYMBOL_GPL(crypto_unregister_notifier);
 
diff --git a/crypto/api.c b/crypto/api.c
index bbc147cb5dec..bc1a848f02ec 100644
--- a/crypto/api.c
+++ b/crypto/api.c
@@ -31,7 +31,7 @@ EXPORT_SYMBOL_GPL(crypto_alg_list);
 DECLARE_RWSEM(crypto_alg_sem);
 EXPORT_SYMBOL_GPL(crypto_alg_sem);
 
-BLOCKING_NOTIFIER_HEAD(crypto_chain);
+SRCU_NOTIFIER_HEAD(crypto_chain);
 EXPORT_SYMBOL_GPL(crypto_chain);
 
 static struct crypto_alg *crypto_larval_wait(struct crypto_alg *alg);
@@ -236,10 +236,10 @@ int crypto_probing_notify(unsigned long val, void *v)
 {
 	int ok;
 
-	ok = blocking_notifier_call_chain(&crypto_chain, val, v);
+	ok = srcu_notifier_call_chain(&crypto_chain, val, v);
 	if (ok == NOTIFY_DONE) {
 		request_module("cryptomgr");
-		ok = blocking_notifier_call_chain(&crypto_chain, val, v);
+		ok = srcu_notifier_call_chain(&crypto_chain, val, v);
 	}
 
 	return ok;
diff --git a/crypto/internal.h b/crypto/internal.h
index 00e42a3ed814..2e85551e235f 100644
--- a/crypto/internal.h
+++ b/crypto/internal.h
@@ -47,7 +47,7 @@ struct crypto_larval {
 
 extern struct list_head crypto_alg_list;
 extern struct rw_semaphore crypto_alg_sem;
-extern struct blocking_notifier_head crypto_chain;
+extern struct srcu_notifier_head crypto_chain;
 
 #ifdef CONFIG_PROC_FS
 void __init crypto_init_proc(void);
@@ -143,7 +143,7 @@ static inline int crypto_is_moribund(struct crypto_alg *alg)
 
 static inline void crypto_notify(unsigned long val, void *v)
 {
-	blocking_notifier_call_chain(&crypto_chain, val, v);
+	srcu_notifier_call_chain(&crypto_chain, val, v);
 }
 
 #endif	/* _CRYPTO_INTERNAL_H */
diff --git a/drivers/acpi/acpica/acglobal.h b/drivers/acpi/acpica/acglobal.h
index faa97604d878..941497f31cf0 100644
--- a/drivers/acpi/acpica/acglobal.h
+++ b/drivers/acpi/acpica/acglobal.h
@@ -116,7 +116,7 @@ ACPI_GLOBAL(u8, acpi_gbl_global_lock_pending);
  * interrupt level
  */
 ACPI_GLOBAL(acpi_spinlock, acpi_gbl_gpe_lock);	/* For GPE data structs and registers */
-ACPI_GLOBAL(acpi_spinlock, acpi_gbl_hardware_lock);	/* For ACPI H/W except GPE registers */
+ACPI_GLOBAL(acpi_raw_spinlock, acpi_gbl_hardware_lock);	/* For ACPI H/W except GPE registers */
 ACPI_GLOBAL(acpi_spinlock, acpi_gbl_reference_count_lock);
 
 /* Mutex for _OSI support */
diff --git a/drivers/acpi/acpica/hwregs.c b/drivers/acpi/acpica/hwregs.c
index 3cf77afd142c..dc32e72132f1 100644
--- a/drivers/acpi/acpica/hwregs.c
+++ b/drivers/acpi/acpica/hwregs.c
@@ -269,14 +269,14 @@ acpi_status acpi_hw_clear_acpi_status(void)
 			  ACPI_BITMASK_ALL_FIXED_STATUS,
 			  ACPI_FORMAT_UINT64(acpi_gbl_xpm1a_status.address)));
 
-	lock_flags = acpi_os_acquire_lock(acpi_gbl_hardware_lock);
+	raw_spin_lock_irqsave(acpi_gbl_hardware_lock, lock_flags);
 
 	/* Clear the fixed events in PM1 A/B */
 
 	status = acpi_hw_register_write(ACPI_REGISTER_PM1_STATUS,
 					ACPI_BITMASK_ALL_FIXED_STATUS);
 
-	acpi_os_release_lock(acpi_gbl_hardware_lock, lock_flags);
+	raw_spin_unlock_irqrestore(acpi_gbl_hardware_lock, lock_flags);
 
 	if (ACPI_FAILURE(status)) {
 		goto exit;
diff --git a/drivers/acpi/acpica/hwxface.c b/drivers/acpi/acpica/hwxface.c
index 5f97468df8ff..8c017f15da7d 100644
--- a/drivers/acpi/acpica/hwxface.c
+++ b/drivers/acpi/acpica/hwxface.c
@@ -374,7 +374,7 @@ acpi_status acpi_write_bit_register(u32 register_id, u32 value)
 		return_ACPI_STATUS(AE_BAD_PARAMETER);
 	}
 
-	lock_flags = acpi_os_acquire_lock(acpi_gbl_hardware_lock);
+	raw_spin_lock_irqsave(acpi_gbl_hardware_lock, lock_flags);
 
 	/*
 	 * At this point, we know that the parent register is one of the
@@ -435,7 +435,7 @@ acpi_status acpi_write_bit_register(u32 register_id, u32 value)
 
 unlock_and_exit:
 
-	acpi_os_release_lock(acpi_gbl_hardware_lock, lock_flags);
+	raw_spin_unlock_irqrestore(acpi_gbl_hardware_lock, lock_flags);
 	return_ACPI_STATUS(status);
 }
 
diff --git a/drivers/acpi/acpica/utmutex.c b/drivers/acpi/acpica/utmutex.c
index ce406e39b669..41a75eb3ae9d 100644
--- a/drivers/acpi/acpica/utmutex.c
+++ b/drivers/acpi/acpica/utmutex.c
@@ -88,7 +88,7 @@ acpi_status acpi_ut_mutex_initialize(void)
 		return_ACPI_STATUS (status);
 	}
 
-	status = acpi_os_create_lock (&acpi_gbl_hardware_lock);
+	status = acpi_os_create_raw_lock (&acpi_gbl_hardware_lock);
 	if (ACPI_FAILURE (status)) {
 		return_ACPI_STATUS (status);
 	}
@@ -156,7 +156,7 @@ void acpi_ut_mutex_terminate(void)
 	/* Delete the spinlocks */
 
 	acpi_os_delete_lock(acpi_gbl_gpe_lock);
-	acpi_os_delete_lock(acpi_gbl_hardware_lock);
+	acpi_os_delete_raw_lock(acpi_gbl_hardware_lock);
 	acpi_os_delete_lock(acpi_gbl_reference_count_lock);
 
 	/* Delete the reader/writer lock */
diff --git a/drivers/ata/libata-sff.c b/drivers/ata/libata-sff.c
index cdf6215a9a22..c9f2a33b8c0b 100644
--- a/drivers/ata/libata-sff.c
+++ b/drivers/ata/libata-sff.c
@@ -678,9 +678,9 @@ unsigned int ata_sff_data_xfer_noirq(struct ata_device *dev, unsigned char *buf,
 	unsigned long flags;
 	unsigned int consumed;
 
-	local_irq_save(flags);
+	local_irq_save_nort(flags);
 	consumed = ata_sff_data_xfer32(dev, buf, buflen, rw);
-	local_irq_restore(flags);
+	local_irq_restore_nort(flags);
 
 	return consumed;
 }
@@ -719,7 +719,7 @@ static void ata_pio_sector(struct ata_queued_cmd *qc)
 		unsigned long flags;
 
 		/* FIXME: use a bounce buffer */
-		local_irq_save(flags);
+		local_irq_save_nort(flags);
 		buf = kmap_atomic(page);
 
 		/* do the actual data transfer */
@@ -727,7 +727,7 @@ static void ata_pio_sector(struct ata_queued_cmd *qc)
 				       do_write);
 
 		kunmap_atomic(buf);
-		local_irq_restore(flags);
+		local_irq_restore_nort(flags);
 	} else {
 		buf = page_address(page);
 		ap->ops->sff_data_xfer(qc->dev, buf + offset, qc->sect_size,
@@ -864,7 +864,7 @@ static int __atapi_pio_bytes(struct ata_queued_cmd *qc, unsigned int bytes)
 		unsigned long flags;
 
 		/* FIXME: use bounce buffer */
-		local_irq_save(flags);
+		local_irq_save_nort(flags);
 		buf = kmap_atomic(page);
 
 		/* do the actual data transfer */
@@ -872,7 +872,7 @@ static int __atapi_pio_bytes(struct ata_queued_cmd *qc, unsigned int bytes)
 								count, rw);
 
 		kunmap_atomic(buf);
-		local_irq_restore(flags);
+		local_irq_restore_nort(flags);
 	} else {
 		buf = page_address(page);
 		consumed = ap->ops->sff_data_xfer(dev,  buf + offset,
diff --git a/drivers/char/random.c b/drivers/char/random.c
index d0da5d852d41..29abd5fa54b0 100644
--- a/drivers/char/random.c
+++ b/drivers/char/random.c
@@ -796,8 +796,6 @@ static void add_timer_randomness(struct timer_rand_state *state, unsigned num)
 	} sample;
 	long delta, delta2, delta3;
 
-	preempt_disable();
-
 	sample.jiffies = jiffies;
 	sample.cycles = random_get_entropy();
 	sample.num = num;
@@ -838,7 +836,6 @@ static void add_timer_randomness(struct timer_rand_state *state, unsigned num)
 		 */
 		credit_entropy_bits(r, min_t(int, fls(delta>>1), 11));
 	}
-	preempt_enable();
 }
 
 void add_input_randomness(unsigned int type, unsigned int code,
@@ -891,28 +888,27 @@ static __u32 get_reg(struct fast_pool *f, struct pt_regs *regs)
 	return *(ptr + f->reg_idx++);
 }
 
-void add_interrupt_randomness(int irq, int irq_flags)
+void add_interrupt_randomness(int irq, int irq_flags, __u64 ip)
 {
 	struct entropy_store	*r;
 	struct fast_pool	*fast_pool = this_cpu_ptr(&irq_randomness);
-	struct pt_regs		*regs = get_irq_regs();
 	unsigned long		now = jiffies;
 	cycles_t		cycles = random_get_entropy();
 	__u32			c_high, j_high;
-	__u64			ip;
 	unsigned long		seed;
 	int			credit = 0;
 
 	if (cycles == 0)
-		cycles = get_reg(fast_pool, regs);
+		cycles = get_reg(fast_pool, NULL);
 	c_high = (sizeof(cycles) > 4) ? cycles >> 32 : 0;
 	j_high = (sizeof(now) > 4) ? now >> 32 : 0;
 	fast_pool->pool[0] ^= cycles ^ j_high ^ irq;
 	fast_pool->pool[1] ^= now ^ c_high;
-	ip = regs ? instruction_pointer(regs) : _RET_IP_;
+	if (!ip)
+		ip = _RET_IP_;
 	fast_pool->pool[2] ^= ip;
 	fast_pool->pool[3] ^= (sizeof(ip) > 4) ? ip >> 32 :
-		get_reg(fast_pool, regs);
+		get_reg(fast_pool, NULL);
 
 	fast_mix(fast_pool);
 	add_interrupt_bench(cycles);
diff --git a/drivers/clocksource/tcb_clksrc.c b/drivers/clocksource/tcb_clksrc.c
index 6ee91401918e..64fb09925065 100644
--- a/drivers/clocksource/tcb_clksrc.c
+++ b/drivers/clocksource/tcb_clksrc.c
@@ -23,8 +23,7 @@
  *     this 32 bit free-running counter. the second channel is not used.
  *
  *   - The third channel may be used to provide a 16-bit clockevent
- *     source, used in either periodic or oneshot mode.  This runs
- *     at 32 KiHZ, and can handle delays of up to two seconds.
+ *     source, used in either periodic or oneshot mode.
  *
  * A boot clocksource and clockevent source are also currently needed,
  * unless the relevant platforms (ARM/AT91, AVR32/AT32) are changed so
@@ -74,6 +73,7 @@ static struct clocksource clksrc = {
 struct tc_clkevt_device {
 	struct clock_event_device	clkevt;
 	struct clk			*clk;
+	u32				freq;
 	void __iomem			*regs;
 };
 
@@ -82,13 +82,6 @@ static struct tc_clkevt_device *to_tc_clkevt(struct clock_event_device *clkevt)
 	return container_of(clkevt, struct tc_clkevt_device, clkevt);
 }
 
-/* For now, we always use the 32K clock ... this optimizes for NO_HZ,
- * because using one of the divided clocks would usually mean the
- * tick rate can never be less than several dozen Hz (vs 0.5 Hz).
- *
- * A divided clock could be good for high resolution timers, since
- * 30.5 usec resolution can seem "low".
- */
 static u32 timer_clock;
 
 static int tc_shutdown(struct clock_event_device *d)
@@ -113,7 +106,7 @@ static int tc_set_oneshot(struct clock_event_device *d)
 
 	clk_enable(tcd->clk);
 
-	/* slow clock, count up to RC, then irq and stop */
+	/* count up to RC, then irq and stop */
 	__raw_writel(timer_clock | ATMEL_TC_CPCSTOP | ATMEL_TC_WAVE |
 		     ATMEL_TC_WAVESEL_UP_AUTO, regs + ATMEL_TC_REG(2, CMR));
 	__raw_writel(ATMEL_TC_CPCS, regs + ATMEL_TC_REG(2, IER));
@@ -135,10 +128,10 @@ static int tc_set_periodic(struct clock_event_device *d)
 	 */
 	clk_enable(tcd->clk);
 
-	/* slow clock, count up to RC, then irq and restart */
+	/* count up to RC, then irq and restart */
 	__raw_writel(timer_clock | ATMEL_TC_WAVE | ATMEL_TC_WAVESEL_UP_AUTO,
 		     regs + ATMEL_TC_REG(2, CMR));
-	__raw_writel((32768 + HZ / 2) / HZ, tcaddr + ATMEL_TC_REG(2, RC));
+	__raw_writel((tcd->freq + HZ / 2) / HZ, tcaddr + ATMEL_TC_REG(2, RC));
 
 	/* Enable clock and interrupts on RC compare */
 	__raw_writel(ATMEL_TC_CPCS, regs + ATMEL_TC_REG(2, IER));
@@ -165,7 +158,11 @@ static struct tc_clkevt_device clkevt = {
 		.features		= CLOCK_EVT_FEAT_PERIODIC |
 					  CLOCK_EVT_FEAT_ONESHOT,
 		/* Should be lower than at91rm9200's system timer */
+#ifdef CONFIG_ATMEL_TCB_CLKSRC_USE_SLOW_CLOCK
 		.rating			= 125,
+#else
+		.rating			= 200,
+#endif
 		.set_next_event		= tc_next_event,
 		.set_state_shutdown	= tc_shutdown,
 		.set_state_periodic	= tc_set_periodic,
@@ -187,8 +184,9 @@ static irqreturn_t ch2_irq(int irq, void *handle)
 	return IRQ_NONE;
 }
 
-static int __init setup_clkevents(struct atmel_tc *tc, int clk32k_divisor_idx)
+static int __init setup_clkevents(struct atmel_tc *tc, int divisor_idx)
 {
+	unsigned divisor = atmel_tc_divisors[divisor_idx];
 	int ret;
 	struct clk *t2_clk = tc->clk[2];
 	int irq = tc->irq[2];
@@ -209,7 +207,11 @@ static int __init setup_clkevents(struct atmel_tc *tc, int clk32k_divisor_idx)
 	clkevt.regs = tc->regs;
 	clkevt.clk = t2_clk;
 
-	timer_clock = clk32k_divisor_idx;
+	timer_clock = divisor_idx;
+	if (!divisor)
+		clkevt.freq = 32768;
+	else
+		clkevt.freq = clk_get_rate(t2_clk) / divisor;
 
 	clkevt.clkevt.cpumask = cpumask_of(0);
 
@@ -220,7 +222,7 @@ static int __init setup_clkevents(struct atmel_tc *tc, int clk32k_divisor_idx)
 		return ret;
 	}
 
-	clockevents_config_and_register(&clkevt.clkevt, 32768, 1, 0xffff);
+	clockevents_config_and_register(&clkevt.clkevt, clkevt.freq, 1, 0xffff);
 
 	return ret;
 }
@@ -357,7 +359,11 @@ static int __init tcb_clksrc_init(void)
 		goto err_disable_t1;
 
 	/* channel 2:  periodic and oneshot timer support */
+#ifdef CONFIG_ATMEL_TCB_CLKSRC_USE_SLOW_CLOCK
 	ret = setup_clkevents(tc, clk32k_divisor_idx);
+#else
+	ret = setup_clkevents(tc, best_divisor_idx);
+#endif
 	if (ret)
 		goto err_unregister_clksrc;
 
diff --git a/drivers/clocksource/timer-atmel-pit.c b/drivers/clocksource/timer-atmel-pit.c
index d911c5dca8f1..a7abdb6638cd 100644
--- a/drivers/clocksource/timer-atmel-pit.c
+++ b/drivers/clocksource/timer-atmel-pit.c
@@ -96,15 +96,24 @@ static int pit_clkevt_shutdown(struct clock_event_device *dev)
 
 	/* disable irq, leaving the clocksource active */
 	pit_write(data->base, AT91_PIT_MR, (data->cycle - 1) | AT91_PIT_PITEN);
+	free_irq(data->irq, data);
 	return 0;
 }
 
+static irqreturn_t at91sam926x_pit_interrupt(int irq, void *dev_id);
 /*
  * Clockevent device:  interrupts every 1/HZ (== pit_cycles * MCK/16)
  */
 static int pit_clkevt_set_periodic(struct clock_event_device *dev)
 {
 	struct pit_data *data = clkevt_to_pit_data(dev);
+	int ret;
+
+	ret = request_irq(data->irq, at91sam926x_pit_interrupt,
+			  IRQF_SHARED | IRQF_TIMER | IRQF_IRQPOLL,
+			  "at91_tick", data);
+	if (ret)
+		panic(pr_fmt("Unable to setup IRQ\n"));
 
 	/* update clocksource counter */
 	data->cnt += data->cycle * PIT_PICNT(pit_read(data->base, AT91_PIT_PIVR));
@@ -181,7 +190,6 @@ static void __init at91sam926x_pit_common_init(struct pit_data *data)
 {
 	unsigned long	pit_rate;
 	unsigned	bits;
-	int		ret;
 
 	/*
 	 * Use our actual MCK to figure out how many MCK/16 ticks per
@@ -206,13 +214,6 @@ static void __init at91sam926x_pit_common_init(struct pit_data *data)
 	data->clksrc.flags = CLOCK_SOURCE_IS_CONTINUOUS;
 	clocksource_register_hz(&data->clksrc, pit_rate);
 
-	/* Set up irq handler */
-	ret = request_irq(data->irq, at91sam926x_pit_interrupt,
-			  IRQF_SHARED | IRQF_TIMER | IRQF_IRQPOLL,
-			  "at91_tick", data);
-	if (ret)
-		panic(pr_fmt("Unable to setup IRQ\n"));
-
 	/* Set up and register clockevents */
 	data->clkevt.name = "pit";
 	data->clkevt.features = CLOCK_EVT_FEAT_PERIODIC;
diff --git a/drivers/clocksource/timer-atmel-st.c b/drivers/clocksource/timer-atmel-st.c
index 29d21d68df5a..103d0fd70cc4 100644
--- a/drivers/clocksource/timer-atmel-st.c
+++ b/drivers/clocksource/timer-atmel-st.c
@@ -115,18 +115,29 @@ static void clkdev32k_disable_and_flush_irq(void)
 	last_crtr = read_CRTR();
 }
 
+static int atmel_st_irq;
+
 static int clkevt32k_shutdown(struct clock_event_device *evt)
 {
 	clkdev32k_disable_and_flush_irq();
 	irqmask = 0;
 	regmap_write(regmap_st, AT91_ST_IER, irqmask);
+	free_irq(atmel_st_irq, regmap_st);
 	return 0;
 }
 
 static int clkevt32k_set_oneshot(struct clock_event_device *dev)
 {
+	int ret;
+
 	clkdev32k_disable_and_flush_irq();
 
+	ret = request_irq(atmel_st_irq, at91rm9200_timer_interrupt,
+			  IRQF_SHARED | IRQF_TIMER | IRQF_IRQPOLL,
+			  "at91_tick", regmap_st);
+	if (ret)
+		panic(pr_fmt("Unable to setup IRQ\n"));
+
 	/*
 	 * ALM for oneshot irqs, set by next_event()
 	 * before 32 seconds have passed.
@@ -139,8 +150,16 @@ static int clkevt32k_set_oneshot(struct clock_event_device *dev)
 
 static int clkevt32k_set_periodic(struct clock_event_device *dev)
 {
+	int ret;
+
 	clkdev32k_disable_and_flush_irq();
 
+	ret = request_irq(atmel_st_irq, at91rm9200_timer_interrupt,
+			  IRQF_SHARED | IRQF_TIMER | IRQF_IRQPOLL,
+			  "at91_tick", regmap_st);
+	if (ret)
+		panic(pr_fmt("Unable to setup IRQ\n"));
+
 	/* PIT for periodic irqs; fixed rate of 1/HZ */
 	irqmask = AT91_ST_PITS;
 	regmap_write(regmap_st, AT91_ST_PIMR, timer_latch);
@@ -198,7 +217,7 @@ static void __init atmel_st_timer_init(struct device_node *node)
 {
 	struct clk *sclk;
 	unsigned int sclk_rate, val;
-	int irq, ret;
+	int ret;
 
 	regmap_st = syscon_node_to_regmap(node);
 	if (IS_ERR(regmap_st))
@@ -210,17 +229,10 @@ static void __init atmel_st_timer_init(struct device_node *node)
 	regmap_read(regmap_st, AT91_ST_SR, &val);
 
 	/* Get the interrupts property */
-	irq  = irq_of_parse_and_map(node, 0);
-	if (!irq)
+	atmel_st_irq  = irq_of_parse_and_map(node, 0);
+	if (!atmel_st_irq)
 		panic(pr_fmt("Unable to get IRQ from DT\n"));
 
-	/* Make IRQs happen for the system timer */
-	ret = request_irq(irq, at91rm9200_timer_interrupt,
-			  IRQF_SHARED | IRQF_TIMER | IRQF_IRQPOLL,
-			  "at91_tick", regmap_st);
-	if (ret)
-		panic(pr_fmt("Unable to setup IRQ\n"));
-
 	sclk = of_clk_get(node, 0);
 	if (IS_ERR(sclk))
 		panic(pr_fmt("Unable to get slow clock\n"));
diff --git a/drivers/cpufreq/Kconfig.x86 b/drivers/cpufreq/Kconfig.x86
index c59bdcb83217..8f23161d80be 100644
--- a/drivers/cpufreq/Kconfig.x86
+++ b/drivers/cpufreq/Kconfig.x86
@@ -123,7 +123,7 @@ config X86_POWERNOW_K7_ACPI
 
 config X86_POWERNOW_K8
 	tristate "AMD Opteron/Athlon64 PowerNow!"
-	depends on ACPI && ACPI_PROCESSOR && X86_ACPI_CPUFREQ
+	depends on ACPI && ACPI_PROCESSOR && X86_ACPI_CPUFREQ && !PREEMPT_RT_BASE
 	help
 	  This adds the CPUFreq driver for K8/early Opteron/Athlon64 processors.
 	  Support for K10 and newer processors is now in acpi-cpufreq.
diff --git a/drivers/cpuidle/coupled.c b/drivers/cpuidle/coupled.c
index 344058f8501a..d5657d50ac40 100644
--- a/drivers/cpuidle/coupled.c
+++ b/drivers/cpuidle/coupled.c
@@ -119,7 +119,6 @@ struct cpuidle_coupled {
 
 #define CPUIDLE_COUPLED_NOT_IDLE	(-1)
 
-static DEFINE_MUTEX(cpuidle_coupled_lock);
 static DEFINE_PER_CPU(struct call_single_data, cpuidle_coupled_poke_cb);
 
 /*
diff --git a/drivers/gpu/drm/i915/i915_gem_execbuffer.c b/drivers/gpu/drm/i915/i915_gem_execbuffer.c
index 6ed7d63a0688..9da7482ad256 100644
--- a/drivers/gpu/drm/i915/i915_gem_execbuffer.c
+++ b/drivers/gpu/drm/i915/i915_gem_execbuffer.c
@@ -1264,7 +1264,9 @@ i915_gem_ringbuffer_submission(struct i915_execbuffer_params *params,
 	if (ret)
 		return ret;
 
+#ifndef CONFIG_PREEMPT_RT_BASE
 	trace_i915_gem_ring_dispatch(params->request, params->dispatch_flags);
+#endif
 
 	i915_gem_execbuffer_move_to_active(vmas, params->request);
 	i915_gem_execbuffer_retire_commands(params);
diff --git a/drivers/gpu/drm/i915/i915_gem_shrinker.c b/drivers/gpu/drm/i915/i915_gem_shrinker.c
index f7df54a8ee2b..9686fa273809 100644
--- a/drivers/gpu/drm/i915/i915_gem_shrinker.c
+++ b/drivers/gpu/drm/i915/i915_gem_shrinker.c
@@ -39,7 +39,7 @@ static bool mutex_is_locked_by(struct mutex *mutex, struct task_struct *task)
 	if (!mutex_is_locked(mutex))
 		return false;
 
-#if defined(CONFIG_SMP) || defined(CONFIG_DEBUG_MUTEXES)
+#if (defined(CONFIG_SMP) || defined(CONFIG_DEBUG_MUTEXES)) && !defined(CONFIG_PREEMPT_RT_BASE)
 	return mutex->owner == task;
 #else
 	/* Since UP may be pre-empted, we cannot assume that we own the lock */
diff --git a/drivers/gpu/drm/i915/intel_display.c b/drivers/gpu/drm/i915/intel_display.c
index 32cf97346978..b58db29dcc55 100644
--- a/drivers/gpu/drm/i915/intel_display.c
+++ b/drivers/gpu/drm/i915/intel_display.c
@@ -11376,7 +11376,7 @@ void intel_check_page_flip(struct drm_device *dev, int pipe)
 	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);
 	struct intel_unpin_work *work;
 
-	WARN_ON(!in_interrupt());
+	WARN_ON_NONRT(!in_interrupt());
 
 	if (crtc == NULL)
 		return;
diff --git a/drivers/i2c/busses/i2c-omap.c b/drivers/i2c/busses/i2c-omap.c
index 08d26ba61ed3..46b89dd42b10 100644
--- a/drivers/i2c/busses/i2c-omap.c
+++ b/drivers/i2c/busses/i2c-omap.c
@@ -995,15 +995,12 @@ omap_i2c_isr(int irq, void *dev_id)
 	u16 mask;
 	u16 stat;
 
-	spin_lock(&omap->lock);
-	mask = omap_i2c_read_reg(omap, OMAP_I2C_IE_REG);
 	stat = omap_i2c_read_reg(omap, OMAP_I2C_STAT_REG);
+	mask = omap_i2c_read_reg(omap, OMAP_I2C_IE_REG);
 
 	if (stat & mask)
 		ret = IRQ_WAKE_THREAD;
 
-	spin_unlock(&omap->lock);
-
 	return ret;
 }
 
diff --git a/drivers/ide/alim15x3.c b/drivers/ide/alim15x3.c
index 36f76e28a0bf..394f142f90c7 100644
--- a/drivers/ide/alim15x3.c
+++ b/drivers/ide/alim15x3.c
@@ -234,7 +234,7 @@ static int init_chipset_ali15x3(struct pci_dev *dev)
 
 	isa_dev = pci_get_device(PCI_VENDOR_ID_AL, PCI_DEVICE_ID_AL_M1533, NULL);
 
-	local_irq_save(flags);
+	local_irq_save_nort(flags);
 
 	if (m5229_revision < 0xC2) {
 		/*
@@ -325,7 +325,7 @@ static int init_chipset_ali15x3(struct pci_dev *dev)
 	}
 	pci_dev_put(north);
 	pci_dev_put(isa_dev);
-	local_irq_restore(flags);
+	local_irq_restore_nort(flags);
 	return 0;
 }
 
diff --git a/drivers/ide/hpt366.c b/drivers/ide/hpt366.c
index 696b6c1ec940..0d0a96629b73 100644
--- a/drivers/ide/hpt366.c
+++ b/drivers/ide/hpt366.c
@@ -1241,7 +1241,7 @@ static int init_dma_hpt366(ide_hwif_t *hwif,
 
 	dma_old = inb(base + 2);
 
-	local_irq_save(flags);
+	local_irq_save_nort(flags);
 
 	dma_new = dma_old;
 	pci_read_config_byte(dev, hwif->channel ? 0x4b : 0x43, &masterdma);
@@ -1252,7 +1252,7 @@ static int init_dma_hpt366(ide_hwif_t *hwif,
 	if (dma_new != dma_old)
 		outb(dma_new, base + 2);
 
-	local_irq_restore(flags);
+	local_irq_restore_nort(flags);
 
 	printk(KERN_INFO "    %s: BM-DMA at 0x%04lx-0x%04lx\n",
 			 hwif->name, base, base + 7);
diff --git a/drivers/ide/ide-io-std.c b/drivers/ide/ide-io-std.c
index 19763977568c..4169433faab5 100644
--- a/drivers/ide/ide-io-std.c
+++ b/drivers/ide/ide-io-std.c
@@ -175,7 +175,7 @@ void ide_input_data(ide_drive_t *drive, struct ide_cmd *cmd, void *buf,
 		unsigned long uninitialized_var(flags);
 
 		if ((io_32bit & 2) && !mmio) {
-			local_irq_save(flags);
+			local_irq_save_nort(flags);
 			ata_vlb_sync(io_ports->nsect_addr);
 		}
 
@@ -186,7 +186,7 @@ void ide_input_data(ide_drive_t *drive, struct ide_cmd *cmd, void *buf,
 			insl(data_addr, buf, words);
 
 		if ((io_32bit & 2) && !mmio)
-			local_irq_restore(flags);
+			local_irq_restore_nort(flags);
 
 		if (((len + 1) & 3) < 2)
 			return;
@@ -219,7 +219,7 @@ void ide_output_data(ide_drive_t *drive, struct ide_cmd *cmd, void *buf,
 		unsigned long uninitialized_var(flags);
 
 		if ((io_32bit & 2) && !mmio) {
-			local_irq_save(flags);
+			local_irq_save_nort(flags);
 			ata_vlb_sync(io_ports->nsect_addr);
 		}
 
@@ -230,7 +230,7 @@ void ide_output_data(ide_drive_t *drive, struct ide_cmd *cmd, void *buf,
 			outsl(data_addr, buf, words);
 
 		if ((io_32bit & 2) && !mmio)
-			local_irq_restore(flags);
+			local_irq_restore_nort(flags);
 
 		if (((len + 1) & 3) < 2)
 			return;
diff --git a/drivers/ide/ide-io.c b/drivers/ide/ide-io.c
index 669ea1e45795..e12e43e62245 100644
--- a/drivers/ide/ide-io.c
+++ b/drivers/ide/ide-io.c
@@ -659,7 +659,7 @@ void ide_timer_expiry (unsigned long data)
 		/* disable_irq_nosync ?? */
 		disable_irq(hwif->irq);
 		/* local CPU only, as if we were handling an interrupt */
-		local_irq_disable();
+		local_irq_disable_nort();
 		if (hwif->polling) {
 			startstop = handler(drive);
 		} else if (drive_is_ready(drive)) {
diff --git a/drivers/ide/ide-iops.c b/drivers/ide/ide-iops.c
index 376f2dc410c5..f014dd1b73dc 100644
--- a/drivers/ide/ide-iops.c
+++ b/drivers/ide/ide-iops.c
@@ -129,12 +129,12 @@ int __ide_wait_stat(ide_drive_t *drive, u8 good, u8 bad,
 				if ((stat & ATA_BUSY) == 0)
 					break;
 
-				local_irq_restore(flags);
+				local_irq_restore_nort(flags);
 				*rstat = stat;
 				return -EBUSY;
 			}
 		}
-		local_irq_restore(flags);
+		local_irq_restore_nort(flags);
 	}
 	/*
 	 * Allow status to settle, then read it again.
diff --git a/drivers/ide/ide-probe.c b/drivers/ide/ide-probe.c
index 0b63facd1d87..4ceba37afc0c 100644
--- a/drivers/ide/ide-probe.c
+++ b/drivers/ide/ide-probe.c
@@ -196,10 +196,10 @@ static void do_identify(ide_drive_t *drive, u8 cmd, u16 *id)
 	int bswap = 1;
 
 	/* local CPU only; some systems need this */
-	local_irq_save(flags);
+	local_irq_save_nort(flags);
 	/* read 512 bytes of id info */
 	hwif->tp_ops->input_data(drive, NULL, id, SECTOR_SIZE);
-	local_irq_restore(flags);
+	local_irq_restore_nort(flags);
 
 	drive->dev_flags |= IDE_DFLAG_ID_READ;
 #ifdef DEBUG
diff --git a/drivers/ide/ide-taskfile.c b/drivers/ide/ide-taskfile.c
index a716693417a3..be0568c722d6 100644
--- a/drivers/ide/ide-taskfile.c
+++ b/drivers/ide/ide-taskfile.c
@@ -250,7 +250,7 @@ void ide_pio_bytes(ide_drive_t *drive, struct ide_cmd *cmd,
 
 		page_is_high = PageHighMem(page);
 		if (page_is_high)
-			local_irq_save(flags);
+			local_irq_save_nort(flags);
 
 		buf = kmap_atomic(page) + offset;
 
@@ -271,7 +271,7 @@ void ide_pio_bytes(ide_drive_t *drive, struct ide_cmd *cmd,
 		kunmap_atomic(buf);
 
 		if (page_is_high)
-			local_irq_restore(flags);
+			local_irq_restore_nort(flags);
 
 		len -= nr_bytes;
 	}
@@ -414,7 +414,7 @@ static ide_startstop_t pre_task_out_intr(ide_drive_t *drive,
 	}
 
 	if ((drive->dev_flags & IDE_DFLAG_UNMASK) == 0)
-		local_irq_disable();
+		local_irq_disable_nort();
 
 	ide_set_handler(drive, &task_pio_intr, WAIT_WORSTCASE);
 
diff --git a/drivers/infiniband/ulp/ipoib/ipoib_multicast.c b/drivers/infiniband/ulp/ipoib/ipoib_multicast.c
index f357ca67a41c..6dcef673d60d 100644
--- a/drivers/infiniband/ulp/ipoib/ipoib_multicast.c
+++ b/drivers/infiniband/ulp/ipoib/ipoib_multicast.c
@@ -847,7 +847,7 @@ void ipoib_mcast_restart_task(struct work_struct *work)
 
 	ipoib_dbg_mcast(priv, "restarting multicast task\n");
 
-	local_irq_save(flags);
+	local_irq_save_nort(flags);
 	netif_addr_lock(dev);
 	spin_lock(&priv->lock);
 
@@ -929,7 +929,7 @@ void ipoib_mcast_restart_task(struct work_struct *work)
 
 	spin_unlock(&priv->lock);
 	netif_addr_unlock(dev);
-	local_irq_restore(flags);
+	local_irq_restore_nort(flags);
 
 	/*
 	 * make sure the in-flight joins have finished before we attempt
diff --git a/drivers/input/gameport/gameport.c b/drivers/input/gameport/gameport.c
index 4a2a9e370be7..e970d9afd179 100644
--- a/drivers/input/gameport/gameport.c
+++ b/drivers/input/gameport/gameport.c
@@ -91,13 +91,13 @@ static int gameport_measure_speed(struct gameport *gameport)
 	tx = ~0;
 
 	for (i = 0; i < 50; i++) {
-		local_irq_save(flags);
+		local_irq_save_nort(flags);
 		t1 = ktime_get_ns();
 		for (t = 0; t < 50; t++)
 			gameport_read(gameport);
 		t2 = ktime_get_ns();
 		t3 = ktime_get_ns();
-		local_irq_restore(flags);
+		local_irq_restore_nort(flags);
 		udelay(i * 10);
 		t = (t2 - t1) - (t3 - t2);
 		if (t < tx)
@@ -124,12 +124,12 @@ static int old_gameport_measure_speed(struct gameport *gameport)
 	tx = 1 << 30;
 
 	for(i = 0; i < 50; i++) {
-		local_irq_save(flags);
+		local_irq_save_nort(flags);
 		GET_TIME(t1);
 		for (t = 0; t < 50; t++) gameport_read(gameport);
 		GET_TIME(t2);
 		GET_TIME(t3);
-		local_irq_restore(flags);
+		local_irq_restore_nort(flags);
 		udelay(i * 10);
 		if ((t = DELTA(t2,t1) - DELTA(t3,t2)) < tx) tx = t;
 	}
@@ -148,11 +148,11 @@ static int old_gameport_measure_speed(struct gameport *gameport)
 	tx = 1 << 30;
 
 	for(i = 0; i < 50; i++) {
-		local_irq_save(flags);
+		local_irq_save_nort(flags);
 		t1 = rdtsc();
 		for (t = 0; t < 50; t++) gameport_read(gameport);
 		t2 = rdtsc();
-		local_irq_restore(flags);
+		local_irq_restore_nort(flags);
 		udelay(i * 10);
 		if (t2 - t1 < tx) tx = t2 - t1;
 	}
diff --git a/drivers/leds/trigger/Kconfig b/drivers/leds/trigger/Kconfig
index 5bda6a9b56bb..d6286584c807 100644
--- a/drivers/leds/trigger/Kconfig
+++ b/drivers/leds/trigger/Kconfig
@@ -61,7 +61,7 @@ config LEDS_TRIGGER_BACKLIGHT
 
 config LEDS_TRIGGER_CPU
 	bool "LED CPU Trigger"
-	depends on LEDS_TRIGGERS
+	depends on LEDS_TRIGGERS && !PREEMPT_RT_BASE
 	help
 	  This allows LEDs to be controlled by active CPUs. This shows
 	  the active CPUs across an array of LEDs so you can see which
diff --git a/drivers/md/bcache/Kconfig b/drivers/md/bcache/Kconfig
index 4d200883c505..98b64ed5cb81 100644
--- a/drivers/md/bcache/Kconfig
+++ b/drivers/md/bcache/Kconfig
@@ -1,6 +1,7 @@
 
 config BCACHE
 	tristate "Block device as cache"
+	depends on !PREEMPT_RT_FULL
 	---help---
 	Allows a block device to be used as cache for other devices; uses
 	a btree for indexing and the layout is optimized for SSDs.
diff --git a/drivers/md/dm.c b/drivers/md/dm.c
index 5df40480228b..1add7dc3c9c7 100644
--- a/drivers/md/dm.c
+++ b/drivers/md/dm.c
@@ -2126,7 +2126,7 @@ static void dm_request_fn(struct request_queue *q)
 		/* Establish tio->ti before queuing work (map_tio_request) */
 		tio->ti = ti;
 		queue_kthread_work(&md->kworker, &tio->work);
-		BUG_ON(!irqs_disabled());
+		BUG_ON_NONRT(!irqs_disabled());
 	}
 
 	goto out;
diff --git a/drivers/md/raid5.c b/drivers/md/raid5.c
index 704ef7fcfbf8..adf72a9b6901 100644
--- a/drivers/md/raid5.c
+++ b/drivers/md/raid5.c
@@ -1929,8 +1929,9 @@ static void raid_run_ops(struct stripe_head *sh, unsigned long ops_request)
 	struct raid5_percpu *percpu;
 	unsigned long cpu;
 
-	cpu = get_cpu();
+	cpu = get_cpu_light();
 	percpu = per_cpu_ptr(conf->percpu, cpu);
+	spin_lock(&percpu->lock);
 	if (test_bit(STRIPE_OP_BIOFILL, &ops_request)) {
 		ops_run_biofill(sh);
 		overlap_clear++;
@@ -1986,7 +1987,8 @@ static void raid_run_ops(struct stripe_head *sh, unsigned long ops_request)
 			if (test_and_clear_bit(R5_Overlap, &dev->flags))
 				wake_up(&sh->raid_conf->wait_for_overlap);
 		}
-	put_cpu();
+	spin_unlock(&percpu->lock);
+	put_cpu_light();
 }
 
 static struct stripe_head *alloc_stripe(struct kmem_cache *sc, gfp_t gfp)
@@ -6411,6 +6413,7 @@ static int raid5_alloc_percpu(struct r5conf *conf)
 			       __func__, cpu);
 			break;
 		}
+		spin_lock_init(&per_cpu_ptr(conf->percpu, cpu)->lock);
 	}
 	put_online_cpus();
 
diff --git a/drivers/md/raid5.h b/drivers/md/raid5.h
index a415e1cd39b8..3f0c2e2a3724 100644
--- a/drivers/md/raid5.h
+++ b/drivers/md/raid5.h
@@ -504,6 +504,7 @@ struct r5conf {
 	int			recovery_disabled;
 	/* per cpu variables */
 	struct raid5_percpu {
+		spinlock_t	lock;		/* Protection for -RT */
 		struct page	*spare_page; /* Used when checking P/Q in raid6 */
 		struct flex_array *scribble;   /* space for constructing buffer
 					      * lists and performing address
diff --git a/drivers/media/platform/vsp1/vsp1_video.c b/drivers/media/platform/vsp1/vsp1_video.c
index 5ce88e1f5d71..b4f8cd74ecb8 100644
--- a/drivers/media/platform/vsp1/vsp1_video.c
+++ b/drivers/media/platform/vsp1/vsp1_video.c
@@ -520,7 +520,7 @@ static bool vsp1_pipeline_stopped(struct vsp1_pipeline *pipe)
 	bool stopped;
 
 	spin_lock_irqsave(&pipe->irqlock, flags);
-	stopped = pipe->state == VSP1_PIPELINE_STOPPED,
+	stopped = pipe->state == VSP1_PIPELINE_STOPPED;
 	spin_unlock_irqrestore(&pipe->irqlock, flags);
 
 	return stopped;
diff --git a/drivers/misc/Kconfig b/drivers/misc/Kconfig
index 22892c701c63..3540f684fb36 100644
--- a/drivers/misc/Kconfig
+++ b/drivers/misc/Kconfig
@@ -54,6 +54,7 @@ config AD525X_DPOT_SPI
 config ATMEL_TCLIB
 	bool "Atmel AT32/AT91 Timer/Counter Library"
 	depends on (AVR32 || ARCH_AT91)
+	default y if PREEMPT_RT_FULL
 	help
 	  Select this if you want a library to allocate the Timer/Counter
 	  blocks found on many Atmel processors.  This facilitates using
@@ -69,8 +70,7 @@ config ATMEL_TCB_CLKSRC
 	  are combined to make a single 32-bit timer.
 
 	  When GENERIC_CLOCKEVENTS is defined, the third timer channel
-	  may be used as a clock event device supporting oneshot mode
-	  (delays of up to two seconds) based on the 32 KiHz clock.
+	  may be used as a clock event device supporting oneshot mode.
 
 config ATMEL_TCB_CLKSRC_BLOCK
 	int
@@ -84,6 +84,15 @@ config ATMEL_TCB_CLKSRC_BLOCK
 	  TC can be used for other purposes, such as PWM generation and
 	  interval timing.
 
+config ATMEL_TCB_CLKSRC_USE_SLOW_CLOCK
+	bool "TC Block use 32 KiHz clock"
+	depends on ATMEL_TCB_CLKSRC
+	default y if !PREEMPT_RT_FULL
+	help
+	  Select this to use 32 KiHz base clock rate as TC block clock
+	  source for clock events.
+
+
 config DUMMY_IRQ
 	tristate "Dummy IRQ handler"
 	default n
@@ -113,6 +122,35 @@ config IBM_ASM
 	  for information on the specific driver level and support statement
 	  for your IBM server.
 
+config HWLAT_DETECTOR
+	tristate "Testing module to detect hardware-induced latencies"
+	depends on DEBUG_FS
+	depends on RING_BUFFER
+	default m
+	---help---
+	  A simple hardware latency detector. Use this module to detect
+	  large latencies introduced by the behavior of the underlying
+	  system firmware external to Linux. We do this using periodic
+	  use of stop_machine to grab all available CPUs and measure
+	  for unexplainable gaps in the CPU timestamp counter(s). By
+	  default, the module is not enabled until the "enable" file
+	  within the "hwlat_detector" debugfs directory is toggled.
+
+	  This module is often used to detect SMI (System Management
+	  Interrupts) on x86 systems, though is not x86 specific. To
+	  this end, we default to using a sample window of 1 second,
+	  during which we will sample for 0.5 seconds. If an SMI or
+	  similar event occurs during that time, it is recorded
+	  into an 8K samples global ring buffer until retreived.
+
+	  WARNING: This software should never be enabled (it can be built
+	  but should not be turned on after it is loaded) in a production
+	  environment where high latencies are a concern since the
+	  sampling mechanism actually introduces latencies for
+	  regular tasks while the CPU(s) are being held.
+
+	  If unsure, say N
+
 config PHANTOM
 	tristate "Sensable PHANToM (PCI)"
 	depends on PCI
diff --git a/drivers/misc/Makefile b/drivers/misc/Makefile
index 537d7f3b78da..ec4aecba0656 100644
--- a/drivers/misc/Makefile
+++ b/drivers/misc/Makefile
@@ -39,6 +39,7 @@ obj-$(CONFIG_C2PORT)		+= c2port/
 obj-$(CONFIG_HMC6352)		+= hmc6352.o
 obj-y				+= eeprom/
 obj-y				+= cb710/
+obj-$(CONFIG_HWLAT_DETECTOR)	+= hwlat_detector.o
 obj-$(CONFIG_SPEAR13XX_PCIE_GADGET)	+= spear13xx_pcie_gadget.o
 obj-$(CONFIG_VMWARE_BALLOON)	+= vmw_balloon.o
 obj-$(CONFIG_ARM_CHARLCD)	+= arm-charlcd.o
diff --git a/drivers/misc/hwlat_detector.c b/drivers/misc/hwlat_detector.c
new file mode 100644
index 000000000000..52f5ad5fd9c0
--- /dev/null
+++ b/drivers/misc/hwlat_detector.c
@@ -0,0 +1,1240 @@
+/*
+ * hwlat_detector.c - A simple Hardware Latency detector.
+ *
+ * Use this module to detect large system latencies induced by the behavior of
+ * certain underlying system hardware or firmware, independent of Linux itself.
+ * The code was developed originally to detect the presence of SMIs on Intel
+ * and AMD systems, although there is no dependency upon x86 herein.
+ *
+ * The classical example usage of this module is in detecting the presence of
+ * SMIs or System Management Interrupts on Intel and AMD systems. An SMI is a
+ * somewhat special form of hardware interrupt spawned from earlier CPU debug
+ * modes in which the (BIOS/EFI/etc.) firmware arranges for the South Bridge
+ * LPC (or other device) to generate a special interrupt under certain
+ * circumstances, for example, upon expiration of a special SMI timer device,
+ * due to certain external thermal readings, on certain I/O address accesses,
+ * and other situations. An SMI hits a special CPU pin, triggers a special
+ * SMI mode (complete with special memory map), and the OS is unaware.
+ *
+ * Although certain hardware-inducing latencies are necessary (for example,
+ * a modern system often requires an SMI handler for correct thermal control
+ * and remote management) they can wreak havoc upon any OS-level performance
+ * guarantees toward low-latency, especially when the OS is not even made
+ * aware of the presence of these interrupts. For this reason, we need a
+ * somewhat brute force mechanism to detect these interrupts. In this case,
+ * we do it by hogging all of the CPU(s) for configurable timer intervals,
+ * sampling the built-in CPU timer, looking for discontiguous readings.
+ *
+ * WARNING: This implementation necessarily introduces latencies. Therefore,
+ *          you should NEVER use this module in a production environment
+ *          requiring any kind of low-latency performance guarantee(s).
+ *
+ * Copyright (C) 2008-2009 Jon Masters, Red Hat, Inc. <jcm@redhat.com>
+ *
+ * Includes useful feedback from Clark Williams <clark@redhat.com>
+ *
+ * This file is licensed under the terms of the GNU General Public
+ * License version 2. This program is licensed "as is" without any
+ * warranty of any kind, whether express or implied.
+ */
+
+#include <linux/module.h>
+#include <linux/init.h>
+#include <linux/ring_buffer.h>
+#include <linux/time.h>
+#include <linux/hrtimer.h>
+#include <linux/kthread.h>
+#include <linux/debugfs.h>
+#include <linux/seq_file.h>
+#include <linux/uaccess.h>
+#include <linux/version.h>
+#include <linux/delay.h>
+#include <linux/slab.h>
+#include <linux/trace_clock.h>
+
+#define BUF_SIZE_DEFAULT	262144UL		/* 8K*(sizeof(entry)) */
+#define BUF_FLAGS		(RB_FL_OVERWRITE)	/* no block on full */
+#define U64STR_SIZE		22			/* 20 digits max */
+
+#define VERSION			"1.0.0"
+#define BANNER			"hwlat_detector: "
+#define DRVNAME			"hwlat_detector"
+#define DEFAULT_SAMPLE_WINDOW	1000000			/* 1s */
+#define DEFAULT_SAMPLE_WIDTH	500000			/* 0.5s */
+#define DEFAULT_LAT_THRESHOLD	10			/* 10us */
+
+/* Module metadata */
+
+MODULE_LICENSE("GPL");
+MODULE_AUTHOR("Jon Masters <jcm@redhat.com>");
+MODULE_DESCRIPTION("A simple hardware latency detector");
+MODULE_VERSION(VERSION);
+
+/* Module parameters */
+
+static int debug;
+static int enabled;
+static int threshold;
+
+module_param(debug, int, 0);			/* enable debug */
+module_param(enabled, int, 0);			/* enable detector */
+module_param(threshold, int, 0);		/* latency threshold */
+
+/* Buffering and sampling */
+
+static struct ring_buffer *ring_buffer;		/* sample buffer */
+static DEFINE_MUTEX(ring_buffer_mutex);		/* lock changes */
+static unsigned long buf_size = BUF_SIZE_DEFAULT;
+static struct task_struct *kthread;		/* sampling thread */
+
+/* DebugFS filesystem entries */
+
+static struct dentry *debug_dir;		/* debugfs directory */
+static struct dentry *debug_max;		/* maximum TSC delta */
+static struct dentry *debug_count;		/* total detect count */
+static struct dentry *debug_sample_width;	/* sample width us */
+static struct dentry *debug_sample_window;	/* sample window us */
+static struct dentry *debug_sample;		/* raw samples us */
+static struct dentry *debug_threshold;		/* threshold us */
+static struct dentry *debug_enable;		/* enable/disable */
+
+/* Individual samples and global state */
+
+struct sample;					/* latency sample */
+struct data;					/* Global state */
+
+/* Sampling functions */
+static int __buffer_add_sample(struct sample *sample);
+static struct sample *buffer_get_sample(struct sample *sample);
+
+/* Threading and state */
+static int kthread_fn(void *unused);
+static int start_kthread(void);
+static int stop_kthread(void);
+static void __reset_stats(void);
+static int init_stats(void);
+
+/* Debugfs interface */
+static ssize_t simple_data_read(struct file *filp, char __user *ubuf,
+				size_t cnt, loff_t *ppos, const u64 *entry);
+static ssize_t simple_data_write(struct file *filp, const char __user *ubuf,
+				 size_t cnt, loff_t *ppos, u64 *entry);
+static int debug_sample_fopen(struct inode *inode, struct file *filp);
+static ssize_t debug_sample_fread(struct file *filp, char __user *ubuf,
+				  size_t cnt, loff_t *ppos);
+static int debug_sample_release(struct inode *inode, struct file *filp);
+static int debug_enable_fopen(struct inode *inode, struct file *filp);
+static ssize_t debug_enable_fread(struct file *filp, char __user *ubuf,
+				  size_t cnt, loff_t *ppos);
+static ssize_t debug_enable_fwrite(struct file *file,
+				   const char __user *user_buffer,
+				   size_t user_size, loff_t *offset);
+
+/* Initialization functions */
+static int init_debugfs(void);
+static void free_debugfs(void);
+static int detector_init(void);
+static void detector_exit(void);
+
+/* Individual latency samples are stored here when detected and packed into
+ * the ring_buffer circular buffer, where they are overwritten when
+ * more than buf_size/sizeof(sample) samples are received. */
+struct sample {
+	u64		seqnum;		/* unique sequence */
+	u64		duration;	/* ktime delta */
+	u64		outer_duration;	/* ktime delta (outer loop) */
+	struct timespec	timestamp;	/* wall time */
+	unsigned long   lost;
+};
+
+/* keep the global state somewhere. */
+static struct data {
+
+	struct mutex lock;		/* protect changes */
+
+	u64	count;			/* total since reset */
+	u64	max_sample;		/* max hardware latency */
+	u64	threshold;		/* sample threshold level */
+
+	u64	sample_window;		/* total sampling window (on+off) */
+	u64	sample_width;		/* active sampling portion of window */
+
+	atomic_t sample_open;		/* whether the sample file is open */
+
+	wait_queue_head_t wq;		/* waitqeue for new sample values */
+
+} data;
+
+/**
+ * __buffer_add_sample - add a new latency sample recording to the ring buffer
+ * @sample: The new latency sample value
+ *
+ * This receives a new latency sample and records it in a global ring buffer.
+ * No additional locking is used in this case.
+ */
+static int __buffer_add_sample(struct sample *sample)
+{
+	return ring_buffer_write(ring_buffer,
+				 sizeof(struct sample), sample);
+}
+
+/**
+ * buffer_get_sample - remove a hardware latency sample from the ring buffer
+ * @sample: Pre-allocated storage for the sample
+ *
+ * This retrieves a hardware latency sample from the global circular buffer
+ */
+static struct sample *buffer_get_sample(struct sample *sample)
+{
+	struct ring_buffer_event *e = NULL;
+	struct sample *s = NULL;
+	unsigned int cpu = 0;
+
+	if (!sample)
+		return NULL;
+
+	mutex_lock(&ring_buffer_mutex);
+	for_each_online_cpu(cpu) {
+		e = ring_buffer_consume(ring_buffer, cpu, NULL, &sample->lost);
+		if (e)
+			break;
+	}
+
+	if (e) {
+		s = ring_buffer_event_data(e);
+		memcpy(sample, s, sizeof(struct sample));
+	} else
+		sample = NULL;
+	mutex_unlock(&ring_buffer_mutex);
+
+	return sample;
+}
+
+#ifndef CONFIG_TRACING
+#define time_type	ktime_t
+#define time_get()	ktime_get()
+#define time_to_us(x)	ktime_to_us(x)
+#define time_sub(a, b)	ktime_sub(a, b)
+#define init_time(a, b)	(a).tv64 = b
+#define time_u64(a)	((a).tv64)
+#else
+#define time_type	u64
+#define time_get()	trace_clock_local()
+#define time_to_us(x)	div_u64(x, 1000)
+#define time_sub(a, b)	((a) - (b))
+#define init_time(a, b)	(a = b)
+#define time_u64(a)	a
+#endif
+/**
+ * get_sample - sample the CPU TSC and look for likely hardware latencies
+ *
+ * Used to repeatedly capture the CPU TSC (or similar), looking for potential
+ * hardware-induced latency. Called with interrupts disabled and with
+ * data.lock held.
+ */
+static int get_sample(void)
+{
+	time_type start, t1, t2, last_t2;
+	s64 diff, total = 0;
+	u64 sample = 0;
+	u64 outer_sample = 0;
+	int ret = -1;
+
+	init_time(last_t2, 0);
+	start = time_get(); /* start timestamp */
+
+	do {
+
+		t1 = time_get();	/* we'll look for a discontinuity */
+		t2 = time_get();
+
+		if (time_u64(last_t2)) {
+			/* Check the delta from outer loop (t2 to next t1) */
+			diff = time_to_us(time_sub(t1, last_t2));
+			/* This shouldn't happen */
+			if (diff < 0) {
+				pr_err(BANNER "time running backwards\n");
+				goto out;
+			}
+			if (diff > outer_sample)
+				outer_sample = diff;
+		}
+		last_t2 = t2;
+
+		total = time_to_us(time_sub(t2, start)); /* sample width */
+
+		/* This checks the inner loop (t1 to t2) */
+		diff = time_to_us(time_sub(t2, t1));     /* current diff */
+
+		/* This shouldn't happen */
+		if (diff < 0) {
+			pr_err(BANNER "time running backwards\n");
+			goto out;
+		}
+
+		if (diff > sample)
+			sample = diff; /* only want highest value */
+
+	} while (total <= data.sample_width);
+
+	ret = 0;
+
+	/* If we exceed the threshold value, we have found a hardware latency */
+	if (sample > data.threshold || outer_sample > data.threshold) {
+		struct sample s;
+
+		ret = 1;
+
+		data.count++;
+		s.seqnum = data.count;
+		s.duration = sample;
+		s.outer_duration = outer_sample;
+		s.timestamp = CURRENT_TIME;
+		__buffer_add_sample(&s);
+
+		/* Keep a running maximum ever recorded hardware latency */
+		if (sample > data.max_sample)
+			data.max_sample = sample;
+	}
+
+out:
+	return ret;
+}
+
+/*
+ * kthread_fn - The CPU time sampling/hardware latency detection kernel thread
+ * @unused: A required part of the kthread API.
+ *
+ * Used to periodically sample the CPU TSC via a call to get_sample. We
+ * disable interrupts, which does (intentionally) introduce latency since we
+ * need to ensure nothing else might be running (and thus pre-empting).
+ * Obviously this should never be used in production environments.
+ *
+ * Currently this runs on which ever CPU it was scheduled on, but most
+ * real-worald hardware latency situations occur across several CPUs,
+ * but we might later generalize this if we find there are any actualy
+ * systems with alternate SMI delivery or other hardware latencies.
+ */
+static int kthread_fn(void *unused)
+{
+	int ret;
+	u64 interval;
+
+	while (!kthread_should_stop()) {
+
+		mutex_lock(&data.lock);
+
+		local_irq_disable();
+		ret = get_sample();
+		local_irq_enable();
+
+		if (ret > 0)
+			wake_up(&data.wq); /* wake up reader(s) */
+
+		interval = data.sample_window - data.sample_width;
+		do_div(interval, USEC_PER_MSEC); /* modifies interval value */
+
+		mutex_unlock(&data.lock);
+
+		if (msleep_interruptible(interval))
+			break;
+	}
+
+	return 0;
+}
+
+/**
+ * start_kthread - Kick off the hardware latency sampling/detector kthread
+ *
+ * This starts a kernel thread that will sit and sample the CPU timestamp
+ * counter (TSC or similar) and look for potential hardware latencies.
+ */
+static int start_kthread(void)
+{
+	kthread = kthread_run(kthread_fn, NULL,
+					DRVNAME);
+	if (IS_ERR(kthread)) {
+		pr_err(BANNER "could not start sampling thread\n");
+		enabled = 0;
+		return -ENOMEM;
+	}
+
+	return 0;
+}
+
+/**
+ * stop_kthread - Inform the hardware latency samping/detector kthread to stop
+ *
+ * This kicks the running hardware latency sampling/detector kernel thread and
+ * tells it to stop sampling now. Use this on unload and at system shutdown.
+ */
+static int stop_kthread(void)
+{
+	int ret;
+
+	ret = kthread_stop(kthread);
+
+	return ret;
+}
+
+/**
+ * __reset_stats - Reset statistics for the hardware latency detector
+ *
+ * We use data to store various statistics and global state. We call this
+ * function in order to reset those when "enable" is toggled on or off, and
+ * also at initialization. Should be called with data.lock held.
+ */
+static void __reset_stats(void)
+{
+	data.count = 0;
+	data.max_sample = 0;
+	ring_buffer_reset(ring_buffer); /* flush out old sample entries */
+}
+
+/**
+ * init_stats - Setup global state statistics for the hardware latency detector
+ *
+ * We use data to store various statistics and global state. We also use
+ * a global ring buffer (ring_buffer) to keep raw samples of detected hardware
+ * induced system latencies. This function initializes these structures and
+ * allocates the global ring buffer also.
+ */
+static int init_stats(void)
+{
+	int ret = -ENOMEM;
+
+	mutex_init(&data.lock);
+	init_waitqueue_head(&data.wq);
+	atomic_set(&data.sample_open, 0);
+
+	ring_buffer = ring_buffer_alloc(buf_size, BUF_FLAGS);
+
+	if (WARN(!ring_buffer, KERN_ERR BANNER
+			       "failed to allocate ring buffer!\n"))
+		goto out;
+
+	__reset_stats();
+	data.threshold = threshold ?: DEFAULT_LAT_THRESHOLD; /* threshold us */
+	data.sample_window = DEFAULT_SAMPLE_WINDOW; /* window us */
+	data.sample_width = DEFAULT_SAMPLE_WIDTH;   /* width us */
+
+	ret = 0;
+
+out:
+	return ret;
+
+}
+
+/*
+ * simple_data_read - Wrapper read function for global state debugfs entries
+ * @filp: The active open file structure for the debugfs "file"
+ * @ubuf: The userspace provided buffer to read value into
+ * @cnt: The maximum number of bytes to read
+ * @ppos: The current "file" position
+ * @entry: The entry to read from
+ *
+ * This function provides a generic read implementation for the global state
+ * "data" structure debugfs filesystem entries. It would be nice to use
+ * simple_attr_read directly, but we need to make sure that the data.lock
+ * is held during the actual read.
+ */
+static ssize_t simple_data_read(struct file *filp, char __user *ubuf,
+				size_t cnt, loff_t *ppos, const u64 *entry)
+{
+	char buf[U64STR_SIZE];
+	u64 val = 0;
+	int len = 0;
+
+	memset(buf, 0, sizeof(buf));
+
+	if (!entry)
+		return -EFAULT;
+
+	mutex_lock(&data.lock);
+	val = *entry;
+	mutex_unlock(&data.lock);
+
+	len = snprintf(buf, sizeof(buf), "%llu\n", (unsigned long long)val);
+
+	return simple_read_from_buffer(ubuf, cnt, ppos, buf, len);
+
+}
+
+/*
+ * simple_data_write - Wrapper write function for global state debugfs entries
+ * @filp: The active open file structure for the debugfs "file"
+ * @ubuf: The userspace provided buffer to write value from
+ * @cnt: The maximum number of bytes to write
+ * @ppos: The current "file" position
+ * @entry: The entry to write to
+ *
+ * This function provides a generic write implementation for the global state
+ * "data" structure debugfs filesystem entries. It would be nice to use
+ * simple_attr_write directly, but we need to make sure that the data.lock
+ * is held during the actual write.
+ */
+static ssize_t simple_data_write(struct file *filp, const char __user *ubuf,
+				 size_t cnt, loff_t *ppos, u64 *entry)
+{
+	char buf[U64STR_SIZE];
+	int csize = min(cnt, sizeof(buf));
+	u64 val = 0;
+	int err = 0;
+
+	memset(buf, '\0', sizeof(buf));
+	if (copy_from_user(buf, ubuf, csize))
+		return -EFAULT;
+
+	buf[U64STR_SIZE-1] = '\0';			/* just in case */
+	err = kstrtoull(buf, 10, &val);
+	if (err)
+		return -EINVAL;
+
+	mutex_lock(&data.lock);
+	*entry = val;
+	mutex_unlock(&data.lock);
+
+	return csize;
+}
+
+/**
+ * debug_count_fopen - Open function for "count" debugfs entry
+ * @inode: The in-kernel inode representation of the debugfs "file"
+ * @filp: The active open file structure for the debugfs "file"
+ *
+ * This function provides an open implementation for the "count" debugfs
+ * interface to the hardware latency detector.
+ */
+static int debug_count_fopen(struct inode *inode, struct file *filp)
+{
+	return 0;
+}
+
+/**
+ * debug_count_fread - Read function for "count" debugfs entry
+ * @filp: The active open file structure for the debugfs "file"
+ * @ubuf: The userspace provided buffer to read value into
+ * @cnt: The maximum number of bytes to read
+ * @ppos: The current "file" position
+ *
+ * This function provides a read implementation for the "count" debugfs
+ * interface to the hardware latency detector. Can be used to read the
+ * number of latency readings exceeding the configured threshold since
+ * the detector was last reset (e.g. by writing a zero into "count").
+ */
+static ssize_t debug_count_fread(struct file *filp, char __user *ubuf,
+				     size_t cnt, loff_t *ppos)
+{
+	return simple_data_read(filp, ubuf, cnt, ppos, &data.count);
+}
+
+/**
+ * debug_count_fwrite - Write function for "count" debugfs entry
+ * @filp: The active open file structure for the debugfs "file"
+ * @ubuf: The user buffer that contains the value to write
+ * @cnt: The maximum number of bytes to write to "file"
+ * @ppos: The current position in the debugfs "file"
+ *
+ * This function provides a write implementation for the "count" debugfs
+ * interface to the hardware latency detector. Can be used to write a
+ * desired value, especially to zero the total count.
+ */
+static ssize_t  debug_count_fwrite(struct file *filp,
+				       const char __user *ubuf,
+				       size_t cnt,
+				       loff_t *ppos)
+{
+	return simple_data_write(filp, ubuf, cnt, ppos, &data.count);
+}
+
+/**
+ * debug_enable_fopen - Dummy open function for "enable" debugfs interface
+ * @inode: The in-kernel inode representation of the debugfs "file"
+ * @filp: The active open file structure for the debugfs "file"
+ *
+ * This function provides an open implementation for the "enable" debugfs
+ * interface to the hardware latency detector.
+ */
+static int debug_enable_fopen(struct inode *inode, struct file *filp)
+{
+	return 0;
+}
+
+/**
+ * debug_enable_fread - Read function for "enable" debugfs interface
+ * @filp: The active open file structure for the debugfs "file"
+ * @ubuf: The userspace provided buffer to read value into
+ * @cnt: The maximum number of bytes to read
+ * @ppos: The current "file" position
+ *
+ * This function provides a read implementation for the "enable" debugfs
+ * interface to the hardware latency detector. Can be used to determine
+ * whether the detector is currently enabled ("0\n" or "1\n" returned).
+ */
+static ssize_t debug_enable_fread(struct file *filp, char __user *ubuf,
+				      size_t cnt, loff_t *ppos)
+{
+	char buf[4];
+
+	if ((cnt < sizeof(buf)) || (*ppos))
+		return 0;
+
+	buf[0] = enabled ? '1' : '0';
+	buf[1] = '\n';
+	buf[2] = '\0';
+	if (copy_to_user(ubuf, buf, strlen(buf)))
+		return -EFAULT;
+	return *ppos = strlen(buf);
+}
+
+/**
+ * debug_enable_fwrite - Write function for "enable" debugfs interface
+ * @filp: The active open file structure for the debugfs "file"
+ * @ubuf: The user buffer that contains the value to write
+ * @cnt: The maximum number of bytes to write to "file"
+ * @ppos: The current position in the debugfs "file"
+ *
+ * This function provides a write implementation for the "enable" debugfs
+ * interface to the hardware latency detector. Can be used to enable or
+ * disable the detector, which will have the side-effect of possibly
+ * also resetting the global stats and kicking off the measuring
+ * kthread (on an enable) or the converse (upon a disable).
+ */
+static ssize_t  debug_enable_fwrite(struct file *filp,
+					const char __user *ubuf,
+					size_t cnt,
+					loff_t *ppos)
+{
+	char buf[4];
+	int csize = min(cnt, sizeof(buf));
+	long val = 0;
+	int err = 0;
+
+	memset(buf, '\0', sizeof(buf));
+	if (copy_from_user(buf, ubuf, csize))
+		return -EFAULT;
+
+	buf[sizeof(buf)-1] = '\0';			/* just in case */
+	err = kstrtoul(buf, 10, &val);
+	if (err)
+		return -EINVAL;
+
+	if (val) {
+		if (enabled)
+			goto unlock;
+		enabled = 1;
+		__reset_stats();
+		if (start_kthread())
+			return -EFAULT;
+	} else {
+		if (!enabled)
+			goto unlock;
+		enabled = 0;
+		err = stop_kthread();
+		if (err) {
+			pr_err(BANNER "cannot stop kthread\n");
+			return -EFAULT;
+		}
+		wake_up(&data.wq);		/* reader(s) should return */
+	}
+unlock:
+	return csize;
+}
+
+/**
+ * debug_max_fopen - Open function for "max" debugfs entry
+ * @inode: The in-kernel inode representation of the debugfs "file"
+ * @filp: The active open file structure for the debugfs "file"
+ *
+ * This function provides an open implementation for the "max" debugfs
+ * interface to the hardware latency detector.
+ */
+static int debug_max_fopen(struct inode *inode, struct file *filp)
+{
+	return 0;
+}
+
+/**
+ * debug_max_fread - Read function for "max" debugfs entry
+ * @filp: The active open file structure for the debugfs "file"
+ * @ubuf: The userspace provided buffer to read value into
+ * @cnt: The maximum number of bytes to read
+ * @ppos: The current "file" position
+ *
+ * This function provides a read implementation for the "max" debugfs
+ * interface to the hardware latency detector. Can be used to determine
+ * the maximum latency value observed since it was last reset.
+ */
+static ssize_t debug_max_fread(struct file *filp, char __user *ubuf,
+				   size_t cnt, loff_t *ppos)
+{
+	return simple_data_read(filp, ubuf, cnt, ppos, &data.max_sample);
+}
+
+/**
+ * debug_max_fwrite - Write function for "max" debugfs entry
+ * @filp: The active open file structure for the debugfs "file"
+ * @ubuf: The user buffer that contains the value to write
+ * @cnt: The maximum number of bytes to write to "file"
+ * @ppos: The current position in the debugfs "file"
+ *
+ * This function provides a write implementation for the "max" debugfs
+ * interface to the hardware latency detector. Can be used to reset the
+ * maximum or set it to some other desired value - if, then, subsequent
+ * measurements exceed this value, the maximum will be updated.
+ */
+static ssize_t  debug_max_fwrite(struct file *filp,
+				     const char __user *ubuf,
+				     size_t cnt,
+				     loff_t *ppos)
+{
+	return simple_data_write(filp, ubuf, cnt, ppos, &data.max_sample);
+}
+
+
+/**
+ * debug_sample_fopen - An open function for "sample" debugfs interface
+ * @inode: The in-kernel inode representation of this debugfs "file"
+ * @filp: The active open file structure for the debugfs "file"
+ *
+ * This function handles opening the "sample" file within the hardware
+ * latency detector debugfs directory interface. This file is used to read
+ * raw samples from the global ring_buffer and allows the user to see a
+ * running latency history. Can be opened blocking or non-blocking,
+ * affecting whether it behaves as a buffer read pipe, or does not.
+ * Implements simple locking to prevent multiple simultaneous use.
+ */
+static int debug_sample_fopen(struct inode *inode, struct file *filp)
+{
+	if (!atomic_add_unless(&data.sample_open, 1, 1))
+		return -EBUSY;
+	else
+		return 0;
+}
+
+/**
+ * debug_sample_fread - A read function for "sample" debugfs interface
+ * @filp: The active open file structure for the debugfs "file"
+ * @ubuf: The user buffer that will contain the samples read
+ * @cnt: The maximum bytes to read from the debugfs "file"
+ * @ppos: The current position in the debugfs "file"
+ *
+ * This function handles reading from the "sample" file within the hardware
+ * latency detector debugfs directory interface. This file is used to read
+ * raw samples from the global ring_buffer and allows the user to see a
+ * running latency history. By default this will block pending a new
+ * value written into the sample buffer, unless there are already a
+ * number of value(s) waiting in the buffer, or the sample file was
+ * previously opened in a non-blocking mode of operation.
+ */
+static ssize_t debug_sample_fread(struct file *filp, char __user *ubuf,
+					size_t cnt, loff_t *ppos)
+{
+	int len = 0;
+	char buf[64];
+	struct sample *sample = NULL;
+
+	if (!enabled)
+		return 0;
+
+	sample = kzalloc(sizeof(struct sample), GFP_KERNEL);
+	if (!sample)
+		return -ENOMEM;
+
+	while (!buffer_get_sample(sample)) {
+
+		DEFINE_WAIT(wait);
+
+		if (filp->f_flags & O_NONBLOCK) {
+			len = -EAGAIN;
+			goto out;
+		}
+
+		prepare_to_wait(&data.wq, &wait, TASK_INTERRUPTIBLE);
+		schedule();
+		finish_wait(&data.wq, &wait);
+
+		if (signal_pending(current)) {
+			len = -EINTR;
+			goto out;
+		}
+
+		if (!enabled) {			/* enable was toggled */
+			len = 0;
+			goto out;
+		}
+	}
+
+	len = snprintf(buf, sizeof(buf), "%010lu.%010lu\t%llu\t%llu\n",
+		       sample->timestamp.tv_sec,
+		       sample->timestamp.tv_nsec,
+		       sample->duration,
+		       sample->outer_duration);
+
+
+	/* handling partial reads is more trouble than it's worth */
+	if (len > cnt)
+		goto out;
+
+	if (copy_to_user(ubuf, buf, len))
+		len = -EFAULT;
+
+out:
+	kfree(sample);
+	return len;
+}
+
+/**
+ * debug_sample_release - Release function for "sample" debugfs interface
+ * @inode: The in-kernel inode represenation of the debugfs "file"
+ * @filp: The active open file structure for the debugfs "file"
+ *
+ * This function completes the close of the debugfs interface "sample" file.
+ * Frees the sample_open "lock" so that other users may open the interface.
+ */
+static int debug_sample_release(struct inode *inode, struct file *filp)
+{
+	atomic_dec(&data.sample_open);
+
+	return 0;
+}
+
+/**
+ * debug_threshold_fopen - Open function for "threshold" debugfs entry
+ * @inode: The in-kernel inode representation of the debugfs "file"
+ * @filp: The active open file structure for the debugfs "file"
+ *
+ * This function provides an open implementation for the "threshold" debugfs
+ * interface to the hardware latency detector.
+ */
+static int debug_threshold_fopen(struct inode *inode, struct file *filp)
+{
+	return 0;
+}
+
+/**
+ * debug_threshold_fread - Read function for "threshold" debugfs entry
+ * @filp: The active open file structure for the debugfs "file"
+ * @ubuf: The userspace provided buffer to read value into
+ * @cnt: The maximum number of bytes to read
+ * @ppos: The current "file" position
+ *
+ * This function provides a read implementation for the "threshold" debugfs
+ * interface to the hardware latency detector. It can be used to determine
+ * the current threshold level at which a latency will be recorded in the
+ * global ring buffer, typically on the order of 10us.
+ */
+static ssize_t debug_threshold_fread(struct file *filp, char __user *ubuf,
+					 size_t cnt, loff_t *ppos)
+{
+	return simple_data_read(filp, ubuf, cnt, ppos, &data.threshold);
+}
+
+/**
+ * debug_threshold_fwrite - Write function for "threshold" debugfs entry
+ * @filp: The active open file structure for the debugfs "file"
+ * @ubuf: The user buffer that contains the value to write
+ * @cnt: The maximum number of bytes to write to "file"
+ * @ppos: The current position in the debugfs "file"
+ *
+ * This function provides a write implementation for the "threshold" debugfs
+ * interface to the hardware latency detector. It can be used to configure
+ * the threshold level at which any subsequently detected latencies will
+ * be recorded into the global ring buffer.
+ */
+static ssize_t  debug_threshold_fwrite(struct file *filp,
+					const char __user *ubuf,
+					size_t cnt,
+					loff_t *ppos)
+{
+	int ret;
+
+	ret = simple_data_write(filp, ubuf, cnt, ppos, &data.threshold);
+
+	if (enabled)
+		wake_up_process(kthread);
+
+	return ret;
+}
+
+/**
+ * debug_width_fopen - Open function for "width" debugfs entry
+ * @inode: The in-kernel inode representation of the debugfs "file"
+ * @filp: The active open file structure for the debugfs "file"
+ *
+ * This function provides an open implementation for the "width" debugfs
+ * interface to the hardware latency detector.
+ */
+static int debug_width_fopen(struct inode *inode, struct file *filp)
+{
+	return 0;
+}
+
+/**
+ * debug_width_fread - Read function for "width" debugfs entry
+ * @filp: The active open file structure for the debugfs "file"
+ * @ubuf: The userspace provided buffer to read value into
+ * @cnt: The maximum number of bytes to read
+ * @ppos: The current "file" position
+ *
+ * This function provides a read implementation for the "width" debugfs
+ * interface to the hardware latency detector. It can be used to determine
+ * for how many us of the total window us we will actively sample for any
+ * hardware-induced latecy periods. Obviously, it is not possible to
+ * sample constantly and have the system respond to a sample reader, or,
+ * worse, without having the system appear to have gone out to lunch.
+ */
+static ssize_t debug_width_fread(struct file *filp, char __user *ubuf,
+				     size_t cnt, loff_t *ppos)
+{
+	return simple_data_read(filp, ubuf, cnt, ppos, &data.sample_width);
+}
+
+/**
+ * debug_width_fwrite - Write function for "width" debugfs entry
+ * @filp: The active open file structure for the debugfs "file"
+ * @ubuf: The user buffer that contains the value to write
+ * @cnt: The maximum number of bytes to write to "file"
+ * @ppos: The current position in the debugfs "file"
+ *
+ * This function provides a write implementation for the "width" debugfs
+ * interface to the hardware latency detector. It can be used to configure
+ * for how many us of the total window us we will actively sample for any
+ * hardware-induced latency periods. Obviously, it is not possible to
+ * sample constantly and have the system respond to a sample reader, or,
+ * worse, without having the system appear to have gone out to lunch. It
+ * is enforced that width is less that the total window size.
+ */
+static ssize_t  debug_width_fwrite(struct file *filp,
+				       const char __user *ubuf,
+				       size_t cnt,
+				       loff_t *ppos)
+{
+	char buf[U64STR_SIZE];
+	int csize = min(cnt, sizeof(buf));
+	u64 val = 0;
+	int err = 0;
+
+	memset(buf, '\0', sizeof(buf));
+	if (copy_from_user(buf, ubuf, csize))
+		return -EFAULT;
+
+	buf[U64STR_SIZE-1] = '\0';			/* just in case */
+	err = kstrtoull(buf, 10, &val);
+	if (err)
+		return -EINVAL;
+
+	mutex_lock(&data.lock);
+	if (val < data.sample_window)
+		data.sample_width = val;
+	else {
+		mutex_unlock(&data.lock);
+		return -EINVAL;
+	}
+	mutex_unlock(&data.lock);
+
+	if (enabled)
+		wake_up_process(kthread);
+
+	return csize;
+}
+
+/**
+ * debug_window_fopen - Open function for "window" debugfs entry
+ * @inode: The in-kernel inode representation of the debugfs "file"
+ * @filp: The active open file structure for the debugfs "file"
+ *
+ * This function provides an open implementation for the "window" debugfs
+ * interface to the hardware latency detector. The window is the total time
+ * in us that will be considered one sample period. Conceptually, windows
+ * occur back-to-back and contain a sample width period during which
+ * actual sampling occurs.
+ */
+static int debug_window_fopen(struct inode *inode, struct file *filp)
+{
+	return 0;
+}
+
+/**
+ * debug_window_fread - Read function for "window" debugfs entry
+ * @filp: The active open file structure for the debugfs "file"
+ * @ubuf: The userspace provided buffer to read value into
+ * @cnt: The maximum number of bytes to read
+ * @ppos: The current "file" position
+ *
+ * This function provides a read implementation for the "window" debugfs
+ * interface to the hardware latency detector. The window is the total time
+ * in us that will be considered one sample period. Conceptually, windows
+ * occur back-to-back and contain a sample width period during which
+ * actual sampling occurs. Can be used to read the total window size.
+ */
+static ssize_t debug_window_fread(struct file *filp, char __user *ubuf,
+				      size_t cnt, loff_t *ppos)
+{
+	return simple_data_read(filp, ubuf, cnt, ppos, &data.sample_window);
+}
+
+/**
+ * debug_window_fwrite - Write function for "window" debugfs entry
+ * @filp: The active open file structure for the debugfs "file"
+ * @ubuf: The user buffer that contains the value to write
+ * @cnt: The maximum number of bytes to write to "file"
+ * @ppos: The current position in the debugfs "file"
+ *
+ * This function provides a write implementation for the "window" debufds
+ * interface to the hardware latency detetector. The window is the total time
+ * in us that will be considered one sample period. Conceptually, windows
+ * occur back-to-back and contain a sample width period during which
+ * actual sampling occurs. Can be used to write a new total window size. It
+ * is enfoced that any value written must be greater than the sample width
+ * size, or an error results.
+ */
+static ssize_t  debug_window_fwrite(struct file *filp,
+					const char __user *ubuf,
+					size_t cnt,
+					loff_t *ppos)
+{
+	char buf[U64STR_SIZE];
+	int csize = min(cnt, sizeof(buf));
+	u64 val = 0;
+	int err = 0;
+
+	memset(buf, '\0', sizeof(buf));
+	if (copy_from_user(buf, ubuf, csize))
+		return -EFAULT;
+
+	buf[U64STR_SIZE-1] = '\0';			/* just in case */
+	err = kstrtoull(buf, 10, &val);
+	if (err)
+		return -EINVAL;
+
+	mutex_lock(&data.lock);
+	if (data.sample_width < val)
+		data.sample_window = val;
+	else {
+		mutex_unlock(&data.lock);
+		return -EINVAL;
+	}
+	mutex_unlock(&data.lock);
+
+	return csize;
+}
+
+/*
+ * Function pointers for the "count" debugfs file operations
+ */
+static const struct file_operations count_fops = {
+	.open		= debug_count_fopen,
+	.read		= debug_count_fread,
+	.write		= debug_count_fwrite,
+	.owner		= THIS_MODULE,
+};
+
+/*
+ * Function pointers for the "enable" debugfs file operations
+ */
+static const struct file_operations enable_fops = {
+	.open		= debug_enable_fopen,
+	.read		= debug_enable_fread,
+	.write		= debug_enable_fwrite,
+	.owner		= THIS_MODULE,
+};
+
+/*
+ * Function pointers for the "max" debugfs file operations
+ */
+static const struct file_operations max_fops = {
+	.open		= debug_max_fopen,
+	.read		= debug_max_fread,
+	.write		= debug_max_fwrite,
+	.owner		= THIS_MODULE,
+};
+
+/*
+ * Function pointers for the "sample" debugfs file operations
+ */
+static const struct file_operations sample_fops = {
+	.open		= debug_sample_fopen,
+	.read		= debug_sample_fread,
+	.release	= debug_sample_release,
+	.owner		= THIS_MODULE,
+};
+
+/*
+ * Function pointers for the "threshold" debugfs file operations
+ */
+static const struct file_operations threshold_fops = {
+	.open		= debug_threshold_fopen,
+	.read		= debug_threshold_fread,
+	.write		= debug_threshold_fwrite,
+	.owner		= THIS_MODULE,
+};
+
+/*
+ * Function pointers for the "width" debugfs file operations
+ */
+static const struct file_operations width_fops = {
+	.open		= debug_width_fopen,
+	.read		= debug_width_fread,
+	.write		= debug_width_fwrite,
+	.owner		= THIS_MODULE,
+};
+
+/*
+ * Function pointers for the "window" debugfs file operations
+ */
+static const struct file_operations window_fops = {
+	.open		= debug_window_fopen,
+	.read		= debug_window_fread,
+	.write		= debug_window_fwrite,
+	.owner		= THIS_MODULE,
+};
+
+/**
+ * init_debugfs - A function to initialize the debugfs interface files
+ *
+ * This function creates entries in debugfs for "hwlat_detector", including
+ * files to read values from the detector, current samples, and the
+ * maximum sample that has been captured since the hardware latency
+ * dectector was started.
+ */
+static int init_debugfs(void)
+{
+	int ret = -ENOMEM;
+
+	debug_dir = debugfs_create_dir(DRVNAME, NULL);
+	if (!debug_dir)
+		goto err_debug_dir;
+
+	debug_sample = debugfs_create_file("sample", 0444,
+					       debug_dir, NULL,
+					       &sample_fops);
+	if (!debug_sample)
+		goto err_sample;
+
+	debug_count = debugfs_create_file("count", 0444,
+					      debug_dir, NULL,
+					      &count_fops);
+	if (!debug_count)
+		goto err_count;
+
+	debug_max = debugfs_create_file("max", 0444,
+					    debug_dir, NULL,
+					    &max_fops);
+	if (!debug_max)
+		goto err_max;
+
+	debug_sample_window = debugfs_create_file("window", 0644,
+						      debug_dir, NULL,
+						      &window_fops);
+	if (!debug_sample_window)
+		goto err_window;
+
+	debug_sample_width = debugfs_create_file("width", 0644,
+						     debug_dir, NULL,
+						     &width_fops);
+	if (!debug_sample_width)
+		goto err_width;
+
+	debug_threshold = debugfs_create_file("threshold", 0644,
+						  debug_dir, NULL,
+						  &threshold_fops);
+	if (!debug_threshold)
+		goto err_threshold;
+
+	debug_enable = debugfs_create_file("enable", 0644,
+					       debug_dir, &enabled,
+					       &enable_fops);
+	if (!debug_enable)
+		goto err_enable;
+
+	else {
+		ret = 0;
+		goto out;
+	}
+
+err_enable:
+	debugfs_remove(debug_threshold);
+err_threshold:
+	debugfs_remove(debug_sample_width);
+err_width:
+	debugfs_remove(debug_sample_window);
+err_window:
+	debugfs_remove(debug_max);
+err_max:
+	debugfs_remove(debug_count);
+err_count:
+	debugfs_remove(debug_sample);
+err_sample:
+	debugfs_remove(debug_dir);
+err_debug_dir:
+out:
+	return ret;
+}
+
+/**
+ * free_debugfs - A function to cleanup the debugfs file interface
+ */
+static void free_debugfs(void)
+{
+	/* could also use a debugfs_remove_recursive */
+	debugfs_remove(debug_enable);
+	debugfs_remove(debug_threshold);
+	debugfs_remove(debug_sample_width);
+	debugfs_remove(debug_sample_window);
+	debugfs_remove(debug_max);
+	debugfs_remove(debug_count);
+	debugfs_remove(debug_sample);
+	debugfs_remove(debug_dir);
+}
+
+/**
+ * detector_init - Standard module initialization code
+ */
+static int detector_init(void)
+{
+	int ret = -ENOMEM;
+
+	pr_info(BANNER "version %s\n", VERSION);
+
+	ret = init_stats();
+	if (ret)
+		goto out;
+
+	ret = init_debugfs();
+	if (ret)
+		goto err_stats;
+
+	if (enabled)
+		ret = start_kthread();
+
+	goto out;
+
+err_stats:
+	ring_buffer_free(ring_buffer);
+out:
+	return ret;
+
+}
+
+/**
+ * detector_exit - Standard module cleanup code
+ */
+static void detector_exit(void)
+{
+	int err;
+
+	if (enabled) {
+		enabled = 0;
+		err = stop_kthread();
+		if (err)
+			pr_err(BANNER "cannot stop kthread\n");
+	}
+
+	free_debugfs();
+	ring_buffer_free(ring_buffer);	/* free up the ring buffer */
+
+}
+
+module_init(detector_init);
+module_exit(detector_exit);
diff --git a/drivers/mmc/host/mmci.c b/drivers/mmc/host/mmci.c
index fb266745f824..d42fc084d66f 100644
--- a/drivers/mmc/host/mmci.c
+++ b/drivers/mmc/host/mmci.c
@@ -1155,15 +1155,12 @@ static irqreturn_t mmci_pio_irq(int irq, void *dev_id)
 	struct sg_mapping_iter *sg_miter = &host->sg_miter;
 	struct variant_data *variant = host->variant;
 	void __iomem *base = host->base;
-	unsigned long flags;
 	u32 status;
 
 	status = readl(base + MMCISTATUS);
 
 	dev_dbg(mmc_dev(host->mmc), "irq1 (pio) %08x\n", status);
 
-	local_irq_save(flags);
-
 	do {
 		unsigned int remain, len;
 		char *buffer;
@@ -1203,8 +1200,6 @@ static irqreturn_t mmci_pio_irq(int irq, void *dev_id)
 
 	sg_miter_stop(sg_miter);
 
-	local_irq_restore(flags);
-
 	/*
 	 * If we have less than the fifo 'half-full' threshold to transfer,
 	 * trigger a PIO interrupt as soon as any data is available.
diff --git a/drivers/net/ethernet/3com/3c59x.c b/drivers/net/ethernet/3com/3c59x.c
index 2839af00f20c..4348b9c850d3 100644
--- a/drivers/net/ethernet/3com/3c59x.c
+++ b/drivers/net/ethernet/3com/3c59x.c
@@ -842,9 +842,9 @@ static void poll_vortex(struct net_device *dev)
 {
 	struct vortex_private *vp = netdev_priv(dev);
 	unsigned long flags;
-	local_irq_save(flags);
+	local_irq_save_nort(flags);
 	(vp->full_bus_master_rx ? boomerang_interrupt:vortex_interrupt)(dev->irq,dev);
-	local_irq_restore(flags);
+	local_irq_restore_nort(flags);
 }
 #endif
 
@@ -1916,12 +1916,12 @@ static void vortex_tx_timeout(struct net_device *dev)
 			 * Block interrupts because vortex_interrupt does a bare spin_lock()
 			 */
 			unsigned long flags;
-			local_irq_save(flags);
+			local_irq_save_nort(flags);
 			if (vp->full_bus_master_tx)
 				boomerang_interrupt(dev->irq, dev);
 			else
 				vortex_interrupt(dev->irq, dev);
-			local_irq_restore(flags);
+			local_irq_restore_nort(flags);
 		}
 	}
 
diff --git a/drivers/net/ethernet/atheros/atl1c/atl1c_main.c b/drivers/net/ethernet/atheros/atl1c/atl1c_main.c
index 8b5988e210d5..cf9928ccdd7e 100644
--- a/drivers/net/ethernet/atheros/atl1c/atl1c_main.c
+++ b/drivers/net/ethernet/atheros/atl1c/atl1c_main.c
@@ -2221,11 +2221,7 @@ static netdev_tx_t atl1c_xmit_frame(struct sk_buff *skb,
 	}
 
 	tpd_req = atl1c_cal_tpd_req(skb);
-	if (!spin_trylock_irqsave(&adapter->tx_lock, flags)) {
-		if (netif_msg_pktdata(adapter))
-			dev_info(&adapter->pdev->dev, "tx locked\n");
-		return NETDEV_TX_LOCKED;
-	}
+	spin_lock_irqsave(&adapter->tx_lock, flags);
 
 	if (atl1c_tpd_avail(adapter, type) < tpd_req) {
 		/* no enough descriptor, just stop queue */
diff --git a/drivers/net/ethernet/atheros/atl1e/atl1e_main.c b/drivers/net/ethernet/atheros/atl1e/atl1e_main.c
index 59a03a193e83..734f7a7ad2c3 100644
--- a/drivers/net/ethernet/atheros/atl1e/atl1e_main.c
+++ b/drivers/net/ethernet/atheros/atl1e/atl1e_main.c
@@ -1880,8 +1880,7 @@ static netdev_tx_t atl1e_xmit_frame(struct sk_buff *skb,
 		return NETDEV_TX_OK;
 	}
 	tpd_req = atl1e_cal_tdp_req(skb);
-	if (!spin_trylock_irqsave(&adapter->tx_lock, flags))
-		return NETDEV_TX_LOCKED;
+	spin_lock_irqsave(&adapter->tx_lock, flags);
 
 	if (atl1e_tpd_avail(adapter) < tpd_req) {
 		/* no enough descriptor, just stop queue */
diff --git a/drivers/net/ethernet/chelsio/cxgb/sge.c b/drivers/net/ethernet/chelsio/cxgb/sge.c
index 526ea74e82d9..86f467a2c485 100644
--- a/drivers/net/ethernet/chelsio/cxgb/sge.c
+++ b/drivers/net/ethernet/chelsio/cxgb/sge.c
@@ -1664,8 +1664,7 @@ static int t1_sge_tx(struct sk_buff *skb, struct adapter *adapter,
 	struct cmdQ *q = &sge->cmdQ[qid];
 	unsigned int credits, pidx, genbit, count, use_sched_skb = 0;
 
-	if (!spin_trylock(&q->lock))
-		return NETDEV_TX_LOCKED;
+	spin_lock(&q->lock);
 
 	reclaim_completed_tx(sge, q);
 
diff --git a/drivers/net/ethernet/neterion/s2io.c b/drivers/net/ethernet/neterion/s2io.c
index 9ba975853ec6..813cfa698160 100644
--- a/drivers/net/ethernet/neterion/s2io.c
+++ b/drivers/net/ethernet/neterion/s2io.c
@@ -4084,12 +4084,7 @@ static netdev_tx_t s2io_xmit(struct sk_buff *skb, struct net_device *dev)
 			[skb->priority & (MAX_TX_FIFOS - 1)];
 	fifo = &mac_control->fifos[queue];
 
-	if (do_spin_lock)
-		spin_lock_irqsave(&fifo->tx_lock, flags);
-	else {
-		if (unlikely(!spin_trylock_irqsave(&fifo->tx_lock, flags)))
-			return NETDEV_TX_LOCKED;
-	}
+	spin_lock_irqsave(&fifo->tx_lock, flags);
 
 	if (sp->config.multiq) {
 		if (__netif_subqueue_stopped(dev, fifo->fifo_no)) {
diff --git a/drivers/net/ethernet/oki-semi/pch_gbe/pch_gbe_main.c b/drivers/net/ethernet/oki-semi/pch_gbe/pch_gbe_main.c
index 3b98b263bad0..ca4add749410 100644
--- a/drivers/net/ethernet/oki-semi/pch_gbe/pch_gbe_main.c
+++ b/drivers/net/ethernet/oki-semi/pch_gbe/pch_gbe_main.c
@@ -2137,10 +2137,8 @@ static int pch_gbe_xmit_frame(struct sk_buff *skb, struct net_device *netdev)
 	struct pch_gbe_tx_ring *tx_ring = adapter->tx_ring;
 	unsigned long flags;
 
-	if (!spin_trylock_irqsave(&tx_ring->tx_lock, flags)) {
-		/* Collision - tell upper layer to requeue */
-		return NETDEV_TX_LOCKED;
-	}
+	spin_lock_irqsave(&tx_ring->tx_lock, flags);
+
 	if (unlikely(!PCH_GBE_DESC_UNUSED(tx_ring))) {
 		netif_stop_queue(netdev);
 		spin_unlock_irqrestore(&tx_ring->tx_lock, flags);
diff --git a/drivers/net/ethernet/realtek/8139too.c b/drivers/net/ethernet/realtek/8139too.c
index ef668d300800..d987d571fdd6 100644
--- a/drivers/net/ethernet/realtek/8139too.c
+++ b/drivers/net/ethernet/realtek/8139too.c
@@ -2229,7 +2229,7 @@ static void rtl8139_poll_controller(struct net_device *dev)
 	struct rtl8139_private *tp = netdev_priv(dev);
 	const int irq = tp->pci_dev->irq;
 
-	disable_irq(irq);
+	disable_irq_nosync(irq);
 	rtl8139_interrupt(irq, dev);
 	enable_irq(irq);
 }
diff --git a/drivers/net/ethernet/tehuti/tehuti.c b/drivers/net/ethernet/tehuti/tehuti.c
index 14c9d1baa85c..e1a5305418a8 100644
--- a/drivers/net/ethernet/tehuti/tehuti.c
+++ b/drivers/net/ethernet/tehuti/tehuti.c
@@ -1629,13 +1629,8 @@ static netdev_tx_t bdx_tx_transmit(struct sk_buff *skb,
 	unsigned long flags;
 
 	ENTER;
-	local_irq_save(flags);
-	if (!spin_trylock(&priv->tx_lock)) {
-		local_irq_restore(flags);
-		DBG("%s[%s]: TX locked, returning NETDEV_TX_LOCKED\n",
-		    BDX_DRV_NAME, ndev->name);
-		return NETDEV_TX_LOCKED;
-	}
+
+	spin_lock_irqsave(&priv->tx_lock, flags);
 
 	/* build tx descriptor */
 	BDX_ASSERT(f->m.wptr >= f->m.memsz);	/* started with valid wptr */
diff --git a/drivers/net/rionet.c b/drivers/net/rionet.c
index 01f08a7751f7..aed2659fa3f2 100644
--- a/drivers/net/rionet.c
+++ b/drivers/net/rionet.c
@@ -174,11 +174,7 @@ static int rionet_start_xmit(struct sk_buff *skb, struct net_device *ndev)
 	unsigned long flags;
 	int add_num = 1;
 
-	local_irq_save(flags);
-	if (!spin_trylock(&rnet->tx_lock)) {
-		local_irq_restore(flags);
-		return NETDEV_TX_LOCKED;
-	}
+	spin_lock_irqsave(&rnet->tx_lock, flags);
 
 	if (is_multicast_ether_addr(eth->h_dest))
 		add_num = nets[rnet->mport->id].nact;
diff --git a/drivers/net/wireless/orinoco/orinoco_usb.c b/drivers/net/wireless/orinoco/orinoco_usb.c
index f2cd513d54b2..6c0f4c9638a2 100644
--- a/drivers/net/wireless/orinoco/orinoco_usb.c
+++ b/drivers/net/wireless/orinoco/orinoco_usb.c
@@ -697,7 +697,7 @@ static void ezusb_req_ctx_wait(struct ezusb_priv *upriv,
 			while (!ctx->done.done && msecs--)
 				udelay(1000);
 		} else {
-			wait_event_interruptible(ctx->done.wait,
+			swait_event_interruptible(ctx->done.wait,
 						 ctx->done.done);
 		}
 		break;
diff --git a/drivers/pci/access.c b/drivers/pci/access.c
index 59ac36fe7c42..7a45a20af78a 100644
--- a/drivers/pci/access.c
+++ b/drivers/pci/access.c
@@ -561,7 +561,7 @@ void pci_cfg_access_unlock(struct pci_dev *dev)
 	WARN_ON(!dev->block_cfg_access);
 
 	dev->block_cfg_access = 0;
-	wake_up_all(&pci_cfg_wait);
+	wake_up_all_locked(&pci_cfg_wait);
 	raw_spin_unlock_irqrestore(&pci_lock, flags);
 }
 EXPORT_SYMBOL_GPL(pci_cfg_access_unlock);
diff --git a/drivers/scsi/fcoe/fcoe.c b/drivers/scsi/fcoe/fcoe.c
index f4424063b860..f1622a05854b 100644
--- a/drivers/scsi/fcoe/fcoe.c
+++ b/drivers/scsi/fcoe/fcoe.c
@@ -1286,7 +1286,7 @@ static void fcoe_percpu_thread_destroy(unsigned int cpu)
 	struct sk_buff *skb;
 #ifdef CONFIG_SMP
 	struct fcoe_percpu_s *p0;
-	unsigned targ_cpu = get_cpu();
+	unsigned targ_cpu = get_cpu_light();
 #endif /* CONFIG_SMP */
 
 	FCOE_DBG("Destroying receive thread for CPU %d\n", cpu);
@@ -1342,7 +1342,7 @@ static void fcoe_percpu_thread_destroy(unsigned int cpu)
 			kfree_skb(skb);
 		spin_unlock_bh(&p->fcoe_rx_list.lock);
 	}
-	put_cpu();
+	put_cpu_light();
 #else
 	/*
 	 * This a non-SMP scenario where the singular Rx thread is
@@ -1566,11 +1566,11 @@ static int fcoe_rcv(struct sk_buff *skb, struct net_device *netdev,
 static int fcoe_alloc_paged_crc_eof(struct sk_buff *skb, int tlen)
 {
 	struct fcoe_percpu_s *fps;
-	int rc;
+	int rc, cpu = get_cpu_light();
 
-	fps = &get_cpu_var(fcoe_percpu);
+	fps = &per_cpu(fcoe_percpu, cpu);
 	rc = fcoe_get_paged_crc_eof(skb, tlen, fps);
-	put_cpu_var(fcoe_percpu);
+	put_cpu_light();
 
 	return rc;
 }
@@ -1766,11 +1766,11 @@ static inline int fcoe_filter_frames(struct fc_lport *lport,
 		return 0;
 	}
 
-	stats = per_cpu_ptr(lport->stats, get_cpu());
+	stats = per_cpu_ptr(lport->stats, get_cpu_light());
 	stats->InvalidCRCCount++;
 	if (stats->InvalidCRCCount < 5)
 		printk(KERN_WARNING "fcoe: dropping frame with CRC error\n");
-	put_cpu();
+	put_cpu_light();
 	return -EINVAL;
 }
 
@@ -1846,13 +1846,13 @@ static void fcoe_recv_frame(struct sk_buff *skb)
 		goto drop;
 
 	if (!fcoe_filter_frames(lport, fp)) {
-		put_cpu();
+		put_cpu_light();
 		fc_exch_recv(lport, fp);
 		return;
 	}
 drop:
 	stats->ErrorFrames++;
-	put_cpu();
+	put_cpu_light();
 	kfree_skb(skb);
 }
 
diff --git a/drivers/scsi/fcoe/fcoe_ctlr.c b/drivers/scsi/fcoe/fcoe_ctlr.c
index 34a1b1f333b4..d91131210695 100644
--- a/drivers/scsi/fcoe/fcoe_ctlr.c
+++ b/drivers/scsi/fcoe/fcoe_ctlr.c
@@ -831,7 +831,7 @@ static unsigned long fcoe_ctlr_age_fcfs(struct fcoe_ctlr *fip)
 
 	INIT_LIST_HEAD(&del_list);
 
-	stats = per_cpu_ptr(fip->lp->stats, get_cpu());
+	stats = per_cpu_ptr(fip->lp->stats, get_cpu_light());
 
 	list_for_each_entry_safe(fcf, next, &fip->fcfs, list) {
 		deadline = fcf->time + fcf->fka_period + fcf->fka_period / 2;
@@ -867,7 +867,7 @@ static unsigned long fcoe_ctlr_age_fcfs(struct fcoe_ctlr *fip)
 				sel_time = fcf->time;
 		}
 	}
-	put_cpu();
+	put_cpu_light();
 
 	list_for_each_entry_safe(fcf, next, &del_list, list) {
 		/* Removes fcf from current list */
diff --git a/drivers/scsi/libfc/fc_exch.c b/drivers/scsi/libfc/fc_exch.c
index 30f9ef0c0d4f..6c686bc01a82 100644
--- a/drivers/scsi/libfc/fc_exch.c
+++ b/drivers/scsi/libfc/fc_exch.c
@@ -814,10 +814,10 @@ static struct fc_exch *fc_exch_em_alloc(struct fc_lport *lport,
 	}
 	memset(ep, 0, sizeof(*ep));
 
-	cpu = get_cpu();
+	cpu = get_cpu_light();
 	pool = per_cpu_ptr(mp->pool, cpu);
 	spin_lock_bh(&pool->lock);
-	put_cpu();
+	put_cpu_light();
 
 	/* peek cache of free slot */
 	if (pool->left != FC_XID_UNKNOWN) {
diff --git a/drivers/scsi/libsas/sas_ata.c b/drivers/scsi/libsas/sas_ata.c
index 9c706d8c1441..d968ffc79c08 100644
--- a/drivers/scsi/libsas/sas_ata.c
+++ b/drivers/scsi/libsas/sas_ata.c
@@ -190,7 +190,7 @@ static unsigned int sas_ata_qc_issue(struct ata_queued_cmd *qc)
 	/* TODO: audit callers to ensure they are ready for qc_issue to
 	 * unconditionally re-enable interrupts
 	 */
-	local_irq_save(flags);
+	local_irq_save_nort(flags);
 	spin_unlock(ap->lock);
 
 	/* If the device fell off, no sense in issuing commands */
@@ -255,7 +255,7 @@ static unsigned int sas_ata_qc_issue(struct ata_queued_cmd *qc)
 
  out:
 	spin_lock(ap->lock);
-	local_irq_restore(flags);
+	local_irq_restore_nort(flags);
 	return ret;
 }
 
diff --git a/drivers/scsi/qla2xxx/qla_inline.h b/drivers/scsi/qla2xxx/qla_inline.h
index fee9eb7c8a60..b42d4adc42dc 100644
--- a/drivers/scsi/qla2xxx/qla_inline.h
+++ b/drivers/scsi/qla2xxx/qla_inline.h
@@ -59,12 +59,12 @@ qla2x00_poll(struct rsp_que *rsp)
 {
 	unsigned long flags;
 	struct qla_hw_data *ha = rsp->hw;
-	local_irq_save(flags);
+	local_irq_save_nort(flags);
 	if (IS_P3P_TYPE(ha))
 		qla82xx_poll(0, rsp);
 	else
 		ha->isp_ops->intr_handler(0, rsp);
-	local_irq_restore(flags);
+	local_irq_restore_nort(flags);
 }
 
 static inline uint8_t *
diff --git a/drivers/thermal/x86_pkg_temp_thermal.c b/drivers/thermal/x86_pkg_temp_thermal.c
index 7fc919f7da4d..a774f0c8d22b 100644
--- a/drivers/thermal/x86_pkg_temp_thermal.c
+++ b/drivers/thermal/x86_pkg_temp_thermal.c
@@ -29,6 +29,7 @@
 #include <linux/pm.h>
 #include <linux/thermal.h>
 #include <linux/debugfs.h>
+#include <linux/work-simple.h>
 #include <asm/cpu_device_id.h>
 #include <asm/mce.h>
 
@@ -352,7 +353,7 @@ static void pkg_temp_thermal_threshold_work_fn(struct work_struct *work)
 	}
 }
 
-static int pkg_temp_thermal_platform_thermal_notify(__u64 msr_val)
+static void platform_thermal_notify_work(struct swork_event *event)
 {
 	unsigned long flags;
 	int cpu = smp_processor_id();
@@ -369,7 +370,7 @@ static int pkg_temp_thermal_platform_thermal_notify(__u64 msr_val)
 			pkg_work_scheduled[phy_id]) {
 		disable_pkg_thres_interrupt();
 		spin_unlock_irqrestore(&pkg_work_lock, flags);
-		return -EINVAL;
+		return;
 	}
 	pkg_work_scheduled[phy_id] = 1;
 	spin_unlock_irqrestore(&pkg_work_lock, flags);
@@ -378,9 +379,48 @@ static int pkg_temp_thermal_platform_thermal_notify(__u64 msr_val)
 	schedule_delayed_work_on(cpu,
 				&per_cpu(pkg_temp_thermal_threshold_work, cpu),
 				msecs_to_jiffies(notify_delay_ms));
+}
+
+#ifdef CONFIG_PREEMPT_RT_FULL
+static struct swork_event notify_work;
+
+static int thermal_notify_work_init(void)
+{
+	int err;
+
+	err = swork_get();
+	if (err)
+		return err;
+
+	INIT_SWORK(&notify_work, platform_thermal_notify_work);
 	return 0;
 }
 
+static void thermal_notify_work_cleanup(void)
+{
+	swork_put();
+}
+
+static int pkg_temp_thermal_platform_thermal_notify(__u64 msr_val)
+{
+	swork_queue(&notify_work);
+	return 0;
+}
+
+#else  /* !CONFIG_PREEMPT_RT_FULL */
+
+static int thermal_notify_work_init(void) { return 0; }
+
+static void thermal_notify_work_cleanup(void) {  }
+
+static int pkg_temp_thermal_platform_thermal_notify(__u64 msr_val)
+{
+	platform_thermal_notify_work(NULL);
+
+	return 0;
+}
+#endif /* CONFIG_PREEMPT_RT_FULL */
+
 static int find_siblings_cpu(int cpu)
 {
 	int i;
@@ -584,6 +624,9 @@ static int __init pkg_temp_thermal_init(void)
 	if (!x86_match_cpu(pkg_temp_thermal_ids))
 		return -ENODEV;
 
+	if (!thermal_notify_work_init())
+		return -ENODEV;
+
 	spin_lock_init(&pkg_work_lock);
 	platform_thermal_package_notify =
 			pkg_temp_thermal_platform_thermal_notify;
@@ -608,7 +651,7 @@ static int __init pkg_temp_thermal_init(void)
 	kfree(pkg_work_scheduled);
 	platform_thermal_package_notify = NULL;
 	platform_thermal_package_rate_control = NULL;
-
+	thermal_notify_work_cleanup();
 	return -ENODEV;
 }
 
@@ -633,6 +676,7 @@ static void __exit pkg_temp_thermal_exit(void)
 	mutex_unlock(&phy_dev_list_mutex);
 	platform_thermal_package_notify = NULL;
 	platform_thermal_package_rate_control = NULL;
+	thermal_notify_work_cleanup();
 	for_each_online_cpu(i)
 		cancel_delayed_work_sync(
 			&per_cpu(pkg_temp_thermal_threshold_work, i));
diff --git a/drivers/tty/serial/8250/8250_core.c b/drivers/tty/serial/8250/8250_core.c
index 39126460c1f5..af7701ca4d48 100644
--- a/drivers/tty/serial/8250/8250_core.c
+++ b/drivers/tty/serial/8250/8250_core.c
@@ -58,7 +58,16 @@ static struct uart_driver serial8250_reg;
 
 static unsigned int skip_txen_test; /* force skip of txen test at init time */
 
-#define PASS_LIMIT	512
+/*
+ * On -rt we can have a more delays, and legitimately
+ * so - so don't drop work spuriously and spam the
+ * syslog:
+ */
+#ifdef CONFIG_PREEMPT_RT_FULL
+# define PASS_LIMIT	1000000
+#else
+# define PASS_LIMIT	512
+#endif
 
 #include <asm/serial.h>
 /*
diff --git a/drivers/tty/serial/8250/8250_port.c b/drivers/tty/serial/8250/8250_port.c
index 52d82d2ac726..5568d70eee0c 100644
--- a/drivers/tty/serial/8250/8250_port.c
+++ b/drivers/tty/serial/8250/8250_port.c
@@ -35,6 +35,7 @@
 #include <linux/nmi.h>
 #include <linux/mutex.h>
 #include <linux/slab.h>
+#include <linux/kdb.h>
 #include <linux/uaccess.h>
 #include <linux/pm_runtime.h>
 
@@ -2851,7 +2852,7 @@ void serial8250_console_write(struct uart_8250_port *up, const char *s,
 
 	if (port->sysrq)
 		locked = 0;
-	else if (oops_in_progress)
+	else if (oops_in_progress || in_kdb_printk())
 		locked = spin_trylock_irqsave(&port->lock, flags);
 	else
 		spin_lock_irqsave(&port->lock, flags);
diff --git a/drivers/tty/serial/amba-pl011.c b/drivers/tty/serial/amba-pl011.c
index 899a77187bde..3ff6363b3751 100644
--- a/drivers/tty/serial/amba-pl011.c
+++ b/drivers/tty/serial/amba-pl011.c
@@ -2067,13 +2067,19 @@ pl011_console_write(struct console *co, const char *s, unsigned int count)
 
 	clk_enable(uap->clk);
 
-	local_irq_save(flags);
+	/*
+	 * local_irq_save(flags);
+	 *
+	 * This local_irq_save() is nonsense. If we come in via sysrq
+	 * handling then interrupts are already disabled. Aside of
+	 * that the port.sysrq check is racy on SMP regardless.
+	*/
 	if (uap->port.sysrq)
 		locked = 0;
 	else if (oops_in_progress)
-		locked = spin_trylock(&uap->port.lock);
+		locked = spin_trylock_irqsave(&uap->port.lock, flags);
 	else
-		spin_lock(&uap->port.lock);
+		spin_lock_irqsave(&uap->port.lock, flags);
 
 	/*
 	 *	First save the CR then disable the interrupts
@@ -2098,8 +2104,7 @@ pl011_console_write(struct console *co, const char *s, unsigned int count)
 		writew(old_cr, uap->port.membase + UART011_CR);
 
 	if (locked)
-		spin_unlock(&uap->port.lock);
-	local_irq_restore(flags);
+		spin_unlock_irqrestore(&uap->port.lock, flags);
 
 	clk_disable(uap->clk);
 }
diff --git a/drivers/tty/serial/omap-serial.c b/drivers/tty/serial/omap-serial.c
index 9d4c84f7485f..29ad4a224aa8 100644
--- a/drivers/tty/serial/omap-serial.c
+++ b/drivers/tty/serial/omap-serial.c
@@ -1257,13 +1257,10 @@ serial_omap_console_write(struct console *co, const char *s,
 
 	pm_runtime_get_sync(up->dev);
 
-	local_irq_save(flags);
-	if (up->port.sysrq)
-		locked = 0;
-	else if (oops_in_progress)
-		locked = spin_trylock(&up->port.lock);
+	if (up->port.sysrq || oops_in_progress)
+		locked = spin_trylock_irqsave(&up->port.lock, flags);
 	else
-		spin_lock(&up->port.lock);
+		spin_lock_irqsave(&up->port.lock, flags);
 
 	/*
 	 * First save the IER then disable the interrupts
@@ -1292,8 +1289,7 @@ serial_omap_console_write(struct console *co, const char *s,
 	pm_runtime_mark_last_busy(up->dev);
 	pm_runtime_put_autosuspend(up->dev);
 	if (locked)
-		spin_unlock(&up->port.lock);
-	local_irq_restore(flags);
+		spin_unlock_irqrestore(&up->port.lock, flags);
 }
 
 static int __init
diff --git a/drivers/usb/core/hcd.c b/drivers/usb/core/hcd.c
index 1c102d60cd9f..a8f2f5bf2674 100644
--- a/drivers/usb/core/hcd.c
+++ b/drivers/usb/core/hcd.c
@@ -1735,9 +1735,9 @@ static void __usb_hcd_giveback_urb(struct urb *urb)
 	 * and no one may trigger the above deadlock situation when
 	 * running complete() in tasklet.
 	 */
-	local_irq_save(flags);
+	local_irq_save_nort(flags);
 	urb->complete(urb);
-	local_irq_restore(flags);
+	local_irq_restore_nort(flags);
 
 	usb_anchor_resume_wakeups(anchor);
 	atomic_dec(&urb->use_count);
diff --git a/drivers/usb/gadget/function/f_fs.c b/drivers/usb/gadget/function/f_fs.c
index cf43e9e18368..3ba63db4dce7 100644
--- a/drivers/usb/gadget/function/f_fs.c
+++ b/drivers/usb/gadget/function/f_fs.c
@@ -1405,7 +1405,7 @@ static void ffs_data_put(struct ffs_data *ffs)
 		pr_info("%s(): freeing\n", __func__);
 		ffs_data_clear(ffs);
 		BUG_ON(waitqueue_active(&ffs->ev.waitq) ||
-		       waitqueue_active(&ffs->ep0req_completion.wait));
+		       swaitqueue_active(&ffs->ep0req_completion.wait));
 		kfree(ffs->dev_name);
 		kfree(ffs);
 	}
diff --git a/drivers/usb/gadget/legacy/inode.c b/drivers/usb/gadget/legacy/inode.c
index f454c7af489c..b20a60343664 100644
--- a/drivers/usb/gadget/legacy/inode.c
+++ b/drivers/usb/gadget/legacy/inode.c
@@ -345,7 +345,7 @@ ep_io (struct ep_data *epdata, void *buf, unsigned len)
 	spin_unlock_irq (&epdata->dev->lock);
 
 	if (likely (value == 0)) {
-		value = wait_event_interruptible (done.wait, done.done);
+		value = swait_event_interruptible (done.wait, done.done);
 		if (value != 0) {
 			spin_lock_irq (&epdata->dev->lock);
 			if (likely (epdata->ep != NULL)) {
@@ -354,7 +354,7 @@ ep_io (struct ep_data *epdata, void *buf, unsigned len)
 				usb_ep_dequeue (epdata->ep, epdata->req);
 				spin_unlock_irq (&epdata->dev->lock);
 
-				wait_event (done.wait, done.done);
+				swait_event (done.wait, done.done);
 				if (epdata->status == -ECONNRESET)
 					epdata->status = -EINTR;
 			} else {
diff --git a/fs/aio.c b/fs/aio.c
index 155f84253f33..14af01540288 100644
--- a/fs/aio.c
+++ b/fs/aio.c
@@ -40,6 +40,7 @@
 #include <linux/ramfs.h>
 #include <linux/percpu-refcount.h>
 #include <linux/mount.h>
+#include <linux/work-simple.h>
 
 #include <asm/kmap_types.h>
 #include <asm/uaccess.h>
@@ -115,7 +116,7 @@ struct kioctx {
 	struct page		**ring_pages;
 	long			nr_pages;
 
-	struct work_struct	free_work;
+	struct swork_event	free_work;
 
 	/*
 	 * signals when all in-flight requests are done
@@ -253,6 +254,7 @@ static int __init aio_setup(void)
 		.mount		= aio_mount,
 		.kill_sb	= kill_anon_super,
 	};
+	BUG_ON(swork_get());
 	aio_mnt = kern_mount(&aio_fs);
 	if (IS_ERR(aio_mnt))
 		panic("Failed to create aio fs mount.");
@@ -568,9 +570,9 @@ static int kiocb_cancel(struct aio_kiocb *kiocb)
 	return cancel(&kiocb->common);
 }
 
-static void free_ioctx(struct work_struct *work)
+static void free_ioctx(struct swork_event *sev)
 {
-	struct kioctx *ctx = container_of(work, struct kioctx, free_work);
+	struct kioctx *ctx = container_of(sev, struct kioctx, free_work);
 
 	pr_debug("freeing %p\n", ctx);
 
@@ -589,8 +591,8 @@ static void free_ioctx_reqs(struct percpu_ref *ref)
 	if (ctx->rq_wait && atomic_dec_and_test(&ctx->rq_wait->count))
 		complete(&ctx->rq_wait->comp);
 
-	INIT_WORK(&ctx->free_work, free_ioctx);
-	schedule_work(&ctx->free_work);
+	INIT_SWORK(&ctx->free_work, free_ioctx);
+	swork_queue(&ctx->free_work);
 }
 
 /*
@@ -598,9 +600,9 @@ static void free_ioctx_reqs(struct percpu_ref *ref)
  * and ctx->users has dropped to 0, so we know no more kiocbs can be submitted -
  * now it's safe to cancel any that need to be.
  */
-static void free_ioctx_users(struct percpu_ref *ref)
+static void free_ioctx_users_work(struct swork_event *sev)
 {
-	struct kioctx *ctx = container_of(ref, struct kioctx, users);
+	struct kioctx *ctx = container_of(sev, struct kioctx, free_work);
 	struct aio_kiocb *req;
 
 	spin_lock_irq(&ctx->ctx_lock);
@@ -619,6 +621,14 @@ static void free_ioctx_users(struct percpu_ref *ref)
 	percpu_ref_put(&ctx->reqs);
 }
 
+static void free_ioctx_users(struct percpu_ref *ref)
+{
+	struct kioctx *ctx = container_of(ref, struct kioctx, users);
+
+	INIT_SWORK(&ctx->free_work, free_ioctx_users_work);
+	swork_queue(&ctx->free_work);
+}
+
 static int ioctx_add_table(struct kioctx *ctx, struct mm_struct *mm)
 {
 	unsigned i, new_nr;
diff --git a/fs/autofs4/autofs_i.h b/fs/autofs4/autofs_i.h
index c37149b929be..4c541347308e 100644
--- a/fs/autofs4/autofs_i.h
+++ b/fs/autofs4/autofs_i.h
@@ -34,6 +34,7 @@
 #include <linux/sched.h>
 #include <linux/mount.h>
 #include <linux/namei.h>
+#include <linux/delay.h>
 #include <asm/current.h>
 #include <asm/uaccess.h>
 
diff --git a/fs/autofs4/expire.c b/fs/autofs4/expire.c
index 1cebc3c52fa5..d487fa27add5 100644
--- a/fs/autofs4/expire.c
+++ b/fs/autofs4/expire.c
@@ -150,7 +150,7 @@ static struct dentry *get_next_positive_dentry(struct dentry *prev,
 			parent = p->d_parent;
 			if (!spin_trylock(&parent->d_lock)) {
 				spin_unlock(&p->d_lock);
-				cpu_relax();
+				cpu_chill();
 				goto relock;
 			}
 			spin_unlock(&p->d_lock);
diff --git a/fs/btrfs/volumes.c b/fs/btrfs/volumes.c
index a23399e8e3ab..1fc2e13fc2c1 100644
--- a/fs/btrfs/volumes.c
+++ b/fs/btrfs/volumes.c
@@ -232,6 +232,7 @@ static struct btrfs_device *__alloc_device(void)
 	spin_lock_init(&dev->reada_lock);
 	atomic_set(&dev->reada_in_flight, 0);
 	atomic_set(&dev->dev_stats_ccnt, 0);
+	btrfs_device_data_ordered_init(dev);
 	INIT_RADIX_TREE(&dev->reada_zones, GFP_NOFS & ~__GFP_DIRECT_RECLAIM);
 	INIT_RADIX_TREE(&dev->reada_extents, GFP_NOFS & ~__GFP_DIRECT_RECLAIM);
 
diff --git a/fs/buffer.c b/fs/buffer.c
index 4f4cd959da7c..72b27e17b907 100644
--- a/fs/buffer.c
+++ b/fs/buffer.c
@@ -305,8 +305,7 @@ static void end_buffer_async_read(struct buffer_head *bh, int uptodate)
 	 * decide that the page is now completely done.
 	 */
 	first = page_buffers(page);
-	local_irq_save(flags);
-	bit_spin_lock(BH_Uptodate_Lock, &first->b_state);
+	flags = bh_uptodate_lock_irqsave(first);
 	clear_buffer_async_read(bh);
 	unlock_buffer(bh);
 	tmp = bh;
@@ -319,8 +318,7 @@ static void end_buffer_async_read(struct buffer_head *bh, int uptodate)
 		}
 		tmp = tmp->b_this_page;
 	} while (tmp != bh);
-	bit_spin_unlock(BH_Uptodate_Lock, &first->b_state);
-	local_irq_restore(flags);
+	bh_uptodate_unlock_irqrestore(first, flags);
 
 	/*
 	 * If none of the buffers had errors and they are all
@@ -332,9 +330,7 @@ static void end_buffer_async_read(struct buffer_head *bh, int uptodate)
 	return;
 
 still_busy:
-	bit_spin_unlock(BH_Uptodate_Lock, &first->b_state);
-	local_irq_restore(flags);
-	return;
+	bh_uptodate_unlock_irqrestore(first, flags);
 }
 
 /*
@@ -362,8 +358,7 @@ void end_buffer_async_write(struct buffer_head *bh, int uptodate)
 	}
 
 	first = page_buffers(page);
-	local_irq_save(flags);
-	bit_spin_lock(BH_Uptodate_Lock, &first->b_state);
+	flags = bh_uptodate_lock_irqsave(first);
 
 	clear_buffer_async_write(bh);
 	unlock_buffer(bh);
@@ -375,15 +370,12 @@ void end_buffer_async_write(struct buffer_head *bh, int uptodate)
 		}
 		tmp = tmp->b_this_page;
 	}
-	bit_spin_unlock(BH_Uptodate_Lock, &first->b_state);
-	local_irq_restore(flags);
+	bh_uptodate_unlock_irqrestore(first, flags);
 	end_page_writeback(page);
 	return;
 
 still_busy:
-	bit_spin_unlock(BH_Uptodate_Lock, &first->b_state);
-	local_irq_restore(flags);
-	return;
+	bh_uptodate_unlock_irqrestore(first, flags);
 }
 EXPORT_SYMBOL(end_buffer_async_write);
 
@@ -3325,6 +3317,7 @@ struct buffer_head *alloc_buffer_head(gfp_t gfp_flags)
 	struct buffer_head *ret = kmem_cache_zalloc(bh_cachep, gfp_flags);
 	if (ret) {
 		INIT_LIST_HEAD(&ret->b_assoc_buffers);
+		buffer_head_init_locks(ret);
 		preempt_disable();
 		__this_cpu_inc(bh_accounting.nr);
 		recalc_bh_state();
diff --git a/fs/dcache.c b/fs/dcache.c
index 5c33aeb0f68f..1b218d5932f0 100644
--- a/fs/dcache.c
+++ b/fs/dcache.c
@@ -19,6 +19,7 @@
 #include <linux/mm.h>
 #include <linux/fs.h>
 #include <linux/fsnotify.h>
+#include <linux/delay.h>
 #include <linux/slab.h>
 #include <linux/init.h>
 #include <linux/hash.h>
@@ -589,7 +590,7 @@ static struct dentry *dentry_kill(struct dentry *dentry)
 
 failed:
 	spin_unlock(&dentry->d_lock);
-	cpu_relax();
+	cpu_chill();
 	return dentry; /* try again with same dentry */
 }
 
@@ -2398,7 +2399,7 @@ void d_delete(struct dentry * dentry)
 	if (dentry->d_lockref.count == 1) {
 		if (!spin_trylock(&inode->i_lock)) {
 			spin_unlock(&dentry->d_lock);
-			cpu_relax();
+			cpu_chill();
 			goto again;
 		}
 		dentry->d_flags &= ~DCACHE_CANT_MOUNT;
diff --git a/fs/eventpoll.c b/fs/eventpoll.c
index 1e009cad8d5c..d0c12504d3b4 100644
--- a/fs/eventpoll.c
+++ b/fs/eventpoll.c
@@ -505,12 +505,12 @@ static int ep_poll_wakeup_proc(void *priv, void *cookie, int call_nests)
  */
 static void ep_poll_safewake(wait_queue_head_t *wq)
 {
-	int this_cpu = get_cpu();
+	int this_cpu = get_cpu_light();
 
 	ep_call_nested(&poll_safewake_ncalls, EP_MAX_NESTS,
 		       ep_poll_wakeup_proc, NULL, wq, (void *) (long) this_cpu);
 
-	put_cpu();
+	put_cpu_light();
 }
 
 static void ep_remove_wait_queue(struct eppoll_entry *pwq)
diff --git a/fs/exec.c b/fs/exec.c
index b06623a9347f..e7760b7b692c 100644
--- a/fs/exec.c
+++ b/fs/exec.c
@@ -865,12 +865,14 @@ static int exec_mmap(struct mm_struct *mm)
 		}
 	}
 	task_lock(tsk);
+	preempt_disable_rt();
 	active_mm = tsk->active_mm;
 	tsk->mm = mm;
 	tsk->active_mm = mm;
 	activate_mm(active_mm, mm);
 	tsk->mm->vmacache_seqnum = 0;
 	vmacache_flush(tsk);
+	preempt_enable_rt();
 	task_unlock(tsk);
 	if (old_mm) {
 		up_read(&old_mm->mmap_sem);
diff --git a/fs/jbd2/checkpoint.c b/fs/jbd2/checkpoint.c
index 684996c8a3a4..6e18a06aaabe 100644
--- a/fs/jbd2/checkpoint.c
+++ b/fs/jbd2/checkpoint.c
@@ -116,6 +116,8 @@ void __jbd2_log_wait_for_space(journal_t *journal)
 	nblocks = jbd2_space_needed(journal);
 	while (jbd2_log_space_left(journal) < nblocks) {
 		write_unlock(&journal->j_state_lock);
+		if (current->plug)
+			io_schedule();
 		mutex_lock(&journal->j_checkpoint_mutex);
 
 		/*
diff --git a/fs/namespace.c b/fs/namespace.c
index 0570729c87fd..62588bfcddf4 100644
--- a/fs/namespace.c
+++ b/fs/namespace.c
@@ -14,6 +14,7 @@
 #include <linux/mnt_namespace.h>
 #include <linux/user_namespace.h>
 #include <linux/namei.h>
+#include <linux/delay.h>
 #include <linux/security.h>
 #include <linux/idr.h>
 #include <linux/init.h>		/* init_rootfs */
@@ -353,8 +354,11 @@ int __mnt_want_write(struct vfsmount *m)
 	 * incremented count after it has set MNT_WRITE_HOLD.
 	 */
 	smp_mb();
-	while (ACCESS_ONCE(mnt->mnt.mnt_flags) & MNT_WRITE_HOLD)
-		cpu_relax();
+	while (ACCESS_ONCE(mnt->mnt.mnt_flags) & MNT_WRITE_HOLD) {
+		preempt_enable();
+		cpu_chill();
+		preempt_disable();
+	}
 	/*
 	 * After the slowpath clears MNT_WRITE_HOLD, mnt_is_readonly will
 	 * be set to match its requirements. So we must not load that until
diff --git a/fs/ntfs/aops.c b/fs/ntfs/aops.c
index 7521e11db728..f0de4b6b8bf3 100644
--- a/fs/ntfs/aops.c
+++ b/fs/ntfs/aops.c
@@ -107,8 +107,7 @@ static void ntfs_end_buffer_async_read(struct buffer_head *bh, int uptodate)
 				"0x%llx.", (unsigned long long)bh->b_blocknr);
 	}
 	first = page_buffers(page);
-	local_irq_save(flags);
-	bit_spin_lock(BH_Uptodate_Lock, &first->b_state);
+	flags = bh_uptodate_lock_irqsave(first);
 	clear_buffer_async_read(bh);
 	unlock_buffer(bh);
 	tmp = bh;
@@ -123,8 +122,7 @@ static void ntfs_end_buffer_async_read(struct buffer_head *bh, int uptodate)
 		}
 		tmp = tmp->b_this_page;
 	} while (tmp != bh);
-	bit_spin_unlock(BH_Uptodate_Lock, &first->b_state);
-	local_irq_restore(flags);
+	bh_uptodate_unlock_irqrestore(first, flags);
 	/*
 	 * If none of the buffers had errors then we can set the page uptodate,
 	 * but we first have to perform the post read mst fixups, if the
@@ -145,13 +143,13 @@ static void ntfs_end_buffer_async_read(struct buffer_head *bh, int uptodate)
 		recs = PAGE_CACHE_SIZE / rec_size;
 		/* Should have been verified before we got here... */
 		BUG_ON(!recs);
-		local_irq_save(flags);
+		local_irq_save_nort(flags);
 		kaddr = kmap_atomic(page);
 		for (i = 0; i < recs; i++)
 			post_read_mst_fixup((NTFS_RECORD*)(kaddr +
 					i * rec_size), rec_size);
 		kunmap_atomic(kaddr);
-		local_irq_restore(flags);
+		local_irq_restore_nort(flags);
 		flush_dcache_page(page);
 		if (likely(page_uptodate && !PageError(page)))
 			SetPageUptodate(page);
@@ -159,9 +157,7 @@ static void ntfs_end_buffer_async_read(struct buffer_head *bh, int uptodate)
 	unlock_page(page);
 	return;
 still_busy:
-	bit_spin_unlock(BH_Uptodate_Lock, &first->b_state);
-	local_irq_restore(flags);
-	return;
+	bh_uptodate_unlock_irqrestore(first, flags);
 }
 
 /**
diff --git a/fs/timerfd.c b/fs/timerfd.c
index b94fa6c3c6eb..64fb86066237 100644
--- a/fs/timerfd.c
+++ b/fs/timerfd.c
@@ -450,7 +450,10 @@ static int do_timerfd_settime(int ufd, int flags,
 				break;
 		}
 		spin_unlock_irq(&ctx->wqh.lock);
-		cpu_relax();
+		if (isalarm(ctx))
+			hrtimer_wait_for_timer(&ctx->t.alarm.timer);
+		else
+			hrtimer_wait_for_timer(&ctx->t.tmr);
 	}
 
 	/*
diff --git a/include/acpi/platform/aclinux.h b/include/acpi/platform/aclinux.h
index 323e5daece54..cc5fbd534fd4 100644
--- a/include/acpi/platform/aclinux.h
+++ b/include/acpi/platform/aclinux.h
@@ -127,6 +127,7 @@
 
 #define acpi_cache_t                        struct kmem_cache
 #define acpi_spinlock                       spinlock_t *
+#define acpi_raw_spinlock		raw_spinlock_t *
 #define acpi_cpu_flags                      unsigned long
 
 /* Use native linux version of acpi_os_allocate_zeroed */
@@ -145,6 +146,20 @@
 #define ACPI_USE_ALTERNATE_PROTOTYPE_acpi_os_get_thread_id
 #define ACPI_USE_ALTERNATE_PROTOTYPE_acpi_os_create_lock
 
+#define acpi_os_create_raw_lock(__handle)			\
+({								\
+	 raw_spinlock_t *lock = ACPI_ALLOCATE(sizeof(*lock));	\
+								\
+	 if (lock) {						\
+		*(__handle) = lock;				\
+		raw_spin_lock_init(*(__handle));		\
+	 }							\
+	 lock ? AE_OK : AE_NO_MEMORY;				\
+ })
+
+#define acpi_os_delete_raw_lock(__handle)	kfree(__handle)
+
+
 /*
  * OSL interfaces used by debugger/disassembler
  */
diff --git a/include/asm-generic/bug.h b/include/asm-generic/bug.h
index 630dd2372238..850e4d993a88 100644
--- a/include/asm-generic/bug.h
+++ b/include/asm-generic/bug.h
@@ -206,6 +206,20 @@ extern void warn_slowpath_null(const char *file, const int line);
 # define WARN_ON_SMP(x)			({0;})
 #endif
 
+#ifdef CONFIG_PREEMPT_RT_BASE
+# define BUG_ON_RT(c)			BUG_ON(c)
+# define BUG_ON_NONRT(c)		do { } while (0)
+# define WARN_ON_RT(condition)		WARN_ON(condition)
+# define WARN_ON_NONRT(condition)	do { } while (0)
+# define WARN_ON_ONCE_NONRT(condition)	do { } while (0)
+#else
+# define BUG_ON_RT(c)			do { } while (0)
+# define BUG_ON_NONRT(c)		BUG_ON(c)
+# define WARN_ON_RT(condition)		do { } while (0)
+# define WARN_ON_NONRT(condition)	WARN_ON(condition)
+# define WARN_ON_ONCE_NONRT(condition)	WARN_ON_ONCE(condition)
+#endif
+
 #endif /* __ASSEMBLY__ */
 
 #endif
diff --git a/include/linux/blk-mq.h b/include/linux/blk-mq.h
index daf17d70aeca..463df8954255 100644
--- a/include/linux/blk-mq.h
+++ b/include/linux/blk-mq.h
@@ -212,6 +212,7 @@ static inline u16 blk_mq_unique_tag_to_tag(u32 unique_tag)
 
 struct blk_mq_hw_ctx *blk_mq_map_queue(struct request_queue *, const int ctx_index);
 struct blk_mq_hw_ctx *blk_mq_alloc_single_hw_queue(struct blk_mq_tag_set *, unsigned int, int);
+void __blk_mq_complete_request_remote_work(struct work_struct *work);
 
 int blk_mq_request_started(struct request *rq);
 void blk_mq_start_request(struct request *rq);
diff --git a/include/linux/blkdev.h b/include/linux/blkdev.h
index c70e3588a48c..2366b9cec665 100644
--- a/include/linux/blkdev.h
+++ b/include/linux/blkdev.h
@@ -89,6 +89,7 @@ struct request {
 	struct list_head queuelist;
 	union {
 		struct call_single_data csd;
+		struct work_struct work;
 		unsigned long fifo_time;
 	};
 
@@ -455,7 +456,7 @@ struct request_queue {
 	struct throtl_data *td;
 #endif
 	struct rcu_head		rcu_head;
-	wait_queue_head_t	mq_freeze_wq;
+	struct swait_head	mq_freeze_wq;
 	struct percpu_ref	q_usage_counter;
 	struct list_head	all_q_node;
 
diff --git a/include/linux/bottom_half.h b/include/linux/bottom_half.h
index 8fdcb783197d..d07dbeec7bc1 100644
--- a/include/linux/bottom_half.h
+++ b/include/linux/bottom_half.h
@@ -3,6 +3,39 @@
 
 #include <linux/preempt.h>
 
+#ifdef CONFIG_PREEMPT_RT_FULL
+
+extern void __local_bh_disable(void);
+extern void _local_bh_enable(void);
+extern void __local_bh_enable(void);
+
+static inline void local_bh_disable(void)
+{
+	__local_bh_disable();
+}
+
+static inline void __local_bh_disable_ip(unsigned long ip, unsigned int cnt)
+{
+	__local_bh_disable();
+}
+
+static inline void local_bh_enable(void)
+{
+	__local_bh_enable();
+}
+
+static inline void __local_bh_enable_ip(unsigned long ip, unsigned int cnt)
+{
+	__local_bh_enable();
+}
+
+static inline void local_bh_enable_ip(unsigned long ip)
+{
+	__local_bh_enable();
+}
+
+#else
+
 #ifdef CONFIG_TRACE_IRQFLAGS
 extern void __local_bh_disable_ip(unsigned long ip, unsigned int cnt);
 #else
@@ -30,5 +63,6 @@ static inline void local_bh_enable(void)
 {
 	__local_bh_enable_ip(_THIS_IP_, SOFTIRQ_DISABLE_OFFSET);
 }
+#endif
 
 #endif /* _LINUX_BH_H */
diff --git a/include/linux/buffer_head.h b/include/linux/buffer_head.h
index 89d9aa9e79bf..4a201008b02d 100644
--- a/include/linux/buffer_head.h
+++ b/include/linux/buffer_head.h
@@ -75,8 +75,50 @@ struct buffer_head {
 	struct address_space *b_assoc_map;	/* mapping this buffer is
 						   associated with */
 	atomic_t b_count;		/* users using this buffer_head */
+#ifdef CONFIG_PREEMPT_RT_BASE
+	spinlock_t b_uptodate_lock;
+#if IS_ENABLED(CONFIG_JBD2)
+	spinlock_t b_state_lock;
+	spinlock_t b_journal_head_lock;
+#endif
+#endif
 };
 
+static inline unsigned long bh_uptodate_lock_irqsave(struct buffer_head *bh)
+{
+	unsigned long flags;
+
+#ifndef CONFIG_PREEMPT_RT_BASE
+	local_irq_save(flags);
+	bit_spin_lock(BH_Uptodate_Lock, &bh->b_state);
+#else
+	spin_lock_irqsave(&bh->b_uptodate_lock, flags);
+#endif
+	return flags;
+}
+
+static inline void
+bh_uptodate_unlock_irqrestore(struct buffer_head *bh, unsigned long flags)
+{
+#ifndef CONFIG_PREEMPT_RT_BASE
+	bit_spin_unlock(BH_Uptodate_Lock, &bh->b_state);
+	local_irq_restore(flags);
+#else
+	spin_unlock_irqrestore(&bh->b_uptodate_lock, flags);
+#endif
+}
+
+static inline void buffer_head_init_locks(struct buffer_head *bh)
+{
+#ifdef CONFIG_PREEMPT_RT_BASE
+	spin_lock_init(&bh->b_uptodate_lock);
+#if IS_ENABLED(CONFIG_JBD2)
+	spin_lock_init(&bh->b_state_lock);
+	spin_lock_init(&bh->b_journal_head_lock);
+#endif
+#endif
+}
+
 /*
  * macro tricks to expand the set_buffer_foo(), clear_buffer_foo()
  * and buffer_foo() functions.
diff --git a/include/linux/cgroup-defs.h b/include/linux/cgroup-defs.h
index 06b77f9dd3f2..634111447aea 100644
--- a/include/linux/cgroup-defs.h
+++ b/include/linux/cgroup-defs.h
@@ -16,6 +16,7 @@
 #include <linux/percpu-refcount.h>
 #include <linux/percpu-rwsem.h>
 #include <linux/workqueue.h>
+#include <linux/work-simple.h>
 
 #ifdef CONFIG_CGROUPS
 
@@ -136,6 +137,7 @@ struct cgroup_subsys_state {
 	/* percpu_ref killing and RCU release */
 	struct rcu_head rcu_head;
 	struct work_struct destroy_work;
+	struct swork_event destroy_swork;
 };
 
 /*
diff --git a/include/linux/completion.h b/include/linux/completion.h
index 5d5aaae3af43..3fe8d14c98c0 100644
--- a/include/linux/completion.h
+++ b/include/linux/completion.h
@@ -7,8 +7,7 @@
  * Atomic wait-for-completion handler data structures.
  * See kernel/sched/completion.c for details.
  */
-
-#include <linux/wait.h>
+#include <linux/wait-simple.h>
 
 /*
  * struct completion - structure used to maintain state for a "completion"
@@ -24,11 +23,11 @@
  */
 struct completion {
 	unsigned int done;
-	wait_queue_head_t wait;
+	struct swait_head wait;
 };
 
 #define COMPLETION_INITIALIZER(work) \
-	{ 0, __WAIT_QUEUE_HEAD_INITIALIZER((work).wait) }
+	{ 0, SWAIT_HEAD_INITIALIZER((work).wait) }
 
 #define COMPLETION_INITIALIZER_ONSTACK(work) \
 	({ init_completion(&work); work; })
@@ -73,7 +72,7 @@ struct completion {
 static inline void init_completion(struct completion *x)
 {
 	x->done = 0;
-	init_waitqueue_head(&x->wait);
+	init_swait_head(&x->wait);
 }
 
 /**
diff --git a/include/linux/cpu.h b/include/linux/cpu.h
index d2ca8c38f9c4..94041d803d0b 100644
--- a/include/linux/cpu.h
+++ b/include/linux/cpu.h
@@ -231,6 +231,8 @@ extern void get_online_cpus(void);
 extern void put_online_cpus(void);
 extern void cpu_hotplug_disable(void);
 extern void cpu_hotplug_enable(void);
+extern void pin_current_cpu(void);
+extern void unpin_current_cpu(void);
 #define hotcpu_notifier(fn, pri)	cpu_notifier(fn, pri)
 #define __hotcpu_notifier(fn, pri)	__cpu_notifier(fn, pri)
 #define register_hotcpu_notifier(nb)	register_cpu_notifier(nb)
@@ -248,6 +250,8 @@ static inline void cpu_hotplug_done(void) {}
 #define put_online_cpus()	do { } while (0)
 #define cpu_hotplug_disable()	do { } while (0)
 #define cpu_hotplug_enable()	do { } while (0)
+static inline void pin_current_cpu(void) { }
+static inline void unpin_current_cpu(void) { }
 #define hotcpu_notifier(fn, pri)	do { (void)(fn); } while (0)
 #define __hotcpu_notifier(fn, pri)	do { (void)(fn); } while (0)
 /* These aren't inline functions due to a GCC bug. */
diff --git a/include/linux/delay.h b/include/linux/delay.h
index a6ecb34cf547..37caab306336 100644
--- a/include/linux/delay.h
+++ b/include/linux/delay.h
@@ -52,4 +52,10 @@ static inline void ssleep(unsigned int seconds)
 	msleep(seconds * 1000);
 }
 
+#ifdef CONFIG_PREEMPT_RT_FULL
+extern void cpu_chill(void);
+#else
+# define cpu_chill()	cpu_relax()
+#endif
+
 #endif /* defined(_LINUX_DELAY_H) */
diff --git a/include/linux/highmem.h b/include/linux/highmem.h
index bb3f3297062a..a117a33ef72c 100644
--- a/include/linux/highmem.h
+++ b/include/linux/highmem.h
@@ -7,6 +7,7 @@
 #include <linux/mm.h>
 #include <linux/uaccess.h>
 #include <linux/hardirq.h>
+#include <linux/sched.h>
 
 #include <asm/cacheflush.h>
 
@@ -65,7 +66,7 @@ static inline void kunmap(struct page *page)
 
 static inline void *kmap_atomic(struct page *page)
 {
-	preempt_disable();
+	preempt_disable_nort();
 	pagefault_disable();
 	return page_address(page);
 }
@@ -74,7 +75,7 @@ static inline void *kmap_atomic(struct page *page)
 static inline void __kunmap_atomic(void *addr)
 {
 	pagefault_enable();
-	preempt_enable();
+	preempt_enable_nort();
 }
 
 #define kmap_atomic_pfn(pfn)	kmap_atomic(pfn_to_page(pfn))
@@ -86,32 +87,51 @@ static inline void __kunmap_atomic(void *addr)
 
 #if defined(CONFIG_HIGHMEM) || defined(CONFIG_X86_32)
 
+#ifndef CONFIG_PREEMPT_RT_FULL
 DECLARE_PER_CPU(int, __kmap_atomic_idx);
+#endif
 
 static inline int kmap_atomic_idx_push(void)
 {
+#ifndef CONFIG_PREEMPT_RT_FULL
 	int idx = __this_cpu_inc_return(__kmap_atomic_idx) - 1;
 
-#ifdef CONFIG_DEBUG_HIGHMEM
+# ifdef CONFIG_DEBUG_HIGHMEM
 	WARN_ON_ONCE(in_irq() && !irqs_disabled());
 	BUG_ON(idx >= KM_TYPE_NR);
-#endif
+# endif
 	return idx;
+#else
+	current->kmap_idx++;
+	BUG_ON(current->kmap_idx > KM_TYPE_NR);
+	return current->kmap_idx - 1;
+#endif
 }
 
 static inline int kmap_atomic_idx(void)
 {
+#ifndef CONFIG_PREEMPT_RT_FULL
 	return __this_cpu_read(__kmap_atomic_idx) - 1;
+#else
+	return current->kmap_idx - 1;
+#endif
 }
 
 static inline void kmap_atomic_idx_pop(void)
 {
-#ifdef CONFIG_DEBUG_HIGHMEM
+#ifndef CONFIG_PREEMPT_RT_FULL
+# ifdef CONFIG_DEBUG_HIGHMEM
 	int idx = __this_cpu_dec_return(__kmap_atomic_idx);
 
 	BUG_ON(idx < 0);
-#else
+# else
 	__this_cpu_dec(__kmap_atomic_idx);
+# endif
+#else
+	current->kmap_idx--;
+# ifdef CONFIG_DEBUG_HIGHMEM
+	BUG_ON(current->kmap_idx < 0);
+# endif
 #endif
 }
 
diff --git a/include/linux/hrtimer.h b/include/linux/hrtimer.h
index 76dd4f0da5ca..27933e47ed22 100644
--- a/include/linux/hrtimer.h
+++ b/include/linux/hrtimer.h
@@ -87,6 +87,9 @@ enum hrtimer_restart {
  * @function:	timer expiry callback function
  * @base:	pointer to the timer base (per cpu and per clock)
  * @state:	state information (See bit values above)
+ * @cb_entry:	list entry to defer timers from hardirq context
+ * @irqsafe:	timer can run in hardirq context
+ * @praecox:	timer expiry time if expired at the time of programming
  * @start_pid: timer statistics field to store the pid of the task which
  *		started the timer
  * @start_site:	timer statistics field to store the site where the timer
@@ -102,6 +105,11 @@ struct hrtimer {
 	enum hrtimer_restart		(*function)(struct hrtimer *);
 	struct hrtimer_clock_base	*base;
 	unsigned long			state;
+	struct list_head		cb_entry;
+	int				irqsafe;
+#ifdef CONFIG_MISSED_TIMER_OFFSETS_HIST
+	ktime_t				praecox;
+#endif
 #ifdef CONFIG_TIMER_STATS
 	int				start_pid;
 	void				*start_site;
@@ -121,11 +129,7 @@ struct hrtimer_sleeper {
 	struct task_struct *task;
 };
 
-#ifdef CONFIG_64BIT
 # define HRTIMER_CLOCK_BASE_ALIGN	64
-#else
-# define HRTIMER_CLOCK_BASE_ALIGN	32
-#endif
 
 /**
  * struct hrtimer_clock_base - the timer base for a specific clock
@@ -134,6 +138,7 @@ struct hrtimer_sleeper {
  *			timer to a base on another cpu.
  * @clockid:		clock id for per_cpu support
  * @active:		red black tree root node for the active timers
+ * @expired:		list head for deferred timers.
  * @get_time:		function to retrieve the current time of the clock
  * @offset:		offset of this clock to the monotonic base
  */
@@ -142,6 +147,7 @@ struct hrtimer_clock_base {
 	int			index;
 	clockid_t		clockid;
 	struct timerqueue_head	active;
+	struct list_head	expired;
 	ktime_t			(*get_time)(void);
 	ktime_t			offset;
 } __attribute__((__aligned__(HRTIMER_CLOCK_BASE_ALIGN)));
@@ -185,6 +191,7 @@ struct hrtimer_cpu_base {
 	raw_spinlock_t			lock;
 	seqcount_t			seq;
 	struct hrtimer			*running;
+	struct hrtimer			*running_soft;
 	unsigned int			cpu;
 	unsigned int			active_bases;
 	unsigned int			clock_was_set_seq;
@@ -201,6 +208,9 @@ struct hrtimer_cpu_base {
 	unsigned int			nr_hangs;
 	unsigned int			max_hang_time;
 #endif
+#ifdef CONFIG_PREEMPT_RT_BASE
+	wait_queue_head_t		wait;
+#endif
 	struct hrtimer_clock_base	clock_base[HRTIMER_MAX_CLOCK_BASES];
 } ____cacheline_aligned;
 
@@ -389,6 +399,13 @@ static inline void hrtimer_restart(struct hrtimer *timer)
 	hrtimer_start_expires(timer, HRTIMER_MODE_ABS);
 }
 
+/* Softirq preemption could deadlock timer removal */
+#ifdef CONFIG_PREEMPT_RT_BASE
+  extern void hrtimer_wait_for_timer(const struct hrtimer *timer);
+#else
+# define hrtimer_wait_for_timer(timer)	do { cpu_relax(); } while (0)
+#endif
+
 /* Query timers: */
 extern ktime_t hrtimer_get_remaining(const struct hrtimer *timer);
 
@@ -408,7 +425,7 @@ static inline int hrtimer_is_queued(struct hrtimer *timer)
  * Helper function to check, whether the timer is running the callback
  * function
  */
-static inline int hrtimer_callback_running(struct hrtimer *timer)
+static inline int hrtimer_callback_running(const struct hrtimer *timer)
 {
 	return timer->base->cpu_base->running == timer;
 }
diff --git a/include/linux/idr.h b/include/linux/idr.h
index 013fd9bc4cb6..f62be0aec911 100644
--- a/include/linux/idr.h
+++ b/include/linux/idr.h
@@ -95,10 +95,14 @@ bool idr_is_empty(struct idr *idp);
  * Each idr_preload() should be matched with an invocation of this
  * function.  See idr_preload() for details.
  */
+#ifdef CONFIG_PREEMPT_RT_FULL
+void idr_preload_end(void);
+#else
 static inline void idr_preload_end(void)
 {
 	preempt_enable();
 }
+#endif
 
 /**
  * idr_find - return pointer for given id
diff --git a/include/linux/init_task.h b/include/linux/init_task.h
index 1c1ff7e4faa4..d8ec0f202eee 100644
--- a/include/linux/init_task.h
+++ b/include/linux/init_task.h
@@ -148,9 +148,16 @@ extern struct task_group root_task_group;
 # define INIT_PERF_EVENTS(tsk)
 #endif
 
+#ifdef CONFIG_PREEMPT_RT_BASE
+# define INIT_TIMER_LIST		.posix_timer_list = NULL,
+#else
+# define INIT_TIMER_LIST
+#endif
+
 #ifdef CONFIG_VIRT_CPU_ACCOUNTING_GEN
 # define INIT_VTIME(tsk)						\
-	.vtime_seqlock = __SEQLOCK_UNLOCKED(tsk.vtime_seqlock),	\
+	.vtime_lock = __RAW_SPIN_LOCK_UNLOCKED(tsk.vtime_lock),	\
+	.vtime_seq = SEQCNT_ZERO(tsk.vtime_seq),			\
 	.vtime_snap = 0,				\
 	.vtime_snap_whence = VTIME_SYS,
 #else
@@ -239,6 +246,7 @@ extern struct task_group root_task_group;
 	.cpu_timers	= INIT_CPU_TIMERS(tsk.cpu_timers),		\
 	.pi_lock	= __RAW_SPIN_LOCK_UNLOCKED(tsk.pi_lock),	\
 	.timer_slack_ns = 50000, /* 50 usec default slack */		\
+	INIT_TIMER_LIST							\
 	.pids = {							\
 		[PIDTYPE_PID]  = INIT_PID_LINK(PIDTYPE_PID),		\
 		[PIDTYPE_PGID] = INIT_PID_LINK(PIDTYPE_PGID),		\
diff --git a/include/linux/interrupt.h b/include/linux/interrupt.h
index ad16809c8596..655cee096aed 100644
--- a/include/linux/interrupt.h
+++ b/include/linux/interrupt.h
@@ -61,6 +61,7 @@
  *                interrupt handler after suspending interrupts. For system
  *                wakeup devices users need to implement wakeup detection in
  *                their interrupt handlers.
+ * IRQF_NO_SOFTIRQ_CALL - Do not process softirqs in the irq thread context (RT)
  */
 #define IRQF_SHARED		0x00000080
 #define IRQF_PROBE_SHARED	0x00000100
@@ -74,6 +75,7 @@
 #define IRQF_NO_THREAD		0x00010000
 #define IRQF_EARLY_RESUME	0x00020000
 #define IRQF_COND_SUSPEND	0x00040000
+#define IRQF_NO_SOFTIRQ_CALL	0x00080000
 
 #define IRQF_TIMER		(__IRQF_TIMER | IRQF_NO_SUSPEND | IRQF_NO_THREAD)
 
@@ -186,7 +188,7 @@ extern void devm_free_irq(struct device *dev, unsigned int irq, void *dev_id);
 #ifdef CONFIG_LOCKDEP
 # define local_irq_enable_in_hardirq()	do { } while (0)
 #else
-# define local_irq_enable_in_hardirq()	local_irq_enable()
+# define local_irq_enable_in_hardirq()	local_irq_enable_nort()
 #endif
 
 extern void disable_irq_nosync(unsigned int irq);
@@ -206,6 +208,7 @@ extern void resume_device_irqs(void);
  * @irq:		Interrupt to which notification applies
  * @kref:		Reference count, for internal use
  * @work:		Work item, for internal use
+ * @list:		List item for deferred callbacks
  * @notify:		Function to be called on change.  This will be
  *			called in process context.
  * @release:		Function to be called on release.  This will be
@@ -217,6 +220,7 @@ struct irq_affinity_notify {
 	unsigned int irq;
 	struct kref kref;
 	struct work_struct work;
+	struct list_head list;
 	void (*notify)(struct irq_affinity_notify *, const cpumask_t *mask);
 	void (*release)(struct kref *ref);
 };
@@ -379,9 +383,13 @@ extern int irq_set_irqchip_state(unsigned int irq, enum irqchip_irq_state which,
 				 bool state);
 
 #ifdef CONFIG_IRQ_FORCED_THREADING
+# ifndef CONFIG_PREEMPT_RT_BASE
 extern bool force_irqthreads;
+# else
+#  define force_irqthreads	(true)
+# endif
 #else
-#define force_irqthreads	(0)
+#define force_irqthreads	(false)
 #endif
 
 #ifndef __ARCH_SET_SOFTIRQ_PENDING
@@ -438,9 +446,10 @@ struct softirq_action
 	void	(*action)(struct softirq_action *);
 };
 
+#ifndef CONFIG_PREEMPT_RT_FULL
 asmlinkage void do_softirq(void);
 asmlinkage void __do_softirq(void);
-
+static inline void thread_do_softirq(void) { do_softirq(); }
 #ifdef __ARCH_HAS_DO_SOFTIRQ
 void do_softirq_own_stack(void);
 #else
@@ -449,13 +458,25 @@ static inline void do_softirq_own_stack(void)
 	__do_softirq();
 }
 #endif
+#else
+extern void thread_do_softirq(void);
+#endif
 
 extern void open_softirq(int nr, void (*action)(struct softirq_action *));
 extern void softirq_init(void);
 extern void __raise_softirq_irqoff(unsigned int nr);
+#ifdef CONFIG_PREEMPT_RT_FULL
+extern void __raise_softirq_irqoff_ksoft(unsigned int nr);
+#else
+static inline void __raise_softirq_irqoff_ksoft(unsigned int nr)
+{
+	__raise_softirq_irqoff(nr);
+}
+#endif
 
 extern void raise_softirq_irqoff(unsigned int nr);
 extern void raise_softirq(unsigned int nr);
+extern void softirq_check_pending_idle(void);
 
 DECLARE_PER_CPU(struct task_struct *, ksoftirqd);
 
@@ -477,8 +498,9 @@ static inline struct task_struct *this_cpu_ksoftirqd(void)
      to be executed on some cpu at least once after this.
    * If the tasklet is already scheduled, but its execution is still not
      started, it will be executed only once.
-   * If this tasklet is already running on another CPU (or schedule is called
-     from tasklet itself), it is rescheduled for later.
+   * If this tasklet is already running on another CPU, it is rescheduled
+     for later.
+   * Schedule must not be called from the tasklet itself (a lockup occurs)
    * Tasklet is strictly serialized wrt itself, but not
      wrt another tasklets. If client needs some intertask synchronization,
      he makes it with spinlocks.
@@ -503,27 +525,36 @@ struct tasklet_struct name = { NULL, 0, ATOMIC_INIT(1), func, data }
 enum
 {
 	TASKLET_STATE_SCHED,	/* Tasklet is scheduled for execution */
-	TASKLET_STATE_RUN	/* Tasklet is running (SMP only) */
+	TASKLET_STATE_RUN,	/* Tasklet is running (SMP only) */
+	TASKLET_STATE_PENDING	/* Tasklet is pending */
 };
 
-#ifdef CONFIG_SMP
+#define TASKLET_STATEF_SCHED	(1 << TASKLET_STATE_SCHED)
+#define TASKLET_STATEF_RUN	(1 << TASKLET_STATE_RUN)
+#define TASKLET_STATEF_PENDING	(1 << TASKLET_STATE_PENDING)
+
+#if defined(CONFIG_SMP) || defined(CONFIG_PREEMPT_RT_FULL)
 static inline int tasklet_trylock(struct tasklet_struct *t)
 {
 	return !test_and_set_bit(TASKLET_STATE_RUN, &(t)->state);
 }
 
+static inline int tasklet_tryunlock(struct tasklet_struct *t)
+{
+	return cmpxchg(&t->state, TASKLET_STATEF_RUN, 0) == TASKLET_STATEF_RUN;
+}
+
 static inline void tasklet_unlock(struct tasklet_struct *t)
 {
 	smp_mb__before_atomic();
 	clear_bit(TASKLET_STATE_RUN, &(t)->state);
 }
 
-static inline void tasklet_unlock_wait(struct tasklet_struct *t)
-{
-	while (test_bit(TASKLET_STATE_RUN, &(t)->state)) { barrier(); }
-}
+extern void tasklet_unlock_wait(struct tasklet_struct *t);
+
 #else
 #define tasklet_trylock(t) 1
+#define tasklet_tryunlock(t)	1
 #define tasklet_unlock_wait(t) do { } while (0)
 #define tasklet_unlock(t) do { } while (0)
 #endif
@@ -572,12 +603,7 @@ static inline void tasklet_disable(struct tasklet_struct *t)
 	smp_mb();
 }
 
-static inline void tasklet_enable(struct tasklet_struct *t)
-{
-	smp_mb__before_atomic();
-	atomic_dec(&t->count);
-}
-
+extern void tasklet_enable(struct tasklet_struct *t);
 extern void tasklet_kill(struct tasklet_struct *t);
 extern void tasklet_kill_immediate(struct tasklet_struct *t, unsigned int cpu);
 extern void tasklet_init(struct tasklet_struct *t,
@@ -608,6 +634,12 @@ void tasklet_hrtimer_cancel(struct tasklet_hrtimer *ttimer)
 	tasklet_kill(&ttimer->tasklet);
 }
 
+#ifdef CONFIG_PREEMPT_RT_FULL
+extern void softirq_early_init(void);
+#else
+static inline void softirq_early_init(void) { }
+#endif
+
 /*
  * Autoprobing for irqs:
  *
diff --git a/include/linux/irq.h b/include/linux/irq.h
index 3c1c96786248..311d3f061452 100644
--- a/include/linux/irq.h
+++ b/include/linux/irq.h
@@ -72,6 +72,7 @@ enum irqchip_irq_state;
  * IRQ_IS_POLLED		- Always polled by another interrupt. Exclude
  *				  it from the spurious interrupt detection
  *				  mechanism and from core side polling.
+ * IRQ_NO_SOFTIRQ_CALL		- No softirq processing in the irq thread context (RT)
  * IRQ_DISABLE_UNLAZY		- Disable lazy irq disable
  */
 enum {
@@ -99,13 +100,14 @@ enum {
 	IRQ_PER_CPU_DEVID	= (1 << 17),
 	IRQ_IS_POLLED		= (1 << 18),
 	IRQ_DISABLE_UNLAZY	= (1 << 19),
+	IRQ_NO_SOFTIRQ_CALL	= (1 << 20),
 };
 
 #define IRQF_MODIFY_MASK	\
 	(IRQ_TYPE_SENSE_MASK | IRQ_NOPROBE | IRQ_NOREQUEST | \
 	 IRQ_NOAUTOEN | IRQ_MOVE_PCNTXT | IRQ_LEVEL | IRQ_NO_BALANCING | \
 	 IRQ_PER_CPU | IRQ_NESTED_THREAD | IRQ_NOTHREAD | IRQ_PER_CPU_DEVID | \
-	 IRQ_IS_POLLED | IRQ_DISABLE_UNLAZY)
+	 IRQ_IS_POLLED | IRQ_DISABLE_UNLAZY | IRQ_NO_SOFTIRQ_CALL)
 
 #define IRQ_NO_BALANCING_MASK	(IRQ_PER_CPU | IRQ_NO_BALANCING)
 
diff --git a/include/linux/irq_work.h b/include/linux/irq_work.h
index 47b9ebd4a74f..2543aab05daa 100644
--- a/include/linux/irq_work.h
+++ b/include/linux/irq_work.h
@@ -16,6 +16,7 @@
 #define IRQ_WORK_BUSY		2UL
 #define IRQ_WORK_FLAGS		3UL
 #define IRQ_WORK_LAZY		4UL /* Doesn't want IPI, wait for tick */
+#define IRQ_WORK_HARD_IRQ	8UL /* Run hard IRQ context, even on RT */
 
 struct irq_work {
 	unsigned long flags;
@@ -51,4 +52,10 @@ static inline bool irq_work_needs_cpu(void) { return false; }
 static inline void irq_work_run(void) { }
 #endif
 
+#if defined(CONFIG_IRQ_WORK) && defined(CONFIG_PREEMPT_RT_FULL)
+void irq_work_tick_soft(void);
+#else
+static inline void irq_work_tick_soft(void) { }
+#endif
+
 #endif /* _LINUX_IRQ_WORK_H */
diff --git a/include/linux/irqdesc.h b/include/linux/irqdesc.h
index a587a33363c7..ad57402a242d 100644
--- a/include/linux/irqdesc.h
+++ b/include/linux/irqdesc.h
@@ -61,6 +61,7 @@ struct irq_desc {
 	unsigned int		irqs_unhandled;
 	atomic_t		threads_handled;
 	int			threads_handled_last;
+	u64			random_ip;
 	raw_spinlock_t		lock;
 	struct cpumask		*percpu_enabled;
 #ifdef CONFIG_SMP
diff --git a/include/linux/irqflags.h b/include/linux/irqflags.h
index 5dd1272d1ab2..9b77034f7c5e 100644
--- a/include/linux/irqflags.h
+++ b/include/linux/irqflags.h
@@ -25,8 +25,6 @@
 # define trace_softirqs_enabled(p)	((p)->softirqs_enabled)
 # define trace_hardirq_enter()	do { current->hardirq_context++; } while (0)
 # define trace_hardirq_exit()	do { current->hardirq_context--; } while (0)
-# define lockdep_softirq_enter()	do { current->softirq_context++; } while (0)
-# define lockdep_softirq_exit()	do { current->softirq_context--; } while (0)
 # define INIT_TRACE_IRQFLAGS	.softirqs_enabled = 1,
 #else
 # define trace_hardirqs_on()		do { } while (0)
@@ -39,9 +37,15 @@
 # define trace_softirqs_enabled(p)	0
 # define trace_hardirq_enter()		do { } while (0)
 # define trace_hardirq_exit()		do { } while (0)
+# define INIT_TRACE_IRQFLAGS
+#endif
+
+#if defined(CONFIG_TRACE_IRQFLAGS) && !defined(CONFIG_PREEMPT_RT_FULL)
+# define lockdep_softirq_enter() do { current->softirq_context++; } while (0)
+# define lockdep_softirq_exit()	 do { current->softirq_context--; } while (0)
+#else
 # define lockdep_softirq_enter()	do { } while (0)
 # define lockdep_softirq_exit()		do { } while (0)
-# define INIT_TRACE_IRQFLAGS
 #endif
 
 #if defined(CONFIG_IRQSOFF_TRACER) || \
@@ -148,4 +152,23 @@
 
 #define irqs_disabled_flags(flags) raw_irqs_disabled_flags(flags)
 
+/*
+ * local_irq* variants depending on RT/!RT
+ */
+#ifdef CONFIG_PREEMPT_RT_FULL
+# define local_irq_disable_nort()	do { } while (0)
+# define local_irq_enable_nort()	do { } while (0)
+# define local_irq_save_nort(flags)	local_save_flags(flags)
+# define local_irq_restore_nort(flags)	(void)(flags)
+# define local_irq_disable_rt()		local_irq_disable()
+# define local_irq_enable_rt()		local_irq_enable()
+#else
+# define local_irq_disable_nort()	local_irq_disable()
+# define local_irq_enable_nort()	local_irq_enable()
+# define local_irq_save_nort(flags)	local_irq_save(flags)
+# define local_irq_restore_nort(flags)	local_irq_restore(flags)
+# define local_irq_disable_rt()		do { } while (0)
+# define local_irq_enable_rt()		do { } while (0)
+#endif
+
 #endif
diff --git a/include/linux/jbd2.h b/include/linux/jbd2.h
index 65407f6c9120..eb5aabe4e18c 100644
--- a/include/linux/jbd2.h
+++ b/include/linux/jbd2.h
@@ -352,32 +352,56 @@ static inline struct journal_head *bh2jh(struct buffer_head *bh)
 
 static inline void jbd_lock_bh_state(struct buffer_head *bh)
 {
+#ifndef CONFIG_PREEMPT_RT_BASE
 	bit_spin_lock(BH_State, &bh->b_state);
+#else
+	spin_lock(&bh->b_state_lock);
+#endif
 }
 
 static inline int jbd_trylock_bh_state(struct buffer_head *bh)
 {
+#ifndef CONFIG_PREEMPT_RT_BASE
 	return bit_spin_trylock(BH_State, &bh->b_state);
+#else
+	return spin_trylock(&bh->b_state_lock);
+#endif
 }
 
 static inline int jbd_is_locked_bh_state(struct buffer_head *bh)
 {
+#ifndef CONFIG_PREEMPT_RT_BASE
 	return bit_spin_is_locked(BH_State, &bh->b_state);
+#else
+	return spin_is_locked(&bh->b_state_lock);
+#endif
 }
 
 static inline void jbd_unlock_bh_state(struct buffer_head *bh)
 {
+#ifndef CONFIG_PREEMPT_RT_BASE
 	bit_spin_unlock(BH_State, &bh->b_state);
+#else
+	spin_unlock(&bh->b_state_lock);
+#endif
 }
 
 static inline void jbd_lock_bh_journal_head(struct buffer_head *bh)
 {
+#ifndef CONFIG_PREEMPT_RT_BASE
 	bit_spin_lock(BH_JournalHead, &bh->b_state);
+#else
+	spin_lock(&bh->b_journal_head_lock);
+#endif
 }
 
 static inline void jbd_unlock_bh_journal_head(struct buffer_head *bh)
 {
+#ifndef CONFIG_PREEMPT_RT_BASE
 	bit_spin_unlock(BH_JournalHead, &bh->b_state);
+#else
+	spin_unlock(&bh->b_journal_head_lock);
+#endif
 }
 
 #define J_ASSERT(assert)	BUG_ON(!(assert))
diff --git a/include/linux/kdb.h b/include/linux/kdb.h
index a19bcf9e762e..897495386446 100644
--- a/include/linux/kdb.h
+++ b/include/linux/kdb.h
@@ -167,6 +167,7 @@ extern __printf(2, 0) int vkdb_printf(enum kdb_msgsrc src, const char *fmt,
 extern __printf(1, 2) int kdb_printf(const char *, ...);
 typedef __printf(1, 2) int (*kdb_printf_t)(const char *, ...);
 
+#define in_kdb_printk()	(kdb_trap_printk)
 extern void kdb_init(int level);
 
 /* Access to kdb specific polling devices */
@@ -201,6 +202,7 @@ extern int kdb_register_flags(char *, kdb_func_t, char *, char *,
 extern int kdb_unregister(char *);
 #else /* ! CONFIG_KGDB_KDB */
 static inline __printf(1, 2) int kdb_printf(const char *fmt, ...) { return 0; }
+#define in_kdb_printk() (0)
 static inline void kdb_init(int level) {}
 static inline int kdb_register(char *cmd, kdb_func_t func, char *usage,
 			       char *help, short minlen) { return 0; }
diff --git a/include/linux/kernel.h b/include/linux/kernel.h
index 350dfb08aee3..2370991aeb8f 100644
--- a/include/linux/kernel.h
+++ b/include/linux/kernel.h
@@ -188,6 +188,9 @@ extern int _cond_resched(void);
  */
 # define might_sleep() \
 	do { __might_sleep(__FILE__, __LINE__, 0); might_resched(); } while (0)
+
+# define might_sleep_no_state_check() \
+	do { ___might_sleep(__FILE__, __LINE__, 0); might_resched(); } while (0)
 # define sched_annotate_sleep()	(current->task_state_change = 0)
 #else
   static inline void ___might_sleep(const char *file, int line,
@@ -195,6 +198,7 @@ extern int _cond_resched(void);
   static inline void __might_sleep(const char *file, int line,
 				   int preempt_offset) { }
 # define might_sleep() do { might_resched(); } while (0)
+# define might_sleep_no_state_check() do { might_resched(); } while (0)
 # define sched_annotate_sleep() do { } while (0)
 #endif
 
@@ -473,6 +477,7 @@ extern enum system_states {
 	SYSTEM_HALT,
 	SYSTEM_POWER_OFF,
 	SYSTEM_RESTART,
+	SYSTEM_SUSPEND,
 } system_state;
 
 #define TAINT_PROPRIETARY_MODULE	0
diff --git a/include/linux/kvm_host.h b/include/linux/kvm_host.h
index c923350ca20a..d856ccde77b4 100644
--- a/include/linux/kvm_host.h
+++ b/include/linux/kvm_host.h
@@ -243,7 +243,7 @@ struct kvm_vcpu {
 	int fpu_active;
 	int guest_fpu_loaded, guest_xcr0_loaded;
 	unsigned char fpu_counter;
-	wait_queue_head_t wq;
+	struct swait_head wq;
 	struct pid *pid;
 	int sigset_active;
 	sigset_t sigset;
@@ -794,7 +794,7 @@ static inline bool kvm_arch_has_assigned_device(struct kvm *kvm)
 }
 #endif
 
-static inline wait_queue_head_t *kvm_arch_vcpu_wq(struct kvm_vcpu *vcpu)
+static inline struct swait_head *kvm_arch_vcpu_wq(struct kvm_vcpu *vcpu)
 {
 #ifdef __KVM_HAVE_ARCH_WQP
 	return vcpu->arch.wqp;
diff --git a/include/linux/lglock.h b/include/linux/lglock.h
index c92ebd100d9b..6f035f635d0e 100644
--- a/include/linux/lglock.h
+++ b/include/linux/lglock.h
@@ -34,13 +34,30 @@
 #endif
 
 struct lglock {
+#ifdef CONFIG_PREEMPT_RT_FULL
+	struct rt_mutex __percpu *lock;
+#else
 	arch_spinlock_t __percpu *lock;
+#endif
 #ifdef CONFIG_DEBUG_LOCK_ALLOC
 	struct lock_class_key lock_key;
 	struct lockdep_map    lock_dep_map;
 #endif
 };
 
+#ifdef CONFIG_PREEMPT_RT_FULL
+# define DEFINE_LGLOCK(name)						\
+	static DEFINE_PER_CPU(struct rt_mutex, name ## _lock)		\
+	= __RT_MUTEX_INITIALIZER( name ## _lock);			\
+	struct lglock name = { .lock = &name ## _lock }
+
+# define DEFINE_STATIC_LGLOCK(name)					\
+	static DEFINE_PER_CPU(struct rt_mutex, name ## _lock)		\
+	= __RT_MUTEX_INITIALIZER( name ## _lock);			\
+	static struct lglock name = { .lock = &name ## _lock }
+
+#else
+
 #define DEFINE_LGLOCK(name)						\
 	static DEFINE_PER_CPU(arch_spinlock_t, name ## _lock)		\
 	= __ARCH_SPIN_LOCK_UNLOCKED;					\
@@ -50,6 +67,7 @@ struct lglock {
 	static DEFINE_PER_CPU(arch_spinlock_t, name ## _lock)		\
 	= __ARCH_SPIN_LOCK_UNLOCKED;					\
 	static struct lglock name = { .lock = &name ## _lock }
+#endif
 
 void lg_lock_init(struct lglock *lg, char *name);
 
@@ -64,6 +82,12 @@ void lg_double_unlock(struct lglock *lg, int cpu1, int cpu2);
 void lg_global_lock(struct lglock *lg);
 void lg_global_unlock(struct lglock *lg);
 
+#ifndef CONFIG_PREEMPT_RT_FULL
+#define lg_global_trylock_relax(name)	lg_global_lock(name)
+#else
+void lg_global_trylock_relax(struct lglock *lg);
+#endif
+
 #else
 /* When !CONFIG_SMP, map lglock to spinlock */
 #define lglock spinlock
diff --git a/include/linux/list_bl.h b/include/linux/list_bl.h
index 8132214e8efd..44f0b5560473 100644
--- a/include/linux/list_bl.h
+++ b/include/linux/list_bl.h
@@ -2,6 +2,7 @@
 #define _LINUX_LIST_BL_H
 
 #include <linux/list.h>
+#include <linux/spinlock.h>
 #include <linux/bit_spinlock.h>
 
 /*
@@ -32,13 +33,22 @@
 
 struct hlist_bl_head {
 	struct hlist_bl_node *first;
+#ifdef CONFIG_PREEMPT_RT_BASE
+	raw_spinlock_t lock;
+#endif
 };
 
 struct hlist_bl_node {
 	struct hlist_bl_node *next, **pprev;
 };
-#define INIT_HLIST_BL_HEAD(ptr) \
-	((ptr)->first = NULL)
+
+static inline void INIT_HLIST_BL_HEAD(struct hlist_bl_head *h)
+{
+	h->first = NULL;
+#ifdef CONFIG_PREEMPT_RT_BASE
+	raw_spin_lock_init(&h->lock);
+#endif
+}
 
 static inline void INIT_HLIST_BL_NODE(struct hlist_bl_node *h)
 {
@@ -118,12 +128,26 @@ static inline void hlist_bl_del_init(struct hlist_bl_node *n)
 
 static inline void hlist_bl_lock(struct hlist_bl_head *b)
 {
+#ifndef CONFIG_PREEMPT_RT_BASE
 	bit_spin_lock(0, (unsigned long *)b);
+#else
+	raw_spin_lock(&b->lock);
+#if defined(CONFIG_SMP) || defined(CONFIG_DEBUG_SPINLOCK)
+	__set_bit(0, (unsigned long *)b);
+#endif
+#endif
 }
 
 static inline void hlist_bl_unlock(struct hlist_bl_head *b)
 {
+#ifndef CONFIG_PREEMPT_RT_BASE
 	__bit_spin_unlock(0, (unsigned long *)b);
+#else
+#if defined(CONFIG_SMP) || defined(CONFIG_DEBUG_SPINLOCK)
+	__clear_bit(0, (unsigned long *)b);
+#endif
+	raw_spin_unlock(&b->lock);
+#endif
 }
 
 static inline bool hlist_bl_is_locked(struct hlist_bl_head *b)
diff --git a/include/linux/locallock.h b/include/linux/locallock.h
new file mode 100644
index 000000000000..339ba00adb9a
--- /dev/null
+++ b/include/linux/locallock.h
@@ -0,0 +1,270 @@
+#ifndef _LINUX_LOCALLOCK_H
+#define _LINUX_LOCALLOCK_H
+
+#include <linux/percpu.h>
+#include <linux/spinlock.h>
+
+#ifdef CONFIG_PREEMPT_RT_BASE
+
+#ifdef CONFIG_DEBUG_SPINLOCK
+# define LL_WARN(cond)	WARN_ON(cond)
+#else
+# define LL_WARN(cond)	do { } while (0)
+#endif
+
+/*
+ * per cpu lock based substitute for local_irq_*()
+ */
+struct local_irq_lock {
+	spinlock_t		lock;
+	struct task_struct	*owner;
+	int			nestcnt;
+	unsigned long		flags;
+};
+
+#define DEFINE_LOCAL_IRQ_LOCK(lvar)					\
+	DEFINE_PER_CPU(struct local_irq_lock, lvar) = {			\
+		.lock = __SPIN_LOCK_UNLOCKED((lvar).lock) }
+
+#define DECLARE_LOCAL_IRQ_LOCK(lvar)					\
+	DECLARE_PER_CPU(struct local_irq_lock, lvar)
+
+#define local_irq_lock_init(lvar)					\
+	do {								\
+		int __cpu;						\
+		for_each_possible_cpu(__cpu)				\
+			spin_lock_init(&per_cpu(lvar, __cpu).lock);	\
+	} while (0)
+
+/*
+ * spin_lock|trylock|unlock_local flavour that does not migrate disable
+ * used for __local_lock|trylock|unlock where get_local_var/put_local_var
+ * already takes care of the migrate_disable/enable
+ * for CONFIG_PREEMPT_BASE map to the normal spin_* calls.
+ */
+#ifdef CONFIG_PREEMPT_RT_FULL
+# define spin_lock_local(lock)			rt_spin_lock(lock)
+# define spin_trylock_local(lock)		rt_spin_trylock(lock)
+# define spin_unlock_local(lock)		rt_spin_unlock(lock)
+#else
+# define spin_lock_local(lock)			spin_lock(lock)
+# define spin_trylock_local(lock)		spin_trylock(lock)
+# define spin_unlock_local(lock)		spin_unlock(lock)
+#endif
+
+static inline void __local_lock(struct local_irq_lock *lv)
+{
+	if (lv->owner != current) {
+		spin_lock_local(&lv->lock);
+		LL_WARN(lv->owner);
+		LL_WARN(lv->nestcnt);
+		lv->owner = current;
+	}
+	lv->nestcnt++;
+}
+
+#define local_lock(lvar)					\
+	do { __local_lock(&get_local_var(lvar)); } while (0)
+
+static inline int __local_trylock(struct local_irq_lock *lv)
+{
+	if (lv->owner != current && spin_trylock_local(&lv->lock)) {
+		LL_WARN(lv->owner);
+		LL_WARN(lv->nestcnt);
+		lv->owner = current;
+		lv->nestcnt = 1;
+		return 1;
+	}
+	return 0;
+}
+
+#define local_trylock(lvar)						\
+	({								\
+		int __locked;						\
+		__locked = __local_trylock(&get_local_var(lvar));	\
+		if (!__locked)						\
+			put_local_var(lvar);				\
+		__locked;						\
+	})
+
+static inline void __local_unlock(struct local_irq_lock *lv)
+{
+	LL_WARN(lv->nestcnt == 0);
+	LL_WARN(lv->owner != current);
+	if (--lv->nestcnt)
+		return;
+
+	lv->owner = NULL;
+	spin_unlock_local(&lv->lock);
+}
+
+#define local_unlock(lvar)					\
+	do {							\
+		__local_unlock(this_cpu_ptr(&lvar));		\
+		put_local_var(lvar);				\
+	} while (0)
+
+static inline void __local_lock_irq(struct local_irq_lock *lv)
+{
+	spin_lock_irqsave(&lv->lock, lv->flags);
+	LL_WARN(lv->owner);
+	LL_WARN(lv->nestcnt);
+	lv->owner = current;
+	lv->nestcnt = 1;
+}
+
+#define local_lock_irq(lvar)						\
+	do { __local_lock_irq(&get_local_var(lvar)); } while (0)
+
+#define local_lock_irq_on(lvar, cpu)					\
+	do { __local_lock_irq(&per_cpu(lvar, cpu)); } while (0)
+
+static inline void __local_unlock_irq(struct local_irq_lock *lv)
+{
+	LL_WARN(!lv->nestcnt);
+	LL_WARN(lv->owner != current);
+	lv->owner = NULL;
+	lv->nestcnt = 0;
+	spin_unlock_irq(&lv->lock);
+}
+
+#define local_unlock_irq(lvar)						\
+	do {								\
+		__local_unlock_irq(this_cpu_ptr(&lvar));		\
+		put_local_var(lvar);					\
+	} while (0)
+
+#define local_unlock_irq_on(lvar, cpu)					\
+	do {								\
+		__local_unlock_irq(&per_cpu(lvar, cpu));		\
+	} while (0)
+
+static inline int __local_lock_irqsave(struct local_irq_lock *lv)
+{
+	if (lv->owner != current) {
+		__local_lock_irq(lv);
+		return 0;
+	} else {
+		lv->nestcnt++;
+		return 1;
+	}
+}
+
+#define local_lock_irqsave(lvar, _flags)				\
+	do {								\
+		if (__local_lock_irqsave(&get_local_var(lvar)))		\
+			put_local_var(lvar);				\
+		_flags = __this_cpu_read(lvar.flags);			\
+	} while (0)
+
+#define local_lock_irqsave_on(lvar, _flags, cpu)			\
+	do {								\
+		__local_lock_irqsave(&per_cpu(lvar, cpu));		\
+		_flags = per_cpu(lvar, cpu).flags;			\
+	} while (0)
+
+static inline int __local_unlock_irqrestore(struct local_irq_lock *lv,
+					    unsigned long flags)
+{
+	LL_WARN(!lv->nestcnt);
+	LL_WARN(lv->owner != current);
+	if (--lv->nestcnt)
+		return 0;
+
+	lv->owner = NULL;
+	spin_unlock_irqrestore(&lv->lock, lv->flags);
+	return 1;
+}
+
+#define local_unlock_irqrestore(lvar, flags)				\
+	do {								\
+		if (__local_unlock_irqrestore(this_cpu_ptr(&lvar), flags)) \
+			put_local_var(lvar);				\
+	} while (0)
+
+#define local_unlock_irqrestore_on(lvar, flags, cpu)			\
+	do {								\
+		__local_unlock_irqrestore(&per_cpu(lvar, cpu), flags);	\
+	} while (0)
+
+#define local_spin_trylock_irq(lvar, lock)				\
+	({								\
+		int __locked;						\
+		local_lock_irq(lvar);					\
+		__locked = spin_trylock(lock);				\
+		if (!__locked)						\
+			local_unlock_irq(lvar);				\
+		__locked;						\
+	})
+
+#define local_spin_lock_irq(lvar, lock)					\
+	do {								\
+		local_lock_irq(lvar);					\
+		spin_lock(lock);					\
+	} while (0)
+
+#define local_spin_unlock_irq(lvar, lock)				\
+	do {								\
+		spin_unlock(lock);					\
+		local_unlock_irq(lvar);					\
+	} while (0)
+
+#define local_spin_lock_irqsave(lvar, lock, flags)			\
+	do {								\
+		local_lock_irqsave(lvar, flags);			\
+		spin_lock(lock);					\
+	} while (0)
+
+#define local_spin_unlock_irqrestore(lvar, lock, flags)			\
+	do {								\
+		spin_unlock(lock);					\
+		local_unlock_irqrestore(lvar, flags);			\
+	} while (0)
+
+#define get_locked_var(lvar, var)					\
+	(*({								\
+		local_lock(lvar);					\
+		this_cpu_ptr(&var);					\
+	}))
+
+#define put_locked_var(lvar, var)	local_unlock(lvar);
+
+#define local_lock_cpu(lvar)						\
+	({								\
+		local_lock(lvar);					\
+		smp_processor_id();					\
+	})
+
+#define local_unlock_cpu(lvar)			local_unlock(lvar)
+
+#else /* PREEMPT_RT_BASE */
+
+#define DEFINE_LOCAL_IRQ_LOCK(lvar)		__typeof__(const int) lvar
+#define DECLARE_LOCAL_IRQ_LOCK(lvar)		extern __typeof__(const int) lvar
+
+static inline void local_irq_lock_init(int lvar) { }
+
+#define local_lock(lvar)			preempt_disable()
+#define local_unlock(lvar)			preempt_enable()
+#define local_lock_irq(lvar)			local_irq_disable()
+#define local_unlock_irq(lvar)			local_irq_enable()
+#define local_lock_irqsave(lvar, flags)		local_irq_save(flags)
+#define local_unlock_irqrestore(lvar, flags)	local_irq_restore(flags)
+
+#define local_spin_trylock_irq(lvar, lock)	spin_trylock_irq(lock)
+#define local_spin_lock_irq(lvar, lock)		spin_lock_irq(lock)
+#define local_spin_unlock_irq(lvar, lock)	spin_unlock_irq(lock)
+#define local_spin_lock_irqsave(lvar, lock, flags)	\
+	spin_lock_irqsave(lock, flags)
+#define local_spin_unlock_irqrestore(lvar, lock, flags)	\
+	spin_unlock_irqrestore(lock, flags)
+
+#define get_locked_var(lvar, var)		get_cpu_var(var)
+#define put_locked_var(lvar, var)		put_cpu_var(var)
+
+#define local_lock_cpu(lvar)			get_cpu()
+#define local_unlock_cpu(lvar)			put_cpu()
+
+#endif
+
+#endif
diff --git a/include/linux/mm_types.h b/include/linux/mm_types.h
index f8d1492a114f..b238ebfbb4d6 100644
--- a/include/linux/mm_types.h
+++ b/include/linux/mm_types.h
@@ -11,6 +11,7 @@
 #include <linux/completion.h>
 #include <linux/cpumask.h>
 #include <linux/uprobes.h>
+#include <linux/rcupdate.h>
 #include <linux/page-flags-layout.h>
 #include <asm/page.h>
 #include <asm/mmu.h>
@@ -504,6 +505,9 @@ struct mm_struct {
 	bool tlb_flush_pending;
 #endif
 	struct uprobes_state uprobes_state;
+#ifdef CONFIG_PREEMPT_RT_BASE
+	struct rcu_head delayed_drop;
+#endif
 #ifdef CONFIG_X86_INTEL_MPX
 	/* address of the bounds directory */
 	void __user *bd_addr;
diff --git a/include/linux/mutex.h b/include/linux/mutex.h
index 2cb7531e7d7a..b3fdfc820216 100644
--- a/include/linux/mutex.h
+++ b/include/linux/mutex.h
@@ -19,6 +19,17 @@
 #include <asm/processor.h>
 #include <linux/osq_lock.h>
 
+#ifdef CONFIG_DEBUG_LOCK_ALLOC
+# define __DEP_MAP_MUTEX_INITIALIZER(lockname) \
+	, .dep_map = { .name = #lockname }
+#else
+# define __DEP_MAP_MUTEX_INITIALIZER(lockname)
+#endif
+
+#ifdef CONFIG_PREEMPT_RT_FULL
+# include <linux/mutex_rt.h>
+#else
+
 /*
  * Simple, straightforward mutexes with strict semantics:
  *
@@ -99,13 +110,6 @@ do {							\
 static inline void mutex_destroy(struct mutex *lock) {}
 #endif
 
-#ifdef CONFIG_DEBUG_LOCK_ALLOC
-# define __DEP_MAP_MUTEX_INITIALIZER(lockname) \
-		, .dep_map = { .name = #lockname }
-#else
-# define __DEP_MAP_MUTEX_INITIALIZER(lockname)
-#endif
-
 #define __MUTEX_INITIALIZER(lockname) \
 		{ .count = ATOMIC_INIT(1) \
 		, .wait_lock = __SPIN_LOCK_UNLOCKED(lockname.wait_lock) \
@@ -173,6 +177,8 @@ extern int __must_check mutex_lock_killable(struct mutex *lock);
 extern int mutex_trylock(struct mutex *lock);
 extern void mutex_unlock(struct mutex *lock);
 
+#endif /* !PREEMPT_RT_FULL */
+
 extern int atomic_dec_and_mutex_lock(atomic_t *cnt, struct mutex *lock);
 
 #endif /* __LINUX_MUTEX_H */
diff --git a/include/linux/mutex_rt.h b/include/linux/mutex_rt.h
new file mode 100644
index 000000000000..c38a44b14da5
--- /dev/null
+++ b/include/linux/mutex_rt.h
@@ -0,0 +1,84 @@
+#ifndef __LINUX_MUTEX_RT_H
+#define __LINUX_MUTEX_RT_H
+
+#ifndef __LINUX_MUTEX_H
+#error "Please include mutex.h"
+#endif
+
+#include <linux/rtmutex.h>
+
+/* FIXME: Just for __lockfunc */
+#include <linux/spinlock.h>
+
+struct mutex {
+	struct rt_mutex		lock;
+#ifdef CONFIG_DEBUG_LOCK_ALLOC
+	struct lockdep_map	dep_map;
+#endif
+};
+
+#define __MUTEX_INITIALIZER(mutexname)					\
+	{								\
+		.lock = __RT_MUTEX_INITIALIZER(mutexname.lock)		\
+		__DEP_MAP_MUTEX_INITIALIZER(mutexname)			\
+	}
+
+#define DEFINE_MUTEX(mutexname)						\
+	struct mutex mutexname = __MUTEX_INITIALIZER(mutexname)
+
+extern void __mutex_do_init(struct mutex *lock, const char *name, struct lock_class_key *key);
+extern void __lockfunc _mutex_lock(struct mutex *lock);
+extern int __lockfunc _mutex_lock_interruptible(struct mutex *lock);
+extern int __lockfunc _mutex_lock_killable(struct mutex *lock);
+extern void __lockfunc _mutex_lock_nested(struct mutex *lock, int subclass);
+extern void __lockfunc _mutex_lock_nest_lock(struct mutex *lock, struct lockdep_map *nest_lock);
+extern int __lockfunc _mutex_lock_interruptible_nested(struct mutex *lock, int subclass);
+extern int __lockfunc _mutex_lock_killable_nested(struct mutex *lock, int subclass);
+extern int __lockfunc _mutex_trylock(struct mutex *lock);
+extern void __lockfunc _mutex_unlock(struct mutex *lock);
+
+#define mutex_is_locked(l)		rt_mutex_is_locked(&(l)->lock)
+#define mutex_lock(l)			_mutex_lock(l)
+#define mutex_lock_interruptible(l)	_mutex_lock_interruptible(l)
+#define mutex_lock_killable(l)		_mutex_lock_killable(l)
+#define mutex_trylock(l)		_mutex_trylock(l)
+#define mutex_unlock(l)			_mutex_unlock(l)
+#define mutex_destroy(l)		rt_mutex_destroy(&(l)->lock)
+
+#ifdef CONFIG_DEBUG_LOCK_ALLOC
+# define mutex_lock_nested(l, s)	_mutex_lock_nested(l, s)
+# define mutex_lock_interruptible_nested(l, s) \
+					_mutex_lock_interruptible_nested(l, s)
+# define mutex_lock_killable_nested(l, s) \
+					_mutex_lock_killable_nested(l, s)
+
+# define mutex_lock_nest_lock(lock, nest_lock)				\
+do {									\
+	typecheck(struct lockdep_map *, &(nest_lock)->dep_map);		\
+	_mutex_lock_nest_lock(lock, &(nest_lock)->dep_map);		\
+} while (0)
+
+#else
+# define mutex_lock_nested(l, s)	_mutex_lock(l)
+# define mutex_lock_interruptible_nested(l, s) \
+					_mutex_lock_interruptible(l)
+# define mutex_lock_killable_nested(l, s) \
+					_mutex_lock_killable(l)
+# define mutex_lock_nest_lock(lock, nest_lock) mutex_lock(lock)
+#endif
+
+# define mutex_init(mutex)				\
+do {							\
+	static struct lock_class_key __key;		\
+							\
+	rt_mutex_init(&(mutex)->lock);			\
+	__mutex_do_init((mutex), #mutex, &__key);	\
+} while (0)
+
+# define __mutex_init(mutex, name, key)			\
+do {							\
+	rt_mutex_init(&(mutex)->lock);			\
+	__mutex_do_init((mutex), name, key);		\
+} while (0)
+
+#endif
diff --git a/include/linux/netdevice.h b/include/linux/netdevice.h
index 3143c847bddb..c0e12f7f0f13 100644
--- a/include/linux/netdevice.h
+++ b/include/linux/netdevice.h
@@ -2249,11 +2249,20 @@ void netdev_freemem(struct net_device *dev);
 void synchronize_net(void);
 int init_dummy_netdev(struct net_device *dev);
 
+#ifdef CONFIG_PREEMPT_RT_FULL
+static inline int dev_recursion_level(void)
+{
+	return current->xmit_recursion;
+}
+
+#else
+
 DECLARE_PER_CPU(int, xmit_recursion);
 static inline int dev_recursion_level(void)
 {
 	return this_cpu_read(xmit_recursion);
 }
+#endif
 
 struct net_device *dev_get_by_index(struct net *net, int ifindex);
 struct net_device *__dev_get_by_index(struct net *net, int ifindex);
@@ -2546,6 +2555,7 @@ struct softnet_data {
 	unsigned int		dropped;
 	struct sk_buff_head	input_pkt_queue;
 	struct napi_struct	backlog;
+	struct sk_buff_head	tofree_queue;
 
 };
 
diff --git a/include/linux/netfilter/x_tables.h b/include/linux/netfilter/x_tables.h
index c5577410c25d..8d8b410f2496 100644
--- a/include/linux/netfilter/x_tables.h
+++ b/include/linux/netfilter/x_tables.h
@@ -4,6 +4,7 @@
 
 #include <linux/netdevice.h>
 #include <linux/static_key.h>
+#include <linux/locallock.h>
 #include <uapi/linux/netfilter/x_tables.h>
 
 /**
@@ -282,6 +283,8 @@ void xt_free_table_info(struct xt_table_info *info);
  */
 DECLARE_PER_CPU(seqcount_t, xt_recseq);
 
+DECLARE_LOCAL_IRQ_LOCK(xt_write_lock);
+
 /* xt_tee_enabled - true if x_tables needs to handle reentrancy
  *
  * Enabled if current ip(6)tables ruleset has at least one -j TEE rule.
@@ -302,6 +305,9 @@ static inline unsigned int xt_write_recseq_begin(void)
 {
 	unsigned int addend;
 
+	/* RT protection */
+	local_lock(xt_write_lock);
+
 	/*
 	 * Low order bit of sequence is set if we already
 	 * called xt_write_recseq_begin().
@@ -332,6 +338,7 @@ static inline void xt_write_recseq_end(unsigned int addend)
 	/* this is kind of a write_seqcount_end(), but addend is 0 or 1 */
 	smp_wmb();
 	__this_cpu_add(xt_recseq.sequence, addend);
+	local_unlock(xt_write_lock);
 }
 
 /*
diff --git a/include/linux/notifier.h b/include/linux/notifier.h
index d14a4c362465..2e4414a0c1c4 100644
--- a/include/linux/notifier.h
+++ b/include/linux/notifier.h
@@ -6,7 +6,7 @@
  *
  *				Alan Cox <Alan.Cox@linux.org>
  */
- 
+
 #ifndef _LINUX_NOTIFIER_H
 #define _LINUX_NOTIFIER_H
 #include <linux/errno.h>
@@ -42,9 +42,7 @@
  * in srcu_notifier_call_chain(): no cache bounces and no memory barriers.
  * As compensation, srcu_notifier_chain_unregister() is rather expensive.
  * SRCU notifier chains should be used when the chain will be called very
- * often but notifier_blocks will seldom be removed.  Also, SRCU notifier
- * chains are slightly more difficult to use because they require special
- * runtime initialization.
+ * often but notifier_blocks will seldom be removed.
  */
 
 typedef	int (*notifier_fn_t)(struct notifier_block *nb,
@@ -88,7 +86,7 @@ struct srcu_notifier_head {
 		(name)->head = NULL;		\
 	} while (0)
 
-/* srcu_notifier_heads must be initialized and cleaned up dynamically */
+/* srcu_notifier_heads must be cleaned up dynamically */
 extern void srcu_init_notifier_head(struct srcu_notifier_head *nh);
 #define srcu_cleanup_notifier_head(name)	\
 		cleanup_srcu_struct(&(name)->srcu);
@@ -101,7 +99,13 @@ extern void srcu_init_notifier_head(struct srcu_notifier_head *nh);
 		.head = NULL }
 #define RAW_NOTIFIER_INIT(name)	{				\
 		.head = NULL }
-/* srcu_notifier_heads cannot be initialized statically */
+
+#define SRCU_NOTIFIER_INIT(name, pcpu)				\
+	{							\
+		.mutex = __MUTEX_INITIALIZER(name.mutex),	\
+		.head = NULL,					\
+		.srcu = __SRCU_STRUCT_INIT(name.srcu, pcpu),	\
+	}
 
 #define ATOMIC_NOTIFIER_HEAD(name)				\
 	struct atomic_notifier_head name =			\
@@ -113,6 +117,18 @@ extern void srcu_init_notifier_head(struct srcu_notifier_head *nh);
 	struct raw_notifier_head name =				\
 		RAW_NOTIFIER_INIT(name)
 
+#define _SRCU_NOTIFIER_HEAD(name, mod)				\
+	static DEFINE_PER_CPU(struct srcu_struct_array,		\
+			name##_head_srcu_array);		\
+	mod struct srcu_notifier_head name =			\
+			SRCU_NOTIFIER_INIT(name, name##_head_srcu_array)
+
+#define SRCU_NOTIFIER_HEAD(name)				\
+	_SRCU_NOTIFIER_HEAD(name, )
+
+#define SRCU_NOTIFIER_HEAD_STATIC(name)				\
+	_SRCU_NOTIFIER_HEAD(name, static)
+
 #ifdef __KERNEL__
 
 extern int atomic_notifier_chain_register(struct atomic_notifier_head *nh,
@@ -182,12 +198,12 @@ static inline int notifier_to_errno(int ret)
 
 /*
  *	Declared notifiers so far. I can imagine quite a few more chains
- *	over time (eg laptop power reset chains, reboot chain (to clean 
+ *	over time (eg laptop power reset chains, reboot chain (to clean
  *	device units up), device [un]mount chain, module load/unload chain,
- *	low memory chain, screenblank chain (for plug in modular screenblankers) 
+ *	low memory chain, screenblank chain (for plug in modular screenblankers)
  *	VC switch chains (for loadable kernel svgalib VC switch helpers) etc...
  */
- 
+
 /* CPU notfiers are defined in include/linux/cpu.h. */
 
 /* netdevice notifiers are defined in include/linux/netdevice.h */
diff --git a/include/linux/percpu.h b/include/linux/percpu.h
index caebf2a758dc..53a60a51c758 100644
--- a/include/linux/percpu.h
+++ b/include/linux/percpu.h
@@ -24,6 +24,35 @@
 	 PERCPU_MODULE_RESERVE)
 #endif
 
+#ifdef CONFIG_PREEMPT_RT_FULL
+
+#define get_local_var(var) (*({		\
+	       migrate_disable();	\
+	       this_cpu_ptr(&var);	}))
+
+#define put_local_var(var) do {	\
+	(void)&(var);		\
+	migrate_enable();	\
+} while (0)
+
+# define get_local_ptr(var) ({		\
+		migrate_disable();	\
+		this_cpu_ptr(var);	})
+
+# define put_local_ptr(var) do {	\
+	(void)(var);			\
+	migrate_enable();		\
+} while (0)
+
+#else
+
+#define get_local_var(var)	get_cpu_var(var)
+#define put_local_var(var)	put_cpu_var(var)
+#define get_local_ptr(var)	get_cpu_ptr(var)
+#define put_local_ptr(var)	put_cpu_ptr(var)
+
+#endif
+
 /* minimum unit size, also is the maximum supported allocation size */
 #define PCPU_MIN_UNIT_SIZE		PFN_ALIGN(32 << 10)
 
diff --git a/include/linux/pid.h b/include/linux/pid.h
index 23705a53abba..2cc64b779f03 100644
--- a/include/linux/pid.h
+++ b/include/linux/pid.h
@@ -2,6 +2,7 @@
 #define _LINUX_PID_H
 
 #include <linux/rcupdate.h>
+#include <linux/atomic.h>
 
 enum pid_type
 {
diff --git a/include/linux/preempt.h b/include/linux/preempt.h
index 75e4e30677f1..1cfb1cb72354 100644
--- a/include/linux/preempt.h
+++ b/include/linux/preempt.h
@@ -50,7 +50,11 @@
 #define HARDIRQ_OFFSET	(1UL << HARDIRQ_SHIFT)
 #define NMI_OFFSET	(1UL << NMI_SHIFT)
 
-#define SOFTIRQ_DISABLE_OFFSET	(2 * SOFTIRQ_OFFSET)
+#ifndef CONFIG_PREEMPT_RT_FULL
+# define SOFTIRQ_DISABLE_OFFSET		(2 * SOFTIRQ_OFFSET)
+#else
+# define SOFTIRQ_DISABLE_OFFSET		(0)
+#endif
 
 /* We use the MSB mostly because its available */
 #define PREEMPT_NEED_RESCHED	0x80000000
@@ -59,9 +63,15 @@
 #include <asm/preempt.h>
 
 #define hardirq_count()	(preempt_count() & HARDIRQ_MASK)
-#define softirq_count()	(preempt_count() & SOFTIRQ_MASK)
 #define irq_count()	(preempt_count() & (HARDIRQ_MASK | SOFTIRQ_MASK \
 				 | NMI_MASK))
+#ifndef CONFIG_PREEMPT_RT_FULL
+# define softirq_count()	(preempt_count() & SOFTIRQ_MASK)
+# define in_serving_softirq()	(softirq_count() & SOFTIRQ_OFFSET)
+#else
+# define softirq_count()	(0UL)
+extern int in_serving_softirq(void);
+#endif
 
 /*
  * Are we doing bottom half or hardware interrupt processing?
@@ -72,7 +82,6 @@
 #define in_irq()		(hardirq_count())
 #define in_softirq()		(softirq_count())
 #define in_interrupt()		(irq_count())
-#define in_serving_softirq()	(softirq_count() & SOFTIRQ_OFFSET)
 
 /*
  * Are we in NMI context?
@@ -91,7 +100,11 @@
 /*
  * The preempt_count offset after spin_lock()
  */
+#if !defined(CONFIG_PREEMPT_RT_FULL)
 #define PREEMPT_LOCK_OFFSET	PREEMPT_DISABLE_OFFSET
+#else
+#define PREEMPT_LOCK_OFFSET	0
+#endif
 
 /*
  * The preempt_count offset needed for things like:
@@ -140,6 +153,20 @@ extern void preempt_count_sub(int val);
 #define preempt_count_inc() preempt_count_add(1)
 #define preempt_count_dec() preempt_count_sub(1)
 
+#ifdef CONFIG_PREEMPT_LAZY
+#define add_preempt_lazy_count(val)	do { preempt_lazy_count() += (val); } while (0)
+#define sub_preempt_lazy_count(val)	do { preempt_lazy_count() -= (val); } while (0)
+#define inc_preempt_lazy_count()	add_preempt_lazy_count(1)
+#define dec_preempt_lazy_count()	sub_preempt_lazy_count(1)
+#define preempt_lazy_count()		(current_thread_info()->preempt_lazy_count)
+#else
+#define add_preempt_lazy_count(val)	do { } while (0)
+#define sub_preempt_lazy_count(val)	do { } while (0)
+#define inc_preempt_lazy_count()	do { } while (0)
+#define dec_preempt_lazy_count()	do { } while (0)
+#define preempt_lazy_count()		(0)
+#endif
+
 #ifdef CONFIG_PREEMPT_COUNT
 
 #define preempt_disable() \
@@ -148,13 +175,25 @@ do { \
 	barrier(); \
 } while (0)
 
+#define preempt_lazy_disable() \
+do { \
+	inc_preempt_lazy_count(); \
+	barrier(); \
+} while (0)
+
 #define sched_preempt_enable_no_resched() \
 do { \
 	barrier(); \
 	preempt_count_dec(); \
 } while (0)
 
-#define preempt_enable_no_resched() sched_preempt_enable_no_resched()
+#ifdef CONFIG_PREEMPT_RT_BASE
+# define preempt_enable_no_resched() sched_preempt_enable_no_resched()
+# define preempt_check_resched_rt() preempt_check_resched()
+#else
+# define preempt_enable_no_resched() preempt_enable()
+# define preempt_check_resched_rt() barrier();
+#endif
 
 #define preemptible()	(preempt_count() == 0 && !irqs_disabled())
 
@@ -179,6 +218,13 @@ do { \
 		__preempt_schedule(); \
 } while (0)
 
+#define preempt_lazy_enable() \
+do { \
+	dec_preempt_lazy_count(); \
+	barrier(); \
+	preempt_check_resched(); \
+} while (0)
+
 #else /* !CONFIG_PREEMPT */
 #define preempt_enable() \
 do { \
@@ -224,6 +270,7 @@ do { \
 #define preempt_disable_notrace()		barrier()
 #define preempt_enable_no_resched_notrace()	barrier()
 #define preempt_enable_notrace()		barrier()
+#define preempt_check_resched_rt()		barrier()
 #define preemptible()				0
 
 #endif /* CONFIG_PREEMPT_COUNT */
@@ -244,10 +291,31 @@ do { \
 } while (0)
 #define preempt_fold_need_resched() \
 do { \
-	if (tif_need_resched()) \
+	if (tif_need_resched_now()) \
 		set_preempt_need_resched(); \
 } while (0)
 
+#ifdef CONFIG_PREEMPT_RT_FULL
+# define preempt_disable_rt()		preempt_disable()
+# define preempt_enable_rt()		preempt_enable()
+# define preempt_disable_nort()		barrier()
+# define preempt_enable_nort()		barrier()
+# ifdef CONFIG_SMP
+   extern void migrate_disable(void);
+   extern void migrate_enable(void);
+# else /* CONFIG_SMP */
+#  define migrate_disable()		barrier()
+#  define migrate_enable()		barrier()
+# endif /* CONFIG_SMP */
+#else
+# define preempt_disable_rt()		barrier()
+# define preempt_enable_rt()		barrier()
+# define preempt_disable_nort()		preempt_disable()
+# define preempt_enable_nort()		preempt_enable()
+# define migrate_disable()		preempt_disable()
+# define migrate_enable()		preempt_enable()
+#endif
+
 #ifdef CONFIG_PREEMPT_NOTIFIERS
 
 struct preempt_notifier;
diff --git a/include/linux/printk.h b/include/linux/printk.h
index 9729565c25ff..9cdca696b718 100644
--- a/include/linux/printk.h
+++ b/include/linux/printk.h
@@ -117,9 +117,11 @@ int no_printk(const char *fmt, ...)
 #ifdef CONFIG_EARLY_PRINTK
 extern asmlinkage __printf(1, 2)
 void early_printk(const char *fmt, ...);
+extern void printk_kill(void);
 #else
 static inline __printf(1, 2) __cold
 void early_printk(const char *s, ...) { }
+static inline void printk_kill(void) { }
 #endif
 
 typedef __printf(1, 0) int (*printk_func_t)(const char *fmt, va_list args);
diff --git a/include/linux/radix-tree.h b/include/linux/radix-tree.h
index 33170dbd9db4..933ff6977c96 100644
--- a/include/linux/radix-tree.h
+++ b/include/linux/radix-tree.h
@@ -277,8 +277,13 @@ radix_tree_gang_lookup(struct radix_tree_root *root, void **results,
 unsigned int radix_tree_gang_lookup_slot(struct radix_tree_root *root,
 			void ***results, unsigned long *indices,
 			unsigned long first_index, unsigned int max_items);
+#ifndef CONFIG_PREEMPT_RT_FULL
 int radix_tree_preload(gfp_t gfp_mask);
 int radix_tree_maybe_preload(gfp_t gfp_mask);
+#else
+static inline int radix_tree_preload(gfp_t gm) { return 0; }
+static inline int radix_tree_maybe_preload(gfp_t gfp_mask) { return 0; }
+#endif
 void radix_tree_init(void);
 void *radix_tree_tag_set(struct radix_tree_root *root,
 			unsigned long index, unsigned int tag);
@@ -303,7 +308,7 @@ unsigned long radix_tree_locate_item(struct radix_tree_root *root, void *item);
 
 static inline void radix_tree_preload_end(void)
 {
-	preempt_enable();
+	preempt_enable_nort();
 }
 
 /**
diff --git a/include/linux/random.h b/include/linux/random.h
index a75840c1aa71..1a804361670c 100644
--- a/include/linux/random.h
+++ b/include/linux/random.h
@@ -20,7 +20,7 @@ struct random_ready_callback {
 extern void add_device_randomness(const void *, unsigned int);
 extern void add_input_randomness(unsigned int type, unsigned int code,
 				 unsigned int value);
-extern void add_interrupt_randomness(int irq, int irq_flags);
+extern void add_interrupt_randomness(int irq, int irq_flags, __u64 ip);
 
 extern void get_random_bytes(void *buf, int nbytes);
 extern int add_random_ready_callback(struct random_ready_callback *rdy);
diff --git a/include/linux/rbtree.h b/include/linux/rbtree.h
index a5aa7ae671f4..24ddffd25492 100644
--- a/include/linux/rbtree.h
+++ b/include/linux/rbtree.h
@@ -31,7 +31,6 @@
 
 #include <linux/kernel.h>
 #include <linux/stddef.h>
-#include <linux/rcupdate.h>
 
 struct rb_node {
 	unsigned long  __rb_parent_color;
@@ -86,14 +85,8 @@ static inline void rb_link_node(struct rb_node *node, struct rb_node *parent,
 	*rb_link = node;
 }
 
-static inline void rb_link_node_rcu(struct rb_node *node, struct rb_node *parent,
-				    struct rb_node **rb_link)
-{
-	node->__rb_parent_color = (unsigned long)parent;
-	node->rb_left = node->rb_right = NULL;
-
-	rcu_assign_pointer(*rb_link, node);
-}
+void rb_link_node_rcu(struct rb_node *node, struct rb_node *parent,
+		      struct rb_node **rb_link);
 
 #define rb_entry_safe(ptr, type, member) \
 	({ typeof(ptr) ____ptr = (ptr); \
diff --git a/include/linux/rcupdate.h b/include/linux/rcupdate.h
index a0189ba67fde..c2f5f955163d 100644
--- a/include/linux/rcupdate.h
+++ b/include/linux/rcupdate.h
@@ -169,6 +169,9 @@ void call_rcu(struct rcu_head *head,
 
 #endif /* #else #ifdef CONFIG_PREEMPT_RCU */
 
+#ifdef CONFIG_PREEMPT_RT_FULL
+#define call_rcu_bh	call_rcu
+#else
 /**
  * call_rcu_bh() - Queue an RCU for invocation after a quicker grace period.
  * @head: structure to be used for queueing the RCU updates.
@@ -192,6 +195,7 @@ void call_rcu(struct rcu_head *head,
  */
 void call_rcu_bh(struct rcu_head *head,
 		 rcu_callback_t func);
+#endif
 
 /**
  * call_rcu_sched() - Queue an RCU for invocation after sched grace period.
@@ -292,6 +296,11 @@ void synchronize_rcu(void);
  * types of kernel builds, the rcu_read_lock() nesting depth is unknowable.
  */
 #define rcu_preempt_depth() (current->rcu_read_lock_nesting)
+#ifndef CONFIG_PREEMPT_RT_FULL
+#define sched_rcu_preempt_depth()	rcu_preempt_depth()
+#else
+static inline int sched_rcu_preempt_depth(void) { return 0; }
+#endif
 
 #else /* #ifdef CONFIG_PREEMPT_RCU */
 
@@ -317,6 +326,8 @@ static inline int rcu_preempt_depth(void)
 	return 0;
 }
 
+#define sched_rcu_preempt_depth()	rcu_preempt_depth()
+
 #endif /* #else #ifdef CONFIG_PREEMPT_RCU */
 
 /* Internal to kernel */
@@ -489,7 +500,14 @@ extern struct lockdep_map rcu_callback_map;
 int debug_lockdep_rcu_enabled(void);
 
 int rcu_read_lock_held(void);
+#ifdef CONFIG_PREEMPT_RT_FULL
+static inline int rcu_read_lock_bh_held(void)
+{
+	return rcu_read_lock_held();
+}
+#else
 int rcu_read_lock_bh_held(void);
+#endif
 
 /**
  * rcu_read_lock_sched_held() - might we be in RCU-sched read-side critical section?
@@ -937,10 +955,14 @@ static inline void rcu_read_unlock(void)
 static inline void rcu_read_lock_bh(void)
 {
 	local_bh_disable();
+#ifdef CONFIG_PREEMPT_RT_FULL
+	rcu_read_lock();
+#else
 	__acquire(RCU_BH);
 	rcu_lock_acquire(&rcu_bh_lock_map);
 	RCU_LOCKDEP_WARN(!rcu_is_watching(),
 			 "rcu_read_lock_bh() used illegally while idle");
+#endif
 }
 
 /*
@@ -950,10 +972,14 @@ static inline void rcu_read_lock_bh(void)
  */
 static inline void rcu_read_unlock_bh(void)
 {
+#ifdef CONFIG_PREEMPT_RT_FULL
+	rcu_read_unlock();
+#else
 	RCU_LOCKDEP_WARN(!rcu_is_watching(),
 			 "rcu_read_unlock_bh() used illegally while idle");
 	rcu_lock_release(&rcu_bh_lock_map);
 	__release(RCU_BH);
+#endif
 	local_bh_enable();
 }
 
diff --git a/include/linux/rcutree.h b/include/linux/rcutree.h
index 60d15a080d7c..436c9e62bfc6 100644
--- a/include/linux/rcutree.h
+++ b/include/linux/rcutree.h
@@ -44,7 +44,11 @@ static inline void rcu_virt_note_context_switch(int cpu)
 	rcu_note_context_switch();
 }
 
+#ifdef CONFIG_PREEMPT_RT_FULL
+# define synchronize_rcu_bh	synchronize_rcu
+#else
 void synchronize_rcu_bh(void);
+#endif
 void synchronize_sched_expedited(void);
 void synchronize_rcu_expedited(void);
 
@@ -72,7 +76,11 @@ static inline void synchronize_rcu_bh_expedited(void)
 }
 
 void rcu_barrier(void);
+#ifdef CONFIG_PREEMPT_RT_FULL
+# define rcu_barrier_bh                rcu_barrier
+#else
 void rcu_barrier_bh(void);
+#endif
 void rcu_barrier_sched(void);
 unsigned long get_state_synchronize_rcu(void);
 void cond_synchronize_rcu(unsigned long oldstate);
@@ -85,12 +93,10 @@ unsigned long rcu_batches_started(void);
 unsigned long rcu_batches_started_bh(void);
 unsigned long rcu_batches_started_sched(void);
 unsigned long rcu_batches_completed(void);
-unsigned long rcu_batches_completed_bh(void);
 unsigned long rcu_batches_completed_sched(void);
 void show_rcu_gp_kthreads(void);
 
 void rcu_force_quiescent_state(void);
-void rcu_bh_force_quiescent_state(void);
 void rcu_sched_force_quiescent_state(void);
 
 void rcu_idle_enter(void);
@@ -105,6 +111,14 @@ extern int rcu_scheduler_active __read_mostly;
 
 bool rcu_is_watching(void);
 
+#ifndef CONFIG_PREEMPT_RT_FULL
+void rcu_bh_force_quiescent_state(void);
+unsigned long rcu_batches_completed_bh(void);
+#else
+# define rcu_bh_force_quiescent_state	rcu_force_quiescent_state
+# define rcu_batches_completed_bh	rcu_batches_completed
+#endif
+
 void rcu_all_qs(void);
 
 #endif /* __LINUX_RCUTREE_H */
diff --git a/include/linux/rtmutex.h b/include/linux/rtmutex.h
index 1abba5ce2a2f..30211c627511 100644
--- a/include/linux/rtmutex.h
+++ b/include/linux/rtmutex.h
@@ -13,11 +13,15 @@
 #define __LINUX_RT_MUTEX_H
 
 #include <linux/linkage.h>
+#include <linux/spinlock_types_raw.h>
 #include <linux/rbtree.h>
-#include <linux/spinlock_types.h>
 
 extern int max_lock_depth; /* for sysctl */
 
+#ifdef CONFIG_DEBUG_MUTEXES
+#include <linux/debug_locks.h>
+#endif
+
 /**
  * The rt_mutex structure
  *
@@ -31,8 +35,8 @@ struct rt_mutex {
 	struct rb_root          waiters;
 	struct rb_node          *waiters_leftmost;
 	struct task_struct	*owner;
-#ifdef CONFIG_DEBUG_RT_MUTEXES
 	int			save_state;
+#ifdef CONFIG_DEBUG_RT_MUTEXES
 	const char 		*name, *file;
 	int			line;
 	void			*magic;
@@ -55,22 +59,33 @@ struct hrtimer_sleeper;
 # define rt_mutex_debug_check_no_locks_held(task)	do { } while (0)
 #endif
 
+# define rt_mutex_init(mutex)					\
+	do {							\
+		raw_spin_lock_init(&(mutex)->wait_lock);	\
+		__rt_mutex_init(mutex, #mutex);			\
+	} while (0)
+
 #ifdef CONFIG_DEBUG_RT_MUTEXES
 # define __DEBUG_RT_MUTEX_INITIALIZER(mutexname) \
 	, .name = #mutexname, .file = __FILE__, .line = __LINE__
-# define rt_mutex_init(mutex)			__rt_mutex_init(mutex, __func__)
  extern void rt_mutex_debug_task_free(struct task_struct *tsk);
 #else
 # define __DEBUG_RT_MUTEX_INITIALIZER(mutexname)
-# define rt_mutex_init(mutex)			__rt_mutex_init(mutex, NULL)
 # define rt_mutex_debug_task_free(t)			do { } while (0)
 #endif
 
-#define __RT_MUTEX_INITIALIZER(mutexname) \
-	{ .wait_lock = __RAW_SPIN_LOCK_UNLOCKED(mutexname.wait_lock) \
+#define __RT_MUTEX_INITIALIZER_PLAIN(mutexname) \
+	 .wait_lock = __RAW_SPIN_LOCK_UNLOCKED(mutexname.wait_lock) \
 	, .waiters = RB_ROOT \
 	, .owner = NULL \
-	__DEBUG_RT_MUTEX_INITIALIZER(mutexname)}
+	__DEBUG_RT_MUTEX_INITIALIZER(mutexname)
+
+#define __RT_MUTEX_INITIALIZER(mutexname) \
+	{ __RT_MUTEX_INITIALIZER_PLAIN(mutexname) }
+
+#define __RT_MUTEX_INITIALIZER_SAVE_STATE(mutexname) \
+	{ __RT_MUTEX_INITIALIZER_PLAIN(mutexname)    \
+	, .save_state = 1 }
 
 #define DEFINE_RT_MUTEX(mutexname) \
 	struct rt_mutex mutexname = __RT_MUTEX_INITIALIZER(mutexname)
@@ -91,6 +106,7 @@ extern void rt_mutex_destroy(struct rt_mutex *lock);
 
 extern void rt_mutex_lock(struct rt_mutex *lock);
 extern int rt_mutex_lock_interruptible(struct rt_mutex *lock);
+extern int rt_mutex_lock_killable(struct rt_mutex *lock);
 extern int rt_mutex_timed_lock(struct rt_mutex *lock,
 			       struct hrtimer_sleeper *timeout);
 
diff --git a/include/linux/rwlock_rt.h b/include/linux/rwlock_rt.h
new file mode 100644
index 000000000000..49ed2d45d3be
--- /dev/null
+++ b/include/linux/rwlock_rt.h
@@ -0,0 +1,99 @@
+#ifndef __LINUX_RWLOCK_RT_H
+#define __LINUX_RWLOCK_RT_H
+
+#ifndef __LINUX_SPINLOCK_H
+#error Do not include directly. Use spinlock.h
+#endif
+
+#define rwlock_init(rwl)				\
+do {							\
+	static struct lock_class_key __key;		\
+							\
+	rt_mutex_init(&(rwl)->lock);			\
+	__rt_rwlock_init(rwl, #rwl, &__key);		\
+} while (0)
+
+extern void __lockfunc rt_write_lock(rwlock_t *rwlock);
+extern void __lockfunc rt_read_lock(rwlock_t *rwlock);
+extern int __lockfunc rt_write_trylock(rwlock_t *rwlock);
+extern int __lockfunc rt_write_trylock_irqsave(rwlock_t *trylock, unsigned long *flags);
+extern int __lockfunc rt_read_trylock(rwlock_t *rwlock);
+extern void __lockfunc rt_write_unlock(rwlock_t *rwlock);
+extern void __lockfunc rt_read_unlock(rwlock_t *rwlock);
+extern unsigned long __lockfunc rt_write_lock_irqsave(rwlock_t *rwlock);
+extern unsigned long __lockfunc rt_read_lock_irqsave(rwlock_t *rwlock);
+extern void __rt_rwlock_init(rwlock_t *rwlock, char *name, struct lock_class_key *key);
+
+#define read_trylock(lock)	__cond_lock(lock, rt_read_trylock(lock))
+#define write_trylock(lock)	__cond_lock(lock, rt_write_trylock(lock))
+
+#define write_trylock_irqsave(lock, flags)	\
+	__cond_lock(lock, rt_write_trylock_irqsave(lock, &flags))
+
+#define read_lock_irqsave(lock, flags)			\
+	do {						\
+		typecheck(unsigned long, flags);	\
+		flags = rt_read_lock_irqsave(lock);	\
+	} while (0)
+
+#define write_lock_irqsave(lock, flags)			\
+	do {						\
+		typecheck(unsigned long, flags);	\
+		flags = rt_write_lock_irqsave(lock);	\
+	} while (0)
+
+#define read_lock(lock)		rt_read_lock(lock)
+
+#define read_lock_bh(lock)				\
+	do {						\
+		local_bh_disable();			\
+		rt_read_lock(lock);			\
+	} while (0)
+
+#define read_lock_irq(lock)	read_lock(lock)
+
+#define write_lock(lock)	rt_write_lock(lock)
+
+#define write_lock_bh(lock)				\
+	do {						\
+		local_bh_disable();			\
+		rt_write_lock(lock);			\
+	} while (0)
+
+#define write_lock_irq(lock)	write_lock(lock)
+
+#define read_unlock(lock)	rt_read_unlock(lock)
+
+#define read_unlock_bh(lock)				\
+	do {						\
+		rt_read_unlock(lock);			\
+		local_bh_enable();			\
+	} while (0)
+
+#define read_unlock_irq(lock)	read_unlock(lock)
+
+#define write_unlock(lock)	rt_write_unlock(lock)
+
+#define write_unlock_bh(lock)				\
+	do {						\
+		rt_write_unlock(lock);			\
+		local_bh_enable();			\
+	} while (0)
+
+#define write_unlock_irq(lock)	write_unlock(lock)
+
+#define read_unlock_irqrestore(lock, flags)		\
+	do {						\
+		typecheck(unsigned long, flags);	\
+		(void) flags;				\
+		rt_read_unlock(lock);			\
+	} while (0)
+
+#define write_unlock_irqrestore(lock, flags) \
+	do {						\
+		typecheck(unsigned long, flags);	\
+		(void) flags;				\
+		rt_write_unlock(lock);			\
+	} while (0)
+
+#endif
diff --git a/include/linux/rwlock_types.h b/include/linux/rwlock_types.h
index cc0072e93e36..d0da966ad7a0 100644
--- a/include/linux/rwlock_types.h
+++ b/include/linux/rwlock_types.h
@@ -1,6 +1,10 @@
 #ifndef __LINUX_RWLOCK_TYPES_H
 #define __LINUX_RWLOCK_TYPES_H
 
+#if !defined(__LINUX_SPINLOCK_TYPES_H)
+# error "Do not include directly, include spinlock_types.h"
+#endif
+
 /*
  * include/linux/rwlock_types.h - generic rwlock type definitions
  *				  and initializers
@@ -43,6 +47,7 @@ typedef struct {
 				RW_DEP_MAP_INIT(lockname) }
 #endif
 
-#define DEFINE_RWLOCK(x)	rwlock_t x = __RW_LOCK_UNLOCKED(x)
+#define DEFINE_RWLOCK(name) \
+	rwlock_t name __cacheline_aligned_in_smp = __RW_LOCK_UNLOCKED(name)
 
 #endif /* __LINUX_RWLOCK_TYPES_H */
diff --git a/include/linux/rwlock_types_rt.h b/include/linux/rwlock_types_rt.h
new file mode 100644
index 000000000000..b13832119591
--- /dev/null
+++ b/include/linux/rwlock_types_rt.h
@@ -0,0 +1,33 @@
+#ifndef __LINUX_RWLOCK_TYPES_RT_H
+#define __LINUX_RWLOCK_TYPES_RT_H
+
+#ifndef __LINUX_SPINLOCK_TYPES_H
+#error "Do not include directly. Include spinlock_types.h instead"
+#endif
+
+/*
+ * rwlocks - rtmutex which allows single reader recursion
+ */
+typedef struct {
+	struct rt_mutex		lock;
+	int			read_depth;
+	unsigned int		break_lock;
+#ifdef CONFIG_DEBUG_LOCK_ALLOC
+	struct lockdep_map	dep_map;
+#endif
+} rwlock_t;
+
+#ifdef CONFIG_DEBUG_LOCK_ALLOC
+# define RW_DEP_MAP_INIT(lockname)	.dep_map = { .name = #lockname }
+#else
+# define RW_DEP_MAP_INIT(lockname)
+#endif
+
+#define __RW_LOCK_UNLOCKED(name) \
+	{ .lock = __RT_MUTEX_INITIALIZER_SAVE_STATE(name.lock),	\
+	  RW_DEP_MAP_INIT(name) }
+
+#define DEFINE_RWLOCK(name) \
+	rwlock_t name __cacheline_aligned_in_smp = __RW_LOCK_UNLOCKED(name)
+
+#endif
diff --git a/include/linux/rwsem.h b/include/linux/rwsem.h
index 8f498cdde280..2b2148431f14 100644
--- a/include/linux/rwsem.h
+++ b/include/linux/rwsem.h
@@ -18,6 +18,10 @@
 #include <linux/osq_lock.h>
 #endif
 
+#ifdef CONFIG_PREEMPT_RT_FULL
+#include <linux/rwsem_rt.h>
+#else /* PREEMPT_RT_FULL */
+
 struct rw_semaphore;
 
 #ifdef CONFIG_RWSEM_GENERIC_SPINLOCK
@@ -177,4 +181,6 @@ extern void up_read_non_owner(struct rw_semaphore *sem);
 # define up_read_non_owner(sem)			up_read(sem)
 #endif
 
+#endif /* !PREEMPT_RT_FULL */
+
 #endif /* _LINUX_RWSEM_H */
diff --git a/include/linux/rwsem_rt.h b/include/linux/rwsem_rt.h
new file mode 100644
index 000000000000..f97860b2e2a4
--- /dev/null
+++ b/include/linux/rwsem_rt.h
@@ -0,0 +1,152 @@
+#ifndef _LINUX_RWSEM_RT_H
+#define _LINUX_RWSEM_RT_H
+
+#ifndef _LINUX_RWSEM_H
+#error "Include rwsem.h"
+#endif
+
+/*
+ * RW-semaphores are a spinlock plus a reader-depth count.
+ *
+ * Note that the semantics are different from the usual
+ * Linux rw-sems, in PREEMPT_RT mode we do not allow
+ * multiple readers to hold the lock at once, we only allow
+ * a read-lock owner to read-lock recursively. This is
+ * better for latency, makes the implementation inherently
+ * fair and makes it simpler as well.
+ */
+
+#include <linux/rtmutex.h>
+
+struct rw_semaphore {
+	struct rt_mutex		lock;
+	int			read_depth;
+#ifdef CONFIG_DEBUG_LOCK_ALLOC
+	struct lockdep_map	dep_map;
+#endif
+};
+
+#define __RWSEM_INITIALIZER(name) \
+	{ .lock = __RT_MUTEX_INITIALIZER(name.lock), \
+	  RW_DEP_MAP_INIT(name) }
+
+#define DECLARE_RWSEM(lockname) \
+	struct rw_semaphore lockname = __RWSEM_INITIALIZER(lockname)
+
+extern void  __rt_rwsem_init(struct rw_semaphore *rwsem, const char *name,
+				     struct lock_class_key *key);
+
+#define __rt_init_rwsem(sem, name, key)			\
+	do {						\
+		rt_mutex_init(&(sem)->lock);		\
+		__rt_rwsem_init((sem), (name), (key));\
+	} while (0)
+
+#define __init_rwsem(sem, name, key) __rt_init_rwsem(sem, name, key)
+
+# define rt_init_rwsem(sem)				\
+do {							\
+	static struct lock_class_key __key;		\
+							\
+	__rt_init_rwsem((sem), #sem, &__key);		\
+} while (0)
+
+extern void rt_down_write(struct rw_semaphore *rwsem);
+extern void rt_down_read_nested(struct rw_semaphore *rwsem, int subclass);
+extern void rt_down_write_nested(struct rw_semaphore *rwsem, int subclass);
+extern void rt_down_write_nested_lock(struct rw_semaphore *rwsem,
+				      struct lockdep_map *nest);
+extern void rt__down_read(struct rw_semaphore *rwsem);
+extern void rt_down_read(struct rw_semaphore *rwsem);
+extern int  rt_down_write_trylock(struct rw_semaphore *rwsem);
+extern int  rt__down_read_trylock(struct rw_semaphore *rwsem);
+extern int  rt_down_read_trylock(struct rw_semaphore *rwsem);
+extern void __rt_up_read(struct rw_semaphore *rwsem);
+extern void rt_up_read(struct rw_semaphore *rwsem);
+extern void rt_up_write(struct rw_semaphore *rwsem);
+extern void rt_downgrade_write(struct rw_semaphore *rwsem);
+
+#define init_rwsem(sem)		rt_init_rwsem(sem)
+#define rwsem_is_locked(s)	rt_mutex_is_locked(&(s)->lock)
+
+static inline int rwsem_is_contended(struct rw_semaphore *sem)
+{
+	/* rt_mutex_has_waiters() */
+	return !RB_EMPTY_ROOT(&sem->lock.waiters);
+}
+
+static inline void __down_read(struct rw_semaphore *sem)
+{
+	rt__down_read(sem);
+}
+
+static inline void down_read(struct rw_semaphore *sem)
+{
+	rt_down_read(sem);
+}
+
+static inline int __down_read_trylock(struct rw_semaphore *sem)
+{
+	return rt__down_read_trylock(sem);
+}
+
+static inline int down_read_trylock(struct rw_semaphore *sem)
+{
+	return rt_down_read_trylock(sem);
+}
+
+static inline void down_write(struct rw_semaphore *sem)
+{
+	rt_down_write(sem);
+}
+
+static inline int down_write_trylock(struct rw_semaphore *sem)
+{
+	return rt_down_write_trylock(sem);
+}
+
+static inline void __up_read(struct rw_semaphore *sem)
+{
+	__rt_up_read(sem);
+}
+
+static inline void up_read(struct rw_semaphore *sem)
+{
+	rt_up_read(sem);
+}
+
+static inline void up_write(struct rw_semaphore *sem)
+{
+	rt_up_write(sem);
+}
+
+static inline void downgrade_write(struct rw_semaphore *sem)
+{
+	rt_downgrade_write(sem);
+}
+
+static inline void down_read_nested(struct rw_semaphore *sem, int subclass)
+{
+	return rt_down_read_nested(sem, subclass);
+}
+
+static inline void down_write_nested(struct rw_semaphore *sem, int subclass)
+{
+	rt_down_write_nested(sem, subclass);
+}
+#ifdef CONFIG_DEBUG_LOCK_ALLOC
+static inline void down_write_nest_lock(struct rw_semaphore *sem,
+		struct rw_semaphore *nest_lock)
+{
+	rt_down_write_nested_lock(sem, &nest_lock->dep_map);
+}
+
+#else
+
+static inline void down_write_nest_lock(struct rw_semaphore *sem,
+		struct rw_semaphore *nest_lock)
+{
+	rt_down_write_nested_lock(sem, NULL);
+}
+#endif
+#endif
diff --git a/include/linux/sched.h b/include/linux/sched.h
index fa39434e3fdd..a8d5ae88b30b 100644
--- a/include/linux/sched.h
+++ b/include/linux/sched.h
@@ -26,6 +26,7 @@ struct sched_param {
 #include <linux/nodemask.h>
 #include <linux/mm_types.h>
 #include <linux/preempt.h>
+#include <asm/kmap_types.h>
 
 #include <asm/page.h>
 #include <asm/ptrace.h>
@@ -242,10 +243,7 @@ extern char ___assert_task_state[1 - 2*!!(
 				 TASK_UNINTERRUPTIBLE | __TASK_STOPPED | \
 				 __TASK_TRACED | EXIT_ZOMBIE | EXIT_DEAD)
 
-#define task_is_traced(task)	((task->state & __TASK_TRACED) != 0)
 #define task_is_stopped(task)	((task->state & __TASK_STOPPED) != 0)
-#define task_is_stopped_or_traced(task)	\
-			((task->state & (__TASK_STOPPED | __TASK_TRACED)) != 0)
 #define task_contributes_to_load(task)	\
 				((task->state & TASK_UNINTERRUPTIBLE) != 0 && \
 				 (task->flags & PF_FROZEN) == 0 && \
@@ -311,6 +309,11 @@ extern char ___assert_task_state[1 - 2*!!(
 
 #endif
 
+#define __set_current_state_no_track(state_value)	\
+	do { current->state = (state_value); } while (0)
+#define set_current_state_no_track(state_value)		\
+	set_mb(current->state, (state_value))
+
 /* Task command name length */
 #define TASK_COMM_LEN 16
 
@@ -968,8 +971,18 @@ struct wake_q_head {
 	struct wake_q_head name = { WAKE_Q_TAIL, &name.first }
 
 extern void wake_q_add(struct wake_q_head *head,
-		       struct task_struct *task);
-extern void wake_up_q(struct wake_q_head *head);
+			      struct task_struct *task);
+extern void __wake_up_q(struct wake_q_head *head, bool sleeper);
+
+static inline void wake_up_q(struct wake_q_head *head)
+{
+	__wake_up_q(head, false);
+}
+
+static inline void wake_up_q_sleeper(struct wake_q_head *head)
+{
+	__wake_up_q(head, true);
+}
 
 /*
  * sched-domains (multiprocessor balancing) declarations:
@@ -1377,6 +1390,7 @@ struct tlbflush_unmap_batch {
 
 struct task_struct {
 	volatile long state;	/* -1 unrunnable, 0 runnable, >0 stopped */
+	volatile long saved_state;	/* saved state for "spinlock sleepers" */
 	void *stack;
 	atomic_t usage;
 	unsigned int flags;	/* per process flags, defined below */
@@ -1413,6 +1427,12 @@ struct task_struct {
 #endif
 
 	unsigned int policy;
+#ifdef CONFIG_PREEMPT_RT_FULL
+	int migrate_disable;
+# ifdef CONFIG_SCHED_DEBUG
+	int migrate_disable_atomic;
+# endif
+#endif
 	int nr_cpus_allowed;
 	cpumask_t cpus_allowed;
 
@@ -1520,7 +1540,8 @@ struct task_struct {
 	cputime_t gtime;
 	struct prev_cputime prev_cputime;
 #ifdef CONFIG_VIRT_CPU_ACCOUNTING_GEN
-	seqlock_t vtime_seqlock;
+	raw_spinlock_t vtime_lock;
+	seqcount_t vtime_seq;
 	unsigned long long vtime_snap;
 	enum {
 		VTIME_SLEEPING = 0,
@@ -1536,6 +1557,9 @@ struct task_struct {
 
 	struct task_cputime cputime_expires;
 	struct list_head cpu_timers[3];
+#ifdef CONFIG_PREEMPT_RT_BASE
+	struct task_struct *posix_timer_list;
+#endif
 
 /* process credentials */
 	const struct cred __rcu *real_cred; /* objective and real subjective task
@@ -1566,10 +1590,15 @@ struct task_struct {
 /* signal handlers */
 	struct signal_struct *signal;
 	struct sighand_struct *sighand;
+	struct sigqueue *sigqueue_cache;
 
 	sigset_t blocked, real_blocked;
 	sigset_t saved_sigmask;	/* restored if set_restore_sigmask() was used */
 	struct sigpending pending;
+#ifdef CONFIG_PREEMPT_RT_FULL
+	/* TODO: move me into ->restart_block ? */
+	struct siginfo forced_info;
+#endif
 
 	unsigned long sas_ss_sp;
 	size_t sas_ss_size;
@@ -1793,6 +1822,12 @@ struct task_struct {
 	unsigned long trace;
 	/* bitmask and counter of trace recursion */
 	unsigned long trace_recursion;
+#ifdef CONFIG_WAKEUP_LATENCY_HIST
+	u64 preempt_timestamp_hist;
+#ifdef CONFIG_MISSED_TIMER_OFFSETS_HIST
+	long timer_offset;
+#endif
+#endif
 #endif /* CONFIG_TRACING */
 #ifdef CONFIG_MEMCG
 	struct mem_cgroup *memcg_in_oom;
@@ -1809,9 +1844,23 @@ struct task_struct {
 	unsigned int	sequential_io;
 	unsigned int	sequential_io_avg;
 #endif
+#ifdef CONFIG_PREEMPT_RT_BASE
+	struct rcu_head put_rcu;
+	int softirq_nestcnt;
+	unsigned int softirqs_raised;
+#endif
+#ifdef CONFIG_PREEMPT_RT_FULL
+# if defined CONFIG_HIGHMEM || defined CONFIG_X86_32
+	int kmap_idx;
+	pte_t kmap_pte[KM_TYPE_NR];
+# endif
+#endif
 #ifdef CONFIG_DEBUG_ATOMIC_SLEEP
 	unsigned long	task_state_change;
 #endif
+#ifdef CONFIG_PREEMPT_RT_FULL
+	int xmit_recursion;
+#endif
 	int pagefault_disabled;
 /* CPU-specific state of this task */
 	struct thread_struct thread;
@@ -1829,9 +1878,6 @@ extern int arch_task_struct_size __read_mostly;
 # define arch_task_struct_size (sizeof(struct task_struct))
 #endif
 
-/* Future-safe accessor for struct task_struct's cpus_allowed. */
-#define tsk_cpus_allowed(tsk) (&(tsk)->cpus_allowed)
-
 #define TNF_MIGRATED	0x01
 #define TNF_NO_GROUP	0x02
 #define TNF_SHARED	0x04
@@ -2021,6 +2067,15 @@ extern struct pid *cad_pid;
 extern void free_task(struct task_struct *tsk);
 #define get_task_struct(tsk) do { atomic_inc(&(tsk)->usage); } while(0)
 
+#ifdef CONFIG_PREEMPT_RT_BASE
+extern void __put_task_struct_cb(struct rcu_head *rhp);
+
+static inline void put_task_struct(struct task_struct *t)
+{
+	if (atomic_dec_and_test(&t->usage))
+		call_rcu(&t->put_rcu, __put_task_struct_cb);
+}
+#else
 extern void __put_task_struct(struct task_struct *t);
 
 static inline void put_task_struct(struct task_struct *t)
@@ -2028,6 +2083,7 @@ static inline void put_task_struct(struct task_struct *t)
 	if (atomic_dec_and_test(&t->usage))
 		__put_task_struct(t);
 }
+#endif
 
 #ifdef CONFIG_VIRT_CPU_ACCOUNTING_GEN
 extern void task_cputime(struct task_struct *t,
@@ -2066,6 +2122,7 @@ extern void thread_group_cputime_adjusted(struct task_struct *p, cputime_t *ut,
 /*
  * Per process flags
  */
+#define PF_IN_SOFTIRQ	0x00000001	/* Task is serving softirq */
 #define PF_EXITING	0x00000004	/* getting shut down */
 #define PF_EXITPIDONE	0x00000008	/* pi exit done on shut down */
 #define PF_VCPU		0x00000010	/* I'm a virtual CPU */
@@ -2230,6 +2287,10 @@ extern void do_set_cpus_allowed(struct task_struct *p,
 
 extern int set_cpus_allowed_ptr(struct task_struct *p,
 				const struct cpumask *new_mask);
+int migrate_me(void);
+void tell_sched_cpu_down_begin(int cpu);
+void tell_sched_cpu_down_done(int cpu);
+
 #else
 static inline void do_set_cpus_allowed(struct task_struct *p,
 				      const struct cpumask *new_mask)
@@ -2242,6 +2303,9 @@ static inline int set_cpus_allowed_ptr(struct task_struct *p,
 		return -EINVAL;
 	return 0;
 }
+static inline int migrate_me(void) { return 0; }
+static inline void tell_sched_cpu_down_begin(int cpu) { }
+static inline void tell_sched_cpu_down_done(int cpu) { }
 #endif
 
 #ifdef CONFIG_NO_HZ_COMMON
@@ -2451,6 +2515,7 @@ extern void xtime_update(unsigned long ticks);
 
 extern int wake_up_state(struct task_struct *tsk, unsigned int state);
 extern int wake_up_process(struct task_struct *tsk);
+extern int wake_up_lock_sleeper(struct task_struct * tsk);
 extern void wake_up_new_task(struct task_struct *tsk);
 #ifdef CONFIG_SMP
  extern void kick_process(struct task_struct *tsk);
@@ -2574,12 +2639,24 @@ extern struct mm_struct * mm_alloc(void);
 
 /* mmdrop drops the mm and the page tables */
 extern void __mmdrop(struct mm_struct *);
+
 static inline void mmdrop(struct mm_struct * mm)
 {
 	if (unlikely(atomic_dec_and_test(&mm->mm_count)))
 		__mmdrop(mm);
 }
 
+#ifdef CONFIG_PREEMPT_RT_BASE
+extern void __mmdrop_delayed(struct rcu_head *rhp);
+static inline void mmdrop_delayed(struct mm_struct *mm)
+{
+	if (atomic_dec_and_test(&mm->mm_count))
+		call_rcu(&mm->delayed_drop, __mmdrop_delayed);
+}
+#else
+# define mmdrop_delayed(mm)	mmdrop(mm)
+#endif
+
 /* mmput gets rid of the mappings and all user-space */
 extern void mmput(struct mm_struct *);
 /* Grab a reference to a task's mm, if it is not already going away */
@@ -2889,6 +2966,43 @@ static inline int test_tsk_need_resched(struct task_struct *tsk)
 	return unlikely(test_tsk_thread_flag(tsk,TIF_NEED_RESCHED));
 }
 
+#ifdef CONFIG_PREEMPT_LAZY
+static inline void set_tsk_need_resched_lazy(struct task_struct *tsk)
+{
+	set_tsk_thread_flag(tsk,TIF_NEED_RESCHED_LAZY);
+}
+
+static inline void clear_tsk_need_resched_lazy(struct task_struct *tsk)
+{
+	clear_tsk_thread_flag(tsk,TIF_NEED_RESCHED_LAZY);
+}
+
+static inline int test_tsk_need_resched_lazy(struct task_struct *tsk)
+{
+	return unlikely(test_tsk_thread_flag(tsk,TIF_NEED_RESCHED_LAZY));
+}
+
+static inline int need_resched_lazy(void)
+{
+	return test_thread_flag(TIF_NEED_RESCHED_LAZY);
+}
+
+static inline int need_resched_now(void)
+{
+	return test_thread_flag(TIF_NEED_RESCHED);
+}
+
+#else
+static inline void clear_tsk_need_resched_lazy(struct task_struct *tsk) { }
+static inline int need_resched_lazy(void) { return 0; }
+
+static inline int need_resched_now(void)
+{
+	return test_thread_flag(TIF_NEED_RESCHED);
+}
+
+#endif
+
 static inline int restart_syscall(void)
 {
 	set_tsk_thread_flag(current, TIF_SIGPENDING);
@@ -2920,6 +3034,51 @@ static inline int signal_pending_state(long state, struct task_struct *p)
 	return (state & TASK_INTERRUPTIBLE) || __fatal_signal_pending(p);
 }
 
+static inline bool __task_is_stopped_or_traced(struct task_struct *task)
+{
+	if (task->state & (__TASK_STOPPED | __TASK_TRACED))
+		return true;
+#ifdef CONFIG_PREEMPT_RT_FULL
+	if (task->saved_state & (__TASK_STOPPED | __TASK_TRACED))
+		return true;
+#endif
+	return false;
+}
+
+static inline bool task_is_stopped_or_traced(struct task_struct *task)
+{
+	bool traced_stopped;
+
+#ifdef CONFIG_PREEMPT_RT_FULL
+	unsigned long flags;
+
+	raw_spin_lock_irqsave(&task->pi_lock, flags);
+	traced_stopped = __task_is_stopped_or_traced(task);
+	raw_spin_unlock_irqrestore(&task->pi_lock, flags);
+#else
+	traced_stopped = __task_is_stopped_or_traced(task);
+#endif
+	return traced_stopped;
+}
+
+static inline bool task_is_traced(struct task_struct *task)
+{
+	bool traced = false;
+
+	if (task->state & __TASK_TRACED)
+		return true;
+#ifdef CONFIG_PREEMPT_RT_FULL
+	/* in case the task is sleeping on tasklist_lock */
+	raw_spin_lock_irq(&task->pi_lock);
+	if (task->state & __TASK_TRACED)
+		traced = true;
+	else if (task->saved_state & __TASK_TRACED)
+		traced = true;
+	raw_spin_unlock_irq(&task->pi_lock);
+#endif
+	return traced;
+}
+
 /*
  * cond_resched() and cond_resched_lock(): latency reduction via
  * explicit rescheduling in places that are safe. The return
@@ -2941,12 +3100,16 @@ extern int __cond_resched_lock(spinlock_t *lock);
 	__cond_resched_lock(lock);				\
 })
 
+#ifndef CONFIG_PREEMPT_RT_FULL
 extern int __cond_resched_softirq(void);
 
 #define cond_resched_softirq() ({					\
 	___might_sleep(__FILE__, __LINE__, SOFTIRQ_DISABLE_OFFSET);	\
 	__cond_resched_softirq();					\
 })
+#else
+# define cond_resched_softirq()		cond_resched()
+#endif
 
 static inline void cond_resched_rcu(void)
 {
@@ -3108,6 +3271,31 @@ static inline void set_task_cpu(struct task_struct *p, unsigned int cpu)
 
 #endif /* CONFIG_SMP */
 
+static inline int __migrate_disabled(struct task_struct *p)
+{
+#ifdef CONFIG_PREEMPT_RT_FULL
+	return p->migrate_disable;
+#else
+	return 0;
+#endif
+}
+
+/* Future-safe accessor for struct task_struct's cpus_allowed. */
+static inline const struct cpumask *tsk_cpus_allowed(struct task_struct *p)
+{
+	if (__migrate_disabled(p))
+		return cpumask_of(task_cpu(p));
+
+	return &p->cpus_allowed;
+}
+
+static inline int tsk_nr_cpus_allowed(struct task_struct *p)
+{
+	if (__migrate_disabled(p))
+		return 1;
+	return p->nr_cpus_allowed;
+}
+
 extern long sched_setaffinity(pid_t pid, const struct cpumask *new_mask);
 extern long sched_getaffinity(pid_t pid, struct cpumask *mask);
 
diff --git a/include/linux/seqlock.h b/include/linux/seqlock.h
index e0582106ef4f..b14f4d2368aa 100644
--- a/include/linux/seqlock.h
+++ b/include/linux/seqlock.h
@@ -220,20 +220,30 @@ static inline int read_seqcount_retry(const seqcount_t *s, unsigned start)
 	return __read_seqcount_retry(s, start);
 }
 
-
-
-static inline void raw_write_seqcount_begin(seqcount_t *s)
+static inline void __raw_write_seqcount_begin(seqcount_t *s)
 {
 	s->sequence++;
 	smp_wmb();
 }
 
-static inline void raw_write_seqcount_end(seqcount_t *s)
+static inline void raw_write_seqcount_begin(seqcount_t *s)
+{
+	preempt_disable_rt();
+	__raw_write_seqcount_begin(s);
+}
+
+static inline void __raw_write_seqcount_end(seqcount_t *s)
 {
 	smp_wmb();
 	s->sequence++;
 }
 
+static inline void raw_write_seqcount_end(seqcount_t *s)
+{
+	__raw_write_seqcount_end(s);
+	preempt_enable_rt();
+}
+
 /**
  * raw_write_seqcount_barrier - do a seq write barrier
  * @s: pointer to seqcount_t
@@ -425,10 +435,32 @@ typedef struct {
 /*
  * Read side functions for starting and finalizing a read side section.
  */
+#ifndef CONFIG_PREEMPT_RT_FULL
 static inline unsigned read_seqbegin(const seqlock_t *sl)
 {
 	return read_seqcount_begin(&sl->seqcount);
 }
+#else
+/*
+ * Starvation safe read side for RT
+ */
+static inline unsigned read_seqbegin(seqlock_t *sl)
+{
+	unsigned ret;
+
+repeat:
+	ret = ACCESS_ONCE(sl->seqcount.sequence);
+	if (unlikely(ret & 1)) {
+		/*
+		 * Take the lock and let the writer proceed (i.e. evtl
+		 * boost it), otherwise we could loop here forever.
+		 */
+		spin_unlock_wait(&sl->lock);
+		goto repeat;
+	}
+	return ret;
+}
+#endif
 
 static inline unsigned read_seqretry(const seqlock_t *sl, unsigned start)
 {
@@ -443,36 +475,36 @@ static inline unsigned read_seqretry(const seqlock_t *sl, unsigned start)
 static inline void write_seqlock(seqlock_t *sl)
 {
 	spin_lock(&sl->lock);
-	write_seqcount_begin(&sl->seqcount);
+	__raw_write_seqcount_begin(&sl->seqcount);
 }
 
 static inline void write_sequnlock(seqlock_t *sl)
 {
-	write_seqcount_end(&sl->seqcount);
+	__raw_write_seqcount_end(&sl->seqcount);
 	spin_unlock(&sl->lock);
 }
 
 static inline void write_seqlock_bh(seqlock_t *sl)
 {
 	spin_lock_bh(&sl->lock);
-	write_seqcount_begin(&sl->seqcount);
+	__raw_write_seqcount_begin(&sl->seqcount);
 }
 
 static inline void write_sequnlock_bh(seqlock_t *sl)
 {
-	write_seqcount_end(&sl->seqcount);
+	__raw_write_seqcount_end(&sl->seqcount);
 	spin_unlock_bh(&sl->lock);
 }
 
 static inline void write_seqlock_irq(seqlock_t *sl)
 {
 	spin_lock_irq(&sl->lock);
-	write_seqcount_begin(&sl->seqcount);
+	__raw_write_seqcount_begin(&sl->seqcount);
 }
 
 static inline void write_sequnlock_irq(seqlock_t *sl)
 {
-	write_seqcount_end(&sl->seqcount);
+	__raw_write_seqcount_end(&sl->seqcount);
 	spin_unlock_irq(&sl->lock);
 }
 
@@ -481,7 +513,7 @@ static inline unsigned long __write_seqlock_irqsave(seqlock_t *sl)
 	unsigned long flags;
 
 	spin_lock_irqsave(&sl->lock, flags);
-	write_seqcount_begin(&sl->seqcount);
+	__raw_write_seqcount_begin(&sl->seqcount);
 	return flags;
 }
 
@@ -491,7 +523,7 @@ static inline unsigned long __write_seqlock_irqsave(seqlock_t *sl)
 static inline void
 write_sequnlock_irqrestore(seqlock_t *sl, unsigned long flags)
 {
-	write_seqcount_end(&sl->seqcount);
+	__raw_write_seqcount_end(&sl->seqcount);
 	spin_unlock_irqrestore(&sl->lock, flags);
 }
 
diff --git a/include/linux/signal.h b/include/linux/signal.h
index 92557bbce7e7..561a1283ee97 100644
--- a/include/linux/signal.h
+++ b/include/linux/signal.h
@@ -218,6 +218,7 @@ static inline void init_sigpending(struct sigpending *sig)
 }
 
 extern void flush_sigqueue(struct sigpending *queue);
+extern void flush_task_sigqueue(struct task_struct *tsk);
 
 /* Test if 'sig' is valid signal. Use this instead of testing _NSIG directly */
 static inline int valid_signal(unsigned long sig)
diff --git a/include/linux/skbuff.h b/include/linux/skbuff.h
index 4355129fff91..628388c4933c 100644
--- a/include/linux/skbuff.h
+++ b/include/linux/skbuff.h
@@ -203,6 +203,7 @@ struct sk_buff_head {
 
 	__u32		qlen;
 	spinlock_t	lock;
+	raw_spinlock_t	raw_lock;
 };
 
 struct sk_buff;
@@ -1463,6 +1464,12 @@ static inline void skb_queue_head_init(struct sk_buff_head *list)
 	__skb_queue_head_init(list);
 }
 
+static inline void skb_queue_head_init_raw(struct sk_buff_head *list)
+{
+	raw_spin_lock_init(&list->raw_lock);
+	__skb_queue_head_init(list);
+}
+
 static inline void skb_queue_head_init_class(struct sk_buff_head *list,
 		struct lock_class_key *class)
 {
diff --git a/include/linux/smp.h b/include/linux/smp.h
index c4414074bd88..e6ab36aeaaab 100644
--- a/include/linux/smp.h
+++ b/include/linux/smp.h
@@ -185,6 +185,9 @@ static inline void smp_init(void) { }
 #define get_cpu()		({ preempt_disable(); smp_processor_id(); })
 #define put_cpu()		preempt_enable()
 
+#define get_cpu_light()		({ migrate_disable(); smp_processor_id(); })
+#define put_cpu_light()		migrate_enable()
+
 /*
  * Callback to arch code if there's nosmp or maxcpus=0 on the
  * boot command line:
diff --git a/include/linux/spinlock.h b/include/linux/spinlock.h
index 47dd0cebd204..b241cc044bd3 100644
--- a/include/linux/spinlock.h
+++ b/include/linux/spinlock.h
@@ -271,7 +271,11 @@ static inline void do_raw_spin_unlock(raw_spinlock_t *lock) __releases(lock)
 #define raw_spin_can_lock(lock)	(!raw_spin_is_locked(lock))
 
 /* Include rwlock functions */
-#include <linux/rwlock.h>
+#ifdef CONFIG_PREEMPT_RT_FULL
+# include <linux/rwlock_rt.h>
+#else
+# include <linux/rwlock.h>
+#endif
 
 /*
  * Pull the _spin_*()/_read_*()/_write_*() functions/declarations:
@@ -282,6 +286,10 @@ static inline void do_raw_spin_unlock(raw_spinlock_t *lock) __releases(lock)
 # include <linux/spinlock_api_up.h>
 #endif
 
+#ifdef CONFIG_PREEMPT_RT_FULL
+# include <linux/spinlock_rt.h>
+#else /* PREEMPT_RT_FULL */
+
 /*
  * Map the spin_lock functions to the raw variants for PREEMPT_RT=n
  */
@@ -416,4 +424,6 @@ extern int _atomic_dec_and_lock(atomic_t *atomic, spinlock_t *lock);
 #define atomic_dec_and_lock(atomic, lock) \
 		__cond_lock(lock, _atomic_dec_and_lock(atomic, lock))
 
+#endif /* !PREEMPT_RT_FULL */
+
 #endif /* __LINUX_SPINLOCK_H */
diff --git a/include/linux/spinlock_api_smp.h b/include/linux/spinlock_api_smp.h
index 5344268e6e62..043263f30e81 100644
--- a/include/linux/spinlock_api_smp.h
+++ b/include/linux/spinlock_api_smp.h
@@ -189,6 +189,8 @@ static inline int __raw_spin_trylock_bh(raw_spinlock_t *lock)
 	return 0;
 }
 
-#include <linux/rwlock_api_smp.h>
+#ifndef CONFIG_PREEMPT_RT_FULL
+# include <linux/rwlock_api_smp.h>
+#endif
 
 #endif /* __LINUX_SPINLOCK_API_SMP_H */
diff --git a/include/linux/spinlock_rt.h b/include/linux/spinlock_rt.h
new file mode 100644
index 000000000000..f757096b230c
--- /dev/null
+++ b/include/linux/spinlock_rt.h
@@ -0,0 +1,174 @@
+#ifndef __LINUX_SPINLOCK_RT_H
+#define __LINUX_SPINLOCK_RT_H
+
+#ifndef __LINUX_SPINLOCK_H
+#error Do not include directly. Use spinlock.h
+#endif
+
+#include <linux/bug.h>
+
+extern void
+__rt_spin_lock_init(spinlock_t *lock, char *name, struct lock_class_key *key);
+
+#define spin_lock_init(slock)				\
+do {							\
+	static struct lock_class_key __key;		\
+							\
+	rt_mutex_init(&(slock)->lock);			\
+	__rt_spin_lock_init(slock, #slock, &__key);	\
+} while (0)
+
+extern void __lockfunc rt_spin_lock(spinlock_t *lock);
+extern unsigned long __lockfunc rt_spin_lock_trace_flags(spinlock_t *lock);
+extern void __lockfunc rt_spin_lock_nested(spinlock_t *lock, int subclass);
+extern void __lockfunc rt_spin_unlock(spinlock_t *lock);
+extern void __lockfunc rt_spin_unlock_wait(spinlock_t *lock);
+extern int __lockfunc rt_spin_trylock_irqsave(spinlock_t *lock, unsigned long *flags);
+extern int __lockfunc rt_spin_trylock_bh(spinlock_t *lock);
+extern int __lockfunc rt_spin_trylock(spinlock_t *lock);
+extern int atomic_dec_and_spin_lock(atomic_t *atomic, spinlock_t *lock);
+
+/*
+ * lockdep-less calls, for derived types like rwlock:
+ * (for trylock they can use rt_mutex_trylock() directly.
+ */
+extern void __lockfunc __rt_spin_lock(struct rt_mutex *lock);
+extern void __lockfunc __rt_spin_unlock(struct rt_mutex *lock);
+extern int __lockfunc __rt_spin_trylock(struct rt_mutex *lock);
+
+#define spin_lock(lock)				\
+	do {					\
+		migrate_disable();		\
+		rt_spin_lock(lock);		\
+	} while (0)
+
+#define spin_lock_bh(lock)			\
+	do {					\
+		local_bh_disable();		\
+		migrate_disable();		\
+		rt_spin_lock(lock);		\
+	} while (0)
+
+#define spin_lock_irq(lock)		spin_lock(lock)
+
+#define spin_do_trylock(lock)		__cond_lock(lock, rt_spin_trylock(lock))
+
+#define spin_trylock(lock)			\
+({						\
+	int __locked;				\
+	migrate_disable();			\
+	__locked = spin_do_trylock(lock);	\
+	if (!__locked)				\
+		migrate_enable();		\
+	__locked;				\
+})
+
+#ifdef CONFIG_LOCKDEP
+# define spin_lock_nested(lock, subclass)		\
+	do {						\
+		migrate_disable();			\
+		rt_spin_lock_nested(lock, subclass);	\
+	} while (0)
+
+#define spin_lock_bh_nested(lock, subclass)		\
+	do {						\
+		local_bh_disable();			\
+		migrate_disable();			\
+		rt_spin_lock_nested(lock, subclass);	\
+	} while (0)
+
+# define spin_lock_irqsave_nested(lock, flags, subclass) \
+	do {						 \
+		typecheck(unsigned long, flags);	 \
+		flags = 0;				 \
+		migrate_disable();			 \
+		rt_spin_lock_nested(lock, subclass);	 \
+	} while (0)
+#else
+# define spin_lock_nested(lock, subclass)	spin_lock(lock)
+# define spin_lock_bh_nested(lock, subclass)	spin_lock_bh(lock)
+
+# define spin_lock_irqsave_nested(lock, flags, subclass) \
+	do {						 \
+		typecheck(unsigned long, flags);	 \
+		flags = 0;				 \
+		spin_lock(lock);			 \
+	} while (0)
+#endif
+
+#define spin_lock_irqsave(lock, flags)			 \
+	do {						 \
+		typecheck(unsigned long, flags);	 \
+		flags = 0;				 \
+		spin_lock(lock);			 \
+	} while (0)
+
+static inline unsigned long spin_lock_trace_flags(spinlock_t *lock)
+{
+	unsigned long flags = 0;
+#ifdef CONFIG_TRACE_IRQFLAGS
+	flags = rt_spin_lock_trace_flags(lock);
+#else
+	spin_lock(lock); /* lock_local */
+#endif
+	return flags;
+}
+
+/* FIXME: we need rt_spin_lock_nest_lock */
+#define spin_lock_nest_lock(lock, nest_lock) spin_lock_nested(lock, 0)
+
+#define spin_unlock(lock)				\
+	do {						\
+		rt_spin_unlock(lock);			\
+		migrate_enable();			\
+	} while (0)
+
+#define spin_unlock_bh(lock)				\
+	do {						\
+		rt_spin_unlock(lock);			\
+		migrate_enable();			\
+		local_bh_enable();			\
+	} while (0)
+
+#define spin_unlock_irq(lock)		spin_unlock(lock)
+
+#define spin_unlock_irqrestore(lock, flags)		\
+	do {						\
+		typecheck(unsigned long, flags);	\
+		(void) flags;				\
+		spin_unlock(lock);			\
+	} while (0)
+
+#define spin_trylock_bh(lock)	__cond_lock(lock, rt_spin_trylock_bh(lock))
+#define spin_trylock_irq(lock)	spin_trylock(lock)
+
+#define spin_trylock_irqsave(lock, flags)	\
+	rt_spin_trylock_irqsave(lock, &(flags))
+
+#define spin_unlock_wait(lock)		rt_spin_unlock_wait(lock)
+
+#ifdef CONFIG_GENERIC_LOCKBREAK
+# define spin_is_contended(lock)	((lock)->break_lock)
+#else
+# define spin_is_contended(lock)	(((void)(lock), 0))
+#endif
+
+static inline int spin_can_lock(spinlock_t *lock)
+{
+	return !rt_mutex_is_locked(&lock->lock);
+}
+
+static inline int spin_is_locked(spinlock_t *lock)
+{
+	return rt_mutex_is_locked(&lock->lock);
+}
+
+static inline void assert_spin_locked(spinlock_t *lock)
+{
+	BUG_ON(!spin_is_locked(lock));
+}
+
+#define atomic_dec_and_lock(atomic, lock) \
+	atomic_dec_and_spin_lock(atomic, lock)
+
+#endif
diff --git a/include/linux/spinlock_types.h b/include/linux/spinlock_types.h
index 73548eb13a5d..10bac715ea96 100644
--- a/include/linux/spinlock_types.h
+++ b/include/linux/spinlock_types.h
@@ -9,80 +9,15 @@
  * Released under the General Public License (GPL).
  */
 
-#if defined(CONFIG_SMP)
-# include <asm/spinlock_types.h>
+#include <linux/spinlock_types_raw.h>
+
+#ifndef CONFIG_PREEMPT_RT_FULL
+# include <linux/spinlock_types_nort.h>
+# include <linux/rwlock_types.h>
 #else
-# include <linux/spinlock_types_up.h>
+# include <linux/rtmutex.h>
+# include <linux/spinlock_types_rt.h>
+# include <linux/rwlock_types_rt.h>
 #endif
 
-#include <linux/lockdep.h>
-
-typedef struct raw_spinlock {
-	arch_spinlock_t raw_lock;
-#ifdef CONFIG_GENERIC_LOCKBREAK
-	unsigned int break_lock;
-#endif
-#ifdef CONFIG_DEBUG_SPINLOCK
-	unsigned int magic, owner_cpu;
-	void *owner;
-#endif
-#ifdef CONFIG_DEBUG_LOCK_ALLOC
-	struct lockdep_map dep_map;
-#endif
-} raw_spinlock_t;
-
-#define SPINLOCK_MAGIC		0xdead4ead
-
-#define SPINLOCK_OWNER_INIT	((void *)-1L)
-
-#ifdef CONFIG_DEBUG_LOCK_ALLOC
-# define SPIN_DEP_MAP_INIT(lockname)	.dep_map = { .name = #lockname }
-#else
-# define SPIN_DEP_MAP_INIT(lockname)
-#endif
-
-#ifdef CONFIG_DEBUG_SPINLOCK
-# define SPIN_DEBUG_INIT(lockname)		\
-	.magic = SPINLOCK_MAGIC,		\
-	.owner_cpu = -1,			\
-	.owner = SPINLOCK_OWNER_INIT,
-#else
-# define SPIN_DEBUG_INIT(lockname)
-#endif
-
-#define __RAW_SPIN_LOCK_INITIALIZER(lockname)	\
-	{					\
-	.raw_lock = __ARCH_SPIN_LOCK_UNLOCKED,	\
-	SPIN_DEBUG_INIT(lockname)		\
-	SPIN_DEP_MAP_INIT(lockname) }
-
-#define __RAW_SPIN_LOCK_UNLOCKED(lockname)	\
-	(raw_spinlock_t) __RAW_SPIN_LOCK_INITIALIZER(lockname)
-
-#define DEFINE_RAW_SPINLOCK(x)	raw_spinlock_t x = __RAW_SPIN_LOCK_UNLOCKED(x)
-
-typedef struct spinlock {
-	union {
-		struct raw_spinlock rlock;
-
-#ifdef CONFIG_DEBUG_LOCK_ALLOC
-# define LOCK_PADSIZE (offsetof(struct raw_spinlock, dep_map))
-		struct {
-			u8 __padding[LOCK_PADSIZE];
-			struct lockdep_map dep_map;
-		};
-#endif
-	};
-} spinlock_t;
-
-#define __SPIN_LOCK_INITIALIZER(lockname) \
-	{ { .rlock = __RAW_SPIN_LOCK_INITIALIZER(lockname) } }
-
-#define __SPIN_LOCK_UNLOCKED(lockname) \
-	(spinlock_t ) __SPIN_LOCK_INITIALIZER(lockname)
-
-#define DEFINE_SPINLOCK(x)	spinlock_t x = __SPIN_LOCK_UNLOCKED(x)
-
-#include <linux/rwlock_types.h>
-
 #endif /* __LINUX_SPINLOCK_TYPES_H */
diff --git a/include/linux/spinlock_types_nort.h b/include/linux/spinlock_types_nort.h
new file mode 100644
index 000000000000..f1dac1fb1d6a
--- /dev/null
+++ b/include/linux/spinlock_types_nort.h
@@ -0,0 +1,33 @@
+#ifndef __LINUX_SPINLOCK_TYPES_NORT_H
+#define __LINUX_SPINLOCK_TYPES_NORT_H
+
+#ifndef __LINUX_SPINLOCK_TYPES_H
+#error "Do not include directly. Include spinlock_types.h instead"
+#endif
+
+/*
+ * The non RT version maps spinlocks to raw_spinlocks
+ */
+typedef struct spinlock {
+	union {
+		struct raw_spinlock rlock;
+
+#ifdef CONFIG_DEBUG_LOCK_ALLOC
+# define LOCK_PADSIZE (offsetof(struct raw_spinlock, dep_map))
+		struct {
+			u8 __padding[LOCK_PADSIZE];
+			struct lockdep_map dep_map;
+		};
+#endif
+	};
+} spinlock_t;
+
+#define __SPIN_LOCK_INITIALIZER(lockname) \
+	{ { .rlock = __RAW_SPIN_LOCK_INITIALIZER(lockname) } }
+
+#define __SPIN_LOCK_UNLOCKED(lockname) \
+	(spinlock_t ) __SPIN_LOCK_INITIALIZER(lockname)
+
+#define DEFINE_SPINLOCK(x)	spinlock_t x = __SPIN_LOCK_UNLOCKED(x)
+
+#endif
diff --git a/include/linux/spinlock_types_raw.h b/include/linux/spinlock_types_raw.h
new file mode 100644
index 000000000000..edffc4d53fc9
--- /dev/null
+++ b/include/linux/spinlock_types_raw.h
@@ -0,0 +1,56 @@
+#ifndef __LINUX_SPINLOCK_TYPES_RAW_H
+#define __LINUX_SPINLOCK_TYPES_RAW_H
+
+#if defined(CONFIG_SMP)
+# include <asm/spinlock_types.h>
+#else
+# include <linux/spinlock_types_up.h>
+#endif
+
+#include <linux/lockdep.h>
+
+typedef struct raw_spinlock {
+	arch_spinlock_t raw_lock;
+#ifdef CONFIG_GENERIC_LOCKBREAK
+	unsigned int break_lock;
+#endif
+#ifdef CONFIG_DEBUG_SPINLOCK
+	unsigned int magic, owner_cpu;
+	void *owner;
+#endif
+#ifdef CONFIG_DEBUG_LOCK_ALLOC
+	struct lockdep_map dep_map;
+#endif
+} raw_spinlock_t;
+
+#define SPINLOCK_MAGIC		0xdead4ead
+
+#define SPINLOCK_OWNER_INIT	((void *)-1L)
+
+#ifdef CONFIG_DEBUG_LOCK_ALLOC
+# define SPIN_DEP_MAP_INIT(lockname)	.dep_map = { .name = #lockname }
+#else
+# define SPIN_DEP_MAP_INIT(lockname)
+#endif
+
+#ifdef CONFIG_DEBUG_SPINLOCK
+# define SPIN_DEBUG_INIT(lockname)		\
+	.magic = SPINLOCK_MAGIC,		\
+	.owner_cpu = -1,			\
+	.owner = SPINLOCK_OWNER_INIT,
+#else
+# define SPIN_DEBUG_INIT(lockname)
+#endif
+
+#define __RAW_SPIN_LOCK_INITIALIZER(lockname)	\
+	{					\
+	.raw_lock = __ARCH_SPIN_LOCK_UNLOCKED,	\
+	SPIN_DEBUG_INIT(lockname)		\
+	SPIN_DEP_MAP_INIT(lockname) }
+
+#define __RAW_SPIN_LOCK_UNLOCKED(lockname)	\
+	(raw_spinlock_t) __RAW_SPIN_LOCK_INITIALIZER(lockname)
+
+#define DEFINE_RAW_SPINLOCK(x)	raw_spinlock_t x = __RAW_SPIN_LOCK_UNLOCKED(x)
+
+#endif
diff --git a/include/linux/spinlock_types_rt.h b/include/linux/spinlock_types_rt.h
new file mode 100644
index 000000000000..9fd431967abc
--- /dev/null
+++ b/include/linux/spinlock_types_rt.h
@@ -0,0 +1,51 @@
+#ifndef __LINUX_SPINLOCK_TYPES_RT_H
+#define __LINUX_SPINLOCK_TYPES_RT_H
+
+#ifndef __LINUX_SPINLOCK_TYPES_H
+#error "Do not include directly. Include spinlock_types.h instead"
+#endif
+
+#include <linux/cache.h>
+
+/*
+ * PREEMPT_RT: spinlocks - an RT mutex plus lock-break field:
+ */
+typedef struct spinlock {
+	struct rt_mutex		lock;
+	unsigned int		break_lock;
+#ifdef CONFIG_DEBUG_LOCK_ALLOC
+	struct lockdep_map	dep_map;
+#endif
+} spinlock_t;
+
+#ifdef CONFIG_DEBUG_RT_MUTEXES
+# define __RT_SPIN_INITIALIZER(name) \
+	{ \
+	.wait_lock = __RAW_SPIN_LOCK_UNLOCKED(name.wait_lock), \
+	.save_state = 1, \
+	.file = __FILE__, \
+	.line = __LINE__ , \
+	}
+#else
+# define __RT_SPIN_INITIALIZER(name) \
+	{								\
+	.wait_lock = __RAW_SPIN_LOCK_UNLOCKED(name.wait_lock),		\
+	.save_state = 1, \
+	}
+#endif
+
+/*
+.wait_list = PLIST_HEAD_INIT_RAW((name).lock.wait_list, (name).lock.wait_lock)
+*/
+
+#define __SPIN_LOCK_UNLOCKED(name)			\
+	{ .lock = __RT_SPIN_INITIALIZER(name.lock),		\
+	  SPIN_DEP_MAP_INIT(name) }
+
+#define __DEFINE_SPINLOCK(name) \
+	spinlock_t name = __SPIN_LOCK_UNLOCKED(name)
+
+#define DEFINE_SPINLOCK(name) \
+	spinlock_t name __cacheline_aligned_in_smp = __SPIN_LOCK_UNLOCKED(name)
+
+#endif
diff --git a/include/linux/srcu.h b/include/linux/srcu.h
index f5f80c5643ac..ec1a8f01563c 100644
--- a/include/linux/srcu.h
+++ b/include/linux/srcu.h
@@ -84,10 +84,10 @@ int init_srcu_struct(struct srcu_struct *sp);
 
 void process_srcu(struct work_struct *work);
 
-#define __SRCU_STRUCT_INIT(name)					\
+#define __SRCU_STRUCT_INIT(name, pcpu_name)				\
 	{								\
 		.completed = -300,					\
-		.per_cpu_ref = &name##_srcu_array,			\
+		.per_cpu_ref = &pcpu_name,				\
 		.queue_lock = __SPIN_LOCK_UNLOCKED(name.queue_lock),	\
 		.running = false,					\
 		.batch_queue = RCU_BATCH_INIT(name.batch_queue),	\
@@ -104,7 +104,7 @@ void process_srcu(struct work_struct *work);
  */
 #define __DEFINE_SRCU(name, is_static)					\
 	static DEFINE_PER_CPU(struct srcu_struct_array, name##_srcu_array);\
-	is_static struct srcu_struct name = __SRCU_STRUCT_INIT(name)
+	is_static struct srcu_struct name = __SRCU_STRUCT_INIT(name, name##_srcu_array)
 #define DEFINE_SRCU(name)		__DEFINE_SRCU(name, /* not static */)
 #define DEFINE_STATIC_SRCU(name)	__DEFINE_SRCU(name, static)
 
diff --git a/include/linux/swap.h b/include/linux/swap.h
index 7ba7dccaf0e7..da646f2eb3c6 100644
--- a/include/linux/swap.h
+++ b/include/linux/swap.h
@@ -11,6 +11,7 @@
 #include <linux/fs.h>
 #include <linux/atomic.h>
 #include <linux/page-flags.h>
+#include <linux/locallock.h>
 #include <asm/page.h>
 
 struct notifier_block;
@@ -252,7 +253,8 @@ struct swap_info_struct {
 void *workingset_eviction(struct address_space *mapping, struct page *page);
 bool workingset_refault(void *shadow);
 void workingset_activation(struct page *page);
-extern struct list_lru workingset_shadow_nodes;
+extern struct list_lru __workingset_shadow_nodes;
+DECLARE_LOCAL_IRQ_LOCK(workingset_shadow_lock);
 
 static inline unsigned int workingset_node_pages(struct radix_tree_node *node)
 {
@@ -296,6 +298,7 @@ extern unsigned long nr_free_pagecache_pages(void);
 
 
 /* linux/mm/swap.c */
+DECLARE_LOCAL_IRQ_LOCK(swapvec_lock);
 extern void lru_cache_add(struct page *);
 extern void lru_cache_add_anon(struct page *page);
 extern void lru_cache_add_file(struct page *page);
diff --git a/include/linux/thread_info.h b/include/linux/thread_info.h
index ff307b548ed3..be9f9dc6a4e1 100644
--- a/include/linux/thread_info.h
+++ b/include/linux/thread_info.h
@@ -102,7 +102,17 @@ static inline int test_ti_thread_flag(struct thread_info *ti, int flag)
 #define test_thread_flag(flag) \
 	test_ti_thread_flag(current_thread_info(), flag)
 
-#define tif_need_resched() test_thread_flag(TIF_NEED_RESCHED)
+#ifdef CONFIG_PREEMPT_LAZY
+#define tif_need_resched()	(test_thread_flag(TIF_NEED_RESCHED) || \
+				 test_thread_flag(TIF_NEED_RESCHED_LAZY))
+#define tif_need_resched_now()	(test_thread_flag(TIF_NEED_RESCHED))
+#define tif_need_resched_lazy()	test_thread_flag(TIF_NEED_RESCHED_LAZY))
+
+#else
+#define tif_need_resched()	test_thread_flag(TIF_NEED_RESCHED)
+#define tif_need_resched_now()	test_thread_flag(TIF_NEED_RESCHED)
+#define tif_need_resched_lazy()	0
+#endif
 
 #if defined TIF_RESTORE_SIGMASK && !defined HAVE_SET_RESTORE_SIGMASK
 /*
diff --git a/include/linux/timer.h b/include/linux/timer.h
index 61aa61dc410c..299d2b78591f 100644
--- a/include/linux/timer.h
+++ b/include/linux/timer.h
@@ -225,7 +225,7 @@ extern void add_timer(struct timer_list *timer);
 
 extern int try_to_del_timer_sync(struct timer_list *timer);
 
-#ifdef CONFIG_SMP
+#if defined(CONFIG_SMP) || defined(CONFIG_PREEMPT_RT_FULL)
   extern int del_timer_sync(struct timer_list *timer);
 #else
 # define del_timer_sync(t)		del_timer(t)
diff --git a/include/linux/trace_events.h b/include/linux/trace_events.h
index 429fdfc3baf5..0a4578dcbf68 100644
--- a/include/linux/trace_events.h
+++ b/include/linux/trace_events.h
@@ -66,6 +66,9 @@ struct trace_entry {
 	unsigned char		flags;
 	unsigned char		preempt_count;
 	int			pid;
+	unsigned short		migrate_disable;
+	unsigned short		padding;
+	unsigned char		preempt_lazy_count;
 };
 
 #define TRACE_EVENT_TYPE_MAX						\
diff --git a/include/linux/uaccess.h b/include/linux/uaccess.h
index 558129af828a..cf5c472bbc79 100644
--- a/include/linux/uaccess.h
+++ b/include/linux/uaccess.h
@@ -24,6 +24,7 @@ static __always_inline void pagefault_disabled_dec(void)
  */
 static inline void pagefault_disable(void)
 {
+	migrate_disable();
 	pagefault_disabled_inc();
 	/*
 	 * make sure to have issued the store before a pagefault
@@ -40,6 +41,7 @@ static inline void pagefault_enable(void)
 	 */
 	barrier();
 	pagefault_disabled_dec();
+	migrate_enable();
 }
 
 /*
diff --git a/include/linux/uprobes.h b/include/linux/uprobes.h
index 4a29c75b146e..0a294e950df8 100644
--- a/include/linux/uprobes.h
+++ b/include/linux/uprobes.h
@@ -27,6 +27,7 @@
 #include <linux/errno.h>
 #include <linux/rbtree.h>
 #include <linux/types.h>
+#include <linux/wait.h>
 
 struct vm_area_struct;
 struct mm_struct;
diff --git a/include/linux/vmstat.h b/include/linux/vmstat.h
index 3e5d9075960f..7eaa847cd5a5 100644
--- a/include/linux/vmstat.h
+++ b/include/linux/vmstat.h
@@ -33,7 +33,9 @@ DECLARE_PER_CPU(struct vm_event_state, vm_event_states);
  */
 static inline void __count_vm_event(enum vm_event_item item)
 {
+	preempt_disable_rt();
 	raw_cpu_inc(vm_event_states.event[item]);
+	preempt_enable_rt();
 }
 
 static inline void count_vm_event(enum vm_event_item item)
@@ -43,7 +45,9 @@ static inline void count_vm_event(enum vm_event_item item)
 
 static inline void __count_vm_events(enum vm_event_item item, long delta)
 {
+	preempt_disable_rt();
 	raw_cpu_add(vm_event_states.event[item], delta);
+	preempt_enable_rt();
 }
 
 static inline void count_vm_events(enum vm_event_item item, long delta)
diff --git a/include/linux/wait-simple.h b/include/linux/wait-simple.h
new file mode 100644
index 000000000000..f86bca2c41d5
--- /dev/null
+++ b/include/linux/wait-simple.h
@@ -0,0 +1,207 @@
+#ifndef _LINUX_WAIT_SIMPLE_H
+#define _LINUX_WAIT_SIMPLE_H
+
+#include <linux/spinlock.h>
+#include <linux/list.h>
+
+#include <asm/current.h>
+
+struct swaiter {
+	struct task_struct	*task;
+	struct list_head	node;
+};
+
+#define DEFINE_SWAITER(name)					\
+	struct swaiter name = {					\
+		.task	= current,				\
+		.node	= LIST_HEAD_INIT((name).node),		\
+	}
+
+struct swait_head {
+	raw_spinlock_t		lock;
+	struct list_head	list;
+};
+
+#define SWAIT_HEAD_INITIALIZER(name) {				\
+		.lock	= __RAW_SPIN_LOCK_UNLOCKED(name.lock),	\
+		.list	= LIST_HEAD_INIT((name).list),		\
+	}
+
+#define DEFINE_SWAIT_HEAD(name)					\
+	struct swait_head name = SWAIT_HEAD_INITIALIZER(name)
+
+extern void __init_swait_head(struct swait_head *h, struct lock_class_key *key);
+
+#define init_swait_head(swh)					\
+	do {							\
+		static struct lock_class_key __key;		\
+								\
+		__init_swait_head((swh), &__key);		\
+	} while (0)
+
+/*
+ * Waiter functions
+ */
+extern void swait_prepare_locked(struct swait_head *head, struct swaiter *w);
+extern void swait_prepare(struct swait_head *head, struct swaiter *w, int state);
+extern void swait_finish_locked(struct swait_head *head, struct swaiter *w);
+extern void swait_finish(struct swait_head *head, struct swaiter *w);
+
+/* Check whether a head has waiters enqueued */
+static inline bool swaitqueue_active(struct swait_head *h)
+{
+	/* Make sure the condition is visible before checking list_empty() */
+	smp_mb();
+	return !list_empty(&h->list);
+}
+
+/*
+ * Wakeup functions
+ */
+extern unsigned int __swait_wake(struct swait_head *head, unsigned int state, unsigned int num);
+extern unsigned int __swait_wake_locked(struct swait_head *head, unsigned int state, unsigned int num);
+
+#define swait_wake(head)			__swait_wake(head, TASK_NORMAL, 1)
+#define swait_wake_interruptible(head)		__swait_wake(head, TASK_INTERRUPTIBLE, 1)
+#define swait_wake_all(head)			__swait_wake(head, TASK_NORMAL, 0)
+#define swait_wake_all_interruptible(head)	__swait_wake(head, TASK_INTERRUPTIBLE, 0)
+
+/*
+ * Event API
+ */
+#define __swait_event(wq, condition)					\
+do {									\
+	DEFINE_SWAITER(__wait);						\
+									\
+	for (;;) {							\
+		swait_prepare(&wq, &__wait, TASK_UNINTERRUPTIBLE);	\
+		if (condition)						\
+			break;						\
+		schedule();						\
+	}								\
+	swait_finish(&wq, &__wait);					\
+} while (0)
+
+/**
+ * swait_event - sleep until a condition gets true
+ * @wq: the waitqueue to wait on
+ * @condition: a C expression for the event to wait for
+ *
+ * The process is put to sleep (TASK_UNINTERRUPTIBLE) until the
+ * @condition evaluates to true. The @condition is checked each time
+ * the waitqueue @wq is woken up.
+ *
+ * wake_up() has to be called after changing any variable that could
+ * change the result of the wait condition.
+ */
+#define swait_event(wq, condition)					\
+do {									\
+	if (condition)							\
+		break;							\
+	__swait_event(wq, condition);					\
+} while (0)
+
+#define __swait_event_interruptible(wq, condition, ret)			\
+do {									\
+	DEFINE_SWAITER(__wait);						\
+									\
+	for (;;) {							\
+		swait_prepare(&wq, &__wait, TASK_INTERRUPTIBLE);	\
+		if (condition)						\
+			break;						\
+		if (signal_pending(current)) {				\
+			ret = -ERESTARTSYS;				\
+			break;						\
+		}							\
+		schedule();						\
+	}								\
+	swait_finish(&wq, &__wait);					\
+} while (0)
+
+#define __swait_event_interruptible_timeout(wq, condition, ret)		\
+do {									\
+	DEFINE_SWAITER(__wait);						\
+									\
+	for (;;) {							\
+		swait_prepare(&wq, &__wait, TASK_INTERRUPTIBLE);	\
+		if (condition)						\
+			break;						\
+		if (signal_pending(current)) {				\
+			ret = -ERESTARTSYS;				\
+			break;						\
+		}							\
+		ret = schedule_timeout(ret);				\
+		if (!ret)						\
+			break;						\
+	}								\
+	swait_finish(&wq, &__wait);					\
+} while (0)
+
+/**
+ * swait_event_interruptible - sleep until a condition gets true
+ * @wq: the waitqueue to wait on
+ * @condition: a C expression for the event to wait for
+ *
+ * The process is put to sleep (TASK_INTERRUPTIBLE) until the
+ * @condition evaluates to true. The @condition is checked each time
+ * the waitqueue @wq is woken up.
+ *
+ * wake_up() has to be called after changing any variable that could
+ * change the result of the wait condition.
+ */
+#define swait_event_interruptible(wq, condition)			\
+({									\
+	int __ret = 0;							\
+	if (!(condition))						\
+		__swait_event_interruptible(wq, condition, __ret);	\
+	__ret;								\
+})
+
+#define swait_event_interruptible_timeout(wq, condition, timeout)	\
+({									\
+	int __ret = timeout;						\
+	if (!(condition))						\
+		__swait_event_interruptible_timeout(wq, condition, __ret);	\
+	__ret;								\
+})
+
+#define __swait_event_timeout(wq, condition, ret)			\
+do {									\
+	DEFINE_SWAITER(__wait);						\
+									\
+	for (;;) {							\
+		swait_prepare(&wq, &__wait, TASK_UNINTERRUPTIBLE);	\
+		if (condition)						\
+			break;						\
+		ret = schedule_timeout(ret);				\
+		if (!ret)						\
+			break;						\
+	}								\
+	swait_finish(&wq, &__wait);					\
+} while (0)
+
+/**
+ * swait_event_timeout - sleep until a condition gets true or a timeout elapses
+ * @wq: the waitqueue to wait on
+ * @condition: a C expression for the event to wait for
+ * @timeout: timeout, in jiffies
+ *
+ * The process is put to sleep (TASK_UNINTERRUPTIBLE) until the
+ * @condition evaluates to true. The @condition is checked each time
+ * the waitqueue @wq is woken up.
+ *
+ * wake_up() has to be called after changing any variable that could
+ * change the result of the wait condition.
+ *
+ * The function returns 0 if the @timeout elapsed, and the remaining
+ * jiffies if the condition evaluated to true before the timeout elapsed.
+ */
+#define swait_event_timeout(wq, condition, timeout)			\
+({									\
+	long __ret = timeout;						\
+	if (!(condition))						\
+		__swait_event_timeout(wq, condition, __ret);		\
+	__ret;								\
+})
+
+#endif
diff --git a/include/linux/wait.h b/include/linux/wait.h
index 513b36f04dfd..981c8a840f96 100644
--- a/include/linux/wait.h
+++ b/include/linux/wait.h
@@ -8,6 +8,7 @@
 #include <linux/spinlock.h>
 #include <asm/current.h>
 #include <uapi/linux/wait.h>
+#include <linux/atomic.h>
 
 typedef struct __wait_queue wait_queue_t;
 typedef int (*wait_queue_func_t)(wait_queue_t *wait, unsigned mode, int flags, void *key);
diff --git a/include/linux/work-simple.h b/include/linux/work-simple.h
new file mode 100644
index 000000000000..f175fa9a6016
--- /dev/null
+++ b/include/linux/work-simple.h
@@ -0,0 +1,24 @@
+#ifndef _LINUX_SWORK_H
+#define _LINUX_SWORK_H
+
+#include <linux/list.h>
+
+struct swork_event {
+	struct list_head item;
+	unsigned long flags;
+	void (*func)(struct swork_event *);
+};
+
+static inline void INIT_SWORK(struct swork_event *event,
+			      void (*func)(struct swork_event *))
+{
+	event->flags = 0;
+	event->func = func;
+}
+
+bool swork_queue(struct swork_event *sev);
+
+int swork_get(void);
+void swork_put(void);
+
+#endif /* _LINUX_SWORK_H */
diff --git a/include/net/dst.h b/include/net/dst.h
index c7329dcd90cc..35c3dba16728 100644
--- a/include/net/dst.h
+++ b/include/net/dst.h
@@ -437,7 +437,7 @@ static inline void dst_confirm(struct dst_entry *dst)
 static inline int dst_neigh_output(struct dst_entry *dst, struct neighbour *n,
 				   struct sk_buff *skb)
 {
-	const struct hh_cache *hh;
+	struct hh_cache *hh;
 
 	if (dst->pending_confirm) {
 		unsigned long now = jiffies;
diff --git a/include/net/neighbour.h b/include/net/neighbour.h
index 8b683841e574..bf656008f6e7 100644
--- a/include/net/neighbour.h
+++ b/include/net/neighbour.h
@@ -446,7 +446,7 @@ static inline int neigh_hh_bridge(struct hh_cache *hh, struct sk_buff *skb)
 }
 #endif
 
-static inline int neigh_hh_output(const struct hh_cache *hh, struct sk_buff *skb)
+static inline int neigh_hh_output(struct hh_cache *hh, struct sk_buff *skb)
 {
 	unsigned int seq;
 	int hh_len;
@@ -501,7 +501,7 @@ struct neighbour_cb {
 
 #define NEIGH_CB(skb)	((struct neighbour_cb *)(skb)->cb)
 
-static inline void neigh_ha_snapshot(char *dst, const struct neighbour *n,
+static inline void neigh_ha_snapshot(char *dst, struct neighbour *n,
 				     const struct net_device *dev)
 {
 	unsigned int seq;
diff --git a/include/net/netns/ipv4.h b/include/net/netns/ipv4.h
index c68926b4899c..dd0751e76065 100644
--- a/include/net/netns/ipv4.h
+++ b/include/net/netns/ipv4.h
@@ -70,6 +70,7 @@ struct netns_ipv4 {
 
 	int sysctl_icmp_echo_ignore_all;
 	int sysctl_icmp_echo_ignore_broadcasts;
+	int sysctl_icmp_echo_sysrq;
 	int sysctl_icmp_ignore_bogus_error_responses;
 	int sysctl_icmp_ratelimit;
 	int sysctl_icmp_ratemask;
diff --git a/include/trace/events/hist.h b/include/trace/events/hist.h
new file mode 100644
index 000000000000..6122e4286177
--- /dev/null
+++ b/include/trace/events/hist.h
@@ -0,0 +1,72 @@
+#undef TRACE_SYSTEM
+#define TRACE_SYSTEM hist
+
+#if !defined(_TRACE_HIST_H) || defined(TRACE_HEADER_MULTI_READ)
+#define _TRACE_HIST_H
+
+#include "latency_hist.h"
+#include <linux/tracepoint.h>
+
+#if !defined(CONFIG_PREEMPT_OFF_HIST) && !defined(CONFIG_INTERRUPT_OFF_HIST)
+#define trace_preemptirqsoff_hist(a, b)
+#else
+TRACE_EVENT(preemptirqsoff_hist,
+
+	TP_PROTO(int reason, int starthist),
+
+	TP_ARGS(reason, starthist),
+
+	TP_STRUCT__entry(
+		__field(int,	reason)
+		__field(int,	starthist)
+	),
+
+	TP_fast_assign(
+		__entry->reason		= reason;
+		__entry->starthist	= starthist;
+	),
+
+	TP_printk("reason=%s starthist=%s", getaction(__entry->reason),
+		  __entry->starthist ? "start" : "stop")
+);
+#endif
+
+#ifndef CONFIG_MISSED_TIMER_OFFSETS_HIST
+#define trace_hrtimer_interrupt(a, b, c, d)
+#else
+TRACE_EVENT(hrtimer_interrupt,
+
+	TP_PROTO(int cpu, long long offset, struct task_struct *curr,
+		struct task_struct *task),
+
+	TP_ARGS(cpu, offset, curr, task),
+
+	TP_STRUCT__entry(
+		__field(int,		cpu)
+		__field(long long,	offset)
+		__array(char,		ccomm,	TASK_COMM_LEN)
+		__field(int,		cprio)
+		__array(char,		tcomm,	TASK_COMM_LEN)
+		__field(int,		tprio)
+	),
+
+	TP_fast_assign(
+		__entry->cpu	= cpu;
+		__entry->offset	= offset;
+		memcpy(__entry->ccomm, curr->comm, TASK_COMM_LEN);
+		__entry->cprio  = curr->prio;
+		memcpy(__entry->tcomm, task != NULL ? task->comm : "<none>",
+			task != NULL ? TASK_COMM_LEN : 7);
+		__entry->tprio  = task != NULL ? task->prio : -1;
+	),
+
+	TP_printk("cpu=%d offset=%lld curr=%s[%d] thread=%s[%d]",
+		__entry->cpu, __entry->offset, __entry->ccomm,
+		__entry->cprio, __entry->tcomm, __entry->tprio)
+);
+#endif
+
+#endif /* _TRACE_HIST_H */
+
+/* This part must be outside protection */
+#include <trace/define_trace.h>
diff --git a/include/trace/events/latency_hist.h b/include/trace/events/latency_hist.h
new file mode 100644
index 000000000000..d3f2fbd560b1
--- /dev/null
+++ b/include/trace/events/latency_hist.h
@@ -0,0 +1,29 @@
+#ifndef _LATENCY_HIST_H
+#define _LATENCY_HIST_H
+
+enum hist_action {
+	IRQS_ON,
+	PREEMPT_ON,
+	TRACE_STOP,
+	IRQS_OFF,
+	PREEMPT_OFF,
+	TRACE_START,
+};
+
+static char *actions[] = {
+	"IRQS_ON",
+	"PREEMPT_ON",
+	"TRACE_STOP",
+	"IRQS_OFF",
+	"PREEMPT_OFF",
+	"TRACE_START",
+};
+
+static inline char *getaction(int action)
+{
+	if (action >= 0 && action <= sizeof(actions)/sizeof(actions[0]))
+		return actions[action];
+	return "unknown";
+}
+
+#endif /* _LATENCY_HIST_H */
diff --git a/init/Kconfig b/init/Kconfig
index 235c7a2c0d20..a7c81c0911da 100644
--- a/init/Kconfig
+++ b/init/Kconfig
@@ -498,7 +498,7 @@ config TINY_RCU
 
 config RCU_EXPERT
 	bool "Make expert-level adjustments to RCU configuration"
-	default n
+	default y if PREEMPT_RT_FULL
 	help
 	  This option needs to be enabled if you wish to make
 	  expert-level adjustments to RCU configuration.  By default,
@@ -614,7 +614,7 @@ config RCU_FANOUT_LEAF
 
 config RCU_FAST_NO_HZ
 	bool "Accelerate last non-dyntick-idle CPU's grace periods"
-	depends on NO_HZ_COMMON && SMP && RCU_EXPERT
+	depends on NO_HZ_COMMON && SMP && RCU_EXPERT && !PREEMPT_RT_FULL
 	default n
 	help
 	  This option permits CPUs to enter dynticks-idle state even if
@@ -641,7 +641,7 @@ config TREE_RCU_TRACE
 config RCU_BOOST
 	bool "Enable RCU priority boosting"
 	depends on RT_MUTEXES && PREEMPT_RCU && RCU_EXPERT
-	default n
+	default y if PREEMPT_RT_FULL
 	help
 	  This option boosts the priority of preempted RCU readers that
 	  block the current preemptible RCU grace period for too long.
@@ -1106,6 +1106,7 @@ config CFS_BANDWIDTH
 config RT_GROUP_SCHED
 	bool "Group scheduling for SCHED_RR/FIFO"
 	depends on CGROUP_SCHED
+	depends on !PREEMPT_RT_FULL
 	default n
 	help
 	  This feature lets you explicitly allocate real CPU bandwidth
@@ -1719,6 +1720,7 @@ choice
 
 config SLAB
 	bool "SLAB"
+	depends on !PREEMPT_RT_FULL
 	help
 	  The regular slab allocator that is established and known to work
 	  well in all environments. It organizes cache hot objects in
@@ -1737,6 +1739,7 @@ config SLUB
 config SLOB
 	depends on EXPERT
 	bool "SLOB (Simple Allocator)"
+	depends on !PREEMPT_RT_FULL
 	help
 	   SLOB replaces the stock allocator with a drastically simpler
 	   allocator. SLOB is generally more space efficient but
@@ -1746,7 +1749,7 @@ endchoice
 
 config SLUB_CPU_PARTIAL
 	default y
-	depends on SLUB && SMP
+	depends on SLUB && SMP && !PREEMPT_RT_FULL
 	bool "SLUB per cpu partial cache"
 	help
 	  Per cpu partial caches accellerate objects allocation and freeing
diff --git a/init/Makefile b/init/Makefile
index 7bc47ee31c36..88cf473554e0 100644
--- a/init/Makefile
+++ b/init/Makefile
@@ -33,4 +33,4 @@ $(obj)/version.o: include/generated/compile.h
 include/generated/compile.h: FORCE
 	@$($(quiet)chk_compile.h)
 	$(Q)$(CONFIG_SHELL) $(srctree)/scripts/mkcompile_h $@ \
-	"$(UTS_MACHINE)" "$(CONFIG_SMP)" "$(CONFIG_PREEMPT)" "$(CC) $(KBUILD_CFLAGS)"
+	"$(UTS_MACHINE)" "$(CONFIG_SMP)" "$(CONFIG_PREEMPT)" "$(CONFIG_PREEMPT_RT_FULL)" "$(CC) $(KBUILD_CFLAGS)"
diff --git a/init/main.c b/init/main.c
index 9e64d7097f1a..4a76e629c137 100644
--- a/init/main.c
+++ b/init/main.c
@@ -530,6 +530,7 @@ asmlinkage __visible void __init start_kernel(void)
 	setup_command_line(command_line);
 	setup_nr_cpu_ids();
 	setup_per_cpu_areas();
+	softirq_early_init();
 	smp_prepare_boot_cpu();	/* arch-specific boot-cpu hooks */
 
 	build_all_zonelists(NULL, NULL);
diff --git a/ipc/msg.c b/ipc/msg.c
index 1471db9a7e61..b8c5e7f2bebc 100644
--- a/ipc/msg.c
+++ b/ipc/msg.c
@@ -183,20 +183,14 @@ static void ss_wakeup(struct list_head *h, int kill)
 	}
 }
 
-static void expunge_all(struct msg_queue *msq, int res)
+static void expunge_all(struct msg_queue *msq, int res,
+			struct wake_q_head *wake_q)
 {
 	struct msg_receiver *msr, *t;
 
 	list_for_each_entry_safe(msr, t, &msq->q_receivers, r_list) {
-		msr->r_msg = NULL; /* initialize expunge ordering */
-		wake_up_process(msr->r_tsk);
-		/*
-		 * Ensure that the wakeup is visible before setting r_msg as
-		 * the receiving end depends on it: either spinning on a nil,
-		 * or dealing with -EAGAIN cases. See lockless receive part 1
-		 * and 2 in do_msgrcv().
-		 */
-		smp_wmb(); /* barrier (B) */
+
+		wake_q_add(wake_q, msr->r_tsk);
 		msr->r_msg = ERR_PTR(res);
 	}
 }
@@ -213,11 +207,13 @@ static void freeque(struct ipc_namespace *ns, struct kern_ipc_perm *ipcp)
 {
 	struct msg_msg *msg, *t;
 	struct msg_queue *msq = container_of(ipcp, struct msg_queue, q_perm);
+	WAKE_Q(wake_q);
 
-	expunge_all(msq, -EIDRM);
+	expunge_all(msq, -EIDRM, &wake_q);
 	ss_wakeup(&msq->q_senders, 1);
 	msg_rmid(ns, msq);
 	ipc_unlock_object(&msq->q_perm);
+	wake_up_q(&wake_q);
 	rcu_read_unlock();
 
 	list_for_each_entry_safe(msg, t, &msq->q_messages, m_list) {
@@ -342,6 +338,7 @@ static int msgctl_down(struct ipc_namespace *ns, int msqid, int cmd,
 	struct kern_ipc_perm *ipcp;
 	struct msqid64_ds uninitialized_var(msqid64);
 	struct msg_queue *msq;
+	WAKE_Q(wake_q);
 	int err;
 
 	if (cmd == IPC_SET) {
@@ -389,7 +386,7 @@ static int msgctl_down(struct ipc_namespace *ns, int msqid, int cmd,
 		/* sleeping receivers might be excluded by
 		 * stricter permissions.
 		 */
-		expunge_all(msq, -EAGAIN);
+		expunge_all(msq, -EAGAIN, &wake_q);
 		/* sleeping senders might be able to send
 		 * due to a larger queue size.
 		 */
@@ -402,6 +399,7 @@ static int msgctl_down(struct ipc_namespace *ns, int msqid, int cmd,
 
 out_unlock0:
 	ipc_unlock_object(&msq->q_perm);
+	wake_up_q(&wake_q);
 out_unlock1:
 	rcu_read_unlock();
 out_up:
@@ -566,7 +564,8 @@ static int testmsg(struct msg_msg *msg, long type, int mode)
 	return 0;
 }
 
-static inline int pipelined_send(struct msg_queue *msq, struct msg_msg *msg)
+static inline int pipelined_send(struct msg_queue *msq, struct msg_msg *msg,
+				 struct wake_q_head *wake_q)
 {
 	struct msg_receiver *msr, *t;
 
@@ -577,27 +576,13 @@ static inline int pipelined_send(struct msg_queue *msq, struct msg_msg *msg)
 
 			list_del(&msr->r_list);
 			if (msr->r_maxsize < msg->m_ts) {
-				/* initialize pipelined send ordering */
-				msr->r_msg = NULL;
-				wake_up_process(msr->r_tsk);
-				/* barrier (B) see barrier comment below */
-				smp_wmb();
+				wake_q_add(wake_q, msr->r_tsk);
 				msr->r_msg = ERR_PTR(-E2BIG);
 			} else {
-				msr->r_msg = NULL;
 				msq->q_lrpid = task_pid_vnr(msr->r_tsk);
 				msq->q_rtime = get_seconds();
-				wake_up_process(msr->r_tsk);
-				/*
-				 * Ensure that the wakeup is visible before
-				 * setting r_msg, as the receiving can otherwise
-				 * exit - once r_msg is set, the receiver can
-				 * continue. See lockless receive part 1 and 2
-				 * in do_msgrcv(). Barrier (B).
-				 */
-				smp_wmb();
+				wake_q_add(wake_q, msr->r_tsk);
 				msr->r_msg = msg;
-
 				return 1;
 			}
 		}
@@ -613,6 +598,7 @@ long do_msgsnd(int msqid, long mtype, void __user *mtext,
 	struct msg_msg *msg;
 	int err;
 	struct ipc_namespace *ns;
+	WAKE_Q(wake_q);
 
 	ns = current->nsproxy->ipc_ns;
 
@@ -698,7 +684,7 @@ long do_msgsnd(int msqid, long mtype, void __user *mtext,
 	msq->q_lspid = task_tgid_vnr(current);
 	msq->q_stime = get_seconds();
 
-	if (!pipelined_send(msq, msg)) {
+	if (!pipelined_send(msq, msg, &wake_q)) {
 		/* no one is waiting for this message, enqueue it */
 		list_add_tail(&msg->m_list, &msq->q_messages);
 		msq->q_cbytes += msgsz;
@@ -712,6 +698,7 @@ long do_msgsnd(int msqid, long mtype, void __user *mtext,
 
 out_unlock0:
 	ipc_unlock_object(&msq->q_perm);
+	wake_up_q(&wake_q);
 out_unlock1:
 	rcu_read_unlock();
 	if (msg != NULL)
@@ -932,57 +919,25 @@ long do_msgrcv(int msqid, void __user *buf, size_t bufsz, long msgtyp, int msgfl
 		rcu_read_lock();
 
 		/* Lockless receive, part 2:
-		 * Wait until pipelined_send or expunge_all are outside of
-		 * wake_up_process(). There is a race with exit(), see
-		 * ipc/mqueue.c for the details. The correct serialization
-		 * ensures that a receiver cannot continue without the wakeup
-		 * being visibible _before_ setting r_msg:
+		 * The work in pipelined_send() and expunge_all():
+		 * - Set pointer to message
+		 * - Queue the receiver task for later wakeup
+		 * - Wake up the process after the lock is dropped.
 		 *
-		 * CPU 0                             CPU 1
-		 * <loop receiver>
-		 *   smp_rmb(); (A) <-- pair -.      <waker thread>
-		 *   <load ->r_msg>           |        msr->r_msg = NULL;
-		 *                            |        wake_up_process();
-		 * <continue>                 `------> smp_wmb(); (B)
-		 *                                     msr->r_msg = msg;
-		 *
-		 * Where (A) orders the message value read and where (B) orders
-		 * the write to the r_msg -- done in both pipelined_send and
-		 * expunge_all.
+		 * Should the process wake up before this wakeup (due to a
+		 * signal) it will either see the message and continue …
 		 */
-		for (;;) {
-			/*
-			 * Pairs with writer barrier in pipelined_send
-			 * or expunge_all.
-			 */
-			smp_rmb(); /* barrier (A) */
-			msg = (struct msg_msg *)msr_d.r_msg;
-			if (msg)
-				break;
 
-			/*
-			 * The cpu_relax() call is a compiler barrier
-			 * which forces everything in this loop to be
-			 * re-loaded.
-			 */
-			cpu_relax();
-		}
-
-		/* Lockless receive, part 3:
-		 * If there is a message or an error then accept it without
-		 * locking.
-		 */
+		msg = (struct msg_msg *)msr_d.r_msg;
 		if (msg != ERR_PTR(-EAGAIN))
 			goto out_unlock1;
 
-		/* Lockless receive, part 3:
-		 * Acquire the queue spinlock.
-		 */
+		 /*
+		  * … or see -EAGAIN, acquire the lock to check the message
+		  * again.
+		  */
 		ipc_lock_object(&msq->q_perm);
 
-		/* Lockless receive, part 4:
-		 * Repeat test after acquiring the spinlock.
-		 */
 		msg = (struct msg_msg *)msr_d.r_msg;
 		if (msg != ERR_PTR(-EAGAIN))
 			goto out_unlock0;
diff --git a/ipc/sem.c b/ipc/sem.c
index b471e5a3863d..dc67f53c0c25 100644
--- a/ipc/sem.c
+++ b/ipc/sem.c
@@ -690,6 +690,13 @@ static int perform_atomic_semop(struct sem_array *sma, struct sem_queue *q)
 static void wake_up_sem_queue_prepare(struct list_head *pt,
 				struct sem_queue *q, int error)
 {
+#ifdef CONFIG_PREEMPT_RT_BASE
+	struct task_struct *p = q->sleeper;
+	get_task_struct(p);
+	q->status = error;
+	wake_up_process(p);
+	put_task_struct(p);
+#else
 	if (list_empty(pt)) {
 		/*
 		 * Hold preempt off so that we don't get preempted and have the
@@ -701,6 +708,7 @@ static void wake_up_sem_queue_prepare(struct list_head *pt,
 	q->pid = error;
 
 	list_add_tail(&q->list, pt);
+#endif
 }
 
 /**
@@ -714,6 +722,7 @@ static void wake_up_sem_queue_prepare(struct list_head *pt,
  */
 static void wake_up_sem_queue_do(struct list_head *pt)
 {
+#ifndef CONFIG_PREEMPT_RT_BASE
 	struct sem_queue *q, *t;
 	int did_something;
 
@@ -726,6 +735,7 @@ static void wake_up_sem_queue_do(struct list_head *pt)
 	}
 	if (did_something)
 		preempt_enable();
+#endif
 }
 
 static void unlink_queue(struct sem_array *sma, struct sem_queue *q)
diff --git a/kernel/Kconfig.locks b/kernel/Kconfig.locks
index ebdb0043203a..b9e6aa7e5aa6 100644
--- a/kernel/Kconfig.locks
+++ b/kernel/Kconfig.locks
@@ -225,11 +225,11 @@ config ARCH_SUPPORTS_ATOMIC_RMW
 
 config MUTEX_SPIN_ON_OWNER
 	def_bool y
-	depends on SMP && !DEBUG_MUTEXES && ARCH_SUPPORTS_ATOMIC_RMW
+	depends on SMP && !DEBUG_MUTEXES && ARCH_SUPPORTS_ATOMIC_RMW && !PREEMPT_RT_FULL
 
 config RWSEM_SPIN_ON_OWNER
        def_bool y
-       depends on SMP && RWSEM_XCHGADD_ALGORITHM && ARCH_SUPPORTS_ATOMIC_RMW
+       depends on SMP && RWSEM_XCHGADD_ALGORITHM && ARCH_SUPPORTS_ATOMIC_RMW && !PREEMPT_RT_FULL
 
 config LOCK_SPIN_ON_OWNER
        def_bool y
diff --git a/kernel/Kconfig.preempt b/kernel/Kconfig.preempt
index 3f9c97419f02..11dbe26a8279 100644
--- a/kernel/Kconfig.preempt
+++ b/kernel/Kconfig.preempt
@@ -1,3 +1,16 @@
+config PREEMPT
+	bool
+	select PREEMPT_COUNT
+
+config PREEMPT_RT_BASE
+	bool
+	select PREEMPT
+
+config HAVE_PREEMPT_LAZY
+	bool
+
+config PREEMPT_LAZY
+	def_bool y if HAVE_PREEMPT_LAZY && PREEMPT_RT_FULL
 
 choice
 	prompt "Preemption Model"
@@ -33,9 +46,9 @@ config PREEMPT_VOLUNTARY
 
 	  Select this if you are building a kernel for a desktop system.
 
-config PREEMPT
+config PREEMPT__LL
 	bool "Preemptible Kernel (Low-Latency Desktop)"
-	select PREEMPT_COUNT
+	select PREEMPT
 	select UNINLINE_SPIN_UNLOCK if !ARCH_INLINE_SPIN_UNLOCK
 	help
 	  This option reduces the latency of the kernel by making
@@ -52,6 +65,22 @@ config PREEMPT
 	  embedded system with latency requirements in the milliseconds
 	  range.
 
+config PREEMPT_RTB
+	bool "Preemptible Kernel (Basic RT)"
+	select PREEMPT_RT_BASE
+	help
+	  This option is basically the same as (Low-Latency Desktop) but
+	  enables changes which are preliminary for the full preemptible
+	  RT kernel.
+
+config PREEMPT_RT_FULL
+	bool "Fully Preemptible Kernel (RT)"
+	depends on IRQ_FORCED_THREADING
+	select PREEMPT_RT_BASE
+	select PREEMPT_RCU
+	help
+	  All and everything
+
 endchoice
 
 config PREEMPT_COUNT
diff --git a/kernel/cgroup.c b/kernel/cgroup.c
index 470f6536b9e8..30996e198d8d 100644
--- a/kernel/cgroup.c
+++ b/kernel/cgroup.c
@@ -4724,10 +4724,10 @@ static void css_free_rcu_fn(struct rcu_head *rcu_head)
 	queue_work(cgroup_destroy_wq, &css->destroy_work);
 }
 
-static void css_release_work_fn(struct work_struct *work)
+static void css_release_work_fn(struct swork_event *sev)
 {
 	struct cgroup_subsys_state *css =
-		container_of(work, struct cgroup_subsys_state, destroy_work);
+		container_of(sev, struct cgroup_subsys_state, destroy_swork);
 	struct cgroup_subsys *ss = css->ss;
 	struct cgroup *cgrp = css->cgroup;
 
@@ -4766,8 +4766,8 @@ static void css_release(struct percpu_ref *ref)
 	struct cgroup_subsys_state *css =
 		container_of(ref, struct cgroup_subsys_state, refcnt);
 
-	INIT_WORK(&css->destroy_work, css_release_work_fn);
-	queue_work(cgroup_destroy_wq, &css->destroy_work);
+	INIT_SWORK(&css->destroy_swork, css_release_work_fn);
+	swork_queue(&css->destroy_swork);
 }
 
 static void init_and_link_css(struct cgroup_subsys_state *css,
@@ -5363,6 +5363,7 @@ static int __init cgroup_wq_init(void)
 	 */
 	cgroup_destroy_wq = alloc_workqueue("cgroup_destroy", 0, 1);
 	BUG_ON(!cgroup_destroy_wq);
+	BUG_ON(swork_get());
 
 	/*
 	 * Used to destroy pidlists and separate to serve as flush domain.
diff --git a/kernel/cpu.c b/kernel/cpu.c
index 85ff5e26e23b..51aafc259546 100644
--- a/kernel/cpu.c
+++ b/kernel/cpu.c
@@ -75,8 +75,8 @@ static struct {
 #endif
 } cpu_hotplug = {
 	.active_writer = NULL,
-	.wq = __WAIT_QUEUE_HEAD_INITIALIZER(cpu_hotplug.wq),
 	.lock = __MUTEX_INITIALIZER(cpu_hotplug.lock),
+	.wq = __WAIT_QUEUE_HEAD_INITIALIZER(cpu_hotplug.wq),
 #ifdef CONFIG_DEBUG_LOCK_ALLOC
 	.dep_map = {.name = "cpu_hotplug.lock" },
 #endif
@@ -89,6 +89,289 @@ static struct {
 #define cpuhp_lock_acquire()      lock_map_acquire(&cpu_hotplug.dep_map)
 #define cpuhp_lock_release()      lock_map_release(&cpu_hotplug.dep_map)
 
+/**
+ * hotplug_pcp	- per cpu hotplug descriptor
+ * @unplug:	set when pin_current_cpu() needs to sync tasks
+ * @sync_tsk:	the task that waits for tasks to finish pinned sections
+ * @refcount:	counter of tasks in pinned sections
+ * @grab_lock:	set when the tasks entering pinned sections should wait
+ * @synced:	notifier for @sync_tsk to tell cpu_down it's finished
+ * @mutex:	the mutex to make tasks wait (used when @grab_lock is true)
+ * @mutex_init:	zero if the mutex hasn't been initialized yet.
+ *
+ * Although @unplug and @sync_tsk may point to the same task, the @unplug
+ * is used as a flag and still exists after @sync_tsk has exited and
+ * @sync_tsk set to NULL.
+ */
+struct hotplug_pcp {
+	struct task_struct *unplug;
+	struct task_struct *sync_tsk;
+	int refcount;
+	int grab_lock;
+	struct completion synced;
+	struct completion unplug_wait;
+#ifdef CONFIG_PREEMPT_RT_FULL
+	/*
+	 * Note, on PREEMPT_RT, the hotplug lock must save the state of
+	 * the task, otherwise the mutex will cause the task to fail
+	 * to sleep when required. (Because it's called from migrate_disable())
+	 *
+	 * The spinlock_t on PREEMPT_RT is a mutex that saves the task's
+	 * state.
+	 */
+	spinlock_t lock;
+#else
+	struct mutex mutex;
+#endif
+	int mutex_init;
+};
+
+#ifdef CONFIG_PREEMPT_RT_FULL
+# define hotplug_lock(hp) rt_spin_lock(&(hp)->lock)
+# define hotplug_unlock(hp) rt_spin_unlock(&(hp)->lock)
+#else
+# define hotplug_lock(hp) mutex_lock(&(hp)->mutex)
+# define hotplug_unlock(hp) mutex_unlock(&(hp)->mutex)
+#endif
+
+static DEFINE_PER_CPU(struct hotplug_pcp, hotplug_pcp);
+
+/**
+ * pin_current_cpu - Prevent the current cpu from being unplugged
+ *
+ * Lightweight version of get_online_cpus() to prevent cpu from being
+ * unplugged when code runs in a migration disabled region.
+ *
+ * Must be called with preemption disabled (preempt_count = 1)!
+ */
+void pin_current_cpu(void)
+{
+	struct hotplug_pcp *hp;
+	int force = 0;
+
+retry:
+	hp = this_cpu_ptr(&hotplug_pcp);
+
+	if (!hp->unplug || hp->refcount || force || preempt_count() > 1 ||
+	    hp->unplug == current) {
+		hp->refcount++;
+		return;
+	}
+	if (hp->grab_lock) {
+		preempt_enable();
+		hotplug_lock(hp);
+		hotplug_unlock(hp);
+	} else {
+		preempt_enable();
+		/*
+		 * Try to push this task off of this CPU.
+		 */
+		if (!migrate_me()) {
+			preempt_disable();
+			hp = this_cpu_ptr(&hotplug_pcp);
+			if (!hp->grab_lock) {
+				/*
+				 * Just let it continue it's already pinned
+				 * or about to sleep.
+				 */
+				force = 1;
+				goto retry;
+			}
+			preempt_enable();
+		}
+	}
+	preempt_disable();
+	goto retry;
+}
+
+/**
+ * unpin_current_cpu - Allow unplug of current cpu
+ *
+ * Must be called with preemption or interrupts disabled!
+ */
+void unpin_current_cpu(void)
+{
+	struct hotplug_pcp *hp = this_cpu_ptr(&hotplug_pcp);
+
+	WARN_ON(hp->refcount <= 0);
+
+	/* This is safe. sync_unplug_thread is pinned to this cpu */
+	if (!--hp->refcount && hp->unplug && hp->unplug != current)
+		wake_up_process(hp->unplug);
+}
+
+static void wait_for_pinned_cpus(struct hotplug_pcp *hp)
+{
+	set_current_state(TASK_UNINTERRUPTIBLE);
+	while (hp->refcount) {
+		schedule_preempt_disabled();
+		set_current_state(TASK_UNINTERRUPTIBLE);
+	}
+}
+
+static int sync_unplug_thread(void *data)
+{
+	struct hotplug_pcp *hp = data;
+
+	wait_for_completion(&hp->unplug_wait);
+	preempt_disable();
+	hp->unplug = current;
+	wait_for_pinned_cpus(hp);
+
+	/*
+	 * This thread will synchronize the cpu_down() with threads
+	 * that have pinned the CPU. When the pinned CPU count reaches
+	 * zero, we inform the cpu_down code to continue to the next step.
+	 */
+	set_current_state(TASK_UNINTERRUPTIBLE);
+	preempt_enable();
+	complete(&hp->synced);
+
+	/*
+	 * If all succeeds, the next step will need tasks to wait till
+	 * the CPU is offline before continuing. To do this, the grab_lock
+	 * is set and tasks going into pin_current_cpu() will block on the
+	 * mutex. But we still need to wait for those that are already in
+	 * pinned CPU sections. If the cpu_down() failed, the kthread_should_stop()
+	 * will kick this thread out.
+	 */
+	while (!hp->grab_lock && !kthread_should_stop()) {
+		schedule();
+		set_current_state(TASK_UNINTERRUPTIBLE);
+	}
+
+	/* Make sure grab_lock is seen before we see a stale completion */
+	smp_mb();
+
+	/*
+	 * Now just before cpu_down() enters stop machine, we need to make
+	 * sure all tasks that are in pinned CPU sections are out, and new
+	 * tasks will now grab the lock, keeping them from entering pinned
+	 * CPU sections.
+	 */
+	if (!kthread_should_stop()) {
+		preempt_disable();
+		wait_for_pinned_cpus(hp);
+		preempt_enable();
+		complete(&hp->synced);
+	}
+
+	set_current_state(TASK_UNINTERRUPTIBLE);
+	while (!kthread_should_stop()) {
+		schedule();
+		set_current_state(TASK_UNINTERRUPTIBLE);
+	}
+	set_current_state(TASK_RUNNING);
+
+	/*
+	 * Force this thread off this CPU as it's going down and
+	 * we don't want any more work on this CPU.
+	 */
+	current->flags &= ~PF_NO_SETAFFINITY;
+	set_cpus_allowed_ptr(current, cpu_present_mask);
+	migrate_me();
+	return 0;
+}
+
+static void __cpu_unplug_sync(struct hotplug_pcp *hp)
+{
+	wake_up_process(hp->sync_tsk);
+	wait_for_completion(&hp->synced);
+}
+
+static void __cpu_unplug_wait(unsigned int cpu)
+{
+	struct hotplug_pcp *hp = &per_cpu(hotplug_pcp, cpu);
+
+	complete(&hp->unplug_wait);
+	wait_for_completion(&hp->synced);
+}
+
+/*
+ * Start the sync_unplug_thread on the target cpu and wait for it to
+ * complete.
+ */
+static int cpu_unplug_begin(unsigned int cpu)
+{
+	struct hotplug_pcp *hp = &per_cpu(hotplug_pcp, cpu);
+	int err;
+
+	/* Protected by cpu_hotplug.lock */
+	if (!hp->mutex_init) {
+#ifdef CONFIG_PREEMPT_RT_FULL
+		spin_lock_init(&hp->lock);
+#else
+		mutex_init(&hp->mutex);
+#endif
+		hp->mutex_init = 1;
+	}
+
+	/* Inform the scheduler to migrate tasks off this CPU */
+	tell_sched_cpu_down_begin(cpu);
+
+	init_completion(&hp->synced);
+	init_completion(&hp->unplug_wait);
+
+	hp->sync_tsk = kthread_create(sync_unplug_thread, hp, "sync_unplug/%d", cpu);
+	if (IS_ERR(hp->sync_tsk)) {
+		err = PTR_ERR(hp->sync_tsk);
+		hp->sync_tsk = NULL;
+		return err;
+	}
+	kthread_bind(hp->sync_tsk, cpu);
+
+	/*
+	 * Wait for tasks to get out of the pinned sections,
+	 * it's still OK if new tasks enter. Some CPU notifiers will
+	 * wait for tasks that are going to enter these sections and
+	 * we must not have them block.
+	 */
+	wake_up_process(hp->sync_tsk);
+	return 0;
+}
+
+static void cpu_unplug_sync(unsigned int cpu)
+{
+	struct hotplug_pcp *hp = &per_cpu(hotplug_pcp, cpu);
+
+	init_completion(&hp->synced);
+	/* The completion needs to be initialzied before setting grab_lock */
+	smp_wmb();
+
+	/* Grab the mutex before setting grab_lock */
+	hotplug_lock(hp);
+	hp->grab_lock = 1;
+
+	/*
+	 * The CPU notifiers have been completed.
+	 * Wait for tasks to get out of pinned CPU sections and have new
+	 * tasks block until the CPU is completely down.
+	 */
+	__cpu_unplug_sync(hp);
+
+	/* All done with the sync thread */
+	kthread_stop(hp->sync_tsk);
+	hp->sync_tsk = NULL;
+}
+
+static void cpu_unplug_done(unsigned int cpu)
+{
+	struct hotplug_pcp *hp = &per_cpu(hotplug_pcp, cpu);
+
+	hp->unplug = NULL;
+	/* Let all tasks know cpu unplug is finished before cleaning up */
+	smp_wmb();
+
+	if (hp->sync_tsk)
+		kthread_stop(hp->sync_tsk);
+
+	if (hp->grab_lock) {
+		hotplug_unlock(hp);
+		/* protected by cpu_hotplug.lock */
+		hp->grab_lock = 0;
+	}
+	tell_sched_cpu_down_done(cpu);
+}
 
 void get_online_cpus(void)
 {
@@ -338,13 +621,15 @@ static int take_cpu_down(void *_param)
 /* Requires cpu_add_remove_lock to be held */
 static int _cpu_down(unsigned int cpu, int tasks_frozen)
 {
-	int err, nr_calls = 0;
+	int mycpu, err, nr_calls = 0;
 	void *hcpu = (void *)(long)cpu;
 	unsigned long mod = tasks_frozen ? CPU_TASKS_FROZEN : 0;
 	struct take_cpu_down_param tcd_param = {
 		.mod = mod,
 		.hcpu = hcpu,
 	};
+	cpumask_var_t cpumask;
+	cpumask_var_t cpumask_org;
 
 	if (num_online_cpus() == 1)
 		return -EBUSY;
@@ -352,7 +637,34 @@ static int _cpu_down(unsigned int cpu, int tasks_frozen)
 	if (!cpu_online(cpu))
 		return -EINVAL;
 
+	/* Move the downtaker off the unplug cpu */
+	if (!alloc_cpumask_var(&cpumask, GFP_KERNEL))
+		return -ENOMEM;
+	if (!alloc_cpumask_var(&cpumask_org, GFP_KERNEL))  {
+		free_cpumask_var(cpumask);
+		return -ENOMEM;
+	}
+
+	cpumask_copy(cpumask_org, tsk_cpus_allowed(current));
+	cpumask_andnot(cpumask, cpu_online_mask, cpumask_of(cpu));
+	set_cpus_allowed_ptr(current, cpumask);
+	free_cpumask_var(cpumask);
+	migrate_disable();
+	mycpu = smp_processor_id();
+	if (mycpu == cpu) {
+		printk(KERN_ERR "Yuck! Still on unplug CPU\n!");
+		migrate_enable();
+		err = -EBUSY;
+		goto restore_cpus;
+	}
+	migrate_enable();
+
 	cpu_hotplug_begin();
+	err = cpu_unplug_begin(cpu);
+	if (err) {
+		printk("cpu_unplug_begin(%d) failed\n", cpu);
+		goto out_cancel;
+	}
 
 	err = __cpu_notify(CPU_DOWN_PREPARE | mod, hcpu, -1, &nr_calls);
 	if (err) {
@@ -378,8 +690,12 @@ static int _cpu_down(unsigned int cpu, int tasks_frozen)
 	else
 		synchronize_rcu();
 
+	__cpu_unplug_wait(cpu);
 	smpboot_park_threads(cpu);
 
+	/* Notifiers are done. Don't let any more tasks pin this CPU. */
+	cpu_unplug_sync(cpu);
+
 	/*
 	 * Prevent irq alloc/free while the dying cpu reorganizes the
 	 * interrupt affinities.
@@ -424,9 +740,14 @@ static int _cpu_down(unsigned int cpu, int tasks_frozen)
 	check_for_tasks(cpu);
 
 out_release:
+	cpu_unplug_done(cpu);
+out_cancel:
 	cpu_hotplug_done();
 	if (!err)
 		cpu_notify_nofail(CPU_POST_DEAD | mod, hcpu);
+restore_cpus:
+	set_cpus_allowed_ptr(current, cpumask_org);
+	free_cpumask_var(cpumask_org);
 	return err;
 }
 
diff --git a/kernel/debug/kdb/kdb_io.c b/kernel/debug/kdb/kdb_io.c
index fc1ef736253c..83c666537a7a 100644
--- a/kernel/debug/kdb/kdb_io.c
+++ b/kernel/debug/kdb/kdb_io.c
@@ -554,7 +554,6 @@ int vkdb_printf(enum kdb_msgsrc src, const char *fmt, va_list ap)
 	int linecount;
 	int colcount;
 	int logging, saved_loglevel = 0;
-	int saved_trap_printk;
 	int got_printf_lock = 0;
 	int retlen = 0;
 	int fnd, len;
@@ -565,8 +564,6 @@ int vkdb_printf(enum kdb_msgsrc src, const char *fmt, va_list ap)
 	unsigned long uninitialized_var(flags);
 
 	preempt_disable();
-	saved_trap_printk = kdb_trap_printk;
-	kdb_trap_printk = 0;
 
 	/* Serialize kdb_printf if multiple cpus try to write at once.
 	 * But if any cpu goes recursive in kdb, just print the output,
@@ -855,7 +852,6 @@ int vkdb_printf(enum kdb_msgsrc src, const char *fmt, va_list ap)
 	} else {
 		__release(kdb_printf_lock);
 	}
-	kdb_trap_printk = saved_trap_printk;
 	preempt_enable();
 	return retlen;
 }
@@ -865,9 +861,11 @@ int kdb_printf(const char *fmt, ...)
 	va_list ap;
 	int r;
 
+	kdb_trap_printk++;
 	va_start(ap, fmt);
 	r = vkdb_printf(KDB_MSGSRC_INTERNAL, fmt, ap);
 	va_end(ap);
+	kdb_trap_printk--;
 
 	return r;
 }
diff --git a/kernel/events/core.c b/kernel/events/core.c
index cfc227ccfceb..6430e415b7e6 100644
--- a/kernel/events/core.c
+++ b/kernel/events/core.c
@@ -7228,6 +7228,7 @@ static void perf_swevent_init_hrtimer(struct perf_event *event)
 
 	hrtimer_init(&hwc->hrtimer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
 	hwc->hrtimer.function = perf_swevent_hrtimer;
+	hwc->hrtimer.irqsafe = 1;
 
 	/*
 	 * Since hrtimers have a fixed rate, we can do a static freq->period
diff --git a/kernel/exit.c b/kernel/exit.c
index 07110c6020a0..73497e458e4a 100644
--- a/kernel/exit.c
+++ b/kernel/exit.c
@@ -144,7 +144,7 @@ static void __exit_signal(struct task_struct *tsk)
 	 * Do this under ->siglock, we can race with another thread
 	 * doing sigqueue_free() if we have SIGQUEUE_PREALLOC signals.
 	 */
-	flush_sigqueue(&tsk->pending);
+	flush_task_sigqueue(tsk);
 	tsk->sighand = NULL;
 	spin_unlock(&sighand->siglock);
 
diff --git a/kernel/fork.c b/kernel/fork.c
index 1155eac61687..46c1e8342ad8 100644
--- a/kernel/fork.c
+++ b/kernel/fork.c
@@ -108,7 +108,7 @@ int max_threads;		/* tunable limit on nr_threads */
 
 DEFINE_PER_CPU(unsigned long, process_counts) = 0;
 
-__cacheline_aligned DEFINE_RWLOCK(tasklist_lock);  /* outer */
+DEFINE_RWLOCK(tasklist_lock);  /* outer */
 
 #ifdef CONFIG_PROVE_RCU
 int lockdep_tasklist_lock_is_held(void)
@@ -244,7 +244,9 @@ static inline void put_signal_struct(struct signal_struct *sig)
 	if (atomic_dec_and_test(&sig->sigcnt))
 		free_signal_struct(sig);
 }
-
+#ifdef CONFIG_PREEMPT_RT_BASE
+static
+#endif
 void __put_task_struct(struct task_struct *tsk)
 {
 	WARN_ON(!tsk->exit_state);
@@ -261,7 +263,18 @@ void __put_task_struct(struct task_struct *tsk)
 	if (!profile_handoff_task(tsk))
 		free_task(tsk);
 }
+#ifndef CONFIG_PREEMPT_RT_BASE
 EXPORT_SYMBOL_GPL(__put_task_struct);
+#else
+void __put_task_struct_cb(struct rcu_head *rhp)
+{
+	struct task_struct *tsk = container_of(rhp, struct task_struct, put_rcu);
+
+	__put_task_struct(tsk);
+
+}
+EXPORT_SYMBOL_GPL(__put_task_struct_cb);
+#endif
 
 void __init __weak arch_task_cache_init(void) { }
 
@@ -689,6 +702,19 @@ void __mmdrop(struct mm_struct *mm)
 }
 EXPORT_SYMBOL_GPL(__mmdrop);
 
+#ifdef CONFIG_PREEMPT_RT_BASE
+/*
+ * RCU callback for delayed mm drop. Not strictly rcu, but we don't
+ * want another facility to make this work.
+ */
+void __mmdrop_delayed(struct rcu_head *rhp)
+{
+	struct mm_struct *mm = container_of(rhp, struct mm_struct, delayed_drop);
+
+	__mmdrop(mm);
+}
+#endif
+
 /*
  * Decrement the use count and release all resources for an mm.
  */
@@ -1218,6 +1244,9 @@ static void rt_mutex_init_task(struct task_struct *p)
  */
 static void posix_cpu_timers_init(struct task_struct *tsk)
 {
+#ifdef CONFIG_PREEMPT_RT_BASE
+	tsk->posix_timer_list = NULL;
+#endif
 	tsk->cputime_expires.prof_exp = 0;
 	tsk->cputime_expires.virt_exp = 0;
 	tsk->cputime_expires.sched_exp = 0;
@@ -1343,13 +1372,15 @@ static struct task_struct *copy_process(unsigned long clone_flags,
 	spin_lock_init(&p->alloc_lock);
 
 	init_sigpending(&p->pending);
+	p->sigqueue_cache = NULL;
 
 	p->utime = p->stime = p->gtime = 0;
 	p->utimescaled = p->stimescaled = 0;
 	prev_cputime_init(&p->prev_cputime);
 
 #ifdef CONFIG_VIRT_CPU_ACCOUNTING_GEN
-	seqlock_init(&p->vtime_seqlock);
+	raw_spin_lock_init(&p->vtime_lock);
+	seqcount_init(&p->vtime_seq);
 	p->vtime_snap = 0;
 	p->vtime_snap_whence = VTIME_SLEEPING;
 #endif
diff --git a/kernel/futex.c b/kernel/futex.c
index 684d7549825a..11bca4377cd3 100644
--- a/kernel/futex.c
+++ b/kernel/futex.c
@@ -815,7 +815,9 @@ void exit_pi_state_list(struct task_struct *curr)
 		 * task still owns the PI-state:
 		 */
 		if (head->next != next) {
+			raw_spin_unlock_irq(&curr->pi_lock);
 			spin_unlock(&hb->lock);
+			raw_spin_lock_irq(&curr->pi_lock);
 			continue;
 		}
 
@@ -1210,6 +1212,7 @@ static int wake_futex_pi(u32 __user *uaddr, u32 uval, struct futex_q *this,
 	struct futex_pi_state *pi_state = this->pi_state;
 	u32 uninitialized_var(curval), newval;
 	WAKE_Q(wake_q);
+	WAKE_Q(wake_sleeper_q);
 	bool deboost;
 	int ret = 0;
 
@@ -1266,7 +1269,8 @@ static int wake_futex_pi(u32 __user *uaddr, u32 uval, struct futex_q *this,
 
 	raw_spin_unlock(&pi_state->pi_mutex.wait_lock);
 
-	deboost = rt_mutex_futex_unlock(&pi_state->pi_mutex, &wake_q);
+	deboost = rt_mutex_futex_unlock(&pi_state->pi_mutex, &wake_q,
+					&wake_sleeper_q);
 
 	/*
 	 * First unlock HB so the waiter does not spin on it once he got woken
@@ -1276,6 +1280,7 @@ static int wake_futex_pi(u32 __user *uaddr, u32 uval, struct futex_q *this,
 	 */
 	spin_unlock(&hb->lock);
 	wake_up_q(&wake_q);
+	wake_up_q_sleeper(&wake_sleeper_q);
 	if (deboost)
 		rt_mutex_adjust_prio(current);
 
@@ -1812,6 +1817,16 @@ static int futex_requeue(u32 __user *uaddr1, unsigned int flags,
 				requeue_pi_wake_futex(this, &key2, hb2);
 				drop_count++;
 				continue;
+			} else if (ret == -EAGAIN) {
+				/*
+				 * Waiter was woken by timeout or
+				 * signal and has set pi_blocked_on to
+				 * PI_WAKEUP_INPROGRESS before we
+				 * tried to enqueue it on the rtmutex.
+				 */
+				this->pi_state = NULL;
+				free_pi_state(pi_state);
+				continue;
 			} else if (ret) {
 				/* -EDEADLK */
 				this->pi_state = NULL;
@@ -2672,7 +2687,7 @@ static int futex_wait_requeue_pi(u32 __user *uaddr, unsigned int flags,
 	struct hrtimer_sleeper timeout, *to = NULL;
 	struct rt_mutex_waiter rt_waiter;
 	struct rt_mutex *pi_mutex = NULL;
-	struct futex_hash_bucket *hb;
+	struct futex_hash_bucket *hb, *hb2;
 	union futex_key key2 = FUTEX_KEY_INIT;
 	struct futex_q q = futex_q_init;
 	int res, ret;
@@ -2697,10 +2712,7 @@ static int futex_wait_requeue_pi(u32 __user *uaddr, unsigned int flags,
 	 * The waiter is allocated on our stack, manipulated by the requeue
 	 * code while we sleep on uaddr.
 	 */
-	debug_rt_mutex_init_waiter(&rt_waiter);
-	RB_CLEAR_NODE(&rt_waiter.pi_tree_entry);
-	RB_CLEAR_NODE(&rt_waiter.tree_entry);
-	rt_waiter.task = NULL;
+	rt_mutex_init_waiter(&rt_waiter, false);
 
 	ret = get_futex_key(uaddr2, flags & FLAGS_SHARED, &key2, VERIFY_WRITE);
 	if (unlikely(ret != 0))
@@ -2731,20 +2743,55 @@ static int futex_wait_requeue_pi(u32 __user *uaddr, unsigned int flags,
 	/* Queue the futex_q, drop the hb lock, wait for wakeup. */
 	futex_wait_queue_me(hb, &q, to);
 
-	spin_lock(&hb->lock);
-	ret = handle_early_requeue_pi_wakeup(hb, &q, &key2, to);
-	spin_unlock(&hb->lock);
-	if (ret)
-		goto out_put_keys;
+	/*
+	 * On RT we must avoid races with requeue and trying to block
+	 * on two mutexes (hb->lock and uaddr2's rtmutex) by
+	 * serializing access to pi_blocked_on with pi_lock.
+	 */
+	raw_spin_lock_irq(&current->pi_lock);
+	if (current->pi_blocked_on) {
+		/*
+		 * We have been requeued or are in the process of
+		 * being requeued.
+		 */
+		raw_spin_unlock_irq(&current->pi_lock);
+	} else {
+		/*
+		 * Setting pi_blocked_on to PI_WAKEUP_INPROGRESS
+		 * prevents a concurrent requeue from moving us to the
+		 * uaddr2 rtmutex. After that we can safely acquire
+		 * (and possibly block on) hb->lock.
+		 */
+		current->pi_blocked_on = PI_WAKEUP_INPROGRESS;
+		raw_spin_unlock_irq(&current->pi_lock);
+
+		spin_lock(&hb->lock);
+
+		/*
+		 * Clean up pi_blocked_on. We might leak it otherwise
+		 * when we succeeded with the hb->lock in the fast
+		 * path.
+		 */
+		raw_spin_lock_irq(&current->pi_lock);
+		current->pi_blocked_on = NULL;
+		raw_spin_unlock_irq(&current->pi_lock);
+
+		ret = handle_early_requeue_pi_wakeup(hb, &q, &key2, to);
+		spin_unlock(&hb->lock);
+		if (ret)
+			goto out_put_keys;
+	}
 
 	/*
-	 * In order for us to be here, we know our q.key == key2, and since
-	 * we took the hb->lock above, we also know that futex_requeue() has
-	 * completed and we no longer have to concern ourselves with a wakeup
-	 * race with the atomic proxy lock acquisition by the requeue code. The
-	 * futex_requeue dropped our key1 reference and incremented our key2
-	 * reference count.
+	 * In order to be here, we have either been requeued, are in
+	 * the process of being requeued, or requeue successfully
+	 * acquired uaddr2 on our behalf.  If pi_blocked_on was
+	 * non-null above, we may be racing with a requeue.  Do not
+	 * rely on q->lock_ptr to be hb2->lock until after blocking on
+	 * hb->lock or hb2->lock. The futex_requeue dropped our key1
+	 * reference and incremented our key2 reference count.
 	 */
+	hb2 = hash_futex(&key2);
 
 	/* Check if the requeue code acquired the second futex for us. */
 	if (!q.rt_waiter) {
@@ -2753,9 +2800,10 @@ static int futex_wait_requeue_pi(u32 __user *uaddr, unsigned int flags,
 		 * did a lock-steal - fix up the PI-state in that case.
 		 */
 		if (q.pi_state && (q.pi_state->owner != current)) {
-			spin_lock(q.lock_ptr);
+			spin_lock(&hb2->lock);
+			BUG_ON(&hb2->lock != q.lock_ptr);
 			ret = fixup_pi_state_owner(uaddr2, &q, current);
-			spin_unlock(q.lock_ptr);
+			spin_unlock(&hb2->lock);
 		}
 	} else {
 		/*
@@ -2768,7 +2816,8 @@ static int futex_wait_requeue_pi(u32 __user *uaddr, unsigned int flags,
 		ret = rt_mutex_finish_proxy_lock(pi_mutex, to, &rt_waiter);
 		debug_rt_mutex_free_waiter(&rt_waiter);
 
-		spin_lock(q.lock_ptr);
+		spin_lock(&hb2->lock);
+		BUG_ON(&hb2->lock != q.lock_ptr);
 		/*
 		 * Fixup the pi_state owner and possibly acquire the lock if we
 		 * haven't already.
diff --git a/kernel/irq/handle.c b/kernel/irq/handle.c
index a302cf9a2126..5c1824a66bda 100644
--- a/kernel/irq/handle.c
+++ b/kernel/irq/handle.c
@@ -134,6 +134,8 @@ void __irq_wake_thread(struct irq_desc *desc, struct irqaction *action)
 
 irqreturn_t handle_irq_event_percpu(struct irq_desc *desc)
 {
+	struct pt_regs *regs = get_irq_regs();
+	u64 ip = regs ? instruction_pointer(regs) : 0;
 	irqreturn_t retval = IRQ_NONE;
 	unsigned int flags = 0, irq = desc->irq_data.irq;
 	struct irqaction *action = desc->action;
@@ -175,7 +177,11 @@ irqreturn_t handle_irq_event_percpu(struct irq_desc *desc)
 		action = action->next;
 	} while (action);
 
-	add_interrupt_randomness(irq, flags);
+#ifdef CONFIG_PREEMPT_RT_FULL
+	desc->random_ip = ip;
+#else
+	add_interrupt_randomness(irq, flags, ip);
+#endif
 
 	if (!noirqdebug)
 		note_interrupt(desc, retval);
diff --git a/kernel/irq/manage.c b/kernel/irq/manage.c
index 6ead200370da..ba2a42a37025 100644
--- a/kernel/irq/manage.c
+++ b/kernel/irq/manage.c
@@ -22,6 +22,7 @@
 #include "internals.h"
 
 #ifdef CONFIG_IRQ_FORCED_THREADING
+# ifndef CONFIG_PREEMPT_RT_BASE
 __read_mostly bool force_irqthreads;
 
 static int __init setup_forced_irqthreads(char *arg)
@@ -30,6 +31,7 @@ static int __init setup_forced_irqthreads(char *arg)
 	return 0;
 }
 early_param("threadirqs", setup_forced_irqthreads);
+# endif
 #endif
 
 static void __synchronize_hardirq(struct irq_desc *desc)
@@ -181,6 +183,62 @@ static inline void
 irq_get_pending(struct cpumask *mask, struct irq_desc *desc) { }
 #endif
 
+#ifdef CONFIG_PREEMPT_RT_FULL
+static void _irq_affinity_notify(struct irq_affinity_notify *notify);
+static struct task_struct *set_affinity_helper;
+static LIST_HEAD(affinity_list);
+static DEFINE_RAW_SPINLOCK(affinity_list_lock);
+
+static int set_affinity_thread(void *unused)
+{
+	while (1) {
+		struct irq_affinity_notify *notify;
+		int empty;
+
+		set_current_state(TASK_INTERRUPTIBLE);
+
+		raw_spin_lock_irq(&affinity_list_lock);
+		empty = list_empty(&affinity_list);
+		raw_spin_unlock_irq(&affinity_list_lock);
+
+		if (empty)
+			schedule();
+		if (kthread_should_stop())
+			break;
+		set_current_state(TASK_RUNNING);
+try_next:
+		notify = NULL;
+
+		raw_spin_lock_irq(&affinity_list_lock);
+		if (!list_empty(&affinity_list)) {
+			notify = list_first_entry(&affinity_list,
+					struct irq_affinity_notify, list);
+			list_del_init(&notify->list);
+		}
+		raw_spin_unlock_irq(&affinity_list_lock);
+
+		if (!notify)
+			continue;
+		_irq_affinity_notify(notify);
+		goto try_next;
+	}
+	return 0;
+}
+
+static void init_helper_thread(void)
+{
+	if (set_affinity_helper)
+		return;
+	set_affinity_helper = kthread_run(set_affinity_thread, NULL,
+			"affinity-cb");
+	WARN_ON(IS_ERR(set_affinity_helper));
+}
+#else
+
+static inline void init_helper_thread(void) { }
+
+#endif
+
 int irq_do_set_affinity(struct irq_data *data, const struct cpumask *mask,
 			bool force)
 {
@@ -220,7 +278,17 @@ int irq_set_affinity_locked(struct irq_data *data, const struct cpumask *mask,
 
 	if (desc->affinity_notify) {
 		kref_get(&desc->affinity_notify->kref);
+
+#ifdef CONFIG_PREEMPT_RT_FULL
+		raw_spin_lock(&affinity_list_lock);
+		if (list_empty(&desc->affinity_notify->list))
+			list_add_tail(&affinity_list,
+					&desc->affinity_notify->list);
+		raw_spin_unlock(&affinity_list_lock);
+		wake_up_process(set_affinity_helper);
+#else
 		schedule_work(&desc->affinity_notify->work);
+#endif
 	}
 	irqd_set(data, IRQD_AFFINITY_SET);
 
@@ -258,10 +326,8 @@ int irq_set_affinity_hint(unsigned int irq, const struct cpumask *m)
 }
 EXPORT_SYMBOL_GPL(irq_set_affinity_hint);
 
-static void irq_affinity_notify(struct work_struct *work)
+static void _irq_affinity_notify(struct irq_affinity_notify *notify)
 {
-	struct irq_affinity_notify *notify =
-		container_of(work, struct irq_affinity_notify, work);
 	struct irq_desc *desc = irq_to_desc(notify->irq);
 	cpumask_var_t cpumask;
 	unsigned long flags;
@@ -283,6 +349,13 @@ static void irq_affinity_notify(struct work_struct *work)
 	kref_put(&notify->kref, notify->release);
 }
 
+static void irq_affinity_notify(struct work_struct *work)
+{
+	struct irq_affinity_notify *notify =
+		container_of(work, struct irq_affinity_notify, work);
+	_irq_affinity_notify(notify);
+}
+
 /**
  *	irq_set_affinity_notifier - control notification of IRQ affinity changes
  *	@irq:		Interrupt for which to enable/disable notification
@@ -312,6 +385,8 @@ irq_set_affinity_notifier(unsigned int irq, struct irq_affinity_notify *notify)
 		notify->irq = irq;
 		kref_init(&notify->kref);
 		INIT_WORK(&notify->work, irq_affinity_notify);
+		INIT_LIST_HEAD(&notify->list);
+		init_helper_thread();
 	}
 
 	raw_spin_lock_irqsave(&desc->lock, flags);
@@ -865,7 +940,15 @@ irq_forced_thread_fn(struct irq_desc *desc, struct irqaction *action)
 	local_bh_disable();
 	ret = action->thread_fn(action->irq, action->dev_id);
 	irq_finalize_oneshot(desc, action);
-	local_bh_enable();
+	/*
+	 * Interrupts which have real time requirements can be set up
+	 * to avoid softirq processing in the thread handler. This is
+	 * safe as these interrupts do not raise soft interrupts.
+	 */
+	if (irq_settings_no_softirq_call(desc))
+		_local_bh_enable();
+	else
+		local_bh_enable();
 	return ret;
 }
 
@@ -962,6 +1045,12 @@ static int irq_thread(void *data)
 		if (action_ret == IRQ_WAKE_THREAD)
 			irq_wake_secondary(desc, action);
 
+#ifdef CONFIG_PREEMPT_RT_FULL
+		migrate_disable();
+		add_interrupt_randomness(action->irq, 0,
+				 desc->random_ip ^ (unsigned long) action);
+		migrate_enable();
+#endif
 		wake_threads_waitq(desc);
 	}
 
@@ -1315,6 +1404,9 @@ __setup_irq(unsigned int irq, struct irq_desc *desc, struct irqaction *new)
 			irqd_set(&desc->irq_data, IRQD_NO_BALANCING);
 		}
 
+		if (new->flags & IRQF_NO_SOFTIRQ_CALL)
+			irq_settings_set_no_softirq_call(desc);
+
 		/* Set default affinity mask once everything is setup */
 		setup_affinity(desc, mask);
 
diff --git a/kernel/irq/settings.h b/kernel/irq/settings.h
index 320579d89091..2df2d4445b1e 100644
--- a/kernel/irq/settings.h
+++ b/kernel/irq/settings.h
@@ -16,6 +16,7 @@ enum {
 	_IRQ_PER_CPU_DEVID	= IRQ_PER_CPU_DEVID,
 	_IRQ_IS_POLLED		= IRQ_IS_POLLED,
 	_IRQ_DISABLE_UNLAZY	= IRQ_DISABLE_UNLAZY,
+	_IRQ_NO_SOFTIRQ_CALL	= IRQ_NO_SOFTIRQ_CALL,
 	_IRQF_MODIFY_MASK	= IRQF_MODIFY_MASK,
 };
 
@@ -30,6 +31,7 @@ enum {
 #define IRQ_PER_CPU_DEVID	GOT_YOU_MORON
 #define IRQ_IS_POLLED		GOT_YOU_MORON
 #define IRQ_DISABLE_UNLAZY	GOT_YOU_MORON
+#define IRQ_NO_SOFTIRQ_CALL	GOT_YOU_MORON
 #undef IRQF_MODIFY_MASK
 #define IRQF_MODIFY_MASK	GOT_YOU_MORON
 
@@ -40,6 +42,16 @@ irq_settings_clr_and_set(struct irq_desc *desc, u32 clr, u32 set)
 	desc->status_use_accessors |= (set & _IRQF_MODIFY_MASK);
 }
 
+static inline bool irq_settings_no_softirq_call(struct irq_desc *desc)
+{
+	return desc->status_use_accessors & _IRQ_NO_SOFTIRQ_CALL;
+}
+
+static inline void irq_settings_set_no_softirq_call(struct irq_desc *desc)
+{
+	desc->status_use_accessors |= _IRQ_NO_SOFTIRQ_CALL;
+}
+
 static inline bool irq_settings_is_per_cpu(struct irq_desc *desc)
 {
 	return desc->status_use_accessors & _IRQ_PER_CPU;
diff --git a/kernel/irq/spurious.c b/kernel/irq/spurious.c
index 32144175458d..ed26f2554972 100644
--- a/kernel/irq/spurious.c
+++ b/kernel/irq/spurious.c
@@ -444,6 +444,10 @@ MODULE_PARM_DESC(noirqdebug, "Disable irq lockup detection when true");
 
 static int __init irqfixup_setup(char *str)
 {
+#ifdef CONFIG_PREEMPT_RT_BASE
+	pr_warn("irqfixup boot option not supported w/ CONFIG_PREEMPT_RT_BASE\n");
+	return 1;
+#endif
 	irqfixup = 1;
 	printk(KERN_WARNING "Misrouted IRQ fixup support enabled.\n");
 	printk(KERN_WARNING "This may impact system performance.\n");
@@ -456,6 +460,10 @@ module_param(irqfixup, int, 0644);
 
 static int __init irqpoll_setup(char *str)
 {
+#ifdef CONFIG_PREEMPT_RT_BASE
+	pr_warn("irqpoll boot option not supported w/ CONFIG_PREEMPT_RT_BASE\n");
+	return 1;
+#endif
 	irqfixup = 2;
 	printk(KERN_WARNING "Misrouted IRQ fixup and polling support "
 				"enabled\n");
diff --git a/kernel/irq_work.c b/kernel/irq_work.c
index bcf107ce0854..2899ba0d23d1 100644
--- a/kernel/irq_work.c
+++ b/kernel/irq_work.c
@@ -17,6 +17,7 @@
 #include <linux/cpu.h>
 #include <linux/notifier.h>
 #include <linux/smp.h>
+#include <linux/interrupt.h>
 #include <asm/processor.h>
 
 
@@ -65,6 +66,8 @@ void __weak arch_irq_work_raise(void)
  */
 bool irq_work_queue_on(struct irq_work *work, int cpu)
 {
+	struct llist_head *list;
+
 	/* All work should have been flushed before going offline */
 	WARN_ON_ONCE(cpu_is_offline(cpu));
 
@@ -75,7 +78,12 @@ bool irq_work_queue_on(struct irq_work *work, int cpu)
 	if (!irq_work_claim(work))
 		return false;
 
-	if (llist_add(&work->llnode, &per_cpu(raised_list, cpu)))
+	if (IS_ENABLED(CONFIG_PREEMPT_RT_FULL) && !(work->flags & IRQ_WORK_HARD_IRQ))
+		list = &per_cpu(lazy_list, cpu);
+	else
+		list = &per_cpu(raised_list, cpu);
+
+	if (llist_add(&work->llnode, list))
 		arch_send_call_function_single_ipi(cpu);
 
 	return true;
@@ -86,6 +94,9 @@ EXPORT_SYMBOL_GPL(irq_work_queue_on);
 /* Enqueue the irq work @work on the current CPU */
 bool irq_work_queue(struct irq_work *work)
 {
+	struct llist_head *list;
+	bool lazy_work, realtime = IS_ENABLED(CONFIG_PREEMPT_RT_FULL);
+
 	/* Only queue if not already pending */
 	if (!irq_work_claim(work))
 		return false;
@@ -93,13 +104,15 @@ bool irq_work_queue(struct irq_work *work)
 	/* Queue the entry and raise the IPI if needed. */
 	preempt_disable();
 
-	/* If the work is "lazy", handle it from next tick if any */
-	if (work->flags & IRQ_WORK_LAZY) {
-		if (llist_add(&work->llnode, this_cpu_ptr(&lazy_list)) &&
-		    tick_nohz_tick_stopped())
-			arch_irq_work_raise();
-	} else {
-		if (llist_add(&work->llnode, this_cpu_ptr(&raised_list)))
+	lazy_work = work->flags & IRQ_WORK_LAZY;
+
+	if (lazy_work || (realtime && !(work->flags & IRQ_WORK_HARD_IRQ)))
+		list = this_cpu_ptr(&lazy_list);
+	else
+		list = this_cpu_ptr(&raised_list);
+
+	if (llist_add(&work->llnode, list)) {
+		if (!lazy_work || tick_nohz_tick_stopped())
 			arch_irq_work_raise();
 	}
 
@@ -116,9 +129,8 @@ bool irq_work_needs_cpu(void)
 	raised = this_cpu_ptr(&raised_list);
 	lazy = this_cpu_ptr(&lazy_list);
 
-	if (llist_empty(raised) || arch_irq_work_has_interrupt())
-		if (llist_empty(lazy))
-			return false;
+	if (llist_empty(raised) && llist_empty(lazy))
+		return false;
 
 	/* All work should have been flushed before going offline */
 	WARN_ON_ONCE(cpu_is_offline(smp_processor_id()));
@@ -132,7 +144,7 @@ static void irq_work_run_list(struct llist_head *list)
 	struct irq_work *work;
 	struct llist_node *llnode;
 
-	BUG_ON(!irqs_disabled());
+	BUG_ON_NONRT(!irqs_disabled());
 
 	if (llist_empty(list))
 		return;
@@ -169,7 +181,16 @@ static void irq_work_run_list(struct llist_head *list)
 void irq_work_run(void)
 {
 	irq_work_run_list(this_cpu_ptr(&raised_list));
-	irq_work_run_list(this_cpu_ptr(&lazy_list));
+	if (IS_ENABLED(CONFIG_PREEMPT_RT_FULL)) {
+		/*
+		 * NOTE: we raise softirq via IPI for safety,
+		 * and execute in irq_work_tick() to move the
+		 * overhead from hard to soft irq context.
+		 */
+		if (!llist_empty(this_cpu_ptr(&lazy_list)))
+			raise_softirq(TIMER_SOFTIRQ);
+	} else
+		irq_work_run_list(this_cpu_ptr(&lazy_list));
 }
 EXPORT_SYMBOL_GPL(irq_work_run);
 
@@ -179,8 +200,17 @@ void irq_work_tick(void)
 
 	if (!llist_empty(raised) && !arch_irq_work_has_interrupt())
 		irq_work_run_list(raised);
+
+	if (!IS_ENABLED(CONFIG_PREEMPT_RT_FULL))
+		irq_work_run_list(this_cpu_ptr(&lazy_list));
+}
+
+#if defined(CONFIG_IRQ_WORK) && defined(CONFIG_PREEMPT_RT_FULL)
+void irq_work_tick_soft(void)
+{
 	irq_work_run_list(this_cpu_ptr(&lazy_list));
 }
+#endif
 
 /*
  * Synchronize against the irq_work @entry, ensures the entry is not
diff --git a/kernel/ksysfs.c b/kernel/ksysfs.c
index e83b26464061..c0e08d1cf33e 100644
--- a/kernel/ksysfs.c
+++ b/kernel/ksysfs.c
@@ -136,6 +136,15 @@ KERNEL_ATTR_RO(vmcoreinfo);
 
 #endif /* CONFIG_KEXEC_CORE */
 
+#if defined(CONFIG_PREEMPT_RT_FULL)
+static ssize_t  realtime_show(struct kobject *kobj,
+			      struct kobj_attribute *attr, char *buf)
+{
+	return sprintf(buf, "%d\n", 1);
+}
+KERNEL_ATTR_RO(realtime);
+#endif
+
 /* whether file capabilities are enabled */
 static ssize_t fscaps_show(struct kobject *kobj,
 				  struct kobj_attribute *attr, char *buf)
@@ -203,6 +212,9 @@ static struct attribute * kernel_attrs[] = {
 	&vmcoreinfo_attr.attr,
 #endif
 	&rcu_expedited_attr.attr,
+#ifdef CONFIG_PREEMPT_RT_FULL
+	&realtime_attr.attr,
+#endif
 	NULL
 };
 
diff --git a/kernel/locking/Makefile b/kernel/locking/Makefile
index 8e96f6cc2a4a..447b03082d88 100644
--- a/kernel/locking/Makefile
+++ b/kernel/locking/Makefile
@@ -1,5 +1,5 @@
 
-obj-y += mutex.o semaphore.o rwsem.o percpu-rwsem.o
+obj-y += semaphore.o percpu-rwsem.o
 
 ifdef CONFIG_FUNCTION_TRACER
 CFLAGS_REMOVE_lockdep.o = $(CC_FLAGS_FTRACE)
@@ -8,7 +8,11 @@ CFLAGS_REMOVE_mutex-debug.o = $(CC_FLAGS_FTRACE)
 CFLAGS_REMOVE_rtmutex-debug.o = $(CC_FLAGS_FTRACE)
 endif
 
+ifneq ($(CONFIG_PREEMPT_RT_FULL),y)
+obj-y += mutex.o
 obj-$(CONFIG_DEBUG_MUTEXES) += mutex-debug.o
+obj-y += rwsem.o
+endif
 obj-$(CONFIG_LOCKDEP) += lockdep.o
 ifeq ($(CONFIG_PROC_FS),y)
 obj-$(CONFIG_LOCKDEP) += lockdep_proc.o
@@ -22,7 +26,10 @@ obj-$(CONFIG_RT_MUTEXES) += rtmutex.o
 obj-$(CONFIG_DEBUG_RT_MUTEXES) += rtmutex-debug.o
 obj-$(CONFIG_DEBUG_SPINLOCK) += spinlock.o
 obj-$(CONFIG_DEBUG_SPINLOCK) += spinlock_debug.o
+ifneq ($(CONFIG_PREEMPT_RT_FULL),y)
 obj-$(CONFIG_RWSEM_GENERIC_SPINLOCK) += rwsem-spinlock.o
 obj-$(CONFIG_RWSEM_XCHGADD_ALGORITHM) += rwsem-xadd.o
+endif
+obj-$(CONFIG_PREEMPT_RT_FULL) += rt.o
 obj-$(CONFIG_QUEUED_RWLOCKS) += qrwlock.o
 obj-$(CONFIG_LOCK_TORTURE_TEST) += locktorture.o
diff --git a/kernel/locking/lglock.c b/kernel/locking/lglock.c
index 951cfcd10b4a..d8be4fcc14f8 100644
--- a/kernel/locking/lglock.c
+++ b/kernel/locking/lglock.c
@@ -4,6 +4,15 @@
 #include <linux/cpu.h>
 #include <linux/string.h>
 
+#ifndef CONFIG_PREEMPT_RT_FULL
+# define lg_lock_ptr		arch_spinlock_t
+# define lg_do_lock(l)		arch_spin_lock(l)
+# define lg_do_unlock(l)	arch_spin_unlock(l)
+#else
+# define lg_lock_ptr		struct rt_mutex
+# define lg_do_lock(l)		__rt_spin_lock(l)
+# define lg_do_unlock(l)	__rt_spin_unlock(l)
+#endif
 /*
  * Note there is no uninit, so lglocks cannot be defined in
  * modules (but it's fine to use them from there)
@@ -12,51 +21,60 @@
 
 void lg_lock_init(struct lglock *lg, char *name)
 {
+#ifdef CONFIG_PREEMPT_RT_FULL
+	int i;
+
+	for_each_possible_cpu(i) {
+		struct rt_mutex *lock = per_cpu_ptr(lg->lock, i);
+
+		rt_mutex_init(lock);
+	}
+#endif
 	LOCKDEP_INIT_MAP(&lg->lock_dep_map, name, &lg->lock_key, 0);
 }
 EXPORT_SYMBOL(lg_lock_init);
 
 void lg_local_lock(struct lglock *lg)
 {
-	arch_spinlock_t *lock;
+	lg_lock_ptr *lock;
 
-	preempt_disable();
+	migrate_disable();
 	lock_acquire_shared(&lg->lock_dep_map, 0, 0, NULL, _RET_IP_);
 	lock = this_cpu_ptr(lg->lock);
-	arch_spin_lock(lock);
+	lg_do_lock(lock);
 }
 EXPORT_SYMBOL(lg_local_lock);
 
 void lg_local_unlock(struct lglock *lg)
 {
-	arch_spinlock_t *lock;
+	lg_lock_ptr *lock;
 
 	lock_release(&lg->lock_dep_map, 1, _RET_IP_);
 	lock = this_cpu_ptr(lg->lock);
-	arch_spin_unlock(lock);
-	preempt_enable();
+	lg_do_unlock(lock);
+	migrate_enable();
 }
 EXPORT_SYMBOL(lg_local_unlock);
 
 void lg_local_lock_cpu(struct lglock *lg, int cpu)
 {
-	arch_spinlock_t *lock;
+	lg_lock_ptr *lock;
 
-	preempt_disable();
+	preempt_disable_nort();
 	lock_acquire_shared(&lg->lock_dep_map, 0, 0, NULL, _RET_IP_);
 	lock = per_cpu_ptr(lg->lock, cpu);
-	arch_spin_lock(lock);
+	lg_do_lock(lock);
 }
 EXPORT_SYMBOL(lg_local_lock_cpu);
 
 void lg_local_unlock_cpu(struct lglock *lg, int cpu)
 {
-	arch_spinlock_t *lock;
+	lg_lock_ptr *lock;
 
 	lock_release(&lg->lock_dep_map, 1, _RET_IP_);
 	lock = per_cpu_ptr(lg->lock, cpu);
-	arch_spin_unlock(lock);
-	preempt_enable();
+	lg_do_unlock(lock);
+	preempt_enable_nort();
 }
 EXPORT_SYMBOL(lg_local_unlock_cpu);
 
@@ -70,15 +88,15 @@ void lg_double_lock(struct lglock *lg, int cpu1, int cpu2)
 
 	preempt_disable();
 	lock_acquire_shared(&lg->lock_dep_map, 0, 0, NULL, _RET_IP_);
-	arch_spin_lock(per_cpu_ptr(lg->lock, cpu1));
-	arch_spin_lock(per_cpu_ptr(lg->lock, cpu2));
+	lg_do_lock(per_cpu_ptr(lg->lock, cpu1));
+	lg_do_lock(per_cpu_ptr(lg->lock, cpu2));
 }
 
 void lg_double_unlock(struct lglock *lg, int cpu1, int cpu2)
 {
 	lock_release(&lg->lock_dep_map, 1, _RET_IP_);
-	arch_spin_unlock(per_cpu_ptr(lg->lock, cpu1));
-	arch_spin_unlock(per_cpu_ptr(lg->lock, cpu2));
+	lg_do_unlock(per_cpu_ptr(lg->lock, cpu1));
+	lg_do_unlock(per_cpu_ptr(lg->lock, cpu2));
 	preempt_enable();
 }
 
@@ -86,12 +104,12 @@ void lg_global_lock(struct lglock *lg)
 {
 	int i;
 
-	preempt_disable();
+	preempt_disable_nort();
 	lock_acquire_exclusive(&lg->lock_dep_map, 0, 0, NULL, _RET_IP_);
 	for_each_possible_cpu(i) {
-		arch_spinlock_t *lock;
+		lg_lock_ptr *lock;
 		lock = per_cpu_ptr(lg->lock, i);
-		arch_spin_lock(lock);
+		lg_do_lock(lock);
 	}
 }
 EXPORT_SYMBOL(lg_global_lock);
@@ -102,10 +120,35 @@ void lg_global_unlock(struct lglock *lg)
 
 	lock_release(&lg->lock_dep_map, 1, _RET_IP_);
 	for_each_possible_cpu(i) {
-		arch_spinlock_t *lock;
+		lg_lock_ptr *lock;
 		lock = per_cpu_ptr(lg->lock, i);
-		arch_spin_unlock(lock);
+		lg_do_unlock(lock);
 	}
-	preempt_enable();
+	preempt_enable_nort();
 }
 EXPORT_SYMBOL(lg_global_unlock);
+
+#ifdef CONFIG_PREEMPT_RT_FULL
+/*
+ * HACK: If you use this, you get to keep the pieces.
+ * Used in queue_stop_cpus_work() when stop machinery
+ * is called from inactive CPU, so we can't schedule.
+ */
+# define lg_do_trylock_relax(l)			\
+	do {					\
+		while (!__rt_spin_trylock(l))	\
+			cpu_relax();		\
+	} while (0)
+
+void lg_global_trylock_relax(struct lglock *lg)
+{
+	int i;
+
+	lock_acquire_exclusive(&lg->lock_dep_map, 0, 0, NULL, _RET_IP_);
+	for_each_possible_cpu(i) {
+		lg_lock_ptr *lock;
+		lock = per_cpu_ptr(lg->lock, i);
+		lg_do_trylock_relax(lock);
+	}
+}
+#endif
diff --git a/kernel/locking/lockdep.c b/kernel/locking/lockdep.c
index 60ace56618f6..e98ee958a353 100644
--- a/kernel/locking/lockdep.c
+++ b/kernel/locking/lockdep.c
@@ -3525,6 +3525,7 @@ static void check_flags(unsigned long flags)
 		}
 	}
 
+#ifndef CONFIG_PREEMPT_RT_FULL
 	/*
 	 * We dont accurately track softirq state in e.g.
 	 * hardirq contexts (such as on 4KSTACKS), so only
@@ -3539,6 +3540,7 @@ static void check_flags(unsigned long flags)
 			DEBUG_LOCKS_WARN_ON(!current->softirqs_enabled);
 		}
 	}
+#endif
 
 	if (!debug_locks)
 		print_irqtrace_events(current);
diff --git a/kernel/locking/locktorture.c b/kernel/locking/locktorture.c
index 8ef1919d63b2..291fc19e28e0 100644
--- a/kernel/locking/locktorture.c
+++ b/kernel/locking/locktorture.c
@@ -26,7 +26,6 @@
 #include <linux/kthread.h>
 #include <linux/sched/rt.h>
 #include <linux/spinlock.h>
-#include <linux/rwlock.h>
 #include <linux/mutex.h>
 #include <linux/rwsem.h>
 #include <linux/smp.h>
diff --git a/kernel/locking/rt.c b/kernel/locking/rt.c
new file mode 100644
index 000000000000..1bbbcad5e360
--- /dev/null
+++ b/kernel/locking/rt.c
@@ -0,0 +1,476 @@
+/*
+ * kernel/rt.c
+ *
+ * Real-Time Preemption Support
+ *
+ * started by Ingo Molnar:
+ *
+ *  Copyright (C) 2004-2006 Red Hat, Inc., Ingo Molnar <mingo@redhat.com>
+ *  Copyright (C) 2006, Timesys Corp., Thomas Gleixner <tglx@timesys.com>
+ *
+ * historic credit for proving that Linux spinlocks can be implemented via
+ * RT-aware mutexes goes to many people: The Pmutex project (Dirk Grambow
+ * and others) who prototyped it on 2.4 and did lots of comparative
+ * research and analysis; TimeSys, for proving that you can implement a
+ * fully preemptible kernel via the use of IRQ threading and mutexes;
+ * Bill Huey for persuasively arguing on lkml that the mutex model is the
+ * right one; and to MontaVista, who ported pmutexes to 2.6.
+ *
+ * This code is a from-scratch implementation and is not based on pmutexes,
+ * but the idea of converting spinlocks to mutexes is used here too.
+ *
+ * lock debugging, locking tree, deadlock detection:
+ *
+ *  Copyright (C) 2004, LynuxWorks, Inc., Igor Manyilov, Bill Huey
+ *  Released under the General Public License (GPL).
+ *
+ * Includes portions of the generic R/W semaphore implementation from:
+ *
+ *  Copyright (c) 2001   David Howells (dhowells@redhat.com).
+ *  - Derived partially from idea by Andrea Arcangeli <andrea@suse.de>
+ *  - Derived also from comments by Linus
+ *
+ * Pending ownership of locks and ownership stealing:
+ *
+ *  Copyright (C) 2005, Kihon Technologies Inc., Steven Rostedt
+ *
+ *   (also by Steven Rostedt)
+ *    - Converted single pi_lock to individual task locks.
+ *
+ * By Esben Nielsen:
+ *    Doing priority inheritance with help of the scheduler.
+ *
+ *  Copyright (C) 2006, Timesys Corp., Thomas Gleixner <tglx@timesys.com>
+ *  - major rework based on Esben Nielsens initial patch
+ *  - replaced thread_info references by task_struct refs
+ *  - removed task->pending_owner dependency
+ *  - BKL drop/reacquire for semaphore style locks to avoid deadlocks
+ *    in the scheduler return path as discussed with Steven Rostedt
+ *
+ *  Copyright (C) 2006, Kihon Technologies Inc.
+ *    Steven Rostedt <rostedt@goodmis.org>
+ *  - debugged and patched Thomas Gleixner's rework.
+ *  - added back the cmpxchg to the rework.
+ *  - turned atomic require back on for SMP.
+ */
+
+#include <linux/spinlock.h>
+#include <linux/rtmutex.h>
+#include <linux/sched.h>
+#include <linux/delay.h>
+#include <linux/module.h>
+#include <linux/kallsyms.h>
+#include <linux/syscalls.h>
+#include <linux/interrupt.h>
+#include <linux/plist.h>
+#include <linux/fs.h>
+#include <linux/futex.h>
+#include <linux/hrtimer.h>
+
+#include "rtmutex_common.h"
+
+/*
+ * struct mutex functions
+ */
+void __mutex_do_init(struct mutex *mutex, const char *name,
+		     struct lock_class_key *key)
+{
+#ifdef CONFIG_DEBUG_LOCK_ALLOC
+	/*
+	 * Make sure we are not reinitializing a held lock:
+	 */
+	debug_check_no_locks_freed((void *)mutex, sizeof(*mutex));
+	lockdep_init_map(&mutex->dep_map, name, key, 0);
+#endif
+	mutex->lock.save_state = 0;
+}
+EXPORT_SYMBOL(__mutex_do_init);
+
+void __lockfunc _mutex_lock(struct mutex *lock)
+{
+	mutex_acquire(&lock->dep_map, 0, 0, _RET_IP_);
+	rt_mutex_lock(&lock->lock);
+}
+EXPORT_SYMBOL(_mutex_lock);
+
+int __lockfunc _mutex_lock_interruptible(struct mutex *lock)
+{
+	int ret;
+
+	mutex_acquire(&lock->dep_map, 0, 0, _RET_IP_);
+	ret = rt_mutex_lock_interruptible(&lock->lock);
+	if (ret)
+		mutex_release(&lock->dep_map, 1, _RET_IP_);
+	return ret;
+}
+EXPORT_SYMBOL(_mutex_lock_interruptible);
+
+int __lockfunc _mutex_lock_killable(struct mutex *lock)
+{
+	int ret;
+
+	mutex_acquire(&lock->dep_map, 0, 0, _RET_IP_);
+	ret = rt_mutex_lock_killable(&lock->lock);
+	if (ret)
+		mutex_release(&lock->dep_map, 1, _RET_IP_);
+	return ret;
+}
+EXPORT_SYMBOL(_mutex_lock_killable);
+
+#ifdef CONFIG_DEBUG_LOCK_ALLOC
+void __lockfunc _mutex_lock_nested(struct mutex *lock, int subclass)
+{
+	mutex_acquire_nest(&lock->dep_map, subclass, 0, NULL, _RET_IP_);
+	rt_mutex_lock(&lock->lock);
+}
+EXPORT_SYMBOL(_mutex_lock_nested);
+
+void __lockfunc _mutex_lock_nest_lock(struct mutex *lock, struct lockdep_map *nest)
+{
+	mutex_acquire_nest(&lock->dep_map, 0, 0, nest, _RET_IP_);
+	rt_mutex_lock(&lock->lock);
+}
+EXPORT_SYMBOL(_mutex_lock_nest_lock);
+
+int __lockfunc _mutex_lock_interruptible_nested(struct mutex *lock, int subclass)
+{
+	int ret;
+
+	mutex_acquire_nest(&lock->dep_map, subclass, 0, NULL, _RET_IP_);
+	ret = rt_mutex_lock_interruptible(&lock->lock);
+	if (ret)
+		mutex_release(&lock->dep_map, 1, _RET_IP_);
+	return ret;
+}
+EXPORT_SYMBOL(_mutex_lock_interruptible_nested);
+
+int __lockfunc _mutex_lock_killable_nested(struct mutex *lock, int subclass)
+{
+	int ret;
+
+	mutex_acquire(&lock->dep_map, subclass, 0, _RET_IP_);
+	ret = rt_mutex_lock_killable(&lock->lock);
+	if (ret)
+		mutex_release(&lock->dep_map, 1, _RET_IP_);
+	return ret;
+}
+EXPORT_SYMBOL(_mutex_lock_killable_nested);
+#endif
+
+int __lockfunc _mutex_trylock(struct mutex *lock)
+{
+	int ret = rt_mutex_trylock(&lock->lock);
+
+	if (ret)
+		mutex_acquire(&lock->dep_map, 0, 1, _RET_IP_);
+
+	return ret;
+}
+EXPORT_SYMBOL(_mutex_trylock);
+
+void __lockfunc _mutex_unlock(struct mutex *lock)
+{
+	mutex_release(&lock->dep_map, 1, _RET_IP_);
+	rt_mutex_unlock(&lock->lock);
+}
+EXPORT_SYMBOL(_mutex_unlock);
+
+/*
+ * rwlock_t functions
+ */
+int __lockfunc rt_write_trylock(rwlock_t *rwlock)
+{
+	int ret;
+
+	migrate_disable();
+	ret = rt_mutex_trylock(&rwlock->lock);
+	if (ret)
+		rwlock_acquire(&rwlock->dep_map, 0, 1, _RET_IP_);
+	else
+		migrate_enable();
+
+	return ret;
+}
+EXPORT_SYMBOL(rt_write_trylock);
+
+int __lockfunc rt_write_trylock_irqsave(rwlock_t *rwlock, unsigned long *flags)
+{
+	int ret;
+
+	*flags = 0;
+	ret = rt_write_trylock(rwlock);
+	return ret;
+}
+EXPORT_SYMBOL(rt_write_trylock_irqsave);
+
+int __lockfunc rt_read_trylock(rwlock_t *rwlock)
+{
+	struct rt_mutex *lock = &rwlock->lock;
+	int ret = 1;
+
+	/*
+	 * recursive read locks succeed when current owns the lock,
+	 * but not when read_depth == 0 which means that the lock is
+	 * write locked.
+	 */
+	if (rt_mutex_owner(lock) != current) {
+		migrate_disable();
+		ret = rt_mutex_trylock(lock);
+		if (ret)
+			rwlock_acquire(&rwlock->dep_map, 0, 1, _RET_IP_);
+		else
+			migrate_enable();
+
+	} else if (!rwlock->read_depth) {
+		ret = 0;
+	}
+
+	if (ret)
+		rwlock->read_depth++;
+
+	return ret;
+}
+EXPORT_SYMBOL(rt_read_trylock);
+
+void __lockfunc rt_write_lock(rwlock_t *rwlock)
+{
+	rwlock_acquire(&rwlock->dep_map, 0, 0, _RET_IP_);
+	migrate_disable();
+	__rt_spin_lock(&rwlock->lock);
+}
+EXPORT_SYMBOL(rt_write_lock);
+
+void __lockfunc rt_read_lock(rwlock_t *rwlock)
+{
+	struct rt_mutex *lock = &rwlock->lock;
+
+
+	/*
+	 * recursive read locks succeed when current owns the lock
+	 */
+	if (rt_mutex_owner(lock) != current) {
+		migrate_disable();
+		rwlock_acquire(&rwlock->dep_map, 0, 0, _RET_IP_);
+		__rt_spin_lock(lock);
+	}
+	rwlock->read_depth++;
+}
+
+EXPORT_SYMBOL(rt_read_lock);
+
+void __lockfunc rt_write_unlock(rwlock_t *rwlock)
+{
+	/* NOTE: we always pass in '1' for nested, for simplicity */
+	rwlock_release(&rwlock->dep_map, 1, _RET_IP_);
+	__rt_spin_unlock(&rwlock->lock);
+	migrate_enable();
+}
+EXPORT_SYMBOL(rt_write_unlock);
+
+void __lockfunc rt_read_unlock(rwlock_t *rwlock)
+{
+	/* Release the lock only when read_depth is down to 0 */
+	if (--rwlock->read_depth == 0) {
+		rwlock_release(&rwlock->dep_map, 1, _RET_IP_);
+		__rt_spin_unlock(&rwlock->lock);
+		migrate_enable();
+	}
+}
+EXPORT_SYMBOL(rt_read_unlock);
+
+unsigned long __lockfunc rt_write_lock_irqsave(rwlock_t *rwlock)
+{
+	rt_write_lock(rwlock);
+
+	return 0;
+}
+EXPORT_SYMBOL(rt_write_lock_irqsave);
+
+unsigned long __lockfunc rt_read_lock_irqsave(rwlock_t *rwlock)
+{
+	rt_read_lock(rwlock);
+
+	return 0;
+}
+EXPORT_SYMBOL(rt_read_lock_irqsave);
+
+void __rt_rwlock_init(rwlock_t *rwlock, char *name, struct lock_class_key *key)
+{
+#ifdef CONFIG_DEBUG_LOCK_ALLOC
+	/*
+	 * Make sure we are not reinitializing a held lock:
+	 */
+	debug_check_no_locks_freed((void *)rwlock, sizeof(*rwlock));
+	lockdep_init_map(&rwlock->dep_map, name, key, 0);
+#endif
+	rwlock->lock.save_state = 1;
+	rwlock->read_depth = 0;
+}
+EXPORT_SYMBOL(__rt_rwlock_init);
+
+/*
+ * rw_semaphores
+ */
+
+void  rt_up_write(struct rw_semaphore *rwsem)
+{
+	rwsem_release(&rwsem->dep_map, 1, _RET_IP_);
+	rt_mutex_unlock(&rwsem->lock);
+}
+EXPORT_SYMBOL(rt_up_write);
+
+void __rt_up_read(struct rw_semaphore *rwsem)
+{
+	if (--rwsem->read_depth == 0)
+		rt_mutex_unlock(&rwsem->lock);
+}
+
+void  rt_up_read(struct rw_semaphore *rwsem)
+{
+	rwsem_release(&rwsem->dep_map, 1, _RET_IP_);
+	__rt_up_read(rwsem);
+}
+EXPORT_SYMBOL(rt_up_read);
+
+/*
+ * downgrade a write lock into a read lock
+ * - just wake up any readers at the front of the queue
+ */
+void  rt_downgrade_write(struct rw_semaphore *rwsem)
+{
+	BUG_ON(rt_mutex_owner(&rwsem->lock) != current);
+	rwsem->read_depth = 1;
+}
+EXPORT_SYMBOL(rt_downgrade_write);
+
+int  rt_down_write_trylock(struct rw_semaphore *rwsem)
+{
+	int ret = rt_mutex_trylock(&rwsem->lock);
+
+	if (ret)
+		rwsem_acquire(&rwsem->dep_map, 0, 1, _RET_IP_);
+	return ret;
+}
+EXPORT_SYMBOL(rt_down_write_trylock);
+
+void  rt_down_write(struct rw_semaphore *rwsem)
+{
+	rwsem_acquire(&rwsem->dep_map, 0, 0, _RET_IP_);
+	rt_mutex_lock(&rwsem->lock);
+}
+EXPORT_SYMBOL(rt_down_write);
+
+void  rt_down_write_nested(struct rw_semaphore *rwsem, int subclass)
+{
+	rwsem_acquire(&rwsem->dep_map, subclass, 0, _RET_IP_);
+	rt_mutex_lock(&rwsem->lock);
+}
+EXPORT_SYMBOL(rt_down_write_nested);
+
+void rt_down_write_nested_lock(struct rw_semaphore *rwsem,
+			       struct lockdep_map *nest)
+{
+	rwsem_acquire_nest(&rwsem->dep_map, 0, 0, nest, _RET_IP_);
+	rt_mutex_lock(&rwsem->lock);
+}
+EXPORT_SYMBOL(rt_down_write_nested_lock);
+
+int rt__down_read_trylock(struct rw_semaphore *rwsem)
+{
+	struct rt_mutex *lock = &rwsem->lock;
+	int ret = 1;
+
+	/*
+	 * recursive read locks succeed when current owns the rwsem,
+	 * but not when read_depth == 0 which means that the rwsem is
+	 * write locked.
+	 */
+	if (rt_mutex_owner(lock) != current)
+		ret = rt_mutex_trylock(&rwsem->lock);
+	else if (!rwsem->read_depth)
+		ret = 0;
+
+	if (ret)
+		rwsem->read_depth++;
+	return ret;
+
+}
+
+int  rt_down_read_trylock(struct rw_semaphore *rwsem)
+{
+	int ret;
+
+	ret = rt__down_read_trylock(rwsem);
+	if (ret)
+		rwsem_acquire(&rwsem->dep_map, 0, 1, _RET_IP_);
+
+	return ret;
+}
+EXPORT_SYMBOL(rt_down_read_trylock);
+
+void rt__down_read(struct rw_semaphore *rwsem)
+{
+	struct rt_mutex *lock = &rwsem->lock;
+
+	if (rt_mutex_owner(lock) != current)
+		rt_mutex_lock(&rwsem->lock);
+	rwsem->read_depth++;
+}
+EXPORT_SYMBOL(rt__down_read);
+
+static void __rt_down_read(struct rw_semaphore *rwsem, int subclass)
+{
+	rwsem_acquire_read(&rwsem->dep_map, subclass, 0, _RET_IP_);
+	rt__down_read(rwsem);
+}
+
+void  rt_down_read(struct rw_semaphore *rwsem)
+{
+	__rt_down_read(rwsem, 0);
+}
+EXPORT_SYMBOL(rt_down_read);
+
+void  rt_down_read_nested(struct rw_semaphore *rwsem, int subclass)
+{
+	__rt_down_read(rwsem, subclass);
+}
+EXPORT_SYMBOL(rt_down_read_nested);
+
+void  __rt_rwsem_init(struct rw_semaphore *rwsem, const char *name,
+			      struct lock_class_key *key)
+{
+#ifdef CONFIG_DEBUG_LOCK_ALLOC
+	/*
+	 * Make sure we are not reinitializing a held lock:
+	 */
+	debug_check_no_locks_freed((void *)rwsem, sizeof(*rwsem));
+	lockdep_init_map(&rwsem->dep_map, name, key, 0);
+#endif
+	rwsem->read_depth = 0;
+	rwsem->lock.save_state = 0;
+}
+EXPORT_SYMBOL(__rt_rwsem_init);
+
+/**
+ * atomic_dec_and_mutex_lock - return holding mutex if we dec to 0
+ * @cnt: the atomic which we are to dec
+ * @lock: the mutex to return holding if we dec to 0
+ *
+ * return true and hold lock if we dec to 0, return false otherwise
+ */
+int atomic_dec_and_mutex_lock(atomic_t *cnt, struct mutex *lock)
+{
+	/* dec if we can't possibly hit 0 */
+	if (atomic_add_unless(cnt, -1, 1))
+		return 0;
+	/* we might hit 0, so take the lock */
+	mutex_lock(lock);
+	if (!atomic_dec_and_test(cnt)) {
+		/* when we actually did the dec, we didn't hit 0 */
+		mutex_unlock(lock);
+		return 0;
+	}
+	/* we hit 0, and we hold the lock */
+	return 1;
+}
+EXPORT_SYMBOL(atomic_dec_and_mutex_lock);
diff --git a/kernel/locking/rtmutex.c b/kernel/locking/rtmutex.c
index 8251e75dd9c0..e1ddae39d56b 100644
--- a/kernel/locking/rtmutex.c
+++ b/kernel/locking/rtmutex.c
@@ -7,6 +7,11 @@
  *  Copyright (C) 2005-2006 Timesys Corp., Thomas Gleixner <tglx@timesys.com>
  *  Copyright (C) 2005 Kihon Technologies Inc., Steven Rostedt
  *  Copyright (C) 2006 Esben Nielsen
+ *  Adaptive Spinlocks:
+ *  Copyright (C) 2008 Novell, Inc., Gregory Haskins, Sven Dietrich,
+ *				     and Peter Morreale,
+ * Adaptive Spinlocks simplification:
+ *  Copyright (C) 2008 Red Hat, Inc., Steven Rostedt <srostedt@redhat.com>
  *
  *  See Documentation/locking/rt-mutex-design.txt for details.
  */
@@ -16,6 +21,7 @@
 #include <linux/sched/rt.h>
 #include <linux/sched/deadline.h>
 #include <linux/timer.h>
+#include <linux/ww_mutex.h>
 
 #include "rtmutex_common.h"
 
@@ -69,6 +75,12 @@ static void fixup_rt_mutex_waiters(struct rt_mutex *lock)
 		clear_rt_mutex_waiters(lock);
 }
 
+static int rt_mutex_real_waiter(struct rt_mutex_waiter *waiter)
+{
+	return waiter && waiter != PI_WAKEUP_INPROGRESS &&
+		waiter != PI_REQUEUE_INPROGRESS;
+}
+
 /*
  * We can speed up the acquire/release, if there's no debugging state to be
  * set up.
@@ -348,6 +360,14 @@ static bool rt_mutex_cond_detect_deadlock(struct rt_mutex_waiter *waiter,
 	return debug_rt_mutex_detect_deadlock(waiter, chwalk);
 }
 
+static void rt_mutex_wake_waiter(struct rt_mutex_waiter *waiter)
+{
+	if (waiter->savestate)
+		wake_up_lock_sleeper(waiter->task);
+	else
+		wake_up_process(waiter->task);
+}
+
 /*
  * Max number of times we'll walk the boosting chain:
  */
@@ -355,7 +375,8 @@ int max_lock_depth = 1024;
 
 static inline struct rt_mutex *task_blocked_on_lock(struct task_struct *p)
 {
-	return p->pi_blocked_on ? p->pi_blocked_on->lock : NULL;
+	return rt_mutex_real_waiter(p->pi_blocked_on) ?
+		p->pi_blocked_on->lock : NULL;
 }
 
 /*
@@ -492,7 +513,7 @@ static int rt_mutex_adjust_prio_chain(struct task_struct *task,
 	 * reached or the state of the chain has changed while we
 	 * dropped the locks.
 	 */
-	if (!waiter)
+	if (!rt_mutex_real_waiter(waiter))
 		goto out_unlock_pi;
 
 	/*
@@ -654,13 +675,16 @@ static int rt_mutex_adjust_prio_chain(struct task_struct *task,
 	 * follow here. This is the end of the chain we are walking.
 	 */
 	if (!rt_mutex_owner(lock)) {
+		struct rt_mutex_waiter *lock_top_waiter;
+
 		/*
 		 * If the requeue [7] above changed the top waiter,
 		 * then we need to wake the new top waiter up to try
 		 * to get the lock.
 		 */
-		if (prerequeue_top_waiter != rt_mutex_top_waiter(lock))
-			wake_up_process(rt_mutex_top_waiter(lock)->task);
+		lock_top_waiter = rt_mutex_top_waiter(lock);
+		if (prerequeue_top_waiter != lock_top_waiter)
+			rt_mutex_wake_waiter(lock_top_waiter);
 		raw_spin_unlock(&lock->wait_lock);
 		return 0;
 	}
@@ -753,6 +777,25 @@ static int rt_mutex_adjust_prio_chain(struct task_struct *task,
 	return ret;
 }
 
+
+#define STEAL_NORMAL  0
+#define STEAL_LATERAL 1
+
+/*
+ * Note that RT tasks are excluded from lateral-steals to prevent the
+ * introduction of an unbounded latency
+ */
+static inline int lock_is_stealable(struct task_struct *task,
+				    struct task_struct *pendowner, int mode)
+{
+    if (mode == STEAL_NORMAL || rt_task(task)) {
+	    if (task->prio >= pendowner->prio)
+		    return 0;
+    } else if (task->prio > pendowner->prio)
+	    return 0;
+    return 1;
+}
+
 /*
  * Try to take an rt-mutex
  *
@@ -763,8 +806,9 @@ static int rt_mutex_adjust_prio_chain(struct task_struct *task,
  * @waiter: The waiter that is queued to the lock's wait tree if the
  *	    callsite called task_blocked_on_lock(), otherwise NULL
  */
-static int try_to_take_rt_mutex(struct rt_mutex *lock, struct task_struct *task,
-				struct rt_mutex_waiter *waiter)
+static int __try_to_take_rt_mutex(struct rt_mutex *lock,
+				  struct task_struct *task,
+				  struct rt_mutex_waiter *waiter, int mode)
 {
 	unsigned long flags;
 
@@ -803,8 +847,10 @@ static int try_to_take_rt_mutex(struct rt_mutex *lock, struct task_struct *task,
 		 * If waiter is not the highest priority waiter of
 		 * @lock, give up.
 		 */
-		if (waiter != rt_mutex_top_waiter(lock))
+		if (waiter != rt_mutex_top_waiter(lock)) {
+			/* XXX lock_is_stealable() ? */
 			return 0;
+		}
 
 		/*
 		 * We can acquire the lock. Remove the waiter from the
@@ -822,14 +868,10 @@ static int try_to_take_rt_mutex(struct rt_mutex *lock, struct task_struct *task,
 		 * not need to be dequeued.
 		 */
 		if (rt_mutex_has_waiters(lock)) {
-			/*
-			 * If @task->prio is greater than or equal to
-			 * the top waiter priority (kernel view),
-			 * @task lost.
-			 */
-			if (task->prio >= rt_mutex_top_waiter(lock)->prio)
-				return 0;
+			struct task_struct *pown = rt_mutex_top_waiter(lock)->task;
 
+			if (task != pown && !lock_is_stealable(task, pown, mode))
+				return 0;
 			/*
 			 * The current top waiter stays enqueued. We
 			 * don't have to change anything in the lock
@@ -878,6 +920,354 @@ static int try_to_take_rt_mutex(struct rt_mutex *lock, struct task_struct *task,
 	return 1;
 }
 
+#ifdef CONFIG_PREEMPT_RT_FULL
+/*
+ * preemptible spin_lock functions:
+ */
+static inline void rt_spin_lock_fastlock(struct rt_mutex *lock,
+					 void  (*slowfn)(struct rt_mutex *lock))
+{
+	might_sleep_no_state_check();
+
+	if (likely(rt_mutex_cmpxchg_acquire(lock, NULL, current)))
+		rt_mutex_deadlock_account_lock(lock, current);
+	else
+		slowfn(lock);
+}
+
+static inline void rt_spin_lock_fastunlock(struct rt_mutex *lock,
+					   void  (*slowfn)(struct rt_mutex *lock))
+{
+	if (likely(rt_mutex_cmpxchg_release(lock, current, NULL)))
+		rt_mutex_deadlock_account_unlock(current);
+	else
+		slowfn(lock);
+}
+#ifdef CONFIG_SMP
+/*
+ * Note that owner is a speculative pointer and dereferencing relies
+ * on rcu_read_lock() and the check against the lock owner.
+ */
+static int adaptive_wait(struct rt_mutex *lock,
+			 struct task_struct *owner)
+{
+	int res = 0;
+
+	rcu_read_lock();
+	for (;;) {
+		if (owner != rt_mutex_owner(lock))
+			break;
+		/*
+		 * Ensure that owner->on_cpu is dereferenced _after_
+		 * checking the above to be valid.
+		 */
+		barrier();
+		if (!owner->on_cpu) {
+			res = 1;
+			break;
+		}
+		cpu_relax();
+	}
+	rcu_read_unlock();
+	return res;
+}
+#else
+static int adaptive_wait(struct rt_mutex *lock,
+			 struct task_struct *orig_owner)
+{
+	return 1;
+}
+#endif
+
+# define pi_lock(lock)		raw_spin_lock_irq(lock)
+# define pi_unlock(lock)	raw_spin_unlock_irq(lock)
+
+static int task_blocks_on_rt_mutex(struct rt_mutex *lock,
+				   struct rt_mutex_waiter *waiter,
+				   struct task_struct *task,
+				   enum rtmutex_chainwalk chwalk);
+/*
+ * Slow path lock function spin_lock style: this variant is very
+ * careful not to miss any non-lock wakeups.
+ *
+ * We store the current state under p->pi_lock in p->saved_state and
+ * the try_to_wake_up() code handles this accordingly.
+ */
+static void  noinline __sched rt_spin_lock_slowlock(struct rt_mutex *lock)
+{
+	struct task_struct *lock_owner, *self = current;
+	struct rt_mutex_waiter waiter, *top_waiter;
+	int ret;
+
+	rt_mutex_init_waiter(&waiter, true);
+
+	raw_spin_lock(&lock->wait_lock);
+
+	if (__try_to_take_rt_mutex(lock, self, NULL, STEAL_LATERAL)) {
+		raw_spin_unlock(&lock->wait_lock);
+		return;
+	}
+
+	BUG_ON(rt_mutex_owner(lock) == self);
+
+	/*
+	 * We save whatever state the task is in and we'll restore it
+	 * after acquiring the lock taking real wakeups into account
+	 * as well. We are serialized via pi_lock against wakeups. See
+	 * try_to_wake_up().
+	 */
+	pi_lock(&self->pi_lock);
+	self->saved_state = self->state;
+	__set_current_state_no_track(TASK_UNINTERRUPTIBLE);
+	pi_unlock(&self->pi_lock);
+
+	ret = task_blocks_on_rt_mutex(lock, &waiter, self, RT_MUTEX_MIN_CHAINWALK);
+	BUG_ON(ret);
+
+	for (;;) {
+		/* Try to acquire the lock again. */
+		if (__try_to_take_rt_mutex(lock, self, &waiter, STEAL_LATERAL))
+			break;
+
+		top_waiter = rt_mutex_top_waiter(lock);
+		lock_owner = rt_mutex_owner(lock);
+
+		raw_spin_unlock(&lock->wait_lock);
+
+		debug_rt_mutex_print_deadlock(&waiter);
+
+		if (top_waiter != &waiter || adaptive_wait(lock, lock_owner))
+			schedule();
+
+		raw_spin_lock(&lock->wait_lock);
+
+		pi_lock(&self->pi_lock);
+		__set_current_state_no_track(TASK_UNINTERRUPTIBLE);
+		pi_unlock(&self->pi_lock);
+	}
+
+	/*
+	 * Restore the task state to current->saved_state. We set it
+	 * to the original state above and the try_to_wake_up() code
+	 * has possibly updated it when a real (non-rtmutex) wakeup
+	 * happened while we were blocked. Clear saved_state so
+	 * try_to_wakeup() does not get confused.
+	 */
+	pi_lock(&self->pi_lock);
+	__set_current_state_no_track(self->saved_state);
+	self->saved_state = TASK_RUNNING;
+	pi_unlock(&self->pi_lock);
+
+	/*
+	 * try_to_take_rt_mutex() sets the waiter bit
+	 * unconditionally. We might have to fix that up:
+	 */
+	fixup_rt_mutex_waiters(lock);
+
+	BUG_ON(rt_mutex_has_waiters(lock) && &waiter == rt_mutex_top_waiter(lock));
+	BUG_ON(!RB_EMPTY_NODE(&waiter.tree_entry));
+
+	raw_spin_unlock(&lock->wait_lock);
+
+	debug_rt_mutex_free_waiter(&waiter);
+}
+
+static void mark_wakeup_next_waiter(struct wake_q_head *wake_q,
+				    struct wake_q_head *wake_sleeper_q,
+				    struct rt_mutex *lock);
+/*
+ * Slow path to release a rt_mutex spin_lock style
+ */
+static void  noinline __sched rt_spin_lock_slowunlock(struct rt_mutex *lock)
+{
+	WAKE_Q(wake_q);
+	WAKE_Q(wake_sleeper_q);
+
+	raw_spin_lock(&lock->wait_lock);
+
+	debug_rt_mutex_unlock(lock);
+
+	rt_mutex_deadlock_account_unlock(current);
+
+	if (!rt_mutex_has_waiters(lock)) {
+		lock->owner = NULL;
+		raw_spin_unlock(&lock->wait_lock);
+		return;
+	}
+
+	mark_wakeup_next_waiter(&wake_q, &wake_sleeper_q, lock);
+
+	raw_spin_unlock(&lock->wait_lock);
+	wake_up_q(&wake_q);
+	wake_up_q_sleeper(&wake_sleeper_q);
+
+	/* Undo pi boosting.when necessary */
+	rt_mutex_adjust_prio(current);
+}
+
+void __lockfunc rt_spin_lock(spinlock_t *lock)
+{
+	rt_spin_lock_fastlock(&lock->lock, rt_spin_lock_slowlock);
+	spin_acquire(&lock->dep_map, 0, 0, _RET_IP_);
+}
+EXPORT_SYMBOL(rt_spin_lock);
+
+void __lockfunc __rt_spin_lock(struct rt_mutex *lock)
+{
+	rt_spin_lock_fastlock(lock, rt_spin_lock_slowlock);
+}
+EXPORT_SYMBOL(__rt_spin_lock);
+
+#ifdef CONFIG_DEBUG_LOCK_ALLOC
+void __lockfunc rt_spin_lock_nested(spinlock_t *lock, int subclass)
+{
+	rt_spin_lock_fastlock(&lock->lock, rt_spin_lock_slowlock);
+	spin_acquire(&lock->dep_map, subclass, 0, _RET_IP_);
+}
+EXPORT_SYMBOL(rt_spin_lock_nested);
+#endif
+
+void __lockfunc rt_spin_unlock(spinlock_t *lock)
+{
+	/* NOTE: we always pass in '1' for nested, for simplicity */
+	spin_release(&lock->dep_map, 1, _RET_IP_);
+	rt_spin_lock_fastunlock(&lock->lock, rt_spin_lock_slowunlock);
+}
+EXPORT_SYMBOL(rt_spin_unlock);
+
+void __lockfunc __rt_spin_unlock(struct rt_mutex *lock)
+{
+	rt_spin_lock_fastunlock(lock, rt_spin_lock_slowunlock);
+}
+EXPORT_SYMBOL(__rt_spin_unlock);
+
+/*
+ * Wait for the lock to get unlocked: instead of polling for an unlock
+ * (like raw spinlocks do), we lock and unlock, to force the kernel to
+ * schedule if there's contention:
+ */
+void __lockfunc rt_spin_unlock_wait(spinlock_t *lock)
+{
+	spin_lock(lock);
+	spin_unlock(lock);
+}
+EXPORT_SYMBOL(rt_spin_unlock_wait);
+
+int __lockfunc __rt_spin_trylock(struct rt_mutex *lock)
+{
+	return rt_mutex_trylock(lock);
+}
+
+int __lockfunc rt_spin_trylock(spinlock_t *lock)
+{
+	int ret = rt_mutex_trylock(&lock->lock);
+
+	if (ret)
+		spin_acquire(&lock->dep_map, 0, 1, _RET_IP_);
+	return ret;
+}
+EXPORT_SYMBOL(rt_spin_trylock);
+
+int __lockfunc rt_spin_trylock_bh(spinlock_t *lock)
+{
+	int ret;
+
+	local_bh_disable();
+	ret = rt_mutex_trylock(&lock->lock);
+	if (ret) {
+		migrate_disable();
+		spin_acquire(&lock->dep_map, 0, 1, _RET_IP_);
+	} else
+		local_bh_enable();
+	return ret;
+}
+EXPORT_SYMBOL(rt_spin_trylock_bh);
+
+int __lockfunc rt_spin_trylock_irqsave(spinlock_t *lock, unsigned long *flags)
+{
+	int ret;
+
+	*flags = 0;
+	ret = rt_mutex_trylock(&lock->lock);
+	if (ret) {
+		migrate_disable();
+		spin_acquire(&lock->dep_map, 0, 1, _RET_IP_);
+	}
+	return ret;
+}
+EXPORT_SYMBOL(rt_spin_trylock_irqsave);
+
+int atomic_dec_and_spin_lock(atomic_t *atomic, spinlock_t *lock)
+{
+	/* Subtract 1 from counter unless that drops it to 0 (ie. it was 1) */
+	if (atomic_add_unless(atomic, -1, 1))
+		return 0;
+	migrate_disable();
+	rt_spin_lock(lock);
+	if (atomic_dec_and_test(atomic))
+		return 1;
+	rt_spin_unlock(lock);
+	migrate_enable();
+	return 0;
+}
+EXPORT_SYMBOL(atomic_dec_and_spin_lock);
+
+	void
+__rt_spin_lock_init(spinlock_t *lock, char *name, struct lock_class_key *key)
+{
+#ifdef CONFIG_DEBUG_LOCK_ALLOC
+	/*
+	 * Make sure we are not reinitializing a held lock:
+	 */
+	debug_check_no_locks_freed((void *)lock, sizeof(*lock));
+	lockdep_init_map(&lock->dep_map, name, key, 0);
+#endif
+}
+EXPORT_SYMBOL(__rt_spin_lock_init);
+
+#endif /* PREEMPT_RT_FULL */
+
+#ifdef CONFIG_PREEMPT_RT_FULL
+	static inline int __sched
+__mutex_lock_check_stamp(struct rt_mutex *lock, struct ww_acquire_ctx *ctx)
+{
+	struct ww_mutex *ww = container_of(lock, struct ww_mutex, base.lock);
+	struct ww_acquire_ctx *hold_ctx = ACCESS_ONCE(ww->ctx);
+
+	if (!hold_ctx)
+		return 0;
+
+	if (unlikely(ctx == hold_ctx))
+		return -EALREADY;
+
+	if (ctx->stamp - hold_ctx->stamp <= LONG_MAX &&
+	    (ctx->stamp != hold_ctx->stamp || ctx > hold_ctx)) {
+#ifdef CONFIG_DEBUG_MUTEXES
+		DEBUG_LOCKS_WARN_ON(ctx->contending_lock);
+		ctx->contending_lock = ww;
+#endif
+		return -EDEADLK;
+	}
+
+	return 0;
+}
+#else
+	static inline int __sched
+__mutex_lock_check_stamp(struct rt_mutex *lock, struct ww_acquire_ctx *ctx)
+{
+	BUG();
+	return 0;
+}
+
+#endif
+
+static inline int
+try_to_take_rt_mutex(struct rt_mutex *lock, struct task_struct *task,
+		     struct rt_mutex_waiter *waiter)
+{
+	return __try_to_take_rt_mutex(lock, task, waiter, STEAL_NORMAL);
+}
+
 /*
  * Task blocks on lock.
  *
@@ -909,6 +1299,23 @@ static int task_blocks_on_rt_mutex(struct rt_mutex *lock,
 		return -EDEADLK;
 
 	raw_spin_lock_irqsave(&task->pi_lock, flags);
+
+	/*
+	 * In the case of futex requeue PI, this will be a proxy
+	 * lock. The task will wake unaware that it is enqueueed on
+	 * this lock. Avoid blocking on two locks and corrupting
+	 * pi_blocked_on via the PI_WAKEUP_INPROGRESS
+	 * flag. futex_wait_requeue_pi() sets this when it wakes up
+	 * before requeue (due to a signal or timeout). Do not enqueue
+	 * the task if PI_WAKEUP_INPROGRESS is set.
+	 */
+	if (task != current && task->pi_blocked_on == PI_WAKEUP_INPROGRESS) {
+		raw_spin_unlock_irqrestore(&task->pi_lock, flags);
+		return -EAGAIN;
+	}
+
+	BUG_ON(rt_mutex_real_waiter(task->pi_blocked_on));
+
 	__rt_mutex_adjust_prio(task);
 	waiter->task = task;
 	waiter->lock = lock;
@@ -932,7 +1339,7 @@ static int task_blocks_on_rt_mutex(struct rt_mutex *lock,
 		rt_mutex_enqueue_pi(owner, waiter);
 
 		__rt_mutex_adjust_prio(owner);
-		if (owner->pi_blocked_on)
+		if (rt_mutex_real_waiter(owner->pi_blocked_on))
 			chain_walk = 1;
 	} else if (rt_mutex_cond_detect_deadlock(waiter, chwalk)) {
 		chain_walk = 1;
@@ -974,6 +1381,7 @@ static int task_blocks_on_rt_mutex(struct rt_mutex *lock,
  * Called with lock->wait_lock held.
  */
 static void mark_wakeup_next_waiter(struct wake_q_head *wake_q,
+				    struct wake_q_head *wake_sleeper_q,
 				    struct rt_mutex *lock)
 {
 	struct rt_mutex_waiter *waiter;
@@ -1003,7 +1411,10 @@ static void mark_wakeup_next_waiter(struct wake_q_head *wake_q,
 
 	raw_spin_unlock_irqrestore(&current->pi_lock, flags);
 
-	wake_q_add(wake_q, waiter->task);
+	if (waiter->savestate)
+		wake_q_add(wake_sleeper_q, waiter->task);
+	else
+		wake_q_add(wake_q, waiter->task);
 }
 
 /*
@@ -1017,7 +1428,7 @@ static void remove_waiter(struct rt_mutex *lock,
 {
 	bool is_top_waiter = (waiter == rt_mutex_top_waiter(lock));
 	struct task_struct *owner = rt_mutex_owner(lock);
-	struct rt_mutex *next_lock;
+	struct rt_mutex *next_lock = NULL;
 	unsigned long flags;
 
 	raw_spin_lock_irqsave(&current->pi_lock, flags);
@@ -1042,7 +1453,8 @@ static void remove_waiter(struct rt_mutex *lock,
 	__rt_mutex_adjust_prio(owner);
 
 	/* Store the lock on which owner is blocked or NULL */
-	next_lock = task_blocked_on_lock(owner);
+	if (rt_mutex_real_waiter(owner->pi_blocked_on))
+		next_lock = task_blocked_on_lock(owner);
 
 	raw_spin_unlock_irqrestore(&owner->pi_lock, flags);
 
@@ -1078,17 +1490,17 @@ void rt_mutex_adjust_pi(struct task_struct *task)
 	raw_spin_lock_irqsave(&task->pi_lock, flags);
 
 	waiter = task->pi_blocked_on;
-	if (!waiter || (waiter->prio == task->prio &&
+	if (!rt_mutex_real_waiter(waiter) || (waiter->prio == task->prio &&
 			!dl_prio(task->prio))) {
 		raw_spin_unlock_irqrestore(&task->pi_lock, flags);
 		return;
 	}
 	next_lock = waiter->lock;
-	raw_spin_unlock_irqrestore(&task->pi_lock, flags);
 
 	/* gets dropped in rt_mutex_adjust_prio_chain()! */
 	get_task_struct(task);
 
+	raw_spin_unlock_irqrestore(&task->pi_lock, flags);
 	rt_mutex_adjust_prio_chain(task, RT_MUTEX_MIN_CHAINWALK, NULL,
 				   next_lock, NULL, task);
 }
@@ -1106,7 +1518,8 @@ void rt_mutex_adjust_pi(struct task_struct *task)
 static int __sched
 __rt_mutex_slowlock(struct rt_mutex *lock, int state,
 		    struct hrtimer_sleeper *timeout,
-		    struct rt_mutex_waiter *waiter)
+		    struct rt_mutex_waiter *waiter,
+		    struct ww_acquire_ctx *ww_ctx)
 {
 	int ret = 0;
 
@@ -1129,6 +1542,12 @@ __rt_mutex_slowlock(struct rt_mutex *lock, int state,
 				break;
 		}
 
+		if (ww_ctx && ww_ctx->acquired > 0) {
+			ret = __mutex_lock_check_stamp(lock, ww_ctx);
+			if (ret)
+				break;
+		}
+
 		raw_spin_unlock(&lock->wait_lock);
 
 		debug_rt_mutex_print_deadlock(waiter);
@@ -1163,25 +1582,102 @@ static void rt_mutex_handle_deadlock(int res, int detect_deadlock,
 	}
 }
 
+static __always_inline void ww_mutex_lock_acquired(struct ww_mutex *ww,
+						   struct ww_acquire_ctx *ww_ctx)
+{
+#ifdef CONFIG_DEBUG_MUTEXES
+	/*
+	 * If this WARN_ON triggers, you used ww_mutex_lock to acquire,
+	 * but released with a normal mutex_unlock in this call.
+	 *
+	 * This should never happen, always use ww_mutex_unlock.
+	 */
+	DEBUG_LOCKS_WARN_ON(ww->ctx);
+
+	/*
+	 * Not quite done after calling ww_acquire_done() ?
+	 */
+	DEBUG_LOCKS_WARN_ON(ww_ctx->done_acquire);
+
+	if (ww_ctx->contending_lock) {
+		/*
+		 * After -EDEADLK you tried to
+		 * acquire a different ww_mutex? Bad!
+		 */
+		DEBUG_LOCKS_WARN_ON(ww_ctx->contending_lock != ww);
+
+		/*
+		 * You called ww_mutex_lock after receiving -EDEADLK,
+		 * but 'forgot' to unlock everything else first?
+		 */
+		DEBUG_LOCKS_WARN_ON(ww_ctx->acquired > 0);
+		ww_ctx->contending_lock = NULL;
+	}
+
+	/*
+	 * Naughty, using a different class will lead to undefined behavior!
+	 */
+	DEBUG_LOCKS_WARN_ON(ww_ctx->ww_class != ww->ww_class);
+#endif
+	ww_ctx->acquired++;
+}
+
+#ifdef CONFIG_PREEMPT_RT_FULL
+static void ww_mutex_account_lock(struct rt_mutex *lock,
+				  struct ww_acquire_ctx *ww_ctx)
+{
+	struct ww_mutex *ww = container_of(lock, struct ww_mutex, base.lock);
+	struct rt_mutex_waiter *waiter, *n;
+
+	/*
+	 * This branch gets optimized out for the common case,
+	 * and is only important for ww_mutex_lock.
+	 */
+	ww_mutex_lock_acquired(ww, ww_ctx);
+	ww->ctx = ww_ctx;
+
+	/*
+	 * Give any possible sleeping processes the chance to wake up,
+	 * so they can recheck if they have to back off.
+	 */
+	rbtree_postorder_for_each_entry_safe(waiter, n, &lock->waiters,
+					     tree_entry) {
+		/* XXX debug rt mutex waiter wakeup */
+
+		BUG_ON(waiter->lock != lock);
+		rt_mutex_wake_waiter(waiter);
+	}
+}
+
+#else
+
+static void ww_mutex_account_lock(struct rt_mutex *lock,
+				  struct ww_acquire_ctx *ww_ctx)
+{
+	BUG();
+}
+#endif
+
 /*
  * Slow path lock function:
  */
 static int __sched
 rt_mutex_slowlock(struct rt_mutex *lock, int state,
 		  struct hrtimer_sleeper *timeout,
-		  enum rtmutex_chainwalk chwalk)
+		  enum rtmutex_chainwalk chwalk,
+		  struct ww_acquire_ctx *ww_ctx)
 {
 	struct rt_mutex_waiter waiter;
 	int ret = 0;
 
-	debug_rt_mutex_init_waiter(&waiter);
-	RB_CLEAR_NODE(&waiter.pi_tree_entry);
-	RB_CLEAR_NODE(&waiter.tree_entry);
+	rt_mutex_init_waiter(&waiter, false);
 
 	raw_spin_lock(&lock->wait_lock);
 
 	/* Try to acquire the lock again: */
 	if (try_to_take_rt_mutex(lock, current, NULL)) {
+		if (ww_ctx)
+			ww_mutex_account_lock(lock, ww_ctx);
 		raw_spin_unlock(&lock->wait_lock);
 		return 0;
 	}
@@ -1196,13 +1692,23 @@ rt_mutex_slowlock(struct rt_mutex *lock, int state,
 
 	if (likely(!ret))
 		/* sleep on the mutex */
-		ret = __rt_mutex_slowlock(lock, state, timeout, &waiter);
+		ret = __rt_mutex_slowlock(lock, state, timeout, &waiter,
+					  ww_ctx);
+	else if (ww_ctx) {
+		/* ww_mutex received EDEADLK, let it become EALREADY */
+		ret = __mutex_lock_check_stamp(lock, ww_ctx);
+		BUG_ON(!ret);
+	}
 
 	if (unlikely(ret)) {
 		__set_current_state(TASK_RUNNING);
 		if (rt_mutex_has_waiters(lock))
 			remove_waiter(lock, &waiter);
-		rt_mutex_handle_deadlock(ret, chwalk, &waiter);
+		/* ww_mutex want to report EDEADLK/EALREADY, let them */
+		if (!ww_ctx)
+			rt_mutex_handle_deadlock(ret, chwalk, &waiter);
+	} else if (ww_ctx) {
+		ww_mutex_account_lock(lock, ww_ctx);
 	}
 
 	/*
@@ -1261,7 +1767,8 @@ static inline int rt_mutex_slowtrylock(struct rt_mutex *lock)
  * Return whether the current task needs to undo a potential priority boosting.
  */
 static bool __sched rt_mutex_slowunlock(struct rt_mutex *lock,
-					struct wake_q_head *wake_q)
+					struct wake_q_head *wake_q,
+					struct wake_q_head *wake_sleeper_q)
 {
 	raw_spin_lock(&lock->wait_lock);
 
@@ -1314,7 +1821,7 @@ static bool __sched rt_mutex_slowunlock(struct rt_mutex *lock,
 	 *
 	 * Queue the next waiter for wakeup once we release the wait_lock.
 	 */
-	mark_wakeup_next_waiter(wake_q, lock);
+	mark_wakeup_next_waiter(wake_q, wake_sleeper_q, lock);
 
 	raw_spin_unlock(&lock->wait_lock);
 
@@ -1330,31 +1837,36 @@ static bool __sched rt_mutex_slowunlock(struct rt_mutex *lock,
  */
 static inline int
 rt_mutex_fastlock(struct rt_mutex *lock, int state,
+		  struct ww_acquire_ctx *ww_ctx,
 		  int (*slowfn)(struct rt_mutex *lock, int state,
 				struct hrtimer_sleeper *timeout,
-				enum rtmutex_chainwalk chwalk))
+				enum rtmutex_chainwalk chwalk,
+				struct ww_acquire_ctx *ww_ctx))
 {
 	if (likely(rt_mutex_cmpxchg_acquire(lock, NULL, current))) {
 		rt_mutex_deadlock_account_lock(lock, current);
 		return 0;
 	} else
-		return slowfn(lock, state, NULL, RT_MUTEX_MIN_CHAINWALK);
+		return slowfn(lock, state, NULL, RT_MUTEX_MIN_CHAINWALK,
+			      ww_ctx);
 }
 
 static inline int
 rt_mutex_timed_fastlock(struct rt_mutex *lock, int state,
 			struct hrtimer_sleeper *timeout,
 			enum rtmutex_chainwalk chwalk,
+			struct ww_acquire_ctx *ww_ctx,
 			int (*slowfn)(struct rt_mutex *lock, int state,
 				      struct hrtimer_sleeper *timeout,
-				      enum rtmutex_chainwalk chwalk))
+				      enum rtmutex_chainwalk chwalk,
+				      struct ww_acquire_ctx *ww_ctx))
 {
 	if (chwalk == RT_MUTEX_MIN_CHAINWALK &&
 	    likely(rt_mutex_cmpxchg_acquire(lock, NULL, current))) {
 		rt_mutex_deadlock_account_lock(lock, current);
 		return 0;
 	} else
-		return slowfn(lock, state, timeout, chwalk);
+		return slowfn(lock, state, timeout, chwalk, ww_ctx);
 }
 
 static inline int
@@ -1371,17 +1883,20 @@ rt_mutex_fasttrylock(struct rt_mutex *lock,
 static inline void
 rt_mutex_fastunlock(struct rt_mutex *lock,
 		    bool (*slowfn)(struct rt_mutex *lock,
-				   struct wake_q_head *wqh))
+				   struct wake_q_head *wqh,
+				   struct wake_q_head *wq_sleeper))
 {
 	WAKE_Q(wake_q);
+	WAKE_Q(wake_sleeper_q);
 
 	if (likely(rt_mutex_cmpxchg_release(lock, current, NULL))) {
 		rt_mutex_deadlock_account_unlock(current);
 
 	} else {
-		bool deboost = slowfn(lock, &wake_q);
+		bool deboost = slowfn(lock, &wake_q, &wake_sleeper_q);
 
 		wake_up_q(&wake_q);
+		wake_up_q_sleeper(&wake_sleeper_q);
 
 		/* Undo pi boosting if necessary: */
 		if (deboost)
@@ -1398,7 +1913,7 @@ void __sched rt_mutex_lock(struct rt_mutex *lock)
 {
 	might_sleep();
 
-	rt_mutex_fastlock(lock, TASK_UNINTERRUPTIBLE, rt_mutex_slowlock);
+	rt_mutex_fastlock(lock, TASK_UNINTERRUPTIBLE, NULL, rt_mutex_slowlock);
 }
 EXPORT_SYMBOL_GPL(rt_mutex_lock);
 
@@ -1415,7 +1930,7 @@ int __sched rt_mutex_lock_interruptible(struct rt_mutex *lock)
 {
 	might_sleep();
 
-	return rt_mutex_fastlock(lock, TASK_INTERRUPTIBLE, rt_mutex_slowlock);
+	return rt_mutex_fastlock(lock, TASK_INTERRUPTIBLE, NULL, rt_mutex_slowlock);
 }
 EXPORT_SYMBOL_GPL(rt_mutex_lock_interruptible);
 
@@ -1428,11 +1943,30 @@ int rt_mutex_timed_futex_lock(struct rt_mutex *lock,
 	might_sleep();
 
 	return rt_mutex_timed_fastlock(lock, TASK_INTERRUPTIBLE, timeout,
-				       RT_MUTEX_FULL_CHAINWALK,
+				       RT_MUTEX_FULL_CHAINWALK, NULL,
 				       rt_mutex_slowlock);
 }
 
 /**
+ * rt_mutex_lock_killable - lock a rt_mutex killable
+ *
+ * @lock:              the rt_mutex to be locked
+ * @detect_deadlock:   deadlock detection on/off
+ *
+ * Returns:
+ *  0          on success
+ * -EINTR      when interrupted by a signal
+ * -EDEADLK    when the lock would deadlock (when deadlock detection is on)
+ */
+int __sched rt_mutex_lock_killable(struct rt_mutex *lock)
+{
+	might_sleep();
+
+	return rt_mutex_fastlock(lock, TASK_KILLABLE, NULL, rt_mutex_slowlock);
+}
+EXPORT_SYMBOL_GPL(rt_mutex_lock_killable);
+
+/**
  * rt_mutex_timed_lock - lock a rt_mutex interruptible
  *			the timeout structure is provided
  *			by the caller
@@ -1452,6 +1986,7 @@ rt_mutex_timed_lock(struct rt_mutex *lock, struct hrtimer_sleeper *timeout)
 
 	return rt_mutex_timed_fastlock(lock, TASK_INTERRUPTIBLE, timeout,
 				       RT_MUTEX_MIN_CHAINWALK,
+				       NULL,
 				       rt_mutex_slowlock);
 }
 EXPORT_SYMBOL_GPL(rt_mutex_timed_lock);
@@ -1469,7 +2004,11 @@ EXPORT_SYMBOL_GPL(rt_mutex_timed_lock);
  */
 int __sched rt_mutex_trylock(struct rt_mutex *lock)
 {
+#ifdef CONFIG_PREEMPT_RT_FULL
+	if (WARN_ON(in_irq() || in_nmi()))
+#else
 	if (WARN_ON(in_irq() || in_nmi() || in_serving_softirq()))
+#endif
 		return 0;
 
 	return rt_mutex_fasttrylock(lock, rt_mutex_slowtrylock);
@@ -1495,13 +2034,14 @@ EXPORT_SYMBOL_GPL(rt_mutex_unlock);
  * required or not.
  */
 bool __sched rt_mutex_futex_unlock(struct rt_mutex *lock,
-				   struct wake_q_head *wqh)
+				   struct wake_q_head *wqh,
+				   struct wake_q_head *wq_sleeper)
 {
 	if (likely(rt_mutex_cmpxchg_release(lock, current, NULL))) {
 		rt_mutex_deadlock_account_unlock(current);
 		return false;
 	}
-	return rt_mutex_slowunlock(lock, wqh);
+	return rt_mutex_slowunlock(lock, wqh, wq_sleeper);
 }
 
 /**
@@ -1534,13 +2074,12 @@ EXPORT_SYMBOL_GPL(rt_mutex_destroy);
 void __rt_mutex_init(struct rt_mutex *lock, const char *name)
 {
 	lock->owner = NULL;
-	raw_spin_lock_init(&lock->wait_lock);
 	lock->waiters = RB_ROOT;
 	lock->waiters_leftmost = NULL;
 
 	debug_rt_mutex_init(lock, name);
 }
-EXPORT_SYMBOL_GPL(__rt_mutex_init);
+EXPORT_SYMBOL(__rt_mutex_init);
 
 /**
  * rt_mutex_init_proxy_locked - initialize and lock a rt_mutex on behalf of a
@@ -1555,7 +2094,7 @@ EXPORT_SYMBOL_GPL(__rt_mutex_init);
 void rt_mutex_init_proxy_locked(struct rt_mutex *lock,
 				struct task_struct *proxy_owner)
 {
-	__rt_mutex_init(lock, NULL);
+	rt_mutex_init(lock);
 	debug_rt_mutex_proxy_lock(lock, proxy_owner);
 	rt_mutex_set_owner(lock, proxy_owner);
 	rt_mutex_deadlock_account_lock(lock, proxy_owner);
@@ -1603,6 +2142,35 @@ int rt_mutex_start_proxy_lock(struct rt_mutex *lock,
 		return 1;
 	}
 
+#ifdef CONFIG_PREEMPT_RT_FULL
+	/*
+	 * In PREEMPT_RT there's an added race.
+	 * If the task, that we are about to requeue, times out,
+	 * it can set the PI_WAKEUP_INPROGRESS. This tells the requeue
+	 * to skip this task. But right after the task sets
+	 * its pi_blocked_on to PI_WAKEUP_INPROGRESS it can then
+	 * block on the spin_lock(&hb->lock), which in RT is an rtmutex.
+	 * This will replace the PI_WAKEUP_INPROGRESS with the actual
+	 * lock that it blocks on. We *must not* place this task
+	 * on this proxy lock in that case.
+	 *
+	 * To prevent this race, we first take the task's pi_lock
+	 * and check if it has updated its pi_blocked_on. If it has,
+	 * we assume that it woke up and we return -EAGAIN.
+	 * Otherwise, we set the task's pi_blocked_on to
+	 * PI_REQUEUE_INPROGRESS, so that if the task is waking up
+	 * it will know that we are in the process of requeuing it.
+	 */
+	raw_spin_lock_irq(&task->pi_lock);
+	if (task->pi_blocked_on) {
+		raw_spin_unlock_irq(&task->pi_lock);
+		raw_spin_unlock(&lock->wait_lock);
+		return -EAGAIN;
+	}
+	task->pi_blocked_on = PI_REQUEUE_INPROGRESS;
+	raw_spin_unlock_irq(&task->pi_lock);
+#endif
+
 	/* We enforce deadlock detection for futexes */
 	ret = task_blocks_on_rt_mutex(lock, waiter, task,
 				      RT_MUTEX_FULL_CHAINWALK);
@@ -1617,7 +2185,7 @@ int rt_mutex_start_proxy_lock(struct rt_mutex *lock,
 		ret = 0;
 	}
 
-	if (unlikely(ret))
+	if (ret && rt_mutex_has_waiters(lock))
 		remove_waiter(lock, waiter);
 
 	raw_spin_unlock(&lock->wait_lock);
@@ -1673,7 +2241,7 @@ int rt_mutex_finish_proxy_lock(struct rt_mutex *lock,
 	set_current_state(TASK_INTERRUPTIBLE);
 
 	/* sleep on the mutex */
-	ret = __rt_mutex_slowlock(lock, TASK_INTERRUPTIBLE, to, waiter);
+	ret = __rt_mutex_slowlock(lock, TASK_INTERRUPTIBLE, to, waiter, NULL);
 
 	if (unlikely(ret))
 		remove_waiter(lock, waiter);
@@ -1688,3 +2256,89 @@ int rt_mutex_finish_proxy_lock(struct rt_mutex *lock,
 
 	return ret;
 }
+
+static inline int
+ww_mutex_deadlock_injection(struct ww_mutex *lock, struct ww_acquire_ctx *ctx)
+{
+#ifdef CONFIG_DEBUG_WW_MUTEX_SLOWPATH
+	unsigned tmp;
+
+	if (ctx->deadlock_inject_countdown-- == 0) {
+		tmp = ctx->deadlock_inject_interval;
+		if (tmp > UINT_MAX/4)
+			tmp = UINT_MAX;
+		else
+			tmp = tmp*2 + tmp + tmp/2;
+
+		ctx->deadlock_inject_interval = tmp;
+		ctx->deadlock_inject_countdown = tmp;
+		ctx->contending_lock = lock;
+
+		ww_mutex_unlock(lock);
+
+		return -EDEADLK;
+	}
+#endif
+
+	return 0;
+}
+
+#ifdef CONFIG_PREEMPT_RT_FULL
+int __sched
+__ww_mutex_lock_interruptible(struct ww_mutex *lock, struct ww_acquire_ctx *ww_ctx)
+{
+	int ret;
+
+	might_sleep();
+
+	mutex_acquire_nest(&lock->base.dep_map, 0, 0, &ww_ctx->dep_map, _RET_IP_);
+	ret = rt_mutex_slowlock(&lock->base.lock, TASK_INTERRUPTIBLE, NULL, 0, ww_ctx);
+	if (ret)
+		mutex_release(&lock->base.dep_map, 1, _RET_IP_);
+	else if (!ret && ww_ctx->acquired > 1)
+		return ww_mutex_deadlock_injection(lock, ww_ctx);
+
+	return ret;
+}
+EXPORT_SYMBOL_GPL(__ww_mutex_lock_interruptible);
+
+int __sched
+__ww_mutex_lock(struct ww_mutex *lock, struct ww_acquire_ctx *ww_ctx)
+{
+	int ret;
+
+	might_sleep();
+
+	mutex_acquire_nest(&lock->base.dep_map, 0, 0, &ww_ctx->dep_map, _RET_IP_);
+	ret = rt_mutex_slowlock(&lock->base.lock, TASK_UNINTERRUPTIBLE, NULL, 0, ww_ctx);
+	if (ret)
+		mutex_release(&lock->base.dep_map, 1, _RET_IP_);
+	else if (!ret && ww_ctx->acquired > 1)
+		return ww_mutex_deadlock_injection(lock, ww_ctx);
+
+	return ret;
+}
+EXPORT_SYMBOL_GPL(__ww_mutex_lock);
+
+void __sched ww_mutex_unlock(struct ww_mutex *lock)
+{
+	int nest = !!lock->ctx;
+
+	/*
+	 * The unlocking fastpath is the 0->1 transition from 'locked'
+	 * into 'unlocked' state:
+	 */
+	if (nest) {
+#ifdef CONFIG_DEBUG_MUTEXES
+		DEBUG_LOCKS_WARN_ON(!lock->ctx->acquired);
+#endif
+		if (lock->ctx->acquired > 0)
+			lock->ctx->acquired--;
+		lock->ctx = NULL;
+	}
+
+	mutex_release(&lock->base.dep_map, nest, _RET_IP_);
+	rt_mutex_unlock(&lock->base.lock);
+}
+EXPORT_SYMBOL(ww_mutex_unlock);
+#endif
diff --git a/kernel/locking/rtmutex_common.h b/kernel/locking/rtmutex_common.h
index 4f5f83c7d2d3..289f062f26cd 100644
--- a/kernel/locking/rtmutex_common.h
+++ b/kernel/locking/rtmutex_common.h
@@ -27,6 +27,7 @@ struct rt_mutex_waiter {
 	struct rb_node          pi_tree_entry;
 	struct task_struct	*task;
 	struct rt_mutex		*lock;
+	bool			savestate;
 #ifdef CONFIG_DEBUG_RT_MUTEXES
 	unsigned long		ip;
 	struct pid		*deadlock_task_pid;
@@ -97,6 +98,9 @@ enum rtmutex_chainwalk {
 /*
  * PI-futex support (proxy locking functions, etc.):
  */
+#define PI_WAKEUP_INPROGRESS	((struct rt_mutex_waiter *) 1)
+#define PI_REQUEUE_INPROGRESS	((struct rt_mutex_waiter *) 2)
+
 extern struct task_struct *rt_mutex_next_owner(struct rt_mutex *lock);
 extern void rt_mutex_init_proxy_locked(struct rt_mutex *lock,
 				       struct task_struct *proxy_owner);
@@ -110,7 +114,8 @@ extern int rt_mutex_finish_proxy_lock(struct rt_mutex *lock,
 				      struct rt_mutex_waiter *waiter);
 extern int rt_mutex_timed_futex_lock(struct rt_mutex *l, struct hrtimer_sleeper *to);
 extern bool rt_mutex_futex_unlock(struct rt_mutex *lock,
-				  struct wake_q_head *wqh);
+				  struct wake_q_head *wqh,
+				  struct wake_q_head *wq_sleeper);
 extern void rt_mutex_adjust_prio(struct task_struct *task);
 
 #ifdef CONFIG_DEBUG_RT_MUTEXES
@@ -119,4 +124,14 @@ extern void rt_mutex_adjust_prio(struct task_struct *task);
 # include "rtmutex.h"
 #endif
 
+static inline void
+rt_mutex_init_waiter(struct rt_mutex_waiter *waiter, bool savestate)
+{
+	debug_rt_mutex_init_waiter(waiter);
+	waiter->task = NULL;
+	waiter->savestate = savestate;
+	RB_CLEAR_NODE(&waiter->pi_tree_entry);
+	RB_CLEAR_NODE(&waiter->tree_entry);
+}
+
 #endif
diff --git a/kernel/locking/spinlock.c b/kernel/locking/spinlock.c
index db3ccb1dd614..909779647bd1 100644
--- a/kernel/locking/spinlock.c
+++ b/kernel/locking/spinlock.c
@@ -124,8 +124,11 @@ void __lockfunc __raw_##op##_lock_bh(locktype##_t *lock)		\
  *         __[spin|read|write]_lock_bh()
  */
 BUILD_LOCK_OPS(spin, raw_spinlock);
+
+#ifndef CONFIG_PREEMPT_RT_FULL
 BUILD_LOCK_OPS(read, rwlock);
 BUILD_LOCK_OPS(write, rwlock);
+#endif
 
 #endif
 
@@ -209,6 +212,8 @@ void __lockfunc _raw_spin_unlock_bh(raw_spinlock_t *lock)
 EXPORT_SYMBOL(_raw_spin_unlock_bh);
 #endif
 
+#ifndef CONFIG_PREEMPT_RT_FULL
+
 #ifndef CONFIG_INLINE_READ_TRYLOCK
 int __lockfunc _raw_read_trylock(rwlock_t *lock)
 {
@@ -353,6 +358,8 @@ void __lockfunc _raw_write_unlock_bh(rwlock_t *lock)
 EXPORT_SYMBOL(_raw_write_unlock_bh);
 #endif
 
+#endif /* !PREEMPT_RT_FULL */
+
 #ifdef CONFIG_DEBUG_LOCK_ALLOC
 
 void __lockfunc _raw_spin_lock_nested(raw_spinlock_t *lock, int subclass)
diff --git a/kernel/locking/spinlock_debug.c b/kernel/locking/spinlock_debug.c
index 0374a596cffa..94970338d518 100644
--- a/kernel/locking/spinlock_debug.c
+++ b/kernel/locking/spinlock_debug.c
@@ -31,6 +31,7 @@ void __raw_spin_lock_init(raw_spinlock_t *lock, const char *name,
 
 EXPORT_SYMBOL(__raw_spin_lock_init);
 
+#ifndef CONFIG_PREEMPT_RT_FULL
 void __rwlock_init(rwlock_t *lock, const char *name,
 		   struct lock_class_key *key)
 {
@@ -48,6 +49,7 @@ void __rwlock_init(rwlock_t *lock, const char *name,
 }
 
 EXPORT_SYMBOL(__rwlock_init);
+#endif
 
 static void spin_dump(raw_spinlock_t *lock, const char *msg)
 {
@@ -159,6 +161,7 @@ void do_raw_spin_unlock(raw_spinlock_t *lock)
 	arch_spin_unlock(&lock->raw_lock);
 }
 
+#ifndef CONFIG_PREEMPT_RT_FULL
 static void rwlock_bug(rwlock_t *lock, const char *msg)
 {
 	if (!debug_locks_off())
@@ -300,3 +303,5 @@ void do_raw_write_unlock(rwlock_t *lock)
 	debug_write_unlock(lock);
 	arch_write_unlock(&lock->raw_lock);
 }
+
+#endif
diff --git a/kernel/panic.c b/kernel/panic.c
index 4b150bc0c6c1..d74f25df0b32 100644
--- a/kernel/panic.c
+++ b/kernel/panic.c
@@ -401,9 +401,11 @@ static u64 oops_id;
 
 static int init_oops_id(void)
 {
+#ifndef CONFIG_PREEMPT_RT_FULL
 	if (!oops_id)
 		get_random_bytes(&oops_id, sizeof(oops_id));
 	else
+#endif
 		oops_id++;
 
 	return 0;
diff --git a/kernel/power/hibernate.c b/kernel/power/hibernate.c
index b7342a24f559..bfd9e0982f15 100644
--- a/kernel/power/hibernate.c
+++ b/kernel/power/hibernate.c
@@ -285,6 +285,8 @@ static int create_image(int platform_mode)
 
 	local_irq_disable();
 
+	system_state = SYSTEM_SUSPEND;
+
 	error = syscore_suspend();
 	if (error) {
 		printk(KERN_ERR "PM: Some system devices failed to power down, "
@@ -314,6 +316,7 @@ static int create_image(int platform_mode)
 	syscore_resume();
 
  Enable_irqs:
+	system_state = SYSTEM_RUNNING;
 	local_irq_enable();
 
  Enable_cpus:
@@ -437,6 +440,7 @@ static int resume_target_kernel(bool platform_mode)
 		goto Enable_cpus;
 
 	local_irq_disable();
+	system_state = SYSTEM_SUSPEND;
 
 	error = syscore_suspend();
 	if (error)
@@ -470,6 +474,7 @@ static int resume_target_kernel(bool platform_mode)
 	syscore_resume();
 
  Enable_irqs:
+	system_state = SYSTEM_RUNNING;
 	local_irq_enable();
 
  Enable_cpus:
@@ -555,6 +560,7 @@ int hibernation_platform_enter(void)
 		goto Enable_cpus;
 
 	local_irq_disable();
+	system_state = SYSTEM_SUSPEND;
 	syscore_suspend();
 	if (pm_wakeup_pending()) {
 		error = -EAGAIN;
@@ -567,6 +573,7 @@ int hibernation_platform_enter(void)
 
  Power_up:
 	syscore_resume();
+	system_state = SYSTEM_RUNNING;
 	local_irq_enable();
 
  Enable_cpus:
diff --git a/kernel/power/suspend.c b/kernel/power/suspend.c
index f9fe133c13e2..80ebc0726290 100644
--- a/kernel/power/suspend.c
+++ b/kernel/power/suspend.c
@@ -359,6 +359,8 @@ static int suspend_enter(suspend_state_t state, bool *wakeup)
 	arch_suspend_disable_irqs();
 	BUG_ON(!irqs_disabled());
 
+	system_state = SYSTEM_SUSPEND;
+
 	error = syscore_suspend();
 	if (!error) {
 		*wakeup = pm_wakeup_pending();
@@ -375,6 +377,8 @@ static int suspend_enter(suspend_state_t state, bool *wakeup)
 		syscore_resume();
 	}
 
+	system_state = SYSTEM_RUNNING;
+
 	arch_suspend_enable_irqs();
 	BUG_ON(irqs_disabled());
 
diff --git a/kernel/printk/printk.c b/kernel/printk/printk.c
index 2ce8826f1053..079a3af04e5e 100644
--- a/kernel/printk/printk.c
+++ b/kernel/printk/printk.c
@@ -1203,6 +1203,7 @@ static int syslog_print_all(char __user *buf, int size, bool clear)
 {
 	char *text;
 	int len = 0;
+	int attempts = 0;
 
 	text = kmalloc(LOG_LINE_MAX + PREFIX_MAX, GFP_KERNEL);
 	if (!text)
@@ -1214,7 +1215,14 @@ static int syslog_print_all(char __user *buf, int size, bool clear)
 		u64 seq;
 		u32 idx;
 		enum log_flags prev;
-
+		int num_msg;
+try_again:
+		attempts++;
+		if (attempts > 10) {
+			len = -EBUSY;
+			goto out;
+		}
+		num_msg = 0;
 		if (clear_seq < log_first_seq) {
 			/* messages are gone, move to first available one */
 			clear_seq = log_first_seq;
@@ -1235,6 +1243,14 @@ static int syslog_print_all(char __user *buf, int size, bool clear)
 			prev = msg->flags;
 			idx = log_next(idx);
 			seq++;
+			num_msg++;
+			if (num_msg > 5) {
+				num_msg = 0;
+				raw_spin_unlock_irq(&logbuf_lock);
+				raw_spin_lock_irq(&logbuf_lock);
+				if (clear_seq < log_first_seq)
+					goto try_again;
+			}
 		}
 
 		/* move first record forward until length fits into the buffer */
@@ -1248,6 +1264,14 @@ static int syslog_print_all(char __user *buf, int size, bool clear)
 			prev = msg->flags;
 			idx = log_next(idx);
 			seq++;
+			num_msg++;
+			if (num_msg > 5) {
+				num_msg = 0;
+				raw_spin_unlock_irq(&logbuf_lock);
+				raw_spin_lock_irq(&logbuf_lock);
+				if (clear_seq < log_first_seq)
+					goto try_again;
+			}
 		}
 
 		/* last message fitting into this dump */
@@ -1288,6 +1312,7 @@ static int syslog_print_all(char __user *buf, int size, bool clear)
 		clear_seq = log_next_seq;
 		clear_idx = log_next_idx;
 	}
+out:
 	raw_spin_unlock_irq(&logbuf_lock);
 
 	kfree(text);
@@ -1443,6 +1468,7 @@ static void call_console_drivers(int level,
 	if (!console_drivers)
 		return;
 
+	migrate_disable();
 	for_each_console(con) {
 		if (exclusive_console && con != exclusive_console)
 			continue;
@@ -1458,6 +1484,7 @@ static void call_console_drivers(int level,
 		else
 			con->write(con, text, len);
 	}
+	migrate_enable();
 }
 
 /*
@@ -1518,6 +1545,15 @@ static inline int can_use_console(unsigned int cpu)
 static int console_trylock_for_printk(void)
 {
 	unsigned int cpu = smp_processor_id();
+#ifdef CONFIG_PREEMPT_RT_FULL
+	int lock = !early_boot_irqs_disabled && (preempt_count() == 0) &&
+		!irqs_disabled();
+#else
+	int lock = 1;
+#endif
+
+	if (!lock)
+		return 0;
 
 	if (!console_trylock())
 		return 0;
@@ -1656,6 +1692,62 @@ static size_t cont_print_text(char *text, size_t size)
 	return textlen;
 }
 
+#ifdef CONFIG_EARLY_PRINTK
+struct console *early_console;
+
+static void early_vprintk(const char *fmt, va_list ap)
+{
+	if (early_console) {
+		char buf[512];
+		int n = vscnprintf(buf, sizeof(buf), fmt, ap);
+
+		early_console->write(early_console, buf, n);
+	}
+}
+
+asmlinkage void early_printk(const char *fmt, ...)
+{
+	va_list ap;
+
+	va_start(ap, fmt);
+	early_vprintk(fmt, ap);
+	va_end(ap);
+}
+
+/*
+ * This is independent of any log levels - a global
+ * kill switch that turns off all of printk.
+ *
+ * Used by the NMI watchdog if early-printk is enabled.
+ */
+static bool __read_mostly printk_killswitch;
+
+static int __init force_early_printk_setup(char *str)
+{
+	printk_killswitch = true;
+	return 0;
+}
+early_param("force_early_printk", force_early_printk_setup);
+
+void printk_kill(void)
+{
+	printk_killswitch = true;
+}
+
+static int forced_early_printk(const char *fmt, va_list ap)
+{
+	if (!printk_killswitch)
+		return 0;
+	early_vprintk(fmt, ap);
+	return 1;
+}
+#else
+static inline int forced_early_printk(const char *fmt, va_list ap)
+{
+	return 0;
+}
+#endif
+
 asmlinkage int vprintk_emit(int facility, int level,
 			    const char *dict, size_t dictlen,
 			    const char *fmt, va_list args)
@@ -1672,6 +1764,13 @@ asmlinkage int vprintk_emit(int facility, int level,
 	/* cpu currently holding logbuf_lock in this function */
 	static unsigned int logbuf_cpu = UINT_MAX;
 
+	/*
+	 * Fall back to early_printk if a debugging subsystem has
+	 * killed printk output
+	 */
+	if (unlikely(forced_early_printk(fmt, args)))
+		return 1;
+
 	if (level == LOGLEVEL_SCHED) {
 		level = LOGLEVEL_DEFAULT;
 		in_sched = true;
@@ -1813,8 +1912,7 @@ asmlinkage int vprintk_emit(int facility, int level,
 		 * console_sem which would prevent anyone from printing to
 		 * console
 		 */
-		preempt_disable();
-
+		migrate_disable();
 		/*
 		 * Try to acquire and then immediately release the console
 		 * semaphore.  The release will print out buffers and wake up
@@ -1822,7 +1920,7 @@ asmlinkage int vprintk_emit(int facility, int level,
 		 */
 		if (console_trylock_for_printk())
 			console_unlock();
-		preempt_enable();
+		migrate_enable();
 		lockdep_on();
 	}
 
@@ -1961,26 +2059,6 @@ DEFINE_PER_CPU(printk_func_t, printk_func);
 
 #endif /* CONFIG_PRINTK */
 
-#ifdef CONFIG_EARLY_PRINTK
-struct console *early_console;
-
-asmlinkage __visible void early_printk(const char *fmt, ...)
-{
-	va_list ap;
-	char buf[512];
-	int n;
-
-	if (!early_console)
-		return;
-
-	va_start(ap, fmt);
-	n = vscnprintf(buf, sizeof(buf), fmt, ap);
-	va_end(ap);
-
-	early_console->write(early_console, buf, n);
-}
-#endif
-
 static int __add_preferred_console(char *name, int idx, char *options,
 				   char *brl_options)
 {
@@ -2202,11 +2280,16 @@ static void console_cont_flush(char *text, size_t size)
 		goto out;
 
 	len = cont_print_text(text, size);
+#ifdef CONFIG_PREEMPT_RT_FULL
+	raw_spin_unlock_irqrestore(&logbuf_lock, flags);
+	call_console_drivers(cont.level, NULL, 0, text, len);
+#else
 	raw_spin_unlock(&logbuf_lock);
 	stop_critical_timings();
 	call_console_drivers(cont.level, NULL, 0, text, len);
 	start_critical_timings();
 	local_irq_restore(flags);
+#endif
 	return;
 out:
 	raw_spin_unlock_irqrestore(&logbuf_lock, flags);
@@ -2305,12 +2388,17 @@ void console_unlock(void)
 		console_idx = log_next(console_idx);
 		console_seq++;
 		console_prev = msg->flags;
+#ifdef CONFIG_PREEMPT_RT_FULL
+		raw_spin_unlock_irqrestore(&logbuf_lock, flags);
+		call_console_drivers(level, ext_text, ext_len, text, len);
+#else
 		raw_spin_unlock(&logbuf_lock);
 
 		stop_critical_timings();	/* don't trace print latency */
 		call_console_drivers(level, ext_text, ext_len, text, len);
 		start_critical_timings();
 		local_irq_restore(flags);
+#endif
 	}
 	console_locked = 0;
 
diff --git a/kernel/ptrace.c b/kernel/ptrace.c
index b760bae64cf1..2856b433d9d6 100644
--- a/kernel/ptrace.c
+++ b/kernel/ptrace.c
@@ -129,7 +129,14 @@ static bool ptrace_freeze_traced(struct task_struct *task)
 
 	spin_lock_irq(&task->sighand->siglock);
 	if (task_is_traced(task) && !__fatal_signal_pending(task)) {
-		task->state = __TASK_TRACED;
+		unsigned long flags;
+
+		raw_spin_lock_irqsave(&task->pi_lock, flags);
+		if (task->state & __TASK_TRACED)
+			task->state = __TASK_TRACED;
+		else
+			task->saved_state = __TASK_TRACED;
+		raw_spin_unlock_irqrestore(&task->pi_lock, flags);
 		ret = true;
 	}
 	spin_unlock_irq(&task->sighand->siglock);
diff --git a/kernel/rcu/tree.c b/kernel/rcu/tree.c
index f07343b54fe5..5359091fecaa 100644
--- a/kernel/rcu/tree.c
+++ b/kernel/rcu/tree.c
@@ -56,6 +56,11 @@
 #include <linux/random.h>
 #include <linux/trace_events.h>
 #include <linux/suspend.h>
+#include <linux/delay.h>
+#include <linux/gfp.h>
+#include <linux/oom.h>
+#include <linux/smpboot.h>
+#include "../time/tick-internal.h"
 
 #include "tree.h"
 #include "rcu.h"
@@ -266,6 +271,19 @@ void rcu_sched_qs(void)
 	}
 }
 
+#ifdef CONFIG_PREEMPT_RT_FULL
+static void rcu_preempt_qs(void);
+
+void rcu_bh_qs(void)
+{
+	unsigned long flags;
+
+	/* Callers to this function, rcu_preempt_qs(), must disable irqs. */
+	local_irq_save(flags);
+	rcu_preempt_qs();
+	local_irq_restore(flags);
+}
+#else
 void rcu_bh_qs(void)
 {
 	if (__this_cpu_read(rcu_bh_data.cpu_no_qs.s)) {
@@ -275,6 +293,7 @@ void rcu_bh_qs(void)
 		__this_cpu_write(rcu_bh_data.cpu_no_qs.b.norm, false);
 	}
 }
+#endif
 
 static DEFINE_PER_CPU(int, rcu_sched_qs_mask);
 
@@ -459,6 +478,7 @@ unsigned long rcu_batches_completed_sched(void)
 }
 EXPORT_SYMBOL_GPL(rcu_batches_completed_sched);
 
+#ifndef CONFIG_PREEMPT_RT_FULL
 /*
  * Return the number of RCU BH batches completed thus far for debug & stats.
  */
@@ -486,6 +506,13 @@ void rcu_bh_force_quiescent_state(void)
 }
 EXPORT_SYMBOL_GPL(rcu_bh_force_quiescent_state);
 
+#else
+void rcu_force_quiescent_state(void)
+{
+}
+EXPORT_SYMBOL_GPL(rcu_force_quiescent_state);
+#endif
+
 /*
  * Force a quiescent state for RCU-sched.
  */
@@ -1611,7 +1638,7 @@ static void rcu_gp_kthread_wake(struct rcu_state *rsp)
 	    !READ_ONCE(rsp->gp_flags) ||
 	    !rsp->gp_kthread)
 		return;
-	wake_up(&rsp->gp_wq);
+	swait_wake(&rsp->gp_wq);
 }
 
 /*
@@ -2076,7 +2103,7 @@ static int __noreturn rcu_gp_kthread(void *arg)
 					       READ_ONCE(rsp->gpnum),
 					       TPS("reqwait"));
 			rsp->gp_state = RCU_GP_WAIT_GPS;
-			wait_event_interruptible(rsp->gp_wq,
+			swait_event_interruptible(rsp->gp_wq,
 						 READ_ONCE(rsp->gp_flags) &
 						 RCU_GP_FLAG_INIT);
 			rsp->gp_state = RCU_GP_DONE_GPS;
@@ -2106,7 +2133,7 @@ static int __noreturn rcu_gp_kthread(void *arg)
 					       READ_ONCE(rsp->gpnum),
 					       TPS("fqswait"));
 			rsp->gp_state = RCU_GP_WAIT_FQS;
-			ret = wait_event_interruptible_timeout(rsp->gp_wq,
+			ret = swait_event_interruptible_timeout(rsp->gp_wq,
 					rcu_gp_fqs_check_wake(rsp, &gf), j);
 			rsp->gp_state = RCU_GP_DOING_FQS;
 			/* Locking provides needed memory barriers. */
@@ -2934,18 +2961,17 @@ __rcu_process_callbacks(struct rcu_state *rsp)
 /*
  * Do RCU core processing for the current CPU.
  */
-static void rcu_process_callbacks(struct softirq_action *unused)
+static void rcu_process_callbacks(void)
 {
 	struct rcu_state *rsp;
 
 	if (cpu_is_offline(smp_processor_id()))
 		return;
-	trace_rcu_utilization(TPS("Start RCU core"));
 	for_each_rcu_flavor(rsp)
 		__rcu_process_callbacks(rsp);
-	trace_rcu_utilization(TPS("End RCU core"));
 }
 
+static DEFINE_PER_CPU(struct task_struct *, rcu_cpu_kthread_task);
 /*
  * Schedule RCU callback invocation.  If the specified type of RCU
  * does not support RCU priority boosting, just do a direct call,
@@ -2957,19 +2983,106 @@ static void invoke_rcu_callbacks(struct rcu_state *rsp, struct rcu_data *rdp)
 {
 	if (unlikely(!READ_ONCE(rcu_scheduler_fully_active)))
 		return;
-	if (likely(!rsp->boost)) {
-		rcu_do_batch(rsp, rdp);
-		return;
-	}
-	invoke_rcu_callbacks_kthread();
+	rcu_do_batch(rsp, rdp);
 }
 
+static void rcu_wake_cond(struct task_struct *t, int status)
+{
+	/*
+	 * If the thread is yielding, only wake it when this
+	 * is invoked from idle
+	 */
+	if (t && (status != RCU_KTHREAD_YIELDING || is_idle_task(current)))
+		wake_up_process(t);
+}
+
+/*
+ * Wake up this CPU's rcuc kthread to do RCU core processing.
+ */
 static void invoke_rcu_core(void)
 {
-	if (cpu_online(smp_processor_id()))
-		raise_softirq(RCU_SOFTIRQ);
+	unsigned long flags;
+	struct task_struct *t;
+
+	if (!cpu_online(smp_processor_id()))
+		return;
+	local_irq_save(flags);
+	__this_cpu_write(rcu_cpu_has_work, 1);
+	t = __this_cpu_read(rcu_cpu_kthread_task);
+	if (t != NULL && current != t)
+		rcu_wake_cond(t, __this_cpu_read(rcu_cpu_kthread_status));
+	local_irq_restore(flags);
 }
 
+static void rcu_cpu_kthread_park(unsigned int cpu)
+{
+	per_cpu(rcu_cpu_kthread_status, cpu) = RCU_KTHREAD_OFFCPU;
+}
+
+static int rcu_cpu_kthread_should_run(unsigned int cpu)
+{
+	return __this_cpu_read(rcu_cpu_has_work);
+}
+
+/*
+ * Per-CPU kernel thread that invokes RCU callbacks.  This replaces the
+ * RCU softirq used in flavors and configurations of RCU that do not
+ * support RCU priority boosting.
+ */
+static void rcu_cpu_kthread(unsigned int cpu)
+{
+	unsigned int *statusp = this_cpu_ptr(&rcu_cpu_kthread_status);
+	char work, *workp = this_cpu_ptr(&rcu_cpu_has_work);
+	int spincnt;
+
+	for (spincnt = 0; spincnt < 10; spincnt++) {
+		trace_rcu_utilization(TPS("Start CPU kthread@rcu_wait"));
+		local_bh_disable();
+		*statusp = RCU_KTHREAD_RUNNING;
+		this_cpu_inc(rcu_cpu_kthread_loops);
+		local_irq_disable();
+		work = *workp;
+		*workp = 0;
+		local_irq_enable();
+		if (work)
+			rcu_process_callbacks();
+		local_bh_enable();
+		if (*workp == 0) {
+			trace_rcu_utilization(TPS("End CPU kthread@rcu_wait"));
+			*statusp = RCU_KTHREAD_WAITING;
+			return;
+		}
+	}
+	*statusp = RCU_KTHREAD_YIELDING;
+	trace_rcu_utilization(TPS("Start CPU kthread@rcu_yield"));
+	schedule_timeout_interruptible(2);
+	trace_rcu_utilization(TPS("End CPU kthread@rcu_yield"));
+	*statusp = RCU_KTHREAD_WAITING;
+}
+
+static struct smp_hotplug_thread rcu_cpu_thread_spec = {
+	.store			= &rcu_cpu_kthread_task,
+	.thread_should_run	= rcu_cpu_kthread_should_run,
+	.thread_fn		= rcu_cpu_kthread,
+	.thread_comm		= "rcuc/%u",
+	.setup			= rcu_cpu_kthread_setup,
+	.park			= rcu_cpu_kthread_park,
+};
+
+/*
+ * Spawn per-CPU RCU core processing kthreads.
+ */
+static int __init rcu_spawn_core_kthreads(void)
+{
+	int cpu;
+
+	for_each_possible_cpu(cpu)
+		per_cpu(rcu_cpu_has_work, cpu) = 0;
+	BUG_ON(smpboot_register_percpu_thread(&rcu_cpu_thread_spec));
+	return 0;
+}
+early_initcall(rcu_spawn_core_kthreads);
+
 /*
  * Handle any core-RCU processing required by a call_rcu() invocation.
  */
@@ -3114,6 +3227,7 @@ void call_rcu_sched(struct rcu_head *head, rcu_callback_t func)
 }
 EXPORT_SYMBOL_GPL(call_rcu_sched);
 
+#ifndef CONFIG_PREEMPT_RT_FULL
 /*
  * Queue an RCU callback for invocation after a quicker grace period.
  */
@@ -3122,6 +3236,7 @@ void call_rcu_bh(struct rcu_head *head, rcu_callback_t func)
 	__call_rcu(head, func, &rcu_bh_state, -1, 0);
 }
 EXPORT_SYMBOL_GPL(call_rcu_bh);
+#endif
 
 /*
  * Queue an RCU callback for lazy invocation after a grace period.
@@ -3213,6 +3328,7 @@ void synchronize_sched(void)
 }
 EXPORT_SYMBOL_GPL(synchronize_sched);
 
+#ifndef CONFIG_PREEMPT_RT_FULL
 /**
  * synchronize_rcu_bh - wait until an rcu_bh grace period has elapsed.
  *
@@ -3239,6 +3355,7 @@ void synchronize_rcu_bh(void)
 		wait_rcu_gp(call_rcu_bh);
 }
 EXPORT_SYMBOL_GPL(synchronize_rcu_bh);
+#endif
 
 /**
  * get_state_synchronize_rcu - Snapshot current RCU state
@@ -3524,7 +3641,7 @@ static void __rcu_report_exp_rnp(struct rcu_state *rsp, struct rcu_node *rnp,
 			raw_spin_unlock_irqrestore(&rnp->lock, flags);
 			if (wake) {
 				smp_mb(); /* EGP done before wake_up(). */
-				wake_up(&rsp->expedited_wq);
+				swait_wake(&rsp->expedited_wq);
 			}
 			break;
 		}
@@ -3781,7 +3898,7 @@ static void synchronize_sched_expedited_wait(struct rcu_state *rsp)
 	jiffies_start = jiffies;
 
 	for (;;) {
-		ret = wait_event_interruptible_timeout(
+		ret = swait_event_interruptible_timeout(
 				rsp->expedited_wq,
 				sync_rcu_preempt_exp_done(rnp_root),
 				jiffies_stall);
@@ -3789,7 +3906,7 @@ static void synchronize_sched_expedited_wait(struct rcu_state *rsp)
 			return;
 		if (ret < 0) {
 			/* Hit a signal, disable CPU stall warnings. */
-			wait_event(rsp->expedited_wq,
+			swait_event(rsp->expedited_wq,
 				   sync_rcu_preempt_exp_done(rnp_root));
 			return;
 		}
@@ -4101,6 +4218,7 @@ static void _rcu_barrier(struct rcu_state *rsp)
 	mutex_unlock(&rsp->barrier_mutex);
 }
 
+#ifndef CONFIG_PREEMPT_RT_FULL
 /**
  * rcu_barrier_bh - Wait until all in-flight call_rcu_bh() callbacks complete.
  */
@@ -4109,6 +4227,7 @@ void rcu_barrier_bh(void)
 	_rcu_barrier(&rcu_bh_state);
 }
 EXPORT_SYMBOL_GPL(rcu_barrier_bh);
+#endif
 
 /**
  * rcu_barrier_sched - Wait for in-flight call_rcu_sched() callbacks.
@@ -4455,8 +4574,8 @@ static void __init rcu_init_one(struct rcu_state *rsp,
 		}
 	}
 
-	init_waitqueue_head(&rsp->gp_wq);
-	init_waitqueue_head(&rsp->expedited_wq);
+	init_swait_head(&rsp->gp_wq);
+	init_swait_head(&rsp->expedited_wq);
 	rnp = rsp->level[rcu_num_lvls - 1];
 	for_each_possible_cpu(i) {
 		while (i > rnp->grphi)
@@ -4581,7 +4700,6 @@ void __init rcu_init(void)
 	if (dump_tree)
 		rcu_dump_rcu_node_tree(&rcu_sched_state);
 	__rcu_init_preempt();
-	open_softirq(RCU_SOFTIRQ, rcu_process_callbacks);
 
 	/*
 	 * We don't need protection against CPU-hotplug here because
diff --git a/kernel/rcu/tree.h b/kernel/rcu/tree.h
index 9fb4e238d4dc..588509d94bbd 100644
--- a/kernel/rcu/tree.h
+++ b/kernel/rcu/tree.h
@@ -28,6 +28,7 @@
 #include <linux/cpumask.h>
 #include <linux/seqlock.h>
 #include <linux/stop_machine.h>
+#include <linux/wait-simple.h>
 
 /*
  * Define shape of hierarchy based on NR_CPUS, CONFIG_RCU_FANOUT, and
@@ -241,7 +242,7 @@ struct rcu_node {
 				/* Refused to boost: not sure why, though. */
 				/*  This can happen due to race conditions. */
 #ifdef CONFIG_RCU_NOCB_CPU
-	wait_queue_head_t nocb_gp_wq[2];
+	struct swait_head nocb_gp_wq[2];
 				/* Place for rcu_nocb_kthread() to wait GP. */
 #endif /* #ifdef CONFIG_RCU_NOCB_CPU */
 	int need_future_gp[2];
@@ -393,7 +394,7 @@ struct rcu_data {
 	atomic_long_t nocb_q_count_lazy; /*  invocation (all stages). */
 	struct rcu_head *nocb_follower_head; /* CBs ready to invoke. */
 	struct rcu_head **nocb_follower_tail;
-	wait_queue_head_t nocb_wq;	/* For nocb kthreads to sleep on. */
+	struct swait_head nocb_wq;	/* For nocb kthreads to sleep on. */
 	struct task_struct *nocb_kthread;
 	int nocb_defer_wakeup;		/* Defer wakeup of nocb_kthread. */
 
@@ -472,7 +473,7 @@ struct rcu_state {
 	unsigned long gpnum;			/* Current gp number. */
 	unsigned long completed;		/* # of last completed gp. */
 	struct task_struct *gp_kthread;		/* Task for grace periods. */
-	wait_queue_head_t gp_wq;		/* Where GP task waits. */
+	struct swait_head gp_wq;		/* Where GP task waits. */
 	short gp_flags;				/* Commands for GP task. */
 	short gp_state;				/* GP kthread sleep state. */
 
@@ -504,7 +505,7 @@ struct rcu_state {
 	atomic_long_t expedited_workdone3;	/* # done by others #3. */
 	atomic_long_t expedited_normal;		/* # fallbacks to normal. */
 	atomic_t expedited_need_qs;		/* # CPUs left to check in. */
-	wait_queue_head_t expedited_wq;		/* Wait for check-ins. */
+	struct swait_head expedited_wq;		/* Wait for check-ins. */
 	int ncpus_snap;				/* # CPUs seen last time. */
 
 	unsigned long jiffies_force_qs;		/* Time at which to invoke */
@@ -562,12 +563,10 @@ extern struct rcu_state rcu_bh_state;
 extern struct rcu_state rcu_preempt_state;
 #endif /* #ifdef CONFIG_PREEMPT_RCU */
 
-#ifdef CONFIG_RCU_BOOST
 DECLARE_PER_CPU(unsigned int, rcu_cpu_kthread_status);
 DECLARE_PER_CPU(int, rcu_cpu_kthread_cpu);
 DECLARE_PER_CPU(unsigned int, rcu_cpu_kthread_loops);
 DECLARE_PER_CPU(char, rcu_cpu_has_work);
-#endif /* #ifdef CONFIG_RCU_BOOST */
 
 #ifndef RCU_TREE_NONCORE
 
@@ -587,10 +586,9 @@ void call_rcu(struct rcu_head *head, rcu_callback_t func);
 static void __init __rcu_init_preempt(void);
 static void rcu_initiate_boost(struct rcu_node *rnp, unsigned long flags);
 static void rcu_preempt_boost_start_gp(struct rcu_node *rnp);
-static void invoke_rcu_callbacks_kthread(void);
 static bool rcu_is_callbacks_kthread(void);
+static void rcu_cpu_kthread_setup(unsigned int cpu);
 #ifdef CONFIG_RCU_BOOST
-static void rcu_preempt_do_callbacks(void);
 static int rcu_spawn_one_boost_kthread(struct rcu_state *rsp,
 						 struct rcu_node *rnp);
 #endif /* #ifdef CONFIG_RCU_BOOST */
diff --git a/kernel/rcu/tree_plugin.h b/kernel/rcu/tree_plugin.h
index 630c19772630..0d2f27f2f38d 100644
--- a/kernel/rcu/tree_plugin.h
+++ b/kernel/rcu/tree_plugin.h
@@ -24,25 +24,10 @@
  *	   Paul E. McKenney <paulmck@linux.vnet.ibm.com>
  */
 
-#include <linux/delay.h>
-#include <linux/gfp.h>
-#include <linux/oom.h>
-#include <linux/smpboot.h>
-#include "../time/tick-internal.h"
-
 #ifdef CONFIG_RCU_BOOST
 
 #include "../locking/rtmutex_common.h"
 
-/*
- * Control variables for per-CPU and per-rcu_node kthreads.  These
- * handle all flavors of RCU.
- */
-static DEFINE_PER_CPU(struct task_struct *, rcu_cpu_kthread_task);
-DEFINE_PER_CPU(unsigned int, rcu_cpu_kthread_status);
-DEFINE_PER_CPU(unsigned int, rcu_cpu_kthread_loops);
-DEFINE_PER_CPU(char, rcu_cpu_has_work);
-
 #else /* #ifdef CONFIG_RCU_BOOST */
 
 /*
@@ -55,6 +40,14 @@ DEFINE_PER_CPU(char, rcu_cpu_has_work);
 
 #endif /* #else #ifdef CONFIG_RCU_BOOST */
 
+/*
+ * Control variables for per-CPU and per-rcu_node kthreads.  These
+ * handle all flavors of RCU.
+ */
+DEFINE_PER_CPU(unsigned int, rcu_cpu_kthread_status);
+DEFINE_PER_CPU(unsigned int, rcu_cpu_kthread_loops);
+DEFINE_PER_CPU(char, rcu_cpu_has_work);
+
 #ifdef CONFIG_RCU_NOCB_CPU
 static cpumask_var_t rcu_nocb_mask; /* CPUs to have callbacks offloaded. */
 static bool have_rcu_nocb_mask;	    /* Was rcu_nocb_mask allocated? */
@@ -432,7 +425,7 @@ void rcu_read_unlock_special(struct task_struct *t)
 	}
 
 	/* Hardware IRQ handlers cannot block, complain if they get here. */
-	if (in_irq() || in_serving_softirq()) {
+	if (preempt_count() & (HARDIRQ_MASK | SOFTIRQ_OFFSET)) {
 		lockdep_rcu_suspicious(__FILE__, __LINE__,
 				       "rcu_read_unlock() from irq or softirq with blocking in critical section!!!\n");
 		pr_alert("->rcu_read_unlock_special: %#x (b: %d, enq: %d nq: %d)\n",
@@ -645,15 +638,6 @@ static void rcu_preempt_check_callbacks(void)
 		t->rcu_read_unlock_special.b.need_qs = true;
 }
 
-#ifdef CONFIG_RCU_BOOST
-
-static void rcu_preempt_do_callbacks(void)
-{
-	rcu_do_batch(rcu_state_p, this_cpu_ptr(rcu_data_p));
-}
-
-#endif /* #ifdef CONFIG_RCU_BOOST */
-
 /*
  * Queue a preemptible-RCU callback for invocation after a grace period.
  */
@@ -930,6 +914,19 @@ void exit_rcu(void)
 
 #endif /* #else #ifdef CONFIG_PREEMPT_RCU */
 
+/*
+ * If boosting, set rcuc kthreads to realtime priority.
+ */
+static void rcu_cpu_kthread_setup(unsigned int cpu)
+{
+#ifdef CONFIG_RCU_BOOST
+	struct sched_param sp;
+
+	sp.sched_priority = kthread_prio;
+	sched_setscheduler_nocheck(current, SCHED_FIFO, &sp);
+#endif /* #ifdef CONFIG_RCU_BOOST */
+}
+
 #ifdef CONFIG_RCU_BOOST
 
 #include "../locking/rtmutex_common.h"
@@ -961,16 +958,6 @@ static void rcu_initiate_boost_trace(struct rcu_node *rnp)
 
 #endif /* #else #ifdef CONFIG_RCU_TRACE */
 
-static void rcu_wake_cond(struct task_struct *t, int status)
-{
-	/*
-	 * If the thread is yielding, only wake it when this
-	 * is invoked from idle
-	 */
-	if (status != RCU_KTHREAD_YIELDING || is_idle_task(current))
-		wake_up_process(t);
-}
-
 /*
  * Carry out RCU priority boosting on the task indicated by ->exp_tasks
  * or ->boost_tasks, advancing the pointer to the next task in the
@@ -1115,23 +1102,6 @@ static void rcu_initiate_boost(struct rcu_node *rnp, unsigned long flags)
 }
 
 /*
- * Wake up the per-CPU kthread to invoke RCU callbacks.
- */
-static void invoke_rcu_callbacks_kthread(void)
-{
-	unsigned long flags;
-
-	local_irq_save(flags);
-	__this_cpu_write(rcu_cpu_has_work, 1);
-	if (__this_cpu_read(rcu_cpu_kthread_task) != NULL &&
-	    current != __this_cpu_read(rcu_cpu_kthread_task)) {
-		rcu_wake_cond(__this_cpu_read(rcu_cpu_kthread_task),
-			      __this_cpu_read(rcu_cpu_kthread_status));
-	}
-	local_irq_restore(flags);
-}
-
-/*
  * Is the current CPU running the RCU-callbacks kthread?
  * Caller must have preemption disabled.
  */
@@ -1186,67 +1156,6 @@ static int rcu_spawn_one_boost_kthread(struct rcu_state *rsp,
 	return 0;
 }
 
-static void rcu_kthread_do_work(void)
-{
-	rcu_do_batch(&rcu_sched_state, this_cpu_ptr(&rcu_sched_data));
-	rcu_do_batch(&rcu_bh_state, this_cpu_ptr(&rcu_bh_data));
-	rcu_preempt_do_callbacks();
-}
-
-static void rcu_cpu_kthread_setup(unsigned int cpu)
-{
-	struct sched_param sp;
-
-	sp.sched_priority = kthread_prio;
-	sched_setscheduler_nocheck(current, SCHED_FIFO, &sp);
-}
-
-static void rcu_cpu_kthread_park(unsigned int cpu)
-{
-	per_cpu(rcu_cpu_kthread_status, cpu) = RCU_KTHREAD_OFFCPU;
-}
-
-static int rcu_cpu_kthread_should_run(unsigned int cpu)
-{
-	return __this_cpu_read(rcu_cpu_has_work);
-}
-
-/*
- * Per-CPU kernel thread that invokes RCU callbacks.  This replaces the
- * RCU softirq used in flavors and configurations of RCU that do not
- * support RCU priority boosting.
- */
-static void rcu_cpu_kthread(unsigned int cpu)
-{
-	unsigned int *statusp = this_cpu_ptr(&rcu_cpu_kthread_status);
-	char work, *workp = this_cpu_ptr(&rcu_cpu_has_work);
-	int spincnt;
-
-	for (spincnt = 0; spincnt < 10; spincnt++) {
-		trace_rcu_utilization(TPS("Start CPU kthread@rcu_wait"));
-		local_bh_disable();
-		*statusp = RCU_KTHREAD_RUNNING;
-		this_cpu_inc(rcu_cpu_kthread_loops);
-		local_irq_disable();
-		work = *workp;
-		*workp = 0;
-		local_irq_enable();
-		if (work)
-			rcu_kthread_do_work();
-		local_bh_enable();
-		if (*workp == 0) {
-			trace_rcu_utilization(TPS("End CPU kthread@rcu_wait"));
-			*statusp = RCU_KTHREAD_WAITING;
-			return;
-		}
-	}
-	*statusp = RCU_KTHREAD_YIELDING;
-	trace_rcu_utilization(TPS("Start CPU kthread@rcu_yield"));
-	schedule_timeout_interruptible(2);
-	trace_rcu_utilization(TPS("End CPU kthread@rcu_yield"));
-	*statusp = RCU_KTHREAD_WAITING;
-}
-
 /*
  * Set the per-rcu_node kthread's affinity to cover all CPUs that are
  * served by the rcu_node in question.  The CPU hotplug lock is still
@@ -1276,26 +1185,12 @@ static void rcu_boost_kthread_setaffinity(struct rcu_node *rnp, int outgoingcpu)
 	free_cpumask_var(cm);
 }
 
-static struct smp_hotplug_thread rcu_cpu_thread_spec = {
-	.store			= &rcu_cpu_kthread_task,
-	.thread_should_run	= rcu_cpu_kthread_should_run,
-	.thread_fn		= rcu_cpu_kthread,
-	.thread_comm		= "rcuc/%u",
-	.setup			= rcu_cpu_kthread_setup,
-	.park			= rcu_cpu_kthread_park,
-};
-
 /*
  * Spawn boost kthreads -- called as soon as the scheduler is running.
  */
 static void __init rcu_spawn_boost_kthreads(void)
 {
 	struct rcu_node *rnp;
-	int cpu;
-
-	for_each_possible_cpu(cpu)
-		per_cpu(rcu_cpu_has_work, cpu) = 0;
-	BUG_ON(smpboot_register_percpu_thread(&rcu_cpu_thread_spec));
 	rcu_for_each_leaf_node(rcu_state_p, rnp)
 		(void)rcu_spawn_one_boost_kthread(rcu_state_p, rnp);
 }
@@ -1318,11 +1213,6 @@ static void rcu_initiate_boost(struct rcu_node *rnp, unsigned long flags)
 	raw_spin_unlock_irqrestore(&rnp->lock, flags);
 }
 
-static void invoke_rcu_callbacks_kthread(void)
-{
-	WARN_ON_ONCE(1);
-}
-
 static bool rcu_is_callbacks_kthread(void)
 {
 	return false;
@@ -1346,7 +1236,7 @@ static void rcu_prepare_kthreads(int cpu)
 
 #endif /* #else #ifdef CONFIG_RCU_BOOST */
 
-#if !defined(CONFIG_RCU_FAST_NO_HZ)
+#if !defined(CONFIG_RCU_FAST_NO_HZ) || defined(CONFIG_PREEMPT_RT_FULL)
 
 /*
  * Check to see if any future RCU-related work will need to be done
@@ -1363,7 +1253,9 @@ int rcu_needs_cpu(u64 basemono, u64 *nextevt)
 	return IS_ENABLED(CONFIG_RCU_NOCB_CPU_ALL)
 	       ? 0 : rcu_cpu_has_callbacks(NULL);
 }
+#endif /* !defined(CONFIG_RCU_FAST_NO_HZ) || defined(CONFIG_PREEMPT_RT_FULL) */
 
+#if !defined(CONFIG_RCU_FAST_NO_HZ)
 /*
  * Because we do not have RCU_FAST_NO_HZ, don't bother cleaning up
  * after it.
@@ -1459,6 +1351,8 @@ static bool __maybe_unused rcu_try_advance_all_cbs(void)
 	return cbs_ready;
 }
 
+#ifndef CONFIG_PREEMPT_RT_FULL
+
 /*
  * Allow the CPU to enter dyntick-idle mode unless it has callbacks ready
  * to invoke.  If the CPU has callbacks, try to advance them.  Tell the
@@ -1504,6 +1398,7 @@ int rcu_needs_cpu(u64 basemono, u64 *nextevt)
 	*nextevt = basemono + dj * TICK_NSEC;
 	return 0;
 }
+#endif /* #ifndef CONFIG_PREEMPT_RT_FULL */
 
 /*
  * Prepare a CPU for idle from an RCU perspective.  The first major task
@@ -1824,7 +1719,7 @@ early_param("rcu_nocb_poll", parse_rcu_nocb_poll);
  */
 static void rcu_nocb_gp_cleanup(struct rcu_state *rsp, struct rcu_node *rnp)
 {
-	wake_up_all(&rnp->nocb_gp_wq[rnp->completed & 0x1]);
+	swait_wake_all(&rnp->nocb_gp_wq[rnp->completed & 0x1]);
 }
 
 /*
@@ -1842,8 +1737,8 @@ static void rcu_nocb_gp_set(struct rcu_node *rnp, int nrq)
 
 static void rcu_init_one_nocb(struct rcu_node *rnp)
 {
-	init_waitqueue_head(&rnp->nocb_gp_wq[0]);
-	init_waitqueue_head(&rnp->nocb_gp_wq[1]);
+	init_swait_head(&rnp->nocb_gp_wq[0]);
+	init_swait_head(&rnp->nocb_gp_wq[1]);
 }
 
 #ifndef CONFIG_RCU_NOCB_CPU_ALL
@@ -1868,7 +1763,7 @@ static void wake_nocb_leader(struct rcu_data *rdp, bool force)
 	if (READ_ONCE(rdp_leader->nocb_leader_sleep) || force) {
 		/* Prior smp_mb__after_atomic() orders against prior enqueue. */
 		WRITE_ONCE(rdp_leader->nocb_leader_sleep, false);
-		wake_up(&rdp_leader->nocb_wq);
+		swait_wake(&rdp_leader->nocb_wq);
 	}
 }
 
@@ -2081,7 +1976,7 @@ static void rcu_nocb_wait_gp(struct rcu_data *rdp)
 	 */
 	trace_rcu_future_gp(rnp, rdp, c, TPS("StartWait"));
 	for (;;) {
-		wait_event_interruptible(
+		swait_event_interruptible(
 			rnp->nocb_gp_wq[c & 0x1],
 			(d = ULONG_CMP_GE(READ_ONCE(rnp->completed), c)));
 		if (likely(d))
@@ -2109,7 +2004,7 @@ static void nocb_leader_wait(struct rcu_data *my_rdp)
 	/* Wait for callbacks to appear. */
 	if (!rcu_nocb_poll) {
 		trace_rcu_nocb_wake(my_rdp->rsp->name, my_rdp->cpu, "Sleep");
-		wait_event_interruptible(my_rdp->nocb_wq,
+		swait_event_interruptible(my_rdp->nocb_wq,
 				!READ_ONCE(my_rdp->nocb_leader_sleep));
 		/* Memory barrier handled by smp_mb() calls below and repoll. */
 	} else if (firsttime) {
@@ -2184,7 +2079,7 @@ static void nocb_leader_wait(struct rcu_data *my_rdp)
 			 * List was empty, wake up the follower.
 			 * Memory barriers supplied by atomic_long_add().
 			 */
-			wake_up(&rdp->nocb_wq);
+			swait_wake(&rdp->nocb_wq);
 		}
 	}
 
@@ -2205,7 +2100,7 @@ static void nocb_follower_wait(struct rcu_data *rdp)
 		if (!rcu_nocb_poll) {
 			trace_rcu_nocb_wake(rdp->rsp->name, rdp->cpu,
 					    "FollowerSleep");
-			wait_event_interruptible(rdp->nocb_wq,
+			swait_event_interruptible(rdp->nocb_wq,
 						 READ_ONCE(rdp->nocb_follower_head));
 		} else if (firsttime) {
 			/* Don't drown trace log with "Poll"! */
@@ -2364,7 +2259,7 @@ void __init rcu_init_nohz(void)
 static void __init rcu_boot_init_nocb_percpu_data(struct rcu_data *rdp)
 {
 	rdp->nocb_tail = &rdp->nocb_head;
-	init_waitqueue_head(&rdp->nocb_wq);
+	init_swait_head(&rdp->nocb_wq);
 	rdp->nocb_follower_tail = &rdp->nocb_follower_head;
 }
 
diff --git a/kernel/rcu/update.c b/kernel/rcu/update.c
index 5f748c5a40f0..9a3904603ff6 100644
--- a/kernel/rcu/update.c
+++ b/kernel/rcu/update.c
@@ -276,6 +276,7 @@ int rcu_read_lock_held(void)
 }
 EXPORT_SYMBOL_GPL(rcu_read_lock_held);
 
+#ifndef CONFIG_PREEMPT_RT_FULL
 /**
  * rcu_read_lock_bh_held() - might we be in RCU-bh read-side critical section?
  *
@@ -302,6 +303,7 @@ int rcu_read_lock_bh_held(void)
 	return in_softirq() || irqs_disabled();
 }
 EXPORT_SYMBOL_GPL(rcu_read_lock_bh_held);
+#endif
 
 #endif /* #ifdef CONFIG_DEBUG_LOCK_ALLOC */
 
diff --git a/kernel/relay.c b/kernel/relay.c
index 0b4570cfacae..60684be39f22 100644
--- a/kernel/relay.c
+++ b/kernel/relay.c
@@ -336,6 +336,10 @@ static void wakeup_readers(unsigned long data)
 {
 	struct rchan_buf *buf = (struct rchan_buf *)data;
 	wake_up_interruptible(&buf->read_wait);
+	/*
+	 * Stupid polling for now:
+	 */
+	mod_timer(&buf->timer, jiffies + 1);
 }
 
 /**
@@ -353,6 +357,7 @@ static void __relay_reset(struct rchan_buf *buf, unsigned int init)
 		init_waitqueue_head(&buf->read_wait);
 		kref_init(&buf->kref);
 		setup_timer(&buf->timer, wakeup_readers, (unsigned long)buf);
+		mod_timer(&buf->timer, jiffies + 1);
 	} else
 		del_timer_sync(&buf->timer);
 
@@ -736,15 +741,6 @@ size_t relay_switch_subbuf(struct rchan_buf *buf, size_t length)
 		else
 			buf->early_bytes += buf->chan->subbuf_size -
 					    buf->padding[old_subbuf];
-		smp_mb();
-		if (waitqueue_active(&buf->read_wait))
-			/*
-			 * Calling wake_up_interruptible() from here
-			 * will deadlock if we happen to be logging
-			 * from the scheduler (trying to re-grab
-			 * rq->lock), so defer it.
-			 */
-			mod_timer(&buf->timer, jiffies + 1);
 	}
 
 	old = buf->data;
diff --git a/kernel/sched/Makefile b/kernel/sched/Makefile
index 67687973ce80..a22fdb617462 100644
--- a/kernel/sched/Makefile
+++ b/kernel/sched/Makefile
@@ -13,7 +13,7 @@ endif
 
 obj-y += core.o loadavg.o clock.o cputime.o
 obj-y += idle_task.o fair.o rt.o deadline.o stop_task.o
-obj-y += wait.o completion.o idle.o
+obj-y += wait.o wait-simple.o work-simple.o completion.o idle.o
 obj-$(CONFIG_SMP) += cpupri.o cpudeadline.o
 obj-$(CONFIG_SCHED_AUTOGROUP) += auto_group.o
 obj-$(CONFIG_SCHEDSTATS) += stats.o
diff --git a/kernel/sched/completion.c b/kernel/sched/completion.c
index 8d0f35debf35..45ebcffd9feb 100644
--- a/kernel/sched/completion.c
+++ b/kernel/sched/completion.c
@@ -30,10 +30,10 @@ void complete(struct completion *x)
 {
 	unsigned long flags;
 
-	spin_lock_irqsave(&x->wait.lock, flags);
+	raw_spin_lock_irqsave(&x->wait.lock, flags);
 	x->done++;
-	__wake_up_locked(&x->wait, TASK_NORMAL, 1);
-	spin_unlock_irqrestore(&x->wait.lock, flags);
+	__swait_wake_locked(&x->wait, TASK_NORMAL, 1);
+	raw_spin_unlock_irqrestore(&x->wait.lock, flags);
 }
 EXPORT_SYMBOL(complete);
 
@@ -50,10 +50,10 @@ void complete_all(struct completion *x)
 {
 	unsigned long flags;
 
-	spin_lock_irqsave(&x->wait.lock, flags);
+	raw_spin_lock_irqsave(&x->wait.lock, flags);
 	x->done += UINT_MAX/2;
-	__wake_up_locked(&x->wait, TASK_NORMAL, 0);
-	spin_unlock_irqrestore(&x->wait.lock, flags);
+	__swait_wake_locked(&x->wait, TASK_NORMAL, 0);
+	raw_spin_unlock_irqrestore(&x->wait.lock, flags);
 }
 EXPORT_SYMBOL(complete_all);
 
@@ -62,20 +62,20 @@ do_wait_for_common(struct completion *x,
 		   long (*action)(long), long timeout, int state)
 {
 	if (!x->done) {
-		DECLARE_WAITQUEUE(wait, current);
+		DEFINE_SWAITER(wait);
 
-		__add_wait_queue_tail_exclusive(&x->wait, &wait);
+		swait_prepare_locked(&x->wait, &wait);
 		do {
 			if (signal_pending_state(state, current)) {
 				timeout = -ERESTARTSYS;
 				break;
 			}
 			__set_current_state(state);
-			spin_unlock_irq(&x->wait.lock);
+			raw_spin_unlock_irq(&x->wait.lock);
 			timeout = action(timeout);
-			spin_lock_irq(&x->wait.lock);
+			raw_spin_lock_irq(&x->wait.lock);
 		} while (!x->done && timeout);
-		__remove_wait_queue(&x->wait, &wait);
+		swait_finish_locked(&x->wait, &wait);
 		if (!x->done)
 			return timeout;
 	}
@@ -89,9 +89,9 @@ __wait_for_common(struct completion *x,
 {
 	might_sleep();
 
-	spin_lock_irq(&x->wait.lock);
+	raw_spin_lock_irq(&x->wait.lock);
 	timeout = do_wait_for_common(x, action, timeout, state);
-	spin_unlock_irq(&x->wait.lock);
+	raw_spin_unlock_irq(&x->wait.lock);
 	return timeout;
 }
 
@@ -277,12 +277,12 @@ bool try_wait_for_completion(struct completion *x)
 	if (!READ_ONCE(x->done))
 		return 0;
 
-	spin_lock_irqsave(&x->wait.lock, flags);
+	raw_spin_lock_irqsave(&x->wait.lock, flags);
 	if (!x->done)
 		ret = 0;
 	else
 		x->done--;
-	spin_unlock_irqrestore(&x->wait.lock, flags);
+	raw_spin_unlock_irqrestore(&x->wait.lock, flags);
 	return ret;
 }
 EXPORT_SYMBOL(try_wait_for_completion);
@@ -311,7 +311,7 @@ bool completion_done(struct completion *x)
 	 * after it's acquired the lock.
 	 */
 	smp_rmb();
-	spin_unlock_wait(&x->wait.lock);
+	raw_spin_unlock_wait(&x->wait.lock);
 	return true;
 }
 EXPORT_SYMBOL(completion_done);
diff --git a/kernel/sched/core.c b/kernel/sched/core.c
index 732e993b564b..7a13fbc28454 100644
--- a/kernel/sched/core.c
+++ b/kernel/sched/core.c
@@ -260,7 +260,11 @@ late_initcall(sched_init_debug);
  * Number of tasks to iterate in a single balance run.
  * Limited because this is done with IRQs disabled.
  */
+#ifndef CONFIG_PREEMPT_RT_FULL
 const_debug unsigned int sysctl_sched_nr_migrate = 32;
+#else
+const_debug unsigned int sysctl_sched_nr_migrate = 8;
+#endif
 
 /*
  * period over which we average the RT time consumption, measured
@@ -438,6 +442,7 @@ static void init_rq_hrtick(struct rq *rq)
 
 	hrtimer_init(&rq->hrtick_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
 	rq->hrtick_timer.function = hrtick;
+	rq->hrtick_timer.irqsafe = 1;
 }
 #else	/* CONFIG_SCHED_HRTICK */
 static inline void hrtick_clear(struct rq *rq)
@@ -542,7 +547,7 @@ void wake_q_add(struct wake_q_head *head, struct task_struct *task)
 	head->lastp = &node->next;
 }
 
-void wake_up_q(struct wake_q_head *head)
+void __wake_up_q(struct wake_q_head *head, bool sleeper)
 {
 	struct wake_q_node *node = head->first;
 
@@ -559,7 +564,10 @@ void wake_up_q(struct wake_q_head *head)
 		 * wake_up_process() implies a wmb() to pair with the queueing
 		 * in wake_q_add() so as not to miss wakeups.
 		 */
-		wake_up_process(task);
+		if (sleeper)
+			wake_up_lock_sleeper(task);
+		else
+			wake_up_process(task);
 		put_task_struct(task);
 	}
 }
@@ -595,6 +603,38 @@ void resched_curr(struct rq *rq)
 		trace_sched_wake_idle_without_ipi(cpu);
 }
 
+#ifdef CONFIG_PREEMPT_LAZY
+void resched_curr_lazy(struct rq *rq)
+{
+	struct task_struct *curr = rq->curr;
+	int cpu;
+
+	if (!sched_feat(PREEMPT_LAZY)) {
+		resched_curr(rq);
+		return;
+	}
+
+	lockdep_assert_held(&rq->lock);
+
+	if (test_tsk_need_resched(curr))
+		return;
+
+	if (test_tsk_need_resched_lazy(curr))
+		return;
+
+	set_tsk_need_resched_lazy(curr);
+
+	cpu = cpu_of(rq);
+	if (cpu == smp_processor_id())
+		return;
+
+	/* NEED_RESCHED_LAZY must be visible before we test polling */
+	smp_mb();
+	if (!tsk_is_polling(curr))
+		smp_send_reschedule(cpu);
+}
+#endif
+
 void resched_cpu(int cpu)
 {
 	struct rq *rq = cpu_rq(cpu);
@@ -618,11 +658,14 @@ void resched_cpu(int cpu)
  */
 int get_nohz_timer_target(void)
 {
-	int i, cpu = smp_processor_id();
+	int i, cpu;
 	struct sched_domain *sd;
 
+	preempt_disable_rt();
+	cpu = smp_processor_id();
+
 	if (!idle_cpu(cpu) && is_housekeeping_cpu(cpu))
-		return cpu;
+		goto preempt_en_rt;
 
 	rcu_read_lock();
 	for_each_domain(cpu, sd) {
@@ -638,6 +681,8 @@ int get_nohz_timer_target(void)
 		cpu = housekeeping_any_cpu();
 unlock:
 	rcu_read_unlock();
+preempt_en_rt:
+	preempt_enable_rt();
 	return cpu;
 }
 /*
@@ -1171,6 +1216,11 @@ void do_set_cpus_allowed(struct task_struct *p, const struct cpumask *new_mask)
 
 	lockdep_assert_held(&p->pi_lock);
 
+	if (__migrate_disabled(p)) {
+		cpumask_copy(&p->cpus_allowed, new_mask);
+		return;
+	}
+
 	queued = task_on_rq_queued(p);
 	running = task_current(rq, p);
 
@@ -1193,6 +1243,84 @@ void do_set_cpus_allowed(struct task_struct *p, const struct cpumask *new_mask)
 		enqueue_task(rq, p, ENQUEUE_RESTORE);
 }
 
+static DEFINE_PER_CPU(struct cpumask, sched_cpumasks);
+static DEFINE_MUTEX(sched_down_mutex);
+static cpumask_t sched_down_cpumask;
+
+void tell_sched_cpu_down_begin(int cpu)
+{
+	mutex_lock(&sched_down_mutex);
+	cpumask_set_cpu(cpu, &sched_down_cpumask);
+	mutex_unlock(&sched_down_mutex);
+}
+
+void tell_sched_cpu_down_done(int cpu)
+{
+	mutex_lock(&sched_down_mutex);
+	cpumask_clear_cpu(cpu, &sched_down_cpumask);
+	mutex_unlock(&sched_down_mutex);
+}
+
+/**
+ * migrate_me - try to move the current task off this cpu
+ *
+ * Used by the pin_current_cpu() code to try to get tasks
+ * to move off the current CPU as it is going down.
+ * It will only move the task if the task isn't pinned to
+ * the CPU (with migrate_disable, affinity or NO_SETAFFINITY)
+ * and the task has to be in a RUNNING state. Otherwise the
+ * movement of the task will wake it up (change its state
+ * to running) when the task did not expect it.
+ *
+ * Returns 1 if it succeeded in moving the current task
+ *         0 otherwise.
+ */
+int migrate_me(void)
+{
+	struct task_struct *p = current;
+	struct migration_arg arg;
+	struct cpumask *cpumask;
+	struct cpumask *mask;
+	unsigned long flags;
+	unsigned int dest_cpu;
+	struct rq *rq;
+
+	/*
+	 * We can not migrate tasks bounded to a CPU or tasks not
+	 * running. The movement of the task will wake it up.
+	 */
+	if (p->flags & PF_NO_SETAFFINITY || p->state)
+		return 0;
+
+	mutex_lock(&sched_down_mutex);
+	rq = task_rq_lock(p, &flags);
+
+	cpumask = this_cpu_ptr(&sched_cpumasks);
+	mask = &p->cpus_allowed;
+
+	cpumask_andnot(cpumask, mask, &sched_down_cpumask);
+
+	if (!cpumask_weight(cpumask)) {
+		/* It's only on this CPU? */
+		task_rq_unlock(rq, p, &flags);
+		mutex_unlock(&sched_down_mutex);
+		return 0;
+	}
+
+	dest_cpu = cpumask_any_and(cpu_active_mask, cpumask);
+
+	arg.task = p;
+	arg.dest_cpu = dest_cpu;
+
+	task_rq_unlock(rq, p, &flags);
+
+	stop_one_cpu(cpu_of(rq), migration_cpu_stop, &arg);
+	tlb_migrate_finish(p->mm);
+	mutex_unlock(&sched_down_mutex);
+
+	return 1;
+}
+
 /*
  * Change a given task's CPU affinity. Migrate the thread to a
  * proper CPU and schedule it away if the CPU it's executing on
@@ -1232,7 +1360,7 @@ static int __set_cpus_allowed_ptr(struct task_struct *p,
 	do_set_cpus_allowed(p, new_mask);
 
 	/* Can the task run on the task's current CPU? If so, we're done */
-	if (cpumask_test_cpu(task_cpu(p), new_mask))
+	if (cpumask_test_cpu(task_cpu(p), new_mask) || __migrate_disabled(p))
 		goto out;
 
 	dest_cpu = cpumask_any_and(cpu_active_mask, new_mask);
@@ -1408,6 +1536,18 @@ int migrate_swap(struct task_struct *cur, struct task_struct *p)
 	return ret;
 }
 
+static bool check_task_state(struct task_struct *p, long match_state)
+{
+	bool match = false;
+
+	raw_spin_lock_irq(&p->pi_lock);
+	if (p->state == match_state || p->saved_state == match_state)
+		match = true;
+	raw_spin_unlock_irq(&p->pi_lock);
+
+	return match;
+}
+
 /*
  * wait_task_inactive - wait for a thread to unschedule.
  *
@@ -1452,7 +1592,7 @@ unsigned long wait_task_inactive(struct task_struct *p, long match_state)
 		 * is actually now running somewhere else!
 		 */
 		while (task_running(rq, p)) {
-			if (match_state && unlikely(p->state != match_state))
+			if (match_state && !check_task_state(p, match_state))
 				return 0;
 			cpu_relax();
 		}
@@ -1467,7 +1607,8 @@ unsigned long wait_task_inactive(struct task_struct *p, long match_state)
 		running = task_running(rq, p);
 		queued = task_on_rq_queued(p);
 		ncsw = 0;
-		if (!match_state || p->state == match_state)
+		if (!match_state || p->state == match_state ||
+		    p->saved_state == match_state)
 			ncsw = p->nvcsw | LONG_MIN; /* sets MSB */
 		task_rq_unlock(rq, p, &flags);
 
@@ -1624,7 +1765,7 @@ int select_task_rq(struct task_struct *p, int cpu, int sd_flags, int wake_flags)
 {
 	lockdep_assert_held(&p->pi_lock);
 
-	if (p->nr_cpus_allowed > 1)
+	if (tsk_nr_cpus_allowed(p) > 1)
 		cpu = p->sched_class->select_task_rq(p, cpu, sd_flags, wake_flags);
 
 	/*
@@ -1704,10 +1845,6 @@ static inline void ttwu_activate(struct rq *rq, struct task_struct *p, int en_fl
 {
 	activate_task(rq, p, en_flags);
 	p->on_rq = TASK_ON_RQ_QUEUED;
-
-	/* if a worker is waking up, notify workqueue */
-	if (p->flags & PF_WQ_WORKER)
-		wq_worker_waking_up(p, cpu_of(rq));
 }
 
 /*
@@ -1934,8 +2071,27 @@ try_to_wake_up(struct task_struct *p, unsigned int state, int wake_flags)
 	 */
 	smp_mb__before_spinlock();
 	raw_spin_lock_irqsave(&p->pi_lock, flags);
-	if (!(p->state & state))
+	if (!(p->state & state)) {
+		/*
+		 * The task might be running due to a spinlock sleeper
+		 * wakeup. Check the saved state and set it to running
+		 * if the wakeup condition is true.
+		 */
+		if (!(wake_flags & WF_LOCK_SLEEPER)) {
+			if (p->saved_state & state) {
+				p->saved_state = TASK_RUNNING;
+				success = 1;
+			}
+		}
 		goto out;
+	}
+
+	/*
+	 * If this is a regular wakeup, then we can unconditionally
+	 * clear the saved state of a "lock sleeper".
+	 */
+	if (!(wake_flags & WF_LOCK_SLEEPER))
+		p->saved_state = TASK_RUNNING;
 
 	trace_sched_waking(p);
 
@@ -2005,52 +2161,6 @@ try_to_wake_up(struct task_struct *p, unsigned int state, int wake_flags)
 }
 
 /**
- * try_to_wake_up_local - try to wake up a local task with rq lock held
- * @p: the thread to be awakened
- *
- * Put @p on the run-queue if it's not already there. The caller must
- * ensure that this_rq() is locked, @p is bound to this_rq() and not
- * the current task.
- */
-static void try_to_wake_up_local(struct task_struct *p)
-{
-	struct rq *rq = task_rq(p);
-
-	if (WARN_ON_ONCE(rq != this_rq()) ||
-	    WARN_ON_ONCE(p == current))
-		return;
-
-	lockdep_assert_held(&rq->lock);
-
-	if (!raw_spin_trylock(&p->pi_lock)) {
-		/*
-		 * This is OK, because current is on_cpu, which avoids it being
-		 * picked for load-balance and preemption/IRQs are still
-		 * disabled avoiding further scheduler activity on it and we've
-		 * not yet picked a replacement task.
-		 */
-		lockdep_unpin_lock(&rq->lock);
-		raw_spin_unlock(&rq->lock);
-		raw_spin_lock(&p->pi_lock);
-		raw_spin_lock(&rq->lock);
-		lockdep_pin_lock(&rq->lock);
-	}
-
-	if (!(p->state & TASK_NORMAL))
-		goto out;
-
-	trace_sched_waking(p);
-
-	if (!task_on_rq_queued(p))
-		ttwu_activate(rq, p, ENQUEUE_WAKEUP);
-
-	ttwu_do_wakeup(rq, p, 0);
-	ttwu_stat(p, smp_processor_id(), 0);
-out:
-	raw_spin_unlock(&p->pi_lock);
-}
-
-/**
  * wake_up_process - Wake up a specific process
  * @p: The process to be woken up.
  *
@@ -2068,6 +2178,18 @@ int wake_up_process(struct task_struct *p)
 }
 EXPORT_SYMBOL(wake_up_process);
 
+/**
+ * wake_up_lock_sleeper - Wake up a specific process blocked on a "sleeping lock"
+ * @p: The process to be woken up.
+ *
+ * Same as wake_up_process() above, but wake_flags=WF_LOCK_SLEEPER to indicate
+ * the nature of the wakeup.
+ */
+int wake_up_lock_sleeper(struct task_struct *p)
+{
+	return try_to_wake_up(p, TASK_ALL, WF_LOCK_SLEEPER);
+}
+
 int wake_up_state(struct task_struct *p, unsigned int state)
 {
 	return try_to_wake_up(p, state, 0);
@@ -2254,6 +2376,9 @@ int sched_fork(unsigned long clone_flags, struct task_struct *p)
 	p->on_cpu = 0;
 #endif
 	init_task_preempt_count(p);
+#ifdef CONFIG_HAVE_PREEMPT_LAZY
+	task_thread_info(p)->preempt_lazy_count = 0;
+#endif
 #ifdef CONFIG_SMP
 	plist_node_init(&p->pushable_tasks, MAX_PRIO);
 	RB_CLEAR_NODE(&p->pushable_dl_tasks);
@@ -2578,8 +2703,12 @@ static struct rq *finish_task_switch(struct task_struct *prev)
 	finish_arch_post_lock_switch();
 
 	fire_sched_in_preempt_notifiers(current);
+	/*
+	 * We use mmdrop_delayed() here so we don't have to do the
+	 * full __mmdrop() when we are the last user.
+	 */
 	if (mm)
-		mmdrop(mm);
+		mmdrop_delayed(mm);
 	if (unlikely(prev_state == TASK_DEAD)) {
 		if (prev->sched_class->task_dead)
 			prev->sched_class->task_dead(prev);
@@ -3022,6 +3151,78 @@ static inline void schedule_debug(struct task_struct *prev)
 	schedstat_inc(this_rq(), sched_count);
 }
 
+#if defined(CONFIG_PREEMPT_RT_FULL) && defined(CONFIG_SMP)
+
+void migrate_disable(void)
+{
+	struct task_struct *p = current;
+
+	if (in_atomic()) {
+#ifdef CONFIG_SCHED_DEBUG
+		p->migrate_disable_atomic++;
+#endif
+		return;
+	}
+
+#ifdef CONFIG_SCHED_DEBUG
+	if (unlikely(p->migrate_disable_atomic)) {
+		tracing_off();
+		WARN_ON_ONCE(1);
+	}
+#endif
+
+	if (p->migrate_disable) {
+		p->migrate_disable++;
+		return;
+	}
+
+	preempt_disable();
+	preempt_lazy_disable();
+	pin_current_cpu();
+	p->migrate_disable = 1;
+	p->nr_cpus_allowed = 1;
+	preempt_enable();
+}
+EXPORT_SYMBOL(migrate_disable);
+
+void migrate_enable(void)
+{
+	struct task_struct *p = current;
+
+	if (in_atomic()) {
+#ifdef CONFIG_SCHED_DEBUG
+		p->migrate_disable_atomic--;
+#endif
+		return;
+	}
+
+#ifdef CONFIG_SCHED_DEBUG
+	if (unlikely(p->migrate_disable_atomic)) {
+		tracing_off();
+		WARN_ON_ONCE(1);
+	}
+#endif
+	WARN_ON_ONCE(p->migrate_disable <= 0);
+
+	if (p->migrate_disable > 1) {
+		p->migrate_disable--;
+		return;
+	}
+
+	preempt_disable();
+	/*
+	 * Clearing migrate_disable causes tsk_cpus_allowed to
+	 * show the tasks original cpu affinity.
+	 */
+	p->migrate_disable = 0;
+
+	unpin_current_cpu();
+	preempt_enable();
+	preempt_lazy_enable();
+}
+EXPORT_SYMBOL(migrate_enable);
+#endif
+
 /*
  * Pick up the highest-prio task:
  */
@@ -3146,19 +3347,6 @@ static void __sched notrace __schedule(bool preempt)
 		} else {
 			deactivate_task(rq, prev, DEQUEUE_SLEEP);
 			prev->on_rq = 0;
-
-			/*
-			 * If a worker went to sleep, notify and ask workqueue
-			 * whether it wants to wake up a task to maintain
-			 * concurrency.
-			 */
-			if (prev->flags & PF_WQ_WORKER) {
-				struct task_struct *to_wakeup;
-
-				to_wakeup = wq_worker_sleeping(prev, cpu);
-				if (to_wakeup)
-					try_to_wake_up_local(to_wakeup);
-			}
 		}
 		switch_count = &prev->nvcsw;
 	}
@@ -3168,6 +3356,7 @@ static void __sched notrace __schedule(bool preempt)
 
 	next = pick_next_task(rq, prev);
 	clear_tsk_need_resched(prev);
+	clear_tsk_need_resched_lazy(prev);
 	clear_preempt_need_resched();
 	rq->clock_skip_update = 0;
 
@@ -3189,9 +3378,20 @@ static void __sched notrace __schedule(bool preempt)
 
 static inline void sched_submit_work(struct task_struct *tsk)
 {
-	if (!tsk->state || tsk_is_pi_blocked(tsk))
+	if (!tsk->state)
 		return;
 	/*
+	 * If a worker went to sleep, notify and ask workqueue whether
+	 * it wants to wake up a task to maintain concurrency.
+	 */
+	if (tsk->flags & PF_WQ_WORKER)
+		wq_worker_sleeping(tsk);
+
+
+	if (tsk_is_pi_blocked(tsk))
+		return;
+
+	/*
 	 * If we are going to sleep and we have plugged IO queued,
 	 * make sure to submit it to avoid deadlocks.
 	 */
@@ -3199,6 +3399,12 @@ static inline void sched_submit_work(struct task_struct *tsk)
 		blk_schedule_flush_plug(tsk);
 }
 
+static void sched_update_worker(struct task_struct *tsk)
+{
+	if (tsk->flags & PF_WQ_WORKER)
+		wq_worker_running(tsk);
+}
+
 asmlinkage __visible void __sched schedule(void)
 {
 	struct task_struct *tsk = current;
@@ -3209,6 +3415,7 @@ asmlinkage __visible void __sched schedule(void)
 		__schedule(false);
 		sched_preempt_enable_no_resched();
 	} while (need_resched());
+	sched_update_worker(tsk);
 }
 EXPORT_SYMBOL(schedule);
 
@@ -3257,6 +3464,30 @@ static void __sched notrace preempt_schedule_common(void)
 	} while (need_resched());
 }
 
+#ifdef CONFIG_PREEMPT_LAZY
+/*
+ * If TIF_NEED_RESCHED is then we allow to be scheduled away since this is
+ * set by a RT task. Oterwise we try to avoid beeing scheduled out as long as
+ * preempt_lazy_count counter >0.
+ */
+static int preemptible_lazy(void)
+{
+	if (test_thread_flag(TIF_NEED_RESCHED))
+		return 1;
+	if (current_thread_info()->preempt_lazy_count)
+		return 0;
+	return 1;
+}
+
+#else
+
+static int preemptible_lazy(void)
+{
+	return 1;
+}
+
+#endif
+
 #ifdef CONFIG_PREEMPT
 /*
  * this is the entry point to schedule() from in-kernel preemption
@@ -3271,6 +3502,8 @@ asmlinkage __visible void __sched notrace preempt_schedule(void)
 	 */
 	if (likely(!preemptible()))
 		return;
+	if (!preemptible_lazy())
+		return;
 
 	preempt_schedule_common();
 }
@@ -3297,6 +3530,8 @@ asmlinkage __visible void __sched notrace preempt_schedule_notrace(void)
 
 	if (likely(!preemptible()))
 		return;
+	if (!preemptible_lazy())
+		return;
 
 	do {
 		preempt_disable_notrace();
@@ -3306,7 +3541,16 @@ asmlinkage __visible void __sched notrace preempt_schedule_notrace(void)
 		 * an infinite recursion.
 		 */
 		prev_ctx = exception_enter();
+		/*
+		 * The add/subtract must not be traced by the function
+		 * tracer. But we still want to account for the
+		 * preempt off latency tracer. Since the _notrace versions
+		 * of add/subtract skip the accounting for latency tracer
+		 * we must force it manually.
+		 */
+		start_critical_timings();
 		__schedule(true);
+		stop_critical_timings();
 		exception_exit(prev_ctx);
 
 		preempt_enable_no_resched_notrace();
@@ -4650,6 +4894,7 @@ int __cond_resched_lock(spinlock_t *lock)
 }
 EXPORT_SYMBOL(__cond_resched_lock);
 
+#ifndef CONFIG_PREEMPT_RT_FULL
 int __sched __cond_resched_softirq(void)
 {
 	BUG_ON(!in_softirq());
@@ -4663,6 +4908,7 @@ int __sched __cond_resched_softirq(void)
 	return 0;
 }
 EXPORT_SYMBOL(__cond_resched_softirq);
+#endif
 
 /**
  * yield - yield the current processor to other threads.
@@ -5027,7 +5273,9 @@ void init_idle(struct task_struct *idle, int cpu)
 
 	/* Set the preempt count _outside_ the spinlocks! */
 	init_idle_preempt_count(idle, cpu);
-
+#ifdef CONFIG_HAVE_PREEMPT_LAZY
+	task_thread_info(idle)->preempt_lazy_count = 0;
+#endif
 	/*
 	 * The idle tasks have their own, simple scheduling class:
 	 */
@@ -5168,6 +5416,8 @@ void sched_setnuma(struct task_struct *p, int nid)
 #endif /* CONFIG_NUMA_BALANCING */
 
 #ifdef CONFIG_HOTPLUG_CPU
+static DEFINE_PER_CPU(struct mm_struct *, idle_last_mm);
+
 /*
  * Ensures that the idle task is using init_mm right before its cpu goes
  * offline.
@@ -5182,7 +5432,11 @@ void idle_task_exit(void)
 		switch_mm(mm, &init_mm, current);
 		finish_arch_post_lock_switch();
 	}
-	mmdrop(mm);
+	/*
+	 * Defer the cleanup to an alive cpu. On RT we can neither
+	 * call mmdrop() nor mmdrop_delayed() from here.
+	 */
+	per_cpu(idle_last_mm, smp_processor_id()) = mm;
 }
 
 /*
@@ -5554,6 +5808,10 @@ migration_call(struct notifier_block *nfb, unsigned long action, void *hcpu)
 
 	case CPU_DEAD:
 		calc_load_migrate(rq);
+		if (per_cpu(idle_last_mm, cpu)) {
+			mmdrop(per_cpu(idle_last_mm, cpu));
+			per_cpu(idle_last_mm, cpu) = NULL;
+		}
 		break;
 #endif
 	}
@@ -7537,7 +7795,7 @@ void __init sched_init(void)
 #ifdef CONFIG_DEBUG_ATOMIC_SLEEP
 static inline int preempt_count_equals(int preempt_offset)
 {
-	int nested = preempt_count() + rcu_preempt_depth();
+	int nested = preempt_count() + sched_rcu_preempt_depth();
 
 	return (nested == preempt_offset);
 }
diff --git a/kernel/sched/cpudeadline.c b/kernel/sched/cpudeadline.c
index 5a75b08cfd85..5be58820465c 100644
--- a/kernel/sched/cpudeadline.c
+++ b/kernel/sched/cpudeadline.c
@@ -103,10 +103,10 @@ int cpudl_find(struct cpudl *cp, struct task_struct *p,
 	const struct sched_dl_entity *dl_se = &p->dl;
 
 	if (later_mask &&
-	    cpumask_and(later_mask, cp->free_cpus, &p->cpus_allowed)) {
+	    cpumask_and(later_mask, cp->free_cpus, tsk_cpus_allowed(p))) {
 		best_cpu = cpumask_any(later_mask);
 		goto out;
-	} else if (cpumask_test_cpu(cpudl_maximum(cp), &p->cpus_allowed) &&
+	} else if (cpumask_test_cpu(cpudl_maximum(cp), tsk_cpus_allowed(p)) &&
 			dl_time_before(dl_se->deadline, cp->elements[0].dl)) {
 		best_cpu = cpudl_maximum(cp);
 		if (later_mask)
diff --git a/kernel/sched/cpupri.c b/kernel/sched/cpupri.c
index 981fcd7dc394..11e9705bf937 100644
--- a/kernel/sched/cpupri.c
+++ b/kernel/sched/cpupri.c
@@ -103,11 +103,11 @@ int cpupri_find(struct cpupri *cp, struct task_struct *p,
 		if (skip)
 			continue;
 
-		if (cpumask_any_and(&p->cpus_allowed, vec->mask) >= nr_cpu_ids)
+		if (cpumask_any_and(tsk_cpus_allowed(p), vec->mask) >= nr_cpu_ids)
 			continue;
 
 		if (lowest_mask) {
-			cpumask_and(lowest_mask, &p->cpus_allowed, vec->mask);
+			cpumask_and(lowest_mask, tsk_cpus_allowed(p), vec->mask);
 
 			/*
 			 * We have to ensure that we have at least one bit
diff --git a/kernel/sched/cputime.c b/kernel/sched/cputime.c
index 05de80b48586..0f75a38cff96 100644
--- a/kernel/sched/cputime.c
+++ b/kernel/sched/cputime.c
@@ -696,37 +696,45 @@ static void __vtime_account_system(struct task_struct *tsk)
 
 void vtime_account_system(struct task_struct *tsk)
 {
-	write_seqlock(&tsk->vtime_seqlock);
+	raw_spin_lock(&tsk->vtime_lock);
+	write_seqcount_begin(&tsk->vtime_seq);
 	__vtime_account_system(tsk);
-	write_sequnlock(&tsk->vtime_seqlock);
+	write_seqcount_end(&tsk->vtime_seq);
+	raw_spin_unlock(&tsk->vtime_lock);
 }
 
 void vtime_gen_account_irq_exit(struct task_struct *tsk)
 {
-	write_seqlock(&tsk->vtime_seqlock);
+	raw_spin_lock(&tsk->vtime_lock);
+	write_seqcount_begin(&tsk->vtime_seq);
 	__vtime_account_system(tsk);
 	if (context_tracking_in_user())
 		tsk->vtime_snap_whence = VTIME_USER;
-	write_sequnlock(&tsk->vtime_seqlock);
+	write_seqcount_end(&tsk->vtime_seq);
+	raw_spin_unlock(&tsk->vtime_lock);
 }
 
 void vtime_account_user(struct task_struct *tsk)
 {
 	cputime_t delta_cpu;
 
-	write_seqlock(&tsk->vtime_seqlock);
+	raw_spin_lock(&tsk->vtime_lock);
+	write_seqcount_begin(&tsk->vtime_seq);
 	delta_cpu = get_vtime_delta(tsk);
 	tsk->vtime_snap_whence = VTIME_SYS;
 	account_user_time(tsk, delta_cpu, cputime_to_scaled(delta_cpu));
-	write_sequnlock(&tsk->vtime_seqlock);
+	write_seqcount_end(&tsk->vtime_seq);
+	raw_spin_unlock(&tsk->vtime_lock);
 }
 
 void vtime_user_enter(struct task_struct *tsk)
 {
-	write_seqlock(&tsk->vtime_seqlock);
+	raw_spin_lock(&tsk->vtime_lock);
+	write_seqcount_begin(&tsk->vtime_seq);
 	__vtime_account_system(tsk);
 	tsk->vtime_snap_whence = VTIME_USER;
-	write_sequnlock(&tsk->vtime_seqlock);
+	write_seqcount_end(&tsk->vtime_seq);
+	raw_spin_unlock(&tsk->vtime_lock);
 }
 
 void vtime_guest_enter(struct task_struct *tsk)
@@ -738,19 +746,23 @@ void vtime_guest_enter(struct task_struct *tsk)
 	 * synchronization against the reader (task_gtime())
 	 * that can thus safely catch up with a tickless delta.
 	 */
-	write_seqlock(&tsk->vtime_seqlock);
+	raw_spin_lock(&tsk->vtime_lock);
+	write_seqcount_begin(&tsk->vtime_seq);
 	__vtime_account_system(tsk);
 	current->flags |= PF_VCPU;
-	write_sequnlock(&tsk->vtime_seqlock);
+	write_seqcount_end(&tsk->vtime_seq);
+	raw_spin_unlock(&tsk->vtime_lock);
 }
 EXPORT_SYMBOL_GPL(vtime_guest_enter);
 
 void vtime_guest_exit(struct task_struct *tsk)
 {
-	write_seqlock(&tsk->vtime_seqlock);
+	raw_spin_lock(&tsk->vtime_lock);
+	write_seqcount_begin(&tsk->vtime_seq);
 	__vtime_account_system(tsk);
 	current->flags &= ~PF_VCPU;
-	write_sequnlock(&tsk->vtime_seqlock);
+	write_seqcount_end(&tsk->vtime_seq);
+	raw_spin_unlock(&tsk->vtime_lock);
 }
 EXPORT_SYMBOL_GPL(vtime_guest_exit);
 
@@ -763,24 +775,30 @@ void vtime_account_idle(struct task_struct *tsk)
 
 void arch_vtime_task_switch(struct task_struct *prev)
 {
-	write_seqlock(&prev->vtime_seqlock);
+	raw_spin_lock(&prev->vtime_lock);
+	write_seqcount_begin(&prev->vtime_seq);
 	prev->vtime_snap_whence = VTIME_SLEEPING;
-	write_sequnlock(&prev->vtime_seqlock);
+	write_seqcount_end(&prev->vtime_seq);
+	raw_spin_unlock(&prev->vtime_lock);
 
-	write_seqlock(&current->vtime_seqlock);
+	raw_spin_lock(&current->vtime_lock);
+	write_seqcount_begin(&current->vtime_seq);
 	current->vtime_snap_whence = VTIME_SYS;
 	current->vtime_snap = sched_clock_cpu(smp_processor_id());
-	write_sequnlock(&current->vtime_seqlock);
+	write_seqcount_end(&current->vtime_seq);
+	raw_spin_unlock(&current->vtime_lock);
 }
 
 void vtime_init_idle(struct task_struct *t, int cpu)
 {
 	unsigned long flags;
 
-	write_seqlock_irqsave(&t->vtime_seqlock, flags);
+	raw_spin_lock_irqsave(&t->vtime_lock, flags);
+	write_seqcount_begin(&t->vtime_seq);
 	t->vtime_snap_whence = VTIME_SYS;
 	t->vtime_snap = sched_clock_cpu(cpu);
-	write_sequnlock_irqrestore(&t->vtime_seqlock, flags);
+	write_seqcount_end(&t->vtime_seq);
+	raw_spin_unlock_irqrestore(&t->vtime_lock, flags);
 }
 
 cputime_t task_gtime(struct task_struct *t)
@@ -792,13 +810,13 @@ cputime_t task_gtime(struct task_struct *t)
 		return t->gtime;
 
 	do {
-		seq = read_seqbegin(&t->vtime_seqlock);
+		seq = read_seqcount_begin(&t->vtime_seq);
 
 		gtime = t->gtime;
 		if (t->flags & PF_VCPU)
 			gtime += vtime_delta(t);
 
-	} while (read_seqretry(&t->vtime_seqlock, seq));
+	} while (read_seqcount_retry(&t->vtime_seq, seq));
 
 	return gtime;
 }
@@ -821,7 +839,7 @@ fetch_task_cputime(struct task_struct *t,
 		*udelta = 0;
 		*sdelta = 0;
 
-		seq = read_seqbegin(&t->vtime_seqlock);
+		seq = read_seqcount_begin(&t->vtime_seq);
 
 		if (u_dst)
 			*u_dst = *u_src;
@@ -845,7 +863,7 @@ fetch_task_cputime(struct task_struct *t,
 			if (t->vtime_snap_whence == VTIME_SYS)
 				*sdelta = delta;
 		}
-	} while (read_seqretry(&t->vtime_seqlock, seq));
+	} while (read_seqcount_retry(&t->vtime_seq, seq));
 }
 
 
diff --git a/kernel/sched/deadline.c b/kernel/sched/deadline.c
index 8b0a15e285f9..7a72e69fcf65 100644
--- a/kernel/sched/deadline.c
+++ b/kernel/sched/deadline.c
@@ -134,7 +134,7 @@ static void inc_dl_migration(struct sched_dl_entity *dl_se, struct dl_rq *dl_rq)
 {
 	struct task_struct *p = dl_task_of(dl_se);
 
-	if (p->nr_cpus_allowed > 1)
+	if (tsk_nr_cpus_allowed(p) > 1)
 		dl_rq->dl_nr_migratory++;
 
 	update_dl_migration(dl_rq);
@@ -144,7 +144,7 @@ static void dec_dl_migration(struct sched_dl_entity *dl_se, struct dl_rq *dl_rq)
 {
 	struct task_struct *p = dl_task_of(dl_se);
 
-	if (p->nr_cpus_allowed > 1)
+	if (tsk_nr_cpus_allowed(p) > 1)
 		dl_rq->dl_nr_migratory--;
 
 	update_dl_migration(dl_rq);
@@ -697,6 +697,7 @@ void init_dl_task_timer(struct sched_dl_entity *dl_se)
 
 	hrtimer_init(timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
 	timer->function = dl_task_timer;
+	timer->irqsafe = 1;
 }
 
 static
@@ -989,7 +990,7 @@ static void enqueue_task_dl(struct rq *rq, struct task_struct *p, int flags)
 
 	enqueue_dl_entity(&p->dl, pi_se, flags);
 
-	if (!task_current(rq, p) && p->nr_cpus_allowed > 1)
+	if (!task_current(rq, p) && tsk_nr_cpus_allowed(p) > 1)
 		enqueue_pushable_dl_task(rq, p);
 }
 
@@ -1067,9 +1068,9 @@ select_task_rq_dl(struct task_struct *p, int cpu, int sd_flag, int flags)
 	 * try to make it stay here, it might be important.
 	 */
 	if (unlikely(dl_task(curr)) &&
-	    (curr->nr_cpus_allowed < 2 ||
+	    (tsk_nr_cpus_allowed(curr) < 2 ||
 	     !dl_entity_preempt(&p->dl, &curr->dl)) &&
-	    (p->nr_cpus_allowed > 1)) {
+	    (tsk_nr_cpus_allowed(p) > 1)) {
 		int target = find_later_rq(p);
 
 		if (target != -1 &&
@@ -1090,7 +1091,7 @@ static void check_preempt_equal_dl(struct rq *rq, struct task_struct *p)
 	 * Current can't be migrated, useless to reschedule,
 	 * let's hope p can move out.
 	 */
-	if (rq->curr->nr_cpus_allowed == 1 ||
+	if (tsk_nr_cpus_allowed(rq->curr) == 1 ||
 	    cpudl_find(&rq->rd->cpudl, rq->curr, NULL) == -1)
 		return;
 
@@ -1098,7 +1099,7 @@ static void check_preempt_equal_dl(struct rq *rq, struct task_struct *p)
 	 * p is migratable, so let's not schedule it and
 	 * see if it is pushed or pulled somewhere else.
 	 */
-	if (p->nr_cpus_allowed != 1 &&
+	if (tsk_nr_cpus_allowed(p) != 1 &&
 	    cpudl_find(&rq->rd->cpudl, p, NULL) != -1)
 		return;
 
@@ -1212,7 +1213,7 @@ static void put_prev_task_dl(struct rq *rq, struct task_struct *p)
 {
 	update_curr_dl(rq);
 
-	if (on_dl_rq(&p->dl) && p->nr_cpus_allowed > 1)
+	if (on_dl_rq(&p->dl) && tsk_nr_cpus_allowed(p) > 1)
 		enqueue_pushable_dl_task(rq, p);
 }
 
@@ -1335,7 +1336,7 @@ static int find_later_rq(struct task_struct *task)
 	if (unlikely(!later_mask))
 		return -1;
 
-	if (task->nr_cpus_allowed == 1)
+	if (tsk_nr_cpus_allowed(task) == 1)
 		return -1;
 
 	/*
@@ -1441,7 +1442,7 @@ static struct rq *find_lock_later_rq(struct task_struct *task, struct rq *rq)
 		if (double_lock_balance(rq, later_rq)) {
 			if (unlikely(task_rq(task) != rq ||
 				     !cpumask_test_cpu(later_rq->cpu,
-				                       &task->cpus_allowed) ||
+						       tsk_cpus_allowed(task)) ||
 				     task_running(rq, task) ||
 				     !task_on_rq_queued(task))) {
 				double_unlock_balance(rq, later_rq);
@@ -1480,7 +1481,7 @@ static struct task_struct *pick_next_pushable_dl_task(struct rq *rq)
 
 	BUG_ON(rq->cpu != task_cpu(p));
 	BUG_ON(task_current(rq, p));
-	BUG_ON(p->nr_cpus_allowed <= 1);
+	BUG_ON(tsk_nr_cpus_allowed(p) <= 1);
 
 	BUG_ON(!task_on_rq_queued(p));
 	BUG_ON(!dl_task(p));
@@ -1519,7 +1520,7 @@ static int push_dl_task(struct rq *rq)
 	 */
 	if (dl_task(rq->curr) &&
 	    dl_time_before(next_task->dl.deadline, rq->curr->dl.deadline) &&
-	    rq->curr->nr_cpus_allowed > 1) {
+	    tsk_nr_cpus_allowed(rq->curr) > 1) {
 		resched_curr(rq);
 		return 0;
 	}
@@ -1666,9 +1667,9 @@ static void task_woken_dl(struct rq *rq, struct task_struct *p)
 {
 	if (!task_running(rq, p) &&
 	    !test_tsk_need_resched(rq->curr) &&
-	    p->nr_cpus_allowed > 1 &&
+	    tsk_nr_cpus_allowed(p) > 1 &&
 	    dl_task(rq->curr) &&
-	    (rq->curr->nr_cpus_allowed < 2 ||
+	    (tsk_nr_cpus_allowed(rq->curr) < 2 ||
 	     !dl_entity_preempt(&p->dl, &rq->curr->dl))) {
 		push_dl_tasks(rq);
 	}
@@ -1769,7 +1770,7 @@ static void switched_to_dl(struct rq *rq, struct task_struct *p)
 {
 	if (task_on_rq_queued(p) && rq->curr != p) {
 #ifdef CONFIG_SMP
-		if (p->nr_cpus_allowed > 1 && rq->dl.overloaded)
+		if (tsk_nr_cpus_allowed(p) > 1 && rq->dl.overloaded)
 			queue_push_tasks(rq);
 #else
 		if (dl_task(rq->curr))
diff --git a/kernel/sched/debug.c b/kernel/sched/debug.c
index 641511771ae6..a2d69b883623 100644
--- a/kernel/sched/debug.c
+++ b/kernel/sched/debug.c
@@ -251,6 +251,9 @@ void print_rt_rq(struct seq_file *m, int cpu, struct rt_rq *rt_rq)
 	P(rt_throttled);
 	PN(rt_time);
 	PN(rt_runtime);
+#ifdef CONFIG_SMP
+	P(rt_nr_migratory);
+#endif
 
 #undef PN
 #undef P
@@ -635,6 +638,10 @@ void proc_sched_show_task(struct task_struct *p, struct seq_file *m)
 #endif
 	P(policy);
 	P(prio);
+#ifdef CONFIG_PREEMPT_RT_FULL
+	P(migrate_disable);
+#endif
+	P(nr_cpus_allowed);
 #undef PN
 #undef __PN
 #undef P
diff --git a/kernel/sched/fair.c b/kernel/sched/fair.c
index cfdc0e61066c..8c71e627b5f2 100644
--- a/kernel/sched/fair.c
+++ b/kernel/sched/fair.c
@@ -3135,7 +3135,7 @@ check_preempt_tick(struct cfs_rq *cfs_rq, struct sched_entity *curr)
 	ideal_runtime = sched_slice(cfs_rq, curr);
 	delta_exec = curr->sum_exec_runtime - curr->prev_sum_exec_runtime;
 	if (delta_exec > ideal_runtime) {
-		resched_curr(rq_of(cfs_rq));
+		resched_curr_lazy(rq_of(cfs_rq));
 		/*
 		 * The current task ran long enough, ensure it doesn't get
 		 * re-elected due to buddy favours.
@@ -3159,7 +3159,7 @@ check_preempt_tick(struct cfs_rq *cfs_rq, struct sched_entity *curr)
 		return;
 
 	if (delta > ideal_runtime)
-		resched_curr(rq_of(cfs_rq));
+		resched_curr_lazy(rq_of(cfs_rq));
 }
 
 static void
@@ -3299,7 +3299,7 @@ entity_tick(struct cfs_rq *cfs_rq, struct sched_entity *curr, int queued)
 	 * validating it and just reschedule.
 	 */
 	if (queued) {
-		resched_curr(rq_of(cfs_rq));
+		resched_curr_lazy(rq_of(cfs_rq));
 		return;
 	}
 	/*
@@ -3481,7 +3481,7 @@ static void __account_cfs_rq_runtime(struct cfs_rq *cfs_rq, u64 delta_exec)
 	 * hierarchy can be throttled
 	 */
 	if (!assign_cfs_rq_runtime(cfs_rq) && likely(cfs_rq->curr))
-		resched_curr(rq_of(cfs_rq));
+		resched_curr_lazy(rq_of(cfs_rq));
 }
 
 static __always_inline
@@ -4093,7 +4093,7 @@ static void hrtick_start_fair(struct rq *rq, struct task_struct *p)
 
 		if (delta < 0) {
 			if (rq->curr == p)
-				resched_curr(rq);
+				resched_curr_lazy(rq);
 			return;
 		}
 		hrtick_start(rq, delta);
@@ -5177,7 +5177,7 @@ static void check_preempt_wakeup(struct rq *rq, struct task_struct *p, int wake_
 	return;
 
 preempt:
-	resched_curr(rq);
+	resched_curr_lazy(rq);
 	/*
 	 * Only set the backward buddy when the current task is still
 	 * on the rq. This can happen when a wakeup gets interleaved
@@ -7928,7 +7928,7 @@ static void task_fork_fair(struct task_struct *p)
 		 * 'current' within the tree based on its new key value.
 		 */
 		swap(curr->vruntime, se->vruntime);
-		resched_curr(rq);
+		resched_curr_lazy(rq);
 	}
 
 	se->vruntime -= cfs_rq->min_vruntime;
@@ -7953,7 +7953,7 @@ prio_changed_fair(struct rq *rq, struct task_struct *p, int oldprio)
 	 */
 	if (rq->curr == p) {
 		if (p->prio > oldprio)
-			resched_curr(rq);
+			resched_curr_lazy(rq);
 	} else
 		check_preempt_curr(rq, p, 0);
 }
diff --git a/kernel/sched/features.h b/kernel/sched/features.h
index 69631fa46c2f..6d28fcd08872 100644
--- a/kernel/sched/features.h
+++ b/kernel/sched/features.h
@@ -45,11 +45,19 @@ SCHED_FEAT(LB_BIAS, true)
  */
 SCHED_FEAT(NONTASK_CAPACITY, true)
 
+#ifdef CONFIG_PREEMPT_RT_FULL
+SCHED_FEAT(TTWU_QUEUE, false)
+# ifdef CONFIG_PREEMPT_LAZY
+SCHED_FEAT(PREEMPT_LAZY, true)
+# endif
+#else
+
 /*
  * Queue remote wakeups on the target CPU and process them
  * using the scheduler IPI. Reduces rq->lock contention/bounces.
  */
 SCHED_FEAT(TTWU_QUEUE, true)
+#endif
 
 #ifdef HAVE_RT_PUSH_IPI
 /*
diff --git a/kernel/sched/rt.c b/kernel/sched/rt.c
index 8ec86abe0ea1..8cf360d309ec 100644
--- a/kernel/sched/rt.c
+++ b/kernel/sched/rt.c
@@ -47,6 +47,7 @@ void init_rt_bandwidth(struct rt_bandwidth *rt_b, u64 period, u64 runtime)
 
 	hrtimer_init(&rt_b->rt_period_timer,
 			CLOCK_MONOTONIC, HRTIMER_MODE_REL);
+	rt_b->rt_period_timer.irqsafe = 1;
 	rt_b->rt_period_timer.function = sched_rt_period_timer;
 }
 
@@ -93,6 +94,7 @@ void init_rt_rq(struct rt_rq *rt_rq)
 	rt_rq->push_cpu = nr_cpu_ids;
 	raw_spin_lock_init(&rt_rq->push_lock);
 	init_irq_work(&rt_rq->push_work, push_irq_work_func);
+	rt_rq->push_work.flags |= IRQ_WORK_HARD_IRQ;
 #endif
 #endif /* CONFIG_SMP */
 	/* We start is dequeued state, because no RT tasks are queued */
@@ -326,7 +328,7 @@ static void inc_rt_migration(struct sched_rt_entity *rt_se, struct rt_rq *rt_rq)
 	rt_rq = &rq_of_rt_rq(rt_rq)->rt;
 
 	rt_rq->rt_nr_total++;
-	if (p->nr_cpus_allowed > 1)
+	if (tsk_nr_cpus_allowed(p) > 1)
 		rt_rq->rt_nr_migratory++;
 
 	update_rt_migration(rt_rq);
@@ -343,7 +345,7 @@ static void dec_rt_migration(struct sched_rt_entity *rt_se, struct rt_rq *rt_rq)
 	rt_rq = &rq_of_rt_rq(rt_rq)->rt;
 
 	rt_rq->rt_nr_total--;
-	if (p->nr_cpus_allowed > 1)
+	if (tsk_nr_cpus_allowed(p) > 1)
 		rt_rq->rt_nr_migratory--;
 
 	update_rt_migration(rt_rq);
@@ -1262,7 +1264,7 @@ enqueue_task_rt(struct rq *rq, struct task_struct *p, int flags)
 
 	enqueue_rt_entity(rt_se, flags & ENQUEUE_HEAD);
 
-	if (!task_current(rq, p) && p->nr_cpus_allowed > 1)
+	if (!task_current(rq, p) && tsk_nr_cpus_allowed(p) > 1)
 		enqueue_pushable_task(rq, p);
 }
 
@@ -1351,7 +1353,7 @@ select_task_rq_rt(struct task_struct *p, int cpu, int sd_flag, int flags)
 	 * will have to sort it out.
 	 */
 	if (curr && unlikely(rt_task(curr)) &&
-	    (curr->nr_cpus_allowed < 2 ||
+	    (tsk_nr_cpus_allowed(curr) < 2 ||
 	     curr->prio <= p->prio)) {
 		int target = find_lowest_rq(p);
 
@@ -1375,7 +1377,7 @@ static void check_preempt_equal_prio(struct rq *rq, struct task_struct *p)
 	 * Current can't be migrated, useless to reschedule,
 	 * let's hope p can move out.
 	 */
-	if (rq->curr->nr_cpus_allowed == 1 ||
+	if (tsk_nr_cpus_allowed(rq->curr) == 1 ||
 	    !cpupri_find(&rq->rd->cpupri, rq->curr, NULL))
 		return;
 
@@ -1383,7 +1385,7 @@ static void check_preempt_equal_prio(struct rq *rq, struct task_struct *p)
 	 * p is migratable, so let's not schedule it and
 	 * see if it is pushed or pulled somewhere else.
 	 */
-	if (p->nr_cpus_allowed != 1
+	if (tsk_nr_cpus_allowed(p) != 1
 	    && cpupri_find(&rq->rd->cpupri, p, NULL))
 		return;
 
@@ -1517,7 +1519,7 @@ static void put_prev_task_rt(struct rq *rq, struct task_struct *p)
 	 * The previous task needs to be made eligible for pushing
 	 * if it is still active
 	 */
-	if (on_rt_rq(&p->rt) && p->nr_cpus_allowed > 1)
+	if (on_rt_rq(&p->rt) && tsk_nr_cpus_allowed(p) > 1)
 		enqueue_pushable_task(rq, p);
 }
 
@@ -1567,7 +1569,7 @@ static int find_lowest_rq(struct task_struct *task)
 	if (unlikely(!lowest_mask))
 		return -1;
 
-	if (task->nr_cpus_allowed == 1)
+	if (tsk_nr_cpus_allowed(task) == 1)
 		return -1; /* No other targets possible */
 
 	if (!cpupri_find(&task_rq(task)->rd->cpupri, task, lowest_mask))
@@ -1699,7 +1701,7 @@ static struct task_struct *pick_next_pushable_task(struct rq *rq)
 
 	BUG_ON(rq->cpu != task_cpu(p));
 	BUG_ON(task_current(rq, p));
-	BUG_ON(p->nr_cpus_allowed <= 1);
+	BUG_ON(tsk_nr_cpus_allowed(p) <= 1);
 
 	BUG_ON(!task_on_rq_queued(p));
 	BUG_ON(!rt_task(p));
@@ -2059,9 +2061,9 @@ static void task_woken_rt(struct rq *rq, struct task_struct *p)
 {
 	if (!task_running(rq, p) &&
 	    !test_tsk_need_resched(rq->curr) &&
-	    p->nr_cpus_allowed > 1 &&
+	    tsk_nr_cpus_allowed(p) > 1 &&
 	    (dl_task(rq->curr) || rt_task(rq->curr)) &&
-	    (rq->curr->nr_cpus_allowed < 2 ||
+	    (tsk_nr_cpus_allowed(rq->curr) < 2 ||
 	     rq->curr->prio <= p->prio))
 		push_rt_tasks(rq);
 }
@@ -2134,7 +2136,7 @@ static void switched_to_rt(struct rq *rq, struct task_struct *p)
 	 */
 	if (task_on_rq_queued(p) && rq->curr != p) {
 #ifdef CONFIG_SMP
-		if (p->nr_cpus_allowed > 1 && rq->rt.overloaded)
+		if (tsk_nr_cpus_allowed(p) > 1 && rq->rt.overloaded)
 			queue_push_tasks(rq);
 #else
 		if (p->prio < rq->curr->prio)
diff --git a/kernel/sched/sched.h b/kernel/sched/sched.h
index b242775bf670..7dd5f53b6ae9 100644
--- a/kernel/sched/sched.h
+++ b/kernel/sched/sched.h
@@ -1100,6 +1100,7 @@ static inline void finish_lock_switch(struct rq *rq, struct task_struct *prev)
 #define WF_SYNC		0x01		/* waker goes to sleep after wakeup */
 #define WF_FORK		0x02		/* child wakeup after fork */
 #define WF_MIGRATED	0x4		/* internal use, task got migrated */
+#define WF_LOCK_SLEEPER	0x08		/* wakeup spinlock "sleeper" */
 
 /*
  * To aid in avoiding the subversion of "niceness" due to uneven distribution
@@ -1299,6 +1300,15 @@ extern void init_sched_fair_class(void);
 extern void resched_curr(struct rq *rq);
 extern void resched_cpu(int cpu);
 
+#ifdef CONFIG_PREEMPT_LAZY
+extern void resched_curr_lazy(struct rq *rq);
+#else
+static inline void resched_curr_lazy(struct rq *rq)
+{
+	resched_curr(rq);
+}
+#endif
+
 extern struct rt_bandwidth def_rt_bandwidth;
 extern void init_rt_bandwidth(struct rt_bandwidth *rt_b, u64 period, u64 runtime);
 
diff --git a/kernel/sched/wait-simple.c b/kernel/sched/wait-simple.c
new file mode 100644
index 000000000000..7dfa86d1f654
--- /dev/null
+++ b/kernel/sched/wait-simple.c
@@ -0,0 +1,115 @@
+/*
+ * Simple waitqueues without fancy flags and callbacks
+ *
+ * (C) 2011 Thomas Gleixner <tglx@linutronix.de>
+ *
+ * Based on kernel/wait.c
+ *
+ * For licencing details see kernel-base/COPYING
+ */
+#include <linux/init.h>
+#include <linux/export.h>
+#include <linux/sched.h>
+#include <linux/wait-simple.h>
+
+/* Adds w to head->list. Must be called with head->lock locked. */
+static inline void __swait_enqueue(struct swait_head *head, struct swaiter *w)
+{
+	list_add(&w->node, &head->list);
+	/* We can't let the condition leak before the setting of head */
+	smp_mb();
+}
+
+/* Removes w from head->list. Must be called with head->lock locked. */
+static inline void __swait_dequeue(struct swaiter *w)
+{
+	list_del_init(&w->node);
+}
+
+void __init_swait_head(struct swait_head *head, struct lock_class_key *key)
+{
+	raw_spin_lock_init(&head->lock);
+	lockdep_set_class(&head->lock, key);
+	INIT_LIST_HEAD(&head->list);
+}
+EXPORT_SYMBOL(__init_swait_head);
+
+void swait_prepare_locked(struct swait_head *head, struct swaiter *w)
+{
+	w->task = current;
+	if (list_empty(&w->node))
+		__swait_enqueue(head, w);
+}
+
+void swait_prepare(struct swait_head *head, struct swaiter *w, int state)
+{
+	unsigned long flags;
+
+	raw_spin_lock_irqsave(&head->lock, flags);
+	swait_prepare_locked(head, w);
+	__set_current_state(state);
+	raw_spin_unlock_irqrestore(&head->lock, flags);
+}
+EXPORT_SYMBOL(swait_prepare);
+
+void swait_finish_locked(struct swait_head *head, struct swaiter *w)
+{
+	__set_current_state(TASK_RUNNING);
+	if (w->task)
+		__swait_dequeue(w);
+}
+
+void swait_finish(struct swait_head *head, struct swaiter *w)
+{
+	unsigned long flags;
+
+	__set_current_state(TASK_RUNNING);
+	if (w->task) {
+		raw_spin_lock_irqsave(&head->lock, flags);
+		__swait_dequeue(w);
+		raw_spin_unlock_irqrestore(&head->lock, flags);
+	}
+}
+EXPORT_SYMBOL(swait_finish);
+
+unsigned int
+__swait_wake_locked(struct swait_head *head, unsigned int state, unsigned int num)
+{
+	struct swaiter *curr, *next;
+	int woken = 0;
+
+	list_for_each_entry_safe(curr, next, &head->list, node) {
+		if (wake_up_state(curr->task, state)) {
+			__swait_dequeue(curr);
+			/*
+			 * The waiting task can free the waiter as
+			 * soon as curr->task = NULL is written,
+			 * without taking any locks. A memory barrier
+			 * is required here to prevent the following
+			 * store to curr->task from getting ahead of
+			 * the dequeue operation.
+			 */
+			smp_wmb();
+			curr->task = NULL;
+			if (++woken == num)
+				break;
+		}
+	}
+	return woken;
+}
+
+unsigned int
+__swait_wake(struct swait_head *head, unsigned int state, unsigned int num)
+{
+	unsigned long flags;
+	int woken;
+
+	if (!swaitqueue_active(head))
+		return 0;
+
+	raw_spin_lock_irqsave(&head->lock, flags);
+	woken = __swait_wake_locked(head, state, num);
+	raw_spin_unlock_irqrestore(&head->lock, flags);
+	return woken;
+}
+EXPORT_SYMBOL(__swait_wake);
diff --git a/kernel/sched/work-simple.c b/kernel/sched/work-simple.c
new file mode 100644
index 000000000000..e57a0522573f
--- /dev/null
+++ b/kernel/sched/work-simple.c
@@ -0,0 +1,173 @@
+/*
+ * Copyright (C) 2014 BMW Car IT GmbH, Daniel Wagner daniel.wagner@bmw-carit.de
+ *
+ * Provides a framework for enqueuing callbacks from irq context
+ * PREEMPT_RT_FULL safe. The callbacks are executed in kthread context.
+ */
+
+#include <linux/wait-simple.h>
+#include <linux/work-simple.h>
+#include <linux/kthread.h>
+#include <linux/slab.h>
+#include <linux/spinlock.h>
+#include <linux/export.h>
+
+#define SWORK_EVENT_PENDING     (1 << 0)
+
+static DEFINE_MUTEX(worker_mutex);
+static struct sworker *glob_worker;
+
+struct sworker {
+	struct list_head events;
+	struct swait_head wq;
+
+	raw_spinlock_t lock;
+
+	struct task_struct *task;
+	int refs;
+};
+
+static bool swork_readable(struct sworker *worker)
+{
+	bool r;
+
+	if (kthread_should_stop())
+		return true;
+
+	raw_spin_lock_irq(&worker->lock);
+	r = !list_empty(&worker->events);
+	raw_spin_unlock_irq(&worker->lock);
+
+	return r;
+}
+
+static int swork_kthread(void *arg)
+{
+	struct sworker *worker = arg;
+
+	for (;;) {
+		swait_event_interruptible(worker->wq,
+					swork_readable(worker));
+		if (kthread_should_stop())
+			break;
+
+		raw_spin_lock_irq(&worker->lock);
+		while (!list_empty(&worker->events)) {
+			struct swork_event *sev;
+
+			sev = list_first_entry(&worker->events,
+					struct swork_event, item);
+			list_del(&sev->item);
+			raw_spin_unlock_irq(&worker->lock);
+
+			WARN_ON_ONCE(!test_and_clear_bit(SWORK_EVENT_PENDING,
+							 &sev->flags));
+			sev->func(sev);
+			raw_spin_lock_irq(&worker->lock);
+		}
+		raw_spin_unlock_irq(&worker->lock);
+	}
+	return 0;
+}
+
+static struct sworker *swork_create(void)
+{
+	struct sworker *worker;
+
+	worker = kzalloc(sizeof(*worker), GFP_KERNEL);
+	if (!worker)
+		return ERR_PTR(-ENOMEM);
+
+	INIT_LIST_HEAD(&worker->events);
+	raw_spin_lock_init(&worker->lock);
+	init_swait_head(&worker->wq);
+
+	worker->task = kthread_run(swork_kthread, worker, "kswork");
+	if (IS_ERR(worker->task)) {
+		kfree(worker);
+		return ERR_PTR(-ENOMEM);
+	}
+
+	return worker;
+}
+
+static void swork_destroy(struct sworker *worker)
+{
+	kthread_stop(worker->task);
+
+	WARN_ON(!list_empty(&worker->events));
+	kfree(worker);
+}
+
+/**
+ * swork_queue - queue swork
+ *
+ * Returns %false if @work was already on a queue, %true otherwise.
+ *
+ * The work is queued and processed on a random CPU
+ */
+bool swork_queue(struct swork_event *sev)
+{
+	unsigned long flags;
+
+	if (test_and_set_bit(SWORK_EVENT_PENDING, &sev->flags))
+		return false;
+
+	raw_spin_lock_irqsave(&glob_worker->lock, flags);
+	list_add_tail(&sev->item, &glob_worker->events);
+	raw_spin_unlock_irqrestore(&glob_worker->lock, flags);
+
+	swait_wake(&glob_worker->wq);
+	return true;
+}
+EXPORT_SYMBOL_GPL(swork_queue);
+
+/**
+ * swork_get - get an instance of the sworker
+ *
+ * Returns an negative error code if the initialization if the worker did not
+ * work, %0 otherwise.
+ *
+ */
+int swork_get(void)
+{
+	struct sworker *worker;
+
+	mutex_lock(&worker_mutex);
+	if (!glob_worker) {
+		worker = swork_create();
+		if (IS_ERR(worker)) {
+			mutex_unlock(&worker_mutex);
+			return -ENOMEM;
+		}
+
+		glob_worker = worker;
+	}
+
+	glob_worker->refs++;
+	mutex_unlock(&worker_mutex);
+
+	return 0;
+}
+EXPORT_SYMBOL_GPL(swork_get);
+
+/**
+ * swork_put - puts an instance of the sworker
+ *
+ * Will destroy the sworker thread. This function must not be called until all
+ * queued events have been completed.
+ */
+void swork_put(void)
+{
+	mutex_lock(&worker_mutex);
+
+	glob_worker->refs--;
+	if (glob_worker->refs > 0)
+		goto out;
+
+	swork_destroy(glob_worker);
+	glob_worker = NULL;
+out:
+	mutex_unlock(&worker_mutex);
+}
+EXPORT_SYMBOL_GPL(swork_put);
diff --git a/kernel/signal.c b/kernel/signal.c
index f3f1f7a972fd..bc2c990f3f63 100644
--- a/kernel/signal.c
+++ b/kernel/signal.c
@@ -14,6 +14,7 @@
 #include <linux/export.h>
 #include <linux/init.h>
 #include <linux/sched.h>
+#include <linux/sched/rt.h>
 #include <linux/fs.h>
 #include <linux/tty.h>
 #include <linux/binfmts.h>
@@ -352,13 +353,30 @@ static bool task_participate_group_stop(struct task_struct *task)
 	return false;
 }
 
+static inline struct sigqueue *get_task_cache(struct task_struct *t)
+{
+	struct sigqueue *q = t->sigqueue_cache;
+
+	if (cmpxchg(&t->sigqueue_cache, q, NULL) != q)
+		return NULL;
+	return q;
+}
+
+static inline int put_task_cache(struct task_struct *t, struct sigqueue *q)
+{
+	if (cmpxchg(&t->sigqueue_cache, NULL, q) == NULL)
+		return 0;
+	return 1;
+}
+
 /*
  * allocate a new signal queue record
  * - this may be called without locks if and only if t == current, otherwise an
  *   appropriate lock must be held to stop the target task from exiting
  */
 static struct sigqueue *
-__sigqueue_alloc(int sig, struct task_struct *t, gfp_t flags, int override_rlimit)
+__sigqueue_do_alloc(int sig, struct task_struct *t, gfp_t flags,
+		    int override_rlimit, int fromslab)
 {
 	struct sigqueue *q = NULL;
 	struct user_struct *user;
@@ -375,7 +393,10 @@ __sigqueue_alloc(int sig, struct task_struct *t, gfp_t flags, int override_rlimi
 	if (override_rlimit ||
 	    atomic_read(&user->sigpending) <=
 			task_rlimit(t, RLIMIT_SIGPENDING)) {
-		q = kmem_cache_alloc(sigqueue_cachep, flags);
+		if (!fromslab)
+			q = get_task_cache(t);
+		if (!q)
+			q = kmem_cache_alloc(sigqueue_cachep, flags);
 	} else {
 		print_dropped_signal(sig);
 	}
@@ -392,6 +413,13 @@ __sigqueue_alloc(int sig, struct task_struct *t, gfp_t flags, int override_rlimi
 	return q;
 }
 
+static struct sigqueue *
+__sigqueue_alloc(int sig, struct task_struct *t, gfp_t flags,
+		 int override_rlimit)
+{
+	return __sigqueue_do_alloc(sig, t, flags, override_rlimit, 0);
+}
+
 static void __sigqueue_free(struct sigqueue *q)
 {
 	if (q->flags & SIGQUEUE_PREALLOC)
@@ -401,6 +429,21 @@ static void __sigqueue_free(struct sigqueue *q)
 	kmem_cache_free(sigqueue_cachep, q);
 }
 
+static void sigqueue_free_current(struct sigqueue *q)
+{
+	struct user_struct *up;
+
+	if (q->flags & SIGQUEUE_PREALLOC)
+		return;
+
+	up = q->user;
+	if (rt_prio(current->normal_prio) && !put_task_cache(current, q)) {
+		atomic_dec(&up->sigpending);
+		free_uid(up);
+	} else
+		  __sigqueue_free(q);
+}
+
 void flush_sigqueue(struct sigpending *queue)
 {
 	struct sigqueue *q;
@@ -414,6 +457,21 @@ void flush_sigqueue(struct sigpending *queue)
 }
 
 /*
+ * Called from __exit_signal. Flush tsk->pending and
+ * tsk->sigqueue_cache
+ */
+void flush_task_sigqueue(struct task_struct *tsk)
+{
+	struct sigqueue *q;
+
+	flush_sigqueue(&tsk->pending);
+
+	q = get_task_cache(tsk);
+	if (q)
+		kmem_cache_free(sigqueue_cachep, q);
+}
+
+/*
  * Flush all pending signals for this kthread.
  */
 void flush_signals(struct task_struct *t)
@@ -525,7 +583,7 @@ static void collect_signal(int sig, struct sigpending *list, siginfo_t *info)
 still_pending:
 		list_del_init(&first->list);
 		copy_siginfo(info, &first->info);
-		__sigqueue_free(first);
+		sigqueue_free_current(first);
 	} else {
 		/*
 		 * Ok, it wasn't in the queue.  This must be
@@ -560,6 +618,8 @@ int dequeue_signal(struct task_struct *tsk, sigset_t *mask, siginfo_t *info)
 {
 	int signr;
 
+	WARN_ON_ONCE(tsk != current);
+
 	/* We only dequeue private signals from ourselves, we don't let
 	 * signalfd steal them
 	 */
@@ -1156,8 +1216,8 @@ int do_send_sig_info(int sig, struct siginfo *info, struct task_struct *p,
  * We don't want to have recursive SIGSEGV's etc, for example,
  * that is why we also clear SIGNAL_UNKILLABLE.
  */
-int
-force_sig_info(int sig, struct siginfo *info, struct task_struct *t)
+static int
+do_force_sig_info(int sig, struct siginfo *info, struct task_struct *t)
 {
 	unsigned long int flags;
 	int ret, blocked, ignored;
@@ -1182,6 +1242,39 @@ force_sig_info(int sig, struct siginfo *info, struct task_struct *t)
 	return ret;
 }
 
+int force_sig_info(int sig, struct siginfo *info, struct task_struct *t)
+{
+/*
+ * On some archs, PREEMPT_RT has to delay sending a signal from a trap
+ * since it can not enable preemption, and the signal code's spin_locks
+ * turn into mutexes. Instead, it must set TIF_NOTIFY_RESUME which will
+ * send the signal on exit of the trap.
+ */
+#ifdef ARCH_RT_DELAYS_SIGNAL_SEND
+	if (in_atomic()) {
+		if (WARN_ON_ONCE(t != current))
+			return 0;
+		if (WARN_ON_ONCE(t->forced_info.si_signo))
+			return 0;
+
+		if (is_si_special(info)) {
+			WARN_ON_ONCE(info != SEND_SIG_PRIV);
+			t->forced_info.si_signo = sig;
+			t->forced_info.si_errno = 0;
+			t->forced_info.si_code = SI_KERNEL;
+			t->forced_info.si_pid = 0;
+			t->forced_info.si_uid = 0;
+		} else {
+			t->forced_info = *info;
+		}
+
+		set_tsk_thread_flag(t, TIF_NOTIFY_RESUME);
+		return 0;
+	}
+#endif
+	return do_force_sig_info(sig, info, t);
+}
+
 /*
  * Nuke all other threads in the group.
  */
@@ -1216,12 +1309,12 @@ struct sighand_struct *__lock_task_sighand(struct task_struct *tsk,
 		 * Disable interrupts early to avoid deadlocks.
 		 * See rcu_read_unlock() comment header for details.
 		 */
-		local_irq_save(*flags);
+		local_irq_save_nort(*flags);
 		rcu_read_lock();
 		sighand = rcu_dereference(tsk->sighand);
 		if (unlikely(sighand == NULL)) {
 			rcu_read_unlock();
-			local_irq_restore(*flags);
+			local_irq_restore_nort(*flags);
 			break;
 		}
 		/*
@@ -1242,7 +1335,7 @@ struct sighand_struct *__lock_task_sighand(struct task_struct *tsk,
 		}
 		spin_unlock(&sighand->siglock);
 		rcu_read_unlock();
-		local_irq_restore(*flags);
+		local_irq_restore_nort(*flags);
 	}
 
 	return sighand;
@@ -1485,7 +1578,8 @@ EXPORT_SYMBOL(kill_pid);
  */
 struct sigqueue *sigqueue_alloc(void)
 {
-	struct sigqueue *q = __sigqueue_alloc(-1, current, GFP_KERNEL, 0);
+	/* Preallocated sigqueue objects always from the slabcache ! */
+	struct sigqueue *q = __sigqueue_do_alloc(-1, current, GFP_KERNEL, 0, 1);
 
 	if (q)
 		q->flags |= SIGQUEUE_PREALLOC;
@@ -1846,15 +1940,7 @@ static void ptrace_stop(int exit_code, int why, int clear_code, siginfo_t *info)
 		if (gstop_done && ptrace_reparented(current))
 			do_notify_parent_cldstop(current, false, why);
 
-		/*
-		 * Don't want to allow preemption here, because
-		 * sys_ptrace() needs this task to be inactive.
-		 *
-		 * XXX: implement read_unlock_no_resched().
-		 */
-		preempt_disable();
 		read_unlock(&tasklist_lock);
-		preempt_enable_no_resched();
 		freezable_schedule();
 	} else {
 		/*
diff --git a/kernel/softirq.c b/kernel/softirq.c
index 479e4436f787..d1e999e74d23 100644
--- a/kernel/softirq.c
+++ b/kernel/softirq.c
@@ -21,10 +21,12 @@
 #include <linux/freezer.h>
 #include <linux/kthread.h>
 #include <linux/rcupdate.h>
+#include <linux/delay.h>
 #include <linux/ftrace.h>
 #include <linux/smp.h>
 #include <linux/smpboot.h>
 #include <linux/tick.h>
+#include <linux/locallock.h>
 #include <linux/irq.h>
 
 #define CREATE_TRACE_POINTS
@@ -56,12 +58,108 @@ EXPORT_SYMBOL(irq_stat);
 static struct softirq_action softirq_vec[NR_SOFTIRQS] __cacheline_aligned_in_smp;
 
 DEFINE_PER_CPU(struct task_struct *, ksoftirqd);
+#ifdef CONFIG_PREEMPT_RT_FULL
+#define TIMER_SOFTIRQS	((1 << TIMER_SOFTIRQ) | (1 << HRTIMER_SOFTIRQ))
+DEFINE_PER_CPU(struct task_struct *, ktimer_softirqd);
+#endif
 
 const char * const softirq_to_name[NR_SOFTIRQS] = {
 	"HI", "TIMER", "NET_TX", "NET_RX", "BLOCK", "BLOCK_IOPOLL",
 	"TASKLET", "SCHED", "HRTIMER", "RCU"
 };
 
+#ifdef CONFIG_NO_HZ_COMMON
+# ifdef CONFIG_PREEMPT_RT_FULL
+
+struct softirq_runner {
+	struct task_struct *runner[NR_SOFTIRQS];
+};
+
+static DEFINE_PER_CPU(struct softirq_runner, softirq_runners);
+
+static inline void softirq_set_runner(unsigned int sirq)
+{
+	struct softirq_runner *sr = this_cpu_ptr(&softirq_runners);
+
+	sr->runner[sirq] = current;
+}
+
+static inline void softirq_clr_runner(unsigned int sirq)
+{
+	struct softirq_runner *sr = this_cpu_ptr(&softirq_runners);
+
+	sr->runner[sirq] = NULL;
+}
+
+/*
+ * On preempt-rt a softirq running context might be blocked on a
+ * lock. There might be no other runnable task on this CPU because the
+ * lock owner runs on some other CPU. So we have to go into idle with
+ * the pending bit set. Therefor we need to check this otherwise we
+ * warn about false positives which confuses users and defeats the
+ * whole purpose of this test.
+ *
+ * This code is called with interrupts disabled.
+ */
+void softirq_check_pending_idle(void)
+{
+	static int rate_limit;
+	struct softirq_runner *sr = this_cpu_ptr(&softirq_runners);
+	u32 warnpending;
+	int i;
+
+	if (rate_limit >= 10)
+		return;
+
+	warnpending = local_softirq_pending() & SOFTIRQ_STOP_IDLE_MASK;
+	for (i = 0; i < NR_SOFTIRQS; i++) {
+		struct task_struct *tsk = sr->runner[i];
+
+		/*
+		 * The wakeup code in rtmutex.c wakes up the task
+		 * _before_ it sets pi_blocked_on to NULL under
+		 * tsk->pi_lock. So we need to check for both: state
+		 * and pi_blocked_on.
+		 */
+		if (tsk) {
+			raw_spin_lock(&tsk->pi_lock);
+			if (tsk->pi_blocked_on || tsk->state == TASK_RUNNING) {
+				/* Clear all bits pending in that task */
+				warnpending &= ~(tsk->softirqs_raised);
+				warnpending &= ~(1 << i);
+			}
+			raw_spin_unlock(&tsk->pi_lock);
+		}
+	}
+
+	if (warnpending) {
+		printk(KERN_ERR "NOHZ: local_softirq_pending %02x\n",
+		       warnpending);
+		rate_limit++;
+	}
+}
+# else
+/*
+ * On !PREEMPT_RT we just printk rate limited:
+ */
+void softirq_check_pending_idle(void)
+{
+	static int rate_limit;
+
+	if (rate_limit < 10 &&
+			(local_softirq_pending() & SOFTIRQ_STOP_IDLE_MASK)) {
+		printk(KERN_ERR "NOHZ: local_softirq_pending %02x\n",
+		       local_softirq_pending());
+		rate_limit++;
+	}
+}
+# endif
+
+#else /* !CONFIG_NO_HZ_COMMON */
+static inline void softirq_set_runner(unsigned int sirq) { }
+static inline void softirq_clr_runner(unsigned int sirq) { }
+#endif
+
 /*
  * we cannot loop indefinitely here to avoid userspace starvation,
  * but we also don't want to introduce a worst case 1/HZ latency
@@ -77,6 +175,79 @@ static void wakeup_softirqd(void)
 		wake_up_process(tsk);
 }
 
+#ifdef CONFIG_PREEMPT_RT_FULL
+static void wakeup_timer_softirqd(void)
+{
+	/* Interrupts are disabled: no need to stop preemption */
+	struct task_struct *tsk = __this_cpu_read(ktimer_softirqd);
+
+	if (tsk && tsk->state != TASK_RUNNING)
+		wake_up_process(tsk);
+}
+#endif
+
+static void handle_softirq(unsigned int vec_nr)
+{
+	struct softirq_action *h = softirq_vec + vec_nr;
+	int prev_count;
+
+	prev_count = preempt_count();
+
+	kstat_incr_softirqs_this_cpu(vec_nr);
+
+	trace_softirq_entry(vec_nr);
+	h->action(h);
+	trace_softirq_exit(vec_nr);
+	if (unlikely(prev_count != preempt_count())) {
+		pr_err("huh, entered softirq %u %s %p with preempt_count %08x, exited with %08x?\n",
+		       vec_nr, softirq_to_name[vec_nr], h->action,
+		       prev_count, preempt_count());
+		preempt_count_set(prev_count);
+	}
+}
+
+#ifndef CONFIG_PREEMPT_RT_FULL
+static inline int ksoftirqd_softirq_pending(void)
+{
+	return local_softirq_pending();
+}
+
+static void handle_pending_softirqs(u32 pending)
+{
+	struct softirq_action *h = softirq_vec;
+	int softirq_bit;
+
+	local_irq_enable();
+
+	h = softirq_vec;
+
+	while ((softirq_bit = ffs(pending))) {
+		unsigned int vec_nr;
+
+		h += softirq_bit - 1;
+		vec_nr = h - softirq_vec;
+		handle_softirq(vec_nr);
+
+		h++;
+		pending >>= softirq_bit;
+	}
+
+	rcu_bh_qs();
+	local_irq_disable();
+}
+
+static void run_ksoftirqd(unsigned int cpu)
+{
+	local_irq_disable();
+	if (ksoftirqd_softirq_pending()) {
+		__do_softirq();
+		local_irq_enable();
+		cond_resched_rcu_qs();
+		return;
+	}
+	local_irq_enable();
+}
+
 /*
  * preempt_count and SOFTIRQ_OFFSET usage:
  * - preempt_count is changed by SOFTIRQ_OFFSET on entering or leaving
@@ -232,10 +403,8 @@ asmlinkage __visible void __do_softirq(void)
 	unsigned long end = jiffies + MAX_SOFTIRQ_TIME;
 	unsigned long old_flags = current->flags;
 	int max_restart = MAX_SOFTIRQ_RESTART;
-	struct softirq_action *h;
 	bool in_hardirq;
 	__u32 pending;
-	int softirq_bit;
 
 	/*
 	 * Mask out PF_MEMALLOC s current task context is borrowed for the
@@ -254,36 +423,7 @@ asmlinkage __visible void __do_softirq(void)
 	/* Reset the pending bitmask before enabling irqs */
 	set_softirq_pending(0);
 
-	local_irq_enable();
-
-	h = softirq_vec;
-
-	while ((softirq_bit = ffs(pending))) {
-		unsigned int vec_nr;
-		int prev_count;
-
-		h += softirq_bit - 1;
-
-		vec_nr = h - softirq_vec;
-		prev_count = preempt_count();
-
-		kstat_incr_softirqs_this_cpu(vec_nr);
-
-		trace_softirq_entry(vec_nr);
-		h->action(h);
-		trace_softirq_exit(vec_nr);
-		if (unlikely(prev_count != preempt_count())) {
-			pr_err("huh, entered softirq %u %s %p with preempt_count %08x, exited with %08x?\n",
-			       vec_nr, softirq_to_name[vec_nr], h->action,
-			       prev_count, preempt_count());
-			preempt_count_set(prev_count);
-		}
-		h++;
-		pending >>= softirq_bit;
-	}
-
-	rcu_bh_qs();
-	local_irq_disable();
+	handle_pending_softirqs(pending);
 
 	pending = local_softirq_pending();
 	if (pending) {
@@ -320,6 +460,308 @@ asmlinkage __visible void do_softirq(void)
 }
 
 /*
+ * This function must run with irqs disabled!
+ */
+void raise_softirq_irqoff(unsigned int nr)
+{
+	__raise_softirq_irqoff(nr);
+
+	/*
+	 * If we're in an interrupt or softirq, we're done
+	 * (this also catches softirq-disabled code). We will
+	 * actually run the softirq once we return from
+	 * the irq or softirq.
+	 *
+	 * Otherwise we wake up ksoftirqd to make sure we
+	 * schedule the softirq soon.
+	 */
+	if (!in_interrupt())
+		wakeup_softirqd();
+}
+
+void __raise_softirq_irqoff(unsigned int nr)
+{
+	trace_softirq_raise(nr);
+	or_softirq_pending(1UL << nr);
+}
+
+static inline void local_bh_disable_nort(void) { local_bh_disable(); }
+static inline void _local_bh_enable_nort(void) { _local_bh_enable(); }
+static void ksoftirqd_set_sched_params(unsigned int cpu) { }
+
+#else /* !PREEMPT_RT_FULL */
+
+/*
+ * On RT we serialize softirq execution with a cpu local lock per softirq
+ */
+static DEFINE_PER_CPU(struct local_irq_lock [NR_SOFTIRQS], local_softirq_locks);
+
+void __init softirq_early_init(void)
+{
+	int i;
+
+	for (i = 0; i < NR_SOFTIRQS; i++)
+		local_irq_lock_init(local_softirq_locks[i]);
+}
+
+static void lock_softirq(int which)
+{
+	local_lock(local_softirq_locks[which]);
+}
+
+static void unlock_softirq(int which)
+{
+	local_unlock(local_softirq_locks[which]);
+}
+
+static void do_single_softirq(int which)
+{
+	unsigned long old_flags = current->flags;
+
+	current->flags &= ~PF_MEMALLOC;
+	vtime_account_irq_enter(current);
+	current->flags |= PF_IN_SOFTIRQ;
+	lockdep_softirq_enter();
+	local_irq_enable();
+	handle_softirq(which);
+	local_irq_disable();
+	lockdep_softirq_exit();
+	current->flags &= ~PF_IN_SOFTIRQ;
+	vtime_account_irq_enter(current);
+	tsk_restore_flags(current, old_flags, PF_MEMALLOC);
+}
+
+/*
+ * Called with interrupts disabled. Process softirqs which were raised
+ * in current context (or on behalf of ksoftirqd).
+ */
+static void do_current_softirqs(void)
+{
+	while (current->softirqs_raised) {
+		int i = __ffs(current->softirqs_raised);
+		unsigned int pending, mask = (1U << i);
+
+		current->softirqs_raised &= ~mask;
+		local_irq_enable();
+
+		/*
+		 * If the lock is contended, we boost the owner to
+		 * process the softirq or leave the critical section
+		 * now.
+		 */
+		lock_softirq(i);
+		local_irq_disable();
+		softirq_set_runner(i);
+		/*
+		 * Check with the local_softirq_pending() bits,
+		 * whether we need to process this still or if someone
+		 * else took care of it.
+		 */
+		pending = local_softirq_pending();
+		if (pending & mask) {
+			set_softirq_pending(pending & ~mask);
+			do_single_softirq(i);
+		}
+		softirq_clr_runner(i);
+		unlock_softirq(i);
+		WARN_ON(current->softirq_nestcnt != 1);
+	}
+}
+
+void __local_bh_disable(void)
+{
+	if (++current->softirq_nestcnt == 1)
+		migrate_disable();
+}
+EXPORT_SYMBOL(__local_bh_disable);
+
+void __local_bh_enable(void)
+{
+	if (WARN_ON(current->softirq_nestcnt == 0))
+		return;
+
+	local_irq_disable();
+	if (current->softirq_nestcnt == 1 && current->softirqs_raised)
+		do_current_softirqs();
+	local_irq_enable();
+
+	if (--current->softirq_nestcnt == 0)
+		migrate_enable();
+}
+EXPORT_SYMBOL(__local_bh_enable);
+
+void _local_bh_enable(void)
+{
+	if (WARN_ON(current->softirq_nestcnt == 0))
+		return;
+	if (--current->softirq_nestcnt == 0)
+		migrate_enable();
+}
+EXPORT_SYMBOL(_local_bh_enable);
+
+int in_serving_softirq(void)
+{
+	return current->flags & PF_IN_SOFTIRQ;
+}
+EXPORT_SYMBOL(in_serving_softirq);
+
+/* Called with preemption disabled */
+static void run_ksoftirqd(unsigned int cpu)
+{
+	local_irq_disable();
+	current->softirq_nestcnt++;
+
+	do_current_softirqs();
+	current->softirq_nestcnt--;
+	local_irq_enable();
+	cond_resched_rcu_qs();
+}
+
+/*
+ * Called from netif_rx_ni(). Preemption enabled, but migration
+ * disabled. So the cpu can't go away under us.
+ */
+void thread_do_softirq(void)
+{
+	if (!in_serving_softirq() && current->softirqs_raised) {
+		current->softirq_nestcnt++;
+		do_current_softirqs();
+		current->softirq_nestcnt--;
+	}
+}
+
+static void do_raise_softirq_irqoff(unsigned int nr)
+{
+	unsigned int mask;
+
+	mask = 1UL << nr;
+
+	trace_softirq_raise(nr);
+	or_softirq_pending(mask);
+
+	/*
+	 * If we are not in a hard interrupt and inside a bh disabled
+	 * region, we simply raise the flag on current. local_bh_enable()
+	 * will make sure that the softirq is executed. Otherwise we
+	 * delegate it to ksoftirqd.
+	 */
+	if (!in_irq() && current->softirq_nestcnt)
+		current->softirqs_raised |= mask;
+	else if (!__this_cpu_read(ksoftirqd) || !__this_cpu_read(ktimer_softirqd))
+		return;
+
+	if (mask & TIMER_SOFTIRQS)
+		__this_cpu_read(ktimer_softirqd)->softirqs_raised |= mask;
+	else
+		__this_cpu_read(ksoftirqd)->softirqs_raised |= mask;
+}
+
+static void wakeup_proper_softirq(unsigned int nr)
+{
+	if ((1UL << nr) & TIMER_SOFTIRQS)
+		wakeup_timer_softirqd();
+	else
+		wakeup_softirqd();
+}
+
+
+void __raise_softirq_irqoff(unsigned int nr)
+{
+	do_raise_softirq_irqoff(nr);
+	if (!in_irq() && !current->softirq_nestcnt)
+		wakeup_proper_softirq(nr);
+}
+
+/*
+ * Same as __raise_softirq_irqoff() but will process them in ksoftirqd
+ */
+void __raise_softirq_irqoff_ksoft(unsigned int nr)
+{
+	unsigned int mask;
+
+	if (WARN_ON_ONCE(!__this_cpu_read(ksoftirqd) ||
+			 !__this_cpu_read(ktimer_softirqd)))
+		return;
+	mask = 1UL << nr;
+
+	trace_softirq_raise(nr);
+	or_softirq_pending(mask);
+	if (mask & TIMER_SOFTIRQS)
+		__this_cpu_read(ktimer_softirqd)->softirqs_raised |= mask;
+	else
+		__this_cpu_read(ksoftirqd)->softirqs_raised |= mask;
+	wakeup_proper_softirq(nr);
+}
+
+/*
+ * This function must run with irqs disabled!
+ */
+void raise_softirq_irqoff(unsigned int nr)
+{
+	do_raise_softirq_irqoff(nr);
+
+	/*
+	 * If we're in an hard interrupt we let irq return code deal
+	 * with the wakeup of ksoftirqd.
+	 */
+	if (in_irq())
+		return;
+	/*
+	 * If we are in thread context but outside of a bh disabled
+	 * region, we need to wake ksoftirqd as well.
+	 *
+	 * CHECKME: Some of the places which do that could be wrapped
+	 * into local_bh_disable/enable pairs. Though it's unclear
+	 * whether this is worth the effort. To find those places just
+	 * raise a WARN() if the condition is met.
+	 */
+	if (!current->softirq_nestcnt)
+		wakeup_proper_softirq(nr);
+}
+
+static inline int ksoftirqd_softirq_pending(void)
+{
+	return current->softirqs_raised;
+}
+
+static inline void local_bh_disable_nort(void) { }
+static inline void _local_bh_enable_nort(void) { }
+
+static inline void ksoftirqd_set_sched_params(unsigned int cpu)
+{
+	/* Take over all but timer pending softirqs when starting */
+	local_irq_disable();
+	current->softirqs_raised = local_softirq_pending() & ~TIMER_SOFTIRQS;
+	local_irq_enable();
+}
+
+static inline void ktimer_softirqd_set_sched_params(unsigned int cpu)
+{
+	struct sched_param param = { .sched_priority = 1 };
+
+	sched_setscheduler(current, SCHED_FIFO, &param);
+
+	/* Take over timer pending softirqs when starting */
+	local_irq_disable();
+	current->softirqs_raised = local_softirq_pending() & TIMER_SOFTIRQS;
+	local_irq_enable();
+}
+
+static inline void ktimer_softirqd_clr_sched_params(unsigned int cpu,
+						    bool online)
+{
+	struct sched_param param = { .sched_priority = 0 };
+
+	sched_setscheduler(current, SCHED_NORMAL, &param);
+}
+
+static int ktimer_softirqd_should_run(unsigned int cpu)
+{
+	return current->softirqs_raised;
+}
+
+#endif /* PREEMPT_RT_FULL */
+/*
  * Enter an interrupt context.
  */
 void irq_enter(void)
@@ -330,9 +772,9 @@ void irq_enter(void)
 		 * Prevent raise_softirq from needlessly waking up ksoftirqd
 		 * here, as softirq will be serviced on return from interrupt.
 		 */
-		local_bh_disable();
+		local_bh_disable_nort();
 		tick_irq_enter();
-		_local_bh_enable();
+		_local_bh_enable_nort();
 	}
 
 	__irq_enter();
@@ -340,6 +782,7 @@ void irq_enter(void)
 
 static inline void invoke_softirq(void)
 {
+#ifndef CONFIG_PREEMPT_RT_FULL
 	if (!force_irqthreads) {
 #ifdef CONFIG_HAVE_IRQ_EXIT_ON_IRQ_STACK
 		/*
@@ -359,6 +802,18 @@ static inline void invoke_softirq(void)
 	} else {
 		wakeup_softirqd();
 	}
+#else /* PREEMPT_RT_FULL */
+	unsigned long flags;
+
+	local_irq_save(flags);
+	if (__this_cpu_read(ksoftirqd) &&
+			__this_cpu_read(ksoftirqd)->softirqs_raised)
+		wakeup_softirqd();
+	if (__this_cpu_read(ktimer_softirqd) &&
+			__this_cpu_read(ktimer_softirqd)->softirqs_raised)
+		wakeup_timer_softirqd();
+	local_irq_restore(flags);
+#endif
 }
 
 static inline void tick_irq_exit(void)
@@ -395,26 +850,6 @@ void irq_exit(void)
 	trace_hardirq_exit(); /* must be last! */
 }
 
-/*
- * This function must run with irqs disabled!
- */
-inline void raise_softirq_irqoff(unsigned int nr)
-{
-	__raise_softirq_irqoff(nr);
-
-	/*
-	 * If we're in an interrupt or softirq, we're done
-	 * (this also catches softirq-disabled code). We will
-	 * actually run the softirq once we return from
-	 * the irq or softirq.
-	 *
-	 * Otherwise we wake up ksoftirqd to make sure we
-	 * schedule the softirq soon.
-	 */
-	if (!in_interrupt())
-		wakeup_softirqd();
-}
-
 void raise_softirq(unsigned int nr)
 {
 	unsigned long flags;
@@ -424,12 +859,6 @@ void raise_softirq(unsigned int nr)
 	local_irq_restore(flags);
 }
 
-void __raise_softirq_irqoff(unsigned int nr)
-{
-	trace_softirq_raise(nr);
-	or_softirq_pending(1UL << nr);
-}
-
 void open_softirq(int nr, void (*action)(struct softirq_action *))
 {
 	softirq_vec[nr].action = action;
@@ -446,15 +875,45 @@ struct tasklet_head {
 static DEFINE_PER_CPU(struct tasklet_head, tasklet_vec);
 static DEFINE_PER_CPU(struct tasklet_head, tasklet_hi_vec);
 
+static void inline
+__tasklet_common_schedule(struct tasklet_struct *t, struct tasklet_head *head, unsigned int nr)
+{
+	if (tasklet_trylock(t)) {
+again:
+		/* We may have been preempted before tasklet_trylock
+		 * and __tasklet_action may have already run.
+		 * So double check the sched bit while the takslet
+		 * is locked before adding it to the list.
+		 */
+		if (test_bit(TASKLET_STATE_SCHED, &t->state)) {
+			t->next = NULL;
+			*head->tail = t;
+			head->tail = &(t->next);
+			raise_softirq_irqoff(nr);
+			tasklet_unlock(t);
+		} else {
+			/* This is subtle. If we hit the corner case above
+			 * It is possible that we get preempted right here,
+			 * and another task has successfully called
+			 * tasklet_schedule(), then this function, and
+			 * failed on the trylock. Thus we must be sure
+			 * before releasing the tasklet lock, that the
+			 * SCHED_BIT is clear. Otherwise the tasklet
+			 * may get its SCHED_BIT set, but not added to the
+			 * list
+			 */
+			if (!tasklet_tryunlock(t))
+				goto again;
+		}
+	}
+}
+
 void __tasklet_schedule(struct tasklet_struct *t)
 {
 	unsigned long flags;
 
 	local_irq_save(flags);
-	t->next = NULL;
-	*__this_cpu_read(tasklet_vec.tail) = t;
-	__this_cpu_write(tasklet_vec.tail, &(t->next));
-	raise_softirq_irqoff(TASKLET_SOFTIRQ);
+	__tasklet_common_schedule(t, this_cpu_ptr(&tasklet_vec), TASKLET_SOFTIRQ);
 	local_irq_restore(flags);
 }
 EXPORT_SYMBOL(__tasklet_schedule);
@@ -464,10 +923,7 @@ void __tasklet_hi_schedule(struct tasklet_struct *t)
 	unsigned long flags;
 
 	local_irq_save(flags);
-	t->next = NULL;
-	*__this_cpu_read(tasklet_hi_vec.tail) = t;
-	__this_cpu_write(tasklet_hi_vec.tail,  &(t->next));
-	raise_softirq_irqoff(HI_SOFTIRQ);
+	__tasklet_common_schedule(t, this_cpu_ptr(&tasklet_hi_vec), HI_SOFTIRQ);
 	local_irq_restore(flags);
 }
 EXPORT_SYMBOL(__tasklet_hi_schedule);
@@ -476,82 +932,122 @@ void __tasklet_hi_schedule_first(struct tasklet_struct *t)
 {
 	BUG_ON(!irqs_disabled());
 
-	t->next = __this_cpu_read(tasklet_hi_vec.head);
-	__this_cpu_write(tasklet_hi_vec.head, t);
-	__raise_softirq_irqoff(HI_SOFTIRQ);
+	__tasklet_hi_schedule(t);
 }
 EXPORT_SYMBOL(__tasklet_hi_schedule_first);
 
-static void tasklet_action(struct softirq_action *a)
+void  tasklet_enable(struct tasklet_struct *t)
 {
-	struct tasklet_struct *list;
+	if (!atomic_dec_and_test(&t->count))
+		return;
+	if (test_and_clear_bit(TASKLET_STATE_PENDING, &t->state))
+		tasklet_schedule(t);
+}
+EXPORT_SYMBOL(tasklet_enable);
 
-	local_irq_disable();
-	list = __this_cpu_read(tasklet_vec.head);
-	__this_cpu_write(tasklet_vec.head, NULL);
-	__this_cpu_write(tasklet_vec.tail, this_cpu_ptr(&tasklet_vec.head));
-	local_irq_enable();
+static void __tasklet_action(struct softirq_action *a,
+			     struct tasklet_struct *list)
+{
+	int loops = 1000000;
 
 	while (list) {
 		struct tasklet_struct *t = list;
 
 		list = list->next;
 
-		if (tasklet_trylock(t)) {
-			if (!atomic_read(&t->count)) {
-				if (!test_and_clear_bit(TASKLET_STATE_SCHED,
-							&t->state))
-					BUG();
-				t->func(t->data);
-				tasklet_unlock(t);
-				continue;
-			}
-			tasklet_unlock(t);
+		/*
+		 * Should always succeed - after a tasklist got on the
+		 * list (after getting the SCHED bit set from 0 to 1),
+		 * nothing but the tasklet softirq it got queued to can
+		 * lock it:
+		 */
+		if (!tasklet_trylock(t)) {
+			WARN_ON(1);
+			continue;
 		}
 
-		local_irq_disable();
 		t->next = NULL;
-		*__this_cpu_read(tasklet_vec.tail) = t;
-		__this_cpu_write(tasklet_vec.tail, &(t->next));
-		__raise_softirq_irqoff(TASKLET_SOFTIRQ);
-		local_irq_enable();
+
+		/*
+		 * If we cannot handle the tasklet because it's disabled,
+		 * mark it as pending. tasklet_enable() will later
+		 * re-schedule the tasklet.
+		 */
+		if (unlikely(atomic_read(&t->count))) {
+out_disabled:
+			/* implicit unlock: */
+			wmb();
+			t->state = TASKLET_STATEF_PENDING;
+			continue;
+		}
+
+		/*
+		 * After this point on the tasklet might be rescheduled
+		 * on another CPU, but it can only be added to another
+		 * CPU's tasklet list if we unlock the tasklet (which we
+		 * dont do yet).
+		 */
+		if (!test_and_clear_bit(TASKLET_STATE_SCHED, &t->state))
+			WARN_ON(1);
+
+again:
+		t->func(t->data);
+
+		/*
+		 * Try to unlock the tasklet. We must use cmpxchg, because
+		 * another CPU might have scheduled or disabled the tasklet.
+		 * We only allow the STATE_RUN -> 0 transition here.
+		 */
+		while (!tasklet_tryunlock(t)) {
+			/*
+			 * If it got disabled meanwhile, bail out:
+			 */
+			if (atomic_read(&t->count))
+				goto out_disabled;
+			/*
+			 * If it got scheduled meanwhile, re-execute
+			 * the tasklet function:
+			 */
+			if (test_and_clear_bit(TASKLET_STATE_SCHED, &t->state))
+				goto again;
+			if (!--loops) {
+				printk("hm, tasklet state: %08lx\n", t->state);
+				WARN_ON(1);
+				tasklet_unlock(t);
+				break;
+			}
+		}
 	}
 }
 
+static void tasklet_action(struct softirq_action *a)
+{
+	struct tasklet_struct *list;
+
+	local_irq_disable();
+
+	list = __this_cpu_read(tasklet_vec.head);
+	__this_cpu_write(tasklet_vec.head, NULL);
+	__this_cpu_write(tasklet_vec.tail, this_cpu_ptr(&tasklet_vec.head));
+
+	local_irq_enable();
+
+	__tasklet_action(a, list);
+}
+
 static void tasklet_hi_action(struct softirq_action *a)
 {
 	struct tasklet_struct *list;
 
 	local_irq_disable();
+
 	list = __this_cpu_read(tasklet_hi_vec.head);
 	__this_cpu_write(tasklet_hi_vec.head, NULL);
 	__this_cpu_write(tasklet_hi_vec.tail, this_cpu_ptr(&tasklet_hi_vec.head));
+
 	local_irq_enable();
 
-	while (list) {
-		struct tasklet_struct *t = list;
-
-		list = list->next;
-
-		if (tasklet_trylock(t)) {
-			if (!atomic_read(&t->count)) {
-				if (!test_and_clear_bit(TASKLET_STATE_SCHED,
-							&t->state))
-					BUG();
-				t->func(t->data);
-				tasklet_unlock(t);
-				continue;
-			}
-			tasklet_unlock(t);
-		}
-
-		local_irq_disable();
-		t->next = NULL;
-		*__this_cpu_read(tasklet_hi_vec.tail) = t;
-		__this_cpu_write(tasklet_hi_vec.tail, &(t->next));
-		__raise_softirq_irqoff(HI_SOFTIRQ);
-		local_irq_enable();
-	}
+	__tasklet_action(a, list);
 }
 
 void tasklet_init(struct tasklet_struct *t,
@@ -572,7 +1068,7 @@ void tasklet_kill(struct tasklet_struct *t)
 
 	while (test_and_set_bit(TASKLET_STATE_SCHED, &t->state)) {
 		do {
-			yield();
+			msleep(1);
 		} while (test_bit(TASKLET_STATE_SCHED, &t->state));
 	}
 	tasklet_unlock_wait(t);
@@ -646,25 +1142,26 @@ void __init softirq_init(void)
 	open_softirq(HI_SOFTIRQ, tasklet_hi_action);
 }
 
+#if defined(CONFIG_SMP) || defined(CONFIG_PREEMPT_RT_FULL)
+void tasklet_unlock_wait(struct tasklet_struct *t)
+{
+	while (test_bit(TASKLET_STATE_RUN, &(t)->state)) {
+		/*
+		 * Hack for now to avoid this busy-loop:
+		 */
+#ifdef CONFIG_PREEMPT_RT_FULL
+		msleep(1);
+#else
+		barrier();
+#endif
+	}
+}
+EXPORT_SYMBOL(tasklet_unlock_wait);
+#endif
+
 static int ksoftirqd_should_run(unsigned int cpu)
 {
-	return local_softirq_pending();
-}
-
-static void run_ksoftirqd(unsigned int cpu)
-{
-	local_irq_disable();
-	if (local_softirq_pending()) {
-		/*
-		 * We can safely run softirq on inline stack, as we are not deep
-		 * in the task stack here.
-		 */
-		__do_softirq();
-		local_irq_enable();
-		cond_resched_rcu_qs();
-		return;
-	}
-	local_irq_enable();
+	return ksoftirqd_softirq_pending();
 }
 
 #ifdef CONFIG_HOTPLUG_CPU
@@ -746,16 +1243,31 @@ static struct notifier_block cpu_nfb = {
 
 static struct smp_hotplug_thread softirq_threads = {
 	.store			= &ksoftirqd,
+	.setup			= ksoftirqd_set_sched_params,
 	.thread_should_run	= ksoftirqd_should_run,
 	.thread_fn		= run_ksoftirqd,
 	.thread_comm		= "ksoftirqd/%u",
 };
 
+#ifdef CONFIG_PREEMPT_RT_FULL
+static struct smp_hotplug_thread softirq_timer_threads = {
+	.store			= &ktimer_softirqd,
+	.setup			= ktimer_softirqd_set_sched_params,
+	.cleanup		= ktimer_softirqd_clr_sched_params,
+	.thread_should_run	= ktimer_softirqd_should_run,
+	.thread_fn		= run_ksoftirqd,
+	.thread_comm		= "ktimersoftd/%u",
+};
+#endif
+
 static __init int spawn_ksoftirqd(void)
 {
 	register_cpu_notifier(&cpu_nfb);
 
 	BUG_ON(smpboot_register_percpu_thread(&softirq_threads));
+#ifdef CONFIG_PREEMPT_RT_FULL
+	BUG_ON(smpboot_register_percpu_thread(&softirq_timer_threads));
+#endif
 
 	return 0;
 }
diff --git a/kernel/stop_machine.c b/kernel/stop_machine.c
index a3bbaee77c58..2c5acc882bad 100644
--- a/kernel/stop_machine.c
+++ b/kernel/stop_machine.c
@@ -30,14 +30,14 @@ struct cpu_stop_done {
 	atomic_t		nr_todo;	/* nr left to execute */
 	bool			executed;	/* actually executed? */
 	int			ret;		/* collected return value */
-	struct completion	completion;	/* fired if nr_todo reaches 0 */
+	struct task_struct	*waiter;	/* woken when nr_todo reaches 0 */
 };
 
 /* the actual stopper, one per every possible cpu, enabled on online cpus */
 struct cpu_stopper {
 	struct task_struct	*thread;
 
-	spinlock_t		lock;
+	raw_spinlock_t		lock;
 	bool			enabled;	/* is this stopper enabled? */
 	struct list_head	works;		/* list of pending works */
 
@@ -59,7 +59,7 @@ static void cpu_stop_init_done(struct cpu_stop_done *done, unsigned int nr_todo)
 {
 	memset(done, 0, sizeof(*done));
 	atomic_set(&done->nr_todo, nr_todo);
-	init_completion(&done->completion);
+	done->waiter = current;
 }
 
 /* signal completion unless @done is NULL */
@@ -68,8 +68,10 @@ static void cpu_stop_signal_done(struct cpu_stop_done *done, bool executed)
 	if (done) {
 		if (executed)
 			done->executed = true;
-		if (atomic_dec_and_test(&done->nr_todo))
-			complete(&done->completion);
+		if (atomic_dec_and_test(&done->nr_todo)) {
+			wake_up_process(done->waiter);
+			done->waiter = NULL;
+		}
 	}
 }
 
@@ -86,12 +88,28 @@ static void cpu_stop_queue_work(unsigned int cpu, struct cpu_stop_work *work)
 	struct cpu_stopper *stopper = &per_cpu(cpu_stopper, cpu);
 	unsigned long flags;
 
-	spin_lock_irqsave(&stopper->lock, flags);
+	raw_spin_lock_irqsave(&stopper->lock, flags);
 	if (stopper->enabled)
 		__cpu_stop_queue_work(stopper, work);
 	else
 		cpu_stop_signal_done(work->done, false);
-	spin_unlock_irqrestore(&stopper->lock, flags);
+	raw_spin_unlock_irqrestore(&stopper->lock, flags);
+}
+
+static void wait_for_stop_done(struct cpu_stop_done *done)
+{
+	set_current_state(TASK_UNINTERRUPTIBLE);
+	while (atomic_read(&done->nr_todo)) {
+		schedule();
+		set_current_state(TASK_UNINTERRUPTIBLE);
+	}
+	/*
+	 * We need to wait until cpu_stop_signal_done() has cleared
+	 * done->waiter.
+	 */
+	while (done->waiter)
+		cpu_relax();
+	set_current_state(TASK_RUNNING);
 }
 
 /**
@@ -125,7 +143,7 @@ int stop_one_cpu(unsigned int cpu, cpu_stop_fn_t fn, void *arg)
 
 	cpu_stop_init_done(&done, 1);
 	cpu_stop_queue_work(cpu, &work);
-	wait_for_completion(&done.completion);
+	wait_for_stop_done(&done);
 	return done.executed ? done.ret : -ENOENT;
 }
 
@@ -224,8 +242,8 @@ static int cpu_stop_queue_two_works(int cpu1, struct cpu_stop_work *work1,
 	int err;
 
 	lg_double_lock(&stop_cpus_lock, cpu1, cpu2);
-	spin_lock_irq(&stopper1->lock);
-	spin_lock_nested(&stopper2->lock, SINGLE_DEPTH_NESTING);
+	raw_spin_lock_irq(&stopper1->lock);
+	raw_spin_lock_nested(&stopper2->lock, SINGLE_DEPTH_NESTING);
 
 	err = -ENOENT;
 	if (!stopper1->enabled || !stopper2->enabled)
@@ -235,8 +253,8 @@ static int cpu_stop_queue_two_works(int cpu1, struct cpu_stop_work *work1,
 	__cpu_stop_queue_work(stopper1, work1);
 	__cpu_stop_queue_work(stopper2, work2);
 unlock:
-	spin_unlock(&stopper2->lock);
-	spin_unlock_irq(&stopper1->lock);
+	raw_spin_unlock(&stopper2->lock);
+	raw_spin_unlock_irq(&stopper1->lock);
 	lg_double_unlock(&stop_cpus_lock, cpu1, cpu2);
 
 	return err;
@@ -258,7 +276,7 @@ int stop_two_cpus(unsigned int cpu1, unsigned int cpu2, cpu_stop_fn_t fn, void *
 	struct cpu_stop_work work1, work2;
 	struct multi_stop_data msdata;
 
-	preempt_disable();
+	preempt_disable_nort();
 	msdata = (struct multi_stop_data){
 		.fn = fn,
 		.data = arg,
@@ -278,13 +296,13 @@ int stop_two_cpus(unsigned int cpu1, unsigned int cpu2, cpu_stop_fn_t fn, void *
 	if (cpu1 > cpu2)
 		swap(cpu1, cpu2);
 	if (cpu_stop_queue_two_works(cpu1, &work1, cpu2, &work2)) {
-		preempt_enable();
+		preempt_enable_nort();
 		return -ENOENT;
 	}
 
-	preempt_enable();
+	preempt_enable_nort();
 
-	wait_for_completion(&done.completion);
+	wait_for_stop_done(&done);
 
 	return done.executed ? done.ret : -ENOENT;
 }
@@ -315,17 +333,20 @@ static DEFINE_MUTEX(stop_cpus_mutex);
 
 static void queue_stop_cpus_work(const struct cpumask *cpumask,
 				 cpu_stop_fn_t fn, void *arg,
-				 struct cpu_stop_done *done)
+				 struct cpu_stop_done *done, bool inactive)
 {
 	struct cpu_stop_work *work;
 	unsigned int cpu;
 
 	/*
-	 * Disable preemption while queueing to avoid getting
-	 * preempted by a stopper which might wait for other stoppers
-	 * to enter @fn which can lead to deadlock.
+	 * Make sure that all work is queued on all cpus before
+	 * any of the cpus can execute it.
 	 */
-	lg_global_lock(&stop_cpus_lock);
+	if (!inactive)
+		lg_global_lock(&stop_cpus_lock);
+	else
+		lg_global_trylock_relax(&stop_cpus_lock);
+
 	for_each_cpu(cpu, cpumask) {
 		work = &per_cpu(cpu_stopper.stop_work, cpu);
 		work->fn = fn;
@@ -342,8 +363,8 @@ static int __stop_cpus(const struct cpumask *cpumask,
 	struct cpu_stop_done done;
 
 	cpu_stop_init_done(&done, cpumask_weight(cpumask));
-	queue_stop_cpus_work(cpumask, fn, arg, &done);
-	wait_for_completion(&done.completion);
+	queue_stop_cpus_work(cpumask, fn, arg, &done, false);
+	wait_for_stop_done(&done);
 	return done.executed ? done.ret : -ENOENT;
 }
 
@@ -422,9 +443,9 @@ static int cpu_stop_should_run(unsigned int cpu)
 	unsigned long flags;
 	int run;
 
-	spin_lock_irqsave(&stopper->lock, flags);
+	raw_spin_lock_irqsave(&stopper->lock, flags);
 	run = !list_empty(&stopper->works);
-	spin_unlock_irqrestore(&stopper->lock, flags);
+	raw_spin_unlock_irqrestore(&stopper->lock, flags);
 	return run;
 }
 
@@ -436,13 +457,13 @@ static void cpu_stopper_thread(unsigned int cpu)
 
 repeat:
 	work = NULL;
-	spin_lock_irq(&stopper->lock);
+	raw_spin_lock_irq(&stopper->lock);
 	if (!list_empty(&stopper->works)) {
 		work = list_first_entry(&stopper->works,
 					struct cpu_stop_work, list);
 		list_del_init(&work->list);
 	}
-	spin_unlock_irq(&stopper->lock);
+	raw_spin_unlock_irq(&stopper->lock);
 
 	if (work) {
 		cpu_stop_fn_t fn = work->fn;
@@ -450,6 +471,16 @@ static void cpu_stopper_thread(unsigned int cpu)
 		struct cpu_stop_done *done = work->done;
 		char ksym_buf[KSYM_NAME_LEN] __maybe_unused;
 
+		/*
+		 * Wait until the stopper finished scheduling on all
+		 * cpus
+		 */
+		lg_global_lock(&stop_cpus_lock);
+		/*
+		 * Let other cpu threads continue as well
+		 */
+		lg_global_unlock(&stop_cpus_lock);
+
 		/* cpu stop callbacks are not allowed to sleep */
 		preempt_disable();
 
@@ -464,7 +495,13 @@ static void cpu_stopper_thread(unsigned int cpu)
 			  kallsyms_lookup((unsigned long)fn, NULL, NULL, NULL,
 					  ksym_buf), arg);
 
+		/*
+		 * Make sure that the wakeup and setting done->waiter
+		 * to NULL is atomic.
+		 */
+		local_irq_disable();
 		cpu_stop_signal_done(done, true);
+		local_irq_enable();
 		goto repeat;
 	}
 }
@@ -520,10 +557,12 @@ static int __init cpu_stop_init(void)
 	for_each_possible_cpu(cpu) {
 		struct cpu_stopper *stopper = &per_cpu(cpu_stopper, cpu);
 
-		spin_lock_init(&stopper->lock);
+		raw_spin_lock_init(&stopper->lock);
 		INIT_LIST_HEAD(&stopper->works);
 	}
 
+	lg_lock_init(&stop_cpus_lock, "stop_cpus_lock");
+
 	BUG_ON(smpboot_register_percpu_thread(&cpu_stop_threads));
 	stop_machine_unpark(raw_smp_processor_id());
 	stop_machine_initialized = true;
@@ -620,11 +659,11 @@ int stop_machine_from_inactive_cpu(cpu_stop_fn_t fn, void *data,
 	set_state(&msdata, MULTI_STOP_PREPARE);
 	cpu_stop_init_done(&done, num_active_cpus());
 	queue_stop_cpus_work(cpu_active_mask, multi_cpu_stop, &msdata,
-			     &done);
+			     &done, true);
 	ret = multi_cpu_stop(&msdata);
 
 	/* Busy wait for completion. */
-	while (!completion_done(&done.completion))
+	while (atomic_read(&done.nr_todo))
 		cpu_relax();
 
 	mutex_unlock(&stop_cpus_mutex);
diff --git a/kernel/time/hrtimer.c b/kernel/time/hrtimer.c
index 435b8850dd80..27c198e74967 100644
--- a/kernel/time/hrtimer.c
+++ b/kernel/time/hrtimer.c
@@ -48,11 +48,13 @@
 #include <linux/sched/rt.h>
 #include <linux/sched/deadline.h>
 #include <linux/timer.h>
+#include <linux/kthread.h>
 #include <linux/freezer.h>
 
 #include <asm/uaccess.h>
 
 #include <trace/events/timer.h>
+#include <trace/events/hist.h>
 
 #include "tick-internal.h"
 
@@ -712,6 +714,44 @@ static void clock_was_set_work(struct work_struct *work)
 
 static DECLARE_WORK(hrtimer_work, clock_was_set_work);
 
+#ifdef CONFIG_PREEMPT_RT_FULL
+/*
+ * RT can not call schedule_work from real interrupt context.
+ * Need to make a thread to do the real work.
+ */
+static struct task_struct *clock_set_delay_thread;
+static bool do_clock_set_delay;
+
+static int run_clock_set_delay(void *ignore)
+{
+	while (!kthread_should_stop()) {
+		set_current_state(TASK_INTERRUPTIBLE);
+		if (do_clock_set_delay) {
+			do_clock_set_delay = false;
+			schedule_work(&hrtimer_work);
+		}
+		schedule();
+	}
+	__set_current_state(TASK_RUNNING);
+	return 0;
+}
+
+void clock_was_set_delayed(void)
+{
+	do_clock_set_delay = true;
+	/* Make visible before waking up process */
+	smp_wmb();
+	wake_up_process(clock_set_delay_thread);
+}
+
+static __init int create_clock_set_delay_thread(void)
+{
+	clock_set_delay_thread = kthread_run(run_clock_set_delay, NULL, "kclksetdelayd");
+	BUG_ON(!clock_set_delay_thread);
+	return 0;
+}
+early_initcall(create_clock_set_delay_thread);
+#else /* PREEMPT_RT_FULL */
 /*
  * Called from timekeeping and resume code to reprogramm the hrtimer
  * interrupt device on all cpus.
@@ -720,6 +760,7 @@ void clock_was_set_delayed(void)
 {
 	schedule_work(&hrtimer_work);
 }
+#endif
 
 #else
 
@@ -729,11 +770,8 @@ static inline int hrtimer_is_hres_enabled(void) { return 0; }
 static inline void hrtimer_switch_to_hres(void) { }
 static inline void
 hrtimer_force_reprogram(struct hrtimer_cpu_base *base, int skip_equal) { }
-static inline int hrtimer_reprogram(struct hrtimer *timer,
-				    struct hrtimer_clock_base *base)
-{
-	return 0;
-}
+static inline void hrtimer_reprogram(struct hrtimer *timer,
+				     struct hrtimer_clock_base *base) { }
 static inline void hrtimer_init_hres(struct hrtimer_cpu_base *base) { }
 static inline void retrigger_next_event(void *arg) { }
 
@@ -865,6 +903,32 @@ u64 hrtimer_forward(struct hrtimer *timer, ktime_t now, ktime_t interval)
 }
 EXPORT_SYMBOL_GPL(hrtimer_forward);
 
+#ifdef CONFIG_PREEMPT_RT_BASE
+# define wake_up_timer_waiters(b)	wake_up(&(b)->wait)
+
+/**
+ * hrtimer_wait_for_timer - Wait for a running timer
+ *
+ * @timer:	timer to wait for
+ *
+ * The function waits in case the timers callback function is
+ * currently executed on the waitqueue of the timer base. The
+ * waitqueue is woken up after the timer callback function has
+ * finished execution.
+ */
+void hrtimer_wait_for_timer(const struct hrtimer *timer)
+{
+	struct hrtimer_clock_base *base = timer->base;
+
+	if (base && base->cpu_base && !timer->irqsafe)
+		wait_event(base->cpu_base->wait,
+				!(hrtimer_callback_running(timer)));
+}
+
+#else
+# define wake_up_timer_waiters(b)	do { } while (0)
+#endif
+
 /*
  * enqueue_hrtimer - internal function to (re)start a timer
  *
@@ -906,6 +970,11 @@ static void __remove_hrtimer(struct hrtimer *timer,
 	if (!(state & HRTIMER_STATE_ENQUEUED))
 		return;
 
+	if (unlikely(!list_empty(&timer->cb_entry))) {
+		list_del_init(&timer->cb_entry);
+		return;
+	}
+
 	if (!timerqueue_del(&base->active, &timer->node))
 		cpu_base->active_bases &= ~(1 << base->index);
 
@@ -994,7 +1063,16 @@ void hrtimer_start_range_ns(struct hrtimer *timer, ktime_t tim,
 	new_base = switch_hrtimer_base(timer, base, mode & HRTIMER_MODE_PINNED);
 
 	timer_stats_hrtimer_set_start_info(timer);
+#ifdef CONFIG_MISSED_TIMER_OFFSETS_HIST
+	{
+		ktime_t now = new_base->get_time();
 
+		if (ktime_to_ns(tim) < ktime_to_ns(now))
+			timer->praecox = now;
+		else
+			timer->praecox = ktime_set(0, 0);
+	}
+#endif
 	leftmost = enqueue_hrtimer(timer, new_base);
 	if (!leftmost)
 		goto unlock;
@@ -1066,7 +1144,7 @@ int hrtimer_cancel(struct hrtimer *timer)
 
 		if (ret >= 0)
 			return ret;
-		cpu_relax();
+		hrtimer_wait_for_timer(timer);
 	}
 }
 EXPORT_SYMBOL_GPL(hrtimer_cancel);
@@ -1126,6 +1204,7 @@ static void __hrtimer_init(struct hrtimer *timer, clockid_t clock_id,
 
 	base = hrtimer_clockid_to_base(clock_id);
 	timer->base = &cpu_base->clock_base[base];
+	INIT_LIST_HEAD(&timer->cb_entry);
 	timerqueue_init(&timer->node);
 
 #ifdef CONFIG_TIMER_STATS
@@ -1166,6 +1245,7 @@ bool hrtimer_active(const struct hrtimer *timer)
 		seq = raw_read_seqcount_begin(&cpu_base->seq);
 
 		if (timer->state != HRTIMER_STATE_INACTIVE ||
+		    cpu_base->running_soft == timer ||
 		    cpu_base->running == timer)
 			return true;
 
@@ -1256,10 +1336,112 @@ static void __run_hrtimer(struct hrtimer_cpu_base *cpu_base,
 	cpu_base->running = NULL;
 }
 
+#ifdef CONFIG_PREEMPT_RT_BASE
+static void hrtimer_rt_reprogram(int restart, struct hrtimer *timer,
+				 struct hrtimer_clock_base *base)
+{
+	int leftmost;
+
+	if (restart != HRTIMER_NORESTART &&
+	    !(timer->state & HRTIMER_STATE_ENQUEUED)) {
+
+		leftmost = enqueue_hrtimer(timer, base);
+		if (!leftmost)
+			return;
+#ifdef CONFIG_HIGH_RES_TIMERS
+		if (!hrtimer_is_hres_active(timer)) {
+			/*
+			 * Kick to reschedule the next tick to handle the new timer
+			 * on dynticks target.
+			 */
+			if (base->cpu_base->nohz_active)
+				wake_up_nohz_cpu(base->cpu_base->cpu);
+		} else {
+
+			hrtimer_reprogram(timer, base);
+		}
+#endif
+	}
+}
+
+/*
+ * The changes in mainline which removed the callback modes from
+ * hrtimer are not yet working with -rt. The non wakeup_process()
+ * based callbacks which involve sleeping locks need to be treated
+ * seperately.
+ */
+static void hrtimer_rt_run_pending(void)
+{
+	enum hrtimer_restart (*fn)(struct hrtimer *);
+	struct hrtimer_cpu_base *cpu_base;
+	struct hrtimer_clock_base *base;
+	struct hrtimer *timer;
+	int index, restart;
+
+	local_irq_disable();
+	cpu_base = &per_cpu(hrtimer_bases, smp_processor_id());
+
+	raw_spin_lock(&cpu_base->lock);
+
+	for (index = 0; index < HRTIMER_MAX_CLOCK_BASES; index++) {
+		base = &cpu_base->clock_base[index];
+
+		while (!list_empty(&base->expired)) {
+			timer = list_first_entry(&base->expired,
+						 struct hrtimer, cb_entry);
+
+			/*
+			 * Same as the above __run_hrtimer function
+			 * just we run with interrupts enabled.
+			 */
+			debug_deactivate(timer);
+			cpu_base->running_soft = timer;
+			raw_write_seqcount_barrier(&cpu_base->seq);
+
+			__remove_hrtimer(timer, base, HRTIMER_STATE_INACTIVE, 0);
+			timer_stats_account_hrtimer(timer);
+			fn = timer->function;
+
+			raw_spin_unlock_irq(&cpu_base->lock);
+			restart = fn(timer);
+			raw_spin_lock_irq(&cpu_base->lock);
+
+			hrtimer_rt_reprogram(restart, timer, base);
+			raw_write_seqcount_barrier(&cpu_base->seq);
+
+			WARN_ON_ONCE(cpu_base->running_soft != timer);
+			cpu_base->running_soft = NULL;
+		}
+	}
+
+	raw_spin_unlock_irq(&cpu_base->lock);
+
+	wake_up_timer_waiters(cpu_base);
+}
+
+static int hrtimer_rt_defer(struct hrtimer *timer)
+{
+	if (timer->irqsafe)
+		return 0;
+
+	__remove_hrtimer(timer, timer->base, timer->state, 0);
+	list_add_tail(&timer->cb_entry, &timer->base->expired);
+	return 1;
+}
+
+#else
+
+static inline int hrtimer_rt_defer(struct hrtimer *timer) { return 0; }
+
+#endif
+
+static enum hrtimer_restart hrtimer_wakeup(struct hrtimer *timer);
+
 static void __hrtimer_run_queues(struct hrtimer_cpu_base *cpu_base, ktime_t now)
 {
 	struct hrtimer_clock_base *base = cpu_base->clock_base;
 	unsigned int active = cpu_base->active_bases;
+	int raise = 0;
 
 	for (; active; base++, active >>= 1) {
 		struct timerqueue_node *node;
@@ -1275,6 +1457,15 @@ static void __hrtimer_run_queues(struct hrtimer_cpu_base *cpu_base, ktime_t now)
 
 			timer = container_of(node, struct hrtimer, node);
 
+			trace_hrtimer_interrupt(raw_smp_processor_id(),
+			    ktime_to_ns(ktime_sub(ktime_to_ns(timer->praecox) ?
+				timer->praecox : hrtimer_get_expires(timer),
+				basenow)),
+			    current,
+			    timer->function == hrtimer_wakeup ?
+			    container_of(timer, struct hrtimer_sleeper,
+				timer)->task : NULL);
+
 			/*
 			 * The immediate goal for using the softexpires is
 			 * minimizing wakeups, not running timers at the
@@ -1290,9 +1481,14 @@ static void __hrtimer_run_queues(struct hrtimer_cpu_base *cpu_base, ktime_t now)
 			if (basenow.tv64 < hrtimer_get_softexpires_tv64(timer))
 				break;
 
-			__run_hrtimer(cpu_base, base, timer, &basenow);
+			if (!hrtimer_rt_defer(timer))
+				__run_hrtimer(cpu_base, base, timer, &basenow);
+			else
+				raise = 1;
 		}
 	}
+	if (raise)
+		raise_softirq_irqoff(HRTIMER_SOFTIRQ);
 }
 
 #ifdef CONFIG_HIGH_RES_TIMERS
@@ -1455,16 +1651,18 @@ static enum hrtimer_restart hrtimer_wakeup(struct hrtimer *timer)
 void hrtimer_init_sleeper(struct hrtimer_sleeper *sl, struct task_struct *task)
 {
 	sl->timer.function = hrtimer_wakeup;
+	sl->timer.irqsafe = 1;
 	sl->task = task;
 }
 EXPORT_SYMBOL_GPL(hrtimer_init_sleeper);
 
-static int __sched do_nanosleep(struct hrtimer_sleeper *t, enum hrtimer_mode mode)
+static int __sched do_nanosleep(struct hrtimer_sleeper *t, enum hrtimer_mode mode,
+				unsigned long state)
 {
 	hrtimer_init_sleeper(t, current);
 
 	do {
-		set_current_state(TASK_INTERRUPTIBLE);
+		set_current_state(state);
 		hrtimer_start_expires(&t->timer, mode);
 
 		if (likely(t->task))
@@ -1506,7 +1704,8 @@ long __sched hrtimer_nanosleep_restart(struct restart_block *restart)
 				HRTIMER_MODE_ABS);
 	hrtimer_set_expires_tv64(&t.timer, restart->nanosleep.expires);
 
-	if (do_nanosleep(&t, HRTIMER_MODE_ABS))
+	/* cpu_chill() does not care about restart state. */
+	if (do_nanosleep(&t, HRTIMER_MODE_ABS, TASK_INTERRUPTIBLE))
 		goto out;
 
 	rmtp = restart->nanosleep.rmtp;
@@ -1523,8 +1722,10 @@ long __sched hrtimer_nanosleep_restart(struct restart_block *restart)
 	return ret;
 }
 
-long hrtimer_nanosleep(struct timespec *rqtp, struct timespec __user *rmtp,
-		       const enum hrtimer_mode mode, const clockid_t clockid)
+static long
+__hrtimer_nanosleep(struct timespec *rqtp, struct timespec __user *rmtp,
+		    const enum hrtimer_mode mode, const clockid_t clockid,
+		    unsigned long state)
 {
 	struct restart_block *restart;
 	struct hrtimer_sleeper t;
@@ -1537,7 +1738,7 @@ long hrtimer_nanosleep(struct timespec *rqtp, struct timespec __user *rmtp,
 
 	hrtimer_init_on_stack(&t.timer, clockid, mode);
 	hrtimer_set_expires_range_ns(&t.timer, timespec_to_ktime(*rqtp), slack);
-	if (do_nanosleep(&t, mode))
+	if (do_nanosleep(&t, mode, state))
 		goto out;
 
 	/* Absolute timers do not update the rmtp value and restart: */
@@ -1564,6 +1765,12 @@ long hrtimer_nanosleep(struct timespec *rqtp, struct timespec __user *rmtp,
 	return ret;
 }
 
+long hrtimer_nanosleep(struct timespec *rqtp, struct timespec __user *rmtp,
+		       const enum hrtimer_mode mode, const clockid_t clockid)
+{
+	return __hrtimer_nanosleep(rqtp, rmtp, mode, clockid, TASK_INTERRUPTIBLE);
+}
+
 SYSCALL_DEFINE2(nanosleep, struct timespec __user *, rqtp,
 		struct timespec __user *, rmtp)
 {
@@ -1578,6 +1785,26 @@ SYSCALL_DEFINE2(nanosleep, struct timespec __user *, rqtp,
 	return hrtimer_nanosleep(&tu, rmtp, HRTIMER_MODE_REL, CLOCK_MONOTONIC);
 }
 
+#ifdef CONFIG_PREEMPT_RT_FULL
+/*
+ * Sleep for 1 ms in hope whoever holds what we want will let it go.
+ */
+void cpu_chill(void)
+{
+	struct timespec tu = {
+		.tv_nsec = NSEC_PER_MSEC,
+	};
+	unsigned int freeze_flag = current->flags & PF_NOFREEZE;
+
+	current->flags |= PF_NOFREEZE;
+	__hrtimer_nanosleep(&tu, NULL, HRTIMER_MODE_REL, CLOCK_MONOTONIC,
+			    TASK_UNINTERRUPTIBLE);
+	if (!freeze_flag)
+		current->flags &= ~PF_NOFREEZE;
+}
+EXPORT_SYMBOL(cpu_chill);
+#endif
+
 /*
  * Functions related to boot-time initialization:
  */
@@ -1589,10 +1816,14 @@ static void init_hrtimers_cpu(int cpu)
 	for (i = 0; i < HRTIMER_MAX_CLOCK_BASES; i++) {
 		cpu_base->clock_base[i].cpu_base = cpu_base;
 		timerqueue_init_head(&cpu_base->clock_base[i].active);
+		INIT_LIST_HEAD(&cpu_base->clock_base[i].expired);
 	}
 
 	cpu_base->cpu = cpu;
 	hrtimer_init_hres(cpu_base);
+#ifdef CONFIG_PREEMPT_RT_BASE
+	init_waitqueue_head(&cpu_base->wait);
+#endif
 }
 
 #ifdef CONFIG_HOTPLUG_CPU
@@ -1690,11 +1921,21 @@ static struct notifier_block hrtimers_nb = {
 	.notifier_call = hrtimer_cpu_notify,
 };
 
+#ifdef CONFIG_PREEMPT_RT_BASE
+static void run_hrtimer_softirq(struct softirq_action *h)
+{
+	hrtimer_rt_run_pending();
+}
+#endif
+
 void __init hrtimers_init(void)
 {
 	hrtimer_cpu_notify(&hrtimers_nb, (unsigned long)CPU_UP_PREPARE,
 			  (void *)(long)smp_processor_id());
 	register_cpu_notifier(&hrtimers_nb);
+#ifdef CONFIG_PREEMPT_RT_BASE
+	open_softirq(HRTIMER_SOFTIRQ, run_hrtimer_softirq);
+#endif
 }
 
 /**
diff --git a/kernel/time/itimer.c b/kernel/time/itimer.c
index 8d262b467573..d0513909d663 100644
--- a/kernel/time/itimer.c
+++ b/kernel/time/itimer.c
@@ -213,6 +213,7 @@ int do_setitimer(int which, struct itimerval *value, struct itimerval *ovalue)
 		/* We are sharing ->siglock with it_real_fn() */
 		if (hrtimer_try_to_cancel(timer) < 0) {
 			spin_unlock_irq(&tsk->sighand->siglock);
+			hrtimer_wait_for_timer(&tsk->signal->real_timer);
 			goto again;
 		}
 		expires = timeval_to_ktime(value->it_value);
diff --git a/kernel/time/jiffies.c b/kernel/time/jiffies.c
index 347fecf86a3f..2ede47408a3e 100644
--- a/kernel/time/jiffies.c
+++ b/kernel/time/jiffies.c
@@ -74,7 +74,8 @@ static struct clocksource clocksource_jiffies = {
 	.max_cycles	= 10,
 };
 
-__cacheline_aligned_in_smp DEFINE_SEQLOCK(jiffies_lock);
+__cacheline_aligned_in_smp DEFINE_RAW_SPINLOCK(jiffies_lock);
+__cacheline_aligned_in_smp seqcount_t jiffies_seq;
 
 #if (BITS_PER_LONG < 64)
 u64 get_jiffies_64(void)
@@ -83,9 +84,9 @@ u64 get_jiffies_64(void)
 	u64 ret;
 
 	do {
-		seq = read_seqbegin(&jiffies_lock);
+		seq = read_seqcount_begin(&jiffies_seq);
 		ret = jiffies_64;
-	} while (read_seqretry(&jiffies_lock, seq));
+	} while (read_seqcount_retry(&jiffies_seq, seq));
 	return ret;
 }
 EXPORT_SYMBOL(get_jiffies_64);
diff --git a/kernel/time/ntp.c b/kernel/time/ntp.c
index 149cc8086aea..cd925efb239a 100644
--- a/kernel/time/ntp.c
+++ b/kernel/time/ntp.c
@@ -10,6 +10,7 @@
 #include <linux/workqueue.h>
 #include <linux/hrtimer.h>
 #include <linux/jiffies.h>
+#include <linux/kthread.h>
 #include <linux/math64.h>
 #include <linux/timex.h>
 #include <linux/time.h>
@@ -562,10 +563,52 @@ static void sync_cmos_clock(struct work_struct *work)
 			   &sync_cmos_work, timespec64_to_jiffies(&next));
 }
 
+#ifdef CONFIG_PREEMPT_RT_FULL
+/*
+ * RT can not call schedule_delayed_work from real interrupt context.
+ * Need to make a thread to do the real work.
+ */
+static struct task_struct *cmos_delay_thread;
+static bool do_cmos_delay;
+
+static int run_cmos_delay(void *ignore)
+{
+	while (!kthread_should_stop()) {
+		set_current_state(TASK_INTERRUPTIBLE);
+		if (do_cmos_delay) {
+			do_cmos_delay = false;
+			queue_delayed_work(system_power_efficient_wq,
+					   &sync_cmos_work, 0);
+		}
+		schedule();
+	}
+	__set_current_state(TASK_RUNNING);
+	return 0;
+}
+
+void ntp_notify_cmos_timer(void)
+{
+	do_cmos_delay = true;
+	/* Make visible before waking up process */
+	smp_wmb();
+	wake_up_process(cmos_delay_thread);
+}
+
+static __init int create_cmos_delay_thread(void)
+{
+	cmos_delay_thread = kthread_run(run_cmos_delay, NULL, "kcmosdelayd");
+	BUG_ON(!cmos_delay_thread);
+	return 0;
+}
+early_initcall(create_cmos_delay_thread);
+
+#else
+
 void ntp_notify_cmos_timer(void)
 {
 	queue_delayed_work(system_power_efficient_wq, &sync_cmos_work, 0);
 }
+#endif /* CONFIG_PREEMPT_RT_FULL */
 
 #else
 void ntp_notify_cmos_timer(void) { }
diff --git a/kernel/time/posix-cpu-timers.c b/kernel/time/posix-cpu-timers.c
index f5e86d282d52..bf28ef601e0e 100644
--- a/kernel/time/posix-cpu-timers.c
+++ b/kernel/time/posix-cpu-timers.c
@@ -3,6 +3,7 @@
  */
 
 #include <linux/sched.h>
+#include <linux/sched/rt.h>
 #include <linux/posix-timers.h>
 #include <linux/errno.h>
 #include <linux/math64.h>
@@ -650,7 +651,7 @@ static int posix_cpu_timer_set(struct k_itimer *timer, int timer_flags,
 	/*
 	 * Disarm any old timer after extracting its expiry time.
 	 */
-	WARN_ON_ONCE(!irqs_disabled());
+	WARN_ON_ONCE_NONRT(!irqs_disabled());
 
 	ret = 0;
 	old_incr = timer->it.cpu.incr;
@@ -1091,7 +1092,7 @@ void posix_cpu_timer_schedule(struct k_itimer *timer)
 	/*
 	 * Now re-arm for the new expiry time.
 	 */
-	WARN_ON_ONCE(!irqs_disabled());
+	WARN_ON_ONCE_NONRT(!irqs_disabled());
 	arm_timer(timer);
 	unlock_task_sighand(p, &flags);
 
@@ -1182,13 +1183,13 @@ static inline int fastpath_timer_check(struct task_struct *tsk)
  * already updated our counts.  We need to check if any timers fire now.
  * Interrupts are disabled.
  */
-void run_posix_cpu_timers(struct task_struct *tsk)
+static void __run_posix_cpu_timers(struct task_struct *tsk)
 {
 	LIST_HEAD(firing);
 	struct k_itimer *timer, *next;
 	unsigned long flags;
 
-	WARN_ON_ONCE(!irqs_disabled());
+	WARN_ON_ONCE_NONRT(!irqs_disabled());
 
 	/*
 	 * The fast path checks that there are no expired thread or thread
@@ -1242,6 +1243,190 @@ void run_posix_cpu_timers(struct task_struct *tsk)
 	}
 }
 
+#ifdef CONFIG_PREEMPT_RT_BASE
+#include <linux/kthread.h>
+#include <linux/cpu.h>
+DEFINE_PER_CPU(struct task_struct *, posix_timer_task);
+DEFINE_PER_CPU(struct task_struct *, posix_timer_tasklist);
+
+static int posix_cpu_timers_thread(void *data)
+{
+	int cpu = (long)data;
+
+	BUG_ON(per_cpu(posix_timer_task,cpu) != current);
+
+	while (!kthread_should_stop()) {
+		struct task_struct *tsk = NULL;
+		struct task_struct *next = NULL;
+
+		if (cpu_is_offline(cpu))
+			goto wait_to_die;
+
+		/* grab task list */
+		raw_local_irq_disable();
+		tsk = per_cpu(posix_timer_tasklist, cpu);
+		per_cpu(posix_timer_tasklist, cpu) = NULL;
+		raw_local_irq_enable();
+
+		/* its possible the list is empty, just return */
+		if (!tsk) {
+			set_current_state(TASK_INTERRUPTIBLE);
+			schedule();
+			__set_current_state(TASK_RUNNING);
+			continue;
+		}
+
+		/* Process task list */
+		while (1) {
+			/* save next */
+			next = tsk->posix_timer_list;
+
+			/* run the task timers, clear its ptr and
+			 * unreference it
+			 */
+			__run_posix_cpu_timers(tsk);
+			tsk->posix_timer_list = NULL;
+			put_task_struct(tsk);
+
+			/* check if this is the last on the list */
+			if (next == tsk)
+				break;
+			tsk = next;
+		}
+	}
+	return 0;
+
+wait_to_die:
+	/* Wait for kthread_stop */
+	set_current_state(TASK_INTERRUPTIBLE);
+	while (!kthread_should_stop()) {
+		schedule();
+		set_current_state(TASK_INTERRUPTIBLE);
+	}
+	__set_current_state(TASK_RUNNING);
+	return 0;
+}
+
+static inline int __fastpath_timer_check(struct task_struct *tsk)
+{
+	/* tsk == current, ensure it is safe to use ->signal/sighand */
+	if (unlikely(tsk->exit_state))
+		return 0;
+
+	if (!task_cputime_zero(&tsk->cputime_expires))
+			return 1;
+
+	if (!task_cputime_zero(&tsk->signal->cputime_expires))
+			return 1;
+
+	return 0;
+}
+
+void run_posix_cpu_timers(struct task_struct *tsk)
+{
+	unsigned long cpu = smp_processor_id();
+	struct task_struct *tasklist;
+
+	BUG_ON(!irqs_disabled());
+	if(!per_cpu(posix_timer_task, cpu))
+		return;
+	/* get per-cpu references */
+	tasklist = per_cpu(posix_timer_tasklist, cpu);
+
+	/* check to see if we're already queued */
+	if (!tsk->posix_timer_list && __fastpath_timer_check(tsk)) {
+		get_task_struct(tsk);
+		if (tasklist) {
+			tsk->posix_timer_list = tasklist;
+		} else {
+			/*
+			 * The list is terminated by a self-pointing
+			 * task_struct
+			 */
+			tsk->posix_timer_list = tsk;
+		}
+		per_cpu(posix_timer_tasklist, cpu) = tsk;
+
+		wake_up_process(per_cpu(posix_timer_task, cpu));
+	}
+}
+
+/*
+ * posix_cpu_thread_call - callback that gets triggered when a CPU is added.
+ * Here we can start up the necessary migration thread for the new CPU.
+ */
+static int posix_cpu_thread_call(struct notifier_block *nfb,
+				 unsigned long action, void *hcpu)
+{
+	int cpu = (long)hcpu;
+	struct task_struct *p;
+	struct sched_param param;
+
+	switch (action) {
+	case CPU_UP_PREPARE:
+		p = kthread_create(posix_cpu_timers_thread, hcpu,
+					"posixcputmr/%d",cpu);
+		if (IS_ERR(p))
+			return NOTIFY_BAD;
+		p->flags |= PF_NOFREEZE;
+		kthread_bind(p, cpu);
+		/* Must be high prio to avoid getting starved */
+		param.sched_priority = MAX_RT_PRIO-1;
+		sched_setscheduler(p, SCHED_FIFO, &param);
+		per_cpu(posix_timer_task,cpu) = p;
+		break;
+	case CPU_ONLINE:
+		/* Strictly unneccessary, as first user will wake it. */
+		wake_up_process(per_cpu(posix_timer_task,cpu));
+		break;
+#ifdef CONFIG_HOTPLUG_CPU
+	case CPU_UP_CANCELED:
+		/* Unbind it from offline cpu so it can run.  Fall thru. */
+		kthread_bind(per_cpu(posix_timer_task, cpu),
+			     cpumask_any(cpu_online_mask));
+		kthread_stop(per_cpu(posix_timer_task,cpu));
+		per_cpu(posix_timer_task,cpu) = NULL;
+		break;
+	case CPU_DEAD:
+		kthread_stop(per_cpu(posix_timer_task,cpu));
+		per_cpu(posix_timer_task,cpu) = NULL;
+		break;
+#endif
+	}
+	return NOTIFY_OK;
+}
+
+/* Register at highest priority so that task migration (migrate_all_tasks)
+ * happens before everything else.
+ */
+static struct notifier_block posix_cpu_thread_notifier = {
+	.notifier_call = posix_cpu_thread_call,
+	.priority = 10
+};
+
+static int __init posix_cpu_thread_init(void)
+{
+	void *hcpu = (void *)(long)smp_processor_id();
+	/* Start one for boot CPU. */
+	unsigned long cpu;
+
+	/* init the per-cpu posix_timer_tasklets */
+	for_each_possible_cpu(cpu)
+		per_cpu(posix_timer_tasklist, cpu) = NULL;
+
+	posix_cpu_thread_call(&posix_cpu_thread_notifier, CPU_UP_PREPARE, hcpu);
+	posix_cpu_thread_call(&posix_cpu_thread_notifier, CPU_ONLINE, hcpu);
+	register_cpu_notifier(&posix_cpu_thread_notifier);
+	return 0;
+}
+early_initcall(posix_cpu_thread_init);
+#else /* CONFIG_PREEMPT_RT_BASE */
+void run_posix_cpu_timers(struct task_struct *tsk)
+{
+	__run_posix_cpu_timers(tsk);
+}
+#endif /* CONFIG_PREEMPT_RT_BASE */
+
 /*
  * Set one of the process-wide special case CPU timers or RLIMIT_CPU.
  * The tsk->sighand->siglock must be held by the caller.
diff --git a/kernel/time/posix-timers.c b/kernel/time/posix-timers.c
index 31d11ac9fa47..7d0f13828c7f 100644
--- a/kernel/time/posix-timers.c
+++ b/kernel/time/posix-timers.c
@@ -506,6 +506,7 @@ static enum hrtimer_restart posix_timer_fn(struct hrtimer *timer)
 static struct pid *good_sigevent(sigevent_t * event)
 {
 	struct task_struct *rtn = current->group_leader;
+	int sig = event->sigev_signo;
 
 	if ((event->sigev_notify & SIGEV_THREAD_ID ) &&
 		(!(rtn = find_task_by_vpid(event->sigev_notify_thread_id)) ||
@@ -514,7 +515,8 @@ static struct pid *good_sigevent(sigevent_t * event)
 		return NULL;
 
 	if (((event->sigev_notify & ~SIGEV_THREAD_ID) != SIGEV_NONE) &&
-	    ((event->sigev_signo <= 0) || (event->sigev_signo > SIGRTMAX)))
+	    (sig <= 0 || sig > SIGRTMAX || sig_kernel_only(sig) ||
+	     sig_kernel_coredump(sig)))
 		return NULL;
 
 	return task_pid(rtn);
@@ -826,6 +828,20 @@ SYSCALL_DEFINE1(timer_getoverrun, timer_t, timer_id)
 	return overrun;
 }
 
+/*
+ * Protected by RCU!
+ */
+static void timer_wait_for_callback(struct k_clock *kc, struct k_itimer *timr)
+{
+#ifdef CONFIG_PREEMPT_RT_FULL
+	if (kc->timer_set == common_timer_set)
+		hrtimer_wait_for_timer(&timr->it.real.timer);
+	else
+		/* FIXME: Whacky hack for posix-cpu-timers */
+		schedule_timeout(1);
+#endif
+}
+
 /* Set a POSIX.1b interval timer. */
 /* timr->it_lock is taken. */
 static int
@@ -903,6 +919,7 @@ SYSCALL_DEFINE4(timer_settime, timer_t, timer_id, int, flags,
 	if (!timr)
 		return -EINVAL;
 
+	rcu_read_lock();
 	kc = clockid_to_kclock(timr->it_clock);
 	if (WARN_ON_ONCE(!kc || !kc->timer_set))
 		error = -EINVAL;
@@ -911,9 +928,12 @@ SYSCALL_DEFINE4(timer_settime, timer_t, timer_id, int, flags,
 
 	unlock_timer(timr, flag);
 	if (error == TIMER_RETRY) {
+		timer_wait_for_callback(kc, timr);
 		rtn = NULL;	// We already got the old time...
+		rcu_read_unlock();
 		goto retry;
 	}
+	rcu_read_unlock();
 
 	if (old_setting && !error &&
 	    copy_to_user(old_setting, &old_spec, sizeof (old_spec)))
@@ -951,10 +971,15 @@ SYSCALL_DEFINE1(timer_delete, timer_t, timer_id)
 	if (!timer)
 		return -EINVAL;
 
+	rcu_read_lock();
 	if (timer_delete_hook(timer) == TIMER_RETRY) {
 		unlock_timer(timer, flags);
+		timer_wait_for_callback(clockid_to_kclock(timer->it_clock),
+					timer);
+		rcu_read_unlock();
 		goto retry_delete;
 	}
+	rcu_read_unlock();
 
 	spin_lock(&current->sighand->siglock);
 	list_del(&timer->list);
@@ -980,8 +1005,18 @@ static void itimer_delete(struct k_itimer *timer)
 retry_delete:
 	spin_lock_irqsave(&timer->it_lock, flags);
 
-	if (timer_delete_hook(timer) == TIMER_RETRY) {
+	/* On RT we can race with a deletion */
+	if (!timer->it_signal) {
 		unlock_timer(timer, flags);
+		return;
+	}
+
+	if (timer_delete_hook(timer) == TIMER_RETRY) {
+		rcu_read_lock();
+		unlock_timer(timer, flags);
+		timer_wait_for_callback(clockid_to_kclock(timer->it_clock),
+					timer);
+		rcu_read_unlock();
 		goto retry_delete;
 	}
 	list_del(&timer->list);
diff --git a/kernel/time/tick-common.c b/kernel/time/tick-common.c
index 4fcd99e12aa0..5a47f2e98faf 100644
--- a/kernel/time/tick-common.c
+++ b/kernel/time/tick-common.c
@@ -79,13 +79,15 @@ int tick_is_oneshot_available(void)
 static void tick_periodic(int cpu)
 {
 	if (tick_do_timer_cpu == cpu) {
-		write_seqlock(&jiffies_lock);
+		raw_spin_lock(&jiffies_lock);
+		write_seqcount_begin(&jiffies_seq);
 
 		/* Keep track of the next tick event */
 		tick_next_period = ktime_add(tick_next_period, tick_period);
 
 		do_timer(1);
-		write_sequnlock(&jiffies_lock);
+		write_seqcount_end(&jiffies_seq);
+		raw_spin_unlock(&jiffies_lock);
 		update_wall_time();
 	}
 
@@ -157,9 +159,9 @@ void tick_setup_periodic(struct clock_event_device *dev, int broadcast)
 		ktime_t next;
 
 		do {
-			seq = read_seqbegin(&jiffies_lock);
+			seq = read_seqcount_begin(&jiffies_seq);
 			next = tick_next_period;
-		} while (read_seqretry(&jiffies_lock, seq));
+		} while (read_seqcount_retry(&jiffies_seq, seq));
 
 		clockevents_switch_state(dev, CLOCK_EVT_STATE_ONESHOT);
 
diff --git a/kernel/time/tick-sched.c b/kernel/time/tick-sched.c
index 7c7ec4515983..7203c01a53b8 100644
--- a/kernel/time/tick-sched.c
+++ b/kernel/time/tick-sched.c
@@ -62,7 +62,8 @@ static void tick_do_update_jiffies64(ktime_t now)
 		return;
 
 	/* Reevalute with jiffies_lock held */
-	write_seqlock(&jiffies_lock);
+	raw_spin_lock(&jiffies_lock);
+	write_seqcount_begin(&jiffies_seq);
 
 	delta = ktime_sub(now, last_jiffies_update);
 	if (delta.tv64 >= tick_period.tv64) {
@@ -85,10 +86,12 @@ static void tick_do_update_jiffies64(ktime_t now)
 		/* Keep the tick_next_period variable up to date */
 		tick_next_period = ktime_add(last_jiffies_update, tick_period);
 	} else {
-		write_sequnlock(&jiffies_lock);
+		write_seqcount_end(&jiffies_seq);
+		raw_spin_unlock(&jiffies_lock);
 		return;
 	}
-	write_sequnlock(&jiffies_lock);
+	write_seqcount_end(&jiffies_seq);
+	raw_spin_unlock(&jiffies_lock);
 	update_wall_time();
 }
 
@@ -99,12 +102,14 @@ static ktime_t tick_init_jiffy_update(void)
 {
 	ktime_t period;
 
-	write_seqlock(&jiffies_lock);
+	raw_spin_lock(&jiffies_lock);
+	write_seqcount_begin(&jiffies_seq);
 	/* Did we start the jiffies update yet ? */
 	if (last_jiffies_update.tv64 == 0)
 		last_jiffies_update = tick_next_period;
 	period = last_jiffies_update;
-	write_sequnlock(&jiffies_lock);
+	write_seqcount_end(&jiffies_seq);
+	raw_spin_unlock(&jiffies_lock);
 	return period;
 }
 
@@ -176,6 +181,11 @@ static bool can_stop_full_tick(void)
 		return false;
 	}
 
+	if (!arch_irq_work_has_interrupt()) {
+		trace_tick_stop(0, "missing irq work interrupt\n");
+		return false;
+	}
+
 	/* sched_clock_tick() needs us? */
 #ifdef CONFIG_HAVE_UNSTABLE_SCHED_CLOCK
 	/*
@@ -204,6 +214,7 @@ static void nohz_full_kick_work_func(struct irq_work *work)
 
 static DEFINE_PER_CPU(struct irq_work, nohz_full_kick_work) = {
 	.func = nohz_full_kick_work_func,
+	.flags = IRQ_WORK_HARD_IRQ,
 };
 
 /*
@@ -578,10 +589,10 @@ static ktime_t tick_nohz_stop_sched_tick(struct tick_sched *ts,
 
 	/* Read jiffies and the time when jiffies were updated last */
 	do {
-		seq = read_seqbegin(&jiffies_lock);
+		seq = read_seqcount_begin(&jiffies_seq);
 		basemono = last_jiffies_update.tv64;
 		basejiff = jiffies;
-	} while (read_seqretry(&jiffies_lock, seq));
+	} while (read_seqcount_retry(&jiffies_seq, seq));
 	ts->last_jiffies = basejiff;
 
 	if (rcu_needs_cpu(basemono, &next_rcu) ||
@@ -753,14 +764,7 @@ static bool can_stop_idle_tick(int cpu, struct tick_sched *ts)
 		return false;
 
 	if (unlikely(local_softirq_pending() && cpu_online(cpu))) {
-		static int ratelimit;
-
-		if (ratelimit < 10 &&
-		    (local_softirq_pending() & SOFTIRQ_STOP_IDLE_MASK)) {
-			pr_warn("NOHZ: local_softirq_pending %02x\n",
-				(unsigned int) local_softirq_pending());
-			ratelimit++;
-		}
+		softirq_check_pending_idle();
 		return false;
 	}
 
@@ -1100,6 +1104,7 @@ void tick_setup_sched_timer(void)
 	 * Emulate tick processing via per-CPU hrtimers:
 	 */
 	hrtimer_init(&ts->sched_timer, CLOCK_MONOTONIC, HRTIMER_MODE_ABS);
+	ts->sched_timer.irqsafe = 1;
 	ts->sched_timer.function = tick_sched_timer;
 
 	/* Get the next period (per cpu) */
diff --git a/kernel/time/timekeeping.c b/kernel/time/timekeeping.c
index d563c1960302..a69602dce862 100644
--- a/kernel/time/timekeeping.c
+++ b/kernel/time/timekeeping.c
@@ -2048,8 +2048,10 @@ EXPORT_SYMBOL(hardpps);
  */
 void xtime_update(unsigned long ticks)
 {
-	write_seqlock(&jiffies_lock);
+	raw_spin_lock(&jiffies_lock);
+	write_seqcount_begin(&jiffies_seq);
 	do_timer(ticks);
-	write_sequnlock(&jiffies_lock);
+	write_seqcount_end(&jiffies_seq);
+	raw_spin_unlock(&jiffies_lock);
 	update_wall_time();
 }
diff --git a/kernel/time/timekeeping.h b/kernel/time/timekeeping.h
index 704f595ce83f..763a3e5121ff 100644
--- a/kernel/time/timekeeping.h
+++ b/kernel/time/timekeeping.h
@@ -19,7 +19,8 @@ extern void timekeeping_resume(void);
 extern void do_timer(unsigned long ticks);
 extern void update_wall_time(void);
 
-extern seqlock_t jiffies_lock;
+extern raw_spinlock_t jiffies_lock;
+extern seqcount_t jiffies_seq;
 
 #define CS_NAME_LEN	32
 
diff --git a/kernel/time/timer.c b/kernel/time/timer.c
index bbc5d1114583..fee8682c209e 100644
--- a/kernel/time/timer.c
+++ b/kernel/time/timer.c
@@ -80,6 +80,9 @@ struct tvec_root {
 struct tvec_base {
 	spinlock_t lock;
 	struct timer_list *running_timer;
+#ifdef CONFIG_PREEMPT_RT_FULL
+	wait_queue_head_t wait_for_running_timer;
+#endif
 	unsigned long timer_jiffies;
 	unsigned long next_timer;
 	unsigned long active_timers;
@@ -777,6 +780,39 @@ static struct tvec_base *lock_timer_base(struct timer_list *timer,
 		cpu_relax();
 	}
 }
+#ifdef CONFIG_PREEMPT_RT_FULL
+static inline struct tvec_base *switch_timer_base(struct timer_list *timer,
+						  struct tvec_base *old,
+						  struct tvec_base *new)
+{
+	/*
+	 * We cannot do the below because we might be preempted and
+	 * then the preempter would see NULL and loop forever.
+	 */
+	if (spin_trylock(&new->lock)) {
+		WRITE_ONCE(timer->flags,
+			   (timer->flags & ~TIMER_BASEMASK) | new->cpu);
+		spin_unlock(&old->lock);
+		return new;
+	}
+	return old;
+}
+
+#else
+static inline struct tvec_base *switch_timer_base(struct timer_list *timer,
+						  struct tvec_base *old,
+						  struct tvec_base *new)
+{
+	/* See the comment in lock_timer_base() */
+	timer->flags |= TIMER_MIGRATING;
+
+	spin_unlock(&old->lock);
+	spin_lock(&new->lock);
+	WRITE_ONCE(timer->flags,
+		   (timer->flags & ~TIMER_BASEMASK) | new->cpu);
+	return new;
+}
+#endif
 
 static inline int
 __mod_timer(struct timer_list *timer, unsigned long expires,
@@ -807,16 +843,8 @@ __mod_timer(struct timer_list *timer, unsigned long expires,
 		 * handler yet has not finished. This also guarantees that
 		 * the timer is serialized wrt itself.
 		 */
-		if (likely(base->running_timer != timer)) {
-			/* See the comment in lock_timer_base() */
-			timer->flags |= TIMER_MIGRATING;
-
-			spin_unlock(&base->lock);
-			base = new_base;
-			spin_lock(&base->lock);
-			WRITE_ONCE(timer->flags,
-				   (timer->flags & ~TIMER_BASEMASK) | base->cpu);
-		}
+		if (likely(base->running_timer != timer))
+			base = switch_timer_base(timer, base, new_base);
 	}
 
 	timer->expires = expires;
@@ -1006,6 +1034,33 @@ void add_timer_on(struct timer_list *timer, int cpu)
 }
 EXPORT_SYMBOL_GPL(add_timer_on);
 
+#ifdef CONFIG_PREEMPT_RT_FULL
+/*
+ * Wait for a running timer
+ */
+static void wait_for_running_timer(struct timer_list *timer)
+{
+	struct tvec_base *base;
+	u32 tf = timer->flags;
+
+	if (tf & TIMER_MIGRATING)
+		return;
+
+	base = per_cpu_ptr(&tvec_bases, tf & TIMER_CPUMASK);
+	wait_event(base->wait_for_running_timer,
+		   base->running_timer != timer);
+}
+
+# define wakeup_timer_waiters(b)	wake_up(&(b)->wait_for_running_timer)
+#else
+static inline void wait_for_running_timer(struct timer_list *timer)
+{
+	cpu_relax();
+}
+
+# define wakeup_timer_waiters(b)	do { } while (0)
+#endif
+
 /**
  * del_timer - deactive a timer.
  * @timer: the timer to be deactivated
@@ -1063,7 +1118,7 @@ int try_to_del_timer_sync(struct timer_list *timer)
 }
 EXPORT_SYMBOL(try_to_del_timer_sync);
 
-#ifdef CONFIG_SMP
+#if defined(CONFIG_SMP) || defined(CONFIG_PREEMPT_RT_FULL)
 /**
  * del_timer_sync - deactivate a timer and wait for the handler to finish.
  * @timer: the timer to be deactivated
@@ -1123,7 +1178,7 @@ int del_timer_sync(struct timer_list *timer)
 		int ret = try_to_del_timer_sync(timer);
 		if (ret >= 0)
 			return ret;
-		cpu_relax();
+		wait_for_running_timer(timer);
 	}
 }
 EXPORT_SYMBOL(del_timer_sync);
@@ -1248,15 +1303,17 @@ static inline void __run_timers(struct tvec_base *base)
 			if (irqsafe) {
 				spin_unlock(&base->lock);
 				call_timer_fn(timer, fn, data);
+				base->running_timer = NULL;
 				spin_lock(&base->lock);
 			} else {
 				spin_unlock_irq(&base->lock);
 				call_timer_fn(timer, fn, data);
+				base->running_timer = NULL;
 				spin_lock_irq(&base->lock);
 			}
 		}
 	}
-	base->running_timer = NULL;
+	wakeup_timer_waiters(base);
 	spin_unlock_irq(&base->lock);
 }
 
@@ -1390,6 +1447,14 @@ u64 get_next_timer_interrupt(unsigned long basej, u64 basem)
 	if (cpu_is_offline(smp_processor_id()))
 		return expires;
 
+#ifdef CONFIG_PREEMPT_RT_FULL
+	/*
+	 * On PREEMPT_RT we cannot sleep here. As a result we can't take
+	 * the base lock to check when the next timer is pending and so
+	 * we assume the next jiffy.
+	 */
+	return basem + TICK_NSEC;
+#endif
 	spin_lock(&base->lock);
 	if (base->active_timers) {
 		if (time_before_eq(base->next_timer, base->timer_jiffies))
@@ -1416,13 +1481,13 @@ void update_process_times(int user_tick)
 
 	/* Note: this timer irq context must be accounted for as well. */
 	account_process_tick(p, user_tick);
+	scheduler_tick();
 	run_local_timers();
 	rcu_check_callbacks(user_tick);
-#ifdef CONFIG_IRQ_WORK
+#if defined(CONFIG_IRQ_WORK)
 	if (in_irq())
 		irq_work_tick();
 #endif
-	scheduler_tick();
 	run_posix_cpu_timers(p);
 }
 
@@ -1433,6 +1498,8 @@ static void run_timer_softirq(struct softirq_action *h)
 {
 	struct tvec_base *base = this_cpu_ptr(&tvec_bases);
 
+	irq_work_tick_soft();
+
 	if (time_after_eq(jiffies, base->timer_jiffies))
 		__run_timers(base);
 }
@@ -1589,7 +1656,7 @@ static void migrate_timers(int cpu)
 
 	BUG_ON(cpu_online(cpu));
 	old_base = per_cpu_ptr(&tvec_bases, cpu);
-	new_base = get_cpu_ptr(&tvec_bases);
+	new_base = get_local_ptr(&tvec_bases);
 	/*
 	 * The caller is globally serialized and nobody else
 	 * takes two locks at once, deadlock is not possible.
@@ -1613,7 +1680,7 @@ static void migrate_timers(int cpu)
 
 	spin_unlock(&old_base->lock);
 	spin_unlock_irq(&new_base->lock);
-	put_cpu_ptr(&tvec_bases);
+	put_local_ptr(&tvec_bases);
 }
 
 static int timer_cpu_notify(struct notifier_block *self,
@@ -1645,6 +1712,9 @@ static void __init init_timer_cpu(int cpu)
 
 	base->cpu = cpu;
 	spin_lock_init(&base->lock);
+#ifdef CONFIG_PREEMPT_RT_FULL
+	init_waitqueue_head(&base->wait_for_running_timer);
+#endif
 
 	base->timer_jiffies = jiffies;
 	base->next_timer = base->timer_jiffies;
diff --git a/kernel/trace/Kconfig b/kernel/trace/Kconfig
index e45db6b0d878..364ccd0eb57b 100644
--- a/kernel/trace/Kconfig
+++ b/kernel/trace/Kconfig
@@ -187,6 +187,24 @@ config IRQSOFF_TRACER
 	  enabled. This option and the preempt-off timing option can be
 	  used together or separately.)
 
+config INTERRUPT_OFF_HIST
+	bool "Interrupts-off Latency Histogram"
+	depends on IRQSOFF_TRACER
+	help
+	  This option generates continuously updated histograms (one per cpu)
+	  of the duration of time periods with interrupts disabled. The
+	  histograms are disabled by default. To enable them, write a non-zero
+	  number to
+
+	      /sys/kernel/debug/tracing/latency_hist/enable/preemptirqsoff
+
+	  If PREEMPT_OFF_HIST is also selected, additional histograms (one
+	  per cpu) are generated that accumulate the duration of time periods
+	  when both interrupts and preemption are disabled. The histogram data
+	  will be located in the debug file system at
+
+	      /sys/kernel/debug/tracing/latency_hist/irqsoff
+
 config PREEMPT_TRACER
 	bool "Preemption-off Latency Tracer"
 	default n
@@ -211,6 +229,24 @@ config PREEMPT_TRACER
 	  enabled. This option and the irqs-off timing option can be
 	  used together or separately.)
 
+config PREEMPT_OFF_HIST
+	bool "Preemption-off Latency Histogram"
+	depends on PREEMPT_TRACER
+	help
+	  This option generates continuously updated histograms (one per cpu)
+	  of the duration of time periods with preemption disabled. The
+	  histograms are disabled by default. To enable them, write a non-zero
+	  number to
+
+	      /sys/kernel/debug/tracing/latency_hist/enable/preemptirqsoff
+
+	  If INTERRUPT_OFF_HIST is also selected, additional histograms (one
+	  per cpu) are generated that accumulate the duration of time periods
+	  when both interrupts and preemption are disabled. The histogram data
+	  will be located in the debug file system at
+
+	      /sys/kernel/debug/tracing/latency_hist/preemptoff
+
 config SCHED_TRACER
 	bool "Scheduling Latency Tracer"
 	select GENERIC_TRACER
@@ -221,6 +257,74 @@ config SCHED_TRACER
 	  This tracer tracks the latency of the highest priority task
 	  to be scheduled in, starting from the point it has woken up.
 
+config WAKEUP_LATENCY_HIST
+	bool "Scheduling Latency Histogram"
+	depends on SCHED_TRACER
+	help
+	  This option generates continuously updated histograms (one per cpu)
+	  of the scheduling latency of the highest priority task.
+	  The histograms are disabled by default. To enable them, write a
+	  non-zero number to
+
+	      /sys/kernel/debug/tracing/latency_hist/enable/wakeup
+
+	  Two different algorithms are used, one to determine the latency of
+	  processes that exclusively use the highest priority of the system and
+	  another one to determine the latency of processes that share the
+	  highest system priority with other processes. The former is used to
+	  improve hardware and system software, the latter to optimize the
+	  priority design of a given system. The histogram data will be
+	  located in the debug file system at
+
+	      /sys/kernel/debug/tracing/latency_hist/wakeup
+
+	  and
+
+	      /sys/kernel/debug/tracing/latency_hist/wakeup/sharedprio
+
+	  If both Scheduling Latency Histogram and Missed Timer Offsets
+	  Histogram are selected, additional histogram data will be collected
+	  that contain, in addition to the wakeup latency, the timer latency, in
+	  case the wakeup was triggered by an expired timer. These histograms
+	  are available in the
+
+	      /sys/kernel/debug/tracing/latency_hist/timerandwakeup
+
+	  directory. They reflect the apparent interrupt and scheduling latency
+	  and are best suitable to determine the worst-case latency of a given
+	  system. To enable these histograms, write a non-zero number to
+
+	      /sys/kernel/debug/tracing/latency_hist/enable/timerandwakeup
+
+config MISSED_TIMER_OFFSETS_HIST
+	depends on HIGH_RES_TIMERS
+	select GENERIC_TRACER
+	bool "Missed Timer Offsets Histogram"
+	help
+	  Generate a histogram of missed timer offsets in microseconds. The
+	  histograms are disabled by default. To enable them, write a non-zero
+	  number to
+
+	      /sys/kernel/debug/tracing/latency_hist/enable/missed_timer_offsets
+
+	  The histogram data will be located in the debug file system at
+
+	      /sys/kernel/debug/tracing/latency_hist/missed_timer_offsets
+
+	  If both Scheduling Latency Histogram and Missed Timer Offsets
+	  Histogram are selected, additional histogram data will be collected
+	  that contain, in addition to the wakeup latency, the timer latency, in
+	  case the wakeup was triggered by an expired timer. These histograms
+	  are available in the
+
+	      /sys/kernel/debug/tracing/latency_hist/timerandwakeup
+
+	  directory. They reflect the apparent interrupt and scheduling latency
+	  and are best suitable to determine the worst-case latency of a given
+	  system. To enable these histograms, write a non-zero number to
+
+	      /sys/kernel/debug/tracing/latency_hist/enable/timerandwakeup
+
 config ENABLE_DEFAULT_TRACERS
 	bool "Trace process context switches and events"
 	depends on !GENERIC_TRACER
diff --git a/kernel/trace/Makefile b/kernel/trace/Makefile
index 9b1044e936a6..3bbaea06824a 100644
--- a/kernel/trace/Makefile
+++ b/kernel/trace/Makefile
@@ -36,6 +36,10 @@ obj-$(CONFIG_FUNCTION_TRACER) += trace_functions.o
 obj-$(CONFIG_IRQSOFF_TRACER) += trace_irqsoff.o
 obj-$(CONFIG_PREEMPT_TRACER) += trace_irqsoff.o
 obj-$(CONFIG_SCHED_TRACER) += trace_sched_wakeup.o
+obj-$(CONFIG_INTERRUPT_OFF_HIST) += latency_hist.o
+obj-$(CONFIG_PREEMPT_OFF_HIST) += latency_hist.o
+obj-$(CONFIG_WAKEUP_LATENCY_HIST) += latency_hist.o
+obj-$(CONFIG_MISSED_TIMER_OFFSETS_HIST) += latency_hist.o
 obj-$(CONFIG_NOP_TRACER) += trace_nop.o
 obj-$(CONFIG_STACK_TRACER) += trace_stack.o
 obj-$(CONFIG_MMIOTRACE) += trace_mmiotrace.o
diff --git a/kernel/trace/latency_hist.c b/kernel/trace/latency_hist.c
new file mode 100644
index 000000000000..7f6ee70dea41
--- /dev/null
+++ b/kernel/trace/latency_hist.c
@@ -0,0 +1,1178 @@
+/*
+ * kernel/trace/latency_hist.c
+ *
+ * Add support for histograms of preemption-off latency and
+ * interrupt-off latency and wakeup latency, it depends on
+ * Real-Time Preemption Support.
+ *
+ *  Copyright (C) 2005 MontaVista Software, Inc.
+ *  Yi Yang <yyang@ch.mvista.com>
+ *
+ *  Converted to work with the new latency tracer.
+ *  Copyright (C) 2008 Red Hat, Inc.
+ *    Steven Rostedt <srostedt@redhat.com>
+ *
+ */
+#include <linux/module.h>
+#include <linux/debugfs.h>
+#include <linux/seq_file.h>
+#include <linux/percpu.h>
+#include <linux/kallsyms.h>
+#include <linux/uaccess.h>
+#include <linux/sched.h>
+#include <linux/sched/rt.h>
+#include <linux/slab.h>
+#include <linux/atomic.h>
+#include <asm/div64.h>
+
+#include "trace.h"
+#include <trace/events/sched.h>
+
+#define NSECS_PER_USECS 1000L
+
+#define CREATE_TRACE_POINTS
+#include <trace/events/hist.h>
+
+enum {
+	IRQSOFF_LATENCY = 0,
+	PREEMPTOFF_LATENCY,
+	PREEMPTIRQSOFF_LATENCY,
+	WAKEUP_LATENCY,
+	WAKEUP_LATENCY_SHAREDPRIO,
+	MISSED_TIMER_OFFSETS,
+	TIMERANDWAKEUP_LATENCY,
+	MAX_LATENCY_TYPE,
+};
+
+#define MAX_ENTRY_NUM 10240
+
+struct hist_data {
+	atomic_t hist_mode; /* 0 log, 1 don't log */
+	long offset; /* set it to MAX_ENTRY_NUM/2 for a bipolar scale */
+	long min_lat;
+	long max_lat;
+	unsigned long long below_hist_bound_samples;
+	unsigned long long above_hist_bound_samples;
+	long long accumulate_lat;
+	unsigned long long total_samples;
+	unsigned long long hist_array[MAX_ENTRY_NUM];
+};
+
+struct enable_data {
+	int latency_type;
+	int enabled;
+};
+
+static char *latency_hist_dir_root = "latency_hist";
+
+#ifdef CONFIG_INTERRUPT_OFF_HIST
+static DEFINE_PER_CPU(struct hist_data, irqsoff_hist);
+static char *irqsoff_hist_dir = "irqsoff";
+static DEFINE_PER_CPU(cycles_t, hist_irqsoff_start);
+static DEFINE_PER_CPU(int, hist_irqsoff_counting);
+#endif
+
+#ifdef CONFIG_PREEMPT_OFF_HIST
+static DEFINE_PER_CPU(struct hist_data, preemptoff_hist);
+static char *preemptoff_hist_dir = "preemptoff";
+static DEFINE_PER_CPU(cycles_t, hist_preemptoff_start);
+static DEFINE_PER_CPU(int, hist_preemptoff_counting);
+#endif
+
+#if defined(CONFIG_PREEMPT_OFF_HIST) && defined(CONFIG_INTERRUPT_OFF_HIST)
+static DEFINE_PER_CPU(struct hist_data, preemptirqsoff_hist);
+static char *preemptirqsoff_hist_dir = "preemptirqsoff";
+static DEFINE_PER_CPU(cycles_t, hist_preemptirqsoff_start);
+static DEFINE_PER_CPU(int, hist_preemptirqsoff_counting);
+#endif
+
+#if defined(CONFIG_PREEMPT_OFF_HIST) || defined(CONFIG_INTERRUPT_OFF_HIST)
+static notrace void probe_preemptirqsoff_hist(void *v, int reason, int start);
+static struct enable_data preemptirqsoff_enabled_data = {
+	.latency_type = PREEMPTIRQSOFF_LATENCY,
+	.enabled = 0,
+};
+#endif
+
+#if defined(CONFIG_WAKEUP_LATENCY_HIST) || \
+	defined(CONFIG_MISSED_TIMER_OFFSETS_HIST)
+struct maxlatproc_data {
+	char comm[FIELD_SIZEOF(struct task_struct, comm)];
+	char current_comm[FIELD_SIZEOF(struct task_struct, comm)];
+	int pid;
+	int current_pid;
+	int prio;
+	int current_prio;
+	long latency;
+	long timeroffset;
+	cycle_t timestamp;
+};
+#endif
+
+#ifdef CONFIG_WAKEUP_LATENCY_HIST
+static DEFINE_PER_CPU(struct hist_data, wakeup_latency_hist);
+static DEFINE_PER_CPU(struct hist_data, wakeup_latency_hist_sharedprio);
+static char *wakeup_latency_hist_dir = "wakeup";
+static char *wakeup_latency_hist_dir_sharedprio = "sharedprio";
+static notrace void probe_wakeup_latency_hist_start(void *v,
+	struct task_struct *p);
+static notrace void probe_wakeup_latency_hist_stop(void *v,
+	bool preempt, struct task_struct *prev, struct task_struct *next);
+static notrace void probe_sched_migrate_task(void *,
+	struct task_struct *task, int cpu);
+static struct enable_data wakeup_latency_enabled_data = {
+	.latency_type = WAKEUP_LATENCY,
+	.enabled = 0,
+};
+static DEFINE_PER_CPU(struct maxlatproc_data, wakeup_maxlatproc);
+static DEFINE_PER_CPU(struct maxlatproc_data, wakeup_maxlatproc_sharedprio);
+static DEFINE_PER_CPU(struct task_struct *, wakeup_task);
+static DEFINE_PER_CPU(int, wakeup_sharedprio);
+static unsigned long wakeup_pid;
+#endif
+
+#ifdef CONFIG_MISSED_TIMER_OFFSETS_HIST
+static DEFINE_PER_CPU(struct hist_data, missed_timer_offsets);
+static char *missed_timer_offsets_dir = "missed_timer_offsets";
+static notrace void probe_hrtimer_interrupt(void *v, int cpu,
+	long long offset, struct task_struct *curr, struct task_struct *task);
+static struct enable_data missed_timer_offsets_enabled_data = {
+	.latency_type = MISSED_TIMER_OFFSETS,
+	.enabled = 0,
+};
+static DEFINE_PER_CPU(struct maxlatproc_data, missed_timer_offsets_maxlatproc);
+static unsigned long missed_timer_offsets_pid;
+#endif
+
+#if defined(CONFIG_WAKEUP_LATENCY_HIST) && \
+	defined(CONFIG_MISSED_TIMER_OFFSETS_HIST)
+static DEFINE_PER_CPU(struct hist_data, timerandwakeup_latency_hist);
+static char *timerandwakeup_latency_hist_dir = "timerandwakeup";
+static struct enable_data timerandwakeup_enabled_data = {
+	.latency_type = TIMERANDWAKEUP_LATENCY,
+	.enabled = 0,
+};
+static DEFINE_PER_CPU(struct maxlatproc_data, timerandwakeup_maxlatproc);
+#endif
+
+void notrace latency_hist(int latency_type, int cpu, long latency,
+			  long timeroffset, cycle_t stop,
+			  struct task_struct *p)
+{
+	struct hist_data *my_hist;
+#if defined(CONFIG_WAKEUP_LATENCY_HIST) || \
+	defined(CONFIG_MISSED_TIMER_OFFSETS_HIST)
+	struct maxlatproc_data *mp = NULL;
+#endif
+
+	if (!cpu_possible(cpu) || latency_type < 0 ||
+	    latency_type >= MAX_LATENCY_TYPE)
+		return;
+
+	switch (latency_type) {
+#ifdef CONFIG_INTERRUPT_OFF_HIST
+	case IRQSOFF_LATENCY:
+		my_hist = &per_cpu(irqsoff_hist, cpu);
+		break;
+#endif
+#ifdef CONFIG_PREEMPT_OFF_HIST
+	case PREEMPTOFF_LATENCY:
+		my_hist = &per_cpu(preemptoff_hist, cpu);
+		break;
+#endif
+#if defined(CONFIG_PREEMPT_OFF_HIST) && defined(CONFIG_INTERRUPT_OFF_HIST)
+	case PREEMPTIRQSOFF_LATENCY:
+		my_hist = &per_cpu(preemptirqsoff_hist, cpu);
+		break;
+#endif
+#ifdef CONFIG_WAKEUP_LATENCY_HIST
+	case WAKEUP_LATENCY:
+		my_hist = &per_cpu(wakeup_latency_hist, cpu);
+		mp = &per_cpu(wakeup_maxlatproc, cpu);
+		break;
+	case WAKEUP_LATENCY_SHAREDPRIO:
+		my_hist = &per_cpu(wakeup_latency_hist_sharedprio, cpu);
+		mp = &per_cpu(wakeup_maxlatproc_sharedprio, cpu);
+		break;
+#endif
+#ifdef CONFIG_MISSED_TIMER_OFFSETS_HIST
+	case MISSED_TIMER_OFFSETS:
+		my_hist = &per_cpu(missed_timer_offsets, cpu);
+		mp = &per_cpu(missed_timer_offsets_maxlatproc, cpu);
+		break;
+#endif
+#if defined(CONFIG_WAKEUP_LATENCY_HIST) && \
+	defined(CONFIG_MISSED_TIMER_OFFSETS_HIST)
+	case TIMERANDWAKEUP_LATENCY:
+		my_hist = &per_cpu(timerandwakeup_latency_hist, cpu);
+		mp = &per_cpu(timerandwakeup_maxlatproc, cpu);
+		break;
+#endif
+
+	default:
+		return;
+	}
+
+	latency += my_hist->offset;
+
+	if (atomic_read(&my_hist->hist_mode) == 0)
+		return;
+
+	if (latency < 0 || latency >= MAX_ENTRY_NUM) {
+		if (latency < 0)
+			my_hist->below_hist_bound_samples++;
+		else
+			my_hist->above_hist_bound_samples++;
+	} else
+		my_hist->hist_array[latency]++;
+
+	if (unlikely(latency > my_hist->max_lat ||
+	    my_hist->min_lat == LONG_MAX)) {
+#if defined(CONFIG_WAKEUP_LATENCY_HIST) || \
+	defined(CONFIG_MISSED_TIMER_OFFSETS_HIST)
+		if (latency_type == WAKEUP_LATENCY ||
+		    latency_type == WAKEUP_LATENCY_SHAREDPRIO ||
+		    latency_type == MISSED_TIMER_OFFSETS ||
+		    latency_type == TIMERANDWAKEUP_LATENCY) {
+			strncpy(mp->comm, p->comm, sizeof(mp->comm));
+			strncpy(mp->current_comm, current->comm,
+			    sizeof(mp->current_comm));
+			mp->pid = task_pid_nr(p);
+			mp->current_pid = task_pid_nr(current);
+			mp->prio = p->prio;
+			mp->current_prio = current->prio;
+			mp->latency = latency;
+			mp->timeroffset = timeroffset;
+			mp->timestamp = stop;
+		}
+#endif
+		my_hist->max_lat = latency;
+	}
+	if (unlikely(latency < my_hist->min_lat))
+		my_hist->min_lat = latency;
+	my_hist->total_samples++;
+	my_hist->accumulate_lat += latency;
+}
+
+static void *l_start(struct seq_file *m, loff_t *pos)
+{
+	loff_t *index_ptr = NULL;
+	loff_t index = *pos;
+	struct hist_data *my_hist = m->private;
+
+	if (index == 0) {
+		char minstr[32], avgstr[32], maxstr[32];
+
+		atomic_dec(&my_hist->hist_mode);
+
+		if (likely(my_hist->total_samples)) {
+			long avg = (long) div64_s64(my_hist->accumulate_lat,
+			    my_hist->total_samples);
+			snprintf(minstr, sizeof(minstr), "%ld",
+			    my_hist->min_lat - my_hist->offset);
+			snprintf(avgstr, sizeof(avgstr), "%ld",
+			    avg - my_hist->offset);
+			snprintf(maxstr, sizeof(maxstr), "%ld",
+			    my_hist->max_lat - my_hist->offset);
+		} else {
+			strcpy(minstr, "<undef>");
+			strcpy(avgstr, minstr);
+			strcpy(maxstr, minstr);
+		}
+
+		seq_printf(m, "#Minimum latency: %s microseconds\n"
+			   "#Average latency: %s microseconds\n"
+			   "#Maximum latency: %s microseconds\n"
+			   "#Total samples: %llu\n"
+			   "#There are %llu samples lower than %ld"
+			   " microseconds.\n"
+			   "#There are %llu samples greater or equal"
+			   " than %ld microseconds.\n"
+			   "#usecs\t%16s\n",
+			   minstr, avgstr, maxstr,
+			   my_hist->total_samples,
+			   my_hist->below_hist_bound_samples,
+			   -my_hist->offset,
+			   my_hist->above_hist_bound_samples,
+			   MAX_ENTRY_NUM - my_hist->offset,
+			   "samples");
+	}
+	if (index < MAX_ENTRY_NUM) {
+		index_ptr = kmalloc(sizeof(loff_t), GFP_KERNEL);
+		if (index_ptr)
+			*index_ptr = index;
+	}
+
+	return index_ptr;
+}
+
+static void *l_next(struct seq_file *m, void *p, loff_t *pos)
+{
+	loff_t *index_ptr = p;
+	struct hist_data *my_hist = m->private;
+
+	if (++*pos >= MAX_ENTRY_NUM) {
+		atomic_inc(&my_hist->hist_mode);
+		return NULL;
+	}
+	*index_ptr = *pos;
+	return index_ptr;
+}
+
+static void l_stop(struct seq_file *m, void *p)
+{
+	kfree(p);
+}
+
+static int l_show(struct seq_file *m, void *p)
+{
+	int index = *(loff_t *) p;
+	struct hist_data *my_hist = m->private;
+
+	seq_printf(m, "%6ld\t%16llu\n", index - my_hist->offset,
+	    my_hist->hist_array[index]);
+	return 0;
+}
+
+static const struct seq_operations latency_hist_seq_op = {
+	.start = l_start,
+	.next  = l_next,
+	.stop  = l_stop,
+	.show  = l_show
+};
+
+static int latency_hist_open(struct inode *inode, struct file *file)
+{
+	int ret;
+
+	ret = seq_open(file, &latency_hist_seq_op);
+	if (!ret) {
+		struct seq_file *seq = file->private_data;
+		seq->private = inode->i_private;
+	}
+	return ret;
+}
+
+static const struct file_operations latency_hist_fops = {
+	.open = latency_hist_open,
+	.read = seq_read,
+	.llseek = seq_lseek,
+	.release = seq_release,
+};
+
+#if defined(CONFIG_WAKEUP_LATENCY_HIST) || \
+	defined(CONFIG_MISSED_TIMER_OFFSETS_HIST)
+static void clear_maxlatprocdata(struct maxlatproc_data *mp)
+{
+	mp->comm[0] = mp->current_comm[0] = '\0';
+	mp->prio = mp->current_prio = mp->pid = mp->current_pid =
+	    mp->latency = mp->timeroffset = -1;
+	mp->timestamp = 0;
+}
+#endif
+
+static void hist_reset(struct hist_data *hist)
+{
+	atomic_dec(&hist->hist_mode);
+
+	memset(hist->hist_array, 0, sizeof(hist->hist_array));
+	hist->below_hist_bound_samples = 0ULL;
+	hist->above_hist_bound_samples = 0ULL;
+	hist->min_lat = LONG_MAX;
+	hist->max_lat = LONG_MIN;
+	hist->total_samples = 0ULL;
+	hist->accumulate_lat = 0LL;
+
+	atomic_inc(&hist->hist_mode);
+}
+
+static ssize_t
+latency_hist_reset(struct file *file, const char __user *a,
+		   size_t size, loff_t *off)
+{
+	int cpu;
+	struct hist_data *hist = NULL;
+#if defined(CONFIG_WAKEUP_LATENCY_HIST) || \
+	defined(CONFIG_MISSED_TIMER_OFFSETS_HIST)
+	struct maxlatproc_data *mp = NULL;
+#endif
+	off_t latency_type = (off_t) file->private_data;
+
+	for_each_online_cpu(cpu) {
+
+		switch (latency_type) {
+#ifdef CONFIG_PREEMPT_OFF_HIST
+		case PREEMPTOFF_LATENCY:
+			hist = &per_cpu(preemptoff_hist, cpu);
+			break;
+#endif
+#ifdef CONFIG_INTERRUPT_OFF_HIST
+		case IRQSOFF_LATENCY:
+			hist = &per_cpu(irqsoff_hist, cpu);
+			break;
+#endif
+#if defined(CONFIG_INTERRUPT_OFF_HIST) && defined(CONFIG_PREEMPT_OFF_HIST)
+		case PREEMPTIRQSOFF_LATENCY:
+			hist = &per_cpu(preemptirqsoff_hist, cpu);
+			break;
+#endif
+#ifdef CONFIG_WAKEUP_LATENCY_HIST
+		case WAKEUP_LATENCY:
+			hist = &per_cpu(wakeup_latency_hist, cpu);
+			mp = &per_cpu(wakeup_maxlatproc, cpu);
+			break;
+		case WAKEUP_LATENCY_SHAREDPRIO:
+			hist = &per_cpu(wakeup_latency_hist_sharedprio, cpu);
+			mp = &per_cpu(wakeup_maxlatproc_sharedprio, cpu);
+			break;
+#endif
+#ifdef CONFIG_MISSED_TIMER_OFFSETS_HIST
+		case MISSED_TIMER_OFFSETS:
+			hist = &per_cpu(missed_timer_offsets, cpu);
+			mp = &per_cpu(missed_timer_offsets_maxlatproc, cpu);
+			break;
+#endif
+#if defined(CONFIG_WAKEUP_LATENCY_HIST) && \
+	defined(CONFIG_MISSED_TIMER_OFFSETS_HIST)
+		case TIMERANDWAKEUP_LATENCY:
+			hist = &per_cpu(timerandwakeup_latency_hist, cpu);
+			mp = &per_cpu(timerandwakeup_maxlatproc, cpu);
+			break;
+#endif
+		}
+
+		hist_reset(hist);
+#if defined(CONFIG_WAKEUP_LATENCY_HIST) || \
+	defined(CONFIG_MISSED_TIMER_OFFSETS_HIST)
+		if (latency_type == WAKEUP_LATENCY ||
+		    latency_type == WAKEUP_LATENCY_SHAREDPRIO ||
+		    latency_type == MISSED_TIMER_OFFSETS ||
+		    latency_type == TIMERANDWAKEUP_LATENCY)
+			clear_maxlatprocdata(mp);
+#endif
+	}
+
+	return size;
+}
+
+#if defined(CONFIG_WAKEUP_LATENCY_HIST) || \
+	defined(CONFIG_MISSED_TIMER_OFFSETS_HIST)
+static ssize_t
+show_pid(struct file *file, char __user *ubuf, size_t cnt, loff_t *ppos)
+{
+	char buf[64];
+	int r;
+	unsigned long *this_pid = file->private_data;
+
+	r = snprintf(buf, sizeof(buf), "%lu\n", *this_pid);
+	return simple_read_from_buffer(ubuf, cnt, ppos, buf, r);
+}
+
+static ssize_t do_pid(struct file *file, const char __user *ubuf,
+		      size_t cnt, loff_t *ppos)
+{
+	char buf[64];
+	unsigned long pid;
+	unsigned long *this_pid = file->private_data;
+
+	if (cnt >= sizeof(buf))
+		return -EINVAL;
+
+	if (copy_from_user(&buf, ubuf, cnt))
+		return -EFAULT;
+
+	buf[cnt] = '\0';
+
+	if (kstrtoul(buf, 10, &pid))
+		return -EINVAL;
+
+	*this_pid = pid;
+
+	return cnt;
+}
+#endif
+
+#if defined(CONFIG_WAKEUP_LATENCY_HIST) || \
+	defined(CONFIG_MISSED_TIMER_OFFSETS_HIST)
+static ssize_t
+show_maxlatproc(struct file *file, char __user *ubuf, size_t cnt, loff_t *ppos)
+{
+	int r;
+	struct maxlatproc_data *mp = file->private_data;
+	int strmaxlen = (TASK_COMM_LEN * 2) + (8 * 8);
+	unsigned long long t;
+	unsigned long usecs, secs;
+	char *buf;
+
+	if (mp->pid == -1 || mp->current_pid == -1) {
+		buf = "(none)\n";
+		return simple_read_from_buffer(ubuf, cnt, ppos, buf,
+		    strlen(buf));
+	}
+
+	buf = kmalloc(strmaxlen, GFP_KERNEL);
+	if (buf == NULL)
+		return -ENOMEM;
+
+	t = ns2usecs(mp->timestamp);
+	usecs = do_div(t, USEC_PER_SEC);
+	secs = (unsigned long) t;
+	r = snprintf(buf, strmaxlen,
+	    "%d %d %ld (%ld) %s <- %d %d %s %lu.%06lu\n", mp->pid,
+	    MAX_RT_PRIO-1 - mp->prio, mp->latency, mp->timeroffset, mp->comm,
+	    mp->current_pid, MAX_RT_PRIO-1 - mp->current_prio, mp->current_comm,
+	    secs, usecs);
+	r = simple_read_from_buffer(ubuf, cnt, ppos, buf, r);
+	kfree(buf);
+	return r;
+}
+#endif
+
+static ssize_t
+show_enable(struct file *file, char __user *ubuf, size_t cnt, loff_t *ppos)
+{
+	char buf[64];
+	struct enable_data *ed = file->private_data;
+	int r;
+
+	r = snprintf(buf, sizeof(buf), "%d\n", ed->enabled);
+	return simple_read_from_buffer(ubuf, cnt, ppos, buf, r);
+}
+
+static ssize_t
+do_enable(struct file *file, const char __user *ubuf, size_t cnt, loff_t *ppos)
+{
+	char buf[64];
+	long enable;
+	struct enable_data *ed = file->private_data;
+
+	if (cnt >= sizeof(buf))
+		return -EINVAL;
+
+	if (copy_from_user(&buf, ubuf, cnt))
+		return -EFAULT;
+
+	buf[cnt] = 0;
+
+	if (kstrtoul(buf, 10, &enable))
+		return -EINVAL;
+
+	if ((enable && ed->enabled) || (!enable && !ed->enabled))
+		return cnt;
+
+	if (enable) {
+		int ret;
+
+		switch (ed->latency_type) {
+#if defined(CONFIG_INTERRUPT_OFF_HIST) || defined(CONFIG_PREEMPT_OFF_HIST)
+		case PREEMPTIRQSOFF_LATENCY:
+			ret = register_trace_preemptirqsoff_hist(
+			    probe_preemptirqsoff_hist, NULL);
+			if (ret) {
+				pr_info("wakeup trace: Couldn't assign "
+				    "probe_preemptirqsoff_hist "
+				    "to trace_preemptirqsoff_hist\n");
+				return ret;
+			}
+			break;
+#endif
+#ifdef CONFIG_WAKEUP_LATENCY_HIST
+		case WAKEUP_LATENCY:
+			ret = register_trace_sched_wakeup(
+			    probe_wakeup_latency_hist_start, NULL);
+			if (ret) {
+				pr_info("wakeup trace: Couldn't assign "
+				    "probe_wakeup_latency_hist_start "
+				    "to trace_sched_wakeup\n");
+				return ret;
+			}
+			ret = register_trace_sched_wakeup_new(
+			    probe_wakeup_latency_hist_start, NULL);
+			if (ret) {
+				pr_info("wakeup trace: Couldn't assign "
+				    "probe_wakeup_latency_hist_start "
+				    "to trace_sched_wakeup_new\n");
+				unregister_trace_sched_wakeup(
+				    probe_wakeup_latency_hist_start, NULL);
+				return ret;
+			}
+			ret = register_trace_sched_switch(
+			    probe_wakeup_latency_hist_stop, NULL);
+			if (ret) {
+				pr_info("wakeup trace: Couldn't assign "
+				    "probe_wakeup_latency_hist_stop "
+				    "to trace_sched_switch\n");
+				unregister_trace_sched_wakeup(
+				    probe_wakeup_latency_hist_start, NULL);
+				unregister_trace_sched_wakeup_new(
+				    probe_wakeup_latency_hist_start, NULL);
+				return ret;
+			}
+			ret = register_trace_sched_migrate_task(
+			    probe_sched_migrate_task, NULL);
+			if (ret) {
+				pr_info("wakeup trace: Couldn't assign "
+				    "probe_sched_migrate_task "
+				    "to trace_sched_migrate_task\n");
+				unregister_trace_sched_wakeup(
+				    probe_wakeup_latency_hist_start, NULL);
+				unregister_trace_sched_wakeup_new(
+				    probe_wakeup_latency_hist_start, NULL);
+				unregister_trace_sched_switch(
+				    probe_wakeup_latency_hist_stop, NULL);
+				return ret;
+			}
+			break;
+#endif
+#ifdef CONFIG_MISSED_TIMER_OFFSETS_HIST
+		case MISSED_TIMER_OFFSETS:
+			ret = register_trace_hrtimer_interrupt(
+			    probe_hrtimer_interrupt, NULL);
+			if (ret) {
+				pr_info("wakeup trace: Couldn't assign "
+				    "probe_hrtimer_interrupt "
+				    "to trace_hrtimer_interrupt\n");
+				return ret;
+			}
+			break;
+#endif
+#if defined(CONFIG_WAKEUP_LATENCY_HIST) && \
+	defined(CONFIG_MISSED_TIMER_OFFSETS_HIST)
+		case TIMERANDWAKEUP_LATENCY:
+			if (!wakeup_latency_enabled_data.enabled ||
+			    !missed_timer_offsets_enabled_data.enabled)
+				return -EINVAL;
+			break;
+#endif
+		default:
+			break;
+		}
+	} else {
+		switch (ed->latency_type) {
+#if defined(CONFIG_INTERRUPT_OFF_HIST) || defined(CONFIG_PREEMPT_OFF_HIST)
+		case PREEMPTIRQSOFF_LATENCY:
+			{
+				int cpu;
+
+				unregister_trace_preemptirqsoff_hist(
+				    probe_preemptirqsoff_hist, NULL);
+				for_each_online_cpu(cpu) {
+#ifdef CONFIG_INTERRUPT_OFF_HIST
+					per_cpu(hist_irqsoff_counting,
+					    cpu) = 0;
+#endif
+#ifdef CONFIG_PREEMPT_OFF_HIST
+					per_cpu(hist_preemptoff_counting,
+					    cpu) = 0;
+#endif
+#if defined(CONFIG_INTERRUPT_OFF_HIST) && defined(CONFIG_PREEMPT_OFF_HIST)
+					per_cpu(hist_preemptirqsoff_counting,
+					    cpu) = 0;
+#endif
+				}
+			}
+			break;
+#endif
+#ifdef CONFIG_WAKEUP_LATENCY_HIST
+		case WAKEUP_LATENCY:
+			{
+				int cpu;
+
+				unregister_trace_sched_wakeup(
+				    probe_wakeup_latency_hist_start, NULL);
+				unregister_trace_sched_wakeup_new(
+				    probe_wakeup_latency_hist_start, NULL);
+				unregister_trace_sched_switch(
+				    probe_wakeup_latency_hist_stop, NULL);
+				unregister_trace_sched_migrate_task(
+				    probe_sched_migrate_task, NULL);
+
+				for_each_online_cpu(cpu) {
+					per_cpu(wakeup_task, cpu) = NULL;
+					per_cpu(wakeup_sharedprio, cpu) = 0;
+				}
+			}
+#ifdef CONFIG_MISSED_TIMER_OFFSETS_HIST
+			timerandwakeup_enabled_data.enabled = 0;
+#endif
+			break;
+#endif
+#ifdef CONFIG_MISSED_TIMER_OFFSETS_HIST
+		case MISSED_TIMER_OFFSETS:
+			unregister_trace_hrtimer_interrupt(
+			    probe_hrtimer_interrupt, NULL);
+#ifdef CONFIG_WAKEUP_LATENCY_HIST
+			timerandwakeup_enabled_data.enabled = 0;
+#endif
+			break;
+#endif
+		default:
+			break;
+		}
+	}
+	ed->enabled = enable;
+	return cnt;
+}
+
+static const struct file_operations latency_hist_reset_fops = {
+	.open = tracing_open_generic,
+	.write = latency_hist_reset,
+};
+
+static const struct file_operations enable_fops = {
+	.open = tracing_open_generic,
+	.read = show_enable,
+	.write = do_enable,
+};
+
+#if defined(CONFIG_WAKEUP_LATENCY_HIST) || \
+	defined(CONFIG_MISSED_TIMER_OFFSETS_HIST)
+static const struct file_operations pid_fops = {
+	.open = tracing_open_generic,
+	.read = show_pid,
+	.write = do_pid,
+};
+
+static const struct file_operations maxlatproc_fops = {
+	.open = tracing_open_generic,
+	.read = show_maxlatproc,
+};
+#endif
+
+#if defined(CONFIG_INTERRUPT_OFF_HIST) || defined(CONFIG_PREEMPT_OFF_HIST)
+static notrace void probe_preemptirqsoff_hist(void *v, int reason,
+	int starthist)
+{
+	int cpu = raw_smp_processor_id();
+	int time_set = 0;
+
+	if (starthist) {
+		cycle_t uninitialized_var(start);
+
+		if (!preempt_count() && !irqs_disabled())
+			return;
+
+#ifdef CONFIG_INTERRUPT_OFF_HIST
+		if ((reason == IRQS_OFF || reason == TRACE_START) &&
+		    !per_cpu(hist_irqsoff_counting, cpu)) {
+			per_cpu(hist_irqsoff_counting, cpu) = 1;
+			start = ftrace_now(cpu);
+			time_set++;
+			per_cpu(hist_irqsoff_start, cpu) = start;
+		}
+#endif
+
+#ifdef CONFIG_PREEMPT_OFF_HIST
+		if ((reason == PREEMPT_OFF || reason == TRACE_START) &&
+		    !per_cpu(hist_preemptoff_counting, cpu)) {
+			per_cpu(hist_preemptoff_counting, cpu) = 1;
+			if (!(time_set++))
+				start = ftrace_now(cpu);
+			per_cpu(hist_preemptoff_start, cpu) = start;
+		}
+#endif
+
+#if defined(CONFIG_INTERRUPT_OFF_HIST) && defined(CONFIG_PREEMPT_OFF_HIST)
+		if (per_cpu(hist_irqsoff_counting, cpu) &&
+		    per_cpu(hist_preemptoff_counting, cpu) &&
+		    !per_cpu(hist_preemptirqsoff_counting, cpu)) {
+			per_cpu(hist_preemptirqsoff_counting, cpu) = 1;
+			if (!time_set)
+				start = ftrace_now(cpu);
+			per_cpu(hist_preemptirqsoff_start, cpu) = start;
+		}
+#endif
+	} else {
+		cycle_t uninitialized_var(stop);
+
+#ifdef CONFIG_INTERRUPT_OFF_HIST
+		if ((reason == IRQS_ON || reason == TRACE_STOP) &&
+		    per_cpu(hist_irqsoff_counting, cpu)) {
+			cycle_t start = per_cpu(hist_irqsoff_start, cpu);
+
+			stop = ftrace_now(cpu);
+			time_set++;
+			if (start) {
+				long latency = ((long) (stop - start)) /
+				    NSECS_PER_USECS;
+
+				latency_hist(IRQSOFF_LATENCY, cpu, latency, 0,
+				    stop, NULL);
+			}
+			per_cpu(hist_irqsoff_counting, cpu) = 0;
+		}
+#endif
+
+#ifdef CONFIG_PREEMPT_OFF_HIST
+		if ((reason == PREEMPT_ON || reason == TRACE_STOP) &&
+		    per_cpu(hist_preemptoff_counting, cpu)) {
+			cycle_t start = per_cpu(hist_preemptoff_start, cpu);
+
+			if (!(time_set++))
+				stop = ftrace_now(cpu);
+			if (start) {
+				long latency = ((long) (stop - start)) /
+				    NSECS_PER_USECS;
+
+				latency_hist(PREEMPTOFF_LATENCY, cpu, latency,
+				    0, stop, NULL);
+			}
+			per_cpu(hist_preemptoff_counting, cpu) = 0;
+		}
+#endif
+
+#if defined(CONFIG_INTERRUPT_OFF_HIST) && defined(CONFIG_PREEMPT_OFF_HIST)
+		if ((!per_cpu(hist_irqsoff_counting, cpu) ||
+		     !per_cpu(hist_preemptoff_counting, cpu)) &&
+		   per_cpu(hist_preemptirqsoff_counting, cpu)) {
+			cycle_t start = per_cpu(hist_preemptirqsoff_start, cpu);
+
+			if (!time_set)
+				stop = ftrace_now(cpu);
+			if (start) {
+				long latency = ((long) (stop - start)) /
+				    NSECS_PER_USECS;
+
+				latency_hist(PREEMPTIRQSOFF_LATENCY, cpu,
+				    latency, 0, stop, NULL);
+			}
+			per_cpu(hist_preemptirqsoff_counting, cpu) = 0;
+		}
+#endif
+	}
+}
+#endif
+
+#ifdef CONFIG_WAKEUP_LATENCY_HIST
+static DEFINE_RAW_SPINLOCK(wakeup_lock);
+static notrace void probe_sched_migrate_task(void *v, struct task_struct *task,
+	int cpu)
+{
+	int old_cpu = task_cpu(task);
+
+	if (cpu != old_cpu) {
+		unsigned long flags;
+		struct task_struct *cpu_wakeup_task;
+
+		raw_spin_lock_irqsave(&wakeup_lock, flags);
+
+		cpu_wakeup_task = per_cpu(wakeup_task, old_cpu);
+		if (task == cpu_wakeup_task) {
+			put_task_struct(cpu_wakeup_task);
+			per_cpu(wakeup_task, old_cpu) = NULL;
+			cpu_wakeup_task = per_cpu(wakeup_task, cpu) = task;
+			get_task_struct(cpu_wakeup_task);
+		}
+
+		raw_spin_unlock_irqrestore(&wakeup_lock, flags);
+	}
+}
+
+static notrace void probe_wakeup_latency_hist_start(void *v,
+	struct task_struct *p)
+{
+	unsigned long flags;
+	struct task_struct *curr = current;
+	int cpu = task_cpu(p);
+	struct task_struct *cpu_wakeup_task;
+
+	raw_spin_lock_irqsave(&wakeup_lock, flags);
+
+	cpu_wakeup_task = per_cpu(wakeup_task, cpu);
+
+	if (wakeup_pid) {
+		if ((cpu_wakeup_task && p->prio == cpu_wakeup_task->prio) ||
+		    p->prio == curr->prio)
+			per_cpu(wakeup_sharedprio, cpu) = 1;
+		if (likely(wakeup_pid != task_pid_nr(p)))
+			goto out;
+	} else {
+		if (likely(!rt_task(p)) ||
+		    (cpu_wakeup_task && p->prio > cpu_wakeup_task->prio) ||
+		    p->prio > curr->prio)
+			goto out;
+		if ((cpu_wakeup_task && p->prio == cpu_wakeup_task->prio) ||
+		    p->prio == curr->prio)
+			per_cpu(wakeup_sharedprio, cpu) = 1;
+	}
+
+	if (cpu_wakeup_task)
+		put_task_struct(cpu_wakeup_task);
+	cpu_wakeup_task = per_cpu(wakeup_task, cpu) = p;
+	get_task_struct(cpu_wakeup_task);
+	cpu_wakeup_task->preempt_timestamp_hist =
+		ftrace_now(raw_smp_processor_id());
+out:
+	raw_spin_unlock_irqrestore(&wakeup_lock, flags);
+}
+
+static notrace void probe_wakeup_latency_hist_stop(void *v,
+	bool preempt, struct task_struct *prev, struct task_struct *next)
+{
+	unsigned long flags;
+	int cpu = task_cpu(next);
+	long latency;
+	cycle_t stop;
+	struct task_struct *cpu_wakeup_task;
+
+	raw_spin_lock_irqsave(&wakeup_lock, flags);
+
+	cpu_wakeup_task = per_cpu(wakeup_task, cpu);
+
+	if (cpu_wakeup_task == NULL)
+		goto out;
+
+	/* Already running? */
+	if (unlikely(current == cpu_wakeup_task))
+		goto out_reset;
+
+	if (next != cpu_wakeup_task) {
+		if (next->prio < cpu_wakeup_task->prio)
+			goto out_reset;
+
+		if (next->prio == cpu_wakeup_task->prio)
+			per_cpu(wakeup_sharedprio, cpu) = 1;
+
+		goto out;
+	}
+
+	if (current->prio == cpu_wakeup_task->prio)
+		per_cpu(wakeup_sharedprio, cpu) = 1;
+
+	/*
+	 * The task we are waiting for is about to be switched to.
+	 * Calculate latency and store it in histogram.
+	 */
+	stop = ftrace_now(raw_smp_processor_id());
+
+	latency = ((long) (stop - next->preempt_timestamp_hist)) /
+	    NSECS_PER_USECS;
+
+	if (per_cpu(wakeup_sharedprio, cpu)) {
+		latency_hist(WAKEUP_LATENCY_SHAREDPRIO, cpu, latency, 0, stop,
+		    next);
+		per_cpu(wakeup_sharedprio, cpu) = 0;
+	} else {
+		latency_hist(WAKEUP_LATENCY, cpu, latency, 0, stop, next);
+#ifdef CONFIG_MISSED_TIMER_OFFSETS_HIST
+		if (timerandwakeup_enabled_data.enabled) {
+			latency_hist(TIMERANDWAKEUP_LATENCY, cpu,
+			    next->timer_offset + latency, next->timer_offset,
+			    stop, next);
+		}
+#endif
+	}
+
+out_reset:
+#ifdef CONFIG_MISSED_TIMER_OFFSETS_HIST
+	next->timer_offset = 0;
+#endif
+	put_task_struct(cpu_wakeup_task);
+	per_cpu(wakeup_task, cpu) = NULL;
+out:
+	raw_spin_unlock_irqrestore(&wakeup_lock, flags);
+}
+#endif
+
+#ifdef CONFIG_MISSED_TIMER_OFFSETS_HIST
+static notrace void probe_hrtimer_interrupt(void *v, int cpu,
+	long long latency_ns, struct task_struct *curr,
+	struct task_struct *task)
+{
+	if (latency_ns <= 0 && task != NULL && rt_task(task) &&
+	    (task->prio < curr->prio ||
+	    (task->prio == curr->prio &&
+	    !cpumask_test_cpu(cpu, &task->cpus_allowed)))) {
+		long latency;
+		cycle_t now;
+
+		if (missed_timer_offsets_pid) {
+			if (likely(missed_timer_offsets_pid !=
+			    task_pid_nr(task)))
+				return;
+		}
+
+		now = ftrace_now(cpu);
+		latency = (long) div_s64(-latency_ns, NSECS_PER_USECS);
+		latency_hist(MISSED_TIMER_OFFSETS, cpu, latency, latency, now,
+		    task);
+#ifdef CONFIG_WAKEUP_LATENCY_HIST
+		task->timer_offset = latency;
+#endif
+	}
+}
+#endif
+
+static __init int latency_hist_init(void)
+{
+	struct dentry *latency_hist_root = NULL;
+	struct dentry *dentry;
+#ifdef CONFIG_WAKEUP_LATENCY_HIST
+	struct dentry *dentry_sharedprio;
+#endif
+	struct dentry *entry;
+	struct dentry *enable_root;
+	int i = 0;
+	struct hist_data *my_hist;
+	char name[64];
+	char *cpufmt = "CPU%d";
+#if defined(CONFIG_WAKEUP_LATENCY_HIST) || \
+	defined(CONFIG_MISSED_TIMER_OFFSETS_HIST)
+	char *cpufmt_maxlatproc = "max_latency-CPU%d";
+	struct maxlatproc_data *mp = NULL;
+#endif
+
+	dentry = tracing_init_dentry();
+	latency_hist_root = debugfs_create_dir(latency_hist_dir_root, dentry);
+	enable_root = debugfs_create_dir("enable", latency_hist_root);
+
+#ifdef CONFIG_INTERRUPT_OFF_HIST
+	dentry = debugfs_create_dir(irqsoff_hist_dir, latency_hist_root);
+	for_each_possible_cpu(i) {
+		sprintf(name, cpufmt, i);
+		entry = debugfs_create_file(name, 0444, dentry,
+		    &per_cpu(irqsoff_hist, i), &latency_hist_fops);
+		my_hist = &per_cpu(irqsoff_hist, i);
+		atomic_set(&my_hist->hist_mode, 1);
+		my_hist->min_lat = LONG_MAX;
+	}
+	entry = debugfs_create_file("reset", 0644, dentry,
+	    (void *)IRQSOFF_LATENCY, &latency_hist_reset_fops);
+#endif
+
+#ifdef CONFIG_PREEMPT_OFF_HIST
+	dentry = debugfs_create_dir(preemptoff_hist_dir,
+	    latency_hist_root);
+	for_each_possible_cpu(i) {
+		sprintf(name, cpufmt, i);
+		entry = debugfs_create_file(name, 0444, dentry,
+		    &per_cpu(preemptoff_hist, i), &latency_hist_fops);
+		my_hist = &per_cpu(preemptoff_hist, i);
+		atomic_set(&my_hist->hist_mode, 1);
+		my_hist->min_lat = LONG_MAX;
+	}
+	entry = debugfs_create_file("reset", 0644, dentry,
+	    (void *)PREEMPTOFF_LATENCY, &latency_hist_reset_fops);
+#endif
+
+#if defined(CONFIG_INTERRUPT_OFF_HIST) && defined(CONFIG_PREEMPT_OFF_HIST)
+	dentry = debugfs_create_dir(preemptirqsoff_hist_dir,
+	    latency_hist_root);
+	for_each_possible_cpu(i) {
+		sprintf(name, cpufmt, i);
+		entry = debugfs_create_file(name, 0444, dentry,
+		    &per_cpu(preemptirqsoff_hist, i), &latency_hist_fops);
+		my_hist = &per_cpu(preemptirqsoff_hist, i);
+		atomic_set(&my_hist->hist_mode, 1);
+		my_hist->min_lat = LONG_MAX;
+	}
+	entry = debugfs_create_file("reset", 0644, dentry,
+	    (void *)PREEMPTIRQSOFF_LATENCY, &latency_hist_reset_fops);
+#endif
+
+#if defined(CONFIG_INTERRUPT_OFF_HIST) || defined(CONFIG_PREEMPT_OFF_HIST)
+	entry = debugfs_create_file("preemptirqsoff", 0644,
+	    enable_root, (void *)&preemptirqsoff_enabled_data,
+	    &enable_fops);
+#endif
+
+#ifdef CONFIG_WAKEUP_LATENCY_HIST
+	dentry = debugfs_create_dir(wakeup_latency_hist_dir,
+	    latency_hist_root);
+	dentry_sharedprio = debugfs_create_dir(
+	    wakeup_latency_hist_dir_sharedprio, dentry);
+	for_each_possible_cpu(i) {
+		sprintf(name, cpufmt, i);
+
+		entry = debugfs_create_file(name, 0444, dentry,
+		    &per_cpu(wakeup_latency_hist, i),
+		    &latency_hist_fops);
+		my_hist = &per_cpu(wakeup_latency_hist, i);
+		atomic_set(&my_hist->hist_mode, 1);
+		my_hist->min_lat = LONG_MAX;
+
+		entry = debugfs_create_file(name, 0444, dentry_sharedprio,
+		    &per_cpu(wakeup_latency_hist_sharedprio, i),
+		    &latency_hist_fops);
+		my_hist = &per_cpu(wakeup_latency_hist_sharedprio, i);
+		atomic_set(&my_hist->hist_mode, 1);
+		my_hist->min_lat = LONG_MAX;
+
+		sprintf(name, cpufmt_maxlatproc, i);
+
+		mp = &per_cpu(wakeup_maxlatproc, i);
+		entry = debugfs_create_file(name, 0444, dentry, mp,
+		    &maxlatproc_fops);
+		clear_maxlatprocdata(mp);
+
+		mp = &per_cpu(wakeup_maxlatproc_sharedprio, i);
+		entry = debugfs_create_file(name, 0444, dentry_sharedprio, mp,
+		    &maxlatproc_fops);
+		clear_maxlatprocdata(mp);
+	}
+	entry = debugfs_create_file("pid", 0644, dentry,
+	    (void *)&wakeup_pid, &pid_fops);
+	entry = debugfs_create_file("reset", 0644, dentry,
+	    (void *)WAKEUP_LATENCY, &latency_hist_reset_fops);
+	entry = debugfs_create_file("reset", 0644, dentry_sharedprio,
+	    (void *)WAKEUP_LATENCY_SHAREDPRIO, &latency_hist_reset_fops);
+	entry = debugfs_create_file("wakeup", 0644,
+	    enable_root, (void *)&wakeup_latency_enabled_data,
+	    &enable_fops);
+#endif
+
+#ifdef CONFIG_MISSED_TIMER_OFFSETS_HIST
+	dentry = debugfs_create_dir(missed_timer_offsets_dir,
+	    latency_hist_root);
+	for_each_possible_cpu(i) {
+		sprintf(name, cpufmt, i);
+		entry = debugfs_create_file(name, 0444, dentry,
+		    &per_cpu(missed_timer_offsets, i), &latency_hist_fops);
+		my_hist = &per_cpu(missed_timer_offsets, i);
+		atomic_set(&my_hist->hist_mode, 1);
+		my_hist->min_lat = LONG_MAX;
+
+		sprintf(name, cpufmt_maxlatproc, i);
+		mp = &per_cpu(missed_timer_offsets_maxlatproc, i);
+		entry = debugfs_create_file(name, 0444, dentry, mp,
+		    &maxlatproc_fops);
+		clear_maxlatprocdata(mp);
+	}
+	entry = debugfs_create_file("pid", 0644, dentry,
+	    (void *)&missed_timer_offsets_pid, &pid_fops);
+	entry = debugfs_create_file("reset", 0644, dentry,
+	    (void *)MISSED_TIMER_OFFSETS, &latency_hist_reset_fops);
+	entry = debugfs_create_file("missed_timer_offsets", 0644,
+	    enable_root, (void *)&missed_timer_offsets_enabled_data,
+	    &enable_fops);
+#endif
+
+#if defined(CONFIG_WAKEUP_LATENCY_HIST) && \
+	defined(CONFIG_MISSED_TIMER_OFFSETS_HIST)
+	dentry = debugfs_create_dir(timerandwakeup_latency_hist_dir,
+	    latency_hist_root);
+	for_each_possible_cpu(i) {
+		sprintf(name, cpufmt, i);
+		entry = debugfs_create_file(name, 0444, dentry,
+		    &per_cpu(timerandwakeup_latency_hist, i),
+		    &latency_hist_fops);
+		my_hist = &per_cpu(timerandwakeup_latency_hist, i);
+		atomic_set(&my_hist->hist_mode, 1);
+		my_hist->min_lat = LONG_MAX;
+
+		sprintf(name, cpufmt_maxlatproc, i);
+		mp = &per_cpu(timerandwakeup_maxlatproc, i);
+		entry = debugfs_create_file(name, 0444, dentry, mp,
+		    &maxlatproc_fops);
+		clear_maxlatprocdata(mp);
+	}
+	entry = debugfs_create_file("reset", 0644, dentry,
+	    (void *)TIMERANDWAKEUP_LATENCY, &latency_hist_reset_fops);
+	entry = debugfs_create_file("timerandwakeup", 0644,
+	    enable_root, (void *)&timerandwakeup_enabled_data,
+	    &enable_fops);
+#endif
+	return 0;
+}
+
+device_initcall(latency_hist_init);
diff --git a/kernel/trace/trace.c b/kernel/trace/trace.c
index 87fb9801bd9e..a652e28fa870 100644
--- a/kernel/trace/trace.c
+++ b/kernel/trace/trace.c
@@ -1652,6 +1652,7 @@ tracing_generic_entry_update(struct trace_entry *entry, unsigned long flags,
 	struct task_struct *tsk = current;
 
 	entry->preempt_count		= pc & 0xff;
+	entry->preempt_lazy_count	= preempt_lazy_count();
 	entry->pid			= (tsk) ? tsk->pid : 0;
 	entry->flags =
 #ifdef CONFIG_TRACE_IRQFLAGS_SUPPORT
@@ -1661,8 +1662,11 @@ tracing_generic_entry_update(struct trace_entry *entry, unsigned long flags,
 #endif
 		((pc & HARDIRQ_MASK) ? TRACE_FLAG_HARDIRQ : 0) |
 		((pc & SOFTIRQ_MASK) ? TRACE_FLAG_SOFTIRQ : 0) |
-		(tif_need_resched() ? TRACE_FLAG_NEED_RESCHED : 0) |
+		(tif_need_resched_now() ? TRACE_FLAG_NEED_RESCHED : 0) |
+		(need_resched_lazy() ? TRACE_FLAG_NEED_RESCHED_LAZY : 0) |
 		(test_preempt_need_resched() ? TRACE_FLAG_PREEMPT_RESCHED : 0);
+
+	entry->migrate_disable = (tsk) ? __migrate_disabled(tsk) & 0xFF : 0;
 }
 EXPORT_SYMBOL_GPL(tracing_generic_entry_update);
 
@@ -2555,14 +2559,17 @@ get_total_entries(struct trace_buffer *buf,
 
 static void print_lat_help_header(struct seq_file *m)
 {
-	seq_puts(m, "#                  _------=> CPU#            \n"
-		    "#                 / _-----=> irqs-off        \n"
-		    "#                | / _----=> need-resched    \n"
-		    "#                || / _---=> hardirq/softirq \n"
-		    "#                ||| / _--=> preempt-depth   \n"
-		    "#                |||| /     delay            \n"
-		    "#  cmd     pid   ||||| time  |   caller      \n"
-		    "#     \\   /      |||||  \\    |   /         \n");
+	seq_puts(m, "#                   _--------=> CPU#              \n"
+		    "#                  / _-------=> irqs-off          \n"
+		    "#                 | / _------=> need-resched      \n"
+		    "#                 || / _-----=> need-resched_lazy \n"
+		    "#                 ||| / _----=> hardirq/softirq   \n"
+		    "#                 |||| / _---=> preempt-depth     \n"
+		    "#                 ||||| / _--=> preempt-lazy-depth\n"
+		    "#                 |||||| / _-=> migrate-disable   \n"
+		    "#                 ||||||| /     delay             \n"
+		    "#  cmd     pid    |||||||| time  |   caller       \n"
+		    "#     \\   /      ||||||||  \\   |   /            \n");
 }
 
 static void print_event_info(struct trace_buffer *buf, struct seq_file *m)
@@ -2588,11 +2595,14 @@ static void print_func_help_header_irq(struct trace_buffer *buf, struct seq_file
 	print_event_info(buf, m);
 	seq_puts(m, "#                              _-----=> irqs-off\n"
 		    "#                             / _----=> need-resched\n"
-		    "#                            | / _---=> hardirq/softirq\n"
-		    "#                            || / _--=> preempt-depth\n"
-		    "#                            ||| /     delay\n"
-		    "#           TASK-PID   CPU#  ||||    TIMESTAMP  FUNCTION\n"
-		    "#              | |       |   ||||       |         |\n");
+		    "#                            |/  _-----=> need-resched_lazy\n"
+		    "#                            || / _---=> hardirq/softirq\n"
+		    "#                            ||| / _--=> preempt-depth\n"
+		    "#                            |||| /_--=> preempt-lazy-depth\n"
+		    "#                            |||||  _-=> migrate-disable   \n"
+		    "#                            ||||| /    delay\n"
+		    "#           TASK-PID   CPU#  ||||||    TIMESTAMP  FUNCTION\n"
+		    "#              | |       |   ||||||       |         |\n");
 }
 
 void
diff --git a/kernel/trace/trace.h b/kernel/trace/trace.h
index 919d9d07686f..3bf86ece683c 100644
--- a/kernel/trace/trace.h
+++ b/kernel/trace/trace.h
@@ -117,6 +117,7 @@ struct kretprobe_trace_entry_head {
  *  NEED_RESCHED	- reschedule is requested
  *  HARDIRQ		- inside an interrupt handler
  *  SOFTIRQ		- inside a softirq handler
+ *  NEED_RESCHED_LAZY	- lazy reschedule is requested
  */
 enum trace_flag_type {
 	TRACE_FLAG_IRQS_OFF		= 0x01,
@@ -125,6 +126,7 @@ enum trace_flag_type {
 	TRACE_FLAG_HARDIRQ		= 0x08,
 	TRACE_FLAG_SOFTIRQ		= 0x10,
 	TRACE_FLAG_PREEMPT_RESCHED	= 0x20,
+	TRACE_FLAG_NEED_RESCHED_LAZY    = 0x40,
 };
 
 #define TRACE_BUF_SIZE		1024
diff --git a/kernel/trace/trace_events.c b/kernel/trace/trace_events.c
index 4f6ef6912e00..878cf6525a0d 100644
--- a/kernel/trace/trace_events.c
+++ b/kernel/trace/trace_events.c
@@ -186,6 +186,8 @@ static int trace_define_common_fields(void)
 	__common_field(unsigned char, flags);
 	__common_field(unsigned char, preempt_count);
 	__common_field(int, pid);
+	__common_field(unsigned short, migrate_disable);
+	__common_field(unsigned short, padding);
 
 	return ret;
 }
diff --git a/kernel/trace/trace_irqsoff.c b/kernel/trace/trace_irqsoff.c
index e4e56589ec1d..36e584fdf863 100644
--- a/kernel/trace/trace_irqsoff.c
+++ b/kernel/trace/trace_irqsoff.c
@@ -13,6 +13,7 @@
 #include <linux/uaccess.h>
 #include <linux/module.h>
 #include <linux/ftrace.h>
+#include <trace/events/hist.h>
 
 #include "trace.h"
 
@@ -420,11 +421,13 @@ void start_critical_timings(void)
 {
 	if (preempt_trace() || irq_trace())
 		start_critical_timing(CALLER_ADDR0, CALLER_ADDR1);
+	trace_preemptirqsoff_hist(TRACE_START, 1);
 }
 EXPORT_SYMBOL_GPL(start_critical_timings);
 
 void stop_critical_timings(void)
 {
+	trace_preemptirqsoff_hist(TRACE_STOP, 0);
 	if (preempt_trace() || irq_trace())
 		stop_critical_timing(CALLER_ADDR0, CALLER_ADDR1);
 }
@@ -434,6 +437,7 @@ EXPORT_SYMBOL_GPL(stop_critical_timings);
 #ifdef CONFIG_PROVE_LOCKING
 void time_hardirqs_on(unsigned long a0, unsigned long a1)
 {
+	trace_preemptirqsoff_hist(IRQS_ON, 0);
 	if (!preempt_trace() && irq_trace())
 		stop_critical_timing(a0, a1);
 }
@@ -442,6 +446,7 @@ void time_hardirqs_off(unsigned long a0, unsigned long a1)
 {
 	if (!preempt_trace() && irq_trace())
 		start_critical_timing(a0, a1);
+	trace_preemptirqsoff_hist(IRQS_OFF, 1);
 }
 
 #else /* !CONFIG_PROVE_LOCKING */
@@ -467,6 +472,7 @@ inline void print_irqtrace_events(struct task_struct *curr)
  */
 void trace_hardirqs_on(void)
 {
+	trace_preemptirqsoff_hist(IRQS_ON, 0);
 	if (!preempt_trace() && irq_trace())
 		stop_critical_timing(CALLER_ADDR0, CALLER_ADDR1);
 }
@@ -476,11 +482,13 @@ void trace_hardirqs_off(void)
 {
 	if (!preempt_trace() && irq_trace())
 		start_critical_timing(CALLER_ADDR0, CALLER_ADDR1);
+	trace_preemptirqsoff_hist(IRQS_OFF, 1);
 }
 EXPORT_SYMBOL(trace_hardirqs_off);
 
 __visible void trace_hardirqs_on_caller(unsigned long caller_addr)
 {
+	trace_preemptirqsoff_hist(IRQS_ON, 0);
 	if (!preempt_trace() && irq_trace())
 		stop_critical_timing(CALLER_ADDR0, caller_addr);
 }
@@ -490,6 +498,7 @@ __visible void trace_hardirqs_off_caller(unsigned long caller_addr)
 {
 	if (!preempt_trace() && irq_trace())
 		start_critical_timing(CALLER_ADDR0, caller_addr);
+	trace_preemptirqsoff_hist(IRQS_OFF, 1);
 }
 EXPORT_SYMBOL(trace_hardirqs_off_caller);
 
@@ -499,12 +508,14 @@ EXPORT_SYMBOL(trace_hardirqs_off_caller);
 #ifdef CONFIG_PREEMPT_TRACER
 void trace_preempt_on(unsigned long a0, unsigned long a1)
 {
+	trace_preemptirqsoff_hist(PREEMPT_ON, 0);
 	if (preempt_trace() && !irq_trace())
 		stop_critical_timing(a0, a1);
 }
 
 void trace_preempt_off(unsigned long a0, unsigned long a1)
 {
+	trace_preemptirqsoff_hist(PREEMPT_ON, 1);
 	if (preempt_trace() && !irq_trace())
 		start_critical_timing(a0, a1);
 }
diff --git a/kernel/trace/trace_output.c b/kernel/trace/trace_output.c
index 282982195e09..9f19d839a756 100644
--- a/kernel/trace/trace_output.c
+++ b/kernel/trace/trace_output.c
@@ -386,6 +386,7 @@ int trace_print_lat_fmt(struct trace_seq *s, struct trace_entry *entry)
 {
 	char hardsoft_irq;
 	char need_resched;
+	char need_resched_lazy;
 	char irqs_off;
 	int hardirq;
 	int softirq;
@@ -413,6 +414,8 @@ int trace_print_lat_fmt(struct trace_seq *s, struct trace_entry *entry)
 		need_resched = '.';
 		break;
 	}
+	need_resched_lazy =
+		(entry->flags & TRACE_FLAG_NEED_RESCHED_LAZY) ? 'L' : '.';
 
 	hardsoft_irq =
 		(hardirq && softirq) ? 'H' :
@@ -420,14 +423,25 @@ int trace_print_lat_fmt(struct trace_seq *s, struct trace_entry *entry)
 		softirq ? 's' :
 		'.';
 
-	trace_seq_printf(s, "%c%c%c",
-			 irqs_off, need_resched, hardsoft_irq);
+	trace_seq_printf(s, "%c%c%c%c",
+			 irqs_off, need_resched, need_resched_lazy,
+			 hardsoft_irq);
 
 	if (entry->preempt_count)
 		trace_seq_printf(s, "%x", entry->preempt_count);
 	else
 		trace_seq_putc(s, '.');
 
+	if (entry->preempt_lazy_count)
+		trace_seq_printf(s, "%x", entry->preempt_lazy_count);
+	else
+		trace_seq_putc(s, '.');
+
+	if (entry->migrate_disable)
+		trace_seq_printf(s, "%x", entry->migrate_disable);
+	else
+		trace_seq_putc(s, '.');
+
 	return !trace_seq_has_overflowed(s);
 }
 
diff --git a/kernel/user.c b/kernel/user.c
index b069ccbfb0b0..1a2e88e98b5e 100644
--- a/kernel/user.c
+++ b/kernel/user.c
@@ -161,11 +161,11 @@ void free_uid(struct user_struct *up)
 	if (!up)
 		return;
 
-	local_irq_save(flags);
+	local_irq_save_nort(flags);
 	if (atomic_dec_and_lock(&up->__count, &uidhash_lock))
 		free_user(up, flags);
 	else
-		local_irq_restore(flags);
+		local_irq_restore_nort(flags);
 }
 
 struct user_struct *alloc_uid(kuid_t uid)
diff --git a/kernel/watchdog.c b/kernel/watchdog.c
index 18f34cf75f74..201775b445a4 100644
--- a/kernel/watchdog.c
+++ b/kernel/watchdog.c
@@ -299,6 +299,8 @@ static int is_softlockup(unsigned long touch_ts)
 
 #ifdef CONFIG_HARDLOCKUP_DETECTOR
 
+static DEFINE_RAW_SPINLOCK(watchdog_output_lock);
+
 static struct perf_event_attr wd_hw_attr = {
 	.type		= PERF_TYPE_HARDWARE,
 	.config		= PERF_COUNT_HW_CPU_CYCLES,
@@ -333,6 +335,13 @@ static void watchdog_overflow_callback(struct perf_event *event,
 		/* only print hardlockups once */
 		if (__this_cpu_read(hard_watchdog_warn) == true)
 			return;
+		/*
+		 * If early-printk is enabled then make sure we do not
+		 * lock up in printk() and kill console logging:
+		 */
+		printk_kill();
+
+		raw_spin_lock(&watchdog_output_lock);
 
 		pr_emerg("Watchdog detected hard LOCKUP on cpu %d", this_cpu);
 		print_modules();
@@ -350,6 +359,7 @@ static void watchdog_overflow_callback(struct perf_event *event,
 				!test_and_set_bit(0, &hardlockup_allcpu_dumped))
 			trigger_allbutself_cpu_backtrace();
 
+		raw_spin_unlock(&watchdog_output_lock);
 		if (hardlockup_panic)
 			panic("Hard LOCKUP");
 
@@ -497,6 +507,7 @@ static void watchdog_enable(unsigned int cpu)
 	/* kick off the timer for the hardlockup detector */
 	hrtimer_init(hrtimer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
 	hrtimer->function = watchdog_timer_fn;
+	hrtimer->irqsafe = 1;
 
 	/* Enable the perf event */
 	watchdog_nmi_enable(cpu);
diff --git a/kernel/workqueue.c b/kernel/workqueue.c
index c579dbab2e36..ca048d21caa8 100644
--- a/kernel/workqueue.c
+++ b/kernel/workqueue.c
@@ -48,6 +48,8 @@
 #include <linux/nodemask.h>
 #include <linux/moduleparam.h>
 #include <linux/uaccess.h>
+#include <linux/locallock.h>
+#include <linux/delay.h>
 
 #include "workqueue_internal.h"
 
@@ -121,11 +123,16 @@ enum {
  *    cpu or grabbing pool->lock is enough for read access.  If
  *    POOL_DISASSOCIATED is set, it's identical to L.
  *
+ *    On RT we need the extra protection via rt_lock_idle_list() for
+ *    the list manipulations against read access from
+ *    wq_worker_sleeping(). All other places are nicely serialized via
+ *    pool->lock.
+ *
  * A: pool->attach_mutex protected.
  *
  * PL: wq_pool_mutex protected.
  *
- * PR: wq_pool_mutex protected for writes.  Sched-RCU protected for reads.
+ * PR: wq_pool_mutex protected for writes.  RCU protected for reads.
  *
  * PW: wq_pool_mutex and wq->mutex protected for writes.  Either for reads.
  *
@@ -134,7 +141,7 @@ enum {
  *
  * WQ: wq->mutex protected.
  *
- * WR: wq->mutex protected for writes.  Sched-RCU protected for reads.
+ * WR: wq->mutex protected for writes.  RCU protected for reads.
  *
  * MD: wq_mayday_lock protected.
  */
@@ -183,7 +190,7 @@ struct worker_pool {
 	atomic_t		nr_running ____cacheline_aligned_in_smp;
 
 	/*
-	 * Destruction of pool is sched-RCU protected to allow dereferences
+	 * Destruction of pool is RCU protected to allow dereferences
 	 * from get_work_pool().
 	 */
 	struct rcu_head		rcu;
@@ -212,7 +219,7 @@ struct pool_workqueue {
 	/*
 	 * Release of unbound pwq is punted to system_wq.  See put_pwq()
 	 * and pwq_unbound_release_workfn() for details.  pool_workqueue
-	 * itself is also sched-RCU protected so that the first pwq can be
+	 * itself is also RCU protected so that the first pwq can be
 	 * determined without grabbing wq->mutex.
 	 */
 	struct work_struct	unbound_release_work;
@@ -331,6 +338,8 @@ EXPORT_SYMBOL_GPL(system_power_efficient_wq);
 struct workqueue_struct *system_freezable_power_efficient_wq __read_mostly;
 EXPORT_SYMBOL_GPL(system_freezable_power_efficient_wq);
 
+static DEFINE_LOCAL_IRQ_LOCK(pendingb_lock);
+
 static int worker_thread(void *__worker);
 static void workqueue_sysfs_unregister(struct workqueue_struct *wq);
 
@@ -338,20 +347,20 @@ static void workqueue_sysfs_unregister(struct workqueue_struct *wq);
 #include <trace/events/workqueue.h>
 
 #define assert_rcu_or_pool_mutex()					\
-	RCU_LOCKDEP_WARN(!rcu_read_lock_sched_held() &&			\
+	RCU_LOCKDEP_WARN(!rcu_read_lock_held() &&			\
 			 !lockdep_is_held(&wq_pool_mutex),		\
-			 "sched RCU or wq_pool_mutex should be held")
+			 "RCU or wq_pool_mutex should be held")
 
 #define assert_rcu_or_wq_mutex(wq)					\
-	RCU_LOCKDEP_WARN(!rcu_read_lock_sched_held() &&			\
+	RCU_LOCKDEP_WARN(!rcu_read_lock_held() &&			\
 			 !lockdep_is_held(&wq->mutex),			\
-			 "sched RCU or wq->mutex should be held")
+			 "RCU or wq->mutex should be held")
 
 #define assert_rcu_or_wq_mutex_or_pool_mutex(wq)			\
-	RCU_LOCKDEP_WARN(!rcu_read_lock_sched_held() &&			\
+	RCU_LOCKDEP_WARN(!rcu_read_lock_held() &&			\
 			 !lockdep_is_held(&wq->mutex) &&		\
 			 !lockdep_is_held(&wq_pool_mutex),		\
-			 "sched RCU, wq->mutex or wq_pool_mutex should be held")
+			 "RCU, wq->mutex or wq_pool_mutex should be held")
 
 #define for_each_cpu_worker_pool(pool, cpu)				\
 	for ((pool) = &per_cpu(cpu_worker_pools, cpu)[0];		\
@@ -363,7 +372,7 @@ static void workqueue_sysfs_unregister(struct workqueue_struct *wq);
  * @pool: iteration cursor
  * @pi: integer used for iteration
  *
- * This must be called either with wq_pool_mutex held or sched RCU read
+ * This must be called either with wq_pool_mutex held or RCU read
  * locked.  If the pool needs to be used beyond the locking in effect, the
  * caller is responsible for guaranteeing that the pool stays online.
  *
@@ -395,7 +404,7 @@ static void workqueue_sysfs_unregister(struct workqueue_struct *wq);
  * @pwq: iteration cursor
  * @wq: the target workqueue
  *
- * This must be called either with wq->mutex held or sched RCU read locked.
+ * This must be called either with wq->mutex held or RCU read locked.
  * If the pwq needs to be used beyond the locking in effect, the caller is
  * responsible for guaranteeing that the pwq stays online.
  *
@@ -407,6 +416,31 @@ static void workqueue_sysfs_unregister(struct workqueue_struct *wq);
 		if (({ assert_rcu_or_wq_mutex(wq); false; })) { }	\
 		else
 
+#ifdef CONFIG_PREEMPT_RT_BASE
+static inline void rt_lock_idle_list(struct worker_pool *pool)
+{
+	preempt_disable();
+}
+static inline void rt_unlock_idle_list(struct worker_pool *pool)
+{
+	preempt_enable();
+}
+static inline void sched_lock_idle_list(struct worker_pool *pool) { }
+static inline void sched_unlock_idle_list(struct worker_pool *pool) { }
+#else
+static inline void rt_lock_idle_list(struct worker_pool *pool) { }
+static inline void rt_unlock_idle_list(struct worker_pool *pool) { }
+static inline void sched_lock_idle_list(struct worker_pool *pool)
+{
+	spin_lock_irq(&pool->lock);
+}
+static inline void sched_unlock_idle_list(struct worker_pool *pool)
+{
+	spin_unlock_irq(&pool->lock);
+}
+#endif
+
+
 #ifdef CONFIG_DEBUG_OBJECTS_WORK
 
 static struct debug_obj_descr work_debug_descr;
@@ -557,7 +591,7 @@ static int worker_pool_assign_id(struct worker_pool *pool)
  * @wq: the target workqueue
  * @node: the node ID
  *
- * This must be called with any of wq_pool_mutex, wq->mutex or sched RCU
+ * This must be called with any of wq_pool_mutex, wq->mutex or RCU
  * read locked.
  * If the pwq needs to be used beyond the locking in effect, the caller is
  * responsible for guaranteeing that the pwq stays online.
@@ -662,8 +696,8 @@ static struct pool_workqueue *get_work_pwq(struct work_struct *work)
  * @work: the work item of interest
  *
  * Pools are created and destroyed under wq_pool_mutex, and allows read
- * access under sched-RCU read lock.  As such, this function should be
- * called under wq_pool_mutex or with preemption disabled.
+ * access under RCU read lock.  As such, this function should be
+ * called under wq_pool_mutex or inside of a rcu_read_lock() region.
  *
  * All fields of the returned pool are accessible as long as the above
  * mentioned locking is in effect.  If the returned pool needs to be used
@@ -800,51 +834,44 @@ static struct worker *first_idle_worker(struct worker_pool *pool)
  */
 static void wake_up_worker(struct worker_pool *pool)
 {
-	struct worker *worker = first_idle_worker(pool);
+	struct worker *worker;
+
+	rt_lock_idle_list(pool);
+
+	worker = first_idle_worker(pool);
 
 	if (likely(worker))
 		wake_up_process(worker->task);
+
+	rt_unlock_idle_list(pool);
 }
 
 /**
- * wq_worker_waking_up - a worker is waking up
- * @task: task waking up
- * @cpu: CPU @task is waking up to
+ * wq_worker_running - a worker is running again
+ * @task: task returning from sleep
  *
- * This function is called during try_to_wake_up() when a worker is
- * being awoken.
- *
- * CONTEXT:
- * spin_lock_irq(rq->lock)
+ * This function is called when a worker returns from schedule()
  */
-void wq_worker_waking_up(struct task_struct *task, int cpu)
+void wq_worker_running(struct task_struct *task)
 {
 	struct worker *worker = kthread_data(task);
 
-	if (!(worker->flags & WORKER_NOT_RUNNING)) {
-		WARN_ON_ONCE(worker->pool->cpu != cpu);
+	if (!worker->sleeping)
+		return;
+	if (!(worker->flags & WORKER_NOT_RUNNING))
 		atomic_inc(&worker->pool->nr_running);
-	}
+	worker->sleeping = 0;
 }
 
 /**
  * wq_worker_sleeping - a worker is going to sleep
  * @task: task going to sleep
- * @cpu: CPU in question, must be the current CPU number
- *
- * This function is called during schedule() when a busy worker is
- * going to sleep.  Worker on the same cpu can be woken up by
- * returning pointer to its task.
- *
- * CONTEXT:
- * spin_lock_irq(rq->lock)
- *
- * Return:
- * Worker task on @cpu to wake up, %NULL if none.
+ * This function is called from schedule() when a busy worker is
+ * going to sleep.
  */
-struct task_struct *wq_worker_sleeping(struct task_struct *task, int cpu)
+void wq_worker_sleeping(struct task_struct *task)
 {
-	struct worker *worker = kthread_data(task), *to_wakeup = NULL;
+	struct worker *worker = kthread_data(task);
 	struct worker_pool *pool;
 
 	/*
@@ -853,29 +880,26 @@ struct task_struct *wq_worker_sleeping(struct task_struct *task, int cpu)
 	 * checking NOT_RUNNING.
 	 */
 	if (worker->flags & WORKER_NOT_RUNNING)
-		return NULL;
+		return;
 
 	pool = worker->pool;
 
-	/* this can only happen on the local cpu */
-	if (WARN_ON_ONCE(cpu != raw_smp_processor_id() || pool->cpu != cpu))
-		return NULL;
+	if (WARN_ON_ONCE(worker->sleeping))
+		return;
+
+	worker->sleeping = 1;
 
 	/*
 	 * The counterpart of the following dec_and_test, implied mb,
 	 * worklist not empty test sequence is in insert_work().
 	 * Please read comment there.
-	 *
-	 * NOT_RUNNING is clear.  This means that we're bound to and
-	 * running on the local cpu w/ rq lock held and preemption
-	 * disabled, which in turn means that none else could be
-	 * manipulating idle_list, so dereferencing idle_list without pool
-	 * lock is safe.
 	 */
 	if (atomic_dec_and_test(&pool->nr_running) &&
-	    !list_empty(&pool->worklist))
-		to_wakeup = first_idle_worker(pool);
-	return to_wakeup ? to_wakeup->task : NULL;
+	    !list_empty(&pool->worklist)) {
+		sched_lock_idle_list(pool);
+		wake_up_worker(pool);
+		sched_unlock_idle_list(pool);
+	}
 }
 
 /**
@@ -1069,12 +1093,12 @@ static void put_pwq_unlocked(struct pool_workqueue *pwq)
 {
 	if (pwq) {
 		/*
-		 * As both pwqs and pools are sched-RCU protected, the
+		 * As both pwqs and pools are RCU protected, the
 		 * following lock operations are safe.
 		 */
-		spin_lock_irq(&pwq->pool->lock);
+		local_spin_lock_irq(pendingb_lock, &pwq->pool->lock);
 		put_pwq(pwq);
-		spin_unlock_irq(&pwq->pool->lock);
+		local_spin_unlock_irq(pendingb_lock, &pwq->pool->lock);
 	}
 }
 
@@ -1176,7 +1200,7 @@ static int try_to_grab_pending(struct work_struct *work, bool is_dwork,
 	struct worker_pool *pool;
 	struct pool_workqueue *pwq;
 
-	local_irq_save(*flags);
+	local_lock_irqsave(pendingb_lock, *flags);
 
 	/* try to steal the timer if it exists */
 	if (is_dwork) {
@@ -1195,6 +1219,7 @@ static int try_to_grab_pending(struct work_struct *work, bool is_dwork,
 	if (!test_and_set_bit(WORK_STRUCT_PENDING_BIT, work_data_bits(work)))
 		return 0;
 
+	rcu_read_lock();
 	/*
 	 * The queueing is in progress, or it is already queued. Try to
 	 * steal it from ->worklist without clearing WORK_STRUCT_PENDING.
@@ -1233,14 +1258,16 @@ static int try_to_grab_pending(struct work_struct *work, bool is_dwork,
 		set_work_pool_and_keep_pending(work, pool->id);
 
 		spin_unlock(&pool->lock);
+		rcu_read_unlock();
 		return 1;
 	}
 	spin_unlock(&pool->lock);
 fail:
-	local_irq_restore(*flags);
+	rcu_read_unlock();
+	local_unlock_irqrestore(pendingb_lock, *flags);
 	if (work_is_canceling(work))
 		return -ENOENT;
-	cpu_relax();
+	cpu_chill();
 	return -EAGAIN;
 }
 
@@ -1309,7 +1336,7 @@ static void __queue_work(int cpu, struct workqueue_struct *wq,
 	 * queued or lose PENDING.  Grabbing PENDING and queueing should
 	 * happen with IRQ disabled.
 	 */
-	WARN_ON_ONCE(!irqs_disabled());
+	WARN_ON_ONCE_NONRT(!irqs_disabled());
 
 	debug_work_activate(work);
 
@@ -1317,6 +1344,8 @@ static void __queue_work(int cpu, struct workqueue_struct *wq,
 	if (unlikely(wq->flags & __WQ_DRAINING) &&
 	    WARN_ON_ONCE(!is_chained_work(wq)))
 		return;
+
+	rcu_read_lock();
 retry:
 	if (req_cpu == WORK_CPU_UNBOUND)
 		cpu = raw_smp_processor_id();
@@ -1373,10 +1402,8 @@ static void __queue_work(int cpu, struct workqueue_struct *wq,
 	/* pwq determined, queue */
 	trace_workqueue_queue_work(req_cpu, pwq, work);
 
-	if (WARN_ON(!list_empty(&work->entry))) {
-		spin_unlock(&pwq->pool->lock);
-		return;
-	}
+	if (WARN_ON(!list_empty(&work->entry)))
+		goto out;
 
 	pwq->nr_in_flight[pwq->work_color]++;
 	work_flags = work_color_to_flags(pwq->work_color);
@@ -1392,7 +1419,9 @@ static void __queue_work(int cpu, struct workqueue_struct *wq,
 
 	insert_work(pwq, work, worklist, work_flags);
 
+out:
 	spin_unlock(&pwq->pool->lock);
+	rcu_read_unlock();
 }
 
 /**
@@ -1412,14 +1441,14 @@ bool queue_work_on(int cpu, struct workqueue_struct *wq,
 	bool ret = false;
 	unsigned long flags;
 
-	local_irq_save(flags);
+	local_lock_irqsave(pendingb_lock,flags);
 
 	if (!test_and_set_bit(WORK_STRUCT_PENDING_BIT, work_data_bits(work))) {
 		__queue_work(cpu, wq, work);
 		ret = true;
 	}
 
-	local_irq_restore(flags);
+	local_unlock_irqrestore(pendingb_lock, flags);
 	return ret;
 }
 EXPORT_SYMBOL(queue_work_on);
@@ -1486,14 +1515,14 @@ bool queue_delayed_work_on(int cpu, struct workqueue_struct *wq,
 	unsigned long flags;
 
 	/* read the comment in __queue_work() */
-	local_irq_save(flags);
+	local_lock_irqsave(pendingb_lock, flags);
 
 	if (!test_and_set_bit(WORK_STRUCT_PENDING_BIT, work_data_bits(work))) {
 		__queue_delayed_work(cpu, wq, dwork, delay);
 		ret = true;
 	}
 
-	local_irq_restore(flags);
+	local_unlock_irqrestore(pendingb_lock, flags);
 	return ret;
 }
 EXPORT_SYMBOL(queue_delayed_work_on);
@@ -1528,7 +1557,7 @@ bool mod_delayed_work_on(int cpu, struct workqueue_struct *wq,
 
 	if (likely(ret >= 0)) {
 		__queue_delayed_work(cpu, wq, dwork, delay);
-		local_irq_restore(flags);
+		local_unlock_irqrestore(pendingb_lock, flags);
 	}
 
 	/* -ENOENT from try_to_grab_pending() becomes %true */
@@ -1561,7 +1590,9 @@ static void worker_enter_idle(struct worker *worker)
 	worker->last_active = jiffies;
 
 	/* idle_list is LIFO */
+	rt_lock_idle_list(pool);
 	list_add(&worker->entry, &pool->idle_list);
+	rt_unlock_idle_list(pool);
 
 	if (too_many_workers(pool) && !timer_pending(&pool->idle_timer))
 		mod_timer(&pool->idle_timer, jiffies + IDLE_WORKER_TIMEOUT);
@@ -1594,7 +1625,9 @@ static void worker_leave_idle(struct worker *worker)
 		return;
 	worker_clr_flags(worker, WORKER_IDLE);
 	pool->nr_idle--;
+	rt_lock_idle_list(pool);
 	list_del_init(&worker->entry);
+	rt_unlock_idle_list(pool);
 }
 
 static struct worker *alloc_worker(int node)
@@ -1760,7 +1793,9 @@ static void destroy_worker(struct worker *worker)
 	pool->nr_workers--;
 	pool->nr_idle--;
 
+	rt_lock_idle_list(pool);
 	list_del_init(&worker->entry);
+	rt_unlock_idle_list(pool);
 	worker->flags |= WORKER_DIE;
 	wake_up_process(worker->task);
 }
@@ -2677,14 +2712,14 @@ static bool start_flush_work(struct work_struct *work, struct wq_barrier *barr)
 
 	might_sleep();
 
-	local_irq_disable();
+	rcu_read_lock();
 	pool = get_work_pool(work);
 	if (!pool) {
-		local_irq_enable();
+		rcu_read_unlock();
 		return false;
 	}
 
-	spin_lock(&pool->lock);
+	spin_lock_irq(&pool->lock);
 	/* see the comment in try_to_grab_pending() with the same code */
 	pwq = get_work_pwq(work);
 	if (pwq) {
@@ -2711,10 +2746,11 @@ static bool start_flush_work(struct work_struct *work, struct wq_barrier *barr)
 	else
 		lock_map_acquire_read(&pwq->wq->lockdep_map);
 	lock_map_release(&pwq->wq->lockdep_map);
-
+	rcu_read_unlock();
 	return true;
 already_gone:
 	spin_unlock_irq(&pool->lock);
+	rcu_read_unlock();
 	return false;
 }
 
@@ -2801,7 +2837,7 @@ static bool __cancel_work_timer(struct work_struct *work, bool is_dwork)
 
 	/* tell other tasks trying to grab @work to back off */
 	mark_work_canceling(work);
-	local_irq_restore(flags);
+	local_unlock_irqrestore(pendingb_lock, flags);
 
 	flush_work(work);
 	clear_work_data(work);
@@ -2856,10 +2892,10 @@ EXPORT_SYMBOL_GPL(cancel_work_sync);
  */
 bool flush_delayed_work(struct delayed_work *dwork)
 {
-	local_irq_disable();
+	local_lock_irq(pendingb_lock);
 	if (del_timer_sync(&dwork->timer))
 		__queue_work(dwork->cpu, dwork->wq, &dwork->work);
-	local_irq_enable();
+	local_unlock_irq(pendingb_lock);
 	return flush_work(&dwork->work);
 }
 EXPORT_SYMBOL(flush_delayed_work);
@@ -2894,7 +2930,7 @@ bool cancel_delayed_work(struct delayed_work *dwork)
 
 	set_work_pool_and_clear_pending(&dwork->work,
 					get_work_pool_id(&dwork->work));
-	local_irq_restore(flags);
+	local_unlock_irqrestore(pendingb_lock, flags);
 	return ret;
 }
 EXPORT_SYMBOL(cancel_delayed_work);
@@ -3122,7 +3158,7 @@ static void rcu_free_pool(struct rcu_head *rcu)
  * put_unbound_pool - put a worker_pool
  * @pool: worker_pool to put
  *
- * Put @pool.  If its refcnt reaches zero, it gets destroyed in sched-RCU
+ * Put @pool.  If its refcnt reaches zero, it gets destroyed in RCU
  * safe manner.  get_unbound_pool() calls this function on its failure path
  * and this function should be able to release pools which went through,
  * successfully or not, init_worker_pool().
@@ -3176,8 +3212,8 @@ static void put_unbound_pool(struct worker_pool *pool)
 	del_timer_sync(&pool->idle_timer);
 	del_timer_sync(&pool->mayday_timer);
 
-	/* sched-RCU protected to allow dereferences from get_work_pool() */
-	call_rcu_sched(&pool->rcu, rcu_free_pool);
+	/* RCU protected to allow dereferences from get_work_pool() */
+	call_rcu(&pool->rcu, rcu_free_pool);
 }
 
 /**
@@ -3284,14 +3320,14 @@ static void pwq_unbound_release_workfn(struct work_struct *work)
 	put_unbound_pool(pool);
 	mutex_unlock(&wq_pool_mutex);
 
-	call_rcu_sched(&pwq->rcu, rcu_free_pwq);
+	call_rcu(&pwq->rcu, rcu_free_pwq);
 
 	/*
 	 * If we're the last pwq going away, @wq is already dead and no one
 	 * is gonna access it anymore.  Schedule RCU free.
 	 */
 	if (is_last)
-		call_rcu_sched(&wq->rcu, rcu_free_wq);
+		call_rcu(&wq->rcu, rcu_free_wq);
 }
 
 /**
@@ -3944,7 +3980,7 @@ void destroy_workqueue(struct workqueue_struct *wq)
 		 * The base ref is never dropped on per-cpu pwqs.  Directly
 		 * schedule RCU free.
 		 */
-		call_rcu_sched(&wq->rcu, rcu_free_wq);
+		call_rcu(&wq->rcu, rcu_free_wq);
 	} else {
 		/*
 		 * We're the sole accessor of @wq at this point.  Directly
@@ -4037,7 +4073,8 @@ bool workqueue_congested(int cpu, struct workqueue_struct *wq)
 	struct pool_workqueue *pwq;
 	bool ret;
 
-	rcu_read_lock_sched();
+	rcu_read_lock();
+	preempt_disable();
 
 	if (cpu == WORK_CPU_UNBOUND)
 		cpu = smp_processor_id();
@@ -4048,7 +4085,8 @@ bool workqueue_congested(int cpu, struct workqueue_struct *wq)
 		pwq = unbound_pwq_by_node(wq, cpu_to_node(cpu));
 
 	ret = !list_empty(&pwq->delayed_works);
-	rcu_read_unlock_sched();
+	preempt_enable();
+	rcu_read_unlock();
 
 	return ret;
 }
@@ -4074,15 +4112,15 @@ unsigned int work_busy(struct work_struct *work)
 	if (work_pending(work))
 		ret |= WORK_BUSY_PENDING;
 
-	local_irq_save(flags);
+	rcu_read_lock();
 	pool = get_work_pool(work);
 	if (pool) {
-		spin_lock(&pool->lock);
+		spin_lock_irqsave(&pool->lock, flags);
 		if (find_worker_executing_work(pool, work))
 			ret |= WORK_BUSY_RUNNING;
-		spin_unlock(&pool->lock);
+		spin_unlock_irqrestore(&pool->lock, flags);
 	}
-	local_irq_restore(flags);
+	rcu_read_unlock();
 
 	return ret;
 }
@@ -4271,7 +4309,7 @@ void show_workqueue_state(void)
 	unsigned long flags;
 	int pi;
 
-	rcu_read_lock_sched();
+	rcu_read_lock();
 
 	pr_info("Showing busy workqueues and worker pools:\n");
 
@@ -4322,7 +4360,7 @@ void show_workqueue_state(void)
 		spin_unlock_irqrestore(&pool->lock, flags);
 	}
 
-	rcu_read_unlock_sched();
+	rcu_read_unlock();
 }
 
 /*
@@ -4672,16 +4710,16 @@ bool freeze_workqueues_busy(void)
 		 * nr_active is monotonically decreasing.  It's safe
 		 * to peek without lock.
 		 */
-		rcu_read_lock_sched();
+		rcu_read_lock();
 		for_each_pwq(pwq, wq) {
 			WARN_ON_ONCE(pwq->nr_active < 0);
 			if (pwq->nr_active) {
 				busy = true;
-				rcu_read_unlock_sched();
+				rcu_read_unlock();
 				goto out_unlock;
 			}
 		}
-		rcu_read_unlock_sched();
+		rcu_read_unlock();
 	}
 out_unlock:
 	mutex_unlock(&wq_pool_mutex);
@@ -4871,7 +4909,8 @@ static ssize_t wq_pool_ids_show(struct device *dev,
 	const char *delim = "";
 	int node, written = 0;
 
-	rcu_read_lock_sched();
+	get_online_cpus();
+	rcu_read_lock();
 	for_each_node(node) {
 		written += scnprintf(buf + written, PAGE_SIZE - written,
 				     "%s%d:%d", delim, node,
@@ -4879,7 +4918,8 @@ static ssize_t wq_pool_ids_show(struct device *dev,
 		delim = " ";
 	}
 	written += scnprintf(buf + written, PAGE_SIZE - written, "\n");
-	rcu_read_unlock_sched();
+	rcu_read_unlock();
+	put_online_cpus();
 
 	return written;
 }
diff --git a/kernel/workqueue_internal.h b/kernel/workqueue_internal.h
index 45215870ac6c..f000c4d6917e 100644
--- a/kernel/workqueue_internal.h
+++ b/kernel/workqueue_internal.h
@@ -43,6 +43,7 @@ struct worker {
 	unsigned long		last_active;	/* L: last active timestamp */
 	unsigned int		flags;		/* X: flags */
 	int			id;		/* I: worker id */
+	int			sleeping;	/* None */
 
 	/*
 	 * Opaque string set with work_set_desc().  Printed out with task
@@ -68,7 +69,7 @@ static inline struct worker *current_wq_worker(void)
  * Scheduler hooks for concurrency managed workqueue.  Only to be used from
  * sched/core.c and workqueue.c.
  */
-void wq_worker_waking_up(struct task_struct *task, int cpu);
-struct task_struct *wq_worker_sleeping(struct task_struct *task, int cpu);
+void wq_worker_running(struct task_struct *task);
+void wq_worker_sleeping(struct task_struct *task);
 
 #endif /* _KERNEL_WORKQUEUE_INTERNAL_H */
diff --git a/lib/Kconfig b/lib/Kconfig
index f0df318104e7..77ff6451ad3f 100644
--- a/lib/Kconfig
+++ b/lib/Kconfig
@@ -395,6 +395,7 @@ config CHECK_SIGNATURE
 
 config CPUMASK_OFFSTACK
 	bool "Force CPU masks off stack" if DEBUG_PER_CPU_MAPS
+	depends on !PREEMPT_RT_FULL
 	help
 	  Use dynamic allocation for cpumask_var_t, instead of putting
 	  them on the stack.  This is a bit more expensive, but avoids
diff --git a/lib/debugobjects.c b/lib/debugobjects.c
index 547f7f923dbc..8fcdbc2fc6d0 100644
--- a/lib/debugobjects.c
+++ b/lib/debugobjects.c
@@ -309,7 +309,10 @@ __debug_object_init(void *addr, struct debug_obj_descr *descr, int onstack)
 	struct debug_obj *obj;
 	unsigned long flags;
 
-	fill_pool();
+#ifdef CONFIG_PREEMPT_RT_FULL
+	if (preempt_count() == 0 && !irqs_disabled())
+#endif
+		fill_pool();
 
 	db = get_bucket((unsigned long) addr);
 
diff --git a/lib/dump_stack.c b/lib/dump_stack.c
index 6745c6230db3..7ccbc6ff80ea 100644
--- a/lib/dump_stack.c
+++ b/lib/dump_stack.c
@@ -33,7 +33,7 @@ asmlinkage __visible void dump_stack(void)
 	 * Permit this cpu to perform nested stack dumps while serialising
 	 * against other CPUs
 	 */
-	preempt_disable();
+	migrate_disable();
 
 retry:
 	cpu = smp_processor_id();
@@ -52,7 +52,7 @@ asmlinkage __visible void dump_stack(void)
 	if (!was_locked)
 		atomic_set(&dump_lock, -1);
 
-	preempt_enable();
+	migrate_enable();
 }
 #else
 asmlinkage __visible void dump_stack(void)
diff --git a/lib/idr.c b/lib/idr.c
index 6098336df267..9decbe914595 100644
--- a/lib/idr.c
+++ b/lib/idr.c
@@ -30,6 +30,7 @@
 #include <linux/idr.h>
 #include <linux/spinlock.h>
 #include <linux/percpu.h>
+#include <linux/locallock.h>
 
 #define MAX_IDR_SHIFT		(sizeof(int) * 8 - 1)
 #define MAX_IDR_BIT		(1U << MAX_IDR_SHIFT)
@@ -45,6 +46,37 @@ static DEFINE_PER_CPU(struct idr_layer *, idr_preload_head);
 static DEFINE_PER_CPU(int, idr_preload_cnt);
 static DEFINE_SPINLOCK(simple_ida_lock);
 
+#ifdef CONFIG_PREEMPT_RT_FULL
+static DEFINE_LOCAL_IRQ_LOCK(idr_lock);
+
+static inline void idr_preload_lock(void)
+{
+	local_lock(idr_lock);
+}
+
+static inline void idr_preload_unlock(void)
+{
+	local_unlock(idr_lock);
+}
+
+void idr_preload_end(void)
+{
+	idr_preload_unlock();
+}
+EXPORT_SYMBOL(idr_preload_end);
+#else
+static inline void idr_preload_lock(void)
+{
+	preempt_disable();
+}
+
+static inline void idr_preload_unlock(void)
+{
+	preempt_enable();
+}
+#endif
+
+
 /* the maximum ID which can be allocated given idr->layers */
 static int idr_max(int layers)
 {
@@ -115,14 +147,14 @@ static struct idr_layer *idr_layer_alloc(gfp_t gfp_mask, struct idr *layer_idr)
 	 * context.  See idr_preload() for details.
 	 */
 	if (!in_interrupt()) {
-		preempt_disable();
+		idr_preload_lock();
 		new = __this_cpu_read(idr_preload_head);
 		if (new) {
 			__this_cpu_write(idr_preload_head, new->ary[0]);
 			__this_cpu_dec(idr_preload_cnt);
 			new->ary[0] = NULL;
 		}
-		preempt_enable();
+		idr_preload_unlock();
 		if (new)
 			return new;
 	}
@@ -366,7 +398,6 @@ static void idr_fill_slot(struct idr *idr, void *ptr, int id,
 	idr_mark_full(pa, id);
 }
 
-
 /**
  * idr_preload - preload for idr_alloc()
  * @gfp_mask: allocation mask to use for preloading
@@ -401,7 +432,7 @@ void idr_preload(gfp_t gfp_mask)
 	WARN_ON_ONCE(in_interrupt());
 	might_sleep_if(gfpflags_allow_blocking(gfp_mask));
 
-	preempt_disable();
+	idr_preload_lock();
 
 	/*
 	 * idr_alloc() is likely to succeed w/o full idr_layer buffer and
@@ -413,9 +444,9 @@ void idr_preload(gfp_t gfp_mask)
 	while (__this_cpu_read(idr_preload_cnt) < MAX_IDR_FREE) {
 		struct idr_layer *new;
 
-		preempt_enable();
+		idr_preload_unlock();
 		new = kmem_cache_zalloc(idr_layer_cache, gfp_mask);
-		preempt_disable();
+		idr_preload_lock();
 		if (!new)
 			break;
 
diff --git a/lib/locking-selftest.c b/lib/locking-selftest.c
index 872a15a2a637..b93a6103fa4d 100644
--- a/lib/locking-selftest.c
+++ b/lib/locking-selftest.c
@@ -590,6 +590,8 @@ GENERATE_TESTCASE(init_held_rsem)
 #include "locking-selftest-spin-hardirq.h"
 GENERATE_PERMUTATIONS_2_EVENTS(irqsafe1_hard_spin)
 
+#ifndef CONFIG_PREEMPT_RT_FULL
+
 #include "locking-selftest-rlock-hardirq.h"
 GENERATE_PERMUTATIONS_2_EVENTS(irqsafe1_hard_rlock)
 
@@ -605,9 +607,12 @@ GENERATE_PERMUTATIONS_2_EVENTS(irqsafe1_soft_rlock)
 #include "locking-selftest-wlock-softirq.h"
 GENERATE_PERMUTATIONS_2_EVENTS(irqsafe1_soft_wlock)
 
+#endif
+
 #undef E1
 #undef E2
 
+#ifndef CONFIG_PREEMPT_RT_FULL
 /*
  * Enabling hardirqs with a softirq-safe lock held:
  */
@@ -640,6 +645,8 @@ GENERATE_PERMUTATIONS_2_EVENTS(irqsafe2A_rlock)
 #undef E1
 #undef E2
 
+#endif
+
 /*
  * Enabling irqs with an irq-safe lock held:
  */
@@ -663,6 +670,8 @@ GENERATE_PERMUTATIONS_2_EVENTS(irqsafe2A_rlock)
 #include "locking-selftest-spin-hardirq.h"
 GENERATE_PERMUTATIONS_2_EVENTS(irqsafe2B_hard_spin)
 
+#ifndef CONFIG_PREEMPT_RT_FULL
+
 #include "locking-selftest-rlock-hardirq.h"
 GENERATE_PERMUTATIONS_2_EVENTS(irqsafe2B_hard_rlock)
 
@@ -678,6 +687,8 @@ GENERATE_PERMUTATIONS_2_EVENTS(irqsafe2B_soft_rlock)
 #include "locking-selftest-wlock-softirq.h"
 GENERATE_PERMUTATIONS_2_EVENTS(irqsafe2B_soft_wlock)
 
+#endif
+
 #undef E1
 #undef E2
 
@@ -709,6 +720,8 @@ GENERATE_PERMUTATIONS_2_EVENTS(irqsafe2B_soft_wlock)
 #include "locking-selftest-spin-hardirq.h"
 GENERATE_PERMUTATIONS_3_EVENTS(irqsafe3_hard_spin)
 
+#ifndef CONFIG_PREEMPT_RT_FULL
+
 #include "locking-selftest-rlock-hardirq.h"
 GENERATE_PERMUTATIONS_3_EVENTS(irqsafe3_hard_rlock)
 
@@ -724,6 +737,8 @@ GENERATE_PERMUTATIONS_3_EVENTS(irqsafe3_soft_rlock)
 #include "locking-selftest-wlock-softirq.h"
 GENERATE_PERMUTATIONS_3_EVENTS(irqsafe3_soft_wlock)
 
+#endif
+
 #undef E1
 #undef E2
 #undef E3
@@ -757,6 +772,8 @@ GENERATE_PERMUTATIONS_3_EVENTS(irqsafe3_soft_wlock)
 #include "locking-selftest-spin-hardirq.h"
 GENERATE_PERMUTATIONS_3_EVENTS(irqsafe4_hard_spin)
 
+#ifndef CONFIG_PREEMPT_RT_FULL
+
 #include "locking-selftest-rlock-hardirq.h"
 GENERATE_PERMUTATIONS_3_EVENTS(irqsafe4_hard_rlock)
 
@@ -772,10 +789,14 @@ GENERATE_PERMUTATIONS_3_EVENTS(irqsafe4_soft_rlock)
 #include "locking-selftest-wlock-softirq.h"
 GENERATE_PERMUTATIONS_3_EVENTS(irqsafe4_soft_wlock)
 
+#endif
+
 #undef E1
 #undef E2
 #undef E3
 
+#ifndef CONFIG_PREEMPT_RT_FULL
+
 /*
  * read-lock / write-lock irq inversion.
  *
@@ -838,6 +859,10 @@ GENERATE_PERMUTATIONS_3_EVENTS(irq_inversion_soft_wlock)
 #undef E2
 #undef E3
 
+#endif
+
+#ifndef CONFIG_PREEMPT_RT_FULL
+
 /*
  * read-lock / write-lock recursion that is actually safe.
  */
@@ -876,6 +901,8 @@ GENERATE_PERMUTATIONS_3_EVENTS(irq_read_recursion_soft)
 #undef E2
 #undef E3
 
+#endif
+
 /*
  * read-lock / write-lock recursion that is unsafe.
  */
@@ -1858,6 +1885,7 @@ void locking_selftest(void)
 
 	printk("  --------------------------------------------------------------------------\n");
 
+#ifndef CONFIG_PREEMPT_RT_FULL
 	/*
 	 * irq-context testcases:
 	 */
@@ -1870,6 +1898,28 @@ void locking_selftest(void)
 
 	DO_TESTCASE_6x2("irq read-recursion", irq_read_recursion);
 //	DO_TESTCASE_6x2B("irq read-recursion #2", irq_read_recursion2);
+#else
+	/* On -rt, we only do hardirq context test for raw spinlock */
+	DO_TESTCASE_1B("hard-irqs-on + irq-safe-A", irqsafe1_hard_spin, 12);
+	DO_TESTCASE_1B("hard-irqs-on + irq-safe-A", irqsafe1_hard_spin, 21);
+
+	DO_TESTCASE_1B("hard-safe-A + irqs-on", irqsafe2B_hard_spin, 12);
+	DO_TESTCASE_1B("hard-safe-A + irqs-on", irqsafe2B_hard_spin, 21);
+
+	DO_TESTCASE_1B("hard-safe-A + unsafe-B #1", irqsafe3_hard_spin, 123);
+	DO_TESTCASE_1B("hard-safe-A + unsafe-B #1", irqsafe3_hard_spin, 132);
+	DO_TESTCASE_1B("hard-safe-A + unsafe-B #1", irqsafe3_hard_spin, 213);
+	DO_TESTCASE_1B("hard-safe-A + unsafe-B #1", irqsafe3_hard_spin, 231);
+	DO_TESTCASE_1B("hard-safe-A + unsafe-B #1", irqsafe3_hard_spin, 312);
+	DO_TESTCASE_1B("hard-safe-A + unsafe-B #1", irqsafe3_hard_spin, 321);
+
+	DO_TESTCASE_1B("hard-safe-A + unsafe-B #2", irqsafe4_hard_spin, 123);
+	DO_TESTCASE_1B("hard-safe-A + unsafe-B #2", irqsafe4_hard_spin, 132);
+	DO_TESTCASE_1B("hard-safe-A + unsafe-B #2", irqsafe4_hard_spin, 213);
+	DO_TESTCASE_1B("hard-safe-A + unsafe-B #2", irqsafe4_hard_spin, 231);
+	DO_TESTCASE_1B("hard-safe-A + unsafe-B #2", irqsafe4_hard_spin, 312);
+	DO_TESTCASE_1B("hard-safe-A + unsafe-B #2", irqsafe4_hard_spin, 321);
+#endif
 
 	ww_tests();
 
diff --git a/lib/percpu_ida.c b/lib/percpu_ida.c
index 6d40944960de..822a2c027e72 100644
--- a/lib/percpu_ida.c
+++ b/lib/percpu_ida.c
@@ -26,6 +26,9 @@
 #include <linux/string.h>
 #include <linux/spinlock.h>
 #include <linux/percpu_ida.h>
+#include <linux/locallock.h>
+
+static DEFINE_LOCAL_IRQ_LOCK(irq_off_lock);
 
 struct percpu_ida_cpu {
 	/*
@@ -148,13 +151,13 @@ int percpu_ida_alloc(struct percpu_ida *pool, int state)
 	unsigned long flags;
 	int tag;
 
-	local_irq_save(flags);
+	local_lock_irqsave(irq_off_lock, flags);
 	tags = this_cpu_ptr(pool->tag_cpu);
 
 	/* Fastpath */
 	tag = alloc_local_tag(tags);
 	if (likely(tag >= 0)) {
-		local_irq_restore(flags);
+		local_unlock_irqrestore(irq_off_lock, flags);
 		return tag;
 	}
 
@@ -173,6 +176,7 @@ int percpu_ida_alloc(struct percpu_ida *pool, int state)
 
 		if (!tags->nr_free)
 			alloc_global_tags(pool, tags);
+
 		if (!tags->nr_free)
 			steal_tags(pool, tags);
 
@@ -184,7 +188,7 @@ int percpu_ida_alloc(struct percpu_ida *pool, int state)
 		}
 
 		spin_unlock(&pool->lock);
-		local_irq_restore(flags);
+		local_unlock_irqrestore(irq_off_lock, flags);
 
 		if (tag >= 0 || state == TASK_RUNNING)
 			break;
@@ -196,7 +200,7 @@ int percpu_ida_alloc(struct percpu_ida *pool, int state)
 
 		schedule();
 
-		local_irq_save(flags);
+		local_lock_irqsave(irq_off_lock, flags);
 		tags = this_cpu_ptr(pool->tag_cpu);
 	}
 	if (state != TASK_RUNNING)
@@ -221,7 +225,7 @@ void percpu_ida_free(struct percpu_ida *pool, unsigned tag)
 
 	BUG_ON(tag >= pool->nr_tags);
 
-	local_irq_save(flags);
+	local_lock_irqsave(irq_off_lock, flags);
 	tags = this_cpu_ptr(pool->tag_cpu);
 
 	spin_lock(&tags->lock);
@@ -253,7 +257,7 @@ void percpu_ida_free(struct percpu_ida *pool, unsigned tag)
 		spin_unlock(&pool->lock);
 	}
 
-	local_irq_restore(flags);
+	local_unlock_irqrestore(irq_off_lock, flags);
 }
 EXPORT_SYMBOL_GPL(percpu_ida_free);
 
@@ -345,7 +349,7 @@ int percpu_ida_for_each_free(struct percpu_ida *pool, percpu_ida_cb fn,
 	struct percpu_ida_cpu *remote;
 	unsigned cpu, i, err = 0;
 
-	local_irq_save(flags);
+	local_lock_irqsave(irq_off_lock, flags);
 	for_each_possible_cpu(cpu) {
 		remote = per_cpu_ptr(pool->tag_cpu, cpu);
 		spin_lock(&remote->lock);
@@ -367,7 +371,7 @@ int percpu_ida_for_each_free(struct percpu_ida *pool, percpu_ida_cb fn,
 	}
 	spin_unlock(&pool->lock);
 out:
-	local_irq_restore(flags);
+	local_unlock_irqrestore(irq_off_lock, flags);
 	return err;
 }
 EXPORT_SYMBOL_GPL(percpu_ida_for_each_free);
diff --git a/lib/radix-tree.c b/lib/radix-tree.c
index fcf5d98574ce..1ee5b26d28ab 100644
--- a/lib/radix-tree.c
+++ b/lib/radix-tree.c
@@ -196,13 +196,14 @@ radix_tree_node_alloc(struct radix_tree_root *root)
 		 * succeed in getting a node here (and never reach
 		 * kmem_cache_alloc)
 		 */
-		rtp = this_cpu_ptr(&radix_tree_preloads);
+		rtp = &get_cpu_var(radix_tree_preloads);
 		if (rtp->nr) {
 			ret = rtp->nodes;
 			rtp->nodes = ret->private_data;
 			ret->private_data = NULL;
 			rtp->nr--;
 		}
+		put_cpu_var(radix_tree_preloads);
 		/*
 		 * Update the allocation stack trace as this is more useful
 		 * for debugging.
@@ -242,6 +243,7 @@ radix_tree_node_free(struct radix_tree_node *node)
 	call_rcu(&node->rcu_head, radix_tree_node_rcu_free);
 }
 
+#ifndef CONFIG_PREEMPT_RT_FULL
 /*
  * Load up this CPU's radix_tree_node buffer with sufficient objects to
  * ensure that the addition of a single element in the tree cannot fail.  On
@@ -310,6 +312,7 @@ int radix_tree_maybe_preload(gfp_t gfp_mask)
 	return 0;
 }
 EXPORT_SYMBOL(radix_tree_maybe_preload);
+#endif
 
 /*
  *	Return the maximum key which can be store into a
diff --git a/lib/rbtree.c b/lib/rbtree.c
index 1356454e36de..d15d6c4327f1 100644
--- a/lib/rbtree.c
+++ b/lib/rbtree.c
@@ -23,6 +23,7 @@
 
 #include <linux/rbtree_augmented.h>
 #include <linux/export.h>
+#include <linux/rcupdate.h>
 
 /*
  * red-black trees properties:  http://en.wikipedia.org/wiki/Rbtree
@@ -590,3 +591,13 @@ struct rb_node *rb_first_postorder(const struct rb_root *root)
 	return rb_left_deepest_node(root->rb_node);
 }
 EXPORT_SYMBOL(rb_first_postorder);
+
+void rb_link_node_rcu(struct rb_node *node, struct rb_node *parent,
+				    struct rb_node **rb_link)
+{
+	node->__rb_parent_color = (unsigned long)parent;
+	node->rb_left = node->rb_right = NULL;
+
+	rcu_assign_pointer(*rb_link, node);
+}
+EXPORT_SYMBOL(rb_link_node_rcu);
diff --git a/lib/scatterlist.c b/lib/scatterlist.c
index bafa9933fa76..ebe3b7edd086 100644
--- a/lib/scatterlist.c
+++ b/lib/scatterlist.c
@@ -620,7 +620,7 @@ void sg_miter_stop(struct sg_mapping_iter *miter)
 			flush_kernel_dcache_page(miter->page);
 
 		if (miter->__flags & SG_MITER_ATOMIC) {
-			WARN_ON_ONCE(preemptible());
+			WARN_ON_ONCE(!pagefault_disabled());
 			kunmap_atomic(miter->addr);
 		} else
 			kunmap(miter->page);
@@ -664,7 +664,7 @@ size_t sg_copy_buffer(struct scatterlist *sgl, unsigned int nents, void *buf,
 	if (!sg_miter_skip(&miter, skip))
 		return false;
 
-	local_irq_save(flags);
+	local_irq_save_nort(flags);
 
 	while (sg_miter_next(&miter) && offset < buflen) {
 		unsigned int len;
@@ -681,7 +681,7 @@ size_t sg_copy_buffer(struct scatterlist *sgl, unsigned int nents, void *buf,
 
 	sg_miter_stop(&miter);
 
-	local_irq_restore(flags);
+	local_irq_restore_nort(flags);
 	return offset;
 }
 EXPORT_SYMBOL(sg_copy_buffer);
diff --git a/lib/smp_processor_id.c b/lib/smp_processor_id.c
index 1afec32de6f2..11fa431046a8 100644
--- a/lib/smp_processor_id.c
+++ b/lib/smp_processor_id.c
@@ -39,8 +39,9 @@ notrace static unsigned int check_preemption_disabled(const char *what1,
 	if (!printk_ratelimit())
 		goto out_enable;
 
-	printk(KERN_ERR "BUG: using %s%s() in preemptible [%08x] code: %s/%d\n",
-		what1, what2, preempt_count() - 1, current->comm, current->pid);
+	printk(KERN_ERR "BUG: using %s%s() in preemptible [%08x %08x] code: %s/%d\n",
+		what1, what2, preempt_count() - 1, __migrate_disabled(current),
+		current->comm, current->pid);
 
 	print_symbol("caller is %s\n", (long)__builtin_return_address(0));
 	dump_stack();
diff --git a/localversion-rt b/localversion-rt
new file mode 100644
index 000000000000..1445cd65885c
--- /dev/null
+++ b/localversion-rt
@@ -0,0 +1 @@
+-rt3
diff --git a/mm/Kconfig b/mm/Kconfig
index 97a4e06b15c0..9614351e68b8 100644
--- a/mm/Kconfig
+++ b/mm/Kconfig
@@ -392,7 +392,7 @@ config NOMMU_INITIAL_TRIM_EXCESS
 
 config TRANSPARENT_HUGEPAGE
 	bool "Transparent Hugepage Support"
-	depends on HAVE_ARCH_TRANSPARENT_HUGEPAGE
+	depends on HAVE_ARCH_TRANSPARENT_HUGEPAGE && !PREEMPT_RT_FULL
 	select COMPACTION
 	help
 	  Transparent Hugepages allows the kernel to use huge pages and
diff --git a/mm/compaction.c b/mm/compaction.c
index de3e1e71cd9f..ba0f146d8cc2 100644
--- a/mm/compaction.c
+++ b/mm/compaction.c
@@ -1443,10 +1443,12 @@ static int compact_zone(struct zone *zone, struct compact_control *cc)
 				cc->migrate_pfn & ~((1UL << cc->order) - 1);
 
 			if (cc->last_migrated_pfn < current_block_start) {
-				cpu = get_cpu();
+				cpu = get_cpu_light();
+				local_lock_irq(swapvec_lock);
 				lru_add_drain_cpu(cpu);
+				local_unlock_irq(swapvec_lock);
 				drain_local_pages(zone);
-				put_cpu();
+				put_cpu_light();
 				/* No more flushing until we migrate again */
 				cc->last_migrated_pfn = 0;
 			}
diff --git a/mm/filemap.c b/mm/filemap.c
index 1bb007624b53..44301361c100 100644
--- a/mm/filemap.c
+++ b/mm/filemap.c
@@ -168,7 +168,9 @@ static void page_cache_tree_delete(struct address_space *mapping,
 	if (!workingset_node_pages(node) &&
 	    list_empty(&node->private_list)) {
 		node->private_data = mapping;
-		list_lru_add(&workingset_shadow_nodes, &node->private_list);
+		local_lock(workingset_shadow_lock);
+		list_lru_add(&__workingset_shadow_nodes, &node->private_list);
+		local_unlock(workingset_shadow_lock);
 	}
 }
 
@@ -597,9 +599,12 @@ static int page_cache_tree_insert(struct address_space *mapping,
 		 * node->private_list is protected by
 		 * mapping->tree_lock.
 		 */
-		if (!list_empty(&node->private_list))
-			list_lru_del(&workingset_shadow_nodes,
+		if (!list_empty(&node->private_list)) {
+			local_lock(workingset_shadow_lock);
+			list_lru_del(&__workingset_shadow_nodes,
 				     &node->private_list);
+			local_unlock(workingset_shadow_lock);
+		}
 	}
 	return 0;
 }
diff --git a/mm/highmem.c b/mm/highmem.c
index 123bcd3ed4f2..16e8cf26d38a 100644
--- a/mm/highmem.c
+++ b/mm/highmem.c
@@ -29,10 +29,11 @@
 #include <linux/kgdb.h>
 #include <asm/tlbflush.h>
 
-
+#ifndef CONFIG_PREEMPT_RT_FULL
 #if defined(CONFIG_HIGHMEM) || defined(CONFIG_X86_32)
 DEFINE_PER_CPU(int, __kmap_atomic_idx);
 #endif
+#endif
 
 /*
  * Virtual_count is not a pure "count".
@@ -107,8 +108,9 @@ static inline wait_queue_head_t *get_pkmap_wait_queue_head(unsigned int color)
 unsigned long totalhigh_pages __read_mostly;
 EXPORT_SYMBOL(totalhigh_pages);
 
-
+#ifndef CONFIG_PREEMPT_RT_FULL
 EXPORT_PER_CPU_SYMBOL(__kmap_atomic_idx);
+#endif
 
 unsigned int nr_free_highpages (void)
 {
diff --git a/mm/memcontrol.c b/mm/memcontrol.c
index fc10620967c7..4a8fac17dbd7 100644
--- a/mm/memcontrol.c
+++ b/mm/memcontrol.c
@@ -67,6 +67,8 @@
 #include <net/sock.h>
 #include <net/ip.h>
 #include <net/tcp_memcontrol.h>
+#include <linux/locallock.h>
+
 #include "slab.h"
 
 #include <asm/uaccess.h>
@@ -87,6 +89,7 @@ int do_swap_account __read_mostly;
 #define do_swap_account		0
 #endif
 
+static DEFINE_LOCAL_IRQ_LOCK(event_lock);
 static const char * const mem_cgroup_stat_names[] = {
 	"cache",
 	"rss",
@@ -1934,14 +1937,17 @@ static void drain_local_stock(struct work_struct *dummy)
  */
 static void refill_stock(struct mem_cgroup *memcg, unsigned int nr_pages)
 {
-	struct memcg_stock_pcp *stock = &get_cpu_var(memcg_stock);
+	struct memcg_stock_pcp *stock;
+	int cpu = get_cpu_light();
+
+	stock = &per_cpu(memcg_stock, cpu);
 
 	if (stock->cached != memcg) { /* reset if necessary */
 		drain_stock(stock);
 		stock->cached = memcg;
 	}
 	stock->nr_pages += nr_pages;
-	put_cpu_var(memcg_stock);
+	put_cpu_light();
 }
 
 /*
@@ -1957,7 +1963,7 @@ static void drain_all_stock(struct mem_cgroup *root_memcg)
 		return;
 	/* Notify other cpus that system-wide "drain" is running */
 	get_online_cpus();
-	curcpu = get_cpu();
+	curcpu = get_cpu_light();
 	for_each_online_cpu(cpu) {
 		struct memcg_stock_pcp *stock = &per_cpu(memcg_stock, cpu);
 		struct mem_cgroup *memcg;
@@ -1974,7 +1980,7 @@ static void drain_all_stock(struct mem_cgroup *root_memcg)
 				schedule_work_on(cpu, &stock->work);
 		}
 	}
-	put_cpu();
+	put_cpu_light();
 	put_online_cpus();
 	mutex_unlock(&percpu_charge_mutex);
 }
@@ -4615,12 +4621,12 @@ static int mem_cgroup_move_account(struct page *page,
 
 	ret = 0;
 
-	local_irq_disable();
+	local_lock_irq(event_lock);
 	mem_cgroup_charge_statistics(to, page, nr_pages);
 	memcg_check_events(to, page);
 	mem_cgroup_charge_statistics(from, page, -nr_pages);
 	memcg_check_events(from, page);
-	local_irq_enable();
+	local_unlock_irq(event_lock);
 out_unlock:
 	unlock_page(page);
 out:
@@ -5373,10 +5379,10 @@ void mem_cgroup_commit_charge(struct page *page, struct mem_cgroup *memcg,
 		VM_BUG_ON_PAGE(!PageTransHuge(page), page);
 	}
 
-	local_irq_disable();
+	local_lock_irq(event_lock);
 	mem_cgroup_charge_statistics(memcg, page, nr_pages);
 	memcg_check_events(memcg, page);
-	local_irq_enable();
+	local_unlock_irq(event_lock);
 
 	if (do_swap_account && PageSwapCache(page)) {
 		swp_entry_t entry = { .val = page_private(page) };
@@ -5432,14 +5438,14 @@ static void uncharge_batch(struct mem_cgroup *memcg, unsigned long pgpgout,
 		memcg_oom_recover(memcg);
 	}
 
-	local_irq_save(flags);
+	local_lock_irqsave(event_lock, flags);
 	__this_cpu_sub(memcg->stat->count[MEM_CGROUP_STAT_RSS], nr_anon);
 	__this_cpu_sub(memcg->stat->count[MEM_CGROUP_STAT_CACHE], nr_file);
 	__this_cpu_sub(memcg->stat->count[MEM_CGROUP_STAT_RSS_HUGE], nr_huge);
 	__this_cpu_add(memcg->stat->events[MEM_CGROUP_EVENTS_PGPGOUT], pgpgout);
 	__this_cpu_add(memcg->stat->nr_page_events, nr_pages);
 	memcg_check_events(memcg, dummy_page);
-	local_irq_restore(flags);
+	local_unlock_irqrestore(event_lock, flags);
 
 	if (!mem_cgroup_is_root(memcg))
 		css_put_many(&memcg->css, nr_pages);
@@ -5631,6 +5637,7 @@ void mem_cgroup_swapout(struct page *page, swp_entry_t entry)
 {
 	struct mem_cgroup *memcg;
 	unsigned short oldid;
+	unsigned long flags;
 
 	VM_BUG_ON_PAGE(PageLRU(page), page);
 	VM_BUG_ON_PAGE(page_count(page), page);
@@ -5659,9 +5666,13 @@ void mem_cgroup_swapout(struct page *page, swp_entry_t entry)
 	 * important here to have the interrupts disabled because it is the
 	 * only synchronisation we have for udpating the per-CPU variables.
 	 */
+	local_lock_irqsave(event_lock, flags);
+#ifndef CONFIG_PREEMPT_RT_BASE
 	VM_BUG_ON(!irqs_disabled());
+#endif
 	mem_cgroup_charge_statistics(memcg, page, -1);
 	memcg_check_events(memcg, page);
+	local_unlock_irqrestore(event_lock, flags);
 }
 
 /**
diff --git a/mm/mmu_context.c b/mm/mmu_context.c
index f802c2d216a7..b1b6f238e42d 100644
--- a/mm/mmu_context.c
+++ b/mm/mmu_context.c
@@ -23,6 +23,7 @@ void use_mm(struct mm_struct *mm)
 	struct task_struct *tsk = current;
 
 	task_lock(tsk);
+	preempt_disable_rt();
 	active_mm = tsk->active_mm;
 	if (active_mm != mm) {
 		atomic_inc(&mm->mm_count);
@@ -30,6 +31,7 @@ void use_mm(struct mm_struct *mm)
 	}
 	tsk->mm = mm;
 	switch_mm(active_mm, mm, tsk);
+	preempt_enable_rt();
 	task_unlock(tsk);
 #ifdef finish_arch_post_lock_switch
 	finish_arch_post_lock_switch();
diff --git a/mm/page_alloc.c b/mm/page_alloc.c
index 9d666df5ef95..d002418fc8cb 100644
--- a/mm/page_alloc.c
+++ b/mm/page_alloc.c
@@ -60,6 +60,7 @@
 #include <linux/page_ext.h>
 #include <linux/hugetlb.h>
 #include <linux/sched/rt.h>
+#include <linux/locallock.h>
 #include <linux/page_owner.h>
 #include <linux/kthread.h>
 
@@ -264,6 +265,18 @@ EXPORT_SYMBOL(nr_node_ids);
 EXPORT_SYMBOL(nr_online_nodes);
 #endif
 
+static DEFINE_LOCAL_IRQ_LOCK(pa_lock);
+
+#ifdef CONFIG_PREEMPT_RT_BASE
+# define cpu_lock_irqsave(cpu, flags)		\
+	local_lock_irqsave_on(pa_lock, flags, cpu)
+# define cpu_unlock_irqrestore(cpu, flags)	\
+	local_unlock_irqrestore_on(pa_lock, flags, cpu)
+#else
+# define cpu_lock_irqsave(cpu, flags)		local_irq_save(flags)
+# define cpu_unlock_irqrestore(cpu, flags)	local_irq_restore(flags)
+#endif
+
 int page_group_by_mobility_disabled __read_mostly;
 
 #ifdef CONFIG_DEFERRED_STRUCT_PAGE_INIT
@@ -764,7 +777,7 @@ static inline int free_pages_check(struct page *page)
 }
 
 /*
- * Frees a number of pages from the PCP lists
+ * Frees a number of pages which have been collected from the pcp lists.
  * Assumes all pages on list are in same zone, and of same order.
  * count is the number of pages to free.
  *
@@ -775,18 +788,53 @@ static inline int free_pages_check(struct page *page)
  * pinned" detection logic.
  */
 static void free_pcppages_bulk(struct zone *zone, int count,
-					struct per_cpu_pages *pcp)
+			       struct list_head *list)
 {
-	int migratetype = 0;
-	int batch_free = 0;
 	int to_free = count;
 	unsigned long nr_scanned;
+	unsigned long flags;
+
+	spin_lock_irqsave(&zone->lock, flags);
 
-	spin_lock(&zone->lock);
 	nr_scanned = zone_page_state(zone, NR_PAGES_SCANNED);
 	if (nr_scanned)
 		__mod_zone_page_state(zone, NR_PAGES_SCANNED, -nr_scanned);
 
+	while (!list_empty(list)) {
+		struct page *page = list_first_entry(list, struct page, lru);
+		int mt;	/* migratetype of the to-be-freed page */
+
+		/* must delete as __free_one_page list manipulates */
+		list_del(&page->lru);
+
+		mt = get_pcppage_migratetype(page);
+		/* MIGRATE_ISOLATE page should not go to pcplists */
+		VM_BUG_ON_PAGE(is_migrate_isolate(mt), page);
+		/* Pageblock could have been isolated meanwhile */
+		if (unlikely(has_isolate_pageblock(zone)))
+			mt = get_pageblock_migratetype(page);
+
+		__free_one_page(page, page_to_pfn(page), zone, 0, mt);
+		trace_mm_page_pcpu_drain(page, 0, mt);
+		to_free--;
+	}
+	WARN_ON(to_free != 0);
+	spin_unlock_irqrestore(&zone->lock, flags);
+}
+
+/*
+ * Moves a number of pages from the PCP lists to free list which
+ * is freed outside of the locked region.
+ *
+ * Assumes all pages on list are in same zone, and of same order.
+ * count is the number of pages to free.
+ */
+static void isolate_pcp_pages(int to_free, struct per_cpu_pages *src,
+			      struct list_head *dst)
+{
+	int migratetype = 0;
+	int batch_free = 0;
+
 	while (to_free) {
 		struct page *page;
 		struct list_head *list;
@@ -802,7 +850,7 @@ static void free_pcppages_bulk(struct zone *zone, int count,
 			batch_free++;
 			if (++migratetype == MIGRATE_PCPTYPES)
 				migratetype = 0;
-			list = &pcp->lists[migratetype];
+			list = &src->lists[migratetype];
 		} while (list_empty(list));
 
 		/* This is the only non-empty list. Free them all. */
@@ -810,24 +858,12 @@ static void free_pcppages_bulk(struct zone *zone, int count,
 			batch_free = to_free;
 
 		do {
-			int mt;	/* migratetype of the to-be-freed page */
-
-			page = list_entry(list->prev, struct page, lru);
-			/* must delete as __free_one_page list manipulates */
+			page = list_last_entry(list, struct page, lru);
 			list_del(&page->lru);
 
-			mt = get_pcppage_migratetype(page);
-			/* MIGRATE_ISOLATE page should not go to pcplists */
-			VM_BUG_ON_PAGE(is_migrate_isolate(mt), page);
-			/* Pageblock could have been isolated meanwhile */
-			if (unlikely(has_isolate_pageblock(zone)))
-				mt = get_pageblock_migratetype(page);
-
-			__free_one_page(page, page_to_pfn(page), zone, 0, mt);
-			trace_mm_page_pcpu_drain(page, 0, mt);
+			list_add(&page->lru, dst);
 		} while (--to_free && --batch_free && !list_empty(list));
 	}
-	spin_unlock(&zone->lock);
 }
 
 static void free_one_page(struct zone *zone,
@@ -836,7 +872,9 @@ static void free_one_page(struct zone *zone,
 				int migratetype)
 {
 	unsigned long nr_scanned;
-	spin_lock(&zone->lock);
+	unsigned long flags;
+
+	spin_lock_irqsave(&zone->lock, flags);
 	nr_scanned = zone_page_state(zone, NR_PAGES_SCANNED);
 	if (nr_scanned)
 		__mod_zone_page_state(zone, NR_PAGES_SCANNED, -nr_scanned);
@@ -846,7 +884,7 @@ static void free_one_page(struct zone *zone,
 		migratetype = get_pfnblock_migratetype(page, pfn);
 	}
 	__free_one_page(page, pfn, zone, order, migratetype);
-	spin_unlock(&zone->lock);
+	spin_unlock_irqrestore(&zone->lock, flags);
 }
 
 static int free_tail_pages_check(struct page *head_page, struct page *page)
@@ -997,10 +1035,10 @@ static void __free_pages_ok(struct page *page, unsigned int order)
 		return;
 
 	migratetype = get_pfnblock_migratetype(page, pfn);
-	local_irq_save(flags);
+	local_lock_irqsave(pa_lock, flags);
 	__count_vm_events(PGFREE, 1 << order);
 	free_one_page(page_zone(page), page, pfn, order, migratetype);
-	local_irq_restore(flags);
+	local_unlock_irqrestore(pa_lock, flags);
 }
 
 static void __init __free_pages_boot_core(struct page *page,
@@ -1857,16 +1895,18 @@ static int rmqueue_bulk(struct zone *zone, unsigned int order,
 void drain_zone_pages(struct zone *zone, struct per_cpu_pages *pcp)
 {
 	unsigned long flags;
+	LIST_HEAD(dst);
 	int to_drain, batch;
 
-	local_irq_save(flags);
+	local_lock_irqsave(pa_lock, flags);
 	batch = READ_ONCE(pcp->batch);
 	to_drain = min(pcp->count, batch);
 	if (to_drain > 0) {
-		free_pcppages_bulk(zone, to_drain, pcp);
+		isolate_pcp_pages(to_drain, pcp, &dst);
 		pcp->count -= to_drain;
 	}
-	local_irq_restore(flags);
+	local_unlock_irqrestore(pa_lock, flags);
+	free_pcppages_bulk(zone, to_drain, &dst);
 }
 #endif
 
@@ -1882,16 +1922,21 @@ static void drain_pages_zone(unsigned int cpu, struct zone *zone)
 	unsigned long flags;
 	struct per_cpu_pageset *pset;
 	struct per_cpu_pages *pcp;
+	LIST_HEAD(dst);
+	int count;
 
-	local_irq_save(flags);
+	cpu_lock_irqsave(cpu, flags);
 	pset = per_cpu_ptr(zone->pageset, cpu);
 
 	pcp = &pset->pcp;
-	if (pcp->count) {
-		free_pcppages_bulk(zone, pcp->count, pcp);
+	count = pcp->count;
+	if (count) {
+		isolate_pcp_pages(count, pcp, &dst);
 		pcp->count = 0;
 	}
-	local_irq_restore(flags);
+	cpu_unlock_irqrestore(cpu, flags);
+	if (count)
+		free_pcppages_bulk(zone, count, &dst);
 }
 
 /*
@@ -1977,8 +2022,17 @@ void drain_all_pages(struct zone *zone)
 		else
 			cpumask_clear_cpu(cpu, &cpus_with_pcps);
 	}
+#ifndef CONFIG_PREEMPT_RT_BASE
 	on_each_cpu_mask(&cpus_with_pcps, (smp_call_func_t) drain_local_pages,
 								zone, 1);
+#else
+	for_each_cpu(cpu, &cpus_with_pcps) {
+		if (zone)
+			drain_pages_zone(cpu, zone);
+		else
+			drain_pages(cpu);
+	}
+#endif
 }
 
 #ifdef CONFIG_HIBERNATION
@@ -2034,7 +2088,7 @@ void free_hot_cold_page(struct page *page, bool cold)
 
 	migratetype = get_pfnblock_migratetype(page, pfn);
 	set_pcppage_migratetype(page, migratetype);
-	local_irq_save(flags);
+	local_lock_irqsave(pa_lock, flags);
 	__count_vm_event(PGFREE);
 
 	/*
@@ -2060,12 +2114,17 @@ void free_hot_cold_page(struct page *page, bool cold)
 	pcp->count++;
 	if (pcp->count >= pcp->high) {
 		unsigned long batch = READ_ONCE(pcp->batch);
-		free_pcppages_bulk(zone, batch, pcp);
+		LIST_HEAD(dst);
+
+		isolate_pcp_pages(batch, pcp, &dst);
 		pcp->count -= batch;
+		local_unlock_irqrestore(pa_lock, flags);
+		free_pcppages_bulk(zone, batch, &dst);
+		return;
 	}
 
 out:
-	local_irq_restore(flags);
+	local_unlock_irqrestore(pa_lock, flags);
 }
 
 /*
@@ -2200,7 +2259,7 @@ struct page *buffered_rmqueue(struct zone *preferred_zone,
 		struct per_cpu_pages *pcp;
 		struct list_head *list;
 
-		local_irq_save(flags);
+		local_lock_irqsave(pa_lock, flags);
 		pcp = &this_cpu_ptr(zone->pageset)->pcp;
 		list = &pcp->lists[migratetype];
 		if (list_empty(list)) {
@@ -2232,7 +2291,7 @@ struct page *buffered_rmqueue(struct zone *preferred_zone,
 			 */
 			WARN_ON_ONCE(order > 1);
 		}
-		spin_lock_irqsave(&zone->lock, flags);
+		local_spin_lock_irqsave(pa_lock, &zone->lock, flags);
 
 		page = NULL;
 		if (alloc_flags & ALLOC_HARDER) {
@@ -2242,11 +2301,13 @@ struct page *buffered_rmqueue(struct zone *preferred_zone,
 		}
 		if (!page)
 			page = __rmqueue(zone, order, migratetype, gfp_flags);
-		spin_unlock(&zone->lock);
-		if (!page)
+		if (!page) {
+			spin_unlock(&zone->lock);
 			goto failed;
+		}
 		__mod_zone_freepage_state(zone, -(1 << order),
 					  get_pcppage_migratetype(page));
+		spin_unlock(&zone->lock);
 	}
 
 	__mod_zone_page_state(zone, NR_ALLOC_BATCH, -(1 << order));
@@ -2256,13 +2317,13 @@ struct page *buffered_rmqueue(struct zone *preferred_zone,
 
 	__count_zone_vm_events(PGALLOC, zone, 1 << order);
 	zone_statistics(preferred_zone, zone, gfp_flags);
-	local_irq_restore(flags);
+	local_unlock_irqrestore(pa_lock, flags);
 
 	VM_BUG_ON_PAGE(bad_range(zone, page), page);
 	return page;
 
 failed:
-	local_irq_restore(flags);
+	local_unlock_irqrestore(pa_lock, flags);
 	return NULL;
 }
 
@@ -5928,6 +5989,7 @@ static int page_alloc_cpu_notify(struct notifier_block *self,
 void __init page_alloc_init(void)
 {
 	hotcpu_notifier(page_alloc_cpu_notify, 0);
+	local_irq_lock_init(pa_lock);
 }
 
 /*
@@ -6822,7 +6884,7 @@ void zone_pcp_reset(struct zone *zone)
 	struct per_cpu_pageset *pset;
 
 	/* avoid races with drain_pages()  */
-	local_irq_save(flags);
+	local_lock_irqsave(pa_lock, flags);
 	if (zone->pageset != &boot_pageset) {
 		for_each_online_cpu(cpu) {
 			pset = per_cpu_ptr(zone->pageset, cpu);
@@ -6831,7 +6893,7 @@ void zone_pcp_reset(struct zone *zone)
 		free_percpu(zone->pageset);
 		zone->pageset = &boot_pageset;
 	}
-	local_irq_restore(flags);
+	local_unlock_irqrestore(pa_lock, flags);
 }
 
 #ifdef CONFIG_MEMORY_HOTREMOVE
diff --git a/mm/slab.h b/mm/slab.h
index 7b6087197997..afdc57941179 100644
--- a/mm/slab.h
+++ b/mm/slab.h
@@ -324,7 +324,11 @@ static inline struct kmem_cache *cache_from_obj(struct kmem_cache *s, void *x)
  * The slab lists for all objects.
  */
 struct kmem_cache_node {
+#ifdef CONFIG_SLUB
+	raw_spinlock_t list_lock;
+#else
 	spinlock_t list_lock;
+#endif
 
 #ifdef CONFIG_SLAB
 	struct list_head slabs_partial;	/* partial list first, better asm code */
diff --git a/mm/slub.c b/mm/slub.c
index 46997517406e..d304d8802e7c 100644
--- a/mm/slub.c
+++ b/mm/slub.c
@@ -1075,7 +1075,7 @@ static noinline struct kmem_cache_node *free_debug_processing(
 	void *object = head;
 	int cnt = 0;
 
-	spin_lock_irqsave(&n->list_lock, *flags);
+	raw_spin_lock_irqsave(&n->list_lock, *flags);
 	slab_lock(page);
 
 	if (!check_slab(s, page))
@@ -1136,7 +1136,7 @@ static noinline struct kmem_cache_node *free_debug_processing(
 
 fail:
 	slab_unlock(page);
-	spin_unlock_irqrestore(&n->list_lock, *flags);
+	raw_spin_unlock_irqrestore(&n->list_lock, *flags);
 	slab_fix(s, "Object at 0x%p not freed", object);
 	return NULL;
 }
@@ -1263,6 +1263,12 @@ static inline void dec_slabs_node(struct kmem_cache *s, int node,
 
 #endif /* CONFIG_SLUB_DEBUG */
 
+struct slub_free_list {
+	raw_spinlock_t		lock;
+	struct list_head	list;
+};
+static DEFINE_PER_CPU(struct slub_free_list, slub_free_list);
+
 /*
  * Hooks for other subsystems that check memory allocations. In a typical
  * production configuration these hooks all should produce no code at all.
@@ -1399,10 +1405,17 @@ static struct page *allocate_slab(struct kmem_cache *s, gfp_t flags, int node)
 	gfp_t alloc_gfp;
 	void *start, *p;
 	int idx, order;
+	bool enableirqs = false;
 
 	flags &= gfp_allowed_mask;
 
 	if (gfpflags_allow_blocking(flags))
+		enableirqs = true;
+#ifdef CONFIG_PREEMPT_RT_FULL
+	if (system_state == SYSTEM_RUNNING)
+		enableirqs = true;
+#endif
+	if (enableirqs)
 		local_irq_enable();
 
 	flags |= s->allocflags;
@@ -1473,7 +1486,7 @@ static struct page *allocate_slab(struct kmem_cache *s, gfp_t flags, int node)
 	page->frozen = 1;
 
 out:
-	if (gfpflags_allow_blocking(flags))
+	if (enableirqs)
 		local_irq_disable();
 	if (!page)
 		return NULL;
@@ -1529,6 +1542,16 @@ static void __free_slab(struct kmem_cache *s, struct page *page)
 	__free_kmem_pages(page, order);
 }
 
+static void free_delayed(struct list_head *h)
+{
+	while(!list_empty(h)) {
+		struct page *page = list_first_entry(h, struct page, lru);
+
+		list_del(&page->lru);
+		__free_slab(page->slab_cache, page);
+	}
+}
+
 #define need_reserve_slab_rcu						\
 	(sizeof(((struct page *)NULL)->lru) < sizeof(struct rcu_head))
 
@@ -1560,6 +1583,12 @@ static void free_slab(struct kmem_cache *s, struct page *page)
 		}
 
 		call_rcu(head, rcu_free_slab);
+	} else if (irqs_disabled()) {
+		struct slub_free_list *f = this_cpu_ptr(&slub_free_list);
+
+		raw_spin_lock(&f->lock);
+		list_add(&page->lru, &f->list);
+		raw_spin_unlock(&f->lock);
 	} else
 		__free_slab(s, page);
 }
@@ -1673,7 +1702,7 @@ static void *get_partial_node(struct kmem_cache *s, struct kmem_cache_node *n,
 	if (!n || !n->nr_partial)
 		return NULL;
 
-	spin_lock(&n->list_lock);
+	raw_spin_lock(&n->list_lock);
 	list_for_each_entry_safe(page, page2, &n->partial, lru) {
 		void *t;
 
@@ -1698,7 +1727,7 @@ static void *get_partial_node(struct kmem_cache *s, struct kmem_cache_node *n,
 			break;
 
 	}
-	spin_unlock(&n->list_lock);
+	raw_spin_unlock(&n->list_lock);
 	return object;
 }
 
@@ -1944,7 +1973,7 @@ static void deactivate_slab(struct kmem_cache *s, struct page *page,
 			 * that acquire_slab() will see a slab page that
 			 * is frozen
 			 */
-			spin_lock(&n->list_lock);
+			raw_spin_lock(&n->list_lock);
 		}
 	} else {
 		m = M_FULL;
@@ -1955,7 +1984,7 @@ static void deactivate_slab(struct kmem_cache *s, struct page *page,
 			 * slabs from diagnostic functions will not see
 			 * any frozen slabs.
 			 */
-			spin_lock(&n->list_lock);
+			raw_spin_lock(&n->list_lock);
 		}
 	}
 
@@ -1990,7 +2019,7 @@ static void deactivate_slab(struct kmem_cache *s, struct page *page,
 		goto redo;
 
 	if (lock)
-		spin_unlock(&n->list_lock);
+		raw_spin_unlock(&n->list_lock);
 
 	if (m == M_FREE) {
 		stat(s, DEACTIVATE_EMPTY);
@@ -2022,10 +2051,10 @@ static void unfreeze_partials(struct kmem_cache *s,
 		n2 = get_node(s, page_to_nid(page));
 		if (n != n2) {
 			if (n)
-				spin_unlock(&n->list_lock);
+				raw_spin_unlock(&n->list_lock);
 
 			n = n2;
-			spin_lock(&n->list_lock);
+			raw_spin_lock(&n->list_lock);
 		}
 
 		do {
@@ -2054,7 +2083,7 @@ static void unfreeze_partials(struct kmem_cache *s,
 	}
 
 	if (n)
-		spin_unlock(&n->list_lock);
+		raw_spin_unlock(&n->list_lock);
 
 	while (discard_page) {
 		page = discard_page;
@@ -2093,14 +2122,21 @@ static void put_cpu_partial(struct kmem_cache *s, struct page *page, int drain)
 			pobjects = oldpage->pobjects;
 			pages = oldpage->pages;
 			if (drain && pobjects > s->cpu_partial) {
+				struct slub_free_list *f;
 				unsigned long flags;
+				LIST_HEAD(tofree);
 				/*
 				 * partial array is full. Move the existing
 				 * set to the per node partial list.
 				 */
 				local_irq_save(flags);
 				unfreeze_partials(s, this_cpu_ptr(s->cpu_slab));
+				f = this_cpu_ptr(&slub_free_list);
+				raw_spin_lock(&f->lock);
+				list_splice_init(&f->list, &tofree);
+				raw_spin_unlock(&f->lock);
 				local_irq_restore(flags);
+				free_delayed(&tofree);
 				oldpage = NULL;
 				pobjects = 0;
 				pages = 0;
@@ -2172,7 +2208,22 @@ static bool has_cpu_slab(int cpu, void *info)
 
 static void flush_all(struct kmem_cache *s)
 {
+	LIST_HEAD(tofree);
+	int cpu;
+
 	on_each_cpu_cond(has_cpu_slab, flush_cpu_slab, s, 1, GFP_ATOMIC);
+	for_each_online_cpu(cpu) {
+		struct slub_free_list *f;
+
+		if (!has_cpu_slab(cpu, s))
+			continue;
+
+		f = &per_cpu(slub_free_list, cpu);
+		raw_spin_lock_irq(&f->lock);
+		list_splice_init(&f->list, &tofree);
+		raw_spin_unlock_irq(&f->lock);
+		free_delayed(&tofree);
+	}
 }
 
 /*
@@ -2208,10 +2259,10 @@ static unsigned long count_partial(struct kmem_cache_node *n,
 	unsigned long x = 0;
 	struct page *page;
 
-	spin_lock_irqsave(&n->list_lock, flags);
+	raw_spin_lock_irqsave(&n->list_lock, flags);
 	list_for_each_entry(page, &n->partial, lru)
 		x += get_count(page);
-	spin_unlock_irqrestore(&n->list_lock, flags);
+	raw_spin_unlock_irqrestore(&n->list_lock, flags);
 	return x;
 }
 #endif /* CONFIG_SLUB_DEBUG || CONFIG_SYSFS */
@@ -2349,8 +2400,10 @@ static inline void *get_freelist(struct kmem_cache *s, struct page *page)
  * already disabled (which is the case for bulk allocation).
  */
 static void *___slab_alloc(struct kmem_cache *s, gfp_t gfpflags, int node,
-			  unsigned long addr, struct kmem_cache_cpu *c)
+			  unsigned long addr, struct kmem_cache_cpu *c,
+			  struct list_head *to_free)
 {
+	struct slub_free_list *f;
 	void *freelist;
 	struct page *page;
 
@@ -2410,6 +2463,13 @@ static void *___slab_alloc(struct kmem_cache *s, gfp_t gfpflags, int node,
 	VM_BUG_ON(!c->page->frozen);
 	c->freelist = get_freepointer(s, freelist);
 	c->tid = next_tid(c->tid);
+
+out:
+	f = this_cpu_ptr(&slub_free_list);
+	raw_spin_lock(&f->lock);
+	list_splice_init(&f->list, to_free);
+	raw_spin_unlock(&f->lock);
+
 	return freelist;
 
 new_slab:
@@ -2441,7 +2501,7 @@ static void *___slab_alloc(struct kmem_cache *s, gfp_t gfpflags, int node,
 	deactivate_slab(s, page, get_freepointer(s, freelist));
 	c->page = NULL;
 	c->freelist = NULL;
-	return freelist;
+	goto out;
 }
 
 /*
@@ -2453,6 +2513,7 @@ static void *__slab_alloc(struct kmem_cache *s, gfp_t gfpflags, int node,
 {
 	void *p;
 	unsigned long flags;
+	LIST_HEAD(tofree);
 
 	local_irq_save(flags);
 #ifdef CONFIG_PREEMPT
@@ -2464,8 +2525,9 @@ static void *__slab_alloc(struct kmem_cache *s, gfp_t gfpflags, int node,
 	c = this_cpu_ptr(s->cpu_slab);
 #endif
 
-	p = ___slab_alloc(s, gfpflags, node, addr, c);
+	p = ___slab_alloc(s, gfpflags, node, addr, c, &tofree);
 	local_irq_restore(flags);
+	free_delayed(&tofree);
 	return p;
 }
 
@@ -2652,7 +2714,7 @@ static void __slab_free(struct kmem_cache *s, struct page *page,
 
 	do {
 		if (unlikely(n)) {
-			spin_unlock_irqrestore(&n->list_lock, flags);
+			raw_spin_unlock_irqrestore(&n->list_lock, flags);
 			n = NULL;
 		}
 		prior = page->freelist;
@@ -2684,7 +2746,7 @@ static void __slab_free(struct kmem_cache *s, struct page *page,
 				 * Otherwise the list_lock will synchronize with
 				 * other processors updating the list of slabs.
 				 */
-				spin_lock_irqsave(&n->list_lock, flags);
+				raw_spin_lock_irqsave(&n->list_lock, flags);
 
 			}
 		}
@@ -2726,7 +2788,7 @@ static void __slab_free(struct kmem_cache *s, struct page *page,
 		add_partial(n, page, DEACTIVATE_TO_TAIL);
 		stat(s, FREE_ADD_PARTIAL);
 	}
-	spin_unlock_irqrestore(&n->list_lock, flags);
+	raw_spin_unlock_irqrestore(&n->list_lock, flags);
 	return;
 
 slab_empty:
@@ -2741,7 +2803,7 @@ static void __slab_free(struct kmem_cache *s, struct page *page,
 		remove_full(s, n, page);
 	}
 
-	spin_unlock_irqrestore(&n->list_lock, flags);
+	raw_spin_unlock_irqrestore(&n->list_lock, flags);
 	stat(s, FREE_SLAB);
 	discard_slab(s, page);
 }
@@ -2913,6 +2975,7 @@ int kmem_cache_alloc_bulk(struct kmem_cache *s, gfp_t flags, size_t size,
 			  void **p)
 {
 	struct kmem_cache_cpu *c;
+	LIST_HEAD(to_free);
 	int i;
 
 	/* memcg and kmem_cache debug support */
@@ -2936,7 +2999,7 @@ int kmem_cache_alloc_bulk(struct kmem_cache *s, gfp_t flags, size_t size,
 			 * of re-populating per CPU c->freelist
 			 */
 			p[i] = ___slab_alloc(s, flags, NUMA_NO_NODE,
-					    _RET_IP_, c);
+					    _RET_IP_, c, &to_free);
 			if (unlikely(!p[i]))
 				goto error;
 
@@ -2948,6 +3011,7 @@ int kmem_cache_alloc_bulk(struct kmem_cache *s, gfp_t flags, size_t size,
 	}
 	c->tid = next_tid(c->tid);
 	local_irq_enable();
+	free_delayed(&to_free);
 
 	/* Clear memory outside IRQ disabled fastpath loop */
 	if (unlikely(flags & __GFP_ZERO)) {
@@ -3095,7 +3159,7 @@ static void
 init_kmem_cache_node(struct kmem_cache_node *n)
 {
 	n->nr_partial = 0;
-	spin_lock_init(&n->list_lock);
+	raw_spin_lock_init(&n->list_lock);
 	INIT_LIST_HEAD(&n->partial);
 #ifdef CONFIG_SLUB_DEBUG
 	atomic_long_set(&n->nr_slabs, 0);
@@ -3677,7 +3741,7 @@ int __kmem_cache_shrink(struct kmem_cache *s, bool deactivate)
 		for (i = 0; i < SHRINK_PROMOTE_MAX; i++)
 			INIT_LIST_HEAD(promote + i);
 
-		spin_lock_irqsave(&n->list_lock, flags);
+		raw_spin_lock_irqsave(&n->list_lock, flags);
 
 		/*
 		 * Build lists of slabs to discard or promote.
@@ -3708,7 +3772,7 @@ int __kmem_cache_shrink(struct kmem_cache *s, bool deactivate)
 		for (i = SHRINK_PROMOTE_MAX - 1; i >= 0; i--)
 			list_splice(promote + i, &n->partial);
 
-		spin_unlock_irqrestore(&n->list_lock, flags);
+		raw_spin_unlock_irqrestore(&n->list_lock, flags);
 
 		/* Release empty slabs */
 		list_for_each_entry_safe(page, t, &discard, lru)
@@ -3884,6 +3948,12 @@ void __init kmem_cache_init(void)
 {
 	static __initdata struct kmem_cache boot_kmem_cache,
 		boot_kmem_cache_node;
+	int cpu;
+
+	for_each_possible_cpu(cpu) {
+		raw_spin_lock_init(&per_cpu(slub_free_list, cpu).lock);
+		INIT_LIST_HEAD(&per_cpu(slub_free_list, cpu).list);
+	}
 
 	if (debug_guardpage_minorder())
 		slub_max_order = 0;
@@ -4127,7 +4197,7 @@ static int validate_slab_node(struct kmem_cache *s,
 	struct page *page;
 	unsigned long flags;
 
-	spin_lock_irqsave(&n->list_lock, flags);
+	raw_spin_lock_irqsave(&n->list_lock, flags);
 
 	list_for_each_entry(page, &n->partial, lru) {
 		validate_slab_slab(s, page, map);
@@ -4149,7 +4219,7 @@ static int validate_slab_node(struct kmem_cache *s,
 		       s->name, count, atomic_long_read(&n->nr_slabs));
 
 out:
-	spin_unlock_irqrestore(&n->list_lock, flags);
+	raw_spin_unlock_irqrestore(&n->list_lock, flags);
 	return count;
 }
 
@@ -4337,12 +4407,12 @@ static int list_locations(struct kmem_cache *s, char *buf,
 		if (!atomic_long_read(&n->nr_slabs))
 			continue;
 
-		spin_lock_irqsave(&n->list_lock, flags);
+		raw_spin_lock_irqsave(&n->list_lock, flags);
 		list_for_each_entry(page, &n->partial, lru)
 			process_slab(&t, s, page, alloc, map);
 		list_for_each_entry(page, &n->full, lru)
 			process_slab(&t, s, page, alloc, map);
-		spin_unlock_irqrestore(&n->list_lock, flags);
+		raw_spin_unlock_irqrestore(&n->list_lock, flags);
 	}
 
 	for (i = 0; i < t.count; i++) {
diff --git a/mm/swap.c b/mm/swap.c
index 39395fb549c0..ca194aeb45d0 100644
--- a/mm/swap.c
+++ b/mm/swap.c
@@ -31,6 +31,7 @@
 #include <linux/memcontrol.h>
 #include <linux/gfp.h>
 #include <linux/uio.h>
+#include <linux/locallock.h>
 #include <linux/hugetlb.h>
 #include <linux/page_idle.h>
 
@@ -46,6 +47,9 @@ static DEFINE_PER_CPU(struct pagevec, lru_add_pvec);
 static DEFINE_PER_CPU(struct pagevec, lru_rotate_pvecs);
 static DEFINE_PER_CPU(struct pagevec, lru_deactivate_file_pvecs);
 
+static DEFINE_LOCAL_IRQ_LOCK(rotate_lock);
+DEFINE_LOCAL_IRQ_LOCK(swapvec_lock);
+
 /*
  * This path almost never happens for VM activity - pages are normally
  * freed via pagevecs.  But it gets used by networking.
@@ -481,11 +485,11 @@ void rotate_reclaimable_page(struct page *page)
 		unsigned long flags;
 
 		page_cache_get(page);
-		local_irq_save(flags);
+		local_lock_irqsave(rotate_lock, flags);
 		pvec = this_cpu_ptr(&lru_rotate_pvecs);
 		if (!pagevec_add(pvec, page))
 			pagevec_move_tail(pvec);
-		local_irq_restore(flags);
+		local_unlock_irqrestore(rotate_lock, flags);
 	}
 }
 
@@ -536,12 +540,13 @@ static bool need_activate_page_drain(int cpu)
 void activate_page(struct page *page)
 {
 	if (PageLRU(page) && !PageActive(page) && !PageUnevictable(page)) {
-		struct pagevec *pvec = &get_cpu_var(activate_page_pvecs);
+		struct pagevec *pvec = &get_locked_var(swapvec_lock,
+						       activate_page_pvecs);
 
 		page_cache_get(page);
 		if (!pagevec_add(pvec, page))
 			pagevec_lru_move_fn(pvec, __activate_page, NULL);
-		put_cpu_var(activate_page_pvecs);
+		put_locked_var(swapvec_lock, activate_page_pvecs);
 	}
 }
 
@@ -567,7 +572,7 @@ void activate_page(struct page *page)
 
 static void __lru_cache_activate_page(struct page *page)
 {
-	struct pagevec *pvec = &get_cpu_var(lru_add_pvec);
+	struct pagevec *pvec = &get_locked_var(swapvec_lock, lru_add_pvec);
 	int i;
 
 	/*
@@ -589,7 +594,7 @@ static void __lru_cache_activate_page(struct page *page)
 		}
 	}
 
-	put_cpu_var(lru_add_pvec);
+	put_locked_var(swapvec_lock, lru_add_pvec);
 }
 
 /*
@@ -630,13 +635,13 @@ EXPORT_SYMBOL(mark_page_accessed);
 
 static void __lru_cache_add(struct page *page)
 {
-	struct pagevec *pvec = &get_cpu_var(lru_add_pvec);
+	struct pagevec *pvec = &get_locked_var(swapvec_lock, lru_add_pvec);
 
 	page_cache_get(page);
 	if (!pagevec_space(pvec))
 		__pagevec_lru_add(pvec);
 	pagevec_add(pvec, page);
-	put_cpu_var(lru_add_pvec);
+	put_locked_var(swapvec_lock, lru_add_pvec);
 }
 
 /**
@@ -816,9 +821,9 @@ void lru_add_drain_cpu(int cpu)
 		unsigned long flags;
 
 		/* No harm done if a racing interrupt already did this */
-		local_irq_save(flags);
+		local_lock_irqsave(rotate_lock, flags);
 		pagevec_move_tail(pvec);
-		local_irq_restore(flags);
+		local_unlock_irqrestore(rotate_lock, flags);
 	}
 
 	pvec = &per_cpu(lru_deactivate_file_pvecs, cpu);
@@ -846,18 +851,19 @@ void deactivate_file_page(struct page *page)
 		return;
 
 	if (likely(get_page_unless_zero(page))) {
-		struct pagevec *pvec = &get_cpu_var(lru_deactivate_file_pvecs);
+		struct pagevec *pvec = &get_locked_var(swapvec_lock,
+						       lru_deactivate_file_pvecs);
 
 		if (!pagevec_add(pvec, page))
 			pagevec_lru_move_fn(pvec, lru_deactivate_file_fn, NULL);
-		put_cpu_var(lru_deactivate_file_pvecs);
+		put_locked_var(swapvec_lock, lru_deactivate_file_pvecs);
 	}
 }
 
 void lru_add_drain(void)
 {
-	lru_add_drain_cpu(get_cpu());
-	put_cpu();
+	lru_add_drain_cpu(local_lock_cpu(swapvec_lock));
+	local_unlock_cpu(swapvec_lock);
 }
 
 static void lru_add_drain_per_cpu(struct work_struct *dummy)
diff --git a/mm/truncate.c b/mm/truncate.c
index 76e35ad97102..5f196420020c 100644
--- a/mm/truncate.c
+++ b/mm/truncate.c
@@ -56,8 +56,11 @@ static void clear_exceptional_entry(struct address_space *mapping,
 	 * protected by mapping->tree_lock.
 	 */
 	if (!workingset_node_shadows(node) &&
-	    !list_empty(&node->private_list))
-		list_lru_del(&workingset_shadow_nodes, &node->private_list);
+	    !list_empty(&node->private_list)) {
+		local_lock(workingset_shadow_lock);
+		list_lru_del(&__workingset_shadow_nodes, &node->private_list);
+		local_unlock(workingset_shadow_lock);
+	}
 	__radix_tree_delete_node(&mapping->page_tree, node);
 unlock:
 	spin_unlock_irq(&mapping->tree_lock);
diff --git a/mm/vmalloc.c b/mm/vmalloc.c
index 8e3c9c5a3042..68740314ad54 100644
--- a/mm/vmalloc.c
+++ b/mm/vmalloc.c
@@ -821,7 +821,7 @@ static void *new_vmap_block(unsigned int order, gfp_t gfp_mask)
 	struct vmap_block *vb;
 	struct vmap_area *va;
 	unsigned long vb_idx;
-	int node, err;
+	int node, err, cpu;
 	void *vaddr;
 
 	node = numa_node_id();
@@ -864,11 +864,12 @@ static void *new_vmap_block(unsigned int order, gfp_t gfp_mask)
 	BUG_ON(err);
 	radix_tree_preload_end();
 
-	vbq = &get_cpu_var(vmap_block_queue);
+	cpu = get_cpu_light();
+	vbq = this_cpu_ptr(&vmap_block_queue);
 	spin_lock(&vbq->lock);
 	list_add_tail_rcu(&vb->free_list, &vbq->free);
 	spin_unlock(&vbq->lock);
-	put_cpu_var(vmap_block_queue);
+	put_cpu_light();
 
 	return vaddr;
 }
@@ -937,6 +938,7 @@ static void *vb_alloc(unsigned long size, gfp_t gfp_mask)
 	struct vmap_block *vb;
 	void *vaddr = NULL;
 	unsigned int order;
+	int cpu;
 
 	BUG_ON(offset_in_page(size));
 	BUG_ON(size > PAGE_SIZE*VMAP_MAX_ALLOC);
@@ -951,7 +953,8 @@ static void *vb_alloc(unsigned long size, gfp_t gfp_mask)
 	order = get_order(size);
 
 	rcu_read_lock();
-	vbq = &get_cpu_var(vmap_block_queue);
+	cpu = get_cpu_light();
+	vbq = this_cpu_ptr(&vmap_block_queue);
 	list_for_each_entry_rcu(vb, &vbq->free, free_list) {
 		unsigned long pages_off;
 
@@ -974,7 +977,7 @@ static void *vb_alloc(unsigned long size, gfp_t gfp_mask)
 		break;
 	}
 
-	put_cpu_var(vmap_block_queue);
+	put_cpu_light();
 	rcu_read_unlock();
 
 	/* Allocate new block if nothing was found */
diff --git a/mm/vmstat.c b/mm/vmstat.c
index c54fd2924f25..64416fd7c209 100644
--- a/mm/vmstat.c
+++ b/mm/vmstat.c
@@ -226,6 +226,7 @@ void __mod_zone_page_state(struct zone *zone, enum zone_stat_item item,
 	long x;
 	long t;
 
+	preempt_disable_rt();
 	x = delta + __this_cpu_read(*p);
 
 	t = __this_cpu_read(pcp->stat_threshold);
@@ -235,6 +236,7 @@ void __mod_zone_page_state(struct zone *zone, enum zone_stat_item item,
 		x = 0;
 	}
 	__this_cpu_write(*p, x);
+	preempt_enable_rt();
 }
 EXPORT_SYMBOL(__mod_zone_page_state);
 
@@ -267,6 +269,7 @@ void __inc_zone_state(struct zone *zone, enum zone_stat_item item)
 	s8 __percpu *p = pcp->vm_stat_diff + item;
 	s8 v, t;
 
+	preempt_disable_rt();
 	v = __this_cpu_inc_return(*p);
 	t = __this_cpu_read(pcp->stat_threshold);
 	if (unlikely(v > t)) {
@@ -275,6 +278,7 @@ void __inc_zone_state(struct zone *zone, enum zone_stat_item item)
 		zone_page_state_add(v + overstep, zone, item);
 		__this_cpu_write(*p, -overstep);
 	}
+	preempt_enable_rt();
 }
 
 void __inc_zone_page_state(struct page *page, enum zone_stat_item item)
@@ -289,6 +293,7 @@ void __dec_zone_state(struct zone *zone, enum zone_stat_item item)
 	s8 __percpu *p = pcp->vm_stat_diff + item;
 	s8 v, t;
 
+	preempt_disable_rt();
 	v = __this_cpu_dec_return(*p);
 	t = __this_cpu_read(pcp->stat_threshold);
 	if (unlikely(v < - t)) {
@@ -297,6 +302,7 @@ void __dec_zone_state(struct zone *zone, enum zone_stat_item item)
 		zone_page_state_add(v - overstep, zone, item);
 		__this_cpu_write(*p, overstep);
 	}
+	preempt_enable_rt();
 }
 
 void __dec_zone_page_state(struct page *page, enum zone_stat_item item)
diff --git a/mm/workingset.c b/mm/workingset.c
index aa017133744b..263d0194734a 100644
--- a/mm/workingset.c
+++ b/mm/workingset.c
@@ -264,7 +264,8 @@ void workingset_activation(struct page *page)
  * point where they would still be useful.
  */
 
-struct list_lru workingset_shadow_nodes;
+struct list_lru __workingset_shadow_nodes;
+DEFINE_LOCAL_IRQ_LOCK(workingset_shadow_lock);
 
 static unsigned long count_shadow_nodes(struct shrinker *shrinker,
 					struct shrink_control *sc)
@@ -274,9 +275,9 @@ static unsigned long count_shadow_nodes(struct shrinker *shrinker,
 	unsigned long pages;
 
 	/* list_lru lock nests inside IRQ-safe mapping->tree_lock */
-	local_irq_disable();
-	shadow_nodes = list_lru_shrink_count(&workingset_shadow_nodes, sc);
-	local_irq_enable();
+	local_lock_irq(workingset_shadow_lock);
+	shadow_nodes = list_lru_shrink_count(&__workingset_shadow_nodes, sc);
+	local_unlock_irq(workingset_shadow_lock);
 
 	pages = node_present_pages(sc->nid);
 	/*
@@ -363,9 +364,9 @@ static enum lru_status shadow_lru_isolate(struct list_head *item,
 	spin_unlock(&mapping->tree_lock);
 	ret = LRU_REMOVED_RETRY;
 out:
-	local_irq_enable();
+	local_unlock_irq(workingset_shadow_lock);
 	cond_resched();
-	local_irq_disable();
+	local_lock_irq(workingset_shadow_lock);
 	spin_lock(lru_lock);
 	return ret;
 }
@@ -376,10 +377,10 @@ static unsigned long scan_shadow_nodes(struct shrinker *shrinker,
 	unsigned long ret;
 
 	/* list_lru lock nests inside IRQ-safe mapping->tree_lock */
-	local_irq_disable();
-	ret =  list_lru_shrink_walk(&workingset_shadow_nodes, sc,
+	local_lock_irq(workingset_shadow_lock);
+	ret =  list_lru_shrink_walk(&__workingset_shadow_nodes, sc,
 				    shadow_lru_isolate, NULL);
-	local_irq_enable();
+	local_unlock_irq(workingset_shadow_lock);
 	return ret;
 }
 
@@ -400,7 +401,7 @@ static int __init workingset_init(void)
 {
 	int ret;
 
-	ret = list_lru_init_key(&workingset_shadow_nodes, &shadow_nodes_key);
+	ret = list_lru_init_key(&__workingset_shadow_nodes, &shadow_nodes_key);
 	if (ret)
 		goto err;
 	ret = register_shrinker(&workingset_shadow_shrinker);
@@ -408,7 +409,7 @@ static int __init workingset_init(void)
 		goto err_list_lru;
 	return 0;
 err_list_lru:
-	list_lru_destroy(&workingset_shadow_nodes);
+	list_lru_destroy(&__workingset_shadow_nodes);
 err:
 	return ret;
 }
diff --git a/net/core/dev.c b/net/core/dev.c
index ae00b894e675..13a55d0df151 100644
--- a/net/core/dev.c
+++ b/net/core/dev.c
@@ -186,6 +186,7 @@ static unsigned int napi_gen_id;
 static DEFINE_HASHTABLE(napi_hash, 8);
 
 static seqcount_t devnet_rename_seq;
+static DEFINE_MUTEX(devnet_rename_mutex);
 
 static inline void dev_base_seq_inc(struct net *net)
 {
@@ -207,14 +208,14 @@ static inline struct hlist_head *dev_index_hash(struct net *net, int ifindex)
 static inline void rps_lock(struct softnet_data *sd)
 {
 #ifdef CONFIG_RPS
-	spin_lock(&sd->input_pkt_queue.lock);
+	raw_spin_lock(&sd->input_pkt_queue.raw_lock);
 #endif
 }
 
 static inline void rps_unlock(struct softnet_data *sd)
 {
 #ifdef CONFIG_RPS
-	spin_unlock(&sd->input_pkt_queue.lock);
+	raw_spin_unlock(&sd->input_pkt_queue.raw_lock);
 #endif
 }
 
@@ -884,7 +885,8 @@ int netdev_get_name(struct net *net, char *name, int ifindex)
 	strcpy(name, dev->name);
 	rcu_read_unlock();
 	if (read_seqcount_retry(&devnet_rename_seq, seq)) {
-		cond_resched();
+		mutex_lock(&devnet_rename_mutex);
+		mutex_unlock(&devnet_rename_mutex);
 		goto retry;
 	}
 
@@ -1153,20 +1155,17 @@ int dev_change_name(struct net_device *dev, const char *newname)
 	if (dev->flags & IFF_UP)
 		return -EBUSY;
 
-	write_seqcount_begin(&devnet_rename_seq);
+	mutex_lock(&devnet_rename_mutex);
+	__raw_write_seqcount_begin(&devnet_rename_seq);
 
-	if (strncmp(newname, dev->name, IFNAMSIZ) == 0) {
-		write_seqcount_end(&devnet_rename_seq);
-		return 0;
-	}
+	if (strncmp(newname, dev->name, IFNAMSIZ) == 0)
+		goto outunlock;
 
 	memcpy(oldname, dev->name, IFNAMSIZ);
 
 	err = dev_get_valid_name(net, dev, newname);
-	if (err < 0) {
-		write_seqcount_end(&devnet_rename_seq);
-		return err;
-	}
+	if (err < 0)
+		goto outunlock;
 
 	if (oldname[0] && !strchr(oldname, '%'))
 		netdev_info(dev, "renamed from %s\n", oldname);
@@ -1179,11 +1178,12 @@ int dev_change_name(struct net_device *dev, const char *newname)
 	if (ret) {
 		memcpy(dev->name, oldname, IFNAMSIZ);
 		dev->name_assign_type = old_assign_type;
-		write_seqcount_end(&devnet_rename_seq);
-		return ret;
+		err = ret;
+		goto outunlock;
 	}
 
-	write_seqcount_end(&devnet_rename_seq);
+	__raw_write_seqcount_end(&devnet_rename_seq);
+	mutex_unlock(&devnet_rename_mutex);
 
 	netdev_adjacent_rename_links(dev, oldname);
 
@@ -1204,7 +1204,8 @@ int dev_change_name(struct net_device *dev, const char *newname)
 		/* err >= 0 after dev_alloc_name() or stores the first errno */
 		if (err >= 0) {
 			err = ret;
-			write_seqcount_begin(&devnet_rename_seq);
+			mutex_lock(&devnet_rename_mutex);
+			__raw_write_seqcount_begin(&devnet_rename_seq);
 			memcpy(dev->name, oldname, IFNAMSIZ);
 			memcpy(oldname, newname, IFNAMSIZ);
 			dev->name_assign_type = old_assign_type;
@@ -1217,6 +1218,11 @@ int dev_change_name(struct net_device *dev, const char *newname)
 	}
 
 	return err;
+
+outunlock:
+	__raw_write_seqcount_end(&devnet_rename_seq);
+	mutex_unlock(&devnet_rename_mutex);
+	return err;
 }
 
 /**
@@ -2246,6 +2252,7 @@ static inline void __netif_reschedule(struct Qdisc *q)
 	sd->output_queue_tailp = &q->next_sched;
 	raise_softirq_irqoff(NET_TX_SOFTIRQ);
 	local_irq_restore(flags);
+	preempt_check_resched_rt();
 }
 
 void __netif_schedule(struct Qdisc *q)
@@ -2327,6 +2334,7 @@ void __dev_kfree_skb_irq(struct sk_buff *skb, enum skb_free_reason reason)
 	__this_cpu_write(softnet_data.completion_queue, skb);
 	raise_softirq_irqoff(NET_TX_SOFTIRQ);
 	local_irq_restore(flags);
+	preempt_check_resched_rt();
 }
 EXPORT_SYMBOL(__dev_kfree_skb_irq);
 
@@ -2938,9 +2946,44 @@ static void skb_update_prio(struct sk_buff *skb)
 #define skb_update_prio(skb)
 #endif
 
+#ifdef CONFIG_PREEMPT_RT_FULL
+
+static inline int xmit_rec_read(void)
+{
+       return current->xmit_recursion;
+}
+
+static inline void xmit_rec_inc(void)
+{
+       current->xmit_recursion++;
+}
+
+static inline void xmit_rec_dec(void)
+{
+       current->xmit_recursion--;
+}
+
+#else
+
 DEFINE_PER_CPU(int, xmit_recursion);
 EXPORT_SYMBOL(xmit_recursion);
 
+static inline int xmit_rec_read(void)
+{
+	return __this_cpu_read(xmit_recursion);
+}
+
+static inline void xmit_rec_inc(void)
+{
+	__this_cpu_inc(xmit_recursion);
+}
+
+static inline int xmit_rec_dec(void)
+{
+	__this_cpu_dec(xmit_recursion);
+}
+#endif
+
 #define RECURSION_LIMIT 10
 
 /**
@@ -3133,7 +3176,7 @@ static int __dev_queue_xmit(struct sk_buff *skb, void *accel_priv)
 
 		if (txq->xmit_lock_owner != cpu) {
 
-			if (__this_cpu_read(xmit_recursion) > RECURSION_LIMIT)
+			if (xmit_rec_read() > RECURSION_LIMIT)
 				goto recursion_alert;
 
 			skb = validate_xmit_skb(skb, dev);
@@ -3143,9 +3186,9 @@ static int __dev_queue_xmit(struct sk_buff *skb, void *accel_priv)
 			HARD_TX_LOCK(dev, txq, cpu);
 
 			if (!netif_xmit_stopped(txq)) {
-				__this_cpu_inc(xmit_recursion);
+				xmit_rec_inc();
 				skb = dev_hard_start_xmit(skb, dev, txq, &rc);
-				__this_cpu_dec(xmit_recursion);
+				xmit_rec_dec();
 				if (dev_xmit_complete(rc)) {
 					HARD_TX_UNLOCK(dev, txq);
 					goto out;
@@ -3519,6 +3562,7 @@ static int enqueue_to_backlog(struct sk_buff *skb, int cpu,
 	rps_unlock(sd);
 
 	local_irq_restore(flags);
+	preempt_check_resched_rt();
 
 	atomic_long_inc(&skb->dev->rx_dropped);
 	kfree_skb(skb);
@@ -3537,7 +3581,7 @@ static int netif_rx_internal(struct sk_buff *skb)
 		struct rps_dev_flow voidflow, *rflow = &voidflow;
 		int cpu;
 
-		preempt_disable();
+		migrate_disable();
 		rcu_read_lock();
 
 		cpu = get_rps_cpu(skb->dev, skb, &rflow);
@@ -3547,13 +3591,13 @@ static int netif_rx_internal(struct sk_buff *skb)
 		ret = enqueue_to_backlog(skb, cpu, &rflow->last_qtail);
 
 		rcu_read_unlock();
-		preempt_enable();
+		migrate_enable();
 	} else
 #endif
 	{
 		unsigned int qtail;
-		ret = enqueue_to_backlog(skb, get_cpu(), &qtail);
-		put_cpu();
+		ret = enqueue_to_backlog(skb, get_cpu_light(), &qtail);
+		put_cpu_light();
 	}
 	return ret;
 }
@@ -3587,16 +3631,44 @@ int netif_rx_ni(struct sk_buff *skb)
 
 	trace_netif_rx_ni_entry(skb);
 
-	preempt_disable();
+	local_bh_disable();
 	err = netif_rx_internal(skb);
-	if (local_softirq_pending())
-		do_softirq();
-	preempt_enable();
+	local_bh_enable();
 
 	return err;
 }
 EXPORT_SYMBOL(netif_rx_ni);
 
+#ifdef CONFIG_PREEMPT_RT_FULL
+/*
+ * RT runs ksoftirqd as a real time thread and the root_lock is a
+ * "sleeping spinlock". If the trylock fails then we can go into an
+ * infinite loop when ksoftirqd preempted the task which actually
+ * holds the lock, because we requeue q and raise NET_TX softirq
+ * causing ksoftirqd to loop forever.
+ *
+ * It's safe to use spin_lock on RT here as softirqs run in thread
+ * context and cannot deadlock against the thread which is holding
+ * root_lock.
+ *
+ * On !RT the trylock might fail, but there we bail out from the
+ * softirq loop after 10 attempts which we can't do on RT. And the
+ * task holding root_lock cannot be preempted, so the only downside of
+ * that trylock is that we need 10 loops to decide that we should have
+ * given up in the first one :)
+ */
+static inline int take_root_lock(spinlock_t *lock)
+{
+	spin_lock(lock);
+	return 1;
+}
+#else
+static inline int take_root_lock(spinlock_t *lock)
+{
+	return spin_trylock(lock);
+}
+#endif
+
 static void net_tx_action(struct softirq_action *h)
 {
 	struct softnet_data *sd = this_cpu_ptr(&softnet_data);
@@ -3638,7 +3710,7 @@ static void net_tx_action(struct softirq_action *h)
 			head = head->next_sched;
 
 			root_lock = qdisc_lock(q);
-			if (spin_trylock(root_lock)) {
+			if (take_root_lock(root_lock)) {
 				smp_mb__before_atomic();
 				clear_bit(__QDISC_STATE_SCHED,
 					  &q->state);
@@ -4044,7 +4116,7 @@ static void flush_backlog(void *arg)
 	skb_queue_walk_safe(&sd->input_pkt_queue, skb, tmp) {
 		if (skb->dev == dev) {
 			__skb_unlink(skb, &sd->input_pkt_queue);
-			kfree_skb(skb);
+			__skb_queue_tail(&sd->tofree_queue, skb);
 			input_queue_head_incr(sd);
 		}
 	}
@@ -4053,10 +4125,13 @@ static void flush_backlog(void *arg)
 	skb_queue_walk_safe(&sd->process_queue, skb, tmp) {
 		if (skb->dev == dev) {
 			__skb_unlink(skb, &sd->process_queue);
-			kfree_skb(skb);
+			__skb_queue_tail(&sd->tofree_queue, skb);
 			input_queue_head_incr(sd);
 		}
 	}
+
+	if (!skb_queue_empty(&sd->tofree_queue))
+		raise_softirq_irqoff(NET_RX_SOFTIRQ);
 }
 
 static int napi_gro_complete(struct sk_buff *skb)
@@ -4507,6 +4582,7 @@ static void net_rps_action_and_irq_enable(struct softnet_data *sd)
 		sd->rps_ipi_list = NULL;
 
 		local_irq_enable();
+		preempt_check_resched_rt();
 
 		/* Send pending IPI's to kick RPS processing on remote cpus. */
 		while (remsd) {
@@ -4520,6 +4596,7 @@ static void net_rps_action_and_irq_enable(struct softnet_data *sd)
 	} else
 #endif
 		local_irq_enable();
+	preempt_check_resched_rt();
 }
 
 static bool sd_has_rps_ipi_waiting(struct softnet_data *sd)
@@ -4601,6 +4678,7 @@ void __napi_schedule(struct napi_struct *n)
 	local_irq_save(flags);
 	____napi_schedule(this_cpu_ptr(&softnet_data), n);
 	local_irq_restore(flags);
+	preempt_check_resched_rt();
 }
 EXPORT_SYMBOL(__napi_schedule);
 
@@ -4877,7 +4955,7 @@ static void net_rx_action(struct softirq_action *h)
 	list_splice_tail(&repoll, &list);
 	list_splice(&list, &sd->poll_list);
 	if (!list_empty(&sd->poll_list))
-		__raise_softirq_irqoff(NET_RX_SOFTIRQ);
+		__raise_softirq_irqoff_ksoft(NET_RX_SOFTIRQ);
 
 	net_rps_action_and_irq_enable(sd);
 }
@@ -7208,7 +7286,7 @@ EXPORT_SYMBOL(free_netdev);
 void synchronize_net(void)
 {
 	might_sleep();
-	if (rtnl_is_locked())
+	if (rtnl_is_locked() && !IS_ENABLED(CONFIG_PREEMPT_RT_FULL))
 		synchronize_rcu_expedited();
 	else
 		synchronize_rcu();
@@ -7449,16 +7527,20 @@ static int dev_cpu_callback(struct notifier_block *nfb,
 
 	raise_softirq_irqoff(NET_TX_SOFTIRQ);
 	local_irq_enable();
+	preempt_check_resched_rt();
 
 	/* Process offline CPU's input_pkt_queue */
 	while ((skb = __skb_dequeue(&oldsd->process_queue))) {
 		netif_rx_ni(skb);
 		input_queue_head_incr(oldsd);
 	}
-	while ((skb = skb_dequeue(&oldsd->input_pkt_queue))) {
+	while ((skb = __skb_dequeue(&oldsd->input_pkt_queue))) {
 		netif_rx_ni(skb);
 		input_queue_head_incr(oldsd);
 	}
+	while ((skb = __skb_dequeue(&oldsd->tofree_queue))) {
+		kfree_skb(skb);
+	}
 
 	return NOTIFY_OK;
 }
@@ -7760,8 +7842,9 @@ static int __init net_dev_init(void)
 	for_each_possible_cpu(i) {
 		struct softnet_data *sd = &per_cpu(softnet_data, i);
 
-		skb_queue_head_init(&sd->input_pkt_queue);
-		skb_queue_head_init(&sd->process_queue);
+		skb_queue_head_init_raw(&sd->input_pkt_queue);
+		skb_queue_head_init_raw(&sd->process_queue);
+		skb_queue_head_init_raw(&sd->tofree_queue);
 		INIT_LIST_HEAD(&sd->poll_list);
 		sd->output_queue_tailp = &sd->output_queue;
 #ifdef CONFIG_RPS
diff --git a/net/core/skbuff.c b/net/core/skbuff.c
index b2df375ec9c2..53f10c2d7718 100644
--- a/net/core/skbuff.c
+++ b/net/core/skbuff.c
@@ -63,6 +63,7 @@
 #include <linux/errqueue.h>
 #include <linux/prefetch.h>
 #include <linux/if_vlan.h>
+#include <linux/locallock.h>
 
 #include <net/protocol.h>
 #include <net/dst.h>
@@ -349,6 +350,8 @@ EXPORT_SYMBOL(build_skb);
 
 static DEFINE_PER_CPU(struct page_frag_cache, netdev_alloc_cache);
 static DEFINE_PER_CPU(struct page_frag_cache, napi_alloc_cache);
+static DEFINE_LOCAL_IRQ_LOCK(netdev_alloc_lock);
+static DEFINE_LOCAL_IRQ_LOCK(napi_alloc_cache_lock);
 
 static void *__netdev_alloc_frag(unsigned int fragsz, gfp_t gfp_mask)
 {
@@ -356,10 +359,10 @@ static void *__netdev_alloc_frag(unsigned int fragsz, gfp_t gfp_mask)
 	unsigned long flags;
 	void *data;
 
-	local_irq_save(flags);
+	local_lock_irqsave(netdev_alloc_lock, flags);
 	nc = this_cpu_ptr(&netdev_alloc_cache);
 	data = __alloc_page_frag(nc, fragsz, gfp_mask);
-	local_irq_restore(flags);
+	local_unlock_irqrestore(netdev_alloc_lock, flags);
 	return data;
 }
 
@@ -378,9 +381,13 @@ EXPORT_SYMBOL(netdev_alloc_frag);
 
 static void *__napi_alloc_frag(unsigned int fragsz, gfp_t gfp_mask)
 {
-	struct page_frag_cache *nc = this_cpu_ptr(&napi_alloc_cache);
+	struct page_frag_cache *nc;
+	void *data;
 
-	return __alloc_page_frag(nc, fragsz, gfp_mask);
+	nc = &get_locked_var(napi_alloc_cache_lock, napi_alloc_cache);
+	data = __alloc_page_frag(nc, fragsz, gfp_mask);
+	put_locked_var(napi_alloc_cache_lock, napi_alloc_cache);
+	return data;
 }
 
 void *napi_alloc_frag(unsigned int fragsz)
@@ -427,13 +434,13 @@ struct sk_buff *__netdev_alloc_skb(struct net_device *dev, unsigned int len,
 	if (sk_memalloc_socks())
 		gfp_mask |= __GFP_MEMALLOC;
 
-	local_irq_save(flags);
+	local_lock_irqsave(netdev_alloc_lock, flags);
 
 	nc = this_cpu_ptr(&netdev_alloc_cache);
 	data = __alloc_page_frag(nc, len, gfp_mask);
 	pfmemalloc = nc->pfmemalloc;
 
-	local_irq_restore(flags);
+	local_unlock_irqrestore(netdev_alloc_lock, flags);
 
 	if (unlikely(!data))
 		return NULL;
@@ -474,9 +481,10 @@ EXPORT_SYMBOL(__netdev_alloc_skb);
 struct sk_buff *__napi_alloc_skb(struct napi_struct *napi, unsigned int len,
 				 gfp_t gfp_mask)
 {
-	struct page_frag_cache *nc = this_cpu_ptr(&napi_alloc_cache);
+	struct page_frag_cache *nc;
 	struct sk_buff *skb;
 	void *data;
+	bool pfmemalloc;
 
 	len += NET_SKB_PAD + NET_IP_ALIGN;
 
@@ -494,7 +502,11 @@ struct sk_buff *__napi_alloc_skb(struct napi_struct *napi, unsigned int len,
 	if (sk_memalloc_socks())
 		gfp_mask |= __GFP_MEMALLOC;
 
+	nc = &get_locked_var(napi_alloc_cache_lock, napi_alloc_cache);
 	data = __alloc_page_frag(nc, len, gfp_mask);
+	pfmemalloc = nc->pfmemalloc;
+	put_locked_var(napi_alloc_cache_lock, napi_alloc_cache);
+
 	if (unlikely(!data))
 		return NULL;
 
@@ -505,7 +517,7 @@ struct sk_buff *__napi_alloc_skb(struct napi_struct *napi, unsigned int len,
 	}
 
 	/* use OR instead of assignment to avoid clearing of bits in mask */
-	if (nc->pfmemalloc)
+	if (pfmemalloc)
 		skb->pfmemalloc = 1;
 	skb->head_frag = 1;
 
diff --git a/net/core/sock.c b/net/core/sock.c
index 0d91f7dca751..9c3234299fc3 100644
--- a/net/core/sock.c
+++ b/net/core/sock.c
@@ -2435,12 +2435,11 @@ void lock_sock_nested(struct sock *sk, int subclass)
 	if (sk->sk_lock.owned)
 		__lock_sock(sk);
 	sk->sk_lock.owned = 1;
-	spin_unlock(&sk->sk_lock.slock);
+	spin_unlock_bh(&sk->sk_lock.slock);
 	/*
 	 * The sk_lock has mutex_lock() semantics here:
 	 */
 	mutex_acquire(&sk->sk_lock.dep_map, subclass, 0, _RET_IP_);
-	local_bh_enable();
 }
 EXPORT_SYMBOL(lock_sock_nested);
 
diff --git a/net/ipv4/icmp.c b/net/ipv4/icmp.c
index 36e26977c908..74314d95d0f8 100644
--- a/net/ipv4/icmp.c
+++ b/net/ipv4/icmp.c
@@ -69,6 +69,7 @@
 #include <linux/jiffies.h>
 #include <linux/kernel.h>
 #include <linux/fcntl.h>
+#include <linux/sysrq.h>
 #include <linux/socket.h>
 #include <linux/in.h>
 #include <linux/inet.h>
@@ -891,6 +892,30 @@ static bool icmp_redirect(struct sk_buff *skb)
 }
 
 /*
+ * 32bit and 64bit have different timestamp length, so we check for
+ * the cookie at offset 20 and verify it is repeated at offset 50
+ */
+#define CO_POS0		20
+#define CO_POS1		50
+#define CO_SIZE		sizeof(int)
+#define ICMP_SYSRQ_SIZE	57
+
+/*
+ * We got a ICMP_SYSRQ_SIZE sized ping request. Check for the cookie
+ * pattern and if it matches send the next byte as a trigger to sysrq.
+ */
+static void icmp_check_sysrq(struct net *net, struct sk_buff *skb)
+{
+	int cookie = htonl(net->ipv4.sysctl_icmp_echo_sysrq);
+	char *p = skb->data;
+
+	if (!memcmp(&cookie, p + CO_POS0, CO_SIZE) &&
+	    !memcmp(&cookie, p + CO_POS1, CO_SIZE) &&
+	    p[CO_POS0 + CO_SIZE] == p[CO_POS1 + CO_SIZE])
+		handle_sysrq(p[CO_POS0 + CO_SIZE]);
+}
+
+/*
  *	Handle ICMP_ECHO ("ping") requests.
  *
  *	RFC 1122: 3.2.2.6 MUST have an echo server that answers ICMP echo
@@ -917,6 +942,11 @@ static bool icmp_echo(struct sk_buff *skb)
 		icmp_param.data_len	   = skb->len;
 		icmp_param.head_len	   = sizeof(struct icmphdr);
 		icmp_reply(&icmp_param, skb);
+
+		if (skb->len == ICMP_SYSRQ_SIZE &&
+		    net->ipv4.sysctl_icmp_echo_sysrq) {
+			icmp_check_sysrq(net, skb);
+		}
 	}
 	/* should there be an ICMP stat for ignored echos? */
 	return true;
diff --git a/net/ipv4/sysctl_net_ipv4.c b/net/ipv4/sysctl_net_ipv4.c
index a0bd7a55193e..1866f910263f 100644
--- a/net/ipv4/sysctl_net_ipv4.c
+++ b/net/ipv4/sysctl_net_ipv4.c
@@ -818,6 +818,13 @@ static struct ctl_table ipv4_net_table[] = {
 		.proc_handler	= proc_dointvec
 	},
 	{
+		.procname	= "icmp_echo_sysrq",
+		.data		= &init_net.ipv4.sysctl_icmp_echo_sysrq,
+		.maxlen		= sizeof(int),
+		.mode		= 0644,
+		.proc_handler	= proc_dointvec
+	},
+	{
 		.procname	= "icmp_ignore_bogus_error_responses",
 		.data		= &init_net.ipv4.sysctl_icmp_ignore_bogus_error_responses,
 		.maxlen		= sizeof(int),
diff --git a/net/mac80211/rx.c b/net/mac80211/rx.c
index 82af407fea7a..62d408b65767 100644
--- a/net/mac80211/rx.c
+++ b/net/mac80211/rx.c
@@ -3554,7 +3554,7 @@ void ieee80211_rx_napi(struct ieee80211_hw *hw, struct sk_buff *skb,
 	struct ieee80211_supported_band *sband;
 	struct ieee80211_rx_status *status = IEEE80211_SKB_RXCB(skb);
 
-	WARN_ON_ONCE(softirq_count() == 0);
+	WARN_ON_ONCE_NONRT(softirq_count() == 0);
 
 	if (WARN_ON(status->band >= IEEE80211_NUM_BANDS))
 		goto drop;
diff --git a/net/netfilter/core.c b/net/netfilter/core.c
index f39276d1c2d7..10880c89d62f 100644
--- a/net/netfilter/core.c
+++ b/net/netfilter/core.c
@@ -22,11 +22,17 @@
 #include <linux/proc_fs.h>
 #include <linux/mutex.h>
 #include <linux/slab.h>
+#include <linux/locallock.h>
 #include <net/net_namespace.h>
 #include <net/sock.h>
 
 #include "nf_internals.h"
 
+#ifdef CONFIG_PREEMPT_RT_BASE
+DEFINE_LOCAL_IRQ_LOCK(xt_write_lock);
+EXPORT_PER_CPU_SYMBOL(xt_write_lock);
+#endif
+
 static DEFINE_MUTEX(afinfo_mutex);
 
 const struct nf_afinfo __rcu *nf_afinfo[NFPROTO_NUMPROTO] __read_mostly;
diff --git a/net/packet/af_packet.c b/net/packet/af_packet.c
index 992396aa635c..2c23bae0ea85 100644
--- a/net/packet/af_packet.c
+++ b/net/packet/af_packet.c
@@ -63,6 +63,7 @@
 #include <linux/if_packet.h>
 #include <linux/wireless.h>
 #include <linux/kernel.h>
+#include <linux/delay.h>
 #include <linux/kmod.h>
 #include <linux/slab.h>
 #include <linux/vmalloc.h>
@@ -695,7 +696,7 @@ static void prb_retire_rx_blk_timer_expired(unsigned long data)
 	if (BLOCK_NUM_PKTS(pbd)) {
 		while (atomic_read(&pkc->blk_fill_in_prog)) {
 			/* Waiting for skb_copy_bits to finish... */
-			cpu_relax();
+			cpu_chill();
 		}
 	}
 
@@ -957,7 +958,7 @@ static void prb_retire_current_block(struct tpacket_kbdq_core *pkc,
 		if (!(status & TP_STATUS_BLK_TMO)) {
 			while (atomic_read(&pkc->blk_fill_in_prog)) {
 				/* Waiting for skb_copy_bits to finish... */
-				cpu_relax();
+				cpu_chill();
 			}
 		}
 		prb_close_block(pkc, pbd, po, status);
diff --git a/net/rds/ib_rdma.c b/net/rds/ib_rdma.c
index a2340748ec86..19123a97b354 100644
--- a/net/rds/ib_rdma.c
+++ b/net/rds/ib_rdma.c
@@ -34,6 +34,7 @@
 #include <linux/slab.h>
 #include <linux/rculist.h>
 #include <linux/llist.h>
+#include <linux/delay.h>
 
 #include "rds.h"
 #include "ib.h"
@@ -313,7 +314,7 @@ static inline void wait_clean_list_grace(void)
 	for_each_online_cpu(cpu) {
 		flag = &per_cpu(clean_list_grace, cpu);
 		while (test_bit(CLEAN_LIST_BUSY_BIT, flag))
-			cpu_relax();
+			cpu_chill();
 	}
 }
 
diff --git a/net/sched/sch_generic.c b/net/sched/sch_generic.c
index 16bc83b2842a..47ef1b11b2e6 100644
--- a/net/sched/sch_generic.c
+++ b/net/sched/sch_generic.c
@@ -890,7 +890,7 @@ void dev_deactivate_many(struct list_head *head)
 	/* Wait for outstanding qdisc_run calls. */
 	list_for_each_entry(dev, head, close_list)
 		while (some_qdisc_is_busy(dev))
-			yield();
+			msleep(1);
 }
 
 void dev_deactivate(struct net_device *dev)
diff --git a/net/sunrpc/svc_xprt.c b/net/sunrpc/svc_xprt.c
index a6cbb2104667..5b69bb580617 100644
--- a/net/sunrpc/svc_xprt.c
+++ b/net/sunrpc/svc_xprt.c
@@ -340,7 +340,7 @@ void svc_xprt_do_enqueue(struct svc_xprt *xprt)
 		goto out;
 	}
 
-	cpu = get_cpu();
+	cpu = get_cpu_light();
 	pool = svc_pool_for_cpu(xprt->xpt_server, cpu);
 
 	atomic_long_inc(&pool->sp_stats.packets);
@@ -376,7 +376,7 @@ void svc_xprt_do_enqueue(struct svc_xprt *xprt)
 
 		atomic_long_inc(&pool->sp_stats.threads_woken);
 		wake_up_process(rqstp->rq_task);
-		put_cpu();
+		put_cpu_light();
 		goto out;
 	}
 	rcu_read_unlock();
@@ -397,7 +397,7 @@ void svc_xprt_do_enqueue(struct svc_xprt *xprt)
 		goto redo_search;
 	}
 	rqstp = NULL;
-	put_cpu();
+	put_cpu_light();
 out:
 	trace_svc_xprt_do_enqueue(xprt, rqstp);
 }
diff --git a/scripts/mkcompile_h b/scripts/mkcompile_h
index 6fdc97ef6023..523e0420d7f0 100755
--- a/scripts/mkcompile_h
+++ b/scripts/mkcompile_h
@@ -4,7 +4,8 @@ TARGET=$1
 ARCH=$2
 SMP=$3
 PREEMPT=$4
-CC=$5
+RT=$5
+CC=$6
 
 vecho() { [ "${quiet}" = "silent_" ] || echo "$@" ; }
 
@@ -57,6 +58,7 @@ UTS_VERSION="#$VERSION"
 CONFIG_FLAGS=""
 if [ -n "$SMP" ] ; then CONFIG_FLAGS="SMP"; fi
 if [ -n "$PREEMPT" ] ; then CONFIG_FLAGS="$CONFIG_FLAGS PREEMPT"; fi
+if [ -n "$RT" ] ; then CONFIG_FLAGS="$CONFIG_FLAGS RT"; fi
 UTS_VERSION="$UTS_VERSION $CONFIG_FLAGS $TIMESTAMP"
 
 # Truncate to maximum length
diff --git a/sound/core/pcm_native.c b/sound/core/pcm_native.c
index a8b27cdc2844..1dce7d3a629b 100644
--- a/sound/core/pcm_native.c
+++ b/sound/core/pcm_native.c
@@ -123,7 +123,7 @@ EXPORT_SYMBOL_GPL(snd_pcm_stream_unlock);
 void snd_pcm_stream_lock_irq(struct snd_pcm_substream *substream)
 {
 	if (!substream->pcm->nonatomic)
-		local_irq_disable();
+		local_irq_disable_nort();
 	snd_pcm_stream_lock(substream);
 }
 EXPORT_SYMBOL_GPL(snd_pcm_stream_lock_irq);
@@ -138,7 +138,7 @@ void snd_pcm_stream_unlock_irq(struct snd_pcm_substream *substream)
 {
 	snd_pcm_stream_unlock(substream);
 	if (!substream->pcm->nonatomic)
-		local_irq_enable();
+		local_irq_enable_nort();
 }
 EXPORT_SYMBOL_GPL(snd_pcm_stream_unlock_irq);
 
@@ -146,7 +146,7 @@ unsigned long _snd_pcm_stream_lock_irqsave(struct snd_pcm_substream *substream)
 {
 	unsigned long flags = 0;
 	if (!substream->pcm->nonatomic)
-		local_irq_save(flags);
+		local_irq_save_nort(flags);
 	snd_pcm_stream_lock(substream);
 	return flags;
 }
@@ -164,7 +164,7 @@ void snd_pcm_stream_unlock_irqrestore(struct snd_pcm_substream *substream,
 {
 	snd_pcm_stream_unlock(substream);
 	if (!substream->pcm->nonatomic)
-		local_irq_restore(flags);
+		local_irq_restore_nort(flags);
 }
 EXPORT_SYMBOL_GPL(snd_pcm_stream_unlock_irqrestore);
 
diff --git a/virt/kvm/async_pf.c b/virt/kvm/async_pf.c
index 77d42be6970e..67b325eab186 100644
--- a/virt/kvm/async_pf.c
+++ b/virt/kvm/async_pf.c
@@ -98,8 +98,8 @@ static void async_pf_execute(struct work_struct *work)
 	 * This memory barrier pairs with prepare_to_wait's set_current_state()
 	 */
 	smp_mb();
-	if (waitqueue_active(&vcpu->wq))
-		wake_up_interruptible(&vcpu->wq);
+	if (swaitqueue_active(&vcpu->wq))
+		swait_wake_interruptible(&vcpu->wq);
 
 	mmput(mm);
 	kvm_put_kvm(vcpu->kvm);
diff --git a/virt/kvm/kvm_main.c b/virt/kvm/kvm_main.c
index 484079efea5b..018ed3eb7e79 100644
--- a/virt/kvm/kvm_main.c
+++ b/virt/kvm/kvm_main.c
@@ -227,7 +227,7 @@ int kvm_vcpu_init(struct kvm_vcpu *vcpu, struct kvm *kvm, unsigned id)
 	vcpu->vcpu_id = id;
 	vcpu->pid = NULL;
 	vcpu->halt_poll_ns = 0;
-	init_waitqueue_head(&vcpu->wq);
+	init_swait_head(&vcpu->wq);
 	kvm_async_pf_vcpu_init(vcpu);
 
 	vcpu->pre_pcpu = -1;
@@ -1999,7 +1999,7 @@ static int kvm_vcpu_check_block(struct kvm_vcpu *vcpu)
 void kvm_vcpu_block(struct kvm_vcpu *vcpu)
 {
 	ktime_t start, cur;
-	DEFINE_WAIT(wait);
+	DEFINE_SWAITER(wait);
 	bool waited = false;
 	u64 block_ns;
 
@@ -2024,7 +2024,7 @@ void kvm_vcpu_block(struct kvm_vcpu *vcpu)
 	kvm_arch_vcpu_blocking(vcpu);
 
 	for (;;) {
-		prepare_to_wait(&vcpu->wq, &wait, TASK_INTERRUPTIBLE);
+		swait_prepare(&vcpu->wq, &wait, TASK_INTERRUPTIBLE);
 
 		if (kvm_vcpu_check_block(vcpu) < 0)
 			break;
@@ -2033,7 +2033,7 @@ void kvm_vcpu_block(struct kvm_vcpu *vcpu)
 		schedule();
 	}
 
-	finish_wait(&vcpu->wq, &wait);
+	swait_finish(&vcpu->wq, &wait);
 	cur = ktime_get();
 
 	kvm_arch_vcpu_unblocking(vcpu);
@@ -2065,11 +2065,11 @@ void kvm_vcpu_kick(struct kvm_vcpu *vcpu)
 {
 	int me;
 	int cpu = vcpu->cpu;
-	wait_queue_head_t *wqp;
+	struct swait_head *wqp;
 
 	wqp = kvm_arch_vcpu_wq(vcpu);
-	if (waitqueue_active(wqp)) {
-		wake_up_interruptible(wqp);
+	if (swaitqueue_active(wqp)) {
+		swait_wake_interruptible(wqp);
 		++vcpu->stat.halt_wakeup;
 	}
 
@@ -2170,7 +2170,7 @@ void kvm_vcpu_on_spin(struct kvm_vcpu *me)
 				continue;
 			if (vcpu == me)
 				continue;
-			if (waitqueue_active(&vcpu->wq) && !kvm_arch_vcpu_runnable(vcpu))
+			if (swaitqueue_active(&vcpu->wq) && !kvm_arch_vcpu_runnable(vcpu))
 				continue;
 			if (!kvm_vcpu_eligible_for_directed_yield(vcpu))
 				continue;