# Codebase Onboarding

## 项目边界

这个工作目录里有两个需要一起理解、但各自独立构建和发布的项目：

- 根目录是 **UltraFoil**：Nintendo Switch homebrew 客户端，C++20、devkitPro/libnx、Plutonium/SDL。它通过 HTTP 与 AeroFoil 通信。
- `AeroFoil/` 是独立的 Flask 服务端 Git 仓库：管理游戏库、网页商店、账号、下载任务、存档和金手指数据。
- `aio-switch-updater/` 是单独克隆的参考项目，不参与 UltraFoil 的 Makefile 或 GitHub Actions 构建；README 说明 Build ID 读取流程借鉴了它。遵守其来源和许可证要求。

根仓库的 `.gitmodules` 只声明 `include/Plutonium`。当前工作区里 `AeroFoil/`、`aio-switch-updater/` 会显示为根仓库的未跟踪目录，但它们各自有 Git 历史；不要把它们误认为根仓库的普通源码，也不要无意间把整份嵌套仓库 `git add` 进客户端提交。AeroFoil 的 Git、Docker Compose 和 CI 命令应在 `AeroFoil/` 目录上下文中运行。

## Quick Start

### AeroFoil 服务端（本地开发）

需要 Python 3.11（与 Docker 基础镜像版本一致）。目前没有 `.env.example`；配置通过环境变量、Web Settings 和 `app/config/` 文件管理。

```bash
cd AeroFoil
python -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
python app/app.py
```

默认访问 `http://localhost:8465`。运行测试：

```bash
python -m unittest discover -s tests
```

Docker 开发环境：`cd AeroFoil && docker compose -f docker-compose.dev.yml up --build`。日常服务端 Compose：`cd AeroFoil && docker compose up -d`。首次部署建议从 `AeroFoil/docker-compose.yml` 复制并改为自己的镜像、挂载路径、端口和强凭据。

### UltraFoil Switch 客户端

需要 devkitPro/libnx、Make、Plutonium 子模块及 Makefile 依赖（包括 `curl-config`、`sdl2-config`、`pkg-config`、`switch-ntfs-3g`、`switch-lwext4`）。

```bash
git submodule update --init --recursive
make -j4 RELEASE=1
```

产物为根目录 `ultrafoil.nro`。Makefile 会要求设置 `DEVKITPRO`。根目录 GitHub Actions 会递归初始化子模块并安装额外 Switch 包；发布工作流把 NRO 放进 `switch/UltraFoil/ultrafoil.nro` 后打成 ZIP。

## Architecture

```text
UltraFoil (Switch, C++/libnx)
  ├─ AeroFoil shop / Remote：浏览和安装内容
  ├─ AeroFoil cheats API：Title ID + 精确 Build ID 查询、bundle 下载
  └─ AeroFoil saves API：存档备份上传、列出、下载、删除

AeroFoil (Flask + Waitress)
  ├─ SQLite：用户、库扫描索引、标题与访问记录
  ├─ 文件卷：游戏文件、配置、缓存、存档、金手指 JSON 数据
  └─ 可选外部服务：TitleDB、金手指上游、Prowlarr、下载客户端、GeoIP
```

UltraFoil 的入口在 `source/main.cpp`，UI 在 `source/ui/`，内容安装代码在 `source/install/` 与 `source/remoteInstall.cpp` / `source/netInstall.cpp`，Switch 系统接口在 `source/nx/`，配置、HTTP、Cheat 与 Save Sync 服务在 `source/util/`。当前产品入口按 README 只暴露 Remote、URL/LAN 网络安装、金手指、存档备份和设置；SD/USB/HDD/MTP 旧实现不代表当前可见功能。

AeroFoil 的 Flask 应用和路由主要在 `AeroFoil/app/app.py`，身份验证在 `app/auth.py`，ORM/启动迁移在 `app/db.py`，设置在 `app/settings.py`，游戏库扫描在 `app/library.py`，TitleDB 索引在 `app/titles.py`，Watchdog 文件监听在 `app/file_watcher.py`，定时任务在 `app/scheduler.py`，Cheat provider/同步在 `app/cheats/service.py`，下载集成在 `app/downloads/`。页面模板与静态资源分别在 `app/templates/`、`app/static/`。

## Data Models and Storage

