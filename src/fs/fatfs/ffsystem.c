/*------------------------------------------------------------------------*/
/* Sample Code of OS Dependent Functions for FatFs                        */
/* (C)ChaN, 2018                                                          */
/*------------------------------------------------------------------------*/


#include "ff.h"
#include <xbook/memalloc.h>
#include <xbook/walltime.h>

#if FF_USE_LFN == 3	/* Dynamic memory allocation */

/*------------------------------------------------------------------------*/
/* Allocate a memory block                                                */
/*------------------------------------------------------------------------*/

void* ff_memalloc (	/* Returns pointer to the allocated memory block (null if not enough core) */
	UINT msize		/* Number of bytes to allocate */
)
{
	return mem_alloc(msize);	/* Allocate a new memory block with POSIX API */
}


/*------------------------------------------------------------------------*/
/* Free a memory block                                                    */
/*------------------------------------------------------------------------*/

void ff_memfree (
	void* mblock	/* Pointer to the memory block to free (nothing to do if null) */
)
{
	mem_free(mblock);	/* Free the memory block with POSIX API */
}

#endif



#if FF_FS_REENTRANT	/* Mutal exclusion */

/*------------------------------------------------------------------------*/
/* Create a Synchronization Object                                        */
/*------------------------------------------------------------------------*/
/* This function is called in f_mount() function to create a new
/  synchronization object for the volume, such as semaphore and mutex.
/  When a 0 is returned, the f_mount() function fails with FR_INT_ERR.
*/

//const osMutexDef_t Mutex[FF_VOLUMES];	/* Table of CMSIS-RTOS mutex */


int ff_cre_syncobj (	/* 1:Function succeeded, 0:Could not create the sync object */
	BYTE vol,			/* Corresponding volume (logical drive number) */
	FF_SYNC_t* sobj		/* Pointer to return the created sync object */
)
{
	/* Win32 */
	//*sobj = CreateMutex(NULL, FALSE, NULL);
	//return (int)(*sobj != INVALID_HANDLE_VALUE);

    /* xbook2 */
    *sobj = semaphore_alloc(1);
    return (int)(*sobj != NULL);

	/* uITRON */
//	T_CSEM csem = {TA_TPRI,1,1};
//	*sobj = acre_sem(&csem);
//	return (int)(*sobj > 0);

	/* uC/OS-II */
//	OS_ERR err;
//	*sobj = OSMutexCreate(0, &err);
//	return (int)(err == OS_NO_ERR);

	/* FreeRTOS */
//	*sobj = xSemaphoreCreateMutex();
//	return (int)(*sobj != NULL);

	/* CMSIS-RTOS */
//	*sobj = osMutexCreate(&Mutex[vol]);
//	return (int)(*sobj != NULL);
}


/*------------------------------------------------------------------------*/
/* Delete a Synchronization Object                                        */
/*------------------------------------------------------------------------*/
/* This function is called in f_mount() function to delete a synchronization
/  object that created with ff_cre_syncobj() function. When a 0 is returned,
/  the f_mount() function fails with FR_INT_ERR.
*/

int ff_del_syncobj (	/* 1:Function succeeded, 0:Could not delete due to an error */
	FF_SYNC_t sobj		/* Sync object tied to the logical drive to be deleted */
)
{
	/* Win32 */
	//return (int)CloseHandle(sobj);
    
    /* xbook2 */
    return semaphore_free(sobj);
	/* uITRON */
//	return (int)(del_sem(sobj) == E_OK);

	/* uC/OS-II */
//	OS_ERR err;
//	OSMutexDel(sobj, OS_DEL_ALWAYS, &err);
//	return (int)(err == OS_NO_ERR);

	/* FreeRTOS */
//  vSemaphoreDelete(sobj);
//	return 1;

	/* CMSIS-RTOS */
//	return (int)(osMutexDelete(sobj) == osOK);
}


/*------------------------------------------------------------------------*/
/* Request Grant to Access the Volume                                     */
/*------------------------------------------------------------------------*/
/* This function is called on entering file functions to lock the volume.
/  When a 0 is returned, the file function fails with FR_TIMEOUT.
*/

int ff_req_grant (	/* 1:Got a grant to access the volume, 0:Could not get a grant */
	FF_SYNC_t sobj	/* Sync object to wait */
)
{
	/* Win32 */
	//return (int)(WaitForSingleObject(sobj, FF_FS_TIMEOUT) == WAIT_OBJECT_0);

    /* xbook2 */
    return (semaphore_down_timeout(sobj, FF_FS_TIMEOUT) == 0);
    
	/* uITRON */
//	return (int)(wai_sem(sobj) == E_OK);

	/* uC/OS-II */
//	OS_ERR err;
//	OSMutexPend(sobj, FF_FS_TIMEOUT, &err));
//	return (int)(err == OS_NO_ERR);

	/* FreeRTOS */
//	return (int)(xSemaphoreTake(sobj, FF_FS_TIMEOUT) == pdTRUE);

	/* CMSIS-RTOS */
//	return (int)(osMutexWait(sobj, FF_FS_TIMEOUT) == osOK);
}


/*------------------------------------------------------------------------*/
/* Release Grant to Access the Volume                                     */
/*------------------------------------------------------------------------*/
/* This function is called on leaving file functions to unlock the volume.
*/

void ff_rel_grant (
	FF_SYNC_t sobj	/* Sync object to be signaled */
)
{
	/* Win32 */
	//ReleaseMutex(sobj);

    /* xbook2 */
    semaphore_up(sobj);
	
	/* uITRON */
//	sig_sem(sobj);

	/* uC/OS-II */
//	OSMutexPost(sobj);

	/* FreeRTOS */
//	xSemaphoreGive(sobj);

	/* CMSIS-RTOS */
//	osMutexRelease(sobj);
}

#endif

#if FF_FS_NORTC == 0

DWORD get_fattime (void)
{
    DWORD value = (((walltime.year - 1980) & 0x7f) << 25) |
            ((walltime.month & 0xf) << 21) |
            ((walltime.day & 0x1f) << 16) |
            ((walltime.hour & 0x1f) << 11) |
            ((walltime.minute & 0x3f) << 5) |
            ((walltime.second / 2) & 0x1f);
    return value;
}

#endif /* FF_FS_NORTC */