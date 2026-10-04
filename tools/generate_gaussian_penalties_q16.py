#!/usr/bin/env python3
import argparse
from decimal import Decimal, ROUND_HALF_UP, getcontext
from pathlib import Path


def round_q16(value: Decimal) -> int:
    scaled = value * Decimal(65536)
    if scaled >= 0:
        return int(scaled.to_integral_value(rounding=ROUND_HALF_UP))
    return -int((-scaled).to_integral_value(rounding=ROUND_HALF_UP))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--decay-base", default="0.934")
    parser.add_argument("output")
    args = parser.parse_args()

    getcontext().prec = 80
    rows = []
    for g in range(1, 256):
        sigma = Decimal(3) * Decimal(g + 3).sqrt()
        row = []
        for f in range(1, 256):
            ratio = Decimal(f - g) / sigma
            value = Decimal("-0.5") * ratio * ratio
            if value < Decimal(-16):
                value = Decimal(-16)
            row.append(round_q16(value))
        rows.append(row)

    decay = round_q16(Decimal(args.decay_base).ln())
    body = []
    for row in rows:
        body.append("  {{" + ", ".join(str(value) for value in row) + "}},")

    output = Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(
        "#ifndef LOCAL_ACR_FINGERPRINT_GAUSSIAN_PENALTIES_Q16_HPP\n"
        "#define LOCAL_ACR_FINGERPRINT_GAUSSIAN_PENALTIES_Q16_HPP\n\n"
        "#include <array>\n"
        "#include <cstdint>\n\n"
        "namespace local_acr {\n\n"
        f"inline constexpr std::int32_t kThresholdDecayQ16 = {decay};\n\n"
        "inline constexpr std::array<std::array<std::int32_t, 255>, 255> "
        "kGaussianPenaltiesQ16 = {{\n"
        + "\n".join(body)
        + "\n}};\n\n"
        "}  // namespace local_acr\n\n"
        "#endif\n",
        encoding="utf-8",
    )

    print(f"decay={decay} self={rows[6][6]} far={rows[0][254]} near={rows[16][19]}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
