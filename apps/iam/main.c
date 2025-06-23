/**
 * @file
 * @brief command line tool that sends a BACnet service to the network:
 * I-Am message
 * @author Steve Karg <skarg@users.sourceforge.net>
 * @date 2016
 * @copyright SPDX-License-Identifier: MIT
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h> /* for time */
/* BACnet Stack defines - first */
#include "bacnet/bacdef.h"
/* BACnet Stack API */
#include "bacnet/bactext.h"
#include "bacnet/iam.h"
#include "bacnet/npdu.h"
#include "bacnet/apdu.h"
#include "bacnet/version.h"
/* some demo stuff needed */
#include "bacnet/basic/binding/address.h"
#include "bacnet/basic/object/device.h"
#include "bacnet/basic/sys/filename.h"
#include "bacnet/basic/services.h"
#include "bacnet/basic/tsm/tsm.h"
#include "bacnet/datalink/datalink.h"
#include "bacnet/datalink/dlenv.h"

/* 用于接收的缓冲区 */
static uint8_t Rx_Buf[MAX_MPDU] = { 0 };

/* 解析的命令行参数 */
static uint32_t Target_Device_ID = BACNET_MAX_INSTANCE; /* 目标设备 ID */
static uint16_t Target_Vendor_ID = BACNET_VENDOR_ID; /* 目标厂商 ID */
static unsigned int Target_Max_APDU = MAX_APDU; /* 目标最大 APDU */
static int Target_Segmentation = SEGMENTATION_NONE; /* 目标分段类型 */
/* 用于标记错误的标志 */
static bool Error_Detected = false;

/* Abort 服务处理函数 */
static void MyAbortHandler(
    BACNET_ADDRESS *src, uint8_t invoke_id, uint8_t abort_reason, bool server)
{
    (void)src;
    (void)invoke_id;
    (void)server;
    printf("BACnet Abort: %s\n", bactext_abort_reason_name(abort_reason));
    Error_Detected = true;
}

/* Reject 服务处理函数 */
static void
MyRejectHandler(BACNET_ADDRESS *src, uint8_t invoke_id, uint8_t reject_reason)
{
    (void)src;
    (void)invoke_id;
    printf("BACnet Reject: %s\n", bactext_reject_reason_name(reject_reason));
    Error_Detected = true;
}

/* 初始化服务处理函数 */
static void Init_Service_Handlers(void)
{
    Device_Init(NULL);
    /* 我们需要处理 who-is 以支持动态设备绑定 */
    apdu_set_unconfirmed_handler(SERVICE_UNCONFIRMED_WHO_IS, handler_who_is);
    /* 为我们未实现的所有服务设置处理程序 */
    /* 发送正确的 reject 消息是必需的... */
    apdu_set_unrecognized_service_handler_handler(handler_unrecognized_service);
    /* 我们必须实现 read property - 这是必需的! */
    apdu_set_confirmed_handler(
        SERVICE_CONFIRMED_READ_PROPERTY, handler_read_property);
    /* 处理返回的回复(请求) */
    apdu_set_unconfirmed_handler(SERVICE_UNCONFIRMED_I_AM, handler_i_am_add);
    /* 处理返回的任何错误 */
    apdu_set_abort_handler(MyAbortHandler);
    apdu_set_reject_handler(MyRejectHandler);
}

/* 打印用法信息 */
static void print_usage(const char *filename)
{
    printf(
        "Usage: %s [device-instance vendor-id max-apdu segmentation]\n",
        filename);
    printf("       [--dnet][--dadr][--mac]\n");
    printf("       [--version][--help]\n");
}

