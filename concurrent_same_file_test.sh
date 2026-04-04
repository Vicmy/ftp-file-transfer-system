#!/bin/bash
# 并发上传相同文件测试（绝对路径版）

PROJECT_ROOT="$(cd "$(dirname "$0")" && pwd)"
CLIENT="$PROJECT_ROOT/bin/client"
THREADS=5
FILE_SIZE_MB=10
TEST_DIR="$PROJECT_ROOT/same_file_test"

RED='\033[0;31m'
GREEN='\033[0;32m'
NC='\033[0m'

cleanup() {
    rm -rf "$TEST_DIR"
    rm -f "$PROJECT_ROOT/server_files/same_file.dat"
}

generate_file() {
    mkdir -p "$TEST_DIR"
    echo "生成一个 ${FILE_SIZE_MB}MB 测试文件..."
    dd if=/dev/urandom of="$TEST_DIR/same_file.dat" bs=1M count=$FILE_SIZE_MB 2>/dev/null
}

concurrent_upload_same() {
    echo -e "\n========== 并发上传相同文件测试（$THREADS 个客户端同时上传） =========="
    pids=()
    for i in $(seq 1 $THREADS); do
        (cd "$TEST_DIR" && $CLIENT upload "same_file.dat" > "upload_$i.log" 2>&1) &
        pids+=($!)
    done
    for pid in "${pids[@]}"; do wait $pid; done
    echo "所有上传任务已完成"

    local server_file="$PROJECT_ROOT/server_files/same_file.dat"
    if [ ! -f "$server_file" ]; then
        echo -e "${RED}❌ 服务端未收到文件${NC}"
        return 1
    fi

    local server_size=$(stat -c%s "$server_file" 2>/dev/null || stat -f%z "$server_file" 2>/dev/null)
    local local_size=$(stat -c%s "$TEST_DIR/same_file.dat" 2>/dev/null || stat -f%z "$TEST_DIR/same_file.dat" 2>/dev/null)
    if [ "$server_size" -eq "$local_size" ] && cmp -s "$server_file" "$TEST_DIR/same_file.dat"; then
        echo -e "${GREEN}✅ 文件锁测试通过（最终文件完整）${NC}"
        return 0
    else
        echo -e "${RED}❌ 文件锁测试失败（文件损坏或不完整）${NC}"
        return 1
    fi
}

main() {
    echo "=== 并发上传相同文件测试（文件锁） ==="
    cleanup
    generate_file
    if ! pgrep -x "server" > /dev/null; then
        echo "服务端未运行，请先启动: $PROJECT_ROOT/bin/server &"
        exit 1
    fi
    concurrent_upload_same
    result=$?
    cleanup
    exit $result
}

main