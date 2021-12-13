#include <xbook/debug.h>
#include <xbook/bitops.h>
#include <string.h>

#include <xbook/driver.h>
#include <assert.h>
#include <xbook/byteorder.h>
#include <xbook/spinlock.h>
#include <math.h>
#include <xbook/waitqueue.h>
#include <xbook/memalloc.h>
#include <arch/io.h>
#include <xbook/hardirq.h>
#include <arch/pci.h>
#include <arch/atomic.h>
#include <arch/cpu.h>
#include <sys/ioctl.h>
#include <stddef.h>
#include <xbook/virmem.h>

#include <drivers/e1000_hw.h>
#include <drivers/e1000_osdep.h>
#include <drivers/e1000.h>

#define DRV_VERSION "v0.1"
#define DRV_NAME "net-e1000" DRV_VERSION

#define DEV_NAME "e1000"
#define E1000_VENDOR_ID 0x8086

// #define DEBUG_DRV

#ifndef NET_IP_ALIGN
#define NET_IP_ALIGN 2
#endif

/*以太网的情况*/
#define ETH_ALEN 6 /*以太网地址，即MAC地址，6字节*/
#define ETH_ZLEN 60 /*不含CRC校验的数据最小长度*/
#define ETH_DATA_LEN 1500 /*帧内数据的最大长度*/
#define ETH_FRAME_LEN 1514 /*不含CRC校验和的最大以太网数据长度*/

/*  设备状态 */
// struct net_device_status {
//     /* 错误记录 */
//     unsigned long tx_errors;         /* 传输错误记录 */
//     unsigned long tx_aborted_errors;  /* 传输故障记录 */
//     unsigned long tx_carrier_errors;  /* 传输携带错误记录 */
//     unsigned long tx_window_errors;   /* 传输窗口错误记录 */
//     unsigned long tx_fifo_errors;     /* 传输fifo错误记录 */
//     unsigned long tx_dropped;        /* 传输丢弃记录 */

//     unsigned long rx_errors;         /* 接收错误 */    
//     unsigned long rx_length_errors;   /* 接收长度错误 */
//     unsigned long rx_missed_errors;   /* 接收丢失错误 */
//     unsigned long rx_fifo_errors;     /* 接收Fifo错误 */
//     unsigned long rx_crc_errors;      /* 接收CRC错误 */
//     unsigned long rx_frame_errors;    /* 接收帧错误 */
//     unsigned long rx_dropped;        /* 接收丢弃记录 */
//     /* 正确记录 */
//     unsigned long tx_packets;        /* 传输包数量 */
//     unsigned long tx_bytes;          /* 传输字节数量 */
    
//     unsigned long collisions;       /* 碰撞次数 */
// };

/* 设备拓展内容 */
typedef struct _e1000_extension {
    device_object_t *device_object;   //设备对象

    timer_t tx_fifo_stall_timer;
    timer_t watchdog_timer;
    timer_t phy_info_timer;

    uint32_t wol;
    uint16_t link_speed;
    uint16_t link_duplex;
    
    /* TX */
	struct e1000_desc_ring tx_ring;
	spinlock_t tx_lock;
	uint32_t txd_cmd;
	uint32_t tx_int_delay;
	uint32_t tx_abs_int_delay;
	uint32_t gotcl;
	uint64_t gotcl_old;
	uint64_t tpt_old;
	uint64_t colc_old;
	uint32_t tx_fifo_head;
	uint32_t tx_head_addr;
	uint32_t tx_fifo_size;
	atomic_t tx_fifo_stall;
	boolean_t pcix_82544;

    /* RX */
	struct e1000_desc_ring rx_ring;
	uint64_t hw_csum_err;
	uint64_t hw_csum_good;
	uint32_t rx_int_delay;
	uint32_t rx_abs_int_delay;
	boolean_t rx_csum;
	uint32_t gorcl;
	uint64_t gorcl_old;

    /* Interrupt Throttle Rate */
	uint32_t itr;

    /* structs defined in e1000_hw.h */
    struct e1000_hw hw;
    struct e1000_hw_stats stats;
    struct e1000_phy_info phy_info;
    struct e1000_phy_stats phy_stats;

    uint32_t part_num;

    uint32_t io_addr;   //IO基地址
    int drv_flags;   //驱动标志
    uint32_t irq;
    flags_t flags;

    pci_device_t* pci_device;

    uint32_t dev_features;   //设备结构特征

    uint32_t rx_buffer_len;
    uint32_t max_frame_size;
    uint32_t min_frame_size;
    
    spinlock_t stats_lock;
    atomic_t irq_sem;

    // uint8_t *rx_buffer;   //接收缓冲区
    // uint8_t *rx_ring;   //接收环
    // uint8_t current_rx;
    // flags_t rx_flags;
    // dma_addr_t rx_ring_dma;   //dma物理地址

    spinlock_t lock;   //普通锁
    spinlock_t rx_lock;   //接收锁

    device_queue_t rx_queue;   //接收队列
}e1000_extension_t;

iostatus_t e1000_driver_func(driver_object_t* driver);

iostatus_t e1000_up(e1000_extension_t* ext);
void e1000_down(e1000_extension_t* ext);
int e1000_reset(e1000_extension_t* ext);
int e1000_transmit(e1000_extension_t* ext, uint8_t* buf, uint32_t len);
iostatus_t e1000_setup_tx_resources(e1000_extension_t* ext);
iostatus_t e1000_setup_rx_resources(e1000_extension_t* ext);

void e1000_free_rx_resources(e1000_extension_t* ext);
void e1000_free_tx_resources(e1000_extension_t* ext);
void e1000_pci_clear_mwi(struct e1000_hw* hw);
void e1000_pci_set_mwi(struct e1000_hw* hw);

static iostatus_t e1000_enter(driver_object_t* driver);
static iostatus_t e1000_exit(driver_object_t* driver);
static iostatus_t e1000_read(device_object_t* device, io_request_t* ioreq);
static iostatus_t e1000_write(device_object_t* device, io_request_t* ioreq);
static iostatus_t e1000_devctl(device_object_t* device, io_request_t* ioreq);
static int e1000_init(e1000_extension_t* ext);
static int e1000_get_pci_info(e1000_extension_t* ext);
static int e1000_sw_init(e1000_extension_t* ext);
static int e1000_init_board(e1000_extension_t* ext);
static iostatus_t e1000_open(device_object_t* device, io_request_t* ioreq);
static iostatus_t e1000_close(device_object_t* device, io_request_t* ioreq);
static void e1000_configure_tx(e1000_extension_t* ext);
static void e1000_configure_rx(e1000_extension_t* ext);
static void e1000_setup_rctl(e1000_extension_t* ext);
static void e1000_set_multi(device_object_t* netdev);
static void e1000_enter_82542_rst(e1000_extension_t* ext);
static void e1000_leave_82542_rst(e1000_extension_t* ext);
static void e1000_clean_tx_ring(e1000_extension_t* ext);
static void e1000_clean_rx_ring(e1000_extension_t* ext);
static void e1000_alloc_rx_buffers(e1000_extension_t* ext);
static int e1000_intr(irqno_t irq, void *data);
static boolean_t e1000_clean_tx_irq(e1000_extension_t* ext);
#ifdef CONFIG_E1000_NAPI
static int e1000_clean(struct net_device *netdev, int *budget);
static boolean_t e1000_clean_rx_irq(e1000_extension_t* ext,
                                    int *work_done, int work_to_do);
#else
static boolean_t e1000_clean_rx_irq(e1000_extension_t* ext);
#endif

static inline void e1000_irq_disable(e1000_extension_t* ext);

uint32_t e1000_io_read(struct e1000_hw *hw, unsigned long port);
void e1000_io_write(struct e1000_hw *hw, unsigned long port, uint32_t value);


