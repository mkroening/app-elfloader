# Unikraft ELF Loader

## Executing ELF binaries

`elfloader` currently supports statically linked position-independent (PIE)
executables compiled for Linux on x86_64. Dynamically linked PIE executables can
be chain loaded using the corresponding official dynamic loader
(e.g., `ld-linux-x86-64.so.2` from glibc). This loader is also recognized as a
statically linked PIE executable.

In this version, any executable can only be passed to an `elfloader` unikernel
via the initrd argument. The ELF executable is the initrd, which means that no
additional initrd can be used as the root filesystem. Any additional filesystem
content must be passed via 9pfs.

### Static-PIE ELF executable
A static PIE executable can be simply handed over as initrd
([`qemu-guest`](https://github.com/unikraft/unikraft/tree/staging/support/scripts)):
```sh
$ qemu-guest -k elfloader_kvm-x86_64 -i /path/to/elfprogram \
             -a "<application arguments>"
```
Any application arguments are passed via kernel argument to the application.
Environment variables cannot be set, currently.

### Dynamically linked ELF executable
A dynamically linked PIE executable must be placed in a 9pfs root file system
along with its library dependencies. You can use `ldd` to list the dynamic
libraries on which the application depends in order to start.
Please note that the VDSO (here: `linux-vdso.so.1`) is provided by the Linux
kernel and is not present on the host filesystem. Please ignore this file.
```sh
$ ldd helloworld
	linux-vdso.so.1 (0x00007ffdd695d000)
	libc.so.6 => /lib/x86_64-linux-gnu/libc.so.6 (0x00007efed259f000)
	/lib64/ld-linux-x86-64.so.2 (0x00007efed2787000)
```

A populated root filesystem may look like:
```
rootfs/
├── libc.so.6
└── helloworld
```

Because the official dynamic loader maps the application and libraries into
memory, the `elfloader` unikernel must be configured with `posix-mmap` or
`ukmmap`. In addition, you must include `vfscore` in the build and do the
following configuration: Under `Library Configuration -> vfscore: Configuration`
select `Automatically mount a root filesystem`, set `Default root filesystem` to
`9PFS`, and optionally set `Default root device` to `fs0`. This last option
simplifies the use of the `-e` parameter of `qemu-guest`.

A dynamically linked application (here: [`/helloworld`](./example/helloworld))
can then be started with
([`qemu-guest`](https://github.com/unikraft/unikraft/tree/staging/support/scripts)):
```sh
> qemu-guest -k elfloader_kvm-x86_64 -i /lib64/ld-linux-x86-64.so.2 -e rootfs/ \
            -a "--library-path / /helloworld <application arguments>"
```
Please note that environment variables cannot be set, currently.


## Debugging

### `strace`-like Output

Unikraft's
[`syscall_shim`](https://github.com/unikraft/unikraft/tree/staging/lib/syscall_shim)
provides the ability to print a strace-like message for every processed binary
system call request on the kernel output. This option can be useful for
understanding what code a system call handler returns to the application and
how the application interacts with the kernel. The setting can be found under
`Library Configuration -> syscall_shim -> Debugging`:
`'strace'-like messages for binary system calls`.

### GNU Debugger (gdb)

It is possible to debug `elfloader` together with the loaded application and use
the full set of debugging facilities for kernel and application at the same
time. In principle, `gdb` must only be made aware of the runtime memory layout
of `elfloader` with the loaded application. Thanks to the single address space
layout, we gain easy debugability and full transparency.

#### Static-PIE ELF executable
As a first step, `gdb` is started with loading the symbols from the `dbg`
image of the `elfloader`. We map the symbols of ELF application with the gdb
command `add-symbol-file` by specifying the application (with debug symbols) and
the base load address. If `info` messages are enabled in `ukdebug`, this base
address will be messaged by the loader like this:
```
ELF program loaded to 0x400101000-0x4001c2a08 (793096 B), entry at 0x40010ad50
```
To this address (here: `0x400101000`) you have to add the offset of the `.text`
segment. You can use `readelf -S` to find it out. In our example it is `0x92a0`
(output shortened):
```
$ readelf -S helloworld_static
Section Headers:
  [Nr] Name              Type             Address           Offset
       Size              EntSize          Flags  Link  Info  Align
  [11] .text             PROGBITS         00000000000092a0  000092a0
       0000000000086eb0  0000000000000000  AX       0     0     32
```
The resulting address here is `0x40010a2a0`. The symbols of the static
helloworld program can then be loaded from `gdb` with the following command:
```
(gdb) add-symbol-file -readnow helloworld_static 0x40010A2A0
```
From this point you have symbol resolution in your debugger for both, the
Unikraft elfloader and the loaded application.

#### Dynamically linked ELF executable
The principle of runtime address space layout for dynamically linked executables
is the same as for statically linked executables. The difference is that we need
to load the symbols of every dynamic library in addition. Because the
application is loaded in a chain by the dynamic loader, we need to get our base
address of the application from the loader, as well.
For this purpose, we recommend to enable `strace`-like output in `syscall_shim`
(read subsection: `strace`-like output). The loader will 1) open the
application, 2) parse its ELF header, and 3) map all needed sections into
memory. Afterwards it will continue with the same steps for each dynamic
library.
For our Helloworld example compiled with `glibc`, this looks as follows:
```cpp
openat(AT_FDCWD, "/helloworld", O_RDONLY|O_CLOEXEC) = fd:3
read(fd:3, <out>"\x7FELF\x02\x01\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x03\x00>\x00\x01\x00\x00\x00"..., 832) = 832
mmap(NULL, 16440, PROT_READ, MAP_PRIVATE|MAP_DENYWRITE, fd:3, 0) = va:0x8000000000
mmap(va:0x8000001000, 4096, PROT_EXEC|PROT_READ, MAP_PRIVATE|MAP_DENYWRITE|MAP_FIXED, fd:3, 4096) = va:0x8000001000
mmap(va:0x8000002000, 4096, PROT_READ, MAP_PRIVATE|MAP_DENYWRITE|MAP_FIXED, fd:3, 8192) = va:0x8000002000
mmap(va:0x8000003000, 8192, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_DENYWRITE|MAP_FIXED, fd:3, 8192) = va:0x8000003000
close(fd:3) = OK
```
The virtual address returned by the first `mmap` operation is the virtual base
address of the application, which we must note down. Please do the same for each
dynamically loaded library:
```cpp
openat(AT_FDCWD, "/libc.so.6", O_RDONLY|O_CLOEXEC) = fd:3
read(fd:3, <out>"\x7FELF\x02\x01\x01\x03\x00\x00\x00\x00\x00\x00\x00\x00\x03\x00>\x00\x01\x00\x00\x00"..., 832) = 832
fstat(fd:3, va:0x40006ef18) = OK
mmap(NULL, 1918592, PROT_READ, MAP_PRIVATE|MAP_DENYWRITE, fd:3, 0) = va:0x8000005000
mmap(va:0x8000027000, 1417216, PROT_EXEC|PROT_READ, MAP_PRIVATE|MAP_DENYWRITE|MAP_FIXED, fd:3, 139264) = va:0x8000027000
mmap(va:0x8000181000, 323584, PROT_READ, MAP_PRIVATE|MAP_DENYWRITE|MAP_FIXED, fd:3, 1556480) = va:0x8000181000
mmap(va:0x80001d0000, 24576, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_DENYWRITE|MAP_FIXED, fd:3, 1875968) = va:0x80001d0000
mmap(va:0x80001d6000, 13952, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED, fd:-1, 0) = va:0x80001d6000
close(fd:3) = OK
```
In this case, the virtual base address of `libc.so.6` is `0x8000005000`.

To load the application and library symbols appropriately, as described in the
previous subsection, you must add the segment offset of the `.text` section to
virtual base address. This allows you to load the symbols from `gdb` with
`add-symbol-file`.

#### Recommendation: `gdb` Setup Script
Because of the calculations, we recommend scripting the `gdb` setup so that any
subsequent `gdb` debug session will be ready quickly. You can get inspiration
from the following `bash` script, which provides a function that automatically
determines the `.text` offset and adds it to a given base address.
The advantage is that only the base addresses have to be noted from the
Unikraft console output.

```bash
#!/bin/bash
# Host and port of the GDB server port (qemu)
GDBSRV=":1234"

# Generate a GDB command line for loading the symbols of an ELF executable/
# shared library.
# Usage: gdb-add-symbols "<ELF executable/library>" \
#                        "<base load address (hex, no leading '0x')>"
gdb-add-symbols()
{
	local LOAD_ELF="$1"
	local LOAD_ADDR="${2}"
	local LOAD_TADDR=
	local TEXT_OFFSET=

	# Hacky way to figure out the .text offset
	TEXT_OFFSET=$( readelf -S "${LOAD_ELF}" | grep '.text' | awk '{ print $5 }' )

	# Compute offset of .text section with base address
	LOAD_TADDR=$( printf 'obase=16;ibase=16;%s+%s\n' "${LOAD_ADDR^^}" "${TEXT_OFFSET^^}" | bc )

	# Generate GDB command
	printf 'add-symbol-file -readnow %s 0x%s' "${LOAD_ELF}" "${LOAD_TADDR}"
}

# Connect to $GDBSRV and set up gdb
# NOTE: The first block of instructions connects to the gdb port of the qemu
#       process and follows the CPU mode change while the guest is booting.
#       This is currently a requirement for using gdb with qemu for x86_64. For
#       other platforms and architectures, this may be different.
#       This block assumes that the guest is started in paused state (`-P` for
#       `qemu-guest`).
# HINT: You can use the `directory` command to specify additional paths that
#       `gdb` will use to search for source files.
#       For example, if you run your dynamically linked application with
#       Debian's libc, you can install (`apt install glibc-source`) and
#       extract the glibc sources under /usr/src/glibc.
#         --eval-command="directory /usr/src/glibc/glibc-2.31"
exec gdb \
	--eval-command="target remote $GDBSRV" \
	--eval-command="hbreak _libkvmplat_entry" \
	--eval-command="continue" \
	--eval-command="disconnect" \
	--eval-command="set arch i386:x86-64:intel" \
	--eval-command="target remote $GDBSRV" \
	\
	--eval-command="$( gdb-add-symbols "rootfs/helloworld" "8000000000" )" \
	--eval-command="$( gdb-add-symbols "rootfs/libc.so.6" "8000005000" )"
```

#### Hint: Debug symbols of libraries installed from packages

If you run your dynamically linked application with libraries installed via a
package manager, you can check if a debug package is also available for
installation.
For example, Debian provides the debug symbols automatically for `gdb` with the
following installation:
```sh
$ apt install libc6-dbg
```
As soon as the Debian's libc.so.6 is loaded by `gdb`, the debugger will load the
symbols provided by the Debian debug package.

#### Hint: Sources of libraries installed from packages

If you run your dynamically linked application with libraries installed via a
package manager, you can check if a source package is available for
installation. For example, the libc sources are available under Debian with the
package `glibc-source`. After you extracted the installed sources archive under
`/usr/src/glibc`, you can make these sources visible to `gdb` with (given that
you use Debian's libc also for the application with `elfloader`):
```
(gdb) directory /usr/src/glibc/glibc-2.31
```
