#!/bin/bash
# 编译内核模块 + 用户程序，并拷贝到 NFS 目录供开发板加载
set -e
make install
