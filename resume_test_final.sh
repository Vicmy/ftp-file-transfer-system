#!/bin/bash
CLIENT="./bin/client"
SERVER="./bin/server"
TEST_FILE="test_resume.dat"
SERVER_DIR="./server_files"
LOCAL_COPY="local_copy.dat"
LOG_FILE="server.log"
SERVER_PID_FILE="server.pid"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
NC='\033[0m'

cleanup() {
    rm -f "$TEST_FILE" "$SERVER_DIR/$TEST_FILE" "$LOCAL_COPY"
}

generate_file() {
    echo "生成 10MB 随机测试文件..."
    dd if=/dev/urandom of="$TEST_FILE" bs=1M count=10 2>/dev/null
}

start_server() {
    if [ -f "$SERVER_PID_FILE" ]; then
        kill -0 $(cat "$SERVER_PID_FILE") 2>/dev/null && return 0
    fi
    $SERVER > /dev/null 2>&1 &
    echo $! > "$SERVER_PID_FILE"
    sleep 1
}

stop_server() {
    if [ -f "$SERVER_PID_FILE" ]; then
        kill $(cat "$SERVER_PID_FILE") 2>/dev/null
        rm -f "$SERVER_PID_FILE"
        sleep 1
    fi
}

test_upload_resume() {
    echo -e "\n========== 上传续传测试 =========="
    # 1. 完整上传
    echo "步骤1: 完整上传文件"
    $CLIENT upload "$TEST_FILE" > /dev/null 2>&1
    if [ ! -f "$SERVER_DIR/$TEST_FILE" ]; then
        echo -e "${RED}❌ 上传失败，服务端文件不存在${NC}"
        return 1
    fi

    # 2. 停止服务端，截断文件，再重启（释放文件句柄）
    echo "步骤2: 停止服务端，截断文件到 3MB，再重启"
    stop_server
    truncate -s 3M "$SERVER_DIR/$TEST_FILE" 2>/dev/null || \
        python3 -c "import os; os.truncate('$SERVER_DIR/$TEST_FILE', 3145728)" 2>/dev/null
    actual_size=$(stat -c%s "$SERVER_DIR/$TEST_FILE" 2>/dev/null || stat -f%z "$SERVER_DIR/$TEST_FILE" 2>/dev/null)
    echo "   服务端当前大小: $actual_size 字节"
    if [ "$actual_size" -ne 3145728 ]; then
        echo -e "${RED}❌ 截断失败，实际大小 $actual_size${NC}"
        start_server
        return 1
    fi
    start_server

    echo "步骤3: 再次上传，期望续传"
    output=$($CLIENT upload "$TEST_FILE" 2>&1)
    if echo "$output" | grep -q "alreadly has"; then
        echo -e "${GREEN}✅ 上传续传成功 (客户端检测到已有偏移)${NC}"
    else
        echo -e "${RED}❌ 上传续传失败 (未检测到已有偏移)${NC}"
        echo "$output"
        return 1
    fi

    echo "步骤4: 校验最终文件"
    if cmp "$TEST_FILE" "$SERVER_DIR/$TEST_FILE"; then
        echo -e "${GREEN}✅ 文件完整一致${NC}"
        return 0
    else
        echo -e "${RED}❌ 文件不一致，续传可能出错${NC}"
        cmp -l "$TEST_FILE" "$SERVER_DIR/$TEST_FILE" | head -5
        return 1
    fi
}

test_download_resume() {
    echo -e "\n========== 下载续传测试 =========="
    # 确保服务端有完整文件
    $CLIENT upload "$TEST_FILE" > /dev/null 2>&1

    echo "步骤1: 完整下载文件"
    $CLIENT download "$TEST_FILE" > /dev/null 2>&1
    cp "$TEST_FILE" "$LOCAL_COPY"

    echo "步骤2: 截断本地文件到 2MB"
    truncate -s 2M "$TEST_FILE" 2>/dev/null || \
        python3 -c "import os; os.truncate('$TEST_FILE', 2097152)" 2>/dev/null
    local_size=$(stat -c%s "$TEST_FILE" 2>/dev/null || stat -f%z "$TEST_FILE" 2>/dev/null)
    echo "   本地当前大小: $local_size 字节"
    if [ "$local_size" -ne 2097152 ]; then
        echo -e "${RED}❌ 截断失败，实际大小 $local_size${NC}"
        return 1
    fi

    echo "步骤3: 再次下载，期望续传"
    output=$($CLIENT download "$TEST_FILE" 2>&1)
    if echo "$output" | grep -q "Local file exists"; then
        echo -e "${GREEN}✅ 下载续传成功 (客户端检测到本地已有部分)${NC}"
    else
        echo -e "${RED}❌ 下载续传失败 (未检测到本地已有文件)${NC}"
        echo "$output"
        return 1
    fi

    echo "步骤4: 校验最终文件"
    if cmp "$LOCAL_COPY" "$TEST_FILE"; then
        echo -e "${GREEN}✅ 文件完整一致${NC}"
        return 0
    else
        echo -e "${RED}❌ 文件不一致，续传可能出错${NC}"
        cmp -l "$LOCAL_COPY" "$TEST_FILE" | head -10
        return 1
    fi
}

main() {
    echo "=== 断点续传自动化测试 ==="
    cleanup
    generate_file
    mkdir -p "$SERVER_DIR"
    start_server

    test_upload_resume
    UPLOAD_RESULT=$?

    test_download_resume
    DOWNLOAD_RESULT=$?

    echo -e "\n========== 测试汇总 =========="
    [ $UPLOAD_RESULT -eq 0 ] && echo -e "${GREEN}上传续传: 通过${NC}" || echo -e "${RED}上传续传: 失败${NC}"
    [ $DOWNLOAD_RESULT -eq 0 ] && echo -e "${GREEN}下载续传: 通过${NC}" || echo -e "${RED}下载续传: 失败${NC}"

    stop_server
    cleanup
    exit $((UPLOAD_RESULT + DOWNLOAD_RESULT))
}

main