/**
 * 1. 申请pci结构(pci_device_t)并初始化厂商号和设备号
 * 2. 启动总线控制
 * 3. 申请设备io空间，在pci_device中登记io空间基地址
 * 4. 申请中断，并在pci_device中登记中断号
**/
static int e1000_get_pci_info(e1000_extension_t* ext)
{
    unsigned long mmio_start = 0;
    int mmio_len;
    /* get pci device */
    pci_device_t* pci_device = pci_get_device(PCI_VENDOR_ID_INTEL, E1000_DEV_ID_82540EM);
    if(pci_device == NULL) {
        keprint(PRINT_DEBUG "E1000_82540EM init failed: pci_get_device.\n");
        return -1;
    }
    ext->pci_device = pci_device;
#ifdef DEBUG_DRV    
    keprint(PRINT_DEBUG "find E1000_82540EM device, vendor id: 0x%x, device id: 0x%x\n",\
            device->vendor_id, device->device_id);
#endif

    /* enable bus mastering */
    pci_enable_bus_mastering(pci_device);

    /* get mem_addr */
    mmio_start = pci_device_get_mem_addr(pci_device);
    mmio_len = pci_device_get_mem_len(pci_device);

    // keprint(PRINT_DEBUG "mmio_start=%x\n", mmio_start);
    // keprint(PRINT_DEBUG "mmio_len=%x\n", mmio_len);

    ext->hw.hw_addr = (uint8_t*)memio_remap(mmio_start, mmio_len);

    /* get io address */
    ext->io_addr = pci_device_get_io_addr(pci_device);
    ext->hw.io_base = pci_device_get_io_addr(pci_device);
    // keprint(PRINT_DEBUG "io_base = %d\n", ext->hw.io_base);
    if(ext->io_addr == 0) {
        keprint(PRINT_DEBUG "E1000_82540EM init failed: INVALID pci device io address.\n");
        return -1;
    }
#ifdef DEBUG_DRV
    keprint(PRINT_DEBUG "E1000_81540EM io address: 0x%x\n", ext->io_addr);
#endif
    /* get irq */
    ext->irq = pci_device_get_irq_line(pci_device);
    if(ext->irq == 0xff) {
        keprint(PRINT_DEBUG "E1000_82540EM init failed: INVALID irq.\n");
        return -1;
    }
#ifdef DEBUG_DRV
    keprint(PRINT_DEBUG "E1000_82540EM irq: %d\n", ext->irq);
#endif
    return 0;
}

/**
 * e1000_sw_init - Initialize general software structures (e1000_extension_t)
 * @ext: board private structure to initialize
 *
 * e1000_sw_init initializes the Adapter private data structure.
 * Fields are initialized based on PCI device information and
 * OS network device settings (MTU size).
 **/

static int e1000_sw_init(e1000_extension_t* ext)
{
    struct e1000_hw* hw = &ext->hw;
    pci_device_t* pdev = ext->pci_device;

    /* pci config space info */
    hw->vendor_id = pdev->vendor_id;
    hw->device_id = pdev->device_id;
    hw->subsystem_vendor_id = pdev->subsystem_vendor_id;
    hw->subsystem_id = pdev->subsystem_device_id;
    hw->revision_id = pdev->revision_id;
    hw->pci_cmd_word = pdev->command;

    ext->tx_abs_int_delay = 0;

    ext->rx_buffer_len = E1000_RXBUFFER_2048;
    ext->rx_ring.count = 128;
    ext->tx_ring.count = 128;
    // hw->max_frame_size = ENET_HEADER_SIZE + ETHERNET_FCS_SIZE;
    // hw->min_frame_size = MINIMUM_ETHERNET_FRAME_SIZE;

    /* identify the MAC，根据pci_device_id确定mac类型 */
    if(e1000_set_mac_type(hw)) {
        keprint(PRINT_DEBUG "Unknown MAC type\n");
        return -1;
    }

    /* initialize eeprom parameters，根据mac类型确定eeprom信息*/
    e1000_init_eeprom_params(hw);

    switch(hw->mac_type) {
        default:
            break;
        case e1000_82541:
        case e1000_82547:
        case e1000_82541_rev_2:
        case e1000_82547_rev_2:
            hw->phy_init_script = 1;
            break;
    }

    /* 根据mac_type确定media_type*/
    e1000_set_media_type(hw);

    hw->wait_autoneg_complete = FALSE;
    hw->tbi_compatibility_en = TRUE;
    hw->adaptive_ifs = TRUE;

    /* copper options 根据media_type确定mdix,disable_polarity_correction,master_slave*/
    if(hw->media_type == e1000_media_type_copper) {
        hw->mdix = AUTO_ALL_MODES;
        hw->disable_polarity_correction = FALSE;
        hw->master_slave = E1000_MASTER_SLAVE;
    }

    atomic_set(&ext->irq_sem, 1);
    spinlock_init(&ext->stats_lock);
    spinlock_init(&ext->tx_lock);
    spinlock_init(&ext->lock);

    return 0;
}

/**
 * e1000_init_board - 初始化网卡设置
 * @ext: board private structure
 **/

static int e1000_init_board(e1000_extension_t* ext)
{
    /* setup the private structrue */
    if(e1000_sw_init(ext)) {
        return -1;
    }

    /* 网卡复位 */
    if(e1000_reset_hw(&ext->hw) != E1000_SUCCESS) {
        keprint(PRINT_DEBUG "e1000_reset_hw failed\n");
        return -1;
    }

    /* 确保eeprom良好 */
    if(e1000_validate_eeprom_checksum(&ext->hw) < 0) {
        keprint(PRINT_DEBUG "the eeprom checksum is not valid\n");
        return -1;
    }

    return 0;
}

void e1000_down(e1000_extension_t* ext)
{
    e1000_irq_disable(ext);
    irq_unregister(ext->irq, ext);
    timer_del(&ext->tx_fifo_stall_timer);
    timer_del(&ext->watchdog_timer);
    timer_del(&ext->phy_info_timer);
    ext->link_speed = 0;
    ext->link_duplex = 0;
    
    e1000_reset(ext);
    e1000_clean_tx_ring(ext);
    e1000_clean_rx_ring(ext);

    /* if wol is not enabled
     * power down the phy so no link is implied when inter face is down */
    if(!ext->wol && ext->hw.media_type == e1000_media_type_copper) {
        uint16_t mii_reg;
        e1000_read_phy_reg(&ext->hw, PHY_CTRL, &mii_reg);
        mii_reg |= MII_CR_POWER_DOWN;
        e1000_write_phy_reg(&ext->hw, PHY_CTRL, mii_reg);
    }
}

int e1000_reset(e1000_extension_t* ext)
{
    uint32_t pba;

    DEBUGFUNC("e1000_reset");

	/* Repartition Pba for greater than 9k mtu
	 * To take effect CTRL.RST is required.
	 */
    if(ext->hw.mac_type < e1000_82547) {
        if(ext->rx_buffer_len > E1000_RXBUFFER_8192) {
            pba = E1000_PBA_40K;
        } else {
            pba = E1000_PBA_48K;
        }
    } else {
        if(ext->rx_buffer_len > E1000_RXBUFFER_8192) {
            pba = E1000_PBA_22K;
        } else {
            pba = E1000_PBA_30K;
        }
        ext->tx_fifo_head = 0;
        ext->tx_head_addr = pba << E1000_TX_HEAD_ADDR_SHIFT;
        ext->tx_fifo_size = 
            (E1000_PBA_40K - pba) << E1000_PBA_BYTES_SHIFT;
        atomic_set(&ext->tx_fifo_stall, 0);
    }
    E1000_WRITE_REG(&ext->hw, PBA, pba);

    /* flow control settings */
    ext->hw.fc_high_water = (pba << E1000_PBA_BYTES_SHIFT) - E1000_FC_HIGH_DIFF;
    ext->hw.fc_low_water = (pba << E1000_PBA_BYTES_SHIFT) - E1000_FC_LOW_DIFF;
    ext->hw.fc_pause_time = E1000_FC_PAUSE_TIME;
    ext->hw.fc = ext->hw.original_fc;

    e1000_reset_hw(&ext->hw);
    if(ext->hw.mac_type >= e1000_82544) {
        E1000_WRITE_REG(&ext->hw, WUC, 0);
    }
    if(e1000_init_hw(&ext->hw)) {
        keprint(PRINT_DEBUG "Hardware error\n");
    }

    /* Enable h/w to recognize an 802.1Q VLAN Ethernet packet */
    E1000_WRITE_REG(&ext->hw, VET, ETHERNET_IEEE_VLAN_TYPE);

    e1000_reset_adaptive(&ext->hw);
    e1000_phy_get_info(&ext->hw, &ext->phy_info);

    DEBUGFUNC("e1000_reset done\n");

    return 0;
}

