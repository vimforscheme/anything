# SonarQube 本地化部署与 Qt 项目 C++ 代码质量检测全量指南

> **版本适配说明**：本指南基于 SonarQube Community Build `26.1.0.118079-community` + `sonar-cxx` 插件 2.3.0，二者为官方测试并发布的兼容组合。`sonarqube:latest` 版本过新，cxx 插件不支持，**不建议使用**。


## 一、环境准备

### 1.1 下载 Docker Desktop AMD64

下载并安装 Docker Desktop AMD64 版本。

### 1.2 安装 WSL2 并开启 Windows 功能

安装 WSL2，并在 Windows 功能中开启：
- 虚拟机平台
- 适用于 Linux 的 Windows 子系统

### 1.3 网络代理注意事项

> **重要**：使用全局代理下载 Docker 镜像时，**不要使用 TUN 模式**，否则 Docker Desktop 可能无法正常解析网络请求。


## 二、拉取 SonarQube 镜像并启动容器

### 2.1 清理旧容器（如有）

```bash
docker stop sonarqube
docker rm sonarqube
```

### 2.2 拉取指定版本镜像

```bash
#镜像查看网址
https://hub.docker.com/layers/library/sonarqube/

# 推荐写法（等价于完整地址）
docker pull sonarqube:26.1.0.118079-community

# 完整写法（效果完全相同）
docker pull docker.io/library/sonarqube:26.1.0.118079-community
```

> **注意**：`docker pull docker.io/library/sonarqube:latest` 版本太新，`sonar-cxx` 插件不支持，**不建议使用**。

### 2.3 启动 SonarQube 容器（带数据卷）

```bash
docker run -d --name sonarqube ^
  -p 9000:9000 ^
  -v sonarqube_data:/opt/sonarqube/data ^
  -v sonarqube_logs:/opt/sonarqube/logs ^
  -v sonarqube_extensions:/opt/sonarqube/extensions ^
  -e SONAR_ES_BOOTSTRAP_CHECKS_DISABLE=true ^
  sonarqube:26.1.0.118079-community
```

> **数据卷说明**：使用 `-v` 挂载数据卷后，即使删除容器，数据也不会丢失。以后重启或升级容器时，只要挂载相同的卷，配置和项目数据都能保留。

执行成功后可看到类似容器 ID：

```
aa4b67c0b8b6dffda5fcab4aa2365d012c11c4bf532e6f2820b8d770a51978a1
```

### 2.4 安装 sonar-cxx 插件

