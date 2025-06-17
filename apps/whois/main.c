/**
 * @file
 * @brief 命令行工具，发送 BACnet Who-Is 请求到设备，并打印接收到的任何 I-Am 响应。
 * 这对于在网络上查找设备，或查找特定实例范围内的设备非常有用。
 * @author Steve Karg <skarg@users.sourceforge.net>
 * @date 2006
 * @copyright SPDX-License-Identifier: MIT
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
/* BACnet Stack 定义 - 首先包含 */
#include "bacnet/bacdef.h"
/* BACnet Stack API */
#include "bacnet/bactext.h"
#include "bacnet/iam.h"
#include "bacnet/npdu.h"
#include "bacnet/apdu.h"
#include "bacnet/bactext.h"
#include "bacnet/version.h"
/* 一些演示需要的东西 */
#include "bacnet/basic/binding/address.h"
#include "bacnet/basic/sys/mstimer.h"
#include "bacnet/basic/sys/filename.h"
#include "bacnet/basic/services.h"
#include "bacnet/basic/tsm/tsm.h"
#include "bacnet/basic/object/device.h"
#include "bacnet/datalink/datalink.h"
#include "bacnet/datalink/dlenv.h"
#include "bacport.h"

/* 用于接收的缓冲区 */
static uint8_t Rx_Buf[MAX_MPDU] = { 0 };

/* 本文件中使用的全局变量 */
static int32_t Target_Object_Instance_Min = -1; /* 目标对象实例最小值 */
static int32_t Target_Object_Instance_Max = -1; /* 目标对象实例最大值 */
static bool Error_Detected = false; /* 错误检测标志 */
/* 调试信息打印 */
static bool BACnet_Debug_Enabled;

#define BAC_ADDRESS_MULT 1 /* BACnet 地址重复标志 */

/* 地址条目结构体 */
struct address_entry {
    struct address_entry *next; /* 指向下一个地址条目的指针 */
    uint8_t Flags; /* 标志位 */
    uint32_t device_id; /* 设备 ID */
    unsigned max_apdu; /* 最大 APDU 长度 */
    BACNET_ADDRESS address; /* BACnet 地址 */
};

/* 地址表结构体 */
static struct address_table {
    struct address_entry *first; /* 指向第一个地址条目的指针 */
    struct address_entry *last; /* 指向最后一个地址条目的指针 */
} Address_Table = { 0 };

/* 分配地址条目 */
static struct address_entry *alloc_address_entry(void)
{
    struct address_entry *rval;
    rval = (struct address_entry *)calloc(1, sizeof(struct address_entry)); /* 分配内存并清零 */
    if (Address_Table.first == 0) /* 如果地址表为空 */
    {
        Address_Table.first = Address_Table.last = rval; /* 新条目既是第一个也是最后一个 */
    }
    else /* 如果地址表不为空 */
    {
        Address_Table.last->next = rval; /* 将新条目添加到链表末尾 */
        Address_Table.last = rval; /* 更新最后一个条目指针 */
    }
    return rval;
}

/* 添加地址到地址表 */
static void address_table_add(
    uint32_t device_id, unsigned max_apdu, const BACNET_ADDRESS *src)
{
    struct address_entry *pMatch;
    uint8_t flags = 0;

    pMatch = Address_Table.first; /* 从地址表头开始查找 */
    while (pMatch) /* 遍历地址表 */
    {
        if (pMatch->device_id == device_id) /* 如果找到具有相同设备 ID 的条目 */
        {
            if (bacnet_address_same(&pMatch->address, src)) /* 如果 MAC 地址也相同 */
            {
                return; /* 地址已存在，直接返回 */
            }
            flags |= BAC_ADDRESS_MULT; /* 标记为重复地址 */
            pMatch->Flags |= BAC_ADDRESS_MULT; /* 更新现有条目的标志 */
        }
        pMatch = pMatch->next; /* 继续下一个条目 */
    }

    pMatch = alloc_address_entry(); /* 分配新的地址条目 */

    pMatch->Flags = flags; /* 设置标志 */
    pMatch->device_id = device_id; /* 设置设备 ID */
    pMatch->max_apdu = max_apdu; /* 设置最大 APDU */
    pMatch->address = *src; /* 复制 BACnet 地址 */

    return;
}

