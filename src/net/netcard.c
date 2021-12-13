#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <xbook/net.h>

LIST_HEAD(netcard_list_head);

static int next_netcard_solt = 0;

static int netcard_solt_cache[NETCARD_SOLT_NR];

#define IS_BAD_SOLT(solt) \
        (solt < 0 || solt >= NETCARD_SOLT_NR)

#define SOLT_TO_HANDLE(solt) netcard_solt_cache[solt]

netcard_drvier_t drv_netcard;

/**
 * netcard_probe_device - 探测设备
 * @type: 设备的类型
 * 
 */
int netcard_probe_device(device_type_t type)
{
    /* 磁盘设备 */
    devent_t *p = NULL;
    devent_t devent;
    netcard_info_t *netcard;
    int card_found = 0;
    do {
        if (sys_scandev(p, type, &devent))
            break;
#ifdef DEBUG_NETCARD
        dbgprint("[net]: %s: probe device %s\n", __func__, devent.de_name);
#endif    
        /* 添加到磁盘数组 */
        /* 创建一个设备信息 */
        netcard = mem_alloc(sizeof(netcard_info_t));
        if (netcard == NULL)
            return -1;
        /* 填信息并添加到链表 */
        netcard->devent = devent;
        netcard->handle = -1;
        /* 设置虚拟磁盘名字 */
        sprintf(netcard->virname, "netcard%d", next_netcard_solt);
        netcard->solt = next_netcard_solt++;
        list_add_tail(&netcard->list, &netcard_list_head);
        
        p = &devent;
        card_found++;
    } while (1);
    if (!card_found)
        return -1;
    return 0;
}


void netcard_info_print()
{
    netcard_info_t *netcard;
    list_for_each_owner (netcard, &netcard_list_head, list) {
        infoprint("[net] probe device:%s -> vir:%s type:%d\n",
            netcard->devent.de_name, netcard->virname, netcard->devent.de_type);
    }
}

int netcard_find_by_name(char *name)
{
    netcard_info_t *netcard;
    list_for_each_owner (netcard, &netcard_list_head, list) {
        if (!strcmp(netcard->virname, name)) {
            return netcard->solt;
        }
    }
    return -1;
}

static int netcard_open(int solt)
{
    if (IS_BAD_SOLT(solt))
        return -1;
    netcard_info_t *netcard;
    list_for_each_owner (netcard, &netcard_list_head, list) {
        if (netcard->solt == solt) {
            netcard->handle = device_open(netcard->devent.de_name, 0);
            if (netcard->handle < 0)
                return -1;
            netcard_solt_cache[solt] = netcard->handle;
            return 0;
        }
    }
    return -1;
}

static int netcard_close(int solt)
{
    if (IS_BAD_SOLT(solt))
        return -1;
    netcard_info_t *netcard;
    list_for_each_owner (netcard, &netcard_list_head, list) {
        if (netcard->solt == solt) {
            if (device_close(netcard->handle) != 0) 
                return -1;
            int i;
            for (i = 0; i < NETCARD_SOLT_NR; i++) {
                if (netcard_solt_cache[i] == netcard->handle) {
                    netcard_solt_cache[i] = -1;
                    break;
                } 
            }
            netcard->handle = -1;
            return 0;
        }
    }
    return -1;
}


static int netcard_read(int solt, void *buffer, size_t size)
{
    if (IS_BAD_SOLT(solt))
        return -1;
    int len = device_read(SOLT_TO_HANDLE(solt), buffer, size, 0);
    if (len <= 0)
        return -1;
    return len;
}

static int netcard_write(int solt, void *buffer, size_t size)
{
    if (IS_BAD_SOLT(solt))
        return -1;
    int len = device_write(SOLT_TO_HANDLE(solt), buffer, size, 0);
    if (len <= 0)
        return -1;
    return len;
}

static int netcard_ioctl(int solt, unsigned int cmd, void *arg)
{
    if (IS_BAD_SOLT(solt))
        return -1;
    if (device_devctl(SOLT_TO_HANDLE(solt), cmd, (unsigned long) arg) < 0)
        return -1;
    return 0;
}

int netcard_manager_init()
{
    int card_found_err = 0;
    if (netcard_probe_device(DEVICE_TYPE_PHYSIC_NETCARD) < 0) {
        card_found_err++;
    }
    if (netcard_probe_device(DEVICE_TYPE_NETWORK) < 0) {
        card_found_err++;
    }
    if (card_found_err > 1) {
        errprint("[net] not netcard found\n");
        return -1;
    }
    int i;
    for (i = 0; i < NETCARD_SOLT_NR; i++) {
        netcard_solt_cache[i] = -1;
    }

    netcard_info_print();
    drv_netcard.open = netcard_open;
    drv_netcard.close = netcard_close;
    drv_netcard.read = netcard_read;
    drv_netcard.write = netcard_write;
    drv_netcard.ioctl = netcard_ioctl;
    return 0;
}