/**
 * e1000_init - 初始化
 * @ext: board private structure
 **/

static int e1000_init(e1000_extension_t* ext)
{
    uint16_t eeprom_data;

    assert(ext);

    pci_device_t* pdev = ext->pci_device;


    /* 对版本进行检测，仅支持82540em网卡 */
    if(pdev->vendor_id != E1000_VENDOR_ID && pdev->device_id != E1000_DEV_ID_82540EM) {
        keprint(PRINT_DEBUG "this deiver only support 82540em netcard.\n");
        return -1;
    }

    /* 初始化版本设定 */
    if(e1000_init_board(ext)) {
        return -1;
    }

    /* 复制网卡中的mac地址*/
    if(e1000_read_mac_addr(&ext->hw)) {
        keprint(PRINT_DEBUG "EEPEOM read error\n");
        return -1;
    }
    //打印mac地址
    keprint(PRINT_DEBUG "e1000_mac_addr:");
    int i;
    for(i=0; i<5; i++) {
        keprint(PRINT_DEBUG "%x-", ext->hw.mac_addr[i]);
    }
    keprint(PRINT_DEBUG "%x\n", ext->hw.mac_addr[5]);

    e1000_read_part_num(&ext->hw, &(ext->part_num));

    /* 获取总线类型和速度 */
    e1000_get_bus_info(&ext->hw);

    /* Initial Wake on LAN setting
	 * If APM wake is enabled in the EEPROM,
	 * enable the ACPI Magic Packet filter
	 */
	switch(ext->hw.mac_type) {
	case e1000_82542_rev2_0:
	case e1000_82542_rev2_1:
	case e1000_82543:
		break;
	case e1000_82546:
	case e1000_82546_rev_3:
		if((E1000_READ_REG(&ext->hw, STATUS) & E1000_STATUS_FUNC_1)
		   && (ext->hw.media_type == e1000_media_type_copper)) {
			e1000_read_eeprom(&ext->hw,
				EEPROM_INIT_CONTROL3_PORT_B, 1, &eeprom_data);
			break;
		}
		/* Fall Through */
	default:
		e1000_read_eeprom(&ext->hw,
			EEPROM_INIT_CONTROL3_PORT_A, 1, &eeprom_data);
		break;
	}

    /* reset the hardware with the new setting */
    e1000_reset(ext);

    keprint(PRINT_DEBUG "Intel(R) PRO/1000 Network Connection\n");

    return 0;
}

/**
 * e1000_free_rx_resources - Free Rx Resources
 * @ext: board private structure
 *
 * Free all receive software resources
 **/

static inline void
e1000_free_tx_resource(struct e1000_buffer* buffer_info)
{
    if(buffer_info->buffer) {
        mem_free(buffer_info->buffer);
        buffer_info->buffer = NULL;
    }
}

/**
 * e1000_clean_tx_ring - Free Tx Buffers
 * @ext: board private structure
 **/

static void e1000_clean_tx_ring(e1000_extension_t* ext)
{
    struct e1000_desc_ring* tx_ring = &ext->tx_ring;
    struct e1000_buffer* buffer_info;
    unsigned long size;
    unsigned int i;

    /* free all the tx ring buffers*/
    for(i=0; i<tx_ring->count; i++) {
        buffer_info = &tx_ring->buffer_info[i];
        e1000_free_tx_resource(buffer_info);
    }

    size = sizeof(struct e1000_buffer) * tx_ring->count;
    memset(tx_ring->buffer_info, 0, size);

    /* zero out the descriptor ring */
    memset(tx_ring->desc, 0, tx_ring->size);

    tx_ring->next_to_use = 0;
    tx_ring->next_to_clean = 0;

    /* zero out tx descrptor regester */
    E1000_WRITE_REG(&ext->hw, TDH, 0);
    E1000_WRITE_REG(&ext->hw, TDT, 0);
}

/**
 * e1000_free_tx_resources - Free Tx Resources
 * @ext: board private structure
 *
 * Free all transmit software resources
 **/

void e1000_free_tx_resources(e1000_extension_t* ext)
{
    e1000_clean_tx_ring(ext);
    
    vir_mem_free(ext->tx_ring.buffer_info);
    ext->tx_ring.buffer_info = NULL;
    
    mem_free(ext->tx_ring.desc);
    
    ext->tx_ring.desc = NULL;
}

/**
 * e1000_clean_rx_ring - Free Rx Buffers
 * @ext: board private structure
 **/

static void e1000_clean_rx_ring(e1000_extension_t* ext)
{
    struct e1000_desc_ring* rx_ring = &ext->rx_ring;
    struct e1000_buffer* buffer_info;
    unsigned long size;
    unsigned int i;

    /* free all the rx ring buffers */
    for(i=0; i<rx_ring->count; i++) {
        buffer_info = rx_ring->buffer_info;
        if(buffer_info->buffer) {
            mem_free(buffer_info->buffer);
            buffer_info->buffer = NULL;
        }
    }

    size = sizeof(struct e1000_buffer) * rx_ring->count;
    memset(rx_ring->buffer_info, 0, size);

    memset(rx_ring->desc, 0, rx_ring->size);

    rx_ring->next_to_clean = 0;
    rx_ring->next_to_use = 0;

    E1000_WRITE_REG(&ext->hw, RDH, 0);
    E1000_WRITE_REG(&ext->hw, RDT, 0);
}

/**
 * e1000_free_rx_resources - Free Rx Resources
 * @ext: board private structure
 *
 * Free all receive software resources
 **/

void e1000_free_rx_resources(e1000_extension_t* ext)
{
    struct e1000_desc_ring* rx_ring = &ext->rx_ring;
    
    e1000_clean_rx_ring(ext);
    
    mem_free(rx_ring->buffer_info);
    rx_ring->buffer_info = NULL;

    rx_ring->desc = NULL;
}

/**
 * e1000_setup_tx_resources - allocate Tx resources (Descriptors)
 * @ext: board private structure
 *
 * Return 0 on success, negative on failure
 **/

iostatus_t e1000_setup_tx_resources(e1000_extension_t* ext)
{
    struct e1000_desc_ring* txdr = &ext->tx_ring;
    // keprint(PRINT_DEBUG "tx_ring->count = %d\n", txdr->count);
    // pci_device_t* pdev = ext->pci_device;
    int size;

    DEBUGFUNC("e1000_setup_tx_rexources start");

    size = sizeof(struct e1000_buffer) * txdr->count + 1;
    // keprint(PRINT_DEBUG "-------size = %d\n", size);
    txdr->buffer_info = mem_alloc(size);
    if(!txdr->buffer_info) {
        keprint(PRINT_DEBUG "---Unable to allocate memory for the transmit desciptor ring\n");
        return -1;
    }
    memset(txdr->buffer_info, 0, size);

    /* round up to nearest 4K */
    txdr->size = txdr->count * sizeof(struct e1000_tx_desc);
    E1000_ROUNDUP(txdr->size, 4096);

    /* 申请DMA空间 */
    txdr->desc = mem_alloc(txdr->size);
    if(!txdr->desc) {
        keprint(PRINT_DEBUG "unable to allocate memory the transmit descriptor ring\n");
        mem_free(txdr->buffer_info);
        return -1;
    }
    txdr->dma = kern_vir_addr2phy_addr(txdr->desc);
    // keprint(PRINT_DEBUG "-------txdr_dma = %x txdr_size = %d\n", txdr->dma, txdr->size);
    memset(txdr->desc, 0, txdr->size);

    txdr->next_to_use = 0;
    txdr->next_to_clean = 0;

    DEBUGFUNC("e1000_setup_tx_resources done\n");

    return IO_SUCCESS;
}

