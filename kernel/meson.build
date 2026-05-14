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
  'src/log/sink.c',
  'src/log/log.c',
  'src/log/sinks/e9.c',
  'src/arch/gdt/gdt.c',
  'src/arch/interrupts/idt.c',
  'cc-runtime/src/cc-runtime.c',
  'src/arch/io.asm',
  'src/arch/gdt/gdt.asm',
  'src/arch/interrupts/idt.asm',
)

kernel_bin = executable(
  'lapis',
  sources,
  include_directories: inc,
  c_args: c_args,
  nasm_args: nasm_args,
  link_args: link_args,
  link_depends: linker_script,
  install: false,
)
