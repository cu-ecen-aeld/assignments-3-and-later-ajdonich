## Oops Message for `faulty` device

What happened? Line 1: `Unable to handle kernel NULL pointer dereference at virtual address 0000000000000000`  
Where it occurred? Line 18: `pc : faulty_write+0x10/0x20 [faulty]` (0x10 bytes into the 0x20 line faulty_write fcn)

``` bash
0: # echo “hello_world” > /dev/faulty
1: Unable to handle kernel NULL pointer dereference at virtual address 0000000000000000
2: Mem abort info:
3:   ESR = 0x0000000096000045
4:   EC = 0x25: DABT (current EL), IL = 32 bits
5:   SET = 0, FnV = 0
6:   EA = 0, S1PTW = 0
7:   FSC = 0x05: level 1 translation fault
8: Data abort info:
9:   ISV = 0, ISS = 0x00000045
10:   CM = 0, WnR = 1
11: user pgtable: 4k pages, 39-bit VAs, pgdp=0000000041b49000
12: [0000000000000000] pgd=0000000000000000, p4d=0000000000000000, pud=0000000000000000
13: Internal error: Oops: 0000000096000045 [#1] SMP
14: Modules linked in: scull(O) faulty(O) hello(O)
15: CPU: 0 PID: 127 Comm: sh Tainted: G           O       6.1.44 #1
16: Hardware name: linux,dummy-virt (DT)
17: pstate: 80000005 (Nzcv daif -PAN -UAO -TCO -DIT -SSBS BTYPE=--)
18: pc : faulty_write+0x10/0x20 [faulty]
19: lr : vfs_write+0xc8/0x390
20: sp : ffffffc008da3d20
21: x29: ffffffc008da3d80 x28: ffffff8001b36a00 x27: 0000000000000000
22: x26: 0000000000000000 x25: 0000000000000000 x24: 0000000000000000
23: x23: 0000000000000012 x22: 0000000000000012 x21: ffffffc008da3dc0
24: x20: 00000055763d1990 x19: ffffff8001bc1c00 x18: 0000000000000000
25: x17: 0000000000000000 x16: 0000000000000000 x15: 0000000000000000
26: x14: 0000000000000000 x13: 0000000000000000 x12: 0000000000000000
27: x11: 0000000000000000 x10: 0000000000000000 x9 : 0000000000000000
28: x8 : 0000000000000000 x7 : 0000000000000000 x6 : 0000000000000000
29: x5 : 0000000000000001 x4 : ffffffc000785000 x3 : ffffffc008da3dc0
30: x2 : 0000000000000012 x1 : 0000000000000000 x0 : 0000000000000000
31: Call trace:
32:  faulty_write+0x10/0x20 [faulty]
33:  ksys_write+0x74/0x110
34:  __arm64_sys_write+0x1c/0x30
35:  invoke_syscall+0x54/0x130
36:  el0_svc_common.constprop.0+0x44/0xf0
37:  do_el0_svc+0x2c/0xc0
38:  el0_svc+0x2c/0x90
39:  el0t_64_sync_handler+0xf4/0x120
40:  el0t_64_sync+0x18c/0x190
41: Code: d2800001 d2800000 d503233f d50323bf (b900003f) 
42: ---[ end trace 0000000000000000 ]---
```

### faulty.c [faulty_write fcn]
``` C
ssize_t faulty_write (struct file *filp, const char __user *buf, size_t count,
		loff_t *pos)
{
	/* make a simple fault by dereferencing a NULL pointer */
	*(int *)0 = 0;
	return 0;
}
```

### objdump faulty.ko  [faulty_write fcn]
``` bash
ubuntu@ip-172-31-40-193:~/assignment-4-ajdonich/buildroot/output$ host/bin/aarch64-linux-objdump -S target/lib/modules/6.1.44/extra/faulty.ko 

target/lib/modules/6.1.44/extra/faulty.ko:     file format elf64-littleaarch64


Disassembly of section .text:

0000000000000000 <faulty_write>:
   0:	d2800001 	mov	x1, #0x0                   	// #0
   4:	d2800000 	mov	x0, #0x0                   	// #0
   8:	d503233f 	paciasp
   c:	d50323bf 	autiasp
  10:	b900003f 	str	wzr, [x1]
  14:	d65f03c0 	ret
  18:	d503201f 	nop
  1c:	d503201f 	nop

// ...
// Output tructated (of
// other faulty.c fcns)
// ...

Disassembly of section .plt:

0000000000000000 <.plt>:
	...

Disassembly of section .text.ftrace_trampoline:

0000000000000000 <.text.ftrace_trampoline>:
	...
```  