/**
 * e1000_setup_rx_resources - allocate Rx resources (Descriptors)
 * @ext: board private structure
 *
 * Returns 0 on success, negative on failure
 **/

iostatus_t e1000_setup_rx_resources(e1000_extension_t* ext)
{
    struct e1000_desc_ring* rxdr = &ext->rx_ring;
    int size;

    DEBUGFUNC("e1000_setup_rx_resources start");

    size = sizeof(struct e1000_buffer) * rxdr->count + 1;
    rxdr->buffer_info = mem_alloc(size);
    if(!rxdr->buffer_info) {
        keprint(PRINT_DEBUG "unable to allocate memory for the recieve descriptor ring\n");
        return -1;
    }
    memset(rxdr->buffer_info, 0, size);

    /* round up to mearset 4K */
    rxdr->size = rxdr->count * sizeof(struct e1000_rx_desc);
    E1000_ROUNDUP(rxdr->size, 4096);

    /* 分配DMA空间 */
    rxdr->desc = mem_alloc(rxdr->size);
    if(!rxdr->desc) {
        keprint(PRINT_DEBUG "unable to allocate memory for the recieve descriptor ring\n");
        return -1;
    }
    rxdr->dma = kern_vir_addr2phy_addr(rxdr->desc);
    memset(rxdr->desc, 0, rxdr->size);
    // keprint(PRINT_DEBUG "-------rxdr_dma = %x rxdr_size = %d\n", rxdr->dma, rxdr->size);

    rxdr->next_to_clean = 0;
    rxdr->next_to_use = 0;
    
    DEBUGFUNC("e1000_setup_rx_resources done\n");

    return IO_SUCCESS;
}

/**
 * e1000_irq_disable - Mask off interrupt generation on the NIC
 * @ext: board private structure
 **/

static inline void
e1000_irq_disable(e1000_extension_t* ext)
{
    E1000_WRITE_REG(&ext->hw, IMC, ~0);
    E1000_WRITE_FLUSH(&ext->hw);
}

/**
 * e1000_irq_enable - Enable default interrupt generation settings
 * @ext: board private structure
 **/

static inline void
e1000_irq_enable(e1000_extension_t* ext)
{
    E1000_WRITE_REG(&ext->hw, IMS, IMS_ENABLE_MASK);
    E1000_WRITE_FLUSH(&ext->hw);
}

/**
 * e1000_up - 启动网卡
 * @ext: board private structure
 **/

iostatus_t e1000_up(e1000_extension_t* ext)
{
    device_object_t* netdev = ext->device_object;
    iostatus_t err;

    /* hardware has been reset, we need to reload some things */

    /* reset the PHY if it was previously powered down */
    if(ext->hw.media_type == e1000_media_type_copper) {
        uint16_t mii_reg;
        e1000_read_phy_reg(&ext->hw, PHY_CTRL, &mii_reg);
        if(mii_reg & MII_CR_POWER_DOWN) {
            e1000_phy_reset(&ext->hw);
        }
    }

    e1000_set_multi(netdev);

    e1000_configure_tx(ext);
    e1000_setup_rctl(ext);
    e1000_configure_rx(ext);
    e1000_alloc_rx_buffers(ext);

    if((err = irq_register(ext->irq, e1000_intr, IRQF_SHARED, "IRQ-Network", DEV_NAME, (void *) ext))) {
        return err;
    }

    timer_modify(&ext->watchdog_timer, systicks);
    e1000_irq_enable(ext);

    return IO_SUCCESS;
}

/**
 * e1000_open - Called when a network interface is made active
 * @device: network interface device structure
 * @ioreq: I/O request
 *
 * Returns 0 on success, negative value on failure
 *
 * The open entry point is called when a network interface is made
 * active by the system (IFF_UP).  At this point all resources needed
 * for transmit and receive operations are allocated, the interrupt
 * handler is registered with the OS, the watchdog timer is started,
 * and the stack is notified that the interface is ready.
 **/

static iostatus_t e1000_open(device_object_t* device, io_request_t* ioreq)
{
    DEBUGFUNC("e1000_open start");
    e1000_extension_t* ext = device->device_extension;
    iostatus_t err;

    /* allocate transmit descriptors */
    /* 分配传输描述符 */
    if((err = e1000_setup_tx_resources(ext))) {
        goto err_setup_tx;
    }

    /* allocate receive descriptors */
    /* 分配接受描述符 */
    if((err = e1000_setup_rx_resources(ext))) {
        goto err_setup_rx;
    }

    if((err = e1000_up(ext))) {
        goto err_up;
    }
    ext->flags = 0;
    ioreq->io_status.status = IO_SUCCESS;
    ioreq->io_status.infomation = 0;
    io_complete_request(ioreq);
    
    DEBUGFUNC("e1000_open done\n");

    return err;

err_up:
    e1000_free_rx_resources(ext);
err_setup_rx:
    e1000_free_tx_resources(ext);
err_setup_tx:
    e1000_reset(ext);

    return err;
}

/**
 * e1000_close - Disables a network interface
 * @device: network interface device structure
 * @ioreq: I/O request
 *
 * Returns 0, this is not allowed to fail
 *
 * The close entry point is called when an interface is de-activated
 * by the OS.  The hardware is still under the drivers control, but
 * needs to be disabled.  A global MAC reset is issued to stop the
 * hardware, and all transmit and receive resources are freed.
 **/

static iostatus_t e1000_close(device_object_t* device, io_request_t* ioreq)
{
    e1000_extension_t* ext = device->device_extension;

    e1000_down(ext);

    e1000_free_tx_resources(ext);
    e1000_free_rx_resources(ext);

    ioreq->io_status.status = IO_SUCCESS;
    ioreq->io_status.infomation = 0;
    io_complete_request(ioreq);

    return IO_SUCCESS;
}

static iostatus_t e1000_read(device_object_t* device, io_request_t* ioreq)
{
    unsigned long len;
    e1000_extension_t* ext = device->device_extension;
    iostatus_t status = IO_SUCCESS;

    uint8_t* buf = (uint8_t*)ioreq->user_buffer;
    int flags = 0;

    len = ioreq->parame.read.length;

    if(ext->flags & DEV_NOWAIT) {
        flags |= IO_NOWAIT;
    }

    /* 从网络接收队列中获取一个包 */
    //len = io_device_queue_pickup(&ext->rx_ring.buffer_info->buffer, buf, len, flags);
    len = io_device_queue_pickup(&ext->rx_queue, buf, len, flags);
    if(len < 0) {
        status = IO_FAILED;
    }

    ioreq->io_status.status = status;
    ioreq->io_status.infomation = len;

    io_complete_request(ioreq);
    return status;
}

static iostatus_t e1000_write(device_object_t* device, io_request_t* ioreq)
{
    unsigned long len = ioreq->parame.write.length;

    uint8_t* buf = (uint8_t*)ioreq->user_buffer;

    ioreq->io_status.status = IO_SUCCESS;
    ioreq->io_status.infomation = len;

    // for(int i=0; i<len; i++) {
    //     keprint(PRINT_DEBUG "%-4x ", *(buf+i));
    //     if(!((i + 1) % 16)) {
    //         keprint(PRINT_DEBUG "\n");
    //     }
    // }
    // keprint(PRINT_DEBUG "len = %d\n", len);

    if(e1000_transmit(device->device_extension, buf, len)) {
        len = -1;
    }

    ioreq->io_status.status = IO_SUCCESS;
    ioreq->io_status.infomation = len;
    io_complete_request(ioreq);

    return IO_SUCCESS;
}

