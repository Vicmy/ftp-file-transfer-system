#!/bin/bash
# 并发与断点续传综合测试脚本
# 功能：并发上传/下载、断点续传、混合并发测试

PROJECT_ROOT="$(cd "$(dirname "$0")" && pwd)"
CLIENT="$PROJECT_ROOT/bin/client"
SERVER_DIR="$PROJECT_ROOT/server_files"

# 并发配置
UPLOAD_THREADS=5
DOWNLOAD_THREADS=5
FILE_SIZE_MB=5

# 固定测试目录
UPLOAD_DIR="$PROJECT_ROOT/test_upload"
DOWNLOAD_DIR="$PROJECT_ROOT/test_download"

# 颜色输出
RED='\033[0;31m'
GREEN='\033[0;32m'
NC='\033[0m'

# 清理测试文件
cleanup() {
    rm -rf "$UPLOAD_DIR" "$DOWNLOAD_DIR"
    rm -f "$SERVER_DIR/up_*.dat" "$SERVER_DIR/down_*.dat"
}

# 生成本地上传测试文件
gen_upload_files() {
    mkdir -p "$UPLOAD_DIR"
    echo "生成上传测试文件..."
    pushd "$UPLOAD_DIR" >/dev/null
    for i in $(seq 1 $UPLOAD_THREADS); do
        dd if=/dev/urandom of="up_$i.dat" bs=1M count=$FILE_SIZE_MB 2>/dev/null
    done
    popd >/dev/null
}

# 准备服务端文件，用于下载测试
prepare_download_files() {
    mkdir -p "$DOWNLOAD_DIR"
    echo "准备服务端下载文件..."
    pushd "$DOWNLOAD_DIR" >/dev/null
    for i in $(seq 1 $DOWNLOAD_THREADS); do
        dd if=/dev/urandom of="down_$i.dat" bs=1M count=$FILE_SIZE_MB 2>/dev/null
        $CLIENT upload "down_$i.dat" >/dev/null 2>&1
    done
    popd >/dev/null
}

# 并发断点上传测试
concurrent_upload_resume() {
    echo -e "\n========== 并发上传（断点续传）=========="
    pids=()
    pushd "$UPLOAD_DIR" >/dev/null

    # 模拟断点：截断服务端文件
    for i in $(seq 1 $UPLOAD_THREADS); do
        cp up_$i.dat "$SERVER_DIR/"
        truncate -s 3M "$SERVER_DIR/up_$i.dat" 2>/dev/null
    done

    # 并发上传
    for i in $(seq 1 $UPLOAD_THREADS); do
        $CLIENT upload up_$i.dat > upload_$i.log 2>&1 &
        pids+=($!)
    done

    wait "${pids[@]}"
    popd >/dev/null
    echo -e "${GREEN}并发断点上传完成${NC}"
}

# 并发断点下载测试
concurrent_download_resume() {
    echo -e "\n========== 并发下载（断点续传）=========="
    pids=()
    pushd "$DOWNLOAD_DIR" >/dev/null

    # 模拟断点：截断本地文件
    for i in $(seq 1 $DOWNLOAD_THREADS); do
        cp "$SERVER_DIR/down_$i.dat" ./
        truncate -s 3M "down_$i.dat" 2>/dev/null
    done

    # 并发下载
    for i in $(seq 1 $DOWNLOAD_THREADS); do
        $CLIENT download down_$i.dat > download_$i.log 2>&1 &
        pids+=($!)
    done

    wait "${pids[@]}"
    popd >/dev/null
    echo -e "${GREEN}并发断点下载完成${NC}"
}

# 混合并发：上传与下载同时执行
mixed_concurrent_resume() {
    echo -e "\n========== 混合上传下载（断点+并发）=========="
    pids=()

    # 准备断点环境
    for i in $(seq 1 $UPLOAD_THREADS); do
        truncate -s 3M "$SERVER_DIR/up_$i.dat" 2>/dev/null
    done
    for i in $(seq 1 $DOWNLOAD_THREADS); do
        truncate -s 3M "$DOWNLOAD_DIR/down_$i.dat" 2>/dev/null
    done

    # 启动所有上传任务
    for i in $(seq 1 $UPLOAD_THREADS); do
        (cd "$UPLOAD_DIR" && $CLIENT upload up_$i.dat > mix_upload_$i.log 2>&1) &
        pids+=($!)
    done

    # 启动所有下载任务
    for i in $(seq 1 $DOWNLOAD_THREADS); do
        (cd "$DOWNLOAD_DIR" && $CLIENT download down_$i.dat > mix_download_$i.log 2>&1) &
        pids+=($!)
    done

    # 等待全部任务完成
    wait "${pids[@]}"
    echo -e "${GREEN}混合并发执行完成${NC}"
}

# 文件完整性校验
check_all_files() {
    echo -e "\n========== 文件完整性校验 =========="
    fail=0

    # 校验上传文件
    for i in $(seq 1 $UPLOAD_THREADS); do
        src="$UPLOAD_DIR/up_$i.dat"
        dst="$SERVER_DIR/up_$i.dat"
        if [ ! -f "$dst" ] || ! cmp -s "$src" "$dst"; then
            echo -e "${RED}up_$i.dat 校验失败${NC}"
            fail=1
        fi
    done

    # 校验下载文件
    for i in $(seq 1 $DOWNLOAD_THREADS); do
        src="$SERVER_DIR/down_$i.dat"
        dst="$DOWNLOAD_DIR/down_$i.dat"
        if [ ! -f "$dst" ] || ! cmp -s "$src" "$dst"; then
            echo -e "${RED}down_$i.dat 校验失败${NC}"
            fail=1
        fi
    done

    if [ $fail -eq 0 ]; then
        echo -e "${GREEN} 全部文件校验通过${NC}"
    else
        echo -e "${RED} 部分文件损坏${NC}"
    fi
    return $fail
}

# 主流程
main() {
    echo -e "\n===== 并发与断点续传综合测试 =====${NC}"
    cleanup
    gen_upload_files #准备上传文件
    prepare_download_files #准备下在文件，并上传至服务器

    # 检查服务端状态
    if ! pgrep -x "server" >/dev/null; then
        echo -e "${RED}请先启动服务端${NC}"
        exit 1
    fi

    # 执行测试
    concurrent_upload_resume #先上传截断，再继续上传
    concurrent_download_resume #先下载截断，再继续下载
    mixed_concurrent_resume  #先上传截断、下载截断，再继续上传、继续下载
    check_all_files

    result=$?
    cleanup
    exit $result
}

main