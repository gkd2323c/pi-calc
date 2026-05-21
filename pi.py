#!/usr/bin/env python3
"""
π（圆周率）高精度计算工具。

三种引擎自动切换，支持一次计算与实时流式输出（自适应批大小）。

用法:
  python pi.py                        输出 100 位 π
  python pi.py 1000                   输出 1000 位
  python pi.py 100000 -o pi.txt       输出到文件
  python pi.py 1000000 -q             静默模式（仅数字）
  python pi.py --live                 实时流式计算
  python pi.py --live --algo gmpy2    指定引擎
  python pi.py --list-algo            列出可用引擎
"""

import sys
import os
import time
import math

# ──────────────────────────────────────────────
# 引擎检测
# ──────────────────────────────────────────────

HAS_GMPY2 = False
try:
    import gmpy2
    HAS_GMPY2 = True
except ImportError:
    pass

C3_INT = 640320 ** 3
_SQRT10005_CACHE = None

# ──────────────────────────────────────────────
# 引擎 1：gmpy2 mpz + mpfr
# ──────────────────────────────────────────────

def _bs_mpz(a: int, b: int):
    from gmpy2 import mpz
    if b - a == 1:
        p = mpz(-24 * (6 * a - 5) * (2 * a - 1) * (6 * a - 1))
        q = mpz(a ** 3 * C3_INT)
        t = p * mpz(545140134 * a + 13591409)
        return p, q, t
    m = (a + b) // 2
    p1, q1, t1 = _bs_mpz(a, m)
    p2, q2, t2 = _bs_mpz(m, b)
    return p1 * p2, q1 * q2, t2 * p1 + t1 * q2


def pi_gmpy2(precision: int) -> str:
    global _SQRT10005_CACHE
    from gmpy2 import mpfr

    n_terms = (precision + 1) // 14 + 3
    p, q, t = _bs_mpz(1, n_terms)

    bits = int((precision + 1) * math.log2(10)) + 128
    gmpy2.get_context().precision = bits

    if _SQRT10005_CACHE is None or _SQRT10005_CACHE[0] < bits:
        _SQRT10005_CACHE = (bits, gmpy2.sqrt(mpfr(10005)))
    sqrt10005 = _SQRT10005_CACHE[1]

    term0 = mpfr(13591409)
    s = term0 * mpfr(q) + mpfr(t)
    pi = mpfr(426880) * sqrt10005 * mpfr(q) / s

    full = format(pi, f".{precision+1}f")
    return full[:precision + 2]


# ──────────────────────────────────────────────
# 引擎 2：Python int BS + Decimal
# ──────────────────────────────────────────────

from decimal import Decimal, getcontext as _dctx

def _bs_int(a: int, b: int):
    if b - a == 1:
        p = -24 * (6 * a - 5) * (2 * a - 1) * (6 * a - 1)
        q = a ** 3 * C3_INT
        t = p * (545140134 * a + 13591409)
        return p, q, t
    m = (a + b) // 2
    p1, q1, t1 = _bs_int(a, m)
    p2, q2, t2 = _bs_int(m, b)
    return p1 * p2, q1 * q2, t2 * p1 + t1 * q2


def pi_decimal_bs(precision: int) -> str:
    n_terms = (precision + 1) // 14 + 3
    p, q, t = _bs_int(1, n_terms)

    _dctx().prec = precision + 30
    d_q = Decimal(q)
    d_t = Decimal(t)
    sqrt10005 = Decimal(10005).sqrt()
    term0 = Decimal(13591409)
    s = term0 * d_q + d_t
    pi = Decimal(426880) * sqrt10005 * d_q / s

    _dctx().prec = precision + 3
    pi = +pi
    full = format(pi, f".{precision+1}f")
    return full[:precision + 2]


# ──────────────────────────────────────────────
# 引擎 3：Decimal 线性迭代（兜底）
# ──────────────────────────────────────────────

def pi_decimal_linear(precision: int) -> str:
    _dctx().prec = precision + 30

    tm = Decimal(1)
    n = Decimal(13591409)
    s = tm * n
    k = 0
    eps = Decimal(10) ** (-(precision + 25))
    c3 = Decimal(640320) ** 3

    while True:
        k += 1
        tm = tm * (-24) * (6 * k - 5) * (2 * k - 1) * (6 * k - 1)
        tm = tm / (Decimal(k) ** 3 * c3)
        n += Decimal(545140134)
        term = tm * n
        if abs(term) < eps:
            break
        s += term

    pi = Decimal(426880) * Decimal(10005).sqrt() / s
    _dctx().prec = precision + 3
    pi = +pi
    full = format(pi, f".{precision+1}f")
    return full[:precision + 2]