static iostatus_t e1000_devctl(device_object_t* device, io_request_t* ioreq)
{
    unsigned int ctlcode = ioreq->parame.devctl.code;
    unsigned long arg = ioreq->parame.devctl.arg;
    e1000_extension_t* ext = (e1000_extension_t*)device->device_extension;
    iostatus_t status;
    unsigned char* mac;
    int i;

    switch(ctlcode) {
        case NETIO_GETMAC:
            mac = (unsigned char*)arg;
            for(i=0; i<6; i++) {
                *mac++ = ext->hw.mac_addr[i];
            }
            status = IO_SUCCESS;
            break;
        case NETIO_SETFLGS:
            ext->flags = *((unsigned long *) arg);
            break;
        case NETIO_GETFLGS:
            *((unsigned long *) arg) = ext->flags;
            break;
        default:
            status = IO_FAILED;
            break;
    }
    ioreq->io_status.status = status;
    ioreq->io_status.infomation = 0;
    io_complete_request(ioreq);
    return status;
}

static iostatus_t e1000_enter(driver_object_t* driver)
{
    iostatus_t status;

    device_object_t* devobj;
    e1000_extension_t* devext;
    
    /* 初始化一些其他内容-初始化devobj(未扩展的内容) */
    status = io_create_device(driver, sizeof(e1000_extension_t), DEV_NAME, DEVICE_TYPE_PHYSIC_NETCARD, &devobj);

    if(status != IO_SUCCESS) {
        keprint(PRINT_DEBUG PRINT_ERR "e1000_enter: create device failed!\n");
        return status;
    }

    /* neither io mode*/
    devobj->flags = 0;

    devext = (e1000_extension_t*)devobj->device_extension;   //设备扩展部分位于devobj的尾部
    devext->device_object = devobj;
    devext->hw.back = devext;
    devext->flags = 0;
    /*初始化接收队列，用内核队列结构保存，等待被读取*/
    io_device_queue_init(&devext->rx_queue);

    /* 申请并初始化pci_device_t，io_addr、中断号*/
    if(e1000_get_pci_info(devext)) {
        status = IO_FAILED;
        io_delete_device(devobj);
        return status;
    }

    if(e1000_init(devext)) {
        status = IO_FAILED;
        io_delete_device(devobj);
        return status;
    }

    return status;
}

static iostatus_t e1000_exit(driver_object_t* driver)
{
    /* 遍历所有对象 */
    device_object_t* devobj;
    device_object_t* next;
    /* 由于涉及要释放devobj，所以需要使用safe版本 */
    list_for_each_owner_safe (devobj, next, &driver->device_list, list) {
        io_delete_device(devobj);   //删除每一个设备
    }

    string_del(&driver->name);   //删除驱动名
    return IO_SUCCESS;
}

iostatus_t e1000_driver_func(driver_object_t* driver)
{
    iostatus_t status = IO_SUCCESS;

    /* 绑定驱动信息 */
    driver->driver_enter = e1000_enter;
    driver->driver_exit = e1000_exit;

    driver->dispatch_function[IOREQ_OPEN] = e1000_open;
    driver->dispatch_function[IOREQ_CLOSE] = e1000_close;
    driver->dispatch_function[IOREQ_READ] = e1000_read;
    driver->dispatch_function[IOREQ_WRITE] = e1000_write;
    driver->dispatch_function[IOREQ_DEVCTL] = e1000_devctl;

    /* 初始化驱动名字 */
    string_new(&driver->name, DRV_NAME, DRIVER_NAME_LEN);
    keprint(PRINT_DEBUG "e1000_driver_vim: driver name=%s\n", driver->name.text);
    
    return status;
}

void e1000_pci_clear_mwi(struct e1000_hw* hw)
{
	e1000_extension_t* ext = hw->back;
    uint32_t value;
    uint16_t cmd;

    cmd = ext->hw.pci_cmd_word & (~(PCI_COMMAND_INVALIDATE));
    value = pci_device_read(ext->pci_device, PCI_STATUS_COMMAND);
    value &= cmd;
    
	pci_device_write(ext->pci_device, PCI_STATUS_COMMAND, value);
}

void e1000_pci_set_mwi(struct e1000_hw* hw)
{
    e1000_extension_t* ext = hw->back;
    uint32_t value;
    uint16_t cmd;

    cmd = ext->hw.pci_cmd_word | PCI_COMMAND_INVALIDATE;
    value = pci_device_read(ext->pci_device, PCI_STATUS_COMMAND);
    value &= cmd;

    pci_device_write(ext->pci_device, PCI_STATUS_COMMAND, value);
}

uint32_t e1000_io_read(struct e1000_hw *hw, unsigned long port)
{
	return in32(port);
}

void e1000_io_write(struct e1000_hw *hw, unsigned long port, uint32_t value)
{
	out32(value, port);
}

/**
 * e1000_set_multi - Multicast and Promiscuous mode set
 * @netdev: network interface device structure
 *
 * The set_multi entry point is called whenever the multicast address
 * list or the network interface flags are updated.  This routine is
 * responsible for configuring the hardware for proper multicast,
 * promiscuous mode, and all-multi behavior.
 **/

static void e1000_set_multi(device_object_t* netdev)
{
    e1000_extension_t* ext = netdev->device_extension;
    struct e1000_hw* hw = &ext->hw;
    uint32_t rctl;
    int i;
    unsigned long flags;

    /* check for promiscuous and all multicast modes */
    /* 检查混杂和所有多播模式 */
    spin_lock_irqsave(&ext->tx_lock, flags);

    rctl = E1000_READ_REG(hw, RCTL);

    //禁用混杂模式
    // rctl |= (E1000_RCTL_UPE | E1000_RCTL_MPE);
    rctl &= ~E1000_RCTL_MPE;
    rctl &= ~E1000_RCTL_UPE;

    E1000_WRITE_REG(hw, RCTL, rctl);

    /* 82542 2.0 needs to be in reset to write receive address registers */

    if(hw->mac_type == e1000_82542_rev2_0) {
        e1000_enter_82542_rst(ext);
    }

    /** load the first 14 multicast address into the exact filters 1-14 
     * RAR 0 is used for the station MAC address
     * if there are not 14 addresses, go ahead and clear the filters
     * **/
    //在1-14接收地址寄存器清零
    for(i=1; i<E1000_RAR_ENTRIES; i++) {
        E1000_WRITE_REG_ARRAY(hw, RA, i << 1, 0);
        E1000_WRITE_REG_ARRAY(hw, RA, (i << 1) + 1, 0);
    }

    /* clear the old setting from the multicast hash table */
    for(i=0; i<E1000_NUM_MTA_REGISTERS; i++) {
        E1000_WRITE_REG_ARRAY(hw, MTA, i, 0);
    }

    /* load any remaining address into the hash table */

    if(hw->mac_type == e1000_82542_rev2_0) {
        e1000_leave_82542_rst(ext);
    }

    spin_unlock_irqrestore(&ext->tx_lock, flags);
}

/* The 82542 2.0 (revision 2) needs to have the receive unit in reset
 * and memory write and invalidate disabled for certain operations
 */
static void e1000_enter_82542_rst(e1000_extension_t* ext)
{
    uint32_t rctl;

    e1000_pci_clear_mwi(&ext->hw);

    rctl = E1000_READ_REG(&ext->hw, RCTL);
    rctl |= E1000_RCTL_RST;
    E1000_WRITE_REG(&ext->hw, RCTL, rctl);
    E1000_WRITE_FLUSH(&ext->hw);
    mdelay(5);

    e1000_clean_rx_ring(ext);
}

static void e1000_leave_82542_rst(e1000_extension_t* ext)
{
    uint32_t rctl;

    rctl = E1000_READ_REG(&ext->hw, RCTL);
    rctl &= ~E1000_RCTL_RST;
    E1000_WRITE_REG(&ext->hw, RCTL, rctl);
    E1000_WRITE_FLUSH(&ext->hw);
    mdelay(5);

    if(ext->hw.pci_cmd_word & PCI_COMMAND_INVALIDATE) {
        e1000_pci_set_mwi(&ext->hw);
    }

    e1000_configure_rx(ext);
    e1000_alloc_rx_buffers(ext);
}

