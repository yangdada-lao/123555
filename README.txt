========================================
乔治控制台 - Lyra 子弹追踪辅助
========================================

一、编译 C++ 后台程序（在 Termux 中）
----------------------------------------
1. 将 auto.cpp 和 compile_cpp.sh 放在同一目录（如 ~/）
2. 安装编译工具：pkg install clang
3. 执行：chmod +x compile_cpp.sh && ./compile_cpp.sh
4. 生成 pmagnet 可执行文件

二、编译 Android APK（在 MT 管理器中）
----------------------------------------
1. 新建工程，将 AndroidManifest.xml、MainActivity.java、FloatingService.java
   按包名路径（com/george/hackctl/）放置。
2. 编译生成 hackctl.apk 并安装。

三、使用流程
----------------------------------------
1. 启动 Lyra 游戏（com.george.lyra）
2. 在 Termux 中运行后台：
   su -c "nohup ./pmagnet > /dev/null 2>&1 &"
3. 打开“乔治控制台”APP，授予悬浮窗权限。
4. 悬浮窗出现，点击开关控制功能。
5. 点击“退出并停止功能”会终止 pmagnet 并关闭悬浮窗。

四、配置文件说明
----------------------------------------
所有设置保存在 /sdcard/hack.cfg，格式示例：
magnet=1
aim=1
bone=8
smooth=1.0

五、注意事项
----------------------------------------
- 需要 Root 权限
- 仅支持 Lyra 游戏（com.george.lyra）
- 如骨骼偏移不对，请调整 auto.cpp 中的 HEAD_BONE_INDEX 和骨骼数组偏移