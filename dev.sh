#!/home/hd/software/zsh/bin/zsh

# 智能开发脚本：自动创建、启动或进入配置了 Zsh 的 BACnet 开发容器

CONTAINER_NAME="bacnet-dev-container"
IMAGE_NAME="bacnet-dev-env"
# 直接使用已设置的环境变量，它应该指向 .../bacnet-stack
PROJECT_ROOT="$BACNET_PROJECT_ROOT"

# --- 检查配置 ---
if [ -z "$PROJECT_ROOT" ]; then
    echo "错误: 环境变量 BACNET_PROJECT_ROOT 未设置。"
    echo "请先在 ~/.bashrc 或 ~/.zshrc 中设置 export BACNET_PROJECT_ROOT=/path/to/your/project"
    exit 1
fi

if [ ! -f "$PROJECT_ROOT/CMakeLists.txt" ]; then
    echo "错误: 在 $PROJECT_ROOT 中未找到顶层 CMakeLists.txt 文件。"
    exit 1
fi

# --- 主逻辑 ---

# 检查容器是否正在运行
if [ "$(docker ps -q -f name=^/${CONTAINER_NAME}$)" ]; then
    echo "容器正在运行，直接进入 Zsh..."
    docker exec -it --user developer ${CONTAINER_NAME} zsh
# 检查容器是否存在但已停止
elif [ "$(docker ps -aq -f status=exited -f name=^/${CONTAINER_NAME}$)" ]; then
    echo "发现已停止的容器，正在启动并进入 Zsh..."
    docker start ${CONTAINER_NAME}
    docker exec -it --user developer ${CONTAINER_NAME} zsh
# 如果容器不存在，则创建、启动并进入
else
    echo "未发现容器，正在创建并启动..."
    docker run \
        -d \
        --name ${CONTAINER_NAME} \
        -p 47808:47808/udp \
        -v "${PROJECT_ROOT}":/home/developer/bacnet-stack \
        -w /home/developer/bacnet-stack \
        ${IMAGE_NAME}
    
    echo "容器已在后台启动，现在进入 Zsh..."
    docker exec -it --user developer ${CONTAINER_NAME} zsh
fi