/* I-Am 服务请求处理函数 */
static void my_i_am_handler(
    uint8_t *service_request, uint16_t service_len, BACNET_ADDRESS *src)
{
    int len = 0;
    uint32_t device_id = 0;
    unsigned max_apdu = 0;
    int segmentation = 0;
    uint16_t vendor_id = 0;
    unsigned i = 0;

    (void)service_len; /* 避免编译器警告未使用的参数 */
    /* 解码 I-Am 服务请求 */
    len = iam_decode_service_request(
        service_request, &device_id, &max_apdu, &segmentation, &vendor_id);
    if (BACnet_Debug_Enabled)
    {
        fprintf(stderr, "Received I-Am Request"); /* 打印接收到 I-Am 请求的调试信息 */
    }
    if (len != -1) /* 如果解码成功 */
    {
        if (BACnet_Debug_Enabled)
        {
            fprintf(stderr, " from %lu, MAC = ", (unsigned long)device_id); /* 打印设备 ID */
            if ((src->mac_len == 6) && (src->len == 0)) /* 如果是 IP 地址 */
            {
                fprintf(
                    stderr, "%u.%u.%u.%u %02X%02X\n", (unsigned)src->mac[0],
                    (unsigned)src->mac[1], (unsigned)src->mac[2],
                    (unsigned)src->mac[3], (unsigned)src->mac[4],
                    (unsigned)src->mac[5]);
            }
            else /* 如果是其他 MAC 地址 */
            {
                for (i = 0; i < src->mac_len; i++)
                {
                    fprintf(stderr, "%02X", (unsigned)src->mac[i]);
                    if (i < (src->mac_len - 1))
                    {
                        fprintf(stderr, ":");
                    }
                }
                fprintf(stderr, "\n");
            }
        }
        address_table_add(device_id, max_apdu, src); /* 将设备信息添加到地址表 */
    }
    else
    {
        if (BACnet_Debug_Enabled)
        {
            fprintf(stderr, ", but unable to decode it.\n"); /* 打印解码失败的调试信息 */
        }
    }

    return;
}

/* Abort 服务处理函数 */
static void MyAbortHandler(
    BACNET_ADDRESS *src, uint8_t invoke_id, uint8_t abort_reason, bool server)
{
    /* FIXME: 验证 src 和 invoke_id */
    (void)src; /* 避免编译器警告未使用的参数 */
    (void)invoke_id; /* 避免编译器警告未使用的参数 */
    (void)server; /* 避免编译器警告未使用的参数 */
    fprintf(
        stderr, "BACnet Abort: %s\n", bactext_abort_reason_name(abort_reason)); /* 打印 Abort 原因 */
    Error_Detected = true; /* 设置错误检测标志 */
}

/* Reject 服务处理函数 */
static void
MyRejectHandler(BACNET_ADDRESS *src, uint8_t invoke_id, uint8_t reject_reason)
{
    /* FIXME: 验证 src 和 invoke_id */
    (void)src; /* 避免编译器警告未使用的参数 */
    (void)invoke_id; /* 避免编译器警告未使用的参数 */
    fprintf(
        stderr, "BACnet Reject: %s\n",
        bactext_reject_reason_name(reject_reason)); /* 打印 Reject 原因 */
    Error_Detected = true; /* 设置错误检测标志 */
}

/* 初始化服务处理函数 */
static void init_service_handlers(void)
{
    Device_Init(NULL); /* 初始化设备对象 */
    /* 注意: 此应用程序不需要处理 who-is，这会给用户带来困惑! */
    /* 为我们未实现的所有服务设置处理程序 */
    /* 发送正确的 reject 消息是必需的... */
    apdu_set_unrecognized_service_handler_handler(handler_unrecognized_service);
    /* 我们必须实现 read property - 这是必需的! */
    apdu_set_confirmed_handler(
        SERVICE_CONFIRMED_READ_PROPERTY, handler_read_property);
    /* 处理返回的回复(请求) */
    apdu_set_unconfirmed_handler(SERVICE_UNCONFIRMED_I_AM, my_i_am_handler);
    /* 处理返回的任何错误 */
    apdu_set_abort_handler(MyAbortHandler);
    apdu_set_reject_handler(MyRejectHandler);
}

/* 打印 MAC 地址 */
static void print_macaddr(const uint8_t *addr, int len)
{
    int j = 0;

    while (j < len)
    {
        if (j != 0)
        {
            printf(":");
        }
        printf("%02X", addr[j]);
        j++;
    }
    while (j < MAX_MAC_LEN) /* 填充空格以对齐 */
    {
        printf("   ");
        j++;
    }
}