/* 打印帮助信息 */
static void print_help(const char *filename)
{
    printf("Send BACnet I-Am message for a device.\n");
    printf("--mac A\n"
           "Optional BACnet mac address."
           "Valid ranges are from 00 to FF (hex) for MS/TP or ARCNET,\n"
           "or an IP string with optional port number like 10.1.2.3:47808\n"
           "or an Ethernet MAC in hex like 00:21:70:7e:32:bb\n");
    printf("\n");
    printf("--dnet N\n"
           "Optional BACnet network number N for directed requests.\n"
           "Valid range is from 0 to 65535 where 0 is the local connection\n"
           "and 65535 is network broadcast.\n");
    printf("\n");
    printf("--dadr A\n"
           "Optional BACnet mac address on the destination BACnet network "
           "number.\n"
           "Valid ranges are from 00 to FF (hex) for MS/TP or ARCNET,\n"
           "or an IP string with optional port number like 10.1.2.3:47808\n"
           "or an Ethernet MAC in hex like 00:21:70:7e:32:bb\n");
    printf("\n");
    printf("--repeat\n"
           "Send the message repeatedly until signalled to quit.\n"
           "Default is to not repeat, sending only a single message.\n");
    printf("\n");
    printf("--retry C\n"
           "Send the message C number of times\n"
           "Default is retry 0, only sending one time.\n");
    printf("\n");
    printf("--delay\n"
           "Delay, in milliseconds, between repeated messages.\n"
           "Default delay is 100ms.\n");
    printf("\n");
    printf("device-instance:\n"
           "BACnet device-ID 0..4194303\n");
    printf("\n");
    printf("vendor-id:\n"
           "Vendor Identifier 0..65535\n");
    printf("\n");
    printf("max-apdu:\n"
           "Maximum APDU size 50..65535\n");
    printf("\n");
    printf("segmentation:\n"
           "BACnet Segmentation 0=both, 1=transmit, 2=receive, 3=none\n");
    printf("\n");
    printf(
        "Example:\n"
        "To send an I-Am message of instance=1234 vendor-id=260 max-apdu=480\n"
        "%s 1234 260 480\n",
        filename);
}

