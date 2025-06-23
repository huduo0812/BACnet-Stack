#!/home/hd/software/zsh/bin/zsh

# 开发环境镜像构建脚本
# 它会自动获取当前用户的ID，并传递给 Dockerfile，以解决权限问题

echo "📦 正在为当前用户 (UID: $(id -u), GID: $(id -g)) 构建开发镜像..."

docker build \
    --build-arg HOST_UID=$(id -u) \
    --build-arg HOST_GID=$(id -g) \
    -t bacnet-dev-env \
    -f Dockerfile.dev \
    .

echo "✅ 开发镜像 'bacnet-dev-env' 构建完成！"