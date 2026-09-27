# SonarQube 本地化部署与 Qt 项目 C++ 代码质量检测全量指南（优化版）

> 版本适配说明：本指南基于 **SonarQube Community Build 26.1.0.118079-community** 与 **sonar-cxx 插件 2.3.0**。二者经官方测试并发布为兼容组合，插件要求 **Java 21**（Java 17 已废弃）。`sonarqube:latest` 版本过新，cxx 插件尚未验证兼容，不建议使用。
>
> **文档命令约定**：本文档中凡标注 `cmd` 的代码块为 **Windows CMD** 命令，标注 `powershell` 的为 **PowerShell** 命令，标注 `bash` 的为 **Linux / WSL2 bash** 命令。请勿将 CMD 的 `^` 续行符与 bash 的 `\` 混用。


## 一、环境准备

### 1.1 安装 Docker Desktop

下载并安装 Docker Desktop AMD64 版本。安装完成后，在 **Settings → Resources** 中将内存调整为 **4GB 以上**，否则 SonarQube 容器可能因内存不足启动失败。

### 1.2 安装 WSL2 并开启 Windows 功能

在 Windows 功能中开启：

- **虚拟机平台**
- **适用于 Linux 的 Windows 子系统**

安装完成后重启系统。如使用 WSL2 运行 Linux 原生命令，需确保 WSL2 发行版已安装（如 Ubuntu 22.04+）。

### 1.3 网络代理注意事项

> **重要**：使用全局代理下载 Docker 镜像时，**不要使用 TUN 模式**，否则 Docker Desktop 可能无法正常解析网络请求。建议在 Docker Desktop 的代理设置中单独配置 HTTP/HTTPS 代理，而非依赖系统级 TUN 模式。

### 1.4 Linux 环境前置配置（WSL2 / 真实 Linux）

SonarQube 依赖 Elasticsearch，要求 `vm.max_map_count` ≥ **524288**。在 WSL2 或 Linux 中执行：

```bash
sudo sysctl -w vm.max_map_count=524288
```

如需持久化，写入 `/etc/sysctl.d/99-sonarqube.conf`：

```bash
echo "vm.max_map_count=524288" | sudo tee /etc/sysctl.d/99-sonarqube.conf
sudo sysctl -p /etc/sysctl.d/99-sonarqube.conf
```


## 二、拉取 SonarQube 镜像并启动容器

### 2.1 清理旧容器（如有）

```cmd
docker stop sonarqube
docker rm sonarqube
```

### 2.2 拉取指定版本镜像

```cmd
docker pull sonarqube:26.1.0.118079-community
```

镜像查看地址：https://hub.docker.com/r/library/sonarqube/tags

> **注意**：`sonarqube:latest` 版本过新，`sonar-cxx` 插件不支持，不建议使用。

### 2.3 启动 SonarQube 容器（带数据卷）

**Windows CMD：**

```cmd
docker run -d --name sonarqube ^
  -p 9000:9000 ^
  -v sonarqube_data:/opt/sonarqube/data ^
  -v sonarqube_logs:/opt/sonarqube/logs ^
  -v sonarqube_extensions:/opt/sonarqube/extensions ^
  -e SONAR_ES_BOOTSTRAP_CHECKS_DISABLE=true ^
  sonarqube:26.1.0.118079-community
```

**PowerShell / bash：**

```bash
docker run -d --name sonarqube \
  -p 9000:9000 \
  -v sonarqube_data:/opt/sonarqube/data \
  -v sonarqube_logs:/opt/sonarqube/logs \
  -v sonarqube_extensions:/opt/sonarqube/extensions \
  -e SONAR_ES_BOOTSTRAP_CHECKS_DISABLE=true \
  sonarqube:26.1.0.118079-community
