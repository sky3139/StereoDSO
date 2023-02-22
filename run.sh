#!/bin/bash

cd ./build
cmake ..
make -j3
cd ..

GLOG_minloglevel=2 ./build/bin/dso_dataset_euroc \
files=/home/jg/Documents/Datasets/EuRoC/V1_01_easy/mav0 \
calib=/home/jg/Desktop/dso_my_workspace/configs/EuRoC/camera_left_rec.txt \
calibRight=/home/jg/Desktop/dso_my_workspace/configs/EuRoC/camera_right_rec.txt \
preset=0 \
mode=1




#1.重命名dpkg目录下的info目录
qing@qingsword.com:~$ sudo mv /var/lib/dpkg/info /var/lib/dpkg/info_qingsword

#2.创建一个新的info文件夹
qing@qingsword.com:~$ sudo mkdir /var/lib/dpkg/info

#3.执行更新操作
qing@qingsword.com:~$ sudo apt-get update && sudo apt-get -f install

#4.将更新操作产生的文件，全部复制到重命名的info_qingsword文件夹下
qing@qingsword.com:~$ sudo mv /var/lib/dpkg/info/* /var/lib/dpkg/info_qingsword

#5.删除创建的info文件夹
qing@qingsword.com:~$ sudo rm -rf /var/lib/dpkg/info

#6.将重命名的info_qingsword文件夹重新重命名为info
qing@qingsword.com:~$ sudo mv /var/lib/dpkg/info_qingsword /var/lib/dpkg/info

#7.再次执行更新操作，问题解决
qing@qingsword.com:~$  sudo apt-get update && sudo apt-get upgrade

