#define _KMEMUSER
#include <vm/hat_pte.h>
#include <sys/types.h>
#include <sys/param.h>
#include <sys/mman.h>
#include <sys/mutex.h>
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define KM_SLEEP 0

struct {
	int a_hat;
} kas = { 0 };

uintptr_t
hat_getpfnum(int _a, void *pa)
{
	return (uintptr_t)pa >> 12;
}

caddr_t
hat_kpm_pfn2va(pfn_t pfn)
{
	return (caddr_t)(pfn << 12);
}

void *
kmem_zalloc(size_t size, int _flags)
{
	void *p = NULL;

	if ((size % 4096) == 0)
		assert(posix_memalign(&p, size, size) == 0);
	else
		p = malloc(size);
	assert(p != NULL);
	memset(p, 0, size);
	return p;
}

void
kmem_free(void *p, size_t _size)
{
	free(p);
}

void panic(const char *str, ...)
{
	va_list args;
	va_start(args, str);
	vprintf(str, args);
	va_end(args);
}

#define mutex_init(A, B, C, D)
#define mutex_enter(L)
#define mutex_exit(L)
#define mutex_destroy(L)

#undef mmu_btop
#define mmu_btop(P) ((P) >> 12)
#define pfn_to_pa(P) ((P) << 12)

#undef PAGESIZE
#define	PAGESIZE 4096

#define CTASSERT(E)
#define ASSERT(E) assert(E)
#define VERIFY(E) assert(E)
#define VERIFY3P(A, B, C) assert(A B C)
#define ASSERT3U(A, B, C) assert(A B C)
#define ASSERT3P(A, B, C) assert(A B C)

#include "../vmm_gpt.c"
#include "../vmm_sol_ept.c"
#include "../vmm_sol_rvi.c"

void
pnode(vmm_gpt_node_t *node, int depth)
{
	char *p = "";
	for (int i = 0; i < depth; i++)
		printf(" ");
	printf("node @pfn:%llx(%d,%d)[", node->vgn_host_pfn, node->vgn_level, node->vgn_index);
	for (int i = 0; i < 512; i++) {
		if (node->vgn_entries[i] != 0) {
			printf("%s%d:%llx", p, i, node->vgn_entries[i]);
			p = ",";
		}
	}
	printf("]\n");
	for (vmm_gpt_node_t *child = node->vgn_children;
	    child != NULL;
	    child = child->vgn_siblings)
	{
		pnode(child, depth + 1);
	}
}

void
ptree(vmm_gpt_t *gpt)
{
	pnode(gpt->vgpt_root, 0);
}

void
pentry(vmm_gpt_t *gpt, uint64_t gpa)
{
	assert(gpt != NULL);
	uint64_t *p = vmm_gpt_lookup(gpt, gpa);
	printf("gpa %llx -> ", gpa);
	if (p == NULL)
		printf("NULL");
	else
		printf("%p (%llx)", p, *p);
	printf("\n");
}

int
main()
{
	uint64_t *entries[MAX_GPT_LEVEL];
	vmm_gpt_t *gpt = rvi_create();

	assert(gpt != NULL);

	vmm_gpt_populate_region(gpt, 0x2000, 0x800000);
	vmm_gpt_populate_entry(gpt, 0x1000);
	vmm_gpt_populate_entry(gpt, 1 << 30);
	vmm_gpt_walk(gpt, 0x1000, entries, MAX_GPT_LEVEL);
	ptree(gpt);
	for (int i = 0; i < MAX_GPT_LEVEL; i++)
		printf("PML%dE @%p = %llx\n", 4 - i, entries[i], 0 /**entries[i]*/);
	vmm_gpt_map(gpt, 0x1000, 2, PROT_READ|PROT_WRITE|PROT_EXEC, 0);
	vmm_gpt_map(gpt, 0x2000, 3, PROT_READ|PROT_WRITE|PROT_EXEC, 0);
	vmm_gpt_map(gpt, 0x200000, 3, PROT_READ|PROT_WRITE|PROT_EXEC, 0);
	vmm_gpt_map(gpt, 1 << 30, 3, PROT_READ|PROT_WRITE|PROT_EXEC, 0);
	pentry(gpt, 0x1000);
	pentry(gpt, 0x2000);
	pentry(gpt, 0x200000);

	vmm_gpt_walk(gpt, 0x1000, entries, MAX_GPT_LEVEL);
	for (int i = 0; i < MAX_GPT_LEVEL; i++)
		printf("PML%dE @%p = %llx\n", 4 - i, entries[i], *entries[i]);

	ptree(gpt);

	vmm_gpt_unmap(gpt, 0x1000);
	vmm_gpt_unmap(gpt, 0x2000);
	ptree(gpt);
	vmm_gpt_unmap(gpt, 0x200000);
	vmm_gpt_unmap(gpt, 1 << 30);

	pentry(gpt, 0x1000);
	pentry(gpt, 0x3000);
	pentry(gpt, 0x300000);

	printf("vacating region\n");
	vmm_gpt_vacate_region(gpt, 0x1000, 0x100000);
	printf("vacated region\n");

	vmm_gpt_free(gpt);
	gpt = NULL;

	uintptr_t p = 0;
	rvi_map_t *map = rvi_ops_create(&p);
	printf("%llx\n", rvi_ops_map(map, 0xfffff000, 0x1015375, 1, 0x5, 0x6));
	ptree(map->rm_gpt);
	pentry(map->rm_gpt, 0xfffff000);
	vmm_gpt_walk(map->rm_gpt, 0xfffff000, entries, MAX_GPT_LEVEL);
	for (int i = 0; i < MAX_GPT_LEVEL; i++)
		printf("PML%dE @%p = %llx\n", 4 - i, entries[i], *entries[i]);
	uint_t prot;
	int r;
	r = rvi_ops_is_wired(map, 0xfffff000, &prot);
	printf("rvi_os_is_wired(map, 0xfffff000, &prot) = %d, prot = %x\n", r, prot);
	r = vmm_gpt_is_mapped(map->rm_gpt, 0xfffff000, &prot);
	printf("vmm_gpt_is_mapped(map->rm_gpt, 0xfffff000, &prot) = %d, prot = %x\n", r, prot);
	rvi_ops_unmap(map, 0xfffff000, 0x100000000);
	ptree(map->rm_gpt);
	rvi_ops_destroy(map);

	return 0;
}
