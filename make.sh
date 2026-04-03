rm -rf ./build
mkdir -p build

# -O3 - максимальная оптимизация. -O0 - без оптимизации.
# -Wall -Wextra - дополнительные предупреждения

g++ -std=c++20 -Wall -Wextra -O3 -o ./build/main main.cpp

./build/main