从 [SonarOpenCommunity/sonar-cxx Releases](https://github.com/SonarOpenCommunity/sonar-cxx/releases/tag/cxx-2.3.0) 下载 `sonar-cxx-plugin-2.3.0.jar`，然后将其复制到容器中：

```bash
docker cp sonar-cxx-plugin-2.3.0.jar sonarqube:/opt/sonarqube/extensions/plugins/

# 重启容器使插件生效
docker restart sonarqube
```

插件安装后，SonarQube 的“质量配置”中就会出现 C++ 相关的规则集。`sonar-cxx` 插件的核心设计思路并非自己分析代码，而是作为“适配器”，**集成现有的 C++ 生态工具**（如 Cppcheck、Clang-Tidy）的分析结果。

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

在 Visual Studio 中安装 Sonar 插件，退出后会自行安装。安装完成后通过 **Extensions > SonarQube for Visual Studio > Connected Mode > Bind to SonarQube (Server, Cloud)…** 连接本地 SonarQube 服务器。

这样可以在 IDE 中直接获得代码质量提示，并与 SonarQube 服务器保持规则同步。


## 四、安装 SonarScanner CLI

### 4.1 下载

文档网站：[SonarScanner CLI | SonarQube Server 2026.1 LTA | Sonar Documentation](https://docs.sonarsource.com/sonarqube-server/2026.1/analyzing-source-code/scanners/sonarscanner)

从官方下载 Windows x64 版本：

- [SonarScanner CLI 8.1.0.6389 - Windows x64](https://binaries.sonarsource.com/Distribution/sonar-scanner-cli/sonar-scanner-cli-8.1.0.6389-windows-x64.zip)
- [SonarScanner CLI 8.0.1.6346 - Windows x64](https://binaries.sonarsource.com/Distribution/sonar-scanner-cli/sonar-scanner-cli-8.0.1.6346-windows-x64.zip)

### 4.2 安装与配置环境变量

1. 将下载的压缩包解压到任意目录（例如 `C:\sonar-scanner`）。
2. 将 `C:\sonar-scanner\bin` 添加到系统的 `PATH` 环境变量中：
   - 按 `Win + S`，搜索 **“环境变量”**
   - 点击 **“编辑系统环境变量”**
   - 在“系统变量”中找到 `Path`，点击“编辑”
   - 添加 `C:\sonar-scanner\bin`

3. 验证安装：

```bash
# 打开新的命令行窗口
sonar-scanner -h
```

如果显示帮助信息，则安装成功。

### 4.3 Java 运行时要求

SonarScanner CLI 8.x 需要 **Java 21 或更高版本**（Java 17 已废弃）。如果本机未安装 Java，可以下载包含嵌入式 JRE 的完整版本（即上述 Windows x64 版本，已内置 JRE）。


## 五、生成 SonarQube 项目与 Token

### 5.1 创建项目

1. 登录 SonarQube 网页（`http://localhost:9000`）
2. 点击 **“项目”** → **“创建项目”** → **“手动”**
3. 输入项目名（如 `My Qt Project`）和项目 Key（如 `my-qt-project`）
4. 选择 **“使用全局设置”** → 点击 **“创建项目”**

### 5.2 生成 Token

1. 进入 **“我的账户”** → **“安全”** 标签页（`http://localhost:9000/account/security`）
2. 输入 Token 名称（如 `local-qt-scan`），点击 **“生成”**
3. **立即复制并保存 Token**（页面关闭后无法再次查看）

> Token 用于扫描器认证，每次执行分析时都需要提供。

## 六、配置 Qt 项目（跨平台版）

### 6.1 创建 sonar-project.properties 文件

在 Qt 项目**根目录**下创建 `sonar-project.properties` 文件，内容如下：

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

# ===== 语言与文件后缀（sonar-cxx 插件必需）=====
sonar.language=cpp
sonar.cxx.file.suffixes=.cpp,.cxx,.cc,.c,.h,.hpp,.hxx

# ===== 排除 Qt 生成文件和第三方库 =====
sonar.exclusions=**/build/**,**/moc_*.cpp,**/ui_*.h,**/qrc_*.cpp,**/*.qm,**/*.qml,**/*.svg,**/*.dll,**/*.obj

# ===== 外部工具报告路径（关键：同时引用 Windows 和 Linux 两份报告）=====
sonar.cxx.cppcheck.reportPaths=cppcheck-report-win.xml,cppcheck-report-linux.xml
# sonar.cxx.clangtidy.reportPaths=clang-tidy-report.txt
```

> **关键说明**：
>
> - `sonar.cxx.file.suffixes` 是**必须显式设置**的参数，否则 cxx 插件的 C++ 传感器不会启用。
> - Qt 项目会生成大量 `moc_*.cpp`、`ui_*.h`、`qrc_*.cpp` 等中间文件，务必通过 `sonar.exclusions` 排除。
> - `sonar.cxx.cppcheck.reportPaths` 支持**逗号分隔的多路径**，用于同时读取 Windows 和 Linux 两份报告。不要有空格。

### 6.2 Qt 项目特殊配置（可选）

如果 Qt 项目使用了 `new` 等惯用写法，可以添加忽略规则：

```properties
# 忽略 Qt 项目中 new 的规则告警
sonar.issue.ignore.multicriteria=e1
sonar.issue.ignore.multicriteria.e1.ruleKey=cpp:S5025
sonar.issue.ignore.multicriteria.e1.resourceKey=**/*.cpp
```

---

## 七、运行 Cppcheck 生成双平台报告（核心改动）

在运行 SonarScanner 之前，**必须先使用 Cppcheck 分析代码并生成 XML 报告**。因为社区版没有内置 C++ 分析器，`sonar-cxx` 插件依赖外部工具的结果。

### 7.1 安装 Cppcheck

- **方式 A**：从 [Cppcheck 官网](https://cppcheck.sourceforge.io/) 下载 Windows 安装包并安装。
- **方式 B**：使用 Chocolatey 或 Scoop 包管理器安装。

### 7.2 生成 Windows 平台报告

在 Qt 项目根目录下执行：

```bash
cppcheck ^
  --platform=win64 ^
  --xml-version=2 ^
  --enable=all ^
  --force ^
  -D_WIN32 -DWIN32 -D_WIN64 -D_MSC_VER=1930 ^
  --output-file=cppcheck-report-win.xml ^
  .
```

### 7.3 生成 Linux 平台报告（在 Windows 上模拟）

在 Qt 项目根目录下执行：

```bash
cppcheck ^
  --platform=unix64 ^
  --xml-version=2 ^
  --enable=all ^
  --force ^
  -U_WIN32 -UWIN32 -UWIN64 -U_MSC_VER ^
  -D__linux__ -D__GNUC__=11 ^
  --output-file=cppcheck-report-linux.xml ^
  .
```

### 7.4 关键参数解释

| 参数                       | 作用                                                        |
| -------------------------- | ----------------------------------------------------------- |
| `--platform=win64`         | 按 64 位 Windows 数据模型分析（指针 8 字节，`long` 4 字节） |
| `--platform=unix64`        | 按 64 位 Linux 数据模型分析（指针 8 字节，`long` 8 字节）   |
| `-D_WIN32 -DWIN32 -DWIN64` | 定义 Windows 平台宏，激活 `#ifdef _WIN32` 分支              |
| `-U_WIN32 -UWIN32 -UWIN64` | 取消 Windows 宏定义，避免 Linux 分析时误入 Windows 分支     |
| `-D__linux__`              | 定义 Linux 宏，激活 `#ifdef __linux__` 分支                 |
| `-D_MSC_VER=1930`          | 模拟 MSVC 2019 编译器版本（部分 Qt 代码依赖此宏）           |
| `-D__GNUC__=11`            | 模拟 GCC 11（Qt 的 Linux 常见编译器）                       |

> **提示**：`-U` 取消宏定义 + `-D` 定义宏 是让 Cppcheck 精确走通两个平台分支的关键。不加这些参数，Cppcheck 在 Windows 上跑时默认会定义 `_WIN32`，导致 Linux 分支代码直接被跳过，无法被检测。

> **注意**：Cppcheck 报告路径需相对于 `sonar-project.properties` 所在目录，确保路径正确。建议将 Cppcheck 的安装路径添加到系统 `PATH` 环境变量中，以便直接在命令行调用。

---

## 八、执行 SonarScanner 扫描

### 8.1 基本运行命令

在 Qt 项目根目录下执行：

```bash
sonar-scanner -Dsonar.host.url=http://localhost:9000 -Dsonar.token=你的Token
```

也可以将 `sonar.token` 和 `sonar.host.url` 写入 `sonar-project.properties` 中，直接运行：

```bash
sonar-scanner
```

### 8.2 完整命令序列（跨平台版）

```bash
# 1. 清理旧报告（可选，避免旧数据干扰）
del cppcheck-report-win.xml cppcheck-report-linux.xml

# 2. 生成 Windows 平台报告
cppcheck --platform=win64 --xml-version=2 --enable=all --force ^
  -D_WIN32 -DWIN32 -D_WIN64 -D_MSC_VER=1930 ^
  --output-file=cppcheck-report-win.xml .

# 3. 生成 Linux 平台报告
cppcheck --platform=unix64 --xml-version=2 --enable=all --force ^
  -U_WIN32 -UWIN32 -UWIN64 -U_MSC_VER ^
  -D__linux__ -D__GNUC__=11 ^
  --output-file=cppcheck-report-linux.xml .

# 4. 执行 SonarScanner 扫描
sonar-scanner -Dsonar.host.url=http://localhost:9000 -Dsonar.token=你的Token
```

### 8.3 常用命令行参数

| 参数                                     | 说明                                                 |
| ---------------------------------------- | ---------------------------------------------------- |
| `-Dsonar.host.url=http://localhost:9000` | 指定 SonarQube 服务器地址                            |
| `-Dsonar.token=<token>`                  | 指定认证 Token（SonarQube 10.x+ 使用 `sonar.token`） |
| `-Dsonar.projectKey=<key>`               | 指定项目 Key（可省略，已在配置文件中定义）           |
| `-Dsonar.verbose=true`                   | 输出详细日志，便于排查问题                           |
| `-X`                                     | 输出调试日志                                         |

---

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

在 **“配置”** → **“质量配置”** 中，可以看到 C++ 相关的规则集。插件安装后，C++ 规则集会自动出现。可以根据团队需求启用或禁用规则。

---

## 十、完整流程总结（跨平台版）

| 步骤 | 操作                            | 关键命令/配置                                                |
| ---- | ------------------------------- | ------------------------------------------------------------ |
| 1    | 拉取镜像                        | `docker pull sonarqube:26.1.0.118079-community`              |
| 2    | 启动容器                        | `docker run -d --name sonarqube -p 9000:9000 -v ...`         |
| 3    | 安装 cxx 插件                   | `docker cp sonar-cxx-plugin-2.3.0.jar ...` + `docker restart` |
| 4    | 安装 SonarScanner CLI           | 解压 → 添加 `bin` 到 `PATH` → `sonar-scanner -h` 验证        |
| 5    | 创建项目 + 生成 Token           | SonarQube 网页 → 创建项目 → 生成 Token                       |
| 6    | 创建 `sonar-project.properties` | 配置项目 Key、源码路径、C++ 文件后缀、**两份 Cppcheck 报告路径** |
| 7    | 运行 Cppcheck（Windows）        | `cppcheck --platform=win64 ... --output-file=cppcheck-report-win.xml .` |
| 8    | 运行 Cppcheck（Linux）          | `cppcheck --platform=unix64 ... --output-file=cppcheck-report-linux.xml .` |
| 9    | 运行 SonarScanner               | `sonar-scanner -Dsonar.host.url=http://localhost:9000 -Dsonar.token=xxx` |
| 10   | 查看报告                        | 浏览器打开 `http://localhost:9000` 查看项目分析结果          |

---

## 十一、常见问题排查

| 问题                           | 可能原因                         | 解决方案                                                     |
| ------------------------------ | -------------------------------- | ------------------------------------------------------------ |
| 扫描后无 C++ 结果              | 未设置 `sonar.cxx.file.suffixes` | 在配置文件中显式添加该参数                                   |
| Cppcheck 报告未被读取          | 报告路径错误                     | 确保路径相对于 `sonar-project.properties` 文件，且用逗号分隔多路径 |
| Linux 分支代码未被扫描         | 宏定义未正确设置                 | 检查 `-U_WIN32`、`-D__linux__` 是否生效，可加 `--debug` 查看 Cppcheck 实际使用的宏 |
| 同一处代码出现重复问题         | 两份报告都覆盖了公共代码         | sonar-cxx 会按「文件+行号+规则」自动去重，一般不影响         |
| 插件未生效                     | 未重启容器                       | 执行 `docker restart sonarqube`                              |
| 登录时提示 Token 无效          | Token 已过期或输入错误           | 重新生成 Token                                               |
| 扫描时报 Java 版本不兼容       | 本机 Java 版本过低               | 使用内置 JRE 的 SonarScanner 版本（Windows x64 完整版）      |
| SonarQube 容器启动失败         | 内存不足                         | Docker Desktop → Settings → Resources → 内存调至 4GB 以上    |
| Cppcheck 报 `too many configs` | 宏组合太多                       | 加 `--max-configs=1` 或 `--force` 强制分析                   |
| 报告路径包含空格               | Windows 路径如 `C:\My Project\`  | 尽量把项目路径放在无空格目录，或用引号包裹路径               |

---

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

**建议定期备份数据卷**，以防意外情况。

---

## 十三、进阶方案：在真实 Linux 环境中运行分析（最准确）

Cppcheck 的 `--platform=unix64` 只是**模拟** Linux 数据模型，并非真正的 Linux 编译环境。某些深度平台相关的缺陷（如头文件差异、系统 API 差异）仍可能漏检。

**最准确的做法是：在 Linux 环境下（真机或 WSL2）也跑一次 Cppcheck + SonarScanner**：

```bash
# 在 Linux / WSL2 中执行
cppcheck --platform=unix64 --xml-version=2 --enable=all --force \
  --output-file=cppcheck-report-linux-native.xml .

sonar-scanner -Dsonar.host.url=http://localhost:9000 -Dsonar.token=你的Token \
  -Dsonar.cxx.cppcheck.reportPaths=cppcheck-report-linux-native.xml
```

然后把这份**原生 Linux 报告**和 Windows 报告合并：

```properties
sonar.cxx.cppcheck.reportPaths=cppcheck-report-win.xml,cppcheck-report-linux-native.xml
```

> **推荐场景**：
>
> - 日常开发在 Windows 上 → 用第七节的模拟双平台方案快速验证
> - 正式 CI/CD → 用本节方案（Windows + Linux 双机跑），结果最准确

---

## 十四、一句话总结

**跨平台检测的核心就是：Cppcheck 跑两遍（分别指定 `--platform=win64` 和 `--platform=unix64`，并配合 `-D` / `-U` 宏），配置里引用两份报告。** 其他环节保持原样，SonarQube 会把两个平台的问题合并展示，实现跨平台代码质量管控。若追求更精确，可进一步在真实 Linux 环境（WSL2）里跑一次 Cppcheck，用原生报告替换模拟报告。
