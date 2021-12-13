#ifndef _XBOOK_PROCESS_H
#define _XBOOK_PROCESS_H

#include "task.h"
#include "elf32.h"

#define PROC_CREATE_INIT    0X80000000      /* 只用于INIT进程 */
#define PROC_CREATE_STOP    0X01            /* 创建后停止，不执行 */

task_t *process_create(char **argv, char **envp, uint32_t flags);
int proc_destroy(task_t *task, int thread);
int proc_vmm_init(task_t *task);
int proc_vmm_exit(task_t *task);
int proc_vmm_exit_when_forking(task_t *child, task_t *parent);
int proc_build_arg(unsigned long arg_top, unsigned long *arg_bottom, char *argv[], char **dest_argv[]);
void proc_map_space_init(task_t *task);
int proc_load_image(vmm_t *vmm, struct Elf32_Ehdr *elf_header, int fd);
void proc_trap_frame_init(task_t *task);
int proc_release(task_t *task);
int proc_pthread_init(task_t *task);
int proc_pthread_exit(task_t *task);
void proc_exec_init(task_t *task);

int proc_res_init(task_t *task);
int proc_res_exit(task_t *task);

int proc_deal_zombie_child(task_t *parent);
void proc_close_one_thread(task_t *thread);
void proc_close_other_threads(task_t *thread);

int sys_fork();
pid_t sys_waitpid(pid_t pid, int *status, int options);
int sys_execve(const char *pathname, const char *argv[], const char *envp[]);
void sys_exit(int status);
unsigned long sys_sleep(unsigned long second);
int sys_create_process(char **argv, char **envp, uint32_t flags);
int sys_resume_process(pid_t pid);

#endif /* _XBOOK_PROCESS_H */