/**
 * e1000_configure_rx - Configure 8254x Receive Unit after Reset
 * @ext: board private structure
 *
 * Configure the Rx unit of the MAC after a reset.
 **/

static void e1000_configure_rx(e1000_extension_t* ext)
{
    uint32_t rdba = ext->rx_ring.dma;
    keprint(PRINT_DEBUG "rdba = %x\n", rdba);
    uint32_t rdlen = ext->rx_ring.count * sizeof(struct e1000_rx_desc);
    uint32_t rctl;
    uint32_t rxcsum;

    /* disable receives while setting up the descriptors */
    rctl = E1000_READ_REG(&ext->hw, RCTL);
    E1000_WRITE_REG(&ext->hw, RCTL, rctl & ~E1000_RCTL_EN);

    /* set the receive delay timer register */
    E1000_WRITE_REG(&ext->hw, RDTR, ext->rx_int_delay);

    if(ext->hw.mac_type >= e1000_82540) {
        E1000_WRITE_REG(&ext->hw, RADV, ext->rx_abs_int_delay);
        if(ext->itr > 1) {
            E1000_WRITE_REG(&ext->hw, ITR, 1000000000/(ext->itr*256));
        }
    }

    /* setup the base and length of the rx descriptor ring */
    // E1000_WRITE_REG(&ext->hw, RDBAL, (rdba & 0x00000000ffffffffUll));
    E1000_WRITE_REG(&ext->hw, RDBAL, rdba);
    // E1000_WRITE_REG(&ext->hw, RDBAH, (rdba >> 32));
    E1000_WRITE_REG(&ext->hw, RDBAH, 0);

    E1000_WRITE_REG(&ext->hw, RDLEN, rdlen);

    /* setup the hw rx head and tail descriptor pointers */
    E1000_WRITE_REG(&ext->hw, RDH, 0);
    E1000_WRITE_REG(&ext->hw, RDT, 0);

    /* enable 82543 receive checksum offload TCP and UDP */
    if((ext->hw.mac_type >= e1000_82543) &&
        (ext->rx_csum == TRUE)) {
        rxcsum = E1000_READ_REG(&ext->hw, RXCSUM);
        rxcsum |= E1000_RXCSUM_TUOFL;
        E1000_WRITE_REG(&ext->hw, RXCSUM, rxcsum);
    }

    /* enable receives */
    E1000_WRITE_REG(&ext->hw, RCTL, rctl);
}

/**
 * e1000_alloc_rx_buffers - Replace used receive buffers
 * @ext: address of board private structure
 **/

static void e1000_alloc_rx_buffers(e1000_extension_t* ext)
{
    struct e1000_desc_ring* rx_ring = &ext->rx_ring;
    struct e1000_rx_desc* rx_desc;
    struct e1000_buffer* buffer_info;
    uint8_t* buffer;
    unsigned int i;

    i = rx_ring->next_to_use;
    buffer_info = &rx_ring->buffer_info[i];   //i=0

    while(!buffer_info->buffer) {
        buffer = mem_alloc(ext->rx_buffer_len + NET_IP_ALIGN);

        if(unlikely(!buffer)) {
            break;
        }

        /* make buffer alignment 2 beyond a 16 byte boundary
         * this will result in a 16 byte alinged IP header after
         * the 14 byte MAC header is removed
         */

        buffer_info->buffer = buffer;
        buffer_info->length = ext->rx_buffer_len;
        buffer_info->dma = kern_vir_addr2phy_addr(buffer);

        rx_desc = E1000_RX_DESC(*rx_ring, i);
        rx_desc->buffer_addr_low = byte_cpu_to_little_endian32(buffer_info->dma); //------
        rx_desc->buffer_addr_high = 0;

        if(unlikely((i & ~(E1000_RX_BUFFER_WRITE - 1)) == i)) {
            wmb();
            E1000_WRITE_REG(&ext->hw, RDT, i);
        }

        if(unlikely(++i == rx_ring->count)) i = 0;
        buffer_info = &rx_ring->buffer_info[i];
    }

    rx_ring->next_to_use = i;
}

/**
 * e1000_configure_tx - Configure 8254x Transmit Unit after Reset
 * @ext: board private structure
 *
 * Configure the Tx unit of the MAC after a reset.
 **/

static void e1000_configure_tx(e1000_extension_t* ext)
{
    uint32_t tdba = ext->tx_ring.dma;
    // keprint(PRINT_DEBUG "tdba = %x\n", tdba);
    uint32_t tdlen = ext->tx_ring.count * sizeof(struct e1000_tx_desc);
    uint32_t tctl, tipg;

    // E1000_WRITE_REG(&ext->hw, TDBAL, (tdba & 0xffffffffUll));
    // keprint(PRINT_DEBUG "tdba_low = %x\n", (tdba & 0xffffffffUll));
    E1000_WRITE_REG(&ext->hw, TDBAL, tdba);
    // keprint(PRINT_DEBUG "tdba >> 32 = %x\n", tdba >> 32);
    E1000_WRITE_REG(&ext->hw, TDBAH, 0);

    E1000_WRITE_REG(&ext->hw, TDLEN, tdlen);

    /* setup the hw tx head and tail descriptor pointers */
    /* 设置传送描述符头尾指针 */
    E1000_WRITE_REG(&ext->hw, TDH, 0);
    E1000_WRITE_REG(&ext->hw, TDT, 0);

    /* set the default values for the tx inter packet gap timer */
    switch(ext->hw.mac_type) {
        case e1000_82542_rev2_0:
        case e1000_82542_rev2_1:
            tipg = DEFAULT_82542_TIPG_IPGT;
            tipg |= DEFAULT_82542_TIPG_IPGR1 << E1000_TIPG_IPGR1_SHIFT;
            tipg |= DEFAULT_82542_TIPG_IPGR2 << E1000_TIPG_IPGR2_SHIFT;
            break;
        default:
            if(ext->hw.media_type == e1000_media_type_fiber ||
               ext->hw.media_type == e1000_media_type_internal_serdes) {
               tipg = DEFAULT_82543_TIPG_IPGT_FIBER;
            } else {
                tipg = DEFAULT_82543_TIPG_IPGT_COPPER;
            }
            tipg |= DEFAULT_82543_TIPG_IPGR1 << E1000_TIPG_IPGR1_SHIFT;
            tipg |= DEFAULT_82543_TIPG_IPGR2 << E1000_TIPG_IPGR2_SHIFT;
    }
    E1000_WRITE_REG(&ext->hw, TIPG, tipg);

    /* set the tx interrupt delay register */
    E1000_WRITE_REG(&ext->hw, TIDV, ext->tx_int_delay);
    if(ext->hw.mac_type >= e1000_82540) {
        // keprint(PRINT_DEBUG "ext->tx_abs_int_delay = %d\n", ext->tx_abs_int_delay);
        E1000_WRITE_REG(&ext->hw, TADV, ext->tx_abs_int_delay);
    }

    /* program the transmit control register */
    tctl = E1000_READ_REG(&ext->hw, TCTL);

    tctl &= ~E1000_TCTL_CT;
    tctl |= E1000_TCTL_EN | E1000_TCTL_PSP |
        (E1000_COLLISION_THRESHOLD << E1000_CT_SHIFT);
    
    E1000_WRITE_REG(&ext->hw, TCTL, tctl);

    /* 设置碰撞距离 */
    e1000_config_collision_dist(&ext->hw);

    /* setup transmit descriptor settings for eop descriptor */
    ext->txd_cmd = E1000_TXD_CMD_IDE | E1000_TXD_CMD_EOP |
        E1000_TXD_CMD_IFCS;
    
    if(ext->hw.mac_type < e1000_82543) {
        ext->txd_cmd |= E1000_TXD_CMD_RPS;
    } else {
        ext->txd_cmd |= E1000_TXD_CMD_RS;
    }

    /* cache if we're 82544 running in pci-x because we'll 
     * need this to apply a workaround later in the send path. 
     */
    if(ext->hw.mac_type == e1000_82544 &&
       ext->hw.bus_type == e1000_bus_type_pcix) {
        ext->pcix_82544 = 1;
    }
}

