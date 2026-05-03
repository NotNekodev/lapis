project(
  'lapis',
  ['c', 'nasm'],
  version: '0.0.1',
  default_options: [
    'c_std=gnu11',
    'warning_level=3',
  ],
)

image_name = get_option('image_name')
qemu_flags = get_option('qemu_flags')

subdir('kernel')

sh = find_program('sh')

iso_output = join_paths(meson.current_build_dir(), image_name + '.iso')
hdd_output = join_paths(meson.current_build_dir(), image_name + '.hdd')
kernel_path = kernel_bin.full_path()
ovmf_path = join_paths(meson.project_source_root(), 'edk2-ovmf', 'ovmf-code-x86_64.fd')

limine_stamp = custom_target(
  'limine-binary',
  output: 'limine-binary.stamp',
  command: [
    sh,
    join_paths(meson.project_source_root(), 'scripts', 'get-limine.sh'),
    meson.project_source_root(),
  ],
  build_by_default: false,
)

edk2_stamp = custom_target(
  'edk2-ovmf',
  output: 'edk2-ovmf.stamp',
  command: [
    sh,
    join_paths(meson.project_source_root(), 'scripts', 'get-edk2.sh'),
    meson.project_source_root(),
  ],
  build_by_default: false,
)

iso = custom_target(
  'iso',
  input: kernel_bin,
  output: image_name + '.iso',
  depends: limine_stamp,
  command: [
    sh,
    join_paths(meson.project_source_root(), 'scripts', 'make-iso.sh'),
    meson.project_source_root(),
    meson.current_build_dir(),
    kernel_path,
    iso_output,
  ],
  build_by_default: true,
)

hdd = custom_target(
  'hdd',
  input: kernel_bin,
  output: image_name + '.hdd',
  depends: limine_stamp,
  command: [
    sh,
    join_paths(meson.project_source_root(), 'scripts', 'make-hdd.sh'),
    meson.project_source_root(),
    kernel_path,
    hdd_output,
    meson.current_build_dir(),
  ],
  build_by_default: false,
)

qemu = find_program('qemu-system-x86_64', required: false)
if qemu.found()
  qemu_flags_list = qemu_flags.split()

  run_target(
    'run',
    command: [
      sh,
      join_paths(meson.project_source_root(), 'scripts', 'run-qemu.sh'),
      qemu.full_path(),
      'iso',
      iso.full_path(),
      hdd.full_path(),
      ovmf_path,
    ] + qemu_flags_list,
    depends: iso,
  )

  run_target(
    'run-uefi',
    command: [
      sh,
      join_paths(meson.project_source_root(), 'scripts', 'run-qemu.sh'),
      qemu.full_path(),
      'iso-uefi',
      iso.full_path(),
      hdd.full_path(),
      ovmf_path,
    ] + qemu_flags_list,
    depends: [edk2_stamp, iso],
  )

  run_target(
    'run-hdd',
    command: [
      sh,
      join_paths(meson.project_source_root(), 'scripts', 'run-qemu.sh'),
      qemu.full_path(),
      'hdd',
      iso.full_path(),
      hdd.full_path(),
      ovmf_path,
    ] + qemu_flags_list,
    depends: hdd,
  )

  run_target(
    'run-hdd-uefi',
    command: [
      sh,
      join_paths(meson.project_source_root(), 'scripts', 'run-qemu.sh'),
      qemu.full_path(),
      'hdd-uefi',
      iso.full_path(),
      hdd.full_path(),
      ovmf_path,
    ] + qemu_flags_list,
    depends: [edk2_stamp, hdd],
  )
endif