# ──────────────────────────────────────────────
# 增量 BS 引擎（缓存中间结果，避免重复计算）
# ──────────────────────────────────────────────

class IncrementalBS:
    """
    增量 Binary Splitting 计算器。

    缓存 BS(1, n) 的 (P, Q, T)，扩展时只算新区间再合并，
    避免每次从头重算。
    """
    def __init__(self):
        self.p = None
        self.q = None
        self.t = None
        self.n = 0  # 当前缓存的项数上限（exclusive）

    def extend_to(self, n_target: int) -> None:
        """确保已算到 n_target 项。只增量计算新增部分。"""
        if n_target <= self.n:
            return
        if self.p is None:
            self.p, self.q, self.t = _bs_mpz(1, n_target)
        else:
            p_new, q_new, t_new = _bs_mpz(self.n, n_target)
            p_merged = self.p * p_new
            q_merged = self.q * q_new
            t_merged = t_new * self.p + self.t * q_new
            self.p, self.q, self.t = p_merged, q_merged, t_merged
        self.n = n_target

    def to_pi(self, precision: int) -> str:
        """从当前缓存计算 π 字符串。"""
        from gmpy2 import mpfr
        bits = int((precision + 1) * math.log2(10)) + 128
        gmpy2.get_context().precision = bits

        sqrt10005 = gmpy2.sqrt(mpfr(10005))
        term0 = mpfr(13591409)
        s = term0 * mpfr(self.q) + mpfr(self.t)
        pi = mpfr(426880) * sqrt10005 * mpfr(self.q) / s

        full = format(pi, f".{precision+1}f")
        return full[:precision + 2]


# ──────────────────────────────────────────────
# 引擎注册
# ──────────────────────────────────────────────

ENGINES = []

def _init_engines():
    if HAS_GMPY2:
        ENGINES.append(("gmpy2", pi_gmpy2))
    ENGINES.append(("bigint+Decimal", pi_decimal_bs))
    ENGINES.append(("Decimal-linear", pi_decimal_linear))

def select_engine(name: str = "auto"):
    if not ENGINES:
        _init_engines()
    if name == "auto":
        return ENGINES[0]
    for label, fn in ENGINES:
        if label == name or name in label:
            return (label, fn)
    raise ValueError(f"unknown engine '{name}'")

def list_engines() -> str:
    if not ENGINES:
        _init_engines()
    lines = ["Available engines (fastest first):"]
    for i, (label, _) in enumerate(ENGINES):
        lines.append(f"  {i+1}. {label}{' (default)' if i == 0 else ''}")
    return "\n".join(lines)


# ──────────────────────────────────────────────
# 实时流式模式 —— 自适应批大小
# ──────────────────────────────────────────────

def _fmt_speed(dps: float) -> str:
    if dps >= 1_000_000:
        return f"{dps/1_000_000:.1f}M"
    if dps >= 1_000:
        return f"{dps/1_000:.1f}K"
    return f"{dps:.0f}"


def _nterms_to_prec(n: int) -> int:
    """项数 → 对应的小数位数（估算）。"""
    return max(1, (n - 3) * 14)


# ── BS 缓存文件的保存与恢复 ──

def _cache_path(output_file: str) -> str:
    """输出文件对应的缓存文件路径。"""
    return output_file + ".cache"


def save_cache(incr, output_file: str) -> None:
    """将 BS 缓存状态 (P, Q, T, n) 写入缓存文件。"""
    if incr is None or not HAS_GMPY2 or incr.p is None:
        return
    path = _cache_path(output_file)
    tmp = path + ".tmp"
    try:
        with open(tmp, "w", encoding="utf-8") as f:
            f.write(f"{incr.n}\n")
            f.write(str(incr.p) + "\n")
            f.write(str(incr.q) + "\n")
            f.write(str(incr.t) + "\n")
        os.replace(tmp, path)  # 原子替换
    except Exception:
        try:
            os.remove(tmp)
        except Exception:
            pass


def load_cache(output_file: str):
    """从缓存文件恢复 BS 缓存。返回 IncrementalBS 或 None。
    校验：必须读到 4 个有效值。"""
    from gmpy2 import mpz
    path = _cache_path(output_file)
    try:
        with open(path, "r", encoding="utf-8") as f:
            lines = f.readlines()
        if len(lines) != 4:
            return None
        n = int(lines[0].strip())
        p = mpz(lines[1].strip())
        q = mpz(lines[2].strip())
        t = mpz(lines[3].strip())
        if n <= 0 or p <= 0 or q <= 0 or t <= 0:
            return None
        incr = IncrementalBS()
        incr.p, incr.q, incr.t = p, q, t
        incr.n = n
        return incr
    except Exception:
        return None


