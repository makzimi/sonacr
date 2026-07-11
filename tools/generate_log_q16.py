#!/usr/bin/env python3
import argparse
from decimal import Decimal, ROUND_HALF_UP, getcontext
from pathlib import Path


def round_q16(value: Decimal) -> int:
    scaled = value * Decimal(65536)
    if scaled >= 0:
        return int(scaled.to_integral_value(rounding=ROUND_HALF_UP))
    return -int((-scaled).to_integral_value(rounding=ROUND_HALF_UP))


def format_array(values, per_line):
    lines = []
    for offset in range(0, len(values), per_line):
        chunk = values[offset : offset + per_line]
        lines.append("  " + ", ".join(f"{value}" for value in chunk) + ",")
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("log_table_cpp")
    parser.add_argument("frequency_weights_hpp")
    args = parser.parse_args()

    getcontext().prec = 80

    ln_mantissa = [round_q16((Decimal(1) + Decimal(i) / Decimal(65536)).ln()) for i in range(65536)]
    ln_weights = [round_q16(Decimal(i + 16).ln()) for i in range(256)]

    log_cpp = Path(args.log_table_cpp)
    log_cpp.parent.mkdir(parents=True, exist_ok=True)
    log_cpp.write_text(
        "#include \"fingerprint/log_q16.hpp\"\n\n"
        "namespace local_acr {\n\n"
        "const std::array<std::int32_t, 65536> kLnMantissaQ16 = {{\n"
        f"{format_array(ln_mantissa, 8)}\n"
        "}};\n\n"
        "}  // namespace local_acr\n",
        encoding="utf-8",
    )

    weights_hpp = Path(args.frequency_weights_hpp)
    weights_hpp.parent.mkdir(parents=True, exist_ok=True)
    weights_hpp.write_text(
        "#ifndef LOCAL_ACR_FINGERPRINT_FREQUENCY_WEIGHTS_Q16_HPP\n"
        "#define LOCAL_ACR_FINGERPRINT_FREQUENCY_WEIGHTS_Q16_HPP\n\n"
        "#include <array>\n"
        "#include <cstdint>\n\n"
        "namespace local_acr {\n\n"
        "inline constexpr std::array<std::int32_t, 256> kLnWeightQ16 = {{\n"
        f"{format_array(ln_weights, 8)}\n"
        "}};\n\n"
        "}  // namespace local_acr\n\n"
        "#endif\n",
        encoding="utf-8",
    )

    print(
        f"ln_mantissa_mid={ln_mantissa[32768]} "
        f"ln_mantissa_last={ln_mantissa[-1]} weight255={ln_weights[255]}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
