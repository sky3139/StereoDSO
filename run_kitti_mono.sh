#!/bin/bash

  ./build/bin/dso_dataset \
    files=/home/jg/Documents/Datasets/TUM_MonoVO/sequence_11/images.zip \
    calib=/home/jg/Documents/Datasets/TUM_MonoVO/sequence_11/camera.txt \
    gamma=/home/jg/Documents/Datasets/TUM_MonoVO/sequence_11/pcalib.txt \
    vignette=/home/jg/Documents/Datasets/TUM_MonoVO/sequence_11/vignette.png \
    preset=0 \
    mode=0


./bin/dso_dataset_kitti calib=../configs/Kitti/00/camera_left.txt files=/home/u20/dataset/00  calibRight=/home/u20/bishe/StereoDSO/configs/Kitti/00/camera_right.txt mode=1
