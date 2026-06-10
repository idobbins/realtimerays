#!/usr/bin/env bash
# Record Metal GPU performance-limiter counters for the headless renderer
# and print busy-window statistics. Usage:
#   [RTR_SPP=N] tools/gpucounters.sh [frames]
# Requires Xcode (xctrace). The binary must already be built.
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$script_dir/.."

frames="${1:-400}"
trace="$(mktemp -d)/rtr.trace"

xctrace record --instrument 'Metal GPU Counters' --output "$trace" \
    --time-limit 30s --launch -- /bin/sh -c \
    "./build/realtimerays --xpost-raw 1920 1080 $frames 30 > /dev/null" \
    > /dev/null 2>&1

values="$(mktemp).xml"
xctrace export --input "$trace" \
    --xpath '/trace-toc/run[@number="1"]/data/table[@schema="gpu-counter-value"]' \
    --output "$values" > /dev/null 2>&1

python3 - "$values" <<'EOF'
import sys
import xml.etree.ElementTree as ET

NAMES = {0:'ALU Limiter',1:'ALU Utilization',2:'F32 Utilization',3:'F16 Utilization',
4:'Tex Sample Limiter',7:'Tex Cache Limiter',10:'Buffer Read Limiter',
11:'Buffer Load Util',12:'Buffer Write Limiter',14:'TG/IB Load Limiter',
20:'Last Level Cache Limiter',24:'Compute Occupancy',25:'Read BW (GB/s)',
26:'Write BW (GB/s)',27:'MMU Limiter',29:'MMU TLB Miss Rate'}

ids = {}
def resolve(el):
    if el.get('id') is not None:
        ids[el.get('id')] = el.text
        return el.text
    return ids.get(el.get('ref'))

by_time = []
for ev, el in ET.iterparse(sys.argv[1], events=('end',)):
    if el.tag != 'row':
        continue
    cells = list(el)
    ts = resolve(cells[0]); cid = resolve(cells[1]); val = resolve(cells[2])
    for c in cells[3:]:
        resolve(c)
    try:
        cid = int(cid); val = float(val); ts = int(ts)
    except (TypeError, ValueError):
        el.clear(); continue
    if not by_time or by_time[-1][0] != ts:
        by_time.append((ts, {}))
    by_time[-1][1][cid] = val
    el.clear()

busy = [d for (ts, d) in by_time if d.get(24, 0.0) > 5.0]
print(f"samples: {len(busy)} busy / {len(by_time)} total")
print(f"{'counter':<28}{'busy mean':>11}{'p95':>9}")
for cid in sorted(NAMES):
    vals = sorted(d[cid] for d in busy if cid in d)
    if not vals:
        continue
    mean = sum(vals) / len(vals)
    p95 = vals[int(0.95 * (len(vals) - 1))]
    print(f"{NAMES[cid]:<28}{mean:>11.2f}{p95:>9.2f}")
EOF

rm -rf "$trace" "$values"
