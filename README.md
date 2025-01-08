# ENYO: A fast multiprocessing C++17 library forked from PBRT v4 (CPU only)

This small lib grants you the power of multithreading in C++! It should be fairly easy to use and is compatible with the new standards of Cpp.
Moreover, the lib is a fork from pbrt-v4 available at this URL: https://github.com/mmp/pbrt-v4

## Requirements

- CMake
- C++ 17 compiler

## Platforms supported

- Windows
- Linux (Tested on Ubuntu 18)
- !Untested on MacOS!

## Documentation

### Build options

`ENYO_BUILD_TEST` builds the template C++ executable

`ENYO_FORCE_DEBUG` prints output message in Release mode


### Code example

#### Single "for-loop" example to calculate _sin(x)/x_

As shown below, the *'ParallelFor'* loop corresponds to a C *for-loop* `for(int64_t i = 0; i < 256; i++)`

```c++
std::array<double, 256> arr;
enyo::ParallelFor(0, 256, [&](const int64_t& i)
{
    arr[i] = sin(PI * double(i) / 256.0);
    arr[i] /= float(i) + epsilon;
});
```

#### 2D "for-loop" example to calculate _x * y_

```c++
std::array<std::array<int32_t, 64>, 64> arr;
enyo::ParallelFor2D({ {0,0}, {64, 64} }, [&](const std::pair<int64_t, int64_t>& p)
{
    arr[p.first][p.second] = (p.first + 1) * (p.second + 1);
});
```
