#!/data/data/com.termux/files/usr/bin/bash
echo "编译 auto.cpp ..."
clang++ -std=c++17 -pthread -O2 auto.cpp -o pmagnet
if [ $? -eq 0 ]; then
    echo "编译成功，生成 pmagnet"
else
    echo "编译失败"
fi