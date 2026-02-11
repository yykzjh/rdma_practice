./build/bin/ibv_srq_pingpong \
    -p 18515 \
    -d mlx5_0 \
    -i 1 \
    -s 52428800 \
    -m 1024 \
    -q 64 \
    -r 500 \
    -n 1000 \
    -l 0 \
    -g 0 \
    \
    -e \
    -o \
    -c \
    127.0.0.1