#!/bin/bash
# 并发上传测试（文件名不含路径）
# 用法: ./concurrent_upload_test.sh

PROJECT_ROOT="$(cd "$(dirname "$0")" && pwd)"
CLIENT="$PROJECT_ROOT/bin/client"
SERVER_DIR="$PROJECT_ROOT/server_files"
THREADS=10
FILE_SIZE_MB=5
TEST_DIR="$PROJECT_ROOT/upload_concurrent_test"

RED='\033[0;31m'
GREEN='\033[0;32m'
NC='\033[0m'

cleanup() {
    # 清空固定测试目录
    rm -rf "$TEST_DIR"
    # 删除服务端文件
    for i in $(seq 1 $THREADS); do
        rm -f "$SERVER_DIR/file_$i.dat"
    done
}

generate_files() {
    #先创建目录
    mkdir -p "$TEST_DIR"
    echo "生成 $THREADS 个 ${FILE_SIZE_MB}MB 文件 → $TEST_DIR"
    
    # 进入固定目录生成文件
    pushd "$TEST_DIR" > /dev/null

    for i in $(seq 1 $THREADS); do
        dd if=/dev/urandom of="file_$i.dat" bs=1M count=$FILE_SIZE_MB 2>/dev/null
    done
    
    popd > /dev/null
    echo "生成完成"
}

concurrent_upload() {
    echo -e "\n========== 并发上传测试 ($THREADS 个文件同时上传) =========="
    pids=()
    # 从固定目录上传
    pushd "$TEST_DIR" > /dev/null

    for i in $(seq 1 $THREADS); do
        $CLIENT upload "file_$i.dat" > "upload_$i.log" 2>&1 &
        pids+=($!)
    done
    for pid in "${pids[@]}"; do
        wait $pid
    done

    popd > /dev/null
    echo "所有上传任务已完成"

    local fail=0
    for i in $(seq 1 $THREADS); do
        local server_file="$SERVER_DIR/file_$i.dat"
        local local_file="$TEST_DIR/file_$i.dat"

        if [ ! -f "$server_file" ]; then
            echo -e "${RED} 文件 file_$i.dat 未上传到服务端${NC}"
            fail=1
        else
            local local_size=$(stat -c%s "$local_file" 2>/dev/null || stat -f%z "$local_file" 2>/dev/null)
            local server_size=$(stat -c%s "$server_file" 2>/dev/null || stat -f%z "$server_file" 2>/dev/null)
            if [ "$local_size" -ne "$server_size" ]; then
                echo -e "${RED} 文件 file_$i.dat 大小不匹配 (本地 $local_size, 服务端 $server_size)${NC}"
                fail=1
            fi
        fi
    done

    if [ $fail -eq 0 ]; then
        echo -e "${GREEN} 并发上传测试通过${NC}"
    else
        echo -e "${RED} 并发上传测试失败${NC}"
    fi
    return $fail
}

main() {
    echo "=== 并发上传测试 ==="
    cleanup
    generate_files

    if ! pgrep -x "server" > /dev/null; then
        echo "服务端未运行，请先启动: ./bin/server &"
        exit 1
    fi

    concurrent_upload
    result=$?
    cleanup
    exit $result
}

main