/* 打印地址缓存 */
static void print_address_cache(void)
{
    BACNET_ADDRESS address;
    unsigned total_addresses = 0; /* 总地址数 */
    unsigned dup_addresses = 0; /* 重复地址数 */
    struct address_entry *addr;
    uint8_t local_sadr = 0; /* 本地 SADR */

    /* 注意: 此字符串格式由 src/address.c 解析，因此必须兼容。 */

    printf(
        ";%-7s  %-20s %-5s %-20s %-4s\n", "Device", "MAC (hex)", "SNET",
        "SADR (hex)", "APDU"); /* 打印表头 */
    printf(";-------- -------------------- ----- -------------------- ----\n"); /* 打印分隔线 */

    addr = Address_Table.first; /* 从地址表头开始 */
    while (addr) /* 遍历地址表 */
    {
        bacnet_address_copy(&address, &addr->address); /* 复制地址信息 */
        total_addresses++; /* 总地址数加一 */
        if (addr->Flags & BAC_ADDRESS_MULT) /* 如果是重复地址 */
        {
            dup_addresses++; /* 重复地址数加一 */
            printf(";"); /* 打印分号前缀 */
        }
        else
        {
            printf(" "); /* 打印空格前缀 */
        }
        printf(" %-7u ", addr->device_id); /* 打印设备 ID */
        print_macaddr(address.mac, address.mac_len); /* 打印 MAC 地址 */
        printf(" %-5hu ", address.net); /* 打印 SNET */
        if (address.net) /* 如果 SNET 不为 0 */
        {
            print_macaddr(address.adr, address.len); /* 打印 SADR */
        }
        else /* 如果 SNET 为 0 */
        {
            print_macaddr(&local_sadr, 1); /* 打印本地 SADR (通常为0) */
        }
        printf(" %-4u ", (unsigned)addr->max_apdu); /* 打印最大 APDU */
        printf("\n");

        addr = addr->next; /* 继续下一个条目 */
    }
    printf(";\n; Total Devices: %u\n", total_addresses); /* 打印总设备数 */
    if (dup_addresses)
    {
        printf("; * Duplicate Devices: %u\n", dup_addresses); /* 打印重复设备数 */
    }
}

/* 打印用法信息 */
static void print_usage(const char *filename)
{
    printf("Usage: %s", filename);
    printf(" [device-instance-min [device-instance-max]]\n");
    printf("       [--dnet][--dadr][--mac]\n");
    printf("       [--version][--help]\n");
}

/* 打印帮助信息 */
static void print_help(const char *filename)
{
    printf("Send BACnet WhoIs service request to a device or multiple\n"
           "devices, and wait for responses. Displays any devices found\n"
           "and their network information.\n"); /* 程序功能描述 */
    printf("\n");
    printf("device-instance:\n"
           "BACnet Device Object Instance number that you are trying\n"
           "to send a Who-Is service request. The value should be in\n"
           "the range of 0 to 4194303. A range of values can also be\n"
           "specified by using a minimum value and a maximum value.\n"); /* device-instance 参数说明 */
    printf("\n");
    printf("--mac A\n"
           "BACnet mac address."
           "Valid ranges are from 00 to FF (hex) for MS/TP or ARCNET,\n"
           "or an IP string with optional port number like 10.1.2.3:47808\n"
           "or an Ethernet MAC in hex like 00:21:70:7e:32:bb\n"); /* --mac 参数说明 */
    printf("\n");
    printf("--dnet N\n"
           "BACnet network number N for directed requests.\n"
           "Valid range is from 0 to 65535 where 0 is the local connection\n"
           "and 65535 is network broadcast.\n"); /* --dnet 参数说明 */
    printf("\n");
    printf("--dadr A\n"
           "BACnet mac address on the destination BACnet network number.\n"
           "Valid ranges are from 00 to FF (hex) for MS/TP or ARCNET,\n"
           "or an IP string with optional port number like 10.1.2.3:47808\n"
           "or an Ethernet MAC in hex like 00:21:70:7e:32:bb\n"); /* --dadr 参数说明 */
    printf("\n");
    printf("--repeat\n"
           "Send the message repeatedly until signalled to quit.\n"
           "Default is disabled, using the APDU timeout as time to quit.\n"); /* --repeat 参数说明 */
    printf("\n");
    printf("--retry C\n"
           "Send the message C number of times\n"
           "Default is retry 0, only sending one time.\n"); /* --retry 参数说明 */
    printf("\n");
    printf("--timeout T\n"
           "Wait T milliseconds after sending before retry\n"
           "Default delay is 3000ms.\n"); /* --timeout 参数说明 */
    printf("\n");
    printf("--delay M\n"
           "Wait M milliseconds for responses after sending\n"
           "Default delay is 100ms.\n"); /* --delay 参数说明 */
    printf("\n");
    printf("Example:\n"); /* 示例用法 */
    printf(
        "Send a WhoIs request to DNET 123:\n"
        "%s --dnet 123\n",
        filename);
    printf(
        "Send a WhoIs request to MAC 10.0.0.1 DNET 123 DADR 05h:\n"
        "%s --mac 10.0.0.1 --dnet 123 --dadr 05\n",
        filename);
    printf(
        "Send a WhoIs request to MAC 10.1.2.3:47808:\n"
        "%s --mac 10.1.2.3:47808\n",
        filename);
    printf(
        "Send a WhoIs request to Device 123:\n"
        "%s 123\n",
        filename);
    printf(
        "Send a WhoIs request to Devices from 1000 to 9000:\n"
        "%s 1000 9000\n",
        filename);
    printf(
        "Send a WhoIs request to Devices from 1000 to 9000 on DNET 123:\n"
        "%s 1000 9000 --dnet 123\n",
        filename);
    printf(
        "Send a WhoIs request to all devices:\n"
        "%s\n",
        filename);
}

