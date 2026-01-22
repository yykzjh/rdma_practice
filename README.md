# rdma_practice

用于在一个独立仓库里按“模块”组织 RDMA 相关小程序（examples），并用 CMake 编译。

## 项目结构（可扩展）

- 顶层每个子目录对应一个模块（例如：`libibverbs`、`librdmacm` 等）
  - `<module>/include/`：该模块专用头文件（例如用于放上游示例依赖的 `config.h`）
  - `<module>/*.cpp`：每个示例程序一个 `.cpp`，对应一个可执行文件（由该模块的 `CMakeLists.txt` 统一管理）

当前已接入：`libibverbs/device_list.cpp`（可执行文件名：`ibv_device_list`）。

## 编译与运行

```bash
mkdir -p build
cd build
cmake ..
make -j
./bin/ibv_device_list
```
