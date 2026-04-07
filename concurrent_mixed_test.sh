#!/bin/bash
# 混合并发测试（绝对路径）

PROJECT_ROOT="$(cd "$(dirname "$0")" && pwd)"
CLIENT="$PROJECT_ROOT/bin/client"
UPLOAD_THREADS=5
DOWNLOAD_THREADS=5
FILE_SIZE_MB=5
TEST_DIR="$PROJECT_ROOT/mixed_test_files"
#TEST_DIR="$PROJECT_ROOT/mixed_test_$(date +%s)"

RED='\033[0;31m'
GREEN='\033[0;32m'
NC='\033[0m'

cleanup() {
    rm -rf "$TEST_DIR"
    for i in $(seq 1 $UPLOAD_THREADS); do
        rm -f "$PROJECT_ROOT/server_files/up_$i.dat"
    done
    for i in $(seq 1 $DOWNLOAD_THREADS); do
        rm -f "$PROJECT_ROOT/server_files/down_$i.dat"
    done
}

generate_upload_files() {
    mkdir -p "$TEST_DIR"
    echo "生成 $UPLOAD_THREADS 个上传文件（每个 ${FILE_SIZE_MB}MB）..."
    pushd "$TEST_DIR" > /dev/null
    for i in $(seq 1 $UPLOAD_THREADS); do
        dd if=/dev/urandom of="up_$i.dat" bs=1M count=$FILE_SIZE_MB 2>/dev/null
    done
    popd > /dev/null
}

prepare_download_files() {
    echo "准备 $DOWNLOAD_THREADS 个下载文件到服务端..."
    mkdir -p "$TEST_DIR"
    pushd "$TEST_DIR" > /dev/null
    for i in $(seq 1 $DOWNLOAD_THREADS); do
        dd if=/dev/urandom of="down_$i.dat" bs=1M count=$FILE_SIZE_MB 2>/dev/null
        $CLIENT upload "down_$i.dat" > "upload_down_$i.log" 2>&1
        if [ ! -f "$PROJECT_ROOT/server_files/down_$i.dat" ]; then
            echo -e "${RED}上传准备文件 down_$i.dat 失败${NC}"
            cat "upload_down_$i.log"
            popd > /dev/null
            exit 1
        fi
        rm -f "down_$i.dat"
        echo -n "."
    done
    popd > /dev/null
    echo
    echo "服务端准备完成"
}

mixed_concurrent() {
    echo -e "\n========== 混合并发测试（$UPLOAD_THREADS 上传 + $DOWNLOAD_THREADS 下载同时进行） =========="
    pids=()
    for i in $(seq 1 $UPLOAD_THREADS); do
        (cd "$TEST_DIR" && $CLIENT upload "up_$i.dat" > "upload_$i.log" 2>&1) &
        pids+=($!)
    done
    for i in $(seq 1 $DOWNLOAD_THREADS); do
        (cd "$TEST_DIR" && $CLIENT download "down_$i.dat" > "download_$i.log" 2>&1) &
        pids+=($!)
    done
    for pid in "${pids[@]}"; do wait $pid; done
    echo "所有混合任务已完成"

    local upload_fail=0
    for i in $(seq 1 $UPLOAD_THREADS); do
        local server_file="$PROJECT_ROOT/server_files/up_$i.dat"
        local local_file="$TEST_DIR/up_$i.dat"
        if [ ! -f "$server_file" ]; then
            echo -e "${RED} 上传文件 up_$i.dat 未收到${NC}"
            upload_fail=1
        else
            local local_size=$(stat -c%s "$local_file" 2>/dev/null || stat -f%z "$local_file" 2>/dev/null)
            local server_size=$(stat -c%s "$server_file" 2>/dev/null || stat -f%z "$server_file" 2>/dev/null)
            if [ "$local_size" -ne "$server_size" ]; then
                echo -e "${RED} 上传文件 up_$i.dat 大小不匹配${NC}"
                upload_fail=1
            fi
        fi
    done

    local download_fail=0
    for i in $(seq 1 $DOWNLOAD_THREADS); do
        local downloaded="$TEST_DIR/down_$i.dat"
        local server_file="$PROJECT_ROOT/server_files/down_$i.dat"
        if [ ! -f "$downloaded" ]; then
            echo -e "${RED} 下载文件 down_$i.dat 未下载到本地${NC}"
            download_fail=1
        elif ! cmp -s "$server_file" "$downloaded"; then
            echo -e "${RED} 下载文件 down_$i.dat 内容不一致${NC}"
            download_fail=1
        fi
    done

    if [ $upload_fail -eq 0 ] && [ $download_fail -eq 0 ]; then
        echo -e "${GREEN} 混合并发测试通过${NC}"
        return 0
    else
        echo -e "${RED} 混合并发测试失败${NC}"
        return 1
    fi
}

main() {
    echo "=== 混合并发测试 ==="
    cleanup
    generate_upload_files
    prepare_download_files
    if ! pgrep -x "server" > /dev/null; then
        echo "服务端未运行，请先启动: $PROJECT_ROOT/bin/server &"
        exit 1
    fi
    mixed_concurrent
    result=$?
    cleanup
    exit $result
}

main