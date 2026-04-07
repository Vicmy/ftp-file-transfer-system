#!/bin/bash
# 并发下载测试（绝对路径）

PROJECT_ROOT="$(cd "$(dirname "$0")" && pwd)"
CLIENT="$PROJECT_ROOT/bin/client"
THREADS=10
FILE_SIZE_MB=5
TEST_DIR="$PROJECT_ROOT/download_concurrent_test"

RED='\033[0;31m'
GREEN='\033[0;32m'
NC='\033[0m'

cleanup() {
    rm -rf "$TEST_DIR"
}

prepare_files() {
    mkdir -p "$TEST_DIR"
    echo "检查服务端测试文件..."
    local need_upload=0
    for i in $(seq 1 $THREADS); do
        if [ ! -f "$PROJECT_ROOT/server_files/file_$i.dat" ]; then
            need_upload=1
            break
        fi
    done
    if [ $need_upload -eq 1 ]; then
        echo "服务端文件缺失，开始上传 $THREADS 个 ${FILE_SIZE_MB}MB 文件..."
        pushd "$TEST_DIR" > /dev/null
        for i in $(seq 1 $THREADS); do
            dd if=/dev/urandom of="file_$i.dat" bs=1M count=$FILE_SIZE_MB 2>/dev/null
            $CLIENT upload "file_$i.dat" > "upload_$i.log" 2>&1
            if [ ! -f "$PROJECT_ROOT/server_files/file_$i.dat" ]; then
                echo -e "${RED}上传 file_$i.dat 失败${NC}"
                cat "upload_$i.log"
                popd > /dev/null
                exit 1
            fi
            rm -f "file_$i.dat"
            echo -n "."
        done
        popd > /dev/null
        echo
        echo "服务端准备完成"
    else
        echo "服务端文件已存在"
    fi
}

concurrent_download() {
    echo -e "\n========== 并发下载测试 ($THREADS 个文件同时下载) =========="
    mkdir -p "$TEST_DIR"
    pids=()
    for i in $(seq 1 $THREADS); do
        (cd "$TEST_DIR" && $CLIENT download "file_$i.dat" > "download_$i.log" 2>&1) &
        pids+=($!)
    done
    for pid in "${pids[@]}"; do wait $pid; done
    echo "所有下载任务已完成"

    local fail=0
    for i in $(seq 1 $THREADS); do
        local downloaded="$TEST_DIR/file_$i.dat"
        local server_file="$PROJECT_ROOT/server_files/file_$i.dat"
        if [ ! -f "$downloaded" ]; then
            echo -e "${RED} 文件 file_$i.dat 未下载到本地${NC}"
            fail=1
        elif ! cmp -s "$server_file" "$downloaded"; then
            echo -e "${RED} 文件 file_$i.dat 内容不一致${NC}"
            fail=1
        fi
    done

    if [ $fail -eq 0 ]; then
        echo -e "${GREEN} 并发下载测试通过${NC}"
    else
        echo -e "${RED} 并发下载测试失败${NC}"
    fi
    return $fail
}

main() {
    echo "=== 并发下载测试 ==="
    cleanup
    prepare_files
    if ! pgrep -x "server" > /dev/null; then
        echo "服务端未运行，请先启动: $PROJECT_ROOT/bin/server &"
        exit 1
    fi
    concurrent_download
    result=$?
    cleanup
    exit $result
}

main