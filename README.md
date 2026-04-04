# 文件传输系统（支持断点续传与并发）

一个基于 TCP 的 C 语言文件传输系统，支持文件上传、下载、断点续传、并发处理、文件锁保护及日志记录。

## 功能特性

- 文件上传与下载
- 断点续传（上传和下载均支持）
- 多客户端并发连接（使用线程池模型）
- 文件锁保护（防止并发上传同一文件导致数据损坏）
- 路径遍历防护（安全检查，拒绝 `..`、`/`、`\` 等危险字符）
- 服务端优雅退出（SIGINT 信号处理）
- 线程安全日志系统（输出到 `server.log`）

## 编译与运行

### 环境要求

- Linux / macOS（支持 POSIX 线程、TCP socket）
- GCC 编译器
- make

### 编译

make clean
make

### 编译后生成：

1. bin/server – 服务端

2. bin/client – 客户端

# 运行服务端
./bin/server &
服务端默认监听 8888 端口，文件存储目录为 ./server_files/。

# 运行客户端

1. 上传文件
./bin/client upload <filename>

2. 下载文件
./bin/client download <filename>
示例：
#生成测试文件
dd if=/dev/urandom of=test.dtxt bs=1M count=100
./bin/client upload test.txt
./bin/client download test.txt
# 断点续传测试
## 上传续传
上传一个大文件，中途用 Ctrl+C 中断（或使用 timeout 命令）：

timeout 2s ./bin/client upload large.bin
再次执行上传命令：

./bin/client upload large.bin
客户端会显示 Server already has X bytes 并从断点处继续上传。

## 下载续传
确保服务端有文件，先完整下载一次文件：
./bin/client download large.bin
截断本地文件（模拟下载中断）：
truncate -s 2M large.bin
再次下载（会自动续传）：
./bin/client download large.bin
客户端会显示 Local file exists, size: X 并从断点处继续下载。
校验文件完整性（与原始文件比较）
cmp large.bin server_files/large.bin

md5sum large.bin server_files/large.bin

### 断点测试时很快，可使用脚本完成。
resume_test.sh
chmod +x resume_test.sh
./resume_test.sh

# 并发测试
项目提供了多个自动化测试脚本（位于项目根目录），用于验证并发场景下的正确性。

## 测试脚本
concurrent_upload_fixed.sh – 10 个客户端同时上传 5MB 文件

concurrent_download_test.sh – 10 个客户端同时下载 5MB 文件

concurrent_mixed_test.sh – 5 个上传 + 5 个下载同时进行

concurrent_same_file_test.sh – 5 个客户端同时上传同一个 10MB 文件（验证文件锁）

run_all_tests.sh – 一键运行上述所有测试


## 运行测试

### 确保服务端已启动
./bin/server &

### 运行全部测试
chmod +x run_all_tests.sh
./run_all_tests.sh
### 预期输出（所有测试通过）：

text
=========================================
  文件传输系统并发测试套件
=========================================

[1/4] 运行并发上传测试...
✅ 并发上传测试通过

[2/4] 运行并发下载测试...
✅ 并发下载测试通过

[3/4] 运行混合并发测试...
✅ 混合并发测试通过

[4/4] 运行相同文件锁测试...
✅ 文件锁测试通过（最终文件完整）

=========================================
  🎉 所有并发测试全部通过！
=========================================
# 协议说明
通信协议为简单的二进制命令+数据格式，定义在 common/protocol.h：

命令码	含义
0x01	上传文件
0x02	下载文件
0x03	查询文件大小（用于断点续传）
详细交互流程见源代码注释。

# 项目结构
text
.
├── bin/                   # 编译输出目录
├── client/                # 客户端源码
│   └── client.c
├── server/                # 服务端源码
│   └── server.c
├── common/                # 公共模块
│   ├── protocol.h         # 协议定义
│   ├── utils.h / utils.c  # 网络收发与安全检查
│   └── log.h / log.c      # 日志系统
├── server_files/          # 服务端文件存储目录（自动创建）
├── Makefile
├── run_all_tests.sh       # 一键测试脚本
└── README.md
# 注意事项
客户端上传或下载完成后会延迟 1 秒关闭连接，以确保服务端完全接收数据（避免并发时过早关闭导致数据不完整）。如需更优雅的实现，可改用应用层 ACK 确认。

服务端默认最大并发连接数 listen 的 backlog（已设为 128）和系统文件描述符限制。运行前调整 ulimit -n 4096。

日志文件 server.log 会自动创建，记录所有连接、上传、下载及错误信息。

