#!/bin/bash
# 断点续传测试脚本（上传续传 + 下载续传）
# 用法：./resume_test.sh

PROJECT_ROOT="$(cd "$(dirname "$0")" && pwd)"
CLIENT="$PROJECT_ROOT/bin/client"
SERVER_DIR="$PROJECT_ROOT/server_files"
TEST_FILE="resume_test.dat"
LOCAL_COPY="resume_local_copy.dat"

RED='\033[0;31m'
GREEN='\033[0;32m'
NC='\033[0m'

cleanup() {
    rm -f "$TEST_FILE" "$SERVER_DIR/$TEST_FILE" "$LOCAL_COPY"
}

generate_file() {
    echo "生成 10MB 随机测试文件..."
    dd if=/dev/urandom of="$TEST_FILE" bs=1M count=10 2>/dev/null
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

    # 2. 服务端截断到 3MB
    echo "步骤2: 模拟服务端数据丢失，截断到 3MB"
    truncate -s 3M "$SERVER_DIR/$TEST_FILE" 2>/dev/null
    actual_size=$(stat -c%s "$SERVER_DIR/$TEST_FILE" 2>/dev/null || stat -f%z "$SERVER_DIR/$TEST_FILE" 2>/dev/null)
    echo "   服务端当前大小: $actual_size 字节"

    # 3. 再次上传，应续传
    echo "步骤3: 再次上传，期望续传"
    output=$($CLIENT upload "$TEST_FILE" 2>&1)
    if echo "$output" | grep -q "alreadly has"; then
        echo -e "${GREEN}✅ 上传续传成功 (客户端检测到已有偏移)${NC}"
    else
        echo -e "${RED}❌ 上传续传失败 (未检测到已有偏移)${NC}"
        return 1
    fi

    # 4. 校验文件完整性
    echo "步骤4: 校验最终文件"
    if cmp "$TEST_FILE" "$SERVER_DIR/$TEST_FILE"; then
        echo -e "${GREEN}✅ 文件完整一致${NC}"
        return 0
    else
        echo -e "${RED}❌ 文件不一致，续传可能出错${NC}"
        return 1
    fi
}

test_download_resume() {
    echo -e "\n========== 下载续传测试 =========="
    # 确保服务端有完整文件
    $CLIENT upload "$TEST_FILE" > /dev/null 2>&1

    # 1. 完整下载到本地副本
    echo "步骤1: 完整下载文件"
    $CLIENT download "$TEST_FILE" > /dev/null 2>&1
    cp "$TEST_FILE" "$LOCAL_COPY"

    # 2. 截断本地文件到 2MB
    echo "步骤2: 模拟本地下载中断，截断到 2MB"
    truncate -s 2M "$TEST_FILE" 2>/dev/null
    local_size=$(stat -c%s "$TEST_FILE" 2>/dev/null || stat -f%z "$TEST_FILE" 2>/dev/null)
    echo "   本地当前大小: $local_size 字节"

    # 3. 再次下载，应续传
    echo "步骤3: 再次下载，期望续传"
    output=$($CLIENT download "$TEST_FILE" 2>&1)
    if echo "$output" | grep -q "Local file exists"; then
        echo -e "${GREEN}✅ 下载续传成功 (客户端检测到本地已有部分)${NC}"
    else
        echo -e "${RED}❌ 下载续传失败 (未检测到本地已有文件)${NC}"
        return 1
    fi

    # 4. 校验文件完整性
    echo "步骤4: 校验最终文件"
    if cmp "$LOCAL_COPY" "$TEST_FILE"; then
        echo -e "${GREEN}✅ 文件完整一致${NC}"
        return 0
    else
        echo -e "${RED}❌ 文件不一致，续传可能出错${NC}"
        return 1
    fi
}

main() {
    echo "=== 断点续传自动化测试 ==="
    cleanup
    generate_file
    mkdir -p "$SERVER_DIR"

    if ! pgrep -x "server" > /dev/null; then
        echo -e "${RED}服务端未运行，请先启动: ./bin/server &${NC}"
        exit 1
    fi

    test_upload_resume
    UPLOAD_RESULT=$?

    test_download_resume
    DOWNLOAD_RESULT=$?

    echo -e "\n========== 测试汇总 =========="
    [ $UPLOAD_RESULT -eq 0 ] && echo -e "${GREEN}上传续传: 通过${NC}" || echo -e "${RED}上传续传: 失败${NC}"
    [ $DOWNLOAD_RESULT -eq 0 ] && echo -e "${GREEN}下载续传: 通过${NC}" || echo -e "${RED}下载续传: 失败${NC}"

    cleanup
    exit $((UPLOAD_RESULT + DOWNLOAD_RESULT))
}

main