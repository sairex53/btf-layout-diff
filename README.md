# btf-layout-diff

Small C++20 command-line tool for spotting named `STRUCT`/`UNION` layout changes between two Linux BTF dumps. It reads the text produced by `bpftool btf dump ... format raw`, compares aggregate sizes and direct member offsets/bitfield widths, and prints a compact report.

The project is an offline inspection aid for kernel and eBPF work. It does not load BPF programs, access the running kernel, need root, or modify anything on the host.

## Example

The included fixture dumps are **synthetic** and exist only to make the example reproducible on any OS:

```text
$ ./build/btf-layout-diff kernel-old.raw kernel-new.raw --type task_struct
~ STRUCT task_struct
  size: 24 -> 32 bytes
  + new_field at 192 bits
```

The `type_id` values differ between the two files on purpose. BTF IDs are local to a dump, so this tool does not treat them as stable identities.

## Build and test

Requires CMake 3.20+ and a C++20 compiler. The parser and tests also build on macOS, so you can work on the project without a Linux host.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

## Compare two real kernels

Save the dumps on the corresponding Linux systems (or from BTF files you already have):

```sh
bpftool btf dump file /sys/kernel/btf/vmlinux format raw > kernel-a.raw
bpftool btf dump file /sys/kernel/btf/vmlinux format raw > kernel-b.raw
```

Then compare one type that your BPF program reads:

```sh
./build/btf-layout-diff kernel-a.raw kernel-b.raw --type task_struct
```

Use dumps from the architectures and kernel configurations you actually care about. A changed offset or size is a useful clue to investigate; it is not, by itself, proof that a particular CO-RE relocation will fail. CO-RE behavior also depends on the relocations recorded in the BPF object and the target BTF. This tool does not inspect either one.

Omit `--type NAME` to print differences for all named aggregates in the dumps. That can be a long report for full `vmlinux` BTF files.

## Scope and limits

- Parses the human-readable `bpftool` raw format, not the binary BTF encoding.
- Compares named `STRUCT` and `UNION` records by kind, total size, direct member names, bit offsets, and bitfield widths.
- Ignores BTF type IDs because they can change between dumps. It also does not compare the referenced member types, pointer targets, enum values, qualifiers, or CO-RE relocation records.
- Anonymous aggregates are skipped. Duplicate named aggregate names are rejected as ambiguous instead of silently choosing one.
- Additions and removals can reflect different kernel configs or built-in components, not necessarily an incompatibility.
- The checked-in `kernel-old.raw` and `kernel-new.raw` dumps are hand-written synthetic fixtures; no claim is made that they came from a real kernel pair.

## Why compare BTF layouts?

BTF provides type information used by tools such as libbpf's CO-RE machinery to adapt accesses to kernel types. When an eBPF program behaves differently across kernels, a compact view of the relevant type's size and field offsets can help narrow down what changed before investigating the object's actual CO-RE relocations and verifier/load result. The report is a starting point for that investigation, not a compatibility verdict.

## References

- [Linux kernel BTF documentation](https://docs.kernel.org/bpf/btf.html)
- [Linux kernel BPF CO-RE documentation](https://docs.kernel.org/bpf/libbpf/libbpf_overview.html#compile-once-run-everywhere-core)
- [`bpftool` documentation in the Linux source tree](https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/tree/tools/bpf/bpftool/Documentation/bpftool-btf.rst)

## License

MIT. See [LICENSE](LICENSE).
