#!/bin/bash
# 一次性运行所有并发测试

PROJECT_ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$PROJECT_ROOT"

echo "========================================="
echo "  文件传输系统并发测试套件"
echo "========================================="

# 检查服务端是否运行
if ! pgrep -x "server" > /dev/null; then
    echo "错误：服务端未运行，请先启动: ./bin/server &"
    exit 1
fi

# 1. 并发上传测试
echo -e "\n[1/4] 运行并发上传测试..."
./concurrent_upload_fixed.sh
if [ $? -ne 0 ]; then
    echo "❌ 并发上传测试失败，停止后续测试"
    exit 1
fi

# 2. 并发下载测试
echo -e "\n[2/4] 运行并发下载测试..."
./concurrent_download_test.sh
if [ $? -ne 0 ]; then
    echo "❌ 并发下载测试失败，停止后续测试"
    exit 1
fi

# 3. 混合并发测试
echo -e "\n[3/4] 运行混合并发测试..."
./concurrent_mixed_test.sh
if [ $? -ne 0 ]; then
    echo "❌ 混合并发测试失败，停止后续测试"
    exit 1
fi

# 4. 相同文件锁测试
echo -e "\n[4/4] 运行相同文件锁测试..."
./concurrent_same_file_test.sh
if [ $? -ne 0 ]; then
    echo "❌ 相同文件锁测试失败"
    exit 1
fi

echo -e "\n========================================="
echo "  🎉 所有并发测试全部通过！"
echo "========================================="