```

> **数据卷说明**：使用 `-v` 挂载数据卷后，即使删除容器，数据也不会丢失。以后重启或升级容器时，只要挂载相同的卷，配置和项目数据都能保留。

### 2.4 安装 sonar-cxx 插件

从 [SonarOpenCommunity/sonar-cxx Releases](https://github.com/SonarOpenCommunity/sonar-cxx/releases/tag/cxx-2.3.0) 下载 `sonar-cxx-plugin-2.3.0.jar`，复制到容器中并重启：

```cmd
docker cp sonar-cxx-plugin-2.3.0.jar sonarqube:/opt/sonarqube/extensions/plugins/
docker restart sonarqube
```

> **权限提示**：SonarQube 以 UID 1000 运行，若插件文件权限不正确，可能导致插件加载失败。如遇此情况，在容器内修正权限：
>
> ```bash
> docker exec -u 0 sonarqube chown 1000:1000 /opt/sonarqube/extensions/plugins/sonar-cxx-plugin-2.3.0.jar
> ```
> 然后再次 `docker restart sonarqube`。
>
> 之后重启进入质量配置，创建选择**扩展现存质量配置**按需选择配置

插件安装后，SonarQube 的“质量配置”中会出现 C++ 相关规则集。`sonar-cxx` 插件的核心设计思路并非自己分析代码，而是作为**适配器**，集成现有的 C++ 生态工具（如 Cppcheck、Clang-Tidy）的分析结果。

### 2.5 访问 SonarQube 控制台

浏览器访问：

```
http://localhost:9000
```

默认登录凭据：

- **用户名**：`admin`
- **密码**：`admin`

首次登录会强制要求修改密码，设置新密码后进入主界面。


## 三、Visual Studio 安装 Sonar 插件并连接（可选）

在 Visual Studio 中安装 **SonarQube for Visual Studio** 插件，安装完成后通过 **Extensions → SonarQube for Visual Studio → Connected Mode → Bind to SonarQube (Server, Cloud)…** 连接本地 SonarQube 服务器。

这样可以在 IDE 中直接获得代码质量提示，并与 SonarQube 服务器保持规则同步。


## 四、安装 SonarScanner CLI

### 4.1 下载

官方文档：[SonarScanner CLI](https://docs.sonarsource.com/sonarqube-server/2026.1/analyzing-source-code/scanners/sonarscanner)

从官方下载 **Windows x64** 版本（已内置 JRE）：

- [SonarScanner CLI 8.1.0.6389 — Windows x64](https://binaries.sonarsource.com/Distribution/sonar-scanner-cli/sonar-scanner-cli-8.1.0.6389-windows-x64.zip)
- [SonarScanner CLI 8.0.1.6346 — Windows x64](https://binaries.sonarsource.com/Distribution/sonar-scanner-cli/sonar-scanner-cli-8.0.1.6346-windows-x64.zip)

如使用 Linux / WSL2，下载对应平台版本：

- [SonarScanner CLI 8.1.0.6389 — Linux x64](https://binaries.sonarsource.com/Distribution/sonar-scanner-cli/sonar-scanner-cli-8.1.0.6389-linux-x64.zip)

### 4.2 安装与配置环境变量（Windows）

1. 将压缩包解压到任意目录（例如 `C:\sonar-scanner`）。
2. 将 `C:\sonar-scanner\bin` 添加到系统 `PATH` 环境变量：
   - 按 `Win + S`，搜索“环境变量”
   - 点击“编辑系统环境变量”
   - 在“系统变量”中找到 `Path`，点击“编辑”
   - 添加 `C:\sonar-scanner\bin`
3. 打开**新的**命令行窗口，验证安装：

```cmd
sonar-scanner -h
```

Windows 下等效命令为 `sonar-scanner.bat -h`，如果 `bin` 已在 `PATH` 中，直接使用 `sonar-scanner` 即可。

### 4.3 Java 运行时要求

SonarScanner CLI 8.x 需要 **Java 21 或更高版本**，Java 17 已废弃。如果本机未安装 Java，可使用上方包含嵌入式 JRE 的 Windows x64 完整版（已内置 JRE）。

> **sonar-cxx 2.3.0 同样要求 Java 21**（服务端和扫描器端均需满足）。如果本机 Java 版本低于 21，扫描会失败。


## 五、生成 SonarQube 项目与 Token

### 5.1 创建项目

1. 登录 SonarQube 网页（`http://localhost:9000`）
2. 点击 **“项目” → “创建项目” → “手动”**
3. 输入项目名（如 `My Qt Project`）和项目 Key（如 `my-qt-project`）
4. 选择 **“使用全局设置”** → 点击 **“创建项目”**