AeroFoil 使用 Flask-SQLAlchemy + SQLite，Alembic/Flask-Migrate 迁移位于 `AeroFoil/app/migrations/versions/`。主数据库默认是 `app/config/aerofoil.db`（容器内通常为 `/app/config/aerofoil.db`）；`AEROFOIL_DB_FILE` 可覆盖路径，旧库 `ownfoil.db` 仍有兼容逻辑。应用启动时会创建或迁移 schema；迁移过程会备份数据库，但升级前仍应备份整个 `/app/config` 卷。

主要实体、字段和关系（定义在 `AeroFoil/app/db.py`）：

| Entity | 主要字段 | 关系 / 约束 |
|---|---|---|
| `Libraries` | `id`, `path`, `last_scan` | `path` unique；一对多 `Files` |
| `Files` | `id`, `library_id`, `filepath`, `folder`, `filename`, `extension`, `size`, `compressed`, `multicontent`, `nb_content`, `download_count`, `identified`, `identification_type`, `identification_error`, `identification_attempts`, `last_attempt` | `library_id` → `Libraries`；`filepath` unique；经 `app_files` 与 `Apps` 多对多 |
| `Titles` | `id`, `title_id`, `have_base`, `up_to_date`, `complete` | `title_id` unique；一对多 `Apps` |
| `Apps` | `id`, `title_id`, `app_id`, `app_version`, `app_version_num`, `app_type`, `owned` | `title_id` → `Titles`；`(app_id, app_version)` unique；经 `app_files` 与 `Files` 多对多 |
| `User` | `id`, `user`, `password`, `admin_access`, `shop_access`, `backup_access`, `frozen`, `message`, `client_uid`, `last_login_at`, `last_login_ip`, `last_login_country`, `last_login_country_code` | `user` unique；`password` 存 Werkzeug 哈希 |
| `TitleRequests` | `id`, `created_at`, `status`, `title_id`, `title_name`, `user_id` | `user_id` → `User` |
| `TitleRequestViews` | `id`, `user_id`, `request_id`, `viewed_at` | 指向 `User` 和 `TitleRequests`；`(user_id, request_id)` unique |
| `AccessEvents` | `id`, `at`, `kind`, `user`, `remote_addr`, `user_agent`, 地理字段、`title`/`file`/`filename`、`bytes_sent`, `ok`, `status_code`, `duration_ms` | 访问/传输审计；`file_id` 不设 FK |

`app_files` 是 `Apps` 与 `Files` 的多对多关联表。没有独立的数据库 seed 文件；首次初始化创建表后会同步 Alembic head，迁移文件在 `app/migrations/versions/`。

金手指没有放在主 SQL 数据库中：Docker 镜像带固定快照 `/opt/aerofoil-cheatdb`，同步/覆盖数据放在 `/app/data/cheatdb`，以结构化 JSON 加载并可按设置回退到上游。TitleDB 的原始数据在 `/app/data/titledb/`，服务端另建可重建的 SQLite 索引：`versions(title_id, version, release_date)`、`cnmts(app_id, version_key, sort_order, version_int, title_type, other_application_id)`、`titles(title_id, name, banner_url, icon_url, category, nsu_id, description, search_hay, sort_order)`。实际游戏文件仍存放在挂载的游戏库目录中，数据库主要保存索引和识别状态。

UltraFoil 不使用关系数据库：配置位于 Switch SD 卡 `sdmc:/switch/UltraFoil/`；cheat 文本安装到 `sdmc:/atmosphere/contents/<TITLE_ID>/cheats/<BUILD_ID>.txt`，来源、标签和冲突组元数据写入 `cheat_state/`；存档由 Switch 原生 save-data API 读取，再通过 AeroFoil 存档 API 同步。

## API Reference

AeroFoil 是 Flask HTTP/JSON API（REST 风格，包含动作型 POST），并兼有 Tinfoil 商店协议与文件流接口。路由主要定义在 `AeroFoil/app/app.py`，登录及用户管理路由在 `AeroFoil/app/auth.py`。下文权限简写：**admin** = 已登录且有管理员权限；**shop** = 商店访问权限；**tinfoil** = 商店/客户端协议访问（公开商店可匿名，私有商店需凭据）；**backup** = 有备份权限的用户，存档同步也接受对应 HTTP Basic Auth；**global** = 还会受全局 IP/国家规则约束。

