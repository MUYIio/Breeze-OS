#ifndef _X86_TSS_H
#define _X86_TSS_H

#include <stdint.h>

typedef struct {
	uint32_t backlink;
	uint32_t esp0;	//we will use esp 
	uint32_t ss0;	//stack segment
	uint32_t esp1;
	uint32_t ss1;
	uint32_t esp2;
	uint32_t ss2;
	uint32_t cr3;
	uint32_t eip;
	uint32_t eflags;
	uint32_t eax;
	uint32_t ecx;
	uint32_t edx;
	uint32_t ebx;
	uint32_t esp;
	uint32_t ebp;
	uint32_t esi;
	uint32_t edi;
	uint32_t es;
	uint32_t cs;
	uint32_t ss;
	uint32_t ds;
	uint32_t fs;
	uint32_t gs;
	uint32_t ldtr;
	uint32_t trap;
	uint32_t iobase;
} tss_t;

void tss_init();
tss_t *tss_get_from_cpu0();
void tss_update_info(unsigned long task_addr);

#endif	/*_X86_CPU_H*/