### 5.2 生成 Token

1. 进入 **“我的账户” → “安全”** 标签页（`http://localhost:9000/account/security`）
2. 输入 Token 名称（如 `local-qt-scan`），点击 **“生成”**
3. **立即复制并保存 Token**（页面关闭后无法再次查看）[squ_726e2b12d3616c91f84f7c61fcfa4061b7deb2a2]

> **Token 安全建议**：推荐通过环境变量 `SONAR_TOKEN` 传递 Token，而非明文写入 `sonar-project.properties`。Windows CMD 设置方式：
> ```cmd
> set SONAR_TOKEN=你的Token
> ```
> PowerShell：
> ```powershell
> $env:SONAR_TOKEN="你的Token"
> ```
> 然后在 `sonar-project.properties` 中省略 `sonar.token` 字段，或在命令行使用 `-Dsonar.token=%SONAR_TOKEN%`（CMD）/ `-Dsonar.token=$env:SONAR_TOKEN`（PowerShell）。


## 六、配置 Qt 项目

### 6.1 创建 sonar-project.properties

在 Qt 项目**根目录**下创建 `sonar-project.properties` 文件：

```properties
# ===== 项目标识（必须唯一）=====
sonar.projectKey=my-qt-project
sonar.projectName=My Qt Project
sonar.projectVersion=1.0

# ===== 服务器连接 =====
sonar.host.url=http://localhost:9000

# ===== 源码路径 =====
sonar.sources=.
sonar.sourceEncoding=UTF-8

# ===== C++ 文件后缀（sonar-cxx 插件必需）=====
sonar.cxx.file.suffixes=.cpp,.cxx,.cc,.c,.h,.hpp,.hxx,.inl

# ===== 排除 Qt 生成文件和第三方库 =====
sonar.exclusions=**/build/**,**/moc_*.cpp,**/ui_*.h,**/qrc_*.cpp,**/*.qm,**/*.qml,**/*.svg,**/*.dll,**/*.obj,**/3rdparty/**

# ===== 外部工具报告路径（Windows + Linux 双报告）=====
sonar.cxx.cppcheck.reportPaths=cppcheck-report-win.xml,cppcheck-report-linux.xml
# sonar.cxx.clangtidy.reportPaths=clang-tidy-report.txt
```

> **关键说明**：
>
> - `sonar.cxx.file.suffixes` 是**必须显式设置**的参数，否则 cxx 插件的 C++ 传感器不会启用。
> - 社区版 + sonar-cxx 场景下，**无需设置 `sonar.language`**，该参数在较新版本中已不是 C++ 分析的必要条件；文件后缀和插件传感器会自动识别 C++ 文件。
> - Qt 项目会生成大量 `moc_*.cpp`、`ui_*.h`、`qrc_*.cpp` 等中间文件，务必通过 `sonar.exclusions` 排除。
> - `sonar.cxx.cppcheck.reportPaths` 支持**逗号分隔的多路径**，路径相对于 `sonar-project.properties` 所在目录，**逗号前后不要有空格**。自插件 2.0 版本起，单数形式 `reportPath` 已替换为支持多路径的 `reportPaths`。

### 6.2 Qt 项目特殊配置（可选）

如果 Qt 项目使用了 `new` 等惯用写法，可以添加忽略规则。**规则键需以实际扫描结果中显示的为准**，社区版 + sonar-cxx 场景下通常来自 `cppcheck:` 或 `cxx:` 仓库：

```properties
# 忽略指定规则告警（示例，请根据实际规则键调整）
sonar.issue.ignore.multicriteria=e1
sonar.issue.ignore.multicriteria.e1.ruleKey=cppcheck:uninitvar
sonar.issue.ignore.multicriteria.e1.resourceKey=**/*.cpp
```