### UltraFoil 直接使用

- `GET /api/client/status`、`GET /api/cheats/titles/<title_id>/index`：API 能力诊断与不含代码正文的 BID/版本索引，区分实际 BID 已匹配、版本推荐、信息不足和不兼容。
- `GET /api/cheats/titles/<title_id>/builds/<build_id>`：只请求指定 Title ID + Build ID 的 cheat；`GET /api/cheats/titles/<title_id>/bundle`：下载该标题所有 build 的 bundle。两者使用 Remote 用户凭据。
- `GET /api/saves/list`、`POST /api/saves/upload/<title_id>`、`GET /api/saves/download/<title_id>/<save_id>.zip`、`DELETE /api/saves/delete/<title_id>/<save_id>`：存档同步。
- 商店安装使用 `/api/shop/sections`、`/api/get_game/<id>` 等 shop 路由。客户端还会尝试 `/api/remote/sections`，AeroFoil 当前没有该 route，之后会回退到 `/api/shop/sections`。

### 路由清单

**Shop / Tinfoil**

- `tinfoil`: `GET /api/shop/sections`、`GET /api/shop/icon/<title_id>`、`GET /api/shop/banner/<title_id>`、`GET /api/get_game/<int:id>`、`GET /api/cheats/titles/<title_id>/builds/<build_id>`、`GET /api/cheats/titles/<title_id>/bundle`、`POST /api/cheats/render`。
- `shop`: `GET /api/titles`、`GET /api/title-details`、`GET /api/title-info/<title_id>`、`GET /api/titledb/search`、`GET /api/library/size`、`GET /api/library/status`、`POST /api/requests`、`GET /api/requests`。
- `global`（无单独 endpoint-level auth）：`GET /api/frozen/notice`。

**Cheat 管理 / 库 / TitleDB（admin）**

- `GET /api/cheats/titles/<title_id>`、`GET /api/cheats/database/status`、`POST /api/cheats/database/sync`。
- `POST /api/title-info/manual`、`POST /api/library/scan`。

**Requests（shop/admin）**

- `admin`: `GET /api/requests/unseen-count`、`POST /api/requests/mark-seen`、`POST /api/requests/delete`、`GET /api/requests/search`。

**用户 / 登录（admin，除页面路由注明外）**

- `GET /api/users`、`GET /api/admin/user-history/<int:user_id>`、`GET /api/auth/lockouts`、`POST /api/auth/lockouts/unlock`、`POST /api/auth/lockouts/unlock-all`。
- `PATCH /api/user/freeze`、`PATCH /api/user`、`DELETE /api/user`、`PATCH /api/user/password`、`POST /api/user/signup`。
- 页面路由：`GET+POST /login`、`GET /logout`；`GET /profile` 要登录且有 backup access。

**审计 / 安全（admin）**

- `GET /api/admin/activity`、`GET /api/admin/clients-history`、`GET /api/admin/clients-history.csv`。
- `POST /api/admin/clients-history/clear`、`POST /api/admin/access-history/clear`、`POST /api/admin/blacklist-ip`。

**Settings / 媒体缓存 / 库路径（admin）**

- `GET /api/settings`；`POST /api/settings/titles`、`POST /api/settings/shop`、`POST /api/settings/downloads`、`POST /api/settings/library`。
- `POST /api/settings/media-cache/refresh`、`POST /api/settings/media-cache/prefetch-icons`、`POST /api/settings/media-cache/prefetch-banners`。
- `POST /api/settings/downloads/test-prowlarr`、`POST /api/settings/downloads/test-client`。
- `GET+POST+DELETE /api/settings/library/paths`。

**下载 / 维护 / 转换（admin）**

