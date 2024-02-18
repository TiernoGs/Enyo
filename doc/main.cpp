
#include <iostream>
#include <array>

#include <enyo.h>

using namespace enyo;

constexpr double PI = 3.14159265359f;
constexpr double epsilon = 1e-8;


void test1()
{
    std::cout << "Starting TEST 1 ---------------------------------------------------\n";
    std::cout << "Calculating f(x) = sin(x) / x for x in [0:PI]\n";

    std::array<double, 256> arr;

    enyo::ParallelFor(0, 256, [&](const int64_t i)
        {
            arr[i] = sin(PI * double(i) / 256.0);
            arr[i] /= float(i) + epsilon;
        });


    for (const auto& v : arr)
        std::cout << v << ' ';
    std::cout << std::endl;
    std::cout << "Ending TEST 1 -----------------------------------------------------\n\n";
}


void test2()
{
    std::cout << "Starting TEST 2 ---------------------------------------------------\n";
    std::cout << "Calculating f(x, y) = x * y for x, y in ]0:64]\n";

    std::array<std::array<int32_t, 64>, 64> arr;

    enyo::ParallelFor2D({ {0,0}, {64, 64} }, [&](const std::pair<int64_t, int64_t> p)
    {
        arr[p.first][p.second] = (p.first + 1) * (p.second + 1);
    });

    std::cout << arr[63][63] << " == " << 64 * 64 << '\n';
    std::cout << "Ending TEST 2 -----------------------------------------------------\n\n";
}

int main(int argc, char* argv[])
{
    // Initializing the multithreaded library
    enyo::Init(4);
    
    test1();
    test2();

    // Clearing everything
    enyo::Cleanup();

    return 0;
}