/**
 * e1000_setup_rctl - configure the receive control register
 * @ext: Board private structure
 **/

static void e1000_setup_rctl(e1000_extension_t* ext)
{
    uint32_t rctl;

    rctl = E1000_READ_REG(&ext->hw, RCTL);

    rctl &= ~(3 << E1000_RCTL_MO_SHIFT);

    rctl |= E1000_RCTL_EN | E1000_RCTL_BAM |
        E1000_RCTL_LBM_NO | E1000_RCTL_RDMTS_HALF |
        (ext->hw.mc_filter_type << E1000_RCTL_MO_SHIFT);
    
    if(ext->hw.tbi_compatibility_on == 1){
        rctl |= E1000_RCTL_SBP;
    } else {
        rctl &= ~E1000_RCTL_SBP;
    }

    /* setup buffer sizes */
    rctl &= ~(E1000_RCTL_SZ_4096);
    rctl |= (E1000_RCTL_BSEX | E1000_RCTL_LPE);
    switch(ext->rx_buffer_len) {
        default:
            rctl |= E1000_RCTL_SZ_2048;
            rctl &= ~(E1000_RCTL_BSEX | E1000_RCTL_LPE);
            break;
        case E1000_RXBUFFER_4096:
            rctl |= E1000_RCTL_SZ_4096;
            break;
        case E1000_RXBUFFER_8192:
            rctl |= E1000_RCTL_SZ_8192;
            break;
        case E1000_RXBUFFER_16384:
            rctl |= E1000_RCTL_SZ_16384;
            break;
    }

    E1000_WRITE_REG(&ext->hw, RCTL, rctl);
}

/**
 * e1000_intr - Interrupt Handler
 * @irq: interrupt number
 * @data: pointer to a network interface device structure
 **/

static int e1000_intr(irqno_t irq, void *data)
{
    e1000_extension_t* ext = (e1000_extension_t*)data;

    struct e1000_hw* hw = &ext->hw;
    uint32_t icr = E1000_READ_REG(hw, ICR);
#ifndef CONFIG_E1000_NAPI
    unsigned int i;
#endif

    if(unlikely(!icr)) {
        return IRQ_NEXTONE;
    }

    if(unlikely(icr & (E1000_ICR_RXSEQ | E1000_ICR_LSC))) {
        hw->get_link_status = 1;
        timer_modify(&ext->watchdog_timer, systicks);
    }

    for(i=0; i<E1000_MAX_INTR; i++) {
        if(unlikely(!e1000_clean_rx_irq(ext) & 
           !e1000_clean_tx_irq(ext))) {
            break;
        }
    }

    return IRQ_HANDLED;
}

/**
 * e1000_rx_checksum - Receive Checksum Offload for 82543
 * @ext: board private structure
 * @rx_desc: receive descriptor
 **/

static inline void
e1000_rx_checksum(e1000_extension_t* ext, 
                  struct e1000_rx_desc* rx_desc)
{
    /* 82543 or new only */
    if(unlikely((ext->hw.mac_type < e1000_82543) || 
    (rx_desc->status & E1000_RXD_STAT_IXSM) || 
    (!(rx_desc->status & E1000_RXD_STAT_TCPCS)))) {
        return;
    }

    /* at this point we know the hardware did the tcp checksum */
    /* now look at the tcp checksum error bit */
    if(rx_desc->errors & E1000_RXD_ERR_TCPE) {
        /* let the stack verify checksum errors */
        ext->hw_csum_err++;
    } else {
        ext->hw_csum_good++;
    }
}

/**
 * e1000_clean_rx_irq - Send received data up the network stack
 * @ext: board private structure
 **/

static boolean_t
#ifdef CONFIG_E1000_NAPI
e1000_clean_rx_irq(e1000_extension_t* ext, int* work_done,
                   int work_to_do)
#else
e1000_clean_rx_irq(e1000_extension_t* ext)
#endif
{
    struct e1000_desc_ring* rx_ring = &ext->rx_ring;
    device_object_t* netdev = ext->device_object;
    // pci_device_t* pci_dev = ext->pci_device;
    struct e1000_rx_desc* rx_desc;
    struct e1000_buffer* buffer_info;
    uint8_t* buffer;
    unsigned long flags;
    uint32_t length;
    uint8_t last_byte;
    unsigned int i;
    boolean_t cleaned = FALSE;

    i = rx_ring->next_to_clean;
    // keprint(PRINT_DEBUG "rx_ring->next_to_clean = %d\n", i);
    /* 获取接收描述符 */
    rx_desc = E1000_RX_DESC(*rx_ring, i);
    // keprint(PRINT_DEBUG "rx_desc_length = %d\n", rx_desc->length);

    while(rx_desc->status & E1000_RXD_STAT_DD) {
        // keprint(PRINT_DEBUG "i = %d\n", i);
        buffer_info = &rx_ring->buffer_info[i];
#ifdef CONFIG_E1000_NAPI
        if(*work_done >= work_to_do) {
            break;
        }
        (*work_done)++;
#endif
        cleaned = TRUE;

        buffer = buffer_info->buffer;
        length = byte_little_endian16_to_cpu(rx_desc->length);
        // keprint(PRINT_DEBUG "length = %d------rx_desc->length = %d\n", length, rx_desc->length);

        if(unlikely(!(rx_desc->status & E1000_RXD_STAT_EOP))) {
            /* all receives must fit into a single buffer */
            keprint(PRINT_DEBUG "%s: receive packet consumed multiple buffers", netdev->name);
            mem_free(buffer);
            goto next_desc;
        }

        if(unlikely(rx_desc->errors & E1000_RXD_ERR_FRAME_ERR_MASK)) {
            keprint(PRINT_DEBUG "AAAAAAA\n");
            last_byte = *(buffer + length - 1);
            if(TBI_ACCEPT(&ext->hw, rx_desc->status,
                          rx_desc->errors, length, last_byte)) {
                spin_lock_irqsave(&ext->stats_lock, flags);
                e1000_tbi_adjust_stats(&ext->hw, 
                                       &ext->stats, 
                                       length, buffer);
                spin_unlock_irqrestore(&ext->stats_lock, flags);
                length--;
            } else {
                mem_free(buffer);
                goto next_desc;
            }
        }

        /* receive checksum offload */
        e1000_rx_checksum(ext, rx_desc);

        // for(int j=0; j<length; j++) {
        //     keprint(PRINT_DEBUG "%-4x ", *(buffer+j));
        //     if(!((j + 1) % 16)) {
        //         keprint(PRINT_DEBUG "\n");
        //     }
        // }

    	/* 网络接口发送数据包 */
        // keprint(PRINT_DEBUG "len = %d\n\n", length - 4);
        io_device_queue_append(&ext->rx_queue, buffer, length - ETHERNET_FCS_SIZE);
        // io_device_queue_append(&ext->rx_queue, buffer, length);

next_desc:
        rx_desc->status = 0;
        buffer_info->buffer = NULL;
        if(unlikely(++i == rx_ring->count)) {
            i = 0;
        }
        rx_desc = E1000_RX_DESC(*rx_ring, i);
    }

    rx_ring->next_to_clean = i;

    e1000_alloc_rx_buffers(ext);

    return cleaned;
}

static inline void
e1000_unmap_and_free_tx_resource(e1000_extension_t* ext, 
            struct e1000_buffer* buffer_info)
{
    if(buffer_info->dma) {
        buffer_info->dma = 0;
    }
    if(buffer_info->buffer) {
        //mem_free(buffer_info->buffer);
        buffer_info->buffer = NULL;
    }
}

