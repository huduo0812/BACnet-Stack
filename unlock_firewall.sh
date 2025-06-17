#!/bin/bash

# =================================================================
# BACnet 端口防火墙“解锁”脚本
#
# 功能:
# 1. 在后台启动一个临时的 BACnet 服务器进程。
# 2. 等待1秒，给予其足够的时间来发送出站数据包以“预热”防火墙。
# 3. 结束这个临时的服务器进程。
# 4. 整个过程不会有程序本身的输出，只有提示信息。
# =================================================================

# --- 检查环境变量 ---
if [ -z "$BACNET_PROJECT_ROOT" ]; then
    echo "❌ 错误: 环境变量 BACNET_PROJECT_ROOT 未设置！"
    echo "   请先在 ~/.zshrc 中设置。"
    exit 1
fi

# --- 定义可执行文件路径 ---
SERVER_EXEC_PATH="$BACNET_PROJECT_ROOT/build/server"

# --- 检查文件是否存在 ---
if [ ! -f "$SERVER_EXEC_PATH" ]; then
    echo "❌ 错误: BACnet 服务器程序未找到于:"
    echo "   $SERVER_EXEC_PATH"
    echo "   请先进到开发容器内，运行 'cmake .. && make' 进行编译。"
    exit 1
fi

# --- 主逻辑 ---
echo "🚀 开始执行防火墙端口解锁操作..."

# 在后台启动服务器，并将所有输出重定向到/dev/null (即丢弃)
"$SERVER_EXEC_PATH" 5678 "Firewall Unlocker" > /dev/null 2>&1 &

# 获取刚刚在后台启动的进程的ID (PID)
SERVER_PID=$!

echo "⏳ 临时服务器已启动 (进程ID: $SERVER_PID)，等待 1 秒..."
sleep 1

# 检查进程是否存在，然后结束它
if ps -p $SERVER_PID > /dev/null; then
    echo "🛑 正在结束临时服务器进程 (PID: $SERVER_PID)..."
    kill $SERVER_PID
    # 等待一小会确保进程完全退出
    sleep 0.5
else
    echo "🤔 警告: 临时服务器进程似乎已经自行退出了。"
fi

echo "✅ 操作完成！防火墙的 UDP 47808 端口已被“预热”。"
echo "   现在您可以在 Yabe 中尝试发现设备了。"