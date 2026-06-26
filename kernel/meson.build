fs = import('fs')

deps_marker = join_paths(meson.current_source_dir(), '.deps-obtained')
if not fs.exists(deps_marker)
  sh = find_program('sh')
  deps_script = join_paths(meson.project_source_root(), 'scripts', 'deps.sh')
  run_command(sh, deps_script, check: true)
  if not fs.exists(deps_marker)
    error('Deps script did not create .deps-obtained')
  endif
endif

c = meson.get_compiler('c')

c_args = [
  '-Wall',
  '-Wextra',
  '-std=gnu11',
  '-nostdinc',
  '-ffreestanding',
  '-fno-stack-protector',
  '-fno-omit-frame-pointer',
  '-fno-stack-check',
  '-fno-lto',
  '-fno-PIC',
  '-ffunction-sections',
  '-fdata-sections',
  '-m64',
  '-march=x86-64',
  '-mabi=sysv',
  '-mno-80387',
  '-mno-mmx',
  '-mno-sse',
  '-mno-sse2',
  '-mno-red-zone',
  '-mcmodel=kernel',
  '-DUACPI_OVERRIDE_CONFIG',
]

if c.get_id() == 'clang'
  c_args += ['-target', 'x86_64-unknown-none-elf']
endif

nasm_args = [
  '-f', 'elf64',
  '-g',
  '-F', 'dwarf',
  '-Wall',
]

inc = include_directories(
  'src',
  'limine-protocol/include',
  'freestnd-c-hdrs/include',
)

linker_script = join_paths(meson.current_source_dir(), 'linker.ld')

link_args = [
  '-nostdlib',
  '-static',
  '-Wl,-m,elf_x86_64',
  '-Wl,-z,max-page-size=0x1000',
  '-Wl,--gc-sections',
  '-Wl,-T,' + linker_script,
]

if c.get_id() == 'clang'
  link_args = ['-target', 'x86_64-unknown-none-elf'] + link_args
endif

sources = files(
  'src/main.c',
  'src/util/memory.c',
  'src/util/spinlock.c',
  'src/util/rbtree.c',
  'src/util/semaphore.c',
  'src/log/sink.c',
  'src/log/log.c',
  'src/log/sinks/e9.c',
  'src/log/sinks/uart16550.c',
  'src/log/sinks/kterm.c',
  'src/kterm/psf.c',
  'src/kterm/kterm.c',
  'src/mm/pmm.c',
  'src/mm/vmm.c',
  'src/mm/paging.c',
  'src/mm/kheap.c',
  'src/arch/gdt/gdt.c',
  'src/arch/interrupts/idt.c',
  'src/arch/interrupts/isr.c',
  'src/arch/interrupts/irq.c',
  'src/arch/cpu.c',
  'src/arch/smp.c',
  'src/arch/interrupts/apic.c',
  'src/arch/interrupts/pit.c',
  'src/acpi/uacpi_wrappers.c',
  'src/dev/pci.c',
  'src/dev/driver.c',
  'src/dev/bus.c',
  'src/dev/device.c',
  'src/fs/vfs/vfs.c',
  'src/fs/vfs/kfile.c',
  'src/fs/ramfs/ramfs.c',
  'src/fs/initrd/cpio.c',
  'src/sched/fd.c',
  'src/sched/sched.c',
  'src/sched/task.c',
  'src/sched/waitqueue.c',
  'src/mm/pfn_db.c',
  'cc-runtime/src/cc-runtime.c',
  'src/arch/io.asm',
  'src/arch/gdt/gdt.asm',
  'src/arch/interrupts/idt.asm',
  'src/sched/context.asm',
  'src/sched/umode_test.asm',
  'src/sched/umode_trampoline.asm'
)

# uacpi
uacpi = subproject('uacpi')

uacpi_sources = uacpi.get_variable('sources')
uacpi_includes = uacpi.get_variable('includes')

sources += uacpi_sources

kernel_bin = executable(
  'lapis',
  sources,
  include_directories: [inc, uacpi_includes],
  c_args: c_args,
  nasm_args: nasm_args,
  link_args: link_args,
  link_depends: linker_script,
  install: false,
)
