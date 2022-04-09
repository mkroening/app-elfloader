/* Authors: Pierre Olivier */ //BSD3clause?
/* Simon Kuenzer */

/* Taken & modified from: Hermitux
 * commit 8780d335
 * apps/hermitux-light/hermitux-light.c */

#include <uk/plat/bootstrap.h>
#include <uk/assert.h>
#include <uk/print.h>
#include <uk/arch/ctx.h>
#include <uk/essentials.h>

#include "binfmt_elf.h"

/* Fields for auxiliary vector
 * (https://lwn.net/Articles/519085/)
 */
#define AT_NULL			0
#define AT_IGNORE		1
#define AT_EXECFD		2
#define AT_PHDR			3
#define AT_PHENT		4
#define AT_PHNUM		5
#define AT_PAGESZ		6
#define AT_BASE			7
#define AT_FLAGS		8
#define AT_ENTRY		9
#define AT_NOTELF		10
#define AT_UID			11
#define AT_EUID			12
#define AT_GID			13
#define AT_EGID			14
#define AT_PLATFORM		15
#define AT_HWCAP		16
#define AT_CLKTCK		17
#define AT_DCACHEBSIZE		19
#define AT_ICACHEBSIZE		20
#define AT_UCACHEBSIZE		21
#define AT_SECURE		23
#define AT_RANDOM		25
#define AT_EXECFN		31
#define AT_SYSINFO_EHDR		33
#define AT_SYSINFO		32

#if CONFIG_ARCH_X86_64
	static const char *auxv_platform = "x86_64";
#else
#error "Unsupported architecture"
#endif /* CONFIG_ARCH_X86_64 */

#define push_auxv(ctx, key, val)				\
	do {							\
		ukarch_rctx_stackpush(ctx, (long) val);		\
		ukarch_rctx_stackpush(ctx, (long) key);      	\
	} while (0)

void ctx_elf(struct ukarch_ctx *ctx, struct elf_prog *prog,
	     int argc, char *argv[], char *environ[],
	     uint64_t *rand[2])
{
	int i, envc;
	void *rnd16 = NULL;

	UK_ASSERT(prog);
	UK_ASSERT((argc >= 1) && argv);

	uk_pr_debug("%s: image:          0x%"PRIx64" - 0x%"PRIx64"\n", argv[0],
		    (uint64_t) prog->img, (uint64_t) prog->img + prog->img_len);
	uk_pr_debug("%s: start:          0x%"PRIx64"\n", argv[0], (uint64_t) prog->start);
	uk_pr_debug("%s: entry:          0x%"PRIx64"\n", argv[0], (uint64_t) prog->entry);
	uk_pr_debug("%s: ehdr_phoff:     0x%"PRIx64"\n", argv[0], (uint64_t) prog->ehdr_phoff);
	uk_pr_debug("%s: ehdr_phnum:     %"PRIu64"\n",   argv[0], (uint64_t) prog->ehdr_phnum);
	uk_pr_debug("%s: ehdr_phentsize: 0x%"PRIx64"\n", argv[0], (uint64_t) prog->ehdr_phentsize);

	/* count the number of environment variables */
	envc = 0;
	if (environ)
		for (char **env = environ; *env; ++env)
			++envc;

	/*
	 * TODO: Create new stack and push arguments there
	 */

	/* We need to push the element on the stack in the inverse order they
	 * will be read by the application's C library (i.e. argc in the end) */

	/* Use first 16bytes on stack to push 16-bytes random numbers */
	//push_rand16(ctx, rand0, rand1);
	//rnd16 = (void *) ctx->sp;
	uk_pr_debug("%s: rnd16 at %p\n", argv[0], rnd16);

	/*
	 * Auxiliary vector
	 */
	push_auxv(ctx, AT_NULL, 0x0);
	push_auxv(ctx, AT_IGNORE, 0x0);
	push_auxv(ctx, AT_EXECFD, 0x0);
	push_auxv(ctx, AT_PHDR, prog->start + prog->ehdr_phoff);
	push_auxv(ctx, AT_PHNUM, prog->ehdr_phnum);
	push_auxv(ctx, AT_PHENT, prog->ehdr_phentsize);
	push_auxv(ctx, AT_RANDOM, (uintptr_t) rand); // pointer to random numbers that we have on the stack
	push_auxv(ctx, AT_BASE, 0x0);
	push_auxv(ctx, AT_SYSINFO_EHDR, 0x0);
	push_auxv(ctx, AT_SYSINFO, 0x0);
	push_auxv(ctx, AT_PAGESZ, 4096);
	push_auxv(ctx, AT_HWCAP, 0x0);
	push_auxv(ctx, AT_CLKTCK, 0x64); // mimic Linux
	push_auxv(ctx, AT_FLAGS, 0x0);
	push_auxv(ctx, AT_ENTRY, prog->entry);
	push_auxv(ctx, AT_UID, 0x0);
	push_auxv(ctx, AT_EUID, 0x0);
	push_auxv(ctx, AT_GID, 0x0);
	push_auxv(ctx, AT_EGID, 0x0);
	push_auxv(ctx, AT_SECURE, 0x0);
	push_auxv(ctx, AT_SYSINFO, 0x0);
	push_auxv(ctx, AT_EXECFN, 0x0);
	push_auxv(ctx, AT_DCACHEBSIZE, 0x0);
	push_auxv(ctx, AT_ICACHEBSIZE, 0x0);
	push_auxv(ctx, AT_UCACHEBSIZE, 0x0);
	push_auxv(ctx, AT_NOTELF, 0x0);
	push_auxv(ctx, AT_PLATFORM, (uintptr_t) auxv_platform);

	/* envp */
	/* Note that this will push NULL to the stack first, which is expected */
	ukarch_rctx_stackpush(ctx, (long) NULL);
	if (environ) {
		for (i=envc-1; i>=0; --i)
			ukarch_rctx_stackpush(ctx, (uintptr_t) environ[i]);
	}

	/* argv + argc */
	/* Same as envp, pushing NULL first */
	ukarch_rctx_stackpush(ctx, (long) NULL);
	if (argc)
		for(i=argc-1; i>0; --i)
			ukarch_rctx_stackpush(ctx, (uintptr_t) argv[i]);
	ukarch_rctx_stackpush(ctx, (long) argc - 1);

	/* ctx will enter entry point with cleared registers */
	/* NOTE: with GlibC, the dynamic linker sets in rdx the address of some
	 * code to be executed at exit (if != 0), however we are not using it
	 * and here it contains some garbage value, so clear it
	 */
	ukarch_ctx_init_entry0(ctx, ctx->sp, 0x0, (ukarch_ctx_entry0) prog->entry);
}