- Downloads: `POST /api/downloads/manual`、`POST /api/downloads/manual-search`、`GET /api/downloads/search`、`POST /api/downloads/queue`、`GET /api/downloads/queue`、`POST /api/downloads/queue/delete`、`POST /api/downloads/check-completed`、`GET /api/downloads/active`。
- Maintenance: `POST /api/manage/organize`、`POST /api/manage/delete-updates`、`POST /api/manage/delete-duplicates`、`POST /api/manage/delete-library-content`、`POST /api/manage/delete-orphaned-addons`、`POST /api/manage/check-downloads`、`GET /api/manage/downloads-queue`。
- Conversion: `POST /api/manage/convert`、`POST /api/manage/convert-single`、`POST /api/manage/convert-job`、`POST /api/manage/convert-single-job`、`GET /api/manage/convertibles`、`GET /api/manage/convert-job/<job_id>`、`POST /api/manage/convert-job/<job_id>/cancel`。
- Admin diagnostics/status: `GET /api/manage/jobs`、`GET /api/manage/health`、`GET /api/manage/diagnostics/memory`、`GET /api/diagnostics/memory`（alias）、`GET /api/manage/libraries`。

**上传 / 存档**

- `admin`: `POST /api/upload`、`DELETE /api/upload/keys`、`POST /api/upload/library`。
- `backup`: `GET /api/saves/list`、`POST /api/saves/upload/<title_id>`、`GET /api/saves/download/<title_id>`、`GET /api/saves/download/<title_id>/<save_id>`，以上下载路径也接受 `.zip` 后缀；`DELETE` 和兼容 `POST` 均支持 `/api/saves/delete/<title_id>` 与 `/api/saves/delete/<title_id>/<save_id>`（删除路径也接受 `.zip` 别名）。数据按用户隔离。

## Authentication and Authorization

- Web 登录用 Flask-Login session cookie；没有 JWT/OAuth。用户密码以哈希形式存储。
- 普通管理 API 由 `access_required('admin'|'shop'|'backup')` 校验登录态和权限；未登录通常进入登录页，无权限返回 403。
- 私有 Tinfoil/shop 请求可用已登录 session 或 HTTP Basic Auth；公开商店可匿名浏览。客户端接口还可能校验 Host/Hauth，冻结用户和 external-Tinfoil-only 设置也会影响访问。`get_game` 文件下载有协议特例。
- 存档同步接受 session 或 Basic Auth，但还要求账号未冻结且有 shop + backup 权限。
- 全局请求检查支持永久 IP/CIDR 黑名单、失败登录临时锁定，以及 GeoIP 国家 allow/block。若在反向代理后启用 `AEROFOIL_TRUST_PROXY_HEADERS`，只应将实际代理地址配置到 `AEROFOIL_TRUSTED_PROXIES`。
- 首次管理员 bootstrap 默认限制在可信私网/已配置可信代理范围内；设置 `USER_ADMIN_NAME` 与 `USER_ADMIN_PASSWORD` 可在启动时创建或更新管理员。若管理员设置 latch 已完成后再删光管理员，应用不会自动重新开放注册，需通过环境变量恢复。生产环境设置固定且强随机的 `AEROFOIL_SECRET_KEY`，否则直接运行应用时自动生成的会话密钥会在重启后变化；多副本也必须共享同一密钥。

## Deployment and Configuration

### AeroFoil Docker

官方 Compose 默认端口 `8465:8465`、镜像 `luketanti/aerofoil:latest`。生产至少持久化以下卷：

- 游戏库目录 → `/games`
- 配置/SQLite/密钥 → `/app/config`
- TitleDB、缓存、同步金手指与存档 → `/app/data`
- 可选转换暂存盘 → `/app/conversion-tmp`

常用环境变量：`PUID` / `PGID`、`USER_ADMIN_NAME` / `USER_ADMIN_PASSWORD`、`AEROFOIL_SECRET_KEY`、`AEROFOIL_HOST` / `AEROFOIL_PORT`、`AEROFOIL_WSGI_THREADS`、`AEROFOIL_TRUST_PROXY_HEADERS` / `AEROFOIL_TRUSTED_PROXIES`、`AEROFOIL_CHEATS_DB_DIR` / `AEROFOIL_CHEATS_REMOTE_FALLBACK` / `AEROFOIL_CHEATS_SYNC_INTERVAL_HOURS`、`AEROFOIL_UPLOAD_TMP_DIR`、`AEROFOIL_CONVERSION_STAGING_DIR` 以及 `SHOP_SECTIONS_CACHE_TTL_S` / `MEDIA_INDEX_TTL_S`。`USER_ADMIN_*` 和 `USER_GUEST_*` 在每次启动时都会创建或更新对应账号，不只是一次性初始化开关。完整选项以 `AeroFoil/README.md` 与 Compose 为准。镜像构建需访问 GitHub 获取固定 commit 的 cheat 数据库，Python requirements 也含 Git URL 依赖。