/* 主函数 */
int main(int argc, char *argv[])
{
    BACNET_ADDRESS src = { 0 }; /* 消息来源地址 */
    uint16_t pdu_len = 0; /* PDU 长度 */
    unsigned timeout_milliseconds = 0; /* 超时时间 (毫秒) */
    unsigned delay_milliseconds = 100; /* 接收响应的延迟时间 (毫秒) */
    struct mstimer apdu_timer = { 0 }; /* APDU 定时器 */
    struct mstimer datalink_timer = { 0 }; /* 数据链路层定时器 */
    long dnet = -1; /* 目标网络号 */
    BACNET_MAC_ADDRESS mac = { 0 }; /* 目标 MAC 地址 */
    BACNET_MAC_ADDRESS adr = { 0 }; /* 目标网络上的 MAC 地址 (SADR) */
    BACNET_ADDRESS dest = { 0 }; /* 目标 BACnet 地址 */
    bool global_broadcast = true; /* 是否为全局广播 */
    int argi = 0; /* 命令行参数索引 */
    unsigned int target_args = 0; /* 目标实例参数计数 */
    const char *filename = NULL; /* 程序文件名 */
    bool repeat_forever = false; /* 是否永远重复发送 */
    long retry_count = 0; /* 重试次数 */

    /* 检查本地环境变量设置 */
    if (getenv("BACNET_DEBUG")) /* 如果定义了 BACNET_DEBUG 环境变量 */
    {
        BACnet_Debug_Enabled = true; /* 启用调试信息打印 */
    }
    /* 解码命令行参数 */
    filename = filename_remove_path(argv[0]); /* 获取程序文件名 (不含路径) */
    for (argi = 1; argi < argc; argi++) /* 遍历命令行参数 */
    {
        if (strcmp(argv[argi], "--help") == 0) /* 如果是 --help 参数 */
        {
            print_usage(filename); /* 打印用法信息 */
            print_help(filename); /* 打印帮助信息 */
            return 0;
        }
        if (strcmp(argv[argi], "--version") == 0) /* 如果是 --version 参数 */
        {
            printf("%s %s\n", filename, BACNET_VERSION_TEXT); /* 打印版本信息 */
            printf("Copyright (C) 2014 by Steve Karg and others.\n"
                   "This is free software; see the source for copying "
                   "conditions.\n"
                   "There is NO warranty; not even for MERCHANTABILITY or\n"
                   "FITNESS FOR A PARTICULAR PURPOSE.\n"); /* 打印版权信息 */
            return 0;
        }
        if (strcmp(argv[argi], "--mac") == 0) /* 如果是 --mac 参数 */
        {
            if (++argi < argc) /* 获取下一个参数作为 MAC 地址 */
            {
                if (bacnet_address_mac_from_ascii(&mac, argv[argi])) /* 从 ASCII 字符串解析 MAC 地址 */
                {
                    global_broadcast = false; /* 如果指定了 MAC 地址，则不是全局广播 */
                }
            }
        }
        else if (strcmp(argv[argi], "--dnet") == 0) /* 如果是 --dnet 参数 */
        {
            if (++argi < argc) /* 获取下一个参数作为网络号 */
            {
                dnet = strtol(argv[argi], NULL, 0); /* 将字符串转换为长整型 */
                if ((dnet >= 0) && (dnet <= BACNET_BROADCAST_NETWORK)) /* 检查网络号是否有效 */
                {
                    global_broadcast = false; /* 如果指定了网络号，则不是全局广播 */
                }
            }
        }
        else if (strcmp(argv[argi], "--dadr") == 0) /* 如果是 --dadr 参数 */
        {
            if (++argi < argc) /* 获取下一个参数作为目标网络上的 MAC 地址 */
            {
                if (bacnet_address_mac_from_ascii(&adr, argv[argi])) /* 从 ASCII 字符串解析 MAC 地址 */
                {
                    global_broadcast = false; /* 如果指定了目标网络上的 MAC 地址，则不是全局广播 */
                }
            }
        }
        else if (strcmp(argv[argi], "--repeat") == 0) /* 如果是 --repeat 参数 */
        {
            repeat_forever = true; /* 设置永远重复发送标志 */
        }
        else if (strcmp(argv[argi], "--retry") == 0) /* 如果是 --retry 参数 */
        {
            if (++argi < argc) /* 获取下一个参数作为重试次数 */
            {
                retry_count = strtol(argv[argi], NULL, 0); /* 将字符串转换为长整型 */
                if (retry_count < 0) /* 确保重试次数不为负 */
                {
                    retry_count = 0;
                }
            }
        }
        else if (strcmp(argv[argi], "--timeout") == 0) /* 如果是 --timeout 参数 */
        {
            if (++argi < argc) /* 获取下一个参数作为超时时间 */
            {
                timeout_milliseconds = strtol(argv[argi], NULL, 0); /* 将字符串转换为长整型 */
            }
        }
        else if (strcmp(argv[argi], "--delay") == 0) /* 如果是 --delay 参数 */
        {
            if (++argi < argc) /* 获取下一个参数作为延迟时间 */
            {
                delay_milliseconds = strtol(argv[argi], NULL, 0); /* 将字符串转换为长整型 */
            }
        }
        else /* 如果是其他参数，则认为是目标对象实例 */
        {
            if (target_args == 0) /* 如果是第一个目标实例参数 */
            {
                Target_Object_Instance_Min = Target_Object_Instance_Max =
                    strtol(argv[argi], NULL, 0); /* 设置最小和最大目标实例 */
                target_args++;
            }
            else if (target_args == 1) /* 如果是第二个目标实例参数 */
            {
                Target_Object_Instance_Max = strtol(argv[argi], NULL, 0); /* 设置最大目标实例 */
                target_args++;
            }
            else /* 如果参数过多 */
            {
                print_usage(filename); /* 打印用法信息 */
                return 1; /* 返回错误码 */
            }
        }
    }
    if (global_broadcast) /* 如果是全局广播 */
    {
        datalink_get_broadcast_address(&dest); /* 获取数据链路层广播地址 */
    }
    else /* 如果不是全局广播 (即指定了目标地址) */
    {
        if (adr.len && mac.len) /* 如果同时指定了 MAC 地址和目标网络上的 MAC 地址 */
        {
            memcpy(&dest.mac[0], &mac.adr[0], mac.len); /* 复制 MAC 地址 */
            dest.mac_len = mac.len;
            memcpy(&dest.adr[0], &adr.adr[0], adr.len); /* 复制目标网络上的 MAC 地址 */
            dest.len = adr.len;
            if ((dnet >= 0) && (dnet <= BACNET_BROADCAST_NETWORK)) /* 如果指定了有效的网络号 */
            {
                dest.net = dnet; /* 设置目标网络号 */
            }
            else /* 如果网络号无效或未指定 */
            {
                dest.net = BACNET_BROADCAST_NETWORK; /* 使用广播网络号 */
            }
        }
        else if (mac.len) /* 如果只指定了 MAC 地址 */
        {
            memcpy(&dest.mac[0], &mac.adr[0], mac.len); /* 复制 MAC 地址 */
            dest.mac_len = mac.len;
            dest.len = 0; /* SADR 长度为 0 */
            if ((dnet >= 0) && (dnet <= BACNET_BROADCAST_NETWORK)) /* 如果指定了有效的网络号 */
            {
                dest.net = dnet; /* 设置目标网络号 */
            }
            else /* 如果网络号无效或未指定 */
            {
                dest.net = 0; /* 使用本地网络 */
            }
        }
        else /* 如果只指定了网络号 (或两者都未指定，但 global_broadcast 为 false 的情况，理论上不应发生) */
        {
            if ((dnet >= 0) && (dnet <= BACNET_BROADCAST_NETWORK)) /* 如果指定了有效的网络号 */
            {
                dest.net = dnet; /* 设置目标网络号 */
            }
            else /* 如果网络号无效或未指定 */
            {
                dest.net = BACNET_BROADCAST_NETWORK; /* 使用广播网络号 */
            }
            dest.mac_len = 0; /* MAC 地址长度为 0 */
            dest.len = 0; /* SADR 长度为 0 */
        }
    }
    /* 检查目标对象实例范围是否有效 */
    if (Target_Object_Instance_Min > BACNET_MAX_INSTANCE)
    {
        fprintf(
            stderr, "device-instance-min=%u - not greater than %u\n",
            (unsigned int)Target_Object_Instance_Min, (unsigned int)BACNET_MAX_INSTANCE);
        return 1;
    }
    if (Target_Object_Instance_Max > BACNET_MAX_INSTANCE)
    {
        fprintf(
            stderr, "device-instance-max=%u - not greater than %u\n",
            (unsigned int)Target_Object_Instance_Max, (unsigned int)BACNET_MAX_INSTANCE);
        return 1;
    }
    /* 设置自身信息 */
    Device_Set_Object_Instance_Number(BACNET_MAX_INSTANCE); /* 设置本设备的对象实例号 (通常为最大值以避免冲突) */
    init_service_handlers(); /* 初始化服务处理函数 */
    address_init(); /* 初始化地址绑定 */
    dlenv_init(); /* 初始化数据链路层环境变量 */
    atexit(datalink_cleanup); /* 注册程序退出时调用的数据链路层清理函数 */
    if (timeout_milliseconds == 0) /* 如果未指定超时时间 */
    {
        timeout_milliseconds = apdu_timeout() * apdu_retries(); /* 使用默认的 APDU 超时时间和重试次数计算 */
    }
    mstimer_set(&apdu_timer, timeout_milliseconds); /* 设置 APDU 定时器 */
    mstimer_set(&datalink_timer, 1000); /* 设置数据链路层维护定时器 (1秒) */
    /* 发送请求 */
    Send_WhoIs_To_Network(
        &dest, Target_Object_Instance_Min, Target_Object_Instance_Max); /* 向网络发送 Who-Is 请求 */
    if (retry_count > 0) /* 如果需要重试 */
    {
        retry_count--; /* 重试次数减一 */
    }
    /* 无限循环处理 */
    for (;;)
    {
        /* 在指定的延迟时间内接收数据，超时返回 0 字节 */
        pdu_len =
            datalink_receive(&src, &Rx_Buf[0], MAX_MPDU, delay_milliseconds);
        /* 处理接收到的数据 */
        if (pdu_len)
        {
            npdu_handler(&src, &Rx_Buf[0], pdu_len); /* 调用 NPDU 处理函数 */
        }
        if (Error_Detected) /* 如果检测到错误 */
        {
            break; /* 退出循环 */
        }
        if (mstimer_expired(&datalink_timer)) /* 如果数据链路层定时器超时 */
        {
            datalink_maintenance_timer(
                mstimer_interval(&datalink_timer) / 1000); /* 执行数据链路层维护 */
            mstimer_reset(&datalink_timer); /* 重置数据链路层定时器 */
        }
        if (mstimer_expired(&apdu_timer)) /* 如果 APDU 定时器超时 */
        {
            if (repeat_forever || retry_count) /* 如果需要永远重复或还有重试次数 */
            {
                Send_WhoIs_To_Network(
                    &dest, Target_Object_Instance_Min,
                    Target_Object_Instance_Max); /* 重新发送 Who-Is 请求 */
                if (retry_count > 0) /* 如果还有重试次数 */
                {
                   retry_count--; /* 重试次数减一 */
                }
            }
            else /* 如果不需要重复且没有重试次数 */
            {
                break; /* 退出循环 */
            }
            mstimer_reset(&apdu_timer); /* 重置 APDU 定时器 */
        }
    }
    print_address_cache(); /* 打印发现的设备地址缓存 */

    return 0; /* 程序正常结束 */
}