/* 主函数 */
int main(int argc, char *argv[])
{
    BACNET_ADDRESS src = { 0 }; /* 消息来源地址 */
    uint16_t pdu_len = 0; /* PDU 长度 */
    long dnet = -1; /* 目标网络号 */
    BACNET_MAC_ADDRESS mac = { 0 }; /* 目标 MAC 地址 */
    BACNET_MAC_ADDRESS adr = { 0 }; /* 目标网络上的 MAC 地址 (SADR) */
    BACNET_ADDRESS dest = { 0 }; /* 目标 BACnet 地址 */
    bool specific_address = false; /* 是否为特定地址 */
    bool repeat_forever = false; /* 是否永远重复发送 */
    unsigned timeout = 100; /* 超时时间 (毫秒) */
    int argi = 0; /* 命令行参数索引 */
    unsigned int target_args = 0; /* 目标参数计数 */
    const char *filename = NULL; /* 程序文件名 */
    long retry_count = 0; /* 重试次数 */

    /* 获取程序文件名 (不含路径) */
    filename = filename_remove_path(argv[0]);
    /* 遍历命令行参数 */
    for (argi = 1; argi < argc; argi++)
    {
        /* 检查是否为 --help 参数 */
        if (strcmp(argv[argi], "--help") == 0)
        {
            print_usage(filename);
            print_help(filename);
            return 0;
        }
        /* 检查是否为 --version 参数 */
        if (strcmp(argv[argi], "--version") == 0)
        {
            printf("%s %s\n", filename, BACNET_VERSION_TEXT);
            printf("Copyright (C) 2016 by Steve Karg and others.\n"
                   "This is free software; see the source for copying "
                   "conditions.\n"
                   "There is NO warranty; not even for MERCHANTABILITY or\n"
                   "FITNESS FOR A PARTICULAR PURPOSE.\n");
            return 0;
        }
        /* 解析 --mac 参数 */
        if (strcmp(argv[argi], "--mac") == 0)
        {
            if (++argi < argc)
            {
                if (bacnet_address_mac_from_ascii(&mac, argv[argi]))
                {
                    specific_address = true;
                }
            }
        }
        /* 解析 --dnet 参数 */
        else if (strcmp(argv[argi], "--dnet") == 0)
        {
            if (++argi < argc)
            {
                dnet = strtol(argv[argi], NULL, 0);
                if ((dnet >= 0) && (dnet <= BACNET_BROADCAST_NETWORK))
                {
                    specific_address = true;
                }
            }
        }
        /* 解析 --dadr 参数 */
        else if (strcmp(argv[argi], "--dadr") == 0)
        {
            if (++argi < argc)
            {
                if (bacnet_address_mac_from_ascii(&adr, argv[argi]))
                {
                    specific_address = true;
                }
            }
        }
        /* 解析 --repeat 参数 */
        else if (strcmp(argv[argi], "--repeat") == 0)
        {
            repeat_forever = true;
        }
        /* 解析 --retry 参数 */
        else if (strcmp(argv[argi], "--retry") == 0)
        {
            if (++argi < argc)
            {
                retry_count = strtol(argv[argi], NULL, 0);
                if (retry_count < 0)
                {
                    retry_count = 0;
                }
            }
        }
        /* 解析 --delay 参数 */
        else if (strcmp(argv[argi], "--delay") == 0)
        {
            if (++argi < argc)
            {
                timeout = strtol(argv[argi], NULL, 0);
            }
        }
        /* 解析 I-Am 服务的参数 */
        else
        {
            if (target_args == 0)
            {
                /* 第一个参数是设备 ID */
                Target_Device_ID = strtol(argv[argi], NULL, 0);
                target_args++;
            }
            else if (target_args == 1)
            {
                /* 第二个参数是厂商 ID */
                Target_Vendor_ID = strtol(argv[argi], NULL, 0);
                target_args++;
            }
            else if (target_args == 2)
            {
                /* 第三个参数是最大 APDU */
                Target_Max_APDU = strtol(argv[argi], NULL, 0);
                target_args++;
            }
            else if (target_args == 3)
            {
                /* 第四个参数是分段支持 */
                Target_Segmentation = strtol(argv[argi], NULL, 0);
                target_args++;
            }
            else
            {
                /* 参数过多，打印用法并退出 */
                print_usage(filename);
                return 1;
            }
        }
    }
    /* 初始化地址绑定 */
    address_init();
    /* 如果指定了目标地址，则构建目标地址结构 */
    if (specific_address)
    {
        if (adr.len && mac.len)
        {
            /* 具有 DNET 和 DADR 的远程网络 */
            memcpy(&dest.mac[0], &mac.adr[0], mac.len);
            dest.mac_len = mac.len;
            memcpy(&dest.adr[0], &adr.adr[0], adr.len);
            dest.len = adr.len;
            if ((dnet >= 0) && (dnet <= BACNET_BROADCAST_NETWORK))
            {
                dest.net = dnet;
            }
            else
            {
                dest.net = BACNET_BROADCAST_NETWORK;
            }
        }
        else if (mac.len)
        {
            /* 具有 MAC 地址的本地或远程网络 */
            memcpy(&dest.mac[0], &mac.adr[0], mac.len);
            dest.mac_len = mac.len;
            dest.len = 0;
            if ((dnet >= 0) && (dnet <= BACNET_BROADCAST_NETWORK))
            {
                dest.net = dnet;
            }
            else
            {
                dest.net = 0;
            }
        }
        else
        {
            /* 仅具有 DNET 的远程网络 */
            if ((dnet >= 0) && (dnet <= BACNET_BROADCAST_NETWORK))
            {
                dest.net = dnet;
            }
            else
            {
                dest.net = BACNET_BROADCAST_NETWORK;
            }
            dest.mac_len = 0;
            dest.len = 0;
        }
    }
    /* 设置我的信息 - 使用最大实例号以避免冲突 */
    Device_Set_Object_Instance_Number(BACNET_MAX_INSTANCE);
    /* 初始化服务处理程序 */
    Init_Service_Handlers();
    /* 初始化地址绑定 */
    address_init();
    /* 初始化数据链路层环境变量 */
    dlenv_init();
    /* 注册程序退出时调用的清理函数 */
    atexit(datalink_cleanup);
    /* 发送请求 */
    do
    {
        /* 发送 I-Am 消息到网络 */
        Send_I_Am_To_Network(
            &dest, Target_Device_ID, Target_Max_APDU, Target_Segmentation,
            Target_Vendor_ID);
        /* 如果需要重复发送或重试 */
        if (repeat_forever || retry_count)
        {
            /* 在超时时间内接收数据，超时返回 0 字节 */
            pdu_len = datalink_receive(&src, &Rx_Buf[0], MAX_MPDU, timeout);
            /* 如果接收到数据，则处理 */
            if (pdu_len)
            {
                npdu_handler(&src, &Rx_Buf[0], pdu_len);
            }
            /* 如果检测到错误，则退出循环 */
            if (Error_Detected)
            {
                break;
            }
            /* 如果还有重试次数，则减一 */
            if (retry_count > 0)
            {
                retry_count--;
            }
        }
    } while (repeat_forever || retry_count); /* 如果需要永远重复或还有重试次数，则继续循环 */

    return 0;
}