`docker-compose.dev.yml` 将宿主机源码目录热挂载到容器；启动脚本递归 `chown /app`，开发容器可能改变宿主机源码文件所有权。开发配置里的示例密码只供本地测试，不能用于生产。Requests 依赖可读取标准代理环境变量（如 `HTTP_PROXY`、`HTTPS_PROXY`、`ALL_PROXY`、`NO_PROXY`；安装了 PySocks），但这不是覆盖所有网络请求路径的应用级代理设置。

### CI / 发布

- 根 `.github/workflows/cyberfoil-build-*.yml`：用固定摘要工具链构建 UltraFoil NRO；手动 release workflow 发布 ZIP。`ci.yml` 另行验证文件回滚与 HTTP Range。
- `AeroFoil/.github/workflows/`：服务端的 dev/master/tag 流程发布 Docker 镜像（GHCR/Docker Hub，多架构）；`ci.yml` 运行 unittest 后构建镜像，功能分支发布单独预览标签；TitleDB 生成另有定时 workflow。
- AeroFoil 本地测试：`cd AeroFoil && python -m unittest discover -s tests`。

## Key Files to Know

- `README.md` — UltraFoil 功能边界、部署配对、cheat/save API 和构建说明。
- `Makefile` — 客户端目标、devkitPro 依赖、编译选项和 NRO 输出配置。
- `source/main.cpp`、`source/ui/MainApplication.cpp` — Switch 应用入口与主 UI。
- `source/remoteInstall.cpp`、`source/netInstall.cpp`、`source/util/cheat_service.cpp`、`source/util/save_sync.cpp` — Remote/LAN、Build ID cheat 和存档同步链路。
- `AeroFoil/AGENTS.md` — 服务端开发规范、测试命令和安全约定。
- `AeroFoil/app/app.py`、`AeroFoil/app/auth.py`、`AeroFoil/app/db.py` — 服务启动、路由/鉴权、数据库模型和迁移初始化。
- `AeroFoil/app/library.py`、`AeroFoil/app/titles.py`、`AeroFoil/app/cheats/service.py` — 游戏库扫描、TitleDB 数据和 cheat provider。
- `AeroFoil/docker-compose.yml`、`AeroFoil/Dockerfile`、`AeroFoil/requirements.txt` — 服务容器运行、镜像构建和 Python 依赖。

## Common Gotchas

- UltraFoil 与 AeroFoil 是分开构建、分开提交、分开发布的；只更新服务端不会更新 Switch NRO，反之亦然。
- Cheat 查询要求精确 Build ID；“有该游戏数据”不代表当前游戏版本一定有可用代码。
- 本地 NRO 构建前确认 `DEVKITPRO` 已设置、Plutonium 子模块初始化、NTFS/ext4 Switch 包安装齐全。
- 挂载 `/app/config` 与 `/app/data`，不要只挂 `/games`；否则数据库、用户、配置、存档和缓存可能随容器重建丢失。
- 更新 AeroFoil 前备份配置卷/SQLite；生产不要依赖默认会变化的 session key，也不要保留无管理员认证状态。
- 若 shop 设为 public，标记为 `tinfoil_access` 的金手指查询/渲染接口也按公开商店路径放行；需要私密金手指时应使用私有 shop，并检查其权限设置。
- UltraFoil 将 Remote 用户名/密码保存在 SD 卡的 `config.json`；客户端若设备丢失或 SD 卡暴露，凭据也会暴露。另外，当前客户端多个 HTTPS 请求关闭了 TLS 证书验证，因此 HTTPS 不能防御伪造服务器/中间人；不要把私有 Remote 凭据暴露在不可信网络，优先使用受信 LAN/VPN，并在代码层恢复证书校验。
- 服务端登录锁定记录在进程内存中，多 worker/重启时不共享；调查也未发现管理表单的 CSRF token/middleware，若要公开部署应先审视反代、浏览器会话和 CSRF 风险。登录 `next` 参数当前未见目标校验。
- 私有 shop、Basic Auth 和代理均应在真实部署配置中验证；Docker 环境变量值不要意外包含引号字符。