def live_compute(precision_start: int, engine_fn, engine_label: str,
                 output_file: str = "") -> None:
    """
    实时流式计算，自适应批大小。

    策略：每批结束后用实测速度（项数/秒）估算下一批大小，
    使每批耗时稳定在 target_interval 附近（默认 200ms）。
    初始阶段用保守的几何增长，快速积累吞吐量数据后过渡到自适应。
    """
    target_interval = 0.20  # 200ms
    min_batch_terms = 5
    max_batch_terms = 1_000_000

    start_time = time.perf_counter()

    # ── 尝试恢复缓存 ──
    incr = None
    if HAS_GMPY2:
        incr = load_cache(output_file) if output_file else None
    if incr is None and HAS_GMPY2:
        incr = IncrementalBS()

    resumed = incr is not None and incr.n > 0 if HAS_GMPY2 else False

    # ── 打开输出文件 ──
    fout = None
    if output_file:
        # 恢复模式：追加；否则新建
        mode = "a" if resumed else "w"
        fout = open(output_file, mode, encoding="utf-8", buffering=1)

    # ── 输出已有数字 ──
    if resumed:
        # 从缓存重建 previous：把缓存算到当前精度的 π
        cur_prec = _nterms_to_prec(incr.n)
        result = incr.to_pi(cur_prec)
        previous = result
        sys.stdout.write(previous)
        sys.stdout.flush()
        if fout:
            pass  # 追加模式，已有内容不动
    else:
        previous = "3."
        sys.stdout.write("3.")
        sys.stdout.flush()
        if fout:
            fout.write("3.")
            fout.flush()

    n_terms = max(10, precision_start // 14 + 3)
    if resumed:
        # 从缓存项数开始，继续增长
        n_terms = max(n_terms, incr.n + min_batch_terms)
    n_prev = incr.n if resumed else 0
    batch_count = 0
    total_digits = len(previous) - 2 if resumed else 0

    # 自适应参数
    ema_tp = None
    alpha = 0.3
    growth_phase = not resumed  # 恢复模式直接进自适应

    if resumed:
        sys.stderr.write(f"  Resumed from cache ({incr.n} terms, {total_digits:,} digits)\n")

    try:
        while True:
            batch_count += 1

            # ── 计算 ──
            t0 = time.perf_counter()

            if incr:
                incr.extend_to(n_terms)
                cur_prec = _nterms_to_prec(n_terms)
                result = incr.to_pi(cur_prec)
            else:
                cur_prec = _nterms_to_prec(n_terms)
                result = engine_fn(cur_prec)

            dt = time.perf_counter() - t0
            if dt < 1e-9:
                dt = 1e-9
            new_terms = n_terms - n_prev if n_prev > 0 else n_terms
            n_prev = n_terms
            actual_rate = new_terms / dt  # 新增项数/秒

            # ── 输出新增数字 ──
            new_part = result[len(previous):]
            previous = result

            sys.stdout.write(new_part)
            sys.stdout.flush()
            if fout:
                fout.write(new_part)
                fout.flush()

            elapsed = time.perf_counter() - start_time
            total_digits = len(result) - 2
            batch_digits = len(new_part)

            # ── 更新吞吐量 EMA ──
            if ema_tp is None:
                ema_tp = actual_rate
            else:
                ema_tp = alpha * actual_rate + (1 - alpha) * ema_tp

            # ── 估算下一批大小 ──
            if growth_phase:
                n_terms = int(n_terms * 1.5)
                if batch_count >= 3:
                    growth_phase = False
            else:
                # 自适应：在现有项数基础上，加上吞吐量 × 目标间隔
                # 得到下一批的总项数。只增不减（保证单调性）。
                new_target = int(ema_tp * target_interval) if ema_tp else n_terms
                n_terms = max(
                    n_terms + min_batch_terms,
                    min(int(n_terms * 5), n_terms + new_target)
                )

            # 更新 n_terms（用于下次的增量计算）并限幅
            n_terms = max(min_batch_terms, min(max_batch_terms, n_terms))

            # 保存缓存到文件
            if incr and output_file:
                save_cache(incr, output_file)

            # ── 状态栏 ──
            avg_rate = total_digits / elapsed if elapsed > 0 else 0

            # ── 状态栏 ──
            # 不使用 `\r`（会和 stdout 的光标位置冲突），
            # 改为每行单独输出到 stderr。终端中看起来像在底部滚动。
            avg_rate = total_digits / elapsed if elapsed > 0 else 0
            file_tag = f" [{os.path.basename(output_file)}]" if fout else ""
            sys.stderr.write(
                f"  [{engine_label}] "
                f"{total_digits:>7,} dig "
                f"+{batch_digits:>5,} "
                f"{_fmt_speed(avg_rate):>5}/s "
                f"{elapsed:>4.1f}s"
                f"{file_tag} Ctrl+C\n"
            )
            sys.stderr.flush()

    except KeyboardInterrupt:
        elapsed = time.perf_counter() - start_time
        sys.stderr.write("\n")
        sys.stderr.write(f"  Stopped. Total {total_digits:,} digits, "
                         f"{elapsed:.1f}s, "
                         f"avg {_fmt_speed(total_digits/elapsed)}/s\n")
    finally:
        if fout:
            fout.close()
            if total_digits > 0:
                size = os.path.getsize(output_file)
                sys.stderr.write(f"  Saved to {output_file} ({size:,} bytes)\n")


# ──────────────────────────────────────────────
# CLI
# ──────────────────────────────────────────────

def main():
    _init_engines()

    import argparse
    ap = argparse.ArgumentParser(
        description="Pi (pi) high precision calculator",
        epilog=(
            "Examples:\n"
            "  pi.py                    100 digits\n"
            "  pi.py 5000               5000 digits\n"
            "  pi.py 100000 -o pi.txt   save to file\n"
            "  pi.py --live             live streaming (adaptive)\n"
            "  pi.py --live -o pi.txt   live + file save\n"
            "  pi.py --list-algo        list available engines"
        ),
    )
    ap.add_argument("precision", nargs="?", type=int, default=100,
                    help="digits (default 100; in --live mode, initial batch size)")
    ap.add_argument("-o", "--output", type=str,
                    help="output file (live mode appends)")
    ap.add_argument("-q", "--quiet", action="store_true",
                    help="quiet: output digits only")
    ap.add_argument("--algo", type=str, default="auto",
                    help="engine selection")
    ap.add_argument("--list-algo", action="store_true",
                    help="list engines and exit")
    ap.add_argument("--verify", action="store_true",
                    help="verify against first 50 known digits")
    ap.add_argument("--live", action="store_true",
                    help="live streaming mode (adaptive batch sizing)")

    args = ap.parse_args()

    if args.list_algo:
        print(list_engines())
        sys.exit(0)

    try:
        label, algo_fn = select_engine(args.algo)
    except ValueError as e:
        print(e, file=sys.stderr)
        sys.exit(1)

    # ── live mode ──
    if args.live:
        live_compute(
            precision_start=max(100, args.precision),
            engine_fn=algo_fn,
            engine_label=label,
            output_file=args.output or "pi_live.txt",
        )
        return

    # ── single-shot mode ──
    prec = args.precision
    if prec < 1:
        print("precision must be >= 1", file=sys.stderr)
        sys.exit(1)

    if not args.quiet:
        print(f"digits: {prec:,} | engine: {label}", file=sys.stderr)
        print(file=sys.stderr)

    t0 = time.perf_counter()
    try:
        pi_str = algo_fn(prec)
    except MemoryError:
        print("out of memory. try installing gmpy2 or lower precision.",
              file=sys.stderr)
        sys.exit(1)
    except Exception as e:
        print(f"computation failed: {e}", file=sys.stderr)
        sys.exit(1)
    elapsed = time.perf_counter() - t0

    if args.output:
        with open(args.output, "w", encoding="utf-8") as f:
            f.write(pi_str + "\n")
        if not args.quiet:
            sz = os.path.getsize(args.output)
            print(f"saved to {args.output} ({sz:,} bytes)", file=sys.stderr)
    else:
        sys.stdout.write(pi_str + "\n")

    if not args.quiet:
        t_str = f"{elapsed*1000:.1f} ms" if elapsed < 10 else f"{elapsed:.2f} s"
        print(f"time: {t_str}", file=sys.stderr)

    if args.verify and prec >= 50:
        expected = "3.14159265358979323846264338327950288419716939937510"
        actual = pi_str[:len(expected)]
        ok = actual == expected
        if not args.quiet:
            print(f"verify: {'OK' if ok else 'FAIL'}", file=sys.stderr)


if __name__ == "__main__":
    main()