/**
 * e1000_clean_tx_irq - Reclaim resources after transmit completes
 * @ext: board private structure
 **/

static boolean_t e1000_clean_tx_irq(e1000_extension_t* ext)
{
    struct e1000_desc_ring* tx_ring = &ext->tx_ring;
    struct e1000_tx_desc* tx_desc;
    struct e1000_tx_desc* eop_desc;
    struct e1000_buffer* buffer_info;
    unsigned int i, eop;
    boolean_t cleaned = FALSE;

    i = tx_ring->next_to_clean;
    eop = tx_ring->buffer_info[i].next_to_watch;
    eop_desc = E1000_TX_DESC(*tx_ring, eop);

    while(eop_desc->upper.data & byte_cpu_to_little_endian32(E1000_TXD_STAT_DD)) {
        // keprint(PRINT_DEBUG "i = %d\n", i);
        // keprint(PRINT_DEBUG "next_to_watch = %d\n", eop);
        // keprint(PRINT_DEBUG "!!!!!!!!!!!\n");
        for(cleaned = FALSE; !cleaned; ) {
            tx_desc = E1000_TX_DESC(*tx_ring, i);
            buffer_info = &tx_ring->buffer_info[i];

            e1000_unmap_and_free_tx_resource(ext, buffer_info);
            tx_desc->buffer_addr_low = 0;
            tx_desc->buffer_addr_high = 0;
            tx_desc->lower.data = 0;
            tx_desc->upper.data = 0;

            cleaned = (i == eop);
            if(unlikely(++i == tx_ring->count)) {
                i = 0;
            }
        }

        eop = tx_ring->buffer_info[i].next_to_watch;
        // keprint(PRINT_DEBUG "i=%d---next_to_watch=%d\n", i, eop);
        // keprint(PRINT_DEBUG "-------i=%d\n", i);
        eop_desc = E1000_TX_DESC(*tx_ring, eop);
    }

    tx_ring->next_to_clean = i;

    return cleaned;
}

#define E1000_MAX_TXD_PWR 12
#define E1000_MAX_DATA_PER_TXD (1<<E1000_MAX_TXD_PWR)
#define TXD_USE_COUNT(S, X) (((S) >> (X)) + 1 )
#define E1000_TX_FLAGS_CSUM		0x00000001
#define E1000_TX_FLAGS_VLAN		0x00000002
#define E1000_TX_FLAGS_TSO		0x00000004
#define E1000_TX_FLAGS_VLAN_MASK	0xffff0000
#define E1000_TX_FLAGS_VLAN_SHIFT	16

static inline int
e1000_tx_map(e1000_extension_t* ext, 
             uint8_t* buffer, 
             unsigned int first, 
             unsigned int max_per_txd, 
             int length)
{
    struct e1000_desc_ring* tx_ring = &ext->tx_ring;
    struct e1000_buffer* buffer_info;
    unsigned int len = length;
    unsigned int offset = 0, size, count = 0, i;

    i = tx_ring->next_to_use;

    while(len) {
        buffer_info = &tx_ring->buffer_info[i];
        size = min(len, max_per_txd);
        if(unlikely(ext->pcix_82544 && 
           !((unsigned long)(buffer + offset + size -1) & 4) &&
           size > 4)) {
            size -= 4;
        }

        buffer_info->length = size;
        buffer_info->dma = kern_vir_addr2phy_addr(buffer + offset);
        // keprint(PRINT_DEBUG "tx_buffer_info->dma = %d\n", buffer_info->dma);
        buffer_info->time_stamp = systicks;

        len -= size;
        offset += size;
        count++;
        if(unlikely(++i == tx_ring->count)) {
            i = 0;
        }
    }

    i = (i == 0) ? tx_ring->count - 1 : i - 1;
    tx_ring->buffer_info[i].buffer = buffer;
    tx_ring->buffer_info[first].next_to_watch = i;


    return count;
}

static inline void
e1000_tx_queue(e1000_extension_t* ext, int count, int tx_flags)
{
    struct e1000_desc_ring* tx_ring = &ext->tx_ring;
    struct e1000_tx_desc* tx_desc = NULL;
    struct e1000_buffer* buffer_info;
    uint32_t txd_upper = 0, txd_lower = E1000_TXD_CMD_IFCS;
    unsigned int i;

    if(likely(tx_flags & E1000_TX_FLAGS_TSO)) {
        txd_lower |= E1000_TXD_CMD_DEXT | E1000_TXD_DTYP_D |
                     E1000_TXD_CMD_TSE;
        txd_upper |= (E1000_TXD_POPTS_IXSM | E1000_TXD_POPTS_TXSM) << 8;
    }

    if(likely(tx_flags & E1000_TX_FLAGS_CSUM)) {
        txd_lower |= E1000_TXD_CMD_DEXT | E1000_TXD_DTYP_D;
        txd_upper |= E1000_TXD_POPTS_TXSM << 8;
    }

    if(unlikely(tx_flags & E1000_TX_FLAGS_VLAN)) {
        txd_lower |= E1000_TXD_CMD_VLE;
        txd_upper |= (tx_flags & E1000_TX_FLAGS_VLAN_MASK);
    }

    i = tx_ring->next_to_use;

    while(count--) {
        buffer_info = &tx_ring->buffer_info[i];
        tx_desc = E1000_TX_DESC(*tx_ring, i);
        // tx_desc->buffer_addr = byte_cpu_to_little_endian32(buffer_info->dma);   //------
        tx_desc->buffer_addr_low = byte_cpu_to_little_endian32(buffer_info->dma);
        tx_desc->buffer_addr_high = 0;
        tx_desc->lower.data = 
            byte_cpu_to_little_endian32(txd_lower | buffer_info->length);
        tx_desc->upper.data = byte_cpu_to_little_endian32(txd_upper);
        if(unlikely(++i == tx_ring->count)) {
            i = 0;
        }
    }

    tx_desc->lower.data |= byte_cpu_to_little_endian32(ext->txd_cmd);
	
    /* Force memory writes to complete before letting h/w
	 * know there are new descriptors to fetch.  (Only
	 * applicable for weak-ordered memory model archs,
	 * such as IA-64). */
    wmb();

    tx_ring->next_to_use = i;
    E1000_WRITE_REG(&ext->hw, TDT, i);
}

int e1000_transmit(e1000_extension_t* ext, uint8_t* buf, uint32_t len)
{
    unsigned int first, max_per_txd = E1000_MAX_DATA_PER_TXD;
    unsigned int max_txd_pwr = E1000_MAX_TXD_PWR;
    unsigned int tx_flags = 0;
    unsigned int length = len;
    unsigned long flags;
    unsigned long intr_flags;
    int count = 0;

    interrupt_save_and_disable(intr_flags);

    if(unlikely(len <= 0)) {
        mem_free(buf);
        interrupt_restore_state(intr_flags);
        return 0;
    }

    count += TXD_USE_COUNT(len, max_txd_pwr);

    if(ext->pcix_82544) {
        count++;
    }

    spin_lock_irqsave(&ext->tx_lock, flags);

    if(unlikely(E1000_DESC_UNUSED(&ext->tx_ring) < count + 2)) {
        spin_unlock_irqrestore(&ext->tx_lock, flags);
        return -1;
    }

    spin_unlock_irqrestore(&ext->tx_lock, flags);

    first = ext->tx_ring.next_to_use;

    //int count_ = e1000_tx_map(ext, buf, first, max_per_txd, length);
    // keprint(PRINT_DEBUG "transmit---tx_length=%d\n", length);
    e1000_tx_queue(ext, 
        e1000_tx_map(ext, buf, first, max_per_txd, length), 
        tx_flags);
    
    interrupt_restore_state(intr_flags);

    return 0;
}

static __init void e1000_driver_entry(void)
{
    if (driver_object_create(e1000_driver_func) < 0) {
        keprint(PRINT_ERR "[driver]: %s create driver failed!\n", __func__);
    }
}

// driver_initcall(e1000_driver_entry);
