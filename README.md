# Pi Calculator

高精度圆周率计算工具，支持命令行和图形界面两种模式。

## 功能

- **单次计算**：计算 π 到任意精度（命令行 `pi 1000` / GUI "Calc"）
- **流式计算**：连续计算并实时输出新增位数（`pi --live` / GUI "Live"）
- **图形界面**：Win32 原生窗口，后台线程计算，界面不卡顿
- **文件输出**：实时保存到文件

## 算法

[Chudnovsky 算法](https://en.wikipedia.org/wiki/Chudnovsky_algorithm) + Binary Splitting，
全部使用 [GMP](https://gmplib.org/) 的大整数算术。

核心公式：

$$ \frac{1}{\pi} = 12 \sum_{k=0}^{\infty} \frac{(-1)^k (6k)! (545140134k + 13591409)}{(3k)! (k!)^3 640320^{3k + 3/2}} $$

## 文件结构

```
pi-calc/
├── pi.py              # Python 版本（依赖 Python 3 + gmpy2）
├── src/
│   ├── pi.c           # C 命令行版源码（需 GMP）
│   ├── pi_gui.c       # C 图形界面版源码（需 GMP + Win32）
│   ├── build_pi.bat   # 命令行版 MSVC 编译脚本
│   └── build_gui.bat  # 图形界面版 MSVC 编译脚本
└── README.md
```

## 编译（C 版本）

需要 [Visual Studio 2022+](https://visualstudio.microsoft.com/) 和 [GMP](https://gmplib.org/)（可从 Python gmpy2 包获取）。

```bash
# 命令行版
cd src
build_pi.bat
pi 1000

# 图形界面版
build_gui.bat
pi_gui.exe
```

## 性能

本机测试结果（C 命令行版，使用 GMP）：

| 精度 | 耗时 |
|------|------|
| 1,000 位 | < 1 ms |
| 10,000 位 | ~1 ms |
| 100,000 位 | ~30 ms |
| 500,000 位 | ~170 ms |

## 参考

- [y-cruncher](http://www.numberworld.org/y-cruncher/) - Alexander Yee 的 π 计算软件
- [GMP 6.3.0](https://gmplib.org/) - GNU 多精度算术库
- [StorageReview 314T 纪录](https://www.storagereview.com/review/storagereview-sets-new-pi-record-314-trillion-digits-on-a-dell-poweredge-r7725)
