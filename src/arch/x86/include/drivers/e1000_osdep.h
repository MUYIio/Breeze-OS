/*******************************************************************************

  
  Copyright(c) 1999 - 2004 Intel Corporation. All rights reserved.
  
  This program is free software; you can redistribute it and/or modify it 
  under the terms of the GNU General Public License as published by the Free 
  Software Foundation; either version 2 of the License, or (at your option) 
  any later version.
  
  This program is distributed in the hope that it will be useful, but WITHOUT 
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or 
  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for 
  more details.
  
  You should have received a copy of the GNU General Public License along with
  this program; if not, write to the Free Software Foundation, Inc., 59 
  Temple Place - Suite 330, Boston, MA  02111-1307, USA.
  
  The full GNU General Public License is included in this distribution in the
  file called LICENSE.
  
  Contact Information:
  Linux NICS <linux.nics@intel.com>
  Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497

*******************************************************************************/


/* glue for the OS independent part of e1000
 * includes register access macros
 */

#ifndef _E1000_OSDEP_H_
#define _E1000_OSDEP_H_

#include <arch/io.h>
#include <arch/time.h>
#include <arch/pci.h>
#include <xbook/debug.h>
#include <xbook/clock.h>
#include <xbook/timer.h>
#include <stdint.h>

#define usec_delay(x) udelay(x)
#ifndef msec_delay
//#define msec_delay(x) msleep(x)
#define msec_delay(x) mdelay(x)

/* Some workarounds require millisecond delays and are run during interrupt
 * context.  Most notably, when establishing link, the phy may need tweaking
 * but cannot process phy register reads/writes faster than millisecond
 * intervals...and we establish link due to a "link status change" interrupt.
 */
#define msec_delay_irq(x) mdelay(x)
#endif

#define PCI_COMMAND_REGISTER   PCI_STATUS_COMMAND
#define CMD_MEM_WRT_INVALIDATE PCI_COMMAND_INVALIDATE

typedef enum {
#undef FALSE
    FALSE = 0,
#undef TRUE
    TRUE = 1
} boolean_t;

#define DBG 0

#define MSGOUT(S, A, B)	keprint(PRINT_DEBUG S "\n", A, B)

#ifndef DBG
#define DEBUGOUT(S)		keprint(PRINT_DEBUG S "\n")
#define DEBUGOUT1(S, A...)	keprint(PRINT_DEBUG S "\n", A)
#else
#define DEBUGOUT(S)
#define DEBUGOUT1(S, A...)
#endif

#define DEBUGFUNC(F) DEBUGOUT(F)
#define DEBUGOUT2 DEBUGOUT1
#define DEBUGOUT3 DEBUGOUT2
#define DEBUGOUT7 DEBUGOUT3

// static inline void writel(uint32_t val, uint32_t addr)
// {
//     (*(volatile uint32_t*)(addr)) = val;
// }

static inline void writel(uint32_t val, uint8_t* addr)
{
    // uint32_t addr_ = (uint32_t*)addr;
    (*(volatile uint32_t*)(addr)) = val;
}

// static inline uint32_t readl(uint32_t addr)
// {
//     return (*(volatile uint32_t*)(addr));
// }

static inline uint32_t readl(void* addr)
{
    // uint32_t addr_ = (uint32_t*)addr;
    return (*(volatile uint32_t*)(addr));
}

#define E1000_WRITE_REG(a, reg, value) ( \
    writel((value), ((a)->hw_addr + \
        (((a)->mac_type >= e1000_82543) ? E1000_##reg : E1000_82542_##reg))))
/*
// #define E1000_WRITE_REG(a, reg, value) ( \
//     out32( \
//             ( \
//                 (a)->hw_addr + \
//                 (((a)->mac_type >= e1000_82543) ? E1000_##reg : E1000_82542_##reg) \
//             ), \
//             (value) \
//         ) \
// )
*/

#define E1000_READ_REG(a, reg) ( \
    readl((a)->hw_addr + \
        (((a)->mac_type >= e1000_82543) ? E1000_##reg : E1000_82542_##reg)))
/*
// #define E1000_READ_REG(a, reg) ( \
//     in32( \
//             ( \
//                 (a)->hw_addr + \
//                 (((a)->mac_type >= e1000_82543) ? E1000_##reg : E1000_82542_##reg) \
//             ) \
//         ) \
// )
*/

#define E1000_WRITE_REG_ARRAY(a, reg, offset, value) ( \
    writel((value), ((a)->hw_addr + \
        (((a)->mac_type >= e1000_82543) ? E1000_##reg : E1000_82542_##reg) + \
        ((offset) << 2))))
/*
// #define E1000_WRITE_REG_ARRAY(a, reg, offset, value) ( \
//     out32( \  
//             ( \
//                 (a)->hw_addr + \
//                 (((a)->mac_type >= e1000_82543) ? E1000_##reg : E1000_82542_##reg) + \
//                 ((offset) << 2) \
//             ), \
//             (value) \
//         ) \
// )
*/

#define E1000_READ_REG_ARRAY(a, reg, offset) ( \
    readl((a)->hw_addr + \
        (((a)->mac_type >= e1000_82543) ? E1000_##reg : E1000_82542_##reg) + \
        ((offset) << 2)))
/*
// #define E1000_READ_REG_ARRAY(a, reg, offset) ( \
//     in32( \  
//             ( \
//                 (a)->hw_addr + \
//                 (((a)->mac_type >= e1000_82543) ? E1000_##reg : E1000_82542_##reg) + \
//                 ((offset) << 2) \
//             ) \
//         ) \
// )
*/

#define E1000_WRITE_FLUSH(a) E1000_READ_REG(a, STATUS)

#endif /* _E1000_OSDEP_H_ */
