# 1. 起干净容器
docker run -it --name qt-dev -w /work ubuntu:16.04

# 2. 复制宿主机文件进去（副本，不挂载）
docker cp /home/user/qt-everywhere-src-5.14.2/. qt-dev:/work/qt-src
docker cp /home/user/zig-linux-x86_64-0.13.0/. qt-dev:/work/zig

# 3. 迭代（反复）
docker start -ai qt-dev
# ... 装依赖、编译、调试 ...
exit
docker start -ai qt-dev
# ... 继续 ...
exit

# 4. 阶段性存档（可选）
docker commit qt-dev qt-dev:step1
docker commit qt-dev qt-dev:step2

# 5. 定稿
docker commit -m "final" qt-dev qt-dev:final

# 6. 验证
docker run -it --name qt-dev-verify qt-dev:final
# ... 确认 ...
exit

# 7. 归档
docker save qt-dev:final -o qt-dev-final.tar

```
docker load -i qt-dev-final.tar
docker run -it qt-dev:final
```











./configure \
  -prefix /opt/qt-5.14.2-arm-static \

  -device-option CROSS_COMPILE=/work/cross/gcc-linaro-7.5.0-2019.12-x86_64_aarch64-linux-gnu/bin/aarch64-linux-gnu- \

  -sysroot /work/cross/gcc-linaro-7.5.0-2019.12-x86_64_aarch64-linux-gnu/aarch64-linux-gnu/libc \

  -xplatform linux-aarch64-gnu-g++ \

  -static \
  -release \
  -opensource -confirm-license \
  -opengl es2 \
  -egl \
  -no-gstreamer \
  -no-alsa \
  -no-pulseaudio \
  -qpa wayland \
  -wayland \
  -no-xcb \
  -qt-zlib \
  -qt-libpng \
  -qt-libjpeg \
  -qt-freetype \
  -qt-pcre \
  -no-openssl \
  -no-cups \
  -no-feature-dbus \
  -nomake examples \
  -nomake tests \
  -skip webengine \
  -skip qt3d \
  -v







libasound -no-gstreamer`、``、`-no-pulseaudio