> 不要直接复制 `cpp:S5025` 等 SonarSource 官方 C++ 分析器的规则键，社区版不会产生这些规则的问题。


## 七、运行 Cppcheck 生成双平台报告

在运行 SonarScanner 之前，**必须先使用 Cppcheck 分析代码并生成 XML 报告**。因为社区版没有内置 C++ 分析器，`sonar-cxx` 插件依赖外部工具的结果。

### 7.1 安装 Cppcheck

- **方式 A**：从 [Cppcheck 官网](https://cppcheck.sourceforge.io/) 下载 Windows 安装包并安装。
- **方式 B**：使用 Chocolatey 或 Scoop 包管理器安装。
- **Linux / WSL2**：`sudo apt install cppcheck`（Ubuntu）或通过 snap 安装。

建议将 Cppcheck 的安装路径添加到系统 `PATH`，以便直接在命令行调用。

### 7.2 生成 Windows 平台报告

**Windows CMD：**

cppcheck --platform=win64 --xml --xml-version=2 --enable=warning,style,performance,portability --force -D_WIN32 -DWIN32 -D_WIN64 -D_MSC_VER=1930 --output-file=cppcheck-report-win.xml .

```cmd
cppcheck ^
  --platform=win64 ^
  --xml --xml-version=2 ^
  --enable=warning,style,performance,portability ^
  --force ^
  -D_WIN32 -DWIN32 -D_WIN64 -D_MSC_VER=1930 ^
  --output-file=cppcheck-report-win.xml ^
  .
```

**PowerShell / bash：**

```bash
cppcheck \
  --platform=win64 \
  --xml --xml-version=2 \
  --enable=warning,style,performance,portability \
  --force \
  -D_WIN32 -DWIN32 -D_WIN64 -D_MSC_VER=1930 \
  --output-file=cppcheck-report-win.xml \
  .
```

> **关键修正**：`--xml` 和 `--xml-version=2` 需要**同时使用**，仅写 `--xml-version=2` 在部分版本中不会输出 XML 格式。`--enable=all` 会产生大量 `information` 和 `missingInclude` 噪声，建议按需使用 `warning,style,performance,portability` 组合。

### 7.3 生成 Linux 平台报告（在 Windows 上模拟）

**Windows CMD：**

```cmd
cppcheck ^
  --platform=unix64 ^
  --xml --xml-version=2 ^
  --enable=warning,style,performance,portability ^
  --force ^
  -U_WIN32 -UWIN32 -UWIN64 -U_MSC_VER ^
  -D__linux__ -D__GNUC__=11 ^
  --output-file=cppcheck-report-linux.xml ^
  .
```

**PowerShell / bash：**

```bash
cppcheck \
  --platform=unix64 \
  --xml --xml-version=2 \
  --enable=warning,style,performance,portability \
  --force \
  -U_WIN32 -UWIN32 -UWIN64 -U_MSC_VER \
  -D__linux__ -D__GNUC__=11 \
  --output-file=cppcheck-report-linux.xml \
  .
```

### 7.4 关键参数解释

| 参数                       | 作用                                                         |
| -------------------------- | ------------------------------------------------------------ |
| `--platform=win64`         | 按 64 位 Windows 数据模型分析（指针 8 字节，`long` 4 字节）  |
| `--platform=unix64`        | 按 64 位 Linux 数据模型分析（指针 8 字节，`long` 8 字节）    |
| `--xml --xml-version=2`    | 输出 Cppcheck XML 2.0 格式报告，供 sonar-cxx 解析            |
| `-D_WIN32 -DWIN32 -DWIN64` | 定义 Windows 平台宏，激活 `#ifdef _WIN32` 分支               |
| `-U_WIN32 -UWIN32 -UWIN64` | 取消 Windows 宏定义，避免 Linux 分析时误入 Windows 分支      |
| `-D__linux__`              | 定义 Linux 宏，激活 `#ifdef __linux__` 分支                  |
| `-D_MSC_VER=1930`          | 模拟 MSVC 2019 编译器版本（按实际编译器调整）                |
| `-D__GNUC__=11`            | 模拟 GCC 11（按 Qt 的 Linux 实际编译器调整）                 |
| `--force`                  | 强制分析所有配置。若宏组合过多导致 `too many configs`，改用 `--max-configs=N` 限制 |
| `-j N`                     | 使用 N 个线程并行分析，加速大项目扫描                        |

> **`--force` 与 `--max-configs` 的区别**：`--force` 会分析所有预处理配置，配置数量不受限制，可能非常耗时；`--max-configs=N` 限制最多分析 N 种配置。如果遇到 `too many configs` 报错，应使用 `--max-configs=12` 或类似值，**而不是** `--force`。如果希望全面覆盖所有平台分支，则使用 `--force`。

> **注意**：Cppcheck 报告路径需相对于 `sonar-project.properties` 所在目录。如果项目路径包含空格（如 `C:\My Project\`），建议将项目放在无空格目录，或在命令中使用引号包裹路径。


## 八、执行 SonarScanner 扫描

### 8.1 基本运行命令

在 Qt 项目根目录下执行：

```cmd
sonar-scanner -Dsonar.host.url=http://localhost:9000 -Dsonar.token=你的Token
```

也可以将 `sonar.host.url` 写入 `sonar-project.properties`，Token 通过环境变量传递：

```properties
sonar.host.url=http://localhost:9000
```

然后直接运行：

```cmd
sonar-scanner
```

### 8.2 完整命令序列（跨平台版）

**Windows CMD：**

```cmd
:: 1. 清理旧报告（可选）
del cppcheck-report-win.xml cppcheck-report-linux.xml 2>nul

:: 2. 生成 Windows 平台报告
cppcheck --platform=win64 --xml --xml-version=2 ^
  --enable=warning,style,performance,portability --force ^
  -D_WIN32 -DWIN32 -D_WIN64 -D_MSC_VER=1930 ^
  --output-file=cppcheck-report-win.xml .

:: 3. 生成 Linux 平台报告
cppcheck --platform=unix64 --xml --xml-version=2 ^
  --enable=warning,style,performance,portability --force ^
  -U_WIN32 -UWIN32 -UWIN64 -U_MSC_VER ^
  -D__linux__ -D__GNUC__=11 ^
  --output-file=cppcheck-report-linux.xml .

:: 4. 执行 SonarScanner 扫描
sonar-scanner -Dsonar.host.url=http://localhost:9000 -Dsonar.token=%SONAR_TOKEN%
```

**bash / WSL2：**

```bash
# 1. 清理旧报告
rm -f cppcheck-report-win.xml cppcheck-report-linux.xml

# 2. 生成 Windows 平台报告
cppcheck --platform=win64 --xml --xml-version=2 \
  --enable=warning,style,performance,portability --force \
  -D_WIN32 -DWIN32 -D_WIN64 -D_MSC_VER=1930 \
  --output-file=cppcheck-report-win.xml .

# 3. 生成 Linux 平台报告
cppcheck --platform=unix64 --xml --xml-version=2 \
  --enable=warning,style,performance,portability --force \
  -U_WIN32 -UWIN32 -UWIN64 -U_MSC_VER \
  -D__linux__ -D__GNUC__=11 \
  --output-file=cppcheck-report-linux.xml .

# 4. 执行 SonarScanner 扫描
sonar-scanner -Dsonar.host.url=http://localhost:9000 -Dsonar.token=$SONAR_TOKEN
```

### 8.3 常用命令行参数

| 参数                                     | 说明                                                 |
| ---------------------------------------- | ---------------------------------------------------- |
| `-Dsonar.host.url=http://localhost:9000` | 指定 SonarQube 服务器地址                            |
| `-Dsonar.token=<token>`                  | 指定认证 Token（SonarQube 10.x+ 使用 `sonar.token`） |
| `-Dsonar.projectKey=<key>`               | 指定项目 Key（可省略，已在配置文件中定义）           |
| `-Dsonar.verbose=true`                   | 输出详细日志，便于排查问题                           |
| `-X`                                     | 输出调试日志                                         |


## 九、查看分析报告

扫描完成后，终端会输出分析报告链接。浏览器打开 SonarQube 网页（`http://localhost:9000`），进入对应项目即可查看：

| 指标         | 说明                                                         |
| ------------ | ------------------------------------------------------------ |
| **代码行数** | C++ 文件的总行数（排除 Qt 生成文件后）                       |
| **问题列表** | Cppcheck 发现的 Bug、代码异味、安全热点等（Windows + Linux 合并） |
| **质量门禁** | 项目是否通过质量门禁                                         |
| **覆盖率**   | 代码覆盖率（需额外配置覆盖率工具生成报告）                   |

### 9.1 问题详情

点击“问题”标签页，可以看到每个问题的：

- **文件路径和行号**
- **问题描述**
- **严重级别**（Blocker / Critical / Major / Minor / Info）
- **规则来源**（如 `cppcheck:uninitvar`）

### 9.2 质量配置管理

在 **“配置” → “质量配置”** 中，可以看到 C++ 相关的规则集。插件安装后，C++ 规则集会自动出现。可以根据团队需求启用或禁用规则。


## 十、完整流程总结（跨平台版）

| 步骤 | 操作                            | 关键命令 / 配置                                              |
| ---- | ------------------------------- | ------------------------------------------------------------ |
| 1    | 拉取镜像                        | `docker pull sonarqube:26.1.0.118079-community`              |
| 2    | 启动容器                        | `docker run -d --name sonarqube -p 9000:9000 -v ...`         |
| 3    | 安装 cxx 插件                   | `docker cp sonar-cxx-plugin-2.3.0.jar ...` + `docker restart` |
| 4    | 安装 SonarScanner CLI           | 解压 → 添加 `bin` 到 `PATH` → `sonar-scanner -h` 验证        |
| 5    | 创建项目 + 生成 Token           | SonarQube 网页 → 创建项目 → 生成 Token                       |
| 6    | 创建 `sonar-project.properties` | 配置项目 Key、源码路径、C++ 文件后缀、两份 Cppcheck 报告路径 |
| 7    | 运行 Cppcheck（Windows）        | `cppcheck --platform=win64 --xml --xml-version=2 ... --output-file=cppcheck-report-win.xml .` |
| 8    | 运行 Cppcheck（Linux）          | `cppcheck --platform=unix64 --xml --xml-version=2 ... --output-file=cppcheck-report-linux.xml .` |
| 9    | 运行 SonarScanner               | `sonar-scanner -Dsonar.host.url=http://localhost:9000 -Dsonar.token=%SONAR_TOKEN%` |
| 10   | 查看报告                        | 浏览器打开 `http://localhost:9000` 查看项目分析结果          |


## 十一、常见问题排查

| 问题                           | 可能原因                           | 解决方案                                                     |
| ------------------------------ | ---------------------------------- | ------------------------------------------------------------ |
| 扫描后无 C++ 结果              | 未设置 `sonar.cxx.file.suffixes`   | 在配置文件中显式添加该参数                                   |
| Cppcheck 报告未被读取          | 报告路径错误                       | 确保路径相对于 `sonar-project.properties` 文件，且用逗号分隔多路径，逗号后不要有空格 |
| Linux 分支代码未被扫描         | 宏定义未正确设置                   | 检查 `-U_WIN32`、`-D__linux__` 是否生效，可加 `--debug` 查看 Cppcheck 实际使用的宏 |
| Cppcheck 报 `too many configs` | 宏组合太多                         | 改用 `--max-configs=N`（如 `--max-configs=12`），不要用 `--force` 强制分析所有配置 |
| 同一处代码出现重复问题         | 两份报告都覆盖了公共代码           | 在 SonarQube 问题列表确认是否重复；如需去重，可在生成报告时用 `--suppress` 排除平台无关文件 |
| 插件未生效                     | 未重启容器，或插件文件权限不足     | 执行 `docker restart sonarqube`；如仍无效，检查插件 JAR 权限（UID 1000） |
| 登录时提示 Token 无效          | Token 已过期或输入错误             | 重新生成 Token                                               |
| 扫描时报 Java 版本不兼容       | 本机 Java 版本低于 21              | 使用内置 JRE 的 SonarScanner Windows x64 完整版，或将 `JAVA_HOME` 指向 Java 21 |
| SonarQube 容器启动失败         | 内存不足或 `vm.max_map_count` 过低 | Docker Desktop 内存调至 4GB 以上；Linux / WSL2 设置 `vm.max_map_count=524288` |
| 报告路径包含空格               | Windows 路径如 `C:\My Project\`    | 尽量把项目路径放在无空格目录，或用引号包裹路径               |
| 忽略规则不生效                 | 使用了错误的规则键                 | 在 SonarQube 问题列表中查看实际规则键（如 `cppcheck:uninitvar`），不要用 `cpp:S5025` 等官方分析器键 |


## 十二、数据持久化说明

当前容器使用了数据卷挂载：

```bash
-v sonarqube_data:/opt/sonarqube/data
-v sonarqube_logs:/opt/sonarqube/logs
-v sonarqube_extensions:/opt/sonarqube/extensions
```

这意味着：

- ✅ **重启容器**（`docker restart sonarqube`）：数据不丢失
- ✅ **重启电脑**：数据不丢失
- ✅ **删除并重新创建容器**（使用相同的 `-v` 参数）：数据不丢失
- ❌ **删除数据卷**（`docker volume rm sonarqube_data`）：数据丢失

**建议定期备份数据卷**。查看数据卷位置：

```cmd
docker volume inspect sonarqube_data
```


## 十三、进阶方案：在真实 Linux 环境中运行分析

Cppcheck 的 `--platform=unix64` 只是**模拟** Linux 数据模型，并非真正的 Linux 编译环境。某些深度平台相关的缺陷（如头文件差异、系统 API 差异）仍可能漏检。

**更准确的做法**：在 Linux 环境下（真机或 WSL2）也跑一次 Cppcheck + SonarScanner：

```bash
# 在 Linux / WSL2 中执行
cppcheck --platform=unix64 --xml --xml-version=2 \
  --enable=warning,style,performance,portability --force \
  --output-file=cppcheck-report-linux-native.xml .

sonar-scanner -Dsonar.host.url=http://localhost:9000 -Dsonar.token=$SONAR_TOKEN \
  -Dsonar.cxx.cppcheck.reportPaths=cppcheck-report-linux-native.xml
```

然后将这份**原生 Linux 报告**和 Windows 报告合并（在 `sonar-project.properties` 中配置）：

```properties
sonar.cxx.cppcheck.reportPaths=cppcheck-report-win.xml,cppcheck-report-linux-native.xml
```

> **推荐场景**：
>
> - 日常开发在 Windows 上 → 用第七节的模拟双平台方案快速验证
> - 正式 CI/CD → 用本节方案（Windows + Linux 双机跑），结果最准确


## 十四、进阶方案：Clang-Tidy 集成（可选）

sonar-cxx 也支持集成 Clang-Tidy 的报告。如果项目已有 `compile_commands.json`，可以生成 Clang-Tidy 报告并与 Cppcheck 报告一起导入：

```bash
# 生成 compile_commands.json（CMake 项目）
cmake -DCMAKE_EXPORT_COMPILE_COMMANDS=ON ..

# 运行 Clang-Tidy
clang-tidy -p build --export-fixes=clang-tidy-report.txt $(find . -name '*.cpp')

# 在 sonar-project.properties 中配置
sonar.cxx.clangtidy.reportPaths=clang-tidy-report.txt
```

> 注意：Clang-Tidy 报告需使用 **YAML 格式**（`--export-fixes` 输出的是 YAML），sonar-cxx 才能正确解析。


## 十五、一句话总结

**跨平台检测的核心就是：Cppcheck 跑两遍（分别指定 `--platform=win64` 和 `--platform=unix64`，并配合 `-D` / `-U` 宏），配置里引用两份报告。** 其他环节保持原样，SonarQube 会把两个平台的问题合并展示，实现跨平台代码质量管控。若追求更精确，可进一步在真实 Linux 环境（WSL2）里跑一次 Cppcheck，用原生报告